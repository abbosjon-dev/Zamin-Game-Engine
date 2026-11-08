// ZaminBench --fonts [--json <file>]: what a font costs (task 485), headless. The main atlas of the engine's font
// (2048 px, 600+ glyphs, 2 x 2 oversampling) rasterized vs read back from the atlas cache on disk (FontAtlasCache),
// glyph lookups (the direct table vs the hash map) and laying text out with the font's kerning pairs off / on.
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

using namespace Zamin;

namespace {
    using Clock = std::chrono::steady_clock;
    double Ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

    double Median(std::vector<double> v)
    {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    }
}

int RunFontBench(const char* jsonFile)
{
    const std::filesystem::path file = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Resources" / "fonts" / "Inter-SemiBold.ttf";
    const std::filesystem::path previous = FontAtlasCache::GetDirectory();
    const std::filesystem::path cache = FileSystem::CreateTempDirectory("zamin-font-bench");
    std::printf("Zamin font benchmark: %s\n\n", file.filename().string().c_str());

    // ---- the main atlas: rasterized (no cache) / stored / read back
    std::vector<double> baked, loaded;
    double stored = 0.0;
    FontAtlasCache::SetDirectory({});
    for (int i = 0; i < 3; i++)
    {
        const auto start = Clock::now();
        Ref<Font> font = Font::CreateHeadless(file);
        baked.push_back(Ms(start));
    }
    FontAtlasCache::SetDirectory(cache);
    {
        const auto start = Clock::now();
        Ref<Font> font = Font::CreateHeadless(file);
        stored = Ms(start);
    }
    for (int i = 0; i < 3; i++)
    {
        const auto start = Clock::now();
        Ref<Font> font = Font::CreateHeadless(file);
        loaded.push_back(Ms(start));
    }
    uintmax_t bytes = 0;
    for (const auto& entry : std::filesystem::directory_iterator(cache))
        bytes += entry.file_size();
    std::printf("Asosiy atlas (2048 px): rasterlash %.1f ms, keshga yozish bilan %.1f ms, keshdan o'qish %.1f ms (fayl %.2f MB)\n", Median(baked), stored,
                Median(loaded), (double)bytes / (1024.0 * 1024.0));

    // ---- glyph lookups and layout
    Ref<Font> font = Font::CreateHeadless(file);
    const FontAtlas& atlas = font->GetMainAtlas();
    const std::string sample = "O'zbekiston - Zamin dvigateli: AVATAR, To'p, Yer. Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
    std::vector<uint32_t> codepoints;
    for (size_t i = 0; i < sample.size();)
        codepoints.push_back(DecodeUTF8(sample, i));
    const int rounds = 20000;
    double sink = 0.0;
    auto start = Clock::now();
    for (int r = 0; r < rounds; r++)
        for (uint32_t cp : codepoints)
        {
            auto it = atlas.Glyphs.find(cp);
            sink += it != atlas.Glyphs.end() ? it->second.Advance : 0.0f;
        }
    const double mapNs = Ms(start) * 1e6 / ((double)rounds * (double)codepoints.size());
    start = Clock::now();
    for (int r = 0; r < rounds; r++)
        for (uint32_t cp : codepoints)
            if (const Glyph* g = atlas.Find(cp))
                sink += g->Advance;
    const double tableNs = Ms(start) * 1e6 / ((double)rounds * (double)codepoints.size());
    std::printf("Glif qidirish: hash xarita %.2f ns, to'g'ridan-to'g'ri jadval %.2f ns\n", mapNs, tableNs);

    const TextMetrics metrics = TextMetrics::Of(font.get());
    TextLayoutSettings wrap;
    wrap.WordWrap = true;
    const UIRect box{ { 0.0f, 0.0f }, { 300.0f, 1000.0f } };
    double layoutNs[2] = {};
    float widths[2] = {};
    for (int kerning = 0; kerning < 2; kerning++)
    {
        font->SetKerningEnabled(kerning == 1);
        (void)TextLayout::Layout(sample, metrics, 24.0f, box, wrap); // the pairs looked up once
        start = Clock::now();
        for (int r = 0; r < 2000; r++)
            sink += TextLayout::Layout(sample, metrics, 24.0f, box, wrap).Size.y;
        layoutNs[kerning] = Ms(start) * 1e6 / (2000.0 * (double)codepoints.size());
        widths[kerning] = TextLayout::MeasureWidth("AVATAR To'p Yer", metrics, 24.0f, 0.0f);
    }
    std::printf("Matn joylash (so'z o'rash, 24 px): kerning'siz %.1f ns / belgi, kerning bilan %.1f ns / belgi\n", layoutNs[0], layoutNs[1]);
    std::printf("\"AVATAR To'p Yer\" kengligi 24 px: %.2f px -> kerning bilan %.2f px (AV %.3f em)\n", widths[0], widths[1], font->GetKerning('A', 'V'));

    FontAtlasCache::SetDirectory(previous);
    std::error_code ec;
    std::filesystem::remove_all(cache, ec);
    if (jsonFile && *jsonFile)
    {
        FileSystem::WriteTextFile(jsonFile,
                                  std::format("{{\n  \"AtlasBakeMs\": {:.2f},\n  \"AtlasStoreMs\": {:.2f},\n  \"AtlasCacheLoadMs\": {:.2f},\n  \"AtlasCacheBytes\": {},\n"
                                              "  \"GlyphMapNs\": {:.3f},\n  \"GlyphTableNs\": {:.3f},\n  \"LayoutNsPerChar\": {:.2f},\n  \"LayoutKerningNsPerChar\": {:.2f},\n"
                                              "  \"Sink\": {:.1f}\n}}\n",
                                              Median(baked), stored, Median(loaded), (uintmax_t)bytes, mapNs, tableNs, layoutNs[0], layoutNs[1], sink > 0 ? 1.0 : 0.0));
        std::printf("\nJSON: %s\n", jsonFile);
    }
    return 0;
}
