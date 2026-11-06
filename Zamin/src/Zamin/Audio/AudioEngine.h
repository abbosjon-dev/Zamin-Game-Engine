#pragma once

#include "Zamin/Audio/AudioMixer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <glm/glm.hpp>

namespace Zamin {

    // How a sound is played (AudioEngine::Play).
    struct SoundParams
    {
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        // The mixer group the sound plays in (an unknown name: SFX, with a warning once).
        // "" = straight to the output, past the mixer (the editor's file previews).
        std::string Group = AudioSettings::SFX;
        // true: streamed from disk while it plays; false: only when the file is larger than the
        // project's threshold (AudioSettings::StreamThresholdMB), otherwise decoded into memory.
        bool Stream = false;
        // 3D sound: volume and pan depend on where the listener is.
        bool Spatial = false;
        glm::vec3 Position{ 0.0f };
        float MinDistance = 1.0f;
        float MaxDistance = 30.0f;
    };

    // A mixer group's level meter (post-fader peaks as linear gains, 1 = full scale).
    struct AudioLevel
    {
        float Left = 0.0f, Right = 0.0f; // falls back smoothly after a peak
        float Hold = 0.0f;               // the highest peak of the last moment (held ~1.5 s)
    };

    // Thin wrapper over miniaudio. All functions are safe to call even when
    // no audio device is available (they simply do nothing).
    //
    // Mixer: every sound plays in a group (bus) of the mixer - Master > Music, SFX, UI and the
    // project's own groups (AudioSettings). A group's fader, mute and solo apply to everything
    // routed to it and to its child groups.
    // Music: one track at a time in the Music group (streamed), with fades and crossfades; it
    // belongs to the game, not to a scene, so it keeps playing across scene changes.
    class AudioEngine
    {
    public:
        // withoutDevice: a working engine that outputs to nothing (headless tests, servers):
        // sounds load, start and report IsPlaying, but nothing is heard and they only advance
        // when RenderOffline mixes them.
        static void Init(bool withoutDevice = false);
        static void Shutdown();
        static bool IsAvailable();
        // False for a device-less engine (and without an engine).
        static bool HasDevice();
        // Once per frame (the application does it): music fades, the level meters, finished
        // sounds released. `deltaSeconds` is real time.
        static void Update(float deltaSeconds);
        // Device-less engines only (tests): mixes `seconds` of audio the way an output device
        // would pull it - sounds advance, the meters see their level. Nothing for a real device.
        static void RenderOffline(float seconds);

        // Returns a handle (0 = failed).
        static uint32_t Play(const std::filesystem::path& path, const SoundParams& params);
        // SFX group.
        static uint32_t Play(const std::filesystem::path& path, float volume = 1.0f, float pitch = 1.0f, bool loop = false);
        static uint32_t PlaySpatial(const std::filesystem::path& path, const glm::vec3& position, float volume = 1.0f,
                                    float pitch = 1.0f, bool loop = false, float minDistance = 1.0f, float maxDistance = 30.0f);
        static void SetSoundPosition(uint32_t handle, const glm::vec3& position);
        static bool IsPlaying(uint32_t handle);
        // Playback position and length of a playing sound, in seconds (0 for an unknown handle).
        static float GetCursor(uint32_t handle);
        static float GetLength(uint32_t handle);
        // Jumps to `seconds` into the sound (clamped to its length).
        static void Seek(uint32_t handle, float seconds);
        static void SetVolume(uint32_t handle, float volume);
        // The sound's own volume (before its group; 0 for an unknown handle).
        static float GetVolume(uint32_t handle);
        // Streamed from disk (not decoded into memory).
        static bool IsStreaming(uint32_t handle);
        // The mixer group the sound plays in ("" = past the mixer or unknown handle).
        static std::string GetSoundGroup(uint32_t handle);
        // Whether a file played with Stream = `requested` is streamed.
        static bool ShouldStream(const std::filesystem::path& path, bool requested);
        static void Stop(uint32_t handle);
        // Everything, music included (the editor leaving play mode, shutdown).
        static void StopAll();
        // Every sound but the music (a scene stops: the music goes on into the next one).
        static void StopAllExceptMusic();
        // Sounds that are playing right now.
        static size_t GetPlayingCount();
        // The engine's output volume (after Master).
        static void SetMasterVolume(float volume);

        // ---- listener (Scene::UpdateAudio: the active Audio Listener, else the primary camera)
        static void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
        static void SetListenerVelocity(const glm::vec3& velocity);
        static glm::vec3 GetListenerPosition();
        static glm::vec3 GetListenerVelocity();

        // ---- mixer groups
        // Replaces the mixer (groups made / removed / moved, faders set). Kept when there is no
        // engine yet: Init builds it.
        static void SetMixerSettings(const AudioSettings& settings);
        static const AudioSettings& GetMixerSettings();
        static bool HasGroup(const std::string& group);
        // False for unknown groups.
        static bool SetGroupVolume(const std::string& group, float volume);
        static float GetGroupVolume(const std::string& group); // the fader (0 for unknown)
        static bool SetGroupMuted(const std::string& group, bool muted);
        static bool IsGroupMuted(const std::string& group);
        static bool SetGroupSolo(const std::string& group, bool solo);
        static bool IsGroupSolo(const std::string& group);
        // What a sound in the group is multiplied by: its fader and all its parents' (mute / solo
        // included).
        static float GetGroupEffectiveVolume(const std::string& group);
        static AudioLevel GetGroupLevel(const std::string& group);
        // Sounds playing in the group itself (not in its children).
        static size_t GetGroupVoiceCount(const std::string& group);

        // ---- music (the Music group, always streamed; one track, the old one fades out)
        // Plays `path` (fading in over `fadeSeconds`). A different track playing now fades out
        // first and the new one starts after it. The track playing already: nothing changes.
        // False when the file cannot be played (the current music then goes on).
        static bool PlayMusic(const std::filesystem::path& path, float fadeSeconds = 0.5f, bool loop = true);
        // The current track fades out while `path` fades in, both over `seconds`.
        static bool CrossfadeMusic(const std::filesystem::path& path, float seconds = 2.0f, bool loop = true);
        static void StopMusic(float fadeSeconds = 0.5f);
        static void PauseMusic();
        static void ResumeMusic();
        static bool IsMusicPaused();
        // A track is playing or waiting to start (one only fading out does not count).
        static bool IsMusicPlaying();
        // The current (or waiting) track ("" when none).
        static std::filesystem::path GetMusic();
        // The music's own volume (on top of the Music group's fader).
        static void SetMusicVolume(float volume);
        static float GetMusicVolume();
        // Tracks heard right now (2 during a crossfade).
        static size_t GetMusicVoiceCount();
        // The current track's sound (0: none, or still waiting for the old one to fade out).
        static uint32_t GetMusicHandle();
    };

}
