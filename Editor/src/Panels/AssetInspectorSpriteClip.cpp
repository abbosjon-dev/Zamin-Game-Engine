// The sprite animation clip part of the asset inspector (.zsanim): a playing preview of its frames
// and its settings, edited live (every Sprite Animation using it follows) and saved to the file.
#include "AssetInspectorPanel.h"
#include "../UI.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    static const ImVec4 s_ClipErrorColor(0.95f, 0.45f, 0.4f, 1.0f);
    static const ImVec4 s_ClipWarnColor(0.95f, 0.75f, 0.35f, 1.0f);

    void AssetInspectorPanel::DrawSpriteClip()
    {
        Ref<SpriteClipAsset> asset = SpriteClipLibrary::Load(m_Path); // cached: edits here show on every user at once
        ImGui::Spacing();
        if (!asset)
        {
            ImGui::TextColored(s_ClipErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  Sprite klip o'qilmadi - konsolni tekshiring");
            return;
        }
        SpriteClip clip = asset->GetClip();
        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.72f, 1.0f), ICON_FA_FILM "  Sprite animatsiya klipi");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Sprite Animation komponentining 'Kliplar' ro'yxatiga torting; skript: PlaySpriteClip(\"%s\"). "
                            "O'zgarishlar darhol (o'yin davomida ham) qo'llanadi va faylga saqlanadi.", asset->GetName().c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        // ---- preview: the frame shown now, playing at the clip's rate
        Ref<Texture2D> texture = clip.Texture.empty() ? nullptr
            : TextureLibrary::Load(Project::GetAssetFileSystemPath(clip.Texture), TextureFilter::Nearest);
        const int frameCount = clip.GetFrameCount();
        if (m_SpriteClipPlaying && clip.FPS > 0.0f)
            m_SpriteClipTime += ImGui::GetIO().DeltaTime;
        int frame = clip.FPS > 0.0f ? (int)std::floor(m_SpriteClipTime * clip.FPS) : 0;
        frame = clip.Loop ? frame % std::max(1, frameCount) : std::min(frame, frameCount - 1);
        {
            const float avail = ImGui::GetContentRegionAvail().x;
            const float box = std::min(avail, 220.0f);
            ImVec2 min = ImGui::GetCursorScreenPos();
            min.x += (avail - box) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 max(min.x + box, min.y + box);
            // A checkerboard behind (transparent pixels).
            const float cell = 12.0f;
            dl->AddRectFilled(min, max, IM_COL32(48, 50, 56, 255));
            for (float y = 0; y < box; y += cell)
                for (float x = ((int)(y / cell) % 2) * cell; x < box; x += cell * 2.0f)
                    dl->AddRectFilled(ImVec2(min.x + x, min.y + y), ImVec2(std::min(max.x, min.x + x + cell), std::min(max.y, min.y + y + cell)),
                                      IM_COL32(62, 65, 72, 255));
            if (texture)
            {
                SpriteRendererComponent sprite;
                sprite.Texture = texture;
                sprite.TexturePath = clip.Texture;
                SpriteAnimationComponent anim;
                anim.ApplyClip(clip);
                anim.Time = clip.FPS > 0.0f ? ((float)frame + 0.5f) / clip.FPS : 0.0f;
                SpriteRegion region = Scene::GetSpriteRegion(sprite, &anim, Project::GetAssetRegistry());
                const glm::vec2 px = glm::max(region.Pixels, glm::vec2(1.0f));
                const float scale = std::min(box / px.x, box / px.y) * 0.92f;
                const ImVec2 size(px.x * scale, px.y * scale);
                const ImVec2 at(min.x + (box - size.x) * 0.5f, min.y + (box - size.y) * 0.5f);
                dl->AddImage((ImTextureID)(uintptr_t)texture->GetRendererID(), at, ImVec2(at.x + size.x, at.y + size.y),
                             ImVec2(region.UVMin.x, region.UVMax.y), ImVec2(region.UVMax.x, region.UVMin.y));
            }
            else
            {
                const char* text = clip.Texture.empty() ? "Tekstura tanlanmagan" : "Tekstura yuklanmadi";
                ImVec2 ts = ImGui::CalcTextSize(text);
                dl->AddText(ImVec2(min.x + (box - ts.x) * 0.5f, min.y + (box - ts.y) * 0.5f), IM_COL32(170, 170, 180, 255), text);
            }
            // The clip's events on this frame: a bell in the corner.
            for (const SpriteClipEvent& ev : clip.Events)
                if (ev.Frame == frame)
                {
                    std::string label = std::string(ICON_FA_BELL "  ") + ev.Name;
                    dl->AddText(ImVec2(min.x + 6.0f, min.y + 4.0f), IM_COL32(255, 210, 120, 255), label.c_str());
                    break;
                }
            dl->AddRect(min, max, IM_COL32(90, 95, 105, 255));
            ImGui::Dummy(ImVec2(avail, box));
        }
        // Transport: play / pause, step, the frame strip.
        {
            const float avail = ImGui::GetContentRegionAvail().x;
            if (ImGui::Button(m_SpriteClipPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY, ImVec2(ImGui::GetFrameHeight() * 1.4f, 0)))
                m_SpriteClipPlaying = !m_SpriteClipPlaying;
            if (ImGui::IsItemHovered()) UI::Tooltip(m_SpriteClipPlaying ? "To'xtatish" : "O'ynatish");
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_BACKWARD_STEP) && clip.FPS > 0.0f)
            {
                m_SpriteClipPlaying = false;
                m_SpriteClipTime = (float)((frame - 1 + frameCount) % std::max(1, frameCount)) / clip.FPS;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FORWARD_STEP) && clip.FPS > 0.0f)
            {
                m_SpriteClipPlaying = false;
                m_SpriteClipTime = (float)((frame + 1) % std::max(1, frameCount)) / clip.FPS;
            }
            ImGui::SameLine();
            ImGui::Text("Kadr %d / %d", frame + 1, frameCount);
            if (clip.UsesFrameList() && frame >= 0 && frame < (int)clip.Frames.size())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", clip.Frames[(size_t)frame].c_str());
            }
            // One cell per frame: click to show it; events marked.
            const float cellW = std::clamp((avail - 4.0f) / std::max(1, frameCount), 6.0f, 28.0f);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float h = 14.0f;
            const int shown = std::min(frameCount, (int)((avail - 4.0f) / cellW));
            for (int i = 0; i < shown; i++)
            {
                ImVec2 a(p.x + i * cellW, p.y), b(p.x + (i + 1) * cellW - 2.0f, p.y + h);
                const bool current = i == frame;
                dl->AddRectFilled(a, b, current ? UI::AccentU32() : IM_COL32(70, 74, 82, 255), 2.0f);
                for (const SpriteClipEvent& ev : clip.Events)
                    if (ev.Frame == i)
                    {
                        dl->AddTriangleFilled(ImVec2((a.x + b.x) * 0.5f, b.y + 1.0f), ImVec2((a.x + b.x) * 0.5f - 4.0f, b.y + 7.0f),
                                              ImVec2((a.x + b.x) * 0.5f + 4.0f, b.y + 7.0f), IM_COL32(255, 200, 90, 255));
                        break;
                    }
            }
            ImGui::InvisibleButton("##frames", ImVec2(std::max(1.0f, shown * cellW), h + 8.0f));
            if (ImGui::IsItemActive() && clip.FPS > 0.0f && cellW > 0.0f)
            {
                const int clicked = std::clamp((int)((ImGui::GetIO().MousePos.x - p.x) / cellW), 0, frameCount - 1);
                m_SpriteClipPlaying = false;
                m_SpriteClipTime = ((float)clicked + 0.01f) / clip.FPS;
            }
        }
        ImGui::Spacing();

        bool changed = false;
        if (UI::BeginPropertyGrid("##spriteClip"))
        {
            UI::PropertyHeader("Kadrlar");
            UI::AssetPathOptions options;
            options.Missing = !clip.Texture.empty() && !texture;
            options.Tooltip = "Klip qaysi rasm uchun: o'ynaganda Sprite Renderer'ga qo'yiladi (bo'sh - renderer'dagisi)";
            changed |= UI::PropertyAssetPath("Tekstura", clip.Texture, ".png;.jpg;.jpeg;.bmp;.tga", ICON_FA_IMAGE, options);
            const SpriteSheet* sheet = clip.Texture.empty() ? nullptr : SpriteSheet::Get(clip.Texture);

            const char* sources[] = { "Grid (ustun x qator)", "Sprite sheet kadrlari" };
            int source = clip.UsesFrameList() ? 1 : 0;
            if (UI::PropertyCombo("Manba", source, sources, 2))
            {
                if (source == 0)
                    clip.Frames.clear();
                else if (sheet)
                    clip.Frames = sheet->FindSequence(sheet->GetSequencePrefixes().empty() ? "" : sheet->GetSequencePrefixes().front());
                changed = true;
            }
            if (source == 1 && !clip.UsesFrameList())
            {
                UI::PropertyLabel("");
                ImGui::TextColored(s_ClipWarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Teksturada kadrlar yo'q (Sprite muharriri)");
            }
            if (clip.UsesFrameList())
            {
                UI::PropertyLabel("Ketma-ketlik", "Bir xil nomli, raqamlangan kadrlar (run_0, run_1, ...) tartib bilan qo'yiladi");
                if (ImGui::BeginCombo("##sequence", std::format("{} ta kadr", clip.Frames.size()).c_str()))
                {
                    if (sheet)
                    {
                        for (const std::string& prefix : sheet->GetSequencePrefixes())
                        {
                            std::vector<std::string> sequence = sheet->FindSequence(prefix);
                            if (ImGui::Selectable(std::format("{}  ({} ta)", prefix, sequence.size()).c_str()))
                            {
                                clip.Frames = sequence;
                                changed = true;
                            }
                        }
                        if (ImGui::Selectable(std::format("(hammasi, {} ta)", sheet->Frames.size()).c_str()))
                        {
                            clip.Frames = sheet->FindSequence("");
                            changed = true;
                        }
                    }
                    else
                        ImGui::TextDisabled("Teksturada kadrlar yo'q");
                    ImGui::EndCombo();
                }
                UI::PropertyLabel("Kadrlar");
                std::string list;
                for (size_t i = 0; i < clip.Frames.size() && i < 12; i++)
                    list += (i ? ", " : "") + clip.Frames[i];
                if (clip.Frames.size() > 12)
                    list += std::format(" ... (+{})", clip.Frames.size() - 12);
                ImGui::TextWrapped("%s", list.c_str());
                int missing = 0;
                for (const std::string& name : clip.Frames)
                    if (!sheet || !sheet->Find(name))
                        missing++;
                if (missing > 0 && !clip.Texture.empty())
                {
                    UI::PropertyLabel("");
                    ImGui::TextColored(s_ClipWarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  %d ta kadr teksturada topilmadi", missing);
                }
            }
            else
            {
                changed |= UI::PropertyInt("Ustunlar", clip.Columns);
                changed |= UI::PropertyInt("Qatorlar", clip.Rows);
                changed |= UI::PropertyInt("Boshlang'ich katak", clip.StartFrame);
                changed |= UI::PropertyInt("Kadrlar soni", clip.FrameCount);
                clip.Columns = std::max(1, clip.Columns);
                clip.Rows = std::max(1, clip.Rows);
                clip.StartFrame = std::max(0, clip.StartFrame);
                clip.FrameCount = std::max(1, clip.FrameCount);
            }

            UI::PropertyHeader("Ijro");
            changed |= UI::PropertyFloat("FPS", clip.FPS, 0.1f, 0.0f, 120.0f, "%.1f");
            clip.FPS = std::clamp(clip.FPS, 0.0f, 240.0f);
            changed |= UI::PropertyBool("Takrorlash", clip.Loop);
            UI::PropertyLabel("Davomiylik");
            ImGui::Text("%.2f s  (%d kadr)", clip.GetDuration(), clip.GetFrameCount());

            UI::PropertyHeader("Hodisalar (event)", "Kadr ko'rsatilganda obyekt skripti OnAnimationEvent(e) oladi (e.Name, e.Parameter, "
                                                    "e.Clip - klip nomi); event bus'da \"AnimationEvent\". Har aylanishda bir marta");
            int remove = -1;
            for (int i = 0; i < (int)clip.Events.size(); i++)
            {
                SpriteClipEvent& ev = clip.Events[(size_t)i];
                ImGui::PushID(i);
                const std::string label = std::format("Event {}", i + 1);
                UI::PropertyLabel(label.c_str(), "Kadr (1 dan), nom va ixtiyoriy parametr");
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float button = ImGui::GetFrameHeight();
                const float avail = ImGui::GetContentRegionAvail().x - button - spacing * 3.0f;
                int shownFrame = ev.Frame + 1;
                ImGui::SetNextItemWidth(avail * 0.22f);
                if (ImGui::DragInt("##frame", &shownFrame, 0.1f, 1, std::max(1, frameCount)))
                {
                    ev.Frame = std::clamp(shownFrame - 1, 0, std::max(0, frameCount - 1));
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(avail * 0.44f);
                changed |= ImGui::InputTextWithHint("##name", Tr("nom"), &ev.Name);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(avail * 0.34f);
                changed |= ImGui::InputTextWithHint("##param", Tr("parametr"), &ev.Parameter);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_TRASH, ImVec2(button, 0)))
                    remove = i;
                ImGui::PopID();
            }
            if (remove >= 0)
            {
                clip.Events.erase(clip.Events.begin() + remove);
                changed = true;
            }
            UI::PropertyLabel("");
            if (ImGui::Button(Tr(ICON_FA_PLUS "  Event")))
            {
                clip.Events.push_back({ std::clamp(frame, 0, std::max(0, frameCount - 1)), "Footstep", "" });
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ko'rsatilayotgan kadrga event qo'shiladi");
            UI::EndPropertyGrid();
        }
        if (changed)
        {
            if (clip.UsesFrameList())
            {
                clip.StartFrame = 0;
                clip.FrameCount = (int)clip.Frames.size();
            }
            for (SpriteClipEvent& ev : clip.Events)
                ev.Frame = std::clamp(ev.Frame, 0, std::max(0, clip.GetFrameCount() - 1));
            if (!ImGui::IsAnyItemActive())
                clip.SortEvents();
            asset->SetClip(clip); // every Sprite Animation playing it, also in the running game
            m_SpriteClipUnsaved = true;
        }
        if (m_SpriteClipUnsaved && !ImGui::IsAnyItemActive())
        {
            m_SpriteClipUnsaved = false;
            SpriteClip sorted = asset->GetClip();
            sorted.SortEvents();
            if (!(sorted == asset->GetClip()))
                asset->SetClip(sorted);
            if (!asset->Save())
                ZM_ERROR("Sprite animatsiya klipi saqlanmadi: {}", m_Path.string());
        }
    }

}
