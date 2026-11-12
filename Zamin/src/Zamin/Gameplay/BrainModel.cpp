#include "BrainModel.h"
#include <algorithm>
#include <cmath>
namespace Zamin::Gameplay {
    const char* BrainName(BrainMode mode)
    {
        switch (mode)
        {
            case BrainMode::Patrol:      return "Patrol";
            case BrainMode::Chase:       return "Chase";
            case BrainMode::Investigate: return "Investigate";
            case BrainMode::Attack:      return "Attack";
            case BrainMode::Return:      return "Return";
            case BrainMode::Blocked:     return "Blocked";
            case BrainMode::Dead:        return "Dead";
            case BrainMode::Disabled:    return "Disabled";
            default:                     return "Idle";
        }
    }
    bool BrainSettings::Validate(std::string& error) const
    {
        error.clear();
        for (float v : { View, Lose, Near, FieldOfView, Memory, Leash, AttackEnter, AttackExit, Damage, AttackCooldown, Wait, Arrive, Repath, Speed })
            if (!std::isfinite(v))
            {
                error = "AI settings must be finite";
                return false;
            }
        if (View <= 0 || View > 100 || Lose < View || Lose > 100 || Near < 0 || Near > View || FieldOfView <= 0 || FieldOfView > 360 || Memory < 0 || Memory > 60 || Leash < Lose || Leash > 1000 ||
            AttackEnter < .05f || AttackEnter > View || AttackExit < AttackEnter || AttackExit > Lose || Damage <= 0 || Damage > 1e6 || AttackCooldown < .05f || AttackCooldown > 60 ||
            Wait < 0 || Wait > 60 || Arrive < .01f || Arrive > 5 || Repath < .05f || Repath > 10 || Speed < .01f || Speed > 50)
        {
            error = "AI settings outside supported ranges";
            return false;
        }
        return true;
    }
    bool StepBrain(const BrainSettings& s, BrainMemory& state, const BrainInput& in, BrainDecision& out, std::string& error)
    {
        if (!s.Validate(error)) return false;
        for (float n : { in.Delta, in.HomeDistance, in.TargetDistance, in.TargetHomeDistance, in.GoalDistance })
            if (!std::isfinite(n) || n < 0)
            {
                error = "Invalid AI input";
                return false;
            }
        if (in.Waypoints > 64)
        {
            error = "AI waypoint limit 64";
            return false;
        }
        auto next = state;
        BrainDecision result;
        auto set = [&](BrainMode mode, bool move = false, bool attack = false, bool track = false) {next.Mode=result.Mode=mode;result.Move=move;result.Attack=attack;result.Track=track; };
        if (!in.Enabled)
        {
            next.Remember = 0;
            set(BrainMode::Disabled);
        }
        else if (!in.Alive)
        {
            next.Remember = 0;
            next.Waiting = false;
            next.WaitLeft = 0;
            set(BrainMode::Dead);
        }
        else if (state.Mode == BrainMode::Dead)
        {
            next.Remember = 0;
            next.Waiting = false;
            next.WaitLeft = 0;
            next.Waypoint = 0;
            next.Direction = 1;
            set(BrainMode::Return, in.HomeDistance > s.Arrive);
        }
        else if (state.Mode == BrainMode::Return)
        {
            next.Remember = 0;
            if (in.HomeDistance > s.Arrive) set(BrainMode::Return, true);
            else
            {
                if (next.Returns != UINT32_MAX) ++next.Returns;
                next.Waiting = false;
                next.WaitLeft = 0;
                set(in.Waypoints ? BrainMode::Patrol : BrainMode::Idle, in.Waypoints > 0);
            }
        }
        else if (state.Mode == BrainMode::Blocked || in.Blocked) set(BrainMode::Blocked);
        else if (in.HomeDistance > s.Leash || (in.Target && in.TargetHomeDistance > s.Leash))
        {
            next.Remember = 0;
            next.Waiting = false;
            set(BrainMode::Return, true);
        }
        else
        {
            next.Remember = in.Visible ? s.Memory : std::max(0.f, next.Remember - in.Delta);
            if (in.Target && in.Visible)
            {
                next.Waiting = false;
                const bool close = in.TargetDistance <= (state.Mode == BrainMode::Attack ? s.AttackExit : s.AttackEnter);
                set(close ? BrainMode::Attack : BrainMode::Chase, !close, close, true);
            }
            else if (in.Target && next.Remember > 0) set(BrainMode::Investigate, in.GoalDistance > s.Arrive, false, true);
            else if (state.Mode == BrainMode::Chase || state.Mode == BrainMode::Attack || state.Mode == BrainMode::Investigate)
            {
                next.Remember = 0;
                set(BrainMode::Return, true);
            }
            else if (!in.Waypoints) set(BrainMode::Idle);
            else
            {
                if (next.Waypoint >= in.Waypoints) next.Waypoint = 0;
                if (in.GoalDistance <= s.Arrive && !next.Waiting)
                {
                    next.Waiting = true;
                    next.WaitLeft = s.Wait;
                    if (next.Arrivals != UINT32_MAX) ++next.Arrivals;
                }
                if (next.Waiting)
                {
                    next.WaitLeft = std::max(0.f, next.WaitLeft - in.Delta);
                    if (next.WaitLeft == 0)
                    {
                        next.Waiting = false;
                        if (in.Waypoints == 1) next.Waiting = true;
                        if (in.Waypoints > 1)
                        {
                            if (s.PingPong)
                            {
                                if (next.Waypoint == in.Waypoints - 1) next.Direction = -1;
                                else if (next.Waypoint == 0) next.Direction = 1;
                                next.Waypoint = uint32_t(int(next.Waypoint) + next.Direction);
                            }
                            else next.Waypoint = (next.Waypoint + 1) % in.Waypoints;
                        }
                    }
                }
                set(BrainMode::Patrol, !next.Waiting);
            }
        }
        state = next;
        out = result;
        return true;
    }
}
