#pragma once
#include <Zamin/Zamin.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <array>
#include <cstdlib>
#include <limits>
// A real Scene/CharacterController/graph demo, included in both native source bundles.
class WorldCrowdProbe
{
public:
    void Tick(Zamin::Scene& scene, Zamin::WorldStreaming& world, bool three, int frame)
    {
        using namespace Zamin;
        const bool qa = std::getenv("ZAMIN_WORLD_CROWD_TEST") != nullptr;
        const bool key = Input::IsKeyPressed(Key::C);
        if (key && !m_Key) m_Enabled = !m_Enabled;
        m_Key = key;
        if (!qa && !m_Enabled)
        {
            for (auto id : m_Actors)
                if (auto e = scene.GetEntityByUUID(id)) scene.DestroyEntity(e);
            m_Actors.clear();
            m_Ready = m_Neighbors = m_Graph = m_FirstArrival = m_Second = m_SecondArrival = false;
            m_Separation = std::numeric_limits<float>::infinity();
            return;
        }
        auto atlas = world.GetNavigationAtlas();
        if (!atlas || !atlas->GetCellCount() || frame < 60) return;
        if (!m_Ready)
        {
            bool found = false;
            for (int y : { 11, 13, 19, 21 })
                for (int x : { 11, 13, 19, 21 })
                {
                    const glm::vec2 centre(x, y);
                    bool clear = true;
                    for (auto offset : Offsets())
                        clear &= atlas->CanWalkSegment(centre + offset, centre - offset);
                    if (clear && !found)
                    {
                        m_Centre = centre;
                        found = true;
                    }
                }
            if (!found)
            {
                if (qa && frame > 120) throw std::runtime_error("No walkable crowd arena");
                return;
            }
            Scene staged;
            const std::array<glm::vec4, 4> colors{ glm::vec4(.1f, .8f, 1, 1), glm::vec4(1, .3f, .4f, 1), glm::vec4(.6f, .3f, 1, 1), glm::vec4(1, .8f, .1f, 1) };
            for (size_t i = 0; i < 4; ++i)
            {
                auto actor = staged.CreateEntity("Crowd walker " + std::to_string(i + 1));
                m_Actors.push_back(actor.GetUUID());
                const auto from = m_Centre + Offsets()[i];
                const auto route = atlas->FindRoute(from, m_Centre - Offsets()[i]);
                if (route.Points.empty()) throw std::runtime_error("Crowd spawn is not walkable");
                const float floor = route.Points.front().Height;
                actor.GetTransform().Translation = three ? glm::vec3(from.x, floor + .03f, from.y) : glm::vec3(from, 6);
                auto& a = actor.AddComponent<NavAgentComponent>();
                a.WorldNavigation = a.CrowdAvoidance = true;
                a.Speed = 2;
                a.Acceleration = 0;
                a.Radius = .3f;
                a.RepathInterval = 0;
                a.CrowdHeight = 1.2f;
                auto& script = actor.AddComponent<NativeScriptComponent>();
                script.ClassName = "Zamin.VisualGraph";
                script.Fields["Graph"].Set(std::string("Scripts/CrowdMonitor.zgraph"));
                if (three)
                {
                    auto& cc = actor.AddComponent<CharacterController3DComponent>();
                    cc.Radius = .25f;
                    cc.Height = 1.2f;
                    auto visual = staged.CreateEntity("Crowd visual " + std::to_string(i + 1));
                    staged.SetParent(visual, actor, false);
                    visual.GetTransform().Translation = { 0, .5f, 0 };
                    visual.GetTransform().Scale = glm::vec3(.7f);
                    auto& mesh = visual.AddComponent<MeshRendererComponent>();
                    mesh.Type = PrimitiveMesh::Sphere;
                    mesh.Mat.Albedo = colors[i];
                }
                else
                {
                    actor.GetTransform().Scale = glm::vec3(.7f);
                    actor.AddComponent<SpriteRendererComponent>().Color = colors[i];
                }
            }
            std::string error;
            scene.ImportStagedCell(staged, error);
            if (!error.empty()) throw std::runtime_error(error);
            Destinations(scene, three, false);
            m_Ready = true;
            ZM_INFO("Crowd oracle: spawned 4 graph agents, controller={} arena={},{}", three, m_Centre.x, m_Centre.y);
        }
        bool all = true;
        for (size_t i = 0; i < m_Actors.size(); ++i)
        {
            auto e = scene.GetEntityByUUID(m_Actors[i]);
            if (!e) throw std::runtime_error("Crowd actor disappeared");
            const auto& a = e.GetComponent<NavAgentComponent>();
            m_Neighbors |= a.CrowdNeighbors > 0;
            if (a.CrowdLimited) throw std::runtime_error("Four-agent crowd unexpectedly saturated");
            all &= a.Arrived;
            for (size_t j = i + 1; j < m_Actors.size(); ++j)
            {
                auto p = scene.GetWorldPosition(e), q = scene.GetWorldPosition(scene.GetEntityByUUID(m_Actors[j]));
                if (three) p.y = q.y = 0;
                m_Separation = std::min(m_Separation, glm::length(p - q));
            }
            auto* host = dynamic_cast<VisualGraphHost*>(e.GetComponent<NativeScriptComponent>().Instance);
            if (!host || !host->VM.Error().empty()) throw std::runtime_error("Crowd graph failed");
            m_Graph |= host->VM.Variables().at("Neighbors").get<double>() > 0;
        }
        if (all && !m_FirstArrival)
        {
            m_FirstArrival = true;
            ZM_INFO("Crowd oracle: all first destinations reached");
            for (auto id : m_Actors) scene.GetEntityByUUID(id).GetComponent<NavAgentComponent>().Stop(true);
            m_RestartFrame = frame + 30;
        }
        if (m_FirstArrival && !m_Second && frame >= m_RestartFrame)
        {
            Destinations(scene, three, true);
            m_Second = true;
            ZM_INFO("Crowd oracle: stop and reverse destinations accepted");
        }
        if (all && m_Second && frame > m_RestartFrame + 1) m_SecondArrival = true;
        if (qa && frame == 420)
        {
            if (!m_FirstArrival || !m_SecondArrival || !m_Neighbors || !m_Graph || m_Separation < .48f) throw std::runtime_error("Crowd completion / separation / graph oracle failed");
            ZM_INFO("Crowd oracle: 2 phases, graph telemetry and separation passed; minimum={}", m_Separation);
        }
        if (frame % 60 == 0) ZM_INFO("Crowd runtime: actors=4 neighbors={} minimum={} first={} returned={}", m_Neighbors, m_Separation, m_FirstArrival, m_SecondArrival);
    }
private:
    bool m_Key = false, m_Enabled = false, m_Ready = false, m_Neighbors = false, m_Graph = false, m_FirstArrival = false, m_Second = false, m_SecondArrival = false;
    int m_RestartFrame = 0;
    float m_Separation = std::numeric_limits<float>::infinity();
    glm::vec2 m_Centre{ 0 };
    std::vector<Zamin::UUID> m_Actors;
    static std::array<glm::vec2, 4> Offsets() { return { glm::vec2(-3, 0), glm::vec2(3, 0), glm::vec2(0, -3), glm::vec2(0, 3) }; }
    void Destinations(Zamin::Scene& scene, bool three, bool returning)
    {
        for (size_t i = 0; i < m_Actors.size(); ++i)
        {
            const auto p = m_Centre + Offsets()[i] * (returning ? 1.f : -1.f);
            if (!scene.SetNavDestination(scene.GetEntityByUUID(m_Actors[i]), three ? glm::vec3(p.x, 0, p.y) : glm::vec3(p, 0))) throw std::runtime_error("Crowd destination admission failed");
        }
    }
};
