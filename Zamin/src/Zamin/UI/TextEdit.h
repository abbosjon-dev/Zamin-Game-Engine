#pragma once

// Single-line text editing for UI input fields: UTF-8 aware caret movement, selection, typing,
// deleting, content filters and the shown text (passwords). No GPU - shared by the scene's input
// fields and the tests.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // What an input field accepts:
    // - Standard: any text;
    // - Integer: digits and a leading minus;
    // - Decimal: digits, a leading minus and one decimal point (a comma is taken as the point);
    // - Alphanumeric: letters (Latin with the Uzbek oʻ / gʻ and its apostrophes, Cyrillic) and digits;
    // - Password: any text, shown as dots.
    enum class UIContentType { Standard = 0, Integer, Decimal, Alphanumeric, Password };

    // The text being edited and its selection: the caret and the selection's other end (Anchor;
    // the same as the caret: nothing selected), byte offsets on codepoint boundaries.
    struct TextEditState
    {
        std::string Text;
        size_t Caret = 0;
        size_t Anchor = 0;

        bool HasSelection() const { return Caret != Anchor; }
        size_t GetSelectionStart() const { return Caret < Anchor ? Caret : Anchor; }
        size_t GetSelectionEnd() const { return Caret < Anchor ? Anchor : Caret; }
    };

    namespace TextEdit
    {
        // ---- UTF-8
        void AppendUTF8(std::string& out, uint32_t codepoint);
        std::string ToUTF8(uint32_t codepoint);
        size_t CountCodepoints(std::string_view text);
        // The codepoint boundary after / before byte offset `i` (clamped to the text).
        size_t NextBoundary(std::string_view text, size_t i);
        size_t PrevBoundary(std::string_view text, size_t i);
        // `i` moved back onto the start of the codepoint it is inside of.
        size_t SnapToBoundary(std::string_view text, size_t i);
        // Byte offset of the n-th codepoint (the end for n past it) and back.
        size_t ByteOffsetOf(std::string_view text, size_t codepointIndex);
        size_t CodepointIndexOf(std::string_view text, size_t byteOffset);
        // Word jumps (Ctrl + arrows): to the start of the previous / the end of the next word.
        size_t PrevWord(std::string_view text, size_t i);
        size_t NextWord(std::string_view text, size_t i);
        // A letter or digit of a word (Latin incl. Latin-1 / Extended-A, Uzbek oʻ gʻ marks, Cyrillic).
        bool IsWordCharacter(uint32_t codepoint);

        // ---- content
        // Whether `codepoint` may be inserted into `text` at byte offset `at` (what is selected is
        // already gone) for the content type. Control characters never are.
        bool Accepts(UIContentType type, std::string_view text, size_t at, uint32_t codepoint);
        // The text an input field shows: the text itself, or one dot per character for a password.
        std::string GetDisplayText(std::string_view text, UIContentType type);
        // `text` with what the content type does not accept left out (and cut to the limit).
        std::string Filter(std::string_view text, UIContentType type, int characterLimit);

        // ---- editing (true when the text changed)
        // Replaces the selection with `input`, one codepoint at a time, keeping those the content
        // type accepts and stopping at the limit (codepoints, 0 = none). The caret ends after it.
        bool Insert(TextEditState& state, std::string_view input, UIContentType type, int characterLimit);
        bool DeleteSelection(TextEditState& state);
        // Backspace / Delete: the selection, else the codepoint (word: up to the word boundary)
        // before / after the caret.
        bool Backspace(TextEditState& state, bool word);
        bool Delete(TextEditState& state, bool word);
        // ---- caret (select: the anchor stays, extending the selection)
        void MoveLeft(TextEditState& state, bool select, bool word);
        void MoveRight(TextEditState& state, bool select, bool word);
        void MoveHome(TextEditState& state, bool select);
        void MoveEnd(TextEditState& state, bool select);
        void SelectAll(TextEditState& state);
        // The caret onto byte offset `at` (selecting from the anchor when `select`).
        void SetCaret(TextEditState& state, size_t at, bool select);
        std::string GetSelectedText(const TextEditState& state);
        // Caret and anchor clamped into the text, onto codepoint boundaries (after the text was set).
        void Clamp(TextEditState& state);

        // ---- layout
        // The caret positions of a laid out text: offsets[i] = x of the boundary before codepoint i
        // (offsets.size() = codepoints + 1). The codepoint index nearest to `x`.
        size_t CaretIndexAt(const std::vector<float>& offsets, float x);
        // The new horizontal scroll (px) of a field `width` px wide so the caret at `caretX` stays in
        // view with `margin` px around it; the text is never scrolled past its own end.
        float ScrollToCaret(float scroll, float caretX, float textWidth, float width, float margin);
    }

}
