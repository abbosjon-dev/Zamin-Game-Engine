#include "Zamin/Core/Profiler.h"

#include <cstring>
#include <mutex>

namespace Zamin {

    namespace {
        std::mutex s_Mutex;
        std::vector<Profiler::Entry> s_Current, s_Last;
        bool s_Enabled = true;
    }

    void Profiler::BeginFrame()
    {
        std::lock_guard lock(s_Mutex);
        s_Last.swap(s_Current);
        s_Current.clear();
    }

    void Profiler::Record(const char* name, float ms)
    {
        if (!s_Enabled) return;
        std::lock_guard lock(s_Mutex);
        for (auto& e : s_Current)
            if (e.Name == name || std::strcmp(e.Name, name) == 0)
            {
                e.Milliseconds += ms;
                e.Calls++;
                return;
            }
        s_Current.push_back({ name, ms, 1 });
    }

    const std::vector<Profiler::Entry>& Profiler::GetLastFrame() { return s_Last; }

    float Profiler::GetLastFrameTotal()
    {
        for (const auto& e : s_Last)
            if (std::strcmp(e.Name, "Kadr") == 0) return e.Milliseconds;
        return 0.0f;
    }

    void Profiler::SetEnabled(bool enabled) { s_Enabled = enabled; }
    bool Profiler::IsEnabled() { return s_Enabled; }

}
