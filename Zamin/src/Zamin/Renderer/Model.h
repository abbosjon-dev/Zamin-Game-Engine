#pragma once

#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/Animation.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    enum class ModelUpAxis { Y = 0, Z };

    // A clip the importer makes out of one of the file's clips (the "Clips" list of a model's import
    // settings): the part of `Source` from Start to End seconds, named Name. Named like its source
    // it replaces it (the clip trimmed); with another name it is a clip of its own (sub-clips:
    // "Walk", "Run" and "Jump" cut out of one long take).
    struct ModelClipSettings
    {
        std::string Source;     // the file's clip
        std::string Name;       // "" = Source (the source trimmed in place)
        float Start = 0.0f;     // seconds into Source
        float End = -1.0f;      // seconds into Source (< 0, or past its end: its end)

        const std::string& GetName() const { return Name.empty() ? Source : Name; }
        bool operator==(const ModelClipSettings&) const = default;
    };

    // How a model file is imported: the "Import" object of its .zmeta file (see AssetImporter).
    struct ModelImportSettings
    {
        float Scale = 1.0f;                   // uniform scale of the whole model (0.01: centimeters -> meters)
        ModelUpAxis UpAxis = ModelUpAxis::Y;  // Z: the file is Z-up (CAD, some exporters) and is turned to Y-up
        bool FlipUVs = false;                 // V = 1 - V (textures shown upside down)

        // ---- animation clips: trimmed / split (see ModelClipSettings)
        std::vector<ModelClipSettings> Clips;
        // false: a clip of the file that sub-clips are cut out of is not kept itself (only its
        // sub-clips; one named like it is its trimmed version). Clips nothing is cut from stay.
        bool KeepSourceClips = true;
        float ClipFrameRate = 30.0f;          // frames the editor shows clip times in (display only)

        // What is applied to the file's geometry and skeleton: scale, then the up-axis rotation.
        glm::mat4 GetTransform() const;
        bool operator==(const ModelImportSettings&) const = default;
    };

    // The clips of a model as its import settings make them out of the file's `clips`: each
    // ModelClipSettings cut out (TrimClip) - in place of its source when named like it, after the
    // file's clips otherwise; sources dropped when !keepSources. Entries whose source is missing
    // or whose name is taken by an earlier entry are skipped (their reasons go to `warnings`).
    void ApplyClipSettings(std::vector<AnimationClip>& clips, const std::vector<ModelClipSettings>& settings, bool keepSources,
                           std::vector<std::string>* warnings = nullptr);

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

    // A glTF mesh node with morph targets (blend shapes): what its targets are called and the
    // weights it has by default. A model's weights are one flat list, its groups after each other.
    struct MorphGroup
    {
        std::string Name;                       // the node's (or its mesh's) name
        std::vector<std::string> Targets;       // the file's target names ("Target N" without them)
        std::vector<float> DefaultWeights;      // the node's / mesh's weights (0 where it has none)
        uint32_t Offset = 0;                    // where its weights start in the model's flat list

        uint32_t GetCount() const { return (uint32_t)Targets.size(); }
    };

    // The morph targets of one submesh: its vertices without any target, and what each target adds
    // to their positions and normals. Shared by everything drawing the model; per-entity weights
    // make per-entity meshes from it (ApplyMorphTargets).
    struct MorphTargetData
    {
        int Group = -1;                                 // index into Model::GetMorphGroups()
        bool Skinned = false;
        std::vector<MeshVertex> Vertices;               // the base vertices (not skinned meshes)
        std::vector<SkinnedVertex> SkinnedVertices;     // the base vertices (skinned meshes)
        std::vector<uint32_t> Indices;
        std::vector<std::vector<glm::vec3>> PositionDeltas; // per target, one per vertex
        std::vector<std::vector<glm::vec3>> NormalDeltas;   // per target (empty: it has none)

        size_t GetVertexCount() const { return Skinned ? SkinnedVertices.size() : Vertices.size(); }
        size_t GetTargetCount() const { return PositionDeltas.size(); }
    };

    // The base vertices plus each target's offsets times its weight (`weights`: one per target of
    // the submesh's group, missing ones 0); normals renormalized.
    void ApplyMorphTargets(const MorphTargetData& data, const float* weights, size_t count, std::vector<MeshVertex>& out);
    void ApplyMorphTargets(const MorphTargetData& data, const float* weights, size_t count, std::vector<SkinnedVertex>& out);

    // One skin of a model file (glTF "skins"): the joints the vertices of its meshes are weighted to,
    // each with its inverse bind matrix. The model's skeleton holds the joints of every skin (one
    // joint per node, parents first), so one Animator poses all of them - a body, its clothes and a
    // second creature in the same file - and each skin picks its own joints out of that pose.
    struct ModelSkin
    {
        std::string Name;
        std::vector<int> Joints;              // skin joint k -> joint of the model's skeleton
        std::vector<glm::mat4> InverseBind;   // per skin joint (the import transform applied)
        // true: its vertices refer to the skeleton's joints themselves (every joint's inverse bind
        // is the skeleton's and its index is under MaxJoints): drawn with the skeleton's matrices.
        // false: they refer to the skin's own joints, drawn with the palette GetSkinMatrices makes.
        bool Direct = true;
        std::vector<glm::mat4> Correction;    // !Direct: inverse(the skeleton joint's inverse bind) x InverseBind
    };

    // Decides how a skin is drawn with `skeleton` (Direct) and prepares its palette (Correction).
    // keepMode: Direct stays as it is (the vertices were made for it); only Correction is updated.
    void PrepareModelSkin(ModelSkin& skin, const Skeleton& skeleton, bool keepMode = false);
    // The skinning matrices of a skin's vertices from the skeleton's (an Animator's JointMatrices,
    // EvaluatePose): `skeletonMatrices` themselves for a Direct skin, else its palette built in
    // `scratch` (joint k: the skeleton joint's matrix x Correction[k] = its global x the skin's
    // inverse bind; at most MaxJoints).
    const std::vector<glm::mat4>& GetSkinMatrices(const ModelSkin& skin, const std::vector<glm::mat4>& skeletonMatrices,
                                                  std::vector<glm::mat4>& scratch);
    // A vertex's four joint influences made to fit the GPU's palette (joints under MaxJoints): the
    // weight of a joint past it goes to the vertex's other joints (the weights renormalized); with
    // none of them left, the vertex follows joint 0.
    void FitJointInfluences(glm::ivec4& joints, glm::vec4& weights);

    class Model;
    // The weights `clip` gives the morph groups it animates at `time` seconds, written into the
    // model's flat list `weights` (resized to it; the groups the clip does not animate keep theirs).
    // Returns true when the clip animates any.
    bool SampleClipMorphWeights(const Model& model, const AnimationClip* clip, float time, std::vector<float>& weights);

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
            // Morph targets (glTF blend shapes): MeshRef shows them at the group's default weights.
            Ref<MorphTargetData> Morph;
            int Skin = -1;          // skinned meshes: index into GetSkins()
        };

        const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
        const AABB& GetBounds() const { return m_Bounds; }
        const std::filesystem::path& GetPath() const { return m_Path; }

        const Skeleton& GetSkeleton() const { return m_Skeleton; }
        const std::vector<AnimationClip>& GetAnimations() const { return m_Animations; }
        // The file's clips as they are in it - before the import settings trim / split them
        // (ModelImportSettings::Clips; the editor's clip settings preview these).
        const std::vector<AnimationClip>& GetSourceAnimations() const { return m_ClipsCut ? m_SourceAnimations : m_Animations; }
        bool IsSkinned() const { return !m_Skeleton.Empty(); }
        // The file's skins (glTF: one per skinned mesh node, often several in one file).
        const std::vector<ModelSkin>& GetSkins() const { return m_Skins; }
        // The skinning matrices to draw submesh `index` with, from the skeleton's (an Animator's
        // JointMatrices, EvaluatePose): those themselves, or its skin's palette in `scratch`.
        const std::vector<glm::mat4>& GetSubMeshJoints(size_t index, const std::vector<glm::mat4>& skeletonMatrices,
                                                       std::vector<glm::mat4>& scratch) const;
        // Models made in code (tests, procedural rigs): the skins their submeshes refer to.
        void SetSkins(std::vector<ModelSkin> skins) { m_Skins = std::move(skins); }
        int FindAnimation(const std::string& name) const
        {
            for (size_t i = 0; i < m_Animations.size(); i++)
                if (m_Animations[i].Name == name) return (int)i;
            return -1;
        }

        // The pose cache of one of its clips at `rate` frames per second (baked when first asked for;
        // nullptr for a clip that is not one of GetAnimations()). Each clip keeps one cache per rate
        // (up to MaxPoseCacheRates; past that the one asked for longest ago is made again for the new
        // rate), so animators of the model with different rates share theirs instead of baking the
        // clip over and over. Dropped when the file is read again.
        static constexpr size_t MaxPoseCacheRates = 8;
        // Main thread only (it may bake and evict).
        const BakedClip* GetBakedClip(const AnimationClip& clip, float rate) const;
        // The cache GetBakedClip has made already (up to date), or nullptr - it changes nothing, so
        // several threads may ask at the same time while nobody calls GetBakedClip (the job system's
        // animator poses: baked first on the main thread, sampled on the workers).
        const BakedClip* FindBakedClip(const AnimationClip& clip, float rate) const;
        // Goes up each time the file is read again (Reload / Reimport): what was made from the old
        // skeleton and clips is out of date.
        uint64_t GetRevision() const { return m_Revision; }

        // ---- morph targets (blend shapes)
        const std::vector<MorphGroup>& GetMorphGroups() const { return m_MorphGroups; }
        bool HasMorphTargets() const { return !m_MorphGroups.empty(); }
        // Weights in the model's flat list (every group's targets after each other).
        uint32_t GetMorphWeightCount() const
        {
            return m_MorphGroups.empty() ? 0u : m_MorphGroups.back().Offset + m_MorphGroups.back().GetCount();
        }
        // The flat index of a target: its name, or "Group/Target" (-1 when there is none).
        int FindMorphTarget(const std::string& name) const;
        // "Target" (one group) or "Group/Target" of a flat index ("" outside the list).
        std::string GetMorphTargetName(uint32_t index) const;
        // The default weights of every group as one flat list.
        std::vector<float> GetDefaultMorphWeights() const;
        // Models made in code (tests, procedural meshes): the morph groups the clips' weight channels refer to.
        void SetMorphGroups(std::vector<MorphGroup> groups);

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
        // The skeleton and skins of a glTF file as Load makes them (every skin's joints, the import
        // settings' transform applied) - reads no meshes or images and needs no GPU. False, with
        // the reason in `error`, when the file cannot be read.
        static bool ReadSkins(const std::filesystem::path& path, Skeleton& skeleton, std::vector<ModelSkin>& skins,
                              std::string* error = nullptr);
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
        std::vector<ModelSkin> m_Skins;
        std::vector<AnimationClip> m_Animations;
        std::vector<AnimationClip> m_SourceAnimations; // only when the import settings cut clips (m_ClipsCut)
        bool m_ClipsCut = false;
        std::vector<MorphGroup> m_MorphGroups;
        ModelImportSettings m_ImportSettings;
        std::vector<std::filesystem::path> m_Dependencies;
        struct PoseCacheEntry
        {
            Ref<BakedClip> Baked;
            uint64_t LastUse = 0; // m_PoseCacheUses when it was last asked for
        };
        mutable std::vector<std::vector<PoseCacheEntry>> m_PoseCache; // per clip of m_Animations: one per rate (GetBakedClip)
        mutable uint64_t m_PoseCacheUses = 0;
        uint64_t m_Revision = 0;
        friend class ModelLoader;
    };

}
