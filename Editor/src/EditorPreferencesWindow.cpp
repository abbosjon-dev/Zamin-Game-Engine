// The editor's preferences (Tahrirlash > Sozlamalar): the theme's accent colour and font size, the
// play-mode tint of the UI, the console's options and the editor camera's defaults, stored per user
// in preferences.json; the ImGui style made from them.
#include "EditorLayer.h"
#include "UI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <span>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {
        // The theme's colours before the play-mode tint (the preferences' preview shows both).
        std::array<ImVec4, ImGuiCol_COUNT> s_UntintedColors{};

        // The UI chrome the play-mode tint colours: backgrounds of windows, title bars, tabs, the
        // menu / status bars, frames, buttons, headers (text and the accent stay as they are).
        constexpr ImGuiCol s_TintedColors[] = {
            ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_PopupBg, ImGuiCol_MenuBarBg,
            ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed,
            ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected,
            ImGuiCol_DockingEmptyBg, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
            ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_Header, ImGuiCol_HeaderHovered,
            ImGuiCol_ScrollbarGrab, ImGuiCol_ScrollbarGrabHovered, ImGuiCol_TableHeaderBg,
        };

        ImVec4 ToImVec4(const glm::vec4& c) { return ImVec4(c.r, c.g, c.b, c.a); }
        glm::vec4 ToVec4(const ImVec4& c) { return { c.x, c.y, c.z, c.w }; }

        // A row of colour swatches; returns the index clicked (-1: none). `current` gets a frame.
        int SwatchRow(const char* id, std::span<const ColorPreset> presets, int current, const std::function<glm::vec3(const glm::vec3&)>& shown)
        {
            int clicked = -1;
            const float size = ImGui::GetFrameHeight() * 1.25f;
            ImGui::PushID(id);
            for (int i = 0; i < (int)presets.size(); i++)
            {
                if (i > 0)
                    ImGui::SameLine(0.0f, 6.0f);
                const glm::vec3 c = shown(presets[i].Color);
                ImGui::PushID(i);
                if (ImGui::ColorButton("##swatch", ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                       ImVec2(size, size)))
                    clicked = i;
                if (ImGui::IsItemHovered())
                    UI::Tooltip(presets[i].Name);
                if (i == current)
                {
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), IM_COL32(240, 242, 245, 255), 5.0f, 0, 2.0f);
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            return clicked;
        }
    }

    void EditorLayer::LoadPreferences()
    {
        const fs::path path = EditorPreferences::GetDefaultPath();
        std::error_code ec;
        if (fs::exists(path, ec) && !m_Preferences.Load(path))
            ZM_WARN("Editor sozlamalari o'qilmadi (standart qiymatlar ishlatiladi): {}", path.string());
        m_Preferences.Sanitize();
        m_ConsolePanel.SetPreferences(m_Preferences.Console);
        ApplyCameraDefaults(true);
        ApplyShortcutPreferences(); // rebound keys over the defaults
        m_PreferencesDirty = false;
    }

    void EditorLayer::SavePreferences()
    {
        m_Preferences.Console = m_ConsolePanel.GetPreferences();
        m_Preferences.Sanitize();
        if (!m_Preferences.Save(EditorPreferences::GetDefaultPath()))
            ZM_WARN("Editor sozlamalari saqlanmadi");
        m_PreferencesDirty = false;
    }

    void EditorLayer::ApplyCameraDefaults(bool flySpeed)
    {
        const EditorCameraDefaults& defaults = m_Preferences.Camera;
        for (int i = 0; i <= SceneViewSettings::Count; i++)
        {
            EditorCamera& camera = GetSceneViewCamera(i);
            camera.SetProjection(defaults.FOV, defaults.NearClip, defaults.FarClip);
            camera.FlyAcceleration = defaults.FlyAcceleration;
            camera.LookSensitivity = defaults.LookSensitivity;
            camera.InvertY = defaults.InvertY;
            if (flySpeed)
                camera.FlySpeed = defaults.FlySpeed;
        }
    }

    bool EditorLayer::IsPlayTintActive() const
    {
        return m_Preferences.PlayTint.IsActive(m_SceneState == SceneState::Play, m_SceneState == SceneState::Simulate);
    }

    void EditorLayer::UpdateEditorTheme()
    {
        ImGuiLayer* imgui = Application::Get().GetImGuiLayer();
        if (!imgui)
            return;
        const EditorThemeSettings& theme = m_Preferences.Theme;
        // A new font size: the fonts are baked again before this frame's UI.
        if (imgui->GetFontSize() != theme.FontSize)
            imgui->SetFontSize(theme.FontSize);

        const bool tinted = IsPlayTintActive();
        const glm::vec3 tint = tinted ? m_Preferences.PlayTint.GetMultiplier() : glm::vec3(1.0f);
        if (m_AppliedTheme.Accent == theme.Accent && m_AppliedTheme.FontSize == theme.FontSize && m_AppliedTheme.Tinted == tinted &&
            m_AppliedTheme.Tint == tint)
            return;

        // From the default style: the theme's colours and sizes, scaled for the content scale and the
        // font size, then the tint over the chrome's colours.
        ImGuiStyle& style = ImGui::GetStyle();
        style = ImGuiStyle();
        ImGuiLayer::SetDarkTheme(theme.Accent);
        style.ScaleAllSizes(imgui->GetContentScale() * theme.GetUIScale());
        std::copy(std::begin(style.Colors), std::end(style.Colors), s_UntintedColors.begin());
        if (tinted)
            for (ImGuiCol c : s_TintedColors)
                style.Colors[c] = ToImVec4(m_Preferences.PlayTint.Apply(ToVec4(style.Colors[c])));
        m_AppliedTheme = { theme.Accent, theme.FontSize, tinted, tint };
    }

    void EditorLayer::UI_Preferences()
    {
        if (!m_ShowPreferences)
            return;
        const float em = ImGui::GetFontSize();
        ImGui::SetNextWindowSize(ImVec2(em * 46.0f, em * 31.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(ICON_FA_USER_GEAR "  Sozlamalar###Preferences", &m_ShowPreferences, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        // ---- pages
        static const char* s_Pages[] = {
            ICON_FA_PALETTE "  Ko'rinish",
            ICON_FA_BRUSH "  O'yin rejimi",
            ICON_FA_TERMINAL "  Konsol",
            ICON_FA_CAMERA "  Editor kamerasi",
            ICON_FA_FLOPPY_DISK "  Saqlash va undo",
            ICON_FA_PEN_TO_SQUARE "  Tashqi muharrir",
            ICON_FA_LEAF "  Ishlash",
        };
        if (ImGui::BeginChild("##pages", ImVec2(em * 11.0f, -ImGui::GetFrameHeightWithSpacing() - 6.0f), ImGuiChildFlags_Borders))
        {
            for (int i = 0; i < (int)std::size(s_Pages); i++)
                if (ImGui::Selectable(s_Pages[i], m_PreferencesPage == i, 0, ImVec2(0, ImGui::GetFrameHeight())))
                    m_PreferencesPage = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();

        bool changed = false;
        if (ImGui::BeginChild("##page", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 6.0f)))
        {
            EditorThemeSettings& theme = m_Preferences.Theme;
            PlayModeTintSettings& tint = m_Preferences.PlayTint;
            switch (m_PreferencesPage)
            {
                case 0:
                {
                    ImGui::SeparatorText("Asosiy rang (accent)");
                    ImGui::TextDisabled("Tanlangan tablar, belgilar, slayderlar va ajratib ko'rsatishlar rangi");
                    int clicked = SwatchRow("accent", EditorThemeSettings::GetAccentPresets(), theme.FindAccentPreset(),
                                            [](const glm::vec3& c) { return c; });
                    if (clicked >= 0)
                    {
                        theme.Accent = EditorThemeSettings::GetAccentPresets()[clicked].Color;
                        changed = true;
                    }
                    ImGui::SameLine(0.0f, 14.0f);
                    if (ImGui::ColorEdit3("Boshqa rang##accent", &theme.Accent.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                        changed = true;
                    // How it looks: an active toggle, a checkbox, a slider, a progress bar.
                    ImGui::Spacing();
                    static bool s_PreviewCheck = true;
                    static float s_PreviewSlider = 0.6f;
                    UI::ToolbarButton(ICON_FA_MAGNET "  Faol tugma", nullptr, true);
                    ImGui::SameLine();
                    ImGui::Checkbox("Belgi", &s_PreviewCheck);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(em * 8.0f);
                    ImGui::SliderFloat("##previewSlider", &s_PreviewSlider, 0.0f, 1.0f);
                    ImGui::SameLine();
                    ImGui::ProgressBar(s_PreviewSlider, ImVec2(em * 6.0f, 0.0f));

                    ImGui::SeparatorText("Shrift o'lchami");
                    ImGui::TextDisabled("Butun UI (oraliqlar, tugmalar) shu nisbatda kattalashadi / kichrayadi");
                    for (float size : { 13.0f, 14.0f, 16.0f, 18.0f, 20.0f })
                    {
                        const bool current = std::abs(theme.FontSize - size) < 0.5f;
                        if (current) ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor());
                        std::string label = std::format("{:.0f} px{}", size, size == EditorThemeSettings::DefaultFontSize ? " (standart)" : "");
                        if (ImGui::Button(label.c_str()) && !current)
                        {
                            theme.FontSize = size;
                            changed = true;
                        }
                        if (current) ImGui::PopStyleColor();
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();
                    // The slider applies when it is let go (the fonts are baked again for a new size).
                    static float s_DraftSize = EditorThemeSettings::DefaultFontSize;
                    ImGui::SetNextItemWidth(em * 14.0f);
                    ImGui::SliderFloat("##fontSize", &s_DraftSize, EditorThemeSettings::MinFontSize, EditorThemeSettings::MaxFontSize, "%.0f px");
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        theme.FontSize = s_DraftSize;
                        theme.Sanitize();
                        changed = true;
                    }
                    if (!ImGui::IsItemActive())
                        s_DraftSize = theme.FontSize;
                    ImGui::SameLine();
                    ImGui::TextDisabled("x%.2f", theme.GetUIScale());
                    ImGui::Spacing();
                    if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "  Standart ko'rinish"))
                    {
                        theme = EditorThemeSettings();
                        changed = true;
                    }
                    break;
                }
                case 1:
                {
                    ImGui::SeparatorText("O'yin rejimida UI rang tusi");
                    ImGui::TextWrapped("O'yin ishlayotganda editor oynalari, sarlavhalar, tablar va menyular shu rangga bo'yaladi - "
                                       "o'yin rejimida qilingan o'zgarishlar To'xtatishda yo'qolishini unutmaslik uchun.");
                    changed |= ImGui::Checkbox("O'yin rejimida bo'yash", &tint.Enabled);
                    ImGui::BeginDisabled(!tint.Enabled);
                    changed |= ImGui::Checkbox("Fizika simulyatsiyasida ham", &tint.InSimulate);
                    // The swatches show a mid grey of the UI under each tint.
                    const PlayModeTintSettings shownTint = tint;
                    int clicked = SwatchRow("tint", PlayModeTintSettings::GetPresets(), tint.FindPreset(), [&](const glm::vec3& c) {
                        PlayModeTintSettings t = shownTint;
                        t.Color = c;
                        t.Strength = 1.0f;
                        return glm::vec3(t.Apply(glm::vec4(0.32f, 0.34f, 0.38f, 1.0f)));
                    });
                    if (clicked >= 0)
                    {
                        tint.Color = PlayModeTintSettings::GetPresets()[clicked].Color;
                        changed = true;
                    }
                    ImGui::SameLine(0.0f, 14.0f);
                    ImGui::SetNextItemWidth(em * 12.0f);
                    if (ImGui::ColorEdit3("Ko'paytuvchi##tint", &tint.Color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR |
                                                                              ImGuiColorEditFlags_NoInputs))
                    {
                        tint.Sanitize();
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Har bir rang kanali shu songa ko'paytiriladi (1 - o'zgarmaydi, 2 gacha)");
                    ImGui::SetNextItemWidth(em * 14.0f);
                    changed |= ImGui::SliderFloat("Kuchi", &tint.Strength, 0.0f, 1.0f, "%.2f");

                    // Before / after: a small window drawn in the theme's colours, and under the tint.
                    ImGui::Spacing();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const float w = em * 12.0f, h = em * 5.5f, title = em * 1.5f;
                    auto mock = [&](const char* label, bool tinted) {
                        const ImVec2 a = ImGui::GetCursorScreenPos();
                        auto col = [&](ImGuiCol c) {
                            glm::vec4 v = ToVec4(s_UntintedColors[c]);
                            if (tinted)
                                v = tint.Apply(v);
                            return ImGui::ColorConvertFloat4ToU32(ToImVec4(v));
                        };
                        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col(ImGuiCol_WindowBg), 6.0f);
                        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + title), col(ImGuiCol_TitleBgActive), 6.0f, ImDrawFlags_RoundCornersTop);
                        dl->AddRectFilled(ImVec2(a.x + 10, a.y + title + 10), ImVec2(a.x + w - 10, a.y + title + 10 + em * 1.3f), col(ImGuiCol_FrameBg), 4.0f);
                        dl->AddRectFilled(ImVec2(a.x + 10, a.y + h - em * 1.6f - 8), ImVec2(a.x + w * 0.5f, a.y + h - 8), col(ImGuiCol_Button), 4.0f);
                        dl->AddRect(a, ImVec2(a.x + w, a.y + h), IM_COL32(0, 0, 0, 120), 6.0f);
                        dl->AddText(ImVec2(a.x + 8, a.y + (title - em) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label);
                        ImGui::Dummy(ImVec2(w, h));
                    };
                    mock(ICON_FA_FEATHER "  Tahrirlash", false);
                    ImGui::SameLine(0.0f, 16.0f);
                    mock(ICON_FA_PLAY "  O'yin", true);
                    ImGui::EndDisabled();
                    if (IsPlayTintActive())
                        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.5f, 1.0f), ICON_FA_PLAY "  Hozir o'yin rejimi: UI bo'yalgan");
                    ImGui::Spacing();
                    if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "  Standart tus"))
                    {
                        tint = PlayModeTintSettings();
                        changed = true;
                    }
                    break;
                }
                case 2:
                {
                    ConsolePreferences console = m_ConsolePanel.GetPreferences();
                    bool consoleChanged = false;
                    ImGui::SeparatorText("Konsol");
                    consoleChanged |= ImGui::Checkbox("O'yin boshlanganda (Play) konsolni tozalash", &console.ClearOnPlay);
                    consoleChanged |= ImGui::Checkbox("Vaqtni ko'rsatish", &console.ShowTimestamps);
                    consoleChanged |= ImGui::Checkbox("Bir xil xabarlarni guruhlash (soni bilan)", &console.Collapse);
                    consoleChanged |= ImGui::Checkbox("Avto-skroll (yangi xabarga)", &console.AutoScroll);
                    ImGui::SeparatorText("Ko'rsatiladigan darajalar");
                    consoleChanged |= ImGui::Checkbox(ICON_FA_BUG "  Trace", &console.ShowTrace);
                    ImGui::SameLine();
                    consoleChanged |= ImGui::Checkbox(ICON_FA_CIRCLE_INFO "  Ma'lumot", &console.ShowInfo);
                    ImGui::SameLine();
                    consoleChanged |= ImGui::Checkbox(ICON_FA_TRIANGLE_EXCLAMATION "  Ogohlantirish", &console.ShowWarn);
                    ImGui::SameLine();
                    consoleChanged |= ImGui::Checkbox(ICON_FA_CIRCLE_XMARK "  Xato", &console.ShowError);
                    ImGui::Spacing();
                    ImGui::TextDisabled("Qidiruv konsolning o'zida: matn (katta-kichik harfsiz) yoki regex (.*)");
                    if (consoleChanged)
                    {
                        m_ConsolePanel.SetPreferences(console);
                        changed = true;
                    }
                    break;
                }
                case 3:
                {
                    EditorCameraDefaults& camera = m_Preferences.Camera;
                    ImGui::SeparatorText("Editor kamerasi");
                    ImGui::TextDisabled("Hamma ko'rinishlarning kamerasi; har yangi sessiya shu qiymatlar bilan boshlanadi (viewport'dagi "
                                        ICON_FA_VIDEO " tugmasi ham shularni o'zgartiradi)");
                    bool projection = false, speed = false;
                    ImGui::SetNextItemWidth(em * 14.0f);
                    projection |= ImGui::SliderFloat("Ko'rish burchagi (FOV)", &camera.FOV, EditorCameraDefaults::MinFOV, EditorCameraDefaults::MaxFOV, "%.0f\xc2\xb0");
                    ImGui::SetNextItemWidth(em * 14.0f);
                    projection |= ImGui::DragFloat("Yaqin chegara (near)", &camera.NearClip, 0.005f, EditorCameraDefaults::MinNear, EditorCameraDefaults::MaxNear, "%.3f");
                    ImGui::SetNextItemWidth(em * 14.0f);
                    projection |= ImGui::DragFloat("Uzoq chegara (far)", &camera.FarClip, 10.0f, 1.0f, EditorCameraDefaults::MaxFar, "%.0f");
                    ImGui::SetNextItemWidth(em * 14.0f);
                    speed |= ImGui::DragFloat("Uchish tezligi", &camera.FlySpeed, 0.1f, EditorCameraDefaults::MinFlySpeed, EditorCameraDefaults::MaxFlySpeed, "%.1f");
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("O'ng tugma + WASD bilan uchish tezligi (uchayotganda g'ildirak bilan ham o'zgaradi)");
                    ImGui::SetNextItemWidth(em * 14.0f);
                    projection |= ImGui::SliderFloat("Tezlanish", &camera.FlyAcceleration, 0.0f, EditorCameraDefaults::MaxAcceleration,
                                                     camera.FlyAcceleration > 0.0f ? "+%.1fx / s" : "yo'q");
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Tugmalar uzoq bosilsa uchish tezligi oshib boradi (ko'pi bilan 4x)");
                    ImGui::SetNextItemWidth(em * 14.0f);
                    projection |= ImGui::SliderFloat("Sichqoncha sezgirligi", &camera.LookSensitivity, EditorCameraDefaults::MinSensitivity,
                                                     EditorCameraDefaults::MaxSensitivity, "%.2fx", ImGuiSliderFlags_Logarithmic);
                    projection |= ImGui::Checkbox("Y o'qini teskari qilish", &camera.InvertY);
                    if (projection || speed)
                    {
                        camera.Sanitize();
                        ApplyCameraDefaults(speed);
                        changed = true;
                    }
                    ImGui::Spacing();
                    ImGui::TextDisabled("Hozirgi kamera: FOV %.0f\xc2\xb0, near %.3g, far %.0f, tezlik %.1f", m_EditorCamera.GetFOV(),
                                        m_EditorCamera.GetNearClip(), m_EditorCamera.GetFarClip(), m_EditorCamera.FlySpeed);
                    if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "  Standart qiymatlar"))
                    {
                        camera = EditorCameraDefaults();
                        ApplyCameraDefaults(true);
                        changed = true;
                    }
                    break;
                }
                case 4:
                    UI_AutosavePreferences(changed);
                    break;
                case 5:
                    UI_ExternalEditorPreferences(changed);
                    break;
                case 6:
                    UI_IdlePreferences(changed);
                    break;
            }
        }
        ImGui::EndChild();

        // ---- bottom: where it is stored, reset everything
        ImGui::Separator();
        // The file, under the home directory as "~/..." (the full path in the tooltip).
        const fs::path file = EditorPreferences::GetDefaultPath();
        std::string shown = file.string();
        if (const char* home = std::getenv("HOME"); home && *home && shown.starts_with(home))
            shown = "~" + shown.substr(std::strlen(home));
        const char* resetLabel = ICON_FA_ARROW_ROTATE_LEFT "  Hammasini standartga qaytarish";
        const float resetWidth = ImGui::CalcTextSize(resetLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float pathRight = ImGui::GetWindowContentRegionMax().x - resetWidth - 12.0f;
        ImGui::AlignTextToFramePadding();
        const ImVec2 pathAt = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(pathAt, ImVec2(ImGui::GetWindowPos().x + pathRight, pathAt.y + ImGui::GetFrameHeight()), true);
        ImGui::TextDisabled(ICON_FA_FLOPPY_DISK "  %s", shown.c_str());
        ImGui::PopClipRect();
        if (ImGui::IsItemHovered())
            UI::Tooltip(file.string().c_str());
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - resetWidth);
        if (ImGui::Button(resetLabel))
        {
            m_Preferences = EditorPreferences();
            m_ConsolePanel.SetPreferences(m_Preferences.Console);
            ApplyCameraDefaults(true);
            m_Shortcuts.ResetAll();
            changed = true;
        }
        if (changed)
        {
            m_Preferences.Sanitize();
            MarkPreferencesDirty();
        }
        ImGui::End();
    }

}
