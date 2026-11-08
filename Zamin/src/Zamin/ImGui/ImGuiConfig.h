#pragma once

// Dear ImGui's compile-time options for the engine, the editor and the game modules: imgui.h includes this
// file first (IMGUI_USER_CONFIG, cmake/Dependencies.cmake). Only plain declarations here - ImGui's own
// sources include it too.
//
// IM_ASSERT: ImGui's checks (a Begin without its End, unbalanced Push / Pop, a popup or an ID stack used
// wrongly ...) are kept in every build. A failed one is written to the log as an error (once per place),
// so the editor's smoke test (ZAMIN_EDITOR_OPEN_ALL_PANELS, Tests/editor_smoke.sh) fails on it in Release
// builds as well, where assert() is compiled out. A Debug build still stops there (abort: the crash
// handler's report has the call stack).

namespace Zamin {
    void ImGuiAssertFailed(const char* expression, const char* file, int line);
    // How many ImGui checks failed in this process (the same place counted every time it fails).
    unsigned long long GetImGuiAssertCount();
}

#define IM_ASSERT(_EXPR) ((_EXPR) ? (void)0 : ::Zamin::ImGuiAssertFailed(#_EXPR, __FILE__, __LINE__))
