#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cctype>

namespace Zamin {

    namespace fs = std::filesystem;
    using ordered_json = nlohmann::ordered_json;

    // ------------------------------------------------------------------ import settings
    const AssetImportSettings::Value* AssetImportSettings::Find(std::string_view key) const
    {
        auto it = m_Values.find(key);
        return it != m_Values.end() ? &it->second : nullptr;
    }

    bool AssetImportSettings::GetBool(std::string_view key, bool fallback) const
    {
        const Value* v = Find(key);
        return v && std::holds_alternative<bool>(*v) ? std::get<bool>(*v) : fallback;
    }

    double AssetImportSettings::GetNumber(std::string_view key, double fallback) const
    {
        const Value* v = Find(key);
        return v && std::holds_alternative<double>(*v) ? std::get<double>(*v) : fallback;
    }

    std::string AssetImportSettings::GetString(std::string_view key, std::string_view fallback) const
    {
        const Value* v = Find(key);
        return v && std::holds_alternative<std::string>(*v) ? std::get<std::string>(*v) : std::string(fallback);
    }

    void AssetImportSettings::Set(std::string_view key, Value value)
    {
        if (key.empty())
            return;
        auto it = m_Values.find(key);
        if (it != m_Values.end())
            it->second = std::move(value);
        else
            m_Values.emplace(std::string(key), std::move(value));
    }

    bool AssetImportSettings::Remove(std::string_view key)
    {
        auto it = m_Values.find(key);
        if (it == m_Values.end())
            return false;
        m_Values.erase(it);
        return true;
    }

    // ------------------------------------------------------------------ meta files
    bool AssetMetaFile::IsMetaFile(const fs::path& path)
    {
        std::string ext = path.extension().string();
        if (ext.size() != Extension.size())
            return false;
        for (size_t i = 0; i < ext.size(); i++)
            if (std::tolower((unsigned char)ext[i]) != Extension[i])
                return false;
        return true;
    }

    fs::path AssetMetaFile::GetMetaPath(const fs::path& assetPath)
    {
        fs::path meta = assetPath;
        meta += Extension;
        return meta;
    }

    fs::path AssetMetaFile::GetAssetPath(const fs::path& metaPath)
    {
        if (!IsMetaFile(metaPath))
            return metaPath;
        fs::path asset = metaPath;
        asset.replace_extension(); // "coin.png.zmeta" -> "coin.png"
        return asset;
    }

    std::string AssetMetaFile::Serialize(const AssetMetadata& metadata)
    {
        ordered_json j;
        j["UUID"] = metadata.Handle.ToString();
        j["Type"] = AssetTypes::GetKey(metadata.Type);
        ordered_json import = ordered_json::object();
        for (const auto& [key, value] : metadata.Import.GetValues())
        {
            if (const bool* b = std::get_if<bool>(&value))
                import[key] = *b;
            else if (const double* d = std::get_if<double>(&value))
            {
                // Whole numbers without ".0" (a scale of 2 reads "2").
                if (*d == (double)(int64_t)*d && *d > -1e15 && *d < 1e15)
                    import[key] = (int64_t)*d;
                else
                    import[key] = *d;
            }
            else if (const std::string* s = std::get_if<std::string>(&value))
                import[key] = *s;
            else if (const auto* raw = std::get_if<AssetImportSettings::Json>(&value))
            {
                ordered_json parsed = ordered_json::parse(raw->Text, nullptr, false);
                import[key] = parsed.is_discarded() ? ordered_json(nullptr) : parsed;
            }
        }
        j["Import"] = std::move(import);
        return j.dump(4) + "\n";
    }

    bool AssetMetaFile::Deserialize(std::string_view text, AssetMetadata& out, std::string* error)
    {
        auto fail = [&](const char* reason) {
            if (error)
                *error = reason;
            return false;
        };
        ordered_json j = ordered_json::parse(text.begin(), text.end(), nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return fail("JSON formati buzilgan");

        AssetHandle handle;
        auto uuid = j.find("UUID");
        if (uuid == j.end())
            return fail("UUID yo'q");
        if (uuid->is_string())
            handle = AssetHandle::FromString(uuid->get<std::string>()).value_or(AssetHandle());
        else if (uuid->is_number_unsigned())
            handle = AssetHandle(uuid->get<uint64_t>());
        if (!handle)
            return fail("UUID noto'g'ri");

        out.Handle = handle;
        if (auto type = j.find("Type"); type != j.end() && type->is_string())
            if (auto known = AssetTypes::FromKey(type->get<std::string>()))
                out.Type = *known;

        out.Import.Clear();
        if (auto import = j.find("Import"); import != j.end() && import->is_object())
        {
            for (auto it = import->begin(); it != import->end(); ++it)
            {
                const ordered_json& v = it.value();
                if (v.is_boolean())
                    out.Import.SetBool(it.key(), v.get<bool>());
                else if (v.is_number())
                    out.Import.SetNumber(it.key(), v.get<double>());
                else if (v.is_string())
                    out.Import.SetString(it.key(), v.get<std::string>());
                else
                    out.Import.Set(it.key(), AssetImportSettings::Json{ v.dump() });
            }
        }
        if (error)
            error->clear();
        return true;
    }

    std::optional<AssetMetadata> AssetMetaFile::Read(const fs::path& assetPath, std::string* error)
    {
        if (error)
            error->clear();
        fs::path metaPath = GetMetaPath(assetPath);
        std::error_code ec;
        if (!fs::is_regular_file(metaPath, ec))
            return std::nullopt;
        std::string text = FileSystem::ReadTextFile(metaPath);
        AssetMetadata metadata;
        metadata.Type = AssetTypes::FromPath(assetPath);
        if (!Deserialize(text, metadata, error))
            return std::nullopt;
        metadata.HasMetaFile = true;
        return metadata;
    }

    bool AssetMetaFile::Write(const fs::path& assetPath, const AssetMetadata& metadata)
    {
        if (!metadata.Handle)
            return false;
        fs::path metaPath = GetMetaPath(assetPath);
        std::string text = Serialize(metadata);
        std::error_code ec;
        if (fs::is_regular_file(metaPath, ec) && FileSystem::ReadTextFile(metaPath) == text)
            return true;
        return FileSystem::WriteTextFile(metaPath, text);
    }

}
