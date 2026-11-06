#pragma once

// What the UI tells scripts about the mouse (ScriptableEntity::OnPointerEnter / Exit / Down / Up /
// Click and the "UIPointer..." events of the scene's event bus).

#include "Zamin/Scene/Entity.h"

#include <glm/glm.hpp>

namespace Zamin {

    struct UIPointerEvent
    {
        // The widget under the mouse: the entity itself, or - for Enter / Exit of a parent - the
        // child the mouse is over (or was, for Exit).
        Entity Target;
        glm::vec2 Position{ 0.0f };       // the mouse in the receiver's UI units (origin bottom-left of the view)
        glm::vec2 ScreenPosition{ 0.0f }; // the mouse in view pixels (origin bottom-left)
        bool Inside = true;               // Up: released over the entity it was pressed on (then a click follows)
    };

}
