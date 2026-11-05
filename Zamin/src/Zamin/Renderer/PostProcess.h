#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/Framebuffer.h"

#include <glm/glm.hpp>
#include <vector>

namespace Zamin {

    enum class TonemapMode { None = 0, ACES, Reinhard };

    struct PostProcessSettings
    {
        TonemapMode Tonemap = TonemapMode::ACES;
        bool Bloom = false;
        float BloomThreshold = 1.0f;
        float BloomKnee = 0.5f;
        float BloomIntensity = 0.6f;
        float BloomRadius = 1.0f;
        // The glow's colour: the bloom (which has the colour of the light it comes from) is
        // multiplied by it - white keeps it as it is, a warm tint gives a golden glow.
        glm::vec3 BloomTint{ 1.0f };
        // Lens dirt: the wide glow of bright lights also lights up smudges and specks of a dirty
        // lens (a texture made once, procedurally) - 0 none .. 1 strong.
        float BloomDirt = 0.0f;

        bool SSAO = false;       // screen-space ambient occlusion (3D)
        float SSAORadius = 0.6f;
        float SSAOIntensity = 1.0f;
        float SSAOBias = 0.03f;

        float Vignette = 0.0f;   // 0..1
        // Chromatic aberration: red and blue are split apart towards the edges of the picture, like
        // the colour fringes of a cheap lens (0 off .. 1 strong).
        float ChromaticAberration = 0.0f;
        // Film grain: animated noise over the final picture, strongest in the mid tones (0 off ..
        // 1 strong); FilmGrainSize: the size of a grain in output pixels (1 .. 4).
        float FilmGrain = 0.0f;
        float FilmGrainSize = 1.0f;
        float Saturation = 1.0f;
        float Contrast = 1.0f;
        float Brightness = 0.0f; // -1..1
        glm::vec3 Tint{ 1.0f };

        // Fast approximate anti-aliasing: smooths jagged edges of the final (tonemapped, graded)
        // picture. FXAASubpixel: how much single-pixel detail (thin lines, speculars) is softened, 0..1.
        bool FXAA = false;
        float FXAASubpixel = 0.75f;

        // Auto exposure (eye adaptation): the average brightness of the HDR picture (the mean of
        // log2 luminance, measured on the GPU through a chain of halving downsamples) is brought
        // to middle grey, smoothly over time. Off by default: only the manual exposure
        // (SceneSettings::Exposure). The measured brightness is held within MinEV..MaxEV (log2
        // luminance): darker scenes stay dark, brighter ones stay bright.
        bool AutoExposure = false;
        float AutoExposureCompensation = 0.0f; // EV: +1 = twice as bright, -1 = half
        float AutoExposureMinEV = -4.0f;
        float AutoExposureMaxEV = 4.0f;
        float AutoExposureSpeed = 2.0f;        // adaptation rate (1/s): higher = faster

        // Settings that leave the picture as it is (no tonemapping, effects or grading; only the
        // gamma encoding): debug views that show values (normals, albedo ...).
        static PostProcessSettings Neutral();

        static constexpr float AutoExposureKey = 0.18f; // middle grey the average is brought to
        static constexpr float MinAutoExposureEV = -12.0f, MaxAutoExposureEV = 12.0f;

        // The exposure multiplier auto exposure applies when the picture's average luminance
        // (geometric mean) is 2^averageEV: the key over the clamped average, times the compensation.
        float GetAutoExposureMultiplier(float averageEV) const;
        // One adaptation step from `currentEV` towards `targetEV` after `deltaTime` seconds
        // (exponential: 1 - e^(-dt * speed) of the way; dt <= 0 or speed <= 0: no change, a
        // non-finite current value: the target at once).
        static float AdaptExposure(float currentEV, float targetEV, float deltaTime, float speed);
        // Values out of range (or not finite) back into range: MinEV <= MaxEV, speed > 0.
        void SanitizeAutoExposure();
        // Chromatic aberration, film grain, bloom tint and lens dirt brought into range (anything not
        // finite to its default, the tint not negative).
        void SanitizeLensEffects();
        static constexpr float MaxFilmGrainSize = 4.0f;
    };

    // What a view kept from its earlier frames: auto exposure's adapted brightness. One per view
    // that is post-processed every frame (the editor viewport, the player's screen); a view
    // without one (a thumbnail, a material preview) measures every picture on its own.
    class PostProcessHistory
    {
    public:
        // Starts over (the next frame adapts at once instead of fading from the old brightness).
        void Reset() { m_Valid = false; }
        // The adapted average brightness (log2 luminance) of the last frame - read back from the
        // GPU (a small stall, editor display only). False when auto exposure has not run.
        bool ReadAdaptedEV(float& outEV) const;

    private:
        friend class PostProcessor;
        Ref<Framebuffer> m_Adapted[2]; // 1x1, ping-pong: last frame's value -> this frame's
        int m_Current = 0;
        bool m_Valid = false;
        uint32_t m_Frame = 0;          // frames post-processed: film grain changes every frame
    };

    // One picture to post-process (besides the settings).
    struct PostProcessInput
    {
        uint32_t ColorTexture = 0;          // linear HDR scene picture
        uint32_t Width = 0, Height = 0;     // its size (the render resolution)
        uint32_t DepthTexture = 0;          // its depth for SSAO (0: no SSAO)
        glm::mat4 Projection{ 1.0f };       // the camera's projection (SSAO; identity: none)
        // The size written (the target's, or the bound framebuffer's viewport). 0: the input's
        // size. Another size scales the picture there (linear filtering) - the render scale.
        uint32_t OutputWidth = 0, OutputHeight = 0;
        float DeltaTime = 0.0f;             // seconds since this view's last frame (adaptation)
        PostProcessHistory* History = nullptr;
    };

    // Screen-space effects applied to a rendered color texture: SSAO, bloom (threshold +
    // mip-chain blur), auto exposure, tonemapping, vignette, color grading, then FXAA. The
    // picture is scaled to the output size in the composite pass.
    class PostProcessor
    {
    public:
        static void Init();
        static void Shutdown();

        // Reads `inputTexture` (w x h) and writes the result into `target`
        // (or the currently bound framebuffer when target is null).
        // depthTexture/projection are needed for SSAO (pass 0 / identity to skip it).
        static void Apply(uint32_t inputTexture, uint32_t width, uint32_t height, const PostProcessSettings& settings,
                          const Ref<Framebuffer>& target, uint32_t depthTexture = 0, const glm::mat4& projection = glm::mat4(1.0f));
        // The same with an output size of its own (render scale) and a view history (auto exposure).
        static void Apply(const PostProcessInput& input, const PostProcessSettings& settings, const Ref<Framebuffer>& target);

        // The lens dirt the bloom lights up: `size` x `size` RGBA8 texels (rows top first), soft
        // specks and smudges of several sizes on black, the same for the same seed. No GPU needed.
        static std::vector<uint8_t> GenerateLensDirt(uint32_t size, uint32_t seed = 1);
        static constexpr uint32_t LensDirtSize = 512;

        // Input is linear HDR; output is display (sRGB) color.
    };

}
