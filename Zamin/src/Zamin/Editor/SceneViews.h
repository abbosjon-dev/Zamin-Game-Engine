#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // The six orthographic axis views of an editor scene view (numpad-like: Num 7 top, Num 1
    // front, Num 3 right, with Ctrl the opposite side), and Free: no axis (any direction, normally
    // perspective). The world is Y-up; the "front" of a scene faces +Z (a camera there looks -Z).
    enum class ViewAxis : uint8_t { Free = 0, Top, Bottom, Front, Back, Right, Left, Count };

    struct ViewAxisInfo
    {
        const char* Key;       // "Top" (files, automation)
        const char* Name;      // "Yuqoridan" (menus)
        const char* Short;     // "Yuqori" (the view's corner label)
        glm::vec3 Forward;     // the direction the camera looks along
        float Pitch, Yaw;      // EditorCamera's angles (radians; a positive pitch looks down)
    };

    const ViewAxisInfo& GetViewAxisInfo(ViewAxis axis);
    // "Top" / "top" / "yuqoridan" ... (any letter case); Free for anything else.
    ViewAxis ViewAxisFromKey(std::string_view key);
    // Top <-> Bottom, Front <-> Back, Right <-> Left (Free stays Free).
    ViewAxis GetOppositeViewAxis(ViewAxis axis);
    // The axis view whose camera looks along `forward` within `toleranceDegrees` (Free: none).
    ViewAxis FindViewAxis(const glm::vec3& forward, float toleranceDegrees = 0.5f);
    // The axis a click on the axis navigator's end of world direction `direction` (+X ... -Z)
    // shows: the view from that side towards the centre (+X: Right, +Y: Top, +Z: Front ...).
    ViewAxis GetViewAxisFromSide(const glm::vec3& direction);
    // The editor grid's plane for a view (0: XZ, 1: XY, 2: YZ). Perspective views and views
    // looking more down than sideways use the ground (XZ; the 2D mode: XY); an orthographic view
    // from the side gets the plane facing it, so its lines do not collapse into one.
    int GetGridPlaneForView(const glm::vec3& forward, bool orthographic, bool mode2D);

    // A fly speed boost that builds up while the camera keeps moving (RMB + WASD held):
    // 1 at the start, growing by `acceleration` per second up to MaxFlyBoost (0: no boost).
    float GetFlyBoost(float movingSeconds, float acceleration);
    inline constexpr float MaxFlyBoost = 4.0f;

    // Which entities show their names in the editor's scene views, and how.
    enum class EntityLabelMode : uint8_t { Off = 0, Selected, All };

    // The names of entities drawn over the scene views (Ko'rinish > Obyekt nomlari): for the
    // selection or every entity, fading out with the distance from the camera, the nearest ones
    // kept where labels would overlap.
    struct EntityLabelSettings
    {
        EntityLabelMode Mode = EntityLabelMode::Off;
        bool Fade = true;
        float FadeStart = 20.0f;      // world units from the camera: fully visible up to here
        float FadeEnd = 60.0f;        // ... gone from here
        bool FadeSelected = false;    // the selection's names fade too (else always fully visible)
        bool HideOverlapping = true;  // a label that would cover a nearer one is left out
        bool Background = true;       // a dark rounded box behind the text
        bool ShowInPlay = false;      // the main viewport's game picture gets them too (play mode)
        float Scale = 1.0f;           // x the UI font
        int MaxLabels = 150;

        static constexpr float MinScale = 0.6f, MaxScale = 2.0f;
        static constexpr int MinLabels = 1, MaxLabelCount = 2000;
        static constexpr float MinFadeRange = 0.01f;

        bool IsEnabled() const { return Mode != EntityLabelMode::Off; }
        // How opaque a label at `distance` world units from the camera is (0..1). Orthographic
        // views pass a negative distance: no fading.
        float GetAlpha(float distance, bool selected) const;
        static const char* GetModeKey(EntityLabelMode mode);   // "Off" / "Selected" / "All"
        static EntityLabelMode ModeFromKey(std::string_view key, EntityLabelMode fallback);
        void Sanitize();
    };

    // A label that could be drawn: its rectangle on screen and what decides its priority.
    struct EntityLabelCandidate
    {
        glm::vec2 Min{ 0.0f }, Max{ 0.0f };   // screen pixels
        float Distance = 0.0f;                // from the camera (orthographic: depth along the view)
        float Alpha = 1.0f;                   // GetAlpha's result
        bool Selected = false;
        bool Hovered = false;
    };

    // The labels to draw, as indices into `candidates`, in drawing order (the farthest first, the
    // selection last - on top). Invisible ones (alpha < 0.02) and those outside [viewMin, viewMax]
    // are left out; with HideOverlapping a label overlapping one already kept is dropped - the
    // selection, then the hovered one, then the nearest are kept first; at most MaxLabels.
    std::vector<size_t> LayoutEntityLabels(std::span<const EntityLabelCandidate> candidates, const EntityLabelSettings& settings,
                                           const glm::vec2& viewMin, const glm::vec2& viewMax);
    // A name shortened to at most `maxCharacters` UTF-8 characters ("..." at the end); empty: "(nomsiz)".
    std::string ShortenEntityLabel(std::string_view name, size_t maxCharacters = 40);

}
