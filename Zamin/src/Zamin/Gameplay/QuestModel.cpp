#include "QuestModel.h"
#include "RulesModel.h"
#include <algorithm>
#include <stdexcept>
namespace Zamin::Gameplay {
    bool ValidQuestKey(const std::string& s) { return !s.empty() && s.size() <= 64 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") == std::string::npos; }
    const char* QuestModeName(QuestMode m)
    {
        switch (m)
        {
            case QuestMode::Active:  return "Active";
            case QuestMode::Ready:   return "Ready";
            case QuestMode::Claimed: return "Claimed";
            case QuestMode::Failed:  return "Failed";
            default:                 return "Locked";
        }
    }
    bool ValidateQuest(const QuestSpec& s, std::string& error)
    {
        error.clear();
        if (!ValidQuestKey(s.Key) || s.Title.empty() || s.Title.size() > 128 || s.Objectives.empty() || s.Objectives.size() > 8 || s.Requires.size() > 8 || s.Rewards.size() > 8 || s.XP > 1000000)
        {
            error = "Quest definition limits";
            return false;
        }
        std::set<std::string> keys;
        for (auto& o : s.Objectives)
            if (!ValidQuestKey(o.Key) || !keys.insert(o.Key).second || o.Label.empty() || o.Label.size() > 96 || o.Match.size() > 64 || int(o.Kind) < 0 || int(o.Kind) > 3 || o.Goal < 1 || o.Goal > 1000000 || (o.Kind == QuestKind::Collect && !ValidItem(o.Match)))
            {
                error = "Invalid quest objective";
                return false;
            }
        keys.clear();
        for (auto& r : s.Requires)
            if (!ValidQuestKey(r) || r == s.Key || !keys.insert(r).second)
            {
                error = "Invalid quest prerequisite";
                return false;
            }
        for (auto& [key, count] : s.Rewards)
            if (!ValidItem(key) || count < 1 || count > 999999)
            {
                error = "Invalid quest reward";
                return false;
            }
        return true;
    }
    QuestProgress NewQuestProgress(const QuestSpec& s)
    {
        std::string error;
        if (!ValidateQuest(s, error)) throw std::runtime_error(error);
        QuestProgress p;
        p.Counts.resize(s.Objectives.size());
        return p;
    }
    bool ValidateQuestProgress(const QuestSpec& s, const QuestProgress& p, std::string& error)
    {
        if (!ValidateQuest(s, error)) return false;
        if (p.Counts.size() != s.Objectives.size() || p.Receipts.size() > 64 || int(p.Mode) < 0 || int(p.Mode) > 4)
        {
            error = "Quest progress layout";
            return false;
        }
        bool complete = true, empty = true;
        for (size_t i = 0; i < p.Counts.size(); ++i)
        {
            if (p.Counts[i] > s.Objectives[i].Goal)
            {
                error = "Quest count exceeds goal";
                return false;
            }
            complete &= p.Counts[i] == s.Objectives[i].Goal;
            empty &= p.Counts[i] == 0;
        }
        for (auto& r : p.Receipts)
            if (!ValidQuestKey(r))
            {
                error = "Invalid quest receipt";
                return false;
            }
        if ((p.Mode == QuestMode::Locked && (!empty || !p.Receipts.empty())) || (p.Mode == QuestMode::Active && complete) || ((p.Mode == QuestMode::Ready || p.Mode == QuestMode::Claimed) && !complete))
        {
            error = "Inconsistent quest mode";
            return false;
        }
        error.clear();
        return true;
    }
    bool StartQuest(const QuestSpec& s, QuestProgress& p, std::string& error)
    {
        if (!ValidateQuestProgress(s, p, error)) return false;
        if (p.Mode != QuestMode::Locked)
        {
            error = "Quest already started";
            return false;
        }
        p.Mode = QuestMode::Active;
        return true;
    }
    bool AdvanceQuest(const QuestSpec& s, QuestProgress& p, const std::string& key, uint32_t amount, const std::string& receipt, bool manual, std::string& error)
    {
        if (!ValidateQuestProgress(s, p, error)) return false;
        if (p.Mode != QuestMode::Active || amount < 1 || amount > 1000000)
        {
            error = "Quest is not active or amount invalid";
            return false;
        }
        auto it = std::find_if(s.Objectives.begin(), s.Objectives.end(), [&](auto& o) { return o.Key == key; });
        if (it == s.Objectives.end() || (manual && it->Kind != QuestKind::Manual))
        {
            error = "Quest objective unavailable";
            return false;
        }
        size_t index = size_t(it - s.Objectives.begin());
        if (p.Counts[index] == it->Goal)
        {
            error = "Quest objective complete";
            return false;
        }
        if (manual && (!ValidQuestKey(receipt) || p.Receipts.contains(receipt) || p.Receipts.size() >= 64))
        {
            error = "Quest receipt duplicate/invalid or capacity 64";
            return false;
        }
        auto next = p;
        if (manual) next.Receipts.insert(receipt);
        next.Counts[index] += std::min(amount, it->Goal - next.Counts[index]);
        bool all = true;
        for (size_t i = 0; i < next.Counts.size(); ++i) all &= next.Counts[i] == s.Objectives[i].Goal;
        if (all) next.Mode = QuestMode::Ready;
        p = std::move(next);
        error.clear();
        return true;
    }
    bool FailQuest(QuestProgress& p, std::string& error)
    {
        error.clear();
        if (p.Mode != QuestMode::Active && p.Mode != QuestMode::Ready)
        {
            error = "Quest cannot fail in this mode";
            return false;
        }
        p.Mode = QuestMode::Failed;
        return true;
    }
    float QuestFraction(const QuestSpec& s, const QuestProgress& p)
    {
        std::string error;
        if (!ValidateQuestProgress(s, p, error)) throw std::runtime_error(error);
        double sum = 0;
        for (size_t i = 0; i < p.Counts.size(); ++i) sum += double(p.Counts[i]) / s.Objectives[i].Goal;
        return float(sum / p.Counts.size());
    }
    LevelProgress QuestLevel(uint32_t xp)
    {
        if (xp > 1000000000) throw std::runtime_error("Quest XP limit");
        LevelProgress p;
        uint32_t base = 0;
        while (p.Level < 100 && xp - base >= 100 * p.Level)
        {
            base += 100 * p.Level;
            ++p.Level;
        }
        p.Current = xp - base;
        p.Needed = p.Level == 100 ? 0 : 100 * p.Level;
        p.Fraction = p.Needed ? float(p.Current) / p.Needed : 1;
        return p;
    }
}
