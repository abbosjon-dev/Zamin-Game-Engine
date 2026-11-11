#include "TestAssertions.h"
#include <Zamin/Scripting/Visual/VisualGraph.h>
using namespace Zamin::Visual;
namespace {
    Graph FunctionGraph()
    {
        Graph g;
        g.Variables["Answer"] = { Type::Number, 0. };
        g.Functions["Double"] = { { { "X", Type::Number, 2. } }, { { "Result", Type::Number, 0. } }, { { "Temp", { Type::Number, 0. } } } };
        g.Nodes = { { 1, "Event.Start" }, { 2, "Function.Call", 0, 0, { { "Name", "Double" }, { "X", 7. } } }, { 3, "Set.Number", 0, 0, { { "Name", "Answer" } } }, { 10, "Function.Entry", 0, 0, { { "Name", "Double" } } }, { 11, "Multiply", 0, 0, { { "B", 2. } } }, { 12, "Local.Set.Number", 0, 0, { { "Name", "Temp" } } }, { 13, "Local.Get.Number", 0, 0, { { "Name", "Temp" } } }, { 14, "Function.Return", 0, 0, { { "Name", "Double" } } } };
        g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 2, "Result", 3, "Value" }, { 10, "Then", 12, "In" }, { 10, "X", 11, "A" }, { 11, "Value", 12, "Value" }, { 12, "Then", 14, "In" }, { 13, "Value", 14, "Result" } };
        return g;
    }
}
TEST(BlueprintTypedFunctionReturnsAndLocals)
{
    auto graph = FunctionGraph();
    CHECK(graph.Validate().empty());
    Machine vm;
    CHECK(vm.Load(graph));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Answer") == 14.);
}
TEST(BlueprintTypedFunctionDelayPreservesArguments)
{
    auto g = FunctionGraph();
    g.Nodes.push_back({ 15, "Delay", 0, 0, { { "Seconds", .1 } } });
    g.Links[3] = { 10, "Then", 15, "In" };
    g.Links.push_back({ 15, "Then", 12, "In" });
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Pending() == 1 && vm.Variables().at("Answer") == 0.);
    vm.Tick(.1);
    CHECK(vm.Error().empty() && vm.Variables().at("Answer") == 14.);
}
TEST(BlueprintSignaturesRoundTripAndRename)
{
    auto g = FunctionGraph();
    auto copy = Graph::Parse(g.Serialize());
    CHECK(copy.Validate().empty());
    CHECK(copy.RenameFunction("Double", "Compute"));
    CHECK(copy.RenameVariable("Answer", "Score"));
    CHECK(copy.Validate().empty());
    Machine vm;
    CHECK(vm.Load(copy));
    vm.Event("Start");
    CHECK(vm.Variables().at("Score") == 14.);
}
TEST(BlueprintFunctionParameterCannotEscape)
{
    auto g = FunctionGraph();
    g.Links.back() = { 10, "X", 3, "Value" };
    CHECK(!g.Validate().empty());
}
TEST(BlueprintMissingReturnFailsClearly)
{
    auto g = FunctionGraph();
    g.Nodes.push_back({ 15, "Branch", 0, 0, { { "Condition", false } } });
    for (auto& link : g.Links)
        if (link.From == 10 && link.Output == "Then") link.To = 15;
    g.Links.push_back({ 15, "True", 12, "In" });
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().find("without Return") != std::string::npos);
}
TEST(BlueprintArrayForEachUsesFrozenValues)
{
    Graph g;
    g.Variables = { { "Items", { Type::ArrayNumber, Value::array({ 1., 2., 3. }) } }, { "Sum", { Type::Number, 0. } } };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Get.ArrayNumber", 0, 0, { { "Name", "Items" } } }, { 3, "ForEach.Number" }, { 4, "Get.Number", 0, 0, { { "Name", "Sum" } } }, { 5, "Add" }, { 6, "Set.Number", 0, 0, { { "Name", "Sum" } } }, { 7, "Set.ArrayNumber", 0, 0, { { "Name", "Items" }, { "Value", Value::array({ 99. }) } } } };
    g.Links = { { 1, "Then", 3, "In" }, { 2, "Value", 3, "Array" }, { 3, "Body", 6, "In" }, { 4, "Value", 5, "A" }, { 3, "Value", 5, "B" }, { 5, "Value", 6, "Value" }, { 6, "Then", 7, "In" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty() && vm.Variables().at("Sum") == 6. && vm.Variables().at("Items").size() == 1);
}
TEST(BlueprintArrayAppendAndCheckedIndex)
{
    Graph g;
    g.Variables["Result"] = { Type::Number, 0. };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Array.Append.Number", 0, 0, { { "Array", Value::array({ 1., 2. }) }, { "Value", 3. } } }, { 3, "Array.Get.Number", 0, 0, { { "Index", 2. } } }, { 4, "Set.Number", 0, 0, { { "Name", "Result" } } } };
    g.Links = { { 1, "Then", 4, "In" }, { 2, "Value", 3, "Array" }, { 3, "Value", 4, "Value" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty() && vm.Variables().at("Result") == 3.);
    g.Nodes[2].Inputs["Index"] = -1.;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().find("Array index") != std::string::npos);
}
TEST(BlueprintArrayTypesAndBudgets)
{
    for (int t = int(Type::ArrayBool); t < int(Type::Count); ++t)
    {
        CHECK(Matches(Type(t), Value::array()));
        CHECK(IsArray(Type(t)));
    }
    CHECK(!Matches(Type::ArrayNumber, Value::array({ "wrong" })));
    CHECK(!Matches(Type::ArrayNumber, Value(std::vector<double>(1025, 1))));
    CHECK(!Matches(Type::ArrayString, Value::array({ std::string(40000, 'a'), std::string(40000, 'b') })));
}
TEST(BlueprintCustomEventsAreDeferredAndNamed)
{
    Graph g;
    g.Variables["Value"] = { Type::Number, 0. };
    g.Nodes = { { 1, "Event.Start" }, { 2, "EmitEvent", 0, 0, { { "Name", "Award" }, { "Number", 42. } } }, { 3, "Event.Custom", 0, 0, { { "Name", "Award" } } }, { 4, "Set.Number", 0, 0, { { "Name", "Value" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 3, "Then", 4, "In" }, { 3, "Number", 4, "Value" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Variables().at("Value") == 0.);
    vm.Tick(0);
    CHECK(vm.Error().empty() && vm.Variables().at("Value") == 42.);
}
TEST(BlueprintClipboardIsAtomicAndRemapsIDs)
{
    Graph g = Graph::Example();
    const auto size = g.Nodes.size();
    auto clip = g.CopyNodes({ 1, 2, 3, 4 });
    std::set<uint32_t> inserted;
    std::string error;
    CHECK(g.PasteNodes(clip, 40, 40, inserted, error));
    CHECK(g.Nodes.size() == size * 2 && inserted.size() == 4 && g.Validate().empty());
    auto before = g.Serialize();
    clip["Nodes"][0]["Kind"] = "Missing";
    CHECK(!g.PasteNodes(clip, 40, 40, inserted, error));
    CHECK(g.Serialize() == before && inserted.empty());
}
TEST(BlueprintClipboardClonesWholeTypedFunction)
{
    auto g = FunctionGraph();
    auto clip = g.CopyNodes({ 1, 2, 3, 10, 11, 12, 13, 14 });
    std::set<uint32_t> added;
    std::string error;
    CHECK(g.PasteNodes(clip, 50, 50, added, error));
    CHECK(g.Functions.size() == 2 && g.Validate().empty());
}
TEST(BlueprintFunctionPinChangePrunesInvalidLinks)
{
    auto g = FunctionGraph();
    g.Functions["Double"].Inputs[0].Kind = Type::String;
    g.Functions["Double"].Inputs[0].Default = "";
    CHECK(g.PruneInvalidPins() >= 2);
    CHECK(g.Validate().empty());
}
namespace {
    Graph ArrayOperation(Type type, const std::string& operation, Value inputs, Type result)
    {
        Graph g;
        g.Variables["Result"] = { result, DefaultValue(result) };
        g.Nodes = { { 1, "Event.Start" }, { 2, "Array." + operation + "." + TypeName(ElementType(type)), 0, 0, inputs.get<std::map<std::string, Value>>() }, { 3, "Set." + std::string(TypeName(result)), 0, 0, { { "Name", "Result" } } } };
        g.Links = { { 1, "Then", 3, "In" }, { 2, "Value", 3, "Value" } };
        return g;
    }
    Value ExecuteArray(Type type, const std::string& op, Value inputs, Type result)
    {
        Machine vm;
        CHECK(vm.Load(ArrayOperation(type, op, inputs, result)));
        vm.Event("Start");
        CHECK(vm.Error().empty());
        return vm.Variables().at("Result");
    }
}
TEST(BlueprintCollectionOperationsAcrossAllElementTypes)
{
    for (int i = int(Type::ArrayBool); i < int(Type::Count); ++i)
    {
        const auto type = Type(i), element = ElementType(type);
        Value item = DefaultValue(element), other = item;
        if (element == Type::Bool) other = true;
        else if (element == Type::Number) other = 7.;
        else if (element == Type::String) other = "second";
        else if (element == Type::Entity) other = 7ull;
        else other[0] = 7.;
        Value array = Value::array({ item, other });
        CHECK(ExecuteArray(type, "Empty", Value::object(), type) == Value::array());
        CHECK(ExecuteArray(type, "Append", { { "Array", Value::array({ item }) }, { "Value", other } }, type) == array);
        CHECK(ExecuteArray(type, "Set", { { "Array", array }, { "Index", 0. }, { "Value", other } }, type) == Value::array({ other, other }));
        CHECK(ExecuteArray(type, "Remove", { { "Array", array }, { "Index", 0. } }, type) == Value::array({ other }));
        CHECK(ExecuteArray(type, "Get", { { "Array", array }, { "Index", 1. } }, element) == other);
        CHECK(ExecuteArray(type, "Length", { { "Array", array } }, Type::Number) == 2.);
        CHECK(ExecuteArray(type, "Contains", { { "Array", array }, { "Value", other } }, Type::Bool) == true);
        CHECK(ExecuteArray(type, "Find", { { "Array", array }, { "Value", other } }, Type::Number) == 1.);
        CHECK(ExecuteArray(type, "Slice", { { "Array", array }, { "Start", 1. }, { "Count", 1. } }, type) == Value::array({ other }));
        CHECK(ExecuteArray(type, "Concat", { { "Array", Value::array({ item }) }, { "Other", Value::array({ other }) } }, type) == array);
    }
}
TEST(BlueprintCollectionBoundsRejectWithoutMutation)
{
    for (const auto& op : { "Get", "Set", "Remove" })
        for (const auto index : { -1., .5, 2., 1.e30 })
        {
            auto graph = ArrayOperation(Type::ArrayNumber, op, { { "Array", Value::array({ 1., 2. }) }, { "Index", index } }, std::string(op) == "Get" ? Type::Number : Type::ArrayNumber);
            Machine vm;
            CHECK(vm.Load(graph));
            vm.Event("Start");
            CHECK(!vm.Error().empty());
        }
    auto graph = ArrayOperation(Type::ArrayNumber, "Append", { { "Array", Value(std::vector<double>(1024, 1)) }, { "Value", 2. } }, Type::ArrayNumber);
    Machine vm;
    CHECK(vm.Load(graph));
    vm.Event("Start");
    CHECK(!vm.Error().empty());
    CHECK(vm.Variables().at("Result").empty());
}
TEST(BlueprintConcurrentDelayedCallsHaveIsolatedLocals)
{
    auto g = FunctionGraph();
    g.Nodes[0].Kind = "Event.Custom";
    g.Nodes[0].Inputs["Name"] = "Compute";
    g.Nodes.push_back({ 15, "Delay", 0, 0, { { "Seconds", .1 } } });
    for (auto& link : g.Links)
        if (link.From == 12 && link.Output == "Then") link.To = 15;
    g.Links.push_back({ 15, "Then", 14, "In" });
    g.Links.push_back({ 1, "Number", 2, "X" });
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Custom.Compute", { { "Number", 3. } });
    vm.Event("Custom.Compute", { { "Number", 9. } });
    CHECK(vm.Pending() == 2);
    CHECK(vm.DebugFrame().at("Arguments").at("X") == 3.);
    CHECK(vm.DebugFrame().at("Locals").at("Temp") == 6.);
    vm.Tick(.1);
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Answer") == 18.);
    CHECK(vm.DebugFrame().empty());
}
TEST(BlueprintNestedFunctionRestoresCallerArgumentsAndLocals)
{
    auto g = FunctionGraph();
    g.Functions["Identity"] = { { { "X", Type::Number, 0. } }, { { "Result", Type::Number, 0. } }, { { "Temp", { Type::Number, 999. } } } };
    g.Nodes.push_back({ 20, "Function.Entry", 0, 0, { { "Name", "Identity" } } });
    g.Nodes.push_back({ 21, "Function.Return", 0, 0, { { "Name", "Identity" } } });
    g.Nodes.push_back({ 22, "Function.Call", 0, 0, { { "Name", "Identity" }, { "X", 99. } } });
    g.Links.push_back({ 20, "Then", 21, "In" });
    g.Links.push_back({ 20, "X", 21, "Result" });
    for (auto& link : g.Links)
        if (link.From == 12 && link.Output == "Then") link.To = 22;
    g.Links.push_back({ 22, "Then", 14, "In" });
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Answer") == 14.);
}
TEST(BlueprintRecursiveCallsHaveBoundedDepthAndStackDiagnostic)
{
    auto g = FunctionGraph();
    g.Nodes.push_back({ 20, "Function.Call", 0, 0, { { "Name", "Double" } } });
    for (auto& link : g.Links)
        if (link.From == 10 && link.Output == "Then") link.To = 20;
    g.Links.push_back({ 20, "Then", 12, "In" });
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().find("depth exceeds 32") != std::string::npos);
    CHECK(vm.CallStack().find("Double") != std::string::npos);
    CHECK(vm.Pending() == 0);
}
TEST(BlueprintVoidFunctionMayFallThrough)
{
    Graph g;
    g.Functions["Run"] = {};
    g.Variables["Done"] = { Type::Bool, false };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Function.Call", 0, 0, { { "Name", "Run" } } }, { 3, "Set.Bool", 0, 0, { { "Name", "Done" }, { "Value", true } } }, { 4, "Function.Entry", 0, 0, { { "Name", "Run" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Done") == true);
}
TEST(BlueprintBreakCannotEscapeFunctionIntoCallerLoop)
{
    auto g = FunctionGraph();
    g.Nodes.push_back({ 20, "Break" });
    for (auto& link : g.Links)
        if (link.From == 12 && link.Output == "Then") link.To = 20;
    // Keep the return statically reachable on another branch; take Break at runtime.
    g.Nodes.push_back({ 21, "Branch", 0, 0, { { "Condition", true } } });
    for (auto& link : g.Links)
        if (link.From == 10 && link.Output == "Then") link.To = 21;
    g.Links.push_back({ 21, "True", 12, "In" });
    g.Links.push_back({ 21, "False", 14, "In" });
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().find("function boundary") != std::string::npos);
}
TEST(BlueprintInvalidSignaturesAndScopesAreRejected)
{
    auto base = FunctionGraph();
    for (const auto name : { "Name", "Then", "In", "" })
    {
        auto g = base;
        g.Functions["Double"].Inputs[0].Name = name;
        CHECK(!g.Validate().empty());
    }
    {
        auto g = base;
        g.Functions["Double"].Inputs.push_back(g.Functions["Double"].Inputs[0]);
        CHECK(!g.Validate().empty());
    }
    {
        auto g = base;
        g.Functions["Double"].Locals["Temp"].Kind = Type::String;
        g.Functions["Double"].Locals["Temp"].Default = "";
        CHECK(!g.Validate().empty());
    }
    {
        auto g = base;
        g.Nodes.push_back(g.Nodes[3]);
        g.Nodes.back().ID = 99;
        CHECK(!g.Validate().empty());
    }
    {
        auto g = base;
        g.Links.push_back({ 1, "Delta", 12, "Value" });
        CHECK(!g.Validate().empty());
    }
    {
        auto g = base;
        g.Functions["Double"].Inputs[0].Default = "bad";
        CHECK(!g.Validate().empty());
    }
}
TEST(BlueprintClipboardConflictNamesDoNotCascade)
{
    Graph source, target;
    source.Variables = { { "Foo", { Type::Number, 1. } }, { "Foo_copy1", { Type::Number, 2. } } };
    target.Variables = { { "Foo", { Type::Number, 3. } }, { "Foo_copy1", { Type::Number, 4. } } };
    source.Nodes = { { 1, "Get.Number", 0, 0, { { "Name", "Foo" } } }, { 2, "Get.Number", 0, 0, { { "Name", "Foo_copy1" } } } };
    std::set<uint32_t> ids;
    std::string error;
    CHECK(target.PasteNodes(source.CopyNodes({ 1, 2 }), 0, 0, ids, error));
    CHECK(target.Nodes[0].Inputs.at("Name") != target.Nodes[1].Inputs.at("Name"));
    CHECK(target.Variables.at(target.Nodes[0].Inputs.at("Name").get<std::string>()).Default == 1.);
    CHECK(target.Variables.at(target.Nodes[1].Inputs.at("Name").get<std::string>()).Default == 2.);
}
TEST(BlueprintClipboardRejectsIncompatibleExistingFunctionAtomically)
{
    auto source = FunctionGraph(), target = source;
    target.Functions["Double"].Inputs[0].Kind = Type::String;
    target.Functions["Double"].Inputs[0].Default = "";
    auto before = target.Serialize();
    std::set<uint32_t> ids;
    std::string error;
    CHECK(!target.PasteNodes(source.CopyNodes({ 2 }), 0, 0, ids, error));
    CHECK(target.Serialize() == before);
    CHECK(ids.empty());
}
TEST(BlueprintCustomEventPayloadAndPendingLimits)
{
    Graph graph;
    graph.Nodes = { { 1, "Event.Custom", 0, 0, { { "Name", "Wait" } } }, { 2, "Delay", 0, 0, { { "Seconds", 100. } } } };
    graph.Nodes.push_back({ 3, "Print" });
    graph.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" } };
    Machine vm;
    CHECK(vm.Load(graph));
    vm.Event("Custom.Wait", { { "Text", std::string(65536, 'x') } });
    CHECK(!vm.Error().empty());
    CHECK(vm.Load(graph));
    for (int i = 0; i < 129; ++i) vm.Event("Custom.Wait");
    CHECK(!vm.Error().empty());
    CHECK(vm.Pending() == 0);
}
TEST(BlueprintFiniteRecursionRestoresEachInvocationResults)
{
    Graph g;
    g.Variables["Sum"] = { Type::Number, 0. };
    g.Functions["SumTo"] = { { { "N", Type::Number, 0. } }, { { "Sum", Type::Number, 0. } }, {} };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Function.Call", 0, 0, { { "Name", "SumTo" }, { "N", 5. } } }, { 3, "Set.Number", 0, 0, { { "Name", "Sum" } } }, { 10, "Function.Entry", 0, 0, { { "Name", "SumTo" } } }, { 11, "Greater", 0, 0, { { "B", 0. } } }, { 12, "Branch" }, { 13, "Subtract", 0, 0, { { "B", 1. } } }, { 14, "Function.Call", 0, 0, { { "Name", "SumTo" } } }, { 15, "Add" }, { 16, "Function.Return", 0, 0, { { "Name", "SumTo" } } }, { 17, "Function.Return", 0, 0, { { "Name", "SumTo" }, { "Sum", 0. } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 2, "Sum", 3, "Value" }, { 10, "Then", 12, "In" }, { 10, "N", 11, "A" }, { 11, "Value", 12, "Condition" }, { 12, "True", 14, "In" }, { 12, "False", 17, "In" }, { 10, "N", 13, "A" }, { 13, "Value", 14, "N" }, { 14, "Then", 16, "In" }, { 10, "N", 15, "A" }, { 14, "Sum", 15, "B" }, { 15, "Value", 16, "Sum" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Sum") == 15.);
}
TEST(BlueprintForEachBreakPublishesCompletedExactlyOnce)
{
    Graph g;
    g.Variables = { { "Visits", { Type::Number, 0. } }, { "Completed", { Type::Bool, false } } };
    g.Nodes = { { 1, "Event.Start" }, { 2, "ForEach.String", 0, 0, { { "Array", Value::array({ "first", "second" }) } } }, { 3, "Set.Number", 0, 0, { { "Name", "Visits" }, { "Value", 1. } } }, { 4, "Break" }, { 5, "Set.Bool", 0, 0, { { "Name", "Completed" }, { "Value", true } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Body", 3, "In" }, { 3, "Then", 4, "In" }, { 2, "Completed", 5, "In" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Visits") == 1.);
    CHECK(vm.Variables().at("Completed") == true);
    g.Nodes[1].Inputs["Array"] = Value::array();
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Visits") == 0.);
    CHECK(vm.Variables().at("Completed") == true);
}
TEST(BlueprintRetainedPayloadIsBoundedAcrossPendingEvents)
{
    Graph g;
    for (int i = 0; i < 50; ++i) g.Variables["Text" + std::to_string(i)] = { Type::String, std::string(60000, 'x') };
    g.Nodes = { { 1, "Event.Custom", 0, 0, { { "Name", "Wait" } } }, { 2, "Delay", 0, 0, { { "Seconds", 100. } } }, { 3, "Print" } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" } };
    Machine vm;
    CHECK(vm.Load(g));
    for (int i = 0; i < 120 && vm.Error().empty(); ++i) vm.Event("Custom.Wait", { { "Text", std::string(60000, 'x') } });
    CHECK(vm.Error().find("8 MiB") != std::string::npos);
    CHECK(vm.Pending() == 0);
    CHECK(vm.RetainedBytes() <= Machine::MaxRetainedBytes);
}
TEST(BlueprintArrayArgumentsAndMultipleReturnsRemainTyped)
{
    Graph g;
    g.Variables = { { "Items", { Type::ArrayVec2, Value::array() } }, { "Ready", { Type::Bool, false } } };
    auto items = Value::array({ Value::array({ 1., 2. }), Value::array({ 3., 4. }) });
    g.Functions["Pair"] = { { { "Items", Type::ArrayVec2, Value::array() } }, { { "Items", Type::ArrayVec2, Value::array() }, { "Ready", Type::Bool, true } }, {} };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Function.Call", 0, 0, { { "Name", "Pair" }, { "Items", items } } }, { 3, "Set.ArrayVec2", 0, 0, { { "Name", "Items" } } }, { 4, "Set.Bool", 0, 0, { { "Name", "Ready" } } }, { 10, "Function.Entry", 0, 0, { { "Name", "Pair" } } }, { 11, "Function.Return", 0, 0, { { "Name", "Pair" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 2, "Items", 3, "Value" }, { 3, "Then", 4, "In" }, { 2, "Ready", 4, "Value" }, { 10, "Then", 11, "In" }, { 10, "Items", 11, "Items" } };
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Items") == items);
    CHECK(vm.Variables().at("Ready") == true);
}
TEST(BlueprintFunctionCannotCaptureCallerExecutionResults)
{
    auto g = FunctionGraph();
    g.Nodes.push_back({ 30, "For", 0, 0, { { "Count", 1. } } });
    g.Links[0] = { 1, "Then", 30, "In" };
    g.Links.push_back({ 30, "Body", 2, "In" });
    for (auto& link : g.Links)
        if (link.From == 10 && link.Output == "X") link = { 30, "Index", 11, "A" };
    CHECK(!g.Validate().empty());
}
TEST(BlueprintEmptyHostPayloadKeepsLegacyEventCompatibility)
{
    auto graph = FunctionGraph();
    Machine vm;
    CHECK(vm.Load(graph));
    vm.Event("Start", {});
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Answer") == 14.);
    CHECK(vm.Load(graph));
    vm.Event("Start", Value::array({ 1. }));
    CHECK(!vm.Error().empty());
}
TEST(BlueprintUserFunctionNamesCannotAliasEventScopes)
{
    Graph g;
    g.Functions["@1"] = { { { "X", Type::Number, 1. } }, {}, {} };
    g.Variables["Answer"] = { Type::Number, 0. };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Function.Entry", 0, 0, { { "Name", "@1" } } }, { 3, "Function.Call", 0, 0, { { "Name", "@1" } } }, { 4, "Set.Number", 0, 0, { { "Name", "Answer" } } } };
    g.Links = { { 1, "Then", 3, "In" }, { 3, "Then", 4, "In" }, { 2, "X", 4, "Value" } };
    CHECK(!g.Validate().empty());
    g.Links.pop_back();
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
}
