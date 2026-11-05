#pragma once

#include "Zamin/Scene/Scene.h"

#include <filesystem>
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
        // Asset paths the last Deserialize* call took from UUIDs (the scene in memory differs from
        // its file until it is saved).
        const std::vector<AssetReferenceRepair>& GetRepairedReferences() const { return m_RepairedReferences; }

        // Entity subsets (prefabs, copy/paste). Parents outside the set become roots.
        // includeEditorFlags = false (prefabs): the hierarchy's hidden / locked flags are left
        // out, the inspector component order is kept.
        static std::string SerializeEntities(Scene& scene, const std::vector<Entity>& entities, bool includeEditorFlags = true);
        // Returns created entities, roots first. generateNewUUIDs = true for prefabs/paste.
        // `repairs` receives the asset paths taken from UUIDs (see above).
        static std::vector<Entity> DeserializeEntities(Scene& scene, const std::string& text, bool generateNewUUIDs,
                                                       std::vector<AssetReferenceRepair>* repairs = nullptr);

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
    };

}
