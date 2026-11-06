#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Zamin {

    // "Animatsiya" window (Oyna > Animatsiya): edits the property clips (.zanim) of an Animation
    // Player - its tracks (a property of the player's entity or of a child, by name path), their
    // keys on a dope sheet or as curves (step / linear / Bezier with tangent handles), the clip's
    // events, length, frame rate and wrap mode. Scrubbing / playing previews the clip in the scene
    // view without play mode; the scene's own values come back when the preview ends (and are
    // what snapshots and saved scenes hold). Record mode: changes made in the inspector or with
    // the gizmo to the previewed entities become keys at the current time. Edits go to the shared
    // clip at once (players see them, also while the game runs) and are written with Save
    // (Ctrl+S, or when the scene is saved). Undo / redo of its own (Ctrl+Z / Ctrl+Y while focused).
    class AnimationTimelinePanel
    {
    public:
        // The scene shown: the editor's scene in edit mode (editable = previews and records), the
        // running copy while playing (the player's own time is shown; scrubbing moves it).
        void SetContext(const Ref<Scene>& scene, bool editable);
        // The hierarchy's selection: the window follows the Animation Player of the selected entity
        // or of its nearest ancestor that has one (unless pinned). The selected entity is the one
        // record mode watches for new properties.
        void SetSelection(Entity selected);
        // Shows an entity's player, clip `clip` of its list.
        void Open(Entity player, int clip);
        // A .zanim file from the content browser: shown on a player of the scene that has it, else on its own.
        void OpenClipFile(const std::filesystem::path& file);

        // Before the scene is drawn (edit mode): preview playback, recording and the preview pose.
        void OnUpdate(Timestep ts, bool visible);
        void OnImGuiRender(bool* show);

        bool IsFocused() const { return m_Focused; }
        bool Undo();
        bool Redo();
        // The scene's own values back (play / simulate starts, another scene opens).
        void StopPreview(bool restore = true);
        // Runs `f` with the scene's own values in place (snapshots, saving), the preview after it.
        void WithoutPreview(const std::function<void()>& f);
        bool IsPreviewing() const { return m_Previewing; }
        bool HasUnsavedChanges() const { return m_Dirty; }
        bool Save();
        void SaveIfDirty() { if (m_Dirty) Save(); }
        void Focus() { m_FocusRequested = true; }
        // Where the window docks the first time it opens (the content browser's dock node).
        void SetDefaultDock(ImGuiID dock) { m_DefaultDock = dock; }

        // ---- automation (screenshots, tests)
        void SetTime(float seconds);
        void SetRecording(bool recording) { m_Recording = recording; }
        void SetCurveView(bool curves) { m_CurveView = curves; }
        void SelectTrack(int index);
        void SelectKey(int track, int key);

        // A clip file was written (the content browser refreshes it).
        std::function<void(const std::filesystem::path&)> OnClipSaved;

    private:
        struct KeyRef
        {
            int Track = -1;
            int Key = -1;
            bool operator==(const KeyRef&) const = default;
        };
        enum class DragMode { None, Scrub, Keys, Event, CurveKey, TangentIn, TangentOut, Pan };

        Entity GetPlayer() const;
        AnimationPlayerComponent* GetPlayerComponent() const;
        void SyncTarget();
        void LoadClip(int index);
        void LoadAsset(const Ref<PropertyClipAsset>& asset, int index);
        void ClearClip();

        // ---- edits: BeginEdit (undo point, once per gesture) -> change m_Work -> Commit -> EndEdit
        void BeginEdit();
        void Commit();
        void EndEdit() { m_EditOpen = false; }
        float Snap(float time) const;
        float FrameDuration() const { return 1.0f / std::max(1.0f, m_Work.FrameRate); }

        void AddTrack(const std::string& path, AnimatedProperty property, const std::string& field);
        void RemoveTrack(int index);
        void AddKeys(int track, float time); // track < 0: every track
        void DeleteSelection();
        void AddEvent(float time);
        // Keys back in time order after a time change; the selection follows its keys.
        void SortTrackKeepSelection(int track);
        void SortEventsKeepSelection();
        bool IsKeySelected(int track, int key) const;
        glm::vec4 CurrentValue(const AnimationTrack& track) const;
        std::string TrackLabel(const AnimationTrack& track) const;

        // ---- preview
        void ApplyPreview();
        void DetectEdits();
        void RefreshWatch();
        void CaptureBase(Entity target, AnimatedProperty property, const std::string& field);

        // ---- drawing
        void DrawToolbar();
        void DrawEmptyState();
        void DrawTimeline();
        void DrawDetails();
        void DrawClipSettings();
        void DrawAddTrackPopup();
        void HandleShortcuts();

    private:
        Ref<Scene> m_Scene;
        bool m_Editable = false;
        UUID m_PlayerID = 0;
        UUID m_SelectedID = 0;
        bool m_Pinned = false;

        int m_ClipIndex = -1;                    // in the player's list (-1: a clip file on its own)
        std::string m_ClipPath;                  // as the player lists it (or the file's asset path)
        Ref<PropertyClipAsset> m_Asset;
        PropertyClip m_Work;                     // the clip being edited (written to m_Asset on every change)
        uint64_t m_AssetRevision = 0;
        bool m_Dirty = false;
        std::vector<PropertyClip> m_UndoStack, m_RedoStack;
        bool m_EditOpen = false;

        float m_Time = 0.0f;
        bool m_Playing = false;
        ClipCursor m_PlayCursor;
        bool m_Recording = false;
        bool m_RecordEditing = false;
        bool m_Snap = true;
        bool m_CurveView = false;

        // view: seconds at the left edge and the zoom; the curve view's value range
        float m_ViewStart = -0.1f;
        float m_PixelsPerSecond = 200.0f;
        float m_ScrollY = 0.0f;                  // the track rows, when they do not fit
        bool m_FitRequested = true;
        float m_ValueMin = -1.0f, m_ValueMax = 1.0f;

        int m_SelectedTrack = -1;
        std::vector<KeyRef> m_SelectedKeys;
        int m_SelectedEvent = -1;
        KeyRef m_ContextKey;
        int m_ContextEvent = -1;
        float m_ContextTime = 0.0f;
        int m_ContextTrack = -1;

        DragMode m_Drag = DragMode::None;
        bool m_DragMoved = false;
        float m_DragStartTime = 0.0f;
        std::vector<float> m_DragKeyTimes;       // the selected keys' times when the drag started
        float m_DragEventTime = 0.0f;
        int m_DragComponent = 0;

        // preview: the scene's own values of what it wrote, and what it wrote last
        struct PropertyValue
        {
            UUID Entity = 0;
            AnimatedProperty Property = AnimatedProperty::Position;
            std::string Field;
            glm::vec4 Value{ 0.0f };
            int Track = -1;
        };
        std::vector<PropertyValue> m_Base;
        std::vector<PropertyValue> m_Applied;
        std::vector<PropertyValue> m_Watch;      // record: the selected entity's other properties as they were
        bool m_Previewing = false;
        Scene* m_PreviewScene = nullptr;

        bool m_Focused = false;
        bool m_FocusRequested = false;
        ImGuiID m_DefaultDock = 0;
        bool m_OpenAddTrack = false;
        std::string m_Status;
        double m_StatusUntil = 0.0;
    };

}
