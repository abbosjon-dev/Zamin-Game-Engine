#pragma once

// A counting hook on the global operator new (task 482): how many heap allocations a piece of code makes - a
// frame of a running scene should make none once it has warmed up. One translation unit per executable includes
// AllocCounter.inl (the replacement operators); every other one includes this header.
//
//   AllocCounter::Scope scope;          // counts from here (every thread) ...
//   scene->UpdateRuntime(ts);
//   uint64_t n = scope.Count();         // ... the operator new calls since
//
// Not available (Available() false, counts 0) under AddressSanitizer, which brings its own operator new.
#include <cstdint>

namespace AllocCounter {

    bool Available();
    // Counting on / off for every thread (off by default: only the code under test is counted).
    void SetEnabled(bool enabled);
    uint64_t GetCount();  // operator new calls while it was on
    uint64_t GetBytes();  // their bytes
    void Reset();

    // Counts while it lives (nested scopes are not supported: the outer one is switched off with it).
    class Scope
    {
    public:
        Scope() : m_Count(GetCount()), m_Bytes(GetBytes()) { SetEnabled(true); }
        ~Scope() { SetEnabled(false); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        uint64_t Count() const { return GetCount() - m_Count; }
        uint64_t Bytes() const { return GetBytes() - m_Bytes; }
    private:
        uint64_t m_Count, m_Bytes;
    };

}
