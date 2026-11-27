#include "Zamin/Scene/TerrainStreaming.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <functional>
#include <random>
#include <thread>

namespace Zamin {

    namespace fs = std::filesystem;
    using json = nlohmann::ordered_json;

    // ------------------------------------------------------------------ the component
    TiledTerrainComponent::TiledTerrainComponent(const TiledTerrainComponent& other) { *this = other; }
    TiledTerrainComponent& TiledTerrainComponent::operator=(const TiledTerrainComponent& o)
    {
        if (this == &o) return *this;
        Path = o.Path;
        LoadRadius = o.LoadRadius;
        UnloadRadius = o.UnloadRadius;
        LODDistance = o.LODDistance;
        MaxLOD = o.MaxLOD;
        MemoryBudgetMB = o.MemoryBudgetMB;
        MaxLoadsPerFrame = o.MaxLoadsPerFrame;
        Collision = o.Collision;
        CollisionRadius = o.CollisionRadius;
        FoliageDistance = o.FoliageDistance;
        ShowTileLOD = o.ShowTileLOD;
        Streamer.reset(); // a copy (Play, Duplicate) streams on its own
        return *this;
    }
    void TiledTerrainComponent::Sanitize()
    {
        auto finite = [](float& v, float fallback, float lo, float hi) { v = std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
        finite(LoadRadius, 320.0f, 1.0f, 100000.0f);
        finite(UnloadRadius, 400.0f, LoadRadius, 100000.0f);
        finite(LODDistance, 96.0f, 1.0f, 100000.0f);
        finite(CollisionRadius, 128.0f, 0.0f, 100000.0f);
        finite(FoliageDistance, 200.0f, 0.0f, 100000.0f);
        MaxLOD = std::min(MaxLOD, 7u);
        MemoryBudgetMB = std::clamp(MemoryBudgetMB, 1u, 65536u);
        MaxLoadsPerFrame = std::clamp(MaxLoadsPerFrame, 1u, 64u);
    }

    // ------------------------------------------------------------------ the tile set file
    uint32_t TerrainTileSet::UsableLevels() const
    {
        uint32_t levels = 0;
        while (levels < std::max(1u, LODLevels) && levels < 8 && LevelResolution(levels) >= 3) levels++;
        return std::max(1u, levels);
    }

    fs::path TerrainTileSet::TileFile(const fs::path& terrainFile, uint32_t x, uint32_t z) const
    {
        const std::string folder = TilesFolder.empty() ? terrainFile.stem().string() + ".tiles" : TilesFolder;
        return terrainFile.parent_path() / folder / std::format("{}_{}.ztile", x, z);
    }

    std::string TerrainTileSet::Validate() const
    {
        if (TilesX < 1 || TilesZ < 1 || TilesX > 256 || TilesZ > 256) return "tiles: 1..256 per side";
        if (TileResolution < 9 || TileResolution > TerrainComponent::MaxResolution || ((TileResolution - 1) & (TileResolution - 2)) != 0)
            return "tile resolution: 2^n + 1 between 9 and 257";
        if (!std::isfinite(TileSize) || TileSize < 1 || TileSize > 4096) return "tile size: 1..4096 m";
        if (!std::isfinite(HeightLimit) || HeightLimit <= 0) return "height limit must be positive";
        if (!TilesFolder.empty() && (!FileSystem::IsSafeRelativePath(TilesFolder) || TilesFolder.find('/') != std::string::npos)) return "unsafe tile folder";
        if (!FoliageCounts.empty() && FoliageCounts.size() != TileCount()) return "foliage counts do not match the tiles";
        return {};
    }

    std::string TerrainTileSet::ToJson() const
    {
        json j;
        j["Version"] = CurrentVersion;
        j["Tiles"] = { TilesX, TilesZ };
        j["TileResolution"] = TileResolution;
        j["TileSize"] = TileSize;
        j["HeightLimit"] = HeightLimit;
        j["LODLevels"] = LODLevels;
        j["TilesFolder"] = TilesFolder;
        auto& layers = j["Layers"] = json::array();
        for (const TerrainLayer& l : Layers)
            layers.push_back({ { "Color", { l.Color.x, l.Color.y, l.Color.z } }, { "TexturePath", l.TexturePath }, { "Tiling", l.Tiling },
                               { "NormalMapPath", l.NormalMapPath }, { "MaskMapPath", l.MaskMapPath }, { "Roughness", l.Roughness },
                               { "Metallic", l.Metallic }, { "Occlusion", l.Occlusion }, { "NormalStrength", l.NormalStrength } });
        j["Foliage"] = { { "NearType", PrimitiveMeshToString(FoliageNearType) }, { "FarType", PrimitiveMeshToString(FoliageFarType) },
                         { "NearModel", FoliageNearModel }, { "FarModel", FoliageFarModel },
                         { "Tint", { FoliageTint.x, FoliageTint.y, FoliageTint.z, FoliageTint.w } },
                         { "LODDistance", Foliage.LODDistance }, { "CullDistance", Foliage.CullDistance } };
        j["FoliageCounts"] = FoliageCounts;
        return j.dump(2) + "\n";
    }

    bool TerrainTileSet::FromJson(const std::string& text, TerrainTileSet& out, std::string* error)
    {
        try
        {
            const json j = json::parse(text);
            if (!j.is_object()) throw std::runtime_error("not an object");
            if (j.value("Version", 1) > CurrentVersion) throw std::runtime_error("made by a newer engine");
            TerrainTileSet s;
            if (j.contains("Tiles") && j["Tiles"].is_array() && j["Tiles"].size() == 2)
            {
                s.TilesX = j["Tiles"][0].get<uint32_t>();
                s.TilesZ = j["Tiles"][1].get<uint32_t>();
            }
            s.TileResolution = j.value("TileResolution", 129u);
            s.TileSize = j.value("TileSize", 64.0f);
            s.HeightLimit = j.value("HeightLimit", 128.0f);
            s.LODLevels = std::clamp(j.value("LODLevels", 4u), 1u, 8u);
            s.TilesFolder = j.value("TilesFolder", std::string());
            if (j.contains("Layers") && j["Layers"].is_array())
                for (size_t i = 0; i < std::min<size_t>(4, j["Layers"].size()); i++)
                {
                    const json& l = j["Layers"][i];
                    TerrainLayer& layer = s.Layers[i];
                    if (l.contains("Color") && l["Color"].is_array() && l["Color"].size() == 3)
                        layer.Color = { l["Color"][0].get<float>(), l["Color"][1].get<float>(), l["Color"][2].get<float>() };
                    layer.TexturePath = l.value("TexturePath", std::string());
                    layer.Tiling = l.value("Tiling", 0.25f);
                    layer.NormalMapPath = l.value("NormalMapPath", std::string());
                    layer.MaskMapPath = l.value("MaskMapPath", std::string());
                    layer.Roughness = l.value("Roughness", 0.95f);
                    layer.Metallic = l.value("Metallic", 0.0f);
                    layer.Occlusion = l.value("Occlusion", 1.0f);
                    layer.NormalStrength = l.value("NormalStrength", 1.0f);
                }
            if (j.contains("Foliage") && j["Foliage"].is_object())
            {
                const json& f = j["Foliage"];
                s.FoliageNearType = PrimitiveMeshFromString(f.value("NearType", std::string("Cone")));
                s.FoliageFarType = PrimitiveMeshFromString(f.value("FarType", std::string("Cube")));
                s.FoliageNearModel = f.value("NearModel", std::string());
                s.FoliageFarModel = f.value("FarModel", std::string());
                if (f.contains("Tint") && f["Tint"].is_array() && f["Tint"].size() == 4)
                    s.FoliageTint = { f["Tint"][0].get<float>(), f["Tint"][1].get<float>(), f["Tint"][2].get<float>(), f["Tint"][3].get<float>() };
                s.Foliage.LODDistance = f.value("LODDistance", 40.0f);
                s.Foliage.CullDistance = f.value("CullDistance", 180.0f);
                s.Foliage.Sanitize();
            }
            if (j.contains("FoliageCounts") && j["FoliageCounts"].is_array()) s.FoliageCounts = j["FoliageCounts"].get<std::vector<uint32_t>>();
            if (std::string problem = s.Validate(); !problem.empty()) throw std::runtime_error(problem);
            if (s.FoliageCounts.empty()) s.FoliageCounts.assign(s.TileCount(), 0);
            out = std::move(s);
            return true;
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return false;
        }
    }

    bool TerrainTileSet::Load(const fs::path& file, TerrainTileSet& out, std::string* error)
    {
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
        {
            if (error) *error = "file not found: " + file.string();
            return false;
        }
        return FromJson(FileSystem::ReadTextFile(file), out, error);
    }

    bool TerrainTileSet::Save(const fs::path& file) const { return FileSystem::WriteTextFileAtomic(file, ToJson()); }

    // ------------------------------------------------------------------ tile files
    namespace TerrainTileIO {
        namespace {
            constexpr uint32_t Version = 1;
            constexpr size_t FixedHeader = 24; // magic, version, resolution, levels, foliage count, reserved
            template<typename T> void Put(std::vector<uint8_t>& out, const T& v)
            {
                const auto* bytes = reinterpret_cast<const uint8_t*>(&v);
                out.insert(out.end(), bytes, bytes + sizeof(T));
            }
            template<typename T> T Get(const uint8_t* data)
            {
                T v;
                std::memcpy(&v, data, sizeof(T));
                return v;
            }
        }

        TerrainTileData Decimate(const TerrainTileData& full, uint32_t level)
        {
            TerrainTileData out;
            out.Level = level;
            const uint32_t step = 1u << level;
            out.Resolution = (full.Resolution - 1) / step + 1;
            out.Heights.resize(size_t(out.Resolution) * out.Resolution);
            out.Weights.resize(out.Heights.size());
            for (uint32_t z = 0; z < out.Resolution; z++)
                for (uint32_t x = 0; x < out.Resolution; x++)
                {
                    const size_t from = size_t(z * step) * full.Resolution + x * step, to = size_t(z) * out.Resolution + x;
                    out.Heights[to] = full.Heights[from];
                    out.Weights[to] = full.Weights[from];
                }
            out.Foliage = full.Foliage;
            return out;
        }

        bool Write(const fs::path& file, const TerrainTileData& full, uint32_t levels, std::string* error)
        {
            if (full.Resolution < 3 || full.Heights.size() != size_t(full.Resolution) * full.Resolution || full.Weights.size() != full.Heights.size())
            {
                if (error) *error = "the tile's samples do not match its resolution";
                return false;
            }
            levels = std::max(1u, levels);
            std::vector<uint8_t> out;
            out.insert(out.end(), { 'Z', 'T', 'I', 'L' });
            Put<uint32_t>(out, Version);
            Put<uint32_t>(out, full.Resolution);
            Put<uint32_t>(out, levels);
            Put<uint32_t>(out, uint32_t(full.Foliage.size()));
            Put<uint32_t>(out, 0);
            const size_t table = out.size();
            out.resize(out.size() + (levels + 1) * 16); // (offset, bytes) per level and the foliage
            std::vector<std::pair<uint64_t, uint64_t>> entries;
            for (uint32_t level = 0; level < levels; level++)
            {
                const TerrainTileData data = level ? Decimate(full, level) : full;
                const uint64_t start = out.size();
                for (float h : data.Heights) Put<float>(out, h);
                for (const glm::vec4& w : data.Weights)
                {
                    const glm::vec4 c = glm::clamp(w, glm::vec4(0), glm::vec4(1));
                    for (int i = 0; i < 4; i++) out.push_back(uint8_t(std::lround(c[i] * 255.0f)));
                }
                entries.push_back({ start, out.size() - start });
            }
            const uint64_t foliageStart = out.size();
            for (const FoliageInstance& f : full.Foliage)
            {
                const float values[7] = { f.Position.x, f.Position.y, f.Position.z, f.Rotation.x, f.Rotation.y, f.Rotation.z, f.Scale };
                for (float v : values) Put<float>(out, v);
            }
            entries.push_back({ foliageStart, out.size() - foliageStart });
            for (size_t i = 0; i < entries.size(); i++)
            {
                std::memcpy(out.data() + table + i * 16, &entries[i].first, 8);
                std::memcpy(out.data() + table + i * 16 + 8, &entries[i].second, 8);
            }
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            const fs::path temporary = file.string() + ".tmp";
            if (!FileSystem::WriteBinaryFile(temporary, out.data(), out.size()))
            {
                if (error) *error = "could not write " + file.string();
                return false;
            }
            fs::rename(temporary, file, ec);
            if (ec && error) *error = ec.message();
            return !ec;
        }

        bool Read(const fs::path& file, uint32_t level, bool foliage, TerrainTileData& out, std::string* error)
        {
            auto fail = [&](const std::string& why) {
                if (error) *error = why + ": " + file.filename().string();
                return false;
            };
            std::ifstream in(file, std::ios::binary);
            if (!in) return fail("tile not found");
            uint8_t header[FixedHeader];
            if (!in.read(reinterpret_cast<char*>(header), FixedHeader) || std::memcmp(header, "ZTIL", 4) != 0) return fail("not a tile file");
            if (Get<uint32_t>(header + 4) != Version) return fail("tile made by another engine version");
            const uint32_t resolution = Get<uint32_t>(header + 8), levels = Get<uint32_t>(header + 12), count = Get<uint32_t>(header + 16);
            if (resolution < 3 || resolution > TerrainComponent::MaxResolution || levels < 1 || levels > 8 || count > 4000000) return fail("damaged tile header");
            level = std::min(level, levels - 1);
            std::vector<uint8_t> table((levels + 1) * 16);
            if (!in.read(reinterpret_cast<char*>(table.data()), std::streamsize(table.size()))) return fail("damaged tile table");
            std::error_code ec;
            const uint64_t size = fs::file_size(file, ec);
            const uint64_t offset = Get<uint64_t>(table.data() + level * 16), bytes = Get<uint64_t>(table.data() + level * 16 + 8);
            const uint32_t r = ((resolution - 1) >> level) + 1;
            if (ec || bytes != uint64_t(r) * r * 8 || offset + bytes > size) return fail("damaged tile level");
            std::vector<uint8_t> data(bytes);
            in.seekg(std::streamoff(offset));
            if (!in.read(reinterpret_cast<char*>(data.data()), std::streamsize(bytes))) return fail("truncated tile");
            out = {};
            out.Level = level;
            out.Resolution = r;
            out.Heights.resize(size_t(r) * r);
            out.Weights.resize(out.Heights.size());
            std::memcpy(out.Heights.data(), data.data(), out.Heights.size() * 4);
            const uint8_t* weights = data.data() + out.Heights.size() * 4;
            for (size_t i = 0; i < out.Weights.size(); i++)
            {
                glm::vec4 w(weights[i * 4], weights[i * 4 + 1], weights[i * 4 + 2], weights[i * 4 + 3]);
                const float sum = w.x + w.y + w.z + w.w;
                out.Weights[i] = sum > 0 ? w / sum : glm::vec4(1, 0, 0, 0);
            }
            for (float& h : out.Heights)
                if (!std::isfinite(h)) h = 0;
            out.FileBytes = FixedHeader + table.size() + bytes;
            if (foliage && count)
            {
                const uint64_t fOffset = Get<uint64_t>(table.data() + levels * 16), fBytes = Get<uint64_t>(table.data() + levels * 16 + 8);
                if (fBytes != uint64_t(count) * 28 || fOffset + fBytes > size) return fail("damaged tile foliage");
                std::vector<float> values(size_t(count) * 7);
                in.seekg(std::streamoff(fOffset));
                if (!in.read(reinterpret_cast<char*>(values.data()), std::streamsize(fBytes))) return fail("truncated tile foliage");
                out.Foliage.resize(count);
                for (uint32_t i = 0; i < count; i++)
                {
                    const float* v = values.data() + size_t(i) * 7;
                    out.Foliage[i] = { { v[0], v[1], v[2] }, { v[3], v[4], v[5] }, v[6] };
                }
                out.FileBytes += fBytes;
            }
            return true;
        }
    }

    // ------------------------------------------------------------------ creation and migration
    namespace TerrainTileTools {
        uint64_t EstimateBytes(const TerrainTileSet& set, uint32_t level, uint32_t foliage)
        {
            const uint64_t r = set.LevelResolution(level), samples = r * r;
            // CPU: float height + vec4 weight; GPU: RGBA32F control + mesh vertices (position, normal, uv) + indices.
            return samples * (4 + 16) + samples * 16 + samples * (32 + 24) + uint64_t(foliage) * (sizeof(FoliageInstance) + 64);
        }

        std::array<uint8_t, 4> EdgeLODs(int self, const std::array<int, 4>& neighbours)
        {
            std::array<uint8_t, 4> edges{};
            for (int i = 0; i < 4; i++)
                edges[i] = self >= 0 && neighbours[i] > self ? uint8_t(neighbours[i] - self) : 0;
            return edges;
        }

        static void PlantFoliage(TerrainTileData& tile, const TerrainTileSet& set, uint32_t count, uint32_t seed)
        {
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            TerrainComponent probe;
            probe.Resolution = tile.Resolution;
            probe.Size = glm::vec2(set.TileSize);
            probe.HeightLimit = set.HeightLimit;
            probe.Heights = tile.Heights;
            probe.Weights = tile.Weights;
            for (uint32_t attempt = 0; attempt < count * 4 && tile.Foliage.size() < count; attempt++)
            {
                const glm::vec2 p(unit(random) * set.TileSize, unit(random) * set.TileSize);
                const glm::vec3 normal = TerrainTools::SampleNormal(probe, p.x, p.y);
                if (normal.y < 0.85f) continue; // not on steep slopes
                tile.Foliage.push_back({ { p.x, TerrainTools::SampleHeight(probe, p.x, p.y), p.y }, { 0, unit(random) * 360.0f, 0 }, 0.7f + unit(random) * 0.6f });
            }
        }

        static bool WriteSet(const fs::path& terrainFile, TerrainTileSet& set, const std::function<TerrainTileData(uint32_t, uint32_t)>& makeTile, std::string& error)
        {
            set.FoliageCounts.clear(); // counted again below
            if (std::string problem = set.Validate(); !problem.empty())
            {
                error = problem;
                return false;
            }
            if (set.TilesFolder.empty()) set.TilesFolder = terrainFile.stem().string() + ".tiles";
            std::error_code ec;
            fs::remove_all(terrainFile.parent_path() / set.TilesFolder, ec);
            set.FoliageCounts.assign(set.TileCount(), 0);
            const uint32_t levels = set.UsableLevels();
            for (uint32_t z = 0; z < set.TilesZ; z++)
                for (uint32_t x = 0; x < set.TilesX; x++)
                {
                    TerrainTileData tile = makeTile(x, z);
                    set.FoliageCounts[z * set.TilesX + x] = uint32_t(tile.Foliage.size());
                    if (!TerrainTileIO::Write(set.TileFile(terrainFile, x, z), tile, levels, &error)) return false;
                }
            set.LODLevels = levels;
            if (!set.Save(terrainFile))
            {
                error = "could not write " + terrainFile.string();
                return false;
            }
            return true;
        }

        bool Create(const fs::path& terrainFile, TerrainTileSet& set, const TerrainTools::WorldTerrainSettings& world, uint32_t foliagePerTile, std::string& error)
        {
            return WriteSet(terrainFile, set, [&](uint32_t x, uint32_t z) {
                TerrainTileData tile;
                tile.Resolution = set.TileResolution;
                tile.Heights.resize(size_t(tile.Resolution) * tile.Resolution);
                tile.Weights.assign(tile.Heights.size(), glm::vec4(1, 0, 0, 0));
                const double spacing = double(set.TileSize) / double(tile.Resolution - 1);
                for (uint32_t j = 0; j < tile.Resolution; j++)
                    for (uint32_t i = 0; i < tile.Resolution; i++)
                    {
                        // Absolute positions: neighbouring tiles compute the same shared samples bit for bit.
                        const glm::dvec2 p(double(x) * set.TileSize + double(i) * spacing, double(z) * set.TileSize + double(j) * spacing);
                        const float h = std::clamp(TerrainTools::WorldHeight(p, world), 0.0f, set.HeightLimit);
                        const size_t index = size_t(j) * tile.Resolution + i;
                        tile.Heights[index] = h;
                        // Higher ground turns to rock (layer 1), steep low ground to soil (layer 2).
                        const float rock = std::clamp((h - world.Base) / std::max(0.1f, world.Amplitude), 0.0f, 1.0f);
                        tile.Weights[index] = glm::vec4(1 - rock, rock, 0, 0);
                    }
                PlantFoliage(tile, set, foliagePerTile, world.Seed * 7919u + z * 131u + x);
                return tile;
            }, error);
        }

        bool MigrateFromTerrain(const TerrainComponent& sourceIn, const FoliageComponent* foliage, const fs::path& terrainFile, uint32_t tileResolution,
                                TerrainTileSet& out, std::string& error)
        {
            TerrainComponent source = sourceIn;
            source.EnsureSize();
            if (tileResolution < 9 || tileResolution > TerrainComponent::MaxResolution || ((tileResolution - 1) & (tileResolution - 2)) != 0)
            {
                error = "tile resolution: 2^n + 1 between 9 and 257";
                return false;
            }
            const uint32_t cells = source.Resolution - 1, tileCells = tileResolution - 1;
            const bool exact = cells % tileCells == 0;
            const uint32_t tiles = std::max(1u, exact ? cells / tileCells : uint32_t(std::lround(double(cells) / tileCells)));
            if (std::abs(source.Size.x - source.Size.y) > 1e-3f)
            {
                error = "only square terrains can be tiled (" + std::format("{} x {} m", source.Size.x, source.Size.y) + ")";
                return false;
            }
            TerrainTileSet set = out;
            set.TilesX = set.TilesZ = tiles;
            set.TileResolution = tileResolution;
            set.TileSize = source.Size.x / float(tiles);
            set.HeightLimit = source.HeightLimit;
            set.Layers = source.Layers;
            if (foliage)
            {
                set.FoliageNearType = foliage->NearType;
                set.FoliageFarType = foliage->FarType;
                set.FoliageNearModel = foliage->NearModel;
                set.FoliageFarModel = foliage->FarModel;
                set.FoliageTint = foliage->Tint;
                set.Foliage = foliage->Settings;
            }
            const uint32_t total = tiles * tileCells + 1;
            auto sample = [&](uint32_t gx, uint32_t gz, float& h, glm::vec4& w) {
                if (exact)
                {
                    const size_t i = size_t(gz) * source.Resolution + gx;
                    h = source.Heights[i];
                    w = source.Weights[i];
                    return;
                }
                // Bilinear resampling onto the tile grid.
                const float fx = float(gx) / float(total - 1) * float(cells), fz = float(gz) / float(total - 1) * float(cells);
                const uint32_t x0 = std::min(uint32_t(fx), cells - 1), z0 = std::min(uint32_t(fz), cells - 1);
                const float a = fx - float(x0), b = fz - float(z0);
                auto at = [&](uint32_t x, uint32_t z) { return size_t(z) * source.Resolution + x; };
                h = glm::mix(glm::mix(source.Heights[at(x0, z0)], source.Heights[at(x0 + 1, z0)], a), glm::mix(source.Heights[at(x0, z0 + 1)], source.Heights[at(x0 + 1, z0 + 1)], a), b);
                w = glm::mix(glm::mix(source.Weights[at(x0, z0)], source.Weights[at(x0 + 1, z0)], a), glm::mix(source.Weights[at(x0, z0 + 1)], source.Weights[at(x0 + 1, z0 + 1)], a), b);
            };
            if (!WriteSet(terrainFile, set, [&](uint32_t tx, uint32_t tz) {
                    TerrainTileData tile;
                    tile.Resolution = tileResolution;
                    tile.Heights.resize(size_t(tileResolution) * tileResolution);
                    tile.Weights.resize(tile.Heights.size());
                    for (uint32_t j = 0; j < tileResolution; j++)
                        for (uint32_t i = 0; i < tileResolution; i++)
                            sample(tx * tileCells + i, tz * tileCells + j, tile.Heights[size_t(j) * tileResolution + i], tile.Weights[size_t(j) * tileResolution + i]);
                    if (foliage)
                        for (const FoliageInstance& f : foliage->Instances)
                        {
                            const uint32_t fx = uint32_t(std::clamp(std::floor(f.Position.x / set.TileSize), 0.0f, float(tiles - 1)));
                            const uint32_t fz = uint32_t(std::clamp(std::floor(f.Position.z / set.TileSize), 0.0f, float(tiles - 1)));
                            if (fx != tx || fz != tz) continue;
                            FoliageInstance local = f;
                            local.Position.x -= float(tx) * set.TileSize;
                            local.Position.z -= float(tz) * set.TileSize;
                            tile.Foliage.push_back(local);
                        }
                    return tile;
                }, error))
                return false;
            out = std::move(set);
            return true;
        }
    }

    // ------------------------------------------------------------------ the IO worker
    namespace {
        // One low-priority reader thread shared by every tiled terrain: reads tiles in request order.
        class TileReader
        {
        public:
            static TileReader& Get()
            {
                static TileReader reader;
                return reader;
            }
            void Push(std::function<void()> job)
            {
                {
                    std::lock_guard lock(m_Mutex);
                    if (!m_Thread.joinable()) m_Thread = std::thread([this] { Run(); });
                    m_Jobs.push_back(std::move(job));
                    m_Pending++;
                }
                m_Wake.notify_one();
            }
            void WaitIdle()
            {
                std::unique_lock lock(m_Mutex);
                m_Idle.wait(lock, [this] { return m_Pending == 0; });
            }
            ~TileReader()
            {
                {
                    std::lock_guard lock(m_Mutex);
                    m_Stop = true;
                }
                m_Wake.notify_all();
                if (m_Thread.joinable()) m_Thread.join();
            }

        private:
            void Run()
            {
                for (;;)
                {
                    std::function<void()> job;
                    {
                        std::unique_lock lock(m_Mutex);
                        m_Wake.wait(lock, [this] { return m_Stop || !m_Jobs.empty(); });
                        if (m_Stop) return;
                        job = std::move(m_Jobs.front());
                        m_Jobs.pop_front();
                    }
                    job();
                    {
                        std::lock_guard lock(m_Mutex);
                        m_Pending--;
                    }
                    m_Idle.notify_all();
                }
            }
            std::mutex m_Mutex;
            std::condition_variable m_Wake, m_Idle;
            std::deque<std::function<void()>> m_Jobs;
            size_t m_Pending = 0;
            bool m_Stop = false;
            std::thread m_Thread;
        };

        using Runtime = TiledTerrainComponent::Runtime;

        fs::path ResolveFile(const std::string& path)
        {
            if (path.empty()) return {};
            if (Project::GetActive())
            {
                const fs::path file = FileSystem::SafeJoin(Project::GetAssetDirectory(), path);
                if (!file.empty()) return file;
            }
            return fs::path(path).is_absolute() ? fs::path(path) : fs::path();
        }

        std::array<TerrainLayer, 4> DebugLayers(int level)
        {
            static const glm::vec3 colors[8] = { { .15f, .75f, .2f }, { .95f, .85f, .15f }, { .95f, .5f, .1f }, { .9f, .15f, .15f },
                                                 { .7f, .2f, .8f }, { .2f, .4f, .9f }, { .2f, .8f, .8f }, { .6f, .6f, .6f } };
            std::array<TerrainLayer, 4> layers;
            for (TerrainLayer& l : layers)
            {
                l = TerrainLayer{};
                l.Color = colors[std::clamp(level, 0, 7)];
            }
            return layers;
        }

        float TileDistance(const TerrainTileSet& set, uint32_t x, uint32_t z, glm::vec2 camera)
        {
            const float x0 = float(x) * set.TileSize, z0 = float(z) * set.TileSize;
            const float dx = std::max({ 0.0f, x0 - camera.x, camera.x - (x0 + set.TileSize) });
            const float dz = std::max({ 0.0f, z0 - camera.y, camera.y - (z0 + set.TileSize) });
            return std::sqrt(dx * dx + dz * dz);
        }

        int DesiredLevel(const TiledTerrainComponent& c, float distance, int current, int maxLevel)
        {
            auto threshold = [&](int level) { return level <= 0 ? 0.0f : c.LODDistance * std::ldexp(1.0f, level - 1); };
            int level = 0;
            while (level < maxLevel && distance >= threshold(level + 1)) level++;
            // A 10% dead band keeps a tile at its level while the camera hovers at a threshold.
            if (current >= 0 && std::abs(current - level) == 1)
            {
                const float lo = threshold(current) * 0.9f, hi = current >= maxLevel ? 1e30f : threshold(current + 1) * 1.1f;
                if (distance >= lo && distance < hi) return current;
            }
            return level;
        }

        Runtime& Open(TiledTerrainComponent& c)
        {
            if (!c.Streamer) c.Streamer = CreateRef<Runtime>();
            Runtime& rt = *c.Streamer;
            if (rt.OpenedPath == c.Path && (!rt.Tiles.empty() || !rt.Stats.Error.empty())) return rt;
            TerrainStreaming::Close(c);
            c.Streamer = CreateRef<Runtime>();
            Runtime& fresh = *c.Streamer;
            fresh.OpenedPath = c.Path;
            fresh.File = ResolveFile(c.Path);
            std::string error;
            if (fresh.File.empty() || !TerrainTileSet::Load(fresh.File, fresh.Set, &error))
            {
                fresh.Stats.Error = error.empty() ? "no tile set (.zterrain)" : error;
                return fresh;
            }
            fresh.Tiles.resize(fresh.Set.TileCount());
            return fresh;
        }

        void MakeResident(Runtime& rt, uint32_t index, TerrainTileData&& data)
        {
            Runtime::Tile& tile = rt.Tiles[index];
            auto terrain = CreateRef<TerrainComponent>();
            terrain->Resolution = data.Resolution;
            terrain->Size = glm::vec2(rt.Set.TileSize);
            terrain->HeightLimit = rt.Set.HeightLimit;
            terrain->Collision = false; // the streamer makes the colliders
            terrain->LODEnabled = true;
            terrain->LockBorderLOD = true;
            terrain->Heights = std::move(data.Heights);
            terrain->Weights = std::move(data.Weights);
            terrain->Layers = rt.DebugLayers ? DebugLayers(int(data.Level)) : rt.Set.Layers;
            tile.Terrain = terrain;
            tile.Foliage.reset();
            if (!data.Foliage.empty())
            {
                auto foliage = CreateRef<FoliageComponent>();
                foliage->NearType = rt.Set.FoliageNearType;
                foliage->FarType = rt.Set.FoliageFarType;
                foliage->NearModel = rt.Set.FoliageNearModel;
                foliage->FarModel = rt.Set.FoliageFarModel;
                foliage->Tint = rt.Set.FoliageTint;
                foliage->Settings = rt.Set.Foliage;
                foliage->Instances = std::move(data.Foliage);
                tile.Foliage = foliage;
            }
            if (tile.Level != int(data.Level) && tile.Level >= 0) rt.Stats.LODChanges++;
            tile.Level = int(data.Level);
            tile.Requested = -1;
            tile.Bytes = TerrainTileTools::EstimateBytes(rt.Set, data.Level, tile.Foliage ? uint32_t(tile.Foliage->Instances.size()) : 0);
            tile.RequestedBytes = 0;
            rt.Stats.LoadsThisFrame++;
            rt.Stats.TotalLoads++;
            rt.Stats.BytesRead += data.FileBytes;
        }

        void Unload(Runtime& rt, Runtime::Tile& tile)
        {
            if (tile.Level >= 0)
            {
                rt.Stats.UnloadsThisFrame++;
                rt.Stats.TotalUnloads++;
            }
            tile.Terrain.reset();
            tile.Foliage.reset();
            tile.Level = -1;
            tile.Bytes = 0;
        }

        void Cancel(Runtime::Tile& tile)
        {
            if (tile.Requested < 0) return;
            tile.Generation++;
            tile.Requested = -1;
            tile.RequestedBytes = 0;
        }

        // Reads a tile level now (main thread: editing, synchronous streaming).
        bool ReadNow(Runtime& rt, uint32_t index, int level, bool foliage)
        {
            TerrainTileData data;
            std::string error;
            const uint32_t x = index % rt.Set.TilesX, z = index / rt.Set.TilesX;
            const auto start = std::chrono::steady_clock::now();
            if (!TerrainTileIO::Read(rt.Set.TileFile(rt.File, x, z), uint32_t(level), foliage, data, &error))
            {
                rt.Stats.Error = error;
                return false;
            }
            rt.Stats.IOMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            Cancel(rt.Tiles[index]);
            MakeResident(rt, index, std::move(data));
            return true;
        }

        void UpdateEdges(Runtime& rt)
        {
            const uint32_t nx = rt.Set.TilesX, nz = rt.Set.TilesZ;
            for (uint32_t z = 0; z < nz; z++)
                for (uint32_t x = 0; x < nx; x++)
                {
                    Runtime::Tile& tile = rt.Tiles[z * nx + x];
                    if (!tile.Terrain) continue;
                    auto level = [&](int tx, int tz) { return tx < 0 || tz < 0 || tx >= int(nx) || tz >= int(nz) ? -1 : rt.Tiles[size_t(tz) * nx + tx].Level; };
                    tile.Terrain->EdgeLOD = TerrainTileTools::EdgeLODs(tile.Level, { level(int(x) - 1, int(z)), level(int(x) + 1, int(z)), level(int(x), int(z) - 1), level(int(x), int(z) + 1) });
                }
        }
    }

    // ------------------------------------------------------------------ streaming
    const TerrainStreamingStats& TerrainStreaming::Update(TiledTerrainComponent& c, const TerrainStreamingView& view)
    {
        c.Sanitize();
        Runtime& rt = Open(c);
        if (view.Frame && rt.LastUpdateFrame == view.Frame) return rt.Stats;
        rt.LastUpdateFrame = view.Frame;
        const auto started = std::chrono::steady_clock::now();
        TerrainStreamingStats& stats = rt.Stats;
        stats.LoadsThisFrame = stats.UnloadsThisFrame = 0;
        if (rt.Tiles.empty()) return stats;
        const TerrainTileSet& set = rt.Set;
        const int maxLevel = int(std::min<uint32_t>(c.MaxLOD, set.UsableLevels() - 1));
        const uint64_t budget = uint64_t(c.MemoryBudgetMB) << 20;
        const glm::mat4 inverse = std::abs(glm::determinant(view.World)) > 1e-12f ? glm::inverse(view.World) : glm::mat4(1);
        const glm::vec3 local = glm::vec3(inverse * glm::vec4(view.Camera, 1));
        const glm::vec2 camera(local.x, local.z);
        const int foliageLevel = DesiredLevel(c, c.FoliageDistance, -1, 7);

        if (rt.DebugLayers != c.ShowTileLOD)
        {
            rt.DebugLayers = c.ShowTileLOD;
            for (Runtime::Tile& tile : rt.Tiles)
                if (tile.Terrain) tile.Terrain->Layers = c.ShowTileLOD ? DebugLayers(tile.Level) : set.Layers;
        }
        // 1. Finished reads become resident (a few a frame); cancelled ones are dropped.
        std::vector<Runtime::Completed> completed;
        {
            std::lock_guard lock(rt.CompletedMutex);
            completed.swap(rt.CompletedReads);
        }
        std::vector<Runtime::Completed> later;
        for (Runtime::Completed& done : completed)
        {
            Runtime::Tile& tile = rt.Tiles[done.Index];
            stats.IOMilliseconds += done.Milliseconds;
            if (done.Generation != tile.Generation || tile.Requested < 0) continue;
            if (!done.Ok)
            {
                stats.Error = done.Error;
                Cancel(tile);
                continue;
            }
            if (stats.LoadsThisFrame >= c.MaxLoadsPerFrame)
            {
                later.push_back(std::move(done));
                continue;
            }
            MakeResident(rt, done.Index, std::move(done.Data));
        }
        if (!later.empty())
        {
            std::lock_guard lock(rt.CompletedMutex);
            rt.CompletedReads.insert(rt.CompletedReads.begin(), std::make_move_iterator(later.begin()), std::make_move_iterator(later.end()));
        }

        // 2. The plan: wanted tiles nearest first at their distance's level; over the budget the farthest are made
        //    coarser first and dropped last. Edited tiles stay at full detail.
        struct Candidate
        {
            uint32_t Index;
            float Distance;
            int Level;
        };
        std::vector<Candidate> plan;
        for (uint32_t i = 0; i < rt.Tiles.size(); i++)
        {
            Runtime::Tile& tile = rt.Tiles[i];
            tile.Distance = TileDistance(set, i % set.TilesX, i / set.TilesX, camera);
            const bool held = tile.Level >= 0 || tile.Requested >= 0;
            if (tile.Dirty || tile.Distance <= c.LoadRadius || (held && tile.Distance <= c.UnloadRadius))
                plan.push_back({ i, tile.Distance, tile.Dirty ? 0 : DesiredLevel(c, tile.Distance, tile.Level >= 0 ? tile.Level : tile.Requested, maxLevel) });
        }
        std::sort(plan.begin(), plan.end(), [](const Candidate& a, const Candidate& b) { return a.Distance != b.Distance ? a.Distance < b.Distance : a.Index < b.Index; });
        auto foliageOf = [&](const Candidate& p) { return p.Level <= foliageLevel ? set.FoliageCounts[p.Index] : 0u; };
        uint64_t total = 0;
        for (const Candidate& p : plan) total += TerrainTileTools::EstimateBytes(set, uint32_t(p.Level), foliageOf(p));
        // From the farthest in: each made as coarse as allowed, and dropped when that is still too much - so the
        // nearest tiles keep their detail as long as anything farther can give way.
        for (size_t i = plan.size(); i-- > 0 && total > budget;)
        {
            if (rt.Tiles[plan[i].Index].Dirty) continue;
            while (total > budget && plan[i].Level < maxLevel)
            {
                total -= TerrainTileTools::EstimateBytes(set, uint32_t(plan[i].Level), foliageOf(plan[i]));
                plan[i].Level++;
                total += TerrainTileTools::EstimateBytes(set, uint32_t(plan[i].Level), foliageOf(plan[i]));
                stats.Coarsened++;
            }
            if (total > budget)
            {
                total -= TerrainTileTools::EstimateBytes(set, uint32_t(plan[i].Level), foliageOf(plan[i]));
                plan.erase(plan.begin() + std::ptrdiff_t(i));
                stats.OverBudgetDrops++;
            }
        }
        stats.Wanted = uint32_t(plan.size());

        // 3. Tiles out of the plan go (their reads are cancelled).
        std::vector<int> planned(rt.Tiles.size(), -1);
        for (const Candidate& p : plan) planned[p.Index] = p.Level;
        uint64_t resident = 0, inFlight = 0;
        for (uint32_t i = 0; i < rt.Tiles.size(); i++)
        {
            Runtime::Tile& tile = rt.Tiles[i];
            if (planned[i] < 0)
            {
                Cancel(tile);
                if (!tile.Dirty) Unload(rt, tile);
            }
            else if (tile.Requested >= 0 && tile.Requested != planned[i])
                Cancel(tile);
            resident += tile.Bytes;
            inFlight += tile.RequestedBytes;
        }

        // 3b. The budget is a hard limit: when what is resident and being read is over it (the budget was lowered, the
        //     camera jumped), tiles go farthest first - those the plan dropped or wants coarser than they are.
        if (resident + inFlight > budget)
        {
            std::vector<uint32_t> order(rt.Tiles.size());
            for (uint32_t i = 0; i < order.size(); i++) order[i] = i;
            std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return rt.Tiles[a].Distance > rt.Tiles[b].Distance; });
            for (int pass = 0; pass < 2 && resident + inFlight > budget; pass++)
                for (uint32_t i : order)
                {
                    if (resident + inFlight <= budget) break;
                    Runtime::Tile& tile = rt.Tiles[i];
                    if (tile.Dirty) continue;
                    if (tile.Requested >= 0 && (pass == 1 || planned[i] < 0))
                    {
                        inFlight -= tile.RequestedBytes;
                        Cancel(tile);
                    }
                    // Pass 0: only tiles finer than planned (they come back coarser); pass 1: anything, farthest first.
                    if (tile.Level >= 0 && (pass == 1 || tile.Level < planned[i]))
                    {
                        resident -= tile.Bytes;
                        Unload(rt, tile);
                        stats.Evictions++;
                    }
                }
        }

        // 4. Reads, nearest first, while they fit the budget (the old level stays drawn until the new one is in).
        uint32_t started_ = 0;
        uint32_t inFlightCount = 0;
        for (const Runtime::Tile& tile : rt.Tiles) inFlightCount += tile.Requested >= 0;
        for (const Candidate& p : plan)
        {
            Runtime::Tile& tile = rt.Tiles[p.Index];
            if (tile.Level == p.Level || tile.Requested == p.Level || tile.Dirty) continue;
            if (started_ >= c.MaxLoadsPerFrame || inFlightCount >= 2 * c.MaxLoadsPerFrame) break;
            const bool foliage = p.Level <= foliageLevel;
            const uint64_t bytes = TerrainTileTools::EstimateBytes(set, uint32_t(p.Level), foliage ? set.FoliageCounts[p.Index] : 0);
            if (resident + inFlight + bytes > budget)
            {
                // A level that does not fit beside the old one: the old one goes first (the budget is a hard limit).
                if (tile.Level >= 0 && resident - tile.Bytes + inFlight + bytes <= budget)
                {
                    resident -= tile.Bytes;
                    Unload(rt, tile);
                }
                else
                    continue;
            }
            tile.Requested = p.Level;
            tile.RequestedBytes = bytes;
            inFlight += bytes;
            inFlightCount++;
            started_++;
            if (view.Synchronous)
            {
                ReadNow(rt, p.Index, p.Level, foliage);
                resident += tile.Bytes;
                inFlight -= bytes;
                inFlightCount--;
                continue;
            }
            const uint64_t generation = ++tile.Generation;
            std::weak_ptr<Runtime> weak = c.Streamer;
            const fs::path file = set.TileFile(rt.File, p.Index % set.TilesX, p.Index / set.TilesX);
            TileReader::Get().Push([weak, file, index = p.Index, level = p.Level, foliage, generation] {
                auto runtime = weak.lock();
                if (!runtime) return;
                Runtime::Completed done;
                done.Index = index;
                done.Generation = generation;
                const auto begin = std::chrono::steady_clock::now();
                done.Ok = TerrainTileIO::Read(file, uint32_t(level), foliage, done.Data, &done.Error);
                done.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                std::lock_guard lock(runtime->CompletedMutex);
                runtime->CompletedReads.push_back(std::move(done));
            });
        }

        // 5. Stitching and the numbers.
        UpdateEdges(rt);
        stats.Tiles = uint32_t(rt.Tiles.size());
        stats.Resident = stats.Loading = stats.FoliageTiles = stats.DirtyTiles = 0;
        stats.ResidentBytes = stats.InFlightBytes = 0;
        stats.LODCounts.fill(0);
        for (const Runtime::Tile& tile : rt.Tiles)
        {
            if (tile.Level >= 0)
            {
                stats.Resident++;
                stats.LODCounts[size_t(std::min(tile.Level, 7))]++;
                stats.FoliageTiles += tile.Foliage != nullptr;
            }
            stats.Loading += tile.Requested >= 0;
            stats.DirtyTiles += tile.Dirty;
            stats.ResidentBytes += tile.Bytes;
            stats.InFlightBytes += tile.RequestedBytes;
        }
        stats.BudgetBytes = budget;
        stats.PeakResidentBytes = std::max(stats.PeakResidentBytes, stats.ResidentBytes);
        stats.PeakLoadsPerFrame = std::max(stats.PeakLoadsPerFrame, stats.LoadsThisFrame);
        stats.UpdateMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return stats;
    }

    std::vector<TerrainStreaming::DrawTile> TerrainStreaming::GetDrawTiles(const TiledTerrainComponent& c, const glm::vec3& cameraLocal, float foliageDistance)
    {
        std::vector<DrawTile> tiles;
        if (!c.Streamer) return tiles;
        const Runtime& rt = *c.Streamer;
        for (uint32_t i = 0; i < rt.Tiles.size(); i++)
        {
            const Runtime::Tile& tile = rt.Tiles[i];
            if (!tile.Terrain) continue;
            DrawTile d;
            d.X = i % rt.Set.TilesX;
            d.Z = i / rt.Set.TilesX;
            d.Terrain = tile.Terrain.get();
            d.Level = tile.Level;
            d.Local = glm::translate(glm::mat4(1.0f), { float(d.X) * rt.Set.TileSize, 0.0f, float(d.Z) * rt.Set.TileSize });
            if (tile.Foliage && TileDistance(rt.Set, d.X, d.Z, { cameraLocal.x, cameraLocal.z }) <= foliageDistance) d.Foliage = tile.Foliage.get();
            tiles.push_back(d);
        }
        return tiles;
    }

    void TerrainStreaming::Close(TiledTerrainComponent& c)
    {
        if (!c.Streamer) return;
        for (Runtime::Tile& tile : c.Streamer->Tiles) Cancel(tile);
        c.Streamer.reset();
    }

    void TerrainStreaming::WaitIdle(TiledTerrainComponent&) { TileReader::Get().WaitIdle(); }

    // ------------------------------------------------------------------ editing
    bool TerrainStreaming::Raycast(const TiledTerrainComponent& c, const glm::vec3& origin, const glm::vec3& direction, TerrainHit& hit)
    {
        if (!c.Streamer) return false;
        const Runtime& rt = *c.Streamer;
        bool found = false;
        for (uint32_t i = 0; i < rt.Tiles.size(); i++)
        {
            const Runtime::Tile& tile = rt.Tiles[i];
            if (!tile.Terrain) continue;
            const glm::vec3 offset(float(i % rt.Set.TilesX) * rt.Set.TileSize, 0, float(i / rt.Set.TilesX) * rt.Set.TileSize);
            TerrainHit h;
            if (TerrainTools::Raycast(*tile.Terrain, origin - offset, direction, h) && (!found || h.Distance < hit.Distance))
            {
                hit = h;
                hit.Position += offset;
                found = true;
            }
        }
        return found;
    }

    float TerrainStreaming::SampleHeight(const TiledTerrainComponent& c, glm::vec2 p, bool* resident)
    {
        if (resident) *resident = false;
        if (!c.Streamer || c.Streamer->Tiles.empty()) return 0.0f;
        const Runtime& rt = *c.Streamer;
        const int x = std::clamp(int(std::floor(p.x / rt.Set.TileSize)), 0, int(rt.Set.TilesX) - 1);
        const int z = std::clamp(int(std::floor(p.y / rt.Set.TileSize)), 0, int(rt.Set.TilesZ) - 1);
        const Runtime::Tile& tile = rt.Tiles[size_t(z) * rt.Set.TilesX + x];
        if (!tile.Terrain) return 0.0f;
        if (resident) *resident = true;
        return TerrainTools::SampleHeight(*tile.Terrain, p.x - float(x) * rt.Set.TileSize, p.y - float(z) * rt.Set.TileSize);
    }

    void TerrainStreaming::BeginStroke(TiledTerrainComponent& c)
    {
        if (!c.Streamer) return;
        c.Streamer->InStroke = true;
        c.Streamer->UndoStack.emplace_back();
    }

    void TerrainStreaming::EndStroke(TiledTerrainComponent& c)
    {
        if (!c.Streamer) return;
        Runtime& rt = *c.Streamer;
        rt.InStroke = false;
        if (!rt.UndoStack.empty() && rt.UndoStack.back().empty()) rt.UndoStack.pop_back();
        else if (!rt.UndoStack.empty()) rt.RedoStack.clear();
        while (rt.UndoStack.size() > 32) rt.UndoStack.erase(rt.UndoStack.begin());
    }

    static std::vector<TerrainComponent*> ReconcileEdges(Runtime& rt, const std::vector<uint32_t>& touched)
    {
        const uint32_t nx = rt.Set.TilesX, nz = rt.Set.TilesZ, r = rt.Set.TileResolution;
        auto full = [&](int x, int z) -> TerrainComponent* {
            if (x < 0 || z < 0 || x >= int(nx) || z >= int(nz)) return nullptr;
            const Runtime::Tile& t = rt.Tiles[size_t(z) * nx + x];
            return t.Terrain && t.Level == 0 ? t.Terrain.get() : nullptr;
        };
        std::vector<TerrainComponent*> changed;
        auto mark = [&](TerrainComponent* t) {
            if (std::find(changed.begin(), changed.end(), t) == changed.end()) changed.push_back(t);
        };
        for (uint32_t index : touched)
        {
            const int x = int(index % nx), z = int(index / nx);
            TerrainComponent* self = full(x, z);
            if (!self) continue;
            // +X / -X and +Z / -Z neighbours: the two copies of every shared sample get their mean.
            const std::pair<int, int> sides[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (auto [dx, dz] : sides)
            {
                TerrainComponent* other = full(x + dx, z + dz);
                if (!other) continue;
                for (uint32_t k = 0; k < r; k++)
                {
                    const size_t a = dx ? size_t(k) * r + (dx > 0 ? r - 1 : 0) : size_t(dz > 0 ? r - 1 : 0) * r + k;
                    const size_t b = dx ? size_t(k) * r + (dx > 0 ? 0 : r - 1) : size_t(dz > 0 ? 0 : r - 1) * r + k;
                    if (self->Heights[a] != other->Heights[b] || self->Weights[a] != other->Weights[b])
                    {
                        const float h = (self->Heights[a] + other->Heights[b]) * 0.5f;
                        const glm::vec4 w = (self->Weights[a] + other->Weights[b]) * 0.5f;
                        self->Heights[a] = other->Heights[b] = h;
                        self->Weights[a] = other->Weights[b] = w;
                        mark(self);
                        mark(other);
                    }
                }
            }
        }
        // Corners are shared by up to four tiles: one mean for all of them.
        for (uint32_t index : touched)
        {
            const int x = int(index % nx), z = int(index / nx);
            for (int cz = z; cz <= z + 1; cz++)
                for (int cx = x; cx <= x + 1; cx++)
                {
                    std::vector<std::pair<TerrainComponent*, size_t>> copies;
                    for (int tz = cz - 1; tz <= cz; tz++)
                        for (int tx = cx - 1; tx <= cx; tx++)
                            if (TerrainComponent* t = full(tx, tz)) copies.push_back({ t, size_t(tz == cz ? 0 : r - 1) * r + (tx == cx ? 0 : r - 1) });
                    if (copies.size() < 2) continue;
                    float h = 0;
                    glm::vec4 w(0);
                    for (auto& [t, i] : copies)
                    {
                        h += t->Heights[i];
                        w += t->Weights[i];
                    }
                    h /= float(copies.size());
                    w /= float(copies.size());
                    for (auto& [t, i] : copies)
                        if (t->Heights[i] != h || t->Weights[i] != w)
                        {
                            t->Heights[i] = h;
                            t->Weights[i] = w;
                            mark(t);
                        }
                }
        }
        for (TerrainComponent* t : changed)
        {
            t->GeometryChanged();
            t->PaintChanged();
        }
        return changed;
    }

    bool TerrainStreaming::ApplyBrush(TiledTerrainComponent& c, glm::vec2 center, const TerrainBrushSettings& brush, float seconds)
    {
        if (!c.Streamer || c.Streamer->Tiles.empty() || !std::isfinite(brush.Radius)) return false;
        Runtime& rt = *c.Streamer;
        const TerrainTileSet& set = rt.Set;
        const float radius = std::clamp(brush.Radius, 0.05f, 4096.0f);
        const int x0 = std::clamp(int(std::floor((center.x - radius) / set.TileSize)), 0, int(set.TilesX) - 1);
        const int x1 = std::clamp(int(std::floor((center.x + radius) / set.TileSize)), 0, int(set.TilesX) - 1);
        const int z0 = std::clamp(int(std::floor((center.y - radius) / set.TileSize)), 0, int(set.TilesZ) - 1);
        const int z1 = std::clamp(int(std::floor((center.y + radius) / set.TileSize)), 0, int(set.TilesZ) - 1);
        if (center.x + radius < 0 || center.y + radius < 0 || center.x - radius > set.TileSize * float(set.TilesX) || center.y - radius > set.TileSize * float(set.TilesZ))
            return false;
        std::vector<uint32_t> touched;
        bool changed = false;
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
            {
                const uint32_t index = uint32_t(z) * set.TilesX + uint32_t(x);
                Runtime::Tile& tile = rt.Tiles[index];
                // Edits need full detail: a tile that is not resident at level 0 is read now (with its foliage).
                if (tile.Level != 0 && !ReadNow(rt, index, 0, true)) continue;
                if (rt.InStroke && !rt.UndoStack.empty())
                {
                    auto& entry = rt.UndoStack.back();
                    if (std::none_of(entry.begin(), entry.end(), [&](const Runtime::UndoTile& u) { return u.Index == index; }))
                        entry.push_back({ index, tile.Terrain->Heights, tile.Terrain->Weights });
                }
                const glm::vec2 local = center - glm::vec2(float(x), float(z)) * set.TileSize;
                if (TerrainTools::ApplyBrush(*tile.Terrain, local, brush, seconds))
                {
                    changed = true;
                    tile.Dirty = true;
                    touched.push_back(index);
                    // The foliage keeps standing on the ground.
                    if (tile.Foliage && brush.Tool != TerrainBrush::Paint)
                        for (FoliageInstance& f : tile.Foliage->Instances)
                            if (glm::length(glm::vec2(f.Position.x, f.Position.z) - local) <= radius + 1.0f)
                                f.Position.y = TerrainTools::SampleHeight(*tile.Terrain, f.Position.x, f.Position.z);
                    if (tile.Foliage) tile.Foliage->Runtime.reset();
                }
            }
        if (!changed) return false;
        // Shared samples equal again; a neighbour whose copy moved is unsaved too.
        for (TerrainComponent* moved : ReconcileEdges(rt, touched))
            for (Runtime::Tile& tile : rt.Tiles)
                if (tile.Terrain.get() == moved) tile.Dirty = true;
        UpdateEdges(rt);
        return true;
    }

    static bool RestoreStroke(TiledTerrainComponent& c, std::vector<Runtime::UndoTile>& entry, std::vector<Runtime::UndoTile>& inverse)
    {
        Runtime& rt = *c.Streamer;
        inverse.clear();
        for (Runtime::UndoTile& u : entry)
        {
            Runtime::Tile& tile = rt.Tiles[u.Index];
            if (tile.Level != 0 && !ReadNow(rt, u.Index, 0, true)) return false;
            inverse.push_back({ u.Index, tile.Terrain->Heights, tile.Terrain->Weights });
            tile.Terrain->Heights = u.Heights;
            tile.Terrain->Weights = u.Weights;
            tile.Terrain->GeometryChanged();
            tile.Terrain->PaintChanged();
            tile.Dirty = true;
        }
        UpdateEdges(rt);
        return true;
    }

    bool TerrainStreaming::Undo(TiledTerrainComponent& c)
    {
        if (!c.Streamer || c.Streamer->UndoStack.empty()) return false;
        Runtime& rt = *c.Streamer;
        std::vector<Runtime::UndoTile> inverse;
        if (!RestoreStroke(c, rt.UndoStack.back(), inverse)) return false;
        rt.UndoStack.pop_back();
        rt.RedoStack.push_back(std::move(inverse));
        return true;
    }

    bool TerrainStreaming::Redo(TiledTerrainComponent& c)
    {
        if (!c.Streamer || c.Streamer->RedoStack.empty()) return false;
        Runtime& rt = *c.Streamer;
        std::vector<Runtime::UndoTile> inverse;
        if (!RestoreStroke(c, rt.RedoStack.back(), inverse)) return false;
        rt.RedoStack.pop_back();
        rt.UndoStack.push_back(std::move(inverse));
        return true;
    }

    size_t TerrainStreaming::SaveDirty(TiledTerrainComponent& c, std::string* error)
    {
        if (!c.Streamer) return 0;
        Runtime& rt = *c.Streamer;
        size_t written = 0;
        // A clean neighbour (or a diagonal one sharing a corner) may have been read back from disk with the edge from
        // before the edit: it takes the edited tile's shared samples and is written too, so the files always agree.
        const int nx = int(rt.Set.TilesX), nz = int(rt.Set.TilesZ), r = int(rt.Set.TileResolution);
        std::vector<uint32_t> dirty;
        for (uint32_t i = 0; i < rt.Tiles.size(); i++)
            if (rt.Tiles[i].Dirty && rt.Tiles[i].Terrain && rt.Tiles[i].Level == 0) dirty.push_back(i);
        for (uint32_t d : dirty)
        {
            const int x = int(d) % nx, z = int(d) / nx;
            const TerrainComponent& source = *rt.Tiles[d].Terrain;
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    const int tx = x + dx, tz = z + dz;
                    if ((!dx && !dz) || tx < 0 || tz < 0 || tx >= nx || tz >= nz) continue;
                    const uint32_t n = uint32_t(tz * nx + tx);
                    Runtime::Tile& neighbour = rt.Tiles[n];
                    if (neighbour.Dirty) continue;
                    if (neighbour.Level != 0 && !ReadNow(rt, n, 0, true)) continue;
                    TerrainComponent& target = *neighbour.Terrain;
                    bool moved = false;
                    for (int j = 0; j < r; j++)
                        for (int i = 0; i < r; i++)
                        {
                            if (i != 0 && i != r - 1 && j != 0 && j != r - 1) continue;
                            const int si = i + dx * (r - 1), sj = j + dz * (r - 1);
                            if (si < 0 || sj < 0 || si >= r || sj >= r) continue;
                            const size_t to = size_t(j) * r + i, from = size_t(sj) * r + si;
                            if (target.Heights[to] != source.Heights[from] || target.Weights[to] != source.Weights[from])
                            {
                                target.Heights[to] = source.Heights[from];
                                target.Weights[to] = source.Weights[from];
                                moved = true;
                            }
                        }
                    if (moved)
                    {
                        target.GeometryChanged();
                        target.PaintChanged();
                        neighbour.Dirty = true;
                    }
                }
        }
        for (uint32_t i = 0; i < rt.Tiles.size(); i++)
        {
            Runtime::Tile& tile = rt.Tiles[i];
            if (!tile.Dirty || !tile.Terrain || tile.Level != 0) continue;
            TerrainTileData data;
            data.Resolution = tile.Terrain->Resolution;
            data.Heights = tile.Terrain->Heights;
            data.Weights = tile.Terrain->Weights;
            if (tile.Foliage) data.Foliage = tile.Foliage->Instances;
            if (!TerrainTileIO::Write(rt.Set.TileFile(rt.File, i % rt.Set.TilesX, i / rt.Set.TilesX), data, rt.Set.UsableLevels(), error)) return written;
            rt.Set.FoliageCounts[i] = uint32_t(data.Foliage.size());
            tile.Dirty = false;
            written++;
        }
        if (written && !rt.Set.Save(rt.File) && error) *error = "could not write " + rt.File.string();
        return written;
    }

    size_t TerrainStreaming::DirtyCount(const TiledTerrainComponent& c)
    {
        if (!c.Streamer) return 0;
        return size_t(std::count_if(c.Streamer->Tiles.begin(), c.Streamer->Tiles.end(), [](const Runtime::Tile& t) { return t.Dirty; }));
    }

    float TerrainStreaming::MaxSharedEdgeError(const TiledTerrainComponent& c)
    {
        if (!c.Streamer) return 0.0f;
        const Runtime& rt = *c.Streamer;
        const uint32_t nx = rt.Set.TilesX, nz = rt.Set.TilesZ;
        float worst = 0.0f;
        for (uint32_t z = 0; z < nz; z++)
            for (uint32_t x = 0; x < nx; x++)
            {
                const Runtime::Tile& a = rt.Tiles[z * nx + x];
                if (!a.Terrain) continue;
                for (auto [dx, dz] : { std::pair{ 1u, 0u }, std::pair{ 0u, 1u } })
                {
                    if (x + dx >= nx || z + dz >= nz) continue;
                    const Runtime::Tile& b = rt.Tiles[(z + dz) * nx + x + dx];
                    if (!b.Terrain) continue;
                    // Compared at the coarser tile's samples (the ones both have).
                    const int coarse = std::max(a.Level, b.Level);
                    const uint32_t sa = 1u << (coarse - a.Level), sb = 1u << (coarse - b.Level), ra = a.Terrain->Resolution, rb = b.Terrain->Resolution;
                    const uint32_t samples = (ra - 1) / sa + 1;
                    for (uint32_t k = 0; k < samples; k++)
                    {
                        const float ha = dx ? a.Terrain->Heights[size_t(k * sa) * ra + ra - 1] : a.Terrain->Heights[size_t(ra - 1) * ra + k * sa];
                        const float hb = dx ? b.Terrain->Heights[size_t(k * sb) * rb] : b.Terrain->Heights[k * sb];
                        worst = std::max(worst, std::abs(ha - hb));
                    }
                }
            }
        return worst;
    }

}
