#include "Zamin/Renderer/MaterialPreview.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/PostProcess.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Renderer3D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Zamin {

    const char* MaterialPreview::GetShapeName(Shape shape)
    {
        switch (shape)
        {
            case Shape::Sphere:   return "Shar";
            case Shape::Cube:     return "Kub";
            case Shape::Plane:    return "Tekislik";
            case Shape::Cylinder: return "Silindr";
        }
        return "Shar";
    }

    static PrimitiveMesh ToPrimitive(MaterialPreview::Shape shape)
    {
        switch (shape)
        {
            case MaterialPreview::Shape::Sphere:   return PrimitiveMesh::Sphere;
            case MaterialPreview::Shape::Cube:     return PrimitiveMesh::Cube;
            case MaterialPreview::Shape::Plane:    return PrimitiveMesh::Plane;
            case MaterialPreview::Shape::Cylinder: return PrimitiveMesh::Cylinder;
        }
        return PrimitiveMesh::Sphere;
    }

    uint32_t MaterialPreview::Render(const Material& material, uint32_t width, uint32_t height, const Settings& settings)
    {
        width = std::max(width, 8u);
        height = std::max(height, 8u);
        if (!m_Target)
        {
            FramebufferSpecification spec;
            spec.Width = width;
            spec.Height = height;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
            m_Target = Framebuffer::Create(spec);
            FramebufferSpecification finalSpec;
            finalSpec.Width = width;
            finalSpec.Height = height;
            finalSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
            m_Final = Framebuffer::Create(finalSpec);
        }
        else if (m_Target->GetSpecification().Width != width || m_Target->GetSpecification().Height != height)
        {
            m_Target->Resize(width, height);
            m_Final->Resize(width, height);
        }
        Ref<Mesh> mesh = MeshLibrary::GetPrimitive(ToPrimitive(settings.PreviewShape));
        if (!mesh)
            return 0;

        RenderStatistics stats = Renderer::GetStats(); // the preview is not part of the scene's numbers
        m_Target->Bind();
        RenderCommand::SetClearColor({ 0.012f, 0.014f, 0.019f, 1.0f }); // linear
        RenderCommand::Clear();

        // The camera stays, the object turns (the light stays where it is, the surface moves under it).
        const float fov = glm::radians(30.0f);
        float aspect = (float)width / (float)height;
        float radius = settings.PreviewShape == Shape::Cube ? 0.87f : settings.PreviewShape == Shape::Cylinder ? 0.75f : 0.62f;
        float fit = radius / std::sin(fov * 0.5f) / std::min(1.0f, aspect);
        float distance = fit * std::clamp(settings.Zoom, 0.3f, 4.0f);
        glm::vec3 eye(0.0f, 0.0f, distance);
        glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(fov, aspect, 0.05f, distance + 10.0f);
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), settings.Pitch, glm::vec3(1.0f, 0.0f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), settings.Yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        if (settings.PreviewShape == Shape::Plane) // lies flat: tilted towards the camera
            model = model * glm::rotate(glm::mat4(1.0f), glm::radians(70.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        SceneEnvironment env;
        env.DrawSky = settings.Background;
        env.HasSun = settings.Sun;
        env.Sun.Direction = glm::normalize(glm::vec3(0.55f, -0.75f, -0.6f)); // from the upper left, in front
        env.Sun.Intensity = 2.6f;
        env.Sun.CastShadows = false;
        env.AmbientIntensity = settings.Environment ? 1.0f : 0.85f;
        env.EnvMap = settings.Environment;
        env.EnvIntensity = settings.EnvironmentIntensity;
        env.EnvRotation = settings.EnvironmentRotation;
        Renderer3D::BeginScene(view, projection, eye, env);
        Renderer3D::SubmitMesh(mesh, model, material, -1, false);
        Renderer3D::EndScene();
        m_Target->Unbind();

        PostProcessSettings post;
        post.Tonemap = TonemapMode::ACES;
        PostProcessor::Apply(m_Target->GetColorAttachmentRendererID(0), width, height, post, m_Final);
        m_Final->Unbind(); // Apply leaves its target bound
        Renderer::GetStats() = stats;
        return m_Final->GetColorAttachmentRendererID(0);
    }

    uint32_t MaterialPreview::GetTextureID() const
    {
        return m_Final ? m_Final->GetColorAttachmentRendererID(0) : 0;
    }

    uint32_t MaterialPreview::GetWidth() const { return m_Final ? m_Final->GetSpecification().Width : 0; }
    uint32_t MaterialPreview::GetHeight() const { return m_Final ? m_Final->GetSpecification().Height : 0; }

    std::vector<uint8_t> MaterialPreview::ReadPixels() const
    {
        if (!m_Final)
            return {};
        std::vector<uint8_t> pixels = m_Final->ReadColorPixels(0);
        m_Final->Unbind();
        uint32_t w = GetWidth(), h = GetHeight();
        if (pixels.size() != (size_t)w * h * 4)
            return {};
        // Bottom row first from the GPU: flipped to top first.
        std::vector<uint8_t> out(pixels.size());
        size_t row = (size_t)w * 4;
        for (uint32_t y = 0; y < h; y++)
            std::copy_n(pixels.data() + (size_t)(h - 1 - y) * row, row, out.data() + (size_t)y * row);
        return out;
    }

}
