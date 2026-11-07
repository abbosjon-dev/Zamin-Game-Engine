#pragma once

#include "Zamin/Audio/AudioMixer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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
        // Full volume closer than MinDistance, fading out to MaxDistance (the project's rolloff
        // model). Negative: the project's default (AudioSettings::Spatial, Project settings > Audio).
        float MinDistance = -1.0f;
        float MaxDistance = -1.0f;

        // Random variation, picked anew each time the sound starts: Volume and Pitch are multiplied
        // by a value between x (min) and y (max) - e.g. pitch 0.9..1.1 so repeated footsteps do not
        // all sound the same. 1..1 = none. (AudioEngine::SetRandomSeed makes it repeatable.)
        glm::vec2 VolumeRandom{ 1.0f, 1.0f };
        glm::vec2 PitchRandom{ 1.0f, 1.0f };

        // Voice limits. When AudioSettings::MaxVoices sounds play already, this one takes the place
        // of a less important one (lower Priority, 0..255; equal: by Steal) or is not played.
        int Priority = 128;
        // The same file at most this many times at once (0 = no limit of its own); one more
        // replaces one of them by Steal (None: it is not played).
        int MaxInstances = 0;
        AudioStealPolicy Steal = AudioStealPolicy::Oldest;

        // 3D sound: how strongly its pitch follows the Doppler effect (x the project's
        // AudioSettings::DopplerFactor; 0 = off) and how fast it moves (units per second).
        float DopplerFactor = 1.0f;
        glm::vec3 Velocity{ 0.0f };

        // 3D sound: the way it faces and its directional cone (off: the same all around).
        glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
        AudioConeSettings Cone;

        // Starts silent and fades in over this many seconds (0 = at full volume at once).
        float FadeIn = 0.0f;
        // Low-pass / high-pass on this sound alone (its group's filter comes on top).
        AudioFilterSettings Filter;
    };

    // What a sound is doing right now (AudioEngine::GetSoundInfo): for the inspector and tests.
    struct SoundInfo
    {
        bool Playing = false;   // heard (started and not paused)
        bool Paused = false;
        bool Looping = false;
        bool Spatial = false;
        bool Streamed = false;
        bool Music = false;
        std::string Group;      // "" = past the mixer
        float Time = 0.0f;      // seconds into the sound
        float Length = 0.0f;    // seconds (0: unknown)
        float Volume = 1.0f;    // as asked (SetVolume / SoundParams::Volume)
        float VolumeVariation = 1.0f; // the random factor it got when it started
        float GroupGain = 1.0f;       // its mixer group with the parents (mute / solo included)
        float Attenuation = 1.0f;     // distance from the listener and the cone (3D; 1 for 2D sounds)
        float ConeGain = 1.0f;        // the directional cone's part of Attenuation (1: no cone / inside it)
        float AudibleVolume = 1.0f;   // Volume x VolumeVariation x ClipGain x Fade x GroupGain x Attenuation
        float Pitch = 1.0f;           // as asked
        float PitchVariation = 1.0f;  // the random factor it got when it started
        float DopplerPitch = 1.0f;    // from the source's and the listener's movement (1: none)
        float AppliedPitch = 1.0f;    // Pitch x PitchVariation x DopplerPitch
        float DopplerFactor = 1.0f;   // the sound's own (before the project's)
        glm::vec3 Velocity{ 0.0f };
        float Distance = 0.0f;        // from the listener (3D)
        int Priority = 128;
        float ClipGain = 1.0f;        // the file's own Volume (its import settings)
        float Fade = 1.0f;            // the fade envelope now (0..1: FadeIn, FadeTo, Stop with a fade)
        float FadeTarget = 1.0f;      // where the envelope is going
        bool Stopping = false;        // fading out: released once silent
        bool Preloaded = false;       // its file is kept decoded in memory (AudioEngine::Preload)
        AudioFilterSettings Filter;   // the sound's own filter (not its group's)
        AudioConeSettings Cone;       // 3D: its directional cone
        glm::vec3 Direction{ 0.0f, 0.0f, -1.0f }; // 3D: the way it faces
    };

    // Why a sound ended by itself (AudioEngine::TakeFinishedSounds).
    enum class SoundEndReason
    {
        Finished = 0, // played to its end (not looping)
        Stolen        // stopped by the voice limit to make room for another sound
    };

    // A sound that ended by itself.
    struct FinishedSound
    {
        uint32_t Handle = 0;
        std::filesystem::path File;
        std::string Group;
        SoundEndReason Reason = SoundEndReason::Finished;
    };

    // The voice limit at work (AudioEngine::GetVoiceStats).
    struct AudioVoiceStats
    {
        size_t Voices = 0;       // sounds that count towards the limit now (playing or paused)
        int MaxVoices = 0;       // AudioSettings::MaxVoices
        uint64_t Stolen = 0;     // sounds stopped to make room for another (since Init)
        uint64_t Rejected = 0;   // sounds not played because there was no room (since Init)
    };

    // A mixer group's level meter (post-fader peaks as linear gains, 1 = full scale).
    struct AudioLevel
    {
        float Left = 0.0f, Right = 0.0f; // falls back smoothly after a peak
        float Hold = 0.0f;               // the highest peak of the last moment (held ~1.5 s)
    };

    // One mixer group in the audio profiler (AudioProfile::Groups).
    struct AudioGroupProfile
    {
        std::string Name;
        std::string Parent;       // "" for Master
        int Depth = 0;            // Master 0, its children 1, ...
        size_t Voices = 0;        // sounds in the group itself (paused and fading ones included)
        size_t Paused = 0;
        size_t Stopping = 0;      // fading out
        size_t Streamed = 0;
        size_t Spatial = 0;
        size_t TotalVoices = 0;   // with every group below it
        uint64_t Stolen = 0;      // its sounds stopped to make room for another (since the reset)
        uint64_t Rejected = 0;    // sounds for it that were not played: no room (since the reset)
        float Gain = 1.0f;        // its fader with its parents' (mute / solo included)
        float Level = 0.0f;       // the meter's hold peak
    };

    // What the audio engine is doing right now (AudioEngine::GetProfile): the editor's Stats
    // window and viewport overlay, the player's ZAMIN_PLAYER_STATS log, tests.
    struct AudioProfile
    {
        bool Available = false;   // an engine runs (with or without a device)
        size_t Voices = 0;        // sounds that count towards the voice limit (playing or paused)
        int MaxVoices = 0;
        size_t Sounds = 0;        // every living sound: the counted ones, the music, the previews
        size_t Playing = 0;       // of those heard now (started, not paused)
        size_t Paused = 0;
        size_t Stopping = 0;      // fading out (released once silent)
        size_t Streamed = 0;
        size_t Spatial = 0;
        size_t Filtered = 0;      // with a low-pass / high-pass of their own
        size_t Music = 0;         // music tracks (2 during a crossfade)
        size_t Previews = 0;      // past the mixer (the editor's file previews)
        size_t PeakVoices = 0;    // the most counted voices at once since the reset
        uint64_t Started = 0;     // sounds started since the reset (the music too)
        uint64_t Stolen = 0;      // since the reset
        uint64_t Rejected = 0;    // since the reset
        size_t Preloaded = 0;     // files kept decoded in memory
        float SinceReset = 0.0f;  // seconds since the counters were reset
        std::vector<AudioGroupProfile> Groups; // the mixer's tree order: parents before children

        const AudioGroupProfile* FindGroup(std::string_view name) const;
        // One line for logs: "12/64 ovoz (cho'qqi 14) | musiqa 1 | to'xtatilgan 3, rad etilgan 0 | SFX 8, UI 2".
        std::string Format() const;
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
        // The output's sample rate in Hz (0 without an engine): sounds are decoded at it.
        static uint32_t GetSampleRate();
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
                                    float pitch = 1.0f, bool loop = false, float minDistance = -1.0f, float maxDistance = -1.0f);
        static void SetSoundPosition(uint32_t handle, const glm::vec3& position);
        // 3D sound: how fast it moves (units per second; Scene measures it from the transform) and
        // its own Doppler factor (x AudioSettings::DopplerFactor).
        static void SetSoundVelocity(uint32_t handle, const glm::vec3& velocity);
        static void SetSoundDopplerFactor(uint32_t handle, float factor);
        // 3D sound: the way it faces (Scene: its entity's forward, -Z) and its directional cone.
        static void SetSoundDirection(uint32_t handle, const glm::vec3& direction);
        static void SetSoundCone(uint32_t handle, const AudioConeSettings& cone);
        static AudioConeSettings GetSoundCone(uint32_t handle);
        // The cone's gain for a listener at `listenerPosition` and a sound at `sourcePosition` facing
        // `sourceDirection` (what miniaudio applies; 1 without a cone or at the same point).
        static float ComputeConeGain(const AudioConeSettings& cone, const glm::vec3& sourcePosition, const glm::vec3& sourceDirection,
                                     const glm::vec3& listenerPosition);
        static bool IsPlaying(uint32_t handle);
        // Playback position and length of a playing sound, in seconds (0 for an unknown handle).
        static float GetCursor(uint32_t handle);
        static float GetLength(uint32_t handle);
        // Jumps to `seconds` into the sound (clamped to its length).
        static void Seek(uint32_t handle, float seconds);
        // The part a looping sound repeats, in seconds: it plays on from where it is through `end`,
        // then again from `start` (an intro before the loop). end <= start or past the sound: its
        // end; 0, 0: the whole sound. A sound starts with its file's region (import settings
        // LoopStart / LoopEnd). False for an unknown handle.
        static bool SetLoopRegion(uint32_t handle, float start, float end);
        // The region in seconds (x start, y end; 0, length when the whole sound loops).
        static glm::vec2 GetLoopRegion(uint32_t handle);
        static void SetVolume(uint32_t handle, float volume);
        // The sound's own volume (before its random variation and its group; 0 for an unknown handle).
        static float GetVolume(uint32_t handle);
        // The pitch as asked (before its random variation and the Doppler effect; 0 for an unknown handle).
        static void SetPitch(uint32_t handle, float pitch);
        static float GetPitch(uint32_t handle);
        static void SetLooping(uint32_t handle, bool loop);
        // 3D sound: its attenuation distances.
        static void SetSoundDistances(uint32_t handle, float minDistance, float maxDistance);
        // Pause keeps the sound (and its place) where it is; Resume goes on from there. False for an
        // unknown handle (a finished or stopped sound) and the music (Audio::PauseMusic).
        static bool Pause(uint32_t handle);
        static bool Resume(uint32_t handle);
        static bool IsPaused(uint32_t handle);
        // Everything about a living sound. False for an unknown handle (`info` is left as it was).
        static bool GetSoundInfo(uint32_t handle, SoundInfo& info);
        // The sound was stopped to make room for another one (voice limit).
        static bool WasStolen(uint32_t handle);
        static AudioVoiceStats GetVoiceStats();

        // ---- sounds that ended by themselves, oldest first, since the last call (the scene hands them
        // to its scripts: OnSoundFinished): non-looping sounds that played to their end (noticed on the
        // main thread by Update and Play) and sounds the voice limit stopped. Not the ones stopped by
        // Stop / a fade / StopAll, nor the music. At most MaxFinishedSounds wait (the oldest go).
        static constexpr size_t MaxFinishedSounds = 1024;
        static std::vector<FinishedSound> TakeFinishedSounds();
        static size_t GetFinishedSoundCount();

        // ---- profiler
        // Every sound by kind, the voice limit, each mixer group's voices and what the limit cost
        // (stolen / rejected since the counters were reset; GetVoiceStats keeps the totals).
        static AudioProfile GetProfile();
        // Peak, started, stolen and rejected start again from 0 (the editor's button, entering play).
        static void ResetProfileCounters();
        // The counted voices of the last frames (one value per Update, oldest first) for a graph.
        static constexpr size_t VoiceHistoryCapacity = 240;
        static std::vector<float> GetVoiceHistory();
        // The random variation (SoundParams::VolumeRandom / PitchRandom) from here on is the same
        // sequence for the same seed (tests, replays). Init picks a random seed.
        static void SetRandomSeed(uint32_t seed);
        // Streamed from disk (not decoded into memory).
        static bool IsStreaming(uint32_t handle);
        // The mixer group the sound plays in ("" = past the mixer or unknown handle).
        static std::string GetSoundGroup(uint32_t handle);
        // Whether a file played with Stream = `requested` is streamed: asked for, its import settings
        // say Stream, or (LoadMode Auto) it is larger than the project's threshold.
        static bool ShouldStream(const std::filesystem::path& path, bool requested);
        static void Stop(uint32_t handle);
        // Fades the sound out over `fadeSeconds`, then releases it (0, or a paused sound: at once).
        // While it fades IsStopping is true; a sound fading out is the first to give its place to
        // a new one at the voice limit.
        static void Stop(uint32_t handle, float fadeSeconds);
        static bool IsStopping(uint32_t handle);
        // The sound's fade envelope goes to `volume` (0..1, on top of its own volume) over `seconds`
        // (e.g. ducking it under a voice line; 0 keeps it playing silently). False for an unknown
        // handle and the music (Audio::SetMusicVolume / the crossfades).
        static bool FadeTo(uint32_t handle, float volume, float seconds);
        // The envelope now (1 for an unknown handle).
        static float GetFade(uint32_t handle);
        // Everything, music included (the editor leaving play mode, shutdown); the preloaded files
        // are let go too.
        static void StopAll();
        // Every sound but the music (a scene stops: the music goes on into the next one).
        static void StopAllExceptMusic();
        // Sounds that are playing right now.
        static size_t GetPlayingCount();
        // The engine's output volume (after Master).
        static void SetMasterVolume(float volume);

        // ---- filters (low-pass / high-pass; a changed cutoff glides there)
        // The sound's own filter (the first one makes a filter node for it; false: unknown handle).
        static bool SetSoundFilter(uint32_t handle, const AudioFilterSettings& filter);
        static AudioFilterSettings GetSoundFilter(uint32_t handle);
        // A mixer group's filter: on everything it sends on (sounds and child groups). Kept in the
        // mixer settings (AudioSettings::Groups[].Filter); false for unknown groups.
        static bool SetGroupFilter(const std::string& group, const AudioFilterSettings& filter);
        static AudioFilterSettings GetGroupFilter(const std::string& group);

        // ---- sound files (their import settings: AudioClipSettings in the .zmeta)
        // Decodes the file into memory now and keeps it there until Unload: the first Play does not
        // wait for decoding and a short sound played again and again is not decoded every time.
        // False when it cannot be read or is streamed (its LoadMode / size). Without an engine: false.
        static bool Preload(const std::filesystem::path& path);
        static void Unload(const std::filesystem::path& path);
        static void UnloadAll();
        static bool IsPreloaded(const std::filesystem::path& path);
        static size_t GetPreloadedCount();
        // A scene starts: exactly these files stay preloaded for it (its Audio Sources' clips with
        // Preload). What an earlier scene (or a clip's Preload on its first play) left decoded and
        // this scene does not use is let go; files preloaded by a Preload call stay.
        static void SetScenePreloads(const std::vector<std::filesystem::path>& paths);
        // The file or its import settings changed (hot reload): a preloaded file is decoded again
        // (the sounds playing it now keep the old data until they end) - or let go when it streams
        // now or its Preload was switched off. Returns 1 when it was preloaded.
        static size_t ReloadFile(const std::filesystem::path& path, bool* failed = nullptr);

        // ---- listener (Scene::UpdateAudio: the active Audio Listener, else the primary camera)
        static void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
        static void SetListenerVelocity(const glm::vec3& velocity);
        static glm::vec3 GetListenerPosition();
        static glm::vec3 GetListenerVelocity();

        // ---- Doppler (the speed of sound: 343 units per second - metres)
        static constexpr float SpeedOfSound = 343.3f;
        // The pitch factor a sound at `sourcePosition` moving with `sourceVelocity` is heard with by
        // a listener at `listenerPosition` moving with `listenerVelocity` (OpenAL's model): above 1
        // while they come closer, below 1 while they move apart, 1 for factor 0. Speeds are capped
        // (at half the speed of sound / factor) and the result kept in 0.5..2.
        static float ComputeDopplerPitch(const glm::vec3& sourcePosition, const glm::vec3& sourceVelocity, const glm::vec3& listenerPosition,
                                         const glm::vec3& listenerVelocity, float factor);

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
