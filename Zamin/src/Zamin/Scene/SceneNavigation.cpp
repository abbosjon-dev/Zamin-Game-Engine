// Navigation in the scene: baking the NavGrid 2D / NavGrid 3D components from the level, path
// queries for scripts, and the Nav Agents walking their paths.
#include "Zamin/Scene/Scene.h"

#include "Zamin/AI/NavGrid.h"
#include "Zamin/AI/NavGridBuilder.h"
#include "Zamin/AI/NavPathSmoothing.h"
#include "Zamin/AI/NavPathfinder.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Math/MathUtils.h"
#include "Zamin/Physics/CollisionLayers.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/TilesetProperties.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace Zamin {

    namespace {

        // One per thread: its buffers are reused by every search.
        thread_local NavPathfinder t_Pathfinder;

        NavPathOptions MakeOptions(const NavGridPathSettings& settings)
        {
            NavPathOptions options;
            options.Diagonals = settings.Diagonals;
            options.CutCorners = settings.CutCorners;
            return options;
        }

        // The grid's plane: XY for 2D grids, XZ for 3D ones.
        glm::vec2 ToPlane(const glm::vec3& p, int dimension) { return dimension == 3 ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y); }

        bool LayerCounts(uint16_t mask, int layer) { return ((mask >> CollisionLayers2D::Clamp(layer)) & 1u) != 0; }

        float ElapsedMilliseconds(std::chrono::steady_clock::time_point since)
        {
            return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - since).count();
        }

        float SanitizeCellSize(float size, float fallback)
        {
            return std::isfinite(size) ? std::clamp(size, NavGrid2DComponent::MinCellSize, NavGrid2DComponent::MaxCellSize) : fallback;
        }

        // FNV-1a over what a grid is baked from.
        struct SourceHasher
        {
            uint64_t Hash = 1469598103934665603ull;
            void Bytes(const void* data, size_t size)
            {
                const auto* p = (const uint8_t*)data;
                for (size_t i = 0; i < size; i++)
                {
                    Hash ^= p[i];
                    Hash *= 1099511628211ull;
                }
            }
            template<typename T>
            void Value(const T& value) { Bytes(&value, sizeof(T)); }
        };

        // A static 2D collider in the world.
        struct NavShape2D
        {
            enum class Kind { Polygon, Circle, Chain } Type = Kind::Polygon;
            std::vector<glm::vec2> Points;
            glm::vec2 Center{ 0.0f };
            float Radius = 0.0f;
            bool Loop = false;
        };

        // The static 2D colliders (Rigidbody 2D Static, not triggers) on a layer of `mask`, placed as
        // their bodies are (the same offsets and scaling as Scene::CreatePhysicsBody).
        void CollectNavShapes2D(Scene& scene, uint16_t mask, bool oneWayBlocks, std::vector<NavShape2D>& out)
        {
            for (auto e : scene.GetAllEntitiesWith<Rigidbody2DComponent>())
            {
                Entity entity{ e, &scene };
                const auto& rb = entity.GetComponent<Rigidbody2DComponent>();
                if (rb.Type != Rigidbody2DComponent::BodyType::Static || !scene.IsActiveInHierarchy(entity))
                    continue;
                glm::vec3 position, rotation, scale;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), position, rotation, scale);
                const glm::vec2 origin(position);
                const float c = std::cos(rotation.z), s = std::sin(rotation.z);
                auto toWorld = [&](const glm::vec2& p) { return origin + glm::vec2(c * p.x - s * p.y, s * p.x + c * p.y); };
                const glm::vec2 bodyScale(std::abs(scale.x), std::abs(scale.y));
                auto counts = [&](int ownLayer, bool sensor) { return !sensor && LayerCounts(mask, ownLayer >= 0 ? ownLayer : rb.Layer); };

                if (const auto* b = entity.TryGetComponent<BoxCollider2DComponent>(); b && counts(b->Layer, b->IsSensor) && (!b->OneWay || oneWayBlocks))
                {
                    const glm::vec2 h(std::max(0.001f, b->Size.x * bodyScale.x), std::max(0.001f, b->Size.y * bodyScale.y));
                    NavShape2D shape;
                    shape.Points = { toWorld(b->Offset + glm::vec2(-h.x, -h.y)), toWorld(b->Offset + glm::vec2(h.x, -h.y)),
                                     toWorld(b->Offset + glm::vec2(h.x, h.y)), toWorld(b->Offset + glm::vec2(-h.x, h.y)) };
                    out.push_back(std::move(shape));
                }
                if (const auto* cc = entity.TryGetComponent<CircleCollider2DComponent>(); cc && counts(cc->Layer, cc->IsSensor))
                {
                    NavShape2D shape;
                    shape.Type = NavShape2D::Kind::Circle;
                    shape.Center = toWorld(cc->Offset);
                    shape.Radius = std::max(0.001f, std::abs(scale.x) * cc->Radius);
                    out.push_back(std::move(shape));
                }
                if (const auto* pc = entity.TryGetComponent<PolygonCollider2DComponent>(); pc && counts(pc->Layer, pc->IsSensor) && pc->Points.size() >= 3)
                {
                    NavShape2D shape;
                    for (const glm::vec2& p : pc->Points)
                        shape.Points.push_back(toWorld(pc->Offset + p * bodyScale));
                    out.push_back(std::move(shape));
                }
                if (const auto* ec = entity.TryGetComponent<EdgeCollider2DComponent>(); ec && counts(ec->Layer, ec->IsSensor) && ec->Points.size() >= 2)
                {
                    NavShape2D shape;
                    shape.Type = NavShape2D::Kind::Chain;
                    shape.Loop = ec->Loop;
                    for (const glm::vec2& p : ec->Points)
                        shape.Points.push_back(toWorld(ec->Offset + p * bodyScale));
                    out.push_back(std::move(shape));
                }
            }
        }

        void GetShapeBounds(const NavShape2D& shape, glm::vec2& min, glm::vec2& max)
        {
            if (shape.Type == NavShape2D::Kind::Circle)
            {
                min = glm::min(min, shape.Center - shape.Radius);
                max = glm::max(max, shape.Center + shape.Radius);
                return;
            }
            for (const glm::vec2& p : shape.Points)
            {
                min = glm::min(min, p);
                max = glm::max(max, p);
            }
        }

        // The tilemaps a 2D grid is baked from (active, colliding, on a layer of its mask).
        template<typename F>
        void ForEachNavTilemap(Scene& scene, const NavGrid2DComponent& c, F&& f)
        {
            if (!c.UseTilemaps)
                return;
            auto use = [&](Entity e) {
                const auto& tm = e.GetComponent<TilemapComponent>();
                if (tm.Collision && scene.IsActiveInHierarchy(e) && LayerCounts(c.LayerMask, tm.CollisionLayer))
                    f(e, tm);
            };
            if (c.Tilemap)
            {
                Entity e = scene.GetEntityByUUID(c.Tilemap.ID);
                if (e && e.HasComponent<TilemapComponent>())
                    use(e);
                return;
            }
            for (auto e : scene.GetAllEntitiesWith<TilemapComponent>())
                use({ e, &scene });
        }

        // The point `lookAhead` along the path ahead of the agent: from its projection on the segment it
        // walks (index - 1 -> index), on past the corners.
        glm::vec2 LookAheadPoint(const std::vector<glm::vec2>& path, int index, const glm::vec2& position, float lookAhead)
        {
            const glm::vec2 s0 = path[(size_t)index - 1], s1 = path[(size_t)index];
            const glm::vec2 segment = s1 - s0;
            const float length2 = glm::dot(segment, segment);
            const float t = length2 > 1e-10f ? std::clamp(glm::dot(position - s0, segment) / length2, 0.0f, 1.0f) : 1.0f;
            glm::vec2 from = s0 + segment * t;
            float left = std::max(0.0f, lookAhead);
            for (size_t i = (size_t)index; i < path.size(); i++)
            {
                const float d = glm::length(path[i] - from);
                if (d >= left && d > 1e-6f)
                    return from + (path[i] - from) * (left / d);
                left -= d;
                from = path[i];
            }
            return path.back();
        }

        void SetEntityWorldPosition(Scene& scene, Entity entity, const glm::vec3& position)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            if (!scene.GetParent(entity))
            {
                transform.Translation = position;
                return;
            }
            glm::mat4 world = scene.GetWorldTransform(entity);
            world[3] = glm::vec4(position, 1.0f);
            const glm::vec3 keepScale = transform.Scale;
            scene.SetWorldTransform(entity, world);
            transform.Scale = keepScale;
        }

    }

    // ------------------------------------------------------------------ grids
    int Scene::GetNavGridDimension(Entity entity)
    {
        if (!entity || !entity.IsValid())
            return 0;
        if (entity.HasComponent<NavGrid3DComponent>())
            return 3;
        return entity.HasComponent<NavGrid2DComponent>() ? 2 : 0;
    }

    const NavGrid* Scene::GetNavGrid(Entity entity, int dimension)
    {
        if (!entity || !entity.IsValid())
            return nullptr;
        if (dimension != 2)
            if (const auto* c = entity.TryGetComponent<NavGrid3DComponent>(); c && c->Baked && !c->Grid.IsEmpty())
                return &c->Grid;
        if (dimension != 3)
            if (const auto* c = entity.TryGetComponent<NavGrid2DComponent>(); c && c->Baked && !c->Grid.IsEmpty())
                return &c->Grid;
        return nullptr;
    }

    bool Scene::GetNavGrid2DSourceBounds(Entity entity, glm::vec2& min, glm::vec2& max)
    {
        const auto* c = entity ? entity.TryGetComponent<NavGrid2DComponent>() : nullptr;
        if (!c)
            return false;
        min = glm::vec2(std::numeric_limits<float>::max());
        max = glm::vec2(std::numeric_limits<float>::lowest());
        bool any = false;
        ForEachNavTilemap(*this, *c, [&](Entity e, const TilemapComponent& tm) {
            glm::vec2 lo, hi;
            NavGridBuilder::GetTilemapBounds(tm, GetWorldTransform(e), lo, hi);
            min = glm::min(min, lo);
            max = glm::max(max, hi);
            any = true;
        });
        if (c->UseColliders)
        {
            std::vector<NavShape2D> shapes;
            CollectNavShapes2D(*this, c->LayerMask, c->OneWayBlocks, shapes);
            for (const NavShape2D& shape : shapes)
                GetShapeBounds(shape, min, max);
            any |= !shapes.empty();
        }
        return any && min.x <= max.x && min.y <= max.y;
    }

    uint64_t Scene::GetNavGrid2DSourceHash(Entity entity)
    {
        const auto* c = entity ? entity.TryGetComponent<NavGrid2DComponent>() : nullptr;
        if (!c)
            return 0;
        SourceHasher h;
        h.Value(c->AutoBounds);
        h.Value(c->Offset);
        h.Value(c->Size);
        h.Value(c->CellSize);
        h.Value(c->UseTilemaps);
        h.Value(c->OneWayBlocks);
        h.Value(c->Tilemap.ID);
        h.Value(c->UseColliders);
        h.Value(c->LayerMask);
        h.Value(c->AgentRadius);
        if (!c->AutoBounds)
            h.Value(GetWorldPosition(entity));
        ForEachNavTilemap(*this, *c, [&](Entity e, const TilemapComponent& tm) {
            h.Value(GetWorldTransform(e));
            h.Value(tm.Width);
            h.Value(tm.Height);
            h.Value(tm.TileSize);
            for (const TilemapLayer& layer : tm.Layers)
            {
                h.Value(layer.Collision);
                if (layer.Collision && !layer.Tiles.empty())
                    h.Bytes(layer.Tiles.data(), layer.Tiles.size() * sizeof(int));
            }
        });
        if (c->UseColliders)
        {
            std::vector<NavShape2D> shapes;
            CollectNavShapes2D(*this, c->LayerMask, c->OneWayBlocks, shapes);
            for (const NavShape2D& shape : shapes)
            {
                h.Value(shape.Type);
                h.Value(shape.Center);
                h.Value(shape.Radius);
                h.Value(shape.Loop);
                if (!shape.Points.empty())
                    h.Bytes(shape.Points.data(), shape.Points.size() * sizeof(glm::vec2));
            }
        }
        return h.Hash;
    }

    bool Scene::BakeNavGrid2D(Entity entity)
    {
        auto* c = entity.TryGetComponent<NavGrid2DComponent>();
        if (!c)
            return false;
        const auto clock = std::chrono::steady_clock::now();
        const float cellSize = SanitizeCellSize(c->CellSize, 0.5f);
        glm::vec2 min, max;
        if (!c->AutoBounds || !GetNavGrid2DSourceBounds(entity, min, max))
        {
            const glm::vec2 center = glm::vec2(GetWorldPosition(entity)) + c->Offset;
            glm::vec2 half = glm::vec2(std::isfinite(c->Size.x) ? c->Size.x : 1.0f, std::isfinite(c->Size.y) ? c->Size.y : 1.0f) * 0.5f;
            half = glm::max(half, glm::vec2(cellSize * 0.5f));
            min = center - half;
            max = center + half;
        }
        const double cellsX = std::ceil((double)(max.x - min.x) / cellSize - 1e-4), cellsY = std::ceil((double)(max.y - min.y) / cellSize - 1e-4);
        if (cellsX > NavGrid::MaxSize || cellsY > NavGrid::MaxSize)
            ZM_CORE_WARN("NavGrid 2D '{}': maydon {}x{} katak - {} gacha qisqartirildi (katak o'lchamini kattalashtiring)", entity.GetName(),
                         (long long)cellsX, (long long)cellsY, NavGrid::MaxSize);
        NavGrid& grid = c->Grid;
        grid.Reset((int)std::clamp(cellsX, 1.0, (double)NavGrid::MaxSize), (int)std::clamp(cellsY, 1.0, (double)NavGrid::MaxSize), cellSize, min);

        ForEachNavTilemap(*this, *c, [&](Entity e, const TilemapComponent& tm) {
            NavGridBuilder::BlockTilemap(grid, tm, GetWorldTransform(e), TilesetProperties::Get(tm.TilesetPath), c->OneWayBlocks);
        });
        if (c->UseColliders)
        {
            std::vector<NavShape2D> shapes;
            CollectNavShapes2D(*this, c->LayerMask, c->OneWayBlocks, shapes);
            for (const NavShape2D& shape : shapes)
                switch (shape.Type)
                {
                    case NavShape2D::Kind::Polygon:
                        NavGridBuilder::BlockPolygon(grid, shape.Points);
                        break;
                    case NavShape2D::Kind::Circle:
                        NavGridBuilder::BlockCircle(grid, shape.Center, shape.Radius);
                        break;
                    case NavShape2D::Kind::Chain:
                        for (size_t i = 0; i + 1 < shape.Points.size(); i++)
                            NavGridBuilder::BlockSegment(grid, shape.Points[i], shape.Points[i + 1]);
                        if (shape.Loop && shape.Points.size() > 2)
                            NavGridBuilder::BlockSegment(grid, shape.Points.back(), shape.Points.front());
                        break;
                }
        }
        grid.Erode(std::isfinite(c->AgentRadius) ? c->AgentRadius : 0.0f);
        c->Baked = true;
        c->BakeCount++;
        c->SourceHash = GetNavGrid2DSourceHash(entity);
        c->RebuildTimer = 0.0f;
        c->BakeMilliseconds = ElapsedMilliseconds(clock);
        return true;
    }

    bool Scene::BakeNavGrid3D(Entity entity)
    {
        auto* c = entity.TryGetComponent<NavGrid3DComponent>();
        if (!c)
            return false;
        const auto clock = std::chrono::steady_clock::now();
        const float cellSize = SanitizeCellSize(c->CellSize, 0.5f);
        const glm::vec3 center = GetWorldPosition(entity) + c->Offset;
        glm::vec3 size = c->Size;
        for (int i = 0; i < 3; i++)
            size[i] = std::isfinite(size[i]) ? std::max(size[i], i == 1 ? 0.1f : cellSize) : 10.0f;
        const double cellsX = std::ceil((double)size.x / cellSize - 1e-4), cellsZ = std::ceil((double)size.z / cellSize - 1e-4);
        if (cellsX > NavGrid::MaxSize || cellsZ > NavGrid::MaxSize)
            ZM_CORE_WARN("NavGrid 3D '{}': maydon {}x{} katak - {} gacha qisqartirildi (katak o'lchamini kattalashtiring)", entity.GetName(),
                         (long long)cellsX, (long long)cellsZ, NavGrid::MaxSize);
        const int width = (int)std::clamp(cellsX, 1.0, (double)NavGrid::MaxSize), height = (int)std::clamp(cellsZ, 1.0, (double)NavGrid::MaxSize);
        const glm::vec2 origin(center.x - (float)width * cellSize * 0.5f, center.z - (float)height * cellSize * 0.5f);
        NavGrid& grid = c->Grid;
        grid.Reset(width, height, cellSize, origin);
        grid.EnableHeights();
        const float stepHeight = std::isfinite(c->StepHeight) ? std::max(0.0f, c->StepHeight) : 0.4f;
        grid.SetMaxClimb(std::max(stepHeight, 0.001f));

        // The static colliders in a world of their own (the running one has moving bodies in it, and
        // the editor has none).
        PhysicsWorld3D world(m_Settings.Gravity3D);
        world.SetCollisionMatrix(Project::GetCollisionLayers2D().Masks);
        int bodies = 0;
        for (auto e : m_Registry.view<Rigidbody3DComponent>())
        {
            Entity body{ e, this };
            if (m_Registry.get<Rigidbody3DComponent>(e).Type != Rigidbody3DComponent::BodyType::Static || !IsActiveInHierarchy(body))
                continue;
            PhysicsBody3DDesc desc;
            if (MakePhysicsBody3DDesc(body, desc, true) && world.CreateBody(desc) != 0xffffffff)
                bodies++;
        }
        world.OptimizeBroadPhase();

        const float top = center.y + size.y * 0.5f;
        const float cosMaxSlope = std::cos(glm::radians(std::clamp(std::isfinite(c->MaxSlope) ? c->MaxSlope : 45.0f, 0.0f, 89.9f)));
        const float agentHeight = std::isfinite(c->AgentHeight) ? c->AgentHeight : 1.8f;
        const float clearance = std::max(0.05f, agentHeight - stepHeight);
        const PhysicsQueryShape3D box = PhysicsQueryShape3D::Box({ cellSize * 0.45f, clearance * 0.5f, cellSize * 0.45f });
        const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        const float noFloor = std::numeric_limits<float>::quiet_NaN();
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                const glm::vec2 p = grid.CellToWorld(x, y);
                const RaycastHit hit = bodies > 0 ? world.CastRay({ p.x, top, p.y }, { 0.0f, -1.0f, 0.0f }, size.y, c->LayerMask, false) : RaycastHit{};
                if (!hit)
                {
                    grid.SetWalkable(x, y, false);
                    grid.SetFloorHeight(x, y, noFloor);
                    continue;
                }
                grid.SetFloorHeight(x, y, hit.Point.y);
                // Too steep, or something in the way between the step height and the agent's head.
                const glm::vec3 above(p.x, hit.Point.y + stepHeight + clearance * 0.5f, p.y);
                if (hit.Normal.y < cosMaxSlope || !world.Overlap(box, above, identity, c->LayerMask, false).empty())
                    grid.SetWalkable(x, y, false);
            }
        grid.Erode(std::isfinite(c->AgentRadius) ? c->AgentRadius : 0.0f);
        c->Baked = true;
        c->BakeCount++;
        c->BakeBodies = bodies;
        c->BakeMilliseconds = ElapsedMilliseconds(clock);
        return true;
    }

    bool Scene::BakeNavGrid(Entity entity)
    {
        if (!entity || !entity.IsValid())
            return false;
        bool baked = false;
        if (entity.HasComponent<NavGrid2DComponent>())
            baked |= BakeNavGrid2D(entity);
        if (entity.HasComponent<NavGrid3DComponent>())
            baked |= BakeNavGrid3D(entity);
        return baked;
    }

    int Scene::BakeNavGrids()
    {
        std::vector<Entity> grids;
        for (auto e : m_Registry.view<NavGrid2DComponent>())
            grids.push_back({ e, this });
        for (auto e : m_Registry.view<NavGrid3DComponent>())
            if (!m_Registry.all_of<NavGrid2DComponent>(e))
                grids.push_back({ e, this });
        int count = 0;
        for (Entity e : grids)
            if (IsActiveInHierarchy(e) && BakeNavGrid(e))
                count++;
        return count;
    }

    void Scene::StartNavigation()
    {
        std::vector<Entity> grids;
        for (auto e : m_Registry.view<NavGrid2DComponent>())
            if (m_Registry.get<NavGrid2DComponent>(e).Paths.BakeOnStart)
                grids.push_back({ e, this });
        for (auto e : m_Registry.view<NavGrid3DComponent>())
            if (m_Registry.get<NavGrid3DComponent>(e).Paths.BakeOnStart)
                grids.push_back({ e, this });
        for (Entity e : grids)
        {
            if (!IsActiveInHierarchy(e))
                continue;
            if (auto* c = e.TryGetComponent<NavGrid2DComponent>(); c && c->Paths.BakeOnStart && BakeNavGrid2D(e))
                ZM_CORE_INFO("Navigatsiya: '{}' 2D grid'i {}x{} katak, {} tasi yuriladigan ({:.1f} ms)", e.GetName(), c->Grid.GetWidth(),
                             c->Grid.GetHeight(), c->Grid.CountWalkable(), c->BakeMilliseconds);
            if (auto* c = e.TryGetComponent<NavGrid3DComponent>(); c && c->Paths.BakeOnStart && BakeNavGrid3D(e))
                ZM_CORE_INFO("Navigatsiya: '{}' 3D grid'i {}x{} katak, {} tasi yuriladigan, {} ta statik jism ({:.1f} ms)", e.GetName(),
                             c->Grid.GetWidth(), c->Grid.GetHeight(), c->Grid.CountWalkable(), c->BakeBodies, c->BakeMilliseconds);
        }
    }

    void Scene::UpdateNavGrids(float dt)
    {
        for (auto e : m_Registry.view<NavGrid2DComponent>())
        {
            auto& c = m_Registry.get<NavGrid2DComponent>(e);
            if (!c.AutoRebuild || !c.Baked)
                continue;
            c.RebuildTimer -= dt;
            if (c.RebuildTimer > 0.0f)
                continue;
            c.RebuildTimer = 0.5f; // a few checks a second
            Entity entity{ e, this };
            if (IsActiveInHierarchy(entity) && GetNavGrid2DSourceHash(entity) != c.SourceHash)
                BakeNavGrid2D(entity);
        }
    }

    // ------------------------------------------------------------------ queries
    Entity Scene::FindNavGridEntity(const glm::vec3& position, int dimension)
    {
        Entity best;
        float bestDistance = std::numeric_limits<float>::max();
        auto consider = [&](entt::entity e, const NavGrid& grid, bool baked, int d) {
            if (!baked || grid.IsEmpty() || !IsActiveInHierarchy({ e, this }))
                return;
            const glm::vec2 p = ToPlane(position, d);
            const glm::vec2 min = grid.GetOrigin(), max = grid.GetOrigin() + grid.GetSize();
            const glm::vec2 outside = glm::max(glm::max(min - p, p - max), glm::vec2(0.0f));
            const float distance = glm::length(outside);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = { e, this };
            }
        };
        if (dimension != 2)
            for (auto e : m_Registry.view<NavGrid3DComponent>())
            {
                const auto& c = m_Registry.get<NavGrid3DComponent>(e);
                consider(e, c.Grid, c.Baked, 3);
            }
        if (dimension != 3)
            for (auto e : m_Registry.view<NavGrid2DComponent>())
            {
                const auto& c = m_Registry.get<NavGrid2DComponent>(e);
                consider(e, c.Grid, c.Baked, 2);
            }
        return best;
    }

    Entity Scene::GetNavAgentGrid(Entity agent)
    {
        const auto* a = agent && agent.IsValid() ? agent.TryGetComponent<NavAgentComponent>() : nullptr;
        if (!a)
            return {};
        if (a->Grid)
        {
            Entity grid = GetEntityByUUID(a->Grid.ID);
            return GetNavGridDimension(grid) ? grid : Entity();
        }
        const glm::vec3 position = GetWorldPosition(agent);
        const int preferred = agent.HasComponent<CharacterController3DComponent>() || agent.HasComponent<Rigidbody3DComponent>() ? 3 : 2;
        if (Entity grid = FindNavGridEntity(position, preferred))
            return grid;
        return FindNavGridEntity(position, 0);
    }

    // The baked grid of a grid entity, with its dimension and path settings (3D first).
    static const NavGrid* GetBakedGrid(Entity entity, int& dimension, const NavGridPathSettings*& settings)
    {
        dimension = 0;
        settings = nullptr;
        if (!entity || !entity.IsValid())
            return nullptr;
        if (const auto* c = entity.TryGetComponent<NavGrid3DComponent>(); c && c->Baked && !c->Grid.IsEmpty())
        {
            dimension = 3;
            settings = &c->Paths;
            return &c->Grid;
        }
        if (const auto* c = entity.TryGetComponent<NavGrid2DComponent>(); c && c->Baked && !c->Grid.IsEmpty())
        {
            dimension = 2;
            settings = &c->Paths;
            return &c->Grid;
        }
        return nullptr;
    }

    static NavPathStatus FindPathOnGrid(const NavGrid& grid, int dimension, const NavGridPathSettings& settings, const glm::vec3& from,
                                        const glm::vec3& to, bool smooth, bool spline, std::vector<glm::vec3>& path)
    {
        path.clear();
        const glm::vec2 a = ToPlane(from, dimension), b = ToPlane(to, dimension);
        NavPathResult result;
        t_Pathfinder.FindPath(grid, a, b, result, MakeOptions(settings));
        if (!result.Found())
            return result.Status;
        std::vector<glm::vec2> points = NavPathSmoothing::ToPoints(grid, result.Cells, &a, &b);
        if (smooth)
            points = NavPathSmoothing::StringPull(grid, points);
        if (spline)
            points = NavPathSmoothing::SplineSmooth(grid, points, grid.GetCellSize() * 0.5f);
        path.reserve(points.size());
        for (const glm::vec2& p : points)
            path.push_back(dimension == 3 ? glm::vec3(p.x, grid.SampleFloorHeight(p), p.y) : glm::vec3(p, from.z));
        return result.Status;
    }

    NavPathStatus Scene::FindNavPath(const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& path, Entity grid, bool smooth, bool spline)
    {
        path.clear();
        if (!grid)
            grid = FindNavGridEntity(from);
        int dimension;
        const NavGridPathSettings* settings;
        const NavGrid* navGrid = GetBakedGrid(grid, dimension, settings);
        if (!navGrid)
            return NavPathStatus::Failed;
        return FindPathOnGrid(*navGrid, dimension, *settings, from, to, smooth, spline, path);
    }

    NavPathStatus Scene::FindNavPath(const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& path)
    {
        return FindNavPath(from, to, path, Entity(), true, false);
    }

    bool Scene::HasNavLineOfSight(const glm::vec3& from, const glm::vec3& to)
    {
        return HasNavLineOfSight(from, to, Entity());
    }

    bool Scene::HasNavLineOfSight(const glm::vec3& from, const glm::vec3& to, Entity grid)
    {
        if (!grid)
            grid = FindNavGridEntity(from);
        int dimension;
        const NavGridPathSettings* settings;
        const NavGrid* navGrid = GetBakedGrid(grid, dimension, settings);
        return navGrid && navGrid->HasLineOfSight(ToPlane(from, dimension), ToPlane(to, dimension));
    }

    // ------------------------------------------------------------------ agents
    bool Scene::ComputeNavAgentPath(Entity agent, NavAgentComponent& a)
    {
        a.RepathRequested = false;
        a.RepathTimer = std::max(0.0f, a.RepathInterval);
        a.PathRequests++;
        a.PathIndex = 1;
        int dimension;
        const NavGridPathSettings* settings;
        const NavGrid* grid = GetBakedGrid(GetNavAgentGrid(agent), dimension, settings);
        if (!grid)
        {
            a.Path.clear();
            a.PathStatus = NavPathStatus::Failed;
            a.Remaining = 0.0f;
            return false;
        }
        a.PathStatus = FindPathOnGrid(*grid, dimension, *settings, GetWorldPosition(agent), a.Destination, a.SmoothPath, a.SplinePath, a.Path);
        a.GridRevision = grid->GetRevision();
        a.Remaining = NavPathSmoothing::GetLength(a.Path);
        if (a.Path.size() < 2)
        {
            // Nothing to walk: there already, or no way to get nearer.
            a.Arrived = a.PathStatus != NavPathStatus::Failed;
            a.Path.clear();
            a.Remaining = 0.0f;
        }
        else
            a.Arrived = false;
        return a.PathStatus != NavPathStatus::Failed;
    }

    bool Scene::SetNavDestination(Entity agent, const glm::vec3& destination)
    {
        auto* a = agent && agent.IsValid() ? agent.TryGetComponent<NavAgentComponent>() : nullptr;
        if (!a)
            return false;
        a->SetDestination(destination);
        return ComputeNavAgentPath(agent, *a);
    }

    void Scene::UpdateNavAgents(float dt)
    {
        auto view = m_Registry.view<NavAgentComponent>();
        if (view.empty() || !(dt > 0.0f))
            return;
        ZM_PROFILE_SCOPE("Navigatsiya agentlari");
        std::vector<glm::vec2> plane;
        for (auto e : view)
        {
            Entity entity{ e, this };
            if (!IsActiveInHierarchy(entity))
                continue;
            auto& a = m_Registry.get<NavAgentComponent>(e);
            int dimension;
            const NavGridPathSettings* settings;
            const NavGrid* grid = GetBakedGrid(GetNavAgentGrid(entity), dimension, settings);
            const int dim = grid ? dimension : (entity.HasComponent<CharacterController3DComponent>() || entity.HasComponent<Rigidbody3DComponent>() ? 3 : 2);

            // A target: it keeps going after it (the path is found again every RepathInterval, or when
            // the target has gone more than a cell from where the path ends).
            if (a.Target)
            {
                Entity target = GetEntityByUUID(a.Target.ID);
                if (target && target != entity && IsActiveInHierarchy(target))
                {
                    a.Destination = GetWorldPosition(target);
                    if (!a.HasDestination)
                    {
                        a.HasDestination = true;
                        a.RepathRequested = true;
                    }
                    if (grid && a.RepathInterval <= 0.0f)
                    {
                        const glm::vec3 end = a.Path.empty() ? GetWorldPosition(entity) : a.Path.back();
                        if (glm::length(ToPlane(end, dim) - ToPlane(a.Destination, dim)) > std::max(grid->GetCellSize(), a.StoppingDistance))
                            a.RepathRequested = true;
                    }
                }
            }

            if (a.HasDestination)
            {
                a.RepathTimer -= dt;
                bool repath = a.RepathRequested;
                if (grid && a.GridRevision != grid->GetRevision())
                    repath = true;
                if (a.RepathInterval > 0.0f && a.RepathTimer <= 0.0f && (!a.Arrived || a.Target))
                    repath = true;
                if (repath)
                    ComputeNavAgentPath(entity, a);
            }

            const glm::vec3 position = GetWorldPosition(entity);
            const glm::vec2 p = ToPlane(position, dim);
            glm::vec2 desired(0.0f);
            if (grid && a.HasDestination && !a.Arrived && a.Path.size() >= 2)
            {
                plane.clear();
                for (const glm::vec3& corner : a.Path)
                    plane.push_back(ToPlane(corner, dim));
                // Corners passed (beyond its segment's end, or near enough).
                const float reach = std::max(0.05f, a.Radius * 0.5f);
                a.PathIndex = std::clamp(a.PathIndex, 1, (int)plane.size() - 1);
                while (a.PathIndex < (int)plane.size() - 1)
                {
                    const glm::vec2 s0 = plane[(size_t)a.PathIndex - 1], s1 = plane[(size_t)a.PathIndex];
                    const glm::vec2 segment = s1 - s0;
                    const float length2 = glm::dot(segment, segment);
                    const float t = length2 > 1e-10f ? glm::dot(p - s0, segment) / length2 : 1.0f;
                    if (t >= 1.0f || glm::length(p - s1) <= reach)
                        a.PathIndex++;
                    else
                        break;
                }
                const glm::vec2 corner = plane[(size_t)a.PathIndex];
                float remaining = glm::length(corner - p);
                for (size_t i = (size_t)a.PathIndex + 1; i < plane.size(); i++)
                    remaining += glm::length(plane[i] - plane[i - 1]);
                a.Remaining = remaining;
                const float stop = std::max(0.01f, std::isfinite(a.StoppingDistance) ? a.StoppingDistance : 0.1f);
                if (remaining <= stop)
                {
                    a.Arrived = true;
                    a.Path.clear();
                    a.PathIndex = 0;
                    a.Remaining = 0.0f;
                    if (a.AutoBraking)
                        a.Velocity = glm::vec3(0.0f);
                }
                else
                {
                    // Towards the point LookAhead along the path (the next corner when the way there is
                    // not clear).
                    glm::vec2 target = LookAheadPoint(plane, a.PathIndex, p, a.LookAhead);
                    if (!grid->HasLineOfSight(p, target) && grid->IsWalkable(grid->WorldToCell(p)))
                        target = corner;
                    glm::vec2 direction = target - p;
                    if (glm::dot(direction, direction) < 1e-10f)
                        direction = corner - p;
                    const float length = glm::length(direction);
                    if (length > 1e-6f)
                    {
                        float speed = std::max(0.0f, a.Speed);
                        if (a.AutoBraking && a.Acceleration > 0.0f)
                        {
                            // Slow enough to stop at the end with Acceleration (a little speed kept so it gets there).
                            const float braking = std::sqrt(2.0f * a.Acceleration * std::max(0.0f, remaining - stop));
                            speed = std::min(speed, std::max(braking, std::min(speed, 0.2f)));
                        }
                        desired = direction / length * speed;
                    }
                }
            }
            else if (a.HasDestination && !a.Arrived && (!grid || a.Path.size() < 2) && a.PathStatus != NavPathStatus::None)
                a.Remaining = 0.0f;

            // Speeding up / braking.
            glm::vec2 velocity = ToPlane(a.Velocity, dim);
            velocity = a.Acceleration > 0.0f ? Math::MoveTowards(velocity, desired, a.Acceleration * dt) : desired;
            a.Velocity = dim == 3 ? glm::vec3(velocity.x, 0.0f, velocity.y) : glm::vec3(velocity, 0.0f);
            const bool moving = glm::dot(velocity, velocity) > 1e-8f;
            const glm::vec2 step = velocity * dt;

            bool dynamicBody = false;
            if (dim == 3)
            {
                auto* cc = m_Registry.try_get<CharacterController3DComponent>(e);
                auto* rb = m_Registry.try_get<Rigidbody3DComponent>(e);
                if (cc && cc->RuntimeID != 0xffffffff)
                    cc->Move(a.Velocity);
                else if (rb && rb->RuntimeBodyID != 0xffffffff && rb->Type == Rigidbody3DComponent::BodyType::Dynamic && m_PhysicsWorld3D)
                {
                    dynamicBody = true;
                    const glm::vec3 current = m_PhysicsWorld3D->GetLinearVelocity(rb->RuntimeBodyID);
                    m_PhysicsWorld3D->SetLinearVelocity(rb->RuntimeBodyID, { velocity.x, current.y, velocity.y });
                }
                else if (moving)
                {
                    // Moved by its transform (kinematic bodies follow it): on the grid's floor.
                    glm::vec3 next = position + glm::vec3(step.x, 0.0f, step.y);
                    if (grid)
                        next.y = grid->SampleFloorHeight({ next.x, next.z }) + a.BaseOffset;
                    SetEntityWorldPosition(*this, entity, next);
                }
            }
            else
            {
                auto* rb = m_Registry.try_get<Rigidbody2DComponent>(e);
                b2Body* body = rb ? (b2Body*)rb->RuntimeBody : nullptr;
                if (body && rb->Type == Rigidbody2DComponent::BodyType::Dynamic)
                {
                    dynamicBody = true;
                    PhysicsWorld2D::SetLinearVelocity(body, velocity);
                }
                else
                {
                    if (moving)
                        SetEntityWorldPosition(*this, entity, position + glm::vec3(step, 0.0f));
                    // A kinematic body also gets the velocity: it carries / pushes what it touches.
                    if (body && rb->Type == Rigidbody2DComponent::BodyType::Kinematic)
                        PhysicsWorld2D::SetLinearVelocity(body, velocity);
                }
            }

            // Facing the way it goes (bodies the physics turns are left alone).
            if (a.UpdateRotation && a.AngularSpeed > 0.0f && !dynamicBody && glm::dot(velocity, velocity) > 0.0025f)
            {
                auto& transform = entity.GetComponent<TransformComponent>();
                const float maxTurn = glm::radians(a.AngularSpeed) * dt;
                if (dim == 3)
                    transform.Rotation.y = Math::MoveTowardsAngle(transform.Rotation.y, std::atan2(-velocity.x, -velocity.y), maxTurn);
                else
                    transform.Rotation.z = Math::MoveTowardsAngle(transform.Rotation.z, std::atan2(velocity.y, velocity.x), maxTurn);
            }
        }
    }

}
