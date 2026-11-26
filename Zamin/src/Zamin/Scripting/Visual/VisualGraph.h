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
        ArrayBool,
        ArrayNumber,
        ArrayString,
        ArrayVec2,
        ArrayVec3,
        ArrayVec4,
        ArrayEntity,
        Count
    };
    struct Pin
    {
        std::string Name;
        Type Kind;
        Value Default;
        std::string Enum; // the enum's name when Kind == Type::Enum
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
        std::string Enum; // the enum's name when Kind == Type::Enum
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
    struct Diagnostic
    {
        uint32_t Node = 0;
        std::string Message;
    };
    const std::map<std::string, Definition>& Catalog();
    // Named values of an enumerated Number input (the dispatch validates it as an integer 0..N-1): the editor shows
    // a combo of these labels (Uzbek source texts, translated with the editor's Tr) and stores the index. The pin
    // and the file format stay plain numbers. Null for any other pin.
    const std::vector<const char*>* PinChoices(const std::string& kind, const std::string& pin);
    const char* TypeName(Type type);
    bool IsArray(Type type);
    Type ElementType(Type type);
    Value DefaultValue(Type type);
    size_t ValueBytes(const Value& value);
    bool Matches(Type type, const Value& value);
    // Same pin type: the kind, and for an Enum pin the same enum.
    bool SameType(const Pin& a, const Pin& b);
    struct FunctionSignature
    {
        std::vector<Pin> Inputs, Outputs;
        std::map<std::string, Variable> Locals;
    };
    struct Graph
    {
        // Version 2 added enums (Type::Enum, "Enums", the "Enum" key of pins/variables). Version 1 files load
        // unchanged (they cannot hold enums) and are written back as version 2.
        static constexpr int FormatVersion = 2;
        int SourceVersion = FormatVersion; // the version the graph was parsed from
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        std::map<std::string, Variable> Variables;
        std::map<std::string, FunctionSignature> Functions;
        std::map<std::string, EnumDefinition> Enums;
        Definition Describe(const Node& node) const;
        // Type check with enum membership: an Enum value must be an entry of the named enum.
        bool Fits(Type type, const std::string& enumName, const Value& value) const;
        bool Fits(const Pin& pin, const Value& value) const { return Fits(pin.Kind, pin.Enum, value); }
        bool RenameEnum(const std::string& from, const std::string& to);
        // How many nodes, variables and signature pins name this enum.
        size_t EnumUses(const std::string& name) const;
        bool RenameVariable(const std::string& from, const std::string& to);
        bool RenameFunction(const std::string& from, const std::string& to);
        size_t PruneInvalidPins();
        Value CopyNodes(const std::set<uint32_t>& selected) const;
        bool PasteNodes(const Value& fragment, float x, float y, std::set<uint32_t>& inserted, std::string& error);
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
    private:
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
