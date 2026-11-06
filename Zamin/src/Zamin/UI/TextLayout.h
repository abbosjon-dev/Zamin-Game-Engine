#pragma once

// Text placed in a box: horizontal / vertical alignment, word wrap at the box's width (UTF-8:
// o'zbek oʻ, gʻ, sh and Cyrillic letters are single characters, apostrophes stay inside words),
// what happens to text that does not fit (overflow, truncate, ellipsis) and line spacing. Pure
// layout - no GPU - so the scene's UI pass, world-space text, the editor and the tests share it;
// TextLayout::Draw draws a laid-out block with Renderer2D.

#include "Zamin/Core/Base.h"
#include "Zamin/UI/UIWidgets.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class Font;
    struct TextParams;

    enum class TextAlignH { Left = 0, Center, Right };
    // Baseline: the first line's baseline at the box's top edge (world-space text: on its origin,
    // as text was placed before alignment existed); in a UI rectangle like Top.
    enum class TextAlignV { Top = 0, Middle, Bottom, Baseline };
    // Text that does not fit the box: drawn past it (Overflow), cut off at whole characters and
    // lines (Truncate), or cut off with "…" where it was cut (Ellipsis).
    enum class TextOverflow { Overflow = 0, Truncate, Ellipsis };

    struct TextLayoutSettings
    {
        TextAlignH AlignH = TextAlignH::Left;
        TextAlignV AlignV = TextAlignV::Middle;
        bool WordWrap = false;             // lines break between words at the box's width
        TextOverflow Overflow = TextOverflow::Overflow;
        float Kerning = 0.0f;              // em added after every character
        float LineSpacing = 0.0f;          // em added to every line's height
    };

    // Font metrics in em (1 = the font size): the font's, else a stand-in (every character 0.55 em
    // wide - headless tests, a font that did not load).
    struct TextMetrics
    {
        const Font* FontPtr = nullptr;
        float Ascent = 0.8f, Descent = -0.2f, LineHeight = 1.2f;

        float Advance(uint32_t codepoint) const;
        bool HasGlyph(uint32_t codepoint) const;
        static TextMetrics Of(const Font* font);
    };

    struct TextLine
    {
        size_t Begin = 0, End = 0; // bytes of the text (End: one past; the spaces a wrapped line ends with are left out)
        float Width = 0.0f;        // px, without the ellipsis
        float X = 0.0f;            // px: where the line starts
        float Baseline = 0.0f;     // px
        bool Ellipsis = false;     // "…" follows it
    };

    struct TextLayoutResult
    {
        std::vector<TextLine> Lines;
        glm::vec2 Size{ 0.0f };     // px: the widest line (with its ellipsis) x the lines' height
        float Top = 0.0f;           // px: the top of the first line
        float EllipsisWidth = 0.0f; // px
        bool Truncated = false;     // characters or lines were left out

        // The box around the lines (their line height, their width with the ellipsis).
        UIRect GetBounds() const;
    };

    namespace TextLayout {

        // Lays `text` out `px` pixels high (1 em) in `box` (px, y up). A box without width: no
        // wrapping and no cutting; the lines align on box.Min.x (Left: start there, Center: centred
        // on it, Right: end there). Without height: no lines are left out; the block aligns on
        // box.Min.y (Top: its top there, Middle: centred, Bottom: its bottom there, Baseline: the
        // first line's baseline there). '\n' always starts a new line.
        TextLayoutResult Layout(std::string_view text, const TextMetrics& metrics, float px, const UIRect& box,
                                const TextLayoutSettings& settings);

        // The width (px) of one line of text (no wrapping; trailing spaces count).
        float MeasureWidth(std::string_view text, const TextMetrics& metrics, float px, float kerning = 0.0f);
        // "…", or "..." for a font without that character.
        std::string_view GetEllipsis(const TextMetrics& metrics);

        // Draws a laid-out block (each line with its ellipsis) at `px`, placed by `transform` (its
        // units: the layout's px). `snap` > 0: line starts and baselines on whole target pixels
        // (`snap` target pixels per unit) - crisp UI text.
        void Draw(std::string_view text, const TextLayoutResult& layout, const Ref<Font>& font, float px, const glm::mat4& transform,
                  const TextParams& params, int entityID = -1, float snap = 0.0f);

    }

}
