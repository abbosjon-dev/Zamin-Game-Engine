#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Zamin {

    // The editor's smoke test (ZAMIN_EDITOR_OPEN_ALL_PANELS=1, EditorSmokeTest.cpp): its steps, where it is
    // and what it found.
    struct EditorSmokeStep
    {
        std::string Name;
        std::function<void()> Open;   // when the step begins
        std::function<void()> Close;  // after its frames, before the editor's state is put back
        int Frames = 3;
        bool Screenshot = false;      // ZAMIN_EDITOR_SMOKE_SHOTS=<folder>: its last frame saved there
    };

    struct EditorSmokeTest
    {
        std::vector<EditorSmokeStep> Steps;
        size_t Current = 0;
        int Frame = -1;                    // frames of the current step drawn (-1: not begun yet)
        bool Running = false;
        uint64_t LogSeen = 0;              // log messages already looked at (Log::GetTotalWritten)
        std::string LastStep;              // the step the previous frame drew: what is found now is its
        int Errors = 0, Frames = 0;
        uint32_t GLErrors = 0;             // OpenGL error flags (glGetError)
        unsigned long long ImGuiAssertsAtStart = 0;
        std::vector<std::string> Failures; // "<step>: <message>" (the first ones)
        std::string ShotDir;
        std::chrono::steady_clock::time_point Start;
        std::vector<std::pair<bool*, bool>> SavedFlags; // the windows as they were (put back after every step)
        std::vector<bool> SavedViews;                   // the extra scene views open or not
        uint64_t TempEntity = 0;                        // the entity every component is added to in turn
    };

}
