// Project settings: the Quality, Audio and Physics (collision layers) categories.
#include "ProjectSettingsPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace Zamin {

    namespace {
        const ImVec4 s_WarnColor(0.95f, 0.72f, 0.32f, 1.0f);

        const char* s_FilterLabels[] = { "Qattiq", "PCF 3x3", "PCF 5x5", "Poisson (16)" };
        const char* s_AnisotropyLabels[] = { "O'chiq", "2x", "4x", "8x", "16x" };
        const char* s_TextureLabels[] = { "To'liq", "1/2", "1/4", "1/8" };
        const char* s_AtlasLabels[] = { "1024", "2048", "4096" };

        int AnisotropyIndex(int samples)
        {
            int index = 0;
            while (index < 4 && (1 << (index + 1)) <= samples)
                index++;
            return index;
        }

        int AtlasIndex(uint32_t size) { return size >= 4096 ? 2 : size >= 2048 ? 1 : 0; }

        // A slider row of a property grid (int).
        bool PropertySliderInt(const char* label, int& value, int min, int max, const char* format = "%d")
        {
            UI::PropertyLabel(label);
            ImGui::PushID(label);
            const bool changed = ImGui::SliderInt("##v", &value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
            return changed;
        }

        bool PropertySliderFloat(const char* label, float& value, float min, float max, const char* format)
        {
            UI::PropertyLabel(label);
            ImGui::PushID(label);
            const bool changed = ImGui::SliderFloat("##v", &value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
            return changed;
        }
    }

    // ------------------------------------------------------------------ quality
    void ProjectSettingsPanel::DrawQuality(ProjectConfig& config)
    {
        QualitySettings& q = config.Quality;
        q.Sanitize();
        const int count = (int)q.Levels.size();
        if (m_QualityLevel < 0 || m_QualityLevel >= count)
        {
            m_QualityLevel = Quality::GetLevel() >= 0 && Quality::GetLevel() < count ? Quality::GetLevel() : q.DefaultLevel;
            m_QualityNameBuffer = q.Levels[(size_t)m_QualityLevel].Name;
        }
        bool changed = false;

        // ---- which level the game starts on, which one the editor shows
        std::vector<const char*> names;
        for (const QualityLevel& level : q.Levels)
            names.push_back(level.Name.c_str());
        if (UI::BeginPropertyGrid("qualityTop"))
        {
            UI::PropertyLabel("Standart daraja", "O'yin shu darajada boshlanadi, agar o'yinchi boshqasini tanlamagan bo'lsa "
                                                 "(Quality::SaveSettings) yoki player --quality bilan ishga tushmagan bo'lsa.");
            int def = q.DefaultLevel;
            if (ImGui::Combo("##default", &def, names.data(), count) && def != q.DefaultLevel)
            {
                q.DefaultLevel = def;
                changed = true;
            }
            UI::PropertyLabel("Editorda ko'rish", "Editor viewport'i va Play shu darajada chiziladi (faqat ko'rinish: loyihaga yozilmaydi).");
            int preview = std::max(0, Quality::GetLevel());
            if (ImGui::Combo("##preview", &preview, names.data(), count))
            {
                Quality::SetLevel(preview);
                m_QualityLevel = preview;
                m_QualityNameBuffer = q.Levels[(size_t)preview].Name;
            }
            UI::EndPropertyGrid();
        }
        ImGui::Spacing();

        // ---- the levels side by side: a click on a column edits that level below
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX;
        int select = -1, toggleLevel = -1, toggleRow = -1;
        if (ImGui::BeginTable("##qualityLevels", count + 1, tableFlags))
        {
            ImGui::TableSetupColumn("##setting", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.5f);
            for (int i = 0; i < count; i++)
                ImGui::TableSetupColumn(names[(size_t)i], ImGuiTableColumnFlags_WidthStretch);
            // Header: the level names, the game's default (star) and the editor's preview (eye).
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Daraja");
            for (int i = 0; i < count; i++)
            {
                ImGui::TableSetColumnIndex(i + 1);
                ImGui::PushID(i);
                std::string label = q.Levels[(size_t)i].Name;
                if (i == q.DefaultLevel) label += "  " ICON_FA_STAR;
                if (i == Quality::GetLevel()) label += "  " ICON_FA_EYE;
                ImGui::PushStyleColor(ImGuiCol_Text, i == m_QualityLevel ? UI::AccentColor() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
                if (ImGui::Selectable(label.c_str(), i == m_QualityLevel))
                    select = i;
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{}{}\nBosing: shu darajani tahrirlash", i == q.DefaultLevel ? ICON_FA_STAR " o'yin standarti  " : "",
                                            i == Quality::GetLevel() ? ICON_FA_EYE " editorda ko'rinmoqda" : "").c_str());
                ImGui::PopID();
            }
            // One row per setting; on / off cells toggle with a click.
            enum Row { Shadows, Atlas, Filter, Cascades, Distance, SSAO, Bloom, FXAA, AutoExposure, Scale, Anisotropy, Textures, RowCount };
            static const char* s_RowLabels[RowCount] = { "Soyalar", "Soya atlasi", "Soya filtri", "Kaskadlar", "Soya masofasi", "SSAO", "Bloom",
                                                         "FXAA", "Avto ekspozitsiya", "Render masshtabi", "Anizotropik filtr", "Tekstura sifati" };
            for (int row = 0; row < RowCount; row++)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.72f, 0.75f, 0.80f, 1.0f), "%s", s_RowLabels[row]);
                UI::HighlightIfSearched(s_RowLabels[row]);
                for (int i = 0; i < count; i++)
                {
                    const QualityLevel& l = q.Levels[(size_t)i];
                    ImGui::TableSetColumnIndex(i + 1);
                    if (i == m_QualityLevel)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, UI::AccentU32(0.12f));
                    ImGui::PushID(row * 64 + i);
                    auto flag = [&](bool on) {
                        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.45f, 0.85f, 0.50f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        if (ImGui::Selectable(on ? ICON_FA_CHECK "##on" : "-##off", false))
                        {
                            toggleLevel = i;
                            toggleRow = row;
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            UI::Tooltip(on ? "Ruxsat (sahna yoqsa ishlaydi). Bosing: o'chirish" : "O'chiq. Bosing: ruxsat berish");
                    };
                    auto value = [&](const std::string& text) {
                        if (ImGui::Selectable(text.c_str(), false))
                            select = i;
                    };
                    const bool shadowsOff = !l.Shadows && row >= Atlas && row <= Distance;
                    if (shadowsOff)
                        ImGui::TextDisabled("-");
                    else
                        switch (row)
                        {
                            case Shadows:      flag(l.Shadows); break;
                            case Atlas:        value(std::to_string(l.ShadowAtlasSize)); break;
                            case Filter:       value(s_FilterLabels[(int)l.MaxShadowFilter]); break;
                            case Cascades:     value(std::to_string(l.MaxShadowCascades)); break;
                            case Distance:     value(std::format("x{:.2f}", l.ShadowDistanceScale)); break;
                            case SSAO:         flag(l.SSAO); break;
                            case Bloom:        flag(l.Bloom); break;
                            case FXAA:         flag(l.FXAA); break;
                            case AutoExposure: flag(l.AutoExposure); break;
                            case Scale:        value(std::format("x{:.2f}", l.RenderScale)); break;
                            case Anisotropy:   value(s_AnisotropyLabels[AnisotropyIndex(l.Anisotropy)]); break;
                            case Textures:     value(s_TextureLabels[std::clamp(l.TextureQuality, 0, 3)]); break;
                            default:           break;
                        }
                    ImGui::PopID();
                }
            }
            if (toggleLevel >= 0)
            {
                QualityLevel& l = q.Levels[(size_t)toggleLevel];
                switch (toggleRow)
                {
                    case Shadows:      l.Shadows = !l.Shadows; break;
                    case SSAO:         l.SSAO = !l.SSAO; break;
                    case Bloom:        l.Bloom = !l.Bloom; break;
                    case FXAA:         l.FXAA = !l.FXAA; break;
                    case AutoExposure: l.AutoExposure = !l.AutoExposure; break;
                    default:           break;
                }
                select = toggleLevel;
                changed = true;
            }
            ImGui::EndTable();
        }
        if (select >= 0 && select != m_QualityLevel)
        {
            m_QualityLevel = select;
            m_QualityNameBuffer = q.Levels[(size_t)select].Name;
        }
        ImGui::TextDisabled(ICON_FA_STAR " o'yin standarti   " ICON_FA_EYE " editorda ko'rinmoqda   Daraja sahna so'raganini faqat kamaytiradi");

        // ---- the selected level in detail
        ImGui::Spacing();
        QualityLevel& level = q.Levels[(size_t)m_QualityLevel];
        ImGui::SeparatorText(std::format("{}  Daraja: {}", ICON_FA_GAUGE_HIGH, level.Name).c_str());
        if (UI::BeginPropertyGrid("qualityLevel", std::min(ImGui::GetContentRegionAvail().x * 0.42f, ImGui::GetFontSize() * 12.0f)))
        {
            UI::PropertyLabel("Nomi");
            ImGui::InputText("##levelName", &m_QualityNameBuffer);
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                std::string name = m_QualityNameBuffer;
                const int other = q.Find(name);
                if (!name.empty() && (other < 0 || other == m_QualityLevel) && name != level.Name)
                {
                    level.Name = name;
                    changed = true;
                }
                else
                    m_QualityNameBuffer = level.Name;
            }

            UI::PropertyHeader(ICON_FA_SUN "  Quyosh soyalari", "Sahnaning soya sozlamalari (Sahna sozlamalari > Soyalar) shu chegaralardan "
                                                               "oshmaydi: kattaroq atlas, qimmatroq filtr yoki ko'proq kaskad so'ralsa, shu darajaniki ishlatiladi.");
            changed |= UI::PropertyBool("Soyalar", level.Shadows);
            ImGui::BeginDisabled(!level.Shadows);
            int atlas = AtlasIndex(level.ShadowAtlasSize);
            if (UI::PropertyCombo("Soya atlasi (eng katta)", atlas, s_AtlasLabels, IM_ARRAYSIZE(s_AtlasLabels)))
            {
                level.ShadowAtlasSize = 1024u << atlas;
                changed = true;
            }
            int filter = (int)level.MaxShadowFilter;
            if (UI::PropertyCombo("Soya filtri (eng qimmati)", filter, s_FilterLabels, IM_ARRAYSIZE(s_FilterLabels)))
            {
                level.MaxShadowFilter = (ShadowFilter)filter;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Narx tartibi: qattiq < PCF 3x3 < Poisson (16) < PCF 5x5. Qimmatroq filtrli sahna shu filtrni oladi.");
            changed |= PropertySliderInt("Kaskadlar (eng ko'pi)", level.MaxShadowCascades, 1, ShadowSettings::MaxCascades);
            changed |= PropertySliderFloat("Soya masofasi", level.ShadowDistanceScale, 0.1f, 4.0f, "x%.2f");
            ImGui::EndDisabled();

            UI::PropertyHeader(ICON_FA_WAND_MAGIC_SPARKLES "  Effektlar", "Sahna yoqqan effekt shu darajada ruxsat bo'lsagina ishlaydi.");
            changed |= UI::PropertyBool("SSAO", level.SSAO);
            changed |= UI::PropertyBool("Bloom", level.Bloom);
            changed |= UI::PropertyBool("FXAA", level.FXAA);
            changed |= UI::PropertyBool("Avto ekspozitsiya", level.AutoExposure);
            changed |= PropertySliderFloat("Render masshtabi", level.RenderScale, QualityLevel::MinScale, QualityLevel::MaxScale, "x%.2f");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Sahnaning render masshtabiga ko'paytiriladi (natija 0.5 .. 2 oralig'ida): 0.75 - tezroq, xiraroq");

            UI::PropertyHeader(ICON_FA_IMAGE "  Teksturalar", "Mipmap'li barcha rasm teksturalari (sprite, material, model teksturalari).");
            int anisotropy = AnisotropyIndex(level.Anisotropy);
            if (UI::PropertyCombo("Anizotropik filtr", anisotropy, s_AnisotropyLabels, IM_ARRAYSIZE(s_AnisotropyLabels)))
            {
                level.Anisotropy = 1 << anisotropy;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Qiya ko'rinadigan yuzalar (yer, devor) uzoqda ham tiniq. Standart darajalarda o'chiq (eski loyihalar bir xil "
                            "ko'rinishi uchun); 4x - 16x tavsiya etiladi. GPU ruxsat bergandan oshmaydi.");
            int textures = std::clamp(level.TextureQuality, 0, QualityLevel::MaxTextureQuality);
            if (UI::PropertyCombo("Tekstura sifati", textures, s_TextureLabels, IM_ARRAYSIZE(s_TextureLabels)))
            {
                level.TextureQuality = textures;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Eng katta mip darajalari tashlab ketiladi: 1/2 - teksturalar ikki baravar kichik o'lchamda o'qiladi");
            UI::EndPropertyGrid();
        }

        // ---- level list: add / remove / move / defaults
        if (ImGui::Button(ICON_FA_PLUS "  Nusxa qo'shish") && count < QualitySettings::MaxLevels)
        {
            QualityLevel copy = level;
            copy.Name = q.MakeUniqueName(level.Name);
            q.Levels.insert(q.Levels.begin() + m_QualityLevel + 1, copy);
            if (q.DefaultLevel > m_QualityLevel)
                q.DefaultLevel++;
            m_QualityLevel++;
            m_QualityNameBuffer = copy.Name;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Shu darajadan nusxa (keyin nomi va qiymatlarini o'zgartiring)");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_QualityLevel == 0);
        if (ImGui::Button(ICON_FA_ARROW_LEFT))
        {
            std::swap(q.Levels[(size_t)m_QualityLevel], q.Levels[(size_t)m_QualityLevel - 1]);
            if (q.DefaultLevel == m_QualityLevel) q.DefaultLevel--;
            else if (q.DefaultLevel == m_QualityLevel - 1) q.DefaultLevel++;
            m_QualityLevel--;
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Chaproqqa (darajalar pastdan yuqoriga: Low ... Ultra)");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_QualityLevel + 1 >= count);
        if (ImGui::Button(ICON_FA_ARROW_RIGHT))
        {
            std::swap(q.Levels[(size_t)m_QualityLevel], q.Levels[(size_t)m_QualityLevel + 1]);
            if (q.DefaultLevel == m_QualityLevel) q.DefaultLevel++;
            else if (q.DefaultLevel == m_QualityLevel + 1) q.DefaultLevel--;
            m_QualityLevel++;
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("O'ngroqqa");
        ImGui::SameLine();
        ImGui::BeginDisabled(count <= 1);
        if (ImGui::Button(ICON_FA_TRASH "  O'chirish"))
        {
            q.Levels.erase(q.Levels.begin() + m_QualityLevel);
            if (q.DefaultLevel >= m_QualityLevel && q.DefaultLevel > 0)
                q.DefaultLevel--;
            m_QualityLevel = std::min(m_QualityLevel, (int)q.Levels.size() - 1);
            m_QualityNameBuffer = q.Levels[(size_t)m_QualityLevel].Name;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Standart darajalar"))
            ImGui::OpenPopup("ResetQuality");
        if (ImGui::BeginPopup("ResetQuality"))
        {
            ImGui::TextUnformatted("Darajalar Low / Medium / High / Ultra standartlariga qaytarilsinmi?");
            if (ImGui::Button("Ha, qaytarish"))
            {
                q = QualitySettings();
                m_QualityLevel = -1;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Yo'q"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- what the previewed level does to the open scene
        ImGui::Spacing();
        ImGui::SeparatorText(ICON_FA_EYE "  Ochiq sahnada");
        if (m_Scene)
        {
            const SceneSettings& s = m_Scene->GetSettings();
            const std::vector<std::string> limits =
                Quality::GetActive().DescribeLimits(s.Shadows, s.Post, s.RenderScale, s.ShadowDistance);
            if (limits.empty())
                ImGui::TextDisabled("%s darajasida sahna o'z sozlamalari bilan chiziladi", Quality::GetLevelName().c_str());
            else
            {
                ImGui::TextColored(s_WarnColor, "%s darajasi bu sahnada:", Quality::GetLevelName().c_str());
                for (const std::string& line : limits)
                    ImGui::BulletText("%s", line.c_str());
            }
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(ICON_FA_CIRCLE_INFO "  Player: --quality Low (yoki raqami). Skriptda: Quality::SetLevel(\"Low\"), Quality::GetLevelNames(), "
                           "Quality::SaveSettings() - tanlov o'yinchining SaveData'sida saqlanadi.");
        ImGui::PopStyleColor();

        if (changed)
        {
            q.Sanitize();
            m_QualityLevel = std::clamp(m_QualityLevel, 0, (int)q.Levels.size() - 1);
            ApplyRuntime(Category::Quality, config);
            m_Dirty = true;
        }
    }

    // ------------------------------------------------------------------ audio
    void ProjectSettingsPanel::DrawAudio(ProjectConfig& config)
    {
        AudioSettings& audio = config.Audio;
        if (!m_SceneEditable)
        {
            ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  O'yin ishlayapti: mikser hozir o'yinniki. Standartlarni "
                                                                      "o'zgartirish uchun o'yinni to'xtating.");
            ImGui::Spacing();
        }
        bool changed = false;
        ImGui::BeginDisabled(!m_SceneEditable);
        if (UI::BeginPropertyGrid("audio"))
        {
            // The master fader on its own row first: the game's overall volume.
            if (AudioGroupSettings* master = audio.Find(AudioSettings::Master))
            {
                UI::PropertyHeader(ICON_FA_VOLUME_HIGH "  Umumiy ovoz",
                                   "Master guruhining boshlang'ich balandligi: hamma ovozlar shundan o'tadi. O'yinchi sozlamalari "
                                   "(Audio::SaveSettings) uni o'yin davomida o'zgartira oladi.");
                const float db = master->Volume > 0.0001f ? 20.0f * std::log10(master->Volume) : -80.0f;
                const std::string format = std::format("{:.0f}%  ({:+.1f} dB)", master->Volume * 100.0f, db);
                changed |= PropertySliderFloat("Master ovozi", master->Volume, 0.0f, AudioSettings::MaxVolume, format.c_str());
            }
            UI::PropertyHeader(ICON_FA_SLIDERS "  Mikser guruhlari (boshlang'ich ovoz)",
                               "Har bir guruh o'yin boshida shu ovoz balandligida (skript Audio::SetGroupVolume bilan o'zgartiradi). "
                               "Guruh qo'shish, ota guruh va filtrlar - Audio Mixer oynasida.");
            for (AudioGroupSettings& group : audio.Groups)
            {
                int depth = 0;
                for (const AudioGroupSettings* g = &group; g && !g->Parent.empty() && depth < 8; g = audio.Find(g->Parent))
                    depth++;
                const std::string label = std::string((size_t)depth * 2, ' ') + group.Name;
                UI::PropertyLabel(label.c_str());
                ImGui::PushID(group.Name.c_str());
                if (ImGui::Checkbox("##mute", &group.Muted))
                    changed = true;
                if (ImGui::IsItemHovered())
                    UI::Tooltip(group.Muted ? "Ovozsiz (bosing: yoqish)" : "Bosing: ovozsiz qilish");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                const float db = group.Volume > 0.0001f ? 20.0f * std::log10(group.Volume) : -80.0f;
                const std::string format = std::format("{:.0f}%  ({:+.1f} dB)", group.Volume * 100.0f, db);
                ImGui::BeginDisabled(group.Muted);
                if (ImGui::SliderFloat("##volume", &group.Volume, 0.0f, AudioSettings::MaxVolume, format.c_str(), ImGuiSliderFlags_AlwaysClamp))
                    changed = true;
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            UI::PropertyHeader(ICON_FA_VOLUME_HIGH "  Ovoz dvigateli");
            changed |= UI::PropertyFloat("Oqim chegarasi (MB)", audio.StreamThresholdMB, 0.1f, 0.1f, 512.0f, "%.1f");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Shundan katta fayllar xotiraga to'liq yuklanmay, diskdan oqim bilan o'qiladi (musiqa doim oqim bilan)");
            changed |= PropertySliderInt("Bir vaqtda ovozlar", audio.MaxVoices, 1, AudioSettings::MaxVoicesLimit);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ko'pi bilan shuncha ovoz bir vaqtda chalinadi (musiqa va editor oldindan eshittirishlari hisobga kirmaydi); "
                            "ortiqchasi muhimligi (Priority) pastrog'ining o'rnini oladi");
            changed |= PropertySliderFloat("Doppler", audio.DopplerFactor, 0.0f, AudioSettings::MaxDopplerFactor, "%.2f");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Harakatlanayotgan 3D ovozlarning Doppler effekti: 1 - tabiatdagidek, 0 - o'chiq");

            AudioSpatialSettings& spatial = audio.Spatial;
            UI::PropertyHeader(ICON_FA_LOCATION_DOT "  3D ovoz (standartlar)",
                               "Yangi Audio Source / Footsteps va skriptlarning PlaySoundAt'i shu masofalar bilan boshlanadi (har bir "
                               "manba o'zinikini o'zgartira oladi). Masofa modeli hamma 3D ovozlarga ta'sir qiladi.");
            changed |= UI::PropertyFloat("Min masofa", spatial.MinDistance, 0.05f, 0.01f, 1000.0f, "%.2f m");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Shundan yaqinda ovoz to'liq balandlikda eshitiladi");
            changed |= UI::PropertyFloat("Max masofa", spatial.MaxDistance, 0.1f, 0.1f, 10000.0f, "%.1f m");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Shundan uzoqda ovoz boshqa pasaymaydi (chiziqli modelda - eshitilmaydi)");
            static const char* s_RolloffLabels[] = { "Teskari (Inverse)", "Chiziqli (Linear)", "Eksponensial", "Masofasiz" };
            int rolloff = (int)spatial.Rolloff;
            if (UI::PropertyCombo("Masofa modeli", rolloff, s_RolloffLabels, 4))
            {
                spatial.Rolloff = (AudioRolloff)rolloff;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Teskari - tabiatdagidek (yaqinda tez, keyin sekin pasayadi); chiziqli - max masofada jim; "
                            "eksponensial - tezroq pasayadi; masofasiz - hamma joyda bir xil (faqat chap / o'ng)");
            ImGui::BeginDisabled(spatial.Rolloff == AudioRolloff::None);
            changed |= PropertySliderFloat("Pasayish (rolloff)", spatial.RolloffFactor, 0.0f, AudioSpatialSettings::MaxRolloffFactor, "%.2f");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ovoz masofa bilan qanchalik tez pasayadi: 1 - model o'zi, 0 - umuman pasaymaydi");

            // The curve: gain over 0 .. 1.25 x the max distance.
            UI::PropertyLabel("Egri chiziq");
            {
                const float width = std::max(80.0f, ImGui::GetContentRegionAvail().x);
                const float height = 70.0f;
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const ImVec2 p1(p0.x + width, p0.y + height);
                draw->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
                const float range = std::max(spatial.MaxDistance * 1.25f, 1.0f);
                const float minX = p0.x + width * std::clamp(spatial.MinDistance / range, 0.0f, 1.0f);
                const float maxX = p0.x + width * std::clamp(spatial.MaxDistance / range, 0.0f, 1.0f);
                const ImU32 guide = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f);
                draw->AddLine(ImVec2(minX, p0.y), ImVec2(minX, p1.y), guide);
                draw->AddLine(ImVec2(maxX, p0.y), ImVec2(maxX, p1.y), guide);
                constexpr int samples = 64;
                ImVec2 points[samples + 1];
                for (int i = 0; i <= samples; i++)
                {
                    const float d = range * (float)i / (float)samples;
                    const float gain = spatial.ComputeGain(d);
                    points[i] = ImVec2(p0.x + width * (float)i / (float)samples, p1.y - 4.0f - (height - 8.0f) * gain);
                }
                draw->AddPolyline(points, samples + 1, ImGui::GetColorU32(ImGuiCol_PlotLines), 0, 2.0f);
                const std::string minText = std::format("{:.1f} m", spatial.MinDistance);
                const std::string maxText = std::format("{:.1f} m", spatial.MaxDistance);
                draw->AddText(ImVec2(minX + 3.0f, p0.y + 2.0f), guide, minText.c_str());
                draw->AddText(ImVec2(std::min(maxX + 3.0f, p1.x - ImGui::CalcTextSize(maxText.c_str()).x - 3.0f), p0.y + 2.0f), guide,
                              maxText.c_str());
                ImGui::Dummy(ImVec2(width, height));
                if (ImGui::IsItemHovered())
                {
                    const float d = range * std::clamp((ImGui::GetIO().MousePos.x - p0.x) / width, 0.0f, 1.0f);
                    const float gain = spatial.ComputeGain(d);
                    UI::Tooltip(std::format("{:.1f} m: {:.0f}%  ({:+.1f} dB)", d, gain * 100.0f, AudioGain::ToDecibels(gain)).c_str());
                }
            }
            UI::PropertyLabel("");
            if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT "  3D standartlarini tiklash"))
            {
                spatial = AudioSpatialSettings();
                changed = true;
            }
            UI::EndPropertyGrid();
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        if (ImGui::Button(ICON_FA_SLIDERS "  Audio Mixer oynasini ochish") && OnAudioMixerRequested)
            OnAudioMixerRequested();
        if (changed)
        {
            audio.Sanitize();
            ApplyRuntime(Category::Audio, config);
            m_Dirty = true;
        }
    }

    // ------------------------------------------------------------------ physics layers
    void ProjectSettingsPanel::DrawPhysicsLayers(ProjectConfig& config)
    {
        CollisionLayers2D& layers = config.CollisionLayers;
        ImGui::TextWrapped("Fizikaning 16 ta to'qnashuv qatlami 2D va 3D uchun bir xil. Rigidbody 2D (va xohlasa har bir kollider), "
                           "tilemap'lar, Rigidbody 3D va Character Controller 3D bitta qatlamda bo'ladi; ikki kollider faqat quyidagi "
                           "jadvalda belgilangan qatlamlar juftligida bir-biriga tegadi (raycast va overlap so'rovlari ham qatlam "
                           "niqobini oladi). 2D uchun o'zgarishlar keyingi Play / Simulate'da, 3D uchun darhol qo'llanadi.");
        ImGui::Spacing();

        // How many 2D / 3D bodies, colliders and tilemaps of the edited scene use each layer.
        std::array<int, CollisionLayers2D::Count> usage2D{}, usage3D{};
        if (m_Scene)
        {
            auto count = [](std::array<int, CollisionLayers2D::Count>& usage, int layer) {
                if (CollisionLayers2D::IsValid(layer))
                    usage[(size_t)layer]++;
            };
            auto& reg = m_Scene->GetRegistry();
            for (auto e : m_Scene->GetAllEntitiesWith<Rigidbody2DComponent>()) count(usage2D, reg.get<Rigidbody2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<BoxCollider2DComponent>()) count(usage2D, reg.get<BoxCollider2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<CircleCollider2DComponent>()) count(usage2D, reg.get<CircleCollider2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<PolygonCollider2DComponent>()) count(usage2D, reg.get<PolygonCollider2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<EdgeCollider2DComponent>()) count(usage2D, reg.get<EdgeCollider2DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<TilemapComponent>()) count(usage2D, reg.get<TilemapComponent>(e).CollisionLayer);
            for (auto e : m_Scene->GetAllEntitiesWith<Rigidbody3DComponent>()) count(usage3D, reg.get<Rigidbody3DComponent>(e).Layer);
            for (auto e : m_Scene->GetAllEntitiesWith<CharacterController3DComponent>())
                count(usage3D, reg.get<CharacterController3DComponent>(e).Layer);
        }

        // ---- presets, copy / paste
        const auto presets = CollisionLayers2D::GetPresets();
        if (ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES "  Presetlar") || m_AutomationOpen == "presets")
        {
            ImGui::OpenPopup("CollisionPresets");
            m_AutomationOpen.clear();
        }
        // The menu opens under its button.
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("CollisionPresets"))
        {
            for (int i = 0; i < (int)presets.size(); i++)
            {
                const std::string label = std::format("{}{}", presets[(size_t)i].Name, presets[(size_t)i].SetsNames ? "  (nomlar bilan)" : "");
                if (ImGui::MenuItem(label.c_str()))
                {
                    if (presets[(size_t)i].SetsNames)
                        m_PendingPreset = i; // confirmed below: it renames the layers
                    else if (layers.ApplyPreset(i))
                        m_Dirty = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip(presets[(size_t)i].Description);
            }
            ImGui::EndPopup();
        }
        if (m_PendingPreset >= 0)
        {
            ImGui::OpenPopup("ConfirmCollisionPreset");
        }
        if (ImGui::BeginPopupModal("ConfirmCollisionPreset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const CollisionLayerPreset& preset = presets[(size_t)std::clamp(m_PendingPreset, 0, (int)presets.size() - 1)];
            int used = 0;
            for (int i = 1; i < CollisionLayers2D::Count; i++)
                used += usage2D[(size_t)i] + usage3D[(size_t)i];
            ImGui::Text("\"%s\" preseti qatlam nomlarini almashtiradi.", preset.Name);
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextDisabled("%s", preset.Description);
            if (used > 0)
                ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Ochiq sahnada %d ta jism Default'dan boshqa qatlamda: ular "
                                                                         "qatlam raqamini saqlaydi (nomi o'zgaradi).", used);
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Qo'llash"))
            {
                if (layers.ApplyPreset(m_PendingPreset))
                    m_Dirty = true;
                m_PendingPreset = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish"))
            {
                m_PendingPreset = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_COPY "  Nusxa"))
        {
            ImGui::SetClipboardText(layers.ToJson().c_str());
            ZM_INFO("Fizika qatlamlari buferga nusxalandi (JSON)");
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Qatlam nomlari va matritsani JSON ko'rinishida buferga olish (boshqa loyihaga qo'yish uchun)");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PASTE "  Qo'yish"))
        {
            const char* text = ImGui::GetClipboardText();
            CollisionLayers2D pasted;
            if (text && CollisionLayers2D::FromJson(text, pasted))
            {
                layers = pasted;
                m_Dirty = true;
                ZM_INFO("Fizika qatlamlari buferdan qo'yildi");
            }
            else
                ZM_WARN("Buferda fizika qatlamlari (JSON) yo'q");
        }

        // ---- the matrix of the named layers (a triangle: each pair once)
        ImGui::SeparatorText("Qaysi qatlamlar to'qnashadi");
        std::vector<int> named = layers.GetNamedLayers();
        for (int i = 0; i < CollisionLayers2D::Count; i++)
            if (usage2D[(size_t)i] + usage3D[(size_t)i] > 0 && std::find(named.begin(), named.end(), i) == named.end())
                named.insert(std::upper_bound(named.begin(), named.end(), i), i); // unnamed, but used
        const int pairs = (int)(named.size() * (named.size() + 1) / 2);
        ImGui::TextDisabled("%d / %d juftlik to'qnashadi. Qatlam nomini bosing: uning hamma juftligini yoqish / o'chirish.",
                            layers.CountCollidingPairs(named), pairs);
        ImGui::Spacing();
        const int columns = (int)named.size() + 1;
        const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_HighlightHoveredColumn;
        int toggleRow = -1;
        if (columns > 1 && ImGui::BeginTable("##physicsMatrix", columns, flags))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder, 140.0f);
            // Columns from the last layer to the first: the rows' cells fill a staircase towards the left.
            std::vector<int> columnLayers(named.rbegin(), named.rend());
            std::vector<std::string> labels;
            for (int l : columnLayers)
                labels.push_back(std::format("{}  {}", l, layers.GetName(l)));
            for (size_t c = 0; c < columnLayers.size(); c++)
                ImGui::TableSetupColumn(labels[c].c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed, 0.0f);
            ImGui::TableAngledHeadersRow();
            for (size_t r = 0; r < named.size(); r++)
            {
                const int a = named[r];
                ImGui::PushID(a);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                const std::string rowLabel = std::format("{}  {}", a, layers.GetName(a));
                if (ImGui::Selectable(rowLabel.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight())))
                    toggleRow = a;
                UI::HighlightIfSearched(rowLabel.c_str());
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{}: 2D'da {} ta, 3D'da {} ta (ochiq sahnada)\nBosing: hamma juftliklarini yoqish / o'chirish",
                                            layers.GetName(a), usage2D[(size_t)a], usage3D[(size_t)a]).c_str());
                for (size_t c = 0; c < columnLayers.size(); c++)
                {
                    const int b = columnLayers[c];
                    // Each pair once: the row's layer against itself and the layers after it.
                    if (b < a)
                        continue;
                    ImGui::TableSetColumnIndex((int)c + 1);
                    ImGui::PushID(b);
                    bool collide = layers.CanCollide(a, b);
                    if (ImGui::Checkbox("##pair", &collide))
                    {
                        layers.SetCollision(a, b, collide);
                        m_Dirty = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        UI::Tooltip(std::format("{} va {}: {}", layers.GetName(a), layers.GetName(b), collide ? "to'qnashadi" : "bir-biridan o'tib ketadi").c_str());
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (toggleRow >= 0)
        {
            bool all = true;
            for (int b : named)
                all &= layers.CanCollide(toggleRow, b);
            layers.SetRow(toggleRow, named, !all);
            m_Dirty = true;
        }
        ImGui::Spacing();

        // ---- names
        ImGui::SeparatorText("Qatlamlar");
        if (ImGui::BeginTable("##physicsLayerNames", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26.0f);
            ImGui::TableSetupColumn("Nomi", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("2D", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("3D", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < CollisionLayers2D::Count; i++)
            {
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%d", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (i == 0)
                {
                    ImGui::BeginDisabled();
                    std::string name = layers.Names[0];
                    ImGui::InputText("##name", &name);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        UI::Tooltip("Asosiy qatlam: hamma narsa avval shu qatlamda");
                }
                else if (ImGui::InputTextWithHint("##name", std::format("(Qatlam {})", i).c_str(), &layers.Names[(size_t)i]))
                    m_Dirty = true;
                for (int d = 0; d < 2; d++)
                {
                    const int used = (d == 0 ? usage2D : usage3D)[(size_t)i];
                    ImGui::TableSetColumnIndex(2 + d);
                    ImGui::AlignTextToFramePadding();
                    if (used > 0)
                        ImGui::Text("%d", used);
                    else
                        ImGui::TextDisabled("-");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Skriptlarda: Raycast2D / Raycast3D(..., LayerMask({ \"Ground\" })), OverlapSphere3D, LayerIndex(\"Player\")");
    }

}
