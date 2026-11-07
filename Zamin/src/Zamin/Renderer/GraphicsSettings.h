#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Whether the OpenGL context is made with debug output (the driver's messages in the log).
    enum class GraphicsDebugContext
    {
        Auto = 0, // Debug builds (NDEBUG not defined) only: what the engine always did
        On,       // also in Release builds (slower: the driver checks every call)
        Off       // never
    };
    const char* GraphicsDebugContextToString(GraphicsDebugContext mode);
    bool GraphicsDebugContextFromString(std::string_view text, GraphicsDebugContext& out);

    // The OpenGL context and the window's framebuffer the game asks for (Project settings > Graphics).
    // Applied by ZaminPlayer when it creates its window (the editor keeps its own: 4.1 core, no MSAA);
    // its command line (--gl 4.6, --msaa 4, --gl-debug) wins over them. Projects without a "Graphics"
    // section get the defaults: OpenGL 4.1 core, debug output in Debug builds, no MSAA, no sRGB
    // framebuffer - what the player always did. A context the driver cannot make falls back to the defaults.
    struct GraphicsSettings
    {
        // The core profile version: 4.1 (what the engine needs; the most macOS has), 4.3, 4.5 or 4.6. A
        // newer one only asks the driver for more (the engine's own rendering is the same).
        int GLMajor = 4, GLMinor = 1;
        GraphicsDebugContext DebugContext = GraphicsDebugContext::Auto;
        // Samples per pixel of the window's own framebuffer (0 = off, 2, 4, 8, 16): what is drawn straight on
        // the screen (the UI over a pixel-perfect game, debug lines, the error screen) gets smooth edges.
        // The 3D picture is anti-aliased by FXAA (Quality).
        int MSAASamples = 0;
        // The window's framebuffer encodes the final picture to sRGB in hardware (the exact sRGB curve
        // instead of the shader's gamma 2.2: slightly different only in the darkest tones).
        bool SRGBFramebuffer = false;

        struct Version
        {
            int Major = 4, Minor = 1;
            const char* Label = "4.1";
        };
        // The versions offered (4.1, 4.3, 4.5, 4.6).
        static const std::vector<Version>& GetSupportedVersions();
        static constexpr int MaxMSAASamples = 16;
        static constexpr int DefaultMajor = 4, DefaultMinor = 1;

        bool operator==(const GraphicsSettings& other) const = default;
        bool IsDefault() const { return *this == GraphicsSettings(); }
        // An unknown version -> 4.1, the samples to 0 or a power of two up to MaxMSAASamples.
        void Sanitize();

        // "4.6"
        std::string GetVersionString() const;
        // "4.6" / "46" / "4.6 core" -> 4, 6 (only the supported versions); false otherwise.
        static bool ParseVersion(std::string_view text, int& major, int& minor);
        // DebugContext with Auto resolved for this build.
        bool WantsDebugContext() const;
        // A sample count as the window can have it (1 -> 0, 3 -> 2, 5 -> 4 ...).
        static int SanitizeSamples(int samples);

        // The "Graphics" section of a .zproject (a JSON object, every value in a fixed order) and back.
        // FromJson keeps the defaults for missing / wrong values; false when the text is not a JSON object.
        std::string ToJson() const;
        static bool FromJson(const std::string& text, GraphicsSettings& out);
    };

    // The context the window got (filled when it is made): the editor's Graphics page and the log show it.
    struct GraphicsContextInfo
    {
        bool Valid = false;                    // a context was made (false headless)
        std::string Vendor, Renderer, Version, ShadingLanguage;
        int Major = 0, Minor = 0;              // the version the context really has
        int RequestedMajor = GraphicsSettings::DefaultMajor, RequestedMinor = GraphicsSettings::DefaultMinor;
        bool FellBack = false;                 // the asked context could not be made: the defaults instead
        bool CoreProfile = true;
        bool DebugContext = false;             // made with the debug flag
        bool DebugOutput = false;              // the driver's messages reach the log
        int Samples = 0;                       // MSAA samples of the window's framebuffer
        int RequestedSamples = 0;
        bool SRGB = false;                     // the window's framebuffer encodes to sRGB (and it is used)
        bool RequestedSRGB = false;
        int MaxTextureSize = 0;
        int MaxSamples = 0;                    // the most MSAA samples the driver offers
        float MaxAnisotropy = 0.0f;            // 0: no anisotropic filtering

        // "OpenGL 4.6 core (debug, MSAA 4x, sRGB)"
        std::string Summary() const;
    };

}
