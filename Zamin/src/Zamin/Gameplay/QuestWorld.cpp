#include "QuestWorld.h"
#include "GameplayRules.h"
#include "GameplayUI.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include <algorithm>
namespace Zamin::Gameplay {
    QuestWorld& QuestWorld::For(Scene& s)
    {
        auto& ctx = s.GetRegistry().ctx();
        if (auto* q = ctx.find<QuestWorld>()) return *q;
        return ctx.emplace<QuestWorld>(&s);
    }
    void ResetQuests(Scene& s)
    {
        if (auto* q = s.GetRegistry().ctx().find<QuestWorld>()) q->Clear();
        s.GetRegistry().ctx().erase<QuestWorld>();
    }
    void BeginQuestFrame(Scene& s)
    {
        if (auto* q = s.GetRegistry().ctx().find<QuestWorld>()) q->BeginFrame();
    }
    void RefreshQuests(Scene& s)
    {
        if (auto* q = s.GetRegistry().ctx().find<QuestWorld>()) q->Refresh();
    }
    void NotifyQuest(Scene& s, Entity actor, QuestKind k, const std::string& match, uint32_t amount, Entity subject)
    {
        if (auto* q = s.GetRegistry().ctx().find<QuestWorld>()) q->Notice(actor, k, match, amount, subject);
    }
    bool QuestWidgetBound(Scene& s, Entity e)
    {
        auto* q = s.GetRegistry().ctx().find<QuestWorld>();
        return q && q->HasHUD(e);
    }
    QuestWorld::Ref QuestWorld::Reference(Entity e) const { return e ? Ref{ uint64_t(e.GetUUID()), uint32_t(e) } : Ref{}; }
    Entity QuestWorld::Resolve(Ref r) const
    {
        auto e = r.ID ? m_Scene->GetEntityByUUID(UUID(r.ID)) : Entity{};
        return e && uint32_t(e) == r.Handle ? e : Entity{};
    }
    QuestWorld::Record* QuestWorld::Find(Entity e)
    {
        if (!e || e.GetScene() != m_Scene) return nullptr;
        auto i = m_Actors.find(uint64_t(e.GetUUID()));
        return i != m_Actors.end() && i->second.Handle == uint32_t(e) ? &i->second : nullptr;
    }
    const QuestWorld::Record* QuestWorld::Find(Entity e) const { return const_cast<QuestWorld*>(this)->Find(e); }
    bool QuestWorld::Usable(Entity e, std::string& error) const
    {
        error.clear();
        if (!m_Scene->IsRunning() || !e || e.GetScene() != m_Scene || !e.IsActiveInHierarchy())
        {
            error = "Quest actor must be active in Play";
            return false;
        }
        auto h = RuleWorld::For(*m_Scene).Health(e);
        if (!h || h->Dead)
        {
            error = "Quest actor needs living configured health";
            return false;
        }
        return true;
    }
    void QuestWorld::Emit(Record& r, QuestEvent event)
    {
        if (r.Events.size() == 32)
        {
            r.Events.pop_front();
            if (r.Dropped != UINT32_MAX) ++r.Dropped;
        }
        r.Events.push_back(std::move(event));
    }
    bool QuestWorld::Configure(Entity actor, const QuestSpec& s, std::string& error)
    {
        if (!Usable(actor, error) || !ValidateQuest(s, error)) return false;
        auto* r = Find(actor);
        if (r && r->Definitions.contains(s.Key))
        {
            error = "Quest already configured";
            return false;
        }
        if ((r && r->Definitions.size() >= 16) || (!r && !m_Actors.contains(uint64_t(actor.GetUUID())) && m_Actors.size() >= 32))
        {
            error = "Quest limits: 32 actors, 16 definitions";
            return false;
        }
        for (auto& key : s.Requires)
            if (!r || !r->Definitions.contains(key))
            {
                error = "Configure prerequisites first (no forward/cyclic references)";
                return false;
            }
        auto progress = NewQuestProgress(s);
        auto id = uint64_t(actor.GetUUID());
        if (!r)
        {
            m_Actors.erase(id);
            auto& fresh = m_Actors[id];
            fresh.Handle = uint32_t(actor);
            r = &fresh;
        }
        r->Definitions.emplace(s.Key, s);
        r->Book.Quests.emplace(s.Key, std::move(progress));
        return true;
    }
    bool QuestWorld::Start(Entity actor, const std::string& key, std::string& error)
    {
        if (!Usable(actor, error)) return false;
        auto* r = Find(actor);
        if (!r || !r->Definitions.contains(key))
        {
            error = "Quest not configured";
            return false;
        }
        auto& s = r->Definitions.at(key);
        for (auto& required : s.Requires)
            if (r->Book.Quests.at(required).Mode != QuestMode::Claimed)
            {
                error = "Quest prerequisite is not claimed";
                return false;
            }
        if (!StartQuest(s, r->Book.Quests.at(key), error)) return false;
        Emit(*r, { "Started", key, "", 0 });
        return true;
    }
    bool QuestWorld::Advance(Entity actor, const std::string& key, const std::string& objective, uint32_t amount, const std::string& receipt, std::string& error)
    {
        if (!Usable(actor, error)) return false;
        auto* r = Find(actor);
        if (!r || !r->Definitions.contains(key))
        {
            error = "Quest not configured";
            return false;
        }
        auto& p = r->Book.Quests.at(key);
        if (!AdvanceQuest(r->Definitions.at(key), p, objective, amount, receipt, true, error)) return false;
        Emit(*r, { "Objective", key, objective, amount });
        if (p.Mode == QuestMode::Ready) Emit(*r, { "Ready", key, "", 0 });
        return true;
    }
    bool QuestWorld::Fail(Entity actor, const std::string& key, std::string& error)
    {
        if (!Usable(actor, error)) return false;
        auto* r = Find(actor);
        if (!r || !r->Book.Quests.contains(key))
        {
            error = "Quest not configured";
            return false;
        }
        if (!FailQuest(r->Book.Quests.at(key), error)) return false;
        Emit(*r, { "Failed", key, "", 0 });
        return true;
    }
    bool QuestWorld::Claim(Entity actor, const std::string& key, std::string& error)
    {
        if (!Usable(actor, error)) return false;
        auto* r = Find(actor);
        if (!r || !r->Definitions.contains(key) || r->Book.Quests.at(key).Mode != QuestMode::Ready)
        {
            error = "Quest is not ready to claim";
            return false;
        }
        auto& rules = RuleWorld::For(*m_Scene);
        auto* a = rules.FindActor(actor);
        auto& s = r->Definitions.at(key);
        auto bag = a->Inventory;
        for (auto& [item, count] : s.Rewards)
            if (!bag.Exchange("", 0, false, item, count, error)) return false;
        if (s.XP > 1000000000 - r->Book.XP)
        {
            error = "Quest XP overflow";
            return false;
        }
        auto before = QuestLevel(r->Book.XP).Level;
        a->Inventory = std::move(bag);
        r->Book.XP += s.XP;
        r->Book.Quests.at(key).Mode = QuestMode::Claimed;
        for (auto& [item, count] : s.Rewards) rules.Emit(*a, { "InventoryChanged", item, uint64_t(actor.GetUUID()), uint64_t(actor.GetUUID()), double(count) });
        Emit(*r, { "Claimed", key, "", s.XP });
        auto after = QuestLevel(r->Book.XP).Level;
        if (after != before) Emit(*r, { "LevelUp", key, "", after });
        return true;
    }
    std::optional<QuestSnapshot> QuestWorld::State(Entity actor, const std::string& key) const
    {
        auto* r = Find(actor);
        if (!r || !r->Definitions.contains(key)) return {};
        QuestSnapshot out{ r->Definitions.at(key), r->Book.Quests.at(key) };
        out.Fraction = QuestFraction(out.Spec, out.Progress);
        for (size_t i = 0; i < out.Progress.Counts.size(); ++i) out.Complete += out.Progress.Counts[i] == out.Spec.Objectives[i].Goal;
        return out;
    }
    uint32_t QuestWorld::XP(Entity e) const
    {
        auto* r = Find(e);
        return r ? r->Book.XP : 0;
    }
    LevelProgress QuestWorld::Progression(Entity e) const { return QuestLevel(XP(e)); }
    std::optional<QuestEvent> QuestWorld::Poll(Entity e)
    {
        auto* r = Find(e);
        if (!r || r->Events.empty()) return {};
        auto event = std::move(r->Events.front());
        r->Events.pop_front();
        return event;
    }
    uint32_t QuestWorld::Dropped(Entity e) const
    {
        auto* r = Find(e);
        return r ? r->Dropped : 0;
    }
    void QuestWorld::Notice(Entity actor, QuestKind kind, const std::string& match, uint32_t amount, Entity subject)
    {
        std::string error;
        if (!Usable(actor, error) || kind == QuestKind::Manual || amount < 1 || amount > 1000000) return;
        auto* r = Find(actor);
        if (!r) return;
        for (auto& [key, s] : r->Definitions)
        {
            auto& p = r->Book.Quests.at(key);
            if (p.Mode != QuestMode::Active) continue;
            for (auto& o : s.Objectives)
            {
                if (o.Kind != kind) continue;
                bool matches = kind == QuestKind::Collect ? o.Match == match : o.Match.empty() || (subject && subject.HasTag(o.Match));
                if (!matches) continue;
                if (AdvanceQuest(s, p, o.Key, amount, "", false, error))
                {
                    Emit(*r, { "Objective", key, o.Key, amount });
                    if (p.Mode == QuestMode::Ready) Emit(*r, { "Ready", key, "", 0 });
                }
            }
        }
    }
    bool QuestWorld::HasHUD(Entity widget) const
    {
        if (!widget || widget.GetScene() != m_Scene) return false;
        auto it = m_HUD.find(uint64_t(widget.GetUUID()));
        return it != m_HUD.end() && Resolve(it->second.Widget) == widget;
    }
    bool QuestWorld::BindHUD(Entity owner, Entity actor, Entity widget, const std::string& key, const std::string& objective, std::string& error)
    {
        error.clear();
        auto state = State(actor, key);
        if (!owner || owner.GetScene() != m_Scene || !widget || widget.GetScene() != m_Scene || !widget.HasComponent<UITransformComponent>() || (!widget.HasComponent<TextComponent>() && !widget.HasComponent<UIProgressBarComponent>()) || !state || m_HUD.size() >= 128 || m_HUD.contains(uint64_t(widget.GetUUID())) || (widget.HasComponent<TextComponent>() && !widget.GetComponent<TextComponent>().LocalizationKey.empty()))
        {
            error = "Invalid quest HUD binding or capacity (128)";
            return false;
        }
        if (!objective.empty() && std::none_of(state->Spec.Objectives.begin(), state->Spec.Objectives.end(), [&](auto& o) { return o.Key == objective; }))
        {
            error = "Quest HUD objective missing";
            return false;
        }
        if (auto* ui = m_Scene->GetRegistry().ctx().find<UIWorld>(); ui && ui->WidgetUsed(widget))
        {
            error = "UI widget already bound";
            return false;
        }
        HUD h;
        h.Owner = Reference(owner);
        h.Actor = Reference(actor);
        h.Widget = Reference(widget);
        h.Quest = key;
        h.Objective = objective;
        h.ScriptOwner = owner.HasComponent<NativeScriptComponent>();
        if (auto* t = widget.TryGetComponent<TextComponent>()) h.OriginalText = t->Text;
        if (auto* b = widget.TryGetComponent<UIProgressBarComponent>())
        {
            h.OriginalValue = b->Value;
            h.OriginalDisplay = b->DisplayValue;
        }
        m_HUD.emplace(uint64_t(widget.GetUUID()), std::move(h));
        return true;
    }
    void QuestWorld::RestoreWidget(const HUD& h)
    {
        auto e = Resolve(h.Widget);
        if (!e) return;
        if (auto* t = e.TryGetComponent<TextComponent>()) t->Text = h.OriginalText;
        if (auto* b = e.TryGetComponent<UIProgressBarComponent>())
        {
            b->Value = h.OriginalValue;
            b->DisplayValue = h.OriginalDisplay;
        }
    }
    bool QuestWorld::UnbindHUD(Entity e)
    {
        if (!HasHUD(e)) return false;
        auto i = m_HUD.find(uint64_t(e.GetUUID()));
        RestoreWidget(i->second);
        m_HUD.erase(i);
        return true;
    }
    void QuestWorld::BeginFrame()
    {
        std::erase_if(m_Actors, [&](auto& item) {auto e=m_Scene->GetEntityByUUID(UUID(item.first));return !e||uint32_t(e)!=item.second.Handle; });
        Refresh();
    }
    void QuestWorld::Refresh()
    {
        for (auto i = m_HUD.begin(); i != m_HUD.end();)
        {
            auto& h = i->second;
            auto owner = Resolve(h.Owner), widget = Resolve(h.Widget), actor = Resolve(h.Actor);
            if (!owner || !widget || !widget.HasComponent<UITransformComponent>() || (!widget.HasComponent<TextComponent>() && !widget.HasComponent<UIProgressBarComponent>()) || (h.ScriptOwner && !owner.HasComponent<NativeScriptComponent>()))
            {
                RestoreWidget(h);
                i = m_HUD.erase(i);
                continue;
            }
            ++i;
            if (!owner.IsActiveInHierarchy()) continue;
            if (auto* n = owner.TryGetComponent<NativeScriptComponent>())
            {
                if (!n->Enabled || (h.ScriptOwner && !n->Instance)) continue;
                if (auto* g = dynamic_cast<VisualGraphHost*>(n->Instance); g && (g->VM.Paused || !g->VM.Error().empty())) continue;
            }
            auto state = State(actor, h.Quest);
            std::string label = "Quest unavailable";
            float fraction = 0;
            if (state)
            {
                label = state->Spec.Title + " | " + QuestModeName(state->Progress.Mode) + " | " + std::to_string(state->Complete) + " / " + std::to_string(state->Spec.Objectives.size());
                fraction = state->Fraction;
                if (!h.Objective.empty())
                    for (size_t n = 0; n < state->Spec.Objectives.size(); ++n)
                        if (state->Spec.Objectives[n].Key == h.Objective)
                        {
                            auto& o = state->Spec.Objectives[n];
                            label = o.Label + "  " + std::to_string(state->Progress.Counts[n]) + " / " + std::to_string(o.Goal);
                            fraction = float(state->Progress.Counts[n]) / o.Goal;
                        }
            }
            if (auto* t = widget.TryGetComponent<TextComponent>()) t->Text = label;
            if (auto* b = widget.TryGetComponent<UIProgressBarComponent>())
            {
                b->Value = fraction;
                if (b->SmoothSpeed <= 0) b->DisplayValue = fraction;
            }
        }
    }
    void QuestWorld::Clear()
    {
        for (auto& [id, h] : m_HUD) RestoreWidget(h);
        m_HUD.clear();
        m_Actors.clear();
    }
}
