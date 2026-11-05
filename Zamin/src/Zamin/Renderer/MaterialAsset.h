#pragma once

#include "Zamin/Renderer/Material.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // What a material asset file (.zmat, JSON) holds: the material's values and its textures as
    // paths relative to the asset folder. MaterialAsset turns it into a renderer Material.
    struct MaterialData
    {
        static constexpr int CurrentVersion = 1;

        MaterialShader Shader = MaterialShader::Lit;
        MaterialBlendMode BlendMode = MaterialBlendMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool DoubleSided = false;
        bool PreserveSpecular = true;      // Transparent: highlights / reflections do not fade with alpha

        glm::vec4 Albedo{ 1.0f };          // sRGB color, alpha = opacity
        std::string AlbedoMap;
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        std::string MetalRoughMap;         // glTF layout: G = roughness, B = metallic
        std::string NormalMap;
        float NormalStrength = 1.0f;
        glm::vec3 Emission{ 0.0f };        // light given off: Emission * EmissionStrength (x EmissiveMap)
        float EmissionStrength = 0.0f;
        std::string EmissiveMap;
        glm::vec2 Tiling{ 1.0f };
        glm::vec2 Offset{ 0.0f };

        bool operator==(const MaterialData&) const = default;

        // The texture paths that are set, in the order AlbedoMap, MetalRoughMap, NormalMap, EmissiveMap.
        std::vector<std::string> GetTexturePaths() const;
        // Copies the values (not the textures) into a renderer material.
        void ApplyValuesTo(Material& material) const;
        // The values of a renderer material (texture paths stay empty: textures do not know them).
        static MaterialData FromMaterial(const Material& material);
    };

    // .zmat file <-> MaterialData. Texture paths are written with the asset's UUID next to them
    // ("AlbedoMapUUID", like scenes do) when the project's registry knows the file, so a material
    // keeps its textures when they are renamed or moved outside the editor.
    class MaterialSerializer
    {
    public:
        static std::string SerializeToString(const MaterialData& data);
        // False, with the reason in `error`, when the text is not a material (broken JSON, not an
        // object). Missing keys keep their defaults; an old single number "Tiling" means the same
        // tiling on both axes; unknown shader / blend mode names fall back to Lit / Opaque.
        static bool DeserializeFromString(const std::string& text, MaterialData& out, std::string* error = nullptr);
        static bool Serialize(const MaterialData& data, const std::filesystem::path& path);
        static bool Deserialize(const std::filesystem::path& path, MaterialData& out, std::string* error = nullptr);

        static const char* ShaderToString(MaterialShader shader);
        static std::optional<MaterialShader> ShaderFromString(std::string_view text);
        static const char* BlendModeToString(MaterialBlendMode mode);
        static std::optional<MaterialBlendMode> BlendModeFromString(std::string_view text);
    };

    // A material asset: the data of a .zmat file and the renderer material made of it (its textures
    // loaded from the project). One object per file is shared by everything that uses it
    // (MaterialLibrary); reloads and edits change this object in place, so every mesh renderer and
    // preview that holds it shows the new look at once.
    class MaterialAsset
    {
    public:
        MaterialAsset() { Touch(); }
        explicit MaterialAsset(const MaterialData& data, std::filesystem::path path = {});

        // A material instance (IsInstance) of exactly this renderer material: `data` is what
        // GetData() returns (values and texture paths), what is drawn is `material` with the values
        // of `data` and the textures `material` already holds (nothing is loaded; textures that came
        // from elsewhere - a model file - have no path in `data`).
        static Ref<MaterialAsset> CreateInstance(const MaterialData& data, const Material& material, std::filesystem::path path = {});

        const MaterialData& GetData() const { return m_Data; }
        // New values (an edit): only the textures whose paths changed are looked up again.
        void SetData(const MaterialData& data);

        // ---- quick edits (scripts): SetData with one value changed.
        //   GetMaterial()->SetColor({ 1.0f, 0.2f, 0.2f, 1.0f });
        const glm::vec4& GetColor() const { return m_Data.Albedo; }
        void SetColor(const glm::vec4& color);
        void SetColor(const glm::vec3& color);      // the opacity stays
        void SetOpacity(float alpha);               // the albedo color's alpha (Transparent / Cutout materials)
        void SetEmission(const glm::vec3& color, float strength);
        void SetMetallic(float metallic);
        void SetRoughness(float roughness);
        void SetTiling(const glm::vec2& tiling);
        void SetOffset(const glm::vec2& offset);
        // Texture paths relative to the asset folder ("" = none).
        void SetAlbedoMap(const std::string& assetPath);
        void SetEmissiveMap(const std::string& assetPath);
        // What the renderer draws with.
        const Material& GetMaterial() const { return m_Material; }

        // The .zmat file (absolute; empty = made in memory).
        const std::filesystem::path& GetPath() const { return m_Path; }
        void SetPath(const std::filesystem::path& path) { m_Path = path; }
        // Changes with every change of the data (SetData, Reload): editors and previews notice
        // edits made elsewhere. Unique across all material objects.
        uint64_t GetRevision() const { return m_Revision; }
        // A runtime copy (Clone, CreateInstance) - a script's per-entity material: it is never
        // cached, never read from or written to its file (Reload / Save refuse); GetPath() still
        // names the asset it was copied from.
        bool IsInstance() const { return m_Instance; }

        // Reads the file again into this object. False - the old content stays - when it cannot be
        // read (or this is an instance); the reason goes to `error`, or to the log without one.
        bool Reload(std::string* error = nullptr);
        // Writes the data to its file. False without a path, for an instance, or when the file
        // cannot be written.
        bool Save() const;
        // Writes to another file, which becomes this material's file (an instance saved this way
        // is a file's material from then on - not the cached one, see MaterialLibrary::Load).
        bool SaveAs(const std::filesystem::path& path);
        // Looks every texture up again (a texture file appeared, or one that failed was fixed).
        void ResolveTextures();
        // An independent copy with the same data and textures (not cached, same path): a material
        // instance (IsInstance) whose edits leave the shared asset alone.
        Ref<MaterialAsset> Clone() const;

    private:
        void Touch();

    private:
        MaterialData m_Data;
        Material m_Material;
        std::filesystem::path m_Path;
        uint64_t m_Revision = 0;
        bool m_Instance = false;
    };

    // Caches material assets by file, so all mesh renderers using one .zmat share one object.
    class MaterialLibrary
    {
    public:
        // The material of a .zmat file (absolute, or relative to the asset folder). nullptr when the
        // file cannot be read: the reason is reported once (AssetImportLog) and the file is not read
        // again until it changes on disk.
        static Ref<MaterialAsset> Load(const std::filesystem::path& path);
        // The cached material of a file (nothing is read).
        static Ref<MaterialAsset> Find(const std::filesystem::path& path);
        static bool IsFileLoaded(const std::filesystem::path& path);
        // A .zmat changed on disk: its cached material is read again in place (a file that could not
        // be loaded before is forgotten, so the next Load tries again). A texture changed or appeared:
        // the cached materials using it look it up again. Returns how many materials changed (a
        // .zmat written with what the material already holds - the editor saved it - counts 0);
        // `failed` is set when the .zmat could not be read (the material keeps its old content).
        static size_t ReloadFile(const std::filesystem::path& path, bool* failed = nullptr);
        // A .zmat (or a folder: every material inside it) was renamed / moved: the cached materials
        // follow (same objects, new path), so scenes resolving the new path get them.
        static void OnFileMoved(const std::filesystem::path& from, const std::filesystem::path& to);
        // Whether a cached material uses this texture file.
        static bool UsesTexture(const std::filesystem::path& texture);
        static std::vector<Ref<MaterialAsset>> GetLoaded();

        // What the cached materials hold at one moment. The editor takes one when play starts:
        // scripts may edit shared materials (ScriptableEntity::GetSharedMaterial) and those edits
        // are taken back when play stops, like every other change made while playing.
        struct Snapshot
        {
            struct Entry
            {
                Ref<MaterialAsset> Asset;
                MaterialData Data;
                uint64_t Revision = 0;
            };
            std::vector<Entry> Materials;
        };
        static Snapshot TakeSnapshot();
        // Every material of the snapshot that changed since gets the data it had back - except one
        // that now holds exactly what its file holds (saved, or read again from a changed file, in
        // the meantime); a material loaded after the snapshot that no longer holds what its file
        // holds is set to the file's content. Returns the paths (asset-relative) of the restored ones.
        static std::vector<std::string> RestoreSnapshot(const Snapshot& snapshot);
        // Drops the cached material of a file when nothing else holds it. True when it was dropped.
        static bool ReleaseIfUnused(const std::filesystem::path& path);
        static void Clear();

        // How material texture paths become textures. The default loads them with TextureLibrary
        // from the asset folder; tests and tools without a GPU set their own (null = the default).
        using TextureLoader = std::function<Ref<Texture2D>(const std::string& assetPath)>;
        static void SetTextureLoader(TextureLoader loader);
        static Ref<Texture2D> LoadTexture(const std::string& assetPath);

        // The cache key of a file: absolute, normalized, '/' separators.
        static std::string MakeKey(const std::filesystem::path& path);
    };

}
