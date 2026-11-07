#include "Zamin/Audio/Audio.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/SaveData.h"
#include "Zamin/Project/Project.h"

namespace Zamin {

    bool Audio::PlayMusic(const std::string& assetPath, float fadeSeconds, bool loop)
    {
        return !assetPath.empty() && AudioEngine::PlayMusic(Project::GetAssetFileSystemPath(assetPath), fadeSeconds, loop);
    }

    bool Audio::CrossfadeTo(const std::string& assetPath, float seconds, bool loop)
    {
        return !assetPath.empty() && AudioEngine::CrossfadeMusic(Project::GetAssetFileSystemPath(assetPath), seconds, loop);
    }

    void Audio::StopMusic(float fadeSeconds) { AudioEngine::StopMusic(fadeSeconds); }
    void Audio::PauseMusic() { AudioEngine::PauseMusic(); }
    void Audio::ResumeMusic() { AudioEngine::ResumeMusic(); }
    bool Audio::IsMusicPaused() { return AudioEngine::IsMusicPaused(); }
    bool Audio::IsMusicPlaying() { return AudioEngine::IsMusicPlaying(); }

    std::string Audio::GetMusic()
    {
        std::filesystem::path music = AudioEngine::GetMusic();
        return music.empty() ? std::string() : Project::GetAssetRelativePath(music);
    }

    void Audio::SetMusicVolume(float volume) { AudioEngine::SetMusicVolume(volume); }
    float Audio::GetMusicVolume() { return AudioEngine::GetMusicVolume(); }

    bool Audio::SetGroupVolume(const std::string& group, float volume) { return AudioEngine::SetGroupVolume(group, volume); }
    float Audio::GetGroupVolume(const std::string& group) { return AudioEngine::GetGroupVolume(group); }
    bool Audio::SetGroupMuted(const std::string& group, bool muted) { return AudioEngine::SetGroupMuted(group, muted); }
    bool Audio::IsGroupMuted(const std::string& group) { return AudioEngine::IsGroupMuted(group); }
    bool Audio::HasGroup(const std::string& group) { return AudioEngine::HasGroup(group); }

    void Audio::ResetMixer()
    {
        Ref<Project> project = Project::GetActive();
        AudioEngine::SetMixerSettings(project ? project->GetConfig().Audio : AudioSettings());
    }

    bool Audio::SetGroupLowPass(const std::string& group, float cutoffHz)
    {
        if (!AudioEngine::HasGroup(group) || !std::isfinite(cutoffHz))
            return false;
        AudioFilterSettings filter = AudioEngine::GetGroupFilter(group);
        filter.LowPass = cutoffHz > 0.0f;
        if (filter.LowPass)
            filter.LowPassCutoff = cutoffHz;
        return AudioEngine::SetGroupFilter(group, filter);
    }

    bool Audio::SetGroupHighPass(const std::string& group, float cutoffHz)
    {
        if (!AudioEngine::HasGroup(group) || !std::isfinite(cutoffHz))
            return false;
        AudioFilterSettings filter = AudioEngine::GetGroupFilter(group);
        filter.HighPass = cutoffHz > 0.0f;
        if (filter.HighPass)
            filter.HighPassCutoff = cutoffHz;
        return AudioEngine::SetGroupFilter(group, filter);
    }

    bool Audio::SetGroupFilter(const std::string& group, const AudioFilterSettings& filter) { return AudioEngine::SetGroupFilter(group, filter); }
    AudioFilterSettings Audio::GetGroupFilter(const std::string& group) { return AudioEngine::GetGroupFilter(group); }

    // ------------------------------------------------------------------ player settings
    bool Audio::SaveSettings()
    {
        AudioPlayerSettings settings = AudioPlayerSettings::Capture(AudioEngine::GetMixerSettings(), AudioEngine::GetMusicVolume());
        SaveData::SetString(SettingsKey, settings.ToJson());
        return SaveData::Save();
    }

    bool Audio::LoadSettings()
    {
        if (SaveData::GetType(SettingsKey) != SaveValueType::String)
            return false;
        AudioPlayerSettings settings;
        if (!AudioPlayerSettings::FromJson(SaveData::GetString(SettingsKey), settings))
        {
            ZM_CORE_WARN("Saqlangan ovoz sozlamalari o'qilmadi ('{}' kaliti) - loyiha standarti ishlatiladi", SettingsKey);
            return false;
        }
        AudioSettings mixer = AudioEngine::GetMixerSettings();
        settings.ApplyTo(mixer);
        AudioEngine::SetMixerSettings(mixer);
        AudioEngine::SetMusicVolume(settings.MusicVolume);
        return true;
    }

    bool Audio::HasSavedSettings() { return SaveData::GetType(SettingsKey) == SaveValueType::String; }

    void Audio::ClearSavedSettings()
    {
        SaveData::DeleteKey(SettingsKey);
        SaveData::Save();
        ResetMixer();
        AudioEngine::SetMusicVolume(1.0f);
    }

    // ------------------------------------------------------------------ sound files
    float Audio::GetClipLength(const std::string& assetPath)
    {
        return assetPath.empty() ? 0.0f : AudioClip::GetLength(Project::GetAssetFileSystemPath(assetPath));
    }

    const AudioFileInfo& Audio::GetClipInfo(const std::string& assetPath)
    {
        return AudioClip::GetInfo(Project::GetAssetFileSystemPath(assetPath));
    }

    bool Audio::Preload(const std::string& assetPath)
    {
        return !assetPath.empty() && AudioEngine::Preload(Project::GetAssetFileSystemPath(assetPath));
    }

    void Audio::Unload(const std::string& assetPath)
    {
        if (!assetPath.empty())
            AudioEngine::Unload(Project::GetAssetFileSystemPath(assetPath));
    }

    bool Audio::IsPreloaded(const std::string& assetPath)
    {
        return !assetPath.empty() && AudioEngine::IsPreloaded(Project::GetAssetFileSystemPath(assetPath));
    }

    void Audio::StopSound(uint32_t handle, float fadeSeconds) { AudioEngine::Stop(handle, fadeSeconds); }

    const AudioSpatialSettings& Audio::GetSpatialDefaults()
    {
        static const AudioSpatialSettings s_Defaults;
        Ref<Project> project = Project::GetActive();
        return project ? project->GetConfig().Audio.Spatial : s_Defaults;
    }

    // ------------------------------------------------------------------ one-shot sounds
    uint32_t Audio::PlaySound(const std::string& assetPath, float volume, float pitch, const std::string& group)
    {
        SoundParams params;
        params.Volume = volume;
        params.Pitch = pitch;
        params.Group = group;
        return AudioEngine::Play(Project::GetAssetFileSystemPath(assetPath), params);
    }

    uint32_t Audio::PlaySoundAt(const std::string& assetPath, const glm::vec3& position, float volume, float pitch, const std::string& group)
    {
        SoundParams params;
        params.Volume = volume;
        params.Pitch = pitch;
        params.Group = group;
        params.Spatial = true;
        params.Position = position;
        return AudioEngine::Play(Project::GetAssetFileSystemPath(assetPath), params);
    }

    uint32_t Audio::PlaySound(const std::string& assetPath, const SoundParams& params)
    {
        if (assetPath.empty())
            return 0;
        return AudioEngine::Play(Project::GetAssetFileSystemPath(assetPath), params);
    }

}
