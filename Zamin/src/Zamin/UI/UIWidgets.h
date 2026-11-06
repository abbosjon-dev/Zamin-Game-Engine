#pragma once

// Screen-space UI: rectangles and the geometry of the widgets (images, sliders, toggles,
// progress bars). Pure math - no GPU - so it is shared by the scene's UI pass, its mouse
// handling and the tests.

#include <glm/glm.hpp>

#include <vector>

namespace Zamin {

    // A UI rectangle in pixels, origin at the bottom-left of the view (y up).
    struct UIRect
    {
        glm::vec2 Min{ 0.0f }, Max{ 0.0f };

        static UIRect FromCenter(const glm::vec2& center, const glm::vec2& size)
        {
            glm::vec2 half = glm::abs(size) * 0.5f;
            return { center - half, center + half };
        }
        glm::vec2 GetSize() const { return Max - Min; }
        glm::vec2 GetCenter() const { return (Min + Max) * 0.5f; }
        float GetWidth() const { return Max.x - Min.x; }
        float GetHeight() const { return Max.y - Min.y; }
        bool Contains(const glm::vec2& p) const { return p.x >= Min.x && p.x <= Max.x && p.y >= Min.y && p.y <= Max.y; }
        bool IsEmpty() const { return Max.x <= Min.x || Max.y <= Min.y; }
        // Shrunk by the amounts in px (negative: grown); never turned inside out (it collapses to its centre line).
        UIRect Inset(float left, float right, float top, float bottom) const;
        UIRect Inset(float amount) const { return Inset(amount, amount, amount, amount); }
        // The part both rectangles cover (empty: Min == Max somewhere inside them).
        UIRect Intersect(const UIRect& other) const;
    };

    // The way a slider or a progress bar grows from its minimum to its maximum.
    enum class UIDirection { LeftToRight = 0, RightToLeft, BottomToTop, TopToBottom };

    // How a UI Image fills its rectangle:
    // - Simple: the picture stretched over it (Preserve aspect: fitted inside, centred);
    // - Sliced: 9-slice - the corners keep their size (border px x border scale), the edges and the
    //   centre stretch: panels, buttons, frames;
    // - Tiled: the edges and the centre repeat at their own size;
    // - Filled: a part of the picture - FillAmount 0..1 of it, horizontally, vertically or as a
    //   clock hand around its centre (Radial 360): bars, cooldowns, timers.
    enum class UIImageType { Simple = 0, Sliced, Tiled, Filled };
    enum class UIFillMethod { Horizontal = 0, Vertical, Radial360 };

    // Where a Filled image starts filling (FillOrigin): Horizontal 0 = left, 1 = right; Vertical
    // 0 = bottom, 1 = top; Radial 360: 0 = top, 1 = right, 2 = bottom, 3 = left.
    // A quad with four corners of its own (counter-clockwise; a triangle repeats its last corner)
    // and their texture coordinates.
    struct UIQuad
    {
        glm::vec2 Position[4]{};
        glm::vec2 UV[4]{};
    };

    // Toggle look: a box with a check mark / a filled square / a dot, or a sliding switch.
    enum class UIToggleStyle { Checkbox = 0, Switch };
    enum class UIToggleMark { Check = 0, Square, Dot };

    namespace UIMath
    {
        bool IsHorizontal(UIDirection direction);
        // Right to left / top to bottom: the minimum is at the far end.
        bool IsReversed(UIDirection direction);

        // The largest rectangle of the aspect (width / height) inside `rect`, centred.
        UIRect FitAspect(const UIRect& rect, float aspect);

        // The quads of a Filled image: `amount` (0..1) of `rect` (whose picture is uvMin..uvMax),
        // starting at `origin` (see UIImageType); Radial 360 sweeps clockwise or not. Appends
        // nothing for amount <= 0, the whole rectangle as one quad for amount >= 1.
        void BuildFill(const UIRect& rect, const glm::vec2& uvMin, const glm::vec2& uvMax, UIFillMethod method, int origin,
                       float amount, bool clockwise, std::vector<UIQuad>& out);
        // The area the quads cover (px^2) - for checks.
        float GetArea(const std::vector<UIQuad>& quads);

        // The part `t` (0..1) of `rect` from its start in `direction` (progress bar / slider fill).
        UIRect FillRect(const UIRect& rect, UIDirection direction, float t);

        // ---- values (sliders, progress bars). min > max works too (the range runs backwards).
        // value -> 0..1 of the range (min == max: 0).
        float Normalize(float value, float min, float max);
        // 0..1 -> a value of the range (rounded to a whole number when `wholeNumbers`).
        float Denormalize(float t, float min, float max, bool wholeNumbers);
        // Clamped into the range (and rounded when `wholeNumbers`).
        float ClampValue(float value, float min, float max, bool wholeNumbers);

        // ---- slider: its handle (handleSize px across, 0 = the rect's thickness) travels between
        // `start` and `end` (the minimum and the maximum), inside the rect.
        float GetSliderHandleSize(const UIRect& rect, UIDirection direction, float handleSize);
        void GetSliderTrack(const UIRect& rect, UIDirection direction, float handleSize, glm::vec2& start, glm::vec2& end);
        // 0..1 along the track for a point (its projection, clamped).
        float SliderNormalizedAt(const UIRect& rect, UIDirection direction, float handleSize, const glm::vec2& point);
        glm::vec2 GetSliderHandleCenter(const UIRect& rect, UIDirection direction, float handleSize, float normalized);

        // ---- toggle: the box (boxSize px, 0 = the rect's height) at the left, the label right of it.
        UIRect GetToggleBox(const UIRect& rect, float boxSize, UIToggleStyle style);
        UIRect GetToggleLabel(const UIRect& rect, float boxSize, UIToggleStyle style, float spacing);
    }

}
