#include "Zamin/UI/TextLayout.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/Renderer2D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin {

    float TextMetrics::Advance(uint32_t codepoint) const
    {
        if (codepoint == '\t')
            codepoint = ' ';
        if (!FontPtr)
            return 0.55f;
        const Glyph* g = FontPtr->GetGlyph(codepoint);
        return g ? g->Advance : 0.0f;
    }

    bool TextMetrics::HasGlyph(uint32_t codepoint) const
    {
        return !FontPtr || FontPtr->HasGlyph(codepoint);
    }

    TextMetrics TextMetrics::Of(const Font* font)
    {
        TextMetrics m;
        if (font && font->IsLoaded())
        {
            m.FontPtr = font;
            m.Ascent = font->GetAscent();
            m.Descent = font->GetDescent();
            m.LineHeight = font->GetLineHeight();
        }
        return m;
    }

    namespace {

        struct Char
        {
            size_t Begin, End; // bytes
            float Advance;     // px (kerning included)
            bool Space;        // a place to break (the spaces go with the break)
            bool BreakAfter;   // a hyphen: the line may end after it
        };

        bool IsSpace(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == 0x3000; }

        // The width of chars [begin, end) without the spaces it ends with.
        float LineWidth(const std::vector<Char>& chars, size_t begin, size_t& end)
        {
            while (end > begin && chars[end - 1].Space)
                end--;
            float w = 0.0f;
            for (size_t k = begin; k < end; k++)
                w += chars[k].Advance;
            return w;
        }

    }

    UIRect TextLayoutResult::GetBounds() const
    {
        if (Lines.empty())
            return { { 0.0f, Top }, { 0.0f, Top } };
        float minX = Lines.front().X, maxX = minX;
        for (const TextLine& l : Lines)
        {
            minX = std::min(minX, l.X);
            maxX = std::max(maxX, l.X + l.Width + (l.Ellipsis ? EllipsisWidth : 0.0f));
        }
        return { { minX, Top - Size.y }, { maxX, Top } };
    }

    namespace TextLayout {

        std::string_view GetEllipsis(const TextMetrics& metrics)
        {
            return metrics.HasGlyph(0x2026) ? std::string_view("\xE2\x80\xA6") : std::string_view("...");
        }

        float MeasureWidth(std::string_view text, const TextMetrics& metrics, float px, float kerning)
        {
            float w = 0.0f;
            size_t i = 0;
            while (i < text.size())
            {
                const uint32_t cp = DecodeUTF8(text, i);
                if (cp == '\r' || cp == '\n')
                    continue;
                w += (metrics.Advance(cp) + kerning) * px;
            }
            return w;
        }

        TextLayoutResult Layout(std::string_view text, const TextMetrics& metrics, float px, const UIRect& box, const TextLayoutSettings& settings)
        {
            TextLayoutResult result;
            px = std::max(px, 0.0f);
            const float maxWidth = box.GetWidth();
            const float maxHeight = box.GetHeight();
            const bool bounded = maxWidth > 0.0f;
            const bool cut = settings.Overflow != TextOverflow::Overflow;
            const bool ellipsis = settings.Overflow == TextOverflow::Ellipsis;
            const float lineAdvance = (metrics.LineHeight + settings.LineSpacing) * px;
            const std::string_view dots = GetEllipsis(metrics);
            result.EllipsisWidth = MeasureWidth(dots, metrics, px, settings.Kerning);

            // ---- lines: every paragraph ('\n'), wrapped at the box's width
            std::vector<Char> chars;
            size_t i = 0;
            bool more = true;
            while (more)
            {
                // One paragraph.
                chars.clear();
                const size_t paragraphBegin = i;
                more = false;
                while (i < text.size())
                {
                    const size_t begin = i;
                    const uint32_t cp = DecodeUTF8(text, i);
                    if (cp == '\n')
                    {
                        more = true;
                        break;
                    }
                    if (cp == '\r')
                        continue;
                    chars.push_back({ begin, i, (metrics.Advance(cp) + settings.Kerning) * px, IsSpace(cp), cp == '-' || cp == 0x2013 });
                }
                if (chars.empty())
                {
                    result.Lines.push_back({ paragraphBegin, paragraphBegin, 0.0f });
                    continue;
                }
                size_t start = 0;
                const size_t n = chars.size();
                while (start < n)
                {
                    size_t end = n;
                    if (settings.WordWrap && bounded)
                    {
                        // As many characters as fit; then back to the last space / hyphen in them
                        // (none: the word is broken where it no longer fits - one character at least).
                        float w = 0.0f;
                        size_t k = start;
                        size_t lastBreak = 0;  // the line may end before chars[lastBreak] (0: no place yet)
                        bool overflow = false, content = false;
                        while (k < n)
                        {
                            const Char& c = chars[k];
                            if (c.Space)
                            {
                                if (content) // spaces a line starts with are no place to break
                                    lastBreak = k;
                                w += c.Advance;
                                k++;
                                continue;
                            }
                            if (w + c.Advance > maxWidth + 0.01f && k > start)
                            {
                                overflow = true;
                                break;
                            }
                            w += c.Advance;
                            k++;
                            content = true;
                            if (c.BreakAfter && k < n)
                                lastBreak = k;
                        }
                        if (overflow)
                            end = lastBreak > start ? lastBreak : k;
                    }
                    size_t lineEnd = end;
                    const float width = LineWidth(chars, start, lineEnd);
                    TextLine line;
                    line.Begin = chars[start].Begin;
                    line.End = lineEnd > start ? chars[lineEnd - 1].End : chars[start].Begin;
                    line.Width = width;
                    result.Lines.push_back(line);
                    start = end;
                    // The spaces at a break go with it.
                    if (start < n)
                        while (start < n && chars[start].Space)
                            start++;
                }
            }

            // ---- what does not fit
            auto cutLine = [&](TextLine& line, float room) {
                // Whole characters off the end (and the spaces before the cut) until it fits `room`.
                while (line.End > line.Begin && line.Width > room + 0.01f)
                {
                    size_t k = line.End;
                    do { k--; } while (k > line.Begin && ((unsigned char)text[k] & 0xC0) == 0x80);
                    size_t j = k;
                    const uint32_t cp = DecodeUTF8(text, j);
                    line.Width -= (metrics.Advance(cp) + settings.Kerning) * px;
                    line.End = k;
                    result.Truncated = true;
                }
                while (line.End > line.Begin && (text[line.End - 1] == ' ' || text[line.End - 1] == '\t'))
                {
                    line.End--;
                    line.Width -= (metrics.Advance(' ') + settings.Kerning) * px;
                }
                line.Width = std::max(0.0f, line.Width);
            };
            if (cut && maxHeight > 0.0f && lineAdvance > 0.0f)
            {
                // Only the lines that fit (one at least).
                const size_t fit = std::max<size_t>(1, (size_t)std::floor((maxHeight + 0.01f) / lineAdvance));
                if (result.Lines.size() > fit)
                {
                    result.Lines.resize(fit);
                    result.Truncated = true;
                    if (ellipsis)
                        result.Lines.back().Ellipsis = true;
                }
            }
            if (cut && bounded)
                for (TextLine& line : result.Lines)
                {
                    const bool tooWide = line.Width > maxWidth + 0.01f;
                    if (ellipsis && (tooWide || line.Ellipsis))
                    {
                        line.Ellipsis = true;
                        cutLine(line, std::max(0.0f, maxWidth - result.EllipsisWidth));
                    }
                    else if (tooWide)
                        cutLine(line, maxWidth);
                }
            else if (ellipsis)
                for (TextLine& line : result.Lines)
                    if (line.Ellipsis)
                    {
                        size_t end = line.End;
                        while (end > line.Begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
                            end--;
                        line.Width -= (float)(line.End - end) * (metrics.Advance(' ') + settings.Kerning) * px;
                        line.End = end;
                    }

            // ---- placing them
            const float height = (float)result.Lines.size() * lineAdvance;
            float top = 0.0f;
            const bool hasHeight = maxHeight > 0.0f;
            switch (settings.AlignV)
            {
                case TextAlignV::Top:      top = hasHeight ? box.Max.y : box.Min.y; break;
                case TextAlignV::Middle:   top = (hasHeight ? box.GetCenter().y : box.Min.y) + height * 0.5f; break;
                case TextAlignV::Bottom:   top = box.Min.y + height; break;
                case TextAlignV::Baseline: top = hasHeight ? box.Max.y : box.Min.y + metrics.Ascent * px; break;
            }
            float widest = 0.0f;
            for (size_t k = 0; k < result.Lines.size(); k++)
            {
                TextLine& line = result.Lines[k];
                const float w = line.Width + (line.Ellipsis ? result.EllipsisWidth : 0.0f);
                widest = std::max(widest, w);
                switch (settings.AlignH)
                {
                    case TextAlignH::Left:   line.X = box.Min.x; break;
                    case TextAlignH::Center: line.X = (bounded ? box.GetCenter().x : box.Min.x) - w * 0.5f; break;
                    case TextAlignH::Right:  line.X = (bounded ? box.Max.x : box.Min.x) - w; break;
                }
                line.Baseline = top - metrics.Ascent * px - (float)k * lineAdvance;
            }
            result.Size = { widest, height };
            result.Top = top;
            return result;
        }

        void Draw(std::string_view text, const TextLayoutResult& layout, const Ref<Font>& font, float px, const glm::mat4& transform,
                  const TextParams& params, int entityID, float snap)
        {
            if (!font || !font->IsLoaded() || px <= 0.0f || params.Color.a <= 0.0f)
                return;
            const TextMetrics metrics = TextMetrics::Of(font.get());
            const std::string_view dots = GetEllipsis(metrics);
            // DrawString measures in em; kerning / line spacing are em too.
            const TextParams line{ params.Color, params.Kerning, 0.0f };
            for (const TextLine& l : layout.Lines)
            {
                if (l.End <= l.Begin && !l.Ellipsis)
                    continue;
                float x = l.X, y = l.Baseline;
                if (snap > 0.0f)
                {
                    x = std::round(x * snap) / snap;
                    y = std::round(y * snap) / snap;
                }
                const glm::mat4 m = transform * glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(px, px, 1.0f));
                if (l.End > l.Begin)
                    Renderer2D::DrawString(text.substr(l.Begin, l.End - l.Begin), font, m, line, entityID);
                if (l.Ellipsis)
                {
                    const glm::mat4 e = m * glm::translate(glm::mat4(1.0f), glm::vec3(l.Width / px, 0.0f, 0.0f));
                    Renderer2D::DrawString(dots, font, e, line, entityID);
                }
            }
        }

    }

}
