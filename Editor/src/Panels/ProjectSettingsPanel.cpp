#include "ProjectSettingsPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

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

    // ------------------------------------------------------------------ panel
    void ProjectSettingsPanel::OnProjectChanged()
    {
        m_Dirty = false;
        StopListening();
    }

    void ProjectSettingsPanel::SaveIfDirty()
    {
        if (!m_Dirty || !Project::GetActive())
            return;
        if (Project::SaveActive(Project::GetProjectFilePath()))
        {
            m_Dirty = false;
            ZM_INFO("Loyiha sozlamalari saqlandi: {}", Project::GetProjectFilePath().filename().string());
        }
        else
            ZM_ERROR("Loyiha sozlamalari saqlanmadi: {}", Project::GetProjectFilePath().string());
    }

    void ProjectSettingsPanel::OnImGuiRender(bool* show)
    {
        if (!*show)
        {
            StopListening();
            return;
        }
        if (m_FocusRequested)
        {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        if (!ImGui::Begin(ICON_FA_GEAR "  Loyiha sozlamalari###ProjectSettings", show))
        {
            ImGui::End();
            return;
        }

        auto project = Project::GetActive();
        if (!project)
        {
            ImGui::TextDisabled("Loyiha ochilmagan");
            ImGui::End();
            return;
        }
        auto& config = project->GetConfig();

        // Save bar
        ImGui::BeginDisabled(!m_Dirty);
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Saqlash"))
            SaveIfDirty();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (m_Dirty)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Saqlanmagan o'zgarishlar bor (Ctrl+S)");
        else
            ImGui::TextDisabled("%s", Project::GetProjectFilePath().filename().string().c_str());
        ImGui::Spacing();

        if (ImGui::BeginTabBar("##projectTabs"))
        {
            if (ImGui::BeginTabItem(ICON_FA_GAMEPAD "  Input"))
            {
                DrawInputMap(config.InputActions);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_SLIDERS "  Umumiy"))
            {
                DrawGeneral(config);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void ProjectSettingsPanel::DrawGeneral(ProjectConfig& c)
    {
        if (!UI::BeginPropertyGrid("general"))
            return;
        UI::PropertyLabel("Nomi");
        ImGui::TextUnformatted(c.Name.c_str());
        UI::PropertyLabel("Papka");
        ImGui::TextWrapped("%s", Project::GetProjectDirectory().string().c_str());
        UI::PropertyLabel("Boshlang'ich sahna");
        ImGui::TextUnformatted(c.StartScene.empty() ? "-" : c.StartScene.generic_string().c_str());
        UI::PropertyLabel("Assets papkasi");
        ImGui::TextUnformatted(c.AssetDirectory.generic_string().c_str());
        UI::PropertyLabel("Skriptlar papkasi");
        ImGui::TextUnformatted(c.SourceDirectory.generic_string().c_str());
        UI::PropertyLabel("Skript moduli");
        ImGui::TextUnformatted(c.ScriptModulePath.empty() ? "-" : c.ScriptModulePath.generic_string().c_str());
        UI::EndPropertyGrid();
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

    bool ProjectSettingsPanel::DrawBinding(InputBinding& b, const ListenTarget& target)
    {
        bool changed = false;
        const float buttonSize = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float sourceWidth = std::min(ImGui::GetFontSize() * 9.0f, avail * 0.42f);
        const float codeWidth = std::max(40.0f, avail - sourceWidth - (buttonSize + spacing) * 2.0f - spacing);

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
        return changed;
    }

    bool ProjectSettingsPanel::DrawBindingList(const char* label, std::vector<InputBinding>& bindings, ListKind kind, int entry)
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextColored(ImVec4(0.72f, 0.75f, 0.80f, 1.0f), "%s", label);
        int remove = -1;
        for (int i = 0; i < (int)bindings.size(); i++)
        {
            ImGui::PushID(i);
            changed |= DrawBinding(bindings[i], { kind, entry, i });
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

    void ProjectSettingsPanel::DrawInputMap(InputActionMap& map)
    {
        UpdateListening(map);
        bool changed = false;
        const ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

        ImGui::TextDisabled("Harakat va o'qlarni nomi bilan tugma, sichqoncha yoki gamepad'ga bog'lang.");
        ImGui::Spacing();

        // ---- actions
        ImGui::SeparatorText(ICON_FA_HAND_POINTER "  Harakatlar (actions)");
        int removeAction = -1;
        for (int i = 0; i < (int)map.Actions.size(); i++)
        {
            InputAction& action = map.Actions[i];
            ImGui::PushID(i);
            bool open = ImGui::TreeNodeEx("##action", nodeFlags, "%s", action.Name.empty() ? "(nomsiz)" : action.Name.c_str());
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
                changed |= DrawBindingList("Bog'lanishlar", action.Bindings, ListKind::Action, i);
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
            ImGui::PushID(1000 + i);
            bool open = ImGui::TreeNodeEx("##axis", nodeFlags, "%s", axis.Name.empty() ? "(nomsiz)" : axis.Name.c_str());
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
                changed |= DrawBindingList("Manfiy tomon (-1)", axis.Negative, ListKind::AxisNegative, i);
                changed |= DrawBindingList("Musbat tomon (+1)", axis.Positive, ListKind::AxisPositive, i);
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

}
