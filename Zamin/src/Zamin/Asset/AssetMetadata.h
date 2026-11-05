#pragma once

#include "Zamin/Asset/AssetTypes.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace Zamin {

    // Import settings of one asset (the "Import" object of its .zmeta file): key -> value pairs such
    // as "Filter": "Nearest" or "Mipmaps": false. Values of other shapes (objects, arrays, null)
    // are kept as JSON text, so a file written by a newer editor loses nothing when saved again.
    class AssetImportSettings
    {
    public:
        struct Json
        {
            std::string Text;
            bool operator==(const Json&) const = default;
        };
        using Value = std::variant<bool, double, std::string, Json>;

        bool IsEmpty() const { return m_Values.empty(); }
        size_t GetCount() const { return m_Values.size(); }
        bool Has(std::string_view key) const { return m_Values.find(key) != m_Values.end(); }
        const Value* Find(std::string_view key) const;

        // The value, or `fallback` when it is missing or of another kind.
        bool GetBool(std::string_view key, bool fallback) const;
        double GetNumber(std::string_view key, double fallback) const;
        float GetFloat(std::string_view key, float fallback) const { return (float)GetNumber(key, fallback); }
        std::string GetString(std::string_view key, std::string_view fallback = {}) const;

        void Set(std::string_view key, Value value);
        void SetBool(std::string_view key, bool value) { Set(key, Value(value)); }
        void SetNumber(std::string_view key, double value) { Set(key, Value(value)); }
        void SetString(std::string_view key, std::string value) { Set(key, Value(std::move(value))); }
        bool Remove(std::string_view key);
        void Clear() { m_Values.clear(); }

        const std::map<std::string, Value, std::less<>>& GetValues() const { return m_Values; }
        bool operator==(const AssetImportSettings&) const = default;

    private:
        std::map<std::string, Value, std::less<>> m_Values;
    };

    // What the registry knows about one asset file.
    struct AssetMetadata
    {
        AssetHandle Handle;
        AssetType Type = AssetType::Other;
        std::string RelativePath;   // relative to the asset directory, '/' separators ("Textures/coin.png")
        AssetImportSettings Import;
        // The handle is stored in the asset's .zmeta file (false: it was made up for this session
        // only - a player that found no .zmeta does not write one).
        bool HasMetaFile = false;
    };

    // The sidecar file every asset has: "<file>.zmeta" next to it, e.g. "coin.png.zmeta":
    //   { "UUID": "3f2a9c...", "Type": "Texture", "Import": { "Filter": "Nearest" } }
    class AssetMetaFile
    {
    public:
        static constexpr std::string_view Extension = ".zmeta";

        static bool IsMetaFile(const std::filesystem::path& path);
        static std::filesystem::path GetMetaPath(const std::filesystem::path& assetPath);   // + ".zmeta"
        static std::filesystem::path GetAssetPath(const std::filesystem::path& metaPath);   // - ".zmeta"

        // JSON text of the handle, type and import settings (the path is where the file is).
        static std::string Serialize(const AssetMetadata& metadata);
        // Reads Handle, Type (when known) and Import from `text`; RelativePath is left alone.
        // False, with the reason in `error`, for broken JSON or a missing / invalid UUID.
        static bool Deserialize(std::string_view text, AssetMetadata& out, std::string* error = nullptr);

        // The .zmeta of `assetPath`. Nothing when it is missing (`error` empty) or broken (`error`
        // says why).
        static std::optional<AssetMetadata> Read(const std::filesystem::path& assetPath, std::string* error = nullptr);
        // Writes the .zmeta of `assetPath`; a file that already holds the same text is not touched.
        static bool Write(const std::filesystem::path& assetPath, const AssetMetadata& metadata);
    };

}
