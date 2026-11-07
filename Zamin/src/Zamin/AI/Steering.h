#pragma once

#include "Zamin/Math/Random.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

// Steering behaviours (Reynolds): small, pure functions an AI script combines into one force and
// integrates into its velocity - for creatures that wander, flocks, pursuers that lead their target,
// or the last metre of a path. Each returns a steering force: the change of velocity it wants
// (desired velocity - current velocity, in velocity units). They work on glm::vec2 (2D games, or a
// 3D game's ground plane) and on glm::vec3; nothing is random except Wander, which draws from its own
// seeded generator (the same seed, the same walk).
//
//   glm::vec2 force(0.0f);
//   Steering::Accumulate(force, Steering::Separation(p, v, neighbours, 1.2f, MaxSpeed) * 1.5f, MaxForce);
//   Steering::Accumulate(force, Steering::Arrive(p, v, goal, MaxSpeed, 2.0f), MaxForce);
//   Steering::Accumulate(force, Steering::Wander(m_Wander, p, v, MaxSpeed, dt) * 0.5f, MaxForce);
//   Steering::Integrate(p, v, force, MaxForce, MaxSpeed, dt);
namespace Zamin::Steering {

    template<glm::length_t L>
    using Vec = glm::vec<L, float, glm::defaultp>;

    // ---- helpers
    // `v` no longer than `max`.
    template<glm::length_t L>
    Vec<L> Truncate(const Vec<L>& v, float max)
    {
        const float length2 = glm::dot(v, v);
        if (!(max > 0.0f))
            return Vec<L>(0.0f);
        if (length2 > max * max)
            return v * (max / std::sqrt(length2));
        return v;
    }

    // `v` scaled to `length` (zero stays zero).
    template<glm::length_t L>
    Vec<L> WithLength(const Vec<L>& v, float length)
    {
        const float l = glm::length(v);
        return l > 1e-6f ? v * (length / l) : Vec<L>(0.0f);
    }

    // Adds `force` to the running total as long as the total stays within maxForce (prioritised
    // blending: add the most important behaviours first; what does not fit is cut). False once the
    // budget is used up.
    template<glm::length_t L>
    bool Accumulate(Vec<L>& total, const Vec<L>& force, float maxForce)
    {
        const float used = glm::length(total);
        const float left = maxForce - used;
        if (!(left > 1e-6f))
            return false;
        const float length = glm::length(force);
        if (length < 1e-9f)
            return true;
        total += length <= left ? force : force * (left / length);
        return length <= left;
    }

    // One step of a simple vehicle: the force (at most maxForce, velocity units / s) changes the
    // velocity (at most maxSpeed), the velocity moves the position.
    template<glm::length_t L>
    void Integrate(Vec<L>& position, Vec<L>& velocity, const Vec<L>& force, float maxForce, float maxSpeed, float dt)
    {
        if (!(dt > 0.0f))
            return;
        velocity = Truncate<L>(velocity + Truncate<L>(force, maxForce) * dt, maxSpeed);
        position += velocity * dt;
    }

    // ---- behaviours
    // Full speed straight at the target.
    template<glm::length_t L>
    Vec<L> Seek(const Vec<L>& position, const Vec<L>& velocity, const Vec<L>& target, float maxSpeed)
    {
        return WithLength<L>(target - position, maxSpeed) - velocity;
    }

    // Full speed straight away from the threat - only while it is nearer than panicDistance (else
    // nothing: zero).
    template<glm::length_t L>
    Vec<L> Flee(const Vec<L>& position, const Vec<L>& velocity, const Vec<L>& threat, float maxSpeed,
                float panicDistance = std::numeric_limits<float>::infinity())
    {
        const Vec<L> away = position - threat;
        if (glm::dot(away, away) > panicDistance * panicDistance)
            return Vec<L>(0.0f);
        if (glm::dot(away, away) < 1e-12f)
        {
            // Right on top of it: any way out (the first axis), the same every time.
            Vec<L> out(0.0f);
            out[0] = maxSpeed;
            return out - velocity;
        }
        return WithLength<L>(away, maxSpeed) - velocity;
    }

    // Towards the target, slowing down inside slowingRadius to stop on it (within stopRadius it
    // brakes to a stand: the force cancels the velocity).
    template<glm::length_t L>
    Vec<L> Arrive(const Vec<L>& position, const Vec<L>& velocity, const Vec<L>& target, float maxSpeed, float slowingRadius,
                  float stopRadius = 0.0f)
    {
        const Vec<L> toTarget = target - position;
        const float distance = glm::length(toTarget);
        stopRadius = std::max(0.0f, stopRadius);
        if (distance <= std::max(stopRadius, 1e-5f))
            return -velocity;
        const float slowing = std::max(slowingRadius - stopRadius, 1e-5f);
        const float speed = maxSpeed * std::min(1.0f, (distance - stopRadius) / slowing);
        return toTarget * (speed / distance) - velocity;
    }

    // Seeks where the target will be: its position + its velocity times the time it takes to get
    // there (at most maxPrediction seconds).
    template<glm::length_t L>
    Vec<L> Pursue(const Vec<L>& position, const Vec<L>& velocity, const Vec<L>& targetPosition, const Vec<L>& targetVelocity, float maxSpeed,
                  float maxPrediction = 1.0f)
    {
        const float distance = glm::length(targetPosition - position);
        const float t = std::clamp(maxSpeed > 1e-6f ? distance / maxSpeed : 0.0f, 0.0f, std::max(0.0f, maxPrediction));
        return Seek<L>(position, velocity, targetPosition + targetVelocity * t, maxSpeed);
    }

    // Flees from where the threat will be (as Pursue predicts it); nothing beyond panicDistance.
    template<glm::length_t L>
    Vec<L> Evade(const Vec<L>& position, const Vec<L>& velocity, const Vec<L>& threatPosition, const Vec<L>& threatVelocity, float maxSpeed,
                 float maxPrediction = 1.0f, float panicDistance = std::numeric_limits<float>::infinity())
    {
        const float distance = glm::length(threatPosition - position);
        if (distance > panicDistance)
            return Vec<L>(0.0f);
        const float t = std::clamp(maxSpeed > 1e-6f ? distance / maxSpeed : 0.0f, 0.0f, std::max(0.0f, maxPrediction));
        return Flee<L>(position, velocity, threatPosition + threatVelocity * t, maxSpeed);
    }

    // Wandering: a point on a circle (Radius) Distance ahead of the agent moves a random bit
    // (Jitter units / s) every step, the agent seeks it - a smooth, aimless walk. 3D: on the XZ
    // plane (walkers) unless Planar is off.
    struct WanderSettings
    {
        float Distance = 2.0f;
        float Radius = 1.0f;
        float Jitter = 6.0f;
        bool Planar = true; // vec3 only
    };

    template<glm::length_t L>
    struct WanderState
    {
        Vec<L> Target{ 0.0f }; // on the circle, relative to its centre
        Vec<L> Heading{ 0.0f };
        RandomGenerator Random;
        bool Initialized = false;

        explicit WanderState(uint64_t seed = 1) : Random(seed) {}
        void Reset(uint64_t seed)
        {
            Random.SetSeed(seed);
            Initialized = false;
            Target = Heading = Vec<L>(0.0f);
        }
    };
    using WanderState2D = WanderState<2>;
    using WanderState3D = WanderState<3>;

    template<glm::length_t L>
    Vec<L> Wander(WanderState<L>& state, const Vec<L>& position, const Vec<L>& velocity, float maxSpeed, float dt,
                  const WanderSettings& settings = {})
    {
        const bool planar = L == 3 && settings.Planar;
        auto flatten = [&](Vec<L> v) {
            if constexpr (L == 3)
                if (planar)
                    v.y = 0.0f;
            return v;
        };
        auto randomUnit = [&]() {
            Vec<L> v(0.0f);
            if constexpr (L == 2)
                v = state.Random.OnUnitCircle();
            else if (planar)
            {
                const glm::vec2 c = state.Random.OnUnitCircle();
                v = Vec<L>(c.x, 0.0f, c.y);
            }
            else
                v = state.Random.OnUnitSphere();
            return v;
        };
        const float radius = std::max(0.0f, settings.Radius);
        if (!state.Initialized)
        {
            state.Target = randomUnit() * radius;
            state.Heading = randomUnit();
            state.Initialized = true;
        }
        // The heading: where it goes (the last one while it stands still).
        const Vec<L> moving = flatten(velocity);
        if (glm::dot(moving, moving) > 1e-8f)
            state.Heading = glm::normalize(moving);
        // The point jitters and goes back onto the circle.
        const float jitter = std::max(0.0f, settings.Jitter) * std::max(0.0f, dt);
        state.Target = WithLength<L>(state.Target + randomUnit() * jitter, radius);
        const Vec<L> goal = position + state.Heading * settings.Distance + state.Target;
        return flatten(Seek<L>(position, velocity, goal, maxSpeed));
    }

    // ---- groups (neighbours: the others near this one - not itself)
    // A push away from neighbours nearer than `radius`, the stronger the nearer (velocity units: at
    // most maxSpeed from one neighbour right on top of it).
    template<glm::length_t L>
    Vec<L> Separation(const Vec<L>& position, std::span<const Vec<L>> neighbours, float radius, float maxSpeed)
    {
        Vec<L> push(0.0f);
        if (!(radius > 0.0f))
            return push;
        for (const Vec<L>& n : neighbours)
        {
            const Vec<L> away = position - n;
            const float d = glm::length(away);
            if (d >= radius)
                continue;
            const float strength = 1.0f - d / radius;
            if (d > 1e-6f)
                push += away * (strength * maxSpeed / d);
        }
        return Truncate<L>(push, maxSpeed);
    }

    // Towards the neighbours' average velocity.
    template<glm::length_t L>
    Vec<L> Alignment(const Vec<L>& velocity, std::span<const Vec<L>> neighbourVelocities)
    {
        if (neighbourVelocities.empty())
            return Vec<L>(0.0f);
        Vec<L> average(0.0f);
        for (const Vec<L>& v : neighbourVelocities)
            average += v;
        return average / (float)neighbourVelocities.size() - velocity;
    }

    // Towards the neighbours' centre (arriving there).
    template<glm::length_t L>
    Vec<L> Cohesion(const Vec<L>& position, const Vec<L>& velocity, std::span<const Vec<L>> neighbours, float maxSpeed, float slowingRadius = 1.0f)
    {
        if (neighbours.empty())
            return Vec<L>(0.0f);
        Vec<L> center(0.0f);
        for (const Vec<L>& n : neighbours)
            center += n;
        return Arrive<L>(position, velocity, center / (float)neighbours.size(), maxSpeed, slowingRadius);
    }

    // ---- obstacle avoidance (feelers; the Nav Agent's Avoidance uses physics rays for them)
    // A ray ahead of the agent, on its ground plane: what it hit (Distance along Direction, the
    // surface's Normal there).
    struct Feeler
    {
        glm::vec2 Direction{ 0.0f }; // unit
        float Length = 0.0f;
        bool Hit = false;
        float Distance = 0.0f;
        glm::vec2 Normal{ 0.0f };
        // Where the centre of what it hit is, relative to the agent (when known: a head-on hit then
        // turns away from it - the short way round).
        bool HasCenter = false;
        glm::vec2 CenterOffset{ 0.0f };
    };

    // Three feelers: straight ahead (Length), and `angle` radians to the left / right (sideScale times
    // as long). Left = turned counter-clockwise in the plane.
    std::array<Feeler, 3> MakeFeelers(const glm::vec2& forward, float length, float angle, float sideScale = 0.75f);

    // The push (velocity units, at most maxSpeed) that steers the agent going `forward` away from what
    // the feelers hit - firmly from the first touch, harder the deeper: sideways off the surface (a
    // head-on hit turns it to the side whose feeler is freer), plus braking for a hit straight ahead.
    // Zero when nothing was hit. `side` (optional, kept by the caller between steps): the way a head-on
    // hit turns, +1 left / -1 right; 0 - choose (then it is set), so the agent does not dither
    // between the two while it goes round one obstacle (reset it to 0 once the way is clear).
    glm::vec2 AvoidObstacles(const glm::vec2& forward, std::span<const Feeler> feelers, float maxSpeed, int* side = nullptr);

}
