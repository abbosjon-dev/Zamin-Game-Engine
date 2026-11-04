#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Core/InputActions.h"

#include <filesystem>
#include <string>

namespace Zamin {

    struct ProjectConfig
    {
        std::string Name = "Untitled";
        std::filesystem::path StartScene;                 // relative to AssetDirectory
        std::filesystem::path AssetDirectory = "Assets";  // relative to project dir
        std::filesystem::path SourceDirectory = "Source"; // C++ gameplay scripts
        std::filesystem::path ScriptModulePath;           // relative to project dir, e.g. Binaries/libGame.so
        // Named input actions / axes (Project settings > Input). Projects without
        // an "InputActions" section get the defaults (Jump, Fire, Horizontal, Vertical ...).
        InputActionMap InputActions = InputActionMap::CreateDefault();
    };

    // A game project: <Name>.zproject + Assets/ + Source/ (C++ scripts) + CMakeLists.txt
    class Project
    {
    public:
        ProjectConfig& GetConfig() { return m_Config; }

        static Ref<Project> GetActive() { return s_ActiveProject; }
        static Ref<Project> New();
        static Ref<Project> Load(const std::filesystem::path& path);
        static bool SaveActive(const std::filesystem::path& path);
        static void SetActive(const Ref<Project>& project) { s_ActiveProject = project; }

        static std::filesystem::path GetProjectDirectory();
        static std::filesystem::path GetProjectFilePath();
        static std::filesystem::path GetAssetDirectory();
        static std::filesystem::path GetSourceDirectory();
        static std::filesystem::path GetScriptModulePath();
        // Input map of the active project (the default map when no project is open).
        static const InputActionMap& GetInputActionMap();

        // Relative asset path -> absolute file system path (works without a project, too).
        static std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& path);
        // Absolute path -> path relative to the asset directory (generic '/' separators).
        static std::string GetAssetRelativePath(const std::filesystem::path& path);

        // Creates a ready-to-build project from the built-in template.
        static Ref<Project> CreateFromTemplate(const std::filesystem::path& directory, const std::string& name);

        // Writes CMakeLists.txt for the C++ script module (pointing to this engine build).
        static bool WriteBuildFiles(const std::filesystem::path& projectDir, const std::string& name);

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;
        std::filesystem::path m_ProjectFile;

        static inline Ref<Project> s_ActiveProject;
    };

    class ProjectSerializer
    {
    public:
        explicit ProjectSerializer(Ref<Project> project) : m_Project(std::move(project)) {}
        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        // The "InputActions" section on its own (JSON), used by tests and the editor.
        static std::string InputMapToString(const InputActionMap& map);
        static bool InputMapFromString(const std::string& text, InputActionMap& out);
    private:
        Ref<Project> m_Project;
    };

}
