#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Texture.h"
#include <array>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Zamin {
    struct CollisionMesh3D;
    struct TerrainSurface;
    struct TerrainLayer
    {
        glm::vec3 Color{ 0.25f, 0.46f, 0.17f };
        std::string TexturePath;
        float Tiling = 0.25f; // repetitions per local metre
        std::string NormalMapPath, MaskMapPath; // mask: R occlusion, G roughness, B metallic
        float Roughness = 0.95f, Metallic = 0.0f, Occlusion = 1.0f, NormalStrength = 1.0f;
    };
    // A bounded, serializable heightfield. GPU/physics caches belong to this instance only.
    struct TerrainComponent
    {
        static constexpr uint32_t MaxResolution = 257;
        uint32_t Resolution = 65;
        glm::vec2 Size{ 64.0f };
        float HeightLimit = 128.0f;
        bool Collision = true;
        bool LODEnabled = true;
        bool LockBorderLOD = false; // adjacent streamed tiles retain exact authored boundary samples
        float LODDistanceScale = 2.0f;
        // Runtime only (tiled terrain streaming, not saved): how many LOD levels coarser the neighbouring tile is on
        // each edge (-X, +X, -Z, +Z); that edge's chain follows the neighbour's samples, so tiles never crack.
        std::array<uint8_t, 4> EdgeLOD{};
        std::vector<float> Heights;
        std::vector<glm::vec4> Weights;
        std::array<TerrainLayer, 4> Layers{ { { { 0.25f, 0.46f, 0.17f }, "", 0.25f }, { { 0.40f, 0.27f, 0.15f }, "", 0.25f }, { { 0.46f, 0.47f, 0.49f }, "", 0.25f }, { { 0.78f, 0.68f, 0.43f }, "", 0.25f } } };
        uint64_t GeometryRevision = 1, PaintRevision = 1;
        struct Cache;
        mutable Ref<Cache> Runtime;
        TerrainComponent() = default;
        TerrainComponent(const TerrainComponent& other);
        TerrainComponent& operator=(const TerrainComponent& other);
        void EnsureSize();
        void Reset(uint32_t resolution, glm::vec2 size, float height = 0.0f);
        void GeometryChanged() { ++GeometryRevision; }
        void PaintChanged() { ++PaintRevision; }
    };
    // A large terrain split into tiles on disk (a .zterrain file and its tile folder), streamed around the camera:
    // tiles within LoadRadius are read on a background thread, far ones at a coarser LOD (fewer samples, less
    // memory), all within MemoryBudgetMB; edges between tiles of different LOD are stitched (no cracks). Each
    // resident tile is an ordinary terrain (GPU layers, patch LOD, picking) with its own foliage; tiles near the
    // camera at full detail get static collision while the game runs. See docs/Terrain.md (Tiled terrain).
    struct TiledTerrainComponent
    {
        std::string Path;              // asset-relative .zterrain
        float LoadRadius = 320.0f;     // m from the camera to a tile's rectangle: loaded within
        float UnloadRadius = 400.0f;   // ... unloaded beyond (at least LoadRadius)
        float LODDistance = 96.0f;     // full detail nearer; every doubling of the distance one LOD level coarser
        uint32_t MaxLOD = 3;           // the coarsest level used (the tile set may store fewer)
        uint32_t MemoryBudgetMB = 96;  // resident tiles' CPU + estimated GPU bytes
        uint32_t MaxLoadsPerFrame = 4; // tile reads started / tiles made resident per frame
        bool Collision = true;         // static colliders for full-detail tiles within CollisionRadius (play)
        float CollisionRadius = 128.0f;
        float FoliageDistance = 200.0f; // tiles nearer than this show their foliage
        bool ShowTileLOD = false;      // debug: tiles coloured by LOD with their borders drawn
        struct Runtime;
        mutable Ref<Runtime> Streamer;
        TiledTerrainComponent() = default;
        TiledTerrainComponent(const TiledTerrainComponent& other);
        TiledTerrainComponent& operator=(const TiledTerrainComponent& other);
        void Sanitize();
    };
    enum class TerrainBrush
    {
        Raise,
        Lower,
        Flatten,
        Smooth,
        Paint
    };
    struct TerrainBrushSettings
    {
        TerrainBrush Tool = TerrainBrush::Raise;
        float Radius = 4.0f, Strength = 5.0f, Hardness = 0.3f, TargetHeight = 0.0f;
        int Layer = 0;
    };
    struct TerrainHit
    {
        glm::vec3 Position{}, Normal{ 0, 1, 0 };
        float Distance = 0;
    };
    namespace TerrainTools {
        // Local coordinates: X/Z in [0, Size.x/y]. Outside samples are clamped to the edge.
        float SampleHeight(const TerrainComponent& terrain, float x, float z);
        glm::vec3 SampleNormal(const TerrainComponent& terrain, float x, float z);
        bool Raycast(const TerrainComponent& terrain, const glm::vec3& origin, const glm::vec3& direction,
                     TerrainHit& hit, float maxDistance = 100000.0f);
        bool ApplyBrush(TerrainComponent& terrain, glm::vec2 center, const TerrainBrushSettings& brush, float seconds);
        void Generate(TerrainComponent& terrain, uint32_t seed, float amplitude, float frequency);
        void BuildMesh(const TerrainComponent& terrain, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);
        Ref<const CollisionMesh3D> GetCollisionMesh(const TerrainComponent& terrain);
        Ref<Mesh> GetRenderMesh(const TerrainComponent& terrain);
        Ref<Texture2D> GetAlbedo(const TerrainComponent& terrain); // legacy baked preview, not the runtime material
        Ref<TerrainSurface> GetSurface(const TerrainComponent& terrain);
        // R16: square, little-endian unsigned 16-bit, [0, HeightLimit]. PNG import accepts 8/16-bit.
        bool ImportHeightmap(TerrainComponent& terrain, const std::filesystem::path& path, std::string& error);
        bool ExportHeightmap(const TerrainComponent& terrain, const std::filesystem::path& path, std::string& error);
    }
}
