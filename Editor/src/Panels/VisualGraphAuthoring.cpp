#include "VisualGraphPanel.h"
#include "VisualGraphLayout.h"
#include "Zamin/Editor/EditorLanguage.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
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
    const Visual::EnumDefinition* VisualGraphPanel::FindEnum(const std::string& name) const
    {
        auto it = m_Graph.Enums.find(name);
        return it == m_Graph.Enums.end() ? nullptr : &it->second;
    }
    bool VisualGraphPanel::TypeCombo(const char* label, Visual::Type& type, std::string& enumName)
    {
        using namespace Visual;
        bool changed = false;
        const bool open = ImGui::BeginCombo(GraphLayout::Field(label).c_str(), TypeName(type));
        GraphLayout::Record(label);
        if (open)
        {
            for (int i = 1; i < int(Type::Count); ++i)
            {
                ImGui::PushID(i);
                if (ImGui::Selectable(TypeName(Type(i)), Type(i) == type) && Type(i) != type)
                {
                    if (Type(i) == Type::Enum && m_Graph.Enums.empty()) m_Error = Tr("Avval enum yarating");
                    else
                    {
                        type = Type(i);
                        if (type == Type::Enum) enumName = m_Graph.Enums.begin()->first;
                        changed = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (type == Type::Enum)
        {
            ImGui::PushID(label);
            const bool enumOpen = ImGui::BeginCombo(GraphLayout::Field(Tr("Enum turi###Enum type")).c_str(), enumName.c_str());
            GraphLayout::Record(Tr("Enum turi###Enum type"));
            if (enumOpen)
            {
                for (const auto& [name, definition] : m_Graph.Enums)
                    if (ImGui::Selectable(name.c_str(), name == enumName) && name != enumName)
                    {
                        enumName = name;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
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
        std::string erase, renameFrom, renameTo, open;
        for (auto& [name, signature] : m_Graph.Functions)
        {
            ImGui::PushID(name.c_str());
            const bool treeOpen = GraphLayout::TreeNode("signature", name + " (" + std::to_string(signature.Inputs.size()) + " -> " + std::to_string(signature.Outputs.size()) + ")", name);
            GraphLayout::PushScope(name);
            if (treeOpen)
            {
                char renamed[129]{};
                std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
                if (ImGui::InputText(GraphLayout::Field(Tr("Funksiyani qayta nomlash###Rename function")).c_str(), renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    renameFrom = name;
                    renameTo = renamed;
                }
                GraphLayout::Record(Tr("Funksiyani qayta nomlash###Rename function"));
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
                if (GraphLayout::Button(Tr("Funksiyani ochish###Open function"))) open = name;
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
                            list[i].Enum = type == Type::Enum ? enumName : "";
                            list[i].Default = type == Type::Enum && FindEnum(enumName) ? Value(FindEnum(enumName)->Entries.front().Value) : DefaultValue(type);
                            changed = true;
                        }
                        auto value = list[i].Default;
                        // Defaults use the same strongly typed literal editor as node pins.
                        if (EditLiteral(Tr("Boshlang'ich qiymat"), list[i].Kind, value, FindEnum(list[i].Enum)))
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
                pins(Tr("Parametrlar###Parameters"), draft.Inputs, 0);
                pins(Tr("Natijalar###Returns"), draft.Outputs, 1);
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
                        const auto* enumeration = m_NewLocalType == Type::Enum ? FindEnum(m_NewLocalEnum) : nullptr;
                        if (m_NewLocalType != Type::Enum || enumeration)
                        {
                            draft.Locals[m_MemberName] = { m_NewLocalType, enumeration ? Value(enumeration->Entries.front().Value) : DefaultValue(m_NewLocalType), enumeration ? m_NewLocalEnum : "" };
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
                        if (EditLiteral(Tr("Boshlang'ich qiymat"), local.Kind, value, FindEnum(local.Enum)))
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
                if (GraphLayout::Button(Tr("Funksiya va uning tugunlarini o'chirish###Delete function and its nodes"))) erase = name;
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
                if (node.Kind.starts_with("Function.") && node.Inputs.contains("Name") && node.Inputs.at("Name") == erase) nodes.insert(node.ID);
            std::erase_if(m_Graph.Nodes, [&](const auto& node) { return nodes.contains(node.ID); });
            std::erase_if(m_Graph.Links, [&](const auto& link) { return nodes.contains(link.From) || nodes.contains(link.To); });
            m_Graph.Functions.erase(erase);
            if (m_FocusFunction == erase) m_FocusFunction.clear();
        }
        if (!open.empty()) FocusFunction(open);
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
            const bool treeOpen = GraphLayout::TreeNode("enum", name + " (" + std::to_string(definition.Entries.size()) + ")", name);
            GraphLayout::PushScope(name);
            if (treeOpen)
            {
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
