#pragma once
#include <Zamin/Zamin.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <algorithm>
#include <cstdlib>
class WorldAgentProbe
{
public:
    void Tick(Zamin::Scene& scene, Zamin::WorldStreaming& world, bool threeD, int frame)
    {
        using namespace Zamin;
        const bool qa = std::getenv("ZAMIN_WORLD_AGENT_TEST") != nullptr;
        const bool key = Input::IsKeyPressed(Key::B);
        if (key && !m_Key) m_Enabled = !m_Enabled;
        m_Key = key;
        auto actor = scene.FindEntityByName("World traversal actor");
        if (!actor)
        {
            if (qa) throw std::runtime_error("World agent missing");
            return;
        }
        auto& a = actor.GetComponent<NavAgentComponent>();
        auto* host = dynamic_cast<VisualGraphHost*>(actor.GetComponent<NativeScriptComponent>().Instance);
        if (!qa && !m_Enabled)
        {
            if (m_Running)
            {
                a.Stop(true);
                Freeze(scene, actor, threeD, true);
                m_Running = false;
            }
            return;
        }
        const auto atlas = world.GetNavigationAtlas();
        if (!host || !host->VM.Error().empty()) throw std::runtime_error("World traversal graph failed");
        if (qa && frame == 180)
        {
            auto disabled = m_Link;
            disabled.Enabled = false;
            std::string error;
            if (!world.SetNavigationLinks({ disabled }, error)) throw std::runtime_error(error);
            m_Running = false;
            ZM_INFO("Agent oracle: portal disabled");
        }
        if (qa && frame == 190)
        {
            if (atlas && atlas->GetActiveLinks()) throw std::runtime_error("Disabled agent link still active");
            m_Disabled = true;
        }
        if (qa && frame == 210)
        {
            std::string error;
            if (!world.SetNavigationLinks({ m_Link }, error)) throw std::runtime_error(error);
        }
        if (!atlas || !atlas->GetCellCount())
        {
            Freeze(scene, actor, threeD, true);
            m_Running = false;
            if (qa && frame == 270)
            {
                if (a.PendingTraversalLink || a.PathStatus != NavPathStatus::Failed || glm::length(a.Velocity) > .001f) throw std::runtime_error("Unloaded agent did not stop");
                m_Unloaded = true;
                ZM_INFO("Agent oracle: unloaded atlas stopped motion");
            }
            return;
        }
        if (frame < 60 || (qa && frame >= 180 && frame <= 210)) return;
        if (!m_LinkCreated)
        {
            const auto route = atlas->FindRoute({ 8, 9 }, { 48, 9 });
            if (route.Status != NavPathStatus::Complete || route.Points.size() < 8) return;
            m_Start = route.Points.front();
            m_Link = { 8001, route.Points[route.Points.size() / 3].Position, route.Points[route.Points.size() * 2 / 3].Position, 1, true, true };
            std::string error;
            if (!world.SetNavigationLinks({ m_Link }, error)) throw std::runtime_error(error);
            m_LinkCreated = true;
            return;
        }
        const auto stats = world.GetStats();
        if (!m_Running && atlas->GetActiveLinks() && stats.Active == stats.Desired)
        {
            Freeze(scene, actor, threeD, false);
            actor.GetTransform().Translation = threeD ? glm::vec3(m_Start.Position.x, m_Start.Height + .02f, m_Start.Position.y) : glm::vec3(m_Start.Position, 5);
            host->Emit("Message", { { "Text", "Begin navigation" } });
            if (!host->VM.Variables().at("Started").get<bool>()) throw std::runtime_error("Graph destination failed");
            m_Running = true;
            if (frame >= 300) m_Returned = true;
            ZM_INFO("Agent oracle: graph destination accepted, controller={}", threeD);
        }
        if (a.TraversalCompletions && !m_Traversed)
        {
            m_Traversed = true;
            ZM_INFO("Agent oracle: graph completed physical traversal");
        }
        if (a.Arrived && m_Traversed && !m_Arrived)
        {
            m_Arrived = true;
            ZM_INFO("Agent oracle: actual actor arrived");
        }
        if (qa && frame == 400)
        {
            if (!m_Traversed || !m_Arrived || !m_Disabled || !m_Unloaded || !m_Returned || !a.Arrived || a.TraversalCompletions < 3 || !host->VM.Variables().at("Success").get<bool>()) throw std::runtime_error("Agent workflow incomplete");
            ZM_INFO("Agent oracle: graph, controller, disable, unload and return passed");
        }
    }
private:
    bool m_Key = false, m_Enabled = false, m_Running = false, m_LinkCreated = false;
    bool m_Traversed = false, m_Arrived = false, m_Disabled = false, m_Unloaded = false, m_Returned = false;
    Zamin::NavTraversalLink m_Link;
    Zamin::NavRoutePoint m_Start;
    static void Freeze(Zamin::Scene& scene, Zamin::Entity actor, bool threeD, bool frozen)
    {
        if (threeD)
        {
            auto& cc = actor.GetComponent<Zamin::CharacterController3DComponent>();
            cc.GravityScale = frozen ? 0 : 1;
            cc.VerticalVelocity = 0;
            if (frozen) cc.Move(glm::vec3(0));
            if (auto body = scene.FindEntityByName("World traversal body")) body.GetTransform().Scale = glm::vec3(frozen ? 0 : .8f);
        }
        else actor.GetTransform().Scale = glm::vec3(frozen ? 0 : 1);
    }
};
