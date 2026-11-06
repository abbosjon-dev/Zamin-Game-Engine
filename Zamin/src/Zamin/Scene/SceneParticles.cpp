// Particle systems of a scene: running every emitter (ParticleSimulation does the work) with what
// it needs from the scene - the Mesh shape's mesh, the collision plane and the physics world, the
// systems its sub-emitters emit into - and drawing a system (and its trails) into the 2D /
// billboard pass of the frame.
#include "Zamin/Scene/Scene.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Physics/CollisionShapes3D.h"
#include "Zamin/Physics/PhysicsWorld2D.h"
#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/ParticleSystem.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <unordered_map>

namespace Zamin {

    // The sampling data of a collision mesh (CollisionShapes3D caches the triangles; this the
    // areas / normals made from them), while the mesh lives.
    static Ref<const ParticleEmitterMesh> GetEmitterMesh(const Ref<const CollisionMesh3D>& source)
    {
        struct Entry
        {
            std::weak_ptr<const CollisionMesh3D> Source;
            Ref<const ParticleEmitterMesh> Mesh;
        };
        static std::unordered_map<const CollisionMesh3D*, Entry> cache;
        auto it = cache.find(source.get());
        if (it != cache.end() && it->second.Source.lock() == source)
            return it->second.Mesh;
        std::erase_if(cache, [](const auto& entry) { return entry.second.Source.expired(); });
        Ref<const ParticleEmitterMesh> mesh = ParticleEmitterMesh::Build(source->Positions, source->Indices);
        cache[source.get()] = { source, mesh };
        return mesh;
    }

    // The Mesh shape's mesh for what its source names now (looked up again only when that changes).
    static void ResolveMeshShape(Entity entity, ParticleSystemComponent& ps)
    {
        using Source = ParticleSystemComponent::MeshShapeSource;
        if (ps.Shape != ParticleSystemComponent::EmitterShape::Mesh)
        {
            if (ps.MeshRuntime || !ps.MeshRuntimeKey.empty())
            {
                ps.MeshRuntime = nullptr;
                ps.MeshRuntimeKey.clear();
            }
            return;
        }
        std::string modelPath;
        PrimitiveMesh primitive = PrimitiveMesh::Model;
        switch (ps.MeshSource)
        {
            case Source::Primitive:
                primitive = ps.MeshPrimitive == PrimitiveMesh::Model ? PrimitiveMesh::Cube : ps.MeshPrimitive;
                break;
            case Source::Model:
                modelPath = ps.MeshModelPath;
                break;
            case Source::Renderer:
                if (const auto* mr = entity.TryGetComponent<MeshRendererComponent>())
                {
                    primitive = mr->Type;
                    if (mr->Type == PrimitiveMesh::Model)
                        modelPath = mr->ModelPath;
                }
                break;
        }
        std::string key = std::format("{}|{}", (int)primitive, modelPath);
        if (key == ps.MeshRuntimeKey)
            return;
        ps.MeshRuntimeKey = std::move(key);
        ps.MeshRuntime = nullptr;
        if (primitive == PrimitiveMesh::Model && modelPath.empty())
            return; // nothing named (yet): from the centre
        std::string error;
        if (Ref<const CollisionMesh3D> mesh = CollisionShapes3D::GetMesh(modelPath, primitive, &error))
            ps.MeshRuntime = GetEmitterMesh(mesh);
        else
            ZM_CORE_WARN("'{}': particle mesh shakli o'qilmadi - {}", entity.GetName(), error);
    }

    // The sub-emitters of a system: its events this frame (and its living particles for Lifetime)
    // make particles in the template systems, in world space.
    static void RunSubEmitters(Scene& scene, Entity entity, ParticleSystemComponent& ps, const glm::mat4& world, float ts, bool editor)
    {
        using Trigger = ParticleSystemComponent::SubEmitter::TriggerType;
        auto position = [&](const glm::vec3& p) { return ps.WorldSpace ? p : glm::vec3(world * glm::vec4(p, 1.0f)); };
        auto direction = [&](const glm::vec3& v) { return ps.WorldSpace ? v : glm::vec3(world * glm::vec4(v, 0.0f)); };
        uint32_t& rng = ps.RandomState;
        for (const auto& sub : ps.SubEmitters.Emitters)
        {
            if (!sub.Emitter || sub.Count <= 0 || sub.Probability <= 0.0f)
                continue;
            Entity target = scene.GetEntityByUUID(sub.Emitter.ID);
            if (!target || target == entity || !scene.IsActiveInHierarchy(target))
                continue;
            auto* child = target.TryGetComponent<ParticleSystemComponent>();
            if (!child || (editor && !child->PlayInEditor))
                continue;
            ResolveMeshShape(target, *child);
            const glm::mat4 targetWorld = scene.GetWorldTransform(target);
            auto emit = [&](const glm::vec3& at, const glm::vec3& velocity, const glm::vec4& color, float size, int count) {
                ParticleSpawnOrigin origin;
                origin.Position = position(at);
                origin.Velocity = direction(velocity) * sub.InheritVelocity;
                if (sub.InheritColor)
                    origin.Tint = glm::vec4(glm::vec3(color), 1.0f);
                if (sub.InheritSize)
                    origin.SizeScale = std::max(0.0f, size);
                ParticleSimulation::Spawn(*child, targetWorld, count, &origin);
            };
            if (sub.Trigger == Trigger::Lifetime)
            {
                // Count per second from every living particle (the fraction left over: by chance).
                const float expected = sub.Count * std::min(sub.Probability, 1.0f) * ts;
                for (const Particle& p : ps.Particles)
                {
                    const int count = (int)(expected + ParticleSimulation::Random01(rng));
                    if (count > 0)
                        emit(p.Position, p.Velocity, ParticleSimulation::GetColor(ps, p), ParticleSimulation::GetSize(ps, p), count);
                }
                continue;
            }
            const ParticleEvent::Type kind = sub.Trigger == Trigger::Birth ? ParticleEvent::Type::Birth
                                           : sub.Trigger == Trigger::Collision ? ParticleEvent::Type::Collision
                                                                               : ParticleEvent::Type::Death;
            for (const ParticleEvent& event : ps.Events)
            {
                if (event.Kind != kind)
                    continue;
                if (sub.Probability < 1.0f && ParticleSimulation::Random01(rng) >= sub.Probability)
                    continue;
                emit(event.Position, event.Velocity, event.Color, event.Size, sub.Count);
            }
        }
    }

    void Scene::UpdateParticles(Timestep ts, bool editor)
    {
        ZM_PROFILE_SCOPE("Particle'lar");
        for (auto e : m_Registry.view<ParticleSystemComponent>())
        {
            auto& ps = m_Registry.get<ParticleSystemComponent>(e);
            Entity entity{ e, this };
            // Not previewed in the editor, or inactive in the hierarchy: stopped and empty, so it
            // starts over (Burst included) when it plays again. Edit mode and direct writes to
            // TagComponent::Active end up here; at runtime SetActive already cleared it.
            if ((editor && !ps.PlayInEditor) || !IsActiveInHierarchy(entity))
            {
                if (!ps.Particles.empty() || ps.BurstDone || ps.EmitAccumulator != 0.0f)
                    ps.Clear();
                continue;
            }
            ResolveMeshShape(entity, ps);
            const glm::mat4 world = GetWorldTransform(entity);

            // Collisions: the plane through an entity, the physics world while it runs.
            ParticleEnvironment environment;
            const auto& collision = ps.Collision;
            if (collision.Enabled)
            {
                if (collision.PlaneEntity)
                    if (Entity planeEntity = GetEntityByUUID(collision.PlaneEntity.ID))
                    {
                        const glm::mat4 planeWorld = GetWorldTransform(planeEntity);
                        const glm::vec3 up(planeWorld[1]);
                        const float length = glm::length(up);
                        const glm::vec3 normal = length > 1e-6f ? up / length : glm::vec3(0.0f, 1.0f, 0.0f);
                        environment.HasPlane = true;
                        environment.Plane = glm::vec4(normal, glm::dot(normal, glm::vec3(planeWorld[3])));
                    }
                using CollisionType = ParticleSystemComponent::CollisionModule::CollisionType;
                const uint16_t mask = collision.CollidesWith;
                // A ray from `from` to `to`, `radius` further: the centre then `radius` (and a hair) off the surface.
                if (collision.Type == CollisionType::World3D && m_PhysicsWorld3D)
                    environment.Cast = [this, mask](const glm::vec3& from, const glm::vec3& to, float radius, glm::vec3& center, glm::vec3& normal) {
                        const glm::vec3 d = to - from;
                        const float length = glm::length(d);
                        if (!(length > 0.0f))
                            return false;
                        const RaycastHit hit = m_PhysicsWorld3D->CastRay(from, d / length, length + radius, mask, false);
                        if (!hit)
                            return false;
                        normal = glm::length(hit.Normal) > 1e-6f ? glm::normalize(hit.Normal) : -d / length;
                        center = hit.Point + normal * (radius + 1e-3f);
                        return true;
                    };
                else if (collision.Type == CollisionType::World2D && m_PhysicsWorld)
                    environment.Cast = [this, mask](const glm::vec3& from, const glm::vec3& to, float radius, glm::vec3& center, glm::vec3& normal) {
                        const glm::vec2 d = glm::vec2(to - from);
                        const float length = glm::length(d);
                        if (!(length > 0.0f))
                            return false;
                        const RaycastHit hit = m_PhysicsWorld->CastRay(glm::vec2(from), d / length, length + radius, mask);
                        if (!hit)
                            return false;
                        const glm::vec2 n = glm::length(glm::vec2(hit.Normal)) > 1e-6f ? glm::normalize(glm::vec2(hit.Normal)) : -d / length;
                        normal = glm::vec3(n, 0.0f);
                        center = glm::vec3(glm::vec2(hit.Point) + n * (radius + 1e-3f), to.z);
                        return true;
                    };
            }
            ParticleSimulation::Update(ps, world, ts, editor, collision.Enabled ? &environment : nullptr);
            if (ps.SubEmitters.Enabled)
                RunSubEmitters(*this, entity, ps, world, ts, editor);
            ps.Events.clear();
        }
    }

    void Scene::EmitParticles(Entity entity, int count)
    {
        if (!entity.IsValid() || entity.GetScene() != this || !IsActiveInHierarchy(entity))
            return;
        if (auto* ps = entity.TryGetComponent<ParticleSystemComponent>())
        {
            ResolveMeshShape(entity, *ps);
            ParticleSimulation::Spawn(*ps, GetWorldTransform(entity), count);
        }
    }

    AABB Scene::GetParticleBounds(const ParticleSystemComponent& ps, const glm::mat4& emitter)
    {
        // Every particle quad (any rotation, billboarded or not) fits in a sphere of half its
        // diagonal around its position; a trail in one of half its widest width around its points.
        const float maxSize = ParticleSimulation::GetMaxSize(ps);
        float scale = 1.0f; // inherited sizes (sub-emitters)
        AABB box{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(-std::numeric_limits<float>::max()) };
        auto add = [&](const glm::vec3& p) {
            const glm::vec3 pos = ps.WorldSpace ? p : glm::vec3(emitter * glm::vec4(p, 1.0f));
            box.Min = glm::min(box.Min, pos);
            box.Max = glm::max(box.Max, pos);
        };
        const bool trails = ps.Trails.Enabled && ps.TrailPool.Capacity > 0;
        for (const auto& p : ps.Particles)
        {
            add(p.Position);
            scale = std::max(scale, p.SizeScale);
            if (trails && p.Trail >= 0)
                for (int i = 0, n = ps.TrailPool.GetCount(p.Trail); i < n; i++)
                    add(ps.TrailPool.Get(p.Trail, i).Position);
        }
        float radius = maxSize * scale * 0.70711f;
        if (trails)
        {
            const float width = std::abs(ps.Trails.Width) * (ps.Trails.WidthFromSize ? maxSize * scale : 1.0f) *
                                std::max(std::abs(ps.Trails.WidthCurve.GetMin()), std::abs(ps.Trails.WidthCurve.GetMax()));
            radius = std::max(radius, width * 0.5f);
        }
        box.Min -= glm::vec3(radius);
        box.Max += glm::vec3(radius);
        return box;
    }

    // The trails of a system as ribbons facing the camera: through each particle and the points
    // it left (newest first), as wide and coloured as the Trails module says at each point's age.
    static void DrawTrails(const ParticleSystemComponent& ps, const glm::mat4& transform, const glm::vec3& viewer, bool perspective,
                           const glm::vec3& viewBack, int id)
    {
        struct Vertex
        {
            glm::vec3 Position;
            float U;
        };
        static thread_local std::vector<Vertex> line;
        const auto& pool = ps.TrailPool;
        const float lifetime = std::max(1e-4f, ps.Trails.Lifetime);
        auto toWorld = [&](const glm::vec3& p) { return ps.WorldSpace ? p : glm::vec3(transform * glm::vec4(p, 1.0f)); };
        static const glm::vec2 uvs[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
        for (const auto& p : ps.Particles)
        {
            if (p.Trail < 0 || pool.GetCount(p.Trail) == 0)
                continue;
            line.clear();
            line.push_back({ toWorld(p.Position), 0.0f });
            for (int i = 0, n = pool.GetCount(p.Trail); i < n; i++)
            {
                const auto& point = pool.Get(p.Trail, i);
                const glm::vec3 position = toWorld(point.Position);
                const glm::vec3 d = position - line.back().Position;
                if (glm::dot(d, d) < 1e-10f)
                    continue; // where the one before is (the particle at rest)
                line.push_back({ position, std::clamp((ps.Time - point.Time) / lifetime, 0.0f, 1.0f) });
            }
            if (line.size() < 2)
                continue;
            const float size = ParticleSimulation::GetSize(ps, p);
            const glm::vec4 color = ParticleSimulation::GetColor(ps, p);
            glm::vec3 previousSide(0.0f);
            glm::vec3 positions[4];
            glm::vec4 colors[4];
            glm::vec3 lastLeft(0.0f), lastRight(0.0f);
            glm::vec4 lastColor(0.0f);
            for (size_t i = 0; i < line.size(); i++)
            {
                const glm::vec3& v = line[i].Position;
                const glm::vec3 tangent = line[i == 0 ? 0 : i - 1].Position - line[std::min(i + 1, line.size() - 1)].Position;
                const glm::vec3 toCamera = perspective ? viewer - v : viewBack;
                glm::vec3 side = glm::cross(tangent, toCamera);
                const float length = glm::length(side);
                side = length > 1e-9f ? side / length : previousSide;
                previousSide = side;
                const float half = ParticleSimulation::GetTrailWidth(ps, size, line[i].U) * 0.5f;
                const glm::vec3 left = v + side * half, right = v - side * half;
                const glm::vec4 c = ParticleSimulation::GetTrailColor(ps, color, line[i].U);
                if (i > 0 && (c.a > 0.0f || lastColor.a > 0.0f))
                {
                    positions[0] = lastLeft;
                    positions[1] = lastRight;
                    positions[2] = right;
                    positions[3] = left;
                    colors[0] = colors[1] = lastColor;
                    colors[2] = colors[3] = c;
                    Renderer2D::DrawQuadVertices(positions, colors, uvs, nullptr, id);
                }
                lastLeft = left;
                lastRight = right;
                lastColor = c;
            }
        }
    }

    // The flipbook frames of a system's texture: their UV rectangles and how big each is drawn in
    // its square (a sprite frame keeps its aspect inside it). Empty: the whole texture.
    struct ParticleFrames
    {
        struct Frame
        {
            glm::vec2 UVMin{ 0.0f }, UVMax{ 1.0f };
            glm::vec2 Fit{ 1.0f };
        };
        std::vector<Frame> Frames;
    };

    static void GatherFrames(const ParticleSystemComponent& ps, ParticleFrames& out, const AssetRegistry* registry)
    {
        out.Frames.clear();
        const auto& sheet = ps.TextureSheet;
        if (!sheet.Enabled || !ps.Texture)
            return;
        const uint32_t width = ps.Texture->GetWidth(), height = ps.Texture->GetHeight();
        if (sheet.Source == ParticleSystemComponent::TextureSheetModule::FrameSource::Grid)
        {
            const int count = ParticleSimulation::GetGridFrameCount(ps);
            out.Frames.reserve((size_t)count);
            for (int i = 0; i < count; i++)
            {
                SpriteRegion cell = SpriteRegion::GridCell(std::max(1, sheet.Columns), std::max(1, sheet.Rows), i, width, height);
                out.Frames.push_back({ cell.UVMin, cell.UVMax, glm::vec2(1.0f) });
            }
            return;
        }
        const SpriteSheet* sprites = ps.TexturePath.empty() ? nullptr : SpriteSheet::Get(registry, ps.TexturePath);
        if (!sprites)
            return;
        auto add = [&](const SpriteFrame& frame) {
            ParticleFrames::Frame f;
            frame.GetUV(width, height, f.UVMin, f.UVMax);
            const glm::vec2 size = glm::max(glm::vec2(frame.Size), glm::vec2(1.0f));
            f.Fit = size / std::max(size.x, size.y);
            out.Frames.push_back(f);
        };
        if (sheet.SpritePrefix.empty())
        {
            for (const SpriteFrame& frame : sprites->Frames)
                add(frame);
        }
        else
        {
            for (const std::string& name : sprites->FindSequence(sheet.SpritePrefix))
                if (const SpriteFrame* frame = sprites->Find(name))
                    add(*frame);
        }
    }

    void Scene::DrawParticleSystem(ParticleSystemComponent& ps, const glm::mat4& transform, const glm::mat4& billboard, int id,
                                   const AssetRegistry* registry, const glm::vec3& viewer, bool perspective)
    {
        // What came before is drawn with depth writes; the particles without them.
        Renderer2D::Flush();
        if (ps.Additive) Renderer2D::SetBlendMode(BlendMode::Additive);
        RenderCommand::SetDepthWrite(false);
        // Trails under the particles.
        if (ps.Trails.Enabled && ps.TrailPool.Capacity > 0)
            DrawTrails(ps, transform, viewer, perspective, glm::vec3(billboard[2]), id);
        if (!ps.TexturePath.empty() && !ps.Texture)
            ps.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(ps.TexturePath));
        static thread_local ParticleFrames frames;
        GatherFrames(ps, frames, registry);
        const int frameCount = (int)frames.Frames.size();
        for (const auto& p : ps.Particles)
        {
            glm::vec4 color = ParticleSimulation::GetColor(ps, p);
            float size = ParticleSimulation::GetSize(ps, p);
            glm::vec3 pos = ps.WorldSpace ? p.Position : glm::vec3(transform * glm::vec4(p.Position, 1.0f));
            glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
            if (ps.Billboard) m = m * billboard;
            m = m * glm::rotate(glm::mat4(1.0f), p.Rotation, glm::vec3(0, 0, 1)) * glm::scale(glm::mat4(1.0f), glm::vec3(size));
            if (ps.Texture && frameCount > 0)
            {
                const ParticleFrames::Frame& f = frames.Frames[(size_t)ParticleSimulation::GetSheetFrame(ps, p, frameCount)];
                if (f.Fit != glm::vec2(1.0f))
                    m = m * glm::scale(glm::mat4(1.0f), glm::vec3(f.Fit, 1.0f));
                Renderer2D::DrawQuad(m, ps.Texture, f.UVMin, f.UVMax, color, id);
            }
            else if (ps.Texture)
                Renderer2D::DrawQuad(m, ps.Texture, 1.0f, color, id);
            else if (ps.RenderAsCircle)
                Renderer2D::DrawCircle(m, color, 1.0f, 0.6f, id);
            else
                Renderer2D::DrawQuad(m, color, id);
        }
        Renderer2D::Flush();
        RenderCommand::SetDepthWrite(true);
        if (ps.Additive) Renderer2D::SetBlendMode(BlendMode::Alpha);
    }

}
