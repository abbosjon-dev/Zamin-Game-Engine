#include "Zamin/Scene/WorldManifest.h"
#include "Zamin/Utils/FileSystem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace Zamin {
    namespace {
        using json = nlohmann::json;
        bool ScenePathSafe(std::string_view path)
        {
            if (path.empty() || path.size() > 1024 || path.front() == '/' || path.find_first_of("\\:\0", 0, 3) != path.npos) return false;
            const std::filesystem::path file{ std::string(path) };
            if (file.is_absolute() || file.extension() != ".zscene") return false;
            for (const auto& part : file)
                if (part == "." || part == "..") return false;
            return true;
        }
        uint64_t ReadUnsigned(const json& j)
        {
            if (!j.is_number_unsigned()) throw std::runtime_error("Expected an unsigned integer");
            return j.get<uint64_t>();
        }
        UUID ReadID(const json& j)
        {
            if (!j.is_string()) throw std::runtime_error("Cell UUID must be a hex string");
            const auto& s = j.get_ref<const std::string&>();
            auto id = UUID::FromString(s);
            if (s.size() != 16 || !id || !std::all_of(s.begin(), s.end(), [](unsigned char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
            })) throw std::runtime_error("Invalid cell UUID");
            return *id;
        }
        glm::dvec3 ReadVector(const json& j)
        {
            if (!j.is_array() || j.size() != 3) throw std::runtime_error("Expected three vector values");
            glm::dvec3 v;
            for (int i = 0; i < 3; ++i)
            {
                if (!j[i].is_number()) throw std::runtime_error("Expected a numeric vector");
                v[i] = j[i].get<double>();
            }
            return v;
        }
    }

    bool WorldManifest::DependencyOrder(std::vector<UUID>& output, std::string& error) const
    {
        error.clear();
        if (Cells.size() > MaxCells)
        {
            error = "World cell limit exceeded";
            return false;
        }
        std::unordered_map<uint64_t, size_t> indices;
        std::set<std::tuple<int, int, int, std::string>> locations;
        for (size_t i = 0; i < Cells.size(); ++i)
        {
            const auto& c = Cells[i];
            if (!c.ID.IsValid() || !indices.emplace(c.ID, i).second)
            {
                error = "Missing or duplicate cell UUID";
                return false;
            }
            if (c.Layer.empty() || c.Layer.size() > 64 || !std::all_of(c.Layer.begin(), c.Layer.end(), [](unsigned char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
            }))
            {
                error = "Invalid cell layer";
                return false;
            }
            for (int axis = 0; axis < 3; ++axis)
                if (c.Coordinate[axis] < -1000000 || c.Coordinate[axis] > 1000000)
                {
                    error = "Cell coordinate outside supported range";
                    return false;
                }
            if (!locations.emplace(c.Coordinate.x, c.Coordinate.y, c.Coordinate.z, c.Layer).second)
            {
                error = "Duplicate cell coordinate in one layer";
                return false;
            }
            if (!ScenePathSafe(c.ScenePath))
            {
                error = "Unsafe cell scene path";
                return false;
            }
            if (c.DeclaredCPUBytes > MaxDeclaredBytesPerCell || c.DeclaredGPUBytes > MaxDeclaredBytesPerCell)
            {
                error = "Cell memory estimate exceeds limit";
                return false;
            }
            if (c.Dependencies.size() > MaxDependencies)
            {
                error = "Cell dependency limit exceeded";
                return false;
            }
        }
        std::vector<size_t> degree(Cells.size());
        std::vector<std::vector<size_t>> dependants(Cells.size());
        for (size_t i = 0; i < Cells.size(); ++i)
        {
            std::set<uint64_t> unique;
            for (auto dependency : Cells[i].Dependencies)
            {
                auto found = indices.find(dependency);
                if (found == indices.end() || found->second == i || !unique.insert(dependency).second)
                {
                    error = "Missing, self or duplicate cell dependency";
                    return false;
                }
                ++degree[i];
                dependants[found->second].push_back(i);
            }
        }
        std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
        for (size_t i = 0; i < degree.size(); ++i)
            if (!degree[i]) ready.push(i);
        std::vector<UUID> result;
        while (!ready.empty())
        {
            auto i = ready.top();
            ready.pop();
            result.push_back(Cells[i].ID);
            for (auto child : dependants[i])
                if (!--degree[child]) ready.push(child);
        }
        if (result.size() != Cells.size())
        {
            error = "World cell dependency cycle";
            return false;
        }
        output = std::move(result);
        return true;
    }

    bool WorldManifest::Validate(std::string& error) const
    {
        error.clear();
        if (Name.empty() || Name.size() > 128)
        {
            error = "Invalid world name";
            return false;
        }
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(Origin[i]) || std::abs(Origin[i]) > 1e9 || !std::isfinite(CellSize[i]) || CellSize[i] < .001 || CellSize[i] > 1e6)
            {
                error = "Invalid world origin or cell size";
                return false;
            }
        std::vector<UUID> order;
        return DependencyOrder(order, error);
    }

    bool WorldManifest::Serialize(std::string& output, std::string& error) const
    {
        if (!Validate(error)) return false;
        try
        {
            json j{ { "Format", "ZaminWorld" }, { "Version", Version }, { "Name", Name }, { "Origin", { Origin.x, Origin.y, Origin.z } }, { "CellSize", { CellSize.x, CellSize.y, CellSize.z } }, { "Cells", json::array() } };
            for (const auto& c : Cells)
            {
                json dependencies = json::array();
                for (auto id : c.Dependencies) dependencies.push_back(id.ToString());
                j["Cells"].push_back({ { "ID", c.ID.ToString() }, { "Coordinate", { c.Coordinate.x, c.Coordinate.y, c.Coordinate.z } }, { "Layer", c.Layer }, { "Scene", c.ScenePath }, { "CPUBytes", c.DeclaredCPUBytes }, { "GPUBytes", c.DeclaredGPUBytes }, { "Dependencies", dependencies } });
            }
            auto text = j.dump(2);
            if (text.size() > MaxFileBytes) throw std::runtime_error("World manifest byte limit exceeded");
            output = std::move(text);
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    bool WorldManifest::Deserialize(std::string_view text, std::string& error)
    {
        error.clear();
        if (text.size() > MaxFileBytes)
        {
            error = "World manifest byte limit exceeded";
            return false;
        }
        try
        {
            std::vector<std::set<std::string>> keys;
            auto j = json::parse(text, [&keys](int depth, json::parse_event_t event, json& value) {
                if (depth > 32) throw std::runtime_error("World manifest nesting limit exceeded");
                if (event == json::parse_event_t::object_start) keys.emplace_back();
                else if (event == json::parse_event_t::object_end) keys.pop_back();
                else if (event == json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
                    throw std::runtime_error("Duplicate world manifest key");
                return true;
            });
            if (!j.is_object() || j.at("Format") != "ZaminWorld" || ReadUnsigned(j.at("Version")) != Version)
                throw std::runtime_error("Unsupported world manifest format/version");
            WorldManifest candidate;
            candidate.Name = j.at("Name").get<std::string>();
            candidate.Origin = ReadVector(j.at("Origin"));
            candidate.CellSize = ReadVector(j.at("CellSize"));
            const auto& cells = j.at("Cells");
            if (!cells.is_array() || cells.size() > MaxCells) throw std::runtime_error("Invalid world cell list");
            for (const auto& c : cells)
            {
                WorldCell cell;
                cell.ID = ReadID(c.at("ID"));
                const auto& coord = c.at("Coordinate");
                if (!coord.is_array() || coord.size() != 3) throw std::runtime_error("Invalid cell coordinate");
                for (int i = 0; i < 3; ++i)
                {
                    if (!coord[i].is_number_integer() || coord[i].get<double>() < -1000000 || coord[i].get<double>() > 1000000)
                        throw std::runtime_error("Invalid cell coordinate value");
                    cell.Coordinate[i] = coord[i].get<int>();
                }
                cell.Layer = c.at("Layer").get<std::string>();
                cell.ScenePath = c.at("Scene").get<std::string>();
                cell.DeclaredCPUBytes = ReadUnsigned(c.at("CPUBytes"));
                cell.DeclaredGPUBytes = ReadUnsigned(c.at("GPUBytes"));
                const auto& deps = c.at("Dependencies");
                if (!deps.is_array() || deps.size() > MaxDependencies) throw std::runtime_error("Invalid cell dependency list");
                for (const auto& id : deps) cell.Dependencies.push_back(ReadID(id));
                candidate.Cells.push_back(std::move(cell));
            }
            if (!candidate.Validate(error)) return false;
            *this = std::move(candidate);
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    bool WorldManifest::Load(const std::filesystem::path& file, std::string& error)
    {
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        if (!in)
        {
            error = "Cannot open world manifest";
            return false;
        }
        auto length = in.tellg();
        if (length < 0 || uint64_t(length) > MaxFileBytes)
        {
            error = "World manifest byte limit exceeded";
            return false;
        }
        std::string text(size_t(length), '\0');
        in.seekg(0);
        if (!in.read(text.data(), std::streamsize(text.size())) || in.peek() != std::char_traits<char>::eof())
        {
            error = "World manifest changed or could not be read";
            return false;
        }
        return Deserialize(text, error);
    }

    bool WorldManifest::Save(const std::filesystem::path& file, std::string& error) const
    {
        std::string text;
        if (!Serialize(text, error)) return false;
        if (!FileSystem::WriteTextFileAtomic(file, text))
        {
            error = "Cannot save world manifest";
            return false;
        }
        return true;
    }
    std::pair<glm::dvec3, glm::dvec3> WorldManifest::Bounds(const WorldCell& cell) const
    {
        const auto min = Origin + glm::dvec3(cell.Coordinate) * CellSize;
        return { min, min + CellSize };
    }
    std::pair<uint64_t, uint64_t> WorldManifest::DeclaredMemory() const
    {
        uint64_t cpu = 0, gpu = 0;
        const auto add = [](uint64_t a, uint64_t b) { return a + std::min(b, std::numeric_limits<uint64_t>::max() - a); };
        for (const auto& c : Cells)
        {
            cpu = add(cpu, c.DeclaredCPUBytes);
            gpu = add(gpu, c.DeclaredGPUBytes);
        }
        return { cpu, gpu };
    }
    std::filesystem::path WorldManifest::ResolveScene(const std::filesystem::path& assets, const WorldCell& cell)
    {
        std::error_code ec;
        if (!ScenePathSafe(cell.ScenePath) || assets.empty() || !std::filesystem::is_directory(assets, ec)) return {};
        auto path = FileSystem::SafeJoin(assets, cell.ScenePath);
        return !path.empty() && FileSystem::IsSubPathOnDisk(path, assets, false) ? path : std::filesystem::path{};
    }
}
