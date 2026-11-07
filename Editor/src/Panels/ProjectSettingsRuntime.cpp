// Project settings: the Graphics (OpenGL context), Time and Physics (default gravity) categories.
#include "ProjectSettingsPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    namespace {
        const ImVec4 s_WarnColor(0.95f, 0.72f, 0.32f, 1.0f);
        const ImVec4 s_OkColor(0.45f, 0.85f, 0.55f, 1.0f);

        bool SliderIntRow(const char* label, const char* tooltip, int& value, int min, int max, const char* format)
        {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            const bool changed = ImGui::SliderInt("##v", &value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
            return changed;
        }

        bool SliderFloatRow(const char* label, const char* tooltip, float& value, float min, float max, const char* format,
                            ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp)
        {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            const bool changed = ImGui::SliderFloat("##v", &value, min, max, format, flags);
            ImGui::PopID();
            return changed;
        }

        // A row of small preset buttons under a value (the active one highlighted).
        template<typename T, typename Label>
        bool PresetButtons(const char* id, T& value, const std::vector<T>& presets, Label label)
        {
            bool changed = false;
            UI::PropertyLabel("");
            ImGui::PushID(id);
            for (size_t i = 0; i < presets.size(); i++)
            {
                if (i > 0)
                    ImGui::SameLine(0.0f, 4.0f);
                const bool active = std::abs((double)value - (double)presets[i]) < 1e-4;
                if (active)
                    ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor(0.55f));
                ImGui::PushID((int)i);
                if (ImGui::SmallButton(label(presets[i]).c_str()) && !active)
                {
                    value = presets[i];
                    changed = true;
                }
                ImGui::PopID();
                if (active)
                    ImGui::PopStyleColor();
            }
            ImGui::PopID();
            return changed;
        }

        void InfoRow(const char* label, const std::string& value, const char* tooltip = nullptr)
        {
            UI::PropertyLabel(label, tooltip);
            ImGui::TextWrapped("%s", value.empty() ? "-" : value.c_str());
        }
    }

    void ProjectSettingsPanel::ApplySceneDefaults(const ProjectConfig& config)
    {
        // Only values that follow the project change (they are not written to the scene's file: no dirty flag).
        if (m_Scene)
            m_Scene->GetSettings().ApplyProjectDefaults(config.Physics, config.Time);
    }

    // ------------------------------------------------------------------ graphics
    void ProjectSettingsPanel::DrawGraphics(ProjectConfig& config)
    {
        GraphicsSettings& g = config.Graphics;
        g.Sanitize();
        const GraphicsContextInfo& current = GraphicsContext::GetInfo();
        bool changed = false;

        if (UI::BeginPropertyGrid("graphicsPlayer"))
        {
            UI::PropertyHeader(ICON_FA_MICROCHIP "  Player konteksti",
                               "ZaminPlayer oynasini shu sozlamalar bilan yaratadi (editor o'z kontekstida qoladi). Drayver bera olmagan "
                               "kontekst o'rniga standart (4.1 core, MSAA'siz, sRGB'siz) olinadi va logda yoziladi. Buyruq qatori ustun: "
                               "--gl 4.6, --msaa 4, --gl-debug / --no-gl-debug.");
            // The OpenGL version.
            const auto& versions = GraphicsSettings::GetSupportedVersions();
            std::vector<const char*> labels;
            int index = 0;
            for (size_t i = 0; i < versions.size(); i++)
            {
                labels.push_back(versions[i].Label);
                if (versions[i].Major == g.GLMajor && versions[i].Minor == g.GLMinor)
                    index = (int)i;
            }
            static const char* s_VersionTips[] = {
                "OpenGL 4.1 core: dvigatelga kerakli versiya, eng ko'p qurilmada bor (macOS'da eng yuqorisi). Standart.",
                "OpenGL 4.3 core: compute shader'lar, KHR_debug yadroda.",
                "OpenGL 4.5 core: direct state access, clip control.",
                "OpenGL 4.6 core: SPIR-V, anizotropik filtr yadroda. Eng yangi drayverlar.",
            };
            if (UI::PropertySegmented("OpenGL versiyasi", index, labels.data(), (int)labels.size(), s_VersionTips))
            {
                g.GLMajor = versions[(size_t)index].Major;
                g.GLMinor = versions[(size_t)index].Minor;
                changed = true;
            }
            if (current.Valid && (g.GLMajor * 10 + g.GLMinor) > (current.Major * 10 + current.Minor))
            {
                UI::PropertyLabel("");
                ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Bu kompyuterdagi kontekst %d.%d: player 4.1 ga qaytishi mumkin",
                                   current.Major, current.Minor);
            }

            // Debug output.
            int debug = (int)g.DebugContext;
            static const char* s_DebugLabels[] = { "Avto", "Yoqilgan", "O'chiq" };
            static const char* s_DebugTips[] = {
                "Faqat Debug build'da (NDEBUG yo'q): drayver xato va ogohlantirishlarini logga yozadi. Standart.",
                "Release build'da ham debug konteksti: drayverning har bir chaqiruv tekshiruvi (sekinroq) - nosozliklarni topish uchun.",
                "Hech qachon: Debug build'da ham drayver xabarlarisiz (tezroq).",
            };
            if (UI::PropertySegmented("Debug konteksti", debug, s_DebugLabels, 3, s_DebugTips))
            {
                g.DebugContext = (GraphicsDebugContext)debug;
                changed = true;
            }
            UI::PropertyLabel("");
            ImGui::TextDisabled("Bu build'da: %s", g.WantsDebugContext() ? "debug konteksti bilan" : "debug kontekstisiz");

            // MSAA of the window's framebuffer.
            static const int s_Samples[] = { 0, 2, 4, 8, 16 };
            static const char* s_SampleLabels[] = { "O'chiq", "2x", "4x", "8x", "16x" };
            static const char* s_SampleTips[] = {
                "Oyna buferi multisample'siz. Standart.",
                "2 namuna: ekranga to'g'ridan-to'g'ri chiziladigan narsalar (UI, debug chiziqlar) qirralari biroz silliq.",
                "4 namuna: ko'p o'yinlar uchun yaxshi muvozanat.",
                "8 namuna: silliqroq, ko'proq xotira.",
                "16 namuna: eng silliq; hamma drayverda yo'q.",
            };
            int sample = 0;
            for (int i = 0; i < 5; i++)
                if (s_Samples[i] == g.MSAASamples)
                    sample = i;
            if (UI::PropertySegmented("MSAA (oyna)", sample, s_SampleLabels, 5, s_SampleTips))
            {
                g.MSAASamples = s_Samples[sample];
                changed = true;
            }
            if (g.MSAASamples > 0)
            {
                UI::PropertyLabel("");
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("3D sahna HDR buferda chiziladi: uning qirralarini Sifat bo'limidagi FXAA silliqlaydi. MSAA oyna buferiga "
                                   "to'g'ridan-to'g'ri chiziladiganlarga (piksel-perfekt o'yin UI'si, debug chiziqlar, xato ekrani) ta'sir qiladi.");
                ImGui::PopStyleColor();
                if (current.Valid && current.MaxSamples > 0 && g.MSAASamples > current.MaxSamples)
                {
                    UI::PropertyLabel("");
                    ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Bu drayver ko'pi bilan %dx beradi", current.MaxSamples);
                }
            }

            // sRGB framebuffer.
            UI::PropertyLabel("sRGB framebuffer", "Oxirgi rasm oyna buferiga chiziqli ranglarda yoziladi va uskuna ularni aniq sRGB egri chizig'i "
                                                  "bilan kodlaydi (o'chiq: shaderning gamma 2.2 si). Farq faqat eng qorong'i tonlarda sezilarli. "
                                                  "Drayver sRGB bufer bermasa - shaderdagi gamma qoladi.");
            changed |= ImGui::Checkbox("##srgb", &g.SRGBFramebuffer);

            UI::PropertyLabel("So'raladi", "Player oynasini yaratganda so'raydigan kontekst");
            ImGui::TextUnformatted(std::format("OpenGL {} core{}{}{}", g.GetVersionString(), g.WantsDebugContext() ? ", debug" : "",
                                               g.MSAASamples > 0 ? std::format(", MSAA {}x", g.MSAASamples) : std::string(),
                                               g.SRGBFramebuffer ? ", sRGB" : "").c_str());
            if (!g.IsDefault())
            {
                UI::PropertyLabel("");
                if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT "  Standart (4.1 core)"))
                {
                    g = GraphicsSettings();
                    changed = true;
                }
            }
            UI::EndPropertyGrid();
        }

        // ---- the context this editor runs on
        ImGui::Spacing();
        if (UI::BeginPropertyGrid("graphicsCurrent"))
        {
            UI::PropertyHeader(ICON_FA_DESKTOP "  Editor konteksti", "Editor oynasining OpenGL konteksti: drayver va videokarta nima berayotgani");
            if (!current.Valid)
                InfoRow("Holat", "Kontekst yo'q");
            else
            {
                InfoRow("Ishlab chiqaruvchi", current.Vendor);
                InfoRow("Videokarta", current.Renderer);
                InfoRow("Versiya", current.Version);
                InfoRow("GLSL", current.ShadingLanguage);
                InfoRow("Kontekst", std::format("{}.{} {} (so'ralgan {}.{}){}", current.Major, current.Minor, current.CoreProfile ? "core" : "compat",
                                                current.RequestedMajor, current.RequestedMinor, current.FellBack ? " - standartga qaytdi" : ""));
                InfoRow("Debug", current.DebugContext ? (current.DebugOutput ? "debug konteksti, xabarlar logda" : "debug konteksti")
                                                      : (current.DebugOutput ? "xabarlar logda" : "yo'q"));
                InfoRow("MSAA (oyna)", current.Samples > 0 ? std::format("{}x", current.Samples) : std::string("yo'q"));
                InfoRow("sRGB oyna buferi", current.SRGB ? "ha" : "yo'q");
                InfoRow("Eng katta tekstura", std::format("{} px", current.MaxTextureSize));
                InfoRow("Eng ko'p MSAA", std::format("{}x", current.MaxSamples));
                InfoRow("Anizotropiya", current.MaxAnisotropy > 0.0f ? std::format("{:.0f}x gacha", current.MaxAnisotropy) : std::string("yo'q"));
                UI::PropertyLabel("");
                if (ImGui::SmallButton(ICON_FA_COPY "  Nusxa olish"))
                {
                    ImGui::SetClipboardText(std::format("{}\n{} | {} | {}\nGLSL {}\nMSAA {} | sRGB {} | max tekstura {} | max MSAA {} | anizotropiya {:.0f}",
                                                        current.Summary(), current.Vendor, current.Renderer, current.Version, current.ShadingLanguage,
                                                        current.Samples, current.SRGB ? "ha" : "yo'q", current.MaxTextureSize, current.MaxSamples,
                                                        current.MaxAnisotropy).c_str());
                    ZM_INFO("Grafik kontekst ma'lumoti nusxalandi");
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Xato hisobotlari uchun: kontekst, drayver va imkoniyatlar");
            }
            UI::EndPropertyGrid();
        }

        if (changed)
        {
            g.Sanitize();
            m_Dirty = true;
        }
    }

    // ------------------------------------------------------------------ time
    void ProjectSettingsPanel::DrawTime(ProjectConfig& config)
    {
        TimeSettings& t = config.Time;
        t.Sanitize();
        bool changed = false;

        if (UI::BeginPropertyGrid("time"))
        {
            UI::PropertyHeader(ICON_FA_STOPWATCH "  Qat'iy qadam",
                               "Fizika (2D va 3D) shu chastotada qat'iy qadamlar bilan yuradi: kadr tezligidan qat'i nazar bir xil natija. "
                               "O'z qadam sozlamasini bermagan sahnalar uchun (Sahna sozlamalari > Fizika > Loyihadan).");
            changed |= SliderIntRow("Qadam chastotasi",
                                    "Soniyasiga shuncha fizika qadami (har biri 1 / chastota s). Ko'proq - tez jismlar, joint'lar va personajlar "
                                    "aniqroq, lekin qimmatroq. Standart: 60 Hz",
                                    t.FixedStepRate, TimeSettings::MinStepRate, TimeSettings::MaxStepRate, "%d Hz");
            changed |= PresetButtons("rates", t.FixedStepRate, TimeSettings::GetStepRatePresets(), [](int r) { return std::format("{}", r); });
            UI::PropertyLabel("Qadam", "Time::GetFixedDeltaTime()");
            ImGui::Text("1/%d s = %.2f ms", t.FixedStepRate, 1000.0f / (float)t.FixedStepRate);
            changed |= SliderIntRow("Kadrda ko'pi bilan",
                                    "Sekin kadr shuncha qadamdan ortig'ini tashlab yuboradi: o'yin sekinlashadi, lekin orqada qolib ketmaydi. "
                                    "Standart: 5",
                                    t.MaxStepsPerFrame, 1, TimeSettings::MaxStepsLimit, "%d qadam");
            UI::PropertyLabel("");
            ImGui::TextDisabled("Bir kadrda ko'pi bilan %.1f ms fizika vaqti (%d FPS gacha to'liq tezlikda)",
                                1000.0f * (float)t.MaxStepsPerFrame / (float)t.FixedStepRate,
                                std::max(1, t.FixedStepRate / std::max(1, t.MaxStepsPerFrame)));
            // The open scene: does it follow?
            if (m_Scene)
            {
                const auto& s = m_Scene->GetSettings();
                UI::PropertyLabel("Ochiq sahna", "Sahna sozlamalari > Fizika: \"Loyihadan\" belgilangan sahnalar shu qiymatlarni oladi");
                if (s.UseProjectPhysicsStep)
                    ImGui::TextColored(s_OkColor, ICON_FA_CHECK "  loyihadan (%d Hz, %d qadam)", s.PhysicsStep.StepRate, s.PhysicsStep.MaxStepsPerFrame);
                else
                    ImGui::TextColored(s_WarnColor, "o'zining: %d Hz, %d qadam", s.PhysicsStep.StepRate, s.PhysicsStep.MaxStepsPerFrame);
            }

            UI::PropertyHeader(ICON_FA_GAUGE_SIMPLE "  O'yin vaqti",
                               "O'yin soati: Time::SetTimeScale(0.3f) - sekinlashtirish, 0 - pauza. Fizika, animatsiyalar, zarrachalar, "
                               "taymerlar va tween'lar unga ergashadi; UI va SetTimersUnscaled(true) skriptlari real vaqtda.");
            changed |= SliderFloatRow("Start tezligi",
                                      "Play boshlanganda o'yin soati shu tezlikda yuradi (1 - normal, 0.5 - ikki marta sekin, 0 - pauzada boshlanadi). "
                                      "Standart: 1",
                                      t.DefaultTimeScale, 0.0f, 4.0f, "%.2fx");
            changed |= PresetButtons("scales", t.DefaultTimeScale, std::vector<float>{ 0.25f, 0.5f, 1.0f, 2.0f },
                                     [](float v) { return std::format("{}x", v); });
            changed |= SliderFloatRow("Eng uzun kadr",
                                      "Bundan uzun kadr (to'xtab qolish, oyna sudralishi) shuncha deb hisoblanadi: o'yin sakramaydi, sekinlashadi. "
                                      "Standart: 0.1 s",
                                      t.MaxDeltaTime, TimeSettings::MinMaxDeltaTime, TimeSettings::MaxMaxDeltaTime, "%.3f s",
                                      ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            if (!t.IsDefault())
            {
                UI::PropertyLabel("");
                if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT "  Standart (60 Hz, 5, 1x, 0.1 s)"))
                {
                    t = TimeSettings();
                    changed = true;
                }
            }

            // The running game's clock (Play): read live, the scale can be tried here.
            UI::PropertyHeader(ICON_FA_CLOCK "  O'yin soati", "Time:: API qaytaradigan qiymatlar. Play paytida masshtabni shu yerda sinab ko'rish mumkin.");
            if (Time::GetGameClock())
            {
                float scale = Time::GetTimeScale();
                if (SliderFloatRow("Vaqt masshtabi", "Time::SetTimeScale: o'ynayotgan o'yinning soati (loyihaga yozilmaydi)", scale, 0.0f, 2.0f, "%.2fx"))
                    Time::SetTimeScale(scale);
                InfoRow("DeltaTime", std::format("{:.2f} ms", Time::GetDeltaTime() * 1000.0f));
                InfoRow("UnscaledDeltaTime", std::format("{:.2f} ms", Time::GetUnscaledDeltaTime() * 1000.0f));
                InfoRow("FixedDeltaTime", std::format("{:.2f} ms", Time::GetFixedDeltaTime() * 1000.0f));
                InfoRow("Time / UnscaledTime", std::format("{:.2f} s / {:.2f} s", Time::GetTime(), Time::GetUnscaledTime()));
            }
            else
            {
                UI::PropertyLabel("Holat");
                ImGui::TextDisabled("O'yin ishlamayapti (Play bosing)");
            }
            InfoRow("FrameCount", std::format("{}", Time::GetFrameCount()));
            InfoRow("TimeSinceStartup", std::format("{:.1f} s", Time::GetTimeSinceStartup()));
            UI::EndPropertyGrid();
        }

        if (changed)
        {
            t.Sanitize();
            m_Dirty = true;
            ApplySceneDefaults(config);
        }
    }

    // ------------------------------------------------------------------ physics: the default gravity
    void ProjectSettingsPanel::DrawPhysics(ProjectConfig& config)
    {
        PhysicsDefaults& p = config.Physics;
        bool changed = false;
        if (UI::BeginPropertyGrid("physicsDefaults"))
        {
            UI::PropertyHeader(ICON_FA_ARROW_DOWN_LONG "  Gravitatsiya",
                               "O'z gravitatsiyasini bermagan sahnalar (yangi sahnalar va dvigatel standartida qolganlar) shuni oladi. Sahna "
                               "o'zinikini Sahna sozlamalari > Fizika'da beradi (\"Loyihadan\" belgisini olib).");
            changed |= UI::PropertyFloat2("Gravitatsiya (2D)", p.Gravity2D, 0.05f);
            changed |= UI::PropertyFloat3("Gravitatsiya (3D)", p.Gravity3D, 0.05f);
            int presetCount = 0;
            const PhysicsDefaults::Preset* presets = PhysicsDefaults::GetPresets(presetCount);
            UI::PropertyLabel("Presetlar", "Pastga qaragan kuch (m/s^2): 2D va 3D gravitatsiyaning yo'nalishi pastga, kattaligi presetniki");
            ImGui::PushID("gravityPresets");
            for (int i = 0; i < presetCount; i++)
            {
                if (i > 0)
                    ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(presets[i].Name).x + 16.0f)
                    ImGui::NewLine();
                ImGui::PushID(i);
                if (ImGui::SmallButton(presets[i].Name))
                {
                    p.Gravity2D = { 0.0f, -presets[i].Strength };
                    p.Gravity3D = { 0.0f, -presets[i].Strength, 0.0f };
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            if (!p.IsDefault())
            {
                UI::PropertyLabel("");
                if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT "  Dvigatel standarti (-9.8 / -9.81)"))
                {
                    p = PhysicsDefaults();
                    changed = true;
                }
            }
            if (m_Scene)
            {
                const auto& s = m_Scene->GetSettings();
                UI::PropertyLabel("Ochiq sahna", "Sahna sozlamalari > Fizika: \"Loyihadan\" belgilangan gravitatsiya shu yerdagini oladi");
                auto show = [&](const char* what, bool follows, const std::string& value) {
                    if (follows)
                        ImGui::TextColored(s_OkColor, ICON_FA_CHECK "  %s: loyihadan %s", what, value.c_str());
                    else
                        ImGui::TextColored(s_WarnColor, "%s: o'zining %s", what, value.c_str());
                };
                show("2D", s.UseProjectGravity, std::format("({:.2f}, {:.2f})", s.Gravity.x, s.Gravity.y));
                UI::PropertyLabel("");
                show("3D", s.UseProjectGravity3D, std::format("({:.2f}, {:.2f}, {:.2f})", s.Gravity3D.x, s.Gravity3D.y, s.Gravity3D.z));
            }
            UI::EndPropertyGrid();
        }
        if (changed)
        {
            p.Sanitize();
            m_Dirty = true;
            ApplySceneDefaults(config);
        }

        ImGui::Spacing();
        ImGui::SeparatorText(ICON_FA_TABLE_CELLS "  To'qnashuv qatlamlari");
        DrawPhysicsLayers(config);
    }

}
