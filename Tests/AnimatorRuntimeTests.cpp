#include "TestAssertions.h"
#include <Zamin/Scene/Scene.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scene/SpriteClip.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <filesystem>
using namespace Zamin;
namespace Zamin {
    Visual::Value DispatchAnimatorGraph(Entity target, const std::string& kind, const std::map<std::string, Visual::Value>& args);
}
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
namespace {
    // Directional sprite clips in one 24-frame grid: Idle 0..3, WalkRight 4..11 (a "Step" on frame 2), WalkUp 12..19
    // and a one-shot Attack 20..23.
    struct SpriteDriveFixture
    {
        GraphFixture Graphs;
        Ref<Scene> World = CreateRef<Scene>();
        Entity Actor;
        std::vector<std::string> Clips;
        explicit SpriteDriveFixture(bool events = false)
        {
            auto clip = [&](const char* name, int start, int count, bool loop, std::vector<SpriteClipEvent> events) {
                SpriteClip c;
                c.Columns = 8;
                c.Rows = 3;
                c.StartFrame = start;
                c.FrameCount = count;
                c.FPS = 12;
                c.Loop = loop;
                c.Events = std::move(events);
                const auto path = Graphs.Root / "Assets" / (std::string(name) + ".zsanim");
                CHECK(SpriteClipSerializer::Serialize(c, path));
                SpriteClipLibrary::ReloadFile(path);
                Clips.push_back(path.string());
            };
            clip("Idle", 0, 4, true, {});
            clip("WalkRight", 4, 8, true, { { 2, "Step", "" } });
            clip("WalkUp", 12, 8, true, {});
            clip("Attack", 20, 4, false, {});
            Actor = World->CreateEntity("Sprite actor");
            Actor.AddComponent<SpriteRendererComponent>();
            auto& sprite = Actor.AddComponent<SpriteAnimationComponent>();
            sprite.Clips = Clips;
            auto& c = Actor.AddComponent<AnimatorComponent>();
            c.Parameters = { { "X", AnimatorParameter::Type::Float, 1 }, { "Y", AnimatorParameter::Type::Float, 0 }, { "S", AnimatorParameter::Type::Float, .5f } };
            AnimatorState move{ "Move" };
            move.Type = AnimatorStateType::BlendTree2D;
            move.BlendParameter = "X";
            move.BlendParameterY = "Y";
            move.Motions = { { "Idle", 0, 1, { 0, 0 } }, { "WalkRight", 0, 1, { 1, 0 } }, { "WalkUp", 0, 1, { 0, 1 } } };
            move.Duration = .8f;
            AnimatorState attack{ "Attack", "Attack", 1, false };
            attack.Duration = .4f;
            AnimatorState tie{ "Tie" };
            tie.Type = AnimatorStateType::BlendTree1D;
            tie.BlendParameter = "S";
            tie.Motions = { { "WalkRight", 0, 1 }, { "WalkUp", 1, 1 } };
            tie.Duration = .8f;
            c.States = { move, attack, tie };
            c.DriveSpriteClips = true;
            if (events)
            {
                Visual::Graph g;
                g.Variables["Steps"] = { Visual::Type::Number, 0. };
                g.Nodes = { { 1, "Event.AnimationEvent" }, { 2, "Set.Number", 0, 0, { { "Name", "Steps" } } }, { 3, "Get.Number", 0, 0, { { "Name", "Steps" } } }, { 4, "Add", 0, 0, { { "B", 1. } } } };
                g.Links = { { 1, "Then", 2, "In" }, { 3, "Value", 4, "A" }, { 4, "Value", 2, "Value" } };
                Graphs.Attach(Actor, g);
            }
        }
        AnimatorComponent& Animator() { return Actor.GetComponent<AnimatorComponent>(); }
        SpriteAnimationComponent& Sprite() { return Actor.GetComponent<SpriteAnimationComponent>(); }
        std::string Clip() { return Sprite().GetCurrentClipName(); }
        int Frame() { return Sprite().GetCurrentFrame(); }
    };
}
// The dominant blend motion picks the clip and the state's normalized clock picks the frame; a direction change
// keeps the phase; a 1D tie keeps the lower index; a transition switches at half its fade; one-shots clamp.
TEST(AnimatorSpriteClipsFollowTheStateClockDominantMotionAndFade)
{
    SpriteDriveFixture f;
    f.World->OnRuntimeStart();
    for (int i = 0; i < 3; ++i) f.World->UpdateRuntime(.1f);
    CHECK(f.Clip() == "WalkRight" && f.Frame() == 4 + 3); // .3 s of a .8 s cycle: floor(.375 x 8) = 3
    f.Animator().FindParameter("X")->Value = 0;
    f.Animator().FindParameter("Y")->Value = 1;
    f.World->UpdateRuntime(.1f);
    CHECK(f.Clip() == "WalkUp" && f.Frame() == 12 + 4); // the phase goes on: .5 x 8 = 4
    for (int i = 0; i < 6; ++i) f.World->UpdateRuntime(.1f);
    CHECK(f.Frame() == 12 + 2); // 1.25 cycles: floor(10) mod 8
    f.Animator().FindParameter("Y")->Value = 0;
    f.World->UpdateRuntime(.1f);
    CHECK(f.Clip() == "Idle" && f.Frame() == 0 + 1); // 1.375 cycles of 4 frames: floor(5.5) mod 4
    // A transition shows the state it leaves until half of its fade, then the new one.
    f.Animator().EnterState(1, .4f);
    f.World->UpdateRuntime(.1f);
    CHECK(f.Clip() == "Idle" && f.Animator().GetFadeProgress() < .5f);
    f.World->UpdateRuntime(.1f);
    CHECK(f.Clip() == "Attack" && f.Frame() == 20 + 2); // .2 s of .4: floor(.5 x 4)
    for (int i = 0; i < 5; ++i) f.World->UpdateRuntime(.1f);
    CHECK(f.Clip() == "Attack" && f.Frame() == 20 + 3); // one-shot: the last frame
    // A 1D tie (S midway): the lower motion index.
    f.Animator().EnterState(2, 0);
    f.World->UpdateRuntime(.1f);
    CHECK(f.Clip() == "WalkRight");
    // An unknown clip name changes nothing; inactive entities freeze clip and frame.
    f.Animator().States[2].Motions[0].Clip = "Missing";
    f.Animator().States[2].Motions[1].Clip = "Missing";
    const int frame = f.Frame();
    f.World->UpdateRuntime(.1f);
    CHECK(f.Clip() == "WalkRight");
    f.Animator().States[2].Motions[0].Clip = "WalkRight";
    f.World->SetActive(f.Actor, false);
    f.World->UpdateRuntime(.3f);
    CHECK(f.Frame() == frame || f.Frame() == frame + 1);
    const int frozen = f.Frame();
    f.World->UpdateRuntime(.3f);
    CHECK(f.Frame() == frozen);
    f.World->OnRuntimeStop();
    // Without DriveSpriteClips the sprite plays on its own clock (old scenes keep their behaviour).
    SpriteDriveFixture own;
    own.Animator().DriveSpriteClips = false;
    own.World->OnRuntimeStart();
    own.World->UpdateRuntime(.25f);
    CHECK(own.Clip() == "Idle" && own.Frame() == 3); // PlayOnStart: the first clip at 12 fps, floor(.25 x 12) = 3
    own.World->OnRuntimeStop();
}
// Each crossed frame's clip event fires exactly once, whatever the frame rate.
TEST(AnimatorSpriteClipsFireFrameEventsOncePerCrossingAtAnyRate)
{
    for (float hz : { 20.f, 30.f, 60.f, 144.f })
    {
        SpriteDriveFixture f(true);
        f.World->OnRuntimeStart();
        const int frames = int(std::round(4.0f * hz)); // 4 s = 5 cycles of the .8 s walk: "Step" (frame 2) 5 times
        for (int i = 0; i < frames; ++i) f.World->UpdateRuntime(1.f / hz);
        auto* host = f.Graphs.Host(f.Actor);
        CHECK(host && host->VM.Error().empty());
        if (host) CHECK(host->VM.Variables().at("Steps") == 5.);
        f.World->OnRuntimeStop();
    }
}
TEST(AnimatorSpriteClipsSerializeAndResetRuntime)
{
    SpriteDriveFixture f;
    auto copy = CreateRef<Scene>();
    CHECK(SceneSerializer(copy).DeserializeFromString(SceneSerializer(f.World).SerializeToString()));
    CHECK(copy->FindEntityByName("Sprite actor").GetComponent<AnimatorComponent>().DriveSpriteClips);
    const auto text = SceneSerializer(f.World).SerializeToString();
    CHECK(text.find("DriveSpriteClips") != std::string::npos);
    f.Animator().DriveSpriteClips = false;
    CHECK(SceneSerializer(f.World).SerializeToString().find("DriveSpriteClips") == std::string::npos); // default: not written
    f.Animator().DriveSpriteClips = true;
    f.World->OnRuntimeStart();
    f.World->UpdateRuntime(.1f);
    CHECK(f.Sprite().DrivenUpdate != 0);
    f.Sprite().ResetRuntime();
    CHECK(f.Sprite().DrivenUpdate == 0);
    f.World->OnRuntimeStop();
}
// A layer's clip asked again keeps its time; with Restart it starts over (crossfading from itself) and its events fire
// again - a second swing of the same attack. The Blueprint node's Restart pin does the same; graphs without it don't.
TEST(AnimatorLayerRestartReplaysTheClipAndItsEvents)
{
    auto scene = CreateRef<Scene>();
    auto actor = scene->CreateEntity("Swordsman");
    auto& mesh = actor.AddComponent<MeshRendererComponent>();
    mesh.ModelAsset = BlendModel();
    mesh.Type = PrimitiveMesh::Model;
    auto& c = actor.AddComponent<AnimatorComponent>();
    c.Clip = "Idle";
    c.Layers.push_back({ "Upper" });
    c.Layers[0].Loop = false;
    c.Events.push_back({ "Run", .5f, "Hit", "" }); // Run lasts .5 s: Hit at .25 s
    int hits = 0;
    scene->GetEventBus().Subscribe("AnimationEvent", [&](const ScriptMessage& m) {
        if (auto* info = std::any_cast<AnimationEventInfo>(&m.Payload); info && info->Name == "Hit") ++hits;
    });
    scene->OnRuntimeStart();
    CHECK(c.PlayLayer("Upper", "Run", false, 1.0f, 0.0f));
    for (int i = 0; i < 6; ++i) scene->UpdateRuntime(.05f);
    CHECK(hits == 1);
    CHECK_NEAR(c.Layers[0].Time, .3f, 1e-4f);
    CHECK(c.PlayLayer("Upper", "Run", false, 1.0f, 0.0f)); // the same clip: keeps playing
    CHECK_NEAR(c.Layers[0].Time, .3f, 1e-4f);
    CHECK(c.PlayLayer("Upper", "Run", false, 1.0f, .1f, true));
    CHECK(c.Layers[0].Time == 0 && c.Layers[0].PreviousClip == "Run" && c.Layers[0].IsFading());
    for (int i = 0; i < 6; ++i) scene->UpdateRuntime(.05f);
    CHECK(hits == 2);
    // the graph node: Restart pin (default false)
    auto& catalog = Visual::Catalog().at("Animator.PlayLayer");
    bool pin = false;
    for (auto& p : catalog.Inputs)
        if (p.Name == "Restart") pin = p.Kind == Visual::Type::Bool && p.Default == Visual::Value(false);
    CHECK(pin);
    std::map<std::string, Visual::Value> args{ { "Name", "Upper" }, { "Clip", "Run" }, { "Speed", 1.0 }, { "Loop", false }, { "Fade", 0.0 } };
    CHECK(DispatchAnimatorGraph(actor, "Animator.PlayLayer", args).at("Success").get<bool>());
    CHECK_NEAR(c.Layers[0].Time, .3f, 1e-4f); // an old graph (no Restart value): unchanged behaviour
    args["Restart"] = true;
    CHECK(DispatchAnimatorGraph(actor, "Animator.PlayLayer", args).at("Success").get<bool>());
    CHECK(c.Layers[0].Time == 0);
    for (int i = 0; i < 6; ++i) scene->UpdateRuntime(.05f);
    CHECK(hits == 3);
    scene->OnRuntimeStop();
}
