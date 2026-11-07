#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#if defined(__linux__)
    #define ZM_PLATFORM_LINUX 1
#elif defined(_WIN32)
    #define ZM_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #define ZM_PLATFORM_MACOS 1
#endif

#define ZM_BIT(x) (1u << (x))
#define ZM_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Zamin {

    template<typename T>
    using Scope = std::unique_ptr<T>;
    template<typename T, typename... Args>
    constexpr Scope<T> CreateScope(Args&&... args) { return std::make_unique<T>(std::forward<Args>(args)...); }

    template<typename T>
    using Ref = std::shared_ptr<T>;
    template<typename T, typename... Args>
    constexpr Ref<T> CreateRef(Args&&... args) { return std::make_shared<T>(std::forward<Args>(args)...); }

}

#include "Zamin/Core/Log.h"
// ZM_ASSERT / ZM_CORE_ASSERT / ZM_VERIFY (with formatted messages), ZM_DEBUGBREAK.
#include "Zamin/Core/Assert.h"
