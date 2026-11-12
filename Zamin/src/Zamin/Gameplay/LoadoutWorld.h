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
        double Applied = 0;
        uint64_t Target = 0;
        std::string Reason;
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
        AbilityResult Cast(Entity, Entity, const std::string&, const RuleQuery&);
        std::optional<LoadoutEvent> Poll(Entity);
        uint32_t Dropped(Entity) const;
        void BeginFrame();
        void Tick(double);
    private:
        friend class SaveWorld;
        friend double LoadoutDamage(Scene&, Entity, Entity, double);
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
        Record* Usable(Entity, std::string&);
        void Emit(Record&, LoadoutEvent);
        Scene* m_Scene;
        std::map<uint64_t, Record> m_Records;
    };
    void ResetLoadouts(Scene&);
    void BeginLoadoutFrame(Scene&);
    void TickLoadouts(Scene&, double);
    double LoadoutDamage(Scene&, Entity source, Entity victim, double amount);
}
