#include "GameplayRuntime.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace Zamin::Gameplay {
    namespace {
        bool Position(glm::vec3 v, float limit) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit; }
    }
    std::string EncodeCheckpoint(const Checkpoint& point)
    {
        if (point.Scene.empty() || point.Scene.size() > 128 || !point.Actor || !Position(point.Position, 1e9f) || !Position(point.Rotation, 10000)) throw std::runtime_error("Invalid checkpoint identity/transform");
        return Visual::Value{ { "Format", "Zamin.GameplayCheckpoint" }, { "Version", 1 }, { "Scene", point.Scene }, { "Actor", std::to_string(point.Actor) }, { "Position", { point.Position.x, point.Position.y, point.Position.z } }, { "Rotation", { point.Rotation.x, point.Rotation.y, point.Rotation.z } } }.dump();
    }
    bool DecodeCheckpoint(std::string_view text, Checkpoint& point, std::string& error)
    {
        error.clear();
        try
        {
            if (text.size() > 4096) throw std::runtime_error("Checkpoint exceeds 4 KiB");
            auto value = Visual::Value::parse(text, [](int depth, Visual::Value::parse_event_t, Visual::Value&) {if(depth>8)throw std::runtime_error("Checkpoint nesting limit");return true; });
            if (value.at("Format") != "Zamin.GameplayCheckpoint" || value.at("Version") != 1) throw std::runtime_error("Unsupported checkpoint format/version");
            Checkpoint next;
            next.Scene = value.at("Scene").get<std::string>();
            const auto id = value.at("Actor").get<std::string>();
            if (id.empty() || id.size() > 20 || id.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("Invalid checkpoint actor ID");
            size_t used = 0;
            next.Actor = std::stoull(id, &used);
            if (used != id.size()) throw std::runtime_error("Invalid checkpoint actor ID");
            for (const char* key : { "Position", "Rotation" })
            {
                const auto& data = value.at(key);
                if (!data.is_array() || data.size() != 3 || !std::all_of(data.begin(), data.end(), [](const auto& n) { return n.is_number() && std::isfinite(n.template get<double>()); })) throw std::runtime_error("Invalid checkpoint vector");
                auto& v = std::string(key) == "Position" ? next.Position : next.Rotation;
                for (int i = 0; i < 3; ++i) v[i] = data.at(i).get<float>();
            }
            EncodeCheckpoint(next);
            point = std::move(next);
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
}
