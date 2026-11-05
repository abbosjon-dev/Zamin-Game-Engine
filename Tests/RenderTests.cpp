// Zamin rendering regression test: draws reference scenes offscreen (a fixed size, a fixed camera,
// nothing updated) and compares each picture with the stored one in Tests/Reference/<name>.png.
// Needs a GL context, so a display (xvfb in CI) - the unit tests (ZaminTests) stay GL-free.
//
//   ZaminRenderTests [--update] [--filter <text>] [--list] [--out <dir>] [--reference <dir>]
//                    [--threshold <0..255>] [--max-percent <p>]
//
//   --update        writes the pictures as the new references (after an intended change of the look)
//   --filter        only the cases whose name contains the text
//   --threshold     a pixel differs when one colour channel differs by more than this (default 8)
//   --max-percent   a case fails when more than this share of its pixels differ (default 0.5)
// On a failure <out>/<name>.png (the new picture) and <name>.diff.png (red: what differs) are
// written. Exit code 0: every case matched.
#include <Zamin/Zamin.h>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifndef ZAMIN_SOURCE_DIR
#define ZAMIN_SOURCE_DIR "."
#endif
#ifndef ZAMIN_RENDER_TEST_OUTPUT
#define ZAMIN_RENDER_TEST_OUTPUT "RenderTests"
#endif

namespace fs = std::filesystem;
using namespace Zamin;

namespace {

    constexpr uint32_t s_Width = 480, s_Height = 270;

    // One picture to check.
    struct RenderCase
    {
        std::string Name;
        std::string Description;               // what it covers (printed with --list)
        std::function<Ref<Scene>()> Build;
        EditorCameraState Camera;
        std::string Reference;                 // another case's reference it must match (empty: its own)
        SceneRenderOptions Options;
        bool NoCullingOrInstancing = false;    // drawn without frustum culling and GPU instancing
        // The editor's selection outline over the picture: these entities (by name, with their
        // children; the last one is the primary selection) and the one under the mouse.
        std::vector<std::string> Outline;
        std::string OutlineHover;
    };

    // ------------------------------------------------------------------ scene building helpers
    Entity AddMesh(const Ref<Scene>& scene, const std::string& name, PrimitiveMesh type, const glm::vec3& position,
                   const glm::vec3& scale, const glm::vec4& albedo, float metallic = 0.0f, float roughness = 0.5f,
                   const glm::vec3& rotation = glm::vec3(0.0f))
    {
        Entity e = scene->CreateEntity(name);
        auto& t = e.GetTransform();
        t.Translation = position;
        t.Rotation = rotation;
        t.Scale = scale;
        auto& mr = e.AddComponent<MeshRendererComponent>();
        mr.Type = type;
        mr.Mat.Albedo = albedo;
        mr.Mat.Metallic = metallic;
        mr.Mat.Roughness = roughness;
        Scene::ResolveMeshRenderer(mr);
        return e;
    }

    Entity AddSun(const Ref<Scene>& scene, const std::string& name, const glm::vec3& rotation, const glm::vec3& color, float intensity,
                  bool shadows)
    {
        Entity e = scene->CreateEntity(name);
        e.GetTransform().Rotation = rotation;
        auto& l = e.AddComponent<DirectionalLightComponent>();
        l.Color = color;
        l.Intensity = intensity;
        l.CastShadows = shadows;
        return e;
    }

    Entity AddPointLight(const Ref<Scene>& scene, const glm::vec3& position, const glm::vec3& color, float intensity, float radius)
    {
        Entity e = scene->CreateEntity("Point");
        e.GetTransform().Translation = position;
        auto& l = e.AddComponent<PointLightComponent>();
        l.Color = color;
        l.Intensity = intensity;
        l.Radius = radius;
        return e;
    }

    Entity AddSpotLight(const Ref<Scene>& scene, const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& color,
                        float intensity, float range, float inner, float outer)
    {
        Entity e = scene->CreateEntity("Spot");
        e.GetTransform().Translation = position;
        e.GetTransform().Rotation = rotation;
        auto& l = e.AddComponent<SpotLightComponent>();
        l.Color = color;
        l.Intensity = intensity;
        l.Range = range;
        l.InnerAngle = inner;
        l.OuterAngle = outer;
        return e;
    }

    EditorCameraState LookAt(const glm::vec3& focal, float distance, float pitchDegrees, float yawDegrees, bool ortho = false)
    {
        EditorCameraState s;
        s.FocalPoint = focal;
        s.Distance = distance;
        s.Pitch = glm::radians(pitchDegrees);
        s.Yaw = glm::radians(yawDegrees);
        s.Orthographic = ortho;
        return s;
    }

    // A ground, a few primitives (one metal), the sun with shadows, a point and a spot light.
    Ref<Scene> BuildLitScene()
    {
        auto scene = CreateRef<Scene>();
        scene->SetName("Lit3D");
        AddMesh(scene, "Ground", PrimitiveMesh::Plane, { 0, 0, 0 }, { 30, 1, 30 }, { 0.62f, 0.62f, 0.64f, 1 }, 0.0f, 0.8f);
        AddMesh(scene, "Box", PrimitiveMesh::Cube, { -2.2f, 0.75f, 0.5f }, { 1.5f, 1.5f, 1.5f }, { 0.85f, 0.3f, 0.25f, 1 }, 0.0f, 0.45f,
                { 0, 0.5f, 0 });
        AddMesh(scene, "Gold", PrimitiveMesh::Sphere, { 0.4f, 1.0f, 0.0f }, { 2, 2, 2 }, { 0.95f, 0.75f, 0.35f, 1 }, 1.0f, 0.25f);
        AddMesh(scene, "Column", PrimitiveMesh::Cylinder, { 2.8f, 1.0f, -1.2f }, { 1, 2, 1 }, { 0.3f, 0.6f, 0.85f, 1 }, 0.0f, 0.6f);
        AddMesh(scene, "Cone", PrimitiveMesh::Cone, { 2.4f, 0.6f, 2.0f }, { 1.2f, 1.2f, 1.2f }, { 0.4f, 0.8f, 0.35f, 1 }, 0.0f, 0.5f);
        AddSun(scene, "Sun", { -0.9f, 0.6f, 0.0f }, { 1.0f, 0.96f, 0.9f }, 3.0f, true);
        AddPointLight(scene, { -1.0f, 1.2f, 2.4f }, { 1.0f, 0.55f, 0.2f }, 12.0f, 6.0f);
        AddSpotLight(scene, { 3.5f, 4.5f, 1.5f }, { -1.2f, 0.4f, 0.0f }, { 0.3f, 0.8f, 1.0f }, 60.0f, 12.0f, 12.0f, 22.0f);
        return scene;
    }

    // Transparent, emissive, unlit and double-sided surfaces, a row of instanced cubes, fog, bloom.
    Ref<Scene> BuildMaterialsScene()
    {
        auto scene = CreateRef<Scene>();
        scene->SetName("Materials");
        auto& s = scene->GetSettings();
        s.FogEnabled = true;
        s.FogDensity = 0.012f;
        s.Post.Bloom = true;
        AddMesh(scene, "Ground", PrimitiveMesh::Plane, { 0, 0, 0 }, { 40, 1, 40 }, { 0.5f, 0.52f, 0.55f, 1 }, 0.0f, 0.9f);
        for (int i = 0; i < 9; i++) // identical: drawn as instances of one call
            AddMesh(scene, "Crate", PrimitiveMesh::Cube, { -6.0f + i * 1.5f, 0.5f, -4.0f }, { 1, 1, 1 }, { 0.7f, 0.55f, 0.35f, 1 }, 0.0f,
                    0.7f);
        Entity glass = AddMesh(scene, "Glass", PrimitiveMesh::Sphere, { -1.5f, 1.2f, 0.5f }, { 2.2f, 2.2f, 2.2f }, { 0.4f, 0.7f, 1.0f, 0.3f },
                               0.0f, 0.05f);
        glass.GetComponent<MeshRendererComponent>().Mat.BlendMode = MaterialBlendMode::Transparent;
        Entity tinted = AddMesh(scene, "Tinted", PrimitiveMesh::Cube, { 0.2f, 1.0f, 1.8f }, { 1.6f, 2.0f, 0.2f }, { 1.0f, 0.3f, 0.3f, 0.5f });
        tinted.GetComponent<MeshRendererComponent>().Mat.BlendMode = MaterialBlendMode::Transparent;
        tinted.GetComponent<MeshRendererComponent>().Mat.DoubleSided = true;
        Entity lamp = AddMesh(scene, "Lamp", PrimitiveMesh::Cube, { 2.5f, 0.6f, 0.0f }, { 1.2f, 1.2f, 1.2f }, { 0.1f, 0.1f, 0.1f, 1 });
        lamp.GetComponent<MeshRendererComponent>().Mat.Emission = { 1.0f, 0.6f, 0.2f };
        lamp.GetComponent<MeshRendererComponent>().Mat.EmissionStrength = 6.0f;
        Entity flat = AddMesh(scene, "Unlit", PrimitiveMesh::Cylinder, { 4.5f, 1.0f, 1.5f }, { 1, 2, 1 }, { 0.2f, 0.9f, 0.5f, 1 });
        flat.GetComponent<MeshRendererComponent>().Mat.Shader = MaterialShader::Unlit;
        Entity sheet = AddMesh(scene, "Sheet", PrimitiveMesh::Plane, { -4.5f, 1.2f, 1.0f }, { 2, 1, 2 }, { 0.9f, 0.9f, 0.3f, 1 }, 0.0f, 0.5f,
                               { 1.2f, 0.6f, 0.0f });
        sheet.GetComponent<MeshRendererComponent>().Mat.DoubleSided = true;
        AddSun(scene, "Sun", { -0.7f, -0.8f, 0.0f }, { 1.0f, 0.95f, 0.85f }, 2.5f, true);
        return scene;
    }

    // Two directional lights (the second one, with shadows, is the sun), a third without shadows,
    // a custom gradient and a large sun disc.
    Ref<Scene> BuildSkyScene()
    {
        auto scene = CreateRef<Scene>();
        scene->SetName("Sky");
        auto& s = scene->GetSettings();
        s.SkyTopColor = { 0.12f, 0.2f, 0.45f };
        s.SkyHorizonColor = { 0.98f, 0.62f, 0.35f };
        s.GroundColor = { 0.25f, 0.18f, 0.15f };
        s.Sky.Curve = 0.35f;
        s.Sky.GroundFade = 8.0f;
        s.Sky.SunSize = 5.0f;
        s.Sky.SunIntensity = 3.0f;
        s.Sky.SunGlow = 0.4f;
        AddMesh(scene, "Ground", PrimitiveMesh::Plane, { 0, 0, 0 }, { 400, 1, 400 }, { 0.6f, 0.6f, 0.6f, 1 }, 0.0f, 0.8f);
        AddMesh(scene, "Ball", PrimitiveMesh::Sphere, { 0, 1, 0 }, { 2, 2, 2 }, { 0.9f, 0.9f, 0.92f, 1 }, 0.0f, 0.4f);
        AddMesh(scene, "Block", PrimitiveMesh::Cube, { 2.5f, 0.75f, -1.0f }, { 1.5f, 1.5f, 1.5f }, { 0.9f, 0.9f, 0.92f, 1 }, 0.0f, 0.6f);
        AddSun(scene, "Fill (no shadows)", { -0.5f, -2.2f, 0.0f }, { 0.4f, 0.55f, 1.0f }, 1.2f, false);
        AddSun(scene, "Sunset", { -0.2f, glm::pi<float>() + 0.25f, 0.0f }, { 1.0f, 0.7f, 0.45f }, 3.0f, true);
        AddSun(scene, "Rim (shadows ignored)", { -0.6f, 2.6f, 0.0f }, { 0.6f, 1.0f, 0.6f }, 0.8f, true);
        return scene;
    }

    // The lit scene through every effect: SSAO, bloom with a tint and lens dirt, chromatic
    // aberration, vignette, film grain (a still pattern: no view history), FXAA.
    Ref<Scene> BuildPostScene()
    {
        Ref<Scene> scene = BuildLitScene();
        scene->SetName("PostEffects");
        Entity lamp = AddMesh(scene, "Hot", PrimitiveMesh::Sphere, { -0.6f, 2.6f, 1.6f }, { 0.6f, 0.6f, 0.6f }, { 0, 0, 0, 1 });
        lamp.GetComponent<MeshRendererComponent>().Mat.Emission = { 1.0f, 0.9f, 0.7f };
        lamp.GetComponent<MeshRendererComponent>().Mat.EmissionStrength = 20.0f;
        auto& p = scene->GetSettings().Post;
        p.SSAO = true;
        p.Bloom = true;
        p.BloomIntensity = 0.8f;
        p.BloomTint = { 1.0f, 0.75f, 0.5f };
        p.BloomDirt = 0.8f;
        p.ChromaticAberration = 0.6f;
        p.Vignette = 0.4f;
        p.FilmGrain = 0.35f;
        p.FilmGrainSize = 1.5f;
        p.FXAA = true;
        return scene;
    }

    // Sprites (rotated, sorted, transparent), circles and rings, and text - drawn by Renderer2D.
    Ref<Scene> BuildSpritesScene()
    {
        auto scene = CreateRef<Scene>();
        scene->SetName("Sprites2D");
        auto& s = scene->GetSettings();
        s.DrawSky = false;
        s.Post.Tonemap = TonemapMode::None;
        for (int i = 0; i < 6; i++)
        {
            Entity e = scene->CreateEntity("Sprite");
            auto& t = e.GetTransform();
            t.Translation = { -5.0f + i * 2.0f, -1.5f + 0.4f * i, 0.0f };
            t.Rotation.z = 0.25f * i;
            t.Scale = { 1.6f, 1.6f, 1.0f };
            auto& sr = e.AddComponent<SpriteRendererComponent>();
            sr.Color = { 0.2f + 0.13f * i, 0.8f - 0.1f * i, 0.4f + 0.08f * i, i % 2 ? 0.6f : 1.0f };
            sr.SortingOrder = i % 3;
        }
        for (int i = 0; i < 4; i++)
        {
            Entity e = scene->CreateEntity("Circle");
            e.GetTransform().Translation = { -4.5f + i * 3.0f, 2.2f, 0.0f };
            e.GetTransform().Scale = { 1.8f, 1.8f, 1.0f };
            auto& c = e.AddComponent<CircleRendererComponent>();
            c.Color = { 1.0f, 0.8f - 0.15f * i, 0.2f + 0.2f * i, 1.0f };
            c.Thickness = i % 2 ? 0.25f : 1.0f;
            c.Fade = i == 3 ? 0.2f : 0.005f;
        }
        Entity text = scene->CreateEntity("Text");
        text.GetTransform().Translation = { -4.0f, -3.6f, 0.0f };
        text.GetTransform().Scale = { 0.8f, 0.8f, 1.0f };
        text.AddComponent<TextComponent>().Text = "Zamin 2D: sprite, doira, matn";
        return scene;
    }

    // ------------------------------------------------------------------ the sample projects
    // A scene of one of the sample projects as it is (models, textures, HDRI, particles and
    // animators as loaded - nothing is updated).
    std::function<Ref<Scene>()> ProjectScene(const std::string& project, const std::string& scenePath)
    {
        return [project, scenePath]() -> Ref<Scene> {
            fs::path dir = fs::path(ZAMIN_SOURCE_DIR) / "Projects" / project;
            fs::path file;
            std::error_code ec;
            for (auto& entry : fs::directory_iterator(dir, ec))
                if (entry.path().extension() == ".zproject")
                    file = entry.path();
            if (file.empty() || !Project::Load(file))
            {
                ZM_ERROR("Loyiha yuklanmadi: {}", dir.string());
                return nullptr;
            }
            auto scene = CreateRef<Scene>();
            SceneSerializer serializer(scene);
            if (!serializer.Deserialize(Project::GetAssetFileSystemPath(scenePath)))
            {
                ZM_ERROR("Sahna yuklanmadi: {} ({})", scenePath, serializer.GetLastError());
                return nullptr;
            }
            scene->ResolveAssets();
            return scene;
        };
    }

    std::vector<RenderCase> MakeCases()
    {
        std::vector<RenderCase> cases;
        cases.push_back({ "Lit3D", "PBR primitivlar, quyosh soyasi (kaskadlar), nuqtali va spot yorug'lik, gradient osmon",
                          BuildLitScene, LookAt({ 0, 1, 0 }, 11.0f, 25.0f, 30.0f) });
        {
            RenderCase c{ "Lit3D_NoCulling", "Lit3D: frustum culling va instancing o'chiq - bir xil chiqishi kerak", BuildLitScene,
                          LookAt({ 0, 1, 0 }, 11.0f, 25.0f, 30.0f), "Lit3D" };
            c.NoCullingOrInstancing = true;
            cases.push_back(c);
        }
        cases.push_back({ "Materials", "Shaffof (saralangan), emissive + bloom, unlit, ikki tomonlama, instanced qator, tuman",
                          BuildMaterialsScene, LookAt({ 0, 1, 0 }, 13.0f, 20.0f, -15.0f) });
        cases.push_back({ "Sky", "Bir nechta quyosh (faqat biri soya), osmon gradienti egriligi va katta quyosh diski",
                          BuildSkyScene, LookAt({ 0, 2.5f, 0 }, 10.0f, -2.0f, 0.0f) });
        cases.push_back({ "PostEffects", "SSAO, bloom rangi va linza changi, xromatik aberratsiya, vinyetka, plyonka donasi, FXAA",
                          BuildPostScene, LookAt({ 0, 1, 0 }, 11.0f, 25.0f, 30.0f) });
        {
            RenderCase c{ "DebugNormals", "Debug ko'rinish: normallar (tonemapping'siz)", BuildLitScene,
                          LookAt({ 0, 1, 0 }, 11.0f, 25.0f, 30.0f) };
            c.Options.DebugView = RenderDebugView::Normals;
            cases.push_back(c);
        }
        cases.push_back({ "Sprites2D", "Renderer2D: sprite'lar (aylangan, shaffof, saralangan), doira va halqalar, matn",
                          BuildSpritesScene, LookAt({ 0, 0, 0 }, 9.0f, 0.0f, 0.0f, true) });
        cases.push_back({ "Showcase3D", "Showcase3D namunasi: modellar, teksturalar, HDRI, skinned mesh",
                          ProjectScene("Showcase3D", "Scenes/Showcase.zscene"), LookAt({ 0, 1.5f, 0 }, 22.0f, 22.0f, 20.0f) });
        cases.push_back({ "Platformer2D", "Platformer2D namunasi: tilemap, sprite'lar, matn",
                          ProjectScene("Platformer2D", "Scenes/Level1.zscene"), LookAt({ 8, 3, 0 }, 14.0f, 0.0f, 0.0f, true) });
        {
            RenderCase c{ "SelectionOutline", "Editor tanlash konturi: skinned model, boshqa obyekt orqasidagi sfera (xira qismi), "
                          "ko'p tanlov, hover, matn", ProjectScene("Showcase3D", "Scenes/Showcase.zscene"),
                          LookAt({ -1.5f, 1.2f, 0.0f }, 15.0f, 16.0f, -38.0f) };
            c.Outline = { "Tentacle", "Title", "Cube 5", "Sphere 6" }; // Sphere 6 behind the hovered Hero Sphere
            c.OutlineHover = "Hero Sphere";
            cases.push_back(c);
        }
        return cases;
    }

    // ------------------------------------------------------------------ rendering
    // The case's picture as RGBA8, rows top first (empty when the scene could not be built).
    std::vector<uint8_t> Render(const RenderCase& c)
    {
        Ref<Scene> scene = c.Build();
        if (!scene)
            return {};
        scene->OnViewportResize(s_Width, s_Height);

        EditorCamera camera;
        camera.SetViewportSize((float)s_Width, (float)s_Height);
        camera.SetViewState(c.Camera);

        const glm::uvec2 size = Renderer::GetRenderResolution(s_Width, s_Height, scene->GetSettings().RenderScale);
        FramebufferSpecification hdrSpec;
        hdrSpec.Width = size.x;
        hdrSpec.Height = size.y;
        hdrSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        Ref<Framebuffer> hdr = Framebuffer::Create(hdrSpec);
        FramebufferSpecification outSpec;
        outSpec.Width = s_Width;
        outSpec.Height = s_Height;
        outSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
        Ref<Framebuffer> output = Framebuffer::Create(outSpec);

        // The outlined entities (as the editor collects them) and their meshes' silhouettes.
        std::vector<OutlineEntity> outline;
        auto id = [](Entity e) { return (int)(uint32_t)(entt::entity)e; };
        for (size_t i = 0; i < c.Outline.size(); i++)
            if (Entity e = scene->FindEntityByName(c.Outline[i]))
            {
                outline.push_back({ id(e), i + 1 == c.Outline.size() ? OutlineCategory::Primary : OutlineCategory::Secondary });
                for (Entity child : scene->GetDescendants(e))
                    outline.push_back({ id(child), OutlineCategory::Child });
            }
        if (!c.OutlineHover.empty())
            if (Entity e = scene->FindEntityByName(c.OutlineHover))
                outline.push_back({ id(e), OutlineCategory::None, true });
        if (!outline.empty())
        {
            std::vector<int> ids;
            for (const OutlineEntity& e : outline)
                ids.push_back(e.EntityID);
            Renderer3D::BeginCapture(ids);
        }

        Renderer3D::SetFrustumCulling(!c.NoCullingOrInstancing);
        Renderer3D::SetInstancing(!c.NoCullingOrInstancing);
        Renderer::ResetStats();
        hdr->Bind();
        glm::vec4 clear = scene->GetClearColor();
        RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), 1.0f });
        RenderCommand::Clear();
        hdr->ClearAttachment(1, -1);
        scene->OnRenderEditor(camera, c.Options);
        hdr->Unbind();
        Renderer3D::EndCapture();

        PostProcessInput input;
        input.ColorTexture = hdr->GetColorAttachmentRendererID(0);
        input.Width = size.x;
        input.Height = size.y;
        input.DepthTexture = hdr->GetDepthAttachmentRendererID();
        input.Projection = camera.GetProjection();
        input.OutputWidth = s_Width;
        input.OutputHeight = s_Height;
        const PostProcessSettings post = Renderer3D::DebugViewUsesPostProcessing(c.Options.DebugView) ? scene->GetSettings().Post
                                                                                                       : PostProcessSettings::Neutral();
        PostProcessor::Apply(input, post, output);
        Renderer3D::SetFrustumCulling(true);
        Renderer3D::SetInstancing(true);
        if (!outline.empty())
        {
            SelectionOutlineInput selection;
            selection.EntityIDTexture = hdr->GetColorAttachmentRendererID(1);
            selection.IDWidth = size.x;
            selection.IDHeight = size.y;
            selection.Width = s_Width;
            selection.Height = s_Height;
            selection.ViewProjection = scene->GetLastProjection() * scene->GetLastView();
            selection.Entities = outline;
            selection.Silhouettes = &Renderer3D::GetCapturedDraws();
            output->Bind();
            SelectionOutline::Render(selection, SelectionOutlineSettings());
            output->Unbind();
        }

        std::vector<uint8_t> pixels = output->ReadColorPixels(0); // bottom row first
        std::vector<uint8_t> flipped(pixels.size());
        const size_t row = (size_t)s_Width * 4;
        for (uint32_t y = 0; y < s_Height && pixels.size() == row * s_Height; y++)
            std::memcpy(&flipped[(size_t)y * row], &pixels[(size_t)(s_Height - 1 - y) * row], row);
        for (size_t i = 3; i < flipped.size(); i += 4)
            flipped[i] = 255;
        return flipped;
    }

    bool Contains(const std::string& text, const std::string& part) { return part.empty() || text.find(part) != std::string::npos; }

    // A window to own the GL context (no ImGui, nothing drawn into it).
    class RenderTestApp : public Application
    {
    public:
        explicit RenderTestApp(const ApplicationSpecification& spec) : Application(spec) {}
    };

}

int main(int argc, char** argv)
{
    bool update = false, list = false;
    std::string filter;
    fs::path outDir = ZAMIN_RENDER_TEST_OUTPUT;
    fs::path referenceDir = fs::path(ZAMIN_SOURCE_DIR) / "Tests" / "Reference";
    int threshold = 8;
    double maxPercent = 0.5;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--update") update = true;
        else if (arg == "--list") list = true;
        else if (arg == "--filter") filter = next();
        else if (arg == "--out") outDir = next();
        else if (arg == "--reference") referenceDir = next();
        else if (arg == "--threshold") threshold = std::atoi(next().c_str());
        else if (arg == "--max-percent") maxPercent = std::atof(next().c_str());
        else
        {
            std::printf("Noma'lum argument: %s\n"
                        "Foydalanish: ZaminRenderTests [--update] [--filter matn] [--list] [--out papka] [--reference papka]\n"
                        "                              [--threshold 0..255] [--max-percent foiz]\n", arg.c_str());
            return 2;
        }
    }

    std::vector<RenderCase> cases = MakeCases();
    if (list)
    {
        for (const RenderCase& c : cases)
            std::printf("%-16s %s%s\n", c.Name.c_str(), c.Description.c_str(),
                        c.Reference.empty() ? "" : (" (etalon: " + c.Reference + ")").c_str());
        return 0;
    }

    ApplicationSpecification spec;
    spec.Name = "Zamin Render Tests";
    spec.EnableImGui = false;
    spec.Window.Width = 640;
    spec.Window.Height = 360;
    spec.Window.VSync = false;
    spec.CommandLineArgs = { argc, argv };
    auto* app = new RenderTestApp(spec);

    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (update)
        fs::create_directories(referenceDir, ec);

    int failed = 0, checked = 0;
    for (const RenderCase& c : cases)
    {
        if (!Contains(c.Name, filter))
            continue;
        checked++;
        std::vector<uint8_t> pixels = Render(c);
        if (pixels.empty())
        {
            std::printf("[XATO] %-16s sahna tayyorlanmadi\n", c.Name.c_str());
            failed++;
            continue;
        }
        const std::string referenceName = c.Reference.empty() ? c.Name : c.Reference;
        const fs::path reference = referenceDir / (referenceName + ".png");
        if (update && c.Reference.empty())
        {
            if (Image::WritePNG(reference.string(), s_Width, s_Height, 4, pixels.data()))
                std::printf("[YANGI] %-16s etalon yozildi: %s\n", c.Name.c_str(), reference.string().c_str());
            else
            {
                std::printf("[XATO] %-16s etalon yozilmadi: %s\n", c.Name.c_str(), reference.string().c_str());
                failed++;
            }
            continue;
        }

        ImageData expected;
        std::string error;
        if (!Image::Load(reference.string(), expected, 4, false, &error))
        {
            std::printf("[XATO] %-16s etalon yo'q yoki o'qilmadi (%s): %s - '--update' bilan yarating\n", c.Name.c_str(),
                        error.c_str(), reference.string().c_str());
            Image::WritePNG((outDir / (c.Name + ".png")).string(), s_Width, s_Height, 4, pixels.data());
            failed++;
            continue;
        }
        if (expected.Width != s_Width || expected.Height != s_Height)
        {
            std::printf("[XATO] %-16s etalon o'lchami %ux%u, kutilgan %ux%u\n", c.Name.c_str(), expected.Width, expected.Height,
                        s_Width, s_Height);
            Image::WritePNG((outDir / (c.Name + ".png")).string(), s_Width, s_Height, 4, pixels.data());
            failed++;
            continue;
        }

        ImageDiff diff = Image::Compare(expected.Pixels, pixels, s_Width, s_Height, threshold, true);
        const bool ok = diff.GetDifferentPercent() <= maxPercent;
        std::printf("[%s] %-16s farqli piksellar %.3f%% (%llu), eng katta farq %d, o'rtacha %.3f\n", ok ? "OK" : "XATO", c.Name.c_str(),
                    diff.GetDifferentPercent(), (unsigned long long)diff.DifferentPixels, diff.MaxDelta, diff.MeanDelta);
        if (!ok)
        {
            failed++;
            const fs::path actual = outDir / (c.Name + ".png"), diffPath = outDir / (c.Name + ".diff.png");
            Image::WritePNG(actual.string(), s_Width, s_Height, 4, pixels.data());
            Image::WritePNG(diffPath.string(), s_Width, s_Height, 4, diff.DiffPixels.data());
            std::printf("       yangi rasm: %s\n       farq: %s\n", actual.string().c_str(), diffPath.string().c_str());
        }
    }

    delete app;
    if (update)
        std::printf("\n%d ta etalon yangilandi, %d xato\n", checked, failed);
    else
        std::printf("\n%d ta render holati, %d xato (chegara: kanal farqi > %d, ruxsat %.2f%% piksel)\n", checked, failed, threshold,
                    maxPercent);
    return failed == 0 ? 0 : 1;
}
