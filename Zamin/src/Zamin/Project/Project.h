#pragma once

#include "Zamin/Asset/AssetTypes.h"
#include "Zamin/Audio/AudioMixer.h"
#include "Zamin/Core/Base.h"
#include "Zamin/Core/InputActions.h"
#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Physics/CollisionLayers.h"
#include "Zamin/Scene/SortingLayers.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    class AssetRegistry;

    struct ProjectConfig
    {
        std::string Name = "Untitled";
        std::filesystem::path StartScene;                 // relative to AssetDirectory
        // UUID of the start scene ("StartSceneUUID"): finds it when it was renamed / moved
        // outside the editor. Written from the asset registry when the project is saved.
        AssetHandle StartSceneHandle;
        std::filesystem::path AssetDirectory = "Assets";  // relative to project dir
        std::filesystem::path SourceDirectory = "Source"; // C++ gameplay scripts
        std::filesystem::path ScriptModulePath;           // relative to project dir, e.g. Binaries/libGame.so
        // Named input actions / axes (Project settings > Input). Projects without
        // an "InputActions" section get the defaults (Jump, Fire, Horizontal, Vertical ...).
        InputActionMap InputActions = InputActionMap::CreateDefault();
        // Gameplay tags offered in the inspector (Project settings > Tags). Entities may
        // still use tags that are not in this list.
        std::vector<std::string> Tags = DefaultTags();
        // Debug drawing in the player (Project settings > General): whether scripts' DebugDraw
        // shapes are shown (the player's --no-debug-draw turns it off) and the physics debug views.
        // Shipping builds have no debug drawing at all. The editor viewport has its own toggles.
        DebugDrawSettings DebugDraw;
        // Named 2D sorting layers, back to front (Project settings > Sorting layers). Projects
        // without a "SortingLayers" section have only "Default".
        SortingLayerList SortingLayers;
        // Named 2D collision layers and which of them collide (Project settings > Physics layers).
        // Projects without a "CollisionLayers2D" section: only "Default", everything collides.
        CollisionLayers2D CollisionLayers;
        // The audio mixer's groups and their defaults, and when sounds stream (Audio Mixer window).
        // Projects without an "Audio" section: Master > Music, SFX, UI at full volume. Applied to
        // the audio engine when the project is loaded; the game may change it while it runs.
        AudioSettings Audio;

        static std::vector<std::string> DefaultTags() { return { "Player", "Enemy", "Pickup" }; }
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
        // Adds a tag to the active project's tag list. Returns true when it was added
        // (false: no project, empty name or already listed).
        static bool RegisterTag(const std::string& tag);
        // Sorting layers of the active project (only "Default" when no project is open).
        static const SortingLayerList& GetSortingLayers();
        // 2D collision layers of the active project (the defaults when no project is open).
        static const CollisionLayers2D& GetCollisionLayers2D();

        // Every asset of the active project with its handle (nullptr without a project). Read from
        // the asset folder on first use (with the snapshot in the cache folder: files moved while the
        // editor was closed keep their handles); the editor keeps it up to date while it runs. An
        // exported game reads the registry file the editor put next to its .zproject instead.
        static AssetRegistry* GetAssetRegistry();
        // The project's cache folder "<project>/.zamin": editor data that can be made again (the
        // asset registry snapshot, thumbnails). Not meant for version control.
        static std::filesystem::path GetCacheDirectory();
        // The asset registry snapshot in the cache folder ("" without a project).
        static std::filesystem::path GetAssetRegistryCachePath();
        // Name of the registry file of an exported game (next to its .zproject).
        static constexpr const char* BuildAssetRegistryFile = "AssetRegistry.zreg";
        // Saves the registry snapshot to the cache folder (a file holding it already is not touched).
        static bool SaveAssetRegistryCache();

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
        Ref<AssetRegistry> m_AssetRegistry;

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
        // The "SortingLayers" section on its own (JSON array of { "ID", "Name" }).
        static std::string SortingLayersToString(const SortingLayerList& layers);
        static bool SortingLayersFromString(const std::string& text, SortingLayerList& out);
    private:
        Ref<Project> m_Project;
    };

}
