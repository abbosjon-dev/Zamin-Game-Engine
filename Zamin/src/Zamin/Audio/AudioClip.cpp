#include "Zamin/Audio/AudioClip.h"

#include <string>
#include <unordered_map>

namespace Zamin {

    namespace {
        struct CachedInfo
        {
            std::filesystem::file_time_type Time{};
            uintmax_t Size = 0;
            AudioFileInfo Info;
        };
        std::unordered_map<std::string, CachedInfo> s_Cache;
    }

    const AudioFileInfo& AudioClip::GetInfo(const std::filesystem::path& path)
    {
        std::error_code ec;
        std::filesystem::file_time_type time = std::filesystem::last_write_time(path, ec);
        uintmax_t size = ec ? 0 : std::filesystem::file_size(path, ec);
        if (ec)
            size = 0;
        const std::string key = path.lexically_normal().string();
        auto it = s_Cache.find(key);
        if (it != s_Cache.end() && it->second.Time == time && it->second.Size == size)
            return it->second.Info;
        if (s_Cache.size() > 2048)
            s_Cache.clear(); // a long session over many projects: start over
        CachedInfo& entry = s_Cache[key];
        entry.Time = time;
        entry.Size = size;
        entry.Info = AssetInfo::ReadAudio(path);
        return entry.Info;
    }

    float AudioClip::GetLength(const std::filesystem::path& path)
    {
        const AudioFileInfo& info = GetInfo(path);
        return info.Valid ? (float)info.Duration : 0.0f;
    }

    uint64_t AudioClip::EstimateDecodedSize(const AudioFileInfo& info, uint32_t sampleRate)
    {
        if (!info.Valid || info.SampleRate == 0)
            return 0;
        double frames = (double)info.Frames;
        if (sampleRate > 0 && sampleRate != info.SampleRate)
            frames = frames * sampleRate / info.SampleRate; // resampled when decoded
        return (uint64_t)(frames * info.Channels * sizeof(float));
    }

    void AudioClip::ClearCache() { s_Cache.clear(); }

}
