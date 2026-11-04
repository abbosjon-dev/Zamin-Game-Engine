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

#if defined(_MSC_VER)
    #define ZM_DEBUGBREAK() __debugbreak()
#else
    #include <csignal>
    #define ZM_DEBUGBREAK() raise(SIGTRAP)
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

#ifndef NDEBUG
    #define ZM_ENABLE_ASSERTS 1
#endif

#ifdef ZM_ENABLE_ASSERTS
    #define ZM_CORE_ASSERT(x, ...) do { if (!(x)) { ZM_CORE_ERROR("Assertion failed: {}", #x); ZM_DEBUGBREAK(); } } while (0)
    #define ZM_ASSERT(x, ...)      do { if (!(x)) { ZM_ERROR("Assertion failed: {}", #x); ZM_DEBUGBREAK(); } } while (0)
#else
    #define ZM_CORE_ASSERT(x, ...) ((void)0)
    #define ZM_ASSERT(x, ...)      ((void)0)
#endif
