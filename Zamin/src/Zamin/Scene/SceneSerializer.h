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

    class SceneSerializer
    {
    public:
        explicit SceneSerializer(const Ref<Scene>& scene) : m_Scene(scene) {}

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        std::string SerializeToString();
        bool DeserializeFromString(const std::string& text);

        // Entity subsets (prefabs, copy/paste). Parents outside the set become roots.
        // includeEditorFlags = false (prefabs): the hierarchy's hidden / locked flags are left
        // out, the inspector component order is kept.
        static std::string SerializeEntities(Scene& scene, const std::vector<Entity>& entities, bool includeEditorFlags = true);
        // Returns created entities, roots first. generateNewUUIDs = true for prefabs/paste.
        static std::vector<Entity> DeserializeEntities(Scene& scene, const std::string& text, bool generateNewUUIDs);

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
    };

}
