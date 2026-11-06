#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Core/Log.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Zamin {

    namespace {
        // A pass-through node after each mixer group: it measures the peak of what the group
        // sends on (post-fader) on the audio thread; the main thread collects it in Update.
        struct MeterNode
        {
            ma_node_base Base; // must stay first: miniaudio sees the node as this
            std::atomic<float> Peak[2];
            ma_uint32 Channels = 2;
        };

        void RaisePeak(std::atomic<float>& peak, float value)
        {
            float old = peak.load(std::memory_order_relaxed);
            while (value > old && !peak.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
        }

        void MeterProcess(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            (void)framesOut;
            (void)frameCountIn;
            auto* meter = (MeterNode*)node;
            // A pass-through node: the frames are already in place, they are only looked at.
            const float* frames = framesIn[0];
            const ma_uint32 channels = std::max<ma_uint32>(1, meter->Channels);
            float peak[2] = { 0.0f, 0.0f };
            for (ma_uint32 f = 0; f < *frameCountOut; f++)
                for (ma_uint32 c = 0; c < channels; c++)
                {
                    float v = std::abs(frames[f * channels + c]);
                    float& p = peak[std::min<ma_uint32>(c, 1)];
                    if (v > p) p = v;
                }
            if (channels == 1)
                peak[1] = peak[0];
            RaisePeak(meter->Peak[0], peak[0]);
            RaisePeak(meter->Peak[1], peak[1]);
        }

        ma_node_vtable s_MeterVTable = { MeterProcess, nullptr, 1, 1, MA_NODE_FLAG_PASSTHROUGH };

        struct MixerGroup
        {
            std::string Name;
            std::string Parent; // what its meter is attached to now
            bool Wired = false;
            std::unique_ptr<ma_sound_group> Group;
            std::unique_ptr<MeterNode> Meter;
            AudioLevel Level;
            float HoldTime = 0.0f;
        };

        struct Voice
        {
            std::unique_ptr<ma_sound> Sound;
            std::string File;  // the file it was loaded from
            std::string Group; // "" = straight to the output
            bool Streamed = false;
            bool Music = false;
            bool Paused = false; // stopped where it is: not released as finished
        };

        // A music track and its fade. Position runs 0..1 along the fade, the track is heard at
        // sin(position * pi/2) (equal power: a crossfade does not dip in the middle).
        struct MusicVoice
        {
            uint32_t Handle = 0;
            std::filesystem::path Path;
            float Position = 0.0f, From = 0.0f, To = 1.0f, Duration = 0.0f, Elapsed = 0.0f;
            bool Stopping = false; // fading out: released when it reaches 0
        };

        struct MusicState
        {
            std::vector<MusicVoice> Voices;
            uint32_t Current = 0;
            std::filesystem::path CurrentPath; // the current or the waiting track
            bool Queued = false;               // CurrentPath starts once the old tracks faded out
            float QueuedFade = 0.0f;
            bool QueuedLoop = true;
            float Volume = 1.0f;
            bool Paused = false;
        };

        ma_engine* s_Engine = nullptr;
        bool s_HasDevice = false;
        std::unordered_map<uint32_t, Voice> s_Voices;
        uint32_t s_NextHandle = 1;
        AudioSettings s_Mixer;
        std::vector<std::unique_ptr<MixerGroup>> s_Groups;
        std::unordered_set<std::string> s_WarnedGroups;
        MusicState s_Music;

        MixerGroup* FindGroup(std::string_view name)
        {
            for (auto& g : s_Groups)
                if (g->Name == name)
                    return g.get();
            return nullptr;
        }

        ma_node* GroupInput(std::string_view name)
        {
            MixerGroup* g = name.empty() ? nullptr : FindGroup(name);
            return g ? (ma_node*)g->Group.get() : ma_engine_get_endpoint(s_Engine);
        }

        void DestroyGroup(MixerGroup& g)
        {
            if (g.Meter)
                ma_node_uninit(&g.Meter->Base, nullptr);
            if (g.Group)
                ma_sound_group_uninit(g.Group.get());
            g.Meter.reset();
            g.Group.reset();
        }

        std::unique_ptr<MixerGroup> CreateGroup(const std::string& name)
        {
            auto g = std::make_unique<MixerGroup>();
            g->Name = name;
            g->Group = std::make_unique<ma_sound_group>();
            if (ma_sound_group_init(s_Engine, MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, g->Group.get()) != MA_SUCCESS)
            {
                ZM_CORE_ERROR("Audio guruhi yaratilmadi: {}", name);
                g->Group.reset();
                return nullptr;
            }
            g->Meter = std::make_unique<MeterNode>();
            g->Meter->Peak[0] = 0.0f;
            g->Meter->Peak[1] = 0.0f;
            g->Meter->Channels = ma_engine_get_channels(s_Engine);
            ma_uint32 channels = g->Meter->Channels;
            ma_node_config config = ma_node_config_init();
            config.vtable = &s_MeterVTable;
            config.pInputChannels = &channels;
            config.pOutputChannels = &channels;
            if (ma_node_init(ma_engine_get_node_graph(s_Engine), &config, nullptr, &g->Meter->Base) != MA_SUCCESS)
            {
                // Without a meter the group goes straight on (no level shown).
                g->Meter.reset();
            }
            else
                ma_node_attach_output_bus(g->Group.get(), 0, &g->Meter->Base, 0);
            return g;
        }

        void ApplyGains()
        {
            for (auto& g : s_Groups)
                ma_sound_group_set_volume(g->Group.get(), s_Mixer.GetLocalGain(g->Name));
        }

        void RouteVoice(Voice& voice)
        {
            ma_node_attach_output_bus(voice.Sound.get(), 0, GroupInput(voice.Group), 0);
        }

        // Makes the engine's groups match s_Mixer.
        void BuildGroups()
        {
            if (!s_Engine)
                return;
            for (const AudioGroupSettings& settings : s_Mixer.Groups)
                if (!FindGroup(settings.Name))
                    if (auto g = CreateGroup(settings.Name))
                        s_Groups.push_back(std::move(g));
            // Groups that are gone: their sounds move to SFX, then they are destroyed.
            for (auto it = s_Groups.begin(); it != s_Groups.end();)
            {
                if (s_Mixer.Find((*it)->Name))
                {
                    ++it;
                    continue;
                }
                for (auto& [handle, voice] : s_Voices)
                    if (voice.Group == (*it)->Name)
                    {
                        voice.Group = AudioSettings::SFX;
                        RouteVoice(voice);
                    }
                // Its children were attached to it: they get their new parents below.
                for (auto& other : s_Groups)
                    if (other->Parent == (*it)->Name)
                        other->Wired = false;
                DestroyGroup(**it);
                it = s_Groups.erase(it);
            }
            for (const AudioGroupSettings& settings : s_Mixer.Groups)
            {
                MixerGroup* g = FindGroup(settings.Name);
                if (!g || (g->Wired && g->Parent == settings.Parent))
                    continue;
                ma_node* output = g->Meter ? (ma_node*)&g->Meter->Base : (ma_node*)g->Group.get();
                ma_node_attach_output_bus(output, 0, GroupInput(settings.Parent), 0);
                g->Parent = settings.Parent;
                g->Wired = true;
            }
            ApplyGains();
        }

        void Release(uint32_t handle)
        {
            if (auto it = s_Voices.find(handle); it != s_Voices.end())
            {
                ma_sound_uninit(it->second.Sound.get());
                s_Voices.erase(it);
            }
        }

        void CleanupFinished()
        {
            std::vector<uint32_t> finished;
            for (auto& [handle, voice] : s_Voices)
                if (!voice.Paused && !ma_sound_is_playing(voice.Sound.get()) && !ma_sound_is_looping(voice.Sound.get()))
                    finished.push_back(handle);
            for (uint32_t handle : finished)
                Release(handle);
        }

        // miniaudio's resource manager (0.11.21; 0.11.22 too) reads the data buffer node it has just
        // freed when a file fails to load (ma_resource_manager_data_buffer_node_acquire, after its
        // "done:" label): a use-after-free on every missing or undecodable sound. So a file is opened
        // with a decoder of our own first and only handed over when that works. A file a living sound
        // was loaded from is not checked again: the manager shares that sound's decoded data and
        // does not open the file. (Only a file whose header reads but whose decoding then fails with
        // an error - not just ends early - still takes that path.)
        ma_result CheckDecodable(const std::string& file)
        {
            for (const auto& [handle, voice] : s_Voices)
                if (voice.File == file)
                    return MA_SUCCESS;
            ma_decoder decoder;
            ma_result result = ma_decoder_init_file(file.c_str(), nullptr, &decoder);
            if (result == MA_SUCCESS)
                ma_decoder_uninit(&decoder);
            return result;
        }

        void ReportFailure(const std::filesystem::path& path, ma_result result)
        {
            std::string reason = AssetImportLog::CheckReadable(path);
            if (reason.empty())
                reason = std::format("audio fayl o'qilmadi - WAV, MP3 yoki FLAC bo'lishi kerak (miniaudio: {})", ma_result_description(result));
            AssetImportLog::Report(path, reason);
        }

        // Whether the file can be played; a failure is reported once.
        bool CanPlay(const std::filesystem::path& path)
        {
            if (!s_Engine || !AssetImportLog::ShouldRetry(path))
                return false;
            ma_result result = CheckDecodable(path.string());
            if (result != MA_SUCCESS)
            {
                ReportFailure(path, result);
                return false;
            }
            return true;
        }

        uint32_t StartSound(const std::filesystem::path& path, const SoundParams& params, bool music)
        {
            if (!s_Engine) return 0;
            CleanupFinished();

            // A sound that failed and did not change since is not decoded again (a script may play it
            // every jump); its error was reported once.
            if (!AssetImportLog::ShouldRetry(path))
                return 0;
            std::string group = params.Group;
            if (!group.empty() && !FindGroup(group))
            {
                if (s_WarnedGroups.insert(group).second)
                    ZM_CORE_WARN("Noma'lum audio guruhi '{}' - ovoz SFX guruhida ijro etiladi", group);
                group = AudioSettings::SFX;
            }
            const std::string file = path.string();
            const bool stream = music || AudioEngine::ShouldStream(path, params.Stream);
            Voice voice;
            voice.Sound = std::make_unique<ma_sound>();
            ma_result result = CheckDecodable(file);
            if (result == MA_SUCCESS)
            {
                ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
                MixerGroup* target = group.empty() ? nullptr : FindGroup(group);
                result = ma_sound_init_from_file(s_Engine, file.c_str(), flags, target ? target->Group.get() : nullptr, nullptr, voice.Sound.get());
            }
            if (result != MA_SUCCESS)
            {
                ReportFailure(path, result);
                return 0;
            }
            AssetImportLog::Clear(path);
            ma_sound* sound = voice.Sound.get();
            ma_sound_set_volume(sound, std::max(0.0f, params.Volume));
            ma_sound_set_pitch(sound, std::max(0.01f, params.Pitch));
            ma_sound_set_looping(sound, params.Loop ? MA_TRUE : MA_FALSE);
            if (params.Spatial)
            {
                ma_sound_set_spatialization_enabled(sound, MA_TRUE);
                ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
                ma_sound_set_min_distance(sound, std::max(0.01f, params.MinDistance));
                ma_sound_set_max_distance(sound, std::max(params.MinDistance, params.MaxDistance));
                ma_sound_set_position(sound, params.Position.x, params.Position.y, params.Position.z);
            }
            else
                ma_sound_set_spatialization_enabled(sound, MA_FALSE);
            ma_sound_start(sound);

            voice.File = file;
            voice.Group = group;
            voice.Streamed = stream;
            voice.Music = music;
            uint32_t handle = s_NextHandle++;
            s_Voices[handle] = std::move(voice);
            return handle;
        }

        Voice* FindVoice(uint32_t handle)
        {
            auto it = s_Voices.find(handle);
            return it != s_Voices.end() ? &it->second : nullptr;
        }

        // ---- music
        float MusicGain(const MusicVoice& v)
        {
            return std::sin(std::clamp(v.Position, 0.0f, 1.0f) * 1.5707963f) * s_Music.Volume;
        }

        void StartFade(MusicVoice& v, float to, float seconds)
        {
            v.From = v.Position;
            v.To = to;
            v.Elapsed = 0.0f;
            v.Duration = std::max(0.0f, seconds);
            if (v.Duration <= 0.0f)
                v.Position = to;
        }

        void ApplyMusicVolume(const MusicVoice& v)
        {
            if (Voice* voice = FindVoice(v.Handle))
                ma_sound_set_volume(voice->Sound.get(), MusicGain(v));
        }

        bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            return a.lexically_normal() == b.lexically_normal();
        }

        // Every track still heard starts fading out; with no fade they stop at once.
        void FadeOutMusic(float seconds)
        {
            for (MusicVoice& v : s_Music.Voices)
                if (!v.Stopping || seconds <= 0.0f)
                {
                    v.Stopping = true;
                    // A track already fading out keeps its (shorter) way down unless this is quicker.
                    StartFade(v, 0.0f, seconds * v.Position);
                }
            std::erase_if(s_Music.Voices, [](const MusicVoice& v) {
                if (v.Stopping && v.Position <= 0.0f)
                {
                    Release(v.Handle);
                    return true;
                }
                return false;
            });
            s_Music.Current = 0;
        }

        bool StartMusicTrack(const std::filesystem::path& path, float fade, bool loop)
        {
            SoundParams params;
            params.Group = AudioSettings::Music;
            params.Loop = loop;
            params.Volume = 0.0f;
            uint32_t handle = StartSound(path, params, true);
            if (!handle)
                return false;
            MusicVoice v;
            v.Handle = handle;
            v.Path = path;
            StartFade(v, 1.0f, fade);
            ApplyMusicVolume(v);
            if (s_Music.Paused)
            {
                Voice* voice = FindVoice(handle);
                ma_sound_stop(voice->Sound.get());
                voice->Paused = true;
            }
            s_Music.Voices.push_back(v);
            s_Music.Current = handle;
            s_Music.CurrentPath = path;
            s_Music.Queued = false;
            return true;
        }

        MusicVoice* CurrentMusic()
        {
            for (MusicVoice& v : s_Music.Voices)
                if (v.Handle == s_Music.Current && !v.Stopping)
                    return &v;
            return nullptr;
        }

        void UpdateMusic(float dt)
        {
            // Tracks that ended by themselves (not looping) or were stopped from outside.
            std::erase_if(s_Music.Voices, [](const MusicVoice& v) { return !FindVoice(v.Handle); });
            if (s_Music.Current && !CurrentMusic())
            {
                s_Music.Current = 0;
                if (!s_Music.Queued)
                    s_Music.CurrentPath.clear();
            }
            if (!s_Music.Paused)
                for (MusicVoice& v : s_Music.Voices)
                {
                    if (v.Duration > 0.0f && v.Position != v.To)
                    {
                        v.Elapsed += dt;
                        float t = std::min(1.0f, v.Elapsed / v.Duration);
                        v.Position = v.From + (v.To - v.From) * t;
                        if (t >= 1.0f)
                            v.Position = v.To;
                    }
                    ApplyMusicVolume(v);
                }
            std::erase_if(s_Music.Voices, [](const MusicVoice& v) {
                if (v.Stopping && v.Position <= 0.0f)
                {
                    Release(v.Handle);
                    return true;
                }
                return false;
            });
            if (s_Music.Queued && s_Music.Voices.empty())
            {
                std::filesystem::path path = s_Music.CurrentPath;
                if (!StartMusicTrack(path, s_Music.QueuedFade, s_Music.QueuedLoop))
                {
                    s_Music.Queued = false;
                    s_Music.CurrentPath.clear();
                }
            }
        }

        void UpdateMeters(float dt)
        {
            // Falls 24 dB a second; the hold mark waits 1.5 s, then falls the same way.
            const float fall = std::pow(10.0f, -1.2f * std::max(0.0f, dt));
            for (auto& g : s_Groups)
            {
                float peak[2] = { 0.0f, 0.0f };
                if (g->Meter)
                    for (int c = 0; c < 2; c++)
                        peak[c] = g->Meter->Peak[c].exchange(0.0f, std::memory_order_relaxed);
                AudioLevel& level = g->Level;
                level.Left = std::max(peak[0], level.Left * fall);
                level.Right = std::max(peak[1], level.Right * fall);
                float top = std::max(peak[0], peak[1]);
                if (top >= level.Hold)
                {
                    level.Hold = top;
                    g->HoldTime = 0.0f;
                }
                else if ((g->HoldTime += dt) > 1.5f)
                    level.Hold *= fall;
                if (level.Left < 1e-5f) level.Left = 0.0f;
                if (level.Right < 1e-5f) level.Right = 0.0f;
                if (level.Hold < 1e-5f) level.Hold = 0.0f;
            }
        }
    }

    void AudioEngine::Init(bool withoutDevice)
    {
        if (s_Engine) return;
        ma_engine_config config = ma_engine_config_init();
        if (withoutDevice)
        {
            config.noDevice = MA_TRUE;
            config.channels = 2;
            config.sampleRate = 48000;
        }
        s_Engine = new ma_engine();
        if (ma_engine_init(&config, s_Engine) != MA_SUCCESS)
        {
            ZM_CORE_WARN("Audio qurilmasi topilmadi - ovoz o'chirilgan");
            delete s_Engine;
            s_Engine = nullptr;
            return;
        }
        s_HasDevice = !withoutDevice;
        BuildGroups();
        ZM_CORE_INFO("{}", withoutDevice ? "Audio tizimi qurilmasiz ishga tushdi" : "Audio tizimi ishga tushdi");
    }

    void AudioEngine::Shutdown()
    {
        if (!s_Engine) return;
        StopAll();
        for (auto& g : s_Groups)
            DestroyGroup(*g);
        s_Groups.clear();
        ma_engine_uninit(s_Engine);
        delete s_Engine;
        s_Engine = nullptr;
        s_HasDevice = false;
    }

    bool AudioEngine::IsAvailable() { return s_Engine != nullptr; }
    bool AudioEngine::HasDevice() { return s_Engine && s_HasDevice; }

    void AudioEngine::Update(float deltaSeconds)
    {
        if (!s_Engine) return;
        float dt = std::isfinite(deltaSeconds) ? std::max(0.0f, deltaSeconds) : 0.0f;
        CleanupFinished();
        UpdateMusic(dt);
        UpdateMeters(dt);
    }

    void AudioEngine::RenderOffline(float seconds)
    {
        if (!s_Engine || s_HasDevice || !(seconds > 0.0f)) return;
        const ma_uint32 channels = ma_engine_get_channels(s_Engine);
        ma_uint64 frames = (ma_uint64)((double)seconds * ma_engine_get_sample_rate(s_Engine));
        std::vector<float> buffer(1024 * channels);
        while (frames > 0)
        {
            ma_uint64 chunk = std::min<ma_uint64>(frames, 1024), read = 0;
            ma_engine_read_pcm_frames(s_Engine, buffer.data(), chunk, &read);
            frames -= chunk;
        }
    }

    uint32_t AudioEngine::Play(const std::filesystem::path& path, const SoundParams& params)
    {
        return StartSound(path, params, false);
    }

    uint32_t AudioEngine::Play(const std::filesystem::path& path, float volume, float pitch, bool loop)
    {
        SoundParams params;
        params.Volume = volume;
        params.Pitch = pitch;
        params.Loop = loop;
        return StartSound(path, params, false);
    }

    uint32_t AudioEngine::PlaySpatial(const std::filesystem::path& path, const glm::vec3& position, float volume, float pitch,
                                      bool loop, float minDistance, float maxDistance)
    {
        SoundParams params;
        params.Volume = volume;
        params.Pitch = pitch;
        params.Loop = loop;
        params.Spatial = true;
        params.Position = position;
        params.MinDistance = minDistance;
        params.MaxDistance = maxDistance;
        return StartSound(path, params, false);
    }

    void AudioEngine::SetSoundPosition(uint32_t handle, const glm::vec3& p)
    {
        if (Voice* v = FindVoice(handle))
            ma_sound_set_position(v->Sound.get(), p.x, p.y, p.z);
    }

    bool AudioEngine::IsPlaying(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v && ma_sound_is_playing(v->Sound.get());
    }

    float AudioEngine::GetCursor(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        float seconds = 0.0f;
        if (v && ma_sound_get_cursor_in_seconds(v->Sound.get(), &seconds) == MA_SUCCESS)
            return seconds;
        return 0.0f;
    }

    float AudioEngine::GetLength(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        float seconds = 0.0f;
        if (v && ma_sound_get_length_in_seconds(v->Sound.get(), &seconds) == MA_SUCCESS)
            return seconds;
        return 0.0f;
    }

    void AudioEngine::Seek(uint32_t handle, float seconds)
    {
        Voice* v = FindVoice(handle);
        if (!v)
            return;
        ma_sound* sound = v->Sound.get();
        // Frames of the sound's own data (its sample rate, not the device's).
        ma_uint32 sampleRate = 0;
        if (ma_sound_get_data_format(sound, nullptr, nullptr, &sampleRate, nullptr, 0) != MA_SUCCESS || sampleRate == 0)
            sampleRate = ma_engine_get_sample_rate(s_Engine);
        ma_uint64 length = 0;
        ma_sound_get_length_in_pcm_frames(sound, &length);
        ma_uint64 frame = (ma_uint64)std::max(0.0, (double)seconds * (double)sampleRate);
        if (length > 0)
            frame = std::min(frame, length - 1);
        ma_sound_seek_to_pcm_frame(sound, frame);
    }

    void AudioEngine::SetVolume(uint32_t handle, float volume)
    {
        if (Voice* v = FindVoice(handle))
            ma_sound_set_volume(v->Sound.get(), std::max(0.0f, volume));
    }

    float AudioEngine::GetVolume(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v ? ma_sound_get_volume(v->Sound.get()) : 0.0f;
    }

    bool AudioEngine::IsStreaming(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v && v->Streamed;
    }

    std::string AudioEngine::GetSoundGroup(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v ? v->Group : std::string();
    }

    bool AudioEngine::ShouldStream(const std::filesystem::path& path, bool requested)
    {
        if (requested)
            return true;
        std::error_code ec;
        uintmax_t size = std::filesystem::file_size(path, ec);
        return !ec && (double)size > (double)s_Mixer.StreamThresholdMB * 1024.0 * 1024.0;
    }

    void AudioEngine::Stop(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        if (!v) return;
        ma_sound_stop(v->Sound.get());
        Release(handle);
    }

    void AudioEngine::StopAll()
    {
        for (auto& [handle, voice] : s_Voices)
        {
            ma_sound_stop(voice.Sound.get());
            ma_sound_uninit(voice.Sound.get());
        }
        s_Voices.clear();
        float volume = s_Music.Volume;
        s_Music = {};
        s_Music.Volume = volume; // the music's volume is a setting, not a track
    }

    void AudioEngine::StopAllExceptMusic()
    {
        std::vector<uint32_t> stop;
        for (auto& [handle, voice] : s_Voices)
            if (!voice.Music)
                stop.push_back(handle);
        for (uint32_t handle : stop)
            Stop(handle);
    }

    size_t AudioEngine::GetPlayingCount()
    {
        size_t count = 0;
        for (auto& [handle, voice] : s_Voices)
            if (ma_sound_is_playing(voice.Sound.get()))
                count++;
        return count;
    }

    void AudioEngine::SetMasterVolume(float volume)
    {
        if (s_Engine) ma_engine_set_volume(s_Engine, volume);
    }

    // ------------------------------------------------------------------ listener
    void AudioEngine::SetListener(const glm::vec3& p, const glm::vec3& f, const glm::vec3& u)
    {
        if (!s_Engine) return;
        ma_engine_listener_set_position(s_Engine, 0, p.x, p.y, p.z);
        ma_engine_listener_set_direction(s_Engine, 0, f.x, f.y, f.z);
        ma_engine_listener_set_world_up(s_Engine, 0, u.x, u.y, u.z);
    }

    void AudioEngine::SetListenerVelocity(const glm::vec3& v)
    {
        if (s_Engine) ma_engine_listener_set_velocity(s_Engine, 0, v.x, v.y, v.z);
    }

    glm::vec3 AudioEngine::GetListenerPosition()
    {
        if (!s_Engine) return glm::vec3(0.0f);
        ma_vec3f p = ma_engine_listener_get_position(s_Engine, 0);
        return { p.x, p.y, p.z };
    }

    glm::vec3 AudioEngine::GetListenerVelocity()
    {
        if (!s_Engine) return glm::vec3(0.0f);
        ma_vec3f v = ma_engine_listener_get_velocity(s_Engine, 0);
        return { v.x, v.y, v.z };
    }

    // ------------------------------------------------------------------ mixer
    void AudioEngine::SetMixerSettings(const AudioSettings& settings)
    {
        s_Mixer = settings;
        s_Mixer.Sanitize();
        BuildGroups();
    }

    const AudioSettings& AudioEngine::GetMixerSettings() { return s_Mixer; }

    bool AudioEngine::HasGroup(const std::string& group) { return s_Mixer.Find(group) != nullptr; }

    bool AudioEngine::SetGroupVolume(const std::string& group, float volume)
    {
        AudioGroupSettings* g = s_Mixer.Find(group);
        if (!g) return false;
        g->Volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, AudioSettings::MaxVolume) : 1.0f;
        ApplyGains();
        return true;
    }

    float AudioEngine::GetGroupVolume(const std::string& group)
    {
        const AudioGroupSettings* g = s_Mixer.Find(group);
        return g ? g->Volume : 0.0f;
    }

    bool AudioEngine::SetGroupMuted(const std::string& group, bool muted)
    {
        AudioGroupSettings* g = s_Mixer.Find(group);
        if (!g) return false;
        g->Muted = muted;
        ApplyGains();
        return true;
    }

    bool AudioEngine::IsGroupMuted(const std::string& group)
    {
        const AudioGroupSettings* g = s_Mixer.Find(group);
        return g && g->Muted;
    }

    bool AudioEngine::SetGroupSolo(const std::string& group, bool solo)
    {
        AudioGroupSettings* g = s_Mixer.Find(group);
        if (!g) return false;
        g->Solo = solo;
        ApplyGains();
        return true;
    }

    bool AudioEngine::IsGroupSolo(const std::string& group)
    {
        const AudioGroupSettings* g = s_Mixer.Find(group);
        return g && g->Solo;
    }

    float AudioEngine::GetGroupEffectiveVolume(const std::string& group) { return s_Mixer.GetEffectiveGain(group); }

    AudioLevel AudioEngine::GetGroupLevel(const std::string& group)
    {
        MixerGroup* g = FindGroup(group);
        return g ? g->Level : AudioLevel();
    }

    size_t AudioEngine::GetGroupVoiceCount(const std::string& group)
    {
        size_t count = 0;
        for (auto& [handle, voice] : s_Voices)
            if (voice.Group == group && ma_sound_is_playing(voice.Sound.get()))
                count++;
        return count;
    }

    // ------------------------------------------------------------------ music
    bool AudioEngine::PlayMusic(const std::filesystem::path& path, float fadeSeconds, bool loop)
    {
        if (!s_Engine || path.empty())
            return false;
        fadeSeconds = std::isfinite(fadeSeconds) ? std::max(0.0f, fadeSeconds) : 0.0f;
        if (MusicVoice* current = CurrentMusic(); current && SamePath(current->Path, path))
        {
            if (current->To < 1.0f)
                StartFade(*current, 1.0f, fadeSeconds);
            return true;
        }
        if (s_Music.Queued && SamePath(s_Music.CurrentPath, path))
            return true;
        if (!CanPlay(path))
            return false;
        FadeOutMusic(fadeSeconds);
        if (s_Music.Voices.empty())
            return StartMusicTrack(path, fadeSeconds, loop);
        // After the old track has faded out (UpdateMusic).
        s_Music.Queued = true;
        s_Music.CurrentPath = path;
        s_Music.QueuedFade = fadeSeconds;
        s_Music.QueuedLoop = loop;
        return true;
    }

    bool AudioEngine::CrossfadeMusic(const std::filesystem::path& path, float seconds, bool loop)
    {
        if (!s_Engine || path.empty())
            return false;
        seconds = std::isfinite(seconds) ? std::max(0.0f, seconds) : 0.0f;
        if (MusicVoice* current = CurrentMusic(); current && SamePath(current->Path, path))
        {
            if (current->To < 1.0f)
                StartFade(*current, 1.0f, seconds);
            return true;
        }
        // A track on its way out comes back from where it is.
        for (MusicVoice& v : s_Music.Voices)
            if (v.Stopping && SamePath(v.Path, path) && FindVoice(v.Handle))
            {
                uint32_t handle = v.Handle;
                for (MusicVoice& other : s_Music.Voices)
                    if (other.Handle != handle && !other.Stopping)
                    {
                        other.Stopping = true;
                        StartFade(other, 0.0f, seconds);
                    }
                v.Stopping = false;
                StartFade(v, 1.0f, seconds * (1.0f - v.Position));
                s_Music.Current = handle;
                s_Music.CurrentPath = path;
                s_Music.Queued = false;
                return true;
            }
        if (!CanPlay(path))
            return false;
        s_Music.Queued = false;
        for (MusicVoice& v : s_Music.Voices)
            if (!v.Stopping)
            {
                v.Stopping = true;
                StartFade(v, 0.0f, seconds);
            }
        std::erase_if(s_Music.Voices, [](const MusicVoice& v) {
            if (v.Stopping && v.Position <= 0.0f)
            {
                Release(v.Handle);
                return true;
            }
            return false;
        });
        return StartMusicTrack(path, seconds, loop);
    }

    void AudioEngine::StopMusic(float fadeSeconds)
    {
        fadeSeconds = std::isfinite(fadeSeconds) ? std::max(0.0f, fadeSeconds) : 0.0f;
        s_Music.Queued = false;
        s_Music.CurrentPath.clear();
        FadeOutMusic(fadeSeconds);
    }

    void AudioEngine::PauseMusic()
    {
        if (s_Music.Paused) return;
        s_Music.Paused = true;
        for (const MusicVoice& m : s_Music.Voices)
            if (Voice* v = FindVoice(m.Handle))
            {
                ma_sound_stop(v->Sound.get());
                v->Paused = true;
            }
    }

    void AudioEngine::ResumeMusic()
    {
        if (!s_Music.Paused) return;
        s_Music.Paused = false;
        for (const MusicVoice& m : s_Music.Voices)
            if (Voice* v = FindVoice(m.Handle))
            {
                v->Paused = false;
                ma_sound_start(v->Sound.get());
            }
    }

    bool AudioEngine::IsMusicPaused() { return s_Music.Paused; }

    bool AudioEngine::IsMusicPlaying() { return s_Music.Queued || CurrentMusic() != nullptr; }

    std::filesystem::path AudioEngine::GetMusic() { return IsMusicPlaying() ? s_Music.CurrentPath : std::filesystem::path(); }

    void AudioEngine::SetMusicVolume(float volume)
    {
        s_Music.Volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, AudioSettings::MaxVolume) : 1.0f;
        for (const MusicVoice& v : s_Music.Voices)
            ApplyMusicVolume(v);
    }

    float AudioEngine::GetMusicVolume() { return s_Music.Volume; }

    size_t AudioEngine::GetMusicVoiceCount()
    {
        size_t count = 0;
        for (const MusicVoice& v : s_Music.Voices)
            if (FindVoice(v.Handle))
                count++;
        return count;
    }

    uint32_t AudioEngine::GetMusicHandle() { return CurrentMusic() ? s_Music.Current : 0; }

}
