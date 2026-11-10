#pragma once

#include "../UI.h"
#include <Zamin/Scene/Entity.h>

namespace Zamin {
    inline void InactiveComponentNote(Entity entity, const char* text)
    {
        if (!entity || entity.IsActiveInHierarchy())
            return;
        UI::PropertyLabel("");
        ImGui::PushTextWrapPos(0.0f); // wraps inside the value column
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.0f), ICON_FA_POWER_OFF "  %s", text);
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Obyekt (yoki ota-onalaridan biri) nofaol. Faollashtirilganda yana ishlaydi.");
    }

}
