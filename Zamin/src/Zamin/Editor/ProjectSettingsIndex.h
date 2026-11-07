#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Zamin {

    // One category of the editor's project settings window: its title, the labels of the settings it
    // shows (what the window's search looks through) and the .zproject sections it edits (keys of
    // ProjectSerializer::ConfigToSections: a top-level section or "Project.<key>").
    struct ProjectSettingsCategory
    {
        std::string Id;          // stable: "General", "Quality", "Physics" ...
        std::string Icon;        // Font Awesome codepoint (UTF-8)
        std::string Title;
        std::string Description;
        std::vector<std::string> Keywords;
        std::vector<std::string> Sections;
    };

    namespace ProjectSettingsIndex {

        // In the window's order: General, Quality, Physics, Audio, Input, TagsAndLayers, BuildScenes,
        // Debug, SaveData.
        const std::vector<ProjectSettingsCategory>& GetCategories();
        // Index of the category with this id (case is ignored), -1 when there is none.
        int Find(std::string_view id);

        // `text` contains `query`: ASCII case is ignored and the Uzbek apostrophes (ʻ U+02BB, ’ U+2019)
        // count as '. An empty query is in every text.
        bool TextContains(std::string_view text, std::string_view query);
        // The title or one of the keywords contains `query`; `matches` gets the matching keywords.
        bool Matches(const ProjectSettingsCategory& category, std::string_view query, std::vector<std::string>* matches = nullptr);

        // The section keys whose text differs between two lists (a key missing from one of them
        // counts as differing).
        std::vector<std::string> DiffSections(const std::vector<std::pair<std::string, std::string>>& a,
                                              const std::vector<std::pair<std::string, std::string>>& b);
        // One of the category's sections is among `changed`.
        bool IsChanged(const ProjectSettingsCategory& category, const std::vector<std::string>& changed);

    }

}
