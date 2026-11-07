#include "Zamin/Math/Noise.h"

#include <algorithm>
#include <cmath>

namespace Zamin::Noise {

    // ------------------------------------------------------------------ hashing
    uint32_t Hash(int x, uint32_t seed)
    {
        return Hash((uint32_t)x ^ Hash(seed));
    }

    uint32_t Hash(int x, int y, uint32_t seed)
    {
        return Hash((uint32_t)x + Hash((uint32_t)y ^ Hash(seed)));
    }

    uint32_t Hash(int x, int y, int z, uint32_t seed)
    {
        return Hash((uint32_t)x + Hash((uint32_t)y + Hash((uint32_t)z ^ Hash(seed))));
    }

    namespace {

        // The lattice cell of a coordinate: its corner (wraps far away instead of overflowing) and the
        // place in it, 0..1.
        struct Axis
        {
            int Cell;
            float Fraction;
        };

        inline Axis Split(float v)
        {
            const float f = std::floor(v);
            const double clamped = std::clamp((double)f, -9.0e18, 9.0e18);
            return { (int)(int32_t)(int64_t)clamped, v - f };
        }

        // Quintic fade: the field and its slope are continuous across the lattice.
        inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
        inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        inline bool Finite(float v) { return std::isfinite(v); }
        inline bool Finite(const glm::vec2& p) { return std::isfinite(p.x) && std::isfinite(p.y); }
        inline bool Finite(const glm::vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

        // 1D gradient: a slope of -1..1 (Perlin 1D is scaled to about -1..1 by its caller).
        inline float Gradient1(uint32_t hash, float x) { return HashToSigned(hash) * x; }

        // 2D: the 4 diagonals and the 4 axes.
        inline float Gradient2(uint32_t hash, float x, float y)
        {
            switch (hash & 7u)
            {
                case 0: return x + y;
                case 1: return -x + y;
                case 2: return x - y;
                case 3: return -x - y;
                case 4: return x;
                case 5: return -x;
                case 6: return y;
                default: return -y;
            }
        }

        // 3D: Perlin's improved set, the 12 edge midpoints of a cube (4 of them twice: 16 entries).
        inline float Gradient3(uint32_t hash, float x, float y, float z)
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

        // Value noise's corner values: -1..1 (both ends reachable).
        inline float CornerValue(uint32_t hash) { return (float)(hash & 0xFFFFFFu) / (float)0xFFFFFF * 2.0f - 1.0f; }

        // A 3D point's lattice cell, its place there and the fade weights: shared by the noises of
        // one point (PerlinVector).
        struct Cell3
        {
            int X, Y, Z;
            float Fx, Fy, Fz;
            float Ux, Uy, Uz;
        };

        inline Cell3 MakeCell(const glm::vec3& p)
        {
            const Axis ax = Split(p.x), ay = Split(p.y), az = Split(p.z);
            return { ax.Cell, ay.Cell, az.Cell, ax.Fraction, ay.Fraction, az.Fraction,
                     Fade(ax.Fraction), Fade(ay.Fraction), Fade(az.Fraction) };
        }

        float Perlin3At(const Cell3& c, uint32_t seed)
        {
            // The corners' hashes, z first: Hash(x + Hash(y + Hash(z ^ Hash(seed)))).
            const uint32_t hs = Hash(seed);
            const uint32_t hz0 = Hash((uint32_t)c.Z ^ hs), hz1 = Hash((uint32_t)(c.Z + 1) ^ hs);
            const uint32_t h00 = Hash((uint32_t)c.Y + hz0), h10 = Hash((uint32_t)(c.Y + 1) + hz0);
            const uint32_t h01 = Hash((uint32_t)c.Y + hz1), h11 = Hash((uint32_t)(c.Y + 1) + hz1);
            const uint32_t x0 = (uint32_t)c.X, x1 = (uint32_t)(c.X + 1);
            const float fx = c.Fx, fy = c.Fy, fz = c.Fz;
            const float n000 = Gradient3(Hash(x0 + h00), fx, fy, fz), n100 = Gradient3(Hash(x1 + h00), fx - 1.0f, fy, fz);
            const float n010 = Gradient3(Hash(x0 + h10), fx, fy - 1.0f, fz), n110 = Gradient3(Hash(x1 + h10), fx - 1.0f, fy - 1.0f, fz);
            const float n001 = Gradient3(Hash(x0 + h01), fx, fy, fz - 1.0f), n101 = Gradient3(Hash(x1 + h01), fx - 1.0f, fy, fz - 1.0f);
            const float n011 = Gradient3(Hash(x0 + h11), fx, fy - 1.0f, fz - 1.0f), n111 = Gradient3(Hash(x1 + h11), fx - 1.0f, fy - 1.0f, fz - 1.0f);
            const float x00 = n000 + (n100 - n000) * c.Ux, x10 = n010 + (n110 - n010) * c.Ux;
            const float x01 = n001 + (n101 - n001) * c.Ux, x11 = n011 + (n111 - n011) * c.Ux;
            const float y0 = x00 + (x10 - x00) * c.Uy, y1 = x01 + (x11 - x01) * c.Uy;
            return std::clamp(y0 + (y1 - y0) * c.Uz, -1.0f, 1.0f);
        }

        // 2D Perlin with the lattice corners wrapped by `period` (0: no wrapping).
        float Perlin2(const glm::vec2& p, uint32_t seed, int periodX, int periodY)
        {
            const Axis ax = Split(p.x), ay = Split(p.y);
            int x0 = ax.Cell, y0 = ay.Cell, x1 = x0 + 1, y1 = y0 + 1;
            if (periodX > 0)
            {
                x0 = ((x0 % periodX) + periodX) % periodX;
                x1 = ((x1 % periodX) + periodX) % periodX;
            }
            if (periodY > 0)
            {
                y0 = ((y0 % periodY) + periodY) % periodY;
                y1 = ((y1 % periodY) + periodY) % periodY;
            }
            const float fx = ax.Fraction, fy = ay.Fraction;
            const float u = Fade(fx), v = Fade(fy);
            const float a = Gradient2(Hash(x0, y0, seed), fx, fy);
            const float b = Gradient2(Hash(x1, y0, seed), fx - 1.0f, fy);
            const float c = Gradient2(Hash(x0, y1, seed), fx, fy - 1.0f);
            const float d = Gradient2(Hash(x1, y1, seed), fx - 1.0f, fy - 1.0f);
            return std::clamp(Lerp(Lerp(a, b, u), Lerp(c, d, u), v), -1.0f, 1.0f);
        }

    }

    // ------------------------------------------------------------------ Perlin
    float Perlin(float x, uint32_t seed)
    {
        if (!Finite(x))
            return 0.0f;
        const Axis a = Split(x);
        const float g0 = Gradient1(Hash(a.Cell, seed), a.Fraction);
        const float g1 = Gradient1(Hash(a.Cell + 1, seed), a.Fraction - 1.0f);
        // The slopes are -1..1: the curve reaches about 0.5 between corners.
        return std::clamp(Lerp(g0, g1, Fade(a.Fraction)) * 2.0f, -1.0f, 1.0f);
    }

    float Perlin(const glm::vec2& p, uint32_t seed)
    {
        return Finite(p) ? Perlin2(p, seed, 0, 0) : 0.0f;
    }

    float Perlin(const glm::vec3& p, uint32_t seed)
    {
        return Finite(p) ? Perlin3At(MakeCell(p), seed) : 0.0f;
    }

    glm::vec3 PerlinVector(const glm::vec3& p, const glm::uvec3& seeds, const glm::bvec3& axes)
    {
        if (!Finite(p))
            return glm::vec3(0.0f);
        const Cell3 cell = MakeCell(p);
        glm::vec3 out(0.0f);
        for (int axis = 0; axis < 3; axis++)
            if (axes[axis])
                out[axis] = Perlin3At(cell, seeds[axis]);
        return out;
    }

    float PerlinPeriodic(const glm::vec2& p, const glm::ivec2& period, uint32_t seed)
    {
        return Finite(p) ? Perlin2(p, seed, std::max(period.x, 1), std::max(period.y, 1)) : 0.0f;
    }

    // ------------------------------------------------------------------ simplex
    float Simplex(const glm::vec2& p, uint32_t seed)
    {
        if (!Finite(p))
            return 0.0f;
        // Skew the plane into a grid of squares split into two triangles; find the triangle.
        constexpr float F2 = 0.36602540378f; // (sqrt(3) - 1) / 2
        constexpr float G2 = 0.21132486540f; // (3 - sqrt(3)) / 6
        const float s = (p.x + p.y) * F2;
        const Axis ai = Split(p.x + s), aj = Split(p.y + s);
        const int i = ai.Cell, j = aj.Cell;
        const float t = (float)(i + j) * G2;
        const float x0 = p.x - ((float)i - t), y0 = p.y - ((float)j - t);
        const int i1 = x0 > y0 ? 1 : 0, j1 = 1 - i1;
        const float x1 = x0 - (float)i1 + G2, y1 = y0 - (float)j1 + G2;
        const float x2 = x0 - 1.0f + 2.0f * G2, y2 = y0 - 1.0f + 2.0f * G2;
        auto corner = [seed](float x, float y, int cx, int cy) {
            float k = 0.5f - x * x - y * y;
            if (k <= 0.0f)
                return 0.0f;
            k *= k;
            return k * k * Gradient2(Hash(cx, cy, seed), x, y);
        };
        const float n = corner(x0, y0, i, j) + corner(x1, y1, i + i1, j + j1) + corner(x2, y2, i + 1, j + 1);
        return std::clamp(n * 70.0f, -1.0f, 1.0f);
    }

    float Simplex(const glm::vec3& p, uint32_t seed)
    {
        if (!Finite(p))
            return 0.0f;
        constexpr float F3 = 1.0f / 3.0f, G3 = 1.0f / 6.0f;
        const float s = (p.x + p.y + p.z) * F3;
        const int i = Split(p.x + s).Cell, j = Split(p.y + s).Cell, k = Split(p.z + s).Cell;
        const float t = (float)(i + j + k) * G3;
        const float x0 = p.x - ((float)i - t), y0 = p.y - ((float)j - t), z0 = p.z - ((float)k - t);
        // Which of the six tetrahedra of the skewed cube the point is in.
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0)
        {
            if (y0 >= z0)      { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
            else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
            else               { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
        }
        else
        {
            if (y0 < z0)       { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
            else if (x0 < z0)  { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
            else               { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        }
        auto corner = [seed](float x, float y, float z, int cx, int cy, int cz) {
            // Radius 0.5 (not Gustavson's 0.6): a corner's influence ends before the next
            // simplex, so the field has no seams.
            float q = 0.5f - x * x - y * y - z * z;
            if (q <= 0.0f)
                return 0.0f;
            q *= q;
            return q * q * Gradient3(Hash(cx, cy, cz, seed), x, y, z);
        };
        const float n = corner(x0, y0, z0, i, j, k) +
                        corner(x0 - (float)i1 + G3, y0 - (float)j1 + G3, z0 - (float)k1 + G3, i + i1, j + j1, k + k1) +
                        corner(x0 - (float)i2 + 2.0f * G3, y0 - (float)j2 + 2.0f * G3, z0 - (float)k2 + 2.0f * G3, i + i2, j + j2, k + k2) +
                        corner(x0 - 1.0f + 3.0f * G3, y0 - 1.0f + 3.0f * G3, z0 - 1.0f + 3.0f * G3, i + 1, j + 1, k + 1);
        return std::clamp(n * 76.0f, -1.0f, 1.0f);
    }

    // ------------------------------------------------------------------ value
    float Value(float x, uint32_t seed)
    {
        if (!Finite(x))
            return 0.0f;
        const Axis a = Split(x);
        // The camera shake's curve since M09: corner i is Hash(seed ^ i * golden ratio).
        const float v0 = CornerValue(Hash(seed ^ ((uint32_t)a.Cell * 0x9E3779B9u)));
        const float v1 = CornerValue(Hash(seed ^ ((uint32_t)(a.Cell + 1) * 0x9E3779B9u)));
        return v0 + (v1 - v0) * Fade(a.Fraction);
    }

    float Value(const glm::vec2& p, uint32_t seed)
    {
        if (!Finite(p))
            return 0.0f;
        const Axis ax = Split(p.x), ay = Split(p.y);
        const float u = Fade(ax.Fraction), v = Fade(ay.Fraction);
        const int x = ax.Cell, y = ay.Cell;
        const float a = Lerp(CornerValue(Hash(x, y, seed)), CornerValue(Hash(x + 1, y, seed)), u);
        const float b = Lerp(CornerValue(Hash(x, y + 1, seed)), CornerValue(Hash(x + 1, y + 1, seed)), u);
        return Lerp(a, b, v);
    }

    float Value(const glm::vec3& p, uint32_t seed)
    {
        if (!Finite(p))
            return 0.0f;
        const Cell3 c = MakeCell(p);
        auto at = [&](int dx, int dy, int dz) { return CornerValue(Hash(c.X + dx, c.Y + dy, c.Z + dz, seed)); };
        const float x00 = Lerp(at(0, 0, 0), at(1, 0, 0), c.Ux), x10 = Lerp(at(0, 1, 0), at(1, 1, 0), c.Ux);
        const float x01 = Lerp(at(0, 0, 1), at(1, 0, 1), c.Ux), x11 = Lerp(at(0, 1, 1), at(1, 1, 1), c.Ux);
        return Lerp(Lerp(x00, x10, c.Uy), Lerp(x01, x11, c.Uy), c.Uz);
    }

    // ------------------------------------------------------------------ fractals
    const char* NoiseTypeToString(NoiseType type)
    {
        switch (type)
        {
            case NoiseType::Simplex: return "Simplex";
            case NoiseType::Value: return "Value";
            default: return "Perlin";
        }
    }

    std::optional<NoiseType> NoiseTypeFromString(std::string_view text)
    {
        if (text == "Perlin") return NoiseType::Perlin;
        if (text == "Simplex") return NoiseType::Simplex;
        if (text == "Value") return NoiseType::Value;
        return std::nullopt;
    }

    uint32_t OctaveSeed(uint32_t seed, int octave)
    {
        return Hash(seed * 0x632BE5ABu + (uint32_t)octave * 0x85EBCA6Bu);
    }

    float Sample(NoiseType type, const glm::vec2& p, uint32_t seed)
    {
        switch (type)
        {
            case NoiseType::Simplex: return Simplex(p, seed);
            case NoiseType::Value: return Value(p, seed);
            default: return Perlin(p, seed);
        }
    }

    float Sample(NoiseType type, const glm::vec3& p, uint32_t seed)
    {
        switch (type)
        {
            case NoiseType::Simplex: return Simplex(p, seed);
            case NoiseType::Value: return Value(p, seed);
            default: return Perlin(p, seed);
        }
    }

    namespace {

        inline float Sample1(NoiseType type, float x, uint32_t seed)
        {
            // No 1D simplex: Perlin's is the same thing in one dimension.
            return type == NoiseType::Value ? Value(x, seed) : Perlin(x, seed);
        }

        // The octave loop: `layer(p * frequency, seed)` per octave, its strength-weighted sum and
        // the total strength.
        template<typename P, typename F>
        float Octaves(const P& p, const FractalSettings& s, F&& layer)
        {
            const int octaves = std::clamp(s.Octaves, 1, 16);
            float sum = 0.0f, amplitude = 1.0f, total = 0.0f, frequency = s.Frequency;
            for (int o = 0; o < octaves; o++)
            {
                sum += layer(p * frequency, OctaveSeed(s.Seed, o)) * amplitude;
                total += amplitude;
                amplitude *= s.Gain;
                frequency *= s.Lacunarity;
            }
            return total > 0.0f ? sum / total : 0.0f;
        }

        template<typename P>
        float RidgedImpl(const P& p, const FractalSettings& s)
        {
            const int octaves = std::clamp(s.Octaves, 1, 16);
            float sum = 0.0f, amplitude = 1.0f, total = 0.0f, frequency = s.Frequency, weight = 1.0f;
            for (int o = 0; o < octaves; o++)
            {
                float n = 1.0f - std::abs(Sample(s.Type, p * frequency, OctaveSeed(s.Seed, o)));
                n *= n;
                n *= weight;
                // The next octave shows where this one is high: detail gathers along the crests.
                weight = std::clamp(n * 2.0f, 0.0f, 1.0f);
                sum += n * amplitude;
                total += amplitude;
                amplitude *= s.Gain;
                frequency *= s.Lacunarity;
            }
            return total > 0.0f ? std::clamp(sum / total, 0.0f, 1.0f) : 0.0f;
        }

    }

    float Fractal(float x, const FractalSettings& s)
    {
        return Octaves(x, s, [&](float q, uint32_t seed) { return Sample1(s.Type, q, seed); });
    }

    float Fractal(const glm::vec2& p, const FractalSettings& s)
    {
        return Octaves(p, s, [&](const glm::vec2& q, uint32_t seed) { return Sample(s.Type, q, seed); });
    }

    float Fractal(const glm::vec3& p, const FractalSettings& s)
    {
        return Octaves(p, s, [&](const glm::vec3& q, uint32_t seed) { return Sample(s.Type, q, seed); });
    }

    glm::vec3 FractalVector(const glm::vec3& p, const FractalSettings& s, const glm::bvec3& axes)
    {
        const int octaves = std::clamp(s.Octaves, 1, 16);
        glm::vec3 sum(0.0f);
        float amplitude = 1.0f, total = 0.0f, frequency = s.Frequency;
        for (int o = 0; o < octaves; o++)
        {
            const uint32_t seed = OctaveSeed(s.Seed, o);
            const glm::uvec3 seeds(seed, seed + 0x27D4EB2Fu, seed + 2u * 0x27D4EB2Fu);
            const glm::vec3 q = p * frequency;
            glm::vec3 n(0.0f);
            if (s.Type == NoiseType::Perlin)
                n = PerlinVector(q, seeds, axes);
            else
                for (int axis = 0; axis < 3; axis++)
                    if (axes[axis])
                        n[axis] = Sample(s.Type, q, seeds[axis]);
            for (int axis = 0; axis < 3; axis++)
                sum[axis] += n[axis] * amplitude;
            total += amplitude;
            amplitude *= s.Gain;
            frequency *= s.Lacunarity;
        }
        return total > 0.0f ? sum / total : glm::vec3(0.0f);
    }

    float Ridged(const glm::vec2& p, const FractalSettings& s) { return RidgedImpl(p, s); }
    float Ridged(const glm::vec3& p, const FractalSettings& s) { return RidgedImpl(p, s); }

    float Turbulence(const glm::vec2& p, const FractalSettings& s)
    {
        return Octaves(p, s, [&](const glm::vec2& q, uint32_t seed) { return std::abs(Sample(s.Type, q, seed)); });
    }

    float Turbulence(const glm::vec3& p, const FractalSettings& s)
    {
        return Octaves(p, s, [&](const glm::vec3& q, uint32_t seed) { return std::abs(Sample(s.Type, q, seed)); });
    }

}
