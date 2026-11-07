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
                               "ZaminPlayer oynasi shunday yaratiladi. O'yin menyusida saqlangan ekran sozlamalari (Display: to'liq ekran, VSync) "
                               "bulardan ustun, buyruq qatori (--fullscreen, --windowed, --borderless, --resolution, --monitor, --fps, --no-vsync) "
                               "esa ikkalasidan ham.");
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
            changed |= UI::PropertyBool("F11 / Alt+Enter", p.FullscreenToggle);
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'yin ichida F11 yoki Alt+Enter to'liq ekran va oyna orasida almashtiradi (oyna avvalgi o'lchami va joyiga "
                            "qaytadi; tanlov saqlanadi). Input xaritasida \"ToggleFullscreen\" harakati bo'lsa, u ham ishlaydi");
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
        ImGui::Spacing();

        // ------------------------------------------------ splash screen, aspect ratio
        DrawPlayerSplash(p);
        ImGui::Spacing();
        DrawPlayerAspect(p);

        if (changed)
        {
            p.Sanitize();
            m_Dirty = true;
        }
    }

    void ProjectSettingsPanel::DrawPlayerSplash(PlayerSettings& p)
    {
        // The game's own picture: loaded again when the path changes.
        const std::filesystem::path file = p.SplashImage.empty() ? std::filesystem::path() : Project::GetAssetFileSystemPath(p.SplashImage);
        if (file != m_SplashImagePath)
        {
            m_SplashImagePath = file;
            m_SplashImagePreview = nullptr;
            m_SplashImageMissing = false;
            if (!file.empty())
            {
                m_SplashImagePreview = Texture2D::Create(file.string());
                if (!m_SplashImagePreview || !m_SplashImagePreview->IsLoaded())
                {
                    m_SplashImagePreview = nullptr;
                    m_SplashImageMissing = true;
                }
            }
        }
        if (!m_SplashLogo)
            m_SplashLogo = SplashLogo::CreateTexture(256);

        bool changed = false;
        if (UI::BeginPropertyGrid("playerSplash"))
        {
            UI::PropertyHeader(ICON_FA_FILM "  Splash ekran",
                               "Player birinchi sahnadan oldin Zamin logotipini (yoki o'yinning o'z rasmini) ko'rsatadi: paydo bo'ladi, turadi va "
                               "so'nadi. Shu vaqtda birinchi sahna fonda yuklanadi; yuklanish cho'zilsa splash kutib turadi (progress chizig'i bilan). "
                               "Buyruq qatori: --no-splash.");
            changed |= UI::PropertyBool("Splash ko'rsatish", p.SplashScreen);
            ImGui::BeginDisabled(!p.SplashScreen);
            UI::PropertyLabel("Davomiyligi", "Paydo bo'lish, turish va so'nish birga (soniya). Sahna hali yuklanmagan bo'lsa uzoqroq turadi");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##splashDuration", &p.SplashDuration, 0.05f, PlayerSettings::MinSplashDuration, PlayerSettings::MaxSplashDuration,
                                 "%.1f s", ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            changed |= UI::PropertyBool("Tugma bilan o'tkazish", p.SplashSkippable);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Istalgan tugma, sichqoncha yoki gamepad tugmasi splashni tugatadi (sahna yuklangan bo'lsa darhol)");
            std::string path = p.SplashImage.generic_string();
            UI::AssetPathOptions options;
            options.PreviewTexture = m_SplashImagePreview ? m_SplashImagePreview->GetRendererID() : 0;
            options.Missing = m_SplashImageMissing;
            options.Tooltip = "O'yinning o'z rasmi (PNG / JPG) Zamin logotipi o'rniga. Bo'sh: Zamin logotipi";
            if (UI::PropertyAssetPath("O'z rasmi", path, ".png;.jpg;.jpeg", ICON_FA_IMAGE, options))
            {
                std::filesystem::path chosen(path);
                p.SplashImage = chosen.is_absolute() ? std::filesystem::path(Project::GetAssetRelativePath(chosen)) : chosen;
                changed = true;
            }
            changed |= UI::PropertyColor3("Fon rangi", p.SplashBackground);
            ImGui::EndDisabled();
            UI::EndPropertyGrid();
        }

        // A small screen as the player shows it at full opacity.
        if (p.SplashScreen)
        {
            ImGui::TextDisabled("Ko'rinishi:");
            const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 22.0f);
            const float height = width * 9.0f / 16.0f;
            const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + width, min.y + height);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const glm::vec3& bg = p.SplashBackground;
            draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(bg.r, bg.g, bg.b, 1.0f)), 4.0f);
            draw->AddRect(min, max, IM_COL32(70, 74, 86, 255), 4.0f);
            if (m_SplashImagePreview)
            {
                const float aspect = (float)m_SplashImagePreview->GetWidth() / (float)std::max(1u, m_SplashImagePreview->GetHeight());
                float w = width * 0.7f, h = w / aspect;
                if (h > height * 0.55f)
                {
                    h = height * 0.55f;
                    w = h * aspect;
                }
                const ImVec2 c(min.x + width * 0.5f, min.y + height * 0.48f);
                draw->AddImage((ImTextureID)(uintptr_t)m_SplashImagePreview->GetRendererID(), ImVec2(c.x - w * 0.5f, c.y - h * 0.5f),
                               ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), ImVec2(0, 1), ImVec2(1, 0));
            }
            else if (m_SplashLogo)
            {
                const float side = std::min(height * 0.34f, width * 0.45f);
                const ImVec2 c(min.x + width * 0.5f, min.y + height * 0.43f);
                draw->AddImage((ImTextureID)(uintptr_t)m_SplashLogo->GetRendererID(), ImVec2(c.x - side * 0.5f, c.y - side * 0.5f),
                               ImVec2(c.x + side * 0.5f, c.y + side * 0.5f), ImVec2(0, 1), ImVec2(1, 0));
                const char* title = "ZAMIN ENGINE";
                const ImVec2 size = ImGui::CalcTextSize(title);
                draw->AddText(ImVec2(c.x - size.x * 0.5f, c.y + side * 0.62f), IM_COL32(236, 240, 244, 255), title);
            }
            ImGui::Dummy(ImVec2(width, height + 2.0f));
            ImGui::TextDisabled("%.1f s%s", p.SplashDuration, p.SplashSkippable ? ", tugma bilan o'tkaziladi" : "");
        }
        if (changed)
        {
            p.Sanitize();
            m_Dirty = true;
        }
    }

    void ProjectSettingsPanel::DrawPlayerAspect(PlayerSettings& p)
    {
        bool changed = false;
        if (UI::BeginPropertyGrid("playerAspect"))
        {
            UI::PropertyHeader(ICON_FA_CROP "  Ekran nisbati",
                               "Bitta ekran shakli uchun qilingan o'yin: tasvir shu nisbatda qoladi, oynaning qolgan qismida polosalar "
                               "(yon tomonlarda yoki tepa-pastda). Sichqoncha va UI tasvir ichida ishlaydi; oyna o'lchami o'zgarsa tasvir "
                               "va UI unga moslashadi. O'chiq: tasvir butun oynani egallaydi (kamera oyna shakliga moslashadi).");
            changed |= UI::PropertyBool("Qat'iy nisbat", p.FixedAspectRatio);
            ImGui::BeginDisabled(!p.FixedAspectRatio);
            UI::PropertyLabel("Nisbat", "Kenglik : balandlik (1 ... 64). Editorda Play ham shu nisbatda ko'rsatiladi (o'yin ko'rinishi \"Erkin\" bo'lsa)");
            static const std::pair<uint32_t, uint32_t> s_Aspects[] = { { 16, 9 }, { 16, 10 }, { 4, 3 }, { 21, 9 }, { 3, 2 }, { 1, 1 }, { 9, 16 } };
            const float comboWidth = ImGui::GetFrameHeight() * 1.2f;
            const float fieldWidth = std::max(40.0f, (ImGui::GetContentRegionAvail().x - comboWidth - ImGui::CalcTextSize(":").x -
                                                      ImGui::GetStyle().ItemSpacing.x * 3.0f) * 0.5f);
            int aw = (int)p.AspectWidth, ah = (int)p.AspectHeight;
            ImGui::SetNextItemWidth(fieldWidth);
            if (ImGui::DragInt("##aspectW", &aw, 0.1f, 1, (int)PlayerSettings::MaxAspectSide, "%d", ImGuiSliderFlags_AlwaysClamp))
            {
                p.AspectWidth = (uint32_t)aw;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(":");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fieldWidth);
            if (ImGui::DragInt("##aspectH", &ah, 0.1f, 1, (int)PlayerSettings::MaxAspectSide, "%d", ImGuiSliderFlags_AlwaysClamp))
            {
                p.AspectHeight = (uint32_t)ah;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(comboWidth);
            if (ImGui::BeginCombo("##aspectPresets", "", ImGuiComboFlags_NoPreview))
            {
                for (const auto& [w, h] : s_Aspects)
                    if (ImGui::Selectable(std::format("{}:{}", w, h).c_str(), p.AspectWidth == w && p.AspectHeight == h))
                    {
                        p.AspectWidth = w;
                        p.AspectHeight = h;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            changed |= UI::PropertyColor3("Polosalar rangi", p.LetterboxColor);
            ImGui::EndDisabled();
            UI::EndPropertyGrid();
        }

        // How windows of a few shapes show the picture: the player's own size, a wide and a square-ish screen.
        if (p.FixedAspectRatio)
        {
            ImGui::TextDisabled("Turli oynalarda:");
            const std::pair<uint32_t, uint32_t> windows[] = { { p.Width, p.Height }, { 2560, 1080 }, { 1024, 768 } };
            const float avail = ImGui::GetContentRegionAvail().x;
            const float boxHeight = std::min(ImGui::GetFontSize() * 5.5f, avail * 0.16f);
            const float spacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 at = ImGui::GetCursorScreenPos();
            float rowHeight = 0.0f;
            for (const auto& [ww, wh] : windows)
            {
                const float aspect = (float)ww / (float)std::max(1u, wh);
                const float h = boxHeight, w = std::min(h * aspect, avail * 0.45f);
                const float hh = w / aspect;
                const ImVec2 min(at.x, at.y), max(at.x + w, at.y + hh);
                const glm::vec3& bars = p.LetterboxColor;
                draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(bars.r, bars.g, bars.b, 1.0f)));
                // The picture (GL rect: Y from the bottom).
                const PlayerSettings::ViewportRect rect = p.ComputeViewport(ww, wh);
                const float sx = w / (float)ww, sy = hh / (float)wh;
                const ImVec2 pmin(min.x + (float)rect.X * sx, max.y - (float)(rect.Y + rect.Height) * sy);
                const ImVec2 pmax(pmin.x + (float)rect.Width * sx, pmin.y + (float)rect.Height * sy);
                draw->AddRectFilledMultiColor(pmin, pmax, IM_COL32(64, 112, 176, 255), IM_COL32(72, 128, 196, 255), IM_COL32(52, 120, 92, 255),
                                              IM_COL32(46, 104, 84, 255));
                const std::string inner = std::format("{}x{}", rect.Width, rect.Height);
                const ImVec2 innerSize = ImGui::CalcTextSize(inner.c_str());
                if (innerSize.x < pmax.x - pmin.x - 4.0f)
                    draw->AddText(ImVec2((pmin.x + pmax.x - innerSize.x) * 0.5f, (pmin.y + pmax.y - innerSize.y) * 0.5f), IM_COL32(236, 240, 244, 230),
                                  inner.c_str());
                draw->AddRect(min, max, IM_COL32(90, 96, 110, 255));
                const std::string label = std::format("{}x{}", ww, wh);
                draw->AddText(ImVec2(min.x, max.y + 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), label.c_str());
                at.x += w + spacing;
                rowHeight = std::max(rowHeight, hh + ImGui::GetTextLineHeight() + 4.0f);
            }
            ImGui::Dummy(ImVec2(avail, rowHeight));
        }
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
