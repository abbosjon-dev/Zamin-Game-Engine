#include "Zamin/Math/Random.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace Zamin {

    static uint64_t NonDeterministicSeed()
    {
        uint64_t seed = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        try
        {
            std::random_device device;
            seed ^= ((uint64_t)device() << 32) ^ (uint64_t)device();
        }
        catch (...)
        {
            // No entropy source: the clock alone is good enough for gameplay.
        }
        // splitmix64 finalizer: spreads similar clock values over all bits.
        seed += 0x9e3779b97f4a7c15ULL;
        seed = (seed ^ (seed >> 30)) * 0xbf58476d1ce4e5b9ULL;
        seed = (seed ^ (seed >> 27)) * 0x94d049bb133111ebULL;
        return seed ^ (seed >> 31);
    }

    RandomGenerator::RandomGenerator() { Randomize(); }

    RandomGenerator::RandomGenerator(uint64_t seed, uint64_t stream) { SetSeed(seed, stream); }

    void RandomGenerator::SetSeed(uint64_t seed, uint64_t stream)
    {
        // pcg32_srandom_r
        m_Seed = seed;
        m_State = 0;
        m_Increment = (stream << 1u) | 1u;
        NextUInt();
        m_State += seed;
        NextUInt();
        m_HasSpareGaussian = false;
        m_SpareGaussian = 0.0f;
    }

    void RandomGenerator::Randomize() { SetSeed(NonDeterministicSeed()); }

    uint32_t RandomGenerator::NextUInt()
    {
        // pcg32_random_r (XSH RR)
        uint64_t old = m_State;
        m_State = old * 6364136223846793005ULL + m_Increment;
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    uint64_t RandomGenerator::NextUInt64()
    {
        uint64_t hi = NextUInt();
        return (hi << 32) | NextUInt();
    }

    float RandomGenerator::Float()
    {
        // 24 random bits -> every value is exactly representable, result < 1.
        return (float)(NextUInt() >> 8) * (1.0f / 16777216.0f);
    }

    float RandomGenerator::Range(float min, float max)
    {
        float v = min + (max - min) * Float();
        // Guard against rounding past the ends (and accept swapped arguments).
        return std::clamp(v, std::min(min, max), std::max(min, max));
    }

    // Unbiased integer in [0, range) (Lemire's multiply-shift with rejection).
    static uint32_t Bounded(RandomGenerator& rng, uint32_t range)
    {
        uint64_t m = (uint64_t)rng.NextUInt() * range;
        uint32_t low = (uint32_t)m;
        if (low < range)
        {
            uint32_t threshold = (0u - range) % range;
            while (low < threshold)
            {
                m = (uint64_t)rng.NextUInt() * range;
                low = (uint32_t)m;
            }
        }
        return (uint32_t)(m >> 32);
    }

    int RandomGenerator::Range(int min, int max)
    {
        if (min > max) std::swap(min, max);
        uint64_t span = (uint64_t)((int64_t)max - (int64_t)min) + 1u;
        if (span > 0xffffffffULL) // the whole int range
            return (int)NextUInt();
        return (int)((int64_t)min + (int64_t)Bounded(*this, (uint32_t)span));
    }

    size_t RandomGenerator::Index(size_t count)
    {
        if (count <= 1)
            return 0;
        if (count <= 0xffffffffULL)
            return Bounded(*this, (uint32_t)count);
        // Huge containers: 64-bit rejection sampling.
        uint64_t limit = UINT64_MAX - UINT64_MAX % count;
        uint64_t v;
        do v = NextUInt64(); while (v >= limit);
        return (size_t)(v % count);
    }

    bool RandomGenerator::Bool(float probability) { return Float() < probability; }

    float RandomGenerator::Angle() { return Float() * glm::two_pi<float>(); }

    float RandomGenerator::Gaussian(float mean, float standardDeviation)
    {
        if (m_HasSpareGaussian)
        {
            m_HasSpareGaussian = false;
            return mean + standardDeviation * m_SpareGaussian;
        }
        // Box-Muller: u1 in (0, 1] so the logarithm is finite.
        float u1 = 1.0f - Float();
        float u2 = Float();
        float r = std::sqrt(-2.0f * std::log(u1));
        float a = glm::two_pi<float>() * u2;
        m_SpareGaussian = r * std::sin(a);
        m_HasSpareGaussian = true;
        return mean + standardDeviation * r * std::cos(a);
    }

    glm::vec2 RandomGenerator::InsideUnitCircle()
    {
        // sqrt keeps the density uniform over the area.
        float r = std::sqrt(Float());
        float a = Angle();
        return { r * std::cos(a), r * std::sin(a) };
    }

    glm::vec2 RandomGenerator::OnUnitCircle()
    {
        float a = Angle();
        return { std::cos(a), std::sin(a) };
    }

    glm::vec3 RandomGenerator::OnUnitSphere()
    {
        // Archimedes: z is uniform on [-1, 1] for a uniform point on the sphere.
        float z = 1.0f - 2.0f * Float();
        float a = Angle();
        float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return { s * std::cos(a), s * std::sin(a), z };
    }

    glm::vec3 RandomGenerator::InsideUnitSphere()
    {
        glm::vec3 dir = OnUnitSphere();
        return dir * std::cbrt(Float());
    }

    glm::vec3 RandomGenerator::InsideBox(const glm::vec3& min, const glm::vec3& max)
    {
        float x = Range(min.x, max.x);
        float y = Range(min.y, max.y);
        float z = Range(min.z, max.z);
        return { x, y, z };
    }

    glm::quat RandomGenerator::Rotation()
    {
        // Shoemake, "Uniform random rotations" (Graphics Gems III).
        float u1 = Float(), u2 = Float(), u3 = Float();
        float a = std::sqrt(1.0f - u1), b = std::sqrt(u1);
        float t1 = glm::two_pi<float>() * u2, t2 = glm::two_pi<float>() * u3;
        return glm::normalize(glm::quat(b * std::cos(t2), a * std::sin(t1), a * std::cos(t1), b * std::sin(t2)));
    }

    glm::vec4 RandomGenerator::ColorHSV(float hueMin, float hueMax, float saturationMin, float saturationMax, float valueMin, float valueMax)
    {
        float h = Range(hueMin, hueMax);
        float s = std::clamp(Range(saturationMin, saturationMax), 0.0f, 1.0f);
        float v = std::clamp(Range(valueMin, valueMax), 0.0f, 1.0f);
        h -= std::floor(h); // wrap into [0, 1)
        float h6 = h * 6.0f;
        int sector = std::min(5, (int)h6);
        float f = h6 - (float)sector;
        float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
        switch (sector)
        {
            case 0:  return { v, t, p, 1.0f };
            case 1:  return { q, v, p, 1.0f };
            case 2:  return { p, v, t, 1.0f };
            case 3:  return { p, q, v, 1.0f };
            case 4:  return { t, p, v, 1.0f };
            default: return { v, p, q, 1.0f };
        }
    }

    int RandomGenerator::WeightedIndex(std::span<const float> weights)
    {
        float total = 0.0f;
        for (float w : weights)
            if (w > 0.0f) total += w;
        if (total <= 0.0f)
            return -1;
        float r = Float() * total;
        int last = -1;
        for (size_t i = 0; i < weights.size(); i++)
        {
            if (weights[i] <= 0.0f) continue;
            last = (int)i;
            if (r < weights[i]) return last;
            r -= weights[i];
        }
        return last; // rounding: the last positive weight
    }

    RandomGenerator& Random::Get()
    {
        static RandomGenerator s_Generator;
        return s_Generator;
    }

}
