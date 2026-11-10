#include <Zamin/Scene/WorldManifest.h>
#include <Zamin/Utils/FileSystem.h>
#include <nlohmann/json.hpp>
#include "TestAssertions.h"
#include <algorithm>
#include <future>
#include <limits>

using namespace Zamin;
namespace {
    WorldManifest MakeWorld()
    {
        WorldManifest m;
        m.Name = "Shared 2D and 3D world";
        m.Origin = { 100, 20, -50 };
        m.CellSize = { 64, 32, 64 };
        m.Cells = {
            { UUID(3), { -1, 0, 0 }, "main", "Scenes/West.zscene", 100, 200, { UUID(1) } },
            { UUID(1), { 0, 0, 0 }, "main", "Scenes/Center.zscene", 200, 300, {} },
            { UUID(2), { 0, 1, 0 }, "main", "Scenes/Upper2D.zscene", 300, 400, {} },
            { UUID(4), { 0, 0, 1 }, "detail", "Scenes/North3D.zscene", 400, 500, { UUID(3), UUID(2) } }
        };
        return m;
    }
}

TEST(WorldManifestCanonicalRoundtripBoundsAndOrder)
{
    auto m = MakeWorld();
    std::string text, error;
    CHECK(m.Serialize(text, error));
    WorldManifest copy;
    CHECK(copy.Deserialize(text, error));
    CHECK(copy.Cells == m.Cells && copy.Origin == m.Origin && copy.CellSize == m.CellSize);
    std::string again;
    CHECK(copy.Serialize(again, error) && again == text);
    std::vector<UUID> order;
    CHECK(copy.DependencyOrder(order, error));
    CHECK(order == std::vector<UUID>({ UUID(1), UUID(3), UUID(2), UUID(4) }));
    auto [min, max] = copy.Bounds(copy.Cells.front());
    CHECK(min == glm::dvec3(36, 20, -50) && max == glm::dvec3(100, 52, 14));
    auto [cpu, gpu] = copy.DeclaredMemory();
    CHECK(cpu == 1000 && gpu == 1400);
}

TEST(WorldManifestRejectsInvalidDependenciesAndLayers)
{
    auto check = [](WorldManifest m) { std::string error; CHECK(!m.Validate(error) && !error.empty()); };
    auto m = MakeWorld();
    m.Cells[0].Dependencies = { UUID(999) };
    check(m);
    m = MakeWorld();
    m.Cells[0].Dependencies = { UUID(3) };
    check(m);
    m = MakeWorld();
    m.Cells[0].Dependencies = { UUID(1), UUID(1) };
    check(m);
    m = MakeWorld();
    m.Cells[1].Dependencies = { UUID(4) };
    check(m);
    m = MakeWorld();
    m.Cells[1].ID = m.Cells[0].ID;
    check(m);
    m = MakeWorld();
    m.Cells[1].ID = UUID(0);
    check(m);
    m = MakeWorld();
    m.Cells[1].Coordinate = m.Cells[0].Coordinate;
    check(m);
    m.Cells[1].Layer = "other";
    std::string error;
    CHECK(m.Validate(error));
    m.Cells[1].Layer = "../outside";
    check(m);
    std::vector<UUID> preserved{ UUID(555) };
    CHECK(!m.DependencyOrder(preserved, error));
    CHECK(preserved == std::vector<UUID>({ UUID(555) }));
}

TEST(WorldManifestUntrustedInputIsBoundedAndTransactional)
{
    using json = nlohmann::json;
    auto m = MakeWorld();
    std::string text, error, unchanged;
    CHECK(m.Serialize(text, error));
    auto original = json::parse(text);
    auto reject = [&](json j) {
        CHECK(!m.Deserialize(j.dump(), error) && !error.empty());
        CHECK(m.Serialize(unchanged, error) && unchanged == text);
    };
    auto invalid = MakeWorld();
    invalid.Origin.x = std::numeric_limits<double>::quiet_NaN();
    CHECK(!invalid.Serialize(unchanged, error));
    invalid = MakeWorld();
    invalid.CellSize.z = 0;
    CHECK(!invalid.Validate(error));
    invalid = MakeWorld();
    invalid.Cells[0].DeclaredGPUBytes = WorldManifest::MaxDeclaredBytesPerCell + 1;
    CHECK(!invalid.Validate(error));
    auto j = original;
    j["Version"] = 2;
    reject(j);
    j = original;
    j["Cells"][0]["ID"] = "0x00000000000003";
    reject(j);
    j = original;
    j["Cells"][0]["CPUBytes"] = -1;
    reject(j);
    j = original;
    j["Cells"][0]["GPUBytes"] = 1.5;
    reject(j);
    j = original;
    j["Cells"][0]["Coordinate"][0] = 2.5;
    reject(j);
    j = original;
    j["Cells"][0]["Coordinate"][0] = 1000001;
    reject(j);
    for (auto path : { "../escape.zscene", "/tmp/escape.zscene", "Scenes/../escape.zscene", "C:/escape.zscene", "a\\b.zscene", "Scenes/a.obj" })
    {
        j = original;
        j["Cells"][0]["Scene"] = path;
        reject(j);
    }
    j = original;
    j["Cells"][0]["Dependencies"] = json::array();
    for (size_t i = 0; i <= WorldManifest::MaxDependencies; ++i) j["Cells"][0]["Dependencies"].push_back("0000000000000001");
    reject(j);
    j = original;
    j["Cells"] = json::array();
    for (size_t i = 0; i <= WorldManifest::MaxCells; ++i) j["Cells"].push_back(original["Cells"][0]);
    reject(j);
    CHECK(!m.Deserialize(std::string(WorldManifest::MaxFileBytes + 1, ' '), error));
    CHECK(!m.Deserialize("{\"Version\":1,\"Version\":1}", error));
    CHECK(!m.Deserialize(std::string(40, '[') + "0" + std::string(40, ']'), error));
    CHECK(m.Serialize(unchanged, error) && unchanged == text);
}

TEST(WorldManifestAtomicFilesSafeResolutionAndWorkerParse)
{
    TempDirectory temp("zamin-world-manifest-");
    if (!temp)
    {
        CHECK(false);
        return;
    }
    const auto root = temp.GetPath();
    auto m = MakeWorld();
    std::string text, error;
    CHECK(m.Save(root / "world.zworld", error));
    WorldManifest loaded;
    CHECK(loaded.Load(root / "world.zworld", error) && loaded.Cells == m.Cells);
    CHECK(loaded.Serialize(text, error));
    auto worker = std::async(std::launch::async, [text] { WorldManifest w; std::string e; return w.Deserialize(text, e) && w.Cells.size() == 4; });
    CHECK(worker.get());
    CHECK(FileSystem::WriteTextFile(root / "bad.zworld", "{truncated"));
    CHECK(!loaded.Load(root / "bad.zworld", error) && loaded.Cells == m.Cells);
    CHECK(FileSystem::WriteTextFile(root / "large.zworld", std::string(WorldManifest::MaxFileBytes + 1, ' ')));
    CHECK(!loaded.Load(root / "large.zworld", error) && loaded.Cells == m.Cells);
    CHECK(!loaded.Load(root / "missing.zworld", error) && loaded.Cells == m.Cells);
    auto scene = WorldManifest::ResolveScene(root, m.Cells[0]);
    CHECK(!scene.empty()); // Missing scene is an IO error for the future loader, not a bad manifest.
    std::filesystem::create_directory(root / "Scenes");
    TempDirectory external("zamin-world-external-");
    if (!external)
    {
        CHECK(false);
        return;
    }
    std::filesystem::create_directory_symlink(external.GetPath(), root / "Escape");
    auto escaped = m.Cells[0];
    escaped.ScenePath = "Escape/Outside.zscene";
    CHECK(WorldManifest::ResolveScene(root, escaped).empty());
    CHECK(WorldManifest::ResolveScene({}, m.Cells[0]).empty());
    m.CellSize.x = std::numeric_limits<double>::infinity();
    CHECK(!m.Save(root / "world.zworld", error));
    CHECK(loaded.Load(root / "world.zworld", error) && loaded.Cells.size() == 4);
    m = MakeWorld();
    m.Cells[0].DeclaredCPUBytes = std::numeric_limits<uint64_t>::max();
    CHECK(m.DeclaredMemory().first == std::numeric_limits<uint64_t>::max());
}


TEST(WorldManifestCapacityAndDeepDependencyChain)
{
    WorldManifest m;
    for (size_t i = 0; i < WorldManifest::MaxCells; ++i)
    {
        WorldCell c;
        c.ID = UUID(i + 1);
        c.Coordinate = { int(i), 0, 0 };
        c.ScenePath = "Scenes/Cell.zscene";
        c.DeclaredCPUBytes = c.DeclaredGPUBytes = WorldManifest::MaxDeclaredBytesPerCell;
        if (i) c.Dependencies.push_back(UUID(i));
        m.Cells.push_back(std::move(c));
    }
    // Reverse storage order: topological traversal must handle the full chain without recursion.
    std::reverse(m.Cells.begin(), m.Cells.end());
    std::string text, error;
    CHECK(m.Serialize(text, error));
    WorldManifest copy;
    CHECK(copy.Deserialize(text, error));
    std::vector<UUID> order;
    CHECK(copy.DependencyOrder(order, error));
    CHECK(order.size() == WorldManifest::MaxCells);
    for (size_t i = 0; i < order.size(); ++i) CHECK(order[i] == UUID(i + 1));
    const auto bytes = WorldManifest::MaxCells * WorldManifest::MaxDeclaredBytesPerCell;
    CHECK(copy.DeclaredMemory() == std::make_pair(bytes, bytes));
}
