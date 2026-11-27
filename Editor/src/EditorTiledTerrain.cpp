// Tiled (streamed) terrain in the editor: making one, converting a single terrain to tiles, sculpting and painting
// across tile borders through the streamer, saving the edited tiles with the scene; and the sprite atlases that
// are rebuilt when their sources change on disk.
#include "EditorLayer.h"
#include "Panels/VisualGraphLayout.h"
#include "UI.h"

#include <Zamin/Asset/SpriteAtlas.h>
#include <Zamin/Scene/TerrainStreaming.h>
#include <Zamin/Utils/FileSystem.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace Zamin {

    namespace fs = std::filesystem;

    Entity EditorLayer::TiledTerrainEntity()
    {
        if (!m_EditorScene) return {};
        auto selected = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selected && selected.GetScene() == m_EditorScene.get() && selected.IsValid() && selected.HasComponent<TiledTerrainComponent>()) return selected;
        return {};
    }

    bool EditorLayer::TiledTerrainMouseHit(Entity entity, TerrainHit& hit)
    {
        if (!entity || m_ViewportSize.x <= 0 || m_ViewportSize.y <= 0) return false;
        const auto mouse = ImGui::GetMousePos();
        const float x = (mouse.x - m_ViewportBounds[0].x) / m_ViewportSize.x, y = (mouse.y - m_ViewportBounds[0].y) / m_ViewportSize.y;
        if (x < 0 || x > 1 || y < 0 || y > 1) return false;
        const auto inverse = glm::inverse(m_EditorCamera.GetViewProjection());
        const auto a = inverse * glm::vec4(x * 2 - 1, 1 - y * 2, -1, 1), b = inverse * glm::vec4(x * 2 - 1, 1 - y * 2, 1, 1);
        const glm::vec3 origin = glm::vec3(a) / a.w, direction = glm::normalize(glm::vec3(b) / b.w - origin);
        const auto world = m_EditorScene->GetWorldTransform(entity);
        if (std::abs(glm::determinant(world)) < 1e-8f) return false;
        const auto local = glm::inverse(world);
        return TerrainStreaming::Raycast(entity.GetComponent<TiledTerrainComponent>(), glm::vec3(local * glm::vec4(origin, 1)), glm::vec3(local * glm::vec4(direction, 0)), hit);
    }

    bool EditorLayer::UpdateTiledTerrainPainting(Timestep ts, bool down)
    {
        Entity entity = TiledTerrainEntity();
        if (!entity) return false;
        auto& tiled = entity.GetComponent<TiledTerrainComponent>();
        if (!down)
        {
            if (m_TiledStroke)
            {
                m_TiledStroke = false;
                TerrainStreaming::EndStroke(tiled);
            }
            return true;
        }
        if (!m_ViewportHovered || Input::IsKeyPressed(Key::LeftAlt) || Input::IsKeyPressed(Key::RightAlt) || m_EditorCamera.IsFlying()) return true;
        if (m_EditorScene->IsLockedInEditor(entity) || m_EditorScene->IsHiddenInEditor(entity) || m_TerrainTool > 4) return true;
        TerrainHit hit;
        if (!TiledTerrainMouseHit(entity, hit)) return true;
        if (Input::IsKeyPressed(Key::LeftControl))
        {
            m_TerrainBrush.TargetHeight = hit.Position.y;
            return true;
        }
        if (!m_TiledStroke)
        {
            TerrainStreaming::BeginStroke(tiled);
            m_TiledStroke = true;
            m_TerrainHasPrevious = false;
        }
        auto settings = m_TerrainBrush;
        settings.Tool = TerrainBrush(m_TerrainTool);
        if (Input::IsKeyPressed(Key::LeftShift))
        {
            if (settings.Tool == TerrainBrush::Raise) settings.Tool = TerrainBrush::Lower;
            else if (settings.Tool == TerrainBrush::Lower) settings.Tool = TerrainBrush::Raise;
        }
        const glm::vec2 center(hit.Position.x, hit.Position.z);
        if (!m_TerrainHasPrevious) m_TerrainPreviousCenter = center;
        const int stamps = std::clamp(int(std::ceil(glm::length(center - m_TerrainPreviousCenter) / std::max(settings.Radius * 0.25f, 0.1f))), 1, 32);
        bool changed = false;
        for (int i = 1; i <= stamps; ++i)
            changed |= TerrainStreaming::ApplyBrush(tiled, glm::mix(m_TerrainPreviousCenter, center, float(i) / float(stamps)), settings, float(ts) / float(stamps));
        if (changed)
        {
            m_SceneDirty = true; // the tiles are written with the scene (Ctrl+S)
            UpdateWindowTitle();
        }
        m_TerrainPreviousCenter = center;
        m_TerrainHasPrevious = true;
        return true;
    }

    bool EditorLayer::DrawTiledTerrainPreview()
    {
        Entity entity = TiledTerrainEntity();
        if (!entity) return false;
        TerrainHit hit;
        if (!TiledTerrainMouseHit(entity, hit)) return true;
        const auto& tiled = entity.GetComponent<TiledTerrainComponent>();
        const auto world = m_EditorScene->GetWorldTransform(entity);
        glm::vec3 previous{};
        for (int i = 0; i <= 64; ++i)
        {
            const float a = float(i) / 64 * 6.2831853f;
            const glm::vec2 p = glm::vec2(hit.Position.x, hit.Position.z) + glm::vec2(std::cos(a), std::sin(a)) * m_TerrainBrush.Radius;
            const glm::vec3 point = glm::vec3(world * glm::vec4(p.x, TerrainStreaming::SampleHeight(tiled, p) + 0.08f, p.y, 1));
            if (i) Renderer2D::DrawLine(previous, point, { 1, 0.8f, 0.15f, 1 });
            previous = point;
        }
        // The tile borders under the brush (where the stroke crosses into the neighbours).
        if (tiled.Streamer && !tiled.Streamer->Tiles.empty())
        {
            const auto& set = tiled.Streamer->Set;
            const int tx = int(std::floor(hit.Position.x / set.TileSize)), tz = int(std::floor(hit.Position.z / set.TileSize));
            for (int k = 0; k <= 1; k++)
                for (int s = 0; s <= 16; s++)
                {
                    const float along = (float(tz) + float(s) / 16.0f) * set.TileSize, x = float(tx + k) * set.TileSize;
                    const glm::vec2 p(x, along), q(x, along + set.TileSize / 16.0f);
                    if (s < 16)
                        Renderer2D::DrawLine(glm::vec3(world * glm::vec4(p.x, TerrainStreaming::SampleHeight(tiled, p) + 0.12f, p.y, 1)),
                                             glm::vec3(world * glm::vec4(q.x, TerrainStreaming::SampleHeight(tiled, q) + 0.12f, q.y, 1)), { 0.3f, 0.8f, 1.0f, 1 });
                    const glm::vec2 u(along - float(tz) * set.TileSize + float(tx) * set.TileSize, float(tz + k) * set.TileSize);
                    const glm::vec2 v(u.x + set.TileSize / 16.0f, u.y);
                    if (s < 16)
                        Renderer2D::DrawLine(glm::vec3(world * glm::vec4(u.x, TerrainStreaming::SampleHeight(tiled, u) + 0.12f, u.y, 1)),
                                             glm::vec3(world * glm::vec4(v.x, TerrainStreaming::SampleHeight(tiled, v) + 0.12f, v.y, 1)), { 0.3f, 0.8f, 1.0f, 1 });
                }
        }
        return true;
    }

    Entity EditorLayer::CreateTiledTerrain()
    {
        if (!m_EditorScene || m_SceneState != SceneState::Edit || !Project::GetActive()) return {};
        const fs::path assets = Project::GetAssetDirectory();
        const fs::path file = AssetFiles::MakeUniquePath(assets / "Maps", "TiledTerrain.zterrain");
        TerrainTileSet set;
        set.TilesX = set.TilesZ = uint32_t(std::clamp(m_TiledTiles, 1, 64));
        set.TileResolution = uint32_t(m_TiledResolution);
        set.TileSize = m_TiledTileSize;
        TerrainTools::WorldTerrainSettings world{ uint32_t(m_TerrainSeed), m_TerrainBase, m_TerrainAmplitude, m_TerrainFrequency * 0.4f };
        std::string error;
        if (!TerrainTileTools::Create(file, set, world, 24, error))
        {
            m_TerrainMessage = TrFormat("Plitkali yer yaratilmadi: {}", error);
            return {};
        }
        if (AssetRegistry* registry = Project::GetAssetRegistry()) registry->OnFileAdded(file.parent_path());
        CommitUndo();
        auto entity = m_EditorScene->CreateEntity("Tiled Terrain");
        auto& tiled = entity.AddComponent<TiledTerrainComponent>();
        tiled.Path = Project::GetAssetRelativePath(file);
        m_SceneHierarchyPanel.SetSelectedEntity(entity);
        Set2DMode(false);
        const float extent = set.TileSize * float(set.TilesX);
        m_EditorCamera.Focus({ extent * 0.25f, m_TerrainBase, extent * 0.25f }, extent * 0.4f);
        m_EditorCamera.SetOrientation(0.65f, 0.7f);
        m_TerrainMessage = TrFormat("Plitkali yer: {} x {} plitka, {}", set.TilesX, set.TilesZ, tiled.Path);
        MarkDirty(Tr("Plitkali yer yaratish"));
        return entity;
    }

    bool EditorLayer::ConvertToTiledTerrain(Entity entity, uint32_t tileResolution)
    {
        if (!entity || !entity.HasComponent<TerrainComponent>() || !Project::GetActive()) return false;
        const fs::path assets = Project::GetAssetDirectory();
        const fs::path file = AssetFiles::MakeUniquePath(assets / "Maps", FileSystem::SanitizeRelativePath(entity.GetName()).empty() ? "Terrain.zterrain"
                                                                                                                              : entity.GetName() + ".zterrain");
        // Packed foliage standing on the terrain (a direct child) moves into the tiles.
        Entity foliageEntity;
        for (auto child : m_EditorScene->GetChildren(entity))
            if (child.HasComponent<FoliageComponent>())
            {
                foliageEntity = child;
                break;
            }
        TerrainTileSet set;
        std::string error;
        if (!TerrainTileTools::MigrateFromTerrain(entity.GetComponent<TerrainComponent>(), foliageEntity ? &foliageEntity.GetComponent<FoliageComponent>() : nullptr,
                                                  file, tileResolution, set, error))
        {
            m_TerrainMessage = TrFormat("Plitkalarga o'tkazilmadi: {}", error);
            return false;
        }
        if (AssetRegistry* registry = Project::GetAssetRegistry()) registry->OnFileAdded(file.parent_path());
        CommitUndo();
        entity.RemoveComponent<TerrainComponent>();
        if (entity.HasComponent<MeshCollider3DComponent>()) entity.RemoveComponent<MeshCollider3DComponent>();
        if (entity.HasComponent<Rigidbody3DComponent>()) entity.RemoveComponent<Rigidbody3DComponent>();
        auto& tiled = entity.AddComponent<TiledTerrainComponent>();
        tiled.Path = Project::GetAssetRelativePath(file);
        if (foliageEntity) m_EditorScene->DestroyEntity(foliageEntity);
        m_TerrainMessage = TrFormat("{} x {} plitkaga o'tkazildi: {}", set.TilesX, set.TilesZ, tiled.Path);
        MarkDirty(Tr("Plitkali yerga o'tkazish"));
        return true;
    }

    void EditorLayer::UI_TiledTerrain(Entity single)
    {
        ImGui::SeparatorText(Tr("Plitkali yer (streaming)"));
        GraphLayout::RecordKey("terrain/tiled");
        static const int resolutions[] = { 33, 65, 129, 257 };
        const char* labels[] = { "33", "65", "129", "257" };
        int r = int(std::find(std::begin(resolutions), std::end(resolutions), m_TiledResolution) - std::begin(resolutions));
        if (ImGui::Combo(Tr("Plitka nuqtalari"), &r, labels, 4)) m_TiledResolution = resolutions[std::clamp(r, 0, 3)];
        if (single)
        {
            if (ImGui::Button(Tr("Plitkalarga o'tkazish")))
                ConvertToTiledTerrain(single, uint32_t(m_TiledResolution));
            GraphLayout::RecordKey("terrain/convert");
            if (ImGui::IsItemHovered())
                UI::Tooltip(Tr("Faol yer plitkalarga bo'linadi (.zterrain va plitka papkasi): katta map kamera atrofida yuklanadi. Undo sahnani qaytaradi, fayllar qoladi"));
        }
        ImGui::SliderInt(Tr("Plitkalar (bir tomonda)"), &m_TiledTiles, 1, 64);
        ImGui::DragFloat(Tr("Plitka o'lchami (m)"), &m_TiledTileSize, 1, 8, 1024, "%.0f");
        if (ImGui::Button(Tr("Yangi plitkali yer")))
            CreateTiledTerrain();
        GraphLayout::RecordKey("terrain/new-tiled");
        if (Entity tiled = TiledTerrainEntity())
        {
            // The brush of the selected tiled terrain (the scatter tools are for single terrains).
            const bool edit = ImGui::Checkbox((std::string(Tr("Cho'tka faol")) + "##tiledBrush").c_str(), &m_TerrainPainting);
            GraphLayout::RecordKey("terrain/brush");
            if (edit && m_TerrainPainting) m_GizmoMode = GizmoMode::None;
            const char* tools[] = { Tr("Ko'tarish"), "Pasaytirish", Tr("Tekislash"), Tr("Silliqlash"), Tr("Tekstura bo'yash") };
            m_TerrainTool = std::clamp(m_TerrainTool, 0, 4);
            UI::TrCombo(Tr("Asbob"), &m_TerrainTool, tools, 5);
            ImGui::SliderFloat("Radius (m)##tiled", &m_TerrainBrush.Radius, 0.25f, 64, "%.2f");
            ImGui::SliderFloat((std::string(Tr("Kuch")) + "##tiled").c_str(), &m_TerrainBrush.Strength, 0.1f, 30, "%.2f");
            const auto& c = tiled.GetComponent<TiledTerrainComponent>();
            if (c.Streamer)
            {
                const auto& stats = c.Streamer->Stats;
                ImGui::Text(Tr("%u / %u plitka, %.1f MB | o'zgargan: %u"), stats.Resident, stats.Tiles, double(stats.ResidentBytes) / 1048576.0, stats.DirtyTiles);
            }
            ImGui::TextWrapped("%s", Tr("Cho'tka plitka chegaralari bo'ylab ishlaydi: tegilgan plitkalar to'liq detalda yuklanadi va umumiy qirralari tenglashtiriladi. Ctrl+S plitkalarni saqlaydi."));
        }
    }

    void EditorLayer::SaveTiledTerrains()
    {
        if (!m_EditorScene) return;
        for (auto handle : m_EditorScene->GetAllEntitiesWith<TiledTerrainComponent>())
        {
            Entity e(handle, m_EditorScene.get());
            auto& tiled = e.GetComponent<TiledTerrainComponent>();
            if (!TerrainStreaming::DirtyCount(tiled)) continue;
            std::string error;
            const size_t written = TerrainStreaming::SaveDirty(tiled, &error);
            if (!error.empty()) ZM_ERROR("Tiled terrain '{}': {}", e.GetName(), error);
            else ZM_INFO("Tiled terrain '{}': {} tiles saved", e.GetName(), written);
        }
    }

    void EditorLayer::RebuildStaleAtlases()
    {
        AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
        if (!registry || m_SceneState != SceneState::Edit) return;
        // About once a second: the sources' sizes and times of every atlas (.zatlas files are not registry assets: the
        // list of them is found on disk every few seconds); a changed stamp is checked against the content hash.
        const double now = ImGui::GetTime();
        if (now - m_AtlasCheckTime < 1.0) return;
        m_AtlasCheckTime = now;
        static std::vector<fs::path> atlases;
        static double listed = -100.0;
        static std::map<std::string, uint64_t> stamps;
        const fs::path assets = Project::GetAssetDirectory();
        std::error_code ec;
        if (now - listed > 5.0 || registry->GetRevision() != m_AtlasRegistryRevision)
        {
            listed = now;
            m_AtlasRegistryRevision = registry->GetRevision();
            atlases.clear();
            for (auto it = fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec))
                if (it->is_regular_file(ec) && FileSystem::HasExtension(it->path(), SpriteAtlasDefinition::Extension)) atlases.push_back(it->path());
        }
        for (const fs::path& file : atlases)
        {
            SpriteAtlasDefinition atlas;
            if (!SpriteAtlasDefinition::Load(file, atlas)) continue;
            uint64_t stamp = HashBytes(&atlas.SourceHash, sizeof(atlas.SourceHash));
            for (const std::string& source : SpriteAtlas::ListSources(assets, atlas))
            {
                const auto size = fs::file_size(assets / source, ec);
                const auto time = fs::last_write_time(assets / source, ec).time_since_epoch().count();
                stamp = HashBytes(source.data(), source.size(), stamp);
                stamp = HashBytes(&size, sizeof(size), stamp);
                stamp = HashBytes(&time, sizeof(time), stamp);
            }
            const std::string relative = FileSystem::RelativePath(file, assets);
            auto [it, fresh] = stamps.try_emplace(relative, stamp);
            if (!fresh && it->second == stamp) continue;
            it->second = stamp;
            if (!SpriteAtlas::IsStale(assets, file)) continue;
            const SpriteAtlasBuildResult r = SpriteAtlas::Build(assets, file, false, registry);
            if (!r.Ok)
            {
                ZM_WARN("Sprite atlas {} was not rebuilt: {}", relative, r.Error);
                continue;
            }
            TextureLibrary::Reload(assets / atlas.Output);
            ZM_INFO("Sprite atlas {} rebuilt: {} sprites ({} kept, {} added, {} removed)", relative, r.Sprites, r.Kept.size(), r.Added.size(), r.Removed.size());
        }
    }

}
