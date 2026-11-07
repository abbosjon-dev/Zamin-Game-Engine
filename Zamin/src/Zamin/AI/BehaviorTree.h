#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Behavior trees for AI (the other helper: StateMachine.h): a tree of nodes ticked every frame from
// its root. Each tick a node answers Running (not done yet: ticked again next frame), Success or
// Failure; composites decide which children run from their answers:
//
//   Sequence  - its children one after another; fails at the first that fails.
//   Selector  - the first child that does not fail (a fallback: try A, else B, else C).
//   Parallel  - all its children every tick; succeeds / fails once enough of them have.
//   Inverter / Succeeder / Failer / Repeat - decorators of one child.
//   Wait / Condition / Action - leaves (the script's work: lambdas).
//
// Sequence / Selector remember the running child and go on from it next tick; the Reactive ones
// start from their first child every tick (a guard's "do I see the hero?" condition interrupts the
// patrol below it: the running branch is halted). A branch "condition, then a long action" that must
// stop when the condition no longer holds is a ReactiveSequence: a plain Sequence would go on with
// its running action without asking again. Nodes share a Blackboard - named values.
//
//   m_Tree.Name = "Qorovul";
//   BehaviorTreeBuilder()
//       .ReactiveSelector("Ildiz")
//           .ReactiveSequence("Ta'qib")
//               .Condition("Ko'ryapti", [this] { return CanSeeTarget(); })
//               .Action("Yugurish", [this] { SetDestination(TargetPosition()); return BTStatus::Running; })
//           .End()
//           .Sequence("Patrul")
//               .Action("Keyingi nuqta", [this] { return GoToNextPoint(); })   // bool: Success / Failure
//               .Wait(2.0f)
//           .End()
//       .End()
//       .Build(m_Tree);
//   m_Tree.Tick(ts);            // OnUpdate
//   WatchBehaviorTree(m_Tree);  // the inspector shows the tree, what runs and the blackboard
//
// A finished root starts again at the next Tick. Not thread safe.
namespace Zamin {

    // Invalid: not ticked yet (or halted).
    enum class BTStatus : uint8_t { Invalid = 0, Running, Success, Failure };
    const char* BTStatusName(BTStatus status); // "Invalid", "Running", "Success", "Failure"

    // Named values the nodes of a tree (and its script) share: bool, int, float, text, vec2, vec3 or
    // an id (uint64_t: an entity's UUID).
    //   board.Set("Alarm", true);  board.Set("LastSeen", position);
    //   if (board.Get<bool>("Alarm")) ...;  glm::vec3 p = board.Get("LastSeen", glm::vec3(0.0f));
    class Blackboard
    {
    public:
        using Value = std::variant<bool, int, float, std::string, glm::vec2, glm::vec3, uint64_t>;

        template<typename T>
        void Set(std::string_view key, const T& value)
        {
            if constexpr (std::is_same_v<T, bool>)
                Store(key, Value(value));
            else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) >= 8)
                Store(key, Value((uint64_t)value));
            else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
                Store(key, Value((int)value));
            else if constexpr (std::is_floating_point_v<T>)
                Store(key, Value((float)value));
            else if constexpr (std::is_convertible_v<const T&, std::string_view>)
                Store(key, Value(std::string(std::string_view(value))));
            else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3>)
                Store(key, Value(value));
            else if constexpr (std::is_convertible_v<const T&, uint64_t>)
                Store(key, Value((uint64_t)value)); // UUID
            else
                static_assert(std::is_same_v<T, bool>, "Blackboard: bool, int, float, text, vec2, vec3 or uint64_t");
        }

        // The value, `fallback` when there is none or it is of another kind (numbers convert: an
        // int read as a float, a float as an int, any number as a bool).
        template<typename T>
        T Get(std::string_view key, const T& fallback = T{}) const
        {
            const Value* v = FindValue(key);
            if (!v)
                return fallback;
            if constexpr (std::is_same_v<T, bool> || std::is_arithmetic_v<T> || std::is_enum_v<T>)
            {
                // Whole numbers exactly (ids do not fit a double).
                if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
                {
                    if (const auto* id = std::get_if<uint64_t>(v))
                        return (T)*id;
                    if (const auto* i = std::get_if<int>(v))
                        return (T)*i;
                }
                double number = 0.0;
                if (!ToNumber(*v, number))
                    return fallback;
                if constexpr (std::is_same_v<T, bool>)
                    return number != 0.0;
                else
                    return (T)number;
            }
            else if constexpr (std::is_same_v<T, std::string>)
                return std::holds_alternative<std::string>(*v) ? std::get<std::string>(*v) : fallback;
            else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3>)
                return std::holds_alternative<T>(*v) ? std::get<T>(*v) : fallback;
            else
                static_assert(std::is_same_v<T, bool>, "Blackboard: bool, int, float, text, vec2, vec3 or uint64_t");
        }

        bool Has(std::string_view key) const { return FindValue(key) != nullptr; }
        // Set and not false / 0 / empty text (what CheckBlackboard asks).
        bool IsTrue(std::string_view key) const;
        bool Erase(std::string_view key);
        void Clear();
        size_t Size() const { return m_Values.size(); }
        bool Empty() const { return m_Values.empty(); }
        const Value* FindValue(std::string_view key) const;
        // Keys in alphabetical order.
        std::vector<std::string> GetKeys() const;
        // The value as text ("true", "3", "1.50", "(1.0, 2.0, 0.0)"); "" when there is none.
        std::string Describe(std::string_view key) const;
        // Changes with every Set / Erase / Clear.
        uint32_t GetRevision() const { return m_Revision; }

    private:
        void Store(std::string_view key, Value value);
        static bool ToNumber(const Value& v, double& out);

        std::map<std::string, Value, std::less<>> m_Values;
        uint32_t m_Revision = 0;
    };

    class BehaviorTree;

    // What a node gets when it is ticked.
    struct BTContext
    {
        float DeltaTime = 0.0f;
        Blackboard& Board;
        BehaviorTree& Tree;
    };

    // A node of a tree. Own leaves derive from it (override OnTick, maybe OnStart / OnHalt) and go
    // in with BehaviorTreeBuilder::Node.
    class BTNode
    {
    public:
        explicit BTNode(std::string name) : m_Name(std::move(name)) {}
        virtual ~BTNode() = default;
        BTNode(const BTNode&) = delete;
        BTNode& operator=(const BTNode&) = delete;

        // One step: OnStart first when it is not running (started anew), then OnTick. The answer
        // stays readable (GetStatus); a node that finished starts again at its next tick.
        BTStatus Tick(BTContext& context);
        // A running node stops (OnHalt; composites halt their running children): its status becomes
        // Invalid.
        void Halt();

        const std::string& GetName() const { return m_Name; }
        BTStatus GetStatus() const { return m_Status; }
        bool IsRunning() const { return m_Status == BTStatus::Running; }
        // "Sequence", "Wait", "Action" ...
        virtual const char* GetTypeName() const = 0;
        virtual size_t GetChildCount() const { return 0; }
        virtual BTNode* GetChild(size_t /*index*/) const { return nullptr; }
        // The tree's tick (BehaviorTree::GetTickIndex) this node was last ticked in (0: never).
        uint32_t GetLastTick() const { return m_LastTick; }
        // Times it was ticked / started anew.
        uint32_t GetTickCount() const { return m_TickCount; }
        uint32_t GetStartCount() const { return m_StartCount; }
        // A short note for the inspector ("2/3", "1.2 / 3.0 s"); empty for most nodes.
        virtual std::string GetDetail() const { return {}; }

    protected:
        virtual void OnStart(BTContext& /*context*/) {}
        virtual BTStatus OnTick(BTContext& context) = 0;
        virtual void OnHalt() {}

    private:
        std::string m_Name;
        BTStatus m_Status = BTStatus::Invalid;
        uint32_t m_LastTick = 0;
        uint32_t m_TickCount = 0;
        uint32_t m_StartCount = 0;
    };

    // ---- composites
    class BTComposite : public BTNode
    {
    public:
        using BTNode::BTNode;
        void AddChild(std::unique_ptr<BTNode> child);
        size_t GetChildCount() const override { return m_Children.size(); }
        BTNode* GetChild(size_t index) const override { return index < m_Children.size() ? m_Children[index].get() : nullptr; }

    protected:
        void OnHalt() override { HaltChildren(0); }
        // Halts the running children from `first` on.
        void HaltChildren(size_t first);
        std::vector<std::unique_ptr<BTNode>> m_Children;
    };

    // Its children in order while they succeed: Success when all did, Failure at the first failure,
    // Running while one runs (with memory: it goes on from that one next tick; reactive: from the
    // first child again - a condition before it that fails now halts it).
    class BTSequence : public BTComposite
    {
    public:
        BTSequence(std::string name, bool reactive = false) : BTComposite(std::move(name)), m_Reactive(reactive) {}
        const char* GetTypeName() const override { return m_Reactive ? "ReactiveSequence" : "Sequence"; }
        bool IsReactive() const { return m_Reactive; }

    protected:
        void OnStart(BTContext&) override { m_Index = 0; }
        BTStatus OnTick(BTContext& context) override;

    private:
        bool m_Reactive;
        size_t m_Index = 0;
    };

    // Its children in order until one does not fail: Success at the first success, Failure when all
    // failed, Running while one runs (memory / reactive as BTSequence).
    class BTSelector : public BTComposite
    {
    public:
        BTSelector(std::string name, bool reactive = false) : BTComposite(std::move(name)), m_Reactive(reactive) {}
        const char* GetTypeName() const override { return m_Reactive ? "ReactiveSelector" : "Selector"; }
        bool IsReactive() const { return m_Reactive; }

    protected:
        void OnStart(BTContext&) override { m_Index = 0; }
        BTStatus OnTick(BTContext& context) override;

    private:
        bool m_Reactive;
        size_t m_Index = 0;
    };

    // Every unfinished child each tick: Success once `successThreshold` children succeeded (< 1: all
    // of them), Failure once `failureThreshold` failed (< 1: all) - the ones still running are then
    // halted; Running until then. Both thresholds reached in one tick: Failure.
    class BTParallel : public BTComposite
    {
    public:
        BTParallel(std::string name, int successThreshold = -1, int failureThreshold = 1)
            : BTComposite(std::move(name)), m_SuccessThreshold(successThreshold), m_FailureThreshold(failureThreshold) {}
        const char* GetTypeName() const override { return "Parallel"; }
        std::string GetDetail() const override;

    protected:
        void OnStart(BTContext&) override;
        BTStatus OnTick(BTContext& context) override;

    private:
        int m_SuccessThreshold, m_FailureThreshold;
        std::vector<BTStatus> m_Results; // per child this run (Invalid: not finished)
        int m_Successes = 0, m_Failures = 0;
    };

    // ---- decorators (one child; without one: Failure)
    class BTDecorator : public BTNode
    {
    public:
        using BTNode::BTNode;
        void SetChild(std::unique_ptr<BTNode> child) { m_Child = std::move(child); }
        size_t GetChildCount() const override { return m_Child ? 1 : 0; }
        BTNode* GetChild(size_t index) const override { return index == 0 ? m_Child.get() : nullptr; }

    protected:
        void OnHalt() override
        {
            if (m_Child)
                m_Child->Halt();
        }
        std::unique_ptr<BTNode> m_Child;
    };

    // Success <-> Failure (Running stays).
    class BTInverter : public BTDecorator
    {
    public:
        using BTDecorator::BTDecorator;
        const char* GetTypeName() const override { return "Inverter"; }

    protected:
        BTStatus OnTick(BTContext& context) override;
    };

    // Success when the child finishes, whatever it answered (Running stays).
    class BTSucceeder : public BTDecorator
    {
    public:
        using BTDecorator::BTDecorator;
        const char* GetTypeName() const override { return "Succeeder"; }

    protected:
        BTStatus OnTick(BTContext& context) override;
    };

    // Failure when the child finishes (Running stays).
    class BTFailer : public BTDecorator
    {
    public:
        using BTDecorator::BTDecorator;
        const char* GetTypeName() const override { return "Failer"; }

    protected:
        BTStatus OnTick(BTContext& context) override;
    };

    // The child again and again while it succeeds: Success after `count` successes (< 1: never -
    // forever), Failure when it fails. At most one run of the child finishes per tick (a child that
    // succeeds at once does not loop within one frame).
    class BTRepeat : public BTDecorator
    {
    public:
        BTRepeat(std::string name, int count) : BTDecorator(std::move(name)), m_Count(count) {}
        const char* GetTypeName() const override { return "Repeat"; }
        std::string GetDetail() const override;
        int GetDone() const { return m_Done; }

    protected:
        void OnStart(BTContext&) override { m_Done = 0; }
        BTStatus OnTick(BTContext& context) override;

    private:
        int m_Count;
        int m_Done = 0;
    };

    // ---- leaves
    // Running for `seconds` (the tree's delta times summed), then Success.
    class BTWait : public BTNode
    {
    public:
        BTWait(std::string name, float seconds) : BTNode(std::move(name)), m_Seconds(seconds) {}
        const char* GetTypeName() const override { return "Wait"; }
        std::string GetDetail() const override;
        float GetElapsed() const { return m_Elapsed; }

    protected:
        void OnStart(BTContext&) override { m_Elapsed = 0.0f; }
        BTStatus OnTick(BTContext& context) override;

    private:
        float m_Seconds;
        float m_Elapsed = 0.0f;
    };

    // Success when the check holds, else Failure (never Running).
    class BTCondition : public BTNode
    {
    public:
        using Check = std::function<bool(BTContext&)>;
        BTCondition(std::string name, Check check) : BTNode(std::move(name)), m_Check(std::move(check)) {}
        const char* GetTypeName() const override { return "Condition"; }

    protected:
        BTStatus OnTick(BTContext& context) override;

    private:
        Check m_Check;
    };

    // The script's work: `tick` answers each tick (Running while it is not done); `onHalt` runs when
    // the action is stopped while running (a reactive branch above it took over).
    class BTAction : public BTNode
    {
    public:
        using Tick = std::function<BTStatus(BTContext&)>;
        using Halted = std::function<void()>;
        BTAction(std::string name, Tick tick, Halted onHalt = {}) : BTNode(std::move(name)), m_Tick(std::move(tick)), m_OnHalt(std::move(onHalt)) {}
        const char* GetTypeName() const override { return "Action"; }

    protected:
        BTStatus OnTick(BTContext& context) override;
        void OnHalt() override
        {
            if (m_OnHalt)
                m_OnHalt();
        }

    private:
        Tick m_Tick;
        Halted m_OnHalt;
    };

    // ---- the tree
    class BehaviorTree
    {
    public:
        BehaviorTree() = default;
        explicit BehaviorTree(std::string name) : Name(std::move(name)) {}
        BehaviorTree(const BehaviorTree&) = delete;
        BehaviorTree& operator=(const BehaviorTree&) = delete;

        // A new root (the old one is halted first).
        void SetRoot(std::unique_ptr<BTNode> root);
        BTNode* GetRoot() const { return m_Root.get(); }
        bool HasRoot() const { return (bool)m_Root; }

        // Ticks the root once (no root: Failure). A finished root starts again at the next Tick.
        BTStatus Tick(float dt);
        // Stops what runs (OnHalt of the running nodes); the next Tick starts from the root again.
        void Halt();
        // Halt, and the counters from zero (the blackboard is kept: Blackboard().Clear() for that).
        void Reset();

        Blackboard& GetBlackboard() { return m_Blackboard; }
        const Blackboard& GetBlackboard() const { return m_Blackboard; }
        BTStatus GetStatus() const { return m_Root ? m_Root->GetStatus() : BTStatus::Invalid; }
        // Ticks so far (the nodes' GetLastTick compares with it: ticked in the last tick or not).
        uint32_t GetTickIndex() const { return m_TickIndex; }
        float GetRunningTime() const { return m_Time; }
        size_t GetNodeCount() const;
        // The branch the last tick ended in, from the root: "Ildiz > Patrul > Wait". Empty before
        // the first tick.
        std::string GetActivePath() const;
        // "Patrul > Wait (Running)" - or "(to'xtagan)" before the first tick / after Halt.
        std::string Describe() const;
        // Every node depth first, with its depth (the root 0).
        void ForEachNode(const std::function<void(const BTNode& node, int depth)>& fn) const;
        // Each change of the active branch to the log (ZM_INFO).
        bool LogChanges = false;

        // Shown with the tree in the inspector and the log.
        std::string Name;

    private:
        std::unique_ptr<BTNode> m_Root;
        Blackboard m_Blackboard;
        uint32_t m_TickIndex = 0;
        float m_Time = 0.0f;
        std::string m_LastPath;
    };

    namespace Detail {

        // A check as BTCondition wants it, from bool(), bool(Blackboard&) or bool(BTContext&).
        template<typename F>
        BTCondition::Check WrapCondition(F&& f)
        {
            using Fn = std::decay_t<F>;
            return [fn = Fn(std::forward<F>(f))](BTContext& c) mutable -> bool {
                if constexpr (std::is_invocable_v<Fn&, BTContext&>)
                    return (bool)fn(c);
                else if constexpr (std::is_invocable_v<Fn&, Blackboard&>)
                    return (bool)fn(c.Board);
                else
                {
                    static_assert(std::is_invocable_v<Fn&>, "Condition: bool(), bool(Blackboard&) or bool(BTContext&)");
                    return (bool)fn();
                }
            };
        }

        // An action's tick from a callable taking nothing, the delta time, the blackboard or the
        // context, answering BTStatus, bool (Success / Failure) or nothing (Success).
        template<typename F>
        BTAction::Tick WrapAction(F&& f)
        {
            using Fn = std::decay_t<F>;
            return [fn = Fn(std::forward<F>(f))](BTContext& c) mutable -> BTStatus {
                auto call = [&]() -> decltype(auto) {
                    if constexpr (std::is_invocable_v<Fn&, BTContext&>)
                        return fn(c);
                    else if constexpr (std::is_invocable_v<Fn&, Blackboard&>)
                        return fn(c.Board);
                    else if constexpr (std::is_invocable_v<Fn&, float>)
                        return fn(c.DeltaTime);
                    else
                    {
                        static_assert(std::is_invocable_v<Fn&>, "Action: (), (float dt), (Blackboard&) or (BTContext&)");
                        return fn();
                    }
                };
                using R = decltype(call());
                if constexpr (std::is_void_v<R>)
                {
                    call();
                    return BTStatus::Success;
                }
                else if constexpr (std::is_same_v<std::decay_t<R>, BTStatus>)
                    return call();
                else
                    return call() ? BTStatus::Success : BTStatus::Failure;
            };
        }

    }

    // Builds a tree top down: a composite or decorator opens (its children follow), End() closes the
    // last open composite; a decorator closes by itself once it has its one child. Mistakes (a
    // second root, an End too many, an empty decorator) are logged and left out.
    class BehaviorTreeBuilder
    {
    public:
        BehaviorTreeBuilder& Sequence(std::string name = "Sequence") { return Open(std::make_unique<BTSequence>(std::move(name), false)); }
        BehaviorTreeBuilder& ReactiveSequence(std::string name = "ReactiveSequence") { return Open(std::make_unique<BTSequence>(std::move(name), true)); }
        BehaviorTreeBuilder& Selector(std::string name = "Selector") { return Open(std::make_unique<BTSelector>(std::move(name), false)); }
        BehaviorTreeBuilder& ReactiveSelector(std::string name = "ReactiveSelector") { return Open(std::make_unique<BTSelector>(std::move(name), true)); }
        BehaviorTreeBuilder& Parallel(std::string name = "Parallel", int successThreshold = -1, int failureThreshold = 1)
        {
            return Open(std::make_unique<BTParallel>(std::move(name), successThreshold, failureThreshold));
        }
        BehaviorTreeBuilder& Inverter(std::string name = "Inverter") { return Open(std::make_unique<BTInverter>(std::move(name))); }
        BehaviorTreeBuilder& Succeeder(std::string name = "Succeeder") { return Open(std::make_unique<BTSucceeder>(std::move(name))); }
        BehaviorTreeBuilder& Failer(std::string name = "Failer") { return Open(std::make_unique<BTFailer>(std::move(name))); }
        // count < 1: forever.
        BehaviorTreeBuilder& Repeat(int count, std::string name = "Repeat") { return Open(std::make_unique<BTRepeat>(std::move(name), count)); }

        BehaviorTreeBuilder& Wait(float seconds, std::string name = "Wait") { return Leaf(std::make_unique<BTWait>(std::move(name), seconds)); }
        template<typename F>
        BehaviorTreeBuilder& Condition(std::string name, F&& check)
        {
            return Leaf(std::make_unique<BTCondition>(std::move(name), Detail::WrapCondition(std::forward<F>(check))));
        }
        template<typename F>
        BehaviorTreeBuilder& Action(std::string name, F&& tick, BTAction::Halted onHalt = {})
        {
            return Leaf(std::make_unique<BTAction>(std::move(name), Detail::WrapAction(std::forward<F>(tick)), std::move(onHalt)));
        }
        // Success when the blackboard's value is set and true (not false / 0 / empty text).
        BehaviorTreeBuilder& CheckBlackboard(std::string key, std::string name = {});
        // Sets the blackboard's value, Success.
        template<typename T>
        BehaviorTreeBuilder& SetBlackboard(std::string key, T value, std::string name = {})
        {
            std::string label = name.empty() ? "= " + key : std::move(name);
            return Leaf(std::make_unique<BTAction>(std::move(label), [key = std::move(key), value = std::move(value)](BTContext& c) {
                c.Board.Set(key, value);
                return BTStatus::Success;
            }));
        }
        // An own node (a leaf class, or a subtree built elsewhere).
        BehaviorTreeBuilder& Node(std::unique_ptr<BTNode> node) { return Leaf(std::move(node)); }

        // Closes the last open composite (and the decorators around it).
        BehaviorTreeBuilder& End();

        // The root (what is still open is closed); empty when nothing was added. The builder is
        // empty afterwards.
        std::unique_ptr<BTNode> Build();
        // The same into the tree (SetRoot). False when nothing was built.
        bool Build(BehaviorTree& tree);

        // Mistakes so far (each also logged).
        int GetErrorCount() const { return m_Errors; }

    private:
        BehaviorTreeBuilder& Open(std::unique_ptr<BTNode> node);
        BehaviorTreeBuilder& Leaf(std::unique_ptr<BTNode> node);
        // Adds the node under the open one (or as the root); false when there is no place for it.
        bool Attach(std::unique_ptr<BTNode>& node, BTNode*& placed);
        void CloseFullDecorators();
        void Error(std::string_view what);

        std::unique_ptr<BTNode> m_Root;
        std::vector<BTNode*> m_Open; // composites / decorators still taking children
        int m_Errors = 0;
    };

}
