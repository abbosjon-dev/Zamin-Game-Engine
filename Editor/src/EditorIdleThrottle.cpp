// Idle throttling: the editor draws fewer frames while its window is not in front or nothing is
// happening, so it does not keep a CPU core and the GPU busy for nothing. Input wakes it at once
// (the frame waits for the window's events, see Application::SetFrameRateLimit).
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>

#include <format>

namespace Zamin {

    void EditorLayer::UpdateIdleThrottle()
    {
        Application& app = Application::Get();
        IdleThrottleState& state = m_IdleState;
        state = IdleThrottleState();
        state.Focused = app.IsWindowFocused();
        state.Running = m_SceneState != SceneState::Edit && m_ActiveScene && !m_ActiveScene->IsPaused();

        // Things moving that should stay smooth: the cameras, previews, a drag or a field typed into.
        bool cameraMoving = m_EditorCamera.IsFlying() || m_EditorCamera.IsTransitioning();
        for (const ExtraSceneView& view : m_ExtraViews)
            cameraMoving |= view.Visible && (view.Camera.IsFlying() || view.Camera.IsTransitioning());
        bool particlePreview = false;
        if (m_SceneState == SceneState::Edit)
            if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity(); selected && selected.GetScene() == m_ActiveScene.get())
                if (const auto* ps = selected.TryGetComponent<ParticleSystemComponent>())
                    particlePreview = ps->Playing && !ps->Paused;
        state.Animating = cameraMoving || particlePreview || m_AnimationPanel.IsPlaying() || ImGui::IsAnyItemActive() ||
                          ImGui::IsAnyMouseDown() || m_GizmoEdit.IsActive();

        // Work that goes on frame by frame (or that the user waits for).
        state.PendingWork = m_Building || m_Exporting || !m_ThumbnailQueue.empty() || !m_PendingLayout.empty() ||
                            m_PendingBuiltInLayout.has_value() || m_ContentBrowserPanel.GetThumbnails().GetStats().Waiting > 0;
        state.Automation = Application::IsAutomationRun();
        const double sinceWake = ImGui::GetTime() - m_WakeTime;
        state.SecondsSinceInput = std::min(app.GetSecondsSinceInput(), sinceWake < 0.0 ? 0.0 : sinceWake);

        m_FrameLimit = IdleThrottle::Decide(m_Preferences.Idle, state);
        app.SetFrameRateLimit(m_FrameLimit.IsLimited() ? m_FrameLimit.FPS : 0.0f);
    }

    void EditorLayer::UI_IdlePreferences(bool& changed)
    {
        const float em = ImGui::GetFontSize();
        IdleThrottleSettings& idle = m_Preferences.Idle;
        ImGui::SeparatorText(Tr("Bo'sh turganda kadrlarni kamaytirish"));
        ImGui::TextWrapped("Editor oynasi orqada bo'lsa yoki hech narsa bo'lmayotgan bo'lsa (kiritish yo'q, o'yin ishlamayapti, "
                           "ko'rib chiqish ijro etilmayapti, hech narsa yuklanmayapti) editor kamroq kadr chizadi - protsessor va "
                           "videokarta bekorga ishlamaydi. Sichqoncha yoki klaviatura darhol to'liq tezlikka qaytaradi.");
        ImGui::Spacing();
        changed |= ImGui::Checkbox(Tr("Yoqilgan"), &idle.Enabled);
        ImGui::BeginDisabled(!idle.Enabled);
        ImGui::SetNextItemWidth(em * 14.0f);
        changed |= ImGui::SliderFloat("Oyna orqada (FPS)", &idle.UnfocusedFPS, IdleThrottleSettings::MinFPS, 60.0f, "%.0f");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Boshqa dastur oldinda bo'lganda (o'yin ishlayotganda cheklanmaydi)");
        ImGui::SetNextItemWidth(em * 14.0f);
        changed |= ImGui::SliderFloat("Bo'sh turganda (FPS)", &idle.IdleFPS, IdleThrottleSettings::MinFPS, IdleThrottleSettings::MaxFPS, "%.0f");
        ImGui::SetNextItemWidth(em * 14.0f);
        changed |= ImGui::SliderFloat("Kutish (s)", &idle.IdleDelay, IdleThrottleSettings::MinDelay, IdleThrottleSettings::MaxDelay, "%.1f s",
                                      ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Oxirgi kiritishdan keyin shuncha vaqt to'liq tezlikda chiziladi (tooltip, animatsiyalar tugaydi)");
        ImGui::EndDisabled();
        if (changed)
            idle.Sanitize();

        // What it is doing right now.
        ImGui::SeparatorText(Tr("Hozir"));
        const IdleThrottleState& s = m_IdleState;
        const char* why = !idle.Enabled ? "o'chirilgan"
                        : s.Automation ? "avtomatlashtirish (ZAMIN_MAX_FRAMES) - cheklanmaydi"
                        : s.Running ? "o'yin / simulyatsiya ishlayapti"
                        : s.PendingWork ? "fonda ish bor (kompilyatsiya, yuklash, rasmlar)"
                        : !s.Focused ? "oyna orqada"
                        : s.Animating ? "ko'rib chiqish ijro etilmoqda yoki nimadir surilmoqda"
                        : s.SecondsSinceInput < idle.IdleDelay ? "yaqinda kiritish bo'ldi"
                                                                : "bo'sh turibdi";
        if (m_FrameLimit.IsLimited())
            ImGui::TextColored(UI::AccentColor(), ICON_FA_LEAF "  %.0f FPS gacha cheklangan (%s)", m_FrameLimit.FPS, why);
        else
            ImGui::Text(ICON_FA_GAUGE_HIGH "  To'liq tezlik (%s)", why);
        ImGui::TextDisabled("Hozirgi FPS: %.0f   |   oxirgi kiritishdan beri: %.1f s", Application::Get().GetFPS(), s.SecondsSinceInput);
        ImGui::Spacing();
        if (ImGui::Button(Tr(ICON_FA_ARROW_ROTATE_LEFT "  Standart qiymatlar")))
        {
            idle = IdleThrottleSettings();
            changed = true;
        }
    }

}
