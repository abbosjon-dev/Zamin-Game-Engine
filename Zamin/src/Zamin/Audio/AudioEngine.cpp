#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Audio/AudioFilter.h"
#include "Zamin/Core/Log.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
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

        // A low-pass / high-pass node (AudioFilterChain) after a mixer group or a sound. The main
        // thread sets the cutoffs (atomics); the audio thread glides the filter to them.
        struct FilterNode
        {
            ma_node_base Base; // must stay first
            std::atomic<float> LowPass{ 0.0f }, HighPass{ 0.0f }; // Hz; 0 = off
            AudioFilterChain Chain;                              // the audio thread's
            ma_uint32 Channels = 2;
            float SampleRate = 48000.0f;
        };

        void FilterProcess(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            (void)frameCountIn;
            auto* filter = (FilterNode*)node;
            const ma_uint32 channels = std::max<ma_uint32>(1, filter->Channels);
            const ma_uint32 frames = *frameCountOut;
            float* out = framesOut[0];
            if (!framesIn || !framesIn[0])
            {
                std::memset(out, 0, (size_t)frames * channels * sizeof(float));
                return;
            }
            if (out != framesIn[0])
                std::memcpy(out, framesIn[0], (size_t)frames * channels * sizeof(float));
            filter->Chain.SetTarget(filter->LowPass.load(std::memory_order_relaxed), filter->HighPass.load(std::memory_order_relaxed));
            filter->Chain.Process(out, frames, channels, filter->SampleRate);
        }

        ma_node_vtable s_FilterVTable = { FilterProcess, nullptr, 1, 1, 0 };

        struct MixerGroup
        {
            std::string Name;
            std::string Parent; // what its meter is attached to now
            bool Wired = false;
            std::unique_ptr<ma_sound_group> Group;
            std::unique_ptr<FilterNode> Filter; // group -> filter -> meter -> parent
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
            bool Spatial = false;
            bool Counted = false; // takes a place of the voice limit (not the music, not a preview past the mixer)
            int Priority = 128;
            // What miniaudio gets: Volume x VolumeVariation, Pitch x PitchVariation x DopplerPitch.
            float Volume = 1.0f, VolumeVariation = 1.0f;
            float Pitch = 1.0f, PitchVariation = 1.0f;
            float DopplerFactor = 1.0f, DopplerPitch = 1.0f;
            glm::vec3 Velocity{ 0.0f };
            float ClipGain = 1.0f; // the file's own Volume (import settings)
            // The fade envelope (main thread: logic and info); miniaudio's fader ramps the samples
            // the same way on the audio thread.
            float Fade = 1.0f, FadeFrom = 1.0f, FadeTarget = 1.0f, FadeDuration = 0.0f, FadeElapsed = 0.0f;
            bool Stopping = false; // fading out: released once silent
            AudioFilterSettings FilterSettings;
            std::unique_ptr<FilterNode> Filter; // made when the sound first gets a filter
            AudioConeSettings Cone;              // 3D: its directional cone
            glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
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
        uint32_t s_RandomState = 0x9E3779B9u;
        uint64_t s_StolenCount = 0, s_RejectedCount = 0;
        std::unordered_set<uint32_t> s_StolenHandles;
        bool s_WarnedVoiceLimit = false;
        // Files kept decoded in the resource manager: file -> asked for by Preload (kept until
        // Unload) or not (a scene's clips, a clip's Preload setting on its first play).
        std::unordered_map<std::string, bool> s_Preloaded;

        // Sounds that ended by themselves, waiting for TakeFinishedSounds.
        std::vector<FinishedSound> s_Finished;

        void RecordFinished(uint32_t handle, const Voice& voice, SoundEndReason reason)
        {
            if (voice.Music || voice.Stopping)
                return; // the music has no owner to tell; one fading out was stopped on purpose
            if (s_Finished.size() >= AudioEngine::MaxFinishedSounds)
                s_Finished.erase(s_Finished.begin()); // nobody takes them (no scene runs): the oldest go
            s_Finished.push_back({ handle, std::filesystem::path(voice.File), voice.Group, reason });
        }

        // ---- profiler (since ResetProfileCounters)
        struct GroupLimitCounts
        {
            uint64_t Stolen = 0, Rejected = 0;
        };
        std::unordered_map<std::string, GroupLimitCounts> s_GroupLimitCounts;
        uint64_t s_ProfileStolen = 0, s_ProfileRejected = 0, s_ProfileStarted = 0;
        size_t s_PeakVoices = 0;
        float s_ProfileTime = 0.0f;
        std::array<float, AudioEngine::VoiceHistoryCapacity> s_VoiceHistory{};
        size_t s_VoiceHistoryCount = 0, s_VoiceHistoryNext = 0;

        size_t CountedVoices()
        {
            size_t count = 0;
            for (const auto& [handle, voice] : s_Voices)
                if (voice.Counted)
                    count++;
            return count;
        }

        bool IsFinite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

        // ---- random variation (xorshift32: the same seed gives the same sequence)
        float RandomFloat01()
        {
            uint32_t& x = s_RandomState;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            return (float)(x >> 8) * (1.0f / 16777216.0f);
        }

        // A value in [min, max] of the range (either order; 1 for broken values). `floor` keeps it above 0.
        float RandomInRange(const glm::vec2& range, float floor)
        {
            float lo = std::isfinite(range.x) ? std::max(floor, range.x) : 1.0f;
            float hi = std::isfinite(range.y) ? std::max(floor, range.y) : 1.0f;
            if (lo > hi)
                std::swap(lo, hi);
            if (hi - lo <= 1e-6f)
                return lo; // no variation: the sequence is not used
            return lo + (hi - lo) * RandomFloat01();
        }

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
            if (g.Filter)
                ma_node_uninit(&g.Filter->Base, nullptr);
            if (g.Group)
                ma_sound_group_uninit(g.Group.get());
            g.Meter.reset();
            g.Filter.reset();
            g.Group.reset();
        }

        std::unique_ptr<FilterNode> CreateFilterNode()
        {
            auto filter = std::make_unique<FilterNode>();
            filter->Channels = ma_engine_get_channels(s_Engine);
            filter->SampleRate = (float)ma_engine_get_sample_rate(s_Engine);
            ma_uint32 channels = filter->Channels;
            ma_node_config config = ma_node_config_init();
            config.vtable = &s_FilterVTable;
            config.pInputChannels = &channels;
            config.pOutputChannels = &channels;
            if (ma_node_init(ma_engine_get_node_graph(s_Engine), &config, nullptr, &filter->Base) != MA_SUCCESS)
                return nullptr;
            return filter;
        }

        // What a group sends on to its parent.
        ma_node* GroupOutput(MixerGroup& g)
        {
            if (g.Meter)
                return (ma_node*)&g.Meter->Base;
            if (g.Filter)
                return (ma_node*)&g.Filter->Base;
            return (ma_node*)g.Group.get();
        }

        void SetFilterTargets(FilterNode& node, const AudioFilterSettings& settings)
        {
            node.LowPass.store(settings.GetLowPass(), std::memory_order_relaxed);
            node.HighPass.store(settings.GetHighPass(), std::memory_order_relaxed);
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
            // Its filter (bypassed while off): without one the group goes on unfiltered.
            g->Filter = CreateFilterNode();
            if (g->Filter)
                ma_node_attach_output_bus(g->Group.get(), 0, &g->Filter->Base, 0);
            ma_node* beforeMeter = g->Filter ? (ma_node*)&g->Filter->Base : (ma_node*)g->Group.get();
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
                ma_node_attach_output_bus(beforeMeter, 0, &g->Meter->Base, 0);
            return g;
        }

        void ApplyGains()
        {
            for (auto& g : s_Groups)
            {
                ma_sound_group_set_volume(g->Group.get(), s_Mixer.GetLocalGain(g->Name));
                if (g->Filter)
                    if (const AudioGroupSettings* settings = s_Mixer.Find(g->Name))
                        SetFilterTargets(*g->Filter, settings->Filter);
            }
        }

        void RouteVoice(Voice& voice)
        {
            ma_node* target = GroupInput(voice.Group);
            if (voice.Filter)
            {
                // The filter's way on first: the sound never goes to a node that leads nowhere.
                ma_node_attach_output_bus(&voice.Filter->Base, 0, target, 0);
                ma_node_attach_output_bus(voice.Sound.get(), 0, &voice.Filter->Base, 0);
            }
            else
                ma_node_attach_output_bus(voice.Sound.get(), 0, target, 0);
        }

        // The sound's filter as its settings say; its node is made the first time one is on.
        // `starting`: the sound has not started yet - the filter starts at its cutoffs (otherwise
        // it glides in from unfiltered).
        void ApplyVoiceFilter(Voice& voice, bool starting)
        {
            if (!voice.Filter)
            {
                if (!voice.FilterSettings.IsActive())
                    return;
                voice.Filter = CreateFilterNode();
                if (!voice.Filter)
                    return;
                if (!starting)
                    voice.Filter->Chain.StartOpen();
                SetFilterTargets(*voice.Filter, voice.FilterSettings);
                RouteVoice(voice);
                return;
            }
            SetFilterTargets(*voice.Filter, voice.FilterSettings);
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
                ma_node_attach_output_bus(GroupOutput(*g), 0, GroupInput(settings.Parent), 0);
                g->Parent = settings.Parent;
                g->Wired = true;
            }
            ApplyGains();
        }

        void ApplyVolume(Voice& voice)
        {
            ma_sound_set_volume(voice.Sound.get(), std::max(0.0f, voice.Volume * voice.VolumeVariation * voice.ClipGain));
        }

        void ApplyPitch(Voice& voice)
        {
            ma_sound_set_pitch(voice.Sound.get(), std::max(0.01f, voice.Pitch * voice.PitchVariation * voice.DopplerPitch));
        }

        glm::vec3 SoundPosition(const Voice& voice)
        {
            ma_vec3f p = ma_sound_get_position(voice.Sound.get());
            return { p.x, p.y, p.z };
        }

        glm::vec3 ListenerPosition()
        {
            ma_vec3f p = ma_engine_listener_get_position(s_Engine, 0);
            return { p.x, p.y, p.z };
        }

        // How much the distance from the listener (miniaudio's inverse model) and the directional
        // cone lower a 3D sound.
        float Attenuation(const Voice& voice, float* distanceOut = nullptr, float* coneOut = nullptr)
        {
            if (distanceOut)
                *distanceOut = 0.0f;
            if (coneOut)
                *coneOut = 1.0f;
            if (!voice.Spatial)
                return 1.0f;
            const ma_sound* sound = voice.Sound.get();
            const glm::vec3 position = SoundPosition(voice), listener = ListenerPosition();
            float distance = glm::length(position - listener);
            if (distanceOut)
                *distanceOut = distance;
            float minDistance = ma_sound_get_min_distance(sound), maxDistance = ma_sound_get_max_distance(sound);
            float gain = 1.0f;
            if (minDistance < maxDistance)
                gain = minDistance / (minDistance + ma_sound_get_rolloff(sound) * (std::clamp(distance, minDistance, maxDistance) - minDistance));
            const float cone = AudioEngine::ComputeConeGain(voice.Cone, position, voice.Direction, listener);
            if (coneOut)
                *coneOut = cone;
            return std::clamp(gain * cone, ma_sound_get_min_gain(sound), ma_sound_get_max_gain(sound));
        }

        // The sound's cone and direction to miniaudio (no cone: all around).
        void ApplyCone(Voice& voice)
        {
            ma_sound* sound = voice.Sound.get();
            if (voice.Cone.IsActive())
            {
                constexpr float toRadians = 0.017453292519943295f;
                ma_sound_set_cone(sound, voice.Cone.InnerAngle * toRadians, voice.Cone.OuterAngle * toRadians, voice.Cone.OuterGain);
            }
            else
                ma_sound_set_cone(sound, 6.283185307f, 6.283185307f, 1.0f);
            ma_sound_set_direction(sound, voice.Direction.x, voice.Direction.y, voice.Direction.z);
        }

        glm::vec3 SafeDirection(const glm::vec3& direction)
        {
            const float length = glm::length(direction);
            return std::isfinite(length) && length > 1e-6f ? direction / length : glm::vec3(0.0f, 0.0f, -1.0f);
        }

        float GroupGain(const Voice& voice)
        {
            return voice.Group.empty() ? 1.0f : s_Mixer.GetEffectiveGain(voice.Group);
        }

        // What is heard of the sound now (0 while paused).
        float AudibleVolume(const Voice& voice)
        {
            if (voice.Paused)
                return 0.0f;
            return std::max(0.0f, voice.Volume * voice.VolumeVariation * voice.ClipGain * voice.Fade) * GroupGain(voice) * Attenuation(voice);
        }

        void DestroyVoice(Voice& voice)
        {
            ma_sound_uninit(voice.Sound.get());
            if (voice.Filter)
                ma_node_uninit(&voice.Filter->Base, nullptr);
            voice.Filter.reset();
        }

        void Release(uint32_t handle)
        {
            if (auto it = s_Voices.find(handle); it != s_Voices.end())
            {
                DestroyVoice(it->second);
                s_Voices.erase(it);
            }
        }

        // The envelope heads for `target` over `seconds` (from where it is now).
        void FadeVoice(Voice& voice, float target, float seconds)
        {
            target = std::clamp(std::isfinite(target) ? target : 1.0f, 0.0f, 1.0f);
            seconds = std::isfinite(seconds) ? std::max(0.0f, seconds) : 0.0f;
            voice.FadeFrom = voice.Fade;
            voice.FadeTarget = target;
            voice.FadeElapsed = 0.0f;
            voice.FadeDuration = seconds;
            if (seconds <= 0.0f)
                voice.Fade = target;
            // -1: miniaudio's fader starts from where it is on the audio thread (no jump).
            ma_sound_set_fade_in_milliseconds(voice.Sound.get(), -1.0f, target, (ma_uint64)std::llround(seconds * 1000.0f));
        }

        // The sound's data in frames of its own sample rate (decoded data: the engine's rate).
        ma_uint32 SoundSampleRate(ma_sound* sound)
        {
            ma_uint32 sampleRate = 0;
            if (ma_sound_get_data_format(sound, nullptr, nullptr, &sampleRate, nullptr, 0) != MA_SUCCESS || sampleRate == 0)
                sampleRate = ma_engine_get_sample_rate(s_Engine);
            return sampleRate;
        }

        // Loop points of the sound's data source (seconds; see AudioEngine::SetLoopRegion).
        bool ApplyLoopRegion(Voice& voice, float start, float end)
        {
            ma_sound* sound = voice.Sound.get();
            ma_data_source* source = ma_sound_get_data_source(sound);
            if (!source)
                return false;
            ma_uint64 length = 0;
            ma_sound_get_length_in_pcm_frames(sound, &length);
            const double rate = (double)SoundSampleRate(sound);
            start = std::isfinite(start) ? std::max(0.0f, start) : 0.0f;
            end = std::isfinite(end) ? end : 0.0f;
            ma_uint64 begin = (ma_uint64)std::llround((double)start * rate);
            ma_uint64 finish = end > start ? (ma_uint64)std::llround((double)end * rate) : ~(ma_uint64)0;
            if (length > 0)
            {
                if (finish != ~(ma_uint64)0 && finish >= length)
                    finish = ~(ma_uint64)0; // up to the end
                if (begin >= length || (finish != ~(ma_uint64)0 && begin >= finish))
                    begin = 0;
            }
            return ma_data_source_set_loop_point_in_pcm_frames(source, begin, finish) == MA_SUCCESS;
        }

        void CleanupFinished()
        {
            std::vector<uint32_t> finished;
            for (auto& [handle, voice] : s_Voices)
                if (!voice.Paused && !ma_sound_is_playing(voice.Sound.get()) && !ma_sound_is_looping(voice.Sound.get()))
                    finished.push_back(handle);
            // In handle (start) order: what started first ended first.
            std::sort(finished.begin(), finished.end());
            for (uint32_t handle : finished)
            {
                RecordFinished(handle, s_Voices.at(handle), SoundEndReason::Finished);
                Release(handle);
            }
        }

        // ---- voice limit
        // The sound to stop among `candidates`: the least important first; among equals the steal
        // policy decides (None: the oldest - only less important ones are candidates then).
        uint32_t PickVictim(const std::vector<uint32_t>& candidates, AudioStealPolicy policy)
        {
            uint32_t best = 0;
            int bestPriority = 0;
            float bestVolume = 0.0f;
            for (uint32_t handle : candidates)
            {
                const Voice& v = s_Voices.at(handle);
                float volume = policy == AudioStealPolicy::Quietest ? AudibleVolume(v) : 0.0f;
                // A sound fading out is on its way already: it goes first.
                const int priority = v.Stopping ? -1 : v.Priority;
                bool better = !best || priority < bestPriority;
                if (!better && priority == bestPriority)
                {
                    if (policy == AudioStealPolicy::Quietest && std::abs(volume - bestVolume) > 1e-6f)
                        better = volume < bestVolume;
                    else
                        better = handle < best; // handles grow: the smaller one started first
                }
                if (better)
                {
                    best = handle;
                    bestPriority = priority;
                    bestVolume = volume;
                }
            }
            return best;
        }

        // The sounds that stop to make room for a new one (empty: there is room). False: no room,
        // the new sound is not played.
        bool MakeRoom(const std::string& file, const SoundParams& params, int priority, bool counted, std::vector<uint32_t>& victims)
        {
            auto mayStop = [&](const Voice& v) {
                return v.Stopping || v.Priority < priority || (v.Priority == priority && params.Steal != AudioStealPolicy::None);
            };
            // The clip's own limit: one of its instances makes room (the total stays the same).
            if (params.MaxInstances > 0)
            {
                std::vector<uint32_t> same, candidates;
                for (const auto& [handle, v] : s_Voices)
                    if (v.Counted && v.File == file)
                    {
                        same.push_back(handle);
                        if (mayStop(v))
                            candidates.push_back(handle);
                    }
                if ((int)same.size() >= params.MaxInstances)
                {
                    // More than the limit (it was lowered): enough of them go.
                    size_t needed = same.size() - (size_t)params.MaxInstances + 1;
                    while (victims.size() < needed)
                    {
                        uint32_t victim = PickVictim(candidates, params.Steal);
                        if (!victim)
                            return false;
                        victims.push_back(victim);
                        std::erase(candidates, victim);
                    }
                    return true;
                }
            }
            if (!counted)
                return true;
            std::vector<uint32_t> candidates;
            size_t count = 0;
            for (const auto& [handle, v] : s_Voices)
                if (v.Counted)
                {
                    count++;
                    if (mayStop(v))
                        candidates.push_back(handle);
                }
            if ((int)count < s_Mixer.MaxVoices)
                return true;
            size_t needed = count - (size_t)s_Mixer.MaxVoices + 1;
            while (victims.size() < needed)
            {
                uint32_t victim = PickVictim(candidates, params.Steal);
                if (!victim)
                {
                    if (!s_WarnedVoiceLimit)
                    {
                        s_WarnedVoiceLimit = true;
                        ZM_CORE_WARN("Ovozlar limiti ({}) to'ldi: yangi ovozlar ijro etilmayapti (Audio Mixer > Sozlamalar)", s_Mixer.MaxVoices);
                    }
                    victims.clear();
                    return false;
                }
                victims.push_back(victim);
                std::erase(candidates, victim);
            }
            return true;
        }

        void Steal(uint32_t handle)
        {
            if (s_StolenHandles.size() > 4096)
                s_StolenHandles.clear(); // only the recent ones matter (the inspector asks about live sources)
            s_StolenHandles.insert(handle);
            s_StolenCount++;
            s_ProfileStolen++;
            if (auto it = s_Voices.find(handle); it != s_Voices.end())
            {
                s_GroupLimitCounts[it->second.Group].Stolen++;
                ma_sound_stop(it->second.Sound.get());
                RecordFinished(handle, it->second, SoundEndReason::Stolen);
            }
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
            if (s_Preloaded.contains(file))
                return MA_SUCCESS; // decoded already: the manager shares that data
            for (const auto& [handle, voice] : s_Voices)
                if (voice.File == file && !voice.Streamed)
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

        ma_resource_manager* ResourceManager() { return ma_engine_get_resource_manager(s_Engine); }

        // Keeps the file decoded in the resource manager (see AudioEngine::Preload).
        bool PreloadFile(const std::filesystem::path& path, bool requested)
        {
            if (!s_Engine || path.empty())
                return false;
            const std::string file = path.string();
            if (auto it = s_Preloaded.find(file); it != s_Preloaded.end())
            {
                it->second = it->second || requested;
                return true;
            }
            if (AudioEngine::ShouldStream(path, false) || !CanPlay(path))
                return false;
            ma_result result = ma_resource_manager_register_file(ResourceManager(), file.c_str(), MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
            if (result != MA_SUCCESS)
            {
                ReportFailure(path, result);
                return false;
            }
            s_Preloaded[file] = requested;
            return true;
        }

        void UnloadFile(const std::string& file)
        {
            if (s_Preloaded.erase(file) && s_Engine)
                ma_resource_manager_unregister_file(ResourceManager(), file.c_str());
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
            const AudioClipSettings clip = AssetImporter::GetAudioSettings(path);
            const bool stream = music || AudioEngine::ShouldStream(path, params.Stream);
            const bool counted = !music && !group.empty();
            const int priority = std::clamp(params.Priority, 0, 255);
            std::vector<uint32_t> victims;
            if (!music && !MakeRoom(file, params, priority, counted, victims))
            {
                s_RejectedCount++;
                s_ProfileRejected++;
                s_GroupLimitCounts[group].Rejected++;
                return 0;
            }
            Voice voice;
            voice.Sound = std::make_unique<ma_sound>();
            // A clip that asks for it stays decoded from its first play on (the next plays share it).
            if (!stream && clip.Preload && !s_Preloaded.contains(file))
                PreloadFile(path, false);
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
            for (uint32_t victim : victims)
                Steal(victim);
            ma_sound* sound = voice.Sound.get();
            voice.Volume = std::isfinite(params.Volume) ? std::max(0.0f, params.Volume) : 1.0f;
            voice.Pitch = std::isfinite(params.Pitch) ? std::max(0.01f, params.Pitch) : 1.0f;
            voice.VolumeVariation = RandomInRange(params.VolumeRandom, 0.0f);
            voice.PitchVariation = RandomInRange(params.PitchRandom, 0.01f);
            voice.ClipGain = std::isfinite(clip.Volume) ? std::clamp(clip.Volume, 0.0f, AudioSettings::MaxVolume) : 1.0f;
            voice.Priority = priority;
            voice.Counted = counted;
            voice.Spatial = params.Spatial;
            voice.DopplerFactor = std::isfinite(params.DopplerFactor) ? std::clamp(params.DopplerFactor, 0.0f, AudioSettings::MaxDopplerFactor) : 1.0f;
            voice.Velocity = params.Velocity;
            ma_sound_set_looping(sound, params.Loop ? MA_TRUE : MA_FALSE);
            // The file's loop region (only heard while it loops).
            if (clip.HasLoopRegion())
                ApplyLoopRegion(voice, clip.LoopStart, clip.LoopEnd);
            // The Doppler pitch is worked out on the main thread (AudioEngine::Update): capped, smooth.
            ma_sound_set_doppler_factor(sound, 0.0f);
            if (params.Spatial)
            {
                ma_sound_set_spatialization_enabled(sound, MA_TRUE);
                ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
                ma_sound_set_min_distance(sound, std::max(0.01f, params.MinDistance));
                ma_sound_set_max_distance(sound, std::max(params.MinDistance, params.MaxDistance));
                ma_sound_set_position(sound, params.Position.x, params.Position.y, params.Position.z);
                voice.Cone = params.Cone;
                voice.Cone.Sanitize();
                voice.Direction = SafeDirection(params.Direction);
                ApplyCone(voice);
                ma_vec3f listenerVelocity = ma_engine_listener_get_velocity(s_Engine, 0);
                voice.DopplerPitch = AudioEngine::ComputeDopplerPitch(params.Position, voice.Velocity, ListenerPosition(),
                                                                      { listenerVelocity.x, listenerVelocity.y, listenerVelocity.z },
                                                                      voice.DopplerFactor * s_Mixer.DopplerFactor);
            }
            else
                ma_sound_set_spatialization_enabled(sound, MA_FALSE);
            ApplyVolume(voice);
            ApplyPitch(voice);
            if (!music && std::isfinite(params.FadeIn) && params.FadeIn > 0.0f)
            {
                voice.Fade = voice.FadeFrom = 0.0f;
                voice.FadeTarget = 1.0f;
                voice.FadeDuration = params.FadeIn;
                ma_sound_set_fade_in_milliseconds(sound, 0.0f, 1.0f, (ma_uint64)std::llround(params.FadeIn * 1000.0f));
            }
            voice.Group = group;
            voice.FilterSettings = params.Filter;
            voice.FilterSettings.Sanitize();
            ApplyVoiceFilter(voice, true);
            ma_sound_start(sound);

            voice.File = file;
            voice.Streamed = stream;
            voice.Music = music;
            uint32_t handle = s_NextHandle++;
            s_Voices[handle] = std::move(voice);
            s_ProfileStarted++;
            if (counted)
                s_PeakVoices = std::max(s_PeakVoices, CountedVoices());
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
            {
                voice->Volume = MusicGain(v);
                ApplyVolume(*voice);
            }
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

        // The Doppler pitch of the moving 3D sounds follows their movement (smoothly: a jerky frame
        // time does not make the pitch wobble).
        void UpdateDoppler(float dt)
        {
            const glm::vec3 listener = ListenerPosition();
            ma_vec3f lv = ma_engine_listener_get_velocity(s_Engine, 0);
            const glm::vec3 listenerVelocity{ lv.x, lv.y, lv.z };
            const float blend = 1.0f - std::exp(-12.0f * dt);
            for (auto& [handle, voice] : s_Voices)
            {
                if (!voice.Spatial)
                    continue;
                float target = AudioEngine::ComputeDopplerPitch(SoundPosition(voice), voice.Velocity, listener, listenerVelocity,
                                                                voice.DopplerFactor * s_Mixer.DopplerFactor);
                float pitch = voice.DopplerPitch + (target - voice.DopplerPitch) * blend;
                if (std::abs(target - pitch) < 1e-4f)
                    pitch = target;
                if (pitch != voice.DopplerPitch)
                {
                    voice.DopplerPitch = pitch;
                    ApplyPitch(voice);
                }
            }
        }

        // The sounds' fade envelopes move on (paused ones wait); one fading out is released once silent.
        void UpdateFades(float dt)
        {
            std::vector<uint32_t> silent;
            for (auto& [handle, v] : s_Voices)
            {
                if (v.Music || v.Paused)
                    continue;
                if (v.Fade != v.FadeTarget)
                {
                    v.FadeElapsed += dt;
                    float t = v.FadeDuration > 0.0f ? std::min(1.0f, v.FadeElapsed / v.FadeDuration) : 1.0f;
                    v.Fade = t >= 1.0f ? v.FadeTarget : v.FadeFrom + (v.FadeTarget - v.FadeFrom) * t;
                }
                if (v.Stopping && v.Fade <= 0.0f)
                    silent.push_back(handle);
            }
            for (uint32_t handle : silent)
                Release(handle);
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
        AudioEngine::SetRandomSeed((uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
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
    uint32_t AudioEngine::GetSampleRate() { return s_Engine ? ma_engine_get_sample_rate(s_Engine) : 0; }

    void AudioEngine::Update(float deltaSeconds)
    {
        if (!s_Engine) return;
        float dt = std::isfinite(deltaSeconds) ? std::max(0.0f, deltaSeconds) : 0.0f;
        CleanupFinished();
        UpdateMusic(dt);
        UpdateFades(dt);
        UpdateDoppler(dt);
        UpdateMeters(dt);
        // The profiler's graph: one sample per frame.
        const size_t voices = CountedVoices();
        s_PeakVoices = std::max(s_PeakVoices, voices);
        s_ProfileTime += dt;
        s_VoiceHistory[s_VoiceHistoryNext] = (float)voices;
        s_VoiceHistoryNext = (s_VoiceHistoryNext + 1) % VoiceHistoryCapacity;
        s_VoiceHistoryCount = std::min(s_VoiceHistoryCount + 1, VoiceHistoryCapacity);
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

    void AudioEngine::SetSoundVelocity(uint32_t handle, const glm::vec3& velocity)
    {
        if (Voice* v = FindVoice(handle))
            v->Velocity = IsFinite(velocity) ? velocity : glm::vec3(0.0f);
    }

    void AudioEngine::SetSoundDopplerFactor(uint32_t handle, float factor)
    {
        if (Voice* v = FindVoice(handle))
            v->DopplerFactor = std::isfinite(factor) ? std::clamp(factor, 0.0f, AudioSettings::MaxDopplerFactor) : 1.0f;
    }

    void AudioEngine::SetSoundDirection(uint32_t handle, const glm::vec3& direction)
    {
        Voice* v = FindVoice(handle);
        if (!v || !v->Spatial)
            return;
        const glm::vec3 d = SafeDirection(direction);
        if (d == v->Direction)
            return;
        v->Direction = d;
        ma_sound_set_direction(v->Sound.get(), d.x, d.y, d.z);
    }

    void AudioEngine::SetSoundCone(uint32_t handle, const AudioConeSettings& cone)
    {
        Voice* v = FindVoice(handle);
        if (!v || !v->Spatial)
            return;
        AudioConeSettings settings = cone;
        settings.Sanitize();
        if (settings == v->Cone)
            return;
        v->Cone = settings;
        ApplyCone(*v);
    }

    AudioConeSettings AudioEngine::GetSoundCone(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v ? v->Cone : AudioConeSettings();
    }

    float AudioEngine::ComputeConeGain(const AudioConeSettings& cone, const glm::vec3& sourcePosition, const glm::vec3& sourceDirection,
                                       const glm::vec3& listenerPosition)
    {
        if (!cone.IsActive())
            return 1.0f;
        const glm::vec3 toListener = listenerPosition - sourcePosition;
        const float distance = glm::length(toListener);
        if (!std::isfinite(distance) || distance <= 0.001f)
            return 1.0f; // right on the listener: no angle
        return cone.GetGain(glm::dot(SafeDirection(sourceDirection), toListener / distance));
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
        const ma_uint32 sampleRate = SoundSampleRate(sound);
        ma_uint64 length = 0;
        ma_sound_get_length_in_pcm_frames(sound, &length);
        ma_uint64 frame = (ma_uint64)std::max(0.0, (double)seconds * (double)sampleRate);
        if (length > 0)
            frame = std::min(frame, length - 1);
        ma_sound_seek_to_pcm_frame(sound, frame);
    }

    bool AudioEngine::SetLoopRegion(uint32_t handle, float start, float end)
    {
        Voice* v = FindVoice(handle);
        return v && ApplyLoopRegion(*v, start, end);
    }

    glm::vec2 AudioEngine::GetLoopRegion(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        if (!v)
            return glm::vec2(0.0f);
        ma_sound* sound = v->Sound.get();
        ma_data_source* source = ma_sound_get_data_source(sound);
        ma_uint64 begin = 0, end = ~(ma_uint64)0, length = 0;
        if (source)
            ma_data_source_get_loop_point_in_pcm_frames(source, &begin, &end);
        ma_sound_get_length_in_pcm_frames(sound, &length);
        if (end == ~(ma_uint64)0 || (length > 0 && end > length))
            end = length;
        const double rate = (double)SoundSampleRate(sound);
        return { (float)((double)begin / rate), (float)((double)end / rate) };
    }

    void AudioEngine::SetVolume(uint32_t handle, float volume)
    {
        if (Voice* v = FindVoice(handle))
        {
            v->Volume = std::isfinite(volume) ? std::max(0.0f, volume) : 1.0f;
            ApplyVolume(*v);
        }
    }

    float AudioEngine::GetVolume(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v ? v->Volume : 0.0f;
    }

    void AudioEngine::SetPitch(uint32_t handle, float pitch)
    {
        if (Voice* v = FindVoice(handle))
        {
            v->Pitch = std::isfinite(pitch) ? std::max(0.01f, pitch) : 1.0f;
            ApplyPitch(*v);
        }
    }

    float AudioEngine::GetPitch(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v ? v->Pitch : 0.0f;
    }

    void AudioEngine::SetLooping(uint32_t handle, bool loop)
    {
        if (Voice* v = FindVoice(handle))
            ma_sound_set_looping(v->Sound.get(), loop ? MA_TRUE : MA_FALSE);
    }

    void AudioEngine::SetSoundDistances(uint32_t handle, float minDistance, float maxDistance)
    {
        Voice* v = FindVoice(handle);
        if (!v || !std::isfinite(minDistance) || !std::isfinite(maxDistance))
            return;
        ma_sound_set_min_distance(v->Sound.get(), std::max(0.01f, minDistance));
        ma_sound_set_max_distance(v->Sound.get(), std::max(minDistance, maxDistance));
    }

    bool AudioEngine::Pause(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        if (!v || v->Music)
            return false;
        if (!v->Paused)
        {
            ma_sound_stop(v->Sound.get());
            v->Paused = true;
        }
        return true;
    }

    bool AudioEngine::Resume(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        if (!v || v->Music)
            return false;
        if (v->Paused)
        {
            v->Paused = false;
            ma_sound_start(v->Sound.get());
        }
        return true;
    }

    bool AudioEngine::IsPaused(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v && v->Paused;
    }

    bool AudioEngine::GetSoundInfo(uint32_t handle, SoundInfo& info)
    {
        Voice* v = FindVoice(handle);
        if (!v)
            return false;
        ma_sound* sound = v->Sound.get();
        SoundInfo out;
        out.Playing = ma_sound_is_playing(sound);
        out.Paused = v->Paused;
        out.Looping = ma_sound_is_looping(sound);
        out.Spatial = v->Spatial;
        out.Streamed = v->Streamed;
        out.Music = v->Music;
        out.Group = v->Group;
        ma_sound_get_cursor_in_seconds(sound, &out.Time);
        ma_sound_get_length_in_seconds(sound, &out.Length);
        out.Volume = v->Volume;
        out.VolumeVariation = v->VolumeVariation;
        out.GroupGain = GroupGain(*v);
        out.Attenuation = Attenuation(*v, &out.Distance, &out.ConeGain);
        out.Cone = v->Cone;
        out.Direction = v->Direction;
        out.ClipGain = v->ClipGain;
        out.Fade = v->Fade;
        out.FadeTarget = v->FadeTarget;
        out.Stopping = v->Stopping;
        out.Preloaded = s_Preloaded.contains(v->File);
        out.Filter = v->FilterSettings;
        out.AudibleVolume = std::max(0.0f, v->Volume * v->VolumeVariation * v->ClipGain * v->Fade) * out.GroupGain * out.Attenuation;
        out.Pitch = v->Pitch;
        out.PitchVariation = v->PitchVariation;
        out.DopplerPitch = v->DopplerPitch;
        out.AppliedPitch = std::max(0.01f, v->Pitch * v->PitchVariation * v->DopplerPitch);
        out.DopplerFactor = v->DopplerFactor;
        out.Velocity = v->Velocity;
        out.Priority = v->Priority;
        info = std::move(out);
        return true;
    }

    bool AudioEngine::WasStolen(uint32_t handle) { return handle && s_StolenHandles.contains(handle); }

    AudioVoiceStats AudioEngine::GetVoiceStats()
    {
        AudioVoiceStats stats;
        stats.Voices = CountedVoices();
        stats.MaxVoices = s_Mixer.MaxVoices;
        stats.Stolen = s_StolenCount;
        stats.Rejected = s_RejectedCount;
        return stats;
    }

    std::vector<FinishedSound> AudioEngine::TakeFinishedSounds()
    {
        std::vector<FinishedSound> finished;
        finished.swap(s_Finished);
        return finished;
    }

    size_t AudioEngine::GetFinishedSoundCount() { return s_Finished.size(); }

    // ------------------------------------------------------------------ profiler
    const AudioGroupProfile* AudioProfile::FindGroup(std::string_view name) const
    {
        for (const AudioGroupProfile& g : Groups)
            if (g.Name == name)
                return &g;
        return nullptr;
    }

    std::string AudioProfile::Format() const
    {
        if (!Available)
            return "audio yo'q";
        std::string text = std::format("{}/{} ovoz (cho'qqi {})", Voices, MaxVoices, PeakVoices);
        if (Paused || Stopping || Streamed)
            text += std::format(", pauza {}, so'nmoqda {}, oqim {}", Paused, Stopping, Streamed);
        if (Music)
            text += std::format(" | musiqa {}", Music);
        if (Previews)
            text += std::format(" | ko'rib chiqish {}", Previews);
        text += std::format(" | to'xtatilgan {}, rad etilgan {}", Stolen, Rejected);
        std::string groups;
        for (const AudioGroupProfile& g : Groups)
            if (g.Voices)
                groups += std::format("{}{} {}", groups.empty() ? "" : ", ", g.Name, g.Voices);
        if (!groups.empty())
            text += " | " + groups;
        return text;
    }

    AudioProfile AudioEngine::GetProfile()
    {
        AudioProfile profile;
        profile.Available = s_Engine != nullptr;
        profile.MaxVoices = s_Mixer.MaxVoices;
        profile.PeakVoices = s_PeakVoices;
        profile.Started = s_ProfileStarted;
        profile.Stolen = s_ProfileStolen;
        profile.Rejected = s_ProfileRejected;
        profile.Preloaded = s_Preloaded.size();
        profile.SinceReset = s_ProfileTime;
        std::unordered_map<std::string, size_t> index;
        for (const auto& [name, depth] : s_Mixer.GetTreeOrder())
        {
            AudioGroupProfile g;
            g.Name = name;
            g.Depth = depth;
            if (const AudioGroupSettings* settings = s_Mixer.Find(name))
                g.Parent = settings->Parent;
            g.Gain = s_Mixer.GetEffectiveGain(name);
            if (MixerGroup* node = FindGroup(name))
                g.Level = node->Level.Hold;
            if (auto it = s_GroupLimitCounts.find(name); it != s_GroupLimitCounts.end())
            {
                g.Stolen = it->second.Stolen;
                g.Rejected = it->second.Rejected;
            }
            index[name] = profile.Groups.size();
            profile.Groups.push_back(std::move(g));
        }
        for (const auto& [handle, voice] : s_Voices)
        {
            const bool playing = ma_sound_is_playing(voice.Sound.get());
            profile.Sounds++;
            profile.Playing += playing ? 1 : 0;
            profile.Paused += voice.Paused ? 1 : 0;
            profile.Stopping += voice.Stopping ? 1 : 0;
            profile.Streamed += voice.Streamed ? 1 : 0;
            profile.Spatial += voice.Spatial ? 1 : 0;
            profile.Filtered += voice.FilterSettings.IsActive() ? 1 : 0;
            profile.Music += voice.Music ? 1 : 0;
            profile.Previews += voice.Group.empty() ? 1 : 0;
            profile.Voices += voice.Counted ? 1 : 0;
            auto it = index.find(voice.Group);
            if (it == index.end())
                continue;
            AudioGroupProfile& g = profile.Groups[it->second];
            g.Voices++;
            g.Paused += voice.Paused ? 1 : 0;
            g.Stopping += voice.Stopping ? 1 : 0;
            g.Streamed += voice.Streamed ? 1 : 0;
            g.Spatial += voice.Spatial ? 1 : 0;
            // Up the tree: every parent hears it too.
            for (std::string parent = voice.Group; !parent.empty();)
            {
                auto p = index.find(parent);
                if (p == index.end())
                    break;
                AudioGroupProfile& up = profile.Groups[p->second];
                up.TotalVoices++;
                parent = up.Parent;
            }
        }
        return profile;
    }

    void AudioEngine::ResetProfileCounters()
    {
        s_GroupLimitCounts.clear();
        s_ProfileStolen = s_ProfileRejected = s_ProfileStarted = 0;
        s_PeakVoices = CountedVoices();
        s_ProfileTime = 0.0f;
    }

    std::vector<float> AudioEngine::GetVoiceHistory()
    {
        std::vector<float> history;
        history.reserve(s_VoiceHistoryCount);
        const size_t start = (s_VoiceHistoryNext + VoiceHistoryCapacity - s_VoiceHistoryCount) % VoiceHistoryCapacity;
        for (size_t i = 0; i < s_VoiceHistoryCount; i++)
            history.push_back(s_VoiceHistory[(start + i) % VoiceHistoryCapacity]);
        return history;
    }

    void AudioEngine::SetRandomSeed(uint32_t seed)
    {
        // Spread the seed's bits (nearby seeds give unrelated sequences); xorshift must not start at 0.
        uint32_t x = seed + 0x9E3779B9u;
        x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
        x = (x ^ (x >> 13)) * 0xC2B2AE35u;
        x ^= x >> 16;
        s_RandomState = x ? x : 0x9E3779B9u;
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
        const AudioClipSettings clip = AssetImporter::GetAudioSettings(path);
        if (clip.LoadMode != AudioLoadMode::Auto)
            return clip.LoadMode == AudioLoadMode::Stream;
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

    void AudioEngine::Stop(uint32_t handle, float fadeSeconds)
    {
        Voice* v = FindVoice(handle);
        if (!v) return;
        if (!std::isfinite(fadeSeconds) || fadeSeconds <= 0.0f || v->Paused || v->Music || !ma_sound_is_playing(v->Sound.get()))
        {
            Stop(handle);
            return;
        }
        v->Stopping = true;
        FadeVoice(*v, 0.0f, fadeSeconds);
    }

    bool AudioEngine::IsStopping(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v && v->Stopping;
    }

    bool AudioEngine::FadeTo(uint32_t handle, float volume, float seconds)
    {
        Voice* v = FindVoice(handle);
        if (!v || v->Music)
            return false;
        if (v->Stopping)
            return true; // on its way out: a stop is not undone by a fade
        FadeVoice(*v, volume, seconds);
        return true;
    }

    float AudioEngine::GetFade(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v ? v->Fade : 1.0f;
    }

    void AudioEngine::StopAll()
    {
        for (auto& [handle, voice] : s_Voices)
        {
            ma_sound_stop(voice.Sound.get());
            DestroyVoice(voice);
        }
        s_Voices.clear();
        s_Finished.clear(); // nothing that played before is told about any more
        UnloadAll();
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

    // ------------------------------------------------------------------ filters
    bool AudioEngine::SetSoundFilter(uint32_t handle, const AudioFilterSettings& filter)
    {
        Voice* v = FindVoice(handle);
        if (!v)
            return false;
        AudioFilterSettings settings = filter;
        settings.Sanitize();
        if (settings == v->FilterSettings && (v->Filter || !settings.IsActive()))
            return true;
        v->FilterSettings = settings;
        ApplyVoiceFilter(*v, false);
        return true;
    }

    AudioFilterSettings AudioEngine::GetSoundFilter(uint32_t handle)
    {
        Voice* v = FindVoice(handle);
        return v ? v->FilterSettings : AudioFilterSettings();
    }

    bool AudioEngine::SetGroupFilter(const std::string& group, const AudioFilterSettings& filter)
    {
        AudioGroupSettings* g = s_Mixer.Find(group);
        if (!g) return false;
        g->Filter = filter;
        g->Filter.Sanitize();
        if (MixerGroup* node = FindGroup(group); node && node->Filter)
            SetFilterTargets(*node->Filter, g->Filter);
        return true;
    }

    AudioFilterSettings AudioEngine::GetGroupFilter(const std::string& group)
    {
        const AudioGroupSettings* g = s_Mixer.Find(group);
        return g ? g->Filter : AudioFilterSettings();
    }

    // ------------------------------------------------------------------ sound files
    bool AudioEngine::Preload(const std::filesystem::path& path) { return PreloadFile(path, true); }

    void AudioEngine::Unload(const std::filesystem::path& path) { UnloadFile(path.string()); }

    void AudioEngine::UnloadAll()
    {
        if (s_Engine)
            for (const auto& [file, requested] : s_Preloaded)
                ma_resource_manager_unregister_file(ResourceManager(), file.c_str());
        s_Preloaded.clear();
    }

    bool AudioEngine::IsPreloaded(const std::filesystem::path& path) { return s_Preloaded.contains(path.string()); }

    size_t AudioEngine::GetPreloadedCount() { return s_Preloaded.size(); }

    void AudioEngine::SetScenePreloads(const std::vector<std::filesystem::path>& paths)
    {
        std::unordered_set<std::string> keep;
        for (const std::filesystem::path& path : paths)
            keep.insert(path.string());
        std::vector<std::string> drop;
        for (const auto& [file, requested] : s_Preloaded)
            if (!requested && !keep.contains(file))
                drop.push_back(file);
        for (const std::string& file : drop)
            UnloadFile(file);
        for (const std::filesystem::path& path : paths)
            PreloadFile(path, false);
    }

    size_t AudioEngine::ReloadFile(const std::filesystem::path& path, bool* failed)
    {
        const std::string file = path.string();
        auto it = s_Preloaded.find(file);
        if (!s_Engine || it == s_Preloaded.end())
            return 0;
        const bool requested = it->second;
        UnloadFile(file);
        // New import settings: a clip that streams now, or (kept for its Preload setting) has it
        // switched off, is let go instead.
        if (ShouldStream(path, false) || (!requested && !AssetImporter::GetAudioSettings(path).Preload))
            return 1;
        // Checked with a decoder of our own (the shortcut for files in use does not apply here).
        ma_decoder decoder;
        ma_result result = ma_decoder_init_file(file.c_str(), nullptr, &decoder);
        if (result == MA_SUCCESS)
        {
            ma_decoder_uninit(&decoder);
            result = ma_resource_manager_register_file(ResourceManager(), file.c_str(), MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
        }
        if (result != MA_SUCCESS)
        {
            ReportFailure(path, result);
            if (failed)
                *failed = true;
            return 1;
        }
        s_Preloaded[file] = requested;
        return 1;
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

    float AudioEngine::ComputeDopplerPitch(const glm::vec3& sourcePosition, const glm::vec3& sourceVelocity, const glm::vec3& listenerPosition,
                                           const glm::vec3& listenerVelocity, float factor)
    {
        if (!std::isfinite(factor) || factor <= 0.0f)
            return 1.0f;
        glm::vec3 toListener = listenerPosition - sourcePosition;
        float distance = glm::length(toListener);
        if (!std::isfinite(distance) || distance < 1e-4f || !IsFinite(sourceVelocity) || !IsFinite(listenerVelocity))
            return 1.0f;
        glm::vec3 direction = toListener / distance;
        // Speeds along the line between them; capped so the pitch cannot run away (a teleport).
        const float limit = 0.5f * SpeedOfSound / factor;
        float listenerSpeed = std::clamp(glm::dot(direction, listenerVelocity), -limit, limit);
        float sourceSpeed = std::clamp(glm::dot(direction, sourceVelocity), -limit, limit);
        float pitch = (SpeedOfSound - factor * listenerSpeed) / (SpeedOfSound - factor * sourceSpeed);
        return std::clamp(pitch, 0.5f, 2.0f);
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
