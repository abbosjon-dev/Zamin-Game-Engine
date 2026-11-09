#include "Zamin/ImGui/ImGuiLayer.h"
#include "Zamin/ImGui/Icons.h"
#include "Zamin/Editor/EditorPreferences.h"
#include "Zamin/Core/Application.h"
#include "Zamin/Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace Zamin {

    ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {}

    // `textOnlyAscii`: a font only icons are drawn with (the large one: content browser tiles, empty states) gets
    // just the ASCII letters of its text font - the rest of Latin and Cyrillic at 3x the size was most of the
    // atlas's pixels (start-up time, task 495).
    static ImFont* AddFontWithIcons(ImGuiIO& io, const std::filesystem::path& file, float size, float iconSize, bool textOnlyAscii = false)
    {
        if (!std::filesystem::exists(file))
            return io.Fonts->AddFontDefault();

        static const ImWchar asciiRanges[] = { 0x0020, 0x007E, 0 };
        static const ImWchar ranges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin-1
            0x0100, 0x017F, // Latin Extended-A
            0x02BB, 0x02BC, // ʻ ʼ
            0x0400, 0x04FF, // Cyrillic
            0x2010, 0x205E, // punctuation
            0x2190, 0x2199, // arrows (key names, "old -> new")
            0,
        };
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        ImFont* font = io.Fonts->AddFontFromFileTTF(file.string().c_str(), size, &cfg, textOnlyAscii ? asciiRanges : ranges);

        std::filesystem::path iconFile = Renderer::GetResourcesDirectory() / "fonts" / "fa-solid-900.ttf";
        if (std::filesystem::exists(iconFile))
        {
            // Only the icons the editor uses (Icons.h): baking Font Awesome's whole block into four fonts was most
            // of the first frame (~1400 glyphs each, oversampled).
            static const ImWchar iconRanges[] = {
#include "Zamin/ImGui/IconRanges.inl"
                0,
            };
            ImFontConfig iconCfg;
            iconCfg.MergeMode = true;
            iconCfg.PixelSnapH = true;
            iconCfg.GlyphMinAdvanceX = iconSize;
            iconCfg.GlyphOffset = ImVec2(0.0f, iconSize * 0.05f);
            io.Fonts->AddFontFromFileTTF(iconFile.string().c_str(), iconSize, &iconCfg, iconRanges);
        }
        return font;
    }

    void ImGuiLayer::BuildFonts()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        auto fonts = Renderer::GetResourcesDirectory() / "fonts";
        float base = m_FontSize * m_ContentScale;
        io.FontDefault = AddFontWithIcons(io, fonts / "Inter-Regular.ttf", base, base * 0.9f);
        s_BoldFont = AddFontWithIcons(io, fonts / "Inter-Bold.ttf", base, base * 0.9f);
        s_TitleFont = AddFontWithIcons(io, fonts / "Inter-Bold.ttf", base * 1.6f, base * 1.4f);
        s_LargeIconFont = AddFontWithIcons(io, fonts / "Inter-Regular.ttf", base * 3.0f, base * 3.0f, true);
        m_BuiltFontSize = m_FontSize;
    }

    void ImGuiLayer::SetFontSize(float pixels)
    {
        m_FontSize = std::clamp(std::isfinite(pixels) ? pixels : 16.0f, 8.0f, 48.0f);
    }

    void ImGuiLayer::OnAttach()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigWindowsMoveFromTitleBarOnly = true;

        Application& app = Application::Get();
        m_ContentScale = std::max(1.0f, app.GetWindow().GetContentScale());
        BuildFonts();

        SetDarkTheme(s_Accent);
        ImGui::GetStyle().ScaleAllSizes(m_ContentScale);

        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 410");
    }

    void ImGuiLayer::OnDetach()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::OnEvent(Event& e)
    {
        if (!m_BlockEvents)
            return;
        ImGuiIO& io = ImGui::GetIO();
        e.Handled |= e.IsInCategory(EventCategoryMouse) && io.WantCaptureMouse;
        e.Handled |= e.IsInCategory(EventCategoryKeyboard) && io.WantCaptureKeyboard;
    }

    void ImGuiLayer::Begin()
    {
        // A new font size: the atlas is baked again between two frames (its texture too).
        if (m_FontSize != m_BuiltFontSize)
        {
            BuildFonts();
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui_ImplOpenGL3_CreateFontsTexture();
        }
        ImGui_ImplOpenGL3_NewFrame();
        // The atlas's pixels are on the GPU once the backend made its texture (in its first NewFrame): the CPU
        // copies (alpha + RGBA, several MB) are let go (task 496). A new font size bakes them again above.
        if (ImFontAtlas* atlas = ImGui::GetIO().Fonts; atlas->TexPixelsAlpha8 || atlas->TexPixelsRGBA32)
        {
            ZM_CORE_INFO("Editor shrift atlasi: {}x{} ({} ta shrift); CPU nusxasi bo'shatildi", atlas->TexWidth, atlas->TexHeight, atlas->Fonts.Size);
            atlas->ClearTexData();
        }
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void ImGuiLayer::End()
    {
        ImGuiIO& io = ImGui::GetIO();
        Application& app = Application::Get();
        io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

        ImGui::Render();
        RenderCommand::SetViewport(0, 0, app.GetWindow().GetFramebufferWidth(), app.GetWindow().GetFramebufferHeight());
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void ImGuiLayer::SetDarkTheme()
    {
        SetDarkTheme(GetDefaultAccentColor());
    }

    void ImGuiLayer::SetDarkTheme(const glm::vec3& accentColor)
    {
        s_Accent = glm::clamp(accentColor, glm::vec3(0.0f), glm::vec3(1.0f));
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(10, 10);
        style.FramePadding = ImVec2(8, 5);
        style.CellPadding = ImVec2(6, 4);
        style.ItemSpacing = ImVec2(8, 6);
        style.ItemInnerSpacing = ImVec2(6, 4);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 13.0f;
        style.GrabMinSize = 10.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        style.WindowRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 5.0f;
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.SeparatorTextBorderSize = 1.0f;
        style.DockingSeparatorSize = 2.0f;

        // "Zamin" palette: deep slate background, emerald/teal accent.
        const ImVec4 bg0     = ImVec4(0.067f, 0.075f, 0.090f, 1.0f);
        const ImVec4 bg1     = ImVec4(0.094f, 0.102f, 0.122f, 1.0f);
        const ImVec4 bg2     = ImVec4(0.125f, 0.137f, 0.161f, 1.0f);
        const ImVec4 bg3     = ImVec4(0.165f, 0.180f, 0.212f, 1.0f);
        const ImVec4 bg4     = ImVec4(0.215f, 0.235f, 0.275f, 1.0f);
        // The accent's lighter (hovered) and darker (pressed) shades follow it.
        const glm::vec3 hover = EditorThemeSettings::GetHoverShade(s_Accent);
        const glm::vec3 dark = EditorThemeSettings::GetDarkShade(s_Accent);
        const ImVec4 accent  = ImVec4(s_Accent.r, s_Accent.g, s_Accent.b, 1.0f);
        const ImVec4 accentH = ImVec4(hover.r, hover.g, hover.b, 1.0f);
        const ImVec4 accentD = ImVec4(dark.r, dark.g, dark.b, 1.0f);
        const ImVec4 text    = ImVec4(0.900f, 0.920f, 0.940f, 1.0f);
        const ImVec4 textDim = ImVec4(0.520f, 0.560f, 0.610f, 1.0f);

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = text;
        c[ImGuiCol_TextDisabled] = textDim;
        c[ImGuiCol_WindowBg] = bg1;
        c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_PopupBg] = ImVec4(bg2.x, bg2.y, bg2.z, 0.98f);
        c[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
        c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_FrameBg] = bg0;
        c[ImGuiCol_FrameBgHovered] = bg3;
        c[ImGuiCol_FrameBgActive] = bg4;
        c[ImGuiCol_TitleBg] = bg0;
        c[ImGuiCol_TitleBgActive] = bg0;
        c[ImGuiCol_TitleBgCollapsed] = bg0;
        c[ImGuiCol_MenuBarBg] = bg0;
        c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab] = bg3;
        c[ImGuiCol_ScrollbarGrabHovered] = bg4;
        c[ImGuiCol_ScrollbarGrabActive] = accentD;
        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accentH;
        c[ImGuiCol_Button] = bg3;
        c[ImGuiCol_ButtonHovered] = bg4;
        c[ImGuiCol_ButtonActive] = accentD;
        c[ImGuiCol_Header] = bg3;
        c[ImGuiCol_HeaderHovered] = bg4;
        c[ImGuiCol_HeaderActive] = accentD;
        c[ImGuiCol_Separator] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
        c[ImGuiCol_SeparatorHovered] = accentD;
        c[ImGuiCol_SeparatorActive] = accent;
        c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ResizeGripHovered] = accentD;
        c[ImGuiCol_ResizeGripActive] = accent;
        c[ImGuiCol_Tab] = bg0;
        c[ImGuiCol_TabHovered] = bg3;
        c[ImGuiCol_TabSelected] = bg1;
        c[ImGuiCol_TabSelectedOverline] = accent;
        c[ImGuiCol_TabDimmed] = bg0;
        c[ImGuiCol_TabDimmedSelected] = bg1;
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.6f);
        c[ImGuiCol_DockingEmptyBg] = bg0;
        c[ImGuiCol_PlotLines] = accent;
        c[ImGuiCol_PlotLinesHovered] = accentH;
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_PlotHistogramHovered] = accentH;
        c[ImGuiCol_TableHeaderBg] = bg2;
        c[ImGuiCol_TableBorderStrong] = ImVec4(0, 0, 0, 0.5f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0, 0, 0, 0.3f);
        c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        c[ImGuiCol_DragDropTarget] = accentH;
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.7f);
        c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.2f, 0.2f, 0.2f, 0.2f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
    }

}
