#pragma once

#include "Zamin/Scene/Scene.h"

#include <filesystem>
#include <string>

namespace Zamin {

    // Scenes are stored as human-readable JSON (*.zscene).
    class SceneSerializer
    {
    public:
        explicit SceneSerializer(const Ref<Scene>& scene) : m_Scene(scene) {}

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        std::string SerializeToString();
        bool DeserializeFromString(const std::string& text);

    private:
        Ref<Scene> m_Scene;
    };

}
