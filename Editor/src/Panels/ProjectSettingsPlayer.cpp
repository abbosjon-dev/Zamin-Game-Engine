// Project settings: the Player category (company / product / version, the window, the icon).
#include "ProjectSettingsPanel.h"
#include "../UI.h"

#include <Zamin/Utils/PlatformUtils.h>

#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <unordered_map>

namespace Zamin {

    namespace {
        const ImVec4 s_WarnColor(0.95f, 0.72f, 0.32f, 1.0f);
        const ImVec4 s_ErrorColor(0.95f, 0.45f, 0.40f, 1.0f);

        struct ResolutionPreset
        {
            uint32_t Width, Height;
            const char* Name;
        };
        const ResolutionPreset s_Resolutions[] = {
            { 1280, 720, "HD" },  { 1366, 768, "" },        { 1600, 900, "HD+" }, { 1920, 1080, "Full HD" }, { 2560, 1440, "QHD" },
            { 3840, 2160, "4K" }, { 1280, 800, "16:10" },   { 1024, 768, "4:3" }, { 800, 600, "4:3" },
        };
        const int s_FrameRates[] = { 0, 30, 60, 75, 120, 144, 240 };

        // Text being typed into the fields below (the value changes when the typing ends).
        std::unordered_map<ImGuiID, std::string>& EditBuffers()
        {
            static std::unordered_map<ImGuiID, std::string> s_Buffers;
            return s_Buffers;
        }

        // A text field whose value changes when editing ends (Enter / the focus leaves / Tab), not on
        // every key: SaveData folders and the window title follow these names.
        bool CommitText(const char* id, std::string& value, const char* hint)
        {
            const ImGuiID key = ImGui::GetID(id);
            auto& buffers = EditBuffers();
            auto it = buffers.find(key);
            if (it == buffers.end())
                it = buffers.emplace(key, value).first;
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint(id, hint, &it->second);
            bool committed = false;
            if (ImGui::IsItemDeactivated())
            {
                if (it->second != value)
                {
                    value = it->second;
                    committed = true;
                }
                buffers.erase(it);
            }
            else if (!ImGui::IsItemActive())
                buffers.erase(it); // not being edited: shows the value as it is next frame
            return committed;
        }

        // A mock title bar: the icon and the title as the player's window shows them.
        void TitleBarPreview(const std::string& title, const Ref<Texture2D>& icon, const char* modeText)
        {
            const float height = ImGui::GetFrameHeight() * 1.25f;
            const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 26.0f);
            const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + width, min.y + height);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float rounding = ImGui::GetStyle().FrameRounding + 2.0f;
            draw->AddRectFilled(min, max, IM_COL32(44, 47, 56, 255), rounding, ImDrawFlags_RoundCornersTop);
            draw->AddRectFilled(ImVec2(min.x, max.y), ImVec2(max.x, max.y + height * 1.6f), IM_COL32(24, 26, 31, 255), rounding,
                                ImDrawFlags_RoundCornersBottom);
            draw->AddRect(min, ImVec2(max.x, max.y + height * 1.6f), IM_COL32(70, 74, 86, 255), rounding);
            float x = min.x + height * 0.3f;
            const float iconSize = height * 0.62f;
            const float iconY = min.y + (height - iconSize) * 0.5f;
            if (icon)
                draw->AddImage((ImTextureID)(uintptr_t)icon->GetRendererID(), ImVec2(x, iconY), ImVec2(x + iconSize, iconY + iconSize), ImVec2(0, 1),
                               ImVec2(1, 0));
            else
                draw->AddRectFilled(ImVec2(x, iconY), ImVec2(x + iconSize, iconY + iconSize), IM_COL32(90, 96, 110, 255), 3.0f);
            x += iconSize + height * 0.3f;
            const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;
            draw->PushClipRect(ImVec2(x, min.y), ImVec2(max.x - height * 2.4f, max.y), true);
            draw->AddText(ImVec2(x, textY), IM_COL32(232, 235, 240, 255), title.empty() ? "(sarlavhasiz)" : title.c_str());
            draw->PopClipRect();
            // The window buttons on the right.
            const float r = height * 0.16f;
            for (int i = 0; i < 3; i++)
            {
                const ImVec2 c(max.x - height * (0.45f + 0.62f * (float)i), min.y + height * 0.5f);
                draw->AddCircleFilled(c, r, i == 0 ? IM_COL32(224, 96, 88, 255) : IM_COL32(120, 126, 140, 255));
            }
            // The mode under the bar.
            draw->AddText(ImVec2(min.x + height * 0.3f, max.y + height * 0.45f), IM_COL32(140, 146, 160, 255), modeText);
            ImGui::Dummy(ImVec2(width, height * 2.6f + 2.0f));
        }
    }

    void ProjectSettingsPanel::DrawPlayer(ProjectConfig& config)
    {
        PlayerSettings& p = config.Player;
        bool changed = false;

        // ------------------------------------------------ identity
        if (UI::BeginPropertyGrid("playerIdentity"))
        {
            UI::PropertyHeader(ICON_FA_BUILDING "  Nom va versiya",
                               "O'yinni kim qilgani, o'yinchilar ko'radigan nomi va versiyasi. Skriptlardan: Application::GetCompanyName(), "
                               "GetProductName(), GetVersion().");
            UI::PropertyLabel("Kompaniya", "Studiya yoki muallif. O'yinning SaveData papkasi shunga bog'liq: ~/.local/share/<kompaniya>/<mahsulot>. "
                                           "Bo'sh: ~/.local/share/zamin/<mahsulot> (avvalgidek)");
            changed |= CommitText("##company", p.CompanyName, "(bo'sh: zamin)");
            UI::PropertyLabel("Mahsulot nomi", "O'yinchilar ko'radigan nom (oyna sarlavhasi, SaveData papkasi, eksport). Bo'sh: loyiha nomi");
            changed |= CommitText("##product", p.ProductName, config.Name.c_str());

            UI::PropertyLabel("Versiya", "Semantik versiya: MAJOR.MINOR.PATCH, ixtiyoriy -prerelease va +build (1.4.2, 2.0.0-beta.1). "
                                         "Application::GetVersion() shuni qaytaradi");
            std::string version = p.Version;
            if (CommitText("##version", version, PlayerSettings::DefaultVersion))
            {
                // What people often type ("v1.2", "2") becomes a proper version.
                if (auto lenient = SemanticVersion::ParseLenient(version); lenient && !SemanticVersion::IsValid(version))
                {
                    ZM_INFO("Versiya '{}' -> {}", version, lenient->ToString());
                    version = lenient->ToString();
                }
                p.Version = version.empty() ? std::string(PlayerSettings::DefaultVersion) : version;
                changed = true;
            }
            const std::optional<SemanticVersion> parsed = SemanticVersion::Parse(p.Version);
            UI::PropertyLabel("");
            if (parsed)
            {
                const struct { const char* Label; SemanticVersion::Part Part; const char* Tip; } bumps[] = {
                    { "+ Major", SemanticVersion::Part::Major, "Katta o'zgarish (eski saqlanganlar mos kelmasligi mumkin): X.0.0" },
                    { "+ Minor", SemanticVersion::Part::Minor, "Yangi imkoniyatlar: x.Y.0" },
                    { "+ Patch", SemanticVersion::Part::Patch, "Tuzatishlar: x.y.Z" },
                };
                for (int i = 0; i < 3; i++)
                {
                    if (i > 0)
                        ImGui::SameLine();
                    if (ImGui::SmallButton(bumps[i].Label))
                    {
                        p.Version = parsed->Bumped(bumps[i].Part).ToString();
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(std::format("{} -> {}", bumps[i].Tip, parsed->Bumped(bumps[i].Part).ToString()).c_str());
                }
                if (!parsed->PreRelease.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(pre-release: %s)", parsed->PreRelease.c_str());
                }
            }
            else
                ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Semantik versiya emas (masalan 1.2.3 yoki 2.0.0-beta.1)");

            UI::PropertyLabel("SaveData papkasi", "O'yin SaveData (PlayerPrefs) bilan saqlagan qiymatlar shu papkada (savedata.json)");
            const std::filesystem::path saveDirectory = SaveData::GetDirectoryFor(p.CompanyName, p.GetProductName(config.Name));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", saveDirectory.string().c_str());
            ImGui::PopStyleColor();
            std::error_code ec;
            if (std::filesystem::is_directory(saveDirectory, ec))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN "##openSave"))
                    Platform::OpenInFileBrowser(saveDirectory);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Fayl menejerida ochish");
            }
            UI::EndPropertyGrid();
        }
        ImGui::Spacing();

        // ------------------------------------------------ icon
        DrawPlayerIcon(p);
        ImGui::Spacing();

        // ------------------------------------------------ window
        if (UI::BeginPropertyGrid("playerWindow"))
        {
            UI::PropertyHeader(ICON_FA_WINDOW_MAXIMIZE "  Oyna",
                               "ZaminPlayer oynasi shunday yaratiladi. Buyruq qatori (--fullscreen, --windowed, --width/--height, --no-vsync) va "
                               "o'yin menyusida saqlangan ekran sozlamalari (Display) bulardan ustun.");
            UI::PropertyLabel("Oyna o'lchami", "Oynali rejimdagi o'lcham (piksel). To'liq ekran va chegarasiz rejim monitorning o'z o'lchamida");
            int size[2] = { (int)p.Width, (int)p.Height };
            const float comboWidth = ImGui::GetFontSize() * 2.2f;
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - comboWidth - ImGui::GetStyle().ItemSpacing.x));
            if (ImGui::DragInt2("##size", size, 2.0f, (int)PlayerSettings::MinWindowSize, (int)PlayerSettings::MaxWindowSize, "%d px"))
            {
                p.Width = (uint32_t)size[0];
                p.Height = (uint32_t)size[1];
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(comboWidth);
            if (ImGui::BeginCombo("##sizePresets", "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_HeightLarge))
            {
                for (const ResolutionPreset& r : s_Resolutions)
                {
                    const std::string label = r.Name[0] ? std::format("{} x {}   {}", r.Width, r.Height, r.Name) : std::format("{} x {}", r.Width, r.Height);
                    if (ImGui::Selectable(label.c_str(), p.Width == r.Width && p.Height == r.Height))
                    {
                        p.Width = r.Width;
                        p.Height = r.Height;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Tayyor o'lchamlar");

            static const char* s_Modes[] = { "Oynali", "To'liq ekran", "Chegarasiz" };
            static const char* s_ModeTips[] = { "Sarlavhali oddiy oyna (o'lchami yuqorida)",
                                                "Eksklyuziv to'liq ekran: monitor o'z video rejimida, oyna monitorni egallaydi",
                                                "Oyna ko'rinishidagi to'liq ekran: chegarasiz oyna butun monitor ustida (Alt+Tab tez)" };
            int mode = (int)p.Mode;
            if (UI::PropertySegmented("Oyna rejimi", mode, s_Modes, 3, s_ModeTips))
            {
                p.Mode = (WindowMode)mode;
                changed = true;
            }
            ImGui::BeginDisabled(p.Mode != WindowMode::Windowed);
            changed |= UI::PropertyBool("O'lchamini o'zgartirish", p.Resizable);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("O'yinchi oynani chetidan tortib kattalashtira oladimi (oynali rejimda)");
            changed |= UI::PropertyBool("VSync", p.VSync);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Kadrlar monitor yangilanishiga moslanadi (tearing yo'q). O'yin menyusidan ham o'zgartiriladi (Display::SetVSync)");

            UI::PropertyLabel("Oyna sarlavhasi", "Bo'sh: mahsulot nomi");
            changed |= CommitText("##title", p.Title, p.GetProductName(config.Name).c_str());
            changed |= UI::PropertyBool("Sarlavhada versiya", p.VersionInTitle);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Sarlavha oxirida versiya: \"O'yin 1.2.0\" (sinov build'lari uchun qulay)");

            UI::PropertyLabel("FPS cheklovi", "Sekundiga eng ko'p kadr. 0 - cheklanmagan (VSync yoqilgan bo'lsa monitor chastotasi)");
            int fps = p.TargetFrameRate;
            const std::string fpsPreview = fps <= 0 ? std::string("Cheklanmagan") : std::format("{} FPS", fps);
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - comboWidth - ImGui::GetStyle().ItemSpacing.x));
            if (ImGui::DragInt("##fps", &fps, 1.0f, 0, PlayerSettings::MaxTargetFrameRate, fps <= 0 ? "Cheklanmagan" : "%d FPS"))
            {
                p.TargetFrameRate = fps;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(comboWidth);
            if (ImGui::BeginCombo("##fpsPresets", fpsPreview.c_str(), ImGuiComboFlags_NoPreview))
            {
                for (int rate : s_FrameRates)
                    if (ImGui::Selectable(rate == 0 ? "Cheklanmagan" : std::format("{} FPS", rate).c_str(), rate == p.TargetFrameRate))
                    {
                        p.TargetFrameRate = rate;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            changed |= UI::PropertyBool("Fonda ishlash", p.RunInBackground);
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'chirilsa: oyna fokusdan chiqqanda o'yin to'xtaydi (vaqt, fizika, skriptlar) va ovoz o'chadi; qaytganda davom etadi");
            UI::EndPropertyGrid();
        }
        const char* modeText = p.Mode == WindowMode::Fullscreen   ? "To'liq ekran (monitor o'lchamida)"
                               : p.Mode == WindowMode::Borderless ? "Chegarasiz to'liq ekran (monitor o'lchamida)"
                                                                  : nullptr;
        const std::string modeLine = modeText ? std::string(modeText)
                                              : std::format("{} x {}{}{}", p.Width, p.Height, p.Resizable ? ", o'lchami o'zgaradi" : "",
                                                            p.TargetFrameRate > 0 ? std::format(", {} FPS", p.TargetFrameRate) : std::string());
        ImGui::TextDisabled("Ko'rinishi:");
        TitleBarPreview(p.GetWindowTitle(config.Name), m_IconPreview, modeLine.c_str());

        if (changed)
        {
            p.Sanitize();
            m_Dirty = true;
        }
    }

    void ProjectSettingsPanel::DrawPlayerIcon(PlayerSettings& p)
    {
        // The preview follows the file (loaded again when it changes on disk).
        const std::filesystem::path file = p.Icon.empty() ? std::filesystem::path() : Project::GetAssetFileSystemPath(p.Icon);
        std::error_code ec;
        const auto time = file.empty() ? std::filesystem::file_time_type() : std::filesystem::last_write_time(file, ec);
        if (file != m_IconPreviewPath || time != m_IconPreviewTime)
        {
            m_IconPreviewPath = file;
            m_IconPreviewTime = time;
            m_IconPreview = nullptr;
            m_IconError.clear();
            m_IconWarning.clear();
            m_IconSize = glm::uvec2(0);
            if (!file.empty())
            {
                ImageData image;
                if (GameIcon::Load(file, image, &m_IconError))
                {
                    m_IconSize = { image.Width, image.Height };
                    m_IconWarning = GameIcon::Check(image);
                    m_IconPreview = Texture2D::Create(file.string());
                    if (m_IconPreview && !m_IconPreview->IsLoaded())
                        m_IconPreview = nullptr;
                }
            }
        }

        bool changed = false;
        if (UI::BeginPropertyGrid("playerIcon"))
        {
            UI::PropertyHeader(ICON_FA_IMAGE "  Ikonka",
                               "O'yin oynasining (sarlavha, vazifalar paneli) va eksport qilingan o'yinning .desktop faylidagi ikonkasi. "
                               "Kvadrat PNG, yaxshisi 256x256 yoki kattaroq: undan 16 ... 256 px o'lchamlar yasaladi.");
            std::string path = p.Icon.generic_string();
            UI::AssetPathOptions options;
            options.PreviewTexture = m_IconPreview ? m_IconPreview->GetRendererID() : 0;
            options.Missing = !path.empty() && !m_IconError.empty();
            options.Tooltip = "PNG faylni Content Browser'dan tortib tashlang";
            if (UI::PropertyAssetPath("Ikonka (PNG)", path, ".png", ICON_FA_IMAGE, options))
            {
                std::filesystem::path chosen(path);
                p.Icon = chosen.is_absolute() ? std::filesystem::path(Project::GetAssetRelativePath(chosen)) : chosen;
                changed = true;
            }
            if (!p.Icon.empty())
            {
                UI::PropertyLabel("");
                if (!m_IconError.empty())
                    ImGui::TextColored(s_ErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s", m_IconError.c_str());
                else
                {
                    std::string sizes;
                    const uint32_t side = std::max(m_IconSize.x, m_IconSize.y);
                    for (uint32_t s : PlayerSettings::IconSizes)
                        if (s <= side || sizes.empty())
                            sizes += (sizes.empty() ? "" : ", ") + std::to_string(s);
                    ImGui::TextDisabled("%u x %u PNG  ->  %s px", m_IconSize.x, m_IconSize.y, sizes.c_str());
                    if (!m_IconWarning.empty())
                        ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s", m_IconWarning.c_str());
                }
            }
            UI::EndPropertyGrid();
        }
        // Big previews: how it looks at a few of the sizes the window gets.
        if (m_IconPreview)
        {
            ImGui::Indent(ImGui::GetFontSize() * 0.5f);
            const float sizes[] = { 16.0f, 32.0f, 64.0f, 128.0f };
            const float scale = std::max(1.0f, ImGui::GetFontSize() / 16.0f);
            for (int i = 0; i < 4; i++)
            {
                if (i > 0)
                    ImGui::SameLine(0.0f, ImGui::GetFontSize());
                const float s = sizes[i] * scale;
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + s, pos.y + s), IM_COL32(36, 38, 46, 255), 4.0f);
                ImGui::Image((ImTextureID)(uintptr_t)m_IconPreview->GetRendererID(), ImVec2(s, s), ImVec2(0, 1), ImVec2(1, 0));
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{} px", (int)sizes[i]).c_str());
            }
            ImGui::Unindent(ImGui::GetFontSize() * 0.5f);
            ImGui::TextDisabled("Eksport: o'yin papkasida <nom>.png va <nom>.desktop (ilovalar menyusi uchun)");
        }
        else if (p.Icon.empty())
            ImGui::TextDisabled("Ikonka tanlanmagan: tizimning standart ikonkasi");
        if (changed)
            m_Dirty = true;
    }

}
