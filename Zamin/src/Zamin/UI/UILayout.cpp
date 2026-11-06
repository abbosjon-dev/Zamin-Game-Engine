#include "Zamin/UI/UILayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin {

    namespace UILayout
    {
        // ------------------------------------------------------------------ rectangles
        UIRect ComputeRect(const UIRect& parent, const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& pivot,
                           const glm::vec2& position, const glm::vec2& size)
        {
            const glm::vec2 parentSize = parent.GetSize();
            const glm::vec2 a0 = parent.Min + anchorMin * parentSize;
            const glm::vec2 a1 = parent.Min + anchorMax * parentSize;
            // Offsets from the anchors' corners (OffsetMin from AnchorMin, OffsetMax from AnchorMax).
            const glm::vec2 offsetMin = position - size * pivot;
            const glm::vec2 offsetMax = position + size * (1.0f - pivot);
            const glm::vec2 p0 = a0 + offsetMin, p1 = a1 + offsetMax;
            // A negative size flips nothing: the rectangle between the two corners.
            return { glm::min(p0, p1), glm::max(p0, p1) };
        }

        void FromRect(const UIRect& parent, const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& pivot,
                      const UIRect& rect, glm::vec2& position, glm::vec2& size)
        {
            const glm::vec2 parentSize = parent.GetSize();
            const glm::vec2 offsetMin = rect.Min - (parent.Min + anchorMin * parentSize);
            const glm::vec2 offsetMax = rect.Max - (parent.Min + anchorMax * parentSize);
            size = offsetMax - offsetMin;
            position = offsetMin + size * pivot;
        }

        glm::vec2 PresetPoint(int preset)
        {
            preset = std::clamp(preset, 0, 8);
            return { (preset % 3) * 0.5f, 1.0f - (preset / 3) * 0.5f };
        }

        glm::vec2 AlignmentOf(int alignment)
        {
            alignment = std::clamp(alignment, 0, 8);
            return { (alignment % 3) * 0.5f, (alignment / 3) * 0.5f };
        }

        // ------------------------------------------------------------------ canvas scale
        float CanvasScale(const glm::vec2& screen, UIScaleMode mode, float scaleFactor, const glm::vec2& referenceResolution,
                          UIScreenMatchMode matchMode, float matchWidthOrHeight)
        {
            if (screen.x <= 0.0f || screen.y <= 0.0f)
                return 1.0f;
            float scale = 1.0f;
            if (mode == UIScaleMode::ConstantPixelSize)
                scale = scaleFactor;
            else
            {
                const glm::vec2 reference = glm::max(referenceResolution, glm::vec2(1.0f));
                const glm::vec2 ratio = screen / reference;
                switch (matchMode)
                {
                    case UIScreenMatchMode::MatchWidthOrHeight:
                    {
                        // Blended in log space: halfway between "twice as wide" and "as high" is sqrt(2), not 1.5.
                        const float t = std::clamp(matchWidthOrHeight, 0.0f, 1.0f);
                        scale = std::exp2(glm::mix(std::log2(ratio.x), std::log2(ratio.y), t));
                        break;
                    }
                    case UIScreenMatchMode::Expand: scale = std::min(ratio.x, ratio.y); break;
                    case UIScreenMatchMode::Shrink: scale = std::max(ratio.x, ratio.y); break;
                }
            }
            return std::isfinite(scale) ? std::max(scale, 0.01f) : 1.0f;
        }

        // ------------------------------------------------------------------ rows / columns
        struct AxisSizes
        {
            float Min = 0.0f, Preferred = 0.0f, Flexible = 0.0f;
        };

        static bool ControlsSize(const UILayoutGroupSettings& g, int axis) { return axis == 0 ? g.ControlChildWidth : g.ControlChildHeight; }
        static bool ForceExpands(const UILayoutGroupSettings& g, int axis) { return axis == 0 ? g.ChildForceExpandWidth : g.ChildForceExpandHeight; }
        static float PaddingTotal(const UILayoutGroupSettings& g, int axis) { return axis == 0 ? g.Padding.x + g.Padding.y : g.Padding.z + g.Padding.w; }
        // From the left (x) / from the top (y).
        static float PaddingStart(const UILayoutGroupSettings& g, int axis) { return axis == 0 ? g.Padding.x : g.Padding.z; }

        static AxisSizes ChildSizes(const UILayoutGroupSettings& g, const UILayoutItem& item, int axis)
        {
            AxisSizes s;
            if (!ControlsSize(g, axis))
            {
                s.Min = s.Preferred = std::max(0.0f, item.Size[axis]);
                s.Flexible = 0.0f;
            }
            else
            {
                s.Min = std::max(0.0f, item.Min[axis]);
                s.Preferred = std::max(s.Min, item.Preferred[axis]);
                s.Flexible = std::max(0.0f, item.Flexible[axis]);
            }
            if (ForceExpands(g, axis))
                s.Flexible = std::max(s.Flexible, 1.0f);
            return s;
        }

        static int MainAxis(const UILayoutGroupSettings& g) { return g.Type == UILayoutType::Horizontal ? 0 : 1; }

        static AxisSizes LinearAxisSizes(const UILayoutGroupSettings& g, std::span<const UILayoutItem> items, int axis)
        {
            const float padding = PaddingTotal(g, axis);
            AxisSizes total{ padding, padding, 0.0f };
            if (items.empty())
                return total;
            if (axis == MainAxis(g))
            {
                const float spacing = g.Spacing * (float)(items.size() - 1);
                total.Min += spacing;
                total.Preferred += spacing;
                for (const UILayoutItem& item : items)
                {
                    const AxisSizes s = ChildSizes(g, item, axis);
                    total.Min += s.Min;
                    total.Preferred += s.Preferred;
                    total.Flexible += s.Flexible;
                }
            }
            else
            {
                float minimum = 0.0f, preferred = 0.0f, flexible = 0.0f;
                for (const UILayoutItem& item : items)
                {
                    const AxisSizes s = ChildSizes(g, item, axis);
                    minimum = std::max(minimum, s.Min);
                    preferred = std::max(preferred, s.Preferred);
                    flexible = std::max(flexible, s.Flexible);
                }
                total.Min += minimum;
                total.Preferred += preferred;
                total.Flexible = flexible;
            }
            return total;
        }

        // Positions (from the left / the top) and sizes of the children along one axis.
        static void LinearAxis(const UILayoutGroupSettings& g, std::span<const UILayoutItem> items, int axis, float size,
                               std::vector<float>& position, std::vector<float>& length)
        {
            const size_t n = items.size();
            position.assign(n, 0.0f);
            length.assign(n, 0.0f);
            const bool control = ControlsSize(g, axis);
            const float alignment = AlignmentOf(g.ChildAlignment)[axis];
            const float padding = PaddingTotal(g, axis), start = PaddingStart(g, axis);
            if (axis != MainAxis(g))
            {
                // Across: every child on its own, aligned in the space between the paddings.
                const float inner = size - padding;
                for (size_t i = 0; i < n; i++)
                {
                    const AxisSizes s = ChildSizes(g, items[i], axis);
                    const float required = std::clamp(inner, s.Min, s.Flexible > 0.0f ? std::max(s.Min, size) : s.Preferred);
                    const float offset = start + (size - (required + padding)) * alignment;
                    if (control)
                    {
                        position[i] = offset;
                        length[i] = required;
                    }
                    else
                    {
                        const float own = std::max(0.0f, items[i].Size[axis]);
                        position[i] = offset + (required - own) * alignment;
                        length[i] = own;
                    }
                }
                return;
            }

            // Along: one after another; the space left over goes to the flexible ones (or, with
            // none, the whole row is aligned), too little space shrinks them towards their minimum.
            const AxisSizes total = LinearAxisSizes(g, items, axis);
            float pos = start;
            float flexibleShare = 0.0f;
            const float surplus = size - total.Preferred;
            if (surplus > 0.0f)
            {
                if (total.Flexible <= 0.0f)
                    pos = start + surplus * alignment;
                else
                    flexibleShare = surplus / total.Flexible;
            }
            float minToPreferred = 0.0f;
            if (total.Min != total.Preferred)
                minToPreferred = std::clamp((size - total.Min) / (total.Preferred - total.Min), 0.0f, 1.0f);
            for (size_t k = 0; k < n; k++)
            {
                const size_t i = g.ReverseArrangement ? n - 1 - k : k;
                const AxisSizes s = ChildSizes(g, items[i], axis);
                const float childSize = glm::mix(s.Min, s.Preferred, minToPreferred) + s.Flexible * flexibleShare;
                if (control)
                {
                    position[i] = pos;
                    length[i] = childSize;
                }
                else
                {
                    const float own = std::max(0.0f, items[i].Size[axis]);
                    position[i] = pos + (childSize - own) * alignment;
                    length[i] = own;
                }
                pos += childSize + g.Spacing;
            }
        }

        // ------------------------------------------------------------------ grid
        struct GridCounts
        {
            int Columns = 1, Rows = 1;   // the cells the children take
            int PerMainAxis = 1;         // cells along the start axis before the next row / column
        };

        static int CeilDiv(int a, int b) { return b <= 0 ? a : (a + b - 1) / b; }

        static int FitCount(float space, float cell, float spacing, int fallback)
        {
            const float step = cell + spacing;
            if (step <= 0.0f)
                return fallback;
            return std::max(1, (int)std::floor((space + spacing + 0.001f) / step));
        }

        static GridCounts CountGrid(const UILayoutGroupSettings& g, int n, const glm::vec2& size)
        {
            GridCounts c;
            n = std::max(n, 0);
            const int count = std::max(1, g.ConstraintCount);
            int columns = 1, rows = 1;
            switch (g.Constraint)
            {
                case UIGridConstraint::FixedColumnCount: columns = count; rows = std::max(1, CeilDiv(n, count)); break;
                case UIGridConstraint::FixedRowCount:    rows = count; columns = std::max(1, CeilDiv(n, count)); break;
                case UIGridConstraint::Flexible:
                    columns = FitCount(size.x - PaddingTotal(g, 0), g.CellSize.x, g.GridSpacing.x, std::max(1, n));
                    rows = FitCount(size.y - PaddingTotal(g, 1), g.CellSize.y, g.GridSpacing.y, std::max(1, n));
                    break;
            }
            if (!g.StartAxisVertical)
            {
                c.PerMainAxis = columns;
                c.Columns = std::clamp(columns, 1, std::max(1, n));
                c.Rows = std::max(1, CeilDiv(n, c.PerMainAxis));
            }
            else
            {
                c.PerMainAxis = rows;
                c.Rows = std::clamp(rows, 1, std::max(1, n));
                c.Columns = std::max(1, CeilDiv(n, c.PerMainAxis));
            }
            return c;
        }

        static UILayoutSizes GridSizes(const UILayoutGroupSettings& g, int n, const glm::vec2& groupSize)
        {
            UILayoutSizes s;
            const glm::vec2 padding{ PaddingTotal(g, 0), PaddingTotal(g, 1) };
            auto span = [&](int cells, int axis) { return cells <= 0 ? 0.0f : cells * g.CellSize[axis] + (cells - 1) * g.GridSpacing[axis]; };
            int minColumns = 1, preferredColumns = 1;
            switch (g.Constraint)
            {
                case UIGridConstraint::FixedColumnCount: minColumns = preferredColumns = std::max(1, g.ConstraintCount); break;
                case UIGridConstraint::FixedRowCount:    minColumns = preferredColumns = std::max(1, CeilDiv(n, std::max(1, g.ConstraintCount))); break;
                case UIGridConstraint::Flexible:         minColumns = 1; preferredColumns = std::max(1, (int)std::ceil(std::sqrt((float)std::max(n, 0)))); break;
            }
            s.Min.x = padding.x + span(n > 0 ? minColumns : 0, 0);
            s.Preferred.x = padding.x + span(n > 0 ? preferredColumns : 0, 0);
            // The rows for the width it has.
            int rows = 0;
            if (n > 0)
            {
                if (g.Constraint == UIGridConstraint::FixedRowCount)
                    rows = std::max(1, g.ConstraintCount);
                else
                {
                    const int columns = g.Constraint == UIGridConstraint::FixedColumnCount
                        ? std::max(1, g.ConstraintCount)
                        : FitCount(groupSize.x - padding.x, g.CellSize.x, g.GridSpacing.x, n);
                    rows = CeilDiv(n, columns);
                }
            }
            s.Min.y = s.Preferred.y = padding.y + span(rows, 1);
            return s;
        }

        // ------------------------------------------------------------------ groups
        UILayoutSizes CalcSizes(const UILayoutGroupSettings& group, std::span<const UILayoutItem> items, const glm::vec2& groupSize)
        {
            if (group.Type == UILayoutType::Grid)
                return GridSizes(group, (int)items.size(), groupSize);
            UILayoutSizes s;
            for (int axis = 0; axis < 2; axis++)
            {
                const AxisSizes a = LinearAxisSizes(group, items, axis);
                s.Min[axis] = a.Min;
                s.Preferred[axis] = a.Preferred;
                s.Flexible[axis] = a.Flexible;
            }
            return s;
        }

        void Arrange(const UIRect& rect, const UILayoutGroupSettings& group, std::span<const UILayoutItem> items, std::vector<UIRect>& out)
        {
            out.assign(items.size(), UIRect{});
            if (items.empty())
                return;
            const glm::vec2 size = rect.GetSize();
            if (group.Type == UILayoutType::Grid)
            {
                const int n = (int)items.size();
                const GridCounts counts = CountGrid(group, n, size);
                const glm::vec2 cell = glm::max(group.CellSize, glm::vec2(0.0f));
                const glm::vec2 required{ counts.Columns * cell.x + (counts.Columns - 1) * group.GridSpacing.x,
                                          counts.Rows * cell.y + (counts.Rows - 1) * group.GridSpacing.y };
                const glm::vec2 alignment = AlignmentOf(group.ChildAlignment);
                const glm::vec2 start{ PaddingStart(group, 0) + (size.x - (required.x + PaddingTotal(group, 0))) * alignment.x,
                                       PaddingStart(group, 1) + (size.y - (required.y + PaddingTotal(group, 1))) * alignment.y };
                const bool fromRight = group.StartCorner % 2 == 1, fromBottom = group.StartCorner / 2 == 1;
                for (int i = 0; i < n; i++)
                {
                    int column = !group.StartAxisVertical ? i % counts.PerMainAxis : i / counts.PerMainAxis;
                    int row = !group.StartAxisVertical ? i / counts.PerMainAxis : i % counts.PerMainAxis;
                    if (fromRight) column = counts.Columns - 1 - column;
                    if (fromBottom) row = counts.Rows - 1 - row;
                    const float x = rect.Min.x + start.x + (cell.x + group.GridSpacing.x) * column;
                    const float top = rect.Max.y - (start.y + (cell.y + group.GridSpacing.y) * row);
                    out[i] = { { x, top - cell.y }, { x + cell.x, top } };
                }
                return;
            }

            std::vector<float> xPosition, xLength, yPosition, yLength;
            LinearAxis(group, items, 0, size.x, xPosition, xLength);
            LinearAxis(group, items, 1, size.y, yPosition, yLength);
            for (size_t i = 0; i < items.size(); i++)
            {
                const float x = rect.Min.x + xPosition[i];
                const float top = rect.Max.y - yPosition[i];
                out[i] = { { x, top - yLength[i] }, { x + xLength[i], top } };
            }
        }

        // ------------------------------------------------------------------ navigation
        glm::vec2 DirectionVector(UINavDirection direction)
        {
            switch (direction)
            {
                case UINavDirection::Up:    return { 0.0f, 1.0f };
                case UINavDirection::Down:  return { 0.0f, -1.0f };
                case UINavDirection::Left:  return { -1.0f, 0.0f };
                case UINavDirection::Right: return { 1.0f, 0.0f };
            }
            return { 0.0f, 0.0f };
        }

        int FindInDirection(const UIRect& from, UINavDirection direction, std::span<const UIRect> candidates)
        {
            const glm::vec2 dir = DirectionVector(direction);
            // From the middle of the edge it leaves through: a neighbour overlapping it sideways is
            // still ahead.
            const glm::vec2 edge = from.GetCenter() + dir * from.GetSize() * 0.5f;
            int best = -1;
            float bestScore = -std::numeric_limits<float>::infinity();
            for (size_t i = 0; i < candidates.size(); i++)
            {
                const glm::vec2 v = candidates[i].GetCenter() - edge;
                const float ahead = glm::dot(dir, v);
                if (ahead <= 0.0f)
                    continue;
                // Near and straight ahead scores high: ahead / distance^2 (= cos / distance).
                const float score = ahead / glm::dot(v, v);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = (int)i;
                }
            }
            return best;
        }
    }

}
