#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <functional>
#include <string>

namespace Zamin {

    // "Animator grafi" window (Oyna > Animator grafi): an Animator's state machine as a graph -
    // states are nodes (clip / 1D blend tree; the default one marked), transitions arrows, plus the
    // "any state" node. Nodes are dragged (positions are saved with the component), a transition is
    // drawn by Shift-dragging from one node to another (or the node's menu), arrows and nodes are
    // selected to edit them in the side panel (conditions, fade, exit time, blend motions), Delete
    // removes them. While the game runs the current state, its time, the crossfade and the
    // parameters are shown live (and can be changed). Pan: middle / right mouse drag; zoom: wheel.
    class AnimatorGraphPanel
    {
    public:
        // The scene shown: the editor's scene (edits are undoable, OnModified) or the running copy.
        void SetContext(const Ref<Scene>& scene, bool editable);
        // The hierarchy's selection: followed while it has an Animator (unless pinned).
        void SetSelection(Entity selected);
        void Open(Entity entity);
        void OnImGuiRender(bool* show);
        void Focus() { m_FocusRequested = true; }
        // Where the window docks the first time it opens.
        void SetDefaultDock(ImGuiID dock) { m_DefaultDock = dock; }

        // ---- automation (screenshots)
        void SelectState(int index);
        void SelectTransition(int index);
        void RequestFit() { m_FitRequested = true; }

        // An edit of the editor's scene (undo point, unsaved mark).
        std::function<void()> OnModified;

    private:
        enum class Kind { None, State, AnyState, Transition };
        struct Selection
        {
            Kind Type = Kind::None;
            int Index = -1;
        };

        Entity GetEntity() const;
        void MarkModified();
        // Positions for states that have none yet (older scenes): a grid, the any-state node on the left.
        void AutoLayout(AnimatorComponent& c);
        void FitView(const AnimatorComponent& c, const ImVec2& canvasSize);

        void DrawToolbar(AnimatorComponent* c);
        void DrawCanvas(AnimatorComponent& c, const Model* model);
        void DrawSidePanel(AnimatorComponent& c, const Model* model);

        ImVec2 ToScreen(const glm::vec2& p) const;
        glm::vec2 ToGraph(const ImVec2& p) const;
        // The node rectangle of a state (-1: any state) in graph units.
        void NodeRect(const AnimatorComponent& c, int state, glm::vec2& min, glm::vec2& max) const;
        int NodeAt(const AnimatorComponent& c, const glm::vec2& p) const; // -2: none, -1: any state
        int TransitionAt(const AnimatorComponent& c, const ImVec2& screen) const;
        // The screen points an arrow of a transition is drawn between (offset when both ways exist).
        bool TransitionSegment(const AnimatorComponent& c, int index, ImVec2& a, ImVec2& b) const;
        void CreateTransition(AnimatorComponent& c, int from, int to);
        void DeleteSelection(AnimatorComponent& c);

    private:
        Ref<Scene> m_Scene;
        bool m_Editable = false;
        UUID m_EntityID = 0;
        bool m_Pinned = false;
        UUID m_LaidOutFor = 0;

        // view: canvas origin offset (pixels) and zoom
        ImVec2 m_CanvasMin{ 0.0f, 0.0f };
        ImVec2 m_Pan{ 60.0f, 60.0f };
        float m_Zoom = 1.0f;
        bool m_FitRequested = true;

        Selection m_Selection;
        int m_DragNode = -2;                     // a node being moved (-1: any state)
        glm::vec2 m_DragOffset{ 0.0f };
        int m_LinkFrom = -2;                     // drawing a new transition from this node (-1: any state)
        bool m_LinkByClick = false;              // started from the menu: the next click picks the target
        int m_ContextNode = -2;
        int m_ContextTransition = -1;
        glm::vec2 m_ContextPoint{ 0.0f };

        bool m_FocusRequested = false;
        ImGuiID m_DefaultDock = 0;
        float m_SideWidth = 320.0f;
    };

}
