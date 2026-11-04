#pragma once

#include <string>
#include <string_view>

namespace Zamin {

    enum class ScriptTemplateKind
    {
        Full = 0,   // every callback with a short example, and every kind of inspector field
        Minimal,    // OnCreate + OnUpdate and one field
    };

    // Source code for new C++ script classes (Editor: Skriptlar > Yangi C++ skript).
    namespace ScriptTemplate {

        // Keeps letters, digits and '_' ("my enemy-2" -> "myenemy2"); a leading digit gets a '_' prefix.
        std::string SanitizeClassName(std::string_view name);
        // A usable class name: a C++ identifier that is not a keyword and does not hide an engine
        // name the generated code uses (Entity, Input, Transform ...).
        bool IsValidClassName(std::string_view name);
        // Human readable reason (Uzbek) why the name cannot be used; empty when it is valid.
        std::string ValidateClassName(std::string_view name);

        // Complete .cpp file text for `className` (which must be valid).
        std::string Generate(std::string_view className, ScriptTemplateKind kind = ScriptTemplateKind::Full);

        // Source/Scripts.cpp of a brand new project (a Rotator and a PlayerController).
        std::string GenerateProjectStarter();

        const char* GetKindName(ScriptTemplateKind kind);
        const char* GetKindDescription(ScriptTemplateKind kind);

    }

}
