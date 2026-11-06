#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Stable identity of an asset file: a random 64-bit number kept in the asset's .zmeta file, so
    // it stays the same when the file is renamed or moved. 0 = no asset.
    class AssetHandle
    {
    public:
        constexpr AssetHandle() = default;
        constexpr explicit AssetHandle(uint64_t value) : m_Value(value) {}
        // A new random handle (never 0).
        static AssetHandle Generate();

        constexpr uint64_t GetValue() const { return m_Value; }
        constexpr bool IsValid() const { return m_Value != 0; }
        constexpr explicit operator bool() const { return IsValid(); }
        constexpr bool operator==(const AssetHandle&) const = default;
        constexpr auto operator<=>(const AssetHandle&) const = default;

        // 16 lower-case hex digits ("" for an invalid handle).
        std::string ToString() const;
        // 1-16 hex digits (any case, an optional "0x"); nothing for other text or 0.
        static std::optional<AssetHandle> FromString(std::string_view text);

    private:
        uint64_t m_Value = 0;
    };

    // What a file of the project is, judged by its extension (content browser icons and filter,
    // the asset registry, where dropped files are imported to). Other = unknown.
    enum class AssetType : uint8_t
    {
        Folder = 0, Scene, Prefab, Texture, Model, Audio, Font, Environment, Material, PhysicsMaterial, Animation, SpriteAnimation, Script, Project, Other,
        Count
    };

    // Asset type detection and names.
    class AssetTypes
    {
    public:
        // ".png", "png" or "PNG" -> Texture; unknown extensions -> Other.
        static AssetType FromExtension(std::string_view extension);
        static AssetType FromPath(const std::filesystem::path& path, bool isDirectory = false);
        // Extensions of a type, lower case with the dot (".png", ".jpg"...).
        static const std::vector<std::string_view>& GetExtensions(AssetType type);

        static const char* GetName(AssetType type);   // shown in the editor ("Rasm", "Model"...)
        static const char* GetKey(AssetType type);    // stored in files ("Texture", "Model"...)
        static std::optional<AssetType> FromKey(std::string_view key);
        // Assets sub folder imported files of this type go to ("Textures", "Models"...; "" = Assets).
        static const char* GetImportFolder(AssetType type);

        // Whether files of this type are assets of the registry (a handle and a .zmeta file):
        // scenes, prefabs, textures, models, audio, fonts, HDR environments, materials and physics
        // materials. Not
        // folders, C++ files, the project file or unknown files (model buffers, notes...).
        static bool IsRegistered(AssetType type);
    };

}

template<>
struct std::hash<Zamin::AssetHandle>
{
    std::size_t operator()(const Zamin::AssetHandle& handle) const noexcept { return std::hash<uint64_t>()(handle.GetValue()); }
};
