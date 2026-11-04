#include "ContentBrowserPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    static const char* s_PayloadType = "CONTENT_BROWSER_ITEM";

    static const char* IconFor(AssetType type, ImU32& color)
    {
        color = IM_COL32(200, 205, 215, 255);
        switch (type)
        {
            case AssetType::Folder:      color = IM_COL32(240, 196, 92, 255); return ICON_FA_FOLDER;
            case AssetType::Scene:       color = IM_COL32(46, 188, 148, 255); return ICON_FA_EARTH_ASIA;
            case AssetType::Prefab:      color = IM_COL32(120, 180, 255, 255); return ICON_FA_BOX;
            case AssetType::Texture:     color = IM_COL32(150, 200, 120, 255); return ICON_FA_FILE_IMAGE;
            case AssetType::Model:       color = IM_COL32(240, 140, 80, 255); return ICON_FA_CUBES;
            case AssetType::Audio:       color = IM_COL32(220, 120, 200, 255); return ICON_FA_FILE_AUDIO;
            case AssetType::Font:        return ICON_FA_FONT;
            case AssetType::Environment: color = IM_COL32(255, 200, 90, 255); return ICON_FA_GLOBE;
            case AssetType::Script:      color = IM_COL32(100, 160, 240, 255); return ICON_FA_FILE_CODE;
            case AssetType::Project:     color = IM_COL32(46, 188, 148, 255); return ICON_FA_GAMEPAD;
            default:                     return ICON_FA_FILE;
        }
    }

    static std::string FormatTime(fs::file_time_type time)
    {
        if (time == fs::file_time_type{})
            return {};
        auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(std::chrono::file_clock::to_sys(time));
        std::time_t t = std::chrono::system_clock::to_time_t(sys);
        std::tm local{};
        localtime_r(&t, &local);
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
        return text;
    }

    ContentBrowserPanel::ContentBrowserPanel() = default;

    void ContentBrowserPanel::SetRoot(const fs::path& root)
    {
        m_Root = root;
        m_Thumbnails.clear();
        NavigateTo(root);
    }

    void ContentBrowserPanel::NavigateTo(const fs::path& directory)
    {
        m_CurrentDirectory = directory;
        m_Selection.clear();
        m_Anchor.clear();
        m_ClickedSelected.clear();
        m_ListDirty = true;
    }

    void ContentBrowserPanel::SetCurrentDirectory(const fs::path& directory)
    {
        std::error_code ec;
        if (!m_Root.empty() && fs::is_directory(directory, ec) && AssetFiles::IsInside(directory, m_Root))
            NavigateTo(directory);
    }

    bool ContentBrowserPanel::ContainsScreenPoint(float x, float y) const
    {
        return m_Visible && x >= m_WindowMin.x && y >= m_WindowMin.y && x < m_WindowMax.x && y < m_WindowMax.y;
    }

    void ContentBrowserPanel::ShowNotice(std::string text, bool warning)
    {
        m_Notice = std::move(text);
        m_NoticeWarning = warning;
        m_NoticeTime = ImGui::GetTime();
    }

    void ContentBrowserPanel::Reveal(const std::vector<fs::path>& paths)
    {
        if (paths.empty() || m_Root.empty() || !AssetFiles::IsInside(paths.front(), m_Root))
            return;
        fs::path folder = paths.front().parent_path();
        NavigateTo(folder);
        m_Search.clear();
        for (const fs::path& p : paths)
        {
            if (p.parent_path() != folder)
                continue;
            std::error_code ec;
            if (!m_Settings->Filter.Matches(AssetFiles::GetType(p, fs::is_directory(p, ec))))
            {
                m_Settings->Filter.Clear(); // would hide what we show
                MarkSettingsModified();
            }
            m_Selection.push_back(p);
        }
        if (!m_Selection.empty())
            m_Anchor = m_Selection.back();
        m_ScrollToSelection = true;
        m_Thumbnails.clear();
    }

    Ref<Texture2D> ContentBrowserPanel::GetThumbnail(const fs::path& path)
    {
        if (AssetFiles::GetType(path) != AssetType::Texture)
            return nullptr;
        std::string key = path.string();
        auto it = m_Thumbnails.find(key);
        if (it != m_Thumbnails.end())
            return it->second;
        Ref<Texture2D> tex = TextureLibrary::Load(path);
        m_Thumbnails[key] = tex;
        return tex;
    }

    // ------------------------------------------------------------------ listing
    bool ContentBrowserPanel::IsRecursiveListing() const
    {
        return !m_Search.empty() || m_Settings->Filter.IsActive();
    }

    void ContentBrowserPanel::RefreshEntries()
    {
        std::string key = std::format("{}|{}|{}|{}|{}", m_CurrentDirectory.string(), m_Search, m_Settings->Filter.Mask,
                                      (int)m_Settings->SortBy, m_Settings->SortAscending);
        double now = ImGui::GetTime();
        if (!m_ListDirty && key == m_ListKey && now - m_ListTime < 1.0)
            return;
        m_ListDirty = false;
        m_ListKey = key;
        m_ListTime = now;

        AssetListOptions options;
        options.Search = m_Search;
        options.Filter = m_Settings->Filter;
        options.SortBy = m_Settings->SortBy;
        options.Ascending = m_Settings->SortAscending;
        options.Excluded = { m_Root / "Build", m_Root / "Binaries" };
        m_Entries = AssetFiles::List(m_CurrentDirectory, options);

        // Selected files that are gone (deleted outside, moved) are forgotten.
        std::erase_if(m_Selection, [&](const fs::path& p) {
            return std::none_of(m_Entries.begin(), m_Entries.end(), [&](const AssetEntry& e) { return e.Path == p; });
        });
    }

    // ------------------------------------------------------------------ selection
    bool ContentBrowserPanel::IsSelected(const fs::path& path) const
    {
        return std::find(m_Selection.begin(), m_Selection.end(), path) != m_Selection.end();
    }

    void ContentBrowserPanel::SelectOnly(const fs::path& path)
    {
        m_Selection = { path };
        m_Anchor = path;
    }

    void ContentBrowserPanel::Click(const fs::path& path, bool ctrl, bool shift)
    {
        if (shift)
        {
            auto indexOf = [&](const fs::path& p) {
                for (size_t i = 0; i < m_Entries.size(); i++)
                    if (m_Entries[i].Path == p) return (int)i;
                return -1;
            };
            int from = indexOf(m_Anchor), to = indexOf(path);
            if (from < 0 || to < 0)
            {
                SelectOnly(path);
                return;
            }
            if (!ctrl)
                m_Selection.clear();
            for (int i = std::min(from, to); i <= std::max(from, to); i++)
                if (!IsSelected(m_Entries[i].Path))
                    m_Selection.push_back(m_Entries[i].Path);
            return; // the anchor stays
        }
        if (ctrl)
        {
            if (IsSelected(path))
                std::erase(m_Selection, path);
            else
                m_Selection.push_back(path);
            m_Anchor = path;
            return;
        }
        SelectOnly(path);
    }

    std::vector<fs::path> ContentBrowserPanel::GetSelectionInOrder() const
    {
        std::vector<fs::path> out;
        for (const AssetEntry& e : m_Entries)
            if (IsSelected(e.Path))
                out.push_back(e.Path);
        return out;
    }

    // ------------------------------------------------------------------ moving
    bool ContentBrowserPanel::IsProtected(const fs::path& path) const
    {
        if (!Project::GetActive())
            return false;
        auto same = [](const fs::path& a, const fs::path& b) {
            return !a.empty() && !b.empty() && AssetFiles::IsInside(a, b) && AssetFiles::IsInside(b, a);
        };
        return same(path, Project::GetAssetDirectory()) || same(path, Project::GetSourceDirectory()) ||
               same(path, Project::GetProjectFilePath()) || same(path, m_Root / "CMakeLists.txt");
    }

    std::string ContentBrowserPanel::CheckMoveAll(const std::vector<fs::path>& paths, const fs::path& folder) const
    {
        std::string first;
        for (const fs::path& p : paths)
        {
            std::string reason = IsProtected(p) ? std::format("'{}' loyiha uchun kerak - ko'chirib bo'lmaydi", p.filename().string())
                                                : AssetFiles::CheckMove(p, folder, Project::GetAssetDirectory());
            if (reason.empty())
                return {};
            if (first.empty())
                first = std::move(reason);
        }
        return first.empty() ? std::string("Ko'chiradigan narsa yo'q") : first;
    }

    void ContentBrowserPanel::MoveItems(const std::vector<fs::path>& paths, const fs::path& folder)
    {
        std::vector<fs::path> movable;
        for (const fs::path& p : paths)
        {
            if (IsProtected(p))
                ZM_WARN("'{}' loyiha uchun kerak - ko'chirilmadi", p.filename().string());
            else
                movable.push_back(p);
        }
        if (movable.empty())
            return;
        AssetFiles::MoveResult result = AssetFiles::MoveInto(movable, folder, Project::GetAssetDirectory(), Project::GetSourceDirectory());
        ReportMove(result, folder, false);
    }

    void ContentBrowserPanel::ReportMove(const AssetFiles::MoveResult& result, const fs::path& folder, bool rename)
    {
        for (const std::string& error : result.Errors)
            ZM_WARN("{}", error);
        if (result.Moved.empty())
        {
            if (!result.Errors.empty())
                ShowNotice(result.Errors.front(), true);
            return;
        }

        m_Thumbnails.clear();
        m_ListDirty = true;
        m_Selection.clear();
        for (const auto& m : result.Moved)
            m_Selection.push_back(m.To);
        m_Anchor = m_Selection.back();

        std::string what = result.Moved.size() == 1 ? std::format("'{}'", result.Moved[0].To.filename().string())
                                                    : std::format("{} ta element", result.Moved.size());
        std::string text = rename ? std::format("{} deb nomlandi", what)
                                  : std::format("{} '{}' papkasiga ko'chirildi", what, folder.filename().string());
        ZM_INFO("{}", text);
        if (result.UpdatedReferences > 0)
        {
            ZM_INFO("{} ta faylda {} ta havola yangilandi", result.UpdatedFiles.size(), result.UpdatedReferences);
            text += std::format(" - {} ta havola yangilandi", result.UpdatedReferences);
        }
        bool warning = !result.Errors.empty();
        if (!result.SourceMentions.empty())
        {
            std::string files;
            for (const fs::path& f : result.SourceMentions)
                files += (files.empty() ? "" : ", ") + f.filename().string();
            ZM_WARN("C++ kodida eski yo'l yozilgan (o'zgartirilmadi): {}", files);
            text += " - C++ kodini tekshiring";
            warning = true;
        }
        ShowNotice(text, warning);
        if (OnAssetsMoved)
            OnAssetsMoved(result);
    }

    void ContentBrowserPanel::DrawFolderDropTarget(const fs::path& folder)
    {
        if (!ImGui::BeginDragDropTarget())
            return;
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(s_PayloadType, ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                                   ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (payload)
        {
            std::vector<fs::path> paths = m_DragPaths;
            if (paths.empty())
                paths.push_back(Project::GetAssetFileSystemPath((const char*)payload->Data));
            if (folder != m_DropCheckFolder)
            {
                m_DropCheckFolder = folder;
                m_DropCheckReason = CheckMoveAll(paths, folder);
            }
            bool ok = m_DropCheckReason.empty();

            ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImU32 color = ok ? UI::AccentU32() : IM_COL32(230, 90, 80, 255);
            fg->AddRect(min, max, color, 4.0f, 0, 2.0f);
            std::string hint = ok ? std::format(ICON_FA_TURN_UP "  '{}' papkasiga ko'chirish{}", folder.filename().string(),
                                                paths.size() > 1 ? std::format(" ({} ta)", paths.size()) : std::string())
                                  : std::string(ICON_FA_CIRCLE_XMARK "  ") + m_DropCheckReason;
            ImVec2 mouse = ImGui::GetMousePos();
            ImVec2 size = ImGui::CalcTextSize(hint.c_str());
            ImVec2 pos(mouse.x + 18.0f, mouse.y - size.y - 16.0f); // above the cursor: the drag preview is below it
            fg->AddRectFilled(ImVec2(pos.x - 6, pos.y - 3), ImVec2(pos.x + size.x + 6, pos.y + size.y + 3), IM_COL32(24, 26, 31, 235), 4.0f);
            fg->AddText(pos, ok ? IM_COL32(220, 240, 232, 255) : IM_COL32(255, 170, 160, 255), hint.c_str());

            if (payload->IsDelivery() && ok)
                m_PendingMove = { paths, folder };
        }
        ImGui::EndDragDropTarget();
    }

    // ------------------------------------------------------------------ toolbar
    void ContentBrowserPanel::DrawFilterPopup()
    {
        if (!ImGui::BeginPopup("##cbfilter"))
            return;
        ImGui::TextDisabled("Fayl turlari (barcha ichki papkalarda)");
        ImGui::Separator();
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        auto& filter = m_Settings->Filter;
        for (AssetType type : AssetTypeFilter::GetFilterableTypes())
        {
            ImU32 color;
            const char* icon = IconFor(type, color);
            ImGui::PushID((int)type);
            bool on = filter.Has(type);
            if (ImGui::Selectable("##type", on, 0, ImVec2(200, 0)))
            {
                filter.Set(type, !on);
                MarkSettingsModified();
            }
            ImGui::SameLine(6);
            ImGui::TextColored(ImVec4(on ? UI::AccentColor() : ImVec4(0.45f, 0.47f, 0.52f, 1.0f)), "%s", on ? ICON_FA_SQUARE_CHECK : ICON_FA_SQUARE);
            ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", icon);
            ImGui::SameLine();
            ImGui::TextUnformatted(AssetFiles::GetTypeName(type));
            ImGui::PopID();
        }
        ImGui::PopItemFlag();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_XMARK "  Filtrni tozalash", nullptr, false, filter.IsActive()))
        {
            filter.Clear();
            MarkSettingsModified();
        }
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::DrawToolbar()
    {
        bool canGoBack = m_CurrentDirectory != m_Root;
        ImGui::BeginDisabled(!canGoBack);
        if (UI::ToolbarButton(ICON_FA_ARROW_LEFT, "Orqaga (Backspace)"))
            NavigateTo(m_CurrentDirectory.parent_path());
        ImGui::EndDisabled();
        if (canGoBack)
            DrawFolderDropTarget(m_CurrentDirectory.parent_path());
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_HOUSE, "Loyiha papkasi"))
            NavigateTo(m_Root);
        DrawFolderDropTarget(m_Root);
        ImGui::SameLine();

        // Path segments (each one is a drop target too).
        fs::path rel = m_CurrentDirectory.lexically_relative(m_Root);
        fs::path accumulated = m_Root;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(m_Root.filename().string().c_str()))
            NavigateTo(m_Root);
        DrawFolderDropTarget(m_Root);
        if (!rel.empty() && rel != ".")
        {
            int index = 0;
            for (const auto& part : rel)
            {
                accumulated /= part;
                ImGui::SameLine(0, 2);
                ImGui::TextDisabled("/");
                ImGui::SameLine(0, 2);
                ImGui::PushID(index++);
                if (ImGui::Button(part.string().c_str()))
                    NavigateTo(accumulated);
                ImGui::PopID();
                if (accumulated != m_CurrentDirectory)
                    DrawFolderDropTarget(accumulated);
            }
        }
        ImGui::PopStyleColor();

        // Right side: type filter, search, grid / list, tile size.
        ImGuiStyle& style = ImGui::GetStyle();
        float button = ImGui::GetFrameHeight();
        bool grid = !m_Settings->ListView;
        float searchWidth = 190.0f, sliderWidth = grid ? 90.0f : 0.0f;
        float right = button * 3 + searchWidth + sliderWidth + style.ItemSpacing.x * (grid ? 5 : 4) + 8.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowContentRegionMax().x - right));

        auto& filter = m_Settings->Filter;
        std::string filterTip = filter.IsActive() ? std::format("Fayl turi bo'yicha filtr ({} ta tur)", filter.Count())
                                                  : std::string("Fayl turi bo'yicha filtr");
        if (UI::ToolbarButton(ICON_FA_FILTER, filterTip.c_str(), filter.IsActive(), ImVec2(button, button)))
            ImGui::OpenPopup("##cbfilter");
        DrawFilterPopup();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(searchWidth);
        if (ImGui::InputTextWithHint("##cbsearch", ICON_FA_MAGNIFYING_GLASS "  Qidirish", &m_Search))
            m_ListDirty = true;
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_TABLE_CELLS_LARGE, "Katakchalar", grid, ImVec2(button, button)) && !grid)
        {
            m_Settings->ListView = false;
            MarkSettingsModified();
        }
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_LIST, "Ro'yxat (nomi, turi, hajmi, sanasi)", !grid, ImVec2(button, button)) && grid)
        {
            m_Settings->ListView = true;
            MarkSettingsModified();
        }
        if (grid)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(sliderWidth);
            if (ImGui::SliderFloat("##size", &m_Settings->ThumbnailSize, ContentBrowserSettings::MinThumbnailSize,
                                   ContentBrowserSettings::MaxThumbnailSize, "%.0f"))
                MarkSettingsModified();
            if (ImGui::IsItemHovered())
                UI::Tooltip("Katakcha o'lchami (Ctrl + g'ildirak)");
        }
    }

    void ContentBrowserPanel::DrawFilterBar()
    {
        auto& filter = m_Settings->Filter;
        if (!filter.IsActive())
            return;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UI::AccentColor(), ICON_FA_FILTER "  Filtr:");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 2));
        for (AssetType type : AssetTypeFilter::GetFilterableTypes())
        {
            if (!filter.Has(type))
                continue;
            ImU32 color;
            const char* icon = IconFor(type, color);
            ImGui::SameLine();
            std::string chip = std::format("{} {}  " ICON_FA_XMARK "##chip{}", icon, AssetFiles::GetTypeName(type), (int)type);
            if (ImGui::Button(chip.c_str()))
            {
                filter.Set(type, false);
                MarkSettingsModified();
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Filtrdan olib tashlash");
        }
        ImGui::PopStyleVar(2);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu ta fayl (ichki papkalar bilan)", m_Entries.size());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::SmallButton("Tozalash"))
        {
            filter.Clear();
            MarkSettingsModified();
        }
        ImGui::PopStyleColor();
    }

    // ------------------------------------------------------------------ items
    void ContentBrowserPanel::Open(const AssetEntry& entry)
    {
        if (entry.IsDirectory)
        {
            m_Search.clear();
            NavigateTo(entry.Path);
        }
        else if (entry.Type == AssetType::Scene && OnOpenScene)
            OnOpenScene(entry.Path);
        else
            Platform::OpenInFileBrowser(entry.Path);
    }

    void ContentBrowserPanel::HandleItemInteraction(const AssetEntry& entry)
    {
        const fs::path& path = entry.Path;
        bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (io.KeyCtrl || io.KeyShift)
                Click(path, io.KeyCtrl, io.KeyShift);
            else if (!IsSelected(path))
                SelectOnly(path);
            else
                m_ClickedSelected = path; // maybe the start of dragging the whole selection
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsSelected(path))
            SelectOnly(path);

        if (ImGui::BeginDragDropSource())
        {
            if (!m_DragStarted)
            {
                m_DragStarted = true;
                m_DragPaths = IsSelected(path) ? GetSelectionInOrder() : std::vector<fs::path>{ path };
                m_DropCheckFolder.clear();
            }
            std::string rel = Project::GetAssetRelativePath(path);
            ImGui::SetDragDropPayload(s_PayloadType, rel.c_str(), rel.size() + 1);
            ImU32 c;
            const char* icon = IconFor(entry.Type, c);
            if (m_DragPaths.size() > 1)
                ImGui::Text("%s  %s  (+%zu ta)", icon, entry.Name.c_str(), m_DragPaths.size() - 1);
            else
                ImGui::Text("%s  %s", icon, entry.Name.c_str());
            ImGui::EndDragDropSource();
        }

        if (entry.IsDirectory)
            DrawFolderDropTarget(path);

        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift)
            Open(entry);

        if (ImGui::BeginPopupContextItem("##item"))
        {
            DrawItemMenu(entry);
            ImGui::EndPopup();
        }

        if (hovered && !ImGui::GetDragDropPayload() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            std::string tip = entry.Name + "\n" + AssetFiles::GetTypeName(entry.Type);
            if (!entry.IsDirectory)
                tip += "  -  " + AssetFiles::FormatSize(entry.Size);
            if (IsRecursiveListing())
                tip += "\n" + Project::GetAssetRelativePath(path.parent_path());
            UI::Tooltip(tip.c_str());
        }
    }

    void ContentBrowserPanel::DrawItemMenu(const AssetEntry& entry)
    {
        const fs::path& path = entry.Path;
        size_t count = std::max<size_t>(1, m_Selection.size());
        if (entry.IsDirectory && ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Ochish"))
            Open(entry);
        if (entry.Type == AssetType::Scene && ImGui::MenuItem(ICON_FA_EARTH_ASIA "  Sahnani ochish") && OnOpenScene)
            OnOpenScene(path);
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Tizimda ochish"))
            Platform::OpenInFileBrowser(path);
        if (ImGui::MenuItem(ICON_FA_COPY "  Yo'lni nusxalash"))
            ImGui::SetClipboardText(Project::GetAssetRelativePath(path).c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_FEATHER "  Nomini o'zgartirish", "F2", false, count == 1 && !IsProtected(path)))
        {
            m_RenameTarget = path;
            m_RenameBuffer = path.filename().string();
            m_OpenRenamePopup = true;
        }
        bool canMoveUp = m_CurrentDirectory != m_Root && !IsRecursiveListing();
        std::string upLabel = count > 1 ? std::format(ICON_FA_TURN_UP "  Yuqori papkaga ko'chirish ({} ta)", count)
                                        : std::string(ICON_FA_TURN_UP "  Yuqori papkaga ko'chirish");
        if (ImGui::MenuItem(upLabel.c_str(), nullptr, false, canMoveUp))
            m_PendingMove = { m_Selection.empty() ? std::vector<fs::path>{ path } : GetSelectionInOrder(), m_CurrentDirectory.parent_path() };
        std::string deleteLabel = count > 1 ? std::format(ICON_FA_TRASH "  O'chirish ({} ta)", count) : std::string(ICON_FA_TRASH "  O'chirish");
        if (ImGui::MenuItem(deleteLabel.c_str(), "Delete"))
            m_PendingDelete = m_Selection.empty() ? std::vector<fs::path>{ path } : GetSelectionInOrder();
    }

    void ContentBrowserPanel::DrawGrid()
    {
        float size = m_Settings->ThumbnailSize;
        float padding = 14.0f;
        float cellSize = size + padding;
        int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellSize));
        if (m_Entries.empty() || !ImGui::BeginTable("##cbgrid", columns))
            return;

        ImU32 accent = UI::AccentU32();
        for (const AssetEntry& entry : m_Entries)
        {
            ImGui::TableNextColumn();
            const fs::path& path = entry.Path;
            ImGui::PushID(path.string().c_str());

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec2 tileSize(size, size + ImGui::GetTextLineHeightWithSpacing() * 2.0f);
            ImGui::InvisibleButton("##tile", tileSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            bool hovered = ImGui::IsItemHovered();
            bool selected = IsSelected(path);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 tileMax(cursor.x + tileSize.x, cursor.y + tileSize.y);
            ImU32 fill = selected ? UI::AccentU32(hovered ? 0.34f : 0.24f) : (hovered ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 6));
            dl->AddRectFilled(cursor, tileMax, fill, 6.0f);
            if (selected)
                dl->AddRect(cursor, tileMax, accent, 6.0f, 0, 1.5f);
            if (selected && m_ScrollToSelection)
                ImGui::SetScrollHereY(0.5f);

            // icon / thumbnail
            ImVec2 iconMin(cursor.x + 6, cursor.y + 6);
            ImVec2 iconMax(cursor.x + size - 6, cursor.y + size - 6);
            Ref<Texture2D> thumb = entry.IsDirectory ? nullptr : GetThumbnail(path);
            if (thumb)
            {
                dl->AddImageRounded((ImTextureID)(uintptr_t)thumb->GetRendererID(), iconMin, iconMax, ImVec2(0, 1), ImVec2(1, 0),
                                    IM_COL32_WHITE, 4.0f);
            }
            else
            {
                ImU32 color;
                const char* icon = IconFor(entry.Type, color);
                ImFont* font = ImGuiLayer::GetLargeIconFont();
                float iconSize = (size - 12) * 0.62f;
                ImVec2 textSize = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
                ImVec2 pos(cursor.x + (size - textSize.x) * 0.5f, cursor.y + (size - textSize.y) * 0.5f);
                dl->AddText(font, iconSize, pos, color, icon);
            }

            // label (2 lines max, clipped)
            ImVec4 clip(cursor.x + 2, cursor.y + size, tileMax.x - 2, tileMax.y);
            ImVec2 labelSize = ImGui::CalcTextSize(entry.Name.c_str(), nullptr, false, size - 4);
            float lx = cursor.x + std::max(2.0f, (size - std::min(labelSize.x, size - 4)) * 0.5f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(lx, cursor.y + size), ImGui::GetColorU32(ImGuiCol_Text),
                        entry.Name.c_str(), nullptr, size - 4, &clip);

            HandleItemInteraction(entry);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void ContentBrowserPanel::DrawList()
    {
        if (m_Entries.empty())
            return;
        bool recursive = IsRecursiveListing();
        ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings |
                                ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
        if (!ImGui::BeginTable(recursive ? "##cblistdeep" : "##cblist", recursive ? 5 : 4, flags))
            return;

        // The first time the table exists it sorts like the saved settings.
        auto sortFlags = [&](AssetSortColumn column, ImGuiTableColumnFlags flags) {
            if (m_Settings->SortBy == column)
                flags |= ImGuiTableColumnFlags_DefaultSort | (m_Settings->SortAscending ? ImGuiTableColumnFlags_PreferSortAscending
                                                                                         : ImGuiTableColumnFlags_PreferSortDescending);
            return flags;
        };
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Nomi", sortFlags(AssetSortColumn::Name, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide),
                                0.0f, (ImGuiID)AssetSortColumn::Name);
        ImGui::TableSetupColumn("Turi", sortFlags(AssetSortColumn::Type, ImGuiTableColumnFlags_WidthFixed), 90.0f, (ImGuiID)AssetSortColumn::Type);
        ImGui::TableSetupColumn("Hajmi", sortFlags(AssetSortColumn::Size, ImGuiTableColumnFlags_WidthFixed), 70.0f, (ImGuiID)AssetSortColumn::Size);
        ImGui::TableSetupColumn("O'zgartirilgan", sortFlags(AssetSortColumn::Modified, ImGuiTableColumnFlags_WidthFixed), 118.0f,
                                (ImGuiID)AssetSortColumn::Modified);
        if (recursive)
            ImGui::TableSetupColumn("Joylashuv", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 150.0f, 99);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty)
        {
            if (specs->SpecsCount > 0)
            {
                auto column = (AssetSortColumn)specs->Specs[0].ColumnUserID;
                bool ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
                if (column != m_Settings->SortBy || ascending != m_Settings->SortAscending)
                {
                    m_Settings->SortBy = column;
                    m_Settings->SortAscending = ascending;
                    AssetFiles::Sort(m_Entries, column, ascending);
                    MarkSettingsModified();
                }
            }
            specs->SpecsDirty = false;
        }

        // Selected rows in the accent colour, like the tiles.
        ImGui::PushStyleColor(ImGuiCol_Header, UI::AccentU32(0.26f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, UI::AccentU32(0.18f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, UI::AccentU32(0.34f));
        float rowHeight = ImGui::GetTextLineHeight() + 6.0f;
        float iconWidth = rowHeight + 2.0f;
        ImGuiListClipper clipper;
        clipper.Begin((int)m_Entries.size(), rowHeight + ImGui::GetStyle().CellPadding.y * 2.0f);
        if (m_ScrollToSelection)
        {
            for (size_t i = 0; i < m_Entries.size(); i++)
                if (IsSelected(m_Entries[i].Path))
                {
                    clipper.IncludeItemByIndex((int)i);
                    break;
                }
        }
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
            {
                const AssetEntry& entry = m_Entries[row];
                ImGui::TableNextRow(0, rowHeight);
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(entry.Path.string().c_str());
                bool selected = IsSelected(entry.Path);
                if (selected && m_ScrollToSelection)
                    ImGui::SetScrollHereY(0.5f);
                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, rowHeight));
                HandleItemInteraction(entry);

                // name: small thumbnail or icon
                ImDrawList* dl = ImGui::GetWindowDrawList();
                Ref<Texture2D> thumb = entry.IsDirectory ? nullptr : GetThumbnail(entry.Path);
                if (thumb)
                    dl->AddImageRounded((ImTextureID)(uintptr_t)thumb->GetRendererID(), ImVec2(rowMin.x + 2, rowMin.y + 2),
                                        ImVec2(rowMin.x + iconWidth - 2, rowMin.y + rowHeight - 2), ImVec2(0, 1), ImVec2(1, 0),
                                        IM_COL32_WHITE, 3.0f);
                else
                {
                    ImU32 color;
                    const char* icon = IconFor(entry.Type, color);
                    ImVec2 iconSize = ImGui::CalcTextSize(icon);
                    dl->AddText(ImVec2(rowMin.x + (iconWidth - iconSize.x) * 0.5f, rowMin.y + (rowHeight - iconSize.y) * 0.5f), color, icon);
                }
                float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                dl->AddText(ImVec2(rowMin.x + iconWidth + 6.0f, textY), ImGui::GetColorU32(ImGuiCol_Text), entry.Name.c_str());

                float textOffset = (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                auto cellText = [&](const char* text, bool rightAlign = false) {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textOffset);
                    if (rightAlign)
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x));
                    ImGui::TextDisabled("%s", text);
                };
                ImGui::TableSetColumnIndex(1);
                cellText(AssetFiles::GetTypeName(entry.Type));
                ImGui::TableSetColumnIndex(2);
                if (!entry.IsDirectory)
                    cellText(AssetFiles::FormatSize(entry.Size).c_str(), true);
                ImGui::TableSetColumnIndex(3);
                cellText(FormatTime(entry.Modified).c_str());
                if (recursive)
                {
                    ImGui::TableSetColumnIndex(4);
                    fs::path where = entry.Path.parent_path().lexically_relative(m_CurrentDirectory);
                    cellText(where == "." ? "." : where.generic_string().c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::EndTable();
    }

    void ContentBrowserPanel::DrawFooter()
    {
        ImGui::Separator();
        uintmax_t selectedBytes = 0;
        for (const AssetEntry& e : m_Entries)
            if (!e.IsDirectory && IsSelected(e.Path))
                selectedBytes += e.Size;
        ImGui::TextDisabled("%zu ta element", m_Entries.size());
        if (!m_Selection.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled(" |  %zu ta tanlangan%s", m_Selection.size(),
                                selectedBytes > 0 ? (" (" + AssetFiles::FormatSize(selectedBytes) + ")").c_str() : "");
        }
        double age = ImGui::GetTime() - m_NoticeTime;
        if (!m_Notice.empty() && age < 6.0)
        {
            float alpha = (float)std::clamp(6.0 - age, 0.0, 1.0);
            ImVec4 color = m_NoticeWarning ? ImVec4(0.95f, 0.72f, 0.35f, alpha) : ImVec4(0.45f, 0.85f, 0.65f, alpha);
            std::string text = std::string(m_NoticeWarning ? ICON_FA_TRIANGLE_EXCLAMATION "  " : ICON_FA_CIRCLE_CHECK "  ") + m_Notice;
            float w = ImGui::CalcTextSize(text.c_str()).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowContentRegionMax().x - w));
            ImGui::TextColored(color, "%s", text.c_str());
        }
    }

    // ------------------------------------------------------------------ keyboard
    void ContentBrowserPanel::HandleKeyboard()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
            return;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            m_Selection.clear();
            for (const AssetEntry& e : m_Entries)
                m_Selection.push_back(e.Path);
            if (!m_Selection.empty())
                m_Anchor = m_Selection.front();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_Selection.clear();
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && m_CurrentDirectory != m_Root)
            NavigateTo(m_CurrentDirectory.parent_path());
        if (m_Selection.empty())
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            m_PendingDelete = GetSelectionInOrder();
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && m_Selection.size() == 1 && !IsProtected(m_Selection.front()))
        {
            m_RenameTarget = m_Selection.front();
            m_RenameBuffer = m_RenameTarget.filename().string();
            m_OpenRenamePopup = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        {
            fs::path target = m_Selection.back();
            auto it = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const AssetEntry& e) { return e.Path == target; });
            if (it != m_Entries.end())
                Open(*it);
        }
    }

    // ------------------------------------------------------------------ menus / popups
    void ContentBrowserPanel::DrawContextMenu()
    {
        if (ImGui::BeginPopupContextWindow("CBContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem(ICON_FA_FOLDER "  Yangi papka")) { m_NewKind = 0; m_NewName = "NewFolder"; m_OpenNewPopup = true; }
            if (ImGui::MenuItem(ICON_FA_EARTH_ASIA "  Yangi sahna")) { m_NewKind = 1; m_NewName = "NewScene"; m_OpenNewPopup = true; }
            if (ImGui::MenuItem(ICON_FA_FILE_CODE "  Yangi C++ skript...") && OnRequestNewScript) OnRequestNewScript();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CHECK_DOUBLE "  Hammasini tanlash", "Ctrl+A", false, !m_Entries.empty()))
            {
                m_Selection.clear();
                for (const AssetEntry& e : m_Entries)
                    m_Selection.push_back(e.Path);
            }
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Fayl menejerida ochish"))
                Platform::OpenInFileBrowser(m_CurrentDirectory);
            if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE "  Yangilash"))
                Refresh();
            ImGui::Separator();
            ImGui::TextDisabled(ICON_FA_FILE_IMPORT "  Import: fayllarni oynaga tashlang");
            ImGui::EndPopup();
        }

        if (m_OpenNewPopup)
        {
            ImGui::OpenPopup("###NewItem");
            m_OpenNewPopup = false;
        }
        if (ImGui::BeginPopupModal(ICON_FA_FOLDER "  Yaratish###NewItem", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const char* titles[] = { "Yangi papka", "Yangi sahna" };
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            ImGui::TextUnformatted(titles[m_NewKind]);
            ImGui::PopFont();
            ImGui::SetNextItemWidth(300);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##name", &m_NewName, ImGuiInputTextFlags_EnterReturnsTrue);
            if ((ImGui::Button(ICON_FA_CHECK "  Yaratish", ImVec2(140, 0)) || enter) && !m_NewName.empty())
            {
                std::error_code ec;
                if (m_NewKind == 0)
                    fs::create_directories(m_CurrentDirectory / m_NewName, ec);
                else if (m_NewKind == 1 && OnNewScene)
                    OnNewScene(m_CurrentDirectory / (m_NewName + ".zscene"));
                m_ListDirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_OpenRenamePopup) { ImGui::OpenPopup("###RenameItem"); m_OpenRenamePopup = false; }
        if (ImGui::BeginPopupModal(ICON_FA_FEATHER "  Nomini o'zgartirish###RenameItem", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(ICON_FA_FEATHER "  Yangi nom");
            ImGui::SetNextItemWidth(300);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##rename", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::TextDisabled("Sahna va prefablardagi havolalar ham yangilanadi.");
            if ((ImGui::Button(ICON_FA_CHECK "  OK", ImVec2(120, 0)) || enter) && !m_RenameBuffer.empty())
            {
                if (m_RenameBuffer != m_RenameTarget.filename().string())
                {
                    AssetFiles::MoveResult result = AssetFiles::Rename(m_RenameTarget, m_RenameBuffer, Project::GetAssetDirectory(),
                                                                       Project::GetSourceDirectory());
                    ReportMove(result, m_RenameTarget.parent_path(), true);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!m_PendingDelete.empty() && !ImGui::IsPopupOpen("###ConfirmDelete"))
            ImGui::OpenPopup("###ConfirmDelete");
        if (ImGui::BeginPopupModal(ICON_FA_TRASH "  O'chirish###ConfirmDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (m_PendingDelete.size() == 1)
                ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION "  '%s' o'chirilsinmi?", m_PendingDelete.front().filename().string().c_str());
            else
            {
                ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION "  %zu ta element o'chirilsinmi?", m_PendingDelete.size());
                for (size_t i = 0; i < m_PendingDelete.size() && i < 8; i++)
                    ImGui::BulletText("%s", m_PendingDelete[i].filename().string().c_str());
                if (m_PendingDelete.size() > 8)
                    ImGui::TextDisabled("... va yana %zu ta", m_PendingDelete.size() - 8);
            }
            ImGui::TextDisabled("Bu amalni qaytarib bo'lmaydi.");
            if (ImGui::Button(ICON_FA_TRASH "  O'chirish", ImVec2(140, 0)))
            {
                for (const fs::path& p : m_PendingDelete)
                {
                    if (IsProtected(p))
                    {
                        ZM_WARN("'{}' loyiha uchun kerak - o'chirilmadi", p.filename().string());
                        continue;
                    }
                    std::error_code ec;
                    fs::remove_all(p, ec);
                    if (ec) ZM_ERROR("'{}' o'chirilmadi: {}", p.filename().string(), ec.message());
                    m_Thumbnails.erase(p.string());
                }
                m_PendingDelete.clear();
                m_Selection.clear();
                m_ListDirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                m_PendingDelete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------ panel
    void ContentBrowserPanel::OnImGuiRender(bool* open)
    {
        m_Visible = false;
        // The drag of the previous frames ended (dropped or cancelled).
        if (m_DragStarted && !ImGui::GetDragDropPayload() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_DragStarted = false;
            m_DragPaths.clear();
            m_DropCheckFolder.clear();
        }
        if (!*open)
            return;
        if (!ImGui::Begin(ICON_FA_FOLDER_OPEN "  Kontent###ContentBrowser", open))
        {
            ImGui::End();
            return;
        }
        m_Visible = true;
        m_WindowMin = ImGui::GetWindowPos();
        m_WindowMax = ImVec2(m_WindowMin.x + ImGui::GetWindowWidth(), m_WindowMin.y + ImGui::GetWindowHeight());

        std::error_code ec;
        if (!m_Root.empty() && !fs::is_directory(m_CurrentDirectory, ec))
            NavigateTo(m_Root); // the folder was moved / deleted
        if (m_Root.empty() || !fs::exists(m_CurrentDirectory, ec))
        {
            ImGui::TextDisabled("Loyiha ochilmagan");
            ImGui::End();
            return;
        }

        RefreshEntries();
        DrawToolbar();
        DrawFilterBar();
        ImGui::Separator();

        float footer = ImGui::GetFrameHeightWithSpacing() + 2.0f;
        ImGuiIO& io = ImGui::GetIO();
        // Ctrl + wheel resizes the tiles instead of scrolling.
        ImGui::BeginChild("##items", ImVec2(0, -footer), ImGuiChildFlags_None,
                          !m_Settings->ListView && io.KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
        if (!m_Settings->ListView && ImGui::IsWindowHovered() && io.KeyCtrl && io.MouseWheel != 0.0f)
        {
            m_Settings->ThumbnailSize = std::clamp(m_Settings->ThumbnailSize + io.MouseWheel * 8.0f,
                                                   ContentBrowserSettings::MinThumbnailSize, ContentBrowserSettings::MaxThumbnailSize);
            MarkSettingsModified();
        }

        if (m_Settings->ListView)
            DrawList();
        else
            DrawGrid();
        m_ScrollToSelection = false;

        if (m_Entries.empty())
        {
            const char* text = IsRecursiveListing() ? "Hech narsa topilmadi"
                                                    : "Papka bo'sh. O'ng tugma bilan yangi narsa yarating yoki fayllarni oynaga tashlang.";
            ImGui::TextDisabled("%s", text);
        }

        // Pressed on a selected item and released without dragging: only that one stays selected.
        if (!m_ClickedSelected.empty() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (!m_DragStarted && !io.KeyCtrl && !io.KeyShift)
                SelectOnly(m_ClickedSelected);
            m_ClickedSelected.clear();
        }
        // A click on empty space clears the selection.
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift)
            m_Selection.clear();

        HandleKeyboard();
        DrawContextMenu();
        ImGui::EndChild();
        DrawFooter();
        ImGui::End();

        if (!m_PendingMove.Paths.empty())
        {
            PendingMove move = std::move(m_PendingMove);
            m_PendingMove = {};
            MoveItems(move.Paths, move.Destination);
        }
    }

}
