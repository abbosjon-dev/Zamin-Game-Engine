#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Core/Log.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace Zamin {

    namespace {
        ma_engine* s_Engine = nullptr;
        std::unordered_map<uint32_t, std::unique_ptr<ma_sound>> s_Sounds;
        uint32_t s_NextHandle = 1;

        void CleanupFinished()
        {
            for (auto it = s_Sounds.begin(); it != s_Sounds.end();)
            {
                if (!ma_sound_is_playing(it->second.get()) && !ma_sound_is_looping(it->second.get()))
                {
                    ma_sound_uninit(it->second.get());
                    it = s_Sounds.erase(it);
                }
                else ++it;
            }
        }
    }

    void AudioEngine::Init()
    {
        s_Engine = new ma_engine();
        if (ma_engine_init(nullptr, s_Engine) != MA_SUCCESS)
        {
            ZM_CORE_WARN("Audio qurilmasi topilmadi - ovoz o'chirilgan");
            delete s_Engine;
            s_Engine = nullptr;
            return;
        }
        ZM_CORE_INFO("Audio tizimi ishga tushdi");
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

        auto sound = std::make_unique<ma_sound>();
        if (ma_sound_init_from_file(s_Engine, path.string().c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get()) != MA_SUCCESS)
        {
            ZM_CORE_ERROR("Ovoz fayli yuklanmadi: '{}'", path.string());
            return 0;
        }
        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_pitch(sound.get(), pitch);
        ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(sound.get());

        uint32_t handle = s_NextHandle++;
        s_Sounds[handle] = std::move(sound);
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

    void AudioEngine::Stop(uint32_t handle)
    {
        auto it = s_Sounds.find(handle);
        if (it == s_Sounds.end()) return;
        ma_sound_stop(it->second.get());
        ma_sound_uninit(it->second.get());
        s_Sounds.erase(it);
    }

    void AudioEngine::StopAll()
    {
        for (auto& [handle, sound] : s_Sounds)
        {
            ma_sound_stop(sound.get());
            ma_sound_uninit(sound.get());
        }
        s_Sounds.clear();
    }

    void AudioEngine::SetMasterVolume(float volume)
    {
        if (s_Engine) ma_engine_set_volume(s_Engine, volume);
    }

}
