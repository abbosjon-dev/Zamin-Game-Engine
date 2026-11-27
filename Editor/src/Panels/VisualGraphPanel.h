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
        // Automation (ZAMIN_EDITOR_GRAPH_QA): a log line with the open graph's size and compile diagnostics.
        void LogQA(const std::string& name) const;
        // Selects a node of the open graph (QA hook ZAMIN_EDITOR_VISUAL_NODE); false when the graph has no such node.
        bool SelectNode(uint32_t id);
        void SetContext(const Ref<Scene>& scene, bool editable, uint64_t selection);
        void OnImGuiRender(bool* show);
        std::function<void()> OnModified;
        // The open graph's file (empty: none) and the graph itself.
        const std::filesystem::path& GetPath() const { return m_Path; }
        const Visual::Graph& GetGraph() const { return m_Graph; }
        // Opens the Visual Script Profiler panel (the button on the status line).
        std::function<void()> OnOpenProfiler;
        bool HeatOverlay() const { return m_HeatOverlay; }
    private:
        // Adds a node at the canvas's top left (one undo step; false at the node limit). Returns its ID in id.
        bool InsertNode(const std::string& kind, std::map<std::string, Visual::Value> inputs, uint32_t* id = nullptr, bool snapshot = true);
        // Native methods (C++ methods and free functions for graphs, ScriptMethod.h): why each one the graph calls
        // cannot run now (empty: they all bind), recomputed when the graph or the C++ registry (a module load or hot
        // reload) changes. A node of a broken method is drawn red and keeps its pins and wires.
        std::map<std::string, std::string> m_NativeProblems;
        uint64_t m_NativeGeneration = 0;
        void RefreshNativeProblems();
        // The node library's C++ section: every registered method and free function.
        void NativePalette(const std::function<bool(std::string)>& matches);
        // Adds a node of a native method with its live signature (and its C++ enums/structs), one undo step.
        bool AddNative(const std::string& key);
        // The selected native node's signature, binding status and "Refresh from C++".
        void NativeProperties(const Visual::Node& node);
        // The VM profiler while playing: the merged profile of every running instance of the open graph (the heat
        // overlay on the canvas; the Visual Script Profiler panel shows the tables).
        Visual::Machine::Profile m_Profile;
        size_t m_ProfileInstances = 0;
        bool m_HeatOverlay = true;
        void CollectProfile();
        Visual::Graph m_Graph;
        std::filesystem::path m_Path;
        Ref<Scene> m_Scene;
        bool m_Editable = true, m_Dirty = false, m_Focus = false, m_Dragging = false, m_Fit = true;
        uint64_t m_Entity = 0;
        uint32_t m_Selected = 0, m_From = 0;
        std::set<uint32_t> m_Selection;
        Visual::Value m_Clipboard;
        bool m_ValidateDirty = true;
        std::vector<Visual::Diagnostic> m_Diagnostics;
        std::map<uint32_t, Visual::Definition> m_Descriptions;
        const Visual::Definition& Describe(const Visual::Node& node);
        char m_FunctionName[128]{ "Calculate" }, m_MemberName[128]{ "Value" }, m_EnumName[65]{ "Mode" }, m_StructName[65]{ "Stats" }, m_MacroName[65]{ "Step" };
        // A literal editor; an Enum value shows its enum's entries as a combo, a Struct value one editor per field
        // (named: the enum/struct name).
        bool EditLiteral(const char* label, Visual::Type type, Visual::Value& value, const std::string& named = {});
        // A pin/variable type picker: the value types (exec: Exec too, for macro pins), and for Enum/ArrayEnum/Struct/
        // ArrayStruct a second combo of the graph's enums/structs.
        bool TypeCombo(const char* label, Visual::Type& type, std::string& enumName, bool exec = false);
        void Functions();
        void Enums();
        void Structs();
        void Macros();
        // The parent graph (set/clear), the inherited events (override) and the parent chain.
        void Inheritance();
        // Loads the parent chain again (after open, undo/redo and a parent change): inherited members are refreshed.
        void Reinherit();
        // Overrides an inherited function: its signature becomes the graph's own, with an entry -> Parent.Call ->
        // return body (one undo step); likewise an inherited event: the event node -> Parent.Event.
        void OverrideFunction(const std::string& name);
        void OverrideEvent(const std::string& key);
        char m_ParentPath[512]{};
        // The canvas shows the graph itself (empty) or one macro's body; new and pasted nodes go there.
        std::string m_Macro;
        void OpenMacro(const std::string& name);
        bool Visible(const Visual::Node& node) const { return node.Macro == m_Macro; }
        // Collapse the selection into a typed function (Graph::CollapseToFunction), undoable.
        void CollapseSelection();
        // "Opens" a typed function: frames its body (Graph::FunctionNodes), selects its entry, dims other nodes.
        void FocusFunction(const std::string& name);
        std::string m_FocusFunction;
        std::set<uint32_t> m_FocusNodes;
        std::vector<Visual::Diagnostic> m_Warnings;
        Visual::Type m_NewVariableType = Visual::Type::Number, m_NewLocalType = Visual::Type::Number;
        std::string m_NewVariableEnum, m_NewLocalEnum;
        int m_ImportField = 0;
        void CopySelection();
        void PasteSelection(float offset = 40);
        void DeleteSelection();
        std::string m_Output, m_Error;
        ImVec2 m_Pan{ 40, 40 };
        float m_Zoom = 1;
        char m_Search[128]{}, m_Name[128]{}, m_File[512]{ "Scripts/NewGraph.zgraph" };
        std::vector<Visual::Value> m_Undo, m_Redo;
        void Snapshot();
        void Canvas();
        // Canvas statistics of the last frame: the nodes/links drawn and the canvas's CPU time (ms).
        size_t m_DrawnNodes = 0, m_DrawnLinks = 0;
        double m_CanvasMs = 0;
        void Properties();
        void GraphVariables();
        void SceneInstance();
    };
}
