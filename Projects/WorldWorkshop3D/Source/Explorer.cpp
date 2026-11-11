#include <Zamin/Zamin.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <cstdlib>
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
            const std::array<glm::vec2, 5> route = { glm::vec2(16, 9), glm::vec2(80, 9), glm::vec2(80, true ? 80 : 40), glm::vec2(1000, 1000), glm::vec2(16, 9) };
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
                    Application::Get().GetWindow().SetTitle(std::format("WorldWorkshop3D | WASD camera | active {} / {} | loaded {} unloaded {}", stats.Active, world->GetPolicy().MaxResidentCells, stats.Activations, stats.Unloads));
                    if (std::getenv("ZAMIN_WORLD_AUTOTOUR")) ZM_INFO("World tour frame {}: active={} admitted={} activated={} unloaded={} failed={}", m_Frame, stats.Active, stats.Admitted, stats.Activations, stats.Unloads, stats.Failed);
                }
    }
private:
    uint32_t m_Frame = 0;
};
ZM_REGISTER_SCRIPT(WorldExplorer3D)
