#include "Zamin/Asset/TextureAtlas.h"

#include <algorithm>
#include <numeric>

namespace Zamin {

    uint64_t HashBytes(const void* data, size_t size, uint64_t seed)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = seed;
        for (size_t i = 0; i < size; i++)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    uint32_t AtlasSettings::EffectivePadding() const { return std::max(Padding, MipLevels ? Alignment() : 0u); }

    double AtlasResult::Occupancy() const
    {
        uint64_t total = 0;
        for (const AtlasPage& page : Pages)
            total += (uint64_t)page.Width * page.Height;
        return total ? (double)ImagePixels / (double)total : 0.0;
    }

    uint64_t AtlasResult::Hash() const
    {
        uint64_t hash = HashBytes(nullptr, 0);
        for (const AtlasPage& page : Pages)
        {
            const uint32_t size[2] = { page.Width, page.Height };
            hash = HashBytes(size, sizeof(size), hash);
            hash = HashBytes(page.Pixels.data(), page.Pixels.size(), hash);
        }
        for (const AtlasPlacement& p : Placements)
        {
            const uint32_t values[5] = { p.Page, p.X, p.Y, p.Width, p.Height };
            hash = HashBytes(values, sizeof(values), hash);
            hash = HashBytes(p.Name.data(), p.Name.size(), hash);
        }
        return hash;
    }

    namespace {
        struct Rect
        {
            uint32_t X = 0, Y = 0, W = 0, H = 0;
            bool Contains(const Rect& o) const { return o.X >= X && o.Y >= Y && o.X + o.W <= X + W && o.Y + o.H <= Y + H; }
            bool Overlaps(const Rect& o) const { return o.X < X + W && o.X + o.W > X && o.Y < Y + H && o.Y + o.H > Y; }
        };

        uint32_t AlignUp(uint32_t value, uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }
        uint32_t NextPowerOfTwo(uint32_t value)
        {
            uint32_t result = 1;
            while (result < value) result <<= 1;
            return result;
        }

        // MaxRects with the best-short-side-fit rule; ties go to the top-most, then left-most free rectangle.
        class MaxRects
        {
        public:
            explicit MaxRects(uint32_t size) { m_Free.push_back({ 0, 0, size, size }); }
            bool Insert(uint32_t w, uint32_t h, Rect& out)
            {
                bool found = false;
                uint64_t bestShort = ~0ull, bestLong = ~0ull;
                for (const Rect& f : m_Free)
                {
                    if (f.W < w || f.H < h) continue;
                    const uint64_t dx = f.W - w, dy = f.H - h, shortSide = std::min(dx, dy), longSide = std::max(dx, dy);
                    const bool better = !found || shortSide < bestShort || (shortSide == bestShort && longSide < bestLong) ||
                                        (shortSide == bestShort && longSide == bestLong && (f.Y < out.Y || (f.Y == out.Y && f.X < out.X)));
                    if (better)
                    {
                        out = { f.X, f.Y, w, h };
                        bestShort = shortSide;
                        bestLong = longSide;
                        found = true;
                    }
                }
                if (!found) return false;
                Place(out);
                return true;
            }

        private:
            void Place(const Rect& used)
            {
                std::vector<Rect> next;
                next.reserve(m_Free.size() + 4);
                for (const Rect& f : m_Free)
                {
                    if (!f.Overlaps(used))
                    {
                        next.push_back(f);
                        continue;
                    }
                    if (used.X > f.X) next.push_back({ f.X, f.Y, used.X - f.X, f.H });
                    if (used.X + used.W < f.X + f.W) next.push_back({ used.X + used.W, f.Y, f.X + f.W - used.X - used.W, f.H });
                    if (used.Y > f.Y) next.push_back({ f.X, f.Y, f.W, used.Y - f.Y });
                    if (used.Y + used.H < f.Y + f.H) next.push_back({ f.X, used.Y + used.H, f.W, f.Y + f.H - used.Y - used.H });
                }
                // Free rectangles inside another are dropped (stable order keeps the packing deterministic).
                m_Free.clear();
                for (size_t i = 0; i < next.size(); i++)
                {
                    bool contained = false;
                    for (size_t j = 0; j < next.size() && !contained; j++)
                        if (i != j && next[j].Contains(next[i]) && (!next[i].Contains(next[j]) || j < i)) contained = true;
                    if (!contained) m_Free.push_back(next[i]);
                }
            }
            std::vector<Rect> m_Free;
        };

        void Blit(AtlasPage& page, const AtlasImage& image, const Rect& cell, uint32_t padding)
        {
            const int64_t w = image.Width, h = image.Height;
            for (uint32_t y = cell.Y; y < cell.Y + cell.H; y++)
                for (uint32_t x = cell.X; x < cell.X + cell.W; x++)
                {
                    int64_t sx = int64_t(x) - int64_t(cell.X + padding), sy = int64_t(y) - int64_t(cell.Y + padding);
                    const bool inside = sx >= 0 && sy >= 0 && sx < w && sy < h;
                    uint8_t* out = page.Pixels.data() + ((size_t)y * page.Width + x) * 4;
                    if (!inside)
                    {
                        if (image.Bleed == AtlasBleed::None)
                        {
                            out[0] = out[1] = out[2] = out[3] = 0;
                            continue;
                        }
                        if (image.Bleed == AtlasBleed::Clamp)
                        {
                            sx = std::clamp<int64_t>(sx, 0, w - 1);
                            sy = std::clamp<int64_t>(sy, 0, h - 1);
                        }
                        else
                        {
                            sx = ((sx % w) + w) % w;
                            sy = ((sy % h) + h) % h;
                        }
                    }
                    const uint8_t* in = image.Pixels.data() + ((size_t)sy * image.Width + (size_t)sx) * 4;
                    std::copy(in, in + 4, out);
                }
        }
    }

    bool AtlasPacker::Pack(std::span<const AtlasImage> images, const AtlasSettings& settings, AtlasResult& out)
    {
        out = {};
        const uint32_t alignment = settings.Alignment(), padding = settings.EffectivePadding();
        uint32_t largestPowerOfTwo = 1;
        while (largestPowerOfTwo * 2 <= std::max(1u, settings.MaxPageSize)) largestPowerOfTwo *= 2;
        const uint32_t maxPage = std::max(alignment, settings.PowerOfTwo ? largestPowerOfTwo : AlignUp(std::max(1u, settings.MaxPageSize), alignment));
        std::vector<Rect> cells(images.size());
        for (size_t i = 0; i < images.size(); i++)
        {
            const AtlasImage& image = images[i];
            if (!image.Width || !image.Height || image.Pixels.size() != (size_t)image.Width * image.Height * 4)
            {
                out.Error = "'" + image.Name + "': empty image or wrong pixel count";
                return false;
            }
            cells[i].W = AlignUp(image.Width + 2 * padding, alignment);
            cells[i].H = AlignUp(image.Height + 2 * padding, alignment);
            if (cells[i].W > maxPage || cells[i].H > maxPage)
            {
                out.Error = "'" + image.Name + "' (" + std::to_string(image.Width) + "x" + std::to_string(image.Height) + " px with padding) does not fit a " +
                            std::to_string(maxPage) + " px page";
                return false;
            }
        }
        // Tall first, then wide, then by name: independent of the order the files were listed in.
        std::vector<size_t> order(images.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (cells[a].H != cells[b].H) return cells[a].H > cells[b].H;
            if (cells[a].W != cells[b].W) return cells[a].W > cells[b].W;
            return images[a].Name < images[b].Name;
        });
        out.Placements.resize(images.size());
        std::vector<Rect> placed(images.size());
        std::vector<size_t> remaining = order;
        while (!remaining.empty())
        {
            if (out.Pages.size() >= settings.MaxPages)
            {
                out = { .Error = "more than " + std::to_string(settings.MaxPages) + " pages are needed" };
                return false;
            }
            uint64_t area = 0;
            for (size_t i : remaining) area += (uint64_t)cells[i].W * cells[i].H;
            uint32_t size = alignment;
            while ((uint64_t)size * size < area && size < maxPage) size *= 2;
            size = std::min(size, maxPage);
            std::vector<size_t> fitted, left;
            for (;;)
            {
                fitted.clear();
                left.clear();
                MaxRects packer(size);
                for (size_t i : remaining)
                {
                    Rect r;
                    if (packer.Insert(cells[i].W, cells[i].H, r))
                    {
                        placed[i] = r;
                        fitted.push_back(i);
                    }
                    else
                        left.push_back(i);
                }
                if (left.empty() || size >= maxPage) break;
                size = std::min(size * 2, maxPage);
            }
            if (fitted.empty())
            {
                out = { .Error = "an image does not fit an empty page" };
                return false;
            }
            uint32_t usedW = 0, usedH = 0;
            for (size_t i : fitted)
            {
                usedW = std::max(usedW, placed[i].X + placed[i].W);
                usedH = std::max(usedH, placed[i].Y + placed[i].H);
            }
            AtlasPage page;
            page.Width = settings.PowerOfTwo ? NextPowerOfTwo(usedW) : AlignUp(usedW, alignment);
            page.Height = settings.PowerOfTwo ? NextPowerOfTwo(usedH) : AlignUp(usedH, alignment);
            page.Pixels.assign((size_t)page.Width * page.Height * 4, 0);
            const uint32_t pageIndex = (uint32_t)out.Pages.size();
            for (size_t i : fitted)
            {
                Blit(page, images[i], placed[i], padding);
                AtlasPlacement& p = out.Placements[i];
                p.Name = images[i].Name;
                p.Page = pageIndex;
                p.X = placed[i].X + padding;
                p.Y = placed[i].Y + padding;
                p.Width = images[i].Width;
                p.Height = images[i].Height;
                p.UVMin = { (float)p.X / (float)page.Width, 1.0f - (float)(p.Y + p.Height) / (float)page.Height };
                p.UVMax = { (float)(p.X + p.Width) / (float)page.Width, 1.0f - (float)p.Y / (float)page.Height };
                out.ImagePixels += (uint64_t)p.Width * p.Height;
            }
            out.Pages.push_back(std::move(page));
            remaining = std::move(left);
        }
        return true;
    }

    std::vector<std::vector<uint8_t>> AtlasPacker::BuildMips(const AtlasPage& page, uint32_t levels)
    {
        std::vector<std::vector<uint8_t>> mips;
        const uint8_t* source = page.Pixels.data();
        uint32_t width = page.Width, height = page.Height;
        for (uint32_t level = 0; level < levels && (width > 1 || height > 1); level++)
        {
            const uint32_t w = std::max(1u, width / 2), h = std::max(1u, height / 2);
            std::vector<uint8_t> next((size_t)w * h * 4);
            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++)
                    for (uint32_t c = 0; c < 4; c++)
                    {
                        auto at = [&](uint32_t sx, uint32_t sy) {
                            return (uint32_t)source[((size_t)std::min(sy, height - 1) * width + std::min(sx, width - 1)) * 4 + c];
                        };
                        next[((size_t)y * w + x) * 4 + c] = (uint8_t)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                    }
            mips.push_back(std::move(next));
            source = mips.back().data();
            width = w;
            height = h;
        }
        return mips;
    }

}
