#include "ProjectileModel.h"
#include "RulesModel.h"
#include <algorithm>
#include <cmath>
namespace Zamin::Gameplay {
    namespace {
        bool Range(double v, double low, double high) { return std::isfinite(v) && v >= low && v <= high; }
        bool Finite(const glm::dvec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        bool Components(const glm::dvec3& v, double limit) { return Finite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit; }
        bool Before(const CandidateHit& a, const CandidateHit& b) { return a.T < b.T || (a.T == b.T && a.ID < b.ID); }
        bool Fail(std::string& error, const char* message)
        {
            error = message;
            return false;
        }
        // Rotates unit vector a toward unit vector b by `angle` radians around their common normal.
        glm::dvec3 Turn(const glm::dvec3& a, const glm::dvec3& b, double angle, bool twoD)
        {
            glm::dvec3 axis = glm::cross(a, b);
            const double length = glm::length(axis);
            if (length > 1e-12) axis /= length;
            else if (twoD) axis = { 0, 0, 1 };
            else axis = glm::normalize(glm::cross(a, std::abs(a.y) < .9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0)));
            const double c = std::cos(angle), s = std::sin(angle);
            return a * c + glm::cross(axis, a) * s + axis * glm::dot(axis, a) * (1 - c);
        }
    }
    bool ProjectileState::HasHit(uint64_t id) const
    {
        for (size_t i = 0; i < std::min<size_t>(HitCount, MaxHits); ++i)
            if (Hits[i] == id) return true;
        return false;
    }
    bool ValidateProjectileSpec(const ProjectileSpec& s, std::string& error)
    {
        error.clear();
        if (!ValidItem(s.Key)) return Fail(error, "Projectile key must be 1..64 letters, digits, _ . -");
        if ((!s.Effect.empty() && !ValidItem(s.Effect)) || (!s.Area.empty() && !ValidItem(s.Area))) return Fail(error, "Invalid projectile effect/area key");
        if (s.Prefab.size() > 260 || s.Prefab.find('\0') != std::string::npos || (!s.Prefab.empty() && !s.Prefab.ends_with(".zprefab")))
            return Fail(error, "Projectile prefab must be a .zprefab path of at most 260 characters");
        if (int(s.Kind) < 0 || int(s.Kind) > 2) return Fail(error, "Projectile kind must be Damage, Heal or Shield");
        if (!Range(s.Speed, .1, 500) || !Range(s.Lifetime, .02, 30) || !(s.MaxDistance == 0 || Range(s.MaxDistance, .1, 1000)))
            return Fail(error, "Projectile speed 0.1..500, lifetime 0.02..30 s, max distance 0 or 0.1..1000");
        if (!Range(s.Radius, 0, 5) || !Range(s.TargetRadius, 0, 10) || s.Radius + s.TargetRadius <= 0)
            return Fail(error, "Projectile radius 0..5 and target radius 0..10, not both zero");
        if (!Range(s.Damage, 0, 1e6) || !Range(s.Homing, 0, 1440)) return Fail(error, "Projectile damage 0..1000000, homing 0..1440 deg/s");
        if (!Components(s.Gravity, 100) || !Finite(s.TargetOffset) || glm::length(s.TargetOffset) > 100) return Fail(error, "Projectile gravity components and target offset must be within 100");
        if (s.Pierce < 0 || s.Pierce > 7 || s.Pool < 0 || s.Pool > 32) return Fail(error, "Projectile pierce 0..7, pool 0..32");
        if (s.DetonateOnExpire && s.Area.empty()) return Fail(error, "DetonateOnExpire requires an area");
        return true;
    }
    bool ValidateProjectileState(const ProjectileSpec& spec, const ProjectileState& s, std::string& error)
    {
        error.clear();
        // The longest fixed step is 1/20 s; lifetime rounds up to the next step.
        const double maximumAge = spec.Lifetime + .05 + 1e-9;
        const double maximumSpeed = spec.Speed + glm::length(spec.Gravity) * maximumAge + 1e-6;
        if (s.Key != spec.Key) return Fail(error, "Projectile state key differs from its definition");
        if (!Components(s.Position, 1e9) || !Finite(s.Velocity) || glm::length(s.Velocity) > maximumSpeed || !Finite(s.Muzzle) || glm::length(s.Muzzle) > 100)
            return Fail(error, "Projectile position/velocity/muzzle out of range");
        if (spec.TwoD && (s.Velocity.z != 0 || s.Muzzle.z != 0)) return Fail(error, "2D projectile has depth motion");
        if (!Range(s.Age, 0, maximumAge) || !Range(s.Travelled, 0, maximumSpeed * maximumAge) || !Range(s.Damage, 0, 1e6))
            return Fail(error, "Projectile age/distance/damage out of range");
        if (s.Phase != ProjectilePhase::Flying && s.Phase != ProjectilePhase::Detonating) return Fail(error, "Invalid projectile phase");
        if (s.Phase == ProjectilePhase::Detonating && spec.Area.empty()) return Fail(error, "Detonating projectile has no area");
        const int hits = s.HitCount, left = s.PierceLeft;
        const bool counted = s.Phase == ProjectilePhase::Flying ? hits + left == spec.Pierce : (hits + left == spec.Pierce || hits + left == spec.Pierce + 1);
        if (hits > std::min(int(ProjectileState::MaxHits), spec.Pierce + 1) || left > spec.Pierce || !counted) return Fail(error, "Projectile hit/pierce count inconsistent");
        for (size_t i = 0; i < ProjectileState::MaxHits; ++i)
        {
            if (i >= size_t(hits))
            {
                if (s.Hits[i] != 0) return Fail(error, "Projectile hit list has trailing entries");
                continue;
            }
            for (size_t j = 0; j < i; ++j)
                if (s.Hits[i] == s.Hits[j] && s.Hits[i] != 0) return Fail(error, "Projectile hit list has duplicates");
        }
        return true;
    }
    Segment AdvanceProjectile(const ProjectileSpec& spec, ProjectileState& state, double h, const glm::dvec3* homingTarget)
    {
        Segment segment;
        segment.From = state.Position;
        if (state.Muzzle != glm::dvec3(0))
        {
            // The launch point is reached through the owner's aim point: a muzzle inside or behind a wall hits it.
            state.Position += state.Muzzle;
            state.Muzzle = {};
        }
        auto velocity = state.Velocity;
        if (spec.TwoD) velocity.z = 0;
        if (homingTarget && spec.Homing > 0)
        {
            auto desired = *homingTarget - state.Position;
            if (spec.TwoD) desired.z = 0;
            const double speed = glm::length(velocity), distance = glm::length(desired);
            if (speed > 1e-12 && distance > 1e-9)
            {
                const auto current = velocity / speed, wanted = desired / distance;
                const double angle = std::acos(std::clamp(glm::dot(current, wanted), -1.0, 1.0));
                const double turn = glm::radians(spec.Homing) * h;
                velocity = (angle <= turn ? wanted : glm::normalize(Turn(current, wanted, turn, spec.TwoD))) * speed;
            }
        }
        auto gravity = spec.Gravity;
        if (spec.TwoD) gravity.z = 0;
        velocity += gravity * h;
        if (spec.TwoD) velocity.z = 0;
        const auto step = velocity * h;
        state.Velocity = velocity;
        state.Position += step;
        state.Age += h;
        state.Travelled += glm::length(step);
        segment.To = state.Position;
        return segment;
    }
    bool Admits(PayloadKind kind, bool friendlyFire, uint64_t owner, uint16_t ownerTeam, uint64_t target, uint16_t targetTeam, bool affectOwner)
    {
        if (target == 0) return false;
        if (owner != 0 && target == owner) return affectOwner;
        const bool sameTeam = ownerTeam != 0 && ownerTeam == targetTeam;
        if (kind == PayloadKind::Damage) return !sameTeam || friendlyFire;
        return sameTeam; // support never helps enemies; an owner on team 0 supports only itself
    }
    std::optional<double> SweepSphere(const Segment& segment, const glm::dvec3& center, double radius, bool twoD)
    {
        auto from = segment.From, to = segment.To, c = center;
        if (twoD) from.z = to.z = c.z = 0;
        const auto d = to - from, f = from - c;
        const double outside = glm::dot(f, f) - radius * radius;
        if (outside <= 0) return 0.0;
        const double a = glm::dot(d, d);
        if (a < 1e-18) return std::nullopt; // degenerate segment: the point test above decided
        const double b = 2 * glm::dot(f, d);
        if (b >= 0) return std::nullopt; // starting outside and not approaching
        const double discriminant = b * b - 4 * a * outside;
        if (discriminant < 0) return std::nullopt;
        const double t = (-b - std::sqrt(discriminant)) / (2 * a);
        if (!(t >= 0 && t <= 1)) return std::nullopt;
        return t;
    }
    size_t SweepActors(const ProjectileSpec& spec, const ProjectileState& state, const Segment& segment, std::span<const ActorProbe> probes, std::span<CandidateHit> out)
    {
        const size_t capacity = std::min<size_t>(out.size(), 16);
        if (capacity == 0) return 0;
        const double radius = spec.Radius + spec.TargetRadius;
        size_t count = 0;
        for (const auto& probe : probes)
        {
            if (probe.ID == 0 || probe.ID == state.Owner || state.HasHit(probe.ID)) continue;
            if (!Admits(spec.Kind, spec.FriendlyFire, state.Owner, state.Team, probe.ID, probe.Team, false)) continue;
            auto t = SweepSphere(segment, probe.Center + spec.TargetOffset, radius, spec.TwoD);
            if (!t) continue;
            const CandidateHit hit{ *t, probe.ID };
            if (count == capacity)
            {
                if (!Before(hit, out[count - 1])) continue;
                --count;
            }
            size_t i = count;
            for (; i > 0 && Before(hit, out[i - 1]); --i) out[i] = out[i - 1];
            out[i] = hit;
            ++count;
        }
        return count;
    }
    ProjectilePool::ProjectilePool() { m_Generation.fill(1); }
    std::optional<uint32_t> ProjectilePool::Acquire()
    {
        for (uint32_t slot = 0; slot < Capacity; ++slot)
            if (!m_Active[slot])
            {
                m_Active[slot] = true;
                ++m_Count;
                return slot;
            }
        return std::nullopt;
    }
    void ProjectilePool::Release(uint32_t slot)
    {
        if (slot >= Capacity || !m_Active[slot]) return;
        m_Active[slot] = false;
        --m_Count;
        m_Generation[slot] = m_Generation[slot] >= MaxGeneration ? 1 : m_Generation[slot] + 1;
    }
    bool ProjectilePool::Live(uint32_t handle) const
    {
        const uint32_t slot = Slot(handle);
        return slot < Capacity && m_Active[slot] && m_Generation[slot] == handle >> 8;
    }
    bool ProjectilePool::Place(uint32_t slot, uint32_t generation)
    {
        if (slot >= Capacity || m_Active[slot] || generation < 1 || generation > MaxGeneration) return false;
        m_Active[slot] = true;
        m_Generation[slot] = generation;
        ++m_Count;
        return true;
    }
    void ProjectilePool::Clear()
    {
        for (uint32_t slot = 0; slot < Capacity; ++slot) Release(slot);
    }
}
