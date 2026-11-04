#pragma once

#include <filesystem>

#include <glm/glm.hpp>

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
        static uint32_t PlaySpatial(const std::filesystem::path& path, const glm::vec3& position, float volume = 1.0f,
                                    float pitch = 1.0f, bool loop = false, float minDistance = 1.0f, float maxDistance = 30.0f);
        static void SetSoundPosition(uint32_t handle, const glm::vec3& position);
        static void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
        static bool IsPlaying(uint32_t handle);
        static void Stop(uint32_t handle);
        static void StopAll();
        static void SetMasterVolume(float volume);
    };

}
