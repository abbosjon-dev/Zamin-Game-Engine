#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Which value of a component an inspector row shows: the row's label as the inspector writes it
    // (the Uzbek source text, before the editor's language) -> the override path of that value
    // ("SpriteRenderer/Color"). The inspector marks a prefab instance's changed rows by it and offers
    // Apply / Revert for the row. Rows not in the table are marked through their component.
    struct PrefabFieldLabel
    {
        std::string_view Component;  // the component's scene-file key ("SpriteRenderer")
        std::string_view Label;      // the row label ("Rang")
        std::string_view Path;       // the override path ("SpriteRenderer/Color")
    };

    namespace PrefabFieldLabels {

        const std::vector<PrefabFieldLabel>& GetAll();
        // The path of the row `label` in component `component`; empty when the table does not have it.
        std::string_view Find(std::string_view component, std::string_view label);

    }

}
