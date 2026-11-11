#pragma once
#include "Zamin/AI/NavGrid.h"
#include <span>
#include <string>
namespace Zamin {
    // Loaded, aligned tiles only. Missing cells stay blocked. Transactional bounded CPU build;
    // mixed resolution, overlapping cells and incompatible climb rules are rejected explicitly.
    bool BuildNavMosaic(std::span<const NavGrid* const> tiles, NavGrid& output, std::string& error,
                        size_t maxCells = 262144);
}
