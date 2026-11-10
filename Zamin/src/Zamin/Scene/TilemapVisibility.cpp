#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Components.h"
#include <algorithm>
#include <limits>

namespace Zamin {
    std::vector<TilemapChunk> GetVisibleTilemapChunks(const TilemapComponent& tilemap, const glm::mat4& world,
                                                      const Frustum* frustum, uint32_t* culled)
    {
        return GetVisibleTilemapChunksWithStats(tilemap, world, frustum, culled, nullptr);
    }

    std::vector<TilemapChunk> GetVisibleTilemapChunksWithStats(const TilemapComponent& tilemap, const glm::mat4& world,
                                                               const Frustum* frustum, uint32_t* culled, uint32_t* boundsTested)
    {
        std::vector<TilemapChunk> visible;
        const int width = std::max(1, tilemap.Width), height = std::max(1, tilemap.Height);
        const int columns = 1 + (width - 1) / TilemapChunkSize, rows = 1 + (height - 1) / TilemapChunkSize;
        auto count = [](uint32_t* value, uint64_t add) {
            if (value) *value = uint32_t(std::min<uint64_t>(uint64_t(*value) + add, std::numeric_limits<uint32_t>::max()));
        };
        auto chunk = [&](int x0, int y0, int x1, int y1) {
            return TilemapChunk{ x0 * TilemapChunkSize, y0 * TilemapChunkSize,
                                 int(std::min<int64_t>(int64_t(x1) * TilemapChunkSize, width)),
                                 int(std::min<int64_t>(int64_t(y1) * TilemapChunkSize, height)) };
        };
        auto bounds = [&](const TilemapChunk& c) {
            glm::vec3 a(c.X0 * tilemap.TileSize, c.Y0 * tilemap.TileSize, 0);
            glm::vec3 b(c.X1 * tilemap.TileSize, c.Y1 * tilemap.TileSize, 0);
            return TransformAABB(AABB{ glm::min(a, b), glm::max(a, b) }, world);
        };
        auto append = [&](int x0, int y0, int x1, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) visible.push_back(chunk(x, y, x + 1, y + 1));
        };
        if (!frustum)
        {
            append(0, 0, columns, rows);
            return visible;
        }
        // Small maps keep the direct path. Large maps reject whole rectangular subtrees.
        if (uint64_t(columns) * uint64_t(rows) <= 64)
        {
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < columns; ++x)
                {
                    auto c = chunk(x, y, x + 1, y + 1);
                    count(boundsTested, 1);
                    if (frustum->Intersects(bounds(c))) visible.push_back(c);
                    else count(culled, 1);
                }
            return visible;
        }
        auto contains = [&](const AABB& box) {
            for (const auto& plane : frustum->Planes)
            {
                glm::vec3 nearest(plane.x >= 0 ? box.Min.x : box.Max.x,
                                  plane.y >= 0 ? box.Min.y : box.Max.y, plane.z >= 0 ? box.Min.z : box.Max.z);
                if (glm::dot(glm::vec3(plane), nearest) + plane.w < 0) return false;
            }
            return true;
        };
        bool alreadyOrdered = false;
        auto visit = [&](auto&& self, int x0, int y0, int x1, int y1) -> void {
            const auto box = bounds(chunk(x0, y0, x1, y1));
            count(boundsTested, 1);
            if (!frustum->Intersects(box))
            {
                count(culled, uint64_t(x1 - x0) * uint64_t(y1 - y0));
                return;
            }
            if ((x1 - x0 == 1 && y1 - y0 == 1) || contains(box))
            {
                append(x0, y0, x1, y1);
                if (x0 == 0 && y0 == 0 && x1 == columns && y1 == rows) alreadyOrdered = true;
                return;
            }
            if (x1 - x0 >= y1 - y0)
            {
                int middle = x0 + (x1 - x0) / 2;
                self(self, x0, y0, middle, y1);
                self(self, middle, y0, x1, y1);
            }
            else
            {
                int middle = y0 + (y1 - y0) / 2;
                self(self, x0, y0, x1, middle);
                self(self, x0, middle, x1, y1);
            }
        };
        visit(visit, 0, 0, columns, rows);
        // Rendering order must match the old row-major path, including translucent tiles.
        if (alreadyOrdered) return visible;
        std::sort(visible.begin(), visible.end(), [](const auto& a, const auto& b) {
            return a.Y0 != b.Y0 ? a.Y0 < b.Y0 : a.X0 < b.X0;
        });
        return visible;
    }
}
