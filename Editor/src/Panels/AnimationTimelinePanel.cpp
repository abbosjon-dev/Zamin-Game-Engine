#include "AnimationTimelinePanel.h"
#include "../UI.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace Zamin {

    namespace fs = std::filesystem;

    static constexpr float s_ListWidth = 240.0f;
    static constexpr float s_RulerHeight = 24.0f;
    static constexpr float s_EventsHeight = 22.0f;
    static constexpr float s_RowHeight = 22.0f;
    static constexpr float s_DetailsWidth = 270.0f;

    static ImU32 ComponentColor(int c, float alpha = 1.0f)
    {
        static const ImVec4 colors[] = { { 0.92f, 0.38f, 0.36f, 1.0f }, { 0.45f, 0.82f, 0.42f, 1.0f },
                                         { 0.40f, 0.62f, 0.98f, 1.0f }, { 0.80f, 0.80f, 0.82f, 1.0f } };
        ImVec4 v = colors[std::clamp(c, 0, 3)];
        v.w = alpha;
        return ImGui::GetColorU32(v);
    }

    static const char* ComponentName(AnimatedProperty property, int c)
    {
        static const char* xyz[] = { "X", "Y", "Z", "W" };
        static const char* rgba[] = { "R", "G", "B", "A" };
        if (PropertyAnimation::GetComponentCount(property) == 1)
            return "";
        return PropertyAnimation::IsColor(property) ? rgba[std::clamp(c, 0, 3)] : xyz[std::clamp(c, 0, 3)];
    }

    static bool Near(const glm::vec4& a, const glm::vec4& b, int count)
    {
        for (int i = 0; i < count; i++)
            if (std::abs(a[i] - b[i]) > 1e-4f * std::max(1.0f, std::abs(a[i])))
                return false;
        return true;
    }

    static void DrawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 outline)
    {
        ImVec2 pts[4] = { { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y } };
        dl->AddConvexPolyFilled(pts, 4, fill);
        dl->AddPolyline(pts, 4, outline, ImDrawFlags_Closed, 1.0f);
    }

    // ------------------------------------------------------------------ target
    void AnimationTimelinePanel::SetContext(const Ref<Scene>& scene, bool editable)
    {
        if (scene != m_Scene)
        {
            // Another scene (play / stop, opened, undo): the old one's preview state means nothing now.
            StopPreview(m_Previewing && m_PreviewScene == m_Scene.get() && m_Scene && m_Editable);
            m_Scene = scene;
            m_Playing = false;
        }
        if (editable != m_Editable)
        {
            if (!editable)
                StopPreview(true);
            m_Editable = editable;
            m_Recording = m_Recording && editable;
        }
    }

    void AnimationTimelinePanel::SetSelection(Entity selected)
    {
        UUID id = selected ? selected.GetUUID() : UUID(0);
        if (id == m_SelectedID)
            return;
        m_SelectedID = id;
        m_Watch.clear();
        if (m_Pinned || !selected || !m_Scene)
            return;
        // The player of the selection (property clips) or its Animator (skeletal clips): its own,
        // else its nearest ancestor's; both on one entity: the mode shown stays.
        Entity player, animator;
        int playerDepth = -1, animatorDepth = -1, depth = 0;
        for (Entity e = selected; e && (!player || !animator); e = m_Scene->GetParent(e), depth++)
        {
            if (!player && e.HasComponent<AnimationPlayerComponent>()) { player = e; playerDepth = depth; }
            if (!animator && e.HasComponent<AnimatorComponent>()) { animator = e; animatorDepth = depth; }
        }
        const bool skeletal = animator && (!player || animatorDepth < playerDepth || (animatorDepth == playerDepth && m_Mode == Mode::Skeletal));
        if (skeletal)
        {
            if (animator.GetUUID() != m_AnimatorID || m_Mode != Mode::Skeletal)
                OpenAnimator(animator);
            return;
        }
        if (player)
        {
            if (m_Mode != Mode::Property)
            {
                StopSkeletalPreview();
                m_Mode = Mode::Property;
            }
            if (player.GetUUID() != m_PlayerID)
            {
                const auto& pc = player.GetComponent<AnimationPlayerComponent>();
                Open(player, pc.Current >= 0 ? pc.Current : 0);
            }
        }
    }

    Entity AnimationTimelinePanel::GetPlayer() const
    {
        if (!m_Scene || !m_PlayerID)
            return {};
        Entity e = m_Scene->GetEntityByUUID(m_PlayerID);
        return e && e.HasComponent<AnimationPlayerComponent>() ? e : Entity();
    }

    AnimationPlayerComponent* AnimationTimelinePanel::GetPlayerComponent() const
    {
        Entity e = GetPlayer();
        return e ? e.TryGetComponent<AnimationPlayerComponent>() : nullptr;
    }

    void AnimationTimelinePanel::Open(Entity player, int clip)
    {
        if (!player || !player.HasComponent<AnimationPlayerComponent>())
            return;
        if (player.GetUUID() != m_PlayerID)
            StopPreview(true);
        StopSkeletalPreview();
        m_Mode = Mode::Property;
        m_PlayerID = player.GetUUID();
        LoadClip(clip);
        m_FocusRequested = true;
    }

    void AnimationTimelinePanel::OpenClipFile(const fs::path& file)
    {
        const std::string relative = Project::GetAssetRelativePath(file);
        // A player of the scene that plays it: the clip is shown on it (previews, records).
        if (m_Scene)
            for (Entity e : m_Scene->GetEntitiesInOrder())
                if (auto* player = e.TryGetComponent<AnimationPlayerComponent>())
                    for (size_t i = 0; i < player->Clips.size(); i++)
                        if (player->Clips[i] == relative)
                        {
                            Open(e, (int)i);
                            return;
                        }
        StopPreview(true);
        StopSkeletalPreview();
        m_Mode = Mode::Property;
        m_PlayerID = 0;
        m_Pinned = true; // a clip on its own: the selection does not take it away
        Ref<PropertyClipAsset> asset = PropertyClipLibrary::Load(file);
        if (!asset)
        {
            ZM_ERROR("Animatsiya klipi ochilmadi: {}", relative);
            return;
        }
        m_ClipPath = relative;
        LoadAsset(asset, -1);
        m_FocusRequested = true;
    }

    void AnimationTimelinePanel::ClearClip()
    {
        StopPreview(true);
        m_Asset = nullptr;
        m_ClipIndex = -1;
        m_ClipPath.clear();
        m_Work = {};
        m_Dirty = false;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_SelectedKeys.clear();
        m_SelectedTrack = m_SelectedEvent = -1;
    }

    void AnimationTimelinePanel::LoadAsset(const Ref<PropertyClipAsset>& asset, int index)
    {
        if (asset == m_Asset && index == m_ClipIndex)
            return;
        SaveIfDirty(); // switching clips keeps the edits
        StopPreview(true);
        m_Asset = asset;
        m_ClipIndex = index;
        m_Work = asset ? asset->GetClip() : PropertyClip();
        m_AssetRevision = asset ? asset->GetRevision() : 0;
        m_Dirty = false;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_SelectedKeys.clear();
        m_SelectedTrack = m_Work.Tracks.empty() ? -1 : 0;
        m_SelectedEvent = -1;
        m_Time = std::clamp(m_Time, 0.0f, m_Work.Duration);
        m_PlayCursor = {};
        m_FitRequested = true;
    }

    void AnimationTimelinePanel::LoadClip(int index)
    {
        auto* player = GetPlayerComponent();
        if (!player)
        {
            ClearClip();
            return;
        }
        player->ResolveClips();
        if (index < 0 || index >= (int)player->Clips.size())
            index = player->Clips.empty() ? -1 : 0;
        if (index < 0 || !player->ClipAssets[(size_t)index])
        {
            ClearClip();
            m_ClipIndex = index;
            m_ClipPath = index >= 0 ? player->Clips[(size_t)index] : std::string();
            return;
        }
        m_Pinned = false;
        LoadAsset(player->ClipAssets[(size_t)index], index);
        m_ClipPath = player->Clips[(size_t)index];
    }

    void AnimationTimelinePanel::SyncTarget()
    {
        if (auto* player = GetPlayerComponent())
        {
            player->ResolveClips();
            // The list changed under us (a clip removed / replaced in the inspector): find ours again.
            if (m_ClipIndex < 0 && !m_Asset && !player->Clips.empty())
                LoadClip(0); // the first clip was just added
            else if (m_ClipIndex >= (int)player->Clips.size() || (m_ClipIndex >= 0 && player->Clips[(size_t)m_ClipIndex] != m_ClipPath))
            {
                auto it = std::find(player->Clips.begin(), player->Clips.end(), m_ClipPath);
                LoadClip(it != player->Clips.end() ? (int)(it - player->Clips.begin()) : 0);
            }
            else if (m_ClipIndex >= 0 && player->ClipAssets[(size_t)m_ClipIndex] != m_Asset)
            {
                const int index = m_ClipIndex;
                m_ClipIndex = -2; // forces the reload
                LoadClip(index);
            }
        }
        else if (m_PlayerID && m_Scene && !m_Scene->GetEntityByUUID(m_PlayerID))
        {
            // The player is gone (deleted, another scene): nothing to show unless it was a file of its own.
            if (m_ClipIndex >= 0)
                ClearClip();
        }
        // The file changed on disk (another program, an undo of a file move): the new content.
        if (m_Asset && m_Asset->GetRevision() != m_AssetRevision && !m_Dirty)
        {
            m_Work = m_Asset->GetClip();
            m_AssetRevision = m_Asset->GetRevision();
            m_SelectedKeys.clear();
        }
    }

    // ------------------------------------------------------------------ edits
    void AnimationTimelinePanel::BeginEdit()
    {
        if (m_EditOpen)
            return;
        m_UndoStack.push_back(m_Work);
        if (m_UndoStack.size() > 100)
            m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
        m_EditOpen = true;
    }

    void AnimationTimelinePanel::Commit()
    {
        if (m_Asset)
        {
            m_Asset->SetClip(m_Work);
            m_AssetRevision = m_Asset->GetRevision();
        }
        m_Dirty = true;
    }

    bool AnimationTimelinePanel::Undo()
    {
        if (m_UndoStack.empty() || !m_Asset)
            return false;
        m_RedoStack.push_back(std::move(m_Work));
        m_Work = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();
        m_SelectedKeys.clear();
        m_SelectedEvent = -1;
        m_EditOpen = false;
        m_Applied.clear(); // what the preview wrote belongs to the other version (no edits to detect)
        Commit();
        return true;
    }

    bool AnimationTimelinePanel::Redo()
    {
        if (m_RedoStack.empty() || !m_Asset)
            return false;
        m_UndoStack.push_back(std::move(m_Work));
        m_Work = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();
        m_SelectedKeys.clear();
        m_SelectedEvent = -1;
        m_EditOpen = false;
        m_Applied.clear(); // what the preview wrote belongs to the other version (no edits to detect)
        Commit();
        return true;
    }

    bool AnimationTimelinePanel::Save()
    {
        if (!m_Asset)
            return false;
        m_Asset->SetClip(m_Work);
        m_AssetRevision = m_Asset->GetRevision();
        if (!m_Asset->Save())
        {
            ZM_ERROR("Animatsiya klipi saqlanmadi: {}", m_Asset->GetPath().string());
            return false;
        }
        m_Dirty = false;
        ZM_INFO("Animatsiya klipi saqlandi: {}", Project::GetAssetRelativePath(m_Asset->GetPath()));
        if (OnClipSaved)
            OnClipSaved(m_Asset->GetPath());
        return true;
    }

    float AnimationTimelinePanel::Snap(float time) const
    {
        time = std::max(0.0f, time);
        if (!m_Snap)
            return time;
        const float fps = std::max(1.0f, m_Work.FrameRate);
        return std::round(time * fps) / fps;
    }

    void AnimationTimelinePanel::SetTime(float seconds)
    {
        if (m_Mode == Mode::Skeletal)
        {
            SetSkeletalTime(seconds);
            return;
        }
        m_Time = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, std::max(0.0f, m_Work.Duration));
        m_PlayCursor.Time = m_Time;
        // While the game runs: the running player goes there (and holds).
        if (!m_Editable)
            if (auto* player = GetPlayerComponent(); player && player->Current == m_ClipIndex && m_ClipIndex >= 0)
            {
                player->Stop();
                player->SetTime(m_Time);
            }
    }

    void AnimationTimelinePanel::SelectTrack(int index)
    {
        m_SelectedTrack = index >= 0 && index < (int)m_Work.Tracks.size() ? index : -1;
    }

    void AnimationTimelinePanel::SelectKey(int track, int key)
    {
        m_SelectedKeys.clear();
        if (track >= 0 && track < (int)m_Work.Tracks.size() && key >= 0 && key < (int)m_Work.Tracks[(size_t)track].Keys.size())
        {
            m_SelectedKeys.push_back({ track, key });
            m_SelectedTrack = track;
        }
    }

    bool AnimationTimelinePanel::IsKeySelected(int track, int key) const
    {
        return std::find(m_SelectedKeys.begin(), m_SelectedKeys.end(), KeyRef{ track, key }) != m_SelectedKeys.end();
    }

    std::string AnimationTimelinePanel::TrackLabel(const AnimationTrack& track) const
    {
        std::string owner;
        if (track.Path.empty())
        {
            Entity player = GetPlayer();
            owner = player ? player.GetName() : std::string("(o'zi)");
        }
        else
        {
            size_t slash = track.Path.rfind('/');
            owner = slash == std::string::npos ? track.Path : track.Path.substr(slash + 1);
        }
        std::string label = owner + "  " + PropertyAnimation::GetPropertyName(track.Property);
        if (track.Property == AnimatedProperty::ScriptFloat)
            label += " " + track.Field;
        return label;
    }

    glm::vec4 AnimationTimelinePanel::CurrentValue(const AnimationTrack& track) const
    {
        glm::vec4 v(0.0f);
        if (Entity player = GetPlayer())
            if (Entity target = AnimationTargets::Resolve(player, track.Path); target && AnimationTargets::Read(target, track.Property, track.Field, v))
                return v;
        return track.Keys.empty() ? v : track.Evaluate(m_Time);
    }

    void AnimationTimelinePanel::AddTrack(const std::string& path, AnimatedProperty property, const std::string& field)
    {
        if (m_Work.FindTrack(path, property, field))
            return;
        BeginEdit();
        AnimationTrack& track = m_Work.GetOrAddTrack(path, property, field);
        glm::vec4 value(0.0f);
        if (Entity player = GetPlayer())
            if (Entity target = AnimationTargets::Resolve(player, path))
                AnimationTargets::Read(target, property, field, value);
        track.SetKey(Snap(m_Time), value);
        Commit();
        EndEdit();
        m_SelectedTrack = (int)m_Work.Tracks.size() - 1;
        SelectKey(m_SelectedTrack, 0);
    }

    void AnimationTimelinePanel::RemoveTrack(int index)
    {
        if (index < 0 || index >= (int)m_Work.Tracks.size())
            return;
        BeginEdit();
        m_Work.Tracks.erase(m_Work.Tracks.begin() + index);
        Commit();
        EndEdit();
        m_SelectedKeys.clear();
        if (m_SelectedTrack >= (int)m_Work.Tracks.size())
            m_SelectedTrack = (int)m_Work.Tracks.size() - 1;
    }

    void AnimationTimelinePanel::AddKeys(int trackIndex, float time)
    {
        if (m_Work.Tracks.empty())
            return;
        BeginEdit();
        m_SelectedKeys.clear();
        time = Snap(time);
        for (int t = 0; t < (int)m_Work.Tracks.size(); t++)
        {
            if (trackIndex >= 0 && t != trackIndex)
                continue;
            AnimationTrack& track = m_Work.Tracks[(size_t)t];
            // At the scrubbed time the entity shows the curve's value (or what was just changed in
            // the inspector); elsewhere the key goes on the curve where it is.
            glm::vec4 value = std::abs(time - m_Time) < 1e-5f ? CurrentValue(track) : track.Evaluate(time);
            int key = track.SetKey(time, value, FrameDuration() * 0.25f);
            m_SelectedKeys.push_back({ t, key });
        }
        Commit();
        EndEdit();
    }

    void AnimationTimelinePanel::DeleteSelection()
    {
        if (m_SelectedEvent >= 0 && m_SelectedEvent < (int)m_Work.Events.size())
        {
            BeginEdit();
            m_Work.Events.erase(m_Work.Events.begin() + m_SelectedEvent);
            m_SelectedEvent = -1;
            Commit();
            EndEdit();
            return;
        }
        if (m_SelectedKeys.empty())
            return;
        BeginEdit();
        std::vector<KeyRef> keys = m_SelectedKeys;
        std::sort(keys.begin(), keys.end(), [](const KeyRef& a, const KeyRef& b) { return a.Track != b.Track ? a.Track < b.Track : a.Key > b.Key; });
        for (const KeyRef& k : keys)
            if (k.Track >= 0 && k.Track < (int)m_Work.Tracks.size())
            {
                auto& track = m_Work.Tracks[(size_t)k.Track];
                if (k.Key >= 0 && k.Key < (int)track.Keys.size())
                    track.Keys.erase(track.Keys.begin() + k.Key);
                track.UpdateTangents();
            }
        m_SelectedKeys.clear();
        Commit();
        EndEdit();
    }

    void AnimationTimelinePanel::AddEvent(float time)
    {
        BeginEdit();
        AnimationEvent ev;
        ev.Time = Snap(time);
        ev.Name = "Event";
        m_Work.Events.push_back(ev);
        m_SelectedEvent = (int)m_Work.Events.size() - 1;
        SortEventsKeepSelection();
        Commit();
        EndEdit();
        m_SelectedKeys.clear();
    }

    void AnimationTimelinePanel::SortTrackKeepSelection(int trackIndex)
    {
        if (trackIndex < 0 || trackIndex >= (int)m_Work.Tracks.size())
            return;
        AnimationTrack& track = m_Work.Tracks[(size_t)trackIndex];
        std::vector<int> order(track.Keys.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return track.Keys[(size_t)a].Time < track.Keys[(size_t)b].Time; });
        std::vector<AnimationKey> sorted;
        std::vector<int> newIndex(order.size());
        sorted.reserve(order.size());
        for (size_t i = 0; i < order.size(); i++)
        {
            newIndex[(size_t)order[i]] = (int)i;
            sorted.push_back(track.Keys[(size_t)order[i]]);
        }
        track.Keys = std::move(sorted);
        track.UpdateTangents();
        for (KeyRef& k : m_SelectedKeys)
            if (k.Track == trackIndex && k.Key >= 0 && k.Key < (int)newIndex.size())
                k.Key = newIndex[(size_t)k.Key];
    }

    void AnimationTimelinePanel::SortEventsKeepSelection()
    {
        const bool selected = m_SelectedEvent >= 0 && m_SelectedEvent < (int)m_Work.Events.size();
        const int before = m_SelectedEvent;
        std::vector<int> order(m_Work.Events.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return m_Work.Events[(size_t)a].Time < m_Work.Events[(size_t)b].Time; });
        std::vector<AnimationEvent> sorted;
        for (int i : order)
            sorted.push_back(m_Work.Events[(size_t)i]);
        m_Work.Events = std::move(sorted);
        if (selected)
            for (size_t i = 0; i < order.size(); i++)
                if (order[i] == before)
                    m_SelectedEvent = (int)i;
    }

    // ------------------------------------------------------------------ preview
    void AnimationTimelinePanel::CaptureBase(Entity target, AnimatedProperty property, const std::string& field)
    {
        const UUID id = target.GetUUID();
        for (const PropertyValue& b : m_Base)
            if (b.Entity == id && b.Property == property && b.Field == field)
                return;
        PropertyValue base;
        base.Entity = id;
        base.Property = property;
        base.Field = field;
        if (AnimationTargets::Read(target, property, field, base.Value))
            m_Base.push_back(std::move(base));
    }

    void AnimationTimelinePanel::ApplyPreview()
    {
        Entity player = GetPlayer();
        if (!player || !m_Scene)
            return;
        m_Applied.clear();
        for (int i = 0; i < (int)m_Work.Tracks.size(); i++)
        {
            const AnimationTrack& track = m_Work.Tracks[(size_t)i];
            if (track.Keys.empty())
                continue;
            Entity target = AnimationTargets::Resolve(player, track.Path);
            if (!target)
                continue;
            CaptureBase(target, track.Property, track.Field);
            if (!AnimationTargets::Write(target, track.Property, track.Field, track.Evaluate(m_Time)))
                continue;
            PropertyValue applied;
            applied.Entity = target.GetUUID();
            applied.Property = track.Property;
            applied.Field = track.Field;
            applied.Track = i;
            AnimationTargets::Read(target, track.Property, track.Field, applied.Value); // as the component holds it
            m_Applied.push_back(std::move(applied));
        }
        // Values of tracks that are gone (removed, undone): the scene's own again.
        for (auto it = m_Base.begin(); it != m_Base.end();)
        {
            const bool used = std::any_of(m_Applied.begin(), m_Applied.end(), [&](const PropertyValue& a) {
                return a.Entity == it->Entity && a.Property == it->Property && a.Field == it->Field;
            });
            if (used)
            {
                ++it;
                continue;
            }
            if (Entity e = m_Scene->GetEntityByUUID(it->Entity))
            {
                AnimationTargets::Write(e, it->Property, it->Field, it->Value);
                if (it->Property == AnimatedProperty::MeshColor)
                    if (auto* mr = e.TryGetComponent<MeshRendererComponent>())
                        Scene::ClearMaterialInstances(*mr);
            }
            it = m_Base.erase(it);
        }
        m_Previewing = true;
        m_PreviewScene = m_Scene.get();
    }

    void AnimationTimelinePanel::StopPreview(bool restore)
    {
        StopSkeletalPreview();
        if (restore && m_Previewing && m_PreviewScene && m_PreviewScene == m_Scene.get())
            for (const PropertyValue& b : m_Base)
                if (Entity e = m_Scene->GetEntityByUUID(b.Entity))
                {
                    AnimationTargets::Write(e, b.Property, b.Field, b.Value);
                    if (b.Property == AnimatedProperty::MeshColor)
                        if (auto* mr = e.TryGetComponent<MeshRendererComponent>())
                            Scene::ClearMaterialInstances(*mr);
                }
        m_Base.clear();
        m_Applied.clear();
        m_Watch.clear();
        m_Previewing = false;
        m_PreviewScene = nullptr;
        if (m_RecordEditing)
        {
            EndEdit();
            m_RecordEditing = false;
        }
    }

    void AnimationTimelinePanel::WithoutPreview(const std::function<void()>& f)
    {
        if (!m_Previewing || !m_Scene || m_PreviewScene != m_Scene.get())
        {
            f();
            return;
        }
        for (const PropertyValue& b : m_Base)
            if (Entity e = m_Scene->GetEntityByUUID(b.Entity))
                AnimationTargets::Write(e, b.Property, b.Field, b.Value);
        f();
        for (const PropertyValue& a : m_Applied)
            if (Entity e = m_Scene->GetEntityByUUID(a.Entity))
                AnimationTargets::Write(e, a.Property, a.Field, a.Value);
    }

    void AnimationTimelinePanel::RefreshWatch()
    {
        m_Watch.clear();
        Entity player = GetPlayer();
        Entity selected = m_Scene && m_SelectedID ? m_Scene->GetEntityByUUID(m_SelectedID) : Entity();
        if (!player || !selected)
            return;
        std::optional<std::string> path = AnimationTargets::MakePath(player, selected);
        if (!path)
            return; // not under the player: nothing of it can be animated by this clip
        for (const auto& candidate : AnimationTargets::GetCandidates(selected))
        {
            if (candidate.Property == AnimatedProperty::ScriptFloat || m_Work.FindTrack(*path, candidate.Property, candidate.Field))
                continue;
            PropertyValue w;
            w.Entity = selected.GetUUID();
            w.Property = candidate.Property;
            w.Field = candidate.Field;
            if (AnimationTargets::Read(selected, candidate.Property, candidate.Field, w.Value))
                m_Watch.push_back(std::move(w));
        }
    }

    void AnimationTimelinePanel::DetectEdits()
    {
        if (!m_Scene)
            return;
        Entity player = GetPlayer();
        bool changed = false;
        const float time = Snap(m_Time);
        // What the preview wrote and is not there any more: changed in the inspector or with the gizmo.
        for (const PropertyValue& a : m_Applied)
        {
            Entity e = m_Scene->GetEntityByUUID(a.Entity);
            if (!e || a.Track < 0 || a.Track >= (int)m_Work.Tracks.size())
                continue;
            glm::vec4 now;
            if (!AnimationTargets::Read(e, a.Property, a.Field, now))
                continue;
            AnimationTrack& track = m_Work.Tracks[(size_t)a.Track];
            const int count = track.GetComponentCount();
            if (Near(now, a.Value, count))
                continue;
            if (track.Property != a.Property || track.Field != a.Field || !player ||
                AnimationTargets::Resolve(player, track.Path) != e)
                continue; // the tracks changed since (removed, reordered)
            if (m_Recording)
            {
                if (!m_RecordEditing)
                {
                    BeginEdit();
                    m_RecordEditing = true;
                }
                int key = track.SetKey(time, now, FrameDuration() * 0.25f);
                SelectKey(a.Track, key);
                changed = true;
            }
            else
            {
                // Not recording: an edit of the scene's own value (shown once the preview ends).
                for (PropertyValue& b : m_Base)
                    if (b.Entity == a.Entity && b.Property == a.Property && b.Field == a.Field)
                        b.Value = now;
                m_Status = "Yozish o'chiq: o'zgarish sahnaning o'z qiymatiga yozildi (kalit qo'yilmadi)";
                m_StatusUntil = ImGui::GetTime() + 4.0;
            }
        }
        // Record: a property of the selected entity that no track has yet - a new track, with the old
        // value at the start (so the change animates from there) and the new one now.
        if (m_Recording && player)
        {
            Entity selected = m_SelectedID ? m_Scene->GetEntityByUUID(m_SelectedID) : Entity();
            std::optional<std::string> path = selected ? AnimationTargets::MakePath(player, selected) : std::nullopt;
            for (const PropertyValue& w : m_Watch)
            {
                if (!path || !selected || w.Entity != selected.GetUUID())
                    continue;
                glm::vec4 now;
                if (!AnimationTargets::Read(selected, w.Property, w.Field, now) ||
                    Near(now, w.Value, PropertyAnimation::GetComponentCount(w.Property)))
                    continue;
                if (!m_RecordEditing)
                {
                    BeginEdit();
                    m_RecordEditing = true;
                }
                AnimationTrack& track = m_Work.GetOrAddTrack(*path, w.Property, w.Field);
                if (time > FrameDuration() * 0.5f)
                    track.SetKey(0.0f, w.Value);
                int key = track.SetKey(time, now);
                // The scene's own value is the old one (back when the preview ends).
                bool known = false;
                for (const PropertyValue& b : m_Base)
                    known |= b.Entity == w.Entity && b.Property == w.Property && b.Field == w.Field;
                if (!known)
                {
                    PropertyValue base = w;
                    m_Base.push_back(base);
                }
                SelectKey((int)m_Work.Tracks.size() - 1, key);
                (void)key;
                changed = true;
                m_Status = std::format("Yangi trek yozildi: {}", TrackLabel(track));
                m_StatusUntil = ImGui::GetTime() + 3.0;
            }
        }
        if (changed)
            Commit();
        else if (m_RecordEditing)
        {
            EndEdit(); // the gesture is over: the next change is a new undo step
            m_RecordEditing = false;
        }
    }

    void AnimationTimelinePanel::OnUpdate(Timestep ts, bool visible)
    {
        UpdateSkeletal(ts, visible);
        if (m_Mode == Mode::Skeletal)
        {
            if (m_Previewing)
                StopPreview(true);
            return;
        }
        Entity player = GetPlayer();
        const bool preview = visible && m_Editable && m_Scene && player && m_Asset;
        if (!preview)
        {
            if (m_Previewing)
                StopPreview(true);
            // The game runs: the window shows where the player is.
            if (visible && !m_Editable)
                if (auto* p = GetPlayerComponent(); p && p->Current == m_ClipIndex && m_ClipIndex >= 0 && p->ClipAssets[(size_t)m_ClipIndex] == m_Asset)
                    m_Time = p->GetTime();
            return;
        }
        if (m_Playing)
        {
            // The preview goes round (a Once clip too) until stopped.
            const AnimationWrap wrap = m_Work.Wrap == AnimationWrap::PingPong ? AnimationWrap::PingPong : AnimationWrap::Loop;
            AdvanceClip(m_PlayCursor, ts, m_Work.Duration, wrap, nullptr);
            m_Time = m_PlayCursor.Time;
        }
        else
            DetectEdits();
        ApplyPreview();
        // What the selected entity has that no track animates yet, as it is now (record compares
        // against it next frame).
        RefreshWatch();
    }

    // ------------------------------------------------------------------ drawing
    void AnimationTimelinePanel::OnImGuiRender(bool* show)
    {
        if (!*show)
        {
            m_Focused = false;
            return;
        }
        if (m_FocusRequested)
        {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(1000.0f, 340.0f), ImGuiCond_FirstUseEver);
        if (m_DefaultDock)
            ImGui::SetNextWindowDockID(m_DefaultDock, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_TIMELINE "  Animatsiya###Animation", show))
        {
            m_Focused = false;
            ImGui::End();
            return;
        }
        m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (m_Mode == Mode::Skeletal)
        {
            DrawSkeletalToolbar();
            ImGui::Separator();
            if (!GetAnimatorEntity() || !GetAnimatorModel() || !GetSkeletalClip())
            {
                DrawSkeletalEmptyState();
                ImGui::End();
                return;
            }
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float details = std::min(s_DetailsWidth, avail.x * 0.35f);
            ImGui::BeginChild("##skeltimeline", ImVec2(avail.x - details - ImGui::GetStyle().ItemSpacing.x, avail.y), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
            DrawSkeletalTimeline();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##skeldetails", ImVec2(details, avail.y), ImGuiChildFlags_Borders);
            DrawSkeletalDetails();
            ImGui::EndChild();
            HandleSkeletalShortcuts();
            ImGui::End();
            return;
        }
        SyncTarget();
        DrawToolbar();
        ImGui::Separator();
        if (!m_Asset)
        {
            DrawEmptyState();
            ImGui::End();
            return;
        }
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float details = std::min(s_DetailsWidth, avail.x * 0.35f);
        ImGui::BeginChild("##timeline", ImVec2(avail.x - details - ImGui::GetStyle().ItemSpacing.x, avail.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        DrawTimeline();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##details", ImVec2(details, avail.y), ImGuiChildFlags_Borders);
        DrawDetails();
        ImGui::EndChild();
        DrawAddTrackPopup();
        HandleShortcuts();
        ImGui::End();
    }

    void AnimationTimelinePanel::DrawToolbar()
    {
        Entity player = GetPlayer();
        auto* pc = GetPlayerComponent();
        DrawModeSwitch();
        // ---- target and clip
        ImGui::AlignTextToFramePadding();
        if (player)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_FILM "  %s", player.GetName().c_str());
        else if (m_Asset)
            ImGui::TextDisabled(ICON_FA_FILM "  (obyektsiz)");
        else
            ImGui::TextDisabled(ICON_FA_FILM "  -");
        if (ImGui::IsItemHovered())
            UI::Tooltip(player ? "Animation Player obyekti (klip treklari shu obyektga va uning bolalariga nisbatan)"
                               : "Klip sahnadagi hech bir Animation Player'da yo'q: ko'rib bo'lmaydi, faqat tahrirlanadi");
        ImGui::SameLine();
        if (UI::ToolbarButton(m_Pinned ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN, m_Pinned ? "Qulflangan: tanlov o'zgarsa ham shu obyekt qoladi"
                                                                                    : "Tanlangan obyektning Animation Player'iga ergashadi", m_Pinned))
            m_Pinned = !m_Pinned;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        std::string clipName = m_Asset ? m_Asset->GetName() : m_ClipPath.empty() ? std::string("(klip yo'q)") : fs::path(m_ClipPath).stem().string();
        if (m_Dirty)
            clipName += " *";
        if (ImGui::BeginCombo("##clip", clipName.c_str()))
        {
            if (pc)
                for (int i = 0; i < (int)pc->Clips.size(); i++)
                {
                    std::string name = fs::path(pc->Clips[(size_t)i]).stem().string();
                    if (ImGui::Selectable(std::format("{}##{}", name, i).c_str(), i == m_ClipIndex))
                        LoadClip(i);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(pc->Clips[(size_t)i].c_str());
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Asset || !m_Dirty);
        if (UI::ToolbarButton(ICON_FA_FLOPPY_DISK, "Klipni saqlash (Ctrl+S)"))
            Save();
        ImGui::EndDisabled();
        if (!m_Asset)
            return;

        // ---- transport
        ImGui::SameLine(0.0f, 14.0f);
        if (UI::ToolbarButton(ICON_FA_BACKWARD_STEP, "Boshiga (Home)")) { m_Playing = false; SetTime(0.0f); }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Editable);
        if (UI::ToolbarButton(m_Playing ? ICON_FA_PAUSE : ICON_FA_PLAY, m_Editable ? "Ko'rib chiqish: o'ynatish / to'xtatish (Probel)"
                                                                                : "O'yin rejimida klipni o'yin o'ynatadi", m_Playing))
        {
            m_Playing = !m_Playing;
            m_PlayCursor = {};
            m_PlayCursor.Time = m_Time >= m_Work.Duration ? 0.0f : m_Time;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_FORWARD_STEP, "Oxiriga (End)")) { m_Playing = false; SetTime(m_Work.Duration); }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        float time = m_Time;
        const int frame = (int)std::round(m_Time * m_Work.FrameRate);
        if (ImGui::DragFloat("##time", &time, 0.01f, 0.0f, m_Work.Duration, std::format("%.2f s  ({} kadr)", frame).c_str()))
        {
            m_Playing = false;
            SetTime(time);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Editable || !player);
        ImGui::PushStyleColor(ImGuiCol_Text, m_Recording ? ImVec4(1.0f, 0.35f, 0.3f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if (UI::ToolbarButton(ICON_FA_CIRCLE, "Yozish: inspektor yoki gizmo bilan qilingan o'zgarishlar joriy vaqtda kalit bo'ladi", m_Recording))
        {
            m_Recording = !m_Recording;
            m_Playing = false;
        }
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_MAGNET, "Kadrlarga yopishish", m_Snap))
            m_Snap = !m_Snap;

        // ---- view
        ImGui::SameLine(0.0f, 14.0f);
        if (UI::ToolbarButton(ICON_FA_DIAMOND, "Kalitlar jadvali (dope sheet)", !m_CurveView))
            m_CurveView = false;
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_BEZIER_CURVE, "Egri chiziqlar (tanlangan trek)", m_CurveView))
            m_CurveView = true;
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_EXPAND, "Klipni oynaga sig'dirish (F)"))
            m_FitRequested = true;
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::BeginDisabled(m_Work.Tracks.empty());
        if (UI::ToolbarButton(ICON_FA_KEY, "Joriy vaqtda kalit (K): tanlangan trekka, bo'lmasa hammasiga"))
            AddKeys(m_SelectedTrack, m_Time);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_BELL, "Joriy vaqtda event: skriptning OnAnimationEvent(nom) chaqiriladi"))
            AddEvent(m_Time);
        if (!m_Status.empty() && ImGui::GetTime() < m_StatusUntil)
        {
            ImGui::SameLine(0.0f, 14.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%s", m_Status.c_str());
        }
        else if (m_Recording)
        {
            ImGui::SameLine(0.0f, 14.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), ICON_FA_CIRCLE "  YOZILMOQDA");
        }
    }

    void AnimationTimelinePanel::DrawEmptyState()
    {
        Entity selected = m_Scene && m_SelectedID ? m_Scene->GetEntityByUUID(m_SelectedID) : Entity();
        auto* pc = GetPlayerComponent();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        if (pc && pc->Clips.empty())
            ImGui::TextDisabled("Animation Player'da klip yo'q: inspektorda \"Yangi klip\" bilan yarating yoki .zanim faylini qo'shing");
        else if (pc && !m_ClipPath.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Klip yuklanmadi: %s", m_ClipPath.c_str());
        else
            ImGui::TextDisabled("Animation Player komponenti bor obyektni tanlang (yoki kontent oynasida .zanim faylini oching)");
        if (selected && !pc && m_Editable && Project::GetActive() && !selected.HasComponent<AnimationPlayerComponent>())
        {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            if (ImGui::Button(std::format(ICON_FA_PLUS "  \"{}\" ga Animation Player va yangi klip", selected.GetName()).c_str()))
            {
                std::string name;
                for (char ch : selected.GetName())
                    name += std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' ? ch : '_';
                if (name.empty()) name = "Clip";
                fs::path file = AssetFiles::MakeUniquePath(Project::GetAssetDirectory() / "Animations", name + PropertyClip::Extension);
                if (PropertyClipSerializer::Serialize(PropertyClip(), file))
                {
                    if (AssetRegistry* registry = Project::GetAssetRegistry())
                        registry->OnFileAdded(file);
                    auto& player = selected.AddComponent<AnimationPlayerComponent>();
                    player.Clips.push_back(Project::GetAssetRelativePath(file));
                    if (OnClipSaved)
                        OnClipSaved(file);
                    ZM_INFO("Animation Player va yangi klip: {} ({})", selected.GetName(), player.Clips.back());
                    Open(selected, 0);
                }
            }
        }
    }

    void AnimationTimelinePanel::DrawAddTrackPopup()
    {
        if (m_OpenAddTrack)
        {
            ImGui::OpenPopup("##addtrack");
            m_OpenAddTrack = false;
        }
        if (!ImGui::BeginPopup("##addtrack"))
            return;
        Entity player = GetPlayer();
        if (!player || !m_Scene)
        {
            ImGui::TextDisabled("Klip Animation Player'ga biriktirilmagan");
            ImGui::EndPopup();
            return;
        }
        ImGui::TextDisabled("Obyekt va xususiyat");
        ImGui::Separator();
        std::vector<Entity> entities = { player };
        for (Entity e : m_Scene->GetDescendants(player))
            entities.push_back(e);
        for (Entity e : entities)
        {
            std::string path = AnimationTargets::MakePath(player, e).value_or(std::string());
            std::string label = path.empty() ? e.GetName() + "  (o'zi)" : path;
            if (ImGui::BeginMenu(std::format("{}##{}", label, (uint64_t)e.GetUUID()).c_str()))
            {
                auto candidates = AnimationTargets::GetCandidates(e);
                if (candidates.empty())
                    ImGui::TextDisabled("Animatsiya qilinadigan narsa yo'q");
                for (const auto& c : candidates)
                {
                    std::string name = PropertyAnimation::GetPropertyName(c.Property);
                    if (c.Property == AnimatedProperty::ScriptFloat)
                        name += ": " + c.Field;
                    const bool exists = m_Work.FindTrack(path, c.Property, c.Field) != nullptr;
                    if (ImGui::MenuItem(name.c_str(), nullptr, false, !exists))
                        AddTrack(path, c.Property, c.Field);
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    void AnimationTimelinePanel::DrawTimeline()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < s_ListWidth + 60.0f || size.y < 60.0f)
            return;
        const ImRect list(origin, ImVec2(origin.x + s_ListWidth, origin.y + size.y));
        const ImRect canvas(ImVec2(origin.x + s_ListWidth, origin.y), ImVec2(origin.x + size.x, origin.y + size.y));
        const float rulerBottom = canvas.Min.y + s_RulerHeight;
        const float eventsBottom = rulerBottom + s_EventsHeight;
        const int trackCount = (int)m_Work.Tracks.size();
        // The rows scroll (the wheel over the track list) when they do not fit.
        const float rowsHeight = (trackCount + 1) * s_RowHeight + 6.0f;
        const float visibleRows = canvas.Max.y - eventsBottom;
        m_ScrollY = std::clamp(m_ScrollY, 0.0f, std::max(0.0f, rowsHeight - visibleRows));
        const float bodyTop = eventsBottom - (m_CurveView ? 0.0f : m_ScrollY);
        const float listTop = eventsBottom - m_ScrollY;

        if (m_FitRequested)
        {
            const float span = std::max(std::max(m_Work.Duration, m_Work.GetLastKeyTime()), 0.1f);
            m_PixelsPerSecond = std::clamp((canvas.GetWidth() - 50.0f) / span, 4.0f, 5000.0f);
            m_ViewStart = -20.0f / m_PixelsPerSecond;
            m_FitRequested = false;
        }
        auto timeToX = [&](float t) { return canvas.Min.x + (t - m_ViewStart) * m_PixelsPerSecond; };
        auto xToTime = [&](float x) { return m_ViewStart + (x - canvas.Min.x) / m_PixelsPerSecond; };

        // ---- curve view: the value range of the selected track (fixed while dragging)
        const AnimationTrack* curveTrack = m_SelectedTrack >= 0 && m_SelectedTrack < trackCount ? &m_Work.Tracks[(size_t)m_SelectedTrack] : nullptr;
        const float curveTop = eventsBottom + 8.0f, curveBottom = canvas.Max.y - 8.0f;
        if (m_CurveView && curveTrack && m_Drag != DragMode::CurveKey && m_Drag != DragMode::TangentIn && m_Drag != DragMode::TangentOut)
        {
            float lo = FLT_MAX, hi = -FLT_MAX;
            const int count = curveTrack->GetComponentCount();
            for (const AnimationKey& k : curveTrack->Keys)
                for (int c = 0; c < count; c++)
                {
                    lo = std::min(lo, k.Value[c]);
                    hi = std::max(hi, k.Value[c]);
                }
            const float end = std::max(m_Work.Duration, curveTrack->Keys.empty() ? 0.0f : curveTrack->Keys.back().Time);
            for (int i = 0; i <= 64; i++)
            {
                glm::vec4 v = curveTrack->Evaluate(end * i / 64.0f);
                for (int c = 0; c < count; c++)
                {
                    lo = std::min(lo, v[c]);
                    hi = std::max(hi, v[c]);
                }
            }
            if (lo > hi) { lo = -1.0f; hi = 1.0f; }
            if (hi - lo < 1e-3f) { lo -= 1.0f; hi += 1.0f; }
            const float pad = (hi - lo) * 0.12f;
            m_ValueMin = lo - pad;
            m_ValueMax = hi + pad;
        }
        auto valueToY = [&](float v) { return curveBottom - (v - m_ValueMin) / (m_ValueMax - m_ValueMin) * (curveBottom - curveTop); };
        auto yToValue = [&](float y) { return m_ValueMin + (curveBottom - y) / (curveBottom - curveTop) * (m_ValueMax - m_ValueMin); };

        // ---- background
        dl->AddRectFilled(list.Min, list.Max, ImGui::GetColorU32(ImGuiCol_ChildBg, 1.0f));
        dl->AddRectFilled(canvas.Min, canvas.Max, IM_COL32(26, 28, 33, 255));
        // outside the clip: darker
        const float x0 = timeToX(0.0f), xEnd = timeToX(m_Work.Duration);
        if (x0 > canvas.Min.x)
            dl->AddRectFilled(ImVec2(canvas.Min.x, rulerBottom), ImVec2(std::min(x0, canvas.Max.x), canvas.Max.y), IM_COL32(16, 17, 20, 255));
        if (xEnd < canvas.Max.x)
            dl->AddRectFilled(ImVec2(std::max(xEnd, canvas.Min.x), rulerBottom), ImVec2(canvas.Max.x, canvas.Max.y), IM_COL32(16, 17, 20, 255));
        dl->AddRectFilled(canvas.Min, ImVec2(canvas.Max.x, rulerBottom), IM_COL32(36, 38, 44, 255));
        dl->AddRectFilled(ImVec2(canvas.Min.x, rulerBottom), ImVec2(canvas.Max.x, eventsBottom), IM_COL32(31, 33, 38, 255));

        // ---- ruler: seconds with labels, frames as small ticks
        dl->PushClipRect(canvas.Min, canvas.Max, true);
        {
            static const float steps[] = { 0.01f, 0.02f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f };
            float major = steps[0];
            for (float s : steps)
            {
                major = s;
                if (s * m_PixelsPerSecond >= 70.0f)
                    break;
            }
            const float frame = FrameDuration();
            if (frame * m_PixelsPerSecond >= 5.0f)
            {
                float t0 = std::floor(xToTime(canvas.Min.x) / frame) * frame;
                for (float t = t0; timeToX(t) < canvas.Max.x; t += frame)
                    if (t >= 0.0f)
                        dl->AddLine(ImVec2(timeToX(t), rulerBottom - 4.0f), ImVec2(timeToX(t), rulerBottom), IM_COL32(110, 115, 125, 255));
            }
            float t0 = std::floor(xToTime(canvas.Min.x) / major) * major;
            for (float t = t0; timeToX(t) < canvas.Max.x; t += major)
            {
                if (t < -1e-4f)
                    continue;
                const float x = timeToX(t);
                dl->AddLine(ImVec2(x, canvas.Min.y + 12.0f), ImVec2(x, rulerBottom), IM_COL32(150, 155, 165, 255));
                dl->AddLine(ImVec2(x, rulerBottom), ImVec2(x, canvas.Max.y), IM_COL32(255, 255, 255, 14));
                std::string label = major < 1.0f ? std::format("{:.2f}", t) : std::format("{:.0f}s", t);
                dl->AddText(ImVec2(x + 3.0f, canvas.Min.y + 1.0f), IM_COL32(170, 175, 185, 255), label.c_str());
            }
            // the clip's end
            dl->AddLine(ImVec2(xEnd, rulerBottom), ImVec2(xEnd, canvas.Max.y), IM_COL32(230, 170, 70, 120), 1.5f);
        }

        // ---- events
        const float eventY = (rulerBottom + eventsBottom) * 0.5f;
        for (int i = 0; i < (int)m_Work.Events.size(); i++)
        {
            const AnimationEvent& ev = m_Work.Events[(size_t)i];
            const float x = timeToX(ev.Time);
            const bool selected = i == m_SelectedEvent;
            ImVec2 pts[5] = { { x - 4.0f, eventY - 7.0f }, { x + 4.0f, eventY - 7.0f }, { x + 4.0f, eventY + 2.0f }, { x, eventY + 7.0f }, { x - 4.0f, eventY + 2.0f } };
            dl->AddConvexPolyFilled(pts, 5, selected ? IM_COL32(255, 210, 90, 255) : IM_COL32(220, 160, 70, 255));
            dl->AddPolyline(pts, 5, IM_COL32(20, 20, 20, 255), ImDrawFlags_Closed, 1.0f);
            if (selected || m_PixelsPerSecond > 120.0f)
                dl->AddText(ImVec2(x + 7.0f, eventY - 7.0f), IM_COL32(230, 200, 140, 255), ev.Name.c_str());
        }

        // ---- rows (dope sheet) or curves
        dl->PushClipRect(ImVec2(canvas.Min.x, eventsBottom), canvas.Max, true);
        if (!m_CurveView)
        {
            for (int t = 0; t < trackCount; t++)
            {
                const float y0 = bodyTop + t * s_RowHeight, y1 = y0 + s_RowHeight;
                if (y0 > canvas.Max.y)
                    break;
                if (y1 < eventsBottom)
                    continue;
                if (t == m_SelectedTrack)
                    dl->AddRectFilled(ImVec2(canvas.Min.x, y0), ImVec2(canvas.Max.x, y1), IM_COL32(46, 188, 148, 22));
                dl->AddLine(ImVec2(canvas.Min.x, y1), ImVec2(canvas.Max.x, y1), IM_COL32(255, 255, 255, 12));
                const AnimationTrack& track = m_Work.Tracks[(size_t)t];
                // segments between keys: Step dotted, Linear / Bezier a line
                for (size_t k = 0; k + 1 < track.Keys.size(); k++)
                {
                    const float xa = timeToX(track.Keys[k].Time), xb = timeToX(track.Keys[k + 1].Time);
                    const ImU32 col = track.Keys[k].Interpolation == KeyInterpolation::Step ? IM_COL32(150, 150, 160, 80) : IM_COL32(46, 188, 148, 70);
                    dl->AddLine(ImVec2(xa, (y0 + y1) * 0.5f), ImVec2(xb, (y0 + y1) * 0.5f), col, 2.0f);
                }
                for (int k = 0; k < (int)track.Keys.size(); k++)
                {
                    const AnimationKey& key = track.Keys[(size_t)k];
                    const bool sel = IsKeySelected(t, k);
                    const ImU32 fill = sel ? IM_COL32(255, 210, 90, 255)
                                     : key.Interpolation == KeyInterpolation::Step ? IM_COL32(170, 170, 180, 255)
                                     : key.Interpolation == KeyInterpolation::Bezier ? IM_COL32(120, 200, 255, 255) : IM_COL32(200, 205, 215, 255);
                    DrawDiamond(dl, ImVec2(timeToX(key.Time), (y0 + y1) * 0.5f), sel ? 6.0f : 5.0f, fill, IM_COL32(15, 15, 18, 255));
                }
            }
        }
        else if (curveTrack)
        {
            // grid lines with values
            // value lines at least ~26 px apart (1, 2, 5 x a power of ten)
            const float range = m_ValueMax - m_ValueMin;
            const float lines = std::max(1.0f, (curveBottom - curveTop) / 26.0f);
            const float rough = std::max(range, 1e-4f) / lines;
            float step = std::pow(10.0f, std::floor(std::log10(rough)));
            if (step * 2.0f >= rough) step *= 2.0f;
            else if (step * 5.0f >= rough) step *= 5.0f;
            else step *= 10.0f;
            for (float v = std::ceil(m_ValueMin / step) * step; v <= m_ValueMax; v += step)
            {
                const float y = valueToY(v);
                dl->AddLine(ImVec2(canvas.Min.x, y), ImVec2(canvas.Max.x, y), std::abs(v) < step * 0.01f ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 14));
                dl->AddText(ImVec2(canvas.Min.x + 3.0f, y - 14.0f), IM_COL32(120, 125, 135, 255), std::format("{:.3g}", v).c_str());
            }
            const int count = curveTrack->GetComponentCount();
            for (int c = 0; c < count; c++)
            {
                ImVec2 prev;
                bool first = true;
                for (float x = canvas.Min.x; x <= canvas.Max.x + 2.0f; x += 2.0f)
                {
                    ImVec2 p(x, valueToY(curveTrack->EvaluateComponent(c, std::max(0.0f, xToTime(x)))));
                    if (!first)
                        dl->AddLine(prev, p, ComponentColor(c), 1.6f);
                    prev = p;
                    first = false;
                }
            }
            for (int k = 0; k < (int)curveTrack->Keys.size(); k++)
            {
                const AnimationKey& key = curveTrack->Keys[(size_t)k];
                const bool sel = IsKeySelected(m_SelectedTrack, k);
                const float x = timeToX(key.Time);
                for (int c = 0; c < count; c++)
                {
                    const float y = valueToY(key.Value[c]);
                    if (sel)
                    {
                        // Bezier handles: a third of the way to the neighbours.
                        const bool hasIn = k > 0 && curveTrack->Keys[(size_t)k - 1].Interpolation == KeyInterpolation::Bezier;
                        const bool hasOut = k + 1 < (int)curveTrack->Keys.size() && key.Interpolation == KeyInterpolation::Bezier;
                        if (hasIn)
                        {
                            const float dt = (key.Time - curveTrack->Keys[(size_t)k - 1].Time) / 3.0f;
                            ImVec2 h(timeToX(key.Time - dt), valueToY(key.Value[c] - key.InTangent[c] * dt));
                            dl->AddLine(ImVec2(x, y), h, IM_COL32(220, 220, 220, 150));
                            dl->AddCircleFilled(h, 4.0f, ComponentColor(c));
                        }
                        if (hasOut)
                        {
                            const float dt = (curveTrack->Keys[(size_t)k + 1].Time - key.Time) / 3.0f;
                            ImVec2 h(timeToX(key.Time + dt), valueToY(key.Value[c] + key.OutTangent[c] * dt));
                            dl->AddLine(ImVec2(x, y), h, IM_COL32(220, 220, 220, 150));
                            dl->AddCircleFilled(h, 4.0f, ComponentColor(c));
                        }
                    }
                    const float r = sel ? 4.5f : 3.5f;
                    dl->AddRectFilled(ImVec2(x - r, y - r), ImVec2(x + r, y + r), sel ? IM_COL32(255, 210, 90, 255) : ComponentColor(c));
                    dl->AddRect(ImVec2(x - r, y - r), ImVec2(x + r, y + r), IM_COL32(10, 10, 12, 255));
                }
            }
        }
        else
            dl->AddText(ImVec2(canvas.Min.x + 12.0f, eventsBottom + 10.0f), IM_COL32(150, 150, 160, 255), "Egri chiziq uchun chapdan trek tanlang");
        dl->PopClipRect();

        // ---- the playhead
        {
            const float x = timeToX(m_Time);
            dl->AddLine(ImVec2(x, canvas.Min.y), ImVec2(x, canvas.Max.y), m_Recording ? IM_COL32(255, 80, 70, 255) : IM_COL32(255, 95, 80, 220), 1.5f);
            ImVec2 tri[3] = { { x - 6.0f, canvas.Min.y }, { x + 6.0f, canvas.Min.y }, { x, canvas.Min.y + 8.0f } };
            dl->AddTriangleFilled(tri[0], tri[1], tri[2], IM_COL32(255, 95, 80, 255));
        }
        dl->PopClipRect();

        // ---- the track list (left)
        {
            dl->AddRectFilled(list.Min, ImVec2(list.Max.x, list.Min.y + s_RulerHeight), IM_COL32(36, 38, 44, 255));
            ImGui::SetCursorScreenPos(ImVec2(list.Min.x + 6.0f, list.Min.y + 4.0f));
            ImGui::TextDisabled("%.2f s   kadr %d / %d", m_Time, (int)std::round(m_Time * m_Work.FrameRate),
                                (int)std::round(m_Work.Duration * m_Work.FrameRate));
            ImGui::SetCursorScreenPos(ImVec2(list.Min.x + 6.0f, rulerBottom + 3.0f));
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.35f, 1.0f), ICON_FA_BELL "  Event'lar (%zu)", m_Work.Events.size());
            ImGui::PushClipRect(ImVec2(list.Min.x, eventsBottom), list.Max, true);
            for (int t = 0; t < trackCount; t++)
            {
                const float y0 = listTop + t * s_RowHeight;
                if (y0 > list.Max.y)
                    break;
                if (y0 + s_RowHeight < eventsBottom)
                    continue;
                const AnimationTrack& track = m_Work.Tracks[(size_t)t];
                ImGui::PushID(t);
                ImGui::SetCursorScreenPos(ImVec2(list.Min.x + 4.0f, y0 + 2.0f));
                Entity target = GetPlayer() ? AnimationTargets::Resolve(GetPlayer(), track.Path) : Entity();
                const bool missing = GetPlayer() && (!target || !AnimationTargets::Has(target, track.Property, track.Field));
                if (missing)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.5f, 0.4f, 1.0f));
                if (ImGui::Selectable(std::format("{}##track", TrackLabel(track)).c_str(), t == m_SelectedTrack, ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(s_ListWidth - 34.0f, s_RowHeight - 4.0f)))
                    m_SelectedTrack = t;
                if (missing)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                {
                    std::string tip = std::format("{}\n{}", track.Path.empty() ? std::string("(Animation Player obyektining o'zi)") : track.Path,
                                                  PropertyAnimation::PropertyToKey(track.Property));
                    if (missing)
                        tip += "\nNishon topilmadi (bola nomi o'zgarganmi?)";
                    UI::Tooltip(tip.c_str());
                }
                ImGui::SameLine(s_ListWidth - 26.0f);
                ImGui::SetCursorScreenPos(ImVec2(list.Min.x + s_ListWidth - 26.0f, y0 + 1.0f));
                if (ImGui::SmallButton(ICON_FA_XMARK))
                    RemoveTrack(t);
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(list.Min.x + 4.0f, listTop + trackCount * s_RowHeight + 4.0f));
            ImGui::BeginDisabled(!GetPlayer());
            if (ImGui::SmallButton(ICON_FA_PLUS "  Trek qo'shish"))
                m_OpenAddTrack = true;
            ImGui::EndDisabled();
            ImGui::PopClipRect();
            if (ImGui::IsMouseHoveringRect(ImVec2(list.Min.x, eventsBottom), list.Max) && ImGui::IsWindowHovered() && io.MouseWheel != 0.0f)
                m_ScrollY -= io.MouseWheel * s_RowHeight * 2.0f;
            if (rowsHeight > visibleRows)
            {
                // a thin scroll bar at the list's right edge
                const float frac = visibleRows / rowsHeight;
                const float barTop = eventsBottom + (m_ScrollY / rowsHeight) * visibleRows;
                dl->AddRectFilled(ImVec2(list.Max.x - 4.0f, barTop), ImVec2(list.Max.x - 1.0f, barTop + frac * visibleRows), IM_COL32(255, 255, 255, 60), 2.0f);
            }
            dl->AddLine(ImVec2(list.Max.x, list.Min.y), ImVec2(list.Max.x, list.Max.y), IM_COL32(0, 0, 0, 160));
        }

        // ---- mouse on the canvas
        ImGui::SetCursorScreenPos(canvas.Min);
        ImGui::InvisibleButton("##canvas", canvas.GetSize(), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                                                ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 mouse = io.MousePos;
        if (hovered && io.MouseWheel != 0.0f)
        {
            if (io.KeyShift)
                m_ViewStart -= io.MouseWheel * 60.0f / m_PixelsPerSecond;
            else
            {
                const float t = xToTime(mouse.x);
                m_PixelsPerSecond = std::clamp(m_PixelsPerSecond * std::pow(1.18f, io.MouseWheel), 4.0f, 5000.0f);
                m_ViewStart = t - (mouse.x - canvas.Min.x) / m_PixelsPerSecond;
            }
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            m_Drag = DragMode::Pan;
        if (m_Drag == DragMode::Pan)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                m_ViewStart -= io.MouseDelta.x / m_PixelsPerSecond;
            else
                m_Drag = DragMode::None;
        }

        // hit tests
        auto hitEvent = [&]() -> int {
            if (mouse.y < rulerBottom || mouse.y > eventsBottom)
                return -1;
            for (int i = (int)m_Work.Events.size() - 1; i >= 0; i--)
                if (std::abs(timeToX(m_Work.Events[(size_t)i].Time) - mouse.x) <= 6.0f)
                    return i;
            return -1;
        };
        auto hitKey = [&]() -> KeyRef {
            if (mouse.y < eventsBottom || m_CurveView)
                return {};
            const int t = (int)((mouse.y - bodyTop) / s_RowHeight);
            if (t < 0 || t >= trackCount)
                return {};
            const auto& keys = m_Work.Tracks[(size_t)t].Keys;
            for (int k = (int)keys.size() - 1; k >= 0; k--)
                if (std::abs(timeToX(keys[(size_t)k].Time) - mouse.x) <= 6.0f)
                    return { t, k };
            return {};
        };
        // curve view: a key's point (component) or a handle of the selected key
        struct CurveHit { KeyRef Key; int Component = -1; int Handle = 0; }; // handle: -1 in, +1 out
        auto hitCurve = [&]() -> CurveHit {
            CurveHit hit;
            if (!m_CurveView || !curveTrack || mouse.y < eventsBottom)
                return hit;
            const int count = curveTrack->GetComponentCount();
            const auto& keys = curveTrack->Keys;
            for (int k = 0; k < (int)keys.size(); k++)
            {
                if (!IsKeySelected(m_SelectedTrack, k))
                    continue;
                for (int c = 0; c < count; c++)
                {
                    if (k > 0 && keys[(size_t)k - 1].Interpolation == KeyInterpolation::Bezier)
                    {
                        const float dt = (keys[(size_t)k].Time - keys[(size_t)k - 1].Time) / 3.0f;
                        ImVec2 h(timeToX(keys[(size_t)k].Time - dt), valueToY(keys[(size_t)k].Value[c] - keys[(size_t)k].InTangent[c] * dt));
                        if (std::abs(h.x - mouse.x) <= 5.0f && std::abs(h.y - mouse.y) <= 5.0f)
                            return { { m_SelectedTrack, k }, c, -1 };
                    }
                    if (k + 1 < (int)keys.size() && keys[(size_t)k].Interpolation == KeyInterpolation::Bezier)
                    {
                        const float dt = (keys[(size_t)k + 1].Time - keys[(size_t)k].Time) / 3.0f;
                        ImVec2 h(timeToX(keys[(size_t)k].Time + dt), valueToY(keys[(size_t)k].Value[c] + keys[(size_t)k].OutTangent[c] * dt));
                        if (std::abs(h.x - mouse.x) <= 5.0f && std::abs(h.y - mouse.y) <= 5.0f)
                            return { { m_SelectedTrack, k }, c, 1 };
                    }
                }
            }
            for (int k = (int)keys.size() - 1; k >= 0; k--)
                for (int c = 0; c < count; c++)
                    if (std::abs(timeToX(keys[(size_t)k].Time) - mouse.x) <= 6.0f && std::abs(valueToY(keys[(size_t)k].Value[c]) - mouse.y) <= 6.0f)
                        return { { m_SelectedTrack, k }, c, 0 };
            return hit;
        };

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_DragMoved = false;
            m_DragStartTime = xToTime(mouse.x);
            if (mouse.y < rulerBottom)
            {
                m_Drag = DragMode::Scrub;
                m_Playing = false;
            }
            else if (mouse.y < eventsBottom)
            {
                const int ev = hitEvent();
                m_SelectedKeys.clear();
                m_SelectedEvent = ev;
                if (ev >= 0)
                {
                    m_Drag = DragMode::Event;
                    m_DragEventTime = m_Work.Events[(size_t)ev].Time;
                }
                else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    AddEvent(xToTime(mouse.x));
            }
            else if (!m_CurveView)
            {
                KeyRef key = hitKey();
                m_SelectedEvent = -1;
                if (key.Track >= 0)
                {
                    if (io.KeyCtrl)
                    {
                        auto it = std::find(m_SelectedKeys.begin(), m_SelectedKeys.end(), key);
                        if (it != m_SelectedKeys.end()) m_SelectedKeys.erase(it);
                        else m_SelectedKeys.push_back(key);
                    }
                    else if (io.KeyShift)
                    {
                        if (!IsKeySelected(key.Track, key.Key))
                            m_SelectedKeys.push_back(key);
                    }
                    else if (!IsKeySelected(key.Track, key.Key))
                        m_SelectedKeys = { key };
                    m_SelectedTrack = key.Track;
                    m_Drag = DragMode::Keys;
                    m_DragKeyTimes.clear();
                    for (const KeyRef& k : m_SelectedKeys)
                        m_DragKeyTimes.push_back(m_Work.Tracks[(size_t)k.Track].Keys[(size_t)k.Key].Time);
                }
                else
                {
                    const int t = (int)((mouse.y - bodyTop) / s_RowHeight);
                    if (t >= 0 && t < trackCount)
                        m_SelectedTrack = t;
                    if (!io.KeyCtrl)
                        m_SelectedKeys.clear();
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && t >= 0 && t < trackCount)
                        AddKeys(t, xToTime(mouse.x));
                }
            }
            else
            {
                CurveHit hit = hitCurve();
                m_SelectedEvent = -1;
                if (hit.Key.Track >= 0 && hit.Handle != 0)
                {
                    m_Drag = hit.Handle < 0 ? DragMode::TangentIn : DragMode::TangentOut;
                    m_DragComponent = hit.Component;
                    m_SelectedKeys = { hit.Key };
                }
                else if (hit.Key.Track >= 0)
                {
                    m_SelectedKeys = { hit.Key };
                    m_Drag = DragMode::CurveKey;
                    m_DragComponent = hit.Component;
                    m_DragKeyTimes = { curveTrack->Keys[(size_t)hit.Key.Key].Time };
                }
                else
                {
                    m_SelectedKeys.clear();
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && curveTrack)
                        AddKeys(m_SelectedTrack, xToTime(mouse.x));
                }
            }
        }

        // dragging
        if (m_Drag != DragMode::None && m_Drag != DragMode::Pan && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (m_DragMoved)
                EndEdit();
            m_Drag = DragMode::None;
        }
        const float dragDelta = xToTime(mouse.x) - m_DragStartTime;
        const bool moved = std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x) > 2.0f || std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y) > 2.0f;
        switch (m_Drag)
        {
            case DragMode::Scrub:
                SetTime(Snap(xToTime(mouse.x)));
                break;
            case DragMode::Keys:
                if (moved && !m_SelectedKeys.empty())
                {
                    if (!m_DragMoved) { BeginEdit(); m_DragMoved = true; }
                    for (size_t i = 0; i < m_SelectedKeys.size() && i < m_DragKeyTimes.size(); i++)
                    {
                        KeyRef& k = m_SelectedKeys[i];
                        m_Work.Tracks[(size_t)k.Track].Keys[(size_t)k.Key].Time = Snap(m_DragKeyTimes[i] + dragDelta);
                    }
                    // re-sort the touched tracks; the drag times follow their keys
                    std::vector<std::pair<KeyRef, float>> before;
                    for (size_t i = 0; i < m_SelectedKeys.size() && i < m_DragKeyTimes.size(); i++)
                        before.push_back({ m_SelectedKeys[i], m_DragKeyTimes[i] });
                    std::vector<int> tracks;
                    for (const KeyRef& k : m_SelectedKeys)
                        if (std::find(tracks.begin(), tracks.end(), k.Track) == tracks.end())
                            tracks.push_back(k.Track);
                    for (int t : tracks)
                        SortTrackKeepSelection(t);
                    (void)before;
                    Commit();
                }
                break;
            case DragMode::Event:
                if (moved && m_SelectedEvent >= 0 && m_SelectedEvent < (int)m_Work.Events.size())
                {
                    if (!m_DragMoved) { BeginEdit(); m_DragMoved = true; }
                    m_Work.Events[(size_t)m_SelectedEvent].Time = Snap(m_DragEventTime + dragDelta);
                    SortEventsKeepSelection();
                    Commit();
                }
                break;
            case DragMode::CurveKey:
                if (moved && m_SelectedKeys.size() == 1 && !m_DragKeyTimes.empty())
                {
                    if (!m_DragMoved) { BeginEdit(); m_DragMoved = true; }
                    KeyRef& k = m_SelectedKeys[0];
                    AnimationKey& key = m_Work.Tracks[(size_t)k.Track].Keys[(size_t)k.Key];
                    if (!io.KeyShift) // Shift: only the value
                        key.Time = Snap(m_DragKeyTimes[0] + dragDelta);
                    key.Value[m_DragComponent] = yToValue(mouse.y);
                    SortTrackKeepSelection(k.Track);
                    Commit();
                }
                break;
            case DragMode::TangentIn:
            case DragMode::TangentOut:
                if (m_SelectedKeys.size() == 1)
                {
                    if (!m_DragMoved) { BeginEdit(); m_DragMoved = true; }
                    KeyRef& k = m_SelectedKeys[0];
                    AnimationKey& key = m_Work.Tracks[(size_t)k.Track].Keys[(size_t)k.Key];
                    float dt = xToTime(mouse.x) - key.Time;
                    if (m_Drag == DragMode::TangentIn) dt = std::min(dt, -1e-3f);
                    else dt = std::max(dt, 1e-3f);
                    const float slope = (yToValue(mouse.y) - key.Value[m_DragComponent]) / dt;
                    key.TangentMode = KeyTangentMode::Free;
                    key.InTangent[m_DragComponent] = key.OutTangent[m_DragComponent] = slope; // a smooth key: both sides
                    Commit();
                }
                break;
            default:
                break;
        }

        // context menus
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_ContextTime = Snap(xToTime(mouse.x));
            m_ContextEvent = hitEvent();
            m_ContextKey = m_CurveView ? hitCurve().Key : hitKey();
            m_ContextTrack = m_CurveView ? m_SelectedTrack : (mouse.y >= eventsBottom ? (int)((mouse.y - bodyTop) / s_RowHeight) : -1);
            if (m_ContextTrack >= trackCount)
                m_ContextTrack = -1;
            if (m_ContextKey.Track >= 0 && !IsKeySelected(m_ContextKey.Track, m_ContextKey.Key))
                m_SelectedKeys = { m_ContextKey };
            if (m_ContextEvent >= 0)
                m_SelectedEvent = m_ContextEvent;
            ImGui::OpenPopup("##timelinemenu");
        }
        if (ImGui::BeginPopup("##timelinemenu"))
        {
            if (m_ContextEvent >= 0 && m_ContextEvent < (int)m_Work.Events.size())
            {
                ImGui::TextDisabled(ICON_FA_BELL "  %s", m_Work.Events[(size_t)m_ContextEvent].Name.c_str());
                if (ImGui::MenuItem(ICON_FA_TRASH "  Event'ni o'chirish"))
                {
                    m_SelectedEvent = m_ContextEvent;
                    DeleteSelection();
                }
            }
            else if (!m_SelectedKeys.empty() && m_ContextKey.Track >= 0)
            {
                ImGui::TextDisabled(ICON_FA_DIAMOND "  %zu ta kalit", m_SelectedKeys.size());
                auto setAll = [&](auto&& f) {
                    BeginEdit();
                    for (const KeyRef& k : m_SelectedKeys)
                        f(m_Work.Tracks[(size_t)k.Track].Keys[(size_t)k.Key]);
                    for (auto& t : m_Work.Tracks)
                        t.UpdateTangents();
                    Commit();
                    EndEdit();
                };
                if (ImGui::MenuItem("Step (sakrash)")) setAll([](AnimationKey& k) { k.Interpolation = KeyInterpolation::Step; });
                if (ImGui::MenuItem("Linear (to'g'ri)")) setAll([](AnimationKey& k) { k.Interpolation = KeyInterpolation::Linear; });
                if (ImGui::MenuItem("Bezier (silliq)")) setAll([](AnimationKey& k) { k.Interpolation = KeyInterpolation::Bezier; });
                ImGui::Separator();
                if (ImGui::MenuItem("Tangent: avtomatik")) setAll([](AnimationKey& k) { k.TangentMode = KeyTangentMode::Auto; });
                if (ImGui::MenuItem("Tangent: tekis (flat)")) setAll([](AnimationKey& k) { k.TangentMode = KeyTangentMode::Flat; });
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_TRASH "  O'chirish", "Delete"))
                    DeleteSelection();
            }
            else
            {
                ImGui::BeginDisabled(m_ContextTrack < 0);
                if (ImGui::MenuItem(ICON_FA_KEY "  Shu yerga kalit"))
                    AddKeys(m_ContextTrack, m_ContextTime);
                ImGui::EndDisabled();
                if (ImGui::MenuItem(ICON_FA_BELL "  Shu yerga event"))
                    AddEvent(m_ContextTime);
                if (ImGui::MenuItem(ICON_FA_FLAG "  Klip shu yerda tugaydi"))
                {
                    BeginEdit();
                    m_Work.Duration = m_ContextTime;
                    Commit();
                    EndEdit();
                }
            }
            ImGui::EndPopup();
        }
    }

    void AnimationTimelinePanel::DrawClipSettings()
    {
        if (!UI::BeginPropertyGrid("##clipsettings", 92.0f))
            return;
        float duration = m_Work.Duration;
        UI::PropertyLabel("Davomiylik", "Klip davomiyligi (soniya). Kalitlar undan keyin bo'lsa ham klip shu yerda tugaydi");
        if (ImGui::DragFloat("##duration", &duration, 0.01f, 0.0f, 600.0f, "%.2f s"))
        {
            BeginEdit();
            m_Work.Duration = std::max(0.0f, duration);
            Commit();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            EndEdit();
        float fps = m_Work.FrameRate;
        UI::PropertyLabel("Kadrlar", "Timeline kadrlari soniyasiga (yopishish va kadr raqamlari uchun); ijro baribir silliq");
        if (ImGui::DragFloat("##fps", &fps, 0.2f, 1.0f, 240.0f, "%.0f fps"))
        {
            BeginEdit();
            m_Work.FrameRate = std::clamp(std::round(fps), 1.0f, 240.0f);
            Commit();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            EndEdit();
        static const char* wraps[] = { "Bir marta", "Takror", "Borib-qaytish" };
        int wrap = (int)m_Work.Wrap;
        UI::PropertyLabel("Tugaganda", "Bir marta: oxirgi qiymatda to'xtaydi; Takror: boshidan; Borib-qaytish: orqaga va yana oldinga");
        if (ImGui::Combo("##wrap", &wrap, wraps, 3))
        {
            BeginEdit();
            m_Work.Wrap = (AnimationWrap)wrap;
            Commit();
            EndEdit();
        }
        UI::PropertyLabel("");
        ImGui::BeginDisabled(m_Work.Tracks.empty());
        if (ImGui::SmallButton("Oxirgi kalitgacha"))
        {
            BeginEdit();
            m_Work.Duration = m_Work.GetLastKeyTime();
            Commit();
            EndEdit();
        }
        ImGui::EndDisabled();
        UI::EndPropertyGrid();
    }

    void AnimationTimelinePanel::DrawDetails()
    {
        // ---- an event
        if (m_SelectedEvent >= 0 && m_SelectedEvent < (int)m_Work.Events.size())
        {
            AnimationEvent& ev = m_Work.Events[(size_t)m_SelectedEvent];
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.35f, 1.0f), ICON_FA_BELL "  Event");
            ImGui::Separator();
            if (UI::BeginPropertyGrid("##event", 70.0f))
            {
                AnimationEvent edited = ev;
                bool changed = false;
                changed |= UI::PropertyFloat("Vaqt (s)", edited.Time, 0.01f, 0.0f, 600.0f, "%.3f");
                bool timeDone = ImGui::IsItemDeactivatedAfterEdit();
                changed |= UI::PropertyString("Nom", edited.Name);
                bool nameDone = ImGui::IsItemDeactivatedAfterEdit();
                changed |= UI::PropertyString("Parametr", edited.Parameter);
                bool paramDone = ImGui::IsItemDeactivatedAfterEdit();
                if (changed)
                {
                    BeginEdit();
                    ev = edited;
                    SortEventsKeepSelection();
                    Commit();
                }
                if (timeDone || nameDone || paramDone)
                    EndEdit();
                UI::EndPropertyGrid();
            }
            ImGui::TextWrapped("Skript: OnAnimationEvent(e) - e.Name, e.Parameter; yoki AddAnimationEventHandler(\"%s\", ...)", ev.Name.c_str());
            if (ImGui::Button(ICON_FA_TRASH "  O'chirish"))
                DeleteSelection();
            return;
        }
        // ---- keys
        if (m_SelectedKeys.size() == 1)
        {
            const KeyRef ref = m_SelectedKeys[0];
            if (ref.Track < 0 || ref.Track >= (int)m_Work.Tracks.size() || ref.Key < 0 || ref.Key >= (int)m_Work.Tracks[(size_t)ref.Track].Keys.size())
            {
                m_SelectedKeys.clear();
                return;
            }
            AnimationTrack& track = m_Work.Tracks[(size_t)ref.Track];
            ImGui::TextColored(UI::AccentColor(), ICON_FA_DIAMOND "  %s", TrackLabel(track).c_str());
            ImGui::Separator();
            if (UI::BeginPropertyGrid("##key", 76.0f))
            {
                AnimationKey key = track.Keys[(size_t)ref.Key];
                bool changed = false, done = false;
                changed |= UI::PropertyFloat("Vaqt (s)", key.Time, 0.01f, 0.0f, 600.0f, "%.3f");
                done |= ImGui::IsItemDeactivatedAfterEdit();
                const int count = track.GetComponentCount();
                if (PropertyAnimation::IsColor(track.Property))
                {
                    UI::PropertyLabel("Rang");
                    const ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoInputs |
                                                      ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
                    if (count == 4)
                        changed |= ImGui::ColorEdit4("##color", &key.Value.x, flags);
                    else
                        changed |= ImGui::ColorEdit3("##color", &key.Value.x, flags);
                    done |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SameLine();
                    if (count == 4)
                        ImGui::TextDisabled("%.2f %.2f %.2f %.2f", key.Value.r, key.Value.g, key.Value.b, key.Value.a);
                    else
                        ImGui::TextDisabled("%.2f %.2f %.2f", key.Value.r, key.Value.g, key.Value.b);
                }
                else
                    for (int c = 0; c < count; c++)
                    {
                        ImGui::PushID(c);
                        std::string label = count == 1 ? std::string("Qiymat") : std::string("Qiymat ") + ComponentName(track.Property, c);
                        UI::PropertyLabel(label.c_str());
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ComponentColor(c)));
                        changed |= ImGui::DragFloat("##v", &key.Value[c], track.Property == AnimatedProperty::Rotation ? 0.5f : 0.01f);
                        ImGui::PopStyleColor();
                        done |= ImGui::IsItemDeactivatedAfterEdit();
                        ImGui::PopID();
                    }
                {
                    const char* items[] = { "Step (sakrash)", "Linear (to'g'ri)", "Bezier (silliq)" };
                    int interp = (int)key.Interpolation;
                    if (UI::PropertyCombo("Keyingisiga", interp, items, 3))
                    {
                        key.Interpolation = (KeyInterpolation)interp;
                        changed = done = true;
                    }
                }
                {
                    const char* items[] = { "Avtomatik", "Tekis (flat)", "Erkin (qo'lda)" };
                    int mode = (int)key.TangentMode;
                    if (UI::PropertyCombo("Tangent", mode, items, 3))
                    {
                        key.TangentMode = (KeyTangentMode)mode;
                        changed = done = true;
                    }
                }
                if (key.TangentMode == KeyTangentMode::Free)
                    for (int c = 0; c < count; c++)
                    {
                        ImGui::PushID(100 + c);
                        UI::PropertyLabel((std::string("Qiyalik ") + ComponentName(track.Property, c)).c_str(),
                                          "Kirish va chiqish qiyaligi (qiymat / soniya)");
                        float both[2] = { key.InTangent[c], key.OutTangent[c] };
                        if (ImGui::DragFloat2("##t", both, 0.02f))
                        {
                            key.InTangent[c] = both[0];
                            key.OutTangent[c] = both[1];
                            changed = true;
                        }
                        done |= ImGui::IsItemDeactivatedAfterEdit();
                        ImGui::PopID();
                    }
                if (changed)
                {
                    BeginEdit();
                    key.Time = Snap(key.Time);
                    track.Keys[(size_t)ref.Key] = key;
                    SortTrackKeepSelection(ref.Track);
                    Commit();
                }
                if (done)
                    EndEdit();
                UI::EndPropertyGrid();
            }
            if (ImGui::Button(ICON_FA_TRASH "  Kalitni o'chirish"))
                DeleteSelection();
            return;
        }
        if (m_SelectedKeys.size() > 1)
        {
            ImGui::TextColored(UI::AccentColor(), ICON_FA_DIAMOND "  %zu ta kalit tanlangan", m_SelectedKeys.size());
            ImGui::Separator();
            ImGui::TextWrapped("Sudrab vaqtini o'zgartiring; o'ng tugma - interpolyatsiya; Delete - o'chirish");
            if (ImGui::Button(ICON_FA_TRASH "  O'chirish"))
                DeleteSelection();
            return;
        }
        // ---- the clip, the selected track, then help
        ImGui::TextColored(UI::AccentColor(), ICON_FA_FILM "  Klip: %s", m_Asset ? m_Asset->GetName().c_str() : "");
        DrawClipSettings();
        ImGui::Separator();
        if (m_SelectedTrack >= 0 && m_SelectedTrack < (int)m_Work.Tracks.size())
        {
            const AnimationTrack& track = m_Work.Tracks[(size_t)m_SelectedTrack];
            ImGui::TextColored(UI::AccentColor(), "%s", TrackLabel(track).c_str());
            ImGui::TextDisabled("%s", track.Path.empty() ? "(Animation Player obyektining o'zi)" : track.Path.c_str());
            ImGui::Separator();
            const glm::vec4 v = track.Evaluate(m_Time);
            const int count = track.GetComponentCount();
            std::string value;
            for (int c = 0; c < count; c++)
                value += std::format("{}{} {:.3f}", c ? "  " : "", ComponentName(track.Property, c), v[c]);
            ImGui::Text("%zu kalit", track.Keys.size());
            ImGui::TextWrapped("%.2f s da: %s", m_Time, value.c_str());
            if (ImGui::Button(ICON_FA_KEY "  Joriy vaqtda kalit (K)"))
                AddKeys(m_SelectedTrack, m_Time);
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.63f, 0.68f, 1.0f));
        ImGui::TextWrapped("Chizg'ich: vaqtni surish. Ikki marta bosish: kalit / event. Sudrash: kalitni ko'chirish. "
                           "G'ildirak: masshtab, Shift+g'ildirak / o'rta tugma: surish. K: kalit, Probel: o'ynatish, "
                           "Delete: o'chirish, Ctrl+Z / Ctrl+Y: bekor qilish / qaytarish.");
        ImGui::PopStyleColor();
    }

    void AnimationTimelinePanel::HandleShortcuts()
    {
        if (!m_Focused || ImGui::GetIO().WantTextInput)
            return;
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && m_Editable)
        {
            m_Playing = !m_Playing;
            m_PlayCursor = {};
            m_PlayCursor.Time = m_Time >= m_Work.Duration ? 0.0f : m_Time;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            DeleteSelection();
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false))
            AddKeys(m_SelectedTrack, m_Time);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            m_FitRequested = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) { m_Playing = false; SetTime(0.0f); }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) { m_Playing = false; SetTime(m_Work.Duration); }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { m_Playing = false; SetTime(Snap(m_Time - FrameDuration())); }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { m_Playing = false; SetTime(Snap(m_Time + FrameDuration())); }
    }

}
