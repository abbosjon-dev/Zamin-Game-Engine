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

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;
uniform mat3 u_NormalMatrix;
uniform mat4 u_LightSpace;

out vec3 v_WorldPos;
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec4 v_LightSpacePos;

void main()
{
    vec4 world = u_Model * vec4(a_Position, 1.0);
    v_WorldPos = world.xyz;
    v_Normal = normalize(u_NormalMatrix * a_Normal);
    v_TexCoord = a_TexCoord;
    v_LightSpacePos = u_LightSpace * vec4(world.xyz + v_Normal * 0.02, 1.0);
    gl_Position = u_ViewProjection * world;
}
)";

        const char* s_MeshFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec4 v_LightSpacePos;

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
uniform float u_ShadowTexel;

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

float ShadowFactor()
{
    vec3 proj = v_LightSpacePos.xyz / v_LightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    float bias = 0.0015;
    float sum = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            sum += texture(u_ShadowMap, vec3(proj.xy + vec2(x, y) * u_ShadowTexel, proj.z - bias));
    return sum / 9.0;
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
    // Blur towards the average for rough surfaces.
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
        float shadow = u_ShadowsEnabled == 1 ? ShadowFactor() : 1.0;
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
    vec3 irradiance = mix(toLinear(u_GroundColor), mix(toLinear(u_HorizonColor), toLinear(u_SkyColor), 0.5), N.y * 0.5 + 0.5);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 R = reflect(-V, N);
    vec3 specularEnv = SkyRadiance(R, roughness) * F * mix(1.0, 0.35, roughness);
    vec3 ambient = (kD * albedo * irradiance + specularEnv) * u_Ambient;

    vec3 emission = u_Emission;
    if (u_HasEmissiveMap == 1)
        emission *= toLinear(texture(u_EmissiveMap, uv).rgb);
    // Linear HDR output - tonemapping & gamma happen in post-processing.
    vec3 color = (ambient + Lo + emission) * u_Exposure;
    o_Color = vec4(color, baseColor.a);
    o_EntityID = u_EntityID;
}
)";

        const char* s_ShadowVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
uniform mat4 u_LightSpace;
uniform mat4 u_Model;
void main() { gl_Position = u_LightSpace * u_Model * vec4(a_Position, 1.0); }
)";

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
void main()
{
    vec3 dir = normalize(v_Direction - u_CameraPos);
    float y = dir.y;
    vec3 color = y > 0.0 ? mix(u_Horizon, u_Top, pow(clamp(y, 0.0, 1.0), 0.6))
                         : mix(u_Horizon, u_Ground, pow(clamp(-y * 3.0, 0.0, 1.0), 0.5));
    if (u_HasSun == 1)
    {
        float d = max(dot(dir, normalize(-u_SunDirection)), 0.0);
        color += u_SunColor * (pow(d, 900.0) * 4.0 + pow(d, 12.0) * 0.18);
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
        };

        struct Renderer3DData
        {
            Ref<Shader> MeshShader;
            Ref<Shader> ShadowShader;
            Ref<Shader> SkyShader;
            Ref<Shader> GridShader;
            Ref<Texture2D> WhiteTexture;
            Ref<Framebuffer> ShadowMap;
            uint32_t ShadowMapSize = 2048;

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
        s_Data->MeshShader = Shader::Create("Renderer3D_PBR", s_MeshVS, s_MeshFS);
        s_Data->ShadowShader = Shader::Create("Renderer3D_Shadow", s_ShadowVS, s_ShadowFS);
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

    void Renderer3D::SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Material& material, int entityID, bool castShadows)
    {
        if (!mesh) return;
        s_Data->Queue.push_back({ mesh, transform, material, entityID, castShadows });
    }

    static glm::mat4 ComputeLightSpace(const SceneEnvironment& env, const glm::vec3& cameraPos, const glm::mat4& view)
    {
        glm::vec3 dir = glm::normalize(env.Sun.Direction);
        // Center the shadow box in front of the camera.
        glm::vec3 forward = -glm::vec3(glm::inverse(view)[2]);
        glm::vec3 center = cameraPos + forward * (env.ShadowDistance * 0.35f);
        float half = env.ShadowDistance * 0.5f;

        glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        glm::mat4 lightView = glm::lookAt(center - dir * env.ShadowDistance, center, up);

        // Snap to texel grid to avoid shimmering when the camera moves.
        float texel = (half * 2.0f) / (float)s_Data->ShadowMapSize;
        glm::vec4 origin = lightView * glm::vec4(0, 0, 0, 1);
        glm::vec2 snapped = glm::floor(glm::vec2(origin) / texel) * texel;
        glm::mat4 snap = glm::translate(glm::mat4(1.0f), glm::vec3(snapped - glm::vec2(origin), 0.0f));

        glm::mat4 lightProj = glm::ortho(-half, half, -half, half, 0.1f, env.ShadowDistance * 2.5f);
        return lightProj * snap * lightView;
    }

    void Renderer3D::EndScene()
    {
        auto& env = s_Data->Environment;
        auto& stats = Renderer::GetStats();
        const glm::mat4 viewProj = s_Data->Projection * s_Data->View;

        bool shadows = env.HasSun && env.Sun.CastShadows && !s_Data->Queue.empty();
        glm::mat4 lightSpace(1.0f);

        // ---------------- shadow pass
        if (shadows)
        {
            lightSpace = ComputeLightSpace(env, s_Data->CameraPosition, s_Data->View);
            RenderCommand::SaveRenderTarget();
            s_Data->ShadowMap->Bind();
            RenderCommand::SetDepthWrite(true);
            RenderCommand::Clear();
            s_Data->ShadowShader->Bind();
            s_Data->ShadowShader->SetMat4("u_LightSpace", lightSpace);
            for (const auto& item : s_Data->Queue)
            {
                if (!item.CastShadows) continue;
                s_Data->ShadowShader->SetMat4("u_Model", item.Transform);
                RenderCommand::DrawIndexed(item.MeshRef->GetVertexArray(), item.MeshRef->GetIndexCount());
                stats.DrawCalls++;
            }
            RenderCommand::RestoreRenderTarget();
        }

        // ---------------- main pass
        if (!s_Data->Queue.empty())
        {
            auto& shader = s_Data->MeshShader;
            shader->Bind();
            shader->SetMat4("u_ViewProjection", viewProj);
            shader->SetMat4("u_LightSpace", lightSpace);
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
            RenderCommand::BindTextureUnit(1, s_Data->ShadowMap->GetDepthAttachmentRendererID());

            RenderCommand::SetCullMode(CullMode::None);
            for (const auto& item : s_Data->Queue)
            {
                const Material& m = item.Mat;
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
