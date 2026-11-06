#pragma once

#include "Zamin/Audio/AudioMixer.h"

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace Zamin {

    struct SoundParams;

    // The game's audio API (scripts): paths are relative to the project's Assets folder.
    // Music plays in the "Music" mixer group, streamed from disk, one track at a time; it is the
    // game's, not a scene's, so it goes on across LoadScene. The editor stops it (and puts the
    // mixer back to the project's defaults) when play mode ends.
    class Audio
    {
    public:
        // ---- music
        // Plays a track, fading it in. A different track playing now fades out first (over the
        // same time) and the new one starts after it; the track playing already goes on.
        static bool PlayMusic(const std::string& assetPath, float fadeSeconds = 0.5f, bool loop = true);
        // The current track fades out while the new one fades in (both over `seconds`).
        static bool CrossfadeTo(const std::string& assetPath, float seconds = 2.0f, bool loop = true);
        static void StopMusic(float fadeSeconds = 0.5f);
        static void PauseMusic();
        static void ResumeMusic();
        static bool IsMusicPaused();
        static bool IsMusicPlaying();
        // The current track (relative to Assets; "" when none).
        static std::string GetMusic();
        // The music's own volume, on top of the Music group's fader (e.g. ducking under dialogue).
        static void SetMusicVolume(float volume);
        static float GetMusicVolume();

        // ---- mixer groups (Master, Music, SFX, UI and the project's own; false: no such group)
        static bool SetGroupVolume(const std::string& group, float volume);
        static float GetGroupVolume(const std::string& group);
        static bool SetGroupMuted(const std::string& group, bool muted);
        static bool IsGroupMuted(const std::string& group);
        static bool HasGroup(const std::string& group);
        // The mixer as the project set it up (Project settings / Audio Mixer window).
        static void ResetMixer();

        // ---- one-shot sounds (handle for AudioEngine; 0 = not played)
        static uint32_t PlaySound(const std::string& assetPath, float volume = 1.0f, float pitch = 1.0f,
                                  const std::string& group = AudioSettings::SFX);
        static uint32_t PlaySoundAt(const std::string& assetPath, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f,
                                    const std::string& group = AudioSettings::SFX);
        // With everything SoundParams has (random volume / pitch range, priority, instance limit,
        // 3D position, ...).
        static uint32_t PlaySound(const std::string& assetPath, const SoundParams& params);
    };

}
