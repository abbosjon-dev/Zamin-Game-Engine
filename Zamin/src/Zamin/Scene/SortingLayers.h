#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // A named sorting layer (Project settings > Sorting layers). 2D renderers (sprites, circles,
    // text, tilemaps, particles) name the layer they belong to by its ID, which stays the same when
    // the layer is renamed or moved; the list's order is the drawing order (the first layer is
    // drawn first, at the back).
    struct SortingLayer
    {
        uint32_t ID = 0;
        std::string Name;
        // Top-down games: inside this layer what stands higher on the screen (a larger world Y) is
        // drawn first, behind what stands lower (see SortKey2D).
        bool YSort = false;
        bool operator==(const SortingLayer&) const = default;
    };

    class SortingLayerList
    {
    public:
        static constexpr uint32_t DefaultID = 0;
        static constexpr std::string_view DefaultName = "Default";
        static constexpr size_t MaxLayers = 64;

        // Only the "Default" layer.
        SortingLayerList();

        const std::vector<SortingLayer>& GetLayers() const { return m_Layers; }
        size_t GetCount() const { return m_Layers.size(); }
        // Drawing position of a layer; an unknown ID (a deleted layer) counts as "Default".
        int GetIndex(uint32_t id) const;
        const SortingLayer* Find(uint32_t id) const;
        const SortingLayer* FindByName(std::string_view name) const;
        // The layer's name ("Default" for an unknown ID).
        const std::string& GetName(uint32_t id) const;
        bool Contains(uint32_t id) const { return Find(id) != nullptr; }
        // The layer sorts by Y (an unknown ID: Default's setting).
        bool IsYSorted(uint32_t id) const;
        // False for an unknown ID.
        bool SetYSort(uint32_t id, bool ySort);

        // A new layer on top (drawn last). Returns its ID; 0 for an empty or a taken name, or when
        // the list is full.
        uint32_t Add(std::string_view name);
        // Any layer can be renamed (Default too). False for an unknown ID, an empty name or one
        // another layer has.
        bool Rename(uint32_t id, std::string_view name);
        // Every layer but Default can be removed; renderers on it fall back to Default.
        bool Remove(uint32_t id);
        // Moves the layer at `from` to position `to` (drawing order).
        bool Move(size_t from, size_t to);

        // A layer stored in a file as an ID and a name: the ID when the list has that layer (with
        // that name), else the layer with that name (the file came from another project), else the
        // ID unchanged (drawn on Default; nothing is lost when the file is saved again).
        uint32_t Resolve(uint32_t id, std::string_view name) const;

        // Rebuilds the list from stored layers (with their Y sorting): duplicate IDs / names and
        // empty names are dropped, Default is put back (at the front) when missing.
        void Assign(const std::vector<SortingLayer>& layers);
        uint32_t GetNextID() const { return m_NextID; }

        bool operator==(const SortingLayerList& other) const { return m_Layers == other.m_Layers; }

    private:
        std::vector<SortingLayer> m_Layers;
        uint32_t m_NextID = 1;
    };

    // What decides the drawing order of 2D things (back to front): the sorting layer's position,
    // then the order inside the layer, then the depth from the camera (farther first).
    // A Y-sorted layer (top-down games) compares the sort point's Y before the order: a larger
    // world Y is drawn first (it stands farther up, behind); the order in the layer then only
    // breaks ties - the parts of one character drawn at its root's Y (see GetSortY).
    struct SortKey2D
    {
        int Layer = 0;
        int Order = 0;
        float Depth = 0.0f; // view-space z: more negative = farther away
        float SortY = 0.0f; // -world Y on a Y-sorted layer, else 0

        // A key on a layer that sorts by Y (`ySort`) at world height `y`, else without Y.
        static SortKey2D Make(int layer, int order, float depth, bool ySort = false, float y = 0.0f)
        {
            return SortKey2D{ layer, order, depth, ySort ? -y : 0.0f };
        }

        static bool Less(const SortKey2D& a, const SortKey2D& b)
        {
            if (a.Layer != b.Layer) return a.Layer < b.Layer;
            if (a.SortY != b.SortY) return a.SortY < b.SortY;
            if (a.Order != b.Order) return a.Order < b.Order;
            return a.Depth < b.Depth;
        }
    };

}
