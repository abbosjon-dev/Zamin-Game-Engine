#pragma once

#include "Zamin/Core/Base.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // How the friction / bounciness of two touching colliders are combined. When the two colliders
    // ask for different modes, the one further down the list wins (Maximum over Multiply over ...).
    // Default is Box2D's own rule: friction sqrt(a * b), restitution max(a, b).
    enum class PhysicsCombine : uint8_t { Default = 0, Average, Minimum, Multiply, Maximum };

    // The surface of a 2D collider: a physics material asset (.zphysmat) or the collider's own values.
    struct PhysicsMaterialData
    {
        float Friction = 0.5f;
        float Restitution = 0.0f;          // 0 = no bounce, 1 = bounces back as fast as it came
        float RestitutionThreshold = 0.5f; // slower impacts (units / s) do not bounce
        float Density = 1.0f;              // mass per area (kg / unit^2)
        PhysicsCombine FrictionCombine = PhysicsCombine::Default;
        PhysicsCombine RestitutionCombine = PhysicsCombine::Default;

        // No negative values, nothing that is not a number.
        void Sanitize();
        bool operator==(const PhysicsMaterialData&) const = default;
    };

    struct PhysicsMaterial
    {
        static constexpr const char* Extension = ".zphysmat";

        // The value two colliders touch with (see PhysicsCombine).
        static float Combine(float a, PhysicsCombine modeA, float b, PhysicsCombine modeB, bool friction);
        static float CombineFriction(float a, PhysicsCombine modeA, float b, PhysicsCombine modeB) { return Combine(a, modeA, b, modeB, true); }
        static float CombineRestitution(float a, PhysicsCombine modeA, float b, PhysicsCombine modeB) { return Combine(a, modeA, b, modeB, false); }

        static const char* GetCombineKey(PhysicsCombine mode);   // in files: "Default", "Average"...
        static const char* GetCombineName(PhysicsCombine mode);  // in the editor (Uzbek)
        static std::optional<PhysicsCombine> CombineFromKey(std::string_view key);

        // { "Friction", "Restitution", "RestitutionThreshold", "Density", "FrictionCombine", "RestitutionCombine" }
        static std::string ToJson(const PhysicsMaterialData& data);
        // Missing values keep the defaults; false (and `error` set) for text that is not such an object.
        static bool FromJson(std::string_view text, PhysicsMaterialData& out, std::string* error = nullptr);
        static bool Save(const std::filesystem::path& file, const PhysicsMaterialData& data);
        static bool Load(const std::filesystem::path& file, PhysicsMaterialData& out, std::string* error = nullptr);
    };

    // A loaded .zphysmat: shared by every collider that names it.
    class PhysicsMaterialAsset
    {
    public:
        PhysicsMaterialAsset(const PhysicsMaterialData& data, std::filesystem::path path) : m_Data(data), m_Path(std::move(path)) {}

        const PhysicsMaterialData& GetData() const { return m_Data; }
        // Changes what every collider using it gets (the running scenes take it on their next step).
        void SetData(const PhysicsMaterialData& data);
        const std::filesystem::path& GetPath() const { return m_Path; }
        void SetPath(std::filesystem::path path) { m_Path = std::move(path); }
        // Counts the changes of its data.
        uint64_t GetRevision() const { return m_Revision; }
        // Reads its file again (in place). False - the data stays - when it cannot be read.
        bool Reload(std::string* error = nullptr);

    private:
        PhysicsMaterialData m_Data;
        std::filesystem::path m_Path;
        uint64_t m_Revision = 0;
    };

    // The .zphysmat files in use, by absolute path (relative paths are in the asset folder).
    class PhysicsMaterialLibrary
    {
    public:
        // nullptr when the file cannot be read (reported once, see AssetImportLog).
        static Ref<PhysicsMaterialAsset> Load(const std::filesystem::path& path);
        static Ref<PhysicsMaterialAsset> Find(const std::filesystem::path& path);
        static bool IsFileLoaded(const std::filesystem::path& path);
        // The file changed on disk: its cached material is read again. Returns 1 when its data changed.
        static size_t ReloadFile(const std::filesystem::path& path, bool* failed = nullptr);
        // A .zphysmat (or a folder of them) was moved: the cached materials follow.
        static void OnFileMoved(const std::filesystem::path& from, const std::filesystem::path& to);
        // Changes whenever a cached material's data changes (reloaded, edited): running scenes then
        // give their colliders the new values.
        static uint64_t GetRevision();
        static void Clear();

        // What a collider touches with: the material asset `materialPath` names, else `inline`.
        static PhysicsMaterialData Resolve(const std::string& materialPath, const PhysicsMaterialData& inlineData);

    private:
        friend class PhysicsMaterialAsset;
        static void OnChanged();
        static std::string MakeKey(const std::filesystem::path& path);
    };

}
