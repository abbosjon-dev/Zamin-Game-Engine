#include "LocalizationPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <format>

namespace Zamin {

    namespace {
        const ImVec4 s_WarnColor(0.95f, 0.72f, 0.32f, 1.0f);
        const ImU32 s_MissingBg = IM_COL32(170, 60, 60, 70);
        const ImU32 s_MismatchBg = IM_COL32(200, 150, 50, 60);

        bool Contains(const std::string& text, const std::string& filter) { return ProjectSettingsIndex::TextContains(text, filter); }

        // "one: 1 tanga | other: {0} tanga"
        std::string PluralSummary(const LocalizedString& entry)
        {
            std::string out;
            for (const char* category : { "zero", "one", "two", "few", "many", "other" })
                if (auto it = entry.Plural.find(std::string_view(category)); it != entry.Plural.end())
                    out += std::format("{}{}: {}", out.empty() ? "" : "  |  ", category, it->second);
            return out;
        }
    }

    void LocalizationPanel::Load()
    {
        m_LoadErrors.clear();
        m_LoadedFor = Project::GetActive().get();
        m_LoadedDirectory = m_LoadedFor ? Project::GetLocalizationDirectory() : std::filesystem::path();
        m_Table = m_LoadedFor ? LocalizationTable::Load(m_LoadedDirectory, Localization::GetLanguages(), &m_LoadErrors) : LocalizationTable();
        // The display names stay as the files have them; languages without a file get their built-in one when saved.
        m_Keys = m_Table.GetKeys();
        m_Loaded = true;
        m_Dirty = false;
    }

    void LocalizationPanel::OnFilesChanged()
    {
        if (!m_Dirty)
            m_Loaded = false;
    }

    bool LocalizationPanel::Save()
    {
        if (!Project::GetActive())
            return false;
        // A new file gets the language's own name ("$name").
        for (StringTable& t : m_Table.Languages)
            if (t.DisplayName.empty())
                t.DisplayName = Localization::GetLanguageName(t.Language);
        // Into the folder the table was read from: when the project's folder changed since (Project settings >
        // Localization), the edits belong to the old folder's files, not over the new folder's (it is read next).
        std::vector<std::string> errors;
        if (m_LoadedDirectory.empty() || !m_Table.Save(m_LoadedDirectory, &errors))
        {
            for (const std::string& e : errors)
                ZM_ERROR("Lokalizatsiya saqlanmadi: {}", e);
            return false;
        }
        m_Dirty = false;
        ZM_INFO("Lokalizatsiya saqlandi: {} ta til, {} ta kalit ({})", m_Table.Languages.size(), m_Keys.size(),
                Project::GetAssetRelativePath(m_LoadedDirectory));
        Localization::Reload(); // the scene's texts and the game use the new strings
        return true;
    }

    std::vector<std::string> LocalizationPanel::SceneKeys() const
    {
        std::vector<std::string> keys;
        if (!m_Scene)
            return keys;
        m_Scene->GetRegistry().view<TextComponent>().each([&](auto, const TextComponent& text) {
            if (!text.LocalizationKey.empty() && std::find(keys.begin(), keys.end(), text.LocalizationKey) == keys.end())
                keys.push_back(text.LocalizationKey);
        });
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    void LocalizationPanel::OnImGuiRender(bool* show)
    {
        if (!*show)
            return;
        if (m_FocusRequested)
        {
            ImGui::SetNextWindowFocus();
            m_FocusRequested = false;
        }
        if (m_FloatingRequested)
        {
            ImGui::SetNextWindowPos(ImVec2(m_FloatingRect.x, m_FloatingRect.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(m_FloatingRect.z, m_FloatingRect.w), ImGuiCond_Always);
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        }
        else
            ImGui::SetNextWindowSize(ImVec2(900.0f, 520.0f), ImGuiCond_FirstUseEver);
        const std::string title = std::format(ICON_FA_LANGUAGE "  {}{}###Localization", EditorText::Tr("Lokalizatsiya"), m_Dirty ? " *" : "");
        if (!ImGui::Begin(title.c_str(), show))
        {
            ImGui::End();
            return;
        }
        if (!Project::GetActive())
        {
            ImGui::TextDisabled("Loyiha ochilmagan");
            ImGui::End();
            return;
        }
        // Another project, or the offered languages / the files' folder changed (Project settings > Localization).
        // With unsaved edits the table stays as it is (Save writes it where it was read from, then it is read again).
        if (m_Loaded && (m_LoadedFor != Project::GetActive().get()))
            m_Loaded = false;
        if (m_Loaded && !m_Dirty)
        {
            std::vector<std::string> columns;
            for (const StringTable& t : m_Table.Languages)
                columns.push_back(t.Language);
            if (columns != Localization::GetLanguages() || m_LoadedDirectory != Project::GetLocalizationDirectory())
                m_Loaded = false;
        }
        if (!m_Loaded)
            Load();

        DrawToolbar();
        DrawSuggestions();
        DrawTable();
        DrawPluralPopup();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) &&
            m_Dirty)
            Save();
        ImGui::End();
    }

    void LocalizationPanel::DrawToolbar()
    {
        ImGui::BeginDisabled(!m_Dirty);
        if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Saqlash")))
            Save();
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Bekor qilish")))
            Load();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Saqlanmagan o'zgarishlarni tashlab, fayllarni qayta o'qish");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##filter", Tr(ICON_FA_MAGNIFYING_GLASS "  kalit yoki matn"), &m_Filter);
        ImGui::SameLine();
        ImGui::Checkbox(Tr("Faqat yetishmayotganlar"), &m_OnlyMissing);

        // A new key.
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        const bool enter = ImGui::InputTextWithHint("##newKey", Tr("yangi.kalit"), &m_NewKey, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(Tr(ICON_FA_PLUS "  Kalit")) || enter) && !m_NewKey.empty())
        {
            if (!LocalizationTable::IsValidKey(m_NewKey))
                m_KeyError = "Kalit noto'g'ri: bo'sh joy, '$' bilan boshlanish yoki '..' bo'lmasin";
            else if (m_Table.AddKey(m_NewKey))
            {
                m_Keys = m_Table.GetKeys();
                m_Dirty = true;
                m_KeyError.clear();
                m_Filter = m_NewKey;
                m_NewKey.clear();
            }
            else
                m_KeyError = std::format("'{}' kaliti bor", m_NewKey);
        }
        if (!m_KeyError.empty())
            ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s", m_KeyError.c_str());

        // The languages' state.
        std::string summary = std::format("{} ta kalit", m_Keys.size());
        for (const StringTable& t : m_Table.Languages)
            if (const size_t missing = m_Table.CountMissing(t.Language); missing > 0)
                summary += std::format("  ·  {}: {} yo'q", t.Language, missing);
        ImGui::TextDisabled("%s  ·  papka: %s", summary.c_str(), Project::GetAssetRelativePath(m_LoadedDirectory).c_str());
        if (m_LoadedDirectory != Project::GetLocalizationDirectory())
            ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Loyihaning papkasi o'zgardi (%s): saqlash eski papkaga yoziladi, "
                                            "keyin yangisi o'qiladi",
                               Project::GetAssetRelativePath(Project::GetLocalizationDirectory()).c_str());
        for (const std::string& error : m_LoadErrors)
            ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s", error.c_str());
        if (m_Table.Languages.empty())
            ImGui::TextColored(s_WarnColor, "Til yo'q: Loyiha sozlamalari > Lokalizatsiya'da til qo'shing");
    }

    void LocalizationPanel::DrawSuggestions()
    {
        // Keys the scene's texts / the game use that no language has.
        std::vector<std::string> unknown;
        for (const std::string& key : SceneKeys())
            if (std::find(m_Keys.begin(), m_Keys.end(), key) == m_Keys.end())
                unknown.push_back(key);
        for (const std::string& key : Localization::GetMissingKeys())
            if (std::find(m_Keys.begin(), m_Keys.end(), key) == m_Keys.end() && std::find(unknown.begin(), unknown.end(), key) == unknown.end())
                unknown.push_back(key);
        if (unknown.empty() || m_Table.Languages.empty())
            return;
        ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Ishlatilgan, lekin fayllarda yo'q:");
        for (const std::string& key : unknown)
        {
            ImGui::SameLine();
            ImGui::PushID(key.c_str());
            if (ImGui::SmallButton(std::format(ICON_FA_PLUS " {}", key).c_str()) && m_Table.AddKey(key))
            {
                m_Keys = m_Table.GetKeys();
                m_Dirty = true;
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered())
                UI::Tooltip("Hamma tillarga bo'sh qilib qo'shish (keyin tarjimasini yozing)");
        }
    }

    bool LocalizationPanel::DrawCell(const std::string& key, const std::string& language, float width)
    {
        const LocalizedString* entry = m_Table.Get(key, language);
        const bool missing = !entry || entry->IsEmpty();
        const std::string& reference = m_Table.Languages.empty() ? language : Localization::GetDefaultLanguage();
        if (missing)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, s_MissingBg);
        else if (m_Table.HasPlaceholderMismatch(key, language, reference))
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, s_MismatchBg);
        bool changed = false;
        ImGui::PushID(language.c_str());
        if (entry && entry->IsPlural())
        {
            const std::string summary = PluralSummary(*entry);
            if (ImGui::Button((summary.empty() ? std::string("(ko'plik)") : summary).c_str(), ImVec2(width, 0.0f)))
            {
                m_PluralKey = key;
                m_PluralLanguage = language;
                m_OpenPlural = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ko'plik shakllari (son bo'yicha): bosing - tahrirlash");
        }
        else
        {
            std::string text = entry ? entry->Text : std::string();
            ImGui::SetNextItemWidth(width);
            if (ImGui::InputTextWithHint("##text", missing ? "tarjima yo'q" : "", &text))
            {
                m_Table.Set(key, language, LocalizedString{ text, {} });
                changed = true;
            }
        }
        if (!missing && ImGui::IsItemHovered() && m_Table.HasPlaceholderMismatch(key, language, reference))
            UI::Tooltip(std::format("{{0}} kabi o'rinlar '{}' tilidagidan farq qiladi: o'yinda boshqa qiymat chiqishi mumkin", reference).c_str());
        ImGui::PopID();
        return changed;
    }

    void LocalizationPanel::DrawTable()
    {
        const int columns = 1 + (int)m_Table.Languages.size();
        if (m_Table.Languages.empty())
            return;
        // The rows shown: filter and "only missing".
        std::vector<const std::string*> rows;
        for (const std::string& key : m_Keys)
        {
            bool match = m_Filter.empty() || Contains(key, m_Filter);
            bool anyMissing = false;
            for (const StringTable& t : m_Table.Languages)
            {
                const LocalizedString* entry = t.Find(key);
                if (!match && entry && (Contains(entry->Text, m_Filter) || Contains(PluralSummary(*entry), m_Filter)))
                    match = true;
                anyMissing |= !entry || entry->IsEmpty();
            }
            if (match && (!m_OnlyMissing || anyMissing))
                rows.push_back(&key);
        }

        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInner | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable("##localization", columns, flags, ImVec2(0.0f, 0.0f)))
            return;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float keyWidth = std::clamp(avail * 0.24f, 150.0f, 280.0f);
        const float languageWidth = std::max(180.0f, (avail - keyWidth) / (float)m_Table.Languages.size() - 8.0f);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Kalit", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, keyWidth);
        std::vector<std::string> headers;
        for (const StringTable& t : m_Table.Languages)
        {
            const size_t missing = m_Table.CountMissing(t.Language);
            headers.push_back(std::format("{}{} ({}){}###col{}", t.Language == Localization::GetDefaultLanguage() ? ICON_FA_STAR " " : "",
                                          Localization::GetLanguageName(t.Language), t.Language,
                                          missing > 0 ? std::format("  " ICON_FA_TRIANGLE_EXCLAMATION " {}", missing) : std::string(), t.Language));
            ImGui::TableSetupColumn(headers.back().c_str(), ImGuiTableColumnFlags_WidthFixed, languageWidth);
        }
        ImGui::TableHeadersRow();

        // Changes to the keys wait until the rows are drawn (`rows` points into m_Keys).
        std::string removeKey, renameFrom, renameTo;
        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size());
        while (clipper.Step())
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++)
            {
                const std::string key = *rows[(size_t)r];
                ImGui::PushID(key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (m_RenamingKey == key)
                {
                    if (m_FocusRename)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_FocusRename = false;
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const bool done = ImGui::InputText("##rename", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (done)
                    {
                        // Done after the table: `rows` points into m_Keys, which the rename rebuilds.
                        if (m_RenameBuffer != key)
                        {
                            renameFrom = key;
                            renameTo = m_RenameBuffer;
                        }
                        m_RenamingKey.clear();
                    }
                    else if (ImGui::IsItemDeactivated())
                        m_RenamingKey.clear();
                }
                else
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Selectable(key.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        m_RenamingKey = key;
                        m_RenameBuffer = key;
                        m_FocusRename = true;
                    }
                    if (ImGui::BeginPopupContextItem("##keyMenu"))
                    {
                        if (ImGui::MenuItem(Tr(ICON_FA_PEN "  Qayta nomlash")))
                        {
                            m_RenamingKey = key;
                            m_RenameBuffer = key;
                            m_FocusRename = true;
                        }
                        if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Kalitni nusxalash")))
                            ImGui::SetClipboardText(key.c_str());
                        if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Localize(\"...\") nusxalash")))
                            ImGui::SetClipboardText(std::format("Localize(\"{}\")", key).c_str());
                        ImGui::Separator();
                        const LocalizedString* first = m_Table.Languages.empty() ? nullptr : m_Table.Get(key, m_Table.Languages[0].Language);
                        const bool plural = first && first->IsPlural();
                        if (ImGui::MenuItem(plural ? ICON_FA_FONT "  Oddiy matn qilish" : ICON_FA_LIST_OL "  Ko'plik shakllari (son bo'yicha)"))
                        {
                            for (StringTable& t : m_Table.Languages)
                            {
                                LocalizedString entry = t.Find(key) ? *t.Find(key) : LocalizedString();
                                if (plural)
                                    entry = LocalizedString{ entry.Get(PluralCategory::Other), {} };
                                else
                                {
                                    const std::string text = entry.Text;
                                    entry = LocalizedString();
                                    for (const std::string& category : Localization::GetPluralCategories(t.Language))
                                        entry.Plural[category] = text;
                                }
                                t.Strings[key] = std::move(entry);
                            }
                            m_Dirty = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  O'chirish (hamma tillardan)")))
                            removeKey = key;
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered() && m_RenamingKey.empty())
                        UI::Tooltip("Ikki marta bosing - qayta nomlash; o'ng tugma - menyu");
                }
                for (const StringTable& t : m_Table.Languages)
                {
                    ImGui::TableNextColumn();
                    const std::string language = t.Language; // DrawCell may change the table
                    if (DrawCell(key, language, -FLT_MIN))
                        m_Dirty = true;
                }
                ImGui::PopID();
            }
        ImGui::EndTable();
        rows.clear();
        if (!renameFrom.empty())
        {
            if (m_Table.RenameKey(renameFrom, renameTo))
            {
                m_Keys = m_Table.GetKeys();
                m_Dirty = true;
                m_KeyError.clear();
            }
            else
                m_KeyError = std::format("'{}' deb nomlab bo'lmadi (noto'g'ri yoki band)", renameTo);
        }
        if (!removeKey.empty() && m_Table.RemoveKey(removeKey))
        {
            m_Keys = m_Table.GetKeys();
            m_Dirty = true;
        }
    }

    void LocalizationPanel::DrawPluralPopup()
    {
        if (m_OpenPlural)
        {
            ImGui::OpenPopup("##plural");
            m_OpenPlural = false;
        }
        if (!ImGui::BeginPopup("##plural"))
            return;
        ImGui::TextUnformatted(std::format(ICON_FA_LIST_OL "  {}  ·  {}", m_PluralKey, Localization::GetLanguageName(m_PluralLanguage)).c_str());
        ImGui::TextDisabled("{0} - son (LocalizePlural(\"%s\", son))", m_PluralKey.c_str());
        ImGui::Separator();
        const LocalizedString* current = m_Table.Get(m_PluralKey, m_PluralLanguage);
        LocalizedString entry = current ? *current : LocalizedString();
        bool changed = false;
        for (const std::string& category : Localization::GetPluralCategories(m_PluralLanguage))
        {
            std::string text = entry.Plural.count(category) ? entry.Plural.find(category)->second : std::string();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(category.c_str());
            ImGui::SameLine(80.0f);
            ImGui::SetNextItemWidth(360.0f);
            ImGui::PushID(category.c_str());
            if (ImGui::InputText("##form", &text))
            {
                entry.Plural[category] = text;
                changed = true;
            }
            ImGui::PopID();
        }
        // An example of each form.
        ImGui::Separator();
        for (int n : { 0, 1, 2, 5, 21 })
        {
            const std::string& form = entry.Get(Localization::GetPluralCategory(m_PluralLanguage, n));
            ImGui::TextDisabled("%d -> %s", n, Localization::Format(form, n).c_str());
        }
        if (changed)
        {
            m_Table.Set(m_PluralKey, m_PluralLanguage, entry);
            m_Dirty = true;
        }
        ImGui::EndPopup();
    }

}
