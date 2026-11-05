#pragma once

#include "Zamin/Asset/AssetTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // An asset file that could not be imported.
    struct AssetImportError
    {
        std::filesystem::path File;   // absolute
        std::string Name;             // as logged: relative to the asset folder when it is inside it
        AssetType Type = AssetType::Other;
        std::string Reason;           // "fayl topilmadi", the decoder's reason...
        bool Reload = false;          // the file failed when it was imported again (the old content stayed)
        uint32_t Count = 0;           // failures since it was reported
        uint64_t Sequence = 0;        // report order (larger = newer)
    };

    // Import errors of asset files - textures, models, fonts, HDR maps, audio, scenes, prefabs.
    // A file that fails to load (missing, broken, an unsupported format) is reported to the console
    // once, with its path and the reason, however often something tries to load it (a tilemap drawn
    // every frame, a script playing a sound, every scene that uses it). It is reported again when the
    // reason changes, when the file changed on disk, or after it loaded fine once. The editor shows
    // the current errors in the content browser and the inspector. Thread-safe.
    class AssetImportLog
    {
    public:
        // A failed import. Returns true when it was written to the console now.
        static bool Report(const std::filesystem::path& file, std::string_view reason, bool reload = false);
        // The file was imported fine: its error is forgotten (a new failure is reported again).
        static void Clear(const std::filesystem::path& file);
        // The file - or a folder: everything inside it - changed on disk: its next failure is
        // reported even with the same reason, and loaders try it again (ShouldRetry).
        static void OnFileChanged(const std::filesystem::path& file);
        static void ClearAll();

        // Whether loading the file again can give anything new: false while it failed before and is
        // still the same on disk (same size and time, or still missing). Loaders that are asked for
        // a broken file every frame skip it then.
        static bool ShouldRetry(const std::filesystem::path& file);

        static std::optional<AssetImportError> Find(const std::filesystem::path& file);
        static bool HasError(const std::filesystem::path& file) { return Find(file).has_value(); }
        // Every current error, sorted by name.
        static std::vector<AssetImportError> GetErrors();
        static size_t GetCount();
        // Changes whenever an error is added, changed or removed (panels refresh what they show).
        static uint64_t GetRevision();
        // How many errors were written to the console so far.
        static uint64_t GetLoggedCount();

        // Why a file cannot be read at all ("" when it is a readable file): missing, a folder...
        static std::string CheckReadable(const std::filesystem::path& file);
        // An image decoder's (stb_image) reason in words, with the original text.
        static std::string DescribeImageError(const char* reason);
        // The key errors are kept under: absolute, normalized (symlinks resolved when possible).
        static std::string MakeKey(const std::filesystem::path& file);
    };

}
