#include "TestAssertions.h"
#include <Zamin/Scene/Scene.h>
#include <Zamin/Scene/Components.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

using namespace Zamin;

// ------------------------------------------------------------------ large 2D tilemap hierarchical visibility
namespace {
    std::vector<TilemapChunk> Reference(const TilemapComponent& t, const glm::mat4& world, const Frustum& frustum)
    {
        std::vector<TilemapChunk> result;
        for (int y = 0; y < t.Height; y += TilemapChunkSize)
            for (int x = 0; x < t.Width; x += TilemapChunkSize)
            {
                TilemapChunk chunk{ x, y, std::min(x + TilemapChunkSize, t.Width), std::min(y + TilemapChunkSize, t.Height) };
                glm::vec3 a(x * t.TileSize, y * t.TileSize, 0), b(chunk.X1 * t.TileSize, chunk.Y1 * t.TileSize, 0);
                if (frustum.Intersects(TransformAABB({ glm::min(a, b), glm::max(a, b) }, world))) result.push_back(chunk);
            }
        return result;
    }
}

TEST(TilemapHierarchyMatchesConservativeReference)
{
    TilemapComponent t;
    t.Width = 513;
    t.Height = 259;
    for (float size : { 1.0f, -0.75f })
        for (float angle : { 0.0f, 0.37f, 1.2f })
            for (bool perspective : { false, true })
            {
                t.TileSize = size;
                auto world = glm::rotate(glm::mat4(1), angle, glm::vec3(0, 0, 1)) * glm::translate(glm::mat4(1), glm::vec3(-120, -75, 0));
                auto projection = perspective ? glm::perspective(glm::radians(55.0f), 1.7f, 0.1f, 1000.0f)
                                              : glm::ortho(-40.0f, 40.0f, -30.0f, 30.0f, 0.1f, 1000.0f);
                auto frustum = Frustum::FromViewProjection(projection * glm::lookAt(glm::vec3(5, -8, 100), glm::vec3(0), glm::vec3(0, 1, 0)));
                uint32_t culled = 0;
                auto actual = GetVisibleTilemapChunks(t, world, &frustum, &culled), expected = Reference(t, world, frustum);
                CHECK(actual.size() == expected.size());
                if (actual.size() != expected.size()) continue;
                for (size_t i = 0; i < actual.size(); ++i)
                    CHECK(actual[i].X0 == expected[i].X0 && actual[i].Y0 == expected[i].Y0 && actual[i].X1 == expected[i].X1 && actual[i].Y1 == expected[i].Y1);
                CHECK(actual.size() + culled == size_t((t.Width + 15) / 16) * size_t((t.Height + 15) / 16));
            }
}

TEST(TilemapLargeMapPrunesWithoutFullScan)
{
    TilemapComponent t;
    t.Width = 4096;
    t.Height = 4096;
    t.TileSize = 1;
    auto world = glm::translate(glm::mat4(1), glm::vec3(-2048, -2048, 0));
    auto frustum = Frustum::FromViewProjection(glm::ortho(-12.0f, 12.0f, -12.0f, 12.0f, -1.0f, 1.0f));
    uint32_t tested = 0, culled = 0;
    auto visible = GetVisibleTilemapChunksWithStats(t, world, &frustum, &culled, &tested);
    std::printf("    Tilemap visibility workload: total=65536 tested=%u visible=%zu\n", tested, visible.size());
    CHECK(visible.size() == 4);
    CHECK(visible.size() + culled == 65536);
    CHECK(tested < 512);
    tested = 0;
    culled = 0;
    world = glm::translate(glm::mat4(1), glm::vec3(10000, 10000, 0));
    CHECK(GetVisibleTilemapChunksWithStats(t, world, &frustum, &culled, &tested).empty());
    CHECK(tested == 1 && culled == 65536);
}

TEST(TilemapFullViewAndNoFrustumKeepRowOrder)
{
    TilemapComponent t;
    t.Width = 257;
    t.Height = 193;
    t.TileSize = 1;
    uint32_t tested = 0, culled = 0;
    auto frustum = Frustum::FromViewProjection(glm::ortho(-1000.0f, 1000.0f, -1000.0f, 1000.0f, -1.0f, 1.0f));
    auto visible = GetVisibleTilemapChunksWithStats(t, glm::mat4(1), &frustum, &culled, &tested);
    auto all = GetVisibleTilemapChunks(t, glm::mat4(1), nullptr);
    CHECK(visible.size() == all.size() && tested == 1 && culled == 0);
    for (size_t i = 0; i < visible.size(); ++i) CHECK(visible[i].X0 == all[i].X0 && visible[i].Y0 == all[i].Y0);
}
