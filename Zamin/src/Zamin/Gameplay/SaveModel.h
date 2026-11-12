#pragma once
#include "RulesModel.h"
#include "GameplayAI.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include <vector>
namespace Zamin::Gameplay {
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
    };
    struct GameSave
    {
        std::string Scene, Schema;
        std::vector<SavedEntity> Entities;
    };
    bool ValidSaveKey(const std::string&);
    // Strict, bounded, deterministic v1 codec. Failure never changes the caller's snapshot.
    std::string EncodeGameSave(const GameSave&);
    bool DecodeGameSave(std::string_view, GameSave&, std::string& error);
}
