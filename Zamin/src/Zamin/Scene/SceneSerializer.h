#pragma once

#include "Zamin/Scene/Scene.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    // Scenes are stored as human-readable JSON (*.zscene).
    class Entity;

    class SceneSerializer
    {
    public:
        explicit SceneSerializer(const Ref<Scene>& scene) : m_Scene(scene) {}

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        std::string SerializeToString();
        bool DeserializeFromString(const std::string& text);

        // Entity subsets (prefabs, copy/paste). Parents outside the set become roots.
        static std::string SerializeEntities(Scene& scene, const std::vector<Entity>& entities);
        // Returns created entities, roots first. generateNewUUIDs = true for prefabs/paste.
        static std::vector<Entity> DeserializeEntities(Scene& scene, const std::string& text, bool generateNewUUIDs);

    private:
        Ref<Scene> m_Scene;
    };

}
