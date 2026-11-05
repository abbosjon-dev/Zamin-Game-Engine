#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    // ------------------------------------------------------------------ data
    std::vector<std::string> MaterialData::GetTexturePaths() const
    {
        std::vector<std::string> paths;
        for (const std::string* p : { &AlbedoMap, &MetalRoughMap, &NormalMap, &EmissiveMap })
            if (!p->empty())
                paths.push_back(*p);
        return paths;
    }

    void MaterialData::ApplyValuesTo(Material& m) const
    {
        m.Shader = Shader;
        m.BlendMode = BlendMode;
        m.AlphaCutoff = AlphaCutoff;
        m.DoubleSided = DoubleSided;
        m.PreserveSpecular = PreserveSpecular;
        m.Albedo = Albedo;
        m.Metallic = Metallic;
        m.Roughness = Roughness;
        m.NormalStrength = NormalStrength;
        m.Emission = Emission;
        m.EmissionStrength = EmissionStrength;
        m.Tiling = Tiling;
        m.Offset = Offset;
    }

    MaterialData MaterialData::FromMaterial(const Material& m)
    {
        MaterialData d;
        d.Shader = m.Shader;
        d.BlendMode = m.BlendMode;
        d.AlphaCutoff = m.AlphaCutoff;
        d.DoubleSided = m.DoubleSided;
        d.PreserveSpecular = m.PreserveSpecular;
        d.Albedo = m.Albedo;
        d.Metallic = m.Metallic;
        d.Roughness = m.Roughness;
        d.NormalStrength = m.NormalStrength;
        d.Emission = m.Emission;
        d.EmissionStrength = m.EmissionStrength;
        d.Tiling = m.Tiling;
        d.Offset = m.Offset;
        return d;
    }

    // ------------------------------------------------------------------ serializer
    const char* MaterialSerializer::ShaderToString(MaterialShader shader)
    {
        return shader == MaterialShader::Unlit ? "Unlit" : "Lit";
    }

    std::optional<MaterialShader> MaterialSerializer::ShaderFromString(std::string_view text)
    {
        if (text == "Lit") return MaterialShader::Lit;
        if (text == "Unlit") return MaterialShader::Unlit;
        return std::nullopt;
    }

    const char* MaterialSerializer::BlendModeToString(MaterialBlendMode mode)
    {
        switch (mode)
        {
            case MaterialBlendMode::Opaque:      return "Opaque";
            case MaterialBlendMode::Cutout:      return "Cutout";
            case MaterialBlendMode::Transparent: return "Transparent";
        }
        return "Opaque";
    }

    std::optional<MaterialBlendMode> MaterialSerializer::BlendModeFromString(std::string_view text)
    {
        if (text == "Opaque") return MaterialBlendMode::Opaque;
        if (text == "Cutout") return MaterialBlendMode::Cutout;
        if (text == "Transparent") return MaterialBlendMode::Transparent;
        return std::nullopt;
    }

    static json ToJson(const glm::vec2& v) { return json::array({ v.x, v.y }); }
    static json ToJson(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }
    static json ToJson(const glm::vec4& v) { return json::array({ v.x, v.y, v.z, v.w }); }

    template<typename V>
    static V ReadVec(const json& j, const char* key, const V& fallback)
    {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->size() < (size_t)V::length())
            return fallback;
        V v;
        for (int i = 0; i < V::length(); i++)
            v[i] = (*it)[i].is_number() ? (*it)[i].get<float>() : fallback[i];
        return v;
    }

    static float ReadFloat(const json& j, const char* key, float fallback)
    {
        auto it = j.find(key);
        return it != j.end() && it->is_number() ? it->get<float>() : fallback;
    }

    static AssetRegistry* ActiveRegistry() { return Project::GetActive() ? Project::GetAssetRegistry() : nullptr; }

    // "<key>UUID" next to a texture path, when the registry knows the file (like scenes, task 070).
    static void WriteTexture(json& j, const char* key, const std::string& path)
    {
        j[key] = path;
        AssetRegistry* registry = ActiveRegistry();
        if (path.empty() || !registry)
            return;
        if (const AssetMetadata* meta = registry->Find(fs::path(path)); meta && meta->HasMetaFile)
            j[std::string(key) + "UUID"] = meta->Handle.ToString();
    }

    // The texture path stored under `key`, or where its UUID is now when that file is gone.
    static std::string ReadTexture(const json& j, const char* key)
    {
        auto it = j.find(key);
        std::string path = it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
        auto handleIt = j.find(std::string(key) + "UUID");
        if (path.empty() || handleIt == j.end() || !handleIt->is_string())
            return path;
        AssetRegistry* registry = ActiveRegistry();
        if (!registry || registry->Find(fs::path(path)))
            return path;
        std::error_code ec;
        if (fs::exists(registry->ToFileSystemPath(path), ec))
            return path;
        std::optional<AssetHandle> handle = AssetHandle::FromString(handleIt->get<std::string>());
        std::string moved = handle ? registry->GetPath(*handle) : std::string();
        if (moved.empty() || moved == path)
            return path;
        ZM_CORE_INFO("Material teksturasi UUID orqali topildi: '{}' -> '{}'", path, moved);
        return moved;
    }

    std::string MaterialSerializer::SerializeToString(const MaterialData& d)
    {
        json j = json::object();
        j["Version"] = MaterialData::CurrentVersion;
        j["Shader"] = ShaderToString(d.Shader);
        j["BlendMode"] = BlendModeToString(d.BlendMode);
        j["AlphaCutoff"] = d.AlphaCutoff;
        j["DoubleSided"] = d.DoubleSided;
        j["PreserveSpecular"] = d.PreserveSpecular;
        j["Albedo"] = ToJson(d.Albedo);
        WriteTexture(j, "AlbedoMap", d.AlbedoMap);
        j["Metallic"] = d.Metallic;
        j["Roughness"] = d.Roughness;
        WriteTexture(j, "MetalRoughMap", d.MetalRoughMap);
        WriteTexture(j, "NormalMap", d.NormalMap);
        j["NormalStrength"] = d.NormalStrength;
        j["Emission"] = ToJson(d.Emission);
        j["EmissionStrength"] = d.EmissionStrength;
        WriteTexture(j, "EmissiveMap", d.EmissiveMap);
        j["Tiling"] = ToJson(d.Tiling);
        j["Offset"] = ToJson(d.Offset);
        return j.dump(2) + "\n";
    }

    bool MaterialSerializer::DeserializeFromString(const std::string& text, MaterialData& out, std::string* error)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            if (error)
                *error = j.is_discarded() ? "JSON xatosi (fayl buzilgan)" : "material fayli JSON obyekt emas";
            return false;
        }
        MaterialData d;
        int version = j.value("Version", 1);
        if (version > MaterialData::CurrentVersion)
            ZM_CORE_WARN("Material fayli yangiroq versiyada ({} > {}): tanilgan maydonlar o'qiladi", version, MaterialData::CurrentVersion);
        if (auto it = j.find("Shader"); it != j.end() && it->is_string())
        {
            if (auto shader = ShaderFromString(it->get<std::string>()))
                d.Shader = *shader;
            else
                ZM_CORE_WARN("Noma'lum material shaderi '{}' - Lit ishlatiladi", it->get<std::string>());
        }
        if (auto it = j.find("BlendMode"); it != j.end() && it->is_string())
        {
            if (auto mode = BlendModeFromString(it->get<std::string>()))
                d.BlendMode = *mode;
            else
                ZM_CORE_WARN("Noma'lum aralashtirish rejimi '{}' - Opaque ishlatiladi", it->get<std::string>());
        }
        d.AlphaCutoff = std::clamp(ReadFloat(j, "AlphaCutoff", d.AlphaCutoff), 0.0f, 1.0f);
        if (auto it = j.find("DoubleSided"); it != j.end() && it->is_boolean())
            d.DoubleSided = it->get<bool>();
        if (auto it = j.find("PreserveSpecular"); it != j.end() && it->is_boolean())
            d.PreserveSpecular = it->get<bool>();
        d.Albedo = ReadVec(j, "Albedo", d.Albedo);
        d.AlbedoMap = ReadTexture(j, "AlbedoMap");
        d.Metallic = ReadFloat(j, "Metallic", d.Metallic);
        d.Roughness = ReadFloat(j, "Roughness", d.Roughness);
        d.MetalRoughMap = ReadTexture(j, "MetalRoughMap");
        d.NormalMap = ReadTexture(j, "NormalMap");
        d.NormalStrength = ReadFloat(j, "NormalStrength", d.NormalStrength);
        d.Emission = ReadVec(j, "Emission", d.Emission);
        d.EmissionStrength = ReadFloat(j, "EmissionStrength", d.EmissionStrength);
        d.EmissiveMap = ReadTexture(j, "EmissiveMap");
        // Tiling was a single number (the same on both axes) before it became a vec2.
        if (auto it = j.find("Tiling"); it != j.end() && it->is_number())
            d.Tiling = glm::vec2(it->get<float>());
        else
            d.Tiling = ReadVec(j, "Tiling", d.Tiling);
        d.Offset = ReadVec(j, "Offset", d.Offset);
        out = d;
        return true;
    }

    bool MaterialSerializer::Serialize(const MaterialData& data, const fs::path& path)
    {
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        return FileSystem::WriteTextFile(path, SerializeToString(data));
    }

    bool MaterialSerializer::Deserialize(const fs::path& path, MaterialData& out, std::string* error)
    {
        std::string unreadable = AssetImportLog::CheckReadable(path);
        if (!unreadable.empty())
        {
            if (error)
                *error = unreadable;
            return false;
        }
        return DeserializeFromString(FileSystem::ReadTextFile(path), out, error);
    }

    // ------------------------------------------------------------------ asset
    static std::atomic<uint64_t> s_MaterialRevision{ 0 };

    MaterialAsset::MaterialAsset(const MaterialData& data, fs::path path)
        : m_Path(std::move(path))
    {
        m_Data = data;
        m_Data.ApplyValuesTo(m_Material);
        ResolveTextures();
        Touch();
    }

    Ref<MaterialAsset> MaterialAsset::CreateInstance(const MaterialData& data, const Material& material, fs::path path)
    {
        Ref<MaterialAsset> instance = CreateRef<MaterialAsset>();
        instance->m_Data = data;
        instance->m_Material = material; // its textures as they are
        instance->m_Data.ApplyValuesTo(instance->m_Material);
        instance->m_Path = std::move(path);
        instance->m_Instance = true;
        instance->Touch();
        return instance;
    }

    void MaterialAsset::Touch()
    {
        m_Revision = ++s_MaterialRevision;
    }

    // ---- quick edits
    void MaterialAsset::SetColor(const glm::vec4& color)
    {
        MaterialData d = m_Data;
        d.Albedo = color;
        SetData(d);
    }

    void MaterialAsset::SetColor(const glm::vec3& color)
    {
        SetColor(glm::vec4(color, m_Data.Albedo.a));
    }

    void MaterialAsset::SetOpacity(float alpha)
    {
        SetColor(glm::vec4(glm::vec3(m_Data.Albedo), std::clamp(alpha, 0.0f, 1.0f)));
    }

    void MaterialAsset::SetEmission(const glm::vec3& color, float strength)
    {
        MaterialData d = m_Data;
        d.Emission = color;
        d.EmissionStrength = std::max(strength, 0.0f);
        SetData(d);
    }

    void MaterialAsset::SetMetallic(float metallic)
    {
        MaterialData d = m_Data;
        d.Metallic = std::clamp(metallic, 0.0f, 1.0f);
        SetData(d);
    }

    void MaterialAsset::SetRoughness(float roughness)
    {
        MaterialData d = m_Data;
        d.Roughness = std::clamp(roughness, 0.0f, 1.0f);
        SetData(d);
    }

    void MaterialAsset::SetTiling(const glm::vec2& tiling)
    {
        MaterialData d = m_Data;
        d.Tiling = tiling;
        SetData(d);
    }

    void MaterialAsset::SetOffset(const glm::vec2& offset)
    {
        MaterialData d = m_Data;
        d.Offset = offset;
        SetData(d);
    }

    void MaterialAsset::SetAlbedoMap(const std::string& assetPath)
    {
        MaterialData d = m_Data;
        d.AlbedoMap = assetPath;
        SetData(d);
    }

    void MaterialAsset::SetEmissiveMap(const std::string& assetPath)
    {
        MaterialData d = m_Data;
        d.EmissiveMap = assetPath;
        SetData(d);
    }

    void MaterialAsset::SetData(const MaterialData& data)
    {
        if (data == m_Data)
            return;
        auto update = [](Ref<Texture2D>& texture, const std::string& oldPath, const std::string& newPath) {
            if (oldPath != newPath || (!texture && !newPath.empty()))
                texture = MaterialLibrary::LoadTexture(newPath);
        };
        update(m_Material.AlbedoMap, m_Data.AlbedoMap, data.AlbedoMap);
        update(m_Material.MetalRoughMap, m_Data.MetalRoughMap, data.MetalRoughMap);
        update(m_Material.NormalMap, m_Data.NormalMap, data.NormalMap);
        update(m_Material.EmissiveMap, m_Data.EmissiveMap, data.EmissiveMap);
        m_Data = data;
        m_Data.ApplyValuesTo(m_Material);
        Touch();
    }

    void MaterialAsset::ResolveTextures()
    {
        m_Material.AlbedoMap = MaterialLibrary::LoadTexture(m_Data.AlbedoMap);
        m_Material.MetalRoughMap = MaterialLibrary::LoadTexture(m_Data.MetalRoughMap);
        m_Material.NormalMap = MaterialLibrary::LoadTexture(m_Data.NormalMap);
        m_Material.EmissiveMap = MaterialLibrary::LoadTexture(m_Data.EmissiveMap);
    }

    bool MaterialAsset::Reload(std::string* error)
    {
        std::string reason = m_Instance ? "material instansiyasi faylga bog'lanmagan"
                           : m_Path.empty() ? "materialning fayli yo'q" : std::string();
        MaterialData data;
        if (reason.empty() && MaterialSerializer::Deserialize(m_Path, data, &reason))
        {
            SetData(data);
            ResolveTextures(); // a texture that failed before may be fine now
            AssetImportLog::Clear(m_Path);
            return true;
        }
        if (error)
            *error = reason;
        else
            AssetImportLog::Report(m_Path, reason, true);
        return false;
    }

    bool MaterialAsset::Save() const
    {
        if (m_Instance)
        {
            // A script's copy: writing it would put one entity's runtime look into the shared file.
            ZM_CORE_WARN("Material instansiyasi faylga saqlanmaydi: {}", m_Path.empty() ? std::string("(xotirada)") : m_Path.filename().string());
            return false;
        }
        return !m_Path.empty() && MaterialSerializer::Serialize(m_Data, m_Path);
    }

    bool MaterialAsset::SaveAs(const fs::path& path)
    {
        if (!MaterialSerializer::Serialize(m_Data, path))
            return false;
        m_Path = path;
        m_Instance = false;
        return true;
    }

    Ref<MaterialAsset> MaterialAsset::Clone() const
    {
        Ref<MaterialAsset> copy = CreateRef<MaterialAsset>();
        copy->m_Data = m_Data;
        copy->m_Material = m_Material; // same texture objects
        copy->m_Path = m_Path;
        copy->m_Instance = true;
        copy->Touch();
        return copy;
    }

    // ------------------------------------------------------------------ library
    static std::unordered_map<std::string, Ref<MaterialAsset>> s_MaterialCache;
    static MaterialLibrary::TextureLoader s_TextureLoader;

    std::string MaterialLibrary::MakeKey(const fs::path& path)
    {
        fs::path full = Project::GetAssetFileSystemPath(path);
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(full, ec);
        return (ec ? fs::absolute(full, ec).lexically_normal() : canonical).generic_string();
    }

    void MaterialLibrary::SetTextureLoader(TextureLoader loader)
    {
        s_TextureLoader = std::move(loader);
    }

    Ref<Texture2D> MaterialLibrary::LoadTexture(const std::string& assetPath)
    {
        if (assetPath.empty())
            return nullptr;
        if (s_TextureLoader)
            return s_TextureLoader(assetPath);
        return TextureLibrary::Load(Project::GetAssetFileSystemPath(assetPath));
    }

    Ref<MaterialAsset> MaterialLibrary::Load(const fs::path& path)
    {
        if (path.empty())
            return nullptr;
        std::string key = MakeKey(path);
        if (auto it = s_MaterialCache.find(key); it != s_MaterialCache.end())
            return it->second;
        // A file that failed and did not change since is not read again (it is asked for every
        // time a scene using it resolves its assets).
        if (!AssetImportLog::ShouldRetry(key))
            return nullptr;
        MaterialData data;
        std::string error;
        if (!MaterialSerializer::Deserialize(key, data, &error))
        {
            AssetImportLog::Report(key, error);
            return nullptr;
        }
        AssetImportLog::Clear(key);
        Ref<MaterialAsset> material = CreateRef<MaterialAsset>(data, fs::path(key));
        s_MaterialCache[key] = material;
        return material;
    }

    Ref<MaterialAsset> MaterialLibrary::Find(const fs::path& path)
    {
        auto it = s_MaterialCache.find(MakeKey(path));
        return it != s_MaterialCache.end() ? it->second : nullptr;
    }

    bool MaterialLibrary::IsFileLoaded(const fs::path& path)
    {
        return s_MaterialCache.contains(MakeKey(path)) || UsesTexture(path);
    }

    static bool UsesTextureFile(const MaterialAsset& material, const std::string& textureKey)
    {
        for (const std::string& texture : material.GetData().GetTexturePaths())
            if (MaterialLibrary::MakeKey(texture) == textureKey)
                return true;
        return false;
    }

    bool MaterialLibrary::UsesTexture(const fs::path& texture)
    {
        std::string key = MakeKey(texture);
        for (const auto& [file, material] : s_MaterialCache)
            if (UsesTextureFile(*material, key))
                return true;
        return false;
    }

    size_t MaterialLibrary::ReloadFile(const fs::path& path, bool* failed)
    {
        std::string key = MakeKey(path);
        if (auto it = s_MaterialCache.find(key); it != s_MaterialCache.end())
        {
            std::string error;
            uint64_t revision = it->second->GetRevision();
            if (it->second->Reload(&error))
                return it->second->GetRevision() != revision ? 1 : 0;
            AssetImportLog::Report(key, error, true);
            if (failed)
                *failed = true;
            return 0;
        }
        // A texture: the materials using it take it again (a file that was missing or broken
        // before; a texture that was loaded is updated in place by TextureLibrary).
        size_t updated = 0;
        for (const auto& [file, material] : s_MaterialCache)
            if (UsesTextureFile(*material, key))
            {
                material->ResolveTextures();
                updated++;
            }
        return updated;
    }

    void MaterialLibrary::OnFileMoved(const fs::path& from, const fs::path& to)
    {
        std::string fromKey = MakeKey(from), toKey = MakeKey(to);
        if (fromKey == toKey)
            return;
        std::string prefix = fromKey + "/";
        std::vector<std::pair<std::string, Ref<MaterialAsset>>> moved;
        for (auto it = s_MaterialCache.begin(); it != s_MaterialCache.end();)
        {
            if (it->first == fromKey || it->first.starts_with(prefix))
            {
                std::string newKey = toKey + it->first.substr(fromKey.size());
                moved.emplace_back(newKey, it->second);
                it = s_MaterialCache.erase(it);
            }
            else
                ++it;
        }
        for (auto& [newKey, material] : moved)
        {
            material->SetPath(fs::path(newKey));
            s_MaterialCache[newKey] = material;
        }
    }

    std::vector<Ref<MaterialAsset>> MaterialLibrary::GetLoaded()
    {
        std::vector<Ref<MaterialAsset>> list;
        list.reserve(s_MaterialCache.size());
        for (const auto& [key, material] : s_MaterialCache)
            list.push_back(material);
        std::sort(list.begin(), list.end(), [](const Ref<MaterialAsset>& a, const Ref<MaterialAsset>& b) { return a->GetPath() < b->GetPath(); });
        return list;
    }

    MaterialLibrary::Snapshot MaterialLibrary::TakeSnapshot()
    {
        Snapshot snapshot;
        for (const Ref<MaterialAsset>& material : GetLoaded())
            snapshot.Materials.push_back({ material, material->GetData(), material->GetRevision() });
        return snapshot;
    }

    std::vector<std::string> MaterialLibrary::RestoreSnapshot(const Snapshot& snapshot)
    {
        std::vector<std::string> restored;
        for (const Snapshot::Entry& entry : snapshot.Materials)
        {
            const Ref<MaterialAsset>& material = entry.Asset;
            if (!material || material->GetRevision() == entry.Revision || material->GetData() == entry.Data)
                continue;
            // Saved meanwhile, or its file changed and was read again: the file is what counts.
            MaterialData onDisk;
            if (!material->GetPath().empty() && MaterialSerializer::Deserialize(material->GetPath(), onDisk) && onDisk == material->GetData())
                continue;
            material->SetData(entry.Data);
            restored.push_back(Project::GetActive() ? Project::GetAssetRelativePath(material->GetPath()) : material->GetPath().generic_string());
        }
        // Loaded after the snapshot (a script switched to it) and changed since: as its file says.
        std::unordered_set<const MaterialAsset*> known;
        for (const Snapshot::Entry& entry : snapshot.Materials)
            known.insert(entry.Asset.get());
        for (const Ref<MaterialAsset>& material : GetLoaded())
        {
            MaterialData onDisk;
            if (known.contains(material.get()) || !MaterialSerializer::Deserialize(material->GetPath(), onDisk) || onDisk == material->GetData())
                continue;
            material->SetData(onDisk);
            restored.push_back(Project::GetActive() ? Project::GetAssetRelativePath(material->GetPath()) : material->GetPath().generic_string());
        }
        return restored;
    }

    bool MaterialLibrary::ReleaseIfUnused(const fs::path& path)
    {
        auto it = s_MaterialCache.find(MakeKey(path));
        if (it == s_MaterialCache.end() || it->second.use_count() > 1)
            return false;
        s_MaterialCache.erase(it);
        return true;
    }

    void MaterialLibrary::Clear()
    {
        s_MaterialCache.clear();
    }

}
