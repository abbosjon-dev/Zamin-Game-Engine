// ZaminBench --render: how long a frame of a large scene takes with a real GL context (a window - xvfb-run without
// a display; Tests/render_bench.sh). The scenes are drawn the way the player draws them: the primary camera, the
// scene's frame (OnUpdateRuntime) into a linear HDR target, then post-processing onto the window.
//
//   ZaminBench --render [--sprites N] [--meshes N] [--frames N] [--case <part of a name>] [--size WxH] [--json <file>]
//                       [--shots <folder>]
//
// The cases (10 000 sprites and 1 000 meshes by default):
//   sprites           N sprites (a quarter of them with one of 4 textures) under an orthographic camera
//   meshes            N primitives (cubes, spheres, cylinders, cones; 8 materials) on a ground with the sun's
//                     shadows and a point light, a perspective camera - instanced as usual
//   meshes-noinst     the same with GPU instancing off (what instancing saves)
//   combined          the sprites (on a wall behind) and the meshes in one scene
// For each: the CPU time of the frame (until everything is submitted) and the whole frame (glFinish: the GPU
// done too) - average, median, p95, min, max ms - and what was drawn (draw calls, quads, meshes, instanced,
// triangles). --json writes them (render-bench.json: the baseline in docs/Benchmarks.md was made with it).
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace Zamin;

namespace {

    struct RenderBenchCase
    {
        const char* Name;
        const char* Description;
        std::function<Ref<Scene>(int sprites, int meshes)> Build;
        bool Instancing = true;
    };

    struct RenderBenchResult
    {
        std::string Name;
        int Entities = 0;
        double CpuAverage = 0, CpuMedian = 0, CpuP95 = 0;
        double FrameAverage = 0, FrameMedian = 0, FrameP95 = 0, FrameMin = 0, FrameMax = 0;
        RenderStatistics Stats;
    };

    // A small repeatable random sequence (the same scene every run).
    struct BenchRandom
    {
        uint32_t State = 0x2545F491u;
        float Next()
        {
            State ^= State << 13;
            State ^= State >> 17;
            State ^= State << 5;
            return (float)(State & 0xFFFFFF) / (float)0xFFFFFF;
        }
        float Range(float a, float b) { return a + (b - a) * Next(); }
    };

    // Four 64 x 64 textures made in memory (checker, stripes, rings, a gradient): no files needed.
    std::vector<Ref<Texture2D>> MakeTextures()
    {
        std::vector<Ref<Texture2D>> textures;
        for (int kind = 0; kind < 4; kind++)
        {
            TextureSpecification spec;
            spec.Width = spec.Height = 64;
            Ref<Texture2D> texture = Texture2D::Create(spec);
            std::vector<uint32_t> pixels(64 * 64);
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 64; x++)
                {
                    bool on = false;
                    if (kind == 0) on = ((x / 8) + (y / 8)) % 2 == 0;
                    if (kind == 1) on = (x / 6) % 2 == 0;
                    if (kind == 2) on = ((int)std::sqrt((float)((x - 32) * (x - 32) + (y - 32) * (y - 32))) / 5) % 2 == 0;
                    const uint8_t v = kind == 3 ? (uint8_t)(x * 4) : (on ? 255 : 90);
                    pixels[(size_t)y * 64 + x] = 0xFF000000u | (uint32_t)v << 16 | (uint32_t)(255 - v / 2) << 8 | v;
                }
            texture->SetData(pixels.data(), (uint32_t)(pixels.size() * sizeof(uint32_t)));
            textures.push_back(texture);
        }
        return textures;
    }

    Entity AddCamera(const Ref<Scene>& scene, bool ortho, const glm::vec3& position, const glm::vec3& rotation, float size)
    {
        Entity camera = scene->CreateEntity("Camera");
        camera.GetTransform().Translation = position;
        camera.GetTransform().Rotation = rotation;
        auto& c = camera.AddComponent<CameraComponent>();
        if (ortho)
            c.Camera.SetOrthographic(size, -100.0f, 100.0f);
        else
            c.Camera.SetPerspective(glm::radians(55.0f), 0.1f, 300.0f);
        c.Camera.SetProjectionType(ortho ? SceneCamera::ProjectionType::Orthographic : SceneCamera::ProjectionType::Perspective);
        c.Primary = true;
        return camera;
    }

    // The textures the sprites use (made once the GL context exists, released before it goes).
    std::vector<Ref<Texture2D>> s_Textures;

    // N sprites on a square of side ~sqrt(N) (z: the wall's depth in the combined scene).
    void AddSprites(const Ref<Scene>& scene, int count, float z, float scale)
    {
        if (s_Textures.empty())
            s_Textures = MakeTextures();
        BenchRandom random;
        const int side = std::max(1, (int)std::ceil(std::sqrt((float)count)));
        for (int i = 0; i < count; i++)
        {
            Entity e = scene->CreateEntity("Sprite");
            auto& t = e.GetTransform();
            t.Translation = { ((float)(i % side) - side * 0.5f) * scale, ((float)(i / side) - side * 0.5f) * scale, z };
            t.Rotation.z = random.Range(-0.6f, 0.6f);
            t.Scale = glm::vec3(scale * random.Range(0.7f, 1.1f));
            auto& sprite = e.AddComponent<SpriteRendererComponent>();
            sprite.Color = { random.Range(0.3f, 1.0f), random.Range(0.3f, 1.0f), random.Range(0.3f, 1.0f), 1.0f };
            if (i % 4 == 0)
                sprite.Texture = s_Textures[(size_t)(i / 4) % s_Textures.size()];
        }
    }

    void AddMeshes(const Ref<Scene>& scene, int count)
    {
        BenchRandom random;
        static const PrimitiveMesh s_Types[] = { PrimitiveMesh::Cube, PrimitiveMesh::Sphere, PrimitiveMesh::Cylinder, PrimitiveMesh::Cone };
        Entity ground = scene->CreateEntity("Ground");
        ground.GetTransform().Scale = { 120.0f, 1.0f, 120.0f };
        auto& gm = ground.AddComponent<MeshRendererComponent>();
        gm.Type = PrimitiveMesh::Plane;
        gm.Mat.Albedo = { 0.55f, 0.57f, 0.6f, 1.0f };
        Scene::ResolveMeshRenderer(gm);
        const int side = std::max(1, (int)std::ceil(std::sqrt((float)count)));
        for (int i = 0; i < count; i++)
        {
            Entity e = scene->CreateEntity("Mesh");
            auto& t = e.GetTransform();
            t.Translation = { ((float)(i % side) - side * 0.5f) * 1.6f, 0.6f, ((float)(i / side) - side * 0.5f) * 1.6f };
            t.Rotation.y = random.Range(0.0f, 3.0f);
            t.Scale = glm::vec3(random.Range(0.6f, 1.1f));
            auto& mr = e.AddComponent<MeshRendererComponent>();
            mr.Type = s_Types[i % 4];
            const int material = (i / 4) % 8; // 8 materials: the instancing batches by mesh and material
            mr.Mat.Albedo = { 0.3f + 0.08f * material, 0.8f - 0.07f * material, 0.4f + 0.05f * (material % 3), 1.0f };
            mr.Mat.Roughness = 0.3f + 0.08f * (float)material;
            mr.Mat.Metallic = material == 7 ? 1.0f : 0.0f;
            Scene::ResolveMeshRenderer(mr);
        }
        Entity sun = scene->CreateEntity("Sun");
        sun.GetTransform().Rotation = { glm::radians(-50.0f), glm::radians(35.0f), 0.0f };
        auto& light = sun.AddComponent<DirectionalLightComponent>();
        light.Intensity = 2.5f;
        light.CastShadows = true;
        Entity point = scene->CreateEntity("Point");
        point.GetTransform().Translation = { 0.0f, 4.0f, 0.0f };
        auto& p = point.AddComponent<PointLightComponent>();
        p.Color = { 1.0f, 0.7f, 0.4f };
        p.Intensity = 6.0f;
        p.Radius = 20.0f;
    }

    std::vector<RenderBenchCase> MakeRenderCases()
    {
        std::vector<RenderBenchCase> cases;
        cases.push_back({ "sprites", "spritelar (ortografik kamera, choragi teksturali)", [](int sprites, int) {
                              auto scene = CreateRef<Scene>();
                              const float side = std::ceil(std::sqrt((float)sprites));
                              AddCamera(scene, true, { 0.0f, 0.0f, 10.0f }, glm::vec3(0.0f), side * 1.05f); // the whole height
                              AddSprites(scene, sprites, 0.0f, 1.0f);
                              return scene;
                          } });
        auto meshes = [](int, int count) {
            auto scene = CreateRef<Scene>();
            const float side = std::ceil(std::sqrt((float)count)) * 1.6f;
            AddCamera(scene, false, { 0.0f, side * 0.55f, side * 0.75f }, { glm::radians(-38.0f), 0.0f, 0.0f }, 0.0f);
            AddMeshes(scene, count);
            return scene;
        };
        cases.push_back({ "meshes", "meshlar (soyali quyosh, nuqtali chiroq, instancing)", meshes });
        cases.push_back({ "meshes-noinst", "o'sha meshlar instancing'siz", meshes, false });
        cases.push_back({ "combined", "spritelar (orqadagi devorda) va meshlar bitta sahnada", [](int sprites, int count) {
                              auto scene = CreateRef<Scene>();
                              const float side = std::ceil(std::sqrt((float)count)) * 1.6f;
                              AddCamera(scene, false, { 0.0f, side * 0.55f, side * 0.75f }, { glm::radians(-38.0f), 0.0f, 0.0f }, 0.0f);
                              AddMeshes(scene, count);
                              AddSprites(scene, sprites, -side * 0.6f, 0.45f);
                              return scene;
                          } });
        return cases;
    }

    double Percentile(std::vector<double> values, double p)
    {
        if (values.empty())
            return 0.0;
        std::sort(values.begin(), values.end());
        const size_t index = std::min(values.size() - 1, (size_t)std::ceil(p * (double)values.size()) - (p > 0.0 ? 1 : 0));
        return values[index];
    }

    double Average(const std::vector<double>& values)
    {
        double sum = 0.0;
        for (double v : values)
            sum += v;
        return values.empty() ? 0.0 : sum / (double)values.size();
    }

    class RenderBenchApp : public Application
    {
    public:
        explicit RenderBenchApp(const ApplicationSpecification& spec) : Application(spec) {}
    };

}

int RunRenderBench(int argc, char** argv)
{
    int sprites = 10000, meshes = 1000, frames = 120;
    uint32_t width = 1280, height = 720;
    std::string only, json, shots;
    for (int i = 1; i < argc; i++)
    {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--render")) continue;
        else if (!std::strcmp(argv[i], "--sprites")) sprites = std::max(1, std::atoi(next()));
        else if (!std::strcmp(argv[i], "--meshes")) meshes = std::max(1, std::atoi(next()));
        else if (!std::strcmp(argv[i], "--frames")) frames = std::max(1, std::atoi(next()));
        else if (!std::strcmp(argv[i], "--case")) only = next();
        else if (!std::strcmp(argv[i], "--json")) json = next();
        else if (!std::strcmp(argv[i], "--shots")) shots = next();
        else if (!std::strcmp(argv[i], "--size"))
        {
            if (std::sscanf(next(), "%ux%u", &width, &height) != 2 || width == 0 || height == 0)
                width = 1280, height = 720;
        }
        else
        {
            std::printf("ZaminBench --render [--sprites N] [--meshes N] [--frames N] [--case <nom qismi>] [--size WxH] [--json <fayl>] "
                        "[--shots <papka>]\n");
            return 1;
        }
    }

    ApplicationSpecification spec;
    spec.Name = "Zamin Render Bench";
    spec.EnableImGui = false;
    spec.Window.Title = "Zamin Render Bench";
    spec.Window.Width = width;
    spec.Window.Height = height;
    spec.Window.VSync = false;
    spec.CommandLineArgs = { argc, argv };
    auto* app = new RenderBenchApp(spec);
    Window& window = app->GetWindow();
    const uint32_t w = window.GetFramebufferWidth(), h = window.GetFramebufferHeight();
    const GraphicsContextInfo& gl = GraphicsContext::GetInfo();
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    std::printf("Zamin render benchmark: %d sprite, %d mesh, %d kadr, %ux%u, %s\nGL: %s / %s / %s\n\n", sprites, meshes, frames, w, h, build,
                gl.Vendor.c_str(), gl.Renderer.c_str(), gl.Version.c_str());
    std::printf("%-14s %8s | %-26s | %-37s | %s\n", "Holat", "Obyekt", "CPU ms (o'rt / med / p95)", "Kadr ms, GPU bilan (o'rt / med / p95 / max)",
                "Chizildi");

    FramebufferSpecification targetSpec;
    targetSpec.Width = w;
    targetSpec.Height = h;
    targetSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
    Ref<Framebuffer> target = Framebuffer::Create(targetSpec);
    PostProcessHistory history;

    std::vector<RenderBenchResult> results;
    for (const RenderBenchCase& c : MakeRenderCases())
    {
        if (!only.empty() && std::string(c.Name).find(only) == std::string::npos)
            continue;
        Ref<Scene> scene = c.Build(sprites, meshes);
        scene->OnViewportResize(w, h);
        scene->OnRuntimeStart();
        Renderer3D::SetInstancing(c.Instancing);
        RenderBenchResult result;
        result.Name = c.Name;
        result.Entities = (int)scene->GetEntitiesInOrder().size();
        std::vector<double> cpu, total;
        const float dt = 1.0f / 60.0f;
        for (int frame = -20; frame < frames; frame++) // 20 frames to warm up (shaders, uploads, caches)
        {
            Renderer::ResetStats();
            const auto start = std::chrono::steady_clock::now();
            target->Bind();
            RenderCommand::SetDepthTest(true);
            RenderCommand::SetDepthWrite(true);
            RenderCommand::SetBlendMode(BlendMode::Alpha);
            const glm::vec4 clear = scene->GetClearColor();
            RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), 1.0f });
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
            if (frame == frames - 1)
                result.Stats = Renderer::GetStats();
            if (frame == frames - 1 && !shots.empty())
            {
                std::error_code ec;
                std::filesystem::create_directories(shots, ec);
                std::vector<uint8_t> pixels = RenderCommand::ReadDefaultFramebuffer(w, h);
                Image::WritePNG((std::filesystem::path(shots) / std::format("render-bench-{}.png", c.Name)).string(), w, h, 4, pixels.data(), true);
            }
            window.OnUpdate(); // swap, events
            if (frame < 0)
                continue;
            cpu.push_back(std::chrono::duration<double, std::milli>(submitted - start).count());
            total.push_back(std::chrono::duration<double, std::milli>(finished - start).count());
        }
        scene->OnRuntimeStop();
        Renderer3D::SetInstancing(true);
        result.CpuAverage = Average(cpu);
        result.CpuMedian = Percentile(cpu, 0.5);
        result.CpuP95 = Percentile(cpu, 0.95);
        result.FrameAverage = Average(total);
        result.FrameMedian = Percentile(total, 0.5);
        result.FrameP95 = Percentile(total, 0.95);
        result.FrameMin = *std::min_element(total.begin(), total.end());
        result.FrameMax = *std::max_element(total.begin(), total.end());
        const RenderStatistics& s = result.Stats;
        std::printf("%-14s %8d | %7.2f / %7.2f / %7.2f | %7.2f / %7.2f / %7.2f / %7.2f | %u chaqiriq, %u kvad, %u mesh (%u instans), %s uchburchak\n",
                    c.Name, result.Entities, result.CpuAverage, result.CpuMedian, result.CpuP95, result.FrameAverage, result.FrameMedian,
                    result.FrameP95, result.FrameMax, s.DrawCalls, s.QuadCount, s.MeshCount, s.InstancedMeshCount,
                    RenderStatistics::FormatCount(s.TriangleCount).c_str());
        results.push_back(result);
    }

    if (!json.empty())
    {
        std::ofstream out(json);
        out << "{\n  \"Renderer\": \"" << gl.Renderer << "\",\n  \"Build\": \"" << build << "\",\n  \"Size\": [" << w << ", " << h
            << "],\n  \"Frames\": " << frames << ",\n  \"Sprites\": " << sprites << ",\n  \"Meshes\": " << meshes << ",\n  \"Cases\": [\n";
        for (size_t i = 0; i < results.size(); i++)
        {
            const RenderBenchResult& r = results[i];
            out << std::format("    {{ \"Name\": \"{}\", \"Entities\": {}, \"CpuMs\": {{ \"Average\": {:.3f}, \"Median\": {:.3f}, \"P95\": {:.3f} }}, "
                               "\"FrameMs\": {{ \"Average\": {:.3f}, \"Median\": {:.3f}, \"P95\": {:.3f}, \"Min\": {:.3f}, \"Max\": {:.3f} }}, "
                               "\"DrawCalls\": {}, \"Quads\": {}, \"Meshes\": {}, \"InstancedMeshes\": {}, \"Triangles\": {} }}{}\n",
                               r.Name, r.Entities, r.CpuAverage, r.CpuMedian, r.CpuP95, r.FrameAverage, r.FrameMedian, r.FrameP95, r.FrameMin,
                               r.FrameMax, r.Stats.DrawCalls, r.Stats.QuadCount, r.Stats.MeshCount, r.Stats.InstancedMeshCount,
                               r.Stats.TriangleCount, i + 1 < results.size() ? "," : "");
        }
        out << "  ]\n}\n";
        std::printf("\nNatija: %s\n", json.c_str());
    }
    s_Textures.clear();
    target.reset();
    delete app;
    return results.empty() ? 1 : 0;
}
