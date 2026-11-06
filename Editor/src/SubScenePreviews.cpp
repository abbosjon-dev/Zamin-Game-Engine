#include "SubScenePreviews.h"

#include <Zamin/Renderer/RenderCommand.h>

namespace Zamin {

    namespace fs = std::filesystem;

    // A file is looked at again for changes this often (frames).
    static constexpr uint64_t CheckInterval = 30;

    void SubScenePreviews::Load(Preview& preview, const fs::path& file)
    {
        preview.SceneRef.reset();
        preview.Roots.clear();
        preview.Problem.clear();
        preview.Placed = glm::mat4(0.0f);
        std::error_code ec;
        preview.Time = fs::last_write_time(file, ec);
        preview.Size = ec ? 0 : fs::file_size(file, ec);
        if (!fs::is_regular_file(file, ec))
        {
            preview.Problem = "fayl topilmadi";
            return;
        }
        Ref<Scene> scene = CreateRef<Scene>();
        SceneSerializer serializer(scene);
        if (!serializer.Deserialize(file))
        {
            preview.Problem = serializer.GetLastError().empty() ? std::string("o'qilmadi") : serializer.GetLastError();
            return;
        }
        scene->ResolveAssets();
        for (Entity root : scene->GetRootEntities())
            preview.Roots.emplace_back(root.GetUUID(), root.GetComponent<TransformComponent>());
        preview.SceneRef = scene;
    }

    void SubScenePreviews::Update(const Ref<Scene>& scene, const fs::path& scenePath)
    {
        m_Frame++;
        if (!scene)
        {
            Clear();
            return;
        }
        if (m_Owner != scene.get())
        {
            m_Previews.clear();
            m_Owner = scene.get();
        }
        const std::string ownPath = scenePath.empty() ? std::string() : Project::GetAssetRelativePath(scenePath);
        std::vector<uint64_t> seen;
        for (Entity host : scene->FindEntitiesWith<SubSceneComponent>())
        {
            const auto& c = host.GetComponent<SubSceneComponent>();
            if (c.ScenePath.empty())
                continue;
            const uint64_t id = (uint64_t)host.GetUUID();
            seen.push_back(id);
            Preview& preview = m_Previews[id];
            preview.Shown = c.ShowInEditor && !scene->IsHiddenInEditor(host) && scene->IsActiveInHierarchy(host);
            const fs::path file = Project::GetAssetFileSystemPath(c.ScenePath);
            if (preview.Path != c.ScenePath)
            {
                preview.Path = c.ScenePath;
                if (!ownPath.empty() && fs::path(c.ScenePath).lexically_normal().generic_string() == ownPath)
                {
                    // The open scene in itself: shown nowhere (the game skips it too).
                    preview.SceneRef.reset();
                    preview.Roots.clear();
                    preview.Problem = "sahna o'zining ichida (halqa)";
                    continue;
                }
                Load(preview, file);
                preview.CheckedFrame = m_Frame;
            }
            else if (m_Frame - preview.CheckedFrame >= CheckInterval && preview.Problem != "sahna o'zining ichida (halqa)")
            {
                // Saved elsewhere since (its own scene was edited): read again.
                preview.CheckedFrame = m_Frame;
                std::error_code ec;
                const auto time = fs::last_write_time(file, ec);
                const uintmax_t size = ec ? 0 : fs::file_size(file, ec);
                if (time != preview.Time || size != preview.Size)
                    Load(preview, file);
            }
            if (!preview.SceneRef)
                continue;
            // Placed by the host as the game will place it (its transform now).
            const glm::mat4 world = c.ApplyTransform ? scene->GetWorldTransform(host) : glm::mat4(1.0f);
            if (world != preview.Placed)
            {
                for (const auto& [rootID, transform] : preview.Roots)
                    if (Entity root = preview.SceneRef->GetEntityByUUID(rootID))
                        root.GetComponent<TransformComponent>() = transform;
                SceneManager::OffsetRoots(*preview.SceneRef, world);
                preview.Placed = world;
            }
        }
        for (auto it = m_Previews.begin(); it != m_Previews.end();)
            it = std::find(seen.begin(), seen.end(), it->first) == seen.end() ? m_Previews.erase(it) : std::next(it);
    }

    void SubScenePreviews::Render(const EditorCamera& camera, const SceneRenderOptions& sceneOptions)
    {
        if (m_Previews.empty())
            return;
        SceneRenderOptions options;
        options.Overlay = true;           // over the open scene's picture: no sky
        options.SkipEditorHidden = true;
        options.Parallax = sceneOptions.Parallax;
        options.DebugView = sceneOptions.DebugView;
        options.Wireframe = sceneOptions.Wireframe;
        // The entity IDs stay the open scene's (a preview's IDs mean nothing there).
        RenderCommand::SetColorWriteMask(1, false, false);
        for (auto& [id, preview] : m_Previews)
            if (preview.Shown && preview.SceneRef)
                preview.SceneRef->OnRenderEditor(camera, options);
        RenderCommand::SetColorWriteMask(1, true, true);
    }

    Ref<Scene> SubScenePreviews::Get(Entity host) const
    {
        if (!host.IsValid() || host.GetScene() != m_Owner)
            return nullptr;
        auto it = m_Previews.find((uint64_t)host.GetUUID());
        return it != m_Previews.end() ? it->second.SceneRef : nullptr;
    }

    std::string SubScenePreviews::GetProblem(Entity host) const
    {
        if (!host.IsValid() || host.GetScene() != m_Owner)
            return {};
        auto it = m_Previews.find((uint64_t)host.GetUUID());
        return it != m_Previews.end() ? it->second.Problem : std::string();
    }

    size_t SubScenePreviews::GetCount() const
    {
        size_t count = 0;
        for (const auto& [id, preview] : m_Previews)
            count += preview.SceneRef ? 1 : 0;
        return count;
    }

    void SubScenePreviews::Invalidate(const fs::path& path)
    {
        const std::string relative = path.is_absolute() ? Project::GetAssetRelativePath(path) : path.generic_string();
        for (auto& [id, preview] : m_Previews)
            if (fs::path(preview.Path).lexically_normal().generic_string() == relative)
                preview.Path.clear(); // read again next Update
    }

}
