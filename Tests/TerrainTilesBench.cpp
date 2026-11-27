// ZaminBench --terrain-tiles: a large tiled terrain streamed around a camera that flies across it, with a real GL
// context (xvfb-run without a display; Mesa llvmpipe in Tests/terrain_tiles_bench.sh, so the numbers compare across
// machines - they are not a physical GPU's).
//
//   ZaminBench --terrain-tiles [--tiles N] [--resolution R] [--frames N] [--case <part of a name>] [--size WxH]
//                              [--json <file>] [--shots <folder>]
//
// N x N tiles (16 by default) of R samples (129) and 64 m each, made with the world generator into a temporary
// folder (with foliage), a shadow-casting sun, a camera that circles the map looking at its centre. Cases:
//   async     tiles read by the background reader (the default runtime path), 96 MB budget
//   sync      the same reads on the main thread (what a frame costs without the reader)
//   tight     async with a 12 MB budget: the farthest tiles coarsened first, then dropped
// Measured per frame: the whole frame (glFinish), the streaming update on the main thread, loads / unloads per frame,
// resident bytes against the budget, LOD counts, triangles and draw calls. Exit code 1 when the budget was ever
// exceeded or two resident neighbours disagree on a shared sample (a crack).
#include <Zamin/Zamin.h>
#include <Zamin/Scene/TerrainStreaming.h>
#include <Zamin/Asset/AsyncLoader.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

using namespace Zamin;

namespace {
    struct TilesCase
    {
        const char* Name;
        bool Async;
        uint32_t BudgetMB;
    };
    struct TilesResult
    {
        std::string Name;
        double FrameAverage = 0, FrameP95 = 0, FrameMax = 0, UpdateAverage = 0, UpdateMax = 0, IOMilliseconds = 0;
        uint32_t PeakLoads = 0, PeakResident = 0, DrawCalls = 0, Triangles = 0;
        uint64_t PeakBytes = 0, Budget = 0, Loads = 0, Unloads = 0, BytesRead = 0, Drops = 0, Coarsened = 0;
        std::array<uint32_t, 8> LODCounts{};
        float SeamError = 0;
        bool OverBudget = false;
    };
    double Percentile(std::vector<double> v, double p)
    {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, size_t(std::ceil(p * double(v.size()))) - (p > 0 ? 1 : 0))];
    }
    double Average(const std::vector<double>& v)
    {
        double s = 0;
        for (double x : v) s += x;
        return v.empty() ? 0 : s / double(v.size());
    }
    class TilesBenchApp : public Application
    {
    public:
        explicit TilesBenchApp(const ApplicationSpecification& spec) : Application(spec) {}
    };
}

int RunTerrainTilesBench(int argc, char** argv)
{
    uint32_t tiles = 16, resolution = 129, width = 1280, height = 720;
    int frames = 120;
    std::string only, json, shots;
    for (int i = 1; i < argc; i++)
    {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--terrain-tiles")) continue;
        else if (!std::strcmp(argv[i], "--tiles")) tiles = uint32_t(std::clamp(std::atoi(next()), 2, 64));
        else if (!std::strcmp(argv[i], "--resolution")) resolution = uint32_t(std::atoi(next()));
        else if (!std::strcmp(argv[i], "--frames")) frames = std::max(1, std::atoi(next()));
        else if (!std::strcmp(argv[i], "--case")) only = next();
        else if (!std::strcmp(argv[i], "--json")) json = next();
        else if (!std::strcmp(argv[i], "--shots")) shots = next();
        else if (!std::strcmp(argv[i], "--size"))
        {
            if (std::sscanf(next(), "%ux%u", &width, &height) != 2 || width == 0 || height == 0) width = 1280, height = 720;
        }
        else
        {
            std::printf("ZaminBench --terrain-tiles [--tiles N] [--resolution R] [--frames N] [--case <part of a name>] [--size WxH] [--json <file>] [--shots <folder>]\n");
            return 1;
        }
    }
    if (resolution != 33 && resolution != 65 && resolution != 129 && resolution != 257) resolution = 129;
    ApplicationSpecification spec;
    spec.Name = "Zamin Terrain Tiles Bench";
    spec.EnableImGui = false;
    spec.Window.Title = spec.Name;
    spec.Window.Width = width;
    spec.Window.Height = height;
    spec.Window.VSync = false;
    spec.CommandLineArgs = { argc, argv };
    auto* app = new TilesBenchApp(spec);
    Window& window = app->GetWindow();
    const uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
    const GraphicsContextInfo& gl = GraphicsContext::GetInfo();
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    // The tile set, made once for every case.
    TempDirectory temp("zamin-tiles-bench");
    const auto file = temp.GetPath() / "Bench.zterrain";
    TerrainTileSet set;
    set.TilesX = set.TilesZ = tiles;
    set.TileResolution = resolution;
    set.TileSize = 64;
    set.Layers[0].Color = { .28f, .5f, .2f };
    set.Layers[1].Color = { .45f, .42f, .38f };
    set.Foliage.LODDistance = 40;
    set.Foliage.CullDistance = 160;
    TerrainTools::WorldTerrainSettings world{ 42, 12, 18, .012f };
    std::string error;
    const auto made = std::chrono::steady_clock::now();
    if (!TerrainTileTools::Create(file, set, world, 400, error))
    {
        std::printf("The tile set could not be made: %s\n", error.c_str());
        delete app;
        return 1;
    }
    uint64_t diskBytes = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(temp.GetPath())) diskBytes += entry.is_regular_file() ? entry.file_size() : 0;
    std::printf("Zamin terrain tile streaming benchmark: %ux%u tiles of %u samples (%.0f m), %d frames, %ux%u, %s\nGL: %s / %s / %s\n"
                "Tile set: %.1f MiB on disk, made in %.0f ms\n\n",
                tiles, tiles, resolution, set.TileSize * float(tiles), frames, w, h, build, gl.Vendor.c_str(), gl.Renderer.c_str(), gl.Version.c_str(),
                double(diskBytes) / 1048576.0, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - made).count());
    FramebufferSpecification targetSpec;
    targetSpec.Width = w;
    targetSpec.Height = h;
    targetSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
    Ref<Framebuffer> target = Framebuffer::Create(targetSpec);
    PostProcessHistory history;
    const TilesCase cases[] = { { "async", true, 96 }, { "sync", false, 96 }, { "tight", true, 12 } };
    std::vector<TilesResult> results;
    int failures = 0;
    const float extent = set.TileSize * float(tiles);
    for (const auto& c : cases)
    {
        if (!only.empty() && std::string(c.Name).find(only) == std::string::npos) continue;
        AsyncLoader::SetEnabled(c.Async);
        auto scene = CreateRef<Scene>();
        Entity terrain = scene->CreateEntity("Tiled Terrain");
        auto& tiled = terrain.AddComponent<TiledTerrainComponent>();
        tiled.Path = file.string();
        tiled.LoadRadius = 400;
        tiled.UnloadRadius = 460;
        tiled.LODDistance = 80;
        tiled.MaxLOD = 3;
        tiled.MemoryBudgetMB = c.BudgetMB;
        tiled.MaxLoadsPerFrame = 4;
        tiled.FoliageDistance = 160;
        tiled.CollisionRadius = 96;
        Entity sun = scene->CreateEntity("Sun");
        sun.GetTransform().Rotation = { -.8f, .7f, 0 };
        sun.AddComponent<DirectionalLightComponent>().CastShadows = true;
        Entity camera = scene->CreateEntity("Camera");
        auto& cam = camera.AddComponent<CameraComponent>();
        cam.Camera.SetPerspective(glm::radians(60.f), .1f, 1500.f);
        cam.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        cam.Primary = true;
        // Once around the map's centre at 38% of its size, looking at the centre: tiles stream in ahead and out behind.
        auto place = [&](int frame) {
            const float a = 6.2831853f * float(frame) / float(std::max(1, frames));
            const glm::vec3 position(extent * (0.5f + 0.38f * std::cos(a)), 70.0f, extent * (0.5f + 0.38f * std::sin(a)));
            const glm::vec3 toCentre = glm::vec3(extent * 0.5f, 0.0f, extent * 0.5f) - position;
            auto& transform = camera.GetTransform();
            transform.Translation = position;
            transform.Rotation = { -.35f, std::atan2(-toCentre.x, -toCentre.z), 0 };
        };
        place(0);
        scene->OnViewportResize(w, h);
        scene->OnRuntimeStart();
        TilesResult r;
        r.Name = c.Name;
        std::vector<double> total, update;
        const float dt = 1.0f / 60.0f;
        for (int frame = -5; frame < frames; frame++)
        {
            place(std::max(0, frame));
            Time::OnFrame(dt); // what the application's loop does: one streaming update per frame
            Renderer::ResetStats();
            const auto start = std::chrono::steady_clock::now();
            target->Bind();
            RenderCommand::SetDepthTest(true);
            RenderCommand::SetDepthWrite(true);
            RenderCommand::SetClearColor({ .45f, .6f, .8f, 1 });
            RenderCommand::Clear();
            scene->OnUpdateRuntime(dt);
            target->Unbind();
            RenderCommand::SetViewport(0, 0, w, h);
            PostProcessInput input;
            input.ColorTexture = target->GetColorAttachmentRendererID(0);
            input.Width = w;
            input.Height = h;
            input.DepthTexture = target->GetDepthAttachmentRendererID();
            input.Projection = scene->GetLastProjection();
            input.OutputWidth = w;
            input.OutputHeight = h;
            input.DeltaTime = dt;
            input.History = &history;
            PostProcessor::Apply(input, scene->GetSettings().Post, nullptr);
            RenderCommand::Finish();
            AsyncLoader::Update(4.0);
            const auto finished = std::chrono::steady_clock::now();
            const TerrainStreamingStats s = scene->GetTerrainStreamingStats();
            const RenderStatistics rs = Renderer::GetStats();
            if (frame == frames - 1 && !shots.empty())
            {
                std::error_code ec;
                std::filesystem::create_directories(shots, ec);
                auto pixels = RenderCommand::ReadDefaultFramebuffer(w, h);
                Image::WritePNG((std::filesystem::path(shots) / std::format("terrain-tiles-bench-{}.png", c.Name)).string(), w, h, 4, pixels.data(), true);
            }
            window.OnUpdate();
            if (s.ResidentBytes > s.BudgetBytes) r.OverBudget = true;
            if (frame < 0) continue;
            total.push_back(std::chrono::duration<double, std::milli>(finished - start).count());
            update.push_back(s.UpdateMilliseconds);
            r.PeakLoads = std::max(r.PeakLoads, s.LoadsThisFrame);
            r.PeakResident = std::max(r.PeakResident, s.Resident);
            r.PeakBytes = std::max(r.PeakBytes, s.ResidentBytes);
            r.Budget = s.BudgetBytes;
            r.Loads = s.TotalLoads;
            r.Unloads = s.TotalUnloads;
            r.BytesRead = s.BytesRead;
            r.Drops = s.OverBudgetDrops;
            r.Coarsened = s.Coarsened;
            r.IOMilliseconds = s.IOMilliseconds;
            r.LODCounts = s.LODCounts;
            r.DrawCalls = rs.DrawCalls;
            r.Triangles = rs.TriangleCount;
        }
        r.SeamError = TerrainStreaming::MaxSharedEdgeError(terrain.GetComponent<TiledTerrainComponent>());
        TerrainStreaming::WaitIdle(terrain.GetComponent<TiledTerrainComponent>());
        scene->OnRuntimeStop();
        r.FrameAverage = Average(total);
        r.FrameP95 = Percentile(total, .95);
        r.FrameMax = total.empty() ? 0 : *std::max_element(total.begin(), total.end());
        r.UpdateAverage = Average(update);
        r.UpdateMax = update.empty() ? 0 : *std::max_element(update.begin(), update.end());
        if (r.OverBudget || r.SeamError > 0)
        {
            std::printf("FAILED %s: %s%s\n", c.Name, r.OverBudget ? "resident bytes over the budget " : "", r.SeamError > 0 ? "cracks between tiles" : "");
            failures++;
        }
        std::printf("%-6s | frame %7.2f ms (p95 %7.2f, max %7.2f) | streaming %6.3f ms (max %6.3f), IO %7.1f ms | loads %llu (peak %u/frame), unloads %llu | "
                    "resident peak %u tiles, %.1f / %.0f MiB | LOD %u/%u/%u/%u | coarsened %llu, dropped %llu | %u draws, %s triangles | read %.1f MiB | seams %.3g\n",
                    c.Name, r.FrameAverage, r.FrameP95, r.FrameMax, r.UpdateAverage, r.UpdateMax, r.IOMilliseconds, (unsigned long long)r.Loads, r.PeakLoads,
                    (unsigned long long)r.Unloads, r.PeakResident, double(r.PeakBytes) / 1048576.0, double(r.Budget) / 1048576.0, r.LODCounts[0], r.LODCounts[1],
                    r.LODCounts[2], r.LODCounts[3], (unsigned long long)r.Coarsened, (unsigned long long)r.Drops, r.DrawCalls,
                    RenderStatistics::FormatCount(r.Triangles).c_str(), double(r.BytesRead) / 1048576.0, double(r.SeamError));
        results.push_back(r);
    }
    AsyncLoader::SetEnabled(false);
    if (!json.empty())
    {
        std::ofstream out(json);
        out << "{\n  \"Build\": \"" << build << "\",\n  \"Renderer\": \"" << gl.Renderer << "\",\n  \"Size\": [" << w << ", " << h << "],\n  \"Frames\": " << frames
            << ",\n  \"Tiles\": " << tiles << ",\n  \"Resolution\": " << resolution << ",\n  \"Results\": {\n";
        std::vector<std::string> rows;
        for (const auto& r : results)
        {
            rows.push_back(std::format("    \"{}.FrameMs\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.FrameAverage));
            rows.push_back(std::format("    \"{}.FrameP95\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.FrameP95));
            rows.push_back(std::format("    \"{}.StreamingMs\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.UpdateAverage));
            rows.push_back(std::format("    \"{}.StreamingMaxMs\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.UpdateMax));
            rows.push_back(std::format("    \"{}.PeakResidentMiB\": {{ \"Value\": {:.2f}, \"Unit\": \"MiB\" }}", r.Name, double(r.PeakBytes) / 1048576.0));
        }
        for (size_t i = 0; i < rows.size(); ++i) out << rows[i] << (i + 1 < rows.size() ? ",\n" : "\n");
        out << "  },\n  \"Cases\": [\n";
        for (size_t i = 0; i < results.size(); ++i)
        {
            const auto& r = results[i];
            out << std::format("    {{ \"Name\": \"{}\", \"FrameMs\": {{ \"Average\": {:.3f}, \"P95\": {:.3f}, \"Max\": {:.3f} }}, "
                               "\"StreamingMs\": {{ \"Average\": {:.3f}, \"Max\": {:.3f} }}, \"IOMs\": {:.1f}, \"Loads\": {}, \"PeakLoadsPerFrame\": {}, "
                               "\"Unloads\": {}, \"PeakResidentTiles\": {}, \"PeakResidentBytes\": {}, \"BudgetBytes\": {}, \"LOD\": [{}, {}, {}, {}], "
                               "\"Coarsened\": {}, \"Dropped\": {}, \"DrawCalls\": {}, \"Triangles\": {}, \"BytesRead\": {}, \"SeamError\": {} }}{}\n",
                               r.Name, r.FrameAverage, r.FrameP95, r.FrameMax, r.UpdateAverage, r.UpdateMax, r.IOMilliseconds, r.Loads, r.PeakLoads, r.Unloads,
                               r.PeakResident, r.PeakBytes, r.Budget, r.LODCounts[0], r.LODCounts[1], r.LODCounts[2], r.LODCounts[3], r.Coarsened, r.Drops,
                               r.DrawCalls, r.Triangles, r.BytesRead, r.SeamError, i + 1 < results.size() ? "," : "");
        }
        out << "  ]\n}\n";
        std::printf("\nResult: %s\n", json.c_str());
    }
    target.reset();
    delete app;
    return results.empty() || failures ? 1 : 0;
}
