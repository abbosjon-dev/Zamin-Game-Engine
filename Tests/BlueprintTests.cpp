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
    // The value element types; struct arrays need a struct definition (BlueprintStructArraysRunTheArrayNodes).
    for (int i = int(Type::ArrayBool); i <= int(Type::ArrayEntity); ++i)
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
TEST(BlueprintFormatVersion4AndOlderMigration)
{
    auto g = FunctionGraph();
    auto json = g.Serialize();
    CHECK(json.at("Version") == Graph::FormatVersion && Graph::FormatVersion == 4 && json.at("Enums").is_object() && json.at("Structs").is_object() && json.at("Macros").is_object());
    // A version 1 file (no Enums/Structs/Macros section): typed functions, locals and legacy names read unchanged.
    auto v1 = json;
    v1["Version"] = 1;
    v1.erase("Enums");
    v1.erase("Structs");
    v1.erase("Macros");
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
    CHECK(upgraded.at("Version") == 4 && Graph::Parse(upgraded).SourceVersion == 4 && Graph::Parse(upgraded).Serialize() == upgraded);
    // A version 2 file (enums, no Structs section) loads unchanged and runs the same.
    auto enumV2 = EnumGraph().Serialize();
    enumV2["Version"] = 2;
    enumV2.erase("Structs");
    enumV2.erase("Macros");
    auto fromV2 = Graph::Parse(enumV2);
    CHECK(fromV2.SourceVersion == 2 && fromV2.Validate().empty() && fromV2.Enums == EnumGraph().Enums && fromV2.Serialize() == EnumGraph().Serialize());
    // Enums do not exist in version 1, structs not in 1-2; a later version is not guessed at.
    auto enumV1 = EnumGraph().Serialize();
    enumV1["Version"] = 1;
    enumV1.erase("Enums");
    enumV1.erase("Structs");
    enumV1.erase("Macros");
    bool threw = false;
    try { Graph::Parse(enumV1); } catch (const std::exception& e) { threw = std::string(e.what()).find("version 2") != std::string::npos; }
    CHECK(threw);
    auto structV2 = json;
    structV2["Version"] = 2;
    structV2.erase("Macros");
    structV2["Structs"] = { { "Point", { { "Fields", Value::array({ { { "Name", "X" }, { "Type", "Number" }, { "Default", 0 } } }) } } } };
    threw = false;
    try { Graph::Parse(structV2); } catch (const std::exception& e) { threw = std::string(e.what()).find("version 3") != std::string::npos; }
    CHECK(threw);
    auto structVariableV2 = json;
    structVariableV2["Version"] = 2;
    structVariableV2.erase("Structs");
    structVariableV2.erase("Macros");
    structVariableV2["Variables"]["P"] = { { "Type", "Struct" }, { "Default", Value::object() }, { "Struct", "Point" } };
    threw = false;
    try { Graph::Parse(structVariableV2); } catch (const std::exception& e) { threw = std::string(e.what()).find("version 3") != std::string::npos; }
    CHECK(threw);
    auto future = json;
    future["Version"] = 5;
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
namespace {
    // Start -> Set Out = A * 2 -> Set Done = true; A is read by a pure Get outside the selection.
    Graph CollapseGraph()
    {
        Graph g;
        g.Variables = { { "A", { Type::Number, 3. } }, { "Out", { Type::Number, 0. } }, { "Done", { Type::Bool, false } } };
        g.Nodes = { { 1, "Event.Start" }, { 2, "Get.Number", 0, 100, { { "Name", "A" } } }, { 3, "Multiply", 200, 100, { { "B", 2. } } }, { 4, "Set.Number", 300, 0, { { "Name", "Out" } } }, { 5, "Set.Bool", 600, 0, { { "Name", "Done" }, { "Value", true } } } };
        g.Links = { { 1, "Then", 4, "In" }, { 2, "Value", 3, "A" }, { 3, "Value", 4, "Value" }, { 4, "Then", 5, "In" } };
        return g;
    }
    std::map<std::string, Value> RunStart(const Graph& g)
    {
        Machine vm;
        CHECK(vm.Load(g));
        vm.Event("Start");
        CHECK(vm.Error().empty());
        return vm.Variables();
    }
}
TEST(BlueprintCollapseSelectionToFunctionKeepsBehaviour)
{
    auto g = CollapseGraph();
    const auto expected = RunStart(g);
    uint32_t call = 0;
    std::string error;
    CHECK(g.CollapseToFunction({ 3, 4 }, "Scale", call, error) && error.empty() && call == 7);
    CHECK(g.Validate().empty() && g.Warnings().empty());
    const auto& signature = g.Functions.at("Scale");
    CHECK(signature.Inputs.size() == 1 && signature.Inputs[0].Name == "Value" && signature.Inputs[0].Kind == Type::Number && signature.Outputs.empty());
    CHECK(g.Find(call)->Kind == "Function.Call" && g.Find(call)->X == 200.f);
    // The call sits in the chain: Start -> Call -> Set Done; the Get feeds its parameter.
    auto linked = [&](uint32_t from, const char* output, uint32_t to, const char* input) { return std::any_of(g.Links.begin(), g.Links.end(), [&](const Link& l) { return l.From == from && l.Output == output && l.To == to && l.Input == input; }); };
    CHECK(linked(1, "Then", call, "In") && linked(call, "Then", 5, "In") && linked(2, "Value", call, "Value") && linked(6, "Value", 3, "A") && linked(4, "Then", 8, "In"));
    CHECK(g.FunctionNodes("Scale") == std::set<uint32_t>({ 3, 4, 6, 8 }));
    CHECK(RunStart(g) == expected && expected.at("Out") == 6.);
    // A second collapse with the same name picks a free one.
    auto again = CollapseGraph();
    again.Functions["Scale"] = {};
    again.Nodes.push_back({ 9, "Function.Entry", 0, 0, { { "Name", "Scale" } } });
    CHECK(again.CollapseToFunction({ 4 }, "Scale", call, error) && again.Functions.contains("Scale2"));
}
TEST(BlueprintCollapseTurnsExecutionResultsIntoReturns)
{
    // Collapsing the call of Double(7) wraps it: its Result leaves the selection, so it becomes a result.
    auto g = FunctionGraph();
    uint32_t call = 0;
    std::string error;
    CHECK(g.CollapseToFunction({ 2 }, "", call, error));
    CHECK(g.Functions.contains("Collapsed") && g.Functions.at("Collapsed").Outputs.size() == 1 && g.Functions.at("Collapsed").Outputs[0].Name == "Result");
    CHECK(g.Validate().empty() && g.Warnings().empty());
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty() && vm.Variables().at("Answer") == 14.);
    // Undo in the editor is a serialized snapshot: the collapsed graph round-trips exactly.
    CHECK(Graph::Parse(g.Serialize()).Serialize() == g.Serialize());
}
TEST(BlueprintCollapseRejectsUnsafeSelectionsAtomically)
{
    const auto base = CollapseGraph();
    const auto snapshot = base.Serialize();
    uint32_t call = 0;
    std::string error;
    auto g = base;
    CHECK(!g.CollapseToFunction({ 1, 4 }, "", call, error) && error.find("Events") != std::string::npos);
    CHECK(!g.CollapseToFunction({ 3 }, "", call, error) && error.find("incoming execution wire") != std::string::npos);
    CHECK(!g.CollapseToFunction({ 2, 4 }, "", call, error) && error.find("pure node also feeds") != std::string::npos); // the Get also feeds the Multiply
    auto shared = base;
    shared.Nodes.push_back({ 6, "Set.Number", 0, 0, { { "Name", "Out" } } });
    shared.Links.push_back({ 5, "Then", 6, "In" });
    shared.Links.push_back({ 3, "Value", 6, "Value" });
    CHECK(!shared.CollapseToFunction({ 3, 4 }, "", call, error) && error.find("pure node also feeds") != std::string::npos);
    CHECK(!g.CollapseToFunction({ 99 }, "", call, error) && error.find("does not exist") != std::string::npos);
    CHECK(!g.CollapseToFunction({}, "", call, error));
    CHECK(call == 0 && g.Serialize() == snapshot);
}
TEST(BlueprintSignatureAndEnumEntryRenamesKeepWires)
{
    auto g = FunctionGraph();
    CHECK(g.RenameFunctionPin("Double", false, "X", "Input") && g.RenameFunctionPin("Double", true, "Result", "Twice"));
    CHECK(!g.RenameFunctionPin("Double", false, "Input", "Then") && !g.RenameFunctionPin("Double", false, "Missing", "Y") && !g.RenameFunctionPin("Nope", false, "Input", "Y"));
    CHECK(g.Nodes[1].Inputs.contains("Input") && !g.Nodes[1].Inputs.contains("X"));
    CHECK(g.Validate().empty() && g.PruneInvalidPins() == 0);
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty() && vm.Variables().at("Answer") == 14.);
    auto e = EnumGraph();
    CHECK(e.RenameEnumEntry("Mode", "Run", "Sprint") && !e.RenameEnumEntry("Mode", "Walk", "Idle") && !e.RenameEnumEntry("Mode", "Missing", "X"));
    CHECK(e.Validate().empty() && e.PruneInvalidPins() == 0);
    Machine run;
    CHECK(run.Load(e));
    run.Event("Start");
    CHECK(run.Variables().at("Picked") == "run" && run.Variables().at("Label") == "Sprint");
}
namespace {
    // Struct Hit { Damage: Number, Kind: Element enum, Tags: ArrayString, Label: String }. Start calls
    // Scale(Base = Make{4, Ice, "bolt"}, Factor = 3) -> Hit, whose body keeps the scaled hit in a struct local;
    // the result goes to Last, is appended to Hits, broken into Total and its Kind field read into KindName.
    Graph StructGraph()
    {
        Graph g;
        g.Enums["Element"] = { { { "Fire", 0 }, { "Ice", 1 } }, "" };
        g.Structs["Hit"] = { { { "Damage", Type::Number, 5. }, { "Kind", Type::Enum, 0, "Element" }, { "Tags", Type::ArrayString, Value::array() }, { "Label", Type::String, "hit" } } };
        const Value hit = { { "Damage", 5. }, { "Kind", 0 }, { "Tags", Value::array() }, { "Label", "hit" } };
        g.Variables["Last"] = { Type::Struct, hit, "Hit" };
        g.Variables["Hits"] = { Type::ArrayStruct, Value::array(), "Hit" };
        g.Variables["Total"] = { Type::Number, 0. };
        g.Variables["KindName"] = { Type::String, "" };
        g.Functions["Scale"] = { { { "Base", Type::Struct, hit, "Hit" }, { "Factor", Type::Number, 1. } }, { { "Result", Type::Struct, hit, "Hit" } }, { { "Work", { Type::Struct, hit, "Hit" } } } };
        g.Nodes = { { 1, "Event.Start" },
                    { 2, "Struct.Make", 0, 0, { { "Struct", "Hit" }, { "Damage", 4. }, { "Kind", 1 }, { "Label", "bolt" } } },
                    { 3, "Function.Call", 0, 0, { { "Name", "Scale" }, { "Factor", 3. } } },
                    { 4, "Set.Struct", 0, 0, { { "Name", "Last" } } },
                    { 5, "Get.Struct", 0, 0, { { "Name", "Last" } } },
                    { 6, "Get.ArrayStruct", 0, 0, { { "Name", "Hits" } } },
                    { 7, "Array.Append.Struct", 0, 0, { { "Struct", "Hit" } } },
                    { 8, "Set.ArrayStruct", 0, 0, { { "Name", "Hits" }, { "Struct", "Hit" } } },
                    { 9, "Struct.Break", 0, 0, { { "Struct", "Hit" } } },
                    { 10, "Set.Number", 0, 0, { { "Name", "Total" } } },
                    { 11, "Struct.GetField", 0, 0, { { "Struct", "Hit" }, { "Field", "Kind" } } },
                    { 12, "Enum.ToText", 0, 0, { { "Enum", "Element" } } },
                    { 13, "Set.String", 0, 0, { { "Name", "KindName" } } },
                    { 20, "Function.Entry", 0, 0, { { "Name", "Scale" } } },
                    { 21, "Struct.GetField", 0, 0, { { "Struct", "Hit" }, { "Field", "Damage" } } },
                    { 22, "Multiply" },
                    { 23, "Struct.SetField", 0, 0, { { "Struct", "Hit" }, { "Field", "Damage" } } },
                    { 24, "Local.Set.Struct", 0, 0, { { "Name", "Work" } } },
                    { 25, "Local.Get.Struct", 0, 0, { { "Name", "Work" } } },
                    { 26, "Function.Return", 0, 0, { { "Name", "Scale" } } } };
        g.Links = { { 1, "Then", 3, "In" }, { 2, "Value", 3, "Base" }, { 3, "Then", 4, "In" }, { 3, "Result", 4, "Value" },
                    { 4, "Then", 8, "In" }, { 6, "Value", 7, "Array" }, { 5, "Value", 7, "Value" }, { 7, "Value", 8, "Value" },
                    { 8, "Then", 10, "In" }, { 5, "Value", 9, "Value" }, { 9, "Damage", 10, "Value" },
                    { 10, "Then", 13, "In" }, { 5, "Value", 11, "Value" }, { 11, "Value", 12, "Value" }, { 12, "Value", 13, "Value" },
                    { 20, "Then", 24, "In" }, { 20, "Base", 21, "Value" }, { 21, "Value", 22, "A" }, { 20, "Factor", 22, "B" },
                    { 20, "Base", 23, "Value" }, { 22, "Value", 23, "FieldValue" }, { 23, "Value", 24, "Value" }, { 24, "Then", 26, "In" }, { 25, "Value", 26, "Result" } };
        return g;
    }
    std::map<std::string, Value> RunStruct(const Graph& g)
    {
        Machine vm;
        CHECK(vm.Load(g));
        vm.Event("Start");
        CHECK(vm.Error().empty());
        if (!vm.Error().empty()) std::printf("    VM: %s\n", vm.Error().c_str());
        return vm.Variables();
    }
}
TEST(BlueprintStructsMakeBreakFieldsFunctionsAndLocalsRun)
{
    auto g = StructGraph();
    for (const auto& d : g.Validate()) std::printf("    %u: %s\n", d.Node, d.Message.c_str());
    CHECK(g.Validate().empty() && g.Warnings().empty());
    // The pins follow the struct: Make takes a pin per field, Break gives one, GetField/SetField the named field.
    const auto make = g.Describe(*g.Find(2)), split = g.Describe(*g.Find(9)), get = g.Describe(*g.Find(11)), set = g.Describe(*g.Find(23));
    CHECK(make.Inputs.size() == 5 && make.Inputs[2].Name == "Kind" && make.Inputs[2].Kind == Type::Enum && make.Inputs[2].Enum == "Element");
    CHECK(make.Outputs[0].Kind == Type::Struct && make.Outputs[0].Enum == "Hit" && make.Outputs[0].Default == g.DefaultFor(Type::Struct, "Hit"));
    CHECK(split.Outputs.size() == 4 && split.Outputs[3].Name == "Label" && split.Outputs[3].Kind == Type::String);
    CHECK(get.Outputs.size() == 1 && get.Outputs[0].Kind == Type::Enum && get.Outputs[0].Enum == "Element");
    CHECK(set.Inputs.back().Name == "FieldValue" && set.Inputs.back().Kind == Type::Number);
    const auto vars = RunStruct(g);
    const Value expected = { { "Damage", 12. }, { "Kind", 1 }, { "Tags", Value::array() }, { "Label", "bolt" } };
    CHECK(vars.at("Last") == expected && vars.at("Hits") == Value::array({ expected }));
    CHECK(vars.at("Total") == 12. && vars.at("KindName") == "Ice");
    // A per-instance override of a struct variable is used when it fits, ignored when its shape is wrong.
    Machine overridden;
    Value custom = expected;
    custom["Label"] = "custom";
    CHECK(overridden.Load(g, {}, { { "Last", custom } }) && overridden.Variables().at("Last") == custom);
    Machine wrong;
    CHECK(wrong.Load(g, {}, { { "Last", { { "Damage", 1. } } } }) && wrong.Variables().at("Last") == g.Variables.at("Last").Default);
}
TEST(BlueprintStructTypesAreCheckedStaticallyAndAtRuntime)
{
    const auto g = StructGraph();
    auto other = g;
    other.Structs["Other"] = other.Structs.at("Hit");
    other.Nodes[1].Inputs["Struct"] = "Other"; // an Other value into the Hit parameter
    CHECK(Mentions(other.Validate(), "Link pin types differ"));
    auto variable = g;
    variable.Variables["Last"].Default.erase("Label");
    CHECK(Mentions(variable.Validate(), "Invalid variable: Last"));
    variable = g;
    variable.Variables["Last"].Default["Kind"] = 7; // not an Element value
    CHECK(Mentions(variable.Validate(), "Invalid variable: Last"));
    auto unknown = g;
    unknown.Nodes[1].Inputs["Struct"] = "Missing";
    CHECK(Mentions(unknown.Validate(), "Unknown struct: Missing"));
    auto field = g;
    field.Nodes[10].Inputs["Field"] = "Speed";
    CHECK(Mentions(field.Validate(), "Unknown struct field: Speed"));
    auto definition = g;
    definition.Structs["Hit"].Fields.push_back({ "Struct", Type::Number, 0. });
    CHECK(Mentions(definition.Validate(), "reserved or duplicate field name"));
    definition = g;
    definition.Structs["Inner"] = { { { "Hit", Type::Struct, Value::object(), "Hit" } } }; // a nested Hit needs a Hit default
    CHECK(Mentions(definition.Validate(), "Struct Inner field Hit has an invalid default"));
    definition.Structs["Inner"].Fields[0].Default = g.DefaultFor(Type::Struct, "Hit");
    CHECK(definition.Validate().empty());
    definition.Structs["Inner"].Fields[0].Kind = Type::Exec;
    CHECK(Mentions(definition.Validate(), "unsupported type"));
    definition = g;
    definition.Structs["Hit"].Fields[1].Enum = "Missing";
    CHECK(Mentions(definition.Validate(), "invalid default or unknown enum"));
    definition = g;
    definition.Structs["Empty"] = {};
    CHECK(Mentions(definition.Validate(), "needs 1..32 fields"));
    auto wired = g;
    wired.Nodes.push_back({ 30, "Constant.String", 0, 0, { { "Value", "Kind" } } });
    wired.Links.push_back({ 30, "Value", 11, "Field" });
    CHECK(Mentions(wired.Validate(), "Struct type and field must be literals"));
    auto local = g;
    local.Functions["Scale"].Locals["Work"].Enum = "Other";
    CHECK(!local.Validate().empty());
    // Bounded memory: a struct array that would pass 64 KiB stops the graph at the node that builds it.
    Graph big;
    big.Structs["Note"] = { { { "Text", Type::String, std::string(2000, 'a') } } };
    big.Variables["Notes"] = { Type::ArrayStruct, Value::array(), "Note" };
    big.Nodes = { { 1, "Event.Start" }, { 2, "For", 0, 0, { { "Count", 100. } } }, { 3, "Get.ArrayStruct", 0, 0, { { "Name", "Notes" } } }, { 4, "Struct.Make", 0, 0, { { "Struct", "Note" } } },
                  { 5, "Array.Append.Struct", 0, 0, { { "Struct", "Note" } } }, { 6, "Set.ArrayStruct", 0, 0, { { "Name", "Notes" } } } };
    big.Links = { { 1, "Then", 2, "In" }, { 2, "Body", 6, "In" }, { 3, "Value", 5, "Array" }, { 4, "Value", 5, "Value" }, { 5, "Value", 6, "Value" } };
    CHECK(big.Validate().empty());
    Machine vm;
    CHECK(vm.Load(big));
    vm.Event("Start");
    CHECK(vm.Error().find("Node 6") != std::string::npos && vm.Error().find("Invalid result from Array.Append.Struct") != std::string::npos);
    CHECK(vm.Variables().at("Notes").size() < 40 && vm.RetainedBytes() < Machine::MaxRetainedBytes);
}
TEST(BlueprintStructsRoundTripAndPaste)
{
    const auto g = StructGraph();
    const auto json = g.Serialize();
    CHECK(json.at("Structs").at("Hit").at("Fields").size() == 4 && json.at("Variables").at("Hits").at("Struct") == "Hit");
    auto copy = Graph::Parse(json);
    CHECK(copy.Structs == g.Structs && copy.Variables.at("Last").Enum == "Hit" && copy.Functions.at("Scale").Locals.at("Work").Kind == Type::Struct);
    CHECK(copy.Serialize() == json && copy.Validate().empty() && RunStruct(copy).at("Total") == 12.);
    // Paste brings the struct along; a different struct of the same name is refused without a change.
    std::set<uint32_t> ids;
    std::string error;
    Graph target;
    CHECK(target.PasteNodes(g.CopyNodes({ 1, 2, 9 }), 0, 0, ids, error) && ids.size() == 3 && target.Structs.at("Hit") == g.Structs.at("Hit"));
    Graph conflicting;
    conflicting.Structs["Hit"] = { { { "Damage", Type::Number, 0. } } };
    CHECK(!conflicting.PasteNodes(g.CopyNodes({ 2 }), 0, 0, ids, error) && error.find("Struct Hit differs") != std::string::npos && conflicting.Nodes.empty());
}
TEST(BlueprintStructRenamesKeepWiresAndValues)
{
    auto g = StructGraph();
    const auto links = g.Links.size();
    CHECK(g.StructUses("Hit") == 20); // 15 nodes (all but Start, the Number/String sets, Multiply, Enum.ToText), 2 variables, a parameter, a result and a local
    CHECK(g.RenameStructField("Hit", "Damage", "Power"));
    CHECK(!g.RenameStructField("Hit", "Power", "Kind") && !g.RenameStructField("Hit", "Power", "Struct") && !g.RenameStructField("Hit", "Missing", "X") && !g.RenameStructField("Nope", "Power", "X"));
    CHECK(g.Validate().empty() && g.PruneInvalidPins() == 0 && g.Links.size() == links);
    CHECK(g.Nodes[1].Inputs.contains("Power") && !g.Nodes[1].Inputs.contains("Damage") && g.Nodes[14].Inputs.at("Field") == "Power");
    CHECK(g.Variables.at("Last").Default.contains("Power") && g.Functions.at("Scale").Locals.at("Work").Default.contains("Power"));
    auto vars = RunStruct(g);
    CHECK(vars.at("Total") == 12. && vars.at("Last").at("Power") == 12.);
    // Renaming the struct retypes nodes, variables, the signature and the local; renaming the enum the field.
    CHECK(g.RenameStruct("Hit", "Strike") && !g.RenameStruct("Strike", "") && g.Validate().empty());
    CHECK(g.Variables.at("Hits").Enum == "Strike" && g.Functions.at("Scale").Outputs[0].Enum == "Strike" && g.Nodes[6].Inputs.at("Struct") == "Strike");
    CHECK(g.RenameEnum("Element", "School") && g.Structs.at("Strike").Fields[1].Enum == "School" && g.Validate().empty());
    CHECK(g.EnumUses("School") == 5); // the field, the Make/Break/GetField nodes that show it, and Enum.ToText
    vars = RunStruct(g);
    CHECK(vars.at("Total") == 12. && vars.at("KindName") == "Ice");
}
TEST(BlueprintStructFieldEditsConformValuesAndUndo)
{
    auto g = StructGraph();
    const auto snapshot = g.Serialize(); // the editor's undo state
    // A new field: every stored Hit value takes its default.
    g.Structs["Hit"].Fields.push_back({ "Crit", Type::Bool, true });
    CHECK(!g.Validate().empty());
    CHECK(g.ConformStructValues() == 4); // Last, the parameter, the result and the local (Hits is empty)
    CHECK(g.Validate().empty() && g.Variables.at("Last").Default.at("Crit") == true);
    CHECK(RunStruct(g).at("Last").at("Crit") == true);
    // Removing a wired field drops its wires (Undo restores them) and the field from every value.
    auto removed = g;
    std::erase_if(removed.Structs["Hit"].Fields, [](const Pin& f) { return f.Name == "Damage"; });
    removed.ConformStructValues();
    CHECK(removed.PruneInvalidPins() >= 2); // Break.Damage -> Total, Make's Damage literal
    CHECK(!removed.Variables.at("Last").Default.contains("Damage") && Mentions(removed.Validate(), "Unknown struct field: Damage"));
    // A retyped field: values reset that field to its new default.
    auto retyped = Graph::Parse(snapshot);
    retyped.Structs["Hit"].Fields[3] = { "Label", Type::Number, 1. };
    CHECK(retyped.ConformStructValues() >= 4 && retyped.Variables.at("Last").Default.at("Label") == 1.);
    CHECK(retyped.PruneInvalidPins() == 1 && retyped.Validate().empty()); // Make's "bolt" literal no longer fits
    // Undo is the serialized snapshot: it restores the definition, the values and the wires exactly.
    CHECK(Graph::Parse(snapshot).Serialize() == StructGraph().Serialize());
}
TEST(BlueprintStructArraysRunTheArrayNodes)
{
    Graph g;
    g.Structs["Item"] = { { { "Name", Type::String, "" }, { "Count", Type::Number, 1. } } };
    const Value sword = { { "Name", "Sword" }, { "Count", 1. } }, arrows = { { "Name", "Arrow" }, { "Count", 20. } };
    g.Variables["Bag"] = { Type::ArrayStruct, Value::array({ sword, arrows }), "Item" };
    g.Variables["Sum"] = { Type::Number, 0. };
    g.Variables["Where"] = { Type::Number, -1. };
    g.Variables["Has"] = { Type::Bool, false };
    g.Variables["Second"] = { Type::Struct, sword, "Item" };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Get.ArrayStruct", 0, 0, { { "Name", "Bag" } } }, { 3, "ForEach.Struct", 0, 0, { { "Struct", "Item" } } }, { 4, "Struct.GetField", 0, 0, { { "Struct", "Item" }, { "Field", "Count" } } },
                { 5, "Get.Number", 0, 0, { { "Name", "Sum" } } }, { 6, "Add" }, { 7, "Set.Number", 0, 0, { { "Name", "Sum" } } }, { 8, "Array.Find.Struct", 0, 0, { { "Struct", "Item" }, { "Value", arrows } } },
                { 9, "Set.Number", 0, 0, { { "Name", "Where" } } }, { 10, "Array.Contains.Struct", 0, 0, { { "Struct", "Item" }, { "Value", sword } } }, { 11, "Set.Bool", 0, 0, { { "Name", "Has" } } },
                { 12, "Array.Get.Struct", 0, 0, { { "Struct", "Item" }, { "Index", 1. } } }, { 13, "Set.Struct", 0, 0, { { "Name", "Second" } } } };
    g.Links = { { 1, "Then", 3, "In" }, { 2, "Value", 3, "Array" }, { 3, "Body", 7, "In" }, { 3, "Value", 4, "Value" }, { 4, "Value", 6, "A" }, { 5, "Value", 6, "B" }, { 6, "Value", 7, "Value" },
                { 3, "Completed", 9, "In" }, { 2, "Value", 8, "Array" }, { 8, "Value", 9, "Value" }, { 9, "Then", 11, "In" }, { 2, "Value", 10, "Array" }, { 10, "Value", 11, "Value" },
                { 11, "Then", 13, "In" }, { 2, "Value", 12, "Array" }, { 12, "Value", 13, "Value" } };
    for (const auto& d : g.Validate()) std::printf("    %u: %s\n", d.Node, d.Message.c_str());
    CHECK(g.Validate().empty());
    const auto vars = RunStruct(g);
    CHECK(vars.at("Sum") == 21. && vars.at("Where") == 1. && vars.at("Has") == true && vars.at("Second") == arrows);
    // An array of another shape does not fit the struct array type.
    CHECK(!g.Fits(Type::ArrayStruct, "Item", Value::array({ { { "Name", "x" } } })) && g.Fits(Type::ArrayStruct, "Item", Value::array()));
}
TEST(BlueprintCollapseCarriesStructWires)
{
    // Collapsing the Break + Set Total step turns the struct wire into a Hit parameter.
    auto g = StructGraph();
    const auto expected = RunStruct(g);
    uint32_t call = 0;
    std::string error;
    CHECK(g.CollapseToFunction({ 9, 10 }, "Store", call, error));
    if (!error.empty()) std::printf("    %s\n", error.c_str());
    const auto& signature = g.Functions.at("Store");
    CHECK(signature.Inputs.size() == 1 && signature.Inputs[0].Kind == Type::Struct && signature.Inputs[0].Enum == "Hit" && g.Fits(signature.Inputs[0], signature.Inputs[0].Default));
    CHECK(g.Validate().empty() && RunStruct(g) == expected);
}
namespace {
    // Struct Point {X, Y}; struct Body {Name, At: Point, Path: ArrayStruct Point}. Start makes a Body with At = {3, 4},
    // moves it with SetField(At) of a SetField(Y) copy, appends a Point to Path and reads At.Y and Path's length back.
    Graph NestedGraph()
    {
        Graph g;
        g.Structs["Point"] = { { { "X", Type::Number, 0. }, { "Y", Type::Number, 0. } } };
        g.Structs["Body"] = { { { "Name", Type::String, "body" }, { "At", Type::Struct, Value{ { "X", 1. }, { "Y", 2. } }, "Point" }, { "Path", Type::ArrayStruct, Value::array(), "Point" } } };
        g.Variables["Thing"] = { Type::Struct, g.DefaultFor(Type::Struct, "Body"), "Body" };
        g.Variables["Y"] = { Type::Number, 0. };
        g.Variables["Steps"] = { Type::Number, 0. };
        g.Nodes = { { 1, "Event.Start" },
                    { 2, "Struct.Make", 0, 0, { { "Struct", "Point" }, { "X", 3. }, { "Y", 4. } } },
                    { 3, "Struct.Make", 0, 0, { { "Struct", "Body" }, { "Name", "probe" } } },
                    { 4, "Struct.GetField", 0, 0, { { "Struct", "Body" }, { "Field", "At" } } },
                    { 5, "Struct.SetField", 0, 0, { { "Struct", "Point" }, { "Field", "Y" }, { "FieldValue", 9. } } },
                    { 6, "Struct.SetField", 0, 0, { { "Struct", "Body" }, { "Field", "At" } } },
                    { 7, "Array.Append.Struct", 0, 0, { { "Struct", "Point" } } },
                    { 8, "Struct.GetField", 0, 0, { { "Struct", "Body" }, { "Field", "Path" } } },
                    { 9, "Struct.SetField", 0, 0, { { "Struct", "Body" }, { "Field", "Path" } } },
                    { 10, "Set.Struct", 0, 0, { { "Name", "Thing" } } },
                    { 11, "Get.Struct", 0, 0, { { "Name", "Thing" } } },
                    { 12, "Struct.Break", 0, 0, { { "Struct", "Body" } } },
                    { 13, "Struct.Break", 0, 0, { { "Struct", "Point" } } },
                    { 14, "Set.Number", 0, 0, { { "Name", "Y" } } },
                    { 15, "Array.Length.Struct", 0, 0, { { "Struct", "Point" } } },
                    { 16, "Set.Number", 0, 0, { { "Name", "Steps" } } } };
        g.Links = { { 2, "Value", 3, "At" }, { 3, "Value", 4, "Value" }, { 4, "Value", 5, "Value" }, { 3, "Value", 6, "Value" }, { 5, "Value", 6, "FieldValue" },
                    { 6, "Value", 8, "Value" }, { 8, "Value", 7, "Array" }, { 2, "Value", 7, "Value" }, { 6, "Value", 9, "Value" }, { 7, "Value", 9, "FieldValue" },
                    { 1, "Then", 10, "In" }, { 9, "Value", 10, "Value" }, { 11, "Value", 12, "Value" }, { 12, "At", 13, "Value" },
                    { 10, "Then", 14, "In" }, { 13, "Y", 14, "Value" }, { 14, "Then", 16, "In" }, { 12, "Path", 15, "Array" }, { 15, "Value", 16, "Value" } };
        return g;
    }
}
TEST(BlueprintNestedStructsMakeBreakAndFieldsRun)
{
    auto g = NestedGraph();
    for (const auto& d : g.Validate()) std::printf("    %u: %s\n", d.Node, d.Message.c_str());
    CHECK(g.Validate().empty());
    // The inner struct's pins keep the inner struct's name.
    const auto make = g.Describe(*g.Find(3)), split = g.Describe(*g.Find(12)), get = g.Describe(*g.Find(4));
    CHECK(make.Inputs[2].Name == "At" && make.Inputs[2].Kind == Type::Struct && make.Inputs[2].Enum == "Point" && make.Inputs[3].Kind == Type::ArrayStruct && make.Inputs[3].Enum == "Point");
    CHECK(split.Outputs[1].Kind == Type::Struct && split.Outputs[1].Enum == "Point" && get.Outputs[0].Enum == "Point");
    const auto vars = RunStruct(g);
    const Value expected = { { "Name", "probe" }, { "At", { { "X", 3. }, { "Y", 9. } } }, { "Path", Value::array({ { { "X", 3. }, { "Y", 4. } } }) } };
    CHECK(vars.at("Thing") == expected && vars.at("Y") == 9. && vars.at("Steps") == 1.);
    // A Point wire does not fit a Body pin; a nested value of the wrong shape does not fit the outer struct.
    auto wrong = g;
    wrong.Links[0] = { 3, "Value", 3, "At" };
    CHECK(!wrong.Validate().empty());
    CHECK(!g.Fits(Type::Struct, "Body", { { "Name", "x" }, { "At", { { "X", 1. } } }, { "Path", Value::array() } }));
    CHECK(g.Fits(Type::Struct, "Body", expected));
    // Format 4 round trip.
    const auto copy = Graph::Parse(g.Serialize());
    CHECK(copy.Structs == g.Structs && copy.Serialize() == g.Serialize() && RunStruct(copy).at("Thing") == expected);
}
TEST(BlueprintNestedStructCyclesAndDepthAreRejected)
{
    auto g = NestedGraph();
    // Point -> Body -> Point: a struct may not contain itself, directly or through another struct.
    g.Structs["Point"].Fields.push_back({ "Owner", Type::Struct, Value::object(), "Body" });
    CHECK(g.StructCycle() == "Body -> Point -> Body" || g.StructCycle() == "Point -> Body -> Point");
    CHECK(Mentions(g.Validate(), "Struct contains itself"));
    auto self = NestedGraph();
    self.Structs["Point"].Fields.push_back({ "Trail", Type::ArrayStruct, Value::array(), "Point" });
    CHECK(self.StructCycle() == "Point -> Point" && Mentions(self.Validate(), "Struct contains itself: Point -> Point"));
    // Eight nested levels exceed the value depth the VM checks.
    Graph deep;
    deep.Structs["L0"] = { { { "V", Type::Number, 0. } } };
    for (int i = 1; i <= 7; ++i)
    {
        const auto inner = "L" + std::to_string(i - 1);
        deep.Structs["L" + std::to_string(i)] = { { { "In", Type::Struct, deep.DefaultFor(Type::Struct, inner), inner } } };
    }
    CHECK(Mentions(deep.Validate(), "Struct L7 nests deeper than 7 levels") && !Mentions(deep.Validate(), "Struct L6 nests"));
    // The 64 KiB value limit holds for nested values: a 40 KiB inner default fits once, not twice.
    Graph big;
    big.Structs["Note"] = { { { "Text", Type::String, std::string(40000, 'a') } } };
    big.Structs["Pair"] = { { { "A", Type::Struct, big.DefaultFor(Type::Struct, "Note"), "Note" }, { "B", Type::Struct, big.DefaultFor(Type::Struct, "Note"), "Note" } } };
    CHECK(Mentions(big.Validate(), "Struct Pair defaults exceed 64 KiB"));
    CHECK(!big.Fits(Type::Struct, "Pair", big.DefaultFor(Type::Struct, "Pair")));
}
TEST(BlueprintNestedStructRenamesAndFieldEditsPropagate)
{
    auto g = NestedGraph();
    const auto links = g.Links.size();
    CHECK(g.StructUses("Point") == 11 + 2); // 11 nodes show a Point pin (all but Start, the Body Get/Set and the Number sets), and the At/Path fields of Body
    // Renaming an inner field renames it inside every stored outer value (the variable, Body's At default, literals).
    CHECK(g.RenameStructField("Point", "Y", "Height"));
    CHECK(g.Variables.at("Thing").Default.at("At").contains("Height") && g.Structs.at("Body").Fields[1].Default.at("Height") == 2.);
    CHECK(g.Nodes[4].Inputs.at("Field") == "Height" && g.Validate().empty() && g.Links.size() == links);
    // Renaming the inner struct retypes the outer struct's fields.
    CHECK(g.RenameStruct("Point", "Vec") && g.Structs.at("Body").Fields[1].Enum == "Vec" && g.Structs.at("Body").Fields[2].Enum == "Vec" && g.Validate().empty());
    CHECK(RunStruct(g).at("Y") == 9.);
    // A new inner field reaches the nested values; a struct in use by a field counts as used.
    g.Structs["Vec"].Fields.push_back({ "Z", Type::Number, 5. });
    CHECK(g.ConformStructValues() >= 2 && g.Variables.at("Thing").Default.at("At").at("Z") == 5. && g.Structs.at("Body").Fields[1].Default.at("Z") == 5.);
    g.PruneInvalidPins();
    for (const auto& d : g.Validate()) std::printf("    %u: %s\n", d.Node, d.Message.c_str());
    CHECK(g.Validate().empty() && RunStruct(g).at("Thing").at("Path")[0].at("Z") == 5.); // Make gives Z its field default
    Graph fieldOnly;
    fieldOnly.Structs = g.Structs;
    CHECK(fieldOnly.StructUses("Vec") == 2);
}
namespace {
    // Enum Rank {Bronze=0, Silver=1, Gold=5}. Start appends Gold to the Ranks variable (an ArrayEnum), passes the array
    // to Count(Ranks) -> Number, which counts the Gold entries in a ForEach.Enum + Enum.Switch, keeping a local
    // ArrayEnum of what it saw.
    Graph EnumArrayGraph()
    {
        Graph g;
        g.Enums["Rank"] = { { { "Bronze", 0 }, { "Silver", 1 }, { "Gold", 5 } }, "" };
        g.Variables["Ranks"] = { Type::ArrayEnum, Value::array({ 0, 5 }), "Rank" };
        g.Variables["Golds"] = { Type::Number, 0. };
        g.Variables["Where"] = { Type::Number, -2. };
        g.Functions["Count"] = { { { "List", Type::ArrayEnum, Value::array(), "Rank" } }, { { "Golds", Type::Number, 0. } }, { { "Seen", { Type::ArrayEnum, Value::array(), "Rank" } }, { "N", { Type::Number, 0. } } } };
        g.Nodes = { { 1, "Event.Start" },
                    { 2, "Get.ArrayEnum", 0, 0, { { "Name", "Ranks" } } },
                    { 3, "Array.Append.Enum", 0, 0, { { "Enum", "Rank" }, { "Value", 5 } } },
                    { 4, "Set.ArrayEnum", 0, 0, { { "Name", "Ranks" }, { "Enum", "Rank" } } },
                    { 5, "Function.Call", 0, 0, { { "Name", "Count" } } },
                    { 6, "Set.Number", 0, 0, { { "Name", "Golds" } } },
                    { 7, "Constant.ArrayEnum", 0, 0, { { "Enum", "Rank" }, { "Value", Value::array({ 1, 1, 5 }) } } },
                    { 8, "Array.Find.Enum", 0, 0, { { "Enum", "Rank" }, { "Value", 5 } } },
                    { 9, "Set.Number", 0, 0, { { "Name", "Where" } } },
                    { 20, "Function.Entry", 0, 0, { { "Name", "Count" } } },
                    { 21, "ForEach.Enum", 0, 0, { { "Enum", "Rank" } } },
                    { 22, "Enum.Switch", 0, 0, { { "Enum", "Rank" } } },
                    { 23, "Local.Get.Number", 0, 0, { { "Name", "N" } } },
                    { 24, "Add", 0, 0, { { "B", 1. } } },
                    { 25, "Local.Set.Number", 0, 0, { { "Name", "N" } } },
                    { 26, "Local.Get.ArrayEnum", 0, 0, { { "Name", "Seen" } } },
                    { 27, "Array.Append.Enum", 0, 0, { { "Enum", "Rank" } } },
                    { 28, "Local.Set.ArrayEnum", 0, 0, { { "Name", "Seen" } } },
                    { 29, "Function.Return", 0, 0, { { "Name", "Count" } } } };
        g.Links = { { 1, "Then", 4, "In" }, { 2, "Value", 3, "Array" }, { 3, "Value", 4, "Value" }, { 4, "Then", 5, "In" }, { 2, "Value", 5, "List" },
                    { 5, "Then", 6, "In" }, { 5, "Golds", 6, "Value" }, { 6, "Then", 9, "In" }, { 7, "Value", 8, "Array" }, { 8, "Value", 9, "Value" },
                    { 20, "Then", 21, "In" }, { 20, "List", 21, "Array" }, { 21, "Body", 28, "In" }, { 26, "Value", 27, "Array" }, { 21, "Value", 27, "Value" }, { 27, "Value", 28, "Value" },
                    { 28, "Then", 22, "In" }, { 21, "Value", 22, "Value" }, { 22, "Gold", 25, "In" }, { 23, "Value", 24, "A" }, { 24, "Value", 25, "Value" },
                    { 21, "Completed", 29, "In" }, { 23, "Value", 29, "Golds" } };
        return g;
    }
}
TEST(BlueprintEnumArraysRunTheArrayNodesForEachAndLocals)
{
    auto g = EnumArrayGraph();
    for (const auto& d : g.Validate()) std::printf("    %u: %s\n", d.Node, d.Message.c_str());
    CHECK(g.Validate().empty());
    const auto each = g.Describe(*g.Find(21)), get = g.Describe(*g.Find(2));
    CHECK(each.Outputs.back().Kind == Type::Enum && each.Outputs.back().Enum == "Rank" && each.Outputs.back().Default == 0);
    CHECK(get.Outputs[0].Kind == Type::ArrayEnum && get.Outputs[0].Enum == "Rank" && IsNamed(Type::ArrayEnum) && ElementType(Type::ArrayEnum) == Type::Enum && ElementType(Type::ArrayStruct) == Type::Struct);
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty());
    // The variable read inside the call is the stored array [Bronze, Gold, Gold]: the Switch counts two Golds.
    CHECK(vm.Variables().at("Ranks") == Value::array({ 0, 5, 5 }) && vm.Variables().at("Golds") == 0. + 2. && vm.Variables().at("Where") == 2.);
    // An integer that is no entry of the enum does not fit, statically and at run time.
    CHECK(!g.Fits(Type::ArrayEnum, "Rank", Value::array({ 0, 2 })) && g.Fits(Type::ArrayEnum, "Rank", Value::array({ 1 })));
    auto bad = g;
    bad.Variables["Ranks"].Default = Value::array({ 3 });
    CHECK(Mentions(bad.Validate(), "Invalid variable: Ranks"));
    bad = g;
    bad.Nodes[6].Inputs["Value"] = Value::array({ 1, 4 });
    CHECK(Mentions(bad.Validate(), "Invalid input: Value"));
    auto runtime = g;
    runtime.Nodes[2].Inputs.erase("Value");
    runtime.Nodes.push_back({ 30, "Constant.Number", 0, 0, { { "Value", 0. } } });
    runtime.Nodes.push_back({ 31, "Array.Get.Enum", 0, 0, { { "Enum", "Rank" }, { "Index", 0. } } }); // reads an Enum, appends it
    runtime.Links.push_back({ 7, "Value", 31, "Array" });
    runtime.Links.push_back({ 31, "Value", 3, "Value" });
    CHECK(runtime.Validate().empty());
    // Another enum's array does not connect.
    auto other = g;
    other.Enums["Mood"] = { { { "Calm", 0 } }, "" };
    other.Nodes[3].Inputs["Enum"] = "Mood";
    CHECK(Mentions(other.Validate(), "Link pin types differ"));
}
TEST(BlueprintEnumArraysRenameRoundTripAndVersion)
{
    auto g = EnumArrayGraph();
    CHECK(g.EnumUses("Rank") >= 14);
    CHECK(g.RenameEnum("Rank", "Medal") && g.Variables.at("Ranks").Enum == "Medal" && g.Functions.at("Count").Inputs[0].Enum == "Medal" && g.Functions.at("Count").Locals.at("Seen").Enum == "Medal");
    CHECK(g.Validate().empty());
    const auto json = g.Serialize();
    CHECK(json.at("Variables").at("Ranks").at("Type") == "ArrayEnum" && json.at("Variables").at("Ranks").at("Enum") == "Medal");
    const auto copy = Graph::Parse(json);
    CHECK(copy.Serialize() == json && copy.Validate().empty());
    // An enum array in a version 3 file is refused rather than guessed.
    auto v3 = json;
    v3["Version"] = 3;
    v3.erase("Macros");
    bool threw = false;
    try { Graph::Parse(v3); } catch (const std::exception& e) { threw = std::string(e.what()).find("version 4") != std::string::npos; }
    CHECK(threw);
    // Struct fields may be enum arrays too.
    Graph s;
    s.Enums = g.Enums;
    s.Structs["Card"] = { { { "Medals", Type::ArrayEnum, Value::array({ 5 }), "Medal" } } };
    CHECK(s.Validate().empty() && s.EnumUses("Medal") == 1);
    s.Structs["Card"].Fields[0].Default = Value::array({ 9 });
    CHECK(Mentions(s.Validate(), "Struct Card field Medals has an invalid default"));
}
TEST(BlueprintFormatVersion3StructFilesLoadUnchanged)
{
    // A version 3 file (structs, no Macros section) loads unchanged and runs the same; newer data in it is refused.
    auto v3 = StructGraph().Serialize();
    v3["Version"] = 3;
    v3.erase("Macros");
    auto fromV3 = Graph::Parse(v3);
    CHECK(fromV3.SourceVersion == 3 && fromV3.Validate().empty() && fromV3.Serialize() == StructGraph().Serialize() && RunStruct(fromV3).at("Total") == 12.);
    auto expectRefused = [](Value json) {
        try { Graph::Parse(json); } catch (const std::exception& e) { return std::string(e.what()).find("version 4") != std::string::npos; }
        return false;
    };
    auto nested = NestedGraph().Serialize();
    nested["Version"] = 3;
    nested.erase("Macros");
    CHECK(expectRefused(nested));
    auto macros = v3;
    macros["Macros"] = Value::object();
    CHECK(expectRefused(macros));
    auto body = v3;
    body["Nodes"][0]["Macro"] = "M";
    CHECK(expectRefused(body));
}
namespace {
    // Macro Award(In: Exec, Amount: Number) -> (Then: Exec, Doubled: Number): the body multiplies Amount by 2,
    // waits 0.1 s (latent flow, which a typed function call cannot hide) and adds Doubled to the Total variable.
    // Start runs two instances in a row (Amount 3 from a literal, 5 from a wire) and stores the second Doubled.
    Graph MacroGraph()
    {
        Graph g;
        g.Variables["Total"] = { Type::Number, 0. };
        g.Variables["Last"] = { Type::Number, 0. };
        g.Macros["Award"] = { { { "In", Type::Exec, nullptr }, { "Amount", Type::Number, 1. } }, { { "Then", Type::Exec, nullptr }, { "Doubled", Type::Number, 0. } } };
        g.Nodes = { { 1, "Event.Start" },
                    { 2, "Macro.Instance", 0, 0, { { "Name", "Award" }, { "Amount", 3. } } },
                    { 3, "Constant.Number", 0, 0, { { "Value", 5. } } },
                    { 4, "Macro.Instance", 0, 0, { { "Name", "Award" } } },
                    { 5, "Set.Number", 0, 0, { { "Name", "Last" } } },
                    { 10, "Macro.Entry" }, { 11, "Multiply", 0, 0, { { "B", 2. } } }, { 12, "Delay", 0, 0, { { "Seconds", .1 } } },
                    { 13, "Get.Number", 0, 0, { { "Name", "Total" } } }, { 14, "Add" }, { 15, "Set.Number", 0, 0, { { "Name", "Total" } } }, { 16, "Macro.Exit" } };
        for (auto& n : g.Nodes)
            if (n.ID >= 10) n.Macro = "Award";
        g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 4, "In" }, { 3, "Value", 4, "Amount" }, { 4, "Then", 5, "In" }, { 4, "Doubled", 5, "Value" },
                    { 10, "In", 12, "In" }, { 10, "Amount", 11, "A" }, { 12, "Then", 15, "In" }, { 13, "Value", 14, "A" }, { 11, "Value", 14, "B" }, { 14, "Value", 15, "Value" },
                    { 15, "Then", 16, "Then" }, { 11, "Value", 16, "Doubled" } };
        return g;
    }
}
TEST(BlueprintMacrosExpandInlineWithLatentFlow)
{
    auto g = MacroGraph();
    for (const auto& d : g.Validate()) std::printf("    %u: %s\n", d.Node, d.Message.c_str());
    CHECK(g.Validate().empty() && g.Warnings().empty());
    // The instance shows the macro's pins; the entry/exit inside the body give/take them.
    const auto instance = g.Describe(*g.Find(2)), entry = g.Describe(*g.Find(10)), exit = g.Describe(*g.Find(16));
    CHECK(instance.Inputs.size() == 3 && instance.Inputs[1].Name == "In" && instance.Inputs[1].Kind == Type::Exec && instance.Outputs[1].Name == "Doubled" && !instance.Pure);
    CHECK(entry.Outputs.size() == 2 && exit.Inputs.size() == 2 && g.MacroUses("Award") == 2);
    // Each instance becomes its own copy of the body with new IDs, above every ID of the graph.
    const auto expansion = g.Expand();
    CHECK(expansion.Errors.empty() && expansion.Nodes.size() == 3 + 2 * 5);
    std::set<uint32_t> ids;
    for (const auto& n : expansion.Nodes)
    {
        CHECK(n.Macro.empty() && n.Kind != "Macro.Instance" && n.Kind != "Macro.Entry" && ids.insert(n.ID).second);
        if (n.ID > 16) CHECK(expansion.Origins.contains(n.ID));
    }
    CHECK(expansion.Origins.size() == 10 && expansion.Origins.at(17).first + expansion.Origins.at(22).first == 2 + 4);
    // The latent body runs: the first Delay holds the chain, then the second instance, then Last.
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty() && vm.Pending() == 1 && vm.Variables().at("Total") == 0.);
    vm.Tick(.1);
    CHECK(vm.Variables().at("Total") == 6. && vm.Pending() == 1 && vm.Variables().at("Last") == 0.);
    vm.Tick(.1);
    CHECK(vm.Error().empty() && vm.Pending() == 0 && vm.Variables().at("Total") == 16. && vm.Variables().at("Last") == 10.);
    // Format 4 round trip: the definition and the body nodes' Macro key.
    const auto json = g.Serialize();
    CHECK(json.at("Macros").at("Award").at("Inputs")[0].at("Type") == "Exec" && json.at("Nodes")[5].at("Macro") == "Award" && !json.at("Nodes")[0].contains("Macro"));
    const auto copy = Graph::Parse(json);
    CHECK(copy.Macros == g.Macros && copy.Serialize() == json && copy.Validate().empty());
}
TEST(BlueprintMacrosPassThroughNestAndPureData)
{
    // Twice(X) -> Value: a pure data macro using another macro (Inc) twice; a pass-through exec and an unwired output.
    Graph g;
    g.Variables["Out"] = { Type::Number, 0. };
    g.Variables["Flag"] = { Type::Bool, false };
    g.Macros["Inc"] = { { { "X", Type::Number, 0. } }, { { "Value", Type::Number, 0. } } };
    g.Macros["Twice"] = { { { "In", Type::Exec, nullptr }, { "X", Type::Number, 0. } }, { { "Then", Type::Exec, nullptr }, { "Value", Type::Number, 0. }, { "Done", Type::Bool, false } } };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Macro.Instance", 0, 0, { { "Name", "Twice" }, { "X", 10. } } }, { 3, "Set.Number", 0, 0, { { "Name", "Out" } } }, { 4, "Set.Bool", 0, 0, { { "Name", "Flag" } } },
                { 10, "Macro.Entry", 0, 0, {}, false, "Inc" }, { 11, "Add", 0, 0, { { "B", 1. } }, false, "Inc" }, { 12, "Macro.Exit", 0, 0, {}, false, "Inc" },
                { 20, "Macro.Entry", 0, 0, {}, false, "Twice" }, { 21, "Macro.Instance", 0, 0, { { "Name", "Inc" } }, false, "Twice" }, { 22, "Macro.Instance", 0, 0, { { "Name", "Inc" } }, false, "Twice" },
                { 23, "Macro.Exit", 0, 0, { { "Done", true } }, false, "Twice" } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 2, "Value", 3, "Value" }, { 3, "Then", 4, "In" }, { 2, "Done", 4, "Value" },
                { 10, "X", 11, "A" }, { 11, "Value", 12, "Value" },
                { 20, "In", 23, "Then" }, { 20, "X", 21, "X" }, { 21, "Value", 22, "X" }, { 22, "Value", 23, "Value" } };
    for (const auto& d : g.Validate()) std::printf("    %u: %s\n", d.Node, d.Message.c_str());
    CHECK(g.Validate().empty() && g.Describe(*g.Find(21)).Pure && !g.Describe(*g.Find(2)).Pure);
    const auto expansion = g.Expand();
    CHECK(expansion.Errors.empty() && expansion.Nodes.size() == 3 + 2); // Start, the two sets and one Add per Inc
    for (const auto& n : expansion.Nodes)
        if (n.Kind == "Add") CHECK(expansion.Origins.at(n.ID).first == 2 && expansion.Origins.at(n.ID).second == 11);
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().empty() && vm.Variables().at("Out") == 12. && vm.Variables().at("Flag") == true);
    // A macro that uses itself, directly or through another, is refused.
    auto loop = g;
    loop.Nodes.push_back({ 13, "Macro.Instance", 0, 0, { { "Name", "Twice" } }, false, "Inc" });
    CHECK(Mentions(loop.Validate(), "uses itself"));
}
TEST(BlueprintMacroDiagnosticsAndErrorsPointAtTheInstance)
{
    // A wrong-typed wire inside the body is found in the body itself (the body node); used, a wiring error at the
    // instance (the instance node).
    auto g = MacroGraph();
    g.Nodes.push_back({ 17, "Constant.String", 0, 0, { { "Value", "x" } }, false, "Award" });
    g.Links.push_back({ 17, "Value", 14, "A" });
    g.Links.erase(std::find_if(g.Links.begin(), g.Links.end(), [](const Link& l) { return l.From == 13; }));
    const auto body = g.Validate();
    CHECK(std::any_of(body.begin(), body.end(), [](const Diagnostic& d) { return d.Node == 14 && d.Message == "In macro Award: Link pin types differ or pin missing"; }));
    g = MacroGraph();
    g.Nodes.push_back({ 6, "Constant.String", 0, 0, { { "Value", "x" } } });
    g.Links.push_back({ 6, "Value", 2, "Amount" });
    const auto wiring = g.Validate();
    CHECK(std::any_of(wiring.begin(), wiring.end(), [](const Diagnostic& d) { return d.Node == 2 && d.Message == "Link pin types differ or pin missing"; }));
    g = MacroGraph();
    g.Links.push_back({ 3, "Value", 2, "Missing" });
    CHECK(Mentions(g.Validate(), "Pin Missing is not in macro Award"));
    // A finding that only exists once the body is in place (a Local outside a typed function) names the instance and
    // the body node it comes from.
    g = MacroGraph();
    g.Nodes.push_back({ 18, "Local.Get.Number", 0, 0, { { "Name", "T" } }, false, "Award" });
    g.Links.erase(std::find_if(g.Links.begin(), g.Links.end(), [](const Link& l) { return l.From == 13; }));
    g.Links.push_back({ 18, "Value", 14, "A" });
    const auto scoped = g.Validate();
    CHECK(std::any_of(scoped.begin(), scoped.end(), [](const Diagnostic& d) { return (d.Node == 2 || d.Node == 4) && d.Source == 18 && d.Message.starts_with("In macro Award: Local/return node"); }));
    // Structural errors: events inside a macro, a missing exit, a cross-boundary wire, an unknown macro.
    g = MacroGraph();
    g.Nodes.push_back({ 19, "Event.Update", 0, 0, {}, false, "Award" });
    CHECK(Mentions(g.Validate(), "A macro cannot hold events or function entries"));
    g = MacroGraph();
    std::erase_if(g.Nodes, [](const Node& n) { return n.ID == 16; });
    std::erase_if(g.Links, [](const Link& l) { return l.To == 16; });
    CHECK(Mentions(g.Validate(), "Macro Award needs exactly one entry and one exit"));
    g = MacroGraph();
    g.Links.push_back({ 13, "Value", 5, "Value" });
    CHECK(Mentions(g.Validate(), "Link crosses a macro boundary"));
    g = MacroGraph();
    g.Nodes[1].Inputs["Name"] = "Nope";
    CHECK(Mentions(g.Validate(), "Unknown macro: Nope"));
    // A runtime error in a copied node reports the instance and the body node.
    g = MacroGraph();
    g.Nodes[7].Inputs["Seconds"] = -1.; // the Delay
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    CHECK(vm.Error().starts_with("Node 2: In macro Award (node 12): Delay must be"));
}
TEST(BlueprintMacroRenamesPasteAndCollapse)
{
    auto g = MacroGraph();
    CHECK(g.RenameMacroPin("Award", false, "Amount", "Points") && g.RenameMacroPin("Award", true, "Doubled", "Twice"));
    CHECK(!g.RenameMacroPin("Award", false, "Points", "In") && !g.RenameMacroPin("Award", false, "Points", "Name") && !g.RenameMacroPin("Nope", false, "Points", "X"));
    CHECK(g.Nodes[1].Inputs.contains("Points") && g.Validate().empty() && g.PruneInvalidPins() == 0);
    CHECK(g.RenameMacro("Award", "Bonus") && !g.RenameMacro("Bonus", "") && g.Nodes[1].Inputs.at("Name") == "Bonus" && g.Nodes[6].Macro == "Bonus" && g.Validate().empty());
    Machine vm;
    CHECK(vm.Load(g));
    vm.Event("Start");
    vm.Tick(.1);
    vm.Tick(.1);
    CHECK(vm.Error().empty() && vm.Variables().at("Last") == 10.);
    // A pasted instance needs the same macro in the target graph; pasting into a body puts the nodes there.
    std::set<uint32_t> ids;
    std::string error;
    auto pasted = g;
    CHECK(pasted.PasteNodes(g.CopyNodes({ 2, 3 }), 10, 10, ids, error) && ids.size() == 2 && pasted.MacroUses("Bonus") == 3);
    Graph other;
    CHECK(!other.PasteNodes(g.CopyNodes({ 2 }), 0, 0, ids, error) && error.find("Macro Bonus is not in this graph") != std::string::npos && other.Nodes.empty());
    auto inside = g;
    CHECK(inside.PasteNodes(g.CopyNodes({ 3 }), 0, 0, ids, error, "Bonus") && inside.Find(*ids.begin())->Macro == "Bonus");
    // Body nodes cannot be collapsed into a function; a macro's entry is not pasted twice.
    uint32_t call = 0;
    CHECK(!g.CollapseToFunction({ 11 }, "F", call, error) && error.find("inside a macro") != std::string::npos);
    CHECK(!inside.PasteNodes(g.CopyNodes({ 10 }), 0, 0, ids, error, "Bonus"));
}
