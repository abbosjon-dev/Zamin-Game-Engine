#include "ThumbnailCache.h"
#include "SceneThumbnails.h"
#include <Zamin/Core/Stopwatch.h>

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

    // Bump when material / prefab thumbnails are drawn differently (saved ones are made again).
    static constexpr int64_t s_MaterialThumbnailVersion = 3;
    static constexpr int64_t s_PrefabThumbnailVersion = 1;

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
        return AssetThumbnails::CanGenerate(path) || type == AssetType::Model || type == AssetType::Material || type == AssetType::Scene ||
               type == AssetType::Prefab;
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
        // A scene shows the picture the editor drew when it was saved (its thumbnail file): none
        // until it is saved once.
        if (AssetTypes::FromPath(path) == AssetType::Scene)
            return GetForFile(SceneMetadata::GetThumbnailPath(path));
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
            entry.Prefab = AssetTypes::FromPath(path) == AssetType::Prefab;
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
        else if (entry.Prefab)
        {
            // A prefab's picture shows its textures, models, fonts ...: their versions are in the stamp.
            entry.Dependencies.clear();
            const std::vector<fs::path> files = PrefabDependencies(entry.Path);
            for (const fs::path& file : files)
                entry.Dependencies.push_back(ThumbnailDiskCache::MakeKey(file));
            entry.DiskStamp = entry.Stamp.WithDependencies(files);
            entry.DiskStamp.Time ^= (int64_t)s_PrefabThumbnailVersion * 0x2F7A3D61;
        }
        Job job;
        job.Key = key;
        job.Path = entry.Path;
        job.Stamp = entry.Stamp;
        job.DiskStamp = entry.DiskStamp;
        job.Render = AssetTypes::FromPath(entry.Path) == AssetType::Model || entry.Material || entry.Prefab;
        {
            std::lock_guard lock(m_Mutex);
            std::erase_if(m_Jobs, [&](const Job& j) { return j.Key == key && j.Save.Pixels.empty(); }); // an older version's
            m_Jobs.push_back(std::move(job));
        }
        m_Wake.notify_one();
    }

    void ThumbnailCache::Invalidate(const fs::path& path)
    {
        if (AssetTypes::FromPath(path) == AssetType::Scene)
            Invalidate(SceneMetadata::GetThumbnailPath(path));
        std::string key = path.string();
        std::string prefix = key + "/";
        std::string fileKey = ThumbnailDiskCache::MakeKey(path);
        std::string filePrefix = fileKey + "/";
        std::vector<std::string> gone;
        for (auto& [k, entry] : m_Entries)
        {
            // A texture of a material / an asset of a prefab (or a folder holding one) changed: drawn again.
            if ((entry.Material || entry.Prefab) && std::any_of(entry.Dependencies.begin(), entry.Dependencies.end(), [&](const std::string& d) {
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

        const Stopwatch budget;
        int uploads = 0;
        while (!m_Ready.empty() && uploads < s_UploadsPerFrame && budget.ElapsedMilliseconds() < s_UploadBudgetMs)
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
            bool rendered = entry.Material ? RenderMaterial(entry, image, save)
                          : entry.Prefab   ? RenderPrefab(entry.Path, image)
                                           : RenderModel(entry.Path, image);
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
        std::vector<glm::mat4> palette;
        const auto& subs = model->GetSubMeshes();
        for (size_t s = 0; s < subs.size(); s++)
            Renderer3D::SubmitMesh(subs[s].MeshRef, glm::mat4(1.0f), subs[s].Mat, -1, false,
                                   subs[s].MeshRef->IsSkinned() ? &model->GetSubMeshJoints(s, joints, palette) : nullptr);
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

    std::vector<fs::path> ThumbnailCache::PrefabDependencies(const fs::path& path)
    {
        std::vector<fs::path> files;
        Scene scratch;
        SceneSerializer::EntitySetReadOptions options;
        options.KeepPrefabLinks = false;
        const std::vector<Entity> entities = PrefabCache::Instantiate(scratch, path, options);
        SceneSerializer::CollectAssetPaths(entities, [&](size_t, std::string_view, const std::string& asset) {
            const fs::path file = Project::GetAssetFileSystemPath(asset);
            if (std::find(files.begin(), files.end(), file) == files.end())
                files.push_back(file);
        });
        return files;
    }

    bool ThumbnailCache::RenderPrefab(const fs::path& path, ThumbnailImage& out)
    {
        // The textures and models loaded for the picture only go again afterwards (see RenderModel).
        const uint64_t textureSerial = TextureLibrary::GetLoadSerial();
        std::vector<fs::path> models;
        for (const fs::path& file : PrefabDependencies(path))
            if (AssetTypes::FromPath(file) == AssetType::Model && !Model::IsFileLoaded(file))
                models.push_back(file);
        bool drawn = false;
        PrefabThumbnailView kind = PrefabThumbnailView::View2D;
        std::vector<uint8_t> pixels;
        uint32_t width = Size * 3, height = Size * 3;
        glm::uvec4 crop(0, 0, 0, 0); // x0, y0, x1, y1 (top row first); empty: the whole picture
        {
            // One plain instance in a scene of its own (not running: its scripts do nothing).
            Ref<Scene> scene = CreateRef<Scene>();
            Entity root = scene->InstantiatePrefab(path);
            if (root)
            {
                kind = PrefabPlacement::ChooseThumbnailView(*scene);
                auto& settings = scene->GetSettings();
                settings.DrawSky = false; // the prefab on the browser's dark backdrop
                if (kind == PrefabThumbnailView::View3D && scene->GetAllEntitiesWith<DirectionalLightComponent>().empty())
                {
                    // Lit like the model thumbnails (a sun from above-left) when it brings no sun.
                    Entity sun = scene->CreateEntity("Thumbnail Sun");
                    sun.AddComponent<DirectionalLightComponent>().Intensity = 2.6f;
                    sun.GetComponent<TransformComponent>().Rotation = { glm::radians(-50.0f), glm::radians(-35.0f), 0.0f };
                }
                // A UI prefab is laid out on a game-like (16:9) screen and letterboxed into the square.
                if (kind == PrefabThumbnailView::UI)
                {
                    width = Size * 4;
                    height = Size * 4 * 9 / 16;
                }
                EditorCamera camera(35.0f, (float)width / (float)height, 0.01f, 2000.0f);
                camera.SetViewportSize((float)width, (float)height);
                if (kind == PrefabThumbnailView::View3D)
                    camera.SetOrientation(0.42f, -0.62f); // three quarters, from above
                else
                {
                    camera.SetOrthographic(true);
                    camera.SetOrientation(0.0f, 0.0f);
                }
                camera.SetViewState(camera.ComputeFrameState(PrefabPlacement::ThumbnailBounds(*scene), 1.12f));
                // A UI prefab: the part of the screen its elements cover (a full-screen backdrop not
                // counted), so a menu fills the tile instead of a screen with a small menu on it.
                if (kind == PrefabThumbnailView::UI)
                {
                    scene->OnViewportResize(width, height);
                    std::vector<UIRect> rects;
                    const float viewArea = (float)width * (float)height;
                    for (auto handle : scene->GetAllEntitiesWith<UITransformComponent>())
                    {
                        Entity e(handle, scene.get());
                        if (!scene->IsActiveInHierarchy(e) ||
                            !(e.HasComponent<UIImageComponent>() || e.HasComponent<TextComponent>() || e.HasComponent<UIButtonComponent>() ||
                              e.HasComponent<SpriteRendererComponent>()))
                            continue;
                        const UIRect r = scene->GetUIScreenRect(e);
                        const glm::vec2 size = r.GetSize();
                        if (size.x <= 0.5f || size.y <= 0.5f || size.x * size.y > viewArea * 0.8f)
                            continue;
                        rects.push_back(r);
                    }
                    // The largest element (the menu's panel) and what lies on it: a version label in a
                    // corner of the screen is left out.
                    glm::vec2 lo(FLT_MAX), hi(-FLT_MAX);
                    if (!rects.empty())
                    {
                        const UIRect main = *std::max_element(rects.begin(), rects.end(), [](const UIRect& a, const UIRect& b) {
                            return a.GetSize().x * a.GetSize().y < b.GetSize().x * b.GetSize().y;
                        });
                        for (const UIRect& r : rects)
                            if (r.Max.x >= main.Min.x && r.Min.x <= main.Max.x && r.Max.y >= main.Min.y && r.Min.y <= main.Max.y)
                            {
                                lo = glm::min(lo, r.Min);
                                hi = glm::max(hi, r.Max);
                            }
                    }
                    if (lo.x < hi.x && lo.y < hi.y)
                    {
                        const glm::vec2 pad = (hi - lo) * 0.06f + glm::vec2(4.0f);
                        lo = glm::max(lo - pad, glm::vec2(0.0f));
                        hi = glm::min(hi + pad, glm::vec2((float)width, (float)height));
                        crop = { (uint32_t)lo.x, (uint32_t)((float)height - hi.y), (uint32_t)std::ceil(hi.x), (uint32_t)std::ceil((float)height - lo.y) };
                    }
                }
                // Effects that play once are shown early (a burst is over after a second or two).
                float warmUp = SceneThumbnails::ParticleWarmUpSeconds;
                for (auto handle : scene->GetAllEntitiesWith<ParticleSystemComponent>())
                    if (!Entity(handle, scene.get()).GetComponent<ParticleSystemComponent>().Looping)
                        warmUp = 0.18f;
                drawn = SceneThumbnails::RenderPixels(scene, &camera, width, height, true, pixels, {}, warmUp);
            }
        }
        TextureLibrary::ReleaseUnused(textureSerial);
        for (const fs::path& model : models)
            Model::ReleaseIfUnused(model);
        if (!drawn)
            return false;
        // The part shown (a UI prefab's elements), scaled to fit the square, centred on the backdrop's colour.
        const uint8_t backdrop[4] = { pixels[0], pixels[1], pixels[2], 255 };
        if (crop.z > crop.x && crop.w > crop.y && crop.z <= width && crop.w <= height && (crop.z - crop.x < width || crop.w - crop.y < height))
        {
            const uint32_t cw = crop.z - crop.x, ch = crop.w - crop.y;
            std::vector<uint8_t> part((size_t)cw * ch * 4);
            for (uint32_t y = 0; y < ch; y++)
                std::copy_n(pixels.data() + ((size_t)(crop.y + y) * width + crop.x) * 4, (size_t)cw * 4, part.data() + (size_t)y * cw * 4);
            pixels = std::move(part);
            width = cw;
            height = ch;
        }
        const ThumbnailImage small = AssetThumbnails::Downscale(pixels.data(), width, height, Size);
        if (!small.IsValid())
            return false;
        out.Width = out.Height = Size;
        out.Pixels.assign((size_t)Size * Size * 4, 0);
        for (size_t i = 0; i < out.Pixels.size(); i += 4)
            std::copy_n(backdrop, 4, out.Pixels.data() + i);
        const uint32_t x0 = (Size - small.Width) / 2, y0 = (Size - small.Height) / 2;
        for (uint32_t y = 0; y < small.Height && y0 + y < Size; y++)
            std::copy_n(small.Pixels.data() + (size_t)y * small.Width * 4, (size_t)std::min(small.Width, Size - x0) * 4,
                        out.Pixels.data() + ((size_t)(y0 + y) * Size + x0) * 4);
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
