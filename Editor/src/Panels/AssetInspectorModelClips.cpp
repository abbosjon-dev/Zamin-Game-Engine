// The clip part of a model's import settings in the asset inspector: the file's clips trimmed to a
// start / end and split into named sub-clips (ModelImportSettings::Clips), edited in seconds or
// frames on a range bar over the source clip, previewed in the model view before they are applied.
#include "AssetInspectorPanel.h"
#include "../UI.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    static const ImVec4 s_ClipsWarnColor(0.95f, 0.75f, 0.35f, 1.0f);

    const AnimationClip* AssetInspectorPanel::FindSourceClip(const std::string& name) const
    {
        if (!m_Model)
            return nullptr;
        for (const AnimationClip& c : m_Model->GetSourceAnimations())
            if (c.Name == name)
                return &c;
        return nullptr;
    }

    void AssetInspectorPanel::StartClipSettingPreview(int entry)
    {
        m_ClipSettingPreview = entry;
        m_ClipPlaying = true;
        m_ClipTime = 0.0f;
        RefreshClipSettingPreview();
    }

    void AssetInspectorPanel::RefreshClipSettingPreview()
    {
        if (m_ClipSettingPreview < 0 || m_ClipSettingPreview >= (int)m_ModelEdit.Clips.size())
        {
            m_ClipSettingPreview = -1;
            return;
        }
        const ModelClipSettings& s = m_ModelEdit.Clips[(size_t)m_ClipSettingPreview];
        const AnimationClip* source = FindSourceClip(s.Source);
        if (!source)
        {
            m_ClipSettingPreview = -1;
            return;
        }
        m_ClipSettingClip = TrimClip(*source, s.Start, s.End, s.GetName());
        if (m_ClipTime > m_ClipSettingClip.Duration)
            m_ClipTime = 0.0f;
    }

    // A bar over the whole source clip: the kept part [start, end] highlighted, its two edges
    // dragged to trim it, a click / drag inside scrubs the preview. Returns true when start / end changed.
    static bool ClipRangeBar(const char* id, float duration, float& start, float& end, float fps, float* scrub, bool previewing,
                             float previewTime)
    {
        const float width = std::max(60.0f, ImGui::GetContentRegionAvail().x);
        const float height = ImGui::GetFrameHeight();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(width, height));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(p0.x + width, p0.y + height);
        const float d = std::max(duration, 1e-4f);
        auto toX = [&](float t) { return p0.x + std::clamp(t / d, 0.0f, 1.0f) * width; };
        auto toT = [&](float x) { return std::clamp((x - p0.x) / width, 0.0f, 1.0f) * d; };
        const float endTime = end < 0.0f || end > duration ? duration : end;
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
        // frame ticks (every frame when there is room, else every 10)
        if (fps > 0.0f)
        {
            const float frames = duration * fps;
            const float step = frames > 0.0f && width / frames >= 4.0f ? 1.0f : 10.0f;
            for (float f = 0.0f; f <= frames + 0.01f; f += step)
            {
                const float x = toX(f / fps);
                const bool major = std::fmod(f, step * 5.0f) < 0.5f;
                dl->AddLine(ImVec2(x, p1.y - (major ? 6.0f : 3.0f)), ImVec2(x, p1.y), IM_COL32(150, 150, 160, major ? 140 : 70));
            }
        }
        const float x0 = toX(start), x1 = toX(endTime);
        dl->AddRectFilled(ImVec2(x0, p0.y + 2.0f), ImVec2(std::max(x1, x0 + 1.0f), p1.y - 2.0f), UI::AccentU32(0.45f), 2.0f);
        const ImU32 handle = IM_COL32(240, 240, 245, 230);
        dl->AddRectFilled(ImVec2(x0 - 2.0f, p0.y), ImVec2(x0 + 2.0f, p1.y), handle, 1.0f);
        dl->AddRectFilled(ImVec2(x1 - 2.0f, p0.y), ImVec2(x1 + 2.0f, p1.y), handle, 1.0f);
        if (previewing)
        {
            const float x = toX(start + previewTime);
            dl->AddLine(ImVec2(x, p0.y - 1.0f), ImVec2(x, p1.y + 1.0f), IM_COL32(255, 200, 80, 255), 2.0f);
        }

        // Which part the drag holds: 0 none, 1 start, 2 end, 3 scrub (kept from the click on).
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID key = ImGui::GetItemID();
        int grab = storage->GetInt(key, 0);
        const float mx = ImGui::GetIO().MousePos.x;
        if (ImGui::IsItemActivated())
        {
            const float grabRadius = 6.0f;
            grab = std::abs(mx - x0) <= grabRadius ? 1 : std::abs(mx - x1) <= grabRadius ? 2 : 3;
            storage->SetInt(key, grab);
        }
        bool changed = false;
        if (ImGui::IsItemActive())
        {
            float t = toT(mx);
            if (fps > 0.0f && !ImGui::GetIO().KeyAlt)
                t = std::round(t * fps) / fps; // snapped to frames (Alt: free)
            if (grab == 1)
            {
                const float s = std::min(t, endTime);
                changed = s != start;
                start = s;
            }
            else if (grab == 2)
            {
                const float e = std::max(t, start);
                const float stored = e >= duration - 1e-4f ? -1.0f : e;
                changed = stored != end;
                end = stored;
            }
            else if (scrub)
                *scrub = std::clamp(t - start, 0.0f, std::max(0.0f, endTime - start));
        }
        else if (ImGui::IsItemDeactivated())
            storage->SetInt(key, 0);
        if (ImGui::IsItemHovered())
        {
            const bool edge = std::abs(mx - x0) <= 6.0f || std::abs(mx - x1) <= 6.0f;
            if (edge)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (!ImGui::IsItemActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                UI::Tooltip("Chetlarini sudrang: boshlanish / oxir (kadrlarga yopishadi, Alt: erkin). Ichida bosing / sudrang: ko'rib chiqish vaqti");
        }
        return changed;
    }

    void AssetInspectorPanel::DrawModelClipSettings()
    {
        const std::vector<AnimationClip> empty;
        const std::vector<AnimationClip>& sources = m_Model ? m_Model->GetSourceAnimations() : empty;
        if (sources.empty() && m_ModelEdit.Clips.empty())
            return;

        if (!UI::BeginPropertyGrid("##modelClipSettings"))
            return;
        UI::PropertyHeader("Kliplarni kesish va bo'lish",
                           "Fayldagi klipning bir qismi (boshlanish..oxir) - shu nomli bo'lsa klip o'rniga (kesilgan), boshqa nom bilan "
                           "alohida klip bo'ladi (bitta uzun yozuvdan Walk, Run, Jump). Animator va Animatsiya oynasi yangi kliplarni "
                           "shu nomlar bilan ko'radi. \"Qo'llash\" bilan model qayta import qilinadi.");
        UI::PropertyLabel("Kadr tezligi", "Vaqtlar kadrda ko'rsatiladi (faqat ko'rinish; kliplar soniyada saqlanadi)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat("##clipFps", &m_ModelEdit.ClipFrameRate, 0.2f, 1.0f, 240.0f, "%.0f kadr/s", ImGuiSliderFlags_AlwaysClamp);
        UI::PropertyLabel("Asl kliplar", "O'chirilsa: undan qism kesilgan fayl klipi ro'yxatda qolmaydi (faqat kesilgan qismlar). "
                                         "Hech narsa kesilmagan kliplar baribir qoladi.");
        ImGui::Checkbox("Saqlansin##keepSources", &m_ModelEdit.KeepSourceClips);
        UI::EndPropertyGrid();

        const float fps = std::max(1.0f, m_ModelEdit.ClipFrameRate);
        int remove = -1;
        for (int i = 0; i < (int)m_ModelEdit.Clips.size(); i++)
        {
            ModelClipSettings& s = m_ModelEdit.Clips[(size_t)i];
            const AnimationClip* source = FindSourceClip(s.Source);
            const float duration = source ? source->Duration : 0.0f;
            ImGui::PushID(i);
            ImGui::Separator();
            // ---- name, source, preview, delete
            const bool previewing = m_ClipSettingPreview == i;
            const float button = ImGui::GetFrameHeight();
            if (ImGui::Button(previewing && m_ClipPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY, ImVec2(button, 0)))
            {
                if (previewing)
                    m_ClipPlaying = !m_ClipPlaying;
                else
                    StartClipSettingPreview(i);
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Shu qismni ko'rib chiqish (qo'llamasdan)");
            ImGui::SameLine();
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float rest = ImGui::GetContentRegionAvail().x - button - spacing;
            ImGui::SetNextItemWidth(rest * 0.5f - spacing);
            std::string name = s.GetName();
            if (ImGui::InputTextWithHint("##name", "nomi", &name))
                s.Name = name == s.Source ? std::string() : name;
            if (ImGui::IsItemHovered())
                UI::Tooltip("Yangi klip nomi. Manba bilan bir xil bo'lsa: manba klip kesiladi (o'rniga)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(rest * 0.5f);
            if (ImGui::BeginCombo("##source", s.Source.empty() ? "(manba)" : s.Source.c_str()))
            {
                for (const AnimationClip& c : sources)
                    if (ImGui::Selectable(std::format("{}  ({:.2f} s)##{}", c.Name, c.Duration, (const void*)&c).c_str(), c.Name == s.Source))
                    {
                        const bool sameName = s.Name.empty();
                        s.Source = c.Name;
                        if (sameName)
                            s.Name.clear();
                    }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Fayldagi qaysi klipdan kesiladi");
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TRASH, ImVec2(button, 0)))
                remove = i;

            // ---- range: the bar, then start / end in seconds and frames
            if (!source)
                ImGui::TextColored(s_ClipsWarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  '%s' klipi faylda yo'q", s.Source.c_str());
            else
            {
                float scrub = previewing ? m_ClipTime : 0.0f;
                if (ClipRangeBar("##range", duration, s.Start, s.End, fps, &scrub, previewing, m_ClipTime))
                    if (previewing)
                        RefreshClipSettingPreview();
                if (ImGui::IsItemActive() && previewing && scrub != m_ClipTime)
                {
                    m_ClipTime = scrub;
                    m_ClipPlaying = false;
                }
                else if (ImGui::IsItemActivated() && !previewing)
                    StartClipSettingPreview(i);
                const float endTime = s.End < 0.0f || s.End > duration ? duration : s.End;
                const float field = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
                ImGui::SetNextItemWidth(field);
                float startFrame = s.Start * fps;
                if (ImGui::DragFloat("##start", &startFrame, 0.25f, 0.0f, endTime * fps, std::format("{:.2f} s  |  %.0f", s.Start).c_str(),
                                     ImGuiSliderFlags_AlwaysClamp))
                {
                    s.Start = std::round(startFrame) / fps;
                    s.Start = std::clamp(s.Start, 0.0f, endTime);
                    if (previewing)
                        RefreshClipSettingPreview();
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Boshlanish (kadr; soniya chapda)");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(field);
                float endFrame = endTime * fps;
                if (ImGui::DragFloat("##end", &endFrame, 0.25f, s.Start * fps, duration * fps, std::format("{:.2f} s  |  %.0f", endTime).c_str(),
                                     ImGuiSliderFlags_AlwaysClamp))
                {
                    const float e = std::clamp(std::round(endFrame) / fps, s.Start, duration);
                    s.End = e >= duration - 1e-4f ? -1.0f : e;
                    if (previewing)
                        RefreshClipSettingPreview();
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Oxir (kadr; oxirigacha bo'lsa fayldagi uzunlik o'zgarsa ham oxirida qoladi)");
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%.2f s, %d kadr", endTime - s.Start, (int)std::round((endTime - s.Start) * fps));
                // Duplicate names / names of other file clips: the importer skips those.
                for (int j = 0; j < i; j++)
                    if (m_ModelEdit.Clips[(size_t)j].GetName() == s.GetName())
                    {
                        ImGui::TextColored(s_ClipsWarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  '%s' nomi yuqorida bor - bu qism yaratilmaydi",
                                           s.GetName().c_str());
                        break;
                    }
                if (s.GetName() != s.Source && FindSourceClip(s.GetName()))
                    ImGui::TextColored(s_ClipsWarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  '%s' fayldagi boshqa klipning nomi", s.GetName().c_str());
            }
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            m_ModelEdit.Clips.erase(m_ModelEdit.Clips.begin() + remove);
            if (m_ClipSettingPreview == remove)
                m_ClipSettingPreview = -1;
            else if (m_ClipSettingPreview > remove)
                m_ClipSettingPreview--;
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(sources.empty());
        if (ImGui::Button(ICON_FA_SCISSORS "  Klip qismi qo'shish"))
            ImGui::OpenPopup("##addClipSetting");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Fayldagi klipdan yangi qism (yoki shu klipni kesish)");
        if (ImGui::BeginPopup("##addClipSetting"))
        {
            for (const AnimationClip& c : sources)
            {
                if (ImGui::BeginMenu(std::format("{}  ({:.2f} s)##{}", c.Name, c.Duration, (const void*)&c).c_str()))
                {
                    if (ImGui::MenuItem(ICON_FA_SCISSORS "  Shu klipni kesish (nomi o'zgarmaydi)"))
                    {
                        m_ModelEdit.Clips.push_back({ c.Name, {}, 0.0f, -1.0f });
                        StartClipSettingPreview((int)m_ModelEdit.Clips.size() - 1);
                    }
                    if (ImGui::MenuItem(ICON_FA_PLUS "  Yangi qism (alohida klip)"))
                    {
                        std::string name = c.Name + "_qism";
                        for (int n = 2; FindSourceClip(name) || std::any_of(m_ModelEdit.Clips.begin(), m_ModelEdit.Clips.end(),
                                                                                [&](const ModelClipSettings& s) { return s.GetName() == name; }); n++)
                            name = std::format("{}_qism{}", c.Name, n);
                        m_ModelEdit.Clips.push_back({ c.Name, name, 0.0f, c.Duration * 0.5f });
                        StartClipSettingPreview((int)m_ModelEdit.Clips.size() - 1);
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndPopup();
        }
        if (m_ModelEdit.Clips != m_ModelSaved.Clips || m_ModelEdit.KeepSourceClips != m_ModelSaved.KeepSourceClips)
        {
            ImGui::SameLine();
            ImGui::TextColored(s_ClipsWarnColor, ICON_FA_CIRCLE_INFO "  Qo'llanmagan: ko'rib chiqish faqat shu oynada");
        }
    }

}
