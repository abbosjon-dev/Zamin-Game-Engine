#pragma once

#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/RendererAPI.h"
#include "Zamin/Renderer/Frustum.h"
#include "Zamin/Renderer/Lighting2D.h"

#include <glm/glm.hpp>
#include <string>
#include <string_view>

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
        // Per-quad shader effects of a sprite (SpriteRendererComponent: hit flash, outline).
        struct SpriteEffect
        {
            glm::vec4 FlashColor{ 1.0f };   // rgb; FlashAmount 0..1 mixes the picture toward it
            float FlashAmount = 0.0f;
            glm::vec4 OutlineColor{ 1.0f };
            float OutlineThickness = 0.0f;  // texture pixels around the opaque pixels (0: none)

            bool HasFlash() const { return FlashAmount > 0.0f; }
            bool HasOutline() const { return OutlineThickness > 0.0f && OutlineColor.a > 0.0f; }
            bool IsActive() const { return HasFlash() || HasOutline(); }
        };

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

        // A sprite quad with effects. `uvRect` (min.xy, max.xy): the picture's own UV rectangle - nothing
        // outside it is sampled, and an outline needs the quad grown beyond it (GrowSpriteQuad) so
        // it is not cut at the picture's edge. A null texture: a plain quad (flash only).
        static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, const glm::vec2& uvMin, const glm::vec2& uvMax,
                             const glm::vec4& tintColor, const SpriteEffect& effect, const glm::vec4& uvRect, int entityID = -1);

        // A quad with corners of its own (counter-clockwise; a triangle repeats its last corner) and
        // their UVs, at depth `z` (UI fills, radial fills). A null texture: a plain colour.
        static void DrawQuadCorners(const glm::vec2 positions[4], const glm::vec2 uvs[4], const Ref<Texture2D>& texture,
                                    const glm::vec4& tintColor, int entityID = -1, float z = 0.0f);

        static void DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness = 1.0f, float fade = 0.005f, int entityID = -1);

        static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID = -1);
        static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID = -1);
        static void DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);

        using TextParams = Zamin::TextParams;
        // Text is laid out with 1 unit = 1 line height, starting at the transform origin (baseline of the first line).
        // The glyphs come from the font's atlas baked nearest the size one em of the text has on the
        // target (GetTextPixelSize), so text stays crisp at any size.
        static void DrawString(std::string_view text, const Ref<Font>& font, const glm::mat4& transform,
                               const TextParams& params, int entityID = -1);
        static glm::vec2 MeasureString(std::string_view text, const Ref<Font>& font, const TextParams& params = TextParams());
        // Pixels of the target one text unit (1 em) placed by `transform` covers with the current
        // scene's camera (0: unknown - no target size, or behind the camera).
        static float GetTextPixelSize(const glm::mat4& transform);

        // Frustum culling helpers (no GPU): the world-space boxes of what the draws cover.
        // The unit quad (-0.5 .. 0.5 in x and y: a sprite, a circle, a tile) moved by `transform`.
        static AABB GetQuadBounds(const glm::mat4& transform);
        // The box around every glyph quad DrawString lays out for `text`, in the text's own space
        // (before its transform); empty (Min > Max) when nothing would be drawn.
        static AABB GetStringBounds(std::string_view text, const Ref<Font>& font, const TextParams& params = TextParams());

        // Draws only inside a rectangle of the viewport (min01 / max01: 0..1 of it, origin bottom-left)
        // from now on - UI clipping; what is pending is drawn first. Off: everywhere again.
        static void SetClipRect(bool enabled, const glm::vec2& min01 = glm::vec2(0.0f), const glm::vec2& max01 = glm::vec2(1.0f));

        // Flushes the current batch and switches blending (e.g. additive particles).
        static void SetBlendMode(BlendMode mode);

        // 2D lighting: quads (sprites, tiles, text) submitted while SetLit(true) is on are multiplied
        // by the light that reaches them (the ambient plus every light, at most
        // Light2DMath::MaxLights); the others are drawn as they are. A change of the lights draws
        // what is pending first. Lit is off until it is switched on.
        static void SetLighting(const Lighting2DData& lighting);
        static const Lighting2DData& GetLighting();
        static void SetLit(bool lit);
        static bool IsLit();
        // Off (default): linear colours for the HDR scene picture that post-processing finishes.
        // On: display (sRGB) colours, for drawing over the finished picture (a pixel-perfect game's
        // screen-space UI at the screen's resolution).
        static void SetDisplayOutput(bool display);

        static float GetLineWidth();
        static void SetLineWidth(float width);

    private:
        static void StartBatch();
        static void NextBatch();
    };

}
