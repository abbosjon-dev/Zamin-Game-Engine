// ZaminBench --foliage: a dense packed foliage scene with a real GL context (xvfb-run without a display; Mesa
// llvmpipe in Tests/foliage_bench.sh, so the numbers compare across machines - they are not a physical GPU's).
//
//   ZaminBench --foliage [--instances N] [--frames N] [--case <part of a name>] [--size WxH] [--json <file>]
//                        [--shots <folder>] [--lod-colors]
//
// N instances (1 048 576 by default) in packed foliage components of up to 65 536 each (256 x 256 m tiles, one tree
// per square metre), near/far primitive LODs, a shadow-casting sun, a perspective camera at the forest's edge that
// moves into it (the same path for every case). The cases:
//   cpu / gpu               CPU culling (the GL 4.1 fallback) / GPU compute culling and indirect draws, HLOD on
//   cpu-nohlod / gpu-nohlod the same without HLOD (every far instance drawn up to the view distance)
// Every case warms up for 10 frames, then the frames are measured: the CPU time of the frame (until everything is
// submitted), the whole frame (glFinish), the culling's CPU time (the CPU path's whole pass; on the GPU path what
// issuing it costs) and the GPU compute time (timestamp queries), draw calls, triangles, instances per LOD, the shadow
// set, HLOD cells. The CPU and GPU cases end on the same camera: their per-LOD counts must match (exit code 1 if not).
#include <Zamin/Zamin.h>
#include <Zamin/Scene/Foliage.h>

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
    struct FoliageCase
    {
        const char* Name;
        FoliageBackend Backend;
        bool HLOD;
    };
    struct FoliageResult
    {
        std::string Name;
        bool GPU = false, HLOD = false;
        double CpuAverage = 0, CpuP95 = 0, FrameAverage = 0, FrameMedian = 0, FrameP95 = 0, FrameMax = 0;
        double CullCPU = 0, CullCPUP95 = 0, CullGPU = 0;
        uint32_t DrawCalls = 0, Triangles = 0, Near = 0, Far = 0, Shadow = 0, HLODCells = 0, HLODInstances = 0, Batches = 0, GPUBatches = 0;
        uint64_t FinalNear = 0, FinalFar = 0, FinalShadow = 0;
        uint64_t GPUBytes = 0, CPUBytes = 0;
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
    class FoliageBenchApp : public Application
    {
    public:
        explicit FoliageBenchApp(const ApplicationSpecification& spec) : Application(spec) {}
    };
    constexpr float TileSize = 256;

    Ref<Scene> BuildForest(uint32_t instances, const FoliageCase& c, float& side)
    {
        auto scene = CreateRef<Scene>();
        scene->SetName("FoliageBench");
        const uint32_t components = std::max<uint32_t>(1, (instances + FoliageBatch::MaxInstances - 1) / FoliageBatch::MaxInstances);
        const auto tiles = uint32_t(std::ceil(std::sqrt(double(components))));
        side = float(tiles) * TileSize;
        Entity ground = scene->CreateEntity("Ground");
        ground.GetTransform().Translation = { side * .5f, 0, side * .5f };
        ground.GetTransform().Scale = { side + 400, 1, side + 400 };
        auto& mr = ground.AddComponent<MeshRendererComponent>();
        mr.Type = PrimitiveMesh::Plane;
        mr.Mat.Albedo = { .36f, .34f, .27f, 1 };
        mr.Mat.Roughness = .95f;
        Scene::ResolveMeshRenderer(mr);
        Entity sun = scene->CreateEntity("Sun");
        sun.GetTransform().Rotation = { -.8f, .7f, 0 };
        sun.AddComponent<DirectionalLightComponent>().CastShadows = true;
        uint32_t left = instances;
        for (uint32_t i = 0; i < components; ++i)
        {
            const uint32_t count = std::min(left, FoliageBatch::MaxInstances);
            left -= count;
            Entity forest = scene->CreateEntity(std::format("Forest {}", i));
            forest.GetTransform().Translation = { float(i % tiles) * TileSize, 0, float(i / tiles) * TileSize };
            auto& f = forest.AddComponent<FoliageComponent>();
            FoliageTools::GenerateGrid(f, count, { TileSize, TileSize }, 100 + i);
            for (auto& inst : f.Instances) inst.Position.y = .5f * inst.Scale;
            f.NearType = PrimitiveMesh::Cone;
            f.FarType = PrimitiveMesh::Cube;
            f.Settings.LODDistance = 40;
            f.Settings.CullDistance = 1000;
            f.Settings.HLODDistance = c.HLOD ? 160.f : 0.f;
            f.Settings.HLODFade = 20;
            f.Settings.HLODCellSize = 32;
            f.Settings.Backend = c.Backend;
            f.CastShadows = true;
        }
        Entity camera = scene->CreateEntity("Camera");
        auto& cam = camera.AddComponent<CameraComponent>();
        cam.Camera.SetPerspective(glm::radians(60.f), .1f, 1200.f);
        cam.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        cam.Primary = true;
        return scene;
    }
    // The camera path: from outside the forest's edge, slowly in and turning a little (the same for every case).
    void PlaceCamera(const Ref<Scene>& scene, float side, int frame)
    {
        Entity camera = scene->FindEntityByName("Camera");
        auto& t = camera.GetTransform();
        t.Translation = { side * .5f, 9, side - 6 - .35f * float(frame) };
        t.Rotation = { -.1f, .0015f * float(frame), 0 };
    }
}

int RunFoliageBench(int argc, char** argv)
{
    uint32_t instances = 1u << 20, width = 1280, height = 720;
    int frames = 60;
    std::string only, json, shots;
    bool lodColors = false;
    for (int i = 1; i < argc; i++)
    {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--foliage")) continue;
        else if (!std::strcmp(argv[i], "--instances")) instances = uint32_t(std::clamp(std::atoll(next()), 1ll, 16ll << 20));
        else if (!std::strcmp(argv[i], "--frames")) frames = std::max(1, std::atoi(next()));
        else if (!std::strcmp(argv[i], "--case")) only = next();
        else if (!std::strcmp(argv[i], "--json")) json = next();
        else if (!std::strcmp(argv[i], "--shots")) shots = next();
        else if (!std::strcmp(argv[i], "--lod-colors")) lodColors = true;
        else if (!std::strcmp(argv[i], "--size"))
        {
            if (std::sscanf(next(), "%ux%u", &width, &height) != 2 || width == 0 || height == 0) width = 1280, height = 720;
        }
        else
        {
            std::printf("ZaminBench --foliage [--instances N] [--frames N] [--case <part of a name>] [--size WxH] [--json <file>] [--shots <folder>] "
                        "[--lod-colors]\n");
            return 1;
        }
    }
    ApplicationSpecification spec;
    spec.Name = "Zamin Foliage Bench";
    spec.EnableImGui = false;
    spec.Window.Title = "Zamin Foliage Bench";
    spec.Window.Width = width;
    spec.Window.Height = height;
    spec.Window.VSync = false;
    spec.CommandLineArgs = { argc, argv };
    auto* app = new FoliageBenchApp(spec);
    Window& window = app->GetWindow();
    const uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
    const GraphicsContextInfo& gl = GraphicsContext::GetInfo();
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    std::printf("Zamin foliage benchmark: %u instances, %d frames, %ux%u, %s\nGL: %s / %s / %s\n\n", instances, frames, w, h, build, gl.Vendor.c_str(),
                gl.Renderer.c_str(), gl.Version.c_str());
    FramebufferSpecification targetSpec;
    targetSpec.Width = w;
    targetSpec.Height = h;
    targetSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
    Ref<Framebuffer> target = Framebuffer::Create(targetSpec);
    PostProcessHistory history;
    Renderer3D::SetFoliageLODDebugView(lodColors);
    const FoliageCase cases[] = { { "cpu", FoliageBackend::CPU, true }, { "gpu", FoliageBackend::GPU, true },
                                  { "cpu-nohlod", FoliageBackend::CPU, false }, { "gpu-nohlod", FoliageBackend::GPU, false } };
    std::vector<FoliageResult> results;
    int failures = 0;
    for (const auto& c : cases)
    {
        if (!only.empty() && std::string(c.Name).find(only) == std::string::npos) continue;
        float side = 0;
        const auto buildStart = std::chrono::steady_clock::now();
        Ref<Scene> scene = BuildForest(instances, c, side);
        scene->OnViewportResize(w, h);
        scene->OnRuntimeStart();
        FoliageResult r;
        r.Name = c.Name;
        r.HLOD = c.HLOD;
        std::vector<double> cpu, total, cull, cullGPU;
        const float dt = 1.0f / 60.0f;
        for (int frame = -10; frame < frames; frame++)
        {
            PlaceCamera(scene, side, frame + 10);
            Renderer::ResetStats();
            const auto start = std::chrono::steady_clock::now();
            target->Bind();
            RenderCommand::SetDepthTest(true);
            RenderCommand::SetDepthWrite(true);
            RenderCommand::SetClearColor({ .35f, .45f, .6f, 1 });
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
            const auto submitted = std::chrono::steady_clock::now();
            RenderCommand::Finish();
            const auto finished = std::chrono::steady_clock::now();
            const RenderStatistics s = Renderer::GetStats();
            if (frame == -10)
                std::printf("%-11s scene built and first frame in %.0f ms\n", c.Name,
                            std::chrono::duration<double, std::milli>(finished - buildStart).count());
            if (frame == frames - 1 && !shots.empty())
            {
                std::error_code ec;
                std::filesystem::create_directories(shots, ec);
                auto pixels = RenderCommand::ReadDefaultFramebuffer(w, h);
                Image::WritePNG((std::filesystem::path(shots) / std::format("foliage-bench-{}{}.png", c.Name, lodColors ? "-lod" : "")).string(), w, h, 4,
                                pixels.data(), true);
            }
            window.OnUpdate();
            if (frame < 0) continue;
            cpu.push_back(std::chrono::duration<double, std::milli>(submitted - start).count());
            total.push_back(std::chrono::duration<double, std::milli>(finished - start).count());
            cull.push_back(s.FoliageCullCPUMilliseconds);
            if (s.FoliageGPUBatches) cullGPU.push_back(s.FoliageCullGPUMilliseconds);
            r.DrawCalls = s.DrawCalls;
            r.Triangles = s.TriangleCount;
            r.Near = s.FoliageNear;
            r.Far = s.FoliageFar;
            r.Shadow = s.FoliageShadowInstances;
            r.HLODCells = s.FoliageHLODCells;
            r.HLODInstances = s.FoliageHLODInstances;
            r.Batches = s.FoliageBatches;
            r.GPUBatches = s.FoliageGPUBatches;
        }
        // The last frame's exact counts (a synchronous readback on the GPU path) and the memory.
        for (auto e : scene->GetAllEntitiesWith<FoliageComponent>())
        {
            Entity entity(e, scene.get());
            auto batch = FoliageTools::Prepare(entity.GetComponent<FoliageComponent>(), scene->GetWorldTransform(entity), int(uint32_t(e)));
            if (!batch) continue;
            const auto v = batch->DebugVisibleCounts(), sh = batch->DebugShadowCounts();
            r.FinalNear += v[0];
            r.FinalFar += v[1];
            r.FinalShadow += sh[0] + sh[1];
            r.GPU |= batch->GetStats().GPU;
            r.GPUBytes += batch->GetStats().GPUBytes;
            r.CPUBytes += batch->GetStats().CPUBytes;
        }
        scene->OnRuntimeStop();
        r.CpuAverage = Average(cpu);
        r.CpuP95 = Percentile(cpu, .95);
        r.FrameAverage = Average(total);
        r.FrameMedian = Percentile(total, .5);
        r.FrameP95 = Percentile(total, .95);
        r.FrameMax = total.empty() ? 0 : *std::max_element(total.begin(), total.end());
        r.CullCPU = Average(cull);
        r.CullCPUP95 = Percentile(cull, .95);
        r.CullGPU = Average(cullGPU);
        std::printf("%-11s %s | CPU %7.2f ms (p95 %7.2f) | frame %7.2f / %7.2f / %7.2f / %7.2f ms | cull CPU %6.3f ms (p95 %6.3f), GPU %6.3f ms | "
                    "%u draw calls, %s triangles | near %u far %u shadow %u | HLOD %u cells for %u instances | %u batches (%u GPU) | "
                    "GPU %.1f MiB, CPU %.1f MiB\n",
                    c.Name, r.GPU ? "GPU" : "CPU", r.CpuAverage, r.CpuP95, r.FrameAverage, r.FrameMedian, r.FrameP95, r.FrameMax, r.CullCPU, r.CullCPUP95,
                    r.CullGPU, r.DrawCalls, RenderStatistics::FormatCount(r.Triangles).c_str(), r.Near, r.Far, r.Shadow, r.HLODCells, r.HLODInstances,
                    r.Batches, r.GPUBatches, double(r.GPUBytes) / 1048576.0, double(r.CPUBytes) / 1048576.0);
        results.push_back(r);
    }
    // The CPU and GPU paths took the same camera path: their final counts must be the same.
    for (const auto& a : results)
        for (const auto& b : results)
            if (!a.GPU && b.GPU && a.HLOD == b.HLOD && (a.FinalNear != b.FinalNear || a.FinalFar != b.FinalFar || a.FinalShadow != b.FinalShadow))
            {
                std::printf("MISMATCH %s vs %s: near %llu/%llu far %llu/%llu shadow %llu/%llu\n", a.Name.c_str(), b.Name.c_str(), (unsigned long long)a.FinalNear,
                            (unsigned long long)b.FinalNear, (unsigned long long)a.FinalFar, (unsigned long long)b.FinalFar, (unsigned long long)a.FinalShadow,
                            (unsigned long long)b.FinalShadow);
                ++failures;
            }
            else if (!a.GPU && b.GPU && a.HLOD == b.HLOD)
                std::printf("CPU/GPU parity %s = %s: near %llu, far %llu, shadow %llu\n", a.Name.c_str(), b.Name.c_str(), (unsigned long long)a.FinalNear,
                            (unsigned long long)a.FinalFar, (unsigned long long)a.FinalShadow);
    if (!json.empty())
    {
        std::ofstream out(json);
        out << "{\n  \"Build\": \"" << build << "\",\n  \"Renderer\": \"" << gl.Renderer << "\",\n  \"Size\": [" << w << ", " << h << "],\n  \"Frames\": " << frames
            << ",\n  \"Instances\": " << instances << ",\n  \"Results\": {\n";
        std::vector<std::string> rows;
        for (const auto& r : results)
        {
            rows.push_back(std::format("    \"{}.FrameMs\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.FrameAverage));
            rows.push_back(std::format("    \"{}.FrameP95\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.FrameP95));
            rows.push_back(std::format("    \"{}.CpuMs\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.CpuAverage));
            rows.push_back(std::format("    \"{}.CullCpuMs\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.CullCPU));
            if (r.GPU) rows.push_back(std::format("    \"{}.CullGpuMs\": {{ \"Ms\": {:.3f}, \"PerFrame\": true }}", r.Name, r.CullGPU));
            rows.push_back(std::format("    \"{}.DrawCalls\": {{ \"Value\": {}, \"Unit\": \"draws\" }}", r.Name, r.DrawCalls));
        }
        for (size_t i = 0; i < rows.size(); ++i) out << rows[i] << (i + 1 < rows.size() ? ",\n" : "\n");
        out << "  },\n  \"Cases\": [\n";
        for (size_t i = 0; i < results.size(); ++i)
        {
            const auto& r = results[i];
            out << std::format("    {{ \"Name\": \"{}\", \"GPU\": {}, \"HLOD\": {}, \"CpuMs\": {{ \"Average\": {:.3f}, \"P95\": {:.3f} }}, "
                               "\"FrameMs\": {{ \"Average\": {:.3f}, \"Median\": {:.3f}, \"P95\": {:.3f}, \"Max\": {:.3f} }}, "
                               "\"CullMs\": {{ \"CPU\": {:.3f}, \"CPUP95\": {:.3f}, \"GPU\": {:.3f} }}, \"DrawCalls\": {}, \"Triangles\": {}, "
                               "\"Near\": {}, \"Far\": {}, \"Shadow\": {}, \"HLODCells\": {}, \"HLODInstances\": {}, \"Batches\": {}, "
                               "\"Final\": {{ \"Near\": {}, \"Far\": {}, \"Shadow\": {} }}, \"GPUBytes\": {}, \"CPUBytes\": {} }}{}\n",
                               r.Name, r.GPU, r.HLOD, r.CpuAverage, r.CpuP95, r.FrameAverage, r.FrameMedian, r.FrameP95, r.FrameMax, r.CullCPU, r.CullCPUP95,
                               r.CullGPU, r.DrawCalls, r.Triangles, r.Near, r.Far, r.Shadow, r.HLODCells, r.HLODInstances, r.Batches, r.FinalNear, r.FinalFar,
                               r.FinalShadow, r.GPUBytes, r.CPUBytes, i + 1 < results.size() ? "," : "");
        }
        out << "  ]\n}\n";
        std::printf("\nResult: %s\n", json.c_str());
    }
    Renderer3D::SetFoliageLODDebugView(false);
    target.reset();
    delete app;
    return results.empty() || failures ? 1 : 0;
}
