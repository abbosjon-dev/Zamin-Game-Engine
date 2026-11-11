#include "TestAssertions.h"
#include <Zamin/Scene/Scene.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <filesystem>
using namespace Zamin;
namespace {
    Ref<Model> BlendModel()
    {
        Skeleton skeleton;
        skeleton.Joints.push_back({ "Root" });
        std::vector<AnimationClip> clips;
        for (auto [name, height, duration] : std::vector<std::tuple<std::string, float, float>>{ { "Idle", 0, 1 }, { "Walk", 2, 1 }, { "Run", 4, .5f } })
        {
            AnimationClip clip;
            clip.Name = name;
            clip.Duration = duration;
            AnimationChannel channel;
            channel.JointIndex = 0;
            channel.Times = { 0, duration };
            channel.Values = { { 0, 0, 0, 0 }, { height, 0, 0, 0 } };
            clip.Channels.push_back(channel);
            clips.push_back(clip);
        }
        return Model::CreateSkeletal(std::move(skeleton), std::move(clips));
    }
    AnimatorComponent BlendController()
    {
        AnimatorComponent c;
        c.Parameters = { { "X", AnimatorParameter::Type::Float, .5f }, { "Y", AnimatorParameter::Type::Float, 0 } };
        AnimatorState state{ "Locomotion" };
        state.Type = AnimatorStateType::BlendTree2D;
        state.BlendParameter = "X";
        state.BlendParameterY = "Y";
        state.Motions = { { "Idle", 0, 1, { 0, 0 } }, { "Walk", 0, 1, { 1, 0 } }, { "Run", 0, 1, { 0, 1 } } };
        c.States = { state, { "Action", "Run", 1, false } };
        return c;
    }
    struct GraphFixture
    {
        Ref<Project> Prior = Project::GetActive();
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "zamin-animator-graph-test";
        GraphFixture()
        {
            std::filesystem::create_directories(Root / "Assets/Scripts");
            Project::New();
            CHECK(Project::SaveActive(Root / "Test.zproject"));
        }
        ~GraphFixture()
        {
            Project::SetActive(Prior);
            std::filesystem::remove_all(Root);
        }
        void Attach(Entity actor, const Visual::Graph& graph)
        {
            CHECK(graph.Save(Root / "Assets/Scripts/Controller.zgraph"));
            auto& script = actor.AddComponent<NativeScriptComponent>();
            script.ClassName = "Zamin.VisualGraph";
            script.Fields["Graph"].Set(std::string("Scripts/Controller.zgraph"));
        }
        VisualGraphHost* Host(Entity actor) { return dynamic_cast<VisualGraphHost*>(actor.GetComponent<NativeScriptComponent>().Instance); }
    };
}
TEST(Animator2DRuntimeBlendsSkeletalPosesAndKeepsPhase)
{
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("Actor");
    auto& mesh = actor.AddComponent<MeshRendererComponent>();
    mesh.ModelAsset = BlendModel();
    mesh.Type = PrimitiveMesh::Model;
    auto& c = actor.AddComponent<AnimatorComponent>();
    c = BlendController();
    scene->OnRuntimeStart();
    scene->UpdateRuntime(.25f);
    CHECK(c.CurrentState == 0 && c.BlendTree == 0 && c.BlendWeights.size() == 3);
    CHECK_NEAR(c.Time, .25f, 1e-5f);
    CHECK_NEAR(c.JointMatrices[0][3].x, .25f, 1e-5f);
    c.SetFloat("X", 0);
    c.SetFloat("Y", 1);
    scene->UpdateRuntime(.125f);
    CHECK_NEAR(c.Time, .5f, 1e-5f);
    CHECK_NEAR(c.JointMatrices[0][3].x, 2, 1e-5f);
    scene->OnRuntimeStop();
}
TEST(Animator2DRuntimeRootMotionUsesWeightedClipDelta)
{
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("Actor");
    auto& mesh = actor.AddComponent<MeshRendererComponent>();
    mesh.ModelAsset = BlendModel();
    mesh.Type = PrimitiveMesh::Model;
    auto& c = actor.AddComponent<AnimatorComponent>();
    c = BlendController();
    c.ApplyRootMotion = true;
    scene->OnRuntimeStart();
    scene->UpdateRuntime(.1f);
    CHECK_NEAR(actor.GetTransform().Translation.x, .1f, 1e-4f);
    CHECK_NEAR(c.JointMatrices[0][3].x, 0, 1e-4f);
    scene->OnRuntimeStop();
}
TEST(AnimatorModelFreeStatesRespectDurationAndResetSharedClipClock)
{
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("2D actor");
    auto& c = actor.AddComponent<AnimatorComponent>();
    c.States = { { "Idle" }, { "Action" } };
    c.States[0].Duration = .5f;
    c.States[1].Duration = .25f;
    c.States[1].Loop = false;
    c.Transitions = { { 0, 1, 0, true, 1, {} } };
    scene->OnRuntimeStart();
    scene->UpdateRuntime(.25f);
    CHECK(c.CurrentState == 0);
    scene->UpdateRuntime(.25f);
    CHECK(c.CurrentState == 0);
    scene->UpdateRuntime(.1f);
    CHECK(c.CurrentState == 1);
    CHECK_NEAR(c.Time, .1f, 1e-5f);
    scene->UpdateRuntime(.2f);
    CHECK(!c.Playing);
    CHECK_NEAR(c.Time, .25f, 1e-5f);
    scene->OnRuntimeStop();
}
TEST(AnimatorModelFreeBlendTreeHasARealNormalizedClock)
{
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("2D blend controller");
    auto& c = actor.AddComponent<AnimatorComponent>();
    c = BlendController();
    c.States[0].Duration = 2;
    scene->OnRuntimeStart();
    scene->UpdateRuntime(.5f);
    CHECK_NEAR(c.Time, .25f, 1e-6f);
    CHECK(c.BlendWeights.size() == 3);
    scene->OnRuntimeStop();
}
TEST(AnimatorControllerSerializationKeeps2DAndTransitionPolicy)
{
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("Actor");
    auto& c = actor.AddComponent<AnimatorComponent>();
    c = BlendController();
    c.States[0].Duration = 2;
    c.Transitions = { { 0, 1, .3f, true, .75f, { { "X", AnimatorCondition::Op::Greater, .5f } }, 8, false } };
    c.StateMachinePaused = true;
    auto copy = CreateRef<Scene>();
    CHECK(SceneSerializer(copy).DeserializeFromString(SceneSerializer(scene).SerializeToString()));
    auto loadedActor = copy->FindEntityByName("Actor");
    const auto& loaded = loadedActor.GetComponent<AnimatorComponent>();
    CHECK(loaded.States[0].Type == AnimatorStateType::BlendTree2D && loaded.States[0].BlendParameterY == "Y");
    CHECK(loaded.States[0].Motions[2].Position == glm::vec2(0, 1));
    CHECK(loaded.States[0].Duration == 2 && loaded.Transitions[0].Priority == 8 && !loaded.Transitions[0].InterruptDuringFade);
    CHECK(!loaded.StateMachinePaused);
    CHECK(loaded.ValidateController().empty());
}
TEST(AnimatorControllerLegacySceneStillUses1DAndOrderedTransitions)
{
    auto scene = CreateRef<Scene>();
    CHECK(SceneSerializer(scene).DeserializeFromString(R"({"Version":2,"Scene":"Legacy","Entities":[{"Entity":1,"Tag":"Actor","Animator":{"Parameters":[{"Name":"Speed","Type":0,"Value":0}],"States":[{"Name":"Move","Type":"BlendTree1D","BlendParameter":"Speed","Motions":[{"Clip":"Walk","Threshold":0,"Speed":1}]}],"Transitions":[{"From":0,"To":0,"Fade":0}]}}]})"));
    auto loadedActor = scene->FindEntityByName("Actor");
    const auto& c = loadedActor.GetComponent<AnimatorComponent>();
    CHECK(c.States[0].Type == AnimatorStateType::BlendTree1D);
    CHECK(c.States[0].Duration == 0);
    CHECK(c.Transitions[0].Priority == 0 && c.Transitions[0].InterruptDuringFade);
}
TEST(AnimatorBlueprintParametersAndSuccessOutputsDriveRealComponent)
{
    GraphFixture fixture;
    Visual::Graph g;
    g.Variables = { { "Read", { Visual::Type::Number, 0. } }, { "Success", { Visual::Type::Bool, false } } };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Animator.SetFloat", 0, 0, { { "Name", "X" }, { "Value", .75 } } }, { 3, "Set.Bool", 0, 0, { { "Name", "Success" } } }, { 4, "Animator.GetFloat", 0, 0, { { "Name", "X" } } }, { 5, "Set.Number", 0, 0, { { "Name", "Read" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 2, "Success", 3, "Value" }, { 3, "Then", 5, "In" }, { 4, "Value", 5, "Value" } };
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("Actor");
    actor.AddComponent<AnimatorComponent>() = BlendController();
    fixture.Attach(actor, g);
    scene->OnRuntimeStart();
    scene->UpdateRuntime(.01f);
    auto* host = fixture.Host(actor);
    CHECK(host && host->VM.Error().empty());
    CHECK(host->VM.Variables().at("Read") == .75 && host->VM.Variables().at("Success") == true);
    scene->OnRuntimeStop();
}
TEST(AnimatorBlueprintRejectsWrongSetterTypeWithoutChangingParameter)
{
    GraphFixture fixture;
    Visual::Graph g;
    g.Variables["Success"] = { Visual::Type::Bool, true };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Animator.SetBool", 0, 0, { { "Name", "X" }, { "Value", true } } }, { 3, "Set.Bool", 0, 0, { { "Name", "Success" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 2, "Success", 3, "Value" } };
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("Actor");
    auto& c = actor.AddComponent<AnimatorComponent>();
    c = BlendController();
    fixture.Attach(actor, g);
    scene->OnRuntimeStart();
    scene->UpdateRuntime(.01f);
    auto* host = fixture.Host(actor);
    CHECK(host && host->VM.Error().empty());
    CHECK(host->VM.Variables().at("Success") == false);
    CHECK(c.GetFloat("X") == .5f);
    scene->OnRuntimeStop();
}
TEST(AnimatorBlueprintDirectClipPausesControllerAndStateResumesIt)
{
    GraphFixture fixture;
    Visual::Graph g;
    g.Nodes = { { 1, "Event.Start" }, { 2, "Animator.PlayClip", 0, 0, { { "Clip", "Run" }, { "Fade", 0. } } }, { 3, "Event.Message" }, { 4, "Animator.SetState", 0, 0, { { "Name", "Locomotion" }, { "Fade", 0. } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 3, "Then", 4, "In" } };
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("Actor");
    auto& mesh = actor.AddComponent<MeshRendererComponent>();
    mesh.ModelAsset = BlendModel();
    mesh.Type = PrimitiveMesh::Model;
    auto& c = actor.AddComponent<AnimatorComponent>();
    c = BlendController();
    fixture.Attach(actor, g);
    scene->OnRuntimeStart();
    scene->UpdateRuntime(.1f);
    auto* host = fixture.Host(actor);
    CHECK(host && host->VM.Error().empty());
    CHECK(c.StateMachinePaused && c.Clip == "Run" && c.CurrentState == -1);
    host->Emit("Message", {});
    scene->UpdateRuntime(.1f);
    CHECK(!c.StateMachinePaused && c.CurrentState == 0 && c.BlendTree == 0);
    scene->OnRuntimeStop();
}
TEST(AnimatorModelFreeClockEmitsBlueprintAnimationEvents)
{
    GraphFixture fixture;
    Visual::Graph g;
    g.Variables["Pulse"] = { Visual::Type::Number, 0. };
    g.Nodes = { { 1, "Event.AnimationEvent" }, { 2, "Set.Number", 0, 0, { { "Name", "Pulse" }, { "Value", 1. } } } };
    g.Links = { { 1, "Then", 2, "In" } };
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("Clock actor");
    auto& c = actor.AddComponent<AnimatorComponent>();
    c.States = { { "Clock" } };
    c.States[0].Duration = 1;
    c.Events = { { "Clock", .5f, "Pulse", "tick" } };
    fixture.Attach(actor, g);
    scene->OnRuntimeStart();
    scene->UpdateRuntime(.25f);
    auto* host = fixture.Host(actor);
    CHECK(host && host->VM.Error().empty());
    CHECK(host->VM.Variables().at("Pulse") == 0.);
    scene->UpdateRuntime(.25f);
    CHECK(host->VM.Variables().at("Pulse") == 1.);
    CHECK(host->VM.Error().empty());
    scene->OnRuntimeStop();
}
