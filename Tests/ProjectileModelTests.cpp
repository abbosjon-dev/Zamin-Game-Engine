#include "TestAssertions.h"
#include <Zamin/Gameplay/ProjectileModel.h>
#include <cmath>
#include <vector>
using namespace Zamin::Gameplay;
namespace {
    ProjectileSpec Spec(bool twoD = true)
    {
        ProjectileSpec s;
        s.Key = "Bolt";
        s.TwoD = twoD;
        return s;
    }
    ProjectileState Flying(const ProjectileSpec& spec, glm::dvec3 position, glm::dvec3 velocity)
    {
        ProjectileState s;
        s.Key = spec.Key;
        s.Owner = 1;
        s.Team = 1;
        s.Position = position;
        s.Velocity = velocity;
        s.PierceLeft = uint8_t(spec.Pierce);
        return s;
    }
}
TEST(ProjectileModelSpecValidationBounds)
{
    std::string e;
    auto s = Spec();
    CHECK(ValidateProjectileSpec(s, e));
    auto bad = [&](auto change) {
        auto copy = Spec();
        change(copy);
        return !ValidateProjectileSpec(copy, e) && !e.empty();
    };
    CHECK(bad([](ProjectileSpec& x) { x.Key = "bad/key"; }));
    CHECK(bad([](ProjectileSpec& x) { x.Effect = "no spaces"; }));
    CHECK(bad([](ProjectileSpec& x) { x.Prefab = "Bolt.png"; }));
    CHECK(bad([](ProjectileSpec& x) { x.Kind = PayloadKind(3); }));
    CHECK(bad([](ProjectileSpec& x) { x.Speed = .05; }));
    CHECK(bad([](ProjectileSpec& x) { x.Speed = 501; }));
    CHECK(bad([](ProjectileSpec& x) { x.Lifetime = .01; }));
    CHECK(bad([](ProjectileSpec& x) { x.Lifetime = 31; }));
    CHECK(bad([](ProjectileSpec& x) { x.MaxDistance = .05; }));
    CHECK(bad([](ProjectileSpec& x) { x.Radius = 0, x.TargetRadius = 0; }));
    CHECK(bad([](ProjectileSpec& x) { x.Radius = NAN; }));
    CHECK(bad([](ProjectileSpec& x) { x.Damage = 1e6 + 1; }));
    CHECK(bad([](ProjectileSpec& x) { x.Homing = 1441; }));
    CHECK(bad([](ProjectileSpec& x) { x.Gravity = { 0, -101, 0 }; }));
    CHECK(bad([](ProjectileSpec& x) { x.TargetOffset = { 80, 80, 0 }; }));
    CHECK(bad([](ProjectileSpec& x) { x.Pierce = 8; }));
    CHECK(bad([](ProjectileSpec& x) { x.Pool = 33; }));
    CHECK(bad([](ProjectileSpec& x) { x.DetonateOnExpire = true; }));
    s.MaxDistance = 0;
    s.Radius = 0;
    s.Damage = 0;
    s.Pierce = 7;
    s.Pool = 0;
    s.Prefab = "Prefabs/Bolt.zprefab";
    CHECK(ValidateProjectileSpec(s, e));
}
TEST(ProjectileModelSweptSphereCatchesFastThinTargetAt20Hz)
{
    auto spec = Spec();
    spec.Speed = 500;
    spec.Radius = 0;
    spec.TargetRadius = .05;
    auto state = Flying(spec, { 0, 0, 0 }, { 500, 0, 0 });
    const auto segment = AdvanceProjectile(spec, state, 1.0 / 20, nullptr);
    CHECK_NEAR(segment.To.x, 25, 1e-12);
    std::vector<ActorProbe> probes = { { 7, { 12.3, 0, 0 }, 2 } };
    std::array<CandidateHit, 16> out{};
    CHECK(SweepActors(spec, state, segment, probes, out) == 1);
    CHECK(out[0].ID == 7);
    CHECK_NEAR(out[0].T, (12.3 - .05) / 25, 1e-12);
    probes[0].Center.y = .06; // just outside a 0.05 radius line
    CHECK(SweepActors(spec, state, segment, probes, out) == 0);
}
TEST(ProjectileModelStartInsideIsZeroAndDegenerateSegmentIsPointTest)
{
    Segment s{ { 0, 0, 0 }, { 1, 0, 0 } };
    CHECK(SweepSphere(s, { .1, 0, 0 }, .5, false) == 0.0);
    CHECK(!SweepSphere(s, { -2, 0, 0 }, .5, false)); // behind
    CHECK(!SweepSphere(s, { 3, 0, 0 }, .5, false));  // beyond the end
    Segment point{ { 2, 0, 0 }, { 2, 0, 0 } };
    CHECK(SweepSphere(point, { 2.2, 0, 0 }, .5, false) == 0.0);
    CHECK(!SweepSphere(point, { 3, 0, 0 }, .5, false));
    CHECK_NEAR(*SweepSphere(s, { 1, 0, 0 }, .5, false), .5, 1e-12);
}
TEST(ProjectileModel2DIgnoresDepth)
{
    Segment s{ { 0, 0, 0 }, { 4, 0, 0 } };
    CHECK(SweepSphere(s, { 2, 0, 5 }, .5, true));
    CHECK(!SweepSphere(s, { 2, 0, 5 }, .5, false));
    auto spec = Spec(true);
    spec.Gravity = { 0, 0, -9.8 };
    auto state = Flying(spec, { 0, 0, 3 }, { 10, 0, 7 });
    AdvanceProjectile(spec, state, .1, nullptr);
    CHECK(state.Position.z == 3 && state.Velocity.z == 0);
}
TEST(ProjectileModelSweepOrdersByTThenIdAndTruncates)
{
    auto spec = Spec();
    auto state = Flying(spec, { 0, 0, 0 }, { 10, 0, 0 });
    Segment s{ { 0, 0, 0 }, { 10, 0, 0 } };
    std::vector<ActorProbe> probes;
    for (uint64_t id = 40; id > 20; --id) probes.push_back({ id, { double(id % 5) + 2, 0, 0 }, 2 });
    std::array<CandidateHit, 16> out{};
    CHECK(SweepActors(spec, state, s, probes, out) == 16);
    for (size_t i = 1; i < 16; ++i) CHECK(out[i - 1].T < out[i].T || (out[i - 1].T == out[i].T && out[i - 1].ID < out[i].ID));
    CHECK(out[0].ID == 25 && out[1].ID == 30); // x = 2: ids 25, 30, 35, 40 in id order
    std::array<CandidateHit, 2> small{};
    CHECK(SweepActors(spec, state, s, probes, small) == 2);
    CHECK(small[0].ID == 25 && small[1].ID == 30);
}
TEST(ProjectileModelSweepSkipsOwnerHitAndTeam)
{
    auto spec = Spec();
    auto state = Flying(spec, { 0, 0, 0 }, { 10, 0, 0 });
    Segment s{ { 0, 0, 0 }, { 10, 0, 0 } };
    std::vector<ActorProbe> probes = { { 1, { 2, 0, 0 }, 1 }, { 2, { 3, 0, 0 }, 1 }, { 3, { 4, 0, 0 }, 2 }, { 4, { 5, 0, 0 }, 0 } };
    state.Hits[0] = 3;
    state.HitCount = 1;
    std::array<CandidateHit, 16> out{};
    CHECK(SweepActors(spec, state, s, probes, out) == 1);
    CHECK(out[0].ID == 4);
    spec.FriendlyFire = true;
    CHECK(SweepActors(spec, state, s, probes, out) == 2);
    CHECK(out[0].ID == 2 && out[1].ID == 4); // the owner is never hit
    spec.Kind = PayloadKind::Heal;
    spec.FriendlyFire = false;
    CHECK(SweepActors(spec, state, s, probes, out) == 1);
    CHECK(out[0].ID == 2);
}
TEST(ProjectileModelAdmissionMatrix)
{
    for (auto kind : { PayloadKind::Damage, PayloadKind::Heal, PayloadKind::Shield })
    {
        const bool damage = kind == PayloadKind::Damage;
        CHECK(!Admits(kind, true, 1, 1, 1, 1, false));        // owner
        CHECK(Admits(kind, false, 1, 1, 1, 1, true));         // owner with AffectOwner (areas)
        CHECK(Admits(kind, false, 1, 1, 2, 1, false) == !damage); // same nonzero team
        CHECK(Admits(kind, true, 1, 1, 2, 1, false));         // friendly fire only matters for damage
        CHECK(Admits(kind, false, 1, 1, 2, 2, false) == damage); // other team
        CHECK(Admits(kind, false, 1, 0, 2, 0, false) == damage); // team 0 supports only itself
        CHECK(!Admits(kind, true, 1, 1, 0, 1, true));         // no target
    }
    CHECK(Admits(PayloadKind::Damage, false, 0, 1, 2, 2, false)); // ownerless keeps its team
    CHECK(!Admits(PayloadKind::Damage, false, 0, 1, 2, 1, false));
}
TEST(ProjectileModelHomingTurnCapAndSpeedPreserved)
{
    auto spec = Spec(true);
    spec.Homing = 90;
    auto state = Flying(spec, { 0, 0, 0 }, { 10, 0, 0 });
    const glm::dvec3 target{ 0, 100, 0 };
    AdvanceProjectile(spec, state, .1, &target);
    CHECK_NEAR(glm::length(state.Velocity), 10, 1e-9);
    CHECK_NEAR(std::atan2(state.Velocity.y, state.Velocity.x), glm::radians(9.0), 1e-9);
    for (int i = 0; i < 20; ++i) AdvanceProjectile(spec, state, .1, &target);
    CHECK_NEAR(glm::length(state.Velocity), 10, 1e-9);
    const auto toward = glm::normalize(target - state.Position);
    CHECK(glm::dot(glm::normalize(state.Velocity), toward) > .999);
    // Opposite direction in 3D turns about a deterministic axis and keeps the speed.
    auto three = Spec(false);
    three.Homing = 180;
    auto back = Flying(three, { 0, 0, 0 }, { 5, 0, 0 });
    const glm::dvec3 behind{ -50, 0, 0 };
    AdvanceProjectile(three, back, .25, &behind);
    CHECK_NEAR(glm::length(back.Velocity), 5, 1e-9);
    CHECK_NEAR(glm::degrees(std::acos(back.Velocity.x / 5)), 45, 1e-6);
    // Without a target the projectile flies straight.
    auto straight = Flying(spec, { 0, 0, 0 }, { 10, 0, 0 });
    AdvanceProjectile(spec, straight, .1, nullptr);
    CHECK(straight.Velocity == glm::dvec3(10, 0, 0));
}
TEST(ProjectileModelGravityArcIsSemiImplicitEuler)
{
    auto spec = Spec(true);
    spec.Gravity = { 0, -10, 0 };
    auto state = Flying(spec, { 0, 0, 0 }, { 10, 0, 0 });
    auto first = AdvanceProjectile(spec, state, .1, nullptr);
    CHECK(first.From == glm::dvec3(0));
    CHECK_NEAR(state.Velocity.y, -1, 1e-12);
    CHECK_NEAR(state.Position.x, 1, 1e-12);
    CHECK_NEAR(state.Position.y, -.1, 1e-12);
    AdvanceProjectile(spec, state, .1, nullptr);
    CHECK_NEAR(state.Position.y, -.3, 1e-12);
    CHECK_NEAR(state.Age, .2, 1e-12);
    CHECK_NEAR(state.Travelled, std::hypot(1, .1) + std::hypot(1, .2), 1e-12);
}
TEST(ProjectileModelMuzzleSegmentStartsAtAimPoint)
{
    auto spec = Spec(true);
    auto state = Flying(spec, { 1, 1, 0 }, { 6, 0, 0 });
    state.Muzzle = { 2, 0, 0 };
    auto segment = AdvanceProjectile(spec, state, .5, nullptr);
    CHECK(segment.From == glm::dvec3(1, 1, 0));
    CHECK(segment.To == glm::dvec3(6, 1, 0));
    CHECK(state.Muzzle == glm::dvec3(0));
    CHECK_NEAR(state.Travelled, 3, 1e-12); // travel only, not the muzzle offset
}
TEST(ProjectileModelPoolLowestFreeGenerationsAndStaleHandles)
{
    ProjectilePool pool;
    CHECK(!pool.Live(0));
    for (uint32_t i = 0; i < ProjectilePool::Capacity; ++i) CHECK(pool.Acquire() == i);
    CHECK(!pool.Acquire());
    CHECK(pool.Count() == 128);
    const uint32_t old = pool.Handle(5);
    CHECK(old == (5u | 1u << 8) && pool.Live(old));
    pool.Release(5);
    pool.Release(5); // twice is harmless
    CHECK(!pool.Live(old) && pool.Count() == 127);
    CHECK(pool.Acquire() == 5u);
    CHECK(pool.Handle(5) == (5u | 2u << 8) && !pool.Live(old) && pool.Live(pool.Handle(5)));
    CHECK(!pool.Live(200)); // slot out of range
    pool.Clear();
    CHECK(pool.Count() == 0 && pool.Generation(0) == 2);
    CHECK(pool.Place(3, ProjectilePool::MaxGeneration));
    CHECK(!pool.Place(3, 7) && !pool.Place(4, 0) && !pool.Place(128, 1));
    pool.Release(3);
    CHECK(pool.Generation(3) == 1); // wraps, never 0
}
TEST(ProjectileModelStateValidationForRestore)
{
    std::string e;
    auto spec = Spec();
    spec.Pierce = 2;
    auto s = Flying(spec, { 1, 2, 0 }, { 12, 0, 0 });
    s.Age = .5;
    s.Travelled = 6;
    s.Damage = 20;
    CHECK(ValidateProjectileState(spec, s, e));
    auto bad = [&](auto change) {
        auto copy = s;
        change(copy);
        return !ValidateProjectileState(spec, copy, e) && !e.empty();
    };
    CHECK(bad([](ProjectileState& x) { x.Key = "Other"; }));
    CHECK(bad([](ProjectileState& x) { x.Position.x = NAN; }));
    CHECK(bad([](ProjectileState& x) { x.Velocity = { 13, 0, 0 }; }));
    CHECK(bad([](ProjectileState& x) { x.Velocity.z = 1; }));
    CHECK(bad([](ProjectileState& x) { x.Age = 3; }));
    CHECK(bad([](ProjectileState& x) { x.Damage = -1; }));
    CHECK(bad([](ProjectileState& x) { x.Phase = ProjectilePhase::Detonating; }));
    CHECK(bad([](ProjectileState& x) { x.HitCount = 1; }));      // pierce bookkeeping
    CHECK(bad([](ProjectileState& x) { x.Hits[3] = 9; }));       // trailing entry
    CHECK(bad([](ProjectileState& x) { x.Hits[0] = x.Hits[1] = 9, x.HitCount = 2, x.PierceLeft = 0; }));
    s.Hits[0] = 9;
    s.HitCount = 1;
    s.PierceLeft = 1;
    CHECK(ValidateProjectileState(spec, s, e));
}
