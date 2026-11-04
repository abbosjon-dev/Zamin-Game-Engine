#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Core/Log.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

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
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(sound.get());

        uint32_t handle = s_NextHandle++;
        s_Sounds[handle] = std::move(sound);
        return handle;
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
