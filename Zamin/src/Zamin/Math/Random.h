#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace Zamin {

    // Small, fast pseudo random generator (PCG32, O'Neill 2014).
    // The same seed produces the same sequence on every platform and compiler
    // (unlike std::uniform_*_distribution), so seeded levels / replays are reproducible.
    //
    //   RandomGenerator rng(1234);
    //   float x = rng.Range(-5.0f, 5.0f);
    //   int roll = rng.Int(1, 6);            // both ends inclusive
    //   glm::vec3 dir = rng.OnUnitSphere();
    class RandomGenerator
    {
    public:
        // Seeded from std::random_device (a different sequence every run).
        RandomGenerator();
        explicit RandomGenerator(uint64_t seed, uint64_t stream = DefaultStream);

        // Restarts the sequence. `stream` selects one of 2^63 independent sequences.
        void SetSeed(uint64_t seed, uint64_t stream = DefaultStream);
        uint64_t GetSeed() const { return m_Seed; }
        // Picks a fresh non-deterministic seed.
        void Randomize();

        // ---- raw bits
        uint32_t NextUInt();
        uint64_t NextUInt64();

        // ---- numbers
        float Float();                          // [0, 1)
        float Range(float min, float max);      // [min, max]
        int Range(int min, int max);            // [min, max], both ends inclusive
        int Int(int min, int max) { return Range(min, max); }
        // Index into a container of `count` elements: [0, count). Returns 0 when count == 0.
        size_t Index(size_t count);
        // true with the given probability (0..1).
        bool Bool(float probability = 0.5f);
        // -1 or +1
        float Sign() { return Bool() ? 1.0f : -1.0f; }
        // Angle in radians [0, 2*pi).
        float Angle();
        // Normal (Gaussian) distribution.
        float Gaussian(float mean = 0.0f, float standardDeviation = 1.0f);

        // ---- geometry (uniformly distributed)
        glm::vec2 InsideUnitCircle();           // |v| <= 1
        glm::vec2 InsideCircle(float radius) { return InsideUnitCircle() * radius; }
        glm::vec2 OnUnitCircle();               // |v| == 1
        glm::vec2 OnCircle(float radius) { return OnUnitCircle() * radius; }
        glm::vec3 InsideUnitSphere();           // |v| <= 1
        glm::vec3 InsideSphere(float radius) { return InsideUnitSphere() * radius; }
        glm::vec3 OnUnitSphere();               // |v| == 1
        glm::vec3 OnSphere(float radius) { return OnUnitSphere() * radius; }
        glm::vec3 InsideBox(const glm::vec3& min, const glm::vec3& max);
        glm::quat Rotation();                   // uniform random orientation
        // Random opaque color from HSV ranges (hue 0..1 wraps around).
        glm::vec4 ColorHSV(float hueMin = 0.0f, float hueMax = 1.0f, float saturationMin = 0.6f, float saturationMax = 1.0f,
                           float valueMin = 0.7f, float valueMax = 1.0f);

        // Index chosen proportionally to non-negative weights (-1 when all weights are <= 0).
        int WeightedIndex(std::span<const float> weights);

        // ---- containers
        template<typename T>
        T& Pick(std::vector<T>& items) { return items[Index(items.size())]; }
        template<typename T>
        const T& Pick(const std::vector<T>& items) { return items[Index(items.size())]; }

        // Fisher-Yates shuffle (deterministic for a given seed).
        template<typename RandomIt>
        void Shuffle(RandomIt first, RandomIt last)
        {
            auto n = std::distance(first, last);
            for (auto i = n - 1; i > 0; i--)
            {
                auto j = (decltype(i))Index((size_t)i + 1);
                using std::swap;
                swap(first[i], first[j]);
            }
        }
        template<typename T>
        void Shuffle(std::vector<T>& items) { Shuffle(items.begin(), items.end()); }

        static constexpr uint64_t DefaultStream = 0xda3e39cb94b95bdbULL;

    private:
        uint64_t m_State = 0, m_Increment = 1;
        uint64_t m_Seed = 0;
        // Second value of the Box-Muller pair.
        bool m_HasSpareGaussian = false;
        float m_SpareGaussian = 0.0f;
    };

    // Global generator for gameplay scripts (main thread only).
    //
    //   Random::SetSeed(42);                        // optional: reproducible run
    //   float speed = Random::Range(2.0f, 4.0f);
    //   if (Random::Bool(0.1f)) DropLoot();
    //   glm::vec2 p = Random::InsideCircle(3.0f);
    class Random
    {
    public:
        static void SetSeed(uint64_t seed) { Get().SetSeed(seed); }
        static uint64_t GetSeed() { return Get().GetSeed(); }
        static void Randomize() { Get().Randomize(); }

        static uint32_t UInt() { return Get().NextUInt(); }
        static float Float() { return Get().Float(); }
        static float Range(float min, float max) { return Get().Range(min, max); }
        static int Range(int min, int max) { return Get().Range(min, max); }
        static int Int(int min, int max) { return Get().Range(min, max); }
        static size_t Index(size_t count) { return Get().Index(count); }
        static bool Bool(float probability = 0.5f) { return Get().Bool(probability); }
        static float Sign() { return Get().Sign(); }
        static float Angle() { return Get().Angle(); }
        static float Gaussian(float mean = 0.0f, float standardDeviation = 1.0f) { return Get().Gaussian(mean, standardDeviation); }

        static glm::vec2 InsideUnitCircle() { return Get().InsideUnitCircle(); }
        static glm::vec2 InsideCircle(float radius) { return Get().InsideCircle(radius); }
        static glm::vec2 OnUnitCircle() { return Get().OnUnitCircle(); }
        static glm::vec2 OnCircle(float radius) { return Get().OnCircle(radius); }
        static glm::vec3 InsideUnitSphere() { return Get().InsideUnitSphere(); }
        static glm::vec3 InsideSphere(float radius) { return Get().InsideSphere(radius); }
        static glm::vec3 OnUnitSphere() { return Get().OnUnitSphere(); }
        static glm::vec3 OnSphere(float radius) { return Get().OnSphere(radius); }
        static glm::vec3 InsideBox(const glm::vec3& min, const glm::vec3& max) { return Get().InsideBox(min, max); }
        static glm::quat Rotation() { return Get().Rotation(); }
        static glm::vec4 ColorHSV(float hueMin = 0.0f, float hueMax = 1.0f, float saturationMin = 0.6f, float saturationMax = 1.0f,
                                  float valueMin = 0.7f, float valueMax = 1.0f)
        {
            return Get().ColorHSV(hueMin, hueMax, saturationMin, saturationMax, valueMin, valueMax);
        }
        static int WeightedIndex(std::span<const float> weights) { return Get().WeightedIndex(weights); }

        template<typename T> static T& Pick(std::vector<T>& items) { return Get().Pick(items); }
        template<typename T> static const T& Pick(const std::vector<T>& items) { return Get().Pick(items); }
        template<typename T> static void Shuffle(std::vector<T>& items) { Get().Shuffle(items); }

        static RandomGenerator& Get();
    };

}
