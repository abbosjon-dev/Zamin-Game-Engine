#include "Zamin/Renderer/Renderer3D.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/Framebuffer.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Zamin {

    namespace {

        const char* s_MeshVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
#ifdef SKINNED
layout(location = 3) in ivec4 a_Joints;
layout(location = 4) in vec4 a_Weights;
uniform mat4 u_Joints[MAX_JOINTS];
#endif

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;
uniform mat3 u_NormalMatrix;
uniform mat4 u_View;

out vec3 v_WorldPos;
out vec3 v_Normal;
out vec2 v_TexCoord;
out float v_ViewDepth;

void main()
{
    vec4 localPos = vec4(a_Position, 1.0);
    vec3 localNormal = a_Normal;
#ifdef SKINNED
    mat4 skin = a_Weights.x * u_Joints[a_Joints.x] + a_Weights.y * u_Joints[a_Joints.y]
              + a_Weights.z * u_Joints[a_Joints.z] + a_Weights.w * u_Joints[a_Joints.w];
    localPos = skin * localPos;
    localNormal = mat3(skin) * localNormal;
#endif
    vec4 world = u_Model * localPos;
    v_WorldPos = world.xyz;
    v_Normal = normalize(u_NormalMatrix * localNormal);
    v_TexCoord = a_TexCoord;
    v_ViewDepth = -(u_View * world).z;
    gl_Position = u_ViewProjection * world;
}
)";

        const char* s_MeshFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_TexCoord;
in float v_ViewDepth;

#define MAX_LOCAL_LIGHTS 32

uniform vec4 u_Albedo;
uniform sampler2D u_AlbedoMap;
uniform sampler2D u_NormalMap;
uniform sampler2D u_MetalRoughMap; // glTF layout: G = roughness, B = metallic
uniform sampler2D u_EmissiveMap;
uniform int u_HasNormalMap;
uniform int u_HasMetalRoughMap;
uniform int u_HasEmissiveMap;
uniform int u_AlbedoSRGB;   // 0: the albedo / emissive map holds linear values (import setting sRGB off)
uniform int u_EmissiveSRGB;
uniform float u_NormalStrength;
uniform vec2 u_Tiling;
uniform vec2 u_Offset;
uniform float u_Metallic;
uniform float u_Roughness;
uniform vec3 u_Emission;
uniform int u_Unlit;          // 1: albedo (x map) + emission only, no lights
uniform int u_BlendMode;      // 0 opaque (alpha ignored), 1 cutout, 2 transparent (premultiplied output)
uniform float u_AlphaCutoff;  // cutout: alpha below this is not drawn
uniform int u_PreserveSpecular; // transparent: highlights, reflections and emission do not fade with alpha
uniform int u_Mirrored;       // the transform has a negative scale: the mesh's front faces wind the other way

uniform vec3 u_CameraPos;
uniform int u_HasSun;
uniform vec3 u_SunDirection;
uniform vec3 u_SunRadiance;
uniform int u_ShadowsEnabled;
uniform sampler2DShadow u_ShadowMap;
uniform float u_ShadowTexel;     // atlas texel size (uv)
uniform mat4 u_LightSpaces[3];
uniform vec3 u_CascadeSplits;     // view depth where each cascade ends
uniform vec3 u_CascadeTexelWorld; // world size of a shadow texel per cascade
uniform int u_CascadeCount;
uniform int u_FogEnabled;
uniform vec3 u_FogColor;
uniform float u_FogDensity;
uniform float u_FogHeightFalloff;
uniform int u_UseIBL;
uniform sampler2D u_EnvMap;
uniform float u_EnvMaxLod;
uniform float u_EnvIntensity;
uniform float u_EnvRotation;
uniform vec3 u_SH[9];

vec2 EquirectUV(vec3 d)
{
    float c = cos(u_EnvRotation), s = sin(u_EnvRotation);
    d = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
    return vec2(atan(d.z, d.x) / 6.2831853 + 0.5, asin(clamp(d.y, -1.0, 1.0)) / 3.14159265 + 0.5);
}

vec3 EvalSH(vec3 n)
{
    float c = cos(u_EnvRotation), s = sin(u_EnvRotation);
    n = vec3(c * n.x - s * n.z, n.y, s * n.x + c * n.z);
    return max(u_SH[0] * 0.282095
         + u_SH[1] * 0.488603 * n.y + u_SH[2] * 0.488603 * n.z + u_SH[3] * 0.488603 * n.x
         + u_SH[4] * 1.092548 * n.x * n.y + u_SH[5] * 1.092548 * n.y * n.z + u_SH[6] * 0.315392 * (3.0 * n.z * n.z - 1.0)
         + u_SH[7] * 1.092548 * n.x * n.z + u_SH[8] * 0.546274 * (n.x * n.x - n.y * n.y), vec3(0.0));
}

// Point and spot lights. Point lights have no direction (zero) and a cone that lets everything
// through (cosines -2 / -1).
uniform int u_LocalLightCount;
uniform vec4 u_LightPosRange[MAX_LOCAL_LIGHTS];  // xyz position, w range
uniform vec4 u_LightRadiance[MAX_LOCAL_LIGHTS];  // rgb color x intensity, w cos(inner angle)
uniform vec4 u_LightDirCos[MAX_LOCAL_LIGHTS];    // xyz direction the light travels, w cos(outer angle)

uniform vec3 u_SkyColor;
uniform vec3 u_HorizonColor;
uniform vec3 u_GroundColor;
uniform float u_Ambient;
uniform float u_Exposure;
uniform int u_EntityID;

const float PI = 3.14159265359;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-5);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Direct light from one source, split into its diffuse and specular parts (transparent surfaces
// fade only the diffuse part when they preserve specular).
void AddLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0,
              inout vec3 diffuse, inout vec3 specular)
{
    vec3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);
    diffuse += kD * albedo / PI * radiance * NdotL;
    specular += spec * radiance * NdotL;
}

float ShadowFactor(vec3 N, vec3 L)
{
    int cascade = u_CascadeCount - 1;
    for (int i = 0; i < u_CascadeCount; i++)
        if (v_ViewDepth < u_CascadeSplits[i]) { cascade = i; break; }
    if (v_ViewDepth > u_CascadeSplits[u_CascadeCount - 1])
        return 1.0;

    // normal-offset bias scaled by the cascade's texel size
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float texelWorld = u_CascadeTexelWorld[cascade];
    vec3 offsetPos = v_WorldPos + N * texelWorld * (1.5 + 2.0 * (1.0 - NdotL));
    vec4 ls = u_LightSpaces[cascade] * vec4(offsetPos, 1.0);
    vec3 proj = ls.xyz / ls.w * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    vec2 atlasOffset = vec2(float(cascade % 2), float(cascade / 2)) * 0.5;
    vec2 lo = atlasOffset + vec2(u_ShadowTexel * 1.5), hi = atlasOffset + vec2(0.5 - u_ShadowTexel * 1.5);
    vec2 uv = atlasOffset + proj.xy * 0.5;
    float bias = 0.0005;
    float sum = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            sum += texture(u_ShadowMap, vec3(clamp(uv + vec2(x, y) * u_ShadowTexel, lo, hi), proj.z - bias));
    float shadow = sum / 9.0;

    // fade out at the end of the last cascade
    float fadeStart = u_CascadeSplits[u_CascadeCount - 1] * 0.85;
    float fade = clamp((v_ViewDepth - fadeStart) / (u_CascadeSplits[u_CascadeCount - 1] - fadeStart), 0.0, 1.0);
    return mix(shadow, 1.0, fade);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Same gradient as the sky shader, used as a cheap environment map.
vec3 SkyRadiance(vec3 dir, float roughness)
{
    float y = dir.y;
    vec3 top = toLinear(u_SkyColor), horizon = toLinear(u_HorizonColor), ground = toLinear(u_GroundColor);
    vec3 c = y > 0.0 ? mix(horizon, top, pow(clamp(y, 0.0, 1.0), 0.6))
                     : mix(horizon, ground, pow(clamp(-y * 3.0, 0.0, 1.0), 0.5));
    vec3 avg = (top + horizon * 2.0 + ground) * 0.25;
    return mix(c, avg, roughness * roughness);
}

// Normal mapping without vertex tangents (cotangent frame from screen-space derivatives).
mat3 CotangentFrame(vec3 N, vec3 p, vec2 uv)
{
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
    return mat3(T * invmax, B * invmax, N);
}

// Fog, then the outputs (linear HDR - tonemapping & gamma happen in post-processing). `diffuse`
// fades with alpha on transparent surfaces; `specular` (highlights, reflections, emission) too
// unless the material preserves specular. Transparent output is premultiplied by alpha.
void Finish(vec3 diffuse, vec3 specular, float alpha)
{
    vec3 color;
    if (u_BlendMode == 2)
        color = u_PreserveSpecular == 1 ? diffuse * alpha + specular : (diffuse + specular) * alpha;
    else
    {
        color = diffuse + specular;
        alpha = 1.0; // opaque and cutout surfaces cover what is behind them
    }
    if (u_FogEnabled == 1)
    {
        float dist = length(u_CameraPos - v_WorldPos);
        float heightFactor = exp(-u_FogHeightFalloff * max(v_WorldPos.y, 0.0));
        float fog = clamp(1.0 - exp(-dist * u_FogDensity * heightFactor), 0.0, 1.0);
        // premultiplied: the fog covers the surface as much as the surface covers the background
        color = mix(color, toLinear(u_FogColor) * u_Exposure * alpha, fog);
    }
    o_Color = vec4(color, alpha);
    o_EntityID = u_EntityID;
}

void main()
{
    vec2 uv = v_TexCoord * u_Tiling + u_Offset;
    vec4 texColor = texture(u_AlbedoMap, uv);
    vec3 texLinear = u_AlbedoSRGB == 1 ? toLinear(texColor.rgb) : texColor.rgb;
    float alpha = clamp(u_Albedo.a * texColor.a, 0.0, 1.0);
    if (u_BlendMode == 1 && alpha < u_AlphaCutoff)
        discard;
    // nothing left to draw (a fully faded surface that does not keep its highlights)
    if (u_BlendMode == 2 && u_PreserveSpecular == 0 && alpha < 0.002)
        discard;
    vec3 albedo = toLinear(u_Albedo.rgb) * texLinear;
    vec3 emission = u_Emission;
    if (u_HasEmissiveMap == 1)
    {
        vec3 emissiveTex = texture(u_EmissiveMap, uv).rgb;
        emission *= u_EmissiveSRGB == 1 ? toLinear(emissiveTex) : emissiveTex;
    }
    if (u_Unlit == 1)
    {
        Finish(albedo * u_Exposure, emission * u_Exposure, alpha);
        return;
    }
    float metallic = clamp(u_Metallic, 0.0, 1.0);
    float roughness = clamp(u_Roughness, 0.04, 1.0);
    if (u_HasMetalRoughMap == 1)
    {
        vec4 mr = texture(u_MetalRoughMap, uv);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
    }

    // Back faces (only drawn on double-sided materials) are lit from their own side.
    bool frontFacing = gl_FrontFacing != (u_Mirrored == 1);
    vec3 N = normalize(v_Normal);
    if (!frontFacing) N = -N;
    if (u_HasNormalMap == 1)
    {
        vec3 tn = texture(u_NormalMap, uv).xyz * 2.0 - 1.0;
        tn.xy *= u_NormalStrength;
        vec3 V0 = u_CameraPos - v_WorldPos;
        N = normalize(CotangentFrame(N, -V0, uv) * tn);
    }
    vec3 V = normalize(u_CameraPos - v_WorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 diffuse = vec3(0.0), specular = vec3(0.0);
    if (u_HasSun == 1)
    {
        float shadow = u_ShadowsEnabled == 1 ? ShadowFactor(N, normalize(-u_SunDirection)) : 1.0;
        AddLight(N, V, normalize(-u_SunDirection), u_SunRadiance * shadow, albedo, metallic, roughness, F0, diffuse, specular);
    }
    for (int i = 0; i < u_LocalLightCount; i++)
    {
        vec3 toLight = u_LightPosRange[i].xyz - v_WorldPos;
        float dist = length(toLight);
        float falloff = clamp(1.0 - pow(dist / max(u_LightPosRange[i].w, 0.001), 4.0), 0.0, 1.0);
        float attenuation = falloff * falloff / (dist * dist + 1.0);
        vec3 L = toLight / max(dist, 1e-4);
        // spot cone: smooth from the outer angle (0) to the inner one (1)
        attenuation *= smoothstep(u_LightDirCos[i].w, u_LightRadiance[i].w, dot(-L, u_LightDirCos[i].xyz));
        if (attenuation <= 0.0)
            continue;
        AddLight(N, V, L, u_LightRadiance[i].rgb * attenuation, albedo, metallic, roughness, F0, diffuse, specular);
    }

    // Image-based-ish ambient: hemisphere irradiance + sky reflection.
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 R = reflect(-V, N);
    vec3 irradiance, specularEnv;
    if (u_UseIBL == 1)
    {
        irradiance = EvalSH(N) * u_EnvIntensity;
        vec3 prefiltered = textureLod(u_EnvMap, EquirectUV(R), roughness * u_EnvMaxLod * 0.85).rgb * u_EnvIntensity;
        // analytic approximation of the split-sum BRDF term (Karis)
        vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022), c1 = vec4(1.0, 0.0425, 1.04, -0.04);
        vec4 r = roughness * c0 + c1;
        float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
        vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
        specularEnv = prefiltered * (F0 * AB.x + AB.y);
    }
    else
    {
        irradiance = mix(toLinear(u_GroundColor), mix(toLinear(u_HorizonColor), toLinear(u_SkyColor), 0.5), N.y * 0.5 + 0.5);
        specularEnv = SkyRadiance(R, roughness) * F * mix(1.0, 0.35, roughness);
    }
    diffuse += kD * albedo * irradiance * u_Ambient;
    specular += specularEnv * u_Ambient + emission;
    Finish(diffuse * u_Exposure, specular * u_Exposure, alpha);
}
)";

        const char* s_ShadowVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
#ifdef ALPHA_TEST
layout(location = 2) in vec2 a_TexCoord;
uniform vec2 u_Tiling;
uniform vec2 u_Offset;
out vec2 v_TexCoord;
#endif
#ifdef SKINNED
layout(location = 3) in ivec4 a_Joints;
layout(location = 4) in vec4 a_Weights;
uniform mat4 u_Joints[MAX_JOINTS];
#endif
uniform mat4 u_LightSpace;
uniform mat4 u_Model;
void main()
{
    vec4 p = vec4(a_Position, 1.0);
#ifdef SKINNED
    mat4 skin = a_Weights.x * u_Joints[a_Joints.x] + a_Weights.y * u_Joints[a_Joints.y]
              + a_Weights.z * u_Joints[a_Joints.z] + a_Weights.w * u_Joints[a_Joints.w];
    p = skin * p;
#endif
#ifdef ALPHA_TEST
    v_TexCoord = a_TexCoord * u_Tiling + u_Offset;
#endif
    gl_Position = u_LightSpace * u_Model * p;
}
)";

        // Shader source with the variant's defines after the #version line.
        std::string Variant(const char* src, bool skinned, bool alphaTest = false)
        {
            std::string s(src);
            size_t eol = s.find('\n');
            std::string defs = "\n#define MAX_JOINTS " + std::to_string(MaxJoints) + "\n" + (skinned ? "#define SKINNED\n" : "") +
                               (alphaTest ? "#define ALPHA_TEST\n" : "");
            return s.substr(0, eol) + defs + s.substr(eol + 1);
        }

        // Depth only; cutout casters drop the texels a cutout surface does not draw (the shadow has
        // the same holes as the surface).
        const char* s_ShadowFS = R"(#version 410 core
#ifdef ALPHA_TEST
in vec2 v_TexCoord;
uniform sampler2D u_AlbedoMap;
uniform float u_AlbedoAlpha;
uniform float u_AlphaCutoff;
#endif
void main()
{
#ifdef ALPHA_TEST
    if (u_AlbedoAlpha * texture(u_AlbedoMap, v_TexCoord).a < u_AlphaCutoff)
        discard;
#endif
}
)";

        const char* s_SkyVS = R"(#version 410 core
uniform mat4 u_InverseViewProjection;
out vec3 v_Direction;
void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vec4 world = u_InverseViewProjection * vec4(pos, 1.0, 1.0);
    v_Direction = world.xyz / world.w;
    gl_Position = vec4(pos, 1.0, 1.0);
}
)";

        const char* s_SkyFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;
in vec3 v_Direction;
uniform vec3 u_CameraPos;
uniform vec3 u_Top;
uniform vec3 u_Horizon;
uniform vec3 u_Ground;
uniform int u_HasSun;
uniform vec3 u_SunDirection;
uniform vec3 u_SunColor;
uniform int u_FogEnabled;
uniform vec3 u_FogColor;
uniform float u_FogDensity;
uniform int u_UseIBL;
uniform sampler2D u_EnvMap;
uniform float u_EnvIntensity;
uniform float u_EnvRotation;
void main()
{
    vec3 dir = normalize(v_Direction - u_CameraPos);
    if (u_UseIBL == 1)
    {
        float c = cos(u_EnvRotation), s = sin(u_EnvRotation);
        vec3 d = vec3(c * dir.x - s * dir.z, dir.y, s * dir.x + c * dir.z);
        vec2 uv = vec2(atan(d.z, d.x) / 6.2831853 + 0.5, asin(clamp(d.y, -1.0, 1.0)) / 3.14159265 + 0.5);
        vec3 env = textureLod(u_EnvMap, uv, 0.0).rgb * u_EnvIntensity;
        if (u_FogEnabled == 1)
            env = mix(env, pow(u_FogColor, vec3(2.2)), (1.0 - smoothstep(-0.05, 0.35, dir.y)) * clamp(u_FogDensity * 40.0, 0.0, 1.0));
        o_Color = vec4(env, 1.0);
        o_EntityID = -1;
        return;
    }
    float y = dir.y;
    vec3 color = y > 0.0 ? mix(u_Horizon, u_Top, pow(clamp(y, 0.0, 1.0), 0.6))
                         : mix(u_Horizon, u_Ground, pow(clamp(-y * 3.0, 0.0, 1.0), 0.5));
    if (u_HasSun == 1)
    {
        float d = max(dot(dir, normalize(-u_SunDirection)), 0.0);
        color += u_SunColor * (pow(d, 900.0) * 4.0 + pow(d, 12.0) * 0.18);
    }
    if (u_FogEnabled == 1)
    {
        float horizon = 1.0 - smoothstep(-0.05, 0.35, dir.y);
        color = mix(color, u_FogColor, horizon * clamp(u_FogDensity * 40.0, 0.0, 1.0));
    }
    o_Color = vec4(pow(color, vec3(2.2)), 1.0);
    o_EntityID = -1;
}
)";

        const char* s_GridVS = R"(#version 410 core
uniform mat4 u_InverseViewProjection;
out vec3 v_Near;
out vec3 v_Far;
vec3 Unproject(vec2 p, float z)
{
    vec4 w = u_InverseViewProjection * vec4(p, z, 1.0);
    return w.xyz / w.w;
}
void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    v_Near = Unproject(pos, -1.0);
    v_Far = Unproject(pos, 1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

        const char* s_GridFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;
in vec3 v_Near;
in vec3 v_Far;
uniform mat4 u_ViewProjection;
uniform vec3 u_CameraPos;
uniform int u_Plane;          // 0 = XZ, 1 = XY
uniform float u_CellSize;     // world units between minor lines
uniform float u_MajorEvery;   // every Nth line is a major line (<= 1: none)
uniform vec4 u_MinorColor;    // rgb + opacity
uniform vec4 u_MajorColor;
uniform int u_ShowAxes;
uniform float u_FadeScale;

// Coverage of the lines every `spacing` world units (anti-aliased, about one pixel wide).
// Lines closer than a few pixels to each other fade out instead of turning into noise.
float LineAlpha(vec2 coord, float spacing)
{
    vec2 c = coord / spacing;
    vec2 derivative = fwidth(c);
    vec2 grid = abs(fract(c - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    float alpha = 1.0 - min(line, 1.0);
    float cellsPerPixel = max(derivative.x, derivative.y);
    return alpha * (1.0 - smoothstep(0.12, 0.3, cellsPerPixel));
}

void main()
{
    vec3 dir = v_Far - v_Near;
    float t = u_Plane == 0 ? -v_Near.y / dir.y : -v_Near.z / dir.z;
    if (t <= 0.0)
        discard;
    vec3 p = v_Near + t * dir;
    vec2 coord = u_Plane == 0 ? p.xz : p.xy;

    vec4 clip = u_ViewProjection * vec4(p, 1.0);
    float depth = clip.z / clip.w;
    gl_FragDepth = depth * 0.5 + 0.5;

    float dist = length(p - u_CameraPos);
    float camHeight = abs(u_Plane == 0 ? u_CameraPos.y : u_CameraPos.z);
    float fade = 1.0 - smoothstep((camHeight * 4.0 + 10.0) * u_FadeScale, (camHeight * 12.0 + 40.0) * u_FadeScale, dist);

    float minor = LineAlpha(coord, u_CellSize) * u_MinorColor.a;
    float major = u_MajorEvery > 1.5 ? LineAlpha(coord, u_CellSize * u_MajorEvery) * u_MajorColor.a : 0.0;
    vec4 color = major >= minor ? vec4(u_MajorColor.rgb, major) : vec4(u_MinorColor.rgb, minor);

    if (u_ShowAxes == 1)
    {
        vec2 pixel = min(fwidth(coord), vec2(u_CellSize)); // world units per pixel
        if (abs(coord.x) < pixel.x) // the line x = 0: Z axis (3D) / Y axis (2D)
            color = vec4(u_Plane == 0 ? vec3(0.25, 0.45, 1.0) : vec3(0.35, 0.85, 0.35), 0.9);
        if (abs(coord.y) < pixel.y) // X axis
            color = vec4(1.0, 0.3, 0.3, 0.9);
    }

    color.a *= fade;
    if (color.a < 0.02)
        discard;
    o_Color = vec4(pow(color.rgb, vec3(2.2)), color.a);
    o_EntityID = -1;
}
)";

        struct DrawItem
        {
            Ref<Mesh> MeshRef;
            glm::mat4 Transform;
            Material Mat;
            int EntityID;
            bool CastShadows;
            std::vector<glm::mat4> Joints; // skinned meshes only
        };

        struct Renderer3DData
        {
            Ref<Shader> MeshShader, SkinnedMeshShader;
            Ref<Shader> ShadowShader, SkinnedShadowShader;
            Ref<Shader> CutoutShadowShader, SkinnedCutoutShadowShader; // alpha tested casters
            Ref<Shader> SkyShader;
            Ref<Shader> GridShader;
            Ref<Texture2D> WhiteTexture;
            Ref<Framebuffer> ShadowMap;
            uint32_t ShadowMapSize = 4096; // 2x2 atlas of 2048 cascades

            glm::mat4 View{ 1.0f }, Projection{ 1.0f };
            glm::vec3 CameraPosition{ 0.0f };
            SceneEnvironment Environment;
            std::vector<DrawItem> Queue;

            bool GridSubmitted = false;
            int GridPlane = 0;
            GridSettings Grid;

            std::vector<Renderer3D::TransparentDrawRecord> LastTransparent;
            glm::mat4 LastTransparentViewProjection{ 1.0f };
            uint64_t EndSceneCount = 0;
        };

        Renderer3DData* s_Data = nullptr;
    }

    void Renderer3D::Init()
    {
        s_Data = new Renderer3DData();
        s_Data->MeshShader = Shader::Create("Renderer3D_PBR", Variant(s_MeshVS, false), s_MeshFS);
        s_Data->SkinnedMeshShader = Shader::Create("Renderer3D_PBR_Skinned", Variant(s_MeshVS, true), s_MeshFS);
        s_Data->ShadowShader = Shader::Create("Renderer3D_Shadow", Variant(s_ShadowVS, false), s_ShadowFS);
        s_Data->SkinnedShadowShader = Shader::Create("Renderer3D_Shadow_Skinned", Variant(s_ShadowVS, true), s_ShadowFS);
        s_Data->CutoutShadowShader = Shader::Create("Renderer3D_Shadow_Cutout", Variant(s_ShadowVS, false, true),
                                                    Variant(s_ShadowFS, false, true));
        s_Data->SkinnedCutoutShadowShader = Shader::Create("Renderer3D_Shadow_Cutout_Skinned", Variant(s_ShadowVS, true, true),
                                                           Variant(s_ShadowFS, true, true));
        s_Data->SkyShader = Shader::Create("Renderer3D_Sky", s_SkyVS, s_SkyFS);
        s_Data->GridShader = Shader::Create("Editor_Grid", s_GridVS, s_GridFS);

        TextureSpecification spec;
        spec.GenerateMips = false;
        s_Data->WhiteTexture = Texture2D::Create(spec);
        uint32_t white = 0xffffffff;
        s_Data->WhiteTexture->SetData(&white, 4);

        FramebufferSpecification fbSpec;
        fbSpec.Width = fbSpec.Height = s_Data->ShadowMapSize;
        fbSpec.Attachments = { FramebufferTextureFormat::DEPTH32F };
        s_Data->ShadowMap = Framebuffer::Create(fbSpec);
    }

    void Renderer3D::Shutdown()
    {
        delete s_Data;
        s_Data = nullptr;
    }

    uint32_t Renderer3D::GetShadowMapRendererID() { return s_Data->ShadowMap->GetDepthAttachmentRendererID(); }

    void Renderer3D::BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                                const SceneEnvironment& environment)
    {
        s_Data->View = view;
        s_Data->Projection = projection;
        s_Data->CameraPosition = cameraPosition;
        s_Data->Environment = environment;
        s_Data->Queue.clear();
        s_Data->GridSubmitted = false;
    }

    void Renderer3D::SubmitGrid(int plane, const GridSettings& settings)
    {
        s_Data->GridSubmitted = true;
        s_Data->GridPlane = plane;
        s_Data->Grid = settings;
    }

    SurfaceState Renderer3D::GetSurfaceState(const Material& material, const glm::mat4& transform, bool castShadows)
    {
        SurfaceState state;
        state.Mirrored = glm::determinant(glm::mat3(transform)) < 0.0f;
        state.Cull = material.DoubleSided ? CullMode::None : (state.Mirrored ? CullMode::Front : CullMode::Back);
        switch (material.BlendMode)
        {
            case MaterialBlendMode::Opaque:
                state.Queue = SurfaceState::Pass::Opaque;
                break;
            case MaterialBlendMode::Cutout:
                state.Queue = SurfaceState::Pass::Cutout;
                state.AlphaTestedShadow = true;
                break;
            case MaterialBlendMode::Transparent:
                state.Queue = SurfaceState::Pass::Transparent;
                state.Blend = BlendMode::Premultiplied;
                state.DepthWrite = false;
                state.TwoSidedBlend = material.DoubleSided;
                castShadows = false;
                break;
        }
        state.CastsShadow = castShadows;
        if (!castShadows)
            state.AlphaTestedShadow = false;
        return state;
    }

    void Renderer3D::SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Material& material, int entityID, bool castShadows,
                                const std::vector<glm::mat4>* joints)
    {
        if (!mesh) return;
        DrawItem item{ mesh, transform, material, entityID, castShadows, {} };
        if (mesh->IsSkinned())
        {
            if (joints && !joints->empty()) item.Joints = *joints;
            item.Joints.resize(MaxJoints, glm::mat4(1.0f));
        }
        s_Data->Queue.push_back(std::move(item));
    }

    struct CascadeData
    {
        int Count = 1;
        glm::mat4 LightSpace[3]{ glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) };
        float Splits[3]{ 0.0f, 0.0f, 0.0f };
        float TexelWorld[3]{ 0.05f, 0.05f, 0.05f };
    };

    static glm::mat4 FitLightToSphere(const glm::vec3& dir, const glm::vec3& center, float radius, float cascadeSize, float& texelWorld)
    {
        glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        float back = radius + 60.0f; // include casters between the light and the slice
        glm::mat4 lightView = glm::lookAt(center - dir * back, center, up);
        texelWorld = (radius * 2.0f) / cascadeSize;
        // snap to the texel grid to avoid shimmering while the camera moves
        glm::vec4 origin = lightView * glm::vec4(0, 0, 0, 1);
        glm::vec2 snapped = glm::floor(glm::vec2(origin) / texelWorld) * texelWorld;
        glm::mat4 snap = glm::translate(glm::mat4(1.0f), glm::vec3(snapped - glm::vec2(origin), 0.0f));
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.1f, back + radius * 2.0f);
        return lightProj * snap * lightView;
    }

    static CascadeData ComputeCascades(const SceneEnvironment& env, const glm::vec3& cameraPos, const glm::mat4& view, const glm::mat4& proj)
    {
        CascadeData c;
        glm::vec3 dir = glm::normalize(env.Sun.Direction);
        const float cascadeSize = (float)(s_Data->ShadowMapSize / 2); // 2x2 atlas
        bool perspective = proj[3][3] == 0.0f;
        if (!perspective)
        {
            // orthographic cameras: a single cascade around the view center
            glm::vec3 forward = -glm::vec3(glm::inverse(view)[2]);
            float half = env.ShadowDistance * 0.5f;
            c.Count = 1;
            c.LightSpace[0] = FitLightToSphere(dir, cameraPos + forward * half, half, cascadeSize, c.TexelWorld[0]);
            c.Splits[0] = env.ShadowDistance * 2.0f;
            return c;
        }

        float camNear = proj[3][2] / (proj[2][2] - 1.0f);
        float camFar = proj[3][2] / (proj[2][2] + 1.0f);
        float n = std::max(0.05f, camNear), f = std::min(camFar, env.ShadowDistance);
        glm::mat4 invViewProj = glm::inverse(proj * view);
        glm::vec3 nearCorners[4], farCorners[4];
        const glm::vec2 ndc[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int i = 0; i < 4; i++)
        {
            glm::vec4 a = invViewProj * glm::vec4(ndc[i], -1.0f, 1.0f);
            glm::vec4 b = invViewProj * glm::vec4(ndc[i], 1.0f, 1.0f);
            nearCorners[i] = glm::vec3(a) / a.w;
            farCorners[i] = glm::vec3(b) / b.w;
        }

        c.Count = 3;
        float prev = n;
        for (int i = 0; i < c.Count; i++)
        {
            float p = (i + 1) / (float)c.Count;
            float uniformSplit = n + (f - n) * p;
            float logSplit = n * std::pow(f / n, p);
            float split = glm::mix(uniformSplit, logSplit, 0.75f);

            glm::vec3 corners[8];
            for (int k = 0; k < 4; k++)
            {
                float t0 = (prev - camNear) / (camFar - camNear), t1 = (split - camNear) / (camFar - camNear);
                corners[k] = glm::mix(nearCorners[k], farCorners[k], t0);
                corners[k + 4] = glm::mix(nearCorners[k], farCorners[k], t1);
            }
            glm::vec3 center(0.0f);
            for (auto& v : corners) center += v;
            center /= 8.0f;
            float radius = 0.0f;
            for (auto& v : corners) radius = std::max(radius, glm::length(v - center));
            radius = std::ceil(radius * 16.0f) / 16.0f;

            c.LightSpace[i] = FitLightToSphere(dir, center, radius, cascadeSize, c.TexelWorld[i]);
            c.Splits[i] = split;
            prev = split;
        }
        return c;
    }

    // The point + spot lights as the shader's uniform arrays (point lights first, at most
    // MaxLocalLights together).
    struct LocalLightArrays
    {
        uint32_t Count = 0;
        glm::vec4 PosRange[Renderer3D::MaxLocalLights];
        glm::vec4 Radiance[Renderer3D::MaxLocalLights];
        glm::vec4 DirCos[Renderer3D::MaxLocalLights];
    };

    static LocalLightArrays PackLocalLights(const SceneEnvironment& env)
    {
        LocalLightArrays out;
        for (const PointLight& pl : env.PointLights)
        {
            if (out.Count == Renderer3D::MaxLocalLights) break;
            out.PosRange[out.Count] = glm::vec4(pl.Position, pl.Radius);
            out.Radiance[out.Count] = glm::vec4(pl.Color * pl.Intensity, -1.0f);
            out.DirCos[out.Count] = glm::vec4(0.0f, 0.0f, 0.0f, -2.0f); // no cone
            out.Count++;
        }
        for (const SpotLight& sl : env.SpotLights)
        {
            if (out.Count == Renderer3D::MaxLocalLights) break;
            glm::vec2 cosines = Renderer3D::GetSpotCosines(sl.InnerAngle, sl.OuterAngle);
            float len = glm::length(sl.Direction);
            glm::vec3 dir = len > 1e-6f ? sl.Direction / len : glm::vec3(0.0f, -1.0f, 0.0f);
            out.PosRange[out.Count] = glm::vec4(sl.Position, sl.Range);
            out.Radiance[out.Count] = glm::vec4(sl.Color * sl.Intensity, cosines.x);
            out.DirCos[out.Count] = glm::vec4(dir, cosines.y);
            out.Count++;
        }
        return out;
    }

    // One queued mesh with its material (the main pass shader's per-frame uniforms are set).
    // `countMesh`: false for the second draw of a two-sided transparent surface.
    static void DrawQueued(const DrawItem& item, bool mirrored, bool countMesh = true)
    {
        auto& stats = Renderer::GetStats();
        const Material& m = item.Mat;
        bool skinned = item.MeshRef->IsSkinned();
        auto& shader = skinned ? s_Data->SkinnedMeshShader : s_Data->MeshShader;
        shader->Bind();
        if (skinned) shader->SetMat4Array("u_Joints", item.Joints.data(), (uint32_t)item.Joints.size());
        (m.AlbedoMap ? m.AlbedoMap : s_Data->WhiteTexture)->Bind(0);
        (m.NormalMap ? m.NormalMap : s_Data->WhiteTexture)->Bind(2);
        (m.MetalRoughMap ? m.MetalRoughMap : s_Data->WhiteTexture)->Bind(3);
        (m.EmissiveMap ? m.EmissiveMap : s_Data->WhiteTexture)->Bind(4);
        shader->SetInt("u_HasNormalMap", m.NormalMap ? 1 : 0);
        shader->SetInt("u_HasMetalRoughMap", m.MetalRoughMap ? 1 : 0);
        shader->SetInt("u_HasEmissiveMap", m.EmissiveMap ? 1 : 0);
        shader->SetInt("u_AlbedoSRGB", !m.AlbedoMap || m.AlbedoMap->GetSpecification().SRGB ? 1 : 0);
        shader->SetInt("u_EmissiveSRGB", !m.EmissiveMap || m.EmissiveMap->GetSpecification().SRGB ? 1 : 0);
        shader->SetFloat("u_NormalStrength", m.NormalStrength);
        shader->SetMat4("u_Model", item.Transform);
        shader->SetMat3("u_NormalMatrix", glm::transpose(glm::inverse(glm::mat3(item.Transform))));
        shader->SetFloat4("u_Albedo", m.Albedo);
        shader->SetFloat2("u_Tiling", m.Tiling);
        shader->SetFloat2("u_Offset", m.Offset);
        shader->SetFloat("u_Metallic", m.Metallic);
        shader->SetFloat("u_Roughness", m.Roughness);
        shader->SetFloat3("u_Emission", m.Emission * m.EmissionStrength);
        shader->SetInt("u_Unlit", m.Shader == MaterialShader::Unlit ? 1 : 0);
        shader->SetInt("u_BlendMode", (int)m.BlendMode);
        shader->SetFloat("u_AlphaCutoff", m.AlphaCutoff);
        shader->SetInt("u_PreserveSpecular", m.PreserveSpecular ? 1 : 0);
        shader->SetInt("u_Mirrored", mirrored ? 1 : 0);
        shader->SetInt("u_EntityID", item.EntityID);
        RenderCommand::DrawIndexed(item.MeshRef->GetVertexArray(), item.MeshRef->GetIndexCount());
        stats.DrawCalls++;
        if (!countMesh)
            return;
        stats.MeshCount++;
        stats.TriangleCount += item.MeshRef->GetIndexCount() / 3;
    }

    void Renderer3D::EndScene()
    {
        auto& env = s_Data->Environment;
        auto& stats = Renderer::GetStats();
        const glm::mat4 viewProj = s_Data->Projection * s_Data->View;

        bool shadows = env.HasSun && env.Sun.CastShadows && !s_Data->Queue.empty();
        CascadeData cascades;

        // ---------------- shadow pass (cascades rendered into a 2x2 atlas)
        if (shadows)
        {
            cascades = ComputeCascades(env, s_Data->CameraPosition, s_Data->View, s_Data->Projection);
            RenderCommand::SaveRenderTarget();
            s_Data->ShadowMap->Bind();
            RenderCommand::SetDepthWrite(true);
            RenderCommand::Clear();
            uint32_t half = s_Data->ShadowMapSize / 2;
            for (int c = 0; c < cascades.Count; c++)
            {
                RenderCommand::SetViewport((c % 2) * half, (c / 2) * half, half, half);
                for (auto& sh : { s_Data->ShadowShader, s_Data->SkinnedShadowShader, s_Data->CutoutShadowShader,
                                  s_Data->SkinnedCutoutShadowShader })
                {
                    sh->Bind();
                    sh->SetMat4("u_LightSpace", cascades.LightSpace[c]);
                }
                for (const auto& item : s_Data->Queue)
                {
                    // Transparent surfaces let the light through (no shadow of their own); cutout
                    // ones only where they are drawn.
                    SurfaceState state = GetSurfaceState(item.Mat, item.Transform, item.CastShadows);
                    if (!state.CastsShadow) continue;
                    bool skinned = item.MeshRef->IsSkinned();
                    auto& sh = state.AlphaTestedShadow ? (skinned ? s_Data->SkinnedCutoutShadowShader : s_Data->CutoutShadowShader)
                                                       : (skinned ? s_Data->SkinnedShadowShader : s_Data->ShadowShader);
                    sh->Bind();
                    if (skinned) sh->SetMat4Array("u_Joints", item.Joints.data(), (uint32_t)item.Joints.size());
                    sh->SetMat4("u_Model", item.Transform);
                    if (state.AlphaTestedShadow)
                    {
                        (item.Mat.AlbedoMap ? item.Mat.AlbedoMap : s_Data->WhiteTexture)->Bind(0);
                        sh->SetInt("u_AlbedoMap", 0);
                        sh->SetFloat("u_AlbedoAlpha", item.Mat.Albedo.a);
                        sh->SetFloat("u_AlphaCutoff", item.Mat.AlphaCutoff);
                        sh->SetFloat2("u_Tiling", item.Mat.Tiling);
                        sh->SetFloat2("u_Offset", item.Mat.Offset);
                    }
                    RenderCommand::DrawIndexed(item.MeshRef->GetVertexArray(), item.MeshRef->GetIndexCount());
                    stats.DrawCalls++;
                }
            }
            RenderCommand::RestoreRenderTarget();
        }

        // ---------------- main pass
        if (!s_Data->Queue.empty())
        {
          const LocalLightArrays localLights = PackLocalLights(env);
          for (auto& shader : { s_Data->MeshShader, s_Data->SkinnedMeshShader })
          {
            shader->Bind();
            shader->SetMat4("u_ViewProjection", viewProj);
            shader->SetMat4("u_View", s_Data->View);
            shader->SetMat4Array("u_LightSpaces", cascades.LightSpace, 3);
            shader->SetFloat3("u_CascadeSplits", { cascades.Splits[0], cascades.Splits[1], cascades.Splits[2] });
            shader->SetFloat3("u_CascadeTexelWorld", { cascades.TexelWorld[0], cascades.TexelWorld[1], cascades.TexelWorld[2] });
            shader->SetInt("u_CascadeCount", cascades.Count);
            shader->SetInt("u_UseIBL", env.EnvMap ? 1 : 0);
            shader->SetInt("u_EnvMap", 5);
            if (env.EnvMap)
            {
                shader->SetFloat("u_EnvMaxLod", env.EnvMap->GetMaxLod());
                shader->SetFloat("u_EnvIntensity", env.EnvIntensity);
                shader->SetFloat("u_EnvRotation", env.EnvRotation);
                const auto& sh = env.EnvMap->GetSH();
                for (int i = 0; i < 9; i++)
                    shader->SetFloat3("u_SH[" + std::to_string(i) + "]", sh[i]);
            }
            shader->SetInt("u_FogEnabled", env.FogEnabled ? 1 : 0);
            shader->SetFloat3("u_FogColor", env.FogColor);
            shader->SetFloat("u_FogDensity", env.FogDensity);
            shader->SetFloat("u_FogHeightFalloff", env.FogHeightFalloff);
            shader->SetFloat3("u_CameraPos", s_Data->CameraPosition);
            shader->SetInt("u_HasSun", env.HasSun ? 1 : 0);
            shader->SetFloat3("u_SunDirection", glm::normalize(env.Sun.Direction));
            shader->SetFloat3("u_SunRadiance", env.Sun.Color * env.Sun.Intensity);
            shader->SetInt("u_ShadowsEnabled", shadows ? 1 : 0);
            shader->SetFloat("u_ShadowTexel", 1.0f / (float)s_Data->ShadowMapSize);
            shader->SetFloat3("u_SkyColor", env.SkyTopColor);
            shader->SetFloat3("u_HorizonColor", env.SkyHorizonColor);
            shader->SetFloat3("u_GroundColor", env.GroundColor);
            shader->SetFloat("u_Ambient", env.AmbientIntensity);
            shader->SetFloat("u_Exposure", env.Exposure);

            shader->SetInt("u_LocalLightCount", (int)localLights.Count);
            if (localLights.Count > 0)
            {
                shader->SetFloat4Array("u_LightPosRange", localLights.PosRange, localLights.Count);
                shader->SetFloat4Array("u_LightRadiance", localLights.Radiance, localLights.Count);
                shader->SetFloat4Array("u_LightDirCos", localLights.DirCos, localLights.Count);
            }

            shader->SetInt("u_AlbedoMap", 0);
            shader->SetInt("u_ShadowMap", 1);
            shader->SetInt("u_NormalMap", 2);
            shader->SetInt("u_MetalRoughMap", 3);
            shader->SetInt("u_EmissiveMap", 4);
          }
            RenderCommand::BindTextureUnit(1, s_Data->ShadowMap->GetDepthAttachmentRendererID());
            (env.EnvMap ? env.EnvMap->GetTexture() : s_Data->WhiteTexture)->Bind(5);

            // Opaque, then cutout surfaces: they cover what is behind them (no blending needed).
            RenderCommand::SetBlendMode(BlendMode::None);
            for (SurfaceState::Pass pass : { SurfaceState::Pass::Opaque, SurfaceState::Pass::Cutout })
                for (const auto& item : s_Data->Queue)
                {
                    SurfaceState state = GetSurfaceState(item.Mat, item.Transform, item.CastShadows);
                    if (state.Queue != pass) continue;
                    RenderCommand::SetCullMode(state.Cull);
                    DrawQueued(item, state.Mirrored);
                }
            RenderCommand::SetCullMode(CullMode::None);
        }

        // ---------------- sky (drawn behind everything, depth = far plane)
        if (env.DrawSky)
        {
            RenderCommand::SetDepthWrite(false);
            auto& sky = s_Data->SkyShader;
            sky->Bind();
            sky->SetMat4("u_InverseViewProjection", glm::inverse(viewProj));
            sky->SetFloat3("u_CameraPos", s_Data->CameraPosition);
            sky->SetFloat3("u_Top", env.SkyTopColor);
            sky->SetFloat3("u_Horizon", env.SkyHorizonColor);
            sky->SetFloat3("u_Ground", env.GroundColor);
            sky->SetInt("u_HasSun", env.HasSun ? 1 : 0);
            sky->SetFloat3("u_SunDirection", glm::normalize(env.Sun.Direction));
            sky->SetFloat3("u_SunColor", env.Sun.Color);
            sky->SetInt("u_UseIBL", env.EnvMap ? 1 : 0);
            sky->SetInt("u_EnvMap", 5);
            if (env.EnvMap)
            {
                env.EnvMap->GetTexture()->Bind(5);
                sky->SetFloat("u_EnvIntensity", env.EnvIntensity);
                sky->SetFloat("u_EnvRotation", env.EnvRotation);
            }
            sky->SetInt("u_FogEnabled", env.FogEnabled ? 1 : 0);
            sky->SetFloat3("u_FogColor", env.FogColor);
            sky->SetFloat("u_FogDensity", env.FogDensity);
            RenderCommand::DrawArrays(3);
            RenderCommand::SetDepthWrite(true);
            stats.DrawCalls++;
        }

        // ---------------- editor grid (blended over the opaque scene, under the transparent one)
        RenderCommand::SetBlendMode(BlendMode::Alpha);
        if (s_Data->GridSubmitted)
            DrawGrid(s_Data->View, s_Data->Projection, s_Data->CameraPosition, s_Data->GridPlane, s_Data->Grid);

        // ---------------- transparent surfaces: over the opaque scene and the sky, by priority and
        // back to front (by the center of their bounds), depth tested but not written (what is
        // behind them stays visible). Output is premultiplied by alpha.
        std::vector<TransparentSortItem> sortItems;
        std::vector<size_t> transparent;
        s_Data->LastTransparent.clear();
        s_Data->LastTransparentViewProjection = viewProj;
        for (size_t i = 0; i < s_Data->Queue.size(); i++)
        {
            const DrawItem& item = s_Data->Queue[i];
            if (item.Mat.BlendMode != MaterialBlendMode::Transparent) continue;
            const AABB& bounds = item.MeshRef->GetBounds();
            transparent.push_back(i);
            sortItems.push_back({ glm::vec3(item.Transform * glm::vec4((bounds.Min + bounds.Max) * 0.5f, 1.0f)), item.Mat.SortPriority });
        }
        if (!transparent.empty())
        {
            RenderCommand::SetBlendMode(BlendMode::Premultiplied);
            RenderCommand::SetDepthWrite(false);
            bool perspective = s_Data->Projection[3][3] == 0.0f;
            glm::vec3 eye = glm::vec3(glm::inverse(s_Data->View)[3]);
            for (size_t order : SortTransparent(s_Data->View, perspective, sortItems))
            {
                const DrawItem& item = s_Data->Queue[transparent[order]];
                stats.TransparentCount++;
                const TransparentSortItem& key = sortItems[order];
                float distance = perspective ? glm::length(key.Center - eye) : -(s_Data->View * glm::vec4(key.Center, 1.0f)).z;
                s_Data->LastTransparent.push_back({ item.EntityID, key.Center, key.Priority, distance });
                SurfaceState state = GetSurfaceState(item.Mat, item.Transform, item.CastShadows);
                if (state.TwoSidedBlend)
                {
                    // the far (inner) side first, then the near one over it
                    RenderCommand::SetCullMode(state.Mirrored ? CullMode::Back : CullMode::Front);
                    DrawQueued(item, state.Mirrored);
                    RenderCommand::SetCullMode(state.Mirrored ? CullMode::Front : CullMode::Back);
                    DrawQueued(item, state.Mirrored, false);
                }
                else
                {
                    RenderCommand::SetCullMode(state.Cull);
                    DrawQueued(item, state.Mirrored);
                }
            }
            RenderCommand::SetCullMode(CullMode::None);
            RenderCommand::SetDepthWrite(true);
        }

        // the defaults everything else (2D, overlays, ImGui) expects
        RenderCommand::SetBlendMode(BlendMode::Alpha);
        RenderCommand::SetCullMode(CullMode::None);
        RenderCommand::SetDepthWrite(true);
        s_Data->Queue.clear();
        s_Data->GridSubmitted = false;
        s_Data->EndSceneCount++;
    }

    uint64_t Renderer3D::GetEndSceneCount() { return s_Data ? s_Data->EndSceneCount : 0; }

    float Renderer3D::GetLightImportance(const LightCandidate& light, const glm::vec3& viewPosition)
    {
        float strength = std::max(light.Strength, 0.0f);
        float range = std::max(light.Range, 0.0f);
        glm::vec3 d = light.Position - viewPosition;
        float dist2 = glm::dot(d, d);
        // Inside the lit sphere: everything around the viewer is lit, full strength.
        if (dist2 <= range * range)
            return strength;
        return strength * (range * range) / dist2;
    }

    std::vector<size_t> Renderer3D::SelectLights(const std::vector<LightCandidate>& lights, const glm::vec3& viewPosition, uint32_t maxCount)
    {
        std::vector<size_t> order(lights.size());
        for (size_t i = 0; i < order.size(); i++)
            order[i] = i;
        if (lights.size() <= maxCount)
            return order;
        std::vector<float> importance(lights.size()), dist2(lights.size());
        for (size_t i = 0; i < lights.size(); i++)
        {
            importance[i] = GetLightImportance(lights[i], viewPosition);
            glm::vec3 d = lights[i].Position - viewPosition;
            dist2[i] = glm::dot(d, d);
        }
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (importance[a] != importance[b])
                return importance[a] > importance[b];
            return dist2[a] < dist2[b];
        });
        order.resize(maxCount);
        return order;
    }

    glm::vec2 Renderer3D::GetSpotCosines(float innerAngleDegrees, float outerAngleDegrees)
    {
        float outer = std::clamp(outerAngleDegrees, 0.1f, 89.0f);
        float inner = std::clamp(innerAngleDegrees, 0.0f, outer);
        float cosOuter = std::cos(glm::radians(outer));
        float cosInner = std::max(std::cos(glm::radians(inner)), cosOuter + 1e-4f);
        return { cosInner, cosOuter };
    }

    float Renderer3D::GetSpotConeFactor(float innerAngleDegrees, float outerAngleDegrees, float angleDegrees)
    {
        glm::vec2 c = GetSpotCosines(innerAngleDegrees, outerAngleDegrees);
        float cd = std::cos(glm::radians(std::clamp(angleDegrees, 0.0f, 180.0f)));
        float t = std::clamp((cd - c.y) / (c.x - c.y), 0.0f, 1.0f); // GLSL smoothstep
        return t * t * (3.0f - 2.0f * t);
    }

    const std::vector<Renderer3D::TransparentDrawRecord>& Renderer3D::GetLastTransparentOrder()
    {
        static const std::vector<TransparentDrawRecord> s_None;
        return s_Data ? s_Data->LastTransparent : s_None;
    }

    const glm::mat4& Renderer3D::GetLastTransparentViewProjection()
    {
        static const glm::mat4 s_Identity(1.0f);
        return s_Data ? s_Data->LastTransparentViewProjection : s_Identity;
    }

    std::vector<size_t> Renderer3D::SortBackToFront(const glm::mat4& view, const std::vector<glm::vec3>& positions)
    {
        std::vector<TransparentSortItem> items(positions.size());
        for (size_t i = 0; i < positions.size(); i++)
            items[i].Center = positions[i];
        return SortTransparent(view, false, items);
    }

    std::vector<size_t> Renderer3D::SortTransparent(const glm::mat4& view, bool perspective, const std::vector<TransparentSortItem>& items)
    {
        // How far behind the camera's near side each surface is (larger = farther = drawn earlier).
        std::vector<float> depth(items.size());
        glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
        for (size_t i = 0; i < items.size(); i++)
        {
            float along = -(view * glm::vec4(items[i].Center, 1.0f)).z; // distance in front of the camera
            if (!perspective)
                depth[i] = along;
            else
                // Behind the camera counts as nearest (negative), like along the view direction.
                depth[i] = along < 0.0f ? along : glm::length(items[i].Center - eye);
        }
        std::vector<size_t> order(items.size());
        for (size_t i = 0; i < order.size(); i++)
            order[i] = i;
        // Lower priority first; then the farthest first; equal keys keep the submission order (stable).
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (items[a].Priority != items[b].Priority)
                return items[a].Priority < items[b].Priority;
            return depth[a] > depth[b];
        });
        return order;
    }

    void Renderer3D::DrawGrid(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition, int plane,
                              const GridSettings& settings)
    {
        GridSettings gs = settings;
        gs.Sanitize();
        glm::mat4 viewProj = projection * view;
        RenderCommand::SetBlendMode(BlendMode::Alpha);
        RenderCommand::SetDepthWrite(false);
        auto& grid = s_Data->GridShader;
        grid->Bind();
        grid->SetMat4("u_ViewProjection", viewProj);
        grid->SetMat4("u_InverseViewProjection", glm::inverse(viewProj));
        grid->SetFloat3("u_CameraPos", cameraPosition);
        grid->SetInt("u_Plane", plane);
        grid->SetFloat("u_CellSize", gs.CellSize);
        grid->SetFloat("u_MajorEvery", (float)gs.MajorEvery);
        grid->SetFloat4("u_MinorColor", gs.Color);
        grid->SetFloat4("u_MajorColor", gs.MajorColor);
        grid->SetInt("u_ShowAxes", gs.ShowAxes ? 1 : 0);
        grid->SetFloat("u_FadeScale", gs.FadeDistance);
        RenderCommand::DrawArrays(3);
        RenderCommand::SetDepthWrite(true);
        Renderer::GetStats().DrawCalls++;
    }

}
