#include "Zamin/UI/TextEdit.h"

#include <algorithm>
#include <cmath>

namespace Zamin::TextEdit {

    static bool IsContinuation(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

    // The codepoint starting at byte `i` (a stray byte reads as itself).
    static uint32_t DecodeAt(std::string_view s, size_t i)
    {
        if (i >= s.size())
            return 0;
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80)
            return c;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        uint32_t cp = c & (0x3F >> extra);
        for (int k = 1; k <= extra && i + k < s.size() && IsContinuation(s[i + k]); k++)
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        return cp;
    }

    // ------------------------------------------------------------------ UTF-8
    void AppendUTF8(std::string& out, uint32_t cp)
    {
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            cp = 0xFFFD; // not a character: the replacement character
        if (cp < 0x80)
            out += (char)cp;
        else if (cp < 0x800)
        {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    std::string ToUTF8(uint32_t codepoint)
    {
        std::string s;
        AppendUTF8(s, codepoint);
        return s;
    }

    size_t CountCodepoints(std::string_view text)
    {
        size_t n = 0;
        for (char c : text)
            if (!IsContinuation(c))
                n++;
        return n;
    }

    size_t NextBoundary(std::string_view text, size_t i)
    {
        if (i >= text.size())
            return text.size();
        i++;
        while (i < text.size() && IsContinuation(text[i]))
            i++;
        return i;
    }

    size_t PrevBoundary(std::string_view text, size_t i)
    {
        i = std::min(i, text.size());
        if (i == 0)
            return 0;
        i--;
        while (i > 0 && IsContinuation(text[i]))
            i--;
        return i;
    }

    size_t SnapToBoundary(std::string_view text, size_t i)
    {
        i = std::min(i, text.size());
        while (i > 0 && i < text.size() && IsContinuation(text[i]))
            i--;
        return i;
    }

    size_t ByteOffsetOf(std::string_view text, size_t codepointIndex)
    {
        size_t i = 0;
        for (size_t n = 0; n < codepointIndex && i < text.size(); n++)
            i = NextBoundary(text, i);
        return i;
    }

    size_t CodepointIndexOf(std::string_view text, size_t byteOffset)
    {
        return CountCodepoints(text.substr(0, SnapToBoundary(text, byteOffset)));
    }

    bool IsWordCharacter(uint32_t cp)
    {
        if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
            return true;
        // The apostrophes of Uzbek Latin are part of its words: oʻ, gʻ (U+02BB, also typed as ' or ‘)
        // and the tutuq belgisi ʼ (U+02BC, also ’).
        if (cp == '\'' || cp == 0x02BB || cp == 0x02BC || cp == 0x2018 || cp == 0x2019)
            return true;
        if (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7) // Latin-1 letters, Latin Extended-A / B
            return true;
        if (cp >= 0x400 && cp <= 0x52F) // Cyrillic (Uzbek Cyrillic ў қ ғ ҳ included)
            return true;
        return false;
    }

    size_t PrevWord(std::string_view text, size_t i)
    {
        i = SnapToBoundary(text, i);
        while (i > 0)
        {
            size_t p = PrevBoundary(text, i);
            if (IsWordCharacter(DecodeAt(text, p)))
                break;
            i = p;
        }
        while (i > 0)
        {
            size_t p = PrevBoundary(text, i);
            if (!IsWordCharacter(DecodeAt(text, p)))
                break;
            i = p;
        }
        return i;
    }

    size_t NextWord(std::string_view text, size_t i)
    {
        i = SnapToBoundary(text, i);
        while (i < text.size() && !IsWordCharacter(DecodeAt(text, i)))
            i = NextBoundary(text, i);
        while (i < text.size() && IsWordCharacter(DecodeAt(text, i)))
            i = NextBoundary(text, i);
        return i;
    }

    // ------------------------------------------------------------------ content
    static bool IsDigit(uint32_t cp) { return cp >= '0' && cp <= '9'; }

    bool Accepts(UIContentType type, std::string_view text, size_t at, uint32_t cp)
    {
        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F) || cp == 0xFEFF)
            return false;
        const bool leadingMinus = !text.empty() && text[0] == '-';
        switch (type)
        {
            case UIContentType::Standard:
            case UIContentType::Password:
                return true;
            case UIContentType::Integer:
            case UIContentType::Decimal:
            {
                if (at == 0 && leadingMinus)
                    return false; // nothing goes before the sign
                if (IsDigit(cp))
                    return true;
                if (cp == '-')
                    return at == 0 && !leadingMinus;
                if (cp == '.' && type == UIContentType::Decimal)
                    return text.find('.') == std::string_view::npos;
                return false;
            }
            case UIContentType::Alphanumeric:
                return IsWordCharacter(cp);
        }
        return true;
    }

    std::string GetDisplayText(std::string_view text, UIContentType type)
    {
        if (type != UIContentType::Password)
            return std::string(text);
        std::string dots;
        const size_t n = CountCodepoints(text);
        dots.reserve(n * 3);
        for (size_t i = 0; i < n; i++)
            dots += "\xE2\x80\xA2"; // U+2022 bullet
        return dots;
    }

    std::string Filter(std::string_view text, UIContentType type, int characterLimit)
    {
        TextEditState state;
        Insert(state, text, type, characterLimit);
        return state.Text;
    }

    // ------------------------------------------------------------------ editing
    bool Insert(TextEditState& state, std::string_view input, UIContentType type, int characterLimit)
    {
        TextEditState s = state;
        Clamp(s);
        DeleteSelection(s);
        size_t count = CountCodepoints(s.Text);
        bool inserted = false;
        size_t i = 0;
        while (i < input.size())
        {
            uint32_t cp = DecodeAt(input, i);
            i = NextBoundary(input, i);
            if (cp == '\r')
                continue;
            if (cp == '\n' || cp == '\t')
                cp = ' '; // one line: pasted line breaks become spaces
            if (cp == ',' && type == UIContentType::Decimal)
                cp = '.';
            if (characterLimit > 0 && count >= (size_t)characterLimit)
                break;
            if (!Accepts(type, s.Text, s.Caret, cp))
                continue;
            std::string bytes = ToUTF8(cp);
            s.Text.insert(s.Caret, bytes);
            s.Caret += bytes.size();
            s.Anchor = s.Caret;
            count++;
            inserted = true;
        }
        if (!inserted)
            return false; // nothing accepted: the selection stays as it was
        state = std::move(s);
        return true;
    }

    bool DeleteSelection(TextEditState& state)
    {
        if (!state.HasSelection())
            return false;
        size_t start = state.GetSelectionStart(), end = state.GetSelectionEnd();
        state.Text.erase(start, end - start);
        state.Caret = state.Anchor = start;
        return true;
    }

    bool Backspace(TextEditState& state, bool word)
    {
        Clamp(state);
        if (DeleteSelection(state))
            return true;
        if (state.Caret == 0)
            return false;
        size_t start = word ? PrevWord(state.Text, state.Caret) : PrevBoundary(state.Text, state.Caret);
        state.Text.erase(start, state.Caret - start);
        state.Caret = state.Anchor = start;
        return true;
    }

    bool Delete(TextEditState& state, bool word)
    {
        Clamp(state);
        if (DeleteSelection(state))
            return true;
        if (state.Caret >= state.Text.size())
            return false;
        size_t end = word ? NextWord(state.Text, state.Caret) : NextBoundary(state.Text, state.Caret);
        state.Text.erase(state.Caret, end - state.Caret);
        state.Anchor = state.Caret;
        return true;
    }

    void MoveLeft(TextEditState& state, bool select, bool word)
    {
        Clamp(state);
        if (!select && state.HasSelection())
        {
            state.Caret = state.Anchor = state.GetSelectionStart();
            return;
        }
        SetCaret(state, word ? PrevWord(state.Text, state.Caret) : PrevBoundary(state.Text, state.Caret), select);
    }

    void MoveRight(TextEditState& state, bool select, bool word)
    {
        Clamp(state);
        if (!select && state.HasSelection())
        {
            state.Caret = state.Anchor = state.GetSelectionEnd();
            return;
        }
        SetCaret(state, word ? NextWord(state.Text, state.Caret) : NextBoundary(state.Text, state.Caret), select);
    }

    void MoveHome(TextEditState& state, bool select) { SetCaret(state, 0, select); }
    void MoveEnd(TextEditState& state, bool select) { SetCaret(state, state.Text.size(), select); }

    void SelectAll(TextEditState& state)
    {
        state.Anchor = 0;
        state.Caret = state.Text.size();
    }

    void SetCaret(TextEditState& state, size_t at, bool select)
    {
        state.Caret = SnapToBoundary(state.Text, at);
        if (!select)
            state.Anchor = state.Caret;
        else
            state.Anchor = SnapToBoundary(state.Text, state.Anchor);
    }

    std::string GetSelectedText(const TextEditState& state)
    {
        size_t start = std::min(state.GetSelectionStart(), state.Text.size());
        size_t end = std::min(state.GetSelectionEnd(), state.Text.size());
        return state.Text.substr(start, end - start);
    }

    void Clamp(TextEditState& state)
    {
        state.Caret = SnapToBoundary(state.Text, state.Caret);
        state.Anchor = SnapToBoundary(state.Text, state.Anchor);
    }

    // ------------------------------------------------------------------ layout
    size_t CaretIndexAt(const std::vector<float>& offsets, float x)
    {
        size_t best = 0;
        float bestDistance = INFINITY;
        for (size_t i = 0; i < offsets.size(); i++)
        {
            float d = std::abs(offsets[i] - x);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    }

    float ScrollToCaret(float scroll, float caretX, float textWidth, float width, float margin)
    {
        width = std::max(1.0f, width);
        if (caretX - scroll < margin)
            scroll = caretX - margin;
        if (caretX - scroll > width - margin)
            scroll = caretX - width + margin;
        float maxScroll = std::max(0.0f, textWidth - width + margin);
        return std::clamp(scroll, 0.0f, maxScroll);
    }

}
