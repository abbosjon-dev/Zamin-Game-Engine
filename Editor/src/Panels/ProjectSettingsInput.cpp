// Project settings > Input: the input map editor (actions, axes, 2D vectors, key capture, duplicate warnings).
#include "ProjectSettingsPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <initializer_list>

namespace Zamin {

    static const char* s_SourceLabels[] = {
        ICON_FA_KEYBOARD "  Klaviatura",
        ICON_FA_COMPUTER_MOUSE "  Sichqoncha",
        ICON_FA_GAMEPAD "  Gamepad",
        ICON_FA_GAMEPAD "  Gamepad o'qi",
    };

    static InputBinding DefaultBindingFor(InputSource source)
    {
        switch (source)
        {
            case InputSource::Key:           return InputBinding::FromKey(Key::Space);
            case InputSource::Mouse:         return InputBinding::FromMouse(MouseButton::Left);
            case InputSource::GamepadButton: return InputBinding::FromGamepadButton(GamepadButton::A);
            case InputSource::GamepadAxis:   return InputBinding::FromGamepadAxis(GamepadAxis::LeftX, 1);
        }
        return {};
    }

    static std::string CodeLabel(const InputBinding& b)
    {
        switch (b.Source)
        {
            case InputSource::Key:           return KeyToString((Key)b.Code);
            case InputSource::Mouse:         return MouseButtonToString((MouseButton)b.Code);
            case InputSource::GamepadButton: return GamepadButtonToString((GamepadButton)b.Code);
            case InputSource::GamepadAxis:   return std::format("{} {}", GamepadAxisToString((GamepadAxis)b.Code), b.Direction < 0 ? '-' : '+');
        }
        return {};
    }

    static std::string Summary(const std::vector<InputBinding>& bindings)
    {
        std::string s;
        for (const auto& b : bindings)
        {
            if (!s.empty()) s += ", ";
            s += InputBindingDisplayName(b);
        }
        return s.empty() ? "(bog'lanmagan)" : s;
    }

    // Returns the first control that is currently held (used by "press a key" capture).
    static bool PollPressed(InputBinding& out, bool includeMouse, bool includeEscape)
    {
        for (Key k : GetAllKeys())
            if ((includeEscape || k != Key::Escape) && Input::IsKeyPressed(k))
            {
                out = InputBinding::FromKey(k);
                return true;
            }
        if (includeMouse)
            for (int i = 0; i < MouseButtonCount; i++)
                if (Input::IsMouseButtonPressed((MouseButton)i))
                {
                    out = InputBinding::FromMouse((MouseButton)i);
                    return true;
                }
        if (Input::IsGamepadConnected(0))
        {
            for (int i = 0; i < GamepadButtonCount; i++)
                if (Input::IsGamepadButtonPressed((GamepadButton)i))
                {
                    out = InputBinding::FromGamepadButton((GamepadButton)i);
                    return true;
                }
            for (int i = 0; i < GamepadAxisCount; i++)
            {
                float v = Input::GetGamepadAxis((GamepadAxis)i, 0, 0.0f);
                bool trigger = i == (int)GamepadAxis::LeftTrigger || i == (int)GamepadAxis::RightTrigger;
                if (trigger ? v > 0.2f : std::abs(v) > 0.6f)
                {
                    out = InputBinding::FromGamepadAxis((GamepadAxis)i, trigger || v > 0.0f ? 1 : -1);
                    return true;
                }
            }
        }
        return false;
    }

    // Live preview: a dot that lights up while the action is active.
    static void ActionIndicator(const InputAction& action)
    {
        const bool active = Input::IsActionPressed(action);
        const float h = ImGui::GetFrameHeight();
        const float r = ImGui::GetFontSize() * 0.26f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, h));
        ImVec2 c(p.x + r + 3.0f, p.y + h * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (active)
        {
            dl->AddCircleFilled(c, r + 2.5f, UI::AccentU32(0.30f));
            dl->AddCircleFilled(c, r, UI::AccentU32());
        }
        else
            dl->AddCircle(c, r, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0, 1.5f);
        if (ImGui::IsItemHovered())
            UI::Tooltip(active ? "Faol (hozir bosilgan)" : "Jonli holat: bog'langan tugmani bosib sinab ko'ring");
    }

    // Live preview: a -1..1 bar filled from the centre to the axis value.
    static void AxisIndicator(const InputAxis& axis)
    {
        const float value = Input::GetAxis(axis);
        const float h = ImGui::GetFrameHeight();
        const float w = ImGui::GetFontSize() * 4.5f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float barH = h * 0.36f;
        ImVec2 a(p.x, p.y + (h - barH) * 0.5f), b(p.x + w, a.y + barH);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg), barH * 0.5f);
        const float mid = (a.x + b.x) * 0.5f;
        const float end = mid + value * (w * 0.5f);
        if (std::abs(value) > 0.001f)
            dl->AddRectFilled(ImVec2(std::min(mid, end), a.y), ImVec2(std::max(mid, end), b.y), UI::AccentU32(), barH * 0.5f);
        dl->AddLine(ImVec2(mid, a.y - 2.0f), ImVec2(mid, b.y + 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), 1.0f);
        if (ImGui::IsItemHovered())
            UI::Tooltip(std::format("Jonli qiymat: {:.2f}", value).c_str());
    }

    // ------------------------------------------------------------------ input map
    std::vector<InputBinding>* ProjectSettingsPanel::ResolveList(InputActionMap& map, ListKind kind, int entry)
    {
        switch (kind)
        {
            case ListKind::Action:
                return entry >= 0 && entry < (int)map.Actions.size() ? &map.Actions[entry].Bindings : nullptr;
            case ListKind::AxisNegative:
                return entry >= 0 && entry < (int)map.Axes.size() ? &map.Axes[entry].Negative : nullptr;
            case ListKind::AxisPositive:
                return entry >= 0 && entry < (int)map.Axes.size() ? &map.Axes[entry].Positive : nullptr;
            default:
                return nullptr;
        }
    }

    void ProjectSettingsPanel::StartListening(const ListenTarget& target)
    {
        m_Listen = target;
        m_ListenArmed = false;
        // Drop keyboard focus so Space/Enter don't activate the focused button while we capture.
        ImGui::SetWindowFocus(nullptr);
    }

    void ProjectSettingsPanel::UpdateListening(InputActionMap& map)
    {
        if (m_Listen.Kind == ListKind::None)
            return;
        std::vector<InputBinding>* list = ResolveList(map, m_Listen.Kind, m_Listen.Entry);
        if (!list || m_Listen.Binding < 0 || m_Listen.Binding >= (int)list->size())
        {
            StopListening();
            return;
        }

        InputBinding pressed;
        if (!m_ListenArmed)
        {
            // Wait until everything (including the click that started listening) is released.
            m_ListenArmed = !PollPressed(pressed, true, true);
            return;
        }
        if (Input::IsKeyPressed(Key::Escape))
        {
            StopListening();
            return;
        }
        // Mouse buttons count only when not clicking on a widget (e.g. the cancel button).
        if (PollPressed(pressed, !ImGui::IsAnyItemHovered(), false))
        {
            (*list)[m_Listen.Binding] = pressed;
            m_Dirty = true;
            StopListening();
        }
    }

    bool ProjectSettingsPanel::DrawBinding(InputBinding& b, const ListenTarget& target, const std::vector<std::string>& sharedWith)
    {
        bool changed = false;
        const float buttonSize = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float sourceWidth = std::min(ImGui::GetFontSize() * 9.0f, avail * 0.42f);
        const int buttons = sharedWith.empty() ? 2 : 3; // capture, delete (+ the warning)
        const float codeWidth = std::max(40.0f, avail - sourceWidth - (buttonSize + spacing) * (float)buttons - spacing);

        ImGui::SetNextItemWidth(sourceWidth);
        int source = (int)b.Source;
        if (ImGui::Combo("##source", &source, s_SourceLabels, IM_ARRAYSIZE(s_SourceLabels)) && source != (int)b.Source)
        {
            b = DefaultBindingFor((InputSource)source);
            changed = true;
        }

        ImGui::SameLine();
        bool listening = m_Listen == target;
        if (listening)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.45f, 0.65f));
            if (ImGui::Button("Tugmani bosing...  (Esc: bekor qilish)", ImVec2(codeWidth, 0)))
                StopListening();
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::SetNextItemWidth(codeWidth);
            if (ImGui::BeginCombo("##code", CodeLabel(b).c_str(), ImGuiComboFlags_HeightLarge))
            {
                auto option = [&](const char* label, const InputBinding& candidate) {
                    bool selected = candidate == b;
                    if (ImGui::Selectable(label, selected) && !selected)
                    {
                        b = candidate;
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                };
                switch (b.Source)
                {
                    case InputSource::Key:
                        for (Key k : GetAllKeys())
                            option(KeyToString(k), InputBinding::FromKey(k));
                        break;
                    case InputSource::Mouse:
                        for (int i = 0; i < MouseButtonCount; i++)
                            option(MouseButtonToString((MouseButton)i), InputBinding::FromMouse((MouseButton)i));
                        break;
                    case InputSource::GamepadButton:
                        for (int i = 0; i < GamepadButtonCount; i++)
                            option(GamepadButtonToString((GamepadButton)i), InputBinding::FromGamepadButton((GamepadButton)i));
                        break;
                    case InputSource::GamepadAxis:
                        for (int i = 0; i < GamepadAxisCount; i++)
                            for (int dir : { 1, -1 })
                            {
                                InputBinding candidate = InputBinding::FromGamepadAxis((GamepadAxis)i, dir);
                                option(CodeLabel(candidate).c_str(), candidate);
                            }
                        break;
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(listening ? ICON_FA_XMARK : ICON_FA_KEYBOARD, ImVec2(buttonSize, 0)))
        {
            if (listening) StopListening();
            else StartListening(target);
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip(listening ? "Bekor qilish" : "Tugmani bosib tanlash (klaviatura, sichqoncha yoki gamepad)");
        // The same control is bound somewhere else too (a warning: sharing can be intended).
        if (!sharedWith.empty())
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.74f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::Button(ICON_FA_TRIANGLE_EXCLAMATION "##shared", ImVec2(buttonSize, 0));
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
            {
                std::string list;
                for (const std::string& user : sharedWith)
                    list += (list.empty() ? "" : ", ") + user;
                UI::Tooltip(std::format("{} yana shu yerlarda bog'langan: {}\nBir tugma bir nechta harakatni ishga tushiradi "
                                        "(ataylab bo'lmasa, birini almashtiring)", InputBindingDisplayName(b), list).c_str());
            }
        }
        return changed;
    }

    bool ProjectSettingsPanel::DrawBindingList(const char* label, std::vector<InputBinding>& bindings, ListKind kind, int entry,
                                               const InputActionMap& map)
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextColored(ImVec4(0.72f, 0.75f, 0.80f, 1.0f), "%s", label);
        int remove = -1;
        // The name of this list as FindUsers reports it ("Jump", "Horizontal (-)").
        std::string self;
        if (kind == ListKind::Action && entry < (int)map.Actions.size())
            self = map.Actions[(size_t)entry].Name;
        else if (kind != ListKind::Action && entry < (int)map.Axes.size())
            self = map.Axes[(size_t)entry].Name + (kind == ListKind::AxisNegative ? " (-)" : " (+)");
        for (int i = 0; i < (int)bindings.size(); i++)
        {
            ImGui::PushID(i);
            // The other places of the same control (this list once: a second time here is listed).
            std::vector<std::string> shared = map.FindUsers(bindings[i]);
            if (auto it = std::find(shared.begin(), shared.end(), self); it != shared.end())
                shared.erase(it);
            changed |= DrawBinding(bindings[i], { kind, entry, i }, shared);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TRASH, ImVec2(ImGui::GetFrameHeight(), 0)))
                remove = i;
            if (ImGui::IsItemHovered())
                UI::Tooltip("Bog'lanishni o'chirish");
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            bindings.erase(bindings.begin() + remove);
            StopListening();
            changed = true;
        }
        if (ImGui::SmallButton(ICON_FA_PLUS "  Bog'lash qo'shish"))
        {
            bindings.push_back(InputBinding::FromKey(Key::Space));
            StartListening({ kind, entry, (int)bindings.size() - 1 });
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    template<typename T>
    static void NameWarning(const std::vector<T>& items, int index)
    {
        const std::string& name = items[index].Name;
        if (name.empty())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Nom bo'sh bo'lmasligi kerak");
            return;
        }
        for (int i = 0; i < (int)items.size(); i++)
            if (i != index && items[i].Name == name)
            {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Bu nom allaqachon bor");
                return;
            }
    }

    // The entry is shown under the list's filter: its name or one of its controls contains it.
    static bool PassesFilter(const std::string& filter, const std::string& name, std::initializer_list<const std::vector<InputBinding>*> lists)
    {
        if (filter.empty() || ProjectSettingsIndex::TextContains(name, filter))
            return true;
        for (const std::vector<InputBinding>* list : lists)
            for (const InputBinding& b : *list)
                if (ProjectSettingsIndex::TextContains(InputBindingDisplayName(b), filter))
                    return true;
        return false;
    }

    void ProjectSettingsPanel::DrawInputMap(InputActionMap& map)
    {
        UpdateListening(map);
        bool changed = false;
        const ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

        ImGui::TextDisabled("Harakat, o'q va vektorlarni nomi bilan tugma, sichqoncha yoki gamepad'ga bog'lang.");
        ImGui::TextDisabled("Skriptda: Input::IsActionJustPressed(\"Jump\"), Input::GetAxis(\"Horizontal\"), Input::GetVector(\"Move\")");
        ImGui::Spacing();

        // Filter by name or control; open / close every entry.
        if (m_AutomationOpen == "expand")
        {
            m_InputOpenRequest = 1;
            m_AutomationOpen.clear();
        }
        const int openRequest = m_InputOpenRequest;
        m_InputOpenRequest = -1;
        auto applyOpen = [&]() {
            if (openRequest >= 0)
                ImGui::SetNextItemOpen(openRequest == 1);
        };
        ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 16.0f, ImGui::GetContentRegionAvail().x));
        ImGui::InputTextWithHint("##inputFilter", ICON_FA_MAGNIFYING_GLASS "  Nom yoki tugma bo'yicha qidirish", &m_InputFilter);
        const std::string& filter = m_InputFilter;
        UI::SameLineIfFits(ICON_FA_PLUS "  Yoyish");
        if (ImGui::Button(ICON_FA_PLUS "  Yoyish"))
            m_InputOpenRequest = 1;
        if (ImGui::IsItemHovered())
            UI::Tooltip("Hamma harakat, o'q va vektorlarni ochish");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_MINUS "  Yig'ish"))
            m_InputOpenRequest = 0;
        if (ImGui::IsItemHovered())
            UI::Tooltip("Hammasini yopish");

        // Controls bound in more than one place.
        const std::vector<InputBindingConflict> conflicts = map.FindConflicts();
        if (!conflicts.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.74f, 0.30f, 1.0f));
            applyOpen();
            const bool open = ImGui::TreeNodeEx("##conflicts", ImGuiTreeNodeFlags_SpanAvailWidth,
                                                ICON_FA_TRIANGLE_EXCLAMATION "  %zu ta takroriy bog'lanish", conflicts.size());
            ImGui::PopStyleColor();
            if (open)
            {
                for (const InputBindingConflict& c : conflicts)
                {
                    std::string users;
                    for (const std::string& u : c.Users)
                        users += (users.empty() ? "" : ", ") + u;
                    ImGui::BulletText("%s:  %s", InputBindingDisplayName(c.Binding).c_str(), users.c_str());
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Bir tugma bir nechta harakatda bo'lishi mumkin (masalan, Gamepad A: Jump va menyu). Ataylab "
                                   "bo'lmasa, birini almashtiring.");
                ImGui::PopStyleColor();
                ImGui::TreePop();
            }
        }
        ImGui::Spacing();

        // ---- actions
        ImGui::SeparatorText(ICON_FA_HAND_POINTER "  Harakatlar (actions)");
        int removeAction = -1;
        for (int i = 0; i < (int)map.Actions.size(); i++)
        {
            InputAction& action = map.Actions[i];
            if (!PassesFilter(filter, action.Name, { &action.Bindings }))
                continue;
            ImGui::PushID(i);
            applyOpen();
            bool open = ImGui::TreeNodeEx("##action", nodeFlags, "%s", action.Name.empty() ? "(nomsiz)" : action.Name.c_str());
            ImGui::SameLine();
            ActionIndicator(action);
            if (!open)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", Summary(action.Bindings).c_str());
            }
            else
            {
                if (UI::BeginPropertyGrid("props"))
                {
                    changed |= UI::PropertyString("Nomi", action.Name);
                    UI::EndPropertyGrid();
                }
                NameWarning(map.Actions, i);
                changed |= DrawBindingList("Bog'lanishlar", action.Bindings, ListKind::Action, i, map);
                if (ImGui::SmallButton(ICON_FA_TRASH "  Harakatni o'chirish"))
                    removeAction = i;
                ImGui::Spacing();
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeAction >= 0)
        {
            map.Actions.erase(map.Actions.begin() + removeAction);
            StopListening();
            changed = true;
        }
        if (ImGui::Button(ICON_FA_PLUS "  Harakat qo'shish"))
        {
            map.Actions.push_back({ map.MakeUniqueActionName("Action"), {} });
            StopListening();
            changed = true;
        }

        // ---- axes
        ImGui::Spacing();
        ImGui::SeparatorText(ICON_FA_ARROWS_LEFT_RIGHT "  O'qlar (axes)");
        int removeAxis = -1;
        static const char* s_AxisItems[] = { "(yo'q)", "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };
        for (int i = 0; i < (int)map.Axes.size(); i++)
        {
            InputAxis& axis = map.Axes[i];
            if (!PassesFilter(filter, axis.Name, { &axis.Negative, &axis.Positive }))
                continue;
            ImGui::PushID(1000 + i);
            applyOpen();
            bool open = ImGui::TreeNodeEx("##axis", nodeFlags, "%s", axis.Name.empty() ? "(nomsiz)" : axis.Name.c_str());
            ImGui::SameLine();
            AxisIndicator(axis);
            if (!open)
            {
                ImGui::SameLine();
                std::string summary = Summary(axis.Negative) + "  |  " + Summary(axis.Positive);
                if (axis.GamepadAxisCode >= 0)
                    summary += std::format("  |  {}", GamepadAxisToString((GamepadAxis)axis.GamepadAxisCode));
                ImGui::TextDisabled("%s", summary.c_str());
            }
            else
            {
                if (UI::BeginPropertyGrid("props"))
                {
                    changed |= UI::PropertyString("Nomi", axis.Name);
                    int gamepadAxis = axis.GamepadAxisCode + 1;
                    if (UI::PropertyCombo("Gamepad o'qi", gamepadAxis, s_AxisItems, IM_ARRAYSIZE(s_AxisItems)))
                    {
                        axis.GamepadAxisCode = gamepadAxis - 1;
                        changed = true;
                    }
                    changed |= UI::PropertyBool("Teskari (invert)", axis.Invert);
                    if (UI::PropertySlider("Deadzone", axis.Deadzone, 0.0f, 0.95f))
                    {
                        axis.Deadzone = std::clamp(axis.Deadzone, 0.0f, 0.95f);
                        changed = true;
                    }
                    UI::EndPropertyGrid();
                }
                NameWarning(map.Axes, i);
                changed |= DrawBindingList("Manfiy tomon (-1)", axis.Negative, ListKind::AxisNegative, i, map);
                changed |= DrawBindingList("Musbat tomon (+1)", axis.Positive, ListKind::AxisPositive, i, map);
                if (ImGui::SmallButton(ICON_FA_TRASH "  O'qni o'chirish"))
                    removeAxis = i;
                ImGui::Spacing();
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeAxis >= 0)
        {
            map.Axes.erase(map.Axes.begin() + removeAxis);
            StopListening();
            changed = true;
        }
        if (ImGui::Button(ICON_FA_PLUS "  O'q qo'shish"))
        {
            InputAxis axis;
            axis.Name = map.MakeUniqueAxisName("Axis");
            map.Axes.push_back(std::move(axis));
            StopListening();
            changed = true;
        }

        // ---- 2D vectors (composite axes)
        changed |= DrawInputVectors(map, openRequest);

        // ---- reset
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Standart holatga qaytarish"))
            ImGui::OpenPopup("ResetInputMap");
        if (ImGui::BeginPopup("ResetInputMap"))
        {
            ImGui::TextUnformatted("Barcha harakat va o'qlar standart holatga qaytarilsinmi?");
            if (ImGui::Button("Ha, qaytarish"))
            {
                map = InputActionMap::CreateDefault();
                StopListening();
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Yo'q"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (changed)
            m_Dirty = true;
    }

    // Live preview of a 2D vector: a circle with the value's dot (and the deadzone ring).
    static void VectorIndicator(const InputVector& vector)
    {
        const glm::vec2 value = Input::GetVector(vector);
        const float size = ImGui::GetFrameHeight() * 2.2f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(size, size));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
        const float r = size * 0.5f - 2.0f;
        dl->AddCircleFilled(c, r, ImGui::GetColorU32(ImGuiCol_FrameBg), 32);
        dl->AddCircle(c, r, ImGui::GetColorU32(ImGuiCol_Border), 32);
        if (vector.Deadzone > 0.0f)
            dl->AddCircle(c, r * std::clamp(vector.Deadzone, 0.0f, 0.95f), ImGui::GetColorU32(ImGuiCol_TextDisabled), 24);
        const ImU32 axisColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), axisColor);
        dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), axisColor);
        // Raw mode can go past the circle (diagonals ~1.41): drawn at the edge.
        glm::vec2 shown = value;
        if (glm::length(shown) > 1.0f)
            shown = glm::normalize(shown);
        const ImVec2 dot(c.x + shown.x * r, c.y - shown.y * r);
        if (glm::length(value) > 0.001f)
            dl->AddLine(c, dot, UI::AccentU32(0.6f), 2.0f);
        dl->AddCircleFilled(dot, 3.5f, UI::AccentU32());
        if (ImGui::IsItemHovered())
            UI::Tooltip(std::format("Jonli qiymat: ({:.2f}, {:.2f}), uzunligi {:.2f}", value.x, value.y, glm::length(value)).c_str());
    }

    bool ProjectSettingsPanel::DrawInputVectors(InputActionMap& map, int openRequest)
    {
        bool changed = false;
        ImGui::Spacing();
        ImGui::SeparatorText(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  Vektorlar (2D composite)");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Ikki o'q bitta 2D qiymat bo'lib o'qiladi: Input::GetVector(\"Move\"). Radial deadzone butun uzunlikka "
                           "ta'sir qiladi (stick biroz og'sa ham 0).");
        ImGui::PopStyleColor();
        static const char* s_ModeLabels[] = { "Uzunlik 1 dan oshmaydi (Clamp)", "Har doim uzunligi 1 (Normalize)", "Xom: o'qlar qanday bo'lsa (Raw)" };
        int remove = -1;
        const ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        for (int i = 0; i < (int)map.Vectors.size(); i++)
        {
            InputVector& vector = map.Vectors[(size_t)i];
            if (!m_InputFilter.empty() && !ProjectSettingsIndex::TextContains(vector.Name, m_InputFilter) &&
                !ProjectSettingsIndex::TextContains(vector.XAxis, m_InputFilter) && !ProjectSettingsIndex::TextContains(vector.YAxis, m_InputFilter))
                continue;
            ImGui::PushID(2000 + i);
            if (openRequest >= 0)
                ImGui::SetNextItemOpen(openRequest == 1);
            const bool open = ImGui::TreeNodeEx("##vector", nodeFlags, "%s", vector.Name.empty() ? "(nomsiz)" : vector.Name.c_str());
            if (!open)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("X: %s  |  Y: %s  |  %s", vector.XAxis.c_str(), vector.YAxis.c_str(), InputVectorModeToString(vector.Mode));
            }
            else
            {
                if (ImGui::BeginTable("##vectorRow", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("props", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 2.4f);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (UI::BeginPropertyGrid("props"))
                    {
                        changed |= UI::PropertyString("Nomi", vector.Name);
                        auto axisCombo = [&](const char* label, std::string& axis) {
                            UI::PropertyLabel(label);
                            ImGui::PushID(label);
                            const bool known = map.FindAxis(axis) != nullptr;
                            const std::string preview = axis.empty() ? std::string("(yo'q)") : known ? axis : axis + "  (topilmadi)";
                            ImGui::PushStyleColor(ImGuiCol_Text, known ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImVec4(0.95f, 0.45f, 0.35f, 1.0f));
                            const bool comboOpen = ImGui::BeginCombo("##axis", preview.c_str());
                            ImGui::PopStyleColor();
                            if (comboOpen)
                            {
                                for (const InputAxis& candidate : map.Axes)
                                    if (ImGui::Selectable(candidate.Name.c_str(), axis == candidate.Name) && axis != candidate.Name)
                                    {
                                        axis = candidate.Name;
                                        changed = true;
                                    }
                                ImGui::EndCombo();
                            }
                            ImGui::PopID();
                        };
                        axisCombo("X o'qi", vector.XAxis);
                        axisCombo("Y o'qi", vector.YAxis);
                        int mode = (int)vector.Mode;
                        if (UI::PropertyCombo("Rejim", mode, s_ModeLabels, IM_ARRAYSIZE(s_ModeLabels)))
                        {
                            vector.Mode = (InputVectorMode)std::clamp(mode, 0, InputVectorModeCount - 1);
                            changed = true;
                        }
                        if (UI::PropertySlider("Radial deadzone", vector.Deadzone, 0.0f, 0.95f))
                        {
                            vector.Deadzone = std::clamp(vector.Deadzone, 0.0f, 0.95f);
                            changed = true;
                        }
                        UI::EndPropertyGrid();
                    }
                    ImGui::TableSetColumnIndex(1);
                    VectorIndicator(vector);
                    ImGui::EndTable();
                }
                NameWarning(map.Vectors, i);
                if (ImGui::SmallButton(ICON_FA_TRASH "  Vektorni o'chirish"))
                    remove = i;
                ImGui::Spacing();
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            map.Vectors.erase(map.Vectors.begin() + remove);
            changed = true;
        }
        if (ImGui::Button(ICON_FA_PLUS "  Vektor qo'shish"))
        {
            InputVector vector;
            vector.Name = map.MakeUniqueVectorName(map.FindVector("Move") ? "Vector" : "Move");
            vector.XAxis = map.Axes.size() > 0 ? map.Axes[0].Name : "Horizontal";
            vector.YAxis = map.Axes.size() > 1 ? map.Axes[1].Name : "Vertical";
            map.Vectors.push_back(std::move(vector));
            changed = true;
        }
        return changed;
    }

}
