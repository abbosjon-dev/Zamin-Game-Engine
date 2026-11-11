#pragma once
#include <Zamin/Zamin.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <cstdlib>
#include <cmath>
// A live navigation-region barrier, partial path exhaustion and same-intent recovery after opening.
class WorldRecoveryProbe
{
public:
    void Tick(Zamin::Scene& scene, Zamin::WorldStreaming& world, bool three, int frame, float dt)
    {
        using namespace Zamin;
        const bool qa = std::getenv("ZAMIN_WORLD_RECOVERY_TEST") != nullptr;
        const bool key = Input::IsKeyPressed(Key::R);
        if (key && !m_Key) m_Enabled = !m_Enabled;
        m_Key = key;
        if (!qa && !m_Enabled)
        {
            if (auto actor = scene.GetEntityByUUID(m_Actor)) scene.DestroyEntity(actor);
            if (auto door = scene.GetEntityByUUID(m_Door)) scene.DestroyEntity(door);
            m_Ready = m_Exhausted = m_Graph = m_Opened = false;
            m_Actor = m_Door = UUID(0);
            m_Elapsed = 0;
            return;
        }
        auto atlas = world.GetNavigationAtlas();
        if (!atlas || !atlas->GetCellCount() || frame < 60) return;
        if (!m_Ready)
        {
            const glm::vec2 from(8, 11), to(14, 11);
            if (!atlas->CanWalkSegment(from, to))
            {
                if (qa && frame > 120) throw std::runtime_error("Recovery arena is not walkable");
                return;
            }
            const auto route = atlas->FindRoute(from, to);
            if (route.Points.empty()) throw std::runtime_error("Recovery route missing");
            const float floor = route.Points.front().Height;
            Scene staged;
            auto actor = staged.CreateEntity("Recovery walker");
            m_Actor = actor.GetUUID();
            actor.GetTransform().Translation = three ? glm::vec3(from.x, floor + .03f, from.y) : glm::vec3(from, 6);
            auto& a = actor.AddComponent<NavAgentComponent>();
            a.WorldNavigation = true;
            a.Speed = 2;
            a.Acceleration = 0;
            a.RepathInterval = 0;
            a.RecoverySettings.Enabled = true;
            a.RecoverySettings.StallSeconds = .2f;
            a.RecoverySettings.CooldownSeconds = .15f;
            a.RecoverySettings.MaxRetries = 2;
            auto& host = actor.AddComponent<NativeScriptComponent>();
            host.ClassName = "Zamin.VisualGraph";
            host.Fields["Graph"].Set(std::string("Scripts/RecoveryMonitor.zgraph"));
            if (three)
            {
                auto& cc = actor.AddComponent<CharacterController3DComponent>();
                cc.Radius = .25f;
                cc.Height = 1.2f;
                auto visual = staged.CreateEntity("Recovery walker visual");
                staged.SetParent(visual, actor, false);
                visual.GetTransform().Translation = { 0, .5f, 0 };
                visual.GetTransform().Scale = glm::vec3(.7f);
                auto& mesh = visual.AddComponent<MeshRendererComponent>();
                mesh.Type = PrimitiveMesh::Sphere;
                mesh.Mat.Albedo = { 1, .75f, .1f, 1 };
            }
            else
            {
                actor.GetTransform().Scale = glm::vec3(.7f);
                actor.AddComponent<SpriteRendererComponent>().Color = { 1, .75f, .1f, 1 };
            }
            auto door = staged.CreateEntity("Recovery navigation barrier");
            m_Door = door.GetUUID();
            door.GetTransform().Translation = three ? glm::vec3(11, 50, 11) : glm::vec3(11, 11, 0);
            door.AddComponent<NavObstacleComponent>().Size = three ? glm::vec3(1, 100, 512) : glm::vec3(1, 512, 1);
            auto fence = staged.CreateEntity("Recovery barrier visual");
            staged.SetParent(fence, door, false);
            if (three)
            {
                fence.GetTransform().Translation = { 0, floor + 1.5f - 50, 0 };
                fence.GetTransform().Scale = { 1, 3, 128 };
                auto& mesh = fence.AddComponent<MeshRendererComponent>();
                mesh.Type = PrimitiveMesh::Cube;
                mesh.Mat.Albedo = { .95f, .15f, .2f, 1 };
            }
            else
            {
                fence.GetTransform().Translation = { 0, 0, 5 };
                fence.GetTransform().Scale = { 1, 128, 1 };
                fence.AddComponent<SpriteRendererComponent>().Color = { .95f, .15f, .2f, 1 };
            }
            std::string error;
            scene.ImportStagedCell(staged, error);
            if (!error.empty()) throw std::runtime_error(error);
            if (!scene.SetNavDestination(scene.GetEntityByUUID(m_Actor), three ? glm::vec3(to.x, 0, to.y) : glm::vec3(to, 0))) throw std::runtime_error("Recovery query admission failed");
            m_Ready = true;
            m_Start = frame;
            m_BlockedRevision = world.GetNavigationAtlas()->GetRevision();
            ZM_INFO("Recovery oracle: barrier created and destination accepted, controller={}", three);
        }
        m_Elapsed += std::isfinite(dt) ? std::clamp(dt, 0.f, .1f) : 0.f;
        auto actor = scene.GetEntityByUUID(m_Actor);
        if (!actor) throw std::runtime_error("Recovery actor disappeared");
        auto& a = actor.GetComponent<NavAgentComponent>();
        auto* host = dynamic_cast<VisualGraphHost*>(actor.GetComponent<NativeScriptComponent>().Instance);
        if (!host || !host->VM.Error().empty()) throw std::runtime_error("Recovery monitor graph failed");
        const bool retry = Input::IsKeyPressed(Key::T);
        if (retry && !m_RetryKey) host->Emit("Message", { { "Text", "Retry navigation" } });
        m_RetryKey = retry;
        if (a.Recovery.IsExhausted() && !m_Exhausted)
        {
            if (!a.HasDestination || a.Recovery.GetRetries() != 2 || a.IsMoving() || glm::length(a.Velocity) > .001f) throw std::runtime_error("Recovery exhaustion did not stop safely");
            m_Exhausted = true;
            ZM_INFO("Recovery oracle: retries exhausted, intent retained and actor stopped");
        }
        m_Graph |= host->VM.Variables().at("Exhausted").get<bool>();
        if (qa && frame == m_Start + 30) m_StableBuilds = world.GetStats().NavigationRebuilds;
        if (qa && frame == m_Start + 100)
        {
            if (!m_Exhausted || !m_Graph || world.GetStats().NavigationRebuilds != m_StableBuilds) throw std::runtime_error("Recovery blocker/graph/carve stability oracle failed");
            ZM_INFO("Recovery oracle: stable carving, no mosaic rebuild feedback");
        }
        if (!m_Opened && (qa ? frame >= m_Start + 150 : m_Elapsed >= 5.f))
        {
            scene.GetEntityByUUID(m_Door).SetActive(false);
            m_Opened = true;
            ZM_INFO("Recovery oracle: barrier opened without resetting destination");
        }
        if (qa && frame == 400)
        {
            if (!m_Exhausted || !m_Graph || !m_Opened || !a.Arrived || a.PathStatus != NavPathStatus::Complete || a.Recovery.IsExhausted() || world.GetNavigationAtlas()->GetRevision() == m_BlockedRevision)
                throw std::runtime_error("Recovery arrival / refreshed topology oracle failed");
            ZM_INFO("Recovery oracle: exhausted, graph telemetry, same-intent topology recovery and arrival passed");
        }
        if (frame % 60 == 0) ZM_INFO("Recovery runtime: phase={} retries={} status={} opened={}", NavRecovery::Name(a.Recovery.GetPhase()), a.Recovery.GetRetries(), NavPathStatusName(a.PathStatus), m_Opened);
    }
private:
    bool m_Key = false, m_RetryKey = false, m_Enabled = false, m_Ready = false, m_Exhausted = false, m_Graph = false, m_Opened = false;
    float m_Elapsed = 0;
    int m_Start = 0;
    uint64_t m_BlockedRevision = 0, m_StableBuilds = 0;
    Zamin::UUID m_Actor{ 0 }, m_Door{ 0 };
};
