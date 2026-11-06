#include "Zamin/Audio/Audio.h"
#include "Zamin/Audio/AudioEngine.h"
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
