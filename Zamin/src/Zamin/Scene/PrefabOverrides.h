#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin::PrefabOverrides {

    // What a prefab instance changed on one of its entities is a list of PATHS into the entity's
    // scene-file JSON (PrefabLinkComponent::Overrides):
    // - the entity's own values: "Tag", "Active", "Parent" (moved to another parent inside the
    //   instance), "ChildOrder" (its prefab children in another order);
    // - "<Component>": a component the instance added (its key in scene files, "Rigidbody2D");
    // - "<Component>/<member>[/<sub>...]": one value - objects are gone into, arrays and numbers
    //   are a single value ("SpriteRenderer/Color", "Camera/Shake/Amplitude");
    //   "NativeScript/Fields/<name>" is one script field.
    // A '/' or '~' inside a name is written "~1" / "~0" (as in JSON Pointer).
    // Removed prefab components are listed apart (PrefabLinkComponent::RemovedComponents).

    std::string EscapeSegment(std::string_view name);
    std::string UnescapeSegment(std::string_view segment);
    // "a/b~1c" -> { "a", "b/c" }.
    std::vector<std::string> SplitPath(std::string_view path);
    std::string JoinPath(const std::vector<std::string>& names);
    // The component (or entity value) a path belongs to: its first part ("SpriteRenderer").
    std::string_view RootKey(std::string_view path);
    // `path` is `prefix` or lies inside it ("A/B/C" is under "A/B", "A/BC" is not).
    bool IsUnder(std::string_view path, std::string_view prefix);

    // Values that belong to the instance and never to the prefab: where the root is placed in the
    // scene (its parent, its position and rotation, its UI position) and its name. They are not
    // overrides, a prefab change does not reach them and Apply does not write them.
    bool IsInstanceOwned(bool isRoot, std::string_view path);

    // The UUID of an entity a sync makes for an instance (`root`: the instance root's UUID, `local`:
    // the entity's ID in the prefab): the same every time (splitmix64), never 0.
    uint64_t MixUUID(uint64_t root, uint64_t local);

}
