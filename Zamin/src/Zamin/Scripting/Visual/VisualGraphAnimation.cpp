#include "VisualGraph.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Components.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace Zamin {
    Visual::Value DispatchAnimatorGraph(Entity target, const std::string& kind, const std::map<std::string, Visual::Value>& args)
    {
        using V = Visual::Value;
        auto* anim = target.TryGetComponent<AnimatorComponent>();
        if (!anim) throw std::runtime_error("Animator component required");
        auto text = [&](const char* key) { return args.at(key).get<std::string>(); };
        auto number = [&](const char* key) {double n=args.at(key).get<double>();if(!std::isfinite(n)||std::abs(n)>std::numeric_limits<float>::max())throw std::runtime_error("Animator float overflow");return float(n); };
        auto success = [](bool value) { return V{ { "Success", value } }; };
        auto parameter = [&](AnimatorParameter::Type type) -> AnimatorParameter* {auto* p=anim->FindParameter(text("Name"));if(!p||p->Kind!=type)throw std::runtime_error("Animator parameter missing or wrong type");return p; };
        if (kind == "Animator.GetFloat") return parameter(AnimatorParameter::Type::Float)->Value;
        if (kind == "Animator.GetBool") return parameter(AnimatorParameter::Type::Bool)->Value != 0;
        if (kind == "Animator.GetTrigger") return parameter(AnimatorParameter::Type::Trigger)->Value != 0;
        if (kind == "Animator.SetFloat") return success(anim->SetFloat(text("Name"), number("Value")));
        if (kind == "Animator.SetFloatDamped") return success(anim->SetFloatDamped(text("Name"), number("Value"), number("Damping"), number("Delta")));
        if (kind == "Animator.SetBool") return success(anim->SetBool(text("Name"), args.at("Value").get<bool>()));
        if (kind == "Animator.SetTrigger") return success(anim->SetTrigger(text("Name")));
        if (kind == "Animator.ResetTrigger") return success(anim->ResetTrigger(text("Name")));
        if (kind == "Animator.StateIs") return std::string(anim->GetCurrentStateName()) == text("Name");
        if (kind == "Animator.State")
        {
            auto* mesh = target.TryGetComponent<MeshRendererComponent>();
            const auto& velocity = anim->RootMotionVelocity;
            return V{ { "Name", anim->GetCurrentStateName() }, { "NormalizedTime", anim->GetNormalizedTime(mesh ? mesh->ModelAsset.get() : nullptr) }, { "Fading", anim->IsFading() }, { "FadeProgress", anim->GetFadeProgress() }, { "RootVelocity", V::array({ velocity.x, velocity.y, velocity.z }) }, { "TurnRate", anim->RootMotionTurnRate } };
        }
        if (kind == "Animator.BlendWeight")
        {
            const float index = number("Index");
            if (index < 0 || std::floor(index) != index || double(index) >= anim->BlendWeights.size()) throw std::runtime_error("Animator blend weight index outside range");
            return anim->BlendWeights[size_t(index)];
        }
        if (kind == "Animator.LayerWeight")
        {
            if (!anim->FindLayer(text("Name"))) throw std::runtime_error("Animator layer missing");
            return anim->GetLayerWeight(text("Name"));
        }
        const float fade = number("Fade");
        if (fade < 0) throw std::runtime_error("Animator fade must be nonnegative");
        if (kind == "Animator.SetState") return success(anim->SetState(text("Name"), fade));
        if (kind == "Animator.SetLayerWeight") return success(anim->SetLayerWeight(text("Name"), number("Weight"), fade));
        auto* mesh = target.TryGetComponent<MeshRendererComponent>();
        if (!mesh || !mesh->ModelAsset || mesh->ModelAsset->FindAnimation(text("Clip")) < 0) return success(false);
        if (kind == "Animator.PlayClip")
        {
            // Direct clip mode explicitly leaves the state machine; states remain authored for later SetState.
            anim->CurrentState = -1;
            anim->StateMachinePaused = true;
            anim->Play(text("Clip"), args.at("Loop").get<bool>(), number("Speed"), fade);
            return success(true);
        }
        if (kind == "Animator.PlayLayer") return success(anim->PlayLayer(text("Name"), text("Clip"), args.at("Loop").get<bool>(), number("Speed"), fade));
        throw std::runtime_error("Unsupported Animator operation");
    }
}
