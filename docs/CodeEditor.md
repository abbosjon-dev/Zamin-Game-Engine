# The built-in C++ code editor

Inside Zamin Editor you can edit, save and compile C++ scripts and jump from an error to its source line.
Visual Script and C++ work in one project; the existing external editor is still available behind the optional **External editor** button.

## Opening and writing

Open the **Window → Code editor** or **Scripts → Code editor** window. Double-clicking a `.cpp` / `.h` file in the
Content Browser, the source button in the script inspector, the asset inspector and the compiler diagnostics also bring you to this window.
On the left are the Source files, in the middle separate tabs, on the right the engine API and gameplay snippets.
**New script** opens the engine's existing C++ templates; the new file opens in the editor right away.

- C++ syntax colors, a monospace font, line numbers, automatic indent, bracket matching and visible whitespace.
- Selection with the keyboard/mouse, copy/cut/paste, Ctrl+Z / Ctrl+Y undo/redo; Ctrl+click for multiple cursors.
- Ctrl+S — the current file; Ctrl+Shift+S — the changed tabs; there is also **Save all**.
- Ctrl+F / Ctrl+H — the find/replace panel; **Next**, a case-sensitive option and an undoable **Replace all**.
- **Go to line** — jump to a line; clicking a compiler error selects its line/column.
- Ctrl+Space — completes engine API names; an API name or a ready-made snippet can be inserted from the side panel.
- F7 / **Build + reload** — first saves the changed files, then starts the existing CMake build and hot reload flow.

The API list is generated from the names and declarations of the 265 public functions in `ScriptableEntity.h`.
This is not a compiler/LSP based semantic IntelliSense: analysis of your own classes, overload resolution,
project-wide rename and a live C++ type check are not available. Types are checked by the real C++ compiler.
Put a snippet in the right place and adapt the class/field names to your own.

## Build and errors

Clicking an error/warning in the Build window opens its source in the built-in editor. A file's diagnostics also appear in the panel below it.
While a build is running a second build does not start. On a save error or a conflict with a change on disk the build is cancelled;
the text in the editor is kept. Hot reload uses the engine's existing lifecycle rules and stops Play when needed.

In a visual-only project the first C++ build writes the `Binaries/lib<Project>.so` module path to the project settings;
if the settings are not saved the build does not start. A build needs CMake, a C++ compiler and the engine SDK.
The SDK include list given to external game projects contains the visual graph's JSON headers too.

## Protecting the text

Files are read as UTF-8. A BOM, CRLF and the presence or absence of a final newline are kept; the internal buffer uses LF.
Binary/NUL content, invalid UTF-8 and a file or edit larger than 1 MB are rejected. Up to 16 files are open at a time.
Undo is limited per tab to 256 entries / 4 MB of edit text (a single newest large edit is kept).

A save is atomic, with a temporary file and a rename. The file on disk is compared with the copy that was read;
if it was changed outside, it is not overwritten automatically. You choose **Reload disk version** or a separately confirmed **Overwrite disk**.
Compiler/SDK files outside the project open read-only. A save also refuses to leave the project through a symlink.

Once changed text has been idle for about 0.75 seconds, an atomic recovery copy is written into `.zamin/code-recovery/`.
The remaining copies are written when the project changes and when the editor closes. Reopening the file offers **Recover edits** or **Discard recovery**.
The recovery also holds the old disk copy: after a crash, a conflict with a file changed outside is still detected.
A save, or an undo back to the saved state, removes the stale recovery copy.
Closing a tab/window with a changed buffer offers Save / Discard / Cancel. If the OS force-kills the editor, the last written recovery remains.
To restore an original file that was deleted or moved on disk, the `Text` value in the recovery JSON copy is kept; there is no automatic file undelete.

## Checks and components

`ZaminTests --filter CodeDocument` checks UTF-8/BOM/CRLF, conflicts, crash recovery, stale recovery, find/replace,
the symlink boundary and that the draft is kept on a write error.

`python3 Tests/code_api_catalog.py --check` checks that the helper API catalogue matches the header.
To update it: `python3 Tests/code_api_catalog.py`.

The code window uses the MIT-licensed [ImGuiColorTextEdit](https://github.com/santaclose/ImGuiColorTextEdit) widget;
the exact commit and the local changes are kept in the [vendor note](../vendor/ImGuiColorTextEdit/ZAMIN-NOTES.md), along with the [license](../vendor/ImGuiColorTextEdit/LICENSE).
The widget is linked only into ZaminEditor; the Player and the engine without UI do not load the new code editor library.
The [license](../Resources/fonts/DejaVuSansMono-LICENSE.txt) of the DejaVu Sans Mono font is distributed with the font.

The regression test in a real window:

```bash
ZAMIN_BUILD_DIR=build-review LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=2 \
  xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' python3 Tests/code_editor_ui_test.py
```

The test needs xdotool and the ImageMagick `import` tool. It runs a real C++ build in a temporary project:
Ctrl+S, undo/redo, Ctrl+Space, a deliberate compiler error, jumping to the diagnostic's line/column, the fix/hot reload and the recovery are checked.
2026-11-10: the 9 CodeDocument tests and all 691 unit tests passed; the GUI flow above was tested in a Linux/Mesa environment.
