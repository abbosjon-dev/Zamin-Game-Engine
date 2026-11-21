#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
namespace Zamin::Gameplay {
    // Shared by projectiles and areas: what a delivered payload does to its target.
    enum class PayloadKind
    {
        Damage,
        Heal,
        Shield
    };
    // Immutable, scene-shared projectile definition. Kinematic flight: no physics body, no contacts.
    struct ProjectileSpec
    {
        std::string Key, Effect, Area, Prefab;    // Effect/Area: optional registry keys; Prefab: visual only
        PayloadKind Kind = PayloadKind::Damage;
        double Speed = 12, Lifetime = 2, MaxDistance = 0; // 0.1..500 m/s, 0.02..30 s, 0 (off) or 0.1..1000 m
        double Radius = .1, TargetRadius = .45, Damage = 10; // 0..5, 0..10, 0..1e6 (0 = effect-only)
        double Homing = 0;                         // degrees/s turn rate toward the target, 0..1440
        glm::dvec3 Gravity{}, TargetOffset{};      // each |component| <= 100; |offset| <= 100
        int Pierce = 0;                            // extra targets after the first, 0..7
        int Pool = 8;                              // pooled visual instances, 0..32
        uint16_t BlockMask = 0xFFFF;               // world-channel layers; 0 = no world blocking
        bool TwoD = false, FriendlyFire = false, PierceShield = false, IgnoreGrace = false, DetonateOnExpire = false;
        bool operator==(const ProjectileSpec&) const = default;
    };
    enum class ProjectilePhase : uint8_t
    {
        Flying,
        Detonating
    };
    struct ProjectileState
    {
        static constexpr size_t MaxHits = 8;
        std::string Key;
        std::string Effect;                        // payload effect override (an ability's Effect); "" = the spec's
        uint64_t Owner = 0, Target = 0;            // UUIDs (0 = none); generational handles live in CombatWorld
        uint16_t Team = 0;                         // the owner's team, snapshotted at launch
        glm::dvec3 Position{}, Velocity{}, Muzzle{}; // Muzzle: first-step offset from the aim point to the launch point
        double Age = 0, Travelled = 0, Damage = 0; // Damage: outgoing snapshot (base + owner Attack)
        double Attack = 0;                         // owner Attack at launch: added to a Damage area payload
        std::array<uint64_t, MaxHits> Hits{};      // one hit per target
        uint8_t HitCount = 0, PierceLeft = 0;
        ProjectilePhase Phase = ProjectilePhase::Flying;
        bool HasHit(uint64_t id) const;
    };
    bool ValidateProjectileSpec(const ProjectileSpec&, std::string& error);
    // Save/restore validation of a live state against its definition.
    bool ValidateProjectileState(const ProjectileSpec&, const ProjectileState&, std::string& error);
    struct Segment
    {
        glm::dvec3 From{}, To{};
    };
    // One fixed step h: homing (turn toward the target by at most Homing*h, speed kept), then v += g*h and
    // p += v*h (semi-implicit Euler). The first step also sweeps the muzzle offset: From is the owner's aim point.
    Segment AdvanceProjectile(const ProjectileSpec&, ProjectileState&, double h, const glm::dvec3* homingTarget);
    // A living, active health actor; Center is its world position (the spec's TargetOffset is added at test time).
    struct ActorProbe
    {
        uint64_t ID = 0;
        glm::dvec3 Center{};
        uint16_t Team = 0;
    };
    struct CandidateHit
    {
        double T = 0; // 0..1 along the segment
        uint64_t ID = 0;
    };
    // Payload admission: may this payload affect the target? (owner, same nonzero team, others)
    bool Admits(PayloadKind, bool friendlyFire, uint64_t owner, uint16_t ownerTeam, uint64_t target, uint16_t targetTeam, bool affectOwner);
    // First contact of a sphere of `radius` moving along the segment with a point (2D: XY only). Start inside: 0.
    std::optional<double> SweepSphere(const Segment&, const glm::dvec3& center, double radius, bool twoD);
    // Analytic swept sphere against the probes; skips the owner, already-hit and inadmissible targets. Sorted by
    // (T, ID) and truncated to out.size() (at most 16). Returns the count written. Allocates nothing.
    size_t SweepActors(const ProjectileSpec&, const ProjectileState&, const Segment&, std::span<const ActorProbe>, std::span<CandidateHit> out);
    // Fixed-capacity slot pool: lowest free slot first, generation bumps on release. Handle = slot | generation << 8;
    // generations are 1..2^24-1, so handle 0 never names a projectile.
    class ProjectilePool
    {
    public:
        static constexpr size_t Capacity = 128;
        ProjectilePool();
        std::optional<uint32_t> Acquire();
        void Release(uint32_t slot);
        bool Live(uint32_t handle) const;
        bool Active(uint32_t slot) const { return slot < Capacity && m_Active[slot]; }
        uint32_t Handle(uint32_t slot) const { return slot | m_Generation[slot] << 8; }
        uint32_t Generation(uint32_t slot) const { return m_Generation[slot]; }
        // Save restore: occupy a slot with an exact generation. False when taken or out of range.
        bool Place(uint32_t slot, uint32_t generation);
        size_t Count() const { return m_Count; }
        void Clear();
        static uint32_t Slot(uint32_t handle) { return handle & 0xFF; }
        static constexpr uint32_t MaxGeneration = 0xFFFFFF;
    private:
        std::array<uint32_t, Capacity> m_Generation{};
        std::array<bool, Capacity> m_Active{};
        size_t m_Count = 0;
    };
}
