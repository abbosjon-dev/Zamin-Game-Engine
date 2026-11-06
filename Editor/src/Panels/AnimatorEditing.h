#pragma once

#include <Zamin/Zamin.h>

#include <glm/glm.hpp>

#include <string>

namespace Zamin {

    // Editing pieces of an Animator's state machine shared by the inspector and the Animator graph
    // window: parameters, a state (clip or 1D blend tree), a transition (fade, exit time,
    // conditions), adding and removing states. Each Draw* returns true when it changed something.
    namespace AnimatorEditing {

        // The name of state `index` (-1: "any state").
        std::string StateName(const AnimatorComponent& c, int index);
        // A combo of the model's clips (none: an empty clip allowed under that label).
        bool ClipCombo(const char* id, std::string& clip, const Model* model, const char* none = nullptr);
        // A combo of the float parameters ("" allowed as `none`).
        bool FloatParameterCombo(const char* id, std::string& parameter, const AnimatorComponent& c, const char* none);

        // Parameter rows. live: the game runs - values are what matters (a trigger is a button and
        // shows while it is pending); the list itself can still be edited.
        bool DrawParameters(AnimatorComponent& c, bool live);
        bool DrawState(AnimatorComponent& c, int index, const Model* model);
        bool DrawTransition(AnimatorComponent& c, int index);

        // A new state (a clip of the model, or a blend tree over its first clips) at `position`
        // (graph units); returns its index.
        int AddState(AnimatorComponent& c, const Model* model, bool blendTree, const glm::vec2& position);
        // Removes a state and its transitions (the other transitions' indices follow).
        void RemoveState(AnimatorComponent& c, int index);

        // The live state, one line each: the state and its normalized time, the crossfade, blend
        // weights, layer weights (inside a property grid when `grid`).
        void DrawLiveStatus(const AnimatorComponent& c, const Model* model, bool grid);
    }

}
