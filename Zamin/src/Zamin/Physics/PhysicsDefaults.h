#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Zamin {

    // The project's physics defaults (Project settings > Physics): the gravity of every scene that does
    // not set its own (Scene settings > Physics) - new scenes and older ones that kept the engine's.
    // Projects without a "Physics" section get the engine's: (0, -9.8) in 2D, (0, -9.81, 0) in 3D.
    struct PhysicsDefaults
    {
        static constexpr glm::vec2 EngineGravity2D{ 0.0f, -9.8f };
        static constexpr glm::vec3 EngineGravity3D{ 0.0f, -9.81f, 0.0f };
        // The largest gravity component accepted (m/s^2; anything further is clamped).
        static constexpr float MaxGravity = 1000.0f;

        glm::vec2 Gravity2D = EngineGravity2D;
        glm::vec3 Gravity3D = EngineGravity3D;

        bool operator==(const PhysicsDefaults&) const = default;
        bool IsDefault() const { return *this == PhysicsDefaults(); }
        // NaNs to the engine's, the components into +-MaxGravity.
        void Sanitize();

        // "Earth", "Moon" ... (the presets of the settings window): the gravity's strength (m/s^2).
        struct Preset
        {
            const char* Name;
            float Strength;
        };
        static const Preset* GetPresets(int& count);

        // The "Physics" section of a .zproject (a JSON object) and back. FromJson keeps the engine's
        // values for missing / wrong ones; false when the text is not a JSON object.
        std::string ToJson() const;
        static bool FromJson(const std::string& text, PhysicsDefaults& out);
    };

    // Two gravities are the same (a value read back from a file may differ in its last bit).
    inline bool SameGravity(const glm::vec2& a, const glm::vec2& b) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec2(1e-5f))); }
    inline bool SameGravity(const glm::vec3& a, const glm::vec3& b) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(1e-5f))); }

}
