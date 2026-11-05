#include "Zamin/Scene/TilemapTools.h"

#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <deque>
#include <unordered_map>

namespace Zamin {

    using ordered_json = nlohmann::ordered_json;

    // ------------------------------------------------------------------ masks
    uint8_t AutoTile::ReduceMask8(uint8_t m)
    {
        if (!(m & N) || !(m & W)) m &= (uint8_t)~NW;
        if (!(m & N) || !(m & E)) m &= (uint8_t)~NE;
        if (!(m & S) || !(m & W)) m &= (uint8_t)~SW;
        if (!(m & S) || !(m & E)) m &= (uint8_t)~SE;
        return m;
    }

    const std::array<uint8_t, 47>& AutoTile::BlobMasks()
    {
        static const std::array<uint8_t, 47> s_Masks = [] {
            std::array<uint8_t, 47> masks{};
            size_t count = 0;
            for (int m = 0; m < 256; m++)
                if (ReduceMask8((uint8_t)m) == (uint8_t)m && count < masks.size())
                    masks[count++] = (uint8_t)m;
            return masks;
        }();
        return s_Masks;
    }

    int AutoTile::BlobIndex(uint8_t mask)
    {
        static const std::array<int8_t, 256> s_Index = [] {
            std::array<int8_t, 256> index{};
            const auto& masks = BlobMasks();
            for (int m = 0; m < 256; m++)
            {
                uint8_t reduced = ReduceMask8((uint8_t)m);
                index[(size_t)m] = (int8_t)(std::find(masks.begin(), masks.end(), reduced) - masks.begin());
            }
            return index;
        }();
        return s_Index[mask];
    }

    uint8_t AutoTile::BlobMask(int index)
    {
        return index >= 0 && index < 47 ? BlobMasks()[(size_t)index] : 0;
    }

    uint8_t AutoTile::EdgesOf(uint8_t m)
    {
        return (uint8_t)(((m & N) ? North : 0) | ((m & W) ? West : 0) | ((m & E) ? East : 0) | ((m & S) ? South : 0));
    }

    // ------------------------------------------------------------------ rule
    void AutoTileRule::EnsureSize()
    {
        Tiles.resize((size_t)GetVariantCount(), -1);
    }

    int AutoTileRule::GetVariant(uint8_t mask) const
    {
        return Mode == AutoTileMode::Blob8 ? AutoTile::BlobIndex(mask) : (mask & 15);
    }

    int AutoTileRule::Resolve(uint8_t mask) const
    {
        const int count = GetVariantCount();
        int variant = GetVariant(mask);
        if (variant >= 0 && variant < (int)Tiles.size() && Tiles[(size_t)variant] >= 0)
            return Tiles[(size_t)variant];
        // The closest variant with a tile: fewest neighbours that differ (ties: the lower variant).
        const uint8_t want = Mode == AutoTileMode::Blob8 ? AutoTile::ReduceMask8(mask) : (uint8_t)(mask & 15);
        int best = -1, bestDistance = 99;
        for (int i = 0; i < count && i < (int)Tiles.size(); i++)
        {
            if (Tiles[(size_t)i] < 0)
                continue;
            uint8_t have = Mode == AutoTileMode::Blob8 ? AutoTile::BlobMask(i) : (uint8_t)i;
            int distance = std::popcount((unsigned)(uint8_t)(have ^ want));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = Tiles[(size_t)i];
            }
        }
        return best;
    }

    bool AutoTileRule::Contains(int tile) const
    {
        return tile >= 0 && std::find(Tiles.begin(), Tiles.end(), tile) != Tiles.end();
    }

    bool AutoTileRule::HasTiles() const
    {
        return std::any_of(Tiles.begin(), Tiles.end(), [](int t) { return t >= 0; });
    }

    AutoTileRule AutoTileRule::Sequential(std::string name, AutoTileMode mode, int firstTile)
    {
        AutoTileRule rule;
        rule.Name = std::move(name);
        rule.Mode = mode;
        rule.Tiles.resize((size_t)rule.GetVariantCount());
        for (int i = 0; i < rule.GetVariantCount(); i++)
            rule.Tiles[(size_t)i] = std::max(0, firstTile) + i;
        return rule;
    }

    // ------------------------------------------------------------------ set
    int AutoTileSet::Find(std::string_view name) const
    {
        for (int i = 0; i < (int)Rules.size(); i++)
            if (Rules[(size_t)i].Name == name)
                return i;
        return -1;
    }

    std::string AutoTileSet::MakeUniqueName(std::string_view base) const
    {
        std::string name(base.empty() ? std::string_view("Yer") : base);
        if (Find(name) < 0)
            return name;
        for (int n = 2;; n++)
        {
            std::string candidate = name + " " + std::to_string(n);
            if (Find(candidate) < 0)
                return candidate;
        }
    }

    int AutoTileSet::FindRuleOf(int tile) const
    {
        for (int i = 0; i < (int)Rules.size(); i++)
            if (Rules[(size_t)i].Contains(tile))
                return i;
        return -1;
    }

    std::string AutoTileSet::ToJson() const
    {
        ordered_json list = ordered_json::array();
        for (const AutoTileRule& r : Rules)
        {
            ordered_json j;
            j["Name"] = r.Name;
            j["Mode"] = r.Mode == AutoTileMode::Blob8 ? "Blob8" : "Edges4";
            std::vector<int> tiles = r.Tiles;
            tiles.resize((size_t)r.GetVariantCount(), -1);
            j["Tiles"] = tiles;
            if (r.ConnectOutside)
                j["ConnectOutside"] = true;
            list.push_back(std::move(j));
        }
        return list.dump();
    }

    bool AutoTileSet::FromJson(std::string_view text, AutoTileSet& out)
    {
        ordered_json list = ordered_json::parse(text.begin(), text.end(), nullptr, false);
        if (list.is_discarded() || !list.is_array())
            return false;
        AutoTileSet set;
        for (const ordered_json& j : list)
        {
            if (!j.is_object())
                continue;
            AutoTileRule rule;
            std::string name = j.contains("Name") && j["Name"].is_string() ? j["Name"].get<std::string>() : std::string();
            rule.Name = set.MakeUniqueName(name);
            std::string mode = j.contains("Mode") && j["Mode"].is_string() ? j["Mode"].get<std::string>() : std::string();
            rule.Mode = mode == "Blob8" ? AutoTileMode::Blob8 : AutoTileMode::Edges4;
            if (auto tiles = j.find("Tiles"); tiles != j.end() && tiles->is_array())
                for (const ordered_json& t : *tiles)
                    rule.Tiles.push_back(t.is_number_integer() ? std::max(-1, t.get<int>()) : -1);
            rule.EnsureSize();
            rule.ConnectOutside = j.contains("ConnectOutside") && j["ConnectOutside"].is_boolean() && j["ConnectOutside"].get<bool>();
            set.Rules.push_back(std::move(rule));
        }
        out = std::move(set);
        return true;
    }

    AutoTileSet AutoTileSet::FromImportSettings(const AssetImportSettings& import)
    {
        AutoTileSet set;
        if (const AssetImportSettings::Value* v = import.Find(ImportKey))
            if (const auto* raw = std::get_if<AssetImportSettings::Json>(v))
                FromJson(raw->Text, set);
        return set;
    }

    void AutoTileSet::WriteTo(AssetImportSettings& import) const
    {
        if (Rules.empty())
            import.Remove(ImportKey);
        else
            import.Set(ImportKey, AssetImportSettings::Json{ ToJson() });
    }

    namespace {
        struct RuleCacheEntry
        {
            const AssetRegistry* Registry = nullptr;
            uint64_t Revision = ~0ull;
            AutoTileSet Set;
        };
        std::unordered_map<std::string, RuleCacheEntry>& RuleCache()
        {
            static std::unordered_map<std::string, RuleCacheEntry> s_Cache;
            return s_Cache;
        }
    }

    const AutoTileSet* AutoTileSet::Get(const AssetRegistry* registry, std::string_view tilesetPath)
    {
        if (!registry || tilesetPath.empty())
            return nullptr;
        auto& cache = RuleCache();
        auto it = cache.find(std::string(tilesetPath));
        if (it == cache.end())
            it = cache.emplace(std::string(tilesetPath), RuleCacheEntry{}).first;
        RuleCacheEntry& entry = it->second;
        if (entry.Registry != registry || entry.Revision != registry->GetRevision())
        {
            const AssetMetadata* meta = registry->Find(std::filesystem::path(tilesetPath));
            entry.Set = meta ? FromImportSettings(meta->Import) : AutoTileSet();
            entry.Registry = registry;
            entry.Revision = registry->GetRevision();
        }
        return entry.Set.IsEmpty() ? nullptr : &entry.Set;
    }

    const AutoTileSet* AutoTileSet::Get(std::string_view tilesetPath)
    {
        if (tilesetPath.empty() || !Project::GetActive())
            return nullptr;
        return Get(Project::GetAssetRegistry(), tilesetPath);
    }

    void AutoTileSet::ClearCache() { RuleCache().clear(); }

    // ------------------------------------------------------------------ applying rules
    bool AutoTile::IsTerrain(const TilemapComponent& tilemap, int layer, const AutoTileRule& rule, int x, int y)
    {
        if (!tilemap.Contains(x, y))
            return rule.ConnectOutside;
        return rule.Contains(tilemap.Get(x, y, layer));
    }

    uint8_t AutoTile::GetMask(const TilemapComponent& tilemap, int layer, const AutoTileRule& rule, int x, int y)
    {
        auto at = [&](int dx, int dy) { return IsTerrain(tilemap, layer, rule, x + dx, y + dy); };
        if (rule.Mode == AutoTileMode::Edges4)
            return (uint8_t)((at(0, 1) ? North : 0) | (at(-1, 0) ? West : 0) | (at(1, 0) ? East : 0) | (at(0, -1) ? South : 0));
        uint8_t m = (uint8_t)((at(-1, 1) ? NW : 0) | (at(0, 1) ? N : 0) | (at(1, 1) ? NE : 0) | (at(-1, 0) ? W : 0) |
                              (at(1, 0) ? E : 0) | (at(-1, -1) ? SW : 0) | (at(0, -1) ? S : 0) | (at(1, -1) ? SE : 0));
        return ReduceMask8(m);
    }

    int AutoTile::Refresh(TilemapComponent& tilemap, int layer, const AutoTileRule& rule, int x0, int y0, int x1, int y1)
    {
        tilemap.EnsureSize();
        if (!tilemap.IsValidLayer(layer) || !rule.HasTiles())
            return 0;
        int ax = std::max(0, std::min(x0, x1)), bx = std::min(tilemap.Width - 1, std::max(x0, x1));
        int ay = std::max(0, std::min(y0, y1)), by = std::min(tilemap.Height - 1, std::max(y0, y1));
        // Masks first (from the cells as they are), then the new tiles: the result does not depend on
        // the order the cells are visited in.
        std::vector<std::pair<glm::ivec2, int>> changes;
        for (int y = ay; y <= by; y++)
            for (int x = ax; x <= bx; x++)
            {
                int current = tilemap.Get(x, y, layer);
                if (!rule.Contains(current))
                    continue;
                int tile = rule.Resolve(GetMask(tilemap, layer, rule, x, y));
                if (tile >= 0 && tile != current)
                    changes.push_back({ { x, y }, tile });
            }
        for (const auto& [cell, tile] : changes)
            tilemap.Set(cell.x, cell.y, tile, layer);
        return (int)changes.size();
    }

    int AutoTile::Paint(TilemapComponent& tilemap, int layer, const AutoTileRule& rule, const std::vector<glm::ivec2>& cells, bool erase)
    {
        tilemap.EnsureSize();
        if (!tilemap.IsValidLayer(layer) || !rule.HasTiles() || cells.empty())
            return 0;
        // Any tile of the terrain marks a painted cell; Refresh gives it the right one.
        const int member = rule.Resolve(rule.Mode == AutoTileMode::Blob8 ? 255 : 15);
        std::vector<int> before;
        glm::ivec2 lo(tilemap.Width, tilemap.Height), hi(-1, -1);
        std::vector<glm::ivec2> touched;
        for (const glm::ivec2& c : cells)
        {
            if (!tilemap.Contains(c.x, c.y))
                continue;
            touched.push_back(c);
            lo = glm::min(lo, c);
            hi = glm::max(hi, c);
        }
        if (touched.empty())
            return 0;
        // The cells' 3 x 3 neighbourhood may change: remember it to count the changes.
        lo = glm::max(lo - 1, glm::ivec2(0));
        hi = glm::min(hi + 1, glm::ivec2(tilemap.Width - 1, tilemap.Height - 1));
        for (int y = lo.y; y <= hi.y; y++)
            for (int x = lo.x; x <= hi.x; x++)
                before.push_back(tilemap.Get(x, y, layer));
        for (const glm::ivec2& c : touched)
        {
            int current = tilemap.Get(c.x, c.y, layer);
            if (erase)
            {
                // The eraser of a terrain removes only that terrain.
                if (rule.Contains(current))
                    tilemap.Set(c.x, c.y, -1, layer);
            }
            else if (!rule.Contains(current))
                tilemap.Set(c.x, c.y, member, layer);
        }
        Refresh(tilemap, layer, rule, lo.x, lo.y, hi.x, hi.y);
        int changed = 0;
        size_t i = 0;
        for (int y = lo.y; y <= hi.y; y++)
            for (int x = lo.x; x <= hi.x; x++)
                changed += tilemap.Get(x, y, layer) != before[i++] ? 1 : 0;
        return changed;
    }

    int AutoTile::Paint(TilemapComponent& tilemap, int layer, const AutoTileRule& rule, int x, int y, bool erase)
    {
        return Paint(tilemap, layer, rule, std::vector<glm::ivec2>{ { x, y } }, erase);
    }

    // ------------------------------------------------------------------ editing tools
    std::vector<glm::ivec2> TilemapEdit::FloodRegion(const TilemapComponent& tilemap, int layer, int x, int y,
                                                     const AutoTileRule* terrain, size_t maxCells)
    {
        std::vector<glm::ivec2> region;
        if (!tilemap.IsValidLayer(layer) || !tilemap.Contains(x, y))
            return region;
        const int start = tilemap.Get(x, y, layer);
        const bool byTerrain = terrain && terrain->Contains(start);
        auto same = [&](int tile) { return byTerrain ? terrain->Contains(tile) : tile == start; };
        std::vector<uint8_t> seen((size_t)tilemap.Width * tilemap.Height, 0);
        std::deque<glm::ivec2> open{ { x, y } };
        seen[(size_t)y * tilemap.Width + x] = 1;
        const glm::ivec2 steps[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        while (!open.empty())
        {
            glm::ivec2 c = open.front();
            open.pop_front();
            region.push_back(c);
            if (maxCells && region.size() >= maxCells)
                break;
            for (const glm::ivec2& step : steps)
            {
                glm::ivec2 n = c + step;
                if (!tilemap.Contains(n.x, n.y) || seen[(size_t)n.y * tilemap.Width + n.x])
                    continue;
                seen[(size_t)n.y * tilemap.Width + n.x] = 1;
                if (same(tilemap.Get(n.x, n.y, layer)))
                    open.push_back(n);
            }
        }
        return region;
    }

    int TilemapEdit::FloodFill(TilemapComponent& tilemap, int layer, int x, int y, int tile, size_t maxCells)
    {
        tilemap.EnsureSize();
        tile = std::max(-1, tile);
        if (!tilemap.IsValidLayer(layer) || !tilemap.Contains(x, y) || tilemap.Get(x, y, layer) == tile)
            return 0;
        return SetCells(tilemap, layer, FloodRegion(tilemap, layer, x, y, nullptr, maxCells), tile);
    }

    std::vector<glm::ivec2> TilemapEdit::RectCells(const TilemapComponent& tilemap, glm::ivec2 a, glm::ivec2 b)
    {
        std::vector<glm::ivec2> cells;
        glm::ivec2 lo = glm::max(glm::min(a, b), glm::ivec2(0));
        glm::ivec2 hi = glm::min(glm::max(a, b), glm::ivec2(tilemap.Width - 1, tilemap.Height - 1));
        if (hi.x < lo.x || hi.y < lo.y)
            return cells;
        cells.reserve((size_t)(hi.x - lo.x + 1) * (size_t)(hi.y - lo.y + 1));
        for (int y = lo.y; y <= hi.y; y++)
            for (int x = lo.x; x <= hi.x; x++)
                cells.push_back({ x, y });
        return cells;
    }

    int TilemapEdit::FillRect(TilemapComponent& tilemap, int layer, glm::ivec2 a, glm::ivec2 b, int tile)
    {
        tilemap.EnsureSize();
        return SetCells(tilemap, layer, RectCells(tilemap, a, b), tile);
    }

    int TilemapEdit::SetCells(TilemapComponent& tilemap, int layer, const std::vector<glm::ivec2>& cells, int tile)
    {
        tilemap.EnsureSize();
        if (!tilemap.IsValidLayer(layer))
            return 0;
        tile = std::max(-1, tile);
        int changed = 0;
        for (const glm::ivec2& c : cells)
            if (tilemap.Contains(c.x, c.y) && tilemap.Get(c.x, c.y, layer) != tile)
            {
                tilemap.Set(c.x, c.y, tile, layer);
                changed++;
            }
        return changed;
    }

    std::vector<glm::ivec2> TilemapEdit::LineCells(glm::ivec2 a, glm::ivec2 b)
    {
        std::vector<glm::ivec2> cells;
        int dx = std::abs(b.x - a.x), dy = -std::abs(b.y - a.y);
        int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
        int err = dx + dy;
        glm::ivec2 p = a;
        // Long jumps (a stroke across the whole map in one frame) stay bounded.
        cells.reserve((size_t)std::min(4096, std::max(dx, -dy) + 1));
        while (true)
        {
            cells.push_back(p);
            if (p == b || cells.size() >= 4096)
                break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; p.x += sx; }
            if (e2 <= dx) { err += dx; p.y += sy; }
        }
        return cells;
    }

    // ------------------------------------------------------------------ brushes
    TileBrush TileBrush::Single(int tile)
    {
        TileBrush brush;
        brush.Tiles = { tile < 0 ? -1 : tile };
        return brush;
    }

    TileBrush TileBrush::FromTileset(int columns, int rows, int tileA, int tileB)
    {
        columns = std::max(1, columns);
        rows = std::max(1, rows);
        const int count = columns * rows;
        tileA = std::clamp(tileA, 0, count - 1);
        tileB = std::clamp(tileB, 0, count - 1);
        int x0 = std::min(tileA % columns, tileB % columns), x1 = std::max(tileA % columns, tileB % columns);
        int y0 = std::min(tileA / columns, tileB / columns), y1 = std::max(tileA / columns, tileB / columns);
        x1 = std::min(x1, x0 + MaxSize - 1);
        y1 = std::min(y1, y0 + MaxSize - 1);
        TileBrush brush;
        brush.Width = x1 - x0 + 1;
        brush.Height = y1 - y0 + 1;
        brush.Tiles.clear();
        brush.Tiles.reserve((size_t)brush.Width * brush.Height);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                brush.Tiles.push_back(y * columns + x);
        return brush;
    }

    int TileBrush::At(glm::ivec2 cell, glm::ivec2 origin) const
    {
        if (Width <= 0 || Height <= 0 || (int)Tiles.size() != Width * Height)
            return -1;
        auto wrap = [](int v, int n) { int m = v % n; return m < 0 ? m + n : m; };
        int bx = wrap(cell.x - origin.x, Width);
        int by = wrap(origin.y - cell.y, Height);
        return Tiles[(size_t)by * Width + bx];
    }

    std::vector<glm::ivec2> TileBrush::Footprint(glm::ivec2 origin) const
    {
        std::vector<glm::ivec2> cells;
        cells.reserve((size_t)std::max(0, Width) * std::max(0, Height));
        for (int by = 0; by < Height; by++)
            for (int bx = 0; bx < Width; bx++)
                cells.push_back({ origin.x + bx, origin.y - by });
        return cells;
    }

    std::vector<glm::ivec2> TileBrush::StrokeCells(const std::vector<glm::ivec2>& strokeCells) const
    {
        if (IsSingle())
            return strokeCells;
        std::vector<glm::ivec2> cells;
        std::unordered_map<int64_t, bool> seen;
        for (const glm::ivec2& c : strokeCells)
            for (const glm::ivec2& f : Footprint(OriginFor(c)))
                if (seen.emplace(((int64_t)f.x << 32) ^ (uint32_t)f.y, true).second)
                    cells.push_back(f);
        return cells;
    }

    int TilemapEdit::Stamp(TilemapComponent& tilemap, int layer, const std::vector<glm::ivec2>& cells, const TileBrush& brush, glm::ivec2 origin)
    {
        tilemap.EnsureSize();
        if (!tilemap.IsValidLayer(layer))
            return 0;
        int changed = 0;
        for (const glm::ivec2& c : cells)
        {
            if (!tilemap.Contains(c.x, c.y))
                continue;
            int tile = brush.At(c, origin);
            if (tilemap.Get(c.x, c.y, layer) == tile)
                continue;
            tilemap.Set(c.x, c.y, tile, layer);
            changed++;
        }
        return changed;
    }

}
