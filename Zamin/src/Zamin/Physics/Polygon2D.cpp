#include "Zamin/Physics/Polygon2D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Zamin {

    namespace {

        float Cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

        // The sine of the turn at b (a -> b -> c): > 0 left (counter-clockwise), 0 straight / degenerate.
        float Turn(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
        {
            const glm::vec2 e1 = b - a, e2 = c - b;
            const float l = glm::length(e1) * glm::length(e2);
            return l > 0.0f ? Cross(e1, e2) / l : 0.0f;
        }

        constexpr float StraightTurn = 1e-4f; // |sin| below this: a straight line

        // Orientation of c against the line a -> b: 1 left, -1 right, 0 on it (relative tolerance).
        int Orientation(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
        {
            const float v = Cross(b - a, c - a);
            const float scale = glm::length(b - a) * glm::length(c - a);
            if (std::abs(v) <= 1e-6f * scale)
                return 0;
            return v > 0.0f ? 1 : -1;
        }

        bool OnSegment(const glm::vec2& a, const glm::vec2& b, const glm::vec2& p)
        {
            return std::min(a.x, b.x) - 1e-7f <= p.x && p.x <= std::max(a.x, b.x) + 1e-7f &&
                   std::min(a.y, b.y) - 1e-7f <= p.y && p.y <= std::max(a.y, b.y) + 1e-7f;
        }

        bool SegmentsIntersect(const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& q1, const glm::vec2& q2)
        {
            const int o1 = Orientation(p1, p2, q1), o2 = Orientation(p1, p2, q2);
            const int o3 = Orientation(q1, q2, p1), o4 = Orientation(q1, q2, p2);
            if (o1 != o2 && o3 != o4 && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0)
                return true;
            if (o1 == 0 && OnSegment(p1, p2, q1)) return true;
            if (o2 == 0 && OnSegment(p1, p2, q2)) return true;
            if (o3 == 0 && OnSegment(q1, q2, p1)) return true;
            if (o4 == 0 && OnSegment(q1, q2, p2)) return true;
            return false;
        }

        // Inside or on the counter-clockwise triangle abc.
        bool InTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
        {
            const float eps = -1e-7f * std::max(1.0f, glm::length(b - a) * glm::length(c - a));
            return Cross(b - a, p - a) >= eps && Cross(c - b, p - b) >= eps && Cross(a - c, p - c) >= eps;
        }

        bool SamePoint(const glm::vec2& a, const glm::vec2& b) { return glm::length(a - b) <= 1e-7f; }

        // Ear clipping on a simple counter-clockwise polygon: triangles as indices.
        std::vector<std::array<int, 3>> EarClip(const std::vector<glm::vec2>& p)
        {
            std::vector<std::array<int, 3>> triangles;
            std::vector<int> v((size_t)p.size());
            for (size_t i = 0; i < p.size(); i++)
                v[i] = (int)i;
            size_t start = 0;
            while (v.size() > 3)
            {
                const size_t n = v.size();
                bool clipped = false;
                for (size_t k = 0; k < n && !clipped; k++)
                {
                    const size_t i = (start + k) % n;
                    const int ia = v[(i + n - 1) % n], ib = v[i], ic = v[(i + 1) % n];
                    const glm::vec2 &a = p[(size_t)ia], &b = p[(size_t)ib], &c = p[(size_t)ic];
                    if (Turn(a, b, c) <= StraightTurn)
                        continue; // reflex or straight: not an ear
                    bool empty = true;
                    for (size_t j = 0; j < n && empty; j++)
                    {
                        const int ij = v[j];
                        if (ij == ia || ij == ib || ij == ic)
                            continue;
                        const glm::vec2& q = p[(size_t)ij];
                        if (SamePoint(q, a) || SamePoint(q, b) || SamePoint(q, c))
                            continue;
                        if (InTriangle(q, a, b, c))
                            empty = false;
                    }
                    if (!empty)
                        continue;
                    triangles.push_back({ ia, ib, ic });
                    v.erase(v.begin() + (ptrdiff_t)i);
                    start = i % v.size();
                    clipped = true;
                }
                if (!clipped)
                {
                    // Only straight corners left (or rounding trouble): drop a straight one, else give up.
                    bool dropped = false;
                    for (size_t i = 0; i < n && !dropped; i++)
                        if (std::abs(Turn(p[(size_t)v[(i + n - 1) % n]], p[(size_t)v[i]], p[(size_t)v[(i + 1) % n]])) <= StraightTurn)
                        {
                            v.erase(v.begin() + (ptrdiff_t)i);
                            dropped = true;
                        }
                    if (!dropped)
                        return {};
                }
            }
            if (v.size() == 3 && Turn(p[(size_t)v[0]], p[(size_t)v[1]], p[(size_t)v[2]]) > StraightTurn)
                triangles.push_back({ v[0], v[1], v[2] });
            return triangles;
        }

        bool IsConvexIndices(const std::vector<glm::vec2>& p, const std::vector<int>& poly)
        {
            const size_t n = poly.size();
            for (size_t i = 0; i < n; i++)
                if (Turn(p[(size_t)poly[(i + n - 1) % n]], p[(size_t)poly[i]], p[(size_t)poly[(i + 1) % n]]) < -StraightTurn)
                    return false;
            return true;
        }

        // Corners on a straight line taken out (at least 3 stay).
        void DropStraight(const std::vector<glm::vec2>& p, std::vector<int>& poly)
        {
            for (size_t i = 0; poly.size() > 3 && i < poly.size();)
            {
                const size_t n = poly.size();
                if (std::abs(Turn(p[(size_t)poly[(i + n - 1) % n]], p[(size_t)poly[i]], p[(size_t)poly[(i + 1) % n]])) <= StraightTurn)
                    poly.erase(poly.begin() + (ptrdiff_t)i);
                else
                    i++;
            }
        }

        // Hertel-Mehlhorn: neighbouring pieces (sharing an edge) merged while the result is convex
        // and small enough.
        std::vector<std::vector<int>> MergePieces(const std::vector<glm::vec2>& p, std::vector<std::vector<int>> pieces, int maxVertices)
        {
            bool merged = true;
            while (merged)
            {
                merged = false;
                for (size_t a = 0; a < pieces.size() && !merged; a++)
                    for (size_t b = a + 1; b < pieces.size() && !merged; b++)
                    {
                        const std::vector<int>& A = pieces[a];
                        const std::vector<int>& B = pieces[b];
                        if ((int)(A.size() + B.size()) - 2 > maxVertices + 2) // straight corners may still go
                            continue;
                        for (size_t k = 0; k < A.size() && !merged; k++)
                        {
                            const int a0 = A[k], a1 = A[(k + 1) % A.size()];
                            for (size_t m = 0; m < B.size(); m++)
                            {
                                if (B[m] != a1 || B[(m + 1) % B.size()] != a0)
                                    continue;
                                // A from a1 round to a0, then B's corners between a0 and a1.
                                std::vector<int> joined;
                                for (size_t i = 0; i < A.size(); i++)
                                    joined.push_back(A[(k + 1 + i) % A.size()]);
                                for (size_t i = 2; i < B.size(); i++)
                                    joined.push_back(B[(m + i) % B.size()]);
                                if (!IsConvexIndices(p, joined))
                                    break;
                                DropStraight(p, joined);
                                if ((int)joined.size() > maxVertices)
                                    break;
                                pieces[a] = std::move(joined);
                                pieces.erase(pieces.begin() + (ptrdiff_t)b);
                                merged = true;
                                break;
                            }
                        }
                    }
            }
            return pieces;
        }

        // A convex counter-clockwise outline in fans of at most maxVertices corners (sharing corner 0).
        std::vector<std::vector<glm::vec2>> SplitConvex(const std::vector<glm::vec2>& poly, int maxVertices)
        {
            std::vector<std::vector<glm::vec2>> pieces;
            const int n = (int)poly.size();
            if (n <= maxVertices)
            {
                pieces.push_back(poly);
                return pieces;
            }
            for (int i = 1; i < n - 1;)
            {
                const int end = std::min(i + maxVertices - 2, n - 1);
                std::vector<glm::vec2> piece{ poly[0] };
                for (int j = i; j <= end; j++)
                    piece.push_back(poly[(size_t)j]);
                pieces.push_back(std::move(piece));
                i = end;
            }
            return pieces;
        }

        float PointSegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
        {
            const glm::vec2 ab = b - a;
            const float l2 = glm::dot(ab, ab);
            const float t = l2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
            return glm::length(p - (a + ab * t));
        }

        void DouglasPeucker(const std::vector<glm::vec2>& points, size_t first, size_t last, float tolerance, std::vector<bool>& keep)
        {
            if (last <= first + 1)
                return;
            float best = -1.0f;
            size_t index = first;
            for (size_t i = first + 1; i < last; i++)
            {
                const float d = PointSegmentDistance(points[i], points[first], points[last]);
                if (d > best)
                {
                    best = d;
                    index = i;
                }
            }
            if (best > tolerance)
            {
                keep[index] = true;
                DouglasPeucker(points, first, index, tolerance, keep);
                DouglasPeucker(points, index, last, tolerance, keep);
            }
        }

    }

    // ------------------------------------------------------------------ basics
    float Polygon2D::SignedArea(const std::vector<glm::vec2>& p)
    {
        float area = 0.0f;
        for (size_t i = 0, n = p.size(); i < n; i++)
            area += Cross(p[i], p[(i + 1) % n]);
        return area;
    }

    bool Polygon2D::IsConvex(const std::vector<glm::vec2>& p)
    {
        const size_t n = p.size();
        if (n < 3)
            return false;
        int sign = 0;
        for (size_t i = 0; i < n; i++)
        {
            const float t = Turn(p[(i + n - 1) % n], p[i], p[(i + 1) % n]);
            if (std::abs(t) <= StraightTurn)
                continue;
            const int s = t > 0.0f ? 1 : -1;
            if (sign != 0 && s != sign)
                return false;
            sign = s;
        }
        return sign != 0 && (n == 3 || IsSimple(p)); // a star turns one way too
    }

    bool Polygon2D::IsSimple(const std::vector<glm::vec2>& p)
    {
        const size_t n = p.size();
        if (n < 3)
            return false;
        for (size_t i = 0; i < n; i++)
        {
            const glm::vec2 &a = p[i], &b = p[(i + 1) % n];
            if (SamePoint(a, b))
                return false;
            // Neighbours: they must not fold back onto each other.
            const glm::vec2& c = p[(i + 2) % n];
            if (Orientation(a, b, c) == 0 && glm::dot(b - a, c - b) < 0.0f)
                return false;
            for (size_t j = i + 2; j < n; j++)
            {
                if (i == 0 && j == n - 1)
                    continue; // the closing edge is a neighbour of the first
                if (SegmentsIntersect(a, b, p[j], p[(j + 1) % n]))
                    return false;
            }
        }
        return true;
    }

    std::vector<glm::vec2> Polygon2D::Clean(const std::vector<glm::vec2>& points, float weld)
    {
        std::vector<glm::vec2> p;
        p.reserve(points.size());
        for (const glm::vec2& v : points)
        {
            if (!std::isfinite(v.x) || !std::isfinite(v.y))
                continue;
            if (p.empty() || glm::length(v - p.back()) > weld)
                p.push_back(v);
        }
        while (p.size() > 1 && glm::length(p.front() - p.back()) <= weld)
            p.pop_back();
        bool changed = true;
        while (changed && p.size() >= 3)
        {
            changed = false;
            for (size_t i = 0; i < p.size() && p.size() >= 3; i++)
            {
                const size_t n = p.size();
                const glm::vec2 &a = p[(i + n - 1) % n], &b = p[i], &c = p[(i + 1) % n];
                // Straight on, or a spike that goes back the way it came.
                if (std::abs(Turn(a, b, c)) <= StraightTurn || glm::length(c - a) <= weld)
                {
                    p.erase(p.begin() + (ptrdiff_t)i);
                    changed = true;
                    break;
                }
            }
        }
        return p;
    }

    std::vector<glm::vec2> Polygon2D::ConvexHull(std::vector<glm::vec2> points)
    {
        std::erase_if(points, [](const glm::vec2& v) { return !std::isfinite(v.x) || !std::isfinite(v.y); });
        std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
        points.erase(std::unique(points.begin(), points.end()), points.end());
        if (points.size() < 3)
            return points;
        std::vector<glm::vec2> hull(points.size() * 2);
        size_t k = 0;
        for (size_t i = 0; i < points.size(); i++)
        {
            while (k >= 2 && Cross(hull[k - 1] - hull[k - 2], points[i] - hull[k - 2]) <= 0.0f)
                k--;
            hull[k++] = points[i];
        }
        for (size_t i = points.size() - 1, t = k + 1; i > 0; i--)
        {
            while (k >= t && Cross(hull[k - 1] - hull[k - 2], points[i - 1] - hull[k - 2]) <= 0.0f)
                k--;
            hull[k++] = points[i - 1];
        }
        hull.resize(k - 1);
        return hull;
    }

    std::vector<glm::vec2> Polygon2D::SimplifyConvex(std::vector<glm::vec2> hull, int maxVertices)
    {
        maxVertices = std::max(3, maxVertices);
        while ((int)hull.size() > maxVertices)
        {
            const size_t n = hull.size();
            size_t best = 0;
            float bestArea = std::numeric_limits<float>::max();
            for (size_t i = 0; i < n; i++)
            {
                const float area = std::abs(Cross(hull[i] - hull[(i + n - 1) % n], hull[(i + 1) % n] - hull[(i + n - 1) % n]));
                if (area < bestArea)
                {
                    bestArea = area;
                    best = i;
                }
            }
            hull.erase(hull.begin() + (ptrdiff_t)best);
        }
        return hull;
    }

    std::vector<std::vector<glm::vec2>> Polygon2D::Triangulate(const std::vector<glm::vec2>& points)
    {
        std::vector<glm::vec2> p = Clean(points);
        std::vector<std::vector<glm::vec2>> out;
        if (p.size() < 3 || !IsSimple(p))
            return out;
        if (SignedArea(p) < 0.0f)
            std::reverse(p.begin(), p.end());
        for (const auto& t : EarClip(p))
            out.push_back({ p[(size_t)t[0]], p[(size_t)t[1]], p[(size_t)t[2]] });
        return out;
    }

    // ------------------------------------------------------------------ decomposition
    Polygon2D::Decomposition Polygon2D::Decompose(const std::vector<glm::vec2>& points, int maxVertices)
    {
        maxVertices = std::clamp(maxVertices, 3, MaxPieceVertices);
        Decomposition out;
        std::vector<glm::vec2> p = Clean(points);
        if (p.size() < 3)
            return out;
        std::vector<std::vector<glm::vec2>> pieces;
        auto useHull = [&]() {
            pieces = SplitConvex(SimplifyConvex(ConvexHull(p), 64), maxVertices);
            out.Result = DecomposeResult::SelfIntersecting;
        };
        // (A crossing outline's lobes cancel out in the signed area: tested first.)
        const bool simple = IsSimple(p);
        if (simple && std::abs(SignedArea(p)) * 0.5f < MinPieceArea)
            return out;
        if (SignedArea(p) < 0.0f)
            std::reverse(p.begin(), p.end());
        if (!simple)
            useHull();
        else if (IsConvex(p))
        {
            pieces = SplitConvex(p, maxVertices);
            out.Result = DecomposeResult::Convex;
        }
        else
        {
            std::vector<std::array<int, 3>> triangles = EarClip(p);
            if (triangles.empty())
                useHull();
            else
            {
                std::vector<std::vector<int>> indexed;
                for (const auto& t : triangles)
                    indexed.push_back({ t[0], t[1], t[2] });
                for (const std::vector<int>& piece : MergePieces(p, std::move(indexed), maxVertices))
                {
                    std::vector<glm::vec2> poly;
                    for (int i : piece)
                        poly.push_back(p[(size_t)i]);
                    pieces.push_back(std::move(poly));
                }
                out.Result = pieces.size() == 1 ? DecomposeResult::Convex : DecomposeResult::Decomposed;
            }
        }
        for (const auto& piece : pieces)
        {
            std::vector<glm::vec2> valid = MakeConvexPiece(piece);
            if (!valid.empty())
                out.Pieces.push_back(std::move(valid));
        }
        if (out.Pieces.empty())
            out.Result = DecomposeResult::Degenerate;
        return out;
    }

    std::vector<glm::vec2> Polygon2D::MakeConvexPiece(const std::vector<glm::vec2>& points)
    {
        std::vector<glm::vec2> welded;
        for (const glm::vec2& v : points)
        {
            if (!std::isfinite(v.x) || !std::isfinite(v.y))
                continue;
            bool unique = true;
            for (const glm::vec2& w : welded)
                if (glm::length(v - w) <= WeldDistance)
                {
                    unique = false;
                    break;
                }
            if (unique)
                welded.push_back(v);
        }
        std::vector<glm::vec2> hull = ConvexHull(std::move(welded));
        if (hull.size() < 3)
            return {};
        hull = SimplifyConvex(Clean(hull), MaxPieceVertices);
        if (hull.size() < 3 || SignedArea(hull) * 0.5f < MinPieceArea)
            return {};
        for (size_t i = 0; i < hull.size(); i++)
            if (glm::length(hull[(i + 1) % hull.size()] - hull[i]) <= WeldDistance)
                return {};
        return hull;
    }

    std::vector<glm::vec2> Polygon2D::CleanPolyline(const std::vector<glm::vec2>& points, bool loop, float weld)
    {
        std::vector<glm::vec2> p;
        for (const glm::vec2& v : points)
        {
            if (!std::isfinite(v.x) || !std::isfinite(v.y))
                continue;
            if (p.empty() || glm::length(v - p.back()) > weld)
                p.push_back(v);
        }
        if (loop)
            while (p.size() > 1 && glm::length(p.front() - p.back()) <= weld)
                p.pop_back();
        if (p.size() < (loop ? 3u : 2u))
            return {};
        return p;
    }

    // ------------------------------------------------------------------ fit to a picture
    std::vector<glm::vec2> Polygon2D::SimplifyLoop(const std::vector<glm::vec2>& loop, float tolerance)
    {
        const size_t n = loop.size();
        if (n <= 3)
            return loop;
        // Split at the two points furthest apart (from point 0 and the one furthest from it).
        size_t far = 0;
        float best = -1.0f;
        for (size_t i = 1; i < n; i++)
            if (float d = glm::length(loop[i] - loop[0]); d > best)
            {
                best = d;
                far = i;
            }
        std::vector<glm::vec2> path(loop.begin(), loop.end());
        path.push_back(loop[0]);
        std::vector<bool> keep(path.size(), false);
        keep[0] = keep[far] = keep[path.size() - 1] = true;
        DouglasPeucker(path, 0, far, tolerance, keep);
        DouglasPeucker(path, far, path.size() - 1, tolerance, keep);
        std::vector<glm::vec2> out;
        for (size_t i = 0; i + 1 < path.size(); i++)
            if (keep[i])
                out.push_back(path[i]);
        return out;
    }

    std::vector<glm::vec2> Polygon2D::TraceAlphaOutline(const uint8_t* rgba, int width, int height, int x0, int y0, int x1, int y1,
                                                        uint8_t alphaThreshold, int maxVertices)
    {
        maxVertices = std::max(3, maxVertices);
        x0 = std::clamp(x0, 0, width); x1 = std::clamp(x1, 0, width);
        y0 = std::clamp(y0, 0, height); y1 = std::clamp(y1, 0, height);
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        const int w = x1 - x0, h = y1 - y0;
        if (!rgba || w <= 0 || h <= 0)
            return {};
        // A padded grid: the border cells are always empty.
        const int W = w + 2, H = h + 2;
        auto at = [W](int x, int y) { return (size_t)y * (size_t)W + (size_t)x; };
        std::vector<uint8_t> opaque((size_t)W * H, 0);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                opaque[at(x + 1, y + 1)] = rgba[(((size_t)(y0 + y) * (size_t)width) + (size_t)(x0 + x)) * 4 + 3] > alphaThreshold;

        // The largest 4-connected island.
        std::vector<int> label((size_t)W * H, -1);
        std::vector<size_t> stack;
        int bestLabel = -1;
        size_t bestSize = 0;
        int labels = 0;
        const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        for (int y = 1; y <= h; y++)
            for (int x = 1; x <= w; x++)
            {
                if (!opaque[at(x, y)] || label[at(x, y)] >= 0)
                    continue;
                size_t size = 0;
                stack.push_back(at(x, y));
                label[at(x, y)] = labels;
                while (!stack.empty())
                {
                    const size_t c = stack.back();
                    stack.pop_back();
                    size++;
                    const int cx = (int)(c % (size_t)W), cy = (int)(c / (size_t)W);
                    for (int d = 0; d < 4; d++)
                    {
                        const size_t nb = at(cx + dx[d], cy + dy[d]);
                        if (opaque[nb] && label[nb] < 0)
                        {
                            label[nb] = labels;
                            stack.push_back(nb);
                        }
                    }
                }
                if (size > bestSize)
                {
                    bestSize = size;
                    bestLabel = labels;
                }
                labels++;
            }
        if (bestLabel < 0)
            return {};
        // Solid: the island and its holes (whatever the outside cannot reach in 4 steps).
        std::vector<uint8_t> outside((size_t)W * H, 0);
        stack.push_back(0);
        outside[0] = 1;
        while (!stack.empty())
        {
            const size_t c = stack.back();
            stack.pop_back();
            const int cx = (int)(c % (size_t)W), cy = (int)(c / (size_t)W);
            for (int d = 0; d < 4; d++)
            {
                const int nx = cx + dx[d], ny = cy + dy[d];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H)
                    continue;
                const size_t nb = at(nx, ny);
                if (!outside[nb] && label[nb] != bestLabel)
                {
                    outside[nb] = 1;
                    stack.push_back(nb);
                }
            }
        }
        auto solid = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && !outside[at(x, y)]; };

        // The outline along the pixel edges (rows down): the solid on the right of each edge.
        // Corner (cx, cy) -> the next corner.
        auto key = [](int x, int y) { return ((int64_t)x << 32) ^ (int64_t)(uint32_t)y; };
        std::unordered_map<int64_t, glm::ivec2> next;
        glm::ivec2 start(-1);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                if (!solid(x, y))
                    continue;
                if (!solid(x, y - 1)) { next[key(x, y)] = { x + 1, y }; if (start.x < 0) start = { x, y }; }
                if (!solid(x + 1, y)) next[key(x + 1, y)] = { x + 1, y + 1 };
                if (!solid(x, y + 1)) next[key(x + 1, y + 1)] = { x, y + 1 };
                if (!solid(x - 1, y)) next[key(x, y + 1)] = { x, y };
            }
        if (start.x < 0)
            return {};
        std::vector<glm::vec2> loop;
        glm::ivec2 c = start;
        for (size_t guard = 0; guard <= next.size(); guard++)
        {
            // In the region's unit square, y up; the padding taken off.
            loop.push_back({ (float)(c.x - 1) / (float)w, 1.0f - (float)(c.y - 1) / (float)h });
            auto it = next.find(key(c.x, c.y));
            if (it == next.end())
                break;
            c = it->second;
            if (c == start)
                break;
        }
        // Straight runs to their ends; y up turns the clockwise (rows down) trace counter-clockwise.
        std::vector<glm::vec2> corners = Clean(loop, 1e-6f);
        if (SignedArea(corners) < 0.0f)
            std::reverse(corners.begin(), corners.end());
        if (corners.size() < 3)
            return {};

        // Simplified in pixels (the tolerance grows until it is small enough).
        const glm::vec2 pixels((float)w, (float)h);
        std::vector<glm::vec2> scaled;
        for (const glm::vec2& v : corners)
            scaled.push_back(v * pixels);
        std::vector<glm::vec2> simple = scaled;
        for (float tolerance = 0.75f; tolerance < 4.0f * std::max(pixels.x, pixels.y); tolerance *= 1.35f)
        {
            simple = Clean(SimplifyLoop(scaled, tolerance), 1e-6f);
            if ((int)simple.size() <= maxVertices)
                break;
        }
        if (simple.size() < 3 || (int)simple.size() > maxVertices || !IsSimple(simple) || std::abs(SignedArea(simple)) < 1e-3f)
            simple = SimplifyConvex(ConvexHull(scaled), maxVertices);
        for (glm::vec2& v : simple)
            v /= pixels;
        if (SignedArea(simple) < 0.0f)
            std::reverse(simple.begin(), simple.end());
        return simple;
    }

}
