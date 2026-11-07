#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Utils/Image.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace Zamin {

    class Texture2D;
    class Font;

    // The splash screen's timing: the picture fades in, stays, and fades out - not before what comes after
    // it (the first scene) is loaded: it stays up longer meanwhile. A skip (any key, when skippable)
    // ends it with a short fade as soon as that is loaded.
    class SplashTimeline
    {
    public:
        enum class Phase { FadeIn = 0, Hold, Waiting, FadeOut, Done };

        static constexpr float FadeInSeconds = 0.5f, FadeOutSeconds = 0.5f, SkipFadeSeconds = 0.2f;

        SplashTimeline() = default;
        // `duration`: seconds from the start to the end of the fade out, when the scene is loaded by then
        // (PlayerSettings::MinSplashDuration ... MaxSplashDuration; the fades are shorter for short ones).
        explicit SplashTimeline(float duration, bool skippable = true);

        // One frame of `dt` seconds. `ready`: what comes after is loaded (the splash may end); `skip`: the
        // player pressed a key / button this frame.
        void Update(float dt, bool ready, bool skip);

        Phase GetPhase() const { return m_Phase; }
        // The picture's opacity (0 ... 1).
        float GetAlpha() const;
        bool IsDone() const { return m_Phase == Phase::Done; }
        float GetElapsed() const { return m_Time; }
        float GetDuration() const { return m_Duration; }
        bool WasSkipped() const { return m_Skipped; }

    private:
        void StartFadeOut(float seconds);

        float m_Duration = 2.0f, m_FadeIn = FadeInSeconds, m_FadeOut = FadeOutSeconds;
        bool m_Skippable = true;
        Phase m_Phase = Phase::FadeIn;
        float m_Time = 0.0f;
        float m_FadeStart = 0.0f, m_FadeLength = FadeOutSeconds, m_FadeFrom = 1.0f;
        bool m_SkipRequested = false, m_Skipped = false;
    };

    namespace SplashLogo {

        // The Zamin logo, drawn here (no file needed): a disc with sky and green hills, a bold white "Z"
        // over it and a light ring. size x size RGBA8, rows top first, straight alpha (transparent
        // around the disc), anti-aliased edges.
        ImageData Generate(uint32_t size = 512);
        // The logo as a texture (needs a GL context).
        Ref<Texture2D> CreateTexture(uint32_t size = 512);

    }

    // The player's splash screen: the Zamin logo with "ZAMIN ENGINE" under it (or the game's own picture)
    // fading in and out over a colour, while the first scene loads in the background, with a thin
    // progress bar when the load takes longer than the splash.
    class SplashScreen
    {
    public:
        // `image`: the game's picture (a file; empty: the Zamin logo). False (and the logo) when it cannot be read.
        bool Begin(float duration, bool skippable, const glm::vec3& background, const std::filesystem::path& image = {});
        void Update(float dt, bool ready, bool skip) { m_Timeline.Update(dt, ready, skip); }
        // Draws it into the bound default framebuffer (width x height pixels). `progress` (0 ... 1) of the
        // load: shown while the splash waits for it.
        void Render(uint32_t width, uint32_t height, float progress) const;

        bool IsActive() const { return m_Active; }
        bool IsDone() const { return m_Timeline.IsDone(); }
        void End() { m_Active = false; m_Texture = nullptr; }
        const SplashTimeline& GetTimeline() const { return m_Timeline; }
        const glm::vec3& GetBackground() const { return m_Background; }

    private:
        SplashTimeline m_Timeline;
        Ref<Texture2D> m_Texture;
        bool m_IsLogo = true;
        bool m_Active = false;
        glm::vec3 m_Background{ 0.0f };
    };

}
