#pragma once

#include "Zamin/Scene/Scene.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Scenes are stored as human-readable JSON (*.zscene).
    class Entity;
    struct TransformComponent;

    //
    // Asset references are paths relative to the asset folder ("Texture": "Textures/coin.png").
    // When the active project's asset registry knows the file, its UUID is written next to the
    // path ("TextureUUID": "3f2a..."). Loading uses the path; when that file is gone but the UUID
    // belongs to an asset somewhere else (renamed / moved outside the editor), the asset's new
    // path is used instead and the repair is reported (GetRepairedReferences).
    class SceneSerializer
    {
    public:
        struct AssetReferenceRepair
        {
            std::string OldPath, NewPath; // relative to the asset folder
        };

        explicit SceneSerializer(const Ref<Scene>& scene) : m_Scene(scene) {}

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        std::string SerializeToString();
        bool DeserializeFromString(const std::string& text);
        // Why the last Deserialize / DeserializeFromString call failed ("" after a success). A scene
        // file that cannot be read is reported once with its path (AssetImportLog).
        const std::string& GetLastError() const { return m_LastError; }
        // Asset paths the last Deserialize* call took from UUIDs (the scene in memory differs from
        // its file until it is saved).
        const std::vector<AssetReferenceRepair>& GetRepairedReferences() const { return m_RepairedReferences; }

        // Entity subsets (prefabs, copy/paste). Parents outside the set become roots.
        // includeEditorFlags = false (prefabs): the hierarchy's hidden / locked flags are left
        // out, the inspector component order is kept.
        static std::string SerializeEntities(Scene& scene, const std::vector<Entity>& entities, bool includeEditorFlags = true);
        // Returns created entities, roots first. generateNewUUIDs = true for prefabs/paste.
        // `repairs` receives the asset paths taken from UUIDs (see above).
        // `error` (optional) receives why nothing could be read; the reason is logged only without it.
        static std::vector<Entity> DeserializeEntities(Scene& scene, const std::string& text, bool generateNewUUIDs,
                                                       std::vector<AssetReferenceRepair>* repairs = nullptr, std::string* error = nullptr);

        // Transform clipboard text (inspector Copy / Paste). Deserialize returns false for any
        // text that is not a copied transform.
        static std::string SerializeTransform(const TransformComponent& transform);
        static bool DeserializeTransform(const std::string& text, TransformComponent& out);

        // Component clipboard text (inspector: copy one component, paste it onto other entities).
        // `key` is the component's name in scene files (ComponentKey<T>()); empty text when the
        // entity has no such component.
        static std::string SerializeComponent(Entity entity, std::string_view key);
        // Key of the component in copied text ("" when the text is not a copied component).
        static std::string GetSerializedComponentKey(const std::string& text);
        // Adds the copied component to `target`, or overwrites the values of the one it already
        // has. False (target unchanged) for any other text. Textures / meshes / fonts are not
        // loaded here: call Scene::ResolveAssets() afterwards.
        static bool DeserializeComponent(const std::string& text, Entity target);

    private:
        Ref<Scene> m_Scene;
        std::vector<AssetReferenceRepair> m_RepairedReferences;
        std::string m_LastError;       // why the last Deserialize* call failed
        bool m_QuietErrors = false;    // Deserialize(file) reports the error with the file's path
    };

    // A scene file loaded in steps (SceneManager's asynchronous loading): Parse on any thread - it
    // only reads and parses the file - then Build on the main thread a slice at a time (settings,
    // the entities, then the hierarchy). The result is what SceneSerializer::Deserialize makes.
    class SceneLoadJob
    {
    public:
        SceneLoadJob();
        ~SceneLoadJob();
        SceneLoadJob(const SceneLoadJob&) = delete;
        SceneLoadJob& operator=(const SceneLoadJob&) = delete;

        // Any thread. False with GetError() when the file cannot be read or is not a scene.
        bool Parse(const std::filesystem::path& path);
        bool ParseString(const std::string& text);
        // Any thread, after Parse: the image files the entities name (as written: relative to the
        // assets), each once - decoded ahead of the main thread's texture uploads.
        std::vector<std::string> CollectImagePaths() const;
        size_t GetEntityCount() const;
        // Main thread: makes the scene into `scene` for about `budgetMs` milliseconds. True once it is
        // finished (or failed: GetError()); call again until then.
        bool Build(Scene& scene, double budgetMs);
        bool IsBuilt() const;
        float GetBuildProgress() const; // 0..1
        const std::string& GetError() const;
        const std::vector<SceneSerializer::AssetReferenceRepair>& GetRepairedReferences() const;

    private:
        struct Data;
        std::unique_ptr<Data> m_Data;
    };

}
