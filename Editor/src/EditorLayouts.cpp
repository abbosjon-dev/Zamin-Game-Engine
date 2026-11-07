// The editor's window layouts (Oyna > Joylashuv): the built-in ones built with ImGui's dock builder
// (Standart, 2D, Animatsiya, 4 ko'rinish), the user's saved ones - ImGui's window / docking settings
// with the open windows, one .zlayout file each in ~/.config/zamin/layouts - and the windows to
// save, apply, rename and delete them.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <cstdlib>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {
        const char* BuiltInIcon(BuiltInLayout layout)
        {
            switch (layout)
            {
                case BuiltInLayout::Mode2D: return ICON_FA_IMAGE;
                case BuiltInLayout::Animation: return ICON_FA_TIMELINE;
                case BuiltInLayout::FourViews: return ICON_FA_TABLE_CELLS_LARGE;
                default: return ICON_FA_BORDER_ALL;
            }
        }

        const char* BuiltInDescription(BuiltInLayout layout)
        {
            switch (layout)
            {
                case BuiltInLayout::Mode2D: return "Ierarxiya ostida plitka palitrasi, viewport 2D rejimda";
                case BuiltInLayout::Animation: return "Pastda katta Animatsiya va Animator grafi oynalari";
                case BuiltInLayout::FourViews: return "Viewport va uchta ortografik ko'rinish: yuqoridan, old, o'ng (2x2)";
                default: return "Ierarxiya chapda, inspektor o'ngda, kontent va konsol pastda";
            }
        }
    }

    std::vector<EditorLayer::LayoutPanel> EditorLayer::GetLayoutPanels()
    {
        return {
            { "Hierarchy", "###Hierarchy", &m_ShowHierarchy },
            { "Inspector", "###Inspector", &m_ShowInspector },
            { "ContentBrowser", "###ContentBrowser", &m_ShowContentBrowser },
            { "Console", "###Console", &m_ShowConsole },
            { "SceneSettings", "###SceneSettings", &m_ShowSceneSettings },
            { "Scenes", "###ScenesPanel", &m_ShowScenes },
            { "ProjectSettings", "###ProjectSettings", &m_ShowProjectSettings },
            { "Stats", "###Stats", &m_ShowStats },
            { "Profiler", "###Profiler", &m_ShowProfiler },
            { "AudioMixer", "###AudioMixer", &m_ShowAudioMixer },
            { "Animation", "###Animation", &m_ShowAnimation },
            { "AnimatorGraph", "###AnimatorGraph", &m_ShowAnimatorGraph },
            { "SpriteEditor", "###SpriteEditor", &m_ShowSpriteEditor },
            { "TilePalette", "###TilePalette", &m_ShowTilePalette },
            { "UndoHistory", "###UndoHistory", &m_ShowUndoHistory },
            { "UnusedAssets", "###UnusedAssets", &m_ShowUnusedAssets },
            { "SceneStatistics", "###SceneStatistics", &m_ShowSceneStatistics },
            { "SceneView2", "###SceneView2", &m_ViewSettings.SceneViews[0].Open },
            { "SceneView3", "###SceneView3", &m_ViewSettings.SceneViews[1].Open },
            { "SceneView4", "###SceneView4", &m_ViewSettings.SceneViews[2].Open },
        };
    }

    void EditorLayer::RequestBuiltInLayout(BuiltInLayout layout)
    {
        m_PendingBuiltInLayout = layout;
        m_PendingLayout.clear();
    }

    void EditorLayer::RequestLayout(const std::string& name)
    {
        if (std::optional<BuiltInLayout> builtIn = EditorLayoutLibrary::FindBuiltIn(name))
        {
            RequestBuiltInLayout(*builtIn);
            return;
        }
        m_PendingLayout = name;
        m_PendingBuiltInLayout.reset();
    }

    void EditorLayer::ApplyPendingLayout()
    {
        if (m_PendingLayout.empty())
            return;
        const std::string name = std::move(m_PendingLayout);
        m_PendingLayout.clear();
        std::optional<EditorLayout> layout = m_Layouts.Load(name);
        if (!layout || layout->Ini.empty())
        {
            ZM_WARN("Joylashuv o'qilmadi: '{}'", name);
            ShowViewportMessage(std::format(ICON_FA_TRIANGLE_EXCLAMATION "  Joylashuv o'qilmadi: {}", name));
            return;
        }
        // Between frames ImGui takes window settings: the dock nodes are rebuilt from the text, the
        // windows it names go back where they were (the others float).
        ImGui::LoadIniSettingsFromMemory(layout->Ini.c_str(), layout->Ini.size());
        for (const LayoutPanel& panel : GetLayoutPanels())
            *panel.Open = layout->IsPanelOpen(panel.Key, *panel.Open);
        m_LayoutInitialized = true;
        m_Preferences.Layout.Current = layout->Name;
        MarkPreferencesDirty();
        MarkViewSettingsDirty(); // the extra views' open state
        ZM_INFO("Joylashuv qo'llandi: {}", layout->Name);
        ShowViewportMessage(std::format(ICON_FA_WINDOW_RESTORE "  Joylashuv: {}", layout->Name));
    }

    void EditorLayer::UI_BuildLayout(BuiltInLayout layout, ImGuiID dockspaceID)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspaceID);
        ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->WorkSize);

        // Which windows are open: the layout's own set (the rest closed).
        auto open = [&](std::initializer_list<const char*> keys) {
            for (const LayoutPanel& panel : GetLayoutPanels())
                *panel.Open = std::find_if(keys.begin(), keys.end(), [&](const char* key) { return std::string_view(key) == panel.Key; }) != keys.end();
        };
        auto dock = [](ImGuiID node, std::initializer_list<const char*> windows) {
            for (const char* window : windows)
                ImGui::DockBuilderDockWindow(window, node);
        };
        // The tool windows that are closed in every layout go with the content browser.
        auto dockTools = [&](ImGuiID bottom, ImGuiID left, ImGuiID right) {
            dock(bottom, { "###TilePalette", "###AudioMixer", "###Profiler", "###UnusedAssets", "###SpriteEditor" });
            dock(left, { "###Stats" });
            dock(right, { "###ProjectSettings", "###UndoHistory" });
        };

        ImGuiID main = dockspaceID;
        switch (layout)
        {
            case BuiltInLayout::Mode2D:
            {
                ImGuiID left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.18f, nullptr, &main);
                ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.25f, nullptr, &main);
                ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.28f, nullptr, &main);
                ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
                dock(left, { "###Hierarchy" });
                dock(leftBottom, { "###TilePalette" });
                dock(right, { "###Inspector", "###SceneSettings", "###ProjectSettings", "###UndoHistory" });
                dock(bottom, { "###ContentBrowser", "###ScenesPanel", "###Console", "###SpriteEditor", "###Animation", "###AnimatorGraph",
                               "###AudioMixer", "###Profiler", "###UnusedAssets" });
                dock(main, { "###Viewport" });
                dock(left, { "###Stats" });
                open({ "Hierarchy", "Inspector", "ContentBrowser", "Console", "SceneSettings", "Scenes", "TilePalette" });
                if (!m_2DMode)
                    Set2DMode(true);
                break;
            }
            case BuiltInLayout::Animation:
            {
                ImGuiID left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.17f, nullptr, &main);
                ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.25f, nullptr, &main);
                ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.44f, nullptr, &main);
                ImGuiID bottomRight = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.34f, nullptr, &bottom);
                dock(left, { "###Hierarchy" });
                dock(right, { "###Inspector", "###SceneSettings" });
                dock(bottom, { "###Animation", "###AnimatorGraph" });
                dock(bottomRight, { "###ContentBrowser", "###ScenesPanel", "###Console" });
                dock(main, { "###Viewport" });
                dockTools(bottomRight, left, right);
                open({ "Hierarchy", "Inspector", "ContentBrowser", "Console", "SceneSettings", "Scenes", "Animation", "AnimatorGraph" });
                break;
            }
            case BuiltInLayout::FourViews:
            {
                ImGuiID left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.16f, nullptr, &main);
                ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.24f, nullptr, &main);
                ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.24f, nullptr, &main);
                // The scene area in four: the viewport top-left, Top top-right, Front bottom-left, Right bottom-right.
                ImGuiID topRight = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.5f, nullptr, &main);
                ImGuiID bottomLeft = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.5f, nullptr, &main);
                ImGuiID bottomRight = ImGui::DockBuilderSplitNode(topRight, ImGuiDir_Down, 0.5f, nullptr, &topRight);
                dock(left, { "###Hierarchy" });
                dock(right, { "###Inspector", "###SceneSettings" });
                dock(bottom, { "###ContentBrowser", "###ScenesPanel", "###Console", "###Animation", "###AnimatorGraph" });
                dock(main, { "###Viewport" });
                dock(topRight, { "###SceneView2" });
                dock(bottomLeft, { "###SceneView3" });
                dock(bottomRight, { "###SceneView4" });
                dockTools(bottom, left, right);
                open({ "Hierarchy", "Inspector", "ContentBrowser", "Console", "SceneSettings", "Scenes", "SceneView2", "SceneView3", "SceneView4" });
                // The three views look at what the viewport looks at: from above, the front, the right.
                static const ViewAxis axes[SceneViewSettings::Count] = { ViewAxis::Top, ViewAxis::Front, ViewAxis::Right };
                for (int i = 0; i < SceneViewSettings::Count; i++)
                {
                    EditorCamera& camera = m_ExtraViews[i].Camera;
                    EditorCameraState state = camera.GetAxisViewState(axes[i]);
                    state.FocalPoint = m_EditorCamera.GetFocalPoint();
                    state.Distance = std::max(m_EditorCamera.GetDistance(), 2.0f);
                    camera.SetViewState(state);
                    m_ExtraViews[i].CameraLoaded = true;
                }
                break;
            }
            default:
            {
                ImGuiID left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.18f, nullptr, &main);
                ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.26f, nullptr, &main);
                ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.30f, nullptr, &main);
                dock(left, { "###Hierarchy" });
                dock(right, { "###Inspector", "###SceneSettings", "###ProjectSettings" });
                dock(bottom, { "###ContentBrowser", "###ScenesPanel", "###Console", "###TilePalette", "###AudioMixer", "###Animation",
                               "###AnimatorGraph" });
                dock(left, { "###Stats" });
                dock(main, { "###Viewport" });
                dockTools(bottom, left, right);
                open({ "Hierarchy", "Inspector", "ContentBrowser", "Console", "SceneSettings", "Scenes" });
                break;
            }
        }
        ImGui::DockBuilderFinish(dockspaceID);
        MarkViewSettingsDirty();
    }

    bool EditorLayer::SaveCurrentLayout(const std::string& name)
    {
        EditorLayout layout;
        layout.Name = EditorLayoutLibrary::CleanName(name);
        size_t size = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&size);
        layout.Ini.assign(ini, size);
        for (const LayoutPanel& panel : GetLayoutPanels())
            layout.Panels[panel.Key] = *panel.Open;
        layout.SavedAt = GetUnixTime();
        std::string error;
        if (!m_Layouts.Save(layout, &error))
        {
            m_LayoutError = error;
            ZM_WARN("Joylashuv saqlanmadi: {}", error);
            return false;
        }
        m_Preferences.Layout.Current = layout.Name;
        MarkPreferencesDirty();
        ZM_INFO("Joylashuv saqlandi: {} ({} bayt)", layout.Name, layout.Ini.size());
        ShowViewportMessage(std::format(ICON_FA_FLOPPY_DISK "  Joylashuv saqlandi: {}", layout.Name));
        m_LayoutError.clear();
        return true;
    }

    // ------------------------------------------------------------------ menus and windows
    void EditorLayer::UI_LayoutMenu()
    {
        if (ImGui::BeginMenu(ICON_FA_WINDOW_RESTORE "  Joylashuv"))
        {
            const std::string& current = m_Preferences.Layout.Current;
            ImGui::TextDisabled("Editor joylashuvlari");
            for (int i = 0; i < (int)BuiltInLayout::Count; i++)
            {
                const BuiltInLayout layout = (BuiltInLayout)i;
                const bool selected = current == EditorLayoutLibrary::GetBuiltInKey(layout);
                const std::string label = std::format("{}  {}", BuiltInIcon(layout), EditorLayoutLibrary::GetBuiltInName(layout));
                if (ImGui::MenuItem(label.c_str(), nullptr, selected))
                    RequestBuiltInLayout(layout);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(BuiltInDescription(layout));
            }
            ImGui::Separator();
            if (m_Layouts.GetNames().empty())
                ImGui::TextDisabled("Saqlangan joylashuv yo'q");
            else
            {
                ImGui::TextDisabled("Saqlanganlar");
                for (const std::string& name : m_Layouts.GetNames())
                    if (ImGui::MenuItem(std::format(ICON_FA_WINDOW_RESTORE "  {}##layout", name).c_str(), nullptr, current == name))
                        RequestLayout(name);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Joriy joylashuvni saqlash..."))
            {
                m_LayoutName = m_Layouts.MakeUniqueName("Mening joylashuvim");
                m_LayoutError.clear();
                m_OpenSaveLayout = true;
            }
            if (ImGui::MenuItem(ICON_FA_LIST "  Joylashuvlarni boshqarish...", nullptr, m_ShowLayouts))
            {
                m_ShowLayouts = !m_ShowLayouts;
                m_Layouts.Refresh();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_ARROW_ROTATE_LEFT "  Joylashuvni tiklash (standart)"))
            RequestBuiltInLayout(BuiltInLayout::Default);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Oynalar standart joylariga qaytadi: ierarxiya chapda, inspektor o'ngda, kontent va konsol pastda; "
                        "boshqa oynalar yopiladi");
    }

    void EditorLayer::UI_LayoutWindows()
    {
        const float em = ImGui::GetFontSize();
        // ---- the save prompt
        if (m_OpenSaveLayout)
        {
            ImGui::OpenPopup("Joylashuvni saqlash###SaveLayout");
            m_OpenSaveLayout = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(em * 26.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Joylashuvni saqlash###SaveLayout", nullptr, ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextUnformatted(ICON_FA_WINDOW_RESTORE "  Oynalarning hozirgi joylashuvi va ochiqlari saqlanadi");
            ImGui::Spacing();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool enter = ImGui::InputTextWithHint("##name", "Joylashuv nomi", &m_LayoutName, ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string name = EditorLayoutLibrary::CleanName(m_LayoutName);
            std::string reason;
            const bool valid = EditorLayoutLibrary::IsValidName(name, &reason);
            if (!valid && !name.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", reason.c_str());
            else if (valid && m_Layouts.Contains(name))
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Shu nomli joylashuv bor: ustidan yoziladi");
            if (!m_LayoutError.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), "%s", m_LayoutError.c_str());
            ImGui::Spacing();
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Saqlash", ImVec2(em * 8.0f, 0.0f)) || (enter && valid))
                if (SaveCurrentLayout(name))
                    ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(em * 8.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- the Joylashuvlar window
        if (!m_ShowLayouts)
            return;
        ImGui::SetNextWindowSize(ImVec2(em * 42.0f, em * 28.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(ICON_FA_WINDOW_RESTORE "  Joylashuvlar###Layouts", &m_ShowLayouts, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }
        const std::string& current = m_Preferences.Layout.Current;
        ImGui::SeparatorText("Editor joylashuvlari");
        if (ImGui::BeginTable("##builtins", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("Nomi", ImGuiTableColumnFlags_WidthFixed, em * 8.0f);
            ImGui::TableSetupColumn("Ta'rif", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##apply", ImGuiTableColumnFlags_WidthFixed, em * 6.0f);
            for (int i = 0; i < (int)BuiltInLayout::Count; i++)
            {
                const BuiltInLayout layout = (BuiltInLayout)i;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (current == EditorLayoutLibrary::GetBuiltInKey(layout))
                    ImGui::TextColored(UI::AccentColor(), "%s  %s", BuiltInIcon(layout), EditorLayoutLibrary::GetBuiltInName(layout));
                else
                    ImGui::Text("%s  %s", BuiltInIcon(layout), EditorLayoutLibrary::GetBuiltInName(layout));
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", BuiltInDescription(layout));
                if (ImGui::IsItemHovered())
                    UI::Tooltip(BuiltInDescription(layout));
                ImGui::TableNextColumn();
                if (ImGui::Button(ICON_FA_CHECK "  Qo'llash", ImVec2(-FLT_MIN, 0.0f)))
                    RequestBuiltInLayout(layout);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Saqlanganlar");
        std::optional<std::string> remove;
        const int64_t now = GetUnixTime();
        if (m_Layouts.GetNames().empty())
            ImGui::TextDisabled("Hali saqlangan joylashuv yo'q: oynalarni joylab, \"Joriy joylashuvni saqlash\" ni bosing");
        else if (ImGui::BeginTable("##layouts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                                   ImVec2(0.0f, std::max(em * 6.0f, ImGui::GetContentRegionAvail().y - em * 4.2f))))
        {
            ImGui::TableSetupColumn("Nomi", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Saqlangan", ImGuiTableColumnFlags_WidthFixed, em * 7.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, em * 8.0f);
            for (const std::string& name : m_Layouts.GetNames())
            {
                ImGui::PushID(name.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool renaming = m_LayoutRenameFrom == name;
                if (renaming)
                {
                    if (m_LayoutRenameFocus)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_LayoutRenameFocus = false;
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##rename", &m_LayoutRenameTo, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    {
                        std::string error;
                        const bool wasCurrent = current == name;
                        if (m_Layouts.Rename(name, m_LayoutRenameTo, &error))
                        {
                            if (wasCurrent)
                            {
                                m_Preferences.Layout.Current = EditorLayoutLibrary::CleanName(m_LayoutRenameTo);
                                MarkPreferencesDirty();
                            }
                            ZM_INFO("Joylashuv nomi o'zgardi: {} → {}", name, EditorLayoutLibrary::CleanName(m_LayoutRenameTo));
                            m_LayoutRenameFrom.clear();
                            m_LayoutError.clear();
                        }
                        else
                            m_LayoutError = error;
                        ImGui::PopID();
                        break; // the list changed
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                        m_LayoutRenameFrom.clear();
                }
                else
                {
                    ImGui::AlignTextToFramePadding();
                    if (ImGui::Selectable(std::format(ICON_FA_WINDOW_RESTORE "  {}", name).c_str(), current == name,
                                          ImGuiSelectableFlags_AllowDoubleClick))
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            RequestLayout(name);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Ikki marta bosing: qo'llash");
                }
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (const int64_t saved = m_Layouts.GetSavedAt(name); saved > 0)
                    ImGui::TextDisabled("%s", FormatTimeAgo(saved, now).c_str());
                else
                    ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (UI::ToolbarButton(ICON_FA_CHECK, "Qo'llash"))
                    RequestLayout(name);
                ImGui::SameLine(0.0f, 2.0f);
                if (UI::ToolbarButton(ICON_FA_FLOPPY_DISK, "Hozirgi joylashuv bilan yangilash"))
                    SaveCurrentLayout(name);
                ImGui::SameLine(0.0f, 2.0f);
                if (UI::ToolbarButton(ICON_FA_PEN, "Nomini o'zgartirish"))
                {
                    m_LayoutRenameFrom = name;
                    m_LayoutRenameTo = name;
                    m_LayoutRenameFocus = true;
                    m_LayoutError.clear();
                }
                ImGui::SameLine(0.0f, 2.0f);
                if (UI::ToolbarButton(ICON_FA_TRASH, "O'chirish"))
                    remove = name;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (remove)
        {
            if (m_Layouts.Remove(*remove))
            {
                ZM_INFO("Joylashuv o'chirildi: {}", *remove);
                if (current == *remove)
                {
                    m_Preferences.Layout.Current.clear();
                    MarkPreferencesDirty();
                }
            }
        }
        if (!m_LayoutError.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", m_LayoutError.c_str());

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Joriy joylashuvni saqlash..."))
        {
            m_LayoutName = m_Layouts.MakeUniqueName("Mening joylashuvim");
            m_LayoutError.clear();
            m_OpenSaveLayout = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Papkani ochish"))
        {
            std::error_code ec;
            fs::create_directories(m_Layouts.GetDirectory(), ec);
            Platform::OpenInFileBrowser(m_Layouts.GetDirectory());
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip(m_Layouts.GetDirectory().string().c_str());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Yangilash"))
            m_Layouts.Refresh();
        ImGui::End();
    }

    void EditorLayer::ApplyLayoutAutomation(std::string_view spec)
    {
        while (!spec.empty())
        {
            const size_t comma = spec.find(',');
            const std::string_view item = spec.substr(0, comma);
            spec = comma == std::string_view::npos ? std::string_view() : spec.substr(comma + 1);
            if (item.starts_with("save="))
                m_AutomationLayoutSave = std::string(item.substr(5)); // a few frames later: the layout asked for is built by then
            else if (item == "manage")
            {
                m_ShowLayouts = true;
                m_Layouts.Refresh();
            }
            else if (item == "saveprompt")
            {
                m_LayoutName = m_Layouts.MakeUniqueName("Mening joylashuvim");
                m_OpenSaveLayout = true;
            }
            else if (!item.empty())
            {
                if (!EditorLayoutLibrary::FindBuiltIn(item) && !m_Layouts.Contains(item))
                    ZM_WARN("ZAMIN_EDITOR_LAYOUT: '{}' joylashuvi yo'q", item);
                else
                    RequestLayout(std::string(item));
            }
        }
    }

}
