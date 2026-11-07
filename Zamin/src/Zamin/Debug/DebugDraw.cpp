#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/VertexArray.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

namespace Zamin {

    // ------------------------------------------------------------------ settings
    static void ClampSetting(float& value, float fallback, float minimum, float maximum)
    {
        if (!std::isfinite(value))
            value = fallback;
        value = std::clamp(value, minimum, maximum);
    }

    void DebugDrawSettings::Sanitize()
    {
        ClampSetting(RaycastDuration, 0.0f, 0.0f, MaxRaycastDuration);
        ClampSetting(NormalLength, 0.4f, 0.01f, 100.0f);
        ClampSetting(VelocityScale, 0.25f, MinVelocityScale, MaxVelocityScale);
        ClampSetting(LineWidth, 2.0f, MinLineWidth, MaxLineWidth);
        ClampSetting(PointSize, 8.0f, MinPointSize, MaxPointSize);
        ClampSetting(TextSize, 17.0f, MinTextSize, MaxTextSize);
        ClampSetting(OccludedOpacity, 0.0f, 0.0f, 1.0f);
    }

#if ZM_DEBUG_DRAW

    namespace {

        struct DebugDrawState
        {
            std::vector<DebugDrawLine> Lines;
            std::vector<DebugDrawPoint> Points;
            std::vector<DebugDrawText> Texts;
            std::vector<DebugDrawQuad> Quads;
            DebugDrawSettings Settings;
            bool WarnedFull = false;
            uint64_t Frame = 1;     // the frame shapes are queued in now
            uint64_t GameFrame = 1; // the last frame Advance started (with game time)
        };

        DebugDrawState& State()
        {
            static DebugDrawState state;
            return state;
        }

        bool Finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

        float Duration(float duration) { return std::isfinite(duration) && duration > 0.0f ? duration : 0.0f; }

        // Room for one more in a list holding at most `max` (warns once when it is full).
        bool HasRoom(size_t size, size_t max)
        {
            if (size < max)
                return true;
            DebugDrawState& s = State();
            if (!s.WarnedFull)
            {
                s.WarnedFull = true;
                ZM_CORE_WARN("DebugDraw navbati to'ldi: yangi shakllar tashlab yuboriladi (uzoq davomiylikdagi chizishlar juda ko'p?)");
            }
            return false;
        }

        void PushLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, float duration, bool depthTest)
        {
            DebugDrawState& s = State();
            if (!s.Settings.Enabled || !Finite(a) || !Finite(b) || !HasRoom(s.Lines.size(), DebugDraw::MaxLines))
                return;
            s.Lines.push_back({ a, b, color, Duration(duration), depthTest, s.Frame });
        }

        // Two unit axes perpendicular to `dir` (and to each other).
        void PerpendicularAxes(const glm::vec3& dir, glm::vec3& u, glm::vec3& v)
        {
            glm::vec3 helper = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            u = glm::normalize(glm::cross(dir, helper));
            v = glm::cross(dir, u);
        }

        constexpr int CircleSegments = 40;

        void PushCircle(const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, float radius, const glm::vec4& color,
                        float duration, bool depthTest)
        {
            glm::vec3 prev = center + u * radius;
            for (int i = 1; i <= CircleSegments; i++)
            {
                float a = glm::two_pi<float>() * (float)i / (float)CircleSegments;
                glm::vec3 p = center + (u * std::cos(a) + v * std::sin(a)) * radius;
                PushLine(prev, p, color, duration, depthTest);
                prev = p;
            }
        }

        // The box around `center` spanned by three half axes (any directions and lengths). Edges
        // along a half axis of length 0 are left out, and so are the copies a flat side would
        // draw twice: a box flat in one axis is a rectangle of 4 lines.
        void PushBox(const glm::vec3& center, const glm::vec3 axes[3], const glm::vec4& color, float duration, bool depthTest)
        {
            bool flat[3];
            for (int k = 0; k < 3; k++)
                flat[k] = glm::length(axes[k]) <= 1e-7f;
            auto corner = [&](int i) {
                glm::vec3 p = center;
                for (int k = 0; k < 3; k++)
                    p += axes[k] * ((i >> k) & 1 ? 1.0f : -1.0f);
                return p;
            };
            for (int i = 0; i < 8; i++)
                for (int k = 0; k < 3; k++)
                {
                    if ((i >> k) & 1 || flat[k])
                        continue; // each edge once (from its corner with bit k clear); no zero-length edges
                    bool duplicate = false;
                    for (int m = 0; m < 3; m++)
                        if (m != k && flat[m] && ((i >> m) & 1))
                            duplicate = true;
                    if (!duplicate)
                        PushLine(corner(i), corner(i | (1 << k)), color, duration, depthTest);
                }
        }

    }

    // ------------------------------------------------------------------ shapes
    void DebugDraw::Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, float duration, bool depthTest)
    {
        PushLine(from, to, color, duration, depthTest);
    }

    void DebugDraw::Ray(const glm::vec3& origin, const glm::vec3& direction, const glm::vec4& color, float duration, bool depthTest)
    {
        PushLine(origin, origin + direction, color, duration, depthTest);
    }

    void DebugDraw::Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, float duration, bool depthTest, float headSize)
    {
        if (!State().Settings.Enabled)
            return;
        PushLine(from, to, color, duration, depthTest);
        glm::vec3 d = to - from;
        float length = glm::length(d);
        if (!(length > 1e-6f) || !std::isfinite(length))
            return;
        d /= length;
        float head = headSize > 0.0f && std::isfinite(headSize) ? headSize : std::min(length * 0.25f, 0.5f);
        head = std::min(head, length);
        float width = head * 0.4f;
        glm::vec3 u, v;
        PerpendicularAxes(d, u, v);
        glm::vec3 base = to - d * head;
        glm::vec3 fins[4] = { base + u * width, base + v * width, base - u * width, base - v * width };
        for (int i = 0; i < 4; i++)
        {
            PushLine(to, fins[i], color, duration, depthTest);
            PushLine(fins[i], fins[(i + 1) % 4], color, duration, depthTest);
        }
    }

    void DebugDraw::Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color, float duration, bool depthTest)
    {
        if (!State().Settings.Enabled)
            return;
        glm::vec3 h = glm::abs(halfExtents);
        const glm::vec3 axes[3] = { { h.x, 0.0f, 0.0f }, { 0.0f, h.y, 0.0f }, { 0.0f, 0.0f, h.z } };
        PushBox(center, axes, color, duration, depthTest);
    }

    void DebugDraw::Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, const glm::vec4& color,
                        float duration, bool depthTest)
    {
        if (!State().Settings.Enabled)
            return;
        glm::quat q = glm::length(rotation) > 1e-6f ? glm::normalize(rotation) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::vec3 axes[3] = { q * glm::vec3(halfExtents.x, 0.0f, 0.0f), q * glm::vec3(0.0f, halfExtents.y, 0.0f),
                                    q * glm::vec3(0.0f, 0.0f, halfExtents.z) };
        PushBox(center, axes, color, duration, depthTest);
    }

    void DebugDraw::Box(const glm::mat4& transform, const glm::vec4& color, float duration, bool depthTest)
    {
        if (!State().Settings.Enabled)
            return;
        const glm::vec3 axes[3] = { glm::vec3(transform[0]) * 0.5f, glm::vec3(transform[1]) * 0.5f, glm::vec3(transform[2]) * 0.5f };
        PushBox(glm::vec3(transform[3]), axes, color, duration, depthTest);
    }

    void DebugDraw::Bounds(const AABB& box, const glm::vec4& color, float duration, bool depthTest)
    {
        Box((box.Min + box.Max) * 0.5f, (box.Max - box.Min) * 0.5f, color, duration, depthTest);
    }

    void DebugDraw::Rect(const glm::vec3& center, const glm::vec2& size, const glm::vec4& color, float duration, bool depthTest)
    {
        Box(center, glm::vec3(size * 0.5f, 0.0f), color, duration, depthTest);
    }

    void DebugDraw::WireSphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration, bool depthTest)
    {
        if (!State().Settings.Enabled || !(radius > 0.0f))
            return;
        PushCircle(center, { 1, 0, 0 }, { 0, 1, 0 }, radius, color, duration, depthTest);
        PushCircle(center, { 1, 0, 0 }, { 0, 0, 1 }, radius, color, duration, depthTest);
        PushCircle(center, { 0, 1, 0 }, { 0, 0, 1 }, radius, color, duration, depthTest);
    }

    // The capsule from `a` to `b`: a ring around each end, four lines along its sides and the two
    // half circles over each end.
    static void PushCapsule(const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec4& color, float duration, bool depthTest)
    {
        const float length = glm::length(b - a);
        if (length <= 1e-6f)
        {
            PushCircle(a, { 1, 0, 0 }, { 0, 1, 0 }, radius, color, duration, depthTest);
            PushCircle(a, { 1, 0, 0 }, { 0, 0, 1 }, radius, color, duration, depthTest);
            PushCircle(a, { 0, 1, 0 }, { 0, 0, 1 }, radius, color, duration, depthTest);
            return;
        }
        const glm::vec3 axis = (b - a) / length;
        glm::vec3 u, v;
        PerpendicularAxes(axis, u, v);
        PushCircle(a, u, v, radius, color, duration, depthTest);
        PushCircle(b, u, v, radius, color, duration, depthTest);
        for (const glm::vec3& side : { u, -u, v, -v })
            PushLine(a + side * radius, b + side * radius, color, duration, depthTest);
        // Half circles: over b (towards +axis) and under a (towards -axis), in the u and v planes.
        constexpr int half = CircleSegments / 2;
        for (const glm::vec3& side : { u, v })
            for (int end = 0; end < 2; end++)
            {
                const glm::vec3 center = end ? b : a;
                const glm::vec3 out = end ? axis : -axis;
                glm::vec3 prev = center + side * radius;
                for (int i = 1; i <= half; i++)
                {
                    const float t = glm::pi<float>() * (float)i / (float)half;
                    const glm::vec3 p = center + (side * std::cos(t) + out * std::sin(t)) * radius;
                    PushLine(prev, p, color, duration, depthTest);
                    prev = p;
                }
            }
    }

    void DebugDraw::WireCapsule(const glm::vec3& point0, const glm::vec3& point1, float radius, const glm::vec4& color, float duration,
                                bool depthTest)
    {
        if (!State().Settings.Enabled || !(radius > 0.0f) || !std::isfinite(radius))
            return;
        PushCapsule(point0, point1, radius, color, duration, depthTest);
    }

    void DebugDraw::Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, float duration,
                           bool depthTest)
    {
        if (!State().Settings.Enabled || !(radius > 0.0f))
            return;
        glm::vec3 n = glm::length(normal) > 1e-6f ? glm::normalize(normal) : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 u, v;
        PerpendicularAxes(n, u, v);
        PushCircle(center, u, v, radius, color, duration, depthTest);
    }

    void DebugDraw::Cross(const glm::vec3& position, float size, const glm::vec4& color, float duration, bool depthTest)
    {
        if (!State().Settings.Enabled)
            return;
        float h = std::abs(size) * 0.5f;
        PushLine(position - glm::vec3(h, 0, 0), position + glm::vec3(h, 0, 0), color, duration, depthTest);
        PushLine(position - glm::vec3(0, h, 0), position + glm::vec3(0, h, 0), color, duration, depthTest);
        PushLine(position - glm::vec3(0, 0, h), position + glm::vec3(0, 0, h), color, duration, depthTest);
    }

    void DebugDraw::Point(const glm::vec3& position, const glm::vec4& color, float duration, bool depthTest, float sizePixels)
    {
        DebugDrawState& s = State();
        if (!s.Settings.Enabled || !Finite(position) || !HasRoom(s.Points.size(), MaxPoints))
            return;
        float size = std::isfinite(sizePixels) && sizePixels > 0.0f ? sizePixels : 0.0f;
        s.Points.push_back({ position, color, size, Duration(duration), depthTest, s.Frame });
    }

    void DebugDraw::Axes(const glm::mat4& transform, float size, float duration, bool depthTest)
    {
        if (!State().Settings.Enabled)
            return;
        glm::vec3 origin(transform[3]);
        const glm::vec4 colors[3] = { DebugColor::Red, DebugColor::Green, DebugColor::Blue };
        for (int k = 0; k < 3; k++)
        {
            glm::vec3 axis(transform[k]);
            float length = glm::length(axis);
            if (length > 1e-6f)
                Arrow(origin, origin + axis / length * size, colors[k], duration, depthTest, std::abs(size) * 0.18f);
        }
    }

    void DebugDraw::Path(const std::vector<glm::vec3>& points, const glm::vec4& color, float duration, bool depthTest, bool closed)
    {
        if (!State().Settings.Enabled || points.size() < 2)
            return;
        for (size_t i = 1; i < points.size(); i++)
            PushLine(points[i - 1], points[i], color, duration, depthTest);
        if (closed && points.size() > 2)
            PushLine(points.back(), points.front(), color, duration, depthTest);
    }

    void DebugDraw::NavigationPath(const std::vector<glm::vec3>& points, const glm::vec4& color, float duration, bool depthTest)
    {
        DebugDrawState& s = State();
        if (!s.Settings.Enabled || points.size() < 2)
            return;
        const float pointSize = s.Settings.PointSize;
        for (size_t i = 1; i < points.size(); i++)
        {
            const glm::vec3 a = points[i - 1], b = points[i];
            PushLine(a, b, color, duration, depthTest);
            glm::vec3 d = b - a;
            float length = glm::length(d);
            if (!(length > 1e-5f) || !std::isfinite(length))
                continue;
            d /= length;
            // A head in the middle of the segment: four fins back from its tip (seen from any side).
            float head = std::min(length * 0.3f, 0.35f);
            glm::vec3 tip = (a + b) * 0.5f + d * (head * 0.5f), u, v;
            PerpendicularAxes(d, u, v);
            glm::vec3 base = tip - d * head;
            for (const glm::vec3& side : { u, v, -u, -v })
                PushLine(tip, base + side * (head * 0.45f), color, duration, depthTest);
        }
        // The waypoints between, the start, the end (a dot and a ring around it facing up).
        for (size_t i = 1; i + 1 < points.size(); i++)
            Point(points[i], color, duration, depthTest, pointSize * 0.75f);
        Point(points.front(), DebugColor::Green, duration, depthTest, pointSize * 1.1f);
        Point(points.back(), color, duration, depthTest, pointSize * 1.1f);
        // The ring lies flat (3D ground paths); a path in the XY plane (2D games) gets it in that
        // plane; a straight line along X could be either: both.
        bool sameY = true, sameZ = true;
        for (const glm::vec3& p : points)
        {
            sameY &= std::abs(p.y - points.front().y) < 1e-5f;
            sameZ &= std::abs(p.z - points.front().z) < 1e-5f;
        }
        if (!sameZ || sameY)
            Circle(points.back(), glm::vec3(0, 1, 0), 0.3f, color, duration, depthTest);
        if (sameZ)
            Circle(points.back(), glm::vec3(0, 0, 1), 0.3f, color, duration, depthTest);
    }

    void DebugDraw::Text(const glm::vec3& position, std::string_view text, const glm::vec4& color, float duration, bool depthTest,
                         float sizePixels)
    {
        DebugDrawState& s = State();
        if (!s.Settings.Enabled || text.empty() || !Finite(position) || !HasRoom(s.Texts.size(), MaxTexts))
            return;
        float size = std::isfinite(sizePixels) && sizePixels > 0.0f ? sizePixels : 0.0f;
        s.Texts.push_back({ position, std::string(text), color, size, Duration(duration), depthTest, s.Frame });
    }

    void DebugDraw::Quad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color,
                         float duration, bool depthTest)
    {
        DebugDrawState& s = State();
        if (!s.Settings.Enabled || !(color.a > 0.0f) || !Finite(p0) || !Finite(p1) || !Finite(p2) || !Finite(p3) ||
            !HasRoom(s.Quads.size(), MaxQuads))
            return;
        s.Quads.push_back({ { p0, p1, p2, p3 }, color, Duration(duration), depthTest, s.Frame });
    }

    void DebugDraw::FilledRect(const glm::vec3& center, const glm::vec2& size, const glm::vec4& color, float duration, bool depthTest)
    {
        const glm::vec3 h(size.x * 0.5f, size.y * 0.5f, 0.0f);
        Quad(center + glm::vec3(-h.x, -h.y, 0.0f), center + glm::vec3(h.x, -h.y, 0.0f), center + glm::vec3(h.x, h.y, 0.0f),
             center + glm::vec3(-h.x, h.y, 0.0f), color, duration, depthTest);
    }

    void DebugDraw::FilledRectXZ(const glm::vec3& center, const glm::vec2& size, const glm::vec4& color, float duration, bool depthTest)
    {
        const glm::vec2 h = size * 0.5f;
        Quad(center + glm::vec3(-h.x, 0.0f, -h.y), center + glm::vec3(h.x, 0.0f, -h.y), center + glm::vec3(h.x, 0.0f, h.y),
             center + glm::vec3(-h.x, 0.0f, h.y), color, duration, depthTest);
    }

    // ------------------------------------------------------------------ physics
    bool DebugDraw::IsShowingRaycasts()
    {
        const DebugDrawSettings& s = State().Settings;
        return s.Enabled && s.ShowRaycasts;
    }

    bool DebugDraw::IsShowingContacts()
    {
        const DebugDrawSettings& s = State().Settings;
        return s.Enabled && s.ShowContacts;
    }

    bool DebugDraw::IsShowingContactDetails()
    {
        const DebugDrawSettings& s = State().Settings;
        return s.Enabled && s.ShowContactDetails;
    }

    bool DebugDraw::IsShowingJoints()
    {
        const DebugDrawSettings& s = State().Settings;
        return s.Enabled && s.ShowJoints;
    }

    bool DebugDraw::IsShowingVelocities()
    {
        const DebugDrawSettings& s = State().Settings;
        return s.Enabled && s.ShowVelocities;
    }

    bool DebugDraw::IsShowingBodyVelocities()
    {
        const DebugDrawSettings& s = State().Settings;
        return s.Enabled && s.ShowBodyVelocities;
    }

    bool DebugDraw::IsShowingNavigation()
    {
        const DebugDrawSettings& s = State().Settings;
        return s.Enabled && s.ShowNavigation;
    }

    void DebugDraw::CharacterVelocity(const glm::vec3& center, const glm::vec3& velocity, const glm::vec3& desired)
    {
        if (!IsShowingVelocities() || !Finite(center) || !Finite(velocity))
            return;
        const DebugDrawSettings& s = State().Settings;
        const glm::vec4 color(0.75f, 0.45f, 1.0f, 1.0f);
        // What the script asked for, when the world did not let it happen (a wall, a slope).
        if (Finite(desired) && glm::length(desired) > 0.01f && glm::length(desired - velocity) > 0.05f)
            Arrow(center, center + desired * s.VelocityScale, glm::vec4(color.r, color.g, color.b, 0.35f), 0.0f, false);
        float speed = glm::length(velocity);
        Point(center, color, 0.0f, false, s.PointSize * 0.6f);
        if (speed < 0.01f)
            return;
        glm::vec3 tip = center + velocity * s.VelocityScale;
        Arrow(center, tip, color, 0.0f, false);
        Text(tip + glm::vec3(0.0f, 0.25f, 0.0f), std::format("{:.1f} m/s", speed), color, 0.0f, false, s.TextSize * 0.8f);
    }

    void DebugDraw::BodyVelocity(const glm::vec3& center, const glm::vec3& velocity)
    {
        if (!IsShowingBodyVelocities() || !Finite(center) || !Finite(velocity) || glm::length(velocity) < 0.05f)
            return;
        Arrow(center, center + velocity * State().Settings.VelocityScale, DebugColor::Cyan, 0.0f, false);
    }

    void DebugDraw::PhysicsRaycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const RaycastHit& hit)
    {
        if (!IsShowingRaycasts() || !Finite(origin))
            return;
        const DebugDrawSettings& s = State().Settings;
        float length = glm::length(direction);
        glm::vec3 dir = length > 0.0f && std::isfinite(length) ? direction / length : glm::vec3(0.0f, -1.0f, 0.0f);
        float range = std::isfinite(maxDistance) ? std::clamp(maxDistance, 0.0f, 10000.0f) : 10000.0f;
        glm::vec3 end = origin + dir * range;
        const float duration = s.RaycastDuration;
        if (hit.Hit)
        {
            PushLine(origin, hit.Point, DebugColor::Green, duration, false);
            if (range > hit.Distance + 1e-4f)
                PushLine(hit.Point, end, glm::vec4(0.75f, 0.78f, 0.8f, 0.3f), duration, false);
            Point(hit.Point, DebugColor::Yellow, duration, false);
            if (glm::length(hit.Normal) > 0.5f)
                Arrow(hit.Point, hit.Point + glm::normalize(hit.Normal) * s.NormalLength, DebugColor::Cyan, duration, false,
                      s.NormalLength * 0.3f);
        }
        else
        {
            PushLine(origin, end, DebugColor::Red, duration, false);
            Point(origin, DebugColor::Red, duration, false, s.PointSize * 0.6f);
        }
    }

    void DebugDraw::PhysicsRaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const std::vector<RaycastHit>& hits)
    {
        if (!IsShowingRaycasts() || !Finite(origin))
            return;
        if (hits.empty())
        {
            PhysicsRaycast(origin, direction, maxDistance, RaycastHit());
            return;
        }
        const DebugDrawSettings& s = State().Settings;
        float length = glm::length(direction);
        glm::vec3 dir = length > 0.0f && std::isfinite(length) ? direction / length : glm::vec3(0.0f, -1.0f, 0.0f);
        float range = std::isfinite(maxDistance) ? std::clamp(maxDistance, 0.0f, 10000.0f) : 10000.0f;
        const float duration = s.RaycastDuration;
        // Solid up to the last hit, faint after it.
        const RaycastHit& last = hits.back();
        PushLine(origin, last.Point, DebugColor::Green, duration, false);
        if (range > last.Distance + 1e-4f)
            PushLine(last.Point, origin + dir * range, glm::vec4(0.75f, 0.78f, 0.8f, 0.3f), duration, false);
        for (size_t i = 0; i < hits.size(); i++)
        {
            const RaycastHit& hit = hits[i];
            Point(hit.Point, DebugColor::Yellow, duration, false);
            if (glm::length(hit.Normal) > 0.5f)
                Arrow(hit.Point, hit.Point + glm::normalize(hit.Normal) * s.NormalLength, DebugColor::Cyan, duration, false,
                      s.NormalLength * 0.3f);
            if (hits.size() > 1)
                Text(hit.Point + glm::vec3(0.0f, s.NormalLength * 0.6f, 0.0f), std::to_string(i + 1), DebugColor::Yellow, duration, false);
        }
    }

    void DebugDraw::PhysicsOverlap(const glm::vec3& center, const std::vector<glm::vec3>& outline, const std::vector<glm::vec3>& found)
    {
        if (!IsShowingRaycasts() || !Finite(center))
            return;
        const DebugDrawSettings& s = State().Settings;
        const float duration = s.RaycastDuration;
        const glm::vec4 color = found.empty() ? DebugColor::Red : DebugColor::Green;
        if (outline.size() >= 2)
            for (size_t i = 0; i < outline.size(); i++)
                PushLine(outline[i], outline[(i + 1) % outline.size()], color, duration, false);
        else
            Cross(center, 0.2f, color, duration, false);
        for (const glm::vec3& p : found)
        {
            PushLine(center, p, glm::vec4(color.r, color.g, color.b, 0.35f), duration, false);
            Point(p, DebugColor::Yellow, duration, false);
        }
    }

    // A 3D query shape at `center`, turned by `rotation`.
    static void PushQueryShape(const PhysicsQueryShape3D& shape, const glm::vec3& center, const glm::quat& rotation, const glm::vec4& color,
                               float duration)
    {
        const glm::quat q = glm::length(rotation) > 1e-6f ? glm::normalize(rotation) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        switch (shape.Type)
        {
            case PhysicsQueryShape3D::Kind::Sphere:
                if (shape.Radius > 0.0f)
                    PushCapsule(center, center, shape.Radius, color, duration, false);
                break;
            case PhysicsQueryShape3D::Kind::Box:
            {
                const glm::vec3 h = glm::abs(shape.HalfExtents);
                const glm::vec3 axes[3] = { q * glm::vec3(h.x, 0.0f, 0.0f), q * glm::vec3(0.0f, h.y, 0.0f), q * glm::vec3(0.0f, 0.0f, h.z) };
                PushBox(center, axes, color, duration, false);
                break;
            }
            case PhysicsQueryShape3D::Kind::Capsule:
            {
                if (!(shape.Radius > 0.0f))
                    break;
                const glm::vec3 half = q * glm::vec3(0.0f, std::max(0.0f, shape.HalfHeight), 0.0f);
                PushCapsule(center - half, center + half, shape.Radius, color, duration, false);
                break;
            }
        }
    }

    void DebugDraw::PhysicsOverlap3D(const PhysicsQueryShape3D& shape, const glm::vec3& center, const glm::quat& rotation,
                                     const std::vector<glm::vec3>& found)
    {
        if (!IsShowingRaycasts() || !Finite(center))
            return;
        const DebugDrawSettings& s = State().Settings;
        const float duration = s.RaycastDuration;
        const glm::vec4 color = found.empty() ? DebugColor::Red : DebugColor::Green;
        PushQueryShape(shape, center, rotation, color, duration);
        for (const glm::vec3& p : found)
        {
            PushLine(center, p, glm::vec4(color.r, color.g, color.b, 0.35f), duration, false);
            Point(p, DebugColor::Yellow, duration, false);
        }
    }

    void DebugDraw::PhysicsShapeCast(const PhysicsQueryShape3D& shape, const glm::vec3& origin, const glm::quat& rotation,
                                     const glm::vec3& direction, float maxDistance, const std::vector<RaycastHit>& hits)
    {
        if (!IsShowingRaycasts() || !Finite(origin))
            return;
        const DebugDrawSettings& s = State().Settings;
        const float duration = s.RaycastDuration;
        const float length = glm::length(direction);
        const glm::vec3 dir = length > 0.0f && std::isfinite(length) ? direction / length : glm::vec3(0.0f, -1.0f, 0.0f);
        const float range = std::isfinite(maxDistance) ? std::clamp(maxDistance, 0.0f, 10000.0f) : 10000.0f;
        const glm::vec4 faint(0.75f, 0.78f, 0.8f, 0.3f);
        // Where it starts (faint), the sweep, where it stops: at the first hit (green) or the end (red).
        PushQueryShape(shape, origin, rotation, faint, duration);
        if (hits.empty())
        {
            PushLine(origin, origin + dir * range, DebugColor::Red, duration, false);
            PushQueryShape(shape, origin + dir * range, rotation, DebugColor::Red, duration);
            return;
        }
        const glm::vec3 stop = origin + dir * hits.front().Distance;
        PushLine(origin, stop, DebugColor::Green, duration, false);
        if (range > hits.front().Distance + 1e-4f)
            PushLine(stop, origin + dir * range, faint, duration, false);
        PushQueryShape(shape, stop, rotation, DebugColor::Green, duration);
        for (size_t i = 0; i < hits.size(); i++)
        {
            const RaycastHit& hit = hits[i];
            Point(hit.Point, DebugColor::Yellow, duration, false);
            if (glm::length(hit.Normal) > 0.5f)
                Arrow(hit.Point, hit.Point + glm::normalize(hit.Normal) * s.NormalLength, DebugColor::Cyan, duration, false,
                      s.NormalLength * 0.3f);
            if (hits.size() > 1)
                Text(hit.Point + glm::vec3(0.0f, s.NormalLength * 0.6f, 0.0f), std::to_string(i + 1), DebugColor::Yellow, duration, false);
        }
    }

    void DebugDraw::PhysicsContact(const glm::vec3& point, const glm::vec3& normal)
    {
        if (!IsShowingContacts())
            return;
        const DebugDrawSettings& s = State().Settings;
        Point(point, DebugColor::Orange, 0.0f, false, s.PointSize * 0.8f);
        if (glm::length(normal) > 0.5f)
            PushLine(point, point + glm::normalize(normal) * s.NormalLength, DebugColor::Yellow, 0.0f, false);
    }

    namespace {
        // A contact that began this fast (m/s) is an impact: a ring for ImpactTime seconds.
        constexpr float ImpactSpeed = 0.5f;
        constexpr float ImpactTime = 0.6f;
        constexpr size_t MaxContactLabels = 64; // per frame (a pile of crates would bury the picture)
        constexpr float LabelImpactSpeed = 0.25f; // m/s: slower contacts were put down, not hit

        // Physics quantities with as many decimals as they need.
        std::string FormatAmount(float value, const char* unit)
        {
            const float a = std::abs(value);
            return a >= 100.0f ? std::format("{:.0f} {}", value, unit)
                 : a >= 1.0f   ? std::format("{:.2f} {}", value, unit)
                               : std::format("{:.3f} {}", value, unit);
        }
    }

    void DebugDraw::PhysicsContact(const PhysicsContactPoint& c)
    {
        if (!IsShowingContacts() || !Finite(c.Position))
            return;
        const DebugDrawSettings& s = State().Settings;
        // Both bodies asleep: dimmed (the solver does nothing with them now).
        const float alpha = c.Sleeping ? 0.4f : 1.0f;
        auto faded = [&](glm::vec4 color) {
            color.a *= alpha;
            return color;
        };
        const glm::vec4 pointColor = c.Sleeping ? glm::vec4(0.55f, 0.65f, 0.85f, 1.0f) : c.Character ? DebugColor::Green : DebugColor::Orange;
        Point(c.Position, faded(pointColor), 0.0f, false, s.PointSize * 0.8f);
        if (!Finite(c.Normal) || glm::length(c.Normal) < 0.5f)
            return;
        const glm::vec3 n = glm::normalize(c.Normal);
        PushLine(c.Position, c.Position + n * s.NormalLength, faded(DebugColor::Yellow), 0.0f, false);
        // How far the shapes overlap, true to scale into EntityA's surface (a few millimetres at
        // rest; a body pushed into another one shows it).
        if (std::isfinite(c.Penetration) && c.Penetration > 0.001f)
            PushLine(c.Position, c.Position - n * std::min(c.Penetration, 10.0f), faded(DebugColor::Red), 0.0f, false);
        // A hard impact, for a moment after the contact began: a ring around the normal that grows
        // and fades (larger for faster hits).
        if (!c.Sleeping && std::isfinite(c.Speed) && c.Speed >= ImpactSpeed && c.Age >= 0.0f && c.Age < ImpactTime)
        {
            const float t = c.Age / ImpactTime;
            const float strength = std::clamp(c.Speed / 8.0f, 0.25f, 1.0f);
            Circle(c.Position, n, s.NormalLength * strength * (0.35f + 0.9f * t), glm::vec4(1.0f, 0.42f, 0.25f, 1.0f - t), 0.0f, false);
        }
    }

    void DebugDraw::PhysicsContacts(const std::vector<PhysicsContactPoint>& contacts)
    {
        if (!IsShowingContacts())
            return;
        for (const PhysicsContactPoint& c : contacts)
            PhysicsContact(c);
        if (!IsShowingContactDetails())
            return;
        const DebugDrawSettings& s = State().Settings;
        auto samePair = [](const PhysicsContactPoint& a, const PhysicsContactPoint& b) {
            return a.EntityA == b.EntityA && a.EntityB == b.EntityB && a.Character == b.Character;
        };
        size_t labels = 0;
        std::vector<glm::vec3> placed;
        for (size_t i = 0; i < contacts.size() && labels < MaxContactLabels;)
        {
            // The pair's points: their middle, the deepest overlap, the hardest impact, the impulse in all.
            const PhysicsContactPoint& first = contacts[i];
            glm::vec3 middle(0.0f), normal(0.0f);
            float depth = -FLT_MAX, speed = 0.0f, impulse = 0.0f;
            bool sleeping = true;
            size_t count = 0, j = i;
            for (; j < contacts.size() && samePair(first, contacts[j]); j++)
            {
                const PhysicsContactPoint& c = contacts[j];
                if (!Finite(c.Position))
                    continue;
                middle += c.Position;
                if (Finite(c.Normal))
                    normal += c.Normal;
                depth = std::max(depth, c.Penetration);
                speed = std::max(speed, c.Speed);
                impulse += c.Impulse;
                sleeping = sleeping && c.Sleeping;
                count++;
            }
            i = j;
            if (count == 0)
                continue;
            middle /= (float)count;
            // Beside the contact along its normal, on the upper side (between two dynamic bodies the
            // normal may point either way).
            glm::vec3 up = glm::length(normal) > 1e-4f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
            if (up.y < -0.2f)
                up = -up;
            std::string text = depth >= 0.0f ? std::format("chuqurlik {:.1f} mm", depth * 1000.0f)
                                             : std::format("oraliq {:.1f} mm", -depth * 1000.0f);
            // A 3D impact (a body put down gently "hits" at one step of gravity: not worth a line);
            // 2D contacts carry Box2D's impulse without a speed.
            const bool impact = speed >= LabelImpactSpeed;
            if (impact)
                text += std::format("\nzarba {:.2f} m/s", speed);
            if (impulse > 1e-5f && (impact || speed <= 0.0f))
                text += "\nimpuls " + FormatAmount(impulse, "N*s");
            if (first.Character)
                text += "\npersonaj";
            else if (sleeping)
                text += "\nuxlayapti";
            const glm::vec4 color = sleeping ? glm::vec4(0.7f, 0.75f, 0.85f, 0.6f) : glm::vec4(1.0f, 0.95f, 0.8f, 1.0f);
            // Pairs touching in about the same place (a ball on a plank on the floor): their labels
            // are stacked instead of written over each other.
            glm::vec3 at = middle + up * (s.NormalLength * 1.35f);
            for (int tries = 0; tries < 8; tries++)
            {
                const bool clash = std::any_of(placed.begin(), placed.end(),
                                               [&](const glm::vec3& other) { return glm::length(other - at) < s.NormalLength * 1.2f; });
                if (!clash)
                    break;
                at.y += s.NormalLength * 1.4f;
            }
            placed.push_back(at);
            Text(at, text, color, 0.0f, false, s.TextSize * 0.75f);
            labels++;
        }
    }

    void DebugDraw::PhysicsJoint(const PhysicsJointDebug& j)
    {
        if (!IsShowingJoints() || !Finite(j.Anchor))
            return;
        const DebugDrawSettings& s = State().Settings;
        const float size = s.NormalLength * 0.3f;
        if (j.Broken)
        {
            // Holds nothing any more: where it was attached, crossed out.
            Point(j.Anchor, DebugColor::Red, 0.0f, false, s.PointSize);
            for (glm::vec3 d : { glm::vec3(1, 1, 0), glm::vec3(1, -1, 0), glm::vec3(0, 1, 1), glm::vec3(0, 1, -1) })
                PushLine(j.Anchor - d * size, j.Anchor + d * size, DebugColor::Red, 0.0f, false);
            return;
        }
        // Green (no load) - yellow - red (about to break); cyan: it never breaks.
        glm::vec4 color = DebugColor::Cyan;
        if (std::isfinite(j.Load) && j.Load >= 0.0f)
        {
            const float t = std::clamp(j.Load, 0.0f, 1.0f);
            color = t < 0.5f ? glm::mix(DebugColor::Green, DebugColor::Yellow, t * 2.0f) : glm::mix(DebugColor::Yellow, DebugColor::Red, t * 2.0f - 1.0f);
        }
        const glm::vec4 connectedColor(0.75f, 0.55f, 1.0f, 1.0f);
        if (Finite(j.BodyCenter))
            PushLine(j.BodyCenter, j.Anchor, glm::vec4(color.r, color.g, color.b, 0.45f), 0.0f, false);
        const bool connected = Finite(j.ConnectedAnchor);
        if (connected && j.HasConnectedBody && Finite(j.ConnectedCenter))
            PushLine(j.ConnectedCenter, j.ConnectedAnchor, glm::vec4(connectedColor.r, connectedColor.g, connectedColor.b, 0.45f), 0.0f, false);
        if (connected)
        {
            // Anchors that should meet but have parted (the joint gives under the load) in red; a
            // rod / slider: the line between its anchors.
            const float gap = glm::length(j.ConnectedAnchor - j.Anchor);
            if (gap > 1e-4f)
                PushLine(j.Anchor, j.ConnectedAnchor, j.AnchorsMeet && gap > 0.02f ? DebugColor::Red : color, 0.0f, false);
            Point(j.ConnectedAnchor, connectedColor, 0.0f, false, s.PointSize * 0.8f);
        }
        Point(j.Anchor, color, 0.0f, false, s.PointSize);
        if (Finite(j.Axis) && glm::length(j.Axis) > 1e-4f)
        {
            const glm::vec3 axis = glm::normalize(j.Axis) * s.NormalLength;
            Arrow(j.Anchor, j.Anchor + axis, glm::vec4(color.r, color.g, color.b, 0.85f), 0.0f, false, s.NormalLength * 0.25f);
        }
        if (IsShowingContactDetails())
        {
            std::string text = FormatAmount(j.Force, "N") + "  " + FormatAmount(j.Torque, "N*m");
            if (std::isfinite(j.Load) && j.Load >= 0.0f)
                text += std::format("\nyuk {:.0f}%", std::min(j.Load, 9.99f) * 100.0f);
            Text(j.Anchor + glm::vec3(0.0f, s.NormalLength * 0.7f, 0.0f), text, color, 0.0f, false, s.TextSize * 0.75f);
        }
    }

    // ------------------------------------------------------------------ queue
    template<typename T>
    static void AgeItems(std::vector<T>& items, float deltaTime)
    {
        size_t kept = 0;
        for (size_t i = 0; i < items.size(); i++)
        {
            T& item = items[i];
            if (item.TimeLeft <= 0.0f)
                continue; // one-frame shape: its frame is over
            item.TimeLeft -= deltaTime;
            if (item.TimeLeft <= 1e-6f)
                continue;
            if (kept != i)
                items[kept] = std::move(item);
            kept++;
        }
        items.resize(kept);
    }

    void DebugDraw::Advance(float deltaTime)
    {
        DebugDrawState& s = State();
        float dt = std::isfinite(deltaTime) && deltaTime > 0.0f ? deltaTime : 0.0f;
        AgeItems(s.Lines, dt);
        AgeItems(s.Points, dt);
        AgeItems(s.Texts, dt);
        AgeItems(s.Quads, dt);
        s.GameFrame = ++s.Frame;
    }

    // One-frame shapes not queued in the last game frame go; everything else stays as it is.
    template<typename T>
    static void DropPausedFrameItems(std::vector<T>& items, uint64_t gameFrame)
    {
        std::erase_if(items, [&](const T& item) { return item.TimeLeft <= 0.0f && item.Frame != gameFrame; });
    }

    void DebugDraw::Hold()
    {
        DebugDrawState& s = State();
        DropPausedFrameItems(s.Lines, s.GameFrame);
        DropPausedFrameItems(s.Points, s.GameFrame);
        DropPausedFrameItems(s.Texts, s.GameFrame);
        DropPausedFrameItems(s.Quads, s.GameFrame);
        s.Frame++;
    }

    uint64_t DebugDraw::GetFrame() { return State().Frame; }

    void DebugDraw::Clear()
    {
        DebugDrawState& s = State();
        s.Lines.clear();
        s.Points.clear();
        s.Texts.clear();
        s.Quads.clear();
        s.WarnedFull = false;
    }

    const std::vector<DebugDrawLine>& DebugDraw::GetLines() { return State().Lines; }
    const std::vector<DebugDrawPoint>& DebugDraw::GetPoints() { return State().Points; }
    const std::vector<DebugDrawText>& DebugDraw::GetTexts() { return State().Texts; }
    const std::vector<DebugDrawQuad>& DebugDraw::GetQuads() { return State().Quads; }

    DebugDrawSettings& DebugDraw::GetSettings() { return State().Settings; }

    // ------------------------------------------------------------------ GPU
    namespace {

        // Depth test against the scene's depth texture, shared by both shaders: the shape is
        // hidden where it is farther from the camera than the surface in front of it (a little
        // slack for shapes lying on a surface, more on a slanted one).
        const char* s_DepthTestGLSL = R"(
uniform sampler2D u_Depth;
uniform int u_HasDepth;
uniform mat4 u_InvProjection;
uniform vec2 u_DepthTexel;   // one texel of the depth texture (uv)
uniform float u_DepthScale;  // depth texels per target pixel

float ViewDistance(vec2 uv, float depth)
{
    vec4 p = u_InvProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return -p.z / p.w;
}

float SceneDistance(vec2 uv) { return ViewDistance(uv, texture(u_Depth, uv).r); }

// `slack`: target pixels the shape reaches around the sample.
bool Occluded(vec2 uv, float depth, float slack)
{
    if (u_HasDepth == 0)
        return false;
    float scene = texture(u_Depth, uv).r;
    if (scene >= 1.0)
        return false; // nothing drawn there
    float sceneDist = ViewDistance(uv, scene);
    float own = ViewDistance(uv, depth);
    vec2 t = u_DepthTexel;
    // How fast the surface's distance changes per texel (the smaller side: no jump over an edge).
    float dx = min(abs(SceneDistance(uv + vec2(t.x, 0.0)) - sceneDist), abs(sceneDist - SceneDistance(uv - vec2(t.x, 0.0))));
    float dy = min(abs(SceneDistance(uv + vec2(0.0, t.y)) - sceneDist), abs(sceneDist - SceneDistance(uv - vec2(0.0, t.y))));
    float slope = min(max(dx, dy), abs(sceneDist) * 0.1);
    float bias = 0.01 + abs(sceneDist) * 0.002 + slope * (slack * u_DepthScale + 1.0);
    return own > sceneDist + bias;
}
)";

        // Lines and points as screen-space quads: lines `LineWidth` pixels wide with round, smooth
        // ends (clipped to the near plane first), points round dots. No GL depth test: Occluded().
        const char* s_ShapeVS = R"(#version 410 core
layout(location = 0) in vec3 a_A;
layout(location = 1) in vec3 a_B;
layout(location = 2) in vec4 a_Color;
// x: the end (0 at A, 1 at B) / a point's corner x (-1, 1); y: the side (-1, 1) / a point's corner y;
// z: width / diameter in pixels; w: 0 line, 1 point, + 2 when depth tested; 4 a filled quad's corner
// (A as it is), 5 depth tested.
layout(location = 3) in vec4 a_Params;

uniform mat4 u_ViewProjection;
uniform vec2 u_ViewportSize;

out vec4 v_Color;
noperspective out vec2 v_Local; // pixels: along the line from A (x) and across it (y)
flat out float v_Radius;
flat out float v_Length;
flat out float v_DepthTest;

const vec4 c_Outside = vec4(2.0, 2.0, 2.0, 1.0); // clipped away

void main()
{
    v_Color = a_Color;
    if (a_Params.w > 3.5)
    {
        // A filled quad's corner: no expansion, fully covered (alpha = the colour's).
        v_DepthTest = a_Params.w > 4.5 ? 1.0 : 0.0;
        v_Radius = 0.5;
        v_Length = 0.0;
        v_Local = vec2(0.0);
        gl_Position = u_ViewProjection * vec4(a_A, 1.0);
        return;
    }
    bool point = mod(a_Params.w, 2.0) > 0.5;
    v_DepthTest = a_Params.w > 1.5 ? 1.0 : 0.0;
    float radius = max(a_Params.z * 0.5, 0.5);
    float pad = radius + 1.0; // one pixel more for the smooth edge
    v_Radius = radius;
    v_Length = 0.0;
    v_Local = vec2(0.0);
    vec2 halfSize = u_ViewportSize * 0.5;
    vec4 a = u_ViewProjection * vec4(a_A, 1.0);
    if (point)
    {
        if (a.w <= 0.0 || a.z < -a.w) { gl_Position = c_Outside; return; }
        v_Local = a_Params.xy * pad;
        a.xy += a_Params.xy * pad / halfSize * a.w;
        gl_Position = a;
        return;
    }
    vec4 b = u_ViewProjection * vec4(a_B, 1.0);
    // Only the part in front of the near plane (z >= -w).
    float da = a.z + a.w, db = b.z + b.w;
    if (da < 0.0 && db < 0.0) { gl_Position = c_Outside; return; }
    if (da < 0.0) a = mix(a, b, min(da / (da - db) + 1e-4, 1.0));
    else if (db < 0.0) b = mix(b, a, min(db / (db - da) + 1e-4, 1.0));
    vec2 sa = a.xy / a.w * halfSize, sb = b.xy / b.w * halfSize;
    vec2 dir = sb - sa;
    float len = length(dir);
    dir = len > 1e-4 ? dir / len : vec2(1.0, 0.0);
    vec2 normal = vec2(-dir.y, dir.x);
    bool atB = a_Params.x > 0.5;
    vec4 p = atB ? b : a;
    p.xy += (dir * (atB ? pad : -pad) + normal * a_Params.y * pad) / halfSize * p.w;
    v_Local = vec2(atB ? len + pad : -pad, a_Params.y * pad);
    v_Length = len;
    gl_Position = p;
}
)";

        const char* s_ShapeFSHead = R"(#version 410 core
layout(location = 0) out vec4 o_Color;

in vec4 v_Color;
noperspective in vec2 v_Local;
flat in float v_Radius;
flat in float v_Length;
flat in float v_DepthTest;

uniform vec2 u_ViewportSize;
uniform float u_OccludedOpacity;
)";

        const char* s_ShapeFSMain = R"(
void main()
{
    // Distance to the segment (a point: to its centre), in pixels.
    float along = max(max(-v_Local.x, v_Local.x - v_Length), 0.0);
    float dist = length(vec2(along, v_Local.y));
    float alpha = v_Color.a * clamp(v_Radius + 0.5 - dist, 0.0, 1.0);
    if (alpha > 0.0 && v_DepthTest > 0.5 && Occluded(gl_FragCoord.xy / u_ViewportSize, gl_FragCoord.z, v_Radius + 1.0))
        alpha *= u_OccludedOpacity;
    if (alpha <= 0.002)
        discard;
    o_Color = vec4(v_Color.rgb * alpha, alpha); // premultiplied: the target keeps its alpha
}
)";

        // Labels: glyph quads already in clip space; all of a label is tested at its anchor.
        const char* s_TextVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec3 a_Anchor; // xy: the anchor's uv, z: 1 when depth tested

out vec2 v_TexCoord;
out vec4 v_Color;
flat out vec3 v_Anchor;

void main()
{
    v_TexCoord = a_TexCoord;
    v_Color = a_Color;
    v_Anchor = a_Anchor;
    gl_Position = vec4(a_Position, 1.0);
}
)";

        const char* s_TextFSHead = R"(#version 410 core
layout(location = 0) out vec4 o_Color;

in vec2 v_TexCoord;
in vec4 v_Color;
flat in vec3 v_Anchor;

uniform sampler2D u_Atlas;
uniform float u_OccludedOpacity;
)";

        const char* s_TextFSMain = R"(
void main()
{
    // The atlas is baked large: a sharper mip and slightly fuller strokes keep small labels readable.
    float alpha = v_Color.a * pow(texture(u_Atlas, v_TexCoord, -0.7).a, 0.85);
    if (alpha > 0.0 && v_Anchor.z > 0.5 && Occluded(v_Anchor.xy, gl_FragCoord.z, 0.0))
        alpha *= u_OccludedOpacity;
    if (alpha <= 0.002)
        discard;
    o_Color = vec4(v_Color.rgb * alpha, alpha);
}
)";

        struct ShapeVertex
        {
            glm::vec3 A;
            glm::vec3 B;
            glm::vec4 Color;
            glm::vec4 Params;
        };

        struct TextVertex
        {
            glm::vec3 Position;
            glm::vec2 TexCoord;
            glm::vec4 Color;
            glm::vec3 Anchor;
        };

        constexpr uint32_t MaxBatchQuads = 16384;

        struct DebugDrawGPU
        {
            Ref<Shader> ShapeShader, TextShader;
            Ref<VertexArray> ShapeVA, TextVA;
            Ref<VertexBuffer> ShapeVB, TextVB;
            std::vector<ShapeVertex> Shapes;
            std::vector<TextVertex> Glyphs;
            bool Initialized = false;
            bool Failed = false;
        };

        DebugDrawGPU& GPU()
        {
            static DebugDrawGPU gpu;
            return gpu;
        }

        bool EnsureGPU()
        {
            DebugDrawGPU& g = GPU();
            if (g.Initialized)
                return !g.Failed;
            g.Initialized = true;
            g.ShapeShader = Shader::Create("DebugDrawShapes", s_ShapeVS, std::string(s_ShapeFSHead) + s_DepthTestGLSL + s_ShapeFSMain);
            g.TextShader = Shader::Create("DebugDrawText", s_TextVS, std::string(s_TextFSHead) + s_DepthTestGLSL + s_TextFSMain);
            if (!g.ShapeShader || !g.ShapeShader->IsValid() || !g.TextShader || !g.TextShader->IsValid())
            {
                ZM_CORE_ERROR("DebugDraw shaderlari kompilyatsiya qilinmadi: debug chizish ko'rinmaydi");
                g.Failed = true;
                return false;
            }

            std::vector<uint32_t> indices(MaxBatchQuads * 6);
            for (uint32_t q = 0; q < MaxBatchQuads; q++)
            {
                const uint32_t v = q * 4;
                uint32_t* i = &indices[(size_t)q * 6];
                i[0] = v; i[1] = v + 1; i[2] = v + 2;
                i[3] = v + 2; i[4] = v + 3; i[5] = v;
            }
            Ref<IndexBuffer> quads = IndexBuffer::Create(indices.data(), (uint32_t)indices.size());

            g.ShapeVA = VertexArray::Create();
            g.ShapeVB = VertexBuffer::Create(MaxBatchQuads * 4 * (uint32_t)sizeof(ShapeVertex));
            g.ShapeVB->SetLayout({
                { ShaderDataType::Float3, "a_A" },
                { ShaderDataType::Float3, "a_B" },
                { ShaderDataType::Float4, "a_Color" },
                { ShaderDataType::Float4, "a_Params" },
            });
            g.ShapeVA->AddVertexBuffer(g.ShapeVB);
            g.ShapeVA->SetIndexBuffer(quads);

            g.TextVA = VertexArray::Create();
            g.TextVB = VertexBuffer::Create(MaxBatchQuads * 4 * (uint32_t)sizeof(TextVertex));
            g.TextVB->SetLayout({
                { ShaderDataType::Float3, "a_Position" },
                { ShaderDataType::Float2, "a_TexCoord" },
                { ShaderDataType::Float4, "a_Color" },
                { ShaderDataType::Float3, "a_Anchor" },
            });
            g.TextVA->AddVertexBuffer(g.TextVB);
            g.TextVA->SetIndexBuffer(quads);
            g.Shapes.reserve(MaxBatchQuads * 4);
            g.Glyphs.reserve(MaxBatchQuads * 4);
            return true;
        }

        void SetDepthUniforms(Shader& shader, const DebugDrawView& view, const glm::mat4& invProjection)
        {
            shader.SetInt("u_Depth", 0);
            shader.SetInt("u_HasDepth", view.DepthTexture && view.DepthWidth && view.DepthHeight ? 1 : 0);
            shader.SetMat4("u_InvProjection", invProjection);
            shader.SetFloat2("u_DepthTexel", { 1.0f / (float)std::max(1u, view.DepthWidth), 1.0f / (float)std::max(1u, view.DepthHeight) });
            shader.SetFloat("u_DepthScale", view.DepthWidth ? (float)view.DepthWidth / (float)view.Width : 1.0f);
        }

        // Uploads `vertices` (whole quads) batch by batch and draws them.
        template<typename Vertex>
        void DrawQuads(std::vector<Vertex>& vertices, const Ref<VertexArray>& va, const Ref<VertexBuffer>& vb)
        {
            const size_t quadCount = vertices.size() / 4;
            for (size_t first = 0; first < quadCount; first += MaxBatchQuads)
            {
                const uint32_t count = (uint32_t)std::min<size_t>(MaxBatchQuads, quadCount - first);
                vb->StreamData(vertices.data() + first * 4, count * 4 * (uint32_t)sizeof(Vertex));
                RenderCommand::DrawIndexed(va, count * 6);
            }
            vertices.clear();
        }

        // A label's glyph quads (a dark shadow first, one pixel down-right, then the glyphs).
        void LayoutText(const DebugDrawText& item, const Font& font, const glm::mat4& viewProjection, const DebugDrawView& view,
                        float sizePixels, std::vector<TextVertex>& out)
        {
            glm::vec4 clip = viewProjection * glm::vec4(item.Position, 1.0f);
            if (clip.w <= 1e-6f)
                return; // behind the camera
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f)
                return; // before the near or past the far plane
            const glm::vec2 size((float)view.Width, (float)view.Height);
            const glm::vec2 anchor = (glm::vec2(ndc) * 0.5f + 0.5f) * size;
            const glm::vec3 anchorInfo(glm::vec2(ndc) * 0.5f + 0.5f, item.DepthTest ? 1.0f : 0.0f);
            const float scale = sizePixels;
            const float lineHeight = font.GetLineHeight() * scale;

            // Line widths (the label is centred on the anchor, each line on its own).
            std::vector<float> widths(1, 0.0f);
            for (size_t i = 0; i < item.Text.size();)
            {
                uint32_t cp = DecodeUTF8(item.Text, i);
                if (cp == '\n') { widths.push_back(0.0f); continue; }
                if (cp == '\t') cp = ' ';
                if (const Glyph* g = font.GetGlyph(cp))
                    widths.back() += g->Advance * scale;
            }
            const float totalHeight = lineHeight * (float)widths.size();
            // Pen at the first line's baseline; whole pixels keep the glyphs crisp.
            const float top = anchor.y + totalHeight * 0.5f;
            float y = std::round(top - font.GetAscent() * scale);
            size_t line = 0;
            float x = std::round(anchor.x - widths[0] * 0.5f);

            auto toClip = [&](float px, float py) {
                return glm::vec3(px / size.x * 2.0f - 1.0f, py / size.y * 2.0f - 1.0f, ndc.z);
            };
            const glm::vec4 shadow(0.0f, 0.0f, 0.0f, 0.8f * item.Color.a);
            for (size_t i = 0; i < item.Text.size();)
            {
                uint32_t cp = DecodeUTF8(item.Text, i);
                if (cp == '\r') continue;
                if (cp == '\n')
                {
                    line++;
                    y -= std::round(lineHeight);
                    x = std::round(anchor.x - widths[std::min(line, widths.size() - 1)] * 0.5f);
                    continue;
                }
                if (cp == '\t') cp = ' ';
                const Glyph* g = font.GetGlyph(cp);
                if (!g) continue;
                if (g->X1 > g->X0)
                {
                    const float x0 = x + g->X0 * scale, x1 = x + g->X1 * scale;
                    const float y0 = y + g->Y0 * scale, y1 = y + g->Y1 * scale;
                    const glm::vec2 uvs[4] = { { g->U0, g->V0 }, { g->U1, g->V0 }, { g->U1, g->V1 }, { g->U0, g->V1 } };
                    for (int pass = 0; pass < 2; pass++)
                    {
                        const float o = pass == 0 ? 1.0f : 0.0f;
                        const glm::vec4& color = pass == 0 ? shadow : item.Color;
                        const glm::vec3 corners[4] = { toClip(x0 + o, y0 - o), toClip(x1 + o, y0 - o), toClip(x1 + o, y1 - o),
                                                       toClip(x0 + o, y1 - o) };
                        for (int k = 0; k < 4; k++)
                            out.push_back({ corners[k], uvs[k], color, anchorInfo });
                    }
                }
                x += g->Advance * scale;
            }
        }

    }

    void DebugDraw::Render(const DebugDrawView& view)
    {
        DebugDrawState& s = State();
        if (!s.Settings.Enabled || view.Width == 0 || view.Height == 0)
            return;
        if (s.Lines.empty() && s.Points.empty() && s.Texts.empty() && s.Quads.empty())
            return;
        ZM_PROFILE_SCOPE("DebugDraw");
        if (!EnsureGPU())
            return;
        DebugDrawGPU& g = GPU();
        s.Settings.Sanitize();
        const DebugDrawSettings& settings = s.Settings;

        const glm::mat4 viewProjection = view.Projection * view.View;
        const glm::mat4 invProjection = glm::inverse(view.Projection);
        const glm::vec2 viewportSize((float)view.Width, (float)view.Height);

        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetCullMode(CullMode::None);
        RenderCommand::SetBlendMode(BlendMode::Premultiplied);
        RenderCommand::BindTextureUnit(0, view.DepthTexture);

        // ---- filled quads, then lines, then points (each in the order they were queued)
        if (!s.Lines.empty() || !s.Points.empty() || !s.Quads.empty())
        {
            for (const DebugDrawQuad& q : s.Quads)
            {
                const float kind = q.DepthTest ? 5.0f : 4.0f;
                for (const glm::vec3& corner : q.Corners)
                    g.Shapes.push_back({ corner, corner, q.Color, { 0.0f, 0.0f, 1.0f, kind } });
            }
            const float width = settings.LineWidth;
            for (const DebugDrawLine& l : s.Lines)
            {
                const float kind = l.DepthTest ? 2.0f : 0.0f;
                g.Shapes.push_back({ l.From, l.To, l.Color, { 0.0f, -1.0f, width, kind } });
                g.Shapes.push_back({ l.From, l.To, l.Color, { 1.0f, -1.0f, width, kind } });
                g.Shapes.push_back({ l.From, l.To, l.Color, { 1.0f, 1.0f, width, kind } });
                g.Shapes.push_back({ l.From, l.To, l.Color, { 0.0f, 1.0f, width, kind } });
            }
            for (const DebugDrawPoint& p : s.Points)
            {
                const float kind = p.DepthTest ? 3.0f : 1.0f;
                const float size = p.Size > 0.0f ? std::min(p.Size, DebugDrawSettings::MaxPointSize * 2.0f) : settings.PointSize;
                g.Shapes.push_back({ p.Position, p.Position, p.Color, { -1.0f, -1.0f, size, kind } });
                g.Shapes.push_back({ p.Position, p.Position, p.Color, { 1.0f, -1.0f, size, kind } });
                g.Shapes.push_back({ p.Position, p.Position, p.Color, { 1.0f, 1.0f, size, kind } });
                g.Shapes.push_back({ p.Position, p.Position, p.Color, { -1.0f, 1.0f, size, kind } });
            }
            g.ShapeShader->Bind();
            g.ShapeShader->SetMat4("u_ViewProjection", viewProjection);
            g.ShapeShader->SetFloat2("u_ViewportSize", viewportSize);
            g.ShapeShader->SetFloat("u_OccludedOpacity", settings.OccludedOpacity);
            SetDepthUniforms(*g.ShapeShader, view, invProjection);
            DrawQuads(g.Shapes, g.ShapeVA, g.ShapeVB);
        }

        // ---- labels
        if (!s.Texts.empty())
        {
            Ref<Font> font = Font::GetDefault();
            if (font && font->IsLoaded())
            {
                for (const DebugDrawText& t : s.Texts)
                    LayoutText(t, *font, viewProjection, view, t.Size > 0.0f ? std::min(t.Size, 256.0f) : settings.TextSize, g.Glyphs);
                if (!g.Glyphs.empty())
                {
                    RenderCommand::BindTextureUnit(1, font->GetAtlasTexture()->GetRendererID());
                    g.TextShader->Bind();
                    g.TextShader->SetInt("u_Atlas", 1);
                    g.TextShader->SetFloat("u_OccludedOpacity", settings.OccludedOpacity);
                    SetDepthUniforms(*g.TextShader, view, invProjection);
                    DrawQuads(g.Glyphs, g.TextVA, g.TextVB);
                    RenderCommand::BindTextureUnit(1, 0);
                }
            }
        }

        RenderCommand::BindTextureUnit(0, 0);
        RenderCommand::SetBlendMode(BlendMode::Alpha);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetDepthTest(true);
    }

    void DebugDraw::Shutdown()
    {
        DebugDrawGPU& g = GPU();
        g = DebugDrawGPU();
    }

#endif

}
