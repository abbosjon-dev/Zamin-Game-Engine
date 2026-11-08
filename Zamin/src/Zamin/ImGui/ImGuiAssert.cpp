#include "Zamin/ImGui/ImGuiConfig.h"

#include "Zamin/Core/Log.h"

#include <atomic>
#include <cstdlib>
#include <format>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace Zamin {

    static std::atomic<unsigned long long> s_ImGuiAsserts{ 0 };

    void ImGuiAssertFailed(const char* expression, const char* file, int line)
    {
        s_ImGuiAsserts.fetch_add(1, std::memory_order_relaxed);
        // Once per place: a check that fails every frame would fill the log.
        static std::mutex s_Mutex;
        static std::set<std::pair<std::string, int>> s_Reported;
        bool first = false;
        {
            std::lock_guard lock(s_Mutex);
            first = s_Reported.emplace(file ? file : "?", line).second;
        }
        if (first)
            ZM_CORE_ERROR("ImGui tekshiruvi bajarilmadi (IM_ASSERT): {} - {}:{}", expression ? expression : "?", file ? file : "?", line);
#ifndef NDEBUG
        // A Debug build stops here, as assert() did (the crash handler writes its report).
        std::abort();
#endif
    }

    unsigned long long GetImGuiAssertCount() { return s_ImGuiAsserts.load(std::memory_order_relaxed); }

}
