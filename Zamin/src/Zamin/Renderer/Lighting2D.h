#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Zamin {

    // One 2D light as the sprite shader gets it (world units, linear colour).
    struct Light2DData
    {
        glm::vec2 Position{ 0.0f };
        float Radius = 1.0f;
        float InnerRadius = 0.0f;
        glm::vec3 Color{ 1.0f };          // linear, times the intensity
        float Falloff = 2.0f;
        bool Spot = false;
        glm::vec2 Direction{ 1.0f, 0.0f }; // spot: the cone's axis (unit)
        float CosOuter = -1.0f;           // spot: cos of half the cone; outside it no light
        float CosInner = -1.0f;           // spot: full light inside this
    };

    // The light Lit 2D renderers (sprites, tilemaps) get: the ambient light, plus every light.
    // Unlit renderers are drawn as before.
    struct Lighting2DData
    {
        glm::vec3 Ambient{ 1.0f }; // linear
        std::vector<Light2DData> Lights;

        bool operator==(const Lighting2DData& other) const;
    };

    // The math of the sprite shader's 2D lighting (the tests check it; the shader does the same).
    class Light2DMath
    {
    public:
        // At most this many lights reach one scene draw (the nearest to the view's centre).
        static constexpr int MaxLights = 32;

        // 1 inside the inner radius, fading to 0 at the radius: (1 - t)^falloff, t from 0 at the
        // inner radius to 1 at the radius.
        static float Attenuation(float distance, float radius, float innerRadius, float falloff);
        // Spot cone: 1 inside the inner cone, 0 outside the outer one, smooth between.
        static float SpotFactor(const glm::vec2& direction, const glm::vec2& toPoint, float cosInner, float cosOuter);
        // The light (linear rgb) at `point`: ambient + every light's colour times its attenuation.
        static glm::vec3 Evaluate(const Lighting2DData& lighting, const glm::vec2& point);
        // A torch's wavering: 1 - amount * noise (0..1, smooth), `speed` noise samples per second;
        // the same for the same time and seed.
        static float Flicker(double time, float amount, float speed, uint32_t seed);
    };

}
