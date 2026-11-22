#pragma once
#include "EffectModel.h"
#include "GameplayRules.h"
#include "SaveModel.h"
#include <array>
#include <filesystem>
#include <functional>
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
        std::string Effect;            // "" = the definition's Effect; otherwise a defined effect matching the kind
    };
    struct FireResult
    {
        bool Success = false;
        uint32_t Handle = 0;           // slot | generation << 8; 0 = none
        std::string Reason;
    };
    // Kinds: Fired, Hit, Impact, Expired, Detonated, Cancelled, EffectApplied, EffectRefreshed, EffectExpired,
    // EffectRemoved, EffectCleared (death).
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
        bool Periodic = false; // a damage/heal-over-time tick of a status effect
        glm::vec3 Position{};
    };
    struct AreaResult
    {
        bool Success = false;
        uint32_t Affected = 0; // targets the area reached (admitted, in range, visible)
        double Applied = 0;    // total damage/heal applied
        std::string Reason;
    };
    struct EffectView
    {
        bool Active = false;
        uint32_t Stacks = 0, Instances = 0; // Refresh: stacks; other rules: instances
        double Remaining = 0, Duration = 0, Magnitude = 0; // longest remaining; the duration cap; total per tick
    };
    struct CombatStatsView
    {
        uint32_t Live = 0, Pooled = 0, Visuals = 0, Steps = 0, Sweeps = 0, ActorTests = 0, Ticks = 0, Rays = 0;
        uint64_t Hits = 0, Detonations = 0, Effects = 0;
        double DroppedTime = 0;
    };
    // Scene-owned runtime combat: shared projectile, area and status-effect definitions, a fixed 128-slot
    // projectile pool and per-actor effect books stepped on the physics fixed step, and optional pooled visual
    // prefabs. Projectiles are kinematic (no physics bodies): actor hits are analytic swept spheres against
    // living health actors; world blocking and area line of sight use physics queries that skip health actors.
    // Health always changes through RuleWorld. Main thread only.
    class CombatWorld
    {
    public:
        static constexpr size_t MaxDefinitions = 32, MaxPerOwner = 32, FireBudget = 32, MaxCandidates = 16;
        static constexpr size_t MaxVisuals = 256, VisualBudget = 8, EventCapacity = 32, HitCapacity = 32, MaxActors = 512;
        static constexpr size_t MaxEffects = 1024, EffectBudget = 64, AreaBudget = 16, DetonationBudget = 16, MaxAreaRays = 32;
        static CombatWorld& For(Scene&);
        explicit CombatWorld(Scene* scene);
        CombatWorld(const CombatWorld&) = delete;
        CombatWorld& operator=(const CombatWorld&) = delete;
        // An identical redefinition succeeds (prefab instances may define on Start); a different one fails.
        bool DefineProjectile(const ProjectileSpec&, std::string& error);
        std::optional<ProjectileSpec> ProjectileDefinition(const std::string& key) const;
        bool DefineArea(const AreaSpec&, std::string& error);
        std::optional<AreaSpec> AreaDefinition(const std::string& key) const;
        bool DefineEffect(const EffectSpec&, std::string& error);
        std::optional<EffectSpec> EffectDefinition(const std::string& key) const;
        FireResult Fire(const FireRequest&);
        // Releases a live projectile (emits Cancelled). With `detonate` and an area payload it detonates on the
        // next fixed step instead. A stale handle returns false.
        bool Cancel(uint32_t handle, bool detonate);
        // Instant area at `center` (world). `source` (optional) gives team, kill credit and its Attack (Damage
        // areas); without a source the area affects every actor in range. magnitude < 0: the definition's Amount.
        // `effect` ("" = the definition's Effect) overrides the status applied to every affected target.
        AreaResult ApplyArea(const std::string& key, Entity source, glm::vec3 center, double magnitude = -1, const std::string& effect = {});
        // Applies a status effect to a living, active health actor. `source` (optional) gives team, kill credit
        // and the team rule (hostile: not on allies; beneficial: allies and self only). magnitude < 0: TickAmount.
        ApplyResult ApplyEffect(Entity target, const std::string& key, Entity source = {}, double magnitude = -1);
        // Removes every instance of `key` ("" = all) and emits EffectRemoved. Returns the number removed.
        size_t RemoveEffects(Entity, const std::string& key);
        EffectView Effect(Entity, const std::string& key) const;
        EffectModifiers Modifiers(Entity) const;
        // Copies the actor's instances (Sequence order) into `out`; returns the count written.
        size_t Effects(Entity, std::span<EffectInstance> out) const;
        size_t EffectCount() const { return m_EffectCount; }
        std::optional<ProjectileState> Projectile(uint32_t handle) const;
        std::optional<CombatEvent> Poll(Entity);
        uint32_t Dropped(Entity) const;
        // Newest first: hit records younger than maxAge where the actor is the source (outgoing) and/or the victim;
        // periodic = false skips effect ticks (the hit marker's direct hits only).
        size_t Recent(Entity actor, bool outgoing, bool incoming, double maxAge, std::span<HitRecord> out, bool periodic = true) const;
        CombatStatsView Stats() const;
        // The definitions' gameplay fields as JSON (Prefab and Pool are visual only and excluded).
        std::string Signature() const;
        size_t LiveCount() const { return m_Pool.Count(); }
        size_t DefinitionCount() const { return m_Definitions.size() + m_Areas.size() + m_EffectSpecs.size(); }
        // Releases every projectile, removes every status effect, hides the visuals and clears events, hit records
        // and the step accumulator. Definitions stay.
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
        struct Book
        {
            uint32_t Handle = ~0u;
            EffectBook Effects;
            EffectModifiers Modifiers; // recomposed on every change
        };
        friend void OnCombatDeath(Scene&, Entity);
        // ---- save v4 support (SaveWorld): capture, then a staged, all-or-nothing restore
        // A save reference resolved by SaveWorld: the entity (if live) and the UUID it names (0 = none).
        struct Resolved
        {
            Entity Live;
            uint64_t ID = 0;
        };
        using Referencer = std::function<std::string(uint64_t)>;
        using Resolver = std::function<Resolved(const std::string&)>;
        struct RestorePlan
        {
            std::map<uint64_t, Book> Books;
            ProjectilePool Pool;
            std::array<Slot, ProjectilePool::Capacity> Slots;
        };
        // True when the scene uses combat (a definition, an effect or a projectile): such scenes write save v4.
        bool Persists() const { return DefinitionCount() > 0 || m_EffectCount > 0 || m_Pool.Count() > 0; }
        std::vector<SavedEffect> SaveEffects(Entity, const Referencer&) const;
        SavedCombat SaveCombat(const Referencer&) const;
        // Starts from the current books minus every listed (bound) entity's book and an empty projectile pool.
        void BeginRestore(RestorePlan&, const std::vector<uint64_t>& bound) const;
        // Validates and stages one present entity's saved effects; returns their stat bonus (loadout validation).
        CombatStats StageEffects(RestorePlan&, Entity, const std::vector<SavedEffect>&, const Resolver&) const;
        void StageProjectiles(RestorePlan&, const SavedCombat&, const Resolver&); // world state is unchanged
        // Swaps the staged state in: visuals hidden (reacquired lazily), events, hit records and accumulator cleared.
        void CommitRestore(RestorePlan&);
        void Check(Entity) const;
        Entity Resolve(uint64_t id, uint32_t& handle) const;
        void Emit(Entity actor, const char* kind, const std::string& key, uint64_t source, uint64_t subject, double amount, const glm::dvec3& position);
        void Record(uint64_t source, uint64_t victim, const std::string& key, double amount, PayloadKind kind, bool killed, const glm::dvec3& position, bool periodic = false);
        const Book* FindBook(Entity) const;
        void Recompose(Book&, Entity);
        std::string PayloadError(const ProjectileSpec&) const;
        std::string AreaError(const AreaSpec&) const;
        ApplyResult Apply(Entity target, const EffectSpec&, Entity source, uint16_t team, bool friendlyFire, double magnitude);
        // teams: payload admission with the team snapshot (a source, or any projectile); false = environment.
        AreaResult Area(const AreaSpec&, Entity source, uint64_t sourceID, uint16_t team, bool teams, glm::dvec3 center, double magnitude, const EffectSpec* effect);
        std::string EffectError(PayloadKind, const std::string& key, const char* missing, const char* mismatch) const;
        bool Visible(const AreaSpec&, const glm::dvec3& from, const glm::dvec3& to);
        void Detonate(Slot&, Entity owner);
        void ClearEffects(Entity);
        void EffectsPhase(double h);
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
        std::map<std::string, AreaSpec> m_Areas;
        std::map<std::string, EffectSpec> m_EffectSpecs;
        std::map<uint64_t, Book> m_Books;      // at most MaxActors, UUID order
        size_t m_EffectCount = 0;              // live instances in all books, at most MaxEffects
        ProjectilePool m_Pool;
        std::array<Slot, ProjectilePool::Capacity> m_Slots;
        std::map<uint64_t, Events> m_Events;
        std::array<HitRecord, HitCapacity> m_HitRing;
        uint32_t m_HitHead = 0, m_HitCount = 0;
        uint64_t m_HitSequence = 0;
        std::vector<ActorProbe> m_Probes;      // reserved once (MaxActors)
        std::vector<Entity> m_ProbeEntities;   // parallel to m_Probes
        std::array<CandidateHit, MaxCandidates> m_Candidates{};
        std::vector<EffectTick> m_Ticks;       // reserved once (16 instances x 4 ticks)
        std::vector<ExpiredEffect> m_Expired;  // reserved once (16)
        std::vector<std::pair<double, uint64_t>> m_AreaTargets; // reserved once (MaxActors)
        double m_Accumulator = 0, m_Now = 0, m_DroppedTime = 0;
        uint32_t m_Fires = 0, m_Instantiations = 0, m_Steps = 0, m_Sweeps = 0, m_ActorTests = 0;
        uint32_t m_EffectCalls = 0, m_AreaCalls = 0, m_TickCount = 0, m_Rays = 0;
        uint64_t m_Hits = 0, m_Detonations = 0;
    };
    void ResetCombat(Scene&, bool destroyVisuals = false);
    void BeginCombatFrame(Scene&);
    void TickCombat(Scene&, double delta);
    // Effect modifiers for the shared gameplay rules; neutral without a combat world or effects.
    CombatStats CombatStatBonus(Scene&, Entity);
    double CombatSpeedScale(Scene&, Entity);
    bool CombatStunned(Scene&, Entity);
    // RuleWorld calls this when damage kills an actor: its status effects are removed (EffectCleared).
    void OnCombatDeath(Scene&, Entity);
}
