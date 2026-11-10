#pragma once

/// @file
/// @brief Renderer3D: the forward PBR mesh renderer (lights, cascaded shadows, sky, instancing, culling).

#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/Animation.h"
#include "Zamin/Renderer/Environment.h"
#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Renderer/RendererAPI.h"
#include "Zamin/Renderer/Frustum.h"

#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

namespace Zamin {

    struct TerrainSurface;
    class FoliageBatch;
    struct DirectionalLight
    {
        glm::vec3 Direction{ -0.4f, -1.0f, -0.3f }; ///< direction the light travels
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

    /// A cone of light: full brightness within InnerAngle of Direction, a smooth fade
    /// (smoothstep of the cosines) to nothing at OuterAngle; Range attenuates like a point light's
    /// radius. Angles in degrees, measured from the axis.
    struct SpotLight
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Direction{ 0.0f, -1.0f, 0.0f }; ///< direction the light travels
        glm::vec3 Color{ 1.0f };
        float Intensity = 20.0f;
        float Range = 15.0f;
        float InnerAngle = 20.0f;
        float OuterAngle = 30.0f;
    };

    /// Editor view modes of the 3D scene: what the mesh shader shows instead of the lit picture.
    enum class RenderDebugView : uint8_t
    {
        Lit = 0,       ///< the normal picture
        LightingOnly,  ///< the lights (sun, local, ambient, shadows) on a plain white surface
        Normals,       ///< world-space normals (after normal maps) as colors: x red, y green, z blue
        Albedo,        ///< the base color (x its map), unlit
        Roughness,     ///< grey: 0 black (mirror) .. 1 white (matte)
        Metallic,      ///< grey: 0 black (dielectric) .. 1 white (metal)
    };
    constexpr int RenderDebugViewCount = 6;

    /// Meshes drawn as their triangle edges: Wire - only the edges (in the view's colors; the
    /// entity IDs for picking still cover the whole faces), ShadedWire - the normal picture with
    /// dark edges over it.
    enum class WireframeMode : uint8_t
    {
        Off = 0,
        Wire,
        ShadedWire
    };

    /// How the edge of a sun shadow is softened: Hard - one (hardware bilinear) comparison,
    /// PCF3x3 / PCF5x5 - a grid of comparisons one texel apart, Poisson - 16 comparisons spread
    /// over a disc (softer than 5x5 for fewer samples).
    enum class ShadowFilter : uint8_t
    {
        Hard = 0,
        PCF3x3,
        PCF5x5,
        Poisson
    };
    constexpr int ShadowFilterCount = 4;

    /// Quality of the sun's cascaded shadow maps (SceneSettings::Shadows). The defaults are the
    /// renderer's original fixed values. The cascades are tiles of one square depth atlas: 2x2
    /// tiles, or the whole atlas when there is a single cascade.
    struct ShadowSettings
    {
        static constexpr uint32_t MinAtlasSize = 1024;
        static constexpr uint32_t MaxAtlasSize = 4096;
        static constexpr int MaxCascades = 4;
        static constexpr float MaxDepthBias = 0.01f;
        static constexpr float MaxNormalBias = 8.0f;

        uint32_t AtlasSize = 4096;            ///< 1024, 2048 or 4096 texels per side
        ShadowFilter Filter = ShadowFilter::PCF3x3;
        int CascadeCount = 3;                 ///< 1 .. MaxCascades (perspective cameras; orthographic ones use one)
        float CascadeSplit = 0.75f;           ///< where the cascades end: 0 evenly spaced .. 1 logarithmic (finer near the camera)
        float DepthBias = 0.0005f;            ///< subtracted from the depth that is compared (light space, 0..1): less acne, more gap
        float NormalBias = 1.0f;              ///< x the offset along the surface normal (about 1.5 .. 3.5 shadow texels)

        /// Values from a file or a script brought into range: the atlas size to the nearest of
        /// 1024 / 2048 / 4096, the counts and biases clamped, anything not finite to its default.
        void Sanitize();
        /// The cascades a camera uses: CascadeCount for a perspective camera, one for an
        /// orthographic camera (its single cascade then has the whole atlas).
        int GetCascadeCount(bool perspective = true) const { return perspective ? CascadeCount : 1; }
        /// The texels per side of one cascade's tile.
        uint32_t GetTileSize(bool perspective = true) const { return GetCascadeCount(perspective) <= 1 ? AtlasSize : AtlasSize / 2; }
        /// Tiles per side of the atlas (1 for a single cascade, else 2).
        int GetAtlasTiles(bool perspective = true) const { return GetCascadeCount(perspective) <= 1 ? 1 : 2; }
    };

    /// The gradient sky (scenes without an HDRI): how its zenith / horizon / ground colours blend,
    /// and the sun disc drawn where the sun (the main directional light) shines from. The image
    /// based lighting of such scenes (ambient, reflections) uses the same gradient. The defaults are
    /// the renderer's original look.
    struct SkySettings
    {
        static constexpr float DefaultSunSize = 2.25f;
        static constexpr float MinSunSize = 0.25f, MaxSunSize = 20.0f;

        float Curve = 0.6f;        ///< above the horizon: exponent of the horizon -> zenith blend (smaller: the zenith colour reaches lower)
        float GroundFade = 3.0f;   ///< below the horizon: how fast the ground colour takes over (larger: a thinner horizon band)
        bool SunDisc = true;
        float SunSize = DefaultSunSize; ///< degrees from the sun's centre where the disc is at half brightness
        float SunIntensity = 4.0f; ///< the disc's brightness (x the sun's colour)
        float SunGlow = 0.18f;     ///< the wide halo around it (x the sun's colour)

        /// Values from a file or a script brought into range (anything not finite to its default).
        void Sanitize();
        /// The exponent of the disc's falloff, pow(cos angle, e): 900 at the default size, ~ 1 / size^2.
        float GetSunDiscExponent() const;
        /// How bright the sky's sun is (x the sun's colour, disc + halo) `angleDegrees` from its
        /// centre - what the sky shader adds. No GPU needed.
        float GetSunBrightness(float angleDegrees) const;
    };

    struct SceneEnvironment
    {
        bool DrawSky = true;
        glm::vec3 SkyTopColor{ 0.32f, 0.52f, 0.85f };
        glm::vec3 SkyHorizonColor{ 0.78f, 0.86f, 0.95f };
        glm::vec3 GroundColor{ 0.36f, 0.33f, 0.30f };
        SkySettings Sky;
        float AmbientIntensity = 0.6f;
        float Exposure = 1.0f;

        /// The sun: the main directional light. It casts the shadows (when its CastShadows is on)
        /// and the gradient sky's sun disc follows it.
        bool HasSun = false;
        DirectionalLight Sun;
        /// More directional lights: they light the scene like the sun but cast no shadow (at most
        /// Renderer3D::MaxDirectionalLights - 1; their CastShadows is not looked at).
        std::vector<DirectionalLight> ExtraSuns;
        /// Local lights: at most Renderer3D::MaxLocalLights point + spot lights together (the
        /// point lights take the slots first).
        std::vector<PointLight> PointLights;
        std::vector<SpotLight> SpotLights;
        float ShadowDistance = 40.0f;
        ShadowSettings Shadows;

        /// Image based lighting / HDRI sky (null = procedural gradient sky)
        Ref<EnvironmentMap> EnvMap;
        float EnvIntensity = 1.0f;
        float EnvRotation = 0.0f; ///< radians around Y

        bool FogEnabled = false;
        glm::vec3 FogColor{ 0.7f, 0.78f, 0.86f };
        float FogDensity = 0.015f;
        float FogHeightFalloff = 0.05f;

        /// Editor view (the scene view's toolbar; runtime and previews keep the defaults).
        RenderDebugView DebugView = RenderDebugView::Lit;
        WireframeMode Wireframe = WireframeMode::Off;
    };

    /// How the renderer draws one mesh with its material (the pass, culling, blending, shadow).
    struct SurfaceState
    {
        enum class Pass : uint8_t
        {
            Opaque = 0,
            Cutout,
            Transparent
        };

        Pass Queue = Pass::Opaque;
        /// Faces culled in the main pass. Double-sided: none (a double-sided transparent surface
        /// is drawn twice, back faces first, see TwoSidedBlend). Single-sided: the back faces - the
        /// front ones when the transform mirrors the mesh (negative scale turns its winding around).
        CullMode Cull = CullMode::Back;
        bool Mirrored = false;
        bool TwoSidedBlend = false;    ///< transparent + double-sided: inner (back) faces, then the outer ones
        BlendMode Blend = BlendMode::None;
        bool DepthWrite = true;
        bool CastsShadow = true;       ///< transparent surfaces never cast (the light goes through)
        bool AlphaTestedShadow = false; ///< cutout: the shadow has the same holes as the surface
    };

    /// Forward PBR mesh renderer with cascaded directional shadows and a gradient / HDRI sky.
    /// Materials are Lit or Unlit. Order of a frame: shadow casters (cutout ones alpha tested),
    /// opaque surfaces, cutout surfaces (alpha tested), the sky, the editor grid, then transparent
    /// surfaces - by Material::SortPriority, then back to front by the center of their bounds (the
    /// distance from the camera; along the view direction for orthographic cameras), depth tested but not written,
    /// premultiplied alpha blending (Material::PreserveSpecular keeps highlights at full strength).
    /// Single-sided materials cull their back faces; double-sided ones draw both, lit from the
    /// side that is seen.
    class Renderer3D
    {
    public:
        /// Point and spot lights the shader takes at once (together).
        static constexpr uint32_t MaxLocalLights = 32;
        /// Directional lights the shader takes at once: the sun (SceneEnvironment::Sun) and up to
        /// three more without shadows (SceneEnvironment::ExtraSuns).
        static constexpr uint32_t MaxDirectionalLights = 4;

        /// A point or spot light competing for one of the MaxLocalLights slots.
        struct LightCandidate
        {
            glm::vec3 Position{ 0.0f };
            float Range = 10.0f;     ///< radius of a point light, range of a spot light
            float Strength = 1.0f;   ///< intensity x the brightest color channel
        };
        /// How much a light matters to a viewer at `viewPosition`: its strength, scaled down by
        /// (range / distance)^2 once the viewer is outside its range (about how much of the view
        /// the lit sphere covers). No GPU needed.
        static float GetLightImportance(const LightCandidate& light, const glm::vec3& viewPosition);
        /// The lights that get a slot: every one (in the given order) when at most `maxCount`,
        /// else the `maxCount` most important ones - most important first, equal importance the
        /// nearest first, then the given order.
        static std::vector<size_t> SelectLights(const std::vector<LightCandidate>& lights, const glm::vec3& viewPosition,
                                                uint32_t maxCount = MaxLocalLights);
        /// The cosines of a spot light's inner / outer angles as the shader uses them (the angles
        /// clamped to (0, 89] degrees, inner <= outer; the inner cosine always a bit larger, so the
        /// fade is well defined).
        static glm::vec2 GetSpotCosines(float innerAngleDegrees, float outerAngleDegrees);
        /// How bright a spot light is in a direction that makes `angleDegrees` with its axis
        /// (1 inside the inner cone, 0 outside the outer one, a smoothstep in between).
        static float GetSpotConeFactor(float innerAngleDegrees, float outerAngleDegrees, float angleDegrees);

        /// Whether a debug view's picture goes through tonemapping and the other effects as it is
        /// (lit, lighting only) or must be shown as it is (normals, albedo, roughness, metallic: the
        /// values themselves, see PostProcessSettings::Neutral).
        static bool DebugViewUsesPostProcessing(RenderDebugView view);

        /// Frustum culling: meshes whose world bounds are outside the camera's view are not drawn,
        /// shadow casters outside a cascade's volume not into it; the scene's 2D sprites, circles,
        /// texts, particle systems and tilemap chunks follow the same switch (on by default; off to
        /// compare).
        static void SetFrustumCulling(bool enabled);
        static bool IsFrustumCullingEnabled();  ///< see SetFrustumCulling
        /// The world-space bounds a mesh is culled by: its bounds moved by the transform - for a
        /// skinned mesh conservative bounds of every pose the joints can give it (see SkinnedBounds).
        static AABB GetWorldBounds(const Mesh& mesh, const glm::mat4& transform, const std::vector<glm::mat4>* joints = nullptr);

        /// GPU instancing: opaque and cutout meshes that are not skinned and share the mesh, the
        /// material (every value the shader takes from it) and the facing are drawn with one
        /// instanced call - the transforms and entity IDs per instance (the picture and the picking
        /// buffer stay the same). Shadow casters are grouped the same way per cascade. On by
        /// default; off to compare.
        static void SetInstancing(bool enabled);
        static bool IsInstancingEnabled();  ///< see SetInstancing
        /// The most instances one call draws (bigger groups are split).
        static constexpr uint32_t MaxInstancesPerDraw = 512;

        /// One mesh to draw, as instancing sees it (no GPU needed).
        struct InstanceCandidate
        {
            const void* MeshKey = nullptr;     ///< the mesh (only its identity counts)
            const Material* Mat = nullptr;
            bool Skinned = false;              ///< skinned meshes are drawn one by one (their own joints)
            bool Mirrored = false;             ///< a negative scale turns the winding (and the culled side) around
        };
        /// Whether meshes with these materials can share an instanced draw: every value the mesh
        /// shader reads from a material is equal (the textures the same objects).
        static bool SameInstanceMaterial(const Material& a, const Material& b);
        /// The draws `items` turn into: groups of indices, in the order of each group's first item,
        /// the items of a group in their own order. Opaque / cutout items that are not skinned and
        /// share the mesh, an equal material and the facing are grouped (at most `maxPerDraw` per
        /// group); transparent and skinned ones are groups of one. Without instancing: every item alone.
        static std::vector<std::vector<size_t>> BuildInstanceBatches(const std::vector<InstanceCandidate>& items,
                                                                     uint32_t maxPerDraw = MaxInstancesPerDraw);

        /// Shadow pass culling by what the camera sees (task 490; on by default, with frustum
        /// culling): a caster inside a cascade's volume is still left out of it when no point the
        /// camera sees in that cascade's depth range can be in its shadow - the caster swept along
        /// the light's direction misses the view (casters behind the camera, beside the view, or
        /// whose shadow falls on what another cascade draws). The picture stays the same: the test
        /// is conservative and keeps a margin for the filter and the normal offset.
        static void SetShadowReceiverCulling(bool enabled);
        static bool IsShadowReceiverCullingEnabled();  ///< see SetShadowReceiverCulling
        /// The volume whose points may receive the shadows of one cascade: the camera's view
        /// (`viewProjection`) between view depths `nearDepth` and `farDepth`, every plane pushed
        /// out by `margin` world units. No GPU needed.
        static Frustum GetShadowReceiverVolume(const glm::mat4& view, const glm::mat4& viewProjection, float nearDepth, float farDepth,
                                               float margin);
        /// Whether `caster` can shadow anything in `receivers`: the box moved up to `reach` along
        /// `lightDirection` (where the light travels; unit length) touches the volume - exact per
        /// plane, conservative like Frustum::Intersects. No GPU needed.
        static bool ShadowReachesVolume(const AABB& caster, const glm::vec3& lightDirection, float reach, const Frustum& receivers);

        /// Where the shadow cascades of a perspective camera end (view depths): `count` splits
        /// between `nearDepth` and `farDepth`, a mix of even and logarithmic spacing (`split`
        /// from 0 to 1). The last one is (up to rounding) `farDepth`. No GPU needed.
        static std::vector<float> GetCascadeSplits(float nearDepth, float farDepth, int count, float split);

        /// Creates the shaders, the shadow atlas and the sky resources (Renderer::Init calls it).
        static void Init();
        static void Shutdown();

        /// Starts a 3D scene: the camera (view, projection, position) and the environment (lights, sky,
        /// fog, shadows - limited by the active quality level). Meshes are queued until EndScene.
        static void BeginScene(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                               const SceneEnvironment& environment);
        /// Queues one mesh with its world transform and material (nothing is drawn yet). `joints`: the
        /// pose of a skinned mesh; `entityID`: the editor's picking ID (-1 none).
        static void SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Material& material,
                               int entityID = -1, bool castShadows = true, const std::vector<glm::mat4>* joints = nullptr);
        /// An opaque terrain mesh with shared control/albedo/normal/ORM inputs.
        static void SubmitTerrain(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<TerrainSurface>& surface,
                                  int entityID = -1, bool castShadows = true);
        static void SubmitFoliage(const Ref<FoliageBatch>& batch);
        /// The editor grid, drawn by EndScene after the opaque surfaces and the sky and before the
        /// transparent ones (so it shows through glass instead of being drawn over it).
        static void SubmitGrid(int plane = 0, const GridSettings& settings = {});
        /// Draws the queued meshes: culling, the shadow cascades, the opaque, cutout, sky, grid and
        /// transparent passes in that order (see the class description).
        static void EndScene();

        /// Editor-only infinite grid, drawn at once. plane: 0 = XZ (3D), 1 = XY (2D). settings.Visible
        /// is not checked here (the caller decides whether to draw).
        static void DrawGrid(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition, int plane = 0,
                             const GridSettings& settings = {});

        static uint32_t GetShadowMapRendererID();  ///< the shadow atlas texture (the editor's debug view)

        /// How a mesh with this material and world transform is drawn (no GPU needed).
        static SurfaceState GetSurfaceState(const Material& material, const glm::mat4& transform, bool castShadows = true);

        /// The order transparent surfaces at these world positions are drawn in: back to front
        /// along the view direction (the farthest first; equal depths keep their order).
        static std::vector<size_t> SortBackToFront(const glm::mat4& view, const std::vector<glm::vec3>& positions);

        /// One transparent surface to sort: the center of its bounds (world space) and its
        /// material's Material::SortPriority.
        struct TransparentSortItem
        {
            glm::vec3 Center{ 0.0f };
            int Priority = 0;
        };
        /// The order transparent surfaces are drawn in: by priority (lower first), then back to front -
        /// `perspective`: by the distance from the camera position (turning the camera in place does
        /// not change the order), orthographic: along the view direction. Equal keys keep their order.
        static std::vector<size_t> SortTransparent(const glm::mat4& view, bool perspective, const std::vector<TransparentSortItem>& items);

        /// One transparent surface of the last EndScene, in the order it was drawn (the editor's
        /// "transparent order" overlay): its entity, the center it was sorted by, its priority and
        /// its sort distance.
        struct TransparentDrawRecord
        {
            int EntityID = -1;
            glm::vec3 Center{ 0.0f };
            int Priority = 0;
            float Distance = 0.0f;
        };
        static const std::vector<TransparentDrawRecord>& GetLastTransparentOrder();
        /// The camera of that EndScene (projection x view).
        static const glm::mat4& GetLastTransparentViewProjection();
        /// How many times EndScene ran (tells whether a scene was drawn in between).
        static uint64_t GetEndSceneCount();

        /// One mesh drawn by a scene, kept for the editor's selection outline (it draws the
        /// silhouettes of the selected meshes again, also where they are hidden behind others).
        struct CapturedDraw
        {
            Ref<Mesh> MeshRef;
            glm::mat4 Transform{ 1.0f };
            int EntityID = -1;
            std::vector<glm::mat4> Joints;  ///< skinned meshes: the pose they were drawn in
            /// Cutout materials: the albedo alpha is tested like the surface does (AlphaCutoff < 0: no test).
            Ref<Texture2D> AlphaMap;
            float AlbedoAlpha = 1.0f;
            float AlphaCutoff = -1.0f;
            glm::vec2 Tiling{ 1.0f }, Offset{ 0.0f };
        };
        /// From BeginCapture to EndCapture every EndScene keeps the meshes it draws of these
        /// entities (their IDs); BeginCapture forgets what an earlier capture kept.
        static void BeginCapture(std::vector<int> entityIDs);
        static void EndCapture();
        static const std::vector<CapturedDraw>& GetCapturedDraws();
    };

}
