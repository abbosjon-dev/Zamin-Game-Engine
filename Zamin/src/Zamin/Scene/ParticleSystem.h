#pragma once

#include "Zamin/Scene/PropertyAnimation.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Particle systems without the scene: the particle, the over-lifetime curves and gradients, and the
// simulation (spawning from the emitter shapes, motion, ageing) and the look of a particle at its
// age (colour, size, flipbook frame). ParticleSystemComponent (Components.h) holds the settings;
// the scene (SceneParticles.cpp) runs it for every emitter and draws it.
namespace Zamin {

    struct ParticleSystemComponent;

    struct Particle
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        float Rotation = 0.0f;
        float AngularVelocity = 0.0f;
        float Life = 0.0f;      // remaining
        float Lifetime = 1.0f;  // total
        uint32_t Seed = 0;      // its own random numbers (gradient / curve blend, start frame): ParticleSimulation::SeedRandom

        // 0 when born, 1 when it dies.
        float GetAge01() const { return Lifetime > 0.0f ? 1.0f - Life / Lifetime : 1.0f; }
        // Seconds since it was born.
        float GetAge() const { return Lifetime - Life; }
    };

    // A colour over 0..1 (a particle's age): stops by time, blended linearly between neighbours
    // (Fixed: a stop's colour holds until the next one). Before the first / after the last stop
    // their colour; without stops white.
    struct ParticleGradient
    {
        struct Stop
        {
            float Time = 0.0f;
            glm::vec4 Color{ 1.0f };
            bool operator==(const Stop&) const = default;
        };
        std::vector<Stop> Stops;
        bool Fixed = false;

        glm::vec4 Evaluate(float t) const;
        // Stops back in time order (stable: equal times keep their order).
        void Sort();
        // A stop at `t` with the colour the gradient has there; returns its index.
        int AddStop(float t);
        bool operator==(const ParticleGradient&) const = default;

        static ParticleGradient TwoStops(const glm::vec4& from, const glm::vec4& to);
    };

    // A number over 0..1 (a particle's age): animation keys (AnimationKey: Time 0..1, Value.x) with
    // the same Step / Linear / Bezier interpolation as the animation clips (PropertyAnimation).
    // Without keys 1.
    struct ParticleCurve
    {
        std::vector<AnimationKey> Keys;

        float Evaluate(float t) const;
        // Keys back in time order, then the automatic tangents (call after editing keys).
        void Sort();
        // The smallest / largest value the curve takes over 0..1 (sampled between the keys: Bezier
        // keys with free tangents may overshoot them).
        float GetMin() const;
        float GetMax() const;
        // A key at `t` with the curve's value there (interpolation of the key before it); returns its index.
        int AddKey(float t);
        bool operator==(const ParticleCurve&) const = default;

        static ParticleCurve Constant(float value);
        static ParticleCurve Linear(float from, float to);
    };

    namespace ParticleSimulation {

        // Xorshift: 0..1 (never 1); advances the state.
        float Random01(uint32_t& state);
        // A random unit vector within `spreadDegrees` of `direction` (180: anywhere).
        glm::vec3 RandomInCone(const glm::vec3& direction, float spreadDegrees, uint32_t& state);
        // A particle's own random number 0..1 for one purpose (`salt`): the same every frame.
        float SeedRandom(uint32_t seed, uint32_t salt);

        // A point and a direction of the emitter's shape in its local space (from `state`).
        void SampleShape(const ParticleSystemComponent& ps, uint32_t& state, glm::vec3& position, glm::vec3& direction);
        // `count` new particles (at most MaxParticles in all) from the emitter at `emitterWorld`.
        void Spawn(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, int count);
        // One frame of a playing or paused system: the start burst and emission while Playing (in the
        // editor a non-looping system only bursts, again whenever it is empty), then Integrate.
        void Update(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float dt, bool editor);
        // Forces, velocity over lifetime, speed limit, motion and ageing; the dead ones are removed.
        void Integrate(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float dt);

        // ---- the look of a particle at its age
        glm::vec4 GetColor(const ParticleSystemComponent& ps, const Particle& p);
        float GetSize(const ParticleSystemComponent& ps, const Particle& p);
        // The largest size a particle of the system can have (culling bounds).
        float GetMaxSize(const ParticleSystemComponent& ps);
        // The flipbook frame (0..frameCount-1) a particle shows (Texture Sheet module; 0 without it).
        int GetSheetFrame(const ParticleSystemComponent& ps, const Particle& p, int frameCount);
        // The frames of the Texture Sheet module's grid (Columns x Rows, at most FrameCount).
        int GetGridFrameCount(const ParticleSystemComponent& ps);

    }

}
