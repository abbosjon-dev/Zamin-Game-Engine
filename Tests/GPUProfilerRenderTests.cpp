#include <Zamin/Zamin.h>
#include <Zamin/Renderer/GPUProfiler.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace Zamin;
class GPUProbeLayer : public Layer
{
public:
    void OnUpdate(Timestep) override
    {
        ++Frames;
        if (Frames == 8) GPUProfiler::SetEnabled(false);
        if (Frames == 13) GPUProfiler::SetEnabled(true);
        RenderCommand::SetClearColor({ .1f, .2f, .3f, 1 });
        RenderCommand::Clear();
        SceneEnvironment env;
        env.DrawSky = false;
        Renderer3D::BeginScene(glm::lookAt(glm::vec3(0, 1, 4), glm::vec3(0), glm::vec3(0, 1, 0)), glm::perspective(glm::radians(45.f), 1.f, .1f, 100.f), { 0, 1, 4 }, env);
        for (int i = 0; i < 64; ++i) Renderer3D::SubmitMesh(MeshLibrary::GetPrimitive(PrimitiveMesh::Cube), glm::translate(glm::mat4(1), glm::vec3(float(i % 8) * .1f, float(i / 8) * .1f, 0)), Material{}, i);
        Renderer3D::EndScene();
        if (GPUProfiler::GetStats().PendingQueries > 8) Bad = true;
    }
    int Frames = 0;
    bool Bad = false;
};
class GPUProbeApp : public Application
{
public:
    using Application::Application;
};
int main(int argc, char** argv)
{
    setenv("ZAMIN_MAX_FRAMES", "40", 1);
    ApplicationSpecification spec;
    spec.EnableImGui = false;
    spec.Name = "GPU timestamp tests";
    spec.Window.Width = spec.Window.Height = 256;
    spec.Window.VSync = false;
    spec.CommandLineArgs = { argc, argv };
    GPUProbeApp app(spec);
    auto* probe = new GPUProbeLayer();
    app.PushLayer(probe);
    app.Run();
    auto s = GPUProfiler::GetStats();
    if (!s.Supported || s.CompletedSamples == 0 || !s.Enabled || s.PendingQueries > 8 || probe->Bad || !std::isfinite(s.AverageMilliseconds) || s.MinimumMilliseconds < 0 || s.MinimumMilliseconds > s.MaximumMilliseconds)
    {
        std::fprintf(stderr, "FAIL GPU profiler: supported=%d completed=%llu pending=%u\n", int(s.Supported), (unsigned long long)s.CompletedSamples, s.PendingQueries);
        return 1;
    }
    std::printf("PASS GPU profiler: samples=%llu last=%.3f average=%.3f min/max=%.3f/%.3f pending=%u dropped=%llu\n", (unsigned long long)s.CompletedSamples, s.LastMilliseconds, s.AverageMilliseconds, s.MinimumMilliseconds, s.MaximumMilliseconds, s.PendingQueries, (unsigned long long)s.DroppedSamples);
    return 0;
}
