#include "VisualGraphPanel.h"
#include "VisualGraphLayout.h"
#include "Zamin/Editor/EditorLanguage.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Project/Project.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
namespace Zamin {
    const Visual::Definition& VisualGraphPanel::Describe(const Visual::Node& node)
    {
        if (!m_Descriptions.contains(node.ID)) m_Descriptions[node.ID] = m_Graph.Describe(node);
        return m_Descriptions.at(node.ID);
    }
    bool VisualGraphPanel::TypeCombo(const char* label, Visual::Type& type, std::string& enumName, bool exec)
    {
        using namespace Visual;
        bool changed = false;
        // The whole type list at once (no scrolling inside the list).
        const bool open = ImGui::BeginCombo(GraphLayout::Field(label).c_str(), TypeName(type), ImGuiComboFlags_HeightLargest);
        GraphLayout::Record(label);
        if (open)
        {
            for (int i = exec ? 0 : 1; i < int(Type::Count); ++i)
            {
                const bool structured = Type(i) == Type::Struct || Type(i) == Type::ArrayStruct;
                const bool enumerated = Type(i) == Type::Enum || Type(i) == Type::ArrayEnum;
                ImGui::PushID(i);
                const bool picked = ImGui::Selectable(TypeName(Type(i)), Type(i) == type);
                // Automation: the open list's entries as "popup/<type>" (drawn over the column, never scrolled to).
                GraphLayout::RecordRect(std::string("popup/") + TypeName(Type(i)), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                if (picked && Type(i) != type)
                {
                    if (enumerated && m_Graph.Enums.empty()) m_Error = Tr("Avval enum yarating");
                    else if (structured && m_Graph.Structs.empty()) m_Error = Tr("Avval struktura yarating");
                    else
                    {
                        type = Type(i);
                        enumName = enumerated ? m_Graph.Enums.begin()->first : structured ? m_Graph.Structs.begin()->first : "";
                        changed = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        // The second combo picks the enum / struct the type names.
        auto pick = [&](const char* title, const auto& definitions) {
            ImGui::PushID(label);
            const bool pickOpen = ImGui::BeginCombo(GraphLayout::Field(title).c_str(), enumName.c_str());
            GraphLayout::Record(title);
            if (pickOpen)
            {
                for (const auto& [name, definition] : definitions)
                {
                    const bool picked = ImGui::Selectable(name.c_str(), name == enumName);
                    GraphLayout::RecordRect("popup/" + name, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                    if (picked && name != enumName)
                    {
                        enumName = name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        };
        if (type == Type::Enum || type == Type::ArrayEnum) pick(Tr("Enum turi###Enum type"), m_Graph.Enums);
        if (type == Type::Struct || type == Type::ArrayStruct) pick(Tr("Struktura turi###Struct type"), m_Graph.Structs);
        return changed;
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
        if (candidate.PasteNodes(m_Clipboard, offset, offset, ids, error, m_Macro))
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
        // A macro's entry and exit stay: they are its pins (deleting the macro removes them).
        std::erase_if(selected, [&](uint32_t id) {
            const auto* node = m_Graph.Find(id);
            return !node || node->Kind == "Macro.Entry" || node->Kind == "Macro.Exit";
        });
        if (selected.empty()) return;
        Snapshot();
        std::erase_if(m_Graph.Links, [&](const auto& link) { return selected.contains(link.From) || selected.contains(link.To); });
        std::erase_if(m_Graph.Nodes, [&](const auto& node) { return selected.contains(node.ID); });
        m_Selected = m_From = 0;
        m_Selection.clear();
    }
    void VisualGraphPanel::CollapseSelection()
    {
        auto selected = m_Selection;
        if (selected.empty() && m_Selected) selected.insert(m_Selected);
        auto candidate = m_Graph;
        uint32_t call = 0;
        std::string error;
        if (!candidate.CollapseToFunction(selected, m_FunctionName, call, error))
        {
            m_Error = error;
            return;
        }
        Snapshot();
        m_Graph = std::move(candidate);
        m_Selected = call;
        m_Selection = { call };
        m_From = 0;
        m_Error.clear();
    }
    void VisualGraphPanel::FocusFunction(const std::string& name)
    {
        m_Macro.clear(); // functions live in the graph itself
        m_FocusNodes = m_Graph.FunctionNodes(name);
        if (m_FocusNodes.empty())
        {
            m_FocusFunction.clear();
            return;
        }
        m_FocusFunction = name;
        for (const auto& node : m_Graph.Nodes)
            if (node.Kind == "Function.Entry" && node.Inputs.contains("Name") && node.Inputs.at("Name") == name)
            {
                m_Selected = node.ID;
                m_Selection = { node.ID };
            }
        m_Fit = true;
    }
    void VisualGraphPanel::Functions()
    {
        using namespace Visual;
        const bool sectionOpen = ImGui::CollapsingHeader(Tr("Tiplangan funksiyalar###Typed functions"));
        GraphLayout::Record(Tr("Tiplangan funksiyalar###Typed functions"));
        if (!sectionOpen) return;
        GraphLayout::PushScope("functions");
        struct ScopeGuard
        {
            ~ScopeGuard() { GraphLayout::PopScope(); }
        } scopeGuard;
        ImGui::InputText(GraphLayout::Field(Tr("Funksiya nomi###Function name")).c_str(), m_FunctionName, sizeof(m_FunctionName));
        GraphLayout::Record(Tr("Funksiya nomi###Function name"));
        if (GraphLayout::Button(Tr("Funksiya yaratish###Create function")) && m_FunctionName[0] && m_Graph.Functions.size() < 64 && !m_Graph.Functions.contains(m_FunctionName))
        {
            Snapshot();
            const std::string name = m_FunctionName;
            m_Graph.Functions[name] = { { { "X", Type::Number, 1. } }, { { "Result", Type::Number, 0. } }, {} };
            uint32_t id = 0;
            for (const auto& node : m_Graph.Nodes) id = std::max(id, node.ID);
            if (id > UINT32_MAX - 2)
            {
                m_Error = Tr("Tugunlar soni chegarasiga yetildi");
                m_Graph.Functions.erase(name);
                return;
            }
            m_Graph.Nodes.push_back({ id + 1, "Function.Entry", 40, 600, { { "Name", name } } });
            m_Graph.Nodes.push_back({ id + 2, "Function.Return", 380, 600, { { "Name", name } } });
            m_Graph.Links.push_back({ id + 1, "Then", id + 2, "In" });
            m_Graph.Links.push_back({ id + 1, "X", id + 2, "Result" });
            m_Fit = true;
        }
        std::string erase, renameFrom, renameTo, open, overrideName;
        const auto overrides = m_Graph.FunctionOverrides();
        for (auto& [name, signature] : m_Graph.Functions)
        {
            // An inherited function is read-only until it is overridden; an override keeps the parent's signature.
            const bool inherited = m_Graph.IsInherited("Function", name), overriding = overrides.contains(name);
            const std::string mark = inherited ? std::string(" [") + Tr("meros") + "]" : overriding ? std::string(" [") + Tr("qayta yozilgan") + "]" : "";
            ImGui::PushID(name.c_str());
            const bool treeOpen = GraphLayout::TreeNode("signature", name + " (" + std::to_string(signature.Inputs.size()) + " -> " + std::to_string(signature.Outputs.size()) + ")" + mark, name);
            GraphLayout::PushScope(name);
            if (treeOpen)
            {
                if (inherited)
                {
                    GraphLayout::FitText((std::string(Tr("meros")) + ": " + (m_Graph.Inherited.Chain.empty() ? m_Graph.Parent : m_Graph.Inherited.Chain.front())).c_str(), true);
                    GraphLayout::RecordKey("inherited");
                    if (GraphLayout::Button(Tr("Funksiyani qayta yozish###Override function"))) overrideName = name;
                }
                if (overriding)
                {
                    GraphLayout::FitText(Tr("Ota grafdagi funksiyani qayta yozadi"), true);
                    GraphLayout::RecordKey("override");
                    if (GraphLayout::Button(Tr("Ota funksiya chaqiruvini qo'shish###Add call parent node")))
                    {
                        uint32_t id = 0;
                        for (const auto& node : m_Graph.Nodes) id = std::max(id, node.ID);
                        if (id < UINT32_MAX && m_Graph.Nodes.size() < 4096)
                        {
                            Snapshot();
                            m_Graph.Nodes.push_back({ id + 1, "Parent.Call", (100 - m_Pan.x) / m_Zoom, (100 - m_Pan.y) / m_Zoom, { { "Name", name } } });
                            m_Selected = id + 1;
                            m_Selection = { id + 1 };
                        }
                    }
                }
                ImGui::BeginDisabled(inherited || overriding);
                char renamed[129]{};
                std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
                if (ImGui::InputText(GraphLayout::Field(Tr("Funksiyani qayta nomlash###Rename function")).c_str(), renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    renameFrom = name;
                    renameTo = renamed;
                }
                GraphLayout::Record(Tr("Funksiyani qayta nomlash###Rename function"));
                ImGui::EndDisabled();
                if (GraphLayout::Button(Tr("Chaqiruv tugunini qo'shish###Add call node")))
                {
                    uint32_t id = 0;
                    for (const auto& node : m_Graph.Nodes) id = std::max(id, node.ID);
                    if (id < UINT32_MAX && m_Graph.Nodes.size() < 4096)
                    {
                        Snapshot();
                        m_Graph.Nodes.push_back({ id + 1, "Function.Call", (100 - m_Pan.x) / m_Zoom, (100 - m_Pan.y) / m_Zoom, { { "Name", name } } });
                        m_Selected = id + 1;
                        m_Selection = { id + 1 };
                    }
                }
                GraphLayout::SameLineIfFits(GraphLayout::ButtonWidth(Tr("Funksiyani ochish###Open function")));
                if (!inherited && GraphLayout::Button(Tr("Funksiyani ochish###Open function"))) open = name;
                auto draft = signature;
                bool changed = false;
                std::vector<std::pair<std::string, std::string>> pinRenames[2];
                auto pins = [&](const char* title, std::vector<Pin>& list, int side) {
                    const bool listOpen = ImGui::TreeNode(title);
                    GraphLayout::Record(title);
                    if (!listOpen) return;
                    GraphLayout::PushScope(side == 0 ? "Parameters" : "Returns");
                    size_t remove = SIZE_MAX;
                    for (size_t i = 0; i < list.size(); ++i)
                    {
                        ImGui::PushID(int(i));
                        GraphLayout::PushScope(std::to_string(i));
                        if (i) ImGui::Separator();
                        char key[129]{};
                        std::snprintf(key, sizeof(key), "%s", list[i].Name.c_str());
                        // Enter renames; the wires of the entry, calls and returns follow (Graph::RenameFunctionPin).
                        const bool renamedPin = ImGui::InputText(GraphLayout::Field(Tr("Pin nomi###Pin name")).c_str(), key, sizeof(key), ImGuiInputTextFlags_EnterReturnsTrue);
                        GraphLayout::Record(Tr("Pin nomi###Pin name"));
                        if (renamedPin && list[i].Name != key) pinRenames[side].push_back({ list[i].Name, key });
                        auto type = list[i].Kind;
                        auto enumName = list[i].Enum;
                        if (TypeCombo(Tr("Pin turi###Pin type"), type, enumName))
                        {
                            list[i].Kind = type;
                            list[i].Enum = IsNamed(type) ? enumName : "";
                            list[i].Default = m_Graph.DefaultFor(type, list[i].Enum);
                            changed = true;
                        }
                        auto value = list[i].Default;
                        // Defaults use the same strongly typed literal editor as node pins.
                        if (EditLiteral(Tr("Boshlang'ich qiymat"), list[i].Kind, value, list[i].Enum))
                        {
                            list[i].Default = std::move(value);
                            changed = true;
                        }
                        if (GraphLayout::SmallButton(Tr("Pinni olib tashlash###Remove pin"))) remove = i;
                        GraphLayout::PopScope();
                        ImGui::PopID();
                    }
                    if (remove != SIZE_MAX)
                    {
                        list.erase(list.begin() + remove);
                        changed = true;
                    }
                    if (list.size() < 32 && GraphLayout::Button(Tr("Pin qo'shish###Add pin")))
                    {
                        std::string pinName = "Value" + std::to_string(list.size() + 1);
                        while (std::any_of(list.begin(), list.end(), [&](const Pin& p) { return p.Name == pinName; })) pinName += "_";
                        list.push_back({ pinName, Type::Number, 0. });
                        changed = true;
                    }
                    GraphLayout::PopScope();
                    ImGui::TreePop();
                };
                ImGui::BeginDisabled(inherited || overriding);
                pins(Tr("Parametrlar###Parameters"), draft.Inputs, 0);
                pins(Tr("Natijalar###Returns"), draft.Outputs, 1);
                ImGui::EndDisabled();
                ImGui::BeginDisabled(inherited);
                const bool localsOpen = ImGui::TreeNode(Tr("Lokal o'zgaruvchilar###Local variables"));
                GraphLayout::Record(Tr("Lokal o'zgaruvchilar###Local variables"));
                if (localsOpen)
                {
                    GraphLayout::PushScope("Locals");
                    ImGui::InputText(GraphLayout::Field(Tr("Lokal nomi###Local name")).c_str(), m_MemberName, sizeof(m_MemberName));
                    GraphLayout::Record(Tr("Lokal nomi###Local name"));
                    TypeCombo(Tr("Lokal turi###Local type"), m_NewLocalType, m_NewLocalEnum);
                    if (m_MemberName[0] && draft.Locals.size() < 32 && GraphLayout::Button(Tr("Lokal qo'shish###Add local")))
                    {
                        const std::string named = IsNamed(m_NewLocalType) ? m_NewLocalEnum : "";
                        const auto initial = m_Graph.DefaultFor(m_NewLocalType, named);
                        if (m_Graph.Fits(m_NewLocalType, named, initial))
                        {
                            draft.Locals[m_MemberName] = { m_NewLocalType, initial, named };
                            changed = true;
                        }
                    }
                    std::string removed;
                    for (const auto& [key, local] : draft.Locals)
                    {
                        ImGui::PushID(key.c_str());
                        GraphLayout::PushScope(key);
                        ImGui::Separator();
                        GraphLayout::FitText((key + " (" + TypeName(local.Kind) + (local.Enum.empty() ? "" : " " + local.Enum) + ")").c_str());
                        auto value = local.Default;
                        if (EditLiteral(Tr("Boshlang'ich qiymat"), local.Kind, value, local.Enum))
                        {
                            draft.Locals.at(key).Default = std::move(value);
                            changed = true;
                        }
                        if (GraphLayout::SmallButton(Tr("Lokalni olib tashlash###Remove local"))) removed = key;
                        GraphLayout::PopScope();
                        ImGui::PopID();
                    }
                    if (!removed.empty())
                    {
                        draft.Locals.erase(removed);
                        changed = true;
                    }
                    GraphLayout::PopScope();
                    ImGui::TreePop();
                }
                if (changed || !pinRenames[0].empty() || !pinRenames[1].empty())
                {
                    Snapshot();
                    if (changed) signature = std::move(draft);
                    bool rejected = false;
                    for (int side = 0; side < 2; ++side)
                        for (const auto& [from, to] : pinRenames[side]) rejected |= !m_Graph.RenameFunctionPin(name, side == 1, from, to);
                    if (rejected) m_Error = Tr("Pin nomi band, bo'sh yoki zaxiralangan");
                    const auto removed = m_Graph.PruneInvalidPins();
                    if (removed) m_Error = std::to_string(removed) + " " + Tr("mos kelmaydigan pin/bog'lanish olib tashlandi; Undo ularni qaytaradi");
                }
                ImGui::EndDisabled();
                // Deleting an override makes the parent's function the graph's again.
                if (!inherited && GraphLayout::Button(overriding ? Tr("Qayta yozishni olib tashlash###Remove override") : Tr("Funksiya va uning tugunlarini o'chirish###Delete function and its nodes"))) erase = name;
                ImGui::TreePop();
            }
            GraphLayout::PopScope();
            ImGui::PopID();
        }
        if (!renameFrom.empty())
        {
            auto candidate = m_Graph;
            if (candidate.RenameFunction(renameFrom, renameTo))
            {
                Snapshot();
                m_Graph = std::move(candidate);
                if (m_FocusFunction == renameFrom) m_FocusFunction = renameTo;
            }
            else m_Error = Tr("Funksiya nomi band yoki bo'sh");
        }
        if (!erase.empty())
        {
            Snapshot();
            std::set<uint32_t> nodes;
            for (const auto& node : m_Graph.Nodes)
                if ((node.Kind.starts_with("Function.") || node.Kind == "Parent.Call") && node.Inputs.contains("Name") && node.Inputs.at("Name") == erase) nodes.insert(node.ID);
            std::erase_if(m_Graph.Nodes, [&](const auto& node) { return nodes.contains(node.ID); });
            std::erase_if(m_Graph.Links, [&](const auto& link) { return nodes.contains(link.From) || nodes.contains(link.To); });
            m_Graph.Functions.erase(erase);
            if (m_FocusFunction == erase) m_FocusFunction.clear();
            if (overrides.contains(erase)) Reinherit();
        }
        if (!open.empty()) FocusFunction(open);
        if (!overrideName.empty()) OverrideFunction(overrideName);
    }
    void VisualGraphPanel::Enums()
    {
        using namespace Visual;
        const bool sectionOpen = ImGui::CollapsingHeader(Tr("Enum turlari###Graph enums"));
        GraphLayout::Record(Tr("Enum turlari###Graph enums"));
        if (!sectionOpen) return;
        GraphLayout::PushScope("enums");
        struct ScopeGuard
        {
            ~ScopeGuard() { GraphLayout::PopScope(); }
        } scopeGuard;
        ImGui::InputText(GraphLayout::Field(Tr("Enum nomi###Enum name")).c_str(), m_EnumName, sizeof(m_EnumName));
        GraphLayout::Record(Tr("Enum nomi###Enum name"));
        if (GraphLayout::Button(Tr("Enum yaratish###Create enum")))
        {
            if (!m_EnumName[0] || m_Graph.Enums.contains(m_EnumName) || m_Graph.Enums.size() >= 64) m_Error = Tr("Enum nomi band yoki bo'sh");
            else
            {
                Snapshot();
                m_Graph.Enums[m_EnumName] = { { { "None", 0 } }, "" };
            }
        }
        // Reflected C++ enum fields of the loaded script classes (ZM_FIELD_ENUM) can be imported as graph enums.
        std::vector<std::pair<std::string, std::string>> fields;
        for (const auto& [className, info] : ScriptRegistry::GetAll())
            for (const auto& field : info.Fields)
                if (field.Type == ScriptFieldType::Enum && !field.EnumOptions.empty()) fields.push_back({ className, field.Name });
        if (fields.empty()) GraphLayout::FitText(Tr("Yuklangan C++ enum maydonlari yo'q"), true);
        else
        {
            m_ImportField = std::clamp(m_ImportField, 0, int(fields.size()) - 1);
            const auto label = [&](int i) { return fields[size_t(i)].first + "." + fields[size_t(i)].second; };
            if (ImGui::BeginCombo(GraphLayout::Field(Tr("C++ enum maydoni###C++ enum field")).c_str(), label(m_ImportField).c_str()))
            {
                for (int i = 0; i < int(fields.size()); ++i)
                    if (ImGui::Selectable(label(i).c_str(), i == m_ImportField)) m_ImportField = i;
                ImGui::EndCombo();
            }
            if (GraphLayout::Button(Tr("C++ maydonidan import qilish###Import from C++ field")))
            {
                EnumDefinition definition;
                std::string error;
                const auto& [className, fieldName] = fields[size_t(m_ImportField)];
                if (!ReflectedEnum(className, fieldName, definition, error)) m_Error = error;
                else if (m_Graph.Enums.size() >= 64) m_Error = Tr("Enum nomi band yoki bo'sh");
                else
                {
                    std::string name = fieldName;
                    for (int i = 2; m_Graph.Enums.contains(name); ++i) name = fieldName + std::to_string(i);
                    Snapshot();
                    m_Graph.Enums[name] = std::move(definition);
                }
            }
        }
        std::string erase, renameFrom, renameTo;
        for (auto& [name, definition] : m_Graph.Enums)
        {
            ImGui::PushID(name.c_str());
            // An inherited enum is read-only here: its parent owns it.
            const bool inherited = m_Graph.IsInherited("Enum", name);
            const bool treeOpen = GraphLayout::TreeNode("enum", (inherited ? name + " [" + Tr("meros") + "]" : name) + " (" + std::to_string(definition.Entries.size()) + ")", name);
            GraphLayout::PushScope(name);
            if (treeOpen)
            {
                ImGui::BeginDisabled(inherited);
                char renamed[65]{};
                std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
                if (ImGui::InputText(GraphLayout::Field(Tr("Enumni qayta nomlash###Rename enum")).c_str(), renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    renameFrom = name;
                    renameTo = renamed;
                }
                GraphLayout::Record(Tr("Enumni qayta nomlash###Rename enum"));
                if (!definition.Source.empty()) GraphLayout::FitText((std::string(Tr("Manba")) + ": " + definition.Source).c_str(), true);
                auto draft = definition;
                bool changed = false;
                size_t remove = SIZE_MAX;
                std::pair<std::string, std::string> entryRename;
                for (size_t i = 0; i < draft.Entries.size(); ++i)
                {
                    ImGui::PushID(int(i));
                    GraphLayout::PushScope(std::to_string(i));
                    if (i) ImGui::Separator();
                    char key[65]{};
                    std::snprintf(key, sizeof(key), "%s", draft.Entries[i].Name.c_str());
                    const bool renamedEntry = ImGui::InputText(GraphLayout::Field(Tr("Qiymat nomi###Entry name")).c_str(), key, sizeof(key), ImGuiInputTextFlags_EnterReturnsTrue);
                    GraphLayout::Record(Tr("Qiymat nomi###Entry name"));
                    if (renamedEntry && draft.Entries[i].Name != key) entryRename = { draft.Entries[i].Name, key };
                    // Committed on Enter (a half-typed number must not collide with another entry's value).
                    char number[32]{};
                    std::snprintf(number, sizeof(number), "%lld", (long long)draft.Entries[i].Value);
                    char* end = nullptr;
                    int64_t value = draft.Entries[i].Value;
                    if (ImGui::InputText(GraphLayout::Field(Tr("Son qiymati###Entry value")).c_str(), number, sizeof(number), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal))
                        value = std::strtoll(number, &end, 10);
                    GraphLayout::Record(Tr("Son qiymati###Entry value"));
                    if (end && end != number && value != draft.Entries[i].Value)
                    {
                        if (draft.Find(value) || std::abs(value) > (int64_t(1) << 53)) m_Error = Tr("Son qiymati band yoki chegaradan tashqarida");
                        else
                        {
                            draft.Entries[i].Value = value;
                            changed = true;
                        }
                    }
                    if (draft.Entries.size() > 1 && GraphLayout::SmallButton(Tr("Qiymatni olib tashlash###Remove entry"))) remove = i;
                    GraphLayout::PopScope();
                    ImGui::PopID();
                }
                if (remove != SIZE_MAX)
                {
                    draft.Entries.erase(draft.Entries.begin() + remove);
                    changed = true;
                }
                if (draft.Entries.size() < 256 && GraphLayout::Button(Tr("Qiymat qo'shish###Add entry")))
                {
                    int64_t value = 0;
                    for (const auto& entry : draft.Entries) value = std::max(value, entry.Value + 1);
                    std::string entryName = "Entry" + std::to_string(draft.Entries.size() + 1);
                    while (draft.Find(entryName)) entryName += "_";
                    draft.Entries.push_back({ entryName, value });
                    changed = true;
                }
                if (changed || !entryRename.first.empty())
                {
                    Snapshot();
                    if (changed) definition = std::move(draft);
                    if (!entryRename.first.empty() && !m_Graph.RenameEnumEntry(name, entryRename.first, entryRename.second)) m_Error = Tr("Qiymat nomi band yoki bo'sh");
                    const auto removed = m_Graph.PruneInvalidPins();
                    if (removed) m_Error = std::to_string(removed) + " " + Tr("mos kelmaydigan pin/bog'lanish olib tashlandi; Undo ularni qaytaradi");
                }
                if (GraphLayout::Button(Tr("Enumni o'chirish###Delete enum"))) erase = name;
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            GraphLayout::PopScope();
            ImGui::PopID();
        }
        if (!renameFrom.empty())
        {
            auto candidate = m_Graph;
            if (candidate.RenameEnum(renameFrom, renameTo))
            {
                Snapshot();
                m_Graph = std::move(candidate);
            }
            else m_Error = Tr("Enum nomi band yoki bo'sh");
        }
        if (!erase.empty())
        {
            if (const auto uses = m_Graph.EnumUses(erase)) m_Error = std::to_string(uses) + " " + Tr("joyda ishlatilgan enum o'chirilmaydi");
            else
            {
                Snapshot();
                m_Graph.Enums.erase(erase);
            }
        }
    }
}
namespace Zamin {
    void VisualGraphPanel::Structs()
    {
        using namespace Visual;
        const bool sectionOpen = ImGui::CollapsingHeader(Tr("Strukturalar###Graph structs"));
        GraphLayout::Record(Tr("Strukturalar###Graph structs"));
        if (!sectionOpen) return;
        GraphLayout::PushScope("structs");
        struct ScopeGuard
        {
            ~ScopeGuard() { GraphLayout::PopScope(); }
        } scopeGuard;
        ImGui::InputText(GraphLayout::Field(Tr("Struktura nomi###Struct name")).c_str(), m_StructName, sizeof(m_StructName));
        GraphLayout::Record(Tr("Struktura nomi###Struct name"));
        if (GraphLayout::Button(Tr("Struktura yaratish###Create struct")))
        {
            if (!m_StructName[0] || m_Graph.Structs.contains(m_StructName) || m_Graph.Structs.size() >= 64) m_Error = Tr("Struktura nomi band yoki bo'sh");
            else
            {
                Snapshot();
                m_Graph.Structs[m_StructName] = { { { "Value", Type::Number, 0. } } };
            }
        }
        std::string erase, renameFrom, renameTo;
        for (auto& [name, definition] : m_Graph.Structs)
        {
            ImGui::PushID(name.c_str());
            // An inherited struct is read-only here: its parent owns it.
            const bool inherited = m_Graph.IsInherited("Struct", name);
            const bool treeOpen = GraphLayout::TreeNode("struct", (inherited ? name + " [" + Tr("meros") + "]" : name) + " {" + std::to_string(definition.Fields.size()) + "}", name);
            GraphLayout::PushScope(name);
            if (treeOpen)
            {
                ImGui::BeginDisabled(inherited);
                char renamed[65]{};
                std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
                if (ImGui::InputText(GraphLayout::Field(Tr("Strukturani qayta nomlash###Rename struct")).c_str(), renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    renameFrom = name;
                    renameTo = renamed;
                }
                GraphLayout::Record(Tr("Strukturani qayta nomlash###Rename struct"));
                auto draft = definition;
                bool changed = false;
                size_t remove = SIZE_MAX;
                std::pair<std::string, std::string> fieldRename;
                for (size_t i = 0; i < draft.Fields.size(); ++i)
                {
                    auto& field = draft.Fields[i];
                    ImGui::PushID(int(i));
                    GraphLayout::PushScope(std::to_string(i));
                    if (i) ImGui::Separator();
                    char key[65]{};
                    std::snprintf(key, sizeof(key), "%s", field.Name.c_str());
                    // Enter renames; Make/Break wires, GetField/SetField literals and stored values follow (Graph::RenameStructField).
                    const bool renamedField = ImGui::InputText(GraphLayout::Field(Tr("Maydon nomi###Field name")).c_str(), key, sizeof(key), ImGuiInputTextFlags_EnterReturnsTrue);
                    GraphLayout::Record(Tr("Maydon nomi###Field name"));
                    if (renamedField && field.Name != key) fieldRename = { field.Name, key };
                    auto type = field.Kind;
                    auto named = field.Enum;
                    if (TypeCombo(Tr("Maydon turi###Field type"), type, named))
                    {
                        field.Kind = type;
                        field.Enum = IsNamed(type) ? named : "";
                        field.Default = m_Graph.DefaultFor(type, field.Enum);
                        changed = true;
                    }
                    auto value = field.Default;
                    if (EditLiteral(Tr("Boshlang'ich qiymat"), field.Kind, value, field.Enum))
                    {
                        field.Default = std::move(value);
                        changed = true;
                    }
                    if (draft.Fields.size() > 1 && GraphLayout::SmallButton(Tr("Maydonni olib tashlash###Remove field"))) remove = i;
                    GraphLayout::PopScope();
                    ImGui::PopID();
                }
                if (remove != SIZE_MAX)
                {
                    draft.Fields.erase(draft.Fields.begin() + remove);
                    changed = true;
                }
                if (draft.Fields.size() < 32 && GraphLayout::Button(Tr("Maydon qo'shish###Add field")))
                {
                    std::string fieldName = "Field" + std::to_string(draft.Fields.size() + 1);
                    while (draft.Find(fieldName)) fieldName += "_";
                    draft.Fields.push_back({ fieldName, Type::Number, 0. });
                    changed = true;
                }
                // A field of struct type may not make the struct contain itself.
                std::string cycle;
                if (changed)
                {
                    Graph probe;
                    probe.Structs = m_Graph.Structs;
                    probe.Structs[name] = draft;
                    cycle = probe.StructCycle();
                }
                if (!cycle.empty()) m_Error = std::string(Tr("Struktura o'zini o'z ichiga ololmaydi")) + ": " + cycle;
                else if (changed || !fieldRename.first.empty())
                {
                    // One undo step: the definition, the renamed field's wires, the conformed values and the pruned pins.
                    Snapshot();
                    if (changed) definition = std::move(draft);
                    if (!fieldRename.first.empty() && !m_Graph.RenameStructField(name, fieldRename.first, fieldRename.second)) m_Error = Tr("Maydon nomi band, bo'sh yoki zaxiralangan");
                    m_Graph.ConformStructValues();
                    const auto removed = m_Graph.PruneInvalidPins();
                    if (removed) m_Error = std::to_string(removed) + " " + Tr("mos kelmaydigan pin/bog'lanish olib tashlandi; Undo ularni qaytaradi");
                }
                if (GraphLayout::Button(Tr("Strukturani o'chirish###Delete struct"))) erase = name;
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            GraphLayout::PopScope();
            ImGui::PopID();
        }
        if (!renameFrom.empty())
        {
            auto candidate = m_Graph;
            if (candidate.RenameStruct(renameFrom, renameTo))
            {
                Snapshot();
                m_Graph = std::move(candidate);
            }
            else m_Error = Tr("Struktura nomi band yoki bo'sh");
        }
        if (!erase.empty())
        {
            if (const auto uses = m_Graph.StructUses(erase)) m_Error = std::to_string(uses) + " " + Tr("joyda ishlatilgan struktura o'chirilmaydi");
            else
            {
                Snapshot();
                m_Graph.Structs.erase(erase);
            }
        }
    }
}
namespace Zamin {
    void VisualGraphPanel::OpenMacro(const std::string& name)
    {
        if (!name.empty() && !m_Graph.Macros.contains(name)) return;
        m_Macro = name;
        m_FocusFunction.clear();
        m_FocusNodes.clear();
        m_Selection.clear();
        m_Selected = m_From = 0;
        for (const auto& node : m_Graph.Nodes)
            if (!name.empty() && node.Macro == name && node.Kind == "Macro.Entry") m_Selected = node.ID;
        m_Fit = true;
    }
    void VisualGraphPanel::Macros()
    {
        using namespace Visual;
        const bool sectionOpen = ImGui::CollapsingHeader(Tr("Makroslar###Macros"));
        GraphLayout::Record(Tr("Makroslar###Macros"));
        if (!sectionOpen) return;
        GraphLayout::PushScope("macros");
        struct ScopeGuard
        {
            ~ScopeGuard() { GraphLayout::PopScope(); }
        } scopeGuard;
        ImGui::InputText(GraphLayout::Field(Tr("Makro nomi###Macro name")).c_str(), m_MacroName, sizeof(m_MacroName));
        GraphLayout::Record(Tr("Makro nomi###Macro name"));
        if (GraphLayout::Button(Tr("Makro yaratish###Create macro")))
        {
            uint32_t id = 0;
            for (const auto& node : m_Graph.Nodes) id = std::max(id, node.ID);
            if (!m_MacroName[0] || m_Graph.Macros.contains(m_MacroName) || m_Graph.Macros.size() >= 64) m_Error = Tr("Makro nomi band yoki bo'sh");
            else if (id > UINT32_MAX - 2 || m_Graph.Nodes.size() + 2 > 4096) m_Error = Tr("Tugunlar soni chegarasiga yetildi");
            else
            {
                // A new macro passes its execution straight through: In -> Then, between its entry and exit.
                Snapshot();
                const std::string name = m_MacroName;
                m_Graph.Macros[name] = { { { "In", Type::Exec, nullptr } }, { { "Then", Type::Exec, nullptr } } };
                m_Graph.Nodes.push_back({ id + 1, "Macro.Entry", 40, 60, {}, false, name });
                m_Graph.Nodes.push_back({ id + 2, "Macro.Exit", 520, 60, {}, false, name });
                m_Graph.Links.push_back({ id + 1, "In", id + 2, "Then" });
                m_Error.clear();
            }
        }
        std::string erase, renameFrom, renameTo, open;
        for (auto& [name, macro] : m_Graph.Macros)
        {
            ImGui::PushID(name.c_str());
            // An inherited macro is read-only here: its parent owns it.
            const bool inherited = m_Graph.IsInherited("Macro", name);
            const bool treeOpen = GraphLayout::TreeNode("macro", (inherited ? name + " [" + Tr("meros") + "]" : name) + " (" + std::to_string(macro.Inputs.size()) + " -> " + std::to_string(macro.Outputs.size()) + ")", name);
            GraphLayout::PushScope(name);
            if (treeOpen)
            {
                ImGui::BeginDisabled(inherited);
                char renamed[65]{};
                std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
                if (ImGui::InputText(GraphLayout::Field(Tr("Makroni qayta nomlash###Rename macro")).c_str(), renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    renameFrom = name;
                    renameTo = renamed;
                }
                GraphLayout::Record(Tr("Makroni qayta nomlash###Rename macro"));
                // An instance goes to the open view (not into the macro's own body: a macro cannot use itself).
                if (m_Macro != name && GraphLayout::Button(Tr("Makro tugunini qo'shish###Add macro node")))
                {
                    uint32_t id = 0;
                    for (const auto& node : m_Graph.Nodes) id = std::max(id, node.ID);
                    if (id < UINT32_MAX && m_Graph.Nodes.size() < 4096)
                    {
                        Snapshot();
                        m_Graph.Nodes.push_back({ id + 1, "Macro.Instance", (100 - m_Pan.x) / m_Zoom, (100 - m_Pan.y) / m_Zoom, { { "Name", name } }, false, m_Macro });
                        m_Selected = id + 1;
                        m_Selection = { id + 1 };
                    }
                }
                GraphLayout::SameLineIfFits(GraphLayout::ButtonWidth(Tr("Makroni ochish###Open macro")));
                if (GraphLayout::Button(Tr("Makroni ochish###Open macro"))) open = name;
                auto draft = macro;
                bool changed = false;
                std::vector<std::pair<std::string, std::string>> pinRenames[2];
                auto pins = [&](const char* title, std::vector<Pin>& list, int side) {
                    const bool listOpen = ImGui::TreeNode(title);
                    GraphLayout::Record(title);
                    if (!listOpen) return;
                    GraphLayout::PushScope(side == 0 ? "Inputs" : "Outputs");
                    size_t remove = SIZE_MAX;
                    for (size_t i = 0; i < list.size(); ++i)
                    {
                        ImGui::PushID(int(i));
                        GraphLayout::PushScope(std::to_string(i));
                        if (i) ImGui::Separator();
                        char key[65]{};
                        std::snprintf(key, sizeof(key), "%s", list[i].Name.c_str());
                        // Enter renames; the wires and literals of the entry/exit and the instances follow (Graph::RenameMacroPin).
                        const bool renamedPin = ImGui::InputText(GraphLayout::Field(Tr("Pin nomi###Pin name")).c_str(), key, sizeof(key), ImGuiInputTextFlags_EnterReturnsTrue);
                        GraphLayout::Record(Tr("Pin nomi###Pin name"));
                        if (renamedPin && list[i].Name != key) pinRenames[side].push_back({ list[i].Name, key });
                        auto type = list[i].Kind;
                        auto named = list[i].Enum;
                        // A macro pin may be an execution pin (Exec) or a data pin of any value type.
                        if (TypeCombo(Tr("Pin turi###Pin type"), type, named, true))
                        {
                            list[i].Kind = type;
                            list[i].Enum = IsNamed(type) ? named : "";
                            list[i].Default = type == Type::Exec ? Value() : m_Graph.DefaultFor(type, list[i].Enum);
                            changed = true;
                        }
                        auto value = list[i].Default;
                        if (list[i].Kind != Type::Exec && EditLiteral(Tr("Boshlang'ich qiymat"), list[i].Kind, value, list[i].Enum))
                        {
                            list[i].Default = std::move(value);
                            changed = true;
                        }
                        if (GraphLayout::SmallButton(Tr("Pinni olib tashlash###Remove pin"))) remove = i;
                        GraphLayout::PopScope();
                        ImGui::PopID();
                    }
                    if (remove != SIZE_MAX)
                    {
                        list.erase(list.begin() + remove);
                        changed = true;
                    }
                    if (list.size() < 32 && GraphLayout::Button(Tr("Pin qo'shish###Add pin")))
                    {
                        std::string pinName = "Value" + std::to_string(list.size() + 1);
                        while (std::any_of(list.begin(), list.end(), [&](const Pin& p) { return p.Name == pinName; })) pinName += "_";
                        list.push_back({ pinName, Type::Number, 0. });
                        changed = true;
                    }
                    GraphLayout::PopScope();
                    ImGui::TreePop();
                };
                pins(Tr("Kirishlar###Inputs"), draft.Inputs, 0);
                pins(Tr("Chiqishlar###Outputs"), draft.Outputs, 1);
                if (changed || !pinRenames[0].empty() || !pinRenames[1].empty())
                {
                    // One undo step: the pins, the renamed pins' wires and the wires/literals that no longer fit.
                    Snapshot();
                    if (changed) macro = std::move(draft);
                    bool rejected = false;
                    for (int side = 0; side < 2; ++side)
                        for (const auto& [from, to] : pinRenames[side]) rejected |= !m_Graph.RenameMacroPin(name, side == 1, from, to);
                    if (rejected) m_Error = Tr("Pin nomi band, bo'sh yoki zaxiralangan");
                    const auto removed = m_Graph.PruneInvalidPins();
                    if (removed) m_Error = std::to_string(removed) + " " + Tr("mos kelmaydigan pin/bog'lanish olib tashlandi; Undo ularni qaytaradi");
                }
                if (GraphLayout::Button(Tr("Makro va uning tugunlarini o'chirish###Delete macro and its nodes"))) erase = name;
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            GraphLayout::PopScope();
            ImGui::PopID();
        }
        if (!renameFrom.empty())
        {
            auto candidate = m_Graph;
            if (candidate.RenameMacro(renameFrom, renameTo))
            {
                Snapshot();
                m_Graph = std::move(candidate);
                if (m_Macro == renameFrom) m_Macro = renameTo;
            }
            else m_Error = Tr("Makro nomi band yoki bo'sh");
        }
        if (!erase.empty())
        {
            // The macro's body and every instance of it go, with their wires (Undo restores them).
            Snapshot();
            std::set<uint32_t> nodes;
            for (const auto& node : m_Graph.Nodes)
                if (node.Macro == erase || (node.Kind == "Macro.Instance" && node.Inputs.contains("Name") && node.Inputs.at("Name") == erase)) nodes.insert(node.ID);
            std::erase_if(m_Graph.Nodes, [&](const auto& node) { return nodes.contains(node.ID); });
            std::erase_if(m_Graph.Links, [&](const auto& link) { return nodes.contains(link.From) || nodes.contains(link.To); });
            m_Graph.Macros.erase(erase);
            if (m_Macro == erase) OpenMacro("");
            m_Selection.clear();
            m_Selected = m_From = 0;
        }
        if (!open.empty()) OpenMacro(open);
    }
}
namespace Zamin {
    void VisualGraphPanel::Reinherit()
    {
        if (!m_Graph.Parent.empty() || m_Graph.Inherited.Base) m_Graph.LoadParent(Project::GetAssetDirectory(), m_Path);
        std::snprintf(m_ParentPath, sizeof(m_ParentPath), "%s", m_Graph.Parent.c_str());
        m_ValidateDirty = true;
        m_Descriptions.clear();
    }
    void VisualGraphPanel::OverrideFunction(const std::string& name)
    {
        using namespace Visual;
        if (!m_Graph.IsInherited("Function", name)) return;
        uint32_t id = 0;
        float bottom = 0;
        for (const auto& node : m_Graph.Nodes)
        {
            id = std::max(id, node.ID);
            if (node.Macro.empty()) bottom = std::max(bottom, node.Y + 140);
        }
        if (id > UINT32_MAX - 3 || m_Graph.Nodes.size() + 3 > 4096)
        {
            m_Error = Tr("Tugunlar soni chegarasiga yetildi");
            return;
        }
        // The signature becomes the graph's own (the same pins); the body runs the parent's version and returns its
        // results: entry -> Parent.Call -> return, every parameter and result wired through.
        Snapshot();
        m_Graph.Inherited.Functions.erase(name);
        const auto& signature = m_Graph.Functions.at(name);
        m_Graph.Nodes.push_back({ id + 1, "Function.Entry", 40, bottom, { { "Name", name } } });
        m_Graph.Nodes.push_back({ id + 2, "Parent.Call", 340, bottom, { { "Name", name } } });
        m_Graph.Nodes.push_back({ id + 3, "Function.Return", 660, bottom, { { "Name", name } } });
        m_Graph.Links.push_back({ id + 1, "Then", id + 2, "In" });
        m_Graph.Links.push_back({ id + 2, "Then", id + 3, "In" });
        for (const auto& pin : signature.Inputs) m_Graph.Links.push_back({ id + 1, pin.Name, id + 2, pin.Name });
        for (const auto& pin : signature.Outputs) m_Graph.Links.push_back({ id + 2, pin.Name, id + 3, pin.Name });
        m_Error.clear();
        OpenMacro("");
        FocusFunction(name);
    }
    void VisualGraphPanel::OverrideEvent(const std::string& key)
    {
        if (!m_Graph.IsInherited("Event", key) || m_Graph.EventOverrides().contains(key)) return;
        const bool custom = key.starts_with("Custom.");
        const std::string kind = custom ? "Event.Custom" : "Event." + key;
        if (!Visual::Catalog().contains(kind)) return;
        uint32_t id = 0;
        float bottom = 0;
        for (const auto& node : m_Graph.Nodes)
        {
            id = std::max(id, node.ID);
            if (node.Macro.empty()) bottom = std::max(bottom, node.Y + 140);
        }
        if (id > UINT32_MAX - 2 || m_Graph.Nodes.size() + 2 > 4096)
        {
            m_Error = Tr("Tugunlar soni chegarasiga yetildi");
            return;
        }
        // The graph's own handler of the event replaces the parent's; Parent.Event runs the parent's first.
        Snapshot();
        std::map<std::string, Visual::Value> inputs;
        if (custom) inputs["Name"] = key.substr(7);
        m_Graph.Nodes.push_back({ id + 1, kind, 40, bottom, std::move(inputs) });
        m_Graph.Nodes.push_back({ id + 2, "Parent.Event", 340, bottom, { { "Name", key } } });
        m_Graph.Links.push_back({ id + 1, "Then", id + 2, "In" });
        OpenMacro("");
        m_Selected = id + 2;
        m_Selection = { id + 2 };
        m_Fit = true;
    }
    void VisualGraphPanel::Inheritance()
    {
        const bool sectionOpen = ImGui::CollapsingHeader(Tr("Meros###Inheritance"));
        GraphLayout::Record(Tr("Meros###Inheritance"));
        if (!sectionOpen) return;
        GraphLayout::PushScope("inheritance");
        struct ScopeGuard
        {
            ~ScopeGuard() { GraphLayout::PopScope(); }
        } scopeGuard;
        GraphLayout::FitText((std::string(Tr("Ota graf")) + ": " + (m_Graph.Parent.empty() ? std::string(Tr("(yo'q)")) : m_Graph.Parent)).c_str(), m_Graph.Parent.empty());
        GraphLayout::RecordKey("Parent");
        for (size_t i = 1; i < m_Graph.Inherited.Chain.size(); ++i) GraphLayout::FitText(("<- " + m_Graph.Inherited.Chain[i]).c_str(), true);
        // The parent: an Assets-relative .zgraph path (another graph's variables, functions, structs, macros and
        // events become this graph's). Set and clear are undoable.
        ImGui::InputTextWithHint(GraphLayout::Field(Tr("Ota graf yo'li###Parent path")).c_str(), "Scripts/Base.zgraph", m_ParentPath, sizeof(m_ParentPath));
        GraphLayout::Record(Tr("Ota graf yo'li###Parent path"));
        auto setParent = [&](const std::string& parent) {
            const auto self = Project::GetAssetRelativePath(m_Path);
            if (!parent.empty() && (!parent.ends_with(".zgraph") || parent == self))
            {
                m_Error = Tr("Ota graf boshqa .zgraph fayli bo'lishi kerak");
                return;
            }
            Snapshot();
            m_Graph.ClearInherited();
            m_Graph.Parent = parent;
            Reinherit();
            m_Error.clear();
        };
        if (GraphLayout::Button(Tr("Ota grafni o'rnatish###Set parent")) && m_ParentPath != m_Graph.Parent) setParent(m_ParentPath);
        if (!m_Graph.Parent.empty())
        {
            GraphLayout::SameLineIfFits(GraphLayout::ButtonWidth(Tr("Ota grafni olib tashlash###Clear parent")));
            if (GraphLayout::Button(Tr("Ota grafni olib tashlash###Clear parent"))) setParent("");
        }
        if (m_Graph.Inherited.Events.empty()) return;
        // The parent's events: an inherited handler runs as it is until this graph handles the event too.
        ImGui::SeparatorText(Tr("Ota hodisalar###Parent events"));
        const auto overridden = m_Graph.EventOverrides();
        std::string overrideKey;
        for (const auto& key : m_Graph.Inherited.Events)
        {
            ImGui::PushID(key.c_str());
            GraphLayout::PushScope(key);
            const bool done = overridden.contains(key);
            GraphLayout::FitText((key + (done ? std::string("  [") + Tr("qayta yozilgan") + "]" : std::string("  [") + Tr("meros") + "]")).c_str(), !done);
            GraphLayout::RecordKey("event");
            if (!done && GraphLayout::SmallButton(Tr("Hodisani qayta yozish###Override event"))) overrideKey = key;
            GraphLayout::PopScope();
            ImGui::PopID();
        }
        if (!overrideKey.empty()) OverrideEvent(overrideKey);
    }
}
