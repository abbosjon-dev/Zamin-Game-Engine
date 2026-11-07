#include "Zamin/Core/UUID.h"

#include "Zamin/Utils/StringUtils.h"

#include <array>
#include <random>

namespace Zamin {

    namespace {

        // One generator per thread (no lock, no data race between the job system's workers and the
        // background scene loader), seeded with as many random_device words as its state holds: a
        // single 32-bit seed would allow only 2^32 different id sequences across all runs.
        std::mt19937_64& Engine()
        {
            thread_local std::mt19937_64 engine = [] {
                std::random_device device;
                std::array<std::random_device::result_type, std::mt19937_64::state_size * 2> words;
                for (auto& word : words)
                    word = device();
                std::seed_seq seed(words.begin(), words.end());
                return std::mt19937_64(seed);
            }();
            return engine;
        }

    }

    UUID::UUID()
    {
        // Never 0 (the "none" id): drawn again in the (2^-64) case it comes up.
        std::mt19937_64& engine = Engine();
        uint64_t value = engine();
        while (value == 0)
            value = engine();
        m_UUID = value;
    }

    std::string UUID::ToString() const
    {
        return StringUtils::ToHex(m_UUID, 16);
    }

    std::optional<UUID> UUID::FromString(std::string_view text, bool allowZero)
    {
        text = StringUtils::Trim(text);
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(2);
        if (text.empty() || text.size() > 16)
            return std::nullopt;
        uint64_t value = 0;
        for (char c : text)
        {
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return std::nullopt;
            value = (value << 4) | (uint64_t)digit;
        }
        if (value == 0 && !allowZero)
            return std::nullopt;
        return UUID(value);
    }

}
