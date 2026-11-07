#include "Zamin/AI/BehaviorTree.h"

#include "Zamin/Core/Log.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    const char* BTStatusName(BTStatus status)
    {
        switch (status)
        {
            case BTStatus::Running: return "Running";
            case BTStatus::Success: return "Success";
            case BTStatus::Failure: return "Failure";
            default: return "Invalid";
        }
    }

    // ------------------------------------------------------------------ blackboard
    void Blackboard::Store(std::string_view key, Value value)
    {
        if (auto it = m_Values.find(key); it != m_Values.end())
            it->second = std::move(value);
        else
            m_Values.emplace(std::string(key), std::move(value));
        m_Revision++;
    }

    const Blackboard::Value* Blackboard::FindValue(std::string_view key) const
    {
        auto it = m_Values.find(key);
        return it != m_Values.end() ? &it->second : nullptr;
    }

    bool Blackboard::ToNumber(const Value& v, double& out)
    {
        if (const auto* b = std::get_if<bool>(&v))
            out = *b ? 1.0 : 0.0;
        else if (const auto* i = std::get_if<int>(&v))
            out = (double)*i;
        else if (const auto* f = std::get_if<float>(&v))
            out = (double)*f;
        else if (const auto* id = std::get_if<uint64_t>(&v))
            out = (double)*id;
        else
            return false;
        return true;
    }

    bool Blackboard::IsTrue(std::string_view key) const
    {
        const Value* v = FindValue(key);
        if (!v)
            return false;
        if (const auto* s = std::get_if<std::string>(v))
            return !s->empty();
        if (std::holds_alternative<glm::vec2>(*v) || std::holds_alternative<glm::vec3>(*v))
            return true; // a position is set
        double number = 0.0;
        return ToNumber(*v, number) && number != 0.0;
    }

    bool Blackboard::Erase(std::string_view key)
    {
        auto it = m_Values.find(key);
        if (it == m_Values.end())
            return false;
        m_Values.erase(it);
        m_Revision++;
        return true;
    }

    void Blackboard::Clear()
    {
        if (m_Values.empty())
            return;
        m_Values.clear();
        m_Revision++;
    }

    std::vector<std::string> Blackboard::GetKeys() const
    {
        std::vector<std::string> keys;
        keys.reserve(m_Values.size());
        for (const auto& [key, value] : m_Values)
            keys.push_back(key);
        return keys; // a std::map: already in order
    }

    std::string Blackboard::Describe(std::string_view key) const
    {
        const Value* v = FindValue(key);
        if (!v)
            return {};
        return std::visit(
            [](const auto& value) -> std::string {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, bool>)
                    return value ? "true" : "false";
                else if constexpr (std::is_same_v<T, int>)
                    return std::to_string(value);
                else if constexpr (std::is_same_v<T, float>)
                    return std::format("{:.2f}", value);
                else if constexpr (std::is_same_v<T, std::string>)
                    return "\"" + value + "\"";
                else if constexpr (std::is_same_v<T, glm::vec2>)
                    return std::format("({:.1f}, {:.1f})", value.x, value.y);
                else if constexpr (std::is_same_v<T, glm::vec3>)
                    return std::format("({:.1f}, {:.1f}, {:.1f})", value.x, value.y, value.z);
                else
                    return std::format("#{:x}", value);
            },
            *v);
    }

    // ------------------------------------------------------------------ nodes
    BTStatus BTNode::Tick(BTContext& context)
    {
        if (m_Status != BTStatus::Running)
        {
            m_StartCount++;
            OnStart(context);
        }
        BTStatus status = OnTick(context);
        if (status == BTStatus::Invalid)
            status = BTStatus::Failure; // a node that does not say: it did not work
        m_Status = status;
        m_TickCount++;
        m_LastTick = context.Tree.GetTickIndex();
        return status;
    }

    void BTNode::Halt()
    {
        if (m_Status == BTStatus::Running)
            OnHalt();
        m_Status = BTStatus::Invalid;
    }

    void BTComposite::AddChild(std::unique_ptr<BTNode> child)
    {
        if (child)
            m_Children.push_back(std::move(child));
    }

    void BTComposite::HaltChildren(size_t first)
    {
        for (size_t i = first; i < m_Children.size(); i++)
            if (m_Children[i]->IsRunning())
                m_Children[i]->Halt();
    }

    BTStatus BTSequence::OnTick(BTContext& context)
    {
        for (size_t i = m_Reactive ? 0 : m_Index; i < m_Children.size(); i++)
        {
            const BTStatus status = m_Children[i]->Tick(context);
            if (status == BTStatus::Success)
                continue;
            // Running or Failure here: what ran further on (a reactive sequence's earlier branch) stops.
            HaltChildren(i + 1);
            if (status == BTStatus::Running)
            {
                m_Index = i;
                return BTStatus::Running;
            }
            m_Index = 0;
            return BTStatus::Failure;
        }
        m_Index = 0;
        return BTStatus::Success;
    }

    BTStatus BTSelector::OnTick(BTContext& context)
    {
        for (size_t i = m_Reactive ? 0 : m_Index; i < m_Children.size(); i++)
        {
            const BTStatus status = m_Children[i]->Tick(context);
            if (status == BTStatus::Failure)
                continue;
            HaltChildren(i + 1);
            if (status == BTStatus::Running)
            {
                m_Index = i;
                return BTStatus::Running;
            }
            m_Index = 0;
            return BTStatus::Success;
        }
        m_Index = 0;
        return BTStatus::Failure;
    }

    void BTParallel::OnStart(BTContext&)
    {
        m_Results.assign(m_Children.size(), BTStatus::Invalid);
        m_Successes = m_Failures = 0;
    }

    BTStatus BTParallel::OnTick(BTContext& context)
    {
        const int n = (int)m_Children.size();
        if (n == 0)
            return BTStatus::Success;
        if (m_Results.size() != m_Children.size())
            m_Results.assign(m_Children.size(), BTStatus::Invalid);
        const int needSuccess = m_SuccessThreshold < 1 ? n : std::min(m_SuccessThreshold, n);
        const int needFailure = m_FailureThreshold < 1 ? n : std::min(m_FailureThreshold, n);
        for (size_t i = 0; i < m_Children.size(); i++)
        {
            if (m_Results[i] != BTStatus::Invalid)
                continue; // finished in this run
            const BTStatus status = m_Children[i]->Tick(context);
            if (status == BTStatus::Success)
                m_Successes++;
            else if (status == BTStatus::Failure)
                m_Failures++;
            if (status != BTStatus::Running)
                m_Results[i] = status;
        }
        BTStatus result = BTStatus::Running;
        if (m_Failures >= needFailure)
            result = BTStatus::Failure;
        else if (m_Successes >= needSuccess)
            result = BTStatus::Success;
        else if (m_Successes + m_Failures >= n)
            result = BTStatus::Failure; // all done, not enough successes
        if (result != BTStatus::Running)
            HaltChildren(0);
        return result;
    }

    std::string BTParallel::GetDetail() const
    {
        return std::format("{}+ {}-", m_Successes, m_Failures);
    }

    BTStatus BTInverter::OnTick(BTContext& context)
    {
        if (!m_Child)
            return BTStatus::Failure;
        const BTStatus status = m_Child->Tick(context);
        return status == BTStatus::Success ? BTStatus::Failure : status == BTStatus::Failure ? BTStatus::Success : status;
    }

    BTStatus BTSucceeder::OnTick(BTContext& context)
    {
        if (!m_Child)
            return BTStatus::Failure;
        return m_Child->Tick(context) == BTStatus::Running ? BTStatus::Running : BTStatus::Success;
    }

    BTStatus BTFailer::OnTick(BTContext& context)
    {
        if (!m_Child)
            return BTStatus::Failure;
        return m_Child->Tick(context) == BTStatus::Running ? BTStatus::Running : BTStatus::Failure;
    }

    BTStatus BTRepeat::OnTick(BTContext& context)
    {
        if (!m_Child)
            return BTStatus::Failure;
        const BTStatus status = m_Child->Tick(context);
        if (status == BTStatus::Running)
            return BTStatus::Running;
        if (status == BTStatus::Failure)
            return BTStatus::Failure;
        m_Done++;
        if (m_Count >= 1 && m_Done >= m_Count)
            return BTStatus::Success;
        return BTStatus::Running; // the next run starts next tick
    }

    std::string BTRepeat::GetDetail() const
    {
        return m_Count >= 1 ? std::format("{}/{}", m_Done, m_Count) : std::format("{}/-", m_Done);
    }

    BTStatus BTWait::OnTick(BTContext& context)
    {
        if (std::isfinite(context.DeltaTime) && context.DeltaTime > 0.0f)
            m_Elapsed += context.DeltaTime;
        const float seconds = std::isfinite(m_Seconds) ? std::max(0.0f, m_Seconds) : 0.0f;
        return m_Elapsed >= seconds ? BTStatus::Success : BTStatus::Running;
    }

    std::string BTWait::GetDetail() const
    {
        return std::format("{:.1f} / {:.1f} s", std::min(m_Elapsed, std::max(0.0f, m_Seconds)), m_Seconds);
    }

    BTStatus BTCondition::OnTick(BTContext& context)
    {
        return m_Check && m_Check(context) ? BTStatus::Success : BTStatus::Failure;
    }

    BTStatus BTAction::OnTick(BTContext& context)
    {
        return m_Tick ? m_Tick(context) : BTStatus::Failure;
    }

    // ------------------------------------------------------------------ the tree
    void BehaviorTree::SetRoot(std::unique_ptr<BTNode> root)
    {
        if (m_Root)
            m_Root->Halt();
        m_Root = std::move(root);
        m_LastPath.clear();
    }

    BTStatus BehaviorTree::Tick(float dt)
    {
        if (!m_Root)
            return BTStatus::Failure;
        if (!std::isfinite(dt) || dt < 0.0f)
            dt = 0.0f;
        m_TickIndex++;
        m_Time += dt;
        BTContext context{ dt, m_Blackboard, *this };
        const BTStatus status = m_Root->Tick(context);
        if (LogChanges)
        {
            std::string path = GetActivePath();
            if (path != m_LastPath)
            {
                ZM_INFO("AI daraxti '{}': {}", Name.empty() ? "AI" : Name, path);
                m_LastPath = std::move(path);
            }
        }
        return status;
    }

    void BehaviorTree::Halt()
    {
        if (m_Root)
            m_Root->Halt();
        m_LastPath.clear();
    }

    void BehaviorTree::Reset()
    {
        Halt();
        m_TickIndex = 0;
        m_Time = 0.0f;
    }

    void BehaviorTree::ForEachNode(const std::function<void(const BTNode&, int)>& fn) const
    {
        if (!m_Root || !fn)
            return;
        struct Walk
        {
            const std::function<void(const BTNode&, int)>& Fn;
            void Visit(const BTNode& node, int depth) const
            {
                Fn(node, depth);
                for (size_t i = 0; i < node.GetChildCount(); i++)
                    if (const BTNode* child = node.GetChild(i))
                        Visit(*child, depth + 1);
            }
        };
        Walk{ fn }.Visit(*m_Root, 0);
    }

    size_t BehaviorTree::GetNodeCount() const
    {
        size_t count = 0;
        ForEachNode([&](const BTNode&, int) { count++; });
        return count;
    }

    std::string BehaviorTree::GetActivePath() const
    {
        if (!m_Root || m_TickIndex == 0 || m_Root->GetLastTick() != m_TickIndex)
            return {};
        // Down the children ticked in the last tick: a running one, else the last ticked (the one
        // that decided).
        std::string path = m_Root->GetName();
        const BTNode* node = m_Root.get();
        for (int depth = 0; depth < 256; depth++)
        {
            const BTNode* next = nullptr;
            for (size_t i = 0; i < node->GetChildCount(); i++)
            {
                const BTNode* child = node->GetChild(i);
                if (!child || child->GetLastTick() != m_TickIndex)
                    continue;
                if (child->IsRunning())
                {
                    next = child;
                    break;
                }
                next = child;
            }
            if (!next)
                break;
            path += " > " + next->GetName();
            node = next;
        }
        return path;
    }

    std::string BehaviorTree::Describe() const
    {
        if (!m_Root || m_Root->GetStatus() == BTStatus::Invalid)
            return "(to'xtagan)";
        std::string path = GetActivePath();
        // The root and its first level say little: the last two nodes of the branch.
        size_t cut = std::string::npos;
        int separators = 0;
        for (size_t i = path.size(); i-- > 0;)
            if (path.compare(i, 3, " > ") == 0 && ++separators == 2)
            {
                cut = i + 3;
                break;
            }
        if (cut != std::string::npos)
            path = path.substr(cut);
        return std::format("{} ({})", path, BTStatusName(m_Root->GetStatus()));
    }

    // ------------------------------------------------------------------ builder
    void BehaviorTreeBuilder::Error(std::string_view what)
    {
        m_Errors++;
        ZM_CORE_WARN("Behavior tree: {}", what);
    }

    bool BehaviorTreeBuilder::Attach(std::unique_ptr<BTNode>& node, BTNode*& placed)
    {
        placed = node.get();
        if (m_Open.empty())
        {
            if (m_Root)
            {
                Error(std::format("'{}' uchun joy yo'q: daraxtning ildizi bitta (ortiqcha End() yoki ikkinchi ildiz)", node->GetName()));
                return false;
            }
            m_Root = std::move(node);
            return true;
        }
        BTNode* parent = m_Open.back();
        if (auto* composite = dynamic_cast<BTComposite*>(parent))
            composite->AddChild(std::move(node));
        else if (auto* decorator = dynamic_cast<BTDecorator*>(parent))
        {
            decorator->SetChild(std::move(node));
            m_Open.pop_back(); // a decorator has one child: closed
        }
        else
        {
            Error(std::format("'{}' bolasi bo'lolmaydi", parent->GetName()));
            return false;
        }
        return true;
    }

    BehaviorTreeBuilder& BehaviorTreeBuilder::Open(std::unique_ptr<BTNode> node)
    {
        BTNode* placed = nullptr;
        if (Attach(node, placed))
            m_Open.push_back(placed);
        return *this;
    }

    BehaviorTreeBuilder& BehaviorTreeBuilder::Leaf(std::unique_ptr<BTNode> node)
    {
        if (!node)
            return *this;
        BTNode* placed = nullptr;
        Attach(node, placed);
        return *this;
    }

    BehaviorTreeBuilder& BehaviorTreeBuilder::CheckBlackboard(std::string key, std::string name)
    {
        std::string label = name.empty() ? key + "?" : std::move(name);
        return Leaf(std::make_unique<BTCondition>(std::move(label), [key = std::move(key)](BTContext& c) { return c.Board.IsTrue(key); }));
    }

    BehaviorTreeBuilder& BehaviorTreeBuilder::End()
    {
        // Decorators still waiting for their child cannot be the thing closed: an error each.
        while (!m_Open.empty() && dynamic_cast<BTDecorator*>(m_Open.back()))
        {
            Error(std::format("'{}' dekoratori bolasiz yopildi", m_Open.back()->GetName()));
            m_Open.pop_back();
        }
        if (m_Open.empty())
        {
            Error("End() ortiqcha: ochiq tugun yo'q");
            return *this;
        }
        m_Open.pop_back();
        return *this;
    }

    std::unique_ptr<BTNode> BehaviorTreeBuilder::Build()
    {
        for (BTNode* open : m_Open)
            if (auto* decorator = dynamic_cast<BTDecorator*>(open); decorator && decorator->GetChildCount() == 0)
                Error(std::format("'{}' dekoratorining bolasi yo'q", open->GetName()));
        m_Open.clear();
        return std::move(m_Root);
    }

    bool BehaviorTreeBuilder::Build(BehaviorTree& tree)
    {
        std::unique_ptr<BTNode> root = Build();
        if (!root)
            return false;
        tree.SetRoot(std::move(root));
        return true;
    }

}
