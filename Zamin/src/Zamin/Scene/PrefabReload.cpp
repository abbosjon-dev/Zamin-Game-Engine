// Prefab hot reload (PrefabUtility::ReloadFiles): prefab files that changed outside the editor are
// read again and the prefabs made of them are brought up to date on disk, each after the ones it is
// made of. The editor then syncs the open scene's instances with the result (SyncScene).
#include "Zamin/Scene/Prefab.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Scene/PrefabDatabase.h"

#include <algorithm>
#include <unordered_set>

namespace Zamin {

    PrefabUtility::ReloadResult PrefabUtility::ReloadFiles(PrefabDatabase& db, const std::vector<std::string>& relPaths)
    {
        ReloadResult result;
        std::vector<std::string> paths;
        for (const std::string& p : relPaths)
            if (std::string path = PrefabDatabase::NormalizePath(p); !path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end())
                paths.push_back(std::move(path));
        std::sort(paths.begin(), paths.end());

        // What really changed (every file looked at before anything is read again).
        for (const std::string& path : paths)
        {
            switch (db.CheckFile(path))
            {
                case PrefabDatabase::FileChange::Changed:
                case PrefabDatabase::FileChange::Added:
                    result.Changed.push_back(path);
                    break;
                case PrefabDatabase::FileChange::Removed:
                    result.Removed.push_back(path);
                    break;
                case PrefabDatabase::FileChange::None:
                case PrefabDatabase::FileChange::OwnWrite:
                    result.Ignored.push_back(path);
                    break;
            }
        }
        if (result.Changed.empty() && result.Removed.empty())
            return result;

        // The prefabs made of them (nesting, variants), each once.
        std::vector<std::string> changed = result.Changed;
        changed.insert(changed.end(), result.Removed.begin(), result.Removed.end());
        std::unordered_set<std::string> seen(changed.begin(), changed.end());
        std::vector<std::string> dependents;
        for (const std::string& path : changed)
            for (const std::string& dependent : db.GetDependents(path))
                if (seen.insert(dependent).second)
                    dependents.push_back(dependent);
        // Each after every one of them it is made of (GetDependents orders one file's users; several
        // changed files: settled here).
        std::sort(dependents.begin(), dependents.end());
        std::vector<std::string> ordered;
        ordered.reserve(dependents.size());
        std::vector<bool> placed(dependents.size(), false);
        for (size_t round = 0; ordered.size() < dependents.size() && round <= dependents.size(); round++)
            for (size_t i = 0; i < dependents.size(); i++)
            {
                if (placed[i])
                    continue;
                bool ready = true;
                for (size_t j = 0; j < dependents.size() && ready; j++)
                    if (j != i && !placed[j] && db.DependsOn(dependents[i], dependents[j]) && !db.DependsOn(dependents[j], dependents[i]))
                        ready = false;
                if (!ready)
                    continue;
                placed[i] = true;
                ordered.push_back(dependents[i]);
            }
        for (size_t i = 0; i < dependents.size(); i++)
            if (!placed[i])
                ordered.push_back(dependents[i]); // a cycle: in name order (reported where it is read)

        for (const std::string& dependent : ordered)
        {
            const size_t before = result.Files.size();
            if (!SyncPrefabFile(db, dependent, &result.Files))
            {
                result.Failed.push_back(dependent);
                ZM_CORE_WARN("Prefab yangilanmadi (ichidagi prefab tashqarida o'zgardi): {}", dependent);
                continue;
            }
            if (result.Files.size() > before)
                result.Rewritten.push_back(dependent);
        }
        result.Affected = changed;
        result.Affected.insert(result.Affected.end(), ordered.begin(), ordered.end());
        return result;
    }

}
