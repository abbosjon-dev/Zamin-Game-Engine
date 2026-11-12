#include <Zamin/Gameplay/GameplayRules.h>
#include "TestAssertions.h"
#include <Zamin/Gameplay/SaveWorld.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Physics/PhysicsWorld2D.h>
#include <Zamin/Physics/PhysicsWorld3D.h>
#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Project/Project.h>
using namespace Zamin;
namespace {
    struct SaveFixture
    {
        Scene World;
        Entity Hero, NPC, Door, Grid;
        std::filesystem::path Root;
        bool Three;
        SaveFixture(bool three = false, uint64_t offset = 0) : Three(three)
        {
            Root = std::filesystem::temp_directory_path() / ("zamin-save-" + std::to_string(uint64_t(UUID())));
            std::filesystem::create_directories(Root);
            SaveData::SetFilePath(Root / "prefs.json");
            World.SetName("SaveLab");
            Hero = World.CreateEntityWithUUID(UUID(100 + offset), "Hero");
            NPC = World.CreateEntityWithUUID(UUID(200 + offset), "NPC");
            Door = World.CreateEntityWithUUID(UUID(300 + offset), "Door");
            Grid = World.CreateEntityWithUUID(UUID(400), "Grid");
            Hero.AddTag("Player");
            Hero.GetTransform().Translation = { 5, 0, 0 };
            Door.GetTransform().Translation = { 6, 0, 0 };
            if (three)
            {
                auto floor = World.CreateEntity();
                floor.GetTransform().Translation = { 0, -.5f, 0 };
                floor.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
                floor.AddComponent<BoxCollider3DComponent>().HalfExtents = { 10, .5f, 10 };
                auto& g = Grid.AddComponent<NavGrid3DComponent>();
                g.Size = { 20, 4, 20 };
                g.AgentRadius = .1f;
                Grid.GetTransform().Translation.y = 1;
                Hero.AddComponent<CharacterController3DComponent>();
                NPC.AddComponent<CharacterController3DComponent>();
            }
            else
            {
                auto& g = Grid.AddComponent<NavGrid2DComponent>();
                g.AutoBounds = false;
                g.Size = { 20, 20 };
                g.CellSize = .5f;
                g.UseTilemaps = g.UseColliders = false;
                g.AgentRadius = 0;
                for (auto e : { Hero, NPC })
                {
                    auto& b = e.AddComponent<Rigidbody2DComponent>();
                    b.Type = Rigidbody2DComponent::BodyType::Dynamic;
                    b.GravityScale = 0;
                    e.AddComponent<BoxCollider2DComponent>().Size = { .2f, .2f };
                }
            }
            NPC.AddComponent<NavAgentComponent>().Grid.ID = 400;
            World.OnRuntimeStart();
            std::string error;
            auto& r = Rules();
            CHECK(r.Configure(Hero, { 100, 10, .5, .5, 1 }, false, error));
            CHECK(r.Configure(NPC, { 60, 0, 0, .5, 2 }, false, error));
            CHECK(r.ConfigureBag(Hero, 4, 10, false, error));
            Gameplay::InteractionSettings use;
            use.RequiredItem = "Key";
            use.RewardItem = "Coin";
            use.Consume = true;
            use.Deactivate = true;
            use.Cooldown = .5;
            CHECK(r.ConfigureInteraction(Door, use, false, error));
            CHECK(Saves().Bind(Hero, "Player", error));
            CHECK(Saves().Bind(NPC, "Guard", error));
            CHECK(Saves().Bind(Door, "Door", error));
            Gameplay::AISpec spec;
            spec.Brain.TwoD = !three;
            spec.Brain.FieldOfView = 360;
            spec.Eye = { 0, 0, 0 };
            spec.Target = Hero.GetUUID();
            spec.Points = { { 0, 1, 0 } };
            if (three) spec.Points = { { 0, 0, 1 } };
            CHECK(Gameplay::AIWorld::For(World).Configure(NPC, NPC, spec, false, error));
        }
        ~SaveFixture()
        {
            World.OnRuntimeStop();
            SaveData::SetFilePath({});
            std::filesystem::remove_all(Root);
        }
        Gameplay::SaveWorld& Saves() { return Gameplay::SaveWorld::For(World); }
        Gameplay::RuleWorld& Rules() { return Gameplay::RuleWorld::For(World); }
        void Advance(float delta = .1f) { World.UpdateRuntime(Timestep(delta)); }
        Gameplay::GameSave Progress()
        {
            std::string error;
            CHECK(Rules().ChangeItem(Hero, "Key", 1, false, error));
            Gameplay::RuleQuery q;
            q.TwoD = !Three;
            q.LineOfSight = false;
            q.Origin = {};
            CHECK(Rules().Interact(Hero, Door, q).Success);
            Rules().Damage(Hero, NPC, 25, true, false, true);
            Gameplay::AIWorld::For(World).Tick(.1f);
            return Saves().Capture("V1");
        }
    };
    Visual::Value Call(Gameplay::Runtime& r, Entity host, const std::string& kind, std::map<std::string, Visual::Value> overrides = {})
    {
        std::map<std::string, Visual::Value> args;
        for (auto& pin : Visual::Catalog().at(kind).Inputs)
            if (pin.Kind != Visual::Type::Exec) args[pin.Name] = pin.Default;
        for (auto& [k, v] : overrides) args[k] = v;
        return r.Dispatch(host, host, kind, args);
    }
}
TEST(GameSaveWorldRestoresHealthBagDoorAndAIWithoutReplayRewards)
{
    SaveFixture f;
    auto save = f.Progress();
    std::string error;
    f.Rules().Heal(f.Hero, 100, false);
    CHECK(f.Rules().ChangeItem(f.Hero, "Coin", 3, false, error));
    f.World.SetActive(f.Door, true);
    f.Hero.GetTransform().Translation = { 8, 0, 0 };
    CHECK(Gameplay::AIWorld::For(f.World).Enable(f.NPC, false));
    auto result = f.Saves().Restore(save, "V1");
    CHECK(result.Success);
    CHECK(result.Entities == 3);
    CHECK_NEAR(f.Rules().Health(f.Hero)->Current, 85, 1e-6);
    CHECK(f.Rules().Inventory(f.Hero).Count("Coin") == 1);
    CHECK(!f.Door.IsActive());
    CHECK_NEAR(f.Hero.GetTransform().Translation.x, 5, 1e-6);
    CHECK(Gameplay::AIWorld::For(f.World).State(f.NPC)->Enabled);
    Gameplay::RuleQuery q;
    q.TwoD = true;
    q.LineOfSight = false;
    f.World.SetActive(f.Door, true);
    CHECK(!f.Rules().Interact(f.Hero, f.Door, q).Success);
    CHECK(f.Rules().Inventory(f.Hero).Count("Coin") == 1);
}
TEST(GameSaveWorldStableKeysRemapDifferentUUIDsAndAITargets)
{
    Gameplay::GameSave save;
    {
        SaveFixture original;
        save = original.Progress();
    }
    SaveFixture restored(false, 1000);
    CHECK(restored.Saves().Restore(save, "V1").Success);
    auto state = Gameplay::AIWorld::For(restored.World).State(restored.NPC);
    CHECK(state->Target == uint64_t(restored.Hero.GetUUID()));
    CHECK(restored.Rules().Inventory(restored.Hero).Count("Coin") == 1);
    CHECK_NEAR(restored.Rules().Health(restored.Hero)->Current, 85, 1e-6);
}
TEST(GameSaveWorldInvalidLateEntityCannotPartiallyMutateHealthPoseOrAI)
{
    SaveFixture f;
    auto save = f.Progress();
    save.Entities.back().Signature = "changed";
    auto before = f.Saves().Capture("V1");
    auto result = f.Saves().Restore(save, "V1");
    CHECK(!result.Success);
    CHECK(EncodeGameSave(before) == EncodeGameSave(f.Saves().Capture("V1")));
}
TEST(GameSaveWorldWrongSceneSchemaAndBindingSetLeaveStateUntouched)
{
    SaveFixture f;
    auto save = f.Progress();
    auto before = EncodeGameSave(f.Saves().Capture("V1"));
    CHECK(!f.Saves().Restore(save, "V2").Success);
    save.Scene = "Other";
    CHECK(!f.Saves().Restore(save, "V1").Success);
    save.Scene = "SaveLab";
    save.Entities.pop_back();
    CHECK(!f.Saves().Restore(save, "V1").Success);
    CHECK(before == EncodeGameSave(f.Saves().Capture("V1")));
}
TEST(GameSaveWorldRecreatedUUIDCannotInheritABindingUntilExplicitRebind)
{
    SaveFixture f;
    auto save = f.Saves().Capture("V1");
    auto id = f.Door.GetUUID();
    f.World.DestroyEntity(f.Door);
    auto replacement = f.World.CreateEntityWithUUID(id, "Replacement");
    CHECK(!f.Saves().Find("Door"));
    CHECK(!f.Saves().Restore(save, "V1").Success);
    std::string error;
    CHECK(f.Saves().Bind(replacement, "Door", error));
    CHECK(f.Saves().Find("Door") == replacement);
}
TEST(GameSaveWorldDestroyedSlotsCaptureTombstonesAndDeactivateRecreatedSlots)
{
    SaveFixture f;
    f.World.DestroyEntity(f.Door);
    auto save = f.Saves().Capture("V1");
    auto tomb = std::find_if(save.Entities.begin(), save.Entities.end(), [](auto& e) { return e.Key == "Door"; });
    CHECK(tomb != save.Entities.end() && !tomb->Present);
    auto replacement = f.World.CreateEntity("NewDoor");
    std::string error;
    CHECK(f.Saves().Bind(replacement, "Door", error));
    CHECK(f.Saves().Restore(save, "V1").Success);
    CHECK(!replacement.IsActive());
}
TEST(GameSaveWorldTimersRebaseToCurrentClockAndDeadRespawnDelaySurvives)
{
    SaveFixture f;
    f.Rules().Damage(f.Hero, f.NPC, 1000, true, false, true);
    f.Rules().Tick(.2f);
    auto save = f.Saves().Capture("V1");
    f.Rules().Tick(20);
    CHECK(f.Saves().Restore(save, "V1").Success);
    auto h = f.Rules().Health(f.Hero);
    CHECK(h->Dead);
    CHECK_NEAR(h->Settings.RespawnDelay - (f.Rules().Now() - h->DiedAt), .3, 1e-5);
    std::string error;
    CHECK(!f.Rules().CanRevive(f.Hero, 0, error));
    f.Rules().Tick(.31f);
    CHECK(f.Rules().CanRevive(f.Hero, 0, error));
}
TEST(GameSaveWorldRestoreClearsTransientRuleAndAIEvents)
{
    SaveFixture f;
    auto save = f.Progress();
    CHECK(f.Rules().Poll(f.Hero).has_value());
    CHECK(f.Saves().Restore(save, "V1").Success);
    CHECK(!f.Rules().Poll(f.Hero));
    CHECK(!Gameplay::AIWorld::For(f.World).Poll(f.NPC));
}
TEST(GameSaveWorldLocal2DBodyTransformAndVelocitiesMatchRestoredPose)
{
    SaveFixture f;
    auto save = f.Saves().Capture("V1");
    auto* body = (b2Body*)f.Hero.GetComponent<Rigidbody2DComponent>().RuntimeBody;
    PhysicsWorld2D::SetTransform(body, { 8, 2 }, 1);
    PhysicsWorld2D::SetLinearVelocity(body, { 5, 2 });
    CHECK(f.Saves().Restore(save, "V1").Success);
    CHECK_NEAR(PhysicsWorld2D::GetPosition(body).x, 5, 1e-5);
    CHECK_NEAR(PhysicsWorld2D::GetPosition(body).y, 0, 1e-5);
    CHECK_NEAR(glm::length(PhysicsWorld2D::GetLinearVelocity(body)), 0, 1e-6);
}
TEST(GameSaveWorldThreeDCharacterRestoreClearsControllerIntent)
{
    SaveFixture f(true);
    auto save = f.Saves().Capture("V1");
    auto& c = f.Hero.GetComponent<CharacterController3DComponent>();
    c.MoveVelocity = { 5, 0, 2 };
    c.VerticalVelocity = 10;
    f.World.GetPhysicsWorld3D()->SetCharacterPosition(c.RuntimeID, { 8, 3, 0 });
    CHECK(f.Saves().Restore(save, "V1").Success);
    CHECK_NEAR(glm::length(c.MoveVelocity), 0, 1e-6);
    CHECK_NEAR(c.VerticalVelocity, 0, 1e-6);
    CHECK_NEAR(f.World.GetPhysicsWorld3D()->GetCharacterPosition(c.RuntimeID).x, 5, 1e-5);
}
TEST(GameSaveWorldPhysicalScaleChangesAndParentChangesAreRejected)
{
    SaveFixture f;
    auto save = f.Saves().Capture("V1");
    save.Entities[2].Scale.x = 2;
    CHECK(!f.Saves().Restore(save, "V1").Success);
    save = f.Saves().Capture("V1");
    auto parent = f.World.CreateEntity();
    CHECK(f.World.SetParent(f.Hero, parent));
    CHECK(!f.Saves().Restore(save, "V1").Success);
}
TEST(GameSaveWorldBindingRejectsChildrenDuplicatesForeignSceneAndUnsafeKeys)
{
    SaveFixture f;
    std::string error;
    CHECK(!f.Saves().Bind(f.Hero, "Other", error));
    CHECK(!f.Saves().Bind(f.NPC, "Player", error));
    CHECK(!f.Saves().Bind(f.Door, "../bad", error));
    Scene other;
    auto foreign = other.CreateEntity();
    CHECK(!f.Saves().Bind(foreign, "Foreign", error));
    auto child = f.World.CreateEntity();
    f.World.SetParent(child, f.Hero);
    CHECK(!f.Saves().Bind(child, "Child", error));
}
TEST(GameSaveWorldBindingAndIOBudgetsAreSceneSharedAndBounded)
{
    SaveFixture f;
    std::string error;
    for (int i = 3; i < 256; ++i) CHECK(f.Saves().Bind(f.World.CreateEntity(), "Object" + std::to_string(i), error));
    CHECK(!f.Saves().Bind(f.World.CreateEntity(), "Overflow", error));
    CHECK(f.Saves().Save("Budget", "V1").Success);
    CHECK(f.Saves().Inspect("Budget").Success);
    CHECK(!f.Saves().Load("Budget", "V1").Success);
    f.Saves().BeginFrame();
    CHECK(f.Saves().Load("Budget", "V1").Success);
}
TEST(GameSaveWorldBackupRecoversCorruptedPrimaryAndPreservesLastGoodProgress)
{
    SaveFixture f;
    auto save = f.Progress();
    CHECK(f.Saves().Save("Quick", "V1").Success);
    f.Rules().Heal(f.Hero, 10, false);
    CHECK(f.Saves().Save("Quick", "V1").Success);
    CHECK(FileSystem::WriteTextFile(f.Saves().Path("Quick"), "broken"));
    f.Saves().BeginFrame();
    auto result = f.Saves().Load("Quick", "V1");
    CHECK(result.Success && result.Recovered);
    CHECK_NEAR(f.Rules().Health(f.Hero)->Current, 85, 1e-6);
    CHECK(f.Rules().Inventory(f.Hero).Count("Coin") == 1);
}
TEST(GameSaveWorldCorruptedPrimaryIsNeverPromotedOverValidBackup)
{
    SaveFixture f;
    CHECK(f.Saves().Save("Quick", "V1").Success);
    f.Rules().Damage(f.Hero, f.NPC, 25, true, false, true);
    CHECK(f.Saves().Save("Quick", "V1").Success);
    auto backup = FileSystem::ReadTextFile(f.Saves().Path("Quick").string() + ".bak");
    CHECK(FileSystem::WriteTextFile(f.Saves().Path("Quick"), "corrupt"));
    f.Saves().BeginFrame();
    CHECK(f.Saves().Save("Quick", "V1").Success);
    CHECK(backup == FileSystem::ReadTextFile(f.Saves().Path("Quick").string() + ".bak"));
}
TEST(GameSaveWorldBackupWriteFailureKeepsPrimaryUntouched)
{
    SaveFixture f;
    CHECK(f.Saves().Save("Quick", "V1").Success);
    auto path = f.Saves().Path("Quick");
    auto before = FileSystem::ReadTextFile(path);
    std::filesystem::create_directory(path.string() + ".bak");
    CHECK(!f.Saves().Save("Quick", "V1").Success);
    CHECK(before == FileSystem::ReadTextFile(path));
}
TEST(GameSaveWorldBothInvalidFilesAndOversizedFilesDoNotMutateState)
{
    SaveFixture f;
    auto save = f.Progress();
    auto before = EncodeGameSave(save);
    auto path = f.Saves().Path("Bad");
    CHECK(FileSystem::WriteTextFileAtomic(path, std::string(2 * 1024 * 1024 + 1, 'x')));
    CHECK(FileSystem::WriteTextFileAtomic(path.string() + ".bak", "{}"));
    CHECK(!f.Saves().Load("Bad", "V1").Success);
    CHECK(before == EncodeGameSave(f.Saves().Capture("V1")));
}
TEST(GameSaveWorldChangedConfigurationDoesNotSilentlyFallBackToOldBackup)
{
    SaveFixture f;
    CHECK(f.Saves().Save("Quick", "V1").Success);
    CHECK(f.Saves().Save("Quick", "V1").Success);
    std::string error;
    CHECK(f.Rules().Configure(f.Hero, { 200, 10, .5, .5, 1 }, true, error));
    f.Saves().BeginFrame();
    auto result = f.Saves().Load("Quick", "V1");
    CHECK(!result.Success && !result.Recovered);
    CHECK(f.Rules().Health(f.Hero)->Current == 200);
}
TEST(GameSaveWorldPlayRestartClearsBindings)
{
    SaveFixture f;
    CHECK(f.Saves().Count() == 3);
    f.World.OnRuntimeStop();
    f.World.OnRuntimeStart();
    CHECK(f.Saves().Count() == 0);
    CHECK(!f.Saves().Find("Player"));
}
TEST(GameSaveWorldBlueprintSaveBindFindInspectLoadOutputsAreTyped)
{
    SaveFixture f;
    Gameplay::Runtime runtime;
    CHECK(Call(runtime, f.Hero, "Gameplay.SaveFind", { { "Key", "Player" } }).at("Entity") == uint64_t(f.Hero.GetUUID()));
    CHECK(Call(runtime, f.Hero, "Gameplay.SaveWrite", { { "Slot", "Graph" } }).at("Success") == true);
    CHECK(Call(runtime, f.Hero, "Gameplay.SaveInspect", { { "Slot", "Graph" } }).at("Entities") == 3);
    f.Saves().BeginFrame();
    CHECK(Call(runtime, f.Hero, "Gameplay.SaveLoad", { { "Slot", "Graph" } }).at("Success") == true);
    CHECK(Call(runtime, f.Hero, "Gameplay.SaveUnbind", { { "Key", "Door" } }).at("Success") == true);
}
TEST(GameSaveWorldManagedPrefabSlotRebindsARecreatedInstanceBeforeRestore)
{
    SaveFixture f;
    auto previous = Project::GetActive();
    Project::New();
    std::filesystem::create_directories(f.Root / "Assets/Prefabs");
    CHECK(Project::SaveActive(f.Root / "Save.zproject"));
    auto prototype = f.World.CreateEntity("Prototype");
    CHECK(f.World.SavePrefab(prototype, f.Root / "Assets/Prefabs/Guard.zprefab"));
    std::string error;
    auto first = f.Saves().Spawn("Spawned", "Prefabs/Guard.zprefab", { 3, 2, 0 }, error);
    CHECK(first);
    if (first)
    {
        auto original = first.GetUUID();
        CHECK(f.Rules().Configure(first, { 40, 0, 0, .5, 2 }, false, error));
        f.Rules().Damage(first, f.Hero, 10, true, false, true);
        auto save = f.Saves().Capture("V1");
        CHECK(f.Saves().Spawn("Spawned", "Prefabs/Guard.zprefab", {}, error) == first);
        f.World.DestroyEntity(first);
        auto fresh = f.Saves().Spawn("Spawned", "Prefabs/Guard.zprefab", {}, error);
        CHECK(fresh);
        if (fresh)
        {
            CHECK(fresh.GetUUID() != original);
            CHECK(f.Rules().Configure(fresh, { 40, 0, 0, .5, 2 }, false, error));
            CHECK(f.Saves().Restore(save, "V1").Success);
            CHECK(f.Rules().Health(fresh)->Current == 30);
            CHECK_NEAR(fresh.GetTransform().Translation.x, 3, 1e-5);
        }
    }
    f.World.OnRuntimeStop();
    Project::SetActive(previous);
}
TEST(GameSaveWorldManagedSpawnRejectsTraversalAndEnforcesEightPerFrame)
{
    SaveFixture f;
    auto previous = Project::GetActive();
    Project::New();
    std::filesystem::create_directories(f.Root / "Assets/Prefabs");
    CHECK(Project::SaveActive(f.Root / "Save.zproject"));
    auto prototype = f.World.CreateEntity("Prototype");
    CHECK(f.World.SavePrefab(prototype, f.Root / "Assets/Prefabs/Guard.zprefab"));
    std::string error;
    CHECK(!f.Saves().Spawn("Unsafe", "../Outside.zprefab", {}, error));
    f.Saves().BeginFrame();
    for (int i = 0; i < 8; ++i) CHECK(f.Saves().Spawn("Spawn" + std::to_string(i), "Prefabs/Guard.zprefab", {}, error));
    CHECK(!f.Saves().Spawn("Ninth", "Prefabs/Guard.zprefab", {}, error));
    f.Saves().BeginFrame();
    CHECK(f.Saves().Spawn("Ninth", "Prefabs/Guard.zprefab", {}, error));
    f.World.OnRuntimeStop();
    Project::SetActive(previous);
}
TEST(GameSaveWorldWorkshopGraphsValidateAndRoundtrip)
{
    auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/SaveWorkshop/Assets/Scripts";
    int count = 0;
    for (auto& e : std::filesystem::directory_iterator(root))
        if (e.path().extension() == ".zgraph")
        {
            auto g = Visual::Graph::Load(e.path());
            CHECK(g.Validate().empty());
            CHECK(Visual::Graph::Parse(g.Serialize()).Validate().empty());
            ++count;
        }
    CHECK(count == 6);
}
TEST(GameSaveWorldForgedHealthAndInteractionProgressAreRejectedBeforeCommit)
{
    SaveFixture f;
    auto save = f.Progress();
    auto before = EncodeGameSave(save);
    for (auto& e : save.Entities)
        if (e.Key == "Player") e.Health = 1001;
    CHECK(!f.Saves().Restore(save, "V1").Success);
    CHECK(EncodeGameSave(f.Saves().Capture("V1")) == before);
    save = f.Saves().Capture("V1");
    for (auto& e : save.Entities)
        if (e.Key == "Door") e.Uses = 999;
    CHECK(!f.Saves().Restore(save, "V1").Success);
    CHECK(EncodeGameSave(f.Saves().Capture("V1")) == before);
}
TEST(GameSaveWorldSlotsRejectPathEscapeAndMissingPrimaryDoesNotMutate)
{
    SaveFixture f;
    auto before = EncodeGameSave(f.Saves().Capture("V1"));
    CHECK(!f.Saves().Save("../escape", "V1").Success);
    CHECK(!f.Saves().Load("Missing", "V1").Success);
    CHECK(EncodeGameSave(f.Saves().Capture("V1")) == before);
    CHECK(!std::filesystem::exists(f.Root / "escape.json"));
}
TEST(GameSaveWorldAIHomeAndPatrolMemoryRestoreAfterRehome)
{
    SaveFixture f;
    auto save = f.Progress();
    auto before = Gameplay::AIWorld::For(f.World).State(f.NPC);
    f.NPC.GetTransform().Translation = { 2, 0, 0 };
    std::string error;
    CHECK(Gameplay::AIWorld::For(f.World).Reset(f.NPC, true, error));
    CHECK(f.Saves().Restore(save, "V1").Success);
    auto after = Gameplay::AIWorld::For(f.World).State(f.NPC);
    CHECK(before->Home == after->Home);
    CHECK(before->Brain.Mode == after->Brain.Mode);
    CHECK(before->Brain.Remember == after->Brain.Remember);
    CHECK(before->Brain.Arrivals == after->Brain.Arrivals);
}
