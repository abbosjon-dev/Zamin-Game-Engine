#include "Zamin/Scene/TerrainFoliage.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Terrain.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Zamin {
    TerrainReprojectResult TerrainFoliage::Reproject(Scene& scene, Entity terrain, const TerrainReprojectOptions& options)
    {
        TerrainReprojectResult result;
        if (!terrain || !terrain.IsValid() || terrain.GetScene() != &scene || !terrain.HasComponent<TerrainComponent>())
        {
            result.Error = "Faol terrain noto'g'ri.";
            return result;
        }
        if (scene.IsRunning() || scene.IsSimulating())
        {
            result.Error = "Foliage qayta joylash faqat Edit rejimida ishlaydi.";
            return result;
        }
        if (!std::isfinite(options.HeightOffset) || !std::isfinite(options.Radius) || !std::isfinite(options.Center.x) || !std::isfinite(options.Center.y))
        {
            result.Error = "Foliage parametrlari chekli son bo'lishi kerak.";
            return result;
        }
        auto& surface = terrain.GetComponent<TerrainComponent>();
        surface.EnsureSize();
        const auto world = scene.GetWorldTransform(terrain);
        const glm::mat3 linear(world);
        if (!std::isfinite(glm::determinant(linear)) || std::abs(glm::determinant(linear)) < 1e-8f)
        {
            result.Error = "Terrain masshtabi nolga yaqin.";
            return result;
        }
        const auto inverse = glm::inverse(linear), normalMatrix = glm::transpose(inverse);
        const uint32_t limit = std::clamp(options.MaxObjects, 1u, 5000u);
        for (auto root : scene.GetChildren(terrain))
        {
            auto* tags = root.TryGetComponent<TagsComponent>();
            // Packed batches own their per-instance terrain projection. Moving their root
            // here would offset the entire forest after a legacy entity-scatter brush stroke.
            if (!tags || !tags->Has("TerrainPainted") || root.HasComponent<FoliageComponent>()) continue;
            if (result.Visited >= limit)
            {
                ++result.Skipped;
                continue;
            }
            ++result.Visited;
            if (scene.IsLockedInEditor(root) || scene.IsHiddenInEditor(root))
            {
                ++result.Skipped;
                continue;
            }
            auto& transform = root.GetTransform();
            auto p = transform.Translation;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || p.x < 0 || p.z < 0 || p.x > surface.Size.x || p.z > surface.Size.y ||
                (options.Radius >= 0 && glm::distance(glm::vec2(p.x, p.z), options.Center) > options.Radius))
            {
                ++result.Skipped;
                continue;
            }
            const float height = TerrainTools::SampleHeight(surface, p.x, p.z) + options.HeightOffset;
            bool changed = std::abs(p.y - height) > 1e-5f;
            if (options.AlignToNormal)
            {
                const auto worldNormal = glm::normalize(normalMatrix * TerrainTools::SampleNormal(surface, p.x, p.z));
                const auto desired = glm::normalize(inverse * worldNormal) * (transform.Scale.y < 0 ? -1.0f : 1.0f);
                auto q = glm::quat(transform.Rotation);
                auto up = q * glm::vec3(0, 1, 0);
                if (std::isfinite(glm::dot(up, up)) && glm::dot(up, desired) < 1 - 1e-6f)
                {
                    q = glm::normalize(glm::rotation(glm::normalize(up), desired) * q);
                    transform.Rotation = glm::eulerAngles(q);
                    changed = true;
                }
            }
            if (changed)
            {
                transform.Translation.y = height;
                ++result.Changed;
            }
        }
        return result;
    }
}
