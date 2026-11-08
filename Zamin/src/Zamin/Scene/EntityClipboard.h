#pragma once

#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneSerializer.h"

#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class PrefabDatabase;

    // The entity clipboard (copy / paste in the hierarchy and the viewport). Its text is the same
    // document as a prefab file - one writer and one reader for both (SceneSerializer's entity sets) -
    // with "Kind": "Clipboard" and what only the clipboard keeps:
    //   - the hierarchy's hidden / locked flags and the prefab links of the instances copied whole
    //     (a pasted instance stays linked to its prefab; a part of one is pasted as plain entities);
    //   - "World": each copied root's transform in the world, by its ID (only for roots that had a
    //     parent) - pasted next to another parent it stays where it was;
    //   - "Origin": {"Prefab", "PrefabUUID"} - a prefab copied from the content browser: pasted as a
    //     linked instance of that prefab (the entities in the text are used when it is gone).
    // Older clipboard texts (no version, M20) and prefab files paste as they always did.
    class EntityClipboard
    {
    public:
        // The subtrees of `roots` (each once, depth-first, in order) as clipboard text.
        static std::string Copy(Scene& scene, const std::vector<Entity>& roots);
        // A prefab file as clipboard text with its Origin (its entities brought up to date with the
        // prefabs nested in it). "" with `error` when it cannot be read.
        static std::string CopyPrefab(PrefabDatabase& db, std::string_view relPath, std::string* error = nullptr);
        // Text that holds entities (the clipboard, a prefab file).
        static bool IsEntityText(std::string_view text);
        // The prefab the text was copied from ("" when none).
        static std::string GetOrigin(const std::string& text);

        struct PasteResult
        {
            std::vector<Entity> Entities;   // everything made, roots first
            std::vector<Entity> Roots;      // the pasted roots, in order
            std::string Origin;             // a prefab pasted as a linked instance of it ("" otherwise)
            size_t Unlinked = 0;            // prefab links dropped (parts of instances)
            std::string Error;              // why nothing was pasted
        };
        // Pastes the text into `scene`: new UUIDs (references inside follow), the links of whole
        // instances kept (`db`: an Origin prefab is placed linked; nullptr - the text's entities are
        // used), the roots placed as the next siblings of `sibling` (none: at the top level) at the
        // world transform they were copied with. Never on a running scene's behalf (the editor's).
        static PasteResult Paste(Scene& scene, PrefabDatabase* db, const std::string& text, Entity sibling = {});
    };

}
