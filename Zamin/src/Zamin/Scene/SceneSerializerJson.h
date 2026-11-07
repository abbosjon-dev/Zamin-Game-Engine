#pragma once

// Engine-internal: one entity as the JSON of a scene file, and one component written back from it
// (nlohmann::json is not part of the engine's public headers - include this from the engine's .cpp
// files only). The prefab code (Scene/Prefab.cpp) compares and merges entities through it.

#include "Zamin/Scene/Entity.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace Zamin::SceneSerializerJson {

    struct EntityJsonOptions
    {
        bool Parent = true;        // "Parent" (the parent's UUID)
        bool HiddenLocked = true;  // "Editor": the hierarchy's eye / lock flags
        bool PrefabMarker = true;  // "Editor": the prefab of an instance root ("Prefab", "PrefabUUID", ...)
        bool PrefabLink = true;    // "Editor": the prefab link (PrefabLinkComponent)
    };

    // The entity as a scene file writes it ("Entity", "Tag", "Active", "Parent", one key per
    // component, "Editor"). Asset UUIDs next to paths only inside WithAssetScope.
    nlohmann::json SerializeEntityJson(Entity entity, const EntityJsonOptions& options = {});

    // Makes the components of a FRESH entity (only an ID, a name and a transform) from its JSON.
    // Throws on values of the wrong type (like the scene reader).
    void ReadEntityJson(const nlohmann::json& e, Entity entity);

    // Sets one top-level key of `entity` to `value` - a component (its values replaced, or added),
    // "Tag" (the name) or "Active" - read through a scratch entity and copied over, so it works on an
    // entity that already has the component. nullptr removes it ("Active": back to active). "Entity",
    // "Parent", "ChildOrder" and "Editor" are not components: false. False with `error` when the
    // value cannot be read (the entity is unchanged).
    bool ApplyComponentJson(Entity entity, std::string_view key, const nlohmann::json* value, std::string* error = nullptr);

    // Runs `fn` with the active project's asset registry looked up for the writes and reads inside
    // it (the "<key>UUID" next to asset paths), as one scene (de)serialization does.
    void WithAssetScope(const std::function<void()>& fn);

    // `root` as indented text (what json::dump(2) gives, written faster).
    std::string DumpIndented(const nlohmann::json& root, size_t sizeHint = 4096);

}
