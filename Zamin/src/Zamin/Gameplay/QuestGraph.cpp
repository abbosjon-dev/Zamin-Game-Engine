#include "QuestWorld.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/Visual/VisualGraph.h"
#include <cmath>
namespace Zamin::Gameplay {
    std::optional<Visual::Value> DispatchQuest(Entity host, Entity actor, const std::string& kind, const std::map<std::string, Visual::Value>& a)
    {
        using V = Visual::Value;
        if (!kind.starts_with("Gameplay.Quest")) return {};
        auto& w = QuestWorld::For(*host.GetScene());
        auto text = [&](const char* k) { return a.at(k).get<std::string>(); };
        std::string error;
        bool ok = false;
        auto integer = [](const V& v, uint32_t min, uint32_t max) {if(!v.is_number())throw std::runtime_error("Quest integer expected");double n=v.get<double>();if(!std::isfinite(n)||n<min||n>max||std::floor(n)!=n)throw std::runtime_error("Quest integer outside limits");return uint32_t(n); };
        if (kind == "Gameplay.QuestConfigure")
        {
            QuestSpec s;
            s.Key = text("Quest");
            s.Title = text("Title");
            s.XP = integer(a.at("XP"), 0, 1000000);
            auto& keys = a.at("Objectives");
            auto& labels = a.at("Labels");
            auto& kinds = a.at("Kinds");
            auto& matches = a.at("Matches");
            auto& goals = a.at("Goals");
            if (!keys.is_array() || keys.empty() || keys.size() > 8 || !labels.is_array() || !kinds.is_array() || !matches.is_array() || !goals.is_array() || labels.size() != keys.size() || kinds.size() != keys.size() || matches.size() != keys.size() || goals.size() != keys.size()) throw std::runtime_error("Quest arrays must have matching lengths 1..8");
            for (size_t i = 0; i < keys.size(); ++i) s.Objectives.push_back({ keys[i].get<std::string>(), labels[i].get<std::string>(), matches[i].get<std::string>(), QuestKind(integer(kinds[i], 0, 3)), integer(goals[i], 1, 1000000) });
            auto& requirements = a.at("Requires");
            if (!requirements.is_array() || requirements.size() > 8) throw std::runtime_error("Quest prerequisite limit 8");
            for (auto& r : requirements) s.Requires.push_back(r.get<std::string>());
            auto& items = a.at("Rewards");
            auto& counts = a.at("RewardCounts");
            if (!items.is_array() || items.size() > 8 || !counts.is_array() || items.size() != counts.size()) throw std::runtime_error("Quest reward arrays limit 8");
            for (size_t i = 0; i < items.size(); ++i)
                if (!s.Rewards.emplace(items[i].get<std::string>(), int(integer(counts[i], 1, 999999))).second) throw std::runtime_error("Duplicate quest reward");
            ok = w.Configure(actor, s, error);
        }
        else if (kind == "Gameplay.QuestStart") ok = w.Start(actor, text("Quest"), error);
        else if (kind == "Gameplay.QuestAdvance") ok = w.Advance(actor, text("Quest"), text("Objective"), integer(a.at("Count"), 1, 1000000), text("Receipt"), error);
        else if (kind == "Gameplay.QuestFail") ok = w.Fail(actor, text("Quest"), error);
        else if (kind == "Gameplay.QuestClaim") ok = w.Claim(actor, text("Quest"), error);
        else if (kind == "Gameplay.QuestState")
        {
            auto s = w.State(actor, text("Quest"));
            return V{ { "Configured", bool(s) }, { "Title", s ? s->Spec.Title : "" }, { "Mode", s ? QuestModeName(s->Progress.Mode) : "" }, { "Fraction", s ? s->Fraction : 0.f }, { "Complete", s ? s->Complete : 0u }, { "Objectives", s ? s->Spec.Objectives.size() : 0u }, { "Ready", s && s->Progress.Mode == QuestMode::Ready }, { "Claimed", s && s->Progress.Mode == QuestMode::Claimed } };
        }
        else if (kind == "Gameplay.QuestObjective")
        {
            auto s = w.State(actor, text("Quest"));
            if (s)
                for (size_t i = 0; i < s->Spec.Objectives.size(); ++i)
                    if (s->Spec.Objectives[i].Key == text("Objective"))
                    {
                        auto& o = s->Spec.Objectives[i];
                        return V{ { "Found", true }, { "Label", o.Label }, { "Current", s->Progress.Counts[i] }, { "Goal", o.Goal }, { "Complete", s->Progress.Counts[i] == o.Goal } };
                    }
            return V{ { "Found", false }, { "Label", "" }, { "Current", 0 }, { "Goal", 0 }, { "Complete", false } };
        }
        else if (kind == "Gameplay.QuestProgression")
        {
            auto p = w.Progression(actor);
            return V{ { "XP", w.XP(actor) }, { "Level", p.Level }, { "Current", p.Current }, { "Needed", p.Needed }, { "Fraction", p.Fraction } };
        }
        else if (kind == "Gameplay.QuestEvent")
        {
            auto e = w.Poll(actor);
            return V{ { "Has", bool(e) }, { "Kind", e ? e->Kind : "" }, { "Quest", e ? e->Quest : "" }, { "Objective", e ? e->Objective : "" }, { "Amount", e ? e->Amount : 0u }, { "Dropped", w.Dropped(actor) } };
        }
        else if (kind == "Gameplay.QuestBindHUD")
        {
            auto id = a.at("Actor").get<uint64_t>();
            auto source = id == UINT64_MAX ? host : id ? host.GetScene()->GetEntityByUUID(UUID(id))
                                                       : Entity{};
            ok = w.BindHUD(host, source, actor, text("Quest"), text("Objective"), error);
        }
        else if (kind == "Gameplay.QuestUnbindHUD") ok = w.UnbindHUD(actor);
        else throw std::runtime_error("Unknown quest operation");
        if (!ok && error.empty()) error = "Quest operation unavailable";
        return V{ { "Success", ok }, { "Reason", error } };
    }
}
