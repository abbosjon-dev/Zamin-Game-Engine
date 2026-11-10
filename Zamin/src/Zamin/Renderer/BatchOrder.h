#pragma once

/// @file
/// @brief BatchOrder: 2D draws reordered for fewer Renderer2D batches, the picture unchanged (task 481).

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace Zamin {

    /// One draw of a sorted 2D list as BatchOrder sees it.
    struct BatchOrderItem
    {
        /// The batch it goes into: draws with the same value can share one (sprites, circles, texts
        /// of one font ...). 0: it stays where it is (others may still pass it when they do not overlap).
        uint32_t Batch = 0;
        /// The rectangle it covers on the target (pixels or NDC, the same for every item, margins
        /// included). Min > Max: not known - it stays where it is and nothing passes it.
        glm::vec2 Min{ 1.0f }, Max{ -1.0f };

        bool HasBounds() const { return Min.x <= Max.x && Min.y <= Max.y; }
        bool Overlaps(const BatchOrderItem& other) const
        {
            return Min.x <= other.Max.x && other.Min.x <= Max.x && Min.y <= other.Max.y && other.Min.y <= Max.y;
        }
    };

    namespace BatchOrder {

        /// The order to submit `items` (sorted back to front) in: their own, except that a draw is
        /// brought forward to follow the last earlier one of its batch when it overlaps none of the
        /// draws it passes. Draws that overlap keep their order, so blending and depth tests see the
        /// same picture; what changes is how often the renderer switches shaders / textures. At most
        /// `window` passed draws are looked at for each move. `order` gets indices into `items`;
        /// returns how many draws were moved.
        size_t Order(std::span<const BatchOrderItem> items, std::vector<uint32_t>& order, uint32_t window = 64);

        /// The same for a part of `order` (its entries [begin, end) are indices into `items`): reordered
        /// in place by the items' Batch values. Returns how many moved.
        size_t OrderRange(std::span<const BatchOrderItem> items, std::vector<uint32_t>& order, size_t begin, size_t end,
                          uint32_t window = 64);

        /// How many times the batch changes along `order` (1 for one batch; 0 for none): what Order lowers.
        size_t CountSwitches(std::span<const BatchOrderItem> items, std::span<const uint32_t> order);

        /// On (default) / off: Scene's 2D pass uses Order (off: the sorted order as it is - to compare).
        void SetEnabled(bool enabled);
        bool IsEnabled();

    }

}
