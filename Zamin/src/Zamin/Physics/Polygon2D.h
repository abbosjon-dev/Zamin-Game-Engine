#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Zamin {

    // 2D polygon helpers for the physics shapes (PolygonCollider2D, EdgeCollider2D) and the editor's
    // "fit to sprite". Pure geometry: no Box2D, no GPU.
    struct Polygon2D
    {
        // Box2D's limits: at most this many vertices per convex piece, points closer than this are one.
        static constexpr int MaxPieceVertices = 8;
        static constexpr float WeldDistance = 0.01f;
        static constexpr float MinPieceArea = 1e-4f;

        // Twice the signed area: positive for counter-clockwise points.
        static float SignedArea(const std::vector<glm::vec2>& points);
        static bool IsCounterClockwise(const std::vector<glm::vec2>& points) { return SignedArea(points) > 0.0f; }
        // Every corner turns the same way (collinear corners allowed); false below 3 points.
        static bool IsConvex(const std::vector<glm::vec2>& points);
        // No two edges cross or touch (except neighbours at their shared corner).
        static bool IsSimple(const std::vector<glm::vec2>& points);

        // The outline without repeated points (closer than `weld`, the closing one too) and without
        // corners on a straight line.
        static std::vector<glm::vec2> Clean(const std::vector<glm::vec2>& points, float weld = WeldDistance);
        // The convex hull, counter-clockwise, without collinear points (monotone chain).
        static std::vector<glm::vec2> ConvexHull(std::vector<glm::vec2> points);
        // A convex hull with at most `maxVertices` corners: the corners whose removal loses the least
        // area go first (the result lies inside the hull).
        static std::vector<glm::vec2> SimplifyConvex(std::vector<glm::vec2> hull, int maxVertices);

        // Triangles of a simple polygon (ear clipping), counter-clockwise; empty when it is not one.
        static std::vector<std::vector<glm::vec2>> Triangulate(const std::vector<glm::vec2>& points);

        enum class DecomposeResult
        {
            Convex = 0,     // one convex piece (or a convex outline split into pieces of MaxPieceVertices)
            Decomposed,     // concave: several convex pieces
            SelfIntersecting, // the outline crosses itself: its convex hull is used instead
            Degenerate      // fewer than 3 usable points / no area: nothing
        };
        struct Decomposition
        {
            std::vector<std::vector<glm::vec2>> Pieces; // convex, counter-clockwise, 3..maxVertices points each
            DecomposeResult Result = DecomposeResult::Degenerate;
        };
        // Convex pieces of at most `maxVertices` corners that cover the polygon (either winding):
        // ear clipping, then neighbouring pieces merged while they stay convex (Hertel-Mehlhorn).
        static Decomposition Decompose(const std::vector<glm::vec2>& points, int maxVertices = MaxPieceVertices);

        // A piece Box2D accepts: its hull with welded points; empty when it is too small / thin.
        static std::vector<glm::vec2> MakeConvexPiece(const std::vector<glm::vec2>& points);

        // A polyline / loop for an edge or chain shape: consecutive points closer than `weld` merged
        // (for a loop also the last and the first); fewer than 2 (loop: 3) points: empty.
        static std::vector<glm::vec2> CleanPolyline(const std::vector<glm::vec2>& points, bool loop, float weld = WeldDistance);

        // ---- clipping (buoyancy: the part of a shape under a fluid's surface)
        // The part of `subject` on the side of the line through `point` that `normal` points away
        // from: dot(p - point, normal) <= 0 (Sutherland-Hodgman). Keeps the winding; empty when
        // nothing is left.
        static std::vector<glm::vec2> ClipHalfPlane(const std::vector<glm::vec2>& subject, const glm::vec2& point, const glm::vec2& normal);
        // The intersection of `subject` (any simple polygon) with the convex polygon `clip` (either
        // winding), counter-clockwise; empty when they do not overlap.
        static std::vector<glm::vec2> ClipConvex(const std::vector<glm::vec2>& subject, const std::vector<glm::vec2>& clip);
        // The area (positive) and the centroid of a simple polygon; false without area.
        static bool AreaCentroid(const std::vector<glm::vec2>& points, float& area, glm::vec2& centroid);
        // A circle as a regular polygon with the circle's area (its corners just outside it),
        // counter-clockwise.
        static std::vector<glm::vec2> CirclePolygon(const glm::vec2& center, float radius, int segments = 24);

        // ---- fitting a shape to a picture
        // The outline of the opaque part of an RGBA image region: the largest 4-connected island of
        // pixels whose alpha is above `alphaThreshold` (its holes filled), traced along the pixel
        // edges and simplified (Douglas-Peucker) to at most `maxVertices` corners. Points are in the
        // region's unit square: (0, 0) its bottom-left, (1, 1) its top-right corner (rows of `rgba`
        // go top to bottom). When the outline crosses itself after the simplification, the convex
        // hull of the island (at most `maxVertices` corners). Empty: nothing opaque.
        static std::vector<glm::vec2> TraceAlphaOutline(const uint8_t* rgba, int width, int height, int x0, int y0, int x1, int y1,
                                                        uint8_t alphaThreshold = 16, int maxVertices = 12);

        // Douglas-Peucker for a closed loop: corners within `tolerance` of the simplified outline go.
        static std::vector<glm::vec2> SimplifyLoop(const std::vector<glm::vec2>& loop, float tolerance);
    };

}
