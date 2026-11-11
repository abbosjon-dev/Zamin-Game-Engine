#include "Zamin/Scene/Terrain.h"
#include "Zamin/Renderer/TerrainSurface.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Scene/TerrainLOD.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Physics/CollisionShapes3D.h"
#include "Zamin/Project/Project.h"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace Zamin {
    struct TerrainComponent::Cache
    {
        struct SourceImage
        {
            std::string Path;
            std::filesystem::file_time_type Modified{};
            int W = 0, H = 0;
            std::vector<uint8_t> Data;
        };
        std::array<SourceImage, 4> Sources;
        std::vector<TerrainPatch> Patches;
        std::vector<Ref<Mesh>> PatchMeshes;
        std::vector<uint32_t> PatchKeys, PatchVertices;
        std::vector<float> HeightSnapshot;
        glm::vec2 PatchSize{};
        uint32_t PatchResolution = 0;
        uint64_t PatchRevision = 0;
        TerrainRenderStats Stats;
        Ref<Mesh> MeshAsset;
        Ref<Texture2D> Albedo;
        Ref<TerrainSurface> Surface;
        std::vector<glm::vec4> ControlSnapshot;
        struct MapState
        {
            std::filesystem::path Path;
            std::filesystem::file_time_type Time{};
            bool Tried = false;
            Ref<Texture2D> Texture;
        };
        std::array<MapState, 12> Maps;
        Ref<CollisionMesh3D> CollisionMesh;
        uint64_t MeshRevision = 0, CollisionRevision = 0, TextureRevision = 0;
    };
    TerrainComponent::TerrainComponent(const TerrainComponent& o) { *this = o; }
    TerrainComponent& TerrainComponent::operator=(const TerrainComponent& o)
    {
        if (this == &o) return *this;
        Resolution = o.Resolution;
        Size = o.Size;
        HeightLimit = o.HeightLimit;
        Collision = o.Collision;
        LODEnabled = o.LODEnabled;
        LockBorderLOD = o.LockBorderLOD;
        LODDistanceScale = o.LODDistanceScale;
        Heights = o.Heights;
        Weights = o.Weights;
        Layers = o.Layers;
        GeometryRevision = o.GeometryRevision;
        PaintRevision = o.PaintRevision;
        Runtime.reset();
        return *this;
    }
    void TerrainComponent::Reset(uint32_t resolution, glm::vec2 size, float height)
    {
        Resolution = std::clamp(resolution, 2u, MaxResolution);
        Size = size;
        Heights.assign(size_t(Resolution) * Resolution, height);
        Weights.assign(Heights.size(), glm::vec4(1, 0, 0, 0));
        EnsureSize();
        GeometryChanged();
        PaintChanged();
    }
    void TerrainComponent::EnsureSize()
    {
        Resolution = std::clamp(Resolution, 2u, MaxResolution);
        for (int a = 0; a < 2; ++a) Size[a] = std::isfinite(Size[a]) ? std::clamp(Size[a], 1.0f, 4096.0f) : 64.0f;
        HeightLimit = std::isfinite(HeightLimit) ? std::clamp(HeightLimit, 1.0f, 4096.0f) : 128.0f;
        LODDistanceScale = std::isfinite(LODDistanceScale) ? std::clamp(LODDistanceScale, 0.25f, 8.0f) : 2.0f;
        const size_t count = size_t(Resolution) * Resolution;
        bool changed = false;
        if (Heights.size() != count)
        {
            Heights.resize(count, 0);
            changed = true;
        }
        if (Weights.size() != count)
        {
            Weights.resize(count, glm::vec4(1, 0, 0, 0));
            changed = true;
        }
        for (float& h : Heights)
        {
            const float v = std::isfinite(h) ? std::clamp(h, 0.0f, HeightLimit) : 0.0f;
            if (v != h)
            {
                h = v;
                changed = true;
            }
        }
        for (auto& w : Weights)
        {
            for (int i = 0; i < 4; ++i) w[i] = std::isfinite(w[i]) ? std::clamp(w[i], 0.0f, 1.0f) : 0.0f;
            const float sum = w.x + w.y + w.z + w.w;
            // Repeated division by an almost-unit float sum can oscillate by one ULP,
            // making unchanged paint data upload every frame. The shader normalizes after sampling.
            if (sum <= 1e-6f) w = glm::vec4(1, 0, 0, 0);
            else if (std::abs(sum - 1.0f) > 1e-6f) w /= sum;
        }
        for (auto& l : Layers)
        {
            for (int i = 0; i < 3; ++i) l.Color[i] = std::isfinite(l.Color[i]) ? std::clamp(l.Color[i], 0.0f, 1.0f) : 0.3f;
            l.Tiling = std::isfinite(l.Tiling) ? std::clamp(l.Tiling, 0.001f, 100.0f) : 0.25f;
            l.Roughness = std::isfinite(l.Roughness) ? std::clamp(l.Roughness, 0.04f, 1.0f) : 0.95f;
            l.Metallic = std::isfinite(l.Metallic) ? std::clamp(l.Metallic, 0.0f, 1.0f) : 0.0f;
            l.Occlusion = std::isfinite(l.Occlusion) ? std::clamp(l.Occlusion, 0.0f, 1.0f) : 1.0f;
            l.NormalStrength = std::isfinite(l.NormalStrength) ? std::clamp(l.NormalStrength, 0.0f, 4.0f) : 1.0f;
        }
        if (changed)
        {
            GeometryChanged();
            PaintChanged();
        }
    }
    namespace TerrainTools {
        static bool Valid(const TerrainComponent& t) { return t.Resolution >= 2 && t.Resolution <= TerrainComponent::MaxResolution && t.Size.x > 0 && t.Size.y > 0 && t.Heights.size() == size_t(t.Resolution) * t.Resolution; }
        static glm::vec3 Position(const TerrainComponent& t, int x, int z) { return { float(x) * t.Size.x / float(t.Resolution - 1), t.Heights[size_t(z) * t.Resolution + size_t(x)], float(z) * t.Size.y / float(t.Resolution - 1) }; }
        float SampleHeight(const TerrainComponent& t, float x, float z)
        {
            if (!Valid(t) || !std::isfinite(x) || !std::isfinite(z)) return 0;
            float gx = std::clamp(x / t.Size.x, 0.0f, 1.0f) * float(t.Resolution - 1), gz = std::clamp(z / t.Size.y, 0.0f, 1.0f) * float(t.Resolution - 1);
            int ix = std::min(int(gx), int(t.Resolution) - 2), iz = std::min(int(gz), int(t.Resolution) - 2);
            float u = gx - float(ix), v = gz - float(iz);
            float a = Position(t, ix, iz).y, b = Position(t, ix + 1, iz).y, c = Position(t, ix, iz + 1).y, d = Position(t, ix + 1, iz + 1).y;
            return u + v <= 1 ? a + (b - a) * u + (c - a) * v : d + (c - d) * (1 - u) + (b - d) * (1 - v);
        }
        glm::vec3 SampleNormal(const TerrainComponent& t, float x, float z)
        {
            if (!Valid(t)) return { 0, 1, 0 };
            if (!std::isfinite(x) || !std::isfinite(z)) return { 0, 1, 0 };
            x = std::clamp(x, 0.0f, t.Size.x);
            z = std::clamp(z, 0.0f, t.Size.y);
            float dx = t.Size.x / float(t.Resolution - 1), dz = t.Size.y / float(t.Resolution - 1);
            const float left = std::max(0.0f, x - dx), right = std::min(t.Size.x, x + dx);
            const float back = std::max(0.0f, z - dz), front = std::min(t.Size.y, z + dz);
            return glm::normalize(glm::vec3((SampleHeight(t, left, z) - SampleHeight(t, right, z)) / std::max(right - left, 1e-6f), 1,
                                            (SampleHeight(t, x, back) - SampleHeight(t, x, front)) / std::max(front - back, 1e-6f)));
        }
        static bool Triangle(glm::vec3 o, glm::vec3 d, glm::vec3 a, glm::vec3 b, glm::vec3 c, float& distance)
        {
            auto e1 = b - a, e2 = c - a, p = glm::cross(d, e2);
            float det = glm::dot(e1, p);
            if (std::abs(det) < 1e-8f) return false;
            float inv = 1 / det;
            auto s = o - a;
            float u = glm::dot(s, p) * inv;
            if (u < -1e-5f || u > 1.00001f) return false;
            auto q = glm::cross(s, e1);
            float v = glm::dot(d, q) * inv;
            if (v < -1e-5f || u + v > 1.00001f) return false;
            distance = glm::dot(e2, q) * inv;
            return distance >= 0 && std::isfinite(distance);
        }
        bool Raycast(const TerrainComponent& t, const glm::vec3& o, const glm::vec3& d, TerrainHit& hit, float maxDistance)
        {
            if (!Valid(t) || !std::isfinite(maxDistance) || maxDistance <= 0) return false;
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite(o[i]) || !std::isfinite(d[i])) return false;
            if (glm::dot(d, d) < 1e-12f) return false;
            float begin = 0, end = maxDistance;
            for (int i : { 0, 2 })
            {
                float extent = i == 0 ? t.Size.x : t.Size.y;
                if (std::abs(d[i]) < 1e-9f)
                {
                    if (o[i] < 0 || o[i] > extent) return false;
                }
                else
                {
                    float a = -o[i] / d[i], b = (extent - o[i]) / d[i];
                    if (a > b) std::swap(a, b);
                    begin = std::max(begin, a);
                    end = std::min(end, b);
                }
            }
            if (begin > end) return false;
            const int cells = int(t.Resolution) - 1;
            glm::vec2 step = t.Size / float(cells);
            glm::vec3 entry = o + d * begin;
            int x = std::clamp(int(entry.x / step.x), 0, cells - 1), z = std::clamp(int(entry.z / step.y), 0, cells - 1);
            int sx = d.x < 0 ? -1 : 1, sz = d.z < 0 ? -1 : 1;
            float tx = std::abs(d.x) < 1e-9f ? std::numeric_limits<float>::infinity() : ((float(x + (sx > 0)) * step.x - o.x) / d.x);
            float tz = std::abs(d.z) < 1e-9f ? std::numeric_limits<float>::infinity() : ((float(z + (sz > 0)) * step.y - o.z) / d.z);
            float deltaX = std::abs(d.x) < 1e-9f ? std::numeric_limits<float>::infinity() : std::abs(step.x / d.x), deltaZ = std::abs(d.z) < 1e-9f ? std::numeric_limits<float>::infinity() : std::abs(step.y / d.z);
            for (int n = 0; n < cells * 2 + 4 && x >= 0 && x < cells && z >= 0 && z < cells; ++n)
            {
                auto a = Position(t, x, z), b = Position(t, x + 1, z), c = Position(t, x, z + 1), e = Position(t, x + 1, z + 1);
                float best = end + 1, dist;
                glm::vec3 normal{ 0, 1, 0 };
                if (Triangle(o, d, a, c, b, dist) && dist >= begin - 1e-4f && dist <= end)
                {
                    best = dist;
                    normal = glm::normalize(glm::cross(c - a, b - a));
                }
                if (Triangle(o, d, b, c, e, dist) && dist >= begin - 1e-4f && dist <= end && dist < best)
                {
                    best = dist;
                    normal = glm::normalize(glm::cross(c - b, e - b));
                }
                float next = std::min(tx, tz);
                if (best <= end && best <= next + 1e-4f)
                {
                    hit = { o + d * best, normal, best };
                    return true;
                }
                if (next > end || !std::isfinite(next)) break;
                bool crossX = tx <= tz, crossZ = tz <= tx;
                if (crossX)
                {
                    x += sx;
                    tx += deltaX;
                }
                if (crossZ)
                {
                    z += sz;
                    tz += deltaZ;
                }
                begin = next;
            }
            return false;
        }
        bool ApplyBrush(TerrainComponent& t, glm::vec2 center, const TerrainBrushSettings& b, float seconds)
        {
            t.EnsureSize();
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(b.Radius) || b.Radius <= 0 || !std::isfinite(b.Strength) || b.Strength <= 0 || !std::isfinite(seconds) || seconds <= 0) return false;
            const float radius = std::clamp(b.Radius, 0.05f, 4096.0f), strength = std::min(b.Strength, 1000.0f) * std::min(seconds, 0.1f), hardness = std::isfinite(b.Hardness) ? std::clamp(b.Hardness, 0.0f, 0.99f) : 0.3f;
            if (center.x - radius > t.Size.x || center.y - radius > t.Size.y || center.x + radius < 0 || center.y + radius < 0) return false;
            const auto old = b.Tool == TerrainBrush::Smooth ? t.Heights : std::vector<float>();
            bool changed = false;
            int n = int(t.Resolution);
            glm::vec2 spacing = t.Size / float(n - 1);
            int x0 = std::clamp(int(std::floor((center.x - radius) / spacing.x)), 0, n - 1), x1 = std::clamp(int(std::ceil((center.x + radius) / spacing.x)), 0, n - 1);
            int z0 = std::clamp(int(std::floor((center.y - radius) / spacing.y)), 0, n - 1), z1 = std::clamp(int(std::ceil((center.y + radius) / spacing.y)), 0, n - 1);
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                {
                    float distance = glm::length(glm::vec2(float(x) * spacing.x, float(z) * spacing.y) - center) / radius;
                    if (distance >= 1) continue;
                    float f = 1 - glm::smoothstep(hardness, 1.0f, distance);
                    size_t i = size_t(z) * t.Resolution + size_t(x);
                    if (b.Tool == TerrainBrush::Paint)
                    {
                        int layer = std::clamp(b.Layer, 0, 3);
                        auto target = glm::vec4(0);
                        target[layer] = 1;
                        auto w = glm::mix(t.Weights[i], target, std::clamp(strength * f, 0.0f, 1.0f));
                        if (glm::length(w - t.Weights[i]) > 1e-7f)
                        {
                            t.Weights[i] = w;
                            changed = true;
                        }
                        continue;
                    }
                    float h = t.Heights[i], value = h;
                    if (b.Tool == TerrainBrush::Raise) value += strength * f;
                    else if (b.Tool == TerrainBrush::Lower) value -= strength * f;
                    else if (b.Tool == TerrainBrush::Flatten && std::isfinite(b.TargetHeight)) value = glm::mix(h, std::clamp(b.TargetHeight, 0.0f, t.HeightLimit), std::clamp(strength * f, 0.0f, 1.0f));
                    else if (b.Tool == TerrainBrush::Smooth)
                    {
                        float sum = 0;
                        int count = 0;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                sum += old[size_t(std::clamp(z + dz, 0, n - 1)) * t.Resolution + size_t(std::clamp(x + dx, 0, n - 1))];
                                ++count;
                            }
                        value = glm::mix(h, sum / float(count), std::clamp(strength * f, 0.0f, 1.0f));
                    }
                    value = std::clamp(value, 0.0f, t.HeightLimit);
                    if (value != h)
                    {
                        t.Heights[i] = value;
                        changed = true;
                    }
                }
            if (changed)
            {
                if (b.Tool == TerrainBrush::Paint) t.PaintChanged();
                else t.GeometryChanged();
            }
            return changed;
        }
        void Generate(TerrainComponent& t, uint32_t seed, float amplitude, float frequency)
        {
            t.EnsureSize();
            amplitude = std::isfinite(amplitude) ? std::clamp(amplitude, 0.0f, t.HeightLimit) : 0;
            frequency = std::isfinite(frequency) ? std::clamp(frequency, 0.001f, 1.0f) : 0.04f;
            auto hash = [seed](int x, int z) {uint32_t h=uint32_t(x)*374761393u+uint32_t(z)*668265263u+seed*1442695041u;h=(h^(h>>13u))*1274126177u;return float(h^(h>>16u))/float(UINT32_MAX); };
            auto noise = [&](float x, float z) {int ix=int(std::floor(x)),iz=int(std::floor(z));float u=x-float(ix),v=z-float(iz);u=u*u*(3-2*u);v=v*v*(3-2*v);return glm::mix(glm::mix(hash(ix,iz),hash(ix+1,iz),u),glm::mix(hash(ix,iz+1),hash(ix+1,iz+1),u),v); };
            for (uint32_t z = 0; z < t.Resolution; ++z)
                for (uint32_t x = 0; x < t.Resolution; ++x)
                {
                    float px = float(x) * t.Size.x / float(t.Resolution - 1) * frequency, pz = float(z) * t.Size.y / float(t.Resolution - 1) * frequency;
                    float h = (noise(px, pz) + noise(px * 2, pz * 2) * 0.5f + noise(px * 4, pz * 4) * 0.25f) / 1.75f;
                    t.Heights[size_t(z) * t.Resolution + x] = h * amplitude;
                }
            t.GeometryChanged();
        }
        void BuildMesh(const TerrainComponent& t, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
        {
            vertices.clear();
            indices.clear();
            if (!Valid(t)) return;
            vertices.reserve(t.Heights.size());
            indices.reserve(size_t(t.Resolution - 1) * (t.Resolution - 1) * 6);
            for (uint32_t z = 0; z < t.Resolution; ++z)
                for (uint32_t x = 0; x < t.Resolution; ++x)
                {
                    auto p = Position(t, int(x), int(z));
                    vertices.push_back({ p, SampleNormal(t, p.x, p.z), { float(x) / float(t.Resolution - 1), float(z) / float(t.Resolution - 1) } });
                }
            for (uint32_t z = 0; z + 1 < t.Resolution; ++z)
                for (uint32_t x = 0; x + 1 < t.Resolution; ++x)
                {
                    uint32_t a = z * t.Resolution + x, b = a + 1, c = a + t.Resolution, d = c + 1;
                    indices.insert(indices.end(), { a, c, b, b, c, d });
                }
        }
        static TerrainComponent::Cache& Cache(const TerrainComponent& t)
        {
            if (!t.Runtime) t.Runtime = CreateRef<TerrainComponent::Cache>();
            return *t.Runtime;
        }
        Ref<const CollisionMesh3D> GetCollisionMesh(const TerrainComponent& t)
        {
            auto& cache = Cache(t);
            if (!cache.CollisionMesh || cache.CollisionRevision != t.GeometryRevision)
            {
                std::vector<MeshVertex> v;
                std::vector<uint32_t> i;
                BuildMesh(t, v, i);
                if (v.empty()) return nullptr;
                auto mesh = CreateRef<CollisionMesh3D>();
                for (auto& a : v) mesh->Positions.push_back(a.Position);
                mesh->Indices = std::move(i);
                cache.CollisionMesh = mesh;
                cache.CollisionRevision = t.GeometryRevision;
            }
            return cache.CollisionMesh;
        }
        Ref<Mesh> GetRenderMesh(const TerrainComponent& t)
        {
            auto& cache = Cache(t);
            if (!cache.MeshAsset || cache.MeshRevision != t.GeometryRevision)
            {
                std::vector<MeshVertex> v;
                std::vector<uint32_t> i;
                BuildMesh(t, v, i);
                if (v.empty()) return nullptr;
                if (!cache.MeshAsset || cache.MeshAsset->GetVertexCount() != v.size()) cache.MeshAsset = CreateRef<Mesh>(v, i);
                else cache.MeshAsset->UpdateVertices(v);
                cache.MeshRevision = t.GeometryRevision;
            }
            return cache.MeshAsset;
        }
        const std::vector<Ref<Mesh>>& GetRenderPatches(const TerrainComponent& t, const glm::mat4& world, const glm::vec3& camera, bool fullDetail)
        {
            auto& cache = Cache(t);
            cache.Stats = {};
            const bool layoutChanged = cache.PatchResolution != t.Resolution || cache.PatchSize != t.Size || cache.Patches.empty();
            std::vector<bool> dirty(cache.Patches.size(), false);
            if (layoutChanged || cache.PatchRevision != t.GeometryRevision)
            {
                auto layout = BuildPatchLayout(t);
                if (layout.empty())
                {
                    cache.Patches.clear();
                    cache.PatchMeshes.clear();
                    return cache.PatchMeshes;
                }
                dirty.assign(layout.size(), layoutChanged || cache.HeightSnapshot.size() != t.Heights.size());
                if (!layoutChanged && cache.HeightSnapshot.size() == t.Heights.size())
                {
                    const uint32_t axis = (t.Resolution - 2) / TerrainPatchCells + 1;
                    for (uint32_t z = 0; z < t.Resolution; ++z)
                        for (uint32_t x = 0; x < t.Resolution; ++x)
                        {
                            const size_t index = size_t(z) * t.Resolution + x;
                            if (cache.HeightSnapshot[index] == t.Heights[index]) continue;
                            // One sample of padding covers normal derivatives on neighboring patches.
                            uint32_t x0 = (x ? x - 1 : 0) / TerrainPatchCells;
                            uint32_t z0 = (z ? z - 1 : 0) / TerrainPatchCells;
                            uint32_t x1 = std::min((x + 1) / TerrainPatchCells, axis - 1);
                            uint32_t z1 = std::min((z + 1) / TerrainPatchCells, axis - 1);
                            for (uint32_t zz = z0; zz <= z1; ++zz)
                                for (uint32_t xx = x0; xx <= x1; ++xx) dirty[size_t(zz) * axis + xx] = true;
                        }
                    for (size_t i = 0; i < layout.size(); ++i) layout[i].LOD = cache.Patches[i].LOD;
                }
                cache.Patches = std::move(layout);
                cache.HeightSnapshot = t.Heights;
                cache.PatchResolution = t.Resolution;
                cache.PatchSize = t.Size;
                cache.PatchRevision = t.GeometryRevision;
                if (layoutChanged)
                {
                    cache.PatchMeshes.assign(cache.Patches.size(), nullptr);
                    cache.PatchKeys.assign(cache.Patches.size(), ~uint32_t(0));
                    cache.PatchVertices.assign(cache.Patches.size(), 0);
                }
            }
            std::vector<uint8_t> previous;
            if (!layoutChanged)
                for (const auto& patch : cache.Patches) previous.push_back(patch.LOD);
            SelectPatchLODs(cache.Patches, t, world, camera, previous, fullDetail);
            for (size_t index = 0; index < cache.Patches.size(); ++index)
            {
                const auto& patch = cache.Patches[index];
                uint32_t key = patch.LOD;
                for (int edge = 0; edge < 4; ++edge) key |= uint32_t(patch.Neighbors[edge]) << (3 * (edge + 1));
                if (!cache.PatchMeshes[index] || dirty[index] || cache.PatchKeys[index] != key)
                {
                    std::vector<MeshVertex> vertices;
                    std::vector<uint32_t> indices;
                    BuildPatchMesh(t, patch, vertices, indices);
                    if (vertices.empty()) continue;
                    if (cache.PatchMeshes[index] && cache.PatchKeys[index] == key && cache.PatchMeshes[index]->GetVertexCount() == vertices.size() &&
                        cache.PatchMeshes[index]->GetIndexCount() == indices.size())
                        cache.PatchMeshes[index]->UpdateVertices(vertices);
                    else cache.PatchMeshes[index] = CreateRef<Mesh>(vertices, indices);
                    cache.PatchKeys[index] = key;
                    cache.PatchVertices[index] = uint32_t(vertices.size());
                    ++cache.Stats.RebuiltPatches;
                }
                if (!cache.PatchMeshes[index]) continue;
                ++cache.Stats.Patches;
                ++cache.Stats.LODCounts[patch.LOD];
                cache.Stats.Vertices += cache.PatchVertices[index];
                cache.Stats.Triangles += cache.PatchMeshes[index]->GetIndexCount() / 3;
            }
            cache.Stats.FullResolutionTriangles = (t.Resolution - 1) * (t.Resolution - 1) * 2;
            return cache.PatchMeshes;
        }
        TerrainRenderStats GetRenderStats(const TerrainComponent& t)
        {
            return t.Runtime ? t.Runtime->Stats : TerrainRenderStats{};
        }

        Ref<Texture2D> GetAlbedo(const TerrainComponent& t)
        {
            auto& cache = Cache(t);
            if (cache.Albedo && cache.TextureRevision == t.PaintRevision) return cache.Albedo;
            stbi_set_flip_vertically_on_load_thread(0);
            constexpr uint32_t size = 512;
            std::vector<uint8_t> pixels(size * size * 4);
            auto& images = cache.Sources;
            for (int l = 0; l < 4; ++l)
            {
                auto& image = images[l];
                auto path = t.Layers[l].TexturePath.empty() ? std::filesystem::path() : Project::GetAssetFileSystemPath(t.Layers[l].TexturePath);
                std::error_code ec;
                const auto modified = path.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, ec);
                if (image.Path == path.string() && image.Modified == modified) continue;
                image = {};
                image.Path = path.string();
                image.Modified = modified;
                int channels = 0, w = 0, h = 0;
                if (path.empty() || ec || !stbi_info(path.string().c_str(), &w, &h, &channels) || w <= 0 || h <= 0 || int64_t(w) * h > 16777216) continue;
                stbi_uc* source = stbi_load(path.string().c_str(), &w, &h, &channels, 3);
                if (!source) continue;
                // The albedo is baked at 512 square; retain bounded source data across brush frames.
                image.W = std::min(w, 1024);
                image.H = std::min(h, 1024);
                image.Data.resize(size_t(image.W) * size_t(image.H) * 3);
                for (int z = 0; z < image.H; ++z)
                    for (int x = 0; x < image.W; ++x)
                        for (int c = 0; c < 3; ++c)
                            image.Data[(size_t(z) * image.W + x) * 3 + c] = source[((size_t(z) * size_t(h) / size_t(image.H)) * size_t(w) + size_t(x) * size_t(w) / size_t(image.W)) * 3 + c];
                stbi_image_free(source);
            }
            for (uint32_t z = 0; z < size; ++z)
                for (uint32_t x = 0; x < size; ++x)
                {
                    float u = float(x) / float(size - 1), v = float(z) / float(size - 1), gx = u * float(t.Resolution - 1), gz = v * float(t.Resolution - 1);
                    int ix = std::min(int(gx), int(t.Resolution) - 2), iz = std::min(int(gz), int(t.Resolution) - 2);
                    float a = gx - float(ix), b = gz - float(iz);
                    auto weight = [&](int dx, int dz) { return t.Weights[size_t(iz + dz) * t.Resolution + size_t(ix + dx)]; };
                    glm::vec4 weights = glm::mix(glm::mix(weight(0, 0), weight(1, 0), a), glm::mix(weight(0, 1), weight(1, 1), a), b);
                    glm::vec3 color(0);
                    for (int l = 0; l < 4; ++l)
                    {
                        auto c = t.Layers[l].Color;
                        if (!images[l].Data.empty())
                        {
                            auto& im = images[l];
                            int xx = std::min(int(glm::fract(u * t.Size.x * t.Layers[l].Tiling) * float(im.W)), im.W - 1), zz = std::min(int(glm::fract(v * t.Size.y * t.Layers[l].Tiling) * float(im.H)), im.H - 1);
                            size_t p = (size_t(zz) * size_t(im.W) + size_t(xx)) * 3;
                            c *= glm::vec3(im.Data[p], im.Data[p + 1], im.Data[p + 2]) / 255.0f;
                        }
                        else
                        {
                            uint32_t n = (x * 1973u + z * 9277u + uint32_t(l) * 26699u) * 747796405u;
                            c *= 0.93f + float((n >> 8u) & 255u) / 255.0f * 0.14f;
                        }
                        color += c * weights[l];
                    }
                    size_t p = (size_t(z) * size + x) * 4;
                    for (int i = 0; i < 3; ++i) pixels[p + size_t(i)] = uint8_t(std::clamp(color[i], 0.0f, 1.0f) * 255);
                    pixels[p + 3] = 255;
                }
            if (!cache.Albedo)
            {
                TextureSpecification spec;
                spec.Width = size;
                spec.Height = size;
                spec.Wrap = TextureWrap::ClampToEdge;
                cache.Albedo = Texture2D::Create(spec);
            }
            cache.Albedo->SetData(pixels.data(), uint32_t(pixels.size()));
            cache.TextureRevision = t.PaintRevision;
            return cache.Albedo;
        }
        Ref<TerrainSurface> GetSurface(const TerrainComponent& t)
        {
            auto& cache = Cache(t);
            if (!cache.Surface) cache.Surface = CreateRef<TerrainSurface>();
            auto& surface = *cache.Surface;
            surface.Layers = t.Layers;
            surface.LocalSize = t.Size;
            const uint32_t resolution = t.Resolution;
            if (resolution < 2 || resolution > TerrainComponent::MaxResolution || t.Weights.size() != size_t(resolution) * resolution) return nullptr;
            if (!surface.Weights || surface.Weights->GetWidth() != resolution)
            {
                TextureSpecification spec;
                spec.Width = spec.Height = resolution;
                spec.Format = ImageFormat::RGBA32F;
                spec.SRGB = false;
                spec.GenerateMips = false;
                spec.Wrap = TextureWrap::ClampToEdge;
                surface.Weights = Texture2D::Create(spec);
                cache.ControlSnapshot.clear();
            }
            if (cache.ControlSnapshot != t.Weights)
            {
                surface.Weights->SetData(t.Weights.data(), uint32_t(t.Weights.size() * sizeof(glm::vec4)));
                cache.ControlSnapshot = t.Weights;
                ++surface.WeightUploads;
            }
            surface.ControlBytes = resolution * resolution * uint32_t(sizeof(glm::vec4));
            surface.WeightUV = { float(resolution - 1) / float(resolution), 0.5f / float(resolution) };
            surface.MissingMaps = 0;
            auto load = [&](const std::string& relative, size_t slot) {
                auto& state = cache.Maps[slot];
                auto path = relative.empty() ? std::filesystem::path{} : FileSystem::SafeJoin(Project::GetAssetDirectory(), relative);
                if (!path.empty() && !FileSystem::IsSubPathOnDisk(path, Project::GetAssetDirectory())) path.clear();
                std::error_code ec;
                auto time = path.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, ec);
                if (!state.Tried || state.Path != path || state.Time != time)
                {
                    state.Path = path;
                    state.Time = time;
                    state.Tried = true;
                    state.Texture = path.empty() || ec ? nullptr : TextureLibrary::Load(path);
                }
                if (!relative.empty() && !state.Texture) ++surface.MissingMaps;
                return state.Texture;
            };
            for (size_t i = 0; i < 4; ++i)
            {
                surface.Albedo[i] = load(t.Layers[i].TexturePath, i * 3);
                surface.Normal[i] = load(t.Layers[i].NormalMapPath, i * 3 + 1);
                surface.Mask[i] = load(t.Layers[i].MaskMapPath, i * 3 + 2);
            }
            return cache.Surface;
        }
        bool ImportHeightmap(TerrainComponent& t, const std::filesystem::path& path, std::string& error)
        {
            stbi_set_flip_vertically_on_load_thread(0);
            t.EnsureSize();
            std::vector<uint16_t> values;
            int width = 0, height = 0;
            if (path.extension() == ".r16")
            {
                std::ifstream f(path, std::ios::binary | std::ios::ate);
                if (!f)
                {
                    error = "Fayl ochilmadi";
                    return false;
                }
                auto bytes = f.tellg();
                if (bytes <= 0 || bytes > 33570818 || bytes % 2 != 0)
                {
                    error = "R16 hajmi noto'g'ri";
                    return false;
                }
                size_t count = size_t(bytes) / 2;
                width = int(std::sqrt(double(count)));
                height = width;
                if (size_t(width) * size_t(width) != count)
                {
                    error = "R16 kvadrat bo'lishi kerak";
                    return false;
                }
                f.seekg(0);
                std::vector<unsigned char> raw(size_t(bytes), 0);
                f.read(reinterpret_cast<char*>(raw.data()), bytes);
                if (!f)
                {
                    error = "Fayl to'liq o'qilmadi";
                    return false;
                }
                values.resize(count);
                for (size_t i = 0; i < count; ++i) values[i] = uint16_t(uint16_t(raw[i * 2]) | (uint16_t(raw[i * 2 + 1]) << 8u));
            }
            else
            {
                int channels = 0;
                if (!stbi_info(path.string().c_str(), &width, &height, &channels) || width < 2 || height < 2 || int64_t(width) * height > 16777216)
                {
                    error = "PNG / rasm ochilmadi yoki 16 MP dan katta";
                    return false;
                }
                auto* data = stbi_load_16(path.string().c_str(), &width, &height, &channels, 1);
                if (!data)
                {
                    error = "Heightmap ochilmadi";
                    return false;
                }
                values.assign(data, data + size_t(width) * size_t(height));
                stbi_image_free(data);
            }
            if (width < 2 || height < 2)
            {
                error = "Kamida 2x2 heightmap kerak";
                return false;
            }
            std::vector<float> heights(t.Heights.size());
            for (uint32_t z = 0; z < t.Resolution; ++z)
                for (uint32_t x = 0; x < t.Resolution; ++x)
                {
                    int xx = int(float(x) / float(t.Resolution - 1) * float(width - 1)), zz = int(float(z) / float(t.Resolution - 1) * float(height - 1));
                    heights[size_t(z) * t.Resolution + x] = float(values[size_t(zz) * size_t(width) + size_t(xx)]) / 65535.0f * t.HeightLimit;
                }
            t.Heights = std::move(heights);
            t.GeometryChanged();
            error.clear();
            return true;
        }
        bool ExportHeightmap(const TerrainComponent& t, const std::filesystem::path& path, std::string& error)
        {
            if (!Valid(t) || path.extension() != ".r16")
            {
                error = ".r16 formatini tanlang";
                return false;
            }
            auto temp = path;
            temp += "." + std::to_string(uint64_t(UUID())) + ".tmp";
            std::ofstream f(temp, std::ios::binary | std::ios::trunc);
            if (!f)
            {
                error = "Fayl yozilmadi";
                return false;
            }
            for (float h : t.Heights)
            {
                uint16_t v = uint16_t(std::clamp(h / t.HeightLimit, 0.0f, 1.0f) * 65535.0f);
                char b[2] = { char(v & 255u), char(v >> 8u) };
                f.write(b, 2);
            }
            f.close();
            if (!f)
            {
                std::error_code ec;
                std::filesystem::remove(temp, ec);
                error = "Fayl to'liq yozilmadi";
                return false;
            }
            std::error_code ec;
            std::filesystem::rename(temp, path, ec);
            if (ec)
            {
                std::filesystem::remove(temp);
                error = "Fayl almashtirilmadi";
                return false;
            }
            error.clear();
            return true;
        }
    }
}
