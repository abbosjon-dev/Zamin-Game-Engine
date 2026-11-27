#include "CombatFixture.h"
#include <Zamin/Editor/EditorLanguage.h>
#include <Zamin/Gameplay/GameplayUI.h>
#include <Zamin/Gameplay/UIModel.h>
#include <Zamin/Localization/Localization.h>
namespace {
    // Hero (team 1, Attack 5, Power 10, 100 energy, no regeneration) against the Enemy 5 m away on +X.
    struct AFixture : CFixture
    {
        Entity Ally;
        AFixture()
        {
            std::string e;
            CHECK(Kit().Configure(Hero, { 5, 0, 10, 100, 0 }, e));
            auto bolt = CFixture::Bolt();
            bolt.Damage = 0; // the ability supplies the amount
            Define(bolt);
            auto salve = CFixture::Bolt(true, "Salve");
            salve.Kind = G::PayloadKind::Heal;
            Define(salve);
            Gameplay::EffectSpec burn = Dot("Burn", 2, .5, 3);
            ::Define(*this, burn);
            auto regen = Effect("Regen", 2);
            regen.Hostile = false;
            regen.Period = .5;
            regen.Tick = G::TickKind::Heal;
            regen.TickAmount = 4;
            ::Define(*this, regen);
            ::Define(*this, Buff("Haste", 5, { 10, 0, 0, 0, 0 }, 1.5));
            ::Define(*this, Area("Nova", 3, 12));
            auto pulse = Area("Pulse", 3, 8);
            pulse.Kind = G::PayloadKind::Heal;
            pulse.AffectOwner = true;
            ::Define(*this, pulse);
            Ally = Actor(150, { -3, 0, 0 }, 1);
        }
        G::LoadoutWorld& Kit() { return G::LoadoutWorld::For(World); }
        bool Learn(G::AbilitySpec a, std::string* error = nullptr)
        {
            std::string e;
            const bool ok = Kit().DefineAbility(Hero, a, e);
            if (error) *error = e;
            return ok;
        }
        G::AbilityResult Cast(const std::string& key, Entity victim, glm::vec3 direction = {})
        {
            G::CastAim aim;
            aim.Direction = direction;
            return Kit().Cast(Hero, victim, key, Query2D(), aim);
        }
        double Energy() { return Kit().State(Hero)->State.Energy; }
        G::AbilityProgress Progress(const std::string& key) { return Kit().State(Hero)->State.Abilities.at(key); }
        std::vector<G::LoadoutEvent> Casts()
        {
            std::vector<G::LoadoutEvent> out;
            while (auto e = Kit().Poll(Hero)) out.push_back(*e);
            return out;
        }
    };
    G::AbilitySpec Linked(const std::string& key, G::AbilityKind kind, double amount, double cost, std::string projectile = {}, std::string area = {}, std::string effect = {})
    {
        auto a = Ability(key, amount, cost, .5);
        a.Kind = kind;
        a.Range = 12;
        a.Projectile = std::move(projectile);
        a.Area = std::move(area);
        a.Effect = std::move(effect);
        return a;
    }
    Entity Text(Scene& scene, Entity canvas, const std::string& name, const std::string& text)
    {
        auto e = scene.CreateEntity(name);
        auto& t = e.AddComponent<UITransformComponent>();
        t.Size = { 200, 30 };
        scene.SetParent(e, canvas, false);
        e.AddComponent<TextComponent>().Text = text;
        return e;
    }
}
TEST(CombatAbilityProjectileCommitsAtLaunchAndAMissStillSpends)
{
    AFixture f;
    CHECK(f.Learn(Linked("Shot", G::AbilityKind::Damage, 20, 10, "Bolt")));
    f.Casts();
    auto cast = f.Cast("Shot", f.Enemy);
    CHECK(cast.Success && cast.Projectile != 0 && cast.Target == uint64_t(f.Enemy.GetUUID()));
    // Damage snapshot at launch: Amount 20 + Power 10, then Fire adds Attack 5.
    CHECK(cast.Applied == 35 && f.Combat().Projectile(cast.Projectile)->Damage == 35);
    CHECK(f.Energy() == 90 && f.Progress("Shot").Remaining == .5 && f.Progress("Shot").Uses == 1);
    auto events = f.Casts();
    CHECK(events.size() == 1 && events[0].Kind == "Cast" && events[0].Amount == 35);
    CHECK(f.HP(f.Enemy) == 100); // nothing has hit yet: resources were committed at launch
    f.Run(40);
    CHECK(f.HP(f.Enemy) == 65 && f.Combat().LiveCount() == 0);
    // A deliberate miss (aimed away, no victim) still spends; a cast that cannot fire spends nothing.
    f.Run(30);
    auto miss = f.Cast("Shot", {}, { 0, 1, 0 });
    CHECK(miss.Success && miss.Target == 0 && f.Energy() == 80 && f.Progress("Shot").Uses == 2);
    f.Run(40);
    CHECK(f.HP(f.Enemy) == 65);
    f.Run(30);
    CHECK(f.Cast("Shot", {}).Reason == "Ability needs a target or direction");
    f.Enemy.GetTransform().Translation = { 20, 0, 0 }; // beyond Range 12
    CHECK(f.Cast("Shot", f.Enemy).Reason == "Ability target invalid or blocked/out of range");
    CHECK(f.Learn(Linked("Ghost", G::AbilityKind::Damage, 5, 1, "Missing")));
    CHECK(f.Cast("Ghost", {}, { 1, 0, 0 }).Reason == "Projectile not defined"); // links resolve at Cast
    CHECK(f.Learn(Linked("Mixed", G::AbilityKind::Heal, 5, 1, "Bolt")));
    CHECK(f.Cast("Mixed", {}, { 1, 0, 0 }).Reason == "Ability/projectile kind mismatch");
    CHECK(f.Energy() == 80 && f.Progress("Shot").Uses == 2 && f.Progress("Ghost").Uses == 0 && f.Combat().LiveCount() == 1); // the miss flies on
    // A support projectile flies to an ally (never an enemy) and heals on impact.
    std::string e;
    CHECK(f.Rules().Damage(f.Ally, {}, 30, true, true, true).Applied == 30);
    CHECK(f.Learn(Linked("Salve", G::AbilityKind::Heal, 10, 5, "Salve")));
    CHECK(f.Cast("Salve", f.Enemy).Reason == "Ability target invalid or blocked/out of range");
    auto salve = f.Cast("Salve", f.Ally);
    CHECK(salve.Success && salve.Applied == 20); // 10 + Power 10; Attack is for damage only
    f.Run(30);
    CHECK(f.HP(f.Ally) == 90 && f.Energy() == 75);
}
TEST(CombatAbilityEffectLinksOverrideAndFollowInstantHits)
{
    AFixture f;
    std::string error;
    // The projectile carries no effect: the ability's Burn rides on the hit.
    CHECK(f.Learn(Linked("Ember", G::AbilityKind::Damage, 5, 0, "Bolt", "", "Burn")));
    CHECK(f.Cast("Ember", f.Enemy).Success);
    f.Run(40);
    CHECK(f.HP(f.Enemy) == 80 && f.Combat().Effect(f.Enemy, "Burn").Active);
    CHECK(f.Learn(Linked("Wrong", G::AbilityKind::Damage, 5, 0, "Bolt", "", "Regen")));
    CHECK(f.Cast("Wrong", f.Enemy).Reason == "Ability effect kind mismatch");
    // Instant damage + effect: Burn follows a hit that did damage.
    CHECK(f.Learn(Linked("Zap", G::AbilityKind::Damage, 5, 1, "", "", "Burn")));
    f.Combat().RemoveEffects(f.Enemy, "");
    auto zap = f.Cast("Zap", f.Enemy);
    CHECK(zap.Success && zap.Applied == 20 && f.Combat().Effect(f.Enemy, "Burn").Active); // 5 + Power 10 + Attack 5
    // Support + effect: Regen on the caster after a heal that healed.
    CHECK(f.Rules().Damage(f.Hero, {}, 40, true, true, true).Applied == 40);
    CHECK(f.Learn(Linked("Mend", G::AbilityKind::Heal, 5, 1, "", "", "Regen")));
    auto mend = f.Cast("Mend", {});
    CHECK(mend.Success && mend.Applied == 15 && f.Combat().Effect(f.Hero, "Regen").Active);
    // Status abilities: hostile needs a victim in range and sight; beneficial is self-cast only.
    CHECK(f.Learn(Linked("Hex", G::AbilityKind::Effect, 1, 3, "", "", "Burn")));
    CHECK(f.Learn(Linked("Rally", G::AbilityKind::Effect, 1, 4, "", "", "Haste")));
    const double energy = f.Energy();
    CHECK(f.Cast("Hex", {}).Reason == "Ability target invalid or blocked/out of range" && f.Energy() == energy);
    f.Combat().RemoveEffects(f.Enemy, "");
    auto hex = f.Cast("Hex", f.Enemy);
    CHECK(hex.Success && hex.Applied == 1 && f.Combat().Effect(f.Enemy, "Burn").Active && f.Energy() == energy - 3);
    CHECK(f.Cast("Rally", f.Enemy).Reason == "Support ability must target self");
    auto rally = f.Cast("Rally", {});
    CHECK(rally.Success && f.Combat().Effect(f.Hero, "Haste").Active && f.Kit().State(f.Hero)->Stats[0] == 15);
    // Definition rules.
    auto both = Linked("Both", G::AbilityKind::Damage, 5, 1, "Bolt", "Nova");
    CHECK(!f.Learn(both, &error) && error == "Ability projectile and area are exclusive");
    CHECK(!f.Learn(Linked("Empty", G::AbilityKind::Effect, 1, 1), &error) && error == "Effect ability needs an effect and no projectile or area");
    CHECK(!f.Learn(Linked("Fired", G::AbilityKind::Effect, 1, 1, "Bolt", "", "Burn"), &error));
    CHECK(!f.Learn(Linked("Bad", G::AbilityKind::Damage, 1, 1, "bad key"), &error) && error == "Invalid ability definition");
}
TEST(CombatAbilityAreaCommitsOnlyWhenItReachesATarget)
{
    AFixture f;
    CHECK(f.Learn(Linked("Nova", G::AbilityKind::Damage, 12, 20, "", "Nova")));
    // Centred on the caster: the enemy is 5 m away (radius 3) and the caster is not its own target.
    CHECK(f.Cast("Nova", {}).Reason == "Ability had no effect" && f.Energy() == 100 && f.Progress("Nova").Uses == 0);
    auto nova = f.Cast("Nova", f.Enemy); // centred on the victim
    CHECK(nova.Success && nova.Target == uint64_t(f.Enemy.GetUUID()));
    CHECK(f.HP(f.Enemy) == 100 - (12 + 10 + 5) && f.Energy() == 80); // Amount + Power, plus Attack in the area
    CHECK(f.Learn(Linked("Pulse", G::AbilityKind::Heal, 8, 5, "", "Pulse")));
    auto full = f.Cast("Pulse", {}); // everyone in reach is at full health: the area still reached them, so it commits
    CHECK(full.Success && full.Applied == 0 && f.Energy() == 75);
    CHECK(f.Rules().Damage(f.Hero, {}, 50, true, true, true).Applied == 50);
    f.Run(31); // the cooldown (0.5 s)
    auto pulse = f.Cast("Pulse", {}); // centred on the caster, who is inside it (AffectOwner); so is the ally
    CHECK(pulse.Success && pulse.Applied == 18 && f.HP(f.Hero) == 50 + 18 && f.Energy() == 70);
    CHECK(f.Learn(Linked("WrongArea", G::AbilityKind::Damage, 8, 5, "", "Pulse")));
    CHECK(f.Cast("WrongArea", f.Enemy).Reason == "Ability/area kind mismatch");
}
TEST(CombatAbilityBlueprintPinsAimAndReturnTheProjectile)
{
    AFixture f;
    Gameplay::Runtime r;
    auto define = CCall(r, f.Hero, "Gameplay.AbilityDefine", { { "Ability", "Shot" }, { "Projectile", "Bolt" }, { "Range", 12 } });
    CHECK(define.at("Success") == true);
    CHECK(CCall(r, f.Hero, "Gameplay.AbilityDefine", { { "Ability", "Hex" }, { "Kind", 3 }, { "Effect", "Burn" } }).at("Success") == true);
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.AbilityDefine", { { "Ability", "Odd" }, { "Kind", 4 } }); }));
    auto cast = CCall(r, f.Hero, "Gameplay.AbilityCast", { { "Ability", "Shot" }, { "TwoD", true }, { "Direction", Visual::Value::array({ 1, 0, 0 }) } });
    CHECK(cast.at("Success") == true && cast.at("Projectile").get<double>() > 0 && cast.at("Victim") == 0);
    auto state = CCall(r, f.Hero, "Gameplay.ProjectileState", { { "Projectile", cast.at("Projectile") } });
    CHECK(state.at("Alive") == true && state.at("Key") == "Bolt");
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.AbilityCast", { { "Ability", "Shot" }, { "Direction", Visual::Value::array({ 1, 0 }) } }); }));
    // Graphs written before the new pins keep working: missing inputs fall back to the pin defaults.
    auto defaults = Visual::Catalog().at("Gameplay.AbilityCast").Inputs;
    CHECK(std::any_of(defaults.begin(), defaults.end(), [](auto& p) { return p.Name == "Direction" && p.Default == Visual::Value::array({ 0., 0., 0. }); }));
}
TEST(CombatHUDEffectStacksTimeLeftAndHitMarker)
{
    Entity canvas, stacks, bar, marker, markerBar;
    CFixture f([&](CFixture& s) {
        canvas = s.World.CreateEntity("Canvas");
        canvas.AddComponent<UICanvasComponent>();
        stacks = Text(s.World, canvas, "Burn", "authored");
        bar = s.World.CreateEntity("Burn bar");
        bar.AddComponent<UITransformComponent>();
        s.World.SetParent(bar, canvas, false);
        bar.AddComponent<UIProgressBarComponent>().Value = .4f;
        marker = Text(s.World, canvas, "Marker", "authored marker");
        markerBar = s.World.CreateEntity("Marker bar");
        markerBar.AddComponent<UITransformComponent>();
        s.World.SetParent(markerBar, canvas, false);
        markerBar.AddComponent<UIProgressBarComponent>();
    });
    Define(f, Dot("Burn", 2, .5, 3, G::StackRule::Refresh, 3));
    f.Define(CFixture::Bolt());
    auto& ui = G::UIWorld::For(f.World);
    std::string error;
    CHECK(ui.BindHUD(f.Hero, f.Enemy, stacks, { G::HUDKind::Effect, "Burn", "YONISH x", true }, error));
    CHECK(ui.BindHUD(f.Hero, f.Enemy, bar, { G::HUDKind::Effect, "Burn", "", false }, error));
    CHECK(ui.BindHUD(f.Hero, f.Hero, marker, { G::HUDKind::HitMarker, "", "X", false }, error));
    CHECK(ui.BindHUD(f.Hero, f.Hero, markerBar, { G::HUDKind::HitMarker, "", "", false }, error));
    CHECK(!ui.BindHUD(f.Hero, f.Enemy, Text(f.World, canvas, "Bad", ""), { G::HUDKind::Effect, "bad key", "", false }, error));
    ui.Refresh();
    CHECK(stacks.GetComponent<TextComponent>().Text == "YONISH x--" && bar.GetComponent<UIProgressBarComponent>().Value == 0);
    CHECK(marker.GetComponent<TextComponent>().Text.empty());
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Burn", f.Hero).Outcome == Outcome::Added);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Burn", f.Hero).Outcome == Outcome::Stacked);
    f.Run(30);
    CHECK(stacks.GetComponent<TextComponent>().Text == "YONISH x2 / 3");
    CHECK_NEAR(bar.GetComponent<UIProgressBarComponent>().Value, 1.5f / 2, 1e-5);
    // The hit marker: the hero's bolt hits at 5 m - 0.55 m (4.45 m at 10 m/s); periodic Burn ticks never count.
    CHECK(marker.GetComponent<TextComponent>().Text.empty());
    f.Fire("Bolt");
    f.Run(27);
    CHECK(marker.GetComponent<TextComponent>().Text == "X" && markerBar.GetComponent<UIProgressBarComponent>().Value > .5f);
    f.Run(16);
    CHECK(marker.GetComponent<TextComponent>().Text.empty() && markerBar.GetComponent<UIProgressBarComponent>().Value == 0);
    f.Run(120);
    CHECK(stacks.GetComponent<TextComponent>().Text == "YONISH x--"); // expired
    CHECK(ui.UnbindHUD(stacks) && stacks.GetComponent<TextComponent>().Text == "authored");
    CHECK(ui.UnbindHUD(bar) && bar.GetComponent<UIProgressBarComponent>().Value == .4f);
}
TEST(CombatHUDFeedRowsFadeAndRestoreAuthoredText)
{
    Localization::Clear(); // no language files (a slice test before may have left a project's names): the rows show keys
    std::vector<Entity> rows;
    Entity canvas;
    CFixture f([&](CFixture& s) {
        canvas = s.World.CreateEntity("Canvas");
        canvas.AddComponent<UICanvasComponent>();
        for (int i = 0; i < 7; ++i)
        {
            rows.push_back(Text(s.World, canvas, "Row" + std::to_string(i), "row " + std::to_string(i)));
            rows.back().GetComponent<TextComponent>().Color = { 1, .5f, 0, .8f };
        }
    });
    Define(f, Dot("Burn", 2, .5, 3));
    auto bolt = CFixture::Bolt();
    bolt.Effect = "Burn";
    f.Define(bolt);
    auto& ui = G::UIWorld::For(f.World);
    std::string error;
    CHECK(!ui.BindCombatFeed(f.Hero, f.Hero, "Feed", rows, {}, error)); // 7 rows: the limit is 6
    CHECK(!ui.BindCombatFeed(f.Hero, f.Hero, "Feed", { rows[0] }, { false, false, 1 }, error));
    CHECK(!ui.BindCombatFeed(f.Hero, f.Hero, "Feed", { rows[0] }, { true, false, 20 }, error));
    CHECK(ui.BindCombatFeed(f.Hero, f.Hero, "Feed", { rows[0], rows[1], rows[2] }, { true, false, 1.2 }, error));
    CHECK(!ui.BindCombatFeed(f.Hero, f.Enemy, "Taken", { rows[2] }, {}, error)); // a row serves one feed
    CHECK(!ui.BindHUD(f.Hero, f.Hero, rows[1], {}, error));
    CHECK(ui.BindCombatFeed(f.Hero, f.Enemy, "Taken", { rows[3] }, { false, true, 1.2 }, error));
    ui.Refresh();
    CHECK(rows[0].GetComponent<TextComponent>().Text.empty() && rows[3].GetComponent<TextComponent>().Text.empty());
    f.Fire("Bolt");
    f.Run(28); // hit: 20 damage, then Burn
    CHECK(rows[0].GetComponent<TextComponent>().Text == "-20  Bolt" && rows[1].GetComponent<TextComponent>().Text.empty());
    CHECK(rows[3].GetComponent<TextComponent>().Text == "-20  Bolt"); // the enemy's incoming feed
    const float fresh = rows[0].GetComponent<TextComponent>().Color.a;
    CHECK(fresh > .7f && fresh <= .8f && rows[0].GetComponent<TextComponent>().Color.r == 1);
    f.Run(30); // the first Burn tick (3) is newest
    CHECK(rows[0].GetComponent<TextComponent>().Text == "-3  Burn" && rows[1].GetComponent<TextComponent>().Text == "-20  Bolt");
    CHECK(rows[1].GetComponent<TextComponent>().Color.a < fresh);
    // The project's language files name the payloads ("combat.<Key>"): the rows follow the language, keys without a
    // string stay as they are.
    {
        std::vector<StringTable> tables(2);
        tables[0].Language = "uz";
        tables[0].Strings["combat.Bolt"] = { "Olov o'qi", {} };
        tables[1].Language = "en";
        tables[1].Strings["combat.Bolt"] = { "Fire bolt", {} };
        LocalizationSettings settings;
        settings.Languages = { "uz", "en" };
        Localization::SetTables(std::move(tables), settings);
        ui.Refresh();
        CHECK(rows[0].GetComponent<TextComponent>().Text == "-3  Burn" && rows[1].GetComponent<TextComponent>().Text == "-20  Olov o'qi");
        CHECK(Localization::SetLanguage("en"));
        ui.Refresh();
        CHECK(rows[1].GetComponent<TextComponent>().Text == "-20  Fire bolt" && G::LocalizedGameplayName("combat.", "Burn", "Burn") == "Burn");
        CHECK(G::LocalizedGameplayName("combat.", "Bolt", "Bolt", 4) == "Bolt"); // longer than the limit: the key
        Localization::Clear();
    }
    f.Run(200); // every record (the last Burn tick included) is older than the lifetime
    CHECK(rows[0].GetComponent<TextComponent>().Text.empty() && rows[0].GetComponent<TextComponent>().Color.a == .8f);
    CHECK(ui.RemoveCombatFeed("Feed") && !ui.RemoveCombatFeed("Feed"));
    CHECK(rows[0].GetComponent<TextComponent>().Text == "row 0" && rows[0].GetComponent<TextComponent>().Color == glm::vec4(1, .5f, 0, .8f));
    // An owner that disappears releases its feed and the authored rows.
    auto owner = f.World.CreateEntity("Feed owner");
    CHECK(ui.BindCombatFeed(owner, f.Hero, "Owned", { rows[4] }, {}, error));
    f.World.DestroyEntity(owner);
    f.Run(2);
    CHECK(rows[4].GetComponent<TextComponent>().Text == "row 4" && ui.BindCombatFeed(f.Hero, f.Hero, "Owned", { rows[4] }, {}, error));
    // Blueprint: rows are an entity array (1..6) and the lifetime is checked.
    Gameplay::Runtime r;
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.UICombatFeed", { { "Rows", Visual::Value::array() } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.UICombatFeed", { { "Rows", Visual::Value::array({ uint64_t(rows[5].GetUUID()) }) }, { "Lifetime", 0 } }); }));
    auto bound = CCall(r, f.Hero, "Gameplay.UICombatFeed", { { "Name", "Blueprint" }, { "Actor", uint64_t(f.Hero.GetUUID()) }, { "Rows", Visual::Value::array({ uint64_t(rows[5].GetUUID()) }) } });
    CHECK(bound.at("Success") == true);
    CHECK(CCall(r, f.Hero, "Gameplay.UICombatFeedRemove", { { "Name", "Blueprint" } }).at("Success") == true);
    CHECK(CCall(r, f.Hero, "Gameplay.UIBindHUD", { { "Kind", 6 }, { "Actor", uint64_t(f.Hero.GetUUID()) }, { "Prefix", "X" } }).at("Success") == false); // the host is not a widget
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.UIBindHUD", { { "Kind", 10 } }); }));
}
TEST(GameUIModelCombatRowsRoundAndFade)
{
    CHECK(G::PresentCombatRow(true, 17.5, "Bolt") == "-18  Bolt" && G::PresentCombatRow(false, 4.4, "Mend") == "+4  Mend");
    CHECK(G::PresentCombatRow(true, 0, "Poison") == "-0  Poison");
    CHECK(Throws([] { G::PresentCombatRow(true, -1, "Bolt"); }) && Throws([] { G::PresentCombatRow(true, 1, std::string(65, 'k')); }));
    CHECK(G::CombatRowAlpha(0, 1.2) == 1 && G::CombatRowAlpha(.6, 1.2) == .5f && G::CombatRowAlpha(5, 1.2) == 0 && G::CombatRowAlpha(-1, 1.2) == 1);
    CHECK(Throws([] { G::CombatRowAlpha(0, 0); }));
}
// Every enumerated pin has translated names, as many as the integers its dispatch accepts (one more is rejected).
TEST(VisualGraphPinChoicesAreTranslated)
{
    CFixture f;
    Gameplay::Runtime r;
    size_t pins = 0;
    for (const auto& [kind, definition] : Visual::Catalog())
        for (const auto& pin : definition.Inputs)
        {
            const auto* choices = Visual::PinChoices(kind, pin.Name);
            if (!choices) continue;
            ++pins;
            CHECK(pin.Kind == Visual::Type::Number && !choices->empty());
            for (const char* label : *choices)
            {
                CHECK(EditorText::HasTranslation(label));
                if (!EditorText::HasTranslation(label)) std::printf("    no translation: %s\n", label);
            }
            // Curve.AddKey's Mode (Constant/Linear/Cubic) is checked by the VM, not the gameplay dispatch.
            if (!kind.starts_with("Gameplay.")) continue;
            const double last = double(choices->size() - 1);
            CHECK(!Throws([&] { CCall(r, f.Hero, kind, { { pin.Name, last } }); }));
            CHECK(Throws([&] { CCall(r, f.Hero, kind, { { pin.Name, last + 1 } }); }));
        }
    CHECK(pins == 11);
    CHECK(!Visual::PinChoices("Gameplay.ProjectileDefine", "Pierce") && !Visual::PinChoices("Missing", "Kind"));
    EditorText::SetLanguage(EditorLanguage::English);
    CHECK(std::string(Tr(Visual::PinChoices("Gameplay.EffectDefine", "Stacking")->at(3))) == "Highest wins");
    EditorText::SetLanguage(EditorLanguage::Uzbek);
}
// A beneficial self-cast status that lowers MaxEnergy clamps the energy the cast commits: the state never exceeds
// its capacity, so the same frame's snapshot restores.
TEST(CombatAbilityCastKeepsEnergyWithinAnEffectLoweredCapacity)
{
    AFixture f;
    ::Define(f, Buff("Weary", 5, { 0, 0, 0, -60, 0 }));
    CHECK(f.Learn(Linked("Focus", G::AbilityKind::Effect, 1, 10, "", "", "Weary")));
    CHECK(f.Energy() == 100);
    auto cast = f.Cast("Focus", {});
    CHECK(cast.Success && f.Combat().Effect(f.Hero, "Weary").Active);
    CHECK(f.Kit().State(f.Hero)->Stats[3] == 40 && f.Energy() == 40); // min(100 - 10, 100 - 60)
    std::string e;
    auto& saves = G::SaveWorld::For(f.World);
    CHECK(saves.Bind(f.Hero, "Player", e));
    const auto save = saves.Capture("Authoring");
    const auto restored = saves.Restore(save, "Authoring");
    CHECK(restored.Success && restored.Reason.empty() && f.Energy() == 40);
}
// Ticks that change nothing (a heal on a full-health actor) are no hits, and a burst of real ticks never hides a
// fresh direct hit from the hit marker.
TEST(CombatHUDZeroTicksAreNoHitsAndTickBurstsNeverHideTheMarker)
{
    Entity canvas, marker;
    std::vector<Entity> rows;
    CFixture f([&](CFixture& s) {
        canvas = s.World.CreateEntity("Canvas");
        canvas.AddComponent<UICanvasComponent>();
        marker = Text(s.World, canvas, "Marker", "");
        for (int i = 0; i < 4; ++i) rows.push_back(Text(s.World, canvas, "Row" + std::to_string(i), ""));
    });
    auto regen = Effect("Regen", 3);
    regen.Hostile = false;
    regen.Period = .25;
    regen.Tick = G::TickKind::Heal;
    regen.TickAmount = 4;
    Define(f, regen);
    Define(f, Dot("Burn", 2, .05, 1));
    f.Define(CFixture::Bolt());
    auto& ui = G::UIWorld::For(f.World);
    std::string error;
    CHECK(ui.BindHUD(f.Hero, f.Hero, marker, { G::HUDKind::HitMarker, "", "X", false }, error));
    CHECK(ui.BindCombatFeed(f.Hero, f.Hero, "Feed", rows, { true, true, 5 }, error));
    // Regen on the full-health Hero: four ticks heal nothing, so no record and no "+0  Regen" rows.
    CHECK(f.Combat().ApplyEffect(f.Hero, "Regen", f.Hero).Outcome == Outcome::Added);
    f.Run(60);
    std::array<G::HitRecord, 8> records{};
    CHECK(f.Combat().Recent(f.Hero, true, true, 10, records) == 0 && f.HP(f.Hero) == 100);
    CHECK(rows[0].GetComponent<TextComponent>().Text.empty());
    // Ten burning actors tick every 3 steps; the bolt's hit (at 4.45 m) is followed by ten tick records at once.
    std::vector<Entity> burning;
    for (int i = 0; i < 10; ++i)
    {
        burning.push_back(f.Actor(500 + uint64_t(i), { float(i), 6, 0 }, 2));
        CHECK(f.Combat().ApplyEffect(burning.back(), "Burn", f.Hero).Outcome == Outcome::Added);
    }
    f.Fire("Bolt");
    f.Run(30);
    CHECK(f.HP(f.Enemy) == 80 && f.HP(burning[0]) < 100);
    CHECK(f.Combat().Recent(f.Hero, true, false, .25, records) == 8 && records[0].Periodic); // ticks are newest
    CHECK(marker.GetComponent<TextComponent>().Text == "X");
    CHECK(f.Combat().Recent(f.Hero, true, false, .25, std::span(records.data(), 1), false) == 1 && !records[0].Periodic && records[0].Key == "Bolt");
}
