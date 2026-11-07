#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Zamin {

    class StateMachine;

    // A state written as a class (the other way: AddState with lambdas). Override what it needs;
    // GetMachine() is set when it is added.
    class AIState
    {
    public:
        virtual ~AIState() = default;
        virtual void OnEnter() {}
        virtual void OnUpdate(float /*dt*/) {}
        virtual void OnExit() {}

        StateMachine* GetMachine() const { return m_Machine; }
        // Seconds since this state was entered (0 when it is not the current one).
        float GetTimeInState() const;
        const std::string& GetName() const { return m_Name; }

    private:
        friend class StateMachine;
        StateMachine* m_Machine = nullptr;
        std::string m_Name;
    };

    // A finite state machine for AI (and anything else with modes): named states with OnEnter /
    // OnUpdate / OnExit, transitions with conditions. A script keeps one as a member:
    //
    //   m_AI.AddState("Patrol", [this] { GoToNextWaypoint(); }, [this](float) { if (HasArrived()) GoToNextWaypoint(); })
    //       .AddState("Chase", {}, [this](float) { SetDestination(TargetPosition()); })
    //       .AddState("Search", [this] { SetDestination(m_LastSeen); })
    //       .AddTransition("Patrol", "Chase", [this] { return CanSeeTarget(); })
    //       .AddTransition("Chase", "Search", [this] { return !CanSeeTarget(); })
    //       .AddTransition("Search", "Chase", [this] { return CanSeeTarget(); })
    //       .AddTimedTransition("Search", "Patrol", 4.0f);
    //   m_AI.Start("Patrol");    // OnStart
    //   m_AI.Update(ts);         // OnUpdate
    //   WatchStateMachine(m_AI); // the inspector shows the state while playing
    //
    // Update first takes the first transition whose condition holds (the "any state" ones first,
    // then the current state's, each in the order added; one transition per Update), then runs the
    // current state's OnUpdate. ChangeState / SendEvent switch at once; called from inside a state's
    // callback, the switch waits until that callback returns. Not thread safe.
    class StateMachine
    {
    public:
        using Action = std::function<void()>;
        using UpdateAction = std::function<void(float)>;
        using Condition = std::function<bool()>;

        // One step of the history (debugging): seconds since Start, and why ("condition", "time",
        // "event: <name>", "ChangeState", "Start").
        struct TransitionRecord
        {
            std::string From, To, Reason;
            float Time = 0.0f;
        };
        static constexpr size_t MaxHistory = 16;

        StateMachine() = default;
        explicit StateMachine(std::string name) : Name(std::move(name)) {}
        StateMachine(const StateMachine&) = delete;
        StateMachine& operator=(const StateMachine&) = delete;

        // ---- building (a name used again replaces that state)
        StateMachine& AddState(std::string name, Action onEnter = {}, UpdateAction onUpdate = {}, Action onExit = {});
        StateMachine& AddState(std::string name, std::unique_ptr<AIState> state);
        // A state object made here (the reference stays valid as long as the machine).
        template<typename T, typename... Args>
        T& AddState(std::string name, Args&&... args)
        {
            static_assert(std::is_base_of_v<AIState, T>, "T must derive from AIState");
            auto state = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *state;
            AddState(std::move(name), std::unique_ptr<AIState>(std::move(state)));
            return ref;
        }
        StateMachine& AddTransition(std::string from, std::string to, Condition condition);
        // From whichever state it is in (not from `to` itself unless allowSelf: it would enter it again).
        StateMachine& AddAnyTransition(std::string to, Condition condition, bool allowSelf = false);
        // After `seconds` in `from`.
        StateMachine& AddTimedTransition(std::string from, std::string to, float seconds);
        // When SendEvent(event) comes while it is in `from` ("" - in any state).
        StateMachine& AddEventTransition(std::string from, std::string to, std::string event);

        // ---- running
        // Enters `initial` (empty: the first state added). False when there is no such state.
        bool Start(std::string_view initial = {});
        // Leaves the current state (OnExit); Start again to go on.
        void Stop();
        void Update(float dt);
        // To `name` now (OnExit of the current, OnEnter of the new one - also when it is the same
        // state). False for an unknown state.
        bool ChangeState(std::string_view name);
        // The event's transition from the current state (else an "any state" one), taken now. False
        // when none fits.
        bool SendEvent(std::string_view event);

        // ---- state
        bool IsRunning() const { return m_Current >= 0; }
        bool HasState(std::string_view name) const { return Find(name) >= 0; }
        bool IsInState(std::string_view name) const;
        const std::string& GetCurrentStateName() const;  // "" while not running
        const std::string& GetPreviousStateName() const; // "" before the first switch
        float GetTimeInState() const { return m_TimeInState; }
        float GetRunningTime() const { return m_Time; }
        uint32_t GetTransitionCount() const { return m_TransitionCount; }
        size_t GetStateCount() const { return m_States.size(); }
        std::vector<std::string> GetStateNames() const;
        AIState* GetStateObject(std::string_view name) const;
        const std::deque<TransitionRecord>& GetHistory() const { return m_History; }
        // "Patrol (2.4 s)" - or "(to'xtagan)" while it is not running.
        std::string Describe() const;

        // Shown with the state in the inspector and the log.
        std::string Name;
        // Each switch to the log (ZM_INFO).
        bool LogTransitions = false;

    private:
        struct State
        {
            std::string Name;
            Action OnEnter, OnExit;
            UpdateAction OnUpdate;
            std::unique_ptr<AIState> Object;
        };
        enum class TransitionKind : uint8_t { Condition, Timed, Event };
        struct Transition
        {
            std::string From, To; // From empty: any state
            TransitionKind Kind = TransitionKind::Condition;
            Condition Check;
            float Seconds = 0.0f;
            std::string Event;
            bool AllowSelf = false;
        };

        int Find(std::string_view name) const;
        bool Applies(const Transition& t) const;
        void Switch(int to, const char* reason);
        void Enter(int state);
        void Exit(int state);

        std::vector<State> m_States;
        std::vector<Transition> m_TransitionList;
        int m_Current = -1;
        int m_Previous = -1;
        float m_TimeInState = 0.0f;
        float m_Time = 0.0f;
        uint32_t m_TransitionCount = 0;
        int m_Depth = 0;           // inside a state's callback
        int m_Pending = -1;        // a switch asked for from inside one
        std::string m_PendingReason;
        std::deque<TransitionRecord> m_History;
    };

}
