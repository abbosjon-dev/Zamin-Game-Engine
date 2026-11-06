#include "Zamin/Renderer/ParticleRenderer.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/VertexArray.h"

#include <algorithm>

namespace Zamin {

    namespace {

        // The corners as SubmitQuad / DrawCircle place them (unit quad, counter-clockwise), the
        // UVs and the circle's local position made from them as those do.
        const char* s_ParticleVS = R"(#version 410 core
layout(location = 0) in vec2 a_Corner;              // -0.5 .. 0.5
layout(location = 1) in vec4 a_PositionRotation;    // instance: xyz centre, w rotation (radians)
layout(location = 2) in vec2 a_Size;
layout(location = 3) in vec4 a_Color;
layout(location = 4) in vec4 a_UVRect;

uniform mat4 u_ViewProjection;
uniform vec3 u_Right;
uniform vec3 u_Up;

out vec4 v_Color;
out vec2 v_TexCoord;
out vec2 v_Local;

void main()
{
    float c = cos(a_PositionRotation.w), s = sin(a_PositionRotation.w);
    vec2 p = a_Corner * a_Size;
    vec2 r = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
    vec3 world = a_PositionRotation.xyz + u_Right * r.x + u_Up * r.y;
    v_Color = a_Color;
    v_TexCoord = mix(a_UVRect.xy, a_UVRect.zw, a_Corner + 0.5);
    v_Local = a_Corner * 2.0;
    gl_Position = u_ViewProjection * vec4(world, 1.0);
}
)";

        // Renderer2D's quad shader (its texture, no effects, not lit) and its circle shader.
        const char* s_ParticleFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

in vec4 v_Color;
in vec2 v_TexCoord;
in vec2 v_Local;

uniform sampler2D u_Texture;
uniform int u_Mode;           // 0: plain quad, 1: textured quad, 2: circle
uniform int u_LinearTexture;  // 1: the texture holds linear values (import setting sRGB off)
uniform int u_DisplayOutput;  // 1: display (sRGB) colour
uniform int u_EntityID;
uniform float u_Thickness;
uniform float u_Fade;

void main()
{
    if (u_Mode == 2)
    {
        float distance = 1.0 - length(v_Local);
        float circle = smoothstep(0.0, u_Fade, distance);
        circle *= smoothstep(u_Thickness + u_Fade, u_Thickness, distance);
        if (circle == 0.0)
            discard;
        o_Color = vec4(u_DisplayOutput == 1 ? v_Color.rgb : pow(v_Color.rgb, vec3(2.2)), v_Color.a * circle);
        o_EntityID = u_EntityID;
        return;
    }
    vec4 texColor = u_Mode == 1 ? texture(u_Texture, v_TexCoord) : vec4(1.0);
    vec4 color = texColor * v_Color;
    vec3 rgb = u_LinearTexture == 1 ? pow(v_Color.rgb, vec3(2.2)) * texColor.rgb : pow(color.rgb, vec3(2.2));
    float alpha = color.a;
    if (alpha < 0.01)
        discard;
    o_Color = vec4(u_DisplayOutput == 1 ? pow(max(rgb, vec3(0.0)), vec3(1.0 / 2.2)) : rgb, alpha);
    o_EntityID = u_EntityID;
}
)";

        struct ParticleRendererData
        {
            Ref<VertexArray> Array;
            Ref<VertexBuffer> Instances;
            Ref<Shader> Program;
        };

        ParticleRendererData* s_Data = nullptr;

    }

    void ParticleRenderer::Init()
    {
        s_Data = new ParticleRendererData();
        s_Data->Array = VertexArray::Create();
        const float corners[] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };
        Ref<VertexBuffer> quad = VertexBuffer::Create(corners, sizeof(corners));
        quad->SetLayout({ { ShaderDataType::Float2, "a_Corner" } });
        s_Data->Array->AddVertexBuffer(quad);
        s_Data->Instances = VertexBuffer::Create(MaxInstancesPerDraw * (uint32_t)sizeof(ParticleInstance));
        s_Data->Instances->SetLayout(BufferLayout({
            { ShaderDataType::Float4, "a_PositionRotation" },
            { ShaderDataType::Float2, "a_Size" },
            { ShaderDataType::Float4, "a_Color" },
            { ShaderDataType::Float4, "a_UVRect" },
        }).SetPerInstance(true));
        s_Data->Array->AddVertexBuffer(s_Data->Instances);
        const uint32_t indices[] = { 0, 1, 2, 2, 3, 0 }; // as the 2D batch's quads
        s_Data->Array->SetIndexBuffer(IndexBuffer::Create(indices, 6));
        s_Data->Program = Shader::Create("Particles_Instanced", s_ParticleVS, s_ParticleFS);
        s_Data->Program->Bind();
        s_Data->Program->SetInt("u_Texture", 0);
    }

    void ParticleRenderer::Shutdown()
    {
        delete s_Data;
        s_Data = nullptr;
    }

    bool ParticleRenderer::IsAvailable() { return s_Data && s_Data->Program && s_Data->Program->IsValid(); }

    void ParticleRenderer::Draw(const ParticleInstance* instances, uint32_t count, const DrawParams& params)
    {
        if (!IsAvailable() || count == 0)
            return;
        ZM_PROFILE_SCOPE("Particle'lar (instanced)");
        static_assert(sizeof(ParticleInstance) == 14 * sizeof(float), "the instance layout above");
        Renderer2D::Flush(); // what was submitted before is drawn first
        Shader& shader = *s_Data->Program;
        shader.Bind();
        shader.SetMat4("u_ViewProjection", Renderer2D::GetViewProjection());
        shader.SetFloat3("u_Right", params.Right);
        shader.SetFloat3("u_Up", params.Up);
        shader.SetInt("u_DisplayOutput", Renderer2D::IsDisplayOutput() ? 1 : 0);
        shader.SetInt("u_EntityID", params.EntityID);
        if (params.Kind == Shape::Circle)
        {
            shader.SetInt("u_Mode", 2);
            shader.SetFloat("u_Thickness", params.CircleThickness);
            shader.SetFloat("u_Fade", params.CircleFade);
        }
        else if (params.Texture)
        {
            shader.SetInt("u_Mode", 1);
            shader.SetInt("u_LinearTexture", params.Texture->GetSpecification().SRGB ? 0 : 1);
            params.Texture->Bind(0);
        }
        else
            shader.SetInt("u_Mode", 0);
        auto& stats = Renderer::GetStats();
        for (uint32_t first = 0; first < count; first += MaxInstancesPerDraw)
        {
            const uint32_t n = std::min(MaxInstancesPerDraw, count - first);
            s_Data->Instances->StreamData(instances + first, n * (uint32_t)sizeof(ParticleInstance));
            RenderCommand::DrawIndexedInstanced(s_Data->Array, 6, n);
            stats.DrawCalls++;
            stats.InstancedParticleDrawCount++;
        }
        stats.InstancedParticleCount += count;
    }

}
