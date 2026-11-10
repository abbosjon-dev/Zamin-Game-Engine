#include "Zamin/Scripting/Visual/VisualGraph.h"
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
        m_ClipSettingPreview = -1;
        m_Preview = nullptr;
        m_PreviewPath.clear();
        m_Model = nullptr;
        m_Material = nullptr;
        m_MaterialLoaded = false;
        m_MaterialView.Yaw = 0.5f;
        m_MaterialView.Pitch = 0.2f;
        m_MaterialView.Zoom = 1.0f;
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
        m_Material = nullptr;
        m_MaterialLoaded = false;
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
        m_AudioSaved = meta ? AssetImporter::ReadAudioSettings(meta->Import) : AudioClipSettings{};
        m_TextureEdit = m_TextureSaved;
        m_ModelEdit = m_ModelSaved;
        m_AudioEdit = m_AudioSaved;
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
            m_Material = nullptr; // looked up by the new path (the library moved it: the same object)
            m_MaterialLoaded = false;
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
        AudioClipSettings audio = AssetImporter::ReadAudioSettings(meta->Import);
        if (!(audio == m_AudioSaved))
        {
            if (m_AudioEdit == m_AudioSaved)
                m_AudioEdit = audio;
            m_AudioSaved = audio;
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
        m_WaveView.Reset();
        m_WaveDrag = WaveDrag::None;
        m_Summary = {};
    }

    // ------------------------------------------------------------------ apply / revert
    bool AssetInspectorPanel::HasUnappliedChanges() const
    {
        switch (m_Type)
        {
            case AssetType::Texture: return !(m_TextureEdit == m_TextureSaved);
            case AssetType::Model:   return !(m_ModelEdit == m_ModelSaved);
            case AssetType::Audio:   return !(m_AudioEdit == m_AudioSaved);
            case AssetType::Material: return HasUnsavedMaterial();
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
        edits.Audio = m_AudioEdit;
        edits.Material = m_Material;
        edits.MaterialSaved = m_MaterialSaved;
        edits.MaterialEdit = m_MaterialEdit;
        return edits;
    }

    void AssetInspectorPanel::KeepUnappliedEdits()
    {
        if (!HasUnappliedChanges() || (m_Type != AssetType::Material && !FindMetadata()))
            return;
        PendingEdits edits = CaptureEdits();
        auto same = [&](const PendingEdits& p) { return p.Handle == edits.Handle && p.Path == edits.Path; };
        m_Pending.erase(std::remove_if(m_Pending.begin(), m_Pending.end(), same), m_Pending.end());
        m_Pending.push_back(std::move(edits));
    }

    bool AssetInspectorPanel::ApplyEdits(const PendingEdits& edits, bool showStatus)
    {
        if (edits.Type == AssetType::Material)
        {
            // Materials keep their edits in the shared object; saving writes them to the .zmat.
            if (!edits.Material)
                return false;
            edits.Material->SetData(edits.MaterialEdit);
            bool saved = edits.Material->Save();
            std::string name = Project::GetAssetRelativePath(edits.Material->GetPath());
            if (showStatus)
            {
                m_StatusError = !saved;
                m_StatusTime = ImGui::GetTime();
                m_Status = saved ? "Saqlandi" : "Saqlanmadi - faylga yozib bo'lmadi";
            }
            if (saved)
                ZM_INFO("Material saqlandi: {}", name);
            else
                ZM_ERROR("Material saqlanmadi: {}", name);
            return saved;
        }
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
        else if (edits.Type == AssetType::Audio)
            AssetImporter::WriteAudioSettings(edits.Audio, import);
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
            else if (edits.Type == AssetType::Audio)
                m_Status = "Saqlandi (keyingi ijrodan boshlab ishlatiladi)";
            else
                m_Status = "Saqlandi (keyingi yuklashda ishlatiladi)";
        }
        ZM_INFO("Import sozlamalari saqlandi: {}{}", name, updated > 0 && !failed ? " (qayta import qilindi)" : "");
        return true;
    }

    bool AssetInspectorPanel::Apply()
    {
        if (m_Type == AssetType::Material)
            return HasUnsavedMaterial() && SaveMaterial();
        if (!HasUnappliedChanges() || !ApplyEdits(CaptureEdits(), true))
            return false;
        m_TextureSaved = m_TextureEdit;
        if (m_Type == AssetType::Model && m_ClipSettingPreview >= 0 && m_Model)
        {
            // The clip previewed is the model's own now: shown as one of its clips.
            const int index = m_Model->FindAnimation(m_ModelEdit.Clips[(size_t)m_ClipSettingPreview].GetName());
            m_ClipSettingPreview = -1;
            if (index >= 0)
                m_Clip = index;
        }
        m_ModelSaved = m_ModelEdit;
        m_AudioSaved = m_AudioEdit;
        m_ChannelTexture = nullptr; // made with the old sampling settings
        return true;
    }

    void AssetInspectorPanel::Revert()
    {
        m_ClipSettingPreview = -1;
        m_TextureEdit = m_TextureSaved;
        m_ModelEdit = m_ModelSaved;
        m_AudioEdit = m_AudioSaved;
        if (m_Type == AssetType::Material)
            RevertMaterial();
    }

    void AssetInspectorPanel::DrawPopups()
    {
        if (m_Pending.empty())
            return;
        if (!ImGui::IsPopupOpen("###UnappliedImport"))
            ImGui::OpenPopup("###UnappliedImport");
        const bool material = m_Pending.front().Type == AssetType::Material;
        std::string popupTitle = material ? Tr(ICON_FA_TRIANGLE_EXCLAMATION "  Saqlanmagan material###UnappliedImport")
                                          : Tr(ICON_FA_TRIANGLE_EXCLAMATION "  Qo'llanmagan import sozlamalari###UnappliedImport");
        if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const PendingEdits& edits = m_Pending.front();
            AssetRegistry* registry = GetRegistry();
            const AssetMetadata* meta = registry ? (edits.Handle ? registry->Find(edits.Handle) : registry->Find(edits.Path)) : nullptr;
            std::string name = meta ? meta->RelativePath : edits.Path.filename().string();
            if (material)
            {
                ImGui::Text("'%s' materiali o'zgartirilgan, lekin saqlanmagan.", name.c_str());
                ImGui::TextDisabled("Saqlansa .zmat fayliga yoziladi; tashlab yuborilsa saqlangan holiga qaytadi.");
            }
            else
            {
                ImGui::Text("'%s' import sozlamalari o'zgartirilgan, lekin qo'llanmagan.", name.c_str());
                ImGui::TextDisabled("Qo'llansa .zmeta fayliga yoziladi va asset darhol qayta import qilinadi.");
            }
            ImGui::Spacing();
            bool close = false;
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentU32(0.75f));
            if (ImGui::Button(material ? ICON_FA_FLOPPY_DISK "  Saqlash" : ICON_FA_CHECK "  Qo'llash", ImVec2(150, 0)))
            {
                if (!ApplyEdits(edits, false) && !material)
                    ZM_WARN("Import sozlamalari saqlanmadi: '{}' ro'yxatda yo'q", name);
                close = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button(Tr(ICON_FA_XMARK "  Tashlab yuborish"), ImVec2(170, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                if (material && edits.Material)
                    edits.Material->SetData(edits.MaterialSaved); // the scene shows the saved material again
                close = true;
            }
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
        DrawImportError();
        switch (m_Type)
        {
            case AssetType::Texture:     DrawTexture(meta); break;
            case AssetType::Environment: DrawTexture(nullptr); break;
            case AssetType::Model:       DrawModel(meta); break;
            case AssetType::Audio:       DrawAudio(meta); break;
            case AssetType::Scene:
            case AssetType::Prefab:      DrawSceneFile(); break;
            case AssetType::Font:        DrawFont(); break;
            case AssetType::VisualGraph:
                if(ImGui::Button("Visual Script muharririda ochish") && OnOpenVisualGraph)OnOpenVisualGraph(m_Path);
                try {auto graph=Visual::Graph::Load(m_Path);ImGui::Text("%zu nodes / %zu links / %zu variables",graph.Nodes.size(),graph.Links.size(),graph.Variables.size());for(auto&d:graph.Validate())ImGui::TextWrapped("Node %u: %s",d.Node,d.Message.c_str());}
                catch(const std::exception&e){ImGui::TextWrapped("%s",e.what());}
                break;
            case AssetType::Script:      DrawScript(); break;
            case AssetType::Material:    DrawMaterial(); break;
            case AssetType::PhysicsMaterial: DrawPhysicsMaterial(); break;
            case AssetType::Animation:   DrawAnimationClip(); break;
            case AssetType::SpriteAnimation: DrawSpriteClip(); break;
            default:
                ImGui::Spacing();
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Bu turdagi fayl uchun ko'rinish yo'q");
                break;
        }
        if (AssetTypes::IsRegistered(m_Type))
            DrawUsage();
    }

    void AssetInspectorPanel::DrawImportError()
    {
        uint64_t revision = AssetImportLog::GetRevision();
        if (revision != m_ImportErrorRevision || m_ImportErrorPath != m_Path)
        {
            m_ImportErrorRevision = revision;
            m_ImportErrorPath = m_Path;
            m_ImportError = AssetImportLog::Find(m_Path);
        }
        if (!m_ImportError)
            return;
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.42f, 0.12f, 0.1f, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.75f, 0.3f, 0.26f, 0.7f));
        ImGui::BeginChild("##importError", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextColored(s_ErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                           m_ImportError->Reload ? "Qayta import qilinmadi - avvalgi holati ishlatilmoqda" : "Import qilinmadi");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(m_ImportError->Reason.c_str());
        if (m_ImportError->Count > 1)
            ImGui::TextDisabled("%u marta yuklashga urinildi (konsolga bir marta yozildi)", m_ImportError->Count);
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton(Tr(ICON_FA_ARROWS_ROTATE "  Qayta urinish")))
        {
            AssetImportLog::OnFileChanged(m_Path); // loaders try the file again
            bool failed = false;
            if (AssetHotReload::CanReload(m_Path)) // also forgets a model that failed (loaded again below)
                AssetHotReload::ReloadAsset(m_Path, &failed);
            m_PreviewPath.clear();
            ResetPreviews();
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Faylni tuzatgandan keyin yana yuklab ko'rish");
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
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
        if (m_Type == AssetType::Material && HasUnsavedMaterial())
        {
            ImGui::SameLine();
            ImGui::TextColored(s_WarnColor, ICON_FA_CIRCLE " saqlanmagan");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Material o'zgartirilgan (sahnada ko'rinadi), lekin faylga saqlanmagan");
        }
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
            if (ImGui::SmallButton(Tr(ICON_FA_EARTH_ASIA "  Ochish")))
                OnOpenScene(m_Path);
        }
        if (m_Type == AssetType::Prefab && OnOpenPrefab)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr(ICON_FA_PEN_TO_SQUARE "  Prefab rejimida ochish")))
                OnOpenPrefab(m_Path);
            if (ImGui::IsItemHovered())
                UI::Tooltip(Tr("Prefabni alohida ochib tahrirlash (saqlanganda barcha nusxalari yangilanadi)"));
            if (OnCreatePrefabVariant)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(Tr(ICON_FA_CLONE "  Variant yaratish")))
                    OnCreatePrefabVariant(m_Path);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(Tr("Shu prefabdan meros oluvchi yangi prefab (prefab rejimida ochiladi)"));
            }
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
        std::string title = std::format(ICON_FA_LINK "  {} ({})###usedBy", Tr("Ishlatilgan joylar"), referrers.size());
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
                                      ImVec2(std::max(40.0f, ImGui::GetContentRegionAvail().x - 120.0f), 0)) && !r.Unsaved)
                {
                    if (!r.AssetPath.empty() && OnRevealRequested)
                        OnRevealRequested(r.File);
                    else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        Platform::OpenInFileBrowser(r.File);
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{}\n{} ta havola{}", r.Name, r.Count,
                                            r.Unsaved ? "\nTahrirlanayotgan sahna - hali faylga saqlanmagan"
                                            : r.AssetPath.empty() ? "\nIkki marta bosing: tizimda ochish" : "\nBosing: kontent brauzerda ko'rsatish").c_str());
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
            std::string usesTitle = std::format(ICON_FA_DIAGRAM_PROJECT "  {} ({})###uses", Tr("Ishlatadigan assetlar"), uses.size());
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
        if (ImGui::Button(Tr(ICON_FA_CHECK "  Qo'llash")))
            Apply();
        if (modified && canApply)
            ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Bekor qilish")))
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
        // (Decoded on the first hover only for images up to 2048 x 2048: a bigger one would stall.)
        if (ImGui::IsItemHovered() && (m_Pixels || width * height <= 2048.0f * 2048.0f))
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
        else if (!m_ImportError) // the import error box says why already
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

        // ---- sprite sheet: the named frames kept in the .zmeta (Sprite Editor)
        if (UI::BeginPropertyGrid("##textureSprites"))
        {
            UI::PropertyHeader(ICON_FA_IMAGES "  Sprite kadrlari", "Rasmning nomlangan kadrlari (atlas): Sprite Renderer ularni nomi bilan "
                                                                  "ko'rsatadi, Sprite Animation ketma-ketlik qilib o'ynaydi. Sprite muharririda "
                                                                  "kesiladi va .zmeta faylida saqlanadi.");
            SpriteSheet sheet = meta ? SpriteSheet::FromImportSettings(meta->Import) : SpriteSheet();
            UI::PropertyLabel("Kadrlar");
            if (sheet.IsEmpty())
                ImGui::TextDisabled("yo'q (butun rasm bitta sprite)");
            else
            {
                std::vector<std::string> prefixes = sheet.GetSequencePrefixes();
                std::string groups;
                for (size_t i = 0; i < prefixes.size() && i < 6; i++)
                    groups += (i ? ", " : "") + prefixes[i];
                if (prefixes.size() > 6)
                    groups += ", ...";
                ImGui::Text("%zu ta", sheet.Frames.size());
                if (!groups.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", groups.c_str());
                }
            }
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!meta || !OnOpenSpriteEditor);
            if (ImGui::Button(Tr(ICON_FA_SCISSORS "  Sprite muharriri"), ImVec2(-FLT_MIN, 0)) && OnOpenSpriteEditor)
                OnOpenSpriteEditor(m_Path);
            ImGui::EndDisabled();
            UI::EndPropertyGrid();
        }

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
            const AnimationClip* clip = m_ClipSettingPreview >= 0 ? &m_ClipSettingClip
                                      : m_Clip >= 0 && m_Clip < (int)clips.size() ? &clips[m_Clip] : nullptr;
            EvaluatePose(model->GetSkeleton(), clip, clip ? m_ClipTime : 0.0f, m_Joints);
            joints = &m_Joints;
        }
        std::vector<glm::mat4> palette;
        const auto& subs = model->GetSubMeshes();
        for (size_t s = 0; s < subs.size(); s++)
            Renderer3D::SubmitMesh(subs[s].MeshRef, glm::mat4(1.0f), subs[s].Mat, -1, false,
                                   subs[s].MeshRef->IsSkinned() && joints ? &model->GetSubMeshJoints(s, *joints, palette) : nullptr);
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
        if (m_ClipPlaying && m_ClipSettingPreview >= 0)
            m_ClipTime = std::fmod(m_ClipTime + dt, std::max(0.001f, m_ClipSettingClip.Duration));
        else if (m_ClipPlaying && m_Clip >= 0)
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
            const bool settingPreview = m_ClipSettingPreview >= 0;
            bool playing = m_ClipPlaying && (m_Clip >= 0 || settingPreview);
            if (ImGui::SmallButton(playing ? ICON_FA_PAUSE "##clipPlay" : ICON_FA_PLAY "##clipPlay"))
            {
                if (m_Clip < 0 && !settingPreview)
                    m_Clip = 0;
                m_ClipPlaying = !playing;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.45f));
            const std::string settingName = settingPreview ? std::format(ICON_FA_SCISSORS " {} (qo'llanmagan)", m_ClipSettingClip.Name) : std::string();
            const char* current = settingPreview ? settingName.c_str() : m_Clip >= 0 ? clips[m_Clip].Name.c_str() : "Bind pose";
            if (ImGui::BeginCombo("##clip", current))
            {
                m_ClipSettingPreview = -1; // a clip picked here: the model's own clips again
                if (ImGui::Selectable(Tr("Bind pose"), m_Clip < 0))
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
            if (m_Clip >= 0 || settingPreview)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                float duration = std::max(0.001f, settingPreview ? m_ClipSettingClip.Duration : clips[m_Clip].Duration);
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
                if (model->IsSkinned() && model->GetSkins().size() > 1)
                {
                    ImGui::Text("%zu ta bo'g'in, %zu skin", model->GetSkeleton().Joints.size(), model->GetSkins().size());
                    if (ImGui::IsItemHovered())
                    {
                        // Each skin: its joints and how it is drawn.
                        std::string text;
                        for (const ModelSkin& skin : model->GetSkins())
                            text += std::format("{}{}: {} suyak{}", text.empty() ? "" : "\n", skin.Name, skin.Joints.size(),
                                                skin.Direct ? "" : " (o'z palitrasi bilan)");
                        UI::Tooltip(text.c_str());
                    }
                }
                else if (model->IsSkinned())
                    ImGui::Text("%zu ta bo'g'in", model->GetSkeleton().Joints.size());
                else
                    ImGui::TextDisabled("yo'q");
                if (model->HasMorphTargets())
                {
                    UI::PropertyLabel("Blend shape'lar", "glTF morph target'lari (Mesh Renderer'da og'irliklari, Animator kliplari ham animatsiya qiladi)");
                    std::string names;
                    for (uint32_t i = 0; i < model->GetMorphWeightCount(); i++)
                        names += (i ? ", " : "") + model->GetMorphTargetName(i);
                    ImGui::TextWrapped("%u: %s", model->GetMorphWeightCount(), names.c_str());
                }
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

        // ---- materials of the file (and writing them out as .zmat assets)
        if (file.Valid && !file.Materials.empty() && (m_ExportOpenSection || m_ExportScrollTo))
        {
            ImGui::SetNextItemOpen(true);
            m_ExportOpenSection = false;
        }
        bool materialsOpen = file.Valid && !file.Materials.empty() &&
                             ImGui::CollapsingHeader(std::format(ICON_FA_PALETTE "  Materiallar ({})###materials", file.Materials.size()).c_str());
        if (materialsOpen && m_ExportScrollTo)
        {
            ImGui::SetScrollHereY(0.1f);
            m_ExportScrollTo = false;
        }
        if (materialsOpen)
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
            if (file.Format != "OBJ")
            {
                ImGui::Spacing();
                if (ImGui::Button(Tr(ICON_FA_FILE_EXPORT "  .zmat ga eksport qilish")))
                    ExportModelMaterials();
                if (ImGui::IsItemHovered())
                {
                    std::string folder = Project::GetAssetRelativePath(Model::GetDefaultMaterialExportDirectory(m_Path));
                    UI::Tooltip(std::format("Har bir material {} papkasiga .zmat fayl bo'lib yoziladi: ranglar, metall/g'adirlik, nurlanish, "
                                            "shaffoflik va teksturalar bilan. Fayl ichidagi (.glb, data URI) rasmlar ularning yoniga PNG/JPG bo'lib "
                                            "chiqariladi. Keyin ularni tahrirlash va boshqa obyektlarda ishlatish mumkin.", folder).c_str());
                }
                ImGui::SameLine();
                ImGui::Checkbox(Tr("Ustidan yozish"), &m_ExportOverwrite);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Oldingi eksportdan qolgan bir xil nomli fayllar qayta yoziladi (o'chirilsa: ular o'zgarmaydi va ishlatiladi)");

                if (m_ExportModel == m_Path)
                {
                    if (!m_ExportError.empty())
                        ImGui::TextColored(s_ErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s", m_ExportError.c_str());
                    else
                    {
                        size_t images = 0;
                        for (const fs::path& written : m_Export.Written)
                            if (AssetFiles::GetType(written) == AssetType::Texture)
                                images++;
                        size_t materials = m_Export.Written.size() - images;
                        std::string summary = std::format("{} ta material yozildi", materials);
                        if (images > 0)
                            summary += std::format(", {} ta rasm chiqarildi", images);
                        if (!m_Export.Kept.empty())
                            summary += std::format(", {} ta mavjud fayl o'zgarmadi", m_Export.Kept.size());
                        ImGui::TextColored(s_OkColor, ICON_FA_CIRCLE_CHECK "  %s", summary.c_str());
                        ImGui::TextDisabled(ICON_FA_FOLDER "  %s", Project::GetAssetRelativePath(m_Export.OutputDirectory).c_str());
                        if (!m_Export.Materials.empty() && OnRevealRequested)
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton(Tr(ICON_FA_ARROW_RIGHT "  Ko'rsatish")))
                                OnRevealRequested(m_Export.Materials.front());
                        }
                        for (const std::string& warning : m_Export.Warnings)
                            ImGui::TextColored(s_WarnColor, ICON_FA_TRIANGLE_EXCLAMATION "  %s", warning.c_str());
                    }
                }
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
                        if (m_Clip != i || m_ClipSettingPreview >= 0)
                            m_ClipTime = 0.0f;
                        m_Clip = i;
                        m_ClipSettingPreview = -1;
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
                    ImGui::Text("%zu", clip.Channels.size() + clip.MorphChannels.size());
                    if (!clip.MorphChannels.empty() && ImGui::IsItemHovered())
                        UI::Tooltip(std::format("{} suyak kanali, {} morph og'irlik kanali", clip.Channels.size(), clip.MorphChannels.size()).c_str());
                    ImGui::PopID();
                }
                // Clips that do not move a skeleton: listed, but the engine does not play them.
                for (const ModelFileInfo::Animation& a : file.Animations)
                {
                    if (model && model->FindAnimation(a.Name) >= 0)
                        continue;
                    if (std::any_of(m_ModelSaved.Clips.begin(), m_ModelSaved.Clips.end(), [&](const ModelClipSettings& c) { return c.Source == a.Name; }))
                        continue; // cut into sub-clips (its own not kept)
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

    bool AssetInspectorPanel::ExportModelMaterials()
    {
        if (m_Type != AssetType::Model)
            return false;
        ModelMaterialExportOptions options;
        options.Overwrite = m_ExportOverwrite;
        m_ExportModel = m_Path;
        m_ExportError.clear();
        m_ExportOpenSection = true;
        if (!Model::ExportMaterials(m_Path, options, m_Export, &m_ExportError))
        {
            ZM_ERROR("Model materiallari eksport qilinmadi: {} - {}", m_Path.filename().string(), m_ExportError);
            return false;
        }
        for (const std::string& warning : m_Export.Warnings)
            ZM_WARN("{}: {}", m_Path.filename().string(), warning);
        if (OnFilesCreated && !m_Export.Written.empty())
            OnFilesCreated(m_Export.Written);
        if (m_Usage)
            m_Usage->MarkDirty();
        return true;
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
        DrawModelClipSettings();
        DrawApplyBar(canApply);
    }

    // ------------------------------------------------------------------ audio
    void AssetInspectorPanel::StopAudio()
    {
        if (m_Sound)
            AudioEngine::Stop(m_Sound);
        m_Sound = 0;
        m_PreviewLoop = glm::vec2(-1.0f);
    }

    uint32_t AssetInspectorPanel::PlayPreviewSound()
    {
        // Past the mixer: the game's group volumes / mutes do not change how the file sounds here.
        SoundParams params;
        params.Volume = m_Volume;
        params.Loop = m_Loop;
        params.Group.clear();
        const uint32_t sound = AudioEngine::Play(m_Path, params);
        // The loop region as edited (it started with the applied one).
        if (sound)
        {
            m_PreviewLoop = GetEditedLoopRegion();
            AudioEngine::SetLoopRegion(sound, m_PreviewLoop.x, m_PreviewLoop.y);
        }
        return sound;
    }

    void AssetInspectorPanel::StartPreviewPlayback()
    {
        if (m_Type == AssetType::Audio)
        {
            StopAudio();
            m_Sound = PlayPreviewSound();
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

    glm::vec2 AssetInspectorPanel::GetEditedLoopRegion() const
    {
        if (!m_AudioEdit.HasLoopRegion())
            return glm::vec2(0.0f);
        float start = 0.0f, end = 0.0f;
        m_AudioEdit.GetLoopRegion((float)m_AudioInfo.Duration, start, end);
        return { start, end };
    }

    void AssetInspectorPanel::ApplyPreviewLoop()
    {
        // The preview loops inside the region as edited (applied or not): what Apply would give.
        if (!m_Sound)
            return;
        const glm::vec2 region = GetEditedLoopRegion();
        if (region == m_PreviewLoop)
            return;
        AudioEngine::SetLoopRegion(m_Sound, region.x, region.y);
        m_PreviewLoop = region;
    }

    void AssetInspectorPanel::ZoomWaveform(double factor, double center)
    {
        m_WaveView.Reset();
        m_WaveView.ZoomAt(0.5, factor);
        m_WaveView.CenterOn(center);
    }

    // A ruler label for `seconds` with ticks `step` apart: no more digits than the step needs.
    static std::string FormatRulerTime(double seconds, double step)
    {
        seconds = std::max(0.0, seconds); // never "-0s" (a -0.0 or a rounding error at the start)
        if (step >= 60.0 || seconds >= 60.0)
        {
            const int minutes = (int)(seconds / 60.0);
            const double rest = seconds - minutes * 60.0;
            return step >= 1.0 ? std::format("{}:{:02.0f}", minutes, std::floor(rest + 1e-6)) : std::format("{}:{:04.1f}", minutes, rest);
        }
        if (step >= 1.0)
            return std::format("{:.0f}s", seconds);
        if (step >= 0.1)
            return std::format("{:.1f}s", seconds);
        if (step >= 0.01)
            return std::format("{:.2f}s", seconds);
        return std::format("{:.0f}ms", seconds * 1000.0);
    }

    void AssetInspectorPanel::DrawWaveform(float height)
    {
        ImGuiIO& io = ImGui::GetIO();
        const float duration = m_AudioInfo.Valid ? (float)m_AudioInfo.Duration : 0.0f;
        if (!m_Waveform && m_AudioInfo.Valid)
        {
            // Decoded on a thread (a long piece of music does not stall the editor), fine enough
            // to zoom in.
            m_Waveform = std::make_shared<Waveform>();
            const uint32_t buckets = WaveformView::GetBucketCount(m_AudioInfo.Frames);
            std::thread([waveform = m_Waveform, path = m_Path, buckets]() {
                waveform->Peaks = AssetInfo::ReadAudioWaveform(path, buckets, &waveform->Error);
                waveform->Done = true;
            }).detach();
        }
        const bool ready = m_Waveform && m_Waveform->Done && !m_Waveform->Peaks.empty() && duration > 0.0f;
        const std::vector<glm::vec2>* peaks = ready ? &m_Waveform->Peaks : nullptr;
        // The narrowest view: ~40 peaks across (about a millisecond each for short sounds).
        if (peaks)
            m_WaveView.MinSpan = std::min(1.0, 40.0 / (double)peaks->size());
        m_WaveView.Clamp();

        const float width = std::max(32.0f, ImGui::GetContentRegionAvail().x);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + height);
        ImGui::InvisibleButton("##waveform", ImVec2(width, height),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        // The wheel zooms / scrolls the waveform here, not the inspector.
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        auto xOf = [&](double fraction) { return min.x + (float)(m_WaveView.ToView(fraction) * width); };
        auto fractionAt = [&](float x) { return std::clamp(m_WaveView.ToSound(std::clamp((double)(x - min.x) / width, 0.0, 1.0)), 0.0, 1.0); };
        const double mouseFraction = fractionAt(io.MousePos.x);
        const float mouseTime = (float)mouseFraction * duration;
        const bool available = AudioEngine::IsAvailable();
        constexpr float minLoop = 0.01f; // seconds
        auto snap = [](float t) { return std::round(t * 1000.0f) / 1000.0f; }; // whole milliseconds

        bool hasLoop = m_AudioEdit.HasLoopRegion();
        glm::vec2 loop = GetEditedLoopRegion();
        float loopStartX = duration > 0.0f ? xOf(loop.x / duration) : min.x;
        float loopEndX = duration > 0.0f ? xOf(loop.y / duration) : max.x;
        const float grab = 5.0f;
        const bool overStart = hovered && hasLoop && std::abs(io.MousePos.x - loopStartX) <= grab;
        const bool overEnd = hovered && hasLoop && !overStart && std::abs(io.MousePos.x - loopEndX) <= grab;

        // ---- input
        if (hovered && ready)
        {
            // Wheel: zoom around the mouse. Shift + wheel (or a sideways wheel): scroll.
            if (io.MouseWheel != 0.0f && !io.KeyShift)
                m_WaveView.ZoomAt(mouseFraction, std::pow(1.25, (double)io.MouseWheel));
            const float sideways = io.MouseWheelH + (io.KeyShift ? io.MouseWheel : 0.0f);
            if (sideways != 0.0f)
                m_WaveView.Pan(-(double)sideways * 0.1 * m_WaveView.GetSpan());
        }
        if (ready && ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            m_WaveDragAnchor = mouseFraction;
            m_ScrubTime = -1.0f;
            if (overStart)
                m_WaveDrag = WaveDrag::LoopStart;
            else if (overEnd)
                m_WaveDrag = WaveDrag::LoopEnd;
            else if (io.KeyShift)
                m_WaveDrag = WaveDrag::NewLoop;
            else
                m_WaveDrag = WaveDrag::Scrub;
        }
        if (ready && ImGui::IsItemClicked(ImGuiMouseButton_Middle))
        {
            m_WaveDrag = WaveDrag::Pan;
            m_WaveDragAnchor = mouseFraction;
        }
        if (!active)
            m_WaveDrag = WaveDrag::None;
        if (active && ready)
        {
            // Dragging past an edge of a zoomed view scrolls it.
            const bool edgeScroll = m_WaveDrag != WaveDrag::Pan && m_WaveDrag != WaveDrag::None;
            if (edgeScroll && io.MousePos.x < min.x)
                m_WaveView.Pan(-m_WaveView.GetSpan() * 0.02);
            else if (edgeScroll && io.MousePos.x > max.x)
                m_WaveView.Pan(m_WaveView.GetSpan() * 0.02);
            switch (m_WaveDrag)
            {
                case WaveDrag::Scrub:
                    // Scrubbing: the preview plays from under the mouse and jumps along with it.
                    if (available && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        if (!m_Sound || !AudioEngine::IsPlaying(m_Sound))
                        {
                            StopAudio();
                            m_Sound = PlayPreviewSound();
                            m_ScrubTime = -1.0f;
                        }
                        if (m_Sound && std::abs(mouseTime - m_ScrubTime) > 0.004f)
                        {
                            AudioEngine::Seek(m_Sound, mouseTime);
                            m_ScrubTime = mouseTime;
                        }
                    }
                    break;
                case WaveDrag::LoopStart:
                {
                    float t = snap(std::clamp(mouseTime, 0.0f, loop.y - minLoop));
                    m_AudioEdit.LoopStart = std::max(0.0f, t);
                    if (m_AudioEdit.LoopEnd <= 0.0f && m_AudioEdit.LoopStart <= 0.0f)
                        m_AudioEdit.LoopStart = 0.0f; // all of it again
                    break;
                }
                case WaveDrag::LoopEnd:
                {
                    float t = snap(std::clamp(mouseTime, loop.x + minLoop, duration));
                    m_AudioEdit.LoopEnd = t >= duration - 0.0005f ? 0.0f : t; // the file's end: 0
                    break;
                }
                case WaveDrag::NewLoop:
                {
                    float a = snap((float)m_WaveDragAnchor * duration), b = snap(mouseTime);
                    if (std::abs(b - a) >= minLoop)
                    {
                        m_AudioEdit.LoopStart = std::min(a, b);
                        float end = std::max(a, b);
                        m_AudioEdit.LoopEnd = end >= duration - 0.0005f ? 0.0f : end;
                    }
                    break;
                }
                case WaveDrag::Pan:
                    // The point grabbed stays under the mouse.
                    m_WaveView.Pan(m_WaveDragAnchor - mouseFraction);
                    break;
                case WaveDrag::None:
                    break;
            }
        }
        if (hovered && ready && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            m_WaveMenuTime = snap(mouseTime);
            ImGui::OpenPopup("##waveformMenu");
        }
        if (ImGui::BeginPopup("##waveformMenu"))
        {
            ImGui::TextDisabled("%s", AssetInfo::FormatDuration(m_WaveMenuTime).c_str());
            if (ImGui::MenuItem(Tr(ICON_FA_PLAY "  Shu joydan ijro"), nullptr, false, available))
            {
                StopAudio();
                m_Sound = PlayPreviewSound();
                if (m_Sound)
                    AudioEngine::Seek(m_Sound, m_WaveMenuTime);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_REPEAT "  Takrorlash boshi shu yerda")))
            {
                m_AudioEdit.LoopStart = m_WaveMenuTime;
                if (m_AudioEdit.LoopEnd > 0.0f && m_AudioEdit.LoopEnd <= m_WaveMenuTime + minLoop)
                    m_AudioEdit.LoopEnd = 0.0f;
            }
            if (ImGui::MenuItem(Tr(ICON_FA_REPEAT "  Takrorlash oxiri shu yerda"), nullptr, false, m_WaveMenuTime > minLoop))
            {
                m_AudioEdit.LoopEnd = m_WaveMenuTime >= duration - 0.0005f ? 0.0f : m_WaveMenuTime;
                if (m_AudioEdit.LoopStart >= m_WaveMenuTime - minLoop)
                    m_AudioEdit.LoopStart = 0.0f;
            }
            if (ImGui::MenuItem(Tr(ICON_FA_XMARK "  Takrorlash oralig'ini olib tashlash"), nullptr, false, hasLoop))
                m_AudioEdit.LoopStart = m_AudioEdit.LoopEnd = 0.0f;
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_MAGNIFYING_GLASS "  Oraliqni kattalashtirish"), nullptr, false, hasLoop && duration > 0.0f))
            {
                const double pad = (double)(loop.y - loop.x) / duration * 0.08;
                m_WaveView.Start = loop.x / duration - pad;
                m_WaveView.End = loop.y / duration + pad;
                m_WaveView.Clamp();
            }
            if (ImGui::MenuItem(Tr(ICON_FA_EXPAND "  Butun ovoz"), nullptr, false, m_WaveView.IsZoomed()))
                m_WaveView.Reset();
            ImGui::EndPopup();
        }
        m_AudioEdit.Sanitize();
        hasLoop = m_AudioEdit.HasLoopRegion();
        loop = GetEditedLoopRegion();
        loopStartX = duration > 0.0f ? xOf(loop.x / duration) : min.x;
        loopEndX = duration > 0.0f ? xOf(loop.y / duration) : max.x;
        ApplyPreviewLoop();

        // Follows the playing cursor in a zoomed view (a page at a time).
        const bool playing = m_Sound && AudioEngine::IsPlaying(m_Sound);
        const float cursorTime = playing ? AudioEngine::GetCursor(m_Sound) : 0.0f;
        const double progress = playing && duration > 0.0f ? std::clamp((double)cursorTime / duration, 0.0, 1.0) : -1.0;
        if (progress >= 0.0 && m_WaveDrag == WaveDrag::None && m_WaveView.IsZoomed() &&
            (progress < m_WaveView.Start || progress > m_WaveView.End))
            m_WaveView.Pan(progress - m_WaveView.Start - m_WaveView.GetSpan() * 0.05);

        // ---- drawing
        dl->PushClipRect(min, max, true);
        dl->AddRectFilled(min, max, IM_COL32(24, 26, 30, 255), 4.0f);
        const float mid = (min.y + max.y) * 0.5f;
        dl->AddLine(ImVec2(min.x, mid), ImVec2(max.x, mid), IM_COL32(60, 64, 72, 255));
        if (hasLoop)
        {
            // The loop region: shaded, the intro before it a little darker.
            dl->AddRectFilled(ImVec2(loopStartX, min.y), ImVec2(loopEndX, max.y), UI::AccentU32(0.13f));
            if (loop.x > 0.0f)
                dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(loopStartX, max.y), IM_COL32(0, 0, 0, 50));
        }
        if (!m_Waveform || !m_Waveform->Done)
        {
            const char* text = m_AudioInfo.Valid ? "To'lqin shakli o'qilmoqda..." : "Ovoz o'qilmadi";
            ImVec2 ts = ImGui::CalcTextSize(text);
            dl->AddText(ImVec2(min.x + (width - ts.x) * 0.5f, mid - ts.y * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
        }
        else if (!peaks)
        {
            std::string text = "To'lqin shakli yo'q: " + m_Waveform->Error;
            dl->AddText(ImVec2(min.x + 8.0f, mid - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(s_ErrorColor), text.c_str());
        }
        else
        {
            const double count = (double)peaks->size();
            const float half = height * 0.42f;
            const int columns = std::max(1, (int)width);
            for (int x = 0; x < columns; x++)
            {
                const double from = m_WaveView.ToSound((double)x / columns), to = m_WaveView.ToSound((double)(x + 1) / columns);
                const glm::vec2 peak = WaveformView::GetPeak(*peaks, from * count, to * count);
                const bool played = progress >= 0.0 && from <= progress;
                const bool inLoop = !hasLoop || (from * duration >= loop.x - 1e-4 && from * duration <= loop.y + 1e-4);
                ImU32 color = played ? UI::AccentU32(0.95f) : inLoop ? IM_COL32(150, 160, 175, 255) : IM_COL32(105, 112, 124, 255);
                const float y0 = mid - peak.y * half, y1 = mid - peak.x * half;
                dl->AddLine(ImVec2(min.x + x + 0.5f, y0), ImVec2(min.x + x + 0.5f, std::max(y1, y0 + 1.0f)), color);
            }

            // Time ruler along the bottom edge.
            const double visible = m_WaveView.GetSpan() * duration;
            const double step = WaveformView::GetTickStep(visible, width, 64.0);
            const double first = std::max(0.0, std::ceil(m_WaveView.Start * duration / step - 1e-9) * step); // +0.0, not -0.0
            for (double t = first; t <= m_WaveView.End * duration + 1e-9; t += step)
            {
                const float x = std::round(xOf(t / duration)) + 0.5f;
                dl->AddLine(ImVec2(x, max.y - 5.0f), ImVec2(x, max.y), IM_COL32(255, 255, 255, 90));
                const std::string label = FormatRulerTime(t, step);
                dl->AddText(ImVec2(x + 3.0f, max.y - ImGui::GetFontSize() - 2.0f), IM_COL32(255, 255, 255, 85), label.c_str());
            }
            // Smaller ticks in between.
            for (double t = first - step; t <= m_WaveView.End * duration + 1e-9; t += step * 0.5)
                if (t >= m_WaveView.Start * duration)
                {
                    const float x = std::round(xOf(t / duration)) + 0.5f;
                    dl->AddLine(ImVec2(x, max.y - 3.0f), ImVec2(x, max.y), IM_COL32(255, 255, 255, 50));
                }
        }
        if (hasLoop && ready)
        {
            // Its edges: handles to drag (a flag at the top of each).
            for (int edge = 0; edge < 2; edge++)
            {
                const float x = std::round(edge == 0 ? loopStartX : loopEndX) + 0.5f;
                const bool hot = (edge == 0 ? overStart : overEnd) || m_WaveDrag == (edge == 0 ? WaveDrag::LoopStart : WaveDrag::LoopEnd);
                const ImU32 color = UI::AccentU32(hot ? 1.0f : 0.8f);
                dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), color, hot ? 2.0f : 1.5f);
                const float dir = edge == 0 ? 1.0f : -1.0f;
                dl->AddTriangleFilled(ImVec2(x, min.y), ImVec2(x + dir * 9.0f, min.y), ImVec2(x, min.y + 9.0f), color);
            }
            // Its name, where it does not run into the time in the corner.
            const char* label = ICON_FA_REPEAT " takrorlash";
            const ImVec2 size = ImGui::CalcTextSize(label);
            const float labelX = std::max(loopStartX, min.x) + 12.0f;
            const float timeWidth = ImGui::CalcTextSize("x00.0   00.000 s / 00.000 s").x + 12.0f;
            if (loopEndX - loopStartX > size.x + 24.0f && labelX + size.x < max.x - timeWidth)
                dl->AddText(ImVec2(labelX, min.y + 2.0f), UI::AccentU32(0.85f), label);
        }
        if (progress >= 0.0)
        {
            const float x = xOf(progress);
            dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 255, 255, 220), 1.5f);
        }
        // Position / length (and the zoom) in the corner.
        std::string time = std::format("{} / {}", AssetInfo::FormatDuration(cursorTime), AssetInfo::FormatDuration(duration));
        if (m_WaveView.IsZoomed())
        {
            const double zoom = 1.0 / m_WaveView.GetSpan();
            time = (zoom < 10.0 ? std::format("x{:.1f}   ", zoom) : std::format("x{:.0f}   ", zoom)) + time;
        }
        const ImVec2 timeSize = ImGui::CalcTextSize(time.c_str());
        const ImVec2 timePos(max.x - timeSize.x - 6.0f, min.y + 3.0f);
        dl->AddRectFilled(ImVec2(timePos.x - 3.0f, timePos.y - 1.0f), ImVec2(max.x - 3.0f, timePos.y + timeSize.y + 1.0f), IM_COL32(24, 26, 30, 200), 3.0f);
        dl->AddText(timePos, ImGui::GetColorU32(ImGuiCol_TextDisabled), time.c_str());
        if (hovered && ready)
        {
            const float x = xOf(mouseFraction);
            dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 255, 255, 70));
            if (overStart || overEnd || m_WaveDrag == WaveDrag::LoopStart || m_WaveDrag == WaveDrag::LoopEnd)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (overStart || m_WaveDrag == WaveDrag::LoopStart)
                UI::Tooltip(std::format("Takrorlash boshi: {} (sudrang)", AssetInfo::FormatDuration(loop.x)).c_str());
            else if (overEnd || m_WaveDrag == WaveDrag::LoopEnd)
                UI::Tooltip(std::format("Takrorlash oxiri: {} (sudrang)", AssetInfo::FormatDuration(loop.y)).c_str());
            else if (m_WaveDrag == WaveDrag::None)
                UI::Tooltip(std::format("{}\nBosing / sudrang: shu joydan ijro (scrub)\nShift + sudrash: takrorlash oralig'i\n"
                                        "G'ildirak: kattalashtirish, Shift + g'ildirak yoki o'rta tugma: surish\nO'ng tugma: menyu",
                                        AssetInfo::FormatDuration(mouseTime)).c_str());
        }
        dl->PopClipRect();

        // ---- the whole sound under a zoomed view: where the view is (click / drag to move it)
        if (peaks && m_WaveView.IsZoomed())
        {
            const float stripHeight = 14.0f;
            const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + width, a.y + stripHeight);
            ImGui::InvisibleButton("##waveOverview", ImVec2(width, stripHeight));
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_WaveView.CenterOn(std::clamp((double)(io.MousePos.x - a.x) / width, 0.0, 1.0));
            if (ImGui::IsItemHovered())
                UI::Tooltip("Butun ovoz: bosing yoki sudrang - ko'rinish shu joyga o'tadi");
            dl->AddRectFilled(a, b, IM_COL32(18, 20, 24, 255), 3.0f);
            const double count = (double)peaks->size();
            const float stripMid = (a.y + b.y) * 0.5f, stripHalf = stripHeight * 0.45f;
            for (int x = 0; x < (int)width; x += 2)
            {
                const glm::vec2 peak = WaveformView::GetPeak(*peaks, (double)x / width * count, (double)(x + 2) / width * count);
                dl->AddLine(ImVec2(a.x + x + 0.5f, stripMid - peak.y * stripHalf), ImVec2(a.x + x + 0.5f, stripMid - peak.x * stripHalf + 1.0f),
                            IM_COL32(110, 118, 130, 255));
            }
            if (hasLoop)
                dl->AddRectFilled(ImVec2(a.x + loop.x / duration * width, a.y), ImVec2(a.x + loop.y / duration * width, b.y), UI::AccentU32(0.15f));
            const ImVec2 v0(a.x + (float)m_WaveView.Start * width, a.y), v1(a.x + (float)m_WaveView.End * width, b.y);
            dl->AddRectFilled(v0, v1, IM_COL32(255, 255, 255, 30));
            dl->AddRect(v0, ImVec2(std::max(v1.x, v0.x + 2.0f), v1.y), IM_COL32(255, 255, 255, 150), 2.0f);
            if (progress >= 0.0)
                dl->AddLine(ImVec2(a.x + (float)progress * width, a.y), ImVec2(a.x + (float)progress * width, b.y), IM_COL32(255, 255, 255, 220));
        }
    }

    void AssetInspectorPanel::DrawAudio(const AssetMetadata* meta)
    {
        if (!m_AudioInfoRead)
        {
            m_AudioInfo = AssetInfo::ReadAudio(m_Path);
            m_AudioInfoRead = true;
        }
        const AudioFileInfo& info = m_AudioInfo;
        ImGui::Spacing();
        DrawWaveform(std::floor(ImGui::GetFontSize() * 6.5f));

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
            m_Sound = PlayPreviewSound();
            AudioEngine::Seek(m_Sound, at);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(m_AudioEdit.HasLoopRegion() ? "Takrorlash (intro, keyin takrorlash oralig'i)" : "Takrorlash");
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

                // How it is read with the settings below (as edited), and what that costs.
                const AudioSettings& mixer = AudioEngine::GetMixerSettings();
                const double megabytes = ec ? 0.0 : (double)bytes / (1024.0 * 1024.0);
                const bool streams = m_AudioEdit.LoadMode == AudioLoadMode::Stream ||
                                     (m_AudioEdit.LoadMode == AudioLoadMode::Auto && megabytes > mixer.StreamThresholdMB);
                UI::PropertyLabel("Yuklanishi", "Ijro etilganda fayl qanday o'qiladi (pastdagi import sozlamasi bo'yicha). Audio Source'ning "
                                                "Streaming belgisi va musiqa (Audio::PlayMusic) har doim oqim bilan o'qiladi.");
                if (streams)
                {
                    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), ICON_FA_WAVE_SQUARE "  Diskdan oqim");
                    if (m_AudioEdit.LoadMode == AudioLoadMode::Auto)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%.1f MB > %.1f MB chegara)", megabytes, mixer.StreamThresholdMB);
                    }
                }
                else
                {
                    ImGui::TextColored(s_OkColor, ICON_FA_MEMORY "  Xotiraga yechiladi");
                    UI::PropertyLabel("Xotirada", "Yechilgan ovoz (32-bit float, dvigatel chastotasida) egallaydigan joy - har ijroda "
                                                  "(oldindan yuklansa - bir marta)");
                    uint64_t decoded = AudioClip::EstimateDecodedSize(info, AudioEngine::GetSampleRate());
                    ImGui::Text("~%.2f MB", (double)decoded / (1024.0 * 1024.0));
                }
                if (AudioEngine::IsPreloaded(m_Path))
                {
                    UI::PropertyLabel("Holat");
                    ImGui::TextColored(s_OkColor, ICON_FA_MEMORY "  Oldindan yuklangan (xotirada)");
                }
            }
            else
            {
                UI::PropertyLabel("Xato");
                ImGui::TextColored(s_ErrorColor, "%s", info.Error.c_str());
            }
            UI::EndPropertyGrid();
        }

        // ---- import settings (the .zmeta)
        ImGui::Spacing();
        if (UI::BeginPropertyGrid("##audioImport"))
        {
            UI::PropertyHeader("Import sozlamalari");
            int mode = (int)m_AudioEdit.LoadMode;
            const char* modes[] = { AssetImporter::GetLoadModeName(AudioLoadMode::Auto), AssetImporter::GetLoadModeName(AudioLoadMode::Decompress),
                                    AssetImporter::GetLoadModeName(AudioLoadMode::Stream) };
            UI::PropertyLabel("Yuklash usuli", "Avtomatik: xotiraga yechiladi, loyiha chegarasidan (Audio Mixer > Sozlamalar) katta fayl "
                                               "diskdan oqim bilan o'qiladi. Xotiraga: har doim to'liq yechiladi - tez-tez chalinadigan "
                                               "qisqa ovozlar. Oqim: o'ynayotganda diskdan o'qiladi - uzun musiqa va fon ovozlari.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##loadMode", &mode, modes, 3))
                m_AudioEdit.LoadMode = (AudioLoadMode)std::clamp(mode, 0, 2);

            const bool streamOnly = m_AudioEdit.LoadMode == AudioLoadMode::Stream;
            UI::PropertyLabel("Oldindan yuklash", "Sahna boshlanganda (unda shu fayl Audio Source'da bo'lsa) yoki birinchi ijroda yechilib, "
                                                  "xotirada qoladi: birinchi ijro kutmaydi va qayta-qayta chalinadigan ovoz har safar "
                                                  "qayta yechilmaydi. Oqim bilan o'qiladigan faylga ta'sir qilmaydi.");
            ImGui::BeginDisabled(streamOnly);
            ImGui::Checkbox("##preload", &m_AudioEdit.Preload);
            ImGui::EndDisabled();
            if (streamOnly && m_AudioEdit.Preload)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(oqim: ishlatilmaydi)");
            }

            UI::PropertyLabel("Balandlik", "Faylning o'z balandligi: undan chalinadigan har bir ovozga ko'paytiriladi (juda baland yoki "
                                           "past yozilgan faylni boshqalarga tenglashtirish). Ikki marta bosish: 0 dB.");
            float decibels = AudioGain::ToDecibels(m_AudioEdit.Volume);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string text = m_AudioEdit.Volume <= 0.0f ? std::string("jim") : std::format("{:+.1f} dB (x{:.2f})", decibels, m_AudioEdit.Volume);
            if (ImGui::SliderFloat("##clipVolume", &decibels, -40.0f, 6.0f, text.c_str()))
                m_AudioEdit.Volume = decibels <= -40.0f ? 0.0f : std::clamp(AudioGain::FromDecibels(decibels), 0.0f, AudioSettings::MaxVolume);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_AudioEdit.Volume = 1.0f;

            // The loop region (also dragged on the waveform: Shift + drag, its edges).
            UI::PropertyLabel("Takrorlash oralig'i", "Takrorlanadigan (Loop) ovoz boshidan shu oraliq oxirigacha chalinadi, keyin "
                                                     "oraliq boshidan qayta-qayta: intro + takrorlanadigan qism (musiqa uchun). To'lqin "
                                                     "shaklida Shift + sudrash bilan belgilanadi, chetlari sudraladi. Oxiri 0 - fayl oxiri; "
                                                     "0..0 - butun fayl.");
            const float duration = m_AudioInfo.Valid ? (float)m_AudioInfo.Duration : 0.0f;
            float region[2] = { m_AudioEdit.LoopStart, m_AudioEdit.LoopEnd };
            const float clear = m_AudioEdit.HasLoopRegion() ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clear);
            if (ImGui::DragFloat2("##loopRegion", region, 0.005f, 0.0f, std::max(0.0f, duration), "%.3f s", ImGuiSliderFlags_AlwaysClamp))
            {
                m_AudioEdit.LoopStart = std::round(region[0] * 1000.0f) / 1000.0f;
                m_AudioEdit.LoopEnd = std::round(region[1] * 1000.0f) / 1000.0f;
                if (m_AudioEdit.LoopEnd >= duration - 0.0005f)
                    m_AudioEdit.LoopEnd = 0.0f;
                m_AudioEdit.Sanitize();
            }
            if (m_AudioEdit.HasLoopRegion())
            {
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_XMARK "##clearLoop", ImVec2(ImGui::GetFrameHeight(), 0)))
                    m_AudioEdit.LoopStart = m_AudioEdit.LoopEnd = 0.0f;
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Takrorlash oralig'ini olib tashlash (butun fayl takrorlanadi)");
                const glm::vec2 loop = GetEditedLoopRegion();
                UI::PropertyLabel("");
                ImGui::TextDisabled("intro %s, takrorlanadi %s", AssetInfo::FormatDuration(loop.x).c_str(),
                                    AssetInfo::FormatDuration(loop.y - loop.x).c_str());
            }
            UI::EndPropertyGrid();
        }
        DrawApplyBar(meta != nullptr);
    }

    // ------------------------------------------------------------------ scenes, prefabs, fonts, scripts
    void AssetInspectorPanel::DrawSceneFile()
    {
        if (!m_Summary.Read)
        {
            m_Summary.Read = true;
            if (m_Type == AssetType::Scene)
                SceneSerializer::ReadSceneInfo(m_Path, m_Summary.Scene);
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
                        {
                            m_Summary.Roots.push_back(e.value("Tag", std::string("Entity")));
                            // A variant: its root is an instance of its base.
                            if (auto editor = e.find("Editor"); m_Type == AssetType::Prefab && m_Summary.Roots.size() == 1 && editor != e.end() &&
                                                                editor->is_object())
                                if (auto prefab = editor->find("Prefab"); prefab != editor->end() && prefab->is_string())
                                    m_Summary.VariantOf = prefab->get<std::string>();
                        }
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
        if (m_Type == AssetType::Scene)
            DrawSceneMetadata();
        if (UI::BeginPropertyGrid("##sceneInfo"))
        {
            UI::PropertyHeader("Ma'lumot");
            UI::PropertyLabel("Obyektlar");
            ImGui::Text("%zu", m_Summary.Entities);
            if (!m_Summary.VariantOf.empty())
            {
                UI::PropertyLabel("Variant");
                ImGui::TextColored(ImVec4(0.365f, 0.663f, 1.0f, 1.0f), ICON_FA_CLONE "  %s", std::filesystem::path(m_Summary.VariantOf).filename().string().c_str());
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{}\n{}", m_Summary.VariantOf,
                                            Tr("Asosdagi o'zgarishlar variantga ham o'tadi (variant o'zgartirgan qiymatlardan tashqari)")).c_str());
            }
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

    // A scene file's thumbnail and metadata (read only: the open scene's are edited in the scene settings).
    void AssetInspectorPanel::DrawSceneMetadata()
    {
        const SceneFileInfo& info = m_Summary.Scene;
        if (m_SceneThumbnails)
            if (Ref<Texture2D> thumbnail = m_SceneThumbnails->Get(m_Path))
            {
                const float width = std::min(ImGui::GetContentRegionAvail().x, 360.0f);
                const float height = width * (float)thumbnail->GetHeight() / (float)std::max(1u, thumbnail->GetWidth());
                ImGui::Image((ImTextureID)(uintptr_t)thumbnail->GetRendererID(), ImVec2(width, height));
                ImGui::Spacing();
            }
        const SceneMetadata& meta = info.Metadata;
        if (UI::BeginPropertyGrid("##sceneMeta"))
        {
            UI::PropertyHeader("Sahna");
            UI::PropertyLabel("Nomi");
            ImGui::TextUnformatted(info.Name.c_str());
            UI::PropertyLabel("Ta'rif");
            ImGui::PushTextWrapPos(0.0f);
            if (meta.Description.empty())
                ImGui::TextDisabled("-");
            else
                ImGui::TextUnformatted(meta.Description.c_str());
            ImGui::PopTextWrapPos();
            UI::PropertyLabel("Muallif");
            ImGui::TextUnformatted(meta.Author.empty() ? "-" : meta.Author.c_str());
            UI::PropertyLabel("Teglar");
            if (meta.Tags.empty())
                ImGui::TextDisabled("-");
            for (size_t i = 0; i < meta.Tags.size(); i++)
            {
                if (i > 0)
                    ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.32f, 0.45f, 1.0f));
                ImGui::SmallButton(std::format("{}##mtag{}", meta.Tags[i], i).c_str());
                ImGui::PopStyleColor();
            }
            UI::PropertyLabel("Format");
            if (info.Version < SceneMigration::CurrentVersion)
            {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "v%d (eski)", info.Version);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("Yuklashda v{} ga yangilanadi (migratsiya); saqlanganda yangi formatda yoziladi",
                                            SceneMigration::CurrentVersion).c_str());
            }
            else if (info.Version > SceneMigration::CurrentVersion)
                ImGui::TextColored(s_ErrorColor, "v%d (yangiroq dvigatel)", info.Version);
            else
                ImGui::Text("v%d", info.Version);
            if (Ref<Project> project = Project::GetActive())
            {
                UI::PropertyLabel("Build indeksi");
                const int index = Project::GetBuildIndex(Project::GetAssetRelativePath(m_Path));
                if (index >= 0)
                    ImGui::Text("%d", index);
                else
                    ImGui::TextDisabled("ro'yxatda yo'q");
            }
            UI::EndPropertyGrid();
        }
        if (IsOpenScene && IsOpenScene(m_Path) && OnEditSceneMetadata)
        {
            if (ImGui::SmallButton(Tr(ICON_FA_PEN "  Ma'lumotni tahrirlash")))
                OnEditSceneMetadata();
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ochiq sahna: ta'rif, muallif va teglar Sahna oynasida tahrirlanadi");
        }
        ImGui::Spacing();
    }

    void AssetInspectorPanel::DrawAnimationClip()
    {
        Ref<PropertyClipAsset> asset = PropertyClipLibrary::Load(m_Path); // cached; edits in the Animation window show at once
        ImGui::Spacing();
        if (!asset)
        {
            ImGui::TextColored(s_ErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  Klip o'qilmadi - konsolni tekshiring");
            return;
        }
        const PropertyClip& clip = asset->GetClip();
        if (ImGui::Button(Tr(ICON_FA_TIMELINE "  Animatsiya oynasida ochish")) && OnOpenAnimation)
            OnOpenAnimation(m_Path);
        ImGui::Spacing();
        if (UI::BeginPropertyGrid("##clip"))
        {
            static const char* wraps[] = { "Bir marta", "Takror", "Borib-qaytish" };
            UI::PropertyLabel("Davomiylik");
            ImGui::Text("%.2f s  (%d kadr, %.0f fps)", clip.Duration, (int)std::round(clip.Duration * clip.FrameRate), clip.FrameRate);
            UI::PropertyLabel("Tugaganda");
            ImGui::TextUnformatted(wraps[std::clamp((int)clip.Wrap, 0, 2)]);
            UI::PropertyLabel("Treklar");
            ImGui::Text("%zu", clip.Tracks.size());
            for (const AnimationTrack& t : clip.Tracks)
            {
                UI::PropertyLabel("");
                std::string field = t.Property == AnimatedProperty::ScriptFloat ? " " + t.Field : std::string();
                ImGui::TextDisabled("%s  %s%s  (%zu kalit)", t.Path.empty() ? "(o'zi)" : t.Path.c_str(),
                                    PropertyAnimation::GetPropertyName(t.Property), field.c_str(), t.Keys.size());
            }
            UI::PropertyLabel("Event'lar");
            ImGui::Text("%zu", clip.Events.size());
            for (const AnimationEvent& e : clip.Events)
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled(ICON_FA_BELL "  %.2f s  %s", e.Time, e.Name.c_str());
            }
            UI::EndPropertyGrid();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Obyektga Animation Player komponentini qo'shing va klipni uning ro'yxatiga torting; "
                           "skript: GetComponent<AnimationPlayerComponent>().Play(\"%s\")", asset->GetName().c_str());
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
        if(ImGui::Button("Kod muharririda ochish")&&OnOpenCode)OnOpenCode(m_Path);
        std::error_code sourceSizeError;
        if(std::filesystem::file_size(m_Path,sourceSizeError)>1024*1024&&!sourceSizeError){
            ImGui::TextDisabled("Source preview is limited to 1 MB");
            return;
        }
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
