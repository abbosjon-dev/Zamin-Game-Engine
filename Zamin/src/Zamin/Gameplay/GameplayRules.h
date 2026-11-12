#pragma once
#include "RulesModel.h"
#include <glm/glm.hpp>
#include <deque>
#include <optional>
#include <vector>
namespace Zamin {
    class Scene;
    class Entity;
}
namespace Zamin::Gameplay {
    struct RuleEvent
    {
        std::string Kind, Item;
        uint64_t Source = 0, Subject = 0;
        double Amount = 0;
    };
    struct InteractionSettings
    {
        std::string Prompt = "Interact", RequiredItem, RewardItem;
        int RequiredCount = 1, RewardCount = 1, MaxUses = 1;
        bool Consume = false, Deactivate = false, Enabled = true;
        double Cooldown = .2, Heal = 0;
        glm::vec3 Offset{};
    };
    struct RuleQuery
    {
        double Range = 3, MinDot = -1;
        glm::vec3 Facing{ 1, 0, 0 }, Origin{ 0, 1, 0 };
        bool TwoD = false, LineOfSight = true;
        uint16_t Mask = 65535;
    };
    struct UseResult
    {
        bool Success = false;
        uint64_t Target = 0;
        double Distance = 0, Healed = 0;
        std::string Reason, Prompt;
    };
    struct AttackResult
    {
        DamageResult Damage;
        uint64_t Target = 0;
        std::string Reason;
    };
    // Scene-owned runtime rules. Graph configs serialize normally; mutable combat/bag state does not.
    // No callbacks are delivered inside transactions. One consumer graph may drain each actor's event FIFO.
    class RuleWorld
    {
    public:
        static RuleWorld& For(Scene&);
        explicit RuleWorld(Scene* scene) : m_Scene(scene) {}
        RuleWorld(const RuleWorld&) = delete;
        RuleWorld& operator=(const RuleWorld&) = delete;
        void BeginFrame();
        void Tick(float delta);
        bool Configure(Entity, const HealthSettings&, bool reset, std::string& error);
        std::optional<HealthState> Health(Entity) const;
        bool Dead(Entity) const;
        DamageResult Damage(Entity victim, Entity source, double amount, bool friendlyFire, bool pierce, bool ignoreGrace);
        double Heal(Entity, double amount, bool shield);
        bool CanRevive(Entity, double grace, std::string& error) const;
        bool Revive(Entity, double grace, std::string& error);
        bool ConfigureBag(Entity, int slots, int stack, bool reset, std::string& error);
        bool ChangeItem(Entity, const std::string&, int count, bool remove, std::string& error);
        Bag Inventory(Entity) const;
        bool ConfigureInteraction(Entity, const InteractionSettings&, bool reset, std::string& error);
        UseResult Find(Entity actor, const RuleQuery&);
        UseResult Interact(Entity actor, Entity object, const RuleQuery&);
        AttackResult Attack(Entity actor, const RuleQuery&, double amount, double cooldown, bool friendlyFire);
        UseResult Sense(Entity actor, const RuleQuery&, const std::string& tag, Entity explicitTarget, double nearDistance = 0);
        AttackResult Strike(Entity actor, Entity victim, const RuleQuery&, double amount, double cooldown, bool friendlyFire);
        unsigned QueryBudgetRemaining() const { return m_Queries < 128 ? 128 - m_Queries : 0; }
        std::optional<RuleEvent> Poll(Entity);
        uint32_t Dropped(Entity) const;
        size_t ActorCount() const { return m_Actors.size(); }
        size_t InteractionCount() const { return m_Interactions.size(); }
        double Now() const { return m_Now; }
    private:
        friend class SaveWorld;
        friend class QuestWorld;
        friend class LoadoutWorld;
        struct Actor
        {
            uint32_t Handle = ~0u, Dropped = 0;
            std::optional<HealthState> Health;
            Bag Inventory;
            double AttackUntil = 0;
            std::deque<RuleEvent> Events;
        };
        struct Interaction
        {
            uint32_t Handle = ~0u;
            InteractionSettings Settings;
            uint32_t Uses = 0;
            double Until = 0;
        };
        Actor& RequireActor(Entity);
        Actor* FindActor(Entity);
        const Actor* FindActor(Entity) const;
        void Check(Entity) const;
        void Emit(Actor&, RuleEvent);
        void Budget();
        bool Visible(Entity actor, Entity object, const RuleQuery&, glm::vec3 point, double& distance) const;
        UseResult CanUse(Entity actor, Entity object, const RuleQuery&, const Interaction&) const;
        Scene* m_Scene;
        std::map<uint64_t, Actor> m_Actors;
        std::map<uint64_t, Interaction> m_Interactions;
        double m_Now = 0;
        unsigned m_Queries = 0;
    };
    void ResetRules(Scene&);
    void BeginRulesFrame(Scene&);
    void TickRules(Scene&, float delta);
}
