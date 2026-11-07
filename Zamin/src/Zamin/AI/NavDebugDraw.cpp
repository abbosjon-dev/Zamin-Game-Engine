// Navigation debug drawing shared by the editor's Navigatsiya gizmo and the game's DebugDraw
// (--debug-nav): grids, agents, obstacles, sight cones and patrol rounds.
#include "Zamin/AI/NavDebugDraw.h"

#include "Zamin/AI/NavGrid.h"
#include "Zamin/AI/Sight.h"
#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Scene.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace Zamin {

    // ------------------------------------------------------------------ the DebugDraw canvas
    void DebugDrawNavCanvas::Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
    {
        DebugDraw::Line(from, to, color, 0.0f, true);
    }

    void DebugDrawNavCanvas::Quad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color)
    {
        DebugDraw::Quad(p0, p1, p2, p3, color, 0.0f, true);
    }

    void DebugDrawNavCanvas::Dot(const glm::vec3& position, float pixels, const glm::vec4& color)
    {
        DebugDraw::Point(position, color, 0.0f, true, pixels);
    }

    float DebugDrawNavCanvas::PixelSize(const glm::vec3& position) const
    {
        const float scale = std::abs(m_Projection[1][1]) * std::max(m_ViewportHeight, 1.0f);
        if (!(scale > 1e-9f) || !std::isfinite(scale))
            return 0.02f;
        if (m_Projection[3][3] != 0.0f)
            return 2.0f / scale; // orthographic
        const float depth = std::max(-(m_View * glm::vec4(position, 1.0f)).z, 1e-3f);
        return 2.0f * depth / scale;
    }

    void DebugDrawNavCanvas::Text(const glm::vec3& position, std::string_view text, const glm::vec4& color)
    {
        DebugDraw::Text(position, text, color, 0.0f, false, 0.0f);
    }

    namespace {

        bool Drawn(const NavDebugOptions& o, entt::entity e) { return !o.Filter || o.Filter(e); }
        bool IsSelected(const NavDebugOptions& o, entt::entity e) { return o.Selected && o.Selected(e); }

        void Cross(NavDebugCanvas& canvas, const glm::vec3& p, float size, const glm::vec4& color)
        {
            canvas.Line(p - glm::vec3(size, 0, 0), p + glm::vec3(size, 0, 0), color);
            canvas.Line(p - glm::vec3(0, size, 0), p + glm::vec3(0, size, 0), color);
            canvas.Line(p - glm::vec3(0, 0, size), p + glm::vec3(0, 0, size), color);
        }

        void WireCircle(NavDebugCanvas& canvas, const glm::vec3& center, const glm::vec3& a, const glm::vec3& b, float radius,
                        const glm::vec4& color, int segments = 40)
        {
            glm::vec3 previous = center + a * radius;
            for (int i = 1; i <= segments; i++)
            {
                const float t = glm::two_pi<float>() * (float)i / (float)segments;
                const glm::vec3 p = center + (a * std::cos(t) + b * std::sin(t)) * radius;
                canvas.Line(previous, p, color);
                previous = p;
            }
        }

        void WireBox(NavDebugCanvas& canvas, const glm::mat4& transform, const glm::vec4& color)
        {
            glm::vec3 c[8];
            for (int i = 0; i < 8; i++)
                c[i] = glm::vec3(transform * glm::vec4((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f));
            static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                                              { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for (const auto& edge : edges)
                canvas.Line(c[edge[0]], c[edge[1]], color);
        }

        // An arrow head in the middle of a segment, pointing along it (`up`: the plane it lies in).
        void Chevron(NavDebugCanvas& canvas, const glm::vec3& from, const glm::vec3& to, const glm::vec3& up, float size, const glm::vec4& color)
        {
            glm::vec3 dir = to - from;
            const float length = glm::length(dir);
            if (!(length > size * 2.5f))
                return;
            dir /= length;
            glm::vec3 side = glm::cross(up, dir);
            if (glm::dot(side, side) < 1e-8f)
                return;
            side = glm::normalize(side);
            const glm::vec3 tip = (from + to) * 0.5f + dir * size * 0.5f;
            canvas.Line(tip, tip - dir * size + side * size * 0.6f, color);
            canvas.Line(tip, tip - dir * size - side * size * 0.6f, color);
        }

        // A run of cells of row y ([x0, x1)) as one quad: on the XY plane, or flat on a 3D grid's floor.
        void CellRun(NavDebugCanvas& canvas, const NavGrid& grid, int x0, int x1, int y, bool flat3D, float height, const glm::vec4& color)
        {
            const float inset = grid.GetCellSize() * 0.04f;
            const glm::vec2 min = grid.GridToWorld(glm::vec2((float)x0, (float)y)) + inset;
            const glm::vec2 max = grid.GridToWorld(glm::vec2((float)x1, (float)(y + 1))) - inset;
            if (flat3D)
            {
                const float h = height + 0.03f;
                canvas.Quad({ min.x, h, min.y }, { max.x, h, min.y }, { max.x, h, max.y }, { min.x, h, max.y }, color);
            }
            else
                canvas.Quad({ min.x, min.y, 0.01f }, { max.x, min.y, 0.01f }, { max.x, max.y, 0.01f }, { min.x, max.y, 0.01f }, color);
        }

        glm::vec3 PlaneUp(int dimension) { return dimension == 3 ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f); }

        void DrawGrids(Scene& scene, NavDebugCanvas& canvas, const NavDebugOptions& o)
        {
            for (auto e : scene.GetAllEntitiesWith<NavGrid2DComponent>())
            {
                Entity entity{ e, &scene };
                const auto& c = entity.GetComponent<NavGrid2DComponent>();
                if (!Drawn(o, e) || !c.Baked || c.Grid.IsEmpty())
                    continue;
                const bool selected = IsSelected(o, e);
                const bool walkable = (selected || o.WalkableCells) && c.Grid.GetCellCount() <= o.MaxWalkableCells;
                NavDebugDraw::DrawGridCells(canvas, c.Grid, false, walkable, selected || !o.Selected ? 1.0f : 0.6f);
                const glm::vec2 min = c.Grid.GetOrigin(), max = min + c.Grid.GetSize();
                const glm::vec4 frame(0.4f, 0.9f, 1.0f, selected ? 0.9f : 0.45f);
                canvas.Line({ min.x, min.y, 0.0f }, { max.x, min.y, 0.0f }, frame);
                canvas.Line({ max.x, min.y, 0.0f }, { max.x, max.y, 0.0f }, frame);
                canvas.Line({ max.x, max.y, 0.0f }, { min.x, max.y, 0.0f }, frame);
                canvas.Line({ min.x, max.y, 0.0f }, { min.x, min.y, 0.0f }, frame);
            }
            for (auto e : scene.GetAllEntitiesWith<NavGrid3DComponent>())
            {
                Entity entity{ e, &scene };
                const auto& c = entity.GetComponent<NavGrid3DComponent>();
                if (!Drawn(o, e))
                    continue;
                const bool selected = IsSelected(o, e);
                // The area's box (also before it is baked: the editor shows where it will be).
                const glm::vec3 center = scene.GetWorldPosition(entity) + c.Offset;
                const glm::vec3 size = glm::max(c.Size, glm::vec3(0.0f));
                glm::mat4 box(1.0f);
                box[0][0] = size.x;
                box[1][1] = size.y;
                box[2][2] = size.z;
                box[3] = glm::vec4(center, 1.0f);
                WireBox(canvas, box, glm::vec4(0.4f, 0.9f, 1.0f, selected ? 0.8f : 0.35f));
                if (c.Baked && !c.Grid.IsEmpty())
                {
                    const bool walkable = (selected || o.WalkableCells) && c.Grid.GetCellCount() <= o.MaxWalkableCells;
                    NavDebugDraw::DrawGridCells(canvas, c.Grid, true, walkable, selected || !o.Selected ? 1.0f : 0.6f);
                }
            }
        }

        void DrawAgents(Scene& scene, NavDebugCanvas& canvas, const NavDebugOptions& o)
        {
            for (auto e : scene.GetAllEntitiesWith<NavAgentComponent>())
            {
                if (!Drawn(o, e))
                    continue;
                Entity entity{ e, &scene };
                const auto& a = entity.GetComponent<NavAgentComponent>();
                const bool selected = IsSelected(o, e);
                const glm::vec4 color = a.IsPathPartial() ? glm::vec4(1.0f, 0.75f, 0.3f, 1.0f) : glm::vec4(0.35f, 0.9f, 1.0f, 1.0f);
                const bool flat3D = Scene::GetNavGridDimension(scene.GetNavAgentGrid(entity)) == 3;
                const glm::vec3 lift(0.0f, flat3D ? 0.08f : 0.0f, 0.0f);
                const glm::vec3 position = scene.GetWorldPosition(entity);
                // The path ahead, from where the agent is: its corners and the end.
                if (a.HasPath() && a.PathIndex >= 1 && a.PathIndex < (int)a.Path.size())
                {
                    glm::vec3 from = position + lift;
                    for (size_t i = (size_t)a.PathIndex; i < a.Path.size(); i++)
                    {
                        const glm::vec3 to = a.Path[i] + lift;
                        canvas.Line(from, to, color);
                        canvas.Dot(to, i + 1 == a.Path.size() ? 9.0f : 5.0f, color);
                        from = to;
                    }
                }
                if (a.HasDestination && (selected || !a.Arrived))
                {
                    const glm::vec3 d = a.Destination + lift;
                    Cross(canvas, d, canvas.PixelSize(d) * 7.0f, a.Arrived ? glm::vec4(0.5f, 1.0f, 0.55f, 0.9f) : color);
                }
                // Avoidance: the feelers (red where they hit) and the push they gave (yellow).
                if (a.Avoidance && a.FeelersActive)
                {
                    const glm::vec4 hitColor(1.0f, 0.3f, 0.25f, 1.0f);
                    for (int i = 0; i < 3; i++)
                    {
                        const bool hit = (a.FeelerHits >> i) & 1u;
                        canvas.Line(a.FeelerOrigin, a.FeelerEnds[(size_t)i], hit ? hitColor : glm::vec4(0.5f, 0.95f, 0.6f, 0.7f));
                        if (hit)
                            canvas.Dot(a.FeelerEnds[(size_t)i], 6.0f, hitColor);
                    }
                    if (glm::dot(a.AvoidancePush, a.AvoidancePush) > 1e-6f)
                        canvas.Line(a.FeelerOrigin, a.FeelerOrigin + a.AvoidancePush * 0.5f, glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
                }
            }
        }

        void DrawObstacles(Scene& scene, NavDebugCanvas& canvas, const NavDebugOptions& o)
        {
            for (auto e : scene.GetAllEntitiesWith<NavObstacleComponent>())
            {
                if (!Drawn(o, e))
                    continue;
                Entity entity{ e, &scene };
                const auto& c = entity.GetComponent<NavObstacleComponent>();
                const glm::vec4 color(1.0f, 0.62f, 0.15f, c.IsCarving() || IsSelected(o, e) ? 1.0f : 0.55f);
                const glm::mat4 world = scene.GetWorldTransform(entity);
                if (c.Shape == NavObstacleComponent::ShapeType::Box)
                {
                    glm::mat4 local(1.0f);
                    const glm::vec3 size = glm::max(c.Size, glm::vec3(0.001f));
                    local[0][0] = size.x;
                    local[1][1] = size.y;
                    local[2][2] = size.z;
                    local[3] = glm::vec4(c.Center, 1.0f);
                    WireBox(canvas, world * local, color);
                    continue;
                }
                // 2D: a circle in the entity's XY plane; 3D: a cylinder along its Y.
                const bool flat = Scene::GetNavGridDimension(scene.FindNavGridEntity(scene.GetWorldPosition(entity))) == 2;
                const glm::vec3 center(world * glm::vec4(c.Center, 1.0f));
                const glm::vec3 ax = glm::vec3(world[0]), ay = glm::vec3(world[1]), az = glm::vec3(world[2]);
                const float sx = glm::length(ax), sy = glm::length(ay), sz = glm::length(az);
                const glm::vec3 nx = sx > 1e-6f ? ax / sx : glm::vec3(1, 0, 0), ny = sy > 1e-6f ? ay / sy : glm::vec3(0, 1, 0),
                                nz = sz > 1e-6f ? az / sz : glm::vec3(0, 0, 1);
                if (flat)
                    WireCircle(canvas, center, nx, ny, c.Radius * std::max(sx, sy), color);
                else
                {
                    const float r = c.Radius * std::max(sx, sz), hh = std::max(0.0f, c.Size.y) * 0.5f * sy;
                    WireCircle(canvas, center - ny * hh, nx, nz, r, color);
                    WireCircle(canvas, center + ny * hh, nx, nz, r, color);
                    for (int i = 0; i < 4; i++)
                    {
                        const float t = glm::half_pi<float>() * (float)i;
                        const glm::vec3 rim = (nx * std::cos(t) + nz * std::sin(t)) * r;
                        canvas.Line(center - ny * hh + rim, center + ny * hh + rim, color);
                    }
                }
            }
        }

        void DrawSight(Scene& scene, NavDebugCanvas& canvas, const NavDebugOptions& o)
        {
            std::vector<glm::vec3> arc;
            for (auto e : scene.GetAllEntitiesWith<SightSensorComponent>())
            {
                if (!Drawn(o, e))
                    continue;
                Entity entity{ e, &scene };
                const auto& c = entity.GetComponent<SightSensorComponent>();
                SightCone cone;
                if (!scene.GetSightCone(entity, cone))
                    continue;
                const bool selected = IsSelected(o, e);
                const bool sees = o.Running && c.CanSee;
                const glm::vec4 color = sees ? glm::vec4(1.0f, 0.3f, 0.25f, 1.0f) : glm::vec4(1.0f, 0.85f, 0.25f, selected || !o.Selected ? 1.0f : 0.7f);
                const glm::vec3 up = PlaneUp(scene.GetSightDimension(entity));
                Sight::GetConeArc(cone, up, std::clamp((int)(cone.FieldOfView / 6.0f), 4, 60), arc);
                if (arc.empty())
                    continue;
                if (cone.FieldOfView < 359.0f)
                {
                    canvas.Line(cone.Eye, arc.front(), color);
                    canvas.Line(cone.Eye, arc.back(), color);
                }
                for (size_t i = 1; i < arc.size(); i++)
                    canvas.Line(arc[i - 1], arc[i], color);
                if (cone.NearDistance > 0.0f)
                {
                    const glm::vec3 a(1.0f, 0.0f, 0.0f);
                    const glm::vec3 b = glm::normalize(glm::cross(up, a));
                    WireCircle(canvas, cone.Eye, a, b, cone.NearDistance, glm::vec4(color.r, color.g, color.b, color.a * 0.5f));
                }
                if (sees)
                    if (Entity target = scene.GetEntityByUUID(c.SeenTarget))
                        canvas.Line(cone.Eye, scene.GetWorldPosition(target) + (up.y > 0.5f ? glm::vec3(0.0f, 0.9f, 0.0f) : glm::vec3(0.0f)), color);
                if (o.Running && c.HasSeen && !c.CanSee && c.RemembersTarget())
                {
                    // Where it last saw one (a cross while it remembers).
                    const glm::vec3 p = c.LastSeenPosition;
                    Cross(canvas, p, canvas.PixelSize(p) * 6.0f, glm::vec4(1.0f, 0.6f, 0.3f, 0.9f));
                    if (o.Labels)
                        canvas.Text(p + up * 0.5f, "?", glm::vec4(1.0f, 0.6f, 0.3f, 1.0f));
                }
            }
        }

        void DrawPatrols(Scene& scene, NavDebugCanvas& canvas, const NavDebugOptions& o)
        {
            std::vector<glm::vec3> points;
            std::vector<float> waits;
            for (auto e : scene.GetAllEntitiesWith<PatrolPathComponent>())
            {
                if (!Drawn(o, e))
                    continue;
                Entity entity{ e, &scene };
                const auto& c = entity.GetComponent<PatrolPathComponent>();
                if (!scene.GetPatrolWaypoints(entity, points, &waits))
                    continue;
                const bool selected = IsSelected(o, e);
                const float alpha = selected || !o.Selected ? 1.0f : 0.6f;
                const glm::vec4 color(1.0f, 0.45f, 0.85f, 0.9f * alpha), faint(1.0f, 0.45f, 0.85f, 0.45f * alpha);
                const glm::vec4 current(1.0f, 0.92f, 0.3f, alpha);
                int dimension = 0;
                if (entity.HasComponent<NavAgentComponent>())
                    dimension = Scene::GetNavGridDimension(scene.GetNavAgentGrid(entity));
                if (dimension == 0)
                    dimension = scene.GetSightDimension(entity);
                const glm::vec3 up = PlaneUp(dimension);
                const glm::vec3 lift = dimension == 3 ? glm::vec3(0.0f, 0.1f, 0.0f) : glm::vec3(0.0f);
                const size_t n = points.size();
                // The round: point to point in order (Loop: back to the first, fainter; Random: all
                // fainter - it goes to any of them).
                const bool random = c.Mode == PatrolPathComponent::PatrolMode::Random;
                for (size_t i = 1; i < n; i++)
                {
                    const glm::vec3 a = points[i - 1] + lift, b = points[i] + lift;
                    canvas.Line(a, b, random ? faint : color);
                    if (!random)
                        Chevron(canvas, a, b, up, canvas.PixelSize((a + b) * 0.5f) * 9.0f, color);
                }
                if (c.Mode == PatrolPathComponent::PatrolMode::Loop && n > 2)
                {
                    const glm::vec3 a = points[n - 1] + lift, b = points[0] + lift;
                    canvas.Line(a, b, faint);
                    Chevron(canvas, a, b, up, canvas.PixelSize((a + b) * 0.5f) * 9.0f, faint);
                }
                // The points: bigger where it waits longer than the default; the first one ringed.
                for (size_t i = 0; i < n; i++)
                {
                    const glm::vec3 p = points[i] + lift;
                    const bool isCurrent = o.Running && c.Started && c.Current == (int)i && c.IsPatrolling();
                    const float waitAt = i < waits.size() ? waits[i] : 0.0f;
                    canvas.Dot(p, isCurrent ? 11.0f : waitAt > 0.0f ? 8.0f : 6.0f, isCurrent ? current : color);
                    if (i == 0)
                        WireCircle(canvas, p, glm::vec3(1.0f, 0.0f, 0.0f), glm::normalize(glm::cross(up, glm::vec3(1.0f, 0.0f, 0.0f))),
                                   canvas.PixelSize(p) * 8.0f, color, 20);
                    if (o.Labels)
                    {
                        std::string label = std::to_string(i);
                        if (isCurrent && c.Waiting)
                            label += std::format(" ({:.1f} s)", std::max(0.0f, c.WaitTimer));
                        else if (waitAt > 0.0f && selected)
                            label += std::format(" {:.1f} s", waitAt);
                        canvas.Text(p + up * (canvas.PixelSize(p) * 16.0f), label, isCurrent ? current : color);
                    }
                }
                // Where it is heading now.
                if (o.Running && c.Started && c.IsPatrolling() && !c.Waiting && c.Current >= 0 && (size_t)c.Current < n)
                    canvas.Line(scene.GetWorldPosition(entity) + lift, points[(size_t)c.Current] + lift, glm::vec4(current.r, current.g, current.b, 0.55f * alpha));
            }
        }

    }

    void NavDebugDraw::DrawGridCells(NavDebugCanvas& canvas, const NavGrid& grid, bool flat3D, bool walkable, float alpha)
    {
        const glm::vec4 blockedColor(0.95f, 0.25f, 0.2f, 0.34f * alpha), walkableColor(0.3f, 0.95f, 0.45f, 0.16f * alpha);
        const glm::vec4 carvedColor(1.0f, 0.6f, 0.12f, 0.5f * alpha);
        const bool carves = grid.CountCarved() > 0;
        // 1 walkable, 2 carved (walkable as baked), 0 blocked.
        auto state = [&](int x, int y) { return grid.IsWalkable(x, y) ? 1 : carves && grid.IsCarved(x, y) && grid.IsBakedWalkable(x, y) ? 2 : 0; };
        for (int y = 0; y < grid.GetHeight(); y++)
        {
            int x = 0;
            while (x < grid.GetWidth())
            {
                const int kind = state(x, y);
                const float h = grid.GetFloorHeight(x, y);
                int end = x + 1;
                while (end < grid.GetWidth() && state(end, y) == kind &&
                       (!flat3D || std::abs(grid.GetFloorHeight(end, y) - h) < 1e-3f || (std::isnan(h) && std::isnan(grid.GetFloorHeight(end, y)))))
                    end++;
                if ((kind != 1 || walkable) && (!flat3D || std::isfinite(h)))
                    CellRun(canvas, grid, x, end, y, flat3D, flat3D ? h : 0.0f, kind == 1 ? walkableColor : kind == 2 ? carvedColor : blockedColor);
                x = end;
            }
        }
    }

    void NavDebugDraw::DrawScene(Scene& scene, NavDebugCanvas& canvas, const NavDebugOptions& options)
    {
        // Inactive entities are left out (their components do nothing), unless the filter decides.
        NavDebugOptions o = options;
        if (!o.Filter)
            o.Filter = [&scene](entt::entity e) { return scene.IsActiveInHierarchy({ e, &scene }); };
        if (o.Grids)
            DrawGrids(scene, canvas, o);
        if (o.Patrols)
            DrawPatrols(scene, canvas, o);
        if (o.Agents)
            DrawAgents(scene, canvas, o);
        if (o.Obstacles)
            DrawObstacles(scene, canvas, o);
        if (o.Sight)
            DrawSight(scene, canvas, o);
    }

    // ------------------------------------------------------------------ the scene's hook
    void Scene::DrawNavigationDebug()
    {
        NavDebugOptions options;
        options.WalkableCells = DebugDraw::GetSettings().ShowNavigationWalkable;
        options.Labels = true;
        options.Running = true;
        DebugDrawNavCanvas canvas(GetLastView(), GetLastProjection(), (float)std::max(1.0f, GetViewportSize().y));
        NavDebugDraw::DrawScene(*this, canvas, options);
    }

}
