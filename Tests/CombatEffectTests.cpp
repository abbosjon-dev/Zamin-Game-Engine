#include "CombatFixture.h"
#include <Zamin/Gameplay/GameplayAI.h>
#include <Zamin/Gameplay/QuestWorld.h>
#include <Zamin/Physics/PhysicsWorld2D.h>
#include <array>
TEST(CombatEffectDefinitionsAreSharedValidatedAndResolvedAtUse)
{
    {
        Scene idle;
        auto e = idle.CreateEntity("Idle");
        std::string error;
        CHECK(!G::CombatWorld::For(idle).DefineEffect(Effect("Burn"), error) && error == "Combat definitions require Play");
        CHECK(!G::CombatWorld::For(idle).DefineArea(Area("Blast", 2, 5), error) && error == "Combat definitions require Play");
        CHECK(G::CombatWorld::For(idle).ApplyEffect(e, "Burn").Reason == "Combat requires Play");
        CHECK(G::CombatWorld::For(idle).ApplyArea("Blast", {}, { 0, 0, 0 }).Reason == "Combat requires Play");
    }
    CFixture f;
    auto& c = f.Combat();
    std::string e;
    CHECK(c.DefineEffect(Dot("Burn", 2, .5, 3), e));
    CHECK(c.DefineEffect(Dot("Burn", 2, .5, 3), e) && e.empty()); // identical: prefab instances may all define
    CHECK(!c.DefineEffect(Dot("Burn", 2, .5, 4), e) && e == "Definition differs");
    CHECK(!c.DefineEffect(Dot("Bad", 1, 2, 3), e) && !e.empty());
    CHECK(c.DefineArea(Area("Blast", 2, 5), e) && c.DefineArea(Area("Blast", 2, 5), e));
    CHECK(!c.DefineArea(Area("Blast", 3, 5), e) && e == "Definition differs");
    CHECK(!c.DefineArea(Area("Blast", 60, 5), e) && !e.empty());
    CHECK(c.EffectDefinition("Burn")->Period == .5 && c.AreaDefinition("Blast")->Radius == 2 && !c.EffectDefinition("Nope"));
    // Payload keys resolve at Fire, so definitions may come in any order; a missing one spends nothing.
    auto bomb = CFixture::Bolt(true, "Bomb");
    bomb.Effect = "Poison";
    bomb.Area = "Nova";
    f.Define(bomb);
    CHECK(f.Fire("Bomb").Reason == "Projectile effect not defined");
    Define(f, Dot("Poison", 3, 1, 2, G::StackRule::Independent, 3));
    CHECK(f.Fire("Bomb").Reason == "Projectile area not defined");
    auto nova = Area("Nova", 3, 12);
    nova.Effect = "Frost";
    Define(f, nova);
    CHECK(f.Fire("Bomb").Reason == "Area effect not defined");
    CHECK(c.ApplyArea("Nova", f.Hero, { 0, 0, 0 }).Reason == "Area effect not defined");
    auto frost = Effect("Frost", 2, G::StackRule::Extend, 2);
    frost.Speed = .5;
    Define(f, frost);
    CHECK(c.LiveCount() == 0 && f.Fire("Bomb").Success && c.LiveCount() == 1);
    // A payload's effect must match it: damage carries hostile effects, support beneficial ones.
    auto mend = CFixture::Bolt(true, "Mend");
    mend.Kind = G::PayloadKind::Heal;
    mend.Effect = "Poison";
    f.Define(mend);
    CHECK(f.Fire("Mend").Reason == "Projectile effect kind mismatch");
    auto pulse = Area("Pulse", 3, 5);
    pulse.Kind = G::PayloadKind::Heal;
    pulse.Effect = "Burn";
    Define(f, pulse);
    CHECK(c.ApplyArea("Pulse", f.Hero, { 0, 0, 0 }).Reason == "Area effect kind mismatch");
    const auto signature = c.Signature();
    CHECK(signature.find("\"Areas\"") != std::string::npos && signature.find("\"Effects\"") != std::string::npos && signature.find("\"Nova\"") != std::string::npos);
    CHECK(c.DefinitionCount() == 2 + 3 + 3);
    // Each registry holds 32 definitions.
    for (int i = 0; c.DefinitionCount() < 2 + 3 + 32; ++i) CHECK(c.DefineEffect(Effect("E" + std::to_string(i)), e));
    CHECK(!c.DefineEffect(Effect("Over"), e) && e == "Definition limit 32");
    for (int i = 0; i < 29; ++i) CHECK(c.DefineArea(Area("A" + std::to_string(i), 1, 1), e));
    CHECK(!c.DefineArea(Area("Over", 1, 1), e) && e == "Definition limit 32");
}
TEST(CombatEffectStackingRulesThroughTheWorldEmitEvents)
{
    CFixture f;
    auto& c = f.Combat();
    Define(f, Dot("Burn", 2, .5, 3, G::StackRule::Refresh, 3));
    auto frost = Effect("Frost", 3, G::StackRule::Extend, 2);
    frost.Speed = .5;
    Define(f, frost);
    Define(f, Dot("Poison", 3, 1, 2, G::StackRule::Independent, 3));
    auto shock = Effect("Shock", 1, G::StackRule::HighestWins);
    shock.Stun = true;
    Define(f, shock);
    auto r = c.ApplyEffect(f.Enemy, "Burn", f.Hero);
    CHECK(r.Outcome == Outcome::Added && r.Stacks == 1);
    r = c.ApplyEffect(f.Enemy, "Burn", f.Hero);
    CHECK(r.Outcome == Outcome::Stacked && r.Stacks == 2);
    auto events = f.Drain(f.Enemy);
    CHECK(events.size() == 2 && events[0].Kind == "EffectApplied" && events[0].Key == "Burn" && events[0].Amount == 1);
    CHECK(events[0].Source == 100 && events[0].Subject == 200 && events[1].Kind == "EffectRefreshed" && events[1].Amount == 2);
    CHECK(f.Drain(f.Hero).empty()); // effect events go to the target
    auto view = c.Effect(f.Enemy, "Burn");
    CHECK(view.Active && view.Stacks == 2 && view.Instances == 1 && view.Remaining == 2 && view.Duration == 2 && view.Magnitude == 6);
    CHECK(c.ApplyEffect(f.Enemy, "Frost", f.Hero).Remaining == 3);
    r = c.ApplyEffect(f.Enemy, "Frost", f.Hero);
    CHECK(r.Outcome == Outcome::Extended && r.Remaining == 6 && c.Effect(f.Enemy, "Frost").Duration == 6);
    for (int i = 0; i < 3; ++i) CHECK(c.ApplyEffect(f.Enemy, "Poison", f.Hero).Outcome == Outcome::Added);
    r = c.ApplyEffect(f.Enemy, "Poison", f.Hero);
    CHECK(r.Outcome == Outcome::Replaced && r.Stacks == 3);
    view = c.Effect(f.Enemy, "Poison");
    CHECK(view.Instances == 3 && view.Stacks == 3 && view.Magnitude == 6);
    CHECK(c.ApplyEffect(f.Enemy, "Shock", f.Hero, 5).Outcome == Outcome::Added);
    CHECK(c.ApplyEffect(f.Enemy, "Shock", f.Hero, 1).Reason == "Stronger effect active");
    CHECK(c.ApplyEffect(f.Enemy, "Shock", f.Hero, 7).Outcome == Outcome::Replaced);
    auto m = c.Modifiers(f.Enemy);
    CHECK(m.Stunned && m.Speed == .5 && m.Instances == 6);
    CHECK(c.EffectCount() == 6 && c.Stats().Effects == 6);
    std::array<G::EffectInstance, 16> list{};
    CHECK(c.Effects(f.Enemy, list) == 6 && list[0].Key == "Burn" && list[0].Stacks == 2 && list[5].Key == "Shock");
    f.Drain(f.Enemy);
    CHECK(c.RemoveEffects(f.Enemy, "Poison") == 3);
    auto removed = f.Drain(f.Enemy);
    CHECK(removed.size() == 1 && removed[0].Kind == "EffectRemoved" && removed[0].Key == "Poison" && removed[0].Amount == 3);
    CHECK(c.RemoveEffects(f.Enemy, "Poison") == 0 && f.Drain(f.Enemy).empty());
    CHECK(c.RemoveEffects(f.Enemy, "") == 3 && c.EffectCount() == 0 && c.Modifiers(f.Enemy) == G::EffectModifiers{});
    CHECK(Throws([&] { c.RemoveEffects(f.Enemy, "bad key"); }));
    // Natural expiry through fixed steps: 3 s at 60 Hz is exactly 180 steps.
    CHECK(c.ApplyEffect(f.Enemy, "Frost").Outcome == Outcome::Added);
    f.Drain(f.Enemy);
    f.Run(179);
    CHECK(c.Effect(f.Enemy, "Frost").Active && c.Modifiers(f.Enemy).Speed == .5);
    f.Run(1);
    CHECK(!c.Effect(f.Enemy, "Frost").Active && c.Modifiers(f.Enemy).Speed == 1 && c.EffectCount() == 0);
    auto expired = f.Drain(f.Enemy);
    CHECK(expired.size() == 1 && expired[0].Kind == "EffectExpired" && expired[0].Key == "Frost" && expired[0].Source == 0);
}
TEST(CombatEffectDamageOverTimeTicksIgnoreGraceAndCreditKills)
{
    CFixture f([](CFixture& x) { x.Enemy.AddTag("Enemy"); });
    f.Health(f.Enemy, 2, 10, 1); // 10 HP and a 1 s hit grace
    Define(f, Dot("Poison", 3, 1, 2, G::StackRule::Independent, 3));
    std::string e;
    G::QuestSpec quest;
    quest.Key = "Hunt";
    quest.Title = "Hunt";
    quest.Objectives = { { "Kill", "Defeat the enemy", "Enemy", G::QuestKind::Kill, 1 } };
    quest.XP = 10;
    auto& quests = G::QuestWorld::For(f.World);
    CHECK(quests.Configure(f.Hero, quest, e) && quests.Start(f.Hero, "Hunt", e));
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Outcome == Outcome::Added);
    f.Run(59);
    CHECK(f.HP(f.Enemy) == 10);
    f.Run(1); // the first tick at exactly 1 s
    CHECK(f.HP(f.Enemy) == 8);
    // Ticks start no hit grace: a direct hit lands right after one, and the next tick ignores the grace it began.
    CHECK(f.Rules().Damage(f.Enemy, f.Hero, 1, false, false, false).Applied == 1);
    f.Run(60);
    CHECK(f.HP(f.Enemy) == 5);
    size_t poison = 0;
    for (const auto& x : RuleEvents(f, f.Enemy))
        if (x.Kind == "Damage" && x.Item == "Poison") poison += x.Source == 100 && x.Amount == 2;
    CHECK(poison == 2);
    std::array<G::HitRecord, 8> records{};
    CHECK(f.Combat().Recent(f.Hero, true, false, 10, records) == 2);
    CHECK(records[0].Periodic && records[0].Key == "Poison" && records[0].Victim == 200 && records[0].Amount == 2 && records[0].Source == 100);
    // A second instance has its own timer; together they finish the enemy and the kill credits the quest.
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Stacks == 2);
    f.Run(60);
    CHECK(f.HP(f.Enemy) == 1 && f.Combat().Effect(f.Enemy, "Poison").Instances == 1); // the first one expired
    CHECK(quests.State(f.Hero, "Hunt")->Progress.Counts[0] == 0);
    f.Run(60);
    CHECK(f.Rules().Dead(f.Enemy) && quests.State(f.Hero, "Hunt")->Progress.Counts[0] == 1);
    CHECK(!f.Combat().Effect(f.Enemy, "Poison").Active && f.Combat().EffectCount() == 0);
    auto died = RuleEvents(f, f.Enemy);
    CHECK(!died.empty() && died.back().Kind == "Died" && died.back().Item == "Poison" && died.back().Source == 100);
    // A source that is gone gives no credit; its ticks keep its team snapshot and keep going.
    auto victim = f.Actor(301, { 0, 4, 0 }, 2), caster = f.Actor(302, { 0, -4, 0 }, 1);
    CHECK(f.Combat().ApplyEffect(victim, "Poison", caster).Outcome == Outcome::Added);
    f.World.DestroyEntity(caster);
    f.Run(60);
    CHECK(f.HP(victim) == 98);
    auto ownerless = RuleEvents(f, victim);
    CHECK(ownerless.size() == 1 && ownerless[0].Source == 0 && ownerless[0].Item == "Poison");
}
TEST(CombatEffectDeathClearsEffectsAndRespawnStartsClean)
{
    CFixture f;
    Define(f, Dot("Burn", 2, .5, 3, G::StackRule::Refresh, 3));
    auto frost = Effect("Frost", 3, G::StackRule::Extend, 2);
    frost.Speed = .5;
    Define(f, frost);
    Define(f, Dot("Poison", 3, .5, 2, G::StackRule::Independent, 3));
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Burn", f.Hero).Outcome == Outcome::Added);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Frost", f.Hero).Outcome == Outcome::Added);
    f.Drain(f.Enemy);
    CHECK(f.Rules().Damage(f.Enemy, f.Hero, 1000, false, false, true).Killed);
    CHECK(f.Combat().EffectCount() == 0 && !f.Combat().Effect(f.Enemy, "Burn").Active && f.Combat().Modifiers(f.Enemy).Speed == 1);
    auto events = f.Drain(f.Enemy);
    CHECK(events.size() == 1 && events[0].Kind == "EffectCleared" && events[0].Amount == 2);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Burn", f.Hero).Reason == "Effect target dead");
    f.Run(10); // past the 0.1 s respawn delay
    std::string e;
    CHECK(f.Rules().Revive(f.Enemy, 0, e) && f.Combat().EffectCount() == 0);
    // A damage-over-time kill drops the actor's remaining ticks of that step and clears its other effects.
    CHECK(f.Rules().Damage(f.Enemy, f.Hero, 97, false, false, true).Applied == 97);
    RuleEvents(f, f.Enemy);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Burn", f.Hero).Outcome == Outcome::Added);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Outcome == Outcome::Added);
    f.Run(30); // both tick at 0.5 s: Burn (applied first) kills, Poison's tick is dropped
    CHECK(f.Rules().Dead(f.Enemy) && f.Combat().EffectCount() == 0);
    auto rules = RuleEvents(f, f.Enemy);
    CHECK(rules.size() == 2 && rules[0].Kind == "Damage" && rules[0].Item == "Burn" && rules[1].Kind == "Died");
}
TEST(CombatEffectInactiveActorsPauseAndTimeScaleFreezeEffects)
{
    CFixture f;
    Define(f, Dot("Poison", 3, 1, 2, G::StackRule::Independent, 3));
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Outcome == Outcome::Added);
    f.Run(30);
    f.World.SetActive(f.Enemy, false);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Reason == "Effect target inactive");
    f.Run(300);
    CHECK(f.HP(f.Enemy) == 100);
    CHECK_NEAR(f.Combat().Effect(f.Enemy, "Poison").Remaining, 2.5, 1e-9); // frozen, not expired
    f.World.SetActive(f.Enemy, true);
    f.Run(30);
    CHECK(f.HP(f.Enemy) == 98);
    f.World.SetPaused(true);
    f.Run(120);
    f.World.SetPaused(false);
    f.World.SetTimeScale(0);
    f.Run(120);
    f.World.SetTimeScale(1);
    CHECK(f.HP(f.Enemy) == 98 && f.Combat().Effect(f.Enemy, "Poison").Active);
    f.Run(120);
    CHECK(f.HP(f.Enemy) == 94 && !f.Combat().Effect(f.Enemy, "Poison").Active); // exactly three ticks in total
    f.Run(60);
    CHECK(f.HP(f.Enemy) == 94);
}
TEST(CombatEffectAdmissionTeamsSourcesBudgetAndLimits)
{
    CFixture f;
    auto& c = f.Combat();
    Define(f, Dot("Poison", 3, 1, 2));
    auto regen = Dot("Regen", 3, .5, 4);
    regen.Tick = G::TickKind::Heal;
    regen.Hostile = false;
    Define(f, regen);
    Define(f, Effect("Mark", 600));
    auto ally = f.Actor(300, { 1, 0, 0 }, 1), neutral = f.Actor(301, { 2, 0, 0 }, 0);
    CHECK(c.ApplyEffect(ally, "Poison", f.Hero).Reason == "Effect target not allowed");
    CHECK(c.ApplyEffect(f.Enemy, "Regen", f.Hero).Reason == "Effect target not allowed");
    CHECK(c.ApplyEffect(ally, "Regen", f.Hero).Outcome == Outcome::Added);
    CHECK(c.ApplyEffect(f.Hero, "Poison", f.Hero).Outcome == Outcome::Added); // an explicit self target
    CHECK(c.ApplyEffect(f.Enemy, "Regen").Outcome == Outcome::Added);         // no source: environment
    CHECK(c.ApplyEffect(f.Enemy, "Regen", neutral).Reason == "Effect target not allowed"); // team 0 supports only itself
    CHECK(c.ApplyEffect(neutral, "Regen", neutral).Outcome == Outcome::Added);
    CHECK(c.ApplyEffect(f.Enemy, "Poison", neutral).Outcome == Outcome::Added);
    auto plain = f.World.CreateEntity("Plain");
    CHECK(c.ApplyEffect(plain, "Poison").Reason == "Effect target health not configured");
    CHECK(c.ApplyEffect(f.Enemy, "Nope").Reason == "Effect not defined");
    CHECK(Throws([&] { c.ApplyEffect(f.Enemy, "Poison", {}, NAN); }));
    CHECK(Throws([&] { c.ApplyEffect(f.Enemy, "Poison", {}, 100001); }));
    Scene other;
    auto stranger = other.CreateEntity("Stranger");
    CHECK(Throws([&] { c.ApplyEffect(stranger, "Poison"); }));
    CHECK(Throws([&] { c.ApplyEffect(f.Enemy, "Poison", stranger); }));
    // 64 direct applications per scene frame, counted in call order.
    f.Run(1);
    for (int i = 0; i < 64; ++i) CHECK(c.ApplyEffect(f.Enemy, "Mark").Reason.empty());
    CHECK(c.ApplyEffect(f.Enemy, "Mark").Reason == "Effect budget 64");
    f.Run(1);
    CHECK(c.ApplyEffect(f.Enemy, "Mark").Outcome == Outcome::Refreshed);
    // At most 1024 instances per scene; refreshes keep working at the cap.
    for (auto e : { f.Hero, f.Enemy, ally, neutral }) c.RemoveEffects(e, "");
    CHECK(c.EffectCount() == 0);
    for (int k = 0; k < 16; ++k) Define(f, Effect("M" + std::to_string(k), 600));
    std::vector<Entity> crowd;
    for (int i = 0; i < 64; ++i) crowd.push_back(f.Actor(400 + uint64_t(i), { float(i), 20, 0 }, 2));
    for (int k = 0; k < 16; ++k)
    {
        f.Run(1);
        for (auto e : crowd) CHECK(c.ApplyEffect(e, "M" + std::to_string(k)).Outcome == Outcome::Added);
    }
    f.Run(1);
    CHECK(c.EffectCount() == 1024);
    CHECK(c.ApplyEffect(f.Enemy, "M0").Reason == "Scene effect limit 1024");
    CHECK(c.ApplyEffect(crowd[0], "Mark").Reason == "Scene effect limit 1024");
    CHECK(c.ApplyEffect(crowd[0], "M0").Outcome == Outcome::Refreshed);
    CHECK(c.ApplyEffect(crowd[0], "M1").Outcome == Outcome::Refreshed);
}
TEST(CombatEffectStatBonusComposesWithLoadoutAndClampsEnergy)
{
    CFixture f;
    auto& c = f.Combat();
    auto& kit = G::LoadoutWorld::For(f.World);
    std::string e;
    CHECK(kit.Configure(f.Hero, { 10, 0, 5, 100, 0 }, e));
    CHECK(kit.DefineItem(f.Hero, { "Sword", G::EquipSlot::Weapon, { 5, 0, 0, 0, 0 } }, e) && f.Rules().ChangeItem(f.Hero, "Sword", 1, false, e) && kit.Equip(f.Hero, "Sword", e));
    Define(f, Buff("Haste", 5, { 10, 0, 0, 50, 10 }, 1.5));
    auto shred = Effect("Shred", 5, G::StackRule::HighestWins);
    shred.Add = { 0, -50, 0, 0, 0 };
    Define(f, shred);
    CHECK(c.ApplyEffect(f.Hero, "Haste").Outcome == Outcome::Added);
    auto s = kit.State(f.Hero);
    CHECK(s->Stats[0] == 25 && s->Stats[3] == 150 && s->Stats[4] == 10 && s->State.Energy == 100); // equipment + effect
    CHECK(kit.Refill(f.Hero, 100) == 50 && kit.State(f.Hero)->State.Energy == 150);
    CHECK(G::LoadoutAttack(f.World, f.Hero) == 25);
    CHECK(f.Rules().Damage(f.Enemy, f.Hero, 10, false, false, false).Applied == 35);
    CHECK(kit.Configure(f.Enemy, { 0, 100, 0, 100, 0 }, e));
    CHECK(f.Rules().Damage(f.Enemy, f.Hero, 15, false, false, false).Applied == 20); // (15 + 25) x 100 / 200
    CHECK(c.ApplyEffect(f.Enemy, "Shred", f.Hero).Outcome == Outcome::Added);
    CHECK(G::LoadoutArmor(f.World, f.Enemy) == 50);
    CHECK_NEAR(f.Rules().Damage(f.Enemy, f.Hero, 5, false, false, false).Applied, 20, 1e-9); // (5 + 25) x 100 / 150
    // Fire snapshots the buffed Attack (also kept for an area payload).
    f.Define(CFixture::Bolt());
    auto shot = f.Fire("Bolt");
    CHECK(c.Projectile(shot.Handle)->Damage == 45 && c.Projectile(shot.Handle)->Attack == 25);
    CHECK(c.Cancel(shot.Handle, false));
    // The regeneration bonus refills while the buff lasts; casting uses the buffed Power.
    CHECK(kit.DefineAbility(f.Hero, Ability("Zap", 1, 60, 0), e));
    auto cast = kit.Cast(f.Hero, f.Enemy, "Zap", Query2D());
    CHECK(cast.Success && kit.State(f.Hero)->State.Energy == 90);
    CHECK_NEAR(cast.Applied, (1 + 5 + 25) * 100.0 / 150, 1e-9);
    f.Run(60);
    CHECK_NEAR(kit.State(f.Hero)->State.Energy, 100, 1e-4);
    // Losing a MaxEnergy bonus clamps energy at once; no energy is granted back.
    CHECK(kit.Refill(f.Hero, 100) > 49 && kit.State(f.Hero)->State.Energy == 150);
    CHECK(c.RemoveEffects(f.Hero, "Haste") == 1);
    s = kit.State(f.Hero);
    CHECK(s->State.Energy == 100 && s->Stats[0] == 15 && s->Stats[3] == 100 && s->Stats[4] == 0);
    CHECK(c.ApplyEffect(f.Hero, "Haste").Outcome == Outcome::Added && kit.Refill(f.Hero, 100) == 50);
    f.Run(299);
    CHECK(kit.State(f.Hero)->State.Energy == 150);
    f.Run(1); // the 5 s buff expires
    CHECK(kit.State(f.Hero)->State.Energy == 100 && !c.Effect(f.Hero, "Haste").Active);
    // Actors without a loadout get Attack and Armor from their effects (base 0).
    auto brute = f.Actor(300, { 1, 0, 0 }, 2), dummy = f.Actor(301, { 2, 0, 0 }, 1);
    Define(f, Buff("Rage", 5, { 10, 0, 0, 0, 0 }));
    Define(f, Buff("Plate", 5, { 0, 100, 0, 0, 0 }));
    CHECK(f.Rules().Damage(dummy, brute, 10, false, false, false).Applied == 10);
    CHECK(c.ApplyEffect(brute, "Rage").Outcome == Outcome::Added);
    CHECK(f.Rules().Damage(dummy, brute, 10, false, false, false).Applied == 20);
    CHECK(c.ApplyEffect(dummy, "Plate").Outcome == Outcome::Added);
    CHECK(f.Rules().Damage(dummy, brute, 10, false, false, false).Applied == 10); // 20 x 100 / 200
    CHECK(G::LoadoutAttack(f.World, brute) == 10 && G::LoadoutArmor(f.World, dummy) == 100 && G::LoadoutArmor(f.World, brute) == 0);
}
TEST(CombatEffectStunGatesActionsWithoutSpendingAnything)
{
    CFixture f;
    std::string e;
    auto shock = Effect("Shock", 1, G::StackRule::HighestWins);
    shock.Stun = true;
    Define(f, shock);
    Define(f, Area("Nova", 3, 5));
    auto& kit = G::LoadoutWorld::For(f.World);
    CHECK(kit.Configure(f.Hero, { 0, 0, 0, 100, 0 }, e) && kit.DefineAbility(f.Hero, Ability("Zap", 5, 10, 1), e));
    CHECK(kit.DefineItem(f.Hero, { "Sword", G::EquipSlot::Weapon, { 1, 0, 0, 0, 0 } }, e) && f.Rules().ChangeItem(f.Hero, "Sword", 1, false, e));
    f.Define(CFixture::Bolt());
    auto lever = f.World.CreateEntityWithUUID(UUID(500), "Lever");
    lever.GetTransform().Translation = { 1, 0, 0 };
    G::InteractionSettings use;
    use.Cooldown = 0;
    use.MaxUses = 0;
    CHECK(f.Rules().ConfigureInteraction(lever, use, false, e));
    CHECK(f.Combat().ApplyEffect(f.Hero, "Shock").Outcome == Outcome::Added && f.Combat().Modifiers(f.Hero).Stunned);
    const auto q = Query2D();
    CHECK(f.Rules().Attack(f.Hero, q, 10, 5, false).Reason == "Stunned");
    CHECK(f.Rules().Strike(f.Hero, f.Enemy, q, 10, 5, false).Reason == "Stunned");
    CHECK(f.Rules().Interact(f.Hero, lever, q).Reason == "Stunned");
    CHECK(kit.Cast(f.Hero, f.Enemy, "Zap", q).Reason == "Stunned");
    CHECK(!kit.Equip(f.Hero, "Sword", e) && e == "Stunned");
    CHECK(f.Fire("Bolt").Reason == "Stunned");
    CHECK(f.Combat().ApplyArea("Nova", f.Hero, { 0, 0, 0 }).Reason == "Stunned");
    CHECK(kit.Refill(f.Hero, 5) == 0); // refills are not actions (energy is full, nothing is refused)
    Gameplay::Runtime r;
    CHECK(CCall(r, f.Hero, "Gameplay.AbilityState", { { "Ability", "Zap" } }).at("Ready") == false);
    CHECK(f.HP(f.Enemy) == 100 && kit.State(f.Hero)->State.Energy == 100 && f.Combat().LiveCount() == 0);
    f.Run(60); // the 1 s stun ends
    CHECK(!f.Combat().Modifiers(f.Hero).Stunned);
    CHECK(CCall(r, f.Hero, "Gameplay.AbilityState", { { "Ability", "Zap" } }).at("Ready") == true);
    // Nothing was spent: no attack cooldown, energy or ability cooldown was taken while stunned.
    CHECK(f.Rules().Attack(f.Hero, q, 10, 5, false).Damage.Applied == 10);
    CHECK(kit.Cast(f.Hero, f.Enemy, "Zap", q).Success && kit.Equip(f.Hero, "Sword", e));
    CHECK(f.Rules().Interact(f.Hero, lever, q).Success && f.Fire("Bolt").Success);
    CHECK(f.Combat().ApplyArea("Nova", f.Hero, { 0, 0, 0 }).Success);
}
TEST(CombatEffectSlowHasteAndStunScaleMotors)
{
    CFixture f([](CFixture& x) {
        auto& body = x.Hero.AddComponent<Rigidbody2DComponent>();
        body.Type = Rigidbody2DComponent::BodyType::Dynamic;
        body.GravityScale = 0;
        body.FixedRotation = true;
        x.Hero.AddComponent<BoxCollider2DComponent>().Size = { .2f, .2f };
    });
    auto frost = Effect("Frost", 10);
    frost.Speed = .5;
    Define(f, frost);
    Define(f, Buff("Haste", 10, {}, 1.5));
    auto shock = Effect("Shock", 10, G::StackRule::HighestWins);
    shock.Stun = true;
    Define(f, shock);
    Gameplay::Runtime r;
    auto* body = (b2Body*)f.Hero.GetComponent<Rigidbody2DComponent>().RuntimeBody;
    auto step = [&] {
        r.BeginFrame(f.World);
        CCall(r, f.Hero, "Gameplay.Motor2D", { { "Move", Visual::Value::array({ 1., 0. }) }, { "TopDown", true }, { "Acceleration", 10000. }, { "Delta", 1. / 60 } });
        return double(PhysicsWorld2D::GetLinearVelocity(body).x);
    };
    CHECK_NEAR(step(), 5, 1e-4);
    CHECK(f.Combat().ApplyEffect(f.Hero, "Frost").Outcome == Outcome::Added);
    CHECK_NEAR(step(), 2.5, 1e-4);
    CHECK(f.Combat().ApplyEffect(f.Hero, "Haste").Outcome == Outcome::Added);
    CHECK_NEAR(step(), 3.75, 1e-4); // strongest slow x strongest haste
    CHECK(f.Combat().ApplyEffect(f.Hero, "Shock").Outcome == Outcome::Added);
    CHECK_NEAR(step(), 0, 1e-6); // stunned: stopped like a dead actor
    CHECK(f.Combat().RemoveEffects(f.Hero, "") == 3);
    CHECK_NEAR(step(), 5, 1e-4);
    r.Stop(f.World);
}
TEST(CombatEffectSlowAndStunDriveAINavigationAndFreezeTheBrain)
{
    const auto root = std::filesystem::temp_directory_path() / ("zamin-combat-ai-" + std::to_string(uint64_t(UUID())));
    std::filesystem::create_directories(root);
    SaveData::SetFilePath(root / "save.json");
    {
        Scene world;
        auto npc = world.CreateEntityWithUUID(UUID(100), "NPC"), hero = world.CreateEntityWithUUID(UUID(200), "Hero");
        hero.AddTag("Player");
        hero.GetTransform().Translation = { 9, 9, 0 }; // never seen: the brain patrols
        auto grid = world.CreateEntityWithUUID(UUID(300), "Grid");
        auto& g = grid.AddComponent<NavGrid2DComponent>();
        g.AutoBounds = false;
        g.Size = { 20, 20 };
        g.CellSize = .5f;
        g.UseTilemaps = g.UseColliders = false;
        g.AgentRadius = 0;
        auto& body = npc.AddComponent<Rigidbody2DComponent>();
        body.Type = Rigidbody2DComponent::BodyType::Dynamic;
        body.GravityScale = 0;
        body.FixedRotation = true;
        npc.AddComponent<BoxCollider2DComponent>().Size = { .3f, .3f };
        npc.AddComponent<NavAgentComponent>().Grid.ID = 300;
        world.OnRuntimeStart();
        std::string e;
        auto& rules = G::RuleWorld::For(world);
        CHECK(rules.Configure(npc, { 100, 0, 0, 0, 2 }, false, e) && rules.Configure(hero, { 100, 0, 0, 0, 1 }, false, e));
        G::AISpec s;
        s.Brain.TwoD = true;
        s.Brain.View = 3;
        s.Brain.Lose = 4;
        s.Brain.Leash = 8;
        s.Brain.Speed = 2;
        s.Brain.Wait = .1f;
        s.Eye = { 0, 0, 0 };
        s.Points = { { 0, 0, 0 }, { 0, 3, 0 } };
        auto& ai = G::AIWorld::For(world);
        CHECK(ai.Configure(npc, npc, s, false, e));
        auto frames = [&](int n) {
            for (int i = 0; i < n; ++i) world.UpdateRuntime(1.f / 30);
        };
        frames(5);
        auto& nav = npc.GetComponent<NavAgentComponent>();
        CHECK(nav.Speed == 2);
        auto& combat = G::CombatWorld::For(world);
        auto frost = Effect("Frost", 30);
        frost.Speed = .5;
        auto shock = Effect("Shock", 30, G::StackRule::HighestWins);
        shock.Stun = true;
        CHECK(combat.DefineEffect(frost, e) && combat.DefineEffect(shock, e));
        CHECK(combat.ApplyEffect(npc, "Frost").Outcome == Outcome::Added);
        frames(1);
        CHECK(nav.Speed == 1 && ai.State(npc)->Status != "Stunned");
        CHECK(combat.ApplyEffect(npc, "Shock").Outcome == Outcome::Added);
        frames(1);
        const auto frozen = *ai.State(npc);
        const auto position = world.GetWorldPosition(npc);
        frames(30);
        const auto after = *ai.State(npc);
        CHECK(after.Status == "Stunned" && after.Brain.Mode == frozen.Brain.Mode && after.Brain.Waypoint == frozen.Brain.Waypoint);
        CHECK(after.Brain.Arrivals == frozen.Brain.Arrivals && after.PathRequests == frozen.PathRequests && after.Transitions == frozen.Transitions);
        CHECK(glm::length(world.GetWorldPosition(npc) - position) < .05f); // stopped while stunned
        CHECK(combat.RemoveEffects(npc, "Shock") == 1);
        frames(30);
        CHECK(ai.State(npc)->Status != "Stunned" && nav.Speed == 1 && glm::length(world.GetWorldPosition(npc) - position) > .1f);
        CHECK(combat.RemoveEffects(npc, "Frost") == 1);
        frames(1);
        CHECK(nav.Speed == 2);
        world.OnRuntimeStop();
    }
    SaveData::SetFilePath({});
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
TEST(CombatAreaFalloffOrderingTargetCapTeamsAndBudget)
{
    CFixture f;
    auto& c = f.Combat();
    std::string e;
    CHECK(G::LoadoutWorld::For(f.World).Configure(f.Hero, { 10, 0, 0, 100, 0 }, e));
    auto a1 = f.Actor(301, { 1, 0, 0 }, 2), a2 = f.Actor(302, { 2, 0, 0 }, 2), a3 = f.Actor(303, { 3, 0, 0 }, 2), a4 = f.Actor(304, { 4, 0, 0 }, 2);
    auto ally = f.Actor(305, { 1.5f, 0, 0 }, 1);
    auto frost = Effect("Frost", 3, G::StackRule::Extend, 2);
    frost.Speed = .5;
    Define(f, frost);
    auto blast = Area("Blast", 4.5, 20);
    blast.Shape = G::Falloff::Linear;
    blast.EdgeScale = .5;
    blast.MaxTargets = 3;
    blast.Effect = "Frost";
    Define(f, blast);
    auto r = c.ApplyArea("Blast", f.Hero, { 0, 0, 0 });
    CHECK(r.Success && r.Affected == 3 && r.Reason.empty());
    // (20 + Attack 10) x (1 - 0.5 d / 4.5): the nearest three enemies, sorted by (distance, UUID).
    CHECK_NEAR(100 - f.HP(a1), 30 * (1 - .5 / 4.5), 1e-9);
    CHECK_NEAR(100 - f.HP(a2), 30 * (1 - 1 / 4.5), 1e-9);
    CHECK_NEAR(100 - f.HP(a3), 20, 1e-9);
    CHECK_NEAR(r.Applied, 30 * (1 - .5 / 4.5) + 30 * (1 - 1 / 4.5) + 20, 1e-9);
    CHECK(f.HP(a4) == 100 && f.HP(ally) == 100 && f.HP(f.Hero) == 100 && f.HP(f.Enemy) == 100);
    CHECK(c.Effect(a1, "Frost").Active && c.Effect(a3, "Frost").Active && !c.Effect(a4, "Frost").Active && !c.Effect(ally, "Frost").Active);
    auto owner = f.Drain(f.Hero);
    CHECK(owner.size() == 1 && owner[0].Kind == "Detonated" && owner[0].Key == "Blast" && owner[0].Amount == 3 && owner[0].Source == 100);
    auto victim = f.Drain(a1);
    CHECK(victim.size() == 2 && victim[0].Kind == "Hit" && victim[0].Key == "Blast" && victim[0].Source == 100 && victim[1].Kind == "EffectApplied");
    auto rule = f.Rules().Poll(a1);
    CHECK(rule && rule->Kind == "Damage" && rule->Item == "Blast" && rule->Source == 100);
    std::array<G::HitRecord, 8> records{};
    CHECK(c.Recent(f.Hero, true, false, 1, records) == 3 && !records[0].Periodic && records[0].Victim == 303);
    // Equal distances are ordered by UUID.
    auto b1 = f.Actor(402, { 10, 1, 0 }, 2), b2 = f.Actor(401, { 10, -1, 0 }, 2);
    auto pick = Area("Pick", 1.2, 1);
    pick.MaxTargets = 1;
    Define(f, pick);
    CHECK(c.ApplyArea("Pick", f.Hero, { 10, 0, 0 }).Affected == 1 && f.HP(b2) == 89 && f.HP(b1) == 100); // 1 + Attack 10
    // Without a source the area ignores teams and has no owner to spare.
    Define(f, Area("Gas", 1, 5));
    CHECK(c.ApplyArea("Gas", {}, { 1.25f, 0, 0 }).Affected == 3 && f.HP(ally) == 95);
    // 16 direct areas per scene frame.
    f.Run(1);
    for (int i = 0; i < 16; ++i) CHECK(c.ApplyArea("Pick", f.Hero, { 50, 0, 0 }).Success);
    CHECK(c.ApplyArea("Pick", f.Hero, { 50, 0, 0 }).Reason == "Area budget 16");
    f.Run(1);
    CHECK(c.ApplyArea("Nope", f.Hero, { 0, 0, 0 }).Reason == "Area not defined");
    f.World.SetActive(a4, false);
    CHECK(c.ApplyArea("Pick", a4, { 0, 0, 0 }).Reason == "Area source inactive");
    CHECK(f.Rules().Damage(f.Enemy, f.Hero, 1000, false, false, true).Killed);
    CHECK(c.ApplyArea("Pick", f.Enemy, { 0, 0, 0 }).Reason == "Area source dead");
    CHECK(Throws([&] { c.ApplyArea("Pick", f.Hero, { NAN, 0, 0 }); }));
    CHECK(Throws([&] { c.ApplyArea("Pick", f.Hero, { 0, 0, 0 }, 2e6); }));
    CHECK(c.ApplyArea("Pick", f.Hero, { 10, 0, 0 }, 7).Applied == 7 + 10); // explicit magnitude plus Attack
}
TEST(CombatAreaSupportHealsAlliesShieldsAndAffectsOwnerOnRequest)
{
    CFixture f;
    std::string e;
    auto ally = f.Actor(300, { 1, 0, 0 }, 1);
    CHECK(f.Rules().Configure(ally, { 100, 20, 0, .1, 1 }, true, e));
    f.Rules().Damage(ally, {}, 60, false, false, true); // shield 20 absorbed, health 60
    f.Rules().Damage(f.Hero, {}, 30, false, false, true);
    f.Rules().Damage(f.Enemy, {}, 20, false, false, true);
    auto pulse = Area("Pulse", 6, 15);
    pulse.Kind = G::PayloadKind::Heal;
    Define(f, pulse);
    auto r = f.Combat().ApplyArea("Pulse", f.Hero, { 0, 0, 0 });
    CHECK(r.Affected == 1 && r.Applied == 15 && f.HP(ally) == 75 && f.HP(f.Hero) == 70 && f.HP(f.Enemy) == 80);
    auto healed = RuleEvents(f, ally);
    CHECK(!healed.empty() && healed.back().Kind == "Healed" && healed.back().Source == 100 && healed.back().Item == "Pulse");
    auto self = pulse;
    self.Key = "Prayer";
    self.AffectOwner = true;
    Define(f, self);
    CHECK(f.Combat().ApplyArea("Prayer", f.Hero, { 0, 0, 0 }).Affected == 2 && f.HP(f.Hero) == 85 && f.HP(ally) == 90);
    auto ward = Area("Ward", 6, 5);
    ward.Kind = G::PayloadKind::Shield;
    Define(f, ward);
    CHECK(f.Combat().ApplyArea("Ward", f.Hero, { 0, 0, 0 }).Applied == 5 && f.Rules().Health(ally)->Shield == 5);
    // A beneficial area effect follows the heal; enemies still get nothing.
    auto regen = Dot("Regen", 1, .5, 4);
    regen.Tick = G::TickKind::Heal;
    regen.Hostile = false;
    Define(f, regen);
    auto spring = pulse;
    spring.Key = "Spring";
    spring.Amount = 0;
    spring.Effect = "Regen";
    Define(f, spring);
    CHECK(f.Combat().ApplyArea("Spring", f.Hero, { 0, 0, 0 }).Affected == 1 && f.Combat().Effect(ally, "Regen").Active && !f.Combat().Effect(f.Enemy, "Regen").Active);
    f.Run(60);
    CHECK(f.HP(ally) == 98 && f.HP(f.Enemy) == 80);
}
TEST(CombatAreaLineOfSightUsesBox2DAndJoltWalls)
{
    {
        CFixture f([](CFixture& x) {
            StaticWall2D(x.World, { 2, 0, 0 }, { .1f, 1 });
            auto guard = x.World.CreateEntityWithUUID(UUID(303), "Guard"); // a health actor with a collider
            guard.GetTransform().Translation = { 0, 1.5f, 0 };
            guard.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
            guard.AddComponent<BoxCollider2DComponent>().Size = { .3f, .3f };
        });
        auto guard = f.World.GetEntityByUUID(UUID(303));
        f.Health(guard, 2);
        auto hidden = f.Actor(301, { 3, 0, 0 }, 2), open = f.Actor(302, { 0, 3, 0 }, 2);
        auto nova = Area("Nova", 4, 10);
        nova.LineOfSight = true;
        Define(f, nova);
        auto r = f.Combat().ApplyArea("Nova", f.Hero, { 0, 0, 0 });
        // The wall hides one target; actors (and their colliders) never occlude each other.
        CHECK(r.Affected == 2 && f.HP(hidden) == 100 && f.HP(open) == 90 && f.HP(guard) == 90);
        CHECK(f.Combat().Stats().Rays == 3);
        auto open2 = nova;
        open2.Key = "Shout";
        open2.LineOfSight = false;
        Define(f, open2);
        CHECK(f.Combat().ApplyArea("Shout", f.Hero, { 0, 0, 0 }).Affected == 3 && f.HP(hidden) == 90);
    }
    {
        CFixture f([](CFixture& x) { StaticBox3D(x.World, { 2, 1, 0 }, { .1f, 2, 2 }); });
        auto hidden = f.Actor(301, { 3, 0, 0 }, 2), open = f.Actor(302, { 0, 0, 3 }, 2);
        auto nova = Area("Nova", 4, 10, false);
        nova.LineOfSight = true;
        nova.TargetOffset = { 0, 1, 0 };
        Define(f, nova);
        CHECK(f.Combat().ApplyArea("Nova", f.Hero, { 0, 1, 0 }).Affected == 1 && f.HP(open) == 90 && f.HP(hidden) == 100 && f.HP(f.Enemy) == 100);
    }
}
TEST(CombatProjectileImpactDetonatesAreaWithLaunchSnapshotAndEffects)
{
    {
        CFixture f;
        std::string e;
        auto& kit = G::LoadoutWorld::For(f.World);
        CHECK(kit.Configure(f.Hero, { 10, 0, 0, 100, 0 }, e));
        auto neighbour = f.Actor(301, { 6, 0, 0 }, 2), ally = f.Actor(302, { 5, 1.5f, 0 }, 1);
        Define(f, Dot("Poison", 3, 1, 2, G::StackRule::Independent, 3));
        Define(f, Dot("Burn", 2, .5, 3, G::StackRule::Refresh, 3));
        auto blast = Area("Blast", 2, 10);
        blast.Effect = "Burn";
        Define(f, blast);
        auto bomb = CFixture::Bolt(true, "Bomb");
        bomb.Effect = "Poison";
        bomb.Area = "Blast";
        f.Define(bomb);
        auto shot = f.Fire("Bomb");
        CHECK(shot.Success && f.Combat().Projectile(shot.Handle)->Attack == 10 && f.Combat().Projectile(shot.Handle)->Damage == 30);
        // A weapon equipped after launch is not in the snapshot.
        CHECK(kit.DefineItem(f.Hero, { "Sword", G::EquipSlot::Weapon, { 50, 0, 0, 0, 0 } }, e) && f.Rules().ChangeItem(f.Hero, "Sword", 1, false, e) && kit.Equip(f.Hero, "Sword", e));
        f.Run(27);
        // Direct 20 + 10 on the victim, then the area (10 + 10) on it and its neighbour; the ally is spared.
        CHECK(f.HP(f.Enemy) == 50 && f.HP(neighbour) == 80 && f.HP(ally) == 100);
        CHECK(f.Combat().Effect(f.Enemy, "Poison").Active && f.Combat().Effect(f.Enemy, "Burn").Active);
        CHECK(f.Combat().Effect(neighbour, "Burn").Active && !f.Combat().Effect(neighbour, "Poison").Active);
        auto owner = f.Drain(f.Hero);
        CHECK(owner.size() == 3 && owner[0].Kind == "Fired" && owner[1].Kind == "Hit" && owner[2].Kind == "Detonated");
        CHECK(owner[2].Key == "Blast" && owner[2].Amount == 2);
        CHECK_NEAR(owner[2].Position.x, 4.45, .17);
        auto victim = f.Drain(f.Enemy);
        CHECK(victim.size() == 4 && victim[0].Key == "Bomb" && victim[1].Key == "Poison" && victim[2].Key == "Blast" && victim[3].Key == "Burn");
        CHECK(f.Combat().Stats().Detonations == 1 && f.Combat().LiveCount() == 0);
    }
    {
        // An ownerless projectile detonates with its team snapshot and no Attack or kill credit.
        CFixture f;
        std::string e;
        CHECK(G::LoadoutWorld::For(f.World).Configure(f.Hero, { 10, 0, 0, 100, 0 }, e));
        auto ally = f.Actor(302, { 5, 1.5f, 0 }, 1);
        Define(f, Area("Blast", 2, 10));
        auto bomb = CFixture::Bolt(true, "Bomb");
        bomb.Area = "Blast";
        f.Define(bomb);
        CHECK(f.Fire("Bomb").Success);
        f.World.DestroyEntity(f.Hero);
        f.Run(27);
        CHECK(f.HP(f.Enemy) == 100 - 30 - 20 && f.HP(ally) == 100); // snapshots: damage 30, area Attack 10
        for (const auto& x : RuleEvents(f, f.Enemy)) CHECK(x.Source == 0);
    }
}
TEST(CombatProjectileDetonatesOnExpiryCancelAndWallImpact)
{
    CFixture f([](CFixture& x) { StaticWall2D(x.World, { 3, -2, 0 }, { .1f, 1 }); });
    Define(f, Area("Blast", 1, 10));
    auto mine = CFixture::Bolt(true, "Mine");
    mine.Speed = 2;
    mine.Lifetime = .5;
    mine.Area = "Blast";
    mine.DetonateOnExpire = true;
    auto dud = mine;
    dud.Key = "Dud";
    dud.DetonateOnExpire = false;
    f.Define(mine);
    f.Define(dud);
    f.Define(CFixture::Bolt());
    auto target = f.Actor(301, { 1, .8f, 0 }, 2); // beside the path: only an area reaches it
    CHECK(f.Fire("Mine").Success);
    f.Run(30); // expires at x = 1 after 0.5 s and detonates at once
    CHECK(f.HP(target) == 90 && f.Combat().LiveCount() == 0);
    auto events = f.Drain(f.Hero);
    CHECK(events.size() == 3 && events[1].Kind == "Expired" && events[2].Kind == "Detonated" && events[2].Amount == 1);
    CHECK(f.Fire("Dud").Success);
    f.Run(31);
    CHECK(f.HP(target) == 90);
    events = f.Drain(f.Hero);
    CHECK(events.size() == 2 && events[1].Kind == "Expired");
    // Cancel(detonate) explodes where the projectile is on the next step; without an area it just releases.
    auto shot = f.Fire("Dud");
    f.Run(25);
    CHECK(f.Combat().Cancel(shot.Handle, true) && f.Combat().Projectile(shot.Handle)->Phase == G::ProjectilePhase::Detonating);
    f.Run(1);
    CHECK(!f.Combat().Projectile(shot.Handle) && f.HP(target) == 80);
    auto bolt = f.Fire("Bolt", { 0, -1, 0 });
    CHECK(f.Combat().Cancel(bolt.Handle, true) && !f.Combat().Projectile(bolt.Handle));
    // A wall impact detonates at the wall.
    auto burst = CFixture::Bolt(true, "Burst");
    burst.Area = "Blast";
    f.Define(burst);
    auto near = f.Actor(302, { 2.5f, -1.2f, 0 }, 2);
    f.Drain(f.Hero);
    CHECK(f.Fire("Burst", { 1, 0, 0 }, {}, {}, { 0, -2, 0 }).Success);
    f.Run(30);
    events = f.Drain(f.Hero);
    CHECK(events.size() == 3 && events[1].Kind == "Impact" && events[2].Kind == "Detonated" && events[2].Amount == 1);
    CHECK_NEAR(events[2].Position.x, 2.9, .05);
    CHECK(f.HP(near) == 90);
}
TEST(CombatDetonationBudgetDefersTheRestToLaterSteps)
{
    CFixture f;
    Define(f, Area("Blast", .5, 1));
    auto mine = CFixture::Bolt(true, "Mine");
    mine.Area = "Blast";
    f.Define(mine);
    std::vector<uint32_t> handles;
    for (int i = 0; i < 20; ++i) handles.push_back(f.Fire("Mine", { 1, 0, 0 }, {}, {}, { 0, 50, 0 }).Handle);
    for (auto h : handles) CHECK(f.Combat().Cancel(h, true));
    CHECK(f.Combat().LiveCount() == 20);
    f.Run(1);
    CHECK(f.Combat().Stats().Detonations == 16 && f.Combat().LiveCount() == 4);
    CHECK(!f.Combat().Projectile(handles[15]) && f.Combat().Projectile(handles[16])); // lower slots first
    f.Run(1);
    CHECK(f.Combat().Stats().Detonations == 20 && f.Combat().LiveCount() == 0);
}
TEST(CombatEffectBlueprintNodesDispatchAndValidate)
{
    CFixture f;
    Gameplay::Runtime r;
    for (auto kind : { "Gameplay.AreaDefine", "Gameplay.AreaApply", "Gameplay.EffectDefine", "Gameplay.EffectApply", "Gameplay.EffectRemove", "Gameplay.EffectState", "Gameplay.CombatModifiers" })
        CHECK(Visual::Catalog().contains(kind));
    CHECK(Visual::Catalog().at("Gameplay.EffectState").Pure && Visual::Catalog().at("Gameplay.CombatModifiers").Pure && !Visual::Catalog().at("Gameplay.EffectApply").Pure);
    CHECK(CCall(r, f.Hero, "Gameplay.EffectDefine").at("Success") == true); // "Burn": 3 s, no ticks
    CHECK(CCall(r, f.Hero, "Gameplay.EffectDefine", { { "Effect", "Poison" }, { "Period", 1 }, { "Tick", 1 }, { "TickAmount", 2 }, { "Stacking", 2 }, { "MaxStacks", 3 } }).at("Success") == true);
    CHECK(CCall(r, f.Hero, "Gameplay.EffectDefine", { { "Effect", "Frost" }, { "Speed", .5 }, { "Stacking", 1 }, { "MaxStacks", 2 }, { "Modifiers", Visual::Value::array({ 0, -5, 0, 0, 0 }) } }).at("Success") == true);
    auto bad = CCall(r, f.Hero, "Gameplay.EffectDefine", { { "Effect", "Bad" }, { "Period", 1 } });
    CHECK(bad.at("Success") == false && !bad.at("Reason").get<std::string>().empty());
    const uint64_t hero = f.Hero.GetUUID(), enemy = f.Enemy.GetUUID();
    auto applied = CCall(r, f.Enemy, "Gameplay.EffectApply", { { "Effect", "Poison" }, { "Source", hero } });
    CHECK(applied.at("Success") == true && applied.at("Outcome") == "Added" && applied.at("Stacks") == 1 && applied.at("Remaining") == 3);
    CHECK(CCall(r, f.Enemy, "Gameplay.EffectApply", { { "Effect", "Poison" }, { "Source", uint64_t(999) } }).at("Reason") == "Effect source unavailable");
    auto refused = CCall(r, f.Enemy, "Gameplay.EffectApply", { { "Effect", "Nope" } });
    CHECK(refused.at("Success") == false && refused.at("Outcome") == "Rejected" && refused.at("Reason") == "Effect not defined");
    CHECK(CCall(r, f.Enemy, "Gameplay.EffectApply", { { "Effect", "Frost" } }).at("Outcome") == "Added");
    const auto events = f.Combat().EffectCount();
    auto state = CCall(r, f.Enemy, "Gameplay.EffectState", { { "Effect", "Poison" } });
    CHECK(state.at("Active") == true && state.at("Stacks") == 1 && state.at("Instances") == 1 && state.at("Fraction") == 1 && state.at("Magnitude") == 2);
    auto mods = CCall(r, f.Enemy, "Gameplay.CombatModifiers");
    CHECK(mods.at("Speed") == .5 && mods.at("Armor") == -5 && mods.at("Stunned") == false && mods.at("Effects") == 2);
    CHECK(CCall(r, f.Hero, "Gameplay.CombatModifiers").at("Speed") == 1 && CCall(r, f.Hero, "Gameplay.EffectState").at("Active") == false);
    CHECK(f.Combat().EffectCount() == events && f.Drain(f.Enemy).size() == 2); // pure nodes changed nothing
    CHECK(CCall(r, f.Enemy, "Gameplay.EffectRemove", { { "Effect", "Poison" } }).at("Removed") == 1);
    CHECK(CCall(r, f.Enemy, "Gameplay.EffectRemove").at("Removed") == 1 && f.Combat().EffectCount() == 0);
    CHECK(CCall(r, f.Hero, "Gameplay.AreaDefine", { { "TwoD", true }, { "LineOfSight", false }, { "Radius", 2 } }).at("Success") == true);
    auto hit = CCall(r, f.Hero, "Gameplay.AreaApply", { { "Victim", enemy } });
    CHECK(hit.at("Success") == true && hit.at("Affected") == 1 && hit.at("Applied") == 10 && f.HP(f.Enemy) == 90);
    CHECK(CCall(r, f.Hero, "Gameplay.AreaApply", { { "Center", Visual::Value::array({ 5., 0., 0. }) } }).at("Affected") == 1);
    CHECK(CCall(r, f.Hero, "Gameplay.AreaApply").at("Affected") == 0); // the owner is spared, the enemy out of range
    CHECK(CCall(r, f.Hero, "Gameplay.AreaApply", { { "Victim", uint64_t(999) } }).at("Reason") == "Area victim unavailable");
    CHECK(CCall(r, f.Hero, "Gameplay.AreaApply", { { "Area", "Nope" } }).at("Reason") == "Area not defined");
    CHECK(CCall(r, f.Hero, "Gameplay.AreaApply", { { "Area", "Nope" }, { "Victim", enemy } }).at("Reason") == "Area not defined");
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.EffectDefine", { { "Tick", 4 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.EffectDefine", { { "Stacking", .5 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.EffectDefine", { { "MaxStacks", 0 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.EffectDefine", { { "MaxStacks", 17 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.EffectDefine", { { "Modifiers", Visual::Value::array({ 0, 0, 0, 0 }) } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.AreaDefine", { { "Falloff", 2 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.AreaDefine", { { "MaxTargets", 33 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.AreaDefine", { { "Mask", -1 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.AreaDefine", { { "Kind", 3 } }); }));
    CHECK(Throws([&] { CCall(r, f.Enemy, "Gameplay.EffectApply", { { "Magnitude", 1e9 } }); }));
    CHECK(Throws([&] { CCall(r, f.Enemy, "Gameplay.EffectRemove", { { "Effect", "bad key" } }); }));
}
namespace {
    // A deterministic fight with every effect and area path: piercing and area projectiles carrying effects,
    // a line-of-sight wall, all four stacking rules, healing support and a damage-over-time kill. Everything
    // resolves before the end, so the digest is independent of where the last frame boundary falls.
    uint64_t EffectScenario(const std::vector<float>& deltas, double seconds, bool check = false)
    {
        CFixture f([](CFixture& x) { StaticWall2D(x.World, { 7, 3, 0 }, { .1f, 1 }); });
        auto e1 = f.Actor(301, { 4, 0, 0 }, 2, 60), e2 = f.Actor(302, { 6, 0, 0 }, 2, 60), e3 = f.Actor(303, { 5, 2.5f, 0 }, 2, 80);
        auto e4 = f.Actor(304, { 8, 3, 0 }, 2, 40), ally = f.Actor(305, { 1, 1, 0 }, 1, 100);
        Define(f, Dot("Poison", 3, 1, 2, G::StackRule::Independent, 3));
        Define(f, Dot("Burn", 2, .5, 3, G::StackRule::Refresh, 3));
        auto frost = Effect("Frost", 1.5, G::StackRule::Extend, 2);
        frost.Speed = .5;
        Define(f, frost);
        auto regen = Dot("Regen", 2, .5, 4);
        regen.Tick = G::TickKind::Heal;
        regen.Hostile = false;
        Define(f, regen);
        auto shock = Effect("Shock", .5, G::StackRule::HighestWins);
        shock.Stun = true;
        Define(f, shock);
        auto nova = Area("Nova", 3, 12);
        nova.Shape = G::Falloff::Linear;
        nova.LineOfSight = true;
        nova.Effect = "Frost";
        Define(f, nova);
        auto mend = Area("Mend", 3, 5);
        mend.Kind = G::PayloadKind::Heal;
        mend.AffectOwner = true;
        mend.Effect = "Regen";
        Define(f, mend);
        auto lance = CFixture::Bolt(true, "Lance");
        lance.Pierce = 2;
        lance.Speed = 14;
        lance.Lifetime = 1;
        lance.Effect = "Burn";
        auto bomb = CFixture::Bolt(true, "Bomb");
        bomb.Speed = 8;
        bomb.Effect = "Poison";
        bomb.Area = "Nova";
        auto mine = CFixture::Bolt(true, "Mine");
        mine.Speed = 3;
        mine.Lifetime = .8;
        mine.Area = "Nova";
        mine.DetonateOnExpire = true;
        for (const auto& s : { lance, bomb, mine }) f.Define(s);
        f.Rules().Damage(ally, {}, 30, false, false, true);
        f.Fire("Lance");
        f.Fire("Bomb", { 0, 0, 0 }, {}, e3);
        f.Fire("Mine", { 1, 1, 0 });
        f.Fire("Bomb", { 1, .4f, 0 });
        f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero);
        f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero);
        f.Combat().ApplyEffect(e4, "Shock", f.Hero, 1);
        f.Combat().ApplyArea("Mend", f.Hero, { 0, 0, 0 });
        double time = 0;
        for (size_t i = 0; time < seconds; ++i)
        {
            const float delta = deltas[i % deltas.size()];
            f.World.UpdateRuntime(delta);
            time += delta;
        }
        uint64_t hash = 1469598103934665603ull;
        auto mix = [&](const std::string& text) {
            for (unsigned char c : text) hash = (hash ^ c) * 1099511628211ull;
        };
        for (auto actor : { f.Hero, f.Enemy, e1, e2, e3, e4, ally })
        {
            for (const auto& e : f.Drain(actor)) mix(std::format("{}|{}|{}|{}|{:.9g}|{:.6f},{:.6f}\n", e.Kind, e.Key, e.Source, e.Subject, e.Amount, e.Position.x, e.Position.y));
            mix(std::format("hp {:.9g} dead {}\n", f.HP(actor), f.Rules().Dead(actor)));
        }
        const auto stats = f.Combat().Stats();
        mix(std::format("live {} effects {} hits {} detonations {}\n", f.Combat().LiveCount(), f.Combat().EffectCount(), stats.Hits, stats.Detonations));
        if (check)
        {
            // A nontrivial, fully resolved fight.
            CHECK(f.Combat().LiveCount() == 0 && f.Combat().EffectCount() == 0);
            // The ally: 70 + Mend 5 + four Regen ticks of 4. e4 is behind the wall and off every path: only stunned.
            // The Enemy stands on the Lance path: pierce hit and Burn on top of its two Poison instances (12).
            CHECK(stats.Hits >= 4 && stats.Detonations >= 4 && f.HP(ally) == 91 && f.HP(e4) == 40);
            CHECK(f.HP(e1) < 60 && f.HP(e2) < 60 && f.HP(e3) < 80 && f.HP(f.Enemy) < 100 - 12 - 20);
        }
        return hash;
    }
}
TEST(CombatEffectDeterministicEventLogAcrossFreshScenesAndFrameRates)
{
    const auto reference = EffectScenario({ 1.0f / 60 }, 7, true);
    CHECK(EffectScenario({ 1.0f / 60 }, 7) == reference);
    CHECK(EffectScenario({ 1.0f / 30 }, 7) == reference);
    CHECK(EffectScenario({ 1.0f / 144 }, 7) == reference);
    CHECK(EffectScenario({ 1.0f / 50, 1.0f / 90, 1.0f / 40, 1.0f / 120 }, 7) == reference);
}
TEST(CombatEffectSaveV4RestoresEffectsAndBuffedEnergy)
{
    CFixture f;
    std::string e;
    auto& saves = G::SaveWorld::For(f.World);
    auto& kit = G::LoadoutWorld::For(f.World);
    CHECK(kit.Configure(f.Hero, { 0, 0, 0, 100, 0 }, e));
    CHECK(saves.Bind(f.Hero, "Player", e) && saves.Bind(f.Enemy, "Enemy", e));
    Define(f, Buff("Haste", 30, { 0, 0, 0, 50, 0 }));
    Define(f, Dot("Poison", 3, 1, 2));
    auto plain = saves.Capture("V1");
    CHECK(plain.Version == 4); // definitions alone make it a combat save (with no effects)
    CHECK(f.Combat().ApplyEffect(f.Hero, "Haste").Outcome == Outcome::Added && kit.Refill(f.Hero, 50) == 50);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Outcome == Outcome::Added);
    f.Run(30); // Poison: 2.5 s left, its first tick due in 0.5 s
    auto save = saves.Capture("V1");
    for (const auto& s : save.Entities)
    {
        if (s.Key == "Player") CHECK(s.Loadout && s.Loadout->Energy == 150 && s.Effects.size() == 1 && s.Effects[0].Key == "Haste"); // buffed energy is valid
        if (s.Key == "Enemy") CHECK(s.Effects.size() == 1 && s.Effects[0].Source == "Player" && std::abs(s.Effects[0].Remaining - 2.5) < 1e-9 && std::abs(s.Effects[0].Tick - .5) < 1e-9);
    }
    f.Run(60);
    CHECK(f.HP(f.Enemy) == 98 && kit.State(f.Hero)->State.Energy == 150);
    auto result = saves.Restore(save, "V1");
    CHECK(result.Success && result.Reason.empty());
    CHECK(f.Combat().EffectCount() == 2 && f.HP(f.Enemy) == 100 && std::abs(f.Combat().Effect(f.Enemy, "Poison").Remaining - 2.5) < 1e-9);
    auto s = kit.State(f.Hero);
    CHECK(s->State.Energy == 150 && s->Stats[3] == 150);
    f.Run(200); // the three remaining ticks, then expiry
    CHECK(f.HP(f.Enemy) == 94 && !f.Combat().Effect(f.Enemy, "Poison").Active);
    CHECK(saves.Restore(plain, "V1").Success); // that snapshot had no effects: they are removed
    CHECK(f.Combat().EffectCount() == 0 && kit.State(f.Hero)->State.Energy == 100 && kit.State(f.Hero)->Stats[3] == 100);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Outcome == Outcome::Added); // definitions stay
}
TEST(CombatEffectPlayStopStartResetsEffectsAndDefinitions)
{
    CFixture f;
    Define(f, Dot("Poison", 3, 1, 2));
    Define(f, Area("Blast", 2, 5));
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Outcome == Outcome::Added);
    f.World.OnRuntimeStop();
    f.World.OnRuntimeStart();
    f.Health(f.Hero, 1);
    f.Health(f.Enemy, 2);
    CHECK(f.Combat().EffectCount() == 0 && f.Combat().DefinitionCount() == 0 && !f.Combat().EffectDefinition("Poison"));
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Reason == "Effect not defined");
    CHECK(f.Combat().ApplyArea("Blast", f.Hero, { 0, 0, 0 }).Reason == "Area not defined");
}
