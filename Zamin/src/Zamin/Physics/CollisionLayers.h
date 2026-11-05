#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // The 16 named collision layers of 2D physics (Project settings > Physics layers) and which
    // pairs of them collide. Every 2D body / collider / tilemap is on one layer; two colliders touch
    // only when the matrix lets their layers collide (Box2D filter: category = the layer's bit, mask
    // = the layers it collides with). Queries (Raycast2D) take a mask of layers too.
    struct CollisionLayers2D
    {
        static constexpr int Count = 16;
        static constexpr uint16_t AllLayers = 0xFFFF;

        // "" = unused (shown as "Qatlam N"); layer 0 is "Default".
        std::array<std::string, Count> Names;
        // Bit b of Masks[a]: layers a and b collide (kept symmetric).
        std::array<uint16_t, Count> Masks;

        CollisionLayers2D()
        {
            Names[0] = "Default";
            Masks.fill(AllLayers);
        }

        static bool IsValid(int layer) { return layer >= 0 && layer < Count; }
        static int Clamp(int layer) { return layer < 0 ? 0 : layer >= Count ? Count - 1 : layer; }

        // The name, or "Qatlam N" for an unnamed one.
        std::string GetName(int layer) const;
        bool IsNamed(int layer) const { return IsValid(layer) && !Names[(size_t)layer].empty(); }
        // The layer with this name (case sensitive), -1 when there is none.
        int Find(std::string_view name) const;
        // The layers with a name, in order (Default first).
        std::vector<int> GetNamedLayers() const;

        bool CanCollide(int a, int b) const
        {
            return IsValid(a) && IsValid(b) && ((Masks[(size_t)a] >> b) & 1u) != 0;
        }
        void SetCollision(int a, int b, bool collide);
        // Box2D's rule for two fixtures' filter bits (b2ContactFilter): each one's layer is in the
        // other's mask.
        static bool FiltersCollide(uint16_t categoryA, uint16_t maskA, uint16_t categoryB, uint16_t maskB)
        {
            return (categoryA & maskB) != 0 && (categoryB & maskA) != 0;
        }
        // Box2D filter bits of a body / collider on `layer`.
        static uint16_t GetCategoryBits(int layer) { return (uint16_t)(1u << Clamp(layer)); }
        uint16_t GetMaskBits(int layer) const { return Masks[(size_t)Clamp(layer)]; }
        // The bits of the named layers (unknown names are left out): masks for queries.
        uint16_t MakeMask(std::initializer_list<std::string_view> names) const;

        bool IsDefault() const { return *this == CollisionLayers2D(); }
        bool operator==(const CollisionLayers2D&) const = default;

        // { "Names": [16 strings], "Collisions": [16 masks] }; missing / bad parts keep the defaults
        // and the matrix is made symmetric (a pair collides when both rows say so).
        std::string ToJson() const;
        static bool FromJson(std::string_view text, CollisionLayers2D& out);
    };

}
