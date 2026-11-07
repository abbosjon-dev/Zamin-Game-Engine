#include "Zamin/AI/Steering.h"

namespace Zamin::Steering {

    namespace {

        glm::vec2 Rotate(const glm::vec2& v, float angle)
        {
            const float c = std::cos(angle), s = std::sin(angle);
            return { c * v.x - s * v.y, s * v.x + c * v.y };
        }

        // The left-hand perpendicular (counter-clockwise).
        glm::vec2 Left(const glm::vec2& v) { return { -v.y, v.x }; }

    }

    std::array<Feeler, 3> MakeFeelers(const glm::vec2& forward, float length, float angle, float sideScale)
    {
        std::array<Feeler, 3> feelers{};
        const float l = glm::length(forward);
        const glm::vec2 f = l > 1e-6f ? forward / l : glm::vec2(1.0f, 0.0f);
        length = std::isfinite(length) ? std::max(0.0f, length) : 0.0f;
        feelers[0].Direction = f;
        feelers[0].Length = length;
        feelers[1].Direction = Rotate(f, angle);
        feelers[1].Length = length * std::max(0.0f, sideScale);
        feelers[2].Direction = Rotate(f, -angle);
        feelers[2].Length = length * std::max(0.0f, sideScale);
        return feelers;
    }

    glm::vec2 AvoidObstacles(const glm::vec2& forward, std::span<const Feeler> feelers, float maxSpeed, int* side)
    {
        const float l = glm::length(forward);
        if (l < 1e-6f || feelers.empty() || !(maxSpeed > 0.0f))
            return glm::vec2(0.0f);
        const glm::vec2 f = forward / l;
        const glm::vec2 left = Left(f);

        // How free each side is: the feelers that point that way (1 = nothing hit).
        float freeLeft = 1.0f, freeRight = 1.0f;
        for (const Feeler& feeler : feelers)
        {
            if (!feeler.Hit || !(feeler.Length > 0.0f))
                continue;
            const float free = std::clamp(feeler.Distance / feeler.Length, 0.0f, 1.0f);
            const float side = glm::dot(feeler.Direction, left);
            if (side > 0.05f)
                freeLeft = std::min(freeLeft, free);
            else if (side < -0.05f)
                freeRight = std::min(freeRight, free);
        }

        glm::vec2 push(0.0f);
        for (const Feeler& feeler : feelers)
        {
            if (!feeler.Hit || !(feeler.Length > 0.0f))
                continue;
            const float depth = 1.0f - std::clamp(feeler.Distance / feeler.Length, 0.0f, 1.0f);
            if (depth <= 0.0f)
                continue;
            // Sideways: the surface's normal without its part along the way it goes.
            glm::vec2 n = feeler.Normal;
            const float nl = glm::length(n);
            n = nl > 1e-6f ? n / nl : -feeler.Direction;
            glm::vec2 lateral = n - f * glm::dot(n, f);
            const float own = glm::dot(feeler.Direction, left);
            const bool ahead = std::abs(own) <= 0.05f;
            if (glm::length(lateral) < 0.25f || (ahead && side && *side != 0))
            {
                // Head on: a side feeler away from its own side; the straight one the way already chosen,
                // else away from the obstacle's centre, else to the freer side (left on a tie).
                float sign = own > 0.05f ? -1.0f : own < -0.05f ? 1.0f : 0.0f;
                if (ahead)
                {
                    if (side && *side != 0)
                        sign = (float)*side;
                    else if (feeler.HasCenter && std::abs(glm::dot(feeler.CenterOffset, left)) > 1e-3f)
                        sign = glm::dot(feeler.CenterOffset, left) > 0.0f ? -1.0f : 1.0f;
                    else
                        sign = freeRight > freeLeft ? -1.0f : 1.0f;
                    if (side)
                        *side = sign > 0.0f ? 1 : -1;
                }
                lateral = left * sign;
            }
            // Firmly from the first touch (a turn takes time at a limited acceleration), harder the deeper.
            push += glm::normalize(lateral) * (0.35f + 0.65f * depth);
            // Straight ahead: slow down too.
            if (ahead)
                push -= f * (depth * 0.5f);
        }
        const float length = glm::length(push);
        if (length < 1e-6f)
            return glm::vec2(0.0f);
        return push * (maxSpeed * std::min(1.0f, length) / length);
    }

}
