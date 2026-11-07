#pragma once

#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Editor/GameViewResolution.h"
#include "Zamin/Editor/SceneViews.h"
#include "Zamin/Project/AssetFiles.h"

#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>

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

    // The editor's transform gizmo (move / rotate / scale handles) and the viewport's icons
    // (cameras, lights, audio sources): how big they are on screen, as factors of their defaults.
    struct TransformGizmoSettings
    {
        float Size = 1.0f;          // x the default size (ImGuizmo: 0.1 of the viewport in clip space)
        float LineThickness = 1.0f; // x the default thickness of the handles' lines, arrows and circles
        float IconSize = 1.0f;      // x the default size of the viewport icons (14 px radius)

        static constexpr float DefaultClipSpaceSize = 0.1f;
        static constexpr float DefaultIconRadius = 14.0f;
        static constexpr float MinSize = 0.25f, MaxSize = 3.0f;
        static constexpr float MinThickness = 0.5f, MaxThickness = 3.0f;
        static constexpr float MinIconSize = 0.5f, MaxIconSize = 2.5f;

        // What ImGuizmo::SetGizmoSizeClipSpace takes.
        float GetClipSpaceSize() const { return DefaultClipSpaceSize * Size; }
        float GetIconRadius() const { return DefaultIconRadius * IconSize; }
        // Factors brought into their ranges (anything not finite to 1).
        void Sanitize();
    };

    // How the editor viewport marks the selected entities: Outline - a smooth contour around what
    // they draw (shader based), Box - the wireframe bounds of meshes and rectangles of 2D shapes.
    enum class SelectionStyle { Outline = 0, Box };

    // The selection outline: a contour `Width` pixels wide around the pixels the selected entities
    // draw (found in the viewport's entity ID picture), anti-aliased on its outer edge. Where a
    // selected mesh is hidden behind something else its silhouette's edge is drawn faintly. The
    // primary selection gets `Color`, the rest of a multi-selection a lighter version of it and the
    // selection's children a fainter one; the entity under the mouse (that a click would select) a
    // thinner `HoverColor` contour. Editor only.
    struct SelectionOutlineSettings
    {
        SelectionStyle Style = SelectionStyle::Outline;
        float Width = 2.0f;                               // pixels
        glm::vec4 Color{ 1.0f, 0.55f, 0.1f, 1.0f };
        bool ShowOccluded = true;                         // the hidden parts of selected meshes, faint
        float OccludedOpacity = 0.35f;                    // x the outline's opacity there
        bool Children = true;                             // the selection's descendants are outlined too
        bool Hover = true;
        float HoverWidth = 1.5f;                          // pixels
        glm::vec4 HoverColor{ 0.62f, 0.84f, 1.0f, 0.7f };

        static constexpr float MinWidth = 0.5f, MaxWidth = 6.0f;

        // The rest of a multi-selection: lighter. The selection's children: fainter.
        glm::vec4 GetSecondaryColor() const;
        glm::vec4 GetChildColor() const;
        // Widths and opacities brought into range, colours clamped to 0..1.
        void Sanitize();
    };

    // For which entities an editor viewport visualisation is drawn.
    enum class GizmoVisibility { Off = 0, Selected = 1, All = 2 };

    // Which bounding boxes are drawn: Local - the box around what an entity draws in its own space,
    // turned and scaled with it; World - the axis-aligned box around that in the world (what the
    // view's culling and the camera framing use); Both.
    enum class BoundsSpace { Local = 0, World, Both };

    // The editor viewport's visualisations of components (never in the player): camera frustums,
    // the direction of directional / spot lights, the radii of 3D audio sources, bounding boxes,
    // the skeletons of skinned models, 2D joints.
    struct EditorGizmoSettings
    {
        GizmoVisibility CameraFrustums = GizmoVisibility::Selected;
        GizmoVisibility LightDirections = GizmoVisibility::Selected;
        GizmoVisibility AudioRanges = GizmoVisibility::Selected;
        GizmoVisibility Bounds = GizmoVisibility::Off;
        BoundsSpace BoundsMode = BoundsSpace::Local;
        GizmoVisibility Skeletons = GizmoVisibility::Selected;
        bool JointNames = false;                           // the joints' names next to them
        // Vertex normals of meshes and models (skinned ones in their pose): a line from each
        // vertex along its normal, NormalLength world units long.
        GizmoVisibility Normals = GizmoVisibility::Off;
        float NormalLength = 0.1f;
        bool NormalsFacingOnly = true;                     // only the normals on the side facing the camera
        bool NormalsByDirection = false;                   // coloured by direction (as the normals view) instead of blue
        // 2D joints: anchors, connected anchors and limits (revolute arcs, slide / suspension axes).
        GizmoVisibility Joints2D = GizmoVisibility::Selected;
        // Particle System emitters: their shape (circle, sphere, box, cone, edge) and direction.
        GizmoVisibility ParticleShapes = GizmoVisibility::Selected;
        // Spline paths (the curve, its points, the way it runs) and Spline Followers' start points.
        GizmoVisibility Splines = GizmoVisibility::All;

        static constexpr float MinNormalLength = 0.001f, MaxNormalLength = 100.0f;

        static const char* GetBoundsKey(BoundsSpace space);  // "Local" / "World" / "Both"
        static BoundsSpace BoundsFromKey(const std::string& key, BoundsSpace fallback);

        // Whether an entity (selected or not) gets the visualisation.
        static bool Shows(GizmoVisibility visibility, bool selected)
        {
            return visibility == GizmoVisibility::All || (visibility == GizmoVisibility::Selected && selected);
        }
        static const char* GetKey(GizmoVisibility visibility);           // "Off" / "Selected" / "All"
        static GizmoVisibility FromKey(const std::string& key, GizmoVisibility fallback);
        // NormalLength brought into range (not finite: the default).
        void Sanitize();
    };

    // Where an overlay sits in the editor viewport.
    enum class OverlayCorner { TopLeft = 0, TopRight, BottomLeft, BottomRight };

    // The statistics overlay over the editor viewport (never in the player): the frame rate and a
    // graph of the last frames' times, what the scene drew (draw calls, triangles, meshes, 2D
    // objects), what frustum culling skipped, the slowest profiler scopes, the audio voices and the
    // memory.
    struct ViewportStatsOverlaySettings
    {
        bool Visible = false;
        bool FrameGraph = true;
        bool DrawCalls = true;
        bool Culling = true;
        bool Hotspots = false;          // the profiler scopes that took the most time themselves
        bool Audio = false;             // the audio profiler: voices against the limit, by mixer group
        bool Memory = false;            // the process's RAM (RSS) and what the engine holds (MemoryStats)
        OverlayCorner Corner = OverlayCorner::TopRight;
        float Opacity = 0.85f;          // of the background
        float TargetFPS = 60.0f;        // the graph's budget line and the colours of the numbers

        static constexpr float MinTargetFPS = 10.0f, MaxTargetFPS = 360.0f;
        static constexpr float MinOpacity = 0.2f;

        // The frame time the target allows (ms).
        float GetBudgetMilliseconds() const { return 1000.0f / TargetFPS; }
        // How a frame time compares to the budget: 0 within it (5% slack), 1 up to twice it, 2 slower.
        int Rate(float milliseconds) const;
        static const char* GetCornerKey(OverlayCorner corner); // "TopLeft" ...
        static OverlayCorner CornerFromKey(const std::string& key, OverlayCorner fallback);
        void Sanitize();
    };

    // Where the editor camera looks from (EditorCamera::GetViewState / SetViewState).
    struct EditorCameraState
    {
        glm::vec3 FocalPoint{ 0.0f };
        float Distance = 12.0f;
        float Pitch = 0.35f;
        float Yaw = -0.6f;
        bool Orthographic = false; // the projection (the 2D mode, an axis view)
        bool Mode2D = false;       // the 2D editing mode: orthographic, no rotation
    };

    // One of the editor's extra scene views ("Ko'rinish 2" ... "Ko'rinish 4"): a window of its own
    // next to the main viewport, with a camera of its own (an axis view by default).
    struct SceneViewSettings
    {
        bool Open = false;
        EditorCameraState Camera;
        bool ShowGrid = true;

        static constexpr int Count = 3;     // extra views (the main viewport is view 1)
        // View `index` (0..Count-1) as it starts: 0 Top, 1 Front, 2 Right - orthographic.
        static SceneViewSettings GetDefault(int index);
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

    // Per-user viewport preferences of the editor: snapping, grid, the transform gizmo's size, the
    // selection outline, debug drawing, gizmos, the statistics overlay, the camera bookmarks of every scene (keyed by the
    // scene file path), the inspector's open / collapsed component headers and the content
    // browser's view, the game view's resolution. Stored as JSON in the editor config directory.
    struct EditorViewSettings
    {
        SnapSettings Snap;
        GridSettings Grid;
        TransformGizmoSettings TransformGizmo;
        SelectionOutlineSettings SelectionOutline;
        // What the viewport shows of DebugDraw (scripts' shapes, physics raycasts / contacts).
        DebugDrawSettings DebugDraw;
        EditorGizmoSettings Gizmos;
        ViewportStatsOverlaySettings StatsOverlay;
        std::map<std::string, CameraBookmarks> Bookmarks;
        InspectorFoldState Inspector;
        ContentBrowserSettings ContentBrowser;
        // The game view's size while playing (viewport toolbar / Ko'rinish menu).
        GameViewResolution GameView;
        // The extra scene views (Oyna > Ko'rinishlar) and the entities' names over the views.
        std::array<SceneViewSettings, SceneViewSettings::Count> SceneViews{ SceneViewSettings::GetDefault(0), SceneViewSettings::GetDefault(1),
                                                                            SceneViewSettings::GetDefault(2) };
        EntityLabelSettings EntityLabels;

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

        // Settings changed for one session only (the editor's automation hooks): each of `sections`
        // (top-level JSON keys such as "EntityLabels") that is still as `applied` left it, and that
        // `applied` changed from `before`, gets `before`'s value back. One changed since keeps its
        // own. Returns how many sections were put back.
        size_t RestoreUnchangedSections(const EditorViewSettings& before, const EditorViewSettings& applied,
                                        std::initializer_list<std::string_view> sections);
    };

}
