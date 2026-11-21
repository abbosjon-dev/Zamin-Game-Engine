#include "CombatFixture.h"
TEST(CombatWorldProjectileHitsThroughSharedRulesWithOwnershipEvents)
{
    CFixture f;
    f.Define(CFixture::Bolt());
    auto shot = f.Fire("Bolt");
    CHECK(shot.Success && shot.Handle == (0u | 1u << 8));
    CHECK(f.Combat().Projectile(shot.Handle)->Damage == 20);
    f.Run(26);
    CHECK(f.Combat().Projectile(shot.Handle) && f.HP(f.Enemy) == 100);
    f.Run(1); // contact at x = 4.45 is reached in the 27th 1/60 s step
    CHECK(!f.Combat().Projectile(shot.Handle) && f.HP(f.Enemy) == 80);
    auto owner = f.Drain(f.Hero);
    CHECK(owner.size() == 2 && owner[0].Kind == "Fired" && owner[1].Kind == "Hit");
    CHECK(owner[1].Source == 100 && owner[1].Subject == 200 && owner[1].Amount == 20 && owner[1].Key == "Bolt");
    CHECK_NEAR(owner[1].Position.x, 4.45, .17);
    auto victim = f.Drain(f.Enemy);
    CHECK(victim.size() == 1 && victim[0].Kind == "Hit");
    auto rule = f.Rules().Poll(f.Enemy);
    CHECK(rule && rule->Kind == "Damage" && rule->Item == "Bolt" && rule->Source == 100 && rule->Amount == 20);
    std::array<Gameplay::HitRecord, 4> records{};
    CHECK(f.Combat().Recent(f.Hero, true, false, 1, records) == 1);
    CHECK(records[0].Victim == 200 && records[0].Amount == 20 && !records[0].Killed);
    CHECK(f.Combat().Recent(f.Hero, false, true, 1, records) == 0);
    CHECK(f.Combat().Recent(f.Enemy, false, true, 1, records) == 1);
    CHECK(f.Combat().Stats().Hits == 1 && f.Combat().Stats().Live == 0);
}
TEST(CombatWorldAttackIsSnapshottedAtLaunchAndArmorAppliesAtImpact)
{
    CFixture f;
    std::string e;
    auto& kit = Gameplay::LoadoutWorld::For(f.World);
    CHECK(kit.Configure(f.Hero, { 10, 0, 0, 100, 0 }, e));
    CHECK(kit.Configure(f.Enemy, { 0, 100, 0, 100, 0 }, e));
    f.Define(CFixture::Bolt());
    auto shot = f.Fire("Bolt");
    CHECK(f.Combat().Projectile(shot.Handle)->Damage == 30);
    CHECK(kit.DefineItem(f.Hero, { "Sword", Gameplay::EquipSlot::Weapon, { 50, 0, 0, 0, 0 } }, e));
    CHECK(f.Rules().ChangeItem(f.Hero, "Sword", 1, false, e));
    CHECK(kit.Equip(f.Hero, "Sword", e)); // after launch: not in this shot
    f.Run(30);
    CHECK(f.HP(f.Enemy) == 85); // (20 + 10) * 100 / (100 + 100)
    // Healing payloads gain no Attack.
    auto heal = CFixture::Bolt(true, "Mend");
    heal.Kind = Gameplay::PayloadKind::Heal;
    f.Define(heal);
    auto ally = f.Actor(300, { 3, 0, 0 }, 1);
    f.Rules().Damage(ally, {}, 50, false, false, true);
    auto mend = f.Fire("Mend");
    CHECK(f.Combat().Projectile(mend.Handle)->Damage == 20);
    f.Run(30);
    CHECK(f.HP(ally) == 70 && f.HP(f.Enemy) == 85); // support stops at the ally, never the enemy
    while (auto r = f.Rules().Poll(ally))
        if (r->Kind == "Healed") CHECK(r->Source == 100 && r->Item == "Mend" && r->Amount == 20);
}
TEST(CombatWorldOwnerlessProjectileKeepsSnapshotsAndExcludesFormerOwner)
{
    CFixture f;
    std::string e;
    CHECK(Gameplay::LoadoutWorld::For(f.World).Configure(f.Hero, { 5, 0, 0, 100, 0 }, e));
    f.Define(CFixture::Bolt());
    auto ally = f.Actor(300, { 2.5f, 0, 0 }, 1);
    auto shot = f.Fire("Bolt");
    CHECK(shot.Success);
    f.World.DestroyEntity(f.Hero);
    // A new entity with the former owner's UUID, on the enemy team, stands in the path: still excluded.
    auto impostor = f.World.CreateEntityWithUUID(UUID(100), "Impostor");
    impostor.GetTransform().Translation = { 1.5f, 0, 0 };
    f.Health(impostor, 2);
    f.Run(40);
    CHECK(f.HP(impostor) == 100 && f.HP(ally) == 100); // former owner and the owner's team are skipped
    CHECK(f.HP(f.Enemy) == 75);                        // the damage snapshot kept the owner's Attack
    auto rule = f.Rules().Poll(f.Enemy);
    CHECK(rule && rule->Kind == "Damage" && rule->Source == 0); // no credit, no owner events
    CHECK(f.Drain(impostor).empty());
    auto victim = f.Drain(f.Enemy);
    CHECK(victim.size() == 1 && victim[0].Source == 0);
}
TEST(CombatWorldPierceHitsInSegmentOrderOncePerTargetThenSpends)
{
    CFixture f;
    auto spec = CFixture::Bolt();
    spec.Key = "Lance";
    spec.Speed = 60; // two dummies inside one step's segment
    spec.Pierce = 2;
    f.Define(spec);
    f.Enemy.GetTransform().Translation = { 9, 0, 0 };
    auto d3 = f.Actor(303, { 4, 0, 0 }, 2), d1 = f.Actor(301, { 2, 0, 0 }, 2), d2 = f.Actor(302, { 2.5f, 0, 0 }, 2);
    f.Fire("Lance");
    f.Run(12);
    CHECK(f.HP(d1) == 80 && f.HP(d2) == 80 && f.HP(d3) == 80 && f.HP(f.Enemy) == 100);
    auto events = f.Drain(f.Hero);
    CHECK(Count(events, "Hit") == 3 && Count(events, "Expired") == 0);
    CHECK(events[1].Subject == 301 && events[2].Subject == 302 && events[3].Subject == 303);
    CHECK(f.Combat().LiveCount() == 0);
}
TEST(CombatWorldRejectedHitConsumesShotAndDeadVictimIsPassedThrough)
{
    CFixture f;
    f.Define(CFixture::Bolt());
    auto front = f.Actor(301, { 2, 0, 0 }, 2, 100);
    f.Health(front, 2, 100, 5); // long hit grace: the second bolt is absorbed with no damage
    f.Fire("Bolt");
    f.Fire("Bolt");
    f.Run(40);
    CHECK(f.HP(front) == 80 && f.HP(f.Enemy) == 100);
    auto events = f.Drain(f.Hero);
    CHECK(Count(events, "Hit") == 2);
    CHECK(events[2].Amount == 20 && events[3].Amount == 0); // slot order within one step
    // A victim killed by an earlier slot in the same step is skipped: the next bolt flies on.
    auto weak = f.Actor(302, { 2, 1, 0 }, 2, 20);
    auto behind = f.Actor(303, { 4, 1, 0 }, 2, 100);
    f.Hero.GetTransform().Translation = { 0, 1, 0 };
    f.Fire("Bolt");
    f.Fire("Bolt");
    f.Run(40);
    CHECK(f.Rules().Dead(weak) && f.HP(behind) == 80);
    // Inactive actors are not probed.
    f.World.SetActive(behind, false);
    f.Fire("Bolt");
    f.Run(60);
    f.World.SetActive(behind, true);
    CHECK(f.HP(behind) == 80);
}
TEST(CombatWorldBox2DWallBlocksAndMuzzleInsideWallImpacts)
{
    CFixture f([](CFixture& x) { StaticWall2D(x.World, { 2.5f, 0, 0 }, { .1f, 2 }); });
    f.Define(CFixture::Bolt());
    f.Fire("Bolt");
    f.Run(30);
    CHECK(f.HP(f.Enemy) == 100);
    auto events = f.Drain(f.Hero);
    CHECK(events.size() == 2 && events[1].Kind == "Impact");
    CHECK_NEAR(events[1].Position.x, 2.4, 1e-3);
    // Launched from inside the wall: the first segment starts at the owner's aim point and hits it.
    auto shot = f.Fire("Bolt", { 1, 0, 0 }, {}, {}, { 2.5f, 0, 0 });
    CHECK(shot.Success);
    f.Run(1);
    events = f.Drain(f.Hero);
    CHECK(events.size() == 2 && events[1].Kind == "Impact" && !f.Combat().Projectile(shot.Handle));
    CHECK_NEAR(events[1].Position.x, 2.4, 1e-3);
    // Without world blocking it passes.
    auto ghost = CFixture::Bolt(true, "Ghost");
    ghost.BlockMask = 0;
    f.Define(ghost);
    f.Fire("Ghost");
    f.Run(30);
    CHECK(f.HP(f.Enemy) == 80);
}
TEST(CombatWorldWallFaceOnAStepBoundaryStillBlocks)
{
    // At 10 m/s and 60 Hz a step ends exactly on the face (x = 4.5 in 2D; sphere centre 2.5 in 3D): the hit at the
    // segment's very end counts, and the next query never starts inside the collider.
    for (bool three : { false, true })
    {
        CFixture f([&](CFixture& x) {
            // Faces exactly representable in float (4.5; 2.75 = sphere centre 2.5 + radius .25).
            if (three) StaticBox3D(x.World, { 3, 0, 0 }, { .25f, 2, 2 });
            else StaticWall2D(x.World, { 5, 0, 0 }, { .5f, 2 });
            x.Enemy.GetTransform().Translation = { 8, 0, 0 };
        });
        auto spec = CFixture::Bolt(!three);
        spec.TargetRadius = .05;
        if (three) spec.Radius = .25;
        f.Define(spec);
        f.Fire("Bolt");
        f.Run(60);
        auto events = f.Drain(f.Hero);
        CHECK(events.size() == 2 && events[1].Kind == "Impact" && f.HP(f.Enemy) == 100);
        CHECK_NEAR(events[1].Position.x, three ? 2.5 : 4.5, 2e-3);
    }
}
TEST(CombatWorldHealthActorCollidersAreActorChannelOnly)
{
    CFixture f([](CFixture& x) {
        x.Enemy.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Static;
        x.Enemy.AddComponent<BoxCollider2DComponent>().Size = { .5f, .5f };
    });
    auto spec = CFixture::Bolt();
    spec.TargetRadius = .05; // the collider face (x = 4.5) comes before the actor sphere (x = 4.85)
    f.Define(spec);
    f.Fire("Bolt");
    f.Run(40);
    auto events = f.Drain(f.Hero);
    CHECK(events.size() == 2 && events[1].Kind == "Hit" && f.HP(f.Enemy) == 80);
}
TEST(CombatWorldJoltWallBlocksAndCharacterIsHitOnceThroughActorChannel)
{
    CFixture f([](CFixture& x) {
        StaticBox3D(x.World, { 0, -.5f, 0 }, { 30, .5f, 30 });
        x.Enemy.AddComponent<CharacterController3DComponent>();
    });
    auto spec = CFixture::Bolt(false);
    spec.TargetOffset = { 0, 1, 0 }; // the sphere cast passes through the character's capsule
    spec.TargetRadius = .05;         // ... whose surface comes before the actor sphere
    f.Define(spec);
    f.Run(5);
    f.Fire("Bolt");
    f.Run(40);
    auto events = f.Drain(f.Hero);
    CHECK(events.size() == 2 && events[1].Kind == "Hit" && f.HP(f.Enemy) == 80);
    CHECK(Count(f.Drain(f.Enemy), "Hit") == 1);
    // A Jolt wall in front of the character stops the next bolt.
    f.World.OnRuntimeStop();
    StaticBox3D(f.World, { 2.5f, 1, 0 }, { .1f, 2, 2 });
    f.World.OnRuntimeStart();
    f.Health(f.Hero, 1);
    f.Health(f.Enemy, 2);
    f.Define(spec);
    f.Run(5);
    f.Fire("Bolt");
    f.Run(40);
    events = f.Drain(f.Hero);
    CHECK(events.size() == 2 && events[1].Kind == "Impact" && f.HP(f.Enemy) == 100);
    CHECK_NEAR(events[1].Position.x, 2.3, 1e-2); // sphere centre at contact: face 2.4 - radius .1
}
TEST(CombatWorldBudgetsPerFrameOwnerAndPoolAreExplicit)
{
    CFixture f;
    auto spec = CFixture::Bolt();
    spec.Lifetime = 30;
    spec.BlockMask = 0;
    f.Define(spec);
    std::vector<Entity> owners;
    for (int i = 0; i < 5; ++i) owners.push_back(f.Actor(400 + uint64_t(i), { 0, float(10 * i + 10), 0 }, 3));
    for (int i = 0; i < 20; ++i) CHECK(f.Fire("Bolt", { 0, 1, 0 }, owners[0]).Success);
    for (int i = 0; i < 12; ++i) CHECK(f.Fire("Bolt", { 0, 1, 0 }, owners[1]).Success);
    CHECK(f.Fire("Bolt", { 0, 1, 0 }, owners[1]).Reason == "Projectile spawn budget 32");
    f.Run(1);
    for (int i = 0; i < 12; ++i) CHECK(f.Fire("Bolt", { 0, 1, 0 }, owners[0]).Success);
    CHECK(f.Fire("Bolt", { 0, 1, 0 }, owners[0]).Reason == "Owner projectile limit 32");
    for (int k = 1; k < 4; ++k)
    {
        f.Run(1);
        for (int i = 0; i < 32 - (k == 1 ? 12 : 0); ++i) CHECK(f.Fire("Bolt", { 0, 1, 0 }, owners[size_t(k)]).Success);
    }
    CHECK(f.Combat().LiveCount() == 128);
    f.Run(1);
    CHECK(f.Fire("Bolt", { 0, 1, 0 }, owners[4]).Reason == "Projectile limit 128");
    CHECK(f.Fire("Missing", { 0, 1, 0 }, owners[4]).Reason == "Projectile not defined");
    CHECK(f.Fire("Bolt", { 0, 0, 0 }, owners[4]).Reason == "Projectile needs a direction or target");
    CHECK(Throws([&] { f.Fire("Bolt", { 0, 1, 0 }, owners[4], {}, { 200, 0, 0 }); }));
    Scene other;
    auto alien = other.CreateEntity();
    CHECK(Throws([&] { f.Fire("Bolt", { 0, 1, 0 }, alien); }));
}
TEST(CombatWorldLifetimeAndDistanceRoundUpToFixedSteps)
{
    CFixture f;
    for (auto [key, lifetime, distance] : { std::tuple{ "Short", .1, 0.0 }, std::tuple{ "Odd", .105, 0.0 }, std::tuple{ "Far", 5.0, 1.0 } })
    {
        auto spec = CFixture::Bolt(true, key);
        spec.Lifetime = lifetime;
        spec.MaxDistance = distance;
        f.Define(spec);
    }
    auto a = f.Fire("Short", { 0, 1, 0 }), b = f.Fire("Odd", { 0, 1, 0 }), c = f.Fire("Far", { 0, 1, 0 });
    f.Run(5);
    CHECK(f.Combat().LiveCount() == 3);
    f.Run(1); // 6 steps = 0.1 s = 1 m at 10 m/s
    CHECK(!f.Combat().Projectile(a.Handle) && f.Combat().Projectile(b.Handle) && !f.Combat().Projectile(c.Handle));
    f.Run(1);
    CHECK(!f.Combat().Projectile(b.Handle));
    auto events = f.Drain(f.Hero);
    CHECK(Count(events, "Expired") == 3);
}
TEST(CombatWorldPauseAndTimeScaleZeroFreezeProjectiles)
{
    CFixture f;
    f.Define(CFixture::Bolt());
    auto shot = f.Fire("Bolt", { 0, 1, 0 });
    f.World.SetPaused(true);
    f.Run(10);
    CHECK(f.Combat().Projectile(shot.Handle)->Age == 0);
    f.World.SetPaused(false);
    f.World.SetTimeScale(0);
    f.Run(10);
    CHECK(f.Combat().Projectile(shot.Handle)->Age == 0);
    f.World.SetTimeScale(.5f);
    f.Run(2); // half speed: one 1/60 s step
    CHECK_NEAR(f.Combat().Projectile(shot.Handle)->Age, 1.0 / 60, 1e-9);
    f.World.SetTimeScale(1);
    // A frame longer than MaxStepsPerFrame drops time like physics does.
    f.Run(1, .5f);
    CHECK_NEAR(f.Combat().Projectile(shot.Handle)->Age, 6.0 / 60, 1e-9);
    CHECK(f.Combat().Stats().DroppedTime > .4);
}
TEST(CombatWorldPlayStopStartResetsDefinitionsAndProjectiles)
{
    CFixture f;
    f.Define(CFixture::Bolt());
    f.Fire("Bolt", { 0, 1, 0 });
    f.World.OnRuntimeStop();
    f.World.OnRuntimeStart();
    CHECK(f.Combat().LiveCount() == 0 && f.Combat().DefinitionCount() == 0);
    f.Health(f.Hero, 1);
    CHECK(f.Fire("Bolt").Reason == "Projectile not defined");
    f.World.OnRuntimeStop();
    std::string error;
    CHECK(!f.Combat().DefineProjectile(CFixture::Bolt(), error) && error == "Combat definitions require Play");
    f.World.OnRuntimeStart();
}
TEST(CombatWorldDefinitionsAreSharedImmutableAndBounded)
{
    CFixture f;
    std::string error;
    auto spec = CFixture::Bolt();
    f.Define(spec);
    f.Define(spec); // identical: prefab instances may define on Start
    spec.Damage = 25;
    CHECK(!f.Combat().DefineProjectile(spec, error) && error == "Definition differs");
    spec = CFixture::Bolt();
    spec.Area = "Blast";
    CHECK(!f.Combat().DefineProjectile(spec, error));
    spec = CFixture::Bolt();
    spec.Speed = 0;
    CHECK(!f.Combat().DefineProjectile(spec, error) && !error.empty());
    for (int i = 1; i < 32; ++i) f.Define(CFixture::Bolt(true, "Bolt" + std::to_string(i)));
    CHECK(!f.Combat().DefineProjectile(CFixture::Bolt(true, "Overflow"), error) && error == "Definition limit 32");
    auto signature = Visual::Value::parse(f.Combat().Signature());
    CHECK(signature.at("Projectiles").size() == 32 && signature.at("Projectiles").at("Bolt").at(8) == 20);
    CHECK(f.Combat().Signature().find("zprefab") == std::string::npos);
}
TEST(CombatWorldVisualPrefabPoolingValidationAndBudget)
{
    CFixture f;
    auto previous = Project::GetActive();
    Project::New();
    std::filesystem::create_directories(f.Root / "Assets/Prefabs");
    CHECK(Project::SaveActive(f.Root / "Combat.zproject"));
    auto good = f.World.CreateEntity("BoltVisual");
    good.AddComponent<SpriteRendererComponent>();
    good.AddComponent<CircleRendererComponent>();
    CHECK(f.World.SavePrefab(good, f.Root / "Assets/Prefabs/Bolt.zprefab"));
    f.World.DestroyEntity(good);
    auto bad = f.World.CreateEntity("BadVisual");
    auto child = f.World.CreateEntity("Collider");
    f.World.SetParent(child, bad);
    child.AddComponent<BoxCollider2DComponent>();
    CHECK(f.World.SavePrefab(bad, f.Root / "Assets/Prefabs/Bad.zprefab"));
    f.World.DestroyEntity(bad);
    auto entities = [&] { return f.World.GetRegistry().view<TagComponent>().size(); };
    const auto before = entities();
    std::string error;
    auto spec = CFixture::Bolt();
    spec.Prefab = "Prefabs/Bad.zprefab";
    CHECK(!f.Combat().DefineProjectile(spec, error) && error.find("must not") != std::string::npos);
    CHECK(entities() == before);
    spec.Prefab = "../Outside.zprefab";
    CHECK(!f.Combat().DefineProjectile(spec, error));
    spec.Prefab = "Prefabs/Missing.zprefab";
    CHECK(!f.Combat().DefineProjectile(spec, error));
    spec.Prefab = "Prefabs/Bolt.zprefab";
    spec.Pool = 4;
    spec.BlockMask = 0;
    spec.Lifetime = 1;
    f.Define(spec);
    CHECK(f.Combat().Stats().Pooled == 1 && f.Combat().Stats().Visuals == 0 && entities() == before + 1);
    std::vector<uint32_t> shots;
    for (int i = 0; i < 6; ++i) shots.push_back(f.Fire("Bolt", { 0, 1, 0 }).Handle);
    f.Run(1);
    CHECK(f.Combat().Stats().Pooled == 4 && f.Combat().Stats().Visuals == 4 && f.Combat().LiveCount() == 6);
    std::vector<Entity> visuals;
    for (auto e : f.World.GetRegistry().view<TagsComponent>())
        if (Entity x{ e, &f.World }; x.HasTag("Projectile visual")) visuals.push_back(x);
    CHECK(visuals.size() == 4);
    size_t shown = 0;
    for (auto v : visuals)
        if (f.World.IsActiveInHierarchy(v))
        {
            ++shown;
            CHECK(!v.HasComponent<Rigidbody2DComponent>());
            CHECK_NEAR(v.GetTransform().Translation.y, 1.0 / 6, 1e-4); // the projectile's position, rotated along +Y
            CHECK_NEAR(v.GetTransform().Rotation.z, glm::half_pi<float>(), 1e-5);
        }
    CHECK(shown == 4);
    f.World.DestroyEntity(visuals[0]); // destroyed by something else: replaced from the pool budget
    f.Run(1);
    CHECK(f.Combat().Stats().Pooled == 4 && f.Combat().Stats().Visuals == 4);
    f.Run(60);
    CHECK(f.Combat().LiveCount() == 0 && f.Combat().Stats().Visuals == 0 && f.Combat().Stats().Pooled == 4);
    f.World.OnRuntimeStop(); // the pooled visuals are runtime entities
    CHECK(entities() == before);
    f.World.OnRuntimeStart();
    Project::SetActive(previous);
}
TEST(CombatWorldSaveRestoreReplacesProjectilesInFlight)
{
    CFixture f;
    std::string error;
    auto& saves = Gameplay::SaveWorld::For(f.World);
    CHECK(saves.Bind(f.Hero, "Player", error) && saves.Bind(f.Enemy, "Enemy", error));
    f.Define(CFixture::Bolt());
    auto empty = saves.Capture("V1");
    CHECK(empty.Version == 4 && empty.Combat && empty.Combat->Projectiles.empty()); // a combat scene writes v4
    const auto shot = f.Fire("Bolt");
    f.Run(10);
    CHECK(f.Combat().LiveCount() == 1);
    auto flying = saves.Capture("V1");
    CHECK(flying.Combat->Projectiles.size() == 1 && flying.Combat->Projectiles[0].Owner == "Player");
    CHECK(saves.Restore(empty, "V1").Success); // the snapshot had nothing in flight: the shot is released
    CHECK(f.Combat().LiveCount() == 0);
    f.Run(40);
    CHECK(f.HP(f.Enemy) == 100);
    CHECK(saves.Restore(flying, "V1").Success); // back in flight, in its slot with its handle
    CHECK(f.Combat().LiveCount() == 1 && f.Combat().Projectile(shot.Handle));
    f.Run(40);
    CHECK(f.HP(f.Enemy) == 80 && f.Combat().LiveCount() == 0);
    CHECK(f.Combat().DefinitionCount() == 1 && f.Fire("Bolt").Success);
}
TEST(CombatWorldBlueprintCatalogDispatchAndTypedValidation)
{
    CFixture f;
    Gameplay::Runtime r;
    for (auto kind : { "Gameplay.ProjectileDefine", "Gameplay.ProjectileFire", "Gameplay.ProjectileState", "Gameplay.ProjectileCancel", "Gameplay.CombatEvent" }) CHECK(Visual::Catalog().contains(kind));
    CHECK(Visual::Catalog().at("Gameplay.ProjectileState").Pure);
    CHECK(CCall(r, f.Hero, "Gameplay.ProjectileDefine", { { "TwoD", true } }).at("Success") == true);
    auto fire = CCall(r, f.Hero, "Gameplay.ProjectileFire", { { "Victim", uint64_t(f.Enemy.GetUUID()) } });
    CHECK(fire.at("Success") == true);
    const double handle = fire.at("Projectile").get<double>();
    auto state = CCall(r, f.Hero, "Gameplay.ProjectileState", { { "Projectile", handle } });
    CHECK(state.at("Alive") == true && state.at("Key") == "Bolt" && state.at("Live") == 1 && state.at("Capacity") == 128);
    CHECK(CCall(r, f.Hero, "Gameplay.CombatEvent").at("Kind") == "Fired");
    CHECK(CCall(r, f.Hero, "Gameplay.ProjectileCancel", { { "Projectile", handle } }).at("Success") == true);
    CHECK(CCall(r, f.Hero, "Gameplay.ProjectileCancel", { { "Projectile", handle } }).at("Success") == false);
    auto cancelled = CCall(r, f.Hero, "Gameplay.CombatEvent");
    CHECK(cancelled.at("Has") == true && cancelled.at("Kind") == "Cancelled" && cancelled.at("Source") == 100);
    CHECK(CCall(r, f.Hero, "Gameplay.CombatEvent").at("Has") == false);
    CHECK(CCall(r, f.Hero, "Gameplay.ProjectileState", { { "Projectile", handle } }).at("Alive") == false);
    CHECK(CCall(r, f.Hero, "Gameplay.ProjectileFire", { { "Victim", uint64_t(999) } }).at("Reason") == "Projectile target unavailable");
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.ProjectileDefine", { { "Kind", 3 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.ProjectileDefine", { { "Pierce", .5 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.ProjectileState", { { "Projectile", -1 } }); }));
    CHECK(Throws([&] { CCall(r, f.Hero, "Gameplay.ProjectileFire", { { "Origin", Visual::Value::array({ 1, 2 }) } }); }));
}
namespace {
    // One deterministic combat scenario: shooters, pierce, homing, gravity and a wall. All shots are fired
    // before the first frame; the result digest covers every actor's ordered combat events and health.
    uint64_t Scenario(const std::vector<float>& deltas, double seconds)
    {
        CFixture f([](CFixture& x) { StaticWall2D(x.World, { 7, 3, 0 }, { .1f, 1 }); });
        auto e1 = f.Actor(301, { 4, 0, 0 }, 2, 30), e2 = f.Actor(302, { 6, 0, 0 }, 2, 30), e3 = f.Actor(303, { 5, 3, 0 }, 2, 50);
        auto e4 = f.Actor(304, { 8, -2, 0 }, 2, 30), sentry = f.Actor(305, { 3, -3, 0 }, 2, 60);
        auto lance = CFixture::Bolt(true, "Lance");
        lance.Pierce = 2;
        lance.Speed = 14;
        lance.Lifetime = 1.5;
        auto seeker = CFixture::Bolt(true, "Seeker");
        seeker.Homing = 120;
        seeker.Lifetime = 1.5;
        auto arc = CFixture::Bolt(true, "Arc");
        arc.Gravity = { 0, -6, 0 };
        arc.Lifetime = 1.5;
        auto slow = CFixture::Bolt(true, "Slow");
        slow.Speed = 6;
        slow.Lifetime = 1.5;
        for (const auto& s : { lance, seeker, arc, slow, CFixture::Bolt(true, "Bolt") }) f.Define(s);
        f.Fire("Lance");
        f.Fire("Seeker", { 1, 0, 0 }, {}, e3);
        f.Fire("Arc", { 1, 1, 0 });
        f.Fire("Bolt", { 0, 0, 0 }, {}, e4);
        f.Fire("Bolt", { 2, 3, 0 });
        f.Fire("Slow", { 0, 0, 0 }, sentry, f.Hero);
        f.Fire("Slow", { 1, 1, 0 }, sentry);
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
        for (auto actor : { f.Hero, f.Enemy, e1, e2, e3, e4, sentry })
        {
            for (const auto& e : f.Drain(actor)) mix(std::format("{}|{}|{}|{}|{:.9g}|{:.6f},{:.6f}\n", e.Kind, e.Key, e.Source, e.Subject, e.Amount, e.Position.x, e.Position.y));
            mix(std::format("hp {:.9g}\n", f.HP(actor)));
        }
        mix(std::format("live {} hits {}\n", f.Combat().LiveCount(), f.Combat().Stats().Hits));
        CHECK(f.Combat().LiveCount() == 0 && f.Combat().Stats().Hits >= 6); // a nontrivial, fully resolved fight
        return hash;
    }
}
TEST(CombatWorldDeterministicEventLogHashAcrossFreshScenes)
{
    const auto a = Scenario({ 1.0f / 60 }, 3), b = Scenario({ 1.0f / 60 }, 3);
    CHECK(a == b);
}
TEST(CombatWorldFrameRateIndependentOutcomes)
{
    const auto reference = Scenario({ 1.0f / 60 }, 3);
    CHECK(Scenario({ 1.0f / 30 }, 3) == reference);
    CHECK(Scenario({ 1.0f / 144 }, 3) == reference);
    CHECK(Scenario({ 1.0f / 50, 1.0f / 90, 1.0f / 40, 1.0f / 120 }, 3) == reference);
}
TEST(CombatRulesDamageRequestGraceTeamCauseAndArmorOnly)
{
    CFixture f;
    f.Health(f.Enemy, 2, 100, 1);
    Gameplay::DamageRequest r;
    r.Amount = 10;
    r.StartGrace = false;
    r.Cause = "Poison";
    CHECK(f.Rules().Damage(f.Enemy, r).Applied == 10);
    CHECK(f.Rules().Damage(f.Enemy, r).Applied == 10); // no grace started
    auto event = f.Rules().Poll(f.Enemy);
    CHECK(event && event->Item == "Poison" && event->Source == 0);
    r.StartGrace = true;
    CHECK(f.Rules().Damage(f.Enemy, r).Applied == 10);
    CHECK(f.Rules().Damage(f.Enemy, r).Reason == "Invulnerable");
    f.Health(f.Enemy, 2);
    r.SourceTeam = 2; // an ownerless projectile of the victim's team
    CHECK(f.Rules().Damage(f.Enemy, r).Reason == "Same team");
    r.FriendlyFire = true;
    CHECK(f.Rules().Damage(f.Enemy, r).Applied == 10);
    r.Cause = "bad key";
    CHECK(Throws([&] { f.Rules().Damage(f.Enemy, r); }));
    std::string e;
    auto& kit = Gameplay::LoadoutWorld::For(f.World);
    CHECK(kit.Configure(f.Hero, { 40, 0, 0, 100, 0 }, e) && kit.Configure(f.Enemy, { 0, 100, 0, 100, 0 }, e));
    Gameplay::DamageRequest attack;
    attack.Source = f.Hero;
    attack.Amount = 20;
    CHECK(f.Rules().Damage(f.Enemy, attack).Applied == 30); // (20 + 40) / 2
    attack.SourceAttack = false;
    CHECK(f.Rules().Damage(f.Enemy, attack).Applied == 10); // armor only
    CHECK(Gameplay::LoadoutAttack(f.World, f.Hero) == 40 && Gameplay::LoadoutAttack(f.World, f.Enemy) == 0);
    f.Rules().Damage(f.Hero, {}, 30, false, false, true);
    CHECK(f.Rules().Heal(f.Hero, 5, false, f.Enemy, "Mend") == 5);
    Gameplay::RuleEvent last;
    while (auto x = f.Rules().Poll(f.Hero)) last = *x;
    CHECK(last.Kind == "Healed" && last.Source == 200 && last.Item == "Mend");
}
