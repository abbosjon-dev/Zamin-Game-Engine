#include "Zamin/Network/NetCodec.h"

#include <cmath>
#include <cstring>

namespace Zamin::Net {

    void Writer::U16(uint16_t v)
    {
        m_Data.push_back(uint8_t(v));
        m_Data.push_back(uint8_t(v >> 8));
    }
    void Writer::U32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i) m_Data.push_back(uint8_t(v >> (8 * i)));
    }
    void Writer::U64(uint64_t v)
    {
        for (int i = 0; i < 8; ++i) m_Data.push_back(uint8_t(v >> (8 * i)));
    }
    void Writer::VarU(uint64_t v)
    {
        while (v >= 0x80)
        {
            m_Data.push_back(uint8_t(v | 0x80));
            v >>= 7;
        }
        m_Data.push_back(uint8_t(v));
    }
    void Writer::VarI(int64_t v) { VarU((uint64_t(v) << 1) ^ uint64_t(v >> 63)); }
    void Writer::F32(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }
    void Writer::F64(double v)
    {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U64(bits);
    }
    void Writer::Bytes(const void* data, size_t size)
    {
        if (!size) return;
        const auto* p = static_cast<const uint8_t*>(data);
        m_Data.insert(m_Data.end(), p, p + size);
    }
    void Writer::String(std::string_view text)
    {
        VarU(text.size());
        Bytes(text.data(), text.size());
    }

    bool Reader::Need(size_t size)
    {
        if (!m_Ok || size > m_Size - m_Pos)
        {
            m_Ok = false;
            return false;
        }
        return true;
    }
    uint8_t Reader::U8()
    {
        if (!Need(1)) return 0;
        return m_Data[m_Pos++];
    }
    uint16_t Reader::U16()
    {
        if (!Need(2)) return 0;
        const uint16_t v = uint16_t(m_Data[m_Pos] | (m_Data[m_Pos + 1] << 8));
        m_Pos += 2;
        return v;
    }
    uint32_t Reader::U32()
    {
        if (!Need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(m_Data[m_Pos + size_t(i)]) << (8 * i);
        m_Pos += 4;
        return v;
    }
    uint64_t Reader::U64()
    {
        if (!Need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(m_Data[m_Pos + size_t(i)]) << (8 * i);
        m_Pos += 8;
        return v;
    }
    uint64_t Reader::VarU(int maxBits)
    {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7)
        {
            const uint8_t byte = U8();
            if (!m_Ok) return 0;
            v |= uint64_t(byte & 0x7F) << shift;
            if (!(byte & 0x80))
            {
                // A value wider than allowed (or a ninth/tenth byte with high bits that do not fit 64 bits).
                if ((maxBits < 64 && (v >> maxBits) != 0) || (shift == 63 && byte > 1))
                {
                    m_Ok = false;
                    return 0;
                }
                return v;
            }
        }
        m_Ok = false;
        return 0;
    }
    int64_t Reader::VarI()
    {
        const uint64_t v = VarU();
        return int64_t(v >> 1) ^ -int64_t(v & 1);
    }
    float Reader::F32()
    {
        const uint32_t bits = U32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        if (!std::isfinite(v))
        {
            m_Ok = false;
            return 0.0f;
        }
        return m_Ok ? v : 0.0f;
    }
    double Reader::F64()
    {
        const uint64_t bits = U64();
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        if (!std::isfinite(v))
        {
            m_Ok = false;
            return 0.0;
        }
        return m_Ok ? v : 0.0;
    }
    bool Reader::Bytes(void* out, size_t size)
    {
        if (!Need(size)) return false;
        if (size) std::memcpy(out, m_Data + m_Pos, size);
        m_Pos += size;
        return true;
    }
    bool Reader::Skip(size_t size)
    {
        if (!Need(size)) return false;
        m_Pos += size;
        return true;
    }
    std::string Reader::String(size_t maxLength)
    {
        const uint64_t length = VarU(32);
        if (!m_Ok || length > maxLength || !Need(size_t(length)))
        {
            m_Ok = false;
            return {};
        }
        std::string text(reinterpret_cast<const char*>(m_Data + m_Pos), size_t(length));
        m_Pos += size_t(length);
        return text;
    }
    size_t Reader::Count(size_t max)
    {
        const uint64_t count = VarU(32);
        if (!m_Ok || count > max)
        {
            m_Ok = false;
            return 0;
        }
        return size_t(count);
    }

    namespace {
        enum class Tag : uint8_t { Null = 0, False, True, Int, Double, String, Array, Object };

        bool Encode(Writer& out, const nlohmann::json& v, std::string* error, const ValueLimits& limits, int depth, size_t& total)
        {
            auto fail = [&](const char* why) {
                if (error) *error = why;
                return false;
            };
            if (++total > limits.MaxTotalItems) return fail("the value has too many elements");
            switch (v.type())
            {
                case nlohmann::json::value_t::null: out.U8(uint8_t(Tag::Null)); return true;
                case nlohmann::json::value_t::boolean: out.U8(uint8_t(v.get<bool>() ? Tag::True : Tag::False)); return true;
                case nlohmann::json::value_t::number_integer:
                    out.U8(uint8_t(Tag::Int));
                    out.VarI(v.get<int64_t>());
                    return true;
                case nlohmann::json::value_t::number_unsigned:
                {
                    const uint64_t u = v.get<uint64_t>();
                    if (u > uint64_t(INT64_MAX)) return fail("an integer above 2^63");
                    out.U8(uint8_t(Tag::Int));
                    out.VarI(int64_t(u));
                    return true;
                }
                case nlohmann::json::value_t::number_float:
                {
                    const double d = v.get<double>();
                    if (!std::isfinite(d)) return fail("a number that is not finite");
                    out.U8(uint8_t(Tag::Double));
                    out.F64(d);
                    return true;
                }
                case nlohmann::json::value_t::string:
                {
                    const auto& s = v.get_ref<const std::string&>();
                    if (s.size() > limits.MaxString) return fail("a string above the size limit");
                    out.U8(uint8_t(Tag::String));
                    out.String(s);
                    return true;
                }
                case nlohmann::json::value_t::array:
                    if (depth >= limits.MaxDepth) return fail("the value is nested too deeply");
                    if (v.size() > limits.MaxItems) return fail("an array above the element limit");
                    out.U8(uint8_t(Tag::Array));
                    out.VarU(v.size());
                    for (const auto& item : v)
                        if (!Encode(out, item, error, limits, depth + 1, total)) return false;
                    return true;
                case nlohmann::json::value_t::object:
                    if (depth >= limits.MaxDepth) return fail("the value is nested too deeply");
                    if (v.size() > limits.MaxItems) return fail("an object above the member limit");
                    out.U8(uint8_t(Tag::Object));
                    out.VarU(v.size());
                    for (auto it = v.begin(); it != v.end(); ++it)
                    {
                        if (it.key().size() > limits.MaxString) return fail("a key above the size limit");
                        out.String(it.key());
                        if (!Encode(out, it.value(), error, limits, depth + 1, total)) return false;
                    }
                    return true;
                default: return fail("an unsupported value (binary or discarded)");
            }
        }

        bool Decode(Reader& in, nlohmann::json& out, const ValueLimits& limits, int depth, size_t& total)
        {
            if (++total > limits.MaxTotalItems)
            {
                in.Fail();
                return false;
            }
            const uint8_t tag = in.U8();
            if (!in.Ok()) return false;
            switch (Tag(tag))
            {
                case Tag::Null: out = nullptr; return true;
                case Tag::False: out = false; return true;
                case Tag::True: out = true; return true;
                case Tag::Int: out = in.VarI(); return in.Ok();
                case Tag::Double: out = in.F64(); return in.Ok();
                case Tag::String: out = in.String(limits.MaxString); return in.Ok();
                case Tag::Array:
                {
                    if (depth >= limits.MaxDepth) break;
                    const size_t count = in.Count(limits.MaxItems);
                    if (!in.Ok()) return false;
                    out = nlohmann::json::array();
                    for (size_t i = 0; i < count; ++i)
                    {
                        nlohmann::json item;
                        if (!Decode(in, item, limits, depth + 1, total)) return false;
                        out.push_back(std::move(item));
                    }
                    return true;
                }
                case Tag::Object:
                {
                    if (depth >= limits.MaxDepth) break;
                    const size_t count = in.Count(limits.MaxItems);
                    if (!in.Ok()) return false;
                    out = nlohmann::json::object();
                    for (size_t i = 0; i < count; ++i)
                    {
                        std::string key = in.String(limits.MaxString);
                        nlohmann::json item;
                        if (!in.Ok() || !Decode(in, item, limits, depth + 1, total)) return false;
                        // A repeated key is a malformed value (an encoder never writes one).
                        if (out.contains(key)) break;
                        out[std::move(key)] = std::move(item);
                    }
                    if (out.size() == count) return true;
                    break;
                }
            }
            in.Fail();
            return false;
        }
    }

    bool EncodeValue(Writer& out, const nlohmann::json& value, std::string* error, const ValueLimits& limits)
    {
        const size_t start = out.Size();
        size_t total = 0;
        if (Encode(out, value, error, limits, 0, total)) return true;
        out.Data().resize(start);
        return false;
    }

    bool DecodeValue(Reader& in, nlohmann::json& out, const ValueLimits& limits)
    {
        size_t total = 0;
        return Decode(in, out, limits, 0, total);
    }

    uint64_t Hash64(std::string_view text, uint64_t seed)
    {
        uint64_t h = seed;
        for (unsigned char c : text)
        {
            h ^= c;
            h *= 0x100000001b3ull;
        }
        return h;
    }

    uint64_t Mix64(uint64_t z)
    {
        z += 0x9e3779b97f4a7c15ull;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

}
