#pragma once

#include "Zamin/Audio/AudioClip.h"
#include "Zamin/Audio/AudioMixer.h"

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace Zamin {

    struct SoundParams;

    /// The game's audio API (scripts): paths are relative to the project's Assets folder.
    /// Music plays in the "Music" mixer group, streamed from disk, one track at a time; it is the
    /// game's, not a scene's, so it goes on across LoadScene. The editor stops it (and puts the
    /// mixer back to the project's defaults) when play mode ends.
    class Audio
    {
    public:
        /// @name Music
        /// @{
        /// Plays a track, fading it in. A different track playing now fades out first (over the
        /// same time) and the new one starts after it; the track playing already goes on.
        static bool PlayMusic(const std::string& assetPath, float fadeSeconds = 0.5f, bool loop = true);
        /// The current track fades out while the new one fades in (both over `seconds`).
        static bool CrossfadeTo(const std::string& assetPath, float seconds = 2.0f, bool loop = true);
        static void StopMusic(float fadeSeconds = 0.5f);
        static void PauseMusic();
        static void ResumeMusic();
        static bool IsMusicPaused();
        static bool IsMusicPlaying();
        /// The current track (relative to Assets; "" when none).
        static std::string GetMusic();
        /// The music's own volume, on top of the Music group's fader (e.g. ducking under dialogue).
        static void SetMusicVolume(float volume);
        static float GetMusicVolume();
        /// @}

        /// @name Mixer groups
        /// Mixer groups (Master, Music, SFX, UI and the project's own; false: no such group)
        /// @{
        static bool SetGroupVolume(const std::string& group, float volume);
        static float GetGroupVolume(const std::string& group);
        static bool SetGroupMuted(const std::string& group, bool muted);
        static bool IsGroupMuted(const std::string& group);
        static bool HasGroup(const std::string& group);
        /// The mixer as the project set it up (Project settings / Audio Mixer window).
        static void ResetMixer();
        /// A group's low-pass / high-pass (e.g. SFX muffled under water: SetGroupLowPass("SFX", 500)).
        /// Cutoff in Hz, <= 0 switches it off; a change glides there. False: no such group.
        static bool SetGroupLowPass(const std::string& group, float cutoffHz);
        static bool SetGroupHighPass(const std::string& group, float cutoffHz);
        static bool SetGroupFilter(const std::string& group, const AudioFilterSettings& filter);
        static AudioFilterSettings GetGroupFilter(const std::string& group);
        /// @}

        /// @name Player settings
        /// Player settings (a game's options menu): the group faders / mutes and the music
        /// volume are kept with SaveData (key SettingsKey), apart from the project's defaults. The
        /// player and the editor's Play load them when the game starts.
        /// @{
        static constexpr const char* SettingsKey = "Zamin.Audio";
        /// Saves the mixer's faders / mutes and the music volume as they are now (and writes the
        /// save file). False without SaveData storage.
        static bool SaveSettings();
        /// Puts the saved settings over the project's mixer. False when nothing was saved (or it
        /// cannot be read): the mixer stays as it is.
        static bool LoadSettings();
        static bool HasSavedSettings();
        /// Forgets the saved settings and goes back to the project's mixer.
        static void ClearSavedSettings();
        /// @}

        /// @name Sound files
        /// Sound files (paths relative to Assets; their import settings in the .zmeta)
        /// Length in seconds (0: unknown / unreadable) and what the file is - read once per version.
        /// @{
        static float GetClipLength(const std::string& assetPath);
        static const AudioFileInfo& GetClipInfo(const std::string& assetPath);
        /// Keeps the file decoded in memory until Unload (AudioEngine::Preload).
        static bool Preload(const std::string& assetPath);
        static void Unload(const std::string& assetPath);
        static bool IsPreloaded(const std::string& assetPath);
        /// @}

        /// @name 3D sound defaults
        /// 3D sound defaults (Project settings > Audio): the distances new Audio Sources and
        /// PlaySoundAt start with and the distance model of every 3D sound (the project's; the
        /// engine's defaults without a project).
        /// @{
        static const AudioSpatialSettings& GetSpatialDefaults();
        /// @}

        /// @name One-shot sounds
        /// One-shot sounds (handle for AudioEngine; 0 = not played)
        /// @{
        static uint32_t PlaySound(const std::string& assetPath, float volume = 1.0f, float pitch = 1.0f,
                                  const std::string& group = AudioSettings::SFX);
        static uint32_t PlaySoundAt(const std::string& assetPath, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f,
                                    const std::string& group = AudioSettings::SFX);
        /// With everything SoundParams has (random volume / pitch range, priority, instance limit,
        /// 3D position, ...).
        static uint32_t PlaySound(const std::string& assetPath, const SoundParams& params);
        /// Stops a sound played above, fading it out over `fadeSeconds` (0: at once).
        static void StopSound(uint32_t handle, float fadeSeconds = 0.0f);
        /// @}
    };

}
