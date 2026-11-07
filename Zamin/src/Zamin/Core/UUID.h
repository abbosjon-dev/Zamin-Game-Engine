#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Zamin {

    // 64-bit random unique identifier used to reference entities and assets. A new one is never 0
    // (0 means "none" everywhere). Generating is thread-safe: every thread draws from its own
    // generator, seeded from std::random_device with the full width of its state, so threads (the job
    // system, background scene loading) and separate runs of the program do not repeat each other's
    // sequences.
    class UUID
    {
    public:
        UUID();
        UUID(uint64_t uuid) : m_UUID(uuid) {}

        operator uint64_t() const { return m_UUID; }
        uint64_t GetValue() const { return m_UUID; }
        bool IsValid() const { return m_UUID != 0; }

        // 16 lower-case hex digits ("00000000000000ff"; also for 0).
        std::string ToString() const;
        // 1-16 hex digits (any case, an optional "0x", whitespace around them). Nothing for other text;
        // "0" reads as the invalid id 0 (allowZero) or as nothing.
        static std::optional<UUID> FromString(std::string_view text, bool allowZero = false);

    private:
        uint64_t m_UUID;
    };

}

template<>
struct std::hash<Zamin::UUID>
{
    // The id itself (std::hash<uint64_t>): the ids are uniformly random already, so every bucket of a
    // hash table gets its share; hash tables keyed by ids keep the order they always had.
    std::size_t operator()(const Zamin::UUID& uuid) const noexcept { return std::hash<uint64_t>()((uint64_t)uuid); }
};
