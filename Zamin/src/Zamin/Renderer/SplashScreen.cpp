#include "Zamin/Renderer/SplashScreen.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Texture.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Zamin {

    // ------------------------------------------------------------------ timeline
    SplashTimeline::SplashTimeline(float duration, bool skippable)
    {
        m_Duration = std::isfinite(duration) ? std::clamp(duration, 0.5f, 10.0f) : 2.0f;
        m_FadeIn = std::min(FadeInSeconds, m_Duration * 0.3f);
        m_FadeOut = std::min(FadeOutSeconds, m_Duration * 0.3f);
        m_FadeLength = m_FadeOut;
        m_Skippable = skippable;
    }

    void SplashTimeline::StartFadeOut(float seconds)
    {
        m_FadeFrom = GetAlpha();
        m_FadeStart = m_Time;
        m_FadeLength = std::max(0.0f, seconds);
        m_Phase = Phase::FadeOut;
    }

    void SplashTimeline::Update(float dt, bool ready, bool skip)
    {
        if (m_Phase == Phase::Done)
            return;
        m_Time += std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
        if (skip && m_Skippable && m_Phase != Phase::FadeOut)
            m_SkipRequested = true;
        // A skip ends it as soon as the scene is there (from whatever it shows now).
        if (m_SkipRequested && ready && m_Phase != Phase::FadeOut)
        {
            m_Skipped = true;
            StartFadeOut(SkipFadeSeconds);
        }
        if (m_Phase == Phase::FadeIn && m_Time >= m_FadeIn)
            m_Phase = Phase::Hold;
        if (m_Phase == Phase::Hold && m_Time >= m_Duration - m_FadeOut)
            m_Phase = Phase::Waiting;
        if (m_Phase == Phase::Waiting && ready)
            StartFadeOut(m_FadeOut);
        if (m_Phase == Phase::FadeOut && m_Time - m_FadeStart >= m_FadeLength)
            m_Phase = Phase::Done;
    }

    float SplashTimeline::GetAlpha() const
    {
        auto smooth = [](float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        switch (m_Phase)
        {
            case Phase::FadeIn:  return m_FadeIn > 0.0f ? smooth(m_Time / m_FadeIn) : 1.0f;
            case Phase::Hold:
            case Phase::Waiting: return 1.0f;
            case Phase::FadeOut: return m_FadeLength > 0.0f ? m_FadeFrom * (1.0f - smooth((m_Time - m_FadeStart) / m_FadeLength)) : 0.0f;
            case Phase::Done:    return 0.0f;
        }
        return 0.0f;
    }

    // ------------------------------------------------------------------ the logo
    namespace SplashLogo {

        namespace {

            float RoundedBox(glm::vec2 p, glm::vec2 center, glm::vec2 halfSize, float radius)
            {
                const glm::vec2 q = glm::abs(p - center) - halfSize + glm::vec2(radius);
                return glm::length(glm::max(q, glm::vec2(0.0f))) + std::min(std::max(q.x, q.y), 0.0f) - radius;
            }

            float Capsule(glm::vec2 p, glm::vec2 a, glm::vec2 b, float radius)
            {
                const glm::vec2 pa = p - a, ba = b - a;
                const float h = std::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
                return glm::length(pa - ba * h) - radius;
            }

            // The "Z": two bars and the diagonal between them.
            float LetterZ(glm::vec2 p)
            {
                const float top = RoundedBox(p, { 0.0f, 0.385f }, { 0.43f, 0.085f }, 0.035f);
                const float bottom = RoundedBox(p, { 0.0f, -0.385f }, { 0.43f, 0.085f }, 0.035f);
                const float diagonal = Capsule(p, { 0.335f, 0.33f }, { -0.335f, -0.33f }, 0.1f);
                return std::min({ top, bottom, diagonal });
            }

            // The hills' top edge at x.
            float Horizon(float x) { return -0.2f + 0.07f * std::sin(2.6f * x + 0.6f) + 0.035f * std::sin(5.3f * x + 1.9f); }

            glm::vec3 Mix(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }

        }

        ImageData Generate(uint32_t size)
        {
            ImageData image;
            size = std::clamp(size, 16u, 2048u);
            image.Width = image.Height = size;
            image.Channels = image.FileChannels = 4;
            image.Pixels.resize((size_t)size * size * 4);
            const float pixel = 2.0f / (float)size; // one pixel in the logo's -1 ... 1 space
            auto coverage = [&](float distance, float softness = 1.0f) {
                return std::clamp(0.5f - distance / (pixel * softness), 0.0f, 1.0f);
            };
            const glm::vec3 skyTop(0.09f, 0.42f, 0.60f), skyLow(0.36f, 0.76f, 0.80f);
            const glm::vec3 landTop(0.27f, 0.68f, 0.38f), landLow(0.10f, 0.40f, 0.26f);
            const glm::vec3 letter(0.97f, 0.98f, 0.95f), ring(1.0f, 1.0f, 1.0f);
            for (uint32_t y = 0; y < size; y++)
                for (uint32_t x = 0; x < size; x++)
                {
                    // y up, the middle at 0.
                    const glm::vec2 p((((float)x + 0.5f) / (float)size) * 2.0f - 1.0f, 1.0f - (((float)y + 0.5f) / (float)size) * 2.0f);
                    const float radius = glm::length(p);
                    const float horizon = Horizon(p.x);
                    // Sky above the hills (lighter toward them), land below (darker toward the bottom).
                    glm::vec3 color = Mix(skyLow, skyTop, std::clamp((p.y - horizon) / (0.95f - horizon), 0.0f, 1.0f));
                    const glm::vec3 land = Mix(landTop, landLow, std::clamp((horizon - p.y) / (horizon + 0.95f), 0.0f, 1.0f));
                    color = Mix(color, land, coverage(p.y - horizon));
                    // The letter's soft shadow, the ring, the letter.
                    color = Mix(color, glm::vec3(0.0f), 0.3f * coverage(LetterZ(p - glm::vec2(0.03f, -0.04f)) - 0.01f, 10.0f));
                    color = Mix(color, ring, 0.4f * coverage(std::abs(radius - 0.885f) - 0.013f));
                    color = Mix(color, letter, coverage(LetterZ(p)));
                    const float alpha = coverage(radius - 0.95f);
                    uint8_t* out = &image.Pixels[((size_t)y * size + x) * 4];
                    for (int c = 0; c < 3; c++)
                        out[c] = (uint8_t)std::lround(std::clamp(color[c], 0.0f, 1.0f) * 255.0f);
                    out[3] = (uint8_t)std::lround(alpha * 255.0f);
                }
            return image;
        }

        Ref<Texture2D> CreateTexture(uint32_t size)
        {
            ImageData image = Generate(size);
            // Textures keep their rows bottom first (like files loaded for the GPU).
            const size_t row = (size_t)image.Width * 4;
            std::vector<uint8_t> flipped(image.Pixels.size());
            for (uint32_t y = 0; y < image.Height; y++)
                std::memcpy(&flipped[(size_t)y * row], &image.Pixels[(size_t)(image.Height - 1 - y) * row], row);
            TextureSpecification spec;
            spec.Width = image.Width;
            spec.Height = image.Height;
            spec.Format = ImageFormat::RGBA8;
            spec.Wrap = TextureWrap::ClampToEdge;
            spec.GenerateMips = true;
            spec.SRGB = true;
            Ref<Texture2D> texture = Texture2D::Create(spec);
            if (texture)
                texture->SetData(flipped.data(), (uint32_t)flipped.size());
            return texture;
        }

    }

    // ------------------------------------------------------------------ the screen
    bool SplashScreen::Begin(float duration, bool skippable, const glm::vec3& background, const std::filesystem::path& image)
    {
        m_Timeline = SplashTimeline(duration, skippable);
        m_Background = background;
        m_Active = true;
        m_IsLogo = true;
        m_Texture = nullptr;
        bool ok = true;
        if (!image.empty())
        {
            Ref<Texture2D> picture = Texture2D::Create(image.string());
            if (picture && picture->IsLoaded())
            {
                m_Texture = picture;
                m_IsLogo = false;
            }
            else
            {
                ZM_CORE_WARN("Splash rasmi o'qilmadi: {} - Zamin logotipi ko'rsatiladi", image.generic_string());
                ok = false;
            }
        }
        if (m_IsLogo)
            m_Texture = SplashLogo::CreateTexture(512);
        return ok;
    }

    void SplashScreen::Render(uint32_t w, uint32_t h, float progress) const
    {
        if (w == 0 || h == 0)
            return;
        RenderCommand::SetViewport(0, 0, w, h);
        RenderCommand::SetClearColor({ m_Background, 1.0f });
        RenderCommand::Clear();
        RenderCommand::SetDepthTest(false);
        const float alpha = m_Timeline.GetAlpha();
        const float fw = (float)w, fh = (float)h;
        Renderer2D::SetDisplayOutput(true);
        Renderer2D::SetLit(false);
        Renderer2D::BeginScene(glm::ortho(0.0f, fw, 0.0f, fh, -1.0f, 1.0f));
        const glm::vec4 tint(1.0f, 1.0f, 1.0f, alpha);
        float pictureBottom = fh * 0.5f;
        if (m_Texture && m_IsLogo)
        {
            // The logo a little above the middle, "ZAMIN ENGINE" under it.
            const float side = std::min(fh * 0.34f, fw * 0.45f);
            const glm::vec2 center(fw * 0.5f, fh * 0.57f);
            Renderer2D::DrawQuad(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(side, side, 1.0f)),
                                 m_Texture, 1.0f, tint);
            pictureBottom = center.y - side * 0.5f;
            if (Ref<Font> font = Font::GetDefault())
            {
                auto centered = [&](const std::string& text, float size, float y, const glm::vec4& color, float kerning) {
                    Renderer2D::TextParams params;
                    params.Color = color;
                    params.Kerning = kerning;
                    const float width = Renderer2D::MeasureString(text, font, params).x * size;
                    const glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(center.x - width * 0.5f, y, 0.0f)) *
                                                glm::scale(glm::mat4(1.0f), glm::vec3(size));
                    Renderer2D::DrawString(text, font, transform, params);
                };
                const float title = side * 0.22f;
                centered("ZAMIN", title, pictureBottom - title * 1.25f, glm::vec4(0.95f, 0.96f, 0.97f, alpha), 0.12f);
                centered("ENGINE", title * 0.36f, pictureBottom - title * 1.25f - title * 0.75f, glm::vec4(0.55f, 0.72f, 0.70f, alpha), 0.6f);
                pictureBottom -= title * 2.2f;
            }
        }
        else if (m_Texture)
        {
            // The game's picture: as large as it fits into 70% x 55% of the screen, its shape kept.
            const float aspect = (float)m_Texture->GetWidth() / (float)std::max(1u, m_Texture->GetHeight());
            float width = fw * 0.7f, height = width / aspect;
            if (height > fh * 0.55f)
            {
                height = fh * 0.55f;
                width = height * aspect;
            }
            Renderer2D::DrawQuad(glm::translate(glm::mat4(1.0f), glm::vec3(fw * 0.5f, fh * 0.52f, 0.0f)) *
                                     glm::scale(glm::mat4(1.0f), glm::vec3(width, height, 1.0f)),
                                 m_Texture, 1.0f, tint);
            pictureBottom = fh * 0.52f - height * 0.5f;
        }
        // Still loading after its time: a thin bar under the picture.
        if (m_Timeline.GetPhase() == SplashTimeline::Phase::Waiting && progress >= 0.0f)
        {
            const float barWidth = std::min(fw * 0.3f, 360.0f * std::max(1.0f, fh / 720.0f));
            const float barHeight = std::max(2.0f, fh * 0.004f);
            const float y = std::max(fh * 0.12f, pictureBottom - fh * 0.06f);
            Renderer2D::DrawQuad(glm::vec2(fw * 0.5f, y), glm::vec2(barWidth, barHeight), glm::vec4(1.0f, 1.0f, 1.0f, 0.15f * alpha));
            const float filled = barWidth * std::clamp(progress, 0.0f, 1.0f);
            if (filled > 0.0f)
                Renderer2D::DrawQuad(glm::vec2(fw * 0.5f - barWidth * 0.5f + filled * 0.5f, y), glm::vec2(filled, barHeight),
                                     glm::vec4(0.55f, 0.85f, 0.80f, 0.9f * alpha));
        }
        Renderer2D::EndScene();
        Renderer2D::SetDisplayOutput(false);
    }

}
