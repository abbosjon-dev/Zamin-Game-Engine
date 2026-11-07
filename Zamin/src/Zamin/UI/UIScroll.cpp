#include "Zamin/UI/UIScroll.h"
#include "Zamin/Math/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace Zamin::UIScroll {

    glm::vec2 CalcOffset(const UIRect& view, const UIRect& content, bool horizontal, bool vertical)
    {
        glm::vec2 offset(0.0f);
        if (horizontal)
        {
            if (content.GetWidth() <= view.GetWidth())
                offset.x = view.Min.x - content.Min.x; // fits: its left edge at the view's
            else if (content.Min.x > view.Min.x)
                offset.x = view.Min.x - content.Min.x; // a gap at the left
            else if (content.Max.x < view.Max.x)
                offset.x = view.Max.x - content.Max.x; // a gap at the right
        }
        if (vertical)
        {
            if (content.GetHeight() <= view.GetHeight())
                offset.y = view.Max.y - content.Max.y; // fits: its top at the view's
            else if (content.Max.y < view.Max.y)
                offset.y = view.Max.y - content.Max.y; // a gap at the top
            else if (content.Min.y > view.Min.y)
                offset.y = view.Min.y - content.Min.y; // a gap at the bottom
        }
        return offset;
    }

    glm::vec2 GetNormalizedPosition(const UIRect& view, const UIRect& content)
    {
        glm::vec2 n(0.0f, 1.0f);
        const float rangeX = content.GetWidth() - view.GetWidth();
        if (rangeX > 1e-4f)
            n.x = (view.Min.x - content.Min.x) / rangeX;
        const float rangeY = content.GetHeight() - view.GetHeight();
        if (rangeY > 1e-4f)
            n.y = 1.0f - (content.Max.y - view.Max.y) / rangeY;
        return n;
    }

    glm::vec2 MoveToNormalized(const UIRect& view, const UIRect& content, const glm::vec2& normalized, const glm::bvec2& axes)
    {
        glm::vec2 delta(0.0f);
        if (axes.x)
        {
            const float range = std::max(0.0f, content.GetWidth() - view.GetWidth());
            delta.x = (view.Min.x - normalized.x * range) - content.Min.x;
        }
        if (axes.y)
        {
            const float range = std::max(0.0f, content.GetHeight() - view.GetHeight());
            delta.y = (view.Max.y + (1.0f - normalized.y) * range) - content.Max.y;
        }
        return delta;
    }

    float RubberDelta(float overStretch, float viewSize)
    {
        if (viewSize <= 0.0f || overStretch == 0.0f)
            return 0.0f;
        const float t = 1.0f - 1.0f / (std::abs(overStretch) * 0.55f / viewSize + 1.0f);
        return t * viewSize * (overStretch < 0.0f ? -1.0f : 1.0f);
    }

    float SmoothDamp(float current, float target, float& velocity, float smoothTime, float dt)
    {
        return Math::SmoothDamp(current, target, velocity, smoothTime, dt);
    }

    float Decelerate(float velocity, float rate, float dt)
    {
        velocity *= std::pow(std::clamp(rate, 0.0f, 1.0f), std::max(0.0f, dt));
        return std::abs(velocity) < 1.0f ? 0.0f : velocity;
    }

    glm::vec2 RevealDelta(const UIRect& view, const UIRect& child, float margin, const glm::bvec2& axes)
    {
        const UIRect c = child.Inset(-margin);
        glm::vec2 delta(0.0f);
        if (axes.x)
        {
            if (c.GetWidth() > view.GetWidth() || c.Min.x < view.Min.x)
                delta.x = view.Min.x - c.Min.x;
            else if (c.Max.x > view.Max.x)
                delta.x = view.Max.x - c.Max.x;
        }
        if (axes.y)
        {
            if (c.GetHeight() > view.GetHeight() || c.Max.y > view.Max.y)
                delta.y = view.Max.y - c.Max.y;
            else if (c.Min.y < view.Min.y)
                delta.y = view.Min.y - c.Min.y;
        }
        return delta;
    }

    UIRect GetScrollbarTrack(const UIRect& rect, bool vertical, float width, float inset, bool bothVisible)
    {
        width = std::max(0.0f, width);
        inset = std::max(0.0f, inset);
        const float corner = bothVisible ? width + inset : 0.0f;
        UIRect track;
        if (vertical)
            track = { { rect.Max.x - inset - width, rect.Min.y + inset + corner }, { rect.Max.x - inset, rect.Max.y - inset } };
        else
            track = { { rect.Min.x + inset, rect.Min.y + inset }, { rect.Max.x - inset - corner, rect.Min.y + inset + width } };
        track.Max = glm::max(track.Max, track.Min);
        return track;
    }

    UIRect GetChildArea(const UIRect& rect, bool showHorizontal, bool showVertical, float width, float inset)
    {
        const float strip = std::max(0.0f, width) + std::max(0.0f, inset) * 2.0f;
        return rect.Inset(0.0f, showVertical ? strip : 0.0f, 0.0f, showHorizontal ? strip : 0.0f);
    }

    float GetHandleLength(float trackLength, float viewSize, float contentSize, float minLength)
    {
        if (trackLength <= 0.0f)
            return 0.0f;
        const float ratio = contentSize > 0.0f ? std::clamp(viewSize / contentSize, 0.0f, 1.0f) : 1.0f;
        return std::clamp(trackLength * ratio, std::min(minLength, trackLength), trackLength);
    }

    UIRect GetScrollbarHandle(const UIRect& track, bool vertical, float handleLength, float normalized)
    {
        const float n = std::clamp(normalized, 0.0f, 1.0f);
        if (vertical)
        {
            const float travel = std::max(0.0f, track.GetHeight() - handleLength);
            const float top = track.Max.y - (1.0f - n) * travel;
            return { { track.Min.x, top - handleLength }, { track.Max.x, top } };
        }
        const float travel = std::max(0.0f, track.GetWidth() - handleLength);
        const float left = track.Min.x + n * travel;
        return { { left, track.Min.y }, { left + handleLength, track.Max.y } };
    }

    float ScrollbarValueAt(const UIRect& track, bool vertical, float handleLength, const glm::vec2& point, float grab)
    {
        if (vertical)
        {
            const float travel = track.GetHeight() - handleLength;
            if (travel <= 0.0f)
                return 1.0f;
            const float top = point.y - grab + handleLength * 0.5f;
            return std::clamp(1.0f - (track.Max.y - top) / travel, 0.0f, 1.0f);
        }
        const float travel = track.GetWidth() - handleLength;
        if (travel <= 0.0f)
            return 0.0f;
        const float left = point.x - grab - handleLength * 0.5f;
        return std::clamp((left - track.Min.x) / travel, 0.0f, 1.0f);
    }

    UIRect MaskClip(const UIRect& rect, const glm::vec4& padding, bool clipped, const UIRect& outer)
    {
        UIRect clip = rect.Inset(padding.x, padding.y, padding.z, padding.w);
        if (clipped)
            clip = clip.Intersect(outer);
        return clip;
    }

}
