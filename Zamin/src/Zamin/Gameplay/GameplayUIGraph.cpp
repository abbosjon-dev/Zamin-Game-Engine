#include "GameplayUI.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scripting/Visual/VisualGraphHost.h"
#include <cmath>
namespace Zamin::Gameplay {
    std::optional<Visual::Value> DispatchUI(Entity host, Entity target, const std::string& kind, const std::map<std::string, Visual::Value>& a)
    {
        using V = Visual::Value;
        if (!kind.starts_with("Gameplay.UI")) return {};
        auto& scene = *host.GetScene();
        auto& world = UIWorld::For(scene);
        auto text = [&](const char* k) { return a.at(k).get<std::string>(); };
        auto flag = [&](const char* k) { return a.at(k).get<bool>(); };
        auto ref = [&](const V& v) {auto id=v.get<uint64_t>();return id==UINT64_MAX?host:id?scene.GetEntityByUUID(UUID(id)):Entity{}; };
        std::string error;
        bool ok = false;
        if (kind == "Gameplay.UIConfigure")
        {
            auto focus = ref(a.at("Focus"));
            if (a.at("Focus").get<uint64_t>() && !focus) return V{ { "Success", false }, { "Reason", "UI focus missing" } };
            UIPanelSpec spec{ text("Name"), text("Action"), focus ? uint64_t(focus.GetUUID()) : 0ull, flag("Pause"), flag("Block"), flag("Back") };
            ok = world.ConfigurePanel(host, target, spec, error);
        }
        else if (kind == "Gameplay.UIOpen") ok = world.Open(text("Name"), error);
        else if (kind == "Gameplay.UIClose") ok = world.Close(text("Name"), error);
        else if (kind == "Gameplay.UIBack") ok = world.Back(error);
        else if (kind == "Gameplay.UIRemove") ok = world.RemovePanel(text("Name"));
        else if (kind == "Gameplay.UIClear")
        {
            world.Clear();
            ok = true;
        }
        else if (kind == "Gameplay.UIRunWhenPaused")
        {
            if (auto* n = target.TryGetComponent<NativeScriptComponent>(); n && n->Instance)
            {
                n->Instance->SetUpdateWhenPaused(flag("Enabled"));
                ok = true;
            }
            else error = "UI controller script missing";
        }
        else if (kind == "Gameplay.UIState")
        {
            auto r = world.State();
            auto selected = scene.GetSelectedUI();
            return V{ { "Top", r.Top }, { "Depth", r.Depth }, { "Paused", r.Paused }, { "Blocked", r.Blocked }, { "Fenced", r.Fenced }, { "Selected", selected ? uint64_t(selected.GetUUID()) : 0ull }, { "Time", scene.GetUnscaledTime() }, { "GameTime", scene.GetTime() }, { "Delta", scene.GetUnscaledDeltaTime() }, { "Dropped", r.Dropped } };
        }
        else if (kind == "Gameplay.UIEvent")
        {
            auto r = world.Poll();
            return V{ { "Has", bool(r) }, { "Kind", r ? r->Kind : "" }, { "Name", r ? r->Name : "" }, { "Item", r ? r->Item : "" }, { "Subject", r ? r->Subject : 0ull } };
        }
        else if (kind == "Gameplay.UIFocus")
        {
            auto e = ref(a.at("Widget"));
            ok = e && scene.IsUISelectable(e);
            if (ok) scene.SetSelectedUI(e);
        }
        else if (kind == "Gameplay.UIBindHUD")
        {
            auto k = a.at("Kind").get<double>();
            if (!std::isfinite(k) || k < 0 || k > 4 || std::floor(k) != k) throw std::runtime_error("Invalid HUD kind 0..4");
            auto actor = ref(a.at("Actor"));
            HUDBindingSpec spec{ HUDKind(int(k)), text("Item"), text("Prefix"), flag("Ratio") };
            ok = world.BindHUD(host, actor, target, spec, error);
        }
        else if (kind == "Gameplay.UIUnbindHUD") ok = world.UnbindHUD(target);
        else if (kind == "Gameplay.UIInventoryConfigure")
        {
            auto& array = a.at("Rows");
            if (!array.is_array() || array.empty() || array.size() > 16) throw std::runtime_error("Inventory UI row limit 1..16");
            std::vector<Entity> rows;
            for (auto& v : array) rows.push_back(ref(v));
            ok = world.BindInventory(host, ref(a.at("Actor")), text("Name"), rows, error);
        }
        else if (kind == "Gameplay.UIInventoryState")
        {
            auto r = world.InventoryState(text("Name"));
            return V{ { "Configured", r.Configured }, { "Available", r.Available }, { "Page", r.Page }, { "Pages", r.Pages }, { "Item", r.Item }, { "Count", r.Count }, { "UsedSlots", r.Used }, { "Slots", r.Slots } };
        }
        else if (kind == "Gameplay.UIInventoryPage")
        {
            auto d = a.at("Delta").get<double>();
            if (!std::isfinite(d) || d < -64 || d > 64 || std::floor(d) != d) throw std::runtime_error("Inventory page delta -64..64");
            ok = world.Page(text("Name"), int(d));
        }
        else if (kind == "Gameplay.UIInventoryRemove") ok = world.RemoveInventory(text("Name"));
        else throw std::runtime_error("Unknown gameplay UI operation");
        if (!ok && error.empty()) error = "UI operation unavailable or busy";
        return V{ { "Success", ok }, { "Reason", error } };
    }
}
