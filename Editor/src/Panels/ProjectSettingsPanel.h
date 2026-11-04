#pragma once

#include <Zamin/Zamin.h>

#include <vector>

namespace Zamin {

    // "Project settings" window: general project info + the input action map
    // (actions and axes bound to keys / mouse buttons / gamepad).
    class ProjectSettingsPanel
    {
    public:
        void OnImGuiRender(bool* show);

        bool IsDirty() const { return m_Dirty; }
        // Writes the active .zproject if there are unsaved changes.
        void SaveIfDirty();
        // Call when another project is opened / the project is closed.
        void OnProjectChanged();
        // Bring the window to the front on the next frame.
        void Focus() { m_FocusRequested = true; }

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
    };

}
