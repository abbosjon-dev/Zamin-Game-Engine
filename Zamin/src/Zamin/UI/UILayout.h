#pragma once

// Screen-space UI layout: an element's rectangle inside its parent's (anchors, pivot, offsets -
// the RectTransform model), the scale a canvas gives its UI for the screen size, the automatic
// arrangement of a layout group's children (rows, columns, grids) and the nearest element in a
// direction (keyboard / gamepad navigation). Pure math - no scene, no GPU - so it is shared by
// the scene's UI pass, the editor and the tests.

#include "Zamin/UI/UIWidgets.h"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace Zamin {

    // How a canvas sizes its UI for the screen:
    // - ConstantPixelSize: 1 UI unit = ScaleFactor screen pixels, whatever the screen;
    // - ScaleWithScreenSize: the UI is made for ReferenceResolution and grows / shrinks with the
    //   screen - matching its width, its height or a blend of both (MatchWidthOrHeight), or so it
    //   always fits inside (Expand) / always fills it (Shrink).
    enum class UIScaleMode { ConstantPixelSize = 0, ScaleWithScreenSize };
    enum class UIScreenMatchMode { MatchWidthOrHeight = 0, Expand, Shrink };

    // A layout group arranges its children in a row (Horizontal), a column (Vertical) or a grid.
    enum class UILayoutType { Horizontal = 0, Vertical, Grid };
    // Grid: as many columns as fit (Flexible), or a fixed number of columns / rows.
    enum class UIGridConstraint { Flexible = 0, FixedColumnCount, FixedRowCount };

    // The settings of a layout group (UILayoutGroupComponent). ChildAlignment and the anchor
    // presets use the same 9 points in reading order: 0 top-left, 1 top, 2 top-right, 3 left,
    // 4 centre, 5 right, 6 bottom-left, 7 bottom, 8 bottom-right.
    struct UILayoutGroupSettings
    {
        UILayoutType Type = UILayoutType::Vertical;
        glm::vec4 Padding{ 0.0f };          // px inside the group's edges: left, right, top, bottom
        float Spacing = 8.0f;               // px between children (rows / columns)
        int ChildAlignment = 0;             // where the children sit when they do not fill the group
        bool ReverseArrangement = false;    // rows / columns: the last child first
        // Rows / columns: the group sets the children's width / height (from their min / preferred /
        // flexible sizes - UILayoutElement - else their own size); otherwise they keep their own.
        bool ControlChildWidth = false, ControlChildHeight = false;
        // ... and the children share the space left over (flexible at least 1).
        bool ChildForceExpandWidth = false, ChildForceExpandHeight = false;
        // Grid: every child is CellSize big, GridSpacing apart, filled from StartCorner along rows
        // (or columns: StartAxisVertical).
        glm::vec2 CellSize{ 100.0f, 100.0f };
        glm::vec2 GridSpacing{ 8.0f, 8.0f };
        int StartCorner = 0;                // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
        bool StartAxisVertical = false;
        UIGridConstraint Constraint = UIGridConstraint::Flexible;
        int ConstraintCount = 2;
        // The group's own width / height follows its content (its preferred size), around its pivot.
        bool FitWidth = false, FitHeight = false;
    };

    // A child of a layout group as the group sees it.
    struct UILayoutItem
    {
        glm::vec2 Size{ 0.0f };      // its own size (kept when the group does not control it)
        glm::vec2 Min{ 0.0f };       // the least it can be
        glm::vec2 Preferred{ 0.0f }; // what it would like to be
        glm::vec2 Flexible{ 0.0f };  // its share of the space left over (0: none)
    };

    // What a layout group needs for its children (padding and spacing included): it is the
    // group's own min / preferred / flexible size when it is a child of another group.
    struct UILayoutSizes
    {
        glm::vec2 Min{ 0.0f }, Preferred{ 0.0f }, Flexible{ 0.0f };
    };

    // Keyboard / gamepad navigation directions.
    enum class UINavDirection { Up = 0, Down, Left, Right };

    namespace UILayout
    {
        // ---- rectangles. Anchors are 0..1 of the parent's rectangle (0,0 bottom-left, 1,1
        // top-right): equal ones pin the element to a point of the parent (Position: px from it to
        // the element's pivot, Size: its size), different ones stretch it with the parent (along
        // that axis Size is added to the anchors' span: 0 = exactly it, -20 = 10 px in on each side
        // with the pivot in the middle). Pivot: 0..1 of the element's own rectangle.
        UIRect ComputeRect(const UIRect& parent, const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& pivot,
                           const glm::vec2& position, const glm::vec2& size);
        // The Position / Size that give `rect` inside `parent` with these anchors and pivot.
        void FromRect(const UIRect& parent, const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& pivot,
                      const UIRect& rect, glm::vec2& position, glm::vec2& size);
        // The point (0..1, y up) of one of the 9 presets (0 top-left ... 8 bottom-right).
        glm::vec2 PresetPoint(int preset);
        // A child's alignment along x (0 left .. 1 right) and y (0 top .. 1 bottom) for ChildAlignment.
        glm::vec2 AlignmentOf(int alignment);

        // ---- canvas scale: screen pixels per UI unit (>= 0.01; 1 for an empty screen).
        float CanvasScale(const glm::vec2& screen, UIScaleMode mode, float scaleFactor, const glm::vec2& referenceResolution,
                          UIScreenMatchMode matchMode, float matchWidthOrHeight);

        // ---- layout groups. `groupSize`: the group's current size (a flexible grid needs its width
        // to know how many rows it has).
        UILayoutSizes CalcSizes(const UILayoutGroupSettings& group, std::span<const UILayoutItem> items, const glm::vec2& groupSize);
        // The children's rectangles inside the group's (one per item, in item order).
        void Arrange(const UIRect& rect, const UILayoutGroupSettings& group, std::span<const UILayoutItem> items, std::vector<UIRect>& out);

        // ---- navigation: the candidate the move from `from` in `direction` reaches - the nearest
        // one ahead (straight ahead counts more than off to the side); -1 when none is ahead.
        int FindInDirection(const UIRect& from, UINavDirection direction, std::span<const UIRect> candidates);
        glm::vec2 DirectionVector(UINavDirection direction);
    }

}
