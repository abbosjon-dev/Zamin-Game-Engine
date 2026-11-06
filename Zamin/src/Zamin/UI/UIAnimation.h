#pragma once

// UI animations: tweens of a UI element's look (scale, alpha, offset - drawn around its pivot,
// with its UI children, without changing the layout), its layout (position, size) or the colour
// of its picture; the show / hide presets of UIAnimationComponent and the hover / press scale
// feedback of its widget. They run on the UI's own time (also while the game is paused). Pure
// math here - the scene runs them (Scene::TweenUI, ShowUI, HideUI).

#include "Zamin/Math/Easing.h"

#include <glm/glm.hpp>

#include <functional>
#include <optional>

namespace Zamin {

    // What a UI tween animates: Scale / Alpha / Offset - the UI Transform's look (Scale, Alpha,
    // VisualOffset); Position / Size - its layout; Color - its picture's colour (UI Image, else
    // Sprite Renderer, Circle Renderer, Text). Values go in a vec4: xy for 2D ones, x for Alpha.
    enum class UITweenProperty { Scale = 0, Alpha, Offset, Position, Size, Color };
    // None: once. Loop: again from the start. PingPong: there and back (each way one round).
    enum class UITweenLoop { None = 0, Loop, PingPong };

    struct UITweenOptions
    {
        Ease Easing = Ease::OutQuad;
        float Delay = 0.0f;                 // seconds before it starts (its start value is read then)
        UITweenLoop Loop = UITweenLoop::None;
        int Loops = -1;                     // Loop / PingPong: how many rounds (-1: forever)
        std::optional<glm::vec4> From;      // else the property's value when it starts
        std::function<void()> OnComplete;   // after the last round (not when it is cancelled)
    };

    // The show / hide presets: Fade (alpha), Pop (scale up from 60 % while it fades in), Slide*
    // (comes in from / goes out to that side, SlideDistance UI units, fading).
    enum class UIAnimPreset { None = 0, Fade, Pop, SlideLeft, SlideRight, SlideUp, SlideDown };

    namespace UIAnimMath {

        // The look a UI element is drawn with (UITransformComponent's Scale, VisualOffset, Alpha).
        struct Look
        {
            glm::vec2 Scale{ 1.0f };
            glm::vec2 Offset{ 0.0f };
            float Alpha = 1.0f;
        };
        // Where a show of `preset` starts from (and a hide ends at), for an element at rest at `rest`.
        Look HiddenLook(UIAnimPreset preset, const Look& rest, float slideDistance);

        // How far into its current round a tween is at `elapsed` seconds (after its delay): 0..1,
        // already turned around on the way back of a ping-pong. `finished`: the last round is over
        // (the result is then where it ends: 1, or 0 after an even number of ping-pong rounds).
        float Progress(float elapsed, float duration, UITweenLoop loop, int loops, bool& finished);

        // The scale a widget's feedback heads for: pressed beats hovered beats rest (1).
        inline float FeedbackTarget(bool hovered, bool pressed, float hoverScale, float pressScale)
        {
            return pressed ? pressScale : hovered ? hoverScale : 1.0f;
        }

    }

}
