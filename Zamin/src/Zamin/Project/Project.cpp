#include "Zamin/Project/Project.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Scripting/ScriptTemplate.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Zamin {

    using json = nlohmann::json;

    Ref<Project> Project::New()
    {
        s_ActiveProject = CreateRef<Project>();
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
        return s_ActiveProject;
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
        return { { "Actions", actions }, { "Axes", axes } };
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
        return map;
    }

    std::string ProjectSerializer::InputMapToString(const InputActionMap& map) { return InputMapToJson(map).dump(4); }

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
        json j;
        j["Project"] = {
            { "Name", c.Name },
            { "StartScene", c.StartScene.generic_string() },
            { "AssetDirectory", c.AssetDirectory.generic_string() },
            { "SourceDirectory", c.SourceDirectory.generic_string() },
            { "ScriptModulePath", c.ScriptModulePath.generic_string() },
            { "EngineVersion", "0.1.0" },
        };
        if (c.StartSceneHandle && !c.StartScene.empty())
            j["Project"]["StartSceneUUID"] = c.StartSceneHandle.ToString();
        j["InputActions"] = InputMapToJson(c.InputActions);
        j["Tags"] = c.Tags;
        return FileSystem::WriteTextFile(filepath, j.dump(4));
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath)
    {
        std::string text = FileSystem::ReadTextFile(filepath);
        if (text.empty()) return false;
        try
        {
            json j = json::parse(text);
            const json& p = j.at("Project");
            auto& c = m_Project->GetConfig();
            c.Name = p.value("Name", "Untitled");
            c.StartScene = p.value("StartScene", "");
            c.StartSceneHandle = {};
            if (auto it = p.find("StartSceneUUID"); it != p.end() && it->is_string())
                c.StartSceneHandle = AssetHandle::FromString(it->get<std::string>()).value_or(AssetHandle());
            c.AssetDirectory = p.value("AssetDirectory", "Assets");
            c.SourceDirectory = p.value("SourceDirectory", "Source");
            c.ScriptModulePath = p.value("ScriptModulePath", "");
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
        }
        catch (const std::exception& e)
        {
            ZM_CORE_ERROR("Loyiha fayli o'qilmadi '{}': {}", filepath.string(), e.what());
            return false;
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
