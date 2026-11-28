#include "VisualGraphPanel.h"
#include "VisualGraphLayout.h"
#include "Zamin/Editor/EditorLanguage.h"
#include <algorithm>
#include <cstdio>
#include <set>
// The graph's network RPCs (format 7, docs/Networking.md): a name, who runs it (Server: a client calls it, the
// server runs it; Client: the server calls it on the entity owner's client; Multicast: the server and every client),
// reliable or not, whether any client may call a server RPC, and typed parameters. Event.Rpc handles one; the
// Net.Call*Rpc nodes call it; both take their parameter pins from here.
namespace Zamin {
    void VisualGraphPanel::NetworkRpcs()
    {
        using namespace Visual;
        const bool sectionOpen = ImGui::CollapsingHeader(Tr("Tarmoq RPC'lari###Network RPCs"));
        GraphLayout::Record(Tr("Tarmoq RPC'lari###Network RPCs"));
        if (!sectionOpen) return;
        GraphLayout::PushScope("rpcs");
        struct ScopeGuard
        {
            ~ScopeGuard() { GraphLayout::PopScope(); }
        } scopeGuard;
        ImGui::InputText(GraphLayout::Field(Tr("RPC nomi###RPC name")).c_str(), m_RpcName, sizeof(m_RpcName));
        GraphLayout::Record(Tr("RPC nomi###RPC name"));
        if (GraphLayout::Button(Tr("RPC yaratish###Create RPC")))
        {
            if (!m_RpcName[0] || m_Graph.Rpcs.contains(m_RpcName) || m_Graph.Rpcs.size() >= MaxRpcs) m_Error = Tr("RPC nomi band yoki bo'sh");
            else
            {
                // A new RPC: a server RPC without parameters, and its handler on the canvas.
                Snapshot();
                m_Graph.Rpcs[m_RpcName] = {};
                InsertNode("Event.Rpc", { { "Name", std::string(m_RpcName) } }, nullptr, false);
                m_Error.clear();
            }
        }
        static const char* kinds[] = { "Server", "Client", "Multicast" };
        std::string erase;
        for (auto& [name, rpc] : m_Graph.Rpcs)
        {
            ImGui::PushID(name.c_str());
            const bool inherited = m_Graph.IsInherited("RPC", name);
            const bool treeOpen = GraphLayout::TreeNode("rpc", (inherited ? name + " [" + Tr("meros") + "]" : name) + " (" + RpcTargetName(rpc.Kind) + ", " + std::to_string(rpc.Params.size()) + ")", name);
            GraphLayout::PushScope(name);
            if (treeOpen)
            {
                ImGui::BeginDisabled(inherited);
                auto draft = rpc;
                bool changed = false;
                int kind = int(draft.Kind);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##kind", &kind, kinds, 3))
                {
                    draft.Kind = RpcDefinition::Target(kind);
                    if (draft.Kind != RpcDefinition::Target::Server) draft.AnyClient = false;
                    changed = true;
                }
                GraphLayout::RecordKey("Kind");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Tr("Server: mijoz chaqiradi, serverda ishlaydi. Client: server chaqiradi, egasining mijozida ishlaydi. Multicast: server chaqiradi, serverda va har bir mijozda ishlaydi"));
                changed |= ImGui::Checkbox(Tr("Ishonchli###Reliable"), &draft.Reliable);
                GraphLayout::Record(Tr("Ishonchli###Reliable"));
                if (draft.Kind == RpcDefinition::Target::Server)
                {
                    changed |= ImGui::Checkbox(Tr("Istalgan mijoz###Any client"), &draft.AnyClient);
                    GraphLayout::Record(Tr("Istalgan mijoz###Any client"));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("O'chirilgan: faqat obyekt egasi chaqira oladi"));
                }
                const bool paramsOpen = ImGui::TreeNode(Tr("Parametrlar###Parameters"));
                GraphLayout::Record(Tr("Parametrlar###Parameters"));
                if (paramsOpen)
                {
                    GraphLayout::PushScope("Params");
                    size_t remove = SIZE_MAX;
                    for (size_t i = 0; i < draft.Params.size(); ++i)
                    {
                        auto& pin = draft.Params[i];
                        ImGui::PushID(int(i));
                        GraphLayout::PushScope(std::to_string(i));
                        if (i) ImGui::Separator();
                        char key[65]{};
                        std::snprintf(key, sizeof(key), "%s", pin.Name.c_str());
                        if (ImGui::InputText(GraphLayout::Field(Tr("Parametr nomi###Parameter name")).c_str(), key, sizeof(key), ImGuiInputTextFlags_EnterReturnsTrue) && pin.Name != key)
                        {
                            const std::string to = key;
                            const bool taken = to.empty() || to == "Name" || to == "Sender" || to == "In" || to == "Then" ||
                                               std::any_of(draft.Params.begin(), draft.Params.end(), [&](const Pin& p) { return p.Name == to; });
                            if (taken) m_Error = Tr("Pin nomi band, bo'sh yoki zaxiralangan");
                            else
                            {
                                // The literals and wires of the RPC's nodes follow the rename.
                                for (auto& node : m_Graph.Nodes)
                                    if ((node.Kind == "Event.Rpc" || node.Kind.starts_with("Net.Call")) && node.Inputs.contains("Name") && node.Inputs.at("Name") == name)
                                    {
                                        if (auto literal = node.Inputs.extract(pin.Name)) node.Inputs[to] = std::move(literal.mapped());
                                        for (auto& link : m_Graph.Links)
                                        {
                                            if (link.From == node.ID && link.Output == pin.Name) link.Output = to;
                                            if (link.To == node.ID && link.Input == pin.Name) link.Input = to;
                                        }
                                    }
                                pin.Name = to;
                                changed = true;
                            }
                        }
                        GraphLayout::Record(Tr("Parametr nomi###Parameter name"));
                        auto type = pin.Kind;
                        auto named = pin.Enum;
                        if (TypeCombo(Tr("Parametr turi###Parameter type"), type, named, false))
                        {
                            pin.Kind = type;
                            pin.Enum = IsNamed(type) ? named : "";
                            pin.Default = m_Graph.DefaultFor(type, pin.Enum);
                            changed = true;
                        }
                        if (GraphLayout::SmallButton(Tr("Parametrni olib tashlash###Remove parameter"))) remove = i;
                        GraphLayout::PopScope();
                        ImGui::PopID();
                    }
                    if (remove != SIZE_MAX)
                    {
                        draft.Params.erase(draft.Params.begin() + ptrdiff_t(remove));
                        changed = true;
                    }
                    if (draft.Params.size() < MaxRpcParams && GraphLayout::Button(Tr("Parametr qo'shish###Add parameter")))
                    {
                        std::string pinName = "Value" + std::to_string(draft.Params.size() + 1);
                        while (std::any_of(draft.Params.begin(), draft.Params.end(), [&](const Pin& p) { return p.Name == pinName; })) pinName += "_";
                        draft.Params.push_back({ pinName, Type::Number, 0. });
                        changed = true;
                    }
                    GraphLayout::PopScope();
                    ImGui::TreePop();
                }
                if (changed)
                {
                    // One undo step: the definition, and the wires / literals of its nodes that no longer fit.
                    Snapshot();
                    rpc = std::move(draft);
                    // A node of the old kind becomes the call node of the new one.
                    for (auto& node : m_Graph.Nodes)
                        if (node.Kind.starts_with("Net.Call") && node.Inputs.contains("Name") && node.Inputs.at("Name") == name) node.Kind = RpcCallKind(rpc.Kind);
                    const auto removed = m_Graph.PruneInvalidPins();
                    if (removed) m_Error = std::to_string(removed) + " " + Tr("mos kelmaydigan pin/bog'lanish olib tashlandi; Undo ularni qaytaradi");
                    m_Descriptions.clear();
                    m_ValidateDirty = true;
                }
                if (GraphLayout::Button(Tr("Chaqiruv tugunini qo'shish###Add call node"))) InsertNode(RpcCallKind(rpc.Kind), { { "Name", name } });
                GraphLayout::SameLineIfFits(GraphLayout::ButtonWidth(Tr("Ishlovchi tugunni qo'shish###Add handler node")));
                if (m_Macro.empty() && GraphLayout::Button(Tr("Ishlovchi tugunni qo'shish###Add handler node"))) InsertNode("Event.Rpc", { { "Name", name } });
                if (GraphLayout::Button(Tr("RPC va uning tugunlarini o'chirish###Delete RPC and its nodes"))) erase = name;
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            GraphLayout::PopScope();
            ImGui::PopID();
        }
        if (!erase.empty())
        {
            // The definition and every node of it go, with their wires (Undo restores them).
            Snapshot();
            std::set<uint32_t> nodes;
            for (const auto& node : m_Graph.Nodes)
                if ((node.Kind == "Event.Rpc" || node.Kind.starts_with("Net.Call")) && node.Inputs.contains("Name") && node.Inputs.at("Name") == erase) nodes.insert(node.ID);
            std::erase_if(m_Graph.Nodes, [&](const auto& node) { return nodes.contains(node.ID); });
            std::erase_if(m_Graph.Links, [&](const auto& link) { return nodes.contains(link.From) || nodes.contains(link.To); });
            m_Graph.Rpcs.erase(erase);
            m_Selection.clear();
            m_Selected = m_From = 0;
        }
    }
}
