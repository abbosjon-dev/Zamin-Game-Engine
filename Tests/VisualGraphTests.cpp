#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Asset/AssetReferences.h"
#include "Zamin/Project/ExportAssets.h"
#include "TestAssertions.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Asset/AssetTypes.h"
#include "Zamin/Utils/FileSystem.h"
#include <filesystem>
using namespace Zamin;
using namespace Zamin::Visual;
namespace {
    Graph Counter()
    {
        Graph g;
        g.Variables["Count"] = { Type::Number, 0 };
        g.Nodes = { { 1, "Event.Start" }, { 2, "For", 250, 0, { { "Count", 5 } } }, { 3, "Set.Number", 500, 0, { { "Name", "Count" } } }, { 4, "Get.Number", 200, 300, { { "Name", "Count" } } }, { 5, "Add", 450, 300, { { "B", 1 } } } };
        g.Links = { { 1, "Then", 2, "In" }, { 2, "Body", 3, "In" }, { 4, "Value", 5, "A" }, { 5, "Value", 3, "Value" } };
        return g;
    }
}
TEST(VisualGraphTypedRoundTrip)
{
    auto g = Graph::Example();
    g.Variables["Speed"] = { Type::Number, 3.5 };
    g.Nodes[0].Breakpoint = true;
    auto j = g.Serialize();
    auto copy = Graph::Parse(j);
    CHECK(copy.Serialize() == j);
    CHECK(copy.Validate().empty());
    CHECK(AssetTypes::FromExtension(".zgraph") == AssetType::VisualGraph);
    CHECK(AssetTypes::IsRegistered(AssetType::VisualGraph));
}
TEST(VisualGraphRejectsInvalidPinsAndCycles)
{
    auto g = Graph::Example();
    g.Links.push_back({ 3, "Value", 3, "Vector" });
    CHECK(!g.Validate().empty());
    g = Graph::Example();
    g.Links.push_back({ 1, "Number", 4, "Vector" });
    CHECK(!g.Validate().empty());
    g = Graph::Example();
    g.Nodes.push_back(g.Nodes[0]);
    CHECK(!g.Validate().empty());
    g = Graph::Example();
    g.Nodes[0].Kind = "Unknown";
    Machine vm;
    CHECK(!vm.Load(g));
    CHECK(!vm.Error().empty());
}
TEST(VisualGraphVariableTypesChecked)
{
    auto g = Counter();
    CHECK(g.Validate().empty());
    g.Variables["Count"].Kind = Type::String;
    CHECK(!g.Validate().empty());
    g = Counter();
    g.Nodes[2].Inputs["Name"] = "Missing";
    CHECK(!g.Validate().empty());
}
TEST(VisualGraphForLoopReadsFreshVariables)
{
    Machine vm;
    CHECK(vm.Load(Counter()));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Count") == 5);
    CHECK(vm.Pending() == 0);
}
TEST(VisualGraphDelayKeepsLoopContinuation)
{
    auto g = Counter();
    g.Nodes.push_back({ 6, "Delay", 0, 0, { { "Seconds", 0.25 } } });
    g.Links[1] = { 2, "Body", 6, "In" };
    g.Links.push_back({ 6, "Then", 3, "In" });
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Variables().at("Count") == 0);
    for (int i = 0; i < 5; ++i) vm.Tick(.25);
    CHECK(vm.Variables().at("Count") == 5);
    CHECK(vm.Pending() == 0);
    CHECK(vm.Error().empty());
}
TEST(VisualGraphSequenceWaitsForDelayedBranch)
{
    Graph g;
    g.Variables["Count"] = { Type::Number, 0 };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Sequence" }, { 3, "Delay", 0, 0, { { "Seconds", 1 } } }, { 4, "Set.Number", 0, 0, { { "Name", "Count" }, { "Value", 1 } } }, { 5, "Set.Number", 0, 0, { { "Name", "Count" }, { "Value", 2 } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "First", 3, "In" }, { 3, "Then", 4, "In" }, { 2, "Second", 5, "In" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Variables().at("Count") == 0);
    vm.Tick(1);
    CHECK(vm.Variables().at("Count") == 2);
}
TEST(VisualGraphBranchChoosesOnePath)
{
    Graph g;
    g.Variables["Result"] = { Type::Bool, false };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Branch", 0, 0, { { "Condition", true } } }, { 3, "Set.Bool", 0, 0, { { "Name", "Result" }, { "Value", true } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "True", 3, "In" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Variables().at("Result") == true);
}
TEST(VisualGraphInfiniteExecutionIsBounded)
{
    Graph g;
    g.Nodes = { { 1, "Event.Start" }, { 2, "Sequence" } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "First", 2, "In" } };
    Machine vm;
    vm.Budget = 50;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(!vm.Error().empty());
    CHECK(vm.Pending() == 0);
}
TEST(VisualGraphNonFiniteMathFailsAtNode)
{
    Graph g;
    g.Variables["X"] = { Type::Number, 0 };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Set.Number", 0, 0, { { "Name", "X" } } }, { 3, "Divide", 0, 0, { { "A", 1 }, { "B", 0 } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 3, "Value", 2, "Value" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().find("Division by zero") != std::string::npos);
}
TEST(VisualGraphInstancesAndOverridesAreIsolated)
{
    Machine a, b;
    CHECK(a.Load(Counter(), {}, { { "Count", 10 } }));
    CHECK(b.Load(Counter(), {}, { { "Count", "wrong type" } }));
    a.Event("Start");
    CHECK(a.Variables().at("Count") == 15);
    CHECK(b.Variables().at("Count") == 0);
}
TEST(VisualGraphDebugPauseStepAndContinue)
{
    auto g = Counter();
    g.Nodes[1].Breakpoint = true;
    Machine vm;
    CHECK(vm.Load(g));
    vm.Debug = true;
    vm.Event("Start");
    CHECK(vm.Paused);
    CHECK(vm.CurrentNode == 2);
    CHECK(vm.Variables().at("Count") == 0);
    vm.Step();
    CHECK(vm.Paused);
    CHECK(vm.CurrentNode == 2);
    vm.Debug = false;
    vm.Continue();
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Count") == 5);
    CHECK(!vm.Paused);
}
TEST(VisualGraphEventPayloadDrivesDataPins)
{
    Graph g;
    g.Variables["Delta"] = { Type::Number, 0 };
    g.Nodes = { { 1, "Event.Update" }, { 2, "Set.Number", 0, 0, { { "Name", "Delta" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 1, "Delta", 2, "Value" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Update", { { "Delta", .125 } });
    CHECK(vm.Variables().at("Delta") == .125);
}
TEST(VisualGraphVectorBridgeReceivesScaledDelta)
{
    Machine vm;
    std::vector<Value> calls;
    CHECK(vm.Load(Graph::Example(), [&](const std::string& name, const auto& args) -> Value {CHECK(name=="Translate");calls.push_back(args.at("Vector"));return nullptr; }));
    vm.Event("Update", { { "Delta", .2 } });
    CHECK(vm.Error().empty());
    CHECK(calls.size() == 1);
    CHECK_NEAR(calls[0][1].get<double>(), .1, 1e-9);
}
TEST(VisualGraphRejectsUnsupportedFormats)
{
    bool threw = false;
    try
    {
        Graph::Parse({ { "Format", "Zamin.VisualGraph" }, { "Version", 2 } });
    }
    catch (...)
    {
        threw = true;
    }
    CHECK(threw);
    auto g = Graph::Example();
    g.Nodes[0].ID = 0;
    CHECK(!g.Validate().empty());
}
TEST(VisualGraphBuiltinCannotBeUnregistered)
{
    auto* info = ScriptRegistry::Find("Zamin.VisualGraph");
    CHECK(info && info->Factory);
    ScriptRegistry::Unregister("Zamin.VisualGraph");
    CHECK(ScriptRegistry::Find("Zamin.VisualGraph"));
    ScriptRegistry::Register({ "Zamin.VisualGraph", nullptr, {} });
    CHECK(ScriptRegistry::Find("Zamin.VisualGraph")->Factory != nullptr);
}
TEST(VisualGraphNativeHostLifecycleWithoutCppModule)
{
    auto prior = Project::GetActive();
    auto root = std::filesystem::temp_directory_path() / "zamin-visual-lifecycle";
    std::filesystem::create_directories(root / "Assets/Scripts");
    Project::New();
    CHECK(Project::SaveActive(root / "Graph.zproject"));
    auto graph = Graph::Example();
    graph.Nodes[1].Inputs["Y"] = 2.;
    CHECK(graph.Save(root / "Assets/Scripts/Move.zgraph"));
    Scene scene;
    auto e = scene.CreateEntity("Visual mover");
    auto& c = e.AddComponent<NativeScriptComponent>();
    c.ClassName = "Zamin.VisualGraph";
    c.Fields["Graph"].Set(std::string("Scripts/Move.zgraph"));
    scene.OnRuntimeStart();
    scene.OnUpdateRuntime(Timestep(.25f));
    CHECK_NEAR(e.GetComponent<TransformComponent>().Translation.y, .5f, .001f);
    CHECK(c.Instance != nullptr);
    auto* host = dynamic_cast<VisualGraphHost*>(c.Instance);
    CHECK(host && host->VM.Error().empty());
    scene.OnRuntimeStop();
    CHECK(c.Instance == nullptr);
    Project::SetActive(prior);
    std::filesystem::remove_all(root);
}

TEST(VisualGraphFunctionsResumeAfterDelayAndReturnToCaller)
{
    Graph g;
    g.Variables["Count"] = { Type::Number, 0 };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Call", 0, 0, { { "Name", "Award" } } }, { 3, "Function", 0, 0, { { "Name", "Award" } } }, { 4, "Delay", 0, 0, { { "Seconds", .5 } } }, { 5, "Set.Number", 0, 0, { { "Name", "Count" }, { "Value", 10 } } }, { 6, "Set.Number", 0, 0, { { "Name", "Count" }, { "Value", 20 } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 3, "Then", 4, "In" }, { 4, "Then", 5, "In" }, { 2, "Then", 6, "In" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Variables().at("Count") == 0);
    vm.Tick(.5);
    CHECK(vm.Variables().at("Count") == 20);
    CHECK(vm.Error().empty());
}
TEST(VisualGraphFunctionNamesAreValidated)
{
    Graph g;
    g.Nodes = { { 1, "Call", 0, 0, { { "Name", "Missing" } } } };
    CHECK(!g.Validate().empty());
    g.Nodes.push_back({ 2, "Function", 0, 0, { { "Name", "Missing" } } });
    CHECK(g.Validate().empty());
    g.Nodes.push_back({ 3, "Function", 0, 0, { { "Name", "Missing" } } });
    CHECK(!g.Validate().empty());
}

TEST(VisualGraphWhileReevaluatesConditionAndBreakCompletes)
{
    auto g = Counter();
    g.Nodes[1].Kind = "While";
    g.Nodes[1].Inputs.clear();
    g.Nodes.push_back({ 6, "Less", 0, 0, { { "B", 3 } } });
    g.Links.push_back({ 4, "Value", 6, "A" });
    g.Links.push_back({ 6, "Value", 2, "Condition" });
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Count") == 3);
    g = Counter();
    g.Nodes.push_back({ 6, "Break" });
    g.Links.push_back({ 3, "Then", 6, "In" });
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Count") == 1);
}
namespace {
    class GraphBridgeProbe : public ScriptableEntity
    {
    public:
        float Health = 25;
    };
}
TEST(VisualGraphWritesAndReadsReflectedCppFields)
{
    auto prior = Project::GetActive();
    auto root = std::filesystem::temp_directory_path() / "zamin-visual-bridge";
    std::filesystem::create_directories(root / "Assets/Scripts");
    Project::New();
    CHECK(Project::SaveActive(root / "Bridge.zproject"));
    ScriptRegistry::Register({ "GraphBridgeProbe", []() -> ScriptableEntity* { return new GraphBridgeProbe; }, { ZM_FIELD(GraphBridgeProbe, Health) } });
    Graph g;
    g.Nodes = { { 1, "Event.Start" }, { 2, "FindEntity", 0, 0, { { "Name", "Native target" } } }, { 3, "WriteField.Number", 0, 0, { { "Name", "Health" }, { "Value", 42 } } }, { 4, "ReadField.Number", 0, 0, { { "Name", "Health" } } }, { 5, "Set.Number", 0, 0, { { "Name", "Observed" } } } };
    g.Variables["Observed"] = { Type::Number, 0 };
    g.Links = { { 1, "Then", 3, "In" }, { 2, "Value", 3, "Target" }, { 3, "Then", 5, "In" }, { 2, "Value", 4, "Target" }, { 4, "Value", 5, "Value" } };
    CHECK(g.Save(root / "Assets/Scripts/Bridge.zgraph"));
    Scene scene;
    auto target = scene.CreateEntity("Native target");
    target.AddComponent<NativeScriptComponent>().ClassName = "GraphBridgeProbe";
    auto e = scene.CreateEntity("Visual controller");
    auto& c = e.AddComponent<NativeScriptComponent>();
    c.ClassName = "Zamin.VisualGraph";
    c.Fields["Graph"].Set(std::string("Scripts/Bridge.zgraph"));
    scene.OnRuntimeStart();
    scene.UpdateRuntime(.01f);
    auto* host = dynamic_cast<VisualGraphHost*>(c.Instance);
    CHECK(host && host->VM.Error().empty());
    CHECK(host->VM.Variables().at("Observed") == 42);
    CHECK_NEAR(dynamic_cast<GraphBridgeProbe*>(target.GetComponent<NativeScriptComponent>().Instance)->Health, 42.f, .001f);
    scene.OnRuntimeStop();
    ScriptRegistry::Unregister("GraphBridgeProbe");
    Project::SetActive(prior);
    std::filesystem::remove_all(root);
}
TEST(VisualGraphPendingWorkIsBoundedAndStopCancelsDelay)
{
    Graph g;
    g.Nodes = { { 1, "Event.Start" }, { 2, "Delay", 0, 0, { { "Seconds", 10 } } }, { 3, "Print", 0, 0, { { "Text", "after delay" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" } };
    Machine vm;
    CHECK(vm.Load(g, [](const auto&, const auto&) -> Value { return nullptr; }));
    vm.Event("Start");
    CHECK(vm.Pending() == 1);
    vm.Stop();
    CHECK(vm.Pending() == 0);
    for (int i = 0; i < 129; ++i) vm.Event("Start");
    CHECK(!vm.Error().empty());
    CHECK(vm.Pending() == 0);
}
TEST(VisualGraphExplicitNullEntityDoesNotMoveSelf)
{
    auto prior = Project::GetActive();
    auto root = std::filesystem::temp_directory_path() / "zamin-visual-null-target";
    std::filesystem::create_directories(root / "Assets/Scripts");
    Project::New();
    CHECK(Project::SaveActive(root / "Null.zproject"));
    Graph g;
    g.Nodes = { { 1, "Event.Start" }, { 2, "SetPosition", 0, 0, { { "Target", 0ull }, { "Vector", Value::array({ 999, 0, 0 }) } } } };
    g.Links = { { 1, "Then", 2, "In" } };
    CHECK(g.Save(root / "Assets/Scripts/Null.zgraph"));
    Scene scene;
    auto e = scene.CreateEntity("Null target test");
    auto& c = e.AddComponent<NativeScriptComponent>();
    c.ClassName = "Zamin.VisualGraph";
    c.Fields["Graph"].Set(std::string("Scripts/Null.zgraph"));
    scene.OnRuntimeStart();
    scene.UpdateRuntime(.01f);
    auto* host = dynamic_cast<VisualGraphHost*>(c.Instance);
    CHECK(host && !host->VM.Error().empty());
    CHECK_NEAR(e.GetTransform().Translation.x, 0.f, .001f);
    scene.OnRuntimeStop();
    Project::SetActive(prior);
    std::filesystem::remove_all(root);
}
TEST(VisualGraphCatalogHasTypedDefaultsAndUniquePins)
{
    CHECK(Catalog().size() > 100);
    for (auto& [key, d] : Catalog())
    {
        CHECK(key == d.Name);
        std::set<std::string> inputs, outputs;
        for (auto& p : d.Inputs)
        {
            CHECK(inputs.insert(p.Name).second);
            CHECK(Matches(p.Kind, p.Default));
        }
        for (auto& p : d.Outputs)
        {
            CHECK(outputs.insert(p.Name).second);
            CHECK(Matches(p.Kind, p.Default));
        }
    }
}
TEST(VisualGraphBridgeCanReturnSeveralTypedOutputs)
{
    Graph g;
    g.Variables["Hit"] = { Type::Bool, false };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Set.Bool", 0, 0, { { "Name", "Hit" } } }, { 3, "Raycast3D" } };
    g.Links = { { 1, "Then", 2, "In" }, { 3, "Hit", 2, "Value" } };
    Machine vm;
    CHECK(vm.Load(g, [](const auto& name, const auto&) -> Value {CHECK(name=="Raycast3D");return {{"Hit",true},{"Entity",0ull},{"Point",Value::array({0,0,0})},{"Normal",Value::array({0,1,0})},{"Distance",0}}; }));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Hit") == true);
}

TEST(VisualGraphSampleAssetsRemainReachableInExport)
{
    auto prior = Project::GetActive();
    auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/VisualWorkshop";
    CHECK(Project::Load(root / "VisualWorkshop.zproject") != nullptr);
    AssetReferenceIndex index;
    auto plan = ExportAssets::PlanForActiveProject(nullptr, nullptr, &index);
    CHECK(index.IsReferenced(root / "Assets/Scripts/Move2D.zgraph"));
    CHECK(index.IsReferenced(root / "Assets/Scripts/Spin3D.zgraph"));
    CHECK(std::find(plan.Included.begin(), plan.Included.end(), "Scripts/Move2D.zgraph") != plan.Included.end());
    CHECK(std::find(plan.Included.begin(), plan.Included.end(), "Scripts/Spin3D.zgraph") != plan.Included.end());
    for (auto file : { "Move2D.zgraph", "Spin3D.zgraph" }) CHECK(Graph::Load(root / "Assets/Scripts" / file).Validate().empty());
    Project::SetActive(prior);
}

TEST(VisualGraphTargetsEntitiesInOtherLoadedScenes)
{
    auto prior = Project::GetActive();
    auto root = std::filesystem::temp_directory_path() / "zamin-visual-cross-scene";
    std::filesystem::create_directories(root / "Assets/Scripts");
    Project::New();
    CHECK(Project::SaveActive(root / "Cross.zproject"));
    auto remote = CreateRef<Scene>();
    auto parent = remote->CreateEntity("Remote parent");
    parent.GetTransform().Translation = { 10, 0, 0 };
    auto child = remote->CreateEntity("Remote child");
    CHECK(remote->SetParent(child, parent, false));
    std::filesystem::create_directories(root / "Assets/Scenes");
    CHECK(SceneSerializer(remote).Serialize(root / "Assets/Scenes/Remote.zscene"));
    Graph g;
    g.Nodes = { { 1, "Event.Update" }, { 2, "FindEntity", 0, 0, { { "Name", "Remote child" } } }, { 3, "SetPosition", 0, 0, { { "Vector", Value::array({ 2, 1, 0 }) } } } };
    g.Links = { { 1, "Then", 3, "In" }, { 2, "Value", 3, "Target" } };
    CHECK(g.Save(root / "Assets/Scripts/Cross.zgraph"));
    auto main = CreateRef<Scene>();
    auto controller = main->CreateEntity("Controller");
    auto& script = controller.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/Cross.zgraph"));
    SceneManager manager;
    manager.Start(main);
    manager.LoadScene(root / "Assets/Scenes/Remote.zscene", LoadSceneMode::Additive);
    manager.ProcessRequests();
    CHECK(manager.GetSceneCount() == 2);
    manager.UpdateScenes(.02f);
    auto* host = dynamic_cast<VisualGraphHost*>(script.Instance);
    CHECK(host && host->VM.Error().empty());
    auto loaded = manager.GetSceneByPath("Scenes/Remote.zscene");
    CHECK(loaded != nullptr);
    if (loaded)
    {
        auto e = loaded->FindEntityByName("Remote child");
        CHECK(e);
        if (e)
        {
            CHECK_NEAR(e.GetTransform().Translation.x, 2.f, .001f);
            CHECK_NEAR(loaded->GetWorldPosition(e).x, 12.f, .001f);
        }
    }
    CHECK_NEAR(controller.GetTransform().Translation.x, 0.f, .001f);
    manager.Stop();
    Project::SetActive(prior);
    std::filesystem::remove_all(root);
}

TEST(VisualGraphTraversalTokensKeepFullUnsignedPrecisionAndSuccessOutput)
{
    Graph graph;
    graph.Variables["Success"] = { Type::Bool, false };
    graph.Nodes = { { 1, "Event.NavTraversalRequested" }, { 2, "CompleteNavTraversal" }, { 3, "Set.Bool", 0, 0, { { "Name", "Success" } } } };
    graph.Links = { { 1, "Then", 2, "In" }, { 1, "Link", 2, "Link" }, { 1, "Revision", 2, "Revision" }, { 1, "Ticket", 2, "Ticket" }, { 2, "Then", 3, "In" }, { 2, "Success", 3, "Value" } };
    CHECK(graph.Validate().empty());
    Machine vm;
    int calls = 0;
    CHECK(vm.Load(graph, [&](const std::string& name, const auto& args) -> Value {
        CHECK(name == "CompleteNavTraversal");
        CHECK(args.at("Link") == std::to_string(UINT64_MAX));
        CHECK(args.at("Revision") == "9007199254740993");
        CHECK(args.at("Ticket") == "9007199254740995");
        ++calls;
        return true;
    }));
    vm.Event("NavTraversalRequested", { { "Link", std::to_string(UINT64_MAX) }, { "Revision", "9007199254740993" }, { "Ticket", "9007199254740995" }, { "Entry", { 0, 0, 0 } }, { "Exit", { 100, 0, 0 } } });
    CHECK(vm.Error().empty() && calls == 1 && vm.Variables().at("Success") == true);
}
TEST(VisualGraphWorldDestinationFailureAndCancelReturnBooleans)
{
    for (const auto* kind : { "SetWorldDestination", "CancelNavTraversal" })
    {
        Graph graph;
        graph.Variables["Success"] = { Type::Bool, true };
        graph.Nodes = { { 1, "Event.Start" }, { 2, kind }, { 3, "Set.Bool", 0, 0, { { "Name", "Success" } } } };
        graph.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 2, "Success", 3, "Value" } };
        Machine vm;
        CHECK(vm.Load(graph, [](const std::string&, const auto&) -> Value { return false; }));
        vm.Event("Start");
        CHECK(vm.Error().empty() && vm.Variables().at("Success") == false);
    }
}

TEST(BlueprintWorkshopRunsTypedDelayedCollectionsInRealScenes)
{
    auto prior = Project::GetActive();
    const auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/VisualWorkshop";
    CHECK(Project::Load(root / "VisualWorkshop.zproject") != nullptr);
    for (const auto& [name, expected] : std::vector<std::pair<std::string, double>>{ { "Visual2D", 12. }, { "Visual3D", 18. } })
    {
        auto scene = CreateRef<Scene>();
        CHECK(SceneSerializer(scene).Deserialize(root / "Assets/Scenes" / (name + ".zscene")));
        scene->OnRuntimeStart();
        for (int i = 0; i < 20; ++i) scene->UpdateRuntime(.02f);
        auto entity = scene->FindEntityByName("Blueprint gameplay controller");
        CHECK(entity);
        if (entity)
        {
            auto* host = dynamic_cast<VisualGraphHost*>(entity.GetComponent<NativeScriptComponent>().Instance);
            CHECK(host);
            if (host)
            {
                CHECK(host->VM.Error().empty());
                CHECK(host->VM.Variables().at("Total") == expected);
                CHECK(host->VM.Pending() == 0);
            }
        }
        scene->OnRuntimeStop();
    }
    AssetReferenceIndex index;
    auto plan = ExportAssets::PlanForActiveProject(nullptr, nullptr, &index);
    CHECK(std::find(plan.Included.begin(), plan.Included.end(), "Scripts/BlueprintGameplay.zgraph") != plan.Included.end());
    Project::SetActive(prior);
}
