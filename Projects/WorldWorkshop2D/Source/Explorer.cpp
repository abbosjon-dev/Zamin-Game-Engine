#include <Zamin/Zamin.h>
#include "NavigationProbe.h"
#include <Zamin/Scene/WorldStreaming.h>
#include <cstdlib>
#include <Zamin/AI/NavPathfinder.h>
#include <array>
#include <format>
using namespace Zamin;
class WorldExplorer2D : public ScriptableEntity
{
public:
    void OnUpdate(Timestep ts) override
    {
        ++m_Frame;
        auto& t = Transform();
        glm::vec2 focus = false ? glm::vec2(t.Translation.x, t.Translation.z - 28) : glm::vec2(t.Translation.x, t.Translation.y);
        if (std::getenv("ZAMIN_WORLD_AUTOTOUR"))
        {
            const std::array<glm::vec2, 5> route = { glm::vec2(16, 9), glm::vec2(144, 9), glm::vec2(144, false ? 144 : 58), glm::vec2(1000, 1000), glm::vec2(16, 9) };
            focus = route[std::min(size_t(m_Frame / 120), route.size() - 1)];
        }
        else
        {
            float speed = 32.f * float(ts);
            if (Input::IsKeyPressed(Key::A)) focus.x -= speed;
            if (Input::IsKeyPressed(Key::D)) focus.x += speed;
            if (Input::IsKeyPressed(Key::W)) focus.y += false ? -speed : speed;
            if (Input::IsKeyPressed(Key::S)) focus.y += false ? speed : -speed;
        }
        if (std::getenv("ZAMIN_WORLD_CHECKPOINT_TEST")) focus = m_Frame >= 180 && m_Frame < 300 ? glm::vec2(1000) : glm::vec2(16, 9);
        if (std::getenv("ZAMIN_WORLD_NAVIGATION_TEST")) focus = m_Frame >= 240 && m_Frame < 300 ? glm::vec2(1000) : glm::vec2(24, 9);
        t.Translation = false ? glm::vec3(focus.x, 28, focus.y + 28) : glm::vec3(focus.x, focus.y, 50);
        TickCheckpoint();
        if (auto* manager = GetScene().GetSceneManager())
            if (auto* world = manager->GetWorldStreaming()) m_Navigation.Tick(GetScene(), *world, false, m_Frame, float(ts));
        if (m_Frame % 60 == 0)
            if (auto* manager = GetScene().GetSceneManager())
                if (auto* world = manager->GetWorldStreaming())
                {
                    const auto stats = world->GetStats();
                    if (std::getenv("ZAMIN_WORLD_AUTOTOUR"))
                    {
                        auto root = GetScene().GetEntityByUUID(world->GetNavigationEntity(2));
                        if (root)
                        {
                            const auto& grid = root.GetComponent<NavGrid2DComponent>().Grid;
                            NavPathfinder finder;
                            NavPathResult path;
                            NavPathOptions options;
                            options.SnapRadius = 0;
                            const auto status = finder.FindPath(grid, focus, focus + glm::vec2(32, 0), path, options);
                            ZM_INFO("World navigation frame {}: crossPath={} expanded={}", m_Frame, status == NavPathStatus::Complete ? 1 : 0, path.Expanded);
                        }
                    }

                    Application::Get().GetWindow().SetTitle(std::format("WorldWorkshop2D | WASD camera | active {} / {} | loaded {} unloaded {}", stats.Active, world->GetPolicy().MaxResidentCells, stats.Activations, stats.Unloads));
                    if (std::getenv("ZAMIN_WORLD_AUTOTOUR")) ZM_INFO("World tour frame {}: active={} admitted={} activated={} unloaded={} failed={}", m_Frame, stats.Active, stats.Admitted, stats.Activations, stats.Unloads, stats.Failed);
                    if (std::getenv("ZAMIN_WORLD_AUTOTOUR")) ZM_INFO("World residency frame {}: rss={} gpu={} caches={} pinned={} evicted={} pressure={} navTiles={} navCells={} navErrors={}", m_Frame,
                                                                     stats.Residency.Process.Resident, stats.Residency.RequestedGPUBytes, stats.Residency.Entries, stats.Residency.Pinned, stats.CacheEvictions,
                                                                     stats.MemoryPressure, stats.NavigationTiles, stats.NavigationCells, stats.NavigationError.empty() ? 0 : 1);
                }
    }
private:
    NavigationProbe m_Navigation;
    bool m_F5 = false, m_F9 = false;
    UUID m_Edited{ 0 }, m_Removed{ 0 }, m_Spawned{ 0 };
    float m_SavedX = 0;
    std::filesystem::path CheckpointPath()
    {
        if (const auto* file = std::getenv("ZAMIN_WORLD_CHECKPOINT_FILE")) return file;
        return SaveData::GetDirectoryForProject("WorldWorkshop2D") / "world-checkpoint.json";
    }
    void TickCheckpoint()
    {
        auto* manager = GetScene().GetSceneManager();
        auto* world = manager ? manager->GetWorldStreaming() : nullptr;
        if (!world) return;
        if (m_Frame == 1) world->SetCaptureOnUnload(true);
        const bool f5 = Input::IsKeyPressed(Key::F5), f9 = Input::IsKeyPressed(Key::F9);
        std::string error;
        if (f5 && !m_F5)
        {
            if (!world->Enqueue({ WorldStreaming::CommandKind::Save, UUID(0), UUID(0), CheckpointPath() }, error)) ZM_ERROR("Checkpoint save: {}", error);
            else ZM_INFO("Checkpoint save queued: {}", CheckpointPath().string());
        }
        if (f9 && !m_F9)
        {
            if (!world->Enqueue({ WorldStreaming::CommandKind::Restore, UUID(0), UUID(0), CheckpointPath() }, error)) ZM_ERROR("Checkpoint restore: {}", error);
        }
        m_F5 = f5;
        m_F9 = f9;
        if (!std::getenv("ZAMIN_WORLD_CHECKPOINT_TEST")) return;
        if (world->GetStats().FailedCommands) throw std::runtime_error("Checkpoint command failed");
        const auto cell = world->GetManifest().Cells.front().ID;
        if (m_Frame == 120)
        {
            for (auto id : world->GetCellEntities(cell))
            {
                auto entity = GetScene().GetEntityByUUID(id);
                if (entity.GetName() == "Cell landmark" || entity.GetName() == "Cell marker")
                {
                    if (!m_Edited.IsValid()) m_Edited = id;
                    else if (!m_Removed.IsValid()) m_Removed = id;
                }
            }
            if (!m_Edited.IsValid() || !m_Removed.IsValid()) throw std::runtime_error("Checkpoint probe needs two cell markers");
            auto entity = GetScene().GetEntityByUUID(m_Edited);
            entity.GetTransform().Translation.x += 3;
            m_SavedX = entity.GetTransform().Translation.x;
            if (!world->Enqueue({ WorldStreaming::CommandKind::Remove, cell, m_Removed, {} }, error)) throw std::runtime_error(error);
            auto marker = GetScene().CreateEntity("Checkpoint spawned actor");
            m_Spawned = marker.GetUUID();
            marker.GetTransform().Translation = { 12, 7, 14 };
            marker.GetTransform().Translation = { 12, 9, 0 };
            marker.AddComponent<SpriteRendererComponent>().Color = { 1, .4f, .1f, 1 };
            if (!world->Enqueue({ WorldStreaming::CommandKind::Adopt, cell, m_Spawned, {} }, error) || !world->Enqueue({ WorldStreaming::CommandKind::Save, UUID(0), UUID(0), CheckpointPath() }, error)) throw std::runtime_error(error);
            ZM_INFO("Checkpoint oracle: queued edits, deletion and spawned actor");
        }
        auto verify = [&] {
            auto edited = GetScene().GetEntityByUUID(m_Edited);
            if (!edited || std::abs(edited.GetTransform().Translation.x - m_SavedX) > .001f || GetScene().GetEntityByUUID(m_Removed) || !GetScene().GetEntityByUUID(m_Spawned))
                throw std::runtime_error("Checkpoint entity state oracle failed");
        };
        if (m_Frame == 360)
        {
            verify();
            ZM_INFO("Checkpoint oracle: unload and return preserved state");
            GetScene().GetEntityByUUID(m_Edited).GetTransform().Translation.x -= 50;
            if (!world->Enqueue({ WorldStreaming::CommandKind::Remove, cell, m_Spawned, {} }, error) || !world->Enqueue({ WorldStreaming::CommandKind::Restore, UUID(0), UUID(0), CheckpointPath() }, error)) throw std::runtime_error(error);
        }
        if (m_Frame == 480)
        {
            verify();
            if (world->GetStats().Restores != 1 || world->GetStats().RestorePending) throw std::runtime_error("Checkpoint restore did not finish");
            ZM_INFO("Checkpoint oracle: disk restore passed");
        }
    }
    uint32_t m_Frame = 0;
};
ZM_REGISTER_SCRIPT(WorldExplorer2D)
