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

        bool SSAO = false;       // screen-space ambient occlusion (3D)
        float SSAORadius = 0.6f;
        float SSAOIntensity = 1.0f;
        float SSAOBias = 0.03f;

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
        // depthTexture/projection are needed for SSAO (pass 0 / identity to skip it).
        static void Apply(uint32_t inputTexture, uint32_t width, uint32_t height, const PostProcessSettings& settings,
                          const Ref<Framebuffer>& target, uint32_t depthTexture = 0, const glm::mat4& projection = glm::mat4(1.0f));

        // Input is linear HDR; output is display (sRGB) color.
    };

}
