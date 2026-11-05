#include "Zamin/Renderer/Lighting2D.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    bool Lighting2DData::operator==(const Lighting2DData& other) const
    {
        if (Ambient != other.Ambient || Lights.size() != other.Lights.size())
            return false;
        for (size_t i = 0; i < Lights.size(); i++)
        {
            const Light2DData& a = Lights[i];
            const Light2DData& b = other.Lights[i];
            if (a.Position != b.Position || a.Radius != b.Radius || a.InnerRadius != b.InnerRadius || a.Color != b.Color ||
                a.Falloff != b.Falloff || a.Spot != b.Spot || a.Direction != b.Direction || a.CosOuter != b.CosOuter ||
                a.CosInner != b.CosInner)
                return false;
        }
        return true;
    }

    float Light2DMath::Attenuation(float distance, float radius, float innerRadius, float falloff)
    {
        if (!(radius > 0.0f) || distance >= radius)
            return 0.0f;
        innerRadius = std::clamp(innerRadius, 0.0f, radius);
        if (distance <= innerRadius)
            return 1.0f;
        float t = (distance - innerRadius) / std::max(1e-6f, radius - innerRadius);
        return std::pow(std::clamp(1.0f - t, 0.0f, 1.0f), std::max(0.01f, falloff));
    }

    float Light2DMath::SpotFactor(const glm::vec2& direction, const glm::vec2& toPoint, float cosInner, float cosOuter)
    {
        float len = glm::length(toPoint);
        if (len < 1e-6f)
            return 1.0f; // at the light itself
        float c = glm::dot(direction, toPoint / len);
        if (cosInner <= cosOuter + 1e-6f)
            return c >= cosOuter ? 1.0f : 0.0f;
        float t = std::clamp((c - cosOuter) / (cosInner - cosOuter), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t); // smoothstep
    }

    glm::vec3 Light2DMath::Evaluate(const Lighting2DData& lighting, const glm::vec2& point)
    {
        glm::vec3 light = lighting.Ambient;
        for (const Light2DData& l : lighting.Lights)
        {
            glm::vec2 d = point - l.Position;
            float a = Attenuation(glm::length(d), l.Radius, l.InnerRadius, l.Falloff);
            if (a > 0.0f && l.Spot)
                a *= SpotFactor(l.Direction, d, l.CosInner, l.CosOuter);
            light += l.Color * a;
        }
        return light;
    }

    // Smooth value noise in 0..1.
    static float Hash01(uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352du;
        x ^= x >> 15; x *= 0x846ca68bu;
        x ^= x >> 16;
        return (float)(x & 0xffffff) / (float)0xffffff;
    }

    float Light2DMath::Flicker(double time, float amount, float speed, uint32_t seed)
    {
        amount = std::clamp(amount, 0.0f, 1.0f);
        if (amount <= 0.0f || !(speed > 0.0f))
            return 1.0f;
        double x = time * (double)speed;
        double cell = std::floor(x);
        float f = (float)(x - cell);
        uint32_t i = (uint32_t)(int64_t)cell;
        float a = Hash01(i * 0x9e3779b9u ^ seed), b = Hash01((i + 1) * 0x9e3779b9u ^ seed);
        float s = f * f * (3.0f - 2.0f * f);
        float noise = a + (b - a) * s;
        return 1.0f - amount * noise;
    }

}
