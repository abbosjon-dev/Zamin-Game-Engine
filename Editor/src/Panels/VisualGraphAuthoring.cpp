#include "VisualGraphPanel.h"
#include "Zamin/Editor/EditorLanguage.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include <algorithm>
#include <cstdio>
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
        if (ImGui::BeginCombo(label, TypeName(type)))
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
            if (ImGui::BeginCombo(Tr("Enum turi###Enum type"), enumName.c_str()))
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
        if (!ImGui::CollapsingHeader(Tr("Tiplangan funksiyalar###Typed functions"))) return;
        ImGui::InputText(Tr("Funksiya nomi###Function name"), m_FunctionName, sizeof(m_FunctionName));
        if (ImGui::Button(Tr("Funksiya yaratish###Create function")) && m_FunctionName[0] && m_Graph.Functions.size() < 64 && !m_Graph.Functions.contains(m_FunctionName))
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
            if (ImGui::TreeNode("signature", "%s (%zu -> %zu)", name.c_str(), signature.Inputs.size(), signature.Outputs.size()))
            {
                char renamed[129]{};
                std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
                if (ImGui::InputText(Tr("Funksiyani qayta nomlash###Rename function"), renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    renameFrom = name;
                    renameTo = renamed;
                }
                if (ImGui::Button(Tr("Chaqiruv tugunini qo'shish###Add call node")))
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
                ImGui::SameLine();
                if (ImGui::Button(Tr("Funksiyani ochish###Open function"))) open = name;
                auto draft = signature;
                bool changed = false;
                std::vector<std::pair<std::string, std::string>> pinRenames[2];
                auto pins = [&](const char* title, std::vector<Pin>& list, int side) {
                    if (!ImGui::TreeNode(title)) return;
                    size_t remove = SIZE_MAX;
                    for (size_t i = 0; i < list.size(); ++i)
                    {
                        ImGui::PushID(int(i));
                        char key[129]{};
                        std::snprintf(key, sizeof(key), "%s", list[i].Name.c_str());
                        // Enter renames; the wires of the entry, calls and returns follow (Graph::RenameFunctionPin).
                        if (ImGui::InputText(Tr("Pin nomi###Pin name"), key, sizeof(key), ImGuiInputTextFlags_EnterReturnsTrue) && list[i].Name != key)
                            pinRenames[side].push_back({ list[i].Name, key });
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
                        if (EditLiteral(Tr("Boshlang'ich qiymat###Default"), list[i].Kind, value, FindEnum(list[i].Enum)))
                        {
                            list[i].Default = std::move(value);
                            changed = true;
                        }
                        if (ImGui::SmallButton(Tr("Pinni olib tashlash###Remove pin"))) remove = i;
                        ImGui::PopID();
                    }
                    if (remove != SIZE_MAX)
                    {
                        list.erase(list.begin() + remove);
                        changed = true;
                    }
                    if (list.size() < 32 && ImGui::Button(Tr("Pin qo'shish###Add pin")))
                    {
                        std::string pinName = "Value" + std::to_string(list.size() + 1);
                        while (std::any_of(list.begin(), list.end(), [&](const Pin& p) { return p.Name == pinName; })) pinName += "_";
                        list.push_back({ pinName, Type::Number, 0. });
                        changed = true;
                    }
                    ImGui::TreePop();
                };
                pins(Tr("Parametrlar###Parameters"), draft.Inputs, 0);
                pins(Tr("Natijalar###Returns"), draft.Outputs, 1);
                if (ImGui::TreeNode(Tr("Lokal o'zgaruvchilar###Local variables")))
                {
                    ImGui::InputText(Tr("Lokal nomi###Local name"), m_MemberName, sizeof(m_MemberName));
                    TypeCombo(Tr("Lokal turi###Local type"), m_NewLocalType, m_NewLocalEnum);
                    if (m_MemberName[0] && draft.Locals.size() < 32 && ImGui::Button(Tr("Lokal qo'shish###Add local")))
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
                        ImGui::Text("%s (%s%s%s)", key.c_str(), TypeName(local.Kind), local.Enum.empty() ? "" : " ", local.Enum.c_str());
                        auto value = local.Default;
                        if (EditLiteral(Tr("Boshlang'ich qiymat###Default"), local.Kind, value, FindEnum(local.Enum)))
                        {
                            draft.Locals.at(key).Default = std::move(value);
                            changed = true;
                        }
                        if (ImGui::SmallButton(Tr("Lokalni olib tashlash###Remove local"))) removed = key;
                        ImGui::PopID();
                    }
                    if (!removed.empty())
                    {
                        draft.Locals.erase(removed);
                        changed = true;
                    }
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
                if (ImGui::Button(Tr("Funksiya va uning tugunlarini o'chirish###Delete function and its nodes"))) erase = name;
                ImGui::TreePop();
            }
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
        if (!ImGui::CollapsingHeader(Tr("Enum turlari###Graph enums"))) return;
        ImGui::InputText(Tr("Enum nomi###Enum name"), m_EnumName, sizeof(m_EnumName));
        if (ImGui::Button(Tr("Enum yaratish###Create enum")))
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
        if (fields.empty()) ImGui::TextDisabled("%s", Tr("Yuklangan C++ enum maydonlari yo'q"));
        else
        {
            m_ImportField = std::clamp(m_ImportField, 0, int(fields.size()) - 1);
            const auto label = [&](int i) { return fields[size_t(i)].first + "." + fields[size_t(i)].second; };
            if (ImGui::BeginCombo(Tr("C++ enum maydoni###C++ enum field"), label(m_ImportField).c_str()))
            {
                for (int i = 0; i < int(fields.size()); ++i)
                    if (ImGui::Selectable(label(i).c_str(), i == m_ImportField)) m_ImportField = i;
                ImGui::EndCombo();
            }
            if (ImGui::Button(Tr("C++ maydonidan import qilish###Import from C++ field")))
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
            if (ImGui::TreeNode("enum", "%s (%zu)", name.c_str(), definition.Entries.size()))
            {
                char renamed[65]{};
                std::snprintf(renamed, sizeof(renamed), "%s", name.c_str());
                if (ImGui::InputText(Tr("Enumni qayta nomlash###Rename enum"), renamed, sizeof(renamed), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    renameFrom = name;
                    renameTo = renamed;
                }
                if (!definition.Source.empty()) ImGui::TextDisabled("%s: %s", Tr("Manba"), definition.Source.c_str());
                auto draft = definition;
                bool changed = false;
                size_t remove = SIZE_MAX;
                std::pair<std::string, std::string> entryRename;
                for (size_t i = 0; i < draft.Entries.size(); ++i)
                {
                    ImGui::PushID(int(i));
                    char key[65]{};
                    std::snprintf(key, sizeof(key), "%s", draft.Entries[i].Name.c_str());
                    if (ImGui::InputText(Tr("Qiymat nomi###Entry name"), key, sizeof(key), ImGuiInputTextFlags_EnterReturnsTrue) && draft.Entries[i].Name != key) entryRename = { draft.Entries[i].Name, key };
                    int64_t value = draft.Entries[i].Value;
                    if (ImGui::InputScalar(Tr("Son qiymati###Entry value"), ImGuiDataType_S64, &value, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue) && value != draft.Entries[i].Value)
                    {
                        if (draft.Find(value) || std::abs(value) > (int64_t(1) << 53)) m_Error = Tr("Son qiymati band yoki chegaradan tashqarida");
                        else
                        {
                            draft.Entries[i].Value = value;
                            changed = true;
                        }
                    }
                    if (draft.Entries.size() > 1 && ImGui::SmallButton(Tr("Qiymatni olib tashlash###Remove entry"))) remove = i;
                    ImGui::PopID();
                }
                if (remove != SIZE_MAX)
                {
                    draft.Entries.erase(draft.Entries.begin() + remove);
                    changed = true;
                }
                if (draft.Entries.size() < 256 && ImGui::Button(Tr("Qiymat qo'shish###Add entry")))
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
                if (ImGui::Button(Tr("Enumni o'chirish###Delete enum"))) erase = name;
                ImGui::TreePop();
            }
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
