#pragma once
#include <filesystem>
namespace Zamin {
    class SceneManager;
    bool* WorldStreamingPanelOpen();
    std::filesystem::path TakeWorldSceneRequest();
    void DrawWorldStreamingPanel(SceneManager* runtime);
}
