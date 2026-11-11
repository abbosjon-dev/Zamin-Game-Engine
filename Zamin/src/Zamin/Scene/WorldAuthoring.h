#pragma once
#include "Zamin/Scene/WorldManifest.h"
namespace Zamin {
    struct WorldGridSettings
    {
        std::string Folder = "NewWorld", Name = "New world";
        std::filesystem::path TemplateScene; // optional cell-local scene inside Assets; UUIDs remapped per clone
        uint32_t Columns = 4, Rows = 4, TerrainResolution = 17, Seed = 42;
        glm::vec2 CellSize{ 32, 32 };
        bool ThreeD = true, Terrain = true, Navigation = true;
        float NavigationCellSize = 2;
        bool Validate(std::string& error) const;
    };
    struct WorldGridResult
    {
        std::filesystem::path World, Anchor;
        size_t Cells = 0, Files = 0, Bytes = 0;
    };
    // Explicit authoring operation, CPU only. Publishes a complete new bundle with no overwrite.
    // Existing project configuration is untouched; the caller decides whether to link the result.
    bool CreateWorldGrid(const std::filesystem::path& assets, const WorldGridSettings& settings,
                         WorldGridResult& output, std::string& error);
}
