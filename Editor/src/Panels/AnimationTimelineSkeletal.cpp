// The Animation window's skeletal mode: an Animator's glTF clips. Scrubbing or playing one shows
// it in the scene view without play mode (AnimatorComponent::EditorPreview); the bones and blend
// shapes the clip moves are rows with their keys; the Animator's events of the clip are edited on
// the timeline (added, dragged in time, renamed, removed) - footsteps placed where the feet land.
#include "AnimationTimelinePanel.h"
#include "../UI.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>

namespace Zamin {

    static constexpr float s_SkelListWidth = 220.0f;
    static constexpr float s_SkelRulerHeight = 24.0f;
    static constexpr float s_SkelEventsHeight = 24.0f;
    static constexpr float s_SkelRowHeight = 20.0f;

    // A row of the skeletal dope sheet: a joint the clip moves, or a morph group it animates.
    struct SkeletalRow
    {
        std::string Label;
        int Depth = 0;
        int Joint = -1;
        int MorphGroup = -1;
        std::vector<float> Times;   // its keys (all channels), sorted, unique
        uint8_t Paths = 0;          // 1 translation, 2 rotation, 4 scale, 8 weights
    };

    static std::vector<SkeletalRow> BuildRows(const Model& model, const AnimationClip& clip)
    {
        const Skeleton& skeleton = model.GetSkeleton();
        std::vector<SkeletalRow> rows;
        std::vector<int> depth(skeleton.Joints.size(), 0);
        for (size_t i = 0; i < skeleton.Joints.size(); i++)
            if (skeleton.Joints[i].Parent >= 0 && skeleton.Joints[i].Parent < (int)i)
                depth[i] = depth[(size_t)skeleton.Joints[i].Parent] + 1;
        // joints in skeleton order (parents first), only those the clip moves
        for (size_t j = 0; j < skeleton.Joints.size(); j++)
        {
            SkeletalRow row;
            for (const AnimationChannel& ch : clip.Channels)
            {
                if (ch.JointIndex != (int)j)
                    continue;
                row.Times.insert(row.Times.end(), ch.Times.begin(), ch.Times.end());
                row.Paths |= ch.Target == AnimationChannel::Path::Translation ? 1 : ch.Target == AnimationChannel::Path::Rotation ? 2 : 4;
            }
            if (row.Paths == 0)
                continue;
            row.Label = skeleton.Joints[j].Name;
            row.Depth = depth[j];
            row.Joint = (int)j;
            rows.push_back(std::move(row));
        }
        for (const MorphWeightChannel& ch : clip.MorphChannels)
        {
            if (ch.Group < 0 || ch.Group >= (int)model.GetMorphGroups().size())
                continue;
            SkeletalRow row;
            row.Label = ICON_FA_FACE_SMILE " " + model.GetMorphGroups()[(size_t)ch.Group].Name;
            row.MorphGroup = ch.Group;
            row.Times = ch.Times;
            row.Paths = 8;
            rows.push_back(std::move(row));
        }
        for (SkeletalRow& row : rows)
        {
            std::sort(row.Times.begin(), row.Times.end());
            row.Times.erase(std::unique(row.Times.begin(), row.Times.end(), [](float a, float b) { return std::abs(a - b) < 1e-5f; }),
                            row.Times.end());
        }
        return rows;
    }

    // ------------------------------------------------------------------ target
    Entity AnimationTimelinePanel::GetAnimatorEntity() const
    {
        if (!m_Scene || !m_AnimatorID)
            return {};
        Entity e = m_Scene->GetEntityByUUID(m_AnimatorID);
        return e && e.HasComponent<AnimatorComponent>() ? e : Entity();
    }

    const Model* AnimationTimelinePanel::GetAnimatorModel() const
    {
        Entity e = GetAnimatorEntity();
        const auto* mr = e ? e.TryGetComponent<MeshRendererComponent>() : nullptr;
        return mr && mr->ModelAsset && !mr->ModelAsset->GetAnimations().empty() ? mr->ModelAsset.get() : nullptr;
    }

    const AnimationClip* AnimationTimelinePanel::GetSkeletalClip() const
    {
        const Model* model = GetAnimatorModel();
        if (!model)
            return nullptr;
        const int index = model->FindAnimation(m_SkelClip);
        return index >= 0 ? &model->GetAnimations()[(size_t)index] : nullptr;
    }

    float AnimationTimelinePanel::SkeletalFrameRate() const
    {
        const Model* model = GetAnimatorModel();
        return model ? std::max(1.0f, model->GetImportSettings().ClipFrameRate) : 30.0f;
    }

    void AnimationTimelinePanel::OpenAnimator(Entity entity, const std::string& clip)
    {
        if (!entity || !entity.HasComponent<AnimatorComponent>())
            return;
        if (m_Mode == Mode::Property)
            StopPreview(true);
        if (entity.GetUUID() != m_AnimatorID)
        {
            StopSkeletalPreview();
            m_SkelTime = 0.0f;
            m_SkelSelectedEvent = m_SkelSelectedRow = -1;
            m_SkelFit = true;
        }
        m_Mode = Mode::Skeletal;
        m_AnimatorID = entity.GetUUID();
        const auto& anim = entity.GetComponent<AnimatorComponent>();
        const Model* model = GetAnimatorModel();
        std::string pick = clip;
        if (pick.empty() && model)
        {
            // what it plays: its clip, its default state's clip, else the first
            pick = anim.Clip;
            if (anim.UsesStateMachine())
            {
                const AnimatorState& st = anim.States[(size_t)std::clamp(anim.DefaultState, 0, (int)anim.States.size() - 1)];
                pick = st.IsBlendTree() ? (st.Motions.empty() ? std::string() : st.Motions.back().Clip) : st.Clip;
            }
            if (model->FindAnimation(pick) < 0)
                pick = model->GetAnimations().front().Name;
        }
        if (!pick.empty())
            SetSkeletalClip(pick);
    }

    void AnimationTimelinePanel::SetSkeletalClip(const std::string& clip)
    {
        if (clip != m_SkelClip)
        {
            m_SkelSelectedEvent = m_SkelSelectedRow = -1;
            m_SkelFit = true;
        }
        m_SkelClip = clip;
        if (const AnimationClip* c = GetSkeletalClip())
            m_SkelTime = std::clamp(m_SkelTime, 0.0f, c->Duration);
    }

    void AnimationTimelinePanel::SetSkeletalTime(float seconds)
    {
        const AnimationClip* clip = GetSkeletalClip();
        const float duration = clip ? clip->Duration : 0.0f;
        m_SkelTime = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, duration);
        if (m_Editable)
            m_SkelPreview = true; // scrubbing shows the pose in the scene view
    }

    void AnimationTimelinePanel::StopSkeletalPreview()
    {
        if (m_Scene && m_AnimatorID)
            if (Entity e = m_Scene->GetEntityByUUID(m_AnimatorID))
                if (auto* anim = e.TryGetComponent<AnimatorComponent>())
                    anim->EditorPreview = {};
        m_SkelPreview = false;
        m_SkelPlaying = false;
    }

    void AnimationTimelinePanel::UpdateSkeletal(Timestep ts, bool visible)
    {
        if (m_Mode != Mode::Skeletal || !visible || !m_Scene)
        {
            if (m_SkelPreview)
                StopSkeletalPreview();
            return;
        }
        Entity e = GetAnimatorEntity();
        const AnimationClip* clip = GetSkeletalClip();
        if (!e || !clip)
        {
            if (m_SkelPreview)
                StopSkeletalPreview();
            if (m_Scene && m_AnimatorID && !m_Scene->GetEntityByUUID(m_AnimatorID))
                m_AnimatorID = 0; // deleted
            return;
        }
        auto& anim = e.GetComponent<AnimatorComponent>();
        if (!m_Editable)
        {
            // The game runs: the window follows the Animator while it plays this clip.
            anim.EditorPreview = {};
            m_SkelPreview = m_SkelPlaying = false;
            if (anim.BlendTree < 0 && (anim.Clip == m_SkelClip || (anim.Clip.empty() && GetAnimatorModel()->GetAnimations().front().Name == m_SkelClip)))
                m_SkelTime = clip->Duration > 0.0f ? (anim.Loop ? std::fmod(std::fmod(anim.Time, clip->Duration) + clip->Duration, clip->Duration)
                                                                : std::clamp(anim.Time, 0.0f, clip->Duration))
                                                   : 0.0f;
            return;
        }
        if (m_SkelPlaying)
        {
            m_SkelPreview = true;
            m_SkelTime += (float)ts * m_SkelSpeed;
            const float d = clip->Duration;
            if (d <= 0.0f)
                m_SkelTime = 0.0f;
            else if (m_SkelLoop)
                m_SkelTime = std::fmod(std::fmod(m_SkelTime, d) + d, d);
            else if (m_SkelTime >= d || m_SkelTime <= 0.0f)
            {
                m_SkelTime = std::clamp(m_SkelTime, 0.0f, d);
                m_SkelPlaying = false;
            }
        }
        if (m_SkelPreview)
            anim.EditorPreview = { true, m_SkelClip, m_SkelTime };
        else
            anim.EditorPreview = {};
    }

    // ------------------------------------------------------------------ events
    int AnimationTimelinePanel::AddSkeletalEvent(float time)
    {
        Entity e = GetAnimatorEntity();
        const AnimationClip* clip = GetSkeletalClip();
        if (!e || !clip || !m_Editable)
            return -1;
        auto& anim = e.GetComponent<AnimatorComponent>();
        AnimatorEvent ev;
        ev.Clip = m_SkelClip;
        ev.Time = clip->Duration > 0.0f ? std::clamp(time / clip->Duration, 0.0f, 1.0f) : 0.0f;
        // A footstep where there is a Footsteps component, else the name of the selected event.
        if (const auto* steps = e.TryGetComponent<FootstepsComponent>(); steps && !steps->EventName.empty())
            ev.Name = steps->EventName;
        else if (m_SkelSelectedEvent >= 0 && m_SkelSelectedEvent < (int)anim.Events.size())
            ev.Name = anim.Events[(size_t)m_SkelSelectedEvent].Name;
        else
            ev.Name = "Event";
        anim.Events.push_back(ev);
        m_SkelSelectedEvent = (int)anim.Events.size() - 1;
        MarkSceneModified();
        return m_SkelSelectedEvent;
    }

    void AnimationTimelinePanel::DeleteSkeletalEvent(int index)
    {
        Entity e = GetAnimatorEntity();
        if (!e || !m_Editable)
            return;
        auto& events = e.GetComponent<AnimatorComponent>().Events;
        if (index < 0 || index >= (int)events.size())
            return;
        events.erase(events.begin() + index);
        m_SkelSelectedEvent = -1;
        MarkSceneModified();
    }

    // ------------------------------------------------------------------ drawing
    void AnimationTimelinePanel::DrawModeSwitch()
    {
        // The selection's (or the window's) player and Animator: one button each.
        Entity selected = m_Scene && m_SelectedID ? m_Scene->GetEntityByUUID(m_SelectedID) : Entity();
        Entity player = GetPlayer(), animator = GetAnimatorEntity();
        for (Entity e = selected; e && m_Scene; e = m_Scene->GetParent(e))
        {
            if (!player && e.HasComponent<AnimationPlayerComponent>()) player = e;
            if (!animator && e.HasComponent<AnimatorComponent>()) animator = e;
        }
        ImGui::BeginDisabled(!player);
        if (UI::ToolbarButton(ICON_FA_FILM, "Property kliplari (.zanim): Animation Player treklari", m_Mode == Mode::Property) &&
            m_Mode != Mode::Property && player)
        {
            StopSkeletalPreview();
            m_Mode = Mode::Property;
            if (player.GetUUID() != m_PlayerID)
                Open(player, 0);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!animator);
        if (UI::ToolbarButton(ICON_FA_BONE, "Skelet kliplari (glTF): Animator modelining kliplari, scrub va event'lar", m_Mode == Mode::Skeletal) &&
            m_Mode != Mode::Skeletal && animator)
            OpenAnimator(animator);
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 14.0f);
    }

    void AnimationTimelinePanel::DrawSkeletalEmptyState()
    {
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        Entity e = GetAnimatorEntity();
        if (!e)
            ImGui::TextDisabled("Animator komponenti bor obyektni tanlang (skelet kliplari uchun)");
        else if (!GetAnimatorModel())
            ImGui::TextDisabled("'%s': Mesh Renderer'da animatsiyali glTF model yo'q", e.GetName().c_str());
        else
            ImGui::TextDisabled("Klipni tanlang");
    }

    void AnimationTimelinePanel::DrawSkeletalToolbar()
    {
        DrawModeSwitch();
        Entity e = GetAnimatorEntity();
        const Model* model = GetAnimatorModel();
        ImGui::AlignTextToFramePadding();
        if (e)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_PERSON_RUNNING "  %s", e.GetName().c_str());
        else
            ImGui::TextDisabled(ICON_FA_PERSON_RUNNING "  -");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Animator obyekti: modelining glTF kliplari");
        ImGui::SameLine();
        if (UI::ToolbarButton(m_Pinned ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN, m_Pinned ? "Qulflangan: tanlov o'zgarsa ham shu obyekt qoladi"
                                                                                    : "Tanlangan obyektning Animator'iga ergashadi", m_Pinned))
            m_Pinned = !m_Pinned;
        if (!model)
            return;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##skelclip", m_SkelClip.empty() ? "(klip)" : m_SkelClip.c_str()))
        {
            for (const AnimationClip& c : model->GetAnimations())
                if (ImGui::Selectable(std::format("{}  ({:.2f} s)##{}", c.Name, c.Duration, (const void*)&c).c_str(), c.Name == m_SkelClip))
                    SetSkeletalClip(c.Name);
            ImGui::EndCombo();
        }
        const AnimationClip* clip = GetSkeletalClip();
        if (!clip)
            return;

        // ---- transport
        ImGui::SameLine(0.0f, 14.0f);
        if (UI::ToolbarButton(ICON_FA_BACKWARD_STEP, "Boshiga (Home)")) { m_SkelPlaying = false; SetSkeletalTime(0.0f); }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Editable);
        if (UI::ToolbarButton(m_SkelPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY, m_Editable ? "Sahnada ko'rib chiqish: o'ynatish / to'xtatish (Probel)"
                                                                                    : "O'yin rejimida klipni Animator o'ynatadi", m_SkelPlaying))
        {
            m_SkelPlaying = !m_SkelPlaying;
            if (m_SkelPlaying && !m_SkelLoop && m_SkelTime >= clip->Duration)
                m_SkelTime = 0.0f;
            m_SkelPreview = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_FORWARD_STEP, "Oxiriga (End)")) { m_SkelPlaying = false; SetSkeletalTime(clip->Duration); }
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_REPEAT, "Takrorlab o'ynatish", m_SkelLoop))
            m_SkelLoop = !m_SkelLoop;
        ImGui::SameLine();
        const float fps = SkeletalFrameRate();
        ImGui::SetNextItemWidth(140.0f);
        float time = m_SkelTime;
        if (ImGui::DragFloat("##skeltime", &time, 0.01f, 0.0f, clip->Duration,
                             std::format("%.2f s  ({} kadr)", (int)std::round(m_SkelTime * fps)).c_str()))
        {
            m_SkelPlaying = false;
            SetSkeletalTime(time);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("##skelspeed", &m_SkelSpeed, 0.01f, -4.0f, 4.0f, "x%.2f");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ko'rib chiqish tezligi (manfiy: orqaga)");
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_MAGNET, "Kadrlarga yopishish", m_Snap))
            m_Snap = !m_Snap;
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_EXPAND, "Klipni oynaga sig'dirish (F)"))
            m_SkelFit = true;
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::BeginDisabled(!m_Editable);
        if (UI::ToolbarButton(ICON_FA_BELL, "Joriy vaqtda Animator event'i (skript OnAnimationEvent, Footsteps qadam)"))
            AddSkeletalEvent(m_SkelTime);
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 14.0f);
        if (!m_Editable)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), ICON_FA_CIRCLE_INFO "  O'yin: Animator vaqti");
        else if (m_SkelPreview)
        {
            ImGui::BeginDisabled(m_SkelPlaying);
            if (UI::ToolbarButton(ICON_FA_EYE, "Sahnada ko'rsatilmoqda - bosing: ko'rib chiqishni to'xtatish (Animator o'z holiga qaytadi)", true))
                StopSkeletalPreview();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(UI::AccentColor(), "Ko'rib chiqish");
        }
        else
            ImGui::TextDisabled("Vaqt chizg'ichini bosing / sudrang: sahnada ko'rinadi");
    }

    void AnimationTimelinePanel::DrawSkeletalTimeline()
    {
        Entity entity = GetAnimatorEntity();
        const Model* model = GetAnimatorModel();
        const AnimationClip* clip = GetSkeletalClip();
        if (!entity || !model || !clip)
            return;
        auto& anim = entity.GetComponent<AnimatorComponent>();
        const std::vector<SkeletalRow> rows = BuildRows(*model, *clip);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < s_SkelListWidth + 60.0f || size.y < 60.0f)
            return;
        const ImRect list(origin, ImVec2(origin.x + s_SkelListWidth, origin.y + size.y));
        const ImRect canvas(ImVec2(origin.x + s_SkelListWidth, origin.y), ImVec2(origin.x + size.x, origin.y + size.y));
        const float rulerBottom = canvas.Min.y + s_SkelRulerHeight;
        const float eventsBottom = rulerBottom + s_SkelEventsHeight;
        const float rowsHeight = rows.size() * s_SkelRowHeight + 6.0f;
        m_SkelScrollY = std::clamp(m_SkelScrollY, 0.0f, std::max(0.0f, rowsHeight - (canvas.Max.y - eventsBottom)));
        const float bodyTop = eventsBottom - m_SkelScrollY;
        const float duration = std::max(clip->Duration, 1e-4f);
        const float fps = SkeletalFrameRate();

        if (m_SkelFit)
        {
            m_SkelPixelsPerSecond = std::clamp((canvas.GetWidth() - 50.0f) / std::max(duration, 0.1f), 4.0f, 5000.0f);
            m_SkelViewStart = -20.0f / m_SkelPixelsPerSecond;
            m_SkelFit = false;
        }
        auto timeToX = [&](float t) { return canvas.Min.x + (t - m_SkelViewStart) * m_SkelPixelsPerSecond; };
        auto xToTime = [&](float x) { return m_SkelViewStart + (x - canvas.Min.x) / m_SkelPixelsPerSecond; };
        auto snap = [&](float t) { return m_Snap && !io.KeyAlt ? std::round(t * fps) / fps : t; };

        // ---- background
        dl->AddRectFilled(list.Min, list.Max, ImGui::GetColorU32(ImGuiCol_ChildBg, 1.0f));
        dl->AddRectFilled(canvas.Min, canvas.Max, IM_COL32(26, 28, 33, 255));
        const float x0 = timeToX(0.0f), xEnd = timeToX(clip->Duration);
        if (x0 > canvas.Min.x)
            dl->AddRectFilled(ImVec2(canvas.Min.x, rulerBottom), ImVec2(std::min(x0, canvas.Max.x), canvas.Max.y), IM_COL32(16, 17, 20, 255));
        if (xEnd < canvas.Max.x)
            dl->AddRectFilled(ImVec2(std::max(xEnd, canvas.Min.x), rulerBottom), ImVec2(canvas.Max.x, canvas.Max.y), IM_COL32(16, 17, 20, 255));
        dl->AddRectFilled(canvas.Min, ImVec2(canvas.Max.x, rulerBottom), IM_COL32(36, 38, 44, 255));
        dl->AddRectFilled(ImVec2(canvas.Min.x, rulerBottom), ImVec2(canvas.Max.x, eventsBottom), IM_COL32(31, 33, 38, 255));

        // ---- list: what each row is
        dl->PushClipRect(list.Min, list.Max, true);
        size_t boneRows = 0;
        for (const SkeletalRow& row : rows)
            boneRows += row.Joint >= 0 ? 1 : 0;
        const std::string summary = boneRows == rows.size() ? std::format("{} - {} suyak, {:.2f} s", clip->Name, boneRows, clip->Duration)
                                  : boneRows == 0 ? std::format("{} - {} blend shape, {:.2f} s", clip->Name, rows.size(), clip->Duration)
                                  : std::format("{} - {} suyak, {} blend shape, {:.2f} s", clip->Name, boneRows, rows.size() - boneRows, clip->Duration);
        dl->AddText(ImVec2(list.Min.x + 8.0f, list.Min.y + 4.0f), IM_COL32(170, 175, 185, 255), summary.c_str());
        dl->AddText(ImVec2(list.Min.x + 8.0f, rulerBottom + 4.0f), IM_COL32(230, 180, 90, 255), ICON_FA_BELL "  Event'lar");
        dl->PushClipRect(ImVec2(list.Min.x, eventsBottom), list.Max, true);
        for (size_t r = 0; r < rows.size(); r++)
        {
            const float y0 = bodyTop + r * s_SkelRowHeight;
            if (y0 > list.Max.y || y0 + s_SkelRowHeight < eventsBottom)
                continue;
            const bool selected = (int)r == m_SkelSelectedRow;
            if (selected)
                dl->AddRectFilled(ImVec2(list.Min.x, y0), ImVec2(list.Max.x, y0 + s_SkelRowHeight), UI::AccentU32(0.157f));
            const SkeletalRow& row = rows[r];
            std::string label = row.Joint >= 0 ? std::string(ICON_FA_BONE " ") + row.Label : row.Label;
            dl->AddText(ImVec2(list.Min.x + 8.0f + row.Depth * 10.0f, y0 + 3.0f), selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 204, 212, 255),
                        label.c_str());
            // which channels: T R S W
            std::string paths = std::string(row.Paths & 1 ? "T" : "") + (row.Paths & 2 ? "R" : "") + (row.Paths & 4 ? "S" : "") + (row.Paths & 8 ? "W" : "");
            dl->AddText(ImVec2(list.Max.x - 8.0f - ImGui::CalcTextSize(paths.c_str()).x, y0 + 3.0f), IM_COL32(120, 125, 135, 255), paths.c_str());
        }
        dl->PopClipRect();
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(list.Min.x, eventsBottom));
        ImGui::InvisibleButton("##skellist", ImVec2(list.GetWidth(), std::max(1.0f, list.Max.y - eventsBottom)));
        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
            m_SkelScrollY -= io.MouseWheel * s_SkelRowHeight * 2.0f;
        if (ImGui::IsItemClicked())
        {
            const int r = (int)std::floor((io.MousePos.y - bodyTop) / s_SkelRowHeight);
            m_SkelSelectedRow = r >= 0 && r < (int)rows.size() ? r : -1;
        }

        // ---- ruler: seconds with labels, frames as small ticks
        dl->PushClipRect(canvas.Min, canvas.Max, true);
        {
            static const float steps[] = { 0.01f, 0.02f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f };
            float major = steps[0];
            for (float st : steps)
            {
                major = st;
                if (st * m_SkelPixelsPerSecond >= 70.0f)
                    break;
            }
            const float frame = 1.0f / fps;
            if (frame * m_SkelPixelsPerSecond >= 5.0f)
            {
                const float t0 = std::floor(xToTime(canvas.Min.x) / frame) * frame;
                for (float t = t0; timeToX(t) < canvas.Max.x; t += frame)
                    if (t >= 0.0f)
                        dl->AddLine(ImVec2(timeToX(t), rulerBottom - 4.0f), ImVec2(timeToX(t), rulerBottom), IM_COL32(110, 115, 125, 255));
            }
            const float t0 = std::floor(xToTime(canvas.Min.x) / major) * major;
            for (float t = t0; timeToX(t) < canvas.Max.x; t += major)
            {
                if (t < -1e-4f)
                    continue;
                const float x = timeToX(t);
                dl->AddLine(ImVec2(x, canvas.Min.y + 12.0f), ImVec2(x, rulerBottom), IM_COL32(150, 155, 165, 255));
                dl->AddLine(ImVec2(x, rulerBottom), ImVec2(x, canvas.Max.y), IM_COL32(255, 255, 255, 14));
                const std::string label = major < 1.0f ? std::format("{:.2f}", t) : std::format("{:.0f}s", t);
                dl->AddText(ImVec2(x + 3.0f, canvas.Min.y + 1.0f), IM_COL32(170, 175, 185, 255), label.c_str());
            }
            dl->AddLine(ImVec2(xEnd, rulerBottom), ImVec2(xEnd, canvas.Max.y), IM_COL32(230, 170, 70, 120), 1.5f);
        }

        // ---- events of this clip (and the ones for every clip, dimmer)
        const float eventY = (rulerBottom + eventsBottom) * 0.5f;
        int hoveredEvent = -1;
        for (int i = 0; i < (int)anim.Events.size(); i++)
        {
            const AnimatorEvent& ev = anim.Events[(size_t)i];
            if (!ev.Clip.empty() && ev.Clip != m_SkelClip)
                continue;
            const float x = timeToX(std::clamp(ev.Time, 0.0f, 1.0f) * clip->Duration);
            const bool selected = i == m_SkelSelectedEvent;
            const bool every = ev.Clip.empty();
            ImVec2 pts[5] = { { x - 4.5f, eventY - 8.0f }, { x + 4.5f, eventY - 8.0f }, { x + 4.5f, eventY + 2.0f }, { x, eventY + 8.0f },
                              { x - 4.5f, eventY + 2.0f } };
            const ImU32 fill = selected ? IM_COL32(255, 214, 96, 255) : every ? IM_COL32(150, 130, 100, 255) : IM_COL32(222, 160, 70, 255);
            dl->AddConvexPolyFilled(pts, 5, fill);
            dl->AddPolyline(pts, 5, IM_COL32(20, 20, 20, 255), ImDrawFlags_Closed, 1.0f);
            dl->AddLine(ImVec2(x, eventsBottom), ImVec2(x, canvas.Max.y), selected ? IM_COL32(255, 214, 96, 70) : IM_COL32(222, 160, 70, 26));
            if (selected || m_SkelPixelsPerSecond * clip->Duration > 300.0f)
                dl->AddText(ImVec2(x + 7.0f, eventY - 8.0f), every ? IM_COL32(180, 165, 140, 255) : IM_COL32(236, 206, 150, 255), ev.Name.c_str());
            if (std::abs(io.MousePos.x - x) <= 6.0f && io.MousePos.y >= rulerBottom && io.MousePos.y <= eventsBottom)
                hoveredEvent = i;
        }

        // ---- rows: key diamonds (joints: by channel kind; blend shapes: pink)
        dl->PushClipRect(ImVec2(canvas.Min.x, eventsBottom), canvas.Max, true);
        for (size_t r = 0; r < rows.size(); r++)
        {
            const float y0 = bodyTop + r * s_SkelRowHeight, y1 = y0 + s_SkelRowHeight;
            if (y0 > canvas.Max.y)
                break;
            if (y1 < eventsBottom)
                continue;
            if ((int)r == m_SkelSelectedRow)
                dl->AddRectFilled(ImVec2(canvas.Min.x, y0), ImVec2(canvas.Max.x, y1), UI::AccentU32(0.086f));
            dl->AddLine(ImVec2(canvas.Min.x, y1), ImVec2(canvas.Max.x, y1), IM_COL32(255, 255, 255, 12));
            const SkeletalRow& row = rows[r];
            const ImU32 fill = row.Paths & 8 ? IM_COL32(236, 120, 170, 255) : row.Paths == 2 ? IM_COL32(110, 160, 245, 255)
                             : IM_COL32(150, 205, 120, 255);
            const float cy = (y0 + y1) * 0.5f;
            for (float t : row.Times)
            {
                const float x = timeToX(t);
                if (x < canvas.Min.x - 6.0f || x > canvas.Max.x + 6.0f)
                    continue;
                ImVec2 pts[4] = { { x, cy - 4.5f }, { x + 4.5f, cy }, { x, cy + 4.5f }, { x - 4.5f, cy } };
                dl->AddConvexPolyFilled(pts, 4, fill);
                dl->AddPolyline(pts, 4, IM_COL32(20, 20, 20, 255), ImDrawFlags_Closed, 1.0f);
            }
        }
        dl->PopClipRect();

        // ---- playhead
        {
            const float x = timeToX(m_SkelTime);
            dl->AddLine(ImVec2(x, canvas.Min.y), ImVec2(x, canvas.Max.y), IM_COL32(255, 92, 80, 255), 1.5f);
            ImVec2 head[3] = { { x - 6.0f, canvas.Min.y }, { x + 6.0f, canvas.Min.y }, { x, canvas.Min.y + 9.0f } };
            dl->AddConvexPolyFilled(head, 3, IM_COL32(255, 92, 80, 255));
            const std::string label = std::format("{}", (int)std::round(m_SkelTime * fps));
            dl->AddText(ImVec2(x + 5.0f, canvas.Min.y + 10.0f), IM_COL32(255, 160, 150, 255), label.c_str());
        }
        dl->PopClipRect();

        // ---- interaction
        ImGui::SetCursorScreenPos(canvas.Min);
        ImGui::InvisibleButton("##skelcanvas", canvas.GetSize(), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                                                                     ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const float mouseTime = xToTime(io.MousePos.x);
        if (hovered && io.MouseWheel != 0.0f)
        {
            // zoom around the mouse
            const float before = mouseTime;
            m_SkelPixelsPerSecond = std::clamp(m_SkelPixelsPerSecond * std::pow(1.15f, io.MouseWheel), 4.0f, 5000.0f);
            m_SkelViewStart = before - (io.MousePos.x - canvas.Min.x) / m_SkelPixelsPerSecond;
        }
        if (ImGui::IsItemActivated())
        {
            m_SkelScrubbing = m_SkelDragging = m_SkelPanning = false;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                m_SkelPanning = true;
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (hoveredEvent >= 0)
                {
                    m_SkelSelectedEvent = hoveredEvent;
                    m_SkelDragging = m_Editable;
                }
                else if (io.MousePos.y < eventsBottom && io.MousePos.y >= rulerBottom && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    AddSkeletalEvent(snap(std::clamp(mouseTime, 0.0f, clip->Duration)));
                else
                {
                    m_SkelScrubbing = true;
                    m_SkelPlaying = false;
                    if (io.MousePos.y >= eventsBottom)
                    {
                        const int r = (int)std::floor((io.MousePos.y - bodyTop) / s_SkelRowHeight);
                        m_SkelSelectedRow = r >= 0 && r < (int)rows.size() ? r : m_SkelSelectedRow;
                    }
                    if (io.MousePos.y < eventsBottom && io.MousePos.y >= rulerBottom)
                        m_SkelSelectedEvent = -1;
                }
            }
        }
        if (ImGui::IsItemActive())
        {
            if (m_SkelPanning && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                m_SkelViewStart -= io.MouseDelta.x / m_SkelPixelsPerSecond;
            else if (m_SkelScrubbing && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (m_Editable)
                    SetSkeletalTime(snap(mouseTime));
            }
            else if (m_SkelDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_SkelSelectedEvent >= 0 &&
                     m_SkelSelectedEvent < (int)anim.Events.size() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
            {
                const float t = snap(std::clamp(mouseTime, 0.0f, clip->Duration));
                const float normalized = clip->Duration > 0.0f ? t / clip->Duration : 0.0f;
                if (anim.Events[(size_t)m_SkelSelectedEvent].Time != normalized)
                {
                    anim.Events[(size_t)m_SkelSelectedEvent].Time = normalized;
                    MarkSceneModified();
                }
            }
        }
        if (ImGui::IsItemDeactivated())
            m_SkelScrubbing = m_SkelDragging = m_SkelPanning = false;
        // right click: the event's menu, or add one there
        static float s_ContextTime = 0.0f;
        static int s_ContextEvent = -1;
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            s_ContextTime = snap(std::clamp(mouseTime, 0.0f, clip->Duration));
            s_ContextEvent = hoveredEvent;
            if (hoveredEvent >= 0)
                m_SkelSelectedEvent = hoveredEvent;
            ImGui::OpenPopup("##skelctx");
        }
        if (ImGui::BeginPopup("##skelctx"))
        {
            ImGui::BeginDisabled(!m_Editable);
            if (s_ContextEvent >= 0 && s_ContextEvent < (int)anim.Events.size())
            {
                AnimatorEvent& ev = anim.Events[(size_t)s_ContextEvent];
                ImGui::TextDisabled(ICON_FA_BELL " %s", ev.Name.c_str());
                ImGui::Separator();
                bool every = ev.Clip.empty();
                if (ImGui::MenuItem(Tr("Hamma kliplarda"), nullptr, &every))
                {
                    ev.Clip = every ? std::string() : m_SkelClip;
                    MarkSceneModified();
                }
                if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  O'chirish"), "Delete"))
                    DeleteSkeletalEvent(s_ContextEvent);
            }
            else
            {
                if (ImGui::MenuItem(std::format(ICON_FA_BELL "  Event qo'shish ({:.2f} s)", s_ContextTime).c_str()))
                    AddSkeletalEvent(s_ContextTime);
                if (ImGui::MenuItem(Tr(ICON_FA_LOCATION_CROSSHAIRS "  Shu vaqtga o'tish")))
                    SetSkeletalTime(s_ContextTime);
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        if (hovered && hoveredEvent >= 0 && !ImGui::IsItemActive())
        {
            const AnimatorEvent& ev = anim.Events[(size_t)hoveredEvent];
            UI::Tooltip(std::format("{}  ({:.2f} s, {:.0f}%){}\nSudrang: vaqtini o'zgartirish, o'ng tugma: menyu", ev.Name,
                                    ev.Time * clip->Duration, ev.Time * 100.0f, ev.Clip.empty() ? "  - hamma kliplarda" : "").c_str());
        }
        else if (hovered && !ImGui::IsItemActive() && io.MousePos.y >= rulerBottom && io.MousePos.y < eventsBottom && m_Editable)
            UI::Tooltip("Ikki marta bosing: shu vaqtda event qo'shish");
    }

    void AnimationTimelinePanel::DrawSkeletalDetails()
    {
        Entity entity = GetAnimatorEntity();
        const Model* model = GetAnimatorModel();
        const AnimationClip* clip = GetSkeletalClip();
        if (!entity || !model || !clip)
            return;
        auto& anim = entity.GetComponent<AnimatorComponent>();
        const float fps = SkeletalFrameRate();

        // ---- the selected event
        if (m_SkelSelectedEvent >= 0 && m_SkelSelectedEvent < (int)anim.Events.size())
        {
            AnimatorEvent& ev = anim.Events[(size_t)m_SkelSelectedEvent];
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), ICON_FA_BELL "  Event");
            ImGui::BeginDisabled(!m_Editable);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##evname", Tr("nomi (OnAnimationEvent)"), &ev.Name))
                MarkSceneModified();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##evparam", Tr("parametr (ixtiyoriy)"), &ev.Parameter))
                MarkSceneModified();
            float seconds = ev.Time * clip->Duration;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##evtime", &seconds, 0.005f, 0.0f, clip->Duration,
                                 std::format("%.3f s  ({:.0f}%%, {} kadr)", ev.Time * 100.0f, (int)std::round(seconds * fps)).c_str(),
                                 ImGuiSliderFlags_AlwaysClamp))
            {
                ev.Time = clip->Duration > 0.0f ? std::clamp(seconds / clip->Duration, 0.0f, 1.0f) : 0.0f;
                MarkSceneModified();
            }
            bool every = ev.Clip.empty();
            if (ImGui::Checkbox(Tr("Hamma kliplarda"), &every))
            {
                ev.Clip = every ? std::string() : m_SkelClip;
                MarkSceneModified();
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Belgilansa: Animator o'ynaydigan har bir klipda shu normalizatsiyalangan vaqtda");
            if (ImGui::Button(Tr(ICON_FA_LOCATION_CROSSHAIRS "  Shu vaqtga")))
                SetSkeletalTime(seconds);
            ImGui::SameLine();
            if (ImGui::Button(Tr(ICON_FA_TRASH "  O'chirish")))
                DeleteSkeletalEvent(m_SkelSelectedEvent);
            ImGui::EndDisabled();
            ImGui::Separator();
        }

        // ---- the clip
        ImGui::TextColored(UI::AccentColor(), ICON_FA_PERSON_RUNNING "  %s", clip->Name.c_str());
        ImGui::TextDisabled("%.2f s, %d kadr (%.0f kadr/s)", clip->Duration, (int)std::round(clip->Duration * fps), fps);
        size_t keys = 0;
        for (const auto& ch : clip->Channels)
            keys += ch.Times.size();
        for (const auto& ch : clip->MorphChannels)
            keys += ch.Times.size();
        ImGui::TextDisabled("%zu kanal, %zu kalit", clip->Channels.size() + clip->MorphChannels.size(), keys);
        int events = 0;
        for (const AnimatorEvent& ev : anim.Events)
            events += ev.Clip == m_SkelClip || ev.Clip.empty() ? 1 : 0;
        ImGui::TextDisabled("%d event", events);
        if (const auto* steps = entity.TryGetComponent<FootstepsComponent>())
            ImGui::TextDisabled(ICON_FA_SHOE_PRINTS "  Qadam event'i: \"%s\"", steps->EventName.c_str());
        if (model->GetImportSettings().Clips.size() > 0)
            for (const ModelClipSettings& s : model->GetImportSettings().Clips)
                if (s.GetName() == clip->Name)
                {
                    ImGui::TextDisabled(ICON_FA_SCISSORS "  '%s' dan: %.2f - %s", s.Source.c_str(), s.Start,
                                        s.End < 0.0f ? "oxiri" : std::format("{:.2f} s", s.End).c_str());
                    break;
                }

        // ---- the selected row: its values at the current time
        const std::vector<SkeletalRow> rows = BuildRows(*model, *clip);
        if (m_SkelSelectedRow >= 0 && m_SkelSelectedRow < (int)rows.size())
        {
            const SkeletalRow& row = rows[(size_t)m_SkelSelectedRow];
            ImGui::Separator();
            if (row.Joint >= 0)
            {
                ImGui::Text(ICON_FA_BONE "  %s", row.Label.c_str());
                LocalPose pose;
                SampleLocalPose(model->GetSkeleton(), clip, m_SkelTime, pose);
                const size_t j = (size_t)row.Joint;
                if (j < pose.Size())
                {
                    const glm::vec3 euler = glm::degrees(glm::eulerAngles(pose.R[j]));
                    ImGui::TextDisabled("T  %.3f  %.3f  %.3f", pose.T[j].x, pose.T[j].y, pose.T[j].z);
                    ImGui::TextDisabled("R  %.1f\xc2\xb0  %.1f\xc2\xb0  %.1f\xc2\xb0", euler.x, euler.y, euler.z);
                    ImGui::TextDisabled("S  %.3f  %.3f  %.3f", pose.S[j].x, pose.S[j].y, pose.S[j].z);
                }
                ImGui::TextDisabled("%zu kalit", row.Times.size());
            }
            else if (row.MorphGroup >= 0)
            {
                const MorphGroup& g = model->GetMorphGroups()[(size_t)row.MorphGroup];
                ImGui::Text("%s", row.Label.c_str());
                std::vector<float> weights = model->GetDefaultMorphWeights();
                SampleClipMorphWeights(*model, clip, m_SkelTime, weights);
                for (uint32_t t = 0; t < g.GetCount(); t++)
                {
                    const float w = weights[g.Offset + t];
                    ImGui::ProgressBar(std::clamp(w, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f), std::format("{}  {:.2f}", g.Targets[t], w).c_str());
                }
            }
        }
        else
        {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Qatorni bosing: suyakning shu vaqtdagi qiymatlari. Event qatorida ikki marta bosing: event qo'shish");
            ImGui::PopTextWrapPos();
        }
    }

    void AnimationTimelinePanel::HandleSkeletalShortcuts()
    {
        if (!m_Focused || ImGui::GetIO().WantTextInput)
            return;
        const AnimationClip* clip = GetSkeletalClip();
        if (!clip)
            return;
        const ImGuiIO& io = ImGui::GetIO();
        const float frame = 1.0f / SkeletalFrameRate();
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && m_Editable)
        {
            m_SkelPlaying = !m_SkelPlaying;
            m_SkelPreview = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            DeleteSkeletalEvent(m_SkelSelectedEvent);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            m_SkelFit = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) { m_SkelPlaying = false; SetSkeletalTime(0.0f); }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) { m_SkelPlaying = false; SetSkeletalTime(clip->Duration); }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { m_SkelPlaying = false; SetSkeletalTime(std::round((m_SkelTime - frame) / frame) * frame); }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { m_SkelPlaying = false; SetSkeletalTime(std::round((m_SkelTime + frame) / frame) * frame); }
    }

}
