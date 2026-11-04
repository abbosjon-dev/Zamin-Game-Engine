#include "ContentBrowserPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>

#include <algorithm>

namespace Zamin {

    namespace fs = std::filesystem;

    static std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    static const char* IconFor(const fs::path& path, bool isDir, ImU32& color)
    {
        color = IM_COL32(200, 205, 215, 255);
        if (isDir) { color = IM_COL32(240, 196, 92, 255); return ICON_FA_FOLDER; }
        std::string ext = Lower(path.extension().string());
        if (ext == ".zscene") { color = IM_COL32(46, 188, 148, 255); return ICON_FA_EARTH_ASIA; }
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp") { color = IM_COL32(100, 160, 240, 255); return ICON_FA_FILE_CODE; }
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") return ICON_FA_FILE_IMAGE;
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") { color = IM_COL32(220, 120, 200, 255); return ICON_FA_FILE_AUDIO; }
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb") { color = IM_COL32(240, 140, 80, 255); return ICON_FA_CUBES; }
        if (ext == ".ttf" || ext == ".otf") return ICON_FA_FONT;
        if (ext == ".hdr") { color = IM_COL32(255, 200, 90, 255); return ICON_FA_GLOBE; }
        if (ext == ".zprefab") { color = IM_COL32(120, 180, 255, 255); return ICON_FA_BOX; }
        if (ext == ".zproject") { color = IM_COL32(46, 188, 148, 255); return ICON_FA_GAMEPAD; }
        return ICON_FA_FILE;
    }

    ContentBrowserPanel::ContentBrowserPanel() = default;

    void ContentBrowserPanel::SetRoot(const fs::path& root)
    {
        m_Root = root;
        m_CurrentDirectory = root;
        m_Thumbnails.clear();
    }

    Ref<Texture2D> ContentBrowserPanel::GetThumbnail(const fs::path& path)
    {
        std::string ext = Lower(path.extension().string());
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tga")
            return nullptr;
        std::string key = path.string();
        auto it = m_Thumbnails.find(key);
        if (it != m_Thumbnails.end())
            return it->second;
        Ref<Texture2D> tex = TextureLibrary::Load(path);
        m_Thumbnails[key] = tex;
        return tex;
    }

    void ContentBrowserPanel::DrawBreadcrumbs()
    {
        bool canGoBack = m_CurrentDirectory != m_Root;
        ImGui::BeginDisabled(!canGoBack);
        if (UI::ToolbarButton(ICON_FA_ARROW_LEFT, "Orqaga"))
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (UI::ToolbarButton(ICON_FA_HOUSE, "Loyiha papkasi"))
            m_CurrentDirectory = m_Root;
        ImGui::SameLine();

        // path segments
        fs::path rel = fs::relative(m_CurrentDirectory, m_Root);
        fs::path accumulated = m_Root;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(m_Root.filename().string().c_str()))
            m_CurrentDirectory = m_Root;
        if (rel != ".")
        {
            for (const auto& part : rel)
            {
                accumulated /= part;
                ImGui::SameLine(0, 2);
                ImGui::TextDisabled("/");
                ImGui::SameLine(0, 2);
                if (ImGui::Button(part.string().c_str()))
                    m_CurrentDirectory = accumulated;
            }
        }
        ImGui::PopStyleColor();

        // Right side: search + size slider
        float right = 330.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowContentRegionMax().x - right));
        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##cbsearch", ICON_FA_MAGNIFYING_GLASS "  Qidirish", &m_Search);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##size", &m_ThumbnailSize, 48.0f, 160.0f, "%.0f");
    }

    void ContentBrowserPanel::DrawContextMenu()
    {
        if (ImGui::BeginPopupContextWindow("CBContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem(ICON_FA_FOLDER "  Yangi papka")) { m_NewKind = 0; m_NewName = "NewFolder"; m_OpenNewPopup = true; }
            if (ImGui::MenuItem(ICON_FA_EARTH_ASIA "  Yangi sahna")) { m_NewKind = 1; m_NewName = "NewScene"; m_OpenNewPopup = true; }
            if (ImGui::MenuItem(ICON_FA_FILE_CODE "  Yangi C++ skript...") && OnRequestNewScript) OnRequestNewScript();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Fayl menejerida ochish"))
                Platform::OpenInFileBrowser(m_CurrentDirectory);
            if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE "  Yangilash"))
                m_Thumbnails.clear();
            ImGui::EndPopup();
        }

        if (m_OpenNewPopup)
        {
            ImGui::OpenPopup("NewItem");
            m_OpenNewPopup = false;
        }
        if (ImGui::BeginPopupModal("NewItem", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_OpenRenamePopup) { ImGui::OpenPopup("RenameItem"); m_OpenRenamePopup = false; }
        if (ImGui::BeginPopupModal("RenameItem", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(ICON_FA_FEATHER "  Yangi nom");
            ImGui::SetNextItemWidth(300);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##rename", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
            if ((ImGui::Button(ICON_FA_CHECK "  OK", ImVec2(120, 0)) || enter) && !m_RenameBuffer.empty())
            {
                std::error_code ec;
                fs::path target = m_RenameTarget.parent_path() / m_RenameBuffer;
                if (!fs::exists(target))
                    fs::rename(m_RenameTarget, target, ec);
                if (ec) ZM_ERROR("Nomlab bo'lmadi: {}", ec.message());
                m_Thumbnails.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!m_PendingDelete.empty())
        {
            ImGui::OpenPopup("ConfirmDelete");
        }
        if (ImGui::BeginPopupModal("ConfirmDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION "  '%s' o'chirilsinmi?", m_PendingDelete.filename().string().c_str());
            ImGui::TextDisabled("Bu amalni qaytarib bo'lmaydi.");
            if (ImGui::Button(ICON_FA_TRASH "  O'chirish", ImVec2(140, 0)))
            {
                std::error_code ec;
                fs::remove_all(m_PendingDelete, ec);
                m_Thumbnails.erase(m_PendingDelete.string());
                m_PendingDelete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(120, 0)))
            {
                m_PendingDelete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserPanel::OnImGuiRender(bool* open)
    {
        if (!*open)
            return;
        if (!ImGui::Begin(ICON_FA_FOLDER_OPEN "  Kontent###ContentBrowser", open))
        {
            ImGui::End();
            return;
        }

        if (m_Root.empty() || !fs::exists(m_CurrentDirectory))
        {
            ImGui::TextDisabled("Loyiha ochilmagan");
            ImGui::End();
            return;
        }

        DrawBreadcrumbs();
        ImGui::Separator();

        ImGui::BeginChild("##grid");
        float padding = 14.0f;
        float cellSize = m_ThumbnailSize + padding;
        int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellSize));

        // Collect and sort entries: directories first, then files.
        std::vector<fs::directory_entry> entries;
        std::error_code ec;
        std::string search = Lower(m_Search);
        auto collect = [&](const fs::path& dir, bool recursive) {
            auto add = [&](const fs::directory_entry& e) {
                std::string name = e.path().filename().string();
                if (name.starts_with(".")) return;
                if (m_CurrentDirectory == m_Root && e.is_directory() && (name == "Build" || name == "Binaries")) return;
                if (!search.empty() && Lower(name).find(search) == std::string::npos) return;
                entries.push_back(e);
            };
            if (recursive)
                for (auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) add(e);
            else
                for (auto& e : fs::directory_iterator(dir, ec)) add(e);
        };
        collect(m_CurrentDirectory, !search.empty());
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.is_directory() != b.is_directory()) return a.is_directory();
            return a.path().filename().string() < b.path().filename().string();
        });

        if (ImGui::BeginTable("##cbgrid", columns))
        {
            for (const auto& entry : entries)
            {
                ImGui::TableNextColumn();
                const fs::path& path = entry.path();
                std::string name = path.filename().string();
                bool isDir = entry.is_directory();
                ImGui::PushID(path.string().c_str());

                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImVec2 tileSize(m_ThumbnailSize, m_ThumbnailSize + ImGui::GetTextLineHeightWithSpacing() * 2.0f);
                ImGui::InvisibleButton("##tile", tileSize);
                bool hovered = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 tileMax(cursor.x + tileSize.x, cursor.y + tileSize.y);
                dl->AddRectFilled(cursor, tileMax, hovered ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 6), 6.0f);

                // icon / thumbnail
                ImVec2 iconMin(cursor.x + 6, cursor.y + 6);
                ImVec2 iconMax(cursor.x + m_ThumbnailSize - 6, cursor.y + m_ThumbnailSize - 6);
                Ref<Texture2D> thumb = isDir ? nullptr : GetThumbnail(path);
                if (thumb)
                {
                    dl->AddImageRounded((ImTextureID)(uintptr_t)thumb->GetRendererID(), iconMin, iconMax, ImVec2(0, 1), ImVec2(1, 0),
                                        IM_COL32_WHITE, 4.0f);
                }
                else
                {
                    ImU32 color;
                    const char* icon = IconFor(path, isDir, color);
                    ImFont* font = ImGuiLayer::GetLargeIconFont();
                    float size = (m_ThumbnailSize - 12) * 0.62f;
                    ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
                    ImVec2 pos(cursor.x + (m_ThumbnailSize - textSize.x) * 0.5f, cursor.y + (m_ThumbnailSize - textSize.y) * 0.5f);
                    dl->AddText(font, size, pos, color, icon);
                }

                // label (2 lines max, clipped)
                ImVec4 clip(cursor.x + 2, cursor.y + m_ThumbnailSize, tileMax.x - 2, tileMax.y);
                std::string label = name;
                ImVec2 labelSize = ImGui::CalcTextSize(label.c_str(), nullptr, false, m_ThumbnailSize - 4);
                float lx = cursor.x + std::max(2.0f, (m_ThumbnailSize - std::min(labelSize.x, m_ThumbnailSize - 4)) * 0.5f);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(lx, cursor.y + m_ThumbnailSize), ImGui::GetColorU32(ImGuiCol_Text),
                            label.c_str(), nullptr, m_ThumbnailSize - 4, &clip);

                if (hovered)
                    UI::Tooltip(name.c_str());

                if (ImGui::BeginDragDropSource())
                {
                    std::string rel = Project::GetAssetRelativePath(path);
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", rel.c_str(), rel.size() + 1);
                    ImU32 c;
                    ImGui::Text("%s  %s", IconFor(path, isDir, c), name.c_str());
                    ImGui::EndDragDropSource();
                }

                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (isDir)
                        m_CurrentDirectory /= path.filename();
                    else if (path.extension() == ".zscene" && OnOpenScene)
                        OnOpenScene(path);
                    else
                        Platform::OpenInFileBrowser(path);
                }

                if (ImGui::BeginPopupContextItem("##item"))
                {
                    if (path.extension() == ".zscene" && ImGui::MenuItem(ICON_FA_EARTH_ASIA "  Sahnani ochish") && OnOpenScene)
                        OnOpenScene(path);
                    if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Tizimda ochish"))
                        Platform::OpenInFileBrowser(path);
                    if (ImGui::MenuItem(ICON_FA_FEATHER "  Nomini o'zgartirish"))
                    {
                        m_RenameTarget = path;
                        m_RenameBuffer = path.filename().string();
                        m_OpenRenamePopup = true;
                    }
                    if (ImGui::MenuItem(ICON_FA_TRASH "  O'chirish"))
                        m_PendingDelete = path;
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (entries.empty())
            ImGui::TextDisabled(search.empty() ? "Papka bo'sh. O'ng tugma bilan yangi narsa yarating." : "Hech narsa topilmadi");

        DrawContextMenu();
        ImGui::EndChild();
        ImGui::End();
    }

}
