#include "Zamin/Project/Project.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Core/Version.h"
#include "Zamin/Scripting/ScriptTemplate.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <stdexcept>

namespace Zamin {

    using json = nlohmann::json;
    using ordered_json = nlohmann::ordered_json;

    Ref<Project> Project::New()
    {
        s_ActiveProject = CreateRef<Project>();
        AudioEngine::SetMixerSettings(s_ActiveProject->m_Config.Audio);
        Quality::SetSettings(s_ActiveProject->m_Config.Quality);
        return s_ActiveProject;
    }

    Ref<Project> Project::Load(const std::filesystem::path& path)
    {
        Ref<Project> project = CreateRef<Project>();
        ProjectSerializer serializer(project);
        if (!serializer.Deserialize(path))
            return nullptr;
        project->m_ProjectFile = std::filesystem::absolute(path);
        project->m_ProjectDirectory = project->m_ProjectFile.parent_path();
        s_ActiveProject = project;
        AudioEngine::SetMixerSettings(project->m_Config.Audio);
        Quality::SetSettings(project->m_Config.Quality);
        ZM_CORE_INFO("Loyiha ochildi: {} ({})", project->m_Config.Name, project->m_ProjectDirectory.string());

        // The start scene was renamed / moved outside the editor: found again by its UUID.
        ProjectConfig& config = project->m_Config;
        std::error_code ec;
        if (!config.StartScene.empty() && config.StartSceneHandle && !std::filesystem::exists(GetAssetFileSystemPath(config.StartScene), ec))
            if (AssetRegistry* registry = GetAssetRegistry())
                if (std::string moved = registry->GetPath(config.StartSceneHandle); !moved.empty())
                {
                    ZM_CORE_INFO("Boshlang'ich sahna UUID orqali topildi: '{}' -> '{}'", config.StartScene.generic_string(), moved);
                    config.StartScene = moved;
                }
        // Build scenes the same way.
        for (BuildSceneEntry& entry : config.BuildScenes)
            if (!entry.Path.empty() && entry.Handle && !std::filesystem::exists(GetAssetFileSystemPath(entry.Path), ec))
                if (AssetRegistry* registry = GetAssetRegistry())
                    if (std::string moved = registry->GetPath(entry.Handle); !moved.empty())
                    {
                        ZM_CORE_INFO("Build sahnasi UUID orqali topildi: '{}' -> '{}'", entry.Path.generic_string(), moved);
                        entry.Path = moved;
                    }
        return s_ActiveProject;
    }

    std::vector<std::filesystem::path> Project::GetBuildScenes()
    {
        std::vector<std::filesystem::path> scenes;
        if (s_ActiveProject)
            for (const BuildSceneEntry& entry : s_ActiveProject->m_Config.BuildScenes)
                if (entry.Enabled && !entry.Path.empty())
                    scenes.push_back(entry.Path);
        return scenes;
    }

    std::filesystem::path Project::GetBuildScenePath(int buildIndex)
    {
        std::vector<std::filesystem::path> scenes = GetBuildScenes();
        return buildIndex >= 0 && buildIndex < (int)scenes.size() ? scenes[(size_t)buildIndex] : std::filesystem::path();
    }

    int Project::GetBuildIndex(const std::filesystem::path& scene)
    {
        const std::string wanted = scene.lexically_normal().generic_string();
        std::vector<std::filesystem::path> scenes = GetBuildScenes();
        for (size_t i = 0; i < scenes.size(); i++)
            if (scenes[i].lexically_normal().generic_string() == wanted)
                return (int)i;
        return -1;
    }

    std::filesystem::path Project::FindBuildScene(std::string_view name)
    {
        for (const std::filesystem::path& scene : GetBuildScenes())
            if (scene.stem().string() == name)
                return scene;
        return {};
    }

    bool Project::AddBuildScene(const std::filesystem::path& scene)
    {
        if (!s_ActiveProject || scene.empty() || scene.extension() != ".zscene")
            return false;
        auto& list = s_ActiveProject->m_Config.BuildScenes;
        const std::string wanted = scene.lexically_normal().generic_string();
        for (const BuildSceneEntry& entry : list)
            if (entry.Path.lexically_normal().generic_string() == wanted)
                return false;
        BuildSceneEntry entry;
        entry.Path = wanted;
        list.push_back(entry);
        return true;
    }

    std::vector<BuildSceneIssue> Project::CheckBuildScenes(const ProjectConfig& config, const std::filesystem::path& assetDirectory)
    {
        std::vector<BuildSceneIssue> issues;
        auto add = [&](BuildSceneIssue::Kind kind, int entry, std::string path, bool error, std::string message) {
            BuildSceneIssue issue;
            issue.Type = kind;
            issue.Entry = entry;
            issue.Path = std::move(path);
            issue.Error = error;
            issue.Message = std::move(message);
            issues.push_back(std::move(issue));
        };
        auto exists = [&](const std::filesystem::path& relative) {
            std::error_code ec;
            return std::filesystem::is_regular_file(relative.is_absolute() ? relative : assetDirectory / relative, ec);
        };
        auto normal = [](const std::filesystem::path& p) { return p.lexically_normal().generic_string(); };

        const std::string start = config.StartScene.empty() ? std::string() : normal(config.StartScene);
        bool anyEnabled = false, anyEnabledThere = false;
        for (const BuildSceneEntry& entry : config.BuildScenes)
            if (entry.Enabled)
            {
                anyEnabled = true;
                anyEnabledThere = anyEnabledThere || exists(entry.Path);
            }

        // The start scene.
        if (start.empty())
        {
            if (!anyEnabledThere)
                add(BuildSceneIssue::Kind::NoStartScene, -1, "", true,
                    "Boshlang'ich sahna tanlanmagan va build ro'yxatida yoqilgan sahna yo'q: o'yin hech narsani ochmaydi");
        }
        else if (!exists(config.StartScene))
            add(BuildSceneIssue::Kind::StartSceneMissing, -1, start, true,
                anyEnabledThere ? std::format("Boshlang'ich sahna fayli topilmadi: {} (o'yin birinchi build sahnasidan boshlanadi)", start)
                                : std::format("Boshlang'ich sahna fayli topilmadi: {}", start));
        else if (!config.BuildScenes.empty())
        {
            auto it = std::find_if(config.BuildScenes.begin(), config.BuildScenes.end(),
                                   [&](const BuildSceneEntry& e) { return normal(e.Path) == start; });
            if (it == config.BuildScenes.end())
                add(BuildSceneIssue::Kind::StartSceneNotListed, -1, start, false,
                    std::format("Boshlang'ich sahna ({}) build ro'yxatida yo'q: eksportga baribir kiradi, lekin build indeksi yo'q", start));
            else if (!it->Enabled)
                add(BuildSceneIssue::Kind::StartSceneDisabled, (int)(it - config.BuildScenes.begin()), start, false,
                    std::format("Boshlang'ich sahna ({}) ro'yxatda o'chirilgan: eksportga baribir kiradi, lekin build indeksi yo'q", start));
        }

        // The list.
        if (!config.BuildScenes.empty() && !anyEnabled)
            add(BuildSceneIssue::Kind::NoEnabledScenes, -1, "", false, "Ro'yxatdagi hamma sahnalar o'chirilgan: build indekslari yo'q");
        std::vector<std::string> seen;
        for (size_t i = 0; i < config.BuildScenes.size(); i++)
        {
            const BuildSceneEntry& entry = config.BuildScenes[i];
            const std::string path = normal(entry.Path);
            if (std::find(seen.begin(), seen.end(), path) != seen.end())
            {
                add(BuildSceneIssue::Kind::Duplicate, (int)i, path, false, std::format("{} ro'yxatda ikki marta", path));
                continue;
            }
            seen.push_back(path);
            if (entry.Path.extension() != ".zscene")
                add(BuildSceneIssue::Kind::NotAScene, (int)i, path, true, std::format("{} sahna fayli emas (.zscene)", path));
            else if (!exists(entry.Path))
                add(BuildSceneIssue::Kind::SceneMissing, (int)i, path, entry.Enabled,
                    std::format("Sahna fayli topilmadi: {}{}", path, entry.Enabled ? "" : " (o'chirilgan)"));
        }
        return issues;
    }

    std::filesystem::path Project::ResolveStartScene(const ProjectConfig& config, const std::filesystem::path& assetDirectory)
    {
        auto exists = [&](const std::filesystem::path& relative) {
            std::error_code ec;
            return !relative.empty() && std::filesystem::is_regular_file(relative.is_absolute() ? relative : assetDirectory / relative, ec);
        };
        if (exists(config.StartScene))
            return config.StartScene;
        for (const BuildSceneEntry& entry : config.BuildScenes)
            if (entry.Enabled && entry.Path.extension() == ".zscene" && exists(entry.Path))
                return entry.Path;
        return {};
    }

    const PlayerSettings& Project::GetPlayerSettings()
    {
        static const PlayerSettings s_Default;
        return s_ActiveProject ? s_ActiveProject->m_Config.Player : s_Default;
    }

    std::string Project::GetProductName()
    {
        return s_ActiveProject ? s_ActiveProject->m_Config.Player.GetProductName(s_ActiveProject->m_Config.Name) : std::string("Zamin");
    }

    std::filesystem::path Project::GetIconPath()
    {
        if (!s_ActiveProject || s_ActiveProject->m_Config.Player.Icon.empty())
            return {};
        return GetAssetFileSystemPath(s_ActiveProject->m_Config.Player.Icon);
    }

    bool Project::SaveActive(const std::filesystem::path& path)
    {
        if (!s_ActiveProject) return false;
        ProjectSerializer serializer(s_ActiveProject);
        if (!serializer.Serialize(path))
            return false;
        s_ActiveProject->m_ProjectFile = std::filesystem::absolute(path);
        s_ActiveProject->m_ProjectDirectory = s_ActiveProject->m_ProjectFile.parent_path();
        return true;
    }

    std::filesystem::path Project::GetProjectDirectory() { return s_ActiveProject ? s_ActiveProject->m_ProjectDirectory : std::filesystem::path(); }
    std::filesystem::path Project::GetProjectFilePath() { return s_ActiveProject ? s_ActiveProject->m_ProjectFile : std::filesystem::path(); }

    std::filesystem::path Project::GetAssetDirectory()
    {
        if (!s_ActiveProject) return std::filesystem::current_path();
        return s_ActiveProject->m_ProjectDirectory / s_ActiveProject->m_Config.AssetDirectory;
    }

    std::filesystem::path Project::GetSourceDirectory()
    {
        if (!s_ActiveProject) return {};
        return s_ActiveProject->m_ProjectDirectory / s_ActiveProject->m_Config.SourceDirectory;
    }

    std::filesystem::path Project::GetScriptModulePath()
    {
        if (!s_ActiveProject || s_ActiveProject->m_Config.ScriptModulePath.empty()) return {};
        return s_ActiveProject->m_ProjectDirectory / s_ActiveProject->m_Config.ScriptModulePath;
    }

    const InputActionMap& Project::GetInputActionMap()
    {
        static const InputActionMap s_Default = InputActionMap::CreateDefault();
        return s_ActiveProject ? s_ActiveProject->m_Config.InputActions : s_Default;
    }

    bool Project::RegisterTag(const std::string& tag)
    {
        if (!s_ActiveProject || tag.empty())
            return false;
        auto& tags = s_ActiveProject->m_Config.Tags;
        if (std::find(tags.begin(), tags.end(), tag) != tags.end())
            return false;
        tags.push_back(tag);
        return true;
    }

    const SortingLayerList& Project::GetSortingLayers()
    {
        static const SortingLayerList s_Default;
        return s_ActiveProject ? s_ActiveProject->m_Config.SortingLayers : s_Default;
    }

    const CollisionLayers2D& Project::GetCollisionLayers2D()
    {
        static const CollisionLayers2D s_Default;
        return s_ActiveProject ? s_ActiveProject->m_Config.CollisionLayers : s_Default;
    }

    AssetRegistry* Project::GetAssetRegistry()
    {
        if (!s_ActiveProject)
            return nullptr;
        Ref<AssetRegistry>& registry = s_ActiveProject->m_AssetRegistry;
        if (!registry)
            registry = CreateRef<AssetRegistry>();
        std::filesystem::path directory = AssetRegistry::NormalizeDirectory(GetAssetDirectory());
        if (!registry->IsScanned() || registry->GetAssetDirectory() != directory)
        {
            std::string error;
            // An exported game: the registry the editor wrote at export (no .zmeta file is read).
            if (!registry->GetWriteMetaFiles())
            {
                std::filesystem::path file = s_ActiveProject->m_ProjectDirectory / BuildAssetRegistryFile;
                if (auto snapshot = AssetRegistry::LoadSnapshot(file, &error))
                {
                    if (registry->Load(directory, *snapshot, true))
                    {
                        ZM_CORE_INFO("Assetlar: {} ta ({} faylidan)", registry->GetCount(), BuildAssetRegistryFile);
                        return registry.get();
                    }
                    ZM_CORE_WARN("{} eskirgan (unda bor fayl topilmadi) - asset papkasi o'qiladi", BuildAssetRegistryFile);
                }
                else if (!error.empty())
                    ZM_CORE_WARN("{} o'qilmadi ({}) - asset papkasi o'qiladi", BuildAssetRegistryFile, error);
            }

            // The snapshot of the last session: what moved since then keeps its handle.
            std::optional<AssetRegistry::Snapshot> previous = AssetRegistry::LoadSnapshot(GetAssetRegistryCachePath(), &error);
            if (!previous && !error.empty())
                ZM_CORE_WARN("Asset registry keshi o'qilmadi ({}) - e'tiborsiz qoldirildi", error);
            const AssetRegistry::ScanResult& scan = registry->Scan(directory, previous ? &*previous : nullptr);
            ZM_CORE_INFO("Assetlar: {} ta{}{}", scan.Assets,
                         scan.CreatedMeta ? std::format(", {} ta yangi .zmeta yaratildi", scan.CreatedMeta) : std::string(),
                         scan.OrphanMeta ? std::format(", {} ta egasiz .zmeta", scan.OrphanMeta) : std::string());
            if (scan.MovedOutside > 0)
                ZM_CORE_INFO("{} ta asset oxirgi ochilgandan beri ko'chirilgan yoki nomi o'zgargan{}", scan.MovedOutside,
                             scan.RecoveredMeta ? std::format(" ({} tasi .zmeta faylisiz - UUID va sozlamalari tiklandi)", scan.RecoveredMeta)
                                                : std::string());
            size_t shown = 0;
            for (const std::string& problem : scan.Problems)
                if (shown++ < 10)
                    ZM_CORE_WARN("{}", problem);
            if (scan.Problems.size() > 10)
                ZM_CORE_WARN("... va yana {} ta .zmeta muammosi", scan.Problems.size() - 10);
        }
        return registry.get();
    }

    std::filesystem::path Project::GetCacheDirectory()
    {
        return s_ActiveProject ? s_ActiveProject->m_ProjectDirectory / ".zamin" : std::filesystem::path();
    }

    std::filesystem::path Project::GetAssetRegistryCachePath()
    {
        return s_ActiveProject ? GetCacheDirectory() / "AssetRegistry.json" : std::filesystem::path();
    }

    bool Project::SaveAssetRegistryCache()
    {
        if (!s_ActiveProject || !s_ActiveProject->m_AssetRegistry || !s_ActiveProject->m_AssetRegistry->IsScanned())
            return false;
        return s_ActiveProject->m_AssetRegistry->SaveSnapshot(GetAssetRegistryCachePath());
    }

    std::filesystem::path Project::GetAssetFileSystemPath(const std::filesystem::path& path)
    {
        if (path.is_absolute() || !s_ActiveProject)
            return path;
        return GetAssetDirectory() / path;
    }

    std::string Project::GetAssetRelativePath(const std::filesystem::path& path)
    {
        std::error_code ec;
        std::filesystem::path rel = std::filesystem::relative(path, GetAssetDirectory(), ec);
        if (ec || rel.empty() || rel.string().starts_with(".."))
            return path.generic_string();
        return rel.generic_string();
    }

    // ------------------------------------------------------------------ serializer
    static json BindingsToJson(const std::vector<InputBinding>& bindings)
    {
        json list = json::array();
        for (const auto& b : bindings)
            list.push_back(InputBindingToString(b));
        return list;
    }

    static std::vector<InputBinding> BindingsFromJson(const json& j, const std::string& owner)
    {
        std::vector<InputBinding> bindings;
        if (!j.is_array()) return bindings;
        for (const json& item : j)
        {
            InputBinding b;
            if (item.is_string() && InputBindingFromString(item.get<std::string>(), b))
                bindings.push_back(b);
            else
                ZM_CORE_WARN("Noma'lum input bog'lanishi '{}' ({})", item.dump(), owner);
        }
        return bindings;
    }

    static json InputMapToJson(const InputActionMap& map)
    {
        json actions = json::array();
        for (const auto& a : map.Actions)
            actions.push_back({ { "Name", a.Name }, { "Bindings", BindingsToJson(a.Bindings) } });
        json axes = json::array();
        for (const auto& a : map.Axes)
        {
            json axis = { { "Name", a.Name }, { "Negative", BindingsToJson(a.Negative) }, { "Positive", BindingsToJson(a.Positive) },
                          { "Invert", a.Invert }, { "Deadzone", a.Deadzone } };
            axis["GamepadAxis"] = a.GamepadAxisCode >= 0 ? GamepadAxisToString((GamepadAxis)a.GamepadAxisCode) : "";
            axes.push_back(axis);
        }
        json result = { { "Actions", actions }, { "Axes", axes } };
        // Composite 2D axes: only when there are any (older files stay as they were).
        if (!map.Vectors.empty())
        {
            json vectors = json::array();
            for (const auto& v : map.Vectors)
                vectors.push_back({ { "Name", v.Name }, { "X", v.XAxis }, { "Y", v.YAxis }, { "Mode", InputVectorModeToString(v.Mode) },
                                    { "Deadzone", v.Deadzone } });
            result["Vectors"] = vectors;
        }
        return result;
    }

    static InputActionMap InputMapFromJson(const json& j)
    {
        InputActionMap map;
        if (j.contains("Actions") && j["Actions"].is_array())
            for (const json& a : j["Actions"])
            {
                InputAction action;
                action.Name = a.value("Name", "");
                if (action.Name.empty()) continue;
                if (a.contains("Bindings")) action.Bindings = BindingsFromJson(a["Bindings"], action.Name);
                map.Actions.push_back(std::move(action));
            }
        if (j.contains("Axes") && j["Axes"].is_array())
            for (const json& a : j["Axes"])
            {
                InputAxis axis;
                axis.Name = a.value("Name", "");
                if (axis.Name.empty()) continue;
                if (a.contains("Negative")) axis.Negative = BindingsFromJson(a["Negative"], axis.Name);
                if (a.contains("Positive")) axis.Positive = BindingsFromJson(a["Positive"], axis.Name);
                GamepadAxis gamepadAxis;
                axis.GamepadAxisCode = GamepadAxisFromString(a.value("GamepadAxis", ""), gamepadAxis) ? (int)gamepadAxis : -1;
                axis.Invert = a.value("Invert", false);
                axis.Deadzone = std::clamp(a.value("Deadzone", 0.15f), 0.0f, 0.95f);
                map.Axes.push_back(std::move(axis));
            }
        if (j.contains("Vectors") && j["Vectors"].is_array())
            for (const json& v : j["Vectors"])
            {
                if (!v.is_object())
                    continue;
                InputVector vector;
                vector.Name = v.value("Name", "");
                if (vector.Name.empty()) continue;
                vector.XAxis = v.value("X", std::string());
                vector.YAxis = v.value("Y", std::string());
                InputVectorModeFromString(v.value("Mode", std::string("Clamp")), vector.Mode);
                vector.Deadzone = std::clamp(v.value("Deadzone", 0.0f), 0.0f, 0.95f);
                map.Vectors.push_back(std::move(vector));
            }
        return map;
    }

    std::string ProjectSerializer::InputMapToString(const InputActionMap& map) { return InputMapToJson(map).dump(4); }

    static json SortingLayersToJson(const SortingLayerList& layers)
    {
        json list = json::array();
        for (const SortingLayer& l : layers.GetLayers())
        {
            json item = { { "ID", l.ID }, { "Name", l.Name } };
            if (l.YSort)
                item["YSort"] = true; // only when on: older files stay the same
            list.push_back(std::move(item));
        }
        return list;
    }

    static void SortingLayersFromJson(const json& j, SortingLayerList& out)
    {
        std::vector<SortingLayer> layers;
        if (j.is_array())
            for (const json& item : j)
            {
                if (!item.is_object())
                    continue;
                auto id = item.find("ID");
                auto name = item.find("Name");
                if (id == item.end() || !id->is_number_unsigned() || name == item.end() || !name->is_string())
                {
                    ZM_CORE_WARN("Noto'g'ri saralash qatlami e'tiborsiz qoldirildi: {}", item.dump());
                    continue;
                }
                auto ySort = item.find("YSort");
                layers.push_back({ id->get<uint32_t>(), name->get<std::string>(), ySort != item.end() && ySort->is_boolean() && ySort->get<bool>() });
            }
        out.Assign(layers);
    }

    std::string ProjectSerializer::SortingLayersToString(const SortingLayerList& layers) { return SortingLayersToJson(layers).dump(4); }

    bool ProjectSerializer::SortingLayersFromString(const std::string& text, SortingLayerList& out)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_array())
            return false;
        SortingLayersFromJson(j, out);
        return true;
    }

    bool ProjectSerializer::InputMapFromString(const std::string& text, InputActionMap& out)
    {
        try
        {
            out = InputMapFromJson(json::parse(text));
            return true;
        }
        catch (const std::exception& e)
        {
            ZM_CORE_ERROR("Input xaritasi o'qilmadi: {}", e.what());
            return false;
        }
    }

    // The top-level sections and the keys of "Project" this engine reads (anything else of a file is
    // kept in ProjectConfig::ExtraSections).
    static const std::vector<std::string>& KnownSections()
    {
        static const std::vector<std::string> s_Keys = { "Version", "Project", "Player", "BuildScenes", "InputActions", "Tags", "SortingLayers",
                                                         "CollisionLayers2D", "Audio", "Quality", "DebugDraw" };
        return s_Keys;
    }

    static const std::vector<std::string>& KnownProjectKeys()
    {
        static const std::vector<std::string> s_Keys = { "Name", "StartScene", "StartSceneUUID", "AssetDirectory", "SourceDirectory",
                                                         "ScriptModulePath", "EngineVersion" };
        return s_Keys;
    }

    static bool Contains(const std::vector<std::string>& list, const std::string& key)
    {
        return std::find(list.begin(), list.end(), key) != list.end();
    }

    // A section made by another module (sorted json, or JSON text) in the file's ordered JSON.
    static ordered_json ToOrdered(const json& j) { return ordered_json::parse(j.dump()); }
    static ordered_json ToOrdered(const std::string& text) { return ordered_json::parse(text); }

    // The kept unknown keys of one object ("" = top level, "Project", "Player") appended to it.
    static void AppendExtras(ordered_json& object, const ProjectConfig& c, const std::string& owner)
    {
        const std::string prefix = owner.empty() ? std::string() : owner + ".";
        for (const auto& [key, text] : c.ExtraSections)
        {
            const bool nested = key.starts_with("Project.") || key.starts_with("Player.");
            if (owner.empty() ? nested : !key.starts_with(prefix))
                continue;
            const std::string name = key.substr(prefix.size());
            if (name.empty() || object.contains(name))
                continue; // the engine writes this key itself now
            ordered_json value = ordered_json::parse(text, nullptr, false);
            if (!value.is_discarded())
                object[name] = std::move(value);
        }
    }

    static bool HasExtras(const ProjectConfig& c, const std::string& owner)
    {
        return std::any_of(c.ExtraSections.begin(), c.ExtraSections.end(), [&](const auto& e) { return e.first.starts_with(owner + "."); });
    }

    // The whole file, its sections in a fixed order (keys inside the sections as their modules write them).
    static ordered_json ConfigToJson(const ProjectConfig& c)
    {
        ordered_json j;
        j["Version"] = ProjectMigration::CurrentVersion;
        ordered_json project;
        project["Name"] = c.Name;
        project["StartScene"] = c.StartScene.generic_string();
        if (c.StartSceneHandle && !c.StartScene.empty())
            project["StartSceneUUID"] = c.StartSceneHandle.ToString();
        project["AssetDirectory"] = c.AssetDirectory.generic_string();
        project["SourceDirectory"] = c.SourceDirectory.generic_string();
        project["ScriptModulePath"] = c.ScriptModulePath.generic_string();
        project["EngineVersion"] = ZM_ENGINE_VERSION;
        AppendExtras(project, c, "Project");
        j["Project"] = std::move(project);
        // Written once something differs from the defaults (older files stay as they were).
        if (!c.Player.IsDefault() || HasExtras(c, "Player"))
        {
            ordered_json player = ToOrdered(c.Player.ToJson());
            AppendExtras(player, c, "Player");
            j["Player"] = std::move(player);
        }
        if (!c.BuildScenes.empty())
        {
            ordered_json list = ordered_json::array();
            for (const BuildSceneEntry& entry : c.BuildScenes)
            {
                ordered_json item;
                item["Path"] = entry.Path.generic_string();
                item["Enabled"] = entry.Enabled;
                if (entry.Handle)
                    item["UUID"] = entry.Handle.ToString();
                list.push_back(std::move(item));
            }
            j["BuildScenes"] = std::move(list);
        }
        j["InputActions"] = ToOrdered(InputMapToJson(c.InputActions));
        j["Tags"] = c.Tags;
        // Written once there is more than the Default layer (older files stay as they were).
        if (!(c.SortingLayers == SortingLayerList()))
            j["SortingLayers"] = ToOrdered(SortingLayersToJson(c.SortingLayers));
        // Written once something differs from the defaults (older files stay as they were).
        if (!c.CollisionLayers.IsDefault())
            j["CollisionLayers2D"] = ToOrdered(c.CollisionLayers.ToJson());
        // Written once the mixer differs from the defaults (older files stay as they were).
        if (!c.Audio.IsDefault())
            j["Audio"] = ToOrdered(c.Audio.ToJson());
        // Written once the levels differ from the defaults (older files stay as they were).
        if (!c.Quality.IsDefault())
            j["Quality"] = ToOrdered(c.Quality.ToJson());
        ordered_json debug;
        debug["Enabled"] = c.DebugDraw.Enabled;
        debug["Raycasts"] = c.DebugDraw.ShowRaycasts;
        debug["RaycastDuration"] = c.DebugDraw.RaycastDuration;
        debug["Contacts"] = c.DebugDraw.ShowContacts;
        debug["ContactDetails"] = c.DebugDraw.ShowContactDetails;
        debug["Joints"] = c.DebugDraw.ShowJoints;
        debug["Velocities"] = c.DebugDraw.ShowVelocities;
        debug["BodyVelocities"] = c.DebugDraw.ShowBodyVelocities;
        debug["VelocityScale"] = c.DebugDraw.VelocityScale;
        debug["LineWidth"] = c.DebugDraw.LineWidth;
        debug["TextSize"] = c.DebugDraw.TextSize;
        j["DebugDraw"] = std::move(debug);
        // What the file had that this engine does not read: at the end, as it was.
        AppendExtras(j, c, "");
        return j;
    }

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath)
    {
        auto& c = m_Project->GetConfig();
        // The start scene's UUID, from the registry of the project being saved (when it is active).
        if (m_Project == Project::GetActive() && !c.StartScene.empty())
            if (AssetRegistry* registry = Project::GetAssetRegistry())
            {
                const AssetMetadata* meta = registry->Find(c.StartScene);
                c.StartSceneHandle = meta && meta->HasMetaFile ? meta->Handle : AssetHandle();
            }
        // The build scenes' UUIDs the same way.
        if (AssetRegistry* registry = m_Project == Project::GetActive() && !c.BuildScenes.empty() ? Project::GetAssetRegistry() : nullptr)
            for (BuildSceneEntry& entry : c.BuildScenes)
            {
                const AssetMetadata* meta = registry->Find(entry.Path);
                if (meta && meta->HasMetaFile)
                    entry.Handle = meta->Handle;
            }
        if (!FileSystem::WriteTextFile(filepath, ConfigToJson(c).dump(4)))
            return false;
        c.FileVersion = ProjectMigration::CurrentVersion; // the file is in the current format now
        return true;
    }

    std::string ProjectSerializer::ConfigToString(const ProjectConfig& config) { return ConfigToJson(config).dump(4); }

    std::vector<std::pair<std::string, std::string>> ProjectSerializer::ConfigToSections(const ProjectConfig& config)
    {
        std::vector<std::pair<std::string, std::string>> sections;
        ordered_json j = ConfigToJson(config);
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (it.key() == "Project" && it->is_object())
                for (auto p = it->begin(); p != it->end(); ++p)
                    sections.emplace_back("Project." + p.key(), p->dump());
            else
                sections.emplace_back(it.key(), it->dump());
        }
        return sections;
    }

    // Throws (nlohmann) when a required part is missing or of the wrong type.
    static void ConfigFromJson(const json& j, ProjectConfig& c)
    {
        const json& p = j.at("Project");
        c.Name = p.value("Name", "Untitled");
        c.StartScene = p.value("StartScene", "");
        c.StartSceneHandle = {};
        if (auto it = p.find("StartSceneUUID"); it != p.end() && it->is_string())
            c.StartSceneHandle = AssetHandle::FromString(it->get<std::string>()).value_or(AssetHandle());
        c.AssetDirectory = p.value("AssetDirectory", "Assets");
        c.SourceDirectory = p.value("SourceDirectory", "Source");
        c.ScriptModulePath = p.value("ScriptModulePath", "");
        // Build scenes (older projects: none - every scene, no build indices).
        c.BuildScenes.clear();
        if (auto it = j.find("BuildScenes"); it != j.end() && it->is_array())
            for (const json& item : *it)
            {
                BuildSceneEntry entry;
                if (item.is_string())
                    entry.Path = item.get<std::string>();
                else if (item.is_object())
                {
                    entry.Path = item.value("Path", std::string());
                    entry.Enabled = item.value("Enabled", true);
                    if (auto uuid = item.find("UUID"); uuid != item.end() && uuid->is_string())
                        entry.Handle = AssetHandle::FromString(uuid->get<std::string>()).value_or(AssetHandle());
                }
                if (!entry.Path.empty())
                    c.BuildScenes.push_back(entry);
            }
        // Older projects have no input section: keep the defaults.
        if (j.contains("InputActions") && j["InputActions"].is_object())
            c.InputActions = InputMapFromJson(j["InputActions"]);
        else
            c.InputActions = InputActionMap::CreateDefault();
        // Tag list (older projects: the defaults).
        if (j.contains("Tags") && j["Tags"].is_array())
        {
            c.Tags.clear();
            for (const json& tag : j["Tags"])
                if (tag.is_string() && !tag.get<std::string>().empty() &&
                    std::find(c.Tags.begin(), c.Tags.end(), tag.get<std::string>()) == c.Tags.end())
                    c.Tags.push_back(tag.get<std::string>());
        }
        else
            c.Tags = ProjectConfig::DefaultTags();
        // Sorting layers (older projects: only Default).
        c.SortingLayers = SortingLayerList();
        if (auto it = j.find("SortingLayers"); it != j.end())
            SortingLayersFromJson(*it, c.SortingLayers);
        // 2D collision layers (older projects: only Default, everything collides).
        c.CollisionLayers = CollisionLayers2D();
        if (auto it = j.find("CollisionLayers2D"); it != j.end() && it->is_object())
            CollisionLayers2D::FromJson(it->dump(), c.CollisionLayers);
        // Audio mixer (older projects: Master > Music, SFX, UI).
        c.Audio = AudioSettings();
        if (auto it = j.find("Audio"); it != j.end() && it->is_object())
            AudioSettings::FromJson(it->dump(), c.Audio);
        // Debug drawing in the player (older projects: on, no physics views).
        c.DebugDraw = DebugDrawSettings();
        if (auto it = j.find("DebugDraw"); it != j.end() && it->is_object())
        {
            const json& d = *it;
            auto readBool = [&](const char* key, bool& out) {
                if (auto v = d.find(key); v != d.end() && v->is_boolean()) out = v->get<bool>();
            };
            auto readFloat = [&](const char* key, float& out) {
                if (auto v = d.find(key); v != d.end() && v->is_number()) out = v->get<float>();
            };
            readBool("Enabled", c.DebugDraw.Enabled);
            readBool("Raycasts", c.DebugDraw.ShowRaycasts);
            readFloat("RaycastDuration", c.DebugDraw.RaycastDuration);
            readBool("Contacts", c.DebugDraw.ShowContacts);
            readBool("ContactDetails", c.DebugDraw.ShowContactDetails);
            readBool("Joints", c.DebugDraw.ShowJoints);
            readBool("Velocities", c.DebugDraw.ShowVelocities);
            readBool("BodyVelocities", c.DebugDraw.ShowBodyVelocities);
            readFloat("VelocityScale", c.DebugDraw.VelocityScale);
            readFloat("LineWidth", c.DebugDraw.LineWidth);
            readFloat("TextSize", c.DebugDraw.TextSize);
            c.DebugDraw.Sanitize();
        }
        // Quality levels (older projects: Low / Medium / High / Ultra, starting on Ultra).
        c.Quality = QualitySettings();
        if (auto it = j.find("Quality"); it != j.end() && it->is_object())
            QualitySettings::FromJson(it->dump(), c.Quality);
        // The player: company / product / version, window, icon (older projects: the defaults).
        c.Player = PlayerSettings();
        if (auto it = j.find("Player"); it != j.end() && it->is_object())
            PlayerSettings::FromJson(it->dump(), c.Player);
    }

    // Migrates, reads, and keeps what is not understood. Throws (nlohmann) on a broken required part.
    static void ParseConfig(const std::string& text, ProjectConfig& out, ProjectMigrationResult& migration)
    {
        std::string migrated = text;
        std::string error;
        if (!ProjectMigration::MigrateText(migrated, &migration, &error))
            throw std::runtime_error(error);
        const ordered_json root = ordered_json::parse(migrated);
        ProjectConfig config;
        ConfigFromJson(json::parse(migrated), config);
        config.FileVersion = migration.FileVersion;
        // Keys this engine does not read, in file order.
        config.ExtraSections.clear();
        for (auto it = root.begin(); it != root.end(); ++it)
        {
            if (!Contains(KnownSections(), it.key()))
                config.ExtraSections.emplace_back(it.key(), it.value().dump());
            else if (it.key() == "Project" && it->is_object())
            {
                for (auto p = it->begin(); p != it->end(); ++p)
                    if (!Contains(KnownProjectKeys(), p.key()))
                        config.ExtraSections.emplace_back("Project." + p.key(), p.value().dump());
            }
            else if (it.key() == "Player" && it->is_object())
            {
                for (auto p = it->begin(); p != it->end(); ++p)
                    if (!Contains(PlayerSettings::GetKnownKeys(), p.key()))
                        config.ExtraSections.emplace_back("Player." + p.key(), p.value().dump());
            }
        }
        out = std::move(config);
    }

    bool ProjectSerializer::ConfigFromString(const std::string& text, ProjectConfig& out, ProjectMigrationResult* migration, std::string* error)
    {
        try
        {
            ProjectMigrationResult result;
            ParseConfig(text, out, result);
            if (migration)
                *migration = std::move(result);
            return true;
        }
        catch (const std::exception& e)
        {
            if (error)
                *error = e.what();
            else
                ZM_CORE_ERROR("Loyiha sozlamalari o'qilmadi: {}", e.what());
            return false;
        }
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath)
    {
        std::string text = FileSystem::ReadTextFile(filepath);
        if (text.empty()) return false;
        ProjectMigrationResult migration;
        try
        {
            ParseConfig(text, m_Project->GetConfig(), migration);
        }
        catch (const std::exception& e)
        {
            ZM_CORE_ERROR("Loyiha fayli o'qilmadi '{}': {}", filepath.string(), e.what());
            return false;
        }
        const std::string name = filepath.filename().string();
        if (migration.Migrated())
        {
            ZM_CORE_INFO("Loyiha fayli eski formatda: {} {} - saqlanganda joriy formatda yoziladi", name, migration.Summary());
            for (const ProjectMigrationStep& step : migration.Steps)
                ZM_CORE_INFO("  v{} -> v{}: {} ({} ta)", step.From, step.To, step.Description, step.Changes);
        }
        if (migration.IsNewer())
            ZM_CORE_WARN("Loyiha fayli yangiroq formatda (v{}, bu dvigatel v{} gacha biladi): {} - tushunarli qismi o'qildi, "
                         "qolgani saqlanganda o'zgarmay yoziladi", migration.FileVersion, ProjectMigration::CurrentVersion, name);
        if (const auto& extra = m_Project->GetConfig().ExtraSections; !extra.empty())
        {
            std::string keys;
            for (size_t i = 0; i < extra.size() && i < 6; i++)
                keys += (i ? ", " : "") + extra[i].first;
            if (extra.size() > 6)
                keys += std::format(" ... (+{})", extra.size() - 6);
            ZM_CORE_INFO("Loyiha faylidagi {} ta noma'lum kalit saqlab qolinadi: {}", extra.size(), keys);
        }
        return true;
    }

    // ------------------------------------------------------------------ template
    static std::filesystem::path FindEngineCMakeDir()
    {
        auto exeDir = FileSystem::GetExecutableDirectory();
        for (auto dir : { exeDir.parent_path(), exeDir, exeDir / "cmake" })
            if (std::filesystem::exists(dir / "ZaminConfig.cmake"))
                return dir;
        return exeDir.parent_path();
    }

    bool Project::WriteBuildFiles(const std::filesystem::path& projectDir, const std::string& name)
    {
        std::string cmake = std::format(R"(# Zamin o'yin loyihasi: C++ skriptlar moduli
cmake_minimum_required(VERSION 3.20)
project({0} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT TARGET Zamin)
    set(Zamin_DIR "{1}" CACHE PATH "Zamin engine build papkasi")
    find_package(Zamin REQUIRED CONFIG)
endif()

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${{CMAKE_CURRENT_SOURCE_DIR}}/Source/*.cpp ${{CMAKE_CURRENT_SOURCE_DIR}}/Source/*.h)
zamin_add_game_module({0} ${{GAME_SOURCES}})
)", name, FindEngineCMakeDir().generic_string());
        return FileSystem::WriteTextFile(projectDir / "CMakeLists.txt", cmake);
    }

    Ref<Project> Project::CreateFromTemplate(const std::filesystem::path& directory, const std::string& name)
    {
        std::error_code ec;
        std::filesystem::create_directories(directory / "Assets" / "Scenes", ec);
        std::filesystem::create_directories(directory / "Assets" / "Textures", ec);
        std::filesystem::create_directories(directory / "Assets" / "Audio", ec);
        std::filesystem::create_directories(directory / "Assets" / "Models", ec);
        std::filesystem::create_directories(directory / "Source", ec);
        if (ec)
        {
            ZM_CORE_ERROR("Loyiha papkasi yaratilmadi: {}", ec.message());
            return nullptr;
        }

        Ref<Project> project = CreateRef<Project>();
        auto& c = project->GetConfig();
        c.Name = name;
        c.StartScene = "Scenes/Main.zscene";
        c.ScriptModulePath = "Binaries/lib" + name + ".so";
        s_ActiveProject = project;
        project->m_ProjectDirectory = std::filesystem::absolute(directory);

        std::filesystem::path projectFile = directory / (name + ".zproject");
        if (!SaveActive(projectFile))
            return nullptr;
        WriteBuildFiles(directory, name);
        if (!std::filesystem::exists(directory / "Source" / "Scripts.cpp"))
            FileSystem::WriteTextFile(directory / "Source" / "Scripts.cpp", ScriptTemplate::GenerateProjectStarter());
        FileSystem::WriteTextFile(directory / ".gitignore", "Build/\nBinaries/\n.zamin/\n");

        // Default scene
        Ref<Scene> scene = CreateRef<Scene>();
        scene->SetName("Main");
        {
            Entity camera = scene->CreateEntity("Main Camera");
            camera.AddComponent<CameraComponent>();
            auto& t = camera.GetComponent<TransformComponent>();
            t.Translation = { 0.0f, 3.0f, 9.0f };
            t.Rotation = { -0.3f, 0.0f, 0.0f };

            Entity sun = scene->CreateEntity("Sun");
            sun.AddComponent<DirectionalLightComponent>();
            sun.GetComponent<TransformComponent>().Rotation = { -0.9f, 0.6f, 0.0f };

            Entity ground = scene->CreateEntity("Ground");
            auto& gm = ground.AddComponent<MeshRendererComponent>();
            gm.Type = PrimitiveMesh::Plane;
            gm.Mat.Albedo = { 0.55f, 0.6f, 0.55f, 1.0f };
            gm.Mat.Roughness = 0.9f;
            ground.GetComponent<TransformComponent>().Scale = { 20.0f, 1.0f, 20.0f };

            Entity cube = scene->CreateEntity("Cube");
            auto& cm = cube.AddComponent<MeshRendererComponent>();
            cm.Mat.Albedo = { 0.95f, 0.45f, 0.2f, 1.0f };
            cm.Mat.Roughness = 0.35f;
            cube.GetComponent<TransformComponent>().Translation = { 0.0f, 1.0f, 0.0f };
            auto& script = cube.AddComponent<NativeScriptComponent>();
            script.ClassName = "Rotator";

            Entity sphere = scene->CreateEntity("Sphere");
            auto& sm = sphere.AddComponent<MeshRendererComponent>();
            sm.Type = PrimitiveMesh::Sphere;
            sm.Mat.Albedo = { 0.85f, 0.85f, 0.9f, 1.0f };
            sm.Mat.Metallic = 1.0f;
            sm.Mat.Roughness = 0.2f;
            sphere.GetComponent<TransformComponent>().Translation = { 2.5f, 0.75f, 0.5f };
            sphere.GetComponent<TransformComponent>().Scale = glm::vec3(1.5f);
        }
        SceneSerializer serializer(scene);
        serializer.Serialize(directory / "Assets" / c.StartScene);

        ZM_CORE_INFO("Yangi loyiha yaratildi: {}", projectFile.string());
        return project;
    }

}
