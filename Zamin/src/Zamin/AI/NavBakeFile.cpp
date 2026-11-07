#include "Zamin/AI/NavBakeFile.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Utils/FileSystem.h"

#include <cmath>
#include <cstring>
#include <format>

namespace Zamin {

    namespace {

        // "ZNAV", the version, the number of grids; per grid its UUID, dimension, fingerprint and the
        // grid itself (Encode). Little-endian, as the machines the engine runs on.
        constexpr char Magic[4] = { 'Z', 'N', 'A', 'V' };
        constexpr uint32_t MaxEntries = 4096;

        template<typename T>
        void Put(std::vector<uint8_t>& out, const T& value)
        {
            const size_t at = out.size();
            out.resize(at + sizeof(T));
            std::memcpy(out.data() + at, &value, sizeof(T));
        }

        template<typename T>
        bool Take(const uint8_t* data, size_t size, size_t& offset, T& value)
        {
            if (offset > size || size - offset < sizeof(T))
                return false;
            std::memcpy(&value, data + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        enum Flags : uint8_t { HasCosts = 1, HasHeights = 2 };

    }

    std::filesystem::path NavBakeFile::PathFor(const std::filesystem::path& sceneFile)
    {
        std::filesystem::path p = sceneFile;
        p.replace_extension(Extension);
        return p;
    }

    void NavBakeFile::Encode(const NavGrid& grid, std::vector<uint8_t>& out)
    {
        const int width = grid.GetWidth(), height = grid.GetHeight();
        Put(out, (int32_t)width);
        Put(out, (int32_t)height);
        Put(out, grid.GetCellSize());
        Put(out, grid.GetOrigin().x);
        Put(out, grid.GetOrigin().y);
        Put(out, grid.GetMaxClimb());
        bool costs = false;
        for (int y = 0; y < height && !costs; y++)
            for (int x = 0; x < width && !costs; x++)
                costs = grid.GetCost(x, y) != 1.0f;
        const uint8_t flags = (uint8_t)((costs ? HasCosts : 0) | (grid.HasHeights() ? HasHeights : 0));
        Put(out, flags);
        // The walkable cells as bits (the baked state: what Nav Obstacles carve is left out).
        const size_t cells = grid.GetCellCount();
        std::vector<uint8_t> bits((cells + 7) / 8, 0);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                if (grid.IsBakedWalkable(x, y))
                {
                    const size_t i = grid.Index(x, y);
                    bits[i / 8] |= (uint8_t)(1u << (i % 8));
                }
        out.insert(out.end(), bits.begin(), bits.end());
        if (costs)
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    Put(out, grid.GetCost(x, y));
        if (grid.HasHeights())
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    Put(out, grid.GetFloorHeight(x, y));
    }

    bool NavBakeFile::Decode(const uint8_t* data, size_t size, size_t& offset, NavGrid& grid)
    {
        grid.Clear();
        int32_t width = 0, height = 0;
        float cellSize = 0.0f, ox = 0.0f, oy = 0.0f, maxClimb = 0.0f;
        uint8_t flags = 0;
        if (!Take(data, size, offset, width) || !Take(data, size, offset, height) || !Take(data, size, offset, cellSize) ||
            !Take(data, size, offset, ox) || !Take(data, size, offset, oy) || !Take(data, size, offset, maxClimb) || !Take(data, size, offset, flags))
            return false;
        if (width < 1 || height < 1 || width > NavGrid::MaxSize || height > NavGrid::MaxSize || !(cellSize > 0.0f) || !std::isfinite(cellSize) ||
            !std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(maxClimb) || (flags & ~(HasCosts | HasHeights)) != 0)
            return false;
        const size_t cells = (size_t)width * (size_t)height;
        const size_t bitBytes = (cells + 7) / 8;
        const size_t needed = bitBytes + ((flags & HasCosts) ? cells * sizeof(float) : 0) + ((flags & HasHeights) ? cells * sizeof(float) : 0);
        if (offset > size || size - offset < needed)
            return false;
        grid.Reset(width, height, cellSize, { ox, oy });
        const uint8_t* bits = data + offset;
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                const size_t i = grid.Index(x, y);
                if (!(bits[i / 8] & (1u << (i % 8))))
                    grid.SetWalkable(x, y, false);
            }
        offset += bitBytes;
        if (flags & HasCosts)
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    float cost = 1.0f;
                    Take(data, size, offset, cost);
                    grid.SetCost(x, y, cost);
                }
        if (flags & HasHeights)
        {
            grid.EnableHeights();
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    float h = 0.0f;
                    Take(data, size, offset, h);
                    grid.SetFloorHeight(x, y, h); // NaN: no floor there
                }
        }
        grid.SetMaxClimb(maxClimb);
        return true;
    }

    std::vector<uint8_t> NavBakeFile::Write(const NavBakeList& entries)
    {
        std::vector<uint8_t> out;
        out.insert(out.end(), Magic, Magic + 4);
        Put(out, Version);
        Put(out, (uint32_t)entries.size());
        for (const NavBakeEntry& e : entries)
        {
            Put(out, e.Grid);
            Put(out, (uint8_t)e.Dimension);
            Put(out, e.Fingerprint);
            Encode(e.Data, out);
        }
        return out;
    }

    bool NavBakeFile::Read(const std::vector<uint8_t>& bytes, NavBakeList& entries, std::string* error)
    {
        entries.clear();
        auto fail = [&](const char* why) {
            if (error)
                *error = why;
            entries.clear();
            return false;
        };
        const uint8_t* data = bytes.data();
        const size_t size = bytes.size();
        if (size < 12 || std::memcmp(data, Magic, 4) != 0)
            return fail(".znav fayli emas");
        size_t offset = 4;
        uint32_t version = 0, count = 0;
        Take(data, size, offset, version);
        Take(data, size, offset, count);
        if (version == 0 || version > Version)
            return fail("noma'lum versiya (yangiroq engine yozgan)");
        if (count > MaxEntries)
            return fail("grid'lar soni noto'g'ri");
        entries.reserve(count);
        for (uint32_t i = 0; i < count; i++)
        {
            NavBakeEntry e;
            uint8_t dimension = 0;
            if (!Take(data, size, offset, e.Grid) || !Take(data, size, offset, dimension) || !Take(data, size, offset, e.Fingerprint))
                return fail("fayl kesilgan");
            if (dimension != 2 && dimension != 3)
                return fail("grid o'lchami noto'g'ri");
            e.Dimension = dimension;
            if (!Decode(data, size, offset, e.Data))
                return fail("grid ma'lumotlari buzilgan");
            entries.push_back(std::move(e));
        }
        if (error)
            error->clear();
        return true;
    }

    bool NavBakeFile::Save(const std::filesystem::path& sceneFile, const NavBakeList& entries, std::string* error)
    {
        const std::filesystem::path file = PathFor(sceneFile);
        std::error_code ec;
        if (entries.empty())
        {
            // Nothing saved: an old file would only be read and thrown away.
            std::filesystem::remove(file, ec);
            return true;
        }
        const std::vector<uint8_t> bytes = Write(entries);
        // Written beside it first: a crash halfway never leaves a cut file under the real name.
        std::filesystem::path temporary = file;
        temporary += ".tmp";
        if (!FileSystem::WriteBinaryFile(temporary, bytes.data(), bytes.size()))
        {
            if (error)
                *error = "fayl yozilmadi";
            return false;
        }
        std::filesystem::rename(temporary, file, ec);
        if (ec)
        {
            std::filesystem::remove(temporary, ec);
            if (error)
                *error = "fayl almashtirilmadi";
            return false;
        }
        return true;
    }

    bool NavBakeFile::Load(const std::filesystem::path& sceneFile, NavBakeList& entries)
    {
        entries.clear();
        const std::filesystem::path file = PathFor(sceneFile);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec))
            return true;
        const std::vector<uint8_t> bytes = FileSystem::ReadBinaryFile(file);
        std::string error;
        if (!Read(bytes, entries, &error))
        {
            ZM_CORE_WARN("Navigatsiya: '{}' o'qilmadi ({}) - grid'lar qayta yaratiladi", file.filename().string(), error);
            return false;
        }
        return true;
    }

}
