#pragma once

#include "Zamin/Renderer/Mesh.h"

#include <glm/glm.hpp>

#include <vector>

namespace Zamin {

    // One vertex normal of a mesh in the world: a line from the vertex along its normal.
    struct NormalLine
    {
        glm::vec3 From{ 0.0f };
        glm::vec3 To{ 0.0f };
        glm::vec3 Normal{ 0.0f, 0.0f, 1.0f }; // unit length, in the world
    };

    struct NormalLineOptions
    {
        float Length = 0.1f;          // world units
        size_t MaxLines = 50000;      // of `out` in all: a mesh with more vertices than fit gets an even sample of them
        // Only the normals on the side that faces the camera (the others point away from it, the
        // surface hides most of them): perspective - towards CameraPosition, orthographic -
        // against CameraForward.
        bool FacingOnly = false;
        bool Orthographic = false;
        glm::vec3 CameraPosition{ 0.0f };
        glm::vec3 CameraForward{ 0.0f, 0.0f, -1.0f };
    };

    // Appends the vertex normals of `geometry` placed by `transform` (normals by its inverse
    // transpose: right under non-uniform scale). A skinned mesh is posed as the mesh shader poses
    // it: each vertex by the weighted sum of its joints' matrices from `jointMatrices` (missing
    // joints: identity; none given: the bind pose). Vertices or normals that are not finite, and
    // zero normals, are skipped. Returns how many lines were added.
    size_t AppendNormalLines(const MeshGeometry& geometry, const glm::mat4& transform, const std::vector<glm::mat4>* jointMatrices,
                             const NormalLineOptions& options, std::vector<NormalLine>& out);

}
