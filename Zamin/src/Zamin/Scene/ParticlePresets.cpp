// Particle presets: the built-in effects, a system as a preset (Capture) and back onto an entity
// (Apply), .zparticle files. Their JSON is the scenes' (SceneSerializer.cpp: ToString / FromString).
#include "Zamin/Scene/ParticlePresets.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Utils/FileSystem.h"

#include <algorithm>
#include <initializer_list>
#include <unordered_map>

namespace Zamin {

    using PS = ParticleSystemComponent;

    // ------------------------------------------------------------------ building blocks
    // A curve through (time, value) keys, smooth (Bezier, automatic tangents) or straight.
    static ParticleCurve Curve(std::initializer_list<std::pair<float, float>> keys, KeyInterpolation interpolation = KeyInterpolation::Bezier)
    {
        ParticleCurve curve;
        for (const auto& [time, value] : keys)
        {
            AnimationKey key;
            key.Time = time;
            key.Value.x = value;
            key.Interpolation = interpolation;
            curve.Keys.push_back(key);
        }
        curve.Sort();
        return curve;
    }

    struct GradientStop
    {
        float Time, R, G, B, A;
    };

    static ParticleGradient Gradient(std::initializer_list<GradientStop> stops)
    {
        ParticleGradient gradient;
        for (const GradientStop& s : stops)
            gradient.Stops.push_back({ s.Time, glm::vec4(s.R, s.G, s.B, s.A) });
        gradient.Sort();
        return gradient;
    }

    // A template that emits nothing itself: only what its owner's sub-emitter makes.
    static PS TemplateSystem()
    {
        PS t;
        t.EmissionEnabled = false;
        t.EmissionRate = 0.0f;
        t.Burst = 0;
        t.MaxParticles = 2000;
        return t;
    }

    static PS::SubEmitter SubEmitter(PS::SubEmitter::TriggerType trigger, int templateIndex, int count, float probability = 1.0f)
    {
        PS::SubEmitter sub;
        sub.Trigger = trigger;
        sub.Emitter.ID = (uint64_t)(templateIndex + 1);
        sub.Count = count;
        sub.Probability = probability;
        return sub;
    }

    // ------------------------------------------------------------------ built-ins
    static ParticlePreset MakeFire()
    {
        ParticlePreset preset{ "Olov", {}, {} };
        PS& ps = preset.System;
        ps.EmissionRate = 70.0f;
        ps.MaxParticles = 400;
        ps.Shape = PS::EmitterShape::Cone;
        ps.ShapeRadius = 0.25f;
        ps.ConeAngle = 8.0f;
        ps.LifetimeMin = 0.6f;
        ps.LifetimeMax = 1.2f;
        ps.SpeedMin = 0.8f;
        ps.SpeedMax = 1.6f;
        ps.Gravity = { 0.0f, 1.2f, 0.0f };
        ps.Drag = 0.6f;
        ps.AngularVelocityMax = 1.0f;
        ps.StartSize = 0.55f;
        ps.EndSize = 0.08f;
        ps.StartColor = { 1.0f, 0.8f, 0.3f, 1.0f };
        ps.EndColor = { 0.6f, 0.1f, 0.02f, 0.0f };
        ps.ColorOverLifetime.Enabled = true;
        ps.ColorOverLifetime.Gradient = Gradient({ { 0.0f, 1.0f, 0.9f, 0.5f, 0.0f }, { 0.1f, 1.0f, 0.8f, 0.3f, 1.0f },
                                                   { 0.45f, 1.0f, 0.4f, 0.08f, 0.85f }, { 1.0f, 0.6f, 0.1f, 0.02f, 0.0f } });
        ps.SizeOverLifetime.Enabled = true;
        ps.SizeOverLifetime.Curve = Curve({ { 0.0f, 0.6f }, { 0.25f, 1.0f }, { 1.0f, 0.15f } });
        ps.Noise.Enabled = true;
        ps.Noise.Strength = { 0.6f, 0.4f, 0.6f };
        ps.Noise.Frequency = 1.2f;
        ps.Noise.ScrollSpeed = 0.8f;
        ps.Additive = true;
        return preset;
    }

    static ParticlePreset MakeSmoke()
    {
        ParticlePreset preset{ "Tutun", {}, {} };
        PS& ps = preset.System;
        ps.EmissionRate = 10.0f;
        ps.MaxParticles = 200;
        ps.Prewarm = true;
        ps.Shape = PS::EmitterShape::Sphere;
        ps.ShapeRadius = 0.3f;
        ps.Spread = 12.0f;
        ps.LifetimeMin = 3.0f;
        ps.LifetimeMax = 5.0f;
        ps.SpeedMin = 0.4f;
        ps.SpeedMax = 0.9f;
        ps.Gravity = { 0.0f, 0.25f, 0.0f };
        ps.Drag = 0.3f;
        ps.AngularVelocityMax = 0.6f;
        ps.StartSize = 0.7f;
        ps.EndSize = 2.0f;
        ps.StartColor = { 0.5f, 0.5f, 0.52f, 0.45f };
        ps.EndColor = { 0.35f, 0.35f, 0.37f, 0.0f };
        ps.ColorOverLifetime.Enabled = true;
        ps.ColorOverLifetime.Gradient = Gradient({ { 0.0f, 0.55f, 0.55f, 0.58f, 0.0f }, { 0.15f, 0.5f, 0.5f, 0.52f, 0.45f },
                                                   { 1.0f, 0.35f, 0.35f, 0.37f, 0.0f } });
        ps.SizeOverLifetime.Enabled = true;
        ps.SizeOverLifetime.Curve = Curve({ { 0.0f, 0.5f }, { 1.0f, 2.8f } });
        ps.Noise.Enabled = true;
        ps.Noise.Strength = { 0.35f, 0.15f, 0.35f };
        ps.Noise.Frequency = 0.4f;
        ps.Noise.ScrollSpeed = 0.2f;
        ps.Additive = false;
        return preset;
    }

    static ParticlePreset MakeSparks()
    {
        ParticlePreset preset{ "Uchqunlar", {}, {} };
        PS& ps = preset.System;
        ps.EmissionRate = 30.0f;
        ps.Bursts = { { 0.0f, 20, 0, 1.0f, 1.0f } };
        ps.MaxParticles = 600;
        ps.Shape = PS::EmitterShape::Cone;
        ps.ShapeRadius = 0.05f;
        ps.ConeAngle = 35.0f;
        ps.LifetimeMin = 0.8f;
        ps.LifetimeMax = 1.5f;
        ps.SpeedMin = 3.0f;
        ps.SpeedMax = 6.0f;
        ps.Gravity = { 0.0f, -9.8f, 0.0f };
        ps.Drag = 0.4f;
        ps.StartSize = 0.06f;
        ps.EndSize = 0.02f;
        ps.StartColor = { 1.0f, 0.95f, 0.6f, 1.0f };
        ps.EndColor = { 1.0f, 0.3f, 0.05f, 0.0f };
        ps.ColorOverLifetime.Enabled = true;
        ps.ColorOverLifetime.Gradient = Gradient({ { 0.0f, 1.0f, 1.0f, 0.85f, 1.0f }, { 0.3f, 1.0f, 0.75f, 0.3f, 1.0f }, { 1.0f, 1.0f, 0.3f, 0.05f, 0.0f } });
        ps.SizeOverLifetime.Enabled = true;
        ps.SizeOverLifetime.Curve = ParticleCurve::Linear(1.0f, 0.3f);
        ps.Collision.Enabled = true;
        ps.Collision.Bounce = 0.35f;
        ps.Collision.Dampen = 0.3f;
        ps.Collision.LifetimeLoss = 0.15f;
        ps.Stretched = true;
        ps.StretchLength = 1.0f;
        ps.StretchSpeed = 0.03f;
        ps.Additive = true;
        return preset;
    }

    static ParticlePreset MakeRain()
    {
        ParticlePreset preset{ "Yomg'ir", {}, {} };
        PS& ps = preset.System;
        ps.EmissionRate = 1200.0f;
        ps.MaxParticles = 2500;
        ps.Shape = PS::EmitterShape::Box;
        ps.ShapeExtents = { 12.0f, 0.1f, 12.0f };
        ps.Direction = { 0.0f, -1.0f, 0.0f };
        ps.Spread = 2.0f;
        ps.LifetimeMin = 1.2f;
        ps.LifetimeMax = 1.6f;
        ps.SpeedMin = 16.0f;
        ps.SpeedMax = 20.0f;
        ps.StartSize = 0.025f;
        ps.EndSize = 0.025f;
        ps.StartColor = { 0.72f, 0.8f, 0.92f, 0.5f };
        ps.EndColor = { 0.72f, 0.8f, 0.92f, 0.35f };
        ps.Additive = false;
        ps.Sorting = PS::SortMode::None; // thin streaks: their order does not show
        ps.Stretched = true;
        ps.StretchLength = 1.0f;
        ps.StretchSpeed = 0.035f;
        ps.FollowCamera.Enabled = true;
        ps.FollowCamera.Offset = { 0.0f, 9.0f, 0.0f };
        ps.FollowCamera.Wrap = true;
        ps.FollowCamera.WrapExtents = { 12.0f, 0.0f, 12.0f };
        // The drops end on the ground (Y 0) in a splash.
        ps.Collision.Enabled = true;
        ps.Collision.Bounce = 0.0f;
        ps.Collision.LifetimeLoss = 1.0f;
        ps.Collision.RadiusScale = 0.0f;
        ps.SubEmitters.Enabled = true;
        ps.SubEmitters.Emitters = { SubEmitter(PS::SubEmitter::TriggerType::Collision, 0, 3, 0.6f) };

        PS splash = TemplateSystem();
        splash.Direction = { 0.0f, 1.0f, 0.0f };
        splash.Spread = 60.0f;
        splash.SpeedMin = 1.0f;
        splash.SpeedMax = 2.2f;
        splash.LifetimeMin = 0.2f;
        splash.LifetimeMax = 0.35f;
        splash.Gravity = { 0.0f, -9.8f, 0.0f };
        splash.StartSize = 0.035f;
        splash.EndSize = 0.01f;
        splash.StartColor = { 0.8f, 0.85f, 0.95f, 0.6f };
        splash.EndColor = { 0.8f, 0.85f, 0.95f, 0.0f };
        splash.Additive = false;
        splash.Sorting = PS::SortMode::None;
        preset.Templates.push_back({ "Yomg'ir sachrashi", splash });
        return preset;
    }

    static ParticlePreset MakeSnow()
    {
        ParticlePreset preset{ "Qor", {}, {} };
        PS& ps = preset.System;
        ps.EmissionRate = 220.0f;
        ps.MaxParticles = 3000;
        ps.Shape = PS::EmitterShape::Box;
        ps.ShapeExtents = { 14.0f, 0.1f, 14.0f };
        ps.Direction = { 0.0f, -1.0f, 0.0f };
        ps.Spread = 10.0f;
        ps.LifetimeMin = 9.0f;
        ps.LifetimeMax = 12.0f;
        ps.SpeedMin = 0.6f;
        ps.SpeedMax = 1.1f;
        ps.AngularVelocityMax = 1.0f;
        ps.StartSize = 0.07f;
        ps.EndSize = 0.07f;
        ps.StartColor = { 1.0f, 1.0f, 1.0f, 0.9f };
        ps.EndColor = { 1.0f, 1.0f, 1.0f, 0.0f };
        ps.ColorOverLifetime.Enabled = true;
        ps.ColorOverLifetime.Gradient = Gradient({ { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f }, { 0.05f, 1.0f, 1.0f, 1.0f, 0.9f },
                                                   { 0.9f, 1.0f, 1.0f, 1.0f, 0.9f }, { 1.0f, 1.0f, 1.0f, 1.0f, 0.0f } });
        ps.SizeOverLifetime.Enabled = true;
        ps.SizeOverLifetime.Curve = ParticleCurve::Constant(1.0f);
        ps.SizeOverLifetime.RandomBetweenTwo = true;
        ps.SizeOverLifetime.CurveB = ParticleCurve::Constant(0.45f);
        ps.Noise.Enabled = true;
        ps.Noise.Strength = { 0.5f, 0.15f, 0.5f };
        ps.Noise.Frequency = 0.35f;
        ps.Noise.ScrollSpeed = 0.3f;
        ps.Noise.Octaves = 2;
        ps.Additive = false;
        ps.Sorting = PS::SortMode::None;
        ps.FollowCamera.Enabled = true;
        ps.FollowCamera.Offset = { 0.0f, 7.0f, 0.0f };
        ps.FollowCamera.Wrap = true;
        ps.FollowCamera.WrapExtents = { 14.0f, 0.0f, 14.0f };
        // Flakes melt away on the ground.
        ps.Collision.Enabled = true;
        ps.Collision.Bounce = 0.0f;
        ps.Collision.LifetimeLoss = 1.0f;
        ps.Collision.RadiusScale = 0.0f;
        return preset;
    }

    static ParticlePreset MakeMagic()
    {
        ParticlePreset preset{ "Sehr", {}, {} };
        PS& ps = preset.System;
        ps.EmissionRate = 40.0f;
        ps.MaxParticles = 300;
        ps.Shape = PS::EmitterShape::Sphere;
        ps.ShapeRadius = 0.6f;
        ps.ShapeThickness = 0.0f;
        ps.Spread = 180.0f;
        ps.LifetimeMin = 1.5f;
        ps.LifetimeMax = 2.5f;
        ps.SpeedMin = 0.05f;
        ps.SpeedMax = 0.2f;
        ps.StartSize = 0.12f;
        ps.EndSize = 0.0f;
        ps.StartColor = { 0.7f, 0.5f, 1.0f, 1.0f };
        ps.EndColor = { 0.3f, 0.2f, 1.0f, 0.0f };
        ps.ColorOverLifetime.Enabled = true;
        ps.ColorOverLifetime.Gradient = Gradient({ { 0.0f, 0.6f, 0.4f, 1.0f, 0.0f }, { 0.15f, 0.75f, 0.5f, 1.0f, 1.0f }, { 1.0f, 0.3f, 0.2f, 1.0f, 0.0f } });
        ps.ColorOverLifetime.RandomBetweenTwo = true;
        ps.ColorOverLifetime.GradientB = Gradient({ { 0.0f, 0.3f, 0.9f, 1.0f, 0.0f }, { 0.15f, 0.5f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.2f, 0.5f, 1.0f, 0.0f } });
        ps.SizeOverLifetime.Enabled = true;
        ps.SizeOverLifetime.Curve = Curve({ { 0.0f, 0.4f }, { 0.2f, 1.0f }, { 1.0f, 0.0f } });
        ps.VelocityOverLifetime.Enabled = true;
        ps.VelocityOverLifetime.Orbital = { 0.0f, 2.5f, 0.0f };
        ps.VelocityOverLifetime.Radial = -0.15f;
        ps.VelocityOverLifetime.Linear = { 0.0f, 0.35f, 0.0f };
        ps.Noise.Enabled = true;
        ps.Noise.Strength = { 0.4f, 0.4f, 0.4f };
        ps.Noise.Frequency = 1.0f;
        ps.Noise.ScrollSpeed = 0.5f;
        ps.Trails.Enabled = true;
        ps.Trails.Ratio = 0.6f;
        ps.Trails.Lifetime = 0.35f;
        ps.Trails.MinVertexDistance = 0.04f;
        ps.Trails.Width = 0.6f;
        ps.Additive = true;
        return preset;
    }

    static ParticlePreset MakeExplosion()
    {
        ParticlePreset preset{ "Portlash", {}, {} };
        PS& ps = preset.System;
        ps.Looping = false;
        ps.Duration = 2.0f;
        ps.EmissionRate = 0.0f;
        ps.Bursts = { { 0.0f, 70, 1, 0.5f, 1.0f } };
        ps.MaxParticles = 200;
        ps.Shape = PS::EmitterShape::Sphere;
        ps.ShapeRadius = 0.25f;
        ps.AlignToShape = true;
        ps.Spread = 15.0f;
        ps.LifetimeMin = 0.6f;
        ps.LifetimeMax = 1.4f;
        ps.SpeedMin = 3.0f;
        ps.SpeedMax = 8.0f;
        ps.Drag = 3.5f;
        ps.Gravity = { 0.0f, 0.8f, 0.0f };
        ps.AngularVelocityMax = 1.5f;
        ps.StartSize = 0.9f;
        ps.EndSize = 1.2f;
        ps.StartColor = { 1.0f, 0.8f, 0.3f, 1.0f };
        ps.EndColor = { 0.15f, 0.15f, 0.15f, 0.0f };
        ps.ColorOverLifetime.Enabled = true;
        ps.ColorOverLifetime.Gradient = Gradient({ { 0.0f, 1.0f, 1.0f, 0.9f, 1.0f }, { 0.15f, 1.0f, 0.8f, 0.3f, 1.0f }, { 0.4f, 1.0f, 0.35f, 0.05f, 0.8f },
                                                   { 0.7f, 0.25f, 0.2f, 0.18f, 0.5f }, { 1.0f, 0.15f, 0.15f, 0.15f, 0.0f } });
        ps.SizeOverLifetime.Enabled = true;
        ps.SizeOverLifetime.Curve = Curve({ { 0.0f, 0.4f }, { 0.2f, 1.0f }, { 1.0f, 1.4f } });
        ps.Noise.Enabled = true;
        ps.Noise.Strength = { 0.5f, 0.5f, 0.5f };
        ps.Noise.Frequency = 0.8f;
        ps.Additive = false;
        // Every fireball throws a spark further out.
        ps.SubEmitters.Enabled = true;
        PS::SubEmitter sparks = SubEmitter(PS::SubEmitter::TriggerType::Birth, 0, 1);
        sparks.InheritVelocity = 1.6f;
        ps.SubEmitters.Emitters = { sparks };

        PS t = TemplateSystem();
        t.Spread = 180.0f;
        t.SpeedMin = 0.0f;
        t.SpeedMax = 0.5f;
        t.LifetimeMin = 0.6f;
        t.LifetimeMax = 1.2f;
        t.Gravity = { 0.0f, -9.8f, 0.0f };
        t.Drag = 0.5f;
        t.StartSize = 0.05f;
        t.EndSize = 0.02f;
        t.StartColor = { 1.0f, 0.9f, 0.5f, 1.0f };
        t.EndColor = { 1.0f, 0.3f, 0.05f, 0.0f };
        t.Stretched = true;
        t.StretchSpeed = 0.04f;
        t.Additive = true;
        t.Collision.Enabled = true;
        t.Collision.Bounce = 0.3f;
        t.Collision.Dampen = 0.3f;
        preset.Templates.push_back({ "Portlash uchqunlari", t });
        return preset;
    }

    static ParticlePreset MakeDust()
    {
        ParticlePreset preset{ "Chang", {}, {} };
        PS& ps = preset.System;
        ps.EmissionRate = 6.0f;
        ps.MaxParticles = 100;
        ps.Prewarm = true;
        ps.Shape = PS::EmitterShape::Box;
        ps.ShapeExtents = { 2.0f, 0.1f, 2.0f };
        ps.Spread = 60.0f;
        ps.LifetimeMin = 4.0f;
        ps.LifetimeMax = 6.0f;
        ps.SpeedMin = 0.05f;
        ps.SpeedMax = 0.25f;
        ps.AngularVelocityMax = 0.3f;
        ps.StartSize = 0.6f;
        ps.EndSize = 1.6f;
        ps.StartColor = { 0.72f, 0.62f, 0.48f, 0.25f };
        ps.EndColor = { 0.7f, 0.6f, 0.45f, 0.0f };
        ps.ColorOverLifetime.Enabled = true;
        ps.ColorOverLifetime.Gradient = Gradient({ { 0.0f, 0.75f, 0.65f, 0.5f, 0.0f }, { 0.3f, 0.72f, 0.62f, 0.48f, 0.25f }, { 1.0f, 0.7f, 0.6f, 0.45f, 0.0f } });
        ps.SizeOverLifetime.Enabled = true;
        ps.SizeOverLifetime.Curve = ParticleCurve::Linear(0.6f, 1.6f);
        ps.Noise.Enabled = true;
        ps.Noise.Strength = { 0.25f, 0.08f, 0.25f };
        ps.Noise.Frequency = 0.3f;
        ps.Noise.ScrollSpeed = 0.15f;
        ps.Additive = false;
        return preset;
    }

    static ParticlePreset MakeFirework()
    {
        ParticlePreset preset{ "Mushak", {}, {} };
        PS& ps = preset.System;
        ps.Duration = 3.6f;
        ps.EmissionRate = 0.0f;
        ps.Bursts = { { 0.0f, 1, 0, 0.9f, 1.0f } };
        ps.MaxParticles = 20;
        ps.Spread = 12.0f;
        ps.LifetimeMin = 1.0f;
        ps.LifetimeMax = 1.3f;
        ps.SpeedMin = 9.0f;
        ps.SpeedMax = 12.0f;
        ps.Gravity = { 0.0f, -6.0f, 0.0f };
        ps.StartSize = 0.12f;
        ps.EndSize = 0.12f;
        ps.ColorOverLifetime.Enabled = true;
        ps.ColorOverLifetime.Gradient = ParticleGradient::TwoStops({ 1.0f, 0.45f, 0.25f, 1.0f }, { 1.0f, 0.75f, 0.3f, 1.0f });
        ps.ColorOverLifetime.RandomBetweenTwo = true;
        ps.ColorOverLifetime.GradientB = ParticleGradient::TwoStops({ 0.3f, 0.7f, 1.0f, 1.0f }, { 0.6f, 0.4f, 1.0f, 1.0f });
        ps.Trails.Enabled = true;
        ps.Trails.Lifetime = 0.4f;
        ps.Trails.Width = 0.5f;
        ps.Additive = true;
        ps.SubEmitters.Enabled = true;
        PS::SubEmitter burst = SubEmitter(PS::SubEmitter::TriggerType::Death, 0, 90);
        burst.InheritVelocity = 0.2f;
        burst.InheritColor = true;
        ps.SubEmitters.Emitters = { burst };

        PS t = TemplateSystem();
        t.Shape = PS::EmitterShape::Sphere;
        t.ShapeRadius = 0.05f;
        t.Spread = 180.0f;
        t.AlignToShape = true;
        t.SpeedMin = 3.0f;
        t.SpeedMax = 5.0f;
        t.LifetimeMin = 1.0f;
        t.LifetimeMax = 1.6f;
        t.Gravity = { 0.0f, -2.5f, 0.0f };
        t.Drag = 1.2f;
        t.StartSize = 0.1f;
        t.EndSize = 0.03f;
        t.ColorOverLifetime.Enabled = true;
        t.ColorOverLifetime.Gradient = Gradient({ { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f }, { 0.7f, 1.0f, 1.0f, 1.0f, 0.9f }, { 1.0f, 1.0f, 1.0f, 1.0f, 0.0f } });
        t.SizeOverLifetime.Enabled = true;
        t.SizeOverLifetime.Curve = ParticleCurve::Linear(1.0f, 0.3f);
        t.Trails.Enabled = true;
        t.Trails.Ratio = 0.5f;
        t.Trails.Lifetime = 0.3f;
        t.Trails.Width = 0.5f;
        t.Additive = true;
        preset.Templates.push_back({ "Mushak portlashi", t });
        return preset;
    }

    const std::vector<ParticlePresets::Info>& ParticlePresets::GetBuiltins()
    {
        static const std::vector<Info> s_Builtins = {
            { "Fire", "Olov", "Ko'tariluvchi olov tillari: rang gradienti, o'lcham egri chizig'i, shovqin (gulxan, mash'al)" },
            { "Smoke", "Tutun", "Sekin ko'tarilib kengayadigan shaffof tutun, boshidan to'la (mo'ri, yong'in)" },
            { "Sparks", "Uchqunlar", "Cho'zilgan uchqunlar: gravitatsiya, yerdan sakraydi (payvand, metall urilishi)" },
            { "Rain", "Yomg'ir", "Kamera atrofida yog'adigan yomg'ir: cho'zilgan tomchilar, yerga tegsa sachraydi" },
            { "Snow", "Qor", "Kamera atrofida sekin aylanib tushadigan qor parchalari (shovqin bilan)" },
            { "Magic", "Sehr", "Aylanib uchadigan rang-barang sehrli zarralar, izlari bilan" },
            { "Explosion", "Portlash", "Bir martalik olovli portlash: tutunga aylanadi, uchqunlar sochiladi" },
            { "Dust", "Chang", "Yer ustida suzib yuradigan yengil chang bulutlari" },
            { "Firework", "Mushak", "Osmonga uchib, o'z rangida portlaydigan mushak raketalari (sub-emitter)" },
        };
        return s_Builtins;
    }

    std::optional<ParticlePreset> ParticlePresets::GetBuiltin(std::string_view key)
    {
        if (key == "Fire") return MakeFire();
        if (key == "Smoke") return MakeSmoke();
        if (key == "Sparks") return MakeSparks();
        if (key == "Rain") return MakeRain();
        if (key == "Snow") return MakeSnow();
        if (key == "Magic") return MakeMagic();
        if (key == "Explosion") return MakeExplosion();
        if (key == "Dust") return MakeDust();
        if (key == "Firework") return MakeFirework();
        return std::nullopt;
    }

    // ------------------------------------------------------------------ capture / apply
    void ParticlePresets::ClearRuntime(ParticleSystemComponent& ps)
    {
        ps.Clear();
        ps.Texture = nullptr;
        ps.RandomState = ParticleSystemComponent().RandomState;
    }

    // What of a system belongs to its scene, made to stand on its own: Custom space -> World, a
    // plane through an entity -> that plane in the world.
    static void Detach(Scene& scene, ParticleSystemComponent& ps)
    {
        ParticlePresets::ClearRuntime(ps);
        if (ps.Space == PS::SimulationSpace::Custom)
            ps.Space = PS::SimulationSpace::World;
        ps.CustomSpace = EntityRef();
        if (ps.Collision.PlaneEntity)
        {
            if (Entity planeEntity = scene.GetEntityByUUID(ps.Collision.PlaneEntity.ID))
            {
                const glm::mat4 world = scene.GetWorldTransform(planeEntity);
                const glm::vec3 up(world[1]);
                const glm::vec3 normal = glm::length(up) > 1e-6f ? glm::normalize(up) : glm::vec3(0.0f, 1.0f, 0.0f);
                ps.Collision.PlaneNormal = normal;
                ps.Collision.PlaneOffset = glm::dot(normal, glm::vec3(world[3]));
            }
            ps.Collision.PlaneEntity = EntityRef();
        }
    }

    ParticlePreset ParticlePresets::Capture(Scene& scene, Entity entity)
    {
        ParticlePreset preset;
        if (!entity || !entity.HasComponent<ParticleSystemComponent>())
            return preset;
        preset.Name = entity.GetName();
        preset.System = entity.GetComponent<ParticleSystemComponent>();
        Detach(scene, preset.System);
        std::unordered_map<uint64_t, int> templates; // entity -> template index
        for (auto& sub : preset.System.SubEmitters.Emitters)
        {
            Entity target = sub.Emitter ? scene.GetEntityByUUID(sub.Emitter.ID) : Entity();
            if (!target || target == entity || !target.HasComponent<ParticleSystemComponent>())
            {
                sub.Emitter.ID = 0;
                continue;
            }
            auto [it, added] = templates.try_emplace(sub.Emitter.ID, (int)preset.Templates.size());
            if (added)
            {
                ParticlePreset::Template t{ target.GetName(), target.GetComponent<ParticleSystemComponent>() };
                Detach(scene, t.System);
                t.System.SubEmitters = {}; // one level
                preset.Templates.push_back(std::move(t));
            }
            sub.Emitter.ID = (uint64_t)(it->second + 1);
        }
        return preset;
    }

    std::vector<Entity> ParticlePresets::Apply(Scene& scene, Entity entity, const ParticlePreset& preset)
    {
        std::vector<Entity> made;
        if (!entity)
            return made;
        // The old templates: its children its sub-emitters used.
        if (const auto* old = entity.TryGetComponent<ParticleSystemComponent>())
        {
            std::vector<Entity> children = scene.GetChildren(entity);
            std::vector<Entity> remove;
            for (const auto& sub : old->SubEmitters.Emitters)
                for (Entity child : children)
                    if (sub.Emitter && (uint64_t)child.GetUUID() == sub.Emitter.ID && child.HasComponent<ParticleSystemComponent>() &&
                        std::find(remove.begin(), remove.end(), child) == remove.end())
                        remove.push_back(child);
            for (Entity child : remove)
                scene.DestroyEntity(child);
        }
        // The templates first (adding their components may move the owner's in storage).
        std::vector<uint64_t> ids;
        for (const auto& t : preset.Templates)
        {
            Entity child = scene.CreateEntity(t.Name.empty() ? std::string("Sub-emitter") : t.Name);
            auto& system = child.AddComponent<ParticleSystemComponent>(t.System);
            ClearRuntime(system);
            system.RandomState ^= (uint32_t)(uint64_t)child.GetUUID();
            system.SubEmitters = {};
            scene.SetParent(child, entity, false);
            ids.push_back((uint64_t)child.GetUUID());
            made.push_back(child);
        }
        uint32_t layer = SortingLayerList::DefaultID;
        int order = 0;
        if (const auto* old = entity.TryGetComponent<ParticleSystemComponent>())
        {
            layer = old->SortingLayer;
            order = old->SortingOrder;
        }
        auto& ps = entity.AddOrReplaceComponent<ParticleSystemComponent>(preset.System);
        ClearRuntime(ps);
        ps.RandomState ^= (uint32_t)(uint64_t)entity.GetUUID();
        ps.SortingLayer = layer;
        ps.SortingOrder = order;
        ps.CustomSpace = EntityRef();
        ps.Collision.PlaneEntity = EntityRef();
        for (auto& sub : ps.SubEmitters.Emitters)
        {
            const uint64_t index = sub.Emitter.ID;
            sub.Emitter.ID = index >= 1 && index <= ids.size() ? ids[(size_t)(index - 1)] : 0;
        }
        for (Entity child : made)
        {
            auto& system = child.GetComponent<ParticleSystemComponent>();
            system.SortingLayer = layer;
            system.SortingOrder = order;
        }
        return made;
    }

    Entity ParticlePresets::Instantiate(Scene& scene, const ParticlePreset& preset, const glm::vec3& position)
    {
        Entity entity = scene.CreateEntity(preset.Name.empty() ? std::string("Particle System") : preset.Name);
        entity.GetComponent<TransformComponent>().Translation = position;
        Apply(scene, entity, preset);
        return entity;
    }

    // ------------------------------------------------------------------ files
    bool ParticlePresets::Save(const std::filesystem::path& file, const ParticlePreset& preset)
    {
        return FileSystem::WriteTextFile(file, ToString(preset));
    }

    bool ParticlePresets::Load(const std::filesystem::path& file, ParticlePreset& out, std::string* error)
    {
        std::string reason = AssetImportLog::CheckReadable(file);
        if (!reason.empty())
        {
            if (error)
                *error = reason;
            return false;
        }
        if (!FromString(FileSystem::ReadTextFile(file), out, error))
            return false;
        if (out.Name.empty())
            out.Name = file.stem().string();
        return true;
    }

}
