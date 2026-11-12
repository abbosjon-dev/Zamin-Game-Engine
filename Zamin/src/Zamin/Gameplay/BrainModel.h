#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
namespace Zamin::Gameplay {
    enum class BrainMode
    {
        Idle,
        Patrol,
        Chase,
        Investigate,
        Attack,
        Return,
        Blocked,
        Dead,
        Disabled
    };
    const char* BrainName(BrainMode);
    struct BrainSettings
    {
        float View = 10, Lose = 14, Near = 2, FieldOfView = 180, Memory = 2, Leash = 20;
        float AttackEnter = 1.5f, AttackExit = 1.8f, Damage = 10, AttackCooldown = .5f;
        float Wait = .5f, Arrive = .25f, Repath = .3f, Speed = 3;
        bool TwoD = false, PingPong = true;
        bool Validate(std::string&) const;
    };
    struct BrainMemory
    {
        BrainMode Mode = BrainMode::Idle;
        float Remember = 0, WaitLeft = 0;
        uint32_t Waypoint = 0, Arrivals = 0, Returns = 0;
        int Direction = 1;
        bool Waiting = false;
    };
    struct BrainInput
    {
        float Delta = 0, HomeDistance = 0, TargetDistance = 0, TargetHomeDistance = 0, GoalDistance = 0;
        uint32_t Waypoints = 0;
        bool Enabled = true, Alive = true, Target = false, Visible = false, Blocked = false;
    };
    struct BrainDecision
    {
        bool Move = false, Attack = false, Track = false;
        BrainMode Mode = BrainMode::Idle;
    };
    // Does not move entities or read a clock; invalid input leaves memory and output untouched.
    bool StepBrain(const BrainSettings&, BrainMemory&, const BrainInput&, BrainDecision&, std::string&);
}
