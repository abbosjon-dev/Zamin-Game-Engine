#pragma once
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include "Zamin/Core/Base.h"
#include <functional>
#include <string>
namespace Zamin {
    class Scene;
    // The merged VM profile of every running instance of a graph in a scene (graph: its Assets-relative path) and
    // how many there are; loaded: the first instance's graph as the VM runs it (node kinds for the tables).
    Visual::Machine::Profile CollectGraphProfile(Scene& scene, const std::string& graph, size_t* instances = nullptr, Visual::Graph* loaded = nullptr);

    // The Visual Script Profiler: while the game plays (in the editor), the VM profiler of the graph the Visual Script
    // panel has open, merged over its running instances - per node calls, inclusive and exclusive time and their
    // share, per typed function the same, and the hot path (the most taken execution path from the costliest
    // entry). A row selects its node in the Visual Script panel. The last profile stays after Play stops.
    class VisualScriptProfilerPanel
    {
    public:
        void SetContext(const Ref<Scene>& scene, bool playing, const std::string& graph, const Visual::Graph* source);
        void OnImGuiRender(bool* show);
        // A node row was clicked: select it in the Visual Script panel.
        std::function<void(uint32_t)> OnSelectNode;
        // The editor turns the VM profiler on while this panel or the heat overlay needs it.
        bool Recording() const { return m_Record; }
    private:
        Ref<Scene> m_Scene;
        bool m_Playing = false, m_Record = true;
        std::string m_Graph;
        const Visual::Graph* m_Source = nullptr;
        Visual::Machine::Profile m_Profile;
        Visual::Graph m_Loaded;
        size_t m_Instances = 0;
        int m_Sort = 2; // 0 node, 1 calls, 2 exclusive, 3 inclusive
        std::string NodeName(uint32_t id) const;
    };
}
