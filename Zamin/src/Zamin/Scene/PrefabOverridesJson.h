#pragma once

// Engine-internal: the override paths (PrefabOverrides.h) over the JSON of one entity (nlohmann::json
// is not part of the engine's public headers - include this from the engine's .cpp files only).

#include "Zamin/Scene/PrefabOverrides.h"

#include <nlohmann/json.hpp>

namespace Zamin::PrefabOverrides {

    // The value at `path` (null when it is not there).
    const nlohmann::json* Get(const nlohmann::json& entity, std::string_view path);
    // Sets the value at `path` (making the objects on the way); an asset path's "<key>UUID" next to
    // it goes along from `sidecarFrom` (the object `value` came from), or is removed.
    void Set(nlohmann::json& entity, std::string_view path, const nlohmann::json& value, const nlohmann::json* sidecarFrom = nullptr);
    // Removes the value at `path` (with its "<key>UUID"); false when it was not there.
    bool Erase(nlohmann::json& entity, std::string_view path);

    // The same value: numbers compared as floats when either is not whole (the files hold floats).
    bool ValuesEqual(const nlohmann::json& a, const nlohmann::json& b);

    // Where `current` differs from `clean` (the entity as the prefab makes it, references in the same
    // space): `paths` the override paths (sorted), `removed` the clean components `current` does not
    // have. "Entity", "Editor" and the instance-owned values (isRoot) are not looked at.
    void DiffPaths(const nlohmann::json& clean, const nlohmann::json& current, bool isRoot, std::vector<std::string>& paths,
                   std::vector<std::string>& removed);

    // The entity as it should be: `clean` with the instance's values at `overrides` taken from
    // `current`, the `removed` components left out and the instance-owned values (and "Entity",
    // "Editor") kept from `current`. A path whose component / object `clean` no longer has is left
    // out and listed in `orphans`.
    nlohmann::json Merge(const nlohmann::json& clean, const nlohmann::json& current, const std::vector<std::string>& overrides,
                         const std::vector<std::string>& removed, bool isRoot, std::vector<std::string>* orphans = nullptr);

}
