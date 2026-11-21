#include "CombatFixture.h"
#include <Zamin/Gameplay/SaveModel.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <Zamin/Utils/FileSystem.h>
namespace G = Zamin::Gameplay;
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
