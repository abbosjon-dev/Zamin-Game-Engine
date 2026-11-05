#pragma once

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

    struct AABB
    {
        glm::vec3 Min{ 0.0f };
        glm::vec3 Max{ 0.0f };
    };

    class Mesh
    {
    public:
        Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);
        Mesh(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices);
        bool IsSkinned() const { return m_Skinned; }

        const Ref<VertexArray>& GetVertexArray() const { return m_VertexArray; }
        uint32_t GetIndexCount() const { return m_IndexCount; }
        uint32_t GetVertexCount() const { return m_VertexCount; }
        uint32_t GetTriangleCount() const { return m_IndexCount / 3; }
        const AABB& GetBounds() const { return m_Bounds; }

    private:
        Ref<VertexArray> m_VertexArray;
        uint32_t m_IndexCount = 0;
        uint32_t m_VertexCount = 0;
        AABB m_Bounds;
        bool m_Skinned = false;
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
