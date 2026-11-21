#pragma once
#include "ProjectileModel.h"
#include "GameplayRules.h"
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <vector>
namespace Zamin::Gameplay {
    struct FireRequest
    {
        std::string Key;
        Entity Owner;                  // live, active, living health actor in this scene
        glm::vec3 Origin{};            // world-space launch offset from the owner's aim point (|.| <= 100)
        glm::vec3 Direction{};         // nonzero: used normalized; zero: aim at Target
        Entity Target;                 // optional: straight aim when Direction is zero, homing target
        double Damage = -1;            // < 0: the definition's Damage
    };
    struct FireResult
    {
        bool Success = false;
        uint32_t Handle = 0;           // slot | generation << 8; 0 = none
        std::string Reason;
    };
    // Kinds: Fired, Hit, Impact, Expired, Cancelled.
    struct CombatEvent
    {
        std::string Kind, Key;
        uint64_t Source = 0, Subject = 0;
        double Amount = 0;
        glm::vec3 Position{};
    };
    struct HitRecord
    {
        uint64_t Sequence = 0, Source = 0, Victim = 0;
        std::string Key;
        double Amount = 0, Time = 0;
        PayloadKind Kind = PayloadKind::Damage;
        bool Killed = false;
        glm::vec3 Position{};
    };
    struct CombatStatsView
    {
        uint32_t Live = 0, Pooled = 0, Visuals = 0, Steps = 0, Sweeps = 0, ActorTests = 0;
        uint64_t Hits = 0, Detonations = 0, Effects = 0;
        double DroppedTime = 0;
    };
    // Scene-owned runtime combat: shared projectile definitions, a fixed 128-slot projectile pool stepped on the
    // physics fixed step, and optional pooled visual prefabs. Projectiles are kinematic (no physics bodies):
    // actor hits are analytic swept spheres against living health actors; world blocking uses physics queries
    // that skip health actors. Health always changes through RuleWorld. Main thread only.
    class CombatWorld
    {
    public:
        static constexpr size_t MaxDefinitions = 32, MaxPerOwner = 32, FireBudget = 32, MaxCandidates = 16;
        static constexpr size_t MaxVisuals = 256, VisualBudget = 8, EventCapacity = 32, HitCapacity = 32, MaxActors = 512;
        static CombatWorld& For(Scene&);
        explicit CombatWorld(Scene* scene);
        CombatWorld(const CombatWorld&) = delete;
        CombatWorld& operator=(const CombatWorld&) = delete;
        // An identical redefinition succeeds (prefab instances may define on Start); a different one fails.
        bool DefineProjectile(const ProjectileSpec&, std::string& error);
        std::optional<ProjectileSpec> ProjectileDefinition(const std::string& key) const;
        FireResult Fire(const FireRequest&);
        // Releases a live projectile (emits Cancelled). A stale handle returns false.
        bool Cancel(uint32_t handle, bool detonate);
        std::optional<ProjectileState> Projectile(uint32_t handle) const;
        std::optional<CombatEvent> Poll(Entity);
        uint32_t Dropped(Entity) const;
        // Newest first: hit records younger than maxAge where the actor is the source (outgoing) and/or the victim.
        size_t Recent(Entity actor, bool outgoing, bool incoming, double maxAge, std::span<HitRecord> out) const;
        CombatStatsView Stats() const;
        // The definitions' gameplay fields as JSON (Prefab and Pool are visual only and excluded).
        std::string Signature() const;
        size_t LiveCount() const { return m_Pool.Count(); }
        size_t DefinitionCount() const { return m_Definitions.size(); }
        // Releases every projectile, hides the visuals and clears events, hit records and the step accumulator.
        // Definitions stay.
        void Clear();
        void BeginFrame();
        void Tick(double delta);
        // Play stop: destroys the pooled visual entities.
        void DestroyVisuals();
        double Now() const { return m_Now; }
    private:
        friend class SaveWorld;
        struct Visual
        {
            uint64_t ID = 0;
            uint32_t Handle = ~0u;
            int Slot = -1;
            bool Shown = false;
        };
        struct Definition
        {
            ProjectileSpec Spec;
            std::filesystem::path Prefab;
            std::vector<Visual> Visuals; // reserved to Spec.Pool
            bool VisualsDisabled = false;
        };
        struct Slot
        {
            ProjectileState State;
            Definition* Def = nullptr;
            uint32_t OwnerHandle = ~0u, TargetHandle = ~0u;
            int Visual = -1;
        };
        struct Events
        {
            uint32_t Handle = ~0u, Dropped = 0;
            std::array<CombatEvent, EventCapacity> Ring;
            uint32_t Head = 0, Count = 0;
        };
        void Check(Entity) const;
        Entity Resolve(uint64_t id, uint32_t& handle) const;
        void Emit(Entity actor, const char* kind, const std::string& key, uint64_t source, uint64_t subject, double amount, const glm::dvec3& position);
        void Record(uint64_t source, uint64_t victim, const std::string& key, double amount, PayloadKind kind, bool killed, const glm::dvec3& position);
        void Step(double h);
        void BuildProbes();
        bool IgnoredBlocker(UUID id) const;
        double WorldSweep(const ProjectileSpec&, const Segment&, bool first);
        void ApplyPayload(Slot&, Entity owner, Entity victim, const glm::dvec3& point);
        void Release(uint32_t slot);
        bool ValidateVisual(Entity root, std::string& error) const;
        Entity Instantiate(Definition&, std::string& error);
        void SyncVisuals();
        Scene* m_Scene;
        std::map<std::string, Definition> m_Definitions;
        ProjectilePool m_Pool;
        std::array<Slot, ProjectilePool::Capacity> m_Slots;
        std::map<uint64_t, Events> m_Events;
        std::array<HitRecord, HitCapacity> m_HitRing;
        uint32_t m_HitHead = 0, m_HitCount = 0;
        uint64_t m_HitSequence = 0;
        std::vector<ActorProbe> m_Probes;      // reserved once (MaxActors)
        std::vector<Entity> m_ProbeEntities;   // parallel to m_Probes
        std::array<CandidateHit, MaxCandidates> m_Candidates{};
        double m_Accumulator = 0, m_Now = 0, m_DroppedTime = 0;
        uint32_t m_Fires = 0, m_Instantiations = 0, m_Steps = 0, m_Sweeps = 0, m_ActorTests = 0;
        uint64_t m_Hits = 0;
    };
    void ResetCombat(Scene&, bool destroyVisuals = false);
    void BeginCombatFrame(Scene&);
    void TickCombat(Scene&, double delta);
}
