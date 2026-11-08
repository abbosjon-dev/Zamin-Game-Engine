// The replacement global operator new / delete of AllocCounter.h: include in exactly one .cpp of an executable.
#include "AllocCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

#if defined(__linux__)
#include <execinfo.h>
#include <unistd.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#define ZAMIN_ALLOC_COUNTER 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ZAMIN_ALLOC_COUNTER 0
#endif
#endif
#ifndef ZAMIN_ALLOC_COUNTER
#define ZAMIN_ALLOC_COUNTER 1
#endif

namespace AllocCounter {

    namespace {
        std::atomic<bool> s_Enabled{ false };
        std::atomic<uint64_t> s_Count{ 0 };
        std::atomic<uint64_t> s_Bytes{ 0 };
    }

    bool Available() { return ZAMIN_ALLOC_COUNTER != 0; }
    void SetEnabled(bool enabled) { s_Enabled.store(enabled, std::memory_order_relaxed); }
    uint64_t GetCount() { return s_Count.load(std::memory_order_relaxed); }
    uint64_t GetBytes() { return s_Bytes.load(std::memory_order_relaxed); }
    void Reset()
    {
        s_Count.store(0, std::memory_order_relaxed);
        s_Bytes.store(0, std::memory_order_relaxed);
    }

    // ZAMIN_ALLOC_TRACE=<n>: the call stacks of n counted allocations (every 7th one) go to stderr - where a
    // frame's allocations come from (addr2line / c++filt the addresses of a build with symbols).
    long TraceBudget()
    {
        static long s_Budget = [] {
            const char* value = std::getenv("ZAMIN_ALLOC_TRACE");
            return value ? std::atol(value) : 0L;
        }();
        return s_Budget;
    }
    thread_local bool t_Tracing = false;
    std::atomic<long> s_Traced{ 0 };

    // Called by the operators below.
    inline void Note(std::size_t size)
    {
        if (s_Enabled.load(std::memory_order_relaxed))
        {
            const uint64_t n = s_Count.fetch_add(1, std::memory_order_relaxed);
            s_Bytes.fetch_add(size, std::memory_order_relaxed);
#if defined(__linux__)
            if (!t_Tracing && n % 7 == 0 && TraceBudget() > 0 && s_Traced.fetch_add(1) < TraceBudget())
            {
                t_Tracing = true;
                void* frames[24];
                const int depth = backtrace(frames, 24);
                const char header[] = "--- ajratma\n";
                (void)!write(2, header, sizeof(header) - 1);
                backtrace_symbols_fd(frames, depth, 2);
                t_Tracing = false;
            }
#endif
        }
    }

}

#if ZAMIN_ALLOC_COUNTER
// malloc / free underneath, like the default ones (the aligned forms are left to the library: they pair with its
// aligned delete). Never inlined: inlined, the compiler would see free() on what operator new returned.
#if defined(__GNUC__)
#define ZAMIN_ALLOC_NOINLINE __attribute__((noinline))
#else
#define ZAMIN_ALLOC_NOINLINE
#endif
ZAMIN_ALLOC_NOINLINE void* operator new(std::size_t size)
{
    AllocCounter::Note(size);
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
ZAMIN_ALLOC_NOINLINE void* operator new[](std::size_t size)
{
    AllocCounter::Note(size);
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
ZAMIN_ALLOC_NOINLINE void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    AllocCounter::Note(size);
    return std::malloc(size ? size : 1);
}
ZAMIN_ALLOC_NOINLINE void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    AllocCounter::Note(size);
    return std::malloc(size ? size : 1);
}
ZAMIN_ALLOC_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
ZAMIN_ALLOC_NOINLINE void operator delete[](void* p) noexcept { std::free(p); }
ZAMIN_ALLOC_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
ZAMIN_ALLOC_NOINLINE void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
ZAMIN_ALLOC_NOINLINE void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
ZAMIN_ALLOC_NOINLINE void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
#endif
