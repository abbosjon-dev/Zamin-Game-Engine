#include "Zamin/Renderer/SelectionOutline.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Renderer/Animation.h"
#include "Zamin/Renderer/Framebuffer.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Shader.h"
#include "Zamin/Renderer/Texture.h"

#include <entt/entity/entity.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace Zamin {

    // The shaders split entity IDs the way EnTT builds them.
    static_assert(entt::entt_traits<entt::entity>::entity_mask == OutlineLookup::IndexMask,
                  "OutlineLookup::IndexBits must match EnTT's entity index bits");

    // ------------------------------------------------------------------ lookup table
    OutlineLookup OutlineLookup::Build(const std::vector<OutlineEntity>& entities)
    {
        OutlineLookup lookup;
        uint32_t maxIndex = 0;
        bool any = false;
        for (const OutlineEntity& e : entities)
        {
            if (e.EntityID == -1 || (e.Category == OutlineCategory::None && !e.Hovered))
                continue;
            maxIndex = std::max(maxIndex, (uint32_t)e.EntityID & IndexMask);
            any = true;
        }
        if (!any)
            return lookup;
        lookup.Height = maxIndex / Width + 1;
        lookup.Texels.assign((size_t)Width * lookup.Height * 4, 0);
        for (const OutlineEntity& e : entities)
        {
            if (e.EntityID == -1 || (e.Category == OutlineCategory::None && !e.Hovered))
                continue;
            const uint32_t id = (uint32_t)e.EntityID, index = id & IndexMask, version = id >> IndexBits;
            uint8_t* t = &lookup.Texels[(size_t)index * 4];
            const uint32_t stored = (uint32_t)t[1] | ((uint32_t)t[2] << 8);
            if ((t[0] != 0 || t[3] != 0) && stored != version)
                continue; // the slot belongs to another version of the index (not in one scene)
            t[0] = std::max(t[0], (uint8_t)e.Category);
            t[1] = (uint8_t)(version & 0xFFu);
            t[2] = (uint8_t)((version >> 8) & 0xFFu);
            if (e.Hovered)
                t[3] = 255;
        }
        // The hover outline marks what a click would newly select.
        for (size_t i = 0; i < lookup.Texels.size(); i += 4)
            if (lookup.Texels[i] != 0)
                lookup.Texels[i + 3] = 0;
        return lookup;
    }

    static const uint8_t* FindTexel(const OutlineLookup& lookup, int entityID)
    {
        if (entityID == -1 || lookup.IsEmpty())
            return nullptr;
        const uint32_t id = (uint32_t)entityID, index = id & OutlineLookup::IndexMask, version = id >> OutlineLookup::IndexBits;
        if (index / OutlineLookup::Width >= lookup.Height)
            return nullptr;
        const uint8_t* t = &lookup.Texels[(size_t)index * 4];
        if (((uint32_t)t[1] | ((uint32_t)t[2] << 8)) != version)
            return nullptr;
        return t;
    }

    OutlineCategory OutlineLookup::GetCategory(int entityID) const
    {
        const uint8_t* t = FindTexel(*this, entityID);
        return t ? (OutlineCategory)std::min<uint8_t>(t[0], (uint8_t)OutlineCategory::Primary) : OutlineCategory::None;
    }

    bool OutlineLookup::IsHovered(int entityID) const
    {
        const uint8_t* t = FindTexel(*this, entityID);
        return t && t[3] != 0;
    }

    float SelectionOutline::Coverage(float distance, float width)
    {
        return std::clamp(width + 1.0f - distance, 0.0f, 1.0f);
    }

    int SelectionOutline::GetSearchRadius(float width, float hoverWidth)
    {
        float w = std::max(width, hoverWidth);
        if (!std::isfinite(w)) w = SelectionOutlineSettings::MaxWidth;
        w = std::clamp(w, SelectionOutlineSettings::MinWidth, SelectionOutlineSettings::MaxWidth);
        return (int)std::ceil(w + 1.0f);
    }

    // ------------------------------------------------------------------ shaders
    namespace {

        const char* s_FullscreenVS = R"(#version 410 core
void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

        // Finding an entity ID picture's pixel in the lookup table (OutlineLookup).
        const char* s_LookupGLSL = R"(
uniform isampler2D u_EntityIDs;
uniform vec2 u_IDSize;
uniform vec2 u_TargetSize;
uniform sampler2D u_Lookup;
uniform int u_LookupHeight;

const int LOOKUP_WIDTH = 1024;
const uint INDEX_BITS = 20u;

// (category / 3, hovered) of what the entity ID picture shows under this target pixel.
vec2 LookupPixel()
{
    ivec2 size = ivec2(u_IDSize + 0.5);
    ivec2 p = clamp(ivec2(gl_FragCoord.xy / u_TargetSize * u_IDSize), ivec2(0), size - 1);
    int id = texelFetch(u_EntityIDs, p, 0).r;
    if (id == -1)
        return vec2(0.0);
    uint u = uint(id);
    int index = int(u & ((1u << INDEX_BITS) - 1u));
    int version = int(u >> INDEX_BITS);
    int row = index / LOOKUP_WIDTH;
    if (row >= u_LookupHeight)
        return vec2(0.0);
    vec4 t = texelFetch(u_Lookup, ivec2(index - row * LOOKUP_WIDTH, row), 0);
    int stored = int(t.g * 255.0 + 0.5) | (int(t.b * 255.0 + 0.5) << 8);
    if (stored != version)
        return vec2(0.0);
    return vec2(floor(t.r * 255.0 + 0.5) / 3.0, t.a > 0.5 ? 1.0 : 0.0);
}
)";

        // Mask: R the visible selection (category / 3), G the selection's silhouette, B the
        // visible hovered entity, A its silhouette. What is visible is part of the silhouette.
        const char* s_MaskFS = R"(
layout(location = 0) out vec4 o_Mask;
void main()
{
    vec2 e = LookupPixel();
    o_Mask = vec4(e.x, e.x, e.y, e.y);
}
)";

        const char* s_SilhouetteVS = R"(#version 410 core
layout(location = 0) in vec3 a_Position;
layout(location = 2) in vec2 a_TexCoord;
#ifdef SKINNED
layout(location = 3) in ivec4 a_Joints;
layout(location = 4) in vec4 a_Weights;
uniform mat4 u_Joints[MAX_JOINTS];
#endif
uniform mat4 u_ViewProjection;
uniform mat4 u_Model;
uniform vec2 u_Tiling;
uniform vec2 u_Offset;
out vec2 v_TexCoord;
void main()
{
    vec4 p = vec4(a_Position, 1.0);
#ifdef SKINNED
    mat4 skin = a_Weights.x * u_Joints[a_Joints.x] + a_Weights.y * u_Joints[a_Joints.y]
              + a_Weights.z * u_Joints[a_Joints.z] + a_Weights.w * u_Joints[a_Joints.w];
    p = skin * p;
#endif
    v_TexCoord = a_TexCoord * u_Tiling + u_Offset;
    gl_Position = u_ViewProjection * u_Model * p;
}
)";

        // A selected (or hovered) mesh's silhouette (no depth test: hidden parts too), blended
        // into the mask with the larger value per channel (overlapping silhouettes keep the
        // higher category).
        const char* s_SilhouetteFS = R"(
layout(location = 0) out vec4 o_Mask;
in vec2 v_TexCoord;
uniform float u_Category;
uniform float u_Hovered;
uniform sampler2D u_AlphaMap;
uniform float u_AlbedoAlpha;
uniform float u_AlphaCutoff;
void main()
{
    if (u_AlphaCutoff >= 0.0 && u_AlbedoAlpha * texture(u_AlphaMap, v_TexCoord).a < u_AlphaCutoff)
        discard;
    o_Mask = vec4(0.0, u_Category, 0.0, u_Hovered);
}
)";

        // The contour: for each pixel outside a silhouette the nearest marked pixel within the
        // search radius (Euclidean, between pixel centres) gives its coverage and colour - a
        // visible one full strength, one only in the silhouette (hidden) faint.
        const char* s_CompositeFS = R"(#version 410 core
layout(location = 0) out vec4 o_Color;
uniform sampler2D u_Mask;
uniform vec2 u_TargetSize;
uniform int u_Radius;
uniform float u_Width;
uniform float u_HoverWidth;
uniform int u_ShowOccluded;
uniform float u_OccludedOpacity;
uniform int u_Hover;
uniform vec4 u_Colors[4]; // hover, child, secondary, primary

float Coverage(float distance, float width) { return clamp(width + 1.0 - distance, 0.0, 1.0); }

vec4 CategoryColor(float category) { return u_Colors[clamp(int(category * 3.0 + 0.5), 1, 3)]; }

void main()
{
    ivec2 size = ivec2(u_TargetSize + 0.5);
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 here = texelFetch(u_Mask, p, 0);
    bool insideSelection = here.g > 0.0;
    bool insideHover = here.a > 0.0;
    if (insideSelection && (insideHover || u_Hover == 0))
        discard;

    const float none = 1e9;
    float visible = none, silhouette = none, hover = none, hoverSilhouette = none;
    float visibleCategory = 0.0, silhouetteCategory = 0.0;
    for (int y = -u_Radius; y <= u_Radius; y++)
        for (int x = -u_Radius; x <= u_Radius; x++)
        {
            ivec2 q = p + ivec2(x, y);
            if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y)
                continue;
            vec4 m = texelFetch(u_Mask, q, 0);
            float d = float(x * x + y * y);
            if (m.r > 0.0 && (d < visible || (d == visible && m.r > visibleCategory))) { visible = d; visibleCategory = m.r; }
            if (m.g > 0.0 && (d < silhouette || (d == silhouette && m.g > silhouetteCategory))) { silhouette = d; silhouetteCategory = m.g; }
            if (m.b > 0.0 && d < hover) hover = d;
            if (m.a > 0.0 && d < hoverSilhouette) hoverSilhouette = d;
        }

    vec4 selection = vec4(0.0);
    if (!insideSelection)
    {
        float a = visible < none ? Coverage(sqrt(visible), u_Width) : 0.0;
        vec4 color = visible < none ? CategoryColor(visibleCategory) : vec4(0.0);
        if (u_ShowOccluded != 0 && silhouette < none)
        {
            float faint = Coverage(sqrt(silhouette), u_Width) * u_OccludedOpacity;
            if (faint > a)
            {
                a = faint;
                color = CategoryColor(silhouetteCategory);
            }
        }
        selection = vec4(color.rgb, color.a * a);
    }
    vec4 hovered = vec4(0.0);
    if (u_Hover != 0 && !insideHover && !insideSelection)
    {
        float a = hover < none ? Coverage(sqrt(hover), u_HoverWidth) : 0.0;
        if (u_ShowOccluded != 0 && hoverSilhouette < none)
            a = max(a, Coverage(sqrt(hoverSilhouette), u_HoverWidth) * u_OccludedOpacity);
        hovered = vec4(u_Colors[0].rgb, u_Colors[0].a * a);
    }

    // the selection over the hover contour
    float alpha = selection.a + hovered.a * (1.0 - selection.a);
    if (alpha <= 0.002)
        discard;
    vec3 rgb = (selection.rgb * selection.a + hovered.rgb * hovered.a * (1.0 - selection.a)) / alpha;
    o_Color = vec4(rgb, alpha);
}
)";

        std::string WithDefines(const char* head, const std::string& defines)
        {
            std::string s(head);
            size_t eol = s.find('\n');
            return s.substr(0, eol + 1) + defines + s.substr(eol + 1);
        }

        struct OutlineGPU
        {
            Ref<Shader> Mask, Silhouette, SkinnedSilhouette, Composite;
            Ref<Framebuffer> MaskTarget;
            Ref<Texture2D> Lookup;
            uint32_t LookupHeight = 0;
            Ref<Texture2D> White;
            bool Initialized = false;
            bool Failed = false;
        };

        OutlineGPU*& GPUPtr()
        {
            static OutlineGPU* gpu = nullptr;
            return gpu;
        }

        OutlineGPU* EnsureGPU()
        {
            OutlineGPU*& g = GPUPtr();
            if (!g)
                g = new OutlineGPU();
            if (g->Initialized)
                return g->Failed ? nullptr : g;
            g->Initialized = true;
            const std::string head = "#version 410 core\n";
            const std::string skinned = "#define SKINNED\n#define MAX_JOINTS " + std::to_string(MaxJoints) + "\n";
            g->Mask = Shader::Create("SelectionOutline_Mask", s_FullscreenVS, head + s_LookupGLSL + s_MaskFS);
            g->Silhouette = Shader::Create("SelectionOutline_Silhouette", s_SilhouetteVS, head + s_SilhouetteFS);
            g->SkinnedSilhouette = Shader::Create("SelectionOutline_Silhouette_Skinned", WithDefines(s_SilhouetteVS, skinned),
                                                  head + s_SilhouetteFS);
            g->Composite = Shader::Create("SelectionOutline_Composite", s_FullscreenVS, s_CompositeFS);
            for (const Ref<Shader>& shader : { g->Mask, g->Silhouette, g->SkinnedSilhouette, g->Composite })
                if (!shader || !shader->IsValid())
                {
                    ZM_CORE_ERROR("Tanlash konturi shaderlari kompilyatsiya qilinmadi: kontur ko'rinmaydi");
                    g->Failed = true;
                    return nullptr;
                }
            TextureSpecification white;
            white.GenerateMips = false;
            white.SRGB = false;
            g->White = Texture2D::Create(white);
            uint32_t pixel = 0xffffffff;
            g->White->SetData(&pixel, 4);
            return g;
        }

        void UploadLookup(OutlineGPU& g, const OutlineLookup& lookup)
        {
            if (!g.Lookup || g.LookupHeight != lookup.Height)
            {
                TextureSpecification spec;
                spec.Width = OutlineLookup::Width;
                spec.Height = lookup.Height;
                spec.Format = ImageFormat::RGBA8;
                spec.Filter = TextureFilter::Nearest;
                spec.Wrap = TextureWrap::ClampToEdge;
                spec.GenerateMips = false;
                spec.SRGB = false;
                g.Lookup = Texture2D::Create(spec);
                g.LookupHeight = lookup.Height;
            }
            g.Lookup->SetData(lookup.Texels.data(), (uint32_t)lookup.Texels.size());
        }

        void SetLookupUniforms(Shader& shader, const SelectionOutlineInput& in, uint32_t lookupHeight)
        {
            shader.SetInt("u_EntityIDs", 0);
            shader.SetInt("u_Lookup", 1);
            shader.SetInt("u_LookupHeight", (int)lookupHeight);
            shader.SetFloat2("u_TargetSize", { (float)in.Width, (float)in.Height });
            shader.SetFloat2("u_IDSize", { (float)in.IDWidth, (float)in.IDHeight });
        }
    }

    void SelectionOutline::Shutdown()
    {
        OutlineGPU*& g = GPUPtr();
        delete g;
        g = nullptr;
    }

    void SelectionOutline::Render(const SelectionOutlineInput& in, const SelectionOutlineSettings& settings)
    {
        if (settings.Style != SelectionStyle::Outline || in.Entities.empty() || !in.EntityIDTexture || in.Width == 0 ||
            in.Height == 0 || in.IDWidth == 0 || in.IDHeight == 0)
            return;
        const OutlineLookup lookup = OutlineLookup::Build(in.Entities);
        if (lookup.IsEmpty())
            return;
        OutlineGPU* g = EnsureGPU();
        if (!g)
            return;
        ZM_PROFILE_SCOPE("SelectionOutline");

        // The caller's target first: making or resizing a framebuffer unbinds it.
        RenderCommand::SaveRenderTarget();
        UploadLookup(*g, lookup);
        if (!g->MaskTarget)
        {
            FramebufferSpecification spec;
            spec.Width = in.Width;
            spec.Height = in.Height;
            spec.Attachments = { FramebufferTextureFormat::RGBA8 };
            g->MaskTarget = Framebuffer::Create(spec);
        }
        else if (const auto& spec = g->MaskTarget->GetSpecification(); spec.Width != in.Width || spec.Height != in.Height)
            g->MaskTarget->Resize(in.Width, in.Height);

        // ---- mask: what the entity ID picture shows, then the silhouettes of the outlined meshes
        g->MaskTarget->Bind();
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetCullMode(CullMode::None);
        RenderCommand::SetBlendMode(BlendMode::None);
        RenderCommand::BindTextureUnit(0, in.EntityIDTexture);
        RenderCommand::BindTextureUnit(1, g->Lookup->GetRendererID());
        g->Mask->Bind();
        SetLookupUniforms(*g->Mask, in, lookup.Height);
        RenderCommand::DrawArrays(3);

        if (in.Silhouettes && !in.Silhouettes->empty())
        {
            RenderCommand::SetBlendMode(BlendMode::Max);
            for (const Ref<Shader>& shader : { g->Silhouette, g->SkinnedSilhouette })
            {
                shader->Bind();
                shader->SetInt("u_AlphaMap", 2);
                shader->SetMat4("u_ViewProjection", in.ViewProjection);
            }
            for (const Renderer3D::CapturedDraw& draw : *in.Silhouettes)
            {
                const OutlineCategory category = lookup.GetCategory(draw.EntityID);
                const bool hovered = lookup.IsHovered(draw.EntityID);
                if ((category == OutlineCategory::None && !hovered) || !draw.MeshRef || !draw.MeshRef->GetVertexArray())
                    continue;
                const bool skinned = draw.MeshRef->IsSkinned();
                Shader& shader = skinned ? *g->SkinnedSilhouette : *g->Silhouette;
                shader.Bind();
                shader.SetMat4("u_Model", draw.Transform);
                shader.SetFloat("u_Category", (float)category / 3.0f);
                shader.SetFloat("u_Hovered", hovered ? 1.0f : 0.0f);
                shader.SetFloat("u_AlphaCutoff", draw.AlphaCutoff);
                shader.SetFloat("u_AlbedoAlpha", draw.AlbedoAlpha);
                shader.SetFloat2("u_Tiling", draw.Tiling);
                shader.SetFloat2("u_Offset", draw.Offset);
                if (skinned && !draw.Joints.empty())
                    shader.SetMat4Array("u_Joints", draw.Joints.data(), (uint32_t)std::min<size_t>(draw.Joints.size(), MaxJoints));
                RenderCommand::BindTextureUnit(2, (draw.AlphaMap ? draw.AlphaMap : g->White)->GetRendererID());
                RenderCommand::DrawIndexed(draw.MeshRef->GetVertexArray());
            }
            RenderCommand::SetBlendMode(BlendMode::None);
        }
        RenderCommand::RestoreRenderTarget();

        // ---- the contour over the target
        Shader& composite = *g->Composite;
        composite.Bind();
        composite.SetInt("u_Mask", 0);
        composite.SetFloat2("u_TargetSize", { (float)in.Width, (float)in.Height });
        composite.SetInt("u_Radius", GetSearchRadius(settings.Width, settings.Hover ? settings.HoverWidth : 0.0f));
        composite.SetFloat("u_Width", settings.Width);
        composite.SetFloat("u_HoverWidth", settings.HoverWidth);
        composite.SetInt("u_ShowOccluded", settings.ShowOccluded ? 1 : 0);
        composite.SetFloat("u_OccludedOpacity", settings.OccludedOpacity);
        composite.SetInt("u_Hover", settings.Hover ? 1 : 0);
        const glm::vec4 colors[4] = { settings.HoverColor, settings.GetChildColor(), settings.GetSecondaryColor(), settings.Color };
        composite.SetFloat4Array("u_Colors", colors, 4);
        RenderCommand::BindTextureUnit(0, g->MaskTarget->GetColorAttachmentRendererID(0));
        RenderCommand::SetBlendMode(BlendMode::Alpha);
        RenderCommand::DrawArrays(3);

        RenderCommand::BindTextureUnit(0, 0);
        RenderCommand::BindTextureUnit(1, 0);
        RenderCommand::BindTextureUnit(2, 0);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetDepthTest(true);
    }

}
