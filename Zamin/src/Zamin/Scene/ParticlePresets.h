#pragma once

#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    class Scene;

    // A particle effect kept apart from a scene: a system's settings and the systems its
    // sub-emitters use (templates; applying the preset makes them the entity's children). In a
    // preset a sub-emitter's Emitter.ID is 1 + its template's index (0: none); what belongs to a
    // scene is not kept - a Custom space is World, a collision plane through an entity is that
    // plane's world normal and offset. The templates' own sub-emitters are left out.
    struct ParticlePreset
    {
        struct Template
        {
            std::string Name;
            ParticleSystemComponent System;
        };
        std::string Name;
        ParticleSystemComponent System;
        std::vector<Template> Templates;
    };

    // The particle effects the editor offers ready-made (fire, smoke, sparks, rain, snow, magic,
    // explosion, dust, firework) and the project's own .zparticle files (Save as preset).
    class ParticlePresets
    {
    public:
        static constexpr const char* Extension = ".zparticle";
        static constexpr int FileVersion = 1;

        struct Info
        {
            const char* Key;          // "Fire", "Rain"... (stable: tests, automation)
            const char* Name;         // shown in the editor ("Olov")
            const char* Description;
        };
        static const std::vector<Info>& GetBuiltins();
        // The built-in preset of `key` (nothing for an unknown key).
        static std::optional<ParticlePreset> GetBuiltin(std::string_view key);

        // `entity`'s Particle System as a preset (named after the entity; its sub-emitters' systems
        // become templates). An entity without one: an empty default preset.
        static ParticlePreset Capture(Scene& scene, Entity entity);
        // `entity`'s Particle System (added if it has none) takes the preset's settings; its
        // sorting layer and order stay. The templates are made as its children - the children its
        // old sub-emitters used go - and its sub-emitters point at them. The system starts over.
        // Returns the templates made.
        static std::vector<Entity> Apply(Scene& scene, Entity entity, const ParticlePreset& preset);
        // A new entity named after the preset at `position`, with it applied.
        static Entity Instantiate(Scene& scene, const ParticlePreset& preset, const glm::vec3& position = glm::vec3(0.0f));

        // JSON text (.zparticle), as the scenes write particle systems (SceneSerializer.cpp).
        static std::string ToString(const ParticlePreset& preset);
        // False (and why in `error`) for text that is not a particle preset.
        static bool FromString(const std::string& text, ParticlePreset& out, std::string* error = nullptr);
        static bool Save(const std::filesystem::path& file, const ParticlePreset& preset);
        static bool Load(const std::filesystem::path& file, ParticlePreset& out, std::string* error = nullptr);

        // A system's settings without what it holds while it runs (particles, trails, events,
        // texture, mesh) and its random generator at the start.
        static void ClearRuntime(ParticleSystemComponent& ps);
    };

}
