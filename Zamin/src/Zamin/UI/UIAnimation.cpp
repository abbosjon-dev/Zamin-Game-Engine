#include "Zamin/UI/UIAnimation.h"

#include <algorithm>
#include <cmath>

namespace Zamin::UIAnimMath {

    Look HiddenLook(UIAnimPreset preset, const Look& rest, float slideDistance)
    {
        Look look = rest;
        switch (preset)
        {
            case UIAnimPreset::None:
                return rest;
            case UIAnimPreset::Fade:
                break;
            case UIAnimPreset::Pop:
                look.Scale = rest.Scale * 0.6f;
                break;
            case UIAnimPreset::SlideLeft:  look.Offset.x -= slideDistance; break;
            case UIAnimPreset::SlideRight: look.Offset.x += slideDistance; break;
            case UIAnimPreset::SlideUp:    look.Offset.y += slideDistance; break;
            case UIAnimPreset::SlideDown:  look.Offset.y -= slideDistance; break;
        }
        look.Alpha = 0.0f;
        return look;
    }

    float Progress(float elapsed, float duration, UITweenLoop loop, int loops, bool& finished)
    {
        finished = false;
        elapsed = std::max(0.0f, elapsed);
        if (duration <= 1e-6f)
        {
            finished = loop == UITweenLoop::None || loops >= 0;
            return loop == UITweenLoop::PingPong && loops >= 0 && loops % 2 == 0 ? 0.0f : 1.0f;
        }
        if (loop == UITweenLoop::None)
        {
            if (elapsed >= duration)
            {
                finished = true;
                return 1.0f;
            }
            return elapsed / duration;
        }
        const float rounds = elapsed / duration;
        if (loops >= 0 && rounds >= (float)loops)
        {
            finished = true;
            if (loops == 0)
                return 0.0f;
            return loop == UITweenLoop::PingPong && loops % 2 == 0 ? 0.0f : 1.0f;
        }
        const float round = std::floor(rounds);
        const float phase = rounds - round;
        if (loop == UITweenLoop::PingPong && ((long long)round % 2) == 1)
            return 1.0f - phase;
        return phase;
    }

}
