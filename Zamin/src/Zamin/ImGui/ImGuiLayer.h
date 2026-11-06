#pragma once

#include "Zamin/Core/Layer.h"

#include <glm/glm.hpp>

struct ImFont;

namespace Zamin {

    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnEvent(Event& e) override;

        void Begin();
        void End();

        // When true, mouse/keyboard events captured by ImGui are not passed to lower layers.
        void BlockEvents(bool block) { m_BlockEvents = block; }

        // The editor's dark theme with an accent colour (selected tabs, checkmarks, sliders ...).
        // Sets the style's colours and its sizes for a scale of 1.
        static void SetDarkTheme();
        static void SetDarkTheme(const glm::vec3& accent);
        static glm::vec3 GetAccentColor() { return s_Accent; }
        static glm::vec3 GetDefaultAccentColor() { return { 0.180f, 0.737f, 0.580f }; }

        // The UI font's size in pixels at a content scale of 1 (16 by default). The fonts are baked
        // again before the next frame (not in the middle of one).
        void SetFontSize(float pixels);
        float GetFontSize() const { return m_FontSize; }
        // The window's content scale (HiDPI) the fonts and sizes are multiplied by.
        float GetContentScale() const { return m_ContentScale; }

        static ImFont* GetBoldFont() { return s_BoldFont; }
        static ImFont* GetLargeIconFont() { return s_LargeIconFont; }
        static ImFont* GetTitleFont() { return s_TitleFont; }

    private:
        void BuildFonts();

    private:
        bool m_BlockEvents = true;
        float m_FontSize = 16.0f, m_BuiltFontSize = 0.0f;
        float m_ContentScale = 1.0f;
        static inline glm::vec3 s_Accent{ 0.180f, 0.737f, 0.580f };
        static inline ImFont* s_BoldFont = nullptr;
        static inline ImFont* s_LargeIconFont = nullptr;
        static inline ImFont* s_TitleFont = nullptr;
    };

}
