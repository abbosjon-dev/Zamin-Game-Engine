#include "Zamin/Editor/PrefabPlacement.h"

#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace Zamin::PrefabPlacement {

    PrefabThumbnailView ChooseThumbnailView(Scene& scene)
    {
        auto any = [&]<typename... T>() { return (... || !scene.GetAllEntitiesWith<T>().empty()); };
        if (any.template operator()<UITransformComponent, UICanvasComponent>())
            return PrefabThumbnailView::UI;
        if (any.template operator()<MeshRendererComponent, AnimatorComponent, Rigidbody3DComponent, BoxCollider3DComponent, SphereCollider3DComponent,
                                    CapsuleCollider3DComponent, ConvexHullCollider3DComponent, MeshCollider3DComponent, CharacterController3DComponent,
                                    FixedJoint3DComponent, HingeJoint3DComponent, DistanceJoint3DComponent, SliderJoint3DComponent, ConeJoint3DComponent,
                                    DirectionalLightComponent, PointLightComponent, SpotLightComponent>())
            return PrefabThumbnailView::View3D;
        // An effect on its own: camera-facing particles are a 3D game's, flat ones a 2D game's.
        for (auto handle : scene.GetAllEntitiesWith<ParticleSystemComponent>())
            if (Entity(handle, &scene).GetComponent<ParticleSystemComponent>().Billboard)
                return PrefabThumbnailView::View3D;
        return PrefabThumbnailView::View2D;
    }

    AABB ThumbnailBounds(Scene& scene)
    {
        std::vector<Entity> roots = scene.GetRootEntities();
        AABB bounds;
        if (scene.ComputeBounds(roots, bounds))
            return bounds;
        // Nothing that draws has a size yet: the entities' places, a unit around them.
        AABB around = AABB::Empty();
        for (Entity e : scene.GetEntitiesInHierarchyOrder())
        {
            const glm::vec3 p = scene.GetWorldPosition(e);
            around.Min = glm::min(around.Min, p - glm::vec3(1.0f));
            around.Max = glm::max(around.Max, p + glm::vec3(1.0f));
        }
        return around.Min.x <= around.Max.x ? around : AABB{ glm::vec3(-1.0f), glm::vec3(1.0f) };
    }

    std::vector<glm::vec3> Layout(size_t count, const glm::vec3& hit, const glm::vec3& right, float spacing)
    {
        std::vector<glm::vec3> out;
        out.reserve(count);
        const float length = glm::length(right);
        const glm::vec3 axis = length > 1e-6f ? right / length : glm::vec3(1.0f, 0.0f, 0.0f);
        const float first = -0.5f * (float)(count > 0 ? count - 1 : 0);
        for (size_t i = 0; i < count; i++)
            out.push_back(hit + axis * ((first + (float)i) * spacing));
        return out;
    }

    float Spacing(const std::vector<float>& widths, float minimum)
    {
        float widest = 0.0f;
        for (float w : widths)
            widest = std::max(widest, w);
        return std::max(minimum, widest * 1.2f);
    }

    std::string UniqueName(std::string_view base, const std::function<bool(std::string_view)>& taken)
    {
        // "Enemy (3)" asked for: its stem "Enemy" is what is counted on from.
        std::string stem(base);
        if (stem.size() > 3 && stem.back() == ')')
            if (const size_t open = stem.rfind(" ("); open != std::string::npos && open + 2 < stem.size() - 1 &&
                                                       std::all_of(stem.begin() + (ptrdiff_t)open + 2, stem.end() - 1,
                                                                   [](char c) { return std::isdigit((unsigned char)c) != 0; }))
                stem.resize(open);
        if (!taken || !taken(base))
            return std::string(base);
        for (int i = 2; i < 100000; i++)
        {
            std::string candidate = std::format("{} ({})", stem, i);
            if (!taken(candidate))
                return candidate;
        }
        return std::string(base);
    }

}
