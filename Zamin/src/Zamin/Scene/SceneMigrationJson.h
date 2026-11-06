#pragma once

// Engine-internal: the migration chain over a parsed scene file (nlohmann::json is not part of the
// engine's public headers - include this from the engine's .cpp files only).

#include "Zamin/Scene/SceneMigration.h"

#include <nlohmann/json.hpp>

namespace Zamin::SceneMigrationJson {

    // The "Version" of a parsed scene file (0: none or not a whole number >= 0).
    int ReadVersion(const nlohmann::json& root);
    // Runs the steps from the file's version to the current one over `root` (a scene file's root
    // object) and sets its "Version". A newer file is left alone (result.IsNewer()).
    SceneMigrationResult Migrate(nlohmann::json& root);

}
