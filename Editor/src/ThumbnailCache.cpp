#include "ThumbnailCache.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Zamin {

    namespace fs = std::filesystem;

    // How much reaches the GPU per frame, and how many thumbnails stay in memory.
    static constexpr int s_UploadsPerFrame = 6;
    static constexpr double s_UploadBudgetMs = 4.0;
    static constexpr size_t s_MaxTextures = 768;
    // Jobs nobody asked for in this many frames (scrolled away, another folder) are dropped.
    static constexpr uint64_t s_ForgetAfterFrames = 90;

    // Bump when material thumbnails are drawn differently (saved ones are made again).
    static constexpr int64_t s_MaterialThumbnailVersion = 3;

    static double NowSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    ThumbnailCache::ThumbnailCache()
    {
        m_Thread = std::thread([this]() { WorkerLoop(); });
    }

    ThumbnailCache::~ThumbnailCache()
    {
        {
            std::lock_guard lock(m_Mutex);
            m_Stop = true;
            m_Jobs.clear();
        }
        m_Wake.notify_all();
        if (m_Thread.joinable())
            m_Thread.join();
    }

    bool ThumbnailCache::CanHaveThumbnail(const fs::path& path)
    {
        AssetType type = AssetTypes::FromPath(path);
        return AssetThumbnails::CanGenerate(path) || type == AssetType::Model || type == AssetType::Material;
    }

    void ThumbnailCache::SetCacheDirectory(const fs::path& directory)
    {
        Clear();
        m_CacheDirectory = directory;
        {
            std::lock_guard lock(m_Mutex);
            m_Jobs.clear();
            m_Results.clear();
            m_Disk.SetDirectory(directory);
            m_PruneRequested = !directory.empty();
        }
        m_Wake.notify_one();
    }

    void ThumbnailCache::Clear()
    {
        m_Entries.clear();
        m_RenderQueue.clear();
        m_Ready.clear();
        std::lock_guard lock(m_Mutex);
        m_Jobs.clear();
        m_Results.clear();
    }

    Ref<Texture2D> ThumbnailCache::Get(const fs::path& path, uintmax_t size, fs::file_time_type time)
    {
        if (!CanHaveThumbnail(path))
            return nullptr;
        std::string key = path.string();
        ThumbnailStamp stamp = ThumbnailStamp::From(size, time, Size);
        auto [it, inserted] = m_Entries.try_emplace(key);
        Entry& entry = it->second;
        entry.LastUsed = m_Frame;
        if (inserted || !(entry.Stamp == stamp))
        {
            entry.Path = path;
            entry.Stamp = stamp;
            entry.Status = State::Waiting;
            entry.Material = AssetTypes::FromPath(path) == AssetType::Material;
            Request(key, entry);
        }
        else if (entry.Material)
            FollowLiveMaterial(key, entry);
        return entry.Texture; // the previous version's picture while a new one is made
    }

    Ref<Texture2D> ThumbnailCache::GetForFile(const fs::path& path)
    {
        if (!CanHaveThumbnail(path))
            return nullptr;
        std::error_code ec;
        uintmax_t size = fs::file_size(path, ec);
        if (ec)
            return nullptr;
        fs::file_time_type time = fs::last_write_time(path, ec);
        if (ec)
            return nullptr;
        return Get(path, size, time);
    }

    void ThumbnailCache::FollowLiveMaterial(const std::string& key, Entry& entry)
    {
        if (entry.Status == State::Waiting || entry.Redraw)
            return;
        Ref<MaterialAsset> live = entry.Live.lock();
        // Something may have loaded the material since it was drawn (the inspector, a scene): look
        // now and then (finding it is a few file system calls).
        if (!live && m_Frame >= entry.LiveCheckFrame)
        {
            entry.LiveCheckFrame = m_Frame + 30;
            live = MaterialLibrary::Find(entry.Path);
            entry.Live = live;
            if (live && live->GetRevision() != entry.RenderedRevision)
            {
                // Not what was drawn (the picture came from the disk cache or another load): drawn
                // again only when it differs from its file - unsaved edits.
                MaterialData onDisk;
                if (MaterialSerializer::Deserialize(entry.Path, onDisk) && onDisk == live->GetData())
                    entry.RenderedRevision = live->GetRevision();
            }
        }
        if (!live || live->GetRevision() == entry.RenderedRevision)
            return;
        // Edited (or reloaded): drawn again, a few times a second at most while it keeps changing.
        double now = NowSeconds();
        if (entry.LastRenderTime >= 0.0 && now - entry.LastRenderTime < 0.15)
            return;
        entry.Redraw = true;
        if (std::find(m_RenderQueue.begin(), m_RenderQueue.end(), key) == m_RenderQueue.end())
            m_RenderQueue.push_back(key);
    }

    void ThumbnailCache::Request(const std::string& key, Entry& entry)
    {
        entry.DiskStamp = entry.Stamp;
        entry.Redraw = false;
        if (entry.Material)
        {
            // A material's picture also shows its textures: their versions are part of the stamp
            // the disk cache checks (the .zmat is small, read here on the main thread because its
            // texture paths may be looked up in the asset registry).
            entry.Dependencies.clear();
            std::vector<fs::path> textures;
            MaterialData data;
            if (MaterialSerializer::Deserialize(entry.Path, data))
                for (const std::string& texture : data.GetTexturePaths())
                {
                    fs::path file = Project::GetAssetFileSystemPath(texture);
                    textures.push_back(file);
                    entry.Dependencies.push_back(ThumbnailDiskCache::MakeKey(file));
                }
            entry.DiskStamp = entry.Stamp.WithDependencies(textures);
            // How materials are drawn is part of it, too: a new look makes saved pictures out of date.
            entry.DiskStamp.Time ^= (int64_t)s_MaterialThumbnailVersion * 0x5BD1E995;
        }
        Job job;
        job.Key = key;
        job.Path = entry.Path;
        job.Stamp = entry.Stamp;
        job.DiskStamp = entry.DiskStamp;
        job.Render = AssetTypes::FromPath(entry.Path) == AssetType::Model || entry.Material;
        {
            std::lock_guard lock(m_Mutex);
            std::erase_if(m_Jobs, [&](const Job& j) { return j.Key == key && j.Save.Pixels.empty(); }); // an older version's
            m_Jobs.push_back(std::move(job));
        }
        m_Wake.notify_one();
    }

    void ThumbnailCache::Invalidate(const fs::path& path)
    {
        std::string key = path.string();
        std::string prefix = key + "/";
        std::string fileKey = ThumbnailDiskCache::MakeKey(path);
        std::string filePrefix = fileKey + "/";
        std::vector<std::string> gone;
        for (auto& [k, entry] : m_Entries)
        {
            // A texture of a material (or a folder holding one) changed: the material is drawn again.
            if (entry.Material && std::any_of(entry.Dependencies.begin(), entry.Dependencies.end(), [&](const std::string& d) {
                    return d == fileKey || d.starts_with(filePrefix);
                }))
                entry.Stamp = {};
            if (k != key && !k.starts_with(prefix))
                continue;
            std::error_code ec;
            if (!fs::exists(entry.Path, ec))
                gone.push_back(k);
            else
                entry.Stamp = {}; // asked for again by the next Get
        }
        for (const std::string& k : gone)
        {
            std::lock_guard lock(m_Mutex);
            m_Disk.Remove(m_Entries.at(k).Path);
            m_Entries.erase(k);
        }
    }

    // ------------------------------------------------------------------ worker
    void ThumbnailCache::WorkerLoop()
    {
        while (true)
        {
            Job job;
            ThumbnailDiskCache disk;
            bool prune = false;
            {
                std::unique_lock lock(m_Mutex);
                m_Wake.wait(lock, [&]() { return m_Stop || !m_Jobs.empty() || m_PruneRequested; });
                if (m_Stop)
                    return;
                disk = m_Disk;
                if (m_PruneRequested)
                {
                    prune = true;
                    m_PruneRequested = false;
                }
                else
                {
                    job = std::move(m_Jobs.back());
                    m_Jobs.pop_back();
                }
            }
            if (prune)
            {
                if (size_t removed = disk.Prune(); removed > 0)
                    ZM_CORE_INFO("Eskirgan {} ta thumbnail keshdan o'chirildi", removed);
                continue;
            }
            if (!job.Save.Pixels.empty())
            {
                disk.Save(job.Path, job.DiskStamp, job.Save);
                continue;
            }

            Result result;
            result.Key = job.Key;
            result.Stamp = job.Stamp;
            if (disk.Load(job.Path, job.DiskStamp, result.Image))
                result.FromDisk = true;
            else if (job.Render)
                result.NeedsRender = true;
            else if (AssetThumbnails::Generate(job.Path, Size, result.Image, &result.Error))
                disk.Save(job.Path, job.Stamp, result.Image);
            std::lock_guard lock(m_Mutex);
            if (m_Stop)
                return;
            m_Results.push_back(std::move(result));
        }
    }

    // ------------------------------------------------------------------ main thread
    void ThumbnailCache::Upload(Entry& entry, const ThumbnailImage& image)
    {
        if (entry.Texture && entry.Texture->GetWidth() == image.Width && entry.Texture->GetHeight() == image.Height)
        {
            entry.Texture->SetData(image.Pixels.data(), (uint32_t)image.Pixels.size()); // the same texture object
            return;
        }
        TextureSpecification spec;
        spec.Width = image.Width;
        spec.Height = image.Height;
        spec.Format = ImageFormat::RGBA8;
        spec.Filter = TextureFilter::Linear;
        spec.Wrap = TextureWrap::ClampToEdge;
        spec.GenerateMips = true; // drawn smaller than it is in small tiles
        Ref<Texture2D> texture = Texture2D::Create(spec);
        texture->SetData(image.Pixels.data(), (uint32_t)image.Pixels.size());
        entry.Texture = texture;
    }

    void ThumbnailCache::Update()
    {
        m_Frame++;
        {
            std::lock_guard lock(m_Mutex);
            for (Result& r : m_Results)
                m_Ready.push_back(std::move(r));
            m_Results.clear();
            // Waiting for something nobody looks at any more: dropped (asked for again when visible).
            std::erase_if(m_Jobs, [&](const Job& job) {
                if (!job.Save.Pixels.empty())
                    return false;
                auto it = m_Entries.find(job.Key);
                bool stale = it == m_Entries.end() || m_Frame - it->second.LastUsed > s_ForgetAfterFrames;
                if (stale && it != m_Entries.end() && !it->second.Texture)
                    m_Entries.erase(it);
                else if (stale && it != m_Entries.end())
                    it->second.Stamp = {};
                return stale;
            });
        }

        auto start = std::chrono::steady_clock::now();
        int uploads = 0;
        while (!m_Ready.empty() && uploads < s_UploadsPerFrame &&
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() < s_UploadBudgetMs)
        {
            Result result = std::move(m_Ready.front());
            m_Ready.pop_front();
            auto it = m_Entries.find(result.Key);
            if (it == m_Entries.end() || !(it->second.Stamp == result.Stamp))
                continue; // an older version of the file, or forgotten meanwhile
            Entry& entry = it->second;
            if (result.NeedsRender)
            {
                if (std::find(m_RenderQueue.begin(), m_RenderQueue.end(), result.Key) == m_RenderQueue.end())
                    m_RenderQueue.push_back(result.Key);
                continue;
            }
            if (result.Image.IsValid())
            {
                Upload(entry, result.Image);
                entry.Status = State::Ready;
                (result.FromDisk ? m_Stats.FromDisk : m_Stats.Generated)++;
                uploads++;
                // Decoded from the file just now (the same decoders the loaders use): a failure
                // reported for an earlier version is over, also when nothing has the file loaded.
                if (!result.FromDisk)
                    AssetImportLog::Clear(entry.Path);
            }
            else
            {
                entry.Status = State::Failed;
                entry.Texture = nullptr;
                m_Stats.Failed++;
                if (!result.Error.empty())
                    AssetImportLog::Report(entry.Path, result.Error); // once, like a failed load
            }
        }

        // One model or material a frame: loaded (if nothing had it yet), rendered, read back, saved
        // in the background.
        while (!m_RenderQueue.empty())
        {
            std::string key = std::move(m_RenderQueue.front());
            m_RenderQueue.pop_front();
            auto it = m_Entries.find(key);
            if (it == m_Entries.end() || (it->second.Status != State::Waiting && !it->second.Redraw))
                continue;
            Entry& entry = it->second;
            if (m_Frame - entry.LastUsed > s_ForgetAfterFrames)
            {
                if (entry.Redraw)
                    entry.Redraw = false; // asked for again when it is on screen
                else
                    entry.Stamp = {}; // not on screen any more: asked for again when it is
                continue;
            }
            ThumbnailImage image;
            bool save = true;
            bool rendered = entry.Material ? RenderMaterial(entry, image, save) : RenderModel(entry.Path, image);
            bool redraw = entry.Redraw;
            entry.Redraw = false;
            if (rendered)
            {
                Upload(entry, image);
                entry.Status = State::Ready;
                (entry.Material ? m_Stats.MaterialsRendered : m_Stats.Rendered)++;
                if (save)
                    SaveToDisk(key, entry, std::move(image));
            }
            else if (!redraw)
            {
                entry.Status = State::Failed;
                m_Stats.Failed++;
            }
            break;
        }
        Evict();
    }

    void ThumbnailCache::Evict()
    {
        if (m_Entries.size() <= s_MaxTextures)
            return;
        std::vector<std::pair<uint64_t, std::string>> byAge;
        byAge.reserve(m_Entries.size());
        for (const auto& [key, entry] : m_Entries)
            if (m_Frame - entry.LastUsed > 2)
                byAge.emplace_back(entry.LastUsed, key);
        std::sort(byAge.begin(), byAge.end());
        size_t excess = m_Entries.size() - s_MaxTextures;
        for (size_t i = 0; i < byAge.size() && i < excess; i++)
            m_Entries.erase(byAge[i].second);
    }

    void ThumbnailCache::SaveToDisk(const std::string& key, const Entry& entry, ThumbnailImage image)
    {
        Job save;
        save.Key = key;
        save.Path = entry.Path;
        save.Stamp = entry.Stamp;
        save.DiskStamp = entry.DiskStamp;
        save.Save = std::move(image);
        {
            std::lock_guard lock(m_Mutex);
            m_Jobs.insert(m_Jobs.begin(), std::move(save)); // after what is on screen
        }
        m_Wake.notify_one();
    }

    bool ThumbnailCache::RenderMaterial(Entry& entry, ThumbnailImage& out, bool& matchesFile)
    {
        // The material something already has loaded (with its unsaved edits) - else loaded for the
        // picture only and released again with the textures it brought.
        Ref<MaterialAsset> material = MaterialLibrary::Find(entry.Path);
        bool wasLoaded = material != nullptr;
        uint64_t textureSerial = TextureLibrary::GetLoadSerial();
        if (!material)
            material = MaterialLibrary::Load(entry.Path); // a failure is reported once (AssetImportLog)
        if (!material)
            return false;
        matchesFile = true;
        if (wasLoaded)
        {
            MaterialData onDisk;
            matchesFile = MaterialSerializer::Deserialize(entry.Path, onDisk) && onDisk == material->GetData();
        }

        // A sphere on a dark backdrop, lit by the sky gradient and a sun; cutout and transparent
        // materials get the checkered wall behind, emissive ones bloom.
        MaterialPreview::Settings settings;
        settings.PreviewShape = MaterialPreview::Shape::Sphere;
        settings.Background = false;
        settings.Yaw = 0.6f;
        settings.Pitch = 0.25f;
        settings.Zoom = 0.9f;
        bool drawn = m_MaterialPreview.Render(material->GetMaterial(), Size, Size, settings) != 0;
        std::vector<uint8_t> pixels = drawn ? m_MaterialPreview.ReadPixels() : std::vector<uint8_t>();
        entry.RenderedRevision = material->GetRevision();
        entry.LastRenderTime = NowSeconds();
        entry.Live = wasLoaded ? material : Ref<MaterialAsset>();
        entry.LiveCheckFrame = m_Frame + 30;
        material.reset();
        if (!wasLoaded && MaterialLibrary::ReleaseIfUnused(entry.Path))
            m_Stats.ReleasedTextures += TextureLibrary::ReleaseUnused(textureSerial);

        if (pixels.size() != (size_t)Size * Size * 4)
            return false;
        out.Width = out.Height = Size;
        out.Pixels = std::move(pixels); // ReadPixels gives the top row first
        return true;
    }

    bool ThumbnailCache::RenderModel(const fs::path& path, ThumbnailImage& out)
    {
        bool wasLoaded = Model::IsFileLoaded(path);
        uint64_t textureSerial = TextureLibrary::GetLoadSerial();
        Ref<Model> model = Model::Load(path); // a failure is reported once (AssetImportLog)
        // Loaded for the picture only: the model and the textures it brought into the texture
        // cache (full size, on the GPU) go again unless something else took them meanwhile.
        auto release = [&]() {
            model.reset();
            if (wasLoaded)
                return;
            Model::ReleaseIfUnused(path);
            m_Stats.ReleasedTextures += TextureLibrary::ReleaseUnused(textureSerial);
        };
        if (!model || model->GetSubMeshes().empty())
        {
            release();
            return false;
        }

        if (!m_ModelTarget)
        {
            FramebufferSpecification spec;
            spec.Width = spec.Height = Size;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
            m_ModelTarget = Framebuffer::Create(spec);
            FramebufferSpecification finalSpec;
            finalSpec.Width = finalSpec.Height = Size;
            finalSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
            m_ModelFinal = Framebuffer::Create(finalSpec);
        }

        // The same look as the inspector's model view, from a fixed angle, in the bind pose.
        RenderStatistics stats = Renderer::GetStats();
        m_ModelTarget->Bind();
        RenderCommand::SetClearColor({ 0.03f, 0.034f, 0.045f, 1.0f });
        RenderCommand::Clear();
        AABB bounds = model->GetBounds();
        glm::vec3 center = (bounds.Min + bounds.Max) * 0.5f;
        float radius = std::max(0.01f, glm::length(bounds.Max - bounds.Min) * 0.5f);
        const float fov = glm::radians(35.0f);
        float distance = radius / std::sin(fov * 0.5f) * 0.92f;
        const float yaw = 0.6f, pitch = 0.4f;
        glm::vec3 eye = center + glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)) * distance;
        glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(fov, 1.0f, std::max(0.001f, distance * 0.01f), distance * 4.0f + radius * 4.0f);
        SceneEnvironment env;
        env.DrawSky = false;
        env.HasSun = true;
        env.Sun.Direction = glm::normalize(glm::vec3(-0.45f, -1.0f, -0.55f));
        env.Sun.Intensity = 3.2f;
        env.Sun.CastShadows = false;
        env.AmbientIntensity = 0.9f;
        Renderer3D::BeginScene(view, projection, eye, env);
        std::vector<glm::mat4> joints;
        if (model->IsSkinned())
            EvaluatePose(model->GetSkeleton(), nullptr, 0.0f, joints);
        for (const Model::SubMesh& sub : model->GetSubMeshes())
            Renderer3D::SubmitMesh(sub.MeshRef, glm::mat4(1.0f), sub.Mat, -1, false, sub.MeshRef->IsSkinned() ? &joints : nullptr);
        Renderer3D::EndScene();
        m_ModelTarget->Unbind();
        PostProcessSettings post;
        post.Tonemap = TonemapMode::ACES;
        PostProcessor::Apply(m_ModelTarget->GetColorAttachmentRendererID(0), Size, Size, post, m_ModelFinal);
        std::vector<uint8_t> pixels = m_ModelFinal->ReadColorPixels(0);
        m_ModelFinal->Unbind(); // Apply leaves its target bound
        Renderer::GetStats() = stats;
        release();

        if (pixels.size() != (size_t)Size * Size * 4)
            return false;
        // Bottom row first from the GPU: flipped to top first.
        out.Width = out.Height = Size;
        out.Pixels.resize(pixels.size());
        size_t row = (size_t)Size * 4;
        for (uint32_t y = 0; y < Size; y++)
            std::copy_n(pixels.data() + (size_t)(Size - 1 - y) * row, row, out.Pixels.data() + (size_t)y * row);
        return true;
    }

    ThumbnailCache::Stats ThumbnailCache::GetStats() const
    {
        Stats stats = m_Stats;
        for (const auto& [key, entry] : m_Entries)
        {
            if (entry.Texture)
                stats.Textures++;
            if (entry.Status == State::Waiting)
                stats.Waiting++;
        }
        return stats;
    }

}
