#include "TestAssertions.h"
#include <Zamin/Gameplay/GameplayAI.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Physics/PhysicsWorld2D.h>
#include <Zamin/Physics/PhysicsWorld3D.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <filesystem>
using namespace Zamin;
namespace {
    struct AIFixture
    {
        Scene World;
        Entity NPC, Hero, Grid;
        bool Three;
        std::filesystem::path Root;
        AIFixture(bool three = false, bool wall = false) : Three(three)
        {
            Root = std::filesystem::temp_directory_path() / ("zamin-ai-" + std::to_string(uint64_t(UUID())));
            std::filesystem::create_directories(Root);
            SaveData::SetFilePath(Root / "save.json");
            SaveData::DeleteAll();
            NPC = World.CreateEntityWithUUID(UUID(100), "NPC");
            Hero = World.CreateEntityWithUUID(UUID(200), "Hero");
            Hero.AddTag("Player");
            Hero.GetTransform().Translation = { 5, 0, 0 };
            Grid = World.CreateEntityWithUUID(UUID(300), "Grid");
            if (three)
            {
                auto floor = World.CreateEntity();
                floor.GetTransform().Translation = { 0, -.5f, 0 };
                floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
                floor.AddComponent<BoxCollider3DComponent>().HalfExtents = { 10, .5f, 10 };
                auto& grid = Grid.AddComponent<NavGrid3DComponent>();
                grid.Size = { 20, 4, 20 };
                grid.AgentRadius = .1f;
                Grid.GetTransform().Translation.y = 1;
                NPC.AddComponent<CharacterController3DComponent>();
            }
            else
            {
                auto& grid = Grid.AddComponent<NavGrid2DComponent>();
                grid.AutoBounds = false;
                grid.Size = { 20, 20 };
                grid.CellSize = .5f;
                grid.UseTilemaps = grid.UseColliders = false;
                grid.AgentRadius = 0;
                auto& body = NPC.AddComponent<Rigidbody2DComponent>();
                body.Type = Rigidbody2DComponent::BodyType::Dynamic;
                body.GravityScale = 0;
                body.FixedRotation = true;
                NPC.AddComponent<BoxCollider2DComponent>().Size = { .3f, .3f };
            }
            NPC.AddComponent<NavAgentComponent>().Grid.ID = 300;
            if (wall)
            {
                auto e = World.CreateEntity("Wall");
                e.GetTransform().Translation = { 1, three ? 1.f : 0.f, 0 };
                if (three)
                {
                    e.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
                    e.AddComponent<BoxCollider3DComponent>().HalfExtents = { .1f, 2, 2 };
                }
                else
                {
                    e.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
                    e.AddComponent<BoxCollider2DComponent>().Size = { .1f, 3 };
                }
            }
            World.OnRuntimeStart();
            std::string error;
            CHECK(Rules().Configure(NPC, { 100, 0, 0, 0, 2 }, false, error));
            CHECK(Rules().Configure(Hero, { 1000, 0, 0, 0, 1 }, false, error));
        }
        ~AIFixture()
        {
            World.OnRuntimeStop();
            SaveData::SetFilePath({});
            std::filesystem::remove_all(Root);
        }
        Gameplay::RuleWorld& Rules() { return Gameplay::RuleWorld::For(World); }
        Gameplay::AIWorld& AI() { return Gameplay::AIWorld::For(World); }
        Gameplay::AISpec Spec()
        {
            Gameplay::AISpec s;
            s.Brain.TwoD = !Three;
            s.Brain.View = 3;
            s.Brain.Lose = 4;
            s.Brain.Near = 1;
            s.Brain.Leash = 8;
            s.Brain.FieldOfView = 360;
            s.Brain.Memory = .2f;
            s.Brain.AttackEnter = 1;
            s.Brain.AttackExit = 1.3f;
            s.Brain.Wait = .1f;
            s.Brain.Speed = 2;
            s.Recovery = { true, .2f, .1f, .05f, 2 };
            s.Eye = { 0, Three ? 1.f : 0.f, 0 };
            s.Points = { { 0, 0, 0 }, Three ? glm::vec3(0, 0, 2) : glm::vec3(0, 2, 0) };
            return s;
        }
        void Configure(Gameplay::AISpec s)
        {
            std::string error;
            CHECK(AI().Configure(NPC, NPC, s, false, error));
        }
        void Frames(int n)
        {
            for (int i = 0; i < n; ++i) World.UpdateRuntime(1.f / 30);
        }
        void Move(Entity e, glm::vec3 p)
        {
            e.GetTransform().Translation = p;
            World.InvalidateWorldTransforms();
            if (auto* b = e.TryGetComponent<Rigidbody2DComponent>(); b && b->RuntimeBody) PhysicsWorld2D::SetTransform((b2Body*)b->RuntimeBody, glm::vec2(p), 0);
            if (auto* c = e.TryGetComponent<CharacterController3DComponent>(); c && c->RuntimeID != ~0u) World.GetPhysicsWorld3D()->SetCharacterPosition(c->RuntimeID, p);
        }
    };
    Visual::Value Call(Gameplay::Runtime& runtime, Entity host, const std::string& kind, std::map<std::string, Visual::Value> overrides = {}, Entity target = {})
    {
        std::map<std::string, Visual::Value> args;
        for (const auto& p : Visual::Catalog().at(kind).Inputs)
            if (p.Kind != Visual::Type::Exec) args[p.Name] = p.Default;
        for (auto& [key, value] : overrides) args[key] = value;
        return runtime.Dispatch(host, target ? target : host, kind, args);
    }
}
TEST(AIGameplayPatrolUsesRealBox2DBodyAndWaypointWait)
{
    AIFixture f;
    f.Configure(f.Spec());
    f.Frames(40);
    auto s = *f.AI().State(f.NPC);
    CHECK(s.Brain.Mode == Gameplay::BrainMode::Patrol && s.Brain.Arrivals >= 1);
    CHECK(f.World.GetWorldPosition(f.NPC).y > .5f);
    CHECK(s.PathRequests > 0);
}
TEST(AIGameplayThreeDPatrolUsesBakedGridAndCharacter)
{
    AIFixture f(true);
    f.Configure(f.Spec());
    f.Frames(40);
    CHECK(f.World.GetWorldPosition(f.NPC).z > .5f);
    CHECK(f.AI().State(f.NPC)->Brain.Arrivals >= 1);
}
TEST(AIGameplaySightChaseAttackAndLockedTargetDamage)
{
    AIFixture f;
    f.Move(f.Hero, { 2.5f, 0, 0 });
    auto spec = f.Spec();
    spec.Points.clear();
    f.Configure(spec);
    f.Frames(100);
    auto s = *f.AI().State(f.NPC);
    CHECK(s.Sightings >= 1 && s.Attacks >= 1);
    CHECK(f.Rules().Health(f.Hero)->Current < 1000);
    CHECK(s.Target == 200);
}
TEST(AIGameplayOccludedTargetIsNotAcquiredInEitherDimension)
{
    for (bool three : { false, true })
    {
        AIFixture f(three, true);
        f.Move(f.Hero, { 2, 0, 0 });
        auto s = f.Spec();
        s.Points.clear();
        f.Configure(s);
        f.Frames(1);
        CHECK(!f.AI().State(f.NPC)->Visible && f.AI().State(f.NPC)->Target == 0);
    }
}
TEST(AIGameplayLostTargetInvestigatesThenReturnsHome)
{
    AIFixture f;
    auto spec = f.Spec();
    spec.Points.clear();
    f.Move(f.Hero, { 2, 0, 0 });
    f.Configure(spec);
    f.Frames(10);
    CHECK(f.AI().State(f.NPC)->Target == 200);
    f.Move(f.Hero, { 5.5f, 0, 0 });
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Brain.Mode == Gameplay::BrainMode::Investigate);
    f.Frames(10);
    CHECK(f.AI().State(f.NPC)->Brain.Mode == Gameplay::BrainMode::Return);
    f.Frames(100);
    CHECK(f.AI().State(f.NPC)->Brain.Returns >= 1);
    CHECK(glm::length(glm::vec2(f.World.GetWorldPosition(f.NPC))) < .4f);
}
TEST(AIGameplayTargetDestroySkipsMemoryAndUUIDReplacementIsFresh)
{
    AIFixture f;
    f.Move(f.Hero, { 2, 0, 0 });
    f.Configure(f.Spec());
    f.Frames(1);
    f.World.DestroyEntity(f.Hero);
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Target == 0);
    auto hero = f.World.CreateEntityWithUUID(UUID(200), "Replacement");
    hero.AddTag("Player");
    hero.GetTransform().Translation = { 1, 0, 0 };
    CHECK(!f.Rules().Health(hero));
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Target == 0);
}
TEST(AIGameplayLocalStallRetriesAreBoundedAndTerminal)
{
    AIFixture f(false, true);
    auto spec = f.Spec();
    spec.Tag = "Nobody";
    spec.Points = { { 3, 0, 0 } };
    f.Configure(spec);
    f.Frames(180);
    auto s = *f.AI().State(f.NPC);
    CHECK(s.Brain.Mode == Gameplay::BrainMode::Blocked);
    CHECK(s.Retries == 2);
    auto requests = s.PathRequests;
    f.Frames(60);
    CHECK(f.AI().State(f.NPC)->PathRequests == requests);
    std::string error;
    CHECK(f.AI().Reset(f.NPC, false, error));
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Brain.Mode != Gameplay::BrainMode::Blocked);
}
TEST(AIGameplayDisableStopsImmediatelyAndRemoveRestoresNavSettings)
{
    AIFixture f;
    auto before = f.NPC.GetComponent<NavAgentComponent>();
    f.Configure(f.Spec());
    f.Frames(20);
    CHECK(f.AI().Enable(f.NPC, false));
    auto* b = (b2Body*)f.NPC.GetComponent<Rigidbody2DComponent>().RuntimeBody;
    CHECK(PhysicsWorld2D::GetLinearVelocity(b) == glm::vec2(0));
    CHECK(!f.NPC.GetComponent<NavAgentComponent>().HasDestination);
    CHECK(f.AI().Remove(f.NPC));
    auto& n = f.NPC.GetComponent<NavAgentComponent>();
    CHECK(n.Speed == before.Speed && n.RepathInterval == before.RepathInterval && n.RecoverySettings.Enabled == before.RecoverySettings.Enabled);
}
TEST(AIGameplayDeathStopsNavigationAndReviveReturns)
{
    AIFixture f;
    f.Configure(f.Spec());
    f.Frames(20);
    CHECK(f.Rules().Damage(f.NPC, {}, 1000, false, false, false).Killed);
    CHECK(!f.NPC.GetComponent<NavAgentComponent>().HasDestination);
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Brain.Mode == Gameplay::BrainMode::Dead);
    std::string error;
    CHECK(f.Rules().Revive(f.NPC, 0, error));
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Brain.Mode == Gameplay::BrainMode::Return);
}
TEST(AIGameplayScenePauseFreezesMemoryAndPrunesDestroyedOwners)
{
    AIFixture f;
    f.Configure(f.Spec());
    f.Frames(1);
    auto s = *f.AI().State(f.NPC);
    f.World.SetTimeScale(0);
    f.Frames(20);
    CHECK(f.AI().State(f.NPC)->Brain.Remember == s.Brain.Remember);
    f.World.DestroyEntity(f.NPC);
    f.Frames(1);
    CHECK(f.AI().Count() == 0);
}
TEST(AIGameplayQueryBudgetDefersWithoutCrashingOrMoving)
{
    AIFixture f;
    f.Configure(f.Spec());
    for (int i = 0; i < 128; ++i) f.Rules().Find(f.Hero, {});
    f.AI().Tick(.1f);
    CHECK(f.AI().State(f.NPC)->Status == "Query budget");
    CHECK(!f.NPC.GetComponent<NavAgentComponent>().HasDestination);
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Status != "Query budget");
}
TEST(AIGameplayRehomeAndInvalidAbsoluteRouteAreTransactional)
{
    AIFixture f;
    auto spec = f.Spec();
    spec.Relative = false;
    f.Configure(spec);
    auto home = f.AI().State(f.NPC)->Home;
    f.Move(f.NPC, { 9, 0, 0 });
    std::string error;
    CHECK(!f.AI().Reset(f.NPC, true, error));
    CHECK(f.AI().State(f.NPC)->Home == home);
}
TEST(AIGameplayConfigurationRejectsWrongDimensionAndConflictingPatrol)
{
    AIFixture f;
    auto spec = f.Spec();
    std::string error;
    spec.Brain.TwoD = false;
    CHECK(!f.AI().Configure(f.NPC, f.NPC, spec, false, error));
    spec = f.Spec();
    f.NPC.AddComponent<PatrolPathComponent>();
    CHECK(!f.AI().Configure(f.NPC, f.NPC, spec, false, error));
    CHECK(f.AI().Count() == 0);
}
TEST(AIGameplayHostDestructionReleasesAgentAndCrossSceneIsRejected)
{
    AIFixture f;
    auto owner = f.World.CreateEntity("Owner");
    std::string error;
    CHECK(f.AI().Configure(owner, f.NPC, f.Spec(), false, error));
    f.Frames(2);
    f.World.DestroyEntity(owner);
    f.Frames(1);
    CHECK(f.AI().Count() == 0 && !f.NPC.GetComponent<NavAgentComponent>().HasDestination);
    Scene other;
    auto e = other.CreateEntity();
    bool caught = false;
    try
    {
        f.AI().State(e);
    }
    catch (const std::exception&)
    {
        caught = true;
    }
    CHECK(caught);
}
TEST(AIGameplayExplicitStrikeCannotHitACloserDifferentActor)
{
    AIFixture f;
    f.Move(f.Hero, { 2, 0, 0 });
    auto bystander = f.World.CreateEntity();
    bystander.GetTransform().Translation = { 1, 0, 0 };
    std::string error;
    CHECK(f.Rules().Configure(bystander, { 100, 0, 0, 0, 1 }, false, error));
    Gameplay::RuleQuery q;
    q.TwoD = true;
    q.Origin = {};
    auto r = f.Rules().Strike(f.NPC, f.Hero, q, 25, 0, false);
    CHECK(r.Target == 200 && r.Damage.Applied == 25);
    CHECK(f.Rules().Health(bystander)->Current == 100);
}
TEST(AIGameplaySenseHonorsFOVNearRangeTagAndTeam)
{
    AIFixture f;
    f.Move(f.Hero, { -2, 0, 0 });
    Gameplay::RuleQuery q;
    q.TwoD = true;
    q.Origin = {};
    q.MinDot = .5;
    CHECK(f.Rules().Sense(f.NPC, q, "Player", {}, 0).Target == 0);
    CHECK(f.Rules().Sense(f.NPC, q, "Player", {}, 3).Target == 200);
    CHECK(f.Rules().Sense(f.NPC, q, "Wrong", {}, 3).Target == 0);
}
TEST(AIGameplayBlueprintStateAndEventsAreTyped)
{
    AIFixture f;
    Gameplay::Runtime runtime;
    CHECK(Call(runtime, f.NPC, "Gameplay.AIConfigure", { { "TwoD", true }, { "Tag", "Nobody" }, { "Points", { { 0, 1, 0 } } } }).at("Success") == true);
    f.Frames(1);
    CHECK(Call(runtime, f.NPC, "Gameplay.AIState").at("Mode") == "Patrol");
    CHECK(Call(runtime, f.NPC, "Gameplay.AIEvent").at("Has") == true);
    CHECK(Call(runtime, f.NPC, "Gameplay.AIEnable", { { "Enabled", false } }).at("Reason") == "");
    CHECK(Call(runtime, f.NPC, "Gameplay.AIRemove").at("Success") == true);
}
TEST(AIGameplayBrainCapacityAndEventTelemetryAreBounded)
{
    AIFixture f;
    auto spec = f.Spec();
    spec.Points.clear();
    spec.Tag = "Nobody";
    std::string error;
    for (int i = 0; i < 32; ++i)
    {
        auto e = f.World.CreateEntity();
        e.AddComponent<NavAgentComponent>().Grid.ID = 300;
        CHECK(f.Rules().Configure(e, { 100, 0, 0, 0, 2 }, false, error));
        CHECK(f.AI().Configure(e, e, spec, false, error));
    }
    CHECK(f.AI().Count() == 32);
    CHECK(!f.AI().Configure(f.NPC, f.NPC, spec, false, error));
    f.AI().Clear();
    CHECK(f.AI().Count() == 0);
    f.Configure(spec);
    for (int i = 0; i < 40; ++i) CHECK(f.AI().Enable(f.NPC, (i % 2) != 0));
    CHECK(f.AI().State(f.NPC)->Dropped == 8);
    int events = 0;
    while (f.AI().Poll(f.NPC)) ++events;
    CHECK(events == 32);
}
TEST(AIGameplayPlayRestartClearsContextAndRestoresAgentConfiguration)
{
    AIFixture f;
    auto original = f.NPC.GetComponent<NavAgentComponent>().RepathInterval;
    f.Configure(f.Spec());
    f.Frames(10);
    f.World.OnRuntimeStop();
    CHECK(f.NPC.GetComponent<NavAgentComponent>().RepathInterval == original);
    f.World.OnRuntimeStart();
    CHECK(f.AI().Count() == 0);
    CHECK(!f.Rules().Health(f.NPC));
}
TEST(AIGameplayBlueprintOwnerPauseAndDisableSuspendTheController)
{
    AIFixture f;
    const auto previous = Project::GetActive();
    Project::New();
    std::filesystem::create_directories(f.Root / "Assets/Scripts");
    CHECK(Project::SaveActive(f.Root / "AI.zproject"));
    Visual::Graph g;
    g.Nodes = { { 1, "Event.Start" }, { 2, "Gameplay.HealthConfigure", 0, 0, { { "Team", 2 } } }, { 3, "Gameplay.AIConfigure", 0, 0, { { "TwoD", true }, { "Tag", "Nobody" }, { "Points", { { 0, 2, 0 } } } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" } };
    CHECK(g.Save(f.Root / "Assets/Scripts/Brain.zgraph"));
    f.World.OnRuntimeStop();
    auto& script = f.NPC.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/Brain.zgraph"));
    f.World.OnRuntimeStart();
    f.Frames(10);
    auto* host = dynamic_cast<VisualGraphHost*>(script.Instance);
    CHECK(host && host->VM.Error().empty());
    CHECK(f.NPC.GetComponent<NavAgentComponent>().HasDestination);
    host->VM.Paused = true;
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Status == "Host suspended");
    CHECK(!f.NPC.GetComponent<NavAgentComponent>().HasDestination);
    host->VM.Paused = false;
    host->SetEnabled(false);
    f.Frames(1);
    CHECK(f.AI().State(f.NPC)->Status == "Host suspended");
    host->SetEnabled(true);
    f.Frames(1);
    CHECK(f.NPC.GetComponent<NavAgentComponent>().HasDestination);
    f.World.OnRuntimeStop();
    Project::SetActive(previous);
}
TEST(AIGameplayWorkshopGraphsAndPrefabReferencesValidate)
{
    auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/AIWorkshop/Assets/Scripts";
    int count = 0;
    for (const auto& e : std::filesystem::directory_iterator(root))
    {
        if (e.path().extension() != ".zgraph") continue;
        auto g = Visual::Graph::Load(e.path());
        CHECK(g.Validate().empty());
        CHECK(Visual::Graph::Parse(g.Serialize()).Validate().empty());
        ++count;
    }
    CHECK(count == 6);
    CHECK(std::filesystem::exists(root.parent_path() / "Prefabs/Reinforcement2D.zprefab"));
    CHECK(std::filesystem::exists(root.parent_path() / "Prefabs/Reinforcement3D.zprefab"));
}
TEST(AIGameplayDynamicThreeDBodyStopsImmediatelyOnDisable)
{
    AIFixture f(true);
    f.World.OnRuntimeStop();
    f.NPC.RemoveComponent<CharacterController3DComponent>();
    f.NPC.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Dynamic;
    auto& shape = f.NPC.AddComponent<BoxCollider3DComponent>();
    shape.HalfExtents = { .3f, .9f, .3f };
    shape.Offset = { 0, .9f, 0 };
    f.World.OnRuntimeStart();
    std::string error;
    CHECK(f.Rules().Configure(f.NPC, { 100, 0, 0, 0, 2 }, false, error));
    f.Configure(f.Spec());
    f.Frames(20);
    const auto id = f.NPC.GetComponent<Rigidbody3DComponent>().RuntimeBodyID;
    CHECK(f.World.GetPhysicsWorld3D()->GetLinearVelocity(id).z > .1f);
    CHECK(f.AI().Enable(f.NPC, false));
    const auto velocity = f.World.GetPhysicsWorld3D()->GetLinearVelocity(id);
    CHECK_NEAR(velocity.x, 0, 1e-5f);
    CHECK_NEAR(velocity.z, 0, 1e-5f);
}
TEST(AIGameplayOptionalSelfPinsResolveToTheGraphHost)
{
    AIFixture f;
    Gameplay::Runtime runtime;
    auto config = Call(runtime, f.NPC, "Gameplay.AIConfigure", { { "TwoD", true }, { "Victim", UINT64_MAX } });
    CHECK(config.at("Success") == false);
    CHECK(config.at("Reason").get<std::string>().find("self") != std::string::npos);
    auto damage = Call(runtime, f.NPC, "Gameplay.Damage", { { "Source", UINT64_MAX }, { "Amount", 1 } });
    CHECK(damage.at("Applied") == 1);
    CHECK(Call(runtime, f.NPC, "Gameplay.Sense", { { "TwoD", true }, { "Victim", UINT64_MAX } }).at("Visible") == false);
}
