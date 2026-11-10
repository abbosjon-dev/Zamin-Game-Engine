#pragma once
#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Math/Geometry.h"
#include <array>
#include <vector>
#include <string>

namespace Zamin {
    enum class FoliageBackend : uint8_t
    {
        Auto,
        CPU,
        GPU
    };
    struct FoliageSettings
    {
        float LODDistance = 40, CullDistance = 180, Hysteresis = .1f;
        FoliageBackend Backend = FoliageBackend::Auto;
        void Sanitize();
    };
    // Returns near(0), far(1), or culled(2); history retains the selected LOD even when invisible.
    uint32_t ClassifyFoliage(const AABB& bounds, const Frustum& frustum, glm::vec3 camera,
                             const FoliageSettings& settings, uint32_t& history, bool frustumCulling = true);
    struct FoliagePart
    {
        Ref<Mesh> Geometry;
        Material Mat;
    };
    struct FoliageStats
    {
        bool GPU = false, CountsAvailable = false;
        uint32_t Instances = 0, Near = 0, Far = 0, Culled = 0;
        uint64_t Uploads = 0, CPUBytes = 0, GPUBytes = 0, CountFrame = 0;
        std::string Error;
    };
    class FoliageBatch
    {
    public:
        static constexpr uint32_t MaxInstances = 65536;
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
        bool Upload(const std::vector<glm::mat4>& worldTransforms, const AABB& prototypeBounds, int entityID);
        void Cull(const Frustum& frustum, glm::vec3 camera, bool frustumCulling = true);
        bool Draw(const Ref<Mesh>& mesh, uint32_t lod, bool shadow = false);
        const FoliageStats& GetStats() const;
        const std::vector<glm::mat4>& GetTransforms() const;
        // Explicit synchronous diagnostic readback for tests/tools; normal rendering never waits.
        std::array<uint32_t, 2> DebugVisibleCounts();
    private:
        struct Impl;
        Scope<Impl> m_Impl;
    };
}
