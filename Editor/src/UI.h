#pragma once

#include <Zamin/ImGui/Icons.h>
#include <Zamin/ImGui/ImGuiLayer.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <string>

// Small helpers for a consistent, good-looking editor UI.
namespace Zamin::UI {

    inline ImVec4 AccentColor() { return ImVec4(0.180f, 0.737f, 0.580f, 1.0f); }
    inline ImU32 AccentU32(float alpha = 1.0f) { return ImGui::GetColorU32(ImVec4(0.180f, 0.737f, 0.580f, alpha)); }

    // Two-column property table: label | widget
    inline bool BeginPropertyGrid(const char* id)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_PadOuterX))
            return false;
        float labelWidth = std::min(ImGui::GetContentRegionAvail().x * 0.36f, ImGui::GetFontSize() * 8.5f);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    inline void EndPropertyGrid() { ImGui::EndTable(); }

    inline void PropertyLabel(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.72f, 0.75f, 0.80f, 1.0f), "%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // Same, with a help text shown when the label is hovered (a dotted underline hints at it).
    inline void PropertyLabel(const char* label, const char* tooltip)
    {
        if (!tooltip || !*tooltip)
        {
            PropertyLabel(label);
            return;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.80f, 0.83f, 0.88f, 1.0f), "%s", label);
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImU32 dots = ImGui::GetColorU32(ImVec4(0.55f, 0.60f, 0.68f, 0.9f));
        for (float x = min.x; x < max.x; x += 3.0f)
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, max.y), ImVec2(std::min(x + 1.5f, max.x), max.y), dots);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tooltip);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // Section title row inside a property grid (accent text + a line across the value column).
    inline void PropertyHeader(const char* text)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::TextColored(AccentColor(), "%s", text);
        float y = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
        ImGui::TableSetColumnIndex(1);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(cursor.x, y), ImVec2(cursor.x + width, y), AccentU32(0.35f));
        ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    }

    inline bool PropertyFloat(const char* label, float& v, float speed = 0.05f, float min = 0.0f, float max = 0.0f, const char* fmt = "%.3f")
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::DragFloat("##v", &v, speed, min, max, fmt);
        ImGui::PopID();
        return changed;
    }

    inline bool PropertySlider(const char* label, float& v, float min, float max)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::SliderFloat("##v", &v, min, max);
        ImGui::PopID();
        return changed;
    }

    inline bool PropertyInt(const char* label, int& v)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::DragInt("##v", &v);
        ImGui::PopID();
        return changed;
    }

    inline bool PropertyBool(const char* label, bool& v)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::Checkbox("##v", &v);
        ImGui::PopID();
        return changed;
    }

    inline bool PropertyFloat2(const char* label, glm::vec2& v, float speed = 0.05f)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::DragFloat2("##v", &v.x, speed);
        ImGui::PopID();
        return changed;
    }

    inline bool PropertyFloat3(const char* label, glm::vec3& v, float speed = 0.05f)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::DragFloat3("##v", &v.x, speed);
        ImGui::PopID();
        return changed;
    }

    inline bool PropertyFloat4(const char* label, glm::vec4& v, float speed = 0.05f)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::DragFloat4("##v", &v.x, speed);
        ImGui::PopID();
        return changed;
    }

    inline bool PropertyColor3(const char* label, glm::vec3& v)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::ColorEdit3("##v", &v.x, ImGuiColorEditFlags_Float);
        ImGui::PopID();
        return changed;
    }

    inline bool PropertyColor4(const char* label, glm::vec4& v)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = ImGui::ColorEdit4("##v", &v.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
        ImGui::PopID();
        return changed;
    }

    bool PropertyString(const char* label, std::string& value, bool multiline = false);

    // Path field that accepts drag & drop from the content browser.
    // Returns true when changed. `extensions` e.g. ".png;.jpg"
    bool PropertyAssetPath(const char* label, std::string& path, const char* extensions, const char* icon);

    bool PropertyCombo(const char* label, int& current, const char* const* items, int count);

    // Transform-style vec3 control with colored X/Y/Z buttons (click = reset).
    bool Vec3Control(const char* label, glm::vec3& values, float resetValue = 0.0f, float speed = 0.05f);

    // Toolbar button with an icon; highlighted when `active`.
    bool ToolbarButton(const char* icon, const char* tooltip, bool active = false, ImVec2 size = ImVec2(0, 0));

    void Tooltip(const char* text);

    // Centered text helper.
    void TextCentered(const char* text);

    // Searchable text field with icon.
    bool SearchBox(const char* id, std::string& text, const char* hint = "Qidirish...");

}
