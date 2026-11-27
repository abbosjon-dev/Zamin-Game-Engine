// Texture atlas packing (determinism, bleed, mip safety), sprite slicing by alpha, sprite atlas assets
// (reimport keeping references) and the export's texture cook (ZTEX round trip, the cook cache). CPU only.
#include "TestAssertions.h"
#include <Zamin/Asset/AssetImporter.h>
#include <Zamin/Asset/AssetMetadata.h>
#include <Zamin/Asset/SpriteAtlas.h>
#include <Zamin/Asset/TextureAtlas.h>
#include <Zamin/Asset/TextureCook.h>
#include <Zamin/Project/ExportCook.h>
#include <Zamin/Project/ExportAssets.h>
#include <Zamin/Renderer/Quality.h>
#include <Zamin/Renderer/SpriteSheet.h>
#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Utils/Image.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>

using namespace Zamin;

namespace {
    AtlasImage Solid(const std::string& name, uint32_t w, uint32_t h, glm::u8vec4 color, AtlasBleed bleed = AtlasBleed::Clamp)
    {
        AtlasImage image;
        image.Name = name;
        image.Width = w;
        image.Height = h;
        image.Bleed = bleed;
        for (uint32_t i = 0; i < w * h; i++) image.Pixels.insert(image.Pixels.end(), { color.r, color.g, color.b, color.a });
        return image;
    }
    std::vector<AtlasImage> RandomImages(uint32_t count, uint32_t seed)
    {
        std::mt19937 random(seed);
        std::vector<AtlasImage> images;
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t w = 4 + random() % 60, h = 4 + random() % 60;
            images.push_back(Solid("sprite_" + std::to_string(i), w, h, { uint8_t(random()), uint8_t(random()), uint8_t(random()), 255 }));
        }
        return images;
    }
    std::map<std::string, AtlasPlacement> ByName(const AtlasResult& r)
    {
        std::map<std::string, AtlasPlacement> out;
        for (const auto& p : r.Placements) out[p.Name] = p;
        return out;
    }
    bool WritePng(const std::filesystem::path& file, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba)
    {
        std::filesystem::create_directories(file.parent_path());
        return Image::WritePNG(file.string(), w, h, 4, rgba.data());
    }
    std::vector<uint8_t> Pattern(uint32_t w, uint32_t h, uint32_t seed, bool noisy)
    {
        std::mt19937 random(seed);
        std::vector<uint8_t> rgba(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
            {
                uint8_t* p = rgba.data() + (size_t(y) * w + x) * 4;
                p[0] = noisy ? uint8_t(random()) : uint8_t(x * 255 / std::max(1u, w - 1));
                p[1] = noisy ? uint8_t(random()) : uint8_t(y * 255 / std::max(1u, h - 1));
                p[2] = uint8_t(seed * 40);
                p[3] = 255;
            }
        return rgba;
    }
}

TEST(AtlasPackerDeterministicAndDisjoint)
{
    AtlasSettings settings;
    settings.MaxPageSize = 512;
    settings.Padding = 2;
    auto images = RandomImages(60, 7);
    AtlasResult a, b;
    CHECK(AtlasPacker::Pack(images, settings, a));
    auto shuffled = images;
    std::mt19937 random(99);
    std::shuffle(shuffled.begin(), shuffled.end(), random);
    CHECK(AtlasPacker::Pack(shuffled, settings, b));
    // The order the files were listed in does not matter: the same pages, every sprite at the same place.
    CHECK(a.Pages.size() == b.Pages.size());
    for (size_t i = 0; i < a.Pages.size(); i++) CHECK(a.Pages[i].Pixels == b.Pages[i].Pixels && a.Pages[i].Width == b.Pages[i].Width);
    CHECK(ByName(a) == ByName(b));
    AtlasResult again;
    CHECK(AtlasPacker::Pack(images, settings, again) && again.Hash() == a.Hash());
    // Padded cells never overlap and stay inside their page; UVs match the pixel rectangles.
    const uint32_t pad = settings.EffectivePadding();
    for (size_t i = 0; i < a.Placements.size(); i++)
    {
        const auto& p = a.Placements[i];
        const auto& page = a.Pages[p.Page];
        CHECK(p.X >= pad && p.Y >= pad && p.X + p.Width + pad <= page.Width && p.Y + p.Height + pad <= page.Height);
        CHECK_NEAR(p.UVMin.x, float(p.X) / page.Width, 1e-6f);
        CHECK_NEAR(p.UVMax.y, 1.0f - float(p.Y) / page.Height, 1e-6f);
        for (size_t j = i + 1; j < a.Placements.size(); j++)
        {
            const auto& q = a.Placements[j];
            if (q.Page != p.Page) continue;
            const bool apart = p.X + p.Width + pad <= q.X - pad || q.X + q.Width + pad <= p.X - pad || p.Y + p.Height + pad <= q.Y - pad ||
                               q.Y + q.Height + pad <= p.Y - pad;
            CHECK(apart);
        }
    }
    CHECK(a.Occupancy() > 0.4);
    std::printf("    Atlas: %zu sprites -> %zu page(s) %ux%u, occupancy %.1f%%\n", a.Placements.size(), a.Pages.size(), a.Pages[0].Width, a.Pages[0].Height,
                a.Occupancy() * 100.0);
}

TEST(AtlasPackerBleedAndMipSafety)
{
    // Solid sprites, mip-safe down to level 2: every texel of a sprite's footprint at levels 0..2 is its own
    // colour (alignment keeps neighbours out of the 2x2 boxes, the bleed keeps transparent black out).
    AtlasSettings settings;
    settings.MaxPageSize = 256;
    settings.Padding = 1;
    settings.MipLevels = 2;
    CHECK(settings.EffectivePadding() == 4);
    std::vector<AtlasImage> images;
    std::mt19937 random(3);
    for (int i = 0; i < 24; i++)
        images.push_back(Solid("s" + std::to_string(i), 5 + random() % 20, 5 + random() % 20, { uint8_t(20 + i * 9), uint8_t(200 - i * 7), uint8_t(i * 3), 255 }));
    AtlasResult r;
    CHECK(AtlasPacker::Pack(images, settings, r));
    CHECK(r.Pages.size() == 1);
    const auto& page = r.Pages[0];
    auto mips = AtlasPacker::BuildMips(page, 2);
    CHECK(mips.size() == 2);
    uint64_t checked = 0;
    for (size_t i = 0; i < images.size(); i++)
    {
        const auto& p = r.Placements[i];
        CHECK(p.X % 4 == 0 || (p.X - 4) % 4 == 0);
        const uint8_t* color = images[i].Pixels.data();
        for (uint32_t level = 0; level <= 2; level++)
        {
            const uint32_t w = std::max(1u, page.Width >> level), scale = 1u << level;
            const uint8_t* pixels = level ? mips[level - 1].data() : page.Pixels.data();
            // The sprite's rectangle grown by one texel of this level (what bilinear sampling at the edge reads).
            const uint32_t x0 = p.X / scale - 1, y0 = p.Y / scale - 1, x1 = (p.X + p.Width + scale - 1) / scale, y1 = (p.Y + p.Height + scale - 1) / scale;
            for (uint32_t y = y0; y <= y1; y++)
                for (uint32_t x = x0; x <= x1; x++)
                {
                    const uint8_t* t = pixels + (size_t(y) * w + x) * 4;
                    CHECK(t[0] == color[0] && t[1] == color[1] && t[2] == color[2] && t[3] == 255);
                    checked++;
                }
        }
    }
    CHECK(checked > 1000);
    // Wrap bleed (terrain layers): the padding repeats the opposite edge, so a repeating texture tiles seamlessly.
    AtlasImage tile;
    tile.Name = "grass";
    tile.Width = tile.Height = 8;
    tile.Bleed = AtlasBleed::Wrap;
    tile.Pixels = Pattern(8, 8, 5, false);
    AtlasSettings wrap;
    wrap.Padding = 3;
    AtlasResult w;
    CHECK(AtlasPacker::Pack(std::span<const AtlasImage>(&tile, 1), wrap, w));
    const auto& wp = w.Placements[0];
    const auto& wpage = w.Pages[0];
    auto at = [&](uint32_t x, uint32_t y) { return wpage.Pixels.data() + (size_t(y) * wpage.Width + x) * 4; };
    CHECK(std::equal(at(wp.X - 1, wp.Y), at(wp.X - 1, wp.Y) + 4, at(wp.X + 7, wp.Y)));       // left pad = right column
    CHECK(std::equal(at(wp.X, wp.Y + 8), at(wp.X, wp.Y + 8) + 4, at(wp.X, wp.Y)));           // bottom pad = top row
    // Clamp bleed: the edge pixel repeated.
    tile.Bleed = AtlasBleed::Clamp;
    CHECK(AtlasPacker::Pack(std::span<const AtlasImage>(&tile, 1), wrap, w));
    CHECK(std::equal(at(wp.X - 2, wp.Y), at(wp.X - 2, wp.Y) + 4, at(wp.X, wp.Y)));
}

TEST(AtlasPackerPagesAndLimits)
{
    AtlasSettings settings;
    settings.MaxPageSize = 64;
    settings.Padding = 1;
    std::vector<AtlasImage> images;
    for (int i = 0; i < 12; i++) images.push_back(Solid("t" + std::to_string(i), 30, 30, { 255, 0, 0, 255 }));
    AtlasResult r;
    CHECK(AtlasPacker::Pack(images, settings, r));
    CHECK(r.Pages.size() == 3); // four 32 px cells per 64 px page
    for (const auto& page : r.Pages) CHECK(page.Width <= 64 && page.Height <= 64);
    settings.MaxPages = 2;
    CHECK(!AtlasPacker::Pack(images, settings, r) && !r.Error.empty() && r.Pages.empty());
    auto big = Solid("huge", 80, 10, { 1, 2, 3, 255 });
    CHECK(!AtlasPacker::Pack(std::span<const AtlasImage>(&big, 1), settings, r) && r.Error.find("huge") != std::string::npos);
}

TEST(SpriteSliceByAlphaAndKeepReferences)
{
    const uint32_t w = 64, h = 32;
    std::vector<uint8_t> rgba(size_t(w) * h * 4, 0);
    auto fill = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) rgba[(size_t(y) * w + x) * 4 + 3] = 255;
    };
    fill(2, 3, 11, 12);   // a coin
    fill(13, 6, 13, 6);   // its glint, 1 px apart: merged
    fill(30, 4, 39, 20);  // a key (overlaps the coin's rows: same reading row)
    fill(50, 1, 50, 1);   // dust
    fill(5, 22, 20, 28);  // a second row
    SpriteSliceSettings s;
    s.Mode = SpriteSliceSettings::SliceMode::Alpha;
    s.NamePrefix = "item";
    s.MergeDistance = 1;
    s.MinSize = 2;
    auto frames = SpriteSheet::SliceGrid(s, w, h, rgba.data());
    CHECK(frames.size() == 3);
    CHECK(frames[0].Position == glm::ivec2(2, 3) && frames[0].Size == glm::ivec2(12, 10)); // coin + glint
    CHECK(frames[1].Position == glm::ivec2(30, 4) && frames[1].Size == glm::ivec2(10, 17));
    CHECK(frames[2].Position == glm::ivec2(5, 22));
    CHECK(frames[0].Name == "item_0" && frames[2].Name == "item_2");
    CHECK(SpriteSheet::SliceGrid(s, w, h, nullptr).empty());
    // Re-slicing keeps the names, pivots and borders of frames that cover the same pixels.
    SpriteSheet previous;
    previous.Frames = frames;
    previous.Frames[0].Name = "coin";
    previous.Frames[0].Pivot = { 0.5f, 0.0f };
    previous.Frames[1].Name = "key";
    previous.Frames[1].Border = { 1, 2, 3, 4 };
    fill(40, 4, 40, 20); // the key grew by a column
    auto again = SpriteSheet::SliceGrid(s, w, h, rgba.data());
    SpriteSheet::KeepReferences(previous, again);
    CHECK(again.size() == 3);
    CHECK(again[0].Name == "coin" && again[0].Pivot == glm::vec2(0.5f, 0.0f));
    CHECK(again[1].Name == "key" && again[1].Border == glm::vec4(1, 2, 3, 4) && again[1].Size.x == 11);
    CHECK(again[2].Name == "item_2");
}

TEST(SpriteAtlasAssetReimportKeepsReferences)
{
    TempDirectory temp("zamin-atlas");
    const auto assets = temp.GetPath();
    CHECK(WritePng(assets / "Sprites/Coins/coin_0.png", 16, 16, Pattern(16, 16, 1, false)));
    CHECK(WritePng(assets / "Sprites/Coins/coin_1.png", 16, 16, Pattern(16, 16, 2, false)));
    CHECK(WritePng(assets / "Sprites/Coins/gem.png", 12, 20, Pattern(12, 20, 3, false)));
    std::string error;
    const std::string atlasPath = SpriteAtlas::CreateFromFolder(assets, "Sprites/Coins", error);
    CHECK(atlasPath == "Sprites/Coins.zatlas");
    SpriteAtlasDefinition atlas;
    CHECK(SpriteAtlasDefinition::Load(assets / atlasPath, atlas));
    CHECK(atlas.Output == "Sprites/Coins.atlas.png" && atlas.SpriteCount == 3 && atlas.SourceHash != 0);
    auto meta = AssetMetaFile::Read(assets / atlas.Output);
    CHECK(meta.has_value());
    const AssetHandle handle = meta->Handle;
    SpriteSheet sheet = SpriteSheet::FromImportSettings(meta->Import);
    CHECK(sheet.Frames.size() == 3 && sheet.Find("coin_0") && sheet.Find("gem"));
    CHECK(AssetImporter::ReadTextureSettings(meta->Import).Wrap == TextureWrap::ClampToEdge);
    // The page holds the sprites' pixels at their frames.
    ImageData page;
    CHECK(Image::Load((assets / atlas.Output).string(), page, 4));
    const SpriteFrame* gem = sheet.Find("gem");
    const auto source = Pattern(12, 20, 3, false);
    for (int y = 0; y < 20; y++)
        CHECK(std::equal(source.begin() + y * 48, source.begin() + y * 48 + 48, page.Pixels.begin() + ((size_t(gem->Position.y) + y) * page.Width + gem->Position.x) * 4));
    // A pivot edited in the Sprite Editor (in the page's .zmeta).
    sheet.Frames[size_t(sheet.FindIndex("gem"))].Pivot = { 0.25f, 0.0f };
    sheet.WriteTo(meta->Import);
    CHECK(AssetMetaFile::Write(assets / atlas.Output, *meta));
    CHECK(!SpriteAtlas::IsStale(assets, assets / atlasPath));
    auto unchanged = SpriteAtlas::Build(assets, assets / atlasPath);
    CHECK(unchanged.Ok && !unchanged.Changed);
    // Reimport: one source repainted bigger, one removed, one added.
    CHECK(WritePng(assets / "Sprites/Coins/gem.png", 14, 22, Pattern(14, 22, 4, false)));
    std::filesystem::remove(assets / "Sprites/Coins/coin_1.png");
    CHECK(WritePng(assets / "Sprites/Coins/coin_2.png", 16, 16, Pattern(16, 16, 5, false)));
    CHECK(SpriteAtlas::IsStale(assets, assets / atlasPath));
    auto rebuilt = SpriteAtlas::Build(assets, assets / atlasPath);
    CHECK(rebuilt.Ok && rebuilt.Changed);
    CHECK(rebuilt.Removed == std::vector<std::string>{ "coin_1" });
    CHECK(rebuilt.Added == std::vector<std::string>{ "coin_2" });
    CHECK(rebuilt.Kept.size() == 2);
    meta = AssetMetaFile::Read(assets / atlas.Output);
    CHECK(meta && meta->Handle == handle); // the UUID that scenes, tilemaps and clips name stays
    sheet = SpriteSheet::FromImportSettings(meta->Import);
    CHECK(sheet.Find("gem") && sheet.Find("gem")->Size == glm::ivec2(14, 22) && sheet.Find("gem")->Pivot == glm::vec2(0.25f, 0.0f));
    CHECK(!SpriteAtlas::IsStale(assets, assets / atlasPath));
    // Grid layout (a tileset): name order, Columns per row, page = columns x rows cells.
    for (int i = 0; i < 6; i++) CHECK(WritePng(assets / ("Tiles/t" + std::to_string(i) + ".png"), 8, 8, Pattern(8, 8, uint32_t(10 + i), false)));
    const std::string tiles = SpriteAtlas::CreateFromFolder(assets, "Tiles", error);
    CHECK(!tiles.empty());
    CHECK(SpriteAtlasDefinition::Load(assets / tiles, atlas));
    atlas.Grid = true;
    atlas.Columns = 4;
    CHECK(atlas.Save(assets / tiles));
    auto grid = SpriteAtlas::Build(assets, assets / tiles);
    CHECK(grid.Ok && grid.PageWidth == 32 && grid.PageHeight == 16);
    sheet = SpriteSheet::FromImportSettings(AssetMetaFile::Read(assets / atlas.Output)->Import);
    CHECK(sheet.Frames.size() == 6 && sheet.Frames[5].Name == "t5" && sheet.Frames[5].Position == glm::ivec2(8, 8));
    CHECK(SpriteAtlas::CreateFromFolder(assets, "Missing", error).empty() && !error.empty());
}

TEST(TextureCookRoundTripAndDamage)
{
    for (uint32_t channels : { 1u, 3u, 4u })
        for (bool compress : { false, true })
        {
            const uint32_t w = 37, h = 19;
            std::vector<uint8_t> pixels(size_t(w) * h * channels);
            std::mt19937 random(channels * 10 + compress);
            for (size_t i = 0; i < pixels.size(); i++) pixels[i] = uint8_t(i % 7 == 0 ? random() : i / 13);
            CookedTextureInfo info;
            info.SourceHash = 42;
            auto bytes = TextureCook::Encode(pixels.data(), w, h, channels, compress, info);
            CHECK(TextureCook::IsCooked(bytes.data(), bytes.size()));
            std::vector<uint8_t> decoded;
            CookedTextureInfo read;
            CHECK(TextureCook::Decode(bytes.data(), bytes.size(), decoded, read));
            CHECK(decoded == pixels && read.Width == w && read.Height == h && read.Channels == channels && read.SourceHash == 42);
            CHECK(read.Compressed == compress);
            auto truncated = bytes;
            truncated.resize(bytes.size() - 5);
            std::string error;
            CHECK(!TextureCook::Decode(truncated.data(), truncated.size(), decoded, read, &error) && !error.empty());
        }
    std::vector<uint8_t> flat(64 * 64 * 4, 200);
    auto damaged = TextureCook::Encode(flat.data(), 64, 64, 4, true, {});
    damaged[60] ^= 0xff;
    std::vector<uint8_t> out;
    CookedTextureInfo info;
    CHECK(!TextureCook::Decode(damaged.data(), damaged.size(), out, info));
    CHECK(!TextureCook::IsCooked("ZTE", 3));
}

TEST(TextureCookCacheInvalidation)
{
    TempDirectory temp("zamin-cook");
    const auto source = temp.GetPath() / "rock.png";
    const auto cache = temp.GetPath() / "Cache";
    CHECK(WritePng(source, 64, 32, Pattern(64, 32, 1, false)));
    TextureCookSettings settings;
    auto first = TextureCook::CookFile(source, settings, true, cache);
    CHECK(first.Ok && !first.CacheHit && !first.Resized && first.Info.Width == 64);
    auto second = TextureCook::CookFile(source, settings, true, cache);
    CHECK(second.Ok && second.CacheHit && second.Bytes == first.Bytes);
    // The source changed: its content hash, so a new key.
    CHECK(WritePng(source, 64, 32, Pattern(64, 32, 2, false)));
    auto changed = TextureCook::CookFile(source, settings, true, cache);
    CHECK(changed.Ok && !changed.CacheHit && changed.Bytes != first.Bytes);
    // A setting that changes the output (the size limit) is part of the key; for a frameless, resizable texture
    // it halves the texture.
    settings.MaxTextureSize = 32;
    auto halved = TextureCook::CookFile(source, settings, true, cache);
    CHECK(halved.Ok && !halved.CacheHit && halved.Resized && halved.Info.Width == 32 && halved.Info.Height == 16 && halved.Info.BakedSkips == 0);
    auto notResizable = TextureCook::CookFile(source, settings, false, cache);
    CHECK(notResizable.Ok && !notResizable.CacheHit && notResizable.Info.Width == 64);
    // Quality levels that all skip a mip: baked out and marked (the runtime samples it as if present).
    settings.MaxTextureSize = 0;
    settings.QualitySkips = 1;
    auto baked = TextureCook::CookFile(source, settings, true, cache);
    CHECK(baked.Ok && baked.Info.BakedSkips == 1 && baked.Info.Width == 32 && baked.Info.SourceWidth == 64);
    // A damaged cache entry is a miss and is written again.
    size_t entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(cache))
    {
        entries++;
        FileSystem::WriteTextFile(entry.path(), "ZTEX broken");
    }
    CHECK(entries == 5);
    auto repaired = TextureCook::CookFile(source, settings, true, cache);
    CHECK(repaired.Ok && !repaired.CacheHit && repaired.Bytes == baked.Bytes);
    CHECK(TextureCook::CookFile(source, settings, true, cache).CacheHit);
    // Quality levels -> skips.
    QualitySettings quality;
    CHECK(ExportCook::QualitySkips(quality) == 0);
    for (auto& level : quality.Levels) level.TextureQuality = std::max(1, level.TextureQuality);
    CHECK(ExportCook::QualitySkips(quality) == 1);
    // The cooked file reads back through Image::Load like the PNG it replaces.
    const auto cooked = temp.GetPath() / "cooked.png";
    auto plain = TextureCook::CookFile(source, TextureCookSettings(), true, {});
    CHECK(FileSystem::WriteBinaryFile(cooked, plain.Bytes.data(), plain.Bytes.size()));
    ImageData a, b;
    CHECK(Image::Load(source.string(), a, 4) && Image::Load(cooked.string(), b, 4));
    CHECK(a.Pixels == b.Pixels && b.Width == 64);
    CHECK(Image::Load(source.string(), a, 3, true) && Image::Load(cooked.string(), b, 3, true) && a.Pixels == b.Pixels);
}

TEST(ExportCookRunReportsAndRepacks)
{
    TempDirectory temp("zamin-export-cook");
    const auto project = temp.GetPath() / "Assets", exported = temp.GetPath() / "Export/Game/Assets";
    // A flat BMP (cooks much smaller), a noisy PNG (the source stays: smaller), an atlas whose source changes.
    CHECK(WritePng(project / "noise.png", 64, 64, Pattern(64, 64, 9, true)));
    {
        // stb writes BMP through its callback API; a tiny uncompressed TGA-like BMP is enough here.
        std::vector<uint8_t> bmp(54 + 128 * 128 * 3, 0);
        const uint32_t size = uint32_t(bmp.size()), offset = 54, header = 40, w = 128, h = 128, pixels = 128 * 128 * 3;
        bmp[0] = 'B';
        bmp[1] = 'M';
        std::memcpy(&bmp[2], &size, 4);
        std::memcpy(&bmp[10], &offset, 4);
        std::memcpy(&bmp[14], &header, 4);
        std::memcpy(&bmp[18], &w, 4);
        std::memcpy(&bmp[22], &h, 4);
        bmp[26] = 1;
        bmp[28] = 24;
        std::memcpy(&bmp[34], &pixels, 4);
        std::fill(bmp.begin() + 54, bmp.end(), uint8_t(120));
        CHECK(FileSystem::WriteBinaryFile(project / "flat.bmp", bmp.data(), bmp.size()));
    }
    CHECK(WritePng(project / "Sprites/Hero/idle_0.png", 16, 16, Pattern(16, 16, 1, false)));
    CHECK(WritePng(project / "Sprites/Hero/idle_1.png", 16, 16, Pattern(16, 16, 2, false)));
    std::string error;
    const std::string atlasPath = SpriteAtlas::CreateFromFolder(project, "Sprites/Hero", error);
    CHECK(!atlasPath.empty());
    CHECK(WritePng(project / "Sprites/Hero/idle_2.png", 16, 16, Pattern(16, 16, 3, false))); // changed after the last build
    // The "copied" export.
    {
        auto cooked = TextureCook::CookFile(project / "noise.png", TextureCookSettings(), false, {});
        CHECK(FileSystem::WriteBinaryFile(project / "precooked.png", cooked.Bytes.data(), cooked.Bytes.size()));
    }
    std::vector<std::string> files = { "flat.bmp", "noise.png", "precooked.png", atlasPath, "Sprites/Hero.atlas.png" };
    for (const auto& f : files)
    {
        std::filesystem::create_directories((exported / f).parent_path());
        std::filesystem::copy_file(project / f, exported / f);
        if (std::filesystem::exists(project / (f + ".zmeta"))) std::filesystem::copy_file(project / (f + ".zmeta"), exported / (f + ".zmeta"));
    }
    ExportCookInputs in;
    in.SourceAssets = project;
    in.ExportAssets = exported;
    in.Files = files;
    in.CacheDirectory = temp.GetPath() / "Cache";
    auto result = ExportCook::Run(in);
    CHECK(result.Atlases == 1 && result.Warnings.empty());
    CHECK(result.Assets.size() == 4);
    std::map<std::string, CookedAsset> byPath;
    for (const auto& a : result.Assets) byPath[a.Path] = a;
    CHECK(byPath["flat.bmp"].Action == "cooked" && byPath["flat.bmp"].CookedBytes < byPath["flat.bmp"].SourceBytes / 10);
    CHECK(byPath["noise.png"].Action == "kept" || byPath["noise.png"].Action == "cooked"); // noise: about as large either way
    CHECK(byPath["precooked.png"].Action == "kept" && byPath["precooked.png"].Note == "already cooked");
    CHECK(byPath["Sprites/Hero.atlas.png"].Action == "cooked" || byPath["Sprites/Hero.atlas.png"].Action == "kept");
    CHECK(TextureCook::IsCookedFile(exported / "flat.bmp"));
    // The re-packed page has the new sprite (the project's page is untouched).
    auto exportedSheet = SpriteSheet::FromImportSettings(AssetMetaFile::Read(exported / "Sprites/Hero.atlas.png")->Import);
    CHECK(exportedSheet.Find("idle_2") != nullptr);
    CHECK(SpriteSheet::FromImportSettings(AssetMetaFile::Read(project / "Sprites/Hero.atlas.png")->Import).Find("idle_2") == nullptr);
    // The manifest next to the exported assets, with the atlas UV table.
    const std::string manifest = FileSystem::ReadTextFile(exported.parent_path() / TextureCook::ManifestFileName);
    std::vector<CookedAsset> listed;
    CHECK(TextureCook::ManifestFromJson(manifest, listed) && listed.size() == 4 && manifest.find("idle_2") != std::string::npos);
    // The cooked BMP decodes to the same pixels as the source.
    ImageData a, b;
    CHECK(Image::Load((project / "flat.bmp").string(), a, 4) && Image::Load((exported / "flat.bmp").string(), b, 4) && a.Pixels == b.Pixels);
    // Incremental: a second cook of fresh copies hits the cache for every texture.
    for (const auto& f : files) std::filesystem::copy_file(project / f, exported / f, std::filesystem::copy_options::overwrite_existing);
    auto second = ExportCook::Run(in);
    CHECK(second.CacheHits >= 2);
    std::printf("    Cook: %zu textures, %zu cache hits on the second run, %.3f s / %.3f s\n", second.Assets.size(), second.CacheHits, result.Seconds, second.Seconds);
    // Export settings: the cook options round-trip through the project's "Export" section (defaults omitted).
    ExportSettings settings;
    CHECK(settings.ToJson().find("Cook") == std::string::npos);
    settings.Cook.MaxTextureSize = 512;
    settings.Cook.Always = true;
    ExportSettings back;
    CHECK(ExportSettings::FromJson(settings.ToJson(), back) && back == settings);
}
