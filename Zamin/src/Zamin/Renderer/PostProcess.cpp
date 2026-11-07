#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/GraphicsContext.h"
#include "Zamin/Renderer/Quality.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/Texture.h"
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

        // Film grain, shared by the last pass of the picture (the composite, or FXAA after it):
        // value noise of hashed lattice points, a new pattern every frame (u_GrainSeed).
        const char* s_GrainGLSL = R"(
uniform int u_LinearOutput;    // 1: the window's sRGB framebuffer encodes the picture (it is written linear)
uniform float u_FilmGrain;     // 0: no grain
uniform float u_GrainSize;     // output pixels per grain
uniform int u_GrainSeed;

uint GrainHash(uint x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float GrainLattice(ivec2 p, uint seed)
{
    return float(GrainHash(uint(p.x) + GrainHash(uint(p.y) + seed)) & 0xffffffU) / 16777215.0;
}
float GrainValue(vec2 p, uint seed)
{
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    ivec2 c = ivec2(i);
    float a = GrainLattice(c, seed), b = GrainLattice(c + ivec2(1, 0), seed);
    float d = GrainLattice(c + ivec2(0, 1), seed), e = GrainLattice(c + ivec2(1, 1), seed);
    return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}
// The display colour as the target wants it: as it is, or back to linear for an sRGB framebuffer
// (the hardware encodes it with the exact sRGB curve).
vec3 FinalOutput(vec3 color)
{
    return u_LinearOutput == 1 ? pow(color, vec3(2.2)) : color;
}
// Display colour with grain over it: two noise values (a softer, triangular spread), strongest in
// the mid tones (dark and bright parts keep their black and white).
vec3 ApplyGrain(vec3 color, vec2 fragCoord)
{
    if (u_FilmGrain <= 0.0)
        return color;
    vec2 p = (fragCoord - 0.5) / max(u_GrainSize, 1.0);
    uint seed = GrainHash(uint(u_GrainSeed) * 2u + 1u);
    float n = GrainValue(p, seed) + GrainValue(p + vec2(17.31, 41.87), seed ^ 0x9e3779b9U) - 1.0;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float response = 1.0 - 0.7 * pow(abs(luma * 2.0 - 1.0), 2.0);
    return clamp(color + n * (u_FilmGrain * 0.16 * response), 0.0, 1.0);
}
)";

        // Inserts the grain functions after the #version line of a fragment shader.
        std::string WithGrain(const char* src)
        {
            std::string s(src);
            size_t eol = s.find('\n');
            return s.substr(0, eol + 1) + s_GrainGLSL + s.substr(eol + 1);
        }

        const char* s_CompositeFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
in vec2 v_UV;
uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform sampler2D u_BloomWide;     // a wider level of the glow (what the lens dirt is lit by)
uniform sampler2D u_DirtMap;
uniform sampler2D u_AO;
uniform int u_AOEnabled;
uniform int u_BloomEnabled;
uniform float u_BloomIntensity;
uniform vec3 u_BloomTint;
uniform float u_BloomDirt;
uniform float u_ChromaticAberration;
uniform int u_ApplyGrain;          // 1: this pass writes the final picture (no FXAA after it)
uniform float u_Vignette;
uniform float u_Saturation;
uniform float u_Contrast;
uniform float u_Brightness;
uniform vec3 u_Tint;
uniform int u_Tonemap;
uniform int u_AutoExposure;
uniform sampler2D u_Exposure;      // 1x1: the adapted average brightness (log2 luminance)
uniform float u_ExposureScale;     // key x 2^compensation
uniform int u_PixelZoom;           // > 0: each scene texel covers that many output pixels (sharp)
uniform vec2 u_PixelOffset;        // output pixels the centred picture starts before the view

vec3 ACES(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    // Pixel-perfect upscaling: the texel under this output pixel, sampled at its centre.
    vec2 uv = v_UV;
    if (u_PixelZoom > 0)
    {
        vec2 size = vec2(textureSize(u_Scene, 0));
        vec2 texel = floor((gl_FragCoord.xy - 0.5 + u_PixelOffset) / float(u_PixelZoom));
        uv = (texel + 0.5) / size;
    }
    vec3 color;
    if (u_ChromaticAberration > 0.0)
    {
        // red and blue taken from a little further out / further in, more towards the edges
        vec2 d = uv - 0.5;
        vec2 shift = d * dot(d, d) * (0.06 * u_ChromaticAberration);
        color = vec3(texture(u_Scene, uv - shift).r, texture(u_Scene, uv).g, texture(u_Scene, uv + shift).b);
        color = max(color, vec3(0.0));
    }
    else
        color = max(texture(u_Scene, uv).rgb, vec3(0.0));
    if (u_AOEnabled == 1)
        color *= texture(u_AO, uv).r;
    if (u_BloomEnabled == 1)
    {
        vec3 glow = texture(u_Bloom, v_UV).rgb;
        if (u_BloomDirt > 0.0)
            glow += texture(u_BloomWide, v_UV).rgb * texture(u_DirtMap, v_UV).rgb * (u_BloomDirt * 6.0);
        color += glow * u_BloomIntensity * u_BloomTint;
    }
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
    color = clamp(color, 0.0, 1.0);
    if (u_ApplyGrain == 1)
        color = ApplyGrain(color, gl_FragCoord.xy);
    o_Color = vec4(FinalOutput(color), 1.0);
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
        o_Color = vec4(FinalOutput(ApplyGrain(colorCenter, gl_FragCoord.xy)), 1.0);
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
    o_Color = vec4(FinalOutput(ApplyGrain(textureLod(u_Source, finalUV, 0.0).rgb, gl_FragCoord.xy)), 1.0);
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

        // Single targets (SSAO, FXAA's input) of a few sizes: several views drawn every frame (the
        // editor's extra scene views) reuse theirs instead of resizing one back and forth.
        struct SizedTarget
        {
            uint32_t Width = 0, Height = 0;
            uint64_t LastUsed = 0;
            Ref<Framebuffer> Target;
        };
        constexpr size_t s_MaxSizedTargets = 4;

        struct PostData
        {
            Ref<Shader> Downsample, Upsample, Composite, SSAO, Blur, Fxaa;
            Ref<Shader> LogLuminance, Halve, Adapt;
            std::vector<Ref<Framebuffer>> LuminanceChain; // 256x256 down to 1x1 (log2 luminance)
            PostProcessHistory OwnHistory;                // views without a history of their own
            std::vector<MipChain> Chains;
            uint64_t ChainUses = 0;
            std::vector<SizedTarget> AO, AOBlur;
            std::vector<SizedTarget> FxaaInput; // the composited (LDR) picture FXAA reads
            uint64_t TargetUses = 0;
            Ref<Texture2D> LensDirt;    // made the first time a scene uses it
            Ref<Texture2D> Black;       // bound where a texture is not used
        };
        PostData* s_Data = nullptr;
        constexpr int s_MipCount = 6;
        constexpr uint32_t s_LuminanceSize = 256;

        // The RGBA8 target of `cache` with this size: kept, else a new one (in place of the one
        // used least recently once the cache is full).
        const Ref<Framebuffer>& GetSizedTarget(std::vector<SizedTarget>& cache, uint32_t width, uint32_t height)
        {
            const uint64_t use = ++s_Data->TargetUses;
            for (SizedTarget& t : cache)
                if (t.Width == width && t.Height == height && t.Target)
                {
                    t.LastUsed = use;
                    return t.Target;
                }
            SizedTarget* slot = nullptr;
            if (cache.size() < s_MaxSizedTargets)
                slot = &cache.emplace_back();
            else
                slot = &*std::min_element(cache.begin(), cache.end(), [](const SizedTarget& a, const SizedTarget& b) { return a.LastUsed < b.LastUsed; });
            slot->Width = width;
            slot->Height = height;
            slot->LastUsed = use;
            if (slot->Target)
                slot->Target->Resize(width, height);
            else
            {
                FramebufferSpecification spec;
                spec.Width = width;
                spec.Height = height;
                spec.Attachments = { FramebufferTextureFormat::RGBA8 }; // linear filtering, clamped
                slot->Target = Framebuffer::Create(spec);
            }
            return slot->Target;
        }

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
        s_Data->Composite = Shader::Create("Post_Composite", s_FullscreenVS, WithGrain(s_CompositeFS));
        s_Data->SSAO = Shader::Create("Post_SSAO", s_FullscreenVS, s_SSAOFS);
        s_Data->Blur = Shader::Create("Post_Blur", s_FullscreenVS, s_BlurFS);
        s_Data->Fxaa = Shader::Create("Post_FXAA", s_FullscreenVS, WithGrain(s_FxaaFS));
        s_Data->LogLuminance = Shader::Create("Post_LogLuminance", s_FullscreenVS, s_LogLuminanceFS);
        s_Data->Halve = Shader::Create("Post_Halve", s_FullscreenVS, s_HalveFS);
        s_Data->Adapt = Shader::Create("Post_Adapt", s_FullscreenVS, s_AdaptFS);

        TextureSpecification black;
        black.GenerateMips = false;
        black.SRGB = false;
        black.Wrap = TextureWrap::ClampToEdge;
        s_Data->Black = Texture2D::Create(black);
        uint32_t zero = 0xff000000;
        s_Data->Black->SetData(&zero, 4);
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
        s.ChromaticAberration = 0.0f;
        s.FilmGrain = 0.0f;
        s.BloomTint = glm::vec3(1.0f);
        s.BloomDirt = 0.0f;
        return s;
    }

    void PostProcessSettings::SanitizeLensEffects()
    {
        auto finiteOr = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        ChromaticAberration = std::clamp(finiteOr(ChromaticAberration, 0.0f), 0.0f, 1.0f);
        FilmGrain = std::clamp(finiteOr(FilmGrain, 0.0f), 0.0f, 1.0f);
        FilmGrainSize = std::clamp(finiteOr(FilmGrainSize, 1.0f), 1.0f, MaxFilmGrainSize);
        BloomDirt = std::clamp(finiteOr(BloomDirt, 0.0f), 0.0f, 1.0f);
        for (int i = 0; i < 3; i++)
            BloomTint[i] = std::clamp(finiteOr(BloomTint[i], 1.0f), 0.0f, 10.0f);
    }

    std::vector<uint8_t> PostProcessor::GenerateLensDirt(uint32_t size, uint32_t seed)
    {
        size = std::clamp(size, 16u, 4096u);
        std::vector<float> value((size_t)size * size, 0.0f);
        uint32_t state = seed ? seed * 747796405u + 2891336453u : 0x9e3779b9u;
        auto next = [&]() { // xorshift32 in 0..1
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (state >> 8) * (1.0f / 16777216.0f);
        };
        const float scale = size / 512.0f;
        // A soft disc (or a ring, like an out-of-focus speck) added into the picture.
        auto disc = [&](float cx, float cy, float radius, float alpha, float softness, bool ring) {
            int x0 = std::max(0, (int)std::floor(cx - radius)), x1 = std::min((int)size - 1, (int)std::ceil(cx + radius));
            int y0 = std::max(0, (int)std::floor(cy - radius)), y1 = std::min((int)size - 1, (int)std::ceil(cy + radius));
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                {
                    float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / radius;
                    if (d >= 1.0f) continue;
                    float edge = std::clamp((1.0f - d) / std::max(softness, 1e-3f), 0.0f, 1.0f);
                    edge = edge * edge * (3.0f - 2.0f * edge);
                    float shape = ring ? 0.45f + 0.55f * d * d : 1.0f; // brighter rim
                    value[(size_t)y * size + x] += alpha * edge * shape;
                }
        };
        // Each random value is drawn into its own variable in a fixed order: the evaluation order of
        // function arguments is unspecified (GCC and Clang differ), so next() calls inside one argument
        // list would give a different picture per compiler.
        struct Speck { float X, Y, Radius, Alpha; bool Ring; };
        auto speck = [&](float minRadius, float radiusRange, float minAlpha, float alphaRange, float ringChance) {
            Speck s;
            s.X = next() * size;
            s.Y = next() * size;
            s.Radius = minRadius + next() * radiusRange;
            s.Alpha = minAlpha + next() * alphaRange;
            s.Ring = ringChance > 0.0f && next() < ringChance;
            return s;
        };
        // Big faint smudges, out-of-focus specks, then fine dust.
        for (int i = 0; i < 10; i++)
        {
            Speck s = speck(60.0f, 110.0f, 0.04f, 0.06f, 0.0f);
            disc(s.X, s.Y, s.Radius * scale, s.Alpha, 0.9f, false);
        }
        for (int i = 0; i < 36; i++)
        {
            Speck s = speck(8.0f, 24.0f, 0.06f, 0.16f, 0.6f);
            disc(s.X, s.Y, s.Radius * scale, s.Alpha, 0.25f, s.Ring);
        }
        for (int i = 0; i < 160; i++)
        {
            Speck s = speck(0.8f, 2.0f, 0.1f, 0.35f, 0.0f);
            disc(s.X, s.Y, s.Radius * scale + 0.5f, s.Alpha, 0.6f, false);
        }
        std::vector<uint8_t> rgba((size_t)size * size * 4);
        for (size_t i = 0; i < value.size(); i++)
        {
            float v = std::clamp(value[i], 0.0f, 1.0f);
            // a slightly warm grey (dust and grease scatter light a little warmer)
            rgba[i * 4 + 0] = (uint8_t)std::lround(v * 255.0f);
            rgba[i * 4 + 1] = (uint8_t)std::lround(v * 0.96f * 255.0f);
            rgba[i * 4 + 2] = (uint8_t)std::lround(v * 0.9f * 255.0f);
            rgba[i * 4 + 3] = 255;
        }
        return rgba;
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

    void PostProcessor::Apply(const PostProcessInput& input, const PostProcessSettings& sceneSettings, const Ref<Framebuffer>& target)
    {
        // The quality level turns off the effects it does not allow (SSAO, bloom, FXAA, auto exposure).
        const PostProcessSettings settings = Quality::GetActive().ApplyTo(sceneSettings);
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
            const Ref<Framebuffer> ao = GetSizedTarget(s_Data->AO, hw, hh);
            const Ref<Framebuffer> aoBlur = GetSizedTarget(s_Data->AOBlur, hw, hh);
            RenderCommand::SetBlendMode(BlendMode::None);
            ao->Bind();
            auto& sh = s_Data->SSAO;
            sh->Bind();
            sh->SetInt("u_Depth", 0);
            RenderCommand::BindTextureUnit(0, depthTexture);
            sh->SetMat4("u_Projection", projection);
            sh->SetMat4("u_InvProjection", glm::inverse(projection));
            // Normals are rebuilt from depths one output pixel apart (at least one texel): with the
            // render scale above 1, neighbouring texels are so close that the depth buffer's
            // precision would show as stripes on flat surfaces.
            sh->SetFloat2("u_TexelSize", { 1.0f / std::min(width, outWidth), 1.0f / std::min(height, outHeight) });
            sh->SetFloat("u_Radius", settings.SSAORadius);
            sh->SetFloat("u_Bias", settings.SSAOBias);
            sh->SetFloat("u_Intensity", settings.SSAOIntensity);
            RenderCommand::DrawArrays(3);

            aoBlur->Bind();
            s_Data->Blur->Bind();
            s_Data->Blur->SetInt("u_Source", 0);
            RenderCommand::BindTextureUnit(0, ao->GetColorAttachmentRendererID(0));
            s_Data->Blur->SetFloat2("u_TexelSize", { 1.0f / hw, 1.0f / hh });
            RenderCommand::DrawArrays(3);
            aoTexture = aoBlur->GetColorAttachmentRendererID(0);
            stats.DrawCalls += 2;
        }

        uint32_t bloomTexture = 0, bloomWideTexture = 0;
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
            bloomWideTexture = mips[std::min<size_t>(2, mips.size() - 1)]->GetColorAttachmentRendererID(0);
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

        // The lens dirt, made the first time it is used (before the composite's textures are
        // bound: making a texture binds it to the active unit).
        PostProcessSettings lens = settings;
        lens.SanitizeLensEffects();
        const bool dirt = settings.Bloom && lens.BloomDirt > 0.0f;
        if (dirt && !s_Data->LensDirt)
        {
            TextureSpecification spec;
            spec.Width = spec.Height = PostProcessor::LensDirtSize;
            spec.Wrap = TextureWrap::ClampToEdge;
            spec.SRGB = false;
            s_Data->LensDirt = Texture2D::Create(spec);
            std::vector<uint8_t> texels = GenerateLensDirt(PostProcessor::LensDirtSize);
            s_Data->LensDirt->SetData(texels.data(), (uint32_t)texels.size());
        }

        // Composite: straight into the target, or (FXAA) into an LDR picture FXAA reads.
        RenderCommand::RestoreRenderTarget();
        const bool fxaa = settings.FXAA && s_Data->Fxaa && s_Data->Fxaa->IsValid();
        Ref<Framebuffer> fxaaInput;
        if (fxaa)
        {
            fxaaInput = GetSizedTarget(s_Data->FxaaInput, outWidth, outHeight);
            fxaaInput->Bind();
        }
        else if (target)
            target->Bind();
        // The window's framebuffer is sRGB (Project settings > Graphics): the last pass writes linear
        // colours and the hardware encodes them.
        const bool srgbWindow = GraphicsContext::GetInfo().SRGB;
        const bool compositeToSRGB = srgbWindow && !fxaa && !target && RenderCommand::IsDrawingToWindow();
        RenderCommand::SetBlendMode(BlendMode::None);
        // Grain: a new pattern every frame of a view with a history (a still one otherwise).
        const uint32_t grainFrame = input.History ? input.History->m_Frame++ : 0u;
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
        comp->SetFloat3("u_BloomTint", lens.BloomTint);
        comp->SetInt("u_BloomWide", 4);
        comp->SetInt("u_DirtMap", 5);
        comp->SetFloat("u_BloomDirt", dirt ? lens.BloomDirt : 0.0f);
        RenderCommand::BindTextureUnit(4, bloomWideTexture);
        (dirt ? s_Data->LensDirt : s_Data->Black)->Bind(5);
        comp->SetFloat("u_ChromaticAberration", lens.ChromaticAberration);
        // Grain on the last pass (FXAA would smooth it away).
        comp->SetInt("u_ApplyGrain", fxaa ? 0 : 1);
        comp->SetInt("u_LinearOutput", compositeToSRGB ? 1 : 0);
        for (auto* sh : { comp.get(), s_Data->Fxaa.get() })
        {
            sh->Bind();
            sh->SetFloat("u_FilmGrain", lens.FilmGrain);
            sh->SetFloat("u_GrainSize", lens.FilmGrainSize);
            sh->SetInt("u_GrainSeed", (int)(grainFrame & 0x7fffffff));
        }
        comp->Bind();
        comp->SetFloat("u_Vignette", settings.Vignette);
        comp->SetFloat("u_Saturation", settings.Saturation);
        comp->SetFloat("u_Contrast", settings.Contrast);
        comp->SetFloat("u_Brightness", settings.Brightness);
        comp->SetFloat3("u_Tint", settings.Tint);
        comp->SetInt("u_Tonemap", (int)settings.Tonemap);
        comp->SetInt("u_PixelZoom", input.PixelZoom > 0 ? input.PixelZoom : 0);
        // (FXAA: the composite goes into a picture of its own, from its corner.)
        comp->SetFloat2("u_PixelOffset", glm::vec2(input.PixelOffset - (fxaa ? glm::ivec2(0) : input.OutputOrigin)));
        comp->SetInt("u_Exposure", 3);
        comp->SetInt("u_AutoExposure", exposureTexture ? 1 : 0);
        RenderCommand::BindTextureUnit(3, exposureTexture);
        if (exposureTexture)
        {
            PostProcessSettings s = settings;
            s.SanitizeAutoExposure();
            comp->SetFloat("u_ExposureScale", PostProcessSettings::AutoExposureKey * std::exp2(s.AutoExposureCompensation));
        }
        if (compositeToSRGB)
            RenderCommand::SetFramebufferSRGB(true);
        RenderCommand::DrawArrays(3);
        if (compositeToSRGB)
            RenderCommand::SetFramebufferSRGB(false);
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
            RenderCommand::BindTextureUnit(0, fxaaInput->GetColorAttachmentRendererID(0));
            aa->SetFloat2("u_TexelSize", { 1.0f / outWidth, 1.0f / outHeight });
            aa->SetFloat("u_Subpixel", std::clamp(settings.FXAASubpixel, 0.0f, 1.0f));
            const bool fxaaToSRGB = srgbWindow && !target && RenderCommand::IsDrawingToWindow();
            aa->SetInt("u_LinearOutput", fxaaToSRGB ? 1 : 0);
            if (fxaaToSRGB)
                RenderCommand::SetFramebufferSRGB(true);
            RenderCommand::DrawArrays(3);
            if (fxaaToSRGB)
                RenderCommand::SetFramebufferSRGB(false);
            stats.DrawCalls++;
        }

        RenderCommand::SetBlendMode(BlendMode::Alpha);
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);
    }

}
