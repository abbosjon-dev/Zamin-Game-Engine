#include "TestAssertions.h"
#include <Zamin/Gameplay/GameplayUI.h>
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Gameplay/SaveWorld.h>
#include <Zamin/Core/Input.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
using namespace Zamin;
namespace {
    struct UIFixture
    {
        Scene World;
        Entity Owner, Actor, Canvas, A, B, A1, A2, B1, Outside, HP, Bar;
        Ref<Project> Previous;
        std::filesystem::path Root;
        UIFixture()
        {
            Input::ResetState();
            Previous = Project::GetActive();
            Project::New();
            World.SetName("UI Lab");
            World.OnViewportResize(800, 600);
            Root = std::filesystem::temp_directory_path() / ("zamin-game-ui-" + std::to_string(uint64_t(UUID())));
            std::filesystem::create_directories(Root / "Assets/Scripts");
            SaveData::SetFilePath(Root / "prefs.json");
            CHECK(Project::SaveActive(Root / "UI.zproject"));
            Owner = World.CreateEntity("Controller");
            Actor = World.CreateEntity("Hero");
            Canvas = World.CreateEntity("Canvas");
            Canvas.AddComponent<UICanvasComponent>().ReferenceResolution = { 800, 600 };
            A = Rect("Inventory", Canvas, { 0, 0 }, { 460, 380 });
            B = Rect("Pause", Canvas, { 0, 0 }, { 480, 400 });
            A.AddComponent<UIImageComponent>();
            B.AddComponent<UIImageComponent>();
            A1 = Button("Apple", A, { 0, 80 });
            A2 = Button("Coin", A, { 0, 20 });
            B1 = Button("Resume", B, { 0, 50 });
            Outside = Button("Background", Canvas, { -280, 0 });
            HP = Rect("Vitals", Canvas, { -200, 250 }, { 250, 40 });
            HP.AddComponent<TextComponent>().Text = "Authored health";
            Bar = Rect("Health bar", Canvas, { -200, 200 }, { 200, 25 });
            Bar.AddComponent<UIProgressBarComponent>().Value = .3f;
            auto& n = A1.AddComponent<UINavigationComponent>();
            n.NavigationMode = UINavigationComponent::Mode::Explicit;
            n.Down.ID = A2.GetUUID();
            auto& m = A2.AddComponent<UINavigationComponent>();
            m.NavigationMode = UINavigationComponent::Mode::Explicit;
            m.Up.ID = A1.GetUUID();
            World.OnRuntimeStart();
            std::string error;
            CHECK(Rules().Configure(Actor, { 100, 20, 0, .5, 1 }, false, error));
            CHECK(Rules().ConfigureBag(Actor, 8, 99, false, error));
            CHECK(UI().ConfigurePanel(Owner, A, { "Bag", "Interact", uint64_t(A1.GetUUID()), false, true, true }, error));
            CHECK(UI().ConfigurePanel(Owner, B, { "Pause", "Pause", uint64_t(B1.GetUUID()), true, true, true }, error));
        }
        ~UIFixture()
        {
            World.OnRuntimeStop();
            SaveData::SetFilePath({});
            Project::SetActive(Previous);
            Input::ResetState();
            std::filesystem::remove_all(Root);
        }
        Entity Rect(const std::string& name, Entity parent, glm::vec2 p, glm::vec2 size)
        {
            auto e = World.CreateEntity(name);
            auto& t = e.AddComponent<UITransformComponent>();
            t.AnchorMin = t.AnchorMax = t.Pivot = { .5f, .5f };
            t.Position = p;
            t.Size = size;
            World.SetParent(e, parent, false);
            return e;
        }
        Entity Button(const std::string& name, Entity parent, glm::vec2 p)
        {
            auto e = Rect(name, parent, p, { 210, 42 });
            e.AddComponent<UIButtonComponent>();
            e.AddComponent<TextComponent>().Text = name;
            return e;
        }
        Gameplay::UIWorld& UI() { return Gameplay::UIWorld::For(World); }
        Gameplay::RuleWorld& Rules() { return Gameplay::RuleWorld::For(World); }
        void Frame()
        {
            Input::Update();
            World.UpdateRuntime(Timestep(1.f / 30));
        }
        void Press(Key key)
        {
            Input::SimulateKey(key, true);
            Frame();
            Input::SimulateKey(key, false);
            Frame();
        }
        void Items()
        {
            std::string error;
            CHECK(Rules().ChangeItem(Actor, "Apple", 2, false, error));
            CHECK(Rules().ChangeItem(Actor, "Coin", 8, false, error));
            CHECK(Rules().ChangeItem(Actor, "Key", 1, false, error));
            CHECK(Rules().ChangeItem(Actor, "Sword", 1, false, error));
        }
        void Inventory()
        {
            std::string error;
            CHECK(UI().BindInventory(Owner, Actor, "Bag", { A1, A2 }, error));
            UI().Refresh();
        }
    };
    Visual::Value Call(Gameplay::Runtime& r, Entity host, const std::string& kind, std::map<std::string, Visual::Value> overrides = {}, Entity target = {})
    {
        std::map<std::string, Visual::Value> args;
        for (auto& p : Visual::Catalog().at(kind).Inputs)
            if (p.Kind != Visual::Type::Exec) args[p.Name] = p.Default;
        for (auto& [k, v] : overrides) args[k] = v;
        return r.Dispatch(host, target ? target : host, kind, args);
    }
}
TEST(GameUIWorldNestedPanelsRestoreFocusAndTimeScale)
{
    UIFixture f;
    std::string error;
    f.World.SetTimeScale(.5f);
    f.World.SetSelectedUI(f.Outside);
    CHECK(f.UI().Open("Bag", error));
    CHECK(f.World.GetSelectedUI() == f.A1 && !f.World.IsGamePaused());
    CHECK(f.World.NavigateUI(UINavDirection::Down));
    CHECK(f.World.GetSelectedUI() == f.A2);
    CHECK(f.UI().Open("Pause", error));
    CHECK(f.World.IsGamePaused() && f.World.GetSelectedUI() == f.B1);
    CHECK(f.UI().Close("Pause", error));
    CHECK(f.World.GetSelectedUI() == f.A2);
    CHECK_NEAR(f.World.GetTimeScale(), .5f, 1e-6);
    CHECK(f.UI().Close("Bag", error));
    CHECK(f.World.GetSelectedUI() == f.Outside);
    CHECK_NEAR(f.World.GetTimeScale(), .5f, 1e-6);
}
TEST(GameUIWorldModalScopeBlocksOutsideSelectionNavigationAndMousePick)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().Open("Bag", error));
    CHECK(!f.World.IsUISelectable(f.Outside));
    f.World.SetSelectedUI(f.Outside);
    CHECK(!f.World.GetSelectedUI());
    f.UI().BeginFrame();
    CHECK(f.World.GetSelectedUI() == f.A1);
    auto p = f.World.GetUIScreenRect(f.Outside).GetCenter();
    CHECK(!f.World.PickUI(p));
    CHECK(f.World.PickUI(f.World.GetUIScreenRect(f.A1).GetCenter()) == f.A1);
}
TEST(GameUIWorldTogglePausesBeforeGameStepAndCloseDefersResumeOneFrame)
{
    UIFixture f;
    Input::SimulateKey(Key::Escape, true);
    f.Frame();
    CHECK(f.UI().State().Top == "Pause");
    CHECK_NEAR(f.World.GetTime(), 0, 1e-6);
    Input::SimulateKey(Key::Escape, false);
    f.Frame();
    Input::SimulateKey(Key::Escape, true);
    f.Frame();
    CHECK(f.UI().State().Depth == 0);
    CHECK_NEAR(f.World.GetTime(), 0, 1e-6);
    Input::SimulateKey(Key::Escape, false);
    f.Frame();
    CHECK(f.World.GetTime() > 0);
}
TEST(GameUIWorldGamepadStartBAndHeldSubmitDoNotLeakAcrossOpening)
{
    UIFixture f;
    Input::SimulateGamepadConnected(0, true);
    Input::SimulateGamepadButton(0, GamepadButton::Start, true);
    Input::SimulateGamepadButton(0, GamepadButton::A, true);
    f.Frame();
    CHECK(f.UI().State().Top == "Pause");
    CHECK(!f.B1.GetComponent<UIButtonComponent>().Clicked);
    Input::SimulateGamepadButton(0, GamepadButton::Start, false);
    f.Frame();
    CHECK(!f.B1.GetComponent<UIButtonComponent>().Clicked);
    Input::SimulateGamepadButton(0, GamepadButton::A, false);
    Input::SimulateGamepadButton(0, GamepadButton::B, true);
    f.Frame();
    CHECK(f.UI().State().Depth == 0);
}
TEST(GameUIWorldKeyboardAndGamepadNavigateOnlyTopPanel)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().Open("Bag", error));
    f.Frame();
    Input::SimulateKey(Key::Down, true);
    f.Frame();
    CHECK(f.World.GetSelectedUI() == f.A2);
    Input::SimulateKey(Key::Down, false);
    f.Frame();
    Input::SimulateGamepadConnected(0, true);
    Input::SimulateGamepadButton(0, GamepadButton::DPadUp, true);
    f.Frame();
    CHECK(f.World.GetSelectedUI() == f.A1);
    CHECK(f.UI().Open("Pause", error));
    f.Frame();
    CHECK(f.World.GetSelectedUI() == f.B1);
    CHECK(!f.World.IsUISelectable(f.A1));
}
TEST(GameUIWorldNonpausedPanelBlocksGameplayInputAndCloseFrame)
{
    UIFixture f;
    Gameplay::Runtime r;
    std::string error;
    Input::SimulateKey(Key::D, true);
    Input::SimulateKey(Key::Space, true);
    Input::Update();
    CHECK(Call(r, f.Actor, "Gameplay.Input").at("Enabled") == true);
    CHECK(f.UI().Open("Bag", error));
    auto input = Call(r, f.Actor, "Gameplay.Input");
    CHECK(input.at("Enabled") == false && input.at("Jump") == false);
    CHECK(!f.World.IsGamePaused());
    CHECK(f.UI().Close("Bag", error));
    CHECK(Call(r, f.Actor, "Gameplay.Input").at("Enabled") == false);
    f.Frame();
    CHECK(Call(r, f.Actor, "Gameplay.Input").at("Enabled") == true);
}
TEST(GameUIWorldExternalPauseBaselineIsPreserved)
{
    UIFixture f;
    std::string error;
    f.World.SetTimeScale(0);
    CHECK(f.UI().Open("Pause", error));
    CHECK(f.UI().Close("Pause", error));
    CHECK(f.World.IsGamePaused());
}
TEST(GameUIWorldInvalidConfigurationAndLowerCloseAreTransactional)
{
    UIFixture f;
    std::string error;
    CHECK(!f.UI().ConfigurePanel(f.Owner, f.A, { "Other", "Other", uint64_t(f.Outside.GetUUID()) }, error));
    CHECK(!f.UI().ConfigurePanel(f.A1, f.A, { "OwnerInside", "" }, error));
    CHECK(f.UI().Open("Bag", error));
    CHECK(f.UI().Open("Pause", error));
    CHECK(!f.UI().Close("Bag", error));
    CHECK(!f.UI().Open("Bag", error));
    CHECK(f.UI().State().Depth == 2 && f.World.GetSelectedUI() == f.B1);
}
TEST(GameUIWorldInventoryRowsPageAndSelectionReadCanonicalBag)
{
    UIFixture f;
    f.Items();
    f.Inventory();
    std::string error;
    CHECK(f.UI().Open("Bag", error));
    f.UI().Refresh();
    CHECK(f.A1.GetComponent<TextComponent>().Text == "Apple  x2");
    CHECK(f.A2.GetComponent<TextComponent>().Text == "Coin  x8");
    CHECK(f.UI().InventoryState("Bag").Item == "Apple");
    f.World.SetSelectedUI(f.A2);
    CHECK(f.UI().InventoryState("Bag").Item == "Coin");
    CHECK(f.UI().Page("Bag", 1));
    auto state = f.UI().InventoryState("Bag");
    CHECK(state.Page == 1 && state.Pages == 2 && state.Item == "Sword" && state.Count == 1);
    CHECK(f.A1.GetComponent<TextComponent>().Text == "Key  x1");
    CHECK(f.A2.GetComponent<TextComponent>().Text == "Sword  x1");
}
TEST(GameUIWorldInventoryShrinkDisablesEmptyRowsAndRepairsFocus)
{
    UIFixture f;
    f.Items();
    f.Inventory();
    std::string error;
    CHECK(f.UI().Open("Bag", error));
    CHECK(f.UI().Page("Bag", 1));
    f.World.SetSelectedUI(f.A2);
    for (auto key : { "Coin", "Key", "Sword" }) CHECK(f.Rules().ChangeItem(f.Actor, key, f.Rules().Inventory(f.Actor).Count(key), true, error));
    f.Frame();
    CHECK(f.UI().InventoryState("Bag").Page == 0);
    CHECK(!f.A2.GetComponent<UIButtonComponent>().Interactable);
    CHECK(f.World.GetSelectedUI() == f.A1);
}
TEST(GameUIWorldHUDBindingsFollowHealthShieldAndInventory)
{
    UIFixture f;
    f.Items();
    std::string error;
    CHECK(f.UI().BindHUD(f.Owner, f.Actor, f.HP, {}, error));
    CHECK(f.UI().BindHUD(f.Owner, f.Actor, f.Bar, {}, error));
    f.Rules().Damage(f.Actor, {}, 45, true, false, true);
    f.UI().Refresh();
    CHECK(f.HP.GetComponent<TextComponent>().Text == "HP 75 / 100");
    CHECK_NEAR(f.Bar.GetComponent<UIProgressBarComponent>().Value, .75f, 1e-6);
    CHECK_NEAR(f.Bar.GetComponent<UIProgressBarComponent>().DisplayValue, .75f, 1e-6);
    CHECK(f.UI().UnbindHUD(f.HP));
    CHECK(f.UI().BindHUD(f.Owner, f.Actor, f.HP, { Gameplay::HUDKind::Item, "Coin", "Coins ", false }, error));
    f.UI().Refresh();
    CHECK(f.HP.GetComponent<TextComponent>().Text == "Coins 8");
}
TEST(GameUIWorldHUDAndInventoryRestoreAuthoredWidgetsWhenRemoved)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().BindHUD(f.Owner, f.Actor, f.HP, {}, error));
    CHECK(f.UI().BindHUD(f.Owner, f.Actor, f.Bar, {}, error));
    f.Items();
    f.Inventory();
    f.UI().Refresh();
    CHECK(f.UI().UnbindHUD(f.HP));
    CHECK(f.HP.GetComponent<TextComponent>().Text == "Authored health");
    CHECK(f.UI().UnbindHUD(f.Bar));
    CHECK_NEAR(f.Bar.GetComponent<UIProgressBarComponent>().Value, .3f, 1e-6);
    CHECK(f.UI().RemoveInventory("Bag"));
    CHECK(f.A1.GetComponent<TextComponent>().Text == "Apple");
    CHECK(f.A2.GetComponent<UIButtonComponent>().Interactable);
}
TEST(GameUIWorldSourceUUIDReplacementDoesNotInheritBindings)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().BindHUD(f.Owner, f.Actor, f.HP, {}, error));
    f.Items();
    f.Inventory();
    auto id = f.Actor.GetUUID();
    f.World.DestroyEntity(f.Actor);
    auto replacement = f.World.CreateEntityWithUUID(id, "Replacement");
    CHECK(f.Rules().Configure(replacement, {}, false, error));
    f.UI().Refresh();
    CHECK(f.HP.GetComponent<TextComponent>().Text == "HP --");
    CHECK(!f.UI().InventoryState("Bag").Available);
    CHECK(!f.A1.GetComponent<UIButtonComponent>().Interactable);
}
TEST(GameUIWorldOwnerDestructionClosesMenusAndReleasesPause)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().Open("Pause", error));
    f.World.DestroyEntity(f.Owner);
    f.Frame();
    CHECK(f.UI().State().Depth == 0 && !f.World.IsGamePaused());
    CHECK(!f.B.IsActive());
}
TEST(GameUIWorldExternalPanelDeactivationReleasesPauseAndCanReopen)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().Open("Pause", error));
    f.World.SetActive(f.B, false);
    f.Frame();
    CHECK(f.UI().State().Depth == 0 && !f.World.IsGamePaused());
    CHECK(f.UI().Open("Pause", error));
}
TEST(GameUIWorldDestroyedInitialFocusFallsBackInsidePanel)
{
    UIFixture f;
    std::string error;
    f.World.DestroyEntity(f.A1);
    CHECK(f.UI().Open("Bag", error));
    CHECK(f.World.GetSelectedUI() == f.A2);
    CHECK(f.World.IsUISelectable(f.A2));
}
TEST(GameUIWorldInventoryConflictsAndForeignRowsAreRejected)
{
    UIFixture f;
    std::string error;
    CHECK(!f.UI().BindInventory(f.Owner, f.Actor, "Bad", { f.A1, f.A1 }, error));
    Scene other;
    auto foreign = other.CreateEntity();
    CHECK(!f.UI().BindInventory(f.Owner, f.Actor, "Bad", { foreign }, error));
    f.Inventory();
    CHECK(!f.UI().BindHUD(f.Owner, f.Actor, f.A1, {}, error));
    CHECK(!f.UI().BindInventory(f.Owner, f.Actor, "Other", { f.A2 }, error));
}
TEST(GameUIWorldBoundWidgetComponentRemovalPrunesAndRestoresOthers)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().BindHUD(f.Owner, f.Actor, f.HP, {}, error));
    f.Items();
    f.Inventory();
    f.A1.RemoveComponent<TextComponent>();
    f.UI().Refresh();
    CHECK(!f.UI().InventoryState("Bag").Configured);
    CHECK(f.A2.GetComponent<TextComponent>().Text == "Coin");
    f.HP.RemoveComponent<UITransformComponent>();
    f.UI().Refresh();
    CHECK(!f.UI().UnbindHUD(f.HP));
}
TEST(GameUIWorldSaveRestoreUpdatesHUDBagWithoutSerializingMenus)
{
    UIFixture f;
    f.Items();
    f.Inventory();
    std::string error;
    CHECK(f.UI().BindHUD(f.Owner, f.Actor, f.HP, {}, error));
    auto& s = Gameplay::SaveWorld::For(f.World);
    CHECK(s.Bind(f.Actor, "Player", error));
    auto save = s.Capture("V1");
    f.Rules().Damage(f.Actor, {}, 45, true, false, true);
    CHECK(f.Rules().ChangeItem(f.Actor, "Coin", 3, true, error));
    CHECK(f.UI().Open("Bag", error));
    CHECK(s.Restore(save, "V1").Success);
    f.UI().Refresh();
    CHECK(f.HP.GetComponent<TextComponent>().Text == "HP 100 / 100");
    CHECK(f.A2.GetComponent<TextComponent>().Text == "Coin  x8");
    CHECK(f.UI().State().Top == "Bag");
}
TEST(GameUIWorldEventQueueAndPanelCapacityAreBounded)
{
    UIFixture f;
    std::string error;
    for (int i = 0; i < 20; ++i)
    {
        CHECK(f.UI().Open("Bag", error));
        CHECK(f.UI().Close("Bag", error));
    }
    CHECK(f.UI().State().Dropped == 8);
    int events = 0;
    while (f.UI().Poll()) ++events;
    CHECK(events == 32);
    for (int i = 2; i < 16; ++i)
    {
        auto root = f.Rect("Extra", f.Canvas, { 0, 0 }, { 100, 100 });
        CHECK(f.UI().ConfigurePanel(f.Owner, root, { "Panel" + std::to_string(i), "" }, error));
    }
    CHECK(!f.UI().ConfigurePanel(f.Owner, f.Rect("Overflow", f.Canvas, { 0, 0 }, { 100, 100 }), { "Overflow", "" }, error));
}
TEST(GameUIWorldPlayRestartClearsScopeAndRestoresTime)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().Open("Pause", error));
    f.World.OnRuntimeStop();
    CHECK_NEAR(f.World.GetTimeScale(), 1, 1e-6);
    f.World.OnRuntimeStart();
    CHECK(f.UI().State().Depth == 0);
    CHECK(f.World.IsUISelectable(f.Outside));
}
TEST(GameUIWorldBlueprintHUDInventoryAndStateOutputsAreTyped)
{
    UIFixture f;
    Gameplay::Runtime runtime;
    CHECK(Call(runtime, f.Owner, "Gameplay.UIOpen", { { "Name", "Bag" } }).at("Success") == true);
    CHECK(Call(runtime, f.Owner, "Gameplay.UIState").at("Top") == "Bag");
    CHECK(Call(runtime, f.Owner, "Gameplay.UIBindHUD", { { "Actor", uint64_t(f.Actor.GetUUID()) } }, f.HP).at("Success") == true);
    f.Items();
    CHECK(Call(runtime, f.Owner, "Gameplay.UIInventoryConfigure", { { "Actor", uint64_t(f.Actor.GetUUID()) }, { "Rows", { uint64_t(f.A1.GetUUID()), uint64_t(f.A2.GetUUID()) } } }).at("Success") == true);
    f.UI().Refresh();
    CHECK(Call(runtime, f.Owner, "Gameplay.UIInventoryState").at("Pages") == 2);
    CHECK(Call(runtime, f.Owner, "Gameplay.UIEvent").at("Has") == true);
}
TEST(GameUIWorldNestedOrderTracksStackAndRestoresAuthoredOrder)
{
    UIFixture f;
    std::string error;
    auto a = f.A.GetComponent<UITransformComponent>().Order, b = f.B.GetComponent<UITransformComponent>().Order;
    CHECK(f.UI().Open("Pause", error));
    CHECK(f.UI().Open("Bag", error));
    CHECK(f.A.GetComponent<UITransformComponent>().Order > f.B.GetComponent<UITransformComponent>().Order);
    CHECK(f.UI().Close("Bag", error));
    CHECK(f.A.GetComponent<UITransformComponent>().Order == a);
    CHECK(f.UI().Close("Pause", error));
    CHECK(f.B.GetComponent<UITransformComponent>().Order == b);
}
TEST(GameUIWorldPanelLosingTransformCannotRetainPauseOrFocusScope)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().Open("Pause", error));
    f.B.RemoveComponent<UITransformComponent>();
    f.Frame();
    CHECK(f.UI().State().Depth == 0 && !f.World.IsGamePaused());
    CHECK(!f.UI().Open("Pause", error));
    CHECK(f.World.IsUISelectable(f.Outside));
}
TEST(GameUIWorldScriptPauseDisableAndRemovalReleaseMenus)
{
    UIFixture f;
    Visual::Graph graph;
    graph.Nodes = { { 1, "Event.Start" } };
    CHECK(graph.Save(f.Root / "Assets/Scripts/Controller.zgraph"));
    f.World.OnRuntimeStop();
    auto& n = f.Owner.AddComponent<NativeScriptComponent>();
    n.ClassName = "Zamin.VisualGraph";
    n.Fields["Graph"].Set(std::string("Scripts/Controller.zgraph"));
    f.World.OnRuntimeStart();
    f.Frame();
    std::string error;
    CHECK(f.UI().ConfigurePanel(f.Owner, f.B, { "Pause", "Pause", uint64_t(f.B1.GetUUID()), true, true, true }, error));
    auto* host = dynamic_cast<VisualGraphHost*>(n.Instance);
    CHECK(host);
    if (host)
    {
        CHECK(f.UI().Open("Pause", error));
        host->VM.Paused = true;
        f.Frame();
        CHECK(!f.World.IsGamePaused());
        host->VM.Paused = false;
        CHECK(f.UI().Open("Pause", error));
        host->SetEnabled(false);
        f.Frame();
        CHECK(!f.World.IsGamePaused());
        host->SetEnabled(true);
        CHECK(f.UI().Open("Pause", error));
        f.Owner.RemoveComponent<NativeScriptComponent>();
        f.Frame();
        CHECK(!f.World.IsGamePaused());
        CHECK(!f.UI().Open("Pause", error));
    }
}
TEST(GameUIWorldPauseControllerBlueprintAndUnscaledClockWork)
{
    UIFixture f;
    Visual::Graph graph;
    graph.Nodes = { { 1, "Event.Start" } };
    CHECK(graph.Save(f.Root / "Assets/Scripts/Controller.zgraph"));
    f.World.OnRuntimeStop();
    auto& n = f.Owner.AddComponent<NativeScriptComponent>();
    n.ClassName = "Zamin.VisualGraph";
    n.Fields["Graph"].Set(std::string("Scripts/Controller.zgraph"));
    f.World.OnRuntimeStart();
    f.Frame();
    std::string setupError;
    CHECK(f.UI().ConfigurePanel(f.Owner, f.B, { "Pause", "Pause", uint64_t(f.B1.GetUUID()), true, true, true }, setupError));
    Gameplay::Runtime runtime;
    CHECK(Call(runtime, f.Owner, "Gameplay.UIRunWhenPaused").at("Success") == true);
    CHECK(n.Instance && n.Instance->IsUpdateWhenPaused());
    std::string error;
    CHECK(f.UI().Open("Pause", error));
    auto before = f.World.GetUnscaledTime();
    f.Frame();
    auto result = Call(runtime, f.Owner, "Gameplay.UIState");
    CHECK(result.at("Time").get<double>() > before);
    CHECK(result.at("Paused") == true);
    CHECK(result.at("GameTime").get<double>() < result.at("Time").get<double>());
}
TEST(GameUIWorldLayoutScalesWidgetsWithinViewportAtDifferentResolutions)
{
    UIFixture f;
    std::string error;
    CHECK(f.UI().Open("Bag", error));
    for (auto size : { glm::uvec2(800, 600), glm::uvec2(1600, 900), glm::uvec2(1024, 768) })
    {
        f.World.OnViewportResize(size.x, size.y);
        for (auto e : { f.A1, f.A2, f.Outside })
        {
            auto rect = f.World.GetUIScreenRect(e);
            CHECK(rect.Min.x >= 0 && rect.Min.y >= 0);
            CHECK(rect.Max.x <= size.x && rect.Max.y <= size.y);
            CHECK(rect.GetWidth() > 0 && rect.GetHeight() > 0);
        }
    }
}
TEST(GameUIWorldWorkshopGraphsValidateAndRoundtrip)
{
    auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/UIWorkshop/Assets/Scripts";
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
TEST(GameUIWorldModalFocusRejectsTextFieldOutsideScopeImmediately)
{
    UIFixture f;
    auto outside = f.Rect("External input", f.Canvas, { 0, 0 }, { 150, 35 });
    outside.AddComponent<UIInputFieldComponent>();
    f.World.FocusInputField(outside);
    CHECK(f.World.GetFocusedInputField() == outside);
    std::string error;
    CHECK(f.UI().Open("Bag", error));
    CHECK(!f.World.IsTextInputFocused());
    f.World.FocusInputField(outside);
    CHECK(!f.World.IsTextInputFocused());
    auto inside = f.Rect("Panel input", f.A, { 0, -60 }, { 150, 35 });
    inside.AddComponent<UIInputFieldComponent>();
    f.World.FocusInputField(inside);
    CHECK(f.World.GetFocusedInputField() == inside);
}
TEST(GameUIWorldOpeningModalCancelsBackgroundScrollDrag)
{
    UIFixture f;
    auto scroll = f.Rect("External scroll", f.Canvas, { -250, 0 }, { 180, 150 });
    auto content = f.Rect("Content", scroll, { 0, 0 }, { 180, 500 });
    auto& s = scroll.AddComponent<UIScrollViewComponent>();
    s.Content.ID = content.GetUUID();
    s.Inertia = false;
    s.ShowVerticalBar = false;
    auto center = f.World.GetUIScreenRect(scroll).GetCenter();
    f.World.SetViewportMouse({ center.x, 600 - center.y }, true);
    f.Frame();
    f.World.SetViewportMouse({ center.x, 550 - center.y }, true);
    f.Frame();
    CHECK(s.Dragging);
    std::string error;
    CHECK(f.UI().Open("Bag", error));
    f.World.SetViewportMouse({ center.x, 500 - center.y }, true);
    f.Frame();
    CHECK(!s.Dragging);
}
TEST(GameUIWorldSparseInventoryPageRestoresItemFocusFromFooter)
{
    UIFixture f;
    f.Items();
    std::string error;
    CHECK(f.Rules().ChangeItem(f.Actor, "Sword", 1, true, error));
    f.Inventory();
    auto footer = f.Button("Page", f.A, { 0, -100 });
    CHECK(f.UI().Open("Bag", error));
    f.World.SetSelectedUI(footer);
    CHECK(f.UI().Page("Bag", 1));
    CHECK(f.World.GetSelectedUI() == f.A1);
    CHECK(f.UI().InventoryState("Bag").Item == "Key");
    CHECK(!f.A2.GetComponent<UIButtonComponent>().Interactable);
}
TEST(GameUIWorldPagingAStaleViewCannotUseAnErasedRecord)
{
    UIFixture f;
    f.Items();
    f.Inventory();
    std::string error;
    CHECK(f.UI().Open("Bag", error));
    f.World.DestroyEntity(f.A1);
    CHECK(!f.UI().Page("Bag", 1));
    CHECK(!f.UI().InventoryState("Bag").Configured);
    CHECK(f.A2.GetComponent<TextComponent>().Text == "Coin");
}
