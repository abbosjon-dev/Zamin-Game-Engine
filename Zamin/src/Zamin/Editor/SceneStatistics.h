#pragma once

#include "Zamin/Asset/AssetTypes.h"
#include "Zamin/Core/UUID.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zamin {

    class Scene;

    // One row of the scene statistics: a component type, a script class or a prefab, with the
    // entities that have it (in hierarchy order).
    struct SceneStatsGroup
    {
        std::string Key;              // component key ("MeshRenderer"), script class, prefab path
        std::vector<UUID> Entities;
        size_t Count() const { return Entities.size(); }
    };

    // An asset the scene names (its entities' components, the scene's sky), with what its file
    // holds - read from the file itself, nothing is loaded - and the memory it takes once loaded
    // (an estimate: GPU textures with their mip chain, mesh vertex / index buffers, decoded sound).
    struct SceneAssetStats
    {
        std::string Path;             // relative to Assets, as the scene names it
        AssetType Type = AssetType::Other;
        std::vector<UUID> Entities;   // the entities naming it (each once; none for the sky)
        uint32_t Uses = 0;            // places it is named (one entity can name it more than once)
        std::vector<std::string> Components; // the components naming it ("SpriteRenderer"...; "Settings": the scene's)
        std::vector<std::string> ViaMaterials; // materials whose maps it is (their users are its users)
        bool Exists = false;
        uint64_t FileSize = 0;
        // textures
        uint32_t Width = 0, Height = 0, Channels = 0;
        bool Mipmaps = false, HDR = false;
        // models: every primitive once (as the file stores it)
        uint64_t Vertices = 0, Indices = 0;
        uint32_t Submeshes = 0;
        // sounds
        double Duration = 0.0;
        uint32_t SampleRate = 0, AudioChannels = 0;
        bool Streamed = false;        // played from disk (no decoded copy in memory)
        uint64_t Memory = 0;          // estimated bytes once loaded (0: not estimated / unknown)
    };

    struct SceneStatistics
    {
        size_t Entities = 0;
        size_t Active = 0, Inactive = 0;  // active in the hierarchy (themselves and every ancestor) / not
        size_t InactiveSelf = 0;          // switched off themselves
        size_t Roots = 0, MaxDepth = 0;   // depth: 0 for roots
        size_t HiddenInEditor = 0, LockedInEditor = 0; // their own eye / lock set
        size_t PrefabInstances = 0;       // roots of prefab instances (EditorMetadataComponent::Prefab)
        size_t ScriptComponents = 0;      // C++ script components (with a class or not)
        size_t ComponentTotal = 0;        // every component of every entity (Transform included)
        std::vector<SceneStatsGroup> Components; // by component key, most used first
        std::vector<SceneStatsGroup> Scripts;    // by class ("" = a script component without one)
        std::vector<SceneStatsGroup> Prefabs;    // instances by prefab
        std::vector<SceneAssetStats> Assets;     // by path
        uint64_t TextureMemory = 0, MeshMemory = 0, AudioMemory = 0;
        uint64_t MeshVertices = 0, MeshIndices = 0; // of the models named (each model once)

        const SceneStatsGroup* FindComponent(std::string_view key) const;
        const SceneStatsGroup* FindScript(std::string_view className) const;
        const SceneStatsGroup* FindPrefab(std::string_view path) const;
        const SceneAssetStats* FindAsset(std::string_view path) const;
        size_t CountAssets(AssetType type) const;
        uint64_t GetTotalMemory() const { return TextureMemory + MeshMemory + AudioMemory; }
    };

    // Counts a scene for the editor's "Sahna statistikasi" window. The asset files are read once
    // and kept until the file changes (its size or write time), so counting again is cheap.
    class SceneStatisticsCollector
    {
    public:
        // `assetDirectory`: what the scene's asset paths are relative to ("" = the active project's).
        SceneStatistics Collect(Scene& scene, const std::filesystem::path& assetDirectory = {});
        void ClearCache() { m_Files.clear(); }
        size_t GetCachedFileCount() const { return m_Files.size(); }

    private:
        struct FileInfo
        {
            std::filesystem::file_time_type Time{};
            uint64_t Size = 0;
            SceneAssetStats Stats;    // the file's part (no users)
        };
        // The file's part of the stats, read when it is new or changed.
        const SceneAssetStats& ReadFile(const std::filesystem::path& file, const std::string& path, AssetType type);

    private:
        std::unordered_map<std::string, FileInfo> m_Files;
    };

    // The tables' columns (sorting).
    enum class SceneStatsColumn : uint8_t { Name = 0, Count, Type, Size, Memory };

    // Stable sorts; ties by name, ascending.
    void SortSceneStatsGroups(std::vector<SceneStatsGroup>& groups, SceneStatsColumn column, bool ascending);
    void SortSceneAssetStats(std::vector<SceneAssetStats>& assets, SceneStatsColumn column, bool ascending);

    // ---- the estimates
    // GPU bytes per texel of an image of `channels` as the engine uploads it: 1 -> R8, 3 -> RGB8,
    // 2 / 4 -> RGBA8; HDR images (environment maps) RGBA32F.
    uint32_t GetTextureBytesPerPixel(uint32_t channels, bool hdr);
    // A model's buffers: 32 bytes per vertex (position, normal, uv), 64 skinned (+ joints, weights),
    // 4 per index.
    uint64_t EstimateMeshMemory(uint64_t vertices, uint64_t skinnedVertices, uint64_t indices);
    // A sound decoded into memory (32-bit float samples); a streamed one keeps no copy (0).
    uint64_t EstimateAudioMemory(uint64_t frames, uint32_t channels, bool streamed);

}
