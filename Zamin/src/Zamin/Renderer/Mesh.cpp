#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Asset/AssetImportLog.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <glm/gtc/constants.hpp>
#include <unordered_map>

namespace Zamin {

    // The box around the vertices' positions (none: the box is left as it is).
    template<typename V>
    static void BoundsOf(const std::vector<V>& vertices, AABB& bounds)
    {
        if (vertices.empty())
            return;
        bounds = AABB::Empty();
        for (const auto& v : vertices)
            bounds.Encapsulate(v.Position);
    }

    Mesh::Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices)
    {
        m_VertexArray = VertexArray::Create();
        Ref<VertexBuffer> vb = VertexBuffer::Create(vertices.data(), (uint32_t)(vertices.size() * sizeof(MeshVertex)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
        });
        m_VertexArray->AddVertexBuffer(vb);
        m_IndexBuffer = IndexBuffer::Create(indices.data(), (uint32_t)indices.size());
        m_VertexArray->SetIndexBuffer(m_IndexBuffer);
        m_VertexBuffer = vb;
        m_IndexCount = (uint32_t)indices.size();
        m_VertexCount = (uint32_t)vertices.size();

        BoundsOf(vertices, m_Bounds);
    }

    Mesh::Mesh(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices) : m_Skinned(true)
    {
        m_VertexArray = VertexArray::Create();
        Ref<VertexBuffer> vb = VertexBuffer::Create(vertices.data(), (uint32_t)(vertices.size() * sizeof(SkinnedVertex)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Int4,   "a_Joints" },
            { ShaderDataType::Float4, "a_Weights" },
        });
        m_VertexArray->AddVertexBuffer(vb);
        m_IndexBuffer = IndexBuffer::Create(indices.data(), (uint32_t)indices.size());
        m_VertexArray->SetIndexBuffer(m_IndexBuffer);
        m_VertexBuffer = vb;
        m_IndexCount = (uint32_t)indices.size();
        m_VertexCount = (uint32_t)vertices.size();
        BoundsOf(vertices, m_Bounds);
    }

    bool Mesh::UpdateVertices(const std::vector<MeshVertex>& vertices)
    {
        if (m_Skinned || vertices.size() != m_VertexCount || !m_VertexBuffer)
            return false;
        m_VertexBuffer->StreamData(vertices.data(), (uint32_t)(vertices.size() * sizeof(MeshVertex)));
        BoundsOf(vertices, m_Bounds);
        m_Geometry.reset();
        return true;
    }

    bool Mesh::UpdateVertices(const std::vector<SkinnedVertex>& vertices)
    {
        if (!m_Skinned || vertices.size() != m_VertexCount || !m_VertexBuffer)
            return false;
        m_VertexBuffer->StreamData(vertices.data(), (uint32_t)(vertices.size() * sizeof(SkinnedVertex)));
        BoundsOf(vertices, m_Bounds);
        m_Geometry.reset();
        return true;
    }

    MeshGeometry MeshGeometry::FromVertices(const std::vector<MeshVertex>& vertices)
    {
        MeshGeometry g;
        g.Positions.reserve(vertices.size());
        g.Normals.reserve(vertices.size());
        for (const MeshVertex& v : vertices)
        {
            g.Positions.push_back(v.Position);
            g.Normals.push_back(v.Normal);
        }
        return g;
    }

    MeshGeometry MeshGeometry::FromVertices(const std::vector<SkinnedVertex>& vertices)
    {
        MeshGeometry g;
        g.Positions.reserve(vertices.size());
        g.Normals.reserve(vertices.size());
        g.Joints.reserve(vertices.size());
        g.Weights.reserve(vertices.size());
        for (const SkinnedVertex& v : vertices)
        {
            g.Positions.push_back(v.Position);
            g.Normals.push_back(v.Normal);
            g.Joints.push_back(v.Joints);
            g.Weights.push_back(v.Weights);
        }
        return g;
    }

    const MeshGeometry& Mesh::GetGeometry() const
    {
        if (m_Geometry)
            return *m_Geometry;
        m_Geometry = CreateScope<MeshGeometry>();
        if (!m_VertexBuffer || m_VertexCount == 0)
            return *m_Geometry;
        if (m_Skinned)
        {
            std::vector<SkinnedVertex> vertices(m_VertexCount);
            if (m_VertexBuffer->GetData(vertices.data(), (uint32_t)(vertices.size() * sizeof(SkinnedVertex))))
                *m_Geometry = MeshGeometry::FromVertices(vertices);
        }
        else
        {
            std::vector<MeshVertex> vertices(m_VertexCount);
            if (m_VertexBuffer->GetData(vertices.data(), (uint32_t)(vertices.size() * sizeof(MeshVertex))))
                *m_Geometry = MeshGeometry::FromVertices(vertices);
        }
        return *m_Geometry;
    }

    const Ref<VertexArray>& Mesh::GetInstancedVertexArray(const Ref<VertexBuffer>& instanceBuffer) const
    {
        if (!m_InstancedVertexArray || m_InstanceBuffer != instanceBuffer)
        {
            // The same vertex and index buffers as the plain vertex array (locations 0..2), then
            // the instance attributes.
            m_InstancedVertexArray = VertexArray::Create();
            m_InstancedVertexArray->AddVertexBuffer(m_VertexBuffer);
            m_InstancedVertexArray->AddVertexBuffer(instanceBuffer);
            m_InstancedVertexArray->SetIndexBuffer(m_IndexBuffer);
            m_InstanceBuffer = instanceBuffer;
        }
        return m_InstancedVertexArray;
    }

    const char* PrimitiveMeshToString(PrimitiveMesh type)
    {
        switch (type)
        {
            case PrimitiveMesh::Cube:     return "Cube";
            case PrimitiveMesh::Sphere:   return "Sphere";
            case PrimitiveMesh::Plane:    return "Plane";
            case PrimitiveMesh::Cylinder: return "Cylinder";
            case PrimitiveMesh::Cone:     return "Cone";
            case PrimitiveMesh::Model:    return "Model";
        }
        return "Cube";
    }

    PrimitiveMesh PrimitiveMeshFromString(const std::string& s)
    {
        if (s == "Sphere")   return PrimitiveMesh::Sphere;
        if (s == "Plane")    return PrimitiveMesh::Plane;
        if (s == "Cylinder") return PrimitiveMesh::Cylinder;
        if (s == "Cone")     return PrimitiveMesh::Cone;
        if (s == "Model")    return PrimitiveMesh::Model;
        return PrimitiveMesh::Cube;
    }

    // ------------------------------------------------------------ primitives
    // The primitives' geometry (counter-clockwise front faces seen from outside, like glTF).
    static void GenerateCube(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx)
    {
        const glm::vec3 normals[6] = { {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
        for (const auto& n : normals)
        {
            glm::vec3 up = std::abs(n.y) > 0.5f ? glm::vec3(0, 0, -n.y) : glm::vec3(0, 1, 0);
            glm::vec3 right = glm::cross(up, n);
            uint32_t base = (uint32_t)v.size();
            glm::vec3 c = n * 0.5f;
            v.push_back({ c - right * 0.5f - up * 0.5f, n, { 0, 0 } });
            v.push_back({ c + right * 0.5f - up * 0.5f, n, { 1, 0 } });
            v.push_back({ c + right * 0.5f + up * 0.5f, n, { 1, 1 } });
            v.push_back({ c - right * 0.5f + up * 0.5f, n, { 0, 1 } });
            idx.insert(idx.end(), { base, base + 1, base + 2, base + 2, base + 3, base });
        }
    }

    static void GenerateSphere(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, uint32_t segments = 48, uint32_t rings = 24)
    {
        for (uint32_t r = 0; r <= rings; r++)
        {
            float phi = glm::pi<float>() * r / rings;
            for (uint32_t s = 0; s <= segments; s++)
            {
                float theta = glm::two_pi<float>() * s / segments;
                glm::vec3 n = { std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta) };
                v.push_back({ n * 0.5f, n, { (float)s / segments, 1.0f - (float)r / rings } });
            }
        }
        for (uint32_t r = 0; r < rings; r++)
            for (uint32_t s = 0; s < segments; s++)
            {
                uint32_t a = r * (segments + 1) + s;
                uint32_t b = a + segments + 1;
                idx.insert(idx.end(), { a, a + 1, b, b, a + 1, b + 1 });
            }
    }

    static void GeneratePlane(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx)
    {
        v = {
            { { -0.5f, 0.0f,  0.5f }, { 0, 1, 0 }, { 0, 0 } },
            { {  0.5f, 0.0f,  0.5f }, { 0, 1, 0 }, { 1, 0 } },
            { {  0.5f, 0.0f, -0.5f }, { 0, 1, 0 }, { 1, 1 } },
            { { -0.5f, 0.0f, -0.5f }, { 0, 1, 0 }, { 0, 1 } },
        };
        idx = { 0, 1, 2, 2, 3, 0 };
    }

    // Cylinder (topRadius = bottomRadius) or cone (topRadius = 0), height 1, centered.
    static void GenerateCylinder(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, float topRadius, uint32_t segments = 40)
    {
        const float bottomRadius = 0.5f;
        float slope = (bottomRadius - topRadius); // over height 1
        for (uint32_t s = 0; s <= segments; s++)
        {
            float t = glm::two_pi<float>() * s / segments;
            float c = std::cos(t), sn = std::sin(t);
            glm::vec3 n = glm::normalize(glm::vec3(c, slope, sn));
            v.push_back({ { c * bottomRadius, -0.5f, sn * bottomRadius }, n, { (float)s / segments, 0 } });
            v.push_back({ { c * topRadius,     0.5f, sn * topRadius },    n, { (float)s / segments, 1 } });
        }
        for (uint32_t s = 0; s < segments; s++)
        {
            uint32_t a = s * 2;
            idx.insert(idx.end(), { a, a + 1, a + 2, a + 2, a + 1, a + 3 });
        }
        auto cap = [&](float y, float radius, float ny) {
            if (radius <= 0.0f) return;
            uint32_t center = (uint32_t)v.size();
            v.push_back({ { 0, y, 0 }, { 0, ny, 0 }, { 0.5f, 0.5f } });
            for (uint32_t s = 0; s <= segments; s++)
            {
                float t = glm::two_pi<float>() * s / segments;
                float c = std::cos(t), sn = std::sin(t);
                v.push_back({ { c * radius, y, sn * radius }, { 0, ny, 0 }, { c * 0.5f + 0.5f, sn * 0.5f + 0.5f } });
            }
            for (uint32_t s = 0; s < segments; s++)
            {
                if (ny > 0) idx.insert(idx.end(), { center, center + 2 + s, center + 1 + s });
                else        idx.insert(idx.end(), { center, center + 1 + s, center + 2 + s });
            }
        };
        cap(0.5f, topRadius, 1.0f);
        cap(-0.5f, bottomRadius, -1.0f);
    }

    void MeshLibrary::GeneratePrimitive(PrimitiveMesh type, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
    {
        vertices.clear();
        indices.clear();
        switch (type)
        {
            case PrimitiveMesh::Sphere:   GenerateSphere(vertices, indices); break;
            case PrimitiveMesh::Plane:    GeneratePlane(vertices, indices); break;
            case PrimitiveMesh::Cylinder: GenerateCylinder(vertices, indices, 0.5f); break;
            case PrimitiveMesh::Cone:     GenerateCylinder(vertices, indices, 0.0f); break;
            default:                      GenerateCube(vertices, indices); break;
        }
    }

    static std::unordered_map<int, Ref<Mesh>> s_Primitives;
    static std::unordered_map<std::string, Ref<Mesh>> s_Models;

    Ref<Mesh> MeshLibrary::GetPrimitive(PrimitiveMesh type)
    {
        auto it = s_Primitives.find((int)type);
        if (it != s_Primitives.end())
            return it->second;

        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        GeneratePrimitive(type, vertices, indices);
        Ref<Mesh> mesh = CreateRef<Mesh>(vertices, indices);
        s_Primitives[(int)type] = mesh;
        return mesh;
    }

    bool MeshLibrary::ReadOBJ(const std::filesystem::path& path, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
                              std::string* error)
    {
        vertices.clear();
        indices.clear();
        tinyobj::ObjReaderConfig config;
        config.triangulate = true;
        config.vertex_color = false;
        tinyobj::ObjReader reader;
        if (!reader.ParseFromFile(path.string(), config))
        {
            if (error)
            {
                *error = reader.Error();
                while (!error->empty() && (error->back() == '\n' || error->back() == '\r'))
                    error->pop_back();
                if (error->empty())
                    *error = "o'qilmadi";
            }
            return false;
        }

        const auto& attrib = reader.GetAttrib();
        for (const auto& shape : reader.GetShapes())
        {
            size_t offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
            {
                size_t fv = shape.mesh.num_face_vertices[f];
                uint32_t base = (uint32_t)vertices.size();
                for (size_t k = 0; k < fv; k++)
                {
                    tinyobj::index_t id = shape.mesh.indices[offset + k];
                    MeshVertex vert{};
                    vert.Position = { attrib.vertices[3 * id.vertex_index + 0], attrib.vertices[3 * id.vertex_index + 1],
                                      attrib.vertices[3 * id.vertex_index + 2] };
                    if (id.normal_index >= 0)
                        vert.Normal = { attrib.normals[3 * id.normal_index + 0], attrib.normals[3 * id.normal_index + 1],
                                        attrib.normals[3 * id.normal_index + 2] };
                    if (id.texcoord_index >= 0)
                        vert.TexCoord = { attrib.texcoords[2 * id.texcoord_index + 0], attrib.texcoords[2 * id.texcoord_index + 1] };
                    vertices.push_back(vert);
                    indices.push_back(base + (uint32_t)k);
                }
                // Flat normals if the file has none.
                if (fv == 3 && shape.mesh.indices[offset].normal_index < 0)
                {
                    glm::vec3 n = glm::normalize(glm::cross(vertices[base + 1].Position - vertices[base].Position,
                                                            vertices[base + 2].Position - vertices[base].Position));
                    for (int k = 0; k < 3; k++) vertices[base + k].Normal = n;
                }
                offset += fv;
            }
        }
        if (vertices.empty())
        {
            if (error) *error = "faylda uchburchak yo'q";
            return false;
        }
        return true;
    }

    Ref<Mesh> MeshLibrary::LoadOBJ(const std::filesystem::path& path)
    {
        std::string key = path.string();
        auto it = s_Models.find(key);
        if (it != s_Models.end())
            return it->second;

        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        std::string error;
        if (!ReadOBJ(path, vertices, indices, &error))
        {
            AssetImportLog::Report(path, error);
            s_Models[key] = nullptr;
            return nullptr;
        }
        AssetImportLog::Clear(path);
        auto mesh = CreateRef<Mesh>(vertices, indices);
        s_Models[key] = mesh;
        ZM_CORE_INFO("Model yuklandi: {} ({} uchburchak)", key, indices.size() / 3);
        return mesh;
    }

    void MeshLibrary::Clear()
    {
        s_Primitives.clear();
        s_Models.clear();
    }

}
