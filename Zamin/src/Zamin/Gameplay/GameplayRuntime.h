#pragma once
#include "GameplayMotor.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include <map>
#include <cstdint>
namespace Zamin {
    class Entity;
    class Scene;
}
namespace Zamin::Gameplay {
    struct Checkpoint
    {
        std::string Scene;
        uint64_t Actor = 0;
        glm::vec3 Position{}, Rotation{};
    };
    std::string EncodeCheckpoint(const Checkpoint&);
    bool DecodeCheckpoint(std::string_view text, Checkpoint&, std::string& error);
    // Per graph instance. No global scene pointers; UUIDs are resolved afresh, generational handles
    // distinguish a destroyed/recreated target. Only targets in the host's scene are accepted.
    class Runtime
    {
    public:
        void BeginFrame(Scene&);
        void Stop(Scene&);
        void AfterPhysics() { m_AfterPhysics = true; }
        void Reset();
        Visual::Value Dispatch(Entity host, Entity target, const std::string&, const std::map<std::string, Visual::Value>&);
        size_t MotorCount() const { return m_Motors.size(); }
    private:
        struct MotorRecord
        {
            uint32_t Handle = 0xffffffff;
            uint64_t Frame = UINT64_MAX;
            MotorState State;
            glm::vec3 Measured{};
            bool TwoD = false, TopDown = false;
        };
        struct CameraRecord
        {
            uint32_t Handle = 0xffffffff;
            uint64_t Anchor = 0;
            CameraState State;
        };
        std::map<uint64_t, MotorRecord> m_Motors;
        std::map<uint64_t, CameraRecord> m_Cameras;
        std::map<std::string, Checkpoint> m_Checkpoints;
        uint64_t m_Frame = 0;
        unsigned m_IO = 0;
        bool m_AfterPhysics = false;
    };
}
