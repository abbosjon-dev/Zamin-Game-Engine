#pragma once

#include "Zamin/Core/Base.h"

#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Zamin {

    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        // Uniforms by name (a string_view: a literal costs no allocation; the location is looked up once).
        virtual void SetInt(std::string_view name, int value) = 0;
        virtual void SetIntArray(std::string_view name, const int* values, uint32_t count) = 0;
        virtual void SetFloat(std::string_view name, float value) = 0;
        virtual void SetFloat2(std::string_view name, const glm::vec2& value) = 0;
        virtual void SetFloat3(std::string_view name, const glm::vec3& value) = 0;
        virtual void SetFloat4(std::string_view name, const glm::vec4& value) = 0;
        virtual void SetFloat4Array(std::string_view name, const glm::vec4* values, uint32_t count) = 0;
        virtual void SetMat3(std::string_view name, const glm::mat3& value) = 0;
        virtual void SetMat4(std::string_view name, const glm::mat4& value) = 0;
        virtual void SetMat4Array(std::string_view name, const glm::mat4* values, uint32_t count) = 0;

        // Links the program's std140 uniform block `block` to a UniformBuffer's binding point; false
        // when the program has no such block (the compiler drops one it does not use).
        virtual bool BindUniformBlock(std::string_view block, uint32_t binding) = 0;

        virtual const std::string& GetName() const = 0;
        virtual bool IsValid() const = 0;

        // File with "#type vertex" / "#type fragment" sections.
        static Ref<Shader> Create(const std::string& filepath);
        static Ref<Shader> Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
    };

}
