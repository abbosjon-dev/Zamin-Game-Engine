// The scene statistics window (Oyna > Sahna statistikasi): how many entities the open scene has
// (active / inactive, roots, depth, prefab instances), the components by type, the C++ scripts by
// class, the prefabs placed and the assets the scene names with what they hold and an estimate of
// their memory. The tables sort by any column; a row selects its entities (Ctrl: adds them),
// a double click frames them.
#include "EditorLayer.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace fs = std::filesystem;

namespace Zamin {

    namespace {
        // "12 345" (thin groups of three digits).
        std::string FormatCount(uint64_t value)
        {
            std::string digits = std::to_string(value);
            std::string out;
            for (size_t i = 0; i < digits.size(); i++)
            {
                if (i > 0 && (digits.size() - i) % 3 == 0)
                    out += ' ';
                out += digits[i];
            }
            return out;
        }

        bool ContainsNoCase(std::string_view text, std::string_view needle)
        {
            if (needle.empty())
                return true;
            auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                                  [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
            return it != text.end();
        }

        const char* ChannelName(uint32_t channels)
        {
            switch (channels)
            {
                case 1: return "kulrang";
                case 2: return "kulrang + alfa";
                case 3: return "RGB";
                default: return "RGBA";
            }
        }

        // What the asset's file holds, in one line.
        std::string DescribeAsset(const SceneAssetStats& a)
        {
            if (!a.Exists)
                return "fayl topilmadi";
            switch (a.Type)
            {
                case AssetType::Texture:
                case AssetType::Environment:
                    if (a.Width == 0)
                        return "o'qilmadi";
                    return std::format("{}x{}  {}{}{}", a.Width, a.Height, a.HDR ? "HDR" : ChannelName(a.Channels),
                                       a.Mipmaps ? ", mip" : "", a.Type == AssetType::Environment ? ", osmon" : "");
                case AssetType::Model:
                    if (a.Submeshes == 0)
                        return "o'qilmadi";
                    return std::format("{} vertex, {} indeks, {} qism", FormatCount(a.Vertices), FormatCount(a.Indices), a.Submeshes);
                case AssetType::Audio:
                    if (a.SampleRate == 0)
                        return "o'qilmadi";
                    return std::format("{}, {} Hz, {}{}", AssetInfo::FormatDuration(a.Duration), a.SampleRate,
                                       a.AudioChannels == 1 ? "mono" : a.AudioChannels == 2 ? "stereo" : std::format("{} kanal", a.AudioChannels),
                                       a.Streamed ? ", oqim" : "");
                default:
                    return {};
            }
        }

        // Sort specs of the current table, when they changed: the column (by user id) and direction.
        bool TakeSortSpecs(SceneStatsColumn& column, bool& ascending)
        {
            ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
            if (!specs || !specs->SpecsDirty)
                return false;
            specs->SpecsDirty = false;
            if (specs->SpecsCount == 0)
                return false;
            column = (SceneStatsColumn)specs->Specs[0].ColumnUserID;
            ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            return true;
        }

        ImGuiTableColumnFlags SortFlags(SceneStatsColumn column, SceneStatsColumn sorted, bool ascending, ImGuiTableColumnFlags flags)
        {
            if (column == sorted)
                flags |= ImGuiTableColumnFlags_DefaultSort |
                         (ascending ? ImGuiTableColumnFlags_PreferSortAscending : ImGuiTableColumnFlags_PreferSortDescending);
            return flags;
        }

        constexpr ImGuiTableFlags s_TableFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                                 ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
                                                 ImGuiTableFlags_PadOuterX;
    }

    void EditorLayer::UpdateSceneStatistics(bool force)
    {
        if (!m_ActiveScene)
            return;
        const double now = ImGui::GetTime();
        const bool otherScene = m_SceneStatsScene != m_ActiveScene.get();
        const bool running = m_SceneState != SceneState::Edit;
        // Not while something is dragged (a gizmo or a slider marks the scene dirty every frame).
        const bool due = force || otherScene || (m_SceneStatsDirty && now - m_SceneStatsTime >= 0.5 && !ImGui::IsAnyMouseDown()) ||
                         (running && now - m_SceneStatsTime >= 1.0);
        if (!due)
            return;
        m_SceneStatistics = m_SceneStatsCollector.Collect(*m_ActiveScene);
        SortSceneStatsGroups(m_SceneStatistics.Components, m_SceneStatsSort[0].first, m_SceneStatsSort[0].second);
        SortSceneStatsGroups(m_SceneStatistics.Scripts, m_SceneStatsSort[1].first, m_SceneStatsSort[1].second);
        SortSceneStatsGroups(m_SceneStatistics.Prefabs, m_SceneStatsSort[2].first, m_SceneStatsSort[2].second);
        SortSceneAssetStats(m_SceneStatistics.Assets, m_SceneStatsSort[3].first, m_SceneStatsSort[3].second);
        m_SceneStatsScene = m_ActiveScene.get();
        m_SceneStatsDirty = false;
        m_SceneStatsTime = now;
    }

    void EditorLayer::SelectStatsEntities(const std::vector<UUID>& ids, bool add, bool frame)
    {
        if (!m_ActiveScene)
            return;
        std::vector<Entity> picked;
        if (add)
            for (Entity e : m_SceneHierarchyPanel.GetSelection().GetEntities())
                if (e && e.GetScene() == m_ActiveScene.get())
                    picked.push_back(e);
        for (UUID id : ids)
            if (Entity e = m_ActiveScene->GetEntityByUUID(id); e && std::find(picked.begin(), picked.end(), e) == picked.end())
                picked.push_back(e);
        // The first of the row's entities stays the primary one (the inspector shows it).
        Entity primary;
        for (UUID id : ids)
            if ((primary = m_ActiveScene->GetEntityByUUID(id)))
                break;
        m_SceneHierarchyPanel.GetSelection().Set(picked, primary);
        if (frame && !picked.empty())
            FrameEntities(picked);
    }

    void EditorLayer::UI_StatsEntityTooltip(const std::vector<UUID>& ids)
    {
        if (ids.empty())
        {
            ImGui::TextDisabled("Obyekt yo'q");
            return;
        }
        constexpr size_t shown = 8;
        for (size_t i = 0; i < ids.size() && i < shown; i++)
            if (Entity e = m_ActiveScene->GetEntityByUUID(ids[i]))
                ImGui::BulletText("%s", e.GetName().c_str());
        if (ids.size() > shown)
            ImGui::TextDisabled("... va yana %zu ta", ids.size() - shown);
        ImGui::Separator();
        ImGui::TextDisabled("Bosish: tanlash (Ctrl: qo'shish), ikki marta: kamerani yo'naltirish");
    }

    void EditorLayer::UI_SceneStatistics()
    {
        if (!m_ShowSceneStatistics)
            return;
        ImGui::SetNextWindowSize(ImVec2(780, 560), ImGuiCond_FirstUseEver);
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + mainViewport->WorkSize.x * 0.5f, mainViewport->WorkPos.y + mainViewport->WorkSize.y * 0.5f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(Tr(ICON_FA_CHART_PIE "  Sahna statistikasi###SceneStatistics"), &m_ShowSceneStatistics))
        {
            ImGui::End();
            return;
        }
        if (!m_ActiveScene)
        {
            ImGui::TextDisabled("Sahna ochilmagan");
            ImGui::End();
            return;
        }
        UpdateSceneStatistics(false);
        const SceneStatistics& s = m_SceneStatistics;

        // ---- the summary: a card per subject
        auto card = [&](const char* icon, const char* label, const std::string& value, const std::string& detail, const char* tooltip) {
            ImGui::TableNextColumn();
            ImGui::BeginGroup();
            ImGui::TextDisabled("%s  %s", icon, label);
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", detail.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::EndGroup();
            if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip(tooltip);
        };
        if (ImGui::BeginTable("##statsCards", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX))
        {
            card(ICON_FA_CUBES, "Obyektlar", FormatCount(s.Entities),
                 std::format("{} faol, {} nofaol\n{} ildiz, chuqurlik {}", s.Active, s.Inactive, s.Roots, s.MaxDepth),
                 "Faol: o'zi va barcha ota-onalari yoqilgan obyektlar. Nofaol: o'zi yoki biror ota-onasi o'chirilgan.\n"
                 "Chuqurlik: ierarxiyaning eng chuqur darajasi (ildiz = 0).");
            card(ICON_FA_LAYER_GROUP, "Komponentlar", FormatCount(s.ComponentTotal),
                 std::format("{} xil tur\n{} yashirin, {} qulflangan", s.Components.size(), s.HiddenInEditor, s.LockedInEditor),
                 "Har bir obyektning har bir komponenti (Transform ham). Yashirin / qulflangan: ierarxiyadagi ko'z va qulf.");
            card(ICON_FA_CODE, "Skriptlar va prefablar", std::format("{}  /  {}", FormatCount(s.ScriptComponents), FormatCount(s.PrefabInstances)),
                 std::format("{} skript klassi\n{} prefab nusxasi ({} xil prefab)", s.Scripts.size(), s.PrefabInstances, s.Prefabs.size()),
                 "Prefab nusxalari: editorda prefabdan qo'yilgan obyektlar (ularning ildizlari).");
            ImGui::TableNextRow();
            card(ICON_FA_FOLDER_OPEN, "Assetlar", FormatCount(s.Assets.size()),
                 std::format("{} rasm, {} model\n{} audio, {} material", s.CountAssets(AssetType::Texture) + s.CountAssets(AssetType::Environment),
                             s.CountAssets(AssetType::Model), s.CountAssets(AssetType::Audio), s.CountAssets(AssetType::Material)),
                 "Sahna obyektlarining komponentlari (va sahna osmoni) nomlagan fayllar; materiallarning rasmlari ham.");
            card(ICON_FA_MEMORY, "Taxminiy xotira", AssetFiles::FormatSize(s.GetTotalMemory()),
                 std::format("teksturalar {}, meshlar {}\naudio {}", AssetFiles::FormatSize(s.TextureMemory), AssetFiles::FormatSize(s.MeshMemory),
                             AssetFiles::FormatSize(s.AudioMemory)),
                 "Yuklangandagi taxminiy hajm: teksturalar GPU'da mip darajalari bilan, model vertex / indeks buferlari,\n"
                 "xotiraga ochilgan tovushlar (oqim bilan ijro etiladiganlar hisobga olinmaydi). Fayllar sarlavhasidan o'qiladi.");
            card(ICON_FA_SHAPES, "Geometriya", FormatCount(s.MeshVertices) + " vertex",
                 std::format("{} indeks\n{} uchburchak", FormatCount(s.MeshIndices), FormatCount(s.MeshIndices / 3)),
                 "Sahnadagi modellar fayllaridagi geometriya (har model bir marta).");
            ImGui::EndTable();
        }

        // ---- refresh and search
        ImGui::Spacing();
        if (ImGui::Button(Tr(ICON_FA_ARROWS_ROTATE "  Yangilash")))
        {
            m_SceneStatsCollector.ClearCache(); // files changed outside the editor are read again
            UpdateSceneStatistics(true);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(m_SceneState == SceneState::Edit ? "Sahna o'zgarganda o'zi yangilanadi; tugma fayllarni ham qayta o'qiydi."
                                                         : "O'yin ketmoqda: har soniyada yangilanadi.");
        ImGui::SameLine();
        UI::SearchBox("##statsSearch", m_SceneStatsSearch, "Nom bo'yicha qidirish (komponent, klass, asset)...");

        const std::string& search = m_SceneStatsSearch;
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        ImGui::PushStyleColor(ImGuiCol_Header, UI::AccentU32(0.26f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, UI::AccentU32(0.18f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, UI::AccentU32(0.34f));
        // A row's selectable: click selects its entities, a double click frames them.
        auto rowSelectable = [&](const std::string& key, const char* label, const std::vector<UUID>& ids) {
            const bool picked = m_SceneStatsPicked == key;
            if (ImGui::Selectable(label, picked, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick |
                                                     ImGuiSelectableFlags_AllowOverlap))
            {
                m_SceneStatsPicked = key;
                SelectStatsEntities(ids, ctrl, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
            }
        };
        auto tabFlags = [&](int tab) {
            ImGuiTabItemFlags flags = m_SceneStatsTab == tab ? ImGuiTabItemFlags_SetSelected : 0;
            return flags;
        };

        if (ImGui::BeginTabBar("##statsTabs"))
        {
            // ---- components
            if (ImGui::BeginTabItem(std::format(ICON_FA_LAYER_GROUP "  Komponentlar ({})###statsComponents", s.Components.size()).c_str(), nullptr, tabFlags(0)))
            {
                if (ImGui::BeginTable("##statsComponentTable", 3, s_TableFlags))
                {
                    auto& sort = m_SceneStatsSort[0];
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Komponent", SortFlags(SceneStatsColumn::Name, sort.first, sort.second, ImGuiTableColumnFlags_WidthStretch),
                                            0.0f, (ImGuiID)SceneStatsColumn::Name);
                    ImGui::TableSetupColumn("Obyektlar", SortFlags(SceneStatsColumn::Count, sort.first, sort.second, ImGuiTableColumnFlags_WidthFixed |
                                            ImGuiTableColumnFlags_PreferSortDescending), 80.0f, (ImGuiID)SceneStatsColumn::Count);
                    ImGui::TableSetupColumn("Ulushi", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 160.0f, 99);
                    ImGui::TableHeadersRow();
                    if (TakeSortSpecs(sort.first, sort.second))
                        SortSceneStatsGroups(m_SceneStatistics.Components, sort.first, sort.second);
                    for (const SceneStatsGroup& g : s.Components)
                    {
                        const char* name = SceneHierarchyPanel::GetComponentDisplayName(g.Key);
                        if (!ContainsNoCase(name, search) && !ContainsNoCase(g.Key, search))
                            continue;
                        ImGui::PushID(g.Key.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        rowSelectable("c:" + g.Key, name, g.Entities);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
                        {
                            ImGui::PushFont(ImGuiLayer::GetBoldFont());
                            ImGui::Text("%s  (%s)", name, g.Key.c_str());
                            ImGui::PopFont();
                            UI_StatsEntityTooltip(g.Entities);
                            ImGui::EndTooltip();
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(FormatCount(g.Count()).c_str());
                        ImGui::TableSetColumnIndex(2);
                        float share = s.Entities ? (float)g.Count() / (float)s.Entities : 0.0f;
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, UI::AccentColor(0.75f));
                        ImGui::ProgressBar(share, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()), std::format("{:.0f}%", share * 100.0f).c_str());
                        ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            // ---- scripts
            if (ImGui::BeginTabItem(std::format(ICON_FA_CODE "  Skriptlar ({})###statsScripts", s.Scripts.size()).c_str(), nullptr, tabFlags(1)))
            {
                if (s.Scripts.empty())
                    ImGui::TextDisabled("Sahnada C++ skript komponenti yo'q");
                else if (ImGui::BeginTable("##statsScriptTable", 4, s_TableFlags))
                {
                    auto& sort = m_SceneStatsSort[1];
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Klass", SortFlags(SceneStatsColumn::Name, sort.first, sort.second, ImGuiTableColumnFlags_WidthStretch),
                                            0.0f, (ImGuiID)SceneStatsColumn::Name);
                    ImGui::TableSetupColumn("Obyektlar", SortFlags(SceneStatsColumn::Count, sort.first, sort.second, ImGuiTableColumnFlags_WidthFixed |
                                            ImGuiTableColumnFlags_PreferSortDescending), 80.0f, (ImGuiID)SceneStatsColumn::Count);
                    ImGui::TableSetupColumn("Holat", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 170.0f, 99);
                    ImGui::TableSetupColumn("Manba", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 190.0f, 98);
                    ImGui::TableHeadersRow();
                    if (TakeSortSpecs(sort.first, sort.second))
                        SortSceneStatsGroups(m_SceneStatistics.Scripts, sort.first, sort.second);
                    for (const SceneStatsGroup& g : s.Scripts)
                    {
                        const std::string label = g.Key.empty() ? std::string("(klass tanlanmagan)") : g.Key;
                        if (!ContainsNoCase(label, search))
                            continue;
                        ImGui::PushID(label.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        rowSelectable("s:" + g.Key, label.c_str(), g.Entities);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
                        {
                            UI_StatsEntityTooltip(g.Entities);
                            ImGui::EndTooltip();
                        }
                        const SourceLocation& source = g.Key.empty() ? SourceLocation() : GetScriptSource(g.Key);
                        if (ImGui::BeginPopupContextItem("##scriptRow"))
                        {
                            if (ImGui::MenuItem(Tr(ICON_FA_PEN_TO_SQUARE "  Kodini ochish"), nullptr, false, source.IsValid()))
                                OpenInExternalEditor(source.File, source.Line, source.Column);
                            if (ImGui::MenuItem(Tr(ICON_FA_OBJECT_GROUP "  Obyektlarni tanlash")))
                                SelectStatsEntities(g.Entities, false, false);
                            ImGui::EndPopup();
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(FormatCount(g.Count()).c_str());
                        ImGui::TableSetColumnIndex(3);
                        if (source.IsValid())
                        {
                            const std::string where = std::format(ICON_FA_PEN_TO_SQUARE "  {}:{}", source.File.filename().string(), source.Line);
                            if (ImGui::SmallButton(where.c_str()))
                                OpenInExternalEditor(source.File, source.Line, source.Column);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                UI::Tooltip(std::format("{}\nTashqi muharrirda ochish", source.File.string()).c_str());
                        }
                        else
                            ImGui::TextDisabled("%s", g.Key.empty() ? "-" : "topilmadi");
                        ImGui::TableSetColumnIndex(2);
                        if (g.Key.empty())
                            ImGui::TextDisabled("-");
                        else if (ScriptRegistry::Find(g.Key))
                            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_CHECK "  ro'yxatda");
                        else
                            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  modulda topilmadi");
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            // ---- prefabs
            if (ImGui::BeginTabItem(std::format(ICON_FA_BOX "  Prefablar ({})###statsPrefabs", s.Prefabs.size()).c_str(), nullptr, tabFlags(2)))
            {
                if (s.Prefabs.empty())
                    ImGui::TextDisabled("Sahnada prefab nusxasi yo'q (prefabni kontent brauzerdan sahnaga sudrab qo'ying)");
                else if (ImGui::BeginTable("##statsPrefabTable", 2, s_TableFlags))
                {
                    auto& sort = m_SceneStatsSort[2];
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Prefab", SortFlags(SceneStatsColumn::Name, sort.first, sort.second, ImGuiTableColumnFlags_WidthStretch),
                                            0.0f, (ImGuiID)SceneStatsColumn::Name);
                    ImGui::TableSetupColumn("Nusxalar", SortFlags(SceneStatsColumn::Count, sort.first, sort.second, ImGuiTableColumnFlags_WidthFixed |
                                            ImGuiTableColumnFlags_PreferSortDescending), 80.0f, (ImGuiID)SceneStatsColumn::Count);
                    ImGui::TableHeadersRow();
                    if (TakeSortSpecs(sort.first, sort.second))
                        SortSceneStatsGroups(m_SceneStatistics.Prefabs, sort.first, sort.second);
                    for (const SceneStatsGroup& g : s.Prefabs)
                    {
                        if (!ContainsNoCase(g.Key, search))
                            continue;
                        ImGui::PushID(g.Key.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImU32 color = 0;
                        const char* icon = ContentBrowserPanel::GetTypeIcon(AssetType::Prefab, &color);
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                        ImGui::TextUnformatted(icon);
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                        rowSelectable("p:" + g.Key, g.Key.c_str(), g.Entities);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
                        {
                            UI_StatsEntityTooltip(g.Entities);
                            ImGui::EndTooltip();
                        }
                        if (ImGui::BeginPopupContextItem("##prefabRow"))
                        {
                            if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Kontent brauzerda ko'rsatish")))
                                RevealAsset(Project::GetAssetFileSystemPath(g.Key));
                            if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Yo'lni nusxalash")))
                                ImGui::SetClipboardText(g.Key.c_str());
                            ImGui::EndPopup();
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(FormatCount(g.Count()).c_str());
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            // ---- assets
            if (ImGui::BeginTabItem(std::format(ICON_FA_FOLDER_OPEN "  Assetlar ({})###statsAssets", s.Assets.size()).c_str(), nullptr, tabFlags(3)))
            {
                // The type filter: the types the scene names.
                std::vector<AssetType> present;
                for (const SceneAssetStats& a : s.Assets)
                    if (std::find(present.begin(), present.end(), a.Type) == present.end())
                        present.push_back(a.Type);
                std::sort(present.begin(), present.end());
                ImGui::SetNextItemWidth(170.0f);
                const char* current = m_SceneStatsAssetType < 0 ? "Hamma turlar" : AssetFiles::GetTypeName((AssetType)m_SceneStatsAssetType);
                if (ImGui::BeginCombo("##statsAssetType", current))
                {
                    if (ImGui::Selectable(Tr("Hamma turlar"), m_SceneStatsAssetType < 0))
                        m_SceneStatsAssetType = -1;
                    for (AssetType type : present)
                        if (ImGui::Selectable(std::format("{} ({})", AssetFiles::GetTypeName(type), s.CountAssets(type)).c_str(),
                                              m_SceneStatsAssetType == (int)type))
                            m_SceneStatsAssetType = (int)type;
                    ImGui::EndCombo();
                }

                std::vector<const SceneAssetStats*> shown;
                uint64_t shownSize = 0, shownMemory = 0;
                size_t missing = 0;
                for (const SceneAssetStats& a : s.Assets)
                {
                    if (m_SceneStatsAssetType >= 0 && (int)a.Type != m_SceneStatsAssetType)
                        continue;
                    if (!ContainsNoCase(a.Path, search))
                        continue;
                    shown.push_back(&a);
                    shownSize += a.FileSize;
                    shownMemory += a.Memory;
                    missing += a.Exists ? 0 : 1;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%zu ta  |  fayllar %s  |  xotira %s", shown.size(), AssetFiles::FormatSize(shownSize).c_str(),
                                    AssetFiles::FormatSize(shownMemory).c_str());
                if (missing > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), ICON_FA_LINK_SLASH "  %zu ta topilmadi", missing);
                }

                if (ImGui::BeginTable("##statsAssetTable", 6, s_TableFlags))
                {
                    auto& sort = m_SceneStatsSort[3];
                    auto column = [&](const char* name, SceneStatsColumn id, ImGuiTableColumnFlags flags, float width) {
                        ImGui::TableSetupColumn(name, SortFlags(id, sort.first, sort.second, flags), width, (ImGuiID)id);
                    };
                    ImGui::TableSetupScrollFreeze(0, 1);
                    column("Asset", SceneStatsColumn::Name, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f);
                    column("Turi", SceneStatsColumn::Type, ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    column("Obyektlar", SceneStatsColumn::Count, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 70.0f);
                    ImGui::TableSetupColumn("Ma'lumot", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 230.0f, 99);
                    column("Fayl", SceneStatsColumn::Size, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 70.0f);
                    column("Xotira", SceneStatsColumn::Memory, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80.0f);
                    ImGui::TableHeadersRow();
                    if (TakeSortSpecs(sort.first, sort.second))
                        SortSceneAssetStats(m_SceneStatistics.Assets, sort.first, sort.second);

                    ImGuiListClipper clipper;
                    clipper.Begin((int)shown.size());
                    while (clipper.Step())
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                        {
                            const SceneAssetStats& a = *shown[row];
                            ImGui::PushID(a.Path.c_str());
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImU32 color = 0;
                            const char* icon = ContentBrowserPanel::GetTypeIcon(a.Type, &color);
                            ImGui::PushStyleColor(ImGuiCol_Text, a.Exists ? color : IM_COL32(240, 110, 100, 255));
                            ImGui::TextUnformatted(a.Exists ? icon : ICON_FA_LINK_SLASH);
                            ImGui::PopStyleColor();
                            ImGui::SameLine();
                            const std::string name = fs::path(a.Path).filename().string();
                            rowSelectable("a:" + a.Path, name.c_str(), a.Entities);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
                            {
                                ImGui::PushFont(ImGuiLayer::GetBoldFont());
                                ImGui::TextUnformatted(a.Path.c_str());
                                ImGui::PopFont();
                                std::string components;
                                for (const std::string& c : a.Components)
                                    components += (components.empty() ? "" : ", ") +
                                                  std::string(c == "Settings" ? "Sahna sozlamalari" : SceneHierarchyPanel::GetComponentDisplayName(c));
                                ImGui::TextDisabled("%u marta nomlangan: %s", a.Uses, components.c_str());
                                for (const std::string& material : a.ViaMaterials)
                                    ImGui::TextDisabled(ICON_FA_PALETTE "  material orqali: %s", material.c_str());
                                ImGui::Separator();
                                UI_StatsEntityTooltip(a.Entities);
                                ImGui::EndTooltip();
                            }
                            if (ImGui::BeginPopupContextItem("##assetRow"))
                            {
                                if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Kontent brauzerda ko'rsatish"), nullptr, false, a.Exists))
                                    RevealAsset(Project::GetAssetFileSystemPath(a.Path));
                                if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Yo'lni nusxalash")))
                                    ImGui::SetClipboardText(a.Path.c_str());
                                if (ImGui::MenuItem(Tr(ICON_FA_OBJECT_GROUP "  Obyektlarni tanlash"), nullptr, false, !a.Entities.empty()))
                                    SelectStatsEntities(a.Entities, false, false);
                                ImGui::EndPopup();
                            }
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextDisabled("%s", AssetFiles::GetTypeName(a.Type));
                            ImGui::TableSetColumnIndex(2);
                            if (a.Entities.empty())
                                ImGui::TextDisabled("%s", a.Components.size() == 1 && a.Components[0] == "Settings" ? "sahna" : "-");
                            else
                                ImGui::TextUnformatted(FormatCount(a.Entities.size()).c_str());
                            ImGui::TableSetColumnIndex(3);
                            const std::string info = DescribeAsset(a);
                            if (!a.Exists)
                                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), "%s", info.c_str());
                            else
                                ImGui::TextDisabled("%s", info.c_str());
                            ImGui::TableSetColumnIndex(4);
                            ImGui::TextDisabled("%s", a.Exists ? AssetFiles::FormatSize(a.FileSize).c_str() : "-");
                            ImGui::TableSetColumnIndex(5);
                            if (a.Memory > 0)
                                ImGui::TextUnformatted(AssetFiles::FormatSize(a.Memory).c_str());
                            else
                                ImGui::TextDisabled("-");
                            ImGui::PopID();
                        }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        m_SceneStatsTab = -1;
        ImGui::PopStyleColor(3);
        ImGui::End();
    }

}
