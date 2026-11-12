#pragma once
#include "SaveModel.h"
#include <filesystem>
#include <map>
namespace Zamin::Gameplay {
    struct SaveResult
    {
        bool Success = false, Recovered = false;
        size_t Entities = 0;
        std::string Reason;
    };
    // Opt-in, stable-key gameplay persistence. Bind/recreate authored prefab slots before loading.
    // Configuration stays in content; snapshots restore compatible runtime state, not arbitrary ECS.
    class SaveWorld
    {
    public:
        static SaveWorld& For(Scene&);
        explicit SaveWorld(Scene* scene) : m_Scene(scene) {}
        bool Bind(Entity, const std::string&, std::string& error);
        Entity Spawn(const std::string& key, const std::string& prefab, glm::vec3 position, std::string& error);
        Entity Find(const std::string&) const;
        bool Unbind(const std::string&);
        GameSave Capture(const std::string& schema) const;
        SaveResult Restore(const GameSave&, const std::string& schema);
        SaveResult Save(const std::string& slot, const std::string& schema);
        SaveResult Load(const std::string& slot, const std::string& schema);
        SaveResult Inspect(const std::string& slot);
        std::filesystem::path Path(const std::string& slot) const;
        void BeginFrame() { m_IO = m_Spawns = 0; }
        size_t Count() const { return m_Bindings.size(); }
    private:
        struct Binding
        {
            uint64_t ID = 0;
            uint32_t Handle = ~0u;
        };
        std::string Signature(Entity) const;
        std::string Reference(uint64_t) const;
        bool Read(const std::filesystem::path&, GameSave&, std::string&) const;
        Scene* m_Scene;
        std::map<std::string, Binding> m_Bindings;
        unsigned m_IO = 0, m_Spawns = 0;
        bool m_Busy = false;
    };
    void ResetSaves(Scene&);
    void BeginSaveFrame(Scene&);
}
