// ZaminBench --nav: how long navigation takes, headless. A 256 x 256 grid with rooms and walls
// (random, the same every run) and 200 agents:
//
//   1. the bakes: the NavGrid 2D from a 256 x 256 tilemap, the NavGrid 3D (256 x 256 cells) from
//      static 3D boxes - what loading a level costs (the grids are baked when the game starts);
//   2. the searches alone: a path (A* + string pulling) for every agent at once - on one thread, then
//      batched over the job system's workers;
//   3. the scene: 200 Nav Agents sent across the map at once (the first frame finds 200 paths), then
//      walking (new destinations when they arrive, a new path every RepathInterval) - ms / frame,
//      on one thread and with the job system.
//
//   ZaminBench --nav [--agents N] [--grid N] [--frames N] [--jobs N]
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

using namespace Zamin;

namespace {

    double Milliseconds(std::chrono::steady_clock::time_point since)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
    }

    // Rooms: the map split by walls every 32 cells with two doors each, plus random blocks inside.
    // `solid(x, y)` gets every blocked cell.
    template<typename F>
    void MakeLevel(int size, uint64_t seed, F&& solid)
    {
        RandomGenerator random(seed);
        const int room = 32;
        for (int line = room; line < size; line += room)
            for (int i = 0; i < size; i++)
            {
                const int inRoom = i % room;
                // Two doors per wall segment (3 cells wide).
                if ((inRoom >= 6 && inRoom < 9) || (inRoom >= 22 && inRoom < 25))
                    continue;
                solid(line, i);
                solid(i, line);
            }
        const int blocks = size * size / 90;
        for (int b = 0; b < blocks; b++)
        {
            const int w = random.Range(1, 5), h = random.Range(1, 5);
            const int x = random.Range(0, size - w), y = random.Range(0, size - h);
            for (int yy = y; yy < y + h; yy++)
                for (int xx = x; xx < x + w; xx++)
                    solid(xx, yy);
        }
    }

    struct Pair
    {
        glm::vec2 From, To;
    };

    // Start / goal pairs on walkable cells far apart (at least a third of the map).
    std::vector<Pair> MakePairs(const NavGrid& grid, int count, uint64_t seed)
    {
        RandomGenerator random(seed);
        std::vector<Pair> pairs;
        auto walkablePoint = [&]() {
            for (int attempt = 0; attempt < 1000; attempt++)
            {
                const glm::ivec2 c(random.Range(0, grid.GetWidth() - 1), random.Range(0, grid.GetHeight() - 1));
                if (grid.IsWalkable(c))
                    return grid.CellToWorld(c);
            }
            return grid.CellToWorld(grid.GetWidth() / 2, grid.GetHeight() / 2);
        };
        const float far = grid.GetSize().x / 3.0f;
        while ((int)pairs.size() < count)
        {
            Pair p{ walkablePoint(), walkablePoint() };
            if (glm::length(p.To - p.From) >= far)
                pairs.push_back(p);
        }
        return pairs;
    }

    struct SearchResult
    {
        double Milliseconds = 0.0;
        int Complete = 0;
        double Expanded = 0.0; // per search
        double Corners = 0.0;  // per path, after string pulling
    };

    // A path for every pair (A* + string pulling, as the agents do it), on one thread or over the
    // job system's workers (a pathfinder per thread).
    SearchResult RunSearches(const NavGrid& grid, const std::vector<Pair>& pairs, bool parallel, int rounds)
    {
        struct Out
        {
            NavPathStatus Status = NavPathStatus::None;
            int Expanded = 0;
            size_t Corners = 0;
        };
        std::vector<Out> out(pairs.size());
        auto search = [&](size_t i) {
            thread_local NavPathfinder finder;
            NavPathResult result;
            finder.FindPath(grid, pairs[i].From, pairs[i].To, result);
            std::vector<glm::vec2> points = NavPathSmoothing::ToPoints(grid, result.Cells, &pairs[i].From, &pairs[i].To);
            points = NavPathSmoothing::StringPull(grid, points);
            out[i] = { result.Status, result.Expanded, points.size() };
        };
        JobSystem::SetEnabled(parallel);
        std::vector<double> times;
        for (int r = 0; r < rounds; r++)
        {
            const auto clock = std::chrono::steady_clock::now();
            JobSystem::ParallelFor((uint32_t)pairs.size(), 1, [&](uint32_t begin, uint32_t end) {
                for (uint32_t i = begin; i < end; i++)
                    search(i);
            });
            times.push_back(Milliseconds(clock));
        }
        JobSystem::SetEnabled(true);
        SearchResult result;
        result.Milliseconds = FrameTimeStats::Compute(times).Median;
        for (const Out& o : out)
        {
            result.Complete += o.Status == NavPathStatus::Complete ? 1 : 0;
            result.Expanded += o.Expanded;
            result.Corners += (double)o.Corners;
        }
        result.Expanded /= (double)std::max<size_t>(1, out.size());
        result.Corners /= (double)std::max<size_t>(1, out.size());
        return result;
    }

    struct SceneResult
    {
        double BakeMilliseconds = 0.0;
        double FirstFrame = 0.0;       // 200 paths at once
        float FirstFramePaths = 0.0f;  // of it: the searches
        FrameTimeStats Frames;
        double PathsPerFrame = 0.0;
        int Arrivals = 0;
    };

    // The level as a tilemap with a NavGrid 2D, `agents` Nav Agents (moved by their transforms) sent
    // across it; `frames` frames of walking.
    SceneResult RunScene(int size, int agents, int frames, bool parallel)
    {
        JobSystem::SetEnabled(parallel);
        auto scene = CreateRef<Scene>();
        scene->GetSettings().Gravity = { 0.0f, 0.0f };
        Entity map = scene->CreateEntity("Xarita");
        map.GetTransform().Translation = { -(float)size * 0.5f, -(float)size * 0.5f, 0.0f };
        auto& tm = map.AddComponent<TilemapComponent>();
        tm.Width = tm.Height = size;
        tm.EnsureSize();
        MakeLevel(size, 7, [&](int x, int y) { tm.Set(x, y, 0); });
        Entity nav = scene->CreateEntity("Navigatsiya");
        auto& grid2D = nav.AddComponent<NavGrid2DComponent>();
        grid2D.CellSize = 1.0f;
        grid2D.AgentRadius = 0.3f;
        // Where the agents start and go (the grid is baked from the same level).
        NavGrid probe((int)size, (int)size, 1.0f, glm::vec2(-(float)size * 0.5f));
        MakeLevel(size, 7, [&](int x, int y) { probe.SetWalkable(x, y, false); });
        probe.Erode(0.3f);
        const std::vector<Pair> pairs = MakePairs(probe, agents, 99);
        std::vector<Entity> walkers;
        for (int i = 0; i < agents; i++)
        {
            Entity e = scene->CreateEntity(std::format("Agent {}", i));
            e.GetTransform().Translation = glm::vec3(pairs[(size_t)i].From, 0.0f);
            auto& a = e.AddComponent<NavAgentComponent>();
            a.Speed = 6.0f;
            a.RepathInterval = 0.5f;
            walkers.push_back(e);
        }

        SceneResult result;
        scene->OnRuntimeStart();
        result.BakeMilliseconds = grid2D.BakeMilliseconds;
        for (int i = 0; i < agents; i++)
            walkers[(size_t)i].GetComponent<NavAgentComponent>().SetDestination(glm::vec3(pairs[(size_t)i].To, 0.0f));
        const float dt = 1.0f / 60.0f;
        auto clock = std::chrono::steady_clock::now();
        scene->OnUpdateRuntime(dt);
        result.FirstFrame = Milliseconds(clock);
        result.FirstFramePaths = scene->GetNavFrameStats().PathMilliseconds;

        // Walking: arrived agents get the start of another pair as their next destination.
        std::vector<double> times;
        uint64_t paths = 0;
        for (int f = 0; f < frames; f++)
        {
            for (int i = 0; i < agents; i++)
            {
                auto& a = walkers[(size_t)i].GetComponent<NavAgentComponent>();
                if (a.Arrived || a.PathStatus == NavPathStatus::Failed)
                {
                    result.Arrivals++;
                    const Pair& next = pairs[(size_t)((i + f) % agents)];
                    a.SetDestination(glm::vec3(f % 2 ? next.From : next.To, 0.0f));
                }
            }
            clock = std::chrono::steady_clock::now();
            scene->OnUpdateRuntime(dt);
            times.push_back(Milliseconds(clock));
            paths += scene->GetNavFrameStats().PathRequests;
        }
        result.Frames = FrameTimeStats::Compute(times);
        result.PathsPerFrame = (double)paths / (double)std::max(1, frames);
        scene->OnRuntimeStop();
        JobSystem::SetEnabled(true);
        return result;
    }

    // The NavGrid 3D bake: `size` x `size` cells (0.5 units) over a floor with boxes, walls and steps.
    double Bake3D(int size, int& walkable)
    {
        auto scene = CreateRef<Scene>();
        const float extent = (float)size * 0.5f; // world units (0.5 per cell)
        auto box = [&](const glm::vec3& at, const glm::vec3& scale) {
            Entity e = scene->CreateEntity("Kub");
            e.GetTransform().Translation = at;
            e.GetTransform().Scale = scale;
            e.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
            e.AddComponent<BoxCollider3DComponent>();
        };
        box({ 0.0f, -0.5f, 0.0f }, { extent + 2.0f, 1.0f, extent + 2.0f });
        // The same rooms (16 cells = 8 units ... in world units: walls every 16 units).
        MakeLevel(size / 2, 7, [&](int x, int y) {
            box({ (float)x - extent * 0.5f + 0.5f, 1.0f, (float)y - extent * 0.5f + 0.5f }, { 1.0f, 2.0f, 1.0f });
        });
        Entity nav = scene->CreateEntity("Navigatsiya");
        auto& c = nav.AddComponent<NavGrid3DComponent>();
        c.Size = { extent, 6.0f, extent };
        c.Offset = { 0.0f, 2.0f, 0.0f };
        c.CellSize = 0.5f;
        scene->OnRuntimeStart();
        walkable = (int)c.Grid.CountWalkable();
        const double ms = c.BakeMilliseconds;
        scene->OnRuntimeStop();
        return ms;
    }

}

int RunNavBench(int agents, int size, int frames, int jobs)
{
    Log::SetMinLevel(LogLevel::Error);
    agents = std::clamp(agents, 1, 10000);
    size = std::clamp(size, 32, 2048);
    JobSystem::Init(jobs > 0 ? (uint32_t)jobs : JobSystem::DefaultWorkerCount());
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug (optimallashtirilmagan: Release'da bir necha barobar tez)";
#endif
    std::printf("Zamin navigatsiya benchmark: %dx%d grid, %d agent, %s, vazifalar tizimi: %u ishchi oqim\n\n", size, size, agents, build,
                JobSystem::GetWorkerCount());

    // ---- the searches alone
    NavGrid grid(size, size, 1.0f, glm::vec2(-(float)size * 0.5f));
    MakeLevel(size, 7, [&](int x, int y) { grid.SetWalkable(x, y, false); });
    grid.Erode(0.3f);
    const std::vector<Pair> pairs = MakePairs(grid, agents, 99);
    std::printf("Grid: %zu / %zu katak yuriladigan (xonalar, eshiklar, to'siqlar)\n", grid.CountWalkable(), grid.GetCellCount());
    const SearchResult serial = RunSearches(grid, pairs, false, 5);
    const SearchResult parallel = RunSearches(grid, pairs, true, 5);
    std::printf("\nYo'l qidirish (A* + string pulling), %d ta yo'l bir vaqtda:\n", agents);
    std::printf("  bitta oqim:         %8.2f ms  (yo'liga %.3f ms)\n", serial.Milliseconds, serial.Milliseconds / agents);
    std::printf("  vazifalar tizimi:   %8.2f ms  (%.1f barobar tez)\n", parallel.Milliseconds,
                parallel.Milliseconds > 0.0 ? serial.Milliseconds / parallel.Milliseconds : 0.0);
    std::printf("  to'liq yo'llar %d / %d, o'rtacha %.0f katak ochildi, %.1f burchak\n", serial.Complete, agents, serial.Expanded, serial.Corners);

    // ---- the scene: Nav Agents
    std::printf("\nSahna: %d Nav Agent, %d kadr (1/60 s)\n", agents, frames);
    std::printf("%-20s %10s %12s %14s %12s %10s %10s\n", "Holat", "bake 2D", "1-kadr", "(yo'llar)", "o'rtacha", "p95", "yo'l/kadr");
    for (bool par : { false, true })
    {
        const SceneResult r = RunScene(size, agents, frames, par);
        std::printf("%-20s %7.1f ms %9.2f ms %11.2f ms %9.3f ms %7.3f ms %10.1f\n", par ? "vazifalar tizimi" : "bitta oqim", r.BakeMilliseconds,
                    r.FirstFrame, (double)r.FirstFramePaths, r.Frames.Average, r.Frames.P95, r.PathsPerFrame);
    }

    // ---- the 3D bake
    int walkable = 0;
    const double bake3D = Bake3D(size, walkable);
    std::printf("\nNavGrid 3D bake: %dx%d katak (nurlar bilan), %.1f ms, %d tasi yuriladigan\n", size, size, bake3D, walkable);
    // The bench bakes without the .znav cache: what a grid costs at play start when it is baked.
    std::printf("\nXulosa: benchmark .znav keshisiz ishlaydi - grid o'yin boshlanganda qurilsa, yuqoridagi bake vaqti sahna yuklanishiga "
                "qo'shiladi. Katta grid'lar uchun NavGrid > \"Faylda saqlash\" (CacheBake): editor bake'ni sahna bilan .znav fayliga "
                "yozadi va o'yin manbalar o'zgarmagan bo'lsa uni qayta qurmasdan o'qiydi.\n");
    JobSystem::Shutdown();
    return 0;
}
