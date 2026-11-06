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
            ImVec2 uv0 = options.PreviewTopRowFirst ? ImVec2(0, 0) : ImVec2(0, 1);
            ImVec2 uv1 = options.PreviewTopRowFirst ? ImVec2(1, 1) : ImVec2(1, 0);
            ImGui::Image((ImTextureID)(uintptr_t)options.PreviewTexture, ImVec2(size, size), uv0, uv1);
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Image((ImTextureID)(uintptr_t)options.PreviewTexture, ImVec2(128, 128), uv0, uv1);
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
            width -= size + spacing;
        }
        width = std::max(width, 20.0f);
        std::string display = path.empty() ? std::string(icon) + "  (yo'q)" : std::string(icon) + "  " + path;
        // Too long for the field: the file name (the whole path is in the tooltip).
        bool shortened = false;
        if (!path.empty() && ImGui::CalcTextSize(display.c_str()).x > width - ImGui::GetStyle().FramePadding.x * 2.0f)
        {
            display = std::string(icon) + "  " + std::filesystem::path(path).filename().string();
            shortened = true;
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (options.Missing && !path.empty())
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.4f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::Button(display.c_str(), ImVec2(width, 0));
        ImGui::PopStyleVar();
        if (options.Missing && !path.empty())
            ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            std::string tip = options.Missing && !path.empty() ? "Fayl yuklanmadi (yo'q yoki buzilgan) - konsolni tekshiring.\n"
                                                                 "Content Browser'dan boshqa fayl tortib tashlang"
                                                               : "Content Browser'dan fayl tortib tashlang";
            if (shortened)
                tip = path + "\n" + tip;
            Tooltip(tip.c_str());
        }

        if (ImGui::BeginDragDropTarget())
        {
            // Only a file of the right type lights the field up (and can be dropped).
            const ImGuiPayload* active = ImGui::GetDragDropPayload();
            if (active && active->IsDataType("CONTENT_BROWSER_ITEM") && HasExtension((const char*)active->Data, extensions))
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::string dropped((const char*)payload->Data);
                    if (dropped != path)
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

    bool PropertyMaterialSurface(const MaterialSurfaceRefs& m, bool forModel)
    {
        bool changed = false;
        int shader = (int)m.Shader;
        const char* shaders[] = { forModel ? "Modelning o'zi" : "Lit (yoritilgan, PBR)", "Unlit (yoritilmagan)" };
        PropertyLabel("Shader", forModel ? "Modelning o'zi: glTF materialidagi shader (odatda Lit). "
                                           "Unlit: faqat rang (x tekstura) va nurlanish - yorug'lik, soya va osmon ta'sir qilmaydi."
                                         : "Lit: quyosh, nuqtali chiroqlar, soyalar va osmon bilan yoritiladi (PBR). "
                                           "Unlit: faqat rang (x tekstura) va nurlanish - yorug'lik, soya va osmon ta'sir qilmaydi "
                                           "(UI belgilari, gologrammalar, ekranlar).");
        ImGui::PushID("##materialSurface");
        if (ImGui::Combo("##shader", &shader, shaders, 2))
        {
            m.Shader = (MaterialShader)shader;
            changed = true;
        }

        int blend = (int)m.BlendMode;
        const char* blends[] = { forModel ? "Modelning o'zi" : "Opaque (shaffof emas)", "Cutout (kesish)", "Transparent (shaffof)" };
        PropertyLabel("Aralashtirish", "Opaque: orqasini to'liq yopadi, alfa e'tiborga olinmaydi. "
                                       "Cutout: alfa chegaradan past piksellar chizilmaydi, qolgani shaffof emas - soyada ham teshiklar bo'ladi "
                                       "(barglar, panjaralar, o't). "
                                       "Transparent: alfa bo'yicha orqasidagi bilan aralashadi, orqadan oldinga saralanib chiziladi va soya "
                                       "tashlamaydi (shisha, suv, tutun).");
        if (ImGui::Combo("##blend", &blend, blends, 3))
        {
            m.BlendMode = (MaterialBlendMode)blend;
            changed = true;
        }
        if (m.BlendMode != MaterialBlendMode::Opaque)
        {
            PropertyLabel("Noshaffoflik", "Rangning alfa qiymati: tekstura alfasi bilan ko'paytiriladi. 0 - ko'rinmas, 1 - to'liq");
            changed |= ImGui::SliderFloat("##opacity", &m.Opacity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (m.BlendMode == MaterialBlendMode::Cutout)
        {
            PropertyLabel("Alfa chegarasi", "Alfasi (rang x tekstura) shundan past piksellar chizilmaydi va soya tashlamaydi");
            changed |= ImGui::SliderFloat("##cutoff", &m.AlphaCutoff, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (m.BlendMode == MaterialBlendMode::Transparent)
        {
            PropertyLabel("Aksni saqlash", "Yoqilgan: aks va yaltirashlar (hamda nurlanish) alfa bilan susaymaydi - faqat sirt rangi "
                                                 "shaffof bo'ladi (shisha, suv). O'chiq: butun sirt alfa bilan so'nadi (obyektni asta yo'qotish).");
            changed |= ImGui::Checkbox("##preserveSpecular", &m.PreserveSpecular);
        }
        // A model's parts keep their own blend mode ("Modelning o'zi"), and its own transparent parts
        // take a non-zero priority too (Scene::ComposeMeshMaterial) - so it is always shown there.
        if (m.BlendMode == MaterialBlendMode::Transparent || forModel)
        {
            PropertyLabel("Ustuvorlik", forModel ? "Modelning shaffof qismlari uchun saralash ustuvorligi (shaffof bo'lmagan qismlarga ta'sir "
                                                   "qilmaydi). Shaffof sirtlar avval ustuvorlik bo'yicha (kichigi oldin), keyin orqadan oldinga "
                                                   "(kameradan uzoqligi bo'yicha) chiziladi. Kattaroq ustuvorlik - boshqa shaffoflar ustida "
                                                   "chiziladi. 0 - modelning o'zi (o'zgartirilmaydi)."
                                                 : "Saralash ustuvorligi. Shaffof sirtlar avval ustuvorlik bo'yicha (kichigi oldin), keyin orqadan oldinga "
                                                   "(kameradan uzoqligi bo'yicha) chiziladi. Kattaroq ustuvorlik - boshqa shaffoflar ustida "
                                                   "chiziladi, qayerda bo'lishidan qat'i nazar (suv ichidagi shisha, dog'lar, effektlar). "
                                                   "Odatda 0.");
            changed |= ImGui::DragInt("##sortPriority", &m.SortPriority, 0.2f, MinMaterialSortPriority, MaxMaterialSortPriority, "%d",
                                      ImGuiSliderFlags_AlwaysClamp);
        }
        PropertyLabel("Ikki tomonlama", forModel ? "Yoqilgan: modelning barcha qismlari ikki tomonlama chiziladi (orqa tomoni teskari "
                                                   "normal bilan yoritiladi). O'chiq: har qism glTF faylidagi doubleSided bayrog'iga ko'ra."
                                                 : "Yoqilgan: sirtning ikkala tomoni chiziladi, orqa tomoni o'z tomonidan (teskari normal bilan) "
                                                   "yoritiladi - barglar, mato, yupqa devorlar. O'chiq: orqa tomonlar chizilmaydi (yopiq jismlar "
                                                   "uchun tezroq).");
        changed |= ImGui::Checkbox("##doubleSided", &m.DoubleSided);
        ImGui::PopID();
        return changed;
    }

    bool PropertyEmission(const char* label, glm::vec3& color, float& strength, const char* tooltip)
    {
        PropertyLabel(label, tooltip ? tooltip
                                     : "Sirt o'zi chiqaradigan yorug'lik: rang x kuch (nurlanish teksturasi bo'lsa, u bilan ham ko'paytiriladi). "
                                       "Rang chiziqli (linear). 1 dan katta natija HDR: sahnada bloom yoqilgan bo'lsa porlaydi. "
                                       "Yorug'lik, soya va tuman unga ta'sir qilmaydi.");
        ImGui::PushID(label);
        bool changed = ImGui::ColorEdit3("##color", &color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
        if (ImGui::IsItemHovered())
            Tooltip("Nurlanish rangi");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        glm::vec3 radiance = color * strength;
        float peak = std::max({ radiance.r, radiance.g, radiance.b });
        const char* badge = peak > 1.0f ? "HDR" : peak > 0.0f ? "LDR" : "o'chiq";
        float badgeWidth = ImGui::CalcTextSize("o'chiq").x + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
        ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - badgeWidth - ImGui::GetStyle().ItemInnerSpacing.x));
        changed |= ImGui::DragFloat("##strength", &strength, strength < 2.0f ? 0.01f : 0.05f, 0.0f, 100.0f, "x %.2f",
                                    ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
            Tooltip("Nurlanish kuchi (intensivlik): rang shunga ko'paytiriladi. 0 - nurlanish yo'q");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImVec4 badgeColor = peak > 1.0f ? ImVec4(1.0f, 0.72f, 0.3f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(badgeColor, "%s", badge);
        if (ImGui::IsItemHovered())
            Tooltip(peak > 1.0f ? "Chiqarilayotgan yorug'lik 1 dan katta (HDR): bloom yoqilgan sahnada atrofga nur sochadi"
                    : peak > 0.0f ? "Chiqarilayotgan yorug'lik 1 gacha: sirt yorqinroq ko'rinadi, lekin bloom bilan porlamaydi"
                                  : "Nurlanish yo'q (rang qora yoki kuch 0)");
        ImGui::PopID();
        return changed;
    }

    bool MaterialPresetMenuItem(const MaterialPreset& preset, const char* tooltipExtra)
    {
        ImGui::PushID(preset.Id.c_str());
        // A swatch of the color (the reflection's for metals, the glow's for emissive ones).
        const MaterialData& d = preset.Data;
        glm::vec3 glow = d.Emission * d.EmissionStrength;
        glm::vec3 swatch = std::max({ glow.r, glow.g, glow.b }) > 0.0f ? d.Emission : glm::vec3(d.Albedo);
        if (preset.Texture == MaterialPreset::Pattern::Wood)
            swatch = { 0.66f, 0.47f, 0.28f };
        else if (preset.Texture == MaterialPreset::Pattern::Concrete)
            swatch = { 0.52f, 0.51f, 0.49f };
        float h = ImGui::GetTextLineHeight();
        ImVec2 at = ImGui::GetCursorScreenPos();
        std::string label = "      " + preset.Name; // room for the swatch
        bool clicked = ImGui::MenuItem(label.c_str());
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 min(at.x + 1.0f, at.y + 1.0f), max(at.x + h - 1.0f, at.y + h - 1.0f);
        ImU32 color = ImGui::GetColorU32(ImVec4(swatch.r, swatch.g, swatch.b, 1.0f));
        if (d.BlendMode == MaterialBlendMode::Transparent)
        {
            draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(swatch.r, swatch.g, swatch.b, 0.25f)), 3.0f);
            draw->AddRect(min, max, color, 3.0f, 0, 1.5f);
        }
        else
            draw->AddRectFilled(min, max, color, 3.0f);
        if (d.Metallic > 0.5f) // a highlight on metals
            draw->AddCircleFilled(ImVec2(min.x + h * 0.32f, min.y + h * 0.32f), h * 0.14f, IM_COL32(255, 255, 255, 170));
        if (ImGui::IsItemHovered())
        {
            std::string text = preset.Description;
            if (tooltipExtra)
                text += std::string("\n") + tooltipExtra;
            Tooltip(text.c_str());
        }
        ImGui::PopID();
        return clicked;
    }

    bool PropertySegmented(const char* label, int& current, const char* const* items, int count, const char* const* tooltips)
    {
        PropertyLabel(label);
        ImGui::PushID(label);
        bool changed = false;
        const float spacing = 2.0f;
        const float width = std::max(1.0f, (ImGui::GetContentRegionAvail().x - spacing * (float)(count - 1)) / (float)std::max(1, count));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, ImGui::GetStyle().ItemSpacing.y));
        for (int i = 0; i < count; i++)
        {
            if (i > 0)
                ImGui::SameLine();
            const bool active = current == i;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, AccentU32(0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentU32(1.0f));
            }
            ImGui::PushID(i);
            if (ImGui::Button(items[i], ImVec2(width, 0.0f)) && !active)
            {
                current = i;
                changed = true;
            }
            if (tooltips && tooltips[i] && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltips[i]);
            ImGui::PopID();
            if (active)
                ImGui::PopStyleColor(2);
        }
        ImGui::PopStyleVar();
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
