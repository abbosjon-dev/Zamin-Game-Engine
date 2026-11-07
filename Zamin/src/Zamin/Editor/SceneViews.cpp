#include "Zamin/Editor/SceneViews.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

namespace Zamin {

    namespace {
        constexpr float HalfPi = glm::half_pi<float>();
        constexpr float Pi = glm::pi<float>();

        // Pitch / yaw give the forward direction (cos p sin y, -sin p, -cos p cos y).
        const ViewAxisInfo s_Axes[] = {
            { "Free", "Erkin (perspektiv)", "Persp", { 0.0f, 0.0f, -1.0f }, 0.0f, 0.0f },
            { "Top", "Yuqoridan", "Yuqori", { 0.0f, -1.0f, 0.0f }, HalfPi, 0.0f },
            { "Bottom", "Pastdan", "Past", { 0.0f, 1.0f, 0.0f }, -HalfPi, 0.0f },
            { "Front", "Old tomondan", "Old", { 0.0f, 0.0f, -1.0f }, 0.0f, 0.0f },
            { "Back", "Orqa tomondan", "Orqa", { 0.0f, 0.0f, 1.0f }, 0.0f, Pi },
            { "Right", "O'ng tomondan", "O'ng", { -1.0f, 0.0f, 0.0f }, 0.0f, -HalfPi },
            { "Left", "Chap tomondan", "Chap", { 1.0f, 0.0f, 0.0f }, 0.0f, HalfPi },
        };

        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
                if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
                    return false;
            return true;
        }

        bool Overlaps(const EntityLabelCandidate& a, const EntityLabelCandidate& b)
        {
            constexpr float margin = 1.0f;
            return a.Min.x < b.Max.x + margin && b.Min.x < a.Max.x + margin && a.Min.y < b.Max.y + margin && b.Min.y < a.Max.y + margin;
        }
    }

    const ViewAxisInfo& GetViewAxisInfo(ViewAxis axis)
    {
        const size_t index = (size_t)axis;
        return s_Axes[index < std::size(s_Axes) ? index : 0];
    }

    ViewAxis ViewAxisFromKey(std::string_view key)
    {
        for (size_t i = 0; i < std::size(s_Axes); i++)
            if (EqualsIgnoreCase(key, s_Axes[i].Key) || EqualsIgnoreCase(key, s_Axes[i].Name) || EqualsIgnoreCase(key, s_Axes[i].Short))
                return (ViewAxis)i;
        return ViewAxis::Free;
    }

    ViewAxis GetOppositeViewAxis(ViewAxis axis)
    {
        switch (axis)
        {
            case ViewAxis::Top: return ViewAxis::Bottom;
            case ViewAxis::Bottom: return ViewAxis::Top;
            case ViewAxis::Front: return ViewAxis::Back;
            case ViewAxis::Back: return ViewAxis::Front;
            case ViewAxis::Right: return ViewAxis::Left;
            case ViewAxis::Left: return ViewAxis::Right;
            default: return ViewAxis::Free;
        }
    }

    ViewAxis FindViewAxis(const glm::vec3& forward, float toleranceDegrees)
    {
        const float length = glm::length(forward);
        if (!(length > 1e-6f))
            return ViewAxis::Free;
        const glm::vec3 direction = forward / length;
        const float cosTolerance = std::cos(glm::radians(std::clamp(toleranceDegrees, 0.0f, 45.0f)));
        for (size_t i = 1; i < std::size(s_Axes); i++)
            if (glm::dot(direction, s_Axes[i].Forward) >= cosTolerance)
                return (ViewAxis)i;
        return ViewAxis::Free;
    }

    ViewAxis GetViewAxisFromSide(const glm::vec3& direction)
    {
        const glm::vec3 a = glm::abs(direction);
        if (a.x >= a.y && a.x >= a.z && a.x > 0.0f)
            return direction.x > 0.0f ? ViewAxis::Right : ViewAxis::Left;
        if (a.y >= a.z && a.y > 0.0f)
            return direction.y > 0.0f ? ViewAxis::Top : ViewAxis::Bottom;
        if (a.z > 0.0f)
            return direction.z > 0.0f ? ViewAxis::Front : ViewAxis::Back;
        return ViewAxis::Free;
    }

    int GetGridPlaneForView(const glm::vec3& forward, bool orthographic, bool mode2D)
    {
        if (mode2D)
            return 1;
        if (!orthographic)
            return 0;
        const glm::vec3 a = glm::abs(forward);
        if (a.y >= a.x && a.y >= a.z)
            return 0;
        return a.z >= a.x ? 1 : 2;
    }

    float GetFlyBoost(float movingSeconds, float acceleration)
    {
        if (!(acceleration > 0.0f) || !(movingSeconds > 0.0f))
            return 1.0f;
        return std::min(1.0f + acceleration * movingSeconds, MaxFlyBoost);
    }

    // ------------------------------------------------------------------ entity labels
    float EntityLabelSettings::GetAlpha(float distance, bool selected) const
    {
        if (!Fade || distance < 0.0f || (selected && !FadeSelected))
            return 1.0f;
        if (!std::isfinite(distance) || distance >= FadeEnd)
            return 0.0f;
        if (distance <= FadeStart)
            return 1.0f;
        const float t = (distance - FadeStart) / std::max(FadeEnd - FadeStart, MinFadeRange);
        return 1.0f - t * t * (3.0f - 2.0f * t); // smoothstep out
    }

    const char* EntityLabelSettings::GetModeKey(EntityLabelMode mode)
    {
        switch (mode)
        {
            case EntityLabelMode::Selected: return "Selected";
            case EntityLabelMode::All: return "All";
            default: return "Off";
        }
    }

    EntityLabelMode EntityLabelSettings::ModeFromKey(std::string_view key, EntityLabelMode fallback)
    {
        for (EntityLabelMode mode : { EntityLabelMode::Off, EntityLabelMode::Selected, EntityLabelMode::All })
            if (EqualsIgnoreCase(key, GetModeKey(mode)))
                return mode;
        return fallback;
    }

    void EntityLabelSettings::Sanitize()
    {
        if ((int)Mode > (int)EntityLabelMode::All)
            Mode = EntityLabelMode::Off;
        FadeStart = std::isfinite(FadeStart) ? std::clamp(FadeStart, 0.0f, 100000.0f) : 20.0f;
        FadeEnd = std::isfinite(FadeEnd) ? FadeEnd : 60.0f;
        FadeEnd = std::clamp(FadeEnd, FadeStart + MinFadeRange, 100000.0f + MinFadeRange);
        Scale = std::isfinite(Scale) ? std::clamp(Scale, MinScale, MaxScale) : 1.0f;
        MaxLabels = std::clamp(MaxLabels, MinLabels, MaxLabelCount);
    }

    std::vector<size_t> LayoutEntityLabels(std::span<const EntityLabelCandidate> candidates, const EntityLabelSettings& settings,
                                           const glm::vec2& viewMin, const glm::vec2& viewMax)
    {
        std::vector<size_t> order;
        order.reserve(candidates.size());
        for (size_t i = 0; i < candidates.size(); i++)
        {
            const EntityLabelCandidate& c = candidates[i];
            if (!(c.Alpha >= 0.02f))
                continue;
            if (c.Max.x <= viewMin.x || c.Min.x >= viewMax.x || c.Max.y <= viewMin.y || c.Min.y >= viewMax.y)
                continue;
            order.push_back(i);
        }
        // Who keeps its place first: the selection, the hovered one, then the nearest.
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const EntityLabelCandidate& x = candidates[a];
            const EntityLabelCandidate& y = candidates[b];
            if (x.Selected != y.Selected) return x.Selected;
            if (x.Hovered != y.Hovered) return x.Hovered;
            return x.Distance < y.Distance;
        });
        const size_t limit = (size_t)std::max(settings.MaxLabels, 1);
        std::vector<size_t> kept;
        kept.reserve(std::min(order.size(), limit));
        for (size_t index : order)
        {
            if (kept.size() >= limit)
                break;
            if (settings.HideOverlapping &&
                std::any_of(kept.begin(), kept.end(), [&](size_t other) { return Overlaps(candidates[index], candidates[other]); }))
                continue;
            kept.push_back(index);
        }
        // Drawn back to front: the farthest first, the selection (and the hovered one) on top.
        std::stable_sort(kept.begin(), kept.end(), [&](size_t a, size_t b) {
            const EntityLabelCandidate& x = candidates[a];
            const EntityLabelCandidate& y = candidates[b];
            const int px = (x.Selected ? 2 : 0) + (x.Hovered ? 1 : 0), py = (y.Selected ? 2 : 0) + (y.Hovered ? 1 : 0);
            if (px != py) return px < py;
            return x.Distance > y.Distance;
        });
        return kept;
    }

    std::string ShortenEntityLabel(std::string_view name, size_t maxCharacters)
    {
        while (!name.empty() && std::isspace((unsigned char)name.front()))
            name.remove_prefix(1);
        while (!name.empty() && std::isspace((unsigned char)name.back()))
            name.remove_suffix(1);
        if (name.empty())
            return "(nomsiz)";
        maxCharacters = std::max<size_t>(maxCharacters, 4);
        // UTF-8: a character starts at every byte that is not a continuation byte (10xxxxxx).
        size_t characters = 0, cut = std::string_view::npos;
        for (size_t i = 0; i < name.size(); i++)
        {
            if (((unsigned char)name[i] & 0xC0) == 0x80)
                continue;
            if (characters == maxCharacters - 3)
                cut = i;
            characters++;
        }
        if (characters <= maxCharacters || cut == std::string_view::npos)
            return std::string(name);
        return std::string(name.substr(0, cut)) + "...";
    }

}
