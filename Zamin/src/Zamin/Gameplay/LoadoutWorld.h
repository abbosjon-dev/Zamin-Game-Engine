#pragma once
#include "LoadoutModel.h"
#include "GameplayRules.h"
namespace Zamin::Gameplay {
    struct LoadoutSnapshot
    {
        CombatStats Stats{};
        LoadoutState State;
    };
    struct AbilityResult
    {
        bool Success = false;
        double Applied = 0;      // instant/area: the amount applied; projectile: its damage snapshot; effect: 1
        uint64_t Target = 0;
        uint32_t Projectile = 0; // projectile casts: the fired projectile's handle
        std::string Reason;
    };
    // Aim of a projectile cast: a nonzero Direction is used as is, otherwise the projectile aims at the victim.
    // Origin is the world-space launch offset from the caster's aim point (FireRequest::Origin).
    struct CastAim
    {
        glm::vec3 Direction{}, Origin{};
    };
    struct LoadoutEvent
    {
        std::string Kind, Key;
        double Amount = 0;
    };
    class LoadoutWorld
    {
    public:
        static LoadoutWorld& For(Scene&);
        explicit LoadoutWorld(Scene* scene) : m_Scene(scene) {}
        LoadoutWorld(const LoadoutWorld&) = delete;
        LoadoutWorld& operator=(const LoadoutWorld&) = delete;
        bool Configure(Entity, const CombatStats&, std::string&);
        bool DefineItem(Entity, const ItemSpec&, std::string&);
        bool DefineAbility(Entity, const AbilitySpec&, std::string&);
        bool Equip(Entity, const std::string&, std::string&);
        bool Unequip(Entity, EquipSlot, std::string&);
        bool Unlock(Entity, const std::string&, bool, std::string&);
        double Refill(Entity, double);
        std::optional<LoadoutSnapshot> State(Entity) const;
        std::optional<AbilitySpec> Ability(Entity, const std::string&) const;
        // Linked abilities (Projectile/Area/Effect) resolve the scene's combat definitions here. Resources are
        // committed only when the cast takes effect: a fired projectile (even if it misses later), an area that
        // reached a target, an applied effect, or an instant amount above zero.
        AbilityResult Cast(Entity, Entity, const std::string&, const RuleQuery&, const CastAim& aim = {});
        std::optional<LoadoutEvent> Poll(Entity);
        uint32_t Dropped(Entity) const;
        void BeginFrame();
        void Tick(double);
    private:
        friend class SaveWorld;
        friend double LoadoutDamage(Scene&, Entity, Entity, double, bool);
        friend double LoadoutAttack(Scene&, Entity);
        friend double LoadoutArmor(Scene&, Entity);
        friend void ClampLoadoutEnergy(Scene&, Entity);
        struct Record
        {
            uint32_t Handle = ~0u;
            CombatStats Base = DefaultCombatStats;
            std::map<std::string, ItemSpec> Items;
            std::map<std::string, AbilitySpec> Abilities;
            LoadoutState State;
            std::deque<LoadoutEvent> Events;
            uint32_t Dropped = 0;
        };
        Record* Find(Entity);
        const Record* Find(Entity) const;
        // action: the actor performs it (cast, equip), so a stun blocks it.
        Record* Usable(Entity, std::string&, bool action = false);
        void Emit(Record&, LoadoutEvent);
        Scene* m_Scene;
        std::map<uint64_t, Record> m_Records;
    };
    void ResetLoadouts(Scene&);
    void BeginLoadoutFrame(Scene&);
    void TickLoadouts(Scene&, double);
    // Outgoing Attack of `source` (when sourceAttack) plus the victim's Armor reduction. sourceAttack=false: the
    // caller snapshotted Attack already (projectiles), so only Armor applies.
    double LoadoutDamage(Scene&, Entity source, Entity victim, double amount, bool sourceAttack = true);
    // The actor's effective outgoing Attack, including status-effect modifiers (an actor without a loadout has
    // base 0 plus its effect bonus): the projectile damage snapshot at launch.
    double LoadoutAttack(Scene&, Entity);
    // The actor's effective Armor (same rules as LoadoutAttack).
    double LoadoutArmor(Scene&, Entity);
    // Clamps the actor's energy to its current capacity (called when a MaxEnergy effect bonus ends).
    void ClampLoadoutEnergy(Scene&, Entity);
}
