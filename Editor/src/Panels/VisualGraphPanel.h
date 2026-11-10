#pragma once
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include "Zamin/Core/Base.h"
#include <imgui.h>
#include <functional>
namespace Zamin {
    class Scene;
    class VisualGraphPanel
    {
    public:
        void Open(const std::filesystem::path& path);
        void SetContext(const Ref<Scene>& scene, bool editable, uint64_t selection);
        void OnImGuiRender(bool* show);
        std::function<void()> OnModified;
    private:
        Visual::Graph m_Graph;
        std::filesystem::path m_Path;
        Ref<Scene> m_Scene;
        bool m_Editable = true, m_Dirty = false, m_Focus = false, m_Dragging = false, m_Fit = true;
        uint64_t m_Entity = 0;
        uint32_t m_Selected = 0, m_From = 0;
        std::string m_Output, m_Error;
        ImVec2 m_Pan{ 40, 40 };
        float m_Zoom = 1;
        char m_Search[128]{}, m_Name[128]{}, m_File[512]{ "Scripts/NewGraph.zgraph" };
        std::vector<Visual::Value> m_Undo, m_Redo;
        void Snapshot();
        void Canvas();
        void Properties();
    };
}
