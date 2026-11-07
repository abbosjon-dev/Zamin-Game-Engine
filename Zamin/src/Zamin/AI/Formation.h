#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

// Formations: where the members of a group stand around their leader (FormationComponent moves
// them; the math is here). Slots are offsets in the leader's frame on its ground plane - x to its
// right, y ahead - turned into the world with the formation's heading:
//
//   std::vector<glm::vec2> offsets;
//   Formation::ComputeOffsets({ FormationShape::Wedge, 1.5f }, 6, offsets);
//   for (const glm::vec2& o : offsets)
//       slots.push_back(Formation::ToWorld(leader, forward, right, o));
//   Formation::AssignSlots(memberPositions, slots, slotToMember); // the least walking in all
namespace Zamin {

    // Line: side by side with the leader (alternately right and left of it); Column: in a file
    // behind it; Wedge: a V behind it, the leader at its tip; Circle: a ring round it (Spacing apart
    // along the ring, at least Spacing from the leader).
    enum class FormationShape : uint8_t { Line = 0, Column, Wedge, Circle };
    const char* FormationShapeName(FormationShape shape); // "Line", "Column", "Wedge", "Circle"

    struct FormationLayout
    {
        FormationShape Shape = FormationShape::Wedge;
        float Spacing = 1.5f;     // between neighbouring members (world units)
        float WedgeAngle = 90.0f; // degrees: how wide the V opens (10..170)
    };

    namespace Formation {

        static constexpr int MaxSlots = 256;

        // The offsets of `count` slots (0..MaxSlots), in the leader's frame (x right, y ahead).
        void ComputeOffsets(const FormationLayout& layout, int count, std::vector<glm::vec2>& out);
        // The ring's radius for `count` members (Circle).
        float CircleRadius(float spacing, int count);
        // An offset in the world (the plane's coordinates): leader + right * x + forward * y.
        inline glm::vec2 ToWorld(const glm::vec2& leader, const glm::vec2& forward, const glm::vec2& right, const glm::vec2& offset)
        {
            return leader + right * offset.x + forward * offset.y;
        }

        // Which member takes which slot: slotToMember[slot] = index into `members` - the assignment
        // with the least total (straight-line) distance between members and their slots (the
        // Hungarian method; above 64 the nearest free slot, slot by slot). Members and slots of the
        // same count; otherwise each slot gets a member while there are members (-1: none) - every
        // member at most once.
        void AssignSlots(std::span<const glm::vec2> members, std::span<const glm::vec2> slots, std::vector<int>& slotToMember);
        // The sum of the distances of an assignment (tests, debugging).
        float AssignmentCost(std::span<const glm::vec2> members, std::span<const glm::vec2> slots, const std::vector<int>& slotToMember);

    }

}
