#pragma once

#include <filesystem>

namespace Zamin {

    // Thin wrapper over miniaudio. All functions are safe to call even when
    // no audio device is available (they simply do nothing).
    class AudioEngine
    {
    public:
        static void Init();
        static void Shutdown();
        static bool IsAvailable();

        // Returns a handle (0 = failed).
        static uint32_t Play(const std::filesystem::path& path, float volume = 1.0f, float pitch = 1.0f, bool loop = false);
        static void Stop(uint32_t handle);
        static void StopAll();
        static void SetMasterVolume(float volume);
    };

}
