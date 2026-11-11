#include "Components.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace Zamin {
    void AnimatorComponent::EnterState(int index, float fade)
    {
        if (index < 0 || index >= int(States.size()) || !std::isfinite(fade)) return;
        const auto& state = States[size_t(index)];
        BeginCrossfade(fade);
        PreviousState = CurrentState;
        CurrentState = index;
        StateMachinePaused = false;
        BlendTree = state.IsBlendTree() ? index : -1;
        if (!state.IsBlendTree()) Clip = state.Clip;
        Loop = state.Loop;
        Speed = state.Speed;
        Playing = true;
        Time = 0;
        EventsAtStart = true;
    }

    bool AnimatorComponent::SetFloat(const std::string& name, float value)
    {
        auto* p = FindParameter(name);
        if (!p || p->Kind != AnimatorParameter::Type::Float || !std::isfinite(value)) return false;
        p->Value = value;
        return true;
    }
    bool AnimatorComponent::SetBool(const std::string& name, bool value)
    {
        auto* p = FindParameter(name);
        if (!p || p->Kind != AnimatorParameter::Type::Bool) return false;
        p->Value = value ? 1.0f : 0.0f;
        return true;
    }
    bool AnimatorComponent::SetTrigger(const std::string& name)
    {
        auto* p = FindParameter(name);
        if (!p || p->Kind != AnimatorParameter::Type::Trigger) return false;
        p->Value = 1.0f;
        return true;
    }
    bool AnimatorComponent::ResetTrigger(const std::string& name)
    {
        auto* p = FindParameter(name);
        if (!p || p->Kind != AnimatorParameter::Type::Trigger) return false;
        p->Value = 0.0f;
        return true;
    }
    bool AnimatorComponent::SetFloatDamped(const std::string& name, float target, float seconds, float delta)
    {
        auto* p = FindParameter(name);
        if (!p || p->Kind != AnimatorParameter::Type::Float || !std::isfinite(target) || !std::isfinite(p->Value) || !std::isfinite(seconds) || !std::isfinite(delta) || seconds < 0 || delta < 0) return false;
        const double alpha = seconds == 0 ? 1.0 : -std::expm1(-double(delta) / seconds);
        return SetFloat(name, float(double(p->Value) * (1.0 - alpha) + double(target) * alpha));
    }
    int AnimatorComponent::SelectTransition(float time) const
    {
        int best = -1;
        for (size_t i = 0; i < Transitions.size(); ++i)
        {
            const auto& t = Transitions[i];
            if (t.From < -1 || t.To < 0 || t.To >= int(States.size()) || (t.From >= 0 && t.From != CurrentState) || (t.From < 0 && t.To == CurrentState)) continue;
            if (!std::isfinite(t.Fade) || t.Fade < 0 || (!t.InterruptDuringFade && IsFading())) continue;
            if (t.HasExitTime && (!std::isfinite(time) || !std::isfinite(t.ExitTime) || t.ExitTime < 0 || time < t.ExitTime)) continue;
            bool valid = true;
            for (const auto& condition : t.Conditions)
            {
                const AnimatorParameter* p = nullptr;
                for (const auto& candidate : Parameters)
                    if (candidate.Name == condition.Parameter)
                    {
                        p = &candidate;
                        break;
                    }
                if (!p || !std::isfinite(p->Value))
                {
                    valid = false;
                    break;
                }
                switch (condition.Mode)
                {
                    case AnimatorCondition::Op::Greater:   valid = p->Kind == AnimatorParameter::Type::Float && std::isfinite(condition.Threshold) && p->Value > condition.Threshold; break;
                    case AnimatorCondition::Op::Less:      valid = p->Kind == AnimatorParameter::Type::Float && std::isfinite(condition.Threshold) && p->Value < condition.Threshold; break;
                    case AnimatorCondition::Op::IsTrue:    valid = p->Kind == AnimatorParameter::Type::Bool && p->Value != 0; break;
                    case AnimatorCondition::Op::IsFalse:   valid = p->Kind == AnimatorParameter::Type::Bool && p->Value == 0; break;
                    case AnimatorCondition::Op::Triggered: valid = p->Kind == AnimatorParameter::Type::Trigger && p->Value != 0; break;
                    default:                               valid = false; break;
                }
                if (!valid) break;
            }
            if (valid && (best < 0 || t.Priority > Transitions[size_t(best)].Priority)) best = int(i);
        }
        return best;
    }
    void AnimatorComponent::ConsumeTransition(int index)
    {
        if (index < 0 || index >= int(Transitions.size())) return;
        for (const auto& c : Transitions[size_t(index)].Conditions)
            if (c.Mode == AnimatorCondition::Op::Triggered) ResetTrigger(c.Parameter);
    }
    void AnimatorComponent::ComputeStateWeights(const AnimatorState& state, std::vector<float>& weights) const
    {
        auto value = [&](const std::string& name) {for(const auto& p:Parameters)if(p.Name==name && p.Kind==AnimatorParameter::Type::Float && std::isfinite(p.Value))return p.Value;return 0.0f; };
        if (state.Motions.size() > 64)
        {
            weights.assign(state.Motions.size(), 0);
            return;
        }
        if (state.Type == AnimatorStateType::BlendTree2D)
        {
            std::vector<glm::vec2> positions;
            positions.reserve(state.Motions.size());
            for (const auto& motion : state.Motions) positions.push_back(motion.Position);
            ComputeBlend2DWeights(positions, { value(state.BlendParameter), value(state.BlendParameterY) }, weights);
        }
        else
        {
            std::vector<float> thresholds;
            thresholds.reserve(state.Motions.size());
            for (const auto& motion : state.Motions) thresholds.push_back(motion.Threshold);
            ComputeBlend1DWeights(thresholds, value(state.BlendParameter), weights);
        }
    }
    std::vector<std::string> AnimatorComponent::ValidateController(const Model* model) const
    {
        std::vector<std::string> errors;
        auto add = [&](std::string error) {if(errors.size()<128)errors.push_back(std::move(error)); };
        std::set<std::string> names;
        if (Parameters.size() > 128 || States.size() > 256 || Transitions.size() > 1024) add("Controller limit: 128 parameters, 256 states, 1024 transitions");
        for (const auto& p : Parameters)
            if (p.Name.empty() || p.Name.size() > 128 || !names.insert(p.Name).second || !std::isfinite(p.Value) || int(p.Kind) < 0 || int(p.Kind) > 2) add("Invalid/duplicate parameter: " + p.Name);
        auto parameter = [&](const std::string& name, AnimatorParameter::Type type) {for(const auto& p:Parameters)if(p.Name==name)return p.Kind==type;return false; };
        names.clear();
        if (!States.empty() && (DefaultState < 0 || DefaultState >= int(States.size()))) add("Default state index is invalid");
        for (const auto& state : States)
        {
            if (state.Name.empty() || state.Name.size() > 128 || !names.insert(state.Name).second) add("Invalid/duplicate state: " + state.Name);
            if (!std::isfinite(state.Speed) || !std::isfinite(state.Duration) || state.Duration < 0 || int(state.Type) > 2) add("Invalid state timing/type: " + state.Name);
            if (!state.SpeedParameter.empty() && !parameter(state.SpeedParameter, AnimatorParameter::Type::Float)) add("Missing float speed parameter: " + state.Name);
            if (state.IsBlendTree())
            {
                if (state.Motions.empty() || state.Motions.size() > 64) add("Blend tree needs 1..64 motions: " + state.Name);
                if (!parameter(state.BlendParameter, AnimatorParameter::Type::Float)) add("Missing float X parameter: " + state.Name);
                if (state.Type == AnimatorStateType::BlendTree2D && !parameter(state.BlendParameterY, AnimatorParameter::Type::Float)) add("Missing float Y parameter: " + state.Name);
                for (size_t i = 0; i < state.Motions.size(); ++i)
                {
                    const auto& m = state.Motions[i];
                    if (!std::isfinite(m.Speed) || !std::isfinite(m.Threshold) || !std::isfinite(m.Position.x) || !std::isfinite(m.Position.y)) add("Invalid blend sample: " + state.Name);
                    if (model && model->FindAnimation(m.Clip) < 0) add("Missing motion clip: " + m.Clip);
                    for (size_t j = 0; j < i; ++j)
                        if (state.Type == AnimatorStateType::BlendTree2D ? m.Position == state.Motions[j].Position : m.Threshold == state.Motions[j].Threshold) add("Duplicate blend sample: " + state.Name);
                }
            }
            else if (model && !state.Clip.empty() && model->FindAnimation(state.Clip) < 0) add("Missing state clip: " + state.Clip);
        }
        for (const auto& t : Transitions)
        {
            if (t.From < -1 || t.From >= int(States.size()) || t.To < 0 || t.To >= int(States.size()) || !std::isfinite(t.Fade) || t.Fade < 0 || !std::isfinite(t.ExitTime) || t.ExitTime < 0) add("Invalid transition index/timing");
            for (const auto& c : t.Conditions)
            {
                const auto expected = (c.Mode == AnimatorCondition::Op::Greater || c.Mode == AnimatorCondition::Op::Less) ? AnimatorParameter::Type::Float : c.Mode == AnimatorCondition::Op::Triggered ? AnimatorParameter::Type::Trigger
                                                                                                                                                                                                        : AnimatorParameter::Type::Bool;
                if (int(c.Mode) < 0 || int(c.Mode) > 4 || !parameter(c.Parameter, expected) || !std::isfinite(c.Threshold)) add("Invalid condition parameter/type: " + c.Parameter);
            }
        }
        return errors;
    }
}
