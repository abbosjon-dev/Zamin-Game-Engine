#include "Zamin/Math/Color.h"
#include "Zamin/Utils/StringUtils.h"

#include <algorithm>
#include <cmath>

namespace Zamin::Color {

    // ------------------------------------------------------------------ HSV / HSL
    namespace {

        // The hue (0..1) of a colour whose largest / smallest components are `max` / `min`.
        float Hue(const glm::vec3& c, float max, float delta)
        {
            if (!(delta > 0.0f))
                return 0.0f;
            float h;
            if (max == c.r)
                h = std::fmod((c.g - c.b) / delta, 6.0f);
            else if (max == c.g)
                h = (c.b - c.r) / delta + 2.0f;
            else
                h = (c.r - c.g) / delta + 4.0f;
            h /= 6.0f;
            return h < 0.0f ? h + 1.0f : h;
        }

        // A hue 0..1 (any number: it wraps).
        float WrapHue(float h)
        {
            if (!std::isfinite(h))
                return 0.0f;
            h -= std::floor(h);
            return h >= 1.0f ? 0.0f : h;
        }

    }

    glm::vec3 RGBToHSV(const glm::vec3& rgb)
    {
        const float max = std::max({ rgb.r, rgb.g, rgb.b }), min = std::min({ rgb.r, rgb.g, rgb.b });
        const float delta = max - min;
        return { Hue(rgb, max, delta), max > 0.0f ? delta / max : 0.0f, max };
    }

    glm::vec3 HSVToRGB(const glm::vec3& hsv)
    {
        const float h = WrapHue(hsv.x) * 6.0f, s = std::clamp(hsv.y, 0.0f, 1.0f), v = hsv.z;
        const int sector = std::min((int)h, 5);
        const float f = h - (float)sector;
        const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
        switch (sector)
        {
            case 0: return { v, t, p };
            case 1: return { q, v, p };
            case 2: return { p, v, t };
            case 3: return { p, q, v };
            case 4: return { t, p, v };
            default: return { v, p, q };
        }
    }

    glm::vec3 RGBToHSL(const glm::vec3& rgb)
    {
        const float max = std::max({ rgb.r, rgb.g, rgb.b }), min = std::min({ rgb.r, rgb.g, rgb.b });
        const float delta = max - min, l = (max + min) * 0.5f;
        const float denominator = 1.0f - std::abs(2.0f * l - 1.0f);
        return { Hue(rgb, max, delta), delta > 0.0f && denominator > 1e-7f ? delta / denominator : 0.0f, l };
    }

    glm::vec3 HSLToRGB(const glm::vec3& hsl)
    {
        const float h = WrapHue(hsl.x) * 6.0f, s = std::clamp(hsl.y, 0.0f, 1.0f), l = hsl.z;
        const float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
        const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
        const float m = l - c * 0.5f;
        glm::vec3 rgb;
        switch (std::min((int)h, 5))
        {
            case 0: rgb = { c, x, 0.0f }; break;
            case 1: rgb = { x, c, 0.0f }; break;
            case 2: rgb = { 0.0f, c, x }; break;
            case 3: rgb = { 0.0f, x, c }; break;
            case 4: rgb = { x, 0.0f, c }; break;
            default: rgb = { c, 0.0f, x }; break;
        }
        return rgb + m;
    }

    glm::vec4 ToHSV(const glm::vec4& rgba) { return glm::vec4(RGBToHSV(glm::vec3(rgba)), rgba.a); }
    glm::vec4 FromHSV(const glm::vec4& hsva) { return glm::vec4(HSVToRGB(glm::vec3(hsva)), hsva.a); }
    glm::vec4 ToHSL(const glm::vec4& rgba) { return glm::vec4(RGBToHSL(glm::vec3(rgba)), rgba.a); }
    glm::vec4 FromHSL(const glm::vec4& hsla) { return glm::vec4(HSLToRGB(glm::vec3(hsla)), hsla.a); }

    // ------------------------------------------------------------------ text
    namespace {

        int HexDigit(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

    }

    std::optional<glm::vec4> FromHex(std::string_view text)
    {
        text = StringUtils::Trim(text);
        if (!text.empty() && text.front() == '#')
            text.remove_prefix(1);
        else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(2);
        const size_t n = text.size();
        if (n != 3 && n != 4 && n != 6 && n != 8)
            return std::nullopt;
        int digits[8];
        for (size_t i = 0; i < n; i++)
            if ((digits[i] = HexDigit(text[i])) < 0)
                return std::nullopt;
        glm::vec4 c(1.0f);
        if (n <= 4)
            for (size_t i = 0; i < n; i++)
                c[(int)i] = (float)(digits[i] * 17) / 255.0f; // #F80 = #FF8800
        else
            for (size_t i = 0; i < n / 2; i++)
                c[(int)i] = (float)(digits[i * 2] * 16 + digits[i * 2 + 1]) / 255.0f;
        return c;
    }

    static int ToByte(float v)
    {
        return std::isfinite(v) ? (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f) : 0;
    }

    std::string ToHex(const glm::vec4& color, HexAlpha alpha)
    {
        static const char* const digits = "0123456789ABCDEF";
        const bool withAlpha = alpha == HexAlpha::Always || (alpha == HexAlpha::Auto && ToByte(color.a) < 255);
        std::string out = "#";
        for (int i = 0; i < (withAlpha ? 4 : 3); i++)
        {
            const int b = ToByte(color[i]);
            out += digits[b >> 4];
            out += digits[b & 15];
        }
        return out;
    }

    namespace {

        struct NamedColor
        {
            const char* Name;
            glm::vec4 Value;
        };

        // Keys without spaces, '_', '-' and apostrophes (any of ' ʻ ‘ ’), lower case.
        const NamedColor s_Names[] = {
            { "white", Colors::White }, { "oq", Colors::White },
            { "black", Colors::Black }, { "qora", Colors::Black },
            { "clear", Colors::Clear }, { "transparent", Colors::Clear }, { "shaffof", Colors::Clear },
            { "gray", Colors::Gray }, { "grey", Colors::Gray }, { "kulrang", Colors::Gray },
            { "red", Colors::Red }, { "qizil", Colors::Red },
            { "green", Colors::Green }, { "yashil", Colors::Green },
            { "blue", Colors::Blue }, { "kok", Colors::Blue },
            { "yellow", Colors::Yellow }, { "sariq", Colors::Yellow },
            { "cyan", Colors::Cyan }, { "feruza", Colors::Cyan },
            { "magenta", Colors::Magenta },
            { "orange", Colors::Orange }, { "toqsariq", Colors::Orange },
            { "purple", Colors::Purple }, { "binafsha", Colors::Purple },
            { "pink", Colors::Pink }, { "pushti", Colors::Pink },
            { "brown", Colors::Brown }, { "jigarrang", Colors::Brown },
            { "skyblue", Colors::SkyBlue }, { "havorang", Colors::SkyBlue },
            { "gold", Colors::Gold }, { "oltin", Colors::Gold }, { "tilla", Colors::Gold },
            { "silver", { 0.75f, 0.75f, 0.75f, 1.0f } }, { "kumush", { 0.75f, 0.75f, 0.75f, 1.0f } },
            { "lime", { 0.75f, 1.0f, 0.0f, 1.0f } },
            { "navy", { 0.0f, 0.0f, 0.5f, 1.0f } },
            { "teal", { 0.0f, 0.5f, 0.5f, 1.0f } },
            { "olive", { 0.5f, 0.5f, 0.0f, 1.0f } },
            { "maroon", { 0.5f, 0.0f, 0.0f, 1.0f } },
            { "violet", { 0.93f, 0.51f, 0.93f, 1.0f } },
            { "indigo", { 0.29f, 0.0f, 0.51f, 1.0f } },
        };

        std::string NameKey(std::string_view name)
        {
            std::string key;
            for (size_t i = 0; i < name.size(); i++)
            {
                const unsigned char c = (unsigned char)name[i];
                // ʻ (U+02BB: CA BB), ‘ (U+2018: E2 80 98), ’ (U+2019: E2 80 99)
                if (c == 0xCA && i + 1 < name.size() && (unsigned char)name[i + 1] == 0xBB)
                {
                    i++;
                    continue;
                }
                if (c == 0xE2 && i + 2 < name.size() && (unsigned char)name[i + 1] == 0x80 &&
                    ((unsigned char)name[i + 2] == 0x98 || (unsigned char)name[i + 2] == 0x99))
                {
                    i += 2;
                    continue;
                }
                if (c == ' ' || c == '_' || c == '-' || c == '\'' || c == '`')
                    continue;
                key += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            return key;
        }

    }

    std::optional<glm::vec4> FromName(std::string_view name)
    {
        const std::string key = NameKey(StringUtils::Trim(name));
        for (const NamedColor& n : s_Names)
            if (key == n.Name)
                return n.Value;
        return std::nullopt;
    }

    std::optional<glm::vec4> Parse(std::string_view text)
    {
        if (auto c = FromHex(text))
            return c;
        return FromName(text);
    }

    // ------------------------------------------------------------------ sRGB / linear
    float SRGBToLinear(float c)
    {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    float LinearToSRGB(float c)
    {
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    glm::vec3 SRGBToLinear(const glm::vec3& c) { return { SRGBToLinear(c.r), SRGBToLinear(c.g), SRGBToLinear(c.b) }; }
    glm::vec3 LinearToSRGB(const glm::vec3& c) { return { LinearToSRGB(c.r), LinearToSRGB(c.g), LinearToSRGB(c.b) }; }
    glm::vec4 SRGBToLinear(const glm::vec4& c) { return glm::vec4(SRGBToLinear(glm::vec3(c)), c.a); }
    glm::vec4 LinearToSRGB(const glm::vec4& c) { return glm::vec4(LinearToSRGB(glm::vec3(c)), c.a); }

    float GammaToLinear(float c, float gamma)
    {
        return c > 0.0f ? std::pow(c, gamma) : 0.0f;
    }

    float LinearToGamma(float c, float gamma)
    {
        return c > 0.0f && gamma > 0.0f ? std::pow(c, 1.0f / gamma) : 0.0f;
    }

    glm::vec3 GammaToLinear(const glm::vec3& c, float gamma) { return { GammaToLinear(c.r, gamma), GammaToLinear(c.g, gamma), GammaToLinear(c.b, gamma) }; }
    glm::vec3 LinearToGamma(const glm::vec3& c, float gamma) { return { LinearToGamma(c.r, gamma), LinearToGamma(c.g, gamma), LinearToGamma(c.b, gamma) }; }

    // ------------------------------------------------------------------ blending
    glm::vec4 Lerp(const glm::vec4& a, const glm::vec4& b, float t)
    {
        return a + (b - a) * t;
    }

    glm::vec4 LerpLinear(const glm::vec4& a, const glm::vec4& b, float t)
    {
        if (t == 0.0f)
            return a;
        if (t == 1.0f)
            return b;
        const glm::vec3 la = SRGBToLinear(glm::vec3(a)), lb = SRGBToLinear(glm::vec3(b));
        return glm::vec4(LinearToSRGB(la + (lb - la) * t), a.a + (b.a - a.a) * t);
    }

    glm::vec4 LerpHSV(const glm::vec4& a, const glm::vec4& b, float t)
    {
        if (t == 0.0f)
            return a;
        if (t == 1.0f)
            return b;
        glm::vec3 ha = RGBToHSV(glm::vec3(a)), hb = RGBToHSV(glm::vec3(b));
        // A grey has no hue of its own: it takes the other colour's (no detour through red).
        if (ha.y <= 1e-5f)
            ha.x = hb.x;
        if (hb.y <= 1e-5f)
            hb.x = ha.x;
        float dh = hb.x - ha.x;
        if (dh > 0.5f)
            dh -= 1.0f;
        else if (dh < -0.5f)
            dh += 1.0f;
        const glm::vec3 hsv(WrapHue(ha.x + dh * t), ha.y + (hb.y - ha.y) * t, ha.z + (hb.z - ha.z) * t);
        return glm::vec4(HSVToRGB(hsv), a.a + (b.a - a.a) * t);
    }

    float Luminance(const glm::vec3& linear)
    {
        return 0.2126f * linear.r + 0.7152f * linear.g + 0.0722f * linear.b;
    }

    float PerceivedBrightness(const glm::vec3& srgb)
    {
        return Luminance(SRGBToLinear(glm::clamp(srgb, glm::vec3(0.0f), glm::vec3(1.0f))));
    }

    glm::vec4 ContrastingTextColor(const glm::vec4& background)
    {
        // W3C: the contrast against black and white is equal at a luminance of about 0.179.
        return PerceivedBrightness(glm::vec3(background)) > 0.179f ? Colors::Black : Colors::White;
    }

    // ------------------------------------------------------------------ 8-bit
    glm::vec4 FromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return glm::vec4(r, g, b, a) / 255.0f;
    }

    uint32_t ToPacked(const glm::vec4& color)
    {
        return ((uint32_t)ToByte(color.r) << 24) | ((uint32_t)ToByte(color.g) << 16) | ((uint32_t)ToByte(color.b) << 8) | (uint32_t)ToByte(color.a);
    }

    glm::vec4 FromPacked(uint32_t v)
    {
        return FromRGBA8((uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
    }

}
