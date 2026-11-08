#pragma once

/// @file
/// @brief Renderer2D: the batched 2D renderer (quads, sprites, circles, lines, text).

#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/RendererAPI.h"
#include "Zamin/Renderer/Frustum.h"
#include "Zamin/Renderer/Lighting2D.h"

#include <glm/glm.hpp>
#include <string>
#include <string_view>

namespace Zamin {

    /// How Renderer2D::DrawString draws a text.
    struct TextParams
    {
        glm::vec4 Color{ 1.0f };   ///< RGBA as a colour picker shows it (sRGB; the shader makes it linear)
        float Kerning = 0.0f;      ///< extra space between the glyphs (em)
        float LineSpacing = 0.0f;  ///< extra space between the lines (em)
    };

    /// Batched 2D renderer: quads, sprites, circles, lines and text.
    ///
    /// Everything between BeginScene and EndScene goes into vertex batches (one each for quads,
    /// circles and lines) that are drawn when they are full, when a setting that needs another draw
    /// changes (blend mode, clip rectangle, lighting, too many textures) and at EndScene. The scene
    /// (Scene::RenderRuntime) draws its sprites, tilemaps, texts, 2D particles and the UI through it;
    /// a layer can draw directly:
    /// @code
    /// Zamin::Renderer2D::BeginScene(camera.GetViewProjection());
    /// Zamin::Renderer2D::DrawQuad(glm::vec2(0.0f), glm::vec2(1.0f), { 0.9f, 0.3f, 0.2f, 1.0f });
    /// Zamin::Renderer2D::DrawLine({ -1, 0, 0 }, { 1, 0, 0 }, { 1, 1, 1, 1 });
    /// Zamin::Renderer2D::EndScene();
    /// @endcode
    /// `entityID` (the editor's picking buffer) is -1 for draws that are no entity. Main thread only.
    class Renderer2D
    {
    public:
        /// Per-quad shader effects of a sprite (SpriteRendererComponent: hit flash, outline).
        struct SpriteEffect
        {
            glm::vec4 FlashColor{ 1.0f };   ///< rgb; FlashAmount 0..1 mixes the picture toward it
            float FlashAmount = 0.0f;
            glm::vec4 OutlineColor{ 1.0f };
            float OutlineThickness = 0.0f;  ///< texture pixels around the opaque pixels (0: none)

            bool HasFlash() const { return FlashAmount > 0.0f; }
            bool HasOutline() const { return OutlineThickness > 0.0f && OutlineColor.a > 0.0f; }
            bool IsActive() const { return HasFlash() || HasOutline(); }
        };

        /// Creates the batches, shaders and the white texture (Renderer::Init calls it).
        static void Init();
        static void Shutdown();

        /// Starts a scene seen through `viewProjection` (projection x view) in the bound framebuffer's viewport.
        static void BeginScene(const glm::mat4& viewProjection);
        /// Draws what is still batched (Flush).
        static void EndScene();
        /// Draws the pending quads, circles and lines now.
        static void Flush();

        /// @name Quads
        /// A quad is the unit square (-0.5 .. 0.5) moved by its transform; colours are RGBA as a colour
        /// picker shows them (sRGB: the shaders make them linear for the HDR picture).
        /// @{
        /// An axis-aligned quad centred on `position` (z = 0).
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
        /// An axis-aligned quad centred on `position`.
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
        /// A textured quad; `tilingFactor` repeats the texture across it, `tintColor` multiplies it.
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture,
                             float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
        /// A quad turned by `rotationRadians` around its centre (the z axis).
        static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotationRadians, const glm::vec4& color);
        /// A quad placed by a full transform.
        static void DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);
        /// A textured quad placed by a full transform.
        static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor = 1.0f,
                             const glm::vec4& tintColor = glm::vec4(1.0f), int entityID = -1);
        /// Sub-region of a texture (sprite sheets). uvMin/uvMax in [0,1].
        static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, const glm::vec2& uvMin,
                             const glm::vec2& uvMax, const glm::vec4& tintColor = glm::vec4(1.0f), int entityID = -1);

        /// A sprite quad with effects. `uvRect` (min.xy, max.xy): the picture's own UV rectangle - nothing
        /// outside it is sampled, and an outline needs the quad grown beyond it (GrowSpriteQuad) so
        /// it is not cut at the picture's edge. A null texture: a plain quad (flash only).
        static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, const glm::vec2& uvMin, const glm::vec2& uvMax,
                             const glm::vec4& tintColor, const SpriteEffect& effect, const glm::vec4& uvRect, int entityID = -1);

        /// A quad with corners of its own (counter-clockwise; a triangle repeats its last corner) and
        /// their UVs, at depth `z` (UI fills, radial fills). A null texture: a plain colour.
        static void DrawQuadCorners(const glm::vec2 positions[4], const glm::vec2 uvs[4], const Ref<Texture2D>& texture,
                                    const glm::vec4& tintColor, int entityID = -1, float z = 0.0f);

        /// A quad of any four corners in 3D (in order around it; a ribbon's segment: particle trails)
        /// with a colour and a UV each. A null texture: plain colours.
        static void DrawQuadVertices(const glm::vec3 positions[4], const glm::vec4 colors[4], const glm::vec2 uvs[4], const Ref<Texture2D>& texture,
                                     int entityID = -1);

        /// @}

        /// A circle filling the unit quad moved by `transform`: `thickness` 1 = a disc, smaller = a ring
        /// (that part of the radius); `fade` softens its edge.
        static void DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness = 1.0f, float fade = 0.005f, int entityID = -1);

        /// A line from p0 to p1 (GetLineWidth pixels wide).
        static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID = -1);
        /// The outline of an axis-aligned rectangle centred on `position` (four lines).
        static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID = -1);
        /// The outline of the unit quad moved by `transform` (four lines).
        static void DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);

        using TextParams = Zamin::TextParams;
        /// Text is laid out with 1 unit = 1 line height, starting at the transform origin (baseline of the first line).
        /// The glyphs come from the font's atlas baked nearest the size one em of the text has on the
        /// target (GetTextPixelSize), so text stays crisp at any size.
        static void DrawString(std::string_view text, const Ref<Font>& font, const glm::mat4& transform,
                               const TextParams& params, int entityID = -1);
        /// The size DrawString would give `text` (in text units: 1 = one line height).
        static glm::vec2 MeasureString(std::string_view text, const Ref<Font>& font, const TextParams& params = TextParams());
        /// Pixels of the target one text unit (1 em) placed by `transform` covers with the current
        /// scene's camera (0: unknown - no target size, or behind the camera).
        static float GetTextPixelSize(const glm::mat4& transform);

        /// Frustum culling helpers (no GPU): the world-space boxes of what the draws cover.
        /// The unit quad (-0.5 .. 0.5 in x and y: a sprite, a circle, a tile) moved by `transform`.
        static AABB GetQuadBounds(const glm::mat4& transform);
        /// The box around every glyph quad DrawString lays out for `text`, in the text's own space
        /// (before its transform); empty (Min > Max) when nothing would be drawn.
        static AABB GetStringBounds(std::string_view text, const Ref<Font>& font, const TextParams& params = TextParams());

        /// Draws only inside a rectangle of the viewport (min01 / max01: 0..1 of it, origin bottom-left)
        /// from now on - UI clipping; what is pending is drawn first. Off: everywhere again.
        static void SetClipRect(bool enabled, const glm::vec2& min01 = glm::vec2(0.0f), const glm::vec2& max01 = glm::vec2(1.0f));

        /// Flushes the current batch and switches blending (e.g. additive particles).
        static void SetBlendMode(BlendMode mode);

        /// 2D lighting: quads (sprites, tiles, text) submitted while SetLit(true) is on are multiplied
        /// by the light that reaches them (the ambient plus every light, at most
        /// Light2DMath::MaxLights); the others are drawn as they are. A change of the lights draws
        /// what is pending first. Lit is off until it is switched on.
        static void SetLighting(const Lighting2DData& lighting);
        static const Lighting2DData& GetLighting();
        static void SetLit(bool lit);
        static bool IsLit();
        /// Off (default): linear colours for the HDR scene picture that post-processing finishes.
        /// On: display (sRGB) colours, for drawing over the finished picture (a pixel-perfect game's
        /// screen-space UI at the screen's resolution).
        static void SetDisplayOutput(bool display);
        static bool IsDisplayOutput();
        /// The camera of the scene BeginScene opened (other renderers drawing into it: particles).
        static const glm::mat4& GetViewProjection();

        static float GetLineWidth();          ///< the width lines are drawn with (pixels)
        static void SetLineWidth(float width);

    private:
        static void StartBatch();
        static void NextBatch();
    };

}
