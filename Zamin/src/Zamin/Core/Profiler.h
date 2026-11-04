#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace Zamin {

    // Lightweight CPU profiler: named scopes are accumulated per frame.
    class Profiler
    {
    public:
        struct Entry
        {
            const char* Name;
            float Milliseconds;
            int Calls;
        };

        static void BeginFrame();
        static void Record(const char* name, float milliseconds);
        static const std::vector<Entry>& GetLastFrame();
        static float GetLastFrameTotal();
        static void SetEnabled(bool enabled);
        static bool IsEnabled();
    };

    class ScopedTimer
    {
    public:
        explicit ScopedTimer(const char* name) : m_Name(name), m_Start(std::chrono::high_resolution_clock::now()) {}
        ~ScopedTimer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            Profiler::Record(m_Name, std::chrono::duration<float, std::milli>(end - m_Start).count());
        }
    private:
        const char* m_Name;
        std::chrono::high_resolution_clock::time_point m_Start;
    };

}

#define ZM_PROFILE_CONCAT_INNER(a, b) a##b
#define ZM_PROFILE_CONCAT(a, b) ZM_PROFILE_CONCAT_INNER(a, b)
#define ZM_PROFILE_SCOPE(name) ::Zamin::ScopedTimer ZM_PROFILE_CONCAT(zmProfileTimer, __LINE__)(name)
