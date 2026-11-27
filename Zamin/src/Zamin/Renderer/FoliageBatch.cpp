#include "Zamin/Renderer/FoliageBatch.h"
#include "Zamin/Renderer/Buffer.h"
#include "Zamin/Renderer/VertexArray.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Core/MemoryStats.h"
#include <glad/glad.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>

namespace Zamin {
    void FoliageSettings::Sanitize()
    {
        LODDistance = std::isfinite(LODDistance) ? std::clamp(LODDistance, 1.f, 100000.f) : 40;
        CullDistance = std::isfinite(CullDistance) ? std::clamp(CullDistance, LODDistance, 100000.f) : std::max(180.f, LODDistance);
        Hysteresis = std::isfinite(Hysteresis) ? std::clamp(Hysteresis, 0.f, .4f) : .1f;
        HLODDistance = std::isfinite(HLODDistance) && HLODDistance > 0 ? std::clamp(HLODDistance, LODDistance, CullDistance) : 0;
        HLODFade = std::isfinite(HLODFade) ? std::clamp(HLODFade, 0.f, 10000.f) : 8;
        HLODCellSize = std::isfinite(HLODCellSize) ? std::clamp(HLODCellSize, 4.f, 4096.f) : 32;
        if (uint32_t(Backend) > 2) Backend = FoliageBackend::Auto;
    }
    bool FoliageInsideFrustum(const Frustum& frustum, const glm::vec3& c, const glm::vec3& e)
    {
        for (const auto& p : frustum.Planes)
            if (p.x * c.x + p.y * c.y + p.z * c.z + p.w + (std::abs(p.x) * e.x + std::abs(p.y) * e.y + std::abs(p.z) * e.z) < 0) return false;
        return true;
    }
    namespace {
        // The LOD history step both backends take (the GPU shader has the same expressions).
        uint32_t StepLOD(float distance, const FoliageSettings& s, uint32_t& history)
        {
            if (history > 1) history = distance > s.LODDistance ? 1 : 0;
            else if (history == 0 && distance > s.LODDistance * (1 + s.Hysteresis)) history = 1;
            else if (history == 1 && distance < s.LODDistance * (1 - s.Hysteresis)) history = 0;
            return history;
        }
    }
    uint32_t ClassifyFoliage(const AABB& bounds, const Frustum& frustum, glm::vec3 camera, const FoliageSettings& settings, uint32_t& history, bool culling)
    {
        const glm::vec3 center = bounds.GetCenter(), extent = bounds.GetExtents();
        const float distance = glm::length(center - camera);
        StepLOD(distance, settings, history);
        // A bound touching the distance sphere remains visible.
        if (distance - glm::length(extent) > settings.CullDistance || (culling && !FoliageInsideFrustum(frustum, center, extent))) return 2;
        return history;
    }
    namespace {
        struct Input
        {
            glm::mat4 Model, Normal;
            glm::vec3 Center;
            uint32_t Cell;
            glm::vec4 Extent;
        };
        static_assert(sizeof(Input) == 160);
        struct Output
        {
            glm::mat4 Model;
            glm::mat3 Normal;
            int ID;
            float Fade; // the HLOD crossfade (0: none); the mesh shader dithers it out
        };
        static_assert(sizeof(Output) == 108);
        struct Command
        {
            uint32_t Count = 0, Instances = 0, First = 0, BaseVertex = 0, BaseInstance = 0;
        };
        static_assert(sizeof(Command) == 20);
        // One command buffer per set: the near parts at 0..127, the far parts at 128..255. Near instances are written
        // from the front of the set's buffer, far ones from the back (their commands' base instance points there).
        constexpr uint32_t FarCommand = 128;
        const char* computeSource = R"(#version 430 core
layout(local_size_x=64)in;
struct Instance{mat4 model;mat4 normal;vec3 center;uint cell;vec4 extent;};
layout(std430,binding=0)readonly buffer InputData{Instance inputData[];};
layout(std430,binding=1)writeonly buffer CameraData{float cameraData[];};
layout(std430,binding=2)writeonly buffer ShadowData{float shadowData[];};
layout(std430,binding=3)buffer CameraCommand{uint cameraCmd[];};
layout(std430,binding=4)buffer ShadowCommand{uint shadowCmd[];};
layout(std430,binding=5)buffer History{uint history[];};
layout(std430,binding=6)readonly buffer CellLevels{uint cellLevel[];};
uniform uint u_Count;uniform int u_ID,u_Frustum,u_Phase,u_NearParts,u_FarParts,u_HLOD,u_Shadows;uniform vec3 u_Camera;
uniform vec3 u_Settings;uniform vec4 u_Planes[6];uniform vec4 u_ShadowPlanes[24];
const uint FAR=640u;
bool Inside(vec4 p,vec3 c,vec3 e){return p.x*c.x+p.y*c.y+p.z*c.z+p.w+(abs(p.x)*e.x+abs(p.y)*e.y+abs(p.z)*e.z)>=0.0;}
void main(){uint i=gl_GlobalInvocationID.x;
    if(u_Phase==0){if(i==0u){cameraCmd[1]=0u;cameraCmd[FAR+1u]=0u;shadowCmd[1]=0u;shadowCmd[FAR+1u]=0u;}return;}
    if(u_Phase==2){if(i<128u){uint n=cameraCmd[1],f=cameraCmd[FAR+1u],sn=shadowCmd[1],sf=shadowCmd[FAR+1u];
        if(i>0u&&i<uint(u_NearParts)){cameraCmd[i*5u+1u]=n;shadowCmd[i*5u+1u]=sn;}
        if(i<uint(u_FarParts)){if(i>0u){cameraCmd[FAR+i*5u+1u]=f;shadowCmd[FAR+i*5u+1u]=sf;}cameraCmd[FAR+i*5u+4u]=u_Count-f;shadowCmd[FAR+i*5u+4u]=u_Count-sf;}}return;}
    if(i>=u_Count)return;
    Instance item=inputData[i];float distance=length(item.center-u_Camera);uint lod=history[i];
    if(lod>1u)lod=distance>u_Settings.x?1u:0u;
    else if(lod==0u&&distance>u_Settings.x*(1.0+u_Settings.z))lod=1u;
    else if(lod==1u&&distance<u_Settings.x*(1.0-u_Settings.z))lod=0u;
    history[i]=lod;
    if(distance-length(item.extent.xyz)>u_Settings.y)return;
    float fade=0.0;
    if(u_HLOD!=0){uint level=cellLevel[item.cell];if(level>=16u)return;fade=float(level)/16.0;}
    bool camera=true;
    if(u_Frustum!=0)for(int p=0;p<6;++p)if(!Inside(u_Planes[p],item.center,item.extent.xyz))camera=false;
    if(camera){uint at=lod==0u?atomicAdd(cameraCmd[1],1u):u_Count-1u-atomicAdd(cameraCmd[FAR+1u],1u);uint b=at*27u;
        for(uint c=0u;c<4u;++c)for(uint r=0u;r<4u;++r)cameraData[b+c*4u+r]=item.model[c][r];
        for(uint c=0u;c<3u;++c)for(uint r=0u;r<3u;++r)cameraData[b+16u+c*3u+r]=item.normal[c][r];
        cameraData[b+25u]=intBitsToFloat(u_ID);cameraData[b+26u]=fade;}
    if(u_Shadows>0){bool any=u_Frustum==0;
        for(int v=0;v<u_Shadows&&!any;++v){bool inside=true;for(int p=0;p<6;++p)if(!Inside(u_ShadowPlanes[v*6+p],item.center,item.extent.xyz))inside=false;any=inside;}
        if(any){uint at=lod==0u?atomicAdd(shadowCmd[1],1u):u_Count-1u-atomicAdd(shadowCmd[FAR+1u],1u);uint b=at*27u;
            for(uint c=0u;c<4u;++c)for(uint r=0u;r<4u;++r)shadowData[b+c*4u+r]=item.model[c][r];
            for(uint c=0u;c<3u;++c)for(uint r=0u;r<3u;++r)shadowData[b+16u+c*3u+r]=item.normal[c][r];
            shadowData[b+25u]=intBitsToFloat(u_ID);shadowData[b+26u]=0.0;}}
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
            BufferLayout l{ { ShaderDataType::Mat4, "Model" }, { ShaderDataType::Mat3, "Normal" }, { ShaderDataType::Int, "ID" }, { ShaderDataType::Float, "Fade" } };
            l.SetPerInstance(true);
            return l;
        }
        double Milliseconds(std::chrono::steady_clock::time_point since)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
        }
        // Vertex buffer kinds: the CPU path's streamed sets (camera near/far, shadow near/far) and the GPU sets.
        enum Kind : int
        {
            CameraNear = 0,
            CameraFar,
            ShadowNear,
            ShadowFar,
            GPUCamera,
            GPUShadow,
            KindCount
        };
    }
    struct FoliageBatch::Impl
    {
        std::vector<glm::mat4> Transforms;
        std::vector<Input> Inputs;
        std::vector<Output> All, Visible[4];
        std::vector<uint32_t> History;
        Ref<VertexBuffer> Buffers[KindCount];
        uint32_t Capacity[KindCount]{};
        AABB Prototype;
        int ID = -1;
        uint64_t Generation = 0;
        GLuint Program = 0, InputBuffer = 0, HistoryBuffer = 0, CellBuffer = 0, Commands[2]{};
        uint32_t GPUCapacity = 0, CellCapacity = 0;
        bool InputsDirty = true;
        struct Readback
        {
            GLuint Buffer = 0;
            GLsync Fence = nullptr;
            uint64_t Frame = 0;
        };
        std::array<Readback, 3> Readbacks{};
        struct Timer
        {
            GLuint Begin = 0, End = 0;
            bool Pending = false;
        };
        std::array<Timer, 4> Timers{};
        bool TimersSupported = false;
        std::map<std::pair<const Mesh*, int>, Ref<VertexArray>> VAOs;
        FoliageStats Stats;
        uint64_t Frame = 0;
        bool ShadowSet = false; // the last Cull produced a shadow set
        std::vector<uint32_t> IndexCounts[2];
        std::vector<const Mesh*> MeshKeys[2];
        bool TriedGPU = false;
        TrackedMemory ExtraMemory{ MemoryCategory::Meshes };
        // HLOD
        FoliageHLODBake Bake;
        bool BakeCurrent = false;  // Bake describes Transforms at the requested cell size
        float BakeRequestedSize = 0;
        Ref<Mesh> ProxyMesh;
        uint64_t ProxyHash = 0;
        Material ProxyMaterial;
        std::vector<uint32_t> CellLevels;
        std::array<std::vector<GLsizei>, FoliageHLOD::FadeLevels + 1> LevelCounts;
        std::array<std::vector<const void*>, FoliageHLOD::FadeLevels + 1> LevelOffsets;
        ~Impl()
        {
            for (auto& r : Readbacks)
            {
                if (r.Fence) glDeleteSync(r.Fence);
                if (r.Buffer) glDeleteBuffers(1, &r.Buffer);
            }
            for (auto& t : Timers)
                if (t.Begin)
                {
                    glDeleteQueries(1, &t.Begin);
                    glDeleteQueries(1, &t.End);
                }
            if (Program) glDeleteProgram(Program);
            for (GLuint* id : { &InputBuffer, &HistoryBuffer, &CellBuffer, &Commands[0], &Commands[1] })
                if (*id) glDeleteBuffers(1, id);
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
            glGenBuffers(1, &CellBuffer);
            glGenBuffers(2, Commands);
            for (auto& r : Readbacks)
            {
                glGenBuffers(1, &r.Buffer);
                glBindBuffer(GL_COPY_WRITE_BUFFER, r.Buffer);
                glBufferData(GL_COPY_WRITE_BUFFER, 4 * sizeof(Command), nullptr, GL_STREAM_READ);
            }
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            GLint bits = 0;
            if (GLAD_GL_VERSION_3_3) glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
            TimersSupported = bits > 0;
            if (TimersSupported)
                for (auto& t : Timers)
                {
                    glGenQueries(1, &t.Begin);
                    glGenQueries(1, &t.End);
                }
            return true;
        }
        void WriteCommands()
        {
            for (GLuint id : Commands)
            {
                std::array<Command, 2 * FarCommand> commands{};
                for (int lod = 0; lod < 2; ++lod)
                    for (size_t i = 0; i < std::min(size_t(FarCommand), IndexCounts[lod].size()); ++i) commands[lod * FarCommand + i].Count = IndexCounts[lod][i];
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, id);
                glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(commands), commands.data(), GL_DYNAMIC_DRAW);
            }
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        }
        // The GPU path's buffers for the current source (made when the GPU path is first used).
        void EnsureGPUResources()
        {
            const auto count = uint32_t(Inputs.size());
            if (GPUCapacity != count)
            {
                GPUCapacity = count;
                const uint32_t bytes = std::max<uint32_t>(1, count) * uint32_t(sizeof(Output));
                for (int kind : { GPUCamera, GPUShadow })
                {
                    Buffers[kind] = VertexBuffer::Create(bytes);
                    Buffers[kind]->SetLayout(Layout());
                    Capacity[kind] = count;
                    DropVAOs(kind);
                }
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, HistoryBuffer);
                glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<size_t>(1, History.size()) * sizeof(uint32_t), History.data(), GL_DYNAMIC_DRAW);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
                WriteCommands();
                InputsDirty = true;
            }
            if (InputsDirty)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, InputBuffer);
                glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<size_t>(1, Inputs.size()) * sizeof(Input), Inputs.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
                InputsDirty = false;
            }
            const auto cells = uint32_t(std::max<size_t>(1, CellLevels.size()));
            if (CellCapacity < cells)
            {
                CellCapacity = cells;
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, CellBuffer);
                glBufferData(GL_SHADER_STORAGE_BUFFER, cells * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            }
        }
        void DropVAOs(int kind)
        {
            for (auto it = VAOs.begin(); it != VAOs.end();)
                it = it->first.second == kind ? VAOs.erase(it) : std::next(it);
        }
        // The CPU path's streamed buffer of a set: grown (and its VAOs remade) when the visible count outgrows it.
        void Stream(int kind, const std::vector<Output>& data)
        {
            if (data.empty()) return;
            if (!Buffers[kind] || Capacity[kind] < data.size())
            {
                Capacity[kind] = std::min<uint32_t>(uint32_t(Inputs.size()), std::max<uint32_t>(1024, uint32_t(data.size() + data.size() / 2)));
                Buffers[kind] = VertexBuffer::Create(Capacity[kind] * uint32_t(sizeof(Output)));
                Buffers[kind]->SetLayout(Layout());
                DropVAOs(kind);
            }
            Buffers[kind]->StreamData(data.data(), uint32_t(data.size() * sizeof(Output)));
        }
        void Poll()
        {
            for (auto& r : Readbacks)
                if (r.Fence)
                {
                    auto status = glClientWaitSync(r.Fence, 0, 0);
                    if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED)
                    {
                        Command c[4];
                        glBindBuffer(GL_COPY_READ_BUFFER, r.Buffer);
                        glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(c), c);
                        glBindBuffer(GL_COPY_READ_BUFFER, 0);
                        if (r.Frame >= Stats.CountFrame)
                        {
                            Stats.Near = c[0].Instances;
                            Stats.Far = c[1].Instances;
                            Stats.ShadowNear = c[2].Instances;
                            Stats.ShadowFar = c[3].Instances;
                            Stats.Culled = Stats.Instances - std::min(Stats.Instances, Stats.Near + Stats.Far + Stats.HLODInstances);
                            Stats.CountFrame = r.Frame;
                            Stats.CountsAvailable = true;
                        }
                        glDeleteSync(r.Fence);
                        r.Fence = nullptr;
                    }
                }
            PollTimers(false);
        }
        void PollTimers(bool wait)
        {
            for (auto& t : Timers)
            {
                if (!t.Pending) continue;
                GLint ready = 0;
                glGetQueryObjectiv(t.End, GL_QUERY_RESULT_AVAILABLE, &ready);
                if (!ready && !wait) continue;
                GLuint64 begin = 0, end = 0;
                glGetQueryObjectui64v(t.Begin, GL_QUERY_RESULT, &begin);
                glGetQueryObjectui64v(t.End, GL_QUERY_RESULT, &end);
                t.Pending = false;
                if (end >= begin)
                {
                    Stats.CullGPUMilliseconds = double(end - begin) / 1e6;
                    Stats.GPUTimeAvailable = true;
                }
            }
        }
        Ref<VertexArray> VAO(const Ref<Mesh>& mesh, int kind)
        {
            auto key = std::make_pair(mesh.get(), kind);
            auto it = VAOs.find(key);
            if (it != VAOs.end()) return it->second;
            if (!Buffers[kind]) return nullptr;
            auto vao = VertexArray::Create();
            for (auto& vb : mesh->GetVertexArray()->GetVertexBuffers()) vao->AddVertexBuffer(vb);
            vao->SetIndexBuffer(mesh->GetVertexArray()->GetIndexBuffer());
            vao->AddVertexBuffer(Buffers[kind]);
            VAOs[key] = vao;
            return vao;
        }
        // Bakes the HLOD proxies when they are on and stale (reusing the unchanged cells of the last bake).
        void EnsureHLOD(const FoliageSettings& settings, const std::vector<FoliagePart>& farParts)
        {
            if (!settings.HLODEnabled() || Transforms.empty())
            {
                if (ProxyMesh) ProxyMesh.reset();
                ProxyHash = 0;
                CellLevels.clear();
                Stats.HLODCells = 0;
                return;
            }
            if (!BakeCurrent || BakeRequestedSize != settings.HLODCellSize)
            {
                auto previous = std::move(Bake);
                Bake = FoliageHLOD::Bake(Transforms, Prototype, settings.HLODCellSize, previous.IsValid() ? &previous : nullptr);
                BakeCurrent = true;
                BakeRequestedSize = settings.HLODCellSize;
                for (size_t i = 0; i < Inputs.size() && i < Bake.InstanceCell.size(); ++i) Inputs[i].Cell = Bake.InstanceCell[i];
                InputsDirty = true;
                Stats.HLODBakes++;
                Stats.HLODRebakedCells = Bake.RebakedCells;
                Stats.HLODReusedCells = Bake.ReusedCells;
                Stats.HLODHash = Bake.Hash;
                Stats.HLODCellSize = Bake.CellSize;
            }
            Stats.HLODCells = uint32_t(Bake.Cells.size());
            if (Bake.IsValid() && (!ProxyMesh || ProxyHash != Bake.Hash))
            {
                ProxyMesh = Bake.Indices.empty() ? nullptr : CreateRef<Mesh>(Bake.Vertices, Bake.Indices);
                ProxyHash = Bake.Hash;
            }
            // The far LOD's look without its maps (the proxy has no texture coordinates).
            ProxyMaterial = Material{};
            if (!farParts.empty())
            {
                ProxyMaterial.Albedo = farParts[0].Mat.Albedo;
                ProxyMaterial.Roughness = farParts[0].Mat.Roughness;
                ProxyMaterial.Metallic = farParts[0].Mat.Metallic;
            }
            ProxyMaterial.Albedo.a = 1;
        }
        // Per-cell crossfade steps and the visible proxies by step (the same table feeds both backends).
        void ClassifyCells(const Frustum& frustum, glm::vec3 camera, bool culling, const FoliageSettings& s)
        {
            Stats.HLODInstances = Stats.HLODCellsDrawn = Stats.HLODCellsFading = Stats.HLODTriangles = 0;
            for (auto& v : LevelCounts) v.clear();
            for (auto& v : LevelOffsets) v.clear();
            if (!s.HLODEnabled() || !Bake.IsValid())
            {
                CellLevels.clear();
                return;
            }
            CellLevels.resize(Bake.Cells.size());
            for (size_t i = 0; i < Bake.Cells.size(); ++i)
            {
                const auto& cell = Bake.Cells[i];
                const glm::vec3 center = cell.Bounds.GetCenter(), extent = cell.Bounds.GetExtents();
                const float distance = glm::length(center - camera);
                const uint32_t level = FoliageHLOD::FadeLevel(distance, s.HLODDistance, s.HLODFade);
                CellLevels[i] = level;
                if (level >= FoliageHLOD::FadeLevels) Stats.HLODInstances += cell.Instances;
                if (!level || !cell.IndexCount) continue;
                if (distance - glm::length(extent) > s.CullDistance || (culling && !FoliageInsideFrustum(frustum, center, extent))) continue;
                LevelCounts[level].push_back(GLsizei(cell.IndexCount));
                LevelOffsets[level].push_back(reinterpret_cast<const void*>(uintptr_t(cell.FirstIndex) * sizeof(uint32_t)));
                Stats.HLODCellsDrawn++;
                Stats.HLODCellsFading += level < FoliageHLOD::FadeLevels;
                Stats.HLODTriangles += cell.IndexCount / 3;
            }
        }
        void UpdateMemory()
        {
            Stats.CPUBytes = Inputs.capacity() * sizeof(Input) + All.capacity() * sizeof(Output) + Transforms.capacity() * sizeof(glm::mat4) + History.capacity() * sizeof(uint32_t) +
                             Bake.Vertices.capacity() * sizeof(MeshVertex) + Bake.Indices.capacity() * 4 + Bake.InstanceCell.capacity() * 4 + Bake.Cells.capacity() * sizeof(FoliageHLODCell);
            for (const auto& v : Visible) Stats.CPUBytes += v.capacity() * sizeof(Output);
            uint64_t extra = 0;
            if (Program && GPUCapacity) extra = uint64_t(GPUCapacity) * (sizeof(Input) + 4) + uint64_t(CellCapacity) * 4 + (2 * 2 * FarCommand + 4 * 3) * sizeof(Command);
            uint64_t buffers = 0;
            for (int k = 0; k < KindCount; ++k)
                if (Buffers[k]) buffers += uint64_t(std::max<uint32_t>(1, Capacity[k])) * sizeof(Output);
            if (ProxyMesh) buffers += Bake.Vertices.size() * sizeof(MeshVertex) + Bake.Indices.size() * 4;
            Stats.GPUBytes = buffers + extra;
            ExtraMemory.Set(extra);
        }
    };
    FoliageBatch::FoliageBatch() : m_Impl(CreateScope<Impl>()) {}
    FoliageBatch::~FoliageBatch() = default;
    const FoliageStats& FoliageBatch::GetStats() const { return m_Impl->Stats; }
    const std::vector<glm::mat4>& FoliageBatch::GetTransforms() const { return m_Impl->Transforms; }
    const Ref<Mesh>& FoliageBatch::GetHLODMesh() const { return m_Impl->ProxyMesh; }
    const Material& FoliageBatch::GetHLODMaterial() const { return m_Impl->ProxyMaterial; }
    const FoliageHLODBake& FoliageBatch::GetHLODBake() const { return m_Impl->Bake; }
    bool FoliageBatch::Upload(const std::vector<glm::mat4>& transforms, const AABB& prototype, int id, uint64_t generation)
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
        const bool sourceSame = generation && generation == d.Generation ? d.Transforms.size() == transforms.size()
                                                                         : d.Transforms.size() == transforms.size() && (transforms.empty() || std::memcmp(d.Transforms.data(), transforms.data(), transforms.size() * sizeof(glm::mat4)) == 0);
        bool same = partsSame && sourceSame && d.ID == id && d.Prototype.Min == prototype.Min && d.Prototype.Max == prototype.Max;
        if (same)
        {
            d.Generation = generation;
            d.EnsureHLOD(Settings, Parts[1]);
            return !d.Transforms.empty();
        }
        d.Stats.Error.clear();
        d.Transforms.clear();
        d.Inputs.clear();
        d.All.clear();
        Bounds = AABB::Empty();
        d.ID = id;
        EntityID = id;
        d.Prototype = prototype;
        d.Generation = generation;
        d.BakeCurrent = false;
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
            d.Inputs.push_back({ matrix, glm::mat4(normal), box.GetCenter(), 0, glm::vec4(box.GetExtents(), 0) });
            d.All.push_back({ matrix, normal, id, 0 });
            Bounds.Encapsulate(box);
        }
        d.History.assign(transforms.size(), 2);
        d.Stats.Instances = uint32_t(transforms.size());
        d.Stats.CountsAvailable = false;
        for (int kind = 0; kind < KindCount; ++kind)
        {
            d.Buffers[kind].reset();
            d.Capacity[kind] = 0;
        }
        d.GPUCapacity = 0;
        d.InputsDirty = true;
        d.EnsureHLOD(Settings, Parts[1]);
        if (transforms.empty()) return false;
        // Obsolete asynchronous counts cannot describe this new instance set.
        for (auto& r : d.Readbacks)
            if (r.Fence)
            {
                glDeleteSync(r.Fence);
                r.Fence = nullptr;
            }
        if (!partsSame && d.Program) d.WriteCommands();
        d.Stats.Uploads++;
        d.UpdateMemory();
        return true;
    }
    void FoliageBatch::Cull(const Frustum& frustum, glm::vec3 camera, bool culling, std::span<const Frustum> shadowVolumes)
    {
        auto& d = *m_Impl;
        const auto started = std::chrono::steady_clock::now();
        ++d.Frame;
        Settings.Sanitize();
        if (d.Transforms.empty()) return;
        d.EnsureHLOD(Settings, Parts[1]);
        const bool gpu = Settings.Backend != FoliageBackend::CPU && d.InitGPU();
        if (d.Program) d.Poll();
        // Backend transitions reset history consistently on both sides.
        if (gpu != d.Stats.GPU)
        {
            std::fill(d.History.begin(), d.History.end(), 2);
            if (d.Program && d.GPUCapacity)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, d.HistoryBuffer);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, d.History.size() * 4, d.History.data());
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            }
            d.Stats.CountsAvailable = false;
        }
        d.Stats.GPU = gpu;
        d.ClassifyCells(frustum, camera, culling, Settings);
        const bool hlod = !d.CellLevels.empty();
        const uint32_t volumes = CastShadows ? uint32_t(std::min<size_t>(shadowVolumes.size(), MaxShadowVolumes)) : 0;
        d.ShadowSet = volumes > 0;
        if (gpu)
        {
            d.EnsureGPUResources();
            if (hlod)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, d.CellBuffer);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, d.CellLevels.size() * 4, d.CellLevels.data());
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            }
            Impl::Timer* timer = nullptr;
            if (d.TimersSupported)
                for (auto& t : d.Timers)
                    if (!t.Pending)
                    {
                        timer = &t;
                        break;
                    }
            if (timer) glQueryCounter(timer->Begin, GL_TIMESTAMP);
            GLint previous = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
            glUseProgram(d.Program);
            glUniform1ui(glGetUniformLocation(d.Program, "u_Count"), d.Stats.Instances);
            glUniform1i(glGetUniformLocation(d.Program, "u_ID"), EntityID);
            glUniform1i(glGetUniformLocation(d.Program, "u_Frustum"), culling ? 1 : 0);
            glUniform1i(glGetUniformLocation(d.Program, "u_HLOD"), hlod ? 1 : 0);
            glUniform1i(glGetUniformLocation(d.Program, "u_Shadows"), GLint(volumes));
            glUniform3fv(glGetUniformLocation(d.Program, "u_Camera"), 1, &camera.x);
            glm::vec3 settings{ Settings.LODDistance, Settings.CullDistance, Settings.Hysteresis };
            glUniform3fv(glGetUniformLocation(d.Program, "u_Settings"), 1, &settings.x);
            glUniform4fv(glGetUniformLocation(d.Program, "u_Planes[0]"), 6, &frustum.Planes[0].x);
            if (volumes)
            {
                glm::vec4 planes[6 * MaxShadowVolumes]{};
                for (uint32_t v = 0; v < volumes; ++v)
                    for (int p = 0; p < 6; ++p) planes[v * 6 + p] = shadowVolumes[v].Planes[p];
                glUniform4fv(glGetUniformLocation(d.Program, "u_ShadowPlanes[0]"), GLsizei(6 * volumes), &planes[0].x);
            }
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, d.InputBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, BufferID(d.Buffers[GPUCamera]));
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, BufferID(d.Buffers[GPUShadow]));
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, d.Commands[0]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, d.Commands[1]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, d.HistoryBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, d.CellBuffer);
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
            if (timer)
            {
                glQueryCounter(timer->End, GL_TIMESTAMP);
                timer->Pending = true;
            }
            for (auto& read : d.Readbacks)
                if (!read.Fence)
                {
                    glBindBuffer(GL_COPY_WRITE_BUFFER, read.Buffer);
                    for (int set = 0; set < 2; ++set)
                    {
                        glBindBuffer(GL_COPY_READ_BUFFER, d.Commands[set]);
                        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, (set * 2) * sizeof(Command), sizeof(Command));
                        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, FarCommand * sizeof(Command), (set * 2 + 1) * sizeof(Command), sizeof(Command));
                    }
                    read.Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                    read.Frame = d.Frame;
                    break;
                }
            for (GLuint binding = 0; binding < 7; ++binding) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glUseProgram(GLuint(previous));
        }
        else
        {
            for (auto& v : d.Visible) v.clear();
            const auto& s = Settings;
            for (size_t i = 0; i < d.Inputs.size(); ++i)
            {
                const auto& in = d.Inputs[i];
                const glm::vec3 extent(in.Extent);
                const float distance = glm::length(in.Center - camera);
                const uint32_t lod = StepLOD(distance, s, d.History[i]);
                if (distance - glm::length(extent) > s.CullDistance) continue;
                float fade = 0;
                if (hlod)
                {
                    const uint32_t level = d.CellLevels[in.Cell];
                    if (level >= FoliageHLOD::FadeLevels) continue;
                    fade = float(level) / float(FoliageHLOD::FadeLevels);
                }
                if (!culling || FoliageInsideFrustum(frustum, in.Center, extent))
                {
                    d.Visible[lod].push_back(d.All[i]);
                    d.Visible[lod].back().Fade = fade;
                }
                if (volumes)
                {
                    bool any = !culling;
                    for (uint32_t v = 0; v < volumes && !any; ++v) any = FoliageInsideFrustum(shadowVolumes[v], in.Center, extent);
                    if (any) d.Visible[2 + lod].push_back(d.All[i]);
                }
            }
            for (int kind = 0; kind < 4; ++kind) d.Stream(kind, d.Visible[kind]);
            d.Stats.Near = uint32_t(d.Visible[0].size());
            d.Stats.Far = uint32_t(d.Visible[1].size());
            d.Stats.ShadowNear = uint32_t(d.Visible[2].size());
            d.Stats.ShadowFar = uint32_t(d.Visible[3].size());
            d.Stats.Culled = d.Stats.Instances - std::min(d.Stats.Instances, d.Stats.Near + d.Stats.Far + d.Stats.HLODInstances);
            d.Stats.CountsAvailable = true;
            d.Stats.CountFrame = d.Frame;
        }
        d.Stats.CullCPUMilliseconds = Milliseconds(started);
        d.UpdateMemory();
    }
    bool FoliageBatch::Draw(const Ref<Mesh>& mesh, uint32_t lod, bool shadow)
    {
        auto& d = *m_Impl;
        if (!mesh || mesh->IsSkinned() || lod > 1 || d.Transforms.empty()) return false;
        if (shadow && !d.ShadowSet) return false;
        size_t part = 0;
        for (; part < Parts[lod].size(); ++part)
            if (Parts[lod][part].Geometry == mesh) break;
        if (part >= Parts[lod].size()) return false;
        if (d.Stats.GPU)
        {
            const int set = shadow ? 1 : 0;
            auto vao = d.VAO(mesh, shadow ? GPUShadow : GPUCamera);
            if (!vao) return false;
            vao->Bind();
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, d.Commands[set]);
            glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, reinterpret_cast<const void*>((lod * FarCommand + part) * sizeof(Command)));
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            return true;
        }
        const int kind = int(lod) + (shadow ? 2 : 0);
        if (d.Visible[kind].empty()) return false;
        auto vao = d.VAO(mesh, kind);
        if (!vao) return false;
        RenderCommand::DrawIndexedInstanced(vao, mesh->GetIndexCount(), uint32_t(d.Visible[kind].size()));
        return true;
    }
    uint32_t FoliageBatch::DrawHLOD(Shader& shader)
    {
        auto& d = *m_Impl;
        if (!d.ProxyMesh || d.CellLevels.empty()) return 0;
        uint32_t draws = 0;
        const auto& vao = d.ProxyMesh->GetVertexArray();
        for (uint32_t level = 1; level <= FoliageHLOD::FadeLevels; ++level)
        {
            if (d.LevelCounts[level].empty()) continue;
            if (!draws) vao->Bind();
            // The proxy's half of the crossfade: -1 - t (the instances take +t); -2 draws it whole.
            shader.SetFloat("u_LODFade", -1.0f - float(level) / float(FoliageHLOD::FadeLevels));
            glMultiDrawElements(GL_TRIANGLES, d.LevelCounts[level].data(), GL_UNSIGNED_INT, d.LevelOffsets[level].data(), GLsizei(d.LevelCounts[level].size()));
            ++draws;
        }
        if (draws) shader.SetFloat("u_LODFade", 0.0f);
        d.Stats.HLODDraws = draws;
        return draws;
    }
    std::array<uint32_t, 2> FoliageBatch::DebugVisibleCounts()
    {
        auto& d = *m_Impl;
        if (!d.Stats.GPU) return { d.Stats.Near, d.Stats.Far };
        std::array<uint32_t, 2> counts{};
        Command cmd;
        glBindBuffer(GL_COPY_READ_BUFFER, d.Commands[0]);
        glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(cmd), &cmd);
        counts[0] = cmd.Instances;
        glGetBufferSubData(GL_COPY_READ_BUFFER, FarCommand * sizeof(Command), sizeof(cmd), &cmd);
        counts[1] = cmd.Instances;
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        return counts;
    }
    std::array<uint32_t, 2> FoliageBatch::DebugShadowCounts()
    {
        auto& d = *m_Impl;
        if (!d.ShadowSet) return { 0, 0 };
        if (!d.Stats.GPU) return { d.Stats.ShadowNear, d.Stats.ShadowFar };
        std::array<uint32_t, 2> counts{};
        Command cmd;
        glBindBuffer(GL_COPY_READ_BUFFER, d.Commands[1]);
        glGetBufferSubData(GL_COPY_READ_BUFFER, 0, sizeof(cmd), &cmd);
        counts[0] = cmd.Instances;
        glGetBufferSubData(GL_COPY_READ_BUFFER, FarCommand * sizeof(Command), sizeof(cmd), &cmd);
        counts[1] = cmd.Instances;
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        return counts;
    }
    void FoliageBatch::DebugResolveTimings()
    {
        if (m_Impl->Program && m_Impl->TimersSupported) m_Impl->PollTimers(true);
    }
}
