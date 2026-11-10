#include "TestAssertions.h"
#include <Zamin/Scene/TerrainLOD.h>
#include <Zamin/Physics/CollisionShapes3D.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdio>
#include <map>
#include <limits>

using namespace Zamin;

// ------------------------------------------------------------------ terrain patch geometry and distance LOD
TEST(TerrainPatchCoverageAndWinding)
{
    for (uint32_t resolution : { 2u, 33u, 34u, 65u, 257u })
    {
        TerrainComponent terrain;
        terrain.Reset(resolution, { 256, 192 }, 3);
        auto patches = TerrainTools::BuildPatchLayout(terrain);
        uint64_t cells = 0, triangles = 0;
        for (const auto& patch : patches)
        {
            cells += patch.CellsX * patch.CellsZ;
            std::vector<MeshVertex> vertices;
            std::vector<uint32_t> indices;
            TerrainTools::BuildPatchMesh(terrain, patch, vertices, indices);
            triangles += indices.size() / 3;
            for (size_t i = 0; i < indices.size(); i += 3)
            {
                CHECK(indices[i] < vertices.size() && indices[i + 1] < vertices.size() && indices[i + 2] < vertices.size());
                const auto a = vertices[indices[i]].Position, b = vertices[indices[i + 1]].Position, c = vertices[indices[i + 2]].Position;
                CHECK(glm::cross(b - a, c - a).y > 0);
            }
        }
        CHECK(cells == uint64_t(resolution - 1) * (resolution - 1));
        CHECK(triangles == cells * 2);
    }
}

TEST(TerrainDistanceLODAndTransformInvariance)
{
    TerrainComponent terrain;
    terrain.Reset(257, { 256, 256 }, 3);
    TerrainTools::Generate(terrain, 42, 24, 0.02f);
    auto near = TerrainTools::BuildPatchLayout(terrain), far = near, transformed = near;
    const glm::vec3 camera(128, 30, 128);
    TerrainTools::SelectPatchLODs(near, terrain, glm::mat4(1), camera);
    TerrainTools::SelectPatchLODs(far, terrain, glm::mat4(1), { 128, 4096, 128 });
    CHECK(std::any_of(near.begin(), near.end(), [](const auto& p) { return p.LOD == 0; }));
    CHECK(std::all_of(far.begin(), far.end(), [](const auto& p) { return p.LOD == TerrainMaxLOD; }));
    uint32_t triangles = 0;
    for (const auto& patch : far)
    {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        TerrainTools::BuildPatchMesh(terrain, patch, vertices, indices);
        triangles += uint32_t(indices.size()) / 3;
    }
    std::printf("    Terrain geometry workload: full=%u far=%u patches=%zu\n", (terrain.Resolution - 1) * (terrain.Resolution - 1) * 2, triangles, far.size());
    CHECK(triangles == 128); // 64 independently selectable patches, two triangles each.
    CHECK(triangles * 100 < 256 * 256 * 2);
    auto world = glm::translate(glm::mat4(1), { 500, -100, 700 }) * glm::rotate(glm::mat4(1), 0.57f, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(10));
    TerrainTools::SelectPatchLODs(transformed, terrain, world, glm::vec3(world * glm::vec4(camera, 1)));
    for (size_t i = 0; i < near.size(); ++i)
    {
        CHECK(near[i].LOD == transformed[i].LOD);
        for (uint8_t neighbor : near[i].Neighbors) CHECK(std::abs(int(near[i].LOD) - int(neighbor)) <= 1);
    }
    terrain.LODEnabled = false;
    TerrainTools::SelectPatchLODs(far, terrain, glm::mat4(1), { 128, 4096, 128 });
    CHECK(std::all_of(far.begin(), far.end(), [](const auto& p) { return p.LOD == 0; }));
}

TEST(TerrainMixedLODEdgesRemainClosed)
{
    TerrainComponent terrain;
    terrain.Reset(65, { 64, 64 }, 0);
    TerrainTools::Generate(terrain, 73, 28, 0.15f);
    auto layout = TerrainTools::BuildPatchLayout(terrain);
    // Test both axes and both which-side-is-fine cases.
    for (int axis = 0; axis < 2; ++axis)
        for (int reverse = 0; reverse < 2; ++reverse)
        {
            auto a = layout[0], b = layout[axis == 0 ? 1 : 2];
            a.LOD = reverse ? 1 : 0;
            b.LOD = reverse ? 0 : 1;
            a.Neighbors.fill(a.LOD);
            b.Neighbors.fill(b.LOD);
            a.Neighbors[axis == 0 ? 1 : 3] = b.LOD;
            b.Neighbors[axis == 0 ? 0 : 2] = a.LOD;
            std::vector<MeshVertex> av, bv;
            std::vector<uint32_t> ai, bi;
            TerrainTools::BuildPatchMesh(terrain, a, av, ai);
            TerrainTools::BuildPatchMesh(terrain, b, bv, bi);
            const auto& fine = reverse ? bv : av;
            const auto& coarse = reverse ? av : bv;
            std::vector<glm::vec3> edge;
            for (const auto& v : coarse)
                if (std::abs(v.Position[axis == 0 ? 0 : 2] - 32) < 1e-6f) edge.push_back(v.Position);
            const int coordinate = axis == 0 ? 2 : 0;
            std::sort(edge.begin(), edge.end(), [&](auto x, auto y) { return x[coordinate] < y[coordinate]; });
            CHECK(edge.size() == 17);
            const auto& fineIndices = reverse ? bi : ai;
            for (uint32_t index : fineIndices)
            {
                const auto& v = fine[index];
                if (std::abs(v.Position[axis == 0 ? 0 : 2] - 32) > 1e-6f) continue;
                const float sample = v.Position[coordinate];
                size_t segment = std::min(size_t(sample / 2), edge.size() - 2);
                float blend = (sample - edge[segment][coordinate]) / (edge[segment + 1][coordinate] - edge[segment][coordinate]);
                CHECK_NEAR(v.Position.y, glm::mix(edge[segment].y, edge[segment + 1].y, blend), 1e-5f);
                CHECK_NEAR(v.TexCoord.x, v.Position.x / terrain.Size.x, 1e-6f);
                CHECK_NEAR(v.TexCoord.y, v.Position.z / terrain.Size.y, 1e-6f);
            }
        }
}

TEST(TerrainLODHysteresisAndOrthographicQuality)
{
    TerrainComponent terrain;
    terrain.Reset(33, { 32, 32 }, 0);
    auto patches = TerrainTools::BuildPatchLayout(terrain);
    const float radius = std::sqrt(32.0f * 32.0f * 2) * 0.5f * std::sqrt(3.0f);
    std::array<uint8_t, 1> previous{ 0 };
    TerrainTools::SelectPatchLODs(patches, terrain, glm::mat4(1), { 16, radius + 128 * 1.05f, 16 }, previous);
    CHECK(patches[0].LOD == 0);
    previous[0] = 1;
    TerrainTools::SelectPatchLODs(patches, terrain, glm::mat4(1), { 16, radius + 128 * 0.95f, 16 }, previous);
    CHECK(patches[0].LOD == 1);
    TerrainTools::SelectPatchLODs(patches, terrain, glm::mat4(1), { 16, radius + 128 * 0.5f, 16 }, previous);
    CHECK(patches[0].LOD == 0);
    TerrainTools::SelectPatchLODs(patches, terrain, glm::mat4(1), { 16, 100000, 16 }, {}, true);
    CHECK(patches[0].LOD == 0);
    auto invalid = glm::mat4(1);
    invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
    TerrainTools::SelectPatchLODs(patches, terrain, invalid, { 16, 100000, 16 });
    CHECK(patches[0].LOD == 0);
}

TEST(TerrainBoundaryNormalsAndInvalidPatches)
{
    TerrainComponent terrain;
    terrain.Reset(33, { 32, 32 }, 0);
    for (uint32_t z = 0; z < 33; ++z)
        for (uint32_t x = 0; x < 33; ++x) terrain.Heights[z * 33 + x] = float(x);
    const auto expected = glm::normalize(glm::vec3(-1, 1, 0));
    for (float x : { 0.0f, 16.0f, 32.0f }) CHECK(glm::length(TerrainTools::SampleNormal(terrain, x, 16) - expected) < 1e-5f);
    auto p = TerrainTools::BuildPatchLayout(terrain).front();
    p.X = std::numeric_limits<uint32_t>::max();
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    TerrainTools::BuildPatchMesh(terrain, p, vertices, indices);
    CHECK(vertices.empty() && indices.empty());
    terrain.Heights[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(TerrainTools::BuildPatchLayout(terrain).empty());
}

TEST(TerrainCollisionRemainsFullResolutionAcrossLOD)
{
    TerrainComponent terrain;
    terrain.Reset(65, { 64, 64 }, 3);
    const auto collision = TerrainTools::GetCollisionMesh(terrain);
    CHECK(collision && collision->Positions.size() == 65 * 65);
    CHECK(collision->Indices.size() == 64 * 64 * 6);
    auto patches = TerrainTools::BuildPatchLayout(terrain);
    TerrainTools::SelectPatchLODs(patches, terrain, glm::mat4(1), { 32, 4096, 32 });
    CHECK(std::all_of(patches.begin(), patches.end(), [](const auto& p) { return p.LOD == TerrainMaxLOD; }));
    CHECK(TerrainTools::GetCollisionMesh(terrain) == collision);
    terrain.LODEnabled = false;
    terrain.LODDistanceScale = 8;
    CHECK(TerrainTools::GetCollisionMesh(terrain) == collision);
    TerrainHit hit;
    CHECK(TerrainTools::Raycast(terrain, { 10, 20, 10 }, { 0, -1, 0 }, hit));
    CHECK_NEAR(hit.Position.y, 3, 1e-6f);
}

TEST(TerrainStitchedCornersPreserveAreaAndTopology)
{
    TerrainComponent terrain;
    terrain.Reset(65, { 64, 64 }, 0);
    TerrainTools::Generate(terrain, 11, 30, 0.08f);
    for (uint8_t lod : { uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4) })
        for (int mask = 0; mask < 16; ++mask)
        {
            auto patch = TerrainTools::BuildPatchLayout(terrain).front();
            patch.LOD = lod;
            for (int edge = 0; edge < 4; ++edge) patch.Neighbors[edge] = (mask & (1 << edge)) ? lod + 1 : lod;
            std::vector<MeshVertex> vertices;
            std::vector<uint32_t> indices;
            TerrainTools::BuildPatchMesh(terrain, patch, vertices, indices);
            std::map<std::pair<uint32_t, uint32_t>, int> edges;
            std::vector<bool> used(vertices.size(), false);
            double area = 0;
            for (size_t i = 0; i < indices.size(); i += 3)
            {
                const auto a = vertices[indices[i]].Position, b = vertices[indices[i + 1]].Position, c = vertices[indices[i + 2]].Position;
                const float projected = glm::cross(b - a, c - a).y;
                CHECK(projected > 0);
                area += projected * 0.5;
                for (int e = 0; e < 3; ++e)
                {
                    auto from = indices[i + e], to = indices[i + (e + 1) % 3];
                    used[from] = true;
                    if (from > to) std::swap(from, to);
                    ++edges[{ from, to }];
                }
            }
            CHECK_NEAR(area, 1024.0, 1e-5);
            for (const auto& [edge, count] : edges)
            {
                CHECK(count == 1 || count == 2);
                if (count == 1)
                {
                    const auto a = vertices[edge.first].Position, b = vertices[edge.second].Position;
                    CHECK((a.x == 0 && b.x == 0) || (a.x == 32 && b.x == 32) || (a.z == 0 && b.z == 0) || (a.z == 32 && b.z == 32));
                }
            }
            for (size_t index = 0; index < vertices.size(); ++index)
            {
                if (!used[index]) continue;
                const auto p = vertices[index].Position;
                const int coarseStep = 1 << (lod + 1);
                if ((mask & 1) && p.x == 0) CHECK(int(p.z) % coarseStep == 0);
                if ((mask & 2) && p.x == 32) CHECK(int(p.z) % coarseStep == 0);
                if ((mask & 4) && p.z == 0) CHECK(int(p.x) % coarseStep == 0);
                if ((mask & 8) && p.z == 32) CHECK(int(p.x) % coarseStep == 0);
            }
        }
}

TEST(TerrainPartialPatchesBalanceAndRemainManifold)
{
    TerrainComponent terrain;
    terrain.Reset(34, { 33, 33 }, 0);
    TerrainTools::Generate(terrain, 91, 8, 0.12f);
    auto patches = TerrainTools::BuildPatchLayout(terrain);
    TerrainTools::SelectPatchLODs(patches, terrain, glm::mat4(1), { 16, 4096, 16 });
    CHECK(patches.size() == 4);
    CHECK(patches[0].LOD == 1); // The one-cell-wide neighbors constrain the otherwise coarse patch.
    for (const auto& patch : patches)
    {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        TerrainTools::BuildPatchMesh(terrain, patch, vertices, indices);
        std::map<std::pair<uint32_t, uint32_t>, int> edges;
        double area = 0;
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const auto a = vertices[indices[i]].Position, b = vertices[indices[i + 1]].Position, c = vertices[indices[i + 2]].Position;
            const float projected = glm::cross(b - a, c - a).y;
            CHECK(projected > 0);
            area += projected * 0.5;
            for (int e = 0; e < 3; ++e)
            {
                auto from = indices[i + e], to = indices[i + (e + 1) % 3];
                if (from > to) std::swap(from, to);
                ++edges[{ from, to }];
            }
        }
        CHECK_NEAR(area, double(patch.CellsX) * patch.CellsZ, 0.001);
        for (const auto& [edge, count] : edges)
        {
            CHECK(count == 1 || count == 2);
            if (count != 1) continue;
            const auto a = vertices[edge.first].Position, b = vertices[edge.second].Position;
            CHECK((a.x == patch.BoundsMin.x && b.x == patch.BoundsMin.x) || (a.x == patch.BoundsMax.x && b.x == patch.BoundsMax.x) ||
                  (a.z == patch.BoundsMin.z && b.z == patch.BoundsMin.z) || (a.z == patch.BoundsMax.z && b.z == patch.BoundsMax.z));
        }
    }
}
