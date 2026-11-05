#include "UI.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <filesystem>
#include <cstring>

namespace Zamin::UI {

    bool PropertyString(const char* label, std::string& value, bool multiline)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = multiline ? ImGui::InputTextMultiline("##v", &value, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f))
                                 : ImGui::InputText("##v", &value);
        ImGui::PopID();
        return changed;
    }

    static bool HasExtension(const std::string& path, const char* extensions)
    {
        if (!extensions || !*extensions) return true;
        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        std::string list = extensions;
        size_t start = 0;
        while (start <= list.size())
        {
            size_t end = list.find(';', start);
            if (end == std::string::npos) end = list.size();
            if (list.substr(start, end - start) == ext)
                return true;
            start = end + 1;
        }
        return false;
    }

    bool PropertyAssetPath(const char* label, std::string& path, const char* extensions, const char* icon)
    {
        return PropertyAssetPath(label, path, extensions, icon, AssetPathOptions{});
    }

    bool PropertyAssetPath(const char* label, std::string& path, const char* extensions, const char* icon, const AssetPathOptions& options)
    {
        PropertyLabel(label, options.Tooltip);
        ImGui::PushID(label);
        bool changed = false;

        float buttonWidth = ImGui::GetFrameHeight();
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        bool action = options.ActionIcon && options.ActionClicked;
        float width = ImGui::GetContentRegionAvail().x - buttonWidth - spacing;
        if (action)
            width -= buttonWidth + spacing;
        if (options.PreviewTexture)
        {
            // A small picture of the asset (a texture) before its path.
            float size = ImGui::GetFrameHeight();
            ImGui::Image((ImTextureID)(uintptr_t)options.PreviewTexture, ImVec2(size, size), ImVec2(0, 1), ImVec2(1, 0));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Image((ImTextureID)(uintptr_t)options.PreviewTexture, ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
            width -= size + spacing;
        }
        std::string display = path.empty() ? std::string(icon) + "  (yo'q)" : std::string(icon) + "  " + path;

        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (options.Missing && !path.empty())
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.4f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::Button(display.c_str(), ImVec2(std::max(width, 20.0f), 0));
        ImGui::PopStyleVar();
        if (options.Missing && !path.empty())
            ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            Tooltip(options.Missing && !path.empty() ? "Fayl yuklanmadi (yo'q yoki buzilgan) - konsolni tekshiring.\n"
                                                       "Content Browser'dan boshqa fayl tortib tashlang"
                                                     : "Content Browser'dan fayl tortib tashlang");

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::string dropped((const char*)payload->Data);
                if (HasExtension(dropped, extensions))
                {
                    path = dropped;
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (action)
        {
            ImGui::SameLine();
            ImGui::BeginDisabled(path.empty());
            if (ImGui::Button(options.ActionIcon, ImVec2(buttonWidth, 0)))
                *options.ActionClicked = true;
            ImGui::EndDisabled();
            if (options.ActionTooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                Tooltip(options.ActionTooltip);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK, ImVec2(buttonWidth, 0)) && !path.empty())
        {
            path.clear();
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    bool PropertyCombo(const char* label, int& current, const char* const* items, int count)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = false;
        if (ImGui::BeginCombo("##v", (current >= 0 && current < count) ? items[current] : ""))
        {
            for (int i = 0; i < count; i++)
            {
                bool selected = current == i;
                if (ImGui::Selectable(items[i], selected))
                {
                    current = i;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    bool Vec3Control(const char* label, glm::vec3& values, float resetValue, float speed)
    {
        bool changed = false;
        PropertyLabel(label);
        ImGui::PushID(label);

        const float spacing = 2.0f;
        float lineHeight = ImGui::GetFrameHeight();
        ImVec2 buttonSize = { lineHeight - 4.0f, lineHeight };
        float full = ImGui::GetContentRegionAvail().x;
        float itemWidth = (full - 3.0f * buttonSize.x - 2.0f * ImGui::GetStyle().ItemSpacing.x - 3 * spacing) / 3.0f;

        const ImVec4 colors[3] = {
            ImVec4(0.80f, 0.22f, 0.25f, 1.0f),
            ImVec4(0.30f, 0.68f, 0.25f, 1.0f),
            ImVec4(0.20f, 0.42f, 0.86f, 1.0f),
        };
        const char* names[3] = { "X", "Y", "Z" };

        for (int i = 0; i < 3; i++)
        {
            ImGui::PushID(i);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, colors[i]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[i].x + 0.1f, colors[i].y + 0.1f, colors[i].z + 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[i]);
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            if (ImGui::Button(names[i], buttonSize))
            {
                values[i] = resetValue;
                changed = true;
            }
            ImGui::PopFont();
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, spacing);
            ImGui::SetNextItemWidth(itemWidth);
            changed |= ImGui::DragFloat("##v", &values[i], speed, 0.0f, 0.0f, "%.2f");
            ImGui::PopStyleVar(2);
            ImGui::PopID();
            if (i < 2) ImGui::SameLine();
        }

        ImGui::PopID();
        return changed;
    }

    bool ToolbarButton(const char* icon, const char* tooltip, bool active, ImVec2 size)
    {
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.52f, 0.41f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.62f, 0.50f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
        }
        bool pressed = ImGui::Button(icon, size);
        ImGui::PopStyleColor(2);
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            Tooltip(tooltip);
        return pressed;
    }

    void Tooltip(const char* text)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(text);
        ImGui::EndTooltip();
        ImGui::PopStyleVar();
    }

    void TextCentered(const char* text)
    {
        float width = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
        ImGui::TextUnformatted(text);
    }

    bool SearchBox(const char* id, std::string& text, const char* hint)
    {
        ImGui::PushID(id);
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::string fullHint = std::string(ICON_FA_MAGNIFYING_GLASS "  ") + hint;
        bool changed = ImGui::InputTextWithHint("##search", fullHint.c_str(), &text);
        ImGui::PopID();
        return changed;
    }

}
