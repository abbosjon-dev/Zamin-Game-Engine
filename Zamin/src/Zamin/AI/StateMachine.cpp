#include "Zamin/AI/StateMachine.h"

#include "Zamin/Core/Log.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    static const std::string s_NoState;

    float AIState::GetTimeInState() const
    {
        if (!m_Machine || !m_Machine->IsRunning() || m_Machine->GetStateObject(m_Machine->GetCurrentStateName()) != this)
            return 0.0f;
        return m_Machine->GetTimeInState();
    }

    // ------------------------------------------------------------------ building
    StateMachine& StateMachine::AddState(std::string name, Action onEnter, UpdateAction onUpdate, Action onExit)
    {
        State state;
        state.Name = std::move(name);
        state.OnEnter = std::move(onEnter);
        state.OnUpdate = std::move(onUpdate);
        state.OnExit = std::move(onExit);
        if (const int i = Find(state.Name); i >= 0)
            m_States[(size_t)i] = std::move(state);
        else
            m_States.push_back(std::move(state));
        return *this;
    }

    StateMachine& StateMachine::AddState(std::string name, std::unique_ptr<AIState> object)
    {
        State state;
        state.Name = std::move(name);
        if (object)
        {
            object->m_Machine = this;
            object->m_Name = state.Name;
        }
        state.Object = std::move(object);
        if (const int i = Find(state.Name); i >= 0)
            m_States[(size_t)i] = std::move(state);
        else
            m_States.push_back(std::move(state));
        return *this;
    }

    StateMachine& StateMachine::AddTransition(std::string from, std::string to, Condition condition)
    {
        Transition t;
        t.From = std::move(from);
        t.To = std::move(to);
        t.Check = std::move(condition);
        m_TransitionList.push_back(std::move(t));
        return *this;
    }

    StateMachine& StateMachine::AddAnyTransition(std::string to, Condition condition, bool allowSelf)
    {
        Transition t;
        t.To = std::move(to);
        t.Check = std::move(condition);
        t.AllowSelf = allowSelf;
        m_TransitionList.push_back(std::move(t));
        return *this;
    }

    StateMachine& StateMachine::AddTimedTransition(std::string from, std::string to, float seconds)
    {
        Transition t;
        t.From = std::move(from);
        t.To = std::move(to);
        t.Kind = TransitionKind::Timed;
        t.Seconds = std::isfinite(seconds) ? std::max(0.0f, seconds) : 0.0f;
        m_TransitionList.push_back(std::move(t));
        return *this;
    }

    StateMachine& StateMachine::AddEventTransition(std::string from, std::string to, std::string event)
    {
        Transition t;
        t.From = std::move(from);
        t.To = std::move(to);
        t.Kind = TransitionKind::Event;
        t.Event = std::move(event);
        t.AllowSelf = true;
        m_TransitionList.push_back(std::move(t));
        return *this;
    }

    // ------------------------------------------------------------------ running
    int StateMachine::Find(std::string_view name) const
    {
        for (size_t i = 0; i < m_States.size(); i++)
            if (m_States[i].Name == name)
                return (int)i;
        return -1;
    }

    void StateMachine::Enter(int state)
    {
        if (state < 0 || state >= (int)m_States.size())
            return;
        // Copies: a callback may add states (the vector moves).
        if (AIState* object = m_States[(size_t)state].Object.get())
            object->OnEnter();
        else if (Action enter = m_States[(size_t)state].OnEnter)
            enter();
    }

    void StateMachine::Exit(int state)
    {
        if (state < 0 || state >= (int)m_States.size())
            return;
        if (AIState* object = m_States[(size_t)state].Object.get())
            object->OnExit();
        else if (Action exit = m_States[(size_t)state].OnExit)
            exit();
    }

    void StateMachine::Switch(int to, const char* reason)
    {
        if (to < 0 || to >= (int)m_States.size())
            return;
        if (m_Depth > 0)
        {
            // From inside a callback: once it has returned.
            m_Pending = to;
            m_PendingReason = reason;
            return;
        }
        std::string why = reason;
        // A state that switches again in its OnEnter goes on to that one (a few times at most: two
        // states that keep sending each other back would hang otherwise).
        for (int hops = 0; hops < 8 && to >= 0; hops++)
        {
            m_Depth++;
            const int from = m_Current;
            if (from >= 0)
                Exit(from);
            m_Previous = from;
            m_Current = to;
            m_TimeInState = 0.0f;
            m_TransitionCount++;
            TransitionRecord record;
            record.From = from >= 0 ? m_States[(size_t)from].Name : std::string();
            record.To = m_States[(size_t)to].Name;
            record.Reason = why;
            record.Time = m_Time;
            if (LogTransitions)
                ZM_CORE_INFO("AI '{}': {} -> {} ({})", Name, record.From.empty() ? "-" : record.From, record.To, record.Reason);
            m_History.push_back(std::move(record));
            while (m_History.size() > MaxHistory)
                m_History.pop_front();
            Enter(to);
            m_Depth--;
            to = m_Pending;
            why = m_PendingReason;
            m_Pending = -1;
        }
        m_Pending = -1;
    }

    bool StateMachine::Start(std::string_view initial)
    {
        const int state = initial.empty() ? (m_States.empty() ? -1 : 0) : Find(initial);
        if (state < 0)
            return false;
        if (m_Current >= 0)
            Stop();
        m_Time = 0.0f;
        Switch(state, "Start");
        return true;
    }

    void StateMachine::Stop()
    {
        if (m_Current < 0)
            return;
        m_Depth++;
        Exit(m_Current);
        m_Depth--;
        m_Previous = m_Current;
        m_Current = -1;
        m_Pending = -1;
        m_TimeInState = 0.0f;
    }

    bool StateMachine::Applies(const Transition& t) const
    {
        switch (t.Kind)
        {
            case TransitionKind::Condition: return t.Check && t.Check();
            case TransitionKind::Timed: return m_TimeInState >= t.Seconds;
            case TransitionKind::Event: return false;
        }
        return false;
    }

    void StateMachine::Update(float dt)
    {
        if (m_Current < 0)
            return;
        dt = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
        m_Time += dt;
        m_TimeInState += dt;

        // The first transition that holds: "any state" ones, then the current state's.
        int to = -1;
        const char* reason = "";
        m_Depth++;
        for (int pass = 0; pass < 2 && to < 0; pass++)
            for (size_t i = 0; i < m_TransitionList.size() && to < 0 && m_Current >= 0; i++)
            {
                const Transition& t = m_TransitionList[i];
                if (t.Kind == TransitionKind::Event || (pass == 0) != t.From.empty())
                    continue;
                if (pass == 1 && t.From != m_States[(size_t)m_Current].Name)
                    continue;
                const int target = Find(t.To);
                if (target < 0 || (pass == 0 && target == m_Current && !t.AllowSelf))
                    continue;
                if (Applies(m_TransitionList[i]))
                {
                    to = target;
                    reason = t.Kind == TransitionKind::Timed ? "time" : "condition";
                }
            }
        m_Depth--;
        if (m_Pending >= 0)
        {
            // A condition asked for a switch itself.
            to = m_Pending;
            reason = "ChangeState";
            m_Pending = -1;
        }
        if (to >= 0)
            Switch(to, reason);
        if (m_Current < 0)
            return;

        m_Depth++;
        if (AIState* object = m_States[(size_t)m_Current].Object.get())
            object->OnUpdate(dt);
        else if (UpdateAction update = m_States[(size_t)m_Current].OnUpdate)
            update(dt);
        m_Depth--;
        if (m_Pending >= 0)
        {
            const int pending = m_Pending;
            m_Pending = -1;
            Switch(pending, m_PendingReason.c_str());
        }
    }

    bool StateMachine::ChangeState(std::string_view name)
    {
        const int state = Find(name);
        if (state < 0)
            return false;
        Switch(state, "ChangeState");
        return true;
    }

    bool StateMachine::SendEvent(std::string_view event)
    {
        if (m_Current < 0)
            return false;
        const std::string& current = m_States[(size_t)m_Current].Name;
        for (int pass = 0; pass < 2; pass++)
            for (const Transition& t : m_TransitionList)
            {
                if (t.Kind != TransitionKind::Event || t.Event != event)
                    continue;
                if (pass == 0 ? t.From != current : !t.From.empty())
                    continue;
                const int target = Find(t.To);
                if (target < 0)
                    continue;
                const std::string reason = "event: " + std::string(event);
                Switch(target, reason.c_str());
                return true;
            }
        return false;
    }

    // ------------------------------------------------------------------ state
    bool StateMachine::IsInState(std::string_view name) const
    {
        return m_Current >= 0 && m_States[(size_t)m_Current].Name == name;
    }

    const std::string& StateMachine::GetCurrentStateName() const
    {
        return m_Current >= 0 ? m_States[(size_t)m_Current].Name : s_NoState;
    }

    const std::string& StateMachine::GetPreviousStateName() const
    {
        return m_Previous >= 0 && m_Previous < (int)m_States.size() ? m_States[(size_t)m_Previous].Name : s_NoState;
    }

    std::vector<std::string> StateMachine::GetStateNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_States.size());
        for (const State& s : m_States)
            names.push_back(s.Name);
        return names;
    }

    AIState* StateMachine::GetStateObject(std::string_view name) const
    {
        const int i = Find(name);
        return i >= 0 ? m_States[(size_t)i].Object.get() : nullptr;
    }

    std::string StateMachine::Describe() const
    {
        if (m_Current < 0)
            return "(to'xtagan)";
        return std::format("{} ({:.1f} s)", m_States[(size_t)m_Current].Name, m_TimeInState);
    }

}
