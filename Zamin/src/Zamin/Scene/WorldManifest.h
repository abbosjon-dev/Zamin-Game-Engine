#pragma once

#include "Zamin/Core/UUID.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Zamin {
    // Metadata only: reading a manifest creates no entities, physics bodies or GPU resources.
    struct WorldCell
    {
        UUID ID{ 0 };
        glm::ivec3 Coordinate{ 0 };
        std::string Layer = "main";
        std::string ScenePath;
        uint64_t DeclaredCPUBytes = 0, DeclaredGPUBytes = 0;
        std::vector<UUID> Dependencies;
        bool operator==(const WorldCell&) const = default;
    };

    // XY coordinates support 2D; XZ / XYZ support 3D. Overlapping coordinates are allowed
    // in different layers. Cell UUIDs are distinct from scene/entity UUIDs.
    class WorldManifest
    {
    public:
        static constexpr uint32_t Version = 1;
        static constexpr size_t MaxCells = 4096, MaxDependencies = 64, MaxFileBytes = 8 * 1024 * 1024;
        static constexpr uint64_t MaxDeclaredBytesPerCell = uint64_t(1) << 40;
        std::string Name = "World";
        glm::dvec3 Origin{ 0 }, CellSize{ 128 };
        std::vector<WorldCell> Cells;

        bool Validate(std::string& error) const;
        // Dependencies precede dependants, with stable input order among ready cells.
        // The output is unchanged on failure.
        bool DependencyOrder(std::vector<UUID>& output, std::string& error) const;
        bool Serialize(std::string& output, std::string& error) const;
        bool Deserialize(std::string_view text, std::string& error);
        bool Load(const std::filesystem::path& file, std::string& error);
        bool Save(const std::filesystem::path& file, std::string& error) const;
        // Metadata bounds use double precision; rendering/floating-origin conversion is separate.
        std::pair<glm::dvec3, glm::dvec3> Bounds(const WorldCell& cell) const;
        // Estimates only, not measured residency. Saturates even if callers skip Validate().
        std::pair<uint64_t, uint64_t> DeclaredMemory() const;
        // Checks lexical traversal and on-disk symlink escapes. Missing files may resolve;
        // the future cell loader must report IO errors without activating a partial scene.
        static std::filesystem::path ResolveScene(const std::filesystem::path& assets, const WorldCell& cell);
    };
}
