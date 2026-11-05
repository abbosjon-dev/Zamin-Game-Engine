#pragma once

#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Renderer/Renderer3D.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Zamin {

    // Which outline an entity gets (the higher one when an entity is listed twice).
    enum class OutlineCategory : uint8_t { None = 0, Child = 1, Secondary = 2, Primary = 3 };

    // An entity the selection outline marks: its ID as the renderers write it into the entity ID
    // picture, its category and whether it is the one under the mouse.
    struct OutlineEntity
    {
        int EntityID = -1;
        OutlineCategory Category = OutlineCategory::None;
        bool Hovered = false;
    };

    // The table the outline shaders look the entity ID picture's pixels up in: one RGBA8 texel per
    // entity index (R the category, G / B the entity's version, A 255 when hovered), `Width`
    // texels a row. An entity ID is an EnTT entity: its index in the low IndexBits bits, the
    // version above them (a recycled index with another version is a different entity).
    struct OutlineLookup
    {
        static constexpr uint32_t Width = 1024;
        static constexpr uint32_t IndexBits = 20;
        static constexpr uint32_t IndexMask = (1u << IndexBits) - 1u;

        uint32_t Height = 0;           // rows (0: nothing is marked)
        std::vector<uint8_t> Texels;   // Width x Height x 4

        // Hovered entities that are also outlined as selected (or a selection's child) are not
        // marked as hovered: the hover outline shows what a click would newly select.
        static OutlineLookup Build(const std::vector<OutlineEntity>& entities);
        // What the shaders find for a pixel showing `entityID` (the same decoding on the CPU).
        OutlineCategory GetCategory(int entityID) const;
        bool IsHovered(int entityID) const;
        bool IsEmpty() const { return Height == 0; }
    };

    // What the outline is drawn from: the scene's entity ID picture (RED_INTEGER, the render
    // resolution) and the whole silhouettes of the outlined meshes - their draws of that scene
    // (Renderer3D::BeginCapture) with the camera they were drawn with. Without a silhouette an
    // entity's outline follows only its visible pixels (2D shapes: what is in front of them too).
    struct SelectionOutlineInput
    {
        uint32_t EntityIDTexture = 0;
        uint32_t IDWidth = 0, IDHeight = 0;
        uint32_t Width = 0, Height = 0;              // the render target the outline goes into (bound)
        glm::mat4 ViewProjection{ 1.0f };
        std::vector<OutlineEntity> Entities;
        const std::vector<Renderer3D::CapturedDraw>* Silhouettes = nullptr;
    };

    // The editor viewport's shader based selection outline. A mask pass marks the pixels of the
    // outlined entities (visible: the entity ID picture; the whole silhouette: the captured meshes
    // drawn again), then a pass over the target draws a smooth contour around the silhouettes:
    // full strength where their edge is visible, faint where it is hidden (ShowOccluded), and a
    // thinner one around the hovered entity.
    class SelectionOutline
    {
    public:
        static void Render(const SelectionOutlineInput& input, const SelectionOutlineSettings& settings);
        static void Shutdown();

        // How much of an outline `width` pixels wide covers a pixel whose centre is `distance`
        // pixels from the centre of the nearest marked pixel: 1 up to the outline's width, then a
        // one pixel wide anti-aliased edge (the shader's function).
        static float Coverage(float distance, float width);
        // How many pixels around each pixel the outline pass looks for marked ones.
        static int GetSearchRadius(float width, float hoverWidth);
    };

}
