#pragma once

#include <glm/glm.hpp>

namespace Zamin::ThemeColors {
    inline glm::vec3 Hover(const glm::vec3& accent)
    {
        return glm::clamp(glm::mix(accent, glm::vec3(1), 0.18f), glm::vec3(0), glm::vec3(1));
    }
    inline glm::vec3 Pressed(const glm::vec3& accent)
    {
        return glm::clamp(accent * 0.68f, glm::vec3(0), glm::vec3(1));
    }
}
