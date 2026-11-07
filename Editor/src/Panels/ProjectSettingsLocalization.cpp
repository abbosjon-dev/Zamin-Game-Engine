// Project settings: the Localization category (the game's languages).
#include "ProjectSettingsPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <format>

namespace Zamin {

    namespace {
        const ImVec4 s_WarnColor(0.95f, 0.72f, 0.32f, 1.0f);
        const ImVec4 s_OkColor(0.45f, 0.85f, 0.55f, 1.0f);

        // Languages offered as one-click additions.
        const char* const s_CommonLanguages[] = { "uz", "ru", "en", "tr", "kk", "ky", "tg", "de", "fr", "es" };

        std::string LanguageLabel(const std::string& code)
        {
            return std::format("{} ({})", Localization::GetLanguageName(code), code);
        }
    }

    void ProjectSettingsPanel::DrawLocalization(ProjectConfig& config)
    {
        LocalizationSettings& loc = config.Localization;
        bool changed = false;
        const std::filesystem::path directory = Project::GetAssetDirectory() / loc.Directory;
        // The languages the window lists: the settings' list, else the files found.
        const std::vector<std::string> found = LocalizationTable::FindLanguageFiles(directory);
        std::vector<std::string> listed = loc.Languages;
        const bool automatic = listed.empty();
        if (automatic)
        {
            listed = found;
            auto it = std::find(listed.begin(), listed.end(), loc.DefaultLanguage);
            if (it == listed.end())
                listed.insert(listed.begin(), loc.DefaultLanguage);
            else
                std::rotate(listed.begin(), it, it + 1);
        }

        if (UI::BeginPropertyGrid("localization"))
        {
            UI::PropertyHeader(ICON_FA_LANGUAGE "  Tillar",
                               "O'yin boshlanadigan til va kalit boshqa tilda bo'lmaganda ishlatiladigan til (standart), o'yin taklif qiladigan "
                               "tillar va ularning fayllari.");
            // Default language.
            UI::PropertyLabel("Standart til", "O'yin shu tilda boshlanadi (o'yinchi boshqasini tanlamagan bo'lsa); boshqa tilda yo'q kalitlar "
                                              "shu tildan olinadi");
            ImGui::PushID("defaultLanguage");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##v", LanguageLabel(loc.DefaultLanguage).c_str()))
            {
                for (const std::string& code : listed)
                    if (ImGui::Selectable(LanguageLabel(code).c_str(), code == loc.DefaultLanguage))
                    {
                        loc.DefaultLanguage = code;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::PopID();

            UI::PropertyLabel("Tizim tilida boshlash", "O'yin kompyuter tilida ($LANG) boshlanadi, agar o'yin uni taklif qilsa; aks holda standart "
                                                       "tilda. O'yinchi tanlagan til (Localization::SaveSettings) baribir ustun.");
            ImGui::PushID("systemLanguage");
            changed |= ImGui::Checkbox("##v", &loc.UseSystemLanguage);
            ImGui::SameLine();
            const std::string system = Localization::DetectSystemLanguage();
            ImGui::TextDisabled("(bu kompyuterda: %s)", system.empty() ? "aniqlanmadi" : LanguageLabel(system).c_str());
            ImGui::PopID();

            UI::PropertyLabel("Til fayllari papkasi", "Assets ichidagi papka: har bir til - <kod>.json fayli (masalan Localization/ru.json)");
            ImGui::PushID("directory");
            std::string dir = loc.Directory.generic_string();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##v", &dir, ImGuiInputTextFlags_EnterReturnsTrue) && dir != loc.Directory.generic_string())
            {
                loc.Directory = dir;
                changed = true;
            }
            ImGui::PopID();

            UI::PropertyLabel("Tillar ro'yxati", "Bo'sh ro'yxat: papkadagi hamma til fayllari (standart til birinchi). Ro'yxat tartibi - o'yindagi "
                                                 "til menyusi tartibi.");
            ImGui::PushID("listMode");
            bool explicitList = !automatic;
            if (ImGui::Checkbox(Tr("O'z ro'yxati"), &explicitList))
            {
                loc.Languages = explicitList ? listed : std::vector<std::string>();
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(automatic ? "(hozir: papkadagi fayllar)" : "(faqat shu tillar)");
            ImGui::PopID();
            UI::EndPropertyGrid();
        }

        // The languages: their files, how complete they are (read again when the language files were
        // reloaded, the list changed, or every 2 s for files changed elsewhere).
        if (m_LocTableVersion != Localization::GetVersion() || m_LocTableLanguages != listed || m_LocTableDirectory != directory ||
            ImGui::GetTime() - m_LocTableTime > 2.0)
        {
            m_LocTable = LocalizationTable::Load(directory, listed);
            m_LocTableVersion = Localization::GetVersion();
            m_LocTableLanguages = listed;
            m_LocTableDirectory = directory;
            m_LocTableTime = ImGui::GetTime();
        }
        const LocalizationTable& table = m_LocTable;
        const size_t keys = table.GetKeys().size();
        ImGui::Spacing();
        if (ImGui::BeginTable("languages", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Til", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("Fayl", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Tayyor", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 3.4f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 1.2f);
            ImGui::TableHeadersRow();
            int moveFrom = -1, moveTo = -1, remove = -1;
            for (size_t i = 0; i < listed.size(); i++)
            {
                const std::string& code = listed[i];
                ImGui::PushID(code.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (code == loc.DefaultLanguage)
                    ImGui::TextColored(ImVec4(UI::AccentColor().x, UI::AccentColor().y, UI::AccentColor().z, 1.0f), ICON_FA_STAR " %s",
                                       LanguageLabel(code).c_str());
                else
                    ImGui::TextUnformatted(LanguageLabel(code).c_str());
                if (code == Localization::GetLanguage())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(ICON_FA_EYE);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Editorda shu til ko'rsatilmoqda");
                }
                ImGui::TableNextColumn();
                const bool exists = std::find(found.begin(), found.end(), code) != found.end();
                if (exists)
                    ImGui::TextColored(s_OkColor, ICON_FA_CHECK " %s.json", code.c_str());
                else
                {
                    ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION " yo'q");
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Tr("Yaratish")))
                    {
                        LocalizationTable created = table;
                        created.Languages.erase(std::remove_if(created.Languages.begin(), created.Languages.end(),
                                                               [&](const StringTable& t) { return t.Language != code; }),
                                                created.Languages.end());
                        if (created.Languages.empty())
                            created.AddLanguage(code);
                        // The other languages' keys, empty: ready to be translated.
                        for (const std::string& key : table.GetKeys())
                            if (!created.Get(key, code))
                                created.Set(key, code, LocalizedString());
                        created.Languages[0].DisplayName = Localization::GetLanguageName(code);
                        std::vector<std::string> errors;
                        if (created.Save(directory, &errors))
                        {
                            ZM_INFO("Til fayli yaratildi: {}", (directory / (code + ".json")).string());
                            Project::ApplyLocalization(true);
                        }
                        else
                            for (const std::string& e : errors)
                                ZM_ERROR("{}", e);
                    }
                }
                ImGui::TableNextColumn();
                const size_t missing = table.CountMissing(code);
                const float done = keys > 0 ? 1.0f - (float)missing / (float)keys : 1.0f;
                ImGui::ProgressBar(done, ImVec2(-FLT_MIN, 0.0f),
                                   keys == 0 ? "kalit yo'q" : missing == 0 ? std::format("{} / {}", keys, keys).c_str()
                                                                           : std::format("{} yo'q", missing).c_str());
                if (ImGui::IsItemHovered() && missing > 0)
                {
                    std::string list;
                    const std::vector<std::string> keysMissing = table.GetMissing(code);
                    for (size_t k = 0; k < keysMissing.size() && k < 12; k++)
                        list += "\n  " + keysMissing[k];
                    if (keysMissing.size() > 12)
                        list += std::format("\n  ... yana {} ta", keysMissing.size() - 12);
                    UI::Tooltip(std::format("Tarjima qilinmagan kalitlar ({} ta):{}", missing, list).c_str());
                }
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(automatic || i == 0);
                if (ImGui::SmallButton(ICON_FA_ARROW_UP))
                {
                    moveFrom = (int)i;
                    moveTo = (int)i - 1;
                }
                ImGui::EndDisabled();
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::BeginDisabled(automatic || i + 1 >= listed.size());
                if (ImGui::SmallButton(ICON_FA_ARROW_DOWN))
                {
                    moveFrom = (int)i;
                    moveTo = (int)i + 1;
                }
                ImGui::EndDisabled();
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::BeginDisabled(code == Localization::GetLanguage());
                if (ImGui::SmallButton(ICON_FA_EYE))
                    Localization::SetLanguage(code);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    UI::Tooltip("Editorda shu tilni ko'rish (sahnadagi lokalizatsiya kalitli matnlar)");
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(automatic || code == loc.DefaultLanguage);
                if (ImGui::SmallButton(ICON_FA_XMARK))
                    remove = (int)i;
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    UI::Tooltip(automatic ? "Papkadagi fayllar ishlatilmoqda: o'z ro'yxatini yoqing"
                                          : code == loc.DefaultLanguage ? "Standart tilni olib bo'lmaydi"
                                                                        : "Ro'yxatdan olish (fayl o'chirilmaydi)");
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (moveFrom >= 0 && moveTo >= 0 && !automatic)
            {
                std::swap(loc.Languages[(size_t)moveFrom], loc.Languages[(size_t)moveTo]);
                changed = true;
            }
            if (remove >= 0 && !automatic)
            {
                loc.Languages.erase(loc.Languages.begin() + remove);
                changed = true;
            }
        }

        // Adding a language.
        ImGui::Spacing();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputTextWithHint("##newLanguage", Tr("til kodi"), &m_NewLanguage);
        ImGui::SameLine();
        const bool validNew = LocalizationSettings::IsValidCode(m_NewLanguage) &&
                              std::find(listed.begin(), listed.end(), m_NewLanguage) == listed.end();
        auto addLanguage = [&](const std::string& code) {
            if (automatic)
                loc.Languages = listed;
            loc.Languages.push_back(code);
            changed = true;
        };
        ImGui::BeginDisabled(!validNew);
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Til qo'shish")))
        {
            addLanguage(m_NewLanguage);
            m_NewLanguage.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        for (const char* code : s_CommonLanguages)
        {
            if (std::find(listed.begin(), listed.end(), code) != listed.end())
                continue;
            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::SmallButton(code))
                addLanguage(code);
            if (ImGui::IsItemHovered())
                UI::Tooltip(std::format("{} tilini qo'shish", Localization::GetLanguageName(code)).c_str());
        }

        // The editor's preview language and the table.
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ICON_FA_EYE "  Editorda ko'rish:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##preview", LanguageLabel(Localization::GetLanguage()).c_str()))
        {
            for (const std::string& code : Localization::GetLanguages())
                if (ImGui::Selectable(LanguageLabel(code).c_str(), code == Localization::GetLanguage()))
                    Localization::SetLanguage(code);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sahnadagi lokalizatsiya kalitli matnlar shu tilda ko'rsatiladi (faqat editorda; loyiha faylida emas)");
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_TABLE_CELLS "  Lokalizatsiya jadvali...")) && OnLocalizationTableRequested)
            OnLocalizationTableRequested();
        ImGui::TextDisabled("%zu ta kalit, %zu ta til. Skriptlar: Localize(\"kalit\"), Localize(\"kalit\", qiymat), LocalizePlural(\"kalit\", son)",
                            keys, listed.size());
        for (const std::string& error : Localization::GetLoadErrors())
            ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s", error.c_str());

        if (changed)
        {
            loc.Sanitize();
            ApplyRuntime(Category::Localization, config);
            m_Dirty = true;
        }
    }

}
