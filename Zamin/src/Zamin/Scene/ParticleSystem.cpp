#include "Zamin/Scene/ParticleSystem.h"
#include "Zamin/Scene/Components.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Zamin {

    // ------------------------------------------------------------------ gradient
    glm::vec4 ParticleGradient::Evaluate(float t) const
    {
        if (Stops.empty())
            return glm::vec4(1.0f);
        if (Stops.size() == 1 || t <= Stops.front().Time)
            return Stops.front().Color;
        if (t >= Stops.back().Time)
            return Stops.back().Color;
        auto it = std::upper_bound(Stops.begin(), Stops.end(), t, [](float time, const Stop& s) { return time < s.Time; });
        const Stop& b = *it;
        const Stop& a = *(it - 1);
        if (Fixed)
            return a.Color;
        const float span = b.Time - a.Time;
        return span > 0.0f ? glm::mix(a.Color, b.Color, (t - a.Time) / span) : b.Color;
    }

    void ParticleGradient::Sort()
    {
        std::stable_sort(Stops.begin(), Stops.end(), [](const Stop& a, const Stop& b) { return a.Time < b.Time; });
    }

    int ParticleGradient::AddStop(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        Stop stop{ t, Evaluate(t) };
        auto it = std::upper_bound(Stops.begin(), Stops.end(), t, [](float time, const Stop& s) { return time < s.Time; });
        const int index = (int)(it - Stops.begin());
        Stops.insert(it, stop);
        return index;
    }

    ParticleGradient ParticleGradient::TwoStops(const glm::vec4& from, const glm::vec4& to)
    {
        ParticleGradient g;
        g.Stops = { { 0.0f, from }, { 1.0f, to } };
        return g;
    }

    // ------------------------------------------------------------------ curve
    float ParticleCurve::Evaluate(float t) const
    {
        // PropertyAnimation::Evaluate for one component, without the vectors (called for every
        // particle every frame): the same numbers.
        const size_t n = Keys.size();
        if (n == 0)
            return 1.0f;
        const AnimationKey& first = Keys.front();
        if (n == 1 || t <= first.Time)
            return first.Value.x;
        const AnimationKey& last = Keys.back();
        if (t >= last.Time)
            return last.Value.x;
        size_t i = 1; // the first key after t (a curve has a few keys)
        while (i < n - 1 && !(t < Keys[i].Time))
            i++;
        const AnimationKey& a = Keys[i - 1];
        const AnimationKey& b = Keys[i];
        const float dt = b.Time - a.Time;
        if (dt <= 0.0f)
            return b.Value.x;
        if (a.Interpolation == KeyInterpolation::Step)
            return a.Value.x;
        const float s = std::clamp((t - a.Time) / dt, 0.0f, 1.0f);
        if (a.Interpolation == KeyInterpolation::Linear)
            return a.Value.x * (1.0f - s) + b.Value.x * s;
        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
        const float h10 = s3 - 2.0f * s2 + s;
        const float h01 = -2.0f * s3 + 3.0f * s2;
        const float h11 = s3 - s2;
        return h00 * a.Value.x + h10 * dt * a.OutTangent.x + h01 * b.Value.x + h11 * dt * b.InTangent.x;
    }

    void ParticleCurve::Sort()
    {
        std::stable_sort(Keys.begin(), Keys.end(), [](const AnimationKey& a, const AnimationKey& b) { return a.Time < b.Time; });
        PropertyAnimation::UpdateTangents(Keys);
    }

    // Values at the keys and in between (enough for the overshoot of a free Bezier tangent).
    template<typename F>
    static void SampleCurve(const ParticleCurve& curve, F&& f)
    {
        if (curve.Keys.empty())
        {
            f(1.0f);
            return;
        }
        for (const AnimationKey& k : curve.Keys)
            f(k.Value.x);
        bool smooth = false;
        for (const AnimationKey& k : curve.Keys)
            smooth |= k.Interpolation == KeyInterpolation::Bezier;
        if (smooth)
            for (int i = 0; i <= 32; i++)
                f(curve.Evaluate(i / 32.0f));
    }

    float ParticleCurve::GetMin() const
    {
        float v = std::numeric_limits<float>::max();
        SampleCurve(*this, [&](float x) { v = std::min(v, x); });
        return v;
    }

    float ParticleCurve::GetMax() const
    {
        float v = -std::numeric_limits<float>::max();
        SampleCurve(*this, [&](float x) { v = std::max(v, x); });
        return v;
    }

    int ParticleCurve::AddKey(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        AnimationKey key;
        key.Time = t;
        key.Value = glm::vec4(Evaluate(t), 0.0f, 0.0f, 0.0f);
        auto it = std::upper_bound(Keys.begin(), Keys.end(), t, [](float time, const AnimationKey& k) { return time < k.Time; });
        if (it != Keys.begin())
            key.Interpolation = (it - 1)->Interpolation;
        const int index = (int)(it - Keys.begin());
        Keys.insert(it, key);
        PropertyAnimation::UpdateTangents(Keys);
        return index;
    }

    ParticleCurve ParticleCurve::Constant(float value)
    {
        ParticleCurve c;
        AnimationKey a;
        a.Time = 0.0f;
        a.Value.x = value;
        AnimationKey b = a;
        b.Time = 1.0f;
        c.Keys = { a, b };
        return c;
    }

    ParticleCurve ParticleCurve::Linear(float from, float to)
    {
        ParticleCurve c = Constant(from);
        c.Keys[1].Value.x = to;
        return c;
    }

    // ------------------------------------------------------------------ emitter mesh
    Ref<ParticleEmitterMesh> ParticleEmitterMesh::Build(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices)
    {
        auto mesh = CreateRef<ParticleEmitterMesh>();
        mesh->Positions = positions;
        mesh->VertexNormals.assign(positions.size(), glm::vec3(0.0f));
        const size_t vertexCount = positions.size();
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
                continue;
            mesh->Indices.insert(mesh->Indices.end(), { a, b, c });
        }
        const size_t triangles = mesh->GetTriangleCount();
        mesh->FaceNormals.resize(triangles);
        mesh->AreaSums.resize(triangles);
        mesh->EdgeSums.resize(triangles * 3);
        // Each edge once: the second triangle at an edge (by vertex index) adds nothing, nor does an
        // edge between two triangles in one plane (a quad's diagonal) - Edge mode draws the outline.
        std::unordered_map<uint64_t, size_t> edges; // first triangle edge (triangle * 3 + k) by vertex pair
        std::vector<float> edgeLength(triangles * 3, 0.0f);
        float area = 0.0f;
        for (size_t t = 0; t < triangles; t++)
        {
            const uint32_t* tri = &mesh->Indices[t * 3];
            const glm::vec3 &a = positions[tri[0]], &b = positions[tri[1]], &c = positions[tri[2]];
            const glm::vec3 cross = glm::cross(b - a, c - a);
            const float length = glm::length(cross);
            mesh->FaceNormals[t] = length > 1e-12f ? cross / length : glm::vec3(0.0f, 1.0f, 0.0f);
            area += length * 0.5f;
            mesh->AreaSums[t] = area;
            for (int k = 0; k < 3; k++)
                mesh->VertexNormals[tri[k]] += cross; // area weighted
            for (int k = 0; k < 3; k++)
            {
                const uint32_t i0 = tri[k], i1 = tri[(k + 1) % 3];
                const uint64_t key = ((uint64_t)std::min(i0, i1) << 32) | std::max(i0, i1);
                auto [it, inserted] = edges.try_emplace(key, t * 3 + k);
                if (inserted)
                    edgeLength[t * 3 + k] = glm::length(positions[i1] - positions[i0]);
                else if (glm::dot(mesh->FaceNormals[it->second / 3], mesh->FaceNormals[t]) > 0.9999f)
                    edgeLength[it->second] = 0.0f; // inside a flat face
            }
        }
        float total = 0.0f;
        for (size_t i = 0; i < edgeLength.size(); i++)
            mesh->EdgeSums[i] = total += edgeLength[i];
        if (total <= 0.0f) // only flat faces' insides (cannot happen for a closed mesh): every edge
            for (size_t t = 0, i = 0; t < triangles; t++)
                for (int k = 0; k < 3; k++, i++)
                {
                    const uint32_t* tri = &mesh->Indices[t * 3];
                    mesh->EdgeSums[i] = total += glm::length(positions[tri[(k + 1) % 3]] - positions[tri[k]]);
                }
        for (glm::vec3& n : mesh->VertexNormals)
        {
            const float length = glm::length(n);
            n = length > 1e-12f ? n / length : glm::vec3(0.0f, 1.0f, 0.0f);
        }
        return mesh;
    }

    // The index of the first running total above `value` (a random point of the whole).
    static size_t PickBySum(const std::vector<float>& sums, float value)
    {
        const size_t i = (size_t)(std::upper_bound(sums.begin(), sums.end(), value) - sums.begin());
        return std::min(i, sums.size() - 1);
    }

    static glm::vec3 BlendNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float u, float v, const glm::vec3& fallback)
    {
        const glm::vec3 n = a * (1.0f - u - v) + b * u + c * v;
        const float length = glm::length(n);
        return length > 1e-6f ? n / length : fallback;
    }

    void ParticleEmitterMesh::Sample(ParticleMeshMode mode, uint32_t& rng, glm::vec3& position, glm::vec3& normal) const
    {
        position = glm::vec3(0.0f);
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
        if (IsEmpty())
            return;
        switch (mode)
        {
            case ParticleMeshMode::Vertex:
            {
                const size_t i = std::min(Positions.size() - 1, (size_t)(ParticleSimulation::Random01(rng) * Positions.size()));
                position = Positions[i];
                normal = VertexNormals[i];
                return;
            }
            case ParticleMeshMode::Edge:
            {
                const size_t e = PickBySum(EdgeSums, ParticleSimulation::Random01(rng) * EdgeSums.back());
                const size_t t = e / 3;
                const uint32_t i0 = Indices[t * 3 + e % 3], i1 = Indices[t * 3 + (e + 1) % 3];
                const float f = ParticleSimulation::Random01(rng);
                position = glm::mix(Positions[i0], Positions[i1], f);
                const glm::vec3 n = glm::mix(VertexNormals[i0], VertexNormals[i1], f);
                normal = glm::length(n) > 1e-6f ? glm::normalize(n) : FaceNormals[t];
                return;
            }
            default:
            {
                const size_t t = PickBySum(AreaSums, ParticleSimulation::Random01(rng) * AreaSums.back());
                float u = ParticleSimulation::Random01(rng), v = ParticleSimulation::Random01(rng);
                if (u + v > 1.0f)
                {
                    u = 1.0f - u;
                    v = 1.0f - v;
                }
                const uint32_t* tri = &Indices[t * 3];
                const glm::vec3 &a = Positions[tri[0]], &b = Positions[tri[1]], &c = Positions[tri[2]];
                position = a + (b - a) * u + (c - a) * v;
                normal = BlendNormal(VertexNormals[tri[0]], VertexNormals[tri[1]], VertexNormals[tri[2]], u, v, FaceNormals[t]);
                return;
            }
        }
    }

    // ------------------------------------------------------------------ trails
    void ParticleTrailPool::Reset(int capacity)
    {
        Capacity = std::clamp(capacity, 0, 1024);
        Points.clear();
        First.clear();
        Count.clear();
        Free.clear();
    }

    int ParticleTrailPool::Allocate()
    {
        if (Capacity <= 0)
            return -1;
        int slot;
        if (!Free.empty())
        {
            slot = Free.back();
            Free.pop_back();
        }
        else
        {
            slot = (int)Count.size();
            First.push_back(0);
            Count.push_back(0);
            Points.resize(Points.size() + (size_t)Capacity);
        }
        First[(size_t)slot] = 0;
        Count[(size_t)slot] = 0;
        return slot;
    }

    void ParticleTrailPool::Release(int slot)
    {
        if (slot < 0 || slot >= (int)Count.size())
            return;
        Count[(size_t)slot] = 0;
        Free.push_back(slot);
    }

    void ParticleTrailPool::Push(int slot, const glm::vec3& position, float time)
    {
        uint16_t& first = First[(size_t)slot];
        uint16_t& count = Count[(size_t)slot];
        if (count == Capacity)
        {
            first = (uint16_t)((first + 1) % Capacity); // the oldest goes
            count--;
        }
        Points[(size_t)slot * Capacity + (first + count) % Capacity] = { position, time };
        count++;
    }

    void ParticleTrailPool::Trim(int slot, float time, float maxAge)
    {
        uint16_t& first = First[(size_t)slot];
        uint16_t& count = Count[(size_t)slot];
        while (count > 0 && time - Points[(size_t)slot * Capacity + first].Time > maxAge)
        {
            first = (uint16_t)((first + 1) % Capacity);
            count--;
        }
    }

    namespace ParticleSimulation {

        using Shape = ParticleSystemComponent::EmitterShape;

        // Events the sub-emitters want (bits by ParticleEvent::Type).
        static uint32_t GetEventMask(const ParticleSystemComponent& ps)
        {
            if (!ps.SubEmitters.Enabled)
                return 0;
            uint32_t mask = 0;
            for (const auto& sub : ps.SubEmitters.Emitters)
            {
                if (!sub.Emitter || sub.Count <= 0 || sub.Probability <= 0.0f)
                    continue;
                using Trigger = ParticleSystemComponent::SubEmitter::TriggerType;
                if (sub.Trigger == Trigger::Birth) mask |= 1u << (int)ParticleEvent::Type::Birth;
                else if (sub.Trigger == Trigger::Death) mask |= 1u << (int)ParticleEvent::Type::Death;
                else if (sub.Trigger == Trigger::Collision) mask |= 1u << (int)ParticleEvent::Type::Collision;
            }
            return mask;
        }

        static bool Wants(uint32_t mask, ParticleEvent::Type type) { return (mask >> (int)type) & 1u; }

        // At most this many events wait for the scene (a system run on its own keeps no more).
        static constexpr size_t MaxEvents = 16384;

        float Random01(uint32_t& state)
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            return (state & 0xFFFFFF) / float(0x1000000);
        }

        // lowbias32 (Chris Wellons): a well mixed 32-bit hash.
        static uint32_t Hash(uint32_t x)
        {
            x ^= x >> 16; x *= 0x7feb352du;
            x ^= x >> 15; x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }

        float SeedRandom(uint32_t seed, uint32_t salt)
        {
            return (Hash(seed ^ (salt * 0x9E3779B9u)) & 0xFFFFFF) / float(0x1000000);
        }

        // ---- noise
        // The gradient of a lattice corner (Perlin's improved set: the 12 edge midpoints of a cube).
        static float Gradient(uint32_t hash, float x, float y, float z)
        {
            switch (hash & 15u)
            {
                case 0: case 12: return x + y;
                case 1: case 13: return -x + y;
                case 2: return x - y;
                case 3: return -x - y;
                case 4: return x + z;
                case 5: return -x + z;
                case 6: return x - z;
                case 7: return -x - z;
                case 8: return y + z;
                case 9: case 14: return -y + z;
                case 10: return y - z;
                default: return -y - z;
            }
        }

        // A point's lattice cell, its place in it and the fade weights there: the same for the
        // three axes' noises of a layer (NoiseField works them out once).
        struct NoiseCell
        {
            int X, Y, Z;
            float Fx, Fy, Fz; // 0..1 in the cell
            float Ux, Uy, Uz; // faded
        };

        static NoiseCell MakeNoiseCell(float px, float py, float pz)
        {
            NoiseCell c;
            const float floorX = std::floor(px), floorY = std::floor(py), floorZ = std::floor(pz);
            c.Fx = px - floorX;
            c.Fy = py - floorY;
            c.Fz = pz - floorZ;
            c.X = (int)floorX;
            c.Y = (int)floorY;
            c.Z = (int)floorZ;
            // Quintic fade: the field and its slope are smooth across the lattice.
            c.Ux = c.Fx * c.Fx * c.Fx * (c.Fx * (c.Fx * 6.0f - 15.0f) + 10.0f);
            c.Uy = c.Fy * c.Fy * c.Fy * (c.Fy * (c.Fy * 6.0f - 15.0f) + 10.0f);
            c.Uz = c.Fz * c.Fz * c.Fz * (c.Fz * (c.Fz * 6.0f - 15.0f) + 10.0f);
            return c;
        }

        static float NoiseAt(const NoiseCell& c, uint32_t seed)
        {
            const uint32_t hs = Hash(seed);
            const uint32_t hz0 = Hash((uint32_t)c.Z ^ hs), hz1 = Hash((uint32_t)(c.Z + 1) ^ hs);
            const uint32_t h00 = Hash((uint32_t)c.Y + hz0), h10 = Hash((uint32_t)(c.Y + 1) + hz0);
            const uint32_t h01 = Hash((uint32_t)c.Y + hz1), h11 = Hash((uint32_t)(c.Y + 1) + hz1);
            const uint32_t x0 = (uint32_t)c.X, x1 = (uint32_t)(c.X + 1);
            const float fx = c.Fx, fy = c.Fy, fz = c.Fz;
            const float n000 = Gradient(Hash(x0 + h00), fx, fy, fz), n100 = Gradient(Hash(x1 + h00), fx - 1.0f, fy, fz);
            const float n010 = Gradient(Hash(x0 + h10), fx, fy - 1.0f, fz), n110 = Gradient(Hash(x1 + h10), fx - 1.0f, fy - 1.0f, fz);
            const float n001 = Gradient(Hash(x0 + h01), fx, fy, fz - 1.0f), n101 = Gradient(Hash(x1 + h01), fx - 1.0f, fy, fz - 1.0f);
            const float n011 = Gradient(Hash(x0 + h11), fx, fy - 1.0f, fz - 1.0f), n111 = Gradient(Hash(x1 + h11), fx - 1.0f, fy - 1.0f, fz - 1.0f);
            const float x00 = n000 + (n100 - n000) * c.Ux, x10 = n010 + (n110 - n010) * c.Ux;
            const float x01 = n001 + (n101 - n001) * c.Ux, x11 = n011 + (n111 - n011) * c.Ux;
            const float y0 = x00 + (x10 - x00) * c.Uy, y1 = x01 + (x11 - x01) * c.Uy;
            return std::clamp(y0 + (y1 - y0) * c.Uz, -1.0f, 1.0f);
        }

        float Noise(const glm::vec3& p, uint32_t seed)
        {
            return NoiseAt(MakeNoiseCell(p.x, p.y, p.z), seed);
        }

        glm::vec3 NoiseField(const glm::vec3& p, uint32_t seed, int octaves, float octaveMultiplier, const glm::bvec3& axes)
        {
            octaves = std::clamp(octaves, 1, 8);
            float sum[3] = { 0.0f, 0.0f, 0.0f };
            float amplitude = 1.0f, total = 0.0f, frequency = 1.0f;
            for (int o = 0; o < octaves; o++)
            {
                const uint32_t s = Hash(seed * 0x632BE5ABu + (uint32_t)o * 0x85EBCA6Bu);
                const NoiseCell cell = MakeNoiseCell(p.x * frequency, p.y * frequency, p.z * frequency);
                for (int axis = 0; axis < 3; axis++)
                    if (axes[axis])
                        sum[axis] += NoiseAt(cell, s + (uint32_t)axis * 0x27D4EB2Fu) * amplitude;
                total += amplitude;
                amplitude *= octaveMultiplier;
                frequency *= 2.0f;
            }
            return total > 0.0f ? glm::vec3(sum[0], sum[1], sum[2]) / total : glm::vec3(0.0f);
        }

        // Two unit vectors at right angles to `dir` (a unit vector).
        static void Basis(const glm::vec3& dir, glm::vec3& t, glm::vec3& b)
        {
            glm::vec3 up = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            t = glm::normalize(glm::cross(up, dir));
            b = glm::cross(dir, t);
        }

        glm::vec3 RandomInCone(const glm::vec3& direction, float spreadDegrees, uint32_t& rng)
        {
            glm::vec3 dir = glm::length(direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0, 1, 0);
            float spread = glm::radians(std::clamp(spreadDegrees, 0.0f, 180.0f));
            float cosTheta = 1.0f - Random01(rng) * (1.0f - std::cos(spread));
            float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
            float phi = Random01(rng) * glm::two_pi<float>();
            glm::vec3 t, b;
            Basis(dir, t, b);
            return glm::normalize(t * (std::cos(phi) * sinTheta) + b * (std::sin(phi) * sinTheta) + dir * cosTheta);
        }

        // The direction away from the centre through `local` (`fallback` at the centre itself).
        static glm::vec3 Outward(const glm::vec3& local, const glm::vec3& fallback)
        {
            const float length = glm::length(local);
            return length > 1e-6f ? local / length : fallback;
        }

        void SampleShape(const ParticleSystemComponent& ps, uint32_t& rng, glm::vec3& local, glm::vec3& dir)
        {
            local = glm::vec3(0.0f);
            // The part of the radius that emits: from 1 - thickness to 1 (1: all of it).
            const float inner = 1.0f - std::clamp(ps.ShapeThickness, 0.0f, 1.0f);
            const Shape shape = ps.ShapeEnabled ? ps.Shape : Shape::Point; // the module off: from the centre
            switch (shape)
            {
                case Shape::Circle:
                {
                    // Uniform over the ring's area (the whole disc: r = sqrt(u)).
                    float a = Random01(rng) * glm::two_pi<float>(), r = std::sqrt(inner * inner + (1.0f - inner * inner) * Random01(rng)) * ps.ShapeRadius;
                    local = { std::cos(a) * r, std::sin(a) * r, 0.0f };
                    break;
                }
                case Shape::Sphere:
                {
                    const float inner3 = inner * inner * inner;
                    local = RandomInCone({ 0, 1, 0 }, 180.0f, rng) * (std::cbrt(inner3 + (1.0f - inner3) * Random01(rng)) * ps.ShapeRadius);
                    break;
                }
                case Shape::Box:
                    local = (glm::vec3(Random01(rng), Random01(rng), Random01(rng)) * 2.0f - 1.0f) * ps.ShapeExtents;
                    break;
                case Shape::Edge:
                    local = { (Random01(rng) - 0.5f) * ps.EdgeLength, 0.0f, 0.0f };
                    break;
                case Shape::Cone:
                {
                    // Every ray goes through the apex behind the base: at a fraction s of the
                    // radius it leans atan(s * tan(angle)) away from the axis.
                    const glm::vec3 axis = glm::length(ps.Direction) > 0.0f ? glm::normalize(ps.Direction) : glm::vec3(0, 1, 0);
                    const float tanAngle = std::tan(glm::radians(std::clamp(ps.ConeAngle, 0.0f, 89.0f)));
                    glm::vec3 across;
                    float s;
                    if (ps.ConeFlat)
                    {
                        // A line across the axis in the XY plane, both sides alike.
                        glm::vec3 side(axis.y, -axis.x, 0.0f);
                        across = glm::length(side) > 1e-6f ? glm::normalize(side) : glm::vec3(1, 0, 0);
                        float u = Random01(rng);
                        s = inner + (1.0f - inner) * Random01(rng);
                        if (u < 0.5f)
                            s = -s;
                    }
                    else
                    {
                        glm::vec3 t, b;
                        Basis(axis, t, b);
                        float phi = Random01(rng) * glm::two_pi<float>();
                        across = t * std::cos(phi) + b * std::sin(phi);
                        s = std::sqrt(inner * inner + (1.0f - inner * inner) * Random01(rng));
                    }
                    local = across * (s * ps.ShapeRadius);
                    const float lean = std::atan(s * tanAngle);
                    dir = glm::normalize(axis * std::cos(lean) + across * std::sin(lean));
                    if (ps.ConeFrom == ParticleSystemComponent::ConeEmitFrom::Volume)
                        local += dir * (Random01(rng) * std::max(0.0f, ps.ConeLength) / std::max(0.01f, std::cos(lean)));
                    return; // the cone gives the direction
                }
                case Shape::Mesh:
                {
                    if (!ps.MeshRuntime || ps.MeshRuntime->IsEmpty())
                        break; // no mesh (yet): from the centre
                    glm::vec3 normal;
                    ps.MeshRuntime->Sample(ps.MeshMode, rng, local, normal);
                    local += normal * ps.MeshNormalOffset;
                    dir = RandomInCone(ps.MeshAlongNormal ? normal : ps.Direction, ps.Spread, rng);
                    return;
                }
                default: break;
            }
            const bool outward = ps.AlignToShape && (shape == Shape::Circle || shape == Shape::Sphere || shape == Shape::Box);
            dir = RandomInCone(outward ? Outward(local, ps.Direction) : ps.Direction, ps.Spread, rng);
        }

        // The pool holds MaxPoints per trail; a new size starts the trails over (the living
        // particles keep theirs from where they are).
        static void PrepareTrails(ParticleSystemComponent& ps)
        {
            if (!ps.Trails.Enabled)
            {
                if (ps.TrailPool.Capacity != 0)
                {
                    ps.TrailPool.Reset(0);
                    for (auto& p : ps.Particles)
                        p.Trail = -1;
                }
                return;
            }
            const int capacity = std::clamp(ps.Trails.MaxPoints, 2, 256);
            if (ps.TrailPool.Capacity == capacity)
                return;
            ps.TrailPool.Reset(capacity);
            for (auto& p : ps.Particles)
            {
                p.Trail = SeedRandom(p.Seed, 5) < ps.Trails.Ratio ? ps.TrailPool.Allocate() : -1;
                if (p.Trail >= 0)
                    ps.TrailPool.Push(p.Trail, p.Position, ps.Time);
            }
        }

        static ParticleEvent MakeEvent(const ParticleSystemComponent& ps, const Particle& p, ParticleEvent::Type type)
        {
            Particle at = p;
            at.Life = std::clamp(at.Life, 0.0f, at.Lifetime); // a dead one: its look at the end
            return { type, p.Position, p.Velocity, GetColor(ps, at), GetSize(ps, at) };
        }

        void Spawn(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, int count, const ParticleSpawnOrigin* origin)
        {
            uint32_t& rng = ps.RandomState;
            // A sub-emitter's particles: the shape around the origin (its axes the emitter's).
            glm::mat4 at = emitterWorld;
            glm::vec3 localOffset(0.0f), localVelocity(0.0f);
            if (origin)
            {
                at[3] = glm::vec4(origin->Position, 1.0f);
                if (ps.IsLocalSpace())
                {
                    const glm::mat4 inverse = glm::inverse(emitterWorld);
                    localOffset = glm::vec3(inverse * glm::vec4(origin->Position, 1.0f));
                    localVelocity = glm::vec3(inverse * glm::vec4(origin->Velocity, 0.0f));
                }
            }
            // Mesh normals stay at right angles to the surface under a non-uniform scale.
            const bool normals = ps.ShapeEnabled && ps.Shape == Shape::Mesh && ps.MeshAlongNormal && !ps.IsLocalSpace();
            const glm::mat3 normalMatrix = normals ? glm::transpose(glm::inverse(glm::mat3(emitterWorld))) : glm::mat3(1.0f);
            PrepareTrails(ps);
            const bool births = Wants(GetEventMask(ps), ParticleEvent::Type::Birth);
            for (int i = 0; i < count && (int)ps.Particles.size() < ps.MaxParticles; i++)
            {
                Particle p;
                glm::vec3 local, dir;
                SampleShape(ps, rng, local, dir);
                float speed = ps.SpeedMin + (ps.SpeedMax - ps.SpeedMin) * Random01(rng);
                if (!ps.IsLocalSpace())
                {
                    p.Position = glm::vec3(at * glm::vec4(local, 1.0f));
                    p.Velocity = glm::normalize(normals ? normalMatrix * dir : glm::vec3(emitterWorld * glm::vec4(dir, 0.0f))) * speed;
                    if (origin)
                        p.Velocity += origin->Velocity;
                }
                else
                {
                    p.Position = origin ? local + localOffset : local;
                    p.Velocity = origin ? dir * speed + localVelocity : dir * speed;
                }
                p.Lifetime = p.Life = std::max(0.01f, ps.LifetimeMin + (ps.LifetimeMax - ps.LifetimeMin) * Random01(rng));
                p.Rotation = Random01(rng) * glm::two_pi<float>();
                p.AngularVelocity = (Random01(rng) * 2.0f - 1.0f) * ps.AngularVelocityMax;
                // Its own numbers from the generator's state, without drawing more from it (the
                // particles after it stay as they were before particles had seeds).
                p.Seed = Hash(rng ^ 0x5bd1e995u);
                if (origin)
                {
                    p.Tint = origin->Tint;
                    p.SizeScale = origin->SizeScale;
                }
                if (ps.Trails.Enabled && SeedRandom(p.Seed, 5) < ps.Trails.Ratio)
                {
                    p.Trail = ps.TrailPool.Allocate();
                    if (p.Trail >= 0)
                        ps.TrailPool.Push(p.Trail, p.Position, ps.Time);
                }
                if (births && ps.Events.size() < MaxEvents)
                    ps.Events.push_back(MakeEvent(ps, p, ParticleEvent::Type::Birth));
                ps.Particles.push_back(p);
            }
        }

        // ---- the emission's timeline
        // One cycle (seconds; never so short that a step would hold thousands).
        static float GetCycle(const ParticleSystemComponent& ps) { return std::max(0.01f, ps.Duration); }

        float GetEmissionTime(const ParticleSystemComponent& ps, float t0, float dt)
        {
            // dt itself unless a limit falls inside the step (the same sum as before there were limits).
            float active = dt;
            const float delay = std::max(0.0f, ps.StartDelay);
            const float t1 = t0 + dt;
            if (t0 < delay)
                active = std::max(0.0f, t1 - delay);
            if (!ps.Looping)
            {
                const float end = delay + GetCycle(ps);
                if (t1 > end)
                    active = std::max(0.0f, end - std::max(t0, delay));
            }
            return active;
        }

        void GetBurstFirings(const ParticleSystemComponent& ps, float t0, float t1, std::vector<int>& firings)
        {
            firings.clear();
            if (ps.Bursts.empty() || !(t1 > t0))
                return;
            const float delay = std::max(0.0f, ps.StartDelay);
            const float cycle = GetCycle(ps);
            const double u0 = std::max(0.0, (double)t0 - delay), u1 = (double)t1 - delay;
            if (u1 <= u0)
                return;
            // The cycles the step touches (a looping system's repeat; at most a few hundred in one step).
            const int64_t first = (int64_t)std::floor(u0 / cycle);
            int64_t last = (int64_t)std::floor(u1 / cycle);
            if (!ps.Looping)
            {
                if (first > 0)
                    return;
                last = 0;
            }
            last = std::min(last, first + 256);
            for (int64_t c = first; c <= last; c++)
            {
                const double start = (double)c * cycle;
                const double r0 = std::max(u0, start) - start, r1 = std::min(u1, start + cycle) - start; // [r0, r1) of this cycle
                if (r1 <= r0)
                    continue;
                for (int b = 0; b < (int)ps.Bursts.size(); b++)
                {
                    const auto& burst = ps.Bursts[(size_t)b];
                    if (burst.Count <= 0 || burst.Probability <= 0.0f)
                        continue;
                    const double time = std::max(0.0f, burst.Time);
                    const double interval = burst.Interval;
                    // Its k-th firing at time + k * interval (k < Cycles; 0: while in the cycle).
                    int64_t kMax = burst.Cycles > 0 ? burst.Cycles : std::numeric_limits<int32_t>::max();
                    if (interval <= 1e-4)
                    {
                        // No interval: every repeat at once (unlimited: once).
                        if (time >= r0 && time < r1 && time < cycle)
                            for (int64_t k = 0; k < (burst.Cycles > 0 ? burst.Cycles : 1); k++)
                                firings.push_back(b);
                        continue;
                    }
                    kMax = std::min<int64_t>(kMax, (int64_t)std::ceil((cycle - time) / interval)); // inside the cycle
                    int64_t k = std::max<int64_t>(0, (int64_t)std::ceil((r0 - time) / interval));
                    for (; k < kMax && firings.size() < 4096; k++)
                    {
                        const double at = time + (double)k * interval;
                        if (at >= r1)
                            break;
                        if (at >= r0 && at < cycle)
                            firings.push_back(b);
                    }
                }
            }
        }

        // The cycle's emission over one step: the start burst, the bursts list and the rate.
        static void Emit(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float dt)
        {
            uint32_t& rng = ps.RandomState;
            if (!ps.EmissionEnabled)
            {
                // Nothing of its own; its cycle still runs (a non-looping one finishes).
                ps.BurstDone = true;
                ps.EmitAccumulator = 0.0f;
                ps.PlayTime += dt;
                const float delay = std::max(0.0f, ps.StartDelay), cycle = GetCycle(ps);
                if (ps.Looping && ps.PlayTime >= delay + cycle)
                    ps.PlayTime = delay + std::fmod(ps.PlayTime - delay, cycle);
                return;
            }
            const float t0 = ps.PlayTime;
            // The start burst once the delay is over (in the step that reaches it).
            if (!ps.BurstDone && t0 + dt >= std::max(0.0f, ps.StartDelay))
            {
                Spawn(ps, emitterWorld, ps.Burst);
                ps.BurstDone = true;
            }
            if (!ps.Bursts.empty())
            {
                static thread_local std::vector<int> firings;
                GetBurstFirings(ps, t0, t0 + dt, firings);
                for (int b : firings)
                {
                    const auto& burst = ps.Bursts[(size_t)b];
                    if (burst.Probability >= 1.0f || Random01(rng) < burst.Probability)
                        Spawn(ps, emitterWorld, burst.Count);
                }
            }
            ps.EmitAccumulator += ps.EmissionRate * GetEmissionTime(ps, t0, dt);
            int count = (int)ps.EmitAccumulator;
            ps.EmitAccumulator -= count;
            if (count > 0)
                Spawn(ps, emitterWorld, count);
            ps.PlayTime = t0 + dt;
            // A looping system's time stays inside its first cycle (no precision lost over hours).
            const float delay = std::max(0.0f, ps.StartDelay);
            const float cycle = GetCycle(ps);
            if (ps.Looping && ps.PlayTime >= delay + cycle)
                ps.PlayTime = delay + std::fmod(ps.PlayTime - delay, cycle);
        }

        bool Update(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float dt, bool editor, const ParticleEnvironment* environment)
        {
            if (ps.Paused)
                return false; // frozen as it is
            // Prewarm: one whole cycle simulated before the first frame (in steps of 1/30 s, at
            // most 600 of them), the bursts and the emission included; no sub-emitter events.
            if (ps.Playing && ps.Looping && ps.Prewarm && !ps.Prewarmed)
            {
                ps.Prewarmed = true;
                ps.PlayTime = std::max(ps.PlayTime, std::max(0.0f, ps.StartDelay)); // the delay is skipped
                const float cycle = GetCycle(ps);
                const int steps = std::clamp((int)std::ceil(cycle * 30.0f), 1, 600);
                const float step = cycle / (float)steps;
                for (int i = 0; i < steps; i++)
                {
                    Emit(ps, emitterWorld, step);
                    Integrate(ps, emitterWorld, step, environment);
                    ps.Events.clear();
                }
            }
            dt *= std::max(0.0f, ps.SimulationSpeed);
            if (ps.Playing && !ps.Finished)
                Emit(ps, emitterWorld, dt);
            Integrate(ps, emitterWorld, dt, environment);
            if (!ps.Playing || ps.Looping || ps.Finished || !ps.Particles.empty() ||
                ps.PlayTime < std::max(0.0f, ps.StartDelay) + GetCycle(ps))
                return false;
            if (editor)
            {
                // Previewed again from the start.
                ps.PlayTime = 0.0f;
                ps.BurstDone = false;
                ps.EmitAccumulator = 0.0f;
                return false;
            }
            ps.Finished = true;
            return true;
        }

        bool GetSpaceToWorld(const ParticleSystemComponent& ps, const glm::mat4& emitterWorld, const ParticleEnvironment* environment, glm::mat4& out)
        {
            if (ps.IsLocalSpace())
            {
                out = emitterWorld;
                return true;
            }
            if (ps.Space == ParticleSystemComponent::SimulationSpace::Custom && environment && environment->HasSpace)
            {
                out = environment->Space;
                return true;
            }
            out = glm::mat4(1.0f);
            return false;
        }

        glm::vec4 GetCollisionPlane(const ParticleSystemComponent& ps, const glm::mat4& emitterWorld, const ParticleEnvironment* environment)
        {
            glm::vec4 plane;
            if (environment && environment->HasPlane)
                plane = environment->Plane;
            else
            {
                const glm::vec3 n = ps.Collision.PlaneNormal;
                const float length = glm::length(n);
                plane = length > 1e-6f ? glm::vec4(n / length, ps.Collision.PlaneOffset / length) : glm::vec4(0.0f, 1.0f, 0.0f, ps.Collision.PlaneOffset);
            }
            glm::mat4 toWorld;
            if (!GetSpaceToWorld(ps, emitterWorld, environment, toWorld))
                return plane;
            // In the particles' space: a point of the plane there, the normal turned (and squeezed) with it.
            const glm::vec3 point = glm::vec3(glm::inverse(toWorld) * glm::vec4(glm::vec3(plane) * plane.w, 1.0f));
            glm::vec3 normal = glm::transpose(glm::mat3(toWorld)) * glm::vec3(plane);
            const float length = glm::length(normal);
            normal = length > 1e-9f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
            return glm::vec4(normal, glm::dot(normal, point));
        }

        // The particles that died go (their trail slots free, a death event each when wanted).
        static void RemoveDead(ParticleSystemComponent& ps, bool deaths)
        {
            auto& particles = ps.Particles;
            size_t kept = 0;
            for (size_t i = 0; i < particles.size(); i++)
            {
                Particle& p = particles[i];
                if (p.Life <= 0.0f)
                {
                    if (deaths && ps.Events.size() < MaxEvents)
                        ps.Events.push_back(MakeEvent(ps, p, ParticleEvent::Type::Death));
                    if (p.Trail >= 0)
                        ps.TrailPool.Release(p.Trail);
                    continue;
                }
                if (kept != i)
                    particles[kept] = p;
                kept++;
            }
            particles.resize(kept);
        }

        void Integrate(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float ts, const ParticleEnvironment* environment)
        {
            ps.Time += ts;
            const float drag = std::max(0.0f, 1.0f - ps.Drag * ts);
            const auto& vol = ps.VelocityOverLifetime;
            const auto& limit = ps.LimitVelocity;
            const auto& noise = ps.Noise;
            const auto& collision = ps.Collision;
            const uint32_t events = GetEventMask(ps);
            PrepareTrails(ps);
            if (!vol.Enabled && !limit.Enabled && !noise.Enabled && !collision.Enabled && !ps.Trails.Enabled && events == 0)
            {
                for (auto& p : ps.Particles)
                {
                    p.Velocity += ps.Gravity * ts;
                    p.Velocity *= drag;
                    p.Position += p.Velocity * ts;
                    p.Rotation += p.AngularVelocity * ts;
                    p.Life -= ts;
                }
                std::erase_if(ps.Particles, [](const Particle& p) { return p.Life <= 0.0f; });
                return;
            }

            // The emitter's rotation (without its scale) and centre, in the particles' space.
            glm::mat3 rotation(1.0f);
            for (int i = 0; i < 3; i++)
            {
                glm::vec3 axis(emitterWorld[i]);
                const float length = glm::length(axis);
                rotation[i] = length > 1e-6f ? axis / length : glm::vec3(0.0f);
            }
            const bool local = ps.IsLocalSpace();
            const glm::vec3 center = !local ? glm::vec3(emitterWorld[3]) : glm::vec3(0.0f);
            // Where the particles' space is in the world (Local: the emitter; Custom: its entity).
            glm::mat4 toWorld(1.0f);
            const bool spaced = GetSpaceToWorld(ps, emitterWorld, environment, toWorld);
            // Linear velocity in the particles' space: emitter axes (World: world axes).
            glm::vec3 linear = vol.Linear;
            if (vol.Enabled)
            {
                if (vol.World)
                {
                    if (spaced)
                    {
                        // The world's axes in the particles' space (their rotation, without scale).
                        glm::mat3 spaceRotation(1.0f);
                        for (int i = 0; i < 3; i++)
                        {
                            glm::vec3 axis(toWorld[i]);
                            const float length = glm::length(axis);
                            spaceRotation[i] = length > 1e-6f ? axis / length : glm::vec3(0.0f);
                        }
                        linear = glm::transpose(spaceRotation) * linear;
                    }
                }
                else if (!local)
                    linear = rotation * linear;
            }
            const bool orbit = vol.Enabled && vol.Orbital != glm::vec3(0.0f);
            const glm::quat spin = orbit ? glm::quat(vol.Orbital * ts) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            // This step's turn around the emitter's own axes, as one matrix for all the particles.
            const glm::mat3 orbitTurn = !orbit ? glm::mat3(1.0f) : !local ? rotation * glm::mat3_cast(spin) * glm::transpose(rotation) : glm::mat3_cast(spin);
            // The speed above the limit kept over this step (frame-rate independent: per 1/60 s).
            const float keep = limit.Enabled ? std::pow(1.0f - std::clamp(limit.Dampen, 0.0f, 1.0f), ts * 60.0f) : 1.0f;

            // Noise: the field drifts along a slanted axis (in 2D the XY picture keeps changing).
            const glm::bvec3 noiseAxes(noise.Strength.x != 0.0f, noise.Strength.y != 0.0f, noise.Strength.z != 0.0f);
            const bool noisy = noise.Enabled && (noiseAxes.x || noiseAxes.y || noiseAxes.z);
            const glm::vec3 scroll = glm::vec3(0.37f, 0.71f, 1.0f) * (ps.Time * noise.ScrollSpeed);
            const float frequency = std::max(0.0f, noise.Frequency);

            // Collisions: the plane in the particles' space, or the world's colliders (cast in world space).
            using CollisionType = ParticleSystemComponent::CollisionModule::CollisionType;
            const bool planeCollision = collision.Enabled && collision.Type == CollisionType::Plane;
            const bool worldCollision = collision.Enabled && collision.Type != CollisionType::Plane && environment && environment->Cast;
            const glm::vec4 plane = planeCollision ? GetCollisionPlane(ps, emitterWorld, environment) : glm::vec4(0.0f);
            const glm::mat4 toLocal = worldCollision && spaced ? glm::inverse(toWorld) : glm::mat4(1.0f);
            // A particle's radius is in world units (it is drawn so); in a scaled space the scale counts.
            const float unitScale = !spaced ? 1.0f
                : std::max(1e-6f, (glm::length(glm::vec3(toWorld[0])) + glm::length(glm::vec3(toWorld[1])) + glm::length(glm::vec3(toWorld[2]))) / 3.0f);
            const float bounce = std::max(0.0f, collision.Bounce);
            const float dampen = std::clamp(collision.Dampen, 0.0f, 1.0f);
            const float restKeep = std::pow(1.0f - dampen, ts * 60.0f); // speed along the surface kept while resting
            // Slower than this off the surface: it comes to rest instead of hopping every frame.
            const float restSpeed = glm::length(ps.Gravity) * ts * 2.0f + 0.01f;
            const bool collisionEvents = Wants(events, ParticleEvent::Type::Collision);
            // Gravity's direction in the world (for the world casts).
            glm::vec3 down(0.0f);
            if (worldCollision)
            {
                const glm::vec3 g = spaced ? glm::mat3(toWorld) * ps.Gravity : ps.Gravity;
                if (glm::length(g) > 1e-6f)
                    down = glm::normalize(g);
            }

            const auto& trails = ps.Trails;
            const float minDistance2 = std::max(0.0f, trails.MinVertexDistance) * std::max(0.0f, trails.MinVertexDistance);

            for (auto& p : ps.Particles)
            {
                const float age = std::clamp(p.GetAge01(), 0.0f, 1.0f);
                p.Velocity += ps.Gravity * ts;
                p.Velocity *= drag;
                if (limit.Enabled)
                {
                    const float maxSpeed = std::max(0.0f, limit.MaxSpeed * limit.MaxSpeedCurve.Evaluate(age));
                    const float speed = glm::length(p.Velocity);
                    if (speed > maxSpeed && speed > 0.0f)
                        p.Velocity *= (maxSpeed + (speed - maxSpeed) * keep) / speed;
                }
                glm::vec3 motion = p.Velocity;
                if (vol.Enabled)
                {
                    motion *= vol.SpeedModifier.Evaluate(age);
                    motion += linear * vol.LinearCurve.Evaluate(age);
                    if (vol.Radial != 0.0f)
                    {
                        const glm::vec3 offset = p.Position - center;
                        const float length = glm::length(offset);
                        if (length > 1e-6f)
                            motion += offset * (vol.Radial / length);
                    }
                    if (orbit)
                    {
                        // Around the emitter's own axes through its centre.
                        p.Position = center + orbitTurn * (p.Position - center);
                    }
                }
                if (noisy)
                    motion += NoiseField(p.Position * frequency + scroll, noise.Seed, noise.Octaves, noise.OctaveMultiplier, noiseAxes) *
                              noise.Strength * noise.StrengthCurve.Evaluate(age);
                const glm::vec3 from = p.Position;
                p.Position += motion * ts;

                if (planeCollision || worldCollision)
                {
                    const float radius = std::max(0.0f, GetSize(ps, p) * 0.5f * collision.RadiusScale);
                    bool hit = false;
                    glm::vec3 normal(0.0f);
                    if (planeCollision)
                    {
                        const float depth = glm::dot(glm::vec3(plane), p.Position) - plane.w - radius / unitScale;
                        if (depth < 0.0f)
                        {
                            hit = true;
                            normal = glm::vec3(plane);
                            p.Position -= normal * depth;
                        }
                    }
                    else
                    {
                        const glm::vec3 a = spaced ? glm::vec3(toWorld * glm::vec4(from, 1.0f)) : from;
                        const glm::vec3 b = spaced ? glm::vec3(toWorld * glm::vec4(p.Position, 1.0f)) : p.Position;
                        glm::vec3 c, n;
                        // Moving along a surface it rests on: the ray down to it keeps it there
                        // (the one along its way would pass over the surface).
                        const bool cast = (a != b && environment->Cast(a, b, radius, c, n)) ||
                                          ((p.Flags & ParticleFlag_Contact) && down != glm::vec3(0.0f) && environment->Cast(b, b + down * 0.02f, radius, c, n));
                        if (cast)
                        {
                            hit = true;
                            if (!spaced)
                            {
                                p.Position = c;
                                normal = n;
                            }
                            else
                            {
                                p.Position = glm::vec3(toLocal * glm::vec4(c, 1.0f));
                                normal = glm::transpose(glm::mat3(toWorld)) * n;
                                const float length = glm::length(normal);
                                normal = length > 1e-9f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
                            }
                        }
                    }
                    if (hit)
                    {
                        const bool impact = !(p.Flags & ParticleFlag_Contact);
                        const float into = glm::dot(p.Velocity, normal);
                        if (into < 0.0f)
                        {
                            const glm::vec3 along = (p.Velocity - normal * into) * (impact ? 1.0f - dampen : restKeep);
                            float away = -into * bounce;
                            if (away < restSpeed)
                                away = 0.0f;
                            p.Velocity = along + normal * away;
                        }
                        if (impact)
                        {
                            p.Life -= std::max(0.0f, collision.LifetimeLoss) * p.Lifetime;
                            if (collisionEvents && ps.Events.size() < MaxEvents)
                                ps.Events.push_back(MakeEvent(ps, p, ParticleEvent::Type::Collision));
                        }
                        if (collision.MinKillSpeed > 0.0f && glm::length(p.Velocity) < collision.MinKillSpeed)
                            p.Life = 0.0f;
                        p.Flags |= ParticleFlag_Contact;
                    }
                    else
                        p.Flags &= ~ParticleFlag_Contact;
                }

                p.Rotation += p.AngularVelocity * ts;
                p.Life -= ts;
                if (p.Trail >= 0)
                {
                    auto& pool = ps.TrailPool;
                    pool.Trim(p.Trail, ps.Time, std::max(0.0f, trails.Lifetime));
                    if (pool.GetCount(p.Trail) == 0)
                        pool.Push(p.Trail, p.Position, ps.Time);
                    else
                    {
                        const glm::vec3 d = p.Position - pool.Get(p.Trail, 0).Position;
                        if (glm::dot(d, d) >= minDistance2)
                            pool.Push(p.Trail, p.Position, ps.Time);
                    }
                }
            }
            RemoveDead(ps, Wants(events, ParticleEvent::Type::Death));
        }

        // ---- look
        glm::vec4 GetColor(const ParticleSystemComponent& ps, const Particle& p)
        {
            const float t = 1.0f - p.Life / p.Lifetime;
            const auto& module = ps.ColorOverLifetime;
            if (!module.Enabled)
                return glm::mix(ps.StartColor, ps.EndColor, t) * p.Tint;
            glm::vec4 color = module.Gradient.Evaluate(t);
            if (module.RandomBetweenTwo)
                color = glm::mix(color, module.GradientB.Evaluate(t), SeedRandom(p.Seed, 1));
            return color * p.Tint;
        }

        float GetSize(const ParticleSystemComponent& ps, const Particle& p)
        {
            const float t = 1.0f - p.Life / p.Lifetime;
            const auto& module = ps.SizeOverLifetime;
            if (!module.Enabled)
                return glm::mix(ps.StartSize, ps.EndSize, t) * p.SizeScale;
            float scale = module.Curve.Evaluate(t);
            if (module.RandomBetweenTwo)
                scale = glm::mix(scale, module.CurveB.Evaluate(t), SeedRandom(p.Seed, 2));
            return ps.StartSize * scale * p.SizeScale;
        }

        float GetMaxSize(const ParticleSystemComponent& ps)
        {
            const auto& module = ps.SizeOverLifetime;
            if (!module.Enabled)
                return std::max(std::abs(ps.StartSize), std::abs(ps.EndSize));
            auto extent = [](const ParticleCurve& c) { return std::max(std::abs(c.GetMin()), std::abs(c.GetMax())); };
            float scale = extent(module.Curve);
            if (module.RandomBetweenTwo)
                scale = std::max(scale, extent(module.CurveB));
            return std::abs(ps.StartSize) * scale;
        }

        void GetDrawOrder(const ParticleSystemComponent& ps, const glm::mat4* toWorld, const glm::vec3& viewer, const glm::vec3& viewForward,
                          bool perspective, std::vector<uint32_t>& order)
        {
            order.clear();
            using Sort = ParticleSystemComponent::SortMode;
            Sort mode = ps.Sorting;
            if (mode == Sort::Auto)
                mode = ps.Additive ? Sort::None : Sort::ByDistance; // additive light adds up in any order
            const size_t n = ps.Particles.size();
            if (mode == Sort::None || n < 2)
                return;
            // A key per particle: the larger, the later it is drawn (on top).
            static thread_local std::vector<float> keys;
            keys.resize(n);
            for (size_t i = 0; i < n; i++)
            {
                const Particle& p = ps.Particles[i];
                switch (mode)
                {
                    case Sort::ByDistance:
                    {
                        const glm::vec3 position = toWorld ? glm::vec3(*toWorld * glm::vec4(p.Position, 1.0f)) : p.Position;
                        const glm::vec3 d = position - viewer;
                        keys[i] = perspective ? -glm::dot(d, d) : -glm::dot(d, viewForward); // the farthest first
                        break;
                    }
                    case Sort::OldestOnTop: keys[i] = p.GetAge(); break;
                    default: keys[i] = -p.GetAge(); break; // YoungestOnTop
                }
            }
            // Already in that order (a 2D system at one depth, the youngest last): drawn as they are.
            bool sorted = true;
            for (size_t i = 1; i < n && sorted; i++)
                sorted = keys[i - 1] <= keys[i];
            if (sorted)
                return;
            order.resize(n);
            for (uint32_t i = 0; i < (uint32_t)n; i++)
                order[i] = i;
            std::stable_sort(order.begin(), order.end(), [](uint32_t a, uint32_t b) { return keys[a] < keys[b]; });
        }

        int WrapAround(ParticleSystemComponent& ps, const glm::vec3& center, const glm::vec3& extents)
        {
            const glm::vec3 e = glm::max(extents, glm::vec3(0.0f));
            if (e.x <= 0.0f && e.y <= 0.0f && e.z <= 0.0f)
                return 0;
            const bool trails = ps.TrailPool.Capacity > 0;
            int moved = 0;
            for (Particle& p : ps.Particles)
            {
                glm::vec3 shift(0.0f);
                for (int axis = 0; axis < 3; axis++)
                {
                    if (e[axis] <= 0.0f)
                        continue;
                    const float d = p.Position[axis] - center[axis];
                    if (d > e[axis] || d < -e[axis])
                    {
                        // Into [-e, e) around the centre (also after a jump of many box sizes).
                        const float size = 2.0f * e[axis];
                        const float wrapped = d - size * std::floor((d + e[axis]) / size);
                        shift[axis] = wrapped - d;
                    }
                }
                if (shift == glm::vec3(0.0f))
                    continue;
                p.Position += shift;
                moved++;
                if (trails && p.Trail >= 0)
                {
                    // The trail goes with it (no streak across the box).
                    auto& pool = ps.TrailPool;
                    const size_t base = (size_t)p.Trail * (size_t)pool.Capacity;
                    for (int i = 0; i < pool.Capacity; i++)
                        pool.Points[base + (size_t)i].Position += shift;
                }
            }
            return moved;
        }

        bool GetStretchedQuad(const ParticleSystemComponent& ps, const glm::vec3& velocity, const glm::vec3& right, const glm::vec3& up,
                              float size, float& rotation, glm::vec2& quad)
        {
            const float speed = glm::length(velocity);
            const glm::vec2 across(glm::dot(velocity, right), glm::dot(velocity, up));
            const float seen = glm::length(across);
            if (!(speed > 1e-5f) || !(seen > 1e-5f))
                return false;
            // The quad's Y along the motion: (0, 1) turned by `rotation` is (-sin, cos).
            rotation = std::atan2(-across.x, across.y);
            const float length = std::max(0.0f, size * ps.StretchLength + speed * std::max(0.0f, ps.StretchSpeed));
            quad = glm::vec2(size, std::max(size * 0.25f, length * (seen / speed)));
            return true;
        }

        float GetNearFade(const ParticleSystemComponent& ps, float distance)
        {
            const float range = ps.FollowCamera.NearFade;
            if (!(range > 0.0f))
                return 1.0f;
            const float t = std::clamp((distance - range * 0.25f) / (range * 0.75f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float GetTrailWidth(const ParticleSystemComponent& ps, float size, float u)
        {
            const auto& trails = ps.Trails;
            return std::max(0.0f, trails.Width * (trails.WidthFromSize ? size : 1.0f) * trails.WidthCurve.Evaluate(std::clamp(u, 0.0f, 1.0f)));
        }

        glm::vec4 GetTrailColor(const ParticleSystemComponent& ps, const glm::vec4& color, float u)
        {
            const glm::vec4 c = ps.Trails.Gradient.Evaluate(std::clamp(u, 0.0f, 1.0f));
            return ps.Trails.InheritColor ? c * color : c;
        }

        int GetGridFrameCount(const ParticleSystemComponent& ps)
        {
            const auto& sheet = ps.TextureSheet;
            const int cells = std::max(1, sheet.Columns) * std::max(1, sheet.Rows);
            return sheet.FrameCount > 0 ? std::min(sheet.FrameCount, cells) : cells;
        }

        int GetSheetFrame(const ParticleSystemComponent& ps, const Particle& p, int frameCount)
        {
            const auto& sheet = ps.TextureSheet;
            if (!sheet.Enabled || frameCount <= 1)
                return 0;
            // One random row of the grid: its cells are the frames.
            const bool row = sheet.RandomRow && sheet.Source == ParticleSystemComponent::TextureSheetModule::FrameSource::Grid;
            const int columns = std::max(1, sheet.Columns);
            const int frames = row ? std::min(columns, frameCount) : frameCount;
            float position;
            if (sheet.Mode == ParticleSystemComponent::TextureSheetModule::TimeMode::FrameRate)
                position = std::max(0.0f, p.GetAge()) * std::max(0.0f, sheet.FrameRate);
            else
                position = std::clamp(p.GetAge01(), 0.0f, 0.99999f) * std::max(0.0f, sheet.Cycles) * frames;
            int frame = (int)std::floor(position) + sheet.StartFrame;
            if (sheet.RandomStartFrame)
                frame += (int)(SeedRandom(p.Seed, 3) * frames);
            frame %= frames;
            if (frame < 0)
                frame += frames;
            if (row)
            {
                const int rows = std::max(1, (frameCount + columns - 1) / columns);
                const int r = std::min(rows - 1, (int)(SeedRandom(p.Seed, 4) * rows));
                frame = std::min(frameCount - 1, r * columns + frame);
            }
            return frame;
        }

    }

}
