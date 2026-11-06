#include "Zamin/UI/UIWidgets.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin {

    UIRect UIRect::Inset(float left, float right, float top, float bottom) const
    {
        UIRect r{ { Min.x + left, Min.y + bottom }, { Max.x - right, Max.y - top } };
        if (r.Min.x > r.Max.x) r.Min.x = r.Max.x = (r.Min.x + r.Max.x) * 0.5f;
        if (r.Min.y > r.Max.y) r.Min.y = r.Max.y = (r.Min.y + r.Max.y) * 0.5f;
        return r;
    }

    UIRect UIRect::Intersect(const UIRect& other) const
    {
        UIRect r{ glm::max(Min, other.Min), glm::min(Max, other.Max) };
        r.Max = glm::max(r.Max, r.Min);
        return r;
    }

    namespace UIMath {

        bool IsHorizontal(UIDirection direction) { return direction == UIDirection::LeftToRight || direction == UIDirection::RightToLeft; }
        bool IsReversed(UIDirection direction) { return direction == UIDirection::RightToLeft || direction == UIDirection::TopToBottom; }

        UIRect FitAspect(const UIRect& rect, float aspect)
        {
            glm::vec2 size = rect.GetSize();
            if (aspect <= 0.0f || size.x <= 0.0f || size.y <= 0.0f)
                return rect;
            glm::vec2 fitted = size.x / size.y > aspect ? glm::vec2(size.y * aspect, size.y) : glm::vec2(size.x, size.x / aspect);
            return UIRect::FromCenter(rect.GetCenter(), fitted);
        }

        // ---- fill
        static UIQuad RectQuad(const glm::vec2& min, const glm::vec2& max, const glm::vec2& uvMin, const glm::vec2& uvMax)
        {
            UIQuad q;
            q.Position[0] = { min.x, min.y }; q.UV[0] = { uvMin.x, uvMin.y };
            q.Position[1] = { max.x, min.y }; q.UV[1] = { uvMax.x, uvMin.y };
            q.Position[2] = { max.x, max.y }; q.UV[2] = { uvMax.x, uvMax.y };
            q.Position[3] = { min.x, max.y }; q.UV[3] = { uvMin.x, uvMax.y };
            return q;
        }

        // The point of the square -1..1 straight out from its centre at `angle` (radians, 0 = right).
        static glm::vec2 SquareBoundary(float angle)
        {
            glm::vec2 d(std::cos(angle), std::sin(angle));
            return d / std::max(std::abs(d.x), std::abs(d.y));
        }

        void BuildFill(const UIRect& rect, const glm::vec2& uvMin, const glm::vec2& uvMax, UIFillMethod method, int origin,
                       float amount, bool clockwise, std::vector<UIQuad>& out)
        {
            if (!(amount > 0.0f) || rect.IsEmpty())
                return;
            if (amount >= 1.0f)
            {
                out.push_back(RectQuad(rect.Min, rect.Max, uvMin, uvMax));
                return;
            }
            const glm::vec2 size = rect.GetSize(), uvSize = uvMax - uvMin;
            if (method == UIFillMethod::Horizontal || method == UIFillMethod::Vertical)
            {
                const int axis = method == UIFillMethod::Horizontal ? 0 : 1;
                glm::vec2 min = rect.Min, max = rect.Max, a = uvMin, b = uvMax;
                if (origin == 1) // from the right / top
                {
                    min[axis] = rect.Max[axis] - size[axis] * amount;
                    a[axis] = uvMax[axis] - uvSize[axis] * amount;
                }
                else
                {
                    max[axis] = rect.Min[axis] + size[axis] * amount;
                    b[axis] = uvMin[axis] + uvSize[axis] * amount;
                }
                out.push_back(RectQuad(min, max, a, b));
                return;
            }

            // Radial 360: a fan of triangles from the centre to the edge, measured in the rectangle's
            // own -1..1 square (its corners at 45 degrees), so half the amount is half the picture.
            const float twoPi = glm::two_pi<float>();
            const float start = glm::half_pi<float>() - (float)(((origin % 4) + 4) % 4) * glm::half_pi<float>(); // top, right, bottom, left
            const float sign = clockwise ? -1.0f : 1.0f;
            const float sweep = amount * twoPi;
            float cuts[6];
            int count = 0;
            cuts[count++] = 0.0f;
            float corners[4];
            int cornerCount = 0;
            for (int k = 0; k < 4; k++)
            {
                float corner = glm::quarter_pi<float>() + (float)k * glm::half_pi<float>();
                float s = std::fmod(sign * (corner - start), twoPi);
                if (s < 0.0f) s += twoPi;
                if (s > 1e-5f && s < sweep - 1e-5f)
                    corners[cornerCount++] = s;
            }
            std::sort(corners, corners + cornerCount);
            for (int k = 0; k < cornerCount; k++)
                cuts[count++] = corners[k];
            cuts[count++] = sweep;

            const glm::vec2 center = rect.GetCenter(), half = size * 0.5f;
            auto position = [&](const glm::vec2& q) { return center + q * half; };
            auto uv = [&](const glm::vec2& q) { return uvMin + (q * 0.5f + 0.5f) * uvSize; };
            for (int i = 0; i + 1 < count; i++)
            {
                glm::vec2 a = SquareBoundary(start + sign * cuts[i]);
                glm::vec2 b = SquareBoundary(start + sign * cuts[i + 1]);
                if (clockwise)
                    std::swap(a, b); // counter-clockwise corners
                UIQuad q;
                q.Position[0] = center;             q.UV[0] = uv(glm::vec2(0.0f));
                q.Position[1] = position(a);        q.UV[1] = uv(a);
                q.Position[2] = position(b);        q.UV[2] = uv(b);
                q.Position[3] = q.Position[2];      q.UV[3] = q.UV[2];
                out.push_back(q);
            }
        }

        float GetArea(const std::vector<UIQuad>& quads)
        {
            float area = 0.0f;
            for (const UIQuad& q : quads)
            {
                // Shoelace formula over the four corners (a repeated corner adds nothing).
                float twice = 0.0f;
                for (int i = 0; i < 4; i++)
                {
                    const glm::vec2& p = q.Position[i];
                    const glm::vec2& n = q.Position[(i + 1) % 4];
                    twice += p.x * n.y - n.x * p.y;
                }
                area += std::abs(twice) * 0.5f;
            }
            return area;
        }

        UIRect FillRect(const UIRect& rect, UIDirection direction, float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            UIRect r = rect;
            switch (direction)
            {
                case UIDirection::LeftToRight: r.Max.x = rect.Min.x + rect.GetWidth() * t; break;
                case UIDirection::RightToLeft: r.Min.x = rect.Max.x - rect.GetWidth() * t; break;
                case UIDirection::BottomToTop: r.Max.y = rect.Min.y + rect.GetHeight() * t; break;
                case UIDirection::TopToBottom: r.Min.y = rect.Max.y - rect.GetHeight() * t; break;
            }
            return r;
        }

        // ---- values
        float Normalize(float value, float min, float max)
        {
            if (max == min)
                return 0.0f;
            return std::clamp((value - min) / (max - min), 0.0f, 1.0f);
        }

        float Denormalize(float t, float min, float max, bool wholeNumbers)
        {
            float v = min + (max - min) * std::clamp(t, 0.0f, 1.0f);
            return ClampValue(v, min, max, wholeNumbers);
        }

        float ClampValue(float value, float min, float max, bool wholeNumbers)
        {
            float lo = std::min(min, max), hi = std::max(min, max);
            if (wholeNumbers)
            {
                value = std::round(value);
                lo = std::ceil(lo);
                hi = std::floor(hi);
                if (lo > hi) // no whole number inside the range
                    return std::round(std::min(min, max));
            }
            if (!(value == value)) // NaN
                value = lo;
            return std::clamp(value, lo, hi);
        }

        // ---- slider
        float GetSliderHandleSize(const UIRect& rect, UIDirection direction, float handleSize)
        {
            float thickness = IsHorizontal(direction) ? rect.GetHeight() : rect.GetWidth();
            float length = IsHorizontal(direction) ? rect.GetWidth() : rect.GetHeight();
            float size = handleSize > 0.0f ? handleSize : thickness;
            return std::clamp(size, 0.0f, std::max(0.0f, length));
        }

        void GetSliderTrack(const UIRect& rect, UIDirection direction, float handleSize, glm::vec2& start, glm::vec2& end)
        {
            const float half = GetSliderHandleSize(rect, direction, handleSize) * 0.5f;
            const glm::vec2 c = rect.GetCenter();
            if (IsHorizontal(direction))
            {
                start = { rect.Min.x + half, c.y };
                end = { rect.Max.x - half, c.y };
            }
            else
            {
                start = { c.x, rect.Min.y + half };
                end = { c.x, rect.Max.y - half };
            }
            if (IsReversed(direction))
                std::swap(start, end);
        }

        float SliderNormalizedAt(const UIRect& rect, UIDirection direction, float handleSize, const glm::vec2& point)
        {
            glm::vec2 start, end;
            GetSliderTrack(rect, direction, handleSize, start, end);
            glm::vec2 d = end - start;
            float length2 = glm::dot(d, d);
            if (length2 < 1e-6f)
                return 0.0f;
            return std::clamp(glm::dot(point - start, d) / length2, 0.0f, 1.0f);
        }

        glm::vec2 GetSliderHandleCenter(const UIRect& rect, UIDirection direction, float handleSize, float normalized)
        {
            glm::vec2 start, end;
            GetSliderTrack(rect, direction, handleSize, start, end);
            return glm::mix(start, end, std::clamp(normalized, 0.0f, 1.0f));
        }

        // ---- toggle
        UIRect GetToggleBox(const UIRect& rect, float boxSize, UIToggleStyle style)
        {
            float h = rect.GetHeight();
            float size = std::clamp(boxSize > 0.0f ? boxSize : h, 0.0f, std::max(0.0f, h));
            float width = style == UIToggleStyle::Switch ? size * 1.8f : size;
            width = std::min(width, std::max(0.0f, rect.GetWidth()));
            float cy = rect.GetCenter().y;
            return { { rect.Min.x, cy - size * 0.5f }, { rect.Min.x + width, cy + size * 0.5f } };
        }

        UIRect GetToggleLabel(const UIRect& rect, float boxSize, UIToggleStyle style, float spacing)
        {
            UIRect box = GetToggleBox(rect, boxSize, style);
            UIRect label = rect;
            label.Min.x = std::min(rect.Max.x, box.Max.x + std::max(0.0f, spacing));
            return label;
        }

    }

}
