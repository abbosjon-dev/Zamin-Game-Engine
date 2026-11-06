// UI animations of a scene: tweens of a UI element's look / layout / colour, the show / hide
// presets of UIAnimationComponent and the hover / press feedback of its widget. They run in the
// UI update (Scene::UpdateUI), on the UI's own time - also while the game is paused.
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"

#include <algorithm>

namespace Zamin {

    // The colour a Color tween animates: its UI Image's, else its Sprite / Circle Renderer's, else its Text's.
    static glm::vec4* UIColorOf(entt::registry& registry, entt::entity e)
    {
        if (auto* c = registry.try_get<UIImageComponent>(e)) return &c->Color;
        if (auto* c = registry.try_get<SpriteRendererComponent>(e)) return &c->Color;
        if (auto* c = registry.try_get<CircleRendererComponent>(e)) return &c->Color;
        if (auto* c = registry.try_get<TextComponent>(e)) return &c->Color;
        return nullptr;
    }

    glm::vec4 Scene::GetUITweenValue(Entity entity, UITweenProperty property, bool* ok)
    {
        if (ok)
            *ok = false;
        if (!entity || entity.GetScene() != this || !entity.HasComponent<UITransformComponent>())
            return glm::vec4(0.0f);
        const auto& t = entity.GetComponent<UITransformComponent>();
        glm::vec4 v(0.0f);
        switch (property)
        {
            case UITweenProperty::Scale:    v = glm::vec4(t.Scale, 0.0f, 0.0f); break;
            case UITweenProperty::Alpha:    v = glm::vec4(t.Alpha, 0.0f, 0.0f, 0.0f); break;
            case UITweenProperty::Offset:   v = glm::vec4(t.VisualOffset, 0.0f, 0.0f); break;
            case UITweenProperty::Position: v = glm::vec4(t.Position, 0.0f, 0.0f); break;
            case UITweenProperty::Size:     v = glm::vec4(t.Size, 0.0f, 0.0f); break;
            case UITweenProperty::Color:
            {
                const glm::vec4* color = UIColorOf(m_Registry, entity);
                if (!color)
                    return v;
                v = *color;
                break;
            }
        }
        if (ok)
            *ok = true;
        return v;
    }

    bool Scene::SetUITweenValue(entt::entity e, UITweenProperty property, const glm::vec4& value)
    {
        auto* t = m_Registry.try_get<UITransformComponent>(e);
        if (!t)
            return false;
        switch (property)
        {
            case UITweenProperty::Scale:    t->Scale = glm::vec2(value); break;
            case UITweenProperty::Alpha:    t->Alpha = std::clamp(value.x, 0.0f, 1.0f); break;
            case UITweenProperty::Offset:   t->VisualOffset = glm::vec2(value); break;
            case UITweenProperty::Position: t->Position = glm::vec2(value); break;
            case UITweenProperty::Size:     t->Size = glm::vec2(value); break;
            case UITweenProperty::Color:
            {
                glm::vec4* color = UIColorOf(m_Registry, e);
                if (!color)
                    return false;
                *color = value;
                break;
            }
        }
        return true;
    }

    uint64_t Scene::TweenUI(Entity entity, UITweenProperty property, const glm::vec4& to, float duration, UITweenOptions options)
    {
        bool ok = false;
        const glm::vec4 now = GetUITweenValue(entity, property, &ok);
        if (!ok)
            return 0;
        CancelUITweens(entity, property, false); // one tween per property
        UITween tw;
        tw.ID = m_NextUITweenID++;
        tw.Entity = entity;
        tw.Property = property;
        tw.To = to;
        tw.Duration = std::max(0.0f, duration);
        tw.Delay = std::max(0.0f, options.Delay);
        tw.Easing = options.Easing;
        tw.Loop = options.Loop;
        tw.Loops = options.Loops;
        tw.OnComplete = std::move(options.OnComplete);
        if (options.From)
        {
            // It starts from there now (also while it waits for its delay - a staggered show stays hidden).
            tw.From = *options.From;
            tw.FromSet = true;
            SetUITweenValue(entity, property, tw.From);
        }
        else if (tw.Delay <= 0.0f)
        {
            tw.From = now;
            tw.FromSet = true;
        }
        const uint64_t id = tw.ID;
        m_UITweens.push_back(std::move(tw));
        return id;
    }

    void Scene::CancelUITweens(Entity entity, bool complete)
    {
        for (int p = 0; p <= (int)UITweenProperty::Color; p++)
            CancelUITweens(entity, (UITweenProperty)p, complete);
    }

    void Scene::CancelUITweens(Entity entity, UITweenProperty property, bool complete)
    {
        if (!entity || entity.GetScene() != this)
            return;
        const entt::entity e = entity;
        for (UITween& tw : m_UITweens)
            if (!tw.Done && tw.Entity == e && tw.Property == property)
            {
                if (complete)
                    SetUITweenValue(e, property, tw.To);
                tw.Done = true;
            }
        if (!m_TickingUITweens)
            std::erase_if(m_UITweens, [](const UITween& tw) { return tw.Done; });
    }

    bool Scene::CancelUITween(uint64_t id)
    {
        for (UITween& tw : m_UITweens)
            if (tw.ID == id && !tw.Done)
            {
                tw.Done = true;
                if (!m_TickingUITweens)
                    std::erase_if(m_UITweens, [](const UITween& t) { return t.Done; });
                return true;
            }
        return false;
    }

    bool Scene::IsUITweening(Entity entity) const
    {
        const entt::entity e = entity;
        return std::any_of(m_UITweens.begin(), m_UITweens.end(), [&](const UITween& tw) { return !tw.Done && tw.Entity == e; });
    }

    void Scene::UpdateUITweens(float dt)
    {
        m_TickingUITweens = true;
        // By index: a callback may start new tweens (they are appended and run from the next frame on).
        const size_t count = m_UITweens.size();
        for (size_t i = 0; i < count && i < m_UITweens.size(); i++)
        {
            if (m_UITweens[i].Done)
                continue;
            if (!m_Registry.valid(m_UITweens[i].Entity) || !m_Registry.all_of<UITransformComponent>(m_UITweens[i].Entity))
            {
                m_UITweens[i].Done = true; // its entity is gone
                continue;
            }
            UITween& tw = m_UITweens[i];
            tw.Elapsed += dt;
            const float active = tw.Elapsed - tw.Delay;
            if (active < 0.0f)
                continue; // still waiting
            if (!tw.FromSet)
            {
                bool ok = false;
                tw.From = GetUITweenValue({ tw.Entity, this }, tw.Property, &ok);
                tw.FromSet = true;
            }
            bool finished = false;
            const float phase = UIAnimMath::Progress(active, tw.Duration, tw.Loop, tw.Loops, finished);
            glm::vec4 value = glm::mix(tw.From, tw.To, ApplyEase(tw.Easing, phase));
            if (finished)
                value = phase >= 1.0f ? tw.To : tw.From; // exactly where it ends
            SetUITweenValue(tw.Entity, tw.Property, value);
            if (finished)
            {
                tw.Done = true;
                std::function<void()> callback = std::move(tw.OnComplete);
                if (callback)
                    callback(); // `tw` may be gone after this (new tweens)
            }
        }
        std::erase_if(m_UITweens, [](const UITween& tw) { return tw.Done; });
        m_TickingUITweens = false;
    }

    // ------------------------------------------------------------------ show / hide presets
    void Scene::StartUIShow(entt::entity e)
    {
        auto& anim = m_Registry.get<UIAnimationComponent>(e);
        auto& t = m_Registry.get<UITransformComponent>(e);
        Entity entity{ e, this };
        // The look it comes back to: where it rests (an interrupted hide / show: the look saved then).
        if (!anim.RestSaved)
        {
            anim.RestLook = { t.Scale, t.VisualOffset, t.Alpha };
            anim.RestSaved = true;
        }
        anim.Hiding = false;
        const UIAnimMath::Look rest = anim.RestLook;
        CancelUITweens(entity, UITweenProperty::Scale);
        CancelUITweens(entity, UITweenProperty::Alpha);
        CancelUITweens(entity, UITweenProperty::Offset);
        if (anim.Show == UIAnimPreset::None)
        {
            t.Scale = rest.Scale;
            t.VisualOffset = rest.Offset;
            t.Alpha = rest.Alpha;
            anim.RestSaved = false;
            return;
        }
        const UIAnimMath::Look hidden = UIAnimMath::HiddenLook(anim.Show, rest, anim.SlideDistance);
        const float duration = anim.Duration;
        UITweenOptions main;
        main.Easing = anim.ShowEase;
        main.Delay = anim.ShowDelay;
        // Back at rest: the next show / hide starts from the look it has then.
        main.OnComplete = [this, e]() {
            if (auto* a = m_Registry.valid(e) ? m_Registry.try_get<UIAnimationComponent>(e) : nullptr; a && !a->Hiding)
                a->RestSaved = false;
        };
        UITweenOptions fade;
        fade.Easing = Ease::OutQuad;
        fade.Delay = anim.ShowDelay;
        fade.From = glm::vec4(hidden.Alpha, 0.0f, 0.0f, 0.0f);
        switch (anim.Show)
        {
            case UIAnimPreset::Fade:
                fade.OnComplete = std::move(main.OnComplete);
                TweenUI(entity, UITweenProperty::Alpha, glm::vec4(rest.Alpha, 0.0f, 0.0f, 0.0f), duration, std::move(fade));
                break;
            case UIAnimPreset::Pop:
                main.From = glm::vec4(hidden.Scale, 0.0f, 0.0f);
                TweenUI(entity, UITweenProperty::Alpha, glm::vec4(rest.Alpha, 0.0f, 0.0f, 0.0f), duration * 0.6f, std::move(fade));
                TweenUI(entity, UITweenProperty::Scale, glm::vec4(rest.Scale, 0.0f, 0.0f), duration, std::move(main));
                break;
            default: // slides
                main.From = glm::vec4(hidden.Offset, 0.0f, 0.0f);
                TweenUI(entity, UITweenProperty::Alpha, glm::vec4(rest.Alpha, 0.0f, 0.0f, 0.0f), duration * 0.6f, std::move(fade));
                TweenUI(entity, UITweenProperty::Offset, glm::vec4(rest.Offset, 0.0f, 0.0f), duration, std::move(main));
                break;
        }
    }

    void Scene::ShowUI(Entity entity)
    {
        if (!entity || entity.GetScene() != this)
            return;
        auto* anim = entity.TryGetComponent<UIAnimationComponent>();
        if (anim && anim->Hiding)
        {
            // A hide is playing: it stops; the show starts again from where it is now.
            anim->Hiding = false;
            if (entity.IsActive() && IsActiveInHierarchy(entity) && entity.HasComponent<UITransformComponent>())
            {
                StartUIShow(entity);
                anim->WasActive = true;
                return;
            }
        }
        SetActive(entity, true); // the UI update notices it and plays the show
        if (anim && m_IsRunning && IsActiveInHierarchy(entity) && !anim->WasActive && entity.HasComponent<UITransformComponent>())
        {
            // At once - nothing is drawn at full size for a frame before it.
            StartUIShow(entity);
            anim->WasActive = true;
        }
    }

    void Scene::HideUI(Entity entity, bool animate)
    {
        if (!entity || entity.GetScene() != this)
            return;
        auto* anim = entity.TryGetComponent<UIAnimationComponent>();
        auto* t = entity.TryGetComponent<UITransformComponent>();
        if (!anim || !t || !animate || !m_IsRunning || anim->Hide == UIAnimPreset::None || !IsActiveInHierarchy(entity))
        {
            if (anim && t && anim->RestSaved)
            {
                // Its look back to rest (a show / hide was playing).
                CancelUITweens(entity, UITweenProperty::Scale);
                CancelUITweens(entity, UITweenProperty::Alpha);
                CancelUITweens(entity, UITweenProperty::Offset);
                t->Scale = anim->RestLook.Scale;
                t->VisualOffset = anim->RestLook.Offset;
                t->Alpha = anim->RestLook.Alpha;
                anim->RestSaved = false;
            }
            if (anim)
                anim->Hiding = false;
            SetActive(entity, false);
            return;
        }
        if (anim->Hiding)
            return; // already on its way out
        if (!anim->RestSaved)
        {
            anim->RestLook = { t->Scale, t->VisualOffset, t->Alpha };
            anim->RestSaved = true;
        }
        anim->Hiding = true;
        const UIAnimMath::Look rest = anim->RestLook;
        const UIAnimMath::Look hidden = UIAnimMath::HiddenLook(anim->Hide, rest, anim->SlideDistance);
        const entt::entity e = entity;
        UITweenOptions main;
        main.Easing = anim->HideEase;
        // Gone: switched off, its look back to rest for the next show.
        main.OnComplete = [this, e]() {
            if (!m_Registry.valid(e))
                return;
            auto* a = m_Registry.try_get<UIAnimationComponent>(e);
            auto* tr = m_Registry.try_get<UITransformComponent>(e);
            if (!a || !tr || !a->Hiding)
                return;
            a->Hiding = false;
            CancelUITweens({ e, this }, UITweenProperty::Scale);
            CancelUITweens({ e, this }, UITweenProperty::Alpha);
            CancelUITweens({ e, this }, UITweenProperty::Offset);
            tr->Scale = a->RestLook.Scale;
            tr->VisualOffset = a->RestLook.Offset;
            tr->Alpha = a->RestLook.Alpha;
            a->RestSaved = false;
            SetActive({ e, this }, false);
            a->WasActive = false;
        };
        const float duration = anim->Duration;
        UITweenOptions fade;
        fade.Easing = Ease::InQuad;
        switch (anim->Hide)
        {
            case UIAnimPreset::Fade:
                fade.OnComplete = std::move(main.OnComplete);
                TweenUI(entity, UITweenProperty::Alpha, glm::vec4(hidden.Alpha, 0.0f, 0.0f, 0.0f), duration, std::move(fade));
                break;
            case UIAnimPreset::Pop:
                TweenUI(entity, UITweenProperty::Alpha, glm::vec4(hidden.Alpha, 0.0f, 0.0f, 0.0f), duration, std::move(fade));
                TweenUI(entity, UITweenProperty::Scale, glm::vec4(hidden.Scale, 0.0f, 0.0f), duration, std::move(main));
                break;
            default: // slides
                TweenUI(entity, UITweenProperty::Alpha, glm::vec4(hidden.Alpha, 0.0f, 0.0f, 0.0f), duration, std::move(fade));
                TweenUI(entity, UITweenProperty::Offset, glm::vec4(hidden.Offset, 0.0f, 0.0f), duration, std::move(main));
                break;
        }
    }

    bool Scene::IsUIHiding(Entity entity)
    {
        const auto* anim = entity && entity.GetScene() == this ? entity.TryGetComponent<UIAnimationComponent>() : nullptr;
        return anim && anim->Hiding;
    }

    void Scene::PunchUIScale(Entity entity, float amount, float duration)
    {
        if (!entity || entity.GetScene() != this || !entity.HasComponent<UITransformComponent>())
            return;
        // Springs back to where it rests (or to where a running scale tween is taking it).
        glm::vec4 rest = glm::vec4(entity.GetComponent<UITransformComponent>().Scale, 0.0f, 0.0f);
        for (const UITween& tw : m_UITweens)
            if (!tw.Done && tw.Entity == (entt::entity)entity && tw.Property == UITweenProperty::Scale && tw.Loop == UITweenLoop::None)
                rest = tw.To;
        UITweenOptions options;
        options.Easing = Ease::OutElastic;
        options.From = rest * (1.0f + amount);
        TweenUI(entity, UITweenProperty::Scale, rest, duration, std::move(options));
    }

    // ------------------------------------------------------------------ every UI update
    void Scene::StartPendingUIShows()
    {
        std::vector<entt::entity> shows;
        for (auto e : m_Registry.view<UIAnimationComponent>())
        {
            auto& anim = m_Registry.get<UIAnimationComponent>(e);
            if (!m_Registry.all_of<UITransformComponent>(e))
                continue;
            // Became active: its show (the first update of the run: only with ShowAtStart).
            const bool active = IsActiveInHierarchy({ e, this });
            if (active && !anim.WasActive && !anim.Hiding && anim.Show != UIAnimPreset::None && (anim.ShowAtStart || m_UIAnimationsStarted))
                shows.push_back(e);
            anim.WasActive = active;
        }
        SortInSceneOrder(shows);
        for (auto e : shows)
            if (m_Registry.valid(e) && m_Registry.all_of<UIAnimationComponent, UITransformComponent>(e))
                StartUIShow(e);
    }

    void Scene::UpdateUIAnimations(float dt)
    {
        StartPendingUIShows();
        m_UIAnimationsStarted = true;
        for (auto e : m_Registry.view<UIAnimationComponent>())
        {
            auto& anim = m_Registry.get<UIAnimationComponent>(e);
            if (!m_Registry.all_of<UITransformComponent>(e))
                continue;
            const bool active = anim.WasActive;

            // Feedback of its widget: bigger under the mouse / while selected by keys, smaller while pressed.
            bool interactable = false;
            if (auto* b = m_Registry.try_get<UIButtonComponent>(e)) interactable |= b->Interactable;
            if (auto* s = m_Registry.try_get<UISliderComponent>(e)) interactable |= s->Interactable;
            if (auto* t = m_Registry.try_get<UIToggleComponent>(e)) interactable |= t->Interactable;
            if (auto* f = m_Registry.try_get<UIInputFieldComponent>(e)) interactable |= f->Interactable;
            const bool usable = interactable && active;
            const bool hovered = usable && (m_HoveredUI == e || (IsUISelectionHighlighted() && m_SelectedUI == e));
            const bool pressed = usable && ((m_MouseDown && m_PressedUI == e && m_HoveredUI == e) || (e == m_UISubmitFlash && m_UISubmitFlashTime > 0.0f));
            const float target = UIAnimMath::FeedbackTarget(hovered, pressed, anim.HoverScale, anim.PressScale);
            if (target != anim.FeedbackTo)
            {
                anim.FeedbackFrom = anim.FeedbackScale;
                anim.FeedbackTo = target;
                anim.FeedbackTime = 0.0f;
            }
            if (anim.FeedbackTime < 1.0f)
            {
                anim.FeedbackTime = anim.FeedbackDuration > 0.0f ? std::min(1.0f, anim.FeedbackTime + dt / anim.FeedbackDuration) : 1.0f;
                // Pressing in: quick and straight; coming back out: a little spring.
                const Ease ease = anim.FeedbackTo < anim.FeedbackFrom ? Ease::OutQuad : Ease::OutBack;
                anim.FeedbackScale = glm::mix(anim.FeedbackFrom, anim.FeedbackTo, ApplyEase(ease, anim.FeedbackTime));
            }
        }
        UpdateUITweens(dt);
    }

}
