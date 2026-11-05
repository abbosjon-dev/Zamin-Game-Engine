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
        return AssetThumbnails::CanGenerate(path) || AssetTypes::FromPath(path) == AssetType::Model;
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
            Request(key, entry);
        }
        return entry.Texture; // the previous version's picture while a new one is made
    }

    void ThumbnailCache::Request(const std::string& key, Entry& entry)
    {
        Job job;
        job.Key = key;
        job.Path = entry.Path;
        job.Stamp = entry.Stamp;
        job.Model = AssetTypes::FromPath(entry.Path) == AssetType::Model;
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
        std::vector<std::string> gone;
        for (auto& [k, entry] : m_Entries)
        {
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
                disk.Save(job.Path, job.Stamp, job.Save);
                continue;
            }

            Result result;
            result.Key = job.Key;
            result.Stamp = job.Stamp;
            if (disk.Load(job.Path, job.Stamp, result.Image))
                result.FromDisk = true;
            else if (job.Model)
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

        // One model a frame: loaded (if nothing had it yet), rendered, read back, saved in the background.
        while (!m_RenderQueue.empty())
        {
            std::string key = std::move(m_RenderQueue.front());
            m_RenderQueue.pop_front();
            auto it = m_Entries.find(key);
            if (it == m_Entries.end() || it->second.Status != State::Waiting)
                continue;
            Entry& entry = it->second;
            if (m_Frame - entry.LastUsed > s_ForgetAfterFrames)
            {
                entry.Stamp = {}; // not on screen any more: asked for again when it is
                continue;
            }
            ThumbnailImage image;
            if (RenderModel(entry.Path, image))
            {
                Upload(entry, image);
                entry.Status = State::Ready;
                m_Stats.Rendered++;
                Job save;
                save.Key = key;
                save.Path = entry.Path;
                save.Stamp = entry.Stamp;
                save.Save = std::move(image);
                {
                    std::lock_guard lock(m_Mutex);
                    m_Jobs.insert(m_Jobs.begin(), std::move(save)); // after what is on screen
                }
                m_Wake.notify_one();
            }
            else
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

    bool ThumbnailCache::RenderModel(const fs::path& path, ThumbnailImage& out)
    {
        bool wasLoaded = Model::IsFileLoaded(path);
        Ref<Model> model = Model::Load(path); // a failure is reported once (AssetImportLog)
        if (!model || model->GetSubMeshes().empty())
            return false;

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

        model.reset();
        if (!wasLoaded)
            Model::ReleaseIfUnused(path); // loaded for the picture only

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
