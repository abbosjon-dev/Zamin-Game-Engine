#include "Zamin/Renderer/Buffer.h"
#include "Zamin/Core/MemoryStats.h"
#include "Zamin/Renderer/VertexArray.h"

#include <glad/glad.h>

#include <algorithm>

namespace Zamin {

    // ---------------------------------------------------------------- VertexBuffer
    class OpenGLVertexBuffer : public VertexBuffer
    {
    public:
        explicit OpenGLVertexBuffer(uint32_t size) : m_Size(size)
        {
            glGenBuffers(1, &m_RendererID);
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
            m_Memory.Set(size);
        }

        OpenGLVertexBuffer(const void* vertices, uint32_t size) : m_Size(size)
        {
            glGenBuffers(1, &m_RendererID);
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
            m_Memory.Set(size);
        }

        ~OpenGLVertexBuffer() override { glDeleteBuffers(1, &m_RendererID); }

        void Bind() const override { glBindBuffer(GL_ARRAY_BUFFER, m_RendererID); }
        void Unbind() const override { glBindBuffer(GL_ARRAY_BUFFER, 0); }

        void SetData(const void* data, uint32_t size) override
        {
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
        }

        void StreamData(const void* data, uint32_t size) override
        {
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ARRAY_BUFFER, m_Size, nullptr, GL_DYNAMIC_DRAW); // orphan
            glBufferSubData(GL_ARRAY_BUFFER, 0, std::min(size, m_Size), data);
        }

        const BufferLayout& GetLayout() const override { return m_Layout; }
        void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

        uint32_t GetSize() const override { return m_Size; }

        bool GetData(void* out, uint32_t size, uint32_t offset) const override
        {
            if (!out || (uint64_t)offset + size > m_Size)
                return false;
            // GL_ARRAY_BUFFER is not part of a vertex array's state: binding it changes no VAO.
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glGetBufferSubData(GL_ARRAY_BUFFER, offset, size, out);
            return true;
        }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_Size = 0;
        BufferLayout m_Layout;
        TrackedMemory m_Memory{ MemoryCategory::Meshes };
    };

    Ref<VertexBuffer> VertexBuffer::Create(uint32_t size) { return CreateRef<OpenGLVertexBuffer>(size); }
    Ref<VertexBuffer> VertexBuffer::Create(const void* vertices, uint32_t size) { return CreateRef<OpenGLVertexBuffer>(vertices, size); }

    // ---------------------------------------------------------------- IndexBuffer
    class OpenGLIndexBuffer : public IndexBuffer
    {
    public:
        OpenGLIndexBuffer(const uint32_t* indices, uint32_t count) : m_Count(count)
        {
            glGenBuffers(1, &m_RendererID);
            // GL_ARRAY_BUFFER so creation does not depend on the currently bound VAO.
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
            m_Memory.Set((uint64_t)count * sizeof(uint32_t));
        }

        ~OpenGLIndexBuffer() override { glDeleteBuffers(1, &m_RendererID); }

        void Bind() const override { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID); }
        void Unbind() const override { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
        uint32_t GetCount() const override { return m_Count; }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_Count;
        TrackedMemory m_Memory{ MemoryCategory::Meshes };
    };

    Ref<IndexBuffer> IndexBuffer::Create(const uint32_t* indices, uint32_t count) { return CreateRef<OpenGLIndexBuffer>(indices, count); }

    // ---------------------------------------------------------------- VertexArray
    static GLenum ShaderDataTypeToGL(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Float: case ShaderDataType::Float2: case ShaderDataType::Float3:
            case ShaderDataType::Float4: case ShaderDataType::Mat3: case ShaderDataType::Mat4:
                return GL_FLOAT;
            case ShaderDataType::Int: case ShaderDataType::Int2: case ShaderDataType::Int3: case ShaderDataType::Int4:
                return GL_INT;
            case ShaderDataType::Bool:
                return GL_BOOL;
            default: return 0;
        }
    }

    class OpenGLVertexArray : public VertexArray
    {
    public:
        OpenGLVertexArray() { glGenVertexArrays(1, &m_RendererID); }
        ~OpenGLVertexArray() override { glDeleteVertexArrays(1, &m_RendererID); }

        void Bind() const override { glBindVertexArray(m_RendererID); }
        void Unbind() const override { glBindVertexArray(0); }

        void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) override
        {
            glBindVertexArray(m_RendererID);
            vertexBuffer->Bind();

            const auto& layout = vertexBuffer->GetLayout();
            for (const auto& element : layout)
            {
                switch (element.Type)
                {
                    case ShaderDataType::Float: case ShaderDataType::Float2:
                    case ShaderDataType::Float3: case ShaderDataType::Float4:
                    {
                        glEnableVertexAttribArray(m_VertexBufferIndex);
                        glVertexAttribPointer(m_VertexBufferIndex, element.GetComponentCount(), ShaderDataTypeToGL(element.Type),
                                              element.Normalized ? GL_TRUE : GL_FALSE, layout.GetStride(), (const void*)element.Offset);
                        if (layout.IsPerInstance())
                            glVertexAttribDivisor(m_VertexBufferIndex, 1);
                        m_VertexBufferIndex++;
                        break;
                    }
                    case ShaderDataType::Int: case ShaderDataType::Int2: case ShaderDataType::Int3:
                    case ShaderDataType::Int4: case ShaderDataType::Bool:
                    {
                        glEnableVertexAttribArray(m_VertexBufferIndex);
                        glVertexAttribIPointer(m_VertexBufferIndex, element.GetComponentCount(), ShaderDataTypeToGL(element.Type),
                                               layout.GetStride(), (const void*)element.Offset);
                        if (layout.IsPerInstance())
                            glVertexAttribDivisor(m_VertexBufferIndex, 1);
                        m_VertexBufferIndex++;
                        break;
                    }
                    case ShaderDataType::Mat3: case ShaderDataType::Mat4:
                    {
                        uint8_t count = element.GetComponentCount();
                        for (uint8_t i = 0; i < count; i++)
                        {
                            glEnableVertexAttribArray(m_VertexBufferIndex);
                            glVertexAttribPointer(m_VertexBufferIndex, count, ShaderDataTypeToGL(element.Type),
                                                  element.Normalized ? GL_TRUE : GL_FALSE, layout.GetStride(),
                                                  (const void*)(element.Offset + sizeof(float) * count * i));
                            glVertexAttribDivisor(m_VertexBufferIndex, 1);
                            m_VertexBufferIndex++;
                        }
                        break;
                    }
                    default: break;
                }
            }
            m_VertexBuffers.push_back(vertexBuffer);
        }

        void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) override
        {
            glBindVertexArray(m_RendererID);
            indexBuffer->Bind();
            m_IndexBuffer = indexBuffer;
        }

        const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const override { return m_VertexBuffers; }
        const Ref<IndexBuffer>& GetIndexBuffer() const override { return m_IndexBuffer; }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_VertexBufferIndex = 0;
        std::vector<Ref<VertexBuffer>> m_VertexBuffers;
        Ref<IndexBuffer> m_IndexBuffer;
    };

    Ref<VertexArray> VertexArray::Create() { return CreateRef<OpenGLVertexArray>(); }

    class OpenGLUniformBuffer : public UniformBuffer
    {
    public:
        OpenGLUniformBuffer(uint32_t size, uint32_t binding) : m_Size(size), m_Binding(binding)
        {
            glGenBuffers(1, &m_RendererID);
            glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
            glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_UNIFORM_BUFFER, binding, m_RendererID);
            m_Memory.Set(size);
        }

        ~OpenGLUniformBuffer() override { glDeleteBuffers(1, &m_RendererID); }

        void SetData(const void* data, uint32_t size, uint32_t offset) override
        {
            if (!data || (uint64_t)offset + size > m_Size)
                return;
            glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
            glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
            glBindBufferBase(GL_UNIFORM_BUFFER, m_Binding, m_RendererID);
            m_Uploaded += size;
        }

        uint32_t GetBinding() const override { return m_Binding; }
        uint32_t GetSize() const override { return m_Size; }
        uint64_t GetUploadedBytes() const override { return m_Uploaded; }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_Size = 0;
        uint32_t m_Binding = 0;
        uint64_t m_Uploaded = 0;
        TrackedMemory m_Memory{ MemoryCategory::Meshes };
    };

    Ref<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding) { return CreateRef<OpenGLUniformBuffer>(size, binding); }
}
