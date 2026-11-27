#pragma once
#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/FoliageHLOD.h"
#include "Zamin/Math/Geometry.h"
#include <array>
#include <span>
#include <vector>
#include <string>

namespace Zamin {
    class Shader;
    enum class FoliageBackend : uint8_t
    {
        Auto,
        CPU,
        GPU
    };
    struct FoliageSettings
    {
        float LODDistance = 40, CullDistance = 180, Hysteresis = .1f;
        // HLOD: cells farther than HLODDistance (0: off) fade into one merged clump proxy each, over HLODFade metres
        // (a dithered crossfade; 0 switches at once). HLODCellSize is the requested world X/Z cell size.
        float HLODDistance = 0, HLODFade = 8, HLODCellSize = 32;
        FoliageBackend Backend = FoliageBackend::Auto;
        void Sanitize();
        bool HLODEnabled() const { return HLODDistance > 0; }
    };
    // Returns near(0), far(1), or culled(2); history retains the selected LOD even when invisible.
    uint32_t ClassifyFoliage(const AABB& bounds, const Frustum& frustum, glm::vec3 camera,
                             const FoliageSettings& settings, uint32_t& history, bool frustumCulling = true);
    // The same box test the GPU pass makes (center/extent against each plane).
    bool FoliageInsideFrustum(const Frustum& frustum, const glm::vec3& center, const glm::vec3& extent);
    struct FoliagePart
    {
        Ref<Mesh> Geometry;
        Material Mat;
    };
    struct FoliageStats
    {
        bool GPU = false, CountsAvailable = false, GPUTimeAvailable = false;
        uint32_t Instances = 0, Near = 0, Far = 0, Culled = 0;
        uint32_t ShadowNear = 0, ShadowFar = 0;    // the shadow set (culled by the cascades' volumes), per LOD
        uint32_t HLODInstances = 0;                // replaced by their cell's proxy this frame
        uint32_t HLODCells = 0;                    // baked cells
        uint32_t HLODCellsDrawn = 0, HLODCellsFading = 0, HLODTriangles = 0, HLODDraws = 0;
        uint32_t HLODBakes = 0, HLODRebakedCells = 0, HLODReusedCells = 0; // the last bake
        uint64_t HLODHash = 0;
        float HLODCellSize = 0;
        double CullCPUMilliseconds = 0;            // the CPU time of the last Cull (the whole pass, or issuing it)
        double CullGPUMilliseconds = 0;            // the GPU pass's time (timestamp queries, a few frames late)
        uint64_t Uploads = 0, CPUBytes = 0, GPUBytes = 0, CountFrame = 0;
        std::string Error;
    };
    class FoliageBatch
    {
    public:
        static constexpr uint32_t MaxInstances = 65536;
        static constexpr uint32_t MaxShadowVolumes = 4;
        FoliageBatch();
        ~FoliageBatch();
        FoliageBatch(const FoliageBatch&) = delete;
        FoliageBatch& operator=(const FoliageBatch&) = delete;
        // Static mesh prototypes only, at most 128 parts per LOD. One picking ID per packed component.
        std::array<std::vector<FoliagePart>, 2> Parts;
        FoliageSettings Settings;
        bool CastShadows = true, Mirrored = false;
        int EntityID = -1;
        AABB Bounds = AABB::Empty();
        // `generation` (optional, non-zero): the caller's version of `worldTransforms`; an unchanged one skips the
        // byte comparison of the whole source.
        bool Upload(const std::vector<glm::mat4>& worldTransforms, const AABB& prototype, int entityID, uint64_t generation = 0);
        // The camera set (per LOD, minus the instances their HLOD cell replaced) and, with shadow volumes (the
        // cascades' light-space boxes; CastShadows on), the shadow set: instances inside any of them.
        void Cull(const Frustum& frustum, glm::vec3 camera, bool frustumCulling = true, std::span<const Frustum> shadowVolumes = {});
        bool Draw(const Ref<Mesh>& mesh, uint32_t lod, bool shadow = false);
        // The visible HLOD cells with the bound (non-instanced mesh) shader: one multi-draw per crossfade step.
        // Returns the draw calls made.
        uint32_t DrawHLOD(Shader& shader);
        const Ref<Mesh>& GetHLODMesh() const;
        const Material& GetHLODMaterial() const;
        const FoliageHLODBake& GetHLODBake() const;
        const FoliageStats& GetStats() const;
        const std::vector<glm::mat4>& GetTransforms() const;
        // Explicit synchronous diagnostic readback for tests/tools; normal rendering never waits.
        std::array<uint32_t, 2> DebugVisibleCounts();
        std::array<uint32_t, 2> DebugShadowCounts();
        // Waits for the pending GPU timestamp results (tests/benchmarks).
        void DebugResolveTimings();
    private:
        struct Impl;
        Scope<Impl> m_Impl;
    };
}
