#include "SceneThumbnails.h"

#include <cmath>
#include <cstring>

namespace Zamin {

    namespace fs = std::filesystem;

    bool SceneThumbnails::Render(const Ref<Scene>& source, const fs::path& scenePath, const EditorCamera* fallback)
    {
        if (!source || scenePath.empty())
            return false;
        // Drawn at 720p and scaled down: the UI and the text are as large as in a game window.
        const uint32_t width = SceneMetadata::ThumbnailWidth * 4, height = SceneMetadata::ThumbnailHeight * 4;
        // A copy: drawing at the thumbnail's size changes the cameras' aspect and the UI layout.
        Ref<Scene> scene = Scene::Copy(source);
        scene->ResolveAssets();
        scene->OnViewportResize(width, height);

        Scene::RuntimeView view;
        const bool gameCamera = scene->GetRuntimeView(view);
        if (!gameCamera && !fallback)
            return false;
        EditorCamera camera;
        if (!gameCamera)
        {
            camera = *fallback;
            camera.SetViewportSize((float)width, (float)height);
        }

        const glm::uvec2 size = gameCamera ? scene->GetRuntimeRenderResolution(width, height)
                                           : Renderer::GetRenderResolution(width, height, scene->GetSettings().RenderScale);
        FramebufferSpecification hdrSpec;
        hdrSpec.Width = std::max(1u, size.x);
        hdrSpec.Height = std::max(1u, size.y);
        hdrSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        Ref<Framebuffer> hdr = Framebuffer::Create(hdrSpec);
        FramebufferSpecification outSpec;
        outSpec.Width = width;
        outSpec.Height = height;
        outSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
        Ref<Framebuffer> output = Framebuffer::Create(outSpec);

        const RenderStatistics stats = Renderer::GetStats(); // the frame's numbers stay the viewport's
        hdr->Bind();
        const glm::vec4 clear = scene->GetSettings().DrawSky ? glm::vec4(0.1f, 0.1f, 0.12f, 1.0f) : scene->GetClearColor();
        RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), 1.0f });
        RenderCommand::Clear();
        hdr->ClearAttachment(1, -1);
        if (gameCamera)
            scene->RenderRuntime(view, false);
        else
        {
            SceneRenderOptions options;
            options.SkipEditorHidden = true;
            scene->OnRenderEditor(camera, options);
        }
        // The sub-scenes the game starts with, where it puts them (over the scene, without a sky).
        for (Entity host : scene->FindEntitiesWith<SubSceneComponent>())
        {
            const auto& c = host.GetComponent<SubSceneComponent>();
            if (!c.LoadOnStart || c.ScenePath.empty() || !scene->IsActiveInHierarchy(host))
                continue;
            const fs::path file = Project::GetAssetFileSystemPath(c.ScenePath);
            if (file.lexically_normal() == scenePath.lexically_normal())
                continue; // itself
            Ref<Scene> sub = CreateRef<Scene>();
            if (!SceneSerializer(sub).Deserialize(file))
                continue;
            sub->ResolveAssets();
            sub->OnViewportResize(width, height);
            if (c.ApplyTransform)
                SceneManager::OffsetRoots(*sub, scene->GetWorldTransform(host));
            if (gameCamera)
                sub->RenderRuntime(view, true);
            else
            {
                SceneRenderOptions options;
                options.SkipEditorHidden = true;
                options.Overlay = true;
                sub->OnRenderEditor(camera, options);
            }
        }
        hdr->Unbind();

        PostProcessInput input;
        input.ColorTexture = hdr->GetColorAttachmentRendererID(0);
        input.Width = hdrSpec.Width;
        input.Height = hdrSpec.Height;
        input.DepthTexture = hdr->GetDepthAttachmentRendererID();
        input.Projection = gameCamera ? view.Projection : camera.GetProjection();
        input.OutputWidth = width;
        input.OutputHeight = height;
        PixelPerfectView pixel;
        const bool upscale = gameCamera && scene->GetPixelPerfectView(pixel) && pixel.Upscale && pixel.RenderSize == size;
        if (upscale)
        {
            input.PixelZoom = pixel.Zoom;
            input.PixelOffset = PixelPerfect::GetUpscaleOffset(pixel);
        }
        PostProcessor::Apply(input, scene->GetSettings().Post, output);
        if (gameCamera && view.DeferUI)
        {
            // A pixel-perfect game's UI goes over the finished picture.
            output->Bind();
            scene->RenderDeferredUI();
            output->Unbind();
        }
        Renderer::GetStats() = stats;

        std::vector<uint8_t> pixels = output->ReadColorPixels(0); // bottom row first
        const size_t row = (size_t)width * 4;
        if (pixels.size() != row * height)
            return false;
        std::vector<uint8_t> flipped(pixels.size());
        for (uint32_t y = 0; y < height; y++)
            std::memcpy(&flipped[(size_t)y * row], &pixels[(size_t)(height - 1 - y) * row], row);
        for (size_t i = 3; i < flipped.size(); i += 4)
            flipped[i] = 255;
        const ThumbnailImage small = AssetThumbnails::Downscale(flipped.data(), width, height, SceneMetadata::ThumbnailWidth);
        if (!small.IsValid())
            return false;
        const fs::path file = SceneMetadata::GetThumbnailPath(scenePath);
        if (!Image::WritePNG(file.string(), small.Width, small.Height, 4, small.Pixels.data()))
        {
            ZM_WARN("Sahna rasmi yozilmadi: {}", file.string());
            return false;
        }
        return true;
    }

    Ref<Texture2D> SceneThumbnails::Get(const fs::path& scenePath)
    {
        const fs::path file = SceneMetadata::GetThumbnailPath(scenePath);
        if (file.empty())
            return nullptr;
        auto [it, inserted] = m_Textures.try_emplace(file.string());
        Entry& entry = it->second;
        if (!inserted && m_Frame < entry.CheckedFrame + 30)
            return entry.Texture;
        entry.CheckedFrame = m_Frame;
        std::error_code ec;
        const uintmax_t size = fs::file_size(file, ec);
        if (ec)
        {
            entry = {};
            entry.CheckedFrame = m_Frame;
            entry.Missing = true;
            return nullptr;
        }
        const fs::file_time_type time = fs::last_write_time(file, ec);
        if (entry.Texture && entry.Size == size && entry.Time == time)
            return entry.Texture;
        if (!entry.Texture && !entry.Missing && !inserted && entry.Size == size && entry.Time == time)
            return nullptr; // a picture that could not be read: not again until it changes
        entry.Size = size;
        entry.Time = time;
        entry.Missing = false;
        ImageData image;
        if (!Image::Load(file.string(), image, 4, false))
        {
            entry.Texture = nullptr;
            return nullptr;
        }
        TextureSpecification spec;
        spec.Width = image.Width;
        spec.Height = image.Height;
        spec.Format = ImageFormat::RGBA8;
        spec.Filter = TextureFilter::Linear;
        spec.Wrap = TextureWrap::ClampToEdge;
        spec.GenerateMips = true;
        Ref<Texture2D> texture = Texture2D::Create(spec);
        texture->SetData(image.Pixels.data(), (uint32_t)image.Pixels.size());
        entry.Texture = texture;
        return texture;
    }

    void SceneThumbnails::Invalidate(const fs::path& scenePath)
    {
        m_Textures.erase(SceneMetadata::GetThumbnailPath(scenePath).string());
    }

}
