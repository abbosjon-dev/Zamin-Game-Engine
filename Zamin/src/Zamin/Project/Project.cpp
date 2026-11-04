#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

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
    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath)
    {
        const auto& c = m_Project->GetConfig();
        json j;
        j["Project"] = {
            { "Name", c.Name },
            { "StartScene", c.StartScene.generic_string() },
            { "AssetDirectory", c.AssetDirectory.generic_string() },
            { "SourceDirectory", c.SourceDirectory.generic_string() },
            { "ScriptModulePath", c.ScriptModulePath.generic_string() },
            { "EngineVersion", "0.1.0" },
        };
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
            c.AssetDirectory = p.value("AssetDirectory", "Assets");
            c.SourceDirectory = p.value("SourceDirectory", "Source");
            c.ScriptModulePath = p.value("ScriptModulePath", "");
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

    static const char* s_TemplateScripts = R"(// O'yin skriptlari (C++). Har bir klass ZM_REGISTER_SCRIPT orqali ro'yxatdan o'tadi
// va Editor'da "Native Script" komponentida tanlanadi.
#include <Zamin/Zamin.h>

using namespace Zamin;

// Obyektni doimiy aylantiradi.
class Rotator : public ScriptableEntity
{
public:
    glm::vec3 Speed = { 0.0f, 1.0f, 0.0f }; // radian/sekund

    void OnUpdate(Timestep ts) override
    {
        Transform().Rotation += Speed * (float)ts;
    }
};
ZM_REGISTER_SCRIPT(Rotator, ZM_FIELD(Rotator, Speed))

// WASD bilan harakatlanadigan obyekt.
class PlayerController : public ScriptableEntity
{
public:
    float MoveSpeed = 5.0f;

    void OnCreate() override
    {
        ZM_INFO("PlayerController ishga tushdi: {}", GetName());
    }

    void OnUpdate(Timestep ts) override
    {
        glm::vec3 dir(0.0f);
        if (Input::IsKeyPressed(Key::W)) dir.z -= 1.0f;
        if (Input::IsKeyPressed(Key::S)) dir.z += 1.0f;
        if (Input::IsKeyPressed(Key::A)) dir.x -= 1.0f;
        if (Input::IsKeyPressed(Key::D)) dir.x += 1.0f;
        if (glm::length(dir) > 0.0f)
            Transform().Translation += glm::normalize(dir) * MoveSpeed * (float)ts;
    }
};
ZM_REGISTER_SCRIPT(PlayerController, ZM_FIELD(PlayerController, MoveSpeed))
)";

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
            FileSystem::WriteTextFile(directory / "Source" / "Scripts.cpp", s_TemplateScripts);
        FileSystem::WriteTextFile(directory / ".gitignore", "Build/\nBinaries/\n");

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
