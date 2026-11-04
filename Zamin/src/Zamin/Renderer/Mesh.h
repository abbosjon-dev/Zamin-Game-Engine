#pragma once

#include "Zamin/Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <filesystem>
#include <vector>

namespace Zamin {

    struct MeshVertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
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

        const Ref<VertexArray>& GetVertexArray() const { return m_VertexArray; }
        uint32_t GetIndexCount() const { return m_IndexCount; }
        const AABB& GetBounds() const { return m_Bounds; }

    private:
        Ref<VertexArray> m_VertexArray;
        uint32_t m_IndexCount = 0;
        AABB m_Bounds;
    };

    enum class PrimitiveMesh { Cube = 0, Sphere, Plane, Cylinder, Cone, Model };
    const char* PrimitiveMeshToString(PrimitiveMesh type);
    PrimitiveMesh PrimitiveMeshFromString(const std::string& str);

    class MeshLibrary
    {
    public:
        static Ref<Mesh> GetPrimitive(PrimitiveMesh type);
        static Ref<Mesh> LoadOBJ(const std::filesystem::path& path);
        static void Clear();
    };

}
