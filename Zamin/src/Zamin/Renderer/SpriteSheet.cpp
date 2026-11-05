#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Project/Project.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <unordered_map>

namespace Zamin {

    using ordered_json = nlohmann::ordered_json;

    // ------------------------------------------------------------------ frame
    void SpriteFrame::GetUV(uint32_t width, uint32_t height, glm::vec2& uvMin, glm::vec2& uvMax) const
    {
        float w = (float)std::max(1u, width), h = (float)std::max(1u, height);
        uvMin = { Position.x / w, 1.0f - (Position.y + Size.y) / h };
        uvMax = { (Position.x + Size.x) / w, 1.0f - Position.y / h };
    }

    // ------------------------------------------------------------------ lookups
    int SpriteSheet::FindIndex(std::string_view name) const
    {
        for (size_t i = 0; i < Frames.size(); i++)
            if (Frames[i].Name == name)
                return (int)i;
        return -1;
    }

    const SpriteFrame* SpriteSheet::Find(std::string_view name) const
    {
        int index = FindIndex(name);
        return index >= 0 ? &Frames[(size_t)index] : nullptr;
    }

    // "walk_12" -> ("walk", 12); false when the name does not end in a number.
    static bool SplitNumbered(std::string_view name, std::string_view& prefix, long long& number)
    {
        size_t end = name.size(), start = end;
        while (start > 0 && std::isdigit((unsigned char)name[start - 1]))
            start--;
        if (start == end || end - start > 15)
            return false;
        number = std::stoll(std::string(name.substr(start)));
        prefix = name.substr(0, start);
        if (!prefix.empty() && (prefix.back() == '_' || prefix.back() == '-' || prefix.back() == ' '))
            prefix.remove_suffix(1);
        return true;
    }

    std::vector<std::string> SpriteSheet::FindSequence(std::string_view prefix) const
    {
        std::vector<std::string> names;
        if (prefix.empty())
        {
            for (const SpriteFrame& f : Frames)
                names.push_back(f.Name);
            return names;
        }
        // "walk_" and "walk" are the same prefix.
        while (!prefix.empty() && (prefix.back() == '_' || prefix.back() == '-' || prefix.back() == ' '))
            prefix.remove_suffix(1);
        std::vector<std::pair<long long, size_t>> numbered;
        for (size_t i = 0; i < Frames.size(); i++)
        {
            std::string_view p;
            long long n = 0;
            if (SplitNumbered(Frames[i].Name, p, n) && p == prefix)
                numbered.push_back({ n, i });
        }
        std::stable_sort(numbered.begin(), numbered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [n, i] : numbered)
            names.push_back(Frames[i].Name);
        return names;
    }

    std::vector<std::string> SpriteSheet::GetSequencePrefixes() const
    {
        std::vector<std::string> prefixes;
        for (const SpriteFrame& f : Frames)
        {
            std::string_view p;
            long long n = 0;
            if (SplitNumbered(f.Name, p, n) && !p.empty() && std::find(prefixes.begin(), prefixes.end(), p) == prefixes.end())
                prefixes.emplace_back(p);
        }
        return prefixes;
    }

    std::string SpriteSheet::MakeUniqueName(std::string_view base) const
    {
        std::string name = base.empty() ? std::string("sprite") : std::string(base);
        if (FindIndex(name) < 0)
            return name;
        for (int n = 1;; n++)
        {
            std::string candidate = std::format("{}_{}", name, n);
            if (FindIndex(candidate) < 0)
                return candidate;
        }
    }

    // ------------------------------------------------------------------ slicing
    glm::ivec2 SpriteSheet::GetCellSize(const SpriteSliceSettings& s, uint32_t width, uint32_t height)
    {
        if (s.Mode == SpriteSliceSettings::SliceMode::CellSize)
            return glm::max(s.CellSize, glm::ivec2(1));
        glm::ivec2 count = glm::max(s.CellCount, glm::ivec2(1));
        glm::ivec2 padding = glm::max(s.Padding, glm::ivec2(0));
        glm::ivec2 offset = glm::max(s.Offset, glm::ivec2(0));
        glm::ivec2 free = glm::ivec2((int)width, (int)height) - offset - padding * (count - 1);
        return glm::max(free / count, glm::ivec2(1));
    }

    static bool CellHasPixels(const uint8_t* rgba, uint32_t width, glm::ivec2 pos, glm::ivec2 size)
    {
        for (int y = pos.y; y < pos.y + size.y; y++)
        {
            const uint8_t* row = rgba + ((size_t)y * width + pos.x) * 4;
            for (int x = 0; x < size.x; x++)
                if (row[(size_t)x * 4 + 3] != 0)
                    return true;
        }
        return false;
    }

    std::vector<SpriteFrame> SpriteSheet::SliceGrid(const SpriteSliceSettings& s, uint32_t width, uint32_t height, const uint8_t* rgba)
    {
        constexpr size_t MaxFrames = 10000;
        std::vector<SpriteFrame> frames;
        if (width == 0 || height == 0)
            return frames;
        const glm::ivec2 cell = GetCellSize(s, width, height);
        const glm::ivec2 padding = glm::max(s.Padding, glm::ivec2(0));
        const glm::ivec2 offset = glm::max(s.Offset, glm::ivec2(0));
        const bool counted = s.Mode == SpriteSliceSettings::SliceMode::CellCount;
        const glm::ivec2 count = glm::max(s.CellCount, glm::ivec2(1));
        const std::string prefix = s.NamePrefix.empty() ? std::string("sprite") : s.NamePrefix;
        int index = 0;
        for (int row = 0; counted ? row < count.y : true; row++)
        {
            int y = offset.y + row * (cell.y + padding.y);
            if (y + cell.y > (int)height)
                break;
            for (int col = 0; counted ? col < count.x : true; col++)
            {
                int x = offset.x + col * (cell.x + padding.x);
                if (x + cell.x > (int)width)
                    break;
                if (s.SkipEmpty && rgba && !CellHasPixels(rgba, width, { x, y }, cell))
                    continue;
                SpriteFrame f;
                f.Name = std::format("{}_{}", prefix, index++);
                f.Position = { x, y };
                f.Size = cell;
                f.Pivot = glm::clamp(s.Pivot, glm::vec2(0.0f), glm::vec2(1.0f));
                frames.push_back(std::move(f));
                if (frames.size() >= MaxFrames)
                    return frames;
            }
        }
        return frames;
    }

    // ------------------------------------------------------------------ storage
    std::string SpriteSheet::ToJson() const
    {
        ordered_json list = ordered_json::array();
        for (const SpriteFrame& f : Frames)
        {
            ordered_json j;
            j["Name"] = f.Name;
            j["Rect"] = { f.Position.x, f.Position.y, f.Size.x, f.Size.y };
            j["Pivot"] = { f.Pivot.x, f.Pivot.y };
            if (f.HasBorder())
                j["Border"] = { f.Border.x, f.Border.y, f.Border.z, f.Border.w };
            list.push_back(std::move(j));
        }
        return list.dump();
    }

    bool SpriteSheet::FromJson(std::string_view text, SpriteSheet& out)
    {
        ordered_json list = ordered_json::parse(text.begin(), text.end(), nullptr, false);
        if (list.is_discarded() || !list.is_array())
            return false;
        SpriteSheet sheet;
        for (const ordered_json& j : list)
        {
            if (!j.is_object())
                continue;
            auto name = j.find("Name");
            auto rect = j.find("Rect");
            if (name == j.end() || !name->is_string() || name->get<std::string>().empty())
                continue;
            if (rect == j.end() || !rect->is_array() || rect->size() != 4)
                continue;
            bool numbers = std::all_of(rect->begin(), rect->end(), [](const ordered_json& v) { return v.is_number(); });
            if (!numbers)
                continue;
            SpriteFrame f;
            f.Position = { (*rect)[0].get<int>(), (*rect)[1].get<int>() };
            f.Size = { (*rect)[2].get<int>(), (*rect)[3].get<int>() };
            if (f.Size.x < 1 || f.Size.y < 1)
                continue;
            if (auto pivot = j.find("Pivot"); pivot != j.end() && pivot->is_array() && pivot->size() == 2 &&
                (*pivot)[0].is_number() && (*pivot)[1].is_number())
                f.Pivot = { (*pivot)[0].get<float>(), (*pivot)[1].get<float>() };
            if (auto border = j.find("Border"); border != j.end() && border->is_array() && border->size() == 4 &&
                std::all_of(border->begin(), border->end(), [](const ordered_json& v) { return v.is_number(); }))
                f.Border = glm::max(glm::vec4((*border)[0].get<float>(), (*border)[1].get<float>(),
                                              (*border)[2].get<float>(), (*border)[3].get<float>()), glm::vec4(0.0f));
            f.Name = sheet.MakeUniqueName(name->get<std::string>());
            sheet.Frames.push_back(std::move(f));
        }
        out = std::move(sheet);
        return true;
    }

    SpriteSheet SpriteSheet::FromImportSettings(const AssetImportSettings& import)
    {
        SpriteSheet sheet;
        if (const AssetImportSettings::Value* v = import.Find(ImportKey))
            if (const auto* raw = std::get_if<AssetImportSettings::Json>(v))
                FromJson(raw->Text, sheet);
        return sheet;
    }

    void SpriteSheet::WriteTo(AssetImportSettings& import) const
    {
        if (Frames.empty())
            import.Remove(ImportKey);
        else
            import.Set(ImportKey, AssetImportSettings::Json{ ToJson() });
    }

    // ------------------------------------------------------------------ cache
    namespace {
        struct SheetCacheEntry
        {
            const AssetRegistry* Registry = nullptr;
            uint64_t Revision = ~0ull;
            bool HasFrames = false;
            SpriteSheet Sheet;
        };
        std::unordered_map<std::string, SheetCacheEntry>& SheetCache()
        {
            static std::unordered_map<std::string, SheetCacheEntry> s_Cache;
            return s_Cache;
        }
    }

    const SpriteSheet* SpriteSheet::Get(const AssetRegistry* registry, std::string_view texturePath)
    {
        if (!registry || texturePath.empty())
            return nullptr;
        auto& cache = SheetCache();
        auto it = cache.find(std::string(texturePath));
        if (it == cache.end())
            it = cache.emplace(std::string(texturePath), SheetCacheEntry{}).first;
        SheetCacheEntry& entry = it->second;
        if (entry.Registry != registry || entry.Revision != registry->GetRevision())
        {
            const AssetMetadata* meta = registry->Find(std::filesystem::path(texturePath));
            entry.Sheet = meta ? FromImportSettings(meta->Import) : SpriteSheet();
            entry.HasFrames = !entry.Sheet.IsEmpty();
            entry.Registry = registry;
            entry.Revision = registry->GetRevision();
        }
        return entry.HasFrames ? &entry.Sheet : nullptr;
    }

    const SpriteSheet* SpriteSheet::Get(std::string_view texturePath)
    {
        if (texturePath.empty() || !Project::GetActive())
            return nullptr;
        return Get(Project::GetAssetRegistry(), texturePath);
    }

    void SpriteSheet::ClearCache() { SheetCache().clear(); }

    // ------------------------------------------------------------------ regions
    SpriteRegion SpriteRegion::WholeTexture(uint32_t width, uint32_t height)
    {
        SpriteRegion r;
        r.Pixels = { (float)std::max(1u, width), (float)std::max(1u, height) };
        return r;
    }

    SpriteRegion SpriteRegion::FromFrame(const SpriteFrame& frame, uint32_t width, uint32_t height)
    {
        SpriteRegion r;
        frame.GetUV(width, height, r.UVMin, r.UVMax);
        r.Pixels = glm::max(glm::vec2(frame.Size), glm::vec2(1.0f));
        r.Pivot = frame.Pivot;
        r.Border = frame.Border;
        r.Whole = false;
        return r;
    }

    SpriteRegion SpriteRegion::GridCell(int columns, int rows, int index, uint32_t width, uint32_t height)
    {
        int cols = std::max(1, columns), rowCount = std::max(1, rows);
        int frame = std::clamp(index, 0, cols * rowCount - 1);
        int cx = frame % cols, cy = frame / cols;
        glm::vec2 cell = { 1.0f / cols, 1.0f / rowCount };
        SpriteRegion r;
        // Frames are numbered left-to-right, top-to-bottom (texture v is flipped).
        r.UVMin = { cx * cell.x, 1.0f - (cy + 1) * cell.y };
        r.UVMax = r.UVMin + cell;
        r.Pixels = glm::max(glm::vec2((float)std::max(1u, width) / cols, (float)std::max(1u, height) / rowCount), glm::vec2(1e-3f));
        r.Whole = false;
        return r;
    }

    // ------------------------------------------------------------------ geometry
    void SpriteGeometry::GetBounds(glm::vec2& min, glm::vec2& max) const
    {
        glm::vec2 size = Mode == SpriteDrawMode::Simple ? glm::vec2(1.0f) : glm::max(Size, glm::vec2(0.0f));
        min = -Pivot * size;
        max = min + size;
    }

    namespace {
        struct Segment
        {
            float P0, P1; // position
            float T0, T1; // texture coordinate
        };

        // [p0, p1] filled with copies of a `tile` long piece of the texture [t0, t1] (the last one
        // cut short); `tile` <= 0 stretches the whole piece once.
        void AddSegments(std::vector<Segment>& out, float p0, float p1, float t0, float t1, float tile)
        {
            constexpr int MaxSegments = 256;
            float length = p1 - p0;
            if (length <= 1e-6f)
                return;
            if (tile <= 1e-6f)
            {
                out.push_back({ p0, p1, t0, t1 });
                return;
            }
            float copies = length / tile;
            if (copies > (float)MaxSegments)
                tile = length / (float)MaxSegments; // never thousands of quads: the pattern grows instead
            for (float p = p0; p < p1 - 1e-5f; p += tile)
            {
                float end = std::min(p + tile, p1);
                float fraction = (end - p) / tile;
                out.push_back({ p, end, t0, t0 + (t1 - t0) * fraction });
            }
        }
    }

    size_t SpriteGeometry::Build(std::vector<SpriteQuad>& out, size_t maxQuads) const
    {
        glm::vec2 min, max;
        GetBounds(min, max);
        const size_t before = out.size();
        if (Mode == SpriteDrawMode::Simple)
        {
            out.push_back({ min, max, UVMin, UVMax });
            return 1;
        }
        if (max.x - min.x <= 1e-6f || max.y - min.y <= 1e-6f)
            return 0;

        const float ppu = PixelsPerUnit > 1e-4f ? PixelsPerUnit : 100.0f;
        const glm::vec2 region = glm::max(RegionPixels, glm::vec2(1e-3f));
        // Borders in px, never more than the picture has.
        glm::vec4 border = glm::max(Border, glm::vec4(0.0f));
        if (border.x + border.y > region.x)
        {
            float f = region.x / (border.x + border.y);
            border.x *= f; border.y *= f;
        }
        if (border.z + border.w > region.y)
        {
            float f = region.y / (border.z + border.w);
            border.z *= f; border.w *= f;
        }
        // In local units; corners that do not fit the rectangle shrink together.
        float left = border.x / ppu, right = border.y / ppu, top = border.z / ppu, bottom = border.w / ppu;
        const glm::vec2 size = max - min;
        if (left + right > size.x)
        {
            float f = size.x / (left + right);
            left *= f; right *= f;
        }
        if (bottom + top > size.y)
        {
            float f = size.y / (bottom + top);
            bottom *= f; top *= f;
        }
        const glm::vec2 duv = UVMax - UVMin;
        const float uL = UVMin.x + border.x / region.x * duv.x, uR = UVMax.x - border.y / region.x * duv.x;
        const float vB = UVMin.y + border.w / region.y * duv.y, vT = UVMax.y - border.z / region.y * duv.y;

        const bool tiled = Mode == SpriteDrawMode::Tiled;
        const float tileX = tiled ? (region.x - border.x - border.y) / ppu : 0.0f;
        const float tileY = tiled ? (region.y - border.z - border.w) / ppu : 0.0f;

        std::vector<Segment> xs, ys;
        AddSegments(xs, min.x, min.x + left, UVMin.x, uL, 0.0f);
        AddSegments(xs, min.x + left, max.x - right, uL, uR, tileX);
        AddSegments(xs, max.x - right, max.x, uR, UVMax.x, 0.0f);
        AddSegments(ys, min.y, min.y + bottom, UVMin.y, vB, 0.0f);
        AddSegments(ys, min.y + bottom, max.y - top, vB, vT, tileY);
        AddSegments(ys, max.y - top, max.y, vT, UVMax.y, 0.0f);

        for (const Segment& y : ys)
            for (const Segment& x : xs)
            {
                if (out.size() - before >= maxQuads)
                    return out.size() - before;
                out.push_back({ { x.P0, y.P0 }, { x.P1, y.P1 }, { x.T0, y.T0 }, { x.T1, y.T1 } });
            }
        return out.size() - before;
    }

}
