#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Zamin {

    // The triangles of a mesh on the CPU (what 3D colliders are built from): positions in the
    // model's space - with its node transforms and import settings, as the renderer draws it;
    // skinned meshes in their bind pose.
    struct CollisionMesh3D
    {
        std::vector<glm::vec3> Positions;
        std::vector<uint32_t> Indices; // three per triangle

        size_t GetTriangleCount() const { return Indices.size() / 3; }
    };

    // A convex hull: its corners and faces. A convex hull collider is this shape (the physics body
    // makes its own copy of it), the editor draws its edges.
    struct ConvexHull3D
    {
        std::vector<glm::vec3> Points;            // corners, in the source's space
        std::vector<std::vector<uint32_t>> Faces; // corner indices, counter-clockwise seen from outside
        std::vector<std::pair<uint32_t, uint32_t>> Edges; // every edge once (GetEdges, kept for drawing)
        size_t SourceVertexCount = 0;             // the points it was made from
        // MaxVertices left out corners the full hull would have (it is a little smaller than the mesh).
        bool Simplified = false;
        glm::vec3 BoundsMin{ 0.0f }, BoundsMax{ 0.0f };

        // Every edge once (lower corner index first).
        std::vector<std::pair<uint32_t, uint32_t>> GetEdges() const;
    };

    // Collision geometry made from meshes: read once per model asset / primitive and cached.
    // Model files are read again when they (or their .zmeta import settings) change on disk.
    class CollisionShapes3D
    {
    public:
        // How many corners a hull may have (Jolt's limit is 256).
        static constexpr int MinHullVertices = 4;
        static constexpr int MaxHullVertices = 256;

        // The triangles of a model file (.gltf / .glb / .obj, absolute or relative to the asset folder)
        // with its import settings, or of a primitive (unit size, centred) when `modelPath` is empty.
        // Null - with the reason in `error` - when the file cannot be read or has no triangles.
        static Ref<const CollisionMesh3D> GetMesh(const std::string& modelPath, PrimitiveMesh primitive, std::string* error = nullptr);
        // The convex hull of GetMesh's vertices with at most `maxVertices` corners: a mesh with more
        // gets a simpler hull through the points furthest out (Jolt's hull builder adds the point
        // furthest outside the hull so far, until the limit). Cached per source and limit.
        static Ref<const ConvexHull3D> GetConvexHull(const std::string& modelPath, PrimitiveMesh primitive, int maxVertices,
                                                     std::string* error = nullptr);
        // The hull of any points (not cached). Null - with the reason in `error` - for fewer than four
        // points or points that span no volume (all on a plane / a line).
        static Ref<const ConvexHull3D> BuildConvexHull(const std::vector<glm::vec3>& points, int maxVertices, std::string* error = nullptr);

        // The triangles of a model file without the cache or the GPU (glTF: every triangle primitive
        // of the default scene; .obj: MeshLibrary::ReadOBJ), import settings applied.
        static bool ReadModelGeometry(const std::filesystem::path& path, CollisionMesh3D& out, std::string* error = nullptr);

        // The edges of a mesh worth drawing (the editor's collider wireframe): each edge of its
        // triangles once, corners welded by position, without the ones between two triangles in one
        // plane (a quad's diagonal) and without degenerate triangles. Cached per mesh while it lives.
        using EdgeList = std::vector<std::pair<glm::vec3, glm::vec3>>;
        static EdgeList BuildFeatureEdges(const CollisionMesh3D& mesh);
        static Ref<const EdgeList> GetFeatureEdges(const Ref<const CollisionMesh3D>& mesh);

        // Cached meshes + hulls (tests, tools).
        static size_t GetCacheSize();
        static void Clear();
    };

}
