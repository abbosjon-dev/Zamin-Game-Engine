#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Zamin {

    struct Camera2DFollowComponent;

    // ------------------------------------------------------------------ camera shake
    // Trauma-based shake of a camera's view (CameraComponent::Shake). Add() puts in trauma (0..1,
    // added up to 1) that fades out linearly; the view is displaced by trauma^Exponent times
    // MaxOffset (along the view's right / up axes) and MaxAngle (roll), following smooth noise
    // curves. Only the picture moves: the camera's transform stays where it is, so following,
    // physics and scripts never see the shake. Deterministic: the same seed and the same calls
    // give the same motion.
    struct CameraShake
    {
        // settings (saved with the camera)
        glm::vec2 MaxOffset{ 0.4f, 0.4f };  // units at full trauma
        float MaxAngle = 2.0f;              // degrees of roll at full trauma
        float Exponent = 2.0f;              // 2: small hits stay subtle, big ones hit hard

        // runtime (cleared when play starts / stops)
        float Trauma = 0.0f;
        float Decay = 0.0f;                 // trauma lost per second
        float Frequency = 20.0f;            // noise samples per second (how jittery it is)
        float Time = 0.0f;
        uint32_t Seed = 0x5EED2D01u;

        static constexpr float MinDuration = 0.01f;

        // Adds `intensity` of trauma (the total stays <= 1); the shake ends `duration` seconds from
        // now, or later when an earlier shake lasts longer. frequency <= 0 keeps the current one.
        void Add(float intensity, float duration, float frequency);
        void Stop() { Trauma = 0.0f; Decay = 0.0f; }
        // Time goes on and the trauma fades.
        void Update(float dt);
        void ResetRuntime() { Trauma = 0.0f; Decay = 0.0f; Time = 0.0f; }
        bool IsActive() const { return Trauma > 0.0f; }
        // Seconds until it is still (0 when it is).
        float GetRemaining() const { return Trauma > 0.0f && Decay > 0.0f ? Trauma / Decay : 0.0f; }
        // trauma ^ Exponent (0..1)
        float GetAmount() const;
        // The displacement now: x, y (units along the view's right / up axes) and the roll (radians).
        glm::vec3 GetOffset() const;
        // The same as a camera-space transform: the camera's world transform times this one is the
        // shaken view (identity when still).
        glm::mat4 GetMatrix() const;

        // Smooth value noise in [-1, 1]: one random value per whole t, eased in between.
        static float Noise(uint32_t seed, float t);
    };

    // ------------------------------------------------------------------ 2D camera follow
    // The math of Camera2DFollowComponent (no scene needed: the scene and the tests use it).
    struct Camera2DFollow
    {
        // Moves `current` towards `goal` the same way at any frame rate: `damping` seconds to cover
        // about 63% of the distance (0 or less: at the goal at once).
        static float Damp(float current, float goal, float damping, float dt);
        static glm::vec2 Damp(const glm::vec2& current, const glm::vec2& goal, const glm::vec2& damping, float dt);
        // The nearest centre to `center` that has `point` inside a dead zone `size` big (its full
        // width / height) around it: the centre moves only as far as the point is outside the zone.
        static glm::vec2 ApplyDeadZone(const glm::vec2& center, const glm::vec2& point, const glm::vec2& size);
        // The nearest centre to `center` that keeps a view `halfView` big (half its width / height)
        // inside [min, max]; on an axis where the view is larger than the bounds: their middle.
        static glm::vec2 ClampToBounds(const glm::vec2& center, const glm::vec2& halfView, const glm::vec2& min, const glm::vec2& max);
        // Half the size of what a camera (`projection`, world transform `cameraWorld`) sees of the
        // plane z = `depth` (orthographic: the same at every depth). Zero when it does not see the plane.
        static glm::vec2 GetHalfView(const glm::mat4& projection, const glm::mat4& cameraWorld, float depth);
        // One frame: where the centre of the view moves from `camera` (world x / y) following the
        // target at `target`. `halfView`: half of what the camera sees (for the bounds); dt: seconds.
        // Updates the component's runtime state (look-ahead, the target's last position).
        static glm::vec2 Step(Camera2DFollowComponent& follow, const glm::vec2& camera, const glm::vec2& target,
                              const glm::vec2& halfView, float dt);
        // Where the view would be with the target at `target` and nothing to catch up (the dead
        // zone and look-ahead left out): the target plus the offset, kept inside the bounds.
        static glm::vec2 GetSnapPosition(const Camera2DFollowComponent& follow, const glm::vec2& target, const glm::vec2& halfView);
    };

}
