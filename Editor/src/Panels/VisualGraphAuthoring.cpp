#include "VisualGraphPanel.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
namespace Zamin {
    const Visual::Definition& VisualGraphPanel::Describe(const Visual::Node& node)
    {
        if (!m_Descriptions.contains(node.ID)) m_Descriptions[node.ID] = m_Graph.Describe(node);
        return m_Descriptions.at(node.ID);
    }
    void VisualGraphPanel::CopySelection()
    {
        auto selection = m_Selection;
        if (selection.empty() && m_Selected) selection.insert(m_Selected);
        m_Clipboard = m_Graph.CopyNodes(selection);
    }
    void VisualGraphPanel::PasteSelection(float offset)
    {
        if (m_Clipboard.is_null()) return;
        auto candidate = m_Graph;
        std::set<uint32_t> ids;
        std::string error;
        if (candidate.PasteNodes(m_Clipboard, offset, offset, ids, error))
        {
            Snapshot();
            m_Graph = std::move(candidate);
            m_Selection = std::move(ids);
            m_Selected = m_Selection.empty() ? 0 : *m_Selection.begin();
            m_From = 0;
        }
        else m_Error = error;
    }
    void VisualGraphPanel::DeleteSelection()
    {
        auto selected = m_Selection;
        if (selected.empty() && m_Selected) selected.insert(m_Selected);
        if (selected.empty()) return;
        Snapshot();
        std::erase_if(m_Graph.Links, [&](const auto& link) { return selected.contains(link.From) || selected.contains(link.To); });
        std::erase_if(m_Graph.Nodes, [&](const auto& node) { return selected.contains(node.ID); });
        m_Selected = m_From = 0;
        m_Selection.clear();
    }
    void VisualGraphPanel::Functions()
    {
        using namespace Visual;
        if (!ImGui::CollapsingHeader("Typed functions")) return;
        ImGui::InputText("Function name", m_FunctionName, sizeof(m_FunctionName));
        if (ImGui::Button("Create function") && m_FunctionName[0] && m_Graph.Functions.size() < 64 && !m_Graph.Functions.contains(m_FunctionName))
        {
            Snapshot();
            const std::string name = m_FunctionName;
            m_Graph.Functions[name] = { { { "X", Type::Number, 1. } }, { { "Result", Type::Number, 0. } }, {} };
            uint32_t id = 0;
            for (const auto& node : m_Graph.Nodes) id = std::max(id, node.ID);
            if (id > UINT32_MAX - 2)
            {
                m_Error = "Node ID space exhausted";
                m_Graph.Functions.erase(name);
                return;
            }
            m_Graph.Nodes.push_back({ id + 1, "Function.Entry", 40, 600, { { "Name", name } } });
            m_Graph.Nodes.push_back({ id + 2, "Function.Return", 380, 600, { { "Name", name } } });
            m_Graph.Links.push_back({ id + 1, "Then", id + 2, "In" });
            m_Graph.Links.push_back({ id + 1, "X", id + 2, "Result" });
            m_Fit = true;
        }
        std::string erase, renameFrom, renameTo;
        for (auto& [name, signature] : m_Graph.Functions)
        {
            ImGui::PushID(name.c_str());
            if (ImGui::TreeNode("signature", "%s (%zu -> %zu)", name.c_str(), signature.Inputs.size(), signature.Outputs.size()))
            {
                char renamed[129]{};
                std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
                if (ImGui::InputText("Rename function", renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    renameFrom = name;
                    renameTo = renamed;
                }
                if (ImGui::Button("Add call node"))
                {
                    Snapshot();
                    uint32_t id = 0;
                    for (const auto& node : m_Graph.Nodes) id = std::max(id, node.ID);
                    if (id < UINT32_MAX)
                    {
                        m_Graph.Nodes.push_back({ id + 1, "Function.Call", 100, 400, { { "Name", name } } });
                        m_Selected = id + 1;
                        m_Selection = { id + 1 };
                        m_Fit = true;
                    }
                }
                auto draft = signature;
                bool changed = false;
                auto pins = [&](const char* title, std::vector<Pin>& list) {
                    if (!ImGui::TreeNode(title)) return;
                    size_t remove = SIZE_MAX;
                    for (size_t i = 0; i < list.size(); ++i)
                    {
                        ImGui::PushID(int(i));
                        char key[129]{};
                        std::snprintf(key, sizeof(key), "%s", list[i].Name.c_str());
                        if (ImGui::InputText("Pin name", key, sizeof(key)))
                        {
                            list[i].Name = key;
                            changed = true;
                        }
                        int type = int(list[i].Kind);
                        auto combo = [](void*, int index) { return TypeName(Type(index)); };
                        if (ImGui::Combo("Pin type", &type, combo, nullptr, int(Type::Count)) && type > 0)
                        {
                            list[i].Kind = Type(type);
                            list[i].Default = DefaultValue(Type(type));
                            changed = true;
                        }
                        auto value = list[i].Default;
                        // Defaults use the same strongly typed literal editor as node pins.
                        if (EditLiteral("Default", list[i].Kind, value))
                        {
                            list[i].Default = std::move(value);
                            changed = true;
                        }
                        if (ImGui::SmallButton("Remove pin")) remove = i;
                        ImGui::PopID();
                    }
                    if (remove != SIZE_MAX)
                    {
                        list.erase(list.begin() + remove);
                        changed = true;
                    }
                    if (list.size() < 32 && ImGui::Button("Add pin"))
                    {
                        list.push_back({ "Value" + std::to_string(list.size() + 1), Type::Number, 0. });
                        changed = true;
                    }
                    ImGui::TreePop();
                };
                pins("Parameters", draft.Inputs);
                pins("Returns", draft.Outputs);
                if (ImGui::TreeNode("Local variables"))
                {
                    ImGui::InputText("Local name", m_MemberName, sizeof(m_MemberName));
                    static int localType = 2;
                    auto combo = [](void*, int index) { return TypeName(Type(index)); };
                    ImGui::Combo("Local type", &localType, combo, nullptr, int(Type::Count));
                    if (localType > 0 && m_MemberName[0] && draft.Locals.size() < 32 && ImGui::Button("Add local"))
                    {
                        draft.Locals[m_MemberName] = { Type(localType), DefaultValue(Type(localType)) };
                        changed = true;
                    }
                    std::string removed;
                    for (const auto& [key, local] : draft.Locals)
                    {
                        ImGui::PushID(key.c_str());
                        ImGui::Text("%s (%s)", key.c_str(), TypeName(local.Kind));
                        auto value = local.Default;
                        if (EditLiteral("Default", local.Kind, value))
                        {
                            draft.Locals.at(key).Default = std::move(value);
                            changed = true;
                        }
                        if (ImGui::SmallButton("Remove local")) removed = key;
                        ImGui::PopID();
                    }
                    if (!removed.empty())
                    {
                        draft.Locals.erase(removed);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                if (changed)
                {
                    Snapshot();
                    signature = std::move(draft);
                    const auto removed = m_Graph.PruneInvalidPins();
                    if (removed) m_Error = std::to_string(removed) + " incompatible pins/wires removed; Undo restores them";
                }
                if (ImGui::Button("Delete function and its nodes")) erase = name;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (!renameFrom.empty())
        {
            Snapshot();
            if (!m_Graph.RenameFunction(renameFrom, renameTo)) m_Error = "Function rename rejected";
        }
        if (!erase.empty())
        {
            Snapshot();
            std::set<uint32_t> nodes;
            for (const auto& node : m_Graph.Nodes)
                if (node.Kind.starts_with("Function.") && node.Inputs.contains("Name") && node.Inputs.at("Name") == erase) nodes.insert(node.ID);
            std::erase_if(m_Graph.Nodes, [&](const auto& node) { return nodes.contains(node.ID); });
            std::erase_if(m_Graph.Links, [&](const auto& link) { return nodes.contains(link.From) || nodes.contains(link.To); });
            m_Graph.Functions.erase(erase);
        }
    }
}
