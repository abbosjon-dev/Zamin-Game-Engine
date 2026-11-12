#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace Zamin::Gameplay {
    enum class QuestKind
    {
        Manual,
        Collect,
        Kill,
        Interact
    };
    enum class QuestMode
    {
        Locked,
        Active,
        Ready,
        Claimed,
        Failed
    };
    struct QuestObjectiveSpec
    {
        std::string Key = "Explore", Label = "Reach checkpoint", Match;
        QuestKind Kind = QuestKind::Manual;
        uint32_t Goal = 1;
    };
    struct QuestSpec
    {
        std::string Key = "Quest", Title = "Field survey";
        std::vector<QuestObjectiveSpec> Objectives;
        std::vector<std::string> Requires;
        std::map<std::string, int> Rewards;
        uint32_t XP = 100;
    };
    struct QuestProgress
    {
        QuestMode Mode = QuestMode::Locked;
        std::vector<uint32_t> Counts;
        std::set<std::string> Receipts;
    };
    struct QuestBookSave
    {
        std::map<std::string, QuestProgress> Quests;
        uint32_t XP = 0;
    };
    struct LevelProgress
    {
        uint32_t Level = 1, Current = 0, Needed = 100;
        float Fraction = 0;
    };
    bool ValidQuestKey(const std::string&);
    bool ValidateQuest(const QuestSpec&, std::string& error);
    bool ValidateQuestProgress(const QuestSpec&, const QuestProgress&, std::string& error);
    QuestProgress NewQuestProgress(const QuestSpec&);
    bool StartQuest(const QuestSpec&, QuestProgress&, std::string& error);
    bool AdvanceQuest(const QuestSpec&, QuestProgress&, const std::string& objective, uint32_t amount, const std::string& receipt, bool manual, std::string& error);
    bool FailQuest(QuestProgress&, std::string& error);
    float QuestFraction(const QuestSpec&, const QuestProgress&);
    const char* QuestModeName(QuestMode);
    LevelProgress QuestLevel(uint32_t xp);
}
