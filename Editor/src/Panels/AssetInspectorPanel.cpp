#include "AssetInspectorPanel.h"
#include "ContentBrowserPanel.h"
#include "../UI.h"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    static AssetRegistry* GetRegistry() { return Project::GetActive() ? Project::GetAssetRegistry() : nullptr; }

    // ------------------------------------------------------------------ target
    void AssetInspectorPanel::SetAsset(const fs::path& path)
    {
        std::error_code ec;
        if (path.empty() || fs::is_directory(path, ec))
        {
            Clear();
            return;
        }
        if (path == m_Path)
            return; // picked again: edits stay
        m_Path = path;
        m_Type = AssetFiles::GetType(path);
        m_Status.clear();
        AssetRegistry* registry = GetRegistry();
        const AssetMetadata* meta = registry ? registry->Find(path) : nullptr;
        m_Handle = meta ? meta->Handle : AssetHandle();
        LoadSettings(meta);
    }

    void AssetInspectorPanel::Clear()
    {
        m_Path.clear();
        m_Type = AssetType::Other;
        m_Handle = {};
        m_Status.clear();
        m_Preview = nullptr;
        m_PreviewPath.clear();
        LoadSettings(nullptr);
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
        if (!registry)
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

    bool AssetInspectorPanel::Apply()
    {
        AssetRegistry* registry = GetRegistry();
        const AssetMetadata* meta = FindMetadata();
        if (!registry || !meta || !HasUnappliedChanges())
            return false;
        AssetImportSettings import = meta->Import; // keys of other tools stay
        if (m_Type == AssetType::Texture)
            AssetImporter::WriteTextureSettings(m_TextureEdit, import);
        else if (m_Type == AssetType::Model)
            AssetImporter::WriteModelSettings(m_ModelEdit, import);
        std::string name = meta->RelativePath;
        if (!registry->SetImportSettings(meta->Handle, import))
            return false;
        m_TextureSaved = m_TextureEdit;
        m_ModelSaved = m_ModelEdit;

        // Imported again right away: every texture / model object made from the file updates in
        // place (the watcher's event for the .zmeta then finds nothing new).
        bool failed = false;
        size_t updated = AssetHotReload::ReloadAsset(m_Path, &failed);
        m_StatusError = failed;
        m_StatusTime = ImGui::GetTime();
        if (failed)
            m_Status = "Saqlandi, lekin qayta import bo'lmadi - konsolni tekshiring";
        else if (updated > 0)
            m_Status = "Saqlandi va qayta import qilindi";
        else
            m_Status = "Saqlandi (keyingi yuklashda ishlatiladi)";
        ZM_INFO("Import sozlamalari saqlandi: {}{}", name, updated > 0 && !failed ? " (qayta import qilindi)" : "");
        return true;
    }

    void AssetInspectorPanel::Revert()
    {
        m_TextureEdit = m_TextureSaved;
        m_ModelEdit = m_ModelSaved;
    }

    // ------------------------------------------------------------------ drawing
    void AssetInspectorPanel::OnImGuiRender()
    {
        if (m_Path.empty())
            return;
        Sync();
        const AssetMetadata* meta = FindMetadata();
        DrawHeader(meta);

        std::error_code ec;
        if (!fs::is_regular_file(m_Path, ec))
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Fayl topilmadi (o'chirilgan yoki ko'chirilgan)");
            return;
        }
        switch (m_Type)
        {
            case AssetType::Texture: DrawTexture(meta); break;
            case AssetType::Model:   DrawModel(meta); break;
            default:
                ImGui::Spacing();
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Bu turdagi asset uchun import sozlamalari yo'q");
                break;
        }
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
        ImGui::EndGroup();
        ImGui::Separator();

        if (UI::BeginPropertyGrid("##assetInfo"))
        {
            UI::PropertyLabel("Yo'l");
            std::string relative = meta ? meta->RelativePath : Project::GetAssetRelativePath(m_Path);
            ImGui::TextWrapped("%s", relative.c_str());

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
            UI::EndPropertyGrid();
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
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "qo'llanmagan o'zgarishlar");
        }
        if (!canApply)
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Asset ro'yxatda yo'q - sozlamalarni saqlab bo'lmaydi");
        if (!m_Status.empty() && ImGui::GetTime() - m_StatusTime < 5.0)
            ImGui::TextColored(m_StatusError ? ImVec4(0.95f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.85f, 0.6f, 1.0f),
                               "%s  %s", m_StatusError ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_CHECK, m_Status.c_str());
    }

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

    void AssetInspectorPanel::DrawTexture(const AssetMetadata* meta)
    {
        // ---- preview (the texture as the scene uses it, imported with the saved settings)
        if (Ref<Texture2D> texture = GetPreviewTexture())
        {
            ImGui::Spacing();
            float width = (float)texture->GetWidth(), height = (float)texture->GetHeight();
            float availX = ImGui::GetContentRegionAvail().x;
            float scale = std::min({ availX / std::max(width, 1.0f), 220.0f / std::max(height, 1.0f), 4.0f });
            ImVec2 size(std::max(1.0f, width * scale), std::max(1.0f, height * scale));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availX - size.x) * 0.5f);
            ImVec2 min = ImGui::GetCursorScreenPos();
            // Checkerboard behind transparent pixels.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float cell = 8.0f;
            for (float y = 0; y < size.y; y += cell)
                for (float x = 0; x < size.x; x += cell)
                {
                    bool dark = ((int)(x / cell) + (int)(y / cell)) % 2 == 0;
                    dl->AddRectFilled(ImVec2(min.x + x, min.y + y), ImVec2(min.x + std::min(x + cell, size.x), min.y + std::min(y + cell, size.y)),
                                      dark ? IM_COL32(60, 62, 68, 255) : IM_COL32(85, 88, 95, 255));
                }
            ImGui::Image((ImTextureID)(uintptr_t)texture->GetRendererID(), size, ImVec2(0, 1), ImVec2(1, 0));
            const char* format = "?";
            switch (texture->GetSpecification().Format)
            {
                case ImageFormat::R8:      format = "R8 (1 kanal)"; break;
                case ImageFormat::RGB8:    format = "RGB8 (3 kanal)"; break;
                case ImageFormat::RGBA8:   format = "RGBA8 (4 kanal)"; break;
                case ImageFormat::RGBA32F: format = "RGBA32F"; break;
                default: break;
            }
            std::string info = std::format("{} x {}  |  {}", texture->GetWidth(), texture->GetHeight(), format);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availX - ImGui::CalcTextSize(info.c_str()).x) * 0.5f));
            ImGui::TextDisabled("%s", info.c_str());
        }
        else
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Rasm o'qilmadi - konsolni tekshiring");

        // ---- import settings
        ImGui::Spacing();
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

    void AssetInspectorPanel::DrawModel(const AssetMetadata* meta)
    {
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

}
