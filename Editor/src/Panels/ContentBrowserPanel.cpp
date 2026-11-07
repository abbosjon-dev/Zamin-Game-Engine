#include "ContentBrowserPanel.h"
#include "../AssetUsageCache.h"
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
            case AssetType::Scene:       color = UI::AccentU32(); return ICON_FA_EARTH_ASIA;
            case AssetType::Prefab:      color = IM_COL32(120, 180, 255, 255); return ICON_FA_BOX;
            case AssetType::Texture:     color = IM_COL32(150, 200, 120, 255); return ICON_FA_FILE_IMAGE;
            case AssetType::Model:       color = IM_COL32(240, 140, 80, 255); return ICON_FA_CUBES;
            case AssetType::Audio:       color = IM_COL32(220, 120, 200, 255); return ICON_FA_FILE_AUDIO;
            case AssetType::Font:        return ICON_FA_FONT;
            case AssetType::Environment: color = IM_COL32(255, 200, 90, 255); return ICON_FA_GLOBE;
            case AssetType::Material:    color = IM_COL32(200, 160, 255, 255); return ICON_FA_PALETTE;
            case AssetType::PhysicsMaterial: color = IM_COL32(110, 215, 230, 255); return ICON_FA_GEM;
            case AssetType::Animation:   color = IM_COL32(255, 170, 90, 255); return ICON_FA_TIMELINE;
            case AssetType::SpriteAnimation: color = IM_COL32(255, 140, 170, 255); return ICON_FA_FILM;
            case AssetType::ParticlePreset: color = IM_COL32(255, 190, 90, 255); return ICON_FA_WAND_MAGIC_SPARKLES;
            case AssetType::Script:      color = IM_COL32(100, 160, 240, 255); return ICON_FA_FILE_CODE;
            case AssetType::Project:     color = UI::AccentU32(); return ICON_FA_GAMEPAD;
            default:                     return ICON_FA_FILE;
        }
    }

    const char* ContentBrowserPanel::GetTypeIcon(AssetType type, ImU32* color)
    {
        ImU32 c;
        const char* icon = IconFor(type, c);
        if (color)
            *color = c;
        return icon;
    }

    bool ContentBrowserPanel::DrawAssetsInUse(const std::vector<AssetInUse>& uses, fs::path* clicked)
    {
        bool result = false;
        constexpr size_t maxReferrers = 8;
        size_t rows = 0;
        for (const AssetInUse& use : uses)
            rows += 1 + std::min(use.Referrers.size(), maxReferrers + 1);
        float height = std::min(250.0f, ImGui::GetTextLineHeightWithSpacing() * (float)rows + ImGui::GetStyle().WindowPadding.y * 2.0f + 2.0f);
        ImGui::BeginChild("##assetsInUse", ImVec2(480.0f, height), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < uses.size(); i++)
        {
            const AssetInUse& use = uses[i];
            ImGui::PushID((int)i);
            ImU32 color;
            const char* icon = IconFor(use.Type, color);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", icon);
            ImGui::SameLine();
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            ImGui::TextUnformatted(use.Path.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("- %zu ta faylda, %u ta havola", use.Referrers.size(), use.GetReferenceCount());
            ImGui::Indent();
            for (size_t r = 0; r < use.Referrers.size() && r < maxReferrers; r++)
            {
                const AssetReferrer& referrer = use.Referrers[r];
                std::string tags;
                if (referrer.Has(AssetReferenceKind::UUID) && !referrer.Has(AssetReferenceKind::Path))
                    tags += "  [UUID]";
                if (referrer.Has(AssetReferenceKind::ModelFile))
                    tags += "  [model fayli]";
                if (referrer.Has(AssetReferenceKind::Code))
                    tags += "  [C++ kod]";
                ImU32 referrerColor;
                const char* referrerIcon = IconFor(AssetFiles::GetType(referrer.File), referrerColor);
                std::string label = std::format("{}  {}{}##ref{}", referrerIcon, referrer.Name, tags, r);
                if (referrer.Unsaved)
                {
                    // The open scene, never saved: no file to show.
                    ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_Disabled);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        UI::Tooltip("Tahrirlanayotgan sahna - hali faylga saqlanmagan");
                    continue;
                }
                if (ImGui::Selectable(label.c_str()))
                {
                    if (clicked)
                        *clicked = referrer.File;
                    result = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Kontent brauzerda ko'rsatish (o'chirish bekor qilinadi)");
            }
            if (use.Referrers.size() > maxReferrers)
                ImGui::TextDisabled("... va yana %zu ta fayl", use.Referrers.size() - maxReferrers);
            ImGui::Unindent();
            ImGui::PopID();
        }
        ImGui::EndChild();
        return result;
    }

    // Start of the UTF-8 character before `at` (names are not always ASCII).
    static size_t PreviousCharacter(const std::string& text, size_t at)
    {
        if (at == 0)
            return 0;
        at--;
        while (at > 0 && ((unsigned char)text[at] & 0xC0) == 0x80)
            at--;
        return at;
    }

    // A name that fits `width` on one line: the middle gives way to an ellipsis so the start and
    // the end with the extension stay readable ("bricks_n...l.png"); when that leaves too little
    // of the start, only the start is kept. The full name is in the tooltip.
    static std::string FitName(const std::string& name, float width)
    {
        auto fits = [&](const std::string& text) { return ImGui::CalcTextSize(text.c_str()).x <= width; };
        if (fits(name))
            return name;
        // U+2026 when the font has it (the editor font does), otherwise three dots.
        const std::string ellipsis = ImGui::GetFont()->FindGlyphNoFallback((ImWchar)0x2026) ? "\xE2\x80\xA6" : "...";
        size_t dot = name.rfind('.');
        size_t tail = dot != std::string::npos && dot > 0 && name.size() - dot <= 9 ? dot : name.size(); // ".zprefab"...
        tail = PreviousCharacter(name, tail); // one letter before the extension, too
        constexpr size_t minHead = 3;
        for (size_t head = PreviousCharacter(name, tail); head >= minHead; head = PreviousCharacter(name, head))
            if (std::string shown = name.substr(0, head) + ellipsis + name.substr(tail); fits(shown))
                return shown;
        for (size_t head = PreviousCharacter(name, name.size()); head > 0; head = PreviousCharacter(name, head))
            if (std::string shown = name.substr(0, head) + ellipsis; fits(shown))
                return shown;
        return ellipsis;
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
        // Thumbnails are kept in the project's cache folder.
        m_Thumbnails.SetCacheDirectory(!root.empty() && Project::GetActive() ? Project::GetCacheDirectory() / "Thumbnails" : fs::path());
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
    }

    Ref<Texture2D> ContentBrowserPanel::GetThumbnail(const AssetEntry& entry)
    {
        if (entry.IsDirectory)
            return nullptr;
        return m_Thumbnails.Get(entry.Path, entry.Size, entry.Modified);
    }

    void ContentBrowserPanel::DrawThumbnail(const Ref<Texture2D>& texture, ImVec2 min, ImVec2 max, float rounding)
    {
        float w = (float)std::max(1u, texture->GetWidth()), h = (float)std::max(1u, texture->GetHeight());
        float boxW = max.x - min.x, boxH = max.y - min.y;
        float scale = std::min(boxW / w, boxH / h);
        ImVec2 size(w * scale, h * scale);
        ImVec2 at(min.x + (boxW - size.x) * 0.5f, min.y + (boxH - size.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddImageRounded((ImTextureID)(uintptr_t)texture->GetRendererID(), at, ImVec2(at.x + size.x, at.y + size.y),
                                                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
    }

    // ------------------------------------------------------------------ listing
    bool ContentBrowserPanel::IsRecursiveListing() const
    {
        return m_SearchActive || m_Settings->Filter.IsActive();
    }

    void ContentBrowserPanel::RefreshEntries()
    {
        m_SearchActive = !AssetSearch(m_Search).IsEmpty();
        // A search looks through the whole project (paths matched from the project folder) unless
        // it is set to the open folder.
        fs::path directory = m_SearchActive && m_Settings->SearchEverywhere ? m_Root : m_CurrentDirectory;
        std::string key = std::format("{}|{}|{}|{}|{}", directory.string(), m_Search, m_Settings->Filter.Mask,
                                      (int)m_Settings->SortBy, m_Settings->SortAscending);
        double now = ImGui::GetTime();
        if (!m_ListDirty && key == m_ListKey && now - m_ListTime < 1.0)
            return;
        m_ListDirty = false;
        m_ListKey = key;
        m_ListTime = now;

        AssetListOptions options;
        options.Search = m_Search;
        options.SearchRoot = m_Root;
        options.SkipTopFolder = true; // "a" must not find everything in "Assets/"
        options.Filter = m_Settings->Filter;
        options.SortBy = m_Settings->SortBy;
        options.Ascending = m_Settings->SortAscending;
        options.Excluded = { m_Root / "Build", m_Root / "Binaries" };
        m_ListDirectory = directory;
        m_Entries = AssetFiles::List(directory, options);
        m_EntryErrorsDirty = true;

        // Selected files that are gone (deleted outside, moved) are forgotten.
        std::erase_if(m_Selection, [&](const fs::path& p) {
            return std::none_of(m_Entries.begin(), m_Entries.end(), [&](const AssetEntry& e) { return e.Path == p; });
        });
    }

    void ContentBrowserPanel::RefreshEntryErrors()
    {
        uint64_t revision = AssetImportLog::GetRevision();
        if (!m_EntryErrorsDirty && revision == m_EntryErrorsRevision)
            return;
        m_EntryErrorsDirty = false;
        m_EntryErrorsRevision = revision;
        m_EntryErrors.clear();
        std::vector<AssetImportError> errors = AssetImportLog::GetErrors();
        if (errors.empty())
            return;
        std::unordered_map<std::string, const AssetImportError*> byFile;
        for (const AssetImportError& e : errors)
            byFile[e.File.string()] = &e;
        // Keys like the log's: the listed folder resolved once, the entries below it.
        const fs::path& listed = m_ListDirectory.empty() ? m_CurrentDirectory : m_ListDirectory;
        fs::path directory = AssetImportLog::MakeKey(listed);
        for (const AssetEntry& entry : m_Entries)
        {
            std::string key = (directory / entry.Path.lexically_relative(listed)).lexically_normal().string();
            if (!entry.IsDirectory)
            {
                if (auto it = byFile.find(key); it != byFile.end())
                    m_EntryErrors[entry.Path.string()] = it->second->Reason;
                continue;
            }
            size_t inside = 0;
            for (const AssetImportError& e : errors)
                if (e.File.string().starts_with(key + "/"))
                    inside++;
            if (inside > 0)
                m_EntryErrors[entry.Path.string()] = std::format("ichida {} ta faylda import xatosi bor", inside);
        }
    }

    const std::string* ContentBrowserPanel::FindEntryError(const fs::path& path) const
    {
        if (m_EntryErrors.empty())
            return nullptr;
        auto it = m_EntryErrors.find(path.string());
        return it != m_EntryErrors.end() ? &it->second : nullptr;
    }

    fs::path ContentBrowserPanel::FromLogPath(const fs::path& file) const
    {
        fs::path rel = file.lexically_relative(AssetImportLog::MakeKey(m_Root));
        if (rel.empty() || *rel.begin() == "..")
            return file;
        return m_Root / rel;
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

        for (const auto& m : result.Moved)
            m_Thumbnails.Invalidate(m.From);
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
        if (ImGui::MenuItem(Tr(ICON_FA_XMARK "  Filtrni tozalash"), nullptr, false, filter.IsActive()))
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
        float searchWidth = 220.0f, sliderWidth = grid ? 90.0f : 0.0f;
        float right = button * 4 + searchWidth + sliderWidth + style.ItemSpacing.x * (grid ? 5 : 4) + 2.0f + 8.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowContentRegionMax().x - right));

        auto& filter = m_Settings->Filter;
        std::string filterTip = filter.IsActive() ? std::format("Fayl turi bo'yicha filtr ({} ta tur)", filter.Count())
                                                  : std::string("Fayl turi bo'yicha filtr");
        if (UI::ToolbarButton(ICON_FA_FILTER, filterTip.c_str(), filter.IsActive(), ImVec2(button, button)))
            ImGui::OpenPopup("##cbfilter");
        DrawFilterPopup();
        ImGui::SameLine();
        bool everywhere = m_Settings->SearchEverywhere;
        if (UI::ToolbarButton(everywhere ? ICON_FA_FOLDER_TREE : ICON_FA_FOLDER_OPEN,
                              everywhere ? "Qidiruv: butun loyihada (bosing - faqat ochiq papkada)"
                                         : "Qidiruv: ochiq papka va uning ichidagilarda (bosing - butun loyihada)",
                              everywhere, ImVec2(button, button)))
        {
            m_Settings->SearchEverywhere = !everywhere;
            m_ListDirty = true;
            MarkSettingsModified();
        }
        ImGui::SameLine(0, 2);
        ImGui::SetNextItemWidth(searchWidth);
        // Ctrl+F while the panel has focus.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            ImGui::SetKeyboardFocusHere();
        if (ImGui::InputTextWithHint("##cbsearch", Tr(ICON_FA_MAGNIFYING_GLASS "  Qidirish (yo'l, *.png, t:rasm)"), &m_Search))
            m_ListDirty = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive())
            UI::Tooltip("Butun yo'l bo'yicha qidiradi (papka nomlari ham hisobga olinadi). Hamma so'zlar mos kelishi kerak:\n"
                        "  coin          nomida yoki papkasida 'coin'\n"
                        "  tex/coin      papkalar va nom shu tartibda (Textures/Items/coin.png)\n"
                        "  Textures/     'Textures' papkasidagi hamma narsa\n"
                        "  *.png         '*' - istalgan matn\n"
                        "  t:rasm        fayl turi (t:model, t:audio, t:sahna, t:prefab...)\n"
                        "Ctrl+F - qidiruvga o'tish");
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
        if (ImGui::SmallButton(Tr("Tozalash")))
        {
            filter.Clear();
            MarkSettingsModified();
        }
        ImGui::PopStyleColor();
    }

    void ContentBrowserPanel::DrawSearchBar()
    {
        if (!m_SearchActive)
            return;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UI::AccentColor(), ICON_FA_MAGNIFYING_GLASS);
        ImGui::SameLine();
        bool everywhere = m_ListDirectory == m_Root;
        std::string where = everywhere ? std::string("butun loyihada")
                                       : std::format("'{}' papkasida", m_CurrentDirectory.filename().string());
        ImGui::TextDisabled("'%s' - %zu ta natija %s", m_Search.c_str(), m_Entries.size(), where.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (!everywhere && ImGui::SmallButton(Tr("Butun loyihada qidirish")))
        {
            m_Settings->SearchEverywhere = true;
            m_ListDirty = true;
            MarkSettingsModified();
        }
        if (!everywhere)
            ImGui::SameLine();
        if (ImGui::SmallButton(Tr(ICON_FA_XMARK "  Tozalash##search")))
        {
            m_Search.clear();
            m_ListDirty = true;
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
        else if (entry.Type == AssetType::Animation && OnOpenAnimation)
            OnOpenAnimation(entry.Path);
        else if (ExternalEditor::IsSourceFile(entry.Path) && OnOpenSourceFile)
            OnOpenSourceFile(entry.Path);
        else
            Platform::OpenInFileBrowser(entry.Path);
    }

    void ContentBrowserPanel::HandleItemInteraction(const AssetEntry& entry)
    {
        const fs::path& path = entry.Path;
        bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        bool clicked = false;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (io.KeyCtrl || io.KeyShift)
                Click(path, io.KeyCtrl, io.KeyShift);
            else if (!IsSelected(path))
                SelectOnly(path);
            else
                m_ClickedSelected = path; // maybe the start of dragging the whole selection
            clicked = true;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (!IsSelected(path))
                SelectOnly(path);
            clicked = true;
        }
        if (clicked && OnAssetSelected) // Ctrl+click on a selected item: the last one left
            OnAssetSelected(IsSelected(path) ? path : m_Selection.empty() ? fs::path() : m_Selection.back());

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
                tip += "\n" ICON_FA_FOLDER "  " + path.parent_path().lexically_relative(m_Root).generic_string();
            if (AssetRegistry* registry = entry.IsDirectory ? nullptr : Project::GetAssetRegistry())
                if (const AssetMetadata* meta = registry->Find(path))
                    tip += "\nUUID: " + meta->Handle.ToString() + (meta->HasMetaFile ? "" : " (.zmeta yo'q)");
            if (const std::string* error = FindEntryError(path))
                tip += std::string("\n" ICON_FA_TRIANGLE_EXCLAMATION "  ") + (entry.IsDirectory ? "" : "Import xatosi: ") + *error;
            UI::Tooltip(tip.c_str());
        }
    }

    void ContentBrowserPanel::DrawItemMenu(const AssetEntry& entry)
    {
        const fs::path& path = entry.Path;
        size_t count = std::max<size_t>(1, m_Selection.size());
        if (entry.IsDirectory && ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Ochish")))
            Open(entry);
        if (entry.Type == AssetType::Scene && ImGui::MenuItem(Tr(ICON_FA_EARTH_ASIA "  Sahnani ochish")) && OnOpenScene)
            OnOpenScene(path);
        // The project's settings that point to files: the build list, the start scene, the game's icon.
        if (Ref<Project> project = Project::GetActive(); project && !entry.IsDirectory)
        {
            ProjectConfig& config = project->GetConfig();
            const std::string relative = Project::GetAssetRelativePath(path);
            if (entry.Type == AssetType::Scene)
            {
                const bool listed = std::any_of(config.BuildScenes.begin(), config.BuildScenes.end(),
                                                [&](const BuildSceneEntry& e) { return e.Path.generic_string() == relative; });
                if (ImGui::MenuItem(Tr(ICON_FA_LIST_OL "  Build ro'yxatiga qo'shish"), nullptr, listed, !listed) && Project::AddBuildScene(relative))
                {
                    ZM_INFO("Build ro'yxatiga qo'shildi: {} (indeks {})", relative, Project::GetBuildIndex(relative));
                    if (OnProjectSettingsModified)
                        OnProjectSettingsModified();
                }
                const bool isStart = config.StartScene.generic_string() == relative;
                if (ImGui::MenuItem(Tr(ICON_FA_HOUSE "  Boshlang'ich sahna qilish"), nullptr, isStart, !isStart))
                {
                    config.StartScene = relative;
                    config.StartSceneHandle = {};
                    ZM_INFO("Boshlang'ich sahna: {}", relative);
                    if (OnProjectSettingsModified)
                        OnProjectSettingsModified();
                }
            }
            else if (path.extension() == ".png")
            {
                const bool isIcon = config.Player.Icon.generic_string() == relative;
                if (ImGui::MenuItem(Tr(ICON_FA_IMAGE "  O'yin ikonkasi qilish"), nullptr, isIcon, !isIcon))
                {
                    config.Player.Icon = relative;
                    ZM_INFO("O'yin ikonkasi: {}", relative);
                    if (OnProjectSettingsModified)
                        OnProjectSettingsModified();
                }
            }
        }
        if (!entry.IsDirectory && ExternalEditor::IsSourceFile(path) && OnOpenSourceFile &&
            ImGui::MenuItem(Tr(ICON_FA_PEN_TO_SQUARE "  Tashqi muharrirda ochish")))
            OnOpenSourceFile(path);
        if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Tizimda ochish")))
            Platform::OpenInFileBrowser(path);
        if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Yo'lni nusxalash")))
            ImGui::SetClipboardText(Project::GetAssetRelativePath(path).c_str());
        if (!entry.IsDirectory && AssetHotReload::CanReload(path))
        {
            bool loaded = AssetHotReload::IsLoaded(path);
            if (ImGui::MenuItem(Tr(ICON_FA_ARROWS_ROTATE "  Qayta yuklash"), nullptr, false, loaded))
            {
                bool failed = false;
                size_t n = AssetHotReload::ReloadAsset(path, &failed);
                m_Thumbnails.Invalidate(path);
                if (failed)
                    ShowNotice(std::format("'{}' qayta yuklanmadi - konsolni tekshiring", entry.Name), true);
                else
                {
                    ZM_INFO("Qayta yuklandi: {} ({} ta nusxa)", Project::GetAssetRelativePath(path), n);
                    ShowNotice(std::format("'{}' qayta yuklandi", entry.Name));
                }
            }
            if (!loaded && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Hozir ishlatilmayapti - fayl o'zgarganda avtomatik yangilanadi");
        }
        ImGui::Separator();
        if (ImGui::MenuItem(Tr(ICON_FA_FEATHER "  Nomini o'zgartirish"), "F2", false, count == 1 && !IsProtected(path)))
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
        bool recursive = IsRecursiveListing();
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
            Ref<Texture2D> thumb = GetThumbnail(entry);
            if (thumb)
                DrawThumbnail(thumb, iconMin, iconMax, 4.0f);
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

            if (FindEntryError(path))
            {
                // Import error badge (the reason is in the tooltip and the inspector).
                float r = std::clamp(size * 0.09f, 6.0f, 10.0f);
                ImVec2 c(iconMax.x - r * 0.5f, iconMin.y + r * 0.5f);
                dl->AddCircleFilled(c, r, IM_COL32(215, 72, 60, 255));
                dl->AddCircle(c, r, IM_COL32(25, 27, 32, 255), 0, 1.5f);
                ImVec2 mark = ImGui::CalcTextSize("!");
                dl->AddText(ImVec2(c.x - mark.x * 0.5f, c.y - mark.y * 0.5f), IM_COL32_WHITE, "!");
            }

            if (recursive)
            {
                // A search / filter result: the name on one line (shortened in the middle when it is
                // too long; tiles out of view keep it whole), the folder it is in below it.
                float line = ImGui::GetTextLineHeight();
                ImVec4 nameClip(cursor.x + 2, cursor.y + size, tileMax.x - 2, cursor.y + size + line);
                std::string name = ImGui::IsRectVisible(cursor, tileMax) ? FitName(entry.Name, size - 4) : entry.Name;
                float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
                float nx = cursor.x + std::max(2.0f, (size - std::min(nameWidth, size - 4)) * 0.5f);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(nx, cursor.y + size), ImGui::GetColorU32(ImGuiCol_Text),
                            name.c_str(), nullptr, 0.0f, &nameClip);
                std::string where = path.parent_path().lexically_relative(m_ListDirectory).generic_string();
                if (where == ".")
                    where.clear();
                while (ImGui::CalcTextSize(where.c_str()).x > size - 4 && where.find('/') != std::string::npos)
                {
                    size_t slash = where.find('/', where.starts_with(".../") ? 4 : 0);
                    if (slash == std::string::npos)
                        break;
                    where = ".../" + where.substr(slash + 1); // the end of a long path
                }
                ImVec4 whereClip(cursor.x + 2, cursor.y + size + line, tileMax.x - 2, tileMax.y);
                float wx = cursor.x + std::max(2.0f, (size - std::min(ImGui::CalcTextSize(where.c_str()).x, size - 4)) * 0.5f);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(wx, cursor.y + size + line),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), where.c_str(), nullptr, 0.0f, &whereClip);
            }
            else
            {
                // label (2 lines max, clipped)
                ImVec4 clip(cursor.x + 2, cursor.y + size, tileMax.x - 2, tileMax.y);
                ImVec2 labelSize = ImGui::CalcTextSize(entry.Name.c_str(), nullptr, false, size - 4);
                float lx = cursor.x + std::max(2.0f, (size - std::min(labelSize.x, size - 4)) * 0.5f);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(lx, cursor.y + size), ImGui::GetColorU32(ImGuiCol_Text),
                            entry.Name.c_str(), nullptr, size - 4, &clip);
            }

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
                Ref<Texture2D> thumb = GetThumbnail(entry);
                if (thumb)
                    DrawThumbnail(thumb, ImVec2(rowMin.x + 2, rowMin.y + 2), ImVec2(rowMin.x + iconWidth - 2, rowMin.y + rowHeight - 2), 3.0f);
                else
                {
                    ImU32 color;
                    const char* icon = IconFor(entry.Type, color);
                    ImVec2 iconSize = ImGui::CalcTextSize(icon);
                    dl->AddText(ImVec2(rowMin.x + (iconWidth - iconSize.x) * 0.5f, rowMin.y + (rowHeight - iconSize.y) * 0.5f), color, icon);
                }
                float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                dl->AddText(ImVec2(rowMin.x + iconWidth + 6.0f, textY), ImGui::GetColorU32(ImGuiCol_Text), entry.Name.c_str());
                if (FindEntryError(entry.Path))
                {
                    float nameWidth = ImGui::CalcTextSize(entry.Name.c_str()).x;
                    dl->AddText(ImVec2(rowMin.x + iconWidth + 12.0f + nameWidth, textY), IM_COL32(230, 95, 80, 255), ICON_FA_TRIANGLE_EXCLAMATION);
                }

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
                    fs::path where = entry.Path.parent_path().lexically_relative(m_ListDirectory);
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
        ImGui::TextDisabled(EditorText::Choose("%zu ta element", "%zu items"), m_Entries.size());
        if (!m_Selection.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled(" |  %zu ta tanlangan%s", m_Selection.size(),
                                selectedBytes > 0 ? (" (" + AssetFiles::FormatSize(selectedBytes) + ")").c_str() : "");
        }
        DrawImportErrors();
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

    void ContentBrowserPanel::DrawImportErrors()
    {
        size_t count = AssetImportLog::GetCount();
        if (count > 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(" | ");
            ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.4f, 1.0f));
            std::string label = std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {} ta import xatosi##importErrors", count);
            if (ImGui::SmallButton(label.c_str()))
                ImGui::OpenPopup("##importErrorList");
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Yuklanmagan assetlar (sabablari konsolda ham bor)");
        }
        if (!ImGui::BeginPopup("##importErrorList"))
            return;
        ImGui::TextDisabled(ICON_FA_TRIANGLE_EXCLAMATION "  Yuklanmagan assetlar - faylni ko'rsatish uchun bosing");
        ImGui::Separator();
        std::vector<AssetImportError> errors = AssetImportLog::GetErrors();
        if (errors.empty())
            ImGui::TextDisabled("Xato yo'q");
        for (size_t i = 0; i < errors.size() && i < 50; i++)
        {
            const AssetImportError& e = errors[i];
            ImGui::PushID((int)i);
            ImU32 color;
            const char* icon = IconFor(e.Type, color);
            std::string label = std::format("{}  {}", icon, e.Name);
            if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(520, 0)))
            {
                fs::path file = FromLogPath(e.File);
                std::error_code ec;
                if (fs::exists(file, ec))
                {
                    Reveal({ file });
                    if (OnAssetSelected)
                        OnAssetSelected(file);
                }
                else if (fs::is_directory(file.parent_path(), ec) && AssetFiles::IsInside(file.parent_path(), m_Root))
                    NavigateTo(file.parent_path());
            }
            ImGui::Indent();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 500.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.5f, 1.0f), "%s%s", e.Reload ? "(qayta yuklashda) " : "", e.Reason.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Unindent();
            ImGui::PopID();
        }
        if (errors.size() > 50)
            ImGui::TextDisabled("... va yana %zu ta", errors.size() - 50);
        ImGui::EndPopup();
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
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !m_Selection.empty())
        {
            m_Selection.clear();
            if (OnAssetSelected)
                OnAssetSelected({});
        }
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

    fs::path ContentBrowserPanel::CreateAnimationClip()
    {
        fs::path file = AssetFiles::MakeUniquePath(m_CurrentDirectory, std::string("NewAnimation") + PropertyClip::Extension);
        if (!PropertyClipSerializer::Serialize(PropertyClip(), file))
        {
            ShowNotice(std::format("Klip yaratilmadi: {}", file.filename().string()), true);
            ZM_ERROR("Animatsiya klipi yozilmadi: {}", file.string());
            return {};
        }
        if (AssetRegistry* registry = Project::GetAssetRegistry())
            registry->OnFileAdded(file);
        ZM_INFO("Yangi animatsiya klipi yaratildi: {}", Project::GetAssetRelativePath(file));
        m_ListDirty = true;
        if (OnFilesChanged)
            OnFilesChanged();
        m_Selection.clear();
        Reveal({ file });
        if (OnAssetSelected)
            OnAssetSelected(file);
        ShowNotice(std::format("Yangi klip: {}  (ikki marta bosing - Animatsiya oynasi; Animation Player'ga torting)", file.filename().string()));
        return file;
    }

    fs::path ContentBrowserPanel::CreateSpriteClip()
    {
        // Made from a selected texture: its sprite sheet's frames (all of them, in order) and the texture.
        SpriteClip clip;
        for (const fs::path& selected : m_Selection)
            if (AssetFiles::GetType(selected) == AssetType::Texture)
            {
                clip.Texture = Project::GetAssetRelativePath(selected);
                if (const SpriteSheet* sheet = SpriteSheet::Get(clip.Texture))
                    clip.Frames = sheet->FindSequence(sheet->GetSequencePrefixes().empty() ? "" : sheet->GetSequencePrefixes().front());
                clip.FrameCount = clip.Frames.empty() ? clip.FrameCount : (int)clip.Frames.size();
                break;
            }
        std::string base = clip.Texture.empty() ? std::string("NewSpriteAnimation") : fs::path(clip.Texture).stem().string();
        fs::path file = AssetFiles::MakeUniquePath(m_CurrentDirectory, base + SpriteClip::Extension);
        if (!SpriteClipSerializer::Serialize(clip, file))
        {
            ShowNotice(std::format("Klip yaratilmadi: {}", file.filename().string()), true);
            ZM_ERROR("Sprite animatsiya klipi yozilmadi: {}", file.string());
            return {};
        }
        if (AssetRegistry* registry = Project::GetAssetRegistry())
            registry->OnFileAdded(file);
        ZM_INFO("Yangi sprite animatsiya klipi yaratildi: {}", Project::GetAssetRelativePath(file));
        m_ListDirty = true;
        if (OnFilesChanged)
            OnFilesChanged();
        m_Selection.clear();
        Reveal({ file });
        if (OnAssetSelected)
            OnAssetSelected(file);
        ShowNotice(std::format("Yangi sprite klip: {}  (inspektorda kadrlar va FPS; Sprite Animation'ga torting)", file.filename().string()));
        return file;
    }

    fs::path ContentBrowserPanel::CreateMaterial(const MaterialPreset* preset)
    {
        if (!Project::GetActive())
            return {};
        fs::path assets = Project::GetAssetDirectory();
        // Asset paths only reach files inside the asset folder.
        fs::path dir = !m_CurrentDirectory.empty() && AssetFiles::IsInside(m_CurrentDirectory, assets) ? m_CurrentDirectory : assets / "Materials";
        fs::path file;
        std::vector<fs::path> written;
        if (preset)
        {
            MaterialPresets::CreateResult result;
            std::string error;
            if (!MaterialPresets::Create(*preset, dir, false, result, &error))
            {
                ShowNotice(std::format("Material yaratilmadi: {}", preset->Name), true);
                ZM_ERROR("Standart material '{}' yaratilmadi: {}", preset->Name, error);
                return {};
            }
            file = result.Material;
            written = result.Written;
        }
        else
        {
            file = AssetFiles::MakeUniquePath(dir, "NewMaterial.zmat");
            if (!MaterialSerializer::Serialize(MaterialData{}, file)) // makes the folder
            {
                ShowNotice(std::format("Material yaratilmadi: {}", file.filename().string()), true);
                ZM_ERROR("Material fayli yozilmadi: {}", file.string());
                return {};
            }
            if (AssetRegistry* registry = Project::GetAssetRegistry())
                registry->OnFileAdded(file); // its UUID at once (scenes write it next to the path)
            written.push_back(file);
        }
        if (preset)
            ZM_INFO("Standart material yaratildi ({}): {}", preset->Name, Project::GetAssetRelativePath(file));
        else
            ZM_INFO("Yangi material yaratildi: {}", Project::GetAssetRelativePath(file));
        for (const fs::path& p : written)
            m_Thumbnails.Invalidate(p);
        m_ListDirty = true;
        if (OnFilesChanged)
            OnFilesChanged();
        m_Selection.clear();
        Reveal({ file });
        if (OnAssetSelected)
            OnAssetSelected(file);
        ShowNotice(std::format("Yangi material: {}  (F2 - nomini o'zgartirish)", file.filename().string()));
        return file;
    }

    bool ContentBrowserPanel::DrawNewMaterialMenu()
    {
        bool chosen = false;
        if (ImGui::MenuItem(Tr(ICON_FA_FILE "  Bo'sh material")))
        {
            CreateMaterial();
            chosen = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Standart qiymatli material asset (.zmat) shu papkada yaratiladi va inspektorda ochiladi");
        ImGui::Separator();
        ImGui::TextDisabled("Standart materiallar");
        for (const MaterialPreset& preset : MaterialPresets::GetAll())
        {
            std::string where = std::format("{}.zmat shu papkada yaratiladi{}", preset.FileName,
                                            preset.Texture != MaterialPreset::Pattern::None ? " (teksturasi Textures/ ichida)" : "");
            if (UI::MaterialPresetMenuItem(preset, where.c_str()))
            {
                CreateMaterial(&preset);
                chosen = true;
            }
        }
        return chosen;
    }

    // Built-in physics materials: name, file name, values.
    struct PhysicsMaterialPreset
    {
        const char* Name;
        const char* FileName;
        const char* Description;
        PhysicsMaterialData Data;
    };
    static const std::vector<PhysicsMaterialPreset>& GetPhysicsMaterialPresets()
    {
        static const std::vector<PhysicsMaterialPreset> s_Presets = [] {
            std::vector<PhysicsMaterialPreset> list;
            PhysicsMaterialData d;
            list.push_back({ "Standart", "NewPhysicsMaterial", "Kolliderlarning standart qiymatlari", d });
            d = {};
            d.Friction = 0.02f;
            d.FrictionCombine = PhysicsCombine::Minimum;
            d.Density = 0.9f;
            list.push_back({ "Muz", "Ice", "Deyarli ishqalanishsiz: ustida hamma narsa sirpanadi (eng kichigi)", d });
            d = {};
            d.Friction = 0.8f;
            d.Restitution = 0.85f;
            d.RestitutionThreshold = 0.2f;
            d.RestitutionCombine = PhysicsCombine::Maximum;
            list.push_back({ "Rezina to'p", "Bouncy", "Kuchli sakraydi (eng kattasi)", d });
            d = {};
            d.Friction = 1.0f;
            d.Density = 1.2f;
            d.FrictionCombine = PhysicsCombine::Maximum;
            list.push_back({ "Yopishqoq", "Grip", "Ishqalanishi katta: qiyalikda ham sirpanmaydi", d });
            d = {};
            d.Friction = 0.4f;
            d.Restitution = 0.1f;
            d.Density = 7.8f;
            list.push_back({ "Metall", "Metal", "Og'ir (zichlik 7.8), biroz sirpanchiq", d });
            d = {};
            d.Friction = 0.6f;
            d.Restitution = 0.15f;
            d.Density = 0.6f;
            list.push_back({ "Yog'och", "Wood", "Yengil, ishqalanishi o'rtacha", d });
            return list;
        }();
        return s_Presets;
    }

    fs::path ContentBrowserPanel::CreatePhysicsMaterial(int preset)
    {
        if (!Project::GetActive())
            return {};
        const auto& presets = GetPhysicsMaterialPresets();
        const PhysicsMaterialPreset& p = presets[(size_t)std::clamp(preset, 0, (int)presets.size() - 1)];
        fs::path assets = Project::GetAssetDirectory();
        fs::path dir = !m_CurrentDirectory.empty() && AssetFiles::IsInside(m_CurrentDirectory, assets) ? m_CurrentDirectory : assets / "Physics";
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path file = AssetFiles::MakeUniquePath(dir, std::string(p.FileName) + PhysicsMaterial::Extension);
        if (!PhysicsMaterial::Save(file, p.Data))
        {
            ShowNotice(std::format("Fizik material yaratilmadi: {}", file.filename().string()), true);
            ZM_ERROR("Fizik material fayli yozilmadi: {}", file.string());
            return {};
        }
        if (AssetRegistry* registry = Project::GetAssetRegistry())
            registry->OnFileAdded(file); // its UUID at once
        ZM_INFO("Yangi fizik material yaratildi: {}", Project::GetAssetRelativePath(file));
        m_ListDirty = true;
        if (OnFilesChanged)
            OnFilesChanged();
        m_Selection.clear();
        Reveal({ file });
        if (OnAssetSelected)
            OnAssetSelected(file);
        ShowNotice(std::format("Yangi fizik material: {}  (F2 - nomini o'zgartirish)", file.filename().string()));
        return file;
    }

    bool ContentBrowserPanel::DrawNewPhysicsMaterialMenu()
    {
        bool chosen = false;
        const auto& presets = GetPhysicsMaterialPresets();
        for (size_t i = 0; i < presets.size(); i++)
        {
            const PhysicsMaterialPreset& p = presets[i];
            std::string label = std::format("{}  {}", i == 0 ? ICON_FA_FILE : ICON_FA_GEM, p.Name);
            if (ImGui::MenuItem(label.c_str()))
            {
                CreatePhysicsMaterial((int)i);
                chosen = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip(std::format("{}\nIshqalanish {:.2f}, sakrash {:.2f}, zichlik {:.2f}\n{}{} shu papkada yaratiladi", p.Description,
                                        p.Data.Friction, p.Data.Restitution, p.Data.Density, p.FileName, PhysicsMaterial::Extension).c_str());
            if (i == 0)
                ImGui::Separator();
        }
        return chosen;
    }

    // ------------------------------------------------------------------ menus / popups
    void ContentBrowserPanel::DrawContextMenu()
    {
        if (ImGui::BeginPopupContextWindow("CBContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_FOLDER "  Yangi papka"))) { m_NewKind = 0; m_NewName = "NewFolder"; m_OpenNewPopup = true; }
            if (ImGui::MenuItem(Tr(ICON_FA_EARTH_ASIA "  Yangi sahna"))) { m_NewKind = 1; m_NewName = "NewScene"; m_OpenNewPopup = true; }
            if (ImGui::MenuItem(Tr(ICON_FA_FILE_CODE "  Yangi C++ skript...")) && OnRequestNewScript) OnRequestNewScript();
            if (ImGui::BeginMenu(Tr(ICON_FA_PALETTE "  Yangi material"), Project::GetActive() != nullptr))
            {
                DrawNewMaterialMenu();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(Tr(ICON_FA_GEM "  Yangi fizik material (2D)"), Project::GetActive() != nullptr))
            {
                DrawNewPhysicsMaterialMenu();
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(Tr(ICON_FA_TIMELINE "  Yangi animatsiya klipi"), nullptr, false, Project::GetActive() != nullptr))
                CreateAnimationClip();
            if (ImGui::MenuItem(Tr(ICON_FA_FILM "  Yangi sprite animatsiya klipi"), nullptr, false, Project::GetActive() != nullptr))
                CreateSpriteClip();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tanlangan rasm bo'lsa - uning sprite sheet kadrlaridan");
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_CHECK_DOUBLE "  Hammasini tanlash"), "Ctrl+A", false, !m_Entries.empty()))
            {
                m_Selection.clear();
                for (const AssetEntry& e : m_Entries)
                    m_Selection.push_back(e.Path);
            }
            if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Fayl menejerida ochish")))
                Platform::OpenInFileBrowser(m_CurrentDirectory);
            if (ImGui::MenuItem(Tr(ICON_FA_ARROWS_ROTATE "  Yangilash")))
                Refresh();
            if (ImGui::MenuItem(Tr(ICON_FA_BROOM "  Ishlatilmagan assetlarni topish..."), nullptr, false, OnFindUnusedAssets != nullptr))
                OnFindUnusedAssets();
            ImGui::Separator();
            ImGui::TextDisabled(ICON_FA_FILE_IMPORT "  Import: fayllarni oynaga tashlang");
            ImGui::EndPopup();
        }

        if (m_OpenNewPopup)
        {
            ImGui::OpenPopup("###NewItem");
            m_OpenNewPopup = false;
        }
        if (ImGui::BeginPopupModal(Tr(ICON_FA_FOLDER "  Yaratish###NewItem"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const char* titles[] = { "Yangi papka", "Yangi sahna" };
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            ImGui::TextUnformatted(titles[m_NewKind]);
            ImGui::PopFont();
            ImGui::SetNextItemWidth(300);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##name", &m_NewName, ImGuiInputTextFlags_EnterReturnsTrue);
            if ((ImGui::Button(Tr(ICON_FA_CHECK "  Yaratish"), ImVec2(140, 0)) || enter) && !m_NewName.empty())
            {
                std::error_code ec;
                if (m_NewKind == 0)
                    fs::create_directories(m_CurrentDirectory / m_NewName, ec);
                else if (m_NewKind == 1 && OnNewScene)
                    OnNewScene(m_CurrentDirectory / (m_NewName + ".zscene"));
                m_ListDirty = true;
                if (OnFilesChanged)
                    OnFilesChanged();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish"), ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_OpenRenamePopup) { ImGui::OpenPopup("###RenameItem"); m_OpenRenamePopup = false; }
        if (ImGui::BeginPopupModal(Tr(ICON_FA_FEATHER "  Nomini o'zgartirish###RenameItem"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(ICON_FA_FEATHER "  Yangi nom");
            ImGui::SetNextItemWidth(300);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##rename", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::TextDisabled("Sahna va prefablardagi havolalar ham yangilanadi.");
            if ((ImGui::Button(Tr(ICON_FA_CHECK "  OK"), ImVec2(120, 0)) || enter) && !m_RenameBuffer.empty())
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
            if (ImGui::Button(Tr("Bekor qilish"), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!m_PendingDelete.empty() && !ImGui::IsPopupOpen("###ConfirmDelete"))
        {
            ImGui::OpenPopup("###ConfirmDelete");
            // What still uses them (saved files and the open scene as it is in the editor).
            m_DeleteUses.clear();
            if (m_Usage && Project::GetActive())
                m_DeleteUses = m_Usage->Get(true).FindUsesOutside(m_PendingDelete);
        }
        if (ImGui::BeginPopupModal(Tr(ICON_FA_TRASH "  O'chirish###ConfirmDelete"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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
            bool inUse = !m_DeleteUses.empty();
            bool reveal = false;
            fs::path clicked;
            if (inUse)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                                   m_DeleteUses.size() == 1 ? "Bu asset ishlatilmoqda:"
                                                            : std::format("{} ta asset ishlatilmoqda:", m_DeleteUses.size()).c_str());
                reveal = DrawAssetsInUse(m_DeleteUses, &clicked);
                ImGui::PushTextWrapPos(480.0f);
                ImGui::TextDisabled("O'chirilsa, bu fayllardagi havolalar buziladi: sahnalarda rasm, model yoki ovoz yo'qoladi, "
                                    "kod esa faylni topa olmaydi.");
                ImGui::PopTextWrapPos();
            }
            ImGui::TextDisabled("Bu amalni qaytarib bo'lmaydi.");
            if (inUse)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.2f, 0.17f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.26f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.3f, 0.25f, 1.0f));
            }
            bool confirm = ImGui::Button(inUse ? ICON_FA_TRASH "  Baribir o'chirish" : ICON_FA_TRASH "  O'chirish", ImVec2(inUse ? 170.0f : 140.0f, 0));
            if (inUse)
                ImGui::PopStyleColor(3);
            if (confirm)
            {
                for (const fs::path& p : m_PendingDelete)
                {
                    if (IsProtected(p))
                    {
                        ZM_WARN("'{}' loyiha uchun kerak - o'chirilmadi", p.filename().string());
                        continue;
                    }
                    std::string error;
                    if (!AssetFiles::Remove(p, &error)) // a file goes with its .zmeta
                        ZM_ERROR("{}", error);
                    else if (AssetRegistry* registry = Project::GetAssetRegistry())
                        registry->OnFileRemoved(p);
                    m_Thumbnails.Invalidate(p);
                }
                if (inUse)
                    ZM_WARN("Ishlatilayotgan {} ta asset o'chirildi - havolalar buzildi", m_DeleteUses.size());
                m_PendingDelete.clear();
                m_DeleteUses.clear();
                m_Selection.clear();
                m_ListDirty = true;
                if (OnFilesChanged)
                    OnFilesChanged();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (inUse && ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere(); // Enter cancels when something would break
            if (ImGui::Button(Tr("Bekor qilish"), ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || reveal)
            {
                m_PendingDelete.clear();
                m_DeleteUses.clear();
                ImGui::CloseCurrentPopup();
                if (reveal)
                {
                    Reveal({ clicked });
                    if (OnAssetSelected)
                        OnAssetSelected(clicked);
                }
            }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------ panel
    void ContentBrowserPanel::OnImGuiRender(bool* open)
    {
        m_Visible = false;
        m_Thumbnails.Update(); // finished thumbnails to the GPU, a few a frame
        // The drag of the previous frames ended (dropped or cancelled).
        if (m_DragStarted && !ImGui::GetDragDropPayload() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_DragStarted = false;
            m_DragPaths.clear();
            m_DropCheckFolder.clear();
        }
        if (!*open)
            return;
        if (!ImGui::Begin(Tr(ICON_FA_FOLDER_OPEN "  Kontent###ContentBrowser"), open))
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
        RefreshEntryErrors();
        DrawToolbar();
        DrawSearchBar();
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
        {
            bool had = !m_Selection.empty();
            m_Selection.clear();
            if (had && OnAssetSelected)
                OnAssetSelected({});
        }

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
