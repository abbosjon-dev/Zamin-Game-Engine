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

#define MAX_POINT_LIGHTS 16

uniform vec4 u_Albedo;
uniform sampler2D u_AlbedoMap;
uniform sampler2D u_NormalMap;
uniform sampler2D u_MetalRoughMap; // glTF layout: G = roughness, B = metallic
uniform sampler2D u_EmissiveMap;
uniform int u_HasNormalMap;
uniform int u_HasMetalRoughMap;
uniform int u_HasEmissiveMap;
uniform float u_NormalStrength;
uniform float u_Tiling;
uniform float u_Metallic;
uniform float u_Roughness;
uniform vec3 u_Emission;

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

uniform int u_PointLightCount;
uniform vec3 u_PointPositions[MAX_POINT_LIGHTS];
uniform vec3 u_PointRadiance[MAX_POINT_LIGHTS];
uniform float u_PointRadius[MAX_POINT_LIGHTS];

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

vec3 BRDF(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
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

void main()
{
    vec2 uv = v_TexCoord * u_Tiling;
    vec4 texColor = texture(u_AlbedoMap, uv);
    vec4 baseColor = u_Albedo * vec4(toLinear(texColor.rgb), texColor.a);
    if (baseColor.a < 0.05)
        discard;
    vec3 albedo = toLinear(u_Albedo.rgb) * toLinear(texColor.rgb);
    float metallic = clamp(u_Metallic, 0.0, 1.0);
    float roughness = clamp(u_Roughness, 0.04, 1.0);
    if (u_HasMetalRoughMap == 1)
    {
        vec4 mr = texture(u_MetalRoughMap, uv);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
    }

    vec3 N = normalize(v_Normal);
    if (!gl_FrontFacing) N = -N;
    if (u_HasNormalMap == 1)
    {
        vec3 tn = texture(u_NormalMap, uv).xyz * 2.0 - 1.0;
        tn.xy *= u_NormalStrength;
        vec3 V0 = u_CameraPos - v_WorldPos;
        N = normalize(CotangentFrame(N, -V0, uv) * tn);
    }
    vec3 V = normalize(u_CameraPos - v_WorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    if (u_HasSun == 1)
    {
        float shadow = u_ShadowsEnabled == 1 ? ShadowFactor(N, normalize(-u_SunDirection)) : 1.0;
        Lo += BRDF(N, V, normalize(-u_SunDirection), u_SunRadiance, albedo, metallic, roughness, F0) * shadow;
    }
    for (int i = 0; i < u_PointLightCount; i++)
    {
        vec3 toLight = u_PointPositions[i] - v_WorldPos;
        float dist = length(toLight);
        float falloff = clamp(1.0 - pow(dist / max(u_PointRadius[i], 0.001), 4.0), 0.0, 1.0);
        float attenuation = falloff * falloff / (dist * dist + 1.0);
        Lo += BRDF(N, V, toLight / max(dist, 1e-4), u_PointRadiance[i] * attenuation, albedo, metallic, roughness, F0);
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
    vec3 ambient = (kD * albedo * irradiance + specularEnv) * u_Ambient;

    vec3 emission = u_Emission;
    if (u_HasEmissiveMap == 1)
        emission *= toLinear(texture(u_EmissiveMap, uv).rgb);
    // Linear HDR output - tonemapping & gamma happen in post-processing.
    vec3 color = (ambient + Lo + emission) * u_Exposure;
    if (u_FogEnabled == 1)
    {
        float dist = length(u_CameraPos - v_WorldPos);
        float heightFactor = exp(-u_FogHeightFalloff * max(v_WorldPos.y, 0.0));
        float fog = 1.0 - exp(-dist * u_FogDensity * heightFactor);
        color = mix(color, toLinear(u_FogColor) * u_Exposure, clamp(fog, 0.0, 1.0));
    }
    o_Color = vec4(color, baseColor.a);
    o_EntityID = u_EntityID;
}
)";

        const char* s_ShadowVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
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
    gl_Position = u_LightSpace * u_Model * p;
}
)";

        std::string Variant(const char* src, bool skinned)
        {
            std::string s(src);
            size_t eol = s.find('\n');
            std::string defs = "\n#define MAX_JOINTS " + std::to_string(MaxJoints) + "\n" + (skinned ? "#define SKINNED\n" : "");
            return s.substr(0, eol) + defs + s.substr(eol + 1);
        }

        const char* s_ShadowFS = R"(#version 410 core
void main() { }
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
uniform int u_Plane; // 0 = XZ, 1 = XY

vec4 Grid(vec2 coord, float scale, vec2 axisCoord)
{
    vec2 c = coord / scale;
    vec2 derivative = fwidth(c);
    vec2 grid = abs(fract(c - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    float alpha = 1.0 - min(line, 1.0);
    vec4 color = vec4(0.45, 0.47, 0.52, alpha);
    vec2 minDer = min(derivative, 1.0) * scale;
    if (abs(axisCoord.x) < minDer.x * 1.0) color = vec4(0.25, 0.45, 1.0, max(alpha, 0.9)); // vertical axis line
    if (abs(axisCoord.y) < minDer.y * 1.0) color = vec4(1.0, 0.3, 0.3, max(alpha, 0.9));   // X axis line
    return color;
}

void main()
{
    vec3 dir = v_Far - v_Near;
    float t = u_Plane == 0 ? -v_Near.y / dir.y : -v_Near.z / dir.z;
    if (t <= 0.0)
        discard;
    vec3 p = v_Near + t * dir;
    vec2 coord = u_Plane == 0 ? p.xz : p.xy;
    vec2 axis = u_Plane == 0 ? vec2(p.x, p.z) : vec2(p.x, p.y);

    vec4 clip = u_ViewProjection * vec4(p, 1.0);
    float depth = clip.z / clip.w;
    gl_FragDepth = depth * 0.5 + 0.5;

    float dist = length(p - u_CameraPos);
    float camHeight = abs(u_Plane == 0 ? u_CameraPos.y : u_CameraPos.z);
    float fade = 1.0 - smoothstep(camHeight * 4.0 + 10.0, camHeight * 12.0 + 40.0, dist);

    vec4 small = Grid(coord, 1.0, axis);
    vec4 big = Grid(coord, 10.0, axis);
    vec4 color = big.a > small.a * 0.9 ? big : small;
    color.a *= fade * 0.75;
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
            Ref<Shader> SkyShader;
            Ref<Shader> GridShader;
            Ref<Texture2D> WhiteTexture;
            Ref<Framebuffer> ShadowMap;
            uint32_t ShadowMapSize = 4096; // 2x2 atlas of 2048 cascades

            glm::mat4 View{ 1.0f }, Projection{ 1.0f };
            glm::vec3 CameraPosition{ 0.0f };
            SceneEnvironment Environment;
            std::vector<DrawItem> Queue;
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
                for (auto& sh : { s_Data->ShadowShader, s_Data->SkinnedShadowShader })
                {
                    sh->Bind();
                    sh->SetMat4("u_LightSpace", cascades.LightSpace[c]);
                }
                for (const auto& item : s_Data->Queue)
                {
                    if (!item.CastShadows) continue;
                    bool skinned = item.MeshRef->IsSkinned();
                    auto& sh = skinned ? s_Data->SkinnedShadowShader : s_Data->ShadowShader;
                    sh->Bind();
                    if (skinned) sh->SetMat4Array("u_Joints", item.Joints.data(), (uint32_t)item.Joints.size());
                    sh->SetMat4("u_Model", item.Transform);
                    RenderCommand::DrawIndexed(item.MeshRef->GetVertexArray(), item.MeshRef->GetIndexCount());
                    stats.DrawCalls++;
                }
            }
            RenderCommand::RestoreRenderTarget();
        }

        // ---------------- main pass
        if (!s_Data->Queue.empty())
        {
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

            uint32_t count = std::min<uint32_t>((uint32_t)env.PointLights.size(), MaxPointLights);
            shader->SetInt("u_PointLightCount", (int)count);
            for (uint32_t i = 0; i < count; i++)
            {
                const auto& pl = env.PointLights[i];
                std::string idx = "[" + std::to_string(i) + "]";
                shader->SetFloat3("u_PointPositions" + idx, pl.Position);
                shader->SetFloat3("u_PointRadiance" + idx, pl.Color * pl.Intensity);
                shader->SetFloat("u_PointRadius" + idx, pl.Radius);
            }

            shader->SetInt("u_AlbedoMap", 0);
            shader->SetInt("u_ShadowMap", 1);
            shader->SetInt("u_NormalMap", 2);
            shader->SetInt("u_MetalRoughMap", 3);
            shader->SetInt("u_EmissiveMap", 4);
          }
            RenderCommand::BindTextureUnit(1, s_Data->ShadowMap->GetDepthAttachmentRendererID());
            (env.EnvMap ? env.EnvMap->GetTexture() : s_Data->WhiteTexture)->Bind(5);

            RenderCommand::SetCullMode(CullMode::None);
            for (const auto& item : s_Data->Queue)
            {
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
                shader->SetFloat("u_NormalStrength", m.NormalStrength);
                shader->SetMat4("u_Model", item.Transform);
                shader->SetMat3("u_NormalMatrix", glm::transpose(glm::inverse(glm::mat3(item.Transform))));
                shader->SetFloat4("u_Albedo", m.Albedo);
                shader->SetFloat("u_Tiling", m.Tiling);
                shader->SetFloat("u_Metallic", m.Metallic);
                shader->SetFloat("u_Roughness", m.Roughness);
                shader->SetFloat3("u_Emission", m.Emission * m.EmissionStrength);
                shader->SetInt("u_EntityID", item.EntityID);
                RenderCommand::DrawIndexed(item.MeshRef->GetVertexArray(), item.MeshRef->GetIndexCount());
                stats.DrawCalls++;
                stats.MeshCount++;
                stats.TriangleCount += item.MeshRef->GetIndexCount() / 3;
            }
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

        s_Data->Queue.clear();
    }

    void Renderer3D::DrawGrid(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition, int plane)
    {
        glm::mat4 viewProj = projection * view;
        RenderCommand::SetDepthWrite(false);
        auto& grid = s_Data->GridShader;
        grid->Bind();
        grid->SetMat4("u_ViewProjection", viewProj);
        grid->SetMat4("u_InverseViewProjection", glm::inverse(viewProj));
        grid->SetFloat3("u_CameraPos", cameraPosition);
        grid->SetInt("u_Plane", plane);
        RenderCommand::DrawArrays(3);
        RenderCommand::SetDepthWrite(true);
        Renderer::GetStats().DrawCalls++;
    }

}
