#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <set>

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
        Entity
    };
    struct Pin
    {
        std::string Name;
        Type Kind;
        Value Default;
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
    };
    struct Diagnostic
    {
        uint32_t Node = 0;
        std::string Message;
    };
    const std::map<std::string, Definition>& Catalog();
    const char* TypeName(Type type);
    bool Matches(Type type, const Value& value);
    struct Graph
    {
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        std::map<std::string, Variable> Variables;
        const Node* Find(uint32_t id) const;
        std::vector<Diagnostic> Validate() const;
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
    private:
        struct Frame
        {
            uint32_t Node = 0;
            int Iteration = -1, Count = 0;
        };
        struct Task
        {
            std::vector<Frame> Stack;
            Value Event;
            std::map<uint32_t, Value> Results;
            double Wait = 0;
        };
        Graph m_Graph;
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
        void Charge();
    };
}
