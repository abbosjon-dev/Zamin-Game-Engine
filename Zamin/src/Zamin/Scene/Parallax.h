#pragma once

#include <glm/glm.hpp>

namespace Zamin {

    // The math of ParallaxComponent (no scene needed: the renderer and the tests use it).
    struct ParallaxMath
    {
        // How far a parallax layer moves: the camera's distance from the layer's own position,
        // times the factor (0 = not at all, 1 = as much as the camera).
        static glm::vec2 GetOffset(const glm::vec2& cameraPosition, const glm::vec2& layerPosition, const glm::vec2& factor)
        {
            return (cameraPosition - layerPosition) * factor;
        }

        // Copies of a piece `size` wide centred at center + k * spacing that cover [viewMin, viewMax]:
        // every k in [first, last] overlaps the view (pieces that only touch it are left out).
        // At most `maxCopies` (the ones nearest the view's centre). False when spacing <= 0 or the
        // view is empty.
        static bool GetRepeatRange(float center, float size, float spacing, float viewMin, float viewMax,
                                   int& first, int& last, int maxCopies = 256);

        // The part of the plane z = `z` (world) a camera sees: the corners of its view projected onto
        // it. False when the camera looks along the plane (no such rectangle).
        static bool GetViewRectAtDepth(const glm::mat4& viewProjection, float z, glm::vec2& min, glm::vec2& max);
    };

}
