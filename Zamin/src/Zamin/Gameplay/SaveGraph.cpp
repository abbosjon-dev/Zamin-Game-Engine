#include "SaveWorld.h"
#include "Zamin/Scene/Entity.h"
#include <cmath>
namespace Zamin::Gameplay {
    std::optional<Visual::Value> DispatchSave(Entity host, Entity actor, const std::string& kind, const std::map<std::string, Visual::Value>& args)
    {
        using V = Visual::Value;
        if (!kind.starts_with("Gameplay.Save")) return {};
        auto& world = SaveWorld::For(*host.GetScene());
        auto text = [&](const char* k) { return args.at(k).get<std::string>(); };
        std::string error;
        if (kind == "Gameplay.SaveFind")
        {
            auto e = world.Find(text("Key"));
            return V{ { "Found", bool(e) }, { "Active", e && e.IsActive() }, { "Entity", e ? uint64_t(e.GetUUID()) : 0ull } };
        }
        if (kind == "Gameplay.SaveSpawn")
        {
            auto& v = args.at("Position");
            glm::vec3 p;
            for (int i = 0; i < 3; ++i)
            {
                auto n = v.at(i).get<double>();
                if (!std::isfinite(n) || std::abs(n) > 1e9) throw std::runtime_error("Invalid persistent spawn position");
                p[i] = float(n);
            }
            auto e = world.Spawn(text("Key"), text("Prefab"), p, error);
            return V{ { "Success", bool(e) }, { "Entity", e ? uint64_t(e.GetUUID()) : 0ull }, { "Reason", error } };
        }
        if (kind == "Gameplay.SaveBind" || kind == "Gameplay.SaveUnbind")
        {
            bool ok = kind == "Gameplay.SaveBind" ? world.Bind(actor, text("Key"), error) : world.Unbind(text("Key"));
            return V{ { "Success", ok }, { "Reason", error } };
        }
        SaveResult r;
        if (kind == "Gameplay.SaveWrite") r = world.Save(text("Slot"), text("Schema"));
        else if (kind == "Gameplay.SaveLoad") r = world.Load(text("Slot"), text("Schema"));
        else if (kind == "Gameplay.SaveInspect") r = world.Inspect(text("Slot"));
        else throw std::runtime_error("Unknown gameplay save operation");
        return V{ { "Success", r.Success }, { "Recovered", r.Recovered }, { "Entities", r.Entities }, { "Reason", r.Reason } };
    }
}
