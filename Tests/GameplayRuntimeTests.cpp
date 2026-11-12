#include "TestAssertions.h"
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Scene/Scene.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Core/Input.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Physics/PhysicsWorld2D.h>
#include <Zamin/Physics/PhysicsWorld3D.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <filesystem>
#include <fstream>
using namespace Zamin;
namespace {
    using V = Visual::Value;
    std::map<std::string, V> Defaults(const std::string& kind)
    {
        std::map<std::string, V> a;
        for (const auto& p : Visual::Catalog().at(kind).Inputs)
            if (p.Kind != Visual::Type::Exec) a[p.Name] = p.Default;
        return a;
    }
    V Call(Gameplay::Runtime& runtime, Entity actor, const std::string& kind, std::map<std::string, V> overrides = {}, Entity target = {})
    {
        auto args = Defaults(kind);
        for (auto& [key, value] : overrides) args[key] = value;
        return runtime.Dispatch(actor, target ? target : actor, kind, args);
    }
    struct Fixture
    {
        Ref<Scene> SceneRef = CreateRef<Scene>();
        Entity Actor;
        std::filesystem::path Root;
        Fixture(bool three = false)
        {
            static int count = 0;
            Root = std::filesystem::temp_directory_path() / ("zamin-gameplay-test-" + std::to_string(++count));
            std::filesystem::create_directories(Root);
            SceneRef->SetName(three ? "Test3D" : "Test2D");
            auto floor = SceneRef->CreateEntity("Floor");
            floor.GetTransform().Translation = { 0, -.5f, 0 };
            Actor = SceneRef->CreateEntityWithUUID(UUID(12345), "Player");
            Actor.GetTransform().Translation = { 0, three ? 1.0f : .6f, 0 };
            if (three)
            {
                floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
                floor.AddComponent<BoxCollider3DComponent>().HalfExtents = { 20, .5f, 20 };
                Actor.AddComponent<CharacterController3DComponent>();
            }
            else
            {
                floor.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
                floor.AddComponent<BoxCollider2DComponent>().Size = { 20, .5f };
                auto& body = Actor.AddComponent<Rigidbody2DComponent>();
                body.FixedRotation = true;
                Actor.AddComponent<BoxCollider2DComponent>().Size = { .4f, .5f };
            }
            SaveData::SetFilePath(Root / "save.json");
            SaveData::DeleteAll();
            SceneRef->OnRuntimeStart();
            for (int i = 0; i < 30; ++i) SceneRef->UpdateRuntime(1.f / 60);
        }
        ~Fixture()
        {
            SceneRef->OnRuntimeStop();
            SaveData::SetFilePath({});
            std::filesystem::remove_all(Root);
        }
        void Move(glm::vec3 position)
        {
            Actor.GetTransform().Translation = position;
            SceneRef->InvalidateWorldTransforms();
            if (auto* b = Actor.TryGetComponent<Rigidbody2DComponent>()) PhysicsWorld2D::SetTransform((b2Body*)b->RuntimeBody, glm::vec2(position), 0);
            if (auto* c = Actor.TryGetComponent<CharacterController3DComponent>()) SceneRef->GetPhysicsWorld3D()->SetCharacterPosition(c->RuntimeID, position);
        }
        void Animator()
        {
            auto& c = Actor.AddComponent<AnimatorComponent>();
            c.Parameters = { { "Speed", AnimatorParameter::Type::Float, 0 }, { "MoveX", AnimatorParameter::Type::Float, 0 }, { "MoveY", AnimatorParameter::Type::Float, 0 }, { "VerticalSpeed", AnimatorParameter::Type::Float, 0 }, { "Grounded", AnimatorParameter::Type::Bool, 0 } };
        }
    };
}
TEST(GameplayRuntimeMotor2DMovesAndStopsOnDisable)
{
    Fixture f;
    Gameplay::Runtime runtime;
    for (int i = 0; i < 30; ++i)
    {
        runtime.BeginFrame(*f.SceneRef);
        Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Move", { 1., 0. } }, { "Delta", 1. / 60 } });
        f.SceneRef->UpdateRuntime(1.f / 60);
    }
    CHECK(f.Actor.GetTransform().Translation.x > 1);
    runtime.Stop(*f.SceneRef);
    CHECK_NEAR(PhysicsWorld2D::GetLinearVelocity((b2Body*)f.Actor.GetComponent<Rigidbody2DComponent>().RuntimeBody).x, 0, 1e-5f);
}
TEST(GameplayRuntimeTopDownStopCancelsBothAxes)
{
    Fixture f;
    auto* body = (b2Body*)f.Actor.GetComponent<Rigidbody2DComponent>().RuntimeBody;
    Gameplay::Runtime runtime;
    runtime.BeginFrame(*f.SceneRef);
    Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Move", { 0., 1. } }, { "TopDown", true }, { "Delta", .1 } });
    CHECK(PhysicsWorld2D::GetLinearVelocity(body).y > 0);
    runtime.Stop(*f.SceneRef);
    CHECK(PhysicsWorld2D::GetLinearVelocity(body) == glm::vec2(0));
}
TEST(GameplayRuntimeMotor2DIgnoresVerticalInputForWalkingSpeed)
{
    Fixture f;
    Gameplay::Runtime runtime;
    runtime.BeginFrame(*f.SceneRef);
    auto result = Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Move", { 1., 1. } }, { "Acceleration", 1000. }, { "Delta", .1 } });
    CHECK_NEAR(result.at("Velocity")[0].get<float>(), 5, 1e-4f);
    CHECK(result.at("Velocity")[2] == 0);
}
TEST(GameplayRuntimeMotorRejectsDuplicateAdvance)
{
    Fixture f;
    Gameplay::Runtime runtime;
    runtime.BeginFrame(*f.SceneRef);
    Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Delta", .02 } });
    bool rejected = false;
    try
    {
        Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Delta", .02 } });
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    CHECK(rejected);
    runtime.BeginFrame(*f.SceneRef);
    CHECK(Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Delta", .02 } }).is_object());
}
TEST(GameplayRuntimeMotor3DUsesRealCharacterAndJump)
{
    Fixture f(true);
    Gameplay::Runtime runtime;
    bool jumped = false;
    for (int i = 0; i < 30; ++i)
    {
        runtime.BeginFrame(*f.SceneRef);
        auto result = Call(runtime, f.Actor, "Gameplay.Motor3D", { { "Move", { 1., 0. } }, { "Delta", 1. / 60 }, { "Jump", i == 10 } });
        jumped |= result.at("Jumped").get<bool>();
        f.SceneRef->UpdateRuntime(1.f / 60);
    }
    CHECK(jumped && f.Actor.GetTransform().Translation.x > 1);
    CHECK(f.Actor.GetTransform().Translation.y > 1);
    runtime.Stop(*f.SceneRef);
    CHECK(f.Actor.GetComponent<CharacterController3DComponent>().MoveVelocity == glm::vec3(0));
}
TEST(GameplayRuntimeAnimatorReadsCompletedPhysicsNotNew2DCommand)
{
    Fixture f;
    f.Animator();
    Gameplay::Runtime runtime;
    runtime.BeginFrame(*f.SceneRef);
    Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Move", { 1., 0. } }, { "Delta", .1 } });
    auto result = Call(runtime, f.Actor, "Gameplay.BindAnimator", { { "TwoD", true }, { "Damping", 0. }, { "Delta", .1 } });
    CHECK(result.at("Success") == true);
    CHECK_NEAR(result.at("Speed").get<float>(), 0, 1e-4f);
    CHECK_NEAR(f.Actor.GetComponent<AnimatorComponent>().GetFloat("Speed"), 0, 1e-4f);
    f.SceneRef->UpdateRuntime(1.f / 60);
    runtime.AfterPhysics();
    result = Call(runtime, f.Actor, "Gameplay.BindAnimator", { { "TwoD", true }, { "Damping", 0. }, { "Delta", .1 } });
    CHECK(result.at("Speed").get<float>() > 0);
}
TEST(GameplayRuntimeAnimatorBindingIsAtomicForWrongTypes)
{
    Fixture f;
    f.Animator();
    auto& animator = f.Actor.GetComponent<AnimatorComponent>();
    animator.SetFloat("Speed", 9);
    animator.FindParameter("Grounded")->Kind = AnimatorParameter::Type::Float;
    Gameplay::Runtime runtime;
    auto result = Call(runtime, f.Actor, "Gameplay.BindAnimator", { { "TwoD", true }, { "Damping", 0. }, { "Delta", .1 } });
    CHECK(result.at("Success") == false && animator.GetFloat("Speed") == 9);
}
TEST(GameplayRuntimeAnimatorBindingRejectsExplicitMissingSource)
{
    Fixture f;
    f.Animator();
    Gameplay::Runtime runtime;
    bool rejected = false;
    try
    {
        Call(runtime, f.Actor, "Gameplay.BindAnimator", { { "Source", 999999ull } });
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    CHECK(rejected);
}
TEST(GameplayRuntimeCameraFollowsAndClamps2DBounds)
{
    Fixture f;
    auto camera = f.SceneRef->CreateEntity("Camera");
    camera.AddComponent<CameraComponent>();
    camera.GetTransform().Translation = { 0, 0, 10 };
    Gameplay::Runtime runtime;
    auto result = Call(runtime, f.Actor, "Gameplay.Camera", { { "Anchor", uint64_t(f.Actor.GetUUID()) }, { "Eye", { 0., 0., 0. } }, { "Offset", { 0., 0., 10. } }, { "BoundsMin", { -2., -2. } }, { "BoundsMax", { 2., 2. } }, { "Delta", .1 } }, camera);
    CHECK(result.at("Blocked") == false);
    CHECK_NEAR(f.SceneRef->GetWorldPosition(camera).x, 0, 1e-4f);
    CHECK_NEAR(f.SceneRef->GetWorldPosition(camera).y, 0, 1e-4f);
}
TEST(GameplayRuntimeCameraObstructionIgnoresPlayerButStopsAtWall)
{
    Fixture f(true);
    auto camera = f.SceneRef->CreateEntity("Camera");
    camera.AddComponent<CameraComponent>();
    camera.GetTransform().Translation = { 0, 3, 6 };
    f.SceneRef->OnRuntimeStop();
    auto wall = f.SceneRef->CreateEntity("Camera wall");
    wall.GetTransform().Translation = { 0, 1, 3 };
    wall.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
    wall.AddComponent<BoxCollider3DComponent>().HalfExtents = { 3, 3, .2f };
    f.SceneRef->OnRuntimeStart();
    for (int i = 0; i < 30; ++i) f.SceneRef->UpdateRuntime(1.f / 60);
    Gameplay::Runtime runtime;
    auto result = Call(runtime, f.Actor, "Gameplay.Camera", { { "Anchor", uint64_t(f.Actor.GetUUID()) }, { "Eye", { 0., 0., 0. } }, { "Offset", { 0., 0., 6. } }, { "Orbit", true }, { "Delta", .1 } }, camera);
    CHECK(result.at("Blocked") == true);
    CHECK(f.SceneRef->GetWorldPosition(camera).z < 3);
}
TEST(GameplayRuntimeCheckpointRestores2DPhysicsAndClearsVelocity)
{
    Fixture f;
    Gameplay::Runtime runtime;
    const auto initial = f.SceneRef->GetWorldPosition(f.Actor);
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture", { { "Persistent", false } }).at("Success") == true);
    f.Move({ 3, 2, 0 });
    auto* body = (b2Body*)f.Actor.GetComponent<Rigidbody2DComponent>().RuntimeBody;
    PhysicsWorld2D::SetLinearVelocity(body, { 4, 2 });
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointRestore", { { "Persistent", false } }).at("Success") == true);
    CHECK_NEAR(f.SceneRef->GetWorldPosition(f.Actor).x, initial.x, 1e-5f);
    CHECK_NEAR(PhysicsWorld2D::GetPosition(body).y, initial.y, 1e-5f);
    CHECK(PhysicsWorld2D::GetLinearVelocity(body) == glm::vec2(0));
}
TEST(GameplayRuntimeCheckpointRestoresCharacterAndResetsIntent)
{
    Fixture f(true);
    Gameplay::Runtime runtime;
    const auto initial = f.SceneRef->GetWorldPosition(f.Actor);
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture", { { "Persistent", false } }).at("Success") == true);
    f.Move({ 4, 2, 0 });
    auto& cc = f.Actor.GetComponent<CharacterController3DComponent>();
    cc.Move({ 3, 0, 0 });
    cc.VerticalVelocity = 7;
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointRestore", { { "Persistent", false } }).at("Success") == true);
    CHECK_NEAR(f.Actor.GetTransform().Translation.x, initial.x, 1e-5f);
    CHECK(cc.MoveVelocity == glm::vec3(0) && cc.VerticalVelocity == 0 && cc.Velocity == glm::vec3(0));
}
TEST(GameplayRuntimePersistentCheckpointLoadsIntoFreshRuntime)
{
    Fixture f;
    Gameplay::Runtime first;
    const auto initial = f.SceneRef->GetWorldPosition(f.Actor);
    CHECK(Call(first, f.Actor, "Gameplay.CheckpointCapture").at("Success") == true);
    f.Move({ 4, 2, 0 });
    Gameplay::Runtime fresh;
    CHECK(Call(fresh, f.Actor, "Gameplay.CheckpointHas") == true);
    CHECK(Call(fresh, f.Actor, "Gameplay.CheckpointRestore").at("Success") == true);
    CHECK_NEAR(f.Actor.GetTransform().Translation.x, initial.x, 1e-5f);
    f.Move({ 3, 2, 0 });
    CHECK(Call(fresh, f.Actor, "Gameplay.CheckpointRestore", { { "Persistent", false } }).at("Success") == true);
    CHECK_NEAR(f.Actor.GetTransform().Translation.x, initial.x, 1e-5f);
}
TEST(GameplayRuntimeCheckpointIdentityMismatchAndCorruptionDoNotMoveActor)
{
    Fixture f;
    Gameplay::Runtime runtime;
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture").at("Success") == true);
    f.Move({ 4, 2, 0 });
    f.SceneRef->SetName("Another scene");
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointRestore").at("Success") == false);
    CHECK(f.Actor.GetTransform().Translation.x == 4);
    f.SceneRef->SetName("Test2D");
    std::ofstream(f.Root / "gameplay-checkpoints/Player.json") << "{";
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointRestore").at("Success") == false);
    CHECK(f.Actor.GetTransform().Translation.x == 4);
}
TEST(GameplayRuntimeFailedPersistentCaptureKeepsMemoryCheckpoint)
{
    Fixture f;
    Gameplay::Runtime runtime;
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture", { { "Persistent", false } }).at("Success") == true);
    f.Move({ 4, 2, 0 });
    std::ofstream(f.Root / "blocked") << "not a directory";
    SaveData::SetFilePath(f.Root / "blocked/save.json");
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture").at("Success") == false);
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointRestore", { { "Persistent", false } }).at("Success") == true);
    CHECK_NEAR(f.Actor.GetTransform().Translation.x, 0, 1e-5f);
}
TEST(GameplayRuntimeCheckpointClearAndSlotValidation)
{
    Fixture f;
    Gameplay::Runtime runtime;
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture").at("Success") == true);
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointClear").at("Success") == true);
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointHas") == false);
    bool rejected = false;
    try
    {
        Call(runtime, f.Actor, "Gameplay.CheckpointCapture", { { "Slot", "../escape" } });
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    CHECK(rejected);
}
TEST(GameplayRuntimeCheckpointIOBudgetAndCacheAreBounded)
{
    Fixture f;
    Gameplay::Runtime runtime;
    for (int i = 0; i < 8; ++i) Call(runtime, f.Actor, "Gameplay.CheckpointHas");
    bool rejected = false;
    try
    {
        Call(runtime, f.Actor, "Gameplay.CheckpointHas");
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    CHECK(rejected);
    runtime.BeginFrame(*f.SceneRef);
    for (int i = 0; i < 16; ++i) CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture", { { "Slot", "Slot" + std::to_string(i) }, { "Persistent", false } }).at("Success") == true);
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture", { { "Slot", "Overflow" }, { "Persistent", false } }).at("Success") == false);
}
TEST(GameplayRuntimeRejectsCrossSceneTargets)
{
    Fixture f;
    auto other = CreateRef<Scene>();
    auto actor = other->CreateEntity("Other");
    Gameplay::Runtime runtime;
    bool rejected = false;
    try
    {
        Call(runtime, f.Actor, "Gameplay.Motion", {}, actor);
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    CHECK(rejected);
}
TEST(GameplayRuntimeCheckpointRejectsObstructedRestore)
{
    Fixture f;
    Gameplay::Runtime runtime;
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointCapture", { { "Persistent", false } }).at("Success") == true);
    const auto point = f.Actor.GetTransform().Translation;
    f.Move({ 4, 2, 0 });
    f.SceneRef->OnRuntimeStop();
    auto block = f.SceneRef->CreateEntity("Block");
    block.GetTransform().Translation = point;
    block.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    block.AddComponent<BoxCollider2DComponent>().Size = { .3f, .3f };
    f.SceneRef->OnRuntimeStart();
    f.SceneRef->UpdateRuntime(1.f / 60);
    CHECK(Call(runtime, f.Actor, "Gameplay.CheckpointRestore", { { "Persistent", false } }).at("Success") == false);
    CHECK(f.Actor.GetTransform().Translation.x > 3);
}
TEST(GameplayRuntimeTypingBlocksCompositeInput)
{
    Fixture f;
    Gameplay::Runtime runtime;
    auto field = f.SceneRef->CreateEntity("Chat");
    field.AddComponent<UITransformComponent>();
    field.AddComponent<UIInputFieldComponent>();
    f.SceneRef->FocusInputField(field);
    CHECK(f.SceneRef->IsTextInputFocused());
    auto value = Call(runtime, f.Actor, "Gameplay.Input");
    CHECK(value.at("Enabled") == false);
    CHECK(value.at("Move") == V::array({ 0, 0 }));
    CHECK(value.at("Jump") == false);
    f.SceneRef->FocusInputField({});
    CHECK(Call(runtime, f.Actor, "Gameplay.Input").at("Enabled") == true);
}
TEST(GameplayRuntimeWallCollisionDoesNotAnimateCommandedSpeed)
{
    Fixture f;
    f.Animator();
    f.SceneRef->OnRuntimeStop();
    auto wall = f.SceneRef->CreateEntity("Wall");
    wall.GetTransform().Translation = { 1, 1, 0 };
    wall.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
    wall.AddComponent<BoxCollider2DComponent>().Size = { .2f, 4 };
    f.SceneRef->OnRuntimeStart();
    Gameplay::Runtime runtime;
    for (int i = 0; i < 120; ++i)
    {
        runtime.BeginFrame(*f.SceneRef);
        Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Move", { 1., 0. } }, { "Delta", 1. / 60 } });
        f.SceneRef->UpdateRuntime(1.f / 60);
        runtime.AfterPhysics();
    }
    auto value = Call(runtime, f.Actor, "Gameplay.BindAnimator", { { "TwoD", true }, { "Damping", 0. }, { "Delta", .1 } });
    CHECK(value.at("Success") == true);
    CHECK_NEAR(value.at("Speed").get<float>(), 0, .01f);
    CHECK(f.Actor.GetTransform().Translation.x < .5f);
}
TEST(GameplayRuntimeDestroyedMotorDoesNotStopReplacementWithSameUUID)
{
    Fixture f;
    Gameplay::Runtime runtime;
    runtime.BeginFrame(*f.SceneRef);
    Call(runtime, f.Actor, "Gameplay.Motor2D", { { "Delta", .02 } });
    f.SceneRef->DestroyEntity(f.Actor);
    auto replacement = f.SceneRef->CreateEntityWithUUID(UUID(12345), "Replacement");
    runtime.BeginFrame(*f.SceneRef);
    CHECK(runtime.MotorCount() == 0);
    runtime.Stop(*f.SceneRef);
    CHECK(replacement);
}
TEST(GameplayWorkshopGraphsValidateAndTextEqualityIsExact)
{
    const auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/GameplayWorkshop/Assets/Scripts";
    int graphs = 0;
    for (const auto& item : std::filesystem::directory_iterator(root))
    {
        if (item.path().extension() != ".zgraph") continue;
        auto graph = Visual::Graph::Load(item.path());
        CHECK(graph.Validate().empty());
        CHECK(Visual::Graph::Parse(graph.Serialize()).Validate().empty());
        ++graphs;
    }
    CHECK(graphs == 5);
    Visual::Graph graph;
    graph.Variables["Match"] = { Visual::Type::Bool, false };
    graph.Nodes = { { 1, "Event.Message" }, { 2, "TextEqual", 0, 0, { { "B", "Restore" } } }, { 3, "Set.Bool", 0, 0, { { "Name", "Match" } } } };
    graph.Links = { { 1, "Text", 2, "A" }, { 1, "Then", 3, "In" }, { 2, "Value", 3, "Value" } };
    Visual::Machine machine;
    CHECK(machine.Load(graph));
    machine.Event("Message", { { "Text", "Restore" } });
    CHECK(machine.Variables().at("Match") == true);
    machine.Event("Message", { { "Text", "restore" } });
    CHECK(machine.Variables().at("Match") == false);
}

TEST(GameplayRuntimeNativeHostDisableAndDebuggerPauseStopMotor)
{
    const auto prior = Project::GetActive();
    Fixture f;
    Project::New();
    std::filesystem::create_directories(f.Root / "Assets/Scripts");
    CHECK(Project::SaveActive(f.Root / "Gameplay.zproject"));
    Visual::Graph graph;
    graph.Nodes = { { 1, "Event.Update" }, { 2, "Gameplay.Motor2D", 0, 0, { { "Move", { 1., 0. } }, { "Delta", .1 } } } };
    graph.Links = { { 1, "Then", 2, "In" } };
    CHECK(graph.Save(f.Root / "Assets/Scripts/Motor.zgraph"));
    f.SceneRef->OnRuntimeStop();
    auto& script = f.Actor.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/Motor.zgraph"));
    f.SceneRef->OnRuntimeStart();
    f.SceneRef->UpdateRuntime(1.f / 60);
    auto* host = dynamic_cast<VisualGraphHost*>(script.Instance);
    CHECK(host && host->VM.Error().empty());
    auto* body = (b2Body*)f.Actor.GetComponent<Rigidbody2DComponent>().RuntimeBody;
    CHECK(PhysicsWorld2D::GetLinearVelocity(body).x > 0);
    host->SetEnabled(false);
    CHECK_NEAR(PhysicsWorld2D::GetLinearVelocity(body).x, 0, 1e-5f);
    host->SetEnabled(true);
    f.SceneRef->UpdateRuntime(1.f / 60);
    CHECK(PhysicsWorld2D::GetLinearVelocity(body).x > 0);
    host->VM.Paused = true;
    f.SceneRef->UpdateRuntime(1.f / 60);
    CHECK_NEAR(PhysicsWorld2D::GetLinearVelocity(body).x, 0, 1e-5f);
    f.SceneRef->OnRuntimeStop();
    Project::SetActive(prior);
}
