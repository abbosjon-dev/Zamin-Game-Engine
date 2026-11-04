#pragma once

#include <Zamin/Zamin.h>

#include <functional>
#include <string>
#include <vector>

namespace Zamin {

    // "Project settings" window: general project info, the input action map (actions and
    // axes bound to keys / mouse buttons / gamepad, with a live preview) and the game's
    // SaveData (PlayerPrefs) viewer / editor.
    class ProjectSettingsPanel
    {
    public:
        enum class Tab { Input = 0, SaveData, Tags, General };

        void OnImGuiRender(bool* show);

        bool IsDirty() const { return m_Dirty; }
        // Something else (e.g. the inspector's tag field) changed the project config.
        void MarkDirty() { m_Dirty = true; }
        // Scene used for the tag usage counts (the editor's active scene).
        void SetScene(const Ref<Scene>& scene) { m_Scene = scene; }
        // false in Play / Simulate: the scene above is then the saved edit scene, which must
        // not change behind the editor's back (no dirty flag / undo), so tags its entities
        // use cannot be renamed until play stops.
        void SetSceneEditable(bool editable) { m_SceneEditable = editable; }
        // Renaming a tag also renames it on the scene's entities.
        std::function<void()> OnSceneModified;
        // Writes the active .zproject if there are unsaved changes.
        void SaveIfDirty();
        // Call when another project is opened / the project is closed.
        void OnProjectChanged();
        // Bring the window to the front on the next frame (optionally on a specific tab).
        void Focus() { m_FocusRequested = true; }
        void Focus(Tab tab) { m_FocusRequested = true; m_RequestedTab = (int)tab; }

    private:
        enum class ListKind { None = 0, Action, AxisNegative, AxisPositive };
        struct ListenTarget
        {
            ListKind Kind = ListKind::None;
            int Entry = -1;   // index of the action / axis
            int Binding = -1; // index inside its binding list
            bool operator==(const ListenTarget&) const = default;
        };

        void DrawGeneral(ProjectConfig& config);
        void DrawSaveData();
        void DrawTags(ProjectConfig& config);
        void DrawInputMap(InputActionMap& map);
        bool DrawBindingList(const char* label, std::vector<InputBinding>& bindings, ListKind kind, int entry);
        bool DrawBinding(InputBinding& binding, const ListenTarget& target);

        void StartListening(const ListenTarget& target);
        void StopListening() { m_Listen = {}; }
        void UpdateListening(InputActionMap& map);
        std::vector<InputBinding>* ResolveList(InputActionMap& map, ListKind kind, int entry);

    private:
        bool m_Dirty = false;
        bool m_FocusRequested = false;
        ListenTarget m_Listen;
        bool m_ListenArmed = false; // all inputs were released since listening started
        int m_RequestedTab = -1;

        Ref<Scene> m_Scene;
        bool m_SceneEditable = true;
        std::string m_NewTag;
        int m_RenamingTag = -1;
        std::string m_TagRenameBuffer;
        bool m_FocusTagRename = false;

        // SaveData tab: new entry
        std::string m_NewSaveKey;
        int m_NewSaveType = 0;
    };

}
