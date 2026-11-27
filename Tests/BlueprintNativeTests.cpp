#include "TestAssertions.h"
#include "Zamin/Project/ExportCook.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scripting/ScriptEngine.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include "Zamin/Scripting/Visual/VisualGraphBytecode.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Utils/FileSystem.h"
#include <filesystem>
#include <memory>
using namespace Zamin;
using namespace Zamin::Visual;

// ---------- Native method reflection (ZM_METHOD / ZM_FUNCTION) and the graph cook
namespace NativeProbeTypes {
    enum class ProbeMedal { Bronze, Silver = 5, Gold };
    ZM_SCRIPT_ENUM(ProbeMedal, "Bronze", { "Silver", ProbeMedal::Silver }, "Gold")
    struct ProbeScore
    {
        float Total = 0;
        int Count = 0;
        ProbeMedal Medal = ProbeMedal::Bronze;
    };
    ZM_SCRIPT_STRUCT(ProbeScore, ZM_STRUCT_FIELD(ProbeScore, Total), ZM_STRUCT_FIELD(ProbeScore, Count), ZM_STRUCT_FIELD(ProbeScore, Medal))
}
using NativeProbeTypes::ProbeMedal;
using NativeProbeTypes::ProbeScore;
namespace {
    class NativeProbe : public ScriptableEntity
    {
    public:
        int Total = 0;
        float Scale(float value, int times) const { return value * float(times); }
        int Add(int amount)
        {
            Total += amount;
            return Total;
        }
        static std::string Greet(const std::string& name) { return "Hello " + name; }
        ProbeMedal MedalFor(float score) const { return score >= 10 ? ProbeMedal::Gold : score >= 5 ? ProbeMedal::Silver : ProbeMedal::Bronze; }
        ProbeScore Summarize(const std::vector<float>& values) const
        {
            ProbeScore s;
            for (float v : values) s.Total += v;
            s.Count = int(values.size());
            s.Medal = MedalFor(s.Total);
            return s;
        }
        std::vector<glm::vec3> Line(glm::vec3 from, glm::vec3 to, int count) const
        {
            std::vector<glm::vec3> points;
            for (int i = 0; i < count; ++i) points.push_back(from + (to - from) * (count > 1 ? float(i) / float(count - 1) : 0.0f));
            return points;
        }
        EntityRef Echo(EntityRef e) const { return e; }
        void Fail(const std::string& why) { throw std::runtime_error("probe refused: " + why); }
        // The "hot reloaded" versions: Scale takes a third parameter, Add is gone.
        float Scale3(float value, int times, float bias) const { return value * float(times) + bias; }
    };
    double Hypot(double x, double y) { return std::sqrt(x * x + y * y); }
    // The probe class as a module would register it (ZM_REGISTER_SCRIPT), as a scoped registrar: destroying it is
    // the module unloading.
    std::unique_ptr<ScriptRegistrar> RegisterProbe()
    {
        return std::make_unique<ScriptRegistrar>("NativeProbe", []() -> ScriptableEntity* { return new NativeProbe; },
            std::vector<ScriptMember>{ ZM_FIELD(NativeProbe, Total), ZM_PURE_METHOD(NativeProbe, Scale, "Value", "Times"), ZM_METHOD(NativeProbe, Add, "Amount").Returns("Total").Tooltip("Adds to Total"),
                                       ZM_PURE_METHOD(NativeProbe, Greet, "Name"), ZM_PURE_METHOD(NativeProbe, MedalFor, "Score"), ZM_PURE_METHOD(NativeProbe, Summarize, "Values"),
                                       ZM_PURE_METHOD(NativeProbe, Line, "From", "To", "Count"), ZM_PURE_METHOD(NativeProbe, Echo, "Entity"), ZM_METHOD(NativeProbe, Fail, "Why") });
    }
    // A graph that uses every probe method: Start -> Add(5) -> Set Sum -> Set Score (Summarize, Break)...
    Graph ProbeGraph()
    {
        Graph g;
        for (const auto* key : { "NativeProbe.Scale", "NativeProbe.Add", "NativeProbe.Greet", "NativeProbe.MedalFor", "NativeProbe.Summarize", "NativeProbe.Line", "NativeProbe.Echo", "ProbeMath.Hypot" })
        {
            NativeSignature signature;
            std::string error;
            CHECK(NativeSignatureOf(key, signature, error));
            CHECK(ImportNativeTypes(g, signature, error));
            g.Natives[key] = signature;
        }
        g.Variables["Sum"] = { Type::Number, 0. };
        g.Variables["Scaled"] = { Type::Number, 0. };
        g.Variables["Greeting"] = { Type::String, "" };
        g.Variables["Medal"] = { Type::String, "" };
        g.Variables["Score"] = { Type::Struct, g.DefaultFor(Type::Struct, "ProbeScore"), "ProbeScore" };
        g.Variables["Points"] = { Type::ArrayVec3, Value::array() };
        g.Variables["Length"] = { Type::Number, 0. };
        g.Variables["Same"] = { Type::Bool, false };
        g.Nodes = { { 1, "Event.Start" }, { 2, "FindEntity", 0, 0, { { "Name", "Probe" } } },
                    { 3, "Native.Call", 0, 0, { { "Method", "NativeProbe.Add" }, { "Amount", 5. } } }, { 4, "Set.Number", 0, 0, { { "Name", "Sum" } } },
                    { 5, "Native.Pure", 0, 0, { { "Method", "NativeProbe.Scale" }, { "Value", 1.5 }, { "Times", 4. } } }, { 6, "Set.Number", 0, 0, { { "Name", "Scaled" } } },
                    { 7, "Native.Pure", 0, 0, { { "Method", "NativeProbe.Greet" }, { "Name", "graph" } } }, { 8, "Set.String", 0, 0, { { "Name", "Greeting" } } },
                    { 9, "Native.Pure", 0, 0, { { "Method", "NativeProbe.MedalFor" }, { "Score", 7. } } }, { 10, "Enum.ToText", 0, 0, { { "Enum", "ProbeMedal" } } }, { 11, "Set.String", 0, 0, { { "Name", "Medal" } } },
                    { 12, "Native.Pure", 0, 0, { { "Method", "NativeProbe.Summarize" }, { "Values", Value::array({ 4., 5., 6. }) } } }, { 13, "Set.Struct", 0, 0, { { "Name", "Score" }, { "Struct", "ProbeScore" } } },
                    { 14, "Native.Pure", 0, 0, { { "Method", "NativeProbe.Line" }, { "From", Value::array({ 0., 0., 0. }) }, { "To", Value::array({ 2., 4., 0. }) }, { "Count", 3. } } }, { 15, "Set.ArrayVec3", 0, 0, { { "Name", "Points" } } },
                    { 16, "Native.Pure", 0, 0, { { "Method", "ProbeMath.Hypot" }, { "X", 3. }, { "Y", 4. } } }, { 17, "Set.Number", 0, 0, { { "Name", "Length" } } },
                    { 18, "Native.Pure", 0, 0, { { "Method", "NativeProbe.Echo" } } }, { 19, "EntityValid" }, { 20, "Set.Bool", 0, 0, { { "Name", "Same" } } } };
        g.Links = { { 1, "Then", 3, "In" }, { 2, "Value", 3, "Target" }, { 3, "Then", 4, "In" }, { 3, "Total", 4, "Value" }, { 4, "Then", 6, "In" }, { 2, "Value", 5, "Target" }, { 5, "Value", 6, "Value" },
                    { 6, "Then", 8, "In" }, { 7, "Value", 8, "Value" }, { 8, "Then", 11, "In" }, { 2, "Value", 9, "Target" }, { 9, "Value", 10, "Value" }, { 10, "Value", 11, "Value" },
                    { 11, "Then", 13, "In" }, { 2, "Value", 12, "Target" }, { 12, "Value", 13, "Value" }, { 13, "Then", 15, "In" }, { 2, "Value", 14, "Target" }, { 14, "Value", 15, "Value" },
                    { 15, "Then", 17, "In" }, { 16, "Value", 17, "Value" }, { 17, "Then", 20, "In" }, { 2, "Value", 18, "Target" }, { 2, "Value", 18, "Entity" }, { 18, "Value", 19, "Target" }, { 19, "Value", 20, "Value" } };
        return g;
    }
    // A project with the graph and a scene with the probe entity and a graph controller; runs Start.
    struct ProbeScene
    {
        std::shared_ptr<Project> Prior = Project::GetActive();
        std::filesystem::path Root;
        Scene World;
        Entity Probe, Controller;
        explicit ProbeScene(const Graph& g, const char* folder = "zamin-native-methods")
        {
            Root = std::filesystem::temp_directory_path() / folder;
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "Assets/Scripts");
            Project::New();
            CHECK(Project::SaveActive(Root / "Native.zproject"));
            CHECK(g.Save(Root / "Assets/Scripts/Native.zgraph"));
            Probe = World.CreateEntity("Probe");
            Probe.AddComponent<NativeScriptComponent>().ClassName = "NativeProbe";
            Controller = World.CreateEntity("Controller");
            auto& c = Controller.AddComponent<NativeScriptComponent>();
            c.ClassName = "Zamin.VisualGraph";
            c.Fields["Graph"].Set(std::string("Scripts/Native.zgraph"));
        }
        VisualGraphHost* Host() { return dynamic_cast<VisualGraphHost*>(Controller.GetComponent<NativeScriptComponent>().Instance); }
        NativeProbe* Instance() { return dynamic_cast<NativeProbe*>(Probe.GetComponent<NativeScriptComponent>().Instance); }
        void Play()
        {
            World.OnRuntimeStart();
            World.UpdateRuntime(.01f);
        }
        ~ProbeScene()
        {
            World.OnRuntimeStop();
            Project::SetActive(Prior);
            std::filesystem::remove_all(Root);
        }
    };
}
ZM_REGISTER_FUNCTIONS(ProbeMath, ZM_PURE_FUNCTION(Hypot, "X", "Y").Tooltip("sqrt(X^2 + Y^2)"))
TEST(BlueprintNativeMethodsReflectTypedSignatures)
{
    auto probe = RegisterProbe();
    const auto* info = ScriptRegistry::Find("NativeProbe");
    CHECK(info && info->Fields.size() == 1 && info->Methods.size() == 8 && info->FindMethod("Add") && !info->FindMethod("Missing"));
    // C++ types become pin types; names come from the macro; statics and free functions need no instance.
    const auto* scale = info->FindMethod("Scale");
    CHECK(scale->IsPure && !scale->Static && scale->Parameters.size() == 2 && scale->Parameters[1].Name == "Times" && scale->Parameters[1].Type.Integer);
    CHECK(info->FindMethod("Greet")->Static && !info->FindMethod("Add")->IsPure && info->FindMethod("Add")->ResultName == "Total" && info->FindMethod("Add")->TooltipText == "Adds to Total");
    CHECK(ScriptValueTypeName(info->FindMethod("Summarize")->Parameters[0].Type) == "Number[]" && ScriptValueTypeName(info->FindMethod("MedalFor")->Result) == "Enum ProbeMedal");
    CHECK(ScriptRegistry::FindEnum("ProbeMedal") && ScriptRegistry::FindEnum("ProbeMedal")->Options[2].Value == 6 && ScriptRegistry::FindStruct("ProbeScore")->Fields.size() == 3);
    std::string owner;
    CHECK(ScriptRegistry::FindMethod("ProbeMath.Hypot", &owner) && owner == "ProbeMath" && ScriptRegistry::FindMethod("ProbeMath.Hypot")->Static);
    CHECK(!ScriptRegistry::FindMethod("NativeProbe") && !ScriptRegistry::FindMethod("NativeProbe.") && !ScriptRegistry::FindMethod("Nobody.Add"));
    const auto keys = NativeMethodKeys();
    CHECK(std::find(keys.begin(), keys.end(), "NativeProbe.Line") != keys.end() && std::find(keys.begin(), keys.end(), "ProbeMath.Hypot") != keys.end());
    NativeSignature s;
    std::string error;
    CHECK(NativeSignatureOf("NativeProbe.Summarize", s, error) && s.Pure && !s.Static && s.Inputs[0].Kind == Type::ArrayNumber && s.Outputs[0].Kind == Type::Struct && s.Outputs[0].Enum == "ProbeScore");
    CHECK(s.Outputs[0].Default == Value({ { "Total", 0. }, { "Count", 0. }, { "Medal", 0 } }));
    CHECK(NativeSignatureOf("NativeProbe.Line", s, error) && s.Outputs[0].Kind == Type::ArrayVec3 && s.Inputs[0].Kind == Type::Vec3);
    CHECK(NativeSignatureOf("NativeProbe.Echo", s, error) && s.Inputs[0].Kind == Type::Entity && s.Text() == "pure (Entity: Entity) -> Value: Entity");
    CHECK(NativeSignatureOf("NativeProbe.Fail", s, error) && !s.Pure && s.Outputs.empty());
    CHECK(!NativeSignatureOf("NativeProbe.Gone", s, error) && error.find("not registered") != std::string::npos);
    // The graph imports the C++ enum and struct; an equal one is shared, a different graph-local one refused.
    Graph g = ProbeGraph();
    CHECK(g.Enums.at("ProbeMedal").Entries == std::vector<EnumEntry>({ { "Bronze", 0 }, { "Silver", 5 }, { "Gold", 6 } }) && g.Enums.at("ProbeMedal").Source == "C++ enum ProbeMedal");
    CHECK(g.Structs.at("ProbeScore").Fields.size() == 3 && g.Structs.at("ProbeScore").Fields[2].Kind == Type::Enum);
    for (const auto& d : g.Validate()) std::printf("    %u: %s\n", d.Node, d.Message.c_str());
    CHECK(g.Validate().empty() && NativeBindingProblems(g).empty());
    Graph local;
    local.Enums["ProbeMedal"] = { { { "Bronze", 0 } }, "" };
    CHECK(NativeSignatureOf("NativeProbe.MedalFor", s, error) && !ImportNativeTypes(local, s, error) && error.find("differs") != std::string::npos);
    // The node's pins: Target (instance methods), the parameters, the result; execution pins for Native.Call.
    const auto call = g.Describe(*g.Find(3)), pure = g.Describe(*g.Find(16));
    CHECK(call.Inputs.size() == 4 && call.Inputs[2].Name == "Target" && call.Inputs[3].Name == "Amount" && call.Outputs.size() == 2 && call.Outputs[1].Name == "Total" && !call.Pure);
    CHECK(pure.Pure && pure.Inputs.size() == 3 && pure.Inputs[1].Name == "X" && pure.Outputs.size() == 1);
}
TEST(BlueprintNativeMethodsRunPureExecStaticStructsAndArrays)
{
    auto probe = RegisterProbe();
    ProbeScene scene(ProbeGraph());
    scene.Play();
    auto* host = scene.Host();
    CHECK(host && host->VM.Error().empty());
    if (!host) return;
    const auto& v = host->VM.Variables();
    CHECK(v.at("Sum") == 5. && scene.Instance() && scene.Instance()->Total == 5);
    CHECK(v.at("Scaled") == 6. && v.at("Greeting") == "Hello graph" && v.at("Medal") == "Silver" && v.at("Length") == 5. && v.at("Same") == true);
    CHECK(v.at("Score") == Value({ { "Total", 15. }, { "Count", 3. }, { "Medal", 6 } }));
    CHECK(v.at("Points") == Value::array({ Value::array({ 0., 0., 0. }), Value::array({ 1., 2., 0. }), Value::array({ 2., 4., 0. }) }));
}
TEST(BlueprintNativeMethodErrorsAreClearAndStopTheGraph)
{
    auto probe = RegisterProbe();
    auto run = [](Graph g, const char* expected) {
        ProbeScene scene(g, "zamin-native-errors");
        scene.Play();
        auto* host = scene.Host();
        const bool ok = host && host->VM.Error().find(expected) != std::string::npos;
        if (!ok && host) std::printf("    error: %s\n", host->VM.Error().c_str());
        return ok;
    };
    // An int parameter takes whole numbers only.
    auto g = ProbeGraph();
    const_cast<Node*>(g.Find(5))->Inputs["Times"] = 2.5;
    CHECK(run(g, "Native method NativeProbe.Scale: expected a whole number"));
    // A C++ exception becomes the graph's error.
    g = ProbeGraph();
    g.Natives["NativeProbe.Fail"] = {};
    std::string error;
    CHECK(NativeSignatureOf("NativeProbe.Fail", g.Natives["NativeProbe.Fail"], error));
    g.Nodes.push_back({ 30, "Native.Call", 0, 0, { { "Method", "NativeProbe.Fail" }, { "Why", "no" } } });
    g.Links.push_back({ 20, "Then", 30, "In" });
    g.Links.push_back({ 2, "Value", 30, "Target" });
    CHECK(run(g, "Native method NativeProbe.Fail: probe refused: no"));
    // An instance method needs the class's live script on its Target.
    g = ProbeGraph();
    std::erase_if(g.Links, [](const Link& l) { return l.To == 3 && l.Input == "Target"; });
    CHECK(run(g, "the Target entity has no live NativeProbe script"));
}
TEST(BlueprintNativeMethodsSurviveHotReloadChanges)
{
    const auto before = ScriptRegistry::Generation();
    auto probe = RegisterProbe();
    CHECK(ScriptRegistry::Generation() != before);
    const Graph g = ProbeGraph();
    // The module is unloaded: the graph still loads, validates and keeps its wires; the nodes cannot run.
    probe.reset();
    CHECK(!ScriptRegistry::Find("NativeProbe"));
    const auto reloaded = Graph::Parse(g.Serialize());
    CHECK(reloaded.Validate().empty() && reloaded.Links.size() == g.Links.size() && reloaded.Natives == g.Natives);
    auto problems = NativeBindingProblems(reloaded);
    CHECK(problems.size() == 7 && problems.at("NativeProbe.Add").find("not registered") != std::string::npos && !problems.contains("ProbeMath.Hypot"));
    {
        ProbeScene scene(g, "zamin-native-unloaded");
        scene.Play();
        auto* host = scene.Host();
        CHECK(host && host->VM.Error().find("Native method NativeProbe.Add: C++ method NativeProbe.Add is not registered") != std::string::npos);
    }
    // A "hot reload" with Scale's signature changed (a third parameter) and Add removed.
    ScriptMethodInfo scale = ZM_PURE_METHOD(NativeProbe, Scale3, "Value", "Times", "Bias");
    scale.Name = "Scale";
    auto changed = std::make_unique<ScriptRegistrar>("NativeProbe", []() -> ScriptableEntity* { return new NativeProbe; },
        std::vector<ScriptMember>{ scale, ZM_PURE_METHOD(NativeProbe, Greet, "Name") });
    problems = NativeBindingProblems(reloaded);
    CHECK(problems.at("NativeProbe.Scale").find("the C++ signature changed: the graph has pure (Value: Number, Times: Number) -> Value: Number, C++ has pure (Value: Number, Times: Number, Bias: Number)") != std::string::npos);
    CHECK(problems.at("NativeProbe.Add").find("not registered") != std::string::npos && !problems.contains("NativeProbe.Greet"));
    // Refreshing the node takes the live signature: the new pin appears, the wires and literals that fit stay.
    Graph refreshed = reloaded;
    NativeSignature live;
    std::string error;
    CHECK(NativeSignatureOf("NativeProbe.Scale", live, error));
    CHECK(refreshed.SetNative("NativeProbe.Scale", live) == 0 && refreshed.Describe(*refreshed.Find(5)).Inputs.size() == 5 && refreshed.Find(5)->Inputs.at("Times") == 4.);
    CHECK(!NativeBindingProblems(refreshed).contains("NativeProbe.Scale"));
    changed.reset();
    // Back to the original module: the same graph binds and runs again (the host re-binds on the new generation).
    probe = RegisterProbe();
    CHECK(NativeBindingProblems(reloaded).empty());
    ProbeScene scene(reloaded, "zamin-native-rebound");
    scene.Play();
    CHECK(scene.Host() && scene.Host()->VM.Error().empty() && scene.Host()->VM.Variables().at("Sum") == 5.);
}
TEST(BlueprintNativeFormatVersionAndValidation)
{
    auto probe = RegisterProbe();
    const Graph g = ProbeGraph();
    // Format 6 round trip: only the signatures the nodes use are saved.
    auto json = g.Serialize();
    CHECK(json.at("Version") == 6 && json.at("Natives").size() == 8 && json.at("Natives").at("NativeProbe.Add").at("Pure") == false);
    Graph extra = g;
    extra.Natives["NativeProbe.Unused"] = g.Natives.at("NativeProbe.Echo");
    CHECK(extra.Serialize() == json && Graph::Parse(json).Serialize() == json);
    // A version 5 file cannot hold native calls.
    auto old = json;
    old["Version"] = 5;
    old.erase("Natives");
    bool threw = false;
    try { Graph::Parse(old); } catch (const std::exception& e) { threw = std::string(e.what()).find("version 6") != std::string::npos; }
    CHECK(threw);
    auto mentions = [](const Graph& graph, const char* text) {
        for (const auto& d : graph.Validate())
            if (d.Message.find(text) != std::string::npos) return true;
        return false;
    };
    // The node kind must match the signature's purity; the method name is a literal; the signature must exist.
    Graph bad = g;
    const_cast<Node*>(bad.Find(5))->Kind = "Native.Call";
    CHECK(mentions(bad, "is pure: use Native.Pure"));
    bad = g;
    bad.Links.push_back({ 7, "Value", 16, "Method" });
    CHECK(mentions(bad, "Native method name must be a literal"));
    bad = g;
    const_cast<Node*>(bad.Find(16))->Inputs["Method"] = "ProbeMath.Missing";
    CHECK(mentions(bad, "Unknown native method (no signature in the graph): ProbeMath.Missing"));
    bad = g;
    bad.Natives["NativeProbe.Add"].Inputs.push_back({ "Target", Type::Number, 0. });
    CHECK(mentions(bad, "Invalid/duplicate/reserved pin in native method NativeProbe.Add"));
    // The clipboard carries the signatures; a paste into a graph with another version of the method is refused.
    std::set<uint32_t> inserted;
    std::string error;
    Graph target;
    CHECK(target.PasteNodes(g.CopyNodes({ 16 }), 0, 0, inserted, error) && target.Natives.contains("ProbeMath.Hypot") && inserted.size() == 1);
    Graph other;
    other.Natives["ProbeMath.Hypot"] = g.Natives.at("NativeProbe.Greet");
    CHECK(!other.PasteNodes(g.CopyNodes({ 16 }), 0, 0, inserted, error) && error.find("another signature") != std::string::npos);
    // The cooked program keeps the signatures and runs the same native calls.
    const auto cooked = Bytecode::Compile(g);
    Program program;
    CHECK(cooked.Ok && Bytecode::Read(cooked.Bytes.data(), cooked.Bytes.size(), program, error) && program.Runnable.Natives == g.Natives);
}
TEST(BlueprintGraphCookCompilesValidatesCachesAndKeepsParents)
{
    auto probe = RegisterProbe();
    const auto root = std::filesystem::temp_directory_path() / "zamin-graph-cook";
    std::filesystem::remove_all(root);
    const auto source = root / "Source/Assets", exported = root / "Export/Assets", cache = root / "Cache";
    std::filesystem::create_directories(source / "Scripts");
    // Good.zgraph is valid; Base.zgraph is the parent of Broken.zgraph, which does not validate.
    Graph good;
    good.Variables["Count"] = { Type::Number, 0. };
    good.Nodes = { { 1, "Event.Start" }, { 2, "Get.Number", 0, 0, { { "Name", "Count" } } }, { 3, "Add", 0, 0, { { "B", 2. } } }, { 4, "Set.Number", 0, 0, { { "Name", "Count" } } } };
    good.Links = { { 1, "Then", 4, "In" }, { 2, "Value", 3, "A" }, { 3, "Value", 4, "Value" } };
    CHECK(good.Save(source / "Scripts/Good.zgraph"));
    Graph base = good;
    base.Variables["Spare"] = { Type::Bool, false };
    CHECK(base.Save(source / "Scripts/Base.zgraph"));
    Graph broken;
    broken.Parent = "Scripts/Base.zgraph";
    broken.Nodes = { { 10, "Event.Start" }, { 11, "Set.Number", 0, 0, { { "Name", "Missing" } } } };
    broken.Links = { { 10, "Then", 11, "In" } };
    CHECK(broken.Save(source / "Scripts/Broken.zgraph"));
    CHECK(ProbeGraph().Save(source / "Scripts/Native.zgraph"));
    std::filesystem::create_directories(exported.parent_path());
    std::filesystem::copy(source, exported, std::filesystem::copy_options::recursive);
    GraphCookInputs inputs{ source, exported, { "Scripts/Base.zgraph", "Scripts/Broken.zgraph", "Scripts/Good.zgraph", "Scripts/Native.zgraph", "Textures/a.png" }, cache };
    auto result = ExportCook::CookGraphs(inputs);
    CHECK(result.Graphs.size() == 4 && result.CacheHits == 0);
    auto find = [&](const char* path) { return *std::find_if(result.Graphs.begin(), result.Graphs.end(), [&](const CookedGraph& g) { return g.Path == path; }); };
    CHECK(find("Scripts/Good.zgraph").Action == "cooked" && find("Scripts/Good.zgraph").CookedBytes < find("Scripts/Good.zgraph").SourceBytes && find("Scripts/Good.zgraph").Nodes == 4);
    CHECK(find("Scripts/Broken.zgraph").Action == "rejected" && find("Scripts/Broken.zgraph").Note.find("Unknown variable: Missing") != std::string::npos);
    CHECK(find("Scripts/Base.zgraph").Action == "kept" && find("Scripts/Native.zgraph").Action == "cooked" && find("Scripts/Native.zgraph").Note.empty());
    CHECK(Bytecode::IsCookedFile(exported / "Scripts/Good.zgraph") && !Bytecode::IsCookedFile(exported / "Scripts/Broken.zgraph") && !Bytecode::IsCookedFile(exported / "Scripts/Base.zgraph"));
    CHECK(result.Warnings.size() == 1 && result.Warnings[0].find("Broken.zgraph was not cooked") != std::string::npos);
    // The second cook takes the unchanged graphs from the cache; a changed graph is compiled again.
    good.Nodes[2].Inputs["B"] = 3.;
    CHECK(good.Save(source / "Scripts/Good.zgraph"));
    result = ExportCook::CookGraphs(inputs);
    CHECK(result.CacheHits == 1 && find("Scripts/Native.zgraph").CacheHit && !find("Scripts/Good.zgraph").CacheHit);
    // A native method the loaded module does not have is a warning (the graph is cooked; the node fails when it runs).
    probe.reset();
    result = ExportCook::CookGraphs(inputs);
    CHECK(find("Scripts/Native.zgraph").Action == "cooked" && find("Scripts/Native.zgraph").Note.find("native: NativeProbe.Add") != std::string::npos);
    probe = RegisterProbe();
    // The Player's host runs the cooked graph (here from the exported folder).
    auto prior = Project::GetActive();
    Project::New();
    CHECK(Project::SaveActive(root / "Export/Cooked.zproject"));
    Scene world;
    auto e = world.CreateEntity("Controller");
    auto& c = e.AddComponent<NativeScriptComponent>();
    c.ClassName = "Zamin.VisualGraph";
    c.Fields["Graph"].Set(std::string("Scripts/Good.zgraph"));
    world.OnRuntimeStart();
    world.UpdateRuntime(.01f);
    auto* host = dynamic_cast<VisualGraphHost*>(c.Instance);
    CHECK(host && host->VM.Error().empty() && host->VM.Variables().at("Count") == 3.);
    world.OnRuntimeStop();
    Project::SetActive(prior);
    std::filesystem::remove_all(root);
}
