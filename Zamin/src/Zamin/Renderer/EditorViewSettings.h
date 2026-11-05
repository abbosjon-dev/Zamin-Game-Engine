#pragma once

#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Project/AssetFiles.h"

#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace Zamin {

    // Snapping steps of the editor gizmo (viewport toolbar: magnet button + step popup).
    struct SnapSettings
    {
        bool Enabled = false;     // snap without holding Ctrl (holding Ctrl inverts it)
        float Translate = 0.5f;   // world units
        float Rotate = 15.0f;     // degrees
        float Scale = 0.1f;       // step of the scale factor

        static constexpr float MinStep = 0.001f;

        // Whether the gizmo snaps right now: the toolbar toggle, inverted while Ctrl is held.
        bool IsActive(bool ctrlHeld) const { return Enabled != ctrlHeld; }
        // Step for a gizmo operation: 0 translate, 1 rotate, 2 scale.
        float GetStep(int operation) const;
        // Steps below MinStep (or not finite) fall back to MinStep / the defaults.
        void Sanitize();
    };

    // The editor viewport grid (XZ plane in 3D, XY plane in 2D mode).
    struct GridSettings
    {
        bool Visible = true;
        float CellSize = 1.0f;     // world units between two minor lines
        int MajorEvery = 10;       // every Nth line is a major line (1 = no major lines)
        glm::vec4 Color{ 0.45f, 0.47f, 0.52f, 0.6f };        // minor lines (alpha = opacity)
        glm::vec4 MajorColor{ 0.58f, 0.60f, 0.66f, 0.85f };  // major lines
        bool ShowAxes = true;      // the world axes in red (X) / blue (Z) or green (Y in 2D)
        float FadeDistance = 1.0f; // how far the grid reaches before it fades out (x the default)

        static constexpr float MinCellSize = 0.001f;
        static constexpr int MaxMajorEvery = 100;

        void Sanitize();
    };

    // For which entities an editor viewport visualisation is drawn.
    enum class GizmoVisibility { Off = 0, Selected = 1, All = 2 };

    // The editor viewport's visualisations of components (never in the player): camera frustums,
    // the direction of directional / spot lights, the radii of 3D audio sources.
    struct EditorGizmoSettings
    {
        GizmoVisibility CameraFrustums = GizmoVisibility::Selected;
        GizmoVisibility LightDirections = GizmoVisibility::Selected;
        GizmoVisibility AudioRanges = GizmoVisibility::Selected;

        // Whether an entity (selected or not) gets the visualisation.
        static bool Shows(GizmoVisibility visibility, bool selected)
        {
            return visibility == GizmoVisibility::All || (visibility == GizmoVisibility::Selected && selected);
        }
        static const char* GetKey(GizmoVisibility visibility);           // "Off" / "Selected" / "All"
        static GizmoVisibility FromKey(const std::string& key, GizmoVisibility fallback);
    };

    // Where the editor camera looks from (EditorCamera::GetViewState / SetViewState).
    struct EditorCameraState
    {
        glm::vec3 FocalPoint{ 0.0f };
        float Distance = 12.0f;
        float Pitch = 0.35f;
        float Yaw = -0.6f;
        bool Orthographic = false; // 2D mode
    };

    // Saved editor camera views of one scene: Ctrl+1..5 stores, 1..5 flies back.
    // Slots are numbered 1..SlotCount; anything else is ignored.
    class CameraBookmarks
    {
    public:
        static constexpr int SlotCount = 5;

        void Set(int slot, const EditorCameraState& state);
        void Clear(int slot);
        void ClearAll() { m_Slots = {}; }
        bool Has(int slot) const { return Get(slot) != nullptr; }
        const EditorCameraState* Get(int slot) const;
        bool IsEmpty() const;
        int Count() const;

    private:
        std::array<std::optional<EditorCameraState>, SlotCount> m_Slots;
    };

    // Which inspector headers are open: per component type ("Rigidbody2D"), scripts per class
    // ("NativeScript:PlayerController"). Collapsing a header collapses it on every entity, in
    // every scene and the next session too. Only headers that differ from their default are kept.
    class InspectorFoldState
    {
    public:
        // `defaultOpen`: what a header the user never toggled starts as.
        bool IsOpen(const std::string& key, bool defaultOpen = true) const;
        // Returns true when the stored state changed.
        bool SetOpen(const std::string& key, bool open, bool defaultOpen = true);
        void Clear() { m_Open.clear(); }
        const std::map<std::string, bool>& GetAll() const { return m_Open; }
        void SetAll(std::map<std::string, bool> states) { m_Open = std::move(states); }

        static constexpr size_t MaxEntries = 512; // script classes come and go: the file stays small

    private:
        std::map<std::string, bool> m_Open;
    };

    // How the content browser shows a folder: tiles or a sortable list, the tile size, the
    // file type filter and the list's sort column.
    struct ContentBrowserSettings
    {
        bool ListView = false;
        float ThumbnailSize = 84.0f;
        AssetTypeFilter Filter;
        AssetSortColumn SortBy = AssetSortColumn::Name;
        bool SortAscending = true;
        // The search looks through the whole project (false: the open folder and its sub folders).
        bool SearchEverywhere = true;

        static constexpr float MinThumbnailSize = 48.0f;
        static constexpr float MaxThumbnailSize = 160.0f;

        void Sanitize();
    };

    // Per-user viewport preferences of the editor: snapping, grid, debug drawing, gizmos, the camera
    // bookmarks of every scene (keyed by the scene file path), the inspector's open / collapsed
    // component headers and the content browser's view. Stored as JSON in the editor config directory.
    struct EditorViewSettings
    {
        SnapSettings Snap;
        GridSettings Grid;
        // What the viewport shows of DebugDraw (scripts' shapes, physics raycasts / contacts).
        DebugDrawSettings DebugDraw;
        EditorGizmoSettings Gizmos;
        std::map<std::string, CameraBookmarks> Bookmarks;
        InspectorFoldState Inspector;
        ContentBrowserSettings ContentBrowser;

        // Bookmarks of a scene file ("" = a scene that was never saved).
        static std::string SceneKey(const std::filesystem::path& scenePath);
        CameraBookmarks& GetBookmarks(const std::filesystem::path& scenePath) { return Bookmarks[SceneKey(scenePath)]; }
        // A scene file (or a folder of them) moved: its bookmarks follow it. Returns how many moved.
        size_t MoveSceneBookmarks(const std::filesystem::path& from, const std::filesystem::path& to);

        std::string ToJson() const;
        // Fields that are missing keep their current value. Returns false (nothing changes)
        // when the text is not an editor-settings JSON object.
        bool FromJson(const std::string& text);
        bool Save(const std::filesystem::path& path) const;
        bool Load(const std::filesystem::path& path);
    };

}
