#pragma once

#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/Animation.h"
#include "Zamin/Renderer/Environment.h"
#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Renderer/RendererAPI.h"

#include <glm/glm.hpp>
#include <vector>

namespace Zamin {

    struct DirectionalLight
    {
        glm::vec3 Direction{ -0.4f, -1.0f, -0.3f }; // direction the light travels
        glm::vec3 Color{ 1.0f };
        float Intensity = 3.0f;
        bool CastShadows = true;
    };

    struct PointLight
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Color{ 1.0f };
        float Intensity = 5.0f;
        float Radius = 10.0f;
    };

    // A cone of light: full brightness within InnerAngle of Direction, a smooth fade
    // (smoothstep of the cosines) to nothing at OuterAngle; Range attenuates like a point light's
    // radius. Angles in degrees, measured from the axis.
    struct SpotLight
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Direction{ 0.0f, -1.0f, 0.0f }; // direction the light travels
        glm::vec3 Color{ 1.0f };
        float Intensity = 20.0f;
        float Range = 15.0f;
        float InnerAngle = 20.0f;
        float OuterAngle = 30.0f;
    };

    struct SceneEnvironment
    {
        bool DrawSky = true;
        glm::vec3 SkyTopColor{ 0.32f, 0.52f, 0.85f };
        glm::vec3 SkyHorizonColor{ 0.78f, 0.86f, 0.95f };
        glm::vec3 GroundColor{ 0.36f, 0.33f, 0.30f };
        float AmbientIntensity = 0.6f;
        float Exposure = 1.0f;

        bool HasSun = false;
        DirectionalLight Sun;
        // Local lights: at most Renderer3D::MaxLocalLights point + spot lights together (the
        // point lights take the slots first).
        std::vector<PointLight> PointLights;
        std::vector<SpotLight> SpotLights;
        float ShadowDistance = 40.0f;

        // Image based lighting / HDRI sky (null = procedural gradient sky)
        Ref<EnvironmentMap> EnvMap;
        float EnvIntensity = 1.0f;
        float EnvRotation = 0.0f; // radians around Y

        bool FogEnabled = false;
        glm::vec3 FogColor{ 0.7f, 0.78f, 0.86f };
        float FogDensity = 0.015f;
        float FogHeightFalloff = 0.05f;
    };

    // How the renderer draws one mesh with its material (the pass, culling, blending, shadow).
    struct SurfaceState
    {
        enum class Pass : uint8_t { Opaque = 0, Cutout, Transparent };

        Pass Queue = Pass::Opaque;
        // Faces culled in the main pass. Double-sided: none (a double-sided transparent surface
        // is drawn twice, back faces first, see TwoSidedBlend). Single-sided: the back faces - the
        // front ones when the transform mirrors the mesh (negative scale turns its winding around).
        CullMode Cull = CullMode::Back;
        bool Mirrored = false;
        bool TwoSidedBlend = false;    // transparent + double-sided: inner (back) faces, then the outer ones
        BlendMode Blend = BlendMode::None;
        bool DepthWrite = true;
        bool CastsShadow = true;       // transparent surfaces never cast (the light goes through)
        bool AlphaTestedShadow = false; // cutout: the shadow has the same holes as the surface
    };

    // Forward PBR mesh renderer with cascaded directional shadows and a gradient / HDRI sky.
    // Materials are Lit or Unlit. Order of a frame: shadow casters (cutout ones alpha tested),
    // opaque surfaces, cutout surfaces (alpha tested), the sky, the editor grid, then transparent
    // surfaces - by Material::SortPriority, then back to front by the center of their bounds (the
    // distance from the camera; along the view direction for orthographic cameras), depth tested but not written,
    // premultiplied alpha blending (Material::PreserveSpecular keeps highlights at full strength).
    // Single-sided materials cull their back faces; double-sided ones draw both, lit from the
    // side that is seen.
    class Renderer3D
    {
    public:
        // Point and spot lights the shader takes at once (together).
        static constexpr uint32_t MaxLocalLights = 32;

        // A point or spot light competing for one of the MaxLocalLights slots.
        struct LightCandidate
        {
            glm::vec3 Position{ 0.0f };
            float Range = 10.0f;     // radius of a point light, range of a spot light
            float Strength = 1.0f;   // intensity x the brightest color channel
        };
        // How much a light matters to a viewer at `viewPosition`: its strength, scaled down by
        // (range / distance)^2 once the viewer is outside its range (about how much of the view
        // the lit sphere covers). No GPU needed.
        static float GetLightImportance(const LightCandidate& light, const glm::vec3& viewPosition);
        // The lights that get a slot: every one (in the given order) when at most `maxCount`,
        // else the `maxCount` most important ones - most important first, equal importance the
        // nearest first, then the given order.
        static std::vector<size_t> SelectLights(const std::vector<LightCandidate>& lights, const glm::vec3& viewPosition,
                                                uint32_t maxCount = MaxLocalLights);
        // The cosines of a spot light's inner / outer angles as the shader uses them (the angles
        // clamped to (0, 89] degrees, inner <= outer; the inner cosine always a bit larger, so the
        // fade is well defined).
        static glm::vec2 GetSpotCosines(float innerAngleDegrees, float outerAngleDegrees);
        // How bright a spot light is in a direction that makes `angleDegrees` with its axis
        // (1 inside the inner cone, 0 outside the outer one, a smoothstep in between).
        static float GetSpotConeFactor(float innerAngleDegrees, float outerAngleDegrees, float angleDegrees);

        static void Init();
        static void Shutdown();

        static void BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                               const SceneEnvironment& environment);
        static void SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Material& material,
                               int entityID = -1, bool castShadows = true, const std::vector<glm::mat4>* joints = nullptr);
        // The editor grid, drawn by EndScene after the opaque surfaces and the sky and before the
        // transparent ones (so it shows through glass instead of being drawn over it).
        static void SubmitGrid(int plane = 0, const GridSettings& settings = {});
        static void EndScene();

        // Editor-only infinite grid, drawn at once. plane: 0 = XZ (3D), 1 = XY (2D). settings.Visible
        // is not checked here (the caller decides whether to draw).
        static void DrawGrid(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition, int plane = 0,
                             const GridSettings& settings = {});

        static uint32_t GetShadowMapRendererID();

        // How a mesh with this material and world transform is drawn (no GPU needed).
        static SurfaceState GetSurfaceState(const Material& material, const glm::mat4& transform, bool castShadows = true);

        // The order transparent surfaces at these world positions are drawn in: back to front
        // along the view direction (the farthest first; equal depths keep their order).
        static std::vector<size_t> SortBackToFront(const glm::mat4& view, const std::vector<glm::vec3>& positions);

        // One transparent surface to sort: the center of its bounds (world space) and its
        // material's Material::SortPriority.
        struct TransparentSortItem
        {
            glm::vec3 Center{ 0.0f };
            int Priority = 0;
        };
        // The order transparent surfaces are drawn in: by priority (lower first), then back to front -
        // `perspective`: by the distance from the camera position (turning the camera in place does
        // not change the order), orthographic: along the view direction. Equal keys keep their order.
        static std::vector<size_t> SortTransparent(const glm::mat4& view, bool perspective, const std::vector<TransparentSortItem>& items);

        // One transparent surface of the last EndScene, in the order it was drawn (the editor's
        // "transparent order" overlay): its entity, the center it was sorted by, its priority and
        // its sort distance.
        struct TransparentDrawRecord
        {
            int EntityID = -1;
            glm::vec3 Center{ 0.0f };
            int Priority = 0;
            float Distance = 0.0f;
        };
        static const std::vector<TransparentDrawRecord>& GetLastTransparentOrder();
        // The camera of that EndScene (projection x view).
        static const glm::mat4& GetLastTransparentViewProjection();
        // How many times EndScene ran (tells whether a scene was drawn in between).
        static uint64_t GetEndSceneCount();
    };

}
