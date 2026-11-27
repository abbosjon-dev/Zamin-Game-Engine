#include "Zamin/Project/ExportCook.h"

#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Asset/SpriteAtlas.h"
#include "Zamin/Renderer/Quality.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Utils/Image.h"
#include "Zamin/Renderer/Texture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <thread>

namespace Zamin {

    namespace fs = std::filesystem;

    uint32_t ExportCook::QualitySkips(const QualitySettings& quality)
    {
        if (quality.Levels.empty()) return 0;
        int skips = QualityLevel::MaxTextureQuality;
        for (const QualityLevel& level : quality.Levels) skips = std::min(skips, std::max(0, level.TextureQuality));
        return uint32_t(skips);
    }

    bool ExportCook::IsCookable(const std::string& path) { return FileSystem::HasExtension(path, { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }); }

    ExportCookResult ExportCook::Run(const ExportCookInputs& in)
    {
        const auto started = std::chrono::steady_clock::now();
        ExportCookResult result;
        std::error_code ec;
        // 1. Sprite atlases: a page whose sources changed since it was built is packed again into the export.
        nlohmann::ordered_json atlases = nlohmann::ordered_json::array();
        for (const std::string& file : in.Files)
        {
            if (!FileSystem::HasExtension(file, SpriteAtlasDefinition::Extension)) continue;
            SpriteAtlasDefinition atlas;
            std::string error;
            if (!SpriteAtlasDefinition::Load(in.SourceAssets / file, atlas, &error))
            {
                result.Warnings.push_back(std::format("atlas {}: {}", file, error));
                continue;
            }
            if (std::find(in.Files.begin(), in.Files.end(), atlas.Output) == in.Files.end()) continue; // its page is not part of the game
            const fs::path page = FileSystem::SafeJoin(in.ExportAssets, atlas.Output);
            if (page.empty()) continue;
            if (SpriteAtlas::HashSources(in.SourceAssets, atlas) != atlas.SourceHash)
            {
                // The export's copy of the page (and its .zmeta) is replaced; the project stays as it is.
                SpriteAtlasBuildResult built;
                if (SpriteAtlas::WritePage(in.SourceAssets, atlas, page, file, built))
                    result.Atlases++;
                else
                    result.Warnings.push_back(std::format("atlas {} could not be re-packed: {}", file, built.Error));
            }
            nlohmann::ordered_json sprites = nlohmann::ordered_json::array();
            uint32_t width = 0, height = 0;
            if (auto meta = AssetMetaFile::Read(page))
            {
                const SpriteSheet sheet = SpriteSheet::FromImportSettings(meta->Import);
                width = atlas.PageWidth;
                height = atlas.PageHeight;
                ImageData probe;
                if (Image::Load(page.string(), probe, 4)) width = probe.Width, height = probe.Height;
                for (const SpriteFrame& f : sheet.Frames)
                {
                    glm::vec2 uvMin, uvMax;
                    f.GetUV(width, height, uvMin, uvMax);
                    sprites.push_back({ { "Name", f.Name }, { "Rect", { f.Position.x, f.Position.y, f.Size.x, f.Size.y } },
                                        { "UV", { uvMin.x, uvMin.y, uvMax.x, uvMax.y } }, { "Pivot", { f.Pivot.x, f.Pivot.y } } });
                }
            }
            atlases.push_back({ { "Atlas", file }, { "Page", atlas.Output }, { "Size", { width, height } }, { "Sprites", sprites } });
        }
        result.AtlasTable = atlases.dump();

        // 2. Textures, in parallel: each cooked from the cache or decoded, resized, compressed; written in place.
        std::vector<std::string> textures;
        for (const std::string& file : in.Files)
            if (IsCookable(file)) textures.push_back(file);
        std::sort(textures.begin(), textures.end());
        result.Assets.resize(textures.size());
        std::atomic<size_t> next{ 0 };
        std::mutex mutex;
        auto worker = [&] {
            for (size_t i; (i = next.fetch_add(1)) < textures.size();)
            {
                const std::string& file = textures[i];
                CookedAsset& asset = result.Assets[i];
                asset.Path = file;
                const fs::path path = in.ExportAssets / file;
                std::error_code e;
                asset.SourceBytes = fs::file_size(path, e);
                if (TextureCook::IsCookedFile(path))
                {
                    asset.Action = "kept";
                    asset.Note = "already cooked";
                    continue;
                }
                // Halving is safe only for textures sampled by UV alone: mip-mapped, linear, without sprite frames
                // (frames and 9-slice borders are pixel rectangles).
                bool resizable = false;
                if (auto meta = AssetMetaFile::Read(path))
                {
                    const TextureImportSettings texture = AssetImporter::ReadTextureSettings(meta->Import);
                    resizable = texture.Mipmaps && texture.Filter == TextureFilter::Linear && SpriteSheet::FromImportSettings(meta->Import).IsEmpty();
                }
                TextureCook::Result cooked = TextureCook::CookFile(path, in.Settings, resizable, in.CacheDirectory);
                if (!cooked.Ok)
                {
                    asset.Action = "failed";
                    asset.Note = cooked.Error;
                    continue;
                }
                asset.CacheHit = cooked.CacheHit;
                asset.Width = cooked.Info.Width;
                asset.Height = cooked.Info.Height;
                asset.SourceWidth = cooked.Info.SourceWidth;
                asset.SourceHeight = cooked.Info.SourceHeight;
                const bool smaller = cooked.Bytes.size() < asset.SourceBytes;
                if (smaller || cooked.Resized || in.Settings.Always)
                {
                    if (!FileSystem::WriteBinaryFile(path, cooked.Bytes.data(), cooked.Bytes.size()))
                    {
                        asset.Action = "failed";
                        asset.Note = "could not be written";
                        continue;
                    }
                    asset.Action = "cooked";
                    asset.CookedBytes = cooked.Bytes.size();
                    if (cooked.Resized) asset.Note = cooked.Info.BakedSkips ? std::format("{} quality mip level(s) baked out", cooked.Info.BakedSkips) : "halved to the size limit";
                }
                else
                {
                    asset.Action = "kept";
                    asset.CookedBytes = asset.SourceBytes;
                    asset.Width = asset.SourceWidth;
                    asset.Height = asset.SourceHeight;
                    asset.Note = std::format("the source is smaller ({} cooked)", cooked.Bytes.size());
                }
            }
        };
        const uint32_t threads = std::clamp<uint32_t>(in.Threads ? in.Threads : std::min(4u, std::max(1u, std::thread::hardware_concurrency())), 1u, 16u);
        std::vector<std::thread> pool;
        for (uint32_t t = 1; t < threads && t < textures.size(); t++) pool.emplace_back(worker);
        worker();
        for (std::thread& t : pool) t.join();
        for (const CookedAsset& a : result.Assets)
        {
            if (a.CacheHit) result.CacheHits++;
            if (a.Action == "failed") result.Warnings.push_back(std::format("texture {} was not cooked: {}", a.Path, a.Note));
        }
        result.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        FileSystem::WriteTextFile(in.ExportAssets.parent_path() / TextureCook::ManifestFileName,
                                  TextureCook::ManifestToJson(in.Settings, result.Assets, result.AtlasTable));
        return result;
    }

}
