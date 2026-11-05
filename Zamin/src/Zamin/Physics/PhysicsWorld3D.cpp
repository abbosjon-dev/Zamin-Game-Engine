#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Physics/CollisionShapes3D.h"
#include "Zamin/Core/Log.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {

    namespace {

        // Jolt's object layer of a body: its collision layer (0..15, CollisionLayers2D) times two, plus
        // one for a moving (dynamic / kinematic) body. Static bodies never collide with each other.
        namespace Layers
        {
            static constexpr int Count = 16;
            static constexpr JPH::ObjectLayer NON_MOVING = 0; // layer 0, static
            static constexpr JPH::ObjectLayer MOVING = 1;     // layer 0, moving
            static constexpr JPH::ObjectLayer NUM_LAYERS = Count * 2;

            inline JPH::ObjectLayer Make(int layer, bool moving)
            {
                return (JPH::ObjectLayer)((std::clamp(layer, 0, Count - 1) << 1) | (moving ? 1 : 0));
            }
            inline bool IsMoving(JPH::ObjectLayer layer) { return (layer & 1) != 0; }
            inline int Index(JPH::ObjectLayer layer) { return (int)((layer >> 1) & (Count - 1)); }
        }

        namespace BroadPhaseLayers
        {
            static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
            static constexpr JPH::BroadPhaseLayer MOVING(1);
            static constexpr JPH::uint NUM_LAYERS(2);
        }

        // Which object layers collide: never two static ones, else what the layer matrix says.
        // Changed only between steps; Jolt's jobs only read it.
        class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
        {
        public:
            std::array<uint16_t, Layers::Count> Masks;

            ObjectLayerPairFilterImpl() { Masks.fill(0xFFFF); }

            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                if (!Layers::IsMoving(a) && !Layers::IsMoving(b))
                    return false;
                return ((Masks[(size_t)Layers::Index(a)] >> Layers::Index(b)) & 1u) != 0;
            }
        };

        class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
        {
        public:
            JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return Layers::IsMoving(layer) ? BroadPhaseLayers::MOVING : BroadPhaseLayers::NON_MOVING;
            }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
            }
#endif
        };

        class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
            {
                if (!Layers::IsMoving(layer)) return bp == BroadPhaseLayers::MOVING;
                return true;
            }
        };

        // ---- query filters: the layers in a mask; sensor bodies only when asked for.
        class QueryLayerFilter final : public JPH::ObjectLayerFilter
        {
        public:
            explicit QueryLayerFilter(uint16_t mask) : m_Mask(mask) {}
            bool ShouldCollide(JPH::ObjectLayer layer) const override { return ((m_Mask >> Layers::Index(layer)) & 1u) != 0; }
        private:
            uint16_t m_Mask;
        };

        class QueryBodyFilter final : public JPH::BodyFilter
        {
        public:
            explicit QueryBodyFilter(bool sensors) : m_Sensors(sensors) {}
            bool ShouldCollideLocked(const JPH::Body& body) const override { return m_Sensors || !body.IsSensor(); }
        private:
            bool m_Sensors;
        };

        std::mutex s_InitMutex;
        int s_JoltRefCount = 0;

        void JoltAcquire()
        {
            std::lock_guard lock(s_InitMutex);
            if (s_JoltRefCount++ == 0)
            {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
        }

        void JoltRelease()
        {
            std::lock_guard lock(s_InitMutex);
            if (--s_JoltRefCount == 0)
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        }

        // Pairs of bodies (their collision group IDs are their body IDs) that a joint keeps from
        // colliding (CollideConnected off), counted: several joints may link the same two bodies.
        // Changed only between steps; Jolt's jobs only read it.
        class JointCollisionFilter final : public JPH::GroupFilter
        {
        public:
            std::unordered_map<uint64_t, int> Pairs;

            static uint64_t Key(uint32_t a, uint32_t b)
            {
                if (a > b) std::swap(a, b);
                return ((uint64_t)a << 32) | b;
            }

            bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override
            {
                return Pairs.empty() || !Pairs.contains(Key(a.GetGroupID(), b.GetGroupID()));
            }
        };

        inline JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
        inline JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
        inline glm::vec3 FromJolt(const JPH::Vec3& v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
        inline glm::quat FromJolt(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

        // The Jolt mesh shape of each collision mesh (CollisionShapes3D::GetMesh) in use: building its
        // tree is the slow part of a mesh collider, so every world shares it, and it outlives the
        // worlds while the mesh lives (Play / Stop in the editor does not build it again). A mesh
        // that was read again (its model file changed) is a new one; the old entry goes.
        struct MeshShapeCache
        {
            struct Entry
            {
                std::weak_ptr<const CollisionMesh3D> Mesh;
                JPH::RefConst<JPH::Shape> Shape;
            };
            std::mutex Mutex;
            std::unordered_map<const CollisionMesh3D*, Entry> Entries;

            void Prune()
            {
                std::erase_if(Entries, [](const auto& item) { return item.second.Mesh.expired(); });
            }
        };

        MeshShapeCache& GetMeshShapeCache()
        {
            static MeshShapeCache s_Cache;
            return s_Cache;
        }

        // Null (with the reason logged) when the mesh has no usable triangle.
        JPH::RefConst<JPH::Shape> GetMeshShape(const Ref<const CollisionMesh3D>& mesh)
        {
            MeshShapeCache& cache = GetMeshShapeCache();
            std::lock_guard lock(cache.Mutex);
            if (auto it = cache.Entries.find(mesh.get()); it != cache.Entries.end() && it->second.Mesh.lock() == mesh)
                return it->second.Shape;
            cache.Prune();
            JPH::VertexList vertices;
            vertices.reserve(mesh->Positions.size());
            for (const glm::vec3& p : mesh->Positions)
                vertices.push_back(JPH::Float3(p.x, p.y, p.z));
            JPH::IndexedTriangleList triangles;
            triangles.reserve(mesh->GetTriangleCount());
            const uint32_t count = (uint32_t)vertices.size();
            for (size_t i = 0; i + 2 < mesh->Indices.size(); i += 3)
            {
                const uint32_t a = mesh->Indices[i], b = mesh->Indices[i + 1], c = mesh->Indices[i + 2];
                if (a < count && b < count && c < count)
                    triangles.push_back(JPH::IndexedTriangle(a, b, c, 0));
            }
            // Removes the degenerate / duplicate triangles.
            JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
            JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                // Remembered as well: the bodies made from it later do not log it again.
                ZM_CORE_ERROR("Mesh collider qurilmadi: {}", result.GetError().c_str());
                cache.Entries[mesh.get()] = { mesh, nullptr };
                return nullptr;
            }
            cache.Entries[mesh.get()] = { mesh, result.Get() };
            return result.Get();
        }
    }

    // ------------------------------------------------------------------ contacts
    class ContactListener3D : public JPH::ContactListener
    {
    public:
        std::mutex Mutex;
        std::unordered_map<uint32_t, uint64_t> BodyToEntity; // guarded by Mutex
        std::vector<ContactEvent> Events;
        // count active sub-shape contacts per body pair, so begin/end fire once per pair
        struct PairState
        {
            int Count = 0;
            bool Sensor = false; // OnContactRemoved no longer has the bodies: remember it here
        };
        std::unordered_map<uint64_t, PairState> PairCounts;

        static uint64_t PairKey(uint32_t a, uint32_t b)
        {
            if (a > b) std::swap(a, b);
            return ((uint64_t)a << 32) | b;
        }

        // ---- debug contact points (CollectDebug), guarded by Mutex
        std::atomic<bool> CollectDebug{ false };
        struct DebugManifold
        {
            JPH::BodyID Body1, Body2;
            glm::vec3 Normal{ 0.0f };
            std::vector<glm::vec3> Points;
            uint64_t Step = 0; // the step that last reported it
        };
        // (body 1 << 32 | body 2, sub shape 1 << 32 | sub shape 2) -> its points
        std::map<std::pair<uint64_t, uint64_t>, DebugManifold> DebugContacts;
        uint64_t DebugStep = 0;

        void RecordDebugContact(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m)
        {
            if (b1.IsSensor() || b2.IsSensor())
                return;
            std::pair<uint64_t, uint64_t> key{ ((uint64_t)b1.GetID().GetIndexAndSequenceNumber() << 32) | b2.GetID().GetIndexAndSequenceNumber(),
                                               ((uint64_t)m.mSubShapeID1.GetValue() << 32) | m.mSubShapeID2.GetValue() };
            DebugManifold& d = DebugContacts[key];
            d.Body1 = b1.GetID();
            d.Body2 = b2.GetID();
            // Jolt's normal moves body 2 out of body 1: turned so that it leaves a static /
            // kinematic surface (the surface normal under a resting crate).
            glm::vec3 n = FromJolt(m.mWorldSpaceNormal);
            d.Normal = !b2.IsDynamic() && b1.IsDynamic() ? -n : n;
            d.Points.clear();
            for (JPH::uint i = 0; i < m.mRelativeContactPointsOn1.size(); i++)
            {
                JPH::RVec3 p1 = m.GetWorldSpaceContactPointOn1(i), p2 = m.GetWorldSpaceContactPointOn2(i);
                d.Points.push_back({ (float)(p1.GetX() + p2.GetX()) * 0.5f, (float)(p1.GetY() + p2.GetY()) * 0.5f,
                                     (float)(p1.GetZ() + p2.GetZ()) * 0.5f });
            }
            d.Step = DebugStep;
        }

        void OnContactPersisted(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings&) override
        {
            if (!CollectDebug.load(std::memory_order_relaxed))
                return;
            std::lock_guard lock(Mutex);
            RecordDebugContact(b1, b2, m);
        }

        void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
        {
            std::lock_guard lock(Mutex);
            if (CollectDebug.load(std::memory_order_relaxed))
                RecordDebugContact(b1, b2, manifold);
            uint32_t a = b1.GetID().GetIndexAndSequenceNumber(), b = b2.GetID().GetIndexAndSequenceNumber();
            auto& pair = PairCounts[PairKey(a, b)];
            if (pair.Count++ == 0)
            {
                pair.Sensor = b1.IsSensor() || b2.IsSensor();
                Push(a, b, true, pair.Sensor);
            }
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
        {
            std::lock_guard lock(Mutex);
            uint32_t a = pair.GetBody1ID().GetIndexAndSequenceNumber(), b = pair.GetBody2ID().GetIndexAndSequenceNumber();
            auto it = PairCounts.find(PairKey(a, b));
            if (it == PairCounts.end()) return;
            if (--it->second.Count <= 0)
            {
                bool sensor = it->second.Sensor;
                PairCounts.erase(it);
                Push(a, b, false, sensor);
            }
        }

    private:
        void Push(uint32_t a, uint32_t b, bool begin, bool sensor)
        {
            auto ea = BodyToEntity.find(a), eb = BodyToEntity.find(b);
            if (ea != BodyToEntity.end() && eb != BodyToEntity.end())
                Events.push_back({ ea->second, eb->second, begin, sensor });
        }
    };

    // ------------------------------------------------------------------ impl
    struct PhysicsWorld3D::Impl
    {
        std::unique_ptr<JPH::TempAllocatorImpl> TempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> JobSystem;
        BPLayerInterfaceImpl BroadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl ObjectVsBroadPhaseFilter;
        ObjectLayerPairFilterImpl ObjectPairFilter;
        std::unique_ptr<JPH::PhysicsSystem> System;
        ContactListener3D Contacts;
        std::vector<JPH::BodyID> Bodies;

        struct CharacterSlot
        {
            JPH::Ref<JPH::CharacterVirtual> Character;
            uint64_t EntityID = 0;
            std::vector<std::pair<uint64_t, bool>> Touching; // (entity id, sensor) from the previous update
            JPH::ObjectLayer Layer = Layers::MOVING;         // its inner body's, what it collides with
        };
        std::vector<CharacterSlot> Characters;

        // ---- joints
        struct JointSlot
        {
            JPH::Ref<JPH::TwoBodyConstraint> Constraint;
            Joint3DType Type = Joint3DType::Fixed;
            uint64_t EntityID = 0;
            JPH::BodyID Body1, Body2;                   // Body1 invalid: the world
            glm::vec3 LocalPoint1{ 0.0f }, LocalPoint2{ 0.0f }; // in the bodies' frames (no Body1: a world point)
            glm::vec3 LocalAxis2{ 0.0f, 1.0f, 0.0f };  // the hinge / slider axis in Body2's frame
            bool NoCollision = false;                  // its pair is in JointFilter
            PhysicsJoint3DDesc Desc;                   // the settings it has now
        };
        std::map<uint32_t, JointSlot> Joints;
        uint32_t NextJoint = 1;
        JPH::Ref<JointCollisionFilter> JointFilter; // made once Jolt's allocator is there

        // The Jolt shape of each convex hull used by a body of this world (the hull is kept alive with it).
        struct HullShape
        {
            Ref<const ConvexHull3D> Hull;
            JPH::RefConst<JPH::Shape> Shape;
        };
        std::unordered_map<const ConvexHull3D*, HullShape> HullShapes;

        bool HasBody(JPH::BodyID id) const { return std::find(Bodies.begin(), Bodies.end(), id) != Bodies.end(); }

        void SetPairCollision(const JointSlot& joint, bool add)
        {
            const uint64_t key = JointCollisionFilter::Key(joint.Body1.GetIndexAndSequenceNumber(), joint.Body2.GetIndexAndSequenceNumber());
            auto& pairs = JointFilter->Pairs;
            if (add)
                pairs[key]++;
            else if (auto it = pairs.find(key); it != pairs.end() && --it->second <= 0)
                pairs.erase(it);
        }

        void WakeJointBodies(const JointSlot& joint)
        {
            for (JPH::BodyID id : { joint.Body1, joint.Body2 })
                if (!id.IsInvalid() && BI().GetMotionType(id) != JPH::EMotionType::Static)
                    BI().ActivateBody(id);
        }

        void RemoveJoint(std::map<uint32_t, JointSlot>::iterator it)
        {
            JointSlot& joint = it->second;
            System->RemoveConstraint(joint.Constraint);
            if (joint.NoCollision)
                SetPairCollision(joint, false);
            WakeJointBodies(joint); // a body that hung from it falls
            Joints.erase(it);
        }

        // A body's frame (its origin, not its centre of mass) in the world.
        void GetFrame(JPH::BodyID id, glm::vec3& position, glm::quat& rotation) const
        {
            if (id.IsInvalid())
            {
                position = glm::vec3(0.0f);
                rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                return;
            }
            JPH::RVec3 p;
            JPH::Quat q;
            BI().GetPositionAndRotation(id, p, q);
            position = { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
            rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        }

        // The entity's body (or character) left the world: no character touches it any more.
        // Scene::EndContactsOf has told the scripts already. Without this, a body recreated
        // before the next update (switched off and on in one frame) would still be in the list,
        // and the characters would never report a new Begin for it (the inner body is
        // kinematic: a static ground gives it no Jolt contact to make up for that either).
        void ForgetTouching(uint64_t entityID)
        {
            if (!entityID) return;
            for (CharacterSlot& slot : Characters)
                std::erase_if(slot.Touching, [&](const std::pair<uint64_t, bool>& t) { return t.first == entityID; });
        }

        JPH::BodyInterface& BI() { return System->GetBodyInterface(); }
        const JPH::BodyInterface& BI() const { return System->GetBodyInterface(); }
    };

    static JPH::BodyID ToBodyID(uint32_t id) { return JPH::BodyID(id); }

    PhysicsWorld3D::PhysicsWorld3D(const glm::vec3& gravity) : m_Impl(std::make_unique<Impl>())
    {
        JoltAcquire();
        m_Impl->JointFilter = new JointCollisionFilter();
        m_Impl->TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
        int threads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
        m_Impl->JobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::min(threads, 8));

        const JPH::uint maxBodies = 16384, numBodyMutexes = 0, maxBodyPairs = 16384, maxContactConstraints = 8192;
        m_Impl->System = std::make_unique<JPH::PhysicsSystem>();
        m_Impl->System->Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints,
                             m_Impl->BroadPhaseLayerInterface, m_Impl->ObjectVsBroadPhaseFilter, m_Impl->ObjectPairFilter);
        m_Impl->System->SetGravity(ToJolt(gravity));
        m_Impl->System->SetContactListener(&m_Impl->Contacts);
    }

    PhysicsWorld3D::~PhysicsWorld3D()
    {
        m_Impl->Characters.clear();
        for (auto& [id, joint] : m_Impl->Joints)
            m_Impl->System->RemoveConstraint(joint.Constraint);
        m_Impl->Joints.clear();
        auto& bi = m_Impl->BI();
        for (const JPH::BodyID& id : m_Impl->Bodies)
        {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
        }
        m_Impl->Bodies.clear();
        m_Impl->HullShapes.clear();
        m_Impl->System.reset();
        m_Impl->JobSystem.reset();
        m_Impl->TempAllocator.reset();
        m_Impl.reset();
        JoltRelease();
    }

    uint32_t PhysicsWorld3D::CreateBody(const PhysicsBody3DDesc& desc)
    {
        if (desc.Shapes.empty())
            return 0xffffffff;

        const JPH::EMotionType motion = desc.MotionType == 0 ? JPH::EMotionType::Static :
                                        desc.MotionType == 2 ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
        auto makeShape = [this, motion](const PhysicsBody3DDesc::Shape& s) -> JPH::RefConst<JPH::Shape> {
            JPH::RefConst<JPH::Shape> shape;
            switch (s.Type)
            {
                case PhysicsBody3DDesc::Shape::Kind::Mesh:
                {
                    // Jolt has no mass / inertia for an open mesh and cannot collide two meshes: static
                    // and kinematic bodies only.
                    if (!s.Mesh || motion == JPH::EMotionType::Dynamic)
                        return nullptr;
                    shape = GetMeshShape(s.Mesh);
                    if (!shape)
                        return nullptr;
                    glm::vec3 scale = glm::max(glm::abs(s.Scale), glm::vec3(0.001f));
                    if (glm::any(glm::greaterThan(glm::abs(scale - glm::vec3(1.0f)), glm::vec3(1e-5f))))
                        shape = new JPH::ScaledShape(shape, ToJolt(scale));
                    break;
                }
                case PhysicsBody3DDesc::Shape::Kind::ConvexHull:
                {
                    if (!s.Hull || s.Hull->Points.size() < 4)
                        return nullptr;
                    auto it = m_Impl->HullShapes.find(s.Hull.get());
                    if (it == m_Impl->HullShapes.end())
                    {
                        JPH::Array<JPH::Vec3> points;
                        points.reserve(s.Hull->Points.size());
                        for (const glm::vec3& p : s.Hull->Points)
                            points.push_back(ToJolt(p));
                        JPH::ConvexHullShapeSettings settings(points, JPH::cDefaultConvexRadius);
                        JPH::ShapeSettings::ShapeResult result = settings.Create();
                        if (result.HasError())
                        {
                            ZM_CORE_ERROR("Jolt konveks qobiq xatosi: {}", result.GetError().c_str());
                            return nullptr;
                        }
                        it = m_Impl->HullShapes.emplace(s.Hull.get(), Impl::HullShape{ s.Hull, result.Get() }).first;
                    }
                    shape = it->second.Shape;
                    glm::vec3 scale = glm::max(glm::abs(s.Scale), glm::vec3(0.001f));
                    if (glm::any(glm::greaterThan(glm::abs(scale - glm::vec3(1.0f)), glm::vec3(1e-5f))))
                        shape = new JPH::ScaledShape(shape, ToJolt(scale));
                    break;
                }
                case PhysicsBody3DDesc::Shape::Kind::Box:
                {
                    glm::vec3 h = glm::max(s.HalfExtents, glm::vec3(0.01f));
                    float convex = std::min({ h.x, h.y, h.z, JPH::cDefaultConvexRadius });
                    shape = new JPH::BoxShape(ToJolt(h), convex);
                    break;
                }
                case PhysicsBody3DDesc::Shape::Kind::Sphere:
                    shape = new JPH::SphereShape(std::max(0.01f, s.Radius));
                    break;
                case PhysicsBody3DDesc::Shape::Kind::Capsule:
                    shape = new JPH::CapsuleShape(std::max(0.01f, s.HalfHeight), std::max(0.01f, s.Radius));
                    break;
            }
            if (glm::length(s.Offset) > 1e-5f)
                shape = new JPH::RotatedTranslatedShape(ToJolt(s.Offset), JPH::Quat::sIdentity(), shape);
            return shape;
        };

        // Shapes that cannot be made (a hull without volume, a mesh on a dynamic body) are left out.
        std::vector<JPH::RefConst<JPH::Shape>> parts;
        std::vector<const PhysicsBody3DDesc::Shape*> made;
        bool hasMesh = false;
        for (const auto& s : desc.Shapes)
            if (JPH::RefConst<JPH::Shape> part = makeShape(s))
            {
                parts.push_back(part);
                made.push_back(&s);
                hasMesh |= s.Type == PhysicsBody3DDesc::Shape::Kind::Mesh;
            }
        if (parts.empty())
            return 0xffffffff;
        JPH::RefConst<JPH::Shape> shape;
        if (parts.size() == 1)
            shape = parts[0];
        else
        {
            JPH::StaticCompoundShapeSettings compound;
            for (const auto& part : parts)
                compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), part);
            auto result = compound.Create();
            if (result.HasError())
            {
                ZM_CORE_ERROR("Jolt compound shape xatosi: {}", result.GetError().c_str());
                return 0xffffffff;
            }
            shape = result.Get();
        }

        const JPH::ObjectLayer layer = Layers::Make(desc.Layer, motion != JPH::EMotionType::Static);

        JPH::BodyCreationSettings settings(shape, JPH::RVec3(desc.Position.x, desc.Position.y, desc.Position.z),
                                           ToJolt(glm::normalize(desc.Rotation)), motion, layer);
        settings.mFriction = made[0]->Friction;
        settings.mRestitution = made[0]->Restitution;
        settings.mIsSensor = std::all_of(made.begin(), made.end(), [](auto* s) { return s->IsSensor; });
        settings.mLinearDamping = desc.LinearDamping;
        settings.mAngularDamping = desc.AngularDamping;
        settings.mGravityFactor = desc.GravityFactor;
        settings.mUserData = (uint64_t)desc.EntityID;
        if (settings.mIsSensor && motion != JPH::EMotionType::Dynamic)
            settings.mCollideKinematicVsNonDynamic = true;
        if (motion == JPH::EMotionType::Kinematic && hasMesh)
        {
            // A mesh has no volume to weigh: a kinematic body gets a box of its bounds (it is never
            // pushed, but Jolt wants a mass for every moving body).
            const glm::vec3 size = glm::max(FromJolt(shape->GetLocalBounds().GetSize()), glm::vec3(0.01f));
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(ToJolt(size), std::max(0.001f, desc.Mass) / (size.x * size.y * size.z));
        }
        if (motion == JPH::EMotionType::Dynamic)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = std::max(0.001f, desc.Mass);
            if (desc.LockRotation)
                settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        }

        auto& bi = m_Impl->BI();
        JPH::Body* body = bi.CreateBody(settings);
        if (!body)
        {
            ZM_CORE_ERROR("3D fizik jism yaratilmadi (limit?)");
            return 0xffffffff;
        }
        JPH::BodyID id = body->GetID();
        // Its group is its own ID: joints with CollideConnected off filter the pair (JointFilter).
        body->SetCollisionGroup(JPH::CollisionGroup(m_Impl->JointFilter, id.GetIndexAndSequenceNumber(), 0));
        bi.AddBody(id, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        m_Impl->Bodies.push_back(id);
        {
            std::lock_guard lock(m_Impl->Contacts.Mutex);
            m_Impl->Contacts.BodyToEntity[id.GetIndexAndSequenceNumber()] = (uint64_t)desc.EntityID;
        }
        return id.GetIndexAndSequenceNumber();
    }

    // World-space bounds of a body in the world, grown a little so that bodies resting on it
    // (touching, not overlapping) are inside.
    static JPH::AABox WakeBounds(const JPH::BodyInterface& bi, JPH::BodyID id)
    {
        JPH::AABox box = bi.GetTransformedShape(id).GetWorldSpaceBounds();
        box.ExpandBy(JPH::Vec3::sReplicate(0.05f));
        return box;
    }

    void PhysicsWorld3D::DestroyBody(uint32_t bodyID, std::vector<PhysicsJoint3DRef>* destroyedJoints)
    {
        if (bodyID == 0xffffffff) return;
        JPH::BodyID id = ToBodyID(bodyID);
        auto& bi = m_Impl->BI();
        auto it = std::find(m_Impl->Bodies.begin(), m_Impl->Bodies.end(), id);
        if (it == m_Impl->Bodies.end()) return;
        // Its joints first: a Jolt constraint keeps pointers to its bodies.
        for (auto j = m_Impl->Joints.begin(); j != m_Impl->Joints.end();)
        {
            auto next = std::next(j);
            if (j->second.Body1 == id || j->second.Body2 == id)
            {
                if (destroyedJoints)
                    destroyedJoints->push_back({ UUID(j->second.EntityID), j->second.Type });
                m_Impl->RemoveJoint(j);
            }
            j = next;
        }
        // Jolt does not wake the bodies that were resting on this one: a sleeping crate would stay
        // in the air where the floor was.
        JPH::AABox around = WakeBounds(bi, id);
        bi.RemoveBody(id);
        bi.DestroyBody(id);
        m_Impl->Bodies.erase(it);
        bi.ActivateBodiesInAABox(around, {}, {});
        std::lock_guard lock(m_Impl->Contacts.Mutex);
        if (auto owner = m_Impl->Contacts.BodyToEntity.find(bodyID); owner != m_Impl->Contacts.BodyToEntity.end())
        {
            m_Impl->ForgetTouching(owner->second);
            m_Impl->Contacts.BodyToEntity.erase(owner);
        }
    }

    void PhysicsWorld3D::WakeBodiesAround(uint32_t bodyID)
    {
        if (bodyID == 0xffffffff) return;
        JPH::BodyID id = ToBodyID(bodyID);
        if (std::find(m_Impl->Bodies.begin(), m_Impl->Bodies.end(), id) == m_Impl->Bodies.end()) return;
        auto& bi = m_Impl->BI();
        bi.ActivateBodiesInAABox(WakeBounds(bi, id), {}, {});
    }

    void PhysicsWorld3D::OptimizeBroadPhase() { m_Impl->System->OptimizeBroadPhase(); }

    void PhysicsWorld3D::Step(float dt)
    {
        ContactListener3D& contacts = m_Impl->Contacts;
        const bool debug = contacts.CollectDebug.load();
        if (debug)
        {
            std::lock_guard lock(contacts.Mutex);
            contacts.DebugStep++;
        }
        m_Impl->System->Update(dt, 1, m_Impl->TempAllocator.get(), m_Impl->JobSystem.get());
        // Jolt puts a body to sleep when it hardly moved for half a second - also at the turning point
        // of a slow swing: a door on a hinge spring would stop short of its angle. Such a body is woken
        // while the spring has not got it there (at an angle where nothing holds it against the spring).
        for (auto& [handle, joint] : m_Impl->Joints)
        {
            const PhysicsJoint3DDesc& d = joint.Desc;
            if (joint.Type != Joint3DType::Hinge || d.EnableMotor || !d.EnableSpring || d.Frequency <= 0.0f)
                continue;
            if (m_Impl->BI().IsActive(joint.Body2) || m_Impl->BI().GetMotionType(joint.Body2) != JPH::EMotionType::Dynamic)
                continue;
            const auto& hinge = static_cast<const JPH::HingeConstraint&>(*joint.Constraint);
            float off = hinge.GetCurrentAngle() - hinge.GetTargetAngle();
            off = std::remainder(off, 2.0f * JPH::JPH_PI);
            if (std::abs(off) > glm::radians(1.0f) && std::abs(hinge.GetTotalLambdaRotationLimits()) < 1e-6f)
                m_Impl->BI().ActivateBody(joint.Body2);
        }
        if (!debug)
            return;
        // A contact this step did not report is gone - unless both bodies sleep (Jolt reports
        // nothing for them, the crate still lies on the floor).
        auto& bi = m_Impl->BI();
        std::lock_guard lock(contacts.Mutex);
        for (auto it = contacts.DebugContacts.begin(); it != contacts.DebugContacts.end();)
        {
            const auto& d = it->second;
            bool keep = d.Step == contacts.DebugStep ||
                        (bi.IsAdded(d.Body1) && bi.IsAdded(d.Body2) && !bi.IsActive(d.Body1) && !bi.IsActive(d.Body2));
            it = keep ? std::next(it) : contacts.DebugContacts.erase(it);
        }
    }

    void PhysicsWorld3D::SetDebugContactsEnabled(bool enabled)
    {
        ContactListener3D& contacts = m_Impl->Contacts;
        if (contacts.CollectDebug.exchange(enabled) == enabled)
            return;
        {
            std::lock_guard lock(contacts.Mutex);
            contacts.DebugContacts.clear();
        }
        if (enabled)
        {
            // Sleeping bodies report no contacts: woken once, they show theirs at once (and keep
            // them when they fall asleep again).
            m_Impl->BI().ActivateBodies(m_Impl->Bodies.data(), (int)m_Impl->Bodies.size());
        }
    }

    bool PhysicsWorld3D::IsDebugContactsEnabled() const { return m_Impl->Contacts.CollectDebug.load(); }

    void PhysicsWorld3D::GetDebugContacts(std::vector<PhysicsContactPoint>& out) const
    {
        {
            std::lock_guard lock(m_Impl->Contacts.Mutex);
            for (const auto& [key, d] : m_Impl->Contacts.DebugContacts)
                for (const glm::vec3& p : d.Points)
                    out.push_back({ p, d.Normal });
        }
        for (const auto& slot : m_Impl->Characters)
        {
            if (!slot.Character)
                continue;
            for (const auto& contact : slot.Character->GetActiveContacts())
                if (contact.mHadCollision && !contact.mIsSensorB)
                    out.push_back({ { (float)contact.mPosition.GetX(), (float)contact.mPosition.GetY(), (float)contact.mPosition.GetZ() },
                                    FromJolt(contact.mContactNormal) });
        }
    }

    void PhysicsWorld3D::GetTransform(uint32_t bodyID, glm::vec3& position, glm::quat& rotation) const
    {
        JPH::RVec3 p;
        JPH::Quat q;
        m_Impl->BI().GetPositionAndRotation(ToBodyID(bodyID), p, q);
        position = { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
        rotation = FromJolt(q);
    }

    void PhysicsWorld3D::SetTransform(uint32_t bodyID, const glm::vec3& position, const glm::quat& rotation)
    {
        m_Impl->BI().SetPositionAndRotation(ToBodyID(bodyID), JPH::RVec3(position.x, position.y, position.z),
                                            ToJolt(glm::normalize(rotation)), JPH::EActivation::Activate);
    }

    void PhysicsWorld3D::MoveKinematic(uint32_t bodyID, const glm::vec3& position, const glm::quat& rotation, float dt)
    {
        m_Impl->BI().MoveKinematic(ToBodyID(bodyID), JPH::RVec3(position.x, position.y, position.z), ToJolt(glm::normalize(rotation)), dt);
    }

    glm::vec3 PhysicsWorld3D::GetLinearVelocity(uint32_t bodyID) const { return FromJolt(m_Impl->BI().GetLinearVelocity(ToBodyID(bodyID))); }
    void PhysicsWorld3D::SetLinearVelocity(uint32_t bodyID, const glm::vec3& v) { m_Impl->BI().SetLinearVelocity(ToBodyID(bodyID), ToJolt(v)); }
    glm::vec3 PhysicsWorld3D::GetAngularVelocity(uint32_t bodyID) const { return FromJolt(m_Impl->BI().GetAngularVelocity(ToBodyID(bodyID))); }
    void PhysicsWorld3D::SetAngularVelocity(uint32_t bodyID, const glm::vec3& v) { m_Impl->BI().SetAngularVelocity(ToBodyID(bodyID), ToJolt(v)); }
    void PhysicsWorld3D::AddImpulse(uint32_t bodyID, const glm::vec3& i) { m_Impl->BI().AddImpulse(ToBodyID(bodyID), ToJolt(i)); }
    void PhysicsWorld3D::AddForce(uint32_t bodyID, const glm::vec3& f) { m_Impl->BI().AddForce(ToBodyID(bodyID), ToJolt(f)); }
    void PhysicsWorld3D::AddTorque(uint32_t bodyID, const glm::vec3& t) { m_Impl->BI().AddTorque(ToBodyID(bodyID), ToJolt(t)); }

    bool PhysicsWorld3D::IsBodyActive(uint32_t bodyID) const
    {
        return bodyID != 0xffffffff && m_Impl->HasBody(ToBodyID(bodyID)) && m_Impl->BI().IsActive(ToBodyID(bodyID));
    }

    // ------------------------------------------------------------------ characters
    uint32_t PhysicsWorld3D::CreateCharacter(const CharacterDesc& desc)
    {
        float radius = std::max(0.05f, desc.Radius);
        float halfCylinder = std::max(0.01f, desc.Height * 0.5f - radius);
        JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(halfCylinder, radius);
        // Offset so the character position is at the feet.
        JPH::RefConst<JPH::Shape> shape = new JPH::RotatedTranslatedShape(JPH::Vec3(0, halfCylinder + radius, 0), JPH::Quat::sIdentity(), capsule);

        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mShape = shape;
        settings->mInnerBodyShape = shape;
        const JPH::ObjectLayer layer = Layers::Make(desc.Layer, true);
        settings->mInnerBodyLayer = layer;
        settings->mMaxSlopeAngle = JPH::DegreesToRadians(desc.MaxSlopeDegrees);
        settings->mMass = desc.Mass;
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius); // feet area counts as support

        auto character = new JPH::CharacterVirtual(settings, JPH::RVec3(desc.Position.x, desc.Position.y, desc.Position.z),
                                                   JPH::Quat::sIdentity(), (uint64_t)desc.EntityID, m_Impl->System.get());
        // A slot freed by DestroyCharacter is reused (entities switched off and on again
        // destroy and recreate their character).
        uint32_t slot = 0;
        while (slot < m_Impl->Characters.size() && m_Impl->Characters[slot].Character)
            slot++;
        if (slot == m_Impl->Characters.size())
            m_Impl->Characters.push_back({});
        m_Impl->Characters[slot] = { character, (uint64_t)desc.EntityID, {}, layer };
        // Raycasts against the inner body report the entity.
        if (!character->GetInnerBodyID().IsInvalid())
        {
            m_Impl->BI().SetUserData(character->GetInnerBodyID(), (uint64_t)desc.EntityID);
            std::lock_guard lock(m_Impl->Contacts.Mutex);
            m_Impl->Contacts.BodyToEntity[character->GetInnerBodyID().GetIndexAndSequenceNumber()] = (uint64_t)desc.EntityID;
        }
        return slot;
    }

    void PhysicsWorld3D::DestroyCharacter(uint32_t id)
    {
        if (id >= m_Impl->Characters.size() || !m_Impl->Characters[id].Character)
            return;
        auto& slot = m_Impl->Characters[id];
        // The inner body goes with the character: its contacts end without events (as for
        // DestroyBody), and whatever rested on it (a crate on its head) wakes up.
        JPH::BodyID inner = slot.Character->GetInnerBodyID();
        JPH::AABox around;
        if (!inner.IsInvalid())
        {
            around = WakeBounds(m_Impl->BI(), inner);
            std::lock_guard lock(m_Impl->Contacts.Mutex);
            m_Impl->Contacts.BodyToEntity.erase(inner.GetIndexAndSequenceNumber());
        }
        slot.Character = nullptr; // releases the CharacterVirtual, which removes its inner body
        // Other characters touching its capsule (inner body) forget it as well.
        m_Impl->ForgetTouching(slot.EntityID);
        slot.EntityID = 0;
        slot.Touching.clear();
        if (around.IsValid())
            m_Impl->BI().ActivateBodiesInAABox(around, {}, {});
    }

    void PhysicsWorld3D::UpdateCharacter(uint32_t id, const glm::vec3& velocity, float dt, float stepHeight, bool stickToFloor)
    {
        if (id >= m_Impl->Characters.size() || !m_Impl->Characters[id].Character) return;
        auto& slot = m_Impl->Characters[id];
        JPH::CharacterVirtual& c = *slot.Character;
        c.SetLinearVelocity(ToJolt(velocity));

        JPH::CharacterVirtual::ExtendedUpdateSettings ext;
        ext.mWalkStairsStepUp = JPH::Vec3(0, std::max(0.0f, stepHeight), 0);
        ext.mStickToFloorStepDown = stickToFloor ? JPH::Vec3(0, -0.5f, 0) : JPH::Vec3::sZero();

        auto& sys = *m_Impl->System;
        c.ExtendedUpdate(dt, sys.GetGravity(), ext,
                         sys.GetDefaultBroadPhaseLayerFilter(slot.Layer),
                         sys.GetDefaultLayerFilter(slot.Layer),
                         JPH::IgnoreSingleBodyFilter(c.GetInnerBodyID()), {}, *m_Impl->TempAllocator);

        // Contact begin/end events
        std::vector<std::pair<uint64_t, bool>> touching;
        for (const auto& contact : c.GetActiveContacts())
        {
            if (!(contact.mHadCollision || contact.mIsSensorB) || !contact.mUserData) continue;
            std::pair<uint64_t, bool> entry{ contact.mUserData, contact.mIsSensorB };
            if (std::find(touching.begin(), touching.end(), entry) == touching.end())
                touching.push_back(entry);
        }
        std::lock_guard lock(m_Impl->Contacts.Mutex);
        for (auto [other, sensor] : touching)
            if (std::find(slot.Touching.begin(), slot.Touching.end(), std::make_pair(other, sensor)) == slot.Touching.end())
                m_Impl->Contacts.Events.push_back({ slot.EntityID, other, true, sensor });
        for (auto [other, sensor] : slot.Touching)
            if (std::find(touching.begin(), touching.end(), std::make_pair(other, sensor)) == touching.end())
                m_Impl->Contacts.Events.push_back({ slot.EntityID, other, false, sensor });
        slot.Touching = std::move(touching);
    }

    glm::vec3 PhysicsWorld3D::GetCharacterPosition(uint32_t id) const
    {
        if (id >= m_Impl->Characters.size() || !m_Impl->Characters[id].Character) return glm::vec3(0.0f);
        JPH::RVec3 p = m_Impl->Characters[id].Character->GetPosition();
        return { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
    }

    void PhysicsWorld3D::SetCharacterPosition(uint32_t id, const glm::vec3& p)
    {
        if (id < m_Impl->Characters.size() && m_Impl->Characters[id].Character)
            m_Impl->Characters[id].Character->SetPosition(JPH::RVec3(p.x, p.y, p.z));
    }

    glm::vec3 PhysicsWorld3D::GetCharacterVelocity(uint32_t id) const
    {
        if (id >= m_Impl->Characters.size() || !m_Impl->Characters[id].Character) return glm::vec3(0.0f);
        return FromJolt(m_Impl->Characters[id].Character->GetLinearVelocity());
    }

    glm::vec3 PhysicsWorld3D::GetCharacterGroundVelocity(uint32_t id) const
    {
        if (id >= m_Impl->Characters.size() || !m_Impl->Characters[id].Character) return glm::vec3(0.0f);
        return FromJolt(m_Impl->Characters[id].Character->GetGroundVelocity());
    }

    bool PhysicsWorld3D::IsCharacterGrounded(uint32_t id) const
    {
        if (id >= m_Impl->Characters.size() || !m_Impl->Characters[id].Character) return false;
        return m_Impl->Characters[id].Character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    // ------------------------------------------------------------------ collision layers
    void PhysicsWorld3D::SetCollisionMatrix(const std::array<uint16_t, 16>& masks)
    {
        m_Impl->ObjectPairFilter.Masks = masks;
    }

    bool PhysicsWorld3D::CanLayersCollide(int a, int b) const
    {
        if (a < 0 || a >= Layers::Count || b < 0 || b >= Layers::Count)
            return false;
        return m_Impl->ObjectPairFilter.ShouldCollide(Layers::Make(a, true), Layers::Make(b, true));
    }

    void PhysicsWorld3D::SetBodyLayer(uint32_t bodyID, int layer)
    {
        if (bodyID == 0xffffffff)
            return;
        // Called for every body every frame: Jolt's lookup (invalid for a body not in the world), not HasBody.
        auto& bi = m_Impl->BI();
        const JPH::BodyID id = ToBodyID(bodyID);
        const JPH::ObjectLayer now = bi.GetObjectLayer(id);
        if (now == JPH::cObjectLayerInvalid)
            return;
        const JPH::ObjectLayer wanted = Layers::Make(layer, Layers::IsMoving(now));
        if (now == wanted)
            return;
        bi.SetObjectLayer(id, wanted);
        // What it touched may not hold it any more (and it may not hold what rests on it): they wake.
        bi.ActivateBodiesInAABox(WakeBounds(bi, id), {}, {});
    }

    int PhysicsWorld3D::GetBodyLayer(uint32_t bodyID) const
    {
        if (bodyID == 0xffffffff)
            return -1;
        const JPH::ObjectLayer layer = m_Impl->BI().GetObjectLayer(ToBodyID(bodyID));
        return layer == JPH::cObjectLayerInvalid ? -1 : Layers::Index(layer);
    }

    void PhysicsWorld3D::SetCharacterLayer(uint32_t id, int layer)
    {
        if (id >= m_Impl->Characters.size() || !m_Impl->Characters[id].Character)
            return;
        auto& slot = m_Impl->Characters[id];
        const JPH::ObjectLayer wanted = Layers::Make(layer, true);
        if (slot.Layer == wanted)
            return;
        slot.Layer = wanted;
        const JPH::BodyID inner = slot.Character->GetInnerBodyID();
        if (!inner.IsInvalid())
        {
            m_Impl->BI().SetObjectLayer(inner, wanted);
            m_Impl->BI().ActivateBodiesInAABox(WakeBounds(m_Impl->BI(), inner), {}, {});
        }
    }

    int PhysicsWorld3D::GetCharacterLayer(uint32_t id) const
    {
        if (id >= m_Impl->Characters.size() || !m_Impl->Characters[id].Character)
            return -1;
        return Layers::Index(m_Impl->Characters[id].Layer);
    }

    size_t PhysicsWorld3D::GetMeshShapeCacheSize()
    {
        MeshShapeCache& cache = GetMeshShapeCache();
        std::lock_guard lock(cache.Mutex);
        cache.Prune();
        return cache.Entries.size();
    }

    // ------------------------------------------------------------------ queries
    namespace {

        bool IsFinite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

        // The unit direction of a ray / cast; false for a query that cannot hit anything.
        bool QueryDirection(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, glm::vec3& dir)
        {
            const float length = glm::length(direction);
            if (!(length > 0.0f) || !std::isfinite(length) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance) || !IsFinite(origin))
                return false;
            dir = direction / length;
            return true;
        }

        JPH::Quat QueryRotation(const glm::quat& rotation)
        {
            const float length = glm::length(rotation);
            if (!(length > 1e-6f) || !std::isfinite(length))
                return JPH::Quat::sIdentity();
            return ToJolt(rotation / length);
        }

        // The query shape in Jolt (null: a size that makes no shape).
        JPH::RefConst<JPH::Shape> MakeQueryShape(const PhysicsQueryShape3D& s)
        {
            switch (s.Type)
            {
                case PhysicsQueryShape3D::Kind::Sphere:
                    if (!(s.Radius > 0.0f) || !std::isfinite(s.Radius))
                        return nullptr;
                    return new JPH::SphereShape(s.Radius);
                case PhysicsQueryShape3D::Kind::Box:
                {
                    const glm::vec3 h = glm::abs(s.HalfExtents);
                    if (!(h.x > 0.0f && h.y > 0.0f && h.z > 0.0f) || !IsFinite(h))
                        return nullptr;
                    return new JPH::BoxShape(ToJolt(h), std::min({ h.x, h.y, h.z, JPH::cDefaultConvexRadius }));
                }
                case PhysicsQueryShape3D::Kind::Capsule:
                {
                    if (!(s.Radius > 0.0f) || !std::isfinite(s.Radius) || !std::isfinite(s.HalfHeight))
                        return nullptr;
                    if (s.HalfHeight <= 1e-5f)
                        return new JPH::SphereShape(s.Radius); // both ends in one place
                    return new JPH::CapsuleShape(s.HalfHeight, s.Radius);
                }
            }
            return nullptr;
        }

        // Shape cast hits: the first (or every) body the shape runs into on its way - not the ones it
        // starts inside (deeper than touching), nor the ones it only touches at the start while moving
        // along them (a sphere on the floor cast sideways) or away from them.
        class ShapeCastHits final : public JPH::CastShapeCollector
        {
        public:
            static constexpr float StartInside = 1.0e-3f; // penetration at the start that counts as "inside"
            static constexpr float MinApproach = 0.02f;   // cos of the steepest angle a touching start may move at (~89 degrees)

            ShapeCastHits(bool all, const glm::vec3& direction) : m_All(all), m_Direction(ToJolt(direction)) {}

            void AddHit(const JPH::ShapeCastResult& result) override
            {
                if (result.mFraction <= 0.0f)
                {
                    if (result.mPenetrationDepth > StartInside)
                        return;
                    const JPH::Vec3 axis = result.mPenetrationAxis;
                    if (axis.LengthSq() < 1e-12f || axis.Normalized().Dot(m_Direction) <= MinApproach)
                        return;
                }
                if (m_All)
                    Hits.push_back(result);
                else if (Hits.empty() || result.mFraction < Hits[0].mFraction)
                {
                    Hits.assign(1, result);
                    UpdateEarlyOutFraction(result.GetEarlyOutFraction());
                }
            }

            std::vector<JPH::ShapeCastResult> Hits;

        private:
            bool m_All;
            JPH::Vec3 m_Direction;
        };

        // Each entity's first hit only, the closest first (ties: by ID).
        void SortHits(std::vector<RaycastHit>& hits)
        {
            std::sort(hits.begin(), hits.end(), [](const RaycastHit& a, const RaycastHit& b) {
                return a.Distance != b.Distance ? a.Distance < b.Distance : (uint64_t)a.EntityID < (uint64_t)b.EntityID;
            });
            std::unordered_set<uint64_t> seen;
            std::erase_if(hits, [&](const RaycastHit& hit) { return !seen.insert((uint64_t)hit.EntityID).second; });
        }
    }

    // Rays: convex shapes are hollow and triangles one-sided, so a ray that starts inside a body
    // (or behind a mesh's triangle) goes through it.
    static void CastRayQuery(const JPH::PhysicsSystem& system, const glm::vec3& origin, const glm::vec3& dir, float maxDistance,
                             uint16_t layerMask, bool includeSensors, bool all, std::vector<RaycastHit>& out)
    {
        const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), ToJolt(dir * maxDistance));
        JPH::RayCastSettings settings;
        settings.SetBackFaceMode(JPH::EBackFaceMode::IgnoreBackFaces);
        settings.mTreatConvexAsSolid = false;
        const QueryLayerFilter layers(layerMask);
        const QueryBodyFilter bodies(includeSensors);
        std::vector<JPH::RayCastResult> results;
        if (all)
        {
            JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
            system.GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, layers, bodies);
            results.assign(collector.mHits.begin(), collector.mHits.end());
        }
        else
        {
            JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
            system.GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, layers, bodies);
            if (collector.HadHit())
                results.push_back(collector.mHit);
        }
        for (const JPH::RayCastResult& result : results)
        {
            JPH::BodyLockRead lock(system.GetBodyLockInterface(), result.mBodyID);
            if (!lock.Succeeded())
                continue;
            const JPH::Body& body = lock.GetBody();
            RaycastHit hit;
            hit.Hit = true;
            hit.Distance = result.mFraction * maxDistance;
            const JPH::RVec3 p = ray.GetPointOnRay(result.mFraction);
            hit.Point = { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
            hit.EntityID = body.GetUserData();
            hit.Normal = FromJolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, p));
            out.push_back(hit);
        }
    }

    RaycastHit PhysicsWorld3D::CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint16_t layerMask,
                                       bool includeSensors) const
    {
        glm::vec3 dir;
        if (!QueryDirection(origin, direction, maxDistance, dir))
            return {};
        std::vector<RaycastHit> hits;
        CastRayQuery(*m_Impl->System, origin, dir, maxDistance, layerMask, includeSensors, false, hits);
        return hits.empty() ? RaycastHit() : hits[0];
    }

    std::vector<RaycastHit> PhysicsWorld3D::CastRayAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                                       uint16_t layerMask, bool includeSensors) const
    {
        std::vector<RaycastHit> hits;
        glm::vec3 dir;
        if (!QueryDirection(origin, direction, maxDistance, dir))
            return hits;
        CastRayQuery(*m_Impl->System, origin, dir, maxDistance, layerMask, includeSensors, true, hits);
        SortHits(hits); // Jolt reports them in no particular order (a body's sub-shapes / triangles each)
        return hits;
    }

    std::vector<UUID> PhysicsWorld3D::Overlap(const PhysicsQueryShape3D& shape, const glm::vec3& center, const glm::quat& rotation,
                                              uint16_t layerMask, bool includeSensors) const
    {
        JPH::RefConst<JPH::Shape> query = IsFinite(center) ? MakeQueryShape(shape) : nullptr;
        if (!query)
            return {};
        JPH::CollideShapeSettings settings;
        // Every triangle it touches counts, from either side.
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const QueryLayerFilter layers(layerMask);
        const QueryBodyFilter bodies(includeSensors);
        const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(QueryRotation(rotation), JPH::RVec3(center.x, center.y, center.z));
        m_Impl->System->GetNarrowPhaseQuery().CollideShape(query, JPH::Vec3::sReplicate(1.0f), transform, settings, JPH::RVec3::sZero(), collector, {},
                                                           layers, bodies);
        std::unordered_set<uint32_t> seenBodies;
        std::vector<std::pair<float, uint64_t>> found;
        const auto& bi = m_Impl->BI();
        for (const JPH::CollideShapeResult& hit : collector.mHits)
        {
            if (!seenBodies.insert(hit.mBodyID2.GetIndexAndSequenceNumber()).second)
                continue;
            const uint64_t entity = bi.GetUserData(hit.mBodyID2);
            if (!entity)
                continue;
            const JPH::RVec3 p = bi.GetPosition(hit.mBodyID2);
            const glm::vec3 position{ (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
            const glm::vec3 d = position - center;
            found.push_back({ glm::dot(d, d), entity });
        }
        std::sort(found.begin(), found.end());
        std::vector<UUID> out;
        std::unordered_set<uint64_t> seen;
        for (const auto& [distance, id] : found)
            if (seen.insert(id).second)
                out.push_back(UUID(id));
        return out;
    }

    static std::vector<RaycastHit> CastShapeQuery(const JPH::PhysicsSystem& system, const PhysicsQueryShape3D& shape, const glm::vec3& origin,
                                                  const glm::quat& rotation, const glm::vec3& direction, float maxDistance, uint16_t layerMask,
                                                  bool includeSensors, bool all)
    {
        std::vector<RaycastHit> out;
        glm::vec3 dir;
        if (!QueryDirection(origin, direction, maxDistance, dir))
            return out;
        JPH::RefConst<JPH::Shape> query = MakeQueryShape(shape);
        if (!query)
            return out;
        const JPH::RMat44 start = JPH::RMat44::sRotationTranslation(QueryRotation(rotation), JPH::RVec3(origin.x, origin.y, origin.z));
        const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(query, JPH::Vec3::sReplicate(1.0f), start, ToJolt(dir * maxDistance));
        // Only what it runs into: back faces (moving out of a body / behind a triangle) do not count. The
        // deepest point of a body it starts in gives the depth that tells "inside" from "touching".
        JPH::ShapeCastSettings settings;
        settings.mReturnDeepestPoint = true;
        ShapeCastHits collector(all, dir);
        const QueryLayerFilter layers(layerMask);
        const QueryBodyFilter bodies(includeSensors);
        system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, layers, bodies);
        // The first touch of each body.
        std::unordered_map<uint32_t, const JPH::ShapeCastResult*> first;
        for (const JPH::ShapeCastResult& hit : collector.Hits)
        {
            auto [it, added] = first.try_emplace(hit.mBodyID2.GetIndexAndSequenceNumber(), &hit);
            if (!added && hit.mFraction < it->second->mFraction)
                it->second = &hit;
        }
        const auto& bi = system.GetBodyInterface();
        for (const auto& [body, hit] : first)
        {
            RaycastHit result;
            result.Hit = true;
            result.EntityID = bi.GetUserData(hit->mBodyID2);
            result.Distance = std::max(0.0f, hit->mFraction) * maxDistance;
            result.Point = FromJolt(hit->mContactPointOn2);
            // The penetration axis points from the cast shape into the body: its surface faces back.
            const JPH::Vec3 axis = hit->mPenetrationAxis;
            result.Normal = axis.LengthSq() > 1e-12f ? -FromJolt(axis.Normalized()) : -dir;
            out.push_back(result);
        }
        SortHits(out);
        return out;
    }

    RaycastHit PhysicsWorld3D::CastShape(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation,
                                         const glm::vec3& direction, float maxDistance, uint16_t layerMask, bool includeSensors) const
    {
        std::vector<RaycastHit> hits = CastShapeQuery(*m_Impl->System, shape, origin, rotation, direction, maxDistance, layerMask, includeSensors, false);
        return hits.empty() ? RaycastHit() : hits[0];
    }

    std::vector<RaycastHit> PhysicsWorld3D::CastShapeAll(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation,
                                                         const glm::vec3& direction, float maxDistance, uint16_t layerMask,
                                                         bool includeSensors) const
    {
        return CastShapeQuery(*m_Impl->System, shape, origin, rotation, direction, maxDistance, layerMask, includeSensors, true);
    }

    // ------------------------------------------------------------------ joints
    namespace {

        glm::vec3 SafeAxis(const glm::vec3& axis, const glm::vec3& fallback)
        {
            const float length = glm::length(axis);
            return length > 1e-6f && std::isfinite(length) ? axis / length : fallback;
        }

        // A unit vector at right angles to `axis` (the hinge's / slider's normal axis).
        glm::vec3 Perpendicular(const glm::vec3& axis)
        {
            glm::vec3 other = std::abs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            return glm::normalize(glm::cross(axis, other));
        }

        // Jolt wants a hinge's limits around the start pose (-180..0, 0..180 degrees, in radians) and
        // a slider's around its start position (lower <= 0 <= upper).
        void HingeLimits(const PhysicsJoint3DDesc& d, float& lower, float& upper)
        {
            if (!d.EnableLimit)
            {
                lower = -JPH::JPH_PI;
                upper = JPH::JPH_PI;
                return;
            }
            float lo = std::min(d.LowerLimit, d.UpperLimit), hi = std::max(d.LowerLimit, d.UpperLimit);
            lower = glm::radians(std::clamp(lo, -180.0f, 0.0f));
            upper = glm::radians(std::clamp(hi, 0.0f, 180.0f));
        }

        void SliderLimits(const PhysicsJoint3DDesc& d, float& lower, float& upper)
        {
            if (!d.EnableLimit)
            {
                lower = -FLT_MAX;
                upper = FLT_MAX;
                return;
            }
            lower = std::min(0.0f, std::min(d.LowerLimit, d.UpperLimit));
            upper = std::max(0.0f, std::max(d.LowerLimit, d.UpperLimit));
        }

        void DistanceRange(const PhysicsJoint3DDesc& d, float& minDistance, float& maxDistance)
        {
            minDistance = std::max(0.0f, std::min(d.MinDistance, d.MaxDistance));
            maxDistance = std::max(minDistance, std::max(d.MinDistance, d.MaxDistance));
        }

        JPH::SpringSettings MakeSpring(float frequency, float damping)
        {
            return JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, std::max(0.0f, frequency), std::max(0.0f, damping));
        }

        // The motor / spring of a hinge or slider as the description asks (settings that only change
        // what the solver does next step).
        void ApplyDrive(JPH::TwoBodyConstraint& constraint, Joint3DType type, const PhysicsJoint3DDesc& d)
        {
            if (type == Joint3DType::Hinge)
            {
                auto& hinge = static_cast<JPH::HingeConstraint&>(constraint);
                JPH::MotorSettings& motor = hinge.GetMotorSettings();
                if (d.EnableMotor)
                {
                    motor.SetTorqueLimit(std::max(0.0f, d.MaxMotorForce));
                    hinge.SetTargetAngularVelocity(glm::radians(d.MotorSpeed));
                    hinge.SetMotorState(JPH::EMotorState::Velocity);
                }
                else if (d.EnableSpring && d.Frequency > 0.0f)
                {
                    motor.SetTorqueLimit(FLT_MAX);
                    motor.mSpringSettings = MakeSpring(d.Frequency, d.DampingRatio);
                    hinge.SetTargetAngle(glm::radians(std::clamp(d.SpringTarget, -180.0f, 180.0f)));
                    hinge.SetMotorState(JPH::EMotorState::Position);
                }
                else
                    hinge.SetMotorState(JPH::EMotorState::Off);
            }
            else if (type == Joint3DType::Slider)
            {
                auto& slider = static_cast<JPH::SliderConstraint&>(constraint);
                if (d.EnableMotor)
                {
                    slider.GetMotorSettings().SetForceLimit(std::max(0.0f, d.MaxMotorForce));
                    slider.SetTargetVelocity(d.MotorSpeed);
                    slider.SetMotorState(JPH::EMotorState::Velocity);
                }
                else
                    slider.SetMotorState(JPH::EMotorState::Off);
            }
        }
    }

    static constexpr JPH::uint JointVelocitySteps = 16, JointPositionSteps = 6;

    uint32_t PhysicsWorld3D::CreateJoint(const PhysicsJoint3DDesc& d)
    {
        if (d.Body == PhysicsJoint3DDesc::NoBody)
            return 0;
        const JPH::BodyID id2 = ToBodyID(d.Body);
        const JPH::BodyID id1 = d.ConnectedBody == PhysicsJoint3DDesc::NoBody ? JPH::BodyID() : ToBodyID(d.ConnectedBody);
        if (!m_Impl->HasBody(id2) || id1 == id2 || (!id1.IsInvalid() && !m_Impl->HasBody(id1)))
            return 0;
        // Outside the step nothing else touches the bodies: no locks needed.
        const JPH::BodyLockInterface& locks = m_Impl->System->GetBodyLockInterfaceNoLock();
        JPH::Body* body2 = locks.TryGetBody(id2);
        JPH::Body* body1 = id1.IsInvalid() ? &JPH::Body::sFixedToWorld : locks.TryGetBody(id1);
        if (!body1 || !body2)
            return 0;

        const JPH::RVec3 point1(d.ConnectedAnchor.x, d.ConnectedAnchor.y, d.ConnectedAnchor.z);
        const JPH::RVec3 point2(d.Anchor.x, d.Anchor.y, d.Anchor.z);
        const glm::vec3 axis = SafeAxis(d.Axis, d.Type == Joint3DType::Slider ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 normal = Perpendicular(axis);
        JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
        switch (d.Type)
        {
            case Joint3DType::Fixed:
            {
                // The world-space frames are the same for both bodies: they stay as they are now.
                auto* s = new JPH::FixedConstraintSettings();
                s->mSpace = JPH::EConstraintSpace::WorldSpace;
                s->mPoint1 = point1;
                s->mPoint2 = point2;
                settings = s;
                break;
            }
            case Joint3DType::Hinge:
            {
                auto* s = new JPH::HingeConstraintSettings();
                s->mSpace = JPH::EConstraintSpace::WorldSpace;
                s->mPoint1 = point1;
                s->mPoint2 = point2;
                s->mHingeAxis1 = s->mHingeAxis2 = ToJolt(axis);
                s->mNormalAxis1 = s->mNormalAxis2 = ToJolt(normal);
                HingeLimits(d, s->mLimitsMin, s->mLimitsMax);
                settings = s;
                break;
            }
            case Joint3DType::Distance:
            {
                auto* s = new JPH::DistanceConstraintSettings();
                s->mSpace = JPH::EConstraintSpace::WorldSpace;
                s->mPoint1 = point1;
                s->mPoint2 = point2;
                DistanceRange(d, s->mMinDistance, s->mMaxDistance);
                s->mLimitsSpringSettings = MakeSpring(d.Frequency, d.DampingRatio);
                settings = s;
                break;
            }
            case Joint3DType::Slider:
            {
                auto* s = new JPH::SliderConstraintSettings();
                s->mSpace = JPH::EConstraintSpace::WorldSpace;
                s->mAutoDetectPoint = false;
                s->mPoint1 = point1;
                s->mPoint2 = point2;
                s->mSliderAxis1 = s->mSliderAxis2 = ToJolt(axis);
                s->mNormalAxis1 = s->mNormalAxis2 = ToJolt(normal);
                SliderLimits(d, s->mLimitsMin, s->mLimitsMax);
                settings = s;
                break;
            }
        }
        // Jointed bodies (chains, ragdolls) get more solver iterations than Jolt's defaults (10 / 2):
        // a long chain hardly stretches. Only the islands with a joint pay for it.
        settings->mNumVelocityStepsOverride = JointVelocitySteps;
        settings->mNumPositionStepsOverride = JointPositionSteps;
        JPH::Ref<JPH::TwoBodyConstraint> constraint = settings->Create(*body1, *body2);
        if (!constraint)
            return 0;
        ApplyDrive(*constraint, d.Type, d);
        m_Impl->System->AddConstraint(constraint);

        Impl::JointSlot joint;
        joint.Constraint = constraint;
        joint.Type = d.Type;
        joint.EntityID = (uint64_t)d.EntityID;
        joint.Body1 = id1;
        joint.Body2 = id2;
        glm::vec3 p1, p2;
        glm::quat q1, q2;
        m_Impl->GetFrame(id1, p1, q1);
        m_Impl->GetFrame(id2, p2, q2);
        joint.LocalPoint1 = glm::inverse(q1) * (d.ConnectedAnchor - p1);
        joint.LocalPoint2 = glm::inverse(q2) * (d.Anchor - p2);
        joint.LocalAxis2 = glm::inverse(q2) * axis;
        joint.Desc = d;
        joint.NoCollision = !d.CollideConnected && !id1.IsInvalid();
        if (joint.NoCollision)
            m_Impl->SetPairCollision(joint, true);
        m_Impl->WakeJointBodies(joint);
        const uint32_t handle = m_Impl->NextJoint++;
        m_Impl->Joints.emplace(handle, std::move(joint));
        return handle;
    }

    void PhysicsWorld3D::DestroyJoint(uint32_t joint)
    {
        if (auto it = m_Impl->Joints.find(joint); it != m_Impl->Joints.end())
            m_Impl->RemoveJoint(it);
    }

    void PhysicsWorld3D::ClaimJoint(UUID entity, Joint3DType type, uint32_t& joint)
    {
        uint32_t keep = 0;
        for (auto it = m_Impl->Joints.begin(); it != m_Impl->Joints.end();)
        {
            auto next = std::next(it);
            if (it->second.EntityID == (uint64_t)entity && it->second.Type == type)
            {
                if (it->first == joint)
                    keep = joint;
                else
                    m_Impl->RemoveJoint(it);
            }
            it = next;
        }
        joint = keep;
    }

    void PhysicsWorld3D::UpdateJoint(uint32_t handle, const PhysicsJoint3DDesc& d)
    {
        auto it = m_Impl->Joints.find(handle);
        if (it == m_Impl->Joints.end())
            return;
        Impl::JointSlot& joint = it->second;
        PhysicsJoint3DDesc& old = joint.Desc;
        bool changed = false;
        if (!joint.Body1.IsInvalid() && d.CollideConnected != old.CollideConnected)
        {
            joint.NoCollision = !d.CollideConnected;
            m_Impl->SetPairCollision(joint, joint.NoCollision);
            old.CollideConnected = d.CollideConnected;
            changed = true;
        }
        switch (joint.Type)
        {
            case Joint3DType::Hinge:
            {
                auto& hinge = static_cast<JPH::HingeConstraint&>(*joint.Constraint);
                if (d.EnableLimit != old.EnableLimit || d.LowerLimit != old.LowerLimit || d.UpperLimit != old.UpperLimit)
                {
                    float lower, upper;
                    HingeLimits(d, lower, upper);
                    hinge.SetLimits(lower, upper);
                    changed = true;
                }
                if (d.EnableMotor != old.EnableMotor || d.MotorSpeed != old.MotorSpeed || d.MaxMotorForce != old.MaxMotorForce ||
                    d.EnableSpring != old.EnableSpring || d.SpringTarget != old.SpringTarget || d.Frequency != old.Frequency ||
                    d.DampingRatio != old.DampingRatio)
                {
                    ApplyDrive(hinge, joint.Type, d);
                    changed = true;
                }
                break;
            }
            case Joint3DType::Distance:
            {
                auto& distance = static_cast<JPH::DistanceConstraint&>(*joint.Constraint);
                if (d.MinDistance != old.MinDistance || d.MaxDistance != old.MaxDistance)
                {
                    float lower, upper;
                    DistanceRange(d, lower, upper);
                    distance.SetDistance(lower, upper);
                    changed = true;
                }
                if (d.Frequency != old.Frequency || d.DampingRatio != old.DampingRatio)
                {
                    distance.SetLimitsSpringSettings(MakeSpring(d.Frequency, d.DampingRatio));
                    changed = true;
                }
                break;
            }
            case Joint3DType::Slider:
            {
                auto& slider = static_cast<JPH::SliderConstraint&>(*joint.Constraint);
                if (d.EnableLimit != old.EnableLimit || d.LowerLimit != old.LowerLimit || d.UpperLimit != old.UpperLimit)
                {
                    float lower, upper;
                    SliderLimits(d, lower, upper);
                    slider.SetLimits(lower, upper);
                    changed = true;
                }
                if (d.EnableMotor != old.EnableMotor || d.MotorSpeed != old.MotorSpeed || d.MaxMotorForce != old.MaxMotorForce)
                {
                    ApplyDrive(slider, joint.Type, d);
                    changed = true;
                }
                break;
            }
            case Joint3DType::Fixed:
                break;
        }
        if (!changed)
            return;
        // The anchors and the axis stay as the joint was made.
        const glm::vec3 anchor = old.Anchor, connectedAnchor = old.ConnectedAnchor, axis = old.Axis;
        const uint32_t body = old.Body, connected = old.ConnectedBody;
        old = d;
        old.Anchor = anchor;
        old.ConnectedAnchor = connectedAnchor;
        old.Axis = axis;
        old.Body = body;
        old.ConnectedBody = connected;
        m_Impl->WakeJointBodies(joint);
    }

    PhysicsJoint3DState PhysicsWorld3D::GetJointState(uint32_t handle, float invDt) const
    {
        PhysicsJoint3DState state;
        auto it = m_Impl->Joints.find(handle);
        if (it == m_Impl->Joints.end())
            return state;
        const Impl::JointSlot& joint = it->second;
        auto length2 = [](const JPH::Vector<2>& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1]); };
        float force = 0.0f, torque = 0.0f;
        glm::vec3 p2;
        glm::quat q2;
        m_Impl->GetFrame(joint.Body2, p2, q2);
        const glm::vec3 axis = q2 * joint.LocalAxis2;
        auto relative = [&](bool angular) {
            const auto& bi = m_Impl->BI();
            glm::vec3 v2 = FromJolt(angular ? bi.GetAngularVelocity(joint.Body2) : bi.GetLinearVelocity(joint.Body2));
            glm::vec3 v1 = joint.Body1.IsInvalid() ? glm::vec3(0.0f)
                                                   : FromJolt(angular ? bi.GetAngularVelocity(joint.Body1) : bi.GetLinearVelocity(joint.Body1));
            return v2 - v1;
        };
        switch (joint.Type)
        {
            case Joint3DType::Fixed:
            {
                const auto& c = static_cast<const JPH::FixedConstraint&>(*joint.Constraint);
                force = c.GetTotalLambdaPosition().Length();
                torque = c.GetTotalLambdaRotation().Length();
                break;
            }
            case Joint3DType::Hinge:
            {
                const auto& c = static_cast<const JPH::HingeConstraint&>(*joint.Constraint);
                force = c.GetTotalLambdaPosition().Length();
                const float rotation = length2(c.GetTotalLambdaRotation()), limits = c.GetTotalLambdaRotationLimits();
                torque = std::sqrt(rotation * rotation + limits * limits);
                state.Angle = glm::degrees(c.GetCurrentAngle());
                state.Speed = glm::degrees(glm::dot(relative(true), axis));
                break;
            }
            case Joint3DType::Distance:
            {
                const auto& c = static_cast<const JPH::DistanceConstraint&>(*joint.Constraint);
                force = std::abs(c.GetTotalLambdaPosition());
                glm::vec3 anchor, connectedAnchor, unused;
                GetJointFrame(handle, anchor, connectedAnchor, unused);
                state.Translation = glm::length(anchor - connectedAnchor);
                break;
            }
            case Joint3DType::Slider:
            {
                const auto& c = static_cast<const JPH::SliderConstraint&>(*joint.Constraint);
                const float position = length2(c.GetTotalLambdaPosition()), limits = c.GetTotalLambdaPositionLimits();
                force = std::sqrt(position * position + limits * limits);
                torque = c.GetTotalLambdaRotation().Length();
                state.Translation = c.GetCurrentPosition();
                state.Speed = glm::dot(relative(false), axis);
                break;
            }
        }
        state.Force = force * invDt;
        state.Torque = torque * invDt;
        return state;
    }

    bool PhysicsWorld3D::GetJointFrame(uint32_t handle, glm::vec3& anchor, glm::vec3& connectedAnchor, glm::vec3& axis) const
    {
        auto it = m_Impl->Joints.find(handle);
        if (it == m_Impl->Joints.end())
            return false;
        const Impl::JointSlot& joint = it->second;
        glm::vec3 p1, p2;
        glm::quat q1, q2;
        m_Impl->GetFrame(joint.Body1, p1, q1);
        m_Impl->GetFrame(joint.Body2, p2, q2);
        anchor = p2 + q2 * joint.LocalPoint2;
        connectedAnchor = p1 + q1 * joint.LocalPoint1;
        axis = q2 * joint.LocalAxis2;
        return true;
    }

    bool PhysicsWorld3D::IsJoint(uint32_t joint) const { return m_Impl->Joints.contains(joint); }

    size_t PhysicsWorld3D::GetJointCount() const { return m_Impl->Joints.size(); }

    bool PhysicsWorld3D::CanBodiesCollide(uint32_t bodyA, uint32_t bodyB) const
    {
        JPH::CollisionGroup a(m_Impl->JointFilter, bodyA, 0), b(m_Impl->JointFilter, bodyB, 0);
        return a.CanCollide(b);
    }

    std::vector<ContactEvent> PhysicsWorld3D::TakeEvents()
    {
        std::lock_guard lock(m_Impl->Contacts.Mutex);
        std::vector<ContactEvent> events = std::move(m_Impl->Contacts.Events);
        m_Impl->Contacts.Events.clear();
        return events;
    }

}
