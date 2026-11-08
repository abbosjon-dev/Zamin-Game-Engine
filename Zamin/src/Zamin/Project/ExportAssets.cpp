#include "Zamin/Project/ExportAssets.h"
#include "Zamin/Utils/StringUtils.h"

#include "Zamin/Asset/AssetReferences.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/SceneSerializer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;
    using ordered_json = nlohmann::ordered_json;

    namespace {

        std::string Normalize(std::string_view path)
        {
            std::string text = fs::path(std::string(path)).lexically_normal().generic_string();
            while (text.size() > 1 && text.back() == '/')
                text.pop_back();
            if (text == ".")
                text.clear();
            return text;
        }

        // '*' matches any run of characters ('/' included).
        bool Glob(std::string_view text, std::string_view pattern)
        {
            size_t t = 0, p = 0, star = std::string_view::npos, mark = 0;
            while (t < text.size())
            {
                if (p < pattern.size() && pattern[p] == '*')
                {
                    star = p++;
                    mark = t;
                }
                else if (p < pattern.size() && pattern[p] == text[t])
                {
                    p++;
                    t++;
                }
                else if (star != std::string_view::npos)
                {
                    p = star + 1;
                    t = ++mark;
                }
                else
                    return false;
            }
            while (p < pattern.size() && pattern[p] == '*')
                p++;
            return p == pattern.size();
        }

    }

    // ------------------------------------------------------------------ settings
    std::string ExportSettings::ToJson() const
    {
        ordered_json j;
        j["ExcludeUnusedAssets"] = ExcludeUnusedAssets;
        j["AlwaysInclude"] = AlwaysInclude;
        j["ReleaseScriptModule"] = ReleaseScriptModule;
        j["StripBinaries"] = StripBinaries;
        if (PrefabWarningsAsErrors)
            j["PrefabWarningsAsErrors"] = true;
        return j.dump(4);
    }

    bool ExportSettings::FromJson(const std::string& text, ExportSettings& out)
    {
        ordered_json j = ordered_json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        ExportSettings s;
        if (auto it = j.find("ExcludeUnusedAssets"); it != j.end() && it->is_boolean())
            s.ExcludeUnusedAssets = it->get<bool>();
        if (auto it = j.find("AlwaysInclude"); it != j.end() && it->is_array())
            for (const auto& item : *it)
                if (item.is_string())
                    if (std::string path = Normalize(item.get<std::string>()); !path.empty() &&
                        std::find(s.AlwaysInclude.begin(), s.AlwaysInclude.end(), path) == s.AlwaysInclude.end())
                        s.AlwaysInclude.push_back(std::move(path));
        if (auto it = j.find("ReleaseScriptModule"); it != j.end() && it->is_boolean())
            s.ReleaseScriptModule = it->get<bool>();
        if (auto it = j.find("StripBinaries"); it != j.end() && it->is_boolean())
            s.StripBinaries = it->get<bool>();
        if (auto it = j.find("PrefabWarningsAsErrors"); it != j.end() && it->is_boolean())
            s.PrefabWarningsAsErrors = it->get<bool>();
        out = std::move(s);
        return true;
    }

    // ------------------------------------------------------------------ the plan
    bool ExportAssets::MatchesInclude(std::string_view path, std::string_view pattern)
    {
        const std::string file = Normalize(path), entry = Normalize(pattern);
        if (file.empty() || entry.empty())
            return false;
        if (entry.find('*') != std::string::npos)
            return Glob(file, entry);
        return file == entry || (file.size() > entry.size() && file.starts_with(entry) && file[entry.size()] == '/');
    }

    std::string ExportAssets::FormatSize(uintmax_t bytes)
    {
        if (bytes >= 1024ull * 1024ull)
            return std::format("{:.1f} MB", (double)bytes / (1024.0 * 1024.0));
        if (bytes >= 1024ull)
            return std::format("{:.0f} KB", (double)bytes / 1024.0);
        return std::format("{} B", bytes);
    }

    ExportAssetPlan ExportAssets::Plan(const AssetReferenceIndex& index, const ExportAssetInputs& inputs)
    {
        ExportAssetPlan plan;
        const fs::path& root = index.GetAssetDirectory();
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec))
            return plan;

        // ---- every file of the asset folder (hidden names and .zmeta files skipped)
        std::vector<std::string> files;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code fec;
            const std::string name = it->path().filename().string();
            if (name.starts_with("."))
            {
                if (it->is_directory(fec))
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(fec) || StringUtils::ToLower(it->path().extension().string()) == ".zmeta")
                continue;
            files.push_back(it->path().lexically_relative(root).generic_string());
        }
        std::sort(files.begin(), files.end());

        // ---- what is always taken: Resources/, the language files, the project's own list
        std::vector<std::string> always = { "Resources" };
        for (const std::string& folder : inputs.AlwaysFolders)
            if (std::string f = Normalize(folder); !f.empty())
                always.push_back(std::move(f));
        for (const std::string& entry : inputs.Settings.AlwaysInclude)
            if (std::string f = Normalize(entry); !f.empty())
                always.push_back(std::move(f));
        auto alwaysTaken = [&](const std::string& file) {
            return std::any_of(always.begin(), always.end(), [&](const std::string& pattern) { return MatchesInclude(file, pattern); });
        };

        // ---- the scenes of the game
        std::unordered_set<std::string> buildScenes;
        for (const std::string& scene : inputs.BuildScenes)
            if (std::string s = Normalize(scene); !s.empty())
                buildScenes.insert(std::move(s));
        auto isScene = [](const std::string& file) { return StringUtils::ToLower(fs::path(file).extension().string()) == ".zscene"; };
        const bool allScenes = buildScenes.empty();
        auto sceneInGame = [&](const std::string& file) { return allScenes || buildScenes.count(file) != 0; };

        // ---- what the game reaches: from the scenes, the files always taken, the project file and the code
        std::vector<std::string> roots;
        for (const std::string& file : files)
            if ((isScene(file) && sceneInGame(file)) || alwaysTaken(file))
                roots.push_back(file);
        const std::unordered_set<std::string> reachable = index.CollectReachable(roots, true);

        auto exclude = [&](const std::string& file, std::string reason) {
            ExcludedAsset asset;
            asset.Path = file;
            asset.Type = AssetTypes::FromPath(file);
            std::error_code sec;
            const uintmax_t size = fs::file_size(root / file, sec);
            asset.Size = sec ? 0 : size;
            asset.Reason = std::move(reason);
            plan.ExcludedBytes += asset.Size;
            plan.ExcludedSet.insert(file);
            plan.Excluded.push_back(std::move(asset));
        };
        for (const std::string& file : files)
        {
            const std::string ext = StringUtils::ToLower(fs::path(file).extension().string());
            if (alwaysTaken(file))
            {
                plan.Included.push_back(file);
                continue;
            }
            if (isScene(file) && !sceneInGame(file))
            {
                exclude(file, "build ro'yxatida yo'q");
                continue;
            }
            if (ext == ".zsceneb" || ext == ".znav")
            {
                // A scene's binary cache / saved navigation bakes go with its scene.
                fs::path scene(file);
                scene.replace_extension(".zscene");
                if (!sceneInGame(scene.generic_string()))
                {
                    exclude(file, "sahnasi eksportga kirmaydi");
                    continue;
                }
                plan.Included.push_back(file);
                continue;
            }
            if (inputs.Settings.ExcludeUnusedAssets && !reachable.count(file))
            {
                const AssetType type = AssetTypes::FromPath(file);
                if (AssetTypes::IsRegistered(type))
                {
                    exclude(file, index.IsReferenced(file) ? "faqat o'yinga kirmaydigan fayllar ishlatadi" : "hech qayerda ishlatilmaydi");
                    continue;
                }
                // Another file (a model's buffer, an image only a model names): out with what uses it.
                if (index.IsReferenced(file))
                {
                    exclude(file, "faqat o'yinga kirmaydigan fayllar ishlatadi");
                    continue;
                }
            }
            plan.Included.push_back(file);
        }
        for (const std::string& file : plan.Included)
        {
            std::error_code sec;
            const uintmax_t size = fs::file_size(root / file, sec);
            plan.IncludedBytes += sec ? 0 : size;
        }
        return plan;
    }

    std::vector<std::string> ExportAssets::CollectScenes(const ProjectConfig& config, const fs::path& assetDirectory, std::vector<std::string>* notes)
    {
        std::vector<std::string> scenes;
        for (const BuildSceneEntry& entry : config.BuildScenes)
            if (entry.Enabled && !entry.Path.empty())
                if (std::string path = entry.Path.lexically_normal().generic_string();
                    std::find(scenes.begin(), scenes.end(), path) == scenes.end())
                    scenes.push_back(std::move(path));
        if (scenes.empty())
            return scenes; // no build list: every scene
        if (!config.StartScene.empty())
        {
            const std::string start = config.StartScene.lexically_normal().generic_string();
            if (std::find(scenes.begin(), scenes.end(), start) == scenes.end())
            {
                if (notes)
                    notes->push_back(std::format("Boshlang'ich sahna build ro'yxatida yo'q - baribir eksportga qo'shiladi: {}", start));
                scenes.push_back(start);
            }
        }
        // The sub-scenes the listed scenes bring (and theirs).
        for (size_t i = 0; i < scenes.size(); i++)
        {
            SceneFileInfo info;
            if (!SceneSerializer::ReadSceneInfo(assetDirectory / scenes[i], info))
                continue;
            for (const std::string& sub : info.SubScenes)
            {
                const std::string path = fs::path(sub).lexically_normal().generic_string();
                if (std::find(scenes.begin(), scenes.end(), path) != scenes.end())
                    continue;
                if (notes)
                    notes->push_back(std::format("'{}' sub-sahnasi ham kiritiladi ({} ichida)", path, scenes[i]));
                scenes.push_back(path);
            }
        }
        return scenes;
    }

    ExportAssetPlan ExportAssets::PlanForActiveProject(std::vector<std::string>* scenes, std::vector<std::string>* notes, AssetReferenceIndex* indexOut)
    {
        Ref<Project> project = Project::GetActive();
        if (!project)
            return {};
        const ProjectConfig& config = project->GetConfig();
        AssetReferenceIndex local;
        AssetReferenceIndex& index = indexOut ? *indexOut : local;
        index.BuildForActiveProject();
        ExportAssetInputs inputs;
        inputs.BuildScenes = CollectScenes(config, Project::GetAssetDirectory(), notes);
        inputs.Settings = config.Export;
        inputs.AlwaysFolders = { config.Localization.Directory.generic_string() };
        if (scenes)
            *scenes = inputs.BuildScenes;
        return Plan(index, inputs);
    }

    std::string ExportAssetPlan::FormatReport(std::string_view gameName) const
    {
        std::string text = std::format("# {} - eksportga kirmagan assetlar\n", gameName);
        text += std::format("# Kiritildi: {} ta fayl ({}), chiqarib tashlandi: {} ta ({})\n", Included.size(), ExportAssets::FormatSize(IncludedBytes),
                            Excluded.size(), ExportAssets::FormatSize(ExcludedBytes));
        text += "# Kerak bo'lsa: Project settings > Build sahnalar > Eksport > \"Doim kiritish\" ro'yxatiga qo'shing.\n";
        for (const ExcludedAsset& asset : Excluded)
            text += std::format("{}\t{}\t{}\n", asset.Path, ExportAssets::FormatSize(asset.Size), asset.Reason);
        return text;
    }

}
