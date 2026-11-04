#include "Zamin/Physics/PhysicsWorld3D.h"
#include "Zamin/Core/Log.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Zamin {

    namespace {

        namespace Layers
        {
            static constexpr JPH::ObjectLayer NON_MOVING = 0;
            static constexpr JPH::ObjectLayer MOVING = 1;
            static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
        }

        namespace BroadPhaseLayers
        {
            static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
            static constexpr JPH::BroadPhaseLayer MOVING(1);
            static constexpr JPH::uint NUM_LAYERS(2);
        }

        class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                if (a == Layers::NON_MOVING) return b == Layers::MOVING;
                return true;
            }
        };

        class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
        {
        public:
            BPLayerInterfaceImpl()
            {
                m_Map[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
                m_Map[Layers::MOVING] = BroadPhaseLayers::MOVING;
            }
            JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return m_Map[layer]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
            }
#endif
        private:
            JPH::BroadPhaseLayer m_Map[Layers::NUM_LAYERS];
        };

        class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
            {
                if (layer == Layers::NON_MOVING) return bp == BroadPhaseLayers::MOVING;
                return true;
            }
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

        inline JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
        inline JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
        inline glm::vec3 FromJolt(const JPH::Vec3& v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
        inline glm::quat FromJolt(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
    }

    // ------------------------------------------------------------------ contacts
    class ContactListener3D : public JPH::ContactListener
    {
    public:
        std::mutex Mutex;
        std::unordered_map<uint32_t, uint64_t> BodyToEntity; // guarded by Mutex
        std::vector<ContactEvent> Events;
        // count active sub-shape contacts per body pair, so begin/end fire once per pair
        std::unordered_map<uint64_t, int> PairCounts;

        static uint64_t PairKey(uint32_t a, uint32_t b)
        {
            if (a > b) std::swap(a, b);
            return ((uint64_t)a << 32) | b;
        }

        void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold&, JPH::ContactSettings&) override
        {
            std::lock_guard lock(Mutex);
            uint32_t a = b1.GetID().GetIndexAndSequenceNumber(), b = b2.GetID().GetIndexAndSequenceNumber();
            if (PairCounts[PairKey(a, b)]++ == 0)
                Push(a, b, true);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
        {
            std::lock_guard lock(Mutex);
            uint32_t a = pair.GetBody1ID().GetIndexAndSequenceNumber(), b = pair.GetBody2ID().GetIndexAndSequenceNumber();
            auto it = PairCounts.find(PairKey(a, b));
            if (it == PairCounts.end()) return;
            if (--it->second <= 0)
            {
                PairCounts.erase(it);
                Push(a, b, false);
            }
        }

    private:
        void Push(uint32_t a, uint32_t b, bool begin)
        {
            auto ea = BodyToEntity.find(a), eb = BodyToEntity.find(b);
            if (ea != BodyToEntity.end() && eb != BodyToEntity.end())
                Events.push_back({ ea->second, eb->second, begin });
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

        JPH::BodyInterface& BI() { return System->GetBodyInterface(); }
        const JPH::BodyInterface& BI() const { return System->GetBodyInterface(); }
    };

    static JPH::BodyID ToBodyID(uint32_t id) { return JPH::BodyID(id); }

    PhysicsWorld3D::PhysicsWorld3D(const glm::vec3& gravity) : m_Impl(std::make_unique<Impl>())
    {
        JoltAcquire();
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
        auto& bi = m_Impl->BI();
        for (const JPH::BodyID& id : m_Impl->Bodies)
        {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
        }
        m_Impl->Bodies.clear();
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

        auto makeShape = [](const PhysicsBody3DDesc::Shape& s) -> JPH::RefConst<JPH::Shape> {
            JPH::RefConst<JPH::Shape> shape;
            switch (s.Type)
            {
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

        JPH::RefConst<JPH::Shape> shape;
        if (desc.Shapes.size() == 1)
            shape = makeShape(desc.Shapes[0]);
        else
        {
            JPH::StaticCompoundShapeSettings compound;
            for (const auto& s : desc.Shapes)
                compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), makeShape(s));
            auto result = compound.Create();
            if (result.HasError())
            {
                ZM_CORE_ERROR("Jolt compound shape xatosi: {}", result.GetError().c_str());
                return 0xffffffff;
            }
            shape = result.Get();
        }

        JPH::EMotionType motion = desc.MotionType == 0 ? JPH::EMotionType::Static :
                                  desc.MotionType == 2 ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
        JPH::ObjectLayer layer = motion == JPH::EMotionType::Static ? Layers::NON_MOVING : Layers::MOVING;

        JPH::BodyCreationSettings settings(shape, JPH::RVec3(desc.Position.x, desc.Position.y, desc.Position.z),
                                           ToJolt(glm::normalize(desc.Rotation)), motion, layer);
        settings.mFriction = desc.Shapes[0].Friction;
        settings.mRestitution = desc.Shapes[0].Restitution;
        settings.mIsSensor = std::all_of(desc.Shapes.begin(), desc.Shapes.end(), [](auto& s) { return s.IsSensor; });
        settings.mLinearDamping = desc.LinearDamping;
        settings.mAngularDamping = desc.AngularDamping;
        settings.mGravityFactor = desc.GravityFactor;
        settings.mUserData = (uint64_t)desc.EntityID;
        if (settings.mIsSensor && motion != JPH::EMotionType::Dynamic)
            settings.mCollideKinematicVsNonDynamic = true;
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
        bi.AddBody(id, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        m_Impl->Bodies.push_back(id);
        {
            std::lock_guard lock(m_Impl->Contacts.Mutex);
            m_Impl->Contacts.BodyToEntity[id.GetIndexAndSequenceNumber()] = (uint64_t)desc.EntityID;
        }
        return id.GetIndexAndSequenceNumber();
    }

    void PhysicsWorld3D::DestroyBody(uint32_t bodyID)
    {
        if (bodyID == 0xffffffff) return;
        JPH::BodyID id = ToBodyID(bodyID);
        auto& bi = m_Impl->BI();
        auto it = std::find(m_Impl->Bodies.begin(), m_Impl->Bodies.end(), id);
        if (it == m_Impl->Bodies.end()) return;
        bi.RemoveBody(id);
        bi.DestroyBody(id);
        m_Impl->Bodies.erase(it);
        std::lock_guard lock(m_Impl->Contacts.Mutex);
        m_Impl->Contacts.BodyToEntity.erase(bodyID);
    }

    void PhysicsWorld3D::OptimizeBroadPhase() { m_Impl->System->OptimizeBroadPhase(); }

    void PhysicsWorld3D::Step(float dt)
    {
        m_Impl->System->Update(dt, 1, m_Impl->TempAllocator.get(), m_Impl->JobSystem.get());
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

    RaycastHit PhysicsWorld3D::CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const
    {
        RaycastHit hit;
        glm::vec3 dir = glm::length(direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0, -1, 0);
        JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), ToJolt(dir * maxDistance));
        JPH::RayCastResult result;
        if (m_Impl->System->GetNarrowPhaseQuery().CastRay(ray, result))
        {
            hit.Hit = true;
            hit.Distance = result.mFraction * maxDistance;
            JPH::RVec3 p = ray.GetPointOnRay(result.mFraction);
            hit.Point = { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
            JPH::BodyLockRead lock(m_Impl->System->GetBodyLockInterface(), result.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Body& body = lock.GetBody();
                hit.EntityID = body.GetUserData();
                hit.Normal = FromJolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, p));
            }
        }
        return hit;
    }

    std::vector<ContactEvent> PhysicsWorld3D::TakeEvents()
    {
        std::lock_guard lock(m_Impl->Contacts.Mutex);
        std::vector<ContactEvent> events = std::move(m_Impl->Contacts.Events);
        m_Impl->Contacts.Events.clear();
        return events;
    }

}
