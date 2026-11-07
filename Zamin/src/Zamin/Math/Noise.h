#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

// Coherent noise: smooth pseudo-random functions of a point (terrain heights, wobbling lights,
// camera shake, particle turbulence, procedural textures).
//
// Every function is deterministic - the same point and seed give the same value on every run and
// machine - keeps no state and may be called from any thread. The seed acts as a seeded
// permutation of the lattice: the corners are hashed together with it (Hash), so each seed is an
// unrelated field that never repeats (unlike the classic 256-entry permutation table, which tiles
// every 256 units). The Periodic variants tile on purpose (seamless textures).
//
//   float h = Noise::Fractal(glm::vec2(x, z) * 0.05f, { .Octaves = 5, .Seed = 42 }); // -1..1
//   float m = Noise::Ridged(glm::vec2(x, z) * 0.02f, { .Octaves = 6 });             // 0..1, ridges
namespace Zamin::Noise {

    // ------------------------------------------------------------------ hashing
    // lowbias32 (Chris Wellons): a well mixed 32-bit integer hash.
    constexpr uint32_t Hash(uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352du;
        x ^= x >> 15; x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }
    // A lattice corner's hash for a seed.
    uint32_t Hash(int x, uint32_t seed);
    uint32_t Hash(int x, int y, uint32_t seed);
    uint32_t Hash(int x, int y, int z, uint32_t seed);
    // A hash as a number: 0..1 (never 1) / -1..1.
    constexpr float HashTo01(uint32_t h) { return (float)(h & 0xFFFFFFu) / (float)0x1000000; }
    constexpr float HashToSigned(uint32_t h) { return HashTo01(h) * 2.0f - 1.0f; }

    // ------------------------------------------------------------------ basic noises
    // Gradient noise (Ken Perlin's improved noise, quintic fade): about -1..1, 0 at whole
    // coordinates; smooth (its slope too) everywhere.
    float Perlin(float x, uint32_t seed = 0);
    float Perlin(const glm::vec2& p, uint32_t seed = 0);
    float Perlin(const glm::vec3& p, uint32_t seed = 0);
    // Three unrelated Perlin noises of one point (one per seed; the lattice cell is found once):
    // vector fields such as the particles' turbulence. Components whose axis is false are 0.
    glm::vec3 PerlinVector(const glm::vec3& p, const glm::uvec3& seeds, const glm::bvec3& axes = glm::bvec3(true));
    // Perlin noise that repeats every `period` units (whole numbers >= 1 per axis): a texture made
    // over 0..period tiles without seams.
    float PerlinPeriodic(const glm::vec2& p, const glm::ivec2& period, uint32_t seed = 0);

    // Simplex noise (Stefan Gustavson's): -1..1, fewer lattice corners than Perlin's (cheaper in
    // 3D) and no square / cubic grid artefacts; smooth everywhere.
    float Simplex(const glm::vec2& p, uint32_t seed = 0);
    float Simplex(const glm::vec3& p, uint32_t seed = 0);

    // Value noise: random values at whole coordinates, smoothly (quintic) blended: -1..1. Cheap;
    // blobbier than gradient noise. The 1D one drives the camera shake.
    float Value(float x, uint32_t seed = 0);
    float Value(const glm::vec2& p, uint32_t seed = 0);
    float Value(const glm::vec3& p, uint32_t seed = 0);

    // -1..1 -> 0..1.
    constexpr float To01(float n) { return n * 0.5f + 0.5f; }

    // ------------------------------------------------------------------ fractals
    enum class NoiseType : uint8_t { Perlin = 0, Simplex, Value };
    constexpr int NoiseTypeCount = 3;
    const char* NoiseTypeToString(NoiseType type);
    std::optional<NoiseType> NoiseTypeFromString(std::string_view text);

    // Layers ("octaves") of a noise added together: each one Lacunarity times the frequency and
    // Gain times the strength of the one before (Gain 0.5, Lacunarity 2: the classic fBm).
    struct FractalSettings
    {
        NoiseType Type = NoiseType::Perlin;
        int Octaves = 4;           // 1..16
        float Frequency = 1.0f;    // of the first octave (features per unit)
        float Lacunarity = 2.0f;   // frequency multiplier per octave
        float Gain = 0.5f;         // strength multiplier per octave ("persistence")
        uint32_t Seed = 0;
        bool operator==(const FractalSettings&) const = default;
    };
    // An octave's own seed (every octave is an unrelated field).
    uint32_t OctaveSeed(uint32_t seed, int octave);

    // Fractal Brownian motion: the octaves summed, divided by their total strength: -1..1.
    float Fractal(float x, const FractalSettings& settings = {});
    float Fractal(const glm::vec2& p, const FractalSettings& settings = {});
    float Fractal(const glm::vec3& p, const FractalSettings& settings = {});
    // Three unrelated fBm fields of a point (Perlin; seeds from the settings' seed): -1..1 per axis.
    glm::vec3 FractalVector(const glm::vec3& p, const FractalSettings& settings = {}, const glm::bvec3& axes = glm::bvec3(true));
    // Ridged multifractal (F. K. Musgrave): 0..1 with sharp crests where the noise crosses 0, each
    // octave weighted by the one before (mountain ranges, veins, lightning).
    float Ridged(const glm::vec2& p, const FractalSettings& settings = {});
    float Ridged(const glm::vec3& p, const FractalSettings& settings = {});
    // Turbulence: the octaves' absolute values summed: 0..1, billowy (clouds, smoke, fire).
    float Turbulence(const glm::vec2& p, const FractalSettings& settings = {});
    float Turbulence(const glm::vec3& p, const FractalSettings& settings = {});

    // One octave of the chosen type.
    float Sample(NoiseType type, const glm::vec2& p, uint32_t seed);
    float Sample(NoiseType type, const glm::vec3& p, uint32_t seed);

}
