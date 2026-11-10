#include "SceneHierarchyPanel.h"
#include "Zamin/Utils/StringUtils.h"
#include "AssetInspectorPanel.h"
#include "AnimatorEditing.h"
#include "../UI.h"

#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Utils/Image.h>

#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <set>

#include "InspectorNotes.h"

namespace Zamin {
    // ------------------------------------------------------------------ inspector
    void SceneHierarchyPanel::DrawInspector()
    {
        m_Selection.Prune();
        Entity primary = GetSelectedEntity();
        if (!primary)
        {
            ImGui::Dummy(ImVec2(0, 40));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushFont(ImGuiLayer::GetLargeIconFont());
            UI::TextCentered(ICON_FA_ARROW_POINTER);
            ImGui::PopFont();
            UI::TextCentered("Obyekt tanlanmagan");
            UI::TextCentered("Ierarxiya yoki Viewport'da obyektni tanlang");
            ImGui::PopStyleColor();
            return;
        }

        if (m_Selection.Size() > 1)
        {
            // Multi-selection: the components shown are the primary entity's; the Transform
            // menu (reset / paste) and the hierarchy / viewport operations act on all of them.
            ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
            ImGui::TextUnformatted(ICON_FA_OBJECT_GROUP);
            ImGui::SameLine();
            ImGui::Text("%zu ta obyekt tanlangan", m_Selection.Size());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                std::string names;
                size_t shown = 0;
                for (Entity e : m_Selection.GetEntities())
                {
                    if (!e) continue;
                    if (shown++ == 12) { names += "\n..."; break; }
                    names += (names.empty() ? "" : "\n") + std::string(e == primary ? ICON_FA_CIRCLE_DOT " " : "   ") + e.GetName();
                }
                UI::Tooltip(names.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(asosiy: %s)", primary.GetName().c_str());
            ImGui::Separator();
        }
        DrawComponents(primary);
    }

    // Header names of the components (also used by the clipboard / move menu items).
    static const char* ComponentDisplayName(std::string_view key)
    {
        static const std::pair<std::string_view, const char*> s_Names[] = {
            { "Transform", "Transform" }, { "Tags", "Teglar" }, { "Camera", "Camera" },
            { "SpriteRenderer", "Sprite Renderer" }, { "SpriteAnimation", "Sprite Animation" },
            { "CircleRenderer", "Circle Renderer" }, { "Text", "Text" }, { "MeshRenderer", "Mesh Renderer" },
            { "Animator", "Animator" }, { "DirectionalLight", "Directional Light" }, { "PointLight", "Point Light" }, { "SpotLight", "Spot Light" },
            { "Rigidbody2D", "Rigidbody 2D" }, { "BoxCollider2D", "Box Collider 2D" }, { "CircleCollider2D", "Circle Collider 2D" },
            { "PolygonCollider2D", "Polygon Collider 2D" }, { "EdgeCollider2D", "Edge Collider 2D" }, { "PlatformEffector2D", "Platform Effector 2D" },
            { "AreaEffector2D", "Area Effector 2D" }, { "BuoyancyEffector2D", "Buoyancy Effector 2D" },
            { "DistanceJoint2D", "Distance Joint 2D" }, { "RevoluteJoint2D", "Revolute Joint 2D" }, { "PrismaticJoint2D", "Prismatic Joint 2D" },
            { "WeldJoint2D", "Weld Joint 2D" }, { "WheelJoint2D", "Wheel Joint 2D" },
            { "Rigidbody3D", "Rigidbody 3D" }, { "BoxCollider3D", "Box Collider 3D" }, { "SphereCollider3D", "Sphere Collider 3D" },
            { "CapsuleCollider3D", "Capsule Collider 3D" }, { "ConvexHullCollider3D", "Convex Hull Collider 3D" },
            { "MeshCollider3D", "Mesh Collider 3D" },
            { "FixedJoint3D", "Fixed Joint 3D" }, { "HingeJoint3D", "Hinge Joint 3D" }, { "DistanceJoint3D", "Distance Joint 3D" },
            { "SliderJoint3D", "Slider Joint 3D" }, { "ConeJoint3D", "Cone Joint 3D" }, { "CharacterController3D", "Character Controller 3D" },
            { "Tilemap", "Tilemap" }, { "Parallax", "Parallax" }, { "Camera2DFollow", "Camera 2D Follow" }, { "Spline", "Spline" }, { "SplineFollower", "Spline Follower" }, { "NavGrid2D", "NavGrid 2D" }, { "NavGrid3D", "NavGrid 3D" }, { "NavAgent", "Nav Agent" }, { "NavObstacle", "Nav Obstacle" }, { "SightSensor", "Sight Sensor" }, { "PatrolPath", "Patrol Path" }, { "Formation", "Formation" }, { "ParticleSystem", "Particle System" }, { "UITransform", "UI Transform" },
            { "UICanvas", "UI Canvas" }, { "UILayoutGroup", "UI Layout Group" }, { "UILayoutElement", "UI Layout Element" }, { "UINavigation", "UI Navigation" },
            { "UIButton", "UI Button" }, { "UIImage", "UI Image" }, { "UISlider", "UI Slider" }, { "UIToggle", "UI Toggle" },
            { "UIProgressBar", "UI Progress Bar" }, { "UIInputField", "UI Input Field" }, { "UIMask", "UI Mask" }, { "UIScrollView", "UI Scroll View" }, { "AudioSource", "Audio Source" }, { "AudioListener", "Audio Listener" }, { "AmbientZone", "Ambient Zone" },
            { "SubScene", "Sub-sahna" }, { "NativeScript", "C++ Skript" },
        };
        for (const auto& [k, name] : s_Names)
            if (k == key)
                return name;
        return "Komponent";
    }

    const char* SceneHierarchyPanel::GetComponentDisplayName(std::string_view key) { return ComponentDisplayName(key); }

    std::string SceneHierarchyPanel::FoldKey(Entity entity, std::string_view key)
    {
        // Every script class keeps its own state (a big PlayerController can stay folded).
        if (key == ComponentKey<NativeScriptComponent>())
            if (const auto* c = entity.TryGetComponent<NativeScriptComponent>(); c && !c->ClassName.empty())
                return std::string(key) + ":" + c->ClassName;
        return std::string(key);
    }

    std::vector<Entity> SceneHierarchyPanel::GetMenuTargets(Entity entity) const
    {
        std::vector<Entity> targets;
        if (IsSelected(entity))
            for (Entity e : m_Selection.GetEntities())
                if (e && e.GetScene() == m_Context.get())
                    targets.push_back(e);
        if (targets.empty())
            targets.push_back(entity);
        return targets;
    }

    void SceneHierarchyPanel::RefreshComponentClipboard()
    {
        // The system clipboard first (copied in another editor window), then our own copy.
        m_PasteText.clear();
        m_PasteKey.clear();
        const char* text = ImGui::GetClipboardText();
        std::string key = text ? SceneSerializer::GetSerializedComponentKey(text) : std::string();
        if (!key.empty())
        {
            m_PasteText = text;
            m_PasteKey = std::move(key);
            return;
        }
        key = SceneSerializer::GetSerializedComponentKey(m_ComponentClipboard);
        if (!key.empty())
        {
            m_PasteText = m_ComponentClipboard;
            m_PasteKey = std::move(key);
        }
    }

    size_t SceneHierarchyPanel::PasteComponent(Entity entity, bool addMissing)
    {
        if (m_PasteText.empty() || m_PasteKey.empty() || !m_SceneEditable || !entity)
            return 0;
        size_t count = 0;
        for (Entity e : GetMenuTargets(entity))
        {
            if (!addMissing && !ComponentRegistry::Has(e, m_PasteKey))
                continue;
            if (SceneSerializer::DeserializeComponent(m_PasteText, e))
            {
                m_Context->ResolveEntityAssets(e); // textures / meshes / fonts of the pasted values
                count++;
            }
        }
        if (count)
        {
            MarkModified();
            ZM_INFO("Komponent joylandi: {} ({} ta obyekt)", ComponentDisplayName(m_PasteKey), count);
        }
        else
            ZM_WARN("Komponentni joylab bo'lmadi: {}", ComponentDisplayName(m_PasteKey));
        return count;
    }

    bool SceneHierarchyPanel::DrawComponentMenuItems(Entity entity, std::string_view key)
    {
        bool changed = false;
        if (ImGui::IsWindowAppearing()) // read the clipboard once per opening
            RefreshComponentClipboard();
        const char* display = ComponentDisplayName(key);

        // ---- copy / paste values
        if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Komponentni nusxalash")))
        {
            m_ComponentClipboard = SceneSerializer::SerializeComponent(entity, key);
            if (!m_ComponentClipboard.empty())
            {
                ImGui::SetClipboardText(m_ComponentClipboard.c_str());
                m_PasteText = m_ComponentClipboard;
                m_PasteKey = std::string(key);
                ZM_INFO("Komponent nusxalandi: {} ({})", display, entity.GetName());
            }
        }
        size_t withComponent = 0;
        for (Entity e : GetMenuTargets(entity))
            withComponent += ComponentRegistry::Has(e, key) ? 1 : 0;
        const bool sameType = m_PasteKey == key;
        std::string pasteLabel = withComponent > 1 ? std::format(ICON_FA_PASTE "  Qiymatlarni joylash ({} ta obyekt)", withComponent)
                                                   : std::string(ICON_FA_PASTE "  Qiymatlarni joylash");
        if (ImGui::MenuItem(pasteLabel.c_str(), nullptr, false, sameType && m_SceneEditable))
            changed |= PasteComponent(entity, false) > 0;
        if (m_PasteKey.empty())
            ImGui::TextDisabled("      Bufer bo'sh");
        else if (!sameType)
            ImGui::TextDisabled("      Buferda boshqa komponent: %s", ComponentDisplayName(m_PasteKey));
        else if (!m_SceneEditable)
            ImGui::TextDisabled("      O'yin davomida joylab bo'lmaydi");

        // ---- order (Transform stays on top)
        ImGui::Separator();
        auto it = std::find(m_SectionOrder.begin(), m_SectionOrder.end(), key);
        const int index = it == m_SectionOrder.end() ? -1 : (int)(it - m_SectionOrder.begin());
        const int first = !m_SectionOrder.empty() && m_SectionOrder.front() == ComponentKey<TransformComponent>() ? 1 : 0;
        const int last = (int)m_SectionOrder.size() - 1;
        const bool canMove = m_SceneEditable && index >= first;
        if (ImGui::MenuItem(Tr(ICON_FA_ARROW_UP "  Yuqoriga ko'chirish"), nullptr, false, canMove && index > first))
            m_PendingMove = { std::string(key), {}, false, index - 1 };
        if (ImGui::MenuItem(Tr(ICON_FA_ARROW_DOWN "  Pastga ko'chirish"), nullptr, false, canMove && index < last))
            m_PendingMove = { std::string(key), {}, false, index + 1 };
        if (ImGui::MenuItem(Tr(ICON_FA_ANGLES_UP "  Eng yuqoriga"), nullptr, false, canMove && index > first))
            m_PendingMove = { std::string(key), {}, false, first };
        if (ImGui::MenuItem(Tr(ICON_FA_ANGLES_DOWN "  Eng pastga"), nullptr, false, canMove && index < last))
            m_PendingMove = { std::string(key), {}, false, -1 };
        return changed;
    }

    void SceneHierarchyPanel::DrawSectionDragDrop(std::string_view key)
    {
        // Drag a header onto another one: upper half = before it, lower half = after it.
        const bool isTransform = key == ComponentKey<TransformComponent>();
        if (!m_SceneEditable)
            return;
        if (!isTransform && ImGui::BeginDragDropSource())
        {
            std::string k(key);
            ImGui::SetDragDropPayload("INSPECTOR_COMPONENT", k.c_str(), k.size() + 1);
            ImGui::Text(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  %s", ComponentDisplayName(key));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* active = ImGui::GetDragDropPayload();
            if (active && active->IsDataType("INSPECTOR_COMPONENT") && std::string_view((const char*)active->Data) != key)
            {
                const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                const bool after = isTransform || ImGui::GetMousePos().y > (min.y + max.y) * 0.5f;
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INSPECTOR_COMPONENT",
                        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                {
                    const float y = after ? max.y + 1.0f : min.y - 1.0f;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), UI::AccentU32(), 2.5f);
                    dl->AddCircleFilled(ImVec2(min.x + 3.0f, y), 3.5f, UI::AccentU32());
                    if (payload->IsDelivery())
                        m_PendingMove = { std::string((const char*)payload->Data), std::string(key), after, -1 };
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // Queues the component's inspector section; sections are drawn later in the entity's order.
    // settingsMenu (optional): extra items of the component's menu (gear button / right-click
    // on the header); returns true when it changed something.
    template<typename T, typename UIFunction>
    void SceneHierarchyPanel::AddSection(const char* icon, const std::string& name, Entity entity, UIFunction uiFunction,
                                         bool removable, bool& modified, const std::function<bool(T&)>& settingsMenu)
    {
        if (!entity.HasComponent<T>())
            return;
        m_Sections.push_back({ ComponentKey<T>(), [this, icon, name, entity, uiFunction, removable, &modified, settingsMenu]() mutable {
            DrawSection<T>(icon, name, entity, uiFunction, removable, modified, settingsMenu);
        } });
    }

    template<typename T, typename UIFunction>
    void SceneHierarchyPanel::DrawSection(const char* icon, const std::string& name, Entity entity, UIFunction& uiFunction,
                                          bool removable, bool& modified, const std::function<bool(T&)>& settingsMenu)
    {
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (!entity.IsValid() || !entity.HasComponent<T>()) // removed by an earlier section (script callbacks)
            return;
        constexpr std::string_view key = ComponentKey<T>();
        ImVec2 available = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.155f, 0.18f, 1.0f));
        float lineHeight = ImGui::GetFontSize() + 12.0f;
        ImGui::Spacing();
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        std::string title = std::string(icon) + "   " + name;
        // Open / collapsed is remembered per component type (scripts per class), for every
        // entity and the next session (editor-view.json).
        const std::string foldKey = FoldKey(entity, key);
        const bool wasOpen = m_FoldState ? m_FoldState->IsOpen(foldKey, true) : true;
        if (m_FoldState)
            ImGui::SetNextItemOpen(wasOpen, ImGuiCond_Always);
        bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), flags, "%s", title.c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (m_FoldState && open != wasOpen && m_FoldState->SetOpen(foldKey, open, true) && OnFoldStateModified)
            OnFoldStateModified();
        DrawPrefabSectionMarks(entity, key, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        DrawSectionDragDrop(key);

        // Menu (gear button / right-click on the header): the component's own items, then copy /
        // paste values and move up / down, then remove.
        bool removeComponent = false;
        const std::string popupId = "ComponentSettings##" + std::string(key); // collapsed headers share one ID scope
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup(popupId.c_str());
        ImGui::SameLine(available.x - lineHeight * 0.75f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(ICON_FA_GEAR, ImVec2(lineHeight, lineHeight)))
            ImGui::OpenPopup(popupId.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Komponent menyusi: nusxalash / joylash, tartib, olib tashlash (sarlavhani sudrab ham tartiblash mumkin)");
        if (ImGui::BeginPopup(popupId.c_str()))
        {
            if (settingsMenu && settingsMenu(entity.GetComponent<T>()))
                modified = true;
            if (key != ComponentKey<TransformComponent>()) // Transform copies / pastes with its own menu
            {
                if (settingsMenu) ImGui::Separator();
                if (DrawComponentMenuItems(entity, key))
                    modified = true;
            }
            DrawPrefabComponentMenuItems(entity, key);
            if (removable)
            {
                ImGui::Separator();
                if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  Komponentni olib tashlash")))
                    removeComponent = true;
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            // Looked up again: a paste onto the other selected entities may have moved the pool.
            if (entity.IsValid() && entity.HasComponent<T>())
            {
                ImGui::Indent(4.0f);
                if (UI::BeginPropertyGrid(name.c_str()))
                {
                    // An entity of a prefab instance: its changed values are marked (UI::PropertyLabel).
                    UI::PrefabFieldScope prefabScope;
                    UI::PrefabFieldScope* scope = nullptr;
                    if (const auto* link = entity.TryGetComponent<PrefabLinkComponent>();
                        link && link->PrefabEntity && m_SceneEditable && m_PrefabInspector.Entity == entity.GetUUID())
                    {
                        prefabScope.Component = key;
                        prefabScope.Overrides = &link->Overrides;
                        prefabScope.Targets = &m_PrefabInspector.Targets;
                        prefabScope.OnAction = [this, entity](const std::string& path, int action) {
                            if (action == 0)
                                RequestPrefab(PrefabRequest::Op::Revert, entity, PrefabScope::Field, path);
                            else if ((size_t)(action - 1) < m_PrefabInspector.Targets.size())
                                RequestPrefab(PrefabRequest::Op::Apply, entity, PrefabScope::Field, path,
                                              action == 1 ? std::string() : m_PrefabInspector.Targets[(size_t)(action - 1)]);
                        };
                        scope = &prefabScope;
                    }
                    UI::PrefabFieldScopeGuard guard(scope);
                    modified |= uiFunction(entity.GetComponent<T>());
                    UI::EndPropertyGrid();
                }
                ImGui::Unindent(4.0f);
            }
            ImGui::TreePop();
        }

        if (removeComponent && entity.IsValid() && entity.HasComponent<T>())
        {
            entity.RemoveComponent<T>();
            modified = true;
        }
    }

    // One line about a material asset: its color, shader, blend mode and textures.
    static void DrawMaterialSummary(const Ref<MaterialAsset>& asset)
    {
        UI::PropertyLabel("");
        const MaterialData& d = asset->GetData();
        ImGui::ColorButton("##albedo", ImVec4(d.Albedo.r, d.Albedo.g, d.Albedo.b, d.Albedo.a),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
        ImGui::SameLine();
        size_t maps = d.GetTexturePaths().size();
        glm::vec3 radiance = d.Emission * d.EmissionStrength;
        float glow = std::max({ radiance.r, radiance.g, radiance.b });
        std::string text = std::format("{} · {}{} · metall {:.2f} · g'adir {:.2f}{}{}", MaterialSerializer::ShaderToString(d.Shader),
                                       MaterialSerializer::BlendModeToString(d.BlendMode), d.DoubleSided ? " · ikki tomonlama" : "",
                                       d.Metallic, d.Roughness, glow > 0.0f ? std::format(" · nurlanish x{:.2f}", d.EmissionStrength) : std::string(),
                                       maps ? std::format(" · {} tekstura", maps) : std::string());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            UI::Tooltip(Project::GetAssetRelativePath(asset->GetPath()).c_str());
    }

    bool SceneHierarchyPanel::DrawExtractModelMaterials(MeshRendererComponent& c)
    {
        if (!Project::GetActive() || !FileSystem::HasExtension(c.ModelPath, { ".gltf", ".glb" }))
            return false;
        UI::PropertyLabel("");
        bool clicked = ImGui::Button(Tr(ICON_FA_FILE_EXPORT "  Modeldan .zmat yaratib biriktirish"), ImVec2(-FLT_MIN, 0));
        if (ImGui::IsItemHovered())
            UI::Tooltip("Modelning glTF materiallari .zmat fayllar bo'lib yoziladi (model yonidagi <nom>_Materials papkasiga, "
                        "fayl ichidagi rasmlar bilan) va har bir qism o'z materialiga biriktiriladi. Oldin eksport qilinganlari "
                        "qayta yozilmaydi - ular ishlatiladi.");
        if (!clicked || !c.ModelAsset)
            return false;
        ModelMaterialExport result;
        std::string error;
        std::filesystem::path modelFile = Project::GetAssetFileSystemPath(c.ModelPath);
        if (!Model::ExportMaterials(modelFile, {}, result, &error))
        {
            ZM_ERROR("Model materiallari eksport qilinmadi: {} - {}", c.ModelPath, error);
            return false;
        }
        for (const std::string& warning : result.Warnings)
            ZM_WARN("{}: {}", c.ModelPath, warning);
        // One material for every part: the renderer's material asset; else a slot per part.
        const auto& subMeshes = c.ModelAsset->GetSubMeshes();
        std::vector<std::string> slots(subMeshes.size());
        std::set<int> used;
        for (size_t i = 0; i < subMeshes.size(); i++)
        {
            int index = subMeshes[i].MaterialIndex;
            if (index >= 0 && (size_t)index < result.Materials.size())
            {
                slots[i] = Project::GetAssetRelativePath(result.Materials[(size_t)index]);
                used.insert(index);
            }
        }
        if (used.size() == 1 && std::none_of(slots.begin(), slots.end(), [](const std::string& p) { return p.empty(); }))
        {
            c.SubmeshMaterials.clear();
            Scene::SetMeshMaterial(c, slots.front());
        }
        else
        {
            c.SubmeshMaterials = slots;
            Scene::SetMeshMaterial(c, c.MaterialPath); // trims empty slots, looks them up
        }
        ZM_INFO("Model materiallari biriktirildi: {} ({} ta material, {} ta qism)", c.ModelPath, result.Materials.size(), subMeshes.size());
        if (OnFilesCreated && !result.Written.empty())
            OnFilesCreated(result.Written);
        return true;
    }

    bool SceneHierarchyPanel::DrawStandardMaterialPicker(MeshRendererComponent& c)
    {
        if (!Project::GetActive())
            return false;
        UI::PropertyLabel("");
        if (ImGui::Button(Tr(ICON_FA_SWATCHBOOK "  Standart material..."), ImVec2(-FLT_MIN, 0)) || m_OpenStandardMaterials)
        {
            m_OpenStandardMaterials = false;
            ImGui::OpenPopup("##standardMaterials");
        }
        ImVec2 below(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Dvigatelning standart materiallari (metall, oltin, plastik, yog'och, shisha...). Tanlangani loyihada "
                        "Assets/Materials/Standart/ ichida bir marta yaratiladi va shu mesh'ga biriktiriladi - uni tanlagan "
                        "hamma obyektlar bitta materialni ishlatadi.");
        // Opens under the button, like a drop-down list. Set after the tooltip: the next Begin
        // (the tooltip's, on the click frame the button is still hovered) would use it up.
        ImGui::SetNextWindowPos(below, ImGuiCond_Appearing);
        bool changed = false;
        if (ImGui::BeginPopup("##standardMaterials"))
        {
            ImGui::TextDisabled("Standart materiallar");
            ImGui::Separator();
            for (const MaterialPreset& preset : MaterialPresets::GetAll())
            {
                if (!UI::MaterialPresetMenuItem(preset, "Assets/Materials/Standart/ dagi umumiy nusxasi biriktiriladi"))
                    continue;
                MaterialPresets::CreateResult result;
                std::string error;
                if (!MaterialPresets::Create(preset, MaterialPresets::GetSharedFolder(), true, result, &error))
                {
                    ZM_ERROR("Standart material '{}' yaratilmadi: {}", preset.Name, error);
                    continue;
                }
                if (!result.Existed)
                    ZM_INFO("Standart material yaratildi: {}", Project::GetAssetRelativePath(result.Material));
                if (OnFilesCreated && !result.Written.empty())
                    OnFilesCreated(result.Written);
                changed |= Scene::SetMeshMaterial(c, Project::GetAssetRelativePath(result.Material));
            }
            ImGui::EndPopup();
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawMeshMaterial(Entity entity, MeshRendererComponent& c)
    {
        bool changed = false;
        bool edit = false;
        UI::AssetPathOptions options;
        options.ActionIcon = ICON_FA_PEN;
        options.ActionTooltip = "Materialni tahrirlash (inspektorda ochiladi)";
        options.ActionClicked = &edit;
        options.Missing = !c.MaterialPath.empty() && !c.MaterialRef;
        if (c.MaterialRef && GetAssetThumbnail)
            options.PreviewTexture = GetAssetThumbnail(Project::GetAssetFileSystemPath(c.MaterialPath));
        options.PreviewTopRowFirst = true;
        options.Tooltip = "Material asset (.zmat). Tanlansa mesh (modelning barcha qismlari) shu material bilan chiziladi va "
                          "quyidagi ichki material ishlatilmaydi. Bitta material ko'p obyektda ishlatiladi: uni o'zgartirsangiz, hammasi o'zgaradi.";
        std::string materialPath = c.MaterialPath;
        if (UI::PropertyAssetPath("Material asset", materialPath, ".zmat", ICON_FA_PALETTE, options))
            changed |= Scene::SetMeshMaterial(c, materialPath);
        if (edit && OnAssetEditRequested)
            OnAssetEditRequested(Project::GetAssetFileSystemPath(c.MaterialPath));
        if (c.MaterialRef)
            DrawMaterialSummary(c.MaterialRef);
        changed |= DrawStandardMaterialPicker(c);
        if (Scene::HasMaterialInstances(c))
        {
            // Play mode: a script made its own copy (GetMaterial) - that is what is drawn.
            UI::PropertyLabel("");
            if (c.MaterialInstance)
            {
                const glm::vec4& color = c.MaterialInstance->GetData().Albedo;
                ImGui::ColorButton("##instanceColor", ImVec4(color.r, color.g, color.b, color.a),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf,
                                   ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                ImGui::SameLine();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.8f, 1.0f, 1.0f));
            ImGui::TextWrapped(ICON_FA_CODE "  Skriptning material nusxasi (instansiya) chizilmoqda");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                UI::Tooltip("Skript GetMaterial() bilan shu obyekt uchun materialning o'z nusxasini yaratgan: uning o'zgarishlari faqat shu "
                            "obyektda ko'rinadi, umumiy .zmat va boshqa obyektlar o'zgarmaydi. O'yin to'xtaganda nusxa yo'qoladi.");
        }

        // Models: a material asset per submesh (overrides the one above for that part).
        if (c.Type == PrimitiveMesh::Model && c.ModelAsset && !c.ModelAsset->GetSubMeshes().empty())
        {
            const auto& subMeshes = c.ModelAsset->GetSubMeshes();
            size_t overridden = std::count_if(c.SubmeshMaterials.begin(), c.SubmeshMaterials.end(), [](const std::string& p) { return !p.empty(); });
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool open = ImGui::TreeNodeEx(Tr("Materiallar###submeshMaterials"), ImGuiTreeNodeFlags_SpanAllColumns);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Qismlar materiallari: har bir submesh uchun alohida material asset. Bo'sh joy: yuqoridagi material asset, "
                            "u ham bo'lmasa modelning o'z materiali.");
            // The count in the value column (the label column is narrow).
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", std::format("{}/{} qismda asset", overridden, subMeshes.size()).c_str());
            if (open)
            {
                for (size_t i = 0; i < subMeshes.size(); i++)
                {
                    ImGui::PushID((int)i);
                    std::string path = i < c.SubmeshMaterials.size() ? c.SubmeshMaterials[i] : std::string();
                    std::string label = subMeshes[i].Name.empty() ? std::format("#{}", i) : std::format("#{} {}", i, subMeshes[i].Name);
                    bool editSlot = false;
                    UI::AssetPathOptions slotOptions;
                    slotOptions.ActionIcon = ICON_FA_PEN;
                    slotOptions.ActionTooltip = "Materialni tahrirlash";
                    slotOptions.ActionClicked = &editSlot;
                    slotOptions.Missing = !path.empty() && (i >= c.SubmeshMaterialRefs.size() || !c.SubmeshMaterialRefs[i]);
                    if (!path.empty() && !slotOptions.Missing && GetAssetThumbnail)
                        slotOptions.PreviewTexture = GetAssetThumbnail(Project::GetAssetFileSystemPath(path));
                    slotOptions.PreviewTopRowFirst = true;
                    if (UI::PropertyAssetPath(label.c_str(), path, ".zmat", ICON_FA_PALETTE, slotOptions))
                        changed |= Scene::SetMeshMaterial(c, path, (int)i);
                    if (editSlot && OnAssetEditRequested)
                        OnAssetEditRequested(Project::GetAssetFileSystemPath(path));
                    ImGui::PopID();
                }
                changed |= DrawExtractModelMaterials(c);
                ImGui::TreePop();
            }
        }

        if (!c.MaterialPath.empty())
        {
            UI::PropertyLabel("");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped(ICON_FA_CIRCLE_INFO "  Ichki material ishlatilmaydi: material asset tanlangan");
            ImGui::PopStyleColor();
            return changed;
        }

        // The inline material (no asset): the component's own values.
        UI::PropertyHeader("Ichki material", c.Type == PrimitiveMesh::Model
                                                 ? "Modelda ichki material tus bo'ladi: rangi modelning o'z ranglariga ko'paytiriladi, "
                                                   "teksturalari va rejimlari faqat tanlanganda modelnikini almashtiradi."
                                                 : nullptr);
        changed |= UI::PropertyMaterialSurface(c.Mat, c.Type == PrimitiveMesh::Model);
        changed |= UI::PropertyColor4("Rang (albedo)", c.Mat.Albedo);
        const char* imgExt = ".png;.jpg;.jpeg;.bmp;.tga";
        // Unlit primitives use neither lighting values nor their maps (a model's parts may be Lit).
        bool lit = c.Mat.Shader == MaterialShader::Lit || c.Type == PrimitiveMesh::Model;
        bool maps = UI::PropertyAssetPath("Tekstura", c.AlbedoPath, imgExt, ICON_FA_FILE_IMAGE);
        if (lit)
        {
            maps |= UI::PropertyAssetPath("Normal map", c.NormalPath, imgExt, ICON_FA_FILE_IMAGE);
            if (!c.NormalPath.empty())
                changed |= UI::PropertySlider("Normal kuchi", c.Mat.NormalStrength, 0.0f, 3.0f);
            maps |= UI::PropertyAssetPath("Metal/Roughness", c.MetalRoughPath, imgExt, ICON_FA_FILE_IMAGE);
        }
        bool hadEmissiveMap = !c.EmissivePath.empty();
        maps |= UI::PropertyAssetPath("Nurlanish map", c.EmissivePath, imgExt, ICON_FA_FILE_IMAGE);
        if (!hadEmissiveMap && !c.EmissivePath.empty())
            EnableEmissionForMap(c.Mat); // else the new map would show nothing
        if (maps)
        {
            Scene::ResolveMeshMaterials(c);
            changed = true;
        }
        changed |= UI::PropertyFloat2("Takrorlash", c.Mat.Tiling, 0.05f);
        changed |= UI::PropertyFloat2("Siljish", c.Mat.Offset, 0.01f);
        if (lit)
        {
            changed |= UI::PropertySlider("Metallik", c.Mat.Metallic, 0.0f, 1.0f);
            changed |= UI::PropertySlider("G'adir-budurlik", c.Mat.Roughness, 0.0f, 1.0f);
        }
        changed |= UI::PropertyEmission("Nurlanish", c.Mat.Emission, c.Mat.EmissionStrength);

        // Turns the inline material into a material asset the mesh then uses (and others can share).
        if (Project::GetActive() && c.Type != PrimitiveMesh::Model)
        {
            UI::PropertyLabel("");
            bool save = ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Asset qilib saqlash"), ImVec2(-FLT_MIN, 0));
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ichki materialni material asset (.zmat) sifatida Assets/Materials/ ga saqlaydi va mesh'ga biriktiradi - "
                            "keyin uni boshqa obyektlar ham ishlatishi mumkin.");
            if (save)
            {
                std::filesystem::path dir = Project::GetAssetDirectory() / "Materials";
                std::string name = entity.GetName().empty() ? std::string("Material") : entity.GetName();
                for (char& ch : name)
                    if (std::string_view("/\\:*?\"<>|").find(ch) != std::string_view::npos)
                        ch = '_';
                std::filesystem::path file = AssetFiles::MakeUniquePath(dir, name + ".zmat");
                if (MaterialSerializer::Serialize(Scene::GetInlineMaterialData(c), file))
                {
                    if (AssetRegistry* registry = Project::GetAssetRegistry())
                        registry->OnFileAdded(file); // a UUID at once (written next to the path in the scene)
                    c.MaterialPath = Project::GetAssetRelativePath(file);
                    Scene::ResolveMeshMaterials(c);
                    changed = true;
                    ZM_INFO("Material asset yaratildi: {}", c.MaterialPath);
                }
                else
                    ZM_ERROR("Material fayli yozilmadi: {}", file.string());
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Shu qiymatlar va teksturalar bilan Assets/Materials ichida .zmat fayl yaratiladi va mesh uni ishlatadi");
        }
        return changed;
    }

    template<typename T>
    void SceneHierarchyPanel::DisplayAddComponentEntry(const char* name, const char* icon)
    {
        Entity entity = GetSelectedEntity();
        if (!entity || entity.HasComponent<T>())
            return;
        if (!m_AddComponentFilter.empty() && StringUtils::ToLower(name).find(StringUtils::ToLower(m_AddComponentFilter)) == std::string::npos)
            return;
        std::string label = std::string(icon) + "   " + name;
        if (ImGui::MenuItem(label.c_str()))
        {
            T& c = entity.AddComponent<T>();
            if constexpr (std::is_same_v<T, TerrainComponent>)
            {
                c.Reset(65, { 64, 64 }, 8);
                if (!entity.HasComponent<Rigidbody3DComponent>()) entity.AddComponent<Rigidbody3DComponent>();
                entity.GetComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
                if (!entity.HasComponent<MeshCollider3DComponent>()) entity.AddComponent<MeshCollider3DComponent>();
                if (OnTerrainWindowRequested) OnTerrainWindowRequested();
            }
            if constexpr (std::is_same_v<T, MeshRendererComponent>)
                c.MeshAsset = MeshLibrary::GetPrimitive(c.Type);
            // 3D sounds start with the project's distances (Project settings > Audio).
            if constexpr (std::is_same_v<T, AudioSourceComponent> || std::is_same_v<T, FootstepsComponent>)
            {
                c.MinDistance = Audio::GetSpatialDefaults().MinDistance;
                c.MaxDistance = Audio::GetSpatialDefaults().MaxDistance;
            }
            if constexpr (std::is_same_v<T, TextComponent>)
                c.FontAsset = Font::GetDefault();
            // On a widget: a little hover / press feedback from the start.
            if constexpr (std::is_same_v<T, UIAnimationComponent>)
                if (entity.HasComponent<UIButtonComponent>() || entity.HasComponent<UIToggleComponent>())
                {
                    c.HoverScale = 1.05f;
                    c.PressScale = 0.94f;
                }
            // A UI widget lives in a UI rectangle: one is added with it (in the middle of the screen).
            constexpr bool widget = std::is_same_v<T, UIImageComponent> || std::is_same_v<T, UISliderComponent> ||
                                    std::is_same_v<T, UIToggleComponent> || std::is_same_v<T, UIProgressBarComponent> ||
                                    std::is_same_v<T, UIInputFieldComponent>;
            // A canvas covers its parent (the screen); a layout group is a panel.
            if constexpr (std::is_same_v<T, UICanvasComponent>)
                if (!entity.HasComponent<UITransformComponent>())
                    entity.AddComponent<UITransformComponent>().SetStretch();
            if constexpr (std::is_same_v<T, UILayoutGroupComponent> || std::is_same_v<T, UILayoutElementComponent> ||
                          std::is_same_v<T, UINavigationComponent> || std::is_same_v<T, UIAnimationComponent> ||
                          std::is_same_v<T, UIMaskComponent> || std::is_same_v<T, UIScrollViewComponent>)
                if (!entity.HasComponent<UITransformComponent>())
                {
                    auto& ui = entity.AddComponent<UITransformComponent>();
                    ui.SetAnchor(UITransformComponent::AnchorPoint::Center);
                    ui.Position = { 0.0f, 0.0f };
                    ui.Size = { 320.0f, 240.0f };
                }
            if constexpr (widget)
                if (!entity.HasComponent<UITransformComponent>())
                {
                    auto& ui = entity.AddComponent<UITransformComponent>();
                    ui.SetAnchor(UITransformComponent::AnchorPoint::Center);
                    ui.Position = { 0.0f, 0.0f };
                    if constexpr (std::is_same_v<T, UISliderComponent>) ui.Size = { 260.0f, 28.0f };
                    else if constexpr (std::is_same_v<T, UIToggleComponent>) ui.Size = { 260.0f, 32.0f };
                    else if constexpr (std::is_same_v<T, UIProgressBarComponent>) ui.Size = { 260.0f, 26.0f };
                    else if constexpr (std::is_same_v<T, UIInputFieldComponent>) ui.Size = { 300.0f, 44.0f };
                    else ui.Size = { 128.0f, 128.0f };
                }
            if constexpr (std::is_same_v<T, UIInputFieldComponent>)
                c.FontAsset = Font::GetDefault();
            MarkModified();
            ImGui::CloseCurrentPopup();
        }
    }

    const std::unordered_map<std::string, ScriptFieldValue>& SceneHierarchyPanel::GetScriptDefaults(const std::string& className)
    {
        auto it = m_ScriptDefaults.find(className);
        if (it != m_ScriptDefaults.end())
            return it->second;

        auto& defaults = m_ScriptDefaults[className];
        const ScriptClassInfo* info = ScriptRegistry::Find(className);
        if (info && info->Factory)
        {
            ScriptableEntity* tmp = info->Factory();
            for (const auto& f : info->Fields)
            {
                ScriptFieldValue v;
                f.Load(tmp, v);
                defaults[f.Name] = v;
            }
            delete tmp;
        }
        return defaults;
    }

    static void DrawWatchedBehaviorTree(const BehaviorTree& tree)
    {
        ImGui::PushID(&tree);
        UI::PropertyLabel(tree.Name.empty() ? "AI daraxti" : tree.Name.c_str());
        ImGui::TextColored(UI::AccentColor(), ICON_FA_DIAGRAM_PROJECT "  %s", tree.Describe().c_str());
        UI::PropertyLabel("");
        const std::string header = std::format("{} ({} {})###bt", Tr("Daraxt"), tree.GetNodeCount(), Tr("tugun"));
        if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen))
        {
            const uint32_t tick = tree.GetTickIndex();
            tree.ForEachNode([&](const BTNode& node, int depth) {
                const bool ticked = tick != 0 && node.GetLastTick() == tick;
                ImVec4 color;
                const char* icon;
                switch (node.GetStatus())
                {
                    case BTStatus::Running: color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); icon = ICON_FA_SPINNER; break;
                    case BTStatus::Success: color = ImVec4(0.45f, 0.9f, 0.5f, 1.0f); icon = ICON_FA_CIRCLE_CHECK; break;
                    case BTStatus::Failure: color = ImVec4(0.95f, 0.42f, 0.38f, 1.0f); icon = ICON_FA_CIRCLE_XMARK; break;
                    default: color = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]; icon = ICON_FA_CIRCLE; break;
                }
                if (!ticked)
                    color.w *= 0.45f; // an answer from an earlier tick
                ImGui::Indent(4.0f + (float)depth * 12.0f);
                ImGui::TextColored(color, "%s %s", icon, node.GetName().c_str());
                ImGui::SameLine();
                const std::string detail = node.GetDetail();
                ImGui::TextDisabled("%s%s%s", node.GetTypeName(), detail.empty() ? "" : "  ", detail.c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip(std::format("{}: {}\n{} {}, {} {}", Tr("Oxirgi javob"), BTStatusName(node.GetStatus()), node.GetTickCount(),
                                            Tr("marta tekshirildi"), node.GetStartCount(), Tr("marta boshlandi")).c_str());
                ImGui::Unindent(4.0f + (float)depth * 12.0f);
            });
            const Blackboard& board = tree.GetBlackboard();
            if (!board.Empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled(ICON_FA_CLIPBOARD "  %s", Tr("Blackboard"));
                for (const std::string& key : board.GetKeys())
                {
                    ImGui::Indent(16.0f);
                    ImGui::TextUnformatted(key.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("= %s", board.Describe(key).c_str());
                    ImGui::Unindent(16.0f);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneHierarchyPanel::DrawScriptComponent(Entity entity, bool& modified)
    {
        // Drawn after this function returns (AddSection): captures by value only.
        AddSection<NativeScriptComponent>(ICON_FA_CODE, "Skript (C++ / Visual)", entity, [this, entity](NativeScriptComponent& c) {
            bool changed = false;
            const auto& all = ScriptRegistry::GetAll();

            UI::PropertyLabel("Faol", "O'chirilgan skript OnEnable/OnStart/OnUpdate olmaydi (taymerlari to'xtaydi).\n"
                                      "Skriptda: SetEnabled(false), IsEnabled()");
            bool enabled = c.Enabled;
            if (ImGui::Checkbox("##enabled", &enabled))
            {
                if (c.Instance)
                {
                    // OnEnable / OnDisable run now and may change this entity's components
                    // (even destroy it): `c` is not touched again this frame.
                    entity.GetScene()->SetScriptEnabled(entity, enabled);
                    return changed;
                }
                else
                {
                    c.Enabled = enabled;
                    changed = true;
                }
            }
            if (c.Instance)
            {
                // What the script gets right now: an inactive entity's script is paused whatever
                // its own flag says (no OnUpdate, timers stopped).
                const bool entityActive = entity.IsActiveInHierarchy();
                ImGui::SameLine();
                ImGui::TextDisabled(!enabled ? "o'chirilgan"
                                    : !entityActive ? "to'xtab turibdi"
                                    : c.Instance->HasStarted() ? "ishlayapti" : "OnStart kutilmoqda");
            }
            InactiveComponentNote(entity, "Obyekt nofaol: skript OnUpdate olmaydi, taymerlari to'xtaydi (faollashganda OnEnable chaqiriladi)");

            if (c.ClassName == "Zamin.VisualGraph") {
                auto it = c.Fields.find("Graph");
                std::string path = it == c.Fields.end() ? "" : it->second.StringValue;
                ImGui::TextWrapped("Visual Script: %s", path.c_str());
                if (ImGui::Button("Visual Script muharririda ochish") && OnVisualGraphRequested)
                    OnVisualGraphRequested(Project::GetAssetFileSystemPath(path));
                return changed;
            }
            UI::PropertyLabel("Klass");
            bool exists = ScriptRegistry::Find(c.ClassName) != nullptr;
            if (!exists && !c.ClassName.empty())
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
            std::string preview = c.ClassName.empty() ? "(tanlang)" : c.ClassName;
            // Room for the "open the code" button after the class.
            const float openWidth = ImGui::GetFrameHeight();
            ImGui::SetNextItemWidth(-(openWidth + ImGui::GetStyle().ItemSpacing.x));
            if (c.Instance)
            {
                // The running instance is of one class: switching it would need a new instance.
                ImGui::BeginDisabled();
                if (ImGui::BeginCombo("##class", preview.c_str()))
                    ImGui::EndCombo();
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("O'yin davomida klassni almashtirib bo'lmaydi");
            }
            else if (ImGui::BeginCombo("##class", preview.c_str()))
            {
                if (all.empty())
                    ImGui::TextDisabled("Skriptlar yo'q. 'Skriptlar > Kompilyatsiya' ni bosing");
                for (const auto& [name, info] : all)
                {
                    if (ImGui::Selectable(name.c_str(), name == c.ClassName))
                    {
                        c.ClassName = name;
                        c.Fields.clear();
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (!exists && !c.ClassName.empty())
                ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::BeginDisabled(c.ClassName.empty() || !OnOpenScriptSource);
            if (ImGui::Button(ICON_FA_PEN_TO_SQUARE "##openScript", ImVec2(openWidth, openWidth)))
                OnOpenScriptSource(c.ClassName);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Klass kodini ichki Kod muharririda ochish (Source papkasidagi ZM_REGISTER_SCRIPT bo'yicha topiladi)");
            if (!exists && !c.ClassName.empty())
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Klass yuklanmagan");
            }

            if (c.Instance)
            {
                // Live editing while playing (not saved to the scene). The field accessors
                // must come from the instance's own class: code may have changed ClassName.
                const std::string& liveClass = c.Instance->GetScriptClassName();
                if (liveClass != c.ClassName)
                {
                    UI::PropertyLabel("Ishlayotgan klass");
                    ImGui::TextDisabled("%s", liveClass.c_str());
                }
                // AI state machines the script watches (WatchStateMachine): the state now, the history on hover.
                for (const StateMachine* machine : c.Instance->GetWatchedStateMachines())
                {
                    ImGui::PushID(machine);
                    UI::PropertyLabel(machine->Name.empty() ? "AI" : machine->Name.c_str());
                    ImGui::TextColored(UI::AccentColor(), ICON_FA_SITEMAP "  %s", machine->Describe().c_str());
                    if (!machine->GetPreviousStateName().empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("<- %s", machine->GetPreviousStateName().c_str());
                    }
                    if (ImGui::IsItemHovered() && !machine->GetHistory().empty())
                    {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(Tr("Oxirgi o'tishlar"));
                        for (auto it = machine->GetHistory().rbegin(); it != machine->GetHistory().rend(); ++it)
                            ImGui::TextDisabled("%6.1f s  %s -> %s  (%s)", it->Time, it->From.empty() ? "-" : it->From.c_str(), it->To.c_str(),
                                                it->Reason.c_str());
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
                for (const BehaviorTree* tree : c.Instance->GetWatchedBehaviorTrees())
                    DrawWatchedBehaviorTree(*tree);
                if (const ScriptClassInfo* live = ScriptRegistry::Find(liveClass))
                    for (const auto& f : live->Fields)
                    {
                        ScriptFieldValue value;
                        f.Load(c.Instance, value);
                        if (DrawScriptField(f, value))
                            f.Store(c.Instance, value);
                    }
                return changed;
            }

            const ScriptClassInfo* info = ScriptRegistry::Find(c.ClassName);
            if (!info)
                return changed;

            const auto& defaults = GetScriptDefaults(c.ClassName);
            for (const auto& f : info->Fields)
            {
                ScriptFieldValue value;
                auto it = c.Fields.find(f.Name);
                if (it == c.Fields.end() || !f.Convert(it->second, value))
                {
                    if (auto d = defaults.find(f.Name); d != defaults.end()) value = d->second;
                    else value.Type = f.Type;
                }
                if (DrawScriptField(f, value))
                {
                    c.Fields[f.Name] = value;
                    changed = true;
                }
            }
            return changed;
        }, true, modified);
    }

    static bool DrawAnimatorStateMachine(AnimatorComponent& c, const Ref<Model>& model, bool* isOpen, bool live,
                                         const std::function<void()>& openGraph)
    {
        bool changed = false;
        ImGui::PushID("asm");
        *isOpen = ImGui::CollapsingHeader(Tr(ICON_FA_DIAGRAM_PROJECT "  Animator holat mashinasi"));
        if (!*isOpen)
        {
            ImGui::PopID();
            return false;
        }
        ImGui::Indent(6.0f);
        if (ImGui::Button(Tr(ICON_FA_DIAGRAM_PROJECT "  Graf oynasida ochish")) && openGraph)
            openGraph();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Animator grafi: holatlar node'lar, o'tishlar strelkalar - sudrab joylashtirish, o'tish chizish, shartlar");
        if (c.States.empty())
            ImGui::TextDisabled("Holatlar yo'q - Animator Play() bilan qo'lda boshqariladi.");
        const Model* m = model.get();

        // ---- parameters
        ImGui::SeparatorText(live ? "Parametrlar (jonli)" : "Parametrlar");
        changed |= AnimatorEditing::DrawParameters(c, live);

        // ---- states: one collapsible row each
        ImGui::SeparatorText(Tr("Holatlar"));
        int removeState = -1;
        for (int i = 0; i < (int)c.States.size(); i++)
        {
            const AnimatorState& st = c.States[(size_t)i];
            ImGui::PushID(1000 + i);
            const bool current = c.CurrentState == i;
            std::string label = std::format("{}  {}{}{}###state", st.IsBlendTree() ? ICON_FA_SHUFFLE : ICON_FA_FILM, st.Name,
                                            c.DefaultState == i ? "   (boshlang'ich)" : "", current ? "   " ICON_FA_PLAY : "");
            if (current)
                ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
            const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding);
            if (current)
                ImGui::PopStyleColor();
            if (open)
            {
                changed |= AnimatorEditing::DrawState(c, i, m);
                if (ImGui::SmallButton(Tr(ICON_FA_TRASH "  Holatni o'chirish")))
                    removeState = i;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeState >= 0)
        {
            AnimatorEditing::RemoveState(c, removeState);
            changed = true;
        }
        if (ImGui::SmallButton(Tr(ICON_FA_PLUS "  Holat")))
        {
            AnimatorEditing::AddState(c, m, false, glm::vec2(40.0f + 30.0f * (float)c.States.size(), 40.0f + 60.0f * (float)c.States.size()));
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr(ICON_FA_SHUFFLE "  Blend tree")))
        {
            AnimatorEditing::AddState(c, m, true, glm::vec2(40.0f + 30.0f * (float)c.States.size(), 40.0f + 60.0f * (float)c.States.size()));
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("1D blend tree holati: float parametr bo'yicha klipllar aralashadi (masalan Speed: turish / yurish / yugurish)");

        // ---- transitions
        ImGui::SeparatorText(Tr("O'tishlar"));
        int removeTransition = -1;
        for (int i = 0; i < (int)c.Transitions.size(); i++)
        {
            const auto& tr = c.Transitions[(size_t)i];
            ImGui::PushID(2000 + i);
            std::string label = std::format("{}  " ICON_FA_ARROW_RIGHT "  {}   ({} shart{})###tr", AnimatorEditing::StateName(c, tr.From),
                                            AnimatorEditing::StateName(c, tr.To), tr.Conditions.size(), tr.HasExitTime ? ", exit time" : "");
            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding))
            {
                changed |= AnimatorEditing::DrawTransition(c, i);
                if (ImGui::SmallButton(Tr(ICON_FA_TRASH "  O'tishni o'chirish")))
                    removeTransition = i;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeTransition >= 0)
        {
            c.Transitions.erase(c.Transitions.begin() + removeTransition);
            changed = true;
        }
        if (!c.States.empty() && ImGui::SmallButton(Tr(ICON_FA_PLUS "  O'tish")))
        {
            c.Transitions.push_back({ -1, 0 });
            changed = true;
        }
        ImGui::Unindent(6.0f);
        ImGui::PopID();
        return changed;
    }

    void SceneHierarchyPanel::DrawComponents(Entity entity)
    {
        bool modified = false;

        // ---- header: name + add component
        {
            // Active checkbox first (like the script header's "Faol" box), then icon and name.
            DrawActiveToggle(entity);
            if (!entity.IsValid())
                return; // a script's OnDisable destroyed it
            // Fetched after the toggle: its OnDisable / OnEnable callbacks may destroy other
            // entities, and the component storage then moves tags around.
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            ImGui::SameLine();
            const bool inactive = !entity.GetScene()->IsActiveInHierarchy(entity);
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            ImGui::AlignTextToFramePadding();
            if (inactive)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(GetEntityIcon(entity));
            if (inactive)
                ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SameLine();
            const ImGuiStyle& style = ImGui::GetStyle();
            float addWidth = ImGui::CalcTextSize(ICON_FA_PLUS "  Komponent").x + style.FramePadding.x * 2;
            float foldWidth = ImGui::GetFrameHeight();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addWidth - foldWidth - style.ItemSpacing.x * 2);
            if (ImGui::InputText("##Tag", &tag))
            {
                entity.GetScene()->InvalidateNameCache(); // edited in place (FindEntity cache)
                modified = true;
            }

            // Collapse / expand every component header at once (remembered like single clicks).
            ImGui::SameLine();
            {
                std::vector<std::string> keys = ComponentRegistry::GetKeys(entity);
                bool anyOpen = false;
                for (const auto& key : keys)
                    anyOpen |= !m_FoldState || m_FoldState->IsOpen(FoldKey(entity, key), true);
                if (UI::ToolbarButton(anyOpen ? ICON_FA_SQUARE_MINUS : ICON_FA_SQUARE_PLUS,
                                      anyOpen ? "Hamma komponentlarni yig'ish" : "Hamma komponentlarni ochish", false, ImVec2(foldWidth, 0)) && m_FoldState)
                {
                    bool changed = false;
                    for (const auto& key : keys)
                        changed |= m_FoldState->SetOpen(FoldKey(entity, key), !anyOpen, true);
                    if (changed && OnFoldStateModified)
                        OnFoldStateModified();
                }
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::AccentMidColor());
            if (ImGui::Button(Tr(ICON_FA_PLUS "  Komponent")))
            {
                m_AddComponentFilter.clear();
                ImGui::OpenPopup("AddComponent");
            }
            ImGui::PopStyleColor(2);

            if (ImGui::BeginPopup("AddComponent"))
            {
                // A copied component (with its values) can be added to the selection directly.
                if (ImGui::IsWindowAppearing())
                    RefreshComponentClipboard();
                if (!m_PasteKey.empty())
                {
                    size_t targets = GetMenuTargets(entity).size();
                    std::string label = targets > 1 ? std::format(ICON_FA_PASTE "  Buferdan joylash: {} ({} ta obyekt)", ComponentDisplayName(m_PasteKey), targets)
                                                    : std::format(ICON_FA_PASTE "  Buferdan joylash: {}", ComponentDisplayName(m_PasteKey));
                    if (ImGui::MenuItem(label.c_str(), nullptr, false, m_SceneEditable))
                    {
                        PasteComponent(entity, true);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        UI::Tooltip(m_SceneEditable ? "Nusxalangan komponent qiymatlari bilan qo'shiladi (bor bo'lsa qiymatlari almashtiriladi)"
                                                    : "O'yin davomida joylab bo'lmaydi");
                    ImGui::Separator();
                }
                ImGui::SetNextItemWidth(220);
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::InputTextWithHint("##filter", Tr(ICON_FA_MAGNIFYING_GLASS "  Qidirish"), &m_AddComponentFilter);
                ImGui::SeparatorText(Tr("Render"));
                DisplayAddComponentEntry<SpriteRendererComponent>("Sprite Renderer", ICON_FA_IMAGE);
                DisplayAddComponentEntry<SpriteAnimationComponent>("Sprite Animation", ICON_FA_FILM);
                DisplayAddComponentEntry<TintAnimationComponent>("Tint Animation (rang)", ICON_FA_PALETTE);
                DisplayAddComponentEntry<CircleRendererComponent>("Circle Renderer", ICON_FA_CIRCLE);
                DisplayAddComponentEntry<TextComponent>("Text", ICON_FA_FONT);
                DisplayAddComponentEntry<TerrainComponent>("Terrain / map", ICON_FA_MOUNTAIN_SUN);
                DisplayAddComponentEntry<FoliageComponent>("Packed foliage", ICON_FA_TREE);
                DisplayAddComponentEntry<MeshRendererComponent>("Mesh Renderer", ICON_FA_CUBE);
                DisplayAddComponentEntry<AnimatorComponent>("Animator (skeletal)", ICON_FA_PERSON_RUNNING);
                DisplayAddComponentEntry<AnimationPlayerComponent>("Animation Player (.zanim kliplar)", ICON_FA_FILM);
                DisplayAddComponentEntry<BoneAttachmentComponent>("Bone Attachment (suyakka biriktirish)", ICON_FA_PAPERCLIP);
                DisplayAddComponentEntry<CameraComponent>("Camera", ICON_FA_CAMERA);
                ImGui::SeparatorText(Tr("Yorug'lik"));
                DisplayAddComponentEntry<DirectionalLightComponent>("Directional Light", ICON_FA_SUN);
                DisplayAddComponentEntry<PointLightComponent>("Point Light", ICON_FA_LIGHTBULB);
                DisplayAddComponentEntry<SpotLightComponent>("Spot Light", ICON_FA_BULLHORN);
                DisplayAddComponentEntry<Light2DComponent>("Light 2D", ICON_FA_FIRE);
                ImGui::SeparatorText(Tr("Fizika 2D"));
                DisplayAddComponentEntry<Rigidbody2DComponent>("Rigidbody 2D", ICON_FA_ATOM);
                DisplayAddComponentEntry<BoxCollider2DComponent>("Box Collider 2D", ICON_FA_VECTOR_SQUARE);
                DisplayAddComponentEntry<CircleCollider2DComponent>("Circle Collider 2D", ICON_FA_CIRCLE_NODES);
                DisplayAddComponentEntry<PolygonCollider2DComponent>("Polygon Collider 2D (ko'pburchak)", ICON_FA_DRAW_POLYGON);
                DisplayAddComponentEntry<EdgeCollider2DComponent>("Edge Collider 2D (chiziq/zanjir)", ICON_FA_CHART_LINE);
                DisplayAddComponentEntry<PlatformEffector2DComponent>("Platform Effector 2D (bir tomonlama)", ICON_FA_TURN_UP);
                DisplayAddComponentEntry<AreaEffector2DComponent>("Area Effector 2D (shamol)", ICON_FA_WIND);
                DisplayAddComponentEntry<BuoyancyEffector2DComponent>("Buoyancy Effector 2D (suv)", ICON_FA_WATER);
                DisplayAddComponentEntry<DistanceJoint2DComponent>("Distance Joint 2D (masofa)", ICON_FA_LINK);
                DisplayAddComponentEntry<RevoluteJoint2DComponent>("Revolute Joint 2D (sharnir)", ICON_FA_ARROWS_ROTATE);
                DisplayAddComponentEntry<PrismaticJoint2DComponent>("Prismatic Joint 2D (sirpanish)", ICON_FA_ARROWS_LEFT_RIGHT);
                DisplayAddComponentEntry<WeldJoint2DComponent>("Weld Joint 2D (payvand)", ICON_FA_HAMMER);
                DisplayAddComponentEntry<WheelJoint2DComponent>("Wheel Joint 2D (g'ildirak)", ICON_FA_GEAR);
                ImGui::SeparatorText(Tr("Fizika 3D"));
                DisplayAddComponentEntry<Rigidbody3DComponent>("Rigidbody 3D", ICON_FA_ATOM);
                DisplayAddComponentEntry<BoxCollider3DComponent>("Box Collider 3D", ICON_FA_CUBE);
                DisplayAddComponentEntry<SphereCollider3DComponent>("Sphere Collider 3D", ICON_FA_CIRCLE);
                DisplayAddComponentEntry<CapsuleCollider3DComponent>("Capsule Collider 3D", ICON_FA_CAPSULES);
                DisplayAddComponentEntry<ConvexHullCollider3DComponent>("Convex Hull Collider 3D (modeldan)", ICON_FA_SHAPES);
                DisplayAddComponentEntry<MeshCollider3DComponent>("Mesh Collider 3D (statik, modeldan)", ICON_FA_DRAW_POLYGON);
                DisplayAddComponentEntry<FixedJoint3DComponent>("Fixed Joint 3D (mahkam)", ICON_FA_LOCK);
                DisplayAddComponentEntry<HingeJoint3DComponent>("Hinge Joint 3D (sharnir)", ICON_FA_DOOR_OPEN);
                DisplayAddComponentEntry<DistanceJoint3DComponent>("Distance Joint 3D (masofa)", ICON_FA_LINK);
                DisplayAddComponentEntry<SliderJoint3DComponent>("Slider Joint 3D (sirpanish)", ICON_FA_ARROWS_LEFT_RIGHT);
                DisplayAddComponentEntry<ConeJoint3DComponent>("Cone Joint 3D (yelka, son: konus)", ICON_FA_BONE);
                DisplayAddComponentEntry<CharacterController3DComponent>("Character Controller 3D", ICON_FA_PERSON_RUNNING);
                ImGui::SeparatorText(Tr("Navigatsiya va AI"));
                DisplayAddComponentEntry<NavGrid2DComponent>("NavGrid 2D (tilemap / collider'lardan yo'l grid'i)", ICON_FA_BORDER_ALL);
                DisplayAddComponentEntry<NavGrid3DComponent>("NavGrid 3D (pol bo'yicha yo'l grid'i)", ICON_FA_TABLE_CELLS);
                DisplayAddComponentEntry<NavAgentComponent>("Nav Agent (yo'l topib yuradi)", ICON_FA_PERSON_WALKING);
                DisplayAddComponentEntry<NavObstacleComponent>("Nav Obstacle (harakatlanuvchi to'siq)", ICON_FA_CUBE);
                DisplayAddComponentEntry<SightSensorComponent>("Sight Sensor (ko'rish maydoni)", ICON_FA_EYE);
                DisplayAddComponentEntry<PatrolPathComponent>("Patrol Path (nuqtalar bo'ylab patrul)", ICON_FA_SHOE_PRINTS);
                DisplayAddComponentEntry<FormationComponent>("Formation (guruh shakli: qator, pona, doira)", ICON_FA_USERS);
                ImGui::SeparatorText(Tr("Effektlar va UI"));
                DisplayAddComponentEntry<TilemapComponent>("Tilemap", ICON_FA_TABLE_CELLS);
                DisplayAddComponentEntry<ParallaxComponent>("Parallax", ICON_FA_MOUNTAIN);
                DisplayAddComponentEntry<Camera2DFollowComponent>("Camera 2D Follow", ICON_FA_VIDEO);
                DisplayAddComponentEntry<SplineComponent>("Spline (yo'l: Catmull-Rom, Bezier)", ICON_FA_BEZIER_CURVE);
                DisplayAddComponentEntry<SplineFollowerComponent>("Spline Follower (yo'l bo'ylab harakat)", ICON_FA_ROUTE);
                DisplayAddComponentEntry<ParticleSystemComponent>("Particle System", ICON_FA_WAND_MAGIC_SPARKLES);
                DisplayAddComponentEntry<UITransformComponent>("UI Transform", ICON_FA_TABLE_CELLS);
                DisplayAddComponentEntry<UICanvasComponent>("UI Canvas (ekranga moslashuvchi masshtab)", ICON_FA_EXPAND);
                DisplayAddComponentEntry<UILayoutGroupComponent>("UI Layout Group (qator, ustun, panjara)", ICON_FA_LIST);
                DisplayAddComponentEntry<UILayoutElementComponent>("UI Layout Element (min / afzal o'lcham)", ICON_FA_ARROWS_LEFT_RIGHT);
                DisplayAddComponentEntry<UINavigationComponent>("UI Navigation (klaviatura / gamepad)", ICON_FA_GAMEPAD);
                DisplayAddComponentEntry<UIAnimationComponent>("UI Animation (ko'rinish, yashirish, bosilish)", ICON_FA_WAND_MAGIC_SPARKLES);
                DisplayAddComponentEntry<UIButtonComponent>("UI Button", ICON_FA_HAND_POINTER);
                DisplayAddComponentEntry<UIImageComponent>("UI Image (rasm, 9-slice, to'ldirish)", ICON_FA_IMAGE);
                DisplayAddComponentEntry<UISliderComponent>("UI Slider", ICON_FA_SLIDERS);
                DisplayAddComponentEntry<UIToggleComponent>("UI Toggle (belgilash)", ICON_FA_SQUARE_CHECK);
                DisplayAddComponentEntry<UIProgressBarComponent>("UI Progress Bar", ICON_FA_BARS_PROGRESS);
                DisplayAddComponentEntry<UIInputFieldComponent>("UI Input Field (matn maydoni)", ICON_FA_I_CURSOR);
                DisplayAddComponentEntry<UIMaskComponent>("UI Mask (bolalarini to'rtburchakda kesadi)", ICON_FA_CROP);
                DisplayAddComponentEntry<UIScrollViewComponent>("UI Scroll View (aylantiriladigan ro'yxat)", ICON_FA_ARROWS_UP_DOWN);
                ImGui::SeparatorText(Tr("Boshqa"));
                DisplayAddComponentEntry<AudioSourceComponent>("Audio Source", ICON_FA_VOLUME_HIGH);
                DisplayAddComponentEntry<AudioListenerComponent>("Audio Listener (tinglovchi)", ICON_FA_EAR_LISTEN);
                DisplayAddComponentEntry<AmbientZoneComponent>("Ambient Zone (hajmdagi fon ovozi)", ICON_FA_TREE);
                DisplayAddComponentEntry<FootstepsComponent>("Footsteps (animatsiya event'idan qadam tovushi)", ICON_FA_SHOE_PRINTS);
                DisplayAddComponentEntry<SubSceneComponent>("Sub-sahna (boshqa sahna fayli shu joyda)", ICON_FA_LAYER_GROUP);
                DisplayAddComponentEntry<NativeScriptComponent>("Script / Visual graph", ICON_FA_CODE);
                DisplayAddComponentEntry<TagsComponent>("Teglar (Tags)", ICON_FA_TAGS);
                ImGui::EndPopup();
            }
            ImGui::TextDisabled("ID: %llu", (unsigned long long)(uint64_t)entity.GetUUID());
            // Why the game leaves it out: switched off itself or by an ancestor.
            DrawInactiveNote(entity);
            // Why the viewport ignores it: the hierarchy's eye / lock (own or a parent's).
            if (entity.GetScene()->IsHiddenInEditor(entity))
            {
                ImGui::SameLine();
                ImGui::TextDisabled("  " ICON_FA_EYE_SLASH " yashirin");
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Editor sahnasida chizilmaydi (o'yinda ko'rinadi). Ierarxiyadagi ko'z tugmasi yoki H");
            }
            if (entity.GetScene()->IsLockedInEditor(entity))
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.96f, 0.72f, 0.32f, 1.0f), "  " ICON_FA_LOCK " qulflangan");
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Viewport'da tanlanmaydi va gizmo bilan surilmaydi; bu yerda tahrirlash mumkin");
            }
            // A prefab instance's entity: its prefab, what it changed, Apply / Revert.
            m_PrefabInspector = {};
            if (PrefabUtility::GetInstanceRoot(entity))
            {
                DrawPrefabBar(entity);
                m_PrefabInspector.Entity = entity.GetUUID();
                if (m_Prefabs && PrefabUtility::IsLinked(entity))
                    m_PrefabInspector.Targets = PrefabUtility::GetApplyTargets(entity, *m_Prefabs);
            }
        }

        // ---- transform
        AddSection<TransformComponent>(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Transform", entity, [](TransformComponent& c) {
            bool changed = UI::Vec3Control("Pozitsiya", c.Translation);
            glm::vec3 rotation = glm::degrees(c.Rotation);
            if (UI::Vec3Control("Aylanish", rotation, 0.0f, 0.5f))
            {
                c.Rotation = glm::radians(rotation);
                changed = true;
            }
            changed |= UI::Vec3Control("Masshtab", c.Scale, 1.0f);
            return changed;
        }, false, modified, [&](TransformComponent&) { return DrawTransformMenu(entity); });

        AddSection<TagsComponent>(ICON_FA_TAGS, "Teglar", entity, [&](TagsComponent& c) {
            return DrawTagsComponent(c);
        }, true, modified);

        AddSection<CameraComponent>(ICON_FA_CAMERA, "Camera", entity, [&](CameraComponent& c) {
            bool changed = false;
            auto& camera = c.Camera;
            changed |= UI::PropertyBool("Asosiy (Primary)", c.Primary);
            InactiveComponentNote(entity, c.Primary ? "Obyekt nofaol: asosiy kamera hisoblanmaydi" : "Obyekt nofaol: kamera ishlatilmaydi");

            const char* types[] = { "Perspektiv", "Ortografik" };
            int type = (int)camera.GetProjectionType();
            if (UI::PropertyCombo("Proyeksiya", type, types, 2))
            {
                camera.SetProjectionType((SceneCamera::ProjectionType)type);
                changed = true;
            }
            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
            {
                float fov = glm::degrees(camera.GetPerspectiveVerticalFOV());
                if (UI::PropertySlider("Ko'rish burchagi", fov, 10.0f, 120.0f)) { camera.SetPerspectiveVerticalFOV(glm::radians(fov)); changed = true; }
                float n = camera.GetPerspectiveNearClip();
                if (UI::PropertyFloat("Yaqin", n, 0.01f, 0.001f, 100.0f)) { camera.SetPerspectiveNearClip(n); changed = true; }
                float f = camera.GetPerspectiveFarClip();
                if (UI::PropertyFloat("Uzoq", f, 1.0f, 1.0f, 100000.0f)) { camera.SetPerspectiveFarClip(f); changed = true; }
            }
            else
            {
                float size = camera.GetOrthographicSize();
                if (UI::PropertyFloat("O'lcham", size, 0.1f, 0.1f, 1000.0f)) { camera.SetOrthographicSize(size); changed = true; }
                float n = camera.GetOrthographicNearClip();
                if (UI::PropertyFloat("Yaqin", n, 0.1f)) { camera.SetOrthographicNearClip(n); changed = true; }
                float f = camera.GetOrthographicFarClip();
                if (UI::PropertyFloat("Uzoq", f, 0.1f)) { camera.SetOrthographicFarClip(f); changed = true; }
            }
            changed |= UI::PropertyBool("Qat'iy nisbat", c.FixedAspectRatio);
            changed |= UI::PropertyColor4("Fon rangi", c.ClearColor);
            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
                changed |= DrawPixelPerfect(c);

            UI::PropertyHeader(ICON_FA_BURST "  Silkinish",
                               "Skriptdan: ShakeCamera(kuch, davomiylik, chastota). Faqat tasvir silkinadi - kameraning joyi, "
                               "kuzatish, fizika va skriptlar o'zgarmaydi");
            UI::PropertyLabel("Maks. siljish", "To'liq silkinishda (kuch 1) kamera ko'rinishi o'ngga/yuqoriga qancha siljiydi (birlik)");
            if (ImGui::DragFloat2("##shakeOffset", &c.Shake.MaxOffset.x, 0.01f, 0.0f, 100.0f, "%.2f"))
            {
                c.Shake.MaxOffset = glm::max(c.Shake.MaxOffset, glm::vec2(0.0f));
                changed = true;
            }
            UI::PropertyLabel("Maks. burchak", "To'liq silkinishda kamera qancha og'adi (gradus)");
            if (ImGui::DragFloat("##shakeAngle", &c.Shake.MaxAngle, 0.05f, 0.0f, 45.0f, "%.1f\xc2\xb0"))
            {
                c.Shake.MaxAngle = std::clamp(c.Shake.MaxAngle, 0.0f, 45.0f);
                changed = true;
            }
            UI::PropertyLabel("Daraja", "Silkinish = kuch ^ daraja. 2: kichik zarbalar yengil, kattalari kuchli; 1: chiziqli");
            if (ImGui::SliderFloat("##shakeExponent", &c.Shake.Exponent, 0.5f, 4.0f, "%.2f"))
            {
                c.Shake.Exponent = std::clamp(c.Shake.Exponent, 0.1f, 8.0f);
                changed = true;
            }
            UI::PropertyLabel("");
            const bool running = m_Context && m_Context->IsRunning();
            ImGui::BeginDisabled(!running);
            if (ImGui::Button(Tr(ICON_FA_BURST "  Sinash")))
                m_Context->ShakeCamera(entity, 0.7f, 0.6f, 20.0f);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip(running ? "Kuch 0.7, 0.6 soniya" : "O'yin rejimida sinab ko'ring");
            if (running && c.Shake.IsActive())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("kuch %.2f, %.2f s", c.Shake.Trauma, c.Shake.GetRemaining());
            }
            return changed;
        }, true, modified);

        AddSection<Camera2DFollowComponent>(ICON_FA_VIDEO, "Camera 2D Follow", entity, [&](Camera2DFollowComponent& c) {
            return DrawCamera2DFollow(entity, c);
        }, true, modified);

        AddSection<SplineComponent>(ICON_FA_BEZIER_CURVE, "Spline", entity, [&](SplineComponent& c) {
            return DrawSpline(entity, c);
        }, true, modified);

        AddSection<SplineFollowerComponent>(ICON_FA_ROUTE, "Spline Follower", entity, [&](SplineFollowerComponent& c) {
            return DrawSplineFollower(entity, c);
        }, true, modified);

        AddSection<NavGrid2DComponent>(ICON_FA_BORDER_ALL, "NavGrid 2D", entity, [&](NavGrid2DComponent& c) {
            return DrawNavGrid2D(entity, c);
        }, true, modified);

        AddSection<NavGrid3DComponent>(ICON_FA_TABLE_CELLS, "NavGrid 3D", entity, [&](NavGrid3DComponent& c) {
            return DrawNavGrid3D(entity, c);
        }, true, modified);

        AddSection<NavAgentComponent>(ICON_FA_PERSON_WALKING, "Nav Agent", entity, [&](NavAgentComponent& c) {
            return DrawNavAgent(entity, c);
        }, true, modified);

        AddSection<NavObstacleComponent>(ICON_FA_CUBE, "Nav Obstacle", entity, [&](NavObstacleComponent& c) {
            return DrawNavObstacle(entity, c);
        }, true, modified);

        AddSection<SightSensorComponent>(ICON_FA_EYE, "Sight Sensor", entity, [&](SightSensorComponent& c) {
            return DrawSightSensor(entity, c);
        }, true, modified);

        AddSection<PatrolPathComponent>(ICON_FA_SHOE_PRINTS, "Patrol Path", entity, [&](PatrolPathComponent& c) {
            return DrawPatrolPath(entity, c);
        }, true, modified);

        AddSection<FormationComponent>(ICON_FA_USERS, "Formation", entity, [&](FormationComponent& c) {
            return DrawFormation(entity, c);
        }, true, modified);

        AddSection<SpriteRendererComponent>(ICON_FA_IMAGE, "Sprite Renderer", entity, [&](SpriteRendererComponent& c) {
            return DrawSpriteRenderer(entity, c);
        }, true, modified);

        AddSection<SpriteAnimationComponent>(ICON_FA_FILM, "Sprite Animation", entity, [&](SpriteAnimationComponent& c) {
            return DrawSpriteAnimation(entity, c);
        }, true, modified);

        AddSection<TintAnimationComponent>(ICON_FA_PALETTE, "Tint Animation", entity, [&](TintAnimationComponent& c) {
            return DrawTintAnimation(entity, c);
        }, true, modified);

        AddSection<CircleRendererComponent>(ICON_FA_CIRCLE, "Circle Renderer", entity, [&](CircleRendererComponent& c) {
            bool changed = UI::PropertyColor4("Rang", c.Color);
            changed |= UI::PropertySlider("Qalinlik", c.Thickness, 0.0f, 1.0f);
            changed |= UI::PropertyFloat("Yumshoqlik", c.Fade, 0.00025f, 0.0f, 1.0f);
            changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);
            return changed;
        }, true, modified);

        AddSection<TextComponent>(ICON_FA_FONT, "Text", entity, [&](TextComponent& c) {
            bool changed = UI::PropertyString("Matn", c.Text, true);
            changed |= DrawLocalizationKey(c);
            changed |= UI::PropertyBool("Boshqaruv ishoralari", c.InputHints);
            ImGui::SetItemTooltip("%s", Tr("Matndagi [Nom] - loyiha input xaritasidagi harakat, o'q yoki vektor - o'yinchi ishlatayotgan "
                                           "qurilmaning tugmalari bilan ko'rinadi: klaviaturada \"Space\", gamepad'da \"A\""));
            changed |= UI::PropertyColor4("Rang", c.Color);
            changed |= UI::PropertyFloat("Harf oralig'i", c.Kerning, 0.005f);
            changed |= UI::PropertyFloat("Qator oralig'i", c.LineSpacing, 0.01f);
            // Alignment: in the UI rectangle, or around the origin in the world.
            const bool ui = entity.HasComponent<UITransformComponent>();
            {
                const char* items[] = { ICON_FA_ALIGN_LEFT, ICON_FA_ALIGN_CENTER, ICON_FA_ALIGN_RIGHT };
                const char* tips[] = { "Chapga", "Markazga", "O'ngga" };
                int h = (int)c.AlignH;
                if (UI::PropertySegmented("Gorizontal", h, items, 3, tips))
                {
                    c.AlignH = (TextAlignH)h;
                    changed = true;
                }
            }
            {
                // Short labels: four of them share the value column (the tooltips say it in full).
                const char* items[] = { "Tepa", "O'rta", "Past", "Asos" };
                const char* tips[] = { "Yuqoriga: matn bloki yuqori chetga (dunyoda: boshi nuqtadan pastga)", "O'rtaga: vertikal markazga",
                                       "Pastga: pastki chetga",
                                       "Tayanch chiziq: birinchi qatorning tayanch chizig'i obyekt nuqtasida (dunyo matni; UI'da - yuqori kabi)" };
                int v = (int)c.AlignV;
                if (UI::PropertySegmented("Vertikal", v, items, ui ? 3 : 4, tips))
                {
                    c.AlignV = (TextAlignV)v;
                    changed = true;
                }
            }
            if (!ui)
                changed |= UI::PropertyFloat("Kenglik (o'rash)", c.MaxWidth, 0.05f, 0.0f, 1.0e6f);
            if (ui || c.MaxWidth > 0.0f)
            {
                changed |= UI::PropertyBool("O'rash", c.WordWrap);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(ui ? "So'z bo'yicha o'rash: qator to'rtburchak kengligiga sig'maganda keyingi qatorga o'tadi"
                                   : "So'z bo'yicha o'rash: qator kenglikka sig'maganda keyingi qatorga o'tadi");
                const char* overflows[] = { "To'liq", "Kesish", "..." };
                const char* tips[] = { "Sig'magan matn chegaradan tashqarida ham chiziladi", "Sig'magan belgilar va qatorlar chizilmaydi",
                                       "Kesilgan joyga \"...\" (uch nuqta) qo'yiladi" };
                int o = (int)c.Overflow;
                if (UI::PropertySegmented("Sig'maganda", o, overflows, 3, tips))
                {
                    c.Overflow = (TextOverflow)o;
                    changed = true;
                }
                if (ui && m_Context && (c.WordWrap || c.Overflow != TextOverflow::Overflow))
                {
                    // How it came out in the rectangle now.
                    const UIRect rect = m_Context->GetUIRect(entity);
                    const auto& t = entity.GetComponent<UITransformComponent>();
                    const float px = std::max(1.0f, t.FontSize > 0.0f ? t.FontSize : rect.GetHeight());
                    const TextLayoutResult layout = c.Layout(rect, px);
                    UI::PropertyLabel("Natija");
                    ImGui::TextDisabled("%zu qator%s", layout.Lines.size(), layout.Truncated ? ", kesilgan" : "");
                }
            }
            if (UI::PropertyAssetPath("Shrift", c.FontPath, ".ttf;.otf", ICON_FA_FONT))
            {
                c.FontAsset = c.FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(c.FontPath));
                changed = true;
            }
            changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);
            return changed;
        }, true, modified);

        AddSection<TerrainComponent>(ICON_FA_MOUNTAIN_SUN, "Terrain", entity, [&](TerrainComponent& c) {
            ImGui::Text("%u x %u | %.0f x %.0f m", c.Resolution, c.Resolution, c.Size.x, c.Size.y);
            bool changed = ImGui::Checkbox("Collision", &c.Collision);
            if (ImGui::Button("Map tayyorlash oynasi") && OnTerrainWindowRequested)
                OnTerrainWindowRequested();
            return changed;
        }, true, modified);

        AddSection<FoliageComponent>(ICON_FA_TREE,"Packed foliage",entity,[&](FoliageComponent& c){
            bool changed=false;const char* types[]={"Kub","Shar","Tekislik","Silindr","Konus","Model"};
            int near=int(c.NearType),far=int(c.FarType),backend=int(c.Settings.Backend);
            if(ImGui::Combo("Yaqin LOD",&near,types,6)){c.NearType=PrimitiveMesh(near);changed=true;}
            if(c.NearType==PrimitiveMesh::Model)changed|=UI::PropertyAssetPath("Yaqin model",c.NearModel,".gltf;.glb;.obj",ICON_FA_CUBES);
            if(ImGui::Combo("Uzoq LOD",&far,types,6)){c.FarType=PrimitiveMesh(far);changed=true;}
            if(c.FarType==PrimitiveMesh::Model)changed|=UI::PropertyAssetPath("Uzoq model",c.FarModel,".gltf;.glb;.obj",ICON_FA_CUBES);
            changed|=ImGui::ColorEdit4("Rang",&c.Tint.x);changed|=ImGui::DragFloat("LOD masofasi",&c.Settings.LODDistance,1,1,100000);
            changed|=ImGui::DragFloat("Ko'rish masofasi",&c.Settings.CullDistance,1,c.Settings.LODDistance,100000);
            changed|=ImGui::SliderFloat("LOD hysteresis",&c.Settings.Hysteresis,0,.4f);
            const char* backends[]={"Auto (GPU / CPU)","CPU","GPU (fallback bilan)"};if(ImGui::Combo("Culling",&backend,backends,3)){c.Settings.Backend=FoliageBackend(backend);changed=true;}
            changed|=ImGui::Checkbox("Soya",&c.CastShadows);
            static int count=4096;static glm::vec2 size{128,128};ImGui::InputInt("Yangi instance soni",&count);ImGui::DragFloat2("Maydon (m)",&size.x,1,1,10000);
            if(ImGui::Button("Yangi grid yaratish")){FoliageTools::GenerateGrid(c,uint32_t(std::clamp(count,0,int(FoliageBatch::MaxInstances))),size);changed=true;}
            if(ImGui::Button("Parent terrain sirtiga joylash"))
            {
                auto parent=entity.GetScene()->GetEntityByUUID(entity.GetComponent<RelationshipComponent>().Parent);
                if(parent&&parent.HasComponent<TerrainComponent>())changed |= FoliageTools::ProjectToTerrain(c,parent.GetComponent<TerrainComponent>(),entity.GetTransform().GetTransform())>0;
            }
            ImGui::Text("%zu / %u instance",c.Instances.size(),FoliageBatch::MaxInstances);
            if(c.Runtime){auto batch=FoliageTools::Prepare(c,entity.GetScene()->GetWorldTransform(entity),int(uint32_t(entt::entity(entity))));if(batch){const auto& stats=batch->GetStats();ImGui::Text("%s culling | uploads %llu",stats.GPU?"GPU":"CPU",(unsigned long long)stats.Uploads);if(stats.CountsAvailable)ImGui::Text("Yaqin %u | uzoq %u | kesilgan %u",stats.Near,stats.Far,stats.Culled);else ImGui::TextDisabled("GPU hisoblari kutilmoqda");ImGui::TextDisabled("CPU %llu KiB | GPU %llu KiB",(unsigned long long)(stats.CPUBytes/1024),(unsigned long long)(stats.GPUBytes/1024));if(!stats.Error.empty())ImGui::TextWrapped("%s",stats.Error.c_str());}}
            ImGui::TextWrapped("Bitta packed komponent tanlanadi. Statik opaque/cutout meshlar. Soya konservativ yaqin LOD bilan.");
            if(changed)c.Sanitize();
            return changed;
        },true,modified);

        AddSection<MeshRendererComponent>(ICON_FA_CUBE, "Mesh Renderer", entity, [&](MeshRendererComponent& c) {
            bool changed = false;
            const char* meshes[] = { "Kub", "Shar", "Tekislik", "Silindr", "Konus", "Model (glTF / OBJ)" };
            int type = (int)c.Type;
            if (UI::PropertyCombo("Mesh", type, meshes, 6))
            {
                c.Type = (PrimitiveMesh)type;
                Scene::ResolveMeshRenderer(c);
                changed = true;
            }
            if (c.Type == PrimitiveMesh::Model)
            {
                if (UI::PropertyAssetPath("Model fayli", c.ModelPath, ".gltf;.glb;.obj", ICON_FA_CUBES))
                {
                    Scene::ResolveMeshRenderer(c);
                    changed = true;
                }
                if (c.ModelAsset)
                {
                    UI::PropertyLabel("Qismlar");
                    ImGui::Text("%zu mesh", c.ModelAsset->GetSubMeshes().size());
                }
                // ---- morph targets (blend shapes): a weight per target
                if (c.ModelAsset && c.ModelAsset->HasMorphTargets())
                {
                    const Model& model = *c.ModelAsset;
                    const auto* anim = entity.TryGetComponent<AnimatorComponent>();
                    const bool animated = anim && anim->MorphWeights.size() == model.GetMorphWeightCount() &&
                                          (!m_SceneEditable || anim->EditorPreview.Active || (anim->PlayInEditor && anim->Playing));
                    UI::PropertyHeader("Blend shape'lar",
                                       "glTF morph target'lari: har biri 0..1 og'irlik bilan mesh shaklini o'zgartiradi (yuz ifodalari, "
                                       "shishish). Saqlanadi; Animator kliplari og'irliklarni animatsiya qilsa, o'ynayotganda ular "
                                       "ishlatiladi. Skript: GetComponent<MeshRendererComponent>().SetMorphWeight(\"nom\", 0.8f)");
                    for (uint32_t i = 0; i < model.GetMorphWeightCount(); i++)
                    {
                        ImGui::PushID((int)i + 7700);
                        const std::string name = model.GetMorphTargetName(i);
                        UI::PropertyLabel(name.c_str(), animated ? "Animator klipi hozir boshqarmoqda (o'ng tomonda: amaldagi qiymat)" : nullptr);
                        float w = c.GetMorphWeight(i);
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (animated ? 56.0f : 0.0f));
                        if (ImGui::SliderFloat("##w", &w, 0.0f, 1.0f, "%.2f"))
                        {
                            c.SetMorphWeight(i, w);
                            changed = true;
                        }
                        if (animated)
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(UI::AccentColor(), ICON_FA_PERSON_RUNNING " %.2f", anim->MorphWeights[i]);
                        }
                        ImGui::PopID();
                    }
                    UI::PropertyLabel("");
                    ImGui::BeginDisabled(c.MorphWeights.empty());
                    if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT "  Modelning o'z og'irliklari")))
                    {
                        c.ResetMorphWeights();
                        changed = true;
                    }
                    ImGui::EndDisabled();
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SeparatorText(Tr("Material"));
            changed |= DrawMeshMaterial(entity, c);
            changed |= UI::PropertyBool("Soya tashlaydi", c.CastShadows);
            return changed;
        }, true, modified);

        AddSection<AnimatorComponent>(ICON_FA_PERSON_RUNNING, "Animator", entity, [&](AnimatorComponent& c) {
            bool changed = false;
            InactiveComponentNote(entity, "Obyekt nofaol: animator yangilanmaydi (joyida to'xtab turadi)");
            auto* mr = entity.TryGetComponent<MeshRendererComponent>();
            Ref<Model> model = mr ? mr->ModelAsset : nullptr;
            UI::PropertyLabel("Klip");
            if (ImGui::BeginCombo("##clip", c.Clip.empty() ? "(birinchi)" : c.Clip.c_str()))
            {
                if (ImGui::Selectable(Tr("(birinchi)"), c.Clip.empty())) { c.Clip.clear(); changed = true; }
                if (model)
                    for (const auto& clip : model->GetAnimations())
                        if (ImGui::Selectable(clip.Name.c_str(), clip.Name == c.Clip)) { c.Play(clip.Name, c.Loop, c.Speed); changed = true; }
                ImGui::EndCombo();
            }
            changed |= UI::PropertyFloat("Tezlik", c.Speed, 0.01f, -10.0f, 10.0f);
            changed |= UI::PropertyBool("Takrorlash", c.Loop);
            changed |= UI::PropertyBool("Ijroda", c.Playing);
            changed |= UI::PropertyBool("Editorda ko'rsatish", c.PlayInEditor);
            changed |= UI::PropertyFloat("O'tish vaqti (s)", c.DefaultFade, 0.01f, 0.0f, 5.0f);
            UI::PropertyLabel("Ma'lumot");
            if (!model) ImGui::TextDisabled("Mesh Renderer'da glTF model yo'q");
            else if (!model->IsSkinned()) ImGui::TextDisabled("Modelda skelet yo'q");
            else ImGui::Text("%zu suyak, %zu klip, t=%.2f", model->GetSkeleton().Joints.size(), model->GetAnimations().size(), c.Time);

            // ---- live state: what plays, how far, the crossfade, blend weights, layers; the
            // parameters while the game runs (editable)
            if (c.UsesStateMachine() || !m_SceneEditable || !c.Layers.empty())
            {
                const bool live = !m_SceneEditable;
                UI::PropertyHeader(live ? "Holat (jonli)" : "Holat (editorda ko'rib chiqish)",
                                   live ? "O'yin davomida: hozirgi holat, normalizatsiyalangan vaqt, o'tish jarayoni, blend og'irliklari "
                                          "va qatlamlar. Parametrlarni shu yerda o'zgartirsa bo'ladi"
                                        : "Editor boshlang'ich holatni ko'rsatadi (parametrlarning saqlangan qiymatlari bilan)");
                AnimatorEditing::DrawLiveStatus(c, model.get(), true);
                if (live && !c.Parameters.empty())
                    for (size_t i = 0; i < c.Parameters.size(); i++)
                    {
                        AnimatorParameter& p = c.Parameters[i];
                        ImGui::PushID((int)i + 9100);
                        std::string label = std::format(ICON_FA_SLIDERS " {}", p.Name);
                        UI::PropertyLabel(label.c_str(), "Parametr (jonli): skriptlar ham o'zgartiradi");
                        if (p.Kind == AnimatorParameter::Type::Float)
                            changed |= ImGui::DragFloat("##v", &p.Value, 0.05f, 0.0f, 0.0f, "%.2f");
                        else if (p.Kind == AnimatorParameter::Type::Bool)
                        {
                            bool b = p.Value != 0.0f;
                            if (ImGui::Checkbox("##v", &b)) { p.Value = b ? 1.0f : 0.0f; changed = true; }
                        }
                        else
                        {
                            const bool pending = p.Value != 0.0f;
                            if (pending)
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
                            if (ImGui::Button(pending ? ICON_FA_BOLT "  Kutmoqda" : ICON_FA_BOLT "  Ishga tushirish"))
                            {
                                p.Value = 1.0f;
                                changed = true;
                            }
                            if (pending)
                                ImGui::PopStyleColor();
                        }
                        ImGui::PopID();
                    }
                if (c.UsesStateMachine())
                {
                    UI::PropertyLabel("");
                    if (ImGui::Button(Tr(ICON_FA_DIAGRAM_PROJECT "  Animator grafi")) && OnAnimatorGraphRequested)
                        OnAnimatorGraphRequested(entity);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Holat mashinasi graf ko'rinishida: node'lar, o'tishlar, jonli holat");
                }
            }

            // ---- layers: clips over the base animation on parts of the body
            UI::PropertyHeader("Qatlamlar", "Qatlam o'z klipini asosiy animatsiya ustidan faqat maskadagi suyaklarda o'ynaydi - masalan "
                                            "ustki tana (maska: Spine) qo'l silkitadi, oyoqlar yuradi. Override: qatlam pozasi og'irligicha "
                                            "o'rnini egallaydi; Additive: klipning birinchi kadridan farqi ustiga qo'shiladi. Skript: "
                                            "PlayLayer(\"nom\", \"klip\"), SetLayerWeight(\"nom\", 0..1, fade)");
            {
                const Skeleton* skeleton = model && model->IsSkinned() ? &model->GetSkeleton() : nullptr;
                int removeLayer = -1;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                for (int i = 0; i < (int)c.Layers.size(); i++)
                {
                    AnimatorLayer& l = c.Layers[(size_t)i];
                    ImGui::PushID(i + 9500);
                    const std::string label = std::format(ICON_FA_LAYER_GROUP " Qatlam {}", i + 1);
                    UI::PropertyLabel(label.c_str(), "Qatlam nomi (skriptlar shu nom bilan boshqaradi)");
                    const float button = ImGui::GetFrameHeight();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button - spacing);
                    changed |= ImGui::InputText("##name", &l.Name);
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_TRASH, ImVec2(button, 0)))
                        removeLayer = i;
                    UI::PropertyLabel("Klip");
                    changed |= AnimatorEditing::ClipCombo("##clip", l.Clip, model.get(), "(yo'q)");
                    UI::PropertyLabel("Og'irlik");
                    if (ImGui::SliderFloat("##weight", &l.Weight, 0.0f, 1.0f, "%.2f"))
                    {
                        l.WeightFadeSpeed = 0.0f; // the slider sets it at once
                        changed = true;
                    }
                    UI::PropertyLabel("Og'irlik x", "Ixtiyoriy float parametr (0..1) og'irlikni ko'paytiradi - skriptdan silliq boshqarish");
                    changed |= AnimatorEditing::FloatParameterCombo("##wp", l.WeightParameter, c, "(yo'q)");
                    {
                        const char* blends[] = { "Override", "Additive" };
                        int blend = (int)l.Blend;
                        if (UI::PropertyCombo("Aralashtirish", blend, blends, 2))
                        {
                            l.Blend = (AnimatorLayerBlend)blend;
                            changed = true;
                        }
                    }
                    // The mask: bones whose subtrees the layer moves.
                    UI::PropertyLabel("Maska", "Belgilangan suyak va undan pastdagi hamma suyaklar (bo'sh - butun tana)");
                    std::string preview = l.MaskBones.empty() ? std::string("(butun tana)") : l.MaskBones.front();
                    if (l.MaskBones.size() > 1)
                        preview += std::format(" (+{})", l.MaskBones.size() - 1);
                    if (skeleton)
                    {
                        std::vector<float> mask;
                        const size_t count = BuildJointMask(*skeleton, l.MaskBones, mask);
                        preview += std::format("  -  {}/{} suyak", count, skeleton->Joints.size());
                    }
                    if (ImGui::BeginCombo("##mask", preview.c_str(), ImGuiComboFlags_HeightLarge))
                    {
                        if (ImGui::Selectable(Tr("(butun tana)"), l.MaskBones.empty(), ImGuiSelectableFlags_DontClosePopups))
                        {
                            l.MaskBones.clear();
                            changed = true;
                        }
                        if (skeleton)
                            for (size_t j = 0; j < skeleton->Joints.size(); j++)
                            {
                                const Joint& joint = skeleton->Joints[j];
                                int depth = 0;
                                for (int p = joint.Parent; p >= 0 && depth < 32; p = skeleton->Joints[(size_t)p].Parent)
                                    depth++;
                                auto it = std::find(l.MaskBones.begin(), l.MaskBones.end(), joint.Name);
                                bool on = it != l.MaskBones.end();
                                ImGui::PushID((int)j);
                                ImGui::Indent(depth * 10.0f + 1.0f);
                                if (ImGui::Checkbox(joint.Name.c_str(), &on))
                                {
                                    if (on) l.MaskBones.push_back(joint.Name);
                                    else l.MaskBones.erase(it);
                                    changed = true;
                                }
                                ImGui::Unindent(depth * 10.0f + 1.0f);
                                ImGui::PopID();
                            }
                        else
                            ImGui::TextDisabled("Modelda skelet yo'q");
                        ImGui::EndCombo();
                    }
                    UI::PropertyLabel("Tezlik / takror");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - spacing);
                    changed |= ImGui::DragFloat("##speed", &l.Speed, 0.01f, -10.0f, 10.0f, "x%.2f");
                    ImGui::SameLine();
                    changed |= ImGui::Checkbox("##loop", &l.Loop);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Takrorlash");
                    ImGui::PopID();
                }
                if (removeLayer >= 0)
                {
                    c.Layers.erase(c.Layers.begin() + removeLayer);
                    changed = true;
                }
                UI::PropertyLabel("");
                if (ImGui::Button(Tr(ICON_FA_PLUS "  Qatlam")))
                {
                    AnimatorLayer l;
                    l.Name = c.Layers.empty() ? "UpperBody" : std::format("Layer{}", c.Layers.size() + 1);
                    // An upper body mask when the skeleton has a spine.
                    if (skeleton)
                        for (const Joint& j : skeleton->Joints)
                        {
                            std::string lower = j.Name;
                            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                            if (lower.find("spine") != std::string::npos)
                            {
                                l.MaskBones = { j.Name };
                                break;
                            }
                        }
                    c.Layers.push_back(std::move(l));
                    changed = true;
                }
            }

            // ---- root motion
            UI::PropertyHeader("Root motion", "Ildiz suyakning yurishi (XZ) va burilishi (Y) obyektni siljitadi: mesh obyektdan "
                                              "uzoqlashib ketmaydi. Character Controller 3D bo'lsa - u yuradi (to'qnashuvlar bilan). "
                                              "Editor ko'rinishida joyida turadi");
            changed |= UI::PropertyBool("Qo'llash", c.ApplyRootMotion);
            if (c.ApplyRootMotion)
            {
                UI::PropertyLabel("Ildiz suyak", "Qaysi suyakning harakati obyektga o'tadi. Avtomatik: klip siljitadigan eng yuqori suyak (odatda Hips / Root)");
                const Skeleton* skeleton = model && model->IsSkinned() ? &model->GetSkeleton() : nullptr;
                std::string preview = c.RootMotionBone.empty() ? std::string("(avtomatik)") : c.RootMotionBone;
                if (c.RootMotionBone.empty() && skeleton)
                {
                    const int clipIndex = c.Clip.empty() ? 0 : model->FindAnimation(c.Clip);
                    const AnimationClip* clip = clipIndex >= 0 && clipIndex < (int)model->GetAnimations().size() ? &model->GetAnimations()[clipIndex] : nullptr;
                    const int joint = FindRootMotionJoint(*skeleton, clip);
                    if (joint >= 0)
                        preview += "  " + skeleton->Joints[joint].Name;
                }
                if (ImGui::BeginCombo("##rootbone", preview.c_str()))
                {
                    if (ImGui::Selectable(Tr("(avtomatik)"), c.RootMotionBone.empty())) { c.RootMotionBone.clear(); changed = true; }
                    if (skeleton)
                        for (const Joint& j : skeleton->Joints)
                            if (ImGui::Selectable(j.Name.c_str(), j.Name == c.RootMotionBone)) { c.RootMotionBone = j.Name; changed = true; }
                    ImGui::EndCombo();
                }
                if (c.RootMotionVelocity != glm::vec3(0.0f) || c.RootMotionTurnRate != 0.0f)
                {
                    UI::PropertyLabel("");
                    ImGui::TextDisabled("%.2f m/s, %.0f gradus/s", glm::length(c.RootMotionVelocity), glm::degrees(c.RootMotionTurnRate));
                }
            }

            // ---- performance: off-screen culling and the pose cache
            UI::PropertyHeader("Samaradorlik", "Ekrandan tashqaridagi modellar va keshlangan pozalar. Bir xil kirishlar bilan (pauza, tezlik 0, "
                                                "tugagan klip) poza qaytadan hisoblanmaydi - bu doim ishlaydi");
            {
                static const char* cullingItems[] = { "Doim", "Pozasiz", "To'liq" };
                static const char* cullingTips[] = {
                    "Ekrandan tashqarida ham to'liq yangilanadi",
                    "Hech bir kamera ko'rmasa: vaqt, holat mashinasi, event'lar va root motion davom etadi, skelet pozasi hisoblanmaydi",
                    "Hech bir kamera ko'rmasa: hech narsa yangilanmaydi - ko'ringanda turgan joyidan davom etadi" };
                int culling = (int)c.Culling;
                // What happens while no view (the game's camera, the editor's) sees the model -
                // also when only its shadow is in view.
                if (UI::PropertySegmented("Ko'rinmasa", culling, cullingItems, 3, cullingTips))
                {
                    c.Culling = (AnimatorCulling)culling;
                    changed = true;
                }
                changed |= UI::PropertyBool("Poza keshi", c.CachePoses);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Klip oldindan belgilangan tezlikda (kadr/s) namuna olinib saqlanadi: har kadr ikki saqlangan kadr "
                                "aralashtiriladi. Ko'p suyakli va ko'p kalitli kliplar uchun arzonroq; step kalitlar aniq qoladi");
                if (c.CachePoses)
                {
                    changed |= UI::PropertyFloat("Kesh kadr/s", c.PoseCacheRate, 1.0f, 1.0f, 240.0f, "%.0f");
                    c.PoseCacheRate = std::clamp(c.PoseCacheRate, 1.0f, 240.0f);
                }
                if (!m_SceneEditable || c.Culled)
                {
                    UI::PropertyLabel("Hozir");
                    if (c.Culled)
                        ImGui::TextColored(UI::AccentColor(), ICON_FA_EYE_SLASH "  kesilgan");
                    else
                        ImGui::TextDisabled(ICON_FA_EYE "  ko'rinmoqda");
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(!c.Culled ? "Model ko'rinishda: har kadr pozalanadi"
                                    : c.Culling == AnimatorCulling::CullCompletely
                                        ? "Oxirgi yangilanishda hech bir kamera modelni ko'rmadi: hech narsa yangilanmadi (vaqt ham)"
                                        : "Oxirgi yangilanishda hech bir kamera modelni ko'rmadi: poza hisoblanmadi (vaqt va event'lar davom etadi)");
                }
            }

            // ---- events
            UI::PropertyHeader("Hodisalar (event)", "Klipning shu nuqtasidan o'tilganda (0..1 - klip boshidan oxirigacha) obyekt skripti "
                                                    "OnAnimationEvent(nom) oladi; event bus'da \"AnimationEvent\". O'yin rejimida");
            int removeEvent = -1;
            for (int i = 0; i < (int)c.Events.size(); i++)
            {
                AnimatorEvent& ev = c.Events[(size_t)i];
                ImGui::PushID(i);
                // Two rows: its name (and the delete button), then which clip and when.
                const std::string label = std::format("Event {}", i + 1);
                UI::PropertyLabel(label.c_str(), "Nom: skriptning OnAnimationEvent(e) da e.Name; klip va vaqt (0..1)");
                const float button = ImGui::GetFrameHeight();
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button - spacing);
                changed |= ImGui::InputTextWithHint("##name", Tr("nom"), &ev.Name);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_TRASH, ImVec2(button, 0)))
                    removeEvent = i;
                UI::PropertyLabel("");
                const float avail = ImGui::GetContentRegionAvail().x - spacing;
                ImGui::SetNextItemWidth(avail * 0.55f);
                if (ImGui::BeginCombo("##clip", ev.Clip.empty() ? "(har qanday)" : ev.Clip.c_str()))
                {
                    if (ImGui::Selectable(Tr("(har qanday)"), ev.Clip.empty())) { ev.Clip.clear(); changed = true; }
                    if (model)
                        for (const auto& clip : model->GetAnimations())
                            if (ImGui::Selectable(clip.Name.c_str(), clip.Name == ev.Clip)) { ev.Clip = clip.Name; changed = true; }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(avail * 0.45f);
                changed |= ImGui::SliderFloat("##time", &ev.Time, 0.0f, 1.0f, "%.2f");
                ImGui::PopID();
            }
            if (removeEvent >= 0)
            {
                c.Events.erase(c.Events.begin() + removeEvent);
                changed = true;
            }
            UI::PropertyLabel("");
            if (ImGui::Button(Tr(ICON_FA_PLUS "  Event")))
            {
                AnimatorEvent ev;
                ev.Clip = c.Clip;
                ev.Name = "Footstep";
                c.Events.push_back(ev);
                changed = true;
            }
            return changed;
        }, true, modified);

        if (entity.HasComponent<AnimatorComponent>()) // right below the Animator header, wherever it is
            m_Sections.push_back({ ComponentKey<AnimatorComponent>(), [&]() {
                auto* anim = entity.IsValid() ? entity.TryGetComponent<AnimatorComponent>() : nullptr;
                if (!anim) return;
                auto* mr = entity.TryGetComponent<MeshRendererComponent>();
                const char* foldKey = "Animator.StateMachine";
                const bool wasOpen = m_FoldState ? m_FoldState->IsOpen(foldKey, false) : false;
                if (m_FoldState)
                    ImGui::SetNextItemOpen(wasOpen, ImGuiCond_Always);
                bool open = wasOpen;
                const UUID id = entity.GetUUID();
                modified |= DrawAnimatorStateMachine(*anim, mr ? mr->ModelAsset : nullptr, &open, !m_SceneEditable, [this, id]() {
                    if (OnAnimatorGraphRequested)
                        if (Entity e = m_Context->GetEntityByUUID(id))
                            OnAnimatorGraphRequested(e);
                });
                if (m_FoldState && open != wasOpen && m_FoldState->SetOpen(foldKey, open, false) && OnFoldStateModified)
                    OnFoldStateModified();
            } });

        AddSection<AnimationPlayerComponent>(ICON_FA_FILM, "Animation Player", entity, [&](AnimationPlayerComponent& c) {
            return DrawAnimationPlayer(entity, c);
        }, true, modified);

        AddSection<BoneAttachmentComponent>(ICON_FA_PAPERCLIP, "Bone Attachment", entity, [&](BoneAttachmentComponent& c) {
            return DrawBoneAttachment(entity, c);
        }, true, modified);

        AddSection<DirectionalLightComponent>(ICON_FA_SUN, "Directional Light", entity, [&](DirectionalLightComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: quyosh hisobga olinmaydi (yorug'lik ham, soya ham yo'q)");
            // Several directional lights all light the scene; only the main one (the first with
            // shadows on, in scene order) casts shadows. Say which role this one has.
            if (entity.IsActiveInHierarchy())
            {
                Scene::LightSet lights = m_Context->CollectLights(glm::vec3(0.0f));
                const bool lit = std::find(lights.DirectionalLights.begin(), lights.DirectionalLights.end(), (entt::entity)entity) !=
                                 lights.DirectionalLights.end();
                Entity sun = lights.Sun != entt::null ? Entity(lights.Sun, m_Context.get()) : Entity();
                const ImVec4 info(0.55f, 0.78f, 0.95f, 1.0f), warn(0.95f, 0.75f, 0.3f, 1.0f);
                auto note = [&](const ImVec4& color, const std::string& text, const char* tooltip, bool selectSun) {
                    UI::PropertyLabel("");
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(color, "%s", text.c_str());
                    ImGui::PopTextWrapPos();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", tooltip);
                    if (selectSun && sun && ImGui::IsItemClicked())
                        SetSelectedEntity(sun);
                };
                static const char* rule = "Barcha faol Directional Light'lar sahnani yoritadi. Soyani faqat bittasi beradi: sahna "
                                          "tartibida 'Soyalar' yoqilgan birinchisi (asosiy quyosh); osmondagi quyosh diski ham unga "
                                          "ergashadi.";
                if (!lit)
                    note(warn, std::format(ICON_FA_CIRCLE_INFO "  Yoritmaydi: {} tadan ortiq quyosh", Renderer3D::MaxDirectionalLights),
                         std::format("Bir vaqtda ko'pi bilan {} ta Directional Light yoritadi (asosiy quyosh va sahna tartibida "
                                     "birinchilari)", Renderer3D::MaxDirectionalLights).c_str(),
                         false);
                else if (sun == entity && lights.DirectionalLights.size() > 1)
                    note(info, c.CastShadows ? ICON_FA_SUN "  Asosiy quyosh: soya shu yorug'likdan" : ICON_FA_SUN "  Asosiy quyosh (soyasiz)",
                         rule, false);
                else if (sun && sun != entity)
                {
                    std::string text = c.CastShadows
                        ? std::format(ICON_FA_CIRCLE_INFO "  Soya bermaydi: soyani '{}' beradi", sun.GetName())
                        : std::format(ICON_FA_CIRCLE_INFO "  Qo'shimcha quyosh: yoritadi, soyasiz (asosiy: '{}')", sun.GetName());
                    note(c.CastShadows ? warn : info, text, std::format("{}\nBosing: asosiy quyoshni tanlash", rule).c_str(), true);
                }
            }
            bool changed = UI::PropertyColor3("Rang", c.Color);
            changed |= UI::PropertyFloat("Kuch", c.Intensity, 0.05f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Soyalar", c.CastShadows);
            return changed;
        }, true, modified);

        AddSection<PointLightComponent>(ICON_FA_LIGHTBULB, "Point Light", entity, [&](PointLightComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: yorug'lik hisobga olinmaydi (yoritmaydi, joy egallamaydi)");
            bool changed = UI::PropertyColor3("Rang", c.Color);
            changed |= UI::PropertyFloat("Kuch", c.Intensity, 0.05f, 0.0f, 1000.0f);
            changed |= UI::PropertyFloat("Radius", c.Radius, 0.05f, 0.0f, 1000.0f);
            return changed;
        }, true, modified);

        AddSection<SpotLightComponent>(ICON_FA_BULLHORN, "Spot Light", entity, [&](SpotLightComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: yorug'lik hisobga olinmaydi (yoritmaydi, joy egallamaydi)");
            bool changed = UI::PropertyColor3("Rang", c.Color);
            changed |= UI::PropertyFloat("Kuch", c.Intensity, 0.05f, 0.0f, 1000.0f);
            UI::PropertyLabel("Masofa", "Yorug'lik yetib boradigan masofa: shundan keyin nolga so'nadi (nuqtali yorug'lik radiusi kabi)");
            changed |= ImGui::DragFloat("##range", &c.Range, 0.05f, 0.0f, 1000.0f, "%.2f");
            // Angles from the axis; the inner one never exceeds the outer one (and the other way round).
            UI::PropertyLabel("Tashqi burchak", "Konus o'qidan chetigacha bo'lgan burchak (gradus): bundan tashqarida yorug'lik yo'q");
            if (ImGui::SliderFloat("##outer", &c.OuterAngle, 1.0f, SpotLightComponent::MaxAngle, "%.1f\xc2\xb0"))
            {
                c.OuterAngle = std::clamp(c.OuterAngle, 0.1f, SpotLightComponent::MaxAngle);
                c.InnerAngle = std::min(c.InnerAngle, c.OuterAngle);
                changed = true;
            }
            UI::PropertyLabel("Ichki burchak", "Shu burchakkacha to'liq yorqinlik; ichki va tashqi burchak orasida yumshoq so'nadi");
            if (ImGui::SliderFloat("##inner", &c.InnerAngle, 0.0f, SpotLightComponent::MaxAngle, "%.1f\xc2\xb0"))
            {
                c.InnerAngle = std::clamp(c.InnerAngle, 0.0f, SpotLightComponent::MaxAngle);
                c.OuterAngle = std::max(c.OuterAngle, c.InnerAngle);
                changed = true;
            }
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f); // wraps inside the value column
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Obyektning -Z o'qi bo'ylab nur sochadi");
            ImGui::PopTextWrapPos();
            return changed;
        }, true, modified);

        AddSection<Light2DComponent>(ICON_FA_FIRE, "Light 2D", entity, [&](Light2DComponent& c) {
            return DrawLight2D(entity, c);
        }, true, modified);

        AddSection<Rigidbody2DComponent>(ICON_FA_ATOM, "Rigidbody 2D", entity, [&](Rigidbody2DComponent& c) {
            bool changed = false;
            const char* types[] = { "Statik", "Dinamik", "Kinematik" };
            int type = (int)c.Type;
            if (UI::PropertyCombo("Turi", type, types, 3)) { c.Type = (Rigidbody2DComponent::BodyType)type; changed = true; }
            changed |= UI::PropertyBool("Aylanmasin", c.FixedRotation);
            changed |= UI::PropertyFloat("Gravitatsiya", c.GravityScale, 0.05f);
            changed |= UI::PropertyFloat("Chiziqli so'nish", c.LinearDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyFloat("Burchak so'nishi", c.AngularDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Tez obyekt (bullet)", c.Bullet);
            changed |= DrawCollisionLayer("Qatlam", c.Layer, false,
                                          "Kolliderlarining to'qnashuv qatlami: qaysi qatlamlarga tegishi loyiha matritsasida "
                                          "(Loyiha sozlamalari > Fizika qatlamlari). Kollider o'z qatlamini tanlashi mumkin.");
            if (c.Type != Rigidbody2DComponent::BodyType::Static)
            {
                const char* modes[] = { "Uyg'oq boshlanadi", "Uxlab boshlanadi", "Hech uxlamaydi" };
                int mode = (int)c.Sleeping;
                UI::PropertyLabel("Uxlash", "Qimirlamay qolgan jism uxlaydi: tegilmaguncha hisoblanmaydi (Sahna sozlamalari > Fizika). "
                                            "Uxlab boshlanadi - urilguncha kutib turadigan minora yoki to'plam uchun; Hech uxlamaydi - "
                                            "doim hisoblanadi");
                ImGui::PushID("sleep");
                if (ImGui::Combo("##v", &mode, modes, 3))
                {
                    c.Sleeping = (Rigidbody2DComponent::SleepMode)std::clamp(mode, 0, 2);
                    changed = true;
                }
                ImGui::PopID();
                if (c.RuntimeBody && m_Context)
                {
                    // While the game runs: whether it sleeps now, and a button to change that.
                    const bool sleeping = m_Context->IsSleeping2D(entity);
                    UI::PropertyLabel("Holati");
                    if (sleeping)
                        ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.95f, 1.0f), ICON_FA_MOON "  Uxlayapti");
                    else
                        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), ICON_FA_BOLT "  Uyg'oq");
                    ImGui::SameLine();
                    if (ImGui::SmallButton(sleeping ? "Uyg'otish" : "Uxlatish"))
                        m_Context->SetAwake2D(entity, sleeping);
                }
            }
            return changed;
        }, true, modified);

        // What every 2D collider has after its shape: the material (or its own values), trigger, layer.
        auto colliderCommon = [&](auto& c, bool hasDensity) {
            PhysicsMaterialData own;
            own.Friction = c.Friction;
            own.Restitution = c.Restitution;
            own.RestitutionThreshold = c.RestitutionThreshold;
            own.Density = c.Density;
            bool material = false;
            bool changed = DrawPhysicsMaterialField(entity, c.MaterialPath, own, material);
            ImGui::BeginDisabled(material); // the material's values are used
            if (hasDensity)
                changed |= UI::PropertyFloat("Zichlik", c.Density, 0.01f, 0.0f, 1000.0f);
            changed |= UI::PropertySlider("Ishqalanish", c.Friction, 0.0f, 1.0f);
            changed |= UI::PropertySlider("Sakrash", c.Restitution, 0.0f, 1.0f);
            changed |= UI::PropertyFloat("Sakrash chegarasi", c.RestitutionThreshold, 0.01f, 0.0f, 100.0f);
            ImGui::EndDisabled();
            changed |= UI::PropertyBool("Trigger (sensor)", c.IsSensor);
            changed |= DrawCollisionLayer("Qatlam", c.Layer, true,
                                          "Shu kolliderning to'qnashuv qatlami. '(Rigidbody 2D qatlami)' - obyekt Rigidbody 2D'sidagi qatlam");
            return changed;
        };

        AddSection<BoxCollider2DComponent>(ICON_FA_VECTOR_SQUARE, "Box Collider 2D", entity, [&](BoxCollider2DComponent& c) {
            bool changed = UI::PropertyFloat2("Siljish", c.Offset);
            changed |= UI::PropertyFloat2("Yarim o'lcham", c.Size);
            changed |= colliderCommon(c, true);
            const bool effector = entity.HasComponent<PlatformEffector2DComponent>();
            ImGui::BeginDisabled(c.IsSensor || effector);
            changed |= UI::PropertyBool("Bir tomonlama (platforma)", c.OneWay);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip(effector ? "Obyektda Platform Effector 2D bor: uning sozlamalari ishlatiladi"
                                     : "Pastdan sakrab o'tiladi, ustiga qo'niladi (yuqorisi - obyektning +Y tomoni). Skriptdan "
                                       "DropThroughPlatforms() bilan pastga tushib ketiladi. Trigger'da ishlamaydi. Ko'proq sozlama: "
                                       "Platform Effector 2D");
            ImGui::EndDisabled();
            if (c.OneWay && !effector && !c.IsSensor && m_SceneEditable)
            {
                UI::PropertyLabel("");
                if (ImGui::SmallButton(Tr(ICON_FA_TURN_UP "  Platform Effector 2D ga o'tkazish")))
                {
                    entity.AddComponent<PlatformEffector2DComponent>();
                    c.OneWay = false;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Xuddi shunday bir tomonlama platforma, lekin yoy burchagi, yon ishqalanish va sakrash sozlamalari bilan");
            }
            return changed;
        }, true, modified);

        AddSection<CircleCollider2DComponent>(ICON_FA_CIRCLE_NODES, "Circle Collider 2D", entity, [&](CircleCollider2DComponent& c) {
            bool changed = UI::PropertyFloat2("Siljish", c.Offset);
            changed |= UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.0f, 1000.0f);
            changed |= colliderCommon(c, true);
            return changed;
        }, true, modified);

        AddSection<PolygonCollider2DComponent>(ICON_FA_DRAW_POLYGON, "Polygon Collider 2D", entity, [&](PolygonCollider2DComponent& c) {
            bool changed = UI::PropertyFloat2("Siljish", c.Offset);
            // What the physics makes of the outline at the entity's scale.
            glm::vec3 position, rotation, scale;
            Math::DecomposeTransform(entity.GetScene()->GetWorldTransform(entity), position, rotation, scale);
            Polygon2D::Decomposition d = Scene::DecomposePolygonCollider(c, glm::abs(glm::vec2(scale)));
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            const ImVec4 info(0.55f, 0.78f, 0.95f, 1.0f), warn(0.95f, 0.75f, 0.3f, 1.0f), error(0.95f, 0.45f, 0.4f, 1.0f);
            switch (d.Result)
            {
                case Polygon2D::DecomposeResult::Convex:
                    ImGui::TextColored(info, ICON_FA_CIRCLE_CHECK "  Qavariq: %zu ta bo'lak", d.Pieces.size());
                    break;
                case Polygon2D::DecomposeResult::Decomposed:
                    ImGui::TextColored(info, ICON_FA_SHAPES "  Botiq: %zu ta qavariq bo'lakka bo'lindi", d.Pieces.size());
                    break;
                case Polygon2D::DecomposeResult::SelfIntersecting:
                    ImGui::TextColored(warn, ICON_FA_TRIANGLE_EXCLAMATION "  Chiziqlari kesishadi: qavariq qobig'i ishlatiladi");
                    break;
                case Polygon2D::DecomposeResult::Degenerate:
                    ImGui::TextColored(error, ICON_FA_CIRCLE_XMARK "  Yuzasi yo'q: kolliziya bo'lmaydi");
                    break;
            }
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Box2D faqat qavariq, ko'pi bilan 8 burchakli shakllarni biladi: botiq yoki ko'p burchakli shakl "
                            "uchburchaklarga bo'linib, qavariq bo'laklarga qayta birlashtiriladi (har biri alohida fixture)");
            changed |= DrawColliderPoints(c.Points, ColliderPointKind::Polygon, 3);
            UI::PropertyLabel("");
            const bool sprite = entity.HasComponent<SpriteRendererComponent>();
            ImGui::BeginDisabled(!sprite || !m_SceneEditable);
            if (ImGui::Button(Tr(ICON_FA_CROP "  Spraytga moslash"), ImVec2(-FLT_MIN, 0)))
                changed |= FitPolygonToSprite(entity, c);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip(sprite ? "Nuqtalar sprayt rasmining shaffof bo'lmagan qismi chetiga (hozirgi kadr) moslanadi: ko'pi bilan 12 nuqta"
                                   : "Obyektda Sprite Renderer yo'q");
            changed |= colliderCommon(c, true);
            return changed;
        }, true, modified);

        AddSection<EdgeCollider2DComponent>(ICON_FA_CHART_LINE, "Edge Collider 2D", entity, [&](EdgeCollider2DComponent& c) {
            bool changed = UI::PropertyFloat2("Siljish", c.Offset);
            changed |= UI::PropertyBool("Yopiq (loop)", c.Loop);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Oxirgi nuqta birinchisiga ulanadi (kamida 3 nuqta)");
            changed |= UI::PropertyBool("Bir tomonlama (silliq zanjir)", c.OneSided);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Faqat nuqtalar yo'nalishining chap tomonidan to'xtatadi (chapdan o'ngga - ustidan; soat millariga teskari "
                            "yopiq - ichidan). Ustida sirpangan jismlar bo'g'inlarga ilinmaydi. Viewportda qattiq tomoni chiziqchalar bilan ko'rsatiladi");
            if (c.OneSided)
            {
                UI::PropertyLabel("");
                if (ImGui::SmallButton(Tr(ICON_FA_ARROWS_LEFT_RIGHT "  Tomonini almashtirish")))
                {
                    std::reverse(c.Points.begin(), c.Points.end());
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Nuqtalar tartibini teskari qiladi: qattiq tomon qarama-qarshi tomonga o'tadi");
            }
            const size_t usable = Polygon2D::CleanPolyline(c.Points, c.Loop).size();
            if (usable == 0)
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.0f), ICON_FA_CIRCLE_XMARK "  Nuqtalar yetarli emas: kolliziya bo'lmaydi");
            }
            changed |= DrawColliderPoints(c.Points, ColliderPointKind::Edge, c.Loop ? 3 : 2);
            changed |= colliderCommon(c, false);
            return changed;
        }, true, modified);

        AddSection<PlatformEffector2DComponent>(ICON_FA_TURN_UP, "Platform Effector 2D", entity, [&](PlatformEffector2DComponent& c) {
            bool changed = false;
            if (!entity.HasComponent<Rigidbody2DComponent>() && !entity.HasComponent<TilemapComponent>())
            {
                UI::PropertyLabel("");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Rigidbody 2D (kolliderlari bilan) yoki Tilemap kerak");
                ImGui::PopTextWrapPos();
            }
            changed |= UI::PropertyBool("Bir tomonlama", c.OneWay);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Jismlar pastdan / yondan o'tib ketadi va ustiga qo'nadi. Skriptdan DropThroughPlatforms() bilan tushib ketiladi");
            ImGui::BeginDisabled(!c.OneWay);
            UI::PropertyLabel("Sirt yoyi", "Faqat shu burchak ichidan (sirtning yuqorisi atrofida) kelgan to'qnashuvlar ushlab qoladi. "
                                           "180 - oddiy bir tomonlama platforma, kichikroq - burchaklardan ham o'tib ketadi, "
                                           "kattaroq - yon tomonlardan ham ushlaydi");
            if (ImGui::SliderFloat("##arc", &c.SurfaceArc, 1.0f, 360.0f, "%.0f\xc2\xb0"))
            {
                c.SurfaceArc = std::clamp(c.SurfaceArc, 1.0f, 360.0f);
                changed = true;
            }
            ImGui::EndDisabled();
            UI::PropertyLabel("Burilish", "Sirtning yuqorisi obyektning +Y o'qidan shuncha gradus buriladi (soat millariga teskari)");
            changed |= ImGui::DragFloat("##rotation", &c.RotationOffset, 0.5f, -360.0f, 360.0f, "%.1f\xc2\xb0");
            UI::PropertyHeader("Yon tomonlar", "Sirtning chap / o'ng tomoniga (yon yoy ichida) tekkan to'qnashuvlar");
            changed |= UI::PropertyBool("Yon ishqalanish", c.UseSideFriction);
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'chiq: yon tomonda ishqalanish yo'q - o'yinchi devorga tiralganda unga yopishib qolmaydi");
            changed |= UI::PropertyBool("Yon sakrash", c.UseSideBounce);
            UI::PropertyLabel("Yon yoy", "Yon tomon deb hisoblanadigan burchak (gorizontal atrofida)");
            if (ImGui::SliderFloat("##sideArc", &c.SideArc, 0.0f, 180.0f, "%.0f\xc2\xb0"))
            {
                c.SideArc = std::clamp(c.SideArc, 0.0f, 180.0f);
                changed = true;
            }
            if (auto* box = entity.TryGetComponent<BoxCollider2DComponent>(); box && box->OneWay)
            {
                UI::PropertyLabel("");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Box Collider 2D'ning 'Bir tomonlama' belgisi o'rniga shu sozlamalar ishlatiladi");
                ImGui::PopTextWrapPos();
            }
            return changed;
        }, true, modified);

        // ---- area / buoyancy effectors
        auto angleField = [](const char* label, const char* tooltip, float& v) {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            bool edited = ImGui::DragFloat("##angle", &v, 0.5f, -360.0f, 360.0f, "%.1f\xc2\xb0");
            ImGui::PopID();
            if (edited)
                v = std::clamp(v, -360.0f, 360.0f);
            return edited;
        };
        auto nonNegative = [](const char* label, const char* tooltip, float& v, float speed, const char* fmt) {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            bool edited = ImGui::DragFloat("##v", &v, speed, 0.0f, 1e6f, fmt);
            ImGui::PopID();
            if (edited)
                v = std::max(0.0f, v);
            return edited;
        };
        const bool physicsRunning = m_Context && (m_Context->IsRunning() || m_Context->IsSimulating());

        AddSection<AreaEffector2DComponent>(ICON_FA_WIND, "Area Effector 2D", entity, [&](AreaEffector2DComponent& c) {
            bool changed = false;
            DrawEffectorNotes(entity, false);
            UI::PropertyHeader("Kuch", "Har fizika qadamida kollider ichidagi har bir dinamik jismga beriladi (shamol, oqim, puflagich)");
            changed |= angleField("Yo'nalish", "Kuch burchagi: 0 - o'ngga (+X), 90 - yuqoriga", c.ForceAngle);
            changed |= UI::PropertyBool("Dunyo burchagi", c.UseGlobalAngle);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Yoqiq: burchak dunyoda. O'chiq: obyekt bilan birga buriladi");
            UI::PropertyLabel("Kattaligi", c.ScaleWithMass ? "N / kg: har bir jism massasidan qat'i nazar shuncha tezlanadi"
                                                           : "Nyuton: yengil jismlar ko'proq, og'irlari kamroq tezlanadi. Manfiy - teskari tomonga");
            changed |= ImGui::DragFloat("##magnitude", &c.ForceMagnitude, 0.1f, -1e5f, 1e5f, c.ScaleWithMass ? "%.2f N/kg" : "%.2f N");
            changed |= nonNegative("Tasodifiy o'zgarish", "Har qadamda kuchga +- shuncha gacha tasodifiy qo'shiladi (shamol shiddati)",
                                   c.ForceVariation, 0.05f, "%.2f");
            changed |= UI::PropertyBool("Massaga ko'paytirish", c.ScaleWithMass);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Kuch jism massasiga ko'paytiriladi: hamma jism bir xil tezlanadi (gravitatsiya kabi)");
            UI::PropertyHeader("Qarshilik", "Ichidagi jismlarning tezligi shu tezlikda so'nadi (1/s, Rigidbody 2D so'nishi kabi)");
            changed |= nonNegative("Chiziqli", "Harakat tezligining so'nishi (1/s)", c.Drag, 0.02f, "%.2f");
            changed |= nonNegative("Burchak", "Aylanishning so'nishi (1/s)", c.AngularDrag, 0.02f, "%.2f");
            changed |= DrawLayerMask("Qatlamlar", c.ColliderMask, "Faqat shu to'qnashuv qatlamlaridagi jismlarga ta'sir qiladi (qatlam matritsasi "
                                                               "ham ruxsat berishi kerak)");
            if (physicsRunning)
            {
                UI::PropertyLabel("Itarilayotgan", "Oxirgi fizika qadamida ta'sir qilgan jismlar");
                ImGui::TextDisabled(ICON_FA_WIND "  %d ta jism", c.AffectedBodies);
            }
            return changed;
        }, true, modified);

        AddSection<BuoyancyEffector2DComponent>(ICON_FA_WATER, "Buoyancy Effector 2D", entity, [&](BuoyancyEffector2DComponent& c) {
            bool changed = false;
            DrawEffectorNotes(entity, true);
            UI::PropertyHeader("Suyuqlik", "Kollider ichi sirtgacha suyuqlik: jism siqib chiqargan suyuqlik og'irligi uni yuqoriga itaradi "
                                           "(zichlik x botgan yuza x gravitatsiya)");
            UI::PropertyLabel("Sirt balandligi", "Suv sirti obyekt joylashuvidan shuncha birlik yuqorida (dunyo birliklarida, gorizontal)");
            const float button = ImGui::GetFrameHeight();
            ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
            changed |= ImGui::DragFloat("##surface", &c.SurfaceLevel, 0.01f, -1e5f, 1e5f, "%.2f");
            ImGui::SameLine();
            ImGui::BeginDisabled(!m_SceneEditable);
            if (ImGui::Button(ICON_FA_ANGLES_UP "##surfaceTop", ImVec2(button, 0)))
            {
                // The top of the entity's colliders (as the physics makes them: sizes scaled, offsets not).
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(entity.GetScene()->GetWorldTransform(entity), p, r, sc);
                const glm::vec2 scale = glm::abs(glm::vec2(sc));
                const float cs = std::cos(r.z), sn = std::sin(r.z);
                float top = -FLT_MAX;
                auto add = [&](const glm::vec2& local) { top = std::max(top, sn * local.x + cs * local.y); };
                if (auto* b = entity.TryGetComponent<BoxCollider2DComponent>())
                    for (glm::vec2 corner : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
                        add(b->Offset + corner * b->Size * scale);
                if (auto* cc = entity.TryGetComponent<CircleCollider2DComponent>())
                    top = std::max(top, sn * cc->Offset.x + cs * cc->Offset.y + cc->Radius * scale.x);
                if (auto* pc = entity.TryGetComponent<PolygonCollider2DComponent>())
                    for (const glm::vec2& q : pc->Points)
                        add(pc->Offset + q * scale);
                if (top > -FLT_MAX)
                {
                    c.SurfaceLevel = top;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Sirtni kolliderning eng yuqori nuqtasiga qo'yish (to'la suv)");
            ImGui::EndDisabled();
            changed |= nonNegative("Zichlik", "Suyuqlik zichligi. Kollideri (Density) shundan kichik jism suzadi, kattasi cho'kadi. "
                                              "Kolliderlarning odatiy zichligi - 1", c.Density, 0.01f, "%.2f");
            UI::PropertyHeader("Qarshilik", "Suv ichidagi jism tezligining so'nishi, qanchalik botganiga qarab (1/s)");
            changed |= nonNegative("Chiziqli", "To'la botgan jism uchun (1/s)", c.LinearDrag, 0.02f, "%.2f");
            changed |= nonNegative("Burchak", "Aylanishning so'nishi, to'la botgan jism uchun (1/s)", c.AngularDrag, 0.02f, "%.2f");
            UI::PropertyHeader("Oqim", "Suvdagi jismlarni shu yo'nalishda olib ketadi (daryo oqimi), botgan qismiga qarab");
            changed |= angleField("Yo'nalish", "Dunyodagi burchak: 0 - o'ngga (+X), 90 - yuqoriga", c.FlowAngle);
            UI::PropertyLabel("Kuch", "To'la botgan jismga beriladigan kuch (N). 0 - oqim yo'q");
            changed |= ImGui::DragFloat("##flow", &c.FlowMagnitude, 0.05f, -1e5f, 1e5f, "%.2f N");
            changed |= nonNegative("Tasodifiy o'zgarish", "Har qadamda oqim kuchiga +- shuncha gacha tasodifiy qo'shiladi (to'lqinlar)",
                                   c.FlowVariation, 0.05f, "%.2f");
            changed |= DrawLayerMask("Qatlamlar", c.ColliderMask, "Faqat shu to'qnashuv qatlamlaridagi jismlar suzadi (qatlam matritsasi "
                                                               "ham ruxsat berishi kerak)");
            if (physicsRunning)
            {
                UI::PropertyLabel("Suvdagi jismlar", "Oxirgi fizika qadamida suyuqlik ichida bo'lgan jismlar");
                ImGui::TextDisabled(ICON_FA_WATER "  %d ta", c.AffectedBodies);
                UI::PropertyLabel("Botgan yuza", "Ularning sirt ostidagi umumiy yuzasi (kvadrat birlik)");
                ImGui::TextDisabled("%.2f", c.SubmergedArea);
            }
            return changed;
        }, true, modified);

        // ---- 2D joints
        auto degrees = [](const char* label, const char* tooltip, float& v, float min, float max) {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            bool edited = ImGui::DragFloat("##deg", &v, 0.5f, min, max, "%.1f\xc2\xb0");
            ImGui::PopID();
            if (edited)
                v = std::clamp(v, min, max);
            return edited;
        };
        auto range = [](const char* lowerLabel, const char* upperLabel, float& lower, float& upper, float speed, const char* fmt) {
            UI::PropertyLabel(lowerLabel);
            ImGui::PushID(lowerLabel);
            bool edited = ImGui::DragFloat("##lower", &lower, speed, -1e6f, upper, fmt);
            ImGui::PopID();
            UI::PropertyLabel(upperLabel);
            ImGui::PushID(upperLabel);
            edited |= ImGui::DragFloat("##upper", &upper, speed, lower, 1e6f, fmt);
            ImGui::PopID();
            if (edited && lower > upper)
                std::swap(lower, upper);
            return edited;
        };
        auto spring = [](float& frequency, float& damping, const char* zeroText) {
            UI::PropertyLabel("Chastota", "Prujina tebranishlari soniyada (Hz). Kattaroq - qattiqroq");
            bool edited = ImGui::DragFloat("##frequency", &frequency, 0.05f, 0.0f, 120.0f, frequency > 0.0f ? "%.2f Hz" : zeroText);
            UI::PropertyLabel("So'nish nisbati", "0 - uzoq tebranadi, 1 - tebranmasdan joyiga keladi");
            edited |= ImGui::SliderFloat("##damping", &damping, 0.0f, 2.0f, "%.2f");
            if (edited)
            {
                frequency = std::max(0.0f, frequency);
                damping = std::max(0.0f, damping);
            }
            return edited;
        };
        auto positive = [](const char* label, const char* tooltip, float& v, const char* fmt) {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            bool edited = ImGui::DragFloat("##v", &v, 0.5f, 0.0f, 1e7f, fmt);
            ImGui::PopID();
            if (edited)
                v = std::max(0.0f, v);
            return edited;
        };

        AddSection<DistanceJoint2DComponent>(ICON_FA_LINK, "Distance Joint 2D", entity, [&](DistanceJoint2DComponent& c) {
            bool changed = DrawJoint2DCommon(entity, c, Joint2DType::Distance);
            UI::PropertyHeader("Masofa", "Ikki langar orasidagi masofa: qattiq tayoq, arqon yoki prujina");
            changed |= UI::PropertyBool("Avto masofa", c.AutoConfigureDistance);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Masofa joint yaratilgandagi langarlar orasidagi masofaga teng bo'ladi");
            ImGui::BeginDisabled(c.AutoConfigureDistance);
            changed |= UI::PropertyFloat("Masofa", c.Distance, 0.01f, 0.0f, 1e5f);
            ImGui::EndDisabled();
            changed |= UI::PropertyBool("Faqat maksimal (arqon)", c.MaxDistanceOnly);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Arqon kabi: jismlar yaqinlashishi mumkin, uzoqlasha olmaydi");
            changed |= spring(c.Frequency, c.DampingRatio, "qattiq");
            DrawJoint2DStatus(c, std::format("Hozirgi masofa: {:.3f}", c.CurrentDistance).c_str());
            return changed;
        }, true, modified);

        AddSection<RevoluteJoint2DComponent>(ICON_FA_ARROWS_ROTATE, "Revolute Joint 2D", entity, [&](RevoluteJoint2DComponent& c) {
            bool changed = DrawJoint2DCommon(entity, c, Joint2DType::Revolute);
            UI::PropertyHeader("Burchak chegarasi", "Shu jismning ulangan jismga nisbatan burilishi, joint yaratilgandagi holatdan (gradus)");
            changed |= UI::PropertyBool("Cheklash", c.EnableLimit);
            ImGui::BeginDisabled(!c.EnableLimit);
            changed |= range("Pastki burchak", "Yuqori burchak", c.LowerAngle, c.UpperAngle, 0.5f, "%.1f\xc2\xb0");
            ImGui::EndDisabled();
            UI::PropertyHeader("Motor", "Jismni anchor atrofida aylantiradi (musbat - soat miliga teskari)");
            changed |= UI::PropertyBool("Motor", c.EnableMotor);
            ImGui::BeginDisabled(!c.EnableMotor);
            changed |= degrees("Tezlik", "Gradus / soniya", c.MotorSpeed, -1e5f, 1e5f);
            changed |= positive("Maks. moment", "Motor bera oladigan eng katta moment (N*m)", c.MaxMotorTorque, "%.1f N*m");
            ImGui::EndDisabled();
            DrawJoint2DStatus(c, std::format("Burchak: {:.1f}\xc2\xb0\nAylanish tezligi: {:.1f}\xc2\xb0/s", c.JointAngle, c.JointSpeed).c_str());
            return changed;
        }, true, modified);

        AddSection<PrismaticJoint2DComponent>(ICON_FA_ARROWS_LEFT_RIGHT, "Prismatic Joint 2D", entity, [&](PrismaticJoint2DComponent& c) {
            bool changed = DrawJoint2DCommon(entity, c, Joint2DType::Prismatic);
            UI::PropertyHeader("Sirpanish", "Jism ulangan jismga nisbatan faqat shu o'q bo'ylab siljiydi va aylanmaydi");
            changed |= UI::PropertyFloat2("O'q", c.Axis, 0.01f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Harakat yo'nalishi shu jismning o'z koordinatalarida (joint yaratilgandagi holatda)");
            changed |= UI::PropertyBool("Cheklash", c.EnableLimit);
            ImGui::BeginDisabled(!c.EnableLimit);
            changed |= range("Pastki chegara", "Yuqori chegara", c.LowerTranslation, c.UpperTranslation, 0.01f, "%.2f");
            ImGui::EndDisabled();
            UI::PropertyHeader("Motor", "Jismni o'q bo'ylab suradi (musbat - o'q yo'nalishida)");
            changed |= UI::PropertyBool("Motor", c.EnableMotor);
            ImGui::BeginDisabled(!c.EnableMotor);
            changed |= UI::PropertyFloat("Tezlik (birlik/s)", c.MotorSpeed, 0.05f);
            changed |= positive("Maks. kuch", "Motor bera oladigan eng katta kuch (N)", c.MaxMotorForce, "%.1f N");
            ImGui::EndDisabled();
            DrawJoint2DStatus(c, std::format("Siljish: {:.3f}\nTezlik: {:.2f} birlik/s", c.JointTranslation, c.JointSpeed).c_str());
            return changed;
        }, true, modified);

        AddSection<WeldJoint2DComponent>(ICON_FA_HAMMER, "Weld Joint 2D", entity, [&](WeldJoint2DComponent& c) {
            bool changed = DrawJoint2DCommon(entity, c, Joint2DType::Weld);
            UI::PropertyHeader("Qattiqlik", "0 Hz - jismlar bir butun; chastota bilan burchak bo'yicha prujinadek egiladi");
            changed |= spring(c.Frequency, c.DampingRatio, "qattiq");
            DrawJoint2DStatus(c, nullptr);
            return changed;
        }, true, modified);

        AddSection<WheelJoint2DComponent>(ICON_FA_GEAR, "Wheel Joint 2D", entity, [&](WheelJoint2DComponent& c) {
            bool changed = DrawJoint2DCommon(entity, c, Joint2DType::Wheel);
            UI::PropertyHeader("Osma", "Shu obyekt - g'ildirak, ulangan jism - kuzov: g'ildirak erkin aylanadi va o'q bo'ylab "
                                       "prujinada yuradi");
            changed |= UI::PropertyFloat2("O'q", c.Axis, 0.01f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Osma yo'nalishi shu jismning o'z koordinatalarida (joint yaratilgandagi holatda; odatda yuqoriga)");
            changed |= spring(c.Frequency, c.DampingRatio, "prujinasiz");
            changed |= UI::PropertyBool("Cheklash", c.EnableLimit);
            ImGui::BeginDisabled(!c.EnableLimit);
            changed |= range("Pastki chegara", "Yuqori chegara", c.LowerTranslation, c.UpperTranslation, 0.01f, "%.2f");
            ImGui::EndDisabled();
            UI::PropertyHeader("Motor", "G'ildirakni aylantiradi (musbat - soat miliga teskari)");
            changed |= UI::PropertyBool("Motor", c.EnableMotor);
            ImGui::BeginDisabled(!c.EnableMotor);
            changed |= degrees("Tezlik", "Gradus / soniya (360 - soniyasiga bir aylanish)", c.MotorSpeed, -1e5f, 1e5f);
            changed |= positive("Maks. moment", "Motor bera oladigan eng katta moment (N*m)", c.MaxMotorTorque, "%.1f N*m");
            ImGui::EndDisabled();
            DrawJoint2DStatus(c, std::format("Osma siljishi: {:.3f}\nAylanish: {:.0f}\xc2\xb0/s", c.JointTranslation, c.JointSpeed).c_str());
            return changed;
        }, true, modified);

        AddSection<Rigidbody3DComponent>(ICON_FA_ATOM, "Rigidbody 3D", entity, [&](Rigidbody3DComponent& c) {
            bool changed = false;
            const char* types[] = { "Statik", "Dinamik", "Kinematik" };
            int type = (int)c.Type;
            if (UI::PropertyCombo("Turi", type, types, 3)) { c.Type = (Rigidbody3DComponent::BodyType)type; changed = true; }
            changed |= UI::PropertyFloat("Massa", c.Mass, 0.05f, 0.001f, 100000.0f);
            changed |= UI::PropertyFloat("Gravitatsiya", c.GravityFactor, 0.05f);
            changed |= UI::PropertyFloat("Chiziqli so'nish", c.LinearDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyFloat("Burchak so'nishi", c.AngularDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Aylanmasin", c.LockRotation);
            changed |= DrawCollisionLayer("Qatlam", c.Layer, false,
                                          "Kolliderlarining to'qnashuv qatlami (2D fizika bilan bir xil qatlamlar): qaysi qatlamlarga "
                                          "tegishi loyiha matritsasida (Loyiha sozlamalari > Fizika qatlamlari). O'yin davomida ham "
                                          "o'zgartirsa bo'ladi; so'rovlar (Raycast3D, OverlapSphere3D...) qatlam niqobini oladi.");
            const bool dynamic = c.Type == Rigidbody3DComponent::BodyType::Dynamic;
            ImGui::BeginDisabled(!dynamic);
            changed |= UI::PropertyBool("Uzluksiz (CCD)", c.ContinuousCollision);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                UI::Tooltip(dynamic ? "Uzluksiz to'qnashuv: tez jism (o'q, otilgan to'p) har qadamda yo'li bo'ylab tekshiriladi: ikki qadam orasida yupqa "
                                      "devordan o'tib ketmaydi, unga urilib to'xtaydi. Qimmatroq - faqat tez va kichik jismlar uchun. "
                                      "O'yin davomida ham o'zgartirsa bo'ladi"
                                    : "Faqat dinamik jismlar uchun (kinematik jismni hech narsa to'xtatmaydi)");
            if (c.Type != Rigidbody3DComponent::BodyType::Static)
            {
                const char* modes[] = { "Uyg'oq boshlanadi", "Uxlab boshlanadi", "Hech uxlamaydi" };
                int mode = (int)c.Sleeping;
                UI::PropertyLabel("Uxlash", "Qimirlamay qolgan jism uxlaydi: tegilmaguncha hisoblanmaydi (Sahna sozlamalari > Fizika). "
                                            "Uxlab boshlanadi - urilguncha kutib turadigan minora yoki to'plam uchun; Hech uxlamaydi - "
                                            "doim hisoblanadi. Skriptdan: IsSleeping3D / WakeUp3D / PutToSleep3D");
                ImGui::PushID("sleep3d");
                if (ImGui::Combo("##v", &mode, modes, 3))
                {
                    c.Sleeping = (Rigidbody3DComponent::SleepMode)std::clamp(mode, 0, 2);
                    changed = true;
                }
                ImGui::PopID();
                if (c.RuntimeBodyID != 0xffffffff && m_Context && m_Context->GetPhysicsWorld3D())
                {
                    // While the game runs: whether it sleeps now, and a button to change that.
                    const bool sleeping = m_Context->IsSleeping3D(entity);
                    UI::PropertyLabel("Holati");
                    if (sleeping)
                        ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.95f, 1.0f), ICON_FA_MOON "  Uxlayapti");
                    else
                        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), ICON_FA_BOLT "  Uyg'oq");
                    ImGui::SameLine();
                    ImGui::PushID("sleep3dState");
                    if (ImGui::SmallButton(sleeping ? "Uyg'otish" : "Uxlatish"))
                        m_Context->SetAwake3D(entity, sleeping);
                    ImGui::PopID();
                }
            }
            return changed;
        }, true, modified);

        // What every 3D collider has after its shape: offset, the material (or its own values), trigger.
        auto collider3D = [&](auto& c) {
            bool changed = UI::PropertyFloat3("Siljish", c.Offset);
            PhysicsMaterialData own;
            own.Friction = c.Friction;
            own.Restitution = c.Restitution;
            own.RestitutionThreshold = PhysicsSurface3D::DefaultRestitutionThreshold;
            bool material = false;
            changed |= DrawPhysicsMaterialField(entity, c.MaterialPath, own, material, true);
            ImGui::BeginDisabled(material); // the material's values are used
            changed |= UI::PropertySlider("Ishqalanish", c.Friction, 0.0f, 1.0f);
            changed |= UI::PropertySlider("Sakrash", c.Restitution, 0.0f, 1.0f);
            ImGui::EndDisabled();
            changed |= UI::PropertyBool("Trigger (sensor)", c.IsSensor);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Urilmaydi - ichiga kirgan / chiqqan jism va qahramonlar skriptga OnTriggerBegin / OnTriggerEnd bo'lib "
                            "keladi (2D'dagidek). Bir jismda trigger va qattiq kolliderlar birga bo'lishi mumkin (masalan, "
                            "quti + uning atrofidagi 'yig'ish' sferasi)");
            return changed;
        };

        AddSection<BoxCollider3DComponent>(ICON_FA_CUBE, "Box Collider 3D", entity, [&](BoxCollider3DComponent& c) {
            bool changed = UI::PropertyFloat3("Yarim o'lcham", c.HalfExtents);
            return collider3D(c) || changed;
        }, true, modified);

        AddSection<SphereCollider3DComponent>(ICON_FA_CIRCLE, "Sphere Collider 3D", entity, [&](SphereCollider3DComponent& c) {
            bool changed = UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.001f, 1000.0f);
            return collider3D(c) || changed;
        }, true, modified);

        AddSection<CapsuleCollider3DComponent>(ICON_FA_CAPSULES, "Capsule Collider 3D", entity, [&](CapsuleCollider3DComponent& c) {
            bool changed = UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.001f, 1000.0f);
            changed |= UI::PropertyFloat("Yarim balandlik", c.HalfHeight, 0.01f, 0.0f, 1000.0f);
            return collider3D(c) || changed;
        }, true, modified);

        AddSection<ConvexHullCollider3DComponent>(ICON_FA_SHAPES, "Convex Hull Collider 3D", entity, [&](ConvexHullCollider3DComponent& c) {
            bool changed = DrawConvexHullCollider(entity, c);
            return collider3D(c) || changed;
        }, true, modified);

        AddSection<MeshCollider3DComponent>(ICON_FA_DRAW_POLYGON, "Mesh Collider 3D", entity, [&](MeshCollider3DComponent& c) {
            bool changed = DrawMeshCollider(entity, c);
            return collider3D(c) || changed;
        }, true, modified);

        // ---- 3D joints
        AddSection<FixedJoint3DComponent>(ICON_FA_LOCK, "Fixed Joint 3D", entity, [&](FixedJoint3DComponent& c) {
            bool changed = DrawJoint3DCommon(entity, c, Joint3DType::Fixed);
            DrawJoint3DStatus(c, nullptr);
            return changed;
        }, true, modified);

        AddSection<HingeJoint3DComponent>(ICON_FA_DOOR_OPEN, "Hinge Joint 3D", entity, [&](HingeJoint3DComponent& c) {
            bool changed = DrawJoint3DCommon(entity, c, Joint3DType::Hinge);
            UI::PropertyHeader("O'q", "Jism langar atrofida faqat shu o'q bo'ylab aylanadi");
            changed |= UI::PropertyFloat3("O'q", c.Axis, 0.01f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Aylanish o'qi shu obyektning o'z koordinatalarida (joint yaratilgandagi holatda). Eshik uchun (0, 1, 0)");
            UI::PropertyHeader("Burchak chegarasi", "Ulangan jismga nisbatan burilish, joint yaratilgandagi holatdan (gradus). "
                                                    "0 doim oraliq ichida: pastki -180..0, yuqori 0..180");
            changed |= UI::PropertyBool("Cheklash", c.EnableLimit);
            ImGui::BeginDisabled(!c.EnableLimit);
            changed |= degrees("Pastki burchak", "-180..0 gradus", c.LowerAngle, -180.0f, 0.0f);
            changed |= degrees("Yuqori burchak", "0..180 gradus", c.UpperAngle, 0.0f, 180.0f);
            ImGui::EndDisabled();
            UI::PropertyHeader("Motor", "Jismni o'q atrofida aylantiradi (musbat - o'q uchidan qaraganda soat miliga teskari)");
            changed |= UI::PropertyBool("Motor", c.EnableMotor);
            ImGui::BeginDisabled(!c.EnableMotor);
            changed |= degrees("Tezlik", "Gradus / soniya", c.MotorSpeed, -1e5f, 1e5f);
            changed |= positive("Maks. moment", "Motor bera oladigan eng katta moment (N*m)", c.MaxMotorTorque, "%.1f N*m");
            ImGui::EndDisabled();
            UI::PropertyHeader("Prujina", "Motor o'chiq bo'lsa jismni nishon burchakka tortadi (o'z-o'zidan yopiladigan eshik)");
            changed |= UI::PropertyBool("Prujina", c.EnableSpring);
            ImGui::BeginDisabled(!c.EnableSpring || c.EnableMotor);
            changed |= degrees("Nishon burchak", "Prujina tortadigan burchak (gradus, joint yaratilgandagi holatdan)", c.SpringTargetAngle, -180.0f, 180.0f);
            changed |= spring(c.SpringFrequency, c.SpringDamping, "o'chiq");
            ImGui::EndDisabled();
            DrawJoint3DStatus(c, std::format("Burchak: {:.1f}\xc2\xb0\nAylanish tezligi: {:.1f}\xc2\xb0/s", c.JointAngle, c.JointSpeed).c_str());
            return changed;
        }, true, modified);

        AddSection<DistanceJoint3DComponent>(ICON_FA_LINK, "Distance Joint 3D", entity, [&](DistanceJoint3DComponent& c) {
            bool changed = DrawJoint3DCommon(entity, c, Joint3DType::Distance);
            UI::PropertyHeader("Masofa", "Langarlar orasidagi masofa shu oraliqda qoladi: teng - qattiq tayoq, arqon - faqat maksimal");
            changed |= UI::PropertyBool("Avto masofa", c.AutoConfigureDistance);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Masofa joint yaratilgandagi langarlar orasidagi masofaga teng bo'ladi (arqon: maksimal shu)");
            ImGui::BeginDisabled(c.AutoConfigureDistance);
            ImGui::BeginDisabled(c.MaxDistanceOnly);
            if (UI::PropertyFloat("Minimal masofa", c.MinDistance, 0.01f, 0.0f, 1e5f))
            {
                c.MinDistance = std::max(0.0f, c.MinDistance);
                c.MaxDistance = std::max(c.MaxDistance, c.MinDistance);
                changed = true;
            }
            ImGui::EndDisabled();
            if (UI::PropertyFloat("Maksimal masofa", c.MaxDistance, 0.01f, 0.0f, 1e5f))
            {
                c.MaxDistance = std::max(0.0f, c.MaxDistance);
                c.MinDistance = std::min(c.MinDistance, c.MaxDistance);
                changed = true;
            }
            ImGui::EndDisabled();
            changed |= UI::PropertyBool("Faqat maksimal (arqon)", c.MaxDistanceOnly);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Arqon kabi: jismlar yaqinlashishi mumkin, uzoqlasha olmaydi");
            changed |= spring(c.Frequency, c.DampingRatio, "qattiq");
            DrawJoint3DStatus(c, std::format("Hozirgi masofa: {:.3f}", c.CurrentDistance).c_str());
            return changed;
        }, true, modified);

        AddSection<SliderJoint3DComponent>(ICON_FA_ARROWS_LEFT_RIGHT, "Slider Joint 3D", entity, [&](SliderJoint3DComponent& c) {
            bool changed = DrawJoint3DCommon(entity, c, Joint3DType::Slider);
            UI::PropertyHeader("Sirpanish", "Jism ulangan jismga nisbatan faqat shu o'q bo'ylab siljiydi va aylanmaydi");
            changed |= UI::PropertyFloat3("O'q", c.Axis, 0.01f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Harakat yo'nalishi shu obyektning o'z koordinatalarida (joint yaratilgandagi holatda)");
            changed |= UI::PropertyBool("Cheklash", c.EnableLimit);
            ImGui::BeginDisabled(!c.EnableLimit);
            UI::PropertyLabel("Pastki chegara", "Boshlang'ich joydan orqaga (<= 0)");
            if (ImGui::DragFloat("##lowerSlide", &c.LowerTranslation, 0.01f, -1e5f, 0.0f, "%.2f"))
            {
                c.LowerTranslation = std::min(0.0f, c.LowerTranslation);
                changed = true;
            }
            UI::PropertyLabel("Yuqori chegara", "Boshlang'ich joydan o'q yo'nalishida (>= 0)");
            if (ImGui::DragFloat("##upperSlide", &c.UpperTranslation, 0.01f, 0.0f, 1e5f, "%.2f"))
            {
                c.UpperTranslation = std::max(0.0f, c.UpperTranslation);
                changed = true;
            }
            ImGui::EndDisabled();
            UI::PropertyHeader("Motor", "Jismni o'q bo'ylab suradi (musbat - o'q yo'nalishida)");
            changed |= UI::PropertyBool("Motor", c.EnableMotor);
            ImGui::BeginDisabled(!c.EnableMotor);
            changed |= UI::PropertyFloat("Tezlik (birlik/s)", c.MotorSpeed, 0.05f);
            changed |= positive("Maks. kuch", "Motor bera oladigan eng katta kuch (N)", c.MaxMotorForce, "%.1f N");
            ImGui::EndDisabled();
            DrawJoint3DStatus(c, std::format("Siljish: {:.3f}\nTezlik: {:.2f} birlik/s", c.JointTranslation, c.JointSpeed).c_str());
            return changed;
        }, true, modified);

        AddSection<ConeJoint3DComponent>(ICON_FA_BONE, "Cone Joint 3D", entity, [&](ConeJoint3DComponent& c) {
            bool changed = DrawJoint3DCommon(entity, c, Joint3DType::Cone);
            UI::PropertyHeader("O'q", "Sharsimon sharnir: jismlar langar nuqtasida tutashadi, jism o'qi konus ichida "
                                      "og'adi va o'z atrofida chegaralangan burchakka buraladi (yelka, son, bo'yin)");
            changed |= UI::PropertyFloat3("O'q", c.Axis, 0.01f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("Buralish o'qi shu obyektning o'z koordinatalarida (joint yaratilgandagi holatda) - qo'l yoki oyoq bo'ylab. "
                            "Konus shu o'q joint yaratilgandagi yo'nalish atrofida");
            UI::PropertyHeader("Chegaralar", "Joint yaratilgandagi holatdan (gradus): u konus o'rtasi va buralish 0");
            changed |= degrees("Konus burchagi", "O'q boshlang'ich yo'nalishidan shuncha og'a oladi (0..180, 180 - erkin)", c.SwingLimit, 0.0f, 180.0f);
            changed |= degrees("Buralish (pastki)", "-180..0 gradus", c.TwistLower, -180.0f, 0.0f);
            changed |= degrees("Buralish (yuqori)", "0..180 gradus", c.TwistUpper, 0.0f, 180.0f);
            changed |= positive("Ishqalanish momenti", "Aylanishga qarshilik (N*m): ragdoll qo'l-oyoqlari erkin silkinmaydi. 0 - yo'q",
                                c.FrictionTorque, c.FrictionTorque > 0.0f ? "%.2f N*m" : "yo'q");
            DrawJoint3DStatus(c, std::format("Og'ish: {:.1f}\xc2\xb0\nBuralish: {:.1f}\xc2\xb0", c.SwingAngle, c.TwistAngle).c_str());
            return changed;
        }, true, modified);

        AddSection<CharacterController3DComponent>(ICON_FA_PERSON_RUNNING, "Character Controller 3D", entity, [&](CharacterController3DComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: qahramon fizikada yo'q (faollashganda joriy joyida qayta yaratiladi)");
            bool changed = UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.05f, 10.0f);
            changed |= UI::PropertyFloat(EditorText::Choose("Balandlik", "Height"), c.Height, 0.01f, 0.2f, 20.0f);
            changed |= UI::PropertySlider("Maks. qiyalik (°)", c.MaxSlope, 0.0f, 89.0f);
            changed |= UI::PropertyFloat("Zinapoya balandligi", c.StepHeight, 0.01f, 0.0f, 2.0f);
            changed |= UI::PropertyFloat("Massa", c.Mass, 0.5f, 1.0f, 10000.0f);
            changed |= UI::PropertyFloat("Gravitatsiya", c.GravityScale, 0.05f, 0.0f, 10.0f);
            changed |= UI::PropertyBool("Yerga yopishish", c.StickToFloor);
            changed |= DrawCollisionLayer("Qatlam", c.Layer, false,
                                          "Qahramonning to'qnashuv qatlami: qaysi jismlarga urilishi / ustida turishi loyiha "
                                          "matritsasida (Loyiha sozlamalari > Fizika qatlamlari)");
            UI::PropertyHeader("Itarish", "Yurib borib tekkan dinamik jismlarni (qutilar, to'plar) o'z tezligiga yaqin tezlikda suradi");
            changed |= UI::PropertyBool("Jismlarni itaradi", c.PushBodies);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                UI::Tooltip("O'chiq: dinamik jismlar qahramon uchun devor (ular baribir qahramonni itarishi mumkin)");
            ImGui::BeginDisabled(!c.PushBodies);
            UI::PropertyLabel("Itarish kuchi", "Qahramon jismni itara oladigan eng katta kuch (N). 100 N - bir necha o'n kg'lik qutini "
                                               "suradi; og'ir jism (yerga ishqalanishi kuchdan katta) uchun ko'proq kerak");
            if (ImGui::DragFloat("##pushForce", &c.PushForce, 5.0f, 0.0f, 1e6f, "%.0f N"))
            {
                c.PushForce = std::max(0.0f, c.PushForce);
                changed = true;
            }
            ImGui::EndDisabled();
            UI::PropertyLabel("Holat");
            ImGui::TextDisabled(!entity.IsActiveInHierarchy() ? "Nofaol" : (c.Grounded ? "Yerda" : "Havoda"));
            if (c.RuntimeID != 0xffffffff)
            {
                // The velocity it really moves with (the viewport's debug drawing shows it as an arrow).
                UI::PropertyLabel("Tezlik", "Oxirgi fizika qadamidagi haqiqiy tezlik (to'qnashuvlardan keyin). Viewport'da o'q bilan: "
                                            "Debug chizish > Personaj tezligi");
                ImGui::TextDisabled("%.2f m/s  (%.1f, %.1f, %.1f)", glm::length(c.Velocity), c.Velocity.x, c.Velocity.y, c.Velocity.z);
            }
            return changed;
        }, true, modified);

        AddSection<TilemapComponent>(ICON_FA_TABLE_CELLS, "Tilemap", entity, [&](TilemapComponent& c) {
            return DrawTilemap(entity, c);
        }, true, modified);

        AddSection<ParticleSystemComponent>(ICON_FA_WAND_MAGIC_SPARKLES, "Particle System", entity, [&](ParticleSystemComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: particle'lar to'xtatilgan");
            return DrawParticleSystem(entity, c);
        }, true, modified);

        AddSection<ParallaxComponent>(ICON_FA_MOUNTAIN, "Parallax", entity, [&](ParallaxComponent& c) {
            bool changed = false;
            changed |= UI::PropertyFloat2("Koeffitsiyent", c.Factor, 0.01f);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Kamera bilan birga qancha siljiydi: 0 - oddiy obyekt, 1 - ekranda qotib turadi (juda uzoq), "
                            "0.5 - kameradan ikki baravar sekin (uzoq fon), manfiy - qarama-qarshi (yaqin old plan)");
            changed |= UI::PropertyBool("Gorizontal takrorlash", c.RepeatX);
            changed |= UI::PropertyBool("Vertikal takrorlash", c.RepeatY);
            if (c.RepeatX || c.RepeatY)
            {
                if (UI::PropertyFloat2("Oraliq (0 = o'lchami)", c.Spacing, 0.05f))
                {
                    c.Spacing = glm::max(c.Spacing, glm::vec2(0.0f));
                    changed = true;
                }
                if (!entity.HasComponent<SpriteRendererComponent>())
                {
                    UI::PropertyLabel("");
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Takrorlash Sprite Renderer'ni chizadi");
                }
            }
            UI::PropertyLabel("");
            ImGui::TextDisabled("O'yin kamerasi bilan ishlaydi (editorda: Ko'rinish > Parallax)");
            return changed;
        }, true, modified);

        AddSection<UITransformComponent>(ICON_FA_TABLE_CELLS, "UI Transform", entity, [&](UITransformComponent& c) {
            bool changed = false;
            // Anchor presets: the 9 points, the 7 stretches; the element stays where it is.
            struct AnchorPreset { const char* Name; glm::vec2 Min, Max; };
            static const AnchorPreset s_Presets[] = {
                { "Yuqori-chap", { 0.0f, 1.0f }, { 0.0f, 1.0f } },   { "Yuqori", { 0.5f, 1.0f }, { 0.5f, 1.0f } },
                { "Yuqori-o'ng", { 1.0f, 1.0f }, { 1.0f, 1.0f } },   { "Chap", { 0.0f, 0.5f }, { 0.0f, 0.5f } },
                { "Markaz", { 0.5f, 0.5f }, { 0.5f, 0.5f } },        { "O'ng", { 1.0f, 0.5f }, { 1.0f, 0.5f } },
                { "Past-chap", { 0.0f, 0.0f }, { 0.0f, 0.0f } },     { "Past", { 0.5f, 0.0f }, { 0.5f, 0.0f } },
                { "Past-o'ng", { 1.0f, 0.0f }, { 1.0f, 0.0f } },
                { "Yuqorida cho'zilgan (chapdan o'ngga)", { 0.0f, 1.0f }, { 1.0f, 1.0f } },
                { "O'rtada cho'zilgan (chapdan o'ngga)", { 0.0f, 0.5f }, { 1.0f, 0.5f } },
                { "Pastda cho'zilgan (chapdan o'ngga)", { 0.0f, 0.0f }, { 1.0f, 0.0f } },
                { "Chapda cho'zilgan (pastdan yuqoriga)", { 0.0f, 0.0f }, { 0.0f, 1.0f } },
                { "Markazda cho'zilgan (pastdan yuqoriga)", { 0.5f, 0.0f }, { 0.5f, 1.0f } },
                { "O'ngda cho'zilgan (pastdan yuqoriga)", { 1.0f, 0.0f }, { 1.0f, 1.0f } },
                { "To'liq cho'zilgan (butun ota)", { 0.0f, 0.0f }, { 1.0f, 1.0f } },
            };
            constexpr int presetCount = (int)(sizeof(s_Presets) / sizeof(s_Presets[0]));
            int current = presetCount; // "custom"
            for (int i = 0; i < presetCount; i++)
                if (c.AnchorMin == s_Presets[i].Min && c.AnchorMax == s_Presets[i].Max)
                    current = i;
            const bool driven = m_Context && m_Context->IsUILayoutDriven(entity);
            if (driven)
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.45f, 0.75f, 0.95f, 1.0f), ICON_FA_LIST "  Ota obyektning layout guruhi joylashtiradi");
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Langar va pozitsiya hisobga olinmaydi; o'lcham - guruh boshqarmasa. Guruhdan chiqarish: UI Layout Element > Layoutni e'tiborsiz qoldirish");
            }
            UI::PropertyLabel("Langar (anchor)", "Ota to'rtburchakning qaysi nuqtasiga (yoki qaysi chetlari orasiga cho'zilib) bog'langan. "
                                                 "Tanlanganda obyekt joyida qoladi; Shift bilan - langarga joylashadi (pivot ham)");
            ImGui::PushID("anchorPreset");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##preset", current < presetCount ? s_Presets[current].Name : "Boshqa (qo'lda)"))
            {
                for (int i = 0; i < presetCount; i++)
                {
                    if (i == 9)
                        ImGui::Separator();
                    if (ImGui::Selectable(s_Presets[i].Name, i == current) && m_Context)
                    {
                        const bool snap = ImGui::GetIO().KeyShift;
                        m_Context->SetUIAnchors(entity, s_Presets[i].Min, s_Presets[i].Max, !snap);
                        if (snap)
                        {
                            // At the anchors: the pivot on the anchor point, no offset (stretched axes fill the span).
                            for (int axis = 0; axis < 2; axis++)
                                if (c.AnchorMin[axis] == c.AnchorMax[axis])
                                {
                                    c.Pivot[axis] = c.AnchorMin[axis];
                                    c.Position[axis] = 0.0f;
                                }
                                else
                                {
                                    c.Position[axis] = 0.0f;
                                    c.Size[axis] = 0.0f;
                                }
                        }
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();

            // Per axis: a point anchor shows position and size, a stretch the two edge offsets.
            auto axisRow = [&](int axis) {
                const bool stretched = c.AnchorMin[axis] != c.AnchorMax[axis];
                if (!stretched)
                {
                    glm::vec2 v{ c.Position[axis], c.Size[axis] };
                    if (UI::PropertyFloat2(axis == 0 ? "X | Kenglik" : "Y | Balandlik", v, 1.0f))
                    {
                        c.Position[axis] = v.x;
                        c.Size[axis] = v.y;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(axis == 0 ? "Langardan pivotgacha (o'ngga +) va kenglik, UI birliklarida"
                                              : "Langardan pivotgacha (yuqoriga +) va balandlik, UI birliklarida");
                }
                else
                {
                    glm::vec2 offsetMin = c.GetOffsetMin(), offsetMax = c.GetOffsetMax();
                    glm::vec2 v{ offsetMin[axis], -offsetMax[axis] };
                    if (UI::PropertyFloat2(axis == 0 ? "Chap | O'ng" : "Past | Yuqori", v, 1.0f))
                    {
                        offsetMin[axis] = v.x;
                        offsetMax[axis] = -v.y;
                        c.SetOffsets(offsetMin, offsetMax);
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Langarlar orasidagi chetlardan ichkariga masofa (UI birliklari): 0 - aynan ota chetida");
                }
            };
            axisRow(0);
            axisRow(1);
            if (ImGui::TreeNodeEx(Tr("Langar va pivot (qo'lda)"), ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                glm::vec2 aMin = c.AnchorMin, aMax = c.AnchorMax, pivot = c.Pivot;
                if (UI::PropertyFloat2("Langar min", aMin, 0.01f) && m_Context)
                {
                    m_Context->SetUIAnchors(entity, glm::clamp(aMin, glm::vec2(0.0f), glm::vec2(1.0f)), c.AnchorMax, true);
                    changed = true;
                }
                if (UI::PropertyFloat2("Langar max", aMax, 0.01f) && m_Context)
                {
                    m_Context->SetUIAnchors(entity, c.AnchorMin, glm::clamp(aMax, glm::vec2(0.0f), glm::vec2(1.0f)), true);
                    changed = true;
                }
                if (UI::PropertyFloat2("Pivot", pivot, 0.01f) && m_Context)
                {
                    m_Context->SetUIPivot(entity, pivot, true);
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("O'z to'rtburchagining 0..1 nuqtasi: pozitsiya shu nuqtani joylaydi (0.5, 0.5 - markaz). O'zgarganda obyekt joyida qoladi");
                ImGui::TreePop();
            }
            changed |= UI::PropertyFloat("Shrift o'lchami", c.FontSize, 0.5f, 0.0f, 500.0f);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Matn balandligi UI birliklarida (0 - to'rtburchak balandligi)");
            changed |= UI::PropertyInt("Tartib", c.Order);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Aka-ukalari orasida chizilish tartibi: kattasi ustida. Bolalar doim otasi ustida chiziladi");
            // The look (not the layout): what UI tweens animate; open by itself when it is not the plain one.
            const bool plainLook = c.Scale == glm::vec2(1.0f) && c.VisualOffset == glm::vec2(0.0f) && c.Alpha == 1.0f;
            if (ImGui::TreeNodeEx(Tr("Ko'rinish (animatsiya)"), ImGuiTreeNodeFlags_SpanAvailWidth | (plainLook ? 0 : ImGuiTreeNodeFlags_DefaultOpen)))
            {
                changed |= UI::PropertyFloat2("Masshtab", c.Scale, 0.01f);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Pivot atrofida shuncha marta katta chiziladi (bolalari bilan); joylashuvga ta'sir qilmaydi");
                changed |= UI::PropertyFloat2("Siljish", c.VisualOffset, 1.0f);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Shuncha UI birligi nariroqda chiziladi (bolalari bilan) - layout guruhidagi elementni ham surish mumkin");
                changed |= UI::PropertySlider("Shaffoflik (alpha)", c.Alpha, 0.0f, 1.0f);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("O'zi va bolalari shu shaffoflikda chiziladi; 0 bo'lsa sichqonchani ham olmaydi");
                if (!plainLook)
                {
                    UI::PropertyLabel("");
                    if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT " Asl holiga")))
                    {
                        c.Scale = glm::vec2(1.0f);
                        c.VisualOffset = glm::vec2(0.0f);
                        c.Alpha = 1.0f;
                        changed = true;
                    }
                }
                ImGui::TreePop();
            }
            if (m_Context)
            {
                const UIRect r = m_Context->GetUIRect(entity);
                const float scale = m_Context->GetUIScale(entity);
                UI::PropertyLabel("");
                if (scale != 1.0f)
                    ImGui::TextDisabled("%.0f x %.0f  (%.0f, %.0f)   masshtab %.2f", r.GetWidth(), r.GetHeight(), r.Min.x, r.Min.y, scale);
                else
                    ImGui::TextDisabled("%.0f x %.0f  (%.0f, %.0f)", r.GetWidth(), r.GetHeight(), r.Min.x, r.Min.y);
            }
            return changed;
        }, true, modified);

        AddSection<UICanvasComponent>(ICON_FA_EXPAND, "UI Canvas", entity, [&](UICanvasComponent& c) {
            bool changed = false;
            static const char* const s_Modes[] = { "Doimiy piksel o'lchami", "Ekran o'lchami bilan masshtablash" };
            int mode = (int)c.ScaleMode;
            if (UI::PropertyCombo("Masshtab rejimi", mode, s_Modes, 2)) { c.ScaleMode = (UIScaleMode)mode; changed = true; }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Bu obyekt va uning ostidagi butun UI: doimiy - 1 UI birligi = koeffitsiyent px; ekran bilan - UI mo'ljal "
                            "o'lchamida chiziladi va ekran kattalashsa / kichraysa birga o'zgaradi");
            if (c.ScaleMode == UIScaleMode::ConstantPixelSize)
            {
                if (UI::PropertyFloat("Koeffitsiyent", c.ScaleFactor, 0.01f, 0.01f, 20.0f, "%.2f"))
                {
                    c.ScaleFactor = std::max(0.01f, c.ScaleFactor);
                    changed = true;
                }
            }
            else
            {
                if (UI::PropertyFloat2("Mo'ljal o'lcham (px)", c.ReferenceResolution, 1.0f))
                {
                    c.ReferenceResolution = glm::max(c.ReferenceResolution, glm::vec2(1.0f));
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("UI qaysi ekran o'lchami uchun yasalgan (masalan 1920 x 1080)");
                static const char* const s_Match[] = { "Kenglik yoki balandlikka moslash", "Kengaytirish (doim sig'adi)", "Qisqartirish (doim to'ldiradi)" };
                int match = (int)c.MatchMode;
                if (UI::PropertyCombo("Moslash", match, s_Match, 3)) { c.MatchMode = (UIScreenMatchMode)match; changed = true; }
                if (c.MatchMode == UIScreenMatchMode::MatchWidthOrHeight)
                {
                    changed |= UI::PropertySlider("Kenglik <-> balandlik", c.MatchWidthOrHeight, 0.0f, 1.0f);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("0 - kenglikka qarab, 1 - balandlikka qarab, 0.5 - ikkalasi teng (logarifmik)");
                }
            }
            if (m_Context)
            {
                const glm::vec2 view = m_Context->GetViewportSize();
                const float scale = c.ComputeScale(view);
                UI::PropertyLabel("");
                ImGui::TextDisabled("Hozir: %.0f x %.0f px -> masshtab %.3f (%.0f x %.0f UI)", view.x, view.y, scale, view.x / scale, view.y / scale);
            }
            if (m_Context && m_Context->GetParent(entity))
                for (Entity a = m_Context->GetParent(entity); a; a = m_Context->GetParent(a))
                    if (a.HasComponent<UICanvasComponent>())
                    {
                        UI::PropertyLabel("");
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Ichki canvas: tashqi canvas masshtabi ishlatiladi");
                        break;
                    }
            return changed;
        }, true, modified);

        AddSection<UILayoutGroupComponent>(ICON_FA_LIST, "UI Layout Group", entity, [&](UILayoutGroupComponent& c) {
            bool changed = false;
            static const char* const s_Types[] = { "Gorizontal (qator)", "Vertikal (ustun)", "Panjara (grid)" };
            int type = (int)c.Type;
            if (UI::PropertyCombo("Turi", type, s_Types, 3)) { c.Type = (UILayoutType)type; changed = true; }
            if (ImGui::IsItemHovered())
                UI::Tooltip("UI bolalarini ierarxiya tartibida joylaydi (nofaollari tashlab ketiladi)");
            changed |= UI::PropertyFloat4("Ichki chekka (C, O', Y, P)", c.Padding, 0.5f);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Chap, o'ng, yuqori, past - guruh chetidan ichkariga, UI birliklarida");
            static const char* const s_Align[] = { "Yuqori-chap", "Yuqori", "Yuqori-o'ng", "Chap", "Markaz", "O'ng", "Past-chap", "Past", "Past-o'ng" };
            changed |= UI::PropertyCombo("Tekislash", c.ChildAlignment, s_Align, 9);
            if (c.Type != UILayoutType::Grid)
            {
                changed |= UI::PropertyFloat("Oraliq", c.Spacing, 0.5f);
                changed |= UI::PropertyBool("Teskari tartib", c.ReverseArrangement);
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText(Tr("Bolalar o'lchami"));
                changed |= UI::PropertyBool("Kenglikni boshqarish", c.ControlChildWidth);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Guruh bolalar kengligini belgilaydi (UI Layout Element: min / afzal / moslashuvchan; yo'q bo'lsa o'z o'lchami)");
                changed |= UI::PropertyBool("Balandlikni boshqarish", c.ControlChildHeight);
                changed |= UI::PropertyBool("Kenglik bo'yicha yoyish", c.ChildForceExpandWidth);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Ortib qolgan joy bolalarga bo'linadi (boshqarilmasa - ular orasiga)");
                changed |= UI::PropertyBool("Balandlik bo'yicha yoyish", c.ChildForceExpandHeight);
            }
            else
            {
                if (UI::PropertyFloat2("Katak o'lchami", c.CellSize, 1.0f)) { c.CellSize = glm::max(c.CellSize, glm::vec2(0.0f)); changed = true; }
                changed |= UI::PropertyFloat2("Oraliq (x, y)", c.GridSpacing, 0.5f);
                static const char* const s_Corners[] = { "Yuqori-chap", "Yuqori-o'ng", "Past-chap", "Past-o'ng" };
                changed |= UI::PropertyCombo("Boshlanish burchagi", c.StartCorner, s_Corners, 4);
                changed |= UI::PropertyBool("Avval ustunlar", c.StartAxisVertical);
                static const char* const s_Constraints[] = { "Moslashuvchan (sig'gancha)", "Ustunlar soni", "Qatorlar soni" };
                int constraint = (int)c.Constraint;
                if (UI::PropertyCombo("Cheklov", constraint, s_Constraints, 3)) { c.Constraint = (UIGridConstraint)constraint; changed = true; }
                if (c.Constraint != UIGridConstraint::Flexible && UI::PropertyInt("Soni", c.ConstraintCount))
                {
                    c.ConstraintCount = std::max(1, c.ConstraintCount);
                    changed = true;
                }
            }
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText(Tr("O'z o'lchami"));
            changed |= UI::PropertyBool("Kenglik mazmunga mos", c.FitWidth);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Guruh kengligi bolalari talab qilgan (afzal) o'lchamga teng bo'ladi, pivot atrofida");
            changed |= UI::PropertyBool("Balandlik mazmunga mos", c.FitHeight);
            if (!entity.HasComponent<UITransformComponent>())
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  UI Transform kerak");
            }
            return changed;
        }, true, modified);

        AddSection<UILayoutElementComponent>(ICON_FA_ARROWS_LEFT_RIGHT, "UI Layout Element", entity, [&](UILayoutElementComponent& c) {
            bool changed = UI::PropertyBool("Layoutni e'tiborsiz qoldirish", c.IgnoreLayout);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ota guruh bu obyektni joylamaydi: o'z UI Transform'i bo'yicha turadi");
            if (!c.IgnoreLayout)
            {
                // -1 = not set: a checkbox per value.
                auto optional = [&](const char* label, glm::vec2& v, const char* tooltip, float fallback) {
                    UI::PropertyLabel(label, tooltip);
                    ImGui::PushID(label);
                    for (int axis = 0; axis < 2; axis++)
                    {
                        ImGui::PushID(axis);
                        bool set = v[axis] >= 0.0f;
                        if (ImGui::Checkbox("##set", &set))
                        {
                            v[axis] = set ? fallback : -1.0f;
                            changed = true;
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(std::max(30.0f, ImGui::GetContentRegionAvail().x * (axis == 0 ? 0.5f : 1.0f) - 4.0f));
                        ImGui::BeginDisabled(!set);
                        float value = std::max(v[axis], 0.0f);
                        if (ImGui::DragFloat("##v", &value, 0.5f, 0.0f, 100000.0f, axis == 0 ? "x: %.1f" : "y: %.1f") && set)
                        {
                            v[axis] = std::max(0.0f, value);
                            changed = true;
                        }
                        ImGui::EndDisabled();
                        if (axis == 0)
                            ImGui::SameLine();
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                };
                optional("Minimal", c.MinSize, "Guruh siqqanda shundan kichraymaydi (belgilanmagan: 0)", 0.0f);
                optional("Afzal", c.PreferredSize, "Joy yetganda shu o'lcham (belgilanmagan: o'z o'lchami)", 100.0f);
                optional("Moslashuvchan", c.FlexibleSize, "Ortib qolgan joydan ulushi (1 va 2: ikkinchisi ikki baravar ko'p oladi)", 1.0f);
                UI::PropertyLabel("");
                ImGui::TextDisabled("Guruh o'lchamni boshqarganda hisobga olinadi");
            }
            return changed;
        }, true, modified);

        AddSection<UIAnimationComponent>(ICON_FA_WAND_MAGIC_SPARKLES, "UI Animation", entity, [&](UIAnimationComponent& c) {
            bool changed = false;
            static const char* const s_Presets[] = { "Yo'q", "Paydo bo'lish (fade)", "Kattalashib chiqish (pop)", "Chapdan", "O'ngdan", "Yuqoridan",
                                                     "Pastdan" };
            static const char* const s_HidePresets[] = { "Yo'q", "So'nish (fade)", "Kichrayib yo'qolish", "Chapga", "O'ngga", "Yuqoriga", "Pastga" };
            static const char* const s_Eases[] = { "Linear", "InQuad", "OutQuad", "InOutQuad", "OutCubic", "InOutCubic", "OutBack", "OutElastic", "OutBounce" };
            int show = (int)c.Show, hide = (int)c.Hide;
            if (UI::PropertyCombo("Ko'rinish", show, s_Presets, 7)) { c.Show = (UIAnimPreset)show; changed = true; }
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'yin paytida obyekt faollashganda (ShowUI yoki SetActive) shu animatsiya bilan chiqadi");
            if (c.Show != UIAnimPreset::None)
            {
                changed |= UI::PropertyBool("Boshlanishda ham", c.ShowAtStart);
                changed |= UI::PropertyFloat("Kechikish (s)", c.ShowDelay, 0.01f, 0.0f, 10.0f);
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Ko'rinish animatsiyasi shuncha kutib boshlanadi - menyu tugmalari ketma-ket chiqishi uchun");
                int ease = (int)c.ShowEase;
                if (UI::PropertyCombo("Ko'rinish egri chizig'i", ease, s_Eases, EaseCount)) { c.ShowEase = (Ease)ease; changed = true; }
            }
            if (UI::PropertyCombo("Yashirish", hide, s_HidePresets, 7)) { c.Hide = (UIAnimPreset)hide; changed = true; }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Scene::HideUI / skriptdagi HideUI(panel) shu animatsiyani o'ynab, so'ng obyektni o'chiradi");
            if (c.Hide != UIAnimPreset::None)
            {
                int ease = (int)c.HideEase;
                if (UI::PropertyCombo("Yashirish egri chizig'i", ease, s_Eases, EaseCount)) { c.HideEase = (Ease)ease; changed = true; }
            }
            if (c.Show != UIAnimPreset::None || c.Hide != UIAnimPreset::None)
                changed |= UI::PropertyFloat("Davomiylik (s)", c.Duration, 0.01f, 0.0f, 10.0f);
            const auto slides = [](UIAnimPreset p) { return p >= UIAnimPreset::SlideLeft; };
            if (slides(c.Show) || slides(c.Hide))
                changed |= UI::PropertyFloat("Surilish masofasi", c.SlideDistance, 1.0f, 0.0f, 10000.0f);
            UI::PropertyHeader("Sichqoncha javobi", "Shu obyektdagi tugma / belgilash / slayder / matn maydoni ustiga kelinganda kattalashadi, bosilganda kichrayadi (1 - o'chiq)");
            changed |= UI::PropertyFloat("Ustida masshtab", c.HoverScale, 0.005f, 0.1f, 3.0f);
            changed |= UI::PropertyFloat("Bosilganda masshtab", c.PressScale, 0.005f, 0.1f, 3.0f);
            changed |= UI::PropertyFloat("O'tish vaqti (s)", c.FeedbackDuration, 0.005f, 0.0f, 2.0f);
            if (m_Context && m_Context->IsRunning())
            {
                UI::PropertyLabel("");
                if (c.Hiding)
                    ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.4f, 1.0f), "Yashirilmoqda...");
                else if (m_Context->IsUITweening(entity))
                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_WAND_MAGIC_SPARKLES "  Animatsiya o'ynamoqda");
                else
                    ImGui::TextDisabled("Tinch (masshtab %.2f)", c.FeedbackScale);
            }
            return changed;
        }, true, modified);

        AddSection<UINavigationComponent>(ICON_FA_GAMEPAD, "UI Navigation", entity, [&](UINavigationComponent& c) {
            bool changed = false;
            static const char* const s_Modes[] = { "Avtomatik (eng yaqini)", "Faqat gorizontal", "Faqat vertikal", "Aniq (qo'lda)", "Yo'q" };
            int mode = (int)c.NavigationMode;
            if (UI::PropertyCombo("Rejim", mode, s_Modes, 5)) { c.NavigationMode = (UINavigationComponent::Mode)mode; changed = true; }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Strelkalar / D-pad / stik bilan bu vidjetdan qayerga o'tiladi. Yo'q - navigatsiya unga kelmaydi (sichqoncha bilan tanlanadi)");
            if (c.NavigationMode == UINavigationComponent::Mode::Explicit)
            {
                changed |= DrawEntityRefField("Yuqoriga", c.Up, "Yuqori strelka bosilganda tanlanadigan vidjet (bo'sh - hech qayer)");
                changed |= DrawEntityRefField("Pastga", c.Down, "Past strelka bosilganda tanlanadigan vidjet");
                changed |= DrawEntityRefField("Chapga", c.Left, "Chap strelka bosilganda tanlanadigan vidjet");
                changed |= DrawEntityRefField("O'ngga", c.Right, "O'ng strelka bosilganda tanlanadigan vidjet");
            }
            changed |= UI::PropertyBool("Faollashganda tanlash", c.SelectOnEnable);
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'yin paytida faollashganda (boshlanishda ham) shu vidjet tanlanadi - menyuning birinchi tugmasi");
            changed |= UI::PropertyBool("Ramka ko'rsatish", c.ShowHighlight);
            if (c.ShowHighlight)
                changed |= UI::PropertyColor4("Ramka rangi", c.HighlightColor);
            if (m_Context && m_Context->IsRunning())
            {
                UI::PropertyLabel("");
                if (m_Context->GetSelectedUI() == entity)
                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_GAMEPAD "  Tanlangan");
                else
                    ImGui::TextDisabled("Tanlanmagan");
            }
            UI::PropertyLabel("");
            ImGui::TextDisabled("Enter / A - bosish, Esc / B - OnCancel, Tab - keyingisi");
            return changed;
        }, true, modified);

        AddSection<UIButtonComponent>(ICON_FA_HAND_POINTER, "UI Button", entity, [&](UIButtonComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: tugma ko'rinmaydi va bosilmaydi");
            bool changed = UI::PropertyColor4("Oddiy", c.NormalColor);
            changed |= UI::PropertyColor4("Ustida (hover)", c.HoverColor);
            changed |= UI::PropertyColor4("Bosilgan", c.PressedColor);
            changed |= UI::PropertyBool("Faol", c.Interactable);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText(Tr("Bosilganda"));
            changed |= UI::PropertyString("Xabar nomi", c.ClickMessage);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Bosilganda shu nomli skript xabari yuboriladi (bo'sh - yo'q): AddMessageHandler(\"Nomi\", ...) yoki OnMessage");
            if (!c.ClickMessage.empty())
                changed |= DrawEntityRefField("Qabul qiluvchi", c.ClickTarget, "Xabar oladigan obyekt (bo'sh - barcha skriptlar, Broadcast)");
            UI::PropertyLabel("");
            ImGui::TextDisabled("Skriptda: OnClick(), .Clicked; hodisa: \"UIClick\"");
            return changed;
        }, true, modified);

        AddSection<UIImageComponent>(ICON_FA_IMAGE, "UI Image", entity, [&](UIImageComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: rasm ko'rinmaydi");
            if (!entity.HasComponent<UITransformComponent>())
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  UI Transform kerak");
            }
            return DrawUIImage(entity, c);
        }, true, modified);

        static const char* const s_UIDirectionNames[] = { "Chapdan o'ngga", "O'ngdan chapga", "Pastdan yuqoriga", "Yuqoridan pastga" };

        AddSection<UISliderComponent>(ICON_FA_SLIDERS, "UI Slider", entity, [&](UISliderComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: slayder ko'rinmaydi va surilmaydi");
            bool changed = false;
            const float step = c.WholeNumbers ? 1.0f : 0.01f;
            if (UI::PropertyFloat("Minimum", c.MinValue, step, 0.0f, 0.0f, c.WholeNumbers ? "%.0f" : "%.3f"))
            {
                c.Value = UIMath::ClampValue(c.Value, c.MinValue, c.MaxValue, c.WholeNumbers);
                changed = true;
            }
            if (UI::PropertyFloat("Maksimum", c.MaxValue, step, 0.0f, 0.0f, c.WholeNumbers ? "%.0f" : "%.3f"))
            {
                c.Value = UIMath::ClampValue(c.Value, c.MinValue, c.MaxValue, c.WholeNumbers);
                changed = true;
            }
            UI::PropertyLabel("Qiymat", "Skriptdan: GetScene().SetSliderValue(entity, qiymat) - OnValueChanged ham chaqiriladi");
            ImGui::PushID("value");
            float lo = std::min(c.MinValue, c.MaxValue), hi = std::max(c.MinValue, c.MaxValue);
            if (ImGui::SliderFloat("##v", &c.Value, lo, hi, c.WholeNumbers ? "%.0f" : "%.3f"))
            {
                c.Value = UIMath::ClampValue(c.Value, c.MinValue, c.MaxValue, c.WholeNumbers);
                changed = true;
            }
            ImGui::PopID();
            if (UI::PropertyBool("Butun sonlar", c.WholeNumbers))
            {
                c.Value = UIMath::ClampValue(c.Value, c.MinValue, c.MaxValue, c.WholeNumbers);
                changed = true;
            }
            int direction = (int)c.Direction;
            if (UI::PropertyCombo("Yo'nalish", direction, s_UIDirectionNames, 4)) { c.Direction = (UIDirection)direction; changed = true; }
            changed |= UI::PropertyBool("Faol", c.Interactable);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText(Tr("Ko'rinish"));
            changed |= UI::PropertyFloat("Yo'lak qalinligi (px)", c.TrackThickness, 0.25f, 0.0f, 1000.0f, "%.1f");
            if (ImGui::IsItemHovered()) UI::Tooltip("0 - butun to'rtburchak qalinligi");
            changed |= UI::PropertyFloat("Tutqich o'lchami (px)", c.HandleSize, 0.25f, 0.0f, 1000.0f, "%.1f");
            if (ImGui::IsItemHovered()) UI::Tooltip("0 - to'rtburchak qalinligi");
            changed |= UI::PropertyBool("Yumaloq", c.Rounded);
            changed |= UI::PropertyColor4("Yo'lak rangi", c.TrackColor);
            changed |= UI::PropertyColor4("To'ldirish rangi", c.FillColor);
            changed |= UI::PropertyColor4("Tutqich rangi", c.HandleColor);
            UI::PropertyLabel("");
            ImGui::TextDisabled("Skriptda: OnValueChanged(qiymat), .Value, .Dragging");
            return changed;
        }, true, modified);

        AddSection<UIToggleComponent>(ICON_FA_SQUARE_CHECK, "UI Toggle", entity, [&](UIToggleComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: belgilash ko'rinmaydi va bosilmaydi");
            bool changed = false;
            if (UI::PropertyBool("Yoqilgan", c.IsOn))
            {
                c.Transition = c.IsOn ? 1.0f : 0.0f;
                if (c.IsOn && m_Context && m_Context->IsRunning())
                {
                    // The group follows while the game runs (as SetToggle does).
                    c.IsOn = false;
                    m_Context->SetToggle(entity, true);
                }
                changed = true;
            }
            const char* styles[] = { "Belgilash qutisi", "Almashtirgich (switch)" };
            int style = (int)c.Style;
            if (UI::PropertyCombo("Ko'rinish", style, styles, 2)) { c.Style = (UIToggleStyle)style; changed = true; }
            if (c.Style == UIToggleStyle::Checkbox)
            {
                const char* marks[] = { "Belgi (galochka)", "Kvadrat", "Nuqta" };
                int mark = (int)c.Mark;
                if (UI::PropertyCombo("Belgi", mark, marks, 3)) { c.Mark = (UIToggleMark)mark; changed = true; }
            }
            changed |= UI::PropertyString("Guruh", c.Group);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Bir xil guruhdagi belgilashlardan faqat bittasi yoqiq bo'ladi (radio tugmalar). Bo'sh - guruhsiz");
            if (!c.Group.empty())
                changed |= UI::PropertyBool("O'chirishga ruxsat", c.AllowSwitchOff);
            changed |= UI::PropertyBool("Faol", c.Interactable);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText(Tr("Ko'rinish"));
            changed |= UI::PropertyFloat("Quti o'lchami (px)", c.BoxSize, 0.25f, 0.0f, 1000.0f, "%.1f");
            if (ImGui::IsItemHovered()) UI::Tooltip("0 - to'rtburchak balandligi");
            changed |= UI::PropertyFloat("Yozuvgacha oraliq (px)", c.LabelSpacing, 0.25f, 0.0f, 1000.0f, "%.1f");
            changed |= UI::PropertyColor4("Quti rangi", c.BoxColor);
            changed |= UI::PropertyColor4("Yoqiq rangi", c.OnColor);
            changed |= UI::PropertyColor4("Belgi rangi", c.MarkColor);
            UI::PropertyLabel("");
            ImGui::TextDisabled(entity.HasComponent<TextComponent>() ? "Yozuv: shu obyektning Text komponenti (qutidan o'ngda)"
                                                                     : "Yozuv uchun Text komponenti qo'shing");
            UI::PropertyLabel("");
            ImGui::TextDisabled("Skriptda: OnToggleChanged(yoqiq), .IsOn");
            return changed;
        }, true, modified);

        AddSection<UIProgressBarComponent>(ICON_FA_BARS_PROGRESS, "UI Progress Bar", entity, [&](UIProgressBarComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: progress bar ko'rinmaydi");
            bool changed = UI::PropertySlider("Qiymat (0..1)", c.Value, 0.0f, 1.0f);
            if (changed && !(m_Context && m_Context->IsRunning()))
                c.DisplayValue = c.Value;
            int direction = (int)c.Direction;
            if (UI::PropertyCombo("Yo'nalish", direction, s_UIDirectionNames, 4)) { c.Direction = (UIDirection)direction; changed = true; }
            changed |= UI::PropertyColor4("Fon rangi", c.BackgroundColor);
            changed |= UI::PropertyColor4("To'ldirish rangi", c.FillColor);
            changed |= UI::PropertyBool("Rang qiymatga qarab", c.UseLowColor);
            if (ImGui::IsItemHovered()) UI::Tooltip("To'ldirish rangi bo'shda shu rangdan to'lada asosiy rangga o'tadi (masalan, sog'liq: qizil -> yashil)");
            if (c.UseLowColor)
                changed |= UI::PropertyColor4("Kam qiymat rangi", c.LowColor);
            changed |= UI::PropertyFloat("Ichki chekinish (px)", c.Padding, 0.25f, 0.0f, 1000.0f, "%.1f");
            changed |= UI::PropertyFloat("Silliq tezlik (/s)", c.SmoothSpeed, 0.01f, 0.0f, 100.0f, "%.2f");
            if (ImGui::IsItemHovered()) UI::Tooltip("O'yinda chiziq qiymatga shu tezlikda yetib boradi (sekundiga). 0 - darhol");
            changed |= UI::PropertyBool("Foizni ko'rsatish", c.ShowPercentage);
            if (c.ShowPercentage)
                changed |= UI::PropertyColor4("Matn rangi", c.TextColor);
            return changed;
        }, true, modified);

        AddSection<UIMaskComponent>(ICON_FA_CROP, "UI Mask", entity, [&](UIMaskComponent& c) {
            bool changed = UI::PropertyBool("O'z rasmini ko'rsatish", c.ShowGraphic);
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'chiq: maskaning o'z rasmi (UI Image, sprite, tugma rangi) chizilmaydi - faqat bolalarini kesadi");
            changed |= UI::PropertyFloat4("Ichki chekka (C, O', Y, P)", c.Padding, 0.5f);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Kesish to'rtburchagi chetlardan shuncha UI birligi ichkarida (manfiy - tashqarida)");
            UI::PropertyLabel("");
            ImGui::TextDisabled("Bolalari faqat shu to'rtburchak ichida ko'rinadi va bosiladi");
            if (m_Context && entity.HasComponent<UITransformComponent>())
            {
                UIRect clip;
                if (m_Context->GetUIClipRect(entity, clip))
                {
                    UI::PropertyLabel("");
                    ImGui::TextDisabled("O'zi ham ota maskasi ichida (%.0f x %.0f px)", clip.GetWidth(), clip.GetHeight());
                }
            }
            return changed;
        }, true, modified);

        AddSection<UIScrollViewComponent>(ICON_FA_ARROWS_UP_DOWN, "UI Scroll View", entity, [&](UIScrollViewComponent& c) {
            bool changed = DrawEntityRefField("Kontent", c.Content, "Aylantiriladigan UI obyekti (ichidagi ro'yxat) - uning UI Transform Position'i suriladi");
            changed |= DrawEntityRefField("Ko'rinish (viewport)", c.Viewport, "Kontent ko'rinadigan to'rtburchak (bo'sh - scroll view'ning o'zi, scrollbar'larsiz)");
            if (m_Context && c.Content && !m_Context->GetScrollContent(entity))
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Kontentda UI Transform yo'q");
            }
            else if (!c.Content)
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Kontent tanlanmagan");
            }
            changed |= UI::PropertyBool("Gorizontal", c.Horizontal);
            changed |= UI::PropertyBool("Vertikal", c.Vertical);
            static const char* const s_Movements[] = { "Cheklanmagan", "Elastik (qaytadi)", "Chetda to'xtaydi" };
            int movement = (int)c.Movement;
            if (UI::PropertyCombo("Harakat", movement, s_Movements, 3)) { c.Movement = (UIScrollMovement)movement; changed = true; }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Elastik - chetdan tortilganda qarshilik qiladi va qo'yib yuborilganda qaytadi; chetda to'xtaydi - undan chiqmaydi");
            if (c.Movement == UIScrollMovement::Elastic)
                changed |= UI::PropertyFloat("Elastiklik (s)", c.Elasticity, 0.005f, 0.0f, 2.0f);
            changed |= UI::PropertyBool("Inersiya", c.Inertia);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Sudrab qo'yib yuborilgandan keyin sekinlashib davom etadi");
            if (c.Inertia)
                changed |= UI::PropertySlider("Sekinlashish", c.DecelerationRate, 0.0f, 1.0f);
            changed |= UI::PropertyFloat("G'ildirak sezgirligi", c.ScrollSensitivity, 0.5f, -1000.0f, 1000.0f, "%.0f");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Sichqoncha g'ildiragining bir pog'onasida necha UI birligi suriladi");
            changed |= UI::PropertyBool("Sudrab aylantirish", c.Draggable);
            UI::PropertyHeader("Scrollbar'lar", "Scroll view o'zi chizadi: vertikali o'ng chetda, gorizontali pastda");
            static const char* const s_Bars[] = { "Doim", "Kerak bo'lganda", "Yo'q" };
            int bars = (int)c.Scrollbars;
            if (UI::PropertyCombo("Ko'rinishi", bars, s_Bars, 3)) { c.Scrollbars = (UIScrollbarVisibility)bars; changed = true; }
            if (c.Scrollbars != UIScrollbarVisibility::Hidden)
            {
                changed |= UI::PropertyFloat("Qalinligi", c.ScrollbarWidth, 0.25f, 0.0f, 200.0f, "%.1f");
                changed |= UI::PropertyFloat("Chetdan", c.ScrollbarInset, 0.25f, 0.0f, 200.0f, "%.1f");
                changed |= UI::PropertyColor4("Yo'lak rangi", c.TrackColor);
                changed |= UI::PropertyColor4("Tutqich rangi", c.HandleColor);
            }
            if (m_Context && entity.HasComponent<UITransformComponent>())
            {
                const glm::vec2 n = m_Context->GetScrollPosition(entity);
                const UIRect view = m_Context->GetScrollViewRect(entity);
                UI::PropertyLabel("");
                ImGui::TextDisabled("Joyi: x %.2f, y %.2f   ko'rinish %.0f x %.0f", n.x, n.y, view.GetWidth(), view.GetHeight());
                if (m_Context->IsRunning() && c.Dragging)
                {
                    UI::PropertyLabel("");
                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_HAND_POINTER "  Sudralmoqda");
                }
            }
            UI::PropertyLabel("");
            ImGui::TextDisabled("Skriptda: OnScrollChanged(pos); hodisa: \"UIScrollChanged\"");
            return changed;
        }, true, modified);

        AddSection<UIInputFieldComponent>(ICON_FA_I_CURSOR, "UI Input Field", entity, [&](UIInputFieldComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: matn maydoni ko'rinmaydi va tahrirlanmaydi");
            bool changed = false;
            std::string text = c.Text;
            if (UI::PropertyString("Matn", text))
            {
                c.Text = TextEdit::Filter(text, c.ContentType, c.CharacterLimit);
                c.Caret = c.Anchor = std::min(c.Caret, c.Text.size());
                changed = true;
            }
            changed |= UI::PropertyString("Bo'sh paytdagi yozuv", c.Placeholder);
            const char* types[] = { "Oddiy matn", "Butun son", "O'nlik son", "Harf va raqam", "Parol" };
            int type = (int)c.ContentType;
            if (UI::PropertyCombo("Mazmun turi", type, types, 5))
            {
                c.ContentType = (UIContentType)type;
                c.Text = TextEdit::Filter(c.Text, c.ContentType, c.CharacterLimit);
                c.Caret = c.Anchor = std::min(c.Caret, c.Text.size());
                changed = true;
            }
            if (UI::PropertyInt("Belgilar chegarasi", c.CharacterLimit))
            {
                c.CharacterLimit = std::max(0, c.CharacterLimit);
                c.Text = TextEdit::Filter(c.Text, c.ContentType, c.CharacterLimit);
                c.Caret = c.Anchor = std::min(c.Caret, c.Text.size());
                changed = true;
            }
            if (ImGui::IsItemHovered()) UI::Tooltip("Ko'pi bilan shuncha belgi (harflar, UTF-8 baytlar emas). 0 - cheklanmagan");
            changed |= UI::PropertyBool("Faol", c.Interactable);
            changed |= UI::PropertyBool("Faqat o'qish", c.ReadOnly);
            changed |= UI::PropertyBool("Fokusda hammasini tanlash", c.SelectAllOnFocus);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText(Tr("Ko'rinish"));
            if (UI::PropertyAssetPath("Shrift", c.FontPath, ".ttf;.otf", ICON_FA_FONT))
            {
                c.FontAsset = c.FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(c.FontPath));
                changed = true;
            }
            changed |= UI::PropertyFloat("Chekinish (px)", c.Padding, 0.25f, 0.0f, 1000.0f, "%.1f");
            changed |= UI::PropertyColor4("Fon", c.BackgroundColor);
            changed |= UI::PropertyColor4("Chegara", c.OutlineColor);
            changed |= UI::PropertyColor4("Fokus chegarasi", c.FocusColor);
            changed |= UI::PropertyColor4("Matn rangi", c.TextColor);
            changed |= UI::PropertyColor4("Bo'sh yozuv rangi", c.PlaceholderColor);
            changed |= UI::PropertyColor4("Kursor", c.CaretColor);
            changed |= UI::PropertyColor4("Tanlov", c.SelectionColor);
            changed |= UI::PropertyFloat("Kursor miltillashi (s)", c.CaretBlinkRate, 0.01f, 0.0f, 5.0f, "%.2f");
            if (m_Context && m_Context->IsRunning())
            {
                UI::PropertyLabel("Holat");
                ImGui::TextDisabled("%s", c.Focused ? "Tahrirlanmoqda" : "Kutmoqda");
            }
            UI::PropertyLabel("");
            ImGui::TextDisabled("Skriptda: OnTextChanged / OnTextSubmit / OnTextEndEdit");
            return changed;
        }, true, modified);

        AddSection<AudioSourceComponent>(ICON_FA_VOLUME_HIGH, "Audio Source", entity, [&](AudioSourceComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: ovoz to'xtatilgan (faollashganda 'Boshida ijro' qayta boshlanadi)");
            bool changed = UI::PropertyAssetPath("Ovoz fayli", c.Path, ".wav;.mp3;.flac;.ogg", ICON_FA_FILE_AUDIO);
            changed |= UI::PropertySlider("Balandlik", c.Volume, 0.0f, 2.0f);
            changed |= UI::PropertySlider("Ton (pitch)", c.Pitch, 0.1f, 3.0f);
            changed |= UI::PropertyBool("Takrorlash", c.Loop);
            changed |= UI::PropertyBool("Boshida ijro", c.PlayOnStart);
            changed |= UI::PropertyBool("3D ovoz (spatial)", c.Spatial);
            if (c.Spatial)
            {
                changed |= UI::PropertyFloat("Min masofa", c.MinDistance, 0.05f, 0.01f, 1000.0f);
                changed |= UI::PropertyFloat("Max masofa", c.MaxDistance, 0.1f, 0.1f, 10000.0f);
                changed |= DrawSpatialDefaultsRow(c.MinDistance, c.MaxDistance);
                UI::PropertyLabel("Doppler", "Ovoz manbai yoki tinglovchi harakatlanganda ton o'zgarishi kuchi (yaqinlashsa - "
                                             "baland, uzoqlashsa - past): 1 - tabiiy, 0 - yo'q. Loyihaning Doppler "
                                             "koeffitsiyentiga ko'paytiriladi (Audio Mixer > Sozlamalar). Tezlik "
                                             "obyekt harakatidan o'lchanadi.");
                ImGui::PushID("doppler");
                changed |= ImGui::SliderFloat("##v", &c.DopplerFactor, 0.0f, AudioSettings::MaxDopplerFactor, "%.2f");
                ImGui::PopID();
            }
            changed |= DrawAudioOutput(c.Output);
            UI::PropertyLabel("Streaming", "Fayl o'ynayotganda diskdan oqim bilan o'qiladi (xotiraga butunlay yuklanmaydi): "
                                           "uzun musiqa va fon ovozlari uchun. Loyiha chegarasidan katta fayllar "
                                           "(Audio Mixer > Sozlamalar) har doim oqim bilan o'qiladi.");
            ImGui::PushID("stream");
            changed |= ImGui::Checkbox("##v", &c.Stream);
            if (!c.Stream && !c.Path.empty() && AudioEngine::ShouldStream(Project::GetAssetFileSystemPath(c.Path), false))
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(katta fayl: baribir oqim)");
            }
            ImGui::PopID();

            if (c.Spatial)
            {
                UI::PropertyHeader(ICON_FA_BULLHORN "  Yo'nalish konusi", "3D ovoz obyektning oldi tomoniga (-Z, sahnada konus bilan "
                                                                         "ko'rsatiladi) qarab baland, orqasida past eshitiladi: "
                                                                         "karnay, televizor, gapirayotgan odam. Masofa so'nishi ustiga "
                                                                         "qo'shiladi.");
                UI::PropertyLabel("Yoqilgan");
                ImGui::PushID("coneEnabled");
                changed |= ImGui::Checkbox("##v", &c.Cone.Enabled);
                ImGui::PopID();
                if (c.Cone.Enabled)
                {
                    UI::PropertyLabel("Ichki burchak", "Konusning to'liq ochilishi (gradus): uning ichida ovoz to'liq balandlikda");
                    ImGui::PushID("coneInner");
                    if (ImGui::SliderFloat("##v", &c.Cone.InnerAngle, 0.0f, 360.0f, "%.0f°", ImGuiSliderFlags_AlwaysClamp))
                    {
                        c.Cone.OuterAngle = std::max(c.Cone.OuterAngle, c.Cone.InnerAngle);
                        changed = true;
                    }
                    ImGui::PopID();
                    UI::PropertyLabel("Tashqi burchak", "Undan tashqarida ovoz 'Orqada' balandligida; ichki va tashqi konus "
                                                        "orasida silliq o'tadi");
                    ImGui::PushID("coneOuter");
                    if (ImGui::SliderFloat("##v", &c.Cone.OuterAngle, 0.0f, 360.0f, "%.0f°", ImGuiSliderFlags_AlwaysClamp))
                    {
                        c.Cone.InnerAngle = std::min(c.Cone.InnerAngle, c.Cone.OuterAngle);
                        changed = true;
                    }
                    ImGui::PopID();
                    UI::PropertyLabel("Orqada", "Tashqi konusdan tashqarida (orqa tomonda) qancha eshitiladi: 0 - jim, 1 - "
                                                "o'zgarmaydi");
                    ImGui::PushID("coneGain");
                    std::string gainText = c.Cone.OuterGain <= 0.0f ? std::string("jim")
                                                                    : std::format("{:.2f} ({:+.1f} dB)", c.Cone.OuterGain,
                                                                                  AudioGain::ToDecibels(c.Cone.OuterGain));
                    changed |= ImGui::SliderFloat("##v", &c.Cone.OuterGain, 0.0f, 1.0f, gainText.c_str(), ImGuiSliderFlags_AlwaysClamp);
                    ImGui::PopID();
                    if (c.Cone.InnerAngle >= 360.0f)
                    {
                        UI::PropertyLabel("");
                        ImGui::TextDisabled("360°: hamma tomonga bir xil (konus ta'sir qilmaydi)");
                    }
                }
            }

            UI::PropertyHeader(ICON_FA_CHART_LINE "  So'nish (fade)", "Ovoz boshlanganda jimdan asta balandlashadi, to'xtatilganda "
                                                                    "(StopAudio, obyekt o'chirilsa yoki yo'q qilinsa) birdan uzilmay asta "
                                                                    "so'nadi. Skriptda: PlayAudio(sekund), StopAudio(sekund), FadeAudio(...).");
            auto fadeField = [&](const char* label, const char* id, float& seconds, const char* tooltip) {
                UI::PropertyLabel(label, tooltip);
                ImGui::PushID(id);
                bool edited = ImGui::DragFloat("##v", &seconds, 0.01f, 0.0f, 60.0f, seconds > 0.0f ? "%.2f s" : "yo'q", ImGuiSliderFlags_AlwaysClamp);
                ImGui::PopID();
                return edited;
            };
            changed |= fadeField("Kirish (fade in)", "fadeIn", c.FadeIn, "Ijro boshlanganda 0 dan to'liq balandlikkacha necha sekundda ko'tariladi");
            changed |= fadeField("Chiqish (fade out)", "fadeOut", c.FadeOut,
                                 "To'xtatilganda necha sekundda so'nadi (bu vaqtda manba bo'sh: PlayAudio yangisini boshlaydi)");

            UI::PropertyHeader(ICON_FA_FILTER "  Filtr", "Low-pass: kesish chastotasidan yuqorisi bo'g'iladi (devor ortida, suv ostida, "
                                                         "uzoqda). High-pass: undan pastki ovozlar kesiladi (radio, telefon). O'yin "
                                                         "davomida o'zgarsa silliq o'tadi. Mixer guruhining filtri ustiga qo'shiladi.");
            UI::PropertyLabel("Low-pass", "Shu chastotadan (Hz) yuqori tovushlar so'nadi: 500 - kuchli bo'g'iq, 5000 - yengil");
            changed |= UI::AudioFilterRow("lowPass", c.Filter.LowPass, c.Filter.LowPassCutoff);
            UI::PropertyLabel("High-pass", "Shu chastotadan (Hz) past tovushlar so'nadi: 300 - 3000 oralig'i radio / telefon kabi");
            changed |= UI::AudioFilterRow("highPass", c.Filter.HighPass, c.Filter.HighPassCutoff);

            UI::PropertyHeader(ICON_FA_SHUFFLE "  Tasodifiylik", "Har ijroda balandlik va ton shu oraliqdagi tasodifiy songa "
                                                                "ko'paytiriladi (1 - 1: o'zgarmaydi): takrorlanadigan ovozlar "
                                                                "(qadam, zarba, o'q) bir xil eshitilmasin.");
            auto range = [&](const char* label, const char* id, glm::vec2& v, float lo, float hi, const char* tooltip) {
                UI::PropertyLabel(label, tooltip);
                ImGui::PushID(id);
                bool edited = ImGui::DragFloatRange2("##v", &v.x, &v.y, 0.005f, lo, hi, "%.2f", "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::PopID();
                return edited;
            };
            changed |= range("Balandlik oralig'i", "volumeRandom", c.VolumeRandom, 0.0f, 2.0f,
                             "Balandlik ko'paytuvchisi: min - max (masalan 0.8 - 1.0)");
            changed |= range("Ton oralig'i", "pitchRandom", c.PitchRandom, 0.1f, 3.0f,
                             "Ton ko'paytuvchisi: min - max (masalan 0.9 - 1.1 - yarim tondan sal kam)");
            if (c.PitchRandom != glm::vec2(1.0f))
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled("%+.1f .. %+.1f yarim ton", 12.0f * std::log2(std::max(0.01f, c.PitchRandom.x)),
                                    12.0f * std::log2(std::max(0.01f, c.PitchRandom.y)));
            }

            UI::PropertyHeader(ICON_FA_LAYER_GROUP "  Ovozlar limiti", "Loyihada bir vaqtda eng ko'pi bilan Audio Mixer > Sozlamalar'dagi "
                                                                       "sonda ovoz ijro etiladi (musiqa hisobga kirmaydi). Limit to'lsa yangi "
                                                                       "ovoz muhimligi pastrog'ining o'rnini oladi.");
            UI::PropertyLabel("Ustuvorlik", "0..255: limit to'lganda past ustuvorlikdagi ovoz to'xtatilib, o'rni muhimrog'iga "
                                            "beriladi. Teng bo'lsa - quyidagi qoida bo'yicha.");
            ImGui::PushID("priority");
            changed |= ImGui::SliderInt("##v", &c.Priority, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
            UI::PropertyLabel("Bir vaqtda (shu fayl)", "Shu ovoz fayli bir vaqtda ko'pi bilan necha marta ijro etiladi "
                                                       "(0 - cheklanmaydi): masalan 20 ta dushman bir xil o'kirsa ham 3 tasi eshitiladi.");
            ImGui::PushID("maxInstances");
            changed |= ImGui::DragInt("##v", &c.MaxInstances, 0.1f, 0, 256, c.MaxInstances > 0 ? "%d ta" : "cheklanmagan", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
            {
                static const char* policies[] = { "Eng eskisi to'xtaydi", "Eng past eshitilgani to'xtaydi", "Yangisi ijro etilmaydi" };
                int policy = (int)c.Steal;
                UI::PropertyLabel("Limit to'lsa", "Eng eskisi: birinchi boshlangan ovoz to'xtaydi. Eng past eshitilgani: balandlik x guruh x "
                                                  "masofa bo'yicha eng jimi. Yangisi ijro etilmaydi: hech narsa to'xtamaydi (ustuvorligi "
                                                  "pastroq ovoz bo'lsa - u to'xtaydi).");
                ImGui::PushID("steal");
                if (ImGui::Combo("##v", &policy, policies, 3))
                {
                    c.Steal = (AudioStealPolicy)std::clamp(policy, 0, 2);
                    changed = true;
                }
                ImGui::PopID();
            }

            if (m_Context && m_Context->IsRunning())
                DrawAudioSourceState(entity, c);
            else
            {
                UI::PropertyLabel("");
                if (ImGui::Button(Tr(ICON_FA_PLAY "  Eshitish")) && !c.Path.empty())
                {
                    SoundParams params;
                    params.Volume = c.Volume;
                    params.Pitch = c.Pitch;
                    params.Group = c.Output;
                    params.Stream = c.Stream;
                    params.VolumeRandom = c.VolumeRandom;
                    params.PitchRandom = c.PitchRandom;
                    params.FadeIn = c.FadeIn;
                    params.Filter = c.Filter;
                    AudioEngine::Play(Project::GetAssetFileSystemPath(c.Path), params);
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Tinglab ko'rish (tasodifiylik bilan: har bosishda boshqacha)");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_STOP))
                    AudioEngine::StopAllExceptMusic();
            }
            return changed;
        }, true, modified);

        AddSection<AudioListenerComponent>(ICON_FA_EAR_LISTEN, "Audio Listener", entity, [&](AudioListenerComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: tinglamaydi (boshqa listener yoki asosiy kamera tinglaydi)");
            bool changed = UI::PropertyBool("Yoqilgan", c.Enabled);
            UI::PropertyLabel("Ustuvorlik", "Bir nechta faol listener bo'lsa eng katta ustuvorlikdagisi tinglaydi "
                                            "(teng bo'lsa - birinchi yaratilgani).");
            ImGui::PushID("priority");
            changed |= ImGui::DragInt("##v", &c.Priority, 0.1f);
            ImGui::PopID();
            UI::PropertyLabel("Tezlikni kuzatish", "Listener tezligi harakatidan o'lchanadi: harakatlanayotgan "
                                                   "3D ovozlarda Doppler effekti (ton o'zgarishi) uchun.");
            ImGui::PushID("trackVelocity");
            changed |= ImGui::Checkbox("##v", &c.TrackVelocity);
            ImGui::PopID();
            UI::PropertyLabel("Holat");
            if (m_Context && m_Context->IsRunning())
            {
                bool listening = m_Context->GetAudioListenerEntity() == entity;
                if (listening)
                    ImGui::TextColored(UI::AccentColor(), ICON_FA_EAR_LISTEN "  Tinglayapti");
                else
                    ImGui::TextDisabled("Tinglamayapti (boshqasi faol)");
                if (c.TrackVelocity)
                    ImGui::TextDisabled("Tezlik: %.1f m/s", glm::length(c.Velocity));
            }
            else
            {
                Entity active = m_Context ? m_Context->GetActiveAudioListener() : Entity();
                if (active == entity)
                    ImGui::TextDisabled("O'yinda shu listener tinglaydi (kamera o'rniga)");
                else if (active)
                    ImGui::TextDisabled("O'yinda '%s' tinglaydi", active.GetName().c_str());
                else
                    ImGui::TextDisabled("Nofaol: o'yinda asosiy kamera tinglaydi");
            }
            return changed;
        }, true, modified);

        AddSection<AmbientZoneComponent>(ICON_FA_TREE, "Ambient Zone", entity, [&](AmbientZoneComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: zona eshitilmaydi (ovozi so'nadi)");
            bool changed = UI::PropertyAssetPath("Ovoz fayli", c.Path, ".wav;.mp3;.flac;.ogg", ICON_FA_FILE_AUDIO);
            changed |= UI::PropertySlider("Balandlik", c.Volume, 0.0f, 2.0f);
            changed |= UI::PropertySlider("Ton (pitch)", c.Pitch, 0.1f, 3.0f);
            changed |= DrawAudioOutput(c.Output);
            UI::PropertyLabel("Streaming", "Uzun fon ovozi diskdan oqim bilan o'qiladi (xotiraga butunlay yuklanmaydi)");
            ImGui::PushID("stream");
            changed |= ImGui::Checkbox("##v", &c.Stream);
            ImGui::PopID();

            UI::PropertyHeader(ICON_FA_VECTOR_SQUARE "  Hajm", "Tinglovchi (Audio Listener yoki asosiy kamera) hajm ichida bo'lsa ovoz "
                                                              "to'liq va joylashuvsiz (ikki quloqqa bir xil) eshitiladi; tashqarida "
                                                              "'Aralashish masofasi' ichida asta so'nadi. Obyektning masshtabi va "
                                                              "aylanishi hajmga ta'sir qiladi.");
            {
                static const char* shapes[] = { "Quti (box)", "Shar (sphere)" };
                int shape = (int)c.Shape;
                UI::PropertyLabel("Shakl");
                ImGui::PushID("shape");
                if (ImGui::Combo("##v", &shape, shapes, 2))
                {
                    c.Shape = (AmbientZoneComponent::ZoneShape)std::clamp(shape, 0, 1);
                    changed = true;
                }
                ImGui::PopID();
            }
            if (c.Shape == AmbientZoneComponent::ZoneShape::Box)
            {
                changed |= UI::PropertyFloat3("O'lcham", c.Size, 0.05f);
                c.Size = glm::max(c.Size, glm::vec3(0.0f));
            }
            else
                changed |= UI::PropertyFloat("Radius", c.Radius, 0.05f, 0.0f, 100000.0f);
            UI::PropertyLabel("Aralashish masofasi", "Hajm chetidan tashqarida shu masofada (m) ovoz to'liqdan jimgacha silliq so'nadi "
                                                     "(0 - chegarada birdan)");
            ImGui::PushID("blend");
            changed |= ImGui::DragFloat("##v", &c.BlendDistance, 0.05f, 0.0f, 1000.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
            UI::PropertyLabel("So'nish vaqti", "Daraja jimdan to'liqgacha eng tez shuncha sekundda o'zgaradi: teleport, zona "
                                               "yoqilishi yoki o'chirilishi silliq eshitiladi (0 - darhol)");
            ImGui::PushID("fade");
            changed |= ImGui::DragFloat("##v", &c.FadeTime, 0.01f, 0.0f, 60.0f, c.FadeTime > 0.0f ? "%.2f s" : "darhol", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
            UI::PropertyLabel("Ustuvorlik", "Ovozlar limiti to'lganda (Audio Source'dagidek): 0..255");
            ImGui::PushID("priority");
            changed |= ImGui::SliderInt("##v", &c.Priority, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();

            UI::PropertyLabel("Holat");
            if (m_Context && m_Context->IsRunning())
            {
                std::string text = c.Level <= 0.0f ? std::string("jim") : std::format("{:.0f}%", c.Level * 100.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.78f, 0.5f, 1.0f));
                ImGui::ProgressBar(std::clamp(c.Level, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f), text.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Hozir eshitilayotgan daraja (So'nish vaqti bilan silliqlangan)");
                if (c.Target >= 1.0f)
                    ImGui::TextColored(UI::AccentColor(), ICON_FA_EAR_LISTEN "  Tinglovchi zona ichida");
                else if (c.Target > 0.0f)
                    ImGui::TextDisabled("Tinglovchi chetdan %.1f m tashqarida (%.0f%%)", c.Distance, c.Target * 100.0f);
                else
                    ImGui::TextDisabled("Tinglovchi uzoqda (%.1f m)", c.Distance);
            }
            else
                ImGui::TextDisabled("O'yinda: tinglovchi yaqinlashsa asta eshitiladi");
            return changed;
        }, true, modified);

        AddSection<FootstepsComponent>(ICON_FA_SHOE_PRINTS, "Footsteps", entity, [&](FootstepsComponent& c) {
            return DrawFootsteps(entity, c);
        }, true, modified);

        AddSection<SubSceneComponent>(ICON_FA_LAYER_GROUP, "Sub-sahna", entity, [&](SubSceneComponent& c) {
            return DrawSubScene(entity, c);
        }, true, modified);

        DrawScriptComponent(entity, modified);

        // Draw the queued sections in the entity's component order (Transform first).
        m_SectionOrder = ComponentRegistry::GetOrderedKeys(entity);
        for (const std::string& key : m_SectionOrder)
        {
            for (InspectorSection& section : m_Sections)
                if (section.Key == key && entity.IsValid())
                    section.Draw();
            if (!entity.IsValid()) // a script callback destroyed it
                break;
        }
        m_Sections.clear();
        if (entity.IsValid())
            DrawRemovedPrefabComponents(entity);
        if (modified) MarkModified();

        // A particle system's new sub-emitter template (adding a component while the inspector
        // holds references into the storage would move them).
        if (m_PendingSubEmitterFor && m_SceneEditable)
            CreateSubEmitterTemplate(m_PendingSubEmitterFor);
        m_PendingSubEmitterFor = 0;
        ApplyPendingParticlePreset(); // a particle preset chosen in the inspector (it replaces the component)

        // Reordering (menu / header drag & drop) is applied after everything was drawn.
        PendingMove move = std::move(m_PendingMove);
        m_PendingMove = {};
        if (!move.Key.empty() && entity.IsValid() && m_SceneEditable)
        {
            int index = move.Index;
            if (!move.Target.empty())
            {
                std::vector<std::string> keys = ComponentRegistry::GetOrderedKeys(entity);
                auto from = std::find(keys.begin(), keys.end(), move.Key), to = std::find(keys.begin(), keys.end(), move.Target);
                index = from == keys.end() || to == keys.end() ? -2 : (int)(to - keys.begin()) + (move.After ? 1 : 0);
                if (index >= 0 && from - keys.begin() < index)
                    index--; // the dragged one leaves its place first
            }
            if (index != -2 && ComponentRegistry::MoveComponent(entity, move.Key, index))
                MarkModified();
        }
    }

}
