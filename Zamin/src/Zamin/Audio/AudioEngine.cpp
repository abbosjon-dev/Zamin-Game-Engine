#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Core/Log.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zamin {

    namespace {
        ma_engine* s_Engine = nullptr;
        std::unordered_map<uint32_t, std::unique_ptr<ma_sound>> s_Sounds;
        std::unordered_map<uint32_t, std::string> s_SoundFiles; // the file each sound was loaded from
        uint32_t s_NextHandle = 1;

        void Release(uint32_t handle)
        {
            if (auto it = s_Sounds.find(handle); it != s_Sounds.end())
            {
                ma_sound_uninit(it->second.get());
                s_Sounds.erase(it);
            }
            s_SoundFiles.erase(handle);
        }

        void CleanupFinished()
        {
            std::vector<uint32_t> finished;
            for (auto& [handle, sound] : s_Sounds)
                if (!ma_sound_is_playing(sound.get()) && !ma_sound_is_looping(sound.get()))
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
            for (const auto& [handle, loaded] : s_SoundFiles)
                if (loaded == file)
                    return MA_SUCCESS;
            ma_decoder decoder;
            ma_result result = ma_decoder_init_file(file.c_str(), nullptr, &decoder);
            if (result == MA_SUCCESS)
                ma_decoder_uninit(&decoder);
            return result;
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
        ZM_CORE_INFO("{}", withoutDevice ? "Audio tizimi qurilmasiz ishga tushdi" : "Audio tizimi ishga tushdi");
    }

    void AudioEngine::Shutdown()
    {
        if (!s_Engine) return;
        StopAll();
        ma_engine_uninit(s_Engine);
        delete s_Engine;
        s_Engine = nullptr;
    }

    bool AudioEngine::IsAvailable() { return s_Engine != nullptr; }

    uint32_t AudioEngine::Play(const std::filesystem::path& path, float volume, float pitch, bool loop)
    {
        if (!s_Engine) return 0;
        CleanupFinished();

        // A sound that failed and did not change since is not decoded again (a script may play it
        // every jump); its error was reported once.
        if (!AssetImportLog::ShouldRetry(path))
            return 0;
        const std::string file = path.string();
        auto sound = std::make_unique<ma_sound>();
        ma_result result = CheckDecodable(file);
        if (result == MA_SUCCESS)
            result = ma_sound_init_from_file(s_Engine, file.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get());
        if (result != MA_SUCCESS)
        {
            std::string reason = AssetImportLog::CheckReadable(path);
            if (reason.empty())
                reason = std::format("audio fayl o'qilmadi - WAV, MP3 yoki FLAC bo'lishi kerak (miniaudio: {})", ma_result_description(result));
            AssetImportLog::Report(path, reason);
            return 0;
        }
        AssetImportLog::Clear(path);
        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_pitch(sound.get(), pitch);
        ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(sound.get());

        uint32_t handle = s_NextHandle++;
        s_Sounds[handle] = std::move(sound);
        s_SoundFiles[handle] = file;
        return handle;
    }

    uint32_t AudioEngine::PlaySpatial(const std::filesystem::path& path, const glm::vec3& position, float volume, float pitch,
                                      bool loop, float minDistance, float maxDistance)
    {
        uint32_t handle = Play(path, volume, pitch, loop);
        auto it = s_Sounds.find(handle);
        if (it == s_Sounds.end()) return 0;
        ma_sound* sound = it->second.get();
        ma_sound_set_spatialization_enabled(sound, MA_TRUE);
        ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(sound, std::max(0.01f, minDistance));
        ma_sound_set_max_distance(sound, std::max(minDistance, maxDistance));
        ma_sound_set_position(sound, position.x, position.y, position.z);
        return handle;
    }

    void AudioEngine::SetSoundPosition(uint32_t handle, const glm::vec3& p)
    {
        if (auto it = s_Sounds.find(handle); it != s_Sounds.end())
            ma_sound_set_position(it->second.get(), p.x, p.y, p.z);
    }

    void AudioEngine::SetListener(const glm::vec3& p, const glm::vec3& f, const glm::vec3& u)
    {
        if (!s_Engine) return;
        ma_engine_listener_set_position(s_Engine, 0, p.x, p.y, p.z);
        ma_engine_listener_set_direction(s_Engine, 0, f.x, f.y, f.z);
        ma_engine_listener_set_world_up(s_Engine, 0, u.x, u.y, u.z);
    }

    bool AudioEngine::IsPlaying(uint32_t handle)
    {
        auto it = s_Sounds.find(handle);
        return it != s_Sounds.end() && ma_sound_is_playing(it->second.get());
    }

    float AudioEngine::GetCursor(uint32_t handle)
    {
        auto it = s_Sounds.find(handle);
        float seconds = 0.0f;
        if (it != s_Sounds.end() && ma_sound_get_cursor_in_seconds(it->second.get(), &seconds) == MA_SUCCESS)
            return seconds;
        return 0.0f;
    }

    float AudioEngine::GetLength(uint32_t handle)
    {
        auto it = s_Sounds.find(handle);
        float seconds = 0.0f;
        if (it != s_Sounds.end() && ma_sound_get_length_in_seconds(it->second.get(), &seconds) == MA_SUCCESS)
            return seconds;
        return 0.0f;
    }

    void AudioEngine::Seek(uint32_t handle, float seconds)
    {
        auto it = s_Sounds.find(handle);
        if (it == s_Sounds.end())
            return;
        ma_sound* sound = it->second.get();
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
        if (auto it = s_Sounds.find(handle); it != s_Sounds.end())
            ma_sound_set_volume(it->second.get(), std::max(0.0f, volume));
    }

    void AudioEngine::Stop(uint32_t handle)
    {
        auto it = s_Sounds.find(handle);
        if (it == s_Sounds.end()) return;
        ma_sound_stop(it->second.get());
        Release(handle);
    }

    void AudioEngine::StopAll()
    {
        for (auto& [handle, sound] : s_Sounds)
        {
            ma_sound_stop(sound.get());
            ma_sound_uninit(sound.get());
        }
        s_Sounds.clear();
        s_SoundFiles.clear();
    }

    size_t AudioEngine::GetPlayingCount()
    {
        size_t count = 0;
        for (auto& [handle, sound] : s_Sounds)
            if (ma_sound_is_playing(sound.get()))
                count++;
        return count;
    }

    void AudioEngine::SetMasterVolume(float volume)
    {
        if (s_Engine) ma_engine_set_volume(s_Engine, volume);
    }

}
