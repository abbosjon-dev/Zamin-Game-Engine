#pragma once

#include "Zamin/Core/Layer.h"

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

        static void SetDarkTheme();
        static ImFont* GetBoldFont() { return s_BoldFont; }
        static ImFont* GetLargeIconFont() { return s_LargeIconFont; }
        static ImFont* GetTitleFont() { return s_TitleFont; }

    private:
        bool m_BlockEvents = true;
        static inline ImFont* s_BoldFont = nullptr;
        static inline ImFont* s_LargeIconFont = nullptr;
        static inline ImFont* s_TitleFont = nullptr;
    };

}
