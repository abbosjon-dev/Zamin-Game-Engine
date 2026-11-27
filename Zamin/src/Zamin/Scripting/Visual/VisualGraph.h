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
    struct Diagnostic
    {
        uint32_t Node = 0;
        std::string Message;
        // For a finding inside an expanded macro: the body node of the macro it comes from (Node is the macro
        // instance in the graph). 0 otherwise.
        uint32_t Source = 0;
    };
    const std::map<std::string, Definition>& Catalog();
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
    struct Graph
    {
        // Version 2 added enums (Type::Enum, "Enums", the "Enum" key of pins/variables); version 3 added structs
        // (Type::Struct/ArrayStruct, "Structs", the "Struct" key); version 4 added inline macros ("Macros", the
        // "Macro" key of body nodes), Type::ArrayEnum and struct fields of struct type. Version 1-3 files load
        // unchanged (they cannot hold the newer data) and are written back as version 4.
        static constexpr int FormatVersion = 4;
        int SourceVersion = FormatVersion; // the version the graph was parsed from
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        std::map<std::string, Variable> Variables;
        std::map<std::string, FunctionSignature> Functions;
        std::map<std::string, EnumDefinition> Enums;
        std::map<std::string, StructDefinition> Structs;
        std::map<std::string, MacroDefinition> Macros;
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
        // The node/link checks of Validate on this graph's nodes as they are (functions: the typed function scope
        // analysis too).
        std::vector<Diagnostic> ValidateNodes(bool functions) const;
        std::vector<Diagnostic> ValidateTypes() const;
        std::vector<Diagnostic> ValidateMacros() const;
    public:
        static Graph Load(const std::filesystem::path& path);
        bool Save(const std::filesystem::path& path) const;
        static Graph Example(bool threeD = true);
    };
// Independent of rendering and C++ compilation. The host supplies whitelisted engine operations.
    class Machine
    {
    public:
        using Bridge = std::function<Value(const std::string&, const std::map<std::string, Value>&)>;
        bool Load(Graph graph, Bridge bridge = {}, const Value& overrides = Value::object());
        void Event(const std::string& name, Value payload = Value::object());
        void Tick(double seconds);
        void Stop();
        const std::map<std::string, Value>& Variables() const { return m_Variables; }
        const std::string& Error() const { return m_Error; }
        const std::deque<uint32_t>& Trace() const { return m_Trace; }
        size_t Pending() const { return m_Tasks.size(); }
        bool Debug = false, Paused = false;
        uint32_t CurrentNode = 0;
        void Step();
        void Continue();
        size_t Budget = 10000;
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
        };
        struct Task
        {
            std::vector<Frame> Stack;
            Value Event;
            std::map<uint32_t, Value> Results;
            double Wait = 0;
            std::vector<Invocation> Calls;
        };
        Graph m_Graph;
        std::map<uint32_t, std::shared_ptr<const Definition>> m_Definitions;
        std::map<std::string, uint32_t> m_TypedEntries;
        Task* m_Active = nullptr;
        std::string m_CallStack;
        std::map<uint32_t, size_t> m_NodeIndex;
        std::map<std::pair<uint32_t, std::string>, Link> m_InputLinks, m_OutputLinks;
        std::map<std::string, uint32_t> m_Functions;
        std::map<std::string, std::vector<uint32_t>> m_Events;
        const Node* Find(uint32_t id) const;
        Bridge m_Bridge;
        std::map<std::string, Value> m_Variables;
        std::deque<Task> m_Tasks;
        std::deque<uint32_t> m_Trace;
        std::string m_Error;
        size_t m_Remaining = 0;
        bool m_Running = false, m_SkipBreakpoint = false;
        Value Input(const Node&, const std::string&, Task&, std::set<uint32_t>&);
        Value Evaluate(uint32_t, const std::string&, Task&, std::set<uint32_t>&);
        void Push(Task&, uint32_t, const std::string&);
        void Run(bool single = false);
        const Definition& Def(const Node& node) const;
        void Charge();
        void CheckStorage() const;
        void ReturnFunction(Task& task, Value values);
        void TypedCall(Task& task, const Node& node);
        void CaptureCallStack(const Task& task);
    };
}
