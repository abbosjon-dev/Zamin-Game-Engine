#include "Zamin/Physics/PhysicsDefaults.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace Zamin {

    using ordered_json = nlohmann::ordered_json;

    void PhysicsDefaults::Sanitize()
    {
        for (int i = 0; i < 2; i++)
            Gravity2D[i] = std::isfinite(Gravity2D[i]) ? std::clamp(Gravity2D[i], -MaxGravity, MaxGravity) : EngineGravity2D[i];
        for (int i = 0; i < 3; i++)
            Gravity3D[i] = std::isfinite(Gravity3D[i]) ? std::clamp(Gravity3D[i], -MaxGravity, MaxGravity) : EngineGravity3D[i];
    }

    const PhysicsDefaults::Preset* PhysicsDefaults::GetPresets(int& count)
    {
        static const Preset s_Presets[] = {
            { "Yer (9.81)", 9.81f }, { "Oy (1.62)", 1.62f }, { "Mars (3.71)", 3.71f }, { "Yupiter (24.79)", 24.79f },
            { "Platformer (20)", 20.0f }, { "Vaznsizlik (0)", 0.0f },
        };
        count = (int)(sizeof(s_Presets) / sizeof(s_Presets[0]));
        return s_Presets;
    }

    std::string PhysicsDefaults::ToJson() const
    {
        ordered_json j;
        j["Gravity2D"] = { Gravity2D.x, Gravity2D.y };
        j["Gravity3D"] = { Gravity3D.x, Gravity3D.y, Gravity3D.z };
        return j.dump(4);
    }

    bool PhysicsDefaults::FromJson(const std::string& text, PhysicsDefaults& out)
    {
        ordered_json j = ordered_json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        PhysicsDefaults s;
        auto read = [&](const char* key, float* values, int count) {
            auto it = j.find(key);
            if (it == j.end() || !it->is_array() || (int)it->size() != count)
                return;
            for (int i = 0; i < count; i++)
                if ((*it)[(size_t)i].is_number())
                    values[i] = (float)(*it)[(size_t)i].get<double>();
        };
        read("Gravity2D", &s.Gravity2D.x, 2);
        read("Gravity3D", &s.Gravity3D.x, 3);
        s.Sanitize();
        out = s;
        return true;
    }

}
