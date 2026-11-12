#include "GameplayUI.h"
#include "GameplayRules.h"
#include "QuestWorld.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include "Zamin/Core/Input.h"
#include <algorithm>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        bool SelectableWidget(Entity e) { return e && e.HasComponent<UITransformComponent>() && (e.HasComponent<UIButtonComponent>() || e.HasComponent<UISliderComponent>() || e.HasComponent<UIToggleComponent>() || e.HasComponent<UIInputFieldComponent>()); }
        struct Guard
        {
            bool& Busy;
            explicit Guard(bool& busy) : Busy(busy) { Busy = true; }
            ~Guard() { Busy = false; }
        };
    }
    UIWorld& UIWorld::For(Scene& scene)
    {
        auto& ctx = scene.GetRegistry().ctx();
        if (auto* w = ctx.find<UIWorld>()) return *w;
        return ctx.emplace<UIWorld>(&scene);
    }
    void ResetGameplayUI(Scene& scene)
    {
        if (auto* w = scene.GetRegistry().ctx().find<UIWorld>()) w->Clear();
        scene.GetRegistry().ctx().erase<UIWorld>();
    }
    void BeginGameplayUI(Scene& scene)
    {
        if (auto* w = scene.GetRegistry().ctx().find<UIWorld>()) w->BeginFrame();
    }
    void RefreshGameplayUI(Scene& scene)
    {
        if (auto* w = scene.GetRegistry().ctx().find<UIWorld>()) w->Refresh();
    }
    bool GameplayUIAllows(Scene& scene, Entity e)
    {
        auto* w = scene.GetRegistry().ctx().find<UIWorld>();
        return !w || w->Allows(e);
    }
    bool GameplayUIFenced(Scene& scene)
    {
        auto* w = scene.GetRegistry().ctx().find<UIWorld>();
        return w && w->Fenced();
    }
    bool GameplayUIBlocks(Scene& scene)
    {
        auto* w = scene.GetRegistry().ctx().find<UIWorld>();
        return w && w->BlocksGame();
    }
    UIWorld::Ref UIWorld::Reference(Entity e) const { return e ? Ref{ uint64_t(e.GetUUID()), uint32_t(e) } : Ref{}; }
    Entity UIWorld::Resolve(Ref ref) const
    {
        auto e = ref.ID ? m_Scene->GetEntityByUUID(UUID(ref.ID)) : Entity{};
        return e && uint32_t(e) == ref.Handle ? e : Entity{};
    }
    UIWorld::Owner UIWorld::Own(Entity e) const { return { Reference(e), e.HasComponent<NativeScriptComponent>() }; }
    bool UIWorld::Live(const Owner& o) const
    {
        auto e = Resolve(o.Entity);
        return e && (!o.Script || e.HasComponent<NativeScriptComponent>());
    }
    bool UIWorld::Enabled(const Owner& o) const
    {
        auto e = Resolve(o.Entity);
        if (!e || !m_Scene->IsActiveInHierarchy(e)) return false;
        if (auto* n = e.TryGetComponent<NativeScriptComponent>())
        {
            if (!n->Enabled || (o.Script && !n->Instance)) return false;
            if (auto* g = dynamic_cast<VisualGraphHost*>(n->Instance); g && (g->VM.Paused || !g->VM.Error().empty())) return false;
        }
        return true;
    }
    bool UIWorld::Inside(Entity e, Entity root) const
    {
        if (!e || !root || e.GetScene() != m_Scene || root.GetScene() != m_Scene) return false;
        for (int depth = 0; e && depth < 256; ++depth)
        {
            if (e == root) return true;
            e = m_Scene->GetEntityByUUID(e.GetComponent<RelationshipComponent>().Parent);
        }
        return false;
    }
    bool UIWorld::Allows(Entity e) const
    {
        auto name = m_Stack.Top();
        if (name.empty()) return true;
        auto i = m_Panels.find(name);
        return i != m_Panels.end() && Inside(e, Resolve(i->second.Root));
    }
    bool UIWorld::WidgetUsed(Entity e) const
    {
        if (QuestWidgetBound(*m_Scene, e)) return true;
        if (m_Bindings.contains(uint64_t(e.GetUUID()))) return true;
        for (auto& [name, inv] : m_Inventories)
            for (auto& row : inv.Rows)
                if (Resolve(row.Target) == e) return true;
        return false;
    }
    UIWorld::Widget UIWorld::Remember(Entity e) const
    {
        Widget out;
        out.Target = Reference(e);
        if (auto* t = e.TryGetComponent<TextComponent>()) out.Text = t->Text;
        if (auto* b = e.TryGetComponent<UIProgressBarComponent>())
        {
            out.Value = b->Value;
            out.Display = b->DisplayValue;
        }
        if (auto* b = e.TryGetComponent<UIButtonComponent>()) out.Interactable = b->Interactable;
        return out;
    }
    void UIWorld::RestoreWidget(const Widget& v)
    {
        auto e = Resolve(v.Target);
        if (!e) return;
        if (auto* t = e.TryGetComponent<TextComponent>()) t->Text = v.Text;
        if (auto* b = e.TryGetComponent<UIProgressBarComponent>())
        {
            b->Value = v.Value;
            b->DisplayValue = v.Display;
        }
        if (auto* b = e.TryGetComponent<UIButtonComponent>()) b->Interactable = v.Interactable;
    }
    void UIWorld::Emit(UIEvent e)
    {
        if (m_Events.size() == 32)
        {
            m_Events.pop_front();
            if (m_Dropped != UINT32_MAX) ++m_Dropped;
        }
        m_Events.push_back(std::move(e));
    }
    std::optional<UIEvent> UIWorld::Poll()
    {
        if (m_Events.empty()) return {};
        auto e = std::move(m_Events.front());
        m_Events.pop_front();
        return e;
    }
    UIState UIWorld::State() const { return { m_Stack.Top(), m_Stack.Entries().size(), m_Scene->IsGamePaused(), BlocksGame(), m_Fenced, m_Dropped }; }
    bool UIWorld::ConfigurePanel(Entity owner, Entity root, const UIPanelSpec& spec, std::string& error)
    {
        error.clear();
        if (m_Busy || !m_Scene->IsRunning() || !owner || !root || owner.GetScene() != m_Scene || root.GetScene() != m_Scene || !root.HasComponent<UITransformComponent>() || root.HasComponent<UICanvasComponent>() || !ValidUIName(spec.Name) || spec.Action.size() > 64 || m_Panels.contains(spec.Name) || m_Panels.size() >= 16 || Inside(owner, root))
        {
            error = "Invalid UI panel, owner or capacity (16)";
            return false;
        }
        for (auto& [name, p] : m_Panels)
            if ((Resolve(p.Root) && root.GetComponent<RelationshipComponent>().Parent != Resolve(p.Root).GetComponent<RelationshipComponent>().Parent) || Inside(root, Resolve(p.Root)) || Inside(Resolve(p.Root), root) || (!spec.Action.empty() && spec.Action == p.Spec.Action))
            {
                error = "Panels must have disjoint roots and unique toggle actions";
                return false;
            }
        auto focus = spec.Focus ? m_Scene->GetEntityByUUID(UUID(spec.Focus)) : Entity{};
        if (spec.Focus && (!SelectableWidget(focus) || !Inside(focus, root)))
        {
            error = "Initial focus must be a widget inside the panel";
            return false;
        }
        Guard guard(m_Busy);
        Panel p;
        p.Host = Own(owner);
        p.Root = Reference(root);
        p.Focus = Reference(focus);
        p.Spec = spec;
        p.OriginalActive = root.IsActive();
        p.OriginalOrder = root.GetComponent<UITransformComponent>().Order;
        m_Panels.emplace(spec.Name, std::move(p));
        m_Scene->HideUI(root, false);
        return true;
    }
    void UIWorld::FocusTop()
    {
        m_Scene->FocusInputField({});
        auto name = m_Stack.Top();
        if (name.empty())
        {
            auto e = Resolve(m_Previous);
            m_Scene->SetSelectedUI(e && m_Scene->IsUISelectable(e) ? e : Entity{});
            return;
        }
        auto& p = m_Panels.at(name);
        for (auto ref : { p.Last, p.Focus })
            if (auto e = Resolve(ref); e && m_Scene->IsUISelectable(e))
            {
                m_Scene->SetSelectedUI(e);
                return;
            }
        Entity best;
        int scanned = 0;
        for (auto handle : m_Scene->GetRegistry().view<UITransformComponent>())
        {
            if (++scanned > 4096) break;
            Entity e{ handle, m_Scene };
            if (m_Scene->IsUISelectable(e) && (!best || uint64_t(e.GetUUID()) < uint64_t(best.GetUUID()))) best = e;
        }
        m_Scene->SetSelectedUI(best);
    }
    void UIWorld::Apply()
    {
        m_Fenced = true;
        for (auto& [name, p] : m_Panels)
            if (auto root = Resolve(p.Root); root && root.HasComponent<UITransformComponent>()) root.GetComponent<UITransformComponent>().Order = p.OriginalOrder;
        int order = 100000;
        for (auto& entry : m_Stack.Entries())
            if (auto root = Resolve(m_Panels.at(entry.Name).Root)) root.GetComponent<UITransformComponent>().Order = order++;
        if (m_OwnsTime && m_Scene->IsRunning()) m_Scene->SetTimeScale(m_Stack.Pauses() ? 0 : m_Baseline);
        if (m_Stack.Entries().empty()) m_OwnsTime = false;
        FocusTop();
    }
    bool UIWorld::Open(const std::string& name, std::string& error)
    {
        error.clear();
        auto it = m_Panels.find(name);
        if (m_Busy || it == m_Panels.end() || !Live(it->second.Host) || !Enabled(it->second.Host) || !Resolve(it->second.Root))
        {
            error = "UI panel unavailable or busy";
            return false;
        }
        if (m_Stack.Top() == name) return true;
        auto next = m_Stack;
        if (!next.Push({ name, it->second.Spec.Pause, it->second.Spec.Block }, error)) return false;
        Guard guard(m_Busy);
        auto selected = Reference(m_Scene->GetSelectedUI());
        if (m_Stack.Entries().empty())
        {
            m_Previous = selected;
            m_Baseline = m_Scene->GetTimeScale();
            m_OwnsTime = true;
        }
        else m_Panels.at(m_Stack.Top()).Last = selected;
        it->second.Previous = selected;
        m_Stack = std::move(next);
        m_Scene->ShowUI(Resolve(it->second.Root));
        Apply();
        Emit({ "Open", name, "", it->second.Root.ID });
        return true;
    }
    bool UIWorld::Close(const std::string& name, std::string& error)
    {
        error.clear();
        if (m_Busy || m_Stack.Top() != name || name.empty())
        {
            error = "Only the top UI panel can close";
            return false;
        }
        Guard guard(m_Busy);
        auto& p = m_Panels.at(name);
        p.Last = Reference(m_Scene->GetSelectedUI());
        m_Stack.Pop();
        if (auto root = Resolve(p.Root)) m_Scene->HideUI(root, false);
        Apply();
        Emit({ "Close", name, "", p.Root.ID });
        return true;
    }
    bool UIWorld::Back(std::string& error)
    {
        auto i = m_Panels.find(m_Stack.Top());
        if (i == m_Panels.end() || !i->second.Spec.Back)
        {
            error = "UI back is disabled or stack empty";
            return false;
        }
        return Close(i->first, error);
    }
    bool UIWorld::RemovePanel(const std::string& name)
    {
        auto it = m_Panels.find(name);
        if (m_Busy || it == m_Panels.end()) return false;
        Guard guard(m_Busy);
        m_Stack.Remove(name);
        auto root = Resolve(it->second.Root);
        bool active = it->second.OriginalActive;
        if (root && root.HasComponent<UITransformComponent>()) root.GetComponent<UITransformComponent>().Order = it->second.OriginalOrder;
        m_Panels.erase(it);
        if (root) m_Scene->SetActive(root, active);
        Apply();
        return true;
    }
    bool UIWorld::BindHUD(Entity owner, Entity actor, Entity widget, const HUDBindingSpec& spec, std::string& error)
    {
        error.clear();
        if (m_Busy || !m_Scene->IsRunning() || !owner || !actor || !widget || owner.GetScene() != m_Scene || actor.GetScene() != m_Scene || widget.GetScene() != m_Scene || !widget.HasComponent<UITransformComponent>() || (widget.HasComponent<TextComponent>() && !widget.GetComponent<TextComponent>().LocalizationKey.empty()) || (!widget.HasComponent<TextComponent>() && !widget.HasComponent<UIProgressBarComponent>()) || int(spec.Kind) < 0 || int(spec.Kind) > 4 || spec.Prefix.size() > 64 || (spec.Kind == HUDKind::Item && !ValidItem(spec.Item)) || WidgetUsed(widget) || m_Bindings.size() >= 128)
        {
            error = "Invalid HUD binding or capacity (128)";
            return false;
        }
        m_Bindings.emplace(uint64_t(widget.GetUUID()), Binding{ Own(owner), Reference(actor), Remember(widget), spec });
        return true;
    }
    bool UIWorld::UnbindHUD(Entity widget)
    {
        if (m_Busy || !widget || widget.GetScene() != m_Scene) return false;
        auto i = m_Bindings.find(uint64_t(widget.GetUUID()));
        if (i == m_Bindings.end() || Resolve(i->second.View.Target) != widget) return false;
        RestoreWidget(i->second.View);
        m_Bindings.erase(i);
        return true;
    }
    bool UIWorld::BindInventory(Entity owner, Entity actor, const std::string& name, const std::vector<Entity>& rows, std::string& error)
    {
        error.clear();
        if (m_Busy || !m_Scene->IsRunning() || !owner || !actor || owner.GetScene() != m_Scene || actor.GetScene() != m_Scene || !ValidUIName(name) || rows.empty() || rows.size() > 16 || m_Inventories.contains(name) || m_Inventories.size() >= 8)
        {
            error = "Invalid inventory view or capacity (8 views, 16 rows)";
            return false;
        }
        std::vector<Widget> next;
        for (auto e : rows)
        {
            if (!e || e.GetScene() != m_Scene || !e.HasComponent<UITransformComponent>() || !e.HasComponent<TextComponent>() || !e.HasComponent<UIButtonComponent>() || !e.GetComponent<TextComponent>().LocalizationKey.empty() || WidgetUsed(e) || std::any_of(next.begin(), next.end(), [&](auto& v) { return Resolve(v.Target) == e; }))
            {
                error = "Inventory rows must be distinct, unbound text buttons";
                return false;
            }
            next.push_back(Remember(e));
        }
        Inventory record;
        record.Host = Own(owner);
        record.Actor = Reference(actor);
        record.Rows = std::move(next);
        m_Inventories.emplace(name, std::move(record));
        return true;
    }
    bool UIWorld::RemoveInventory(const std::string& name)
    {
        auto i = m_Inventories.find(name);
        if (m_Busy || i == m_Inventories.end()) return false;
        for (auto& row : i->second.Rows) RestoreWidget(row);
        m_Inventories.erase(i);
        return true;
    }
    void UIWorld::SelectInventory(const std::string& name, Inventory& record)
    {
        auto actor = Resolve(record.Actor);
        if (!actor) return;
        auto page = PresentInventory(RuleWorld::For(*m_Scene).Inventory(actor), record.Page, int(record.Rows.size()));
        auto selected = m_Scene->GetSelectedUI();
        std::string key = record.Selected;
        if (!key.empty() && std::none_of(page.Items.begin(), page.Items.end(), [&](auto& i) { return i.Key == key; })) key.clear();
        for (size_t i = 0; i < page.Items.size(); ++i)
            if (Resolve(record.Rows[i].Target) == selected) key = page.Items[i].Key;
        if (key != record.Selected)
        {
            record.Selected = key;
            Emit({ "Selection", name, key, uint64_t(actor.GetUUID()) });
        }
    }
    InventoryUIState UIWorld::InventoryState(const std::string& name)
    {
        InventoryUIState out;
        auto it = m_Inventories.find(name);
        if (it == m_Inventories.end()) return out;
        out.Configured = true;
        auto& record = it->second;
        auto actor = Resolve(record.Actor);
        if (!actor) return out;
        SelectInventory(name, record);
        auto bag = RuleWorld::For(*m_Scene).Inventory(actor);
        auto page = PresentInventory(bag, record.Page, int(record.Rows.size()));
        out.Available = true;
        out.Page = page.Page;
        out.Pages = page.Pages;
        out.Used = int(bag.Items.size());
        out.Slots = bag.Slots;
        out.Item = record.Selected;
        out.Count = bag.Count(out.Item);
        return out;
    }
    bool UIWorld::Page(const std::string& name, int delta)
    {
        auto it = m_Inventories.find(name);
        if (m_Busy || it == m_Inventories.end() || delta < -64 || delta > 64) return false;
        auto actor = Resolve(it->second.Actor);
        if (!actor) return false;
        auto page = PresentInventory(RuleWorld::For(*m_Scene).Inventory(actor), it->second.Page + delta, int(it->second.Rows.size()));
        it->second.Page = page.Page;
        it->second.Selected.clear();
        Refresh();
        it = m_Inventories.find(name);
        if (it == m_Inventories.end()) return false;
        if (!m_Stack.Top().empty())
        {
            auto selected = m_Scene->GetSelectedUI();
            Entity focus;
            bool keep = false;
            for (auto& row : it->second.Rows)
                if (auto e = Resolve(row.Target); e && m_Scene->IsUISelectable(e))
                {
                    if (!focus) focus = e;
                    if (e == selected) keep = true;
                }
            if (!keep && focus) m_Scene->SetSelectedUI(focus);
        }
        return true;
    }
    void UIWorld::Refresh()
    {
        if (m_Busy) return;
        Guard guard(m_Busy);
        auto& rules = RuleWorld::For(*m_Scene);
        for (auto i = m_Bindings.begin(); i != m_Bindings.end();)
        {
            auto& r = i->second;
            auto widget = Resolve(r.View.Target);
            if (!widget || !widget.HasComponent<UITransformComponent>() || (!widget.HasComponent<TextComponent>() && !widget.HasComponent<UIProgressBarComponent>()) || !Live(r.Host))
            {
                RestoreWidget(r.View);
                i = m_Bindings.erase(i);
                continue;
            }
            ++i;
            if (!Enabled(r.Host)) continue;
            auto actor = Resolve(r.Actor);
            bool available = bool(actor);
            double value = 0, max = 0;
            if (actor)
            {
                auto h = rules.Health(actor);
                auto bag = rules.Inventory(actor);
                switch (r.Spec.Kind)
                {
                    case HUDKind::Health:
                        available = bool(h);
                        if (h)
                        {
                            value = h->Current;
                            max = h->Settings.Maximum;
                        }
                        break;
                    case HUDKind::Shield:
                        available = bool(h);
                        if (h)
                        {
                            value = h->Shield;
                            max = h->Settings.Shield;
                        }
                        break;
                    case HUDKind::Item:
                        value = bag.Count(r.Spec.Item);
                        max = bag.Stack;
                        break;
                    case HUDKind::Bag:
                        value = double(bag.Items.size());
                        max = bag.Slots;
                        break;
                    case HUDKind::Deaths:
                        available = bool(h);
                        if (h)
                        {
                            value = h->Deaths;
                            max = h->Deaths;
                        }
                        break;
                }
            }
            auto p = available ? PresentHUD(value, max, r.Spec.Prefix, r.Spec.Ratio) : HUDPresentation{ r.Spec.Prefix + "--", 0 };
            if (auto* t = widget.TryGetComponent<TextComponent>()) t->Text = p.Text;
            if (auto* b = widget.TryGetComponent<UIProgressBarComponent>())
            {
                b->Value = p.Fraction;
                if (b->SmoothSpeed <= 0) b->DisplayValue = p.Fraction;
            }
        }
        for (auto i = m_Inventories.begin(); i != m_Inventories.end();)
        {
            auto& r = i->second;
            if (!Live(r.Host) || std::any_of(r.Rows.begin(), r.Rows.end(), [&](const Widget& v) { auto e = Resolve(v.Target); return !e || !e.HasComponent<UITransformComponent>() || !e.HasComponent<TextComponent>() || !e.HasComponent<UIButtonComponent>(); }))
            {
                for (auto& v : r.Rows) RestoreWidget(v);
                i = m_Inventories.erase(i);
                continue;
            }
            auto name = i->first;
            ++i;
            if (!Enabled(r.Host)) continue;
            auto actor = Resolve(r.Actor);
            auto bag = actor ? rules.Inventory(actor) : Bag{};
            if (!actor) bag.Items.clear();
            auto page = PresentInventory(bag, r.Page, int(r.Rows.size()));
            r.Page = page.Page;
            for (size_t index = 0; index < r.Rows.size(); ++index)
            {
                auto e = Resolve(r.Rows[index].Target);
                auto* t = e.TryGetComponent<TextComponent>();
                auto* b = e.TryGetComponent<UIButtonComponent>();
                if (!t || !b) continue;
                bool filled = actor && index < page.Items.size();
                b->Interactable = filled;
                t->Text = filled ? page.Items[index].Key + "  x" + std::to_string(page.Items[index].Count) : "--";
            }
            if (actor) SelectInventory(name, r);
            else r.Selected.clear();
        }
    }
    void UIWorld::BeginFrame()
    {
        if (m_Busy) return;
        m_Fenced = false;
        bool changed = false;
        {
            Guard guard(m_Busy);
            for (auto i = m_Panels.begin(); i != m_Panels.end();)
            {
                auto& r = i->second;
                auto root = Resolve(r.Root);
                if (!root || !root.HasComponent<UITransformComponent>() || !Live(r.Host))
                {
                    changed = m_Stack.Remove(i->first) || changed;
                    if (root)
                    {
                        if (root.HasComponent<UITransformComponent>()) root.GetComponent<UITransformComponent>().Order = r.OriginalOrder;
                        m_Scene->SetActive(root, false);
                    }
                    i = m_Panels.erase(i);
                    continue;
                }
                if (m_Stack.Contains(i->first) && (!Enabled(r.Host) || !root.IsActiveInHierarchy()))
                {
                    changed = m_Stack.Remove(i->first) || changed;
                    m_Scene->HideUI(root, false);
                }
                ++i;
            }
            if (changed) Apply();
        }
        Refresh();
        if (m_Scene->IsUIKeyboardEnabled() && !m_Scene->IsTextInputFocused())
        {
            bool back = Input::IsKeyJustPressed(Key::Escape);
            for (int id = 0; id < Input::MaxGamepads; ++id) back |= Input::IsGamepadButtonJustPressed(GamepadButton::B, id);
            std::string error;
            if (back && !m_Stack.Top().empty()) { Back(error); }
            else
            {
                std::string requested;
                for (auto& [name, p] : m_Panels)
                    if (!p.Spec.Action.empty() && Enabled(p.Host) && Input::IsActionJustPressed(p.Spec.Action))
                    {
                        requested = name;
                        break;
                    }
                if (!requested.empty())
                {
                    if (m_Stack.Top() == requested) Close(requested, error);
                    else Open(requested, error);
                }
            }
        }
        if (!m_Stack.Top().empty())
        {
            auto selected = m_Scene->GetSelectedUI();
            if (!selected || !m_Scene->IsUISelectable(selected))
            {
                Guard guard(m_Busy);
                FocusTop();
            }
        }
    }
    void UIWorld::Clear()
    {
        if (m_Busy) return;
        Guard guard(m_Busy);
        m_Stack.Clear();
        for (auto& [name, p] : m_Panels)
            if (auto e = Resolve(p.Root))
            {
                if (e.HasComponent<UITransformComponent>()) e.GetComponent<UITransformComponent>().Order = p.OriginalOrder;
                m_Scene->SetActive(e, p.OriginalActive);
            }
        m_Panels.clear();
        for (auto& [id, b] : m_Bindings) RestoreWidget(b.View);
        m_Bindings.clear();
        for (auto& [name, i] : m_Inventories)
            for (auto& row : i.Rows) RestoreWidget(row);
        m_Inventories.clear();
        if (m_OwnsTime && m_Scene->IsRunning()) m_Scene->SetTimeScale(m_Baseline);
        m_OwnsTime = false;
        m_Fenced = true;
        m_Events.clear();
        m_Dropped = 0;
    }
}
