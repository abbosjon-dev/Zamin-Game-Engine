#pragma once

/// @file
/// @brief Bounds-checked binary encoding for the network layer: a growing writer, a reader that never reads past
/// its buffer (every read after a failure returns zero and keeps the reader failed), variable-length integers and
/// a compact tagged encoding of script values (nlohmann::json) with hard limits on depth, sizes and counts.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin::Net {

    /// Appends little-endian values to a byte buffer.
    class Writer
    {
    public:
        void U8(uint8_t v) { m_Data.push_back(v); }
        void U16(uint16_t v);
        void U32(uint32_t v);
        void U64(uint64_t v);
        /// LEB128: 7 bits a byte (1 byte below 128).
        void VarU(uint64_t v);
        /// Zig-zag then VarU (small negative numbers stay small).
        void VarI(int64_t v);
        void F32(float v);
        void F64(double v);
        void Bytes(const void* data, size_t size);
        /// VarU length, then the bytes (the reader takes a maximum length).
        void String(std::string_view text);
        const std::vector<uint8_t>& Data() const { return m_Data; }
        std::vector<uint8_t>& Data() { return m_Data; }
        size_t Size() const { return m_Data.size(); }
        void Clear() { m_Data.clear(); }

    private:
        std::vector<uint8_t> m_Data;
    };

    /// Reads what Writer wrote. Every read checks the remaining size; a short buffer, an over-long varint or a string
    /// above its limit fails the reader (Ok() false) and every later read returns 0 / empty.
    class Reader
    {
    public:
        Reader(const uint8_t* data, size_t size) : m_Data(data), m_Size(data ? size : 0) {}
        explicit Reader(const std::vector<uint8_t>& data) : Reader(data.data(), data.size()) {}

        uint8_t U8();
        uint16_t U16();
        uint32_t U32();
        uint64_t U64();
        /// At most `maxBits` significant bits (a larger value fails).
        uint64_t VarU(int maxBits = 64);
        int64_t VarI();
        float F32();          ///< fails on NaN / infinity (the network never sends them)
        double F64();         ///< the same
        bool Bytes(void* out, size_t size);
        /// A String of at most maxLength bytes.
        std::string String(size_t maxLength);
        /// A VarU count of at most `max` (fails above).
        size_t Count(size_t max);

        bool Ok() const { return m_Ok; }
        void Fail() { m_Ok = false; }
        size_t Position() const { return m_Pos; }
        size_t Remaining() const { return m_Ok ? m_Size - m_Pos : 0; }
        bool AtEnd() const { return m_Ok && m_Pos == m_Size; }
        const uint8_t* Cursor() const { return m_Data + m_Pos; }
        /// Skips `size` bytes (fails when they are not there).
        bool Skip(size_t size);

    private:
        bool Need(size_t size);
        const uint8_t* m_Data = nullptr;
        size_t m_Size = 0, m_Pos = 0;
        bool m_Ok = true;
    };

    /// The limits of an encoded script value (EncodeValue / DecodeValue).
    struct ValueLimits
    {
        int MaxDepth = 8;            ///< arrays / objects inside each other
        size_t MaxString = 1024;     ///< bytes of one string or key
        size_t MaxItems = 256;       ///< elements of one array / members of one object
        size_t MaxTotalItems = 4096; ///< values in the whole tree
    };

    /// A script value (null, bool, integer, double, string, array, object) in a tagged binary form. False (with a
    /// reason) for a value beyond the limits or a non-finite number; the writer is left as it was then.
    bool EncodeValue(Writer& out, const nlohmann::json& value, std::string* error = nullptr, const ValueLimits& limits = {});
    /// The value EncodeValue wrote; false (the reader failed) for anything else.
    bool DecodeValue(Reader& in, nlohmann::json& out, const ValueLimits& limits = {});

    /// 64-bit FNV-1a (project ids, schema hashes).
    uint64_t Hash64(std::string_view text, uint64_t seed = 0xcbf29ce484222325ull);
    /// A strong 64-bit mix (challenge tokens): splitmix64's finalizer.
    uint64_t Mix64(uint64_t value);

}
