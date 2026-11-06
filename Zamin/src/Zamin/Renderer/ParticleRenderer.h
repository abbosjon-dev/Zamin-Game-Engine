#pragma once

#include "Zamin/Renderer/Texture.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Zamin {

    // One particle of an instanced draw: a quad around Position, Size wide / high along the draw's
    // two axes, turned by Rotation (radians) in its own plane, showing UVRect of the texture.
    struct ParticleInstance
    {
        glm::vec3 Position{ 0.0f };
        float Rotation = 0.0f;
        glm::vec2 Size{ 1.0f };
        glm::vec4 Color{ 1.0f };                      // as the 2D renderer takes it (sRGB, alpha)
        glm::vec4 UVRect{ 0.0f, 0.0f, 1.0f, 1.0f };   // min.xy, max.xy
    };

    // Particle quads drawn with GPU instancing: one draw call per system (the vertex shader makes
    // the four corners of each instance) instead of four vertices per particle through the 2D batch.
    // They look as Renderer2D draws them - a quad as its texture shader (without sprite effects or
    // 2D lights), a circle as its soft circle - in the scene Renderer2D::BeginScene opened (its
    // camera, its output). Blending, depth and scissor are the caller's (the GL state as it is).
    class ParticleRenderer
    {
    public:
        enum class Shape { Quad = 0, Circle };

        struct DrawParams
        {
            Shape Kind = Shape::Quad;
            Ref<Texture2D> Texture;                   // quads; null: plain colour
            glm::vec3 Right{ 1.0f, 0.0f, 0.0f };      // the quads' X and Y in the world (billboards: the camera's)
            glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
            float CircleThickness = 1.0f;             // circles: as Renderer2D::DrawCircle
            float CircleFade = 0.6f;
            int EntityID = -1;
        };

        static void Init();
        static void Shutdown();
        // False before Init (no GL: tests) - draw through Renderer2D then.
        static bool IsAvailable();

        // Draws what Renderer2D has pending first (the order stays), then the instances in as few
        // calls as the instance buffer allows (one for up to MaxInstancesPerDraw).
        static void Draw(const ParticleInstance* instances, uint32_t count, const DrawParams& params);

        static constexpr uint32_t MaxInstancesPerDraw = 16384;
    };

}
