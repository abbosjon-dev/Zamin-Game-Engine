#pragma once

#include "Zamin/Asset/AssetInfo.h"

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

namespace Zamin {

    // How a sound file is read when it plays.
    enum class AudioLoadMode
    {
        Auto = 0,   // decoded into memory, streamed when larger than the project's threshold
        Decompress, // always decoded into memory first (short sounds played often)
        Stream      // always streamed from disk while it plays (long music, ambience)
    };

    // Import settings of a sound file (the "Import" object of its .zmeta; the asset inspector
    // edits them):
    //   { "LoadMode": "Auto" | "Decompress" | "Stream", "Preload": false, "Volume": 1.0,
    //     "LoopStart": 1.5, "LoopEnd": 9.0 }   (the loop keys only when a loop region is set)
    struct AudioClipSettings
    {
        AudioLoadMode LoadMode = AudioLoadMode::Auto;
        // Decoded into memory before it is needed and kept there: when a scene that uses it starts
        // (its Audio Sources) or when it first plays - the first Play does not wait for decoding and
        // a short sound played again and again is not decoded every time. Not for streamed files.
        bool Preload = false;
        // The file's own gain (balancing a file recorded too loud or too quiet), x every sound
        // played from it. 0..AudioSettings::MaxVolume.
        float Volume = 1.0f;
        // The loop region, in seconds: a sound played from the file with Loop on plays from the
        // beginning through LoopEnd, then again and again from LoopStart (an intro, then the loop).
        // LoopEnd 0 = the end of the file; 0..0 = the whole file (no region).
        float LoopStart = 0.0f;
        float LoopEnd = 0.0f;

        bool HasLoopRegion() const { return LoopStart > 0.0f || LoopEnd > 0.0f; }
        // The region in seconds for a file `duration` long: start in 0..duration, end after it
        // (an end of 0, past the file or before the start: the end of the file).
        void GetLoopRegion(float duration, float& start, float& end) const;
        // Finite, non-negative values; an end not after the start becomes 0 (the file's end).
        void Sanitize();

        bool operator==(const AudioClipSettings&) const = default;
    };

    // The asset inspector's waveform: which part of the sound is shown (zoom / pan), as fractions
    // of the sound's length (0 = its start, 1 = its end).
    struct WaveformView
    {
        double Start = 0.0, End = 1.0;
        // The narrowest view (a fraction of the sound): set from the waveform's resolution.
        double MinSpan = 1.0 / 4096.0;

        double GetSpan() const { return End - Start; }
        bool IsZoomed() const { return Start > 0.0 || End < 1.0; }
        void Reset() { Start = 0.0; End = 1.0; }
        // `factor` > 1 zooms in, < 1 out; `anchor` (a fraction of the sound) stays where it is on screen.
        void ZoomAt(double anchor, double factor);
        // Moves the view by `delta` (a fraction of the sound), kept inside the sound.
        void Pan(double delta);
        // Moves the view so `center` is in its middle (the span stays).
        void CenterOn(double center);
        // `x` across the view (0..1) -> a fraction of the sound, and back.
        double ToSound(double x) const { return Start + x * GetSpan(); }
        double ToView(double sound) const { return GetSpan() > 0.0 ? (sound - Start) / GetSpan() : 0.0; }
        // Span within MinSpan..1, the view inside 0..1.
        void Clamp();

        // How many peaks a waveform of `frames` samples is read with: enough to zoom in (a peak per
        // ~32 frames), at least 512, at most 65536.
        static uint32_t GetBucketCount(uint64_t frames);
        // The lowest and highest sample (x, y) of the peaks in [from, to) - bucket positions, may be
        // fractional; a range narrower than a bucket gives that bucket.
        static glm::vec2 GetPeak(const std::vector<glm::vec2>& peaks, double from, double to);
        // A tick step for a time ruler: 1, 2 or 5 x 10^n seconds, so ticks are at least
        // `minPixels` apart when `seconds` take `pixels`.
        static double GetTickStep(double seconds, double pixels, double minPixels);
    };

    // What a sound file holds, read once per version of the file (duration, format, channels,
    // sample rate) and kept: the asset inspector, scripts (Audio::GetClipLength) and tools ask
    // without decoding it again. Thread-unsafe: the main thread.
    class AudioClip
    {
    public:
        // The file's info (Valid = false with the reason for a file that cannot be read).
        static const AudioFileInfo& GetInfo(const std::filesystem::path& path);
        // Seconds (0 when unknown).
        static float GetLength(const std::filesystem::path& path);
        // Bytes the decoded sound takes in memory: 32-bit float samples at `sampleRate` (the
        // engine's; 0 = the file's own) with the file's channels.
        static uint64_t EstimateDecodedSize(const AudioFileInfo& info, uint32_t sampleRate = 0);
        // Forgets what was read (a test, a project switch).
        static void ClearCache();
    };

}
