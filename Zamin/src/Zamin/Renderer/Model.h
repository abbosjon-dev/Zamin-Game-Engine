#pragma once

#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/Animation.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    enum class ModelUpAxis { Y = 0, Z };

    // How a model file is imported: the "Import" object of its .zmeta file (see AssetImporter).
    struct ModelImportSettings
    {
        float Scale = 1.0f;                   // uniform scale of the whole model (0.01: centimeters -> meters)
        ModelUpAxis UpAxis = ModelUpAxis::Y;  // Z: the file is Z-up (CAD, some exporters) and is turned to Y-up
        bool FlipUVs = false;                 // V = 1 - V (textures shown upside down)

        // What is applied to the file's geometry and skeleton: scale, then the up-axis rotation.
        glm::mat4 GetTransform() const;
        bool operator==(const ModelImportSettings&) const = default;
    };

    // Applies an import transform to a skeleton whose skinned vertices were transformed by
    // `transform`: the skinned result is the old one transformed the same way (animations, which
    // are joint-local, stay as they are).
    void ApplyImportTransform(Skeleton& skeleton, const glm::mat4& transform);

    // A model asset made of several meshes, each with its own material (glTF / OBJ).
    class Model
    {
    public:
        struct SubMesh
        {
            Ref<Mesh> MeshRef;
            Material Mat;
            std::string Name;
        };

        const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
        const AABB& GetBounds() const { return m_Bounds; }
        const std::filesystem::path& GetPath() const { return m_Path; }

        const Skeleton& GetSkeleton() const { return m_Skeleton; }
        const std::vector<AnimationClip>& GetAnimations() const { return m_Animations; }
        bool IsSkinned() const { return !m_Skeleton.Empty(); }
        int FindAnimation(const std::string& name) const
        {
            for (size_t i = 0; i < m_Animations.size(); i++)
                if (m_Animations[i].Name == name) return (int)i;
            return -1;
        }

        const ModelImportSettings& GetImportSettings() const { return m_ImportSettings; }
        // Other files the model is read from (external .gltf buffers), absolute.
        const std::vector<std::filesystem::path>& GetDependencies() const { return m_Dependencies; }

        // Reads the file again into this model with its current import settings: the same object
        // (components, animators and previews keep it) gets the new meshes, materials, skeleton
        // and clips. False - and the old content stays - when the file cannot be read; the reason
        // goes to `error`, or to the log without one.
        bool Reload(std::string* error = nullptr);
        // The same with the given import settings.
        bool Reimport(const ModelImportSettings& settings, std::string* error = nullptr);

        // .gltf / .glb (meshes, node transforms, PBR materials + textures) or .obj, imported with
        // the file's .zmeta settings (AssetImporter::GetModelSettings). Cached by path.
        static Ref<Model> Load(const std::filesystem::path& path);
        // Every cached model made from this file (or reading it as a buffer) is read again in
        // place. Returns how many were updated; `failed` is set when the file could not be read.
        // A file that could not be loaded before is forgotten, so the next Load tries again.
        static size_t ReloadFile(const std::filesystem::path& path, bool* failed = nullptr);
        // Whether a cached model was made from this file (or reads it).
        static bool IsFileLoaded(const std::filesystem::path& path);
        static void ClearCache();

    private:
        std::vector<SubMesh> m_SubMeshes;
        AABB m_Bounds;
        std::filesystem::path m_Path;
        Skeleton m_Skeleton;
        std::vector<AnimationClip> m_Animations;
        ModelImportSettings m_ImportSettings;
        std::vector<std::filesystem::path> m_Dependencies;
        friend class ModelLoader;
    };

}
