#include "AssetInspectorPanel.h"
#include "ContentBrowserPanel.h"
#include "SceneHierarchyPanel.h"
#include "../AssetUsageCache.h"
#include "../UI.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <map>
#include <regex>
#include <thread>

namespace Zamin {

    namespace fs = std::filesystem;

    static AssetRegistry* GetRegistry() { return Project::GetActive() ? Project::GetAssetRegistry() : nullptr; }

    static const ImVec4 s_ErrorColor(0.95f, 0.45f, 0.4f, 1.0f);
    static const ImVec4 s_WarnColor(0.95f, 0.75f, 0.35f, 1.0f);
    static const ImVec4 s_OkColor(0.5f, 0.85f, 0.6f, 1.0f);

    // "12 345" (thousands separated).
    static std::string FormatCount(uint64_t value)
    {
        std::string digits = std::to_string(value), out;
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0)
                out += ' ';
            out += digits[i];
        }
        return out;
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

    static const char* ChannelLayoutName(uint32_t channels)
    {
        switch (channels)
        {
            case 1:  return "kulrang";
            case 2:  return "kulrang + alfa";
            case 3:  return "RGB";
            case 4:  return "RGBA";
            default: return "?";
        }
    }

    static uint32_t BytesPerPixel(ImageFormat format)
    {
        switch (format)
        {
            case ImageFormat::R8:      return 1;
            case ImageFormat::RGB8:    return 3;
            case ImageFormat::RGBA32F: return 16;
            default:                   return 4;
        }
    }

    static const char* GpuFormatName(ImageFormat format)
    {
        switch (format)
        {
            case ImageFormat::R8:      return "R8";
            case ImageFormat::RGB8:    return "RGB8";
            case ImageFormat::RGBA8:   return "RGBA8";
            case ImageFormat::RGBA32F: return "RGBA32F";
            default:                   return "?";
        }
    }

    static bool IsPowerOfTwo(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }

    // Small rounded tag after a text ("UUID", "kod"...).
    static void Tag(const char* text, ImU32 color)
    {
        ImGui::SameLine(0.0f, 6.0f);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::CalcTextSize(text);
        float pad = 4.0f;
        float y = pos.y + (ImGui::GetTextLineHeight() - size.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, y - 1.0f), ImVec2(pos.x + size.x + pad * 2.0f, y + size.y + 1.0f), color, 3.0f);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, pos.y));
        ImGui::TextUnformatted(text);
        ImGui::SameLine(0.0f, pad);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    AssetInspectorPanel::AssetInspectorPanel() = default;

    AssetInspectorPanel::~AssetInspectorPanel()
    {
        StopAudio();
    }

    // ------------------------------------------------------------------ target
    void AssetInspectorPanel::SetAsset(const fs::path& path)
    {
        if (path.empty())
        {
            Clear();
            return;
        }
        if (path == m_Path)
            return; // picked again: edits stay
        KeepUnappliedEdits();
        StopAudio();
        std::error_code ec;
        m_Path = path;
        m_IsFolder = fs::is_directory(path, ec);
        m_Type = m_IsFolder ? AssetType::Folder : AssetFiles::GetType(path);
        m_Status.clear();
        AssetRegistry* registry = GetRegistry();
        const AssetMetadata* meta = registry && !m_IsFolder ? registry->Find(path) : nullptr;
        m_Handle = meta ? meta->Handle : AssetHandle();
        LoadSettings(meta);
        // Preview state starts over for another file.
        m_ChannelView = ImageChannelView::RGBA;
        m_Zoom = 0;
        m_Yaw = 0.6f;
        m_Pitch = 0.35f;
        m_ZoomDistance = 1.0f;
        m_Clip = -1;
        m_ClipPlaying = false;
        m_ClipTime = 0.0f;
        m_Preview = nullptr;
        m_PreviewPath.clear();
        m_Model = nullptr;
        m_FolderSummary = {};
        ResetPreviews();
        m_VersionPath.clear();
    }

    void AssetInspectorPanel::SetSelection(const std::vector<fs::path>& paths)
    {
        m_Selection = paths;
    }

    void AssetInspectorPanel::Clear()
    {
        if (!m_Path.empty())
            KeepUnappliedEdits();
        StopAudio();
        m_Path.clear();
        m_IsFolder = false;
        m_Selection.clear();
        m_Type = AssetType::Other;
        m_Handle = {};
        m_Status.clear();
        m_Preview = nullptr;
        m_PreviewPath.clear();
        m_Model = nullptr;
        m_FolderSummary = {};
        ResetPreviews();
        m_VersionPath.clear();
        LoadSettings(nullptr);
    }

    void AssetInspectorPanel::Reset()
    {
        Clear();
        m_Pending.clear(); // another project: its assets are not this one's
    }

    void AssetInspectorPanel::LoadSettings(const AssetMetadata* meta)
    {
        m_TextureSaved = meta ? AssetImporter::ReadTextureSettings(meta->Import) : TextureImportSettings{};
        m_ModelSaved = meta ? AssetImporter::ReadModelSettings(meta->Import) : ModelImportSettings{};
        m_TextureEdit = m_TextureSaved;
        m_ModelEdit = m_ModelSaved;
    }

    const AssetMetadata* AssetInspectorPanel::FindMetadata() const
    {
        AssetRegistry* registry = GetRegistry();
        if (!registry || m_IsFolder)
            return nullptr;
        if (m_Handle)
            if (const AssetMetadata* meta = registry->Find(m_Handle))
                return meta;
        return registry->Find(m_Path);
    }

    void AssetInspectorPanel::Sync()
    {
        AssetRegistry* registry = GetRegistry();
        const AssetMetadata* meta = FindMetadata();
        if (!registry || !meta)
            return;
        m_Handle = meta->Handle;
        // Renamed / moved (in the browser or outside the editor): the handle tells where it is now.
        fs::path current = registry->ToFileSystemPath(meta->RelativePath);
        if (registry->ToRelativePath(m_Path) != meta->RelativePath)
        {
            m_Path = current;
            m_Type = AssetFiles::GetType(current);
            m_Preview = nullptr;
            m_PreviewPath.clear();
            m_Model = nullptr;
            ResetPreviews();
            m_VersionPath.clear();
        }
        // Settings changed on disk (a .zmeta edited by hand, version control): taken over, and
        // into the editor fields too unless they hold edits of their own.
        TextureImportSettings texture = AssetImporter::ReadTextureSettings(meta->Import);
        if (!(texture == m_TextureSaved))
        {
            if (m_TextureEdit == m_TextureSaved)
                m_TextureEdit = texture;
            m_TextureSaved = texture;
        }
        ModelImportSettings model = AssetImporter::ReadModelSettings(meta->Import);
        if (!(model == m_ModelSaved))
        {
            if (m_ModelEdit == m_ModelSaved)
                m_ModelEdit = model;
            m_ModelSaved = model;
        }
    }

    bool AssetInspectorPanel::CheckFileVersion()
    {
        std::error_code ec;
        fs::file_time_type time = fs::last_write_time(m_Path, ec);
        uintmax_t size = m_IsFolder ? 0 : fs::file_size(m_Path, ec);
        if (ec)
            size = 0;
        if (m_VersionPath == m_Path && time == m_VersionTime && size == m_VersionSize)
            return false;
        m_VersionPath = m_Path;
        m_VersionTime = time;
        m_VersionSize = size;
        ResetPreviews();
        return true;
    }

    void AssetInspectorPanel::ResetPreviews()
    {
        // What is read from the file; the texture / model objects themselves are updated in place
        // by hot reload (and looked up again by path when needed).
        m_ImageInfo = {};
        m_ImageInfoRead = false;
        m_Pixels.reset();
        m_PixelsFailed = false;
        m_ChannelTexture = nullptr;
        m_ChannelSource = nullptr;
        m_ModelInfo = {};
        m_ModelInfoRead = false;
        m_ModelLoaded = false;
        m_FontLoaded = false;
        m_AudioInfo = {};
        m_AudioInfoRead = false;
        m_Waveform = nullptr;
        m_Summary = {};
    }

    // ------------------------------------------------------------------ apply / revert
    bool AssetInspectorPanel::HasUnappliedChanges() const
    {
        switch (m_Type)
        {
            case AssetType::Texture: return !(m_TextureEdit == m_TextureSaved);
            case AssetType::Model:   return !(m_ModelEdit == m_ModelSaved);
            default:                 return false;
        }
    }

    AssetInspectorPanel::PendingEdits AssetInspectorPanel::CaptureEdits() const
    {
        PendingEdits edits;
        edits.Path = m_Path;
        edits.Handle = m_Handle;
        edits.Type = m_Type;
        edits.Texture = m_TextureEdit;
        edits.Model = m_ModelEdit;
        return edits;
    }

    void AssetInspectorPanel::KeepUnappliedEdits()
    {
        if (!HasUnappliedChanges() || !FindMetadata())
            return;
        PendingEdits edits = CaptureEdits();
        auto same = [&](const PendingEdits& p) { return p.Handle == edits.Handle && p.Path == edits.Path; };
        m_Pending.erase(std::remove_if(m_Pending.begin(), m_Pending.end(), same), m_Pending.end());
        m_Pending.push_back(std::move(edits));
    }

    bool AssetInspectorPanel::ApplyEdits(const PendingEdits& edits, bool showStatus)
    {
        AssetRegistry* registry = GetRegistry();
        if (!registry)
            return false;
        const AssetMetadata* meta = edits.Handle ? registry->Find(edits.Handle) : nullptr;
        if (!meta)
            meta = registry->Find(edits.Path);
        if (!meta)
            return false;
        AssetImportSettings import = meta->Import; // keys of other tools stay
        if (edits.Type == AssetType::Texture)
            AssetImporter::WriteTextureSettings(edits.Texture, import);
        else if (edits.Type == AssetType::Model)
            AssetImporter::WriteModelSettings(edits.Model, import);
        else
            return false;
        std::string name = meta->RelativePath;
        fs::path file = registry->ToFileSystemPath(meta->RelativePath);
        if (!registry->SetImportSettings(meta->Handle, import))
            return false;

        // Imported again right away: every texture / model object made from the file updates in
        // place (the watcher's event for the .zmeta then finds nothing new).
        bool failed = false;
        size_t updated = AssetHotReload::ReloadAsset(file, &failed);
        if (showStatus)
        {
            m_StatusError = failed;
            m_StatusTime = ImGui::GetTime();
            if (failed)
                m_Status = "Saqlandi, lekin qayta import bo'lmadi - konsolni tekshiring";
            else if (updated > 0)
                m_Status = "Saqlandi va qayta import qilindi";
            else
                m_Status = "Saqlandi (keyingi yuklashda ishlatiladi)";
        }
        ZM_INFO("Import sozlamalari saqlandi: {}{}", name, updated > 0 && !failed ? " (qayta import qilindi)" : "");
        return true;
    }

    bool AssetInspectorPanel::Apply()
    {
        if (!HasUnappliedChanges() || !ApplyEdits(CaptureEdits(), true))
            return false;
        m_TextureSaved = m_TextureEdit;
        m_ModelSaved = m_ModelEdit;
        m_ChannelTexture = nullptr; // made with the old sampling settings
        return true;
    }

    void AssetInspectorPanel::Revert()
    {
        m_TextureEdit = m_TextureSaved;
        m_ModelEdit = m_ModelSaved;
    }

    void AssetInspectorPanel::DrawPopups()
    {
        if (m_Pending.empty())
            return;
        if (!ImGui::IsPopupOpen("###UnappliedImport"))
            ImGui::OpenPopup("###UnappliedImport");
        if (ImGui::BeginPopupModal(ICON_FA_TRIANGLE_EXCLAMATION "  Qo'llanmagan import sozlamalari###UnappliedImport", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            const PendingEdits& edits = m_Pending.front();
            AssetRegistry* registry = GetRegistry();
            const AssetMetadata* meta = registry ? (edits.Handle ? registry->Find(edits.Handle) : registry->Find(edits.Path)) : nullptr;
            std::string name = meta ? meta->RelativePath : edits.Path.filename().string();
            ImGui::Text("'%s' import sozlamalari o'zgartirilgan, lekin qo'llanmagan.", name.c_str());
            ImGui::TextDisabled("Qo'llansa .zmeta fayliga yoziladi va asset darhol qayta import qilinadi.");
            ImGui::Spacing();
            bool close = false;
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentU32(0.75f));
            if (ImGui::Button(ICON_FA_CHECK "  Qo'llash", ImVec2(150, 0)))
            {
                if (!ApplyEdits(edits, false))
                    ZM_WARN("Import sozlamalari saqlanmadi: '{}' ro'yxatda yo'q", name);
                close = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK "  Tashlab yuborish", ImVec2(170, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                close = true;
            if (close)
            {
                m_Pending.erase(m_Pending.begin());
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------ drawing
    void AssetInspectorPanel::OnImGuiRender()
    {
        if (m_Path.empty())
            return;
        if (m_Selection.size() > 1)
        {
            DrawMultiSelection();
            return;
        }
        Sync();
        CheckFileVersion();
        const AssetMetadata* meta = FindMetadata();
        DrawHeader(meta);

        std::error_code ec;
        if (!fs::exists(m_Path, ec))
        {
            ImGui::Spacing();
            ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  Fayl topilmadi (o'chirilgan yoki ko'chirilgan)");
            return;
        }
        if (m_IsFolder)
        {
            DrawFolder();
            return;
        }
        switch (m_Type)
        {
            case AssetType::Texture:     DrawTexture(meta); break;
            case AssetType::Environment: DrawTexture(nullptr); break;
            case AssetType::Model:       DrawModel(meta); break;
            case AssetType::Audio:       DrawAudio(); break;
            case AssetType::Scene:
            case AssetType::Prefab:      DrawSceneFile(); break;
            case AssetType::Font:        DrawFont(); break;
            case AssetType::Script:      DrawScript(); break;
            default:
                ImGui::Spacing();
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Bu turdagi fayl uchun ko'rinish yo'q");
                break;
        }
        if (AssetTypes::IsRegistered(m_Type))
            DrawUsage();
    }

    void AssetInspectorPanel::DrawHeader(const AssetMetadata* meta)
    {
        ImU32 color = 0;
        const char* icon = ContentBrowserPanel::GetTypeIcon(m_Type, &color);
        ImGui::PushFont(ImGuiLayer::GetLargeIconFont());
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(m_Path.filename().string().c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("%s", AssetFiles::GetTypeName(m_Type));
        // Quick actions.
        if (OnRevealRequested && ImGui::SmallButton(ICON_FA_FOLDER_OPEN "##reveal"))
            OnRevealRequested(m_Path);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Kontent brauzerda ko'rsatish");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_FILE_EXPORT "##system"))
            Platform::OpenInFileBrowser(m_Path);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Tizimda ochish");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_COPY "##copyPath"))
            ImGui::SetClipboardText(Project::GetAssetRelativePath(m_Path).c_str());
        if (ImGui::IsItemHovered())
            UI::Tooltip("Yo'lni nusxalash");
        if (m_Type == AssetType::Scene && OnOpenScene)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_EARTH_ASIA "  Ochish"))
                OnOpenScene(m_Path);
        }
        ImGui::EndGroup();
        ImGui::Separator();

        if (UI::BeginPropertyGrid("##assetInfo"))
        {
            UI::PropertyLabel("Yo'l");
            std::string relative = meta ? meta->RelativePath : Project::GetAssetRelativePath(m_Path);
            if (!meta && Project::GetActive() && !AssetFiles::IsInside(m_Path, Project::GetAssetDirectory()))
            {
                // Outside the asset folder (Source, the project file): relative to the project.
                std::error_code ec;
                fs::path projectRelative = fs::relative(m_Path, Project::GetProjectDirectory(), ec);
                if (!ec && !projectRelative.empty())
                    relative = projectRelative.generic_string();
            }
            ImGui::TextWrapped("%s", relative.c_str());

            if (!m_IsFolder)
            {
                UI::PropertyLabel("UUID", "Assetning doimiy identifikatori (.zmeta faylida). Fayl nomi o'zgarsa ham "
                                          "sahnalardagi havolalar u orqali topiladi.");
                if (meta && meta->HasMetaFile)
                {
                    std::string id = meta->Handle.ToString();
                    ImGui::TextUnformatted(id.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_COPY "##copyUUID"))
                        ImGui::SetClipboardText(id.c_str());
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Nusxalash");
                }
                else
                    ImGui::TextDisabled(meta ? "faqat shu seans uchun (.zmeta yo'q)" : "ro'yxatda yo'q");

                std::error_code ec;
                uintmax_t size = fs::file_size(m_Path, ec);
                if (!ec)
                {
                    UI::PropertyLabel("Hajm");
                    ImGui::TextUnformatted(AssetFiles::FormatSize(size).c_str());
                }
            }
            std::string modified = FormatTime(m_VersionTime);
            if (!modified.empty())
            {
                UI::PropertyLabel("O'zgartirilgan");
                ImGui::TextUnformatted(modified.c_str());
            }
            UI::EndPropertyGrid();
        }
    }

    // ------------------------------------------------------------------ where it is used
    void AssetInspectorPanel::DrawUsage()
    {
        if (!m_Usage || !Project::GetActive())
            return;
        const AssetReferenceIndex& index = m_Usage->Get();
        std::string relative = Project::GetAssetRelativePath(m_Path);
        const std::vector<AssetReferrer>& referrers = index.GetReferrers(relative);

        ImGui::Spacing();
        std::string title = std::format(ICON_FA_LINK "  Ishlatilgan joylar ({})###usedBy", referrers.size());
        if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (referrers.empty())
            {
                ImGui::TextColored(s_WarnColor, ICON_FA_CIRCLE_INFO "  Hech qayerda ishlatilmayapti");
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Sahnalar, prefablar, materiallar, loyiha fayli, modellar va C++ koddagi satrlar tekshirildi. "
                                   "Kod yo'lni qismlardan yig'sa (\"Level\" + n), bu ro'yxatda ko'rinmaydi.");
                ImGui::PopStyleColor();
            }
            for (const AssetReferrer& r : referrers)
            {
                ImGui::PushID(r.Name.c_str());
                AssetType type = AssetFiles::GetType(r.File);
                ImU32 color = 0;
                const char* icon = ContentBrowserPanel::GetTypeIcon(type, &color);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(icon);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                std::string label = r.AssetPath.empty() ? r.Name : r.AssetPath;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(std::max(40.0f, ImGui::GetContentRegionAvail().x - 120.0f), 0)))
                {
                    if (!r.AssetPath.empty() && OnRevealRequested)
                        OnRevealRequested(r.File);
                    else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        Platform::OpenInFileBrowser(r.File);
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{}\n{} ta havola{}", r.Name, r.Count,
                                            r.AssetPath.empty() ? "\nIkki marta bosing: tizimda ochish" : "\nBosing: kontent brauzerda ko'rsatish").c_str());
                if (r.Has(AssetReferenceKind::Path)) Tag("yo'l", IM_COL32(60, 90, 120, 255));
                if (r.Has(AssetReferenceKind::UUID)) Tag("UUID", IM_COL32(70, 110, 80, 255));
                if (r.Has(AssetReferenceKind::ModelFile)) Tag("model", IM_COL32(130, 85, 50, 255));
                if (r.Has(AssetReferenceKind::Code)) Tag("kod", IM_COL32(60, 80, 140, 255));
                ImGui::PopID();
            }
        }

        // What this file itself uses (scenes, prefabs, models, materials).
        if (m_Type == AssetType::Scene || m_Type == AssetType::Prefab || m_Type == AssetType::Model || m_Type == AssetType::Material)
        {
            std::vector<std::string> uses = index.GetReferences(m_Path);
            std::vector<const MissingAssetReference*> broken;
            fs::path self = fs::absolute(m_Path).lexically_normal();
            for (const MissingAssetReference& m : index.GetMissing())
                if (m.File == self)
                    broken.push_back(&m);
            std::string usesTitle = std::format(ICON_FA_DIAGRAM_PROJECT "  Ishlatadigan assetlar ({})###uses", uses.size());
            if (ImGui::CollapsingHeader(usesTitle.c_str()))
            {
                if (uses.empty() && broken.empty())
                    ImGui::TextDisabled("Boshqa assetlarga havola yo'q");
                for (const std::string& asset : uses)
                {
                    ImU32 color = 0;
                    const char* icon = ContentBrowserPanel::GetTypeIcon(AssetFiles::GetType(asset), &color);
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(icon);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (ImGui::Selectable((asset + "##use").c_str()) && OnRevealRequested)
                        OnRevealRequested(Project::GetAssetFileSystemPath(asset));
                }
                for (const MissingAssetReference* m : broken)
                {
                    ImGui::TextColored(s_ErrorColor, ICON_FA_CIRCLE_XMARK "  %s", m->Path.c_str());
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Bunday fayl yo'q (havola buzilgan)");
                }
            }
        }
    }

    // Shared row of Apply / Revert buttons (and the last result).
    void AssetInspectorPanel::DrawApplyBar(bool canApply)
    {
        bool modified = HasUnappliedChanges();
        ImGui::Spacing();
        ImGui::BeginDisabled(!modified || !canApply);
        if (modified && canApply)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentU32(0.75f));
        if (ImGui::Button(ICON_FA_CHECK "  Qo'llash"))
            Apply();
        if (modified && canApply)
            ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Bekor qilish"))
            Revert();
        ImGui::EndDisabled();
        if (modified)
        {
            ImGui::SameLine();
            ImGui::TextColored(s_WarnColor, "qo'llanmagan o'zgarishlar");
        }
        if (!canApply)
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Asset ro'yxatda yo'q - sozlamalarni saqlab bo'lmaydi");
        if (!m_Status.empty() && ImGui::GetTime() - m_StatusTime < 5.0)
            ImGui::TextColored(m_StatusError ? s_ErrorColor : s_OkColor, "%s  %s",
                               m_StatusError ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_CHECK, m_Status.c_str());
    }

    // ------------------------------------------------------------------ textures
    Ref<Texture2D> AssetInspectorPanel::GetPreviewTexture()
    {
        std::error_code ec;
        fs::file_time_type time = fs::last_write_time(m_Path, ec);
        if (m_PreviewPath != m_Path || time != m_PreviewTime)
        {
            m_PreviewPath = m_Path;
            m_PreviewTime = time;
            m_Preview = TextureLibrary::Load(m_Path); // the cached texture (hot reload updates it in place)
        }
        return m_Preview;
    }

    const ImageData* AssetInspectorPanel::GetPixels()
    {
        if (!m_Pixels && !m_PixelsFailed)
        {
            auto pixels = std::make_unique<ImageData>();
            std::string error;
            // Bottom-up rows like the GPU texture, so both are shown with the same coordinates.
            if (Image::Load(m_Path.string(), *pixels, 4, true, &error))
                m_Pixels = std::move(pixels);
            else
                m_PixelsFailed = true;
        }
        return m_Pixels.get();
    }

    Ref<Texture2D> AssetInspectorPanel::GetChannelTexture(const Ref<Texture2D>& preview)
    {
        if (m_ChannelView == ImageChannelView::RGBA || !preview)
            return preview;
        bool stale = !m_ChannelTexture || m_ChannelTextureView != m_ChannelView || m_ChannelSource != preview ||
                     m_ChannelSourceWidth != preview->GetWidth() || m_ChannelSourceHeight != preview->GetHeight();
        if (!stale)
            return m_ChannelTexture;
        const ImageData* pixels = GetPixels();
        if (!pixels || !pixels->IsValid())
            return preview;
        TextureSpecification spec;
        spec.Width = pixels->Width;
        spec.Height = pixels->Height;
        spec.Format = ImageFormat::RGBA8;
        spec.Filter = preview->GetSpecification().Filter;
        spec.Wrap = TextureWrap::ClampToEdge;
        spec.GenerateMips = preview->GetSpecification().GenerateMips;
        m_ChannelTexture = Texture2D::Create(spec);
        std::vector<uint8_t> view = Image::MakeChannelView(pixels->Pixels, m_ChannelView);
        m_ChannelTexture->SetData(view.data(), (uint32_t)view.size());
        m_ChannelTextureView = m_ChannelView;
        m_ChannelSource = preview;
        m_ChannelSourceWidth = preview->GetWidth();
        m_ChannelSourceHeight = preview->GetHeight();
        return m_ChannelTexture;
    }

    void AssetInspectorPanel::DrawTexturePreview(const Ref<Texture2D>& texture)
    {
        // ---- channel views and zoom
        const std::pair<ImageChannelView, const char*> views[] = {
            { ImageChannelView::RGBA, "RGBA" }, { ImageChannelView::RGB, "RGB" }, { ImageChannelView::Red, "R" },
            { ImageChannelView::Green, "G" }, { ImageChannelView::Blue, "B" }, { ImageChannelView::Alpha, "A" },
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 4.0f));
        for (const auto& [view, label] : views)
        {
            bool active = m_ChannelView == view;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentU32(0.7f));
            if (ImGui::SmallButton(label))
                m_ChannelView = view;
            if (active)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                UI::Tooltip(view == ImageChannelView::RGBA ? "Rang va shaffoflik" : view == ImageChannelView::RGB ? "Faqat rang (shaffofliksiz)"
                            : view == ImageChannelView::Alpha ? "Shaffoflik (alfa) kulrangda" : "Bitta kanal kulrangda");
            ImGui::SameLine();
        }
        ImGui::PopStyleVar();
        const char* zooms[] = { "Moslash", "1:1", "2:1", "4:1", "8:1" };
        int zoomIndex = m_Zoom == 0 ? 0 : m_Zoom == 1 ? 1 : m_Zoom == 2 ? 2 : m_Zoom == 4 ? 3 : 4;
        float comboWidth = 90.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - comboWidth));
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::Combo("##zoom", &zoomIndex, zooms, 5))
            m_Zoom = zoomIndex == 0 ? 0 : 1 << (zoomIndex - 1);

        Ref<Texture2D> shown = GetChannelTexture(texture);
        float width = (float)texture->GetWidth(), height = (float)texture->GetHeight();
        float availX = ImGui::GetContentRegionAvail().x;
        ImVec2 size;
        bool scrolled = m_Zoom > 0;
        if (!scrolled)
        {
            float scale = std::min({ availX / std::max(width, 1.0f), 240.0f / std::max(height, 1.0f), 4.0f });
            size = ImVec2(std::max(1.0f, width * scale), std::max(1.0f, height * scale));
        }
        else
            size = ImVec2(width * m_Zoom, height * m_Zoom);

        if (scrolled)
        {
            float childHeight = std::min(size.y + ImGui::GetStyle().ScrollbarSize + 4.0f, 320.0f);
            ImGui::BeginChild("##texView", ImVec2(0, childHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        }
        else
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availX - size.x) * 0.5f);
        ImVec2 min = ImGui::GetCursorScreenPos();
        if (m_ChannelView == ImageChannelView::RGBA)
        {
            // Checkerboard behind transparent pixels (only the visible part).
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
            const float cell = 8.0f;
            float x0 = std::max(0.0f, std::floor((clipMin.x - min.x) / cell) * cell);
            float y0 = std::max(0.0f, std::floor((clipMin.y - min.y) / cell) * cell);
            for (float y = y0; y < size.y && min.y + y < clipMax.y; y += cell)
                for (float x = x0; x < size.x && min.x + x < clipMax.x; x += cell)
                {
                    bool dark = ((int)(x / cell) + (int)(y / cell)) % 2 == 0;
                    dl->AddRectFilled(ImVec2(min.x + x, min.y + y), ImVec2(min.x + std::min(x + cell, size.x), min.y + std::min(y + cell, size.y)),
                                      dark ? IM_COL32(60, 62, 68, 255) : IM_COL32(85, 88, 95, 255));
                }
        }
        ImGui::Image((ImTextureID)(uintptr_t)shown->GetRendererID(), size, ImVec2(0, 1), ImVec2(1, 0));
        // The texel under the mouse.
        if (ImGui::IsItemHovered() && width * height <= 4096.0f * 4096.0f)
        {
            ImVec2 mouse = ImGui::GetMousePos();
            int x = std::clamp((int)((mouse.x - min.x) / size.x * width), 0, (int)width - 1);
            int yTop = std::clamp((int)((mouse.y - min.y) / size.y * height), 0, (int)height - 1);
            if (const ImageData* pixels = GetPixels(); pixels && pixels->Width == (uint32_t)width && pixels->Height == (uint32_t)height)
            {
                size_t row = (size_t)(pixels->Height - 1 - (uint32_t)yTop); // rows are bottom-up
                const uint8_t* p = &pixels->Pixels[(row * pixels->Width + (size_t)x) * 4];
                ImGui::BeginTooltip();
                ImGui::ColorButton("##texel", ImVec4(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f),
                                   ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip, ImVec2(28, 28));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text("x %d, y %d", x, yTop);
                ImGui::Text("R %d  G %d  B %d  A %d", p[0], p[1], p[2], p[3]);
                ImGui::EndGroup();
                ImGui::EndTooltip();
            }
        }
        if (scrolled)
            ImGui::EndChild();
    }

    void AssetInspectorPanel::DrawTexture(const AssetMetadata* meta)
    {
        if (!m_ImageInfoRead)
        {
            m_ImageInfo = AssetInfo::ReadImage(m_Path);
            m_ImageInfoRead = true;
        }
        bool environment = m_Type == AssetType::Environment;
        ImGui::Spacing();
        Ref<Texture2D> texture = GetPreviewTexture();
        if (texture && texture->IsLoaded())
            DrawTexturePreview(texture);
        else
        {
            ImGui::TextColored(s_ErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  Rasm o'qilmadi - konsolni tekshiring");
            if (!m_ImageInfo.Error.empty())
                ImGui::TextDisabled("Sabab: %s", m_ImageInfo.Error.c_str());
        }

        // ---- what the file and the GPU texture are
        ImGui::Spacing();
        if (UI::BeginPropertyGrid("##textureInfo"))
        {
            UI::PropertyHeader("Ma'lumot");
            if (m_ImageInfo.Valid)
            {
                UI::PropertyLabel("O'lcham");
                bool pot = IsPowerOfTwo(m_ImageInfo.Width) && IsPowerOfTwo(m_ImageInfo.Height);
                ImGui::Text("%u x %u px", m_ImageInfo.Width, m_ImageInfo.Height);
                if (pot)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(2^n)");
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Ikkala tomoni ham 2 ning darajasi");
                }
                UI::PropertyLabel("Fayl", "Fayl formati, unda saqlangan kanallar va bitta kanal uchun bitlar.");
                ImGui::Text("%s, %u kanal (%s), %s", m_ImageInfo.Format.c_str(), m_ImageInfo.Channels, ChannelLayoutName(m_ImageInfo.Channels),
                            m_ImageInfo.IsHDR ? "32-bit float (HDR)" : m_ImageInfo.Is16Bit ? "16-bit" : "8-bit");
                if (m_ImageInfo.Is16Bit)
                {
                    UI::PropertyLabel("");
                    ImGui::TextColored(s_WarnColor, ICON_FA_CIRCLE_INFO "  GPU'ga 8-bit sifatida yuklanadi");
                }
            }
            else
            {
                UI::PropertyLabel("Fayl");
                ImGui::TextColored(s_ErrorColor, "%s", m_ImageInfo.Error.empty() ? "o'qilmadi" : m_ImageInfo.Error.c_str());
            }
            if (texture && texture->IsLoaded() && !environment)
            {
                const TextureSpecification& spec = texture->GetSpecification();
                UI::PropertyLabel("GPU formati", "Teksturaning video xotiradagi formati. sRGB: shader ranglarni chiziqli yorug'likka o'tkazadi.");
                ImGui::Text("%s  |  %s", GpuFormatName(spec.Format), spec.SRGB ? "sRGB rang" : "chiziqli (ma'lumot)");
                uint32_t levels = spec.GenerateMips ? AssetInfo::GetMipLevelCount(spec.Width, spec.Height) : 1;
                UI::PropertyLabel("Mipmap");
                if (spec.GenerateMips)
                    ImGui::Text("%u daraja", levels);
                else
                    ImGui::TextDisabled("yo'q");
                UI::PropertyLabel("Video xotira", "Taxminiy: barcha mip darajalari bilan.");
                ImGui::TextUnformatted(AssetFiles::FormatSize(AssetInfo::EstimateTextureMemory(spec.Width, spec.Height,
                                                                                              BytesPerPixel(spec.Format), spec.GenerateMips)).c_str());
            }
            if (environment)
            {
                UI::PropertyLabel("Ishlatilishi");
                ImGui::TextWrapped("Sahna sozlamalaridagi HDRI muhit (osmon va yoritish)");
            }
            UI::EndPropertyGrid();
        }
        if (environment)
            return;

        // ---- import settings
        bool canApply = meta != nullptr;
        if (UI::BeginPropertyGrid("##textureImport"))
        {
            UI::PropertyHeader("Import sozlamalari");
            const TextureFilter filters[] = { TextureFilter::Linear, TextureFilter::Nearest };
            const char* filterNames[] = { AssetImporter::GetFilterName(filters[0]), AssetImporter::GetFilterName(filters[1]) };
            int filter = m_TextureEdit.Filter == TextureFilter::Nearest ? 1 : 0;
            UI::PropertyLabel("Filtr", "Kattalashtirilganda: silliq (linear) yoki aniq kvadrat piksellar (nearest - pixel-art uchun).");
            if (ImGui::Combo("##filter", &filter, filterNames, 2))
                m_TextureEdit.Filter = filters[filter];

            const TextureWrap wraps[] = { TextureWrap::Repeat, TextureWrap::ClampToEdge, TextureWrap::MirroredRepeat };
            const char* wrapNames[] = { AssetImporter::GetWrapName(wraps[0]), AssetImporter::GetWrapName(wraps[1]), AssetImporter::GetWrapName(wraps[2]) };
            int wrap = (int)(std::find(std::begin(wraps), std::end(wraps), m_TextureEdit.Wrap) - std::begin(wraps));
            UI::PropertyLabel("Wrap", "UV 0..1 dan tashqarida (tiling): rasm takrorlanadi, chetdagi piksel cho'ziladi "
                                      "yoki har safar ko'zgudek aks etadi.");
            if (ImGui::Combo("##wrap", &wrap, wrapNames, 3))
                m_TextureEdit.Wrap = wraps[wrap];

            UI::PropertyLabel("Mipmap", "Kichraytirilgan nusxalar: uzoqdagi yuzalar titramaydi va tezroq chiziladi. "
                                        "Pixel-art va UI rasmlari uchun o'chirish mumkin.");
            ImGui::Checkbox("##mipmaps", &m_TextureEdit.Mipmaps);

            UI::PropertyLabel("sRGB (rang)", "Rasm ranglari sRGB'da saqlangan (fotolar, sprite, albedo / emissive). "
                                             "Ma'lumot teksturalari (maska, lookup) uchun o'chiring - qiymatlar o'zgarmay ishlatiladi.");
            ImGui::Checkbox("##srgb", &m_TextureEdit.SRGB);
            UI::EndPropertyGrid();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(ICON_FA_CIRCLE_INFO "  Sprite / Tilemap'dagi 'Pixel art' belgisi filtr va mipmap'ni bekor qiladi");
        ImGui::PopStyleColor();
        DrawApplyBar(canApply);
    }

    // ------------------------------------------------------------------ models
    void AssetInspectorPanel::RenderModelPreview(const Ref<Model>& model, uint32_t width, uint32_t height)
    {
        if (!m_ModelTarget)
        {
            FramebufferSpecification spec;
            spec.Width = width;
            spec.Height = height;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
            m_ModelTarget = Framebuffer::Create(spec);
            FramebufferSpecification finalSpec;
            finalSpec.Width = width;
            finalSpec.Height = height;
            finalSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
            m_ModelFinal = Framebuffer::Create(finalSpec);
        }
        else if (m_ModelTarget->GetSpecification().Width != width || m_ModelTarget->GetSpecification().Height != height)
        {
            m_ModelTarget->Resize(width, height);
            m_ModelFinal->Resize(width, height);
        }

        // The preview's draw calls are not the scene's (the statistics window).
        RenderStatistics stats = Renderer::GetStats();
        m_ModelTarget->Bind();
        RenderCommand::SetClearColor({ 0.016f, 0.018f, 0.024f, 1.0f }); // linear
        RenderCommand::Clear();

        AABB bounds = model->GetBounds();
        glm::vec3 center = (bounds.Min + bounds.Max) * 0.5f;
        float radius = std::max(0.01f, glm::length(bounds.Max - bounds.Min) * 0.5f);
        const float fov = glm::radians(35.0f);
        float distance = radius / std::sin(fov * 0.5f) * m_ZoomDistance;
        glm::vec3 direction(std::cos(m_Pitch) * std::sin(m_Yaw), std::sin(m_Pitch), std::cos(m_Pitch) * std::cos(m_Yaw));
        glm::vec3 eye = center + direction * distance;
        glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(fov, (float)width / (float)std::max(1u, height), std::max(0.001f, distance * 0.01f),
                                                distance * 4.0f + radius * 4.0f);

        SceneEnvironment env;
        env.DrawSky = false;
        env.HasSun = true;
        env.Sun.Direction = glm::normalize(glm::vec3(-0.45f, -1.0f, -0.55f));
        env.Sun.Intensity = 3.2f;
        env.Sun.CastShadows = false;
        env.AmbientIntensity = 0.9f;
        Renderer3D::BeginScene(view, projection, eye, env);
        const std::vector<glm::mat4>* joints = nullptr;
        if (model->IsSkinned())
        {
            const auto& clips = model->GetAnimations();
            const AnimationClip* clip = m_Clip >= 0 && m_Clip < (int)clips.size() ? &clips[m_Clip] : nullptr;
            EvaluatePose(model->GetSkeleton(), clip, clip ? m_ClipTime : 0.0f, m_Joints);
            joints = &m_Joints;
        }
        for (const Model::SubMesh& sub : model->GetSubMeshes())
            Renderer3D::SubmitMesh(sub.MeshRef, glm::mat4(1.0f), sub.Mat, -1, false, sub.MeshRef->IsSkinned() ? joints : nullptr);
        Renderer3D::EndScene();
        m_ModelTarget->Unbind();

        PostProcessSettings post;
        post.Tonemap = TonemapMode::ACES;
        PostProcessor::Apply(m_ModelTarget->GetColorAttachmentRendererID(0), width, height, post, m_ModelFinal);
        m_ModelFinal->Unbind(); // Apply leaves its target bound; ImGui draws into the window
        Renderer::GetStats() = stats;
    }

    void AssetInspectorPanel::DrawModelPreview(const Ref<Model>& model)
    {
        float dt = ImGui::GetIO().DeltaTime;
        const auto& clips = model->GetAnimations();
        if (m_Clip >= (int)clips.size())
        {
            m_Clip = -1;
            m_ClipPlaying = false;
        }
        if (m_ClipPlaying && m_Clip >= 0)
        {
            float duration = std::max(0.001f, clips[m_Clip].Duration);
            m_ClipTime = std::fmod(m_ClipTime + dt, duration);
        }
        if (m_AutoRotate && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_Yaw += dt * 0.6f;

        float availX = ImGui::GetContentRegionAvail().x;
        uint32_t width = (uint32_t)std::clamp(availX, 64.0f, 1024.0f);
        uint32_t height = (uint32_t)std::clamp(availX * 0.7f, 64.0f, 360.0f);
        RenderModelPreview(model, width, height);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(uintptr_t)m_ModelFinal->GetColorAttachmentRendererID(0), ImVec2((float)width, (float)height),
                     ImVec2(0, 1), ImVec2(1, 0));
        // Drag: orbit, wheel: zoom, double click: back to the start view.
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##modelView", ImVec2((float)width, (float)height));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_Yaw -= delta.x * 0.01f;
            m_Pitch = std::clamp(m_Pitch + delta.y * 0.01f, -1.45f, 1.45f);
        }
        if (ImGui::IsItemHovered())
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
                m_ZoomDistance = std::clamp(m_ZoomDistance * std::pow(0.88f, wheel), 0.2f, 5.0f);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_Yaw = 0.6f;
                m_Pitch = 0.35f;
                m_ZoomDistance = 1.0f;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                UI::Tooltip("Sudrang: aylantirish, g'ildirak: yaqinlashtirish, ikki marta bosing: boshlang'ich ko'rinish");
        }

        // Controls under the view.
        bool rotate = m_AutoRotate;
        if (rotate)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentU32(0.6f));
        if (ImGui::SmallButton(ICON_FA_ROTATE "##autoRotate"))
            m_AutoRotate = !m_AutoRotate;
        if (rotate)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Avtomatik aylantirish");
        if (!clips.empty())
        {
            ImGui::SameLine();
            bool playing = m_ClipPlaying && m_Clip >= 0;
            if (ImGui::SmallButton(playing ? ICON_FA_PAUSE "##clipPlay" : ICON_FA_PLAY "##clipPlay"))
            {
                if (m_Clip < 0)
                    m_Clip = 0;
                m_ClipPlaying = !playing;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.45f));
            const char* current = m_Clip >= 0 ? clips[m_Clip].Name.c_str() : "Bind pose";
            if (ImGui::BeginCombo("##clip", current))
            {
                if (ImGui::Selectable("Bind pose", m_Clip < 0))
                {
                    m_Clip = -1;
                    m_ClipPlaying = false;
                    m_ClipTime = 0.0f;
                }
                for (int i = 0; i < (int)clips.size(); i++)
                    if (ImGui::Selectable(std::format("{}##clip{}", clips[i].Name, i).c_str(), m_Clip == i))
                    {
                        m_Clip = i;
                        m_ClipTime = 0.0f;
                        m_ClipPlaying = true;
                    }
                ImGui::EndCombo();
            }
            if (m_Clip >= 0)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                float duration = std::max(0.001f, clips[m_Clip].Duration);
                if (ImGui::SliderFloat("##clipTime", &m_ClipTime, 0.0f, duration, "%.2f s"))
                    m_ClipPlaying = false;
            }
        }
    }

    void AssetInspectorPanel::DrawModelInfo(const Ref<Model>& model)
    {
        const ModelFileInfo& file = m_ModelInfo;
        uint64_t vertices = 0, triangles = 0;
        if (model)
            for (const Model::SubMesh& sub : model->GetSubMeshes())
            {
                vertices += sub.MeshRef->GetVertexCount();
                triangles += sub.MeshRef->GetTriangleCount();
            }

        if (UI::BeginPropertyGrid("##modelInfo"))
        {
            UI::PropertyHeader("Ma'lumot");
            UI::PropertyLabel("Format");
            if (file.Valid)
            {
                std::string format = file.Format + (file.Version.empty() ? "" : " " + file.Version);
                ImGui::TextUnformatted(format.c_str());
                if (!file.Generator.empty())
                {
                    UI::PropertyLabel("Eksportchi");
                    ImGui::TextWrapped("%s", file.Generator.c_str());
                }
            }
            else
                ImGui::TextColored(s_ErrorColor, "%s", file.Error.empty() ? "o'qilmadi" : file.Error.c_str());
            if (model)
            {
                UI::PropertyLabel("Submeshlar", "Chiziladigan qismlar (har biri o'z materiali bilan; bir mesh bir necha tugunda bo'lsa, har biri alohida).");
                ImGui::Text("%zu", model->GetSubMeshes().size());
                UI::PropertyLabel("Uchburchaklar");
                ImGui::TextUnformatted(FormatCount(triangles).c_str());
                UI::PropertyLabel("Uchlar (vertex)");
                ImGui::TextUnformatted(FormatCount(vertices).c_str());
                const AABB& b = model->GetBounds();
                glm::vec3 size = b.Max - b.Min;
                UI::PropertyLabel("O'lcham", "Import masshtabi bilan (metr).");
                ImGui::Text("%.3g x %.3g x %.3g", size.x, size.y, size.z);
                UI::PropertyLabel("Skelet");
                if (model->IsSkinned())
                    ImGui::Text("%zu ta bo'g'in", model->GetSkeleton().Joints.size());
                else
                    ImGui::TextDisabled("yo'q");
            }
            if (file.Valid && file.Format != "OBJ")
            {
                UI::PropertyLabel("Faylda");
                ImGui::TextWrapped("%u mesh, %zu primitiv, %zu material, %u rasm, %u tugun", file.Meshes, file.Primitives.size(),
                                   file.Materials.size(), file.Images, file.Nodes);
            }
            UI::EndPropertyGrid();
        }

        // ---- submeshes
        if (model && !model->GetSubMeshes().empty() &&
            ImGui::CollapsingHeader(std::format(ICON_FA_CUBES "  Submeshlar ({})###submeshes", model->GetSubMeshes().size()).c_str()))
        {
            if (ImGui::BeginTable("##submeshTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Nomi", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("Uchlar", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Uchburchak", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableHeadersRow();
                int i = 0;
                for (const Model::SubMesh& sub : model->GetSubMeshes())
                {
                    ImGui::PushID(i++);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(sub.Name.c_str());
                    if (sub.MeshRef->IsSkinned())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled(ICON_FA_BONE);
                        if (ImGui::IsItemHovered())
                            UI::Tooltip("Skeletga bog'langan (skinned)");
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(FormatCount(sub.MeshRef->GetVertexCount()).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(FormatCount(sub.MeshRef->GetTriangleCount()).c_str());
                    ImGui::TableSetColumnIndex(3);
                    const Material& m = sub.Mat;
                    ImGui::ColorButton("##albedo", ImVec4(m.Albedo.r, m.Albedo.g, m.Albedo.b, m.Albedo.a),
                                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(14, 14));
                    if (ImGui::IsItemHovered())
                    {
                        std::string maps;
                        if (m.AlbedoMap) maps += "\n  albedo: " + Project::GetAssetRelativePath(m.AlbedoMap->GetPath());
                        if (m.NormalMap) maps += "\n  normal: " + Project::GetAssetRelativePath(m.NormalMap->GetPath());
                        if (m.MetalRoughMap) maps += "\n  metal/roughness: " + Project::GetAssetRelativePath(m.MetalRoughMap->GetPath());
                        if (m.EmissiveMap) maps += "\n  emissive: " + Project::GetAssetRelativePath(m.EmissiveMap->GetPath());
                        UI::Tooltip(std::format("Rang {:.2f} {:.2f} {:.2f}  |  metallik {:.2f}, g'adir-budirlik {:.2f}{}{}", m.Albedo.r, m.Albedo.g,
                                                m.Albedo.b, m.Metallic, m.Roughness, maps.empty() ? "" : "\nXaritalar:", maps).c_str());
                    }
                    ImGui::SameLine();
                    int maps = (m.AlbedoMap ? 1 : 0) + (m.NormalMap ? 1 : 0) + (m.MetalRoughMap ? 1 : 0) + (m.EmissiveMap ? 1 : 0);
                    if (maps > 0)
                        ImGui::TextDisabled(ICON_FA_IMAGE " %d", maps);
                    else
                        ImGui::TextDisabled("rang");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        // ---- materials of the file
        if (file.Valid && !file.Materials.empty() &&
            ImGui::CollapsingHeader(std::format(ICON_FA_PALETTE "  Materiallar ({})###materials", file.Materials.size()).c_str()))
        {
            for (size_t i = 0; i < file.Materials.size(); i++)
            {
                const ModelFileInfo::MaterialInfo& m = file.Materials[i];
                ImGui::PushID((int)i);
                ImGui::ColorButton("##base", ImVec4(m.BaseColor.r, m.BaseColor.g, m.BaseColor.b, m.BaseColor.a),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(14, 14));
                ImGui::SameLine();
                ImGui::TextUnformatted(m.Name.c_str());
                std::string details = std::format("metallik {:.2f}, g'adir-budirlik {:.2f}", m.Metallic, m.Roughness);
                for (const std::string& map : m.Maps)
                    details += ", " + map;
                ImGui::SameLine();
                ImGui::TextDisabled("%s", details.c_str());
                ImGui::PopID();
            }
        }

        // ---- animations: the clips the Animator plays (and the file's other ones)
        size_t fileClips = file.Valid ? file.Animations.size() : 0;
        size_t clipCount = model ? model->GetAnimations().size() : 0;
        if ((clipCount > 0 || fileClips > 0) &&
            ImGui::CollapsingHeader(std::format(ICON_FA_PERSON_RUNNING "  Animatsiyalar ({})###animations", std::max(clipCount, fileClips)).c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("##clipTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                ImGui::TableSetupColumn("Nomi", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("Davomiyligi", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Kanallar", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableHeadersRow();
                for (int i = 0; model && i < (int)clipCount; i++)
                {
                    const AnimationClip& clip = model->GetAnimations()[i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    bool playing = m_Clip == i && m_ClipPlaying;
                    if (ImGui::SmallButton(playing ? ICON_FA_PAUSE : ICON_FA_PLAY))
                    {
                        if (m_Clip != i)
                            m_ClipTime = 0.0f;
                        m_Clip = i;
                        m_ClipPlaying = !playing;
                    }
                    ImGui::TableSetColumnIndex(1);
                    if (m_Clip == i)
                        ImGui::TextColored(UI::AccentColor(), "%s", clip.Name.c_str());
                    else
                        ImGui::TextUnformatted(clip.Name.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(AssetInfo::FormatDuration(clip.Duration).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%zu", clip.Channels.size());
                    ImGui::PopID();
                }
                // Clips that do not move a skeleton: listed, but the engine does not play them.
                for (const ModelFileInfo::Animation& a : file.Animations)
                {
                    if (a.TargetsJoints && model && model->FindAnimation(a.Name) >= 0)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s", a.Name.c_str());
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Skelet bo'g'inlarini harakatlantirmaydi - Animator faqat skelet animatsiyalarini o'ynatadi");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s", AssetInfo::FormatDuration(a.Duration).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextDisabled("%u", a.Channels);
                }
                ImGui::EndTable();
            }
        }

        // ---- skeleton
        if (model && model->IsSkinned() &&
            ImGui::CollapsingHeader(std::format(ICON_FA_BONE "  Skelet ({} bo'g'in)###skeleton", model->GetSkeleton().Joints.size()).c_str()))
        {
            const auto& joints = model->GetSkeleton().Joints;
            ImGui::BeginChild("##joints", ImVec2(0, std::min(220.0f, ImGui::GetTextLineHeightWithSpacing() * (float)joints.size() + 8.0f)),
                              ImGuiChildFlags_Borders);
            std::vector<int> depth(joints.size(), 0);
            for (size_t i = 0; i < joints.size(); i++)
            {
                if (joints[i].Parent >= 0 && joints[i].Parent < (int)i)
                    depth[i] = depth[joints[i].Parent] + 1;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth[i] * 12.0f);
                ImGui::TextUnformatted(joints[i].Name.c_str());
            }
            ImGui::EndChild();
        }

        // ---- files next to the model
        if (file.Valid && !file.ExternalFiles.empty() &&
            ImGui::CollapsingHeader(std::format(ICON_FA_FILE "  Bog'liq fayllar ({})###modelFiles", file.ExternalFiles.size()).c_str()))
        {
            for (const std::string& name : file.ExternalFiles)
            {
                bool missing = std::find(file.MissingFiles.begin(), file.MissingFiles.end(), name) != file.MissingFiles.end();
                if (missing)
                    ImGui::TextColored(s_ErrorColor, ICON_FA_CIRCLE_XMARK "  %s  (topilmadi)", name.c_str());
                else
                    ImGui::Text(ICON_FA_FILE "  %s", name.c_str());
            }
        }
    }

    void AssetInspectorPanel::DrawModel(const AssetMetadata* meta)
    {
        if (!m_ModelInfoRead)
        {
            m_ModelInfo = AssetInfo::ReadModel(m_Path);
            m_ModelInfoRead = true;
        }
        if (!m_ModelLoaded)
        {
            m_Model = Model::Load(m_Path); // cached: the scene's own model object (hot reload updates it)
            m_ModelLoaded = true;
        }
        ImGui::Spacing();
        if (m_Model && !m_Model->GetSubMeshes().empty())
            DrawModelPreview(m_Model);
        else
            ImGui::TextColored(s_ErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  Model yuklanmadi - konsolni tekshiring");
        ImGui::Spacing();
        DrawModelInfo(m_Model && !m_Model->GetSubMeshes().empty() ? m_Model : nullptr);

        ImGui::Spacing();
        bool canApply = meta != nullptr;
        if (UI::BeginPropertyGrid("##modelImport"))
        {
            UI::PropertyHeader("Import sozlamalari");
            UI::PropertyLabel("Masshtab", "Butun modelning o'lchami (1 = fayldagidek). Santimetrda saqlangan model uchun 0.01.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat("##scale", &m_ModelEdit.Scale, 0.01f, AssetImporter::MinModelScale, AssetImporter::MaxModelScale, "%.4g",
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            UI::PropertyLabel("");
            const std::pair<const char*, float> presets[] = { { "x1", 1.0f }, { "x0.01", 0.01f }, { "x100", 100.0f } };
            for (const auto& [label, value] : presets)
            {
                if (value != 1.0f)
                    ImGui::SameLine();
                if (ImGui::SmallButton(label))
                    m_ModelEdit.Scale = value;
                if (value == 0.01f && ImGui::IsItemHovered())
                    UI::Tooltip("Santimetr -> metr");
                else if (value == 100.0f && ImGui::IsItemHovered())
                    UI::Tooltip("Metr -> santimetr");
            }

            int axis = m_ModelEdit.UpAxis == ModelUpAxis::Z ? 1 : 0;
            const char* axisNames[] = { AssetImporter::GetUpAxisName(ModelUpAxis::Y), AssetImporter::GetUpAxisName(ModelUpAxis::Z) };
            UI::PropertyLabel("Yuqori o'q", "Fayl qaysi o'qni yuqori deb hisoblaydi. Z: CAD va ba'zi eksportchilar - model "
                                            "Y-yuqoriga aylantiriladi (skelet va animatsiyalar bilan).");
            if (ImGui::Combo("##upAxis", &axis, axisNames, 2))
                m_ModelEdit.UpAxis = axis == 1 ? ModelUpAxis::Z : ModelUpAxis::Y;

            UI::PropertyLabel("UV aylantirish", "V = 1 - V: teksturalar teskari (ostin-ustun) ko'rinsa yoqing.");
            ImGui::Checkbox("##flipUVs", &m_ModelEdit.FlipUVs);
            UI::EndPropertyGrid();
        }
        DrawApplyBar(canApply);
    }

    // ------------------------------------------------------------------ audio
    void AssetInspectorPanel::StopAudio()
    {
        if (m_Sound)
            AudioEngine::Stop(m_Sound);
        m_Sound = 0;
    }

    void AssetInspectorPanel::StartPreviewPlayback()
    {
        if (m_Type == AssetType::Audio)
        {
            StopAudio();
            m_Sound = AudioEngine::Play(m_Path, m_Volume, 1.0f, m_Loop);
            if (m_Sound)
                ZM_INFO("Ovoz ko'rib chiqilmoqda: {}", m_Path.filename().string());
        }
        else if (m_Type == AssetType::Model)
        {
            m_Clip = 0;
            m_ClipTime = 0.0f;
            m_ClipPlaying = true;
        }
    }

    void AssetInspectorPanel::DrawWaveform(float height)
    {
        ImVec2 size(ImGui::GetContentRegionAvail().x, height);
        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 max(min.x + size.x, min.y + size.y);
        ImGui::InvisibleButton("##waveform", size);
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(min, max, IM_COL32(24, 26, 30, 255), 4.0f);
        float mid = (min.y + max.y) * 0.5f;
        dl->AddLine(ImVec2(min.x, mid), ImVec2(max.x, mid), IM_COL32(60, 64, 72, 255));

        if (!m_Waveform && m_AudioInfo.Valid)
        {
            // Decoded on a thread: a long piece of music does not stall the editor.
            m_Waveform = std::make_shared<Waveform>();
            std::thread([waveform = m_Waveform, path = m_Path]() {
                waveform->Peaks = AssetInfo::ReadAudioWaveform(path, 512, &waveform->Error);
                waveform->Done = true;
            }).detach();
        }
        bool playing = m_Sound && AudioEngine::IsPlaying(m_Sound);
        float duration = (float)m_AudioInfo.Duration;
        float progress = playing && duration > 0.0f ? std::clamp(AudioEngine::GetCursor(m_Sound) / duration, 0.0f, 1.0f) : -1.0f;

        if (!m_Waveform || !m_Waveform->Done)
        {
            const char* text = m_AudioInfo.Valid ? "To'lqin shakli o'qilmoqda..." : "Ovoz o'qilmadi";
            ImVec2 ts = ImGui::CalcTextSize(text);
            dl->AddText(ImVec2(min.x + (size.x - ts.x) * 0.5f, mid - ts.y * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
        }
        else if (m_Waveform->Peaks.empty())
        {
            std::string text = "To'lqin shakli yo'q: " + m_Waveform->Error;
            dl->AddText(ImVec2(min.x + 8.0f, mid - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(s_ErrorColor), text.c_str());
        }
        else
        {
            const std::vector<glm::vec2>& peaks = m_Waveform->Peaks;
            float half = size.y * 0.45f;
            int columns = std::max(1, (int)size.x);
            for (int x = 0; x < columns; x++)
            {
                size_t from = (size_t)x * peaks.size() / columns, to = std::max(from + 1, (size_t)(x + 1) * peaks.size() / columns);
                glm::vec2 peak(1.0f, -1.0f);
                for (size_t i = from; i < to && i < peaks.size(); i++)
                {
                    peak.x = std::min(peak.x, peaks[i].x);
                    peak.y = std::max(peak.y, peaks[i].y);
                }
                bool played = progress >= 0.0f && (float)x / columns <= progress;
                ImU32 color = played ? UI::AccentU32(0.95f) : IM_COL32(150, 160, 175, 255);
                float y0 = mid - peak.y * half, y1 = mid - peak.x * half;
                dl->AddLine(ImVec2(min.x + x + 0.5f, y0), ImVec2(min.x + x + 0.5f, std::max(y1, y0 + 1.0f)), color);
            }
        }
        if (progress >= 0.0f)
        {
            float x = min.x + progress * size.x;
            dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 255, 255, 200), 1.5f);
        }
        // Position / length in the corner.
        std::string time = std::format("{} / {}", AssetInfo::FormatDuration(playing ? AudioEngine::GetCursor(m_Sound) : 0.0f),
                                       AssetInfo::FormatDuration(m_AudioInfo.Duration));
        ImVec2 timeSize = ImGui::CalcTextSize(time.c_str());
        ImVec2 timePos(max.x - timeSize.x - 6.0f, min.y + 3.0f);
        dl->AddRectFilled(ImVec2(timePos.x - 3.0f, timePos.y - 1.0f), ImVec2(max.x - 3.0f, timePos.y + timeSize.y + 1.0f), IM_COL32(24, 26, 30, 200), 3.0f);
        dl->AddText(timePos, ImGui::GetColorU32(ImGuiCol_TextDisabled), time.c_str());
        if (hovered && duration > 0.0f)
        {
            float t = std::clamp((ImGui::GetMousePos().x - min.x) / size.x, 0.0f, 1.0f) * duration;
            float x = min.x + t / duration * size.x;
            dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 255, 255, 70));
            UI::Tooltip(std::format("{}  (bosing: shu joydan ijro)", AssetInfo::FormatDuration(t)).c_str());
            if (clicked && AudioEngine::IsAvailable())
            {
                if (!playing)
                {
                    StopAudio();
                    m_Sound = AudioEngine::Play(m_Path, m_Volume, 1.0f, m_Loop);
                }
                if (m_Sound)
                    AudioEngine::Seek(m_Sound, t);
            }
        }
    }

    void AssetInspectorPanel::DrawAudio()
    {
        if (!m_AudioInfoRead)
        {
            m_AudioInfo = AssetInfo::ReadAudio(m_Path);
            m_AudioInfoRead = true;
        }
        const AudioFileInfo& info = m_AudioInfo;
        ImGui::Spacing();
        DrawWaveform(84.0f);

        // ---- play / stop
        bool available = AudioEngine::IsAvailable();
        bool playing = m_Sound && AudioEngine::IsPlaying(m_Sound);
        ImGui::BeginDisabled(!available || !info.Valid);
        if (playing)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentU32(0.75f));
        if (ImGui::Button(playing ? ICON_FA_STOP "  To'xtatish" : ICON_FA_PLAY "  Ijro etish", ImVec2(130, 0)))
        {
            if (playing)
                StopAudio();
            else
                StartPreviewPlayback();
        }
        if (playing)
            ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Checkbox(ICON_FA_REPEAT "##loop", &m_Loop) && playing)
        {
            // Applied by starting again from where it is.
            float at = AudioEngine::GetCursor(m_Sound);
            StopAudio();
            m_Sound = AudioEngine::Play(m_Path, m_Volume, 1.0f, m_Loop);
            AudioEngine::Seek(m_Sound, at);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Takrorlash");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##volume", &m_Volume, 0.0f, 1.0f, ICON_FA_VOLUME_HIGH " %.2f") && m_Sound)
            AudioEngine::SetVolume(m_Sound, m_Volume);
        ImGui::EndDisabled();
        if (!available)
            ImGui::TextColored(s_WarnColor, ICON_FA_CIRCLE_INFO "  Audio qurilmasi yo'q - ijro etib bo'lmaydi");

        // ---- what the file is
        ImGui::Spacing();
        if (UI::BeginPropertyGrid("##audioInfo"))
        {
            UI::PropertyHeader("Ma'lumot");
            if (info.Valid)
            {
                UI::PropertyLabel("Format");
                ImGui::Text("%s, %s", info.Format.c_str(), info.SampleFormat.c_str());
                UI::PropertyLabel("Davomiyligi");
                ImGui::TextUnformatted(AssetInfo::FormatDuration(info.Duration).c_str());
                UI::PropertyLabel("Kanallar");
                if (info.Channels == 1)
                    ImGui::TextUnformatted("1 (mono)");
                else if (info.Channels == 2)
                    ImGui::TextUnformatted("2 (stereo)");
                else
                    ImGui::Text("%u", info.Channels);
                UI::PropertyLabel("Chastota");
                ImGui::Text("%u Hz", info.SampleRate);
                UI::PropertyLabel("Namunalar", "Har bir kanal uchun audio kadrlar soni.");
                ImGui::TextUnformatted(FormatCount(info.Frames).c_str());
                std::error_code ec;
                uintmax_t bytes = fs::file_size(m_Path, ec);
                if (!ec && info.Duration > 0.0)
                {
                    UI::PropertyLabel("Bitreyt", "Fayl hajmi / davomiylik (o'rtacha).");
                    ImGui::Text("%.0f kbit/s", (double)bytes * 8.0 / info.Duration / 1000.0);
                }
                if (info.Channels > 1)
                {
                    UI::PropertyLabel("");
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped(ICON_FA_CIRCLE_INFO "  3D (fazoviy) ovoz uchun mono fayl yaxshiroq");
                    ImGui::PopStyleColor();
                }
            }
            else
            {
                UI::PropertyLabel("Xato");
                ImGui::TextColored(s_ErrorColor, "%s", info.Error.c_str());
            }
            UI::EndPropertyGrid();
        }
    }

    // ------------------------------------------------------------------ scenes, prefabs, fonts, scripts
    void AssetInspectorPanel::DrawSceneFile()
    {
        if (!m_Summary.Read)
        {
            m_Summary.Read = true;
            nlohmann::json root = nlohmann::json::parse(FileSystem::ReadTextFile(m_Path), nullptr, false);
            if (root.is_discarded() || !root.is_object())
                m_Summary.Error = "JSON o'qilmadi";
            else
            {
                m_Summary.Valid = true;
                std::map<std::string, size_t> components;
                if (auto it = root.find("Entities"); it != root.end() && it->is_array())
                    for (const auto& e : *it)
                    {
                        if (!e.is_object())
                            continue;
                        m_Summary.Entities++;
                        if (!e.contains("Parent"))
                            m_Summary.Roots.push_back(e.value("Tag", std::string("Entity")));
                        for (auto c = e.begin(); c != e.end(); ++c)
                            if (c.value().is_object())
                                components[c.key()]++;
                    }
                m_Summary.Components.assign(components.begin(), components.end());
                std::stable_sort(m_Summary.Components.begin(), m_Summary.Components.end(),
                                 [](const auto& a, const auto& b) { return a.second > b.second; });
            }
        }
        ImGui::Spacing();
        if (!m_Summary.Valid)
        {
            ImGui::TextColored(s_ErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s", m_Summary.Error.c_str());
            return;
        }
        if (UI::BeginPropertyGrid("##sceneInfo"))
        {
            UI::PropertyHeader("Ma'lumot");
            UI::PropertyLabel("Obyektlar");
            ImGui::Text("%zu", m_Summary.Entities);
            UI::PropertyLabel(m_Type == AssetType::Prefab ? "Ildiz" : "Ildiz obyektlar");
            std::string roots;
            for (size_t i = 0; i < m_Summary.Roots.size() && i < 12; i++)
                roots += (i ? ", " : "") + m_Summary.Roots[i];
            if (m_Summary.Roots.size() > 12)
                roots += std::format(" ... (+{})", m_Summary.Roots.size() - 12);
            ImGui::TextWrapped("%s", roots.empty() ? "-" : roots.c_str());
            UI::EndPropertyGrid();
        }
        if (!m_Summary.Components.empty() &&
            ImGui::CollapsingHeader(std::format(ICON_FA_LAYER_GROUP "  Komponentlar ({})###components", m_Summary.Components.size()).c_str()))
        {
            for (const auto& [key, count] : m_Summary.Components)
            {
                ImGui::Text("%s", SceneHierarchyPanel::GetComponentDisplayName(key));
                ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetContentRegionMax().x - 40.0f));
                ImGui::TextDisabled("%zu", count);
            }
        }
    }

    void AssetInspectorPanel::DrawFont()
    {
        if (!m_FontLoaded)
        {
            m_Font = Font::Load(m_Path); // cached (hot reload rebuilds it in place)
            m_FontLoaded = true;
        }
        const Ref<Font>& font = m_Font;
        ImGui::Spacing();
        if (!font || !font->GetAtlasTexture())
        {
            ImGui::TextColored(s_ErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  Shrift yuklanmadi - konsolni tekshiring");
            return;
        }
        const Ref<Texture2D>& atlas = font->GetAtlasTexture();
        float availX = ImGui::GetContentRegionAvail().x;
        float scale = std::min(availX / std::max(1.0f, (float)atlas->GetWidth()), 1.0f);
        ImVec2 size(atlas->GetWidth() * scale, atlas->GetHeight() * scale);
        ImVec2 min = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), IM_COL32(20, 22, 26, 255));
        ImGui::Image((ImTextureID)(uintptr_t)atlas->GetRendererID(), size, ImVec2(0, 1), ImVec2(1, 0));
        if (UI::BeginPropertyGrid("##fontInfo"))
        {
            UI::PropertyHeader("Ma'lumot");
            UI::PropertyLabel("Atlas");
            ImGui::Text("%u x %u px", atlas->GetWidth(), atlas->GetHeight());
            UI::PropertyLabel("Qator balandligi");
            ImGui::Text("%.3f", font->GetLineHeight());
            UI::PropertyLabel("Ascent / descent");
            ImGui::Text("%.3f / %.3f", font->GetAscent(), font->GetDescent());
            UI::EndPropertyGrid();
        }
    }

    void AssetInspectorPanel::DrawScript()
    {
        if (!m_Summary.Read)
        {
            m_Summary.Read = true;
            std::string code = FileSystem::ReadTextFile(m_Path);
            m_Summary.Valid = true;
            m_Summary.Lines = (size_t)std::count(code.begin(), code.end(), '\n') + (code.empty() || code.back() == '\n' ? 0 : 1);
            static const std::regex s_Register(R"(ZM_REGISTER_SCRIPT\s*\(\s*([A-Za-z_]\w*))");
            for (auto it = std::sregex_iterator(code.begin(), code.end(), s_Register); it != std::sregex_iterator(); ++it)
                m_Summary.Classes.push_back((*it)[1].str());
        }
        ImGui::Spacing();
        if (UI::BeginPropertyGrid("##scriptInfo"))
        {
            UI::PropertyHeader("Ma'lumot");
            UI::PropertyLabel("Qatorlar");
            ImGui::Text("%zu", m_Summary.Lines);
            UI::PropertyLabel("Skript klasslari", "Shu faylda ZM_REGISTER_SCRIPT bilan ro'yxatdan o'tgan klasslar.");
            if (m_Summary.Classes.empty())
                ImGui::TextDisabled("yo'q");
            else
            {
                for (const std::string& name : m_Summary.Classes)
                {
                    bool loaded = ScriptRegistry::Find(name) != nullptr;
                    ImGui::TextColored(loaded ? s_OkColor : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s %s",
                                       loaded ? ICON_FA_CIRCLE_CHECK : ICON_FA_CIRCLE, name.c_str());
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(loaded ? "Skript modulida yuklangan" : "Yuklanmagan (kompilyatsiya qiling)");
                }
            }
            UI::EndPropertyGrid();
        }
    }

    // ------------------------------------------------------------------ folders, several files
    void AssetInspectorPanel::DrawFolder()
    {
        FolderSummary& s = m_FolderSummary;
        if (s.Path != m_Path || ImGui::GetTime() - s.Time > 2.0)
        {
            s = {};
            s.Path = m_Path;
            s.Time = ImGui::GetTime();
            std::map<AssetType, size_t> types;
            std::error_code ec;
            for (fs::recursive_directory_iterator it(m_Path, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                std::error_code fec;
                std::string name = it->path().filename().string();
                bool directory = it->is_directory(fec);
                if (name.starts_with(".") || name.ends_with(".zmeta"))
                {
                    if (directory) it.disable_recursion_pending();
                    continue;
                }
                if (directory)
                {
                    s.Folders++;
                    continue;
                }
                s.Files++;
                uintmax_t size = it->file_size(fec);
                s.Size += fec ? 0 : size;
                types[AssetFiles::GetType(it->path())]++;
            }
            s.Types.assign(types.begin(), types.end());
            std::stable_sort(s.Types.begin(), s.Types.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        }
        ImGui::Spacing();
        if (UI::BeginPropertyGrid("##folderInfo"))
        {
            UI::PropertyHeader("Ichida");
            UI::PropertyLabel("Fayllar");
            ImGui::Text("%zu", s.Files);
            UI::PropertyLabel("Papkalar");
            ImGui::Text("%zu", s.Folders);
            UI::PropertyLabel("Jami hajm");
            ImGui::TextUnformatted(AssetFiles::FormatSize(s.Size).c_str());
            for (const auto& [type, count] : s.Types)
            {
                ImU32 color = 0;
                const char* icon = ContentBrowserPanel::GetTypeIcon(type, &color);
                UI::PropertyLabel(AssetFiles::GetTypeName(type));
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(icon);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::Text("%zu", count);
            }
            UI::EndPropertyGrid();
        }
    }

    void AssetInspectorPanel::DrawMultiSelection()
    {
        std::map<AssetType, size_t> types;
        uintmax_t total = 0;
        for (const fs::path& p : m_Selection)
        {
            std::error_code ec;
            bool directory = fs::is_directory(p, ec);
            types[AssetFiles::GetType(p, directory)]++;
            if (!directory)
            {
                uintmax_t size = fs::file_size(p, ec);
                total += ec ? 0 : size;
            }
        }
        ImGui::PushFont(ImGuiLayer::GetLargeIconFont());
        ImGui::TextColored(UI::AccentColor(), ICON_FA_LAYER_GROUP);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::Text("%zu ta element tanlangan", m_Selection.size());
        ImGui::PopFont();
        ImGui::TextDisabled("Jami %s (papkalarsiz)", AssetFiles::FormatSize(total).c_str());
        ImGui::EndGroup();
        ImGui::Separator();
        if (UI::BeginPropertyGrid("##multiInfo"))
        {
            for (const auto& [type, count] : types)
            {
                ImU32 color = 0;
                const char* icon = ContentBrowserPanel::GetTypeIcon(type, &color);
                UI::PropertyLabel(AssetFiles::GetTypeName(type));
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(icon);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::Text("%zu", count);
            }
            UI::EndPropertyGrid();
        }
        ImGui::Spacing();
        for (size_t i = 0; i < m_Selection.size() && i < 16; i++)
            ImGui::BulletText("%s", m_Selection[i].filename().string().c_str());
        if (m_Selection.size() > 16)
            ImGui::TextDisabled("... va yana %zu ta", m_Selection.size() - 16);
        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Bitta faylni tanlang - uning ko'rinishi va sozlamalari chiqadi");
    }

}
