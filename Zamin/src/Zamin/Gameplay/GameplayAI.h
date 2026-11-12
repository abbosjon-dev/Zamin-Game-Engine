#pragma once
#include "BrainModel.h"
#include "Zamin/AI/NavRecovery.h"
#include <map>
#include <deque>
#include <optional>
#include <vector>
namespace Zamin {
    class Scene;
    class Entity;
}
namespace Zamin::Gameplay {
    struct AISpec
    {
        BrainSettings Brain;
        NavRecovery::Settings Recovery{ true, .75f, .5f, .05f, 3 };
        std::string Tag = "Player";
        uint64_t Target = 0;
        std::vector<glm::vec3> Points;
        glm::vec3 Eye{ 0, 1, 0 }, Facing{ 1, 0, 0 };
        uint16_t Mask = 65535;
        bool Relative = true, Enabled = true;
    };
    struct AISnapshot
    {
        BrainMemory Brain;
        uint64_t Target = 0;
        glm::vec3 Home{}, Goal{}, LastSeen{}, Facing{ 1, 0, 0 };
        std::string Status;
        uint32_t Sightings = 0, Losses = 0, Attacks = 0, Kills = 0, Transitions = 0, PathRequests = 0, Retries = 0, Dropped = 0;
        bool Visible = false, Enabled = true;
    };
    struct AIEvent
    {
        std::string Mode;
        uint64_t Target = 0;
    };
    class AIWorld
    {
    public:
        static AIWorld& For(Scene&);
        explicit AIWorld(Scene* scene) : m_Scene(scene) {}
        AIWorld(const AIWorld&) = delete;
        AIWorld& operator=(const AIWorld&) = delete;
        bool Configure(Entity owner, Entity actor, const AISpec&, bool reset, std::string& error);
        bool Enable(Entity, bool);
        bool Reset(Entity, bool rehome, std::string& error);
        bool Remove(Entity);
        void Clear();
        void Prune();
        void Tick(float delta);
        std::optional<AISnapshot> State(Entity) const;
        std::optional<AIEvent> Poll(Entity);
        size_t Count() const { return m_Records.size(); }
    private:
        struct Record
        {
            uint32_t Handle = ~0u, OwnerHandle = ~0u, TargetHandle = ~0u, Deaths = 0;
            uint64_t Owner = 0;
            bool ScriptOwner = false, Issued = false;
            float OriginalSpeed = 0, OriginalStop = 0, OriginalRepath = 0, RepathLeft = 0;
            NavRecovery::Settings OriginalRecovery;
            AISpec Spec;
            AISnapshot State;
            NavRecovery Recovery;
            std::vector<glm::vec3> Route;
            std::deque<AIEvent> Events;
        };
        void Check(Entity) const;
        Record* Find(Entity);
        const Record* Find(Entity) const;
        void Stop(Entity, Record&);
        void Release(Entity, Record&);
        void Transition(Record&, BrainMode);
        Scene* m_Scene;
        std::map<uint64_t, Record> m_Records;
    };
    void ResetAI(Scene&);
    void BeginAIFrame(Scene&);
    void TickAI(Scene&, float delta);
}
