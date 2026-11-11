#include <Zamin/Zamin.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <cstdlib>
#include <Zamin/AI/NavPathfinder.h>
#include <array>
#include <format>
using namespace Zamin;
class WorldExplorer3D : public ScriptableEntity
{
public:
    void OnUpdate(Timestep ts) override
    {
        ++m_Frame;
        auto& t = Transform();
        glm::vec2 focus = true ? glm::vec2(t.Translation.x, t.Translation.z - 28) : glm::vec2(t.Translation.x, t.Translation.y);
        if (std::getenv("ZAMIN_WORLD_AUTOTOUR"))
        {
            const std::array<glm::vec2, 5> route = { glm::vec2(16, 9), glm::vec2(144, 9), glm::vec2(144, true ? 144 : 58), glm::vec2(1000, 1000), glm::vec2(16, 9) };
            focus = route[std::min(size_t(m_Frame / 120), route.size() - 1)];
        }
        else
        {
            float speed = 32.f * float(ts);
            if (Input::IsKeyPressed(Key::A)) focus.x -= speed;
            if (Input::IsKeyPressed(Key::D)) focus.x += speed;
            if (Input::IsKeyPressed(Key::W)) focus.y += true ? -speed : speed;
            if (Input::IsKeyPressed(Key::S)) focus.y += true ? speed : -speed;
        }
        t.Translation = true ? glm::vec3(focus.x, 28, focus.y + 28) : glm::vec3(focus.x, focus.y, 50);
        if (m_Frame % 60 == 0)
            if (auto* manager = GetScene().GetSceneManager())
                if (auto* world = manager->GetWorldStreaming())
                {
                    const auto stats = world->GetStats();
                    if (std::getenv("ZAMIN_WORLD_AUTOTOUR"))
                    {
                        auto root = GetScene().GetEntityByUUID(world->GetNavigationEntity(3));
                        if (root)
                        {
                            const auto& grid = root.GetComponent<NavGrid3DComponent>().Grid;
                            NavPathfinder finder;
                            NavPathResult path;
                            NavPathOptions options;
                            options.SnapRadius = 0;
                            const auto status = finder.FindPath(grid, focus, focus + glm::vec2(32, 0), path, options);
                            ZM_INFO("World navigation frame {}: crossPath={} expanded={}", m_Frame, status == NavPathStatus::Complete ? 1 : 0, path.Expanded);
                        }
                    }

                    Application::Get().GetWindow().SetTitle(std::format("WorldWorkshop3D | WASD camera | active {} / {} | loaded {} unloaded {}", stats.Active, world->GetPolicy().MaxResidentCells, stats.Activations, stats.Unloads));
                    if (std::getenv("ZAMIN_WORLD_AUTOTOUR")) ZM_INFO("World tour frame {}: active={} admitted={} activated={} unloaded={} failed={}", m_Frame, stats.Active, stats.Admitted, stats.Activations, stats.Unloads, stats.Failed);
                    if (std::getenv("ZAMIN_WORLD_AUTOTOUR")) ZM_INFO("World residency frame {}: rss={} gpu={} caches={} pinned={} evicted={} pressure={} navTiles={} navCells={} navErrors={}", m_Frame,
                                                                     stats.Residency.Process.Resident, stats.Residency.RequestedGPUBytes, stats.Residency.Entries, stats.Residency.Pinned, stats.CacheEvictions,
                                                                     stats.MemoryPressure, stats.NavigationTiles, stats.NavigationCells, stats.NavigationError.empty() ? 0 : 1);
                }
    }
private:
    uint32_t m_Frame = 0;
};
ZM_REGISTER_SCRIPT(WorldExplorer3D)
