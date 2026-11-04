#pragma once

#include <cstdint>
#include <functional>

namespace Zamin {

    // 64-bit random unique identifier used to reference entities and assets.
    class UUID
    {
    public:
        UUID();
        UUID(uint64_t uuid) : m_UUID(uuid) {}

        operator uint64_t() const { return m_UUID; }

    private:
        uint64_t m_UUID;
    };

}

template<>
struct std::hash<Zamin::UUID>
{
    std::size_t operator()(const Zamin::UUID& uuid) const noexcept { return std::hash<uint64_t>()((uint64_t)uuid); }
};
