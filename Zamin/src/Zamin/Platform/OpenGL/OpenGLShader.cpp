#include "Zamin/Renderer/Shader.h"
#include "Zamin/Utils/FileSystem.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <vector>

namespace Zamin {

    class OpenGLShader : public Shader
    {
    public:
        OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
            : m_Name(name)
        {
            Compile(vertexSrc, fragmentSrc);
        }

        ~OpenGLShader() override { if (m_RendererID) glDeleteProgram(m_RendererID); }

        void Bind() const override { glUseProgram(m_RendererID); }
        void Unbind() const override { glUseProgram(0); }

        void SetInt(const std::string& name, int value) override { glUniform1i(Loc(name), value); }
        void SetIntArray(const std::string& name, const int* values, uint32_t count) override { glUniform1iv(Loc(name), (GLsizei)count, values); }
        void SetFloat(const std::string& name, float value) override { glUniform1f(Loc(name), value); }
        void SetFloat2(const std::string& name, const glm::vec2& v) override { glUniform2f(Loc(name), v.x, v.y); }
        void SetFloat3(const std::string& name, const glm::vec3& v) override { glUniform3f(Loc(name), v.x, v.y, v.z); }
        void SetFloat4(const std::string& name, const glm::vec4& v) override { glUniform4f(Loc(name), v.x, v.y, v.z, v.w); }
        void SetFloat4Array(const std::string& name, const glm::vec4* values, uint32_t count) override
        {
            glUniform4fv(Loc(name), (GLsizei)count, glm::value_ptr(values[0]));
        }
        void SetMat3(const std::string& name, const glm::mat3& m) override { glUniformMatrix3fv(Loc(name), 1, GL_FALSE, glm::value_ptr(m)); }
        void SetMat4(const std::string& name, const glm::mat4& m) override { glUniformMatrix4fv(Loc(name), 1, GL_FALSE, glm::value_ptr(m)); }

        void SetMat4Array(const std::string& name, const glm::mat4* m, uint32_t count) override
        {
            glUniformMatrix4fv(Loc(name), (GLsizei)count, GL_FALSE, glm::value_ptr(m[0]));
        }

        const std::string& GetName() const override { return m_Name; }
        bool IsValid() const override { return m_RendererID != 0; }

    private:
        GLint Loc(const std::string& name)
        {
            auto it = m_LocationCache.find(name);
            if (it != m_LocationCache.end())
                return it->second;
            GLint location = glGetUniformLocation(m_RendererID, name.c_str());
            m_LocationCache[name] = location;
            return location;
        }

        static GLuint CompileStage(GLenum type, const std::string& source, const std::string& shaderName)
        {
            GLuint shader = glCreateShader(type);
            const char* src = source.c_str();
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);

            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                GLint len = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
                std::vector<char> log(len + 1);
                glGetShaderInfoLog(shader, len, &len, log.data());
                ZM_CORE_ERROR("Shader '{}' ({}) kompilyatsiya xatosi:\n{}", shaderName,
                              type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data());
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        }

        void Compile(const std::string& vertexSrc, const std::string& fragmentSrc)
        {
            GLuint vs = CompileStage(GL_VERTEX_SHADER, vertexSrc, m_Name);
            GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc, m_Name);
            if (!vs || !fs)
            {
                if (vs) glDeleteShader(vs);
                if (fs) glDeleteShader(fs);
                return;
            }

            GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);

            GLint ok = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                GLint len = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
                std::vector<char> log(len + 1);
                glGetProgramInfoLog(program, len, &len, log.data());
                ZM_CORE_ERROR("Shader '{}' link xatosi:\n{}", m_Name, log.data());
                glDeleteProgram(program);
                program = 0;
            }
            glDetachShader(program, vs);
            glDetachShader(program, fs);
            glDeleteShader(vs);
            glDeleteShader(fs);
            m_RendererID = program;
        }

    private:
        uint32_t m_RendererID = 0;
        std::string m_Name;
        std::unordered_map<std::string, GLint> m_LocationCache;
    };

    Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
    {
        return CreateRef<OpenGLShader>(name, vertexSrc, fragmentSrc);
    }

    Ref<Shader> Shader::Create(const std::string& filepath)
    {
        std::string source = FileSystem::ReadTextFile(filepath);
        std::string vertex, fragment;
        const std::string token = "#type";
        size_t pos = source.find(token);
        while (pos != std::string::npos)
        {
            size_t eol = source.find_first_of("\r\n", pos);
            std::string type = source.substr(pos + token.size() + 1, eol - pos - token.size() - 1);
            size_t next = source.find_first_not_of("\r\n", eol);
            pos = source.find(token, next);
            std::string body = source.substr(next, pos == std::string::npos ? std::string::npos : pos - next);
            if (type.starts_with("vertex")) vertex = body;
            else if (type.starts_with("fragment") || type.starts_with("pixel")) fragment = body;
        }
        std::string name = std::filesystem::path(filepath).stem().string();
        return CreateRef<OpenGLShader>(name, vertex, fragment);
    }

}
