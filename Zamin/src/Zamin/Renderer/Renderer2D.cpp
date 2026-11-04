#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/VertexArray.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>

namespace Zamin {

    namespace {

        const char* s_QuadVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;
layout(location = 5) in int a_EntityID;

uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec2 v_TexCoord;
flat out float v_TexIndex;
out float v_TilingFactor;
flat out int v_EntityID;

void main()
{
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexIndex = a_TexIndex;
    v_TilingFactor = a_TilingFactor;
    v_EntityID = a_EntityID;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
)";

        const char* s_QuadFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

in vec4 v_Color;
in vec2 v_TexCoord;
flat in float v_TexIndex;
in float v_TilingFactor;
flat in int v_EntityID;

uniform sampler2D u_Textures[16];

void main()
{
    vec2 uv = v_TexCoord * v_TilingFactor;
    vec4 texColor;
    switch (int(v_TexIndex))
    {
        case  0: texColor = texture(u_Textures[ 0], uv); break;
        case  1: texColor = texture(u_Textures[ 1], uv); break;
        case  2: texColor = texture(u_Textures[ 2], uv); break;
        case  3: texColor = texture(u_Textures[ 3], uv); break;
        case  4: texColor = texture(u_Textures[ 4], uv); break;
        case  5: texColor = texture(u_Textures[ 5], uv); break;
        case  6: texColor = texture(u_Textures[ 6], uv); break;
        case  7: texColor = texture(u_Textures[ 7], uv); break;
        case  8: texColor = texture(u_Textures[ 8], uv); break;
        case  9: texColor = texture(u_Textures[ 9], uv); break;
        case 10: texColor = texture(u_Textures[10], uv); break;
        case 11: texColor = texture(u_Textures[11], uv); break;
        case 12: texColor = texture(u_Textures[12], uv); break;
        case 13: texColor = texture(u_Textures[13], uv); break;
        case 14: texColor = texture(u_Textures[14], uv); break;
        case 15: texColor = texture(u_Textures[15], uv); break;
        default: texColor = vec4(1.0); break;
    }
    texColor *= v_Color;
    if (texColor.a < 0.01)
        discard;
    // Sprites are authored in sRGB; the scene buffer is linear HDR.
    o_Color = vec4(pow(texColor.rgb, vec3(2.2)), texColor.a);
    o_EntityID = v_EntityID;
}
)";

        const char* s_CircleVS = R"(#version 410 core
layout(location = 0) in vec3 a_WorldPosition;
layout(location = 1) in vec3 a_LocalPosition;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in float a_Thickness;
layout(location = 4) in float a_Fade;
layout(location = 5) in int a_EntityID;

uniform mat4 u_ViewProjection;

out vec3 v_LocalPosition;
out vec4 v_Color;
out float v_Thickness;
out float v_Fade;
flat out int v_EntityID;

void main()
{
    v_LocalPosition = a_LocalPosition;
    v_Color = a_Color;
    v_Thickness = a_Thickness;
    v_Fade = a_Fade;
    v_EntityID = a_EntityID;
    gl_Position = u_ViewProjection * vec4(a_WorldPosition, 1.0);
}
)";

        const char* s_CircleFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

in vec3 v_LocalPosition;
in vec4 v_Color;
in float v_Thickness;
in float v_Fade;
flat in int v_EntityID;

void main()
{
    float distance = 1.0 - length(v_LocalPosition);
    float circle = smoothstep(0.0, v_Fade, distance);
    circle *= smoothstep(v_Thickness + v_Fade, v_Thickness, distance);
    if (circle == 0.0)
        discard;
    o_Color = vec4(pow(v_Color.rgb, vec3(2.2)), v_Color.a * circle);
    o_EntityID = v_EntityID;
}
)";

        const char* s_LineVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in int a_EntityID;

uniform mat4 u_ViewProjection;

out vec4 v_Color;
flat out int v_EntityID;

void main()
{
    v_Color = a_Color;
    v_EntityID = a_EntityID;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
)";

        const char* s_LineFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
layout(location = 1) out int o_EntityID;

in vec4 v_Color;
flat in int v_EntityID;

void main()
{
    o_Color = vec4(pow(v_Color.rgb, vec3(2.2)), v_Color.a);
    o_EntityID = v_EntityID;
}
)";

        struct QuadVertex
        {
            glm::vec3 Position;
            glm::vec4 Color;
            glm::vec2 TexCoord;
            float TexIndex;
            float TilingFactor;
            int EntityID;
        };

        struct CircleVertex
        {
            glm::vec3 WorldPosition;
            glm::vec3 LocalPosition;
            glm::vec4 Color;
            float Thickness;
            float Fade;
            int EntityID;
        };

        struct LineVertex
        {
            glm::vec3 Position;
            glm::vec4 Color;
            int EntityID;
        };

        struct Renderer2DData
        {
            static constexpr uint32_t MaxQuads = 20000;
            static constexpr uint32_t MaxVertices = MaxQuads * 4;
            static constexpr uint32_t MaxIndices = MaxQuads * 6;
            static constexpr uint32_t MaxTextureSlots = 16;

            Ref<VertexArray> QuadVertexArray;
            Ref<VertexBuffer> QuadVertexBuffer;
            Ref<Shader> QuadShader;
            Ref<Texture2D> WhiteTexture;

            Ref<VertexArray> CircleVertexArray;
            Ref<VertexBuffer> CircleVertexBuffer;
            Ref<Shader> CircleShader;

            Ref<VertexArray> LineVertexArray;
            Ref<VertexBuffer> LineVertexBuffer;
            Ref<Shader> LineShader;

            uint32_t QuadIndexCount = 0;
            QuadVertex* QuadVertexBufferBase = nullptr;
            QuadVertex* QuadVertexBufferPtr = nullptr;

            uint32_t CircleIndexCount = 0;
            CircleVertex* CircleVertexBufferBase = nullptr;
            CircleVertex* CircleVertexBufferPtr = nullptr;

            uint32_t LineVertexCount = 0;
            LineVertex* LineVertexBufferBase = nullptr;
            LineVertex* LineVertexBufferPtr = nullptr;

            float LineWidth = 2.0f;

            std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;
            uint32_t TextureSlotIndex = 1; // 0 = white texture

            glm::vec4 QuadVertexPositions[4];
            glm::mat4 ViewProjection = glm::mat4(1.0f);
        };

        Renderer2DData* s_Data = nullptr;

        constexpr glm::vec2 s_TexCoords[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    }

    void Renderer2D::Init()
    {
        s_Data = new Renderer2DData();

        // Quads
        s_Data->QuadVertexArray = VertexArray::Create();
        s_Data->QuadVertexBuffer = VertexBuffer::Create(Renderer2DData::MaxVertices * sizeof(QuadVertex));
        s_Data->QuadVertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float4, "a_Color" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float,  "a_TexIndex" },
            { ShaderDataType::Float,  "a_TilingFactor" },
            { ShaderDataType::Int,    "a_EntityID" },
        });
        s_Data->QuadVertexArray->AddVertexBuffer(s_Data->QuadVertexBuffer);
        s_Data->QuadVertexBufferBase = new QuadVertex[Renderer2DData::MaxVertices];

        uint32_t* quadIndices = new uint32_t[Renderer2DData::MaxIndices];
        uint32_t offset = 0;
        for (uint32_t i = 0; i < Renderer2DData::MaxIndices; i += 6)
        {
            quadIndices[i + 0] = offset + 0;
            quadIndices[i + 1] = offset + 1;
            quadIndices[i + 2] = offset + 2;
            quadIndices[i + 3] = offset + 2;
            quadIndices[i + 4] = offset + 3;
            quadIndices[i + 5] = offset + 0;
            offset += 4;
        }
        Ref<IndexBuffer> quadIB = IndexBuffer::Create(quadIndices, Renderer2DData::MaxIndices);
        s_Data->QuadVertexArray->SetIndexBuffer(quadIB);
        delete[] quadIndices;

        // Circles (share the quad index buffer)
        s_Data->CircleVertexArray = VertexArray::Create();
        s_Data->CircleVertexBuffer = VertexBuffer::Create(Renderer2DData::MaxVertices * sizeof(CircleVertex));
        s_Data->CircleVertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_WorldPosition" },
            { ShaderDataType::Float3, "a_LocalPosition" },
            { ShaderDataType::Float4, "a_Color" },
            { ShaderDataType::Float,  "a_Thickness" },
            { ShaderDataType::Float,  "a_Fade" },
            { ShaderDataType::Int,    "a_EntityID" },
        });
        s_Data->CircleVertexArray->AddVertexBuffer(s_Data->CircleVertexBuffer);
        s_Data->CircleVertexArray->SetIndexBuffer(quadIB);
        s_Data->CircleVertexBufferBase = new CircleVertex[Renderer2DData::MaxVertices];

        // Lines
        s_Data->LineVertexArray = VertexArray::Create();
        s_Data->LineVertexBuffer = VertexBuffer::Create(Renderer2DData::MaxVertices * sizeof(LineVertex));
        s_Data->LineVertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float4, "a_Color" },
            { ShaderDataType::Int,    "a_EntityID" },
        });
        s_Data->LineVertexArray->AddVertexBuffer(s_Data->LineVertexBuffer);
        s_Data->LineVertexBufferBase = new LineVertex[Renderer2DData::MaxVertices];

        // White texture
        TextureSpecification whiteSpec;
        whiteSpec.GenerateMips = false;
        s_Data->WhiteTexture = Texture2D::Create(whiteSpec);
        uint32_t white = 0xffffffff;
        s_Data->WhiteTexture->SetData(&white, sizeof(uint32_t));
        s_Data->TextureSlots[0] = s_Data->WhiteTexture;

        // Shaders
        s_Data->QuadShader = Shader::Create("Renderer2D_Quad", s_QuadVS, s_QuadFS);
        s_Data->CircleShader = Shader::Create("Renderer2D_Circle", s_CircleVS, s_CircleFS);
        s_Data->LineShader = Shader::Create("Renderer2D_Line", s_LineVS, s_LineFS);

        int samplers[Renderer2DData::MaxTextureSlots];
        for (uint32_t i = 0; i < Renderer2DData::MaxTextureSlots; i++)
            samplers[i] = (int)i;
        s_Data->QuadShader->Bind();
        s_Data->QuadShader->SetIntArray("u_Textures", samplers, Renderer2DData::MaxTextureSlots);

        s_Data->QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
        s_Data->QuadVertexPositions[1] = {  0.5f, -0.5f, 0.0f, 1.0f };
        s_Data->QuadVertexPositions[2] = {  0.5f,  0.5f, 0.0f, 1.0f };
        s_Data->QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };
    }

    void Renderer2D::Shutdown()
    {
        if (!s_Data) return;
        delete[] s_Data->QuadVertexBufferBase;
        delete[] s_Data->CircleVertexBufferBase;
        delete[] s_Data->LineVertexBufferBase;
        delete s_Data;
        s_Data = nullptr;
    }

    void Renderer2D::BeginScene(const glm::mat4& viewProjection)
    {
        s_Data->ViewProjection = viewProjection;
        StartBatch();
    }

    void Renderer2D::EndScene() { Flush(); }

    static void ResetBatch()
    {
        s_Data->QuadIndexCount = 0;
        s_Data->QuadVertexBufferPtr = s_Data->QuadVertexBufferBase;
        s_Data->CircleIndexCount = 0;
        s_Data->CircleVertexBufferPtr = s_Data->CircleVertexBufferBase;
        s_Data->LineVertexCount = 0;
        s_Data->LineVertexBufferPtr = s_Data->LineVertexBufferBase;
        s_Data->TextureSlotIndex = 1;
    }

    void Renderer2D::StartBatch() { ResetBatch(); }

    void Renderer2D::Flush()
    {
        auto& stats = Renderer::GetStats();

        if (s_Data->QuadIndexCount)
        {
            uint32_t dataSize = (uint32_t)((uint8_t*)s_Data->QuadVertexBufferPtr - (uint8_t*)s_Data->QuadVertexBufferBase);
            s_Data->QuadVertexBuffer->SetData(s_Data->QuadVertexBufferBase, dataSize);
            for (uint32_t i = 0; i < s_Data->TextureSlotIndex; i++)
                s_Data->TextureSlots[i]->Bind(i);
            s_Data->QuadShader->Bind();
            s_Data->QuadShader->SetMat4("u_ViewProjection", s_Data->ViewProjection);
            RenderCommand::DrawIndexed(s_Data->QuadVertexArray, s_Data->QuadIndexCount);
            stats.DrawCalls++;
        }

        if (s_Data->CircleIndexCount)
        {
            uint32_t dataSize = (uint32_t)((uint8_t*)s_Data->CircleVertexBufferPtr - (uint8_t*)s_Data->CircleVertexBufferBase);
            s_Data->CircleVertexBuffer->SetData(s_Data->CircleVertexBufferBase, dataSize);
            s_Data->CircleShader->Bind();
            s_Data->CircleShader->SetMat4("u_ViewProjection", s_Data->ViewProjection);
            RenderCommand::DrawIndexed(s_Data->CircleVertexArray, s_Data->CircleIndexCount);
            stats.DrawCalls++;
        }

        if (s_Data->LineVertexCount)
        {
            uint32_t dataSize = (uint32_t)((uint8_t*)s_Data->LineVertexBufferPtr - (uint8_t*)s_Data->LineVertexBufferBase);
            s_Data->LineVertexBuffer->SetData(s_Data->LineVertexBufferBase, dataSize);
            s_Data->LineShader->Bind();
            s_Data->LineShader->SetMat4("u_ViewProjection", s_Data->ViewProjection);
            RenderCommand::SetLineWidth(s_Data->LineWidth);
            RenderCommand::DrawLines(s_Data->LineVertexArray, s_Data->LineVertexCount);
            stats.DrawCalls++;
        }
    }

    void Renderer2D::NextBatch()
    {
        Flush();
        StartBatch();
    }

    // ------------------------------------------------------------------ quads
    static float GetTextureIndex(const Ref<Texture2D>& texture)
    {
        if (!texture)
            return 0.0f;
        for (uint32_t i = 1; i < s_Data->TextureSlotIndex; i++)
            if (*s_Data->TextureSlots[i] == *texture)
                return (float)i;

        if (s_Data->TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
        {
            Renderer2D::Flush();
            ResetBatch();
        }

        float index = (float)s_Data->TextureSlotIndex;
        s_Data->TextureSlots[s_Data->TextureSlotIndex++] = texture;
        return index;
    }

    static void SubmitQuad(const glm::mat4& transform, const glm::vec4& color, float texIndex, float tiling,
                           const glm::vec2* texCoords, int entityID)
    {
        for (int i = 0; i < 4; i++)
        {
            s_Data->QuadVertexBufferPtr->Position = transform * s_Data->QuadVertexPositions[i];
            s_Data->QuadVertexBufferPtr->Color = color;
            s_Data->QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data->QuadVertexBufferPtr->TexIndex = texIndex;
            s_Data->QuadVertexBufferPtr->TilingFactor = tiling;
            s_Data->QuadVertexBufferPtr->EntityID = entityID;
            s_Data->QuadVertexBufferPtr++;
        }
        s_Data->QuadIndexCount += 6;
        Renderer::GetStats().QuadCount++;
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, color);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        DrawQuad(transform, color);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        DrawQuad(transform, texture, tilingFactor, tintColor);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
            * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        DrawQuad(transform, color);
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID)
    {
        if (s_Data->QuadIndexCount >= Renderer2DData::MaxIndices)
            NextBatch();
        SubmitQuad(transform, color, 0.0f, 1.0f, s_TexCoords, entityID);
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, int entityID)
    {
        if (s_Data->QuadIndexCount >= Renderer2DData::MaxIndices)
            NextBatch();
        float texIndex = GetTextureIndex(texture);
        SubmitQuad(transform, tintColor, texIndex, tilingFactor, s_TexCoords, entityID);
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, const glm::vec2& uvMin,
                              const glm::vec2& uvMax, const glm::vec4& tintColor, int entityID)
    {
        if (s_Data->QuadIndexCount >= Renderer2DData::MaxIndices)
            NextBatch();
        float texIndex = GetTextureIndex(texture);
        const glm::vec2 coords[4] = { { uvMin.x, uvMin.y }, { uvMax.x, uvMin.y }, { uvMax.x, uvMax.y }, { uvMin.x, uvMax.y } };
        SubmitQuad(transform, tintColor, texIndex, 1.0f, coords, entityID);
    }

    // ------------------------------------------------------------------ circles
    void Renderer2D::DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness, float fade, int entityID)
    {
        if (s_Data->CircleIndexCount >= Renderer2DData::MaxIndices)
            NextBatch();

        for (int i = 0; i < 4; i++)
        {
            s_Data->CircleVertexBufferPtr->WorldPosition = transform * s_Data->QuadVertexPositions[i];
            s_Data->CircleVertexBufferPtr->LocalPosition = s_Data->QuadVertexPositions[i] * 2.0f;
            s_Data->CircleVertexBufferPtr->Color = color;
            s_Data->CircleVertexBufferPtr->Thickness = thickness;
            s_Data->CircleVertexBufferPtr->Fade = fade;
            s_Data->CircleVertexBufferPtr->EntityID = entityID;
            s_Data->CircleVertexBufferPtr++;
        }
        s_Data->CircleIndexCount += 6;
        Renderer::GetStats().CircleCount++;
    }

    // ------------------------------------------------------------------ lines
    void Renderer2D::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID)
    {
        if (s_Data->LineVertexCount + 2 >= Renderer2DData::MaxVertices)
            NextBatch();
        s_Data->LineVertexBufferPtr->Position = p0;
        s_Data->LineVertexBufferPtr->Color = color;
        s_Data->LineVertexBufferPtr->EntityID = entityID;
        s_Data->LineVertexBufferPtr++;
        s_Data->LineVertexBufferPtr->Position = p1;
        s_Data->LineVertexBufferPtr->Color = color;
        s_Data->LineVertexBufferPtr->EntityID = entityID;
        s_Data->LineVertexBufferPtr++;
        s_Data->LineVertexCount += 2;
        Renderer::GetStats().LineCount++;
    }

    void Renderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID)
    {
        glm::vec3 p0 = { position.x - size.x * 0.5f, position.y - size.y * 0.5f, position.z };
        glm::vec3 p1 = { position.x + size.x * 0.5f, position.y - size.y * 0.5f, position.z };
        glm::vec3 p2 = { position.x + size.x * 0.5f, position.y + size.y * 0.5f, position.z };
        glm::vec3 p3 = { position.x - size.x * 0.5f, position.y + size.y * 0.5f, position.z };
        DrawLine(p0, p1, color, entityID);
        DrawLine(p1, p2, color, entityID);
        DrawLine(p2, p3, color, entityID);
        DrawLine(p3, p0, color, entityID);
    }

    void Renderer2D::DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID)
    {
        glm::vec3 v[4];
        for (int i = 0; i < 4; i++)
            v[i] = transform * s_Data->QuadVertexPositions[i];
        for (int i = 0; i < 4; i++)
            DrawLine(v[i], v[(i + 1) % 4], color, entityID);
    }

    void Renderer2D::SetBlendMode(BlendMode mode)
    {
        NextBatch();
        RenderCommand::SetBlendMode(mode);
    }

    float Renderer2D::GetLineWidth() { return s_Data->LineWidth; }
    void Renderer2D::SetLineWidth(float width) { s_Data->LineWidth = width; }

    // ------------------------------------------------------------------ text
    void Renderer2D::DrawString(const std::string& text, const Ref<Font>& font, const glm::mat4& transform,
                                const TextParams& params, int entityID)
    {
        if (!font || !font->IsLoaded())
            return;
        const Ref<Texture2D>& atlas = font->GetAtlasTexture();

        float x = 0.0f, y = 0.0f;
        const float lineAdvance = font->GetLineHeight() + params.LineSpacing;

        size_t i = 0;
        while (i < text.size())
        {
            uint32_t cp = DecodeUTF8(text, i);
            if (cp == '\r') continue;
            if (cp == '\n') { x = 0.0f; y -= lineAdvance; continue; }
            if (cp == '\t') cp = ' ';

            const Glyph* g = font->GetGlyph(cp);
            if (!g) continue;

            if (g->X1 > g->X0)
            {
                if (s_Data->QuadIndexCount >= Renderer2DData::MaxIndices)
                    NextBatch();
                float texIndex = GetTextureIndex(atlas);

                glm::vec4 corners[4] = {
                    { x + g->X0, y + g->Y0, 0.0f, 1.0f },
                    { x + g->X1, y + g->Y0, 0.0f, 1.0f },
                    { x + g->X1, y + g->Y1, 0.0f, 1.0f },
                    { x + g->X0, y + g->Y1, 0.0f, 1.0f },
                };
                glm::vec2 uvs[4] = { { g->U0, g->V0 }, { g->U1, g->V0 }, { g->U1, g->V1 }, { g->U0, g->V1 } };
                for (int k = 0; k < 4; k++)
                {
                    s_Data->QuadVertexBufferPtr->Position = transform * corners[k];
                    s_Data->QuadVertexBufferPtr->Color = params.Color;
                    s_Data->QuadVertexBufferPtr->TexCoord = uvs[k];
                    s_Data->QuadVertexBufferPtr->TexIndex = texIndex;
                    s_Data->QuadVertexBufferPtr->TilingFactor = 1.0f;
                    s_Data->QuadVertexBufferPtr->EntityID = entityID;
                    s_Data->QuadVertexBufferPtr++;
                }
                s_Data->QuadIndexCount += 6;
                Renderer::GetStats().QuadCount++;
            }
            x += g->Advance + params.Kerning;
        }
    }

    glm::vec2 Renderer2D::MeasureString(const std::string& text, const Ref<Font>& font, const TextParams& params)
    {
        if (!font) return { 0.0f, 0.0f };
        float x = 0.0f, maxX = 0.0f;
        int lines = 1;
        size_t i = 0;
        while (i < text.size())
        {
            uint32_t cp = DecodeUTF8(text, i);
            if (cp == '\n') { maxX = std::max(maxX, x); x = 0.0f; lines++; continue; }
            if (const Glyph* g = font->GetGlyph(cp))
                x += g->Advance + params.Kerning;
        }
        maxX = std::max(maxX, x);
        return { maxX, lines * (font->GetLineHeight() + params.LineSpacing) };
    }

}
