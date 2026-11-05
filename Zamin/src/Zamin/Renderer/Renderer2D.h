#pragma once

#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/RendererAPI.h"
#include "Zamin/Renderer/Frustum.h"

#include <glm/glm.hpp>
#include <string>

namespace Zamin {

    struct TextParams
    {
        glm::vec4 Color{ 1.0f };
        float Kerning = 0.0f;
        float LineSpacing = 0.0f;
    };

    // Batched 2D renderer: quads, sprites, circles, lines and text.
    class Renderer2D
    {
    public:
        static void Init();
        static void Shutdown();

        static void BeginScene(const glm::mat4& viewProjection);
        static void EndScene();
        static void Flush();

        // Quads
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture,
                             float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
        static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotationRadians, const glm::vec4& color);

        static void DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);
        static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor = 1.0f,
                             const glm::vec4& tintColor = glm::vec4(1.0f), int entityID = -1);
        // Sub-region of a texture (sprite sheets). uvMin/uvMax in [0,1].
        static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, const glm::vec2& uvMin,
                             const glm::vec2& uvMax, const glm::vec4& tintColor = glm::vec4(1.0f), int entityID = -1);

        static void DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness = 1.0f, float fade = 0.005f, int entityID = -1);

        static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID = -1);
        static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID = -1);
        static void DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);

        using TextParams = Zamin::TextParams;
        // Text is laid out with 1 unit = 1 line height, starting at the transform origin (baseline of the first line).
        static void DrawString(const std::string& text, const Ref<Font>& font, const glm::mat4& transform,
                               const TextParams& params, int entityID = -1);
        static glm::vec2 MeasureString(const std::string& text, const Ref<Font>& font, const TextParams& params = TextParams());

        // Frustum culling helpers (no GPU): the world-space boxes of what the draws cover.
        // The unit quad (-0.5 .. 0.5 in x and y: a sprite, a circle, a tile) moved by `transform`.
        static AABB GetQuadBounds(const glm::mat4& transform);
        // The box around every glyph quad DrawString lays out for `text`, in the text's own space
        // (before its transform); empty (Min > Max) when nothing would be drawn.
        static AABB GetStringBounds(const std::string& text, const Ref<Font>& font, const TextParams& params = TextParams());

        // Flushes the current batch and switches blending (e.g. additive particles).
        static void SetBlendMode(BlendMode mode);

        static float GetLineWidth();
        static void SetLineWidth(float width);

    private:
        static void StartBatch();
        static void NextBatch();
    };

}
