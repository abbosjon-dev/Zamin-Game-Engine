#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Core/Profiler.h"

#include <algorithm>
#include <cmath>
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

        const char* s_SSAOFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Depth;
uniform mat4 u_Projection;
uniform mat4 u_InvProjection;
uniform vec2 u_TexelSize;   // of the depth texture
uniform float u_Radius;
uniform float u_Bias;
uniform float u_Intensity;

vec3 ViewPos(vec2 uv)
{
    float d = texture(u_Depth, uv).r;
    vec4 p = u_InvProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

float Hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main()
{
    float depth = texture(u_Depth, v_UV).r;
    if (depth >= 0.99999) { o_Color = vec4(1.0); return; }
    vec3 P = ViewPos(v_UV);
    // normal from neighbouring depths (choose the smaller difference to avoid edge artifacts)
    vec3 px = ViewPos(v_UV + vec2(u_TexelSize.x, 0)), nx = ViewPos(v_UV - vec2(u_TexelSize.x, 0));
    vec3 py = ViewPos(v_UV + vec2(0, u_TexelSize.y)), ny = ViewPos(v_UV - vec2(0, u_TexelSize.y));
    vec3 dx = abs(px.z - P.z) < abs(P.z - nx.z) ? px - P : P - nx;
    vec3 dy = abs(py.z - P.z) < abs(P.z - ny.z) ? py - P : P - ny;
    vec3 N = normalize(cross(dx, dy));

    float angle = Hash(gl_FragCoord.xy) * 6.2831853;
    vec3 randomVec = vec3(cos(angle), sin(angle), 0.0);
    vec3 T = normalize(randomVec - N * dot(randomVec, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    const int SAMPLES = 16;
    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; i++)
    {
        // deterministic hemisphere kernel, denser near the center
        float fi = float(i);
        float r = fract(sin(fi * 91.7) * 4375.85);
        float a = fi * 2.39996;
        float z = fract(sin(fi * 13.1) * 9137.3) * 0.9 + 0.1;
        vec3 k = normalize(vec3(cos(a) * sqrt(1.0 - z * z), sin(a) * sqrt(1.0 - z * z), z));
        float scale = mix(0.1, 1.0, (fi / SAMPLES) * (fi / SAMPLES));
        vec3 S = P + TBN * k * (u_Radius * scale * (0.5 + 0.5 * r));

        vec4 offset = u_Projection * vec4(S, 1.0);
        offset.xy = offset.xy / offset.w * 0.5 + 0.5;
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) continue;
        float sceneZ = ViewPos(offset.xy).z;
        float range = smoothstep(0.0, 1.0, u_Radius / max(abs(P.z - sceneZ), 1e-4));
        occlusion += (sceneZ >= S.z + u_Bias ? 1.0 : 0.0) * range;
    }
    float ao = 1.0 - occlusion / float(SAMPLES);
    ao = mix(ao, 1.0, smoothstep(25.0, 60.0, -P.z)); // fade out in the distance (noise)
    o_Color = vec4(vec3(pow(clamp(ao, 0.0, 1.0), u_Intensity)), 1.0);
}
)";

        const char* s_BlurFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_TexelSize;
void main()
{
    float sum = 0.0;
    for (int x = -2; x <= 1; x++)
        for (int y = -2; y <= 1; y++)
            sum += texture(u_Source, v_UV + (vec2(x, y) + 0.5) * u_TexelSize).r;
    o_Color = vec4(vec3(sum / 16.0), 1.0);
}
)";

        const char* s_CompositeFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform sampler2D u_AO;
uniform int u_AOEnabled;
uniform int u_BloomEnabled;
uniform float u_BloomIntensity;
uniform float u_Vignette;
uniform float u_Saturation;
uniform float u_Contrast;
uniform float u_Brightness;
uniform vec3 u_Tint;
uniform int u_Tonemap;
uniform int u_AutoExposure;
uniform sampler2D u_Exposure;      // 1x1: the adapted average brightness (log2 luminance)
uniform float u_ExposureScale;     // key x 2^compensation

vec3 ACES(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 color = max(texture(u_Scene, v_UV).rgb, vec3(0.0));
    if (u_AOEnabled == 1)
        color *= texture(u_AO, v_UV).r;
    if (u_BloomEnabled == 1)
        color += texture(u_Bloom, v_UV).rgb * u_BloomIntensity;
    if (u_AutoExposure == 1)
        color *= u_ExposureScale / exp2(texelFetch(u_Exposure, ivec2(0), 0).r);

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

        // FXAA (after Timothy Lottes' FXAA 3.11, quality preset): on the tonemapped, gamma
        // encoded picture. Finds edges by the contrast of the luma around each pixel, walks along
        // the edge to both of its ends, and blends across it by how far the pixel is from the
        // nearer end; a subpixel term softens single-pixel detail.
        const char* s_FxaaFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_TexelSize;
uniform float u_Subpixel;

const float EDGE_THRESHOLD_MIN = 0.0312;
const float EDGE_THRESHOLD_MAX = 0.125;
const int ITERATIONS = 12;

float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float Quality(int i) { return i < 5 ? 1.0 : (i == 5 ? 1.5 : (i < 10 ? 2.0 : (i == 10 ? 4.0 : 8.0))); }

void main()
{
    vec3 colorCenter = textureLod(u_Source, v_UV, 0.0).rgb;
    float lumaCenter = Luma(colorCenter);
    float lumaDown = Luma(textureLodOffset(u_Source, v_UV, 0.0, ivec2(0, -1)).rgb);
    float lumaUp = Luma(textureLodOffset(u_Source, v_UV, 0.0, ivec2(0, 1)).rgb);
    float lumaLeft = Luma(textureLodOffset(u_Source, v_UV, 0.0, ivec2(-1, 0)).rgb);
    float lumaRight = Luma(textureLodOffset(u_Source, v_UV, 0.0, ivec2(1, 0)).rgb);
    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;
    // flat area: nothing to smooth
    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX))
    {
        o_Color = vec4(colorCenter, 1.0);
        return;
    }

    float lumaDownLeft = Luma(textureLodOffset(u_Source, v_UV, 0.0, ivec2(-1, -1)).rgb);
    float lumaUpRight = Luma(textureLodOffset(u_Source, v_UV, 0.0, ivec2(1, 1)).rgb);
    float lumaUpLeft = Luma(textureLodOffset(u_Source, v_UV, 0.0, ivec2(-1, 1)).rgb);
    float lumaDownRight = Luma(textureLodOffset(u_Source, v_UV, 0.0, ivec2(1, -1)).rgb);
    float lumaDownUp = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;
    float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
    float lumaDownCorners = lumaDownLeft + lumaDownRight;
    float lumaRightCorners = lumaDownRight + lumaUpRight;
    float lumaUpCorners = lumaUpRight + lumaUpLeft;

    // edge direction
    float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0
                         + abs(-2.0 * lumaRight + lumaRightCorners);
    float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0
                       + abs(-2.0 * lumaDown + lumaDownCorners);
    bool isHorizontal = edgeHorizontal >= edgeVertical;

    // which side of the pixel the edge is on
    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp : lumaRight;
    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;
    bool is1Steepest = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
    float stepLength = isHorizontal ? u_TexelSize.y : u_TexelSize.x;
    float lumaLocalAverage;
    if (is1Steepest)
    {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    }
    else
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    vec2 currentUV = v_UV;
    if (isHorizontal) currentUV.y += stepLength * 0.5;
    else currentUV.x += stepLength * 0.5;

    // walk along the edge in both directions until its contrast is gone
    vec2 offset = isHorizontal ? vec2(u_TexelSize.x, 0.0) : vec2(0.0, u_TexelSize.y);
    vec2 uv1 = currentUV - offset, uv2 = currentUV + offset;
    float lumaEnd1 = Luma(textureLod(u_Source, uv1, 0.0).rgb) - lumaLocalAverage;
    float lumaEnd2 = Luma(textureLod(u_Source, uv2, 0.0).rgb) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;
    for (int i = 2; i < ITERATIONS && !(reached1 && reached2); i++)
    {
        if (!reached1) lumaEnd1 = Luma(textureLod(u_Source, uv1, 0.0).rgb) - lumaLocalAverage;
        if (!reached2) lumaEnd2 = Luma(textureLod(u_Source, uv2, 0.0).rgb) - lumaLocalAverage;
        reached1 = abs(lumaEnd1) >= gradientScaled;
        reached2 = abs(lumaEnd2) >= gradientScaled;
        if (!reached1) uv1 -= offset * Quality(i);
        if (!reached2) uv2 += offset * Quality(i);
    }

    float distance1 = isHorizontal ? (v_UV.x - uv1.x) : (v_UV.y - uv1.y);
    float distance2 = isHorizontal ? (uv2.x - v_UV.x) : (uv2.y - v_UV.y);
    bool isDirection1 = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeLength = distance1 + distance2;
    float pixelOffset = -distanceFinal / max(edgeLength, 1e-6) + 0.5;
    bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;
    bool correctVariation = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // subpixel aliasing
    float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
    float subPixel1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
    float subPixel2 = (-2.0 * subPixel1 + 3.0) * subPixel1 * subPixel1;
    finalOffset = max(finalOffset, subPixel2 * subPixel2 * u_Subpixel);

    vec2 finalUV = v_UV;
    if (isHorizontal) finalUV.y += finalOffset * stepLength;
    else finalUV.x += finalOffset * stepLength;
    o_Color = vec4(textureLod(u_Source, finalUV, 0.0).rgb, 1.0);
}
)";

        // Auto exposure, step 1: the log2 luminance of the picture, four taps per output texel
        // (each a bilinear average of four texels when the picture is larger).
        const char* s_LogLuminanceFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_OutputTexel;   // of the target
float LogLuminance(vec2 uv)
{
    vec3 c = max(textureLod(u_Source, uv, 0.0).rgb, vec3(0.0));
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    if (isnan(lum) || isinf(lum)) lum = 1.0;
    return log2(clamp(lum, 1e-5, 65000.0));
}
void main()
{
    vec2 q = u_OutputTexel * 0.25;
    float sum = LogLuminance(v_UV + vec2(-q.x, -q.y)) + LogLuminance(v_UV + vec2(q.x, -q.y))
              + LogLuminance(v_UV + vec2(-q.x, q.y)) + LogLuminance(v_UV + vec2(q.x, q.y));
    o_Color = vec4(sum * 0.25, 0.0, 0.0, 1.0);
}
)";

        // Step 2: halving downsamples; one bilinear tap on the corner of four texels is their mean.
        const char* s_HalveFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Source;
void main()
{
    o_Color = vec4(textureLod(u_Source, v_UV, 0.0).r, 0.0, 0.0, 1.0);
}
)";

        // Step 3: the adapted brightness moves towards the measured one (held within min..max EV).
        const char* s_AdaptFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
uniform sampler2D u_Average;   // 1x1 mean log2 luminance of this frame
uniform sampler2D u_Previous;  // 1x1 adapted value of the last frame
uniform float u_MinEV;
uniform float u_MaxEV;
uniform float u_Blend;         // 1 - e^(-dt * speed)
uniform int u_Reset;
void main()
{
    float target = clamp(texelFetch(u_Average, ivec2(0), 0).r, u_MinEV, u_MaxEV);
    float previous = texelFetch(u_Previous, ivec2(0), 0).r;
    float adapted = (u_Reset == 1 || isnan(previous) || isinf(previous)) ? target : previous + (target - previous) * u_Blend;
    o_Color = vec4(adapted, 0.0, 0.0, 1.0);
}
)";

        // The bloom mip chain of one picture size. A few sizes are kept, so the scene view and small
        // offscreen pictures (material previews, thumbnails) drawn in the same frame do not make each
        // other create their framebuffers again.
        struct MipChain
        {
            uint32_t Width = 0, Height = 0;
            int Levels = 0;
            uint64_t LastUsed = 0;
            std::vector<Ref<Framebuffer>> Mips;
        };
        constexpr size_t s_MaxMipChains = 4;

        struct PostData
        {
            Ref<Shader> Downsample, Upsample, Composite, SSAO, Blur, Fxaa;
            Ref<Shader> LogLuminance, Halve, Adapt;
            std::vector<Ref<Framebuffer>> LuminanceChain; // 256x256 down to 1x1 (log2 luminance)
            PostProcessHistory OwnHistory;                // views without a history of their own
            std::vector<MipChain> Chains;
            uint64_t ChainUses = 0;
            Ref<Framebuffer> AO, AOBlur;
            Ref<Framebuffer> FxaaInput; // the composited (LDR) picture FXAA reads
        };
        PostData* s_Data = nullptr;
        constexpr int s_MipCount = 6;
        constexpr uint32_t s_LuminanceSize = 256;

        Ref<Framebuffer> CreateColorTarget(uint32_t width, uint32_t height)
        {
            FramebufferSpecification spec;
            spec.Width = width;
            spec.Height = height;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F };
            return Framebuffer::Create(spec);
        }

        // Auto exposure: measures the picture and moves the history's adapted brightness;
        // returns the texture holding it (1x1, log2 luminance).
        uint32_t UpdateAutoExposure(uint32_t inputTexture, const PostProcessSettings& settings, float deltaTime,
                                    bool& valid, int& current, Ref<Framebuffer>* adapted)
        {
            auto& stats = Renderer::GetStats();
            if (s_Data->LuminanceChain.empty())
                for (uint32_t size = s_LuminanceSize; size >= 1; size /= 2)
                    s_Data->LuminanceChain.push_back(CreateColorTarget(size, size));
            RenderCommand::SetBlendMode(BlendMode::None);

            auto& lum = s_Data->LogLuminance;
            s_Data->LuminanceChain[0]->Bind();
            lum->Bind();
            lum->SetInt("u_Source", 0);
            lum->SetFloat2("u_OutputTexel", { 1.0f / s_LuminanceSize, 1.0f / s_LuminanceSize });
            RenderCommand::BindTextureUnit(0, inputTexture);
            RenderCommand::DrawArrays(3);
            stats.DrawCalls++;

            auto& halve = s_Data->Halve;
            halve->Bind();
            halve->SetInt("u_Source", 0);
            for (size_t i = 1; i < s_Data->LuminanceChain.size(); i++)
            {
                s_Data->LuminanceChain[i]->Bind();
                RenderCommand::BindTextureUnit(0, s_Data->LuminanceChain[i - 1]->GetColorAttachmentRendererID(0));
                RenderCommand::DrawArrays(3);
                stats.DrawCalls++;
            }

            for (int i = 0; i < 2; i++)
                if (!adapted[i])
                    adapted[i] = CreateColorTarget(1, 1);
            int previous = current;
            current = 1 - current;
            adapted[current]->Bind();
            auto& adapt = s_Data->Adapt;
            adapt->Bind();
            adapt->SetInt("u_Average", 0);
            adapt->SetInt("u_Previous", 1);
            RenderCommand::BindTextureUnit(0, s_Data->LuminanceChain.back()->GetColorAttachmentRendererID(0));
            RenderCommand::BindTextureUnit(1, adapted[previous]->GetColorAttachmentRendererID(0));
            PostProcessSettings s = settings;
            s.SanitizeAutoExposure();
            adapt->SetFloat("u_MinEV", s.AutoExposureMinEV);
            adapt->SetFloat("u_MaxEV", s.AutoExposureMaxEV);
            adapt->SetFloat("u_Blend", PostProcessSettings::AdaptExposure(0.0f, 1.0f, deltaTime, s.AutoExposureSpeed));
            adapt->SetInt("u_Reset", valid ? 0 : 1);
            RenderCommand::DrawArrays(3);
            stats.DrawCalls++;
            valid = true;
            return adapted[current]->GetColorAttachmentRendererID(0);
        }
        constexpr uint32_t s_MinMipSize = 8;

        std::vector<Ref<Framebuffer>>& EnsureMips(uint32_t width, uint32_t height, int levels)
        {
            s_Data->ChainUses++;
            for (MipChain& chain : s_Data->Chains)
                if (chain.Width == width && chain.Height == height && chain.Levels == levels && !chain.Mips.empty())
                {
                    chain.LastUsed = s_Data->ChainUses;
                    return chain.Mips;
                }
            // A new size: a free slot, else the one used least recently.
            MipChain* slot = nullptr;
            if (s_Data->Chains.size() < s_MaxMipChains)
                slot = &s_Data->Chains.emplace_back();
            else
                slot = &*std::min_element(s_Data->Chains.begin(), s_Data->Chains.end(),
                                          [](const MipChain& a, const MipChain& b) { return a.LastUsed < b.LastUsed; });
            slot->Width = width;
            slot->Height = height;
            slot->Levels = levels;
            slot->LastUsed = s_Data->ChainUses;
            slot->Mips.clear();
            // Down to `levels` halvings, but not below a few pixels: in a small picture (a material
            // preview) the last levels would spread the glow over all of it.
            uint32_t w = width, h = height;
            for (int i = 0; i < levels; i++)
            {
                if (i > 0 && std::min(w, h) / 2 < s_MinMipSize)
                    break;
                w = std::max(1u, w / 2);
                h = std::max(1u, h / 2);
                FramebufferSpecification spec;
                spec.Width = w;
                spec.Height = h;
                spec.Attachments = { FramebufferTextureFormat::RGBA16F };
                slot->Mips.push_back(Framebuffer::Create(spec));
                if (w == 1 && h == 1) break;
            }
            return slot->Mips;
        }
    }

    void PostProcessor::Init()
    {
        s_Data = new PostData();
        s_Data->Downsample = Shader::Create("Post_Downsample", s_FullscreenVS, s_DownsampleFS);
        s_Data->Upsample = Shader::Create("Post_Upsample", s_FullscreenVS, s_UpsampleFS);
        s_Data->Composite = Shader::Create("Post_Composite", s_FullscreenVS, s_CompositeFS);
        s_Data->SSAO = Shader::Create("Post_SSAO", s_FullscreenVS, s_SSAOFS);
        s_Data->Blur = Shader::Create("Post_Blur", s_FullscreenVS, s_BlurFS);
        s_Data->Fxaa = Shader::Create("Post_FXAA", s_FullscreenVS, s_FxaaFS);
        s_Data->LogLuminance = Shader::Create("Post_LogLuminance", s_FullscreenVS, s_LogLuminanceFS);
        s_Data->Halve = Shader::Create("Post_Halve", s_FullscreenVS, s_HalveFS);
        s_Data->Adapt = Shader::Create("Post_Adapt", s_FullscreenVS, s_AdaptFS);
    }

    void PostProcessor::Shutdown()
    {
        delete s_Data;
        s_Data = nullptr;
    }

    PostProcessSettings PostProcessSettings::Neutral()
    {
        PostProcessSettings s;
        s.Tonemap = TonemapMode::None;
        s.Bloom = false;
        s.SSAO = false;
        s.Vignette = 0.0f;
        s.Saturation = 1.0f;
        s.Contrast = 1.0f;
        s.Brightness = 0.0f;
        s.Tint = glm::vec3(1.0f);
        s.FXAA = false;
        s.AutoExposure = false;
        return s;
    }

    float PostProcessSettings::GetAutoExposureMultiplier(float averageEV) const
    {
        PostProcessSettings s = *this;
        s.SanitizeAutoExposure();
        float ev = std::isfinite(averageEV) ? std::clamp(averageEV, s.AutoExposureMinEV, s.AutoExposureMaxEV)
                                            : 0.0f;
        return AutoExposureKey * std::exp2(s.AutoExposureCompensation) / std::exp2(ev);
    }

    float PostProcessSettings::AdaptExposure(float currentEV, float targetEV, float deltaTime, float speed)
    {
        if (!std::isfinite(currentEV))
            return targetEV;
        if (!(deltaTime > 0.0f) || !(speed > 0.0f) || !std::isfinite(deltaTime) || !std::isfinite(speed))
            return currentEV;
        float blend = 1.0f - std::exp(-deltaTime * speed);
        return currentEV + (targetEV - currentEV) * blend;
    }

    void PostProcessSettings::SanitizeAutoExposure()
    {
        auto finiteOr = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        AutoExposureMinEV = std::clamp(finiteOr(AutoExposureMinEV, -4.0f), MinAutoExposureEV, MaxAutoExposureEV);
        AutoExposureMaxEV = std::clamp(finiteOr(AutoExposureMaxEV, 4.0f), MinAutoExposureEV, MaxAutoExposureEV);
        if (AutoExposureMinEV > AutoExposureMaxEV)
            std::swap(AutoExposureMinEV, AutoExposureMaxEV);
        AutoExposureCompensation = std::clamp(finiteOr(AutoExposureCompensation, 0.0f), -10.0f, 10.0f);
        AutoExposureSpeed = std::clamp(finiteOr(AutoExposureSpeed, 2.0f), 0.01f, 100.0f);
    }

    bool PostProcessHistory::ReadAdaptedEV(float& outEV) const
    {
        const Ref<Framebuffer>& fb = m_Adapted[m_Current];
        float rgba[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (!m_Valid || !fb || !fb->ReadPixelFloat(0, 0, 0, rgba))
            return false;
        outEV = rgba[0];
        return true;
    }

    void PostProcessor::Apply(uint32_t inputTexture, uint32_t width, uint32_t height, const PostProcessSettings& settings,
                              const Ref<Framebuffer>& target, uint32_t depthTexture, const glm::mat4& projection)
    {
        PostProcessInput input;
        input.ColorTexture = inputTexture;
        input.Width = width;
        input.Height = height;
        input.DepthTexture = depthTexture;
        input.Projection = projection;
        Apply(input, settings, target);
    }

    void PostProcessor::Apply(const PostProcessInput& input, const PostProcessSettings& settings, const Ref<Framebuffer>& target)
    {
        const uint32_t inputTexture = input.ColorTexture, width = input.Width, height = input.Height;
        const uint32_t depthTexture = input.DepthTexture;
        const glm::mat4& projection = input.Projection;
        if (!s_Data || width == 0 || height == 0)
            return;
        // The size written: the picture is scaled there by the composite pass (linear filtering).
        const uint32_t outWidth = input.OutputWidth ? input.OutputWidth : width;
        const uint32_t outHeight = input.OutputHeight ? input.OutputHeight : height;

        ZM_PROFILE_SCOPE("Post-processing");
        RenderCommand::SaveRenderTarget();
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthWrite(false);
        auto& stats = Renderer::GetStats();

        // ---- SSAO (half resolution + blur), only for perspective projections
        uint32_t aoTexture = 0;
        bool ssao = settings.SSAO && depthTexture != 0 && projection[3][3] == 0.0f;
        if (ssao)
        {
            uint32_t hw = std::max(1u, width / 2), hh = std::max(1u, height / 2);
            for (Ref<Framebuffer>* fb : { &s_Data->AO, &s_Data->AOBlur })
            {
                if (!*fb)
                {
                    FramebufferSpecification spec;
                    spec.Width = hw; spec.Height = hh;
                    spec.Attachments = { FramebufferTextureFormat::RGBA8 };
                    *fb = Framebuffer::Create(spec);
                }
                else if ((*fb)->GetSpecification().Width != hw || (*fb)->GetSpecification().Height != hh)
                    (*fb)->Resize(hw, hh);
            }
            RenderCommand::SetBlendMode(BlendMode::None);
            s_Data->AO->Bind();
            auto& sh = s_Data->SSAO;
            sh->Bind();
            sh->SetInt("u_Depth", 0);
            RenderCommand::BindTextureUnit(0, depthTexture);
            sh->SetMat4("u_Projection", projection);
            sh->SetMat4("u_InvProjection", glm::inverse(projection));
            sh->SetFloat2("u_TexelSize", { 1.0f / width, 1.0f / height });
            sh->SetFloat("u_Radius", settings.SSAORadius);
            sh->SetFloat("u_Bias", settings.SSAOBias);
            sh->SetFloat("u_Intensity", settings.SSAOIntensity);
            RenderCommand::DrawArrays(3);

            s_Data->AOBlur->Bind();
            s_Data->Blur->Bind();
            s_Data->Blur->SetInt("u_Source", 0);
            RenderCommand::BindTextureUnit(0, s_Data->AO->GetColorAttachmentRendererID(0));
            s_Data->Blur->SetFloat2("u_TexelSize", { 1.0f / hw, 1.0f / hh });
            RenderCommand::DrawArrays(3);
            aoTexture = s_Data->AOBlur->GetColorAttachmentRendererID(0);
            stats.DrawCalls += 2;
        }

        uint32_t bloomTexture = 0;
        if (settings.Bloom)
        {
            // The same glow at every render scale: one level more for each doubling of the picture
            // over the output size (the widest level stays the same size on screen).
            int levels = s_MipCount;
            if (outWidth != width)
                levels = std::clamp(s_MipCount + (int)std::lround(std::log2((float)width / (float)outWidth)), 2, s_MipCount + 2);
            std::vector<Ref<Framebuffer>>& mips = EnsureMips(width, height, levels);
            RenderCommand::SetBlendMode(BlendMode::None);

            // Downsample chain (first pass with threshold)
            auto& down = s_Data->Downsample;
            down->Bind();
            down->SetInt("u_Source", 0);
            down->SetFloat("u_Threshold", settings.BloomThreshold);
            down->SetFloat("u_Knee", std::max(0.001f, settings.BloomKnee));
            uint32_t source = inputTexture;
            uint32_t srcW = width, srcH = height;
            for (size_t i = 0; i < mips.size(); i++)
            {
                auto& mip = mips[i];
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
            for (int i = (int)mips.size() - 1; i > 0; i--)
            {
                auto& small = mips[i];
                auto& large = mips[i - 1];
                large->Bind();
                RenderCommand::BindTextureUnit(0, small->GetColorAttachmentRendererID(0));
                up->SetFloat2("u_TexelSize", { 1.0f / small->GetSpecification().Width, 1.0f / small->GetSpecification().Height });
                RenderCommand::DrawArrays(3);
                stats.DrawCalls++;
            }
            bloomTexture = mips[0]->GetColorAttachmentRendererID(0);
        }

        // Auto exposure: this view's adapted brightness (views without a history: measured fresh).
        uint32_t exposureTexture = 0;
        const bool autoExposure = settings.AutoExposure && s_Data->LogLuminance && s_Data->LogLuminance->IsValid() &&
                                  s_Data->Halve->IsValid() && s_Data->Adapt->IsValid();
        PostProcessHistory& history = input.History ? *input.History : s_Data->OwnHistory;
        if (!input.History)
            history.Reset();
        if (autoExposure)
            exposureTexture = UpdateAutoExposure(inputTexture, settings, input.DeltaTime, history.m_Valid, history.m_Current,
                                                 history.m_Adapted);
        else
            history.Reset(); // switched on later: adapts at once, not from a stale value

        // Composite: straight into the target, or (FXAA) into an LDR picture FXAA reads.
        RenderCommand::RestoreRenderTarget();
        const bool fxaa = settings.FXAA && s_Data->Fxaa && s_Data->Fxaa->IsValid();
        if (fxaa)
        {
            if (!s_Data->FxaaInput)
            {
                FramebufferSpecification spec;
                spec.Width = outWidth;
                spec.Height = outHeight;
                spec.Attachments = { FramebufferTextureFormat::RGBA8 }; // linear filtering, clamped
                s_Data->FxaaInput = Framebuffer::Create(spec);
            }
            else if (s_Data->FxaaInput->GetSpecification().Width != outWidth || s_Data->FxaaInput->GetSpecification().Height != outHeight)
                s_Data->FxaaInput->Resize(outWidth, outHeight);
            s_Data->FxaaInput->Bind();
        }
        else if (target)
            target->Bind();
        RenderCommand::SetBlendMode(BlendMode::None);
        auto& comp = s_Data->Composite;
        comp->Bind();
        comp->SetInt("u_Scene", 0);
        comp->SetInt("u_Bloom", 1);
        comp->SetInt("u_AO", 2);
        RenderCommand::BindTextureUnit(0, inputTexture);
        RenderCommand::BindTextureUnit(1, bloomTexture);
        RenderCommand::BindTextureUnit(2, aoTexture);
        comp->SetInt("u_AOEnabled", ssao ? 1 : 0);
        comp->SetInt("u_BloomEnabled", settings.Bloom ? 1 : 0);
        comp->SetFloat("u_BloomIntensity", settings.BloomIntensity);
        comp->SetFloat("u_Vignette", settings.Vignette);
        comp->SetFloat("u_Saturation", settings.Saturation);
        comp->SetFloat("u_Contrast", settings.Contrast);
        comp->SetFloat("u_Brightness", settings.Brightness);
        comp->SetFloat3("u_Tint", settings.Tint);
        comp->SetInt("u_Tonemap", (int)settings.Tonemap);
        comp->SetInt("u_Exposure", 3);
        comp->SetInt("u_AutoExposure", exposureTexture ? 1 : 0);
        RenderCommand::BindTextureUnit(3, exposureTexture);
        if (exposureTexture)
        {
            PostProcessSettings s = settings;
            s.SanitizeAutoExposure();
            comp->SetFloat("u_ExposureScale", PostProcessSettings::AutoExposureKey * std::exp2(s.AutoExposureCompensation));
        }
        RenderCommand::DrawArrays(3);
        stats.DrawCalls++;

        // FXAA: the composited picture, anti-aliased into the target.
        if (fxaa)
        {
            RenderCommand::RestoreRenderTarget();
            if (target)
                target->Bind();
            auto& aa = s_Data->Fxaa;
            aa->Bind();
            aa->SetInt("u_Source", 0);
            RenderCommand::BindTextureUnit(0, s_Data->FxaaInput->GetColorAttachmentRendererID(0));
            aa->SetFloat2("u_TexelSize", { 1.0f / outWidth, 1.0f / outHeight });
            aa->SetFloat("u_Subpixel", std::clamp(settings.FXAASubpixel, 0.0f, 1.0f));
            RenderCommand::DrawArrays(3);
            stats.DrawCalls++;
        }

        RenderCommand::SetBlendMode(BlendMode::Alpha);
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);
    }

}
