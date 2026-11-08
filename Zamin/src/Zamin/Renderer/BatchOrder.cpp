#include "Zamin/Renderer/BatchOrder.h"

#include <algorithm>

namespace Zamin::BatchOrder {

    namespace {
        bool s_Enabled = true;

        // Scratch kept between frames (Order runs every frame: no allocations once it has grown).
        struct Scratch
        {
            std::vector<uint8_t> Taken;
            std::vector<uint32_t> Skipped;
            std::vector<uint32_t> Input;
            std::vector<uint32_t> Output;
        };
        thread_local Scratch t_Scratch;

        // The greedy pass over `input` (indices into items) into `output`.
        size_t Run(std::span<const BatchOrderItem> items, const std::vector<uint32_t>& input, std::vector<uint32_t>& output, uint32_t window)
        {
            Scratch& scratch = t_Scratch;
            const size_t n = input.size();
            scratch.Taken.assign(n, 0);
            output.clear();
            size_t moved = 0;
            for (size_t front = 0; front < n; front++)
            {
                if (scratch.Taken[front])
                    continue;
                scratch.Taken[front] = 1;
                output.push_back(input[front]);
                const BatchOrderItem& head = items[input[front]];
                if (head.Batch == 0 || !head.HasBounds())
                    continue;
                // Pull the following draws of this batch forward over those in between that they do not overlap.
                std::vector<uint32_t>& skipped = scratch.Skipped;
                skipped.clear();
                for (size_t j = front + 1; j < n; j++)
                {
                    if (scratch.Taken[j])
                        continue;
                    const BatchOrderItem& item = items[input[j]];
                    if (!item.HasBounds())
                        break; // nothing passes a draw of unknown extent
                    if (item.Batch == head.Batch)
                    {
                        bool free = true;
                        for (uint32_t k : skipped)
                            if (item.Overlaps(items[input[k]]))
                            {
                                free = false;
                                break;
                            }
                        if (free)
                        {
                            scratch.Taken[j] = 1;
                            output.push_back(input[j]);
                            if (!skipped.empty())
                                moved++;
                            continue;
                        }
                    }
                    if (skipped.size() >= window)
                        break;
                    skipped.push_back((uint32_t)j);
                }
            }
            return moved;
        }
    }

    size_t Order(std::span<const BatchOrderItem> items, std::vector<uint32_t>& order, uint32_t window)
    {
        order.resize(items.size());
        for (size_t i = 0; i < items.size(); i++)
            order[i] = (uint32_t)i;
        if (!s_Enabled || items.size() < 3)
            return 0;
        return OrderRange(items, order, 0, order.size(), window);
    }

    size_t OrderRange(std::span<const BatchOrderItem> items, std::vector<uint32_t>& order, size_t begin, size_t end, uint32_t window)
    {
        end = std::min(end, order.size());
        if (begin + 2 >= end)
            return 0;
        Scratch& scratch = t_Scratch;
        scratch.Input.assign(order.begin() + (std::ptrdiff_t)begin, order.begin() + (std::ptrdiff_t)end);
        const size_t moved = Run(items, scratch.Input, scratch.Output, std::max<uint32_t>(1, window));
        std::copy(scratch.Output.begin(), scratch.Output.end(), order.begin() + (std::ptrdiff_t)begin);
        return moved;
    }

    size_t CountSwitches(std::span<const BatchOrderItem> items, std::span<const uint32_t> order)
    {
        size_t switches = 0;
        for (size_t i = 0; i < order.size(); i++)
        {
            const BatchOrderItem& item = items[order[i]];
            if (i == 0 || item.Batch == 0 || item.Batch != items[order[i - 1]].Batch)
                switches++;
        }
        return switches;
    }

    void SetEnabled(bool enabled) { s_Enabled = enabled; }
    bool IsEnabled() { return s_Enabled; }

}
