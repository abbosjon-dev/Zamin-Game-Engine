#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Colour helpers: conversions between RGB, HSV and HSL, hex text ("#FF8800", "#FF880080"),
// sRGB <-> linear light, blending, packed 8-bit colours and colour names.
//
// Colours are glm::vec3 / glm::vec4 with components 0..1 (r, g, b[, a]); the vec4 functions keep
// the alpha unless they say otherwise. Component colours in the editor (sprites, lights, text) are
// sRGB - what you see in the colour picker; the renderer converts them to linear light itself.
//
//   glm::vec4 c = Color::Parse("#3FA9F5").value_or(Colors::White);
//   glm::vec4 darker = Color::FromHSV(Color::ToHSV(c) * glm::vec4(1, 1, 0.7f, 1));
//   std::string text = Color::ToHex(c); // "#3FA9F5"
namespace Zamin::Color {

    // ------------------------------------------------------------------ HSV / HSL
    // Hue 0..1 (red 0, green 1/3, blue 2/3), saturation and value / lightness 0..1.
    glm::vec3 RGBToHSV(const glm::vec3& rgb);
    glm::vec3 HSVToRGB(const glm::vec3& hsv);
    glm::vec3 RGBToHSL(const glm::vec3& rgb);
    glm::vec3 HSLToRGB(const glm::vec3& hsl);
    // With alpha (w stays the alpha).
    glm::vec4 ToHSV(const glm::vec4& rgba);
    glm::vec4 FromHSV(const glm::vec4& hsva);
    inline glm::vec4 FromHSV(float h, float s, float v, float a = 1.0f) { return FromHSV(glm::vec4(h, s, v, a)); }
    glm::vec4 ToHSL(const glm::vec4& rgba);
    glm::vec4 FromHSL(const glm::vec4& hsla);
    inline glm::vec4 FromHSL(float h, float s, float l, float a = 1.0f) { return FromHSL(glm::vec4(h, s, l, a)); }

    // ------------------------------------------------------------------ text
    // "#RGB", "#RGBA", "#RRGGBB" or "#RRGGBBAA" (the '#' optional, any letter case, spaces around
    // allowed); nullopt for anything else.
    std::optional<glm::vec4> FromHex(std::string_view text);
    // "#RRGGBB"; with alpha "#RRGGBBAA" (Auto: only when the alpha is below 1). Clamped to 0..1.
    enum class HexAlpha { Auto = 0, Always, Never };
    std::string ToHex(const glm::vec4& color, HexAlpha alpha = HexAlpha::Auto);
    inline std::string ToHex(const glm::vec3& color) { return ToHex(glm::vec4(color, 1.0f), HexAlpha::Never); }
    // A colour name (Colors below, CSS-like English names and their Uzbek names, any letter case:
    // "red" / "qizil", "sky blue" / "havorang") - nullopt when unknown.
    std::optional<glm::vec4> FromName(std::string_view name);
    // A hex colour or a name.
    std::optional<glm::vec4> Parse(std::string_view text);

    // ------------------------------------------------------------------ sRGB / linear
    // The exact sRGB curve (a linear toe, then the 2.4 power).
    float SRGBToLinear(float c);
    float LinearToSRGB(float c);
    glm::vec3 SRGBToLinear(const glm::vec3& c);
    glm::vec3 LinearToSRGB(const glm::vec3& c);
    glm::vec4 SRGBToLinear(const glm::vec4& c);
    glm::vec4 LinearToSRGB(const glm::vec4& c);
    // The 2.2 power the renderer's shaders use (a close, cheaper approximation of the sRGB curve).
    float GammaToLinear(float c, float gamma = 2.2f);
    float LinearToGamma(float c, float gamma = 2.2f);
    glm::vec3 GammaToLinear(const glm::vec3& c, float gamma = 2.2f);
    glm::vec3 LinearToGamma(const glm::vec3& c, float gamma = 2.2f);

    // ------------------------------------------------------------------ blending
    // Plain blend of the (sRGB) components.
    glm::vec4 Lerp(const glm::vec4& a, const glm::vec4& b, float t);
    // Blended in linear light (physically even: no dark band between red and green).
    glm::vec4 LerpLinear(const glm::vec4& a, const glm::vec4& b, float t);
    // Blended in HSV, the hue the short way round the colour wheel (rainbow-like transitions).
    glm::vec4 LerpHSV(const glm::vec4& a, const glm::vec4& b, float t);
    // Relative luminance of a linear colour (Rec. 709 weights), 0..1.
    float Luminance(const glm::vec3& linear);
    // The brightness people see of an sRGB colour (its luminance, sRGB curve undone).
    float PerceivedBrightness(const glm::vec3& srgb);
    // Black or white, whichever reads better on `background` (labels on coloured buttons).
    glm::vec4 ContrastingTextColor(const glm::vec4& background);
    inline glm::vec4 WithAlpha(const glm::vec4& c, float alpha) { return { c.r, c.g, c.b, alpha }; }

    // ------------------------------------------------------------------ 8-bit
    glm::vec4 FromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    // 0xRRGGBBAA and back (components rounded, clamped to 0..1).
    uint32_t ToPacked(const glm::vec4& color);
    glm::vec4 FromPacked(uint32_t rrggbbaa);

}

// Ready-made colours (sRGB, opaque).
namespace Zamin::Colors {

    inline constexpr glm::vec4 White{ 1.0f, 1.0f, 1.0f, 1.0f };
    inline constexpr glm::vec4 Black{ 0.0f, 0.0f, 0.0f, 1.0f };
    inline constexpr glm::vec4 Clear{ 0.0f, 0.0f, 0.0f, 0.0f };
    inline constexpr glm::vec4 Gray{ 0.5f, 0.5f, 0.5f, 1.0f };
    inline constexpr glm::vec4 Red{ 1.0f, 0.0f, 0.0f, 1.0f };
    inline constexpr glm::vec4 Green{ 0.0f, 1.0f, 0.0f, 1.0f };
    inline constexpr glm::vec4 Blue{ 0.0f, 0.0f, 1.0f, 1.0f };
    inline constexpr glm::vec4 Yellow{ 1.0f, 1.0f, 0.0f, 1.0f };
    inline constexpr glm::vec4 Cyan{ 0.0f, 1.0f, 1.0f, 1.0f };
    inline constexpr glm::vec4 Magenta{ 1.0f, 0.0f, 1.0f, 1.0f };
    inline constexpr glm::vec4 Orange{ 1.0f, 0.5f, 0.0f, 1.0f };
    inline constexpr glm::vec4 Purple{ 0.5f, 0.0f, 0.5f, 1.0f };
    inline constexpr glm::vec4 Pink{ 1.0f, 0.75f, 0.8f, 1.0f };
    inline constexpr glm::vec4 Brown{ 0.6f, 0.4f, 0.2f, 1.0f };
    inline constexpr glm::vec4 SkyBlue{ 0.53f, 0.81f, 0.92f, 1.0f };
    inline constexpr glm::vec4 Gold{ 1.0f, 0.84f, 0.0f, 1.0f };

}
