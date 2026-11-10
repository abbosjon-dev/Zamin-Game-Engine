# ImGuiColorTextEdit

Source: https://github.com/santaclose/ImGuiColorTextEdit
Pinned revision: `264bee49ddc3c789b05d928d09c628649458da47`.
The upstream MIT license is preserved in LICENSE. Compiled only into ZaminEditor.

Local changes: std::regex in place of Boost (no extra binary dependency); undoable InsertText without
clipboard modification; a monotonic edit revision; 256-record / 4 MB undo-text retention per document;
cursor byte-offset accessor; invalid palette fallback; 1 MB paste limit; IMGUI_DEFINE_MATH_OPERATORS redefinition guard. C++ uses the upstream manual tokenizer.
