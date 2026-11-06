#include "Zamin/Asset/AssetTypes.h"
#include "Zamin/Core/UUID.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace Zamin {

    // ------------------------------------------------------------------ handle
    AssetHandle AssetHandle::Generate()
    {
        return AssetHandle((uint64_t)UUID()); // UUID values are never 0
    }

    std::string AssetHandle::ToString() const
    {
        return m_Value ? std::format("{:016x}", m_Value) : std::string();
    }

    std::optional<AssetHandle> AssetHandle::FromString(std::string_view text)
    {
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(2);
        if (text.empty() || text.size() > 16)
            return std::nullopt;
        uint64_t value = 0;
        for (char c : text)
        {
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return std::nullopt;
            value = (value << 4) | (uint64_t)digit;
        }
        if (value == 0)
            return std::nullopt;
        return AssetHandle(value);
    }

    // ------------------------------------------------------------------ types
    struct AssetTypeInfo
    {
        AssetType Type;
        const char* Name;
        const char* Key;
        const char* ImportFolder;
        bool Registered;
    };

    static constexpr std::array<AssetTypeInfo, (size_t)AssetType::Count> s_TypeInfo = { {
        { AssetType::Folder, "Papka", "Folder", "", false },
        { AssetType::Scene, "Sahna", "Scene", "Scenes", true },
        { AssetType::Prefab, "Prefab", "Prefab", "Prefabs", true },
        { AssetType::Texture, "Rasm", "Texture", "Textures", true },
        { AssetType::Model, "Model", "Model", "Models", true },
        { AssetType::Audio, "Audio", "Audio", "Audio", true },
        { AssetType::Font, "Shrift", "Font", "Fonts", true },
        { AssetType::Environment, "HDRI muhit", "Environment", "Textures", true },
        { AssetType::Material, "Material", "Material", "Materials", true },
        { AssetType::PhysicsMaterial, "Fizik material", "PhysicsMaterial", "Physics", true },
        { AssetType::Animation, "Animatsiya klipi", "Animation", "Animations", true },
        { AssetType::SpriteAnimation, "Sprite animatsiya klipi", "SpriteAnimation", "Animations", true },
        { AssetType::ParticlePreset, "Particle preset", "ParticlePreset", "Particles", true },
        { AssetType::Script, "C++ skript", "Script", "", false },
        { AssetType::Project, "Loyiha", "Project", "", false },
        { AssetType::Other, "Boshqa", "Other", "", false },
    } };

    static const std::array<std::vector<std::string_view>, (size_t)AssetType::Count>& ExtensionTable()
    {
        static const std::array<std::vector<std::string_view>, (size_t)AssetType::Count> s_Extensions = { {
            {},                                                           // Folder
            { ".zscene" },                                                // Scene
            { ".zprefab" },                                               // Prefab
            { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd" },  // Texture (stb_image)
            { ".gltf", ".glb", ".obj" },                                  // Model
            { ".wav", ".mp3", ".ogg", ".flac" },                          // Audio (miniaudio)
            { ".ttf", ".otf" },                                           // Font
            { ".hdr" },                                                   // Environment
            { ".zmat" },                                                  // Material
            { ".zphysmat" },                                              // PhysicsMaterial
            { ".zanim" },                                                 // Animation
            { ".zsanim" },                                                // SpriteAnimation
            { ".zparticle" },                                             // ParticlePreset
            { ".cpp", ".h", ".hpp", ".cc", ".c", ".inl" },                // Script
            { ".zproject" },                                              // Project
            {},                                                           // Other
        } };
        return s_Extensions;
    }

    AssetType AssetTypes::FromExtension(std::string_view extension)
    {
        if (extension.empty())
            return AssetType::Other;
        std::string ext;
        ext.reserve(extension.size() + 1);
        if (extension.front() != '.')
            ext += '.';
        for (char c : extension)
            ext += (char)std::tolower((unsigned char)c);
        const auto& table = ExtensionTable();
        for (size_t t = 0; t < table.size(); t++)
            if (std::find(table[t].begin(), table[t].end(), ext) != table[t].end())
                return (AssetType)t;
        return AssetType::Other;
    }

    AssetType AssetTypes::FromPath(const std::filesystem::path& path, bool isDirectory)
    {
        if (isDirectory)
            return AssetType::Folder;
        return FromExtension(path.extension().string());
    }

    const std::vector<std::string_view>& AssetTypes::GetExtensions(AssetType type)
    {
        static const std::vector<std::string_view> s_None;
        return type < AssetType::Count ? ExtensionTable()[(size_t)type] : s_None;
    }

    const char* AssetTypes::GetName(AssetType type)
    {
        return type < AssetType::Count ? s_TypeInfo[(size_t)type].Name : "Boshqa";
    }

    const char* AssetTypes::GetKey(AssetType type)
    {
        return type < AssetType::Count ? s_TypeInfo[(size_t)type].Key : "Other";
    }

    std::optional<AssetType> AssetTypes::FromKey(std::string_view key)
    {
        for (const auto& info : s_TypeInfo)
            if (key == info.Key)
                return info.Type;
        return std::nullopt;
    }

    const char* AssetTypes::GetImportFolder(AssetType type)
    {
        return type < AssetType::Count ? s_TypeInfo[(size_t)type].ImportFolder : "";
    }

    bool AssetTypes::IsRegistered(AssetType type)
    {
        return type < AssetType::Count && s_TypeInfo[(size_t)type].Registered;
    }

}
