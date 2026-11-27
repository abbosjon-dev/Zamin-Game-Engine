// The Visual Script panel's native method support (the node library's C++ section, broken-node diagnostics,
// "Refresh from C++") and its VM profiler data (the heat overlay; VisualScriptProfilerPanel shows the tables).
#include "VisualGraphPanel.h"
#include "VisualGraphLayout.h"
#include "VisualScriptProfilerPanel.h"
#include "Zamin/Editor/EditorLanguage.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include <imgui.h>
#include <algorithm>
#include <format>
namespace Zamin {
    bool VisualGraphPanel::InsertNode(const std::string& kind, std::map<std::string, Visual::Value> inputs, uint32_t* id, bool snapshot)
    {
        uint64_t nextID = 1;
        for (auto& n : m_Graph.Nodes) nextID = std::max(nextID, uint64_t(n.ID) + 1);
        if (nextID > UINT32_MAX || m_Graph.Nodes.size() >= 4096)
        {
            m_Error = Tr("Tugunlar soni chegarasiga yetildi");
            return false;
        }
        if (snapshot) Snapshot();
        const auto added = uint32_t(nextID);
        m_Graph.Nodes.push_back({ added, kind, (100 - m_Pan.x) / m_Zoom, (100 - m_Pan.y) / m_Zoom, std::move(inputs), false, m_Macro });
        m_Selected = added;
        m_Selection = { added };
        if (id) *id = added;
        return true;
    }
    void VisualGraphPanel::RefreshNativeProblems()
    {
        m_NativeProblems = NativeBindingProblems(m_Graph);
        m_NativeGeneration = ScriptRegistry::Generation();
    }
    void VisualGraphPanel::NativePalette(const std::function<bool(std::string)>& matches)
    {
        // Every registered C++ method and free function, found by "c++", "native", its class or its name.
        for (const auto& key : NativeMethodKeys())
        {
            const auto* method = ScriptRegistry::FindMethod(key);
            if (!method || !matches("c++ native " + key)) continue;
            const std::string label = std::string("C++ / ") + key + (method->IsPure ? "" : "  >");
            if (ImGui::Selectable(label.c_str())) AddNative(key);
            GraphLayout::RecordRect("palette/" + key, ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); // (a long name is cut, like the others)
            if (ImGui::IsItemHovered())
            {
                Visual::NativeSignature signature;
                std::string error;
                ImGui::SetTooltip("%s", NativeSignatureOf(key, signature, error) ? (signature.Text() + (signature.Tooltip.empty() ? "" : "\n" + signature.Tooltip)).c_str() : error.c_str());
            }
        }
    }
    bool VisualGraphPanel::AddNative(const std::string& key)
    {
        Visual::NativeSignature live;
        std::string error;
        if (!NativeSignatureOf(key, live, error))
        {
            m_Error = error;
            return false;
        }
        // The C++ enums and structs the method uses become graph enums/structs; a graph type of the same name that
        // differs is refused (nothing changes).
        Visual::Graph next = m_Graph;
        if (!ImportNativeTypes(next, live, error))
        {
            m_Error = error;
            return false;
        }
        if (m_Graph.Nodes.size() >= 4096)
        {
            m_Error = Tr("Tugunlar soni chegarasiga yetildi");
            return false;
        }
        Snapshot();
        m_Graph.Enums = next.Enums;
        m_Graph.Structs = next.Structs;
        // A signature of an older version of the method: the existing nodes take the live one too.
        m_Graph.SetNative(key, live);
        InsertNode(live.Pure ? "Native.Pure" : "Native.Call", { { "Method", key } }, nullptr, false);
        m_Error.clear();
        m_ValidateDirty = true;
        m_Descriptions.clear();
        return true;
    }
    void VisualGraphPanel::NativeProperties(const Visual::Node& node)
    {
        const auto key = Visual::NativeKey(node);
        GraphLayout::PushScope("native");
        GraphLayout::FitText((std::string("C++: ") + key).c_str());
        GraphLayout::RecordKey("method");
        if (auto signature = m_Graph.Natives.find(key); signature != m_Graph.Natives.end())
        {
            ImGui::TextWrapped("%s", signature->second.Text().c_str());
            if (!signature->second.Tooltip.empty()) ImGui::TextDisabled("%s", signature->second.Tooltip.c_str());
        }
        if (auto problem = m_NativeProblems.find(key); problem != m_NativeProblems.end())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .45f, .4f, 1));
            ImGui::TextWrapped("%s", problem->second.c_str());
            ImGui::PopStyleColor();
            GraphLayout::RecordKey("problem");
            Visual::NativeSignature live;
            std::string error;
            const bool available = NativeSignatureOf(key, live, error);
            ImGui::BeginDisabled(!available || !m_Editable);
            // The live signature: new pins appear, the literals and wires that no longer fit go (one undo step).
            if (GraphLayout::Button(Tr("C++ dan yangilash###Refresh from C++")))
            {
                Visual::Graph next = m_Graph;
                if (!ImportNativeTypes(next, live, error, true)) m_Error = error;
                else
                {
                    Snapshot();
                    m_Graph.Enums = next.Enums;
                    m_Graph.Structs = next.Structs;
                    m_Graph.ConformStructValues();
                    m_Graph.SetNative(key, live);
                    m_ValidateDirty = true;
                    m_Descriptions.clear();
                    m_Error.clear();
                }
            }
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextColored({ .35f, .85f, .6f, 1 }, "%s", Tr("C++ metodi ulangan"));
            GraphLayout::RecordKey("bound");
        }
        GraphLayout::PopScope();
    }
    void VisualGraphPanel::CollectProfile()
    {
        m_Profile = {};
        m_ProfileInstances = 0;
        if (m_Editable || !m_Scene || m_Path.empty()) return;
        m_Profile = CollectGraphProfile(*m_Scene, Project::GetAssetRelativePath(m_Path), &m_ProfileInstances);
    }
}
