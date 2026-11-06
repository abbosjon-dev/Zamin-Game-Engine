#include "Zamin/Scene/ParticleSystem.h"
#include "Zamin/Scene/Components.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin {

    // ------------------------------------------------------------------ gradient
    glm::vec4 ParticleGradient::Evaluate(float t) const
    {
        if (Stops.empty())
            return glm::vec4(1.0f);
        if (Stops.size() == 1 || t <= Stops.front().Time)
            return Stops.front().Color;
        if (t >= Stops.back().Time)
            return Stops.back().Color;
        auto it = std::upper_bound(Stops.begin(), Stops.end(), t, [](float time, const Stop& s) { return time < s.Time; });
        const Stop& b = *it;
        const Stop& a = *(it - 1);
        if (Fixed)
            return a.Color;
        const float span = b.Time - a.Time;
        return span > 0.0f ? glm::mix(a.Color, b.Color, (t - a.Time) / span) : b.Color;
    }

    void ParticleGradient::Sort()
    {
        std::stable_sort(Stops.begin(), Stops.end(), [](const Stop& a, const Stop& b) { return a.Time < b.Time; });
    }

    int ParticleGradient::AddStop(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        Stop stop{ t, Evaluate(t) };
        auto it = std::upper_bound(Stops.begin(), Stops.end(), t, [](float time, const Stop& s) { return time < s.Time; });
        const int index = (int)(it - Stops.begin());
        Stops.insert(it, stop);
        return index;
    }

    ParticleGradient ParticleGradient::TwoStops(const glm::vec4& from, const glm::vec4& to)
    {
        ParticleGradient g;
        g.Stops = { { 0.0f, from }, { 1.0f, to } };
        return g;
    }

    // ------------------------------------------------------------------ curve
    float ParticleCurve::Evaluate(float t) const
    {
        return Keys.empty() ? 1.0f : PropertyAnimation::Evaluate(Keys, t).x;
    }

    void ParticleCurve::Sort()
    {
        std::stable_sort(Keys.begin(), Keys.end(), [](const AnimationKey& a, const AnimationKey& b) { return a.Time < b.Time; });
        PropertyAnimation::UpdateTangents(Keys);
    }

    // Values at the keys and in between (enough for the overshoot of a free Bezier tangent).
    template<typename F>
    static void SampleCurve(const ParticleCurve& curve, F&& f)
    {
        if (curve.Keys.empty())
        {
            f(1.0f);
            return;
        }
        for (const AnimationKey& k : curve.Keys)
            f(k.Value.x);
        bool smooth = false;
        for (const AnimationKey& k : curve.Keys)
            smooth |= k.Interpolation == KeyInterpolation::Bezier;
        if (smooth)
            for (int i = 0; i <= 32; i++)
                f(curve.Evaluate(i / 32.0f));
    }

    float ParticleCurve::GetMin() const
    {
        float v = std::numeric_limits<float>::max();
        SampleCurve(*this, [&](float x) { v = std::min(v, x); });
        return v;
    }

    float ParticleCurve::GetMax() const
    {
        float v = -std::numeric_limits<float>::max();
        SampleCurve(*this, [&](float x) { v = std::max(v, x); });
        return v;
    }

    int ParticleCurve::AddKey(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        AnimationKey key;
        key.Time = t;
        key.Value = glm::vec4(Evaluate(t), 0.0f, 0.0f, 0.0f);
        auto it = std::upper_bound(Keys.begin(), Keys.end(), t, [](float time, const AnimationKey& k) { return time < k.Time; });
        if (it != Keys.begin())
            key.Interpolation = (it - 1)->Interpolation;
        const int index = (int)(it - Keys.begin());
        Keys.insert(it, key);
        PropertyAnimation::UpdateTangents(Keys);
        return index;
    }

    ParticleCurve ParticleCurve::Constant(float value)
    {
        ParticleCurve c;
        AnimationKey a;
        a.Time = 0.0f;
        a.Value.x = value;
        AnimationKey b = a;
        b.Time = 1.0f;
        c.Keys = { a, b };
        return c;
    }

    ParticleCurve ParticleCurve::Linear(float from, float to)
    {
        ParticleCurve c = Constant(from);
        c.Keys[1].Value.x = to;
        return c;
    }

    namespace ParticleSimulation {

        using Shape = ParticleSystemComponent::EmitterShape;

        float Random01(uint32_t& state)
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            return (state & 0xFFFFFF) / float(0x1000000);
        }

        // lowbias32 (Chris Wellons): a well mixed 32-bit hash.
        static uint32_t Hash(uint32_t x)
        {
            x ^= x >> 16; x *= 0x7feb352du;
            x ^= x >> 15; x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }

        float SeedRandom(uint32_t seed, uint32_t salt)
        {
            return (Hash(seed ^ (salt * 0x9E3779B9u)) & 0xFFFFFF) / float(0x1000000);
        }

        // Two unit vectors at right angles to `dir` (a unit vector).
        static void Basis(const glm::vec3& dir, glm::vec3& t, glm::vec3& b)
        {
            glm::vec3 up = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            t = glm::normalize(glm::cross(up, dir));
            b = glm::cross(dir, t);
        }

        glm::vec3 RandomInCone(const glm::vec3& direction, float spreadDegrees, uint32_t& rng)
        {
            glm::vec3 dir = glm::length(direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0, 1, 0);
            float spread = glm::radians(std::clamp(spreadDegrees, 0.0f, 180.0f));
            float cosTheta = 1.0f - Random01(rng) * (1.0f - std::cos(spread));
            float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
            float phi = Random01(rng) * glm::two_pi<float>();
            glm::vec3 t, b;
            Basis(dir, t, b);
            return glm::normalize(t * (std::cos(phi) * sinTheta) + b * (std::sin(phi) * sinTheta) + dir * cosTheta);
        }

        // The direction away from the centre through `local` (`fallback` at the centre itself).
        static glm::vec3 Outward(const glm::vec3& local, const glm::vec3& fallback)
        {
            const float length = glm::length(local);
            return length > 1e-6f ? local / length : fallback;
        }

        void SampleShape(const ParticleSystemComponent& ps, uint32_t& rng, glm::vec3& local, glm::vec3& dir)
        {
            local = glm::vec3(0.0f);
            // The part of the radius that emits: from 1 - thickness to 1 (1: all of it).
            const float inner = 1.0f - std::clamp(ps.ShapeThickness, 0.0f, 1.0f);
            switch (ps.Shape)
            {
                case Shape::Circle:
                {
                    // Uniform over the ring's area (the whole disc: r = sqrt(u)).
                    float a = Random01(rng) * glm::two_pi<float>(), r = std::sqrt(inner * inner + (1.0f - inner * inner) * Random01(rng)) * ps.ShapeRadius;
                    local = { std::cos(a) * r, std::sin(a) * r, 0.0f };
                    break;
                }
                case Shape::Sphere:
                {
                    const float inner3 = inner * inner * inner;
                    local = RandomInCone({ 0, 1, 0 }, 180.0f, rng) * (std::cbrt(inner3 + (1.0f - inner3) * Random01(rng)) * ps.ShapeRadius);
                    break;
                }
                case Shape::Box:
                    local = (glm::vec3(Random01(rng), Random01(rng), Random01(rng)) * 2.0f - 1.0f) * ps.ShapeExtents;
                    break;
                case Shape::Edge:
                    local = { (Random01(rng) - 0.5f) * ps.EdgeLength, 0.0f, 0.0f };
                    break;
                case Shape::Cone:
                {
                    // Every ray goes through the apex behind the base: at a fraction s of the
                    // radius it leans atan(s * tan(angle)) away from the axis.
                    const glm::vec3 axis = glm::length(ps.Direction) > 0.0f ? glm::normalize(ps.Direction) : glm::vec3(0, 1, 0);
                    const float tanAngle = std::tan(glm::radians(std::clamp(ps.ConeAngle, 0.0f, 89.0f)));
                    glm::vec3 across;
                    float s;
                    if (ps.ConeFlat)
                    {
                        // A line across the axis in the XY plane, both sides alike.
                        glm::vec3 side(axis.y, -axis.x, 0.0f);
                        across = glm::length(side) > 1e-6f ? glm::normalize(side) : glm::vec3(1, 0, 0);
                        float u = Random01(rng);
                        s = inner + (1.0f - inner) * Random01(rng);
                        if (u < 0.5f)
                            s = -s;
                    }
                    else
                    {
                        glm::vec3 t, b;
                        Basis(axis, t, b);
                        float phi = Random01(rng) * glm::two_pi<float>();
                        across = t * std::cos(phi) + b * std::sin(phi);
                        s = std::sqrt(inner * inner + (1.0f - inner * inner) * Random01(rng));
                    }
                    local = across * (s * ps.ShapeRadius);
                    const float lean = std::atan(s * tanAngle);
                    dir = glm::normalize(axis * std::cos(lean) + across * std::sin(lean));
                    if (ps.ConeFrom == ParticleSystemComponent::ConeEmitFrom::Volume)
                        local += dir * (Random01(rng) * std::max(0.0f, ps.ConeLength) / std::max(0.01f, std::cos(lean)));
                    return; // the cone gives the direction
                }
                default: break;
            }
            const bool outward = ps.AlignToShape && (ps.Shape == Shape::Circle || ps.Shape == Shape::Sphere || ps.Shape == Shape::Box);
            dir = RandomInCone(outward ? Outward(local, ps.Direction) : ps.Direction, ps.Spread, rng);
        }

        void Spawn(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, int count)
        {
            uint32_t& rng = ps.RandomState;
            for (int i = 0; i < count && (int)ps.Particles.size() < ps.MaxParticles; i++)
            {
                Particle p;
                glm::vec3 local, dir;
                SampleShape(ps, rng, local, dir);
                float speed = ps.SpeedMin + (ps.SpeedMax - ps.SpeedMin) * Random01(rng);
                if (ps.WorldSpace)
                {
                    p.Position = glm::vec3(emitterWorld * glm::vec4(local, 1.0f));
                    p.Velocity = glm::normalize(glm::vec3(emitterWorld * glm::vec4(dir, 0.0f))) * speed;
                }
                else
                {
                    p.Position = local;
                    p.Velocity = dir * speed;
                }
                p.Lifetime = p.Life = std::max(0.01f, ps.LifetimeMin + (ps.LifetimeMax - ps.LifetimeMin) * Random01(rng));
                p.Rotation = Random01(rng) * glm::two_pi<float>();
                p.AngularVelocity = (Random01(rng) * 2.0f - 1.0f) * ps.AngularVelocityMax;
                // Its own numbers from the generator's state, without drawing more from it (the
                // particles after it stay as they were before particles had seeds).
                p.Seed = Hash(rng ^ 0x5bd1e995u);
                ps.Particles.push_back(p);
            }
        }

        void Update(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float dt, bool editor)
        {
            if (ps.Playing)
            {
                if (!ps.BurstDone)
                {
                    Spawn(ps, emitterWorld, ps.Burst);
                    ps.BurstDone = true;
                }
                if (ps.Looping || !editor)
                {
                    ps.EmitAccumulator += ps.EmissionRate * dt;
                    int count = (int)ps.EmitAccumulator;
                    ps.EmitAccumulator -= count;
                    if (count > 0) Spawn(ps, emitterWorld, count);
                }
                if (!ps.Looping && ps.BurstDone && ps.EmissionRate <= 0.0f && ps.Particles.empty() && editor)
                    ps.BurstDone = false; // keep previewing bursts in the editor
            }
            Integrate(ps, emitterWorld, dt);
        }

        void Integrate(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float ts)
        {
            const float drag = std::max(0.0f, 1.0f - ps.Drag * ts);
            const auto& vol = ps.VelocityOverLifetime;
            const auto& limit = ps.LimitVelocity;
            if (!vol.Enabled && !limit.Enabled)
            {
                for (auto& p : ps.Particles)
                {
                    p.Velocity += ps.Gravity * ts;
                    p.Velocity *= drag;
                    p.Position += p.Velocity * ts;
                    p.Rotation += p.AngularVelocity * ts;
                    p.Life -= ts;
                }
                std::erase_if(ps.Particles, [](const Particle& p) { return p.Life <= 0.0f; });
                return;
            }

            // The emitter's rotation (without its scale) and centre, in the particles' space.
            glm::mat3 rotation(1.0f);
            for (int i = 0; i < 3; i++)
            {
                glm::vec3 axis(emitterWorld[i]);
                const float length = glm::length(axis);
                rotation[i] = length > 1e-6f ? axis / length : glm::vec3(0.0f);
            }
            const glm::vec3 center = ps.WorldSpace ? glm::vec3(emitterWorld[3]) : glm::vec3(0.0f);
            // Linear velocity in the particles' space: emitter axes (World: world axes).
            glm::vec3 linear = vol.Linear;
            if (vol.Enabled)
            {
                if (ps.WorldSpace && !vol.World)
                    linear = rotation * linear;
                else if (!ps.WorldSpace && vol.World)
                    linear = glm::transpose(rotation) * linear;
            }
            const bool orbit = vol.Enabled && vol.Orbital != glm::vec3(0.0f);
            const glm::quat spin = orbit ? glm::quat(vol.Orbital * ts) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            // The speed above the limit kept over this step (frame-rate independent: per 1/60 s).
            const float keep = limit.Enabled ? std::pow(1.0f - std::clamp(limit.Dampen, 0.0f, 1.0f), ts * 60.0f) : 1.0f;

            for (auto& p : ps.Particles)
            {
                const float age = std::clamp(p.GetAge01(), 0.0f, 1.0f);
                p.Velocity += ps.Gravity * ts;
                p.Velocity *= drag;
                if (limit.Enabled)
                {
                    const float maxSpeed = std::max(0.0f, limit.MaxSpeed * limit.MaxSpeedCurve.Evaluate(age));
                    const float speed = glm::length(p.Velocity);
                    if (speed > maxSpeed && speed > 0.0f)
                        p.Velocity *= (maxSpeed + (speed - maxSpeed) * keep) / speed;
                }
                glm::vec3 motion = p.Velocity;
                if (vol.Enabled)
                {
                    motion *= vol.SpeedModifier.Evaluate(age);
                    motion += linear * vol.LinearCurve.Evaluate(age);
                    if (vol.Radial != 0.0f)
                    {
                        const glm::vec3 offset = p.Position - center;
                        const float length = glm::length(offset);
                        if (length > 1e-6f)
                            motion += offset * (vol.Radial / length);
                    }
                    if (orbit)
                    {
                        // Around the emitter's own axes through its centre.
                        const glm::vec3 offset = p.Position - center;
                        const glm::vec3 turned = ps.WorldSpace ? rotation * (spin * (glm::transpose(rotation) * offset)) : spin * offset;
                        p.Position = center + turned;
                    }
                }
                p.Position += motion * ts;
                p.Rotation += p.AngularVelocity * ts;
                p.Life -= ts;
            }
            std::erase_if(ps.Particles, [](const Particle& p) { return p.Life <= 0.0f; });
        }

        // ---- look
        glm::vec4 GetColor(const ParticleSystemComponent& ps, const Particle& p)
        {
            const float t = 1.0f - p.Life / p.Lifetime;
            const auto& module = ps.ColorOverLifetime;
            if (!module.Enabled)
                return glm::mix(ps.StartColor, ps.EndColor, t);
            glm::vec4 color = module.Gradient.Evaluate(t);
            if (module.RandomBetweenTwo)
                color = glm::mix(color, module.GradientB.Evaluate(t), SeedRandom(p.Seed, 1));
            return color;
        }

        float GetSize(const ParticleSystemComponent& ps, const Particle& p)
        {
            const float t = 1.0f - p.Life / p.Lifetime;
            const auto& module = ps.SizeOverLifetime;
            if (!module.Enabled)
                return glm::mix(ps.StartSize, ps.EndSize, t);
            float scale = module.Curve.Evaluate(t);
            if (module.RandomBetweenTwo)
                scale = glm::mix(scale, module.CurveB.Evaluate(t), SeedRandom(p.Seed, 2));
            return ps.StartSize * scale;
        }

        float GetMaxSize(const ParticleSystemComponent& ps)
        {
            const auto& module = ps.SizeOverLifetime;
            if (!module.Enabled)
                return std::max(std::abs(ps.StartSize), std::abs(ps.EndSize));
            auto extent = [](const ParticleCurve& c) { return std::max(std::abs(c.GetMin()), std::abs(c.GetMax())); };
            float scale = extent(module.Curve);
            if (module.RandomBetweenTwo)
                scale = std::max(scale, extent(module.CurveB));
            return std::abs(ps.StartSize) * scale;
        }

        int GetGridFrameCount(const ParticleSystemComponent& ps)
        {
            const auto& sheet = ps.TextureSheet;
            const int cells = std::max(1, sheet.Columns) * std::max(1, sheet.Rows);
            return sheet.FrameCount > 0 ? std::min(sheet.FrameCount, cells) : cells;
        }

        int GetSheetFrame(const ParticleSystemComponent& ps, const Particle& p, int frameCount)
        {
            const auto& sheet = ps.TextureSheet;
            if (!sheet.Enabled || frameCount <= 1)
                return 0;
            // One random row of the grid: its cells are the frames.
            const bool row = sheet.RandomRow && sheet.Source == ParticleSystemComponent::TextureSheetModule::FrameSource::Grid;
            const int columns = std::max(1, sheet.Columns);
            const int frames = row ? std::min(columns, frameCount) : frameCount;
            float position;
            if (sheet.Mode == ParticleSystemComponent::TextureSheetModule::TimeMode::FrameRate)
                position = std::max(0.0f, p.GetAge()) * std::max(0.0f, sheet.FrameRate);
            else
                position = std::clamp(p.GetAge01(), 0.0f, 0.99999f) * std::max(0.0f, sheet.Cycles) * frames;
            int frame = (int)std::floor(position) + sheet.StartFrame;
            if (sheet.RandomStartFrame)
                frame += (int)(SeedRandom(p.Seed, 3) * frames);
            frame %= frames;
            if (frame < 0)
                frame += frames;
            if (row)
            {
                const int rows = std::max(1, (frameCount + columns - 1) / columns);
                const int r = std::min(rows - 1, (int)(SeedRandom(p.Seed, 4) * rows));
                frame = std::min(frameCount - 1, r * columns + frame);
            }
            return frame;
        }

    }

}
