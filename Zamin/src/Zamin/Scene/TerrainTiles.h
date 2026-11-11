#pragma once
#include "Zamin/Scene/Terrain.h"
namespace Zamin::TerrainTools {
    struct WorldTerrainSettings
    {
        uint32_t Seed = 42;
        float Base = 8, Amplitude = 6, Frequency = .02f;
    };
    // Deterministic absolute X/Z sampling; tile-local seed offsets never create boundary jumps.
    float WorldHeight(glm::dvec2 position, const WorldTerrainSettings& settings);
    bool GenerateWorldTile(TerrainComponent& terrain, glm::dvec3 origin, const WorldTerrainSettings& settings, std::string& error);
    // Translation-only, full-edge adjacent tiles. Fine samples conform to the common coarse edge.
    // No mutations on failure. C0 geometry continuity; shading normals remain local to each tile.
    bool StitchTiles(TerrainComponent& a, glm::dvec3 originA, TerrainComponent& b, glm::dvec3 originB,
                     uint32_t blendRows, std::string& error);
}
