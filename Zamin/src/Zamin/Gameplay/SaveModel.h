#pragma once
#include "RulesModel.h"
#include "QuestModel.h"
#include "LoadoutModel.h"
#include "GameplayAI.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include <vector>
namespace Zamin::Gameplay {
    // Save v4: one active status-effect instance. Times are relative (seconds left), so no clock is rebased.
    struct SavedEffect
    {
        std::string Key, Source;       // Source: a save key, "uuid:<n>" (an unbound authored entity) or ""
        uint32_t Team = 0, Stacks = 1; // Team: the applier's team snapshot (0..65535); Stacks 1..16
        bool FriendlyFire = false;
        double Remaining = 0, Tick = 0, Magnitude = 0; // > 0 seconds left; seconds to the next tick (0: none)
        uint64_t Sequence = 1;         // application order, strictly increasing within an entity
    };
    // Save v4: one projectile in flight, in its pool slot with its handle generation.
    struct SavedProjectile
    {
        uint32_t Slot = 0, Generation = 1, Team = 0, PierceLeft = 0, Phase = 0;
        std::string Key, Effect, Owner, Target; // Owner/Target: references as SavedEffect::Source
        glm::dvec3 Position{}, Velocity{}, Muzzle{};
        double Age = 0, Travelled = 0, Damage = 0, Attack = 0;
        std::vector<std::string> Hits; // up to 8 references (an empty one: a target that no longer resolves)
    };
    struct SavedCombat
    {
        std::string Signature; // CombatWorld::Signature() of the definitions the state was saved with
        std::vector<SavedProjectile> Projectiles;
    };
    // A save reference: a save key, "uuid:<digits>" or "".
    bool ValidSaveReference(const std::string&);
    struct SavedEntity
    {
        std::string Key, Signature, Target;
        bool Present = true, Active = true, HasActor = false, HasHealth = false, HasInteraction = false, HasAI = false;
        glm::vec3 Position{}, Rotation{}, Scale{ 1 };
        double Health = 0, Shield = 0, Grace = 0, Respawn = 0, Attack = 0, InteractionCooldown = 0;
        uint32_t Deaths = 0, Uses = 0;
        bool Dead = false;
        Bag Inventory;
        AISnapshot AI;
        QuestBookSave QuestBook;
        std::optional<LoadoutState> Loadout;
        std::vector<SavedEffect> Effects; // v4: at most 16, sorted by Sequence; present living health actors only
    };
    struct GameSave
    {
        std::string Scene, Schema;
        std::vector<SavedEntity> Entities;
        int Version = 1;
        std::optional<SavedCombat> Combat; // v4 only (required there)
    };
    bool ValidSaveKey(const std::string&);
    // Strict, bounded, deterministic v1/v2/v3/v4 codec. Failure never changes the caller's snapshot.
    std::string EncodeGameSave(const GameSave&);
    bool DecodeGameSave(std::string_view, GameSave&, std::string& error);
}
