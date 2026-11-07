#pragma once

#include "Zamin/Physics/PhysicsTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ZM_DEBUG_DRAW: 1 = DebugDraw works (editor, Development builds - the default), 0 = every call
// is an empty inline function (Shipping builds: ZAMIN_SHIPPING), so scripts can leave their
// debug drawing in. The CMake options ZAMIN_DEBUG_DRAW / ZAMIN_SHIPPING set it for the engine and
// the game modules alike.
#if !defined(ZM_DEBUG_DRAW)
    #if defined(ZAMIN_SHIPPING)
        #define ZM_DEBUG_DRAW 0
    #else
        #define ZM_DEBUG_DRAW 1
    #endif
#endif

namespace Zamin {

    struct AABB;

    // Ready-made colours for debug drawing (any RGBA works; alpha = opacity).
    namespace DebugColor {
        inline const glm::vec4 White{ 1.0f, 1.0f, 1.0f, 1.0f };
        inline const glm::vec4 Red{ 1.0f, 0.27f, 0.23f, 1.0f };
        inline const glm::vec4 Green{ 0.3f, 0.95f, 0.4f, 1.0f };
        inline const glm::vec4 Blue{ 0.3f, 0.55f, 1.0f, 1.0f };
        inline const glm::vec4 Yellow{ 1.0f, 0.88f, 0.2f, 1.0f };
        inline const glm::vec4 Cyan{ 0.25f, 0.9f, 1.0f, 1.0f };
        inline const glm::vec4 Magenta{ 1.0f, 0.35f, 0.9f, 1.0f };
        inline const glm::vec4 Orange{ 1.0f, 0.58f, 0.15f, 1.0f };
        inline const glm::vec4 Gray{ 0.6f, 0.6f, 0.62f, 1.0f };
    }

    // How debug drawing behaves (the editor's viewport menu, the project's settings for the player).
    struct DebugDrawSettings
    {
        // Off: calls queue nothing and nothing is drawn (the runtime toggle; the player's
        // --no-debug-draw flag).
        bool Enabled = true;
        // Every Scene::Raycast2D / Raycast3D query (scripts: Raycast2D / Raycast3D): the ray up to
        // the hit point (green) and on to its full length (faint), the hit point and the surface
        // normal there; a miss in red. Raycast2DAll: every hit, numbered. The 2D overlap queries
        // (OverlapCircle2D / OverlapBox2D / OverlapPoint2D): their shape and what they found.
        // Shown for RaycastDuration seconds of game time (0: the frame of the query).
        bool ShowRaycasts = false;
        float RaycastDuration = 0.0f;
        // The physics contact points (2D and 3D bodies, 3D characters) with their normals, every
        // frame while the game runs / simulates: orange (characters' green), dimmed while both
        // bodies sleep (sleeping 3D bodies keep the contacts they had), the overlap as a red line
        // into the surface and, for a moment, a ring where a 3D body hit hard. ShowContactDetails:
        // a label per touching pair - how deep the shapes overlap, the 3D impact's speed and impulse
        // (2D: Box2D's normal impulse).
        bool ShowContacts = false;
        bool ShowContactDetails = false;
        // The running joints (2D and 3D), every frame while the game runs / simulates: both anchors
        // with a line between them (it shows how far the joint gives), lines to the bodies' centres,
        // the hinge / slider / cone axis; coloured by how close it is to breaking (green - yellow -
        // red; cyan: unbreakable), a broken one crossed out in red. With ShowContactDetails: the force
        // and torque it holds.
        bool ShowJoints = false;
        // Length of the normals drawn at hit points and contact points (world units).
        float NormalLength = 0.4f;
        // Velocity arrows while the game runs / simulates, every frame: ShowVelocities - the 3D
        // character controllers (their velocity after the collisions, from the middle of the
        // capsule, with the speed written at the tip; fainter, the velocity the script asked for
        // when it differs); ShowBodyVelocities - the moving dynamic rigid bodies (2D and 3D). An
        // arrow is VelocityScale seconds of the movement long (velocity x scale).
        bool ShowVelocities = false;
        bool ShowBodyVelocities = false;
        float VelocityScale = 0.25f;
        // Navigation while the game runs, every frame (Scene::DrawNavigationDebug): the baked NavGrid
        // 2D / 3D cells (blocked red, carved by Nav Obstacles orange; ShowNavigationWalkable: the
        // walkable ones green too) and the grid's frame, the Nav Agents' paths, destinations and
        // feelers, the Nav Obstacles' shapes, the Sight Sensors' cones and the Patrol Paths' rounds.
        // The player's --debug-nav; in the editor the viewport's Gizmos > Navigatsiya shows the same.
        bool ShowNavigation = false;
        bool ShowNavigationWalkable = false;

        float LineWidth = 2.0f;   // pixels
        float PointSize = 8.0f;   // pixels (DebugDraw::Point with size 0)
        float TextSize = 17.0f;   // pixels (DebugDraw::Text with size 0)
        // Depth-tested shapes behind the scene's surfaces: 0 hidden .. 1 as bright as in front.
        float OccludedOpacity = 0.0f;

        static constexpr float MinLineWidth = 1.0f, MaxLineWidth = 8.0f;
        static constexpr float MinPointSize = 2.0f, MaxPointSize = 32.0f;
        static constexpr float MinTextSize = 8.0f, MaxTextSize = 48.0f;
        static constexpr float MaxRaycastDuration = 10.0f;
        static constexpr float MinVelocityScale = 0.01f, MaxVelocityScale = 10.0f;

        // Values out of range (or not finite) back into range.
        void Sanitize();

        bool operator==(const DebugDrawSettings&) const = default;
    };

    // What is queued (DebugDraw::GetLines / GetPoints / GetTexts). TimeLeft: seconds of game time
    // still to go; 0 = only until the game's next frame. Frame: the queue's frame it was added in.
    struct DebugDrawLine
    {
        glm::vec3 From{ 0.0f }, To{ 0.0f };
        glm::vec4 Color{ 1.0f };
        float TimeLeft = 0.0f;
        bool DepthTest = true;
        uint64_t Frame = 0;
    };

    struct DebugDrawPoint
    {
        glm::vec3 Position{ 0.0f };
        glm::vec4 Color{ 1.0f };
        float Size = 0.0f; // pixels (0: DebugDrawSettings::PointSize)
        float TimeLeft = 0.0f;
        bool DepthTest = true;
        uint64_t Frame = 0;
    };

    // A filled, flat four-cornered shape (corners in order round it; a navigation cell, an area).
    struct DebugDrawQuad
    {
        std::array<glm::vec3, 4> Corners{};
        glm::vec4 Color{ 1.0f };
        float TimeLeft = 0.0f;
        bool DepthTest = true;
        uint64_t Frame = 0;
    };

    struct DebugDrawText
    {
        glm::vec3 Position{ 0.0f };
        std::string Text;
        glm::vec4 Color{ 1.0f };
        float Size = 0.0f; // pixels (0: DebugDrawSettings::TextSize)
        float TimeLeft = 0.0f;
        bool DepthTest = false;
        uint64_t Frame = 0;
    };

    // The view the queue is drawn into (DebugDraw::Render).
    struct DebugDrawView
    {
        glm::mat4 View{ 1.0f };
        glm::mat4 Projection{ 1.0f };
        uint32_t Width = 0, Height = 0;   // the bound target's size in pixels (its viewport)
        // The scene's depth (the same camera, any resolution: the render scale) for depth-tested
        // shapes; 0: everything is drawn on top.
        uint32_t DepthTexture = 0;
        uint32_t DepthWidth = 0, DepthHeight = 0;
    };

#if ZM_DEBUG_DRAW

    // Immediate-mode debug drawing in world space, from scripts and engine code alike:
    //
    //     DebugDraw::Line(a, b, DebugColor::Red);                  // this frame only
    //     DebugDraw::WireSphere(target, 2.0f, DebugColor::Yellow, 3.0f); // 3 seconds of game time
    //     DebugDraw::Text(Position() + glm::vec3(0, 2, 0), "Salom");
    //
    // duration: 0 = drawn until the game's next frame (call it every frame to keep it), > 0 =
    // seconds of game time - the clock stops while the game is paused (one-frame shapes stay on
    // screen then, too). depthTest: hidden behind the scene's surfaces (false: always on top).
    // The queue belongs to the scene that is updated (the game in play / simulate mode, the edit
    // scene in the editor): it is emptied when play / simulation starts and stops. Drawn after the
    // scene (post-processed picture: the colours are exactly the ones given) by the editor
    // viewport and the player. Main thread only. Without ZM_DEBUG_DRAW (Shipping) every function
    // is an empty inline one.
    class DebugDraw
    {
    public:
        static void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = DebugColor::White,
                         float duration = 0.0f, bool depthTest = true);
        // From `origin` to origin + direction (the direction's length is the ray's length).
        static void Ray(const glm::vec3& origin, const glm::vec3& direction, const glm::vec4& color = DebugColor::White,
                        float duration = 0.0f, bool depthTest = true);
        // A line with a pyramid head at `to`. headSize: the head's length (0: a quarter of the
        // arrow, at most 0.5).
        static void Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = DebugColor::White,
                          float duration = 0.0f, bool depthTest = true, float headSize = 0.0f);
        // Axis-aligned box. A half extent of 0 gives a flat rectangle (4 lines).
        static void Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color = DebugColor::White,
                        float duration = 0.0f, bool depthTest = true);
        // Oriented box.
        static void Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
                        const glm::vec4& color = DebugColor::White, float duration = 0.0f, bool depthTest = true);
        // The unit cube (-0.5 .. 0.5) moved by `transform`: e.g. an entity's world transform.
        static void Box(const glm::mat4& transform, const glm::vec4& color = DebugColor::White, float duration = 0.0f,
                        bool depthTest = true);
        static void Bounds(const AABB& box, const glm::vec4& color = DebugColor::White, float duration = 0.0f, bool depthTest = true);
        // Rectangle in the XY plane (2D games).
        static void Rect(const glm::vec3& center, const glm::vec2& size, const glm::vec4& color = DebugColor::White,
                         float duration = 0.0f, bool depthTest = true);
        // Wire sphere: three circles around its axes.
        static void WireSphere(const glm::vec3& center, float radius, const glm::vec4& color = DebugColor::White,
                               float duration = 0.0f, bool depthTest = true);
        static void Sphere(const glm::vec3& center, float radius, const glm::vec4& color = DebugColor::White,
                           float duration = 0.0f, bool depthTest = true)
        {
            WireSphere(center, radius, color, duration, depthTest);
        }
        // Wire capsule between the centres of its two end spheres (equal points: a sphere).
        static void WireCapsule(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec4& color = DebugColor::White,
                                float duration = 0.0f, bool depthTest = true);
        // Circle around `normal` (the plane it lies in faces it).
        static void Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color = DebugColor::White,
                           float duration = 0.0f, bool depthTest = true);
        // Circle in the XY plane (2D games).
        static void Circle(const glm::vec3& center, float radius, const glm::vec4& color = DebugColor::White,
                           float duration = 0.0f, bool depthTest = true)
        {
            Circle(center, glm::vec3(0.0f, 0.0f, 1.0f), radius, color, duration, depthTest);
        }
        // Three lines through `position` along the world axes, `size` long.
        static void Cross(const glm::vec3& position, float size = 0.25f, const glm::vec4& color = DebugColor::White,
                          float duration = 0.0f, bool depthTest = true);
        // A round dot of `sizePixels` (0: the settings' point size) - the same size at any distance.
        static void Point(const glm::vec3& position, const glm::vec4& color = DebugColor::White, float duration = 0.0f,
                          bool depthTest = true, float sizePixels = 0.0f);
        // The red / green / blue axes of a transform, `size` long.
        static void Axes(const glm::mat4& transform, float size = 1.0f, float duration = 0.0f, bool depthTest = true);
        // Lines through the points in order (closed: back to the first one).
        static void Path(const std::vector<glm::vec3>& points, const glm::vec4& color = DebugColor::White, float duration = 0.0f,
                         bool depthTest = true, bool closed = false);
        // A route to follow (navigation / AI paths): its segments, a head in the middle of each
        // one pointing the way, a dot at every waypoint, the start in green and a ring around the
        // end. Fewer than 2 points: nothing.
        static void NavigationPath(const std::vector<glm::vec3>& points, const glm::vec4& color = DebugColor::Cyan,
                                   float duration = 0.0f, bool depthTest = true);
        // A label facing the screen, centred on `position` (several lines with '\n'). depthTest:
        // hidden while the point is behind a surface.
        static void Text(const glm::vec3& position, std::string_view text, const glm::vec4& color = DebugColor::White,
                         float duration = 0.0f, bool depthTest = false, float sizePixels = 0.0f);
        // A filled quad through four corners in order round it (two triangles: 0-1-2, 2-3-0); use a
        // translucent colour. Drawn before the lines and points.
        static void Quad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                         const glm::vec4& color = DebugColor::White, float duration = 0.0f, bool depthTest = true);
        // A filled rectangle in the XY plane (2D games) / flat on the XZ plane at `center`'s height.
        static void FilledRect(const glm::vec3& center, const glm::vec2& size, const glm::vec4& color = DebugColor::White,
                               float duration = 0.0f, bool depthTest = true);
        static void FilledRectXZ(const glm::vec3& center, const glm::vec2& size, const glm::vec4& color = DebugColor::White,
                                 float duration = 0.0f, bool depthTest = true);

        // ---- engine hooks
        // A physics ray query and its result (shown when ShowRaycasts is on).
        static void PhysicsRaycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const RaycastHit& hit);
        // A ray query that reports every hit (Raycast2DAll): the ray, each hit point with its normal
        // and its place in the order (shown when ShowRaycasts is on).
        static void PhysicsRaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const std::vector<RaycastHit>& hits);
        // An overlap query (OverlapCircle2D / OverlapBox2D / OverlapPoint2D): its outline (empty: a
        // point query at `center`), green when it found something, and the found entities' positions.
        static void PhysicsOverlap(const glm::vec3& center, const std::vector<glm::vec3>& outline, const std::vector<glm::vec3>& found);
        // A 3D overlap query (OverlapSphere3D / OverlapBox3D / OverlapCapsule3D): its shape, green when
        // it found something, and the found entities' positions.
        static void PhysicsOverlap3D(const PhysicsQueryShape3D& shape, const glm::vec3& center, const glm::quat& rotation,
                                     const std::vector<glm::vec3>& found);
        // A 3D shape cast (SphereCast3D ... CapsuleCast3D, the *All variants): the shape where it starts,
        // the way it is swept, where it stops at the first hit (or its full length: a miss in red) and
        // each hit's point and normal, numbered when there are several.
        static void PhysicsShapeCast(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation,
                                     const glm::vec3& direction, float maxDistance, const std::vector<RaycastHit>& hits);
        // A contact point and its normal (shown when ShowContacts is on).
        static void PhysicsContact(const glm::vec3& point, const glm::vec3& normal);
        // A contact point with what is known about it: its normal, how deep the shapes overlap, a
        // hard impact for a moment after it began, dimmed when both bodies sleep.
        static void PhysicsContact(const PhysicsContactPoint& contact);
        // The contacts of a frame: each point, and with ShowContactDetails a label per touching pair
        // (the points of a pair follow each other).
        static void PhysicsContacts(const std::vector<PhysicsContactPoint>& contacts);
        // A running (or broken) joint (shown when ShowJoints is on).
        static void PhysicsJoint(const PhysicsJointDebug& joint);
        // A 3D character's velocity (`desired`: what its script asked for) from `center` (shown
        // when ShowVelocities is on).
        static void CharacterVelocity(const glm::vec3& center, const glm::vec3& velocity, const glm::vec3& desired);
        // A dynamic rigid body's velocity (shown when ShowBodyVelocities is on).
        static void BodyVelocity(const glm::vec3& center, const glm::vec3& velocity);
        static bool IsShowingRaycasts();
        static bool IsShowingContacts();
        static bool IsShowingContactDetails();
        static bool IsShowingJoints();
        static bool IsShowingVelocities();
        static bool IsShowingBodyVelocities();
        static bool IsShowingNavigation();

        // ---- the queue
        // Game time went on by `deltaTime` seconds (Scene: every frame that is not paused, the
        // edit scene every frame): one-frame shapes from the frame before go, the others count down.
        static void Advance(float deltaTime);
        // A frame without game time (the game is paused): what the last game frame drew stays on
        // screen (one-frame shapes too, the others do not count down), while one-frame shapes
        // queued in an earlier paused frame (tools, UI code) go - they do not pile up.
        static void Hold();
        // Frames the queue has seen (Advance and Hold each start one).
        static uint64_t GetFrame();
        static void Clear();
        static const std::vector<DebugDrawLine>& GetLines();
        static const std::vector<DebugDrawPoint>& GetPoints();
        static const std::vector<DebugDrawText>& GetTexts();
        static const std::vector<DebugDrawQuad>& GetQuads();
        // Shapes are dropped (with one warning) when this many lines / points / labels / quads are queued.
        static constexpr size_t MaxLines = 262144;
        static constexpr size_t MaxPoints = 65536;
        static constexpr size_t MaxTexts = 4096;
        static constexpr size_t MaxQuads = 131072;

        // ---- settings (runtime toggle)
        // The player starts with the project's settings (and its command line), the editor with
        // its viewport's: in the editor, play / simulation starts with them and goes back to them
        // when it stops. A script's change (SetEnabled, GetSettings().ShowRaycasts = ...) stays
        // until then - or until the viewport's debug draw settings are changed, which put them back.
        static DebugDrawSettings& GetSettings();
        static bool IsEnabled() { return GetSettings().Enabled; }
        static void SetEnabled(bool enabled) { GetSettings().Enabled = enabled; }
        static constexpr bool IsCompiledIn() { return true; }

        // ---- GPU (editor viewport / player, after post-processing into the bound target)
        static void Render(const DebugDrawView& view);
        // Frees the GPU objects (Renderer::Shutdown).
        static void Shutdown();
    };

#else

    // Shipping: the same API, doing nothing (another inline namespace: no clash with an engine
    // built with debug drawing).
    inline namespace DebugDrawDisabled {
        class DebugDraw
        {
        public:
            static void Line(const glm::vec3&, const glm::vec3&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void Ray(const glm::vec3&, const glm::vec3&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void Arrow(const glm::vec3&, const glm::vec3&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true,
                              float = 0.0f) {}
            static void Box(const glm::vec3&, const glm::vec3&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void Box(const glm::vec3&, const glm::vec3&, const glm::quat&, const glm::vec4& = DebugColor::White, float = 0.0f,
                            bool = true) {}
            static void Box(const glm::mat4&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void Bounds(const AABB&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void Rect(const glm::vec3&, const glm::vec2&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void WireSphere(const glm::vec3&, float, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void Sphere(const glm::vec3&, float, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void WireCapsule(const glm::vec3&, const glm::vec3&, float, const glm::vec4& = DebugColor::White, float = 0.0f,
                                    bool = true) {}
            static void Circle(const glm::vec3&, const glm::vec3&, float, const glm::vec4& = DebugColor::White, float = 0.0f,
                               bool = true) {}
            static void Circle(const glm::vec3&, float, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void Cross(const glm::vec3&, float = 0.25f, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void Point(const glm::vec3&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true, float = 0.0f) {}
            static void Axes(const glm::mat4&, float = 1.0f, float = 0.0f, bool = true) {}
            static void Path(const std::vector<glm::vec3>&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true,
                             bool = false) {}
            static void NavigationPath(const std::vector<glm::vec3>&, const glm::vec4& = DebugColor::Cyan, float = 0.0f, bool = true) {}
            static void Text(const glm::vec3&, std::string_view, const glm::vec4& = DebugColor::White, float = 0.0f, bool = false,
                             float = 0.0f) {}
            static void Quad(const glm::vec3&, const glm::vec3&, const glm::vec3&, const glm::vec3&, const glm::vec4& = DebugColor::White,
                             float = 0.0f, bool = true) {}
            static void FilledRect(const glm::vec3&, const glm::vec2&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}
            static void FilledRectXZ(const glm::vec3&, const glm::vec2&, const glm::vec4& = DebugColor::White, float = 0.0f, bool = true) {}

            static void PhysicsRaycast(const glm::vec3&, const glm::vec3&, float, const RaycastHit&) {}
            static void PhysicsRaycastAll(const glm::vec3&, const glm::vec3&, float, const std::vector<RaycastHit>&) {}
            static void PhysicsOverlap(const glm::vec3&, const std::vector<glm::vec3>&, const std::vector<glm::vec3>&) {}
            static void PhysicsOverlap3D(const PhysicsQueryShape3D&, const glm::vec3&, const glm::quat&, const std::vector<glm::vec3>&) {}
            static void PhysicsShapeCast(const PhysicsQueryShape3D&, const glm::vec3&, const glm::quat&, const glm::vec3&, float,
                                         const std::vector<RaycastHit>&) {}
            static void PhysicsContact(const glm::vec3&, const glm::vec3&) {}
            static void PhysicsContact(const PhysicsContactPoint&) {}
            static void PhysicsContacts(const std::vector<PhysicsContactPoint>&) {}
            static void PhysicsJoint(const PhysicsJointDebug&) {}
            static void CharacterVelocity(const glm::vec3&, const glm::vec3&, const glm::vec3&) {}
            static void BodyVelocity(const glm::vec3&, const glm::vec3&) {}
            static bool IsShowingRaycasts() { return false; }
            static bool IsShowingContacts() { return false; }
            static bool IsShowingContactDetails() { return false; }
            static bool IsShowingJoints() { return false; }
            static bool IsShowingVelocities() { return false; }
            static bool IsShowingBodyVelocities() { return false; }
            static bool IsShowingNavigation() { return false; }

            static void Advance(float) {}
            static void Hold() {}
            static uint64_t GetFrame() { return 0; }
            static void Clear() {}
            static const std::vector<DebugDrawLine>& GetLines() { static const std::vector<DebugDrawLine> none; return none; }
            static const std::vector<DebugDrawPoint>& GetPoints() { static const std::vector<DebugDrawPoint> none; return none; }
            static const std::vector<DebugDrawText>& GetTexts() { static const std::vector<DebugDrawText> none; return none; }
            static const std::vector<DebugDrawQuad>& GetQuads() { static const std::vector<DebugDrawQuad> none; return none; }
            static constexpr size_t MaxLines = 0, MaxPoints = 0, MaxTexts = 0, MaxQuads = 0;

            static DebugDrawSettings& GetSettings()
            {
                static DebugDrawSettings settings;
                settings.Enabled = false;
                return settings;
            }
            static bool IsEnabled() { return false; }
            static void SetEnabled(bool) {}
            static constexpr bool IsCompiledIn() { return false; }

            static void Render(const DebugDrawView&) {}
            static void Shutdown() {}
        };
    }

#endif

}
