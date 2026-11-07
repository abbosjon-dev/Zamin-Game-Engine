#pragma once

// Engine-internal: SceneRecovery over the parsed tree (nlohmann::json is not part of the engine's
// public headers - include this from the engine's .cpp files only).

#include "Zamin/Scene/SceneRecovery.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace Zamin::SceneRecoveryJson {

    // The parts of `text` that parse (see SceneRecovery), as the root object of a scene file / an
    // entity set; nothing when the text is not one at all ("nope", "[1, 2", nesting too deep).
    std::optional<nlohmann::json> Salvage(const std::string& text, SceneRecovery::Result* stats = nullptr);

    // A pre-scan: whether objects / arrays nest deeper than `maxDepth` anywhere (strings skipped).
    bool NestsDeeperThan(const std::string& text, int maxDepth);

}
