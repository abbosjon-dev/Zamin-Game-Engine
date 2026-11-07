#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <type_traits>

namespace Zamin {

    using json = nlohmann::json;

    // ------------------------------------------------------------------ snapping
    float SnapSettings::GetStep(int operation) const
    {
        switch (operation)
        {
            case 1:  return Rotate;
            case 2:  return Scale;
            default: return Translate;
        }
    }

    static float SanitizeStep(float value, float fallback, float minimum)
    {
        if (!std::isfinite(value)) return fallback;
        return std::max(value, minimum);
    }

    void SnapSettings::Sanitize()
    {
        Translate = SanitizeStep(Translate, 0.5f, MinStep);
        Rotate = std::min(SanitizeStep(Rotate, 15.0f, MinStep), 360.0f);
        Scale = SanitizeStep(Scale, 0.1f, MinStep);
    }

    // ------------------------------------------------------------------ grid
    void GridSettings::Sanitize()
    {
        CellSize = SanitizeStep(CellSize, 1.0f, MinCellSize);
        MajorEvery = std::clamp(MajorEvery, 1, MaxMajorEvery);
        FadeDistance = std::clamp(SanitizeStep(FadeDistance, 1.0f, 0.1f), 0.1f, 10.0f);
        Color = glm::clamp(Color, glm::vec4(0.0f), glm::vec4(1.0f));
        MajorColor = glm::clamp(MajorColor, glm::vec4(0.0f), glm::vec4(1.0f));
    }

    // ------------------------------------------------------------------ transform gizmo
    static float SanitizeFactor(float value, float minimum, float maximum)
    {
        if (!std::isfinite(value)) return 1.0f;
        return std::clamp(value, minimum, maximum);
    }

    void TransformGizmoSettings::Sanitize()
    {
        Size = SanitizeFactor(Size, MinSize, MaxSize);
        LineThickness = SanitizeFactor(LineThickness, MinThickness, MaxThickness);
        IconSize = SanitizeFactor(IconSize, MinIconSize, MaxIconSize);
    }

    // ------------------------------------------------------------------ selection outline
    static glm::vec4 SanitizeColor(const glm::vec4& color, const glm::vec4& fallback)
    {
        for (int i = 0; i < 4; i++)
            if (!std::isfinite(color[i])) return fallback;
        return glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
    }

    glm::vec4 SelectionOutlineSettings::GetSecondaryColor() const
    {
        return glm::vec4(glm::mix(glm::vec3(Color), glm::vec3(1.0f), 0.4f), Color.a * 0.9f);
    }

    glm::vec4 SelectionOutlineSettings::GetChildColor() const
    {
        return glm::vec4(glm::mix(glm::vec3(Color), glm::vec3(1.0f), 0.15f), Color.a * 0.5f);
    }

    void SelectionOutlineSettings::Sanitize()
    {
        if ((int)Style < (int)SelectionStyle::Outline || (int)Style > (int)SelectionStyle::Box)
            Style = SelectionStyle::Outline;
        Width = std::isfinite(Width) ? std::clamp(Width, MinWidth, MaxWidth) : 2.0f;
        HoverWidth = std::isfinite(HoverWidth) ? std::clamp(HoverWidth, MinWidth, MaxWidth) : 1.5f;
        OccludedOpacity = std::isfinite(OccludedOpacity) ? std::clamp(OccludedOpacity, 0.0f, 1.0f) : 0.35f;
        SelectionOutlineSettings defaults;
        Color = SanitizeColor(Color, defaults.Color);
        HoverColor = SanitizeColor(HoverColor, defaults.HoverColor);
    }

    // ------------------------------------------------------------------ camera bookmarks
    void CameraBookmarks::Set(int slot, const EditorCameraState& state)
    {
        if (slot >= 1 && slot <= SlotCount)
            m_Slots[slot - 1] = state;
    }

    void CameraBookmarks::Clear(int slot)
    {
        if (slot >= 1 && slot <= SlotCount)
            m_Slots[slot - 1].reset();
    }

    const EditorCameraState* CameraBookmarks::Get(int slot) const
    {
        if (slot < 1 || slot > SlotCount || !m_Slots[slot - 1])
            return nullptr;
        return &*m_Slots[slot - 1];
    }

    bool CameraBookmarks::IsEmpty() const { return Count() == 0; }

    int CameraBookmarks::Count() const
    {
        return (int)std::count_if(m_Slots.begin(), m_Slots.end(), [](const auto& s) { return s.has_value(); });
    }

    // ------------------------------------------------------------------ extra scene views
    SceneViewSettings SceneViewSettings::GetDefault(int index)
    {
        static const ViewAxis axes[Count] = { ViewAxis::Top, ViewAxis::Front, ViewAxis::Right };
        const ViewAxisInfo& axis = GetViewAxisInfo(axes[std::clamp(index, 0, Count - 1)]);
        SceneViewSettings view;
        view.Camera.FocalPoint = glm::vec3(0.0f);
        view.Camera.Distance = 20.0f;
        view.Camera.Pitch = axis.Pitch;
        view.Camera.Yaw = axis.Yaw;
        view.Camera.Orthographic = true;
        return view;
    }

    // ------------------------------------------------------------------ inspector headers
    bool InspectorFoldState::IsOpen(const std::string& key, bool defaultOpen) const
    {
        auto it = m_Open.find(key);
        return it == m_Open.end() ? defaultOpen : it->second;
    }

    bool InspectorFoldState::SetOpen(const std::string& key, bool open, bool defaultOpen)
    {
        if (key.empty())
            return false;
        auto it = m_Open.find(key);
        if (open == defaultOpen)
        {
            if (it == m_Open.end())
                return false;
            m_Open.erase(it);
            return true;
        }
        if (it != m_Open.end() && it->second == open)
            return false;
        if (it == m_Open.end() && m_Open.size() >= MaxEntries)
            return false;
        m_Open[key] = open;
        return true;
    }

    // ------------------------------------------------------------------ content browser
    void ContentBrowserSettings::Sanitize()
    {
        ThumbnailSize = std::clamp(SanitizeStep(ThumbnailSize, 84.0f, MinThumbnailSize), MinThumbnailSize, MaxThumbnailSize);
        Filter.Mask &= (1u << (uint32_t)AssetType::Count) - 1u;
        Filter.Set(AssetType::Folder, false);
        Filter.Set(AssetType::Project, false);
        if ((int)SortBy < (int)AssetSortColumn::Name || (int)SortBy > (int)AssetSortColumn::Modified)
            SortBy = AssetSortColumn::Name;
    }

    static const char* SortColumnKey(AssetSortColumn column)
    {
        switch (column)
        {
            case AssetSortColumn::Type:     return "Type";
            case AssetSortColumn::Size:     return "Size";
            case AssetSortColumn::Modified: return "Modified";
            default:                        return "Name";
        }
    }

    // ------------------------------------------------------------------ serialization
    std::string EditorViewSettings::SceneKey(const std::filesystem::path& scenePath)
    {
        if (scenePath.empty())
            return {};
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(scenePath, ec);
        return (ec ? scenePath : abs).lexically_normal().generic_string();
    }

    size_t EditorViewSettings::MoveSceneBookmarks(const std::filesystem::path& from, const std::filesystem::path& to)
    {
        std::string fromKey = SceneKey(from), toKey = SceneKey(to);
        if (fromKey.empty() || toKey.empty() || fromKey == toKey)
            return 0;
        std::vector<std::pair<std::string, std::string>> renames;
        for (const auto& [key, bookmarks] : Bookmarks)
            if (auto mapped = AssetFiles::RemapPath(key, fromKey, toKey))
                renames.emplace_back(key, *mapped);
        for (const auto& [oldKey, newKey] : renames)
        {
            CameraBookmarks moved = Bookmarks[oldKey];
            Bookmarks.erase(oldKey);
            Bookmarks[newKey] = moved;
        }
        return renames.size();
    }

    // Floats written with 6 significant digits (0.1, not 0.10000000149011612).
    static double Num(float v)
    {
        std::string text = std::format("{:.6g}", v);
        double out = v;
        std::from_chars(text.data(), text.data() + text.size(), out); // locale independent
        return out;
    }
    static json Vec(const glm::vec3& v) { return json::array({ Num(v.x), Num(v.y), Num(v.z) }); }
    static json Vec(const glm::vec4& v) { return json::array({ Num(v.x), Num(v.y), Num(v.z), Num(v.w) }); }

    template<typename T>
    static void Read(const json& j, const char* key, T& out)
    {
        auto it = j.find(key);
        if (it == j.end()) return;
        if constexpr (std::is_same_v<T, bool>)
        {
            if (it->is_boolean()) out = it->get<bool>();
        }
        else
        {
            if (it->is_number()) out = it->get<T>();
        }
    }

    template<int N>
    static void ReadVec(const json& j, const char* key, glm::vec<N, float>& out)
    {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->size() != N) return;
        glm::vec<N, float> v;
        for (int i = 0; i < N; i++)
        {
            if (!(*it)[i].is_number()) return;
            v[i] = (*it)[i].get<float>();
        }
        out = v;
    }

    const char* EditorGizmoSettings::GetKey(GizmoVisibility visibility)
    {
        switch (visibility)
        {
            case GizmoVisibility::Off: return "Off";
            case GizmoVisibility::All: return "All";
            default: return "Selected";
        }
    }

    GizmoVisibility EditorGizmoSettings::FromKey(const std::string& key, GizmoVisibility fallback)
    {
        for (GizmoVisibility v : { GizmoVisibility::Off, GizmoVisibility::Selected, GizmoVisibility::All })
            if (key == GetKey(v))
                return v;
        return fallback;
    }

    const char* EditorGizmoSettings::GetBoundsKey(BoundsSpace space)
    {
        switch (space)
        {
            case BoundsSpace::World: return "World";
            case BoundsSpace::Both: return "Both";
            default: return "Local";
        }
    }

    BoundsSpace EditorGizmoSettings::BoundsFromKey(const std::string& key, BoundsSpace fallback)
    {
        for (BoundsSpace space : { BoundsSpace::Local, BoundsSpace::World, BoundsSpace::Both })
            if (key == GetBoundsKey(space))
                return space;
        return fallback;
    }

    void EditorGizmoSettings::Sanitize()
    {
        NormalLength = std::isfinite(NormalLength) ? std::clamp(NormalLength, MinNormalLength, MaxNormalLength) : 0.1f;
        for (GizmoVisibility* v : { &CameraFrustums, &LightDirections, &AudioRanges, &Bounds, &Skeletons, &Normals, &Joints2D, &ParticleShapes })
            if ((int)*v < (int)GizmoVisibility::Off || (int)*v > (int)GizmoVisibility::All)
                *v = GizmoVisibility::Selected;
        if ((int)BoundsMode < (int)BoundsSpace::Local || (int)BoundsMode > (int)BoundsSpace::Both)
            BoundsMode = BoundsSpace::Local;
    }

    // ------------------------------------------------------------------ statistics overlay
    int ViewportStatsOverlaySettings::Rate(float milliseconds) const
    {
        const float budget = GetBudgetMilliseconds();
        if (!(milliseconds <= budget * 1.05f))
            return milliseconds <= budget * 2.0f ? 1 : 2;
        return 0;
    }

    const char* ViewportStatsOverlaySettings::GetCornerKey(OverlayCorner corner)
    {
        switch (corner)
        {
            case OverlayCorner::TopLeft: return "TopLeft";
            case OverlayCorner::BottomLeft: return "BottomLeft";
            case OverlayCorner::BottomRight: return "BottomRight";
            default: return "TopRight";
        }
    }

    OverlayCorner ViewportStatsOverlaySettings::CornerFromKey(const std::string& key, OverlayCorner fallback)
    {
        for (OverlayCorner c : { OverlayCorner::TopLeft, OverlayCorner::TopRight, OverlayCorner::BottomLeft, OverlayCorner::BottomRight })
            if (key == GetCornerKey(c))
                return c;
        return fallback;
    }

    void ViewportStatsOverlaySettings::Sanitize()
    {
        TargetFPS = std::isfinite(TargetFPS) ? std::clamp(TargetFPS, MinTargetFPS, MaxTargetFPS) : 60.0f;
        Opacity = std::isfinite(Opacity) ? std::clamp(Opacity, MinOpacity, 1.0f) : 0.85f;
        if ((int)Corner < (int)OverlayCorner::TopLeft || (int)Corner > (int)OverlayCorner::BottomRight)
            Corner = OverlayCorner::TopRight;
    }

    static json CameraStateJson(const EditorCameraState& s)
    {
        return {
            { "FocalPoint", Vec(s.FocalPoint) },
            { "Distance", Num(s.Distance) },
            { "Pitch", Num(s.Pitch) },
            { "Yaw", Num(s.Yaw) },
            { "Orthographic", s.Orthographic },
            { "Mode2D", s.Mode2D },
        };
    }

    // Files from before Mode2D: an orthographic camera was always the 2D mode.
    static EditorCameraState ReadCameraState(const json& value, EditorCameraState s)
    {
        ReadVec(value, "FocalPoint", s.FocalPoint);
        Read(value, "Distance", s.Distance);
        Read(value, "Pitch", s.Pitch);
        Read(value, "Yaw", s.Yaw);
        Read(value, "Orthographic", s.Orthographic);
        if (value.contains("Mode2D"))
            Read(value, "Mode2D", s.Mode2D);
        else
            s.Mode2D = s.Orthographic;
        s.Mode2D = s.Mode2D && s.Orthographic;
        if (!std::isfinite(s.Distance) || s.Distance <= 0.0f)
            s.Distance = 12.0f;
        if (!std::isfinite(s.Pitch)) s.Pitch = 0.35f;
        if (!std::isfinite(s.Yaw)) s.Yaw = -0.6f;
        for (int i = 0; i < 3; i++)
            if (!std::isfinite(s.FocalPoint[i])) s.FocalPoint[i] = 0.0f;
        return s;
    }

    std::string EditorViewSettings::ToJson() const
    {
        json j;
        j["ZaminEditorView"] = 1;
        j["Snap"] = {
            { "Enabled", Snap.Enabled },
            { "Translate", Num(Snap.Translate) },
            { "Rotate", Num(Snap.Rotate) },
            { "Scale", Num(Snap.Scale) },
        };
        j["Grid"] = {
            { "Visible", Grid.Visible },
            { "CellSize", Num(Grid.CellSize) },
            { "MajorEvery", Grid.MajorEvery },
            { "Color", Vec(Grid.Color) },
            { "MajorColor", Vec(Grid.MajorColor) },
            { "ShowAxes", Grid.ShowAxes },
            { "FadeDistance", Num(Grid.FadeDistance) },
        };
        j["TransformGizmo"] = {
            { "Size", Num(TransformGizmo.Size) },
            { "LineThickness", Num(TransformGizmo.LineThickness) },
            { "IconSize", Num(TransformGizmo.IconSize) },
        };
        j["SelectionOutline"] = {
            { "Style", SelectionOutline.Style == SelectionStyle::Box ? "Box" : "Outline" },
            { "Width", Num(SelectionOutline.Width) },
            { "Color", Vec(SelectionOutline.Color) },
            { "ShowOccluded", SelectionOutline.ShowOccluded },
            { "OccludedOpacity", Num(SelectionOutline.OccludedOpacity) },
            { "Children", SelectionOutline.Children },
            { "Hover", SelectionOutline.Hover },
            { "HoverWidth", Num(SelectionOutline.HoverWidth) },
            { "HoverColor", Vec(SelectionOutline.HoverColor) },
        };
        j["DebugDraw"] = {
            { "Enabled", DebugDraw.Enabled },
            { "Raycasts", DebugDraw.ShowRaycasts },
            { "RaycastDuration", Num(DebugDraw.RaycastDuration) },
            { "Contacts", DebugDraw.ShowContacts },
            { "ContactDetails", DebugDraw.ShowContactDetails },
            { "Joints", DebugDraw.ShowJoints },
            { "NormalLength", Num(DebugDraw.NormalLength) },
            { "LineWidth", Num(DebugDraw.LineWidth) },
            { "PointSize", Num(DebugDraw.PointSize) },
            { "TextSize", Num(DebugDraw.TextSize) },
            { "OccludedOpacity", Num(DebugDraw.OccludedOpacity) },
            { "Velocities", DebugDraw.ShowVelocities },
            { "BodyVelocities", DebugDraw.ShowBodyVelocities },
            { "VelocityScale", Num(DebugDraw.VelocityScale) },
        };
        j["Gizmos"] = {
            { "CameraFrustums", EditorGizmoSettings::GetKey(Gizmos.CameraFrustums) },
            { "LightDirections", EditorGizmoSettings::GetKey(Gizmos.LightDirections) },
            { "AudioRanges", EditorGizmoSettings::GetKey(Gizmos.AudioRanges) },
            { "Bounds", EditorGizmoSettings::GetKey(Gizmos.Bounds) },
            { "BoundsSpace", EditorGizmoSettings::GetBoundsKey(Gizmos.BoundsMode) },
            { "Skeletons", EditorGizmoSettings::GetKey(Gizmos.Skeletons) },
            { "JointNames", Gizmos.JointNames },
            { "Normals", EditorGizmoSettings::GetKey(Gizmos.Normals) },
            { "NormalLength", Num(Gizmos.NormalLength) },
            { "NormalsFacingOnly", Gizmos.NormalsFacingOnly },
            { "NormalsByDirection", Gizmos.NormalsByDirection },
            { "Joints2D", EditorGizmoSettings::GetKey(Gizmos.Joints2D) },
            { "ParticleShapes", EditorGizmoSettings::GetKey(Gizmos.ParticleShapes) },
        };
        j["StatsOverlay"] = {
            { "Visible", StatsOverlay.Visible },
            { "FrameGraph", StatsOverlay.FrameGraph },
            { "DrawCalls", StatsOverlay.DrawCalls },
            { "Culling", StatsOverlay.Culling },
            { "Hotspots", StatsOverlay.Hotspots },
            { "Audio", StatsOverlay.Audio },
            { "Corner", ViewportStatsOverlaySettings::GetCornerKey(StatsOverlay.Corner) },
            { "Opacity", Num(StatsOverlay.Opacity) },
            { "TargetFPS", Num(StatsOverlay.TargetFPS) },
        };
        json scenes = json::object();
        for (const auto& [scene, bookmarks] : Bookmarks)
        {
            if (scene.empty() || bookmarks.IsEmpty())
                continue; // unsaved scenes and empty sets are not kept
            json slots = json::object();
            for (int slot = 1; slot <= CameraBookmarks::SlotCount; slot++)
                if (const EditorCameraState* s = bookmarks.Get(slot))
                    slots[std::to_string(slot)] = CameraStateJson(*s);
            scenes[scene] = std::move(slots);
        }
        j["CameraBookmarks"] = std::move(scenes);
        json headers = json::object();
        for (const auto& [key, open] : Inspector.GetAll())
            headers[key] = open;
        j["InspectorHeaders"] = std::move(headers);
        json filter = json::array();
        for (AssetType type : AssetTypeFilter::GetFilterableTypes())
            if (ContentBrowser.Filter.Has(type))
                filter.push_back(AssetFiles::GetTypeKey(type));
        j["ContentBrowser"] = {
            { "View", ContentBrowser.ListView ? "List" : "Grid" },
            { "ThumbnailSize", Num(ContentBrowser.ThumbnailSize) },
            { "Filter", std::move(filter) },
            { "SortBy", SortColumnKey(ContentBrowser.SortBy) },
            { "Ascending", ContentBrowser.SortAscending },
            { "SearchScope", ContentBrowser.SearchEverywhere ? "Project" : "Folder" },
        };
        j["GameView"] = {
            { "Preset", GameViewResolution::GetKey(GameView.Preset) },
            { "CustomWidth", GameView.CustomSize.x },
            { "CustomHeight", GameView.CustomSize.y },
            { "CustomAspect", GameView.CustomAspect },
        };
        json views = json::array();
        for (const SceneViewSettings& view : SceneViews)
            views.push_back({ { "Open", view.Open }, { "Grid", view.ShowGrid }, { "Camera", CameraStateJson(view.Camera) } });
        j["SceneViews"] = std::move(views);
        j["EntityLabels"] = {
            { "Mode", EntityLabelSettings::GetModeKey(EntityLabels.Mode) },
            { "Fade", EntityLabels.Fade },
            { "FadeStart", Num(EntityLabels.FadeStart) },
            { "FadeEnd", Num(EntityLabels.FadeEnd) },
            { "FadeSelected", EntityLabels.FadeSelected },
            { "HideOverlapping", EntityLabels.HideOverlapping },
            { "Background", EntityLabels.Background },
            { "ShowInPlay", EntityLabels.ShowInPlay },
            { "Scale", Num(EntityLabels.Scale) },
            { "MaxLabels", EntityLabels.MaxLabels },
        };
        return j.dump(2);
    }

    bool EditorViewSettings::FromJson(const std::string& text)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("ZaminEditorView"))
            return false;

        if (auto it = j.find("Snap"); it != j.end() && it->is_object())
        {
            Read(*it, "Enabled", Snap.Enabled);
            Read(*it, "Translate", Snap.Translate);
            Read(*it, "Rotate", Snap.Rotate);
            Read(*it, "Scale", Snap.Scale);
            Snap.Sanitize();
        }
        if (auto it = j.find("Grid"); it != j.end() && it->is_object())
        {
            Read(*it, "Visible", Grid.Visible);
            Read(*it, "CellSize", Grid.CellSize);
            Read(*it, "MajorEvery", Grid.MajorEvery);
            ReadVec(*it, "Color", Grid.Color);
            ReadVec(*it, "MajorColor", Grid.MajorColor);
            Read(*it, "ShowAxes", Grid.ShowAxes);
            Read(*it, "FadeDistance", Grid.FadeDistance);
            Grid.Sanitize();
        }
        if (auto it = j.find("TransformGizmo"); it != j.end() && it->is_object())
        {
            Read(*it, "Size", TransformGizmo.Size);
            Read(*it, "LineThickness", TransformGizmo.LineThickness);
            Read(*it, "IconSize", TransformGizmo.IconSize);
            TransformGizmo.Sanitize();
        }
        if (auto it = j.find("SelectionOutline"); it != j.end() && it->is_object())
        {
            auto& o = SelectionOutline;
            if (auto style = it->find("Style"); style != it->end() && style->is_string())
                o.Style = style->get<std::string>() == "Box" ? SelectionStyle::Box : SelectionStyle::Outline;
            Read(*it, "Width", o.Width);
            ReadVec(*it, "Color", o.Color);
            Read(*it, "ShowOccluded", o.ShowOccluded);
            Read(*it, "OccludedOpacity", o.OccludedOpacity);
            Read(*it, "Children", o.Children);
            Read(*it, "Hover", o.Hover);
            Read(*it, "HoverWidth", o.HoverWidth);
            ReadVec(*it, "HoverColor", o.HoverColor);
            o.Sanitize();
        }
        if (auto it = j.find("DebugDraw"); it != j.end() && it->is_object())
        {
            Read(*it, "Enabled", DebugDraw.Enabled);
            Read(*it, "Raycasts", DebugDraw.ShowRaycasts);
            Read(*it, "RaycastDuration", DebugDraw.RaycastDuration);
            Read(*it, "Contacts", DebugDraw.ShowContacts);
            Read(*it, "ContactDetails", DebugDraw.ShowContactDetails);
            Read(*it, "Joints", DebugDraw.ShowJoints);
            Read(*it, "NormalLength", DebugDraw.NormalLength);
            Read(*it, "LineWidth", DebugDraw.LineWidth);
            Read(*it, "PointSize", DebugDraw.PointSize);
            Read(*it, "TextSize", DebugDraw.TextSize);
            Read(*it, "OccludedOpacity", DebugDraw.OccludedOpacity);
            Read(*it, "Velocities", DebugDraw.ShowVelocities);
            Read(*it, "BodyVelocities", DebugDraw.ShowBodyVelocities);
            Read(*it, "VelocityScale", DebugDraw.VelocityScale);
            DebugDraw.Sanitize();
        }
        if (auto it = j.find("Gizmos"); it != j.end() && it->is_object())
        {
            auto readVisibility = [&](const char* key, GizmoVisibility& out) {
                if (auto v = it->find(key); v != it->end() && v->is_string())
                    out = EditorGizmoSettings::FromKey(v->get<std::string>(), out);
            };
            readVisibility("CameraFrustums", Gizmos.CameraFrustums);
            readVisibility("LightDirections", Gizmos.LightDirections);
            readVisibility("AudioRanges", Gizmos.AudioRanges);
            readVisibility("Bounds", Gizmos.Bounds);
            readVisibility("Skeletons", Gizmos.Skeletons);
            readVisibility("Normals", Gizmos.Normals);
            readVisibility("Joints2D", Gizmos.Joints2D);
            readVisibility("ParticleShapes", Gizmos.ParticleShapes);
            Read(*it, "NormalLength", Gizmos.NormalLength);
            Read(*it, "NormalsFacingOnly", Gizmos.NormalsFacingOnly);
            Read(*it, "NormalsByDirection", Gizmos.NormalsByDirection);
            if (auto v = it->find("BoundsSpace"); v != it->end() && v->is_string())
                Gizmos.BoundsMode = EditorGizmoSettings::BoundsFromKey(v->get<std::string>(), Gizmos.BoundsMode);
            Read(*it, "JointNames", Gizmos.JointNames);
            Gizmos.Sanitize();
        }
        if (auto it = j.find("StatsOverlay"); it != j.end() && it->is_object())
        {
            auto& o = StatsOverlay;
            Read(*it, "Visible", o.Visible);
            Read(*it, "FrameGraph", o.FrameGraph);
            Read(*it, "DrawCalls", o.DrawCalls);
            Read(*it, "Culling", o.Culling);
            Read(*it, "Hotspots", o.Hotspots);
            Read(*it, "Audio", o.Audio);
            if (auto c = it->find("Corner"); c != it->end() && c->is_string())
                o.Corner = ViewportStatsOverlaySettings::CornerFromKey(c->get<std::string>(), o.Corner);
            Read(*it, "Opacity", o.Opacity);
            Read(*it, "TargetFPS", o.TargetFPS);
            o.Sanitize();
        }
        if (auto it = j.find("CameraBookmarks"); it != j.end() && it->is_object())
        {
            for (const auto& [scene, slots] : it->items())
            {
                if (scene.empty() || !slots.is_object())
                    continue;
                CameraBookmarks bookmarks;
                for (const auto& [key, value] : slots.items())
                {
                    int slot = std::atoi(key.c_str());
                    if (slot < 1 || slot > CameraBookmarks::SlotCount || !value.is_object())
                        continue;
                    bookmarks.Set(slot, ReadCameraState(value, EditorCameraState()));
                }
                Bookmarks[scene] = bookmarks;
            }
        }
        if (auto it = j.find("InspectorHeaders"); it != j.end() && it->is_object())
        {
            std::map<std::string, bool> headers;
            for (const auto& [key, open] : it->items())
                if (!key.empty() && open.is_boolean() && headers.size() < InspectorFoldState::MaxEntries)
                    headers[key] = open.get<bool>();
            Inspector.SetAll(std::move(headers));
        }
        if (auto it = j.find("ContentBrowser"); it != j.end() && it->is_object())
        {
            auto& cb = ContentBrowser;
            if (auto view = it->find("View"); view != it->end() && view->is_string())
                cb.ListView = view->get<std::string>() == "List";
            Read(*it, "ThumbnailSize", cb.ThumbnailSize);
            if (auto filter = it->find("Filter"); filter != it->end() && filter->is_array())
            {
                cb.Filter.Clear();
                for (const json& key : *filter)
                    if (key.is_string())
                        if (auto type = AssetFiles::FindTypeByKey(key.get<std::string>()))
                            cb.Filter.Set(*type, true);
            }
            if (auto sort = it->find("SortBy"); sort != it->end() && sort->is_string())
            {
                std::string key = sort->get<std::string>();
                cb.SortBy = AssetSortColumn::Name;
                for (AssetSortColumn c : { AssetSortColumn::Type, AssetSortColumn::Size, AssetSortColumn::Modified })
                    if (key == SortColumnKey(c))
                        cb.SortBy = c;
            }
            Read(*it, "Ascending", cb.SortAscending);
            if (auto scope = it->find("SearchScope"); scope != it->end() && scope->is_string())
                cb.SearchEverywhere = scope->get<std::string>() != "Folder";
            cb.Sanitize();
        }
        if (auto it = j.find("GameView"); it != j.end() && it->is_object())
        {
            if (auto preset = it->find("Preset"); preset != it->end() && preset->is_string())
                GameView.Preset = GameViewResolution::FromKey(preset->get<std::string>(), GameViewPreset::Free);
            Read(*it, "CustomWidth", GameView.CustomSize.x);
            Read(*it, "CustomHeight", GameView.CustomSize.y);
            Read(*it, "CustomAspect", GameView.CustomAspect);
            GameView.Sanitize();
        }
        if (auto it = j.find("SceneViews"); it != j.end() && it->is_array())
            for (size_t i = 0; i < it->size() && i < SceneViews.size(); i++)
            {
                const json& value = (*it)[i];
                if (!value.is_object())
                    continue;
                SceneViewSettings& view = SceneViews[i];
                Read(value, "Open", view.Open);
                Read(value, "Grid", view.ShowGrid);
                if (auto camera = value.find("Camera"); camera != value.end() && camera->is_object())
                {
                    view.Camera = ReadCameraState(*camera, view.Camera);
                    view.Camera.Mode2D = false; // the extra views have no 2D mode
                }
            }
        if (auto it = j.find("EntityLabels"); it != j.end() && it->is_object())
        {
            auto& l = EntityLabels;
            if (auto mode = it->find("Mode"); mode != it->end() && mode->is_string())
                l.Mode = EntityLabelSettings::ModeFromKey(mode->get<std::string>(), EntityLabelMode::Off);
            Read(*it, "Fade", l.Fade);
            Read(*it, "FadeStart", l.FadeStart);
            Read(*it, "FadeEnd", l.FadeEnd);
            Read(*it, "FadeSelected", l.FadeSelected);
            Read(*it, "HideOverlapping", l.HideOverlapping);
            Read(*it, "Background", l.Background);
            Read(*it, "ShowInPlay", l.ShowInPlay);
            Read(*it, "Scale", l.Scale);
            Read(*it, "MaxLabels", l.MaxLabels);
            l.Sanitize();
        }
        return true;
    }

    bool EditorViewSettings::Save(const std::filesystem::path& path) const
    {
        return FileSystem::WriteTextFile(path, ToJson());
    }

    bool EditorViewSettings::Load(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return false;
        return FromJson(FileSystem::ReadTextFile(path));
    }

}
