// The scene view's particle panel: while a Particle System is selected, a small panel in a corner of
// the viewport plays, pauses, restarts and stops it and shows its particle count (its sub-emitter
// templates' too) and where it is in its cycle - also while the game runs.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_set>

namespace Zamin {

    void EditorLayer::UI_ParticleOverlay()
    {
        Entity entity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (!entity || !entity.IsValid() || entity.GetScene() != m_ActiveScene.get() || !entity.HasComponent<ParticleSystemComponent>())
            return;
        const glm::vec2 viewport = m_ViewportBounds[1] - m_ViewportBounds[0];
        if (viewport.x < 320.0f || viewport.y < 200.0f)
            return;
        auto& ps = entity.GetComponent<ParticleSystemComponent>();

        // The particles of its sub-emitter templates (a firework's bursts) count with it.
        size_t subParticles = 0;
        std::unordered_set<uint64_t> seen;
        for (const auto& sub : ps.SubEmitters.Emitters)
            if (ps.SubEmitters.Enabled && sub.Emitter && seen.insert(sub.Emitter.ID).second)
                if (Entity target = m_ActiveScene->GetEntityByUUID(sub.Emitter.ID); target && target != entity)
                    if (const auto* child = target.TryGetComponent<ParticleSystemComponent>())
                        subParticles += child->Particles.size();

        const float fontSize = ImGui::GetFontSize();
        const float width = std::floor(fontSize * 16.0f);
        // Bottom right; bottom left when the statistics overlay is there.
        const auto& stats = m_ViewSettings.StatsOverlay;
        const bool left = stats.Visible && stats.Corner == OverlayCorner::BottomRight;
        static float s_Height = 96.0f; // the last frame's (it grows with its rows)
        float bottom = m_ViewportBounds[1].y - 14.0f;
        if (left && (m_DebugView != RenderDebugView::Lit || m_Wireframe != WireframeMode::Off || m_ShowTransparentOrder))
            bottom -= ImGui::GetTextLineHeight() + 22.0f; // above the view mode's label
        const ImVec2 pos(left ? m_ViewportBounds[0].x + 12.0f : m_ViewportBounds[1].x - width - 12.0f, bottom - s_Height);
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.08f, 0.095f, 0.88f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        if (ImGui::BeginChild("##ParticleOverlay", ImVec2(width, 0.0f),
                              ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
        {
            // ---- title: the system and its count
            const std::string count = subParticles > 0 ? std::format("{} + {}", ps.Particles.size(), subParticles)
                                                       : std::format("{}", ps.Particles.size());
            const float countWidth = ImGui::CalcTextSize(count.c_str()).x;
            ImGui::TextColored(UI::AccentColor(), ICON_FA_WAND_MAGIC_SPARKLES);
            ImGui::SameLine();
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            // The name as long as it fits before the count.
            const float room = ImGui::GetWindowContentRegionMax().x - ImGui::GetCursorPosX() - countWidth - 10.0f;
            std::string name = entity.GetName();
            if (ImGui::CalcTextSize(name.c_str()).x > room)
            {
                while (!name.empty() && ImGui::CalcTextSize((name + "...").c_str()).x > room)
                {
                    name.pop_back();
                    while (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0x80) // a whole UTF-8 letter
                        name.pop_back();
                    if (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0xC0)
                        name.pop_back();
                }
                name += "...";
            }
            ImGui::TextUnformatted(name.c_str());
            ImGui::PopFont();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip(entity.GetName().c_str());
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - countWidth));
            ImGui::TextColored(ps.Particles.size() >= (size_t)std::max(1, ps.MaxParticles) ? ImVec4(0.95f, 0.6f, 0.3f, 1.0f)
                                                                                          : ImVec4(0.85f, 0.88f, 0.92f, 1.0f),
                               "%s", count.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip(std::format("Tirik particle'lar: {} (maksimal {}){}", ps.Particles.size(), ps.MaxParticles,
                                        subParticles > 0 ? std::format("\nSub-emitter shablonlarida: {}", subParticles) : std::string()).c_str());

            // ---- controls
            const bool emitting = ps.IsEmitting();
            const float button = ImGui::GetFrameHeight();
            if (UI::ToolbarButton(emitting ? ICON_FA_PAUSE "###psPlay" : ICON_FA_PLAY "###psPlay",
                                  emitting ? "Pauza (particle'lar qotadi)" : ps.Paused ? "Davom ettirish" : "Ijro (boshidan)", ps.Paused,
                                  ImVec2(button * 1.4f, 0)))
            {
                if (emitting)
                    ps.Pause();
                else
                    ps.Play();
            }
            ImGui::SameLine(0.0f, 3.0f);
            if (UI::ToolbarButton(ICON_FA_ARROWS_ROTATE "###psRestart", "Qayta boshlash", false, ImVec2(button * 1.4f, 0)))
                ps.Restart();
            ImGui::SameLine(0.0f, 3.0f);
            if (UI::ToolbarButton(ICON_FA_STOP "###psStop", "To'xtatish (particle'lar o'chadi)", !ps.Playing && !ps.Paused,
                                  ImVec2(button * 1.4f, 0)))
                ps.Stop();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            const float delay = std::max(0.0f, ps.StartDelay), cycle = std::max(0.01f, ps.Duration);
            std::string time;
            float fraction = 0.0f;
            if (ps.Finished)
                time = "tugadi";
            else if (!ps.Playing && !ps.Paused)
                time = "to'xtatilgan";
            else if (ps.PlayTime < delay)
            {
                time = std::format("kechikish {:.1f} s", delay - ps.PlayTime);
                fraction = delay > 0.0f ? ps.PlayTime / delay : 0.0f;
            }
            else
            {
                const float t = std::min(ps.PlayTime - delay, cycle);
                time = std::format("{:.1f} / {:.1f} s{}", t, cycle, ps.Looping ? "  " ICON_FA_INFINITY : "");
                fraction = t / cycle;
            }
            ImGui::TextDisabled("%s", time.c_str());
            // The cycle as a thin bar.
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float inner = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, ImVec2(a.x + inner, a.y + 3.0f), IM_COL32(255, 255, 255, 30), 1.5f);
            dl->AddRectFilled(a, ImVec2(a.x + inner * std::clamp(fraction, 0.0f, 1.0f), a.y + 3.0f),
                              ps.Paused ? IM_COL32(220, 170, 70, 255) : UI::AccentU32(), 1.5f);
            ImGui::Dummy(ImVec2(inner, 3.0f));
            // What is not obvious from the picture.
            std::string notes;
            if (!ps.RendererEnabled)
                notes += ICON_FA_EYE_SLASH " chizilmaydi  ";
            if (ps.FollowCamera.Enabled)
                notes += ps.FollowActive ? ICON_FA_CLOUD_RAIN " kamera atrofida  " : ICON_FA_CLOUD_RAIN " kamera yo'q  ";
            if (ps.SimulationSpeed != 1.0f)
                notes += std::format(ICON_FA_GAUGE_HIGH " x{:.2f}  ", ps.SimulationSpeed);
            if (m_SceneState == SceneState::Edit && !ps.PlayInEditor)
                notes += "editorda o'chiq";
            if (!notes.empty())
                ImGui::TextDisabled("%s", notes.c_str());
        }
        ImGui::EndChild();
        s_Height = ImGui::GetItemRectSize().y;
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

}
