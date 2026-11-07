#include "Zamin/AI/Formation.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Zamin {

    const char* FormationShapeName(FormationShape shape)
    {
        switch (shape)
        {
            case FormationShape::Line: return "Line";
            case FormationShape::Column: return "Column";
            case FormationShape::Wedge: return "Wedge";
            case FormationShape::Circle: return "Circle";
        }
        return "Wedge";
    }

    float Formation::CircleRadius(float spacing, int count)
    {
        const float s = std::isfinite(spacing) ? std::max(0.0f, spacing) : 0.0f;
        return std::max(s, (float)std::max(count, 1) * s / glm::two_pi<float>());
    }

    void Formation::ComputeOffsets(const FormationLayout& layout, int count, std::vector<glm::vec2>& out)
    {
        out.clear();
        count = std::clamp(count, 0, MaxSlots);
        const float spacing = std::isfinite(layout.Spacing) ? std::max(0.0f, layout.Spacing) : 1.0f;
        out.reserve((size_t)count);
        switch (layout.Shape)
        {
            case FormationShape::Line:
                // Right, left, further right, further left ... of the leader.
                for (int i = 0; i < count; i++)
                    out.emplace_back((i % 2 == 0 ? 1.0f : -1.0f) * (float)(i / 2 + 1) * spacing, 0.0f);
                break;
            case FormationShape::Column:
                for (int i = 0; i < count; i++)
                    out.emplace_back(0.0f, -(float)(i + 1) * spacing);
                break;
            case FormationShape::Wedge:
            {
                // Two wings back from the leader, `spacing` apart along each.
                const float angle = std::isfinite(layout.WedgeAngle) ? std::clamp(layout.WedgeAngle, 10.0f, 170.0f) : 90.0f;
                const float half = glm::radians(angle) * 0.5f;
                const float sx = std::sin(half) * spacing, sy = std::cos(half) * spacing;
                for (int i = 0; i < count; i++)
                {
                    const float row = (float)(i / 2 + 1);
                    out.emplace_back((i % 2 == 0 ? 1.0f : -1.0f) * row * sx, -row * sy);
                }
                break;
            }
            case FormationShape::Circle:
            {
                // Evenly round the ring, the first one ahead, going clockwise seen from above.
                const float radius = CircleRadius(spacing, count);
                for (int i = 0; i < count; i++)
                {
                    const float a = glm::two_pi<float>() * (float)i / (float)count;
                    out.emplace_back(std::sin(a) * radius, std::cos(a) * radius);
                }
                break;
            }
        }
    }

    namespace {

        // The Hungarian method for an n x m cost matrix (n <= m, row major): rowToColumn[i] for every row.
        void Hungarian(const std::vector<double>& cost, int n, int m, std::vector<int>& rowToColumn)
        {
            const double inf = std::numeric_limits<double>::infinity();
            std::vector<double> u((size_t)n + 1, 0.0), v((size_t)m + 1, 0.0), minv((size_t)m + 1);
            std::vector<int> p((size_t)m + 1, 0), way((size_t)m + 1, 0);
            std::vector<char> used((size_t)m + 1);
            for (int i = 1; i <= n; i++)
            {
                p[0] = i;
                int j0 = 0;
                std::fill(minv.begin(), minv.end(), inf);
                std::fill(used.begin(), used.end(), (char)0);
                do
                {
                    used[(size_t)j0] = 1;
                    const int i0 = p[(size_t)j0];
                    int j1 = 0;
                    double delta = inf;
                    for (int j = 1; j <= m; j++)
                    {
                        if (used[(size_t)j])
                            continue;
                        const double current = cost[(size_t)(i0 - 1) * (size_t)m + (size_t)(j - 1)] - u[(size_t)i0] - v[(size_t)j];
                        if (current < minv[(size_t)j])
                        {
                            minv[(size_t)j] = current;
                            way[(size_t)j] = j0;
                        }
                        if (minv[(size_t)j] < delta)
                        {
                            delta = minv[(size_t)j];
                            j1 = j;
                        }
                    }
                    for (int j = 0; j <= m; j++)
                    {
                        if (used[(size_t)j])
                        {
                            u[(size_t)p[(size_t)j]] += delta;
                            v[(size_t)j] -= delta;
                        }
                        else
                            minv[(size_t)j] -= delta;
                    }
                    j0 = j1;
                } while (p[(size_t)j0] != 0);
                do
                {
                    const int j1 = way[(size_t)j0];
                    p[(size_t)j0] = p[(size_t)j1];
                    j0 = j1;
                } while (j0 != 0);
            }
            rowToColumn.assign((size_t)n, -1);
            for (int j = 1; j <= m; j++)
                if (p[(size_t)j] > 0)
                    rowToColumn[(size_t)(p[(size_t)j] - 1)] = j - 1;
        }

    }

    void Formation::AssignSlots(std::span<const glm::vec2> members, std::span<const glm::vec2> slots, std::vector<int>& slotToMember)
    {
        slotToMember.assign(slots.size(), -1);
        const int memberCount = (int)members.size(), slotCount = (int)slots.size();
        if (memberCount == 0 || slotCount == 0)
            return;
        auto distance = [&](int member, int slot) {
            const float d = glm::length(members[(size_t)member] - slots[(size_t)slot]);
            return std::isfinite(d) ? (double)d : 1e12;
        };
        if (std::max(memberCount, slotCount) > 64)
        {
            // Big groups: slot by slot, the nearest member still free.
            std::vector<char> taken((size_t)memberCount, 0);
            for (int s = 0; s < slotCount; s++)
            {
                int best = -1;
                double bestDistance = std::numeric_limits<double>::infinity();
                for (int m = 0; m < memberCount; m++)
                    if (!taken[(size_t)m] && distance(m, s) < bestDistance)
                    {
                        bestDistance = distance(m, s);
                        best = m;
                    }
                if (best < 0)
                    break;
                taken[(size_t)best] = 1;
                slotToMember[(size_t)s] = best;
            }
            return;
        }
        std::vector<int> assignment;
        if (slotCount <= memberCount)
        {
            // Rows: the slots.
            std::vector<double> cost((size_t)slotCount * (size_t)memberCount);
            for (int s = 0; s < slotCount; s++)
                for (int m = 0; m < memberCount; m++)
                    cost[(size_t)s * (size_t)memberCount + (size_t)m] = distance(m, s);
            Hungarian(cost, slotCount, memberCount, assignment);
            for (int s = 0; s < slotCount; s++)
                slotToMember[(size_t)s] = assignment[(size_t)s];
        }
        else
        {
            // Rows: the members (some slots stay empty).
            std::vector<double> cost((size_t)memberCount * (size_t)slotCount);
            for (int m = 0; m < memberCount; m++)
                for (int s = 0; s < slotCount; s++)
                    cost[(size_t)m * (size_t)slotCount + (size_t)s] = distance(m, s);
            Hungarian(cost, memberCount, slotCount, assignment);
            for (int m = 0; m < memberCount; m++)
                if (assignment[(size_t)m] >= 0)
                    slotToMember[(size_t)assignment[(size_t)m]] = m;
        }
    }

    float Formation::AssignmentCost(std::span<const glm::vec2> members, std::span<const glm::vec2> slots, const std::vector<int>& slotToMember)
    {
        float total = 0.0f;
        for (size_t s = 0; s < slotToMember.size() && s < slots.size(); s++)
            if (const int m = slotToMember[s]; m >= 0 && (size_t)m < members.size())
                total += glm::length(members[(size_t)m] - slots[s]);
        return total;
    }

}
