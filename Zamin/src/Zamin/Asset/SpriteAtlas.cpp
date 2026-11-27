#include "Zamin/Asset/SpriteAtlas.h"

#include "Zamin/Asset/AssetImporter.h"
#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Utils/Image.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <set>

namespace Zamin {

    namespace fs = std::filesystem;
    using json = nlohmann::ordered_json;

    std::string SpriteAtlasDefinition::ToJson() const
    {
        json j;
        j["Version"] = CurrentVersion;
        j["Sources"] = Sources;
        j["Output"] = Output;
        j["Recursive"] = Recursive;
        j["PixelArt"] = PixelArt;
        j["Pivot"] = { Pivot.x, Pivot.y };
        if (Grid)
        {
            j["Grid"] = true;
            j["Columns"] = Columns;
        }
        j["MaxPageSize"] = Packing.MaxPageSize;
        j["Padding"] = Packing.Padding;
        j["MipLevels"] = Packing.MipLevels;
        j["SourceHash"] = std::format("{:016x}", SourceHash);
        j["Sprites"] = SpriteCount;
        j["Page"] = { PageWidth, PageHeight };
        return j.dump(4) + "\n";
    }

    bool SpriteAtlasDefinition::FromJson(const std::string& text, SpriteAtlasDefinition& out, std::string* error)
    {
        try
        {
            const json j = json::parse(text);
            if (!j.is_object()) throw std::runtime_error("not an object");
            SpriteAtlasDefinition a;
            if (j.value("Version", 1) > CurrentVersion) throw std::runtime_error("made by a newer engine (version " + std::to_string(j.value("Version", 1)) + ")");
            a.Sources = j.value("Sources", std::string());
            a.Output = j.value("Output", std::string());
            if (a.Sources.empty() || a.Output.empty()) throw std::runtime_error("Sources and Output are required");
            if (!FileSystem::IsSafeRelativePath(a.Sources) || !FileSystem::IsSafeRelativePath(a.Output)) throw std::runtime_error("unsafe path");
            a.Recursive = j.value("Recursive", false);
            a.PixelArt = j.value("PixelArt", false);
            a.Grid = j.value("Grid", false);
            a.Columns = std::clamp(j.value("Columns", 8u), 1u, 1024u);
            if (j.contains("Pivot") && j["Pivot"].is_array() && j["Pivot"].size() == 2)
                a.Pivot = glm::clamp(glm::vec2(j["Pivot"][0].get<float>(), j["Pivot"][1].get<float>()), glm::vec2(0.0f), glm::vec2(1.0f));
            a.Packing.MaxPageSize = std::clamp(j.value("MaxPageSize", 2048u), 16u, 8192u);
            a.Packing.Padding = std::min(j.value("Padding", 2u), 64u);
            a.Packing.MipLevels = std::min(j.value("MipLevels", 0u), 6u);
            a.SourceHash = std::stoull(j.value("SourceHash", std::string("0")), nullptr, 16);
            a.SpriteCount = j.value("Sprites", 0u);
            if (j.contains("Page") && j["Page"].is_array() && j["Page"].size() == 2)
            {
                a.PageWidth = j["Page"][0].get<uint32_t>();
                a.PageHeight = j["Page"][1].get<uint32_t>();
            }
            out = std::move(a);
            return true;
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return false;
        }
    }

    bool SpriteAtlasDefinition::Load(const fs::path& file, SpriteAtlasDefinition& out, std::string* error)
    {
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
        {
            if (error) *error = "file not found";
            return false;
        }
        return FromJson(FileSystem::ReadTextFile(file), out, error);
    }

    bool SpriteAtlasDefinition::Save(const fs::path& file) const { return FileSystem::WriteTextFileAtomic(file, ToJson()); }

    std::vector<std::string> SpriteAtlas::ListSources(const fs::path& assetDirectory, const SpriteAtlasDefinition& atlas)
    {
        std::vector<std::string> files;
        const fs::path folder = FileSystem::SafeJoin(assetDirectory, atlas.Sources);
        std::error_code ec;
        if (folder.empty() || !fs::is_directory(folder, ec)) return files;
        auto take = [&](const fs::path& path) {
            if (!FileSystem::HasExtension(path, { ".png", ".jpg", ".jpeg", ".tga", ".bmp" })) return;
            const std::string relative = FileSystem::RelativePath(path, assetDirectory);
            if (relative != atlas.Output) files.push_back(relative);
        };
        if (atlas.Recursive)
        {
            for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec)) take(it->path());
        }
        else
            for (const auto& entry : fs::directory_iterator(folder, ec))
                if (entry.is_regular_file(ec)) take(entry.path());
        std::sort(files.begin(), files.end());
        return files;
    }

    uint64_t SpriteAtlas::HashSources(const fs::path& assetDirectory, const SpriteAtlasDefinition& atlas, std::string* error)
    {
        SpriteAtlasDefinition settings = atlas;
        settings.SourceHash = 0;
        settings.SpriteCount = settings.PageWidth = settings.PageHeight = 0;
        const std::string text = settings.ToJson();
        uint64_t hash = HashBytes(text.data(), text.size());
        for (const std::string& file : ListSources(assetDirectory, atlas))
        {
            const fs::path path = assetDirectory / file;
            const std::vector<uint8_t> bytes = FileSystem::ReadBinaryFile(path);
            if (bytes.empty() && error) *error = "unreadable: " + file;
            hash = HashBytes(file.data(), file.size(), hash);
            hash = HashBytes(bytes.data(), bytes.size(), hash);
            if (auto meta = AssetMetaFile::Read(path))
            {
                const std::string frames = SpriteSheet::FromImportSettings(meta->Import).ToJson();
                hash = HashBytes(frames.data(), frames.size(), hash);
            }
        }
        return hash;
    }

    bool SpriteAtlas::Pack(const fs::path& assetDirectory, const SpriteAtlasDefinition& atlas, AtlasResult& result, std::vector<std::string>& names,
                           std::vector<glm::vec2>& pivots, std::vector<glm::vec4>& borders, std::string& error)
    {
        std::vector<AtlasImage> images;
        names.clear();
        pivots.clear();
        borders.clear();
        SpriteSheet unique; // names taken so far
        const std::string folder = atlas.Sources.empty() || atlas.Sources.back() == '/' ? atlas.Sources : atlas.Sources + "/";
        for (const std::string& file : ListSources(assetDirectory, atlas))
        {
            ImageData image;
            std::string reason;
            if (!Image::Load((assetDirectory / file).string(), image, 4, false, &reason))
            {
                error = file + ": " + reason;
                return false;
            }
            std::string stem = file.starts_with(folder) ? file.substr(folder.size()) : file;
            stem = fs::path(stem).replace_extension().generic_string();
            SpriteSheet sheet;
            if (auto meta = AssetMetaFile::Read(assetDirectory / file)) sheet = SpriteSheet::FromImportSettings(meta->Import);
            auto add = [&](std::string name, glm::ivec2 position, glm::ivec2 size, glm::vec2 pivot, glm::vec4 border) {
                position = glm::clamp(position, glm::ivec2(0), glm::ivec2(int(image.Width) - 1, int(image.Height) - 1));
                size = glm::clamp(size, glm::ivec2(1), glm::ivec2(int(image.Width), int(image.Height)) - position);
                AtlasImage cut;
                cut.Name = unique.FindIndex(name) < 0 ? name : unique.MakeUniqueName(stem + "_" + name);
                cut.Width = uint32_t(size.x);
                cut.Height = uint32_t(size.y);
                cut.Pixels.resize(size_t(cut.Width) * cut.Height * 4);
                for (uint32_t y = 0; y < cut.Height; y++)
                    std::copy_n(image.Pixels.data() + ((size_t(position.y) + y) * image.Width + size_t(position.x)) * 4, size_t(cut.Width) * 4,
                                cut.Pixels.data() + size_t(y) * cut.Width * 4);
                SpriteFrame marker;
                marker.Name = cut.Name;
                unique.Frames.push_back(marker);
                names.push_back(cut.Name);
                pivots.push_back(pivot);
                borders.push_back(border);
                images.push_back(std::move(cut));
            };
            if (sheet.IsEmpty())
                add(stem, { 0, 0 }, { int(image.Width), int(image.Height) }, atlas.Pivot, glm::vec4(0));
            else
                for (const SpriteFrame& f : sheet.Frames) add(f.Name, f.Position, f.Size, f.Pivot, f.Border);
        }
        if (images.empty())
        {
            error = "no images in " + atlas.Sources;
            return false;
        }
        if (atlas.Grid)
        {
            // Name order, row by row: the tile numbers of a tilemap stay put when a tile is edited.
            std::vector<size_t> order(images.size());
            for (size_t i = 0; i < order.size(); i++) order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return images[a].Name < images[b].Name; });
            const uint32_t w = images.front().Width, h = images.front().Height;
            for (const AtlasImage& image : images)
                if (image.Width != w || image.Height != h)
                {
                    error = "a grid atlas needs sprites of one size ('" + image.Name + "' is " + std::to_string(image.Width) + "x" + std::to_string(image.Height) +
                            ", the first " + std::to_string(w) + "x" + std::to_string(h) + ")";
                    return false;
                }
            const uint32_t columns = std::min<uint32_t>(atlas.Columns, uint32_t(images.size())), rows = uint32_t((images.size() + columns - 1) / columns);
            if (columns * w > atlas.Packing.MaxPageSize || rows * h > atlas.Packing.MaxPageSize)
            {
                error = "the grid does not fit the page size";
                return false;
            }
            result = {};
            AtlasPage page;
            page.Width = columns * w;
            page.Height = rows * h;
            page.Pixels.assign(size_t(page.Width) * page.Height * 4, 0);
            result.Placements.resize(images.size());
            for (size_t slot = 0; slot < order.size(); slot++)
            {
                const AtlasImage& image = images[order[slot]];
                AtlasPlacement& p = result.Placements[order[slot]];
                p.Name = image.Name;
                p.X = uint32_t(slot % columns) * w;
                p.Y = uint32_t(slot / columns) * h;
                p.Width = w;
                p.Height = h;
                p.UVMin = { float(p.X) / float(page.Width), 1.0f - float(p.Y + h) / float(page.Height) };
                p.UVMax = { float(p.X + w) / float(page.Width), 1.0f - float(p.Y) / float(page.Height) };
                for (uint32_t y = 0; y < h; y++)
                    std::copy_n(image.Pixels.data() + size_t(y) * w * 4, size_t(w) * 4, page.Pixels.data() + (size_t(p.Y + y) * page.Width + p.X) * 4);
                result.ImagePixels += uint64_t(w) * h;
            }
            result.Pages.push_back(std::move(page));
            return true;
        }
        AtlasSettings packing = atlas.Packing;
        packing.MaxPages = 1;
        if (!AtlasPacker::Pack(images, packing, result))
        {
            error = result.Error + " (raise the page size or split the folder)";
            return false;
        }
        return true;
    }

    bool SpriteAtlas::WritePage(const fs::path& assetDirectory, const SpriteAtlasDefinition& atlas, const fs::path& output, const std::string& atlasPath,
                                SpriteAtlasBuildResult& r, AtlasResult* packedOut)
    {
        std::error_code ec;
        AtlasResult packed;
        std::vector<std::string> names;
        std::vector<glm::vec2> pivots;
        std::vector<glm::vec4> borders;
        if (!Pack(assetDirectory, atlas, packed, names, pivots, borders, r.Error)) return false;
        const AtlasPage& page = packed.Pages.front();
        fs::create_directories(output.parent_path(), ec);
        if (!Image::WritePNG(output.string(), page.Width, page.Height, 4, page.Pixels.data()))
        {
            r.Error = "the page could not be written: " + atlas.Output;
            return false;
        }
        // The page's .zmeta: its UUID stays, frames of the same name keep their hand-edited pivot / borders.
        AssetMetadata meta;
        const bool hadMeta = [&] {
            if (auto existing = AssetMetaFile::Read(output))
            {
                meta = *existing;
                return true;
            }
            return false;
        }();
        if (!meta.Handle) meta.Handle = AssetHandle::Generate();
        meta.Type = AssetType::Texture;
        meta.RelativePath = atlas.Output;
        const SpriteSheet previous = SpriteSheet::FromImportSettings(meta.Import);
        SpriteSheet sheet;
        for (size_t i = 0; i < packed.Placements.size(); i++)
        {
            const AtlasPlacement& p = packed.Placements[i];
            SpriteFrame f;
            f.Name = p.Name;
            f.Position = { int(p.X), int(p.Y) };
            f.Size = { int(p.Width), int(p.Height) };
            f.Pivot = pivots[i];
            f.Border = borders[i];
            if (const SpriteFrame* old = previous.Find(f.Name))
            {
                f.Pivot = old->Pivot;
                f.Border = old->Border;
                r.Kept.push_back(f.Name);
            }
            else
                r.Added.push_back(f.Name);
            sheet.Frames.push_back(std::move(f));
        }
        if (atlas.Grid) // frame i is tile i
            std::stable_sort(sheet.Frames.begin(), sheet.Frames.end(), [](const SpriteFrame& a, const SpriteFrame& b) {
                return a.Position.y != b.Position.y ? a.Position.y < b.Position.y : a.Position.x < b.Position.x;
            });
        for (const SpriteFrame& f : previous.Frames)
            if (!sheet.Find(f.Name)) r.Removed.push_back(f.Name);
        TextureImportSettings texture = hadMeta ? AssetImporter::ReadTextureSettings(meta.Import) : TextureImportSettings{};
        if (!hadMeta || previous.IsEmpty())
        {
            texture.Filter = atlas.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear;
            texture.Wrap = TextureWrap::ClampToEdge;
            texture.Mipmaps = atlas.Packing.MipLevels > 0;
        }
        AssetImporter::WriteTextureSettings(texture, meta.Import);
        sheet.WriteTo(meta.Import);
        meta.Import.SetString("Atlas", atlasPath);
        if (!AssetMetaFile::Write(output, meta))
        {
            r.Error = "the page's .zmeta could not be written";
            return false;
        }
        r.Sprites = uint32_t(sheet.Frames.size());
        r.PageWidth = page.Width;
        r.PageHeight = page.Height;
        r.Occupancy = packed.Occupancy();
        if (packedOut) *packedOut = std::move(packed);
        return true;
    }

    SpriteAtlasBuildResult SpriteAtlas::Build(const fs::path& assetDirectory, const fs::path& atlasFile, bool force, AssetRegistry* registry)
    {
        SpriteAtlasBuildResult r;
        SpriteAtlasDefinition atlas;
        if (!SpriteAtlasDefinition::Load(atlasFile, atlas, &r.Error)) return r;
        const fs::path output = FileSystem::SafeJoin(assetDirectory, atlas.Output);
        if (output.empty() || !FileSystem::HasExtension(output, ".png"))
        {
            r.Error = "the page must be a .png inside the asset folder";
            return r;
        }
        r.SourceHash = HashSources(assetDirectory, atlas, &r.Error);
        if (!r.Error.empty()) return r;
        std::error_code ec;
        if (!force && r.SourceHash == atlas.SourceHash && fs::is_regular_file(output, ec))
        {
            r.Ok = true;
            r.Sprites = atlas.SpriteCount;
            r.PageWidth = atlas.PageWidth;
            r.PageHeight = atlas.PageHeight;
            return r;
        }
        AtlasResult packed;
        if (!WritePage(assetDirectory, atlas, output, FileSystem::RelativePath(atlasFile, assetDirectory), r, &packed)) return r;
        const AtlasPage& page = packed.Pages.front();
        atlas.SourceHash = r.SourceHash;
        atlas.SpriteCount = r.Sprites;
        atlas.PageWidth = page.Width;
        atlas.PageHeight = page.Height;
        if (!atlas.Save(atlasFile))
        {
            r.Error = "the atlas file could not be written";
            return r;
        }
        if (registry)
        {
            if (!registry->Find(fs::path(atlas.Output))) registry->OnFileAdded(output);
            else registry->OnFileModified(output);
            registry->OnMetaFileChanged(AssetMetaFile::GetMetaPath(output));
            registry->OnFileModified(atlasFile);
        }
        SpriteSheet::ClearCache();
        r.Ok = r.Changed = true;
        return r;
    }

    bool SpriteAtlas::IsStale(const fs::path& assetDirectory, const fs::path& atlasFile)
    {
        SpriteAtlasDefinition atlas;
        if (!SpriteAtlasDefinition::Load(atlasFile, atlas)) return false;
        std::error_code ec;
        const fs::path output = FileSystem::SafeJoin(assetDirectory, atlas.Output);
        return output.empty() || !fs::is_regular_file(output, ec) || HashSources(assetDirectory, atlas) != atlas.SourceHash;
    }

    std::string SpriteAtlas::CreateFromFolder(const fs::path& assetDirectory, const std::string& folder, std::string& error, AssetRegistry* registry)
    {
        std::string clean = folder;
        while (!clean.empty() && clean.back() == '/') clean.pop_back();
        std::error_code ec;
        if (clean.empty() || !FileSystem::IsSafeRelativePath(clean) || !fs::is_directory(assetDirectory / clean, ec))
        {
            error = "not a folder of the assets: " + folder;
            return {};
        }
        const fs::path base = fs::path(clean);
        std::string name = base.filename().string();
        const std::string parent = base.parent_path().generic_string();
        auto join = [&](const std::string& file) { return parent.empty() ? file : parent + "/" + file; };
        std::string atlasPath = join(name + SpriteAtlasDefinition::Extension);
        for (int n = 1; fs::exists(assetDirectory / atlasPath, ec); n++) atlasPath = join(std::format("{}_{}{}", name, n, SpriteAtlasDefinition::Extension));
        SpriteAtlasDefinition atlas;
        atlas.Sources = clean;
        atlas.Output = fs::path(atlasPath).replace_extension(".atlas.png").generic_string();
        SpriteAtlasDefinition probe = atlas;
        if (ListSources(assetDirectory, probe).empty())
        {
            error = "the folder has no images: " + clean;
            return {};
        }
        // A pixel-art folder (every source nearest-filtered) gets a pixel-art page.
        bool pixelArt = true;
        for (const std::string& file : ListSources(assetDirectory, probe))
        {
            const auto meta = AssetMetaFile::Read(assetDirectory / file);
            pixelArt &= meta && AssetImporter::ReadTextureSettings(meta->Import).Filter == TextureFilter::Nearest;
        }
        atlas.PixelArt = pixelArt;
        if (!atlas.Save(assetDirectory / atlasPath))
        {
            error = "could not write " + atlasPath;
            return {};
        }
        if (registry) registry->OnFileAdded(assetDirectory / atlasPath);
        SpriteAtlasBuildResult built = Build(assetDirectory, assetDirectory / atlasPath, true, registry);
        if (!built.Ok)
        {
            error = built.Error;
            return {};
        }
        return atlasPath;
    }

}
