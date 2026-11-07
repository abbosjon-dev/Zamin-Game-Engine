#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstddef>
#include <functional>
#include <string_view>

namespace Zamin {

    class NavGrid;
    class Scene;

    // Where navigation debug shapes go: the editor's overlay (Renderer2D, on top of the picture) or
    // the DebugDraw queue (the game's --debug-nav, depth tested against the scene). What is drawn is
    // the same (NavDebugDraw::DrawScene).
    class NavDebugCanvas
    {
    public:
        virtual ~NavDebugCanvas() = default;
        virtual void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) = 0;
        // A filled flat shape through four corners in order round it.
        virtual void Quad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color) = 0;
        // A round dot `pixels` across on the screen.
        virtual void Dot(const glm::vec3& position, float pixels, const glm::vec4& color) = 0;
        // World units one screen pixel covers at `position` (markers keep their size on screen).
        virtual float PixelSize(const glm::vec3& position) const = 0;
        // A label (canvases without text leave it out).
        virtual void Text(const glm::vec3& /*position*/, std::string_view /*text*/, const glm::vec4& /*color*/) {}
    };

    // A canvas that queues into DebugDraw for this frame (Shipping: nothing). PixelSize from the
    // camera given (the scene's last view: Scene::GetLastView / GetLastProjection and its viewport).
    class DebugDrawNavCanvas : public NavDebugCanvas
    {
    public:
        DebugDrawNavCanvas(const glm::mat4& view, const glm::mat4& projection, float viewportHeight)
            : m_View(view), m_Projection(projection), m_ViewportHeight(viewportHeight) {}
        void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) override;
        void Quad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color) override;
        void Dot(const glm::vec3& position, float pixels, const glm::vec4& color) override;
        float PixelSize(const glm::vec3& position) const override;
        void Text(const glm::vec3& position, std::string_view text, const glm::vec4& color) override;

    private:
        glm::mat4 m_View, m_Projection;
        float m_ViewportHeight;
    };

    // What NavDebugDraw::DrawScene shows.
    struct NavDebugOptions
    {
        bool Grids = true;          // the baked grids' frames and blocked / carved cells
        bool WalkableCells = false; // the walkable cells too (green; Selected grids always)
        bool Agents = true;         // paths, destinations, feelers
        bool Obstacles = true;      // Nav Obstacles' shapes
        bool Sight = true;          // Sight Sensors' cones, what they see, where they last saw it
        bool Patrols = true;        // Patrol Paths' rounds and the point each is heading to
        bool Labels = false;        // waypoint numbers, waits (canvases with text)
        bool Running = true;        // the game runs: its live state (seen targets, the current waypoint)
        size_t MaxWalkableCells = 250000; // bigger grids never draw their walkable cells
        // Which entities are drawn (not set: all active ones) and which are selected (brighter; their
        // grid's walkable cells, their destination when there).
        std::function<bool(entt::entity)> Filter;
        std::function<bool(entt::entity)> Selected;
    };

    namespace NavDebugDraw {

        // A baked grid's cells: blocked (red) and carved by Nav Obstacles (orange) always, walkable
        // ones (green) with `walkable`; runs of equal cells of a row (3D: on one floor height) as one
        // quad. 3D cells without a floor are left out. `alpha` scales the colours.
        void DrawGridCells(NavDebugCanvas& canvas, const NavGrid& grid, bool flat3D, bool walkable, float alpha);

        // Everything of the scene's navigation the options ask for.
        void DrawScene(Scene& scene, NavDebugCanvas& canvas, const NavDebugOptions& options);

    }

}
