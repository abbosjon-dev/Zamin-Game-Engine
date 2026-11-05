#include "Zamin/Core/Profiler.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <format>
#include <mutex>
#include <unordered_map>

namespace Zamin {

    namespace {
        // A node while its frame is recorded (the order its scopes first ran).
        struct RawNode
        {
            const char* Name;
            float Milliseconds;
            int Calls;
            int Parent;
        };

        struct AverageSlot
        {
            float Value = 0.0f;
            uint64_t Frame = 0;
        };

        std::mutex s_Mutex;
        std::vector<RawNode> s_Current;
        std::vector<Profiler::Entry> s_Last;
        std::unordered_map<uint64_t, AverageSlot> s_Averages;
        uint64_t s_Frame = 1;
        bool s_Enabled = true;

        // The scopes this thread has open (indices into s_Current), valid for frame t_StackFrame.
        thread_local std::vector<int> t_Stack;
        thread_local uint64_t t_StackFrame = 0;

        constexpr uint64_t AverageKeepFrames = 300; // a path not seen for this long forgets its average

        bool SameName(const char* a, const char* b) { return a == b || std::strcmp(a, b) == 0; }

        uint64_t HashPath(uint64_t parent, const char* name)
        {
            uint64_t h = parent ^ 0xcbf29ce484222325ull;
            for (const char* c = name; *c; c++)
            {
                h ^= (unsigned char)*c;
                h *= 0x100000001b3ull;
            }
            return h * 0x9e3779b97f4a7c15ull + 0x632be59bd9b4e019ull;
        }

        // This thread's open scopes, emptied when they belong to an earlier frame. Under s_Mutex.
        std::vector<int>& Stack()
        {
            if (t_StackFrame != s_Frame)
            {
                t_Stack.clear();
                t_StackFrame = s_Frame;
            }
            return t_Stack;
        }

        // The node `name` under the innermost open scope (made when it is new). Under s_Mutex.
        int FindOrAdd(const char* name)
        {
            std::vector<int>& stack = Stack();
            const int parent = stack.empty() ? -1 : stack.back();
            for (size_t i = parent < 0 ? 0 : (size_t)parent + 1; i < s_Current.size(); i++)
                if (s_Current[i].Parent == parent && SameName(s_Current[i].Name, name))
                    return (int)i;
            s_Current.push_back({ name, 0.0f, 0, parent });
            return (int)s_Current.size() - 1;
        }

        // The recorded frame in depth-first order, with depths, subtree ends, the children's
        // time and the averages. Under s_Mutex.
        void Finish(std::vector<Profiler::Entry>& out)
        {
            out.clear();
            out.reserve(s_Current.size());
            std::vector<std::vector<int>> children(s_Current.size());
            std::vector<int> roots;
            for (size_t i = 0; i < s_Current.size(); i++)
                (s_Current[i].Parent < 0 ? roots : children[(size_t)s_Current[i].Parent]).push_back((int)i);

            auto visit = [&](auto& self, int raw, int parent, int depth, uint64_t parentHash) -> void {
                const RawNode& node = s_Current[(size_t)raw];
                const int index = (int)out.size();
                Profiler::Entry e;
                e.Name = node.Name;
                e.Milliseconds = node.Milliseconds;
                e.Calls = node.Calls;
                e.Parent = parent;
                e.Depth = depth;
                const uint64_t hash = HashPath(parentHash, node.Name);
                AverageSlot& slot = s_Averages[hash];
                slot.Value = slot.Frame == 0 || slot.Frame + 1 < s_Frame ? node.Milliseconds
                                                                          : slot.Value + (node.Milliseconds - slot.Value) * Profiler::AverageWeight;
                slot.Frame = s_Frame;
                e.AverageMilliseconds = slot.Value;
                out.push_back(e);
                for (int child : children[(size_t)raw])
                {
                    const size_t at = out.size();
                    self(self, child, index, depth + 1, hash);
                    out[(size_t)index].ChildMilliseconds += out[at].Milliseconds;
                }
                out[(size_t)index].End = (int)out.size();
            };
            for (int root : roots)
                visit(visit, root, -1, 0, 0);

            if (s_Frame % AverageKeepFrames == 0)
                for (auto it = s_Averages.begin(); it != s_Averages.end();)
                    it = it->second.Frame + AverageKeepFrames < s_Frame ? s_Averages.erase(it) : std::next(it);
        }
    }

    void Profiler::BeginFrame()
    {
        std::lock_guard lock(s_Mutex);
        Finish(s_Last);
        s_Current.clear();
        s_Frame++;
    }

    Profiler::ScopeToken Profiler::BeginScope(const char* name)
    {
        if (!s_Enabled || !name)
            return {};
        std::lock_guard lock(s_Mutex);
        const int index = FindOrAdd(name);
        Stack().push_back(index);
        return { index, s_Frame };
    }

    void Profiler::EndScope(const ScopeToken& token, float milliseconds)
    {
        if (token.Index < 0)
            return;
        std::lock_guard lock(s_Mutex);
        if (token.Frame != s_Frame || (size_t)token.Index >= s_Current.size())
            return; // opened in an earlier frame
        RawNode& node = s_Current[(size_t)token.Index];
        if (std::isfinite(milliseconds) && milliseconds > 0.0f)
            node.Milliseconds += milliseconds;
        node.Calls++;
        // Closed in order (RAII): it is the innermost; otherwise the scopes inside it go too.
        std::vector<int>& stack = Stack();
        for (size_t i = stack.size(); i-- > 0;)
            if (stack[i] == token.Index)
            {
                stack.resize(i);
                break;
            }
    }

    void Profiler::Record(const char* name, float ms)
    {
        if (!s_Enabled || !name)
            return;
        std::lock_guard lock(s_Mutex);
        RawNode& node = s_Current[(size_t)FindOrAdd(name)];
        if (std::isfinite(ms) && ms > 0.0f)
            node.Milliseconds += ms;
        node.Calls++;
    }

    const std::vector<Profiler::Entry>& Profiler::GetLastFrame() { return s_Last; }

    float Profiler::GetLastFrameTotal()
    {
        for (const auto& e : s_Last)
            if (e.Parent < 0 && std::strcmp(e.Name, "Kadr") == 0) return e.Milliseconds;
        return 0.0f;
    }

    const Profiler::Entry* Profiler::Find(std::string_view path)
    {
        int parent = -1;
        const Entry* found = nullptr;
        while (!path.empty())
        {
            size_t slash = path.find('/');
            std::string_view name = path.substr(0, slash);
            path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
            found = nullptr;
            const size_t begin = parent < 0 ? 0 : (size_t)parent + 1;
            const size_t end = parent < 0 ? s_Last.size() : (size_t)s_Last[(size_t)parent].End;
            for (size_t i = begin; i < end; i = (size_t)s_Last[i].End)
                if (name == s_Last[i].Name)
                {
                    found = &s_Last[i];
                    parent = (int)i;
                    break;
                }
            if (!found)
                return nullptr;
        }
        return found;
    }

    std::vector<int> Profiler::GetChildren(const std::vector<Entry>& entries, int parent)
    {
        std::vector<int> children;
        if (parent >= (int)entries.size())
            return children;
        const size_t begin = parent < 0 ? 0 : (size_t)parent + 1;
        const size_t end = parent < 0 ? entries.size() : (size_t)entries[(size_t)parent].End;
        for (size_t i = begin; i < end && i < entries.size(); i = std::max(i + 1, (size_t)entries[i].End))
            children.push_back((int)i);
        return children;
    }

    std::vector<int> Profiler::GetHotspots(const std::vector<Entry>& entries, size_t count, bool useAverage)
    {
        // The self time of a node: averaged, it is its average minus its children's averages.
        std::vector<float> self(entries.size());
        for (size_t i = 0; i < entries.size(); i++)
            self[i] = useAverage ? entries[i].AverageMilliseconds : entries[i].Milliseconds;
        for (size_t i = 0; i < entries.size(); i++)
            if (entries[i].Parent >= 0 && (size_t)entries[i].Parent < entries.size())
                self[(size_t)entries[i].Parent] -= useAverage ? entries[i].AverageMilliseconds : entries[i].Milliseconds;
        std::vector<int> order(entries.size());
        for (size_t i = 0; i < order.size(); i++)
            order[i] = (int)i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return self[(size_t)a] > self[(size_t)b]; });
        order.resize(std::min(count, order.size()));
        return order;
    }

    std::string Profiler::FormatTree(const std::vector<Entry>& entries, float minMilliseconds)
    {
        std::string out;
        for (size_t i = 0; i < entries.size();)
        {
            const Entry& e = entries[i];
            if (e.Milliseconds < minMilliseconds)
            {
                i = std::max(i + 1, (size_t)e.End); // the whole subtree
                continue;
            }
            out += std::string((size_t)e.Depth * 2, ' ');
            out += std::format("{} {:.2f} ms", e.Name, e.Milliseconds);
            const bool children = (size_t)e.End > i + 1;
            if (children || e.Calls > 1)
            {
                out += " (";
                if (children)
                    out += std::format("o'zi {:.2f} ms", e.GetSelfMilliseconds());
                if (e.Calls > 1)
                    out += std::format("{}{}x", children ? ", " : "", e.Calls);
                out += ")";
            }
            out += '\n';
            i++;
        }
        return out;
    }

    void Profiler::SetEnabled(bool enabled) { s_Enabled = enabled; }
    bool Profiler::IsEnabled() { return s_Enabled; }

    void Profiler::Reset()
    {
        std::lock_guard lock(s_Mutex);
        s_Current.clear();
        s_Last.clear();
        s_Averages.clear();
        s_Frame++;
        t_Stack.clear();
        t_StackFrame = s_Frame;
    }

    // ------------------------------------------------------------------ frame times
    void FrameTimeHistory::Push(float milliseconds)
    {
        if (!std::isfinite(milliseconds) || milliseconds < 0.0f)
            return;
        m_Values[m_Next] = milliseconds;
        m_Next = (m_Next + 1) % Capacity;
        m_Count = std::min(m_Count + 1, Capacity);
    }

    float FrameTimeHistory::Get(size_t index) const
    {
        if (index >= m_Count)
            return 0.0f;
        const size_t oldest = (m_Next + Capacity - m_Count) % Capacity;
        return m_Values[(oldest + index) % Capacity];
    }

    FrameTimeHistory::Summary FrameTimeHistory::Summarize(size_t frames) const
    {
        Summary s;
        const size_t n = frames == 0 ? m_Count : std::min(frames, m_Count);
        if (n == 0)
            return s;
        std::vector<float> values;
        values.reserve(n);
        double sum = 0.0;
        for (size_t i = m_Count - n; i < m_Count; i++)
        {
            float v = Get(i);
            values.push_back(v);
            sum += v;
        }
        s.Average = (float)(sum / (double)n);
        auto [mn, mx] = std::minmax_element(values.begin(), values.end());
        s.Min = *mn;
        s.Max = *mx;
        // The slowest 1% (at least one frame).
        const size_t worst = std::max<size_t>(1, n / 100);
        std::partial_sort(values.begin(), values.begin() + (std::ptrdiff_t)worst, values.end(), std::greater<float>());
        double worstSum = 0.0;
        for (size_t i = 0; i < worst; i++)
            worstSum += values[i];
        s.Worst1Percent = (float)(worstSum / (double)worst);
        s.AverageFPS = s.Average > 0.0f ? 1000.0f / s.Average : 0.0f;
        s.Low1PercentFPS = s.Worst1Percent > 0.0f ? 1000.0f / s.Worst1Percent : 0.0f;
        return s;
    }

}
