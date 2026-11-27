#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <memory>
#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace Zamin::Visual {
    using Value = nlohmann::json;
    enum class Type
    {
        Exec,
        Bool,
        Number,
        String,
        Vec2,
        Vec3,
        Vec4,
        Entity,
        // A value of a named enum (Graph::Enums): an integer that must be one of the enum's entry values.
        // The pin/variable names its enum in Pin::Enum / Variable::Enum.
        Enum,
        // A value of a graph-local struct (Graph::Structs): a JSON object with exactly the struct's fields, each of
        // its field's type. The pin/variable names its struct in Pin::Enum / Variable::Enum (the "named type").
        Struct,
        ArrayBool,
        ArrayNumber,
        ArrayString,
        ArrayVec2,
        ArrayVec3,
        ArrayVec4,
        ArrayEntity,
        // An array of one enum's values (each an entry value of the enum the pin/variable names).
        ArrayEnum,
        // An array of one struct's values (up to 1024 elements, 64 KiB).
        ArrayStruct,
        // A float curve: keys of time, value, interpolation and tangents (see the Curve functions below).
        Curve,
        Count
    };
    struct Pin
    {
        std::string Name;
        Type Kind;
        Value Default;
        // The named type: the enum's name when Kind == Type::Enum/ArrayEnum, the struct's name for Struct/ArrayStruct.
        std::string Enum;
        bool operator==(const Pin&) const = default;
    };
    struct Definition
    {
        std::string Name, Category;
        std::vector<Pin> Inputs, Outputs;
        bool Pure = false;
    };
    struct Node
    {
        uint32_t ID = 0;
        std::string Kind;
        float X = 0, Y = 0;
        std::map<std::string, Value> Inputs;
        bool Breakpoint = false;
        // The macro whose body holds this node (Graph::Macros); empty for a node of the graph itself.
        std::string Macro;
    };
    struct Link
    {
        uint32_t From = 0;
        std::string Output;
        uint32_t To = 0;
        std::string Input;
    };
    struct Variable
    {
        Type Kind = Type::Number;
        Value Default = 0.0;
        std::string Enum; // the named type (enum or struct name), see Pin::Enum
    };
    // One named value of an enum. Values are integers within +-2^53 (exact in JSON); names and values are unique.
    struct EnumEntry
    {
        std::string Name;
        int64_t Value = 0;
        bool operator==(const EnumEntry&) const = default;
    };
    // A graph-local enum, or one imported from a reflected C++ enum field (Source "Class.Field").
    struct EnumDefinition
    {
        std::vector<EnumEntry> Entries;
        std::string Source;
        const EnumEntry* Find(int64_t value) const;
        const EnumEntry* Find(const std::string& name) const;
        bool operator==(const EnumDefinition&) const = default;
    };
    // A graph-local struct: 1..32 named, typed fields (any value type except Exec; enums, arrays and other structs of
    // the graph are allowed, but a struct cannot contain itself, directly or through other structs). A field's Default
    // is the field's value in a new struct value.
    struct StructDefinition
    {
        std::vector<Pin> Fields;
        const Pin* Find(const std::string& name) const;
        bool operator==(const StructDefinition&) const = default;
    };
    // An inline macro: a subgraph of the graph's own nodes (Node::Macro == the macro's name) between one Macro.Entry
    // (its outputs are the macro's Inputs) and one Macro.Exit (its inputs are the Outputs). Pins may be Exec or data;
    // unlike a typed function the body may hold latent flow (Delay, loops). Each Macro.Instance node is replaced by a
    // copy of the body at compile time (Graph::Expand), with node IDs unique per instance.
    struct MacroDefinition
    {
        std::vector<Pin> Inputs, Outputs;
        bool operator==(const MacroDefinition&) const = default;
    };
    // Curves (Type::Curve, format 5). A curve value is {"Keys": [{"Time", "Value", "Mode", "In", "Out"}, ...]}: 0..256
    // keys of finite numbers in strictly increasing Time. Mode is the interpolation from that key to the next one:
    // "Constant" holds the key's value, "Linear" goes straight to the next value, "Cubic" is a Hermite spline with
    // the key's Out tangent and the next key's In tangent (value per time unit). Before the first and after the
    // last key the curve holds the end key's value; an empty curve is 0 everywhere.
    enum class CurveMode
    {
        Constant,
        Linear,
        Cubic
    };
    struct CurveKey
    {
        double Time = 0, Value = 0;
        CurveMode Mode = CurveMode::Linear;
        double In = 0, Out = 0;
        bool operator==(const CurveKey&) const = default;
    };
    constexpr size_t MaxCurveKeys = 256;
    const char* CurveModeName(CurveMode mode);
    // A valid curve value (the shape above, every key complete, times increasing).
    bool IsCurve(const Value& value);
    // The keys of a valid curve (an invalid value gives none).
    std::vector<CurveKey> CurveKeys(const Value& curve);
    // The curve value of keys (sorted by time; the caller keeps the times distinct).
    Value MakeCurve(std::vector<CurveKey> keys);
    double EvaluateCurve(const std::vector<CurveKey>& keys, double time);
    double EvaluateCurve(const Value& curve, double time);
    struct Diagnostic
    {
        uint32_t Node = 0;
        std::string Message;
        // For a finding inside an expanded macro: the body node of the macro it comes from (Node is the macro
        // instance in the graph). 0 otherwise.
        uint32_t Source = 0;
    };
    const std::map<std::string, Definition>& Catalog();
    // Reads a graph file's JSON (at most 4 MB, nesting at most 64 levels: checked on the text before parsing, which
    // is much faster than a parser callback on large graphs).
    Value ReadGraphJson(const std::filesystem::path& path);
    // Named values of an enumerated Number input (the dispatch validates it as an integer 0..N-1): the editor shows
    // a combo of these labels (Uzbek source texts, translated with the editor's Tr) and stores the index. The pin
    // and the file format stay plain numbers. Null for any other pin.
    const std::vector<const char*>* PinChoices(const std::string& kind, const std::string& pin);
    const char* TypeName(Type type);
    bool IsArray(Type type);
    // A type that names a graph definition: Enum, ArrayEnum, Struct and ArrayStruct (Pin::Enum holds the name).
    bool IsNamed(Type type);
    Type ElementType(Type type);
    Value DefaultValue(Type type);
    size_t ValueBytes(const Value& value);
    bool Matches(Type type, const Value& value);
    // Same pin type: the kind, and for an Enum/Struct pin the same enum/struct.
    bool SameType(const Pin& a, const Pin& b);
    struct FunctionSignature
    {
        std::vector<Pin> Inputs, Outputs;
        std::map<std::string, Variable> Locals;
    };
    // A reflected C++ method or free function a graph calls (format 6, the Native.Call / Native.Pure nodes, whose
    // literal Method names it): the signature the graph was built with, kept in the graph so that it loads,
    // validates and keeps its wires without the C++ module. Key: "Class.Method" or "Library.Function". The host
    // binds it to the live method when the graph runs and reports a node whose method is gone or changed
    // (VisualGraphHost.h: NativeBindingProblem).
    struct NativeSignature
    {
        bool Static = false;              // no Target input (a static member or a free function)
        bool Pure = false;                // a data node (Native.Pure) instead of an execution node (Native.Call)
        std::vector<Pin> Inputs, Outputs; // the parameters; the result (none or one)
        std::string Tooltip;
        // The same kind and pins (names, types, named types); defaults and tooltips do not count.
        bool Matches(const NativeSignature& other) const;
        // "static pure (X: Number, Y: Number) -> Value: Number"
        std::string Text() const;
        bool operator==(const NativeSignature&) const = default;
    };
    constexpr size_t MaxNatives = 64;
    // The Method literal of a Native.Call / Native.Pure node (empty for other nodes).
    std::string NativeKey(const Node& node);
    struct Graph
    {
        // Graph inheritance (format 5): the parent's variables, functions, enums, structs, macros and event handlers
        // are the child's too. A child overrides a parent function by defining one with the same signature (its
        // Parent.Call node runs the parent's version) and a parent event by handling the same event (Parent.Event
        // runs the parent's handlers); a call of an overridden function anywhere, the parent's own nodes included,
        // runs the override. A member renamed in a parent (Renames) is renamed in its children when they load.
        struct Rename
        {
            std::string Kind, From, To; // Kind: Variable, Function, Enum, Struct or Macro
            bool operator==(const Rename&) const = default;
        };
        struct Inheritance
        {
            std::vector<std::string> Chain; // the parent paths, the nearest first
            // The members that come from the parents (in this graph's maps for lookups, but not saved with it).
            std::set<std::string> Variables, Functions, Enums, Structs, Macros;
            // The event keys ("Start", "Custom.Name" ...) the parents handle.
            std::set<std::string> Events;
            std::vector<Diagnostic> Errors; // missing, unreadable or cyclic parents, conflicting members
            // The whole parent chain as one resolved graph (its nodes keep the parent files' IDs).
            std::shared_ptr<const Graph> Base;
            // Resolved graphs only: node ID -> {parent path, the node's ID in that file}; event handlers replaced
            // by an override; each Parent.Event node -> the parent handlers it runs.
            std::map<uint32_t, std::pair<std::string, uint32_t>> Origins;
            std::set<uint32_t> OverriddenEvents;
            std::map<uint32_t, std::vector<uint32_t>> ParentEvents;
        };
        static constexpr size_t MaxRenames = 64, MaxInheritanceDepth = 8;
        // The parent graph (an Assets-relative .zgraph path); empty: none.
        std::string Parent;
        // This graph's own member renames, the oldest first (at most MaxRenames), for its children.
        std::vector<Rename> Renames;
        Inheritance Inherited;
        // Loads the parent chain from disk (Parent, relative to assets) and inherits from it; earlier inherited
        // members are dropped first. Problems land in Inherited.Errors (Validate reports them). self: this graph's
        // own file, for the cycle check.
        void LoadParent(const std::filesystem::path& assets, const std::filesystem::path& self = {});
        // Inherits from a parent graph already loaded (with its own parents): the editor and tests.
        void InheritFrom(const Graph& parent, const std::string& parentPath);
        // Drops the inherited members (the graph as its file holds it).
        void ClearInherited();
        // The graph the VM runs: the parent chain's nodes (new IDs above this graph's), overridden functions
        // renamed to "<Parent>::<Name>", Parent.Call turned into calls of them. Without a parent: a copy.
        Graph Resolved() const;
        // The functions / event keys this graph overrides (its own definitions of what the parents have).
        std::set<std::string> FunctionOverrides() const;
        std::set<std::string> EventOverrides() const;
        // The event key of an event node ("Start", "Custom.Name"); empty for other nodes.
        static std::string EventKey(const Node& node);
        bool IsInherited(const std::string& kind, const std::string& name) const;
        // Version 2 added enums (Type::Enum, "Enums", the "Enum" key of pins/variables); version 3 added structs
        // (Type::Struct/ArrayStruct, "Structs", the "Struct" key); version 4 added inline macros ("Macros", the
        // "Macro" key of body nodes), Type::ArrayEnum and struct fields of struct type. Version 1-3 files load
        // unchanged (they cannot hold the newer data) and are written back as version 4. Version 5 added curves
        // (Type::Curve) and inheritance ("Parent", "Renames", the Parent.Call/Parent.Event nodes); version 1-4 files
        // load unchanged and are written back as version 5.
        // Version 6 added native method calls ("Natives", the Native.Call/Native.Pure nodes); version 1-5 files load
        // unchanged and are written back as version 6.
        static constexpr int FormatVersion = 6;
        int SourceVersion = FormatVersion; // the version the graph was parsed from
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        std::map<std::string, Variable> Variables;
        std::map<std::string, FunctionSignature> Functions;
        std::map<std::string, EnumDefinition> Enums;
        std::map<std::string, StructDefinition> Structs;
        std::map<std::string, MacroDefinition> Macros;
        // The native method signatures the graph's Native.* nodes use (only the used ones are saved).
        std::map<std::string, NativeSignature> Natives;
        // Sets a native method's signature (e.g. the live one after a C++ change) and drops the literals and wires
        // that no longer fit its pins; returns how many were dropped.
        size_t SetNative(const std::string& key, NativeSignature signature);
        // The native methods the graph's own nodes call.
        std::set<std::string> NativesUsed() const;
        Definition Describe(const Node& node) const;
        // Type check with the named types: an Enum value must be an entry of the named enum, a Struct value must have
        // exactly the struct's fields, each fitting its field.
        bool Fits(Type type, const std::string& enumName, const Value& value) const;
        bool Fits(const Pin& pin, const Value& value) const { return Fits(pin.Kind, pin.Enum, value); }
        // The initial value of a type: the first entry of an enum, the field defaults of a struct.
        Value DefaultFor(Type type, const std::string& named) const;
        bool RenameEnum(const std::string& from, const std::string& to);
        // Renames a struct; every node, variable, signature pin and local that names it follows.
        bool RenameStruct(const std::string& from, const std::string& to);
        // Renames a field: the wires of Make/Break pins named after it, Get/SetField literals and the stored struct
        // values (variable defaults, signature defaults, locals, node literals) follow.
        bool RenameStructField(const std::string& structName, const std::string& from, const std::string& to);
        // How many nodes, variables, signature pins and struct fields name this struct.
        size_t StructUses(const std::string& name) const;
        // The first struct that contains itself (directly or through other structs' fields), as "A -> B -> A";
        // empty when there is none.
        std::string StructCycle() const;
        // Renames a macro: its instances' Name literal and its body nodes follow.
        bool RenameMacro(const std::string& from, const std::string& to);
        // Renames an input (output == false) or output pin of a macro; the wires and literals of its entry/exit and
        // instances follow.
        bool RenameMacroPin(const std::string& macro, bool output, const std::string& from, const std::string& to);
        // How many Macro.Instance nodes use this macro.
        size_t MacroUses(const std::string& name) const;
        // The graph with every Macro.Instance replaced by a copy of its macro's body (nested macros too) and without
        // macro bodies or definitions: what the VM runs. Origins maps every copied node to {the instance in this
        // graph, the body node it was copied from}; Errors holds the instance wiring/definition errors.
        struct Expansion
        {
            std::vector<Node> Nodes;
            std::vector<Link> Links;
            std::map<uint32_t, std::pair<uint32_t, uint32_t>> Origins;
            std::vector<Diagnostic> Errors;
        };
        Expansion Expand() const;
        // After a struct's fields change: every stored value of that struct keeps its fitting fields and takes the
        // field defaults for the others. Returns how many values changed.
        size_t ConformStructValues();
        // Renames an entry; the Switch outputs named after it keep their wires.
        bool RenameEnumEntry(const std::string& enumName, const std::string& from, const std::string& to);
        // How many nodes, variables and signature pins name this enum.
        size_t EnumUses(const std::string& name) const;
        bool RenameVariable(const std::string& from, const std::string& to);
        bool RenameFunction(const std::string& from, const std::string& to);
        // Renames a parameter (result == false) or result of a typed function; the wires and literal arguments of
        // its entry, calls and returns follow the new name.
        bool RenameFunctionPin(const std::string& function, bool result, const std::string& from, const std::string& to);
        size_t PruneInvalidPins();
        Value CopyNodes(const std::set<uint32_t>& selected) const;
        // Pastes a CopyNodes fragment into the graph itself (macro empty) or into a macro's body.
        bool PasteNodes(const Value& fragment, float x, float y, std::set<uint32_t>& inserted, std::string& error, const std::string& macro = {});
        const Node* Find(uint32_t id) const;
        std::vector<Diagnostic> Validate() const;
        std::vector<Diagnostic> ValidateFunctions() const;
        // Non-blocking findings (the graph still loads): e.g. a function with results where some execution
        // paths end without a Return (the VM stops with an error if such a path runs).
        std::vector<Diagnostic> Warnings() const;
        // The nodes of a typed function: its entry, everything its execution reaches, and their data sources.
        std::set<uint32_t> FunctionNodes(const std::string& name) const;
        // Moves the selected nodes into a new typed function and puts a call in their place: data wires that
        // enter the selection become parameters, wires that leave it become results, the one execution wire in
        // becomes the call's In and the one execution wire out its Then. Events, entries/returns, locals and a
        // selected pure node that also feeds unselected nodes are refused. All or nothing; the new call node's ID
        // is returned in call.
        bool CollapseToFunction(const std::set<uint32_t>& selected, const std::string& name, uint32_t& call, std::string& error);
        Value Serialize() const;
        static Graph Parse(const Value& json);
    private:
        Definition DescribeStruct(const Node& node, Definition catalog) const;
        Definition DescribeMacro(const Node& node, Definition catalog) const;
        Definition DescribeNative(const Node& node, Definition catalog) const;
        std::vector<Diagnostic> ValidateNatives() const;
        // The node/link checks of Validate on this graph's nodes as they are (functions: the typed function scope
        // analysis too).
        std::vector<Diagnostic> ValidateNodes(bool functions) const;
        std::vector<Diagnostic> ValidateTypes() const;
        std::vector<Diagnostic> ValidateMacros() const;
        // Validate without the inheritance checks (a graph without a parent, or a resolved one).
        std::vector<Diagnostic> ValidateGraph() const;
        std::vector<Diagnostic> ValidateInheritance() const;
        void LoadParentChain(const std::filesystem::path& assets, std::vector<std::filesystem::path> chain);
        void Record(const char* kind, const std::string& from, const std::string& to);
    public:
        // Loads a graph file and its parent chain (assets: the Assets folder the Parent path is relative to; empty:
        // the nearest folder named Assets above the file, else the file's folder).
        static Graph Load(const std::filesystem::path& path, const std::filesystem::path& assets = {});
        bool Save(const std::filesystem::path& path) const;
        static Graph Example(bool threeD = true);
    };
    // A graph as the VM runs it: validated, the parent chain resolved (Inherited.Origins: the parent file and ID of
    // every inherited node, ParentEvents/OverriddenEvents), macros expanded, one shared pin description per node.
    // Machine::Prepare makes it from a graph; a cooked graph stores it (VisualGraphBytecode.h).
    struct Program
    {
        Graph Runnable;
        std::vector<std::shared_ptr<const Definition>> Definitions; // per node of Runnable.Nodes, in order
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> MacroOrigins; // copied node -> {its instance, body node}
        std::map<uint32_t, std::string> SourceMacros;                 // macro body node -> its macro
    };
// Independent of rendering and C++ compilation. The host supplies whitelisted engine operations.
    class Machine
    {
    public:
        using Bridge = std::function<Value(const std::string&, const std::map<std::string, Value>&)>;
        // Validates the graph (what Load rejects: false, the first error in error, all in diagnostics), resolves
        // its parent chain and expands its macros.
        static bool Prepare(Graph graph, Program& out, std::string& error, std::vector<Diagnostic>* diagnostics = nullptr);
        bool Load(Graph graph, Bridge bridge = {}, const Value& overrides = Value::object());
        // Runs a prepared program (Prepare's, or a cooked graph's: Bytecode::Read) without validating it again.
        bool Load(Program program, Bridge bridge = {}, const Value& overrides = Value::object());
        void Event(const std::string& name, Value payload = Value::object());
        void Tick(double seconds);
        void Stop();
        const std::map<std::string, Value>& Variables() const { return m_Variables; }
        const std::string& Error() const { return m_Error; }
        const std::deque<uint32_t>& Trace() const { return m_Trace; }
        // The native method signatures of the loaded graph (the host binds them to the live C++ methods).
        const std::map<std::string, NativeSignature>& Natives() const { return m_Graph.Natives; }
        // The graph as the VM runs it (parent chain resolved, macros expanded).
        const Graph& Loaded() const { return m_Graph; }
        size_t Pending() const { return m_Tasks.size(); }
        bool Debug = false, Paused = false;
        uint32_t CurrentNode = 0;
        void Step();
        void Continue();
        size_t Budget = 10000;
        // The VM profiler (Profiling, or ProfileAll for every machine): per node of the loaded graph (a node copied
        // from a macro counts on its instance) the calls and the inclusive time (with the data nodes it evaluates)
        // and exclusive time (its own), per typed function the calls and the time of the nodes that ran inside it
        // (inclusive: also inside the functions it called; exclusive: its own nodes), and how often execution went
        // from one node to the next (hot paths). Times in seconds, CPU time of the VM's own work: a Delay's wait does
        // not count.
        struct NodeProfile
        {
            uint64_t Calls = 0;
            double Inclusive = 0, Exclusive = 0;
        };
        struct Profile
        {
            std::map<uint32_t, NodeProfile> Nodes;
            std::map<std::string, NodeProfile> Functions;
            std::map<std::pair<uint32_t, uint32_t>, uint64_t> Edges; // execution from -> to: how often
            uint64_t Runs = 0;
            double Seconds = 0; // inside Run, all tasks
            // Adds another machine's profile (instances of one graph).
            void Merge(const Profile& other);
            // The hottest execution path: from the entry node (an event or a function entry) with the most
            // inclusive time downstream, following the most taken transition each step, without repeats.
            std::vector<uint32_t> HotPath(size_t limit = 32) const;
            // The nodes with the most exclusive time, slowest first.
            std::vector<uint32_t> Hottest(size_t count) const;
        };
        bool Profiling = false;
        static inline bool ProfileAll = false;
        const Profile& GetProfile() const { return m_Profile; }
        void ResetProfile() { m_Profile = {}; }
        static constexpr size_t MaxRetainedBytes = 8 * 1024 * 1024;
        size_t RetainedBytes() const;
        Value DebugFrame() const;
        const std::string& CallStack() const { return m_CallStack; }
        // The node of the loaded graph a running node comes from: a node copied from a macro body reports the
        // macro instance (source: the body node); other nodes report themselves.
        uint32_t Origin(uint32_t id, uint32_t* source = nullptr) const;
    private:
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> m_Origins;
        std::map<uint32_t, std::string> m_SourceMacros; // body node -> its macro
        struct Frame
        {
            uint32_t Node = 0;
            int Iteration = -1, Count = 0;
            bool ReturnCall = false;
        };
        struct Invocation
        {
            std::string Name;
            uint32_t Caller = 0, Entry = 0;
            size_t ReturnDepth = 0;
            std::map<std::string, Value> Arguments, Locals;
            std::map<uint32_t, Value> ParentResults;
            double ProfileTime = 0; // profiler: the time of the steps run inside this call so far
        };
        struct Task
        {
            std::vector<Frame> Stack;
            Value Event;
            std::map<uint32_t, Value> Results;
            double Wait = 0;
            std::vector<Invocation> Calls;
            uint32_t Last = 0; // the last execution node (profiler: transitions)
        };
        Profile m_Profile;
        struct Measure; // a measured node (VisualGraph.cpp)
        // Profiler: the child time of the open measured regions (exclusive = inclusive - children).
        std::vector<double> m_ProfileChildren;
        bool ProfilingNow() const { return Profiling || ProfileAll; }
        // The data nodes being evaluated (cycle and depth check): a small stack, nodes leave it in reverse order.
        struct Active
        {
            std::vector<uint32_t> Nodes;
            size_t size() const { return Nodes.size(); }
            std::pair<size_t, bool> insert(uint32_t id)
            {
                if (std::find(Nodes.begin(), Nodes.end(), id) != Nodes.end()) return { 0, false };
                Nodes.push_back(id);
                return { Nodes.size() - 1, true };
            }
            void erase(uint32_t id)
            {
                if (!Nodes.empty() && Nodes.back() == id) Nodes.pop_back();
                else if (auto it = std::find(Nodes.begin(), Nodes.end(), id); it != Nodes.end()) Nodes.erase(it);
            }
        };
        // One wire into a node's data input / out of its execution output (by the node's index in m_Graph.Nodes).
        struct InputLink
        {
            std::string Input;
            uint32_t From = 0;
            std::string Output;
        };
        struct ExecLink
        {
            std::string Output;
            uint32_t To = 0;
        };
        std::vector<std::vector<InputLink>> m_InputsOf;
        std::vector<std::vector<ExecLink>> m_ExecOf;
        size_t m_VariableBytes = 0; // RetainedBytes of m_Variables, kept up to date by Set
        size_t IndexOf(const Node& node) const { return size_t(&node - m_Graph.Nodes.data()); }
        Graph m_Graph;
        std::vector<std::shared_ptr<const Definition>> m_Definitions; // per node index
        std::map<std::string, uint32_t> m_TypedEntries;
        Task* m_Active = nullptr;
        std::string m_CallStack;
        std::unordered_map<uint32_t, size_t> m_NodeIndex;
        std::map<std::string, uint32_t> m_Functions;
        std::map<std::string, std::vector<uint32_t>> m_Events;
        std::map<uint32_t, std::pair<std::string, uint32_t>> m_Inherited; // resolved node -> parent file, node
        const Node* Find(uint32_t id) const;
        Bridge m_Bridge;
        std::map<std::string, Value> m_Variables;
        std::deque<Task> m_Tasks;
        std::deque<uint32_t> m_Trace;
        std::string m_Error;
        size_t m_Remaining = 0;
        bool m_Running = false, m_SkipBreakpoint = false;
        Value Input(const Node&, std::string_view, Task&, Active&);
        Value Evaluate(uint32_t, const std::string&, Task&, Active&);
        void Push(Task&, uint32_t, const std::string&);
        void Run(bool single = false);
        const Definition& Def(const Node& node) const;
        void Charge();
        void CheckStorage() const;
        void ReturnFunction(Task& task, Value values);
        void TypedCall(Task& task, const Node& node);
        Value CallNative(const Node& node, Task& task, Active& active);
        void CaptureCallStack(const Task& task);
    };
}
