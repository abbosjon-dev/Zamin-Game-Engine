#pragma once

#include <Zamin/Zamin.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace Zamin {

    // Scene thumbnails in the editor (SceneMetadata::GetThumbnailPath): drawn when a scene is saved,
    // shown at their own size by the inspector and the Scenes panel (the content browser's tiles go
    // through its ThumbnailCache).
    class SceneThumbnails
    {
    public:
        static constexpr float ParticleWarmUpSeconds = 2.0f;

        // Draws `scene` through its primary camera - what the game shows first - else through
        // `fallback` (the editor camera: a scene without a camera) into the thumbnail file of
        // `scenePath` (SceneMetadata::ThumbnailWidth x ThumbnailHeight). False when nothing could be
        // drawn or written. The scene itself is not touched (a copy is drawn; its particle systems
        // run ParticleWarmUpSeconds first, as the scene view plays them - else they would be empty).
        static bool Render(const Ref<Scene>& scene, const std::filesystem::path& scenePath, const EditorCamera* fallback);

        // The thumbnail of a scene file as a texture (nullptr without one): loaded once per version
        // of the picture file (its size and time are checked at most every few frames).
        Ref<Texture2D> Get(const std::filesystem::path& scenePath);
        // Forgets one scene's texture (its picture was drawn again) / all of them.
        void Invalidate(const std::filesystem::path& scenePath);
        void Clear() { m_Textures.clear(); }
        // Once a frame (the checks of changed pictures are spread out).
        void Update() { m_Frame++; }

    private:
        struct Entry
        {
            Ref<Texture2D> Texture;
            uintmax_t Size = 0;
            std::filesystem::file_time_type Time{};
            uint64_t CheckedFrame = 0;
            bool Missing = false;
        };
        std::unordered_map<std::string, Entry> m_Textures;
        uint64_t m_Frame = 0;
    };

}
