// Screen-space UI of a scene: layout of the UI rectangles, the mouse and keyboard handling of the
// widgets (buttons, sliders, toggles, input fields) and the UI pass that draws them over the scene.
#include "Zamin/Scene/Scene.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Core/Input.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/SpriteSheet.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/EventBus.h"
#include "Zamin/Scripting/ScriptableEntity.h"
#include "Zamin/UI/TextEdit.h"
#include "Zamin/UI/UIWidgets.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    // ------------------------------------------------------------------ layout
    static glm::vec2 AnchorOffset(UITransformComponent::AnchorPoint a, const glm::vec2& viewport)
    {
        int i = (int)a;
        float fx = (i % 3) * 0.5f;          // 0, 0.5, 1
        float fy = 1.0f - (i / 3) * 0.5f;   // top = 1
        return { fx * viewport.x, fy * viewport.y };
    }

    static glm::vec2 UICenter(const UITransformComponent& ui, const glm::vec2& viewport)
    {
        return AnchorOffset(ui.Anchor, viewport) + ui.Position; // bottom-left origin, pixels
    }

    static UIRect ComputeUIRect(const UITransformComponent& ui, const glm::vec2& viewport)
    {
        return UIRect::FromCenter(UICenter(ui, viewport), ui.Size);
    }

    UIRect Scene::GetUIRect(Entity entity)
    {
        if (!entity || entity.GetScene() != this)
            return {};
        const auto* ui = entity.TryGetComponent<UITransformComponent>();
        return ui ? ComputeUIRect(*ui, GetViewportSize()) : UIRect{};
    }

    std::vector<entt::entity> Scene::GetUIDrawOrder(const std::unordered_set<entt::entity>* skip)
    {
        std::vector<entt::entity> ui;
        for (auto e : m_Registry.view<UITransformComponent>())
            if (!skip || !skip->contains(e))
                ui.push_back(e);
        std::reverse(ui.begin(), ui.end()); // views iterate newest-first; keep creation order for equal Order
        std::stable_sort(ui.begin(), ui.end(), [&](entt::entity a, entt::entity b) {
            return m_Registry.get<UITransformComponent>(a).Order < m_Registry.get<UITransformComponent>(b).Order;
        });
        return ui;
    }

    // Whether the mouse stops at the entity (an interactable widget, or an image that catches it).
    static bool TakesMouse(entt::registry& registry, entt::entity e)
    {
        if (auto* b = registry.try_get<UIButtonComponent>(e); b && b->Interactable) return true;
        if (auto* s = registry.try_get<UISliderComponent>(e); s && s->Interactable) return true;
        if (auto* t = registry.try_get<UIToggleComponent>(e); t && t->Interactable) return true;
        if (auto* f = registry.try_get<UIInputFieldComponent>(e); f && f->Interactable) return true;
        if (auto* i = registry.try_get<UIImageComponent>(e); i && i->RaycastTarget) return true;
        return false;
    }

    Entity Scene::PickUI(const glm::vec2& point)
    {
        const glm::vec2 viewport = GetViewportSize();
        std::vector<entt::entity> order = GetUIDrawOrder();
        // The topmost first: the last one drawn.
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            entt::entity e = *it;
            if (!TakesMouse(m_Registry, e) || !IsActiveInHierarchy({ e, this }))
                continue; // inactive UI is not drawn and cannot be hovered: the mouse reaches what is below
            if (ComputeUIRect(m_Registry.get<UITransformComponent>(e), viewport).Contains(point))
                return { e, this };
        }
        return {};
    }

    // ------------------------------------------------------------------ input field text layout
    // The font size (px) of a widget's own text: the UI Transform's, else `share` of its height.
    static float WidgetFontSize(const UITransformComponent& t, const UIRect& rect, float share)
    {
        return std::max(1.0f, t.FontSize > 0.0f ? t.FontSize : rect.GetHeight() * share);
    }

    static const Font* LoadedFont(const Ref<Font>& font) { return font && font->IsLoaded() ? font.get() : nullptr; }

    // The x (px from the text's start) of every caret position of `display`; without a loaded font
    // (headless) every character counts as 0.55 of the size.
    static void LayoutCaretOffsets(const std::string& display, const Font* font, float px, std::vector<float>& offsets)
    {
        offsets.clear();
        offsets.push_back(0.0f);
        float x = 0.0f;
        size_t i = 0;
        while (i < display.size())
        {
            uint32_t cp = DecodeUTF8(display, i);
            if (cp == '\t') cp = ' ';
            float advance = 0.55f;
            if (font)
            {
                const Glyph* g = font->GetGlyph(cp);
                advance = g ? g->Advance : 0.0f;
            }
            x += advance * px;
            offsets.push_back(x);
        }
    }

    // The area the text of an input field is drawn in.
    static UIRect InputTextArea(const UIInputFieldComponent& field, const UIRect& rect)
    {
        return rect.Inset(field.Padding, field.Padding, 0.0f, 0.0f);
    }

    void Scene::ScrollInputField(entt::entity e)
    {
        auto& field = m_Registry.get<UIInputFieldComponent>(e);
        const auto& t = m_Registry.get<UITransformComponent>(e);
        const UIRect rect = ComputeUIRect(t, GetViewportSize());
        const UIRect area = InputTextArea(field, rect);
        std::vector<float> offsets;
        LayoutCaretOffsets(TextEdit::GetDisplayText(field.Text, field.ContentType), LoadedFont(field.FontAsset),
                           WidgetFontSize(t, rect, 0.5f), offsets);
        const size_t caret = std::min(TextEdit::CodepointIndexOf(field.Text, field.Caret), offsets.size() - 1);
        field.Scroll = TextEdit::ScrollToCaret(field.Scroll, offsets[caret], offsets.back(), area.GetWidth(), 2.0f);
    }

    // The caret position (bytes) under a view point.
    static size_t InputCaretAt(const UIInputFieldComponent& field, const UITransformComponent& t, const UIRect& rect, const glm::vec2& point)
    {
        const UIRect area = InputTextArea(field, rect);
        std::vector<float> offsets;
        LayoutCaretOffsets(TextEdit::GetDisplayText(field.Text, field.ContentType), LoadedFont(field.FontAsset), WidgetFontSize(t, rect, 0.5f),
                           offsets);
        const size_t index = TextEdit::CaretIndexAt(offsets, point.x - (area.Min.x - field.Scroll));
        return TextEdit::ByteOffsetOf(field.Text, index);
    }

    // ------------------------------------------------------------------ widget changes
    void Scene::DeliverUIChange(const UIChangeEvent& change)
    {
        if (!m_IsRunning || !m_Registry.valid(change.Entity))
            return;
        Entity entity{ change.Entity, this };
        // Like OnClick, the callbacks also reach disabled scripts.
        if (m_ScriptsRunning)
            if (auto* nsc = m_Registry.try_get<NativeScriptComponent>(change.Entity); nsc && nsc->Instance)
            {
                ScriptableEntity* script = nsc->Instance;
                switch (change.Kind)
                {
                    case UIChange::Slider:        script->OnValueChanged(change.Value); break;
                    case UIChange::Toggle:        script->OnToggleChanged(change.On); break;
                    case UIChange::TextChanged:   script->OnTextChanged(change.Text); break;
                    case UIChange::TextSubmitted: script->OnTextSubmit(change.Text); break;
                    case UIChange::TextEndEdit:   script->OnTextEndEdit(change.Text); break;
                }
            }
        if (!m_EventBus || !m_Registry.valid(change.Entity))
            return; // destroyed by its callback
        switch (change.Kind)
        {
            case UIChange::Slider:        m_EventBus->Emit("UISliderChanged", change.Value, entity); break;
            case UIChange::Toggle:        m_EventBus->Emit("UIToggleChanged", change.On, entity); break;
            case UIChange::TextChanged:   m_EventBus->Emit("UITextChanged", change.Text, entity); break;
            case UIChange::TextSubmitted: m_EventBus->Emit("UITextSubmitted", change.Text, entity); break;
            case UIChange::TextEndEdit:   m_EventBus->Emit("UITextEndEdit", change.Text, entity); break;
        }
    }

    void Scene::DeliverUIChanges(std::vector<UIChangeEvent>& changes)
    {
        for (const UIChangeEvent& change : changes)
            DeliverUIChange(change);
        changes.clear();
    }

    bool Scene::ApplyToggle(entt::entity e, bool isOn, std::vector<UIChangeEvent>* changes)
    {
        auto& toggle = m_Registry.get<UIToggleComponent>(e);
        if (toggle.IsOn == isOn)
            return false;
        if (isOn && !toggle.Group.empty())
        {
            // Radio buttons: the others of the group switch off first.
            std::vector<entt::entity> others;
            for (auto other : m_Registry.view<UIToggleComponent>())
                if (other != e)
                    others.push_back(other);
            SortInSceneOrder(others);
            for (auto other : others)
            {
                auto& o = m_Registry.get<UIToggleComponent>(other);
                if (o.IsOn && o.Group == toggle.Group)
                {
                    o.IsOn = false;
                    o.ValueChanged = true;
                    if (changes)
                        changes->push_back({ other, UIChange::Toggle, 0.0f, false, {} });
                }
            }
        }
        toggle.IsOn = isOn;
        toggle.ValueChanged = true;
        if (changes)
            changes->push_back({ e, UIChange::Toggle, isOn ? 1.0f : 0.0f, isOn, {} });
        return true;
    }

    bool Scene::SetSliderValue(Entity entity, float value, bool notify)
    {
        if (!entity || entity.GetScene() != this)
            return false;
        auto* slider = entity.TryGetComponent<UISliderComponent>();
        if (!slider)
            return false;
        value = UIMath::ClampValue(value, slider->MinValue, slider->MaxValue, slider->WholeNumbers);
        if (value == slider->Value)
            return false;
        slider->Value = value;
        if (notify)
        {
            slider->ValueChanged = true;
            DeliverUIChange({ entity, UIChange::Slider, value, false, {} });
        }
        return true;
    }

    bool Scene::SetToggle(Entity entity, bool isOn, bool notify)
    {
        if (!entity || entity.GetScene() != this || !entity.HasComponent<UIToggleComponent>())
            return false;
        std::vector<UIChangeEvent> changes;
        if (!ApplyToggle(entity, isOn, &changes))
            return false;
        if (notify)
            DeliverUIChanges(changes);
        return true;
    }

    bool Scene::SetInputText(Entity entity, const std::string& text, bool notify)
    {
        if (!entity || entity.GetScene() != this)
            return false;
        auto* field = entity.TryGetComponent<UIInputFieldComponent>();
        if (!field)
            return false;
        std::string filtered = TextEdit::Filter(text, field->ContentType, field->CharacterLimit);
        if (filtered == field->Text)
            return false;
        field->Text = std::move(filtered);
        TextEditState state{ field->Text, field->Caret, field->Anchor };
        TextEdit::Clamp(state);
        field->Caret = state.Caret;
        field->Anchor = state.Anchor;
        if (field->Focused)
            ScrollInputField(entity);
        if (notify)
        {
            field->TextChanged = true;
            DeliverUIChange({ entity, UIChange::TextChanged, 0.0f, false, field->Text });
        }
        return true;
    }

    void Scene::BeginInputEdit(entt::entity e)
    {
        auto& field = m_Registry.get<UIInputFieldComponent>(e);
        m_FocusedInput = e;
        field.Focused = true;
        field.TextBeforeEdit = field.Text;
        field.Caret = field.Text.size();
        field.Anchor = field.SelectAllOnFocus ? 0 : field.Caret;
        field.BlinkTime = 0.0f;
        field.Selecting = false;
        ScrollInputField(e);
    }

    void Scene::EndInputEdit(std::vector<UIChangeEvent>* changes)
    {
        entt::entity e = m_FocusedInput;
        m_FocusedInput = entt::null;
        if (e == entt::null || !m_Registry.valid(e))
            return;
        auto* field = m_Registry.try_get<UIInputFieldComponent>(e);
        if (!field)
            return;
        field->Focused = false;
        field->Selecting = false;
        field->Anchor = field->Caret; // the selection goes
        field->Scroll = 0.0f;         // the start of the text shows again
        if (changes)
            changes->push_back({ e, UIChange::TextEndEdit, 0.0f, false, field->Text });
    }

    void Scene::FocusInputField(Entity entity)
    {
        std::vector<UIChangeEvent> changes;
        const bool valid = entity && entity.GetScene() == this && entity.HasComponent<UIInputFieldComponent>();
        if (valid && m_FocusedInput == (entt::entity)entity)
            return;
        if (m_FocusedInput != entt::null)
            EndInputEdit(&changes);
        if (valid)
            BeginInputEdit(entity);
        DeliverUIChanges(changes);
    }

    Entity Scene::GetFocusedInputField()
    {
        return m_FocusedInput != entt::null && m_Registry.valid(m_FocusedInput) ? Entity{ m_FocusedInput, this } : Entity{};
    }

    // ------------------------------------------------------------------ keyboard (the input field being edited)
    void Scene::ProcessTextInput(entt::entity e, std::vector<UIChangeEvent>& changes)
    {
        auto& field = m_Registry.get<UIInputFieldComponent>(e);
        const std::vector<TextInputEvent>& events = Input::GetTextInput();
        if (events.empty())
            return;
        TextEditState state{ field.Text, field.Caret, field.Anchor };
        TextEdit::Clamp(state);
        const bool editable = !field.ReadOnly;
        const bool secret = field.ContentType == UIContentType::Password; // never copied out
        bool changed = false, submit = false, cancel = false, any = false;
        for (const TextInputEvent& ev : events)
        {
            if (ev.IsCharacter())
            {
                // Ctrl + key are shortcuts (AltGr = Ctrl + Alt still types on some layouts).
                if (ev.Control && !ev.Alt)
                    continue;
                if (editable)
                    changed |= TextEdit::Insert(state, TextEdit::ToUTF8(ev.Codepoint), field.ContentType, field.CharacterLimit);
                any = true;
                continue;
            }
            const bool word = ev.Control || ev.Alt;
            switch (ev.KeyCode)
            {
                case Key::Left:  TextEdit::MoveLeft(state, ev.Shift, word); any = true; break;
                case Key::Right: TextEdit::MoveRight(state, ev.Shift, word); any = true; break;
                case Key::Up:
                case Key::Home:  TextEdit::MoveHome(state, ev.Shift); any = true; break;
                case Key::Down:
                case Key::End:   TextEdit::MoveEnd(state, ev.Shift); any = true; break;
                case Key::Backspace:
                    if (editable) changed |= TextEdit::Backspace(state, ev.Control);
                    any = true;
                    break;
                case Key::Delete:
                    if (editable) changed |= TextEdit::Delete(state, ev.Control);
                    any = true;
                    break;
                case Key::Enter:
                case Key::KPEnter: submit = true; break;
                case Key::Escape:  cancel = true; break;
                case Key::A:
                    if (ev.Control) { TextEdit::SelectAll(state); any = true; }
                    break;
                case Key::C:
                    if (ev.Control && state.HasSelection() && !secret)
                        Input::SetClipboardText(TextEdit::GetSelectedText(state));
                    break;
                case Key::X:
                    if (ev.Control && state.HasSelection() && !secret && editable)
                    {
                        Input::SetClipboardText(TextEdit::GetSelectedText(state));
                        changed |= TextEdit::DeleteSelection(state);
                        any = true;
                    }
                    break;
                case Key::V:
                    if (ev.Control && editable)
                    {
                        changed |= TextEdit::Insert(state, Input::GetClipboardText(), field.ContentType, field.CharacterLimit);
                        any = true;
                    }
                    break;
                default: break;
            }
            if (submit || cancel)
                break; // editing ends: what came after it in the frame is not for this field
        }
        field.Text = std::move(state.Text);
        field.Caret = state.Caret;
        field.Anchor = state.Anchor;
        if (any)
            field.BlinkTime = 0.0f; // the caret shows while typing
        if (changed)
        {
            field.TextChanged = true;
            changes.push_back({ e, UIChange::TextChanged, 0.0f, false, field.Text });
        }
        if (cancel && field.Text != field.TextBeforeEdit)
        {
            // Escape: the text it had when editing began.
            field.Text = field.TextBeforeEdit;
            field.TextChanged = true;
            changes.push_back({ e, UIChange::TextChanged, 0.0f, false, field.Text });
        }
        if (submit)
        {
            field.Submitted = true;
            changes.push_back({ e, UIChange::TextSubmitted, 0.0f, false, field.Text });
        }
        if (submit || cancel)
            EndInputEdit(&changes);
        else
            ScrollInputField(e);
    }

    void Scene::ResetUIRuntime()
    {
        m_PressedUI = m_FocusedInput = m_LastUIClickEntity = entt::null;
        m_LastUIClickTime = -1.0f;
        m_MouseWasDown = m_MouseDown;
        for (auto e : m_Registry.view<UIButtonComponent>())
        {
            auto& b = m_Registry.get<UIButtonComponent>(e);
            b.Hovered = b.Pressed = b.Clicked = false;
        }
        for (auto e : m_Registry.view<UISliderComponent>())
        {
            auto& s = m_Registry.get<UISliderComponent>(e);
            s.Hovered = s.Dragging = s.ValueChanged = false;
        }
        for (auto e : m_Registry.view<UIToggleComponent>())
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            t.Hovered = t.Pressed = t.ValueChanged = false;
            t.Transition = t.IsOn ? 1.0f : 0.0f;
        }
        for (auto e : m_Registry.view<UIProgressBarComponent>())
        {
            auto& p = m_Registry.get<UIProgressBarComponent>(e);
            p.DisplayValue = std::clamp(p.Value, 0.0f, 1.0f);
        }
        for (auto e : m_Registry.view<UIInputFieldComponent>())
            m_Registry.get<UIInputFieldComponent>(e).ResetRuntime();
    }

    // ------------------------------------------------------------------ update (every frame of the game, also while paused)
    void Scene::UpdateUI(Timestep ts)
    {
        const float dt = std::max(0.0f, (float)ts);
        m_UITime += dt;
        const glm::vec2 viewport = GetViewportSize();
        const glm::vec2 mouse = { m_MousePosition.x, viewport.y - m_MousePosition.y }; // to bottom-left origin
        const bool pressedNow = m_MouseDown && !m_MouseWasDown;
        const bool released = !m_MouseDown && m_MouseWasDown;
        m_MouseWasDown = m_MouseDown;

        // A widget that went inactive (or lost its component) while held down is not clicked when it comes back.
        if (m_PressedUI != entt::null && (!m_Registry.valid(m_PressedUI) || !IsActiveInHierarchy({ m_PressedUI, this })))
            m_PressedUI = entt::null;
        std::vector<UIChangeEvent> changes;
        if (m_FocusedInput != entt::null)
        {
            const bool alive = m_Registry.valid(m_FocusedInput) && m_Registry.all_of<UIInputFieldComponent, UITransformComponent>(m_FocusedInput);
            if (!alive)
                m_FocusedInput = entt::null;
            else if (!IsActiveInHierarchy({ m_FocusedInput, this }) || !m_Registry.get<UIInputFieldComponent>(m_FocusedInput).Interactable)
                EndInputEdit(&changes);
        }

        // One-frame flags and the hover state start over.
        for (auto e : m_Registry.view<UIButtonComponent>())
        {
            auto& b = m_Registry.get<UIButtonComponent>(e);
            b.Clicked = b.Hovered = false;
        }
        for (auto e : m_Registry.view<UISliderComponent>())
        {
            auto& s = m_Registry.get<UISliderComponent>(e);
            s.ValueChanged = s.Hovered = false;
        }
        for (auto e : m_Registry.view<UIToggleComponent>())
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            t.ValueChanged = t.Hovered = false;
        }
        for (auto e : m_Registry.view<UIInputFieldComponent>())
        {
            auto& f = m_Registry.get<UIInputFieldComponent>(e);
            f.TextChanged = f.Submitted = f.Hovered = false;
        }

        // ---- the mouse
        const entt::entity hovered = (entt::entity)PickUI(mouse);
        if (hovered != entt::null)
        {
            if (auto* b = m_Registry.try_get<UIButtonComponent>(hovered); b && b->Interactable) b->Hovered = true;
            if (auto* s = m_Registry.try_get<UISliderComponent>(hovered); s && s->Interactable) s->Hovered = true;
            if (auto* t = m_Registry.try_get<UIToggleComponent>(hovered); t && t->Interactable) t->Hovered = true;
            if (auto* f = m_Registry.try_get<UIInputFieldComponent>(hovered); f && f->Interactable) f->Hovered = true;
        }

        auto sliderFollowMouse = [&](entt::entity e) {
            auto& s = m_Registry.get<UISliderComponent>(e);
            const UIRect rect = ComputeUIRect(m_Registry.get<UITransformComponent>(e), viewport);
            const float t = UIMath::SliderNormalizedAt(rect, s.Direction, s.HandleSize, mouse);
            const float value = UIMath::Denormalize(t, s.MinValue, s.MaxValue, s.WholeNumbers);
            if (value != s.Value)
            {
                s.Value = value;
                s.ValueChanged = true;
                changes.push_back({ e, UIChange::Slider, value, false, {} });
            }
        };

        if (pressedNow)
        {
            m_PressedUI = hovered;
            // A press anywhere but on the field being edited ends its editing.
            if (m_FocusedInput != entt::null && hovered != m_FocusedInput)
                EndInputEdit(&changes);
            if (hovered != entt::null)
            {
                if (auto* s = m_Registry.try_get<UISliderComponent>(hovered); s && s->Interactable)
                    sliderFollowMouse(hovered); // the handle jumps to the click
                if (auto* f = m_Registry.try_get<UIInputFieldComponent>(hovered); f && f->Interactable)
                {
                    const bool doubleClick = m_LastUIClickEntity == hovered && m_UITime - m_LastUIClickTime < 0.35f;
                    const bool focusing = m_FocusedInput != hovered;
                    if (focusing)
                        BeginInputEdit(hovered);
                    if (doubleClick)
                    {
                        // Double click: everything.
                        f->Anchor = 0;
                        f->Caret = f->Text.size();
                        f->Selecting = false;
                    }
                    else if (!(focusing && f->SelectAllOnFocus))
                    {
                        const auto& t = m_Registry.get<UITransformComponent>(hovered);
                        f->Caret = f->Anchor = InputCaretAt(*f, t, ComputeUIRect(t, viewport), mouse);
                        f->Selecting = true;
                    }
                    f->BlinkTime = 0.0f;
                    ScrollInputField(hovered);
                }
                m_LastUIClickTime = m_UITime;
            }
            m_LastUIClickEntity = hovered;
        }

        // Held down: a slider follows the mouse anywhere; an input field selects up to it (the
        // position the button came up at still counts).
        if ((m_MouseDown || released) && m_PressedUI != entt::null)
        {
            if (auto* s = m_Registry.try_get<UISliderComponent>(m_PressedUI); s && s->Interactable && !pressedNow)
                sliderFollowMouse(m_PressedUI);
            if (auto* f = m_Registry.try_get<UIInputFieldComponent>(m_PressedUI); f && f->Selecting && m_FocusedInput == m_PressedUI && !pressedNow)
            {
                const auto& t = m_Registry.get<UITransformComponent>(m_PressedUI);
                f->Caret = InputCaretAt(*f, t, ComputeUIRect(t, viewport), mouse);
                ScrollInputField(m_PressedUI);
            }
        }

        std::vector<entt::entity> clicked;
        for (auto e : m_Registry.view<UIButtonComponent>())
        {
            auto& b = m_Registry.get<UIButtonComponent>(e);
            b.Pressed = m_MouseDown && m_PressedUI == e && b.Hovered;
            if (released && m_PressedUI == e && b.Hovered)
            {
                b.Clicked = true;
                clicked.push_back(e);
            }
        }
        for (auto e : m_Registry.view<UISliderComponent>())
        {
            auto& s = m_Registry.get<UISliderComponent>(e);
            s.Dragging = m_MouseDown && m_PressedUI == e && s.Interactable;
        }
        std::vector<entt::entity> switched;
        for (auto e : m_Registry.view<UIToggleComponent>())
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            t.Pressed = m_MouseDown && m_PressedUI == e && t.Hovered;
            if (released && m_PressedUI == e && t.Hovered)
                switched.push_back(e);
        }
        for (auto e : switched)
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            // A radio button that is on stays on (unless the group allows none).
            if (t.IsOn && !t.Group.empty() && !t.AllowSwitchOff)
                continue;
            ApplyToggle(e, !t.IsOn, &changes);
        }
        if (released)
        {
            if (m_PressedUI != entt::null && m_Registry.valid(m_PressedUI))
                if (auto* f = m_Registry.try_get<UIInputFieldComponent>(m_PressedUI))
                    f->Selecting = false;
            m_PressedUI = entt::null;
        }

        // ---- the keyboard
        if (m_FocusedInput != entt::null && m_UIKeyboardEnabled)
            ProcessTextInput(m_FocusedInput, changes);

        // ---- what animates by itself (the UI's own time: it goes on while the game is paused)
        if (m_FocusedInput != entt::null)
            m_Registry.get<UIInputFieldComponent>(m_FocusedInput).BlinkTime += dt;
        for (auto e : m_Registry.view<UIToggleComponent>())
        {
            auto& t = m_Registry.get<UIToggleComponent>(e);
            const float target = t.IsOn ? 1.0f : 0.0f, step = dt / 0.12f;
            t.Transition = t.Transition < target ? std::min(target, t.Transition + step) : std::max(target, t.Transition - step);
        }
        for (auto e : m_Registry.view<UIProgressBarComponent>())
        {
            auto& p = m_Registry.get<UIProgressBarComponent>(e);
            const float target = std::clamp(p.Value, 0.0f, 1.0f);
            if (p.SmoothSpeed <= 0.0f)
                p.DisplayValue = target;
            else
            {
                const float step = p.SmoothSpeed * dt;
                p.DisplayValue = p.DisplayValue < target ? std::min(target, p.DisplayValue + step) : std::max(target, p.DisplayValue - step);
            }
        }

        // ---- the scripts hear of it: clicks first, then the changes in the order they happened
        if (m_ScriptsRunning)
            for (auto e : clicked)
                if (m_Registry.valid(e) && IsActiveInHierarchy({ e, this })) // still active after earlier OnClicks
                    if (auto* nsc = m_Registry.try_get<NativeScriptComponent>(e); nsc && nsc->Instance)
                        nsc->Instance->OnClick();
        DeliverUIChanges(changes);
    }

    // ------------------------------------------------------------------ drawing helpers
    static glm::mat4 RectTransform(const UIRect& r)
    {
        const glm::vec2 c = r.GetCenter(), size = r.GetSize();
        return glm::translate(glm::mat4(1.0f), glm::vec3(c, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
    }

    static void DrawRectFilled(const UIRect& r, const glm::vec4& color, int id)
    {
        if (r.IsEmpty() || color.a <= 0.0f)
            return;
        Renderer2D::DrawQuad(RectTransform(r), color, id);
    }

    static void DrawRectOutline(const UIRect& r, float width, const glm::vec4& color, int id)
    {
        if (r.IsEmpty() || color.a <= 0.0f || width <= 0.0f)
            return;
        width = std::min(width, std::min(r.GetWidth(), r.GetHeight()) * 0.5f);
        DrawRectFilled({ r.Min, { r.Max.x, r.Min.y + width } }, color, id);                                   // bottom
        DrawRectFilled({ { r.Min.x, r.Max.y - width }, r.Max }, color, id);                                   // top
        DrawRectFilled({ { r.Min.x, r.Min.y + width }, { r.Min.x + width, r.Max.y - width } }, color, id);    // left
        DrawRectFilled({ { r.Max.x - width, r.Min.y + width }, { r.Max.x, r.Max.y - width } }, color, id);    // right
    }

    static void DrawDisc(const glm::vec2& center, const glm::vec2& size, const glm::vec4& color, int id)
    {
        if (size.x <= 0.0f || size.y <= 0.0f || color.a <= 0.0f)
            return;
        const float fade = std::clamp(2.0f / std::max(1.0f, std::min(size.x, size.y)), 0.002f, 0.5f); // about a pixel of edge
        Renderer2D::DrawCircle(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f)),
                               color, 1.0f, fade, id);
    }

    // A bar with round ends (a pill) when `rounded`, else a rectangle.
    static void DrawBar(const UIRect& r, const glm::vec4& color, bool rounded, int id)
    {
        if (r.IsEmpty() || color.a <= 0.0f)
            return;
        if (!rounded)
        {
            DrawRectFilled(r, color, id);
            return;
        }
        const glm::vec2 size = r.GetSize(), c = r.GetCenter();
        if (size.x >= size.y)
        {
            const float radius = size.y * 0.5f;
            if (size.x <= size.y)
            {
                DrawDisc(c, size, color, id);
                return;
            }
            DrawRectFilled({ { r.Min.x + radius, r.Min.y }, { r.Max.x - radius, r.Max.y } }, color, id);
            DrawDisc({ r.Min.x + radius, c.y }, glm::vec2(size.y), color, id);
            DrawDisc({ r.Max.x - radius, c.y }, glm::vec2(size.y), color, id);
        }
        else
        {
            const float radius = size.x * 0.5f;
            DrawRectFilled({ { r.Min.x, r.Min.y + radius }, { r.Max.x, r.Max.y - radius } }, color, id);
            DrawDisc({ c.x, r.Min.y + radius }, glm::vec2(size.x), color, id);
            DrawDisc({ c.x, r.Max.y - radius }, glm::vec2(size.x), color, id);
        }
    }

    // A thick line from a to b (px), drawn as a rotated quad.
    static void DrawStroke(const glm::vec2& a, const glm::vec2& b, float thickness, const glm::vec4& color, int id)
    {
        const glm::vec2 d = b - a;
        const float length = glm::length(d);
        if (length <= 0.0f || thickness <= 0.0f)
            return;
        const glm::vec2 mid = (a + b) * 0.5f;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(mid, 0.0f)) * glm::rotate(glm::mat4(1.0f), std::atan2(d.y, d.x), glm::vec3(0, 0, 1)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(length + thickness * 0.5f, thickness, 1.0f));
        Renderer2D::DrawQuad(m, color, id);
    }

    static glm::vec4 Lighten(const glm::vec4& c, float amount) { return glm::vec4(glm::mix(glm::vec3(c), glm::vec3(1.0f), amount), c.a); }
    static glm::vec4 Darken(const glm::vec4& c, float amount) { return glm::vec4(glm::vec3(c) * (1.0f - amount), c.a); }
    static glm::vec4 Disabled(const glm::vec4& c) { return glm::vec4(glm::vec3(c) * 0.6f, c.a * 0.8f); }

    // One line of text with its baseline placed so the line is centred vertically on `centerY`,
    // starting at `x` (px).
    static void DrawTextLine(const std::string& text, const Ref<Font>& font, float px, float x, float centerY, const glm::vec4& color, int id)
    {
        if (!font || text.empty() || color.a <= 0.0f)
            return;
        const float baseline = centerY + (font->GetLineHeight() * 0.5f - font->GetAscent()) * px;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(x, baseline, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(px, px, 1.0f));
        Renderer2D::DrawString(text, font, m, Renderer2D::TextParams{ color, 0.0f, 0.0f }, id);
    }

    // The part of the texture an image shows: its sprite sheet frame, else the whole texture.
    static SpriteRegion ImageRegion(const UIImageComponent& image, const AssetRegistry* registry)
    {
        if (!image.Texture)
            return SpriteRegion();
        const uint32_t w = image.Texture->GetWidth(), h = image.Texture->GetHeight();
        if (!image.SpriteName.empty() && registry)
            if (const SpriteSheet* sheet = SpriteSheet::Get(registry, image.TexturePath))
                if (const SpriteFrame* frame = sheet->Find(image.SpriteName))
                    return SpriteRegion::FromFrame(*frame, w, h);
        return SpriteRegion::WholeTexture(w, h);
    }

    static void DrawUIImage(UIImageComponent& image, const UIRect& rect, const glm::vec4& color, int id, const AssetRegistry* registry)
    {
        if (!image.Texture && !image.TexturePath.empty())
            image.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(image.TexturePath), image.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        if (rect.IsEmpty() || color.a <= 0.0f)
            return;
        const SpriteRegion region = ImageRegion(image, registry);
        const float aspect = region.Pixels.y > 0.0f ? region.Pixels.x / region.Pixels.y : 1.0f;
        switch (image.Type)
        {
            case UIImageType::Simple:
            {
                const UIRect r = image.PreserveAspect && image.Texture ? UIMath::FitAspect(rect, aspect) : rect;
                if (image.Texture)
                    Renderer2D::DrawQuad(RectTransform(r), image.Texture, region.UVMin, region.UVMax, color, id);
                else
                    Renderer2D::DrawQuad(RectTransform(r), color, id);
                return;
            }
            case UIImageType::Sliced:
            case UIImageType::Tiled:
            {
                SpriteGeometry g;
                g.Mode = image.Type == UIImageType::Sliced ? SpriteDrawMode::Sliced : SpriteDrawMode::Tiled;
                g.Size = rect.GetSize();
                g.Pivot = glm::vec2(0.5f);
                g.UVMin = region.UVMin;
                g.UVMax = region.UVMax;
                g.RegionPixels = region.Pixels;
                g.Border = image.HasSliceBorder() ? image.SliceBorder : region.Border;
                g.PixelsPerUnit = 1.0f / std::max(0.01f, image.BorderScale); // 1 px of the picture = BorderScale px on screen
                if (!image.Texture)
                {
                    g.Mode = SpriteDrawMode::Sliced; // no picture: one plain rectangle
                    g.Border = glm::vec4(0.0f);
                }
                static thread_local std::vector<SpriteQuad> quads;
                quads.clear();
                g.Build(quads);
                const glm::vec2 center = rect.GetCenter();
                for (const SpriteQuad& q : quads)
                {
                    const UIRect piece{ center + q.Min, center + q.Max };
                    if (image.Texture)
                        Renderer2D::DrawQuad(RectTransform(piece), image.Texture, q.UVMin, q.UVMax, color, id);
                    else
                        Renderer2D::DrawQuad(RectTransform(piece), color, id);
                }
                return;
            }
            case UIImageType::Filled:
            {
                const UIRect r = image.PreserveAspect && image.Texture ? UIMath::FitAspect(rect, aspect) : rect;
                static thread_local std::vector<UIQuad> quads;
                quads.clear();
                UIMath::BuildFill(r, region.UVMin, region.UVMax, image.FillMethod, image.FillOrigin, image.FillAmount, image.FillClockwise, quads);
                for (const UIQuad& q : quads)
                    Renderer2D::DrawQuadCorners(q.Position, q.UV, image.Texture, color, id);
                return;
            }
        }
    }

    static void DrawUISlider(const UISliderComponent& s, const UIRect& rect, const glm::vec4& tint, int id)
    {
        const bool horizontal = UIMath::IsHorizontal(s.Direction);
        const float thicknessOfRect = horizontal ? rect.GetHeight() : rect.GetWidth();
        const float thickness = s.TrackThickness > 0.0f ? std::min(s.TrackThickness, thicknessOfRect) : thicknessOfRect;
        const glm::vec2 c = rect.GetCenter();
        const UIRect track = horizontal ? UIRect{ { rect.Min.x, c.y - thickness * 0.5f }, { rect.Max.x, c.y + thickness * 0.5f } }
                                        : UIRect{ { c.x - thickness * 0.5f, rect.Min.y }, { c.x + thickness * 0.5f, rect.Max.y } };
        auto look = [&](const glm::vec4& color) { return (s.Interactable ? color : Disabled(color)) * tint; };
        DrawBar(track, look(s.TrackColor), s.Rounded, id);

        const float handle = UIMath::GetSliderHandleSize(rect, s.Direction, s.HandleSize);
        const glm::vec2 h = UIMath::GetSliderHandleCenter(rect, s.Direction, s.HandleSize, s.GetNormalizedValue());
        UIRect fill = track;
        switch (s.Direction)
        {
            case UIDirection::LeftToRight: fill.Max.x = h.x; break;
            case UIDirection::RightToLeft: fill.Min.x = h.x; break;
            case UIDirection::BottomToTop: fill.Max.y = h.y; break;
            case UIDirection::TopToBottom: fill.Min.y = h.y; break;
        }
        DrawBar(fill, look(s.FillColor), s.Rounded, id);

        glm::vec4 handleColor = s.HandleColor;
        if (s.Interactable)
            handleColor = s.Dragging ? Darken(handleColor, 0.15f) : s.Hovered ? Lighten(handleColor, 0.25f) : handleColor;
        if (s.Rounded)
        {
            // A soft shadow ring under the knob, then the knob.
            DrawDisc(h, glm::vec2(handle), glm::vec4(0.0f, 0.0f, 0.0f, 0.25f * handleColor.a) * glm::vec4(1, 1, 1, tint.a), id);
            DrawDisc(h, glm::vec2(handle * 0.86f), look(handleColor), id);
        }
        else
        {
            const glm::vec2 size = horizontal ? glm::vec2(handle * 0.5f, handle) : glm::vec2(handle, handle * 0.5f);
            DrawRectFilled(UIRect::FromCenter(h, size), look(handleColor), id);
        }
    }

    static void DrawUIToggle(const UIToggleComponent& t, const UIRect& rect, bool running, const glm::vec4& tint, int id)
    {
        const UIRect box = UIMath::GetToggleBox(rect, t.BoxSize, t.Style);
        if (box.IsEmpty())
            return;
        const float k = running ? std::clamp(t.Transition, 0.0f, 1.0f) : (t.IsOn ? 1.0f : 0.0f);
        glm::vec4 boxColor = glm::mix(t.BoxColor, t.OnColor, k);
        if (!t.Interactable)
            boxColor = Disabled(boxColor);
        else if (t.Pressed)
            boxColor = Darken(boxColor, 0.15f);
        else if (t.Hovered)
            boxColor = Lighten(boxColor, 0.15f);
        boxColor *= tint;
        const glm::vec4 markColor = (t.Interactable ? t.MarkColor : Disabled(t.MarkColor)) * tint;
        const float size = box.GetHeight();

        if (t.Style == UIToggleStyle::Switch)
        {
            DrawBar(box, boxColor, true, id);
            const float knob = size * 0.76f;
            const float x = glm::mix(box.Min.x + size * 0.5f, box.Max.x - size * 0.5f, k);
            DrawDisc({ x, box.GetCenter().y }, glm::vec2(knob), markColor, id);
            return;
        }

        DrawRectFilled(box, boxColor, id);
        DrawRectOutline(box, std::max(1.0f, size * 0.06f), glm::vec4(glm::vec3(Lighten(boxColor, 0.2f)), boxColor.a), id);
        if (k <= 0.01f)
            return;
        glm::vec4 mark = markColor;
        mark.a *= k;
        auto at = [&](float u, float v) { return box.Min + glm::vec2(u, v) * box.GetSize(); };
        switch (t.Mark)
        {
            case UIToggleMark::Check:
            {
                const float stroke = std::max(1.5f, size * 0.13f);
                DrawStroke(at(0.22f, 0.53f), at(0.42f, 0.32f), stroke, mark, id);
                DrawStroke(at(0.42f, 0.32f), at(0.79f, 0.71f), stroke, mark, id);
                break;
            }
            case UIToggleMark::Square:
                DrawRectFilled(box.Inset(size * 0.25f), mark, id);
                break;
            case UIToggleMark::Dot:
                DrawDisc(box.GetCenter(), glm::vec2(size * 0.46f), mark, id);
                break;
        }
    }

    static void DrawUIProgressBar(const UIProgressBarComponent& p, const UITransformComponent& t, const UIRect& rect, bool running,
                                  const glm::vec4& tint, int id)
    {
        const float value = std::clamp(running ? p.DisplayValue : p.Value, 0.0f, 1.0f);
        DrawRectFilled(rect, p.BackgroundColor * tint, id);
        const UIRect fill = UIMath::FillRect(rect.Inset(p.Padding), p.Direction, value);
        DrawRectFilled(fill, p.GetFillColor(value) * tint, id);
        if (p.ShowPercentage)
            if (Ref<Font> font = Font::GetDefault())
            {
                const std::string label = std::format("{}%", (int)std::round(value * 100.0f));
                const float px = WidgetFontSize(t, rect, 0.6f);
                const float width = Renderer2D::MeasureString(label, font).x * px;
                DrawTextLine(label, font, px, rect.GetCenter().x - width * 0.5f, rect.GetCenter().y, p.TextColor * tint, id);
            }
    }

    static void DrawUIInputField(UIInputFieldComponent& f, const UITransformComponent& t, const UIRect& rect, const glm::vec2& viewport,
                                 const glm::vec4& tint, int id)
    {
        if (!f.FontAsset)
            f.FontAsset = f.FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(f.FontPath));
        glm::vec4 background = f.BackgroundColor;
        if (!f.Interactable)
            background = Disabled(background);
        else if (f.Hovered && !f.Focused)
            background = Lighten(background, 0.05f);
        DrawRectFilled(rect, background * tint, id);
        if (f.Focused)
            DrawRectOutline(rect, 2.0f, f.FocusColor * tint, id);
        else
            DrawRectOutline(rect, 1.0f, f.OutlineColor * tint, id);

        const Ref<Font>& font = f.FontAsset;
        if (!font || !font->IsLoaded())
            return;
        const UIRect area = InputTextArea(f, rect);
        if (area.IsEmpty())
            return;
        const float px = WidgetFontSize(t, rect, 0.5f);
        const bool placeholder = f.Text.empty();
        const std::string display = placeholder ? f.Placeholder : TextEdit::GetDisplayText(f.Text, f.ContentType);
        const float scroll = f.Focused ? f.Scroll : 0.0f;
        const float x0 = area.Min.x - scroll;
        const float cy = rect.GetCenter().y;
        const float baseline = cy + (font->GetLineHeight() * 0.5f - font->GetAscent()) * px;
        const float top = baseline + font->GetAscent() * px, bottom = baseline + font->GetDescent() * px;

        // Only inside the field (a long text scrolls under its edges).
        const glm::vec2 v = glm::max(viewport, glm::vec2(1.0f));
        const UIRect clip = area.Inset(-1.0f, -1.0f, 0.0f, 0.0f).Intersect(rect);
        Renderer2D::SetClipRect(true, clip.Min / v, clip.Max / v);

        std::vector<float> offsets;
        if (!placeholder)
            LayoutCaretOffsets(display, font.get(), px, offsets);
        if (f.Focused && !placeholder && f.Caret != f.Anchor)
        {
            const size_t a = std::min(TextEdit::CodepointIndexOf(f.Text, std::min(f.Caret, f.Anchor)), offsets.size() - 1);
            const size_t b = std::min(TextEdit::CodepointIndexOf(f.Text, std::max(f.Caret, f.Anchor)), offsets.size() - 1);
            DrawRectFilled({ { x0 + offsets[a], bottom }, { x0 + offsets[b], top } }, f.SelectionColor * tint, id);
        }
        DrawTextLine(display, font, px, x0, cy, (placeholder ? f.PlaceholderColor : f.TextColor) * tint, id);
        if (f.Focused && !f.ReadOnly)
        {
            const bool visible = f.CaretBlinkRate <= 0.0f || std::fmod(f.BlinkTime, f.CaretBlinkRate * 2.0f) < f.CaretBlinkRate;
            if (visible)
            {
                const size_t c = placeholder ? 0 : std::min(TextEdit::CodepointIndexOf(f.Text, f.Caret), offsets.size() - 1);
                const float x = x0 + (placeholder ? 0.0f : offsets[c]);
                const float width = std::max(1.5f, px * 0.07f);
                DrawRectFilled({ { x - width * 0.5f, bottom }, { x + width * 0.5f, top } }, f.CaretColor * tint, id);
            }
        }
        Renderer2D::SetClipRect(false);
    }

    // ------------------------------------------------------------------ the UI pass
    void Scene::RenderUI(const std::unordered_set<entt::entity>* skip)
    {
        // ---------------- screen-space UI (pixels, bottom-left origin)
        auto uiView = m_Registry.view<UITransformComponent>();
        if (uiView.empty() || m_ViewportWidth == 0 || m_ViewportHeight == 0)
            return;
        glm::vec2 viewport = GetViewportSize();
        std::vector<entt::entity> ui = GetUIDrawOrder(skip);
        const AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;

        RenderCommand::SetDepthTest(false);
        Renderer2D::SetLit(false);
        Renderer2D::BeginScene(glm::ortho(0.0f, viewport.x, 0.0f, viewport.y, -1.0f, 1.0f));
        for (auto e : ui)
        {
            const auto& t = m_Registry.get<UITransformComponent>(e);
            glm::vec2 center = UICenter(t, viewport);
            const UIRect uiRect = ComputeUIRect(t, viewport);
            int id = (int)(uint32_t)e;
            glm::mat4 rect = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(t.Size, 1.0f));

            glm::vec4 tint(1.0f);
            if (auto* b = m_Registry.try_get<UIButtonComponent>(e))
                tint = !b->Interactable ? glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) : b->Pressed ? b->PressedColor : b->Hovered ? b->HoverColor : b->NormalColor;
            // A tint animation colours all of it (the button's own colours included).
            const glm::vec4 animated = m_Registry.all_of<TintAnimationComponent>(e) ? m_Registry.get<TintAnimationComponent>(e).GetColor() : glm::vec4(1.0f);
            tint *= animated;

            auto* image = m_Registry.try_get<UIImageComponent>(e);
            if (auto* s = m_Registry.try_get<SpriteRendererComponent>(e))
            {
                glm::vec4 c = s->Color * tint;
                const AssetRegistry* sheets = !s->SpriteName.empty() ? registry : nullptr;
                if (s->DrawMode == SpriteDrawMode::Simple)
                    DrawSprite(rect, *s, nullptr, c, id, sheets);
                else // 9-slice / tiled panels: borders in screen px
                    DrawSprite(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)), *s, nullptr, c, id, sheets, &t.Size);
            }
            else if (m_Registry.all_of<UIButtonComponent>(e) && !image)
                Renderer2D::DrawQuad(rect, tint, id);
            if (image)
                DrawUIImage(*image, uiRect, image->Color * tint, id, registry);

            if (auto* c = m_Registry.try_get<CircleRendererComponent>(e))
                Renderer2D::DrawCircle(rect, c->Color * animated, c->Thickness, c->Fade, id);

            // Widgets draw over the entity's own picture.
            if (auto* p = m_Registry.try_get<UIProgressBarComponent>(e))
                DrawUIProgressBar(*p, t, uiRect, m_IsRunning, animated, id);
            if (auto* s = m_Registry.try_get<UISliderComponent>(e))
                DrawUISlider(*s, uiRect, animated, id);
            auto* toggle = m_Registry.try_get<UIToggleComponent>(e);
            if (toggle)
                DrawUIToggle(*toggle, uiRect, m_IsRunning, animated, id);
            if (auto* f = m_Registry.try_get<UIInputFieldComponent>(e))
                DrawUIInputField(*f, t, uiRect, viewport, animated, id);

            if (auto* text = m_Registry.try_get<TextComponent>(e))
            {
                if (!text->FontAsset) text->FontAsset = Font::GetDefault();
                Renderer2D::TextParams params{ text->Color * animated, text->Kerning, text->LineSpacing };
                // A toggle's label goes right of its box.
                UIRect textRect = toggle ? UIMath::GetToggleLabel(uiRect, toggle->BoxSize, toggle->Style, toggle->LabelSpacing) : uiRect;
                const glm::vec2 textCenter = toggle ? textRect.GetCenter() : center;
                const float textWidth = toggle ? textRect.GetWidth() : t.Size.x;
                float scale = std::max(1.0f, t.FontSize > 0.0f ? t.FontSize : toggle ? uiRect.GetHeight() * 0.55f : t.Size.y);
                glm::mat4 tm = glm::translate(glm::mat4(1.0f), glm::vec3(textCenter, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(scale, scale, 1.0f));
                glm::vec2 size = Renderer2D::MeasureString(text->Text, text->FontAsset, params);
                float asc = text->FontAsset ? text->FontAsset->GetAscent() : 0.8f;
                // Text is vertically centered on the rect; horizontally centered or left-aligned.
                float x = text->Centered ? -size.x * 0.5f : -textWidth * 0.5f / scale;
                tm = tm * glm::translate(glm::mat4(1.0f), { x, size.y * 0.5f - asc, 0.0f });
                Renderer2D::DrawString(text->Text, text->FontAsset, tm, params, id);
            }
        }
        Renderer2D::EndScene();
        RenderCommand::SetDepthTest(true);
    }

}
