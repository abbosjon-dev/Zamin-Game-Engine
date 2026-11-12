#pragma once
#include "QuestModel.h"
#include <deque>
#include <optional>
namespace Zamin {
    class Scene;
    class Entity;
}
namespace Zamin::Gameplay {
    struct QuestSnapshot
    {
        QuestSpec Spec;
        QuestProgress Progress;
        float Fraction = 0;
        uint32_t Complete = 0;
    };
    struct QuestEvent
    {
        std::string Kind, Quest, Objective;
        uint32_t Amount = 0;
    };
    class QuestWorld
    {
    public:
        static QuestWorld& For(Scene&);
        explicit QuestWorld(Scene* scene) : m_Scene(scene) {}
        QuestWorld(const QuestWorld&) = delete;
        QuestWorld& operator=(const QuestWorld&) = delete;
        bool Configure(Entity actor, const QuestSpec&, std::string& error);
        bool Start(Entity, const std::string&, std::string& error);
        bool Advance(Entity, const std::string&, const std::string&, uint32_t, const std::string&, std::string& error);
        bool Fail(Entity, const std::string&, std::string& error);
        bool Claim(Entity, const std::string&, std::string& error);
        std::optional<QuestSnapshot> State(Entity, const std::string&) const;
        LevelProgress Progression(Entity) const;
        uint32_t XP(Entity) const;
        std::optional<QuestEvent> Poll(Entity);
        uint32_t Dropped(Entity) const;
        void Notice(Entity actor, QuestKind, const std::string& match, uint32_t amount, Entity subject);
        bool BindHUD(Entity owner, Entity actor, Entity widget, const std::string& quest, const std::string& objective, std::string& error);
        bool UnbindHUD(Entity widget);
        bool HasHUD(Entity widget) const;
        void BeginFrame();
        void Refresh();
        void Clear();
    private:
        friend class SaveWorld;
        struct Ref
        {
            uint64_t ID = 0;
            uint32_t Handle = ~0u;
        };
        struct Record
        {
            uint32_t Handle = ~0u;
            std::map<std::string, QuestSpec> Definitions;
            QuestBookSave Book;
            std::deque<QuestEvent> Events;
            uint32_t Dropped = 0;
        };
        struct HUD
        {
            Ref Owner, Actor, Widget;
            std::string Quest, Objective, OriginalText;
            float OriginalValue = 0, OriginalDisplay = 0;
            bool ScriptOwner = false;
        };
        Record* Find(Entity);
        const Record* Find(Entity) const;
        bool Usable(Entity, std::string&) const;
        Entity Resolve(Ref) const;
        Ref Reference(Entity) const;
        void Emit(Record&, QuestEvent);
        void RestoreWidget(const HUD&);
        Scene* m_Scene;
        std::map<uint64_t, Record> m_Actors;
        std::map<uint64_t, HUD> m_HUD;
    };
    void ResetQuests(Scene&);
    void BeginQuestFrame(Scene&);
    void RefreshQuests(Scene&);
    bool QuestWidgetBound(Scene&, Entity);
    void NotifyQuest(Scene&, Entity, QuestKind, const std::string&, uint32_t, Entity);
}
