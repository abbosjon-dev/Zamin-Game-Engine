#include "TestAssertions.h"
#include <Zamin/Scene/Components.h>
#include <algorithm>
#include <limits>
#include <numeric>
using namespace Zamin;
namespace {
    AnimatorComponent Controller()
    {
        AnimatorComponent c;
        c.Parameters = { { "X", AnimatorParameter::Type::Float, 0 }, { "Y", AnimatorParameter::Type::Float, 0 }, { "Ready", AnimatorParameter::Type::Bool, 0 }, { "Jump", AnimatorParameter::Type::Trigger, 0 }, { "Hit", AnimatorParameter::Type::Trigger, 0 } };
        c.States = { { "Idle", "Idle" }, { "Move", "Walk" }, { "Jump", "Jump", 1, false } };
        c.CurrentState = 0;
        c.Transitions = { { 0, 1, 0, false, 1, { { "X", AnimatorCondition::Op::Greater, .1f } } }, { -1, 2, .2f, false, 1, { { "Jump", AnimatorCondition::Op::Triggered } } } };
        return c;
    }
    std::vector<glm::vec2> Cross() { return { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }; }
}
TEST(Animator2DWeightsResolveExactSamples)
{
    auto points = Cross();
    std::vector<float> weights;
    for (size_t i = 0; i < points.size(); ++i)
    {
        ComputeBlend2DWeights(points, points[i], weights);
        CHECK_NEAR(weights[i], 1, 1e-6f);
        CHECK_NEAR(std::accumulate(weights.begin(), weights.end(), 0.0f), 1, 1e-6f);
    }
}
TEST(Animator2DWeightsInterpolateAxisAndClampOutside)
{
    std::vector<float> w;
    ComputeBlend2DWeights({ { 0, 0 }, { 1, 0 } }, { .25f, 0 }, w);
    CHECK_NEAR(w[0], .75f, 1e-6f);
    CHECK_NEAR(w[1], .25f, 1e-6f);
    ComputeBlend2DWeights({ { 0, 0 }, { 1, 0 } }, { 100, 10 }, w);
    CHECK(w[0] == 0 && w[1] == 1);
    ComputeBlend2DWeights({ { 0, 0 }, { 1, 0 } }, { -100, 10 }, w);
    CHECK(w[0] == 1 && w[1] == 0);
}
TEST(Animator2DWeightsHaveSymmetryAndPartitionUnity)
{
    auto p = Cross();
    std::vector<float> w;
    for (int x = -20; x <= 20; ++x)
        for (int y = -20; y <= 20; ++y)
        {
            ComputeBlend2DWeights(p, { x * .1f, y * .1f }, w);
            CHECK_NEAR(std::accumulate(w.begin(), w.end(), 0.0f), 1, 1e-5f);
            for (float value : w) CHECK(std::isfinite(value) && value >= 0 && value <= 1);
        }
    ComputeBlend2DWeights(p, { .5f, .5f }, w);
    CHECK_NEAR(w[1], w[3], 1e-6f);
    CHECK(w[2] == 0 && w[4] == 0);
}
TEST(Animator2DWeightsIgnoreDuplicateAndInvalidSamples)
{
    std::vector<float> w;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ComputeBlend2DWeights({ { 0, 0 }, { 0, 0 }, { nan, 1 }, { 1, 0 } }, { .5f, 0 }, w);
    CHECK_NEAR(w[0], .5f, 1e-6f);
    CHECK(w[1] == 0 && w[2] == 0);
    CHECK_NEAR(w[3], .5f, 1e-6f);
    ComputeBlend2DWeights({ { 0, 0 }, { 1, 0 } }, { nan, 1 }, w);
    CHECK(w[0] == 1);
    ComputeBlend2DWeights({ { nan, nan } }, {}, w);
    CHECK(w.size() == 1 && w[0] == 0);
}
TEST(Animator2DWeightsHandleEmptySingletonAndBudget)
{
    std::vector<float> w;
    ComputeBlend2DWeights({}, {}, w);
    CHECK(w.empty());
    ComputeBlend2DWeights({ { 2, 3 } }, { 50, 0 }, w);
    CHECK(w.size() == 1 && w[0] == 1);
    ComputeBlend2DWeights(std::vector<glm::vec2>(65), {}, w);
    CHECK(w.size() == 65 && std::accumulate(w.begin(), w.end(), 0.0f) == 0);
}
TEST(Animator2DWeightsAreStableUnderTranslationAndScale)
{
    auto p = Cross();
    std::vector<float> a, b;
    ComputeBlend2DWeights(p, { .25f, .75f }, a);
    for (auto& point : p) point = point * 10.0f + glm::vec2(100, 200);
    ComputeBlend2DWeights(p, { 102.5f, 207.5f }, b);
    for (size_t i = 0; i < a.size(); ++i) CHECK_NEAR(a[i], b[i], 1e-6f);
}
TEST(AnimatorParametersEnforceTypesAndFiniteValues)
{
    auto c = Controller();
    CHECK(c.SetFloat("X", 3));
    CHECK(c.SetBool("Ready", true));
    CHECK(c.SetTrigger("Jump"));
    CHECK(!c.SetFloat("Ready", 3));
    CHECK(!c.SetBool("X", true));
    CHECK(!c.SetTrigger("Ready"));
    CHECK(!c.SetFloat("Missing", 1));
    CHECK(!c.SetFloat("X", std::numeric_limits<float>::infinity()));
    CHECK(c.GetFloat("X") == 3);
    CHECK(c.ResetTrigger("Jump"));
    CHECK(!c.ResetTrigger("Ready"));
}
TEST(AnimatorDampingIsFramePartitionInvariant)
{
    auto a = Controller(), b = a;
    CHECK(a.SetFloatDamped("X", 10, .2f, .5f));
    for (int i = 0; i < 50; ++i) CHECK(b.SetFloatDamped("X", 10, .2f, .01f));
    CHECK_NEAR(a.GetFloat("X"), b.GetFloat("X"), 1e-5f);
    CHECK(a.GetFloat("X") > 0 && a.GetFloat("X") < 10);
}
TEST(AnimatorDampingRejectsInvalidInputAndPreservesValue)
{
    auto c = Controller();
    c.SetFloat("X", 2);
    CHECK(!c.SetFloatDamped("X", 5, -1, .1f));
    CHECK(!c.SetFloatDamped("X", 5, 1, -.1f));
    CHECK(!c.SetFloatDamped("Ready", 5, 1, .1f));
    CHECK(!c.SetFloatDamped("X", std::numeric_limits<float>::quiet_NaN(), 1, .1f));
    CHECK(c.GetFloat("X") == 2);
    CHECK(c.SetFloatDamped("X", 5, 1, 0));
    CHECK(c.GetFloat("X") == 2);
    CHECK(c.SetFloatDamped("X", 5, 0, .1f));
    CHECK(c.GetFloat("X") == 5);
}
TEST(AnimatorPriorityWinsAndTiesKeepAuthorOrder)
{
    auto c = Controller();
    c.SetFloat("X", 1);
    c.SetTrigger("Jump");
    CHECK(c.SelectTransition(0) == 0);
    c.Transitions[1].Priority = 10;
    CHECK(c.SelectTransition(0) == 1);
    CHECK(c.GetBool("Jump"));
    c.ConsumeTransition(1);
    CHECK(!c.GetBool("Jump"));
    CHECK(c.SelectTransition(0) == 0);
}
TEST(AnimatorOnlyWinningTransitionConsumesItsTriggers)
{
    auto c = Controller();
    c.SetTrigger("Jump");
    c.SetTrigger("Hit");
    c.Transitions[0].Conditions = { { "Hit", AnimatorCondition::Op::Triggered } };
    c.Transitions[1].Priority = 2;
    const int index = c.SelectTransition(0);
    CHECK(index == 1);
    c.ConsumeTransition(index);
    CHECK(!c.GetBool("Jump") && c.GetBool("Hit"));
}
TEST(AnimatorFadeInterruptionPolicyKeepsTriggerPending)
{
    auto c = Controller();
    c.SetTrigger("Jump");
    c.FadeDuration = 1;
    c.FadeElapsed = .2f;
    c.Transitions[1].InterruptDuringFade = false;
    CHECK(c.SelectTransition(0) == -1);
    CHECK(c.GetBool("Jump"));
    c.FadeElapsed = 1;
    CHECK(c.SelectTransition(0) == 1);
    c.FadeElapsed = 0;
    c.Transitions[1].InterruptDuringFade = true;
    CHECK(c.SelectTransition(0) == 1);
}
TEST(AnimatorMissingOrMistypedConditionsNeverPass)
{
    auto c = Controller();
    c.Transitions.resize(1);
    auto& condition = c.Transitions[0].Conditions[0];
    condition = { "Missing", AnimatorCondition::Op::IsFalse };
    CHECK(c.SelectTransition(0) == -1);
    condition = { "X", AnimatorCondition::Op::IsFalse };
    CHECK(c.SelectTransition(0) == -1);
    condition = { "Ready", AnimatorCondition::Op::Greater, -1 };
    CHECK(c.SelectTransition(0) == -1);
    condition = { "Ready", AnimatorCondition::Op::Triggered };
    c.SetBool("Ready", true);
    CHECK(c.SelectTransition(0) == -1);
}
TEST(AnimatorExitTimeAndInvalidTransitionDoNotConsumeTriggers)
{
    auto c = Controller();
    c.SetTrigger("Jump");
    c.Transitions[1].HasExitTime = true;
    c.Transitions[1].ExitTime = 1;
    CHECK(c.SelectTransition(.9f) == -1 && c.GetBool("Jump"));
    CHECK(c.SelectTransition(1) == 1);
    c.Transitions[1].To = 99;
    CHECK(c.SelectTransition(1) == -1);
    c.Transitions[1].To = 2;
    c.Transitions[1].Fade = -1;
    CHECK(c.SelectTransition(1) == -1);
}
TEST(AnimatorStateEntryRestartsSameClipAndSelf)
{
    auto c = Controller();
    c.States[1].Clip = c.States[0].Clip;
    c.Clip = c.States[0].Clip;
    c.Time = .75f;
    c.EnterState(1, .2f);
    CHECK(c.CurrentState == 1 && c.Time == 0 && c.PreviousTime == .75f && c.PreviousState == 0);
    CHECK(c.IsFading() && c.EventsAtStart);
    c.Time = .3f;
    c.EnterState(1, 0);
    CHECK(c.Time == 0 && c.PreviousState == 1 && !c.IsFading());
}
TEST(AnimatorModelFreeClockAndDirectModeResume)
{
    auto c = Controller();
    c.States[0].Duration = 2;
    c.Time = .5f;
    CHECK_NEAR(c.GetNormalizedTime(nullptr), .25f, 1e-6f);
    c.StateMachinePaused = true;
    CHECK(!c.UsesStateMachine());
    CHECK(c.SetState("Move", 0));
    CHECK(c.UsesStateMachine() && c.CurrentState == 1);
    c.StateMachinePaused = true;
    c.ResetRuntime();
    CHECK(!c.StateMachinePaused);
}
TEST(Animator2DStateWeightsUseBothFloatAxes)
{
    auto c = Controller();
    AnimatorState state;
    state.Type = AnimatorStateType::BlendTree2D;
    state.BlendParameter = "X";
    state.BlendParameterY = "Y";
    for (auto position : Cross())
    {
        AnimatorBlendMotion motion;
        motion.Position = position;
        state.Motions.push_back(motion);
    }
    c.SetFloat("Y", .5f);
    std::vector<float> w;
    c.ComputeStateWeights(state, w);
    CHECK_NEAR(w[0], .5f, 1e-6f);
    CHECK_NEAR(w[3], .5f, 1e-6f);
    CHECK(w[1] == 0 && w[2] == 0 && w[4] == 0);
}
TEST(AnimatorControllerValidationReportsNamesTypesAndReferences)
{
    auto c = Controller();
    CHECK(c.ValidateController().empty());
    c.Parameters.push_back(c.Parameters[0]);
    CHECK(!c.ValidateController().empty());
    c.Parameters.pop_back();
    c.States[1].Name = c.States[0].Name;
    CHECK(!c.ValidateController().empty());
    c.States[1].Name = "Move";
    c.DefaultState = 99;
    CHECK(!c.ValidateController().empty());
    c.DefaultState = 0;
    c.Transitions[0].Conditions[0].Parameter = "Ready";
    CHECK(!c.ValidateController().empty());
}
TEST(AnimatorControllerValidationReportsInvalidBlendSamples)
{
    auto c = Controller();
    auto& state = c.States[0];
    state.Type = AnimatorStateType::BlendTree2D;
    state.BlendParameter = "X";
    state.BlendParameterY = "Y";
    state.Motions = { { "A", 0, 1, { 0, 0 } }, { "B", 0, 1, { 1, 0 } } };
    CHECK(c.ValidateController().empty());
    state.Motions[1].Position = { 0, 0 };
    CHECK(!c.ValidateController().empty());
    state.Motions[1].Position = { 1, 0 };
    state.BlendParameterY = "Ready";
    CHECK(!c.ValidateController().empty());
}
TEST(AnimatorControllerValidationAndBlendBudgetsAreBounded)
{
    auto c = Controller();
    auto& state = c.States[0];
    state.Type = AnimatorStateType::BlendTree2D;
    state.BlendParameter = "X";
    state.BlendParameterY = "Y";
    state.Motions.resize(65);
    CHECK(!c.ValidateController().empty());
    std::vector<float> w;
    c.ComputeStateWeights(state, w);
    CHECK(std::accumulate(w.begin(), w.end(), 0.0f) == 0);
    c.States.resize(260);
    CHECK(c.ValidateController().size() <= 128);
}
TEST(AnimatorBlendEntryPreservesLegacyClipField)
{
    auto c = Controller();
    c.Clip = "Idle";
    c.Time = .5f;
    c.States[1].Type = AnimatorStateType::BlendTree2D;
    c.EnterState(1, .2f);
    CHECK(c.Clip == "Idle" && c.PreviousClip == "Idle" && c.BlendTree == 1 && c.Time == 0);
    c.EnterState(2, 0);
    CHECK(c.Clip == "Jump" && c.BlendTree == -1);
}
