#pragma once

#include "Zamin/Asset/AssetInfo.h"

#include <filesystem>

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
    //   { "LoadMode": "Auto" | "Decompress" | "Stream", "Preload": false, "Volume": 1.0 }
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

        bool operator==(const AudioClipSettings&) const = default;
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
