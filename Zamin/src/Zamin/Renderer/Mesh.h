#pragma once

#include "Zamin/Math/Geometry.h"
#include "Zamin/Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    struct MeshVertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
    };

    struct SkinnedVertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
        glm::ivec4 Joints{ 0 };
        glm::vec4 Weights{ 0.0f };
    };

    // A mesh's vertices on the CPU: their positions and normals, for a skinned mesh also the
    // joints and weights they follow (debug views, tools).
    struct MeshGeometry
    {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals;
        std::vector<glm::ivec4> Joints;  // skinned meshes only
        std::vector<glm::vec4> Weights;

        size_t GetVertexCount() const { return Positions.size(); }
        bool IsSkinned() const { return !Joints.empty() && Joints.size() == Positions.size() && Weights.size() == Positions.size(); }
        static MeshGeometry FromVertices(const std::vector<MeshVertex>& vertices);
        static MeshGeometry FromVertices(const std::vector<SkinnedVertex>& vertices);
    };

    class Mesh
    {
    public:
        Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);
        Mesh(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices);
        bool IsSkinned() const { return m_Skinned; }

        const Ref<VertexArray>& GetVertexArray() const { return m_VertexArray; }
        // The mesh's vertices and indices plus `instanceBuffer` (per-instance attributes from
        // location 3 on) for instanced draws; made on first use, again when the buffer changes.
        // Not for skinned meshes (their joints take locations 3 and 4).
        const Ref<VertexArray>& GetInstancedVertexArray(const Ref<VertexBuffer>& instanceBuffer) const;
        uint32_t GetIndexCount() const { return m_IndexCount; }
        uint32_t GetVertexCount() const { return m_VertexCount; }
        uint32_t GetTriangleCount() const { return m_IndexCount / 3; }
        const AABB& GetBounds() const { return m_Bounds; }
        // The vertices on the CPU: read back from the GPU on the first call (slow), then kept.
        const MeshGeometry& GetGeometry() const;
        // New vertex data of the same layout and count (morph targets): uploaded, bounds updated.
        // False (nothing changed) when the kind or the count differs.
        bool UpdateVertices(const std::vector<MeshVertex>& vertices);
        bool UpdateVertices(const std::vector<SkinnedVertex>& vertices);

    private:
        Ref<VertexArray> m_VertexArray;
        Ref<VertexBuffer> m_VertexBuffer;
        Ref<IndexBuffer> m_IndexBuffer;
        mutable Ref<VertexArray> m_InstancedVertexArray;
        mutable Ref<VertexBuffer> m_InstanceBuffer;
        uint32_t m_IndexCount = 0;
        uint32_t m_VertexCount = 0;
        AABB m_Bounds;
        bool m_Skinned = false;
        mutable Scope<MeshGeometry> m_Geometry;
    };

    enum class PrimitiveMesh { Cube = 0, Sphere, Plane, Cylinder, Cone, Model };
    const char* PrimitiveMeshToString(PrimitiveMesh type);
    PrimitiveMesh PrimitiveMeshFromString(const std::string& str);

    class MeshLibrary
    {
    public:
        static Ref<Mesh> GetPrimitive(PrimitiveMesh type);
        // The geometry of a primitive (no GPU): unit size, centered, counter-clockwise front faces
        // seen from outside (single-sided materials cull the others).
        static void GeneratePrimitive(PrimitiveMesh type, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);
        static Ref<Mesh> LoadOBJ(const std::filesystem::path& path);
        // The triangles of an .obj file (flat normals where it has none). False, with the reason in
        // `error`, when it cannot be read or holds no faces.
        static bool ReadOBJ(const std::filesystem::path& path, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
                            std::string* error = nullptr);
        static void Clear();
    };

}
