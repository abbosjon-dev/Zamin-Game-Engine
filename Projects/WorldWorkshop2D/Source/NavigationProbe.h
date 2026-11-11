#pragma once
#include <Zamin/Zamin.h>
#include <Zamin/Scene/WorldStreaming.h>
#include <cstdlib>
#include <algorithm>

// Included in each sample's source bundle so native exports remain self-contained.
class NavigationProbe
{
public:
    void Tick(Zamin::Scene& scene, Zamin::WorldStreaming& world, bool threeD, int frame, float dt)
    {
        using namespace Zamin;
        const bool qa = std::getenv("ZAMIN_WORLD_NAVIGATION_TEST") != nullptr;
        const bool key = Input::IsKeyPressed(Key::N);
        if (key && !m_Key)
        {
            m_Enabled = !m_Enabled;
            if (!m_Enabled)
            {
                Hide(scene);
                m_Ready = false;
            }
        }
        m_Key = key;
        if (!qa && !m_Enabled) return;
        const auto atlas = world.GetNavigationAtlas();
        const uint64_t revision = atlas ? atlas->GetRevision() : 0;
        if (m_Ready && revision != m_Revision)
        {
            if (m_Follower.Advance(revision, 10) != NavRouteFollower::State::Stale) throw std::runtime_error("Navigation stale route did not stop");
            if (!m_StaleLogged)
            {
                ZM_INFO("Navigation oracle: stale topology stopped actor");
                m_StaleLogged = true;
            }
            Hide(scene);
            m_Ready = false;
        }
        if (qa && frame == 190)
        {
            if (!atlas || atlas->GetActiveLinks() != 0 || m_Ready) throw std::runtime_error("Disabled portal did not invalidate route");
            m_DisabledSeen = true;
        }
        if (qa && frame == 270)
        {
            if (world.GetStats().Active || (atlas && atlas->GetCellCount())) throw std::runtime_error("Navigation empty region retained tiles");
            m_EmptySeen = true;
        }
        if (!atlas || atlas->GetCellCount() == 0) return;
        if (frame < 60 || (qa && frame > 180 && frame < 210)) return;
        if (!m_LinkCreated)
        {
            NavRoute route;
            for (int y : { 9, 7, 11, 5 })
            {
                route = atlas->FindRoute({ 8, float(y) }, { 48, float(y) });
                if (route.Status == NavPathStatus::Complete && route.Points.size() >= 8) break;
            }
            if (route.Status != NavPathStatus::Complete || route.Points.size() < 8)
            {
                if (qa && frame > 120) throw std::runtime_error("Navigation demo cannot find cross-cell route");
                return;
            }
            m_From = route.Points.front().Position;
            m_To = route.Points.back().Position;
            m_Link = { 9001, route.Points[route.Points.size() / 3].Position, route.Points[route.Points.size() * 2 / 3].Position, 1, true, true };
            std::string error;
            if (!world.SetNavigationLinks({ m_Link }, error)) throw std::runtime_error(error);
            m_LinkCreated = true;
            return;
        }
        if (qa && frame == 180)
        {
            auto link = m_Link;
            link.Enabled = false;
            std::string error;
            if (!world.SetNavigationLinks({ link }, error)) throw std::runtime_error(error);
            return;
        }
        if (qa && frame == 210)
        {
            std::string error;
            if (!world.SetNavigationLinks({ m_Link }, error)) throw std::runtime_error(error);
            return;
        }
        if (!m_Ready)
        {
            if (atlas->GetActiveLinks() == 0) return;
            const auto route = atlas->FindRoute(m_From, m_To);
            if (route.Status != NavPathStatus::Complete || !m_Follower.SetRoute(route)) return;
            m_Revision = revision;
            m_Ready = true;
            m_Wait = 0;
            if (frame >= 300) m_ReturnSeen = true;
            size_t i = 0;
            for (const auto& p : route.Points)
            {
                auto entity = Marker(scene, i++, threeD);
                entity.GetTransform().Translation = Position(p, threeD);
                entity.GetTransform().Scale = glm::vec3(p.IncomingLink ? .8f : .32f);
                Color(entity, threeD, p.IncomingLink ? glm::vec4(.8f, .15f, 1, 1) : glm::vec4(.1f, 1, .7f, 1));
            }
            auto actor = scene.GetEntityByUUID(m_Actor);
            if (!actor)
            {
                actor = scene.CreateEntity("Navigation kinematic actor");
                if (threeD) actor.AddComponent<MeshRendererComponent>().Type = PrimitiveMesh::Sphere;
                else actor.AddComponent<SpriteRendererComponent>();
                m_Actor = actor.GetUUID();
            }
            actor.GetTransform().Scale = glm::vec3(1.2f);
            Color(actor, threeD, { 1, .65f, .08f, 1 });
            ZM_INFO("Navigation oracle: route ready points={} cells={} links={} revision={}", route.Points.size(), atlas->GetCellCount(), atlas->GetActiveLinks(), revision);
        }
        auto state = m_Follower.Advance(revision, qa ? .5f : std::clamp(dt, 0.f, .1f) * 12);
        if (state == NavRouteFollower::State::AwaitingTraversal)
        {
            if (++m_Wait >= 15)
            {
                if (!m_Follower.CompleteTraversal(revision, m_Follower.GetPendingLink())) throw std::runtime_error("Navigation traversal completion failed");
                m_Wait = 0;
                m_Traversed = true;
                ZM_INFO("Navigation oracle: explicit portal traversal completed");
            }
        }
        if (auto actor = scene.GetEntityByUUID(m_Actor)) actor.GetTransform().Translation = Position(m_Follower.GetPosition(), threeD) + (threeD ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1));
        if (state == NavRouteFollower::State::Arrived && !m_ArrivedLogged)
        {
            m_ArrivedLogged = true;
            ZM_INFO("Navigation oracle: actor arrived after traversal");
        }
        if (qa && frame == 400)
        {
            if (!m_Traversed || !m_StaleLogged || !m_ArrivedLogged || !m_Ready || !m_DisabledSeen || !m_EmptySeen || !m_ReturnSeen) throw std::runtime_error("Navigation workflow oracle incomplete");
            ZM_INFO("Navigation oracle: disable, unload, return and replan passed");
        }
    }
private:
    bool m_Key = false, m_Enabled = false, m_LinkCreated = false, m_Ready = false;
    bool m_Traversed = false, m_StaleLogged = false, m_ArrivedLogged = false;
    bool m_DisabledSeen = false, m_EmptySeen = false, m_ReturnSeen = false;
    int m_Wait = 0;
    uint64_t m_Revision = 0;
    glm::vec2 m_From{ 0 }, m_To{ 0 };
    Zamin::NavTraversalLink m_Link;
    Zamin::NavRouteFollower m_Follower;
    std::vector<Zamin::UUID> m_Markers;
    Zamin::UUID m_Actor{ 0 };
    static glm::vec3 Position(const Zamin::NavRoutePoint& p, bool threeD)
    {
        return threeD ? glm::vec3(p.Position.x, p.Height + .6f, p.Position.y) : glm::vec3(p.Position, 5);
    }
    static void Color(Zamin::Entity entity, bool threeD, glm::vec4 color)
    {
        if (threeD) entity.GetComponent<Zamin::MeshRendererComponent>().Mat.Albedo = color;
        else entity.GetComponent<Zamin::SpriteRendererComponent>().Color = color;
    }
    Zamin::Entity Marker(Zamin::Scene& scene, size_t index, bool threeD)
    {
        if (index < m_Markers.size())
            if (auto entity = scene.GetEntityByUUID(m_Markers[index])) return entity;
        auto entity = scene.CreateEntity("Navigation route marker");
        if (threeD) entity.AddComponent<Zamin::MeshRendererComponent>().Type = Zamin::PrimitiveMesh::Sphere;
        else entity.AddComponent<Zamin::SpriteRendererComponent>();
        if (index < m_Markers.size()) m_Markers[index] = entity.GetUUID();
        else m_Markers.push_back(entity.GetUUID());
        return entity;
    }
    void Hide(Zamin::Scene& scene)
    {
        for (auto id : m_Markers)
            if (auto entity = scene.GetEntityByUUID(id)) entity.GetTransform().Scale = glm::vec3(0);
        if (auto actor = scene.GetEntityByUUID(m_Actor)) actor.GetTransform().Scale = glm::vec3(0);
    }
};
