#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace Zamin {
    class Scene;
    class Entity;
    struct TerrainReprojectOptions
    {
        bool AlignToNormal = false;
        float HeightOffset = 0;
        glm::vec2 Center{ 0 };
        float Radius = -1; // negative: whole terrain; otherwise a local X/Z brush region
        uint32_t MaxObjects = 5000;
    };
    struct TerrainReprojectResult
    {
        uint32_t Visited = 0, Changed = 0, Skipped = 0;
        std::string Error;
    };
    class TerrainFoliage
    {
    public:
        // Edit-time only. Direct children tagged TerrainPainted; hand-placed objects stay put.
        // Preserves X/Z, scale and prefab hierarchy; height offset is local to the terrain.
        static TerrainReprojectResult Reproject(Scene& scene, Entity terrain, const TerrainReprojectOptions& options = {});
    };
}
