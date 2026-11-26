#pragma once
#include <imgui.h>
#include <string>
#include <string_view>
// Layout helpers of the Visual Script properties column. The column is narrow (about 320 px), so a label sits
// above its full-width field (Field), or left of it only when both fit (InlineLabel); text that is wider than the
// column is cut with "..." and shows the whole text in a tooltip (FitText). Nothing is drawn past the right edge.
//
// Automation (ZAMIN_EDITOR_VISUAL_LAYOUT=<file.json>): every helper records its item's screen rectangle under a
// key "<scope>/<id>" (the scope stack below, the id is the part after "###", else the visible text) and whether it
// crosses the right edge of its window. Flush writes the rectangles of the current frame; GUI tests click them
// instead of fixed coordinates and assert that no recorded item is clipped.
namespace Zamin::GraphLayout {
    // "Text###Id" -> "Text"; "Text##x" -> "Text".
    std::string_view Visible(const char* label);
    // A single line of text cut to the remaining width with "..." (the full text in a tooltip when cut).
    void FitText(const char* text, bool disabled = false);
    // A label on its own line; the next item spans the column. Returns the hidden ImGui label ("##" + label).
    std::string Field(const char* label);
    // A label left of the next item when the label fits the label column and room remains; otherwise above it.
    // The next item spans the rest of the row.
    void InlineLabel(const char* text, float column = 112);
    // ImGui::SameLine() when an item of the given width still fits this row (else the item starts a new row).
    void SameLineIfFits(float width);
    float ButtonWidth(const char* label, bool small = false);
    // A button / small button that wraps to a new row when it does not fit, recorded for automation.
    bool Button(const char* label);
    bool SmallButton(const char* label);
    // Text cut with "..." (whole UTF-8 characters) to fit the width; cut tells whether it was.
    std::string Cut(const std::string& text, float width, bool& cut);
    // A tree node whose text is cut to the row (full text in a tooltip), recorded under key (default: the text).
    bool TreeNode(const char* id, const std::string& text, const std::string& key = {});
    // Automation: the key scope (function/struct/enum names, section names).
    void PushScope(const std::string& scope);
    void PopScope();
    // Records the last item under the scope and the label's id.
    void Record(const char* label);
    void RecordKey(const std::string& key);
    bool Recording();
    // Writes the frame's rectangles (and the named region rectangle) to the automation file and starts a new frame.
    void Flush();
    void RecordRegion(const char* name);
}
