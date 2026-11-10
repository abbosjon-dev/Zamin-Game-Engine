#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/GraphicsContext.h"
#include "Zamin/Renderer/ShaderCache.h"
#include "Zamin/Utils/FileSystem.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <vector>

namespace Zamin {

    // What the driver offers for program binaries, asked once (the first shader made).
    static void QueryProgramBinarySupport()
    {
        static bool s_Queried = false;
        if (s_Queried)
            return;
        s_Queried = true;
        GLint formats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        while (glGetError() != GL_NO_ERROR) {} // (a driver without the query: no binaries)
        ShaderCache::SetBinaryFormatCount(formats);
        const GraphicsContextInfo& info = GraphicsContext::GetInfo();
        auto text = [](GLenum name) {
            const GLubyte* s = glGetString(name);
            return s ? std::string((const char*)s) : std::string();
        };
        ShaderCache::SetDriver(info.Valid ? info.Vendor + "|" + info.Renderer + "|" + info.Version
                                          : text(GL_VENDOR) + "|" + text(GL_RENDERER) + "|" + text(GL_VERSION));
        if (formats <= 0)
            ZM_CORE_INFO("Shader keshi: drayver program binary formatini bermaydi - shader'lar har safar kompilyatsiya qilinadi");
    }

    class OpenGLShader : public Shader
    {
    public:
        OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
            : m_Name(name)
        {
            QueryProgramBinarySupport();
            const auto start = std::chrono::steady_clock::now();
            auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
            const bool cached = ShaderCache::IsAvailable();
            const uint64_t key = cached ? ShaderCache::MakeKey(name, vertexSrc, fragmentSrc, ShaderCache::GetDriver()) : 0;
            if (cached && LoadBinary(key))
            {
                ShaderCache::RecordHit(elapsed());
                return;
            }
            Compile(vertexSrc, fragmentSrc, cached);
            if (cached && m_RendererID)
                StoreBinary(key);
            ShaderCache::RecordMiss(elapsed());
        }

        ~OpenGLShader() override { if (m_RendererID) glDeleteProgram(m_RendererID); }

        void Bind() const override { glUseProgram(m_RendererID); }
        void Unbind() const override { glUseProgram(0); }

        void SetInt(std::string_view name, int value) override { glUniform1i(Loc(name), value); }
        void SetIntArray(std::string_view name, const int* values, uint32_t count) override { glUniform1iv(Loc(name), (GLsizei)count, values); }
        void SetFloat(std::string_view name, float value) override { glUniform1f(Loc(name), value); }
        void SetFloat2(std::string_view name, const glm::vec2& v) override { glUniform2f(Loc(name), v.x, v.y); }
        void SetFloat3(std::string_view name, const glm::vec3& v) override { glUniform3f(Loc(name), v.x, v.y, v.z); }
        void SetFloat4(std::string_view name, const glm::vec4& v) override { glUniform4f(Loc(name), v.x, v.y, v.z, v.w); }
        void SetFloat4Array(std::string_view name, const glm::vec4* values, uint32_t count) override
        {
            glUniform4fv(Loc(name), (GLsizei)count, glm::value_ptr(values[0]));
        }
        void SetMat3(std::string_view name, const glm::mat3& m) override { glUniformMatrix3fv(Loc(name), 1, GL_FALSE, glm::value_ptr(m)); }
        void SetMat4(std::string_view name, const glm::mat4& m) override { glUniformMatrix4fv(Loc(name), 1, GL_FALSE, glm::value_ptr(m)); }

        void SetMat4Array(std::string_view name, const glm::mat4* m, uint32_t count) override
        {
            glUniformMatrix4fv(Loc(name), (GLsizei)count, GL_FALSE, glm::value_ptr(m[0]));
        }

        bool BindUniformBlock(std::string_view block, uint32_t binding) override
        {
            if (!m_RendererID)
                return false;
            const std::string name(block);
            const GLuint index = glGetUniformBlockIndex(m_RendererID, name.c_str());
            if (index == GL_INVALID_INDEX)
                return false;
            glUniformBlockBinding(m_RendererID, index, binding);
            return true;
        }

        const std::string& GetName() const override { return m_Name; }
        bool IsValid() const override { return m_RendererID != 0; }

    private:
        GLint Loc(std::string_view name)
        {
            auto it = m_LocationCache.find(name); // (no std::string made to look it up)
            if (it != m_LocationCache.end())
                return it->second;
            std::string key(name);
            GLint location = glGetUniformLocation(m_RendererID, key.c_str());
            m_LocationCache.emplace(std::move(key), location);
            return location;
        }

        struct NameHash
        {
            using is_transparent = void;
            size_t operator()(std::string_view name) const { return std::hash<std::string_view>()(name); }
        };

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

        // The program from the binary ShaderCache has for `key`: false when there is none or the
        // driver does not take it (that file goes; the program is compiled from source then).
        bool LoadBinary(uint64_t key)
        {
            uint32_t format = 0;
            std::vector<uint8_t> binary;
            if (!ShaderCache::Load(key, format, binary))
                return false;
            GLuint program = glCreateProgram();
            glProgramBinary(program, (GLenum)format, binary.data(), (GLsizei)binary.size());
            GLint ok = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &ok);
            while (glGetError() != GL_NO_ERROR) {} // (an unknown format reports GL_INVALID_ENUM)
            if (!ok)
            {
                glDeleteProgram(program);
                ShaderCache::Remove(key);
                ShaderCache::RecordRejected();
                ZM_CORE_WARN("Shader '{}': saqlangan binary drayverga to'g'ri kelmadi - qayta kompilyatsiya qilinadi", m_Name);
                return false;
            }
            m_RendererID = program;
            return true;
        }

        void StoreBinary(uint64_t key) const
        {
            GLint length = 0;
            glGetProgramiv(m_RendererID, GL_PROGRAM_BINARY_LENGTH, &length);
            if (length <= 0)
                return;
            std::vector<uint8_t> binary((size_t)length);
            GLenum format = 0;
            GLsizei written = 0;
            glGetProgramBinary(m_RendererID, length, &written, &format, binary.data());
            if (written <= 0)
                return;
            binary.resize((size_t)written);
            ShaderCache::Store(key, (uint32_t)format, binary);
        }

        void Compile(const std::string& vertexSrc, const std::string& fragmentSrc, bool retrievable)
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
            if (retrievable)
                glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE); // (before the link)
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
            // A failed link already deleted the program. Detaching from zero would
            // introduce GL_INVALID_VALUE and contaminate subsequent valid draws.
            if (program)
            {
                glDetachShader(program, vs);
                glDetachShader(program, fs);
            }
            glDeleteShader(vs);
            glDeleteShader(fs);
            m_RendererID = program;
        }

    private:
        uint32_t m_RendererID = 0;
        std::string m_Name;
        std::unordered_map<std::string, GLint, NameHash, std::equal_to<>> m_LocationCache;
    };

    Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
    {
        return CreateRef<OpenGLShader>(name, vertexSrc, fragmentSrc);
    }

    Ref<Shader> Shader::Create(const std::string& filepath)
    {
        const std::string source = FileSystem::ReadTextFile(filepath);
        std::string vertex, fragment;
        bool haveVertex = false, haveFragment = false;
        size_t position = 0;
        std::string* stage = nullptr;
        auto fail = [&](const char* message) -> Ref<Shader> {
            ZM_CORE_ERROR("Shader '{}': {}", filepath, message);
            return nullptr;
        };
        while (position < source.size())
        {
            const size_t end = source.find('\n', position);
            std::string line = source.substr(position, end == std::string::npos ? end : end - position);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos && line.compare(start, 5, "#type") == 0)
            {
                auto type = line.substr(start + 5);
                const auto first = type.find_first_not_of(" \t"), last = type.find_last_not_of(" \t");
                type = first == std::string::npos ? "" : type.substr(first, last - first + 1);
                if (type == "vertex")
                {
                    if (haveVertex) return fail("Duplicate vertex stage");
                    haveVertex = true; stage = &vertex;
                }
                else if (type == "fragment" || type == "pixel")
                {
                    if (haveFragment) return fail("Duplicate fragment stage");
                    haveFragment = true; stage = &fragment;
                }
                else return fail("Unknown or missing shader stage");
            }
            else if (stage) *stage += line + "\n";
            if (end == std::string::npos) break;
            position = end + 1;
        }
        if (!haveVertex || !haveFragment || vertex.find_first_not_of(" \t\r\n") == std::string::npos || fragment.find_first_not_of(" \t\r\n") == std::string::npos)
            return fail("Both vertex and fragment stages must have source");
        std::string name = std::filesystem::path(filepath).stem().string();
        return CreateRef<OpenGLShader>(name, vertex, fragment);
    }

}
