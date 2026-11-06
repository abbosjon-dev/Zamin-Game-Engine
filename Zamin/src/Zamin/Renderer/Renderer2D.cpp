#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Renderer.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/VertexArray.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Zamin {

    namespace {

        const char* s_QuadVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;
layout(location = 5) in int a_EntityID;
layout(location = 6) in vec4 a_Flash;         // rgb + amount
layout(location = 7) in vec4 a_OutlineColor;
layout(location = 8) in vec4 a_UVRect;        // the picture's own UV rectangle (min, max)
layout(location = 9) in float a_OutlineWidth; // texture pixels (0: no outline)
layout(location = 10) in float a_Lit;         // 1: lit by the 2D lights

uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec2 v_TexCoord;
flat out float v_TexIndex;
out float v_TilingFactor;
flat out int v_EntityID;
flat out vec4 v_Flash;
flat out vec4 v_OutlineColor;
flat out vec4 v_UVRect;
flat out float v_OutlineWidth;
flat out float v_Lit;
out vec2 v_WorldPos;

void main()
{
    v_Lit = a_Lit;
    v_WorldPos = a_Position.xy;
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexIndex = a_TexIndex;
    v_TilingFactor = a_TilingFactor;
    v_EntityID = a_EntityID;
    v_Flash = a_Flash;
    v_OutlineColor = a_OutlineColor;
    v_UVRect = a_UVRect;
    v_OutlineWidth = a_OutlineWidth;
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
flat in vec4 v_Flash;
flat in vec4 v_OutlineColor;
flat in vec4 v_UVRect;
flat in float v_OutlineWidth;
flat in float v_Lit;
in vec2 v_WorldPos;

uniform sampler2D u_Textures[16];
uniform int u_LinearTextures; // bit per slot: the texture holds linear values (import setting sRGB off)
uniform int u_DisplayOutput;  // 1: written as display (sRGB) colour - drawn over the finished picture

// 2D lights (Light2DMath does the same on the CPU)
const int MAX_LIGHTS_2D = 32;
uniform int u_LightCount;
uniform vec3 u_Ambient2D;                       // linear
uniform vec4 u_LightPosRadius[MAX_LIGHTS_2D];   // xy position, z radius, w inner radius
uniform vec4 u_LightColor[MAX_LIGHTS_2D];       // rgb linear x intensity, w falloff
uniform vec4 u_LightSpot[MAX_LIGHTS_2D];        // xy direction, z cos outer, w cos inner (z < -1.5: a point light)

vec3 Lighting2D(vec2 p)
{
    vec3 light = u_Ambient2D;
    for (int i = 0; i < u_LightCount; i++)
    {
        vec4 pr = u_LightPosRadius[i];
        vec2 d = p - pr.xy;
        float dist = length(d);
        if (dist >= pr.z)
            continue;
        float inner = clamp(pr.w, 0.0, pr.z);
        float a = 1.0;
        if (dist > inner)
            a = pow(clamp(1.0 - (dist - inner) / max(1e-6, pr.z - inner), 0.0, 1.0), max(0.01, u_LightColor[i].w));
        vec4 spot = u_LightSpot[i];
        if (spot.z > -1.5 && dist > 1e-6)
        {
            float c = dot(spot.xy, d / dist);
            if (spot.w <= spot.z + 1e-6)
                a *= c >= spot.z ? 1.0 : 0.0;
            else
                a *= smoothstep(spot.z, spot.w, c);
        }
        light += u_LightColor[i].rgb * a;
    }
    return light;
}

vec4 SampleTexture(int index, vec2 uv)
{
    switch (index)
    {
        case  0: return texture(u_Textures[ 0], uv);
        case  1: return texture(u_Textures[ 1], uv);
        case  2: return texture(u_Textures[ 2], uv);
        case  3: return texture(u_Textures[ 3], uv);
        case  4: return texture(u_Textures[ 4], uv);
        case  5: return texture(u_Textures[ 5], uv);
        case  6: return texture(u_Textures[ 6], uv);
        case  7: return texture(u_Textures[ 7], uv);
        case  8: return texture(u_Textures[ 8], uv);
        case  9: return texture(u_Textures[ 9], uv);
        case 10: return texture(u_Textures[10], uv);
        case 11: return texture(u_Textures[11], uv);
        case 12: return texture(u_Textures[12], uv);
        case 13: return texture(u_Textures[13], uv);
        case 14: return texture(u_Textures[14], uv);
        case 15: return texture(u_Textures[15], uv);
    }
    return vec4(1.0);
}

vec4 SampleTextureLod(int index, vec2 uv)
{
    switch (index)
    {
        case  0: return textureLod(u_Textures[ 0], uv, 0.0);
        case  1: return textureLod(u_Textures[ 1], uv, 0.0);
        case  2: return textureLod(u_Textures[ 2], uv, 0.0);
        case  3: return textureLod(u_Textures[ 3], uv, 0.0);
        case  4: return textureLod(u_Textures[ 4], uv, 0.0);
        case  5: return textureLod(u_Textures[ 5], uv, 0.0);
        case  6: return textureLod(u_Textures[ 6], uv, 0.0);
        case  7: return textureLod(u_Textures[ 7], uv, 0.0);
        case  8: return textureLod(u_Textures[ 8], uv, 0.0);
        case  9: return textureLod(u_Textures[ 9], uv, 0.0);
        case 10: return textureLod(u_Textures[10], uv, 0.0);
        case 11: return textureLod(u_Textures[11], uv, 0.0);
        case 12: return textureLod(u_Textures[12], uv, 0.0);
        case 13: return textureLod(u_Textures[13], uv, 0.0);
        case 14: return textureLod(u_Textures[14], uv, 0.0);
        case 15: return textureLod(u_Textures[15], uv, 0.0);
    }
    return vec4(1.0);
}

vec2 TextureSize(int index)
{
    switch (index)
    {
        case  0: return vec2(textureSize(u_Textures[ 0], 0));
        case  1: return vec2(textureSize(u_Textures[ 1], 0));
        case  2: return vec2(textureSize(u_Textures[ 2], 0));
        case  3: return vec2(textureSize(u_Textures[ 3], 0));
        case  4: return vec2(textureSize(u_Textures[ 4], 0));
        case  5: return vec2(textureSize(u_Textures[ 5], 0));
        case  6: return vec2(textureSize(u_Textures[ 6], 0));
        case  7: return vec2(textureSize(u_Textures[ 7], 0));
        case  8: return vec2(textureSize(u_Textures[ 8], 0));
        case  9: return vec2(textureSize(u_Textures[ 9], 0));
        case 10: return vec2(textureSize(u_Textures[10], 0));
        case 11: return vec2(textureSize(u_Textures[11], 0));
        case 12: return vec2(textureSize(u_Textures[12], 0));
        case 13: return vec2(textureSize(u_Textures[13], 0));
        case 14: return vec2(textureSize(u_Textures[14], 0));
        case 15: return vec2(textureSize(u_Textures[15], 0));
    }
    return vec2(1.0);
}

bool InsideRect(vec2 p) { return all(greaterThanEqual(p, v_UVRect.xy)) && all(lessThanEqual(p, v_UVRect.zw)); }

void main()
{
    int index = int(v_TexIndex);
    vec2 uv = v_TexCoord * v_TilingFactor;
    vec4 texColor = SampleTexture(index, uv);
    bool outline = v_OutlineWidth > 0.0;
    if (outline && !InsideRect(v_TexCoord))
        texColor = vec4(0.0); // the quad grew by the outline: no picture out here
    bool linearTexture = ((u_LinearTextures >> index) & 1) != 0;
    vec4 color = texColor * v_Color;
    // Sprites are authored in sRGB; the scene buffer is linear HDR.
    vec3 rgb = linearTexture ? pow(v_Color.rgb, vec3(2.2)) * texColor.rgb : pow(color.rgb, vec3(2.2));
    float alpha = color.a;
    // 2D lights (Lit renderers): the picture times the light that reaches it.
    if (v_Lit > 0.5)
        rgb *= Lighting2D(v_WorldPos);
    // Hit flash: the picture toward the flash colour.
    if (v_Flash.a > 0.0)
        rgb = mix(rgb, pow(v_Flash.rgb, vec3(2.2)), clamp(v_Flash.a, 0.0, 1.0));
    // Outline: the most opaque neighbour within the width (rings of samples), under the picture.
    if (outline && texColor.a < 0.999)
    {
        vec2 texel = 1.0 / max(TextureSize(index), vec2(1.0));
        float around = 0.0;
        int rings = int(clamp(ceil(v_OutlineWidth), 1.0, 4.0));
        for (int r = 1; r <= rings; r++)
        {
            float radius = v_OutlineWidth * float(r) / float(rings);
            for (int k = 0; k < 12; k++)
            {
                float angle = float(k) * 0.5235988;
                vec2 p = v_TexCoord + vec2(cos(angle), sin(angle)) * radius * texel;
                if (InsideRect(p))
                    around = max(around, SampleTextureLod(index, p).a);
            }
        }
        float outlineAlpha = around * v_OutlineColor.a * v_Color.a;
        float total = alpha + outlineAlpha * (1.0 - alpha);
        if (total > 0.0)
            rgb = (rgb * alpha + pow(v_OutlineColor.rgb, vec3(2.2)) * outlineAlpha * (1.0 - alpha)) / total;
        alpha = total;
    }
    if (alpha < 0.01)
        discard;
    o_Color = vec4(u_DisplayOutput == 1 ? pow(max(rgb, vec3(0.0)), vec3(1.0 / 2.2)) : rgb, alpha);
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
uniform int u_DisplayOutput; // 1: display (sRGB) colour

void main()
{
    float distance = 1.0 - length(v_LocalPosition);
    float circle = smoothstep(0.0, v_Fade, distance);
    circle *= smoothstep(v_Thickness + v_Fade, v_Thickness, distance);
    if (circle == 0.0)
        discard;
    o_Color = vec4(u_DisplayOutput == 1 ? v_Color.rgb : pow(v_Color.rgb, vec3(2.2)), v_Color.a * circle);
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
uniform int u_DisplayOutput; // 1: display (sRGB) colour

void main()
{
    o_Color = vec4(u_DisplayOutput == 1 ? v_Color.rgb : pow(v_Color.rgb, vec3(2.2)), v_Color.a);
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
            glm::vec4 Flash;        // rgb + amount (0: none)
            glm::vec4 OutlineColor;
            glm::vec4 UVRect;       // min.xy, max.xy
            float OutlineWidth;     // texture pixels (0: none)
            float Lit;              // 1: lit by the 2D lights
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

            int DisplayOutput = 0; // 1: display (sRGB) colours, see SetDisplayOutput

            // 2D lighting: what lit quads get (uploaded with the next quad draw that follows a change)
            float CurrentLit = 0.0f;
            Lighting2DData Lighting;
            bool LightingDirty = true;

            std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;
            uint32_t TextureSlotIndex = 1; // 0 = white texture

            glm::vec4 QuadVertexPositions[4];
            glm::mat4 ViewProjection = glm::mat4(1.0f);
            glm::vec2 ViewportSize{ 0.0f }; // of the target when the scene began (text picks its atlas by its size on it)
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
            { ShaderDataType::Float4, "a_Flash" },
            { ShaderDataType::Float4, "a_OutlineColor" },
            { ShaderDataType::Float4, "a_UVRect" },
            { ShaderDataType::Float,  "a_OutlineWidth" },
            { ShaderDataType::Float,  "a_Lit" },
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
        const glm::ivec4 viewport = RenderCommand::GetViewport();
        s_Data->ViewportSize = glm::vec2((float)std::max(0, viewport.z), (float)std::max(0, viewport.w));
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

    // The 2D lights into the quad shader's uniforms (the shader is bound).
    static void UploadLighting()
    {
        const Lighting2DData& l = s_Data->Lighting;
        const int count = std::min((int)l.Lights.size(), Light2DMath::MaxLights);
        std::array<glm::vec4, Light2DMath::MaxLights> posRadius{}, color{}, spot{};
        for (int i = 0; i < count; i++)
        {
            const Light2DData& light = l.Lights[(size_t)i];
            posRadius[(size_t)i] = { light.Position, light.Radius, light.InnerRadius };
            color[(size_t)i] = { light.Color, light.Falloff };
            spot[(size_t)i] = light.Spot ? glm::vec4(light.Direction, light.CosOuter, light.CosInner) : glm::vec4(1.0f, 0.0f, -2.0f, -2.0f);
        }
        Shader& shader = *s_Data->QuadShader;
        shader.SetInt("u_LightCount", count);
        shader.SetFloat3("u_Ambient2D", l.Ambient);
        shader.SetFloat4Array("u_LightPosRadius", posRadius.data(), (uint32_t)count);
        shader.SetFloat4Array("u_LightColor", color.data(), (uint32_t)count);
        shader.SetFloat4Array("u_LightSpot", spot.data(), (uint32_t)count);
        s_Data->LightingDirty = false;
    }

    // Each kind of the batch drawn and taken out of it (the next draw starts that kind afresh).
    static void FlushQuads()
    {
        if (!s_Data->QuadIndexCount)
            return;
        uint32_t dataSize = (uint32_t)((uint8_t*)s_Data->QuadVertexBufferPtr - (uint8_t*)s_Data->QuadVertexBufferBase);
        s_Data->QuadVertexBuffer->SetData(s_Data->QuadVertexBufferBase, dataSize);
        int linearTextures = 0;
        for (uint32_t i = 0; i < s_Data->TextureSlotIndex; i++)
        {
            s_Data->TextureSlots[i]->Bind(i);
            if (!s_Data->TextureSlots[i]->GetSpecification().SRGB)
                linearTextures |= 1 << i;
        }
        s_Data->QuadShader->Bind();
        s_Data->QuadShader->SetMat4("u_ViewProjection", s_Data->ViewProjection);
        s_Data->QuadShader->SetInt("u_LinearTextures", linearTextures);
        s_Data->QuadShader->SetInt("u_DisplayOutput", s_Data->DisplayOutput);
        if (s_Data->LightingDirty)
            UploadLighting();
        RenderCommand::DrawIndexed(s_Data->QuadVertexArray, s_Data->QuadIndexCount);
        Renderer::GetStats().DrawCalls++;
        s_Data->QuadIndexCount = 0;
        s_Data->QuadVertexBufferPtr = s_Data->QuadVertexBufferBase;
        s_Data->TextureSlotIndex = 1;
    }

    static void FlushCircles()
    {
        if (!s_Data->CircleIndexCount)
            return;
        uint32_t dataSize = (uint32_t)((uint8_t*)s_Data->CircleVertexBufferPtr - (uint8_t*)s_Data->CircleVertexBufferBase);
        s_Data->CircleVertexBuffer->SetData(s_Data->CircleVertexBufferBase, dataSize);
        s_Data->CircleShader->Bind();
        s_Data->CircleShader->SetMat4("u_ViewProjection", s_Data->ViewProjection);
        s_Data->CircleShader->SetInt("u_DisplayOutput", s_Data->DisplayOutput);
        RenderCommand::DrawIndexed(s_Data->CircleVertexArray, s_Data->CircleIndexCount);
        Renderer::GetStats().DrawCalls++;
        s_Data->CircleIndexCount = 0;
        s_Data->CircleVertexBufferPtr = s_Data->CircleVertexBufferBase;
    }

    static void FlushLines()
    {
        if (!s_Data->LineVertexCount)
            return;
        uint32_t dataSize = (uint32_t)((uint8_t*)s_Data->LineVertexBufferPtr - (uint8_t*)s_Data->LineVertexBufferBase);
        s_Data->LineVertexBuffer->SetData(s_Data->LineVertexBufferBase, dataSize);
        s_Data->LineShader->Bind();
        s_Data->LineShader->SetMat4("u_ViewProjection", s_Data->ViewProjection);
        s_Data->LineShader->SetInt("u_DisplayOutput", s_Data->DisplayOutput);
        RenderCommand::SetLineWidth(s_Data->LineWidth);
        RenderCommand::DrawLines(s_Data->LineVertexArray, s_Data->LineVertexCount);
        Renderer::GetStats().DrawCalls++;
        s_Data->LineVertexCount = 0;
        s_Data->LineVertexBufferPtr = s_Data->LineVertexBufferBase;
    }

    // Draws what is pending and empties the batch (flushing twice draws nothing twice).
    void Renderer2D::Flush()
    {
        FlushQuads();
        FlushCircles();
        FlushLines();
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

    // Quads and circles are drawn in the order they were submitted: what is pending of the other
    // kind is drawn first. (A batch draws its quads, then its circles: a circle submitted after a
    // text glyph or a sprite in front of it would fail the depth test on that quad's blended edge
    // and leave a fringe of the colour behind both.) Lines stay at the end of the batch.
    static void BeginQuads()
    {
        if (s_Data->CircleIndexCount)
            FlushCircles();
    }

    static void SubmitQuad(const glm::mat4& transform, const glm::vec4& color, float texIndex, float tiling,
                           const glm::vec2* texCoords, int entityID, const Renderer2D::SpriteEffect* effect = nullptr,
                           const glm::vec4& uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f))
    {
        BeginQuads();
        const glm::vec4 flash = effect && effect->HasFlash() ? glm::vec4(glm::vec3(effect->FlashColor), std::min(1.0f, effect->FlashAmount * effect->FlashColor.a))
                                                             : glm::vec4(0.0f);
        const bool outline = effect && effect->HasOutline();
        for (int i = 0; i < 4; i++)
        {
            s_Data->QuadVertexBufferPtr->Position = transform * s_Data->QuadVertexPositions[i];
            s_Data->QuadVertexBufferPtr->Color = color;
            s_Data->QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data->QuadVertexBufferPtr->TexIndex = texIndex;
            s_Data->QuadVertexBufferPtr->TilingFactor = tiling;
            s_Data->QuadVertexBufferPtr->EntityID = entityID;
            s_Data->QuadVertexBufferPtr->Flash = flash;
            s_Data->QuadVertexBufferPtr->OutlineColor = outline ? effect->OutlineColor : glm::vec4(0.0f);
            s_Data->QuadVertexBufferPtr->UVRect = uvRect;
            s_Data->QuadVertexBufferPtr->OutlineWidth = outline ? effect->OutlineThickness : 0.0f;
            s_Data->QuadVertexBufferPtr->Lit = s_Data->CurrentLit;
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

    void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, const glm::vec2& uvMin, const glm::vec2& uvMax,
                              const glm::vec4& tintColor, const SpriteEffect& effect, const glm::vec4& uvRect, int entityID)
    {
        if (s_Data->QuadIndexCount >= Renderer2DData::MaxIndices)
            NextBatch();
        float texIndex = GetTextureIndex(texture);
        const glm::vec2 coords[4] = { { uvMin.x, uvMin.y }, { uvMax.x, uvMin.y }, { uvMax.x, uvMax.y }, { uvMin.x, uvMax.y } };
        SpriteEffect e = effect;
        if (!texture)
            e.OutlineThickness = 0.0f; // a plain quad has no picture to outline
        SubmitQuad(transform, tintColor, texIndex, 1.0f, coords, entityID, &e, uvRect);
    }

    // ------------------------------------------------------------------ circles
    void Renderer2D::DrawQuadCorners(const glm::vec2 positions[4], const glm::vec2 uvs[4], const Ref<Texture2D>& texture,
                                     const glm::vec4& tintColor, int entityID, float z)
    {
        if (s_Data->QuadIndexCount >= Renderer2DData::MaxIndices)
            NextBatch();
        float texIndex = GetTextureIndex(texture);
        BeginQuads();
        for (int i = 0; i < 4; i++)
        {
            s_Data->QuadVertexBufferPtr->Position = glm::vec3(positions[i], z);
            s_Data->QuadVertexBufferPtr->Color = tintColor;
            s_Data->QuadVertexBufferPtr->TexCoord = uvs[i];
            s_Data->QuadVertexBufferPtr->TexIndex = texIndex;
            s_Data->QuadVertexBufferPtr->TilingFactor = 1.0f;
            s_Data->QuadVertexBufferPtr->EntityID = entityID;
            s_Data->QuadVertexBufferPtr->Flash = glm::vec4(0.0f);
            s_Data->QuadVertexBufferPtr->OutlineColor = glm::vec4(0.0f);
            s_Data->QuadVertexBufferPtr->UVRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            s_Data->QuadVertexBufferPtr->OutlineWidth = 0.0f;
            s_Data->QuadVertexBufferPtr->Lit = s_Data->CurrentLit;
            s_Data->QuadVertexBufferPtr++;
        }
        s_Data->QuadIndexCount += 6;
        Renderer::GetStats().QuadCount++;
    }

    void Renderer2D::DrawQuadVertices(const glm::vec3 positions[4], const glm::vec4 colors[4], const glm::vec2 uvs[4], const Ref<Texture2D>& texture,
                                      int entityID)
    {
        if (s_Data->QuadIndexCount >= Renderer2DData::MaxIndices)
            NextBatch();
        float texIndex = GetTextureIndex(texture);
        BeginQuads();
        for (int i = 0; i < 4; i++)
        {
            s_Data->QuadVertexBufferPtr->Position = positions[i];
            s_Data->QuadVertexBufferPtr->Color = colors[i];
            s_Data->QuadVertexBufferPtr->TexCoord = uvs[i];
            s_Data->QuadVertexBufferPtr->TexIndex = texIndex;
            s_Data->QuadVertexBufferPtr->TilingFactor = 1.0f;
            s_Data->QuadVertexBufferPtr->EntityID = entityID;
            s_Data->QuadVertexBufferPtr->Flash = glm::vec4(0.0f);
            s_Data->QuadVertexBufferPtr->OutlineColor = glm::vec4(0.0f);
            s_Data->QuadVertexBufferPtr->UVRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            s_Data->QuadVertexBufferPtr->OutlineWidth = 0.0f;
            s_Data->QuadVertexBufferPtr->Lit = s_Data->CurrentLit;
            s_Data->QuadVertexBufferPtr++;
        }
        s_Data->QuadIndexCount += 6;
        Renderer::GetStats().QuadCount++;
    }

    void Renderer2D::SetClipRect(bool enabled, const glm::vec2& min01, const glm::vec2& max01)
    {
        Flush();
        RenderCommand::SetScissor(enabled, min01, max01);
    }

    void Renderer2D::DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness, float fade, int entityID)
    {
        if (s_Data->CircleIndexCount >= Renderer2DData::MaxIndices)
            NextBatch();
        if (s_Data->QuadIndexCount)
            FlushQuads(); // keeps the submission order (see BeginQuads)

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

    void Renderer2D::SetLighting(const Lighting2DData& lighting)
    {
        if (!s_Data->LightingDirty && s_Data->Lighting == lighting)
            return;
        // What was submitted before was meant for the old lights.
        FlushQuads();
        s_Data->Lighting = lighting;
        if (s_Data->Lighting.Lights.size() > (size_t)Light2DMath::MaxLights)
            s_Data->Lighting.Lights.resize((size_t)Light2DMath::MaxLights);
        s_Data->LightingDirty = true;
    }

    const Lighting2DData& Renderer2D::GetLighting() { return s_Data->Lighting; }

    void Renderer2D::SetLit(bool lit) { s_Data->CurrentLit = lit ? 1.0f : 0.0f; }

    void Renderer2D::SetDisplayOutput(bool display)
    {
        if ((s_Data->DisplayOutput == 1) == display)
            return;
        Flush(); // what is pending was meant for the other output
        s_Data->DisplayOutput = display ? 1 : 0;
    }
    bool Renderer2D::IsLit() { return s_Data->CurrentLit > 0.5f; }
    bool Renderer2D::IsDisplayOutput() { return s_Data->DisplayOutput == 1; }
    const glm::mat4& Renderer2D::GetViewProjection() { return s_Data->ViewProjection; }

    float Renderer2D::GetLineWidth() { return s_Data->LineWidth; }
    void Renderer2D::SetLineWidth(float width) { s_Data->LineWidth = width; }

    // ------------------------------------------------------------------ text
    float Renderer2D::GetTextPixelSize(const glm::mat4& transform)
    {
        // One em of the text (its local unit square) through the camera onto the target: the
        // geometric mean of its two sides there, in pixels.
        const glm::vec2 viewport = s_Data->ViewportSize;
        if (viewport.x <= 0.0f || viewport.y <= 0.0f)
            return 0.0f;
        const glm::mat4 m = s_Data->ViewProjection * transform;
        const glm::vec4 o = m * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const glm::vec4 x = m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        const glm::vec4 y = m * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        if (o.w <= 1e-6f || x.w <= 1e-6f || y.w <= 1e-6f)
            return 0.0f; // behind the camera
        const glm::vec2 po = glm::vec2(o) / o.w;
        const glm::vec2 half = viewport * 0.5f;
        const float sx = glm::length((glm::vec2(x) / x.w - po) * half);
        const float sy = glm::length((glm::vec2(y) / y.w - po) * half);
        return std::sqrt(sx * sy);
    }

    void Renderer2D::DrawString(std::string_view text, const Ref<Font>& font, const glm::mat4& transform,
                                const TextParams& params, int entityID)
    {
        if (!font || !font->IsLoaded())
            return;
        BeginQuads();
        // Glyph pictures from the atlas baked nearest the size the text has on the target; the
        // layout (advances) always from the font's metrics, so it does not depend on the size.
        const FontAtlas& sized = font->GetAtlas(GetTextPixelSize(transform));
        const Ref<Texture2D>& atlas = sized.Texture;

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
            const Glyph* q = sized.Find(cp);
            if (!q) q = g;

            if (q->X1 > q->X0)
            {
                if (s_Data->QuadIndexCount >= Renderer2DData::MaxIndices)
                    NextBatch();
                float texIndex = GetTextureIndex(atlas);

                glm::vec4 corners[4] = {
                    { x + q->X0, y + q->Y0, 0.0f, 1.0f },
                    { x + q->X1, y + q->Y0, 0.0f, 1.0f },
                    { x + q->X1, y + q->Y1, 0.0f, 1.0f },
                    { x + q->X0, y + q->Y1, 0.0f, 1.0f },
                };
                glm::vec2 uvs[4] = { { q->U0, q->V0 }, { q->U1, q->V0 }, { q->U1, q->V1 }, { q->U0, q->V1 } };
                for (int k = 0; k < 4; k++)
                {
                    s_Data->QuadVertexBufferPtr->Position = transform * corners[k];
                    s_Data->QuadVertexBufferPtr->Color = params.Color;
                    s_Data->QuadVertexBufferPtr->TexCoord = uvs[k];
                    s_Data->QuadVertexBufferPtr->TexIndex = texIndex;
                    s_Data->QuadVertexBufferPtr->TilingFactor = 1.0f;
                    s_Data->QuadVertexBufferPtr->EntityID = entityID;
                    s_Data->QuadVertexBufferPtr->Flash = glm::vec4(0.0f);
                    s_Data->QuadVertexBufferPtr->OutlineColor = glm::vec4(0.0f);
                    s_Data->QuadVertexBufferPtr->UVRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
                    s_Data->QuadVertexBufferPtr->OutlineWidth = 0.0f;
                    s_Data->QuadVertexBufferPtr->Lit = s_Data->CurrentLit;
                    s_Data->QuadVertexBufferPtr++;
                }
                s_Data->QuadIndexCount += 6;
                Renderer::GetStats().QuadCount++;
            }
            x += g->Advance + params.Kerning;
        }
    }

    AABB Renderer2D::GetQuadBounds(const glm::mat4& transform)
    {
        return TransformAABB(AABB{ { -0.5f, -0.5f, 0.0f }, { 0.5f, 0.5f, 0.0f } }, transform);
    }

    AABB Renderer2D::GetStringBounds(std::string_view text, const Ref<Font>& font, const TextParams& params)
    {
        AABB box{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(-std::numeric_limits<float>::max()) };
        if (!font || !font->IsLoaded())
            return box;
        // The same layout as DrawString.
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
                box.Min = glm::min(box.Min, glm::vec3(x + std::min(g->X0, g->X1), y + std::min(g->Y0, g->Y1), 0.0f));
                box.Max = glm::max(box.Max, glm::vec3(x + std::max(g->X0, g->X1), y + std::max(g->Y0, g->Y1), 0.0f));
            }
            x += g->Advance + params.Kerning;
        }
        return box;
    }

    glm::vec2 Renderer2D::MeasureString(std::string_view text, const Ref<Font>& font, const TextParams& params)
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
