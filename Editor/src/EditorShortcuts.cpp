// The editor's keyboard shortcuts: the actions bound to the shortcut table (ShortcutRegistry), the
// keys pressed each frame dispatched through it, and the shortcuts window (Yordam > Klaviatura
// yorliqlari) that lists, searches and rebinds them.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <cstdlib>
#include <cstring>
#include <format>

namespace Zamin {

    namespace {
        // A key (GLFW codes) as ImGui knows it.
        ImGuiKey ToImGuiKey(Key key)
        {
            const int k = (int)key;
            if (k >= (int)Key::A && k <= (int)Key::Z) return (ImGuiKey)(ImGuiKey_A + (k - (int)Key::A));
            if (k >= (int)Key::D0 && k <= (int)Key::D9) return (ImGuiKey)(ImGuiKey_0 + (k - (int)Key::D0));
            if (k >= (int)Key::F1 && k <= (int)Key::F12) return (ImGuiKey)(ImGuiKey_F1 + (k - (int)Key::F1));
            if (k >= (int)Key::KP0 && k <= (int)Key::KP9) return (ImGuiKey)(ImGuiKey_Keypad0 + (k - (int)Key::KP0));
            switch (key)
            {
                case Key::Space: return ImGuiKey_Space;
                case Key::Apostrophe: return ImGuiKey_Apostrophe;
                case Key::Comma: return ImGuiKey_Comma;
                case Key::Minus: return ImGuiKey_Minus;
                case Key::Period: return ImGuiKey_Period;
                case Key::Slash: return ImGuiKey_Slash;
                case Key::Semicolon: return ImGuiKey_Semicolon;
                case Key::Equal: return ImGuiKey_Equal;
                case Key::LeftBracket: return ImGuiKey_LeftBracket;
                case Key::Backslash: return ImGuiKey_Backslash;
                case Key::RightBracket: return ImGuiKey_RightBracket;
                case Key::GraveAccent: return ImGuiKey_GraveAccent;
                case Key::Escape: return ImGuiKey_Escape;
                case Key::Enter: return ImGuiKey_Enter;
                case Key::Tab: return ImGuiKey_Tab;
                case Key::Backspace: return ImGuiKey_Backspace;
                case Key::Insert: return ImGuiKey_Insert;
                case Key::Delete: return ImGuiKey_Delete;
                case Key::Right: return ImGuiKey_RightArrow;
                case Key::Left: return ImGuiKey_LeftArrow;
                case Key::Down: return ImGuiKey_DownArrow;
                case Key::Up: return ImGuiKey_UpArrow;
                case Key::PageUp: return ImGuiKey_PageUp;
                case Key::PageDown: return ImGuiKey_PageDown;
                case Key::Home: return ImGuiKey_Home;
                case Key::End: return ImGuiKey_End;
                case Key::CapsLock: return ImGuiKey_CapsLock;
                case Key::ScrollLock: return ImGuiKey_ScrollLock;
                case Key::NumLock: return ImGuiKey_NumLock;
                case Key::PrintScreen: return ImGuiKey_PrintScreen;
                case Key::Pause: return ImGuiKey_Pause;
                case Key::KPDecimal: return ImGuiKey_KeypadDecimal;
                case Key::KPDivide: return ImGuiKey_KeypadDivide;
                case Key::KPMultiply: return ImGuiKey_KeypadMultiply;
                case Key::KPSubtract: return ImGuiKey_KeypadSubtract;
                case Key::KPAdd: return ImGuiKey_KeypadAdd;
                case Key::KPEnter: return ImGuiKey_KeypadEnter;
                case Key::KPEqual: return ImGuiKey_KeypadEqual;
                case Key::Menu: return ImGuiKey_Menu;
                default: return ImGuiKey_None;
            }
        }

        // The chord pressed this frame (a key that is not a modifier, with the modifiers held).
        bool PressedChord(KeyChord& out)
        {
            const ImGuiIO& io = ImGui::GetIO();
            for (Key key : GetAllKeys())
            {
                if (KeyChord::IsModifierKey(key))
                    continue;
                const ImGuiKey imguiKey = ToImGuiKey(key);
                if (imguiKey != ImGuiKey_None && ImGui::IsKeyPressed(imguiKey, false))
                {
                    out = KeyChord(key, io.KeyCtrl, io.KeyShift, io.KeyAlt);
                    return true;
                }
            }
            return false;
        }

        // Whether the window with the keyboard is `id` (or a child of it).
        bool HasKeyboard(const char* id)
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            ImGuiWindow* window = ImGui::FindWindowByName(id);
            return window && g.NavWindow && g.NavWindow->RootWindow == window;
        }

        // A chord drawn like a key cap.
        void KeyCap(const char* text, bool dim = false)
        {
            const ImVec2 size = ImGui::CalcTextSize(text);
            const ImVec2 pad(6.0f, 2.0f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(at, ImVec2(at.x + size.x + pad.x * 2.0f, at.y + size.y + pad.y * 2.0f),
                              ImGui::GetColorU32(ImGuiCol_FrameBg, dim ? 0.6f : 1.0f), 4.0f);
            dl->AddRect(at, ImVec2(at.x + size.x + pad.x * 2.0f, at.y + size.y + pad.y * 2.0f), ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
            dl->AddText(ImVec2(at.x + pad.x, at.y + pad.y), ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text), text);
            ImGui::Dummy(ImVec2(size.x + pad.x * 2.0f, size.y + pad.y * 2.0f));
        }
    }

    // ------------------------------------------------------------------ the table and its actions
    void EditorLayer::RegisterShortcuts()
    {
        namespace E = EditorShortcut;
        RegisterEditorShortcuts(m_Shortcuts);

        // Ctrl+S / Ctrl+Z go to the Animation window's clip while it has the keyboard (its skeletal
        // mode edits the scene's Animator events: the scene's undo).
        auto save = [this](bool as) {
            if (m_AnimationPanel.IsFocused() && m_AnimationPanel.HasUnsavedChanges())
            {
                m_AnimationPanel.Save(); // the clip only: the window that has the keyboard
                return;
            }
            as ? SaveSceneAs() : SaveScene();
            m_ProjectSettingsPanel.SaveIfDirty();
            if (m_SpriteEditorPanel.HasUnsavedChanges())
                m_SpriteEditorPanel.Save();
        };
        auto clipUndo = [this]() { return m_AnimationPanel.IsFocused() && !m_AnimationPanel.IsSkeletalMode(); };
        m_Shortcuts.Bind(E::NewScene, [this]() { NewScene(); });
        m_Shortcuts.Bind(E::OpenScene, [this]() { OpenScene(); });
        m_Shortcuts.Bind(E::SaveScene, [save]() { save(false); });
        m_Shortcuts.Bind(E::SaveSceneAs, [save]() { save(true); });
        m_Shortcuts.Bind(E::OpenProject, [this]() { OpenProjectDialog(); });
        m_Shortcuts.Bind(E::Undo, [this, clipUndo]() { clipUndo() ? (void)m_AnimationPanel.Undo() : Undo(); });
        m_Shortcuts.Bind(E::Redo, [this, clipUndo]() { clipUndo() ? (void)m_AnimationPanel.Redo() : Redo(); });
        m_Shortcuts.Bind(E::RedoAlt, [this, clipUndo]() { clipUndo() ? (void)m_AnimationPanel.Redo() : Redo(); });
        m_Shortcuts.Bind(E::UndoHistory, [this]() {
            m_ShowUndoHistory = !m_ShowUndoHistory;
            m_UndoHistoryScroll = true;
        });
        m_Shortcuts.Bind(E::Copy, [this]() { CopySelectedEntity(); });
        m_Shortcuts.Bind(E::Paste, [this]() { PasteEntity(); });
        m_Shortcuts.Bind(E::Duplicate, [this]() { DuplicateSelectedEntity(); });
        m_Shortcuts.Bind(E::Preferences, [this]() { m_ShowPreferences = !m_ShowPreferences; });
        // Delete needs the keyboard (not only the mouse) on the scene view or the hierarchy.
        m_Shortcuts.Bind(E::Delete, [this]() {
            if (m_ViewportFocused || m_SceneHierarchyPanel.IsHierarchyFocused())
                DeleteSelectedEntity();
        });
        m_Shortcuts.Bind(E::SelectAll, [this]() {
            if (m_SceneHierarchyPanel.IsHierarchyFocused() || (m_ViewportFocused && m_SceneState != SceneState::Play))
                m_SceneHierarchyPanel.SelectAll();
        });
        m_Shortcuts.Bind(E::ToggleActive, [this]() { ToggleSelectionActive(); });
        m_Shortcuts.Bind(E::ToggleHidden, [this]() { if (m_SceneState == SceneState::Edit) ToggleSelectionHidden(); });
        m_Shortcuts.Bind(E::ShowAllHidden, [this]() { if (m_SceneState == SceneState::Edit) ShowAllHidden(); });
        m_Shortcuts.Bind(E::PlayToggle, [this]() { m_SceneState == SceneState::Edit ? OnScenePlay() : OnSceneStop(); });
        m_Shortcuts.Bind(E::PlayStop, [this]() { OnSceneStop(); });
        m_Shortcuts.Bind(E::BuildScripts, [this]() { BuildScripts(true); });
        m_Shortcuts.Bind(E::ToolNone, [this]() { m_GizmoMode = GizmoMode::None; });
        m_Shortcuts.Bind(E::ToolMove, [this]() { m_GizmoMode = GizmoMode::Translate; });
        m_Shortcuts.Bind(E::ToolRotate, [this]() { m_GizmoMode = GizmoMode::Rotate; });
        m_Shortcuts.Bind(E::ToolScale, [this]() { m_GizmoMode = GizmoMode::Scale; });
        m_Shortcuts.Bind(E::FrameSelection, [this]() { FrameSelection(); });
        m_Shortcuts.Bind(E::FrameAll, [this]() { FrameAll(); });
        for (int slot = 1; slot <= E::BookmarkSlots; slot++)
        {
            m_Shortcuts.Bind(E::BookmarkSave(slot), [this, slot]() { SaveCameraBookmark(slot); });
            m_Shortcuts.Bind(E::BookmarkRecall(slot), [this, slot]() { RecallCameraBookmark(slot); });
        }
        using Tool = SceneHierarchyPanel::TileTool;
        m_Shortcuts.Bind(E::TileBrush, [this]() { m_SceneHierarchyPanel.SetTileTool(Tool::Brush); });
        m_Shortcuts.Bind(E::TileRectangle, [this]() { m_SceneHierarchyPanel.SetTileTool(Tool::Rectangle); });
        m_Shortcuts.Bind(E::TileFill, [this]() { m_SceneHierarchyPanel.SetTileTool(Tool::Fill); });
        m_Shortcuts.Bind(E::TileEraser, [this]() { m_SceneHierarchyPanel.SetTileTool(Tool::Eraser); });
        m_Shortcuts.Bind(E::TilePicker, [this]() { m_SceneHierarchyPanel.SetTileTool(Tool::Picker); });
        m_Shortcuts.Bind(E::TileExit, [this]() { m_SceneHierarchyPanel.SetTilePaint(false); });
        m_Shortcuts.Bind(E::TileExitNone, [this]() {
            m_SceneHierarchyPanel.SetTilePaint(false);
            m_GizmoMode = GizmoMode::None;
        });
        m_Shortcuts.Bind(E::TileExitMove, [this]() {
            m_SceneHierarchyPanel.SetTilePaint(false);
            m_GizmoMode = GizmoMode::Translate;
        });
        // Collider points / joint anchors: every kind of handle editing ends.
        auto endHandles = [this]() {
            m_SceneHierarchyPanel.SetColliderPointEdit(SceneHierarchyPanel::ColliderPointKind::None);
            m_SceneHierarchyPanel.SetJointAnchorEdit(-1);
            m_SceneHierarchyPanel.SetJoint3DAnchorEdit(-1);
        };
        m_Shortcuts.Bind(E::HandlesExit, endHandles);
        m_Shortcuts.Bind(E::HandlesExitNone, endHandles);
        m_Shortcuts.Bind(E::HandlesExitMove, [this, endHandles]() {
            endHandles();
            m_GizmoMode = GizmoMode::Translate;
        });
        m_Shortcuts.Bind(E::ShortcutsWindow, [this]() { m_ShowShortcuts = !m_ShowShortcuts; });
    }

    void EditorLayer::ApplyShortcutPreferences()
    {
        const size_t applied = m_Shortcuts.ApplyOverrides(m_Preferences.Shortcuts);
        if (applied != m_Preferences.Shortcuts.size())
            ZM_WARN("Klaviatura yorliqlari: {} ta saqlangan bog'lanish tanilmadi", m_Preferences.Shortcuts.size() - applied);
        if (size_t conflicts = m_Shortcuts.CountConflicts(); conflicts > 0)
            ZM_WARN("Klaviatura yorliqlarida {} ta to'qnashuv bor (Yordam > Klaviatura yorliqlari)", conflicts);

        // ZAMIN_EDITOR_SHORTCUT_BIND="tools.move=E;file.save=Ctrl+O": bindings for automation (not saved).
        if (const char* bind = std::getenv("ZAMIN_EDITOR_SHORTCUT_BIND"); bind && *bind)
        {
            std::string_view spec(bind);
            while (!spec.empty())
            {
                const size_t end = spec.find(';');
                const std::string_view item = spec.substr(0, end);
                spec = end == std::string_view::npos ? std::string_view() : spec.substr(end + 1);
                const size_t eq = item.find('=');
                KeyChord chord;
                if (eq != std::string_view::npos && KeyChord::Parse(item.substr(eq + 1), chord) && m_Shortcuts.Rebind(item.substr(0, eq), chord))
                    m_ShortcutsAutomation = true;
            }
        }
        // ZAMIN_EDITOR_SHORTCUTS=<search> (or 1): the window opens (with the search filled in);
        // ZAMIN_EDITOR_SHORTCUT_CAPTURE=<id>: that row waits for its new keys (screenshots).
        if (const char* open = std::getenv("ZAMIN_EDITOR_SHORTCUTS"); open && *open)
        {
            m_ShowShortcuts = true;
            if (std::strcmp(open, "1") != 0)
                m_ShortcutSearch = open;
        }
        if (const char* capture = std::getenv("ZAMIN_EDITOR_SHORTCUT_CAPTURE"); capture && m_Shortcuts.Find(capture))
            m_ShortcutCapture = capture;
    }

    void EditorLayer::SaveShortcutPreferences()
    {
        if (m_ShortcutsAutomation)
            return; // automation's bindings stay out of the user's preferences
        m_Preferences.Shortcuts = m_Shortcuts.GetOverrides();
        MarkPreferencesDirty();
    }

    // ------------------------------------------------------------------ dispatch
    uint32_t EditorLayer::GetShortcutSituations() const
    {
        namespace S = ShortcutSituation;
        uint32_t situations = S::Editor;
        if (m_SceneHierarchyPanel.IsHierarchyFocused())
            situations |= S::Hierarchy;
        // The scene view: the mouse over it or the keyboard in it, not flying the camera (RMB:
        // WASD / QE move it then) and not playing (the game has the keys).
        const bool flying = Input::IsMouseButtonPressed(MouseButton::Right);
        if (!flying && (m_ViewportHovered || m_ViewportFocused) && m_SceneState != SceneState::Play)
        {
            situations |= S::SceneView;
            if (m_SceneState == SceneState::Edit)
            {
                if (m_SceneHierarchyPanel.IsTilePainting())
                    situations |= S::ViewportTile;
                else if (IsViewportHandleEditing())
                    situations |= S::ViewportHandles;
                else
                    situations |= S::ViewportTools;
            }
        }
        if (m_SceneState == SceneState::Play)
            situations |= S::Playing;
        // Windows with keys of their own.
        if (HasKeyboard("###ContentBrowser")) situations |= S::ContentBrowser;
        if (m_AnimationPanel.IsFocused()) situations |= S::Animation;
        if (HasKeyboard("###AnimatorGraph")) situations |= S::AnimatorGraph;
        if (HasKeyboard("###SpriteEditor")) situations |= S::SpriteEditor;
        if (m_ConsolePanel.IsFocused()) situations |= S::Console;
        return situations;
    }

    void EditorLayer::HandleShortcuts()
    {
        // Not while typing (into the editor, or into the game's input field), nor while the
        // shortcuts window waits for keys.
        const ImGuiIO& io = ImGui::GetIO();
        const bool typingInGame = m_SceneState == SceneState::Play && m_ViewportFocused && m_ActiveScene && m_ActiveScene->IsTextInputFocused();
        if (io.WantTextInput || typingInGame || !m_ShortcutCapture.empty() || m_ShortcutSearchByKey)
            return;
        uint32_t situations = 0;
        for (Key key : GetAllKeys())
        {
            if (KeyChord::IsModifierKey(key))
                continue;
            const ImGuiKey imguiKey = ToImGuiKey(key);
            if (imguiKey == ImGuiKey_None || !ImGui::IsKeyPressed(imguiKey, false))
                continue;
            if (situations == 0)
                situations = GetShortcutSituations();
            const KeyChord chord(key, io.KeyCtrl, io.KeyShift, io.KeyAlt);
            if (const ShortcutInfo* shortcut = m_Shortcuts.Match(chord, situations); shortcut && shortcut->Action)
                shortcut->Action();
        }
    }

    // ------------------------------------------------------------------ the window
    void EditorLayer::UI_ShortcutsWindow()
    {
        if (!m_ShowShortcuts)
        {
            m_ShortcutCapture.clear();
            m_ShortcutSearchByKey = false;
            return;
        }
        const float em = ImGui::GetFontSize();
        ImGui::SetNextWindowSize(ImVec2(em * 50.0f, em * 36.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(ICON_FA_KEYBOARD "  Klaviatura yorliqlari###Shortcuts", &m_ShowShortcuts, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        // ---- keys pressed for a rebinding / the key filter
        if (!m_ShortcutCapture.empty() || m_ShortcutSearchByKey)
        {
            KeyChord chord;
            if (PressedChord(chord))
            {
                if (chord == KeyChord(Key::Escape))
                {
                    m_ShortcutCapture.clear(); // Esc alone cancels
                    m_ShortcutSearchByKey = false;
                }
                else if (m_ShortcutSearchByKey)
                {
                    m_ShortcutKeyFilter = chord;
                    m_ShortcutSearchByKey = false;
                }
                else
                {
                    const ShortcutInfo* info = m_Shortcuts.Find(m_ShortcutCapture);
                    if (info && m_Shortcuts.Rebind(m_ShortcutCapture, chord))
                    {
                        std::vector<const ShortcutInfo*> conflicts = m_Shortcuts.FindConflicts(m_ShortcutCapture);
                        if (conflicts.empty())
                            ZM_INFO("Yorliq: {} - {}", info->Name, chord.GetDisplayName());
                        else
                            ZM_WARN("Yorliq: {} - {} ({} bilan to'qnashadi)", info->Name, chord.GetDisplayName(), conflicts.front()->Name);
                        SaveShortcutPreferences();
                    }
                    m_ShortcutCapture.clear();
                }
            }
        }

        // ---- search, filters
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* keyLabel = ICON_FA_KEYBOARD "  Tugma bo'yicha";
        const float keyWidth = ImGui::CalcTextSize(keyLabel).x + style.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - keyWidth - style.ItemSpacing.x);
        UI::SearchBox("##shortcutSearch", m_ShortcutSearch, "Qidirish: nomi, tugma, joyi...");
        ImGui::SameLine();
        if (m_ShortcutSearchByKey)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor());
        if (ImGui::Button(m_ShortcutSearchByKey ? ICON_FA_KEYBOARD "  Bosing...##bykey" : keyLabel, ImVec2(keyWidth, 0.0f)))
        {
            m_ShortcutSearchByKey = !m_ShortcutSearchByKey;
            m_ShortcutCapture.clear();
        }
        if (m_ShortcutSearchByKey)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Tugmalarni bosing: shu tugmalardagi yorliqlar qoladi (Esc - bekor)");

        const size_t modified = m_Shortcuts.CountModified(), conflicts = m_Shortcuts.CountConflicts();
        if (!m_ShortcutKeyFilter.IsEmpty())
        {
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor());
            if (ImGui::SmallButton(std::format(ICON_FA_KEYBOARD "  {}  " ICON_FA_XMARK "##keyFilter", m_ShortcutKeyFilter.GetDisplayName()).c_str()))
                m_ShortcutKeyFilter = {};
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                UI::Tooltip("Tugma filtri (bosib olib tashlash)");
            ImGui::SameLine();
        }
        ImGui::Checkbox(std::format("O'zgartirilganlar ({})", modified).c_str(), &m_ShortcutsModifiedOnly);
        ImGui::SameLine();
        if (conflicts > 0)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.35f, 1.0f));
        ImGui::Checkbox(std::format("To'qnashuvlar ({})", conflicts).c_str(), &m_ShortcutsConflictsOnly);
        if (conflicts > 0)
            ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("|  %zu ta yorliq", m_Shortcuts.GetAll().size());
        ImGui::TextDisabled("Yorliqni o'zgartirish uchun tugmasini bosing, keyin yangi tugmalarni bosing (Esc - bekor qilish).");

        // ---- the table, by category
        const float footer = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y + 4.0f;
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        std::string clickedId;
        bool captureHovered = false;
        if (ImGui::BeginTable("##shortcuts", 4, flags, ImVec2(0.0f, -footer)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Amal", ImGuiTableColumnFlags_WidthStretch, 0.46f);
            ImGui::TableSetupColumn("Yorliq", ImGuiTableColumnFlags_WidthStretch, 0.26f);
            ImGui::TableSetupColumn("Qayerda", ImGuiTableColumnFlags_WidthStretch, 0.2f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 3.0f + style.ItemSpacing.x * 2.0f);
            ImGui::TableHeadersRow();
            size_t shown = 0;
            for (const std::string& category : m_Shortcuts.GetCategories())
            {
                bool header = false;
                for (const ShortcutInfo& s : m_Shortcuts.GetAll())
                {
                    if (s.Category != category || !ShortcutRegistry::MatchesSearch(s, m_ShortcutSearch))
                        continue;
                    if (!m_ShortcutKeyFilter.IsEmpty() && s.Chord != m_ShortcutKeyFilter)
                        continue;
                    if (m_ShortcutsModifiedOnly && !s.IsModified())
                        continue;
                    const std::vector<const ShortcutInfo*> clashes = m_Shortcuts.FindConflicts(s.Id);
                    if (m_ShortcutsConflictsOnly && clashes.empty())
                        continue;
                    if (!header)
                    {
                        header = true;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::PushFont(ImGuiLayer::GetBoldFont());
                        ImGui::TextColored(UI::AccentColor(), "%s", category.c_str());
                        ImGui::PopFont();
                    }
                    shown++;
                    ImGui::PushID(s.Id.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Indent(em * 0.6f);
                    if (s.Fixed)
                        ImGui::TextDisabled("%s", s.Name.c_str());
                    else
                        ImGui::TextUnformatted(s.Name.c_str());
                    ImGui::Unindent(em * 0.6f);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(s.Fixed ? std::format("{}\nPanel o'zi boshqaradi - o'zgartirib bo'lmaydi", s.Id).c_str() : s.Id.c_str());

                    // The chord: a button that waits for the new keys.
                    ImGui::TableSetColumnIndex(1);
                    if (s.Fixed)
                    {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
                        KeyCap(s.Display.c_str(), true);
                    }
                    else if (m_ShortcutCapture == s.Id)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor());
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::AccentMidColor());
                        if (ImGui::Button(ICON_FA_KEYBOARD "  Tugmalarni bosing...", ImVec2(-FLT_MIN, 0.0f)))
                            clickedId = s.Id;
                        captureHovered = ImGui::IsItemHovered();
                        ImGui::PopStyleColor(2);
                        if (ImGui::IsItemHovered())
                            UI::Tooltip("Yangi tugmalarni bosing (Esc - bekor qilish)");
                    }
                    else
                    {
                        const std::string label = (s.Display.empty() ? std::string("\xe2\x80\x94") : s.Display) + "##chord";
                        if (s.IsModified())
                            ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
                        if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f)))
                            clickedId = s.Id;
                        if (s.IsModified())
                            ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            UI::Tooltip(s.IsModified() ? std::format("Standart: {} (bosing - o'zgartirish)", s.Default.GetDisplayName()).c_str()
                                                       : "Bosing - o'zgartirish");
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", GetScopeName(s.Scope));

                    ImGui::TableSetColumnIndex(3);
                    if (!clashes.empty())
                    {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION);
                        if (ImGui::IsItemHovered())
                        {
                            std::string text = "Shu tugmalar boshqa yorliqda ham:";
                            for (const ShortcutInfo* other : clashes)
                                text += std::format("\n  {} ({}, {})", other->Name, other->Category, GetScopeName(other->Scope));
                            UI::Tooltip(text.c_str());
                        }
                        ImGui::SameLine();
                    }
                    if (!s.Fixed && s.IsModified())
                    {
                        if (ImGui::SmallButton(ICON_FA_ARROW_ROTATE_LEFT))
                        {
                            m_Shortcuts.ResetToDefault(s.Id);
                            SaveShortcutPreferences();
                        }
                        if (ImGui::IsItemHovered())
                            UI::Tooltip(std::format("Standartga qaytarish: {}", s.Default.GetDisplayName()).c_str());
                        ImGui::SameLine();
                    }
                    // Unbinding: offered on the row under the mouse (last frame's) only.
                    if (!s.Fixed && !s.Chord.IsEmpty() && ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex())
                    {
                        if (ImGui::SmallButton(ICON_FA_XMARK))
                        {
                            m_Shortcuts.Rebind(s.Id, {});
                            SaveShortcutPreferences();
                        }
                        if (ImGui::IsItemHovered())
                            UI::Tooltip("Tugmasiz qoldirish");
                    }
                    ImGui::PopID();
                }
            }
            if (shown == 0)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Hech narsa topilmadi");
            }
            ImGui::EndTable();
        }
        if (!clickedId.empty())
        {
            m_ShortcutCapture = m_ShortcutCapture == clickedId ? std::string() : clickedId;
            m_ShortcutSearchByKey = false;
        }
        else if (!m_ShortcutCapture.empty() && !captureHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_ShortcutCapture.clear(); // a click elsewhere cancels

        // ---- bottom
        ImGui::Separator();
        ImGui::BeginDisabled(modified == 0);
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "  Hammasini standartga qaytarish"))
        {
            m_Shortcuts.ResetAll();
            SaveShortcutPreferences();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(ICON_FA_FLOPPY_DISK "  O'zgarishlar preferences.json da saqlanadi%s", m_ShortcutsAutomation ? " (avtomatlashtirish: saqlanmaydi)" : "");
        ImGui::End();
    }

}
