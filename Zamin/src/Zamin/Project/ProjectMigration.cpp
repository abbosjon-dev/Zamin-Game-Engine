#include "Zamin/Project/ProjectMigration.h"

#include <nlohmann/json.hpp>

#include <format>
#include <functional>

namespace Zamin {

    using ordered_json = nlohmann::ordered_json;

    namespace {

        // v0 -> v1: the flat keys into the "Project" object.
        size_t MigrateFlatToProject(ordered_json& root)
        {
            static const char* s_Keys[] = { "Name", "StartScene", "StartSceneUUID", "AssetDirectory", "SourceDirectory", "ScriptModulePath",
                                            "EngineVersion" };
            ordered_json project = ordered_json::object();
            size_t changes = 0;
            for (const char* key : s_Keys)
                if (auto it = root.find(key); it != root.end())
                {
                    project[key] = *it;
                    root.erase(it);
                    changes++;
                }
            // "Project" first, the other sections after it as they were.
            ordered_json result = ordered_json::object();
            result["Project"] = std::move(project);
            for (auto it = root.begin(); it != root.end(); ++it)
                result[it.key()] = it.value();
            root = std::move(result);
            return changes;
        }

        // v1 -> v2: build scenes written as plain paths become { "Path", "Enabled" }.
        size_t MigrateBuildSceneEntries(ordered_json& root)
        {
            size_t changes = 0;
            if (auto it = root.find("BuildScenes"); it != root.end() && it->is_array())
                for (ordered_json& item : *it)
                    if (item.is_string())
                    {
                        item = ordered_json{ { "Path", item.get<std::string>() }, { "Enabled", true } };
                        changes++;
                    }
            return changes;
        }

        struct Step
        {
            ProjectMigrationStep Info;
            std::function<size_t(ordered_json&)> Apply;
        };

        const std::vector<Step>& Steps()
        {
            static const std::vector<Step> s_Steps = {
                { { 0, 1, "tekis kalitlar (Name, StartScene ...) \"Project\" obyektiga ko'chirildi" }, MigrateFlatToProject },
                { { 1, 2, "build sahnalari yo'l matnidan { Path, Enabled } obyektiga o'tkazildi; \"Version\" qo'shildi" }, MigrateBuildSceneEntries },
            };
            return s_Steps;
        }

        int VersionOf(const ordered_json& root)
        {
            if (!root.is_object())
                return -1;
            if (auto it = root.find("Version"); it != root.end())
                return it->is_number_integer() && it->get<int>() >= 0 ? it->get<int>() : -1;
            if (auto it = root.find("Project"); it != root.end())
                return it->is_object() ? 1 : -1;
            if (auto it = root.find("Name"); it != root.end() && it->is_string())
                return 0;
            return -1;
        }

    }

    bool ProjectMigrationResult::IsNewer() const { return FileVersion > ProjectMigration::CurrentVersion; }

    size_t ProjectMigrationResult::GetChangeCount() const
    {
        size_t count = 0;
        for (const ProjectMigrationStep& step : Steps)
            count += step.Changes;
        return count;
    }

    std::string ProjectMigrationResult::Summary() const
    {
        if (Steps.empty())
            return {};
        return std::format("v{} -> v{} ({} qadam, {} ta o'zgarish)", FileVersion, Version, Steps.size(), GetChangeCount());
    }

    const std::vector<ProjectMigrationStep>& ProjectMigration::GetChain()
    {
        static const std::vector<ProjectMigrationStep> s_Chain = [] {
            std::vector<ProjectMigrationStep> chain;
            for (const Step& step : Steps())
                chain.push_back(step.Info);
            return chain;
        }();
        return s_Chain;
    }

    int ProjectMigration::ReadVersion(const std::string& text)
    {
        ordered_json root = ordered_json::parse(text, nullptr, false);
        return root.is_discarded() ? -1 : VersionOf(root);
    }

    bool ProjectMigration::MigrateText(std::string& text, ProjectMigrationResult* result, std::string* error)
    {
        ordered_json root = ordered_json::parse(text, nullptr, false);
        const int version = root.is_discarded() ? -1 : VersionOf(root);
        if (version < 0)
        {
            if (error)
                *error = root.is_discarded() ? "JSON emas" : "loyiha fayli emas (\"Project\" yoki \"Version\" yo'q)";
            return false;
        }
        ProjectMigrationResult r;
        r.FileVersion = version;
        r.Version = version;
        if (version < CurrentVersion)
        {
            for (const Step& step : Steps())
                if (step.Info.From == r.Version)
                {
                    ProjectMigrationStep applied = step.Info;
                    applied.Changes = step.Apply(root);
                    r.Steps.push_back(std::move(applied));
                    r.Version = step.Info.To;
                }
            // "Version" first.
            ordered_json migrated = ordered_json::object();
            migrated["Version"] = r.Version;
            for (auto it = root.begin(); it != root.end(); ++it)
                if (it.key() != "Version")
                    migrated[it.key()] = it.value();
            text = migrated.dump(4);
        }
        if (result)
            *result = std::move(r);
        return true;
    }

}
