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
        float LODDistanceScale = 2.0f;
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
