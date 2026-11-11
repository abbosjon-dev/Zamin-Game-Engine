// Navigation in the scene: baking the NavGrid 2D / NavGrid 3D components from the level, path
// queries for scripts, and the Nav Agents walking their paths.
#include "Zamin/Scene/Scene.h"

#include "Zamin/AI/NavCarving.h"
#include "Zamin/AI/NavGrid.h"
#include "Zamin/AI/NavGridBuilder.h"
#include "Zamin/AI/NavPathSmoothing.h"
#include "Zamin/AI/NavPathfinder.h"
#include "Zamin/AI/Steering.h"
#include "Zamin/Core/JobSystem.h"
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
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Scene/TilesetProperties.h"

#include <filesystem>

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

        float NavigationElapsedMilliseconds(std::chrono::steady_clock::time_point since)
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

        // Whether the path ahead (from the agent's position through the corners from `index` on)
        // crosses a cell that is not walkable now. The first leg counts only when the agent stands on
        // a walkable cell (one carved under its feet: the new path starts from the nearest free one).
        bool PathBlocked(const NavGrid& grid, const glm::vec2& position, const std::vector<glm::vec2>& path, int index)
        {
            glm::vec2 from = position;
            bool check = grid.IsWalkable(grid.WorldToCell(position));
            for (size_t i = (size_t)std::max(index, 1); i < path.size(); i++)
            {
                if (check && !grid.HasLineOfSight(from, path[i]))
                    return true;
                from = path[i];
                check = true;
            }
            return false;
        }

        // `entity` is `root` or below it in the hierarchy.
        bool IsSelfOrBelow(Scene& scene, Entity entity, Entity root)
        {
            for (int steps = 0; entity && steps < 256; steps++)
            {
                if (entity == root)
                    return true;
                entity = scene.GetParent(entity);
            }
            return false;
        }

        // A body the grid already knows about (static colliders, tilemaps).
        bool IsStaticBody(Entity entity, int dimension)
        {
            if (dimension == 3)
            {
                const auto* rb = entity.TryGetComponent<Rigidbody3DComponent>();
                return rb && rb->Type == Rigidbody3DComponent::BodyType::Static;
            }
            if (entity.HasComponent<TilemapComponent>())
                return true;
            const auto* rb = entity.TryGetComponent<Rigidbody2DComponent>();
            return rb && rb->Type == Rigidbody2DComponent::BodyType::Static;
        }

        // ---- obstacles
        // The points of an obstacle's shape in the world: a box's 8 corners; a circle's centre, a
        // point on its rim along local X, Z and Y, and the ends of its height (3D: along local Y).
        void GetObstaclePoints(const NavObstacleComponent& c, const glm::mat4& world, std::vector<glm::vec3>& out)
        {
            out.clear();
            auto toWorld = [&](const glm::vec3& p) { return glm::vec3(world * glm::vec4(p, 1.0f)); };
            if (c.Shape == NavObstacleComponent::ShapeType::Box)
            {
                const glm::vec3 h = glm::max(c.Size, glm::vec3(0.0f)) * 0.5f;
                for (int i = 0; i < 8; i++)
                    out.push_back(toWorld(c.Center + glm::vec3(i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z)));
                return;
            }
            const float r = std::max(0.0f, c.Radius), hh = std::max(0.0f, c.Size.y) * 0.5f;
            out.push_back(toWorld(c.Center));
            out.push_back(toWorld(c.Center + glm::vec3(r, 0.0f, 0.0f)));
            out.push_back(toWorld(c.Center + glm::vec3(0.0f, 0.0f, r)));
            out.push_back(toWorld(c.Center + glm::vec3(0.0f, r, 0.0f)));
            out.push_back(toWorld(c.Center - glm::vec3(0.0f, hh, 0.0f)));
            out.push_back(toWorld(c.Center + glm::vec3(0.0f, hh, 0.0f)));
        }

        // The shape's footprint on a grid's plane (2D: XY, 3D: XZ with its height span).
        NavCarveShape MakeCarveShape(const NavObstacleComponent& c, const std::vector<glm::vec3>& points, int dimension)
        {
            NavCarveShape shape;
            if (points.empty())
                return shape;
            if (c.Shape == NavObstacleComponent::ShapeType::Box)
            {
                std::vector<glm::vec2> flat;
                flat.reserve(points.size());
                for (const glm::vec3& p : points)
                    flat.push_back(ToPlane(p, dimension));
                shape.Outline = NavCarving::ConvexHull(std::move(flat));
                if (shape.Outline.size() == 1)
                {
                    // A box with no size: a point.
                    shape.Center = shape.Outline[0];
                    shape.Outline.clear();
                }
            }
            else
            {
                const glm::vec2 center = ToPlane(points[0], dimension);
                shape.Center = center;
                // 2D: the disc lies in the local XY plane; 3D: in XZ (an upright cylinder).
                const glm::vec2 a = ToPlane(points[1], dimension) - center;
                const glm::vec2 b = ToPlane(points[dimension == 3 ? 2 : 3], dimension) - center;
                shape.Radius = std::max(glm::length(a), glm::length(b));
            }
            if (dimension == 3)
            {
                shape.Bottom = std::numeric_limits<float>::max();
                shape.Top = std::numeric_limits<float>::lowest();
                for (size_t i = c.Shape == NavObstacleComponent::ShapeType::Box ? 0 : 4; i < points.size(); i++)
                {
                    shape.Bottom = std::min(shape.Bottom, points[i].y);
                    shape.Top = std::max(shape.Top, points[i].y);
                }
            }
            return shape;
        }

        float MaxDisplacement(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b)
        {
            if (a.size() != b.size())
                return std::numeric_limits<float>::max();
            float d = 0.0f;
            for (size_t i = 0; i < a.size(); i++)
                d = std::max(d, glm::length(a[i] - b[i]));
            return d;
        }

        // A baked grid an obstacle may carve, with what its cells keep clear of.
        struct CarveTarget
        {
            uint64_t ID = 0;
            int Dimension = 2;
            NavGrid* Grid = nullptr;
            float Clearance = 0.0f, StepHeight = 0.0f, AgentHeight = 0.0f;
        };

        void CollectCarveTargets(Scene& scene, const NavObstacleComponent& c, std::vector<CarveTarget>& out)
        {
            out.clear();
            auto add = [&](Entity e) {
                if (!scene.IsActiveInHierarchy(e))
                    return;
                if (auto* g = e.TryGetComponent<NavGrid2DComponent>(); g && g->Baked && !g->Grid.IsEmpty())
                    out.push_back({ (uint64_t)e.GetUUID(), 2, &g->Grid, std::isfinite(g->AgentRadius) ? g->AgentRadius : 0.0f, 0.0f, 0.0f });
                if (auto* g = e.TryGetComponent<NavGrid3DComponent>(); g && g->Baked && !g->Grid.IsEmpty())
                    out.push_back({ (uint64_t)e.GetUUID(), 3, &g->Grid, std::isfinite(g->AgentRadius) ? g->AgentRadius : 0.0f,
                                    std::isfinite(g->StepHeight) ? g->StepHeight : 0.4f, std::isfinite(g->AgentHeight) ? g->AgentHeight : 1.8f });
            };
            if (c.Grid)
            {
                if (Entity e = scene.GetEntityByUUID(c.Grid.ID))
                    add(e);
                return;
            }
            std::vector<Entity> grids;
            for (auto e : scene.GetAllEntitiesWith<NavGrid2DComponent>())
                grids.push_back({ e, &scene });
            for (auto e : scene.GetAllEntitiesWith<NavGrid3DComponent>())
                if (!scene.GetRegistry().all_of<NavGrid2DComponent>(e))
                    grids.push_back({ e, &scene });
            for (Entity e : grids)
                add(e);
        }

        uint64_t CarveSignature(const std::vector<CarveTarget>& targets)
        {
            SourceHasher h;
            for (const CarveTarget& t : targets)
            {
                h.Value(t.ID);
                h.Value(t.Dimension);
                h.Value(t.Grid->GetSerial());
                h.Value(t.Clearance);
            }
            return h.Hash;
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
        // The sources summed: the same level gives the same fingerprint whatever order its entities
        // are in (the editor's scene and the one loaded from its file). Their bounds as the bake finds
        // them (GetNavGrid2DSourceBounds): without any, AutoBounds falls back to Size around the entity.
        uint64_t sources = 0;
        glm::vec2 boundsMin(std::numeric_limits<float>::max()), boundsMax(std::numeric_limits<float>::lowest());
        bool anySource = false;
        ForEachNavTilemap(*this, *c, [&](Entity e, const TilemapComponent& tm) {
            glm::vec2 lo, hi;
            NavGridBuilder::GetTilemapBounds(tm, GetWorldTransform(e), lo, hi);
            boundsMin = glm::min(boundsMin, lo);
            boundsMax = glm::max(boundsMax, hi);
            anySource = true;
            SourceHasher t;
            t.Value(GetWorldTransform(e));
            t.Value(tm.Width);
            t.Value(tm.Height);
            t.Value(tm.TileSize);
            t.Bytes(tm.TilesetPath.data(), tm.TilesetPath.size());
            if (const TilesetProperties* properties = TilesetProperties::Get(tm.TilesetPath))
                for (TileCollision kind : { TileCollision::None, TileCollision::OneWay })
                {
                    const std::vector<int> tiles = properties->GetTilesWith(kind);
                    t.Value(kind);
                    if (!tiles.empty())
                        t.Bytes(tiles.data(), tiles.size() * sizeof(int));
                }
            for (const TilemapLayer& layer : tm.Layers)
            {
                t.Value(layer.Collision);
                if (layer.Collision && !layer.Tiles.empty())
                    t.Bytes(layer.Tiles.data(), layer.Tiles.size() * sizeof(int));
            }
            sources += t.Hash;
        });
        if (c->UseColliders)
        {
            std::vector<NavShape2D> shapes;
            CollectNavShapes2D(*this, c->LayerMask, c->OneWayBlocks, shapes);
            for (const NavShape2D& shape : shapes)
            {
                GetShapeBounds(shape, boundsMin, boundsMax);
                anySource = true;
                SourceHasher t;
                t.Value(shape.Type);
                t.Value(shape.Center);
                t.Value(shape.Radius);
                t.Value(shape.Loop);
                if (!shape.Points.empty())
                    t.Bytes(shape.Points.data(), shape.Points.size() * sizeof(glm::vec2));
                sources += t.Hash;
            }
        }
        h.Value(sources);
        // AutoBounds with no sources: the area is Size around the entity, so where it is counts too.
        if (c->AutoBounds && !(anySource && boundsMin.x <= boundsMax.x && boundsMin.y <= boundsMax.y))
            h.Value(GetWorldPosition(entity));
        return h.Hash;
    }

    // What a NavGrid 3D bake reads: its settings and where it is, the collision matrix, and every
    // active static 3D body - its transform, layer and colliders (a model collider's file by its
    // path and size) - summed in any order.
    static uint64_t NavGrid3DSourceHash(Scene& scene, Entity entity)
    {
        const auto* c = entity ? entity.TryGetComponent<NavGrid3DComponent>() : nullptr;
        if (!c)
            return 0;
        SourceHasher h;
        h.Value(scene.GetWorldPosition(entity));
        h.Value(c->Offset);
        h.Value(c->Size);
        h.Value(c->CellSize);
        h.Value(c->AgentRadius);
        h.Value(c->AgentHeight);
        h.Value(c->MaxSlope);
        h.Value(c->StepHeight);
        h.Value(c->LayerMask);
        h.Value(Project::GetCollisionLayers2D().Masks);
        auto modelFile = [&](SourceHasher& t, const std::string& colliderPath, Entity body) {
            std::string path = colliderPath;
            if (path.empty())
                if (const auto* mr = body.TryGetComponent<MeshRendererComponent>())
                {
                    t.Value(mr->Type);
                    path = mr->ModelPath;
                }
            t.Bytes(path.data(), path.size());
            if (!path.empty())
            {
                std::error_code ec;
                const uintmax_t size = std::filesystem::file_size(Project::GetAssetFileSystemPath(path), ec);
                t.Value(ec ? (uintmax_t)0 : size);
            }
        };
        uint64_t sources = 0;
        for (auto e : scene.GetAllEntitiesWith<Rigidbody3DComponent>())
        {
            Entity body{ e, &scene };
            const auto& rb = body.GetComponent<Rigidbody3DComponent>();
            if (rb.Type != Rigidbody3DComponent::BodyType::Static || !scene.IsActiveInHierarchy(body))
                continue;
            SourceHasher t;
            t.Value(scene.GetWorldTransform(body));
            t.Value(rb.Layer);
            if (const auto* b = body.TryGetComponent<BoxCollider3DComponent>())
            {
                t.Value(1);
                t.Value(b->HalfExtents);
                t.Value(b->Offset);
                t.Value(b->IsSensor);
            }
            if (const auto* sc = body.TryGetComponent<SphereCollider3DComponent>())
            {
                t.Value(2);
                t.Value(sc->Radius);
                t.Value(sc->Offset);
                t.Value(sc->IsSensor);
            }
            if (const auto* cc = body.TryGetComponent<CapsuleCollider3DComponent>())
            {
                t.Value(3);
                t.Value(cc->Radius);
                t.Value(cc->HalfHeight);
                t.Value(cc->Offset);
                t.Value(cc->IsSensor);
            }
            if (const auto* hc = body.TryGetComponent<ConvexHullCollider3DComponent>())
            {
                t.Value(4);
                modelFile(t, hc->ModelPath, body);
                t.Value(hc->MaxVertices);
                t.Value(hc->Offset);
                t.Value(hc->IsSensor);
            }
            if (const auto* mc = body.TryGetComponent<MeshCollider3DComponent>())
            {
                t.Value(5);
                modelFile(t, mc->ModelPath, body);
                t.Value(mc->Offset);
                t.Value(mc->IsSensor);
            }
            sources += t.Hash;
        }
        h.Value(sources);
        return h.Hash;
    }

    uint64_t Scene::GetNavGridSourceHash(Entity entity, int dimension)
    {
        if (!entity || !entity.IsValid())
            return 0;
        if (dimension != 2 && entity.HasComponent<NavGrid3DComponent>())
            return NavGrid3DSourceHash(*this, entity);
        if (dimension != 3 && entity.HasComponent<NavGrid2DComponent>())
            return GetNavGrid2DSourceHash(entity);
        return 0;
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
        c->FromBakeFile = false;
        c->BakeMilliseconds = NavigationElapsedMilliseconds(clock);
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
        c->FromBakeFile = false;
        c->BakeMilliseconds = NavigationElapsedMilliseconds(clock);
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

    bool Scene::LoadSavedNavBake(Entity entity, int dimension)
    {
        if (!m_NavBakes || m_NavBakes->empty())
            return false;
        const uint64_t id = (uint64_t)entity.GetUUID();
        const NavBakeEntry* saved = nullptr;
        for (const NavBakeEntry& e : *m_NavBakes)
            if (e.Grid == id && e.Dimension == dimension && !e.Data.IsEmpty())
                saved = &e;
        if (!saved)
            return false;
        const auto clock = std::chrono::steady_clock::now();
        const uint64_t fingerprint = GetNavGridSourceHash(entity, dimension);
        if (fingerprint != saved->Fingerprint)
        {
            ZM_CORE_INFO("Navigatsiya: '{}' grid'ining manbalari .znav saqlangandan beri o'zgargan - qayta yaratiladi", entity.GetName());
            return false;
        }
        if (dimension == 3)
        {
            auto& c = entity.GetComponent<NavGrid3DComponent>();
            c.Grid = saved->Data;
            c.Baked = c.FromBakeFile = true;
            c.BakeCount++;
            c.BakeMilliseconds = NavigationElapsedMilliseconds(clock);
        }
        else
        {
            auto& c = entity.GetComponent<NavGrid2DComponent>();
            c.Grid = saved->Data;
            c.Baked = c.FromBakeFile = true;
            c.BakeCount++;
            c.SourceHash = fingerprint; // AutoRebuild compares with it
            c.RebuildTimer = 0.0f;
            c.BakeMilliseconds = NavigationElapsedMilliseconds(clock);
        }
        return true;
    }

    NavBakeList Scene::CollectNavBakes()
    {
        NavBakeList list;
        auto add = [&](Entity e, int dimension, const NavGrid& grid) {
            NavBakeEntry entry;
            entry.Grid = (uint64_t)e.GetUUID();
            entry.Dimension = dimension;
            entry.Fingerprint = GetNavGridSourceHash(e, dimension);
            entry.Data = grid;
            entry.Data.ClearCarves();
            list.push_back(std::move(entry));
        };
        std::vector<Entity> grids;
        for (auto e : m_Registry.view<NavGrid2DComponent>())
            grids.push_back({ e, this });
        for (auto e : m_Registry.view<NavGrid3DComponent>())
            if (!m_Registry.all_of<NavGrid2DComponent>(e))
                grids.push_back({ e, this });
        // In UUID order: the same scene writes the same file.
        std::sort(grids.begin(), grids.end(), [](Entity a, Entity b) { return (uint64_t)a.GetUUID() < (uint64_t)b.GetUUID(); });
        for (Entity e : grids)
        {
            if (!IsActiveInHierarchy(e))
                continue;
            // Only grids the game bakes at start read the saved bake (CacheBake is kept, hidden, while
            // BakeOnStart is off).
            if (auto* c = e.TryGetComponent<NavGrid3DComponent>(); c && c->Paths.CacheBake && c->Paths.BakeOnStart && BakeNavGrid3D(e))
                add(e, 3, c->Grid);
            if (auto* c = e.TryGetComponent<NavGrid2DComponent>(); c && c->Paths.CacheBake && c->Paths.BakeOnStart && BakeNavGrid2D(e))
                add(e, 2, c->Grid);
        }
        return list;
    }

    void Scene::BakeNavGridOnStart(Entity e)
    {
        if (auto* c = e.TryGetComponent<NavGrid2DComponent>(); c && c->StartBakePending)
        {
            c->StartBakePending = false;
            if (c->Paths.CacheBake && LoadSavedNavBake(e, 2))
                ZM_CORE_INFO("Navigatsiya: '{}' 2D grid'i .znav faylidan olindi: {}x{} katak ({:.1f} ms)", e.GetName(), c->Grid.GetWidth(),
                             c->Grid.GetHeight(), c->BakeMilliseconds);
            else if (BakeNavGrid2D(e))
                ZM_CORE_INFO("Navigatsiya: '{}' 2D grid'i {}x{} katak, {} tasi yuriladigan ({:.1f} ms)", e.GetName(), c->Grid.GetWidth(),
                             c->Grid.GetHeight(), c->Grid.CountWalkable(), c->BakeMilliseconds);
        }
        if (auto* c = e.TryGetComponent<NavGrid3DComponent>(); c && c->StartBakePending)
        {
            c->StartBakePending = false;
            if (c->Paths.CacheBake && LoadSavedNavBake(e, 3))
                ZM_CORE_INFO("Navigatsiya: '{}' 3D grid'i .znav faylidan olindi: {}x{} katak ({:.1f} ms)", e.GetName(), c->Grid.GetWidth(),
                             c->Grid.GetHeight(), c->BakeMilliseconds);
            else if (BakeNavGrid3D(e))
                ZM_CORE_INFO("Navigatsiya: '{}' 3D grid'i {}x{} katak, {} tasi yuriladigan, {} ta statik jism ({:.1f} ms)", e.GetName(),
                             c->Grid.GetWidth(), c->Grid.GetHeight(), c->Grid.CountWalkable(), c->BakeBodies, c->BakeMilliseconds);
        }
    }

    void Scene::BakePendingNavGrids()
    {
        // Each grid entity once (both components bake in one go), active ones only: the others when
        // they are switched on.
        std::vector<Entity> grids;
        for (auto e : m_Registry.view<NavGrid2DComponent>())
            if (m_Registry.get<NavGrid2DComponent>(e).StartBakePending)
                grids.push_back({ e, this });
        for (auto e : m_Registry.view<NavGrid3DComponent>())
            if (m_Registry.get<NavGrid3DComponent>(e).StartBakePending)
                if (const auto* c2 = m_Registry.try_get<NavGrid2DComponent>(e); !c2 || !c2->StartBakePending)
                    grids.push_back({ e, this });
        for (Entity e : grids)
            if (e.IsValid() && IsActiveInHierarchy(e))
                BakeNavGridOnStart(e);
    }

    void Scene::StartNavigation()
    {
        for (auto e : m_Registry.view<NavGrid2DComponent>())
        {
            auto& c = m_Registry.get<NavGrid2DComponent>(e);
            c.StartBakePending = c.Paths.BakeOnStart;
        }
        for (auto e : m_Registry.view<NavGrid3DComponent>())
        {
            auto& c = m_Registry.get<NavGrid3DComponent>(e);
            c.StartBakePending = c.Paths.BakeOnStart;
        }
        BakePendingNavGrids();
    }

    void Scene::StartSpawnedNavGrids(const std::vector<Entity>& entities)
    {
        // A spawned grid starts as at play start: a copy of a live grid does not keep the bake made
        // where the original is.
        bool pending = false;
        for (Entity e : entities)
        {
            if (!e.IsValid())
                continue;
            if (auto* c = e.TryGetComponent<NavGrid2DComponent>())
            {
                c->ResetRuntime();
                c->StartBakePending = c->Paths.BakeOnStart;
                pending |= c->StartBakePending;
            }
            if (auto* c = e.TryGetComponent<NavGrid3DComponent>())
            {
                c->ResetRuntime();
                c->StartBakePending = c->Paths.BakeOnStart;
                pending |= c->StartBakePending;
            }
        }
        if (pending)
            BakePendingNavGrids();
    }

    void Scene::UpdateNavGrids(float dt)
    {
        // BakeOnStart grids that were switched off until now (switching on bakes them already; this
        // catches any left).
        BakePendingNavGrids();
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
        if (m_SceneManager)
            if (auto* world = m_SceneManager->GetWorldStreaming(); world && world->IsAnchor(this))
            {
                auto root = GetEntityByUUID(world->GetNavigationEntity(dimension));
                if (root && IsActiveInHierarchy(root))
                {
                    if (const auto* c = root.TryGetComponent<NavGrid2DComponent>(); c && c->Baked && !c->Grid.IsEmpty()) return root;
                    if (const auto* c = root.TryGetComponent<NavGrid3DComponent>(); c && c->Baked && !c->Grid.IsEmpty()) return root;
                }
            }
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
        NavPathOptions options = MakeOptions(settings);
        if (dimension == 3)
            options.StartHeight = from.y; // the floor it stands on
        t_Pathfinder.FindPath(grid, a, b, result, options);
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
    namespace {

        // One agent's path search: what it needs (filled on the main thread), what it found (filled
        // by whichever thread runs it - only the grid is read, and each thread has its own pathfinder).
        struct NavPathRequest
        {
            NavAgentComponent* Agent = nullptr;
            const NavGrid* Grid = nullptr;
            int Dimension = 2;
            const NavGridPathSettings* Settings = nullptr;
            glm::vec3 From{ 0.0f }, To{ 0.0f };
            bool Smooth = true, Spline = false;
            std::vector<glm::vec3> Path;
            NavPathStatus Status = NavPathStatus::None;
        };

        // The bookkeeping of a new search; false when the agent has no baked grid (the path failed).
        bool PrepareNavPath(NavAgentComponent& a, const NavGrid* grid, int dimension, const NavGridPathSettings* settings, const glm::vec3& from,
                            NavPathRequest& request)
        {
            a.RepathRequested = false;
            a.RepathTimer = std::max(0.0f, a.RepathInterval);
            a.PathRequests++;
            a.PathIndex = 1;
            if (!grid || !settings)
            {
                a.Path.clear();
                a.PathStatus = NavPathStatus::Failed;
                a.Remaining = 0.0f;
                return false;
            }
            request.Agent = &a;
            request.Grid = grid;
            request.Dimension = dimension;
            request.Settings = settings;
            request.From = from;
            request.To = a.Destination;
            request.Smooth = a.SmoothPath;
            request.Spline = a.SplinePath;
            return true;
        }

        void RunNavPath(NavPathRequest& r)
        {
            r.Status = FindPathOnGrid(*r.Grid, r.Dimension, *r.Settings, r.From, r.To, r.Smooth, r.Spline, r.Path);
        }

        void ApplyNavPath(NavPathRequest& r)
        {
            NavAgentComponent& a = *r.Agent;
            a.PathStatus = r.Status;
            a.Path = std::move(r.Path);
            a.GridRevision = r.Grid->GetRevision();
            a.CarveRevision = r.Grid->GetCarveRevision();
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
        }

    }

    bool Scene::ComputeNavAgentPath(Entity agent, NavAgentComponent& a)
    {
        if (a.WorldNavigation) return ComputeWorldNavAgentPath(agent, a);
        int dimension;
        const NavGridPathSettings* settings;
        const NavGrid* grid = GetBakedGrid(GetNavAgentGrid(agent), dimension, settings);
        NavPathRequest request;
        if (!PrepareNavPath(a, grid, dimension, settings, GetWorldPosition(agent), request))
            return false;
        RunNavPath(request);
        ApplyNavPath(request);
        return a.PathStatus != NavPathStatus::Failed;
    }

    bool Scene::SetNavDestination(Entity agent, const glm::vec3& destination)
    {
        auto* a = agent && agent.GetScene() == this && agent.IsValid() ? agent.TryGetComponent<NavAgentComponent>() : nullptr;
        if (!a || !std::isfinite(destination.x) || !std::isfinite(destination.y) || !std::isfinite(destination.z))
            return false;
        a->SetDestination(destination);
        return ComputeNavAgentPath(agent, *a);
    }

    bool Scene::IsNavPathBlocked(Entity agent)
    {
        const auto* a = agent && agent.IsValid() ? agent.TryGetComponent<NavAgentComponent>() : nullptr;
        if (!a || a->Path.size() < 2)
            return false;
        if (a->WorldNavigation)
        {
            auto* world = GetSceneManager() ? GetSceneManager()->GetWorldStreaming() : nullptr;
            auto atlas = world && world->IsAnchor(this) ? world->GetNavigationAtlas() : nullptr;
            return !atlas || atlas->GetRevision() != a->AtlasRevision;
        }
        int dimension;
        const NavGridPathSettings* settings;
        const NavGrid* grid = GetBakedGrid(GetNavAgentGrid(agent), dimension, settings);
        if (!grid)
            return false;
        std::vector<glm::vec2> plane;
        plane.reserve(a->Path.size());
        for (const glm::vec3& corner : a->Path)
            plane.push_back(ToPlane(corner, dimension));
        return PathBlocked(*grid, ToPlane(GetWorldPosition(agent), dimension), plane, a->PathIndex);
    }

    // ------------------------------------------------------------------ obstacles
    void Scene::ReleaseNavObstacle(NavObstacleComponent& c)
    {
        for (const NavObstacleComponent::Carving& carving : c.Carvings)
        {
            Entity e = GetEntityByUUID(carving.Grid);
            if (!e)
                continue;
            NavGrid* grid = nullptr;
            if (carving.Dimension == 3)
            {
                if (auto* g = e.TryGetComponent<NavGrid3DComponent>())
                    grid = &g->Grid;
            }
            else if (auto* g = e.TryGetComponent<NavGrid2DComponent>())
                grid = &g->Grid;
            // A grid baked again since has none of these carves.
            if (grid && grid->GetSerial() == carving.Serial)
                grid->Uncarve(carving.Cells);
        }
        c.Carvings.clear();
        c.CarvedPoints.clear();
        c.GridSignature = 0;
    }

    void Scene::OnNavObstacleDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (auto* c = registry.try_get<NavObstacleComponent>(entity); c && !c->Carvings.empty())
            ReleaseNavObstacle(*c);
    }

    void Scene::UpdateNavObstacles()
    {
        UpdateNavObstacles(0.0f);
    }

    void Scene::UpdateNavObstacles(float dt)
    {
        auto view = m_Registry.view<NavObstacleComponent>();
        if (view.empty())
            return;
        ZM_PROFILE_SCOPE("Navigatsiya to'siqlari");
        std::vector<glm::vec3> points;
        std::vector<CarveTarget> targets;
        std::vector<uint32_t> cells, added, removed;
        for (auto e : view)
        {
            Entity entity{ e, this };
            auto& c = m_Registry.get<NavObstacleComponent>(e);
            if (!IsActiveInHierarchy(entity))
            {
                if (!c.Carvings.empty())
                    ReleaseNavObstacle(c);
                c.LastPoints.clear();
                c.StillTime = 0.0f;
                continue;
            }
            GetObstaclePoints(c, GetWorldTransform(entity), points);
            // Standing still (CarveOnlyStationary): no corner moved since the last frame.
            if (!c.LastPoints.empty() && MaxDisplacement(points, c.LastPoints) <= 1e-4f)
                c.StillTime += std::max(0.0f, dt);
            else if (!c.LastPoints.empty())
                c.StillTime = 0.0f;
            c.LastPoints = points;
            if (c.CarveOnlyStationary && c.StillTime < std::max(0.0f, c.StationaryTime))
            {
                if (!c.Carvings.empty())
                    ReleaseNavObstacle(c);
                continue;
            }

            CollectCarveTargets(*this, c, targets);
            const uint64_t signature = CarveSignature(targets);
            const bool moved = c.CarvedPoints.empty() || MaxDisplacement(points, c.CarvedPoints) > std::max(0.0f, c.MoveThreshold);
            if (!moved && signature == c.GridSignature)
                continue;

            // Only the cells that changed are carved / given back.
            std::vector<NavObstacleComponent::Carving> next;
            std::vector<bool> kept(c.Carvings.size(), false);
            for (const CarveTarget& t : targets)
            {
                NavCarving::CollectCells(*t.Grid, MakeCarveShape(c, points, t.Dimension), t.Clearance, t.StepHeight, t.AgentHeight, cells);
                const NavObstacleComponent::Carving* before = nullptr;
                for (size_t i = 0; i < c.Carvings.size(); i++)
                    if (c.Carvings[i].Grid == t.ID && c.Carvings[i].Dimension == t.Dimension && c.Carvings[i].Serial == t.Grid->GetSerial())
                    {
                        before = &c.Carvings[i];
                        kept[i] = true;
                        break;
                    }
                if (before)
                {
                    NavCarving::Diff(before->Cells, cells, added, removed);
                    t.Grid->Uncarve(removed);
                    t.Grid->Carve(added);
                }
                else
                    t.Grid->Carve(cells);
                if (!cells.empty())
                    next.push_back({ t.ID, t.Dimension, t.Grid->GetSerial(), cells });
            }
            // Grids it no longer covers (or no longer may carve) get their cells back.
            for (size_t i = 0; i < c.Carvings.size(); i++)
                if (!kept[i])
                {
                    NavObstacleComponent single;
                    single.Carvings.push_back(std::move(c.Carvings[i]));
                    ReleaseNavObstacle(single);
                }
            c.Carvings = std::move(next);
            c.CarvedPoints = points;
            c.GridSignature = signature;
            c.CarveCount++;
        }
    }

    // ------------------------------------------------------------------ avoidance
    glm::vec2 Scene::ComputeNavAvoidance(Entity agent, NavAgentComponent& a, const NavGrid* grid, int dimension, const glm::vec2& desired)
    {
        (void)grid;
        const float speed = glm::length(desired);
        if (speed < 1e-4f || !(a.FeelerLength > 0.0f))
            return glm::vec2(0.0f);
        const glm::vec2 forward = desired / speed;
        std::array<Steering::Feeler, 3> feelers =
            Steering::MakeFeelers(forward, a.FeelerLength, glm::radians(std::clamp(a.FeelerAngle, 0.0f, 89.0f)));
        const glm::vec3 position = GetWorldPosition(agent);
        const glm::vec3 origin = dimension == 3 ? position + glm::vec3(0.0f, a.FeelerHeight, 0.0f) : position;
        Entity target = a.Target ? GetEntityByUUID(a.Target.ID) : Entity();
        a.FeelerOrigin = origin;
        a.FeelerHits = 0;
        // The plane's sideways direction (the straight feeler is cast from both edges of the agent's
        // body: something just beside the centre line is in its way too).
        const glm::vec2 left2(-forward.y, forward.x);
        const glm::vec3 left = dimension == 3 ? glm::vec3(left2.x, 0.0f, left2.y) : glm::vec3(left2, 0.0f);
        const float halfWidth = std::max(0.0f, a.Radius) * 0.9f;
        auto cast = [&](Steering::Feeler& feeler, const glm::vec3& from) {
            const glm::vec3 direction = dimension == 3 ? glm::vec3(feeler.Direction.x, 0.0f, feeler.Direction.y) : glm::vec3(feeler.Direction, 0.0f);
            const std::vector<RaycastHit> hits = dimension == 3
                ? Raycast3DAll(from, direction, feeler.Length, a.AvoidanceMask, false)
                : Raycast2DAll(glm::vec2(from), feeler.Direction, feeler.Length, a.AvoidanceMask, false);
            for (const RaycastHit& hit : hits)
            {
                Entity other = GetEntityByUUID(hit.EntityID);
                if (!other || IsSelfOrBelow(*this, other, agent) || (target && IsSelfOrBelow(*this, other, target)) ||
                    (!a.AvoidStatic && IsStaticBody(other, dimension)))
                    continue;
                if (!feeler.Hit || hit.Distance < feeler.Distance)
                {
                    feeler.Hit = true;
                    feeler.Distance = hit.Distance;
                    feeler.Normal = ToPlane(hit.Normal, dimension);
                    feeler.HasCenter = true;
                    feeler.CenterOffset = ToPlane(GetWorldPosition(other) - position, dimension);
                }
                break;
            }
        };
        for (size_t i = 0; i < feelers.size(); i++)
        {
            Steering::Feeler& feeler = feelers[i];
            if (i == 0 && halfWidth > 1e-4f)
            {
                cast(feeler, origin + left * halfWidth);
                cast(feeler, origin - left * halfWidth);
            }
            else
                cast(feeler, origin);
            if (feeler.Hit)
                a.FeelerHits |= (uint8_t)(1u << i);
            const glm::vec3 direction = dimension == 3 ? glm::vec3(feeler.Direction.x, 0.0f, feeler.Direction.y) : glm::vec3(feeler.Direction, 0.0f);
            a.FeelerEnds[i] = origin + direction * (feeler.Hit ? feeler.Distance : feeler.Length);
        }
        a.FeelersActive = true;
        // The side it goes round on is kept until the way has been clear for a moment.
        a.AvoidClearTime = a.FeelerHits ? 0.0f : a.AvoidClearTime + std::max(0.0f, a.LastDeltaTime);
        if (a.AvoidClearTime > 0.5f)
            a.AvoidSide = 0;
        glm::vec2 push = Steering::AvoidObstacles(forward, feelers, std::max(0.0f, a.Speed), &a.AvoidSide) * std::max(0.0f, a.AvoidanceWeight);
        // Nothing in front any more: the last push fades out over a moment (else the path pulls it
        // straight back into the corner of what it just went round).
        const glm::vec2 last = ToPlane(a.AvoidancePush, dimension);
        const glm::vec2 fading = last * std::exp(-5.0f * std::max(0.0f, a.LastDeltaTime));
        if (glm::dot(fading, fading) > glm::dot(push, push))
            push = fading;
        return push;
    }

    void Scene::UpdateNavAgents(float dt)
    {
        auto view = m_Registry.view<NavAgentComponent>();
        if (view.empty() || !(dt > 0.0f))
            return;
        ZM_PROFILE_SCOPE("Navigatsiya agentlari");
        std::vector<glm::vec2> plane;
        // Each active agent's grid this frame (found once: the lookup walks the grids).
        struct AgentFrame
        {
            entt::entity Handle;
            const NavGrid* Grid;
            int Dim;
        };
        std::vector<AgentFrame> agents;
        agents.reserve(view.size());
        std::vector<NavPathRequest> requests;

        // ---- 1: who needs a new path (a target that moved, a blocked path, the repath timer)
        for (auto e : view)
        {
            Entity entity{ e, this };
            if (!IsActiveInHierarchy(entity))
                continue;
            auto& a = m_Registry.get<NavAgentComponent>(e);
            if (a.WorldNavigation) continue;
            int dimension;
            const NavGridPathSettings* settings;
            const NavGrid* grid = GetBakedGrid(GetNavAgentGrid(entity), dimension, settings);
            const int dim = grid ? dimension : (entity.HasComponent<CharacterController3DComponent>() || entity.HasComponent<Rigidbody3DComponent>() ? 3 : 2);

            // A target: it keeps going after it (the path is found again every RepathInterval, or when
            // the target has gone more than a cell from where the path ends) - unless Stop /
            // SetDestination paused that (ResumeTarget).
            const bool following = a.IsFollowingTarget();
            if (following)
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

            // The grid's carving changed (a Nav Obstacle moved): a new path when this one is blocked
            // now - or was partial (the way may be open again).
            if (grid && a.CarveRevision != grid->GetCarveRevision())
            {
                a.CarveRevision = grid->GetCarveRevision();
                if (a.HasDestination && a.PathStatus == NavPathStatus::Partial)
                    a.RepathRequested = true;
                else if (a.HasDestination && !a.Arrived && a.Path.size() >= 2)
                {
                    plane.clear();
                    for (const glm::vec3& corner : a.Path)
                        plane.push_back(ToPlane(corner, dim));
                    if (PathBlocked(*grid, ToPlane(GetWorldPosition(entity), dim), plane, a.PathIndex))
                    {
                        a.RepathRequested = true;
                        a.BlockedRepaths++;
                    }
                }
            }

            if (a.HasDestination)
            {
                a.RepathTimer -= dt;
                bool repath = a.RepathRequested;
                if (grid && a.GridRevision != grid->GetRevision())
                    repath = true;
                if (a.RepathInterval > 0.0f && a.RepathTimer <= 0.0f && (!a.Arrived || following))
                    repath = true;
                if (repath)
                {
                    NavPathRequest request;
                    if (PrepareNavPath(a, grid, dimension, settings, GetWorldPosition(entity), request))
                        requests.push_back(std::move(request));
                }
            }
            agents.push_back({ e, grid, dim });
        }

        // ---- 2: the searches - spread over the job system's workers when there are several (each
        // thread has its own pathfinder; the grids are only read), applied in the agents' order.
        m_NavFrameStats = {};
        m_NavFrameStats.Agents = (uint32_t)agents.size();
        m_NavFrameStats.PathRequests = (uint32_t)requests.size();
        if (!requests.empty())
        {
            ZM_PROFILE_SCOPE("Navigatsiya yo'llari");
            const auto clock = std::chrono::steady_clock::now();
            m_NavFrameStats.Parallel = requests.size() >= NavParallelPathThreshold && JobSystem::IsParallel();
            if (m_NavFrameStats.Parallel)
                JobSystem::ParallelFor((uint32_t)requests.size(), 1, [&](uint32_t begin, uint32_t end) {
                    for (uint32_t i = begin; i < end; i++)
                        RunNavPath(requests[i]);
                });
            else
                for (NavPathRequest& request : requests)
                    RunNavPath(request);
            for (NavPathRequest& request : requests)
                ApplyNavPath(request);
            m_NavFrameStats.PathMilliseconds = NavigationElapsedMilliseconds(clock);
        }

        // ---- 3: walking
        for (const AgentFrame& frame : agents)
        {
            const entt::entity e = frame.Handle;
            Entity entity{ e, this };
            auto& a = m_Registry.get<NavAgentComponent>(e);
            const NavGrid* grid = frame.Grid;
            const int dim = frame.Dim;

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

            // Local avoidance: the feelers' push blended in (never towards a cell the grid blocks).
            a.FeelersActive = false;
            a.FeelerHits = 0;
            a.LastDeltaTime = dt;
            if (!a.Avoidance || glm::dot(desired, desired) <= 1e-8f)
                a.AvoidancePush = glm::vec3(0.0f);
            else
            {
                const glm::vec2 push = ComputeNavAvoidance(entity, a, grid, dim, desired);
                a.AvoidancePush = dim == 3 ? glm::vec3(push.x, 0.0f, push.y) : glm::vec3(push, 0.0f);
                if (glm::dot(push, push) > 1e-8f)
                {
                    const float wanted = glm::length(desired);
                    glm::vec2 blended = Steering::Truncate<2>(desired + push, std::max(wanted, 1e-4f));
                    if (grid && glm::dot(blended, blended) > 1e-8f)
                    {
                        const float probe = std::max(grid->GetCellSize() * 0.5f, a.Radius);
                        const bool blockedThere = !grid->IsWalkable(grid->WorldToCell(p + glm::normalize(blended) * probe));
                        const bool blockedAhead = !grid->IsWalkable(grid->WorldToCell(p + desired / wanted * probe));
                        if (blockedThere && !blockedAhead)
                            blended = desired;
                    }
                    desired = blended;
                }
            }

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
        UpdateWorldNavAgents(dt);
    }

}
