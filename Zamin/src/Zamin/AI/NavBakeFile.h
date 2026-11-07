#pragma once

#include "Zamin/AI/NavGrid.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Saved navigation bakes: a ".znav" file next to a scene ("Level.zscene" -> "Level.znav") with the
// baked grids of its NavGrid 2D / 3D components that ask for it (NavGridPathSettings::CacheBake).
//
// The grids are derived data: by default they are baked again whenever the game starts a scene (a
// small grid takes milliseconds - ZaminBench --nav measures it), so they can never be out of date.
// A big 3D grid takes long to bake (a ray and an overlap per cell): with CacheBake the editor writes
// it into the .znav when the scene is saved, together with a fingerprint of everything the bake
// read (the grid's settings and position, the tilemaps / static colliders, the collision matrix).
// When the game starts the scene it takes a saved grid only when the fingerprint is still the
// same; anything else - an old, broken or missing file, a moved wall - and the grid is baked as
// before. The file can never make a level wrong, only faster to load.
namespace Zamin {

    struct NavBakeEntry
    {
        uint64_t Grid = 0;        // the grid entity's UUID
        int Dimension = 2;        // which of its components (2 / 3)
        uint64_t Fingerprint = 0; // of the sources it was baked from (Scene::GetNavGridSourceHash)
        NavGrid Data;             // as baked (carves left out)
    };
    using NavBakeList = std::vector<NavBakeEntry>;

    namespace NavBakeFile {

        static constexpr const char* Extension = ".znav";
        static constexpr uint32_t Version = 1;

        // "Level.zscene" -> "Level.znav".
        std::filesystem::path PathFor(const std::filesystem::path& sceneFile);

        // One grid as bytes (its baked state: walkable cells, costs, floor heights, MaxClimb) and back.
        void Encode(const NavGrid& grid, std::vector<uint8_t>& out);
        // False (and `grid` cleared) when the bytes are not a grid.
        bool Decode(const uint8_t* data, size_t size, size_t& offset, NavGrid& grid);

        std::vector<uint8_t> Write(const NavBakeList& entries);
        // False with the reason in `error` (a wrong header, a version from the future, cut short).
        bool Read(const std::vector<uint8_t>& bytes, NavBakeList& entries, std::string* error = nullptr);

        // The file next to `sceneFile`: written (atomically), or removed when `entries` is empty.
        bool Save(const std::filesystem::path& sceneFile, const NavBakeList& entries, std::string* error = nullptr);
        // The file next to `sceneFile` (none: an empty list, true). A broken file: a warning, false.
        bool Load(const std::filesystem::path& sceneFile, NavBakeList& entries);

    }

}
