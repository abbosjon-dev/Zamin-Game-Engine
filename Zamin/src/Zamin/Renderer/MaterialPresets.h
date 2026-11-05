#pragma once

#include "Zamin/Renderer/MaterialAsset.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // A picture made by a preset (RGBA8, top row first), written as a PNG next to the material.
    struct PresetTexture
    {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Pixels;

        bool IsValid() const { return Width > 0 && Height > 0 && Pixels.size() == (size_t)Width * Height * 4; }
    };

    // One of the engine's standard materials: values that look right for a kind of surface, and
    // for some of them textures generated on the CPU (wood grain, concrete, brushed metal).
    struct MaterialPreset
    {
        enum class Pattern : uint8_t { None = 0, Wood, Concrete, BrushedMetal };

        std::string Id;            // stable name ("wood"), for code and automation
        std::string Name;          // shown in the editor ("Yog'och")
        std::string FileName;      // file name stem of the material it makes ("Wood")
        std::string Description;   // tooltip
        MaterialData Data;         // texture paths empty: filled in from the generated textures
        Pattern Texture = Pattern::None;
    };

    // The built-in material library: metal, gold, copper, plastic, rubber, wood, glass, ceramic,
    // concrete, neon... The editor makes .zmat files of them ("Yangi material" menu of the content
    // browser, the mesh renderer's material field); everything here works without a GPU.
    class MaterialPresets
    {
    public:
        static const std::vector<MaterialPreset>& GetAll();
        static const MaterialPreset* Find(std::string_view id);

        // The preset's generated texture (deterministic: the same pixels every time; tiles without
        // seams). Not valid for presets without a pattern. `size`: width = height (at least 16).
        static PresetTexture GenerateTexture(MaterialPreset::Pattern pattern, uint32_t size = 256);
        // Which texture of the material a pattern is, and the file name it is written as.
        static const char* GetTextureSuffix(MaterialPreset::Pattern pattern); // "Albedo", "MetalRough"

        struct CreateResult
        {
            std::filesystem::path Material;               // the .zmat
            std::vector<std::filesystem::path> Written;   // files written now (the texture, the .zmat)
            bool Existed = false;                         // shared: the file was there already (nothing written)
        };
        // The active project's folder for the shared copies of the presets (Assets/Materials/Standart;
        // empty without a project). Generated textures go to its Textures folder.
        static std::filesystem::path GetSharedFolder();
        // Where a preset's generated texture is used from for a material written into `folder`:
        // folder/Textures/<FileName>_<suffix>.png when that file is there already (or without a
        // project), else the project's one copy in GetSharedFolder()/Textures - so the copies of a
        // preset in different folders share one texture file.
        static std::filesystem::path GetTexturePath(const MaterialPreset& preset, const std::filesystem::path& folder);
        // Writes the preset as a .zmat into `folder`: under a free name ("Wood.zmat", "Wood_1.zmat"...),
        // or with `shared` exactly "<FileName>.zmat" - an existing one is used as it is (one file a
        // project shares). A generated texture goes to GetTexturePath(); an existing one of that name
        // is used as it is. Paths in the .zmat are relative to the asset folder of the active project
        // (absolute without one); new files get their UUID at once.
        static bool Create(const MaterialPreset& preset, const std::filesystem::path& folder, bool shared, CreateResult& result,
                           std::string* error = nullptr);
    };

}
