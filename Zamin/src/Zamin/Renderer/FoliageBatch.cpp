#include "Zamin/Renderer/FoliageBatch.h"
#include "Zamin/Renderer/Buffer.h"
#include "Zamin/Renderer/VertexArray.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Core/MemoryStats.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace Zamin {
    void FoliageSettings::Sanitize()
    {
        LODDistance = std::isfinite(LODDistance) ? std::clamp(LODDistance, 1.f, 100000.f) : 40;
        CullDistance = std::isfinite(CullDistance) ? std::clamp(CullDistance, LODDistance, 100000.f) : std::max(180.f, LODDistance);
        Hysteresis = std::isfinite(Hysteresis) ? std::clamp(Hysteresis, 0.f, .4f) : .1f;
        if (uint32_t(Backend) > 2) Backend = FoliageBackend::Auto;
    }
    uint32_t ClassifyFoliage(const AABB& bounds, const Frustum& frustum, glm::vec3 camera, const FoliageSettings& settings, uint32_t& history, bool culling)
    {
        const auto delta = bounds.GetCenter() - camera;
        const float distance = glm::length(delta);
        if (history > 1) history = distance > settings.LODDistance ? 1 : 0;
        else if (history == 0 && distance > settings.LODDistance * (1 + settings.Hysteresis)) history = 1;
        else if (history == 1 && distance < settings.LODDistance * (1 - settings.Hysteresis)) history = 0;
        // A bound touching the distance sphere remains visible.
        const float radius = glm::length(bounds.GetExtents());
        if (distance - radius > settings.CullDistance || (culling && !frustum.Intersects(bounds))) return 2;
        return history;
    }
    namespace {
        struct Input
        {
            glm::mat4 Model, Normal;
            glm::vec4 Center, Extent;
        };
        static_assert(sizeof(Input) == 160);
        struct Output
        {
            glm::mat4 Model;
            glm::mat3 Normal;
            int ID;
        };
        static_assert(sizeof(Output) == 104);
        struct Command
        {
            uint32_t Count = 0, Instances = 0, First = 0, BaseVertex = 0, BaseInstance = 0;
        };
        static_assert(sizeof(Command) == 20);
        const char* computeSource = R"(#version 430 core
layout(local_size_x=64)in;
struct Instance{mat4 model;mat4 normal;vec4 center;vec4 extent;};
layout(std430,binding=0)readonly buffer InputData{Instance inputData[];};
layout(std430,binding=1)writeonly buffer NearData{float nearData[];};
layout(std430,binding=2)writeonly buffer FarData{float farData[];};
layout(std430,binding=3)buffer NearCommand{uint nearCmd[];};
layout(std430,binding=4)buffer FarCommand{uint farCmd[];};
layout(std430,binding=5)buffer History{uint history[];};
uniform uint u_Count;uniform int u_ID,u_Frustum,u_Phase,u_NearParts,u_FarParts;uniform vec3 u_Camera;
uniform vec3 u_Settings;uniform vec4 u_Planes[6];
void main(){uint i=gl_GlobalInvocationID.x;
    if(u_Phase==0){if(i==0u){nearCmd[1]=0u;farCmd[1]=0u;}return;}
    if(u_Phase==2){if(i>0u){if(i<uint(u_NearParts))nearCmd[i*5u+1u]=nearCmd[1];if(i<uint(u_FarParts))farCmd[i*5u+1u]=farCmd[1];}return;}
    if(i>=u_Count)return;
    Instance item=inputData[i];float distance=length(item.center.xyz-u_Camera);uint lod=history[i];
    if(lod>1u)lod=distance>u_Settings.x?1u:0u;
    else if(lod==0u&&distance>u_Settings.x*(1.0+u_Settings.z))lod=1u;
    else if(lod==1u&&distance<u_Settings.x*(1.0-u_Settings.z))lod=0u;
    history[i]=lod;
    if(distance-length(item.extent.xyz)>u_Settings.y)return;
    if(u_Frustum!=0)for(int p=0;p<6;++p)if(dot(u_Planes[p].xyz,item.center.xyz)+u_Planes[p].w+dot(abs(u_Planes[p].xyz),item.extent.xyz)<0.0)return;
    uint at=(lod==0u?atomicAdd(nearCmd[1],1u):atomicAdd(farCmd[1],1u))*26u;
    for(uint c=0u;c<4u;++c)for(uint r=0u;r<4u;++r){float v=item.model[c][r];if(lod==0u)nearData[at+c*4u+r]=v;else farData[at+c*4u+r]=v;}
    for(uint c=0u;c<3u;++c)for(uint r=0u;r<3u;++r){float v=item.normal[c][r];if(lod==0u)nearData[at+16u+c*3u+r]=v;else farData[at+16u+c*3u+r]=v;}
    if(lod==0u)nearData[at+25u]=intBitsToFloat(u_ID);else farData[at+25u]=intBitsToFloat(u_ID);
})";
        GLuint BufferID(const Ref<VertexBuffer>& buffer)
        {
            buffer->Bind();
            GLint id = 0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &id);
            return GLuint(id);
        }
        bool Finite(const glm::mat4& m)
        {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(m[c][r])) return false;
            return true;
        }
        BufferLayout Layout()
        {
            BufferLayout l{ { ShaderDataType::Mat4, "Model" }, { ShaderDataType::Mat3, "Normal" }, { ShaderDataType::Int, "ID" } };
            l.SetPerInstance(true);
            return l;
        }
    }
    struct FoliageBatch::Impl
    {
        std::vector<glm::mat4> Transforms;
        std::vector<Input> Inputs;
        std::vector<Output> All, Visible[2];
        std::vector<uint32_t> History;
        Ref<VertexBuffer> Full, VisibleBuffers[2];
        AABB Prototype;
        int ID = -1;
        GLuint Program = 0, InputBuffer = 0, HistoryBuffer = 0, Commands[2]{};
        struct Readback
        {
            GLuint Buffer = 0;
            GLsync Fence = nullptr;
            uint64_t Frame = 0;
        };
        std::array<Readback, 3> Readbacks{};
        std::map<std::pair<const Mesh*, int>, Ref<VertexArray>> VAOs;
        FoliageStats Stats;
        uint64_t Frame = 0;
        std::vector<uint32_t> IndexCounts[2];
        std::vector<const Mesh*> MeshKeys[2];
        bool TriedGPU = false;
        TrackedMemory ExtraMemory{ MemoryCategory::Meshes };
        ~Impl()
        {
            for (auto& r : Readbacks)
            {
                if (r.Fence) glDeleteSync(r.Fence);
                if (r.Buffer) glDeleteBuffers(1, &r.Buffer);
            }
            if (Program) glDeleteProgram(Program);
            if (InputBuffer) glDeleteBuffers(1, &InputBuffer);
            if (HistoryBuffer) glDeleteBuffers(1, &HistoryBuffer);
            if (Commands[0] || Commands[1]) glDeleteBuffers(2, Commands);
        }
        bool InitGPU()
        {
            if (TriedGPU) return Program != 0;
            TriedGPU = true;
            if (!GLAD_GL_VERSION_4_3 || !glDispatchCompute || !glDrawElementsIndirect) return false;
            GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(shader, 1, &computeSource, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[2048]{};
                glGetShaderInfoLog(shader, 2048, nullptr, log);
                Stats.Error = log;
                glDeleteShader(shader);
                return false;
            }
            Program = glCreateProgram();
            glAttachShader(Program, shader);
            glLinkProgram(Program);
            glDeleteShader(shader);
            glGetProgramiv(Program, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                char log[2048]{};
                glGetProgramInfoLog(Program, 2048, nullptr, log);
                Stats.Error = log;
                glDeleteProgram(Program);
                Program = 0;
                return false;
            }
            glGenBuffers(1, &InputBuffer);
            glGenBuffers(1, &HistoryBuffer);
            glGenBuffers(2, Commands);
            for (auto id : Commands)
            {
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, id);
                glBufferData(GL_DRAW_INDIRECT_BUFFER, 128 * sizeof(Command), nullptr, GL_DYNAMIC_DRAW);
            }
            for (auto& r : Readbacks)
            {
                glGenBuffers(1, &r.Buffer);
                glBindBuffer(GL_COPY_WRITE_BUFFER, r.Buffer);
                glBufferData(GL_COPY_WRITE_BUFFER, 2 * sizeof(Command), nullptr, GL_STREAM_READ);
            }
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            return true;
        }
        void Poll()
        {
            for (auto& r : Readbacks)
                if (r.Fence)
                {
                    auto status = glClientWaitSync(r.Fence, 0, 0);
                    if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED)
                    {
                        Command c[2];
                        glBindBuffer(GL_COPY_READ_BUFFER, r.Buffer);
                        glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(c), c);
                        glBindBuffer(GL_COPY_READ_BUFFER, 0);
                        if (r.Frame >= Stats.CountFrame)
                        {
                            Stats.Near = c[0].Instances;
                            Stats.Far = c[1].Instances;
                            Stats.Culled = Stats.Instances - std::min(Stats.Instances, Stats.Near + Stats.Far);
                            Stats.CountFrame = r.Frame;
                            Stats.CountsAvailable = true;
                        }
                        glDeleteSync(r.Fence);
                        r.Fence = nullptr;
                    }
                }
        }
        Ref<VertexArray> VAO(const Ref<Mesh>& mesh, int kind)
        {
            auto key = std::make_pair(mesh.get(), kind);
            auto it = VAOs.find(key);
            if (it != VAOs.end()) return it->second;
            auto vao = VertexArray::Create();
            for (auto& vb : mesh->GetVertexArray()->GetVertexBuffers()) vao->AddVertexBuffer(vb);
            vao->SetIndexBuffer(mesh->GetVertexArray()->GetIndexBuffer());
            vao->AddVertexBuffer(kind == 2 ? Full : VisibleBuffers[kind]);
            VAOs[key] = vao;
            return vao;
        }
    };
    FoliageBatch::FoliageBatch() : m_Impl(CreateScope<Impl>()) {}
    FoliageBatch::~FoliageBatch() = default;
    const FoliageStats& FoliageBatch::GetStats() const { return m_Impl->Stats; }
    const std::vector<glm::mat4>& FoliageBatch::GetTransforms() const { return m_Impl->Transforms; }
    bool FoliageBatch::Upload(const std::vector<glm::mat4>& transforms, const AABB& prototype, int id)
    {
        auto& d = *m_Impl;
        Settings.Sanitize();
        for (const auto& parts : Parts)
        {
            if (parts.empty() || parts.size() > 128)
            {
                d.Stats.Error = "Invalid foliage part count";
                return false;
            }
            for (const auto& part : parts)
                if (!part.Geometry || part.Geometry->IsSkinned())
                {
                    d.Stats.Error = "Invalid foliage geometry";
                    return false;
                }
        }
        bool partsSame = true;
        for (int lod = 0; lod < 2; ++lod)
        {
            std::vector<uint32_t> counts;
            std::vector<const Mesh*> keys;
            for (const auto& p : Parts[lod])
            {
                counts.push_back(p.Geometry->GetIndexCount());
                keys.push_back(p.Geometry.get());
            }
            partsSame &= keys == d.MeshKeys[lod];
            d.MeshKeys[lod] = std::move(keys);
            partsSame &= counts == d.IndexCounts[lod];
            d.IndexCounts[lod] = std::move(counts);
        }
        bool same = partsSame && d.ID == id && d.Prototype.Min == prototype.Min && d.Prototype.Max == prototype.Max && d.Transforms.size() == transforms.size() && (transforms.empty() || std::memcmp(d.Transforms.data(), transforms.data(), transforms.size() * sizeof(glm::mat4)) == 0);
        if (same) return !d.Transforms.empty();
        d.Stats.Error.clear();
        d.Transforms.clear();
        d.Inputs.clear();
        d.All.clear();
        Bounds = AABB::Empty();
        d.ID = id;
        EntityID = id;
        d.Prototype = prototype;
        d.VAOs.clear();
        if (!prototype.IsValid() || transforms.size() > MaxInstances)
        {
            d.Stats.Error = "Invalid foliage bounds/count";
            return false;
        }
        bool first = true;
        for (const auto& matrix : transforms)
        {
            const float det = glm::determinant(glm::mat3(matrix));
            if (!Finite(matrix) || !std::isfinite(det) || std::abs(det) < 1e-8f)
            {
                d.Stats.Error = "Invalid foliage transform";
                d.Transforms.clear();
                d.Inputs.clear();
                d.All.clear();
                return false;
            }
            if (first)
            {
                Mirrored = det < 0;
                first = false;
            }
            else if (Mirrored != (det < 0))
            {
                d.Stats.Error = "Mixed foliage winding is not supported in one batch";
                d.Transforms.clear();
                d.Inputs.clear();
                d.All.clear();
                return false;
            }
            auto box = TransformAABB(prototype, matrix);
            auto normal = glm::transpose(glm::inverse(glm::mat3(matrix)));
            d.Transforms.push_back(matrix);
            d.Inputs.push_back({ matrix, glm::mat4(normal), glm::vec4(box.GetCenter(), 0), glm::vec4(box.GetExtents(), 0) });
            d.All.push_back({ matrix, normal, id });
            Bounds.Encapsulate(box);
        }
        d.History.assign(transforms.size(), 2);
        d.Stats.Instances = uint32_t(transforms.size());
        d.Stats.CountsAvailable = false;
        if (transforms.empty()) return false;
        uint32_t bytes = uint32_t(transforms.size() * sizeof(Output));
        d.Full = VertexBuffer::Create(d.All.data(), bytes);
        d.Full->SetLayout(Layout());
        for (auto& buffer : d.VisibleBuffers)
        {
            buffer = VertexBuffer::Create(bytes);
            buffer->SetLayout(Layout());
        }
        if (d.InitGPU())
        {
            for (int lod = 0; lod < 2; ++lod)
            {
                std::array<Command, 128> commands{};
                for (size_t i = 0; i < std::min(size_t(128), d.IndexCounts[lod].size()); ++i) commands[i].Count = d.IndexCounts[lod][i];
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, d.Commands[lod]);
                glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(commands), commands.data(), GL_DYNAMIC_DRAW);
            }
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, d.InputBuffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, d.Inputs.size() * sizeof(Input), d.Inputs.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, d.HistoryBuffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, d.History.size() * sizeof(uint32_t), d.History.data(), GL_DYNAMIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        // Obsolete asynchronous counts cannot describe this new instance set.
        for (auto& r : d.Readbacks)
            if (r.Fence)
            {
                glDeleteSync(r.Fence);
                r.Fence = nullptr;
            }
        d.Stats.Uploads++;
        d.Stats.CPUBytes = d.Inputs.capacity() * sizeof(Input) + d.All.capacity() * sizeof(Output) + d.Transforms.capacity() * sizeof(glm::mat4) + d.History.capacity() * sizeof(uint32_t);
        d.Stats.GPUBytes = 3ull * bytes + (d.Program ? d.Inputs.size() * sizeof(Input) + d.History.size() * 4 + (2 * 128 + 6) * sizeof(Command) : 0);
        d.ExtraMemory.Set(d.Program ? d.Inputs.size() * sizeof(Input) + d.History.size() * 4 + (2 * 128 + 6) * sizeof(Command) : 0);
        return true;
    }
    void FoliageBatch::Cull(const Frustum& frustum, glm::vec3 camera, bool culling)
    {
        auto& d = *m_Impl;
        ++d.Frame;
        Settings.Sanitize();
        if (d.Transforms.empty()) return;
        d.Poll();
        const bool gpu = Settings.Backend != FoliageBackend::CPU && d.Program;
        // Backend transitions reset history consistently on both sides.
        if (gpu != d.Stats.GPU)
        {
            std::fill(d.History.begin(), d.History.end(), 2);
            if (d.Program)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, d.HistoryBuffer);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, d.History.size() * 4, d.History.data());
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            }
        }
        d.Stats.GPU = gpu;
        if (gpu)
        {
            GLint previous = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
            glUseProgram(d.Program);
            glUniform1ui(glGetUniformLocation(d.Program, "u_Count"), d.Stats.Instances);
            glUniform1i(glGetUniformLocation(d.Program, "u_ID"), EntityID);
            glUniform1i(glGetUniformLocation(d.Program, "u_Frustum"), culling ? 1 : 0);
            glUniform3fv(glGetUniformLocation(d.Program, "u_Camera"), 1, &camera.x);
            glm::vec3 settings{ Settings.LODDistance, Settings.CullDistance, Settings.Hysteresis };
            glUniform3fv(glGetUniformLocation(d.Program, "u_Settings"), 1, &settings.x);
            glUniform4fv(glGetUniformLocation(d.Program, "u_Planes[0]"), 6, &frustum.Planes[0].x);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, d.InputBuffer);
            for (int lod = 0; lod < 2; ++lod)
            {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(1 + lod), BufferID(d.VisibleBuffers[lod]));
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(3 + lod), d.Commands[lod]);
            }
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, d.HistoryBuffer);
            glUniform1i(glGetUniformLocation(d.Program, "u_NearParts"), GLint(Parts[0].size()));
            glUniform1i(glGetUniformLocation(d.Program, "u_FarParts"), GLint(Parts[1].size()));
            glUniform1i(glGetUniformLocation(d.Program, "u_Phase"), 0);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            glUniform1i(glGetUniformLocation(d.Program, "u_Phase"), 1);
            glDispatchCompute((d.Stats.Instances + 63) / 64, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            glUniform1i(glGetUniformLocation(d.Program, "u_Phase"), 2);
            glDispatchCompute(2, 1, 1);
            glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
            for (auto& read : d.Readbacks)
                if (!read.Fence)
                {
                    glBindBuffer(GL_COPY_WRITE_BUFFER, read.Buffer);
                    for (int lod = 0; lod < 2; ++lod)
                    {
                        glBindBuffer(GL_COPY_READ_BUFFER, d.Commands[lod]);
                        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, lod * sizeof(Command), sizeof(Command));
                    }
                    read.Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                    read.Frame = d.Frame;
                    break;
                }
            for (GLuint binding = 0; binding < 6; ++binding) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glUseProgram(GLuint(previous));
        }
        else
        {
            for (auto& v : d.Visible) v.clear();
            for (size_t i = 0; i < d.Inputs.size(); ++i)
            {
                const auto& in = d.Inputs[i];
                auto box = AABB::FromCenterExtents(glm::vec3(in.Center), glm::vec3(in.Extent));
                auto lod = ClassifyFoliage(box, frustum, camera, Settings, d.History[i], culling);
                if (lod < 2) d.Visible[lod].push_back(d.All[i]);
            }
            for (int lod = 0; lod < 2; ++lod)
                if (!d.Visible[lod].empty()) d.VisibleBuffers[lod]->StreamData(d.Visible[lod].data(), uint32_t(d.Visible[lod].size() * sizeof(Output)));
            d.Stats.Near = uint32_t(d.Visible[0].size());
            d.Stats.Far = uint32_t(d.Visible[1].size());
            d.Stats.Culled = d.Stats.Instances - d.Stats.Near - d.Stats.Far;
            d.Stats.CountsAvailable = true;
            d.Stats.CountFrame = d.Frame;
        }
        d.Stats.CPUBytes = d.Inputs.capacity() * sizeof(Input) + d.All.capacity() * sizeof(Output) + d.Transforms.capacity() * sizeof(glm::mat4) + d.History.capacity() * sizeof(uint32_t) + (d.Visible[0].capacity() + d.Visible[1].capacity()) * sizeof(Output);
    }
    bool FoliageBatch::Draw(const Ref<Mesh>& mesh, uint32_t lod, bool shadow)
    {
        auto& d = *m_Impl;
        if (!mesh || mesh->IsSkinned() || lod > 1 || d.Transforms.empty()) return false;
        if (shadow)
        {
            RenderCommand::DrawIndexedInstanced(d.VAO(mesh, 2), mesh->GetIndexCount(), d.Stats.Instances);
            return true;
        }
        if (d.Stats.GPU)
        {
            auto vao = d.VAO(mesh, int(lod));
            vao->Bind();
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, d.Commands[lod]);
            size_t part = 0;
            for (; part < Parts[lod].size(); ++part)
                if (Parts[lod][part].Geometry == mesh) break;
            if (part >= Parts[lod].size())
            {
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
                return false;
            }
            glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, reinterpret_cast<const void*>(part * sizeof(Command)));
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            return true;
        }
        if (d.Visible[lod].empty()) return false;
        RenderCommand::DrawIndexedInstanced(d.VAO(mesh, int(lod)), mesh->GetIndexCount(), uint32_t(d.Visible[lod].size()));
        return true;
    }
    std::array<uint32_t, 2> FoliageBatch::DebugVisibleCounts()
    {
        auto& d = *m_Impl;
        if (!d.Stats.GPU) return { d.Stats.Near, d.Stats.Far };
        std::array<uint32_t, 2> counts{};
        for (int i = 0; i < 2; ++i)
        {
            Command cmd;
            glBindBuffer(GL_COPY_READ_BUFFER, d.Commands[i]);
            glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(cmd), &cmd);
            counts[i] = cmd.Instances;
        }
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        return counts;
    }
}
