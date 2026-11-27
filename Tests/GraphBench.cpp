// ZaminBench --graphs: large Visual Script graphs, headless (no GL). Each case is generated, saved as a .zgraph and
// measured: loading the source file (JSON parse + Graph::Parse), validating it, cooking it to bytecode
// (Visual::Bytecode::Compile), loading the VM from the source (validate, resolve, expand, bind) against loading it
// from the cooked file, running its Start event, and running it with the VM profiler on.
//
//   ZaminBench --graphs [--nodes N] [--repeat R] [--case <part of a name>] [--json <file>] [--write <folder>]
//
// --write <folder>: also saves each generated graph there as Bench<Case>.zgraph (the editor's large graph checks).
// Cases (N: about 4000 nodes by default; a graph file holds at most 4096):
//   wide    one event, an execution chain of N/4 Set nodes, each fed by a small expression (Get, Multiply, Add)
//   deep    31 typed functions calling each other (F0 -> F1 -> ... -> F30, the VM's depth limit is 32), run 100 times
//           from a For loop, plus filler expressions up to N nodes
//   loops   nested For loops (100 x 100) whose body adds to an accumulator, plus filler expressions up to N nodes
//   macros  N/8 instances of one inline macro (expanded at load / cook time) on an execution chain
// The cooked program must run like the source (the same variables) and the profiler must count every executed node,
// else the exit code is 3. Times in ms (median of R repeats, 9 by default); sizes in bytes.
#include <Zamin/Scripting/Visual/VisualGraph.h>
#include <Zamin/Scripting/Visual/VisualGraphBytecode.h>
#include <Zamin/Utils/FileSystem.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <vector>

using namespace Zamin;
using namespace Zamin::Visual;

namespace {
    using Clock = std::chrono::steady_clock;
    double Median(std::vector<double> v)
    {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0 : v[v.size() / 2];
    }
    // Calls fn repeat times; the median ms.
    double Time(int repeat, const std::function<void()>& fn)
    {
        std::vector<double> ms;
        for (int i = 0; i < repeat; ++i)
        {
            const auto start = Clock::now();
            fn();
            ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
        return Median(ms);
    }
    struct Builder
    {
        Graph G;
        uint32_t Next = 1;
        float X = 0, Y = 0;
        uint32_t Add(const std::string& kind, std::map<std::string, Value> inputs = {}, const std::string& macro = {})
        {
            const uint32_t id = Next++;
            G.Nodes.push_back({ id, kind, X, Y, std::move(inputs), false, macro });
            X += 260;
            if (X > 26000) X = 0, Y += 220;
            return id;
        }
        void Link(uint32_t from, const char* output, uint32_t to, const char* input) { G.Links.push_back({ from, output, to, input }); }
        // Pure filler: Get X -> Multiply -> Add (Accum stays untouched by them; they make the graph big).
        void Fill(size_t nodes)
        {
            while (G.Nodes.size() + 3 <= nodes)
            {
                const auto get = Add("Get.Number", { { "Name", "X" } }), mul = Add("Multiply", { { "B", 2. } }), add = Add("Add", { { "B", 1. } });
                Link(get, "Value", mul, "A");
                Link(mul, "Value", add, "A");
            }
        }
    };
    Graph Wide(size_t nodes)
    {
        Builder b;
        b.G.Variables["Accum"] = { Type::Number, 0. };
        b.G.Variables["X"] = { Type::Number, 1.5 };
        uint32_t last = b.Add("Event.Start");
        const char* out = "Then";
        while (b.G.Nodes.size() + 4 <= nodes)
        {
            const auto set = b.Add("Set.Number", { { "Name", "Accum" } }), get = b.Add("Get.Number", { { "Name", "Accum" } }), x = b.Add("Get.Number", { { "Name", "X" } });
            const auto mul = b.Add("Multiply", { { "B", .5 } }), add = b.Add("Add");
            b.Link(last, out, set, "In");
            b.Link(x, "Value", mul, "A");
            b.Link(get, "Value", add, "A");
            b.Link(mul, "Value", add, "B");
            b.Link(add, "Value", set, "Value");
            last = set;
        }
        b.Fill(nodes);
        return b.G;
    }
    Graph Deep(size_t nodes)
    {
        Builder b;
        b.G.Variables["Accum"] = { Type::Number, 0. };
        b.G.Variables["X"] = { Type::Number, 1.5 };
        constexpr int depth = 31;
        for (int f = 0; f < depth; ++f) b.G.Functions["F" + std::to_string(f)] = { { { "X", Type::Number, 0. } }, { { "Value", Type::Number, 0. } }, {} };
        for (int f = 0; f < depth; ++f)
        {
            const std::string name = "F" + std::to_string(f);
            const auto entry = b.Add("Function.Entry", { { "Name", name } }), ret = b.Add("Function.Return", { { "Name", name } }), add = b.Add("Add", { { "B", 1. } });
            b.Link(entry, "X", add, "A");
            if (f + 1 < depth)
            {
                const auto call = b.Add("Function.Call", { { "Name", "F" + std::to_string(f + 1) } });
                b.Link(entry, "Then", call, "In");
                b.Link(add, "Value", call, "X");
                b.Link(call, "Then", ret, "In");
                b.Link(call, "Value", ret, "Value");
            }
            else
            {
                b.Link(entry, "Then", ret, "In");
                b.Link(add, "Value", ret, "Value");
            }
        }
        const auto start = b.Add("Event.Start"), loop = b.Add("For", { { "Count", 100. } }), call = b.Add("Function.Call", { { "Name", "F0" } });
        const auto set = b.Add("Set.Number", { { "Name", "Accum" } }), get = b.Add("Get.Number", { { "Name", "Accum" } }), add = b.Add("Add");
        b.Link(start, "Then", loop, "In");
        b.Link(loop, "Body", call, "In");
        b.Link(loop, "Index", call, "X");
        b.Link(call, "Then", set, "In");
        b.Link(get, "Value", add, "A");
        b.Link(call, "Value", add, "B");
        b.Link(add, "Value", set, "Value");
        b.Fill(nodes);
        return b.G;
    }
    Graph Loops(size_t nodes)
    {
        Builder b;
        b.G.Variables["Accum"] = { Type::Number, 0. };
        b.G.Variables["X"] = { Type::Number, 1.5 };
        const auto start = b.Add("Event.Start"), outer = b.Add("For", { { "Count", 100. } }), inner = b.Add("For", { { "Count", 100. } });
        const auto set = b.Add("Set.Number", { { "Name", "Accum" } }), get = b.Add("Get.Number", { { "Name", "Accum" } }), mul = b.Add("Multiply", { { "B", .5 } }), add = b.Add("Add");
        b.Link(start, "Then", outer, "In");
        b.Link(outer, "Body", inner, "In");
        b.Link(inner, "Body", set, "In");
        b.Link(inner, "Index", mul, "A");
        b.Link(get, "Value", add, "A");
        b.Link(mul, "Value", add, "B");
        b.Link(add, "Value", set, "Value");
        b.Fill(nodes);
        return b.G;
    }
    Graph Macros(size_t nodes)
    {
        Builder b;
        b.G.Variables["Accum"] = { Type::Number, 0. };
        b.G.Variables["X"] = { Type::Number, 1.5 };
        // Step: In -> Set Accum = Accum + Amount -> Then.
        b.G.Macros["Step"] = { { { "In", Type::Exec, nullptr }, { "Amount", Type::Number, 1. } }, { { "Then", Type::Exec, nullptr } } };
        const auto entry = b.Add("Macro.Entry", {}, "Step"), exit = b.Add("Macro.Exit", {}, "Step"), set = b.Add("Set.Number", { { "Name", "Accum" } }, "Step");
        const auto get = b.Add("Get.Number", { { "Name", "Accum" } }, "Step"), add = b.Add("Add", {}, "Step");
        b.Link(entry, "In", set, "In");
        b.Link(set, "Then", exit, "Then");
        b.Link(get, "Value", add, "A");
        b.Link(entry, "Amount", add, "B");
        b.Link(add, "Value", set, "Value");
        uint32_t last = b.Add("Event.Start");
        const char* out = "Then";
        // Each instance expands to the body's 3 nodes: the expanded program stays within a graph's 4096 nodes.
        for (size_t i = 0; i < nodes / 8; ++i)
        {
            const auto instance = b.Add("Macro.Instance", { { "Name", "Step" }, { "Amount", double(i % 7) } });
            b.Link(last, out, instance, "In");
            last = instance;
        }
        b.Fill(nodes - nodes / 4);
        return b.G;
    }
    struct Case
    {
        const char* Name;
        std::function<Graph(size_t)> Make;
    };
    struct Result
    {
        std::string Name;
        size_t Nodes = 0, Links = 0, ProgramNodes = 0, SourceBytes = 0, CookedBytes = 0, Steps = 0;
        double LoadSource = 0, Validate = 0, Cook = 0, VMSource = 0, VMCooked = 0, Run = 0, RunProfiled = 0;
        bool Same = false, Profiled = false;
    };
}

int RunGraphBench(int argc, char** argv)
{
    size_t nodes = 4000;
    int repeat = 9;
    std::string only, json, write;
    for (int i = 1; i < argc; i++)
    {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--graphs")) continue;
        else if (!std::strcmp(argv[i], "--nodes")) nodes = size_t(std::clamp(std::atoi(next()), 64, 4096));
        else if (!std::strcmp(argv[i], "--repeat")) repeat = std::clamp(std::atoi(next()), 1, 100);
        else if (!std::strcmp(argv[i], "--case")) only = next();
        else if (!std::strcmp(argv[i], "--json")) json = next();
        else if (!std::strcmp(argv[i], "--write")) write = next();
        else
        {
            std::printf("ZaminBench --graphs [--nodes N] [--repeat R] [--case <part of a name>] [--json <file>] [--write <folder>]\n");
            return 1;
        }
    }
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    TempDirectory temp("zamin-graph-bench");
    const std::vector<Case> cases = { { "wide", Wide }, { "deep", Deep }, { "loops", Loops }, { "macros", Macros } };
    std::vector<Result> results;
    bool ok = true;
    std::printf("Visual Script graphs (%s build, about %zu nodes, median of %d)\n", build, nodes, repeat);
    std::printf("%-8s %6s %6s %10s %10s %9s %9s %9s %9s %9s %9s %9s\n", "case", "nodes", "run", "source B", "cooked B", "load ms", "valid ms", "cook ms",
                "vm src", "vm cook", "run ms", "prof ms");
    for (const auto& c : cases)
    {
        if (!only.empty() && std::string(c.Name).find(only) == std::string::npos) continue;
        Result r;
        r.Name = c.Name;
        const Graph made = c.Make(nodes);
        const auto path = temp.GetPath() / (r.Name + ".zgraph");
        if (!made.Save(path))
        {
            std::printf("%s: the graph could not be saved\n", c.Name);
            return 3;
        }
        r.Nodes = made.Nodes.size();
        r.Links = made.Links.size();
        if (!write.empty())
        {
            std::string name = r.Name;
            name[0] = char(std::toupper(name[0]));
            std::filesystem::create_directories(write);
            if (!made.Save(std::filesystem::path(write) / ("Bench" + name + ".zgraph"))) std::printf("%s: could not be written to %s\n", c.Name, write.c_str());
        }
        std::error_code ec;
        r.SourceBytes = size_t(std::filesystem::file_size(path, ec));
        Graph loaded;
        r.LoadSource = Time(repeat, [&] { loaded = Graph::Load(path); });
        std::vector<Diagnostic> diagnostics;
        r.Validate = Time(repeat, [&] { diagnostics = loaded.Validate(); });
        if (!diagnostics.empty())
        {
            std::printf("%s: the generated graph does not validate: node %u: %s\n", c.Name, diagnostics[0].Node, diagnostics[0].Message.c_str());
            return 3;
        }
        Bytecode::CompileResult cooked;
        r.Cook = Time(repeat, [&] { cooked = Bytecode::Compile(loaded); });
        const auto cookedPath = temp.GetPath() / (r.Name + ".zgbc");
        FileSystem::WriteBinaryFile(cookedPath, cooked.Bytes.data(), cooked.Bytes.size());
        r.CookedBytes = cooked.Bytes.size();
        r.ProgramNodes = cooked.Nodes;
        // The VM from the source file (as the editor's Play does) against the VM from the cooked file (the Player).
        Machine source, binary;
        source.Budget = binary.Budget = 10'000'000;
        r.VMSource = Time(repeat, [&] { source.Load(Graph::Load(path)); });
        std::string error;
        r.VMCooked = Time(repeat, [&] {
            Program program;
            if (Bytecode::ReadFile(cookedPath, program, error)) binary.Load(std::move(program));
        });
        if (!source.Error().empty() || !binary.Error().empty() || !error.empty())
        {
            std::printf("%s: load failed: %s%s%s\n", c.Name, source.Error().c_str(), binary.Error().c_str(), error.c_str());
            return 3;
        }
        r.Run = Time(repeat, [&] {
            source.Event("Start");
            binary.Event("Start");
        }) / 2.0; // (both machines run the event: half is one run)
        r.Same = source.Error().empty() && binary.Error().empty() && source.Variables() == binary.Variables();
        // The profiler on: its overhead, and it must have counted the steps.
        Machine profiled;
        profiled.Budget = source.Budget;
        profiled.Profiling = true;
        profiled.Load(Graph::Load(path));
        r.RunProfiled = Time(repeat, [&] { profiled.Event("Start"); });
        uint64_t steps = 0;
        for (const auto& [id, n] : profiled.GetProfile().Nodes) steps += n.Calls;
        r.Steps = size_t(steps / uint64_t(repeat));
        r.Profiled = profiled.Error().empty() && steps > 0 && !profiled.GetProfile().HotPath().empty();
        if (!r.Same || !r.Profiled)
        {
            std::printf("%s: the cooked program ran differently (%s) or the profiler counted nothing\n", c.Name, source.Error().c_str());
            ok = false;
        }
        std::printf("%-8s %6zu %6zu %10zu %10zu %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f\n", c.Name, r.Nodes, r.Steps, r.SourceBytes, r.CookedBytes, r.LoadSource, r.Validate,
                    r.Cook, r.VMSource, r.VMCooked, r.Run, r.RunProfiled);
        results.push_back(std::move(r));
    }
    if (!json.empty())
    {
        std::string text = std::format("{{\n  \"Build\": \"{}\",\n  \"Nodes\": {},\n  \"Repeat\": {},\n  \"Results\": {{\n", build, nodes, repeat);
        std::vector<std::string> rows;
        for (const auto& r : results)
        {
            for (const auto& [key, ms] : std::vector<std::pair<const char*, double>>{ { "LoadSourceMs", r.LoadSource }, { "ValidateMs", r.Validate }, { "CookMs", r.Cook },
                                                                                      { "VMLoadSourceMs", r.VMSource }, { "VMLoadCookedMs", r.VMCooked }, { "RunMs", r.Run }, { "RunProfiledMs", r.RunProfiled } })
                rows.push_back(std::format("    \"{}.{}\": {{ \"Ms\": {:.4f} }}", r.Name, key, ms));
            rows.push_back(std::format("    \"{}.SourceBytes\": {{ \"Value\": {}, \"Unit\": \"B\" }}", r.Name, r.SourceBytes));
            rows.push_back(std::format("    \"{}.CookedBytes\": {{ \"Value\": {}, \"Unit\": \"B\" }}", r.Name, r.CookedBytes));
        }
        for (size_t i = 0; i < rows.size(); ++i) text += rows[i] + (i + 1 < rows.size() ? ",\n" : "\n");
        text += "  },\n  \"Cases\": [\n";
        for (size_t i = 0; i < results.size(); ++i)
        {
            const auto& r = results[i];
            text += std::format("    {{ \"Name\": \"{}\", \"Nodes\": {}, \"Links\": {}, \"ProgramNodes\": {}, \"Steps\": {}, \"SourceBytes\": {}, \"CookedBytes\": {}, "
                                "\"LoadSpeedup\": {:.2f}, \"SameResult\": {}, \"Profiled\": {} }}{}\n",
                                r.Name, r.Nodes, r.Links, r.ProgramNodes, r.Steps, r.SourceBytes, r.CookedBytes, r.VMCooked > 0 ? r.VMSource / r.VMCooked : 0.0,
                                r.Same ? "true" : "false", r.Profiled ? "true" : "false", i + 1 < results.size() ? "," : "");
        }
        text += "  ]\n}\n";
        FileSystem::WriteTextFile(json, text);
        std::printf("\nJSON: %s\n", json.c_str());
    }
    return ok ? 0 : 3;
}
