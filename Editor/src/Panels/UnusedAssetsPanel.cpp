#include "UnusedAssetsPanel.h"
#include "ContentBrowserPanel.h"
#include "../AssetUsageCache.h"
#include "../UI.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    static std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    void UnusedAssetsPanel::Refresh()
    {
        if (!m_Usage)
            return;
        m_Usage->MarkDirty();
        m_Usage->Get(true);
        m_Revision = ~0ull;
        Update();
    }

    void UnusedAssetsPanel::Update()
    {
        if (!m_Usage)
            return;
        const AssetReferenceIndex& index = m_Usage->Get();
        if (m_Usage->GetRevision() == m_Revision)
            return;
        m_Revision = m_Usage->GetRevision();
        m_Unused = index.FindUnused(m_ReachableOnly);
        m_AssetCount = index.GetAssets().size();
        m_ScannedFiles = index.GetScannedFileCount();
        m_TotalSize = 0;
        std::unordered_set<std::string> still;
        for (const UnusedAsset& u : m_Unused)
        {
            m_TotalSize += u.Size;
            if (m_Checked.count(u.Path))
                still.insert(u.Path);
        }
        m_Checked = std::move(still);
        if (m_LogResult && index.IsBuilt())
        {
            m_LogResult = false;
            std::string list;
            for (size_t i = 0; i < m_Unused.size() && i < 12; i++)
                list += (i ? ", " : "") + m_Unused[i].Path;
            ZM_INFO("Ishlatilmagan assetlar{}: {} ta ({}), {} ta asset va {} ta fayl tekshirildi{}{}", m_ReachableOnly ? " (chuqur)" : "",
                    m_Unused.size(), AssetFiles::FormatSize(m_TotalSize), m_AssetCount, m_ScannedFiles, list.empty() ? "" : ": ", list);
            for (const MissingAssetReference& m : index.GetMissing())
                ZM_WARN("Buzilgan havola: {} -> {}", m.Name, m.Path);
        }
    }

    bool UnusedAssetsPanel::Matches(const UnusedAsset& asset) const
    {
        if (m_Filter.IsActive() && !m_Filter.Matches(asset.Type))
            return false;
        return m_Search.empty() || Lower(asset.Path).find(Lower(m_Search)) != std::string::npos;
    }

    void UnusedAssetsPanel::DrawToolbar()
    {
        if (ImGui::Button(Tr(ICON_FA_ARROWS_ROTATE "  Qayta skanerlash")))
            Refresh();
        ImGui::SameLine();
        if (ImGui::Checkbox(Tr("Chuqur tahlil"), &m_ReachableOnly))
            m_Revision = ~0ull;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'chiq: hech bir fayl havola qilmaydigan assetlar.\n"
                        "Yoqiq: faqat boshlang'ich sahna (loyiha fayli) va C++ koddan - sahnalar, prefablar, modellar orqali -\n"
                        "yetib boriladigan assetlar ishlatilgan hisoblanadi: hech qayerdan ochilmaydigan sahna va faqat u\n"
                        "ishlatadigan assetlar ham ro'yxatga tushadi.");
        ImGui::SameLine();
        std::string filterLabel = m_Filter.IsActive() ? std::format(ICON_FA_FILTER "  Turi ({})", m_Filter.Count()) : std::string(ICON_FA_FILTER "  Turi");
        if (ImGui::Button(filterLabel.c_str()))
            ImGui::OpenPopup("##unusedFilter");
        if (ImGui::BeginPopup("##unusedFilter"))
        {
            for (AssetType type : AssetTypeFilter::GetFilterableTypes())
            {
                if (!AssetTypes::IsRegistered(type))
                    continue;
                bool on = m_Filter.Has(type);
                if (ImGui::Checkbox(AssetFiles::GetTypeName(type), &on))
                    m_Filter.Set(type, on);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr("Hammasi"), nullptr, false, m_Filter.IsActive()))
                m_Filter.Clear();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##unusedSearch", Tr(ICON_FA_MAGNIFYING_GLASS "  Qidirish (yo'l)"), &m_Search);

        if (m_Usage && m_Usage->IsDirty())
            ImGui::TextDisabled(ICON_FA_ARROWS_ROTATE "  Yangilanmoqda...");
        else if (m_Unused.empty())
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.6f, 1.0f), ICON_FA_CIRCLE_CHECK "  Hamma assetlar ishlatilmoqda (%zu ta asset, %zu ta fayl tekshirildi)",
                               m_AssetCount, m_ScannedFiles);
        else
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %zu ta asset ishlatilmayapti, jami %s",
                               m_Unused.size(), AssetFiles::FormatSize(m_TotalSize).c_str());
        if (!m_Unused.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu ta asset, %zu ta fayl tekshirildi)", m_AssetCount, m_ScannedFiles);
        }
    }

    void UnusedAssetsPanel::DrawList()
    {
        std::vector<const UnusedAsset*> shown;
        for (const UnusedAsset& u : m_Unused)
            if (Matches(u))
                shown.push_back(&u);
        if (m_Unused.empty())
            return;
        if (shown.empty())
        {
            ImGui::TextDisabled("Filtrga mos asset yo'q");
            return;
        }
        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        float height = std::max(120.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3.0f);
        if (!ImGui::BeginTable("##unusedTable", 4, flags, ImVec2(0, height)))
            return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##check", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("Nomi", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Hajm", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Papka", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        bool all = std::all_of(shown.begin(), shown.end(), [&](const UnusedAsset* u) { return m_Checked.count(u->Path) > 0; });
        if (ImGui::Checkbox("##all", &all))
            for (const UnusedAsset* u : shown)
                all ? (void)m_Checked.insert(u->Path) : (void)m_Checked.erase(u->Path);
        for (int column = 1; column < 4; column++)
        {
            ImGui::TableSetColumnIndex(column);
            ImGui::TableHeader(ImGui::TableGetColumnName(column));
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)shown.size());
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
            {
                const UnusedAsset& u = *shown[row];
                ImGui::PushID(u.Path.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool checked = m_Checked.count(u.Path) > 0;
                if (ImGui::Checkbox("##check", &checked))
                    checked ? (void)m_Checked.insert(u.Path) : (void)m_Checked.erase(u.Path);
                ImGui::TableSetColumnIndex(1);
                ImU32 color = 0;
                const char* icon = ContentBrowserPanel::GetTypeIcon(u.Type, &color);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(icon);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                fs::path path(u.Path);
                if (ImGui::Selectable(path.filename().string().c_str(), m_Selected == u.Path,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                {
                    m_Selected = u.Path;
                    if (OnAssetSelected)
                        OnAssetSelected(Project::GetAssetFileSystemPath(u.Path));
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    UI::Tooltip(std::format("{}\n{}  -  {}", u.Path, AssetFiles::GetTypeName(u.Type), AssetFiles::FormatSize(u.Size)).c_str());
                if (ImGui::BeginPopupContextItem("##row"))
                {
                    if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Kontent brauzerda ko'rsatish")) && OnAssetSelected)
                        OnAssetSelected(Project::GetAssetFileSystemPath(u.Path));
                    if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Yo'lni nusxalash")))
                        ImGui::SetClipboardText(u.Path.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  O'chirish...")))
                    {
                        m_PendingDelete = { u.Path };
                        m_OpenDeletePopup = true;
                    }
                    ImGui::EndPopup();
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", AssetFiles::FormatSize(u.Size).c_str());
                ImGui::TableSetColumnIndex(3);
                std::string folder = path.parent_path().generic_string();
                ImGui::TextDisabled("%s", folder.empty() ? "Assets" : folder.c_str());
                ImGui::PopID();
            }
        ImGui::EndTable();

        ImGui::BeginDisabled(m_Checked.empty());
        std::string label = std::format(ICON_FA_TRASH "  Belgilanganlarni o'chirish ({})", m_Checked.size());
        if (ImGui::Button(label.c_str()))
        {
            m_PendingDelete.clear();
            for (const UnusedAsset& u : m_Unused)
                if (m_Checked.count(u.Path))
                    m_PendingDelete.push_back(u.Path);
            m_OpenDeletePopup = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Tekshiriladi: sahnalar, prefablar, materiallar, loyiha fayli, .gltf modellar ichidagi fayllar va C++ koddagi\n"
                        "satrlar (\"Prefabs/Ball.zprefab\"). Ochiq sahnaning saqlanmagan holati ham hisobga olinadi.\n"
                        "Kod yo'lni qismlardan yig'sa (\"Level\" + n), bunday assetlar bu yerda ishlatilmagan ko'rinadi.");
    }

    void UnusedAssetsPanel::DrawMissing()
    {
        if (!m_Usage)
            return;
        const auto& missing = m_Usage->Get().GetMissing();
        if (missing.empty())
            return;
        std::string title = std::format(ICON_FA_LINK_SLASH "  {} ({})###missingRefs", Tr("Buzilgan havolalar"), missing.size());
        if (!ImGui::CollapsingHeader(title.c_str()))
            return;
        for (const MissingAssetReference& m : missing)
        {
            ImGui::PushID(&m);
            if (ImGui::Selectable(m.Name.c_str(), false, ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x * 0.45f, 0)) &&
                OnAssetSelected && !m.Unsaved) // the open scene, never saved: no file to show
                OnAssetSelected(m.File);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), ICON_FA_ARROW_RIGHT "  %s", m.Path.c_str());
            ImGui::PopID();
        }
    }

    void UnusedAssetsPanel::DrawDeletePopup()
    {
        if (m_OpenDeletePopup)
        {
            ImGui::OpenPopup("###DeleteUnused");
            m_OpenDeletePopup = false;
            // Unused in the deep sense can still be named by files nothing opens (an old scene).
            m_DeleteUses.clear();
            if (m_Usage)
            {
                std::vector<fs::path> paths(m_PendingDelete.begin(), m_PendingDelete.end());
                m_DeleteUses = m_Usage->Get(true).FindUsesOutside(paths);
            }
        }
        if (!ImGui::BeginPopupModal(Tr(ICON_FA_TRASH "  Ishlatilmagan assetlarni o'chirish###DeleteUnused"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (m_PendingDelete.size() == 1)
            ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION "  '%s' o'chirilsinmi?", m_PendingDelete.front().c_str());
        else
            ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION "  %zu ta asset o'chirilsinmi?", m_PendingDelete.size());
        for (size_t i = 0; i < m_PendingDelete.size() && i < 10 && m_PendingDelete.size() > 1; i++)
            ImGui::BulletText("%s", m_PendingDelete[i].c_str());
        if (m_PendingDelete.size() > 10)
            ImGui::TextDisabled("... va yana %zu ta", m_PendingDelete.size() - 10);
        bool reveal = false;
        fs::path clicked;
        if (!m_DeleteUses.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %zu tasi hali ham boshqa fayllarda tilga olingan "
                               "(ishlatilmaydigan sahna / prefablarda):", m_DeleteUses.size());
            reveal = ContentBrowserPanel::DrawAssetsInUse(m_DeleteUses, &clicked);
        }
        ImGui::TextDisabled("Fayllar .zmeta bilan birga o'chadi. Bu amalni qaytarib bo'lmaydi.");
        ImGui::Spacing();
        if (ImGui::Button(Tr(ICON_FA_TRASH "  O'chirish"), ImVec2(140, 0)))
        {
            std::vector<fs::path> deleted;
            AssetRegistry* registry = Project::GetAssetRegistry();
            for (const std::string& relative : m_PendingDelete)
            {
                fs::path path = Project::GetAssetFileSystemPath(relative);
                std::string error;
                if (!AssetFiles::Remove(path, &error))
                {
                    ZM_ERROR("{}", error);
                    continue;
                }
                if (registry)
                    registry->OnFileRemoved(path);
                m_Checked.erase(relative);
                deleted.push_back(path);
            }
            if (!deleted.empty())
            {
                ZM_INFO("{} ta ishlatilmagan asset o'chirildi", deleted.size());
                if (OnAssetsDeleted)
                    OnAssetsDeleted(deleted);
            }
            m_PendingDelete.clear();
            if (m_Usage)
                m_Usage->MarkDirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Bekor qilish"), ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || reveal)
        {
            m_PendingDelete.clear();
            ImGui::CloseCurrentPopup();
            if (reveal && OnAssetSelected)
                OnAssetSelected(clicked);
        }
        ImGui::EndPopup();
    }

    void UnusedAssetsPanel::OnImGuiRender(bool* open)
    {
        if (!*open)
            return;
        ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Tr(ICON_FA_BROOM "  Ishlatilmagan assetlar###UnusedAssets"), open))
        {
            ImGui::End();
            return;
        }
        if (!Project::GetActive() || !m_Usage)
        {
            ImGui::TextDisabled("Loyiha ochilmagan");
            ImGui::End();
            return;
        }
        Update();
        DrawToolbar();
        ImGui::Separator();
        DrawList();
        DrawMissing();
        DrawDeletePopup();
        ImGui::End();
    }

}
