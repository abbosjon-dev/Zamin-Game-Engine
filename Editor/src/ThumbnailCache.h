#pragma once

#include <Zamin/Zamin.h>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Zamin {

    // The content browser's thumbnails: small GPU textures of images, HDR maps and fonts (decoded
    // and scaled down on a background thread) and of models, materials and prefabs (rendered on the
    // main thread, one a frame - a material on a sphere with MaterialPreview, a prefab placed in a
    // scene of its own and drawn from the view that suits it: PrefabPlacement::ChooseThumbnailView). They are kept in memory by
    // path and file version - asked for again only when the file's size or time changes (for a
    // material also one of its textures) - and saved under the project's ".zamin/Thumbnails", so a
    // project shows them at once the next time it opens. A material that is loaded follows its edits
    // live (drawn again a few times a second while it changes; saved to disk only when it matches
    // its file). Finished thumbnails go to the GPU a few per frame, so a folder of large images does
    // not stall the editor.
    class ThumbnailCache
    {
    public:
        static constexpr uint32_t Size = 128;

        ThumbnailCache();
        ~ThumbnailCache();
        ThumbnailCache(const ThumbnailCache&) = delete;
        ThumbnailCache& operator=(const ThumbnailCache&) = delete;

        // Where thumbnails are saved ("" = in memory only). Forgets the ones in memory and removes
        // saved thumbnails of files that are gone (in the background).
        void SetCacheDirectory(const std::filesystem::path& directory);
        const std::filesystem::path& GetCacheDirectory() const { return m_CacheDirectory; }

        static bool CanHaveThumbnail(const std::filesystem::path& path);
        // The thumbnail of a file in this version (the listing's size and time): nullptr while it is
        // being made or when the file has none (draw an icon). Asks for it when needed; the
        // previous picture stays while a changed file's new one is made.
        Ref<Texture2D> Get(const std::filesystem::path& path, uintmax_t size, std::filesystem::file_time_type time);
        // The same for a file whose size and time are not at hand (reads them; nullptr when the
        // file is gone) - asset fields of the inspector.
        Ref<Texture2D> GetForFile(const std::filesystem::path& path);
        // Once a frame: finished thumbnails go to the GPU (a few per frame), one model is rendered.
        void Update();
        // The file (a folder: everything inside) changed or is gone: its thumbnail is made again.
        void Invalidate(const std::filesystem::path& path);
        // Forgets the thumbnails in memory (saved ones stay).
        void Clear();

        struct Stats
        {
            size_t Textures = 0;      // in memory
            size_t Waiting = 0;       // asked for, not done yet
            uint64_t FromDisk = 0, Generated = 0, Rendered = 0, Failed = 0;
            uint64_t MaterialsRendered = 0; // also live redraws of edited materials
            uint64_t ReleasedTextures = 0; // loaded with a model for its picture only, freed again
        };
        Stats GetStats() const;

    private:
        enum class State : uint8_t { Waiting, Ready, Failed };
        struct Entry
        {
            Ref<Texture2D> Texture;
            ThumbnailStamp Stamp;           // the version asked for (the listing's size and time)
            ThumbnailStamp DiskStamp;       // with the versions of its dependencies (materials: textures)
            std::filesystem::path Path;
            State Status = State::Waiting;
            uint64_t LastUsed = 0;          // frame
            bool Material = false;
            bool Prefab = false;
            // Materials: their textures (ThumbnailDiskCache::MakeKey), the loaded material the
            // picture follows (when something has it loaded) and the revision it was drawn at.
            std::vector<std::string> Dependencies;
            std::weak_ptr<MaterialAsset> Live;
            uint64_t RenderedRevision = 0;
            uint64_t LiveCheckFrame = 0;
            double LastRenderTime = -1.0;
            bool Redraw = false;            // drawn again from the live material (the picture stays meanwhile)
        };
        struct Job
        {
            std::string Key;
            std::filesystem::path Path;
            ThumbnailStamp Stamp;
            ThumbnailStamp DiskStamp;
            bool Render = false;            // disk cache only: a miss is rendered on the main thread
            ThumbnailImage Save;            // set: only save this (a rendered picture) to the disk cache
        };
        struct Result
        {
            std::string Key;
            ThumbnailStamp Stamp;
            ThumbnailImage Image;
            std::string Error;
            bool FromDisk = false;
            bool NeedsRender = false;
        };

        void Request(const std::string& key, Entry& entry);
        void WorkerLoop();
        void Upload(Entry& entry, const ThumbnailImage& image);
        bool RenderModel(const std::filesystem::path& path, ThumbnailImage& out);
        bool RenderPrefab(const std::filesystem::path& path, ThumbnailImage& out);
        // A prefab's picture also shows its assets (textures, models, fonts ...): their files.
        static std::vector<std::filesystem::path> PrefabDependencies(const std::filesystem::path& path);
        // `matchesFile`: the picture shows what the file holds (not unsaved edits) - it may be saved.
        bool RenderMaterial(Entry& entry, ThumbnailImage& out, bool& matchesFile);
        // Live materials that changed since they were drawn are queued to be drawn again.
        void FollowLiveMaterial(const std::string& key, Entry& entry);
        void SaveToDisk(const std::string& key, const Entry& entry, ThumbnailImage image);
        void Evict();

    private:
        std::unordered_map<std::string, Entry> m_Entries;   // main thread only
        std::deque<std::string> m_RenderQueue;              // models to render (keys)
        std::deque<Result> m_Ready;                         // taken from the worker, not uploaded yet
        std::filesystem::path m_CacheDirectory;
        uint64_t m_Frame = 0;
        Stats m_Stats;
        Ref<Framebuffer> m_ModelTarget, m_ModelFinal;
        MaterialPreview m_MaterialPreview;

        // shared with the worker
        mutable std::mutex m_Mutex;
        std::condition_variable m_Wake;
        std::vector<Job> m_Jobs;                            // the newest first out (what is on screen now)
        std::vector<Result> m_Results;
        ThumbnailDiskCache m_Disk;
        bool m_PruneRequested = false;
        bool m_Stop = false;
        std::thread m_Thread;
    };

}
