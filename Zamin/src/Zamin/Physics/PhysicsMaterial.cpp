#include "Zamin/Physics/PhysicsMaterial.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace Zamin {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    // ------------------------------------------------------------------ data
    void PhysicsMaterialData::Sanitize()
    {
        auto clean = [](float& v, float fallback) {
            if (!std::isfinite(v))
                v = fallback;
            v = std::max(0.0f, v);
        };
        clean(Friction, 0.5f);
        clean(Restitution, 0.0f);
        clean(RestitutionThreshold, 0.5f);
        clean(Density, 1.0f);
        if ((int)FrictionCombine > (int)PhysicsCombine::Maximum) FrictionCombine = PhysicsCombine::Default;
        if ((int)RestitutionCombine > (int)PhysicsCombine::Maximum) RestitutionCombine = PhysicsCombine::Default;
    }

    float PhysicsMaterial::Combine(float a, PhysicsCombine modeA, float b, PhysicsCombine modeB, bool friction)
    {
        switch (std::max(modeA, modeB))
        {
            case PhysicsCombine::Average:  return (a + b) * 0.5f;
            case PhysicsCombine::Minimum:  return std::min(a, b);
            case PhysicsCombine::Multiply: return a * b;
            case PhysicsCombine::Maximum:  return std::max(a, b);
            case PhysicsCombine::Default:  break;
        }
        // Box2D's b2MixFriction / b2MixRestitution.
        return friction ? std::sqrt(std::max(0.0f, a * b)) : std::max(a, b);
    }

    static constexpr std::array<const char*, 5> s_CombineKeys = { "Default", "Average", "Minimum", "Multiply", "Maximum" };
    static constexpr std::array<const char*, 5> s_CombineNames = { "Standart (Box2D)", "O'rtacha", "Eng kichigi", "Ko'paytma", "Eng kattasi" };

    const char* PhysicsMaterial::GetCombineKey(PhysicsCombine mode)
    {
        return (size_t)mode < s_CombineKeys.size() ? s_CombineKeys[(size_t)mode] : s_CombineKeys[0];
    }

    const char* PhysicsMaterial::GetCombineName(PhysicsCombine mode)
    {
        return (size_t)mode < s_CombineNames.size() ? s_CombineNames[(size_t)mode] : s_CombineNames[0];
    }

    std::optional<PhysicsCombine> PhysicsMaterial::CombineFromKey(std::string_view key)
    {
        for (size_t i = 0; i < s_CombineKeys.size(); i++)
            if (key == s_CombineKeys[i])
                return (PhysicsCombine)i;
        return std::nullopt;
    }

    std::string PhysicsMaterial::ToJson(const PhysicsMaterialData& d)
    {
        json j = { { "Friction", d.Friction }, { "Restitution", d.Restitution }, { "RestitutionThreshold", d.RestitutionThreshold },
                   { "Density", d.Density }, { "FrictionCombine", GetCombineKey(d.FrictionCombine) },
                   { "RestitutionCombine", GetCombineKey(d.RestitutionCombine) } };
        return j.dump(4);
    }

    bool PhysicsMaterial::FromJson(std::string_view text, PhysicsMaterialData& out, std::string* error)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            if (error)
                *error = "JSON obyekt emas";
            return false;
        }
        PhysicsMaterialData d;
        auto number = [&](const char* key, float& v) {
            if (auto it = j.find(key); it != j.end() && it->is_number())
                v = it->get<float>();
        };
        auto combine = [&](const char* key, PhysicsCombine& v) {
            if (auto it = j.find(key); it != j.end() && it->is_string())
                v = CombineFromKey(it->get<std::string>()).value_or(PhysicsCombine::Default);
        };
        number("Friction", d.Friction);
        number("Restitution", d.Restitution);
        number("RestitutionThreshold", d.RestitutionThreshold);
        number("Density", d.Density);
        combine("FrictionCombine", d.FrictionCombine);
        combine("RestitutionCombine", d.RestitutionCombine);
        d.Sanitize();
        out = d;
        return true;
    }

    bool PhysicsMaterial::Save(const fs::path& file, const PhysicsMaterialData& data)
    {
        return FileSystem::WriteTextFile(file, ToJson(data));
    }

    bool PhysicsMaterial::Load(const fs::path& file, PhysicsMaterialData& out, std::string* error)
    {
        std::string reason = AssetImportLog::CheckReadable(file);
        if (!reason.empty())
        {
            if (error)
                *error = reason;
            return false;
        }
        return FromJson(FileSystem::ReadTextFile(file), out, error);
    }

    // ------------------------------------------------------------------ asset
    void PhysicsMaterialAsset::SetData(const PhysicsMaterialData& data)
    {
        PhysicsMaterialData clean = data;
        clean.Sanitize();
        if (clean == m_Data)
            return;
        m_Data = clean;
        m_Revision++;
        PhysicsMaterialLibrary::OnChanged();
    }

    bool PhysicsMaterialAsset::Reload(std::string* error)
    {
        PhysicsMaterialData data;
        if (!PhysicsMaterial::Load(m_Path, data, error))
            return false;
        SetData(data);
        return true;
    }

    // ------------------------------------------------------------------ library
    static std::unordered_map<std::string, Ref<PhysicsMaterialAsset>> s_Cache;
    static uint64_t s_Revision = 1;

    std::string PhysicsMaterialLibrary::MakeKey(const fs::path& path)
    {
        fs::path full = Project::GetAssetFileSystemPath(path);
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(full, ec);
        return (ec ? fs::absolute(full, ec).lexically_normal() : canonical).generic_string();
    }

    Ref<PhysicsMaterialAsset> PhysicsMaterialLibrary::Load(const fs::path& path)
    {
        if (path.empty())
            return nullptr;
        std::string key = MakeKey(path);
        if (auto it = s_Cache.find(key); it != s_Cache.end())
            return it->second;
        if (!AssetImportLog::ShouldRetry(key))
            return nullptr;
        PhysicsMaterialData data;
        std::string error;
        if (!PhysicsMaterial::Load(key, data, &error))
        {
            AssetImportLog::Report(key, error);
            return nullptr;
        }
        AssetImportLog::Clear(key);
        Ref<PhysicsMaterialAsset> asset = CreateRef<PhysicsMaterialAsset>(data, fs::path(key));
        s_Cache[key] = asset;
        return asset;
    }

    Ref<PhysicsMaterialAsset> PhysicsMaterialLibrary::Find(const fs::path& path)
    {
        auto it = s_Cache.find(MakeKey(path));
        return it != s_Cache.end() ? it->second : nullptr;
    }

    bool PhysicsMaterialLibrary::IsFileLoaded(const fs::path& path) { return Find(path) != nullptr; }

    size_t PhysicsMaterialLibrary::ReloadFile(const fs::path& path, bool* failed)
    {
        auto it = s_Cache.find(MakeKey(path));
        if (it == s_Cache.end())
            return 0;
        std::string error;
        uint64_t revision = it->second->GetRevision();
        if (!it->second->Reload(&error))
        {
            AssetImportLog::Report(it->first, error, true);
            if (failed)
                *failed = true;
            return 0;
        }
        AssetImportLog::Clear(it->first);
        return it->second->GetRevision() != revision ? 1 : 0;
    }

    void PhysicsMaterialLibrary::OnFileMoved(const fs::path& from, const fs::path& to)
    {
        std::string fromKey = MakeKey(from), toKey = MakeKey(to);
        if (fromKey == toKey)
            return;
        std::string prefix = fromKey + "/";
        std::vector<std::pair<std::string, Ref<PhysicsMaterialAsset>>> moved;
        for (auto it = s_Cache.begin(); it != s_Cache.end();)
        {
            if (it->first == fromKey || it->first.starts_with(prefix))
            {
                moved.emplace_back(toKey + it->first.substr(fromKey.size()), it->second);
                it = s_Cache.erase(it);
            }
            else
                ++it;
        }
        for (auto& [key, asset] : moved)
        {
            asset->SetPath(fs::path(key));
            s_Cache[key] = asset;
        }
    }

    uint64_t PhysicsMaterialLibrary::GetRevision() { return s_Revision; }
    void PhysicsMaterialLibrary::OnChanged() { s_Revision++; }

    void PhysicsMaterialLibrary::Clear()
    {
        s_Cache.clear();
        s_Revision++;
    }

    PhysicsMaterialData PhysicsMaterialLibrary::Resolve(const std::string& materialPath, const PhysicsMaterialData& inlineData)
    {
        if (!materialPath.empty())
            if (Ref<PhysicsMaterialAsset> asset = Load(materialPath))
                return asset->GetData();
        return inlineData;
    }

}
