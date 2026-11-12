#include "TestAssertions.h"
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Physics/PhysicsWorld2D.h>
#include <filesystem>
using namespace Zamin;
namespace {
    using V = Visual::Value;
    struct RulesFixture
    {
        Scene World;
        Entity Hero, Enemy, Item;
        std::filesystem::path Root;
        RulesFixture(bool motor = false)
        {
            static unsigned count = 0;
            Root = std::filesystem::temp_directory_path() / ("zamin-rules-" + std::to_string(++count));
            std::filesystem::create_directories(Root);
            SaveData::SetFilePath(Root / "save.json");
            SaveData::DeleteAll();
            World.SetName("RulesTest");
            Hero = World.CreateEntityWithUUID(UUID(100), "Hero");
            Enemy = World.CreateEntityWithUUID(UUID(200), "Enemy");
            Item = World.CreateEntityWithUUID(UUID(300), "Item");
            Enemy.GetTransform().Translation = { 2, 0, 0 };
            Item.GetTransform().Translation = { 1, 1, 0 };
            if (motor)
            {
                auto& b = Hero.AddComponent<Rigidbody2DComponent>();
                b.Type = Rigidbody2DComponent::BodyType::Dynamic;
                b.GravityScale = 0;
                Hero.AddComponent<BoxCollider2DComponent>();
            }
            World.OnRuntimeStart();
        }
        ~RulesFixture()
        {
            World.OnRuntimeStop();
            SaveData::SetFilePath({});
            std::filesystem::remove_all(Root);
        }
        Gameplay::RuleWorld& Rules() { return Gameplay::RuleWorld::For(World); }
        void Health(Entity e, int team = 0)
        {
            std::string error;
            CHECK(Rules().Configure(e, { 100, 25, .2, .1, uint16_t(team) }, false, error));
        }
        void Use(Entity e, Gameplay::InteractionSettings s = {})
        {
            std::string error;
            CHECK(Rules().ConfigureInteraction(e, s, false, error));
        }
        Gameplay::RuleQuery Query(bool three = false)
        {
            Gameplay::RuleQuery q;
            q.TwoD = !three;
            return q;
        }
        void Wall(bool three)
        {
            World.OnRuntimeStop();
            auto wall = World.CreateEntity("Blocking wall");
            wall.GetTransform().Translation = { .5f, 1, 0 };
            if (three)
            {
                wall.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
                wall.AddComponent<BoxCollider3DComponent>().HalfExtents = { .1f, 2, 2 };
            }
            else
            {
                wall.AddComponent<Rigidbody2DComponent>();
                wall.AddComponent<BoxCollider2DComponent>().Size = { .1f, 2 };
            }
            World.OnRuntimeStart();
        }
    };
    V GraphCall(Gameplay::Runtime& runtime, Entity host, const std::string& kind, std::map<std::string, V> overrides = {}, Entity target = {})
    {
        std::map<std::string, V> args;
        for (const auto& p : Visual::Catalog().at(kind).Inputs)
            if (p.Kind != Visual::Type::Exec) args[p.Name] = p.Default;
        for (auto& [key, value] : overrides) args[key] = value;
        return runtime.Dispatch(host, target ? target : host, kind, args);
    }
}
TEST(RulesWorldDifferentGraphHostsShareHealth)
{
    RulesFixture f;
    Gameplay::Runtime a, b;
    CHECK(GraphCall(a, f.Hero, "Gameplay.HealthConfigure").at("Success") == true);
    CHECK(GraphCall(b, f.Enemy, "Gameplay.Damage", { { "Amount", 30. } }, f.Hero).at("Applied") == 30);
    CHECK(GraphCall(a, f.Hero, "Gameplay.Health").at("Current") == 70);
    CHECK(GraphCall(b, f.Hero, "Gameplay.HealthConfigure").at("Success") == false);
    CHECK(GraphCall(a, f.Hero, "Gameplay.Health").at("Current") == 70);
}
TEST(RulesWorldFriendlyFireAndEnvironmentDamage)
{
    RulesFixture f;
    f.Health(f.Hero, 1);
    f.Health(f.Enemy, 1);
    CHECK(f.Rules().Damage(f.Hero, f.Enemy, 10, false, false, false).Applied == 0);
    CHECK(f.Rules().Damage(f.Hero, {}, 10, false, false, false).Applied == 10);
}
TEST(RulesWorldPauseDoesNotExpireDamageGrace)
{
    RulesFixture f;
    f.Health(f.Hero);
    f.Rules().Damage(f.Hero, {}, 10, false, false, false);
    f.World.SetTimeScale(0);
    f.World.UpdateRuntime(1);
    CHECK(f.Rules().Damage(f.Hero, {}, 10, false, false, false).Applied == 0);
    f.World.SetTimeScale(1);
    f.World.UpdateRuntime(.25f);
    CHECK(f.Rules().Damage(f.Hero, {}, 10, false, false, false).Applied == 10);
}
TEST(RulesWorldResetOnStopAndPlayRestart)
{
    RulesFixture f;
    f.Health(f.Hero);
    std::string error;
    CHECK(f.Rules().ChangeItem(f.Hero, "Coin", 3, false, error));
    f.World.OnRuntimeStop();
    f.World.OnRuntimeStart();
    CHECK(!f.Rules().Health(f.Hero));
    CHECK(f.Rules().Inventory(f.Hero).Items.empty());
}
TEST(RulesWorldPrunesDestroyedAndRecreatedUUID)
{
    RulesFixture f;
    f.Health(f.Hero);
    f.Use(f.Item);
    f.World.DestroyEntity(f.Hero);
    f.World.DestroyEntity(f.Item);
    auto replacement = f.World.CreateEntityWithUUID(UUID(100), "New hero");
    CHECK(!f.Rules().Health(replacement));
    f.World.UpdateRuntime(.01f);
    CHECK(f.Rules().ActorCount() == 0 && f.Rules().InteractionCount() == 0);
}
TEST(RulesWorldRejectsCrossSceneEntity)
{
    RulesFixture f;
    Scene other;
    auto e = other.CreateEntity();
    bool caught = false;
    try
    {
        f.Rules().Health(e);
    }
    catch (const std::exception&)
    {
        caught = true;
    }
    CHECK(caught);
}
TEST(RulesWorldDeathEmitsOneOrderedEdgeAndBoundedEvents)
{
    RulesFixture f;
    f.Health(f.Hero);
    auto r = f.Rules().Damage(f.Hero, {}, 1000, false, false, false);
    CHECK(r.Killed);
    f.Rules().Damage(f.Hero, {}, 1000, false, false, true);
    CHECK(f.Rules().Poll(f.Hero)->Kind == "Damage");
    CHECK(f.Rules().Poll(f.Hero)->Kind == "Died");
    CHECK(!f.Rules().Poll(f.Hero));
    std::string error;
    for (int i = 0; i < 40; ++i) CHECK(f.Rules().ChangeItem(f.Hero, "Coin", 1, false, error));
    CHECK(f.Rules().Dropped(f.Hero) == 8);
    int count = 0;
    while (f.Rules().Poll(f.Hero)) ++count;
    CHECK(count == 32);
}
TEST(RulesWorldDeadActorCannotMoveOrInteract)
{
    RulesFixture f(true);
    f.Health(f.Hero);
    f.Use(f.Item);
    f.Rules().Damage(f.Hero, {}, 1000, false, false, false);
    Gameplay::Runtime runtime;
    runtime.BeginFrame(f.World);
    GraphCall(runtime, f.Hero, "Gameplay.Motor2D", { { "Move", { 1., 1. } }, { "TopDown", true }, { "Delta", .1 } });
    CHECK(PhysicsWorld2D::GetLinearVelocity((b2Body*)f.Hero.GetComponent<Rigidbody2DComponent>().RuntimeBody) == glm::vec2(0));
    CHECK(!f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
}
TEST(RulesWorldInteractionCollectsOnlyOnceAcrossHosts)
{
    RulesFixture f;
    Gameplay::InteractionSettings s;
    s.RewardItem = "Key";
    s.Deactivate = true;
    f.Use(f.Item, s);
    CHECK(f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    CHECK(!f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Key") == 1);
    CHECK(!f.World.IsActiveInHierarchy(f.Item));
}
TEST(RulesWorldFailedInteractionDoesNotConsumeOrExhaust)
{
    RulesFixture f;
    std::string error;
    CHECK(f.Rules().ConfigureBag(f.Hero, 2, 1, false, error));
    CHECK(f.Rules().ChangeItem(f.Hero, "Key", 1, false, error));
    CHECK(f.Rules().ChangeItem(f.Hero, "Gem", 1, false, error));
    Gameplay::InteractionSettings s;
    s.RequiredItem = "Key";
    s.Consume = true;
    s.RewardItem = "Gem";
    f.Use(f.Item, s);
    CHECK(!f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Key") == 1);
    CHECK(f.Rules().ChangeItem(f.Hero, "Gem", 1, true, error));
    CHECK(f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Key") == 0);
}
TEST(RulesWorldFullHealthDoesNotWasteMedkitOrKey)
{
    RulesFixture f;
    f.Health(f.Hero);
    std::string error;
    CHECK(f.Rules().ChangeItem(f.Hero, "Key", 1, false, error));
    Gameplay::InteractionSettings s;
    s.Heal = 50;
    s.RequiredItem = "Key";
    s.Consume = true;
    f.Use(f.Item, s);
    CHECK(!f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Key") == 1);
    f.Rules().Damage(f.Hero, {}, 50, false, false, false);
    auto r = f.Rules().Interact(f.Hero, f.Item, f.Query());
    CHECK(r.Success && r.Healed == 25 && f.Rules().Health(f.Hero)->Current == 100);
}
TEST(RulesWorldInteractionCooldownUsesSharedSceneClock)
{
    RulesFixture f;
    Gameplay::InteractionSettings s;
    s.MaxUses = 0;
    s.Cooldown = .5;
    s.RewardItem = "Coin";
    f.Use(f.Item, s);
    CHECK(f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    CHECK(!f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    f.World.UpdateRuntime(.51f);
    CHECK(f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Coin") == 2);
}
TEST(RulesWorldFindReportsLockedPromptWithDeterministicTie)
{
    RulesFixture f;
    Gameplay::InteractionSettings s;
    s.RequiredItem = "Key";
    s.Prompt = "Unlock";
    f.Use(f.Item, s);
    auto other = f.World.CreateEntityWithUUID(UUID(301), "Tie");
    other.GetTransform() = f.Item.GetTransform();
    f.Use(other);
    auto result = f.Rules().Find(f.Hero, f.Query());
    CHECK(result.Target == 300 && !result.Success && result.Prompt == "Unlock");
}
TEST(RulesWorldInteractionRevalidatesDistanceAfterFind)
{
    RulesFixture f;
    f.Use(f.Item);
    CHECK(f.Rules().Find(f.Hero, f.Query()).Target == 300);
    f.Hero.GetTransform().Translation.x = -10;
    f.World.InvalidateWorldTransforms();
    CHECK(!f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
}
TEST(RulesWorldFacingFilterAndInvalidQuery)
{
    RulesFixture f;
    f.Use(f.Item);
    auto q = f.Query();
    q.MinDot = .5;
    q.Facing = { -1, 0, 0 };
    CHECK(f.Rules().Find(f.Hero, q).Target == 0);
    q.Facing = {};
    bool caught = false;
    try
    {
        f.Rules().Find(f.Hero, q);
    }
    catch (const std::exception&)
    {
        caught = true;
    }
    CHECK(caught);
}
TEST(RulesWorldTwoDLineOfSightAndBypass)
{
    RulesFixture f;
    f.Wall(false);
    f.Use(f.Item);
    auto q = f.Query();
    CHECK(f.Rules().Find(f.Hero, q).Target == 0);
    CHECK(!f.Rules().Interact(f.Hero, f.Item, q).Success);
    q.LineOfSight = false;
    CHECK(f.Rules().Interact(f.Hero, f.Item, q).Success);
}
TEST(RulesWorldThreeDLineOfSightUsesJolt)
{
    RulesFixture f;
    f.Wall(true);
    f.Use(f.Item);
    auto q = f.Query(true);
    CHECK(f.Rules().Find(f.Hero, q).Target == 0);
    q.LineOfSight = false;
    CHECK(f.Rules().Find(f.Hero, q).Target == 300);
}
TEST(RulesWorldMeleeUsesFacingTeamAndCooldown)
{
    RulesFixture f;
    f.Health(f.Hero, 1);
    f.Health(f.Enemy, 2);
    auto q = f.Query();
    q.MinDot = .5;
    auto r = f.Rules().Attack(f.Hero, q, 50, .4, false);
    CHECK(r.Target == 200 && r.Damage.Applied == 50);
    CHECK(f.Rules().Attack(f.Hero, q, 50, .4, false).Target == 0);
    f.World.UpdateRuntime(.41f);
    CHECK(f.Rules().Attack(f.Hero, q, 100, .4, false).Damage.Killed);
}
TEST(RulesWorldMissStillConsumesAttackCooldown)
{
    RulesFixture f;
    f.Health(f.Hero, 1);
    auto q = f.Query();
    CHECK(f.Rules().Attack(f.Hero, q, 50, .4, false).Target == 0);
    f.Health(f.Enemy, 2);
    CHECK(f.Rules().Attack(f.Hero, q, 50, .4, false).Reason == "Attack cooldown");
}
TEST(RulesWorldMeleeCannotHitThroughWall)
{
    RulesFixture f;
    f.Wall(false);
    f.Health(f.Hero, 1);
    f.Health(f.Enemy, 2);
    CHECK(f.Rules().Attack(f.Hero, f.Query(), 50, .4, false).Target == 0);
    CHECK(f.Rules().Health(f.Enemy)->Current == 100);
}
TEST(RulesWorldQueriesAndActorRecordsAreBounded)
{
    RulesFixture f;
    auto q = f.Query();
    for (int i = 0; i < 128; ++i) f.Rules().Find(f.Hero, q);
    bool caught = false;
    try
    {
        f.Rules().Find(f.Hero, q);
    }
    catch (const std::exception&)
    {
        caught = true;
    }
    CHECK(caught);
    f.World.UpdateRuntime(.01f);
    CHECK(f.Rules().Find(f.Hero, q).Target == 0);
    for (int i = 0; i < 512; ++i)
    {
        auto e = f.World.CreateEntity();
        f.Health(e);
    }
    CHECK(f.Rules().ActorCount() == 512);
    caught = false;
    try
    {
        f.Health(f.Hero);
    }
    catch (const std::exception&)
    {
        caught = true;
    }
    CHECK(caught);
}
TEST(RulesWorldRespawnRestoresCheckpointBeforeReviving)
{
    RulesFixture f(true);
    f.Health(f.Hero);
    Gameplay::Runtime runtime;
    CHECK(GraphCall(runtime, f.Hero, "Gameplay.CheckpointCapture", { { "Persistent", false } }).at("Success") == true);
    f.Rules().Damage(f.Hero, {}, 1000, false, false, false);
    f.Hero.GetTransform().Translation.x = 10;
    f.World.InvalidateWorldTransforms();
    CHECK(GraphCall(runtime, f.Hero, "Gameplay.Respawn", { { "Persistent", false } }).at("Success") == false);
    CHECK(f.Rules().Dead(f.Hero));
    f.World.UpdateRuntime(.11f);
    runtime.BeginFrame(f.World);
    auto result = GraphCall(runtime, f.Hero, "Gameplay.Respawn", { { "Persistent", false }, { "Clearance", 0. } });
    CHECK(result.at("Success") == true);
    CHECK(!f.Rules().Dead(f.Hero) && f.Rules().Health(f.Hero)->Current == 100);
    CHECK_NEAR(f.World.GetWorldPosition(f.Hero).x, 0, 1e-5f);
}
TEST(RulesWorldMissingCheckpointCannotPartiallyRevive)
{
    RulesFixture f;
    f.Health(f.Hero);
    f.Rules().Damage(f.Hero, {}, 1000, false, false, false);
    f.World.UpdateRuntime(.11f);
    Gameplay::Runtime runtime;
    auto r = GraphCall(runtime, f.Hero, "Gameplay.Respawn", { { "Persistent", false } });
    CHECK(r.at("Success") == false && f.Rules().Dead(f.Hero));
    CHECK(f.Rules().Health(f.Hero)->Current == 0);
}
TEST(RulesWorldBlueprintInventoryAndEventPinsAreTyped)
{
    RulesFixture f;
    Gameplay::Runtime runtime;
    CHECK(GraphCall(runtime, f.Hero, "Gameplay.InventoryAdd", { { "Item", "Key" }, { "Count", 2 } }).at("Success") == true);
    CHECK(GraphCall(runtime, f.Hero, "Gameplay.InventoryCount", { { "Item", "Key" } }).at("Count") == 2);
    auto event = GraphCall(runtime, f.Hero, "Gameplay.RuleEvent");
    CHECK(event.at("Has") == true && event.at("Kind") == "InventoryChanged" && event.at("Item") == "Key");
    CHECK(GraphCall(runtime, f.Hero, "Gameplay.RuleEvent").at("Has") == false);
}
TEST(RulesWorkshopGraphsRoundTripAndValidate)
{
    auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/RulesWorkshop/Assets/Scripts";
    size_t count = 0;
    for (const auto& item : std::filesystem::directory_iterator(root))
    {
        if (item.path().extension() != ".zgraph") continue;
        auto graph = Visual::Graph::Load(item.path());
        CHECK(graph.Validate().empty());
        CHECK(Visual::Graph::Parse(graph.Serialize()).Validate().empty());
        ++count;
    }
    CHECK(count == 19);
}
TEST(RulesWorldInteractionInventoryEventsReflectCommittedExchange)
{
    RulesFixture f;
    std::string error;
    CHECK(f.Rules().ChangeItem(f.Hero, "Key", 1, false, error));
    while (f.Rules().Poll(f.Hero)) {}
    Gameplay::InteractionSettings s;
    s.RequiredItem = "Key";
    s.Consume = true;
    s.RewardItem = "Gem";
    f.Use(f.Item, s);
    CHECK(f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    CHECK(f.Rules().Poll(f.Hero)->Kind == "Interacted");
    auto removed = f.Rules().Poll(f.Hero);
    CHECK(removed->Kind == "InventoryChanged" && removed->Item == "Key" && removed->Amount == -1);
    auto added = f.Rules().Poll(f.Hero);
    CHECK(added->Kind == "InventoryChanged" && added->Item == "Gem" && added->Amount == 1);
    CHECK(f.Rules().Poll(f.Hero)->Kind == "Collected");
    CHECK(!f.Rules().Poll(f.Hero));
}
TEST(RulesWorldInvalidBridgeNumbersCannotChangeInventory)
{
    RulesFixture f;
    Gameplay::Runtime runtime;
    GraphCall(runtime, f.Hero, "Gameplay.InventoryAdd");
    bool caught = false;
    try
    {
        GraphCall(runtime, f.Hero, "Gameplay.InventoryRemove", { { "Count", .5 } });
    }
    catch (const std::exception&)
    {
        caught = true;
    }
    CHECK(caught);
    CHECK(f.Rules().Inventory(f.Hero).Count("Coin") == 1);
}
TEST(RulesWorldRecreatedInteractionDoesNotInheritUses)
{
    RulesFixture f;
    f.Use(f.Item);
    CHECK(f.Rules().Interact(f.Hero, f.Item, f.Query()).Success);
    f.World.DestroyEntity(f.Item);
    auto replacement = f.World.CreateEntityWithUUID(UUID(300), "New item");
    replacement.GetTransform().Translation = { 1, 1, 0 };
    f.Use(replacement);
    CHECK(f.Rules().Interact(f.Hero, replacement, f.Query()).Success);
}
TEST(RulesWorldPausedFrameRefreshesQueryBudgetWithoutAdvancingTimers)
{
    RulesFixture f;
    f.Health(f.Hero);
    f.Use(f.Item);
    f.Rules().Damage(f.Hero, {}, 10, false, false, false);
    auto query = f.Query();
    for (int i = 0; i < 128; ++i) f.Rules().Find(f.Hero, query);
    f.World.SetTimeScale(0);
    f.World.DestroyEntity(f.Item);
    f.World.UpdateRuntime(1);
    CHECK(f.Rules().Find(f.Hero, query).Target == 0);
    CHECK(f.Rules().InteractionCount() == 0);
    CHECK(f.Rules().Now() == 0);
    CHECK(f.Rules().Damage(f.Hero, {}, 10, false, false, false).Applied == 0);
}
TEST(RulesWorldLethalDamageStopsIntentWithoutAnotherMotorInvocation)
{
    RulesFixture f(true);
    f.Health(f.Hero);
    auto* body = (b2Body*)f.Hero.GetComponent<Rigidbody2DComponent>().RuntimeBody;
    PhysicsWorld2D::SetLinearVelocity(body, { 5, 4 });
    CHECK(f.Rules().Damage(f.Hero, {}, 1000, false, false, false).Killed);
    CHECK(PhysicsWorld2D::GetLinearVelocity(body) == glm::vec2(0));
    auto character = f.World.CreateEntity("Character intent");
    character.AddComponent<CharacterController3DComponent>().MoveVelocity = { 3, 0, 2 };
    f.Health(character);
    CHECK(f.Rules().Damage(character, {}, 1000, false, false, false).Killed);
    CHECK(character.GetComponent<CharacterController3DComponent>().MoveVelocity == glm::vec3(0));
}
TEST(RulesWorldRespawnInvalidatesMotorStateWhenDeadFramesSkipTheMotor)
{
    RulesFixture f(true);
    f.Health(f.Hero);
    Gameplay::Runtime runtime;
    runtime.BeginFrame(f.World);
    CHECK(GraphCall(runtime, f.Hero, "Gameplay.Motor2D", { { "Move", { -1., 0. } }, { "Delta", .1 } }).at("Facing") == -1);
    f.Rules().Damage(f.Hero, {}, 1000, false, false, false);
    f.World.UpdateRuntime(.2f); // The graph's dead branch deliberately does not invoke Motor.
    std::string error;
    CHECK(f.Rules().Revive(f.Hero, 1, error));
    runtime.BeginFrame(f.World);
    CHECK(GraphCall(runtime, f.Hero, "Gameplay.Motor2D", { { "Move", { 0., 0. } }, { "Delta", .1 } }).at("Facing") == 1);
}
