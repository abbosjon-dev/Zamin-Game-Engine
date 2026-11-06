#include "ProjectSettingsPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <unordered_map>

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
            auto tabFlags = [&](Tab tab) {
                return m_RequestedTab == (int)tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            };
            if (ImGui::BeginTabItem(ICON_FA_GAMEPAD "  Input", nullptr, tabFlags(Tab::Input)))
            {
                DrawInputMap(config.InputActions);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_FLOPPY_DISK "  Saqlangan ma'lumotlar", nullptr, tabFlags(Tab::SaveData)))
            {
                DrawSaveData();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_TAGS "  Teglar", nullptr, tabFlags(Tab::Tags)))
            {
                DrawTags(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_LAYER_GROUP "  Saralash qatlamlari", nullptr, tabFlags(Tab::SortingLayers)))
            {
                DrawSortingLayers(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_TABLE_CELLS "  Fizika qatlamlari", nullptr, tabFlags(Tab::PhysicsLayers)))
            {
                DrawPhysicsLayers(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_LIST_OL "  Build sahnalar", nullptr, tabFlags(Tab::BuildScenes)))
            {
                DrawBuildScenes(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_SLIDERS "  Umumiy", nullptr, tabFlags(Tab::General)))
            {
                DrawGeneral(config);
                ImGui::EndTabItem();
            }
            m_RequestedTab = -1;
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

        // Debug drawing in the player (the editor viewport has its own toggles in the toolbar).
        UI::PropertyHeader(ICON_FA_BUG "  Debug chizish (o'yinda)",
                           "Skriptlardagi DebugDraw chaqiruvlari (chiziq, quti, sfera, o'q, matn) va fizika ko'rinishlari "
                           "ZaminPlayer'da. Shipping build'da debug chizish umuman yo'q; --no-debug-draw bayrog'i ham o'chiradi. "
                           "Editor viewport'ining o'z sozlamalari bor (asboblar panelidagi " ICON_FA_BUG " tugma).");
        bool changed = false;
        DebugDrawSettings& d = c.DebugDraw;
        changed |= UI::PropertyBool("Yoqilgan", d.Enabled);
        ImGui::BeginDisabled(!d.Enabled);
        changed |= UI::PropertyBool("Raycastlar", d.ShowRaycasts);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Har bir Raycast2D / Raycast3D so'rovi: nur, tegish nuqtasi va normal (tegmasa qizil)");
        if (d.ShowRaycasts)
            changed |= UI::PropertyFloat("Raycast vaqti (s)", d.RaycastDuration, 0.01f, 0.0f, DebugDrawSettings::MaxRaycastDuration, "%.2f");
        changed |= UI::PropertyBool("Kontakt nuqtalari", d.ShowContacts);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Fizik jismlar tegib turgan nuqtalar va normallari (2D va 3D): uxlayotganlari xira, qizil chiziq - shakllar "
                        "qancha kirgani, halqa - 3D zarba; --debug-contacts bayrog'i ham yoqadi");
        changed |= UI::PropertyBool("Joint'lar", d.ShowJoints);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ishlayotgan 2D / 3D joint'lar: langarlar, o'qlar, uzilishga yaqinligi rangda; --debug-joints bayrog'i ham yoqadi");
        if (d.ShowContacts || d.ShowJoints)
        {
            changed |= UI::PropertyBool("Fizika qiymatlari", d.ShowContactDetails);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Kontaktlar ustida chuqurlik, zarba tezligi va impuls, joint'larda kuch / moment yozuvlari; "
                            "--debug-contact-details bayrog'i ham yoqadi");
        }
        changed |= UI::PropertyBool("Personaj tezligi", d.ShowVelocities);
        if (ImGui::IsItemHovered())
            UI::Tooltip("3D character controller'larning tezlik vektori (o'q va tezlik, m/s); --debug-velocities bayrog'i ham yoqadi");
        changed |= UI::PropertyBool("Jismlar tezligi", d.ShowBodyVelocities);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Harakatlanayotgan dinamik jismlarning (2D va 3D) tezlik vektori");
        if (d.ShowVelocities || d.ShowBodyVelocities)
            changed |= UI::PropertyFloat("Tezlik o'qi (s)", d.VelocityScale, 0.01f, DebugDrawSettings::MinVelocityScale,
                                         DebugDrawSettings::MaxVelocityScale, "%.2f");
        changed |= UI::PropertyFloat("Chiziq qalinligi (px)", d.LineWidth, 0.05f, DebugDrawSettings::MinLineWidth, DebugDrawSettings::MaxLineWidth, "%.1f");
        changed |= UI::PropertyFloat("Matn o'lchami (px)", d.TextSize, 0.1f, DebugDrawSettings::MinTextSize, DebugDrawSettings::MaxTextSize, "%.0f");
        ImGui::EndDisabled();
        if (changed)
        {
            d.Sanitize();
            m_Dirty = true;
        }
        UI::EndPropertyGrid();
    }

    // ------------------------------------------------------------------ build scenes
    void ProjectSettingsPanel::DrawBuildScenes(ProjectConfig& config)
    {
        auto& list = config.BuildScenes;
        ImGui::TextWrapped("O'yin shu sahnalardan quriladi. Yoqilganlar tartib bo'yicha build indeksini oladi (0, 1, 2 ...): skriptda "
                           "LoadScene(1) yoki nomi bilan LoadScene(\"Level2\"). Ro'yxat bo'sh bo'lmasa, eksportga faqat shu sahnalar "
                           "(va boshlang'ich sahna) kiradi. Tartibni sudrab o'zgartiring (o'ng tugma: menyu).");
        ImGui::Spacing();

        // Add: the open scene, every scene of the project, or a .zscene dropped from the content browser.
        const std::string open = m_OpenScenePath.generic_string();
        const bool openListed = std::any_of(list.begin(), list.end(), [&](const BuildSceneEntry& e) { return e.Path.generic_string() == open; });
        ImGui::BeginDisabled(open.empty() || openListed);
        if (ImGui::Button(ICON_FA_PLUS "  Ochiq sahnani qo'shish") && Project::AddBuildScene(m_OpenScenePath))
            m_Dirty = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(open.empty() ? "Ochiq sahna hali saqlanmagan" : openListed ? "Ochiq sahna ro'yxatda bor" : open.c_str());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LAYER_GROUP "  Barcha sahnalarni qo'shish"))
        {
            std::vector<std::string> found;
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(Project::GetAssetDirectory(), ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                if (it->path().extension() == ".zscene")
                    found.push_back(Project::GetAssetRelativePath(it->path()));
            std::sort(found.begin(), found.end());
            size_t added = 0;
            for (const std::string& scene : found)
                added += Project::AddBuildScene(scene) ? 1 : 0;
            if (added)
                m_Dirty = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(list.empty());
        if (ImGui::Button(ICON_FA_TRASH "  Tozalash"))
        {
            list.clear();
            m_Dirty = true;
        }
        ImGui::EndDisabled();
        ImGui::Spacing();

        const std::string start = config.StartScene.generic_string();
        int remove = -1, moveFrom = -1, moveTo = -1;
        int buildIndex = 0;
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        const float button = ImGui::GetFrameHeight();
        if (ImGui::BeginTable("##buildScenes", 5, flags))
        {
            ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_WidthFixed, button * 1.6f);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00").x + 4.0f);
            ImGui::TableSetupColumn("Sahna", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##start", ImGuiTableColumnFlags_WidthFixed, button);
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, button);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)list.size(); i++)
            {
                BuildSceneEntry& entry = list[(size_t)i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                // Drag handle + on / off.
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled(ICON_FA_GRIP_LINES);
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("BUILD_SCENE", &i, sizeof(int));
                    ImGui::TextUnformatted(entry.Path.generic_string().c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::SameLine(0.0f, 2.0f);
                if (ImGui::Checkbox("##enabled", &entry.Enabled))
                    m_Dirty = true;
                if (ImGui::IsItemHovered())
                    UI::Tooltip(entry.Enabled ? "Buildga kiradi (o'chirilsa indeksi yo'qoladi)" : "O'chirilgan: buildga kirmaydi");
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                if (entry.Enabled)
                    ImGui::TextColored(UI::AccentColor(), "%d", buildIndex++);
                else
                    ImGui::TextDisabled("-");
                // The scene's name (its folder dimmed); drag to reorder, right-click for more.
                ImGui::TableSetColumnIndex(2);
                std::error_code ec;
                const bool exists = std::filesystem::exists(Project::GetAssetFileSystemPath(entry.Path), ec);
                const std::string folder = entry.Path.parent_path().generic_string();
                const std::string label = std::format("{}  {}", exists ? ICON_FA_EARTH_ASIA : ICON_FA_TRIANGLE_EXCLAMATION, entry.Path.stem().string());
                ImGui::PushStyleColor(ImGuiCol_Text, !exists ? ImVec4(0.95f, 0.45f, 0.40f, 1.0f)
                                                    : entry.Enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::AlignTextToFramePadding();
                ImGui::Selectable(label.c_str(), entry.Path.generic_string() == open, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap);
                ImGui::PopStyleColor();
                const bool rowHovered = ImGui::IsItemHovered();
                if (rowHovered)
                {
                    UI::Tooltip(!exists ? std::format("Fayl topilmadi: {}", entry.Path.generic_string()).c_str()
                                        : std::format("{}\nIkki marta bosing: ochish. Sudrab tartibni o'zgartiring, o'ng tugma: menyu",
                                                      entry.Path.generic_string()).c_str());
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && exists && OnOpenSceneRequested)
                        OnOpenSceneRequested(entry.Path);
                }
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("BUILD_SCENE", &i, sizeof(int));
                    ImGui::TextUnformatted(entry.Path.generic_string().c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BUILD_SCENE"))
                    {
                        moveFrom = *(const int*)payload->Data;
                        moveTo = i;
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem("##row"))
                {
                    if (ImGui::MenuItem(ICON_FA_ARROW_UP "  Yuqoriga", nullptr, false, i > 0))
                    {
                        moveFrom = i;
                        moveTo = i - 1;
                    }
                    if (ImGui::MenuItem(ICON_FA_ARROW_DOWN "  Pastga", nullptr, false, i + 1 < (int)list.size()))
                    {
                        moveFrom = i;
                        moveTo = i + 1;
                    }
                    if (ImGui::MenuItem(ICON_FA_STAR "  Boshlang'ich sahna qilish", nullptr, false, entry.Path.generic_string() != start))
                    {
                        config.StartScene = entry.Path;
                        config.StartSceneHandle = {};
                        m_Dirty = true;
                    }
                    if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Ochish", nullptr, false, exists && OnOpenSceneRequested != nullptr))
                        OnOpenSceneRequested(entry.Path);
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_FA_XMARK "  Ro'yxatdan olib tashlash"))
                        remove = i;
                    ImGui::EndPopup();
                }
                if (!folder.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", folder.c_str());
                }
                // The start scene (the player starts with it).
                ImGui::TableSetColumnIndex(3);
                const bool isStart = entry.Path.generic_string() == start;
                ImGui::PushStyleColor(ImGuiCol_Text, isStart ? ImVec4(1.0f, 0.82f, 0.30f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (UI::ToolbarButton(ICON_FA_STAR, isStart ? "Boshlang'ich sahna (player shu bilan boshlanadi)" : "Boshlang'ich sahna qilish", false,
                                      ImVec2(button, 0)) && !isStart)
                {
                    config.StartScene = entry.Path;
                    config.StartSceneHandle = {};
                    m_Dirty = true;
                }
                ImGui::PopStyleColor();
                ImGui::TableSetColumnIndex(4);
                if (UI::ToolbarButton(ICON_FA_XMARK, "Ro'yxatdan olib tashlash", false, ImVec2(button, 0)))
                    remove = i;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (list.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Ro'yxat bo'sh: eksportga hamma sahnalar kiradi, build indekslari yo'q.");
        }
        // A scene file dropped from the content browser joins the list.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##buildDrop", ImVec2(std::max(1.0f, avail.x), std::max(30.0f, std::min(avail.y, 80.0f))));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::filesystem::path path((const char*)payload->Data);
                if (Project::AddBuildScene(path.is_absolute() ? std::filesystem::path(Project::GetAssetRelativePath(path)) : path))
                    m_Dirty = true;
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BUILD_SCENE"))
            {
                moveFrom = *(const int*)payload->Data;
                moveTo = (int)list.size() - 1;
            }
            ImGui::EndDragDropTarget();
        }
        if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo && moveFrom < (int)list.size() && moveTo < (int)list.size())
        {
            BuildSceneEntry moved = list[(size_t)moveFrom];
            list.erase(list.begin() + moveFrom);
            list.insert(list.begin() + moveTo, moved);
            m_Dirty = true;
        }
        else if (remove >= 0 && remove < (int)list.size())
        {
            list.erase(list.begin() + remove);
            m_Dirty = true;
        }
        if (!start.empty() && !list.empty() && Project::GetBuildIndex(config.StartScene) < 0)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.32f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Boshlang'ich sahna (%s) ro'yxatda yo'q yoki o'chirilgan: "
                               "eksportga baribir kiradi, lekin build indeksi yo'q", start.c_str());
        }
    }

    // ------------------------------------------------------------------ tags
    static std::string TrimText(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    void ProjectSettingsPanel::DrawTags(ProjectConfig& config)
    {
        auto& tags = config.Tags;
        ImGui::TextWrapped("Teglar obyektlarni guruhlaydi (masalan, barcha dushmanlar). Inspektorda \"Teglar\" komponenti "
                           "bilan beriladi, skriptda HasTag(\"Enemy\") va FindEntitiesWithTag(\"Enemy\") bilan so'raladi. "
                           "Bu ro'yxat inspektordagi takliflar uchun; ro'yxatda yo'q teglar ham ishlaydi.");
        ImGui::Spacing();

        // Add a new tag
        std::string trimmed = TrimText(m_NewTag);
        const bool duplicate = std::find(tags.begin(), tags.end(), trimmed) != tags.end();
        ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.6f));
        bool submit = ImGui::InputTextWithHint("##newTag", "Yangi teg nomi", &m_NewTag, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(trimmed.empty() || duplicate);
        submit |= ImGui::Button(ICON_FA_PLUS "  Qo'shish");
        ImGui::EndDisabled();
        if (submit && !trimmed.empty() && !duplicate)
        {
            tags.push_back(trimmed);
            m_NewTag.clear();
            m_Dirty = true;
        }
        if (duplicate && !trimmed.empty())
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Bu teg allaqachon bor");
        }

        // Tags used in the open scene but missing from the list
        std::vector<std::string> missing;
        if (m_Scene)
            for (auto& t : m_Scene->GetAllTags())
                if (std::find(tags.begin(), tags.end(), t) == tags.end())
                    missing.push_back(t);
        if (!missing.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button(std::format(ICON_FA_PLUS "  Sahnadagilarni qo'shish ({})", missing.size()).c_str()))
            {
                tags.insert(tags.end(), missing.begin(), missing.end());
                m_Dirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                std::string list;
                for (auto& t : missing) list += (list.empty() ? "" : ", ") + t;
                UI::Tooltip(("Sahnada ishlatilgan, ro'yxatda yo'q: " + list).c_str());
            }
        }
        ImGui::Spacing();

        if (tags.empty())
        {
            ImGui::TextDisabled("Ro'yxat bo'sh");
            return;
        }

        int remove = -1, moveUp = -1;
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##tags", 3, flags))
        {
            ImGui::TableSetupColumn("Teg", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Sahnada", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 3.4f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)tags.size(); i++)
            {
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const size_t used = m_Scene ? m_Scene->CountEntitiesWithTag(tags[i]) : 0;
                // Renaming also renames the tag on the scene's entities: only while editing.
                const bool canRename = m_SceneEditable || used == 0;
                if (m_RenamingTag == i && !canRename)
                    m_RenamingTag = -1; // play started while the rename field was open
                if (m_RenamingTag == i)
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (m_FocusTagRename)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_FocusTagRename = false;
                    }
                    if (ImGui::InputText("##rename", &m_TagRenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        std::string name = TrimText(m_TagRenameBuffer);
                        if (!name.empty() && std::find(tags.begin(), tags.end(), name) == tags.end())
                        {
                            // Rename in the open scene too, so existing entities keep the tag.
                            if (m_Scene)
                            {
                                bool sceneChanged = false;
                                for (Entity e : m_Scene->FindEntitiesWithTag(tags[i]))
                                {
                                    auto& c = e.GetComponent<TagsComponent>();
                                    if (c.Has(name)) c.Remove(tags[i]);
                                    else std::replace(c.Tags.begin(), c.Tags.end(), tags[i], name);
                                    sceneChanged = true;
                                }
                                if (sceneChanged && OnSceneModified)
                                    OnSceneModified();
                            }
                            tags[i] = name;
                            m_Dirty = true;
                        }
                        m_RenamingTag = -1;
                    }
                    else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape))
                        m_RenamingTag = -1;
                }
                else
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text(ICON_FA_TAG "  %s", tags[i].c_str());
                    if (canRename && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        m_RenamingTag = i;
                        m_TagRenameBuffer = tags[i];
                        m_FocusTagRename = true;
                    }
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                if (used > 0) ImGui::Text("%zu ta obyekt", used);
                else ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(2);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::BeginDisabled(i == 0);
                if (ImGui::Button(ICON_FA_ARROW_UP)) moveUp = i;
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) UI::Tooltip("Yuqoriga");
                ImGui::SameLine();
                ImGui::BeginDisabled(!canRename);
                if (ImGui::Button(ICON_FA_PEN))
                {
                    m_RenamingTag = i;
                    m_TagRenameBuffer = tags[i];
                    m_FocusTagRename = true;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    UI::Tooltip(canRename ? "Qayta nomlash (sahnadagi obyektlarda ham)"
                                          : "Sahnada ishlatilgan tegni o'yin davomida qayta nomlab bo'lmaydi");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_TRASH)) remove = i;
                if (ImGui::IsItemHovered()) UI::Tooltip("Ro'yxatdan olib tashlash (obyektlardagi teglar qoladi)");
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (moveUp > 0)
        {
            std::swap(tags[moveUp], tags[moveUp - 1]);
            m_Dirty = true;
        }
        if (remove >= 0)
        {
            tags.erase(tags.begin() + remove);
            if (m_RenamingTag == remove) m_RenamingTag = -1;
            m_Dirty = true;
        }
    }

    // ------------------------------------------------------------------ sorting layers
    // How many 2D renderers of the scene are on each layer (by ID).
    static std::unordered_map<uint32_t, size_t> CountSortingLayerUse(Scene& scene)
    {
        std::unordered_map<uint32_t, size_t> used;
        for (auto e : scene.GetAllEntitiesWith<SpriteRendererComponent>())
            used[Entity(e, &scene).GetComponent<SpriteRendererComponent>().SortingLayer]++;
        for (auto e : scene.GetAllEntitiesWith<CircleRendererComponent>())
            used[Entity(e, &scene).GetComponent<CircleRendererComponent>().SortingLayer]++;
        for (auto e : scene.GetAllEntitiesWith<TextComponent>())
            used[Entity(e, &scene).GetComponent<TextComponent>().SortingLayer]++;
        for (auto e : scene.GetAllEntitiesWith<TilemapComponent>())
            used[Entity(e, &scene).GetComponent<TilemapComponent>().SortingLayer]++;
        for (auto e : scene.GetAllEntitiesWith<ParticleSystemComponent>())
            used[Entity(e, &scene).GetComponent<ParticleSystemComponent>().SortingLayer]++;
        return used;
    }

    void ProjectSettingsPanel::DrawPhysicsLayers(ProjectConfig& config)
    {
        CollisionLayers2D& layers = config.CollisionLayers;
        ImGui::TextWrapped("Fizikaning 16 ta to'qnashuv qatlami (2D va 3D uchun bir xil). Rigidbody 2D (va xohlasa har bir kollider), "
                           "tilemap'lar, Rigidbody 3D va Character Controller 3D bitta qatlamda bo'ladi; ikki kollider faqat quyidagi "
                           "jadvalda belgilangan qatlamlar juftligida bir-biriga tegadi (raycast va overlap so'rovlari ham qatlam "
                           "niqobini oladi). 2D uchun o'zgarishlar keyingi Play / Simulate'da, 3D uchun darhol qo'llanadi.");
        ImGui::Spacing();

        // How many bodies / colliders / tilemaps of the edited scene use each layer.
        std::array<int, CollisionLayers2D::Count> usage{};
        if (m_Scene)
        {
            auto count = [&](int layer) {
                if (CollisionLayers2D::IsValid(layer))
                    usage[(size_t)layer]++;
            };
            for (auto e : m_Scene->GetAllEntitiesWith<Rigidbody2DComponent>())
                count(m_Scene->GetRegistry().get<Rigidbody2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<BoxCollider2DComponent>()) count(m_Scene->GetRegistry().get<BoxCollider2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<CircleCollider2DComponent>()) count(m_Scene->GetRegistry().get<CircleCollider2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<PolygonCollider2DComponent>()) count(m_Scene->GetRegistry().get<PolygonCollider2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<EdgeCollider2DComponent>()) count(m_Scene->GetRegistry().get<EdgeCollider2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<TilemapComponent>()) count(m_Scene->GetRegistry().get<TilemapComponent>(e).CollisionLayer);
            for (auto e : m_Scene->GetAllEntitiesWith<Rigidbody3DComponent>()) count(m_Scene->GetRegistry().get<Rigidbody3DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<CharacterController3DComponent>())
                count(m_Scene->GetRegistry().get<CharacterController3DComponent>(e).Layer);
        }

        // ---- the matrix of the named layers (a triangle: each pair once)
        ImGui::SeparatorText("Qaysi qatlamlar to'qnashadi");
        std::vector<int> named = layers.GetNamedLayers();
        for (int i = 0; i < CollisionLayers2D::Count; i++)
            if (usage[(size_t)i] > 0 && std::find(named.begin(), named.end(), i) == named.end())
                named.insert(std::upper_bound(named.begin(), named.end(), i), i); // unnamed, but used
        if (ImGui::SmallButton(ICON_FA_CHECK_DOUBLE "  Hammasi to'qnashadi"))
        {
            layers.Masks.fill(CollisionLayers2D::AllLayers);
            m_Dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_SQUARE "  Hech biri"))
        {
            for (int a : named)
                for (int b : named)
                    layers.SetCollision(a, b, false);
            m_Dirty = true;
        }
        ImGui::Spacing();
        const int columns = (int)named.size() + 1;
        const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_HighlightHoveredColumn;
        if (columns > 1 && ImGui::BeginTable("##physicsMatrix", columns, flags))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder, 140.0f);
            // Columns from the last layer to the first: the rows' cells fill a staircase towards the left.
            std::vector<int> columnLayers(named.rbegin(), named.rend());
            std::vector<std::string> labels;
            for (int l : columnLayers)
                labels.push_back(std::format("{}  {}", l, layers.GetName(l)));
            for (size_t c = 0; c < columnLayers.size(); c++)
                ImGui::TableSetupColumn(labels[c].c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed, 0.0f);
            ImGui::TableAngledHeadersRow();
            for (size_t r = 0; r < named.size(); r++)
            {
                const int a = named[r];
                ImGui::PushID(a);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(std::format("{}  {}", a, layers.GetName(a)).c_str());
                for (size_t c = 0; c < columnLayers.size(); c++)
                {
                    const int b = columnLayers[c];
                    // Each pair once: the row's layer against itself and the layers after it.
                    if (b < a)
                        continue;
                    ImGui::TableSetColumnIndex((int)c + 1);
                    ImGui::PushID(b);
                    bool collide = layers.CanCollide(a, b);
                    if (ImGui::Checkbox("##pair", &collide))
                    {
                        layers.SetCollision(a, b, collide);
                        m_Dirty = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip(std::format("{} va {}: {}", layers.GetName(a), layers.GetName(b), collide ? "to'qnashadi" : "bir-biridan o'tib ketadi").c_str());
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        // ---- names
        ImGui::SeparatorText("Qatlamlar");
        if (ImGui::BeginTable("##physicsLayerNames", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26.0f);
            ImGui::TableSetupColumn("Nomi", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Sahnada", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < CollisionLayers2D::Count; i++)
            {
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%d", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (i == 0)
                {
                    ImGui::BeginDisabled();
                    std::string name = layers.Names[0];
                    ImGui::InputText("##name", &name);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        UI::Tooltip("Asosiy qatlam: hamma narsa avval shu qatlamda");
                }
                else if (ImGui::InputTextWithHint("##name", std::format("(Qatlam {})", i).c_str(), &layers.Names[(size_t)i]))
                    m_Dirty = true;
                ImGui::TableSetColumnIndex(2);
                ImGui::AlignTextToFramePadding();
                if (usage[(size_t)i] > 0)
                    ImGui::Text("%d", usage[(size_t)i]);
                else
                    ImGui::TextDisabled("-");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Skriptlarda: Raycast2D / Raycast3D(..., LayerMask({ \"Ground\" })), OverlapSphere3D, LayerIndex(\"Player\")");
    }

    void ProjectSettingsPanel::DrawSortingLayers(ProjectConfig& config)
    {
        SortingLayerList& layers = config.SortingLayers;
        ImGui::TextWrapped("2D renderer'lar (sprite, doira, matn, tilemap, particle) shu qatlamlarning birida chiziladi. Ro'yxat "
                           "tartibi - chizish tartibi: yuqoridagi qatlam orqada, pastdagisi hammasidan oldinda. Qatlam ichida "
                           "\"Qatlamdagi tartib\", keyin kameradan uzoqlik hal qiladi. Obyektlar qatlamni ID bilan eslaydi: "
                           "qayta nomlash va surish sahnalarni buzmaydi; o'chirilgan qatlamdagilar Default'da chiziladi.");
        ImGui::TextWrapped("\"Y\" belgilangan qatlam (top-down o'yinlar) avval Y bo'yicha saralanadi: ekranda yuqoriroq turgan "
                           "(Y katta) orqada, pastroqdagisi oldinda chiziladi. Saralash nuqtasi - obyektning pivot'i (oyoqlari); "
                           "shu qatlamdagi ota-obyekt ostidagi qismlar otaning Y'i bilan birga, o'zaro tartib bo'yicha chiziladi.");
        ImGui::Spacing();

        std::string trimmed = TrimText(m_NewSortingLayer);
        const bool duplicate = !trimmed.empty() && layers.FindByName(trimmed);
        const bool full = layers.GetCount() >= SortingLayerList::MaxLayers;
        ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.6f));
        bool submit = ImGui::InputTextWithHint("##newLayer", "Yangi qatlam nomi", &m_NewSortingLayer, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(trimmed.empty() || duplicate || full);
        submit |= ImGui::Button(ICON_FA_PLUS "  Qo'shish##layer");
        ImGui::EndDisabled();
        if (submit && !trimmed.empty() && !duplicate && !full && layers.Add(trimmed))
        {
            m_NewSortingLayer.clear();
            m_Dirty = true;
        }
        if (duplicate)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Bu nom band");
        }
        ImGui::Spacing();

        const std::unordered_map<uint32_t, size_t> used = m_Scene ? CountSortingLayerUse(*m_Scene) : std::unordered_map<uint32_t, size_t>();
        int moveFrom = -1, moveTo = -1;
        uint32_t remove = ~0u;
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
        uint32_t ySortToggle = ~0u;
        if (ImGui::BeginTable("##sortingLayers", 4, flags))
        {
            const float buttons = 4.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.2f);
            ImGui::TableSetupColumn("Qatlam (sahnada)", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 4.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, buttons);
            ImGui::TableHeadersRow();
            const auto& list = layers.GetLayers();
            for (int i = 0; i < (int)list.size(); i++)
            {
                const SortingLayer& layer = list[(size_t)i];
                ImGui::PushID((int)layer.ID);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%d", i + 1);
                ImGui::TableSetColumnIndex(1);
                if (m_RenamingLayer == layer.ID)
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (m_FocusLayerRename)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_FocusLayerRename = false;
                    }
                    if (ImGui::InputText("##rename", &m_LayerRenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        if (layers.Rename(layer.ID, TrimText(m_LayerRenameBuffer)))
                            m_Dirty = true;
                        m_RenamingLayer = ~0u;
                    }
                    else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape))
                        m_RenamingLayer = ~0u;
                }
                else
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text(ICON_FA_LAYER_GROUP "  %s", layer.Name.c_str());
                    if (ImGui::IsItemHovered())
                    {
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            m_RenamingLayer = layer.ID;
                            m_LayerRenameBuffer = layer.Name;
                            m_FocusLayerRename = true;
                        }
                        UI::Tooltip(layer.ID == SortingLayerList::DefaultID ? "Asosiy qatlam (o'chirib bo'lmaydi). Ikki marta bosing: qayta nomlash"
                                                                            : "Ikki marta bosing: qayta nomlash");
                    }
                    auto it = used.find(layer.ID);
                    ImGui::SameLine();
                    if (it != used.end() && it->second > 0)
                        ImGui::TextDisabled("%zu", it->second);
                    else
                        ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(2);
                bool ySort = layer.YSort;
                if (ImGui::Checkbox("##ysort", &ySort))
                    ySortToggle = layer.ID;
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Y bo'yicha saralash (top-down): Y katta - orqada, kichik - oldinda. Tartib faqat bir xil "
                                "Y'dagilarni (bitta qahramon qismlarini) ajratadi");
                ImGui::TableSetColumnIndex(3);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::BeginDisabled(i == 0);
                if (ImGui::Button(ICON_FA_ARROW_UP)) { moveFrom = i; moveTo = i - 1; }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) UI::Tooltip("Orqaroqqa (oldinroq chiziladi)");
                ImGui::SameLine();
                ImGui::BeginDisabled(i + 1 == (int)list.size());
                if (ImGui::Button(ICON_FA_ARROW_DOWN)) { moveFrom = i; moveTo = i + 1; }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) UI::Tooltip("Oldinroqqa (keyinroq chiziladi)");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_PEN))
                {
                    m_RenamingLayer = layer.ID;
                    m_LayerRenameBuffer = layer.Name;
                    m_FocusLayerRename = true;
                }
                if (ImGui::IsItemHovered()) UI::Tooltip("Qayta nomlash (obyektlar qatlamni ID bilan eslaydi)");
                ImGui::SameLine();
                ImGui::BeginDisabled(layer.ID == SortingLayerList::DefaultID);
                if (ImGui::Button(ICON_FA_TRASH)) remove = layer.ID;
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    UI::Tooltip(layer.ID == SortingLayerList::DefaultID ? "Default qatlamini o'chirib bo'lmaydi"
                                                                        : "O'chirish (undagi obyektlar Default'da chiziladi)");
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ySortToggle != ~0u && layers.SetYSort(ySortToggle, !layers.IsYSorted(ySortToggle)))
            m_Dirty = true;
        if (moveFrom >= 0 && layers.Move((size_t)moveFrom, (size_t)moveTo))
            m_Dirty = true;
        if (remove != ~0u && layers.Remove(remove))
        {
            if (m_RenamingLayer == remove) m_RenamingLayer = ~0u;
            m_Dirty = true;
        }
    }

    // ------------------------------------------------------------------ save data
    void ProjectSettingsPanel::DrawSaveData()
    {
        namespace fs = std::filesystem;
        const fs::path file = SaveData::GetFilePath();

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("O'yin skriptlari SaveData::SetInt / SetFloat / SetString / SetBool bilan saqlagan qiymatlar "
                           "(o'yinlar orasida saqlanadi).");
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Fayl:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", file.string().c_str());

        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Faylga yozish"))
            SaveData::Save();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Qayta o'qish"))
            SaveData::Reload();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Saqlanmagan o'zgarishlarni bekor qilib, faylni qaytadan o'qish");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Papkani ochish"))
        {
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            Platform::OpenInFileBrowser(file.parent_path());
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH "  Hammasini o'chirish"))
            ImGui::OpenPopup("ClearSaveData");
        if (ImGui::BeginPopup("ClearSaveData"))
        {
            ImGui::TextUnformatted("Barcha saqlangan qiymatlar o'chirilsinmi? (fayl ham yangilanadi)");
            if (ImGui::Button("Ha, o'chirish"))
            {
                SaveData::DeleteAll();
                SaveData::Save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Yo'q"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (SaveData::IsDirty())
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Faylga hali yozilmagan o'zgarishlar bor");
        ImGui::Spacing();

        // ---- values
        const std::vector<std::string> keys = SaveData::GetKeys();
        std::string remove;
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
        if (keys.empty())
            ImGui::TextDisabled("Hozircha hech narsa saqlanmagan");
        else if (ImGui::BeginTable("##saveData", 4, tableFlags))
        {
            ImGui::TableSetupColumn("Kalit", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("Tur", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.8f);
            ImGui::TableSetupColumn("Qiymat", ImGuiTableColumnFlags_WidthStretch, 0.62f);
            ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableHeadersRow();
            for (const std::string& key : keys)
            {
                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(key.c_str());
                ImGui::TableNextColumn();
                SaveValueType type = SaveData::GetType(key);
                ImGui::TextDisabled("%s", SaveData::TypeToString(type));
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                switch (type)
                {
                    case SaveValueType::Int:
                    {
                        int v = SaveData::GetInt(key);
                        if (ImGui::InputInt("##v", &v))
                            SaveData::SetInt(key, v);
                        break;
                    }
                    case SaveValueType::Float:
                    {
                        float v = SaveData::GetFloat(key);
                        if (ImGui::DragFloat("##v", &v, 0.01f, 0.0f, 0.0f, "%.4g"))
                            SaveData::SetFloat(key, v);
                        break;
                    }
                    case SaveValueType::String:
                    {
                        std::string v = SaveData::GetString(key);
                        if (ImGui::InputText("##v", &v))
                            SaveData::SetString(key, v);
                        break;
                    }
                    case SaveValueType::Bool:
                    {
                        bool v = SaveData::GetBool(key);
                        if (ImGui::Checkbox("##v", &v))
                            SaveData::SetBool(key, v);
                        break;
                    }
                    default:
                        break;
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(ICON_FA_TRASH, ImVec2(ImGui::GetFrameHeight(), 0)))
                    remove = key;
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Kalitni o'chirish");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!remove.empty())
            SaveData::DeleteKey(remove);

        // ---- new value
        ImGui::Spacing();
        ImGui::SeparatorText(ICON_FA_PLUS "  Yangi qiymat");
        static const char* s_TypeNames[] = { "Int", "Float", "String", "Bool" };
        const float typeWidth = ImGui::GetFontSize() * 6.0f;
        ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - typeWidth - ImGui::GetFontSize() * 6.5f));
        bool submit = ImGui::InputTextWithHint("##newKey", "kalit nomi", &m_NewSaveKey, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(typeWidth);
        ImGui::Combo("##newType", &m_NewSaveType, s_TypeNames, IM_ARRAYSIZE(s_TypeNames));
        ImGui::SameLine();
        const bool exists = !m_NewSaveKey.empty() && SaveData::HasKey(m_NewSaveKey);
        ImGui::BeginDisabled(m_NewSaveKey.empty() || exists);
        submit |= ImGui::Button(ICON_FA_PLUS "  Qo'shish");
        ImGui::EndDisabled();
        if (exists)
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Bu kalit allaqachon bor");
        if (submit && !m_NewSaveKey.empty() && !exists)
        {
            switch (m_NewSaveType)
            {
                case 0: SaveData::SetInt(m_NewSaveKey, 0); break;
                case 1: SaveData::SetFloat(m_NewSaveKey, 0.0f); break;
                case 2: SaveData::SetString(m_NewSaveKey, ""); break;
                default: SaveData::SetBool(m_NewSaveKey, false); break;
            }
            m_NewSaveKey.clear();
        }
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
        ImGui::TextDisabled("Skriptda: Input::IsActionJustPressed(\"Jump\"), Input::GetAxis(\"Horizontal\")");
        ImGui::Spacing();

        // ---- actions
        ImGui::SeparatorText(ICON_FA_HAND_POINTER "  Harakatlar (actions)");
        int removeAction = -1;
        for (int i = 0; i < (int)map.Actions.size(); i++)
        {
            InputAction& action = map.Actions[i];
            ImGui::PushID(i);
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
