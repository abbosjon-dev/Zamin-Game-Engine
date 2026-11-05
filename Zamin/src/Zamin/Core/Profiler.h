#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Lightweight CPU profiler: named scopes are accumulated per frame into a tree. A scope opened
    // while another one is open on the same thread is its child; every call of a scope with the
    // same name under the same parent adds to one node (its time and call count).
    class Profiler
    {
    public:
        // One node of a frame's tree. The frame's entries are in depth-first order: a parent comes
        // before its children, siblings in the order they first ran, and the entries of a node's
        // subtree are the ones from it up to (not including) `End`.
        struct Entry
        {
            const char* Name = "";
            float Milliseconds = 0.0f;        // all its calls, children included
            int Calls = 0;
            int Parent = -1;                  // index of the enclosing scope in the same frame, -1 at the top
            int Depth = 0;                    // 0 at the top
            int End = 0;                      // one past the last entry of its subtree
            float ChildMilliseconds = 0.0f;   // the time of its direct children
            float AverageMilliseconds = 0.0f; // Milliseconds smoothed over the recent frames (same path)

            // The time spent in the scope itself, outside its children.
            float GetSelfMilliseconds() const { return std::max(0.0f, Milliseconds - ChildMilliseconds); }
        };

        // An open scope (BeginScope / EndScope). Index -1: nothing is recorded.
        struct ScopeToken
        {
            int Index = -1;
            uint64_t Frame = 0;
        };

        // Weight of the newest frame in AverageMilliseconds (an exponential moving average).
        static constexpr float AverageWeight = 0.1f;

        // The frame recorded so far becomes the last frame (GetLastFrame); a new one starts.
        // Scopes still open are dropped from the new frame (their EndScope records nothing).
        static void BeginFrame();
        // Opens a scope inside the innermost one this thread has open (the top when none).
        static ScopeToken BeginScope(const char* name);
        // Closes it, adding `milliseconds` and one call to its node.
        static void EndScope(const ScopeToken& token, float milliseconds);
        // A finished piece of work inside the innermost open scope (a leaf node).
        static void Record(const char* name, float milliseconds);

        static const std::vector<Entry>& GetLastFrame();
        // The last frame's "Kadr" scope (the main loop's frame), 0 when there was none.
        static float GetLastFrameTotal();
        // A node of the last frame by its path: the names from the top, separated by '/'
        // ("Kadr/Layer'lar (update)/Render"). Null when the frame has no such node.
        static const Entry* Find(std::string_view path);
        // The indices of a node's direct children (`parent` -1: the top level) in `entries`.
        static std::vector<int> GetChildren(const std::vector<Entry>& entries, int parent);
        // The `count` nodes that took the most time themselves (outside their children), slowest
        // first: where the frame's time goes.
        static std::vector<int> GetHotspots(const std::vector<Entry>& entries, size_t count, bool useAverage = false);
        // The tree as indented lines ("Render 3.21 ms (o'zi 0.40 ms, 2x)"), for logs; nodes below
        // `minMilliseconds` (and their subtrees) are left out.
        static std::string FormatTree(const std::vector<Entry>& entries, float minMilliseconds = 0.0f);

        static void SetEnabled(bool enabled);
        static bool IsEnabled();
        // Forgets everything (both frames, the averages, the open scopes of this thread).
        static void Reset();
    };

    class ScopedTimer
    {
    public:
        explicit ScopedTimer(const char* name) : m_Token(Profiler::BeginScope(name)), m_Start(std::chrono::high_resolution_clock::now()) {}
        ~ScopedTimer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            Profiler::EndScope(m_Token, std::chrono::duration<float, std::milli>(end - m_Start).count());
        }
        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;
    private:
        Profiler::ScopeToken m_Token;
        std::chrono::high_resolution_clock::time_point m_Start;
    };

    // The time of the last `Capacity` frames (milliseconds) and statistics over them: the viewport's
    // FPS graph.
    class FrameTimeHistory
    {
    public:
        static constexpr size_t Capacity = 240;

        // Values that are not finite or negative are not kept.
        void Push(float milliseconds);
        void Clear() { m_Count = 0; m_Next = 0; }
        size_t Size() const { return m_Count; }
        bool Empty() const { return m_Count == 0; }
        // 0 = the oldest kept frame, Size() - 1 = the newest.
        float Get(size_t index) const;
        float Latest() const { return m_Count ? Get(m_Count - 1) : 0.0f; }

        struct Summary
        {
            float Average = 0.0f;  // ms
            float Min = 0.0f;
            float Max = 0.0f;
            float Worst1Percent = 0.0f; // the average of the slowest 1% of the frames (at least one)
            float AverageFPS = 0.0f;    // 1000 / Average
            float Low1PercentFPS = 0.0f; // 1000 / Worst1Percent ("1% low")
        };
        // Over the newest `frames` frames (0: all that are kept).
        Summary Summarize(size_t frames = 0) const;

    private:
        std::array<float, Capacity> m_Values{};
        size_t m_Count = 0;
        size_t m_Next = 0;
    };

}

#define ZM_PROFILE_CONCAT_INNER(a, b) a##b
#define ZM_PROFILE_CONCAT(a, b) ZM_PROFILE_CONCAT_INNER(a, b)
#define ZM_PROFILE_SCOPE(name) ::Zamin::ScopedTimer ZM_PROFILE_CONCAT(zmProfileTimer, __LINE__)(name)
