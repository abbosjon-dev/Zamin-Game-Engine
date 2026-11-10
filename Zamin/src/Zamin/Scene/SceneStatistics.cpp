#include "Zamin/Editor/SceneStatistics.h"

#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetInfo.h"
#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Project/AssetFiles.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/ComponentRegistry.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Zamin {

    // ------------------------------------------------------------------ estimates
    uint32_t GetTextureBytesPerPixel(uint32_t channels, bool hdr)
    {
        if (hdr)
            return 16;
        switch (channels)
        {
            case 1: return 1;
            case 3: return 3;
            default: return 4;
        }
    }

    uint64_t EstimateMeshMemory(uint64_t vertices, uint64_t skinnedVertices, uint64_t indices)
    {
        skinnedVertices = std::min(skinnedVertices, vertices);
        return (vertices - skinnedVertices) * 32ull + skinnedVertices * 64ull + indices * 4ull;
    }

    uint64_t EstimateAudioMemory(uint64_t frames, uint32_t channels, bool streamed)
    {
        return streamed ? 0 : frames * (uint64_t)channels * 4ull;
    }

    // ------------------------------------------------------------------ lookups
    static const SceneStatsGroup* FindGroup(const std::vector<SceneStatsGroup>& groups, std::string_view key)
    {
        for (const SceneStatsGroup& g : groups)
            if (g.Key == key)
                return &g;
        return nullptr;
    }

    const SceneStatsGroup* SceneStatistics::FindComponent(std::string_view key) const { return FindGroup(Components, key); }
    const SceneStatsGroup* SceneStatistics::FindScript(std::string_view className) const { return FindGroup(Scripts, className); }
    const SceneStatsGroup* SceneStatistics::FindPrefab(std::string_view path) const { return FindGroup(Prefabs, path); }

    const SceneAssetStats* SceneStatistics::FindAsset(std::string_view path) const
    {
        for (const SceneAssetStats& a : Assets)
            if (a.Path == path)
                return &a;
        return nullptr;
    }

    size_t SceneStatistics::CountAssets(AssetType type) const
    {
        return (size_t)std::count_if(Assets.begin(), Assets.end(), [&](const SceneAssetStats& a) { return a.Type == type; });
    }

    // ------------------------------------------------------------------ files
    static std::string NormalizeAssetPath(const std::string& path)
    {
        return fs::path(path).lexically_normal().generic_string();
    }

    // The asset paths a material file names (its texture maps).
    static std::vector<std::string> ReadMaterialReferences(const fs::path& file)
    {
        std::vector<std::string> out;
        json root = json::parse(FileSystem::ReadTextFile(file), nullptr, false);
        if (root.is_discarded())
            return out;
        std::function<void(const json&, std::string_view)> walk = [&](const json& node, std::string_view key) {
            if (node.is_object())
            {
                for (auto it = node.begin(); it != node.end(); ++it)
                {
                    if (!it->is_string())
                        walk(it.value(), it.key());
                    else if (!(it.key().size() > 4 && it.key().ends_with("UUID")) &&
                             AssetFiles::IsAssetPathValue(it.key(), it->get_ref<const std::string&>()))
                        out.push_back(NormalizeAssetPath(it->get<std::string>()));
                }
            }
            else if (node.is_array())
            {
                for (const json& item : node)
                {
                    if (item.is_string())
                    {
                        if (AssetFiles::IsAssetPathValue(key, item.get_ref<const std::string&>()))
                            out.push_back(NormalizeAssetPath(item.get<std::string>()));
                    }
                    else
                        walk(item, key);
                }
            }
        };
        walk(root, "");
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    const SceneAssetStats& SceneStatisticsCollector::ReadFile(const fs::path& file, const std::string& path, AssetType type)
    {
        std::error_code ec;
        const std::string key = file.lexically_normal().generic_string();
        auto time = fs::last_write_time(file, ec);
        bool exists = !ec;
        uint64_t size = exists ? (uint64_t)fs::file_size(file, ec) : 0;
        if (ec)
            size = 0;
        // The import settings (mipmaps, streaming) are in the .zmeta: a change there counts too.
        std::error_code metaEc;
        auto metaTime = fs::last_write_time(AssetMetaFile::GetMetaPath(file), metaEc);
        if (!metaEc && metaTime > time)
            time = metaTime;

        auto it = m_Files.find(key);
        if (it != m_Files.end() && it->second.Time == time && it->second.Size == size && it->second.Stats.Exists == exists)
            return it->second.Stats;

        FileInfo info;
        info.Time = time;
        info.Size = size;
        SceneAssetStats& s = info.Stats;
        s.Path = path;
        s.Type = type;
        s.Exists = exists;
        s.FileSize = size;
        if (exists)
        {
            switch (type)
            {
                case AssetType::Texture:
                case AssetType::Environment:
                {
                    ImageFileInfo image = AssetInfo::ReadImage(file);
                    if (image.Valid)
                    {
                        s.Width = image.Width;
                        s.Height = image.Height;
                        s.Channels = image.Channels;
                        s.HDR = image.IsHDR || type == AssetType::Environment;
                        // Environment panoramas are drawn without mips; textures as imported.
                        s.Mipmaps = type == AssetType::Texture && AssetImporter::GetTextureSettings(file).Mipmaps;
                        s.Memory = AssetInfo::EstimateTextureMemory(s.Width, s.Height, GetTextureBytesPerPixel(s.Channels, s.HDR), s.Mipmaps);
                    }
                    break;
                }
                case AssetType::Model:
                {
                    ModelFileInfo model = AssetInfo::ReadModel(file);
                    if (model.Valid)
                    {
                        uint64_t skinned = 0;
                        for (const ModelFileInfo::Primitive& p : model.Primitives)
                        {
                            if (p.Triangles == 0)
                                continue; // lines / points are not drawn
                            s.Vertices += p.Vertices;
                            s.Indices += (uint64_t)p.Triangles * 3ull;
                            skinned += p.Skinned ? p.Vertices : 0;
                            s.Submeshes++;
                        }
                        s.Memory = EstimateMeshMemory(s.Vertices, skinned, s.Indices);
                    }
                    break;
                }
                case AssetType::Audio:
                {
                    AudioFileInfo audio = AssetInfo::ReadAudio(file);
                    if (audio.Valid)
                    {
                        s.Duration = audio.Duration;
                        s.SampleRate = audio.SampleRate;
                        s.AudioChannels = audio.Channels;
                        s.Streamed = AudioEngine::ShouldStream(file, false);
                        s.Memory = EstimateAudioMemory(audio.Frames, audio.Channels, s.Streamed);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        auto& stored = m_Files[key];
        stored = std::move(info);
        return stored.Stats;
    }

    // ------------------------------------------------------------------ counting
    SceneStatistics SceneStatisticsCollector::Collect(Scene& scene, const fs::path& assetDirectory)
    {
        SceneStatistics stats;
        const fs::path assets = assetDirectory.empty() ? (Project::GetActive() ? Project::GetAssetDirectory() : fs::path()) : assetDirectory;
        std::vector<Entity> entities = scene.GetEntitiesInHierarchyOrder();
        stats.Entities = entities.size();

        const auto& types = ComponentRegistry::GetTypes();
        std::vector<std::vector<UUID>> byType(types.size());
        std::map<std::string, std::vector<UUID>> scripts, prefabs;
        std::unordered_map<entt::entity, size_t> depth;
        depth.reserve(entities.size());
        for (Entity e : entities)
        {
            const UUID id = e.GetUUID();
            Entity parent = scene.GetParent(e);
            size_t d = 0;
            if (!parent)
                stats.Roots++;
            else if (auto it = depth.find((entt::entity)parent); it != depth.end())
                d = it->second + 1;
            depth[(entt::entity)e] = d;
            stats.MaxDepth = std::max(stats.MaxDepth, d);

            if (scene.IsActiveInHierarchy(e))
                stats.Active++;
            else
                stats.Inactive++;
            if (!scene.IsActive(e))
                stats.InactiveSelf++;
            if (scene.IsHiddenInEditor(e, false))
                stats.HiddenInEditor++;
            if (scene.IsLockedInEditor(e, false))
                stats.LockedInEditor++;
            if (std::string prefab = scene.GetPrefabSource(e); !prefab.empty())
            {
                stats.PrefabInstances++;
                prefabs[NormalizeAssetPath(prefab)].push_back(id);
            }
            for (size_t t = 0; t < types.size(); t++)
                if (types[t].Has(e))
                {
                    byType[t].push_back(id);
                    stats.ComponentTotal++;
                }
            if (const auto* script = e.TryGetComponent<NativeScriptComponent>())
            {
                stats.ScriptComponents++;
                scripts[script->ClassName].push_back(id);
            }
        }

        for (size_t t = 0; t < types.size(); t++)
            if (!byType[t].empty())
                stats.Components.push_back({ std::string(types[t].Key), std::move(byType[t]) });
        for (auto& [name, ids] : scripts)
            stats.Scripts.push_back({ name, std::move(ids) });
        for (auto& [path, ids] : prefabs)
            stats.Prefabs.push_back({ path, std::move(ids) });
        SortSceneStatsGroups(stats.Components, SceneStatsColumn::Count, false);
        SortSceneStatsGroups(stats.Scripts, SceneStatsColumn::Count, false);
        SortSceneStatsGroups(stats.Prefabs, SceneStatsColumn::Count, false);

        // ---- assets: what the components name, the scene's sky, and the textures of the materials named
        struct Use
        {
            std::vector<UUID> Entities;
            std::unordered_set<uint64_t> Seen;
            uint32_t Uses = 0;
            std::set<std::string> Components, Via;
        };
        std::map<std::string, Use> uses;
        auto addUse = [&](const std::string& path, UUID entity, bool hasEntity, std::string_view component, const std::string& via) {
            Use& u = uses[path];
            u.Uses++;
            u.Components.insert(std::string(component));
            if (!via.empty())
                u.Via.insert(via);
            if (hasEntity && u.Seen.insert((uint64_t)entity).second)
                u.Entities.push_back(entity);
        };
        SceneSerializer::CollectAssetPaths(entities, [&](size_t index, std::string_view component, const std::string& path) {
            addUse(NormalizeAssetPath(path), entities[index].GetUUID(), true, component, {});
        });
        if (const std::string& sky = scene.GetSettings().EnvironmentMap; !sky.empty())
            addUse(NormalizeAssetPath(sky), 0, false, "Settings", {});

        auto resolve = [&](const std::string& path) { return fs::path(path).is_absolute() ? fs::path(path) : assets / path; };
        // Materials bring their texture maps along (the same users).
        std::vector<std::pair<std::string, Use>> materials;
        for (const auto& [path, use] : uses)
            if (AssetTypes::FromPath(path) == AssetType::Material)
                materials.emplace_back(path, use);
        for (const auto& [material, use] : materials)
            for (const std::string& texture : ReadMaterialReferences(resolve(material)))
            {
                Use& u = uses[texture];
                u.Uses += use.Uses;
                u.Components.insert(use.Components.begin(), use.Components.end());
                u.Via.insert(material);
                for (UUID id : use.Entities)
                    if (u.Seen.insert((uint64_t)id).second)
                        u.Entities.push_back(id);
            }

        for (auto& [path, use] : uses)
        {
            const AssetType type = AssetTypes::FromPath(path);
            SceneAssetStats a = ReadFile(resolve(path), path, type);
            a.Entities = std::move(use.Entities);
            a.Uses = use.Uses;
            a.Components.assign(use.Components.begin(), use.Components.end());
            a.ViaMaterials.assign(use.Via.begin(), use.Via.end());
            switch (type)
            {
                case AssetType::Texture:
                case AssetType::Environment:
                    stats.TextureMemory += a.Memory;
                    break;
                case AssetType::Model:
                    stats.MeshMemory += a.Memory;
                    stats.MeshVertices += a.Vertices;
                    stats.MeshIndices += a.Indices;
                    break;
                case AssetType::Audio:
                    stats.AudioMemory += a.Memory;
                    break;
                default:
                    break;
            }
            stats.Assets.push_back(std::move(a));
        }
        return stats;
    }

    // ------------------------------------------------------------------ sorting
    static int CompareNames(const std::string& a, const std::string& b)
    {
        return a < b ? -1 : (b < a ? 1 : 0);
    }

    void SortSceneStatsGroups(std::vector<SceneStatsGroup>& groups, SceneStatsColumn column, bool ascending)
    {
        std::stable_sort(groups.begin(), groups.end(), [&](const SceneStatsGroup& a, const SceneStatsGroup& b) {
            int order = 0;
            if (column == SceneStatsColumn::Count && a.Count() != b.Count())
                order = a.Count() < b.Count() ? -1 : 1;
            else if (column != SceneStatsColumn::Name)
                return CompareNames(a.Key, b.Key) < 0; // ties: by name, ascending
            else
                order = CompareNames(a.Key, b.Key);
            return ascending ? order < 0 : order > 0;
        });
    }

    void SortSceneAssetStats(std::vector<SceneAssetStats>& assets, SceneStatsColumn column, bool ascending)
    {
        auto value = [&](const SceneAssetStats& a) -> uint64_t {
            switch (column)
            {
                case SceneStatsColumn::Count: return a.Entities.size();
                case SceneStatsColumn::Type: return (uint64_t)a.Type;
                case SceneStatsColumn::Size: return a.FileSize;
                case SceneStatsColumn::Memory: return a.Memory;
                default: return 0;
            }
        };
        std::stable_sort(assets.begin(), assets.end(), [&](const SceneAssetStats& a, const SceneAssetStats& b) {
            int order = 0;
            if (column != SceneStatsColumn::Name)
            {
                uint64_t va = value(a), vb = value(b);
                if (va == vb)
                    return CompareNames(a.Path, b.Path) < 0;
                order = va < vb ? -1 : 1;
            }
            else
                order = CompareNames(a.Path, b.Path);
            return ascending ? order < 0 : order > 0;
        });
    }

}
