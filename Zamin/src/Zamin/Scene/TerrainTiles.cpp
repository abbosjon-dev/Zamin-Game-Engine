#include "Zamin/Scene/TerrainTiles.h"
#include <algorithm>
#include <cmath>
namespace Zamin::TerrainTools {
    namespace {
        bool Finite(glm::dvec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        bool Valid(const TerrainComponent& t)
        {
            return t.Resolution >= 2 && t.Resolution <= TerrainComponent::MaxResolution &&
                   std::isfinite(t.Size.x) && std::isfinite(t.Size.y) && t.Size.x >= 1 && t.Size.y >= 1 && t.Size.x <= 4096 && t.Size.y <= 4096 &&
                   std::isfinite(t.HeightLimit) && t.HeightLimit >= 1 && t.HeightLimit <= 4096 && t.Heights.size() == size_t(t.Resolution) * t.Resolution &&
                   std::all_of(t.Heights.begin(), t.Heights.end(), [](float h) { return std::isfinite(h); });
        }
    }
    float WorldHeight(glm::dvec2 p, const WorldTerrainSettings& s)
    {
        const double phase = double(s.Seed % 65521) * .017;
        return float(s.Base + s.Amplitude * (.6 * std::sin(p.x * s.Frequency + phase) * std::cos(p.y * s.Frequency - phase) +
                                             .25 * std::sin((p.x + p.y) * s.Frequency * 1.7 + phase)));
    }
    bool GenerateWorldTile(TerrainComponent& t, glm::dvec3 origin, const WorldTerrainSettings& settings, std::string& error)
    {
        error.clear();
        if (!Valid(t) || !Finite(origin) || !std::isfinite(settings.Base) || !std::isfinite(settings.Amplitude) ||
            settings.Amplitude < 0 || !std::isfinite(settings.Frequency) || settings.Frequency <= 0)
        {
            error = "Invalid world terrain parameters";
            return false;
        }
        auto heights = t.Heights;
        for (uint32_t z = 0; z < t.Resolution; ++z)
            for (uint32_t x = 0; x < t.Resolution; ++x)
            {
                const glm::dvec2 p = glm::dvec2(origin.x, origin.z) + glm::dvec2(x, z) * glm::dvec2(t.Size) / double(t.Resolution - 1);
                const double value = WorldHeight(p, settings) - origin.y;
                if (!std::isfinite(value) || value < 0 || value > t.HeightLimit)
                {
                    error = "World terrain height is outside the tile height range";
                    return false;
                }
                heights[size_t(z) * t.Resolution + x] = float(value);
            }
        t.Heights = std::move(heights);
        t.LockBorderLOD = true;
        t.GeometryChanged();
        return true;
    }
    bool StitchTiles(TerrainComponent& a, glm::dvec3 oa, TerrainComponent& b, glm::dvec3 ob, uint32_t blendRows, std::string& error)
    {
        error.clear();
        auto fail = [&](const char* why) { error = why; return false; };
        if (&a == &b || !Valid(a) || !Valid(b) || !Finite(oa) || !Finite(ob)) return fail("Invalid terrain tiles");
        auto near = [](double x, double y) { return std::abs(x - y) <= 1e-5; };
        bool alongX = false, aPositive = true;
        if (near(oa.z, ob.z) && near(a.Size.y, b.Size.y) && (near(oa.x + a.Size.x, ob.x) || near(ob.x + b.Size.x, oa.x)))
        {
            alongX = true;
            aPositive = near(oa.x + a.Size.x, ob.x);
        }
        else if (near(oa.x, ob.x) && near(a.Size.x, b.Size.x) && (near(oa.z + a.Size.y, ob.z) || near(ob.z + b.Size.y, oa.z)))
            aPositive = near(oa.z + a.Size.y, ob.z);
        else return fail("Terrain tiles must share a complete axis-aligned edge");
        const uint32_t coarse = std::min(a.Resolution, b.Resolution) - 1;
        if ((a.Resolution - 1) % coarse || (b.Resolution - 1) % coarse) return fail("Terrain edge resolutions are incompatible");
        auto index = [&](const TerrainComponent& t, bool positive, uint32_t i, uint32_t row) {
            const uint32_t edge = positive ? t.Resolution - 1 - row : row;
            return alongX ? size_t(i) * t.Resolution + edge : size_t(edge) * t.Resolution + i;
        };
        const double lower = std::max(oa.y, ob.y), upper = std::min(oa.y + a.HeightLimit, ob.y + b.HeightLimit);
        if (lower > upper) return fail("Terrain height ranges do not intersect");
        std::vector<double> edge(coarse + 1);
        for (uint32_t i = 0; i <= coarse; ++i)
        {
            const double x = a.Heights[index(a, aPositive, i * (a.Resolution - 1) / coarse, 0)] + oa.y;
            const double y = b.Heights[index(b, !aPositive, i * (b.Resolution - 1) / coarse, 0)] + ob.y;
            edge[i] = std::clamp((x + y) * .5, lower, upper);
        }
        auto apply = [&](TerrainComponent& t, double worldY, bool positive) {
            const uint32_t rows = std::min(blendRows, t.Resolution - 2);
            for (uint32_t i = 0; i < t.Resolution; ++i)
            {
                const double u = double(i) * coarse / (t.Resolution - 1);
                const auto lo = std::min(uint32_t(u), coarse - 1);
                const double desired = std::lerp(edge[lo], edge[lo + 1], u - lo) - worldY;
                const double delta = desired - t.Heights[index(t, positive, i, 0)];
                for (uint32_t row = 0; row <= rows; ++row)
                {
                    auto& height = t.Heights[index(t, positive, i, row)];
                    height = float(std::clamp(height + delta * (1.0 - double(row) / (rows + 1)), 0.0, double(t.HeightLimit)));
                }
            }
            t.LockBorderLOD = true;
            t.GeometryChanged();
        };
        apply(a, oa.y, aPositive);
        apply(b, ob.y, !aPositive);
        return true;
    }
}
