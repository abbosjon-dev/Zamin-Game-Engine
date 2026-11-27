#include <Zamin/Zamin.h>
#include <Zamin/Scene/Foliage.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <stdexcept>
#include <chrono>
using namespace Zamin;
void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
class TestApp : public Application
{
public:
    using Application::Application;
};
int main(int argc, char** argv)
{
    ApplicationSpecification spec;
    spec.Name = "Packed foliage GL oracle";
    spec.EnableImGui = false;
    spec.Window.Width = spec.Window.Height = 256;
    spec.Window.VSync = false;
    spec.CommandLineArgs = { argc, argv };
    TestApp app(spec);
    try
    {
        bool require = argc > 1 && std::string(argv[1]) == "--require-gpu";
        Check(!require || GLAD_GL_VERSION_4_3, "requested GPU compute capability");
        FramebufferSpecification fs;
        fs.Width = fs.Height = 192;
        fs.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        auto target = Framebuffer::Create(fs);
        auto cpu = CreateRef<FoliageBatch>(), gpu = CreateRef<FoliageBatch>();
        Material mat;
        mat.Albedo = { .25f, .6f, .15f, 1 };
        for (auto batch : { cpu, gpu })
        {
            batch->Parts[0].push_back({ MeshLibrary::GetPrimitive(PrimitiveMesh::Cone), mat });
            batch->Parts[1].push_back({ MeshLibrary::GetPrimitive(PrimitiveMesh::Cube), mat });
            batch->Settings.LODDistance = 25;
            batch->Settings.CullDistance = 80;
            batch->Settings.Hysteresis = .1f;
        }
        cpu->Settings.Backend = FoliageBackend::CPU;
        gpu->Settings.Backend = FoliageBackend::GPU;
        std::vector<glm::mat4> transforms;
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x) transforms.push_back(glm::translate(glm::mat4(1), { float(x) - 32, 0, float(z) - 32 }));
        AABB box{ { -.5f, -.5f, -.5f }, { .5f, .5f, .5f } };
        Check(cpu->Upload(transforms, box, 42) && gpu->Upload(transforms, box, 42), "upload 4096 packed instances");
        auto uploads = gpu->GetStats().Uploads;
        auto draw = [&](const Ref<FoliageBatch>& batch, glm::vec3 eye, bool sun = false, WireframeMode wire = WireframeMode::Off) {
            target->Bind();
            RenderCommand::SetClearColor({ 0, 0, 0, 1 });
            RenderCommand::Clear();
            target->ClearAttachment(1, -1);
            SceneEnvironment env;
            env.DrawSky = false;
            env.DebugView = RenderDebugView::Albedo;
            env.HasSun = sun;
            env.Shadows.AtlasSize = 1024;
            env.Wireframe = wire;
            Renderer3D::BeginScene(glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0)), glm::perspective(glm::radians(55.f), 1.f, .1f, 300.f), eye, env);
            Renderer3D::SubmitFoliage(batch);
            Renderer3D::EndScene();
            auto pixels = target->ReadColorPixels();
            target->Unbind();
            Check(glGetError() == GL_NO_ERROR, "GL state/error");
            return pixels;
        };
        uint64_t baselineTriangles = uint64_t(transforms.size()) * cpu->Parts[0][0].Geometry->GetTriangleCount();
        uint64_t selectedTriangles = 0;
        for (glm::vec3 eye : { glm::vec3(0, 12, 40), glm::vec3(40, 20, 60), glm::vec3(0, 12, 41), glm::vec3(0, 12, 39), glm::vec3(0, 100, 170) })
        {
            auto reference = draw(cpu, eye), actual = draw(gpu, eye);
            auto cc = cpu->DebugVisibleCounts(), gc = gpu->DebugVisibleCounts();
            Check(cc == gc, "GPU and CPU culling/LOD counts match");
            Check(reference.size() == actual.size(), "pixel buffer size");
            size_t different = 0;
            for (size_t p = 0; p < actual.size(); p += 4)
            {
                bool differs = false;
                for (int c = 0; c < 3; ++c) differs |= std::abs(int(actual[p + c]) - int(reference[p + c])) > 8;
                if (differs) ++different;
            }
            Check(different * 200 <= actual.size() / 4, "GPU/CPU rendered images match within 0.5 percent");
            std::printf("view %.0f,%.0f,%.0f: near=%u far=%u culled=%zu GPU=%d pixel-diff=%zu\n", eye.x, eye.y, eye.z, gc[0], gc[1], transforms.size() - gc[0] - gc[1], int(gpu->GetStats().GPU), different);
            if (!selectedTriangles) selectedTriangles = uint64_t(gc[0]) * cpu->Parts[0][0].Geometry->GetTriangleCount() + uint64_t(gc[1]) * cpu->Parts[1][0].Geometry->GetTriangleCount();
        }
        Check(gpu->GetStats().GPU == bool(GLAD_GL_VERSION_4_3), "capability selects GPU or CPU fallback");
        Check(gpu->GetStats().Uploads == uploads, "camera motion does not upload instance source");
        draw(gpu, { 0, 12, 40 }, true);
        draw(gpu, { 0, 12, 40 }, false, WireframeMode::Wire);
        draw(gpu, { 0, 12, 40 }, false, WireframeMode::ShadedWire);
        target->Bind();
        bool picked = false;
        for (int y = 24; y < 170; y += 12)
            for (int x = 24; x < 170; x += 12) picked |= target->ReadPixel(1, x, y) == 42;
        target->Unbind();
        Check(picked, "packed component picking ID");
        transforms[0] = glm::translate(glm::mat4(1), { 0, 2, 0 });
        Check(gpu->Upload(transforms, box, 42), "dirty instance update");
        Check(gpu->GetStats().Uploads == uploads + 1, "changed source uploaded once");
        gpu->Settings.Backend = FoliageBackend::CPU;
        draw(gpu, { 0, 12, 40 });
        Check(!gpu->GetStats().GPU, "backend switch to CPU");
        gpu->Settings.Backend = FoliageBackend::Auto;
        draw(gpu, { 0, 12, 40 });
        Check(gpu->GetStats().GPU == bool(GLAD_GL_VERSION_4_3), "backend switch back");
        // Multi-part models propagate the GPU count into every indirect command.
        for (auto batch : { cpu, gpu })
        {
            batch->Parts[1].push_back({ MeshLibrary::GetPrimitive(PrimitiveMesh::Sphere), mat });
            Check(batch->Upload(transforms, box, 42), "model part change invalidates cached commands/VAOs");
        }
        auto multiCPU = draw(cpu, { 0, 12, 40 }), multiGPU = draw(gpu, { 0, 12, 40 });
        size_t multiDifference = 0;
        for (size_t p = 0; p < multiCPU.size(); p += 4)
        {
            bool differs = false;
            for (int c = 0; c < 3; ++c) differs |= std::abs(int(multiCPU[p + c]) - int(multiGPU[p + c])) > 8;
            multiDifference += differs;
        }
        Check(multiDifference * 200 <= multiCPU.size() / 4, "multi-part indirect draw image matches CPU");
        Check(cpu->DebugVisibleCounts() == gpu->DebugVisibleCounts(), "multi-part visibility matches");
        auto invalid = CreateRef<FoliageBatch>();
        Check(!invalid->Upload(transforms, box, 1), "empty LOD rejected");
        invalid->Parts[0] = cpu->Parts[0];
        invalid->Parts[1] = cpu->Parts[1];
        transforms[0] = glm::scale(transforms[0], glm::vec3(-1, 1, 1));
        Check(!invalid->Upload(transforms, box, 1), "mixed winding rejected");
        // ---- HLOD + the culled shadow set: CPU and GPU agree per LOD, per shadow LOD and per HLOD cell.
        auto hcpu = CreateRef<FoliageBatch>(), hgpu = CreateRef<FoliageBatch>();
        for (auto batch : { hcpu, hgpu })
        {
            batch->Parts[0].push_back({ MeshLibrary::GetPrimitive(PrimitiveMesh::Cone), mat });
            batch->Parts[1].push_back({ MeshLibrary::GetPrimitive(PrimitiveMesh::Cube), mat });
            batch->Settings.LODDistance = 12;
            batch->Settings.CullDistance = 90;
            batch->Settings.HLODDistance = 22;
            batch->Settings.HLODFade = 8;
            batch->Settings.HLODCellSize = 8;
        }
        hcpu->Settings.Backend = FoliageBackend::CPU;
        hgpu->Settings.Backend = FoliageBackend::GPU;
        transforms[0] = glm::translate(glm::mat4(1), { -32, 0, -32 });
        Check(hcpu->Upload(transforms, box, 7) && hgpu->Upload(transforms, box, 7), "upload HLOD batches");
        Check(hcpu->GetHLODMesh() && hgpu->GetHLODMesh(), "HLOD proxy mesh baked");
        Check(hcpu->GetStats().HLODHash == hgpu->GetStats().HLODHash && hcpu->GetStats().HLODCells == 64, "HLOD bake is the same on both backends");
        auto drawH = [&](const Ref<FoliageBatch>& batch, glm::vec3 eye, bool sun) {
            target->Bind();
            RenderCommand::SetClearColor({ 0, 0, 0, 1 });
            RenderCommand::Clear();
            target->ClearAttachment(1, -1);
            SceneEnvironment env;
            env.DrawSky = false;
            env.DebugView = RenderDebugView::Albedo;
            env.HasSun = sun;
            env.Shadows.AtlasSize = 1024;
            Renderer::ResetStats();
            Renderer3D::BeginScene(glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0)), glm::perspective(glm::radians(55.f), 1.f, .1f, 300.f), eye, env);
            Renderer3D::SubmitFoliage(batch);
            Renderer3D::EndScene();
            auto pixels = target->ReadColorPixels();
            target->Unbind();
            Check(glGetError() == GL_NO_ERROR, "GL state/error (HLOD)");
            return pixels;
        };
        uint32_t fadingSeen = 0, replacedSeen = 0, shadowCulledSeen = 0;
        for (glm::vec3 eye : { glm::vec3(0, 10, 40), glm::vec3(-30, 14, 45), glm::vec3(0, 30, 0.01f), glm::vec3(35, 6, -10), glm::vec3(0, 60, 120) })
        {
            auto reference = drawH(hcpu, eye, true), actual = drawH(hgpu, eye, true);
            const auto cc = hcpu->DebugVisibleCounts(), gc = hgpu->DebugVisibleCounts();
            const auto cs = hcpu->DebugShadowCounts(), gs = hgpu->DebugShadowCounts();
            const auto& a = hcpu->GetStats();
            const auto& b = hgpu->GetStats();
            Check(cc == gc, "HLOD: GPU and CPU camera counts per LOD match");
            Check(cs == gs, "HLOD: GPU and CPU shadow counts per LOD match");
            Check(a.HLODInstances == b.HLODInstances && a.HLODCellsDrawn == b.HLODCellsDrawn && a.HLODCellsFading == b.HLODCellsFading, "HLOD cells match");
            Check(cc[0] + cc[1] + a.HLODInstances <= 4096, "a replaced instance is not drawn");
            size_t different = 0;
            for (size_t p = 0; p < actual.size(); p += 4)
            {
                bool differs = false;
                for (int c = 0; c < 3; ++c) differs |= std::abs(int(actual[p + c]) - int(reference[p + c])) > 8;
                different += differs;
            }
            Check(different * 200 <= actual.size() / 4, "HLOD: GPU/CPU rendered images match within 0.5 percent");
            fadingSeen += a.HLODCellsFading;
            replacedSeen += a.HLODInstances;
            shadowCulledSeen += 4096 - std::min<uint32_t>(4096, cs[0] + cs[1]);
            std::printf("hlod view %.0f,%.0f,%.0f: near=%u far=%u hlod-instances=%u cells=%u fading=%u shadow=%u/%u GPU=%d pixel-diff=%zu\n", eye.x, eye.y, eye.z, gc[0], gc[1],
                        b.HLODInstances, b.HLODCellsDrawn, b.HLODCellsFading, gs[0], gs[1], int(b.GPU), different);
        }
        Check(fadingSeen > 0 && replacedSeen > 0, "the views cover the HLOD crossfade and replaced cells");
        Check(shadowCulledSeen > 0, "the shadow set is culled by the cascades' volumes");
        drawH(hgpu, { 0, 10, 40 }, true);
        hgpu->DebugResolveTimings();
        Check(!hgpu->GetStats().GPU || hgpu->GetStats().GPUTimeAvailable, "GPU cull timestamp result");
        std::printf("GPU cull %.3f ms, CPU cull %.3f ms (CPU path), CPU issue %.3f ms (GPU path)\n", hgpu->GetStats().CullGPUMilliseconds, hcpu->GetStats().CullCPUMilliseconds,
                    hgpu->GetStats().CullCPUMilliseconds);
        // The LOD/HLOD debug view shows all three kinds.
        Renderer3D::SetFoliageLODDebugView(true);
        auto debugPixels = drawH(hgpu, { 0, 6, 36 }, false);
        Renderer3D::SetFoliageLODDebugView(false);
        uint32_t green = 0, yellow = 0, magenta = 0;
        for (size_t p = 0; p < debugPixels.size(); p += 4)
        {
            const int r = debugPixels[p], g = debugPixels[p + 1], bl = debugPixels[p + 2];
            green += g > 150 && r < 120 && bl < 120;
            yellow += r > 180 && g > 150 && bl < 80;
            magenta += r > 150 && g < 100 && bl > 130;
        }
        std::printf("debug view: green=%u yellow=%u magenta=%u\n", green, yellow, magenta);
        Check(green > 20 && yellow > 20 && magenta > 20, "debug view colours near/far/HLOD");
        // A paint edit in one corner rebakes only the touched cells.
        const uint64_t hashBefore = hgpu->GetStats().HLODHash;
        for (auto& m : transforms)
            if (m[3].x < -26 && m[3].z < -26) m[3].y += 1;
        Check(hgpu->Upload(transforms, box, 7), "edited upload");
        drawH(hgpu, { 0, 10, 40 }, false);
        Check(hgpu->GetStats().HLODHash != hashBefore && hgpu->GetStats().HLODRebakedCells == 1 && hgpu->GetStats().HLODReusedCells == 63, "edit rebakes one cell");
        // Turning HLOD off frees the proxies; every instance comes back to the LOD sets.
        hgpu->Settings.HLODDistance = 0;
        drawH(hgpu, { 0, 30, 0.01f }, false);
        Check(!hgpu->GetHLODMesh() && hgpu->GetStats().HLODInstances == 0, "HLOD off");
        std::printf("PASS packed foliage: 4096 instances, full near triangles=%llu first-view selected triangles=%llu, no per-frame source upload\n", (unsigned long long)baselineTriangles, (unsigned long long)selectedTriangles);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
    return 0;
}
