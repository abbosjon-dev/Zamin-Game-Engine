#pragma once

// Scroll views and masks: how far a content rectangle may move inside its view (its bounds, the
// rubber band past an edge, the spring back, inertia), where it is (the normalized position), the
// move that shows a child, the scrollbars' geometry and the clip rectangles masks give their UI.
// Pure math - no scene, no GPU - so it is shared by the scene's UI, the editor and the tests.

#include "Zamin/UI/UIWidgets.h"

#include <glm/glm.hpp>

namespace Zamin {

    // How a scroll view's content may leave its bounds: Unrestricted - anywhere; Elastic - pulled
    // past an edge (drag, inertia) it resists more and more and springs back when let go; Clamped -
    // never past an edge.
    enum class UIScrollMovement { Unrestricted = 0, Elastic, Clamped };
    // When a scroll view draws its scrollbars (for the axes it scrolls): always, only while its
    // content does not fit in the view, never.
    enum class UIScrollbarVisibility { Permanent = 0, AutoHide, Hidden };

    namespace UIScroll
    {
        // The move (UI units) that brings `content` back into its bounds in `view` along the axes
        // that scroll (0 where it is in them). Content bigger than the view must cover it (no gap
        // at an edge); smaller content sits with its top edge (vertical) / left edge (horizontal)
        // at the view's.
        glm::vec2 CalcOffset(const UIRect& view, const UIRect& content, bool horizontal, bool vertical);
        // Where the content is, 0..1 per axis: x 0 = its left end shown .. 1 = its right end; y 1 =
        // its top shown .. 0 = its bottom (outside 0..1 while pulled past an edge). Content that
        // fits along an axis: x 0, y 1.
        glm::vec2 GetNormalizedPosition(const UIRect& view, const UIRect& content);
        // The move that puts the content at `normalized` (only along the axes of `axes`).
        glm::vec2 MoveToNormalized(const UIRect& view, const UIRect& content, const glm::vec2& normalized, const glm::bvec2& axes);
        // The rubber band: how far content pulled `overStretch` past an edge of a view `viewSize`
        // long is drawn past it - less, and ever less for more (never a whole view).
        float RubberDelta(float overStretch, float viewSize);
        // A critically damped spring from `current` towards `target` (Unity's SmoothDamp): the new
        // value after dt seconds; `velocity` is kept between the calls. It never overshoots.
        float SmoothDamp(float current, float target, float& velocity, float smoothTime, float dt);
        // Inertia: the speed after dt seconds when `rate` of it is kept after a second (0 = it stops
        // at once, 1 = it never slows); below 1 unit per second it stops.
        float Decelerate(float velocity, float rate, float dt);
        // The least move of the content that shows all of `child` (with `margin` around it) in the
        // view, along the axes of `axes` (0: it is shown already). A child bigger than the view
        // shows its top / left edge.
        glm::vec2 RevealDelta(const UIRect& view, const UIRect& child, float margin, const glm::bvec2& axes);

        // ---- scrollbars, inside the scroll view's rectangle: the vertical one along its right
        // edge, the horizontal one along its bottom, `width` thick and `inset` in from the edges
        // (with both, each ends before the corner of the other).
        UIRect GetScrollbarTrack(const UIRect& rect, bool vertical, float width, float inset, bool bothVisible);
        // The space the scroll view's children are laid out in (and the view, without a viewport of
        // its own): its rectangle without the strips of the scrollbars that are shown.
        UIRect GetChildArea(const UIRect& rect, bool showHorizontal, bool showVertical, float width, float inset);
        // The handle's length along a track `trackLength` long: the part of the content the view
        // shows (at least minLength, at most the whole track).
        float GetHandleLength(float trackLength, float viewSize, float contentSize, float minLength);
        // The handle on the track for a normalized position (vertical: 1 = at the top).
        UIRect GetScrollbarHandle(const UIRect& track, bool vertical, float handleLength, float normalized);
        // The normalized position that puts the handle's centre at `point` (minus `grab`: where on
        // the handle it was taken, from its centre), clamped to 0..1.
        float ScrollbarValueAt(const UIRect& track, bool vertical, float handleLength, const glm::vec2& point, float grab);

        // ---- masks: the clip a mask gives its UI subtree - its rectangle `padding` px in from its
        // edges (left, right, top, bottom), cut by the clip it is in itself (`outer`, when `clipped`).
        UIRect MaskClip(const UIRect& rect, const glm::vec4& padding, bool clipped, const UIRect& outer);
    }

}
