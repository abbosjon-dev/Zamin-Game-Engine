// The asset inspector's sprite atlas (.zatlas) and tiled terrain (.zterrain) views.
#include "AssetInspectorPanel.h"
#include "VisualGraphLayout.h"
#include "../UI.h"

#include <Zamin/Asset/SpriteAtlas.h>
#include <Zamin/Scene/SpriteClip.h>
#include <Zamin/Scene/TerrainStreaming.h>
#include <Zamin/Project/ExportAssets.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    void AssetInspectorPanel::DrawSpriteAtlas()
    {
        const fs::path assets = Project::GetAssetDirectory();
        if (m_AtlasPath != m_Path)
        {
            m_AtlasPath = m_Path;
            m_AtlasEdit = {};
            std::string error;
            if (!SpriteAtlasDefinition::Load(m_Path, m_AtlasEdit, &error))
            {
                m_AtlasStatus = TrFormat("Atlas o'qilmadi: {}", error);
                m_AtlasStatusError = true;
            }
            else
                m_AtlasStatus.clear();
            m_AtlasCheckTime = -100.0;
        }
        // Whether the sources changed since the page was built (checked about once a second).
        if (ImGui::GetTime() - m_AtlasCheckTime > 1.0)
        {
            m_AtlasStale = SpriteAtlas::IsStale(assets, m_Path);
            m_AtlasCheckTime = ImGui::GetTime();
        }
        SpriteAtlasDefinition& a = m_AtlasEdit;
        ImGui::Spacing();
        if (m_AtlasStale)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.32f, 1.0f), "%s", Tr(ICON_FA_TRIANGLE_EXCLAMATION "  Manbalar o'zgargan - atlasni qayta yig'ing"));
        else
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "%s", Tr(ICON_FA_CIRCLE_CHECK "  Atlas manbalari bilan bir xil"));
        GraphLayout::RecordKey("atlas/state");
        bool edited = false;
        if (UI::BeginPropertyGrid("##atlas"))
        {
            UI::PropertyLabel("Manba papka", "Bu papkaning rasmlari (har biri bitta sprite, o'z kadrlari bo'lsa - kadrlari) sahifaga joylanadi");
            edited |= ImGui::InputText("##sources", &a.Sources);
            UI::PropertyLabel("Sahifa", "Yig'ilgan rasm: oddiy tekstura, kadrlari uning .zmeta faylida");
            ImGui::TextDisabled("%s  (%u x %u)", a.Output.c_str(), a.PageWidth, a.PageHeight);
            UI::PropertyLabel("Pixel-art");
            edited |= ImGui::Checkbox("##pixelArt", &a.PixelArt);
            UI::PropertyLabel("Ichki papkalar");
            edited |= ImGui::Checkbox("##recursive", &a.Recursive);
            UI::PropertyLabel("Grid (tileset)", "Hamma spritelar bir o'lchamda, nom tartibida qatorma-qator: sahifa Tilemap uchun tileset bo'ladi");
            edited |= ImGui::Checkbox("##grid", &a.Grid);
            if (a.Grid)
            {
                UI::PropertyLabel("Ustunlar");
                int columns = int(a.Columns);
                if (ImGui::DragInt("##columns", &columns, 0.2f, 1, 1024))
                {
                    a.Columns = uint32_t(std::clamp(columns, 1, 1024));
                    edited = true;
                }
            }
            UI::PropertyLabel("Sahifa hajmi (px)");
            static const char* sizes[] = { "256", "512", "1024", "2048", "4096" };
            int size = std::clamp(int(std::log2(std::max(256u, a.Packing.MaxPageSize))) - 8, 0, 4);
            if (ImGui::Combo("##pageSize", &size, sizes, 5))
            {
                a.Packing.MaxPageSize = 256u << size;
                edited = true;
            }
            UI::PropertyLabel("Oraliq (px)", "Har bir sprite atrofidagi chekka piksellar nusxasi (bleed): filtrlashda qo'shni sprite ko'rinmaydi");
            int padding = int(a.Packing.Padding);
            if (ImGui::DragInt("##padding", &padding, 0.2f, 0, 64))
            {
                a.Packing.Padding = uint32_t(std::clamp(padding, 0, 64));
                edited = true;
            }
            UI::PropertyLabel("Mip xavfsiz darajalar", "Spritelar 2^n pikselga tekislanadi: shu darajagacha mip'lar qo'shni spritelarni aralashtirmaydi");
            int mips = int(a.Packing.MipLevels);
            if (ImGui::SliderInt("##mips", &mips, 0, 4))
            {
                a.Packing.MipLevels = uint32_t(mips);
                edited = true;
            }
            UI::PropertyLabel("Pivot", "Manbasida pivot bo'lmagan spritelar uchun (Sprite muharririda har biri alohida o'zgartiriladi)");
            edited |= ImGui::DragFloat2("##pivot", &a.Pivot.x, 0.01f, 0.0f, 1.0f, "%.2f");
            UI::EndPropertyGrid();
        }
        if (edited)
            m_AtlasStale = true;
        auto rebuild = [&](bool force) {
            if (!a.Save(m_Path))
            {
                m_AtlasStatus = Tr("Atlas fayli yozilmadi");
                m_AtlasStatusError = true;
                return;
            }
            const SpriteAtlasBuildResult r = SpriteAtlas::Build(assets, m_Path, force, Project::GetAssetRegistry());
            if (!r.Ok)
            {
                m_AtlasStatus = TrFormat("Atlas yig'ilmadi: {}", r.Error);
                m_AtlasStatusError = true;
                return;
            }
            SpriteAtlasDefinition::Load(m_Path, a);
            TextureLibrary::Reload(assets / a.Output);
            m_AtlasStatus = TrFormat("{} ta sprite, {} x {} px, to'liqlik {:.0f}% | qoldi {}, qo'shildi {}, olib tashlandi {}", r.Sprites, r.PageWidth, r.PageHeight,
                                     r.Occupancy * 100.0, r.Kept.size(), r.Added.size(), r.Removed.size());
            m_AtlasStatusError = false;
            m_AtlasStale = false;
            m_AtlasCheckTime = ImGui::GetTime();
            if (OnFilesCreated) OnFilesCreated({ assets / a.Output });
        };
        if (ImGui::Button(Tr(ICON_FA_ARROWS_ROTATE "  Qayta yig'ish")))
            rebuild(true);
        GraphLayout::RecordKey("atlas/rebuild");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Manbalarni qaytadan joylash: bir xil nomli kadrlar pivot va 9-slice chegaralarini saqlaydi, sahifaning UUID'si o'zgarmaydi");
        ImGui::SameLine();
        ImGui::BeginDisabled(!OnOpenSpriteEditor);
        if (ImGui::Button(Tr(ICON_FA_SCISSORS "  Sprite muharriri")) && OnOpenSpriteEditor)
            OnOpenSpriteEditor(assets / a.Output);
        ImGui::EndDisabled();
        if (!m_AtlasStatus.empty())
        {
            if (m_AtlasStatusError)
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", m_AtlasStatus.c_str());
            else
                ImGui::TextWrapped("%s", m_AtlasStatus.c_str());
            GraphLayout::RecordKey("atlas/status");
        }

        // ---- the page with every frame's rectangle
        const SpriteSheet* sheet = SpriteSheet::Get(a.Output);
        Ref<Texture2D> page = TextureLibrary::Load(assets / a.Output, a.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        if (page && page->IsReady() && page->GetWidth() > 1)
        {
            ImGui::SeparatorText(TrFormat("Sahifa ({} ta kadr)###atlasPage", sheet ? sheet->Frames.size() : 0).c_str());
            const float width = std::min(ImGui::GetContentRegionAvail().x, 512.0f);
            const float scale = width / float(page->GetWidth());
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 extent(width, float(page->GetHeight()) * scale);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, ImVec2(origin.x + extent.x, origin.y + extent.y), IM_COL32(48, 50, 56, 255));
            ImGui::Image((ImTextureID)(uintptr_t)page->GetRendererID(), extent, ImVec2(0, 1), ImVec2(1, 0));
            GraphLayout::RecordKey("atlas/page");
            if (sheet)
                for (const SpriteFrame& f : sheet->Frames)
                {
                    const ImVec2 lo(origin.x + float(f.Position.x) * scale, origin.y + float(f.Position.y) * scale);
                    const ImVec2 hi(lo.x + float(f.Size.x) * scale, lo.y + float(f.Size.y) * scale);
                    draw->AddRect(lo, hi, IM_COL32(90, 200, 255, 200));
                    const ImVec2 pivot(lo.x + f.Pivot.x * (hi.x - lo.x), hi.y - f.Pivot.y * (hi.y - lo.y));
                    draw->AddCircleFilled(pivot, 2.5f, IM_COL32(255, 210, 60, 255));
                    if (ImGui::IsMouseHoveringRect(lo, hi))
                        ImGui::SetTooltip("%s  %dx%d  pivot %.2f, %.2f", f.Name.c_str(), f.Size.x, f.Size.y, f.Pivot.x, f.Pivot.y);
                }
        }

        // ---- sprite clips from numbered frames ("run_0", "run_1" ...)
        if (sheet && !sheet->GetSequencePrefixes().empty())
        {
            ImGui::SeparatorText(Tr("Animatsiya klipi"));
            const std::vector<std::string> prefixes = sheet->GetSequencePrefixes();
            m_AtlasClipPrefix = std::clamp(m_AtlasClipPrefix, 0, int(prefixes.size()) - 1);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo("##clipPrefix", prefixes[size_t(m_AtlasClipPrefix)].c_str()))
            {
                for (int i = 0; i < int(prefixes.size()); i++)
                    if (ImGui::Selectable(prefixes[size_t(i)].c_str(), i == m_AtlasClipPrefix))
                        m_AtlasClipPrefix = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr(ICON_FA_FILM "  Klip yaratish")))
            {
                SpriteClip clip;
                clip.Texture = a.Output;
                clip.Frames = sheet->FindSequence(prefixes[size_t(m_AtlasClipPrefix)]);
                clip.FPS = 10.0f;
                const fs::path file = AssetFiles::MakeUniquePath(m_Path.parent_path(), prefixes[size_t(m_AtlasClipPrefix)] + SpriteClip::Extension);
                if (SpriteClipSerializer::Serialize(clip, file))
                {
                    if (AssetRegistry* registry = Project::GetAssetRegistry()) registry->OnFileAdded(file);
                    if (OnFilesCreated) OnFilesCreated({ file });
                    m_AtlasStatus = TrFormat("Klip yaratildi: {} ({} kadr)", file.filename().string(), clip.Frames.size());
                    m_AtlasStatusError = false;
                }
            }
            GraphLayout::RecordKey("atlas/clip");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Tanlangan ketma-ketlik kadrlaridan .zsanim klip: Sprite Animation komponentiga qo'shing");
        }
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("Sprite Renderer: tekstura - sahifa, sprite - kadr nomi. Tilemap: Grid rejimidagi sahifa tileset bo'ladi. "
                                    "Eksport manbalari o'zgargan sahifani qayta yig'adi; ishlatilmagan manba papka o'yinga kirmaydi."));
    }

    void AssetInspectorPanel::DrawTerrainTileSet()
    {
        TerrainTileSet set;
        std::string error;
        ImGui::Spacing();
        if (!TerrainTileSet::Load(m_Path, set, &error))
        {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", TrFormat("Plitkali yer o'qilmadi: {}", error).c_str());
            return;
        }
        if (UI::BeginPropertyGrid("##tiles"))
        {
            UI::PropertyLabel("Plitkalar");
            ImGui::Text("%u x %u  (%.0f x %.0f m)", set.TilesX, set.TilesZ, set.TileSize * float(set.TilesX), set.TileSize * float(set.TilesZ));
            UI::PropertyLabel("Plitka");
            ImGui::Text(Tr("%u nuqta, %.0f m, %u LOD daraja"), set.TileResolution, set.TileSize, set.UsableLevels());
            UI::PropertyLabel("Xotira (plitka)", "Bitta plitkaning har bir LOD darajada taxminiy CPU + GPU hajmi");
            std::string levels;
            for (uint32_t l = 0; l < set.UsableLevels(); l++) levels += (l ? " | " : "") + ExportAssets::FormatSize(TerrainTileTools::EstimateBytes(set, l, 0));
            ImGui::TextDisabled("%s", levels.c_str());
            uint64_t foliage = 0;
            for (uint32_t c : set.FoliageCounts) foliage += c;
            UI::PropertyLabel("O'simliklar");
            ImGui::Text("%llu", (unsigned long long)foliage);
            UI::EndPropertyGrid();
        }
        ImGui::TextWrapped("%s", Tr("Sahnaga Tiled terrain komponenti bilan qo'shing (Path - shu fayl)."));
    }

}
