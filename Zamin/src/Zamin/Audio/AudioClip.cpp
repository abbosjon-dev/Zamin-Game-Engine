#include "Zamin/Audio/AudioClip.h"

#include <algorithm>
#include <cmath>
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

    // ------------------------------------------------------------------ loop region
    void AudioClipSettings::GetLoopRegion(float duration, float& start, float& end) const
    {
        duration = std::isfinite(duration) ? std::max(0.0f, duration) : 0.0f;
        start = std::isfinite(LoopStart) ? std::clamp(LoopStart, 0.0f, duration) : 0.0f;
        end = std::isfinite(LoopEnd) ? LoopEnd : 0.0f;
        if (end <= start || end > duration)
            end = duration;
        if (end <= start) // a region at the very end: the whole file
            start = 0.0f;
    }

    void AudioClipSettings::Sanitize()
    {
        LoopStart = std::isfinite(LoopStart) ? std::max(0.0f, LoopStart) : 0.0f;
        LoopEnd = std::isfinite(LoopEnd) ? std::max(0.0f, LoopEnd) : 0.0f;
        if (LoopEnd > 0.0f && LoopEnd <= LoopStart)
            LoopEnd = 0.0f;
    }

    // ------------------------------------------------------------------ waveform view
    void WaveformView::Clamp()
    {
        const double minSpan = std::clamp(std::isfinite(MinSpan) ? MinSpan : 1e-4, 1e-6, 1.0);
        if (!std::isfinite(Start) || !std::isfinite(End))
            Reset();
        double span = std::clamp(End - Start, minSpan, 1.0);
        double start = std::clamp(Start, 0.0, 1.0 - span);
        Start = start;
        End = start + span;
    }

    void WaveformView::ZoomAt(double anchor, double factor)
    {
        if (!std::isfinite(anchor) || !std::isfinite(factor) || factor <= 0.0)
            return;
        const double minSpan = std::clamp(MinSpan, 1e-6, 1.0);
        const double span = GetSpan();
        const double newSpan = std::clamp(span / factor, minSpan, 1.0);
        // The anchor keeps its place across the view.
        const double x = span > 0.0 ? std::clamp((anchor - Start) / span, 0.0, 1.0) : 0.5;
        Start = anchor - x * newSpan;
        End = Start + newSpan;
        Clamp();
    }

    void WaveformView::Pan(double delta)
    {
        if (!std::isfinite(delta))
            return;
        Start += delta;
        End += delta;
        Clamp();
    }

    void WaveformView::CenterOn(double center)
    {
        if (!std::isfinite(center))
            return;
        const double span = GetSpan();
        Start = center - span * 0.5;
        End = Start + span;
        Clamp();
    }

    uint32_t WaveformView::GetBucketCount(uint64_t frames)
    {
        return (uint32_t)std::clamp<uint64_t>(frames / 32, 512, 65536);
    }

    glm::vec2 WaveformView::GetPeak(const std::vector<glm::vec2>& peaks, double from, double to)
    {
        if (peaks.empty() || !std::isfinite(from) || !std::isfinite(to))
            return glm::vec2(0.0f);
        const double last = (double)peaks.size() - 1.0;
        size_t first = (size_t)std::clamp(std::floor(from), 0.0, last);
        size_t end = (size_t)std::clamp(std::ceil(to), (double)first + 1.0, (double)peaks.size());
        glm::vec2 peak(peaks[first]);
        for (size_t i = first + 1; i < end; i++)
        {
            peak.x = std::min(peak.x, peaks[i].x);
            peak.y = std::max(peak.y, peaks[i].y);
        }
        return peak;
    }

    double WaveformView::GetTickStep(double seconds, double pixels, double minPixels)
    {
        if (!(seconds > 0.0) || !(pixels > 0.0) || !std::isfinite(seconds) || !std::isfinite(pixels))
            return 1.0;
        const double minStep = seconds * std::max(1.0, minPixels) / pixels;
        double power = std::pow(10.0, std::floor(std::log10(minStep)));
        for (double m : { 1.0, 2.0, 5.0, 10.0 })
            if (power * m >= minStep - 1e-12)
                return power * m;
        return power * 10.0;
    }

}
