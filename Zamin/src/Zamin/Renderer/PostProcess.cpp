#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Core/Profiler.h"

#include <glm/glm.hpp>
#include <vector>

namespace Zamin {

    namespace {

        const char* s_FullscreenVS = R"(#version 410 core
out vec2 v_UV;
void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_UV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

        // 13-tap downsample (Jimenez 2014) with optional soft threshold on the first pass.
        const char* s_DownsampleFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_TexelSize;
uniform int u_Prefilter;
uniform float u_Threshold;
uniform float u_Knee;

vec3 Prefilter(vec3 c)
{
    float brightness = max(c.r, max(c.g, c.b));
    float soft = brightness - u_Threshold + u_Knee;
    soft = clamp(soft, 0.0, 2.0 * u_Knee);
    soft = soft * soft / (4.0 * u_Knee + 1e-4);
    float contribution = max(soft, brightness - u_Threshold) / max(brightness, 1e-4);
    return c * contribution;
}

void main()
{
    vec2 t = u_TexelSize;
    vec3 a = texture(u_Source, v_UV + t * vec2(-2, 2)).rgb;
    vec3 b = texture(u_Source, v_UV + t * vec2( 0, 2)).rgb;
    vec3 c = texture(u_Source, v_UV + t * vec2( 2, 2)).rgb;
    vec3 d = texture(u_Source, v_UV + t * vec2(-2, 0)).rgb;
    vec3 e = texture(u_Source, v_UV).rgb;
    vec3 f = texture(u_Source, v_UV + t * vec2( 2, 0)).rgb;
    vec3 g = texture(u_Source, v_UV + t * vec2(-2,-2)).rgb;
    vec3 h = texture(u_Source, v_UV + t * vec2( 0,-2)).rgb;
    vec3 i = texture(u_Source, v_UV + t * vec2( 2,-2)).rgb;
    vec3 j = texture(u_Source, v_UV + t * vec2(-1, 1)).rgb;
    vec3 k = texture(u_Source, v_UV + t * vec2( 1, 1)).rgb;
    vec3 l = texture(u_Source, v_UV + t * vec2(-1,-1)).rgb;
    vec3 m = texture(u_Source, v_UV + t * vec2( 1,-1)).rgb;
    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (u_Prefilter == 1)
        color = Prefilter(color);
    o_Color = vec4(color, 1.0);
}
)";

        // 3x3 tent upsample, blended additively into the next larger mip.
        const char* s_UpsampleFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_TexelSize;
uniform float u_Radius;
void main()
{
    vec2 t = u_TexelSize * u_Radius;
    vec3 c = texture(u_Source, v_UV + vec2(-t.x, t.y)).rgb
           + texture(u_Source, v_UV + vec2(0.0, t.y)).rgb * 2.0
           + texture(u_Source, v_UV + vec2(t.x, t.y)).rgb
           + texture(u_Source, v_UV + vec2(-t.x, 0.0)).rgb * 2.0
           + texture(u_Source, v_UV).rgb * 4.0
           + texture(u_Source, v_UV + vec2(t.x, 0.0)).rgb * 2.0
           + texture(u_Source, v_UV + vec2(-t.x, -t.y)).rgb
           + texture(u_Source, v_UV + vec2(0.0, -t.y)).rgb * 2.0
           + texture(u_Source, v_UV + vec2(t.x, -t.y)).rgb;
    o_Color = vec4(c / 16.0, 1.0);
}
)";

        const char* s_CompositeFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform int u_BloomEnabled;
uniform float u_BloomIntensity;
uniform float u_Vignette;
uniform float u_Saturation;
uniform float u_Contrast;
uniform float u_Brightness;
uniform vec3 u_Tint;
uniform int u_Tonemap;

vec3 ACES(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 color = max(texture(u_Scene, v_UV).rgb, vec3(0.0));
    if (u_BloomEnabled == 1)
        color += texture(u_Bloom, v_UV).rgb * u_BloomIntensity;

    if (u_Tonemap == 1) color = ACES(color);
    else if (u_Tonemap == 2) color = color / (1.0 + color);
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));

    color *= u_Tint;
    color += u_Brightness;
    color = (color - 0.5) * u_Contrast + 0.5;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, u_Saturation);

    if (u_Vignette > 0.0)
    {
        vec2 d = v_UV - 0.5;
        float v = smoothstep(0.85, 0.25, length(d) * (0.6 + u_Vignette));
        color *= mix(1.0, v, u_Vignette);
    }
    o_Color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

        struct PostData
        {
            Ref<Shader> Downsample, Upsample, Composite;
            std::vector<Ref<Framebuffer>> Mips;
            uint32_t Width = 0, Height = 0;
        };
        PostData* s_Data = nullptr;
        constexpr int s_MipCount = 6;

        void EnsureMips(uint32_t width, uint32_t height)
        {
            if (s_Data->Width == width && s_Data->Height == height && !s_Data->Mips.empty())
                return;
            s_Data->Width = width;
            s_Data->Height = height;
            s_Data->Mips.clear();
            uint32_t w = width, h = height;
            for (int i = 0; i < s_MipCount; i++)
            {
                w = std::max(1u, w / 2);
                h = std::max(1u, h / 2);
                FramebufferSpecification spec;
                spec.Width = w;
                spec.Height = h;
                spec.Attachments = { FramebufferTextureFormat::RGBA16F };
                s_Data->Mips.push_back(Framebuffer::Create(spec));
                if (w == 1 && h == 1) break;
            }
        }
    }

    void PostProcessor::Init()
    {
        s_Data = new PostData();
        s_Data->Downsample = Shader::Create("Post_Downsample", s_FullscreenVS, s_DownsampleFS);
        s_Data->Upsample = Shader::Create("Post_Upsample", s_FullscreenVS, s_UpsampleFS);
        s_Data->Composite = Shader::Create("Post_Composite", s_FullscreenVS, s_CompositeFS);
    }

    void PostProcessor::Shutdown()
    {
        delete s_Data;
        s_Data = nullptr;
    }

    void PostProcessor::Apply(uint32_t inputTexture, uint32_t width, uint32_t height, const PostProcessSettings& settings,
                              const Ref<Framebuffer>& target)
    {
        if (!s_Data || width == 0 || height == 0)
            return;

        ZM_PROFILE_SCOPE("Post-processing");
        RenderCommand::SaveRenderTarget();
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthWrite(false);
        auto& stats = Renderer::GetStats();

        uint32_t bloomTexture = 0;
        if (settings.Bloom)
        {
            EnsureMips(width, height);
            RenderCommand::SetBlendMode(BlendMode::None);

            // Downsample chain (first pass with threshold)
            auto& down = s_Data->Downsample;
            down->Bind();
            down->SetInt("u_Source", 0);
            down->SetFloat("u_Threshold", settings.BloomThreshold);
            down->SetFloat("u_Knee", std::max(0.001f, settings.BloomKnee));
            uint32_t source = inputTexture;
            uint32_t srcW = width, srcH = height;
            for (size_t i = 0; i < s_Data->Mips.size(); i++)
            {
                auto& mip = s_Data->Mips[i];
                mip->Bind();
                RenderCommand::BindTextureUnit(0, source);
                down->SetFloat2("u_TexelSize", { 1.0f / srcW, 1.0f / srcH });
                down->SetInt("u_Prefilter", i == 0 ? 1 : 0);
                RenderCommand::DrawArrays(3);
                stats.DrawCalls++;
                source = mip->GetColorAttachmentRendererID(0);
                srcW = mip->GetSpecification().Width;
                srcH = mip->GetSpecification().Height;
            }

            // Upsample + accumulate
            auto& up = s_Data->Upsample;
            up->Bind();
            up->SetInt("u_Source", 0);
            up->SetFloat("u_Radius", settings.BloomRadius);
            RenderCommand::SetBlendMode(BlendMode::Additive);
            for (int i = (int)s_Data->Mips.size() - 1; i > 0; i--)
            {
                auto& small = s_Data->Mips[i];
                auto& large = s_Data->Mips[i - 1];
                large->Bind();
                RenderCommand::BindTextureUnit(0, small->GetColorAttachmentRendererID(0));
                up->SetFloat2("u_TexelSize", { 1.0f / small->GetSpecification().Width, 1.0f / small->GetSpecification().Height });
                RenderCommand::DrawArrays(3);
                stats.DrawCalls++;
            }
            bloomTexture = s_Data->Mips[0]->GetColorAttachmentRendererID(0);
        }

        // Composite
        RenderCommand::RestoreRenderTarget();
        if (target)
            target->Bind();
        RenderCommand::SetBlendMode(BlendMode::None);
        auto& comp = s_Data->Composite;
        comp->Bind();
        comp->SetInt("u_Scene", 0);
        comp->SetInt("u_Bloom", 1);
        RenderCommand::BindTextureUnit(0, inputTexture);
        RenderCommand::BindTextureUnit(1, bloomTexture);
        comp->SetInt("u_BloomEnabled", settings.Bloom ? 1 : 0);
        comp->SetFloat("u_BloomIntensity", settings.BloomIntensity);
        comp->SetFloat("u_Vignette", settings.Vignette);
        comp->SetFloat("u_Saturation", settings.Saturation);
        comp->SetFloat("u_Contrast", settings.Contrast);
        comp->SetFloat("u_Brightness", settings.Brightness);
        comp->SetFloat3("u_Tint", settings.Tint);
        comp->SetInt("u_Tonemap", (int)settings.Tonemap);
        RenderCommand::DrawArrays(3);
        stats.DrawCalls++;

        RenderCommand::SetBlendMode(BlendMode::Alpha);
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);
    }

}
