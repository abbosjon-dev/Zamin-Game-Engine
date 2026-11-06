// Particle systems of a scene: running every emitter (ParticleSimulation does the work) and drawing
// a system into the 2D / billboard pass of the frame.
#include "Zamin/Scene/Scene.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/ParticleSystem.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <limits>

namespace Zamin {

    void Scene::UpdateParticles(Timestep ts, bool editor)
    {
        ZM_PROFILE_SCOPE("Particle'lar");
        for (auto e : m_Registry.view<ParticleSystemComponent>())
        {
            auto& ps = m_Registry.get<ParticleSystemComponent>(e);
            Entity entity{ e, this };
            // Not previewed in the editor, or inactive in the hierarchy: stopped and empty, so it
            // starts over (Burst included) when it plays again. Edit mode and direct writes to
            // TagComponent::Active end up here; at runtime SetActive already cleared it.
            if ((editor && !ps.PlayInEditor) || !IsActiveInHierarchy(entity))
            {
                if (!ps.Particles.empty() || ps.BurstDone || ps.EmitAccumulator != 0.0f)
                    ps.Clear();
                continue;
            }
            ParticleSimulation::Update(ps, GetWorldTransform(entity), ts, editor);
        }
    }

    void Scene::EmitParticles(Entity entity, int count)
    {
        if (!entity.IsValid() || entity.GetScene() != this || !IsActiveInHierarchy(entity))
            return;
        if (auto* ps = entity.TryGetComponent<ParticleSystemComponent>())
            ParticleSimulation::Spawn(*ps, GetWorldTransform(entity), count);
    }

    AABB Scene::GetParticleBounds(const ParticleSystemComponent& ps, const glm::mat4& emitter)
    {
        // Every particle quad (any rotation, billboarded or not) fits in a sphere of half its
        // diagonal around its position.
        const float radius = ParticleSimulation::GetMaxSize(ps) * 0.70711f;
        AABB box{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(-std::numeric_limits<float>::max()) };
        for (const auto& p : ps.Particles)
        {
            glm::vec3 pos = ps.WorldSpace ? p.Position : glm::vec3(emitter * glm::vec4(p.Position, 1.0f));
            box.Min = glm::min(box.Min, pos);
            box.Max = glm::max(box.Max, pos);
        }
        box.Min -= glm::vec3(radius);
        box.Max += glm::vec3(radius);
        return box;
    }

    // The flipbook frames of a system's texture: their UV rectangles and how big each is drawn in
    // its square (a sprite frame keeps its aspect inside it). Empty: the whole texture.
    struct ParticleFrames
    {
        struct Frame
        {
            glm::vec2 UVMin{ 0.0f }, UVMax{ 1.0f };
            glm::vec2 Fit{ 1.0f };
        };
        std::vector<Frame> Frames;
    };

    static void GatherFrames(const ParticleSystemComponent& ps, ParticleFrames& out, const AssetRegistry* registry)
    {
        out.Frames.clear();
        const auto& sheet = ps.TextureSheet;
        if (!sheet.Enabled || !ps.Texture)
            return;
        const uint32_t width = ps.Texture->GetWidth(), height = ps.Texture->GetHeight();
        if (sheet.Source == ParticleSystemComponent::TextureSheetModule::FrameSource::Grid)
        {
            const int count = ParticleSimulation::GetGridFrameCount(ps);
            out.Frames.reserve((size_t)count);
            for (int i = 0; i < count; i++)
            {
                SpriteRegion cell = SpriteRegion::GridCell(std::max(1, sheet.Columns), std::max(1, sheet.Rows), i, width, height);
                out.Frames.push_back({ cell.UVMin, cell.UVMax, glm::vec2(1.0f) });
            }
            return;
        }
        const SpriteSheet* sprites = ps.TexturePath.empty() ? nullptr : SpriteSheet::Get(registry, ps.TexturePath);
        if (!sprites)
            return;
        auto add = [&](const SpriteFrame& frame) {
            ParticleFrames::Frame f;
            frame.GetUV(width, height, f.UVMin, f.UVMax);
            const glm::vec2 size = glm::max(glm::vec2(frame.Size), glm::vec2(1.0f));
            f.Fit = size / std::max(size.x, size.y);
            out.Frames.push_back(f);
        };
        if (sheet.SpritePrefix.empty())
        {
            for (const SpriteFrame& frame : sprites->Frames)
                add(frame);
        }
        else
        {
            for (const std::string& name : sprites->FindSequence(sheet.SpritePrefix))
                if (const SpriteFrame* frame = sprites->Find(name))
                    add(*frame);
        }
    }

    void Scene::DrawParticleSystem(ParticleSystemComponent& ps, const glm::mat4& transform, const glm::mat4& billboard, int id,
                                   const AssetRegistry* registry)
    {
        // What came before is drawn with depth writes; the particles without them.
        Renderer2D::Flush();
        if (ps.Additive) Renderer2D::SetBlendMode(BlendMode::Additive);
        RenderCommand::SetDepthWrite(false);
        if (!ps.TexturePath.empty() && !ps.Texture)
            ps.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(ps.TexturePath));
        static thread_local ParticleFrames frames;
        GatherFrames(ps, frames, registry);
        const int frameCount = (int)frames.Frames.size();
        for (const auto& p : ps.Particles)
        {
            glm::vec4 color = ParticleSimulation::GetColor(ps, p);
            float size = ParticleSimulation::GetSize(ps, p);
            glm::vec3 pos = ps.WorldSpace ? p.Position : glm::vec3(transform * glm::vec4(p.Position, 1.0f));
            glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
            if (ps.Billboard) m = m * billboard;
            m = m * glm::rotate(glm::mat4(1.0f), p.Rotation, glm::vec3(0, 0, 1)) * glm::scale(glm::mat4(1.0f), glm::vec3(size));
            if (ps.Texture && frameCount > 0)
            {
                const ParticleFrames::Frame& f = frames.Frames[(size_t)ParticleSimulation::GetSheetFrame(ps, p, frameCount)];
                if (f.Fit != glm::vec2(1.0f))
                    m = m * glm::scale(glm::mat4(1.0f), glm::vec3(f.Fit, 1.0f));
                Renderer2D::DrawQuad(m, ps.Texture, f.UVMin, f.UVMax, color, id);
            }
            else if (ps.Texture)
                Renderer2D::DrawQuad(m, ps.Texture, 1.0f, color, id);
            else if (ps.RenderAsCircle)
                Renderer2D::DrawCircle(m, color, 1.0f, 0.6f, id);
            else
                Renderer2D::DrawQuad(m, color, id);
        }
        Renderer2D::Flush();
        RenderCommand::SetDepthWrite(true);
        if (ps.Additive) Renderer2D::SetBlendMode(BlendMode::Alpha);
    }

}
