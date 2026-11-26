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
namespace {
    bool Mentions(const std::vector<Diagnostic>& diagnostics, const std::string& text)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& d) { return d.Message.find(text) != std::string::npos; });
    }
    // Mode { Idle = 0, Walk = 1, Run = 5 }: a graph-local enum with an explicit, non-contiguous value.
    Graph EnumGraph()
    {
        Graph g;
        g.Enums["Mode"] = { { { "Idle", 0 }, { "Walk", 1 }, { "Run", 5 } }, "" };
        g.Variables["Current"] = { Type::Enum, 5, "Mode" };
        g.Variables["Label"] = { Type::String, "" };
        g.Variables["Picked"] = { Type::String, "" };
        g.Variables["Number"] = { Type::Number, 0. };
        g.Variables["Same"] = { Type::Bool, false };
        g.Nodes = { { 1, "Event.Start" }, { 2, "Get.Enum", 0, 0, { { "Name", "Current" } } }, { 3, "Enum.Switch", 0, 0, { { "Enum", "Mode" } } }, { 4, "Set.String", 0, 0, { { "Name", "Picked" }, { "Value", "walk" } } }, { 5, "Set.String", 0, 0, { { "Name", "Picked" }, { "Value", "run" } } }, { 6, "Enum.ToText", 0, 0, { { "Enum", "Mode" } } }, { 7, "Set.String", 0, 0, { { "Name", "Label" } } }, { 8, "Enum.ToNumber", 0, 0, { { "Enum", "Mode" } } }, { 9, "Set.Number", 0, 0, { { "Name", "Number" } } }, { 10, "Enum.FromNumber", 0, 0, { { "Enum", "Mode" }, { "Number", 5. } } }, { 11, "Enum.Equal", 0, 0, { { "Enum", "Mode" } } }, { 12, "Set.Bool", 0, 0, { { "Name", "Same" } } } };
        g.Links = { { 1, "Then", 3, "In" }, { 2, "Value", 3, "Value" }, { 3, "Walk", 4, "In" }, { 3, "Run", 5, "In" }, { 5, "Then", 7, "In" }, { 2, "Value", 6, "Value" }, { 6, "Value", 7, "Value" }, { 7, "Then", 9, "In" }, { 2, "Value", 8, "Value" }, { 8, "Value", 9, "Value" }, { 9, "Then", 12, "In" }, { 2, "Value", 11, "A" }, { 10, "Value", 11, "B" }, { 11, "Value", 12, "Value" } };
        return g;
    }
}
TEST(BlueprintFunctionWithoutReachableReturnIsACompileError)
{
    auto g = FunctionGraph();
    std::erase_if(g.Links, [](const Link& l) { return l.To == 14 && l.Input == "In"; });
    CHECK(Mentions(g.Validate(), "no reachable Return"));
    Machine vm;
    CHECK(!vm.Load(g) && vm.Error().find("no reachable Return") != std::string::npos);
    // A function without results may still end anywhere.
    g.Functions["Double"].Outputs.clear();
    g.PruneInvalidPins();
    std::erase_if(g.Nodes, [](const Node& n) { return n.ID == 3 || n.ID == 13 || n.ID == 14; });
    std::erase_if(g.Links, [](const Link& l) { return l.To == 3 || l.From == 3 || l.From == 13 || l.To == 14; });
    CHECK(g.Validate().empty() && g.Warnings().empty());
}
TEST(BlueprintPartialReturnPathsAreWarnings)
{
    auto g = FunctionGraph();
    CHECK(g.Warnings().empty());
    // Branch: True reaches Return, False ends -> the graph loads, the editor warns.
    g.Nodes.push_back({ 15, "Branch", 0, 0, { { "Condition", true } } });
    for (auto& link : g.Links)
        if (link.From == 10 && link.Output == "Then") link.To = 15;
    g.Links.push_back({ 15, "True", 12, "In" });
    CHECK(g.Validate().empty());
    const auto warnings = g.Warnings();
    CHECK(warnings.size() == 1 && warnings[0].Node == 10 && warnings[0].Message.find("Double") != std::string::npos);
    // Both branches returning clears the warning; a loop counts only its Completed output.
    g.Links.push_back({ 15, "False", 14, "In" });
    CHECK(g.Validate().empty() && g.Warnings().empty());
    auto loop = FunctionGraph();
    loop.Nodes.push_back({ 16, "For", 0, 0, { { "Count", 2. } } });
    for (auto& link : loop.Links)
        if (link.From == 12 && link.Output == "Then") link.From = 16, link.Output = "Completed";
    loop.Links.push_back({ 12, "Then", 16, "In" });
    CHECK(loop.Validate().empty() && loop.Warnings().empty());
    // A Sequence returns when one of its outputs returns on every path.
    auto sequence = FunctionGraph();
    sequence.Nodes.push_back({ 17, "Sequence" });
    sequence.Nodes.push_back({ 18, "Print", 0, 0, { { "Text", "first" } } });
    for (auto& link : sequence.Links)
        if (link.From == 10 && link.Output == "Then") link.To = 17;
    sequence.Links.push_back({ 17, "First", 18, "In" });
    sequence.Links.push_back({ 17, "Second", 12, "In" });
    CHECK(sequence.Validate().empty() && sequence.Warnings().empty());
}
TEST(BlueprintCallArityAndSignatureDiagnostics)
{
    auto g = FunctionGraph();
    g.Nodes[1].Inputs["Y"] = 3.; // the Call of Double(X) gets an argument the signature does not have
    CHECK(Mentions(g.Validate(), "Argument Y is not in the signature of Double"));
    g = FunctionGraph();
    g.Links.push_back({ 2, "Missing", 3, "Value" });
    CHECK(Mentions(g.Validate(), "Pin Missing is not in the function signature"));
    g = FunctionGraph();
    g.Nodes[1].Inputs["Name"] = "Triple";
    CHECK(Mentions(g.Validate(), "Typed function not found: Triple"));
    // Wrong argument type: a String wire into the Number parameter.
    g = FunctionGraph();
    g.Nodes.push_back({ 30, "Constant.String", 0, 0, { { "Value", "x" } } });
    g.Links.push_back({ 30, "Value", 2, "X" });
    CHECK(Mentions(g.Validate(), "Link pin types differ"));
}
TEST(BlueprintFormatVersion2AndVersion1Migration)
{
    auto g = FunctionGraph();
    auto json = g.Serialize();
    CHECK(json.at("Version") == Graph::FormatVersion && Graph::FormatVersion == 2 && json.at("Enums").is_object());
    // A version 1 file (no Enums section): typed functions, locals and legacy names read unchanged.
    auto v1 = json;
    v1["Version"] = 1;
    v1.erase("Enums");
    v1["Nodes"].push_back({ { "ID", 40 }, { "Kind", "Function" }, { "X", 0 }, { "Y", 0 }, { "Inputs", { { "Name", "Legacy" } } }, { "Breakpoint", false } });
    v1["Nodes"].push_back({ { "ID", 41 }, { "Kind", "Event.Create" }, { "X", 0 }, { "Y", 0 }, { "Inputs", Value::object() }, { "Breakpoint", false } });
    v1["Nodes"].push_back({ { "ID", 42 }, { "Kind", "Call" }, { "X", 0 }, { "Y", 0 }, { "Inputs", { { "Name", "Legacy" } } }, { "Breakpoint", false } });
    v1["Links"].push_back({ { "From", 41 }, { "Output", "Then" }, { "To", 42 }, { "Input", "In" } });
    auto migrated = Graph::Parse(v1);
    CHECK(migrated.SourceVersion == 1 && migrated.Validate().empty());
    Machine vm;
    CHECK(vm.Load(migrated));
    vm.Event("Create");
    vm.Event("Start");
    CHECK(vm.Error().empty() && vm.Variables().at("Answer") == 14.);
    const auto upgraded = migrated.Serialize();
    CHECK(upgraded.at("Version") == 2 && Graph::Parse(upgraded).SourceVersion == 2 && Graph::Parse(upgraded).Serialize() == upgraded);
    // Enums do not exist in version 1; a later version is not guessed at.
    auto enumV1 = EnumGraph().Serialize();
    enumV1["Version"] = 1;
    enumV1.erase("Enums");
    bool threw = false;
    try { Graph::Parse(enumV1); } catch (const std::exception& e) { threw = std::string(e.what()).find("version 2") != std::string::npos; }
    CHECK(threw);
    auto future = json;
    future["Version"] = 3;
    threw = false;
    try { Graph::Parse(future); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}
TEST(BlueprintEnumSwitchAndConversionsRun)
{
    auto g = EnumGraph();
    CHECK(g.Validate().empty());
    const auto def = g.Describe(*g.Find(3));
    CHECK(def.Outputs.size() == 3 && def.Outputs[2].Name == "Run" && def.Outputs[2].Kind == Type::Exec);
    CHECK(def.Inputs.back().Kind == Type::Enum && def.Inputs.back().Enum == "Mode" && def.Inputs.back().Default == 0);
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    CHECK(vm.Variables().at("Picked") == "run" && vm.Variables().at("Label") == "Run" && vm.Variables().at("Number") == 5. && vm.Variables().at("Same") == true);
    // A per-instance override picks another entry; a value outside the enum is ignored.
    Machine walk;
    CHECK(walk.Load(g, {}, { { "Current", 1 } }));
    walk.Event("Start");
    CHECK(walk.Variables().at("Picked") == "walk" && walk.Variables().at("Label") == "") ;
    Machine invalid;
    CHECK(invalid.Load(g, {}, { { "Current", 3 } }));
    CHECK(invalid.Variables().at("Current") == 5);
    // Round trip keeps the enum, its values and the enum-typed variable.
    auto copy = Graph::Parse(g.Serialize());
    CHECK(copy.Enums == g.Enums && copy.Variables.at("Current").Enum == "Mode" && copy.Serialize() == g.Serialize());
}
TEST(BlueprintEnumTypesAreCheckedStaticallyAndAtRuntime)
{
    auto g = EnumGraph();
    g.Enums["Team"] = { { { "Red", 0 }, { "Blue", 1 } }, "" };
    auto wrongWire = g;
    wrongWire.Nodes.push_back({ 20, "Constant.Enum", 0, 0, { { "Enum", "Team" } } });
    wrongWire.Links.back() = { 20, "Value", 11, "B" }; // Team into a Mode pin
    CHECK(Mentions(wrongWire.Validate(), "Link pin types differ"));
    auto literal = g;
    literal.Nodes.push_back({ 21, "Constant.Enum", 0, 0, { { "Enum", "Mode" }, { "Value", 3 } } }); // 3 is not a Mode value
    CHECK(Mentions(literal.Validate(), "Invalid input: Value"));
    auto unknown = g;
    unknown.Nodes[2].Inputs["Enum"] = "Missing";
    CHECK(Mentions(unknown.Validate(), "Unknown enum: Missing"));
    auto variable = g;
    variable.Variables["Current"].Default = 2;
    CHECK(Mentions(variable.Validate(), "Invalid variable: Current"));
    auto mismatch = g;
    mismatch.Nodes[1].Inputs["Enum"] = "Team"; // Get.Enum of a Mode variable declared as Team
    CHECK(Mentions(mismatch.Validate(), "Variable type differs from node"));
    auto wiredType = g;
    wiredType.Nodes.push_back({ 22, "Constant.String", 0, 0, { { "Value", "Mode" } } });
    wiredType.Links.push_back({ 22, "Value", 6, "Enum" });
    CHECK(Mentions(wiredType.Validate(), "Enum type must be a literal"));
    auto duplicate = g;
    duplicate.Enums["Mode"].Entries.push_back({ "Walk", 9 });
    CHECK(Mentions(duplicate.Validate(), "duplicate entry name"));
    duplicate = g;
    duplicate.Enums["Mode"].Entries.push_back({ "Sprint", 5 });
    CHECK(Mentions(duplicate.Validate(), "duplicate or out-of-range value"));
    // FromNumber with a number that is no entry stops the node that reads it (Set.Bool #12).
    auto runtime = g;
    runtime.Nodes[9].Inputs["Number"] = 2.;
    Machine vm;
    CHECK(vm.Load(runtime));
    vm.Event("Start");
    CHECK(vm.Error().find("Node 12") != std::string::npos && vm.Error().find("not a value of enum Mode") != std::string::npos);
}
TEST(BlueprintEnumFunctionParametersReturnsLocalsAndRename)
{
    Graph g;
    g.Enums["Mode"] = { { { "Idle", 0 }, { "Walk", 1 }, { "Run", 5 } }, "" };
    g.Variables["Result"] = { Type::Enum, 0, "Mode" };
    // Faster(Mode In) -> Mode Out: Idle -> Walk, otherwise Run; the pick goes through an Enum local.
    g.Functions["Faster"] = { { { "Current", Type::Enum, 0, "Mode" } }, { { "Out", Type::Enum, 0, "Mode" } }, { { "Pick", { Type::Enum, 5, "Mode" } } } };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Function.Call", 0, 0, { { "Name", "Faster" }, { "Current", 0 } } }, { 3, "Set.Enum", 0, 0, { { "Name", "Result" } } },
                { 10, "Function.Entry", 0, 0, { { "Name", "Faster" } } }, { 11, "Enum.Switch", 0, 0, { { "Enum", "Mode" } } }, { 12, "Local.Set.Enum", 0, 0, { { "Name", "Pick" }, { "Value", 1 } } },
                { 13, "Local.Get.Enum", 0, 0, { { "Name", "Pick" } } }, { 14, "Function.Return", 0, 0, { { "Name", "Faster" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 2, "Out", 3, "Value" }, { 10, "Then", 11, "In" }, { 10, "Current", 11, "Value" }, { 11, "Idle", 12, "In" }, { 12, "Then", 14, "In" }, { 11, "Walk", 14, "In" }, { 11, "Run", 14, "In" }, { 13, "Value", 14, "Out" } };
    CHECK(g.Validate().empty() && g.Warnings().empty());
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty() && vm.Variables().at("Result") == 1);
    // Renaming the enum retypes the signature, the local, the variable and every enum node; uses are counted.
    CHECK(g.EnumUses("Mode") == 11); // 7 nodes, 1 variable, a parameter, a result and a local
    CHECK(g.RenameEnum("Mode", "Gait") && !g.RenameEnum("Gait", "") && g.Validate().empty());
    CHECK(g.Functions.at("Faster").Locals.at("Pick").Enum == "Gait" && g.Variables.at("Result").Enum == "Gait" && g.Nodes[4].Inputs.at("Enum") == "Gait");
    auto copy = Graph::Parse(g.Serialize());
    CHECK(copy.Serialize() == g.Serialize() && copy.Functions.at("Faster").Outputs[0].Enum == "Gait");
    // An enum argument that is not an entry is rejected when the call runs.
    g.Nodes[1].Inputs["Current"] = 7;
    CHECK(Mentions(g.Validate(), "Invalid input: Current"));
    // Recursion with enum arguments stays bounded by the typed call depth.
    auto recursive = Graph::Parse(copy.Serialize());
    recursive.Nodes.push_back({ 20, "Function.Call", 0, 0, { { "Name", "Faster" } } });
    for (auto& link : recursive.Links)
        if (link.From == 11 && link.Output == "Idle") link.To = 20;
    recursive.Links.push_back({ 20, "Then", 12, "In" });
    recursive.Links.push_back({ 10, "Current", 20, "Current" });
    Machine deep;
    CHECK(deep.Load(recursive));
    deep.Event("Start");
    CHECK(deep.Error().find("depth exceeds 32") != std::string::npos && deep.Pending() == 0);
}
TEST(BlueprintEnumPasteSharesEqualDefinitionsOnly)
{
    auto source = EnumGraph();
    const auto fragment = source.CopyNodes({ 6, 10, 11 });
    Graph target;
    std::set<uint32_t> ids;
    std::string error;
    CHECK(target.PasteNodes(fragment, 10, 10, ids, error) && ids.size() == 3 && target.Enums.at("Mode") == source.Enums.at("Mode"));
    Graph conflicting;
    conflicting.Enums["Mode"] = { { { "Idle", 0 } }, "" };
    conflicting.Nodes = { { 1, "Event.Start" } };
    CHECK(!conflicting.PasteNodes(fragment, 0, 0, ids, error) && error.find("Enum Mode differs") != std::string::npos);
    CHECK(conflicting.Nodes.size() == 1 && conflicting.Enums.at("Mode").Entries.size() == 1);
}
TEST(BlueprintFunctionNodesSelectsTheBodyOnly)
{
    auto g = FunctionGraph();
    const auto body = g.FunctionNodes("Double");
    CHECK(body == std::set<uint32_t>({ 10, 11, 12, 13, 14 }));
    CHECK(g.FunctionNodes("Missing").empty());
}
