#include "Zamin/Renderer/FoliageHLOD.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

namespace Zamin::FoliageHLOD {
    namespace {
        constexpr uint64_t FNVOffset = 1469598103934665603ull, FNVPrime = 1099511628211ull;
        uint64_t Mix(uint64_t hash, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * FNVPrime;
            return hash;
        }
        // Four sloped faces from the base rectangle up to the apex (the base stays open: it sits on the ground).
        void AddClump(const AABB& box, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
        {
            const glm::vec3 center = box.GetCenter();
            const glm::vec3 apex{ center.x, box.Max.y, center.z };
            const float y = box.Min.y;
            const glm::vec3 base[4] = { { box.Min.x, y, box.Min.z }, { box.Max.x, y, box.Min.z }, { box.Max.x, y, box.Max.z }, { box.Min.x, y, box.Max.z } };
            for (int side = 0; side < 4; ++side)
            {
                glm::vec3 a = base[side], b = base[(side + 1) % 4];
                glm::vec3 normal = glm::cross(b - a, apex - a);
                const glm::vec3 faceCenter = (a + b + apex) / 3.0f;
                if (glm::dot(normal, faceCenter - glm::vec3(center.x, faceCenter.y, center.z)) < 0)
                {
                    std::swap(a, b);
                    normal = -normal;
                }
                const float length = glm::length(normal);
                normal = length > 0 ? normal / length : glm::vec3(0, 1, 0);
                const auto first = uint32_t(vertices.size());
                for (const auto& p : { a, b, apex }) vertices.push_back({ p, normal, { 0, 0 } });
                indices.insert(indices.end(), { first, first + 1, first + 2 });
            }
        }
    }
    uint32_t FadeLevel(float distance, float start, float width)
    {
        if (!(distance >= start)) return 0;
        if (!(width > 0)) return FadeLevels;
        const float t = std::clamp((distance - start) / width, 0.0f, 1.0f);
        return uint32_t(std::lround(t * float(FadeLevels)));
    }
    FoliageHLODBake Bake(std::span<const glm::mat4> transforms, const AABB& prototype, float cellSize, const FoliageHLODBake* previous)
    {
        FoliageHLODBake bake;
        if (transforms.empty() || !prototype.IsValid() || !std::isfinite(cellSize) || cellSize <= 0) return bake;
        std::vector<AABB> boxes;
        boxes.reserve(transforms.size());
        glm::vec2 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
        for (const auto& m : transforms)
        {
            const auto box = TransformAABB(prototype, m);
            boxes.push_back(box);
            const glm::vec3 c = box.GetCenter();
            low = glm::min(low, glm::vec2(c.x, c.z));
            high = glm::max(high, glm::vec2(c.x, c.z));
        }
        // A fixed lattice (multiples of the cell size): an edit far from the edges keeps the other cells' coordinates.
        float size = cellSize;
        glm::vec2 origin;
        glm::ivec2 dims;
        for (;;)
        {
            origin = glm::floor(low / size) * size;
            dims = glm::ivec2(glm::floor((high - origin) / size)) + 1;
            if (uint64_t(dims.x) * uint64_t(dims.y) <= MaxCells) break;
            size *= 2;
        }
        bake.CellSize = size;
        bake.Origin = origin;
        bake.Dimensions = dims;
        // Counting sort of the instances by cell (stable: each cell keeps the source order).
        std::vector<uint32_t> gridCell(transforms.size());
        std::vector<uint32_t> counts(size_t(dims.x) * dims.y, 0);
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            const glm::vec3 c = boxes[i].GetCenter();
            const glm::ivec2 cell = glm::clamp(glm::ivec2(glm::floor((glm::vec2(c.x, c.z) - origin) / size)), glm::ivec2(0), dims - 1);
            gridCell[i] = uint32_t(cell.y * dims.x + cell.x);
            ++counts[gridCell[i]];
        }
        std::vector<uint32_t> start(counts.size() + 1, 0), order(transforms.size());
        for (size_t g = 0; g < counts.size(); ++g) start[g + 1] = start[g] + counts[g];
        {
            auto cursor = start;
            for (size_t i = 0; i < gridCell.size(); ++i) order[cursor[gridCell[i]]++] = uint32_t(i);
        }
        // The previous bake's cells by coordinate (only on the same lattice).
        std::map<std::pair<int, int>, const FoliageHLODCell*> reuse;
        if (previous && previous->CellSize == size && previous->Origin == origin)
            for (const auto& cell : previous->Cells) reuse[{ cell.Coord.x, cell.Coord.y }] = &cell;
        std::vector<uint32_t> denseToCell(counts.size(), ~0u);
        bake.InstanceCell.assign(transforms.size(), 0);
        for (uint32_t g = 0; g < counts.size(); ++g)
        {
            if (!counts[g]) continue;
            FoliageHLODCell cell;
            cell.Coord = { int(g % uint32_t(dims.x)), int(g / uint32_t(dims.x)) };
            cell.Instances = counts[g];
            uint64_t hash = Mix(FNVOffset, &size, sizeof(size));
            hash = Mix(hash, &prototype, sizeof(prototype));
            for (uint32_t k = start[g]; k < start[g + 1]; ++k)
            {
                const uint32_t i = order[k];
                hash = Mix(hash, &transforms[i], sizeof(glm::mat4));
                cell.Bounds.Encapsulate(boxes[i]);
            }
            cell.Hash = hash;
            cell.FirstIndex = uint32_t(bake.Indices.size());
            cell.FirstVertex = uint32_t(bake.Vertices.size());
            auto it = reuse.find({ cell.Coord.x, cell.Coord.y });
            if (it != reuse.end() && it->second->Hash == hash && it->second->Instances == cell.Instances)
            {
                const auto& old = *it->second;
                const auto base = uint32_t(bake.Vertices.size());
                bake.Vertices.insert(bake.Vertices.end(), previous->Vertices.begin() + old.FirstVertex, previous->Vertices.begin() + old.FirstVertex + old.VertexCount);
                for (uint32_t k = 0; k < old.IndexCount; ++k) bake.Indices.push_back(previous->Indices[old.FirstIndex + k] - old.FirstVertex + base);
                ++bake.ReusedCells;
            }
            else
            {
                // Clumps: the instances of each sub-square of the cell, as one pyramid over their bounds.
                constexpr uint32_t S = ClumpsPerSide;
                AABB clumps[S * S];
                for (auto& c : clumps) c = AABB::Empty();
                const glm::vec2 cellOrigin = origin + glm::vec2(cell.Coord) * size;
                for (uint32_t k = start[g]; k < start[g + 1]; ++k)
                {
                    const AABB& box = boxes[order[k]];
                    const glm::vec3 c = box.GetCenter();
                    const glm::ivec2 sub = glm::clamp(glm::ivec2(glm::floor((glm::vec2(c.x, c.z) - cellOrigin) / size * float(S))), glm::ivec2(0), glm::ivec2(int(S) - 1));
                    clumps[sub.y * int(S) + sub.x].Encapsulate(box);
                }
                for (const auto& clump : clumps)
                    if (clump.IsValid()) AddClump(clump, bake.Vertices, bake.Indices);
                ++bake.RebakedCells;
            }
            cell.IndexCount = uint32_t(bake.Indices.size()) - cell.FirstIndex;
            cell.VertexCount = uint32_t(bake.Vertices.size()) - cell.FirstVertex;
            denseToCell[g] = uint32_t(bake.Cells.size());
            bake.Cells.push_back(cell);
        }
        for (size_t i = 0; i < gridCell.size(); ++i) bake.InstanceCell[i] = denseToCell[gridCell[i]];
        uint64_t hash = Mix(FNVOffset, &bake.CellSize, sizeof(float));
        hash = Mix(hash, &bake.Origin, sizeof(bake.Origin));
        hash = Mix(hash, &bake.Dimensions, sizeof(bake.Dimensions));
        for (const auto& cell : bake.Cells) hash = Mix(hash, &cell.Hash, sizeof(cell.Hash));
        if (!bake.Vertices.empty()) hash = Mix(hash, bake.Vertices.data(), bake.Vertices.size() * sizeof(MeshVertex));
        if (!bake.Indices.empty()) hash = Mix(hash, bake.Indices.data(), bake.Indices.size() * sizeof(uint32_t));
        bake.Hash = hash;
        return bake;
    }
}
