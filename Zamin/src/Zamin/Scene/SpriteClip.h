#pragma once

#include "Zamin/Core/Base.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // A moment of a sprite animation clip: when the frame is shown, the entity's script gets
    // OnAnimationEvent (its Name, Parameter, the clip's name) and the event bus "AnimationEvent".
    struct SpriteClipEvent
    {
        int Frame = 0;              // 0 = the clip's first frame
        std::string Name;
        std::string Parameter;      // optional text handed over with it
        bool operator==(const SpriteClipEvent&) const = default;
    };

    // A sprite animation clip: the content of a .zsanim file. Its frames are named frames of a
    // texture's sprite sheet (the Sprite Editor's frames, in this order) or, without names, cells
    // of a grid (Columns x Rows, numbered left to right, top to bottom; StartFrame..+FrameCount).
    // Texture: the picture it is made for - set on the Sprite Renderer when the clip plays ("" =
    // whatever the renderer has).
    struct SpriteClip
    {
        static constexpr int CurrentVersion = 1;
        static constexpr const char* Extension = ".zsanim";

        std::string Texture;                    // asset path ("" = the renderer's own)
        std::vector<std::string> Frames;        // sprite sheet frame names ("" list = the grid)
        int Columns = 4;
        int Rows = 1;
        int StartFrame = 0;
        int FrameCount = 4;
        float FPS = 10.0f;
        bool Loop = true;
        std::vector<SpriteClipEvent> Events;    // by frame

        bool UsesFrameList() const { return !Frames.empty(); }
        int GetFrameCount() const { return Frames.empty() ? std::max(1, FrameCount) : (int)Frames.size(); }
        float GetDuration() const { return FPS > 0.0f ? (float)GetFrameCount() / FPS : 0.0f; }
        void SortEvents();
        bool operator==(const SpriteClip&) const = default;
    };

    class SpriteClipSerializer
    {
    public:
        static std::string SerializeToString(const SpriteClip& clip);
        static bool DeserializeFromString(const std::string& text, SpriteClip& out, std::string* error = nullptr);
        static bool Serialize(const SpriteClip& clip, const std::filesystem::path& path);
        static bool Deserialize(const std::filesystem::path& path, SpriteClip& out, std::string* error = nullptr);
    };

    // A .zsanim file's clip, shared by every Sprite Animation that uses it (SpriteClipLibrary):
    // a change (the asset inspector, a reload) shows on all of them.
    class SpriteClipAsset
    {
    public:
        SpriteClipAsset() = default;
        SpriteClipAsset(SpriteClip clip, std::filesystem::path path) : m_Clip(std::move(clip)), m_Path(std::move(path)) {}

        const SpriteClip& GetClip() const { return m_Clip; }
        void SetClip(SpriteClip clip);
        // The clip's name: its file name without the extension.
        std::string GetName() const { return m_Path.stem().string(); }
        const std::filesystem::path& GetPath() const { return m_Path; }
        void SetPath(const std::filesystem::path& path) { m_Path = path; }
        // Changes with every change of the clip (SetClip, Reload).
        uint64_t GetRevision() const { return m_Revision; }
        bool Reload(std::string* error = nullptr);
        bool Save() const;

    private:
        SpriteClip m_Clip;
        std::filesystem::path m_Path;
        uint64_t m_Revision = 1;
    };

    // Caches sprite clips by file.
    class SpriteClipLibrary
    {
    public:
        // The clip of a .zsanim file (absolute, or relative to the asset folder); nullptr when it
        // cannot be read (reported once, AssetImportLog).
        static Ref<SpriteClipAsset> Load(const std::filesystem::path& path);
        static Ref<SpriteClipAsset> Find(const std::filesystem::path& path);
        static bool IsFileLoaded(const std::filesystem::path& path);
        // The file changed on disk: its cached clip is read again in place. Returns 1 when the
        // clip changed.
        static size_t ReloadFile(const std::filesystem::path& path, bool* failed = nullptr);
        static void OnFileMoved(const std::filesystem::path& from, const std::filesystem::path& to);
        static void Clear();
    };

    // The frame indices (0 = the clip's first) shown while a clip `count` frames long plays from
    // frame index `from` (the one shown before; -1: nothing yet, so the first frame shown counts)
    // to `to` (unbounded: frames are Time x FPS; loops wrap). Each frame entered once, in order;
    // a step of a whole cycle or more enters every frame once. Not looping: clamped to the last.
    void CollectSpriteFrames(int from, int to, int count, bool loop, std::vector<int>& out);

}
