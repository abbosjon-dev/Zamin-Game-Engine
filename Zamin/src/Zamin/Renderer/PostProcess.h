#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Renderer/Framebuffer.h"

#include <glm/glm.hpp>

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

        float Vignette = 0.0f;   // 0..1
        float Saturation = 1.0f;
        float Contrast = 1.0f;
        float Brightness = 0.0f; // -1..1
        glm::vec3 Tint{ 1.0f };
    };

    // Screen-space effects applied to a rendered color texture:
    // bloom (threshold + mip-chain blur), vignette, color grading.
    class PostProcessor
    {
    public:
        static void Init();
        static void Shutdown();

        // Reads `inputTexture` (w x h) and writes the result into `target`
        // (or the currently bound framebuffer when target is null).
        static void Apply(uint32_t inputTexture, uint32_t width, uint32_t height, const PostProcessSettings& settings,
                          const Ref<Framebuffer>& target);

        // Input is linear HDR; output is display (sRGB) color.
    };

}
