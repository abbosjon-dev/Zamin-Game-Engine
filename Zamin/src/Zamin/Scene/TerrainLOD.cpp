#include "Zamin/Scene/TerrainLOD.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin::TerrainTools {
    namespace {
        bool Valid(const TerrainComponent& t)
        {
            return t.Resolution >= 2 && t.Resolution <= TerrainComponent::MaxResolution &&
                   t.Heights.size() == size_t(t.Resolution) * t.Resolution &&
                   std::isfinite(t.Size.x) && std::isfinite(t.Size.y) && t.Size.x > 0 && t.Size.y > 0;
        }
        uint8_t MaxLOD(const TerrainPatch& p)
        {
            uint8_t level = 0;
            uint32_t cells = std::min(p.CellsX, p.CellsZ);
            while (cells >= 2 && level < TerrainMaxLOD)
            {
                cells /= 2;
                ++level;
            }
            return level;
        }
        glm::vec3 Point(const TerrainComponent& t, uint32_t x, uint32_t z)
        {
            return { float(x) / float(t.Resolution - 1) * t.Size.x,
                     t.Heights[size_t(z) * t.Resolution + x],
                     float(z) / float(t.Resolution - 1) * t.Size.y };
        }
        bool Finite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    std::vector<TerrainPatch> BuildPatchLayout(const TerrainComponent& t)
    {
        std::vector<TerrainPatch> result;
        if (!Valid(t)) return result;
        const uint32_t cells = t.Resolution - 1;
        for (uint32_t z = 0; z < cells; z += TerrainPatchCells)
            for (uint32_t x = 0; x < cells; x += TerrainPatchCells)
            {
                TerrainPatch p;
                p.X = x;
                p.Z = z;
                p.CellsX = std::min(TerrainPatchCells, cells - x);
                p.CellsZ = std::min(TerrainPatchCells, cells - z);
                p.BoundsMin = Point(t, x, z);
                p.BoundsMax = Point(t, x + p.CellsX, z + p.CellsZ);
                float low = std::numeric_limits<float>::max(), high = -low;
                for (uint32_t zz = z; zz <= z + p.CellsZ; ++zz)
                    for (uint32_t xx = x; xx <= x + p.CellsX; ++xx)
                    {
                        const float h = t.Heights[size_t(zz) * t.Resolution + xx];
                        if (!std::isfinite(h)) return {};
                        low = std::min(low, h);
                        high = std::max(high, h);
                    }
                p.BoundsMin.y = low;
                p.BoundsMax.y = high;
                result.push_back(p);
            }
        return result;
    }

    void SelectPatchLODs(std::vector<TerrainPatch>& patches, const TerrainComponent& t,
                         const glm::mat4& world, const glm::vec3& camera, std::span<const uint8_t> previous, bool fullDetail)
    {
        if (!Valid(t))
        {
            patches.clear();
            return;
        }
        const uint32_t axis = (t.Resolution - 2) / TerrainPatchCells + 1;
        if (patches.size() != size_t(axis) * axis) { patches = BuildPatchLayout(t); }
        if (patches.empty()) return;
        // A Frobenius norm bounds arbitrary affine transforms, including nonuniform scale/shear.
        const float scale = std::sqrt(glm::dot(glm::vec3(world[0]), glm::vec3(world[0])) +
                                      glm::dot(glm::vec3(world[1]), glm::vec3(world[1])) + glm::dot(glm::vec3(world[2]), glm::vec3(world[2])));
        bool finite = Finite(camera) && std::isfinite(scale) && scale > 1e-8f;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row) finite &= std::isfinite(world[column][row]);
        const float distanceScale = std::isfinite(t.LODDistanceScale) ? std::clamp(t.LODDistanceScale, 0.25f, 8.0f) : 2.0f;
        for (size_t i = 0; i < patches.size(); ++i)
        {
            auto& p = patches[i];
            p.Neighbors.fill(0);
            if (!t.LODEnabled || fullDetail || !finite)
            {
                p.LOD = 0;
                continue;
            }
            const auto center = glm::vec3(world * glm::vec4((p.BoundsMin + p.BoundsMax) * 0.5f, 1));
            const float radius = glm::length(p.BoundsMax - p.BoundsMin) * 0.5f * scale;
            const float distance = std::max(0.0f, glm::length(camera - center) - radius);
            const float width = std::max(glm::length(glm::vec3(world[0])) * (p.BoundsMax.x - p.BoundsMin.x),
                                         glm::length(glm::vec3(world[2])) * (p.BoundsMax.z - p.BoundsMin.z));
            const float reference = std::max(width * distanceScale, 0.01f);
            const uint8_t limit = MaxLOD(p);
            uint8_t level = 0;
            if (previous.size() == patches.size())
            {
                level = std::min(previous[i], limit);
                // A 15% dead band avoids toggling geometry when a camera jitters at a threshold.
                while (level < limit && distance > std::ldexp(reference, level + 1) * 1.15f) ++level;
                while (level > 0 && distance < std::ldexp(reference, level) * 0.85f) --level;
            }
            else
                while (level < limit && distance >= std::ldexp(reference, level + 1)) ++level;
            p.LOD = level;
        }
        bool changed;
        do
        {
            changed = false;
            for (uint32_t z = 0; z < axis; ++z)
                for (uint32_t x = 0; x < axis; ++x)
                {
                    auto relax = [&](size_t a, size_t b) {
                        if (patches[a].LOD > patches[b].LOD + 1)
                        {
                            patches[a].LOD = patches[b].LOD + 1;
                            changed = true;
                        }
                        if (patches[b].LOD > patches[a].LOD + 1)
                        {
                            patches[b].LOD = patches[a].LOD + 1;
                            changed = true;
                        }
                    };
                    size_t i = size_t(z) * axis + x;
                    if (x + 1 < axis) relax(i, i + 1);
                    if (z + 1 < axis) relax(i, i + axis);
                }
        } while (changed);
        for (uint32_t z = 0; z < axis; ++z)
            for (uint32_t x = 0; x < axis; ++x)
            {
                size_t i = size_t(z) * axis + x;
                auto& p = patches[i];
                p.Neighbors = { x ? patches[i - 1].LOD : p.LOD, x + 1 < axis ? patches[i + 1].LOD : p.LOD,
                                z ? patches[i - axis].LOD : p.LOD, z + 1 < axis ? patches[i + axis].LOD : p.LOD };
            }
    }

    void BuildPatchMesh(const TerrainComponent& t, const TerrainPatch& p,
                        std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
    {
        vertices.clear();
        indices.clear();
        if (!Valid(t) || !p.CellsX || !p.CellsZ || p.X >= t.Resolution - 1 || p.Z >= t.Resolution - 1 ||
            p.CellsX > t.Resolution - 1 - p.X || p.CellsZ > t.Resolution - 1 - p.Z) return;
        const uint32_t step = 1u << std::min(p.LOD, TerrainMaxLOD);
        std::vector<uint32_t> xs, zs;
        for (uint32_t x = 0; x < p.CellsX; x += step) xs.push_back(p.X + x);
        for (uint32_t z = 0; z < p.CellsZ; z += step) zs.push_back(p.Z + z);
        xs.push_back(p.X + p.CellsX);
        zs.push_back(p.Z + p.CellsZ);
        vertices.reserve(xs.size() * zs.size());
        for (uint32_t z : zs)
            for (uint32_t x : xs)
            {
                const glm::vec3 point = Point(t, x, z);
                vertices.push_back({ point, SampleNormal(t, point.x, point.z), { float(x) / float(t.Resolution - 1), float(z) / float(t.Resolution - 1) } });
            }
        const uint32_t columns = uint32_t(xs.size()), rows = uint32_t(zs.size());
        auto vertex = [&](uint32_t x, uint32_t z) { return z * columns + x; };
        auto triangle = [&](uint32_t a, uint32_t b, uint32_t c) {
            const glm::ivec2 origin(xs[a % columns], zs[a / columns]);
            const glm::ivec2 ab = glm::ivec2(xs[b % columns], zs[b / columns]) - origin;
            const glm::ivec2 ac = glm::ivec2(xs[c % columns], zs[c / columns]) - origin;
            const int64_t area = int64_t(ac.x) * ab.y - int64_t(ab.x) * ac.y;
            if (area > 0) indices.insert(indices.end(), { a, b, c });
            else if (area < 0) indices.insert(indices.end(), { a, c, b });
        };
        auto grid = [&](uint32_t x0, uint32_t z0, uint32_t x1, uint32_t z1) {
            for (uint32_t z = z0; z + 1 < z1; ++z)
                for (uint32_t x = x0; x + 1 < x1; ++x)
                {
                    auto a = vertex(x, z), b = vertex(x + 1, z), c = vertex(x, z + 1), d = vertex(x + 1, z + 1);
                    triangle(a, c, b);
                    triangle(b, c, d);
                }
        };
        indices.reserve((xs.size() - 1) * (zs.size() - 1) * 6);
        if (std::none_of(p.Neighbors.begin(), p.Neighbors.end(), [&](uint8_t n) { return n > p.LOD; }))
        {
            grid(0, 0, columns, rows);
            return;
        }
        // Boundary chains omit fine vertices that the coarse neighbor does not have. The ring
        // connects those chains to the unmodified fine interior with a conforming triangulation.
        // This avoids both geometric and rasterization T-junctions (including mixed-LOD corners).
        auto horizontal = [&](uint32_t z, uint32_t first, uint32_t end, uint8_t lod) {
            std::vector<uint32_t> result;
            const uint32_t stride = 1u << std::min(lod, TerrainMaxLOD);
            for (uint32_t x = first; x < end; ++x)
                if (x == first || x + 1 == end || (xs[x] - p.X) % stride == 0) result.push_back(vertex(x, z));
            return result;
        };
        auto vertical = [&](uint32_t x, uint32_t first, uint32_t end, uint8_t lod) {
            std::vector<uint32_t> result;
            const uint32_t stride = 1u << std::min(lod, TerrainMaxLOD);
            for (uint32_t z = first; z < end; ++z)
                if (z == first || z + 1 == end || (zs[z] - p.Z) % stride == 0) result.push_back(vertex(x, z));
            return result;
        };
        auto zip = [&](const std::vector<uint32_t>& outer, const std::vector<uint32_t>& inner, bool alongX) {
            if (outer.empty() || inner.empty()) return;
            size_t a = 0, b = 0;
            auto coordinate = [&](uint32_t i) { return alongX ? xs[i % columns] : zs[i / columns]; };
            while (a + 1 < outer.size() || b + 1 < inner.size())
            {
                if (b + 1 == inner.size() || (a + 1 < outer.size() && coordinate(outer[a + 1]) <= coordinate(inner[b + 1])))
                {
                    triangle(outer[a], inner[b], outer[a + 1]);
                    ++a;
                }
                else
                {
                    triangle(outer[a], inner[b], inner[b + 1]);
                    ++b;
                }
            }
        };
        if (columns == 2)
        {
            zip(vertical(0, 0, rows, std::max(p.LOD, p.Neighbors[0])),
                vertical(columns - 1, 0, rows, std::max(p.LOD, p.Neighbors[1])), false);
            return;
        }
        if (rows == 2)
        {
            zip(horizontal(0, 0, columns, std::max(p.LOD, p.Neighbors[2])),
                horizontal(rows - 1, 0, columns, std::max(p.LOD, p.Neighbors[3])), true);
            return;
        }
        grid(1, 1, columns - 1, rows - 1);
        zip(horizontal(0, 0, columns, std::max(p.LOD, p.Neighbors[2])), horizontal(1, 1, columns - 1, p.LOD), true);
        zip(horizontal(rows - 1, 0, columns, std::max(p.LOD, p.Neighbors[3])), horizontal(rows - 2, 1, columns - 1, p.LOD), true);
        zip(vertical(0, 0, rows, std::max(p.LOD, p.Neighbors[0])), vertical(1, 1, rows - 1, p.LOD), false);
        zip(vertical(columns - 1, 0, rows, std::max(p.LOD, p.Neighbors[1])), vertical(columns - 2, 1, rows - 1, p.LOD), false);
    }
}
