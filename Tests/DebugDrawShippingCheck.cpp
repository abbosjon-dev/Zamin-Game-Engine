// The DebugDraw API as a Shipping build compiles it (ZM_DEBUG_DRAW 0): every call is an empty inline
// function in another inline namespace, so it neither clashes with the engine's DebugDraw (built
// with debug drawing) nor touches its queue. ZaminTests calls DrawEverything().
#undef ZM_DEBUG_DRAW
#define ZM_DEBUG_DRAW 0
#include <Zamin/Debug/DebugDraw.h>
#include <Zamin/Renderer/Mesh.h>

namespace ShippingCheck {

    using namespace Zamin;

    static_assert(!DebugDraw::IsCompiledIn(), "Shipping: DebugDraw must be compiled out");

    // Calls the whole API; returns what the disabled API reports (0 when it does nothing).
    int DrawEverything()
    {
        const glm::vec3 p(1.0f, 2.0f, 3.0f);
        DebugDraw::Line(p, p * 2.0f, DebugColor::Red, 1.0f, false);
        DebugDraw::Ray(p, glm::vec3(0, 1, 0));
        DebugDraw::Arrow(p, p + glm::vec3(1.0f), DebugColor::Green, 2.0f, true, 0.3f);
        DebugDraw::Box(p, glm::vec3(1.0f));
        DebugDraw::Box(p, glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), DebugColor::Blue);
        DebugDraw::Box(glm::mat4(1.0f), DebugColor::Cyan, 0.5f);
        DebugDraw::Bounds(AABB{ glm::vec3(-1.0f), glm::vec3(1.0f) });
        DebugDraw::Rect(p, glm::vec2(2.0f, 1.0f));
        DebugDraw::WireSphere(p, 2.0f, DebugColor::Yellow, 3.0f);
        DebugDraw::Sphere(p, 1.0f);
        DebugDraw::Circle(p, glm::vec3(0, 1, 0), 1.0f);
        DebugDraw::Circle(p, 1.0f, DebugColor::Magenta);
        DebugDraw::Cross(p, 0.5f);
        DebugDraw::Point(p, DebugColor::Orange, 0.0f, true, 6.0f);
        DebugDraw::Axes(glm::mat4(1.0f), 2.0f);
        DebugDraw::Path({ p, p * 2.0f, p * 3.0f }, DebugColor::Gray, 0.0f, true, true);
        DebugDraw::Text(p, "Shipping", DebugColor::White, 1.0f, true, 20.0f);
        DebugDraw::PhysicsRaycast(p, glm::vec3(0, -1, 0), 10.0f, RaycastHit{});
        DebugDraw::PhysicsContact(p, glm::vec3(0, 1, 0));
        DebugDraw::SetEnabled(true);
        DebugDraw::GetSettings().ShowRaycasts = true;
        DebugDraw::Advance(1.0f);
        DebugDraw::Render(DebugDrawView{});
        DebugDraw::Clear();
        DebugDraw::Shutdown();
        return (int)(DebugDraw::GetLines().size() + DebugDraw::GetPoints().size() + DebugDraw::GetTexts().size()) +
               (DebugDraw::IsEnabled() ? 1 : 0) + (DebugDraw::IsShowingRaycasts() ? 1 : 0) + (DebugDraw::IsShowingContacts() ? 1 : 0) +
               (DebugDraw::GetSettings().Enabled ? 1 : 0) + (int)DebugDraw::MaxLines;
    }

}
