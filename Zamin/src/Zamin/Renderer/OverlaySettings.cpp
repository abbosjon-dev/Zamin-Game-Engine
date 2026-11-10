#include "Zamin/Renderer/EditorViewSettings.h"
#include "Zamin/Renderer/SettingsMath.h"
#include <algorithm>
#include <cmath>

namespace Zamin {
    using SettingsMath::SanitizeStep;
    // ------------------------------------------------------------------ snapping
    float SnapSettings::GetStep(int operation) const
    {
        switch (operation)
        {
            case 1:  return Rotate;
            case 2:  return Scale;
            default: return Translate;
        }
    }

    void SnapSettings::Sanitize()
    {
        Translate = SanitizeStep(Translate, 0.5f, MinStep);
        Rotate = std::min(SanitizeStep(Rotate, 15.0f, MinStep), 360.0f);
        Scale = SanitizeStep(Scale, 0.1f, MinStep);
    }

    // ------------------------------------------------------------------ grid
    void GridSettings::Sanitize()
    {
        CellSize = SanitizeStep(CellSize, 1.0f, MinCellSize);
        MajorEvery = std::clamp(MajorEvery, 1, MaxMajorEvery);
        FadeDistance = std::clamp(SanitizeStep(FadeDistance, 1.0f, 0.1f), 0.1f, 10.0f);
        Color = glm::clamp(Color, glm::vec4(0.0f), glm::vec4(1.0f));
        MajorColor = glm::clamp(MajorColor, glm::vec4(0.0f), glm::vec4(1.0f));
    }

    // ------------------------------------------------------------------ transform gizmo
    static float SanitizeFactor(float value, float minimum, float maximum)
    {
        if (!std::isfinite(value)) return 1.0f;
        return std::clamp(value, minimum, maximum);
    }

    void TransformGizmoSettings::Sanitize()
    {
        Size = SanitizeFactor(Size, MinSize, MaxSize);
        LineThickness = SanitizeFactor(LineThickness, MinThickness, MaxThickness);
        IconSize = SanitizeFactor(IconSize, MinIconSize, MaxIconSize);
    }

    // ------------------------------------------------------------------ selection outline
    static glm::vec4 SanitizeColor(const glm::vec4& color, const glm::vec4& fallback)
    {
        for (int i = 0; i < 4; i++)
            if (!std::isfinite(color[i])) return fallback;
        return glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
    }

    glm::vec4 SelectionOutlineSettings::GetSecondaryColor() const
    {
        return glm::vec4(glm::mix(glm::vec3(Color), glm::vec3(1.0f), 0.4f), Color.a * 0.9f);
    }

    glm::vec4 SelectionOutlineSettings::GetChildColor() const
    {
        return glm::vec4(glm::mix(glm::vec3(Color), glm::vec3(1.0f), 0.15f), Color.a * 0.5f);
    }

    void SelectionOutlineSettings::Sanitize()
    {
        if ((int)Style < (int)SelectionStyle::Outline || (int)Style > (int)SelectionStyle::Box)
            Style = SelectionStyle::Outline;
        Width = std::isfinite(Width) ? std::clamp(Width, MinWidth, MaxWidth) : 2.0f;
        HoverWidth = std::isfinite(HoverWidth) ? std::clamp(HoverWidth, MinWidth, MaxWidth) : 1.5f;
        OccludedOpacity = std::isfinite(OccludedOpacity) ? std::clamp(OccludedOpacity, 0.0f, 1.0f) : 0.35f;
        SelectionOutlineSettings defaults;
        Color = SanitizeColor(Color, defaults.Color);
        HoverColor = SanitizeColor(HoverColor, defaults.HoverColor);
    }

}
