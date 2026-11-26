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
    private:
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
        char m_FunctionName[128]{ "Calculate" }, m_MemberName[128]{ "Value" }, m_EnumName[65]{ "Mode" }, m_StructName[65]{ "Stats" };
        // A literal editor; an Enum value shows its enum's entries as a combo, a Struct value one editor per field
        // (named: the enum/struct name).
        bool EditLiteral(const char* label, Visual::Type type, Visual::Value& value, const std::string& named = {});
        // A pin/variable type picker: the value types, and for Enum/Struct/ArrayStruct a second combo of the graph's
        // enums/structs (structs: false for struct fields, which cannot hold structs).
        bool TypeCombo(const char* label, Visual::Type& type, std::string& enumName, bool structs = true);
        void Functions();
        void Enums();
        void Structs();
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
        void Properties();
        void GraphVariables();
        void SceneInstance();
    };
}
