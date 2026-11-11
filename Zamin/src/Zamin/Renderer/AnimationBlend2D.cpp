#include "Animation.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Zamin {
    void ComputeBlend2DWeights(const std::vector<glm::vec2>& positions, glm::vec2 value, std::vector<float>& weights)
    {
        weights.assign(positions.size(), 0.0f);
        if (positions.empty() || positions.size() > 64) return;
        if (!std::isfinite(value.x) || !std::isfinite(value.y)) value = {};
        double sum = 0.0, nearestDistance = std::numeric_limits<double>::infinity();
        size_t nearest = positions.size();
        for (size_t i = 0; i < positions.size(); ++i)
        {
            const auto p = glm::dvec2(positions[i]);
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
            const auto delta = glm::dvec2(value) - p;
            const double distance = glm::dot(delta, delta);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = i;
            }
            double weight = 1.0;
            for (size_t j = 0; j < positions.size(); ++j)
            {
                if (i == j || !std::isfinite(positions[j].x) || !std::isfinite(positions[j].y)) continue;
                const auto direction = glm::dvec2(positions[j]) - p;
                const double length = glm::dot(direction, direction);
                if (length == 0.0)
                {
                    if (j < i)
                    {
                        weight = 0.0;
                        break;
                    }
                    continue;
                }
                weight = std::min(weight, std::clamp(1.0 - glm::dot(delta, direction) / length, 0.0, 1.0));
            }
            weights[i] = float(weight);
            sum += weight;
        }
        if (sum > 0.0)
            for (auto& w : weights) w = float(w / sum);
        else if (nearest < positions.size()) weights[nearest] = 1.0f;
    }
}
