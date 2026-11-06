#pragma once

#include <Zamin/Zamin.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace Zamin {

    // The editor's view of the sub-scenes of the open scene (SubSceneComponent): each file loaded
    // once per version into a scene of its own - read-only, never saved - with its roots placed by
    // the host entity's world transform, and drawn into the scene view over the open scene (without
    // a sky, its own lights; the picking pass does not see it: a click goes through to what is
    // behind). The hierarchy lists its entities under the host. A sub-scene's own sub-scenes are not
    // previewed (they are loaded in play mode).
    class SubScenePreviews
    {
    public:
        // Once a frame in edit mode: previews made / dropped / placed again for `scene`'s hosts (a
        // file that changed on disk is read again; that is checked every few frames).
        void Update(const Ref<Scene>& scene, const std::filesystem::path& scenePath);
        // Over the scene view's picture (the bound framebuffer, after the open scene was drawn).
        void Render(const EditorCamera& camera, const SceneRenderOptions& sceneOptions);

        // The preview of `host`'s sub-scene (null: none, not readable, a loop).
        Ref<Scene> Get(Entity host) const;
        // Why `host` has no preview ("" when it has one or has no SubSceneComponent).
        std::string GetProblem(Entity host) const;
        size_t GetCount() const;
        // Every preview is dropped (another scene opened, play started); the next Update makes them again.
        void Clear() { m_Previews.clear(); m_Owner = nullptr; }
        // Reads `path` again next Update (it was just saved).
        void Invalidate(const std::filesystem::path& path);

    private:
        struct Preview
        {
            std::string Path;                        // as the component has it (relative to the assets)
            std::filesystem::file_time_type Time{};
            uintmax_t Size = 0;
            Ref<Scene> SceneRef;
            std::vector<std::pair<UUID, TransformComponent>> Roots; // as in the file
            glm::mat4 Placed{ 0.0f };                // the host transform the roots were placed by
            std::string Problem;
            uint64_t CheckedFrame = 0;
            bool Shown = true;
        };
        void Load(Preview& preview, const std::filesystem::path& file);

        std::unordered_map<uint64_t, Preview> m_Previews; // by host entity UUID
        const Scene* m_Owner = nullptr;
        uint64_t m_Frame = 0;
    };

}
