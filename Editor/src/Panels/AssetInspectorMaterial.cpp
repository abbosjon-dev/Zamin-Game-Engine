// The material part of the asset inspector: a turning preview of the material and its editor.
#include "AssetInspectorPanel.h"
#include "../UI.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Zamin {

    namespace fs = std::filesystem;

    static const ImVec4 s_MaterialErrorColor(0.95f, 0.45f, 0.4f, 1.0f);
    static const ImVec4 s_MaterialWarnColor(0.95f, 0.75f, 0.35f, 1.0f);
    static const ImVec4 s_MaterialOkColor(0.5f, 0.85f, 0.6f, 1.0f);

    static constexpr const char* s_ImageExtensions = ".png;.jpg;.jpeg;.bmp;.tga";

    void AssetInspectorPanel::DrawMaterial()
    {
        if (!m_MaterialLoaded)
        {
            m_MaterialLoaded = true;
            // The library's object - the one the scene draws with - so edits show everywhere at once.
            m_Material = MaterialLibrary::Load(m_Path);
            if (m_Material)
            {
                // What the file holds is "saved"; the object may still hold edits that were not saved.
                MaterialData onDisk;
                m_MaterialSaved = MaterialSerializer::Deserialize(m_Path, onDisk) ? onDisk : m_Material->GetData();
                m_MaterialEdit = m_Material->GetData();
                m_MaterialRevision = m_Material->GetRevision();
            }
        }
        if (!m_Material)
        {
            ImGui::Spacing();
            ImGui::TextColored(s_MaterialErrorColor, ICON_FA_TRIANGLE_EXCLAMATION "  Material yuklanmadi - konsolni tekshiring");
            return;
        }
        SyncMaterial();

        ImGui::Spacing();
        DrawMaterialPreview();
        ImGui::Spacing();
        if (DrawMaterialFields())
            ApplyMaterialEdit();
        DrawMaterialSaveBar();
    }

    void AssetInspectorPanel::SyncMaterial()
    {
        if (!m_Material || m_Material->GetRevision() == m_MaterialRevision)
            return;
        // Changed elsewhere - its file was read again (edited outside the editor, version control):
        // that is what is saved now. Edits made here stay on top of it.
        bool edited = !(m_MaterialEdit == m_MaterialSaved);
        m_MaterialSaved = m_Material->GetData();
        if (!edited)
            m_MaterialEdit = m_MaterialSaved;
        else
            m_Material->SetData(m_MaterialEdit);
        m_MaterialRevision = m_Material->GetRevision();
    }

    void AssetInspectorPanel::ApplyMaterialEdit()
    {
        if (!m_Material)
            return;
        m_Material->SetData(m_MaterialEdit);
        m_MaterialRevision = m_Material->GetRevision();
    }

    bool AssetInspectorPanel::SaveMaterial()
    {
        if (!m_Material)
            return false;
        PendingEdits edits = CaptureEdits();
        edits.Type = AssetType::Material;
        bool saved = ApplyEdits(edits, true);
        if (saved)
            m_MaterialSaved = m_MaterialEdit;
        m_MaterialRevision = m_Material->GetRevision();
        return saved;
    }

    void AssetInspectorPanel::RevertMaterial()
    {
        if (!m_Material)
            return;
        m_MaterialEdit = m_MaterialSaved;
        ApplyMaterialEdit();
    }

    // ------------------------------------------------------------------ preview
    void AssetInspectorPanel::DrawMaterialPreview()
    {
        float dt = ImGui::GetIO().DeltaTime;
        if (m_MaterialAutoRotate && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_MaterialView.Yaw += dt * 0.5f;

        MaterialPreview::Settings settings = m_MaterialView;
        settings.Environment = nullptr;
        bool sceneLight = false;
        if (m_MaterialSceneLight && GetSceneLighting)
            sceneLight = GetSceneLighting(settings);

        float availX = ImGui::GetContentRegionAvail().x;
        uint32_t width = (uint32_t)std::clamp(availX, 64.0f, 1024.0f);
        uint32_t height = (uint32_t)std::clamp(availX * 0.62f, 96.0f, 300.0f);
        uint32_t texture = m_MaterialPreview.Render(m_Material->GetMaterial(), width, height, settings);
        if (!texture)
            return;

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(uintptr_t)texture, ImVec2((float)width, (float)height), ImVec2(0, 1), ImVec2(1, 0));
        // Drag: turn the object, wheel: zoom, double click: back to the start view.
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##materialView", ImVec2((float)width, (float)height));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_MaterialView.Yaw += delta.x * 0.01f;
            m_MaterialView.Pitch = std::clamp(m_MaterialView.Pitch + delta.y * 0.01f, -1.5f, 1.5f);
        }
        if (ImGui::IsItemHovered())
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
                m_MaterialView.Zoom = std::clamp(m_MaterialView.Zoom * std::pow(0.88f, wheel), 0.4f, 3.0f);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_MaterialView.Yaw = 0.5f;
                m_MaterialView.Pitch = 0.2f;
                m_MaterialView.Zoom = 1.0f;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                UI::Tooltip("Sudrang: aylantirish, g'ildirak: yaqinlashtirish, ikki marta bosing: boshlang'ich ko'rinish");
        }

        // Controls under the view: auto rotate, background, shape, lighting.
        auto toggle = [](const char* icon, bool& value, const char* tooltip) {
            bool on = value;
            if (on)
                ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentU32(0.6f));
            if (ImGui::Button(icon, ImVec2(ImGui::GetFrameHeight(), 0)))
                value = !value;
            if (on)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                UI::Tooltip(tooltip);
        };
        toggle(ICON_FA_ROTATE "##materialRotate", m_MaterialAutoRotate, "Avtomatik aylantirish");
        ImGui::SameLine();
        toggle(ICON_FA_IMAGE "##materialBackground", m_MaterialView.Background, "Orqa fon (osmon)");
        if (m_Material->GetMaterial().BlendMode != MaterialBlendMode::Opaque)
        {
            ImGui::SameLine();
            toggle(ICON_FA_CHESS_BOARD "##materialChecker", m_MaterialView.Checker,
                   "Shaxmat devor: obyekt orqasida - shaffof va kesilgan joylar yaxshi ko'rinadi");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * 0.4f, 70.0f, 110.0f));
        const char* shape = MaterialPreview::GetShapeName(m_MaterialView.PreviewShape);
        if (ImGui::BeginCombo("##materialShape", shape))
        {
            for (int i = 0; i <= (int)MaterialPreview::Shape::Cylinder; i++)
            {
                auto s = (MaterialPreview::Shape)i;
                if (ImGui::Selectable(MaterialPreview::GetShapeName(s), s == m_MaterialView.PreviewShape))
                    m_MaterialView.PreviewShape = s;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ko'rinish shakli");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char* lighting[] = { ICON_FA_SUN "  Osmon", ICON_FA_EARTH_ASIA "  Sahna HDRI" };
        int light = m_MaterialSceneLight ? 1 : 0;
        if (ImGui::Combo("##materialLight", &light, lighting, 2))
            m_MaterialSceneLight = light == 1;
        if (ImGui::IsItemHovered())
            UI::Tooltip(m_MaterialSceneLight && !sceneLight ? "Ochiq sahnada HDRI muhit xaritasi yo'q - osmon gradienti ishlatilmoqda"
                                                            : "Yoritish: osmon gradienti yoki ochiq sahnaning HDRI xaritasi (IBL)");
    }

    // ------------------------------------------------------------------ fields
    bool AssetInspectorPanel::DrawMaterialFields()
    {
        bool changed = false;
        MaterialData& d = m_MaterialEdit;
        const Material& live = m_Material->GetMaterial();
        auto texture = [&](const char* label, std::string& path, const Ref<Texture2D>& loaded, const char* tooltip) {
            UI::AssetPathOptions options;
            options.PreviewTexture = loaded ? loaded->GetRendererID() : 0;
            options.Missing = !path.empty() && !loaded;
            options.Tooltip = tooltip;
            changed |= UI::PropertyAssetPath(label, path, s_ImageExtensions, ICON_FA_FILE_IMAGE, options);
        };

        if (!UI::BeginPropertyGrid("##materialFields"))
            return false;

        UI::PropertyHeader("Sirt turi");
        changed |= UI::PropertyMaterialSurface(d);

        UI::PropertyHeader("Asosiy rang");
        changed |= UI::PropertyColor4("Rang (albedo)", d.Albedo);
        texture("Tekstura", d.AlbedoMap, live.AlbedoMap, "Asosiy rang teksturasi (rang bilan ko'paytiriladi)");

        if (d.Shader == MaterialShader::Lit)
        {
            UI::PropertyHeader("Sirt");
            changed |= UI::PropertySlider("Metallik", d.Metallic, 0.0f, 1.0f);
            changed |= UI::PropertySlider("G'adir-budurlik", d.Roughness, 0.0f, 1.0f);
            texture("Metal/Roughness", d.MetalRoughMap, live.MetalRoughMap,
                    "glTF tartibi: G kanal - g'adir-budurlik, B kanal - metallik (qiymatlar shu bilan ko'paytiriladi)");
            texture("Normal map", d.NormalMap, live.NormalMap, "Sirt notekisligi (tangent-space normal map)");
            if (!d.NormalMap.empty())
                changed |= UI::PropertySlider("Normal kuchi", d.NormalStrength, 0.0f, 3.0f);
        }

        UI::PropertyHeader("Nurlanish");
        changed |= UI::PropertyEmission("Nurlanish", d.Emission, d.EmissionStrength);
        bool hadEmissiveMap = !d.EmissiveMap.empty();
        texture("Nurlanish map", d.EmissiveMap, live.EmissiveMap, "Nurlanish rangi x kuch shu tekstura bilan ko'paytiriladi "
                                                                  "(nurlanish o'chiq bo'lsa, tekstura berilganda oq x 1 bo'ladi)");
        if (!hadEmissiveMap && !d.EmissiveMap.empty())
            EnableEmissionForMap(d); // else the new map would show nothing

        UI::PropertyHeader("UV");
        changed |= UI::PropertyFloat2("Takrorlash", d.Tiling, 0.05f);
        changed |= UI::PropertyFloat2("Siljish", d.Offset, 0.01f);
        UI::EndPropertyGrid();
        return changed;
    }

    void AssetInspectorPanel::DrawMaterialSaveBar()
    {
        bool dirty = HasUnsavedMaterial();
        ImGui::Spacing();
        ImGui::BeginDisabled(!dirty);
        if (dirty)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentU32(0.75f));
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Saqlash"))
            SaveMaterial();
        if (dirty)
            ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Bekor qilish"))
            RevertMaterial();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Faylda saqlangan qiymatlarga qaytarish");
        if (dirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(s_MaterialWarnColor, ICON_FA_CIRCLE "  saqlanmagan o'zgarishlar");
        }
        if (!m_Status.empty() && ImGui::GetTime() - m_StatusTime < 5.0)
            ImGui::TextColored(m_StatusError ? s_MaterialErrorColor : s_MaterialOkColor, "%s  %s",
                               m_StatusError ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_CHECK, m_Status.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(ICON_FA_CIRCLE_INFO "  O'zgarishlar sahnada darhol ko'rinadi (shu materialni ishlatadigan hamma obyektlarda); "
                           "faylga 'Saqlash' bilan yoziladi.");
        ImGui::PopStyleColor();
    }

}
