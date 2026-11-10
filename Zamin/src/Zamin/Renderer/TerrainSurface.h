#pragma once
#include "Zamin/Scene/Terrain.h"

namespace Zamin {
    // Cached terrain GPU inputs. Sources use the shared texture library/import/hot reload path.
    struct TerrainSurface
    {
        std::array<TerrainLayer, 4> Layers;
        std::array<Ref<Texture2D>, 4> Albedo, Normal, Mask;
        Ref<Texture2D> Weights;
        glm::vec2 LocalSize{ 1 }, WeightUV{ 1, 0 };
        uint64_t WeightUploads = 0;
        uint32_t ControlBytes = 0, MissingMaps = 0;
    };
}
