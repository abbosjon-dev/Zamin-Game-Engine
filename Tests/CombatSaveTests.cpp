#include "CombatFixture.h"
#include <Zamin/Gameplay/SaveModel.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <Zamin/Utils/FileSystem.h>
#include <limits>
namespace {
    std::filesystem::path SaveFixture(const char* name) { return std::filesystem::path(ZAMIN_SOURCE_DIR) / "Tests" / "Fixtures" / "GameSaves" / name; }
    G::EffectSpec PoisonSpec()
    {
        G::EffectSpec s;
        s.Key = "Poison";
        s.Duration = 3;
        s.Period = 1;
        s.Tick = G::TickKind::Damage;
        s.TickAmount = 2;
        s.Rule = G::StackRule::Independent;
        s.MaxStacks = 3;
        return s;
    }
}
// The golden fixtures were written by the v1/v2/v3 codec before save v4 existed (loadout-v3.json is a real
// LoadoutWorkshop phase-0 save). Every one still decodes and re-encodes to the same bytes.
TEST(GameSaveLegacyFixturesDecodeAndReencodeIdentically)
{
    const std::pair<const char*, int> fixtures[] = { { "v1.json", 1 }, { "v2.json", 2 }, { "v3.json", 3 }, { "loadout-v3.json", 3 } };
    for (const auto& [name, version] : fixtures)
    {
        const auto bytes = FileSystem::ReadTextFile(SaveFixture(name));
        CHECK(!bytes.empty());
        G::GameSave save;
        std::string error;
        CHECK(G::DecodeGameSave(bytes, save, error));
        CHECK(error.empty());
        CHECK(save.Version == version);
        CHECK(G::EncodeGameSave(save) == bytes);
    }
    G::GameSave v2, v3;
    std::string error;
    CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(SaveFixture("v2.json")), v2, error));
    CHECK(v2.Entities.size() == 3 && v2.Entities[0].QuestBook.XP == 150 && !v2.Entities[0].Loadout && !v2.Entities[2].Present);
    CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(SaveFixture("v3.json")), v3, error));
    CHECK(v3.Entities[0].Loadout && v3.Entities[0].Loadout->Energy == 62.5 && v3.Entities[0].Loadout->Equipped.at(G::EquipSlot::Body) == "Plate");
    CHECK(v3.Entities[0].Loadout->Abilities.at("Bolt").Uses == 2 && !v3.Entities[0].Loadout->Abilities.at("Mend").Unlocked);
}
// A real v3 LoadoutWorkshop save (written before save v4) restores into the real Loadout2D scene, also when the scene
// defines combat and has a live effect: the v3 -> v4 migration means no effects and no projectiles in flight.
TEST(GameSaveLegacyV3RestoresIntoCombatScene)
{
    auto prior = Project::GetActive();
    const auto root = std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects/LoadoutWorkshop";
    const auto home = std::filesystem::temp_directory_path() / ("zamin-legacy-v3-" + std::to_string(uint64_t(UUID())));
    std::filesystem::create_directories(home);
    SaveData::SetFilePath(home / "prefs.json");
    CHECK(Project::Load(root / "LoadoutWorkshop.zproject") != nullptr);
    G::GameSave save;
    std::string error;
    CHECK(G::DecodeGameSave(FileSystem::ReadTextFile(SaveFixture("loadout-v3.json")), save, error));
    for (bool combat : { false, true })
    {
        auto scene = CreateRef<Scene>();
        CHECK(SceneSerializer(scene).Deserialize(root / "Assets/Scenes/Loadout2D.zscene"));
        scene->OnRuntimeStart();
        for (int i = 0; i < 4; ++i) scene->UpdateRuntime(1.0f / 30);
        auto& saves = G::SaveWorld::For(*scene);
        auto player = saves.Find("Player");
        CHECK(player);
        if (!player)
        {
            scene->OnRuntimeStop();
            continue;
        }
        if (combat)
        {
            auto& world = G::CombatWorld::For(*scene);
            CHECK(world.DefineEffect(PoisonSpec(), error));
            G::ProjectileSpec bolt;
            bolt.Key = "Dart";
            bolt.TwoD = true;
            CHECK(world.DefineProjectile(bolt, error));
            CHECK(world.ApplyEffect(player, "Poison").Outcome == G::ApplyOutcome::Added);
            G::FireRequest shot;
            shot.Key = "Dart";
            shot.Owner = player;
            shot.Direction = { 1, 0, 0 };
            CHECK(world.Fire(shot).Success);
            CHECK(world.EffectCount() == 1 && world.LiveCount() == 1);
        }
        auto result = saves.Restore(save, "LoadoutLabV1");
        CHECK(result.Success);
        CHECK(result.Reason.empty());
        auto state = G::LoadoutWorld::For(*scene).State(player);
        CHECK(state);
        if (state)
        {
            CHECK(state->State.Equipped.at(G::EquipSlot::Weapon) == "Sword" && state->State.Equipped.at(G::EquipSlot::Body) == "Plate");
            CHECK(state->State.Energy == 65 && state->State.Abilities.at("Bolt").Uses == 1 && state->State.Abilities.at("Bolt").Remaining > 0);
        }
        CHECK(G::RuleWorld::For(*scene).Inventory(player).Count("Potion") == 1);
        if (auto* world = scene->GetRegistry().ctx().find<G::CombatWorld>()) CHECK(world->EffectCount() == 0 && world->LiveCount() == 0 && world->DefinitionCount() == (combat ? 2u : 0u));
        CHECK(!combat || scene->GetRegistry().ctx().find<G::CombatWorld>());
        scene->OnRuntimeStop();
    }
    Project::SetActive(prior);
    SaveData::SetFilePath({});
    std::error_code ec;
    std::filesystem::remove_all(home, ec);
}
namespace {
    G::GameSave V4Example()
    {
        G::GameSave save{ "SaveLab", "V4", {} };
        save.Version = 4;
        G::SavedEntity hero;
        hero.Key = "Player";
        hero.Signature = "authored";
        hero.HasActor = hero.HasHealth = true;
        hero.Health = 73;
        G::SavedEffect poison;
        poison.Key = "Poison";
        poison.Source = "uuid:850";
        poison.Team = 2;
        poison.Remaining = 1.75;
        poison.Tick = .75;
        poison.Magnitude = 2;
        poison.Sequence = 3;
        G::SavedEffect haste;
        haste.Key = "Haste";
        haste.Source = "Player";
        haste.Team = 1;
        haste.FriendlyFire = true;
        haste.Remaining = 1.0 / 3;
        haste.Sequence = 7;
        hero.Effects = { poison, haste };
        G::SavedEntity npc;
        npc.Key = "NPC";
        npc.Signature = "brain";
        npc.HasActor = npc.HasHealth = true;
        npc.Health = 30;
        save.Entities = { hero, npc };
        G::SavedCombat combat;
        combat.Signature = "{}";
        G::SavedProjectile bolt;
        bolt.Slot = 3;
        bolt.Generation = 12;
        bolt.Key = "Bolt";
        bolt.Effect = "Burn";
        bolt.Owner = "Player";
        bolt.Team = 1;
        bolt.Target = "NPC";
        bolt.Position = { 1.0 / 3, 2e-9, -7.25 };
        bolt.Velocity = { 15, 0, .1 };
        bolt.Muzzle = { 0, .5, 0 };
        bolt.Age = .25;
        bolt.Travelled = 3.75;
        bolt.Damage = 30;
        bolt.Attack = 10;
        bolt.PierceLeft = 1;
        bolt.Hits = { "NPC", "" };
        combat.Projectiles = { bolt };
        save.Combat = combat;
        return save;
    }
    bool Rejects(const G::GameSave& save)
    {
        try
        {
            G::EncodeGameSave(save);
            return false;
        }
        catch (const std::exception&)
        {
            return true;
        }
    }
    // Re-signs an edited document (FNV-1a of the payload, as the codec does) so only the edit is judged.
    std::string Reseal(Visual::Value document)
    {
        uint64_t h = 14695981039346656037ull;
        for (unsigned char c : document.at("Payload").dump())
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        document["Checksum"] = std::to_string(h);
        return document.dump();
    }
    bool Decodes(const std::string& bytes)
    {
        G::GameSave out;
        std::string error;
        return G::DecodeGameSave(bytes, out, error);
    }
    // The combat scene used by the save tests: Hero/Enemy bound, an unbound Dummy behind the Enemy.
    void Definitions(CFixture& f, double poison = 3)
    {
        auto bolt = CFixture::Bolt();
        bolt.Speed = 6;
        bolt.Pierce = 1;
        bolt.Effect = "Burn";
        f.Define(bolt);
        Define(f, Dot("Burn", 2, .5, 3, G::StackRule::Refresh, 2));
        Define(f, Dot("Poison", poison, 1, 2, G::StackRule::Independent, 3));
        Define(f, Buff("Haste", 30, { 0, 0, 0, 50, 0 }));
    }
    void CombatScene(CFixture& f, double poison = 3)
    {
        std::string e;
        Definitions(f, poison);
        f.Actor(300, { 8, 0, 0 }, 2);
        auto& saves = G::SaveWorld::For(f.World);
        CHECK(saves.Bind(f.Hero, "Player", e) && saves.Bind(f.Enemy, "Enemy", e));
    }
    struct EffectRow
    {
        std::string Key;
        uint64_t Source = 0;
        uint32_t Stacks = 0;
        double Remaining = 0, Tick = 0, Magnitude = 0;
        uint64_t Sequence = 0;
        bool operator==(const EffectRow&) const = default;
    };
    std::vector<EffectRow> Rows(CFixture& f, Entity e)
    {
        std::array<G::EffectInstance, 16> buffer;
        const size_t n = f.Combat().Effects(e, buffer);
        std::vector<EffectRow> out;
        for (size_t i = 0; i < n; ++i) out.push_back({ buffer[i].Key, buffer[i].Source, buffer[i].Stacks, buffer[i].Remaining, buffer[i].UntilTick, buffer[i].Magnitude, buffer[i].Sequence });
        return out;
    }
}
TEST(GameSaveModelV4RoundtripKeepsEffectsAndProjectilesExactly)
{
    const auto save = V4Example();
    const auto bytes = G::EncodeGameSave(save);
    G::GameSave copy;
    std::string error;
    CHECK(G::DecodeGameSave(bytes, copy, error));
    CHECK(copy.Version == 4 && copy.Combat && copy.Combat->Signature == "{}");
    CHECK(G::EncodeGameSave(copy) == bytes);
    const auto& effects = copy.Entities[0].Effects;
    CHECK(effects.size() == 2 && effects[0].Key == "Poison" && effects[0].Source == "uuid:850" && effects[0].Team == 2 && effects[0].Tick == .75);
    CHECK(effects[1].FriendlyFire && effects[1].Remaining == 1.0 / 3 && effects[1].Sequence == 7 && copy.Entities[1].Effects.empty());
    const auto& p = copy.Combat->Projectiles.at(0);
    CHECK(p.Slot == 3 && p.Generation == 12 && p.Key == "Bolt" && p.Effect == "Burn" && p.Owner == "Player" && p.Target == "NPC");
    CHECK(p.Position == glm::dvec3(1.0 / 3, 2e-9, -7.25) && p.Velocity == glm::dvec3(15, 0, .1) && p.Muzzle == glm::dvec3(0, .5, 0)); // doubles are exact
    CHECK(p.Age == .25 && p.Travelled == 3.75 && p.Damage == 30 && p.Attack == 10 && p.PierceLeft == 1 && p.Phase == 0);
    CHECK(p.Hits == std::vector<std::string>({ "NPC", "" }));
    // Older versions never carry combat state.
    auto legacy = V4Example();
    legacy.Version = 3;
    CHECK(Rejects(legacy));
    legacy.Combat.reset();
    CHECK(Rejects(legacy)); // still has effects
    for (auto& e : legacy.Entities) e.Effects.clear();
    CHECK(!Rejects(legacy));
    auto missing = V4Example();
    missing.Combat.reset();
    CHECK(Rejects(missing));
}
TEST(GameSaveModelV4StrictFieldsAndBoundsRejectWholeSnapshot)
{
    using Edit = std::function<void(G::GameSave&)>;
    const std::vector<Edit> edits = {
        [](auto& s) { s.Entities[0].Effects[0].Remaining = 0; },
        [](auto& s) { s.Entities[0].Effects[0].Remaining = std::numeric_limits<double>::quiet_NaN(); },
        [](auto& s) { s.Entities[0].Effects[0].Remaining = 9601; },
        [](auto& s) { s.Entities[0].Effects[0].Tick = 61; },
        [](auto& s) { s.Entities[0].Effects[0].Magnitude = 100001; },
        [](auto& s) { s.Entities[0].Effects[0].Stacks = 0; },
        [](auto& s) { s.Entities[0].Effects[0].Stacks = 17; },
        [](auto& s) { s.Entities[0].Effects[0].Team = 65536; },
        [](auto& s) { s.Entities[0].Effects[0].Sequence = 7; }, // duplicate sequence
        [](auto& s) { s.Entities[0].Effects[1].Sequence = 2; }, // out of application order
        [](auto& s) { s.Entities[0].Effects[0].Sequence = 0; },
        [](auto& s) { s.Entities[0].Effects[0].Key = "bad key"; },
        [](auto& s) { s.Entities[0].Effects[0].Source = "uuid:0"; },
        [](auto& s) { s.Entities[0].Effects[0].Source = "uuid:12a"; },
        [](auto& s) { s.Entities[0].Effects[0].Source = "uuid:0123"; },
        [](auto& s) { s.Entities[0].Effects[0].Source = "uuid:18446744073709551616"; },
        [](auto& s) { s.Entities[0].Effects[0].Source = "../x"; },
        [](auto& s) { s.Entities[0].Effects.resize(17, s.Entities[0].Effects[0]); },
        [](auto& s) { s.Entities[0].Dead = true, s.Entities[0].Health = 0; }, // death clears effects
        [](auto& s) { s.Entities[0].HasActor = s.Entities[0].HasHealth = false; },
        [](auto& s) { s.Combat->Projectiles[0].Slot = 128; },
        [](auto& s) { s.Combat->Projectiles.push_back(s.Combat->Projectiles[0]); }, // duplicate slot
        [](auto& s) { s.Combat->Projectiles[0].Generation = 0; },
        [](auto& s) { s.Combat->Projectiles[0].Generation = 0x1000000; },
        [](auto& s) { s.Combat->Projectiles[0].Phase = 2; },
        [](auto& s) { s.Combat->Projectiles[0].PierceLeft = 8; },
        [](auto& s) { s.Combat->Projectiles[0].Hits = std::vector<std::string>(9, ""); },
        [](auto& s) { s.Combat->Projectiles[0].Hits = { "NPC", "NPC" }; },
        [](auto& s) { s.Combat->Projectiles[0].Position.y = std::numeric_limits<double>::infinity(); },
        [](auto& s) { s.Combat->Projectiles[0].Position.x = 2e9; },
        [](auto& s) { s.Combat->Projectiles[0].Muzzle.z = 101; },
        [](auto& s) { s.Combat->Projectiles[0].Age = 32; },
        [](auto& s) { s.Combat->Projectiles[0].Damage = 1000001; },
        [](auto& s) { s.Combat->Projectiles[0].Key.clear(); },
        [](auto& s) { s.Combat->Projectiles[0].Effect = "a/b"; },
        [](auto& s) { s.Combat->Projectiles[0].Owner = "uuid:"; },
        [](auto& s) { s.Combat->Signature = std::string(65537, 'x'); },
        [](auto& s) {
            auto p = s.Combat->Projectiles[0];
            s.Combat->Projectiles.clear();
            for (uint32_t i = 0; i < 129; ++i) s.Combat->Projectiles.push_back(p), s.Combat->Projectiles.back().Slot = i % 128;
        },
    };
    CHECK(!Rejects(V4Example()));
    size_t rejected = 0;
    for (const auto& edit : edits)
    {
        auto save = V4Example();
        edit(save);
        if (Rejects(save)) ++rejected;
    }
    CHECK(rejected == edits.size());
    // Field sets are exact: an unknown, missing or mistyped field rejects the document.
    const auto document = Visual::Value::parse(G::EncodeGameSave(V4Example()));
    CHECK(Decodes(Reseal(document)));
    using JsonEdit = std::function<void(Visual::Value&)>;
    const std::vector<JsonEdit> json = {
        [](auto& d) { d["Payload"]["Entities"][0]["Effects"][0]["Extra"] = 1; },
        [](auto& d) { d["Payload"]["Entities"][0]["Effects"][0].erase("Tick"); },
        [](auto& d) { d["Payload"]["Entities"][0]["Effects"][0]["Sequence"] = 3.5; },
        [](auto& d) { d["Payload"]["Entities"][0]["Effects"][0]["Sequence"] = -3; },
        [](auto& d) { d["Payload"]["Entities"][0]["Effects"][0]["FriendlyFire"] = 1; },
        [](auto& d) { d["Payload"]["Entities"][0].erase("Effects"); },
        [](auto& d) { d["Payload"]["Combat"]["Extra"] = 0; },
        [](auto& d) { d["Payload"]["Combat"]["Projectiles"] = Visual::Value::object(); },
        [](auto& d) { d["Payload"]["Combat"]["Projectiles"][0].erase("Hits"); },
        [](auto& d) { d["Payload"]["Combat"]["Projectiles"][0]["Position"] = Visual::Value::array({ 0, 0 }); },
        [](auto& d) { d["Payload"].erase("Combat"); },
        [](auto& d) { d["Version"] = 3; },
    };
    size_t refused = 0;
    for (const auto& edit : json)
    {
        auto copy = document;
        edit(copy);
        if (!Decodes(Reseal(copy))) ++refused;
    }
    CHECK(refused == json.size());
}
TEST(GameSaveModelMaximalV4DocumentFitsTwoMiB)
{
    auto key = [](const std::string& prefix, size_t n) {
        auto s = prefix + std::to_string(n);
        return s + std::string(64 - s.size(), 'x');
    };
    G::GameSave save{ "SaveLab", "V4", {} };
    save.Version = 4;
    for (size_t i = 0; i < 256; ++i)
    {
        G::SavedEntity e;
        e.Key = key("E", i);
        e.Signature = "s";
        e.HasActor = e.HasHealth = true;
        e.Health = 123456.789012345;
        for (uint64_t k = 0; k < 16; ++k)
        {
            G::SavedEffect x;
            x.Key = key("Effect", k);
            x.Source = "uuid:18446744073709551615";
            x.Team = 65535;
            x.FriendlyFire = true;
            x.Stacks = 16;
            x.Remaining = 9599.123456789012;
            x.Tick = 59.123456789012345;
            x.Magnitude = 99999.12345678901;
            x.Sequence = (uint64_t(1) << 53) - 16 + k;
            e.Effects.push_back(x);
        }
        save.Entities.push_back(std::move(e));
    }
    G::SavedCombat combat;
    combat.Signature = std::string(65536, 's');
    for (uint32_t slot = 0; slot < 128; ++slot)
    {
        G::SavedProjectile p;
        p.Slot = slot;
        p.Generation = 0xFFFFFF;
        p.Key = key("Projectile", slot);
        p.Effect = key("Effect", slot);
        p.Owner = key("E", slot);
        p.Target = key("E", slot + 1);
        p.Team = 65535;
        p.Position = { -987654321.12345678, 987654321.12345678, -123456789.87654321 };
        p.Velocity = { -5196.123456789012, 5196.123456789012, -5196.123456789012 };
        p.Muzzle = { -57.12345678901234, 57.12345678901234, -57.12345678901234 };
        p.Age = 30.049999999999997;
        p.Travelled = 156123.12345678901;
        p.Damage = p.Attack = 999999.1234567891;
        p.PierceLeft = 7;
        p.Phase = 1;
        for (size_t h = 0; h < 8; ++h) p.Hits.push_back(key("E", 100 + h));
        combat.Projectiles.push_back(std::move(p));
    }
    save.Combat = std::move(combat);
    std::string bytes;
    CHECK(!Throws([&] { bytes = G::EncodeGameSave(save); }));
    std::printf("    v4 eng katta hujjat: %zu bayt (chegara %d)\n", bytes.size(), 2 * 1024 * 1024);
    CHECK(!bytes.empty() && bytes.size() <= 2 * 1024 * 1024);
    G::GameSave copy;
    std::string error;
    CHECK(G::DecodeGameSave(bytes, copy, error) && copy.Entities.size() == 256 && copy.Combat->Projectiles.size() == 128);
}
// Save, keep playing, then restore into a fresh scene and play the same number of fixed steps: the projectile in
// flight, both books of effects and every health value continue exactly as in the original run.
TEST(CombatSaveV4ContinuesExactlyInAFreshScene)
{
    G::GameSave save;
    uint32_t handle = 0;
    std::vector<EffectRow> heroAfter, enemyAfter, dummyAfter;
    std::optional<G::ProjectileState> midway;
    double hero = 0, enemy = 0, dummy = 0;
    {
        CFixture f;
        CombatScene(f);
        handle = f.Fire("Bolt").Handle;
        CHECK(handle != 0);
        CHECK(f.Combat().ApplyEffect(f.Hero, "Poison", f.Enemy).Outcome == Outcome::Added);
        f.Run(30);
        CHECK(f.Combat().ApplyEffect(f.Hero, "Poison", f.Enemy).Outcome == Outcome::Added); // a second, later instance
        f.Run(1);
        save = G::SaveWorld::For(f.World).Capture("Combat");
        CHECK(save.Version == 4 && save.Combat->Projectiles.size() == 1 && save.Combat->Projectiles[0].Owner == "Player");
        f.Run(15);
        midway = f.Combat().Projectile(handle);
        f.Run(150);
        heroAfter = Rows(f, f.Hero);
        enemyAfter = Rows(f, f.Enemy);
        auto d = f.World.GetEntityByUUID(UUID(300));
        dummyAfter = Rows(f, d);
        hero = f.HP(f.Hero), enemy = f.HP(f.Enemy), dummy = f.HP(d);
        CHECK(enemy < 100 && dummy < 100 && hero < 100 && f.Combat().LiveCount() == 0); // the bolt pierced both
    }
    CFixture g;
    CombatScene(g);
    auto restored = G::SaveWorld::For(g.World).Restore(save, "Combat");
    CHECK(restored.Success && restored.Reason.empty());
    CHECK(g.Combat().LiveCount() == 1 && g.Combat().Projectile(handle)); // same slot and generation: the handle is live
    CHECK(Rows(g, g.Hero).size() == 2 && Rows(g, g.Hero)[0].Source == uint64_t(g.Enemy.GetUUID()));
    g.Run(15);
    auto now = g.Combat().Projectile(handle);
    CHECK(midway && now && now->Position == midway->Position && now->Velocity == midway->Velocity && now->Age == midway->Age && now->HitCount == midway->HitCount);
    g.Run(150);
    auto d = g.World.GetEntityByUUID(UUID(300));
    CHECK(g.HP(g.Hero) == hero && g.HP(g.Enemy) == enemy && g.HP(d) == dummy);
    CHECK(Rows(g, g.Hero) == heroAfter && Rows(g, g.Enemy) == enemyAfter && Rows(g, d) == dummyAfter);
    CHECK(g.Combat().LiveCount() == 0);
}
TEST(CombatSaveUnresolvedSourcesAndOwnersBecomeOwnerless)
{
    CFixture f;
    CombatScene(f);
    auto turret = f.World.CreateEntity("Turret"); // runtime, unbound: referenced by UUID
    turret.GetTransform().Translation = { -6, 0, 0 };
    f.Health(turret, 2);
    auto dart = CFixture::Bolt(true, "Dart");
    dart.Speed = 4;
    f.Define(dart);
    const auto shot = f.Fire("Dart", { 1, 0, 0 }, turret);
    CHECK(shot.Success);
    CHECK(f.Combat().ApplyEffect(f.Hero, "Poison", turret).Outcome == Outcome::Added);
    f.Run(6);
    auto& saves = G::SaveWorld::For(f.World);
    auto save = saves.Capture("Combat");
    const auto reference = "uuid:" + std::to_string(uint64_t(turret.GetUUID()));
    CHECK(save.Combat->Projectiles.size() == 1 && save.Combat->Projectiles[0].Owner == reference);
    for (const auto& s : save.Entities)
        if (s.Key == "Player") CHECK(s.Effects.size() == 1 && s.Effects[0].Source == reference);
    CHECK(saves.Restore(save, "Combat").Success); // still there: the same session resolves the UUID
    CHECK(f.Combat().Projectile(shot.Handle)->Owner == uint64_t(turret.GetUUID()) && Rows(f, f.Hero).at(0).Source == uint64_t(turret.GetUUID()));
    f.World.DestroyEntity(turret);
    f.Run(1);
    auto gone = saves.Capture("Combat");
    CHECK(gone.Combat->Projectiles[0].Owner.empty()); // a gone owner is saved as none
    CHECK(saves.Restore(save, "Combat").Success);     // the older snapshot names a UUID nothing has now
    CHECK(f.Combat().Projectile(shot.Handle)->Owner == 0 && Rows(f, f.Hero).at(0).Source == 0);
    RuleEvents(f, f.Hero);
    f.Run(120);
    // Ownerless: the dart keeps its team snapshot and damage; the poison's two remaining ticks in 2 s still hurt,
    // with no credit.
    CHECK(f.HP(f.Hero) == 100 - 20 - 2 * 2 && f.Combat().LiveCount() == 0);
    for (const auto& event : RuleEvents(f, f.Hero))
        if (event.Kind == "Damage") CHECK(event.Source == 0);
}
TEST(CombatSaveEffectsAreStagedBeforeLoadoutEnergyValidation)
{
    CFixture f;
    CombatScene(f);
    std::string e;
    auto& kit = G::LoadoutWorld::For(f.World);
    CHECK(kit.Configure(f.Hero, { 0, 0, 0, 100, 0 }, e));
    CHECK(f.Combat().ApplyEffect(f.Hero, "Haste").Outcome == Outcome::Added && kit.Refill(f.Hero, 50) == 50);
    auto& saves = G::SaveWorld::For(f.World);
    auto save = saves.Capture("Combat");
    auto forged = save;
    for (auto& s : forged.Entities)
        if (s.Key == "Player") s.Effects.clear(); // 150 energy without the MaxEnergy bonus that allows it
    f.Hero.GetTransform().Translation = { 1, 2, 0 };
    CHECK(kit.Refill(f.Hero, 0) == 0 && f.Combat().RemoveEffects(f.Hero, "Haste") == 1 && kit.State(f.Hero)->State.Energy == 100);
    auto result = saves.Restore(forged, "Combat");
    CHECK(!result.Success && result.Reason == "Invalid saved loadout");
    CHECK(f.Hero.GetTransform().Translation.x == 1 && kit.State(f.Hero)->State.Energy == 100 && f.Combat().EffectCount() == 0);
    CHECK(saves.Restore(save, "Combat").Success);
    CHECK(kit.State(f.Hero)->State.Energy == 150 && kit.State(f.Hero)->Stats[3] == 150 && f.Combat().Effect(f.Hero, "Haste").Active);
}
TEST(CombatSaveChangedDefinitionsAndInvalidStateRejectWithoutMutation)
{
    CFixture f;
    CombatScene(f);
    auto& saves = G::SaveWorld::For(f.World);
    CHECK(f.Combat().ApplyEffect(f.Hero, "Poison", f.Enemy).Outcome == Outcome::Added);
    f.Fire("Bolt");
    f.Fire("Bolt", { 0, 1, 0 });
    f.Run(10);
    auto save = saves.Capture("Combat");
    CHECK(save.Combat->Projectiles.size() == 2);
    f.Run(20);
    const auto heroHP = f.HP(f.Hero);
    const auto effects = Rows(f, f.Hero);
    const uint32_t handle = save.Combat->Projectiles[0].Slot | save.Combat->Projectiles[0].Generation << 8;
    const auto first = f.Combat().Projectile(handle);
    auto unchanged = [&] {
        auto now = f.Combat().Projectile(handle);
        return f.HP(f.Hero) == heroHP && Rows(f, f.Hero) == effects && f.Combat().LiveCount() == 2 && first && now && now->Position == first->Position;
    };
    using Edit = std::function<void(G::GameSave&)>;
    const std::vector<std::pair<Edit, std::string>> forged = {
        { [](auto& s) { s.Combat->Projectiles.back().Age = 5; }, "Invalid saved projectile" }, // older than its lifetime
        { [](auto& s) { s.Combat->Projectiles.back().Key = "Missing"; }, "Saved projectile not defined" },
        { [](auto& s) { s.Combat->Projectiles.back().Effect = "Haste"; }, "Saved projectile effect kind mismatch" },
        { [](auto& s) { s.Combat->Projectiles.back().PierceLeft = 0; }, "Invalid saved projectile" }, // hits + left != pierce
        { [](auto& s) { for (auto& e : s.Entities) if (e.Key == "Player") e.Effects[0].Remaining = 4; }, "Invalid saved effects" },
        { [](auto& s) { for (auto& e : s.Entities) if (e.Key == "Player") e.Effects[0].Key = "Unknown"; }, "Invalid saved effects" },
        { [](auto& s) { s.Combat->Signature = "{}"; }, "Combat definitions changed" },
    };
    for (const auto& [edit, reason] : forged)
    {
        auto copy = save;
        edit(copy);
        auto result = saves.Restore(copy, "Combat");
        CHECK(!result.Success && result.Reason.starts_with(reason));
        CHECK(unchanged());
    }
    Define(f, Effect("Extra"));                                                          // a definition added later
    CHECK(saves.Restore(save, "Combat").Reason == "Combat definitions changed");
    CHECK(unchanged());
    CFixture other;
    CombatScene(other, 4); // a longer poison
    CHECK(G::SaveWorld::For(other.World).Restore(save, "Combat").Reason == "Combat definitions changed" && other.Combat().LiveCount() == 0);
}
TEST(CombatSaveLegacyVersionMigratesBoundActorsToNoEffectsOrProjectiles)
{
    CFixture f;
    std::string e;
    auto& saves = G::SaveWorld::For(f.World);
    CHECK(saves.Bind(f.Hero, "Player", e) && saves.Bind(f.Enemy, "Enemy", e));
    auto dummy = f.Actor(300, { 8, 0, 0 }, 2);
    auto legacy = saves.Capture("Combat"); // no combat yet: v1
    CHECK(legacy.Version == 1 && !legacy.Combat);
    Definitions(f);
    CHECK(f.Combat().ApplyEffect(f.Enemy, "Poison", f.Hero).Outcome == Outcome::Added);
    CHECK(f.Combat().ApplyEffect(dummy, "Poison", f.Hero).Outcome == Outcome::Added);
    f.Fire("Bolt", { 0, 1, 0 });
    f.Run(3);
    auto result = saves.Restore(legacy, "Combat");
    CHECK(result.Success);
    // Bound actors take the snapshot (no effects existed then); the unbound dummy keeps its runtime state.
    CHECK(Rows(f, f.Enemy).empty() && Rows(f, dummy).size() == 1 && f.Combat().LiveCount() == 0 && f.Combat().DefinitionCount() == 4);
}
TEST(CombatSaveUnlinkedAbilitySignaturesStayByteIdentical)
{
    CFixture f;
    std::string e;
    auto& saves = G::SaveWorld::For(f.World);
    auto& kit = G::LoadoutWorld::For(f.World);
    CHECK(kit.Configure(f.Hero, G::DefaultCombatStats, e) && saves.Bind(f.Hero, "Player", e));
    CHECK(kit.DefineAbility(f.Hero, Ability("Zap", 10, 5, 1), e));
    auto plain = saves.Capture("Combat");
    CHECK(plain.Version == 3);
    auto loadout = Visual::Value::parse(plain.Entities[0].Signature).at("Loadout");
    CHECK(loadout.at(2).at("Zap").size() == 8); // the v3 layout, unchanged
    auto linked = Ability("Spit", 6, 0, 2);
    linked.Projectile = "Dart";
    CHECK(kit.DefineAbility(f.Hero, linked, e));
    auto save = saves.Capture("Combat");
    auto abilities = Visual::Value::parse(save.Entities[0].Signature).at("Loadout").at(2);
    CHECK(abilities.at("Zap").size() == 8 && abilities.at("Spit").size() == 11 && abilities.at("Spit")[8] == "Dart" && abilities.at("Spit")[10] == "");
    CHECK(save.Version == 3); // links alone do not make a combat save: no combat definitions exist yet
}
