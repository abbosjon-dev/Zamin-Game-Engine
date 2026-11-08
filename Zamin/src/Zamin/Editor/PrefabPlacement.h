#pragma once

#include "Zamin/Math/Geometry.h"
#include "Zamin/Scene/Entity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class Scene;

    // How the content browser's picture of a prefab is taken: a UI prefab as a game screen at its
    // canvas' reference resolution, a 3D one from a three-quarter angle, a 2D one straight on.
    enum class PrefabThumbnailView : uint8_t { UI, View2D, View3D };

    // The editor's prefab placement and pictures, the parts that need no window (tests reach them):
    // which view a prefab's thumbnail is taken from and what it frames, where several prefabs
    // dropped at once go, and the names their instances get.
    namespace PrefabPlacement {

        // UI when anything has a UI Transform or a UI Canvas; 3D when anything is 3D (a mesh, a 3D
        // body, collider or joint, a 3D light, an animator, billboarded particles); else 2D.
        PrefabThumbnailView ChooseThumbnailView(Scene& scene);
        // What the picture frames: the bounds of what the entities draw (Scene::ComputeBounds), else
        // a small box around their positions (a light, an effect that has not started).
        AABB ThumbnailBounds(Scene& scene);

        // Where `count` prefabs dropped at `hit` go: in a row along `right` (normalized here),
        // `spacing` apart, the row centred on the hit (one prefab: at the hit).
        std::vector<glm::vec3> Layout(size_t count, const glm::vec3& hit, const glm::vec3& right, float spacing);
        // The row's spacing for prefabs this wide: 1.2 x the widest, at least `minimum`.
        float Spacing(const std::vector<float>& widths, float minimum = 1.0f);
        // `base` when it is free, else "base (2)", "base (3)" ... (a "base (5)" asked for counts on
        // from "base"). `taken` says whether a name is used.
        std::string UniqueName(std::string_view base, const std::function<bool(std::string_view)>& taken);

    }

}
