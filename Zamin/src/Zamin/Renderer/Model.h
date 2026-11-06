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

    // How the materials of a model file are written out as material assets (Model::ExportMaterials).
    struct ModelMaterialExportOptions
    {
        // Where the .zmat files (and the images taken out of the file) go; empty = the default,
        // "<model folder>/<model name>_Materials" (GetDefaultMaterialExportDirectory).
        std::filesystem::path OutputDirectory;
        // Files of the same names that are already there (an earlier export) are written again;
        // false: they are kept as they are and used.
        bool Overwrite = false;
    };

    struct ModelMaterialExport
    {
        std::filesystem::path OutputDirectory;          // absolute
        // Per material of the file, in its order: its .zmat (absolute) - written, or kept.
        std::vector<std::filesystem::path> Materials;
        std::vector<std::filesystem::path> Written;      // files written: materials and extracted images
        std::vector<std::filesystem::path> Kept;         // files that were there and were left alone
        std::vector<std::string> Warnings;              // images that could not be taken out (missing, unknown format)
    };

    // A model asset made of several meshes, each with its own material (glTF / OBJ).
    class Model
    {
    public:
        struct SubMesh
        {
            Ref<Mesh> MeshRef;
            Material Mat;
            std::string Name;
            int MaterialIndex = -1; // the file's material it was made with (-1 = none; .obj: 0)
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
        // A model of a skeleton and its clips only - no meshes, no file, not cached: procedural
        // rigs and tests (needs no GPU).
        static Ref<Model> CreateSkeletal(Skeleton skeleton, std::vector<AnimationClip> animations);
        // The materials of a model file as the importer makes them - values and modes (glTF
        // alphaMode OPAQUE / MASK -> Cutout with alphaCutoff / BLEND -> Transparent, doubleSided,
        // KHR_materials_unlit) without their textures, in the file's material order (.obj: one
        // default material). Reads no buffers or images and needs no GPU. False, with the reason
        // in `error`, when the file cannot be read.
        static bool ReadMaterials(const std::filesystem::path& path, std::vector<Material>& out, std::string* error = nullptr);
        // Writes the materials of a glTF file (.gltf / .glb) as material assets (.zmat) with what
        // ReadMaterials reads plus their textures: image files the model uses are referred to
        // where they are (copied next to the materials when they are outside the asset folder),
        // images inside the file (.glb buffers, data URIs) are taken out as files (.png / .jpg as
        // they are stored) next to the materials. Names come from the file's materials / images
        // (made safe for file names, unique within the export). With an active project the new
        // files get UUIDs at once (written next to the texture paths). Needs no GPU. False, with
        // the reason in `error`, when the file cannot be read, is not glTF or has no materials.
        static bool ExportMaterials(const std::filesystem::path& path, const ModelMaterialExportOptions& options,
                                    ModelMaterialExport& result, std::string* error = nullptr);
        static std::filesystem::path GetDefaultMaterialExportDirectory(const std::filesystem::path& modelPath);
        // Every cached model made from this file (or reading it as a buffer) is read again in
        // place. Returns how many were updated; `failed` is set when the file could not be read.
        // A file that could not be loaded before is forgotten, so the next Load tries again.
        static size_t ReloadFile(const std::filesystem::path& path, bool* failed = nullptr);
        // Whether a cached model was made from this file (or reads it).
        static bool IsFileLoaded(const std::filesystem::path& path);
        // Drops the cached model of a file when nothing else holds it (it was loaded for a moment,
        // e.g. for a content browser thumbnail). Returns true when it was dropped.
        static bool ReleaseIfUnused(const std::filesystem::path& path);
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
