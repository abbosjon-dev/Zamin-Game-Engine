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
#include <chrono>
#include <cmath>
#include <format>
#include <set>

namespace Zamin {

    static const char* EntityIcon(Entity e)
    {
        if (e.HasComponent<SubSceneComponent>()) return ICON_FA_LAYER_GROUP;
        if (const auto* meta = e.TryGetComponent<EditorMetadataComponent>(); meta && !meta->Prefab.empty()) return ICON_FA_BOX;
        if (e.HasComponent<UIInputFieldComponent>()) return ICON_FA_I_CURSOR;
        if (e.HasComponent<UISliderComponent>()) return ICON_FA_SLIDERS;
        if (e.HasComponent<UIToggleComponent>()) return ICON_FA_SQUARE_CHECK;
        if (e.HasComponent<UIProgressBarComponent>()) return ICON_FA_BARS_PROGRESS;
        if (e.HasComponent<UIButtonComponent>()) return ICON_FA_HAND_POINTER;
        if (e.HasComponent<UIScrollViewComponent>()) return ICON_FA_ARROWS_UP_DOWN;
        if (e.HasComponent<UILayoutGroupComponent>()) return ICON_FA_LIST;
        if (e.HasComponent<UIImageComponent>()) return ICON_FA_IMAGE;
        if (e.HasComponent<UICanvasComponent>()) return ICON_FA_EXPAND;
        if (e.HasComponent<UIMaskComponent>()) return ICON_FA_CROP;
        if (e.HasComponent<UITransformComponent>()) return ICON_FA_TABLE_CELLS;
        if (e.HasComponent<ParticleSystemComponent>()) return ICON_FA_WAND_MAGIC_SPARKLES;
        if (e.HasComponent<CameraComponent>()) return ICON_FA_CAMERA;
        if (e.HasComponent<ParallaxComponent>()) return ICON_FA_MOUNTAIN;
        if (e.HasComponent<DirectionalLightComponent>()) return ICON_FA_SUN;
        if (e.HasComponent<PointLightComponent>()) return ICON_FA_LIGHTBULB;
        if (e.HasComponent<SpotLightComponent>()) return ICON_FA_BULLHORN;
        if (e.HasComponent<Light2DComponent>()) return ICON_FA_FIRE;
        if (e.HasComponent<TerrainComponent>()) return ICON_FA_MOUNTAIN_SUN;
        if (e.HasComponent<MeshRendererComponent>()) return ICON_FA_CUBE;
        if (e.HasComponent<TextComponent>()) return ICON_FA_FONT;
        if (e.HasComponent<CircleRendererComponent>()) return ICON_FA_CIRCLE;
        if (e.HasComponent<SpriteRendererComponent>()) return ICON_FA_IMAGE;
        if (e.HasComponent<AudioSourceComponent>()) return ICON_FA_VOLUME_HIGH;
        if (e.HasComponent<AudioListenerComponent>()) return ICON_FA_EAR_LISTEN;
        if (e.HasComponent<AmbientZoneComponent>()) return ICON_FA_TREE;
        if (e.HasComponent<ParticleSystemComponent>()) return ICON_FA_WAND_MAGIC_SPARKLES;
        if (e.HasComponent<UIButtonComponent>()) return ICON_FA_HAND_POINTER;
        if (e.HasComponent<UITransformComponent>()) return ICON_FA_TABLE_CELLS;
        return ICON_FA_BOX;
    }

    void SceneHierarchyPanel::SetContext(const Ref<Scene>& context, bool keepSelection)
    {
        // Remap reads the old entities: m_Context still keeps the previous scene alive here.
        if (keepSelection && context)
            m_Selection.Remap(context.get());
        else
            m_Selection.Clear();
        m_Context = context;
        m_RenamingEntity = entt::null;
        m_ClickedSelected = {};
        m_PendingClick = {};
        m_PendingDrop = {};
        m_VisibleRows.clear();
    }

    // ------------------------------------------------------------------ selection
    std::vector<Entity> SceneHierarchyPanel::GetSelectedRoots() const
    {
        if (!m_Context || m_Selection.IsEmpty())
            return {};
        if (m_Selection.Size() == 1) // the common case, without walking the hierarchy
        {
            Entity only = m_Selection.GetEntities().front();
            return only && only.GetScene() == m_Context.get() ? std::vector<Entity>{ only } : std::vector<Entity>{};
        }
        return m_Context->GetTopLevelEntities(m_Selection.GetEntities());
    }

    void SceneHierarchyPanel::SelectWithModifiers(Entity entity, bool ctrl, bool shift)
    {
        if (ctrl)
        {
            if (entity) m_Selection.Toggle(entity);
        }
        else if (shift)
        {
            if (entity) m_Selection.Add(entity, true);
        }
        else
            m_Selection.Select(entity);
    }

    void SceneHierarchyPanel::SelectAll()
    {
        if (!m_Context) return;
        Entity primary = GetSelectedEntity();
        m_Selection.Set(m_Context->GetEntitiesInHierarchyOrder(), primary);
    }

    size_t SceneHierarchyPanel::DuplicateSelection(bool skipLocked)
    {
        std::vector<Entity> roots = GetSelectedRoots();
        if (skipLocked)
            std::erase_if(roots, [&](Entity e) { return m_Context->IsLockedInEditor(e); });
        if (roots.empty()) return 0;
        Entity primary = GetSelectedEntity();
        std::vector<Entity> copies = m_Context->DuplicateEntities(roots);
        // The copy of the primary entity stays primary (or of the subtree it is in).
        Entity newPrimary;
        for (size_t i = 0; i < roots.size() && i < copies.size(); i++)
            if (roots[i] == primary || m_Context->IsAncestorOf(roots[i], primary))
                newPrimary = copies[i];
        m_Selection.Set(copies, newPrimary);
        MarkModified();
        return copies.size();
    }

    size_t SceneHierarchyPanel::DeleteSelection()
    {
        std::vector<Entity> roots = GetSelectedRoots();
        if (roots.empty()) return 0;
        m_Selection.Clear();
        m_RenamingEntity = entt::null;
        size_t count = m_Context->DestroyEntities(roots);
        MarkModified();
        return count;
    }

    // ------------------------------------------------------------------ eye / lock
    size_t SceneHierarchyPanel::SetFlagFromRow(Entity entity, bool lockFlag, bool value)
    {
        if (!m_Context || !entity || !m_SceneEditable)
            return 0;
        // A click on a row of the selection sets the flag of every selected entity.
        std::vector<Entity> targets;
        if (IsSelected(entity))
            for (Entity e : m_Selection.GetEntities())
                if (e && e.GetScene() == m_Context.get()) targets.push_back(e);
        if (targets.empty())
            targets.push_back(entity);
        bool changed = false;
        for (Entity e : targets)
        {
            bool before = lockFlag ? m_Context->IsLockedInEditor(e, false) : m_Context->IsHiddenInEditor(e, false);
            if (before == value) continue;
            if (lockFlag) m_Context->SetLockedInEditor(e, value);
            else m_Context->SetHiddenInEditor(e, value);
            changed = true;
        }
        if (changed)
            MarkModified();
        return targets.size();
    }

    size_t SceneHierarchyPanel::ToggleSelectionHidden(bool* hidden)
    {
        Entity primary = GetSelectedEntity();
        if (!m_Context || !primary || !m_SceneEditable)
            return 0;
        bool value = !m_Context->IsHiddenInEditor(primary, false);
        if (hidden) *hidden = value;
        return SetFlagFromRow(primary, false, value);
    }

    size_t SceneHierarchyPanel::ToggleSelectionLocked(bool* locked)
    {
        Entity primary = GetSelectedEntity();
        if (!m_Context || !primary || !m_SceneEditable)
            return 0;
        bool value = !m_Context->IsLockedInEditor(primary, false);
        if (locked) *locked = value;
        return SetFlagFromRow(primary, true, value);
    }

    // Clears one flag on every entity that has it (collected first: clearing may remove the
    // metadata component while the view is being walked).
    static size_t ClearEditorFlag(Scene& scene, bool lockFlag)
    {
        std::vector<Entity> flagged;
        for (auto e : scene.GetAllEntitiesWith<EditorMetadataComponent>())
        {
            const auto& m = scene.GetRegistry().get<EditorMetadataComponent>(e);
            if (lockFlag ? m.Locked : m.Hidden)
                flagged.emplace_back(e, &scene);
        }
        for (Entity e : flagged)
        {
            if (lockFlag) scene.SetLockedInEditor(e, false);
            else scene.SetHiddenInEditor(e, false);
        }
        return flagged.size();
    }

    size_t SceneHierarchyPanel::ShowAll()
    {
        if (!m_Context || !m_SceneEditable) return 0;
        size_t count = ClearEditorFlag(*m_Context, false);
        if (count) MarkModified();
        return count;
    }

    size_t SceneHierarchyPanel::UnlockAll()
    {
        if (!m_Context || !m_SceneEditable) return 0;
        size_t count = ClearEditorFlag(*m_Context, true);
        if (count) MarkModified();
        return count;
    }

    // ------------------------------------------------------------------ active flag
    // The nearest ancestor that is switched off itself (what keeps an active entity out of the game).
    static Entity FindInactiveAncestor(Scene& scene, Entity entity)
    {
        int depth = 0;
        for (Entity p = scene.GetParent(entity); p && depth < 4096; p = scene.GetParent(p), depth++)
            if (!p.IsActive())
                return p;
        return {};
    }

    // Inspector row under a component that an inactive entity switches off (camera, particles,
    // audio, animators, UI buttons): says why it does nothing right now.
    size_t SceneHierarchyPanel::SetActiveFromRow(Entity entity, bool active)
    {
        if (!m_Context || !entity || entity.GetScene() != m_Context.get())
            return 0;
        // Unlike the eye / lock this is game state: it can be changed while playing (the running
        // scene's scripts get OnDisable / OnEnable at once) and is part of the undo history.
        std::vector<Entity> targets = GetMenuTargets(entity);
        bool changed = false;
        for (Entity e : targets)
        {
            // A script's OnDisable / OnEnable may have destroyed later targets already.
            if (!e.IsValid() || e.IsActive() == active)
                continue;
            m_Context->SetActive(e, active);
            changed = true;
        }
        if (changed)
            MarkModified();
        return targets.size();
    }

    size_t SceneHierarchyPanel::ToggleSelectionActive(bool* active)
    {
        Entity primary = GetSelectedEntity();
        if (!m_Context || !primary || primary.GetScene() != m_Context.get())
            return 0;
        bool value = !primary.IsActive();
        if (active) *active = value;
        return SetActiveFromRow(primary, value);
    }

    void SceneHierarchyPanel::DrawActiveToggle(Entity entity)
    {
        // The entity's own flag, for the whole selection when it is part of it; a selection with
        // both states shows a mixed box and a click switches all of them on.
        std::vector<Entity> targets = GetMenuTargets(entity);
        const bool active = entity.IsActive();
        const bool mixed = std::any_of(targets.begin(), targets.end(), [&](Entity e) { return e.IsValid() && e.IsActive() != active; });
        bool value = active;
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
        bool clicked = ImGui::Checkbox("##Active", &value);
        ImGui::PopItemFlag();
        if (ImGui::IsItemHovered())
        {
            std::string tip = mixed ? std::string("Tanlanganlarning bir qismi nofaol. Bosing: hammasini faollashtirish")
                            : active ? std::string("Faol: obyekt o'yinda ishtirok etadi. O'chirilsa u va bolalari chizilmaydi, "
                                                   "fizikada qatnashmaydi, skriptlari OnDisable oladi va yangilanmaydi  (Alt+Shift+A)")
                                     : std::string("Nofaol: obyekt va uning bolalari o'yinda ishtirok etmaydi. Bosing: faollashtirish  (Alt+Shift+A)");
            if (targets.size() > 1)
                tip += std::format("\n{} ta tanlangan obyektga qo'llaniladi", targets.size());
            UI::Tooltip(tip.c_str());
        }
        if (clicked)
            SetActiveFromRow(entity, mixed ? true : value);
    }

    void SceneHierarchyPanel::DrawInactiveNote(Entity entity)
    {
        Scene& scene = *entity.GetScene();
        if (scene.IsActiveInHierarchy(entity))
            return;
        ImGui::SameLine();
        const ImVec4 color(0.95f, 0.55f, 0.35f, 1.0f);
        if (!entity.IsActive())
        {
            ImGui::TextColored(color, "  " ICON_FA_POWER_OFF " nofaol");
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'yinda ishtirok etmaydi (bolalari ham): chizilmaydi, fizikada yo'q, skriptlari yangilanmaydi. "
                            "Nom yonidagi belgi yoki Alt+Shift+A bilan yoqiladi");
            return;
        }
        // Switched off by an ancestor: a click selects it.
        Entity ancestor = FindInactiveAncestor(scene, entity);
        std::string text = ancestor ? std::format("  " ICON_FA_POWER_OFF " nofaol (ota: {})", ancestor.GetName())
                                    : std::string("  " ICON_FA_POWER_OFF " nofaol (ota obyekt)");
        ImGui::TextColored(ImVec4(color.x, color.y, color.z, 0.7f), "%s", text.c_str());
        if (ImGui::IsItemHovered())
            UI::Tooltip("O'zi faol, lekin ota obyekt nofaol - u bilan birga o'yinda ishtirok etmaydi. Bosing: ota obyektni tanlash");
        if (ancestor && ImGui::IsItemClicked(ImGuiMouseButton_Left))
            SetSelectedEntity(ancestor);
    }

    void SceneHierarchyPanel::DrawRowToggles(Entity entity, const ImVec2& rowMin, const ImVec2& rowMax)
    {
        const float size = rowMax.y - rowMin.y;
        const ImVec2 restore = ImGui::GetCursorScreenPos();
        const bool rowHovered = ImGui::IsMouseHoveringRect(ImVec2(m_GutterX, rowMin.y), rowMax) && ImGui::IsWindowHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // One column each: eye (hidden in the scene view), lock (not pickable in the viewport).
        // Own flag: bright icon; inherited from a parent: faint icon; off: shown on hover only.
        auto toggle = [&](const char* id, float x, bool self, bool inherited, const char* onIcon, const char* offIcon,
                          const ImVec4& onColor, const char* tip) {
            ImGui::SetCursorScreenPos(ImVec2(x, rowMin.y));
            bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size)) && m_SceneEditable;
            bool hovered = ImGui::IsItemHovered();
            const bool on = self || inherited;
            float alpha = self ? 1.0f : inherited ? 0.4f : hovered ? 0.85f : rowHovered ? 0.3f : 0.0f;
            if (hovered && !self && inherited) alpha = 0.7f;
            if (alpha > 0.0f)
            {
                ImVec4 c = on ? onColor : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                c.w *= alpha * (m_SceneEditable ? 1.0f : 0.6f);
                const char* icon = on ? onIcon : offIcon;
                ImVec2 ts = ImGui::CalcTextSize(icon);
                dl->AddText(ImVec2(x + (size - ts.x) * 0.5f, rowMin.y + (size - ts.y) * 0.5f), ImGui::GetColorU32(c), icon);
            }
            if (hovered)
                UI::Tooltip(tip);
            return clicked;
        };

        const bool selfHidden = m_Context->IsHiddenInEditor(entity, false);
        const bool inheritedHidden = !selfHidden && m_Context->IsHiddenInEditor(entity, true);
        const char* hideTip = selfHidden ? "Yashirilgan: editor sahnasida chizilmaydi (o'yinda ko'rinadi). Bosing: ko'rsatish  (H)"
                            : inheritedHidden ? "Ota obyekt yashirilgan - u bilan birga yashirin. Bosing: buni ham yashirish"
                            : "Editor sahnasida yashirish  (H)";
        if (toggle("##eye", m_GutterX, selfHidden, inheritedHidden, ICON_FA_EYE_SLASH, ICON_FA_EYE,
                   ImGui::GetStyleColorVec4(ImGuiCol_Text), hideTip))
            SetFlagFromRow(entity, false, !selfHidden);

        const bool selfLocked = m_Context->IsLockedInEditor(entity, false);
        const bool inheritedLocked = !selfLocked && m_Context->IsLockedInEditor(entity, true);
        const char* lockTip = selfLocked ? "Qulflangan: viewport'da tanlanmaydi va gizmo bilan surilmaydi. Bosing: qulfni ochish"
                            : inheritedLocked ? "Ota obyekt qulflangan - u bilan birga qulflangan. Bosing: buni ham qulflash"
                            : "Qulflash: viewport'da tanlanmaydi va surilmaydi (ierarxiya va inspektorda tahrirlash mumkin)";
        if (toggle("##lock", m_GutterX + size, selfLocked, inheritedLocked, ICON_FA_LOCK, ICON_FA_LOCK_OPEN,
                   ImVec4(0.96f, 0.72f, 0.32f, 1.0f), lockTip))
            SetFlagFromRow(entity, true, !selfLocked);

        ImGui::SetCursorScreenPos(restore);
    }

    void SceneHierarchyPanel::OnImGuiRender(bool* showHierarchy, bool* showInspector)
    {
        if (*showHierarchy)
        {
            if (ImGui::Begin(Tr(ICON_FA_LIST "  Ierarxiya###Hierarchy"), showHierarchy))
                DrawHierarchy();
            ImGui::End();
        }
        // An entity picked (hierarchy, viewport, menus) after the asset: the inspector shows entities again.
        if (m_AssetInspector && m_Selection.GetRevision() != m_SeenSelectionRevision)
        {
            m_SeenSelectionRevision = m_Selection.GetRevision();
            m_AssetInspector->Clear();
        }
        if (*showInspector)
        {
            if (ImGui::Begin(Tr(ICON_FA_SLIDERS "  Inspektor###Inspector"), showInspector))
            {
                if (m_AssetInspector && m_AssetInspector->HasAsset())
                    m_AssetInspector->OnImGuiRender();
                else
                    DrawInspector();
                if (m_ScrollInspectorFrames > 0) // automation: show the last components
                {
                    ImGui::SetScrollY(m_ScrollInspectorY < 0.0f ? ImGui::GetScrollMaxY() : std::min(m_ScrollInspectorY, ImGui::GetScrollMaxY()));
                    m_ScrollInspectorFrames--;
                }
            }
            ImGui::End();
        }
        // A particle preset dropped onto the hierarchy (the inspector applies its own after drawing).
        ApplyPendingParticlePreset();
        // Last: these replace the scene the panels were drawing.
        ApplyPendingSceneRequests();
    }

    void SceneHierarchyPanel::ApplyPendingSceneRequests()
    {
        std::filesystem::path open = std::move(m_PendingOpenScene);
        Ref<Scene> runtime = std::move(m_PendingRuntimeScene);
        m_PendingOpenScene.clear();
        m_PendingRuntimeScene.reset();
        if (!open.empty() && OnOpenSceneRequested)
            OnOpenSceneRequested(open);
        else if (runtime && OnRuntimeSceneSelected)
            OnRuntimeSceneSelected(runtime);
    }

    // ------------------------------------------------------------------ presets
    Entity SceneHierarchyPanel::CreateEntityPreset(const char* preset)
    {
        std::string p = preset;
        Entity e;
        auto mesh = [&](const char* name, PrimitiveMesh type) {
            e = m_Context->CreateEntity(name);
            auto& mr = e.AddComponent<MeshRendererComponent>();
            mr.Type = type;
            mr.MeshAsset = MeshLibrary::GetPrimitive(type);
        };

        if (p == "Empty") e = m_Context->CreateEntity("Bo'sh obyekt");
        else if (p == "Sprite") { e = m_Context->CreateEntity("Sprite"); e.AddComponent<SpriteRendererComponent>(); }
        else if (p == "Circle") { e = m_Context->CreateEntity("Doira"); e.AddComponent<CircleRendererComponent>(); }
        else if (p == "Text")
        {
            e = m_Context->CreateEntity("Matn");
            auto& t = e.AddComponent<TextComponent>();
            t.Text = "Salom, Zamin!";
            t.FontAsset = Font::GetDefault();
        }
        else if (p == "Terrain")
        {
            e = m_Context->CreateEntity("Terrain");
            e.AddComponent<TerrainComponent>().Reset(65, { 64, 64 }, 8);
            e.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
            e.AddComponent<MeshCollider3DComponent>();
            if (OnTerrainWindowRequested) OnTerrainWindowRequested();
        }
        else if (p == "Cube") mesh("Kub", PrimitiveMesh::Cube);
        else if (p == "Sphere") mesh("Shar", PrimitiveMesh::Sphere);
        else if (p == "Plane") { mesh("Tekislik", PrimitiveMesh::Plane); e.GetTransform().Scale = { 10, 1, 10 }; }
        else if (p == "Cylinder") mesh("Silindr", PrimitiveMesh::Cylinder);
        else if (p == "Cone") mesh("Konus", PrimitiveMesh::Cone);
        else if (p == "Model") { mesh("Model", PrimitiveMesh::Model); e.GetComponent<MeshRendererComponent>().MeshAsset = nullptr; }
        else if (p == "Camera") { e = m_Context->CreateEntity("Kamera"); e.AddComponent<CameraComponent>().Primary = !m_Context->GetPrimaryCameraEntity(true); }
        else if (p == "Camera2D")
        {
            e = m_Context->CreateEntity("2D Kamera");
            auto& c = e.AddComponent<CameraComponent>();
            c.Primary = !m_Context->GetPrimaryCameraEntity(true);
            c.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
        }
        else if (p == "DirectionalLight")
        {
            e = m_Context->CreateEntity("Quyosh");
            e.AddComponent<DirectionalLightComponent>();
            e.GetTransform().Rotation = { -0.9f, 0.6f, 0.0f };
        }
        else if (p == "PointLight") { e = m_Context->CreateEntity("Nuqtali yorug'lik"); e.AddComponent<PointLightComponent>(); e.GetTransform().Translation = { 0, 2, 0 }; }
        else if (p == "SpotLight")
        {
            // Hangs 4 m up, pointing straight down (the light travels along -Z).
            e = m_Context->CreateEntity("Spot yorug'lik");
            e.AddComponent<SpotLightComponent>();
            e.GetTransform().Translation = { 0, 4, 0 };
            e.GetTransform().Rotation = { -glm::half_pi<float>(), 0.0f, 0.0f };
        }
        else if (p == "Physics Box")
        {
            e = m_Context->CreateEntity("Fizik quti");
            e.AddComponent<SpriteRendererComponent>(glm::vec4(0.9f, 0.6f, 0.2f, 1.0f));
            e.AddComponent<Rigidbody2DComponent>();
            e.AddComponent<BoxCollider2DComponent>();
        }
        else if (p == "Physics Ball")
        {
            e = m_Context->CreateEntity("Fizik shar");
            e.AddComponent<CircleRendererComponent>().Color = { 0.3f, 0.7f, 0.95f, 1.0f };
            e.AddComponent<Rigidbody2DComponent>();
            e.AddComponent<CircleCollider2DComponent>();
        }
        else if (p == "Physics Cube")
        {
            mesh("Fizik kub", PrimitiveMesh::Cube);
            e.GetTransform().Translation = { 0.0f, 3.0f, 0.0f };
            e.AddComponent<Rigidbody3DComponent>();
            e.AddComponent<BoxCollider3DComponent>();
        }
        else if (p == "Physics Sphere")
        {
            mesh("Fizik shar", PrimitiveMesh::Sphere);
            e.GetTransform().Translation = { 0.0f, 3.0f, 0.0f };
            e.AddComponent<Rigidbody3DComponent>();
            e.AddComponent<SphereCollider3DComponent>();
        }
        else if (p == "Physics Ground")
        {
            mesh("Fizik yer", PrimitiveMesh::Cube);
            e.GetTransform().Scale = { 20.0f, 0.2f, 20.0f };
            e.GetTransform().Translation = { 0.0f, -0.1f, 0.0f };
            e.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
            e.AddComponent<BoxCollider3DComponent>();
        }
        else if (p == "Fire" || p == "Sparks" || p == "Smoke")
        {
            // The particle library's effects (the Effects menu lists all of them).
            if (std::optional<ParticlePreset> preset = ParticlePresets::GetBuiltin(p))
                e = ParticlePresets::Instantiate(*m_Context, *preset);
        }
        else if (p == "UI Text")
        {
            e = m_Context->CreateEntity("UI Matn");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.Position = { 30.0f, -40.0f }; ui.Size = { 300.0f, 36.0f };
            auto& t = e.AddComponent<TextComponent>();
            t.Text = "Hisob: 0";
            t.FontAsset = Font::GetDefault();
        }
        else if (p == "UI Image")
        {
            e = m_Context->CreateEntity("UI Rasm");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.SetAnchor(UITransformComponent::AnchorPoint::Center); ui.Position = { 0, 0 }; ui.Size = { 128, 128 };
            e.AddComponent<UIImageComponent>();
        }
        else if (p == "UI Slider")
        {
            e = m_Context->CreateEntity("UI Slayder");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.SetAnchor(UITransformComponent::AnchorPoint::Center); ui.Position = { 0, 0 }; ui.Size = { 260, 28 };
            e.AddComponent<UISliderComponent>();
        }
        else if (p == "UI Toggle")
        {
            e = m_Context->CreateEntity("UI Belgilash");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.SetAnchor(UITransformComponent::AnchorPoint::Center); ui.Position = { 0, 0 }; ui.Size = { 260, 32 };
            ui.FontSize = 22.0f;
            e.AddComponent<UIToggleComponent>();
            auto& t = e.AddComponent<TextComponent>();
            t.Text = "Ovoz yoqilgan";
            t.FontAsset = Font::GetDefault();
        }
        else if (p == "UI Progress")
        {
            e = m_Context->CreateEntity("UI Progress");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.SetAnchor(UITransformComponent::AnchorPoint::Center); ui.Position = { 0, 0 }; ui.Size = { 260, 26 };
            e.AddComponent<UIProgressBarComponent>().ShowPercentage = true;
        }
        else if (p == "UI Input")
        {
            e = m_Context->CreateEntity("UI Matn maydoni");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.SetAnchor(UITransformComponent::AnchorPoint::Center); ui.Position = { 0, 0 }; ui.Size = { 300, 44 };
            ui.FontSize = 22.0f;
            e.AddComponent<UIInputFieldComponent>().Placeholder = "Ismingiz...";
        }
        else if (p == "UI Canvas")
        {
            e = m_Context->CreateEntity("Canvas");
            e.AddComponent<UITransformComponent>().SetStretch();
            e.AddComponent<UICanvasComponent>();
        }
        else if (p == "UI Menu")
        {
            // A panel whose vertical layout group stacks three buttons (it grows with them).
            e = m_Context->CreateEntity("UI Menyu");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.SetAnchor(UITransformComponent::AnchorPoint::Center);
            ui.Position = { 0.0f, 0.0f };
            ui.Size = { 320.0f, 240.0f };
            auto& image = e.AddComponent<UIImageComponent>();
            image.Color = { 0.08f, 0.09f, 0.12f, 0.92f };
            auto& group = e.AddComponent<UILayoutGroupComponent>();
            group.Type = UILayoutType::Vertical;
            group.Padding = glm::vec4(20.0f);
            group.Spacing = 12.0f;
            group.ChildAlignment = 1;
            group.ControlChildWidth = group.ChildForceExpandWidth = true;
            group.FitHeight = true;
            const char* labels[] = { "Davom etish", "Sozlamalar", "Chiqish" };
            for (int i = 0; i < 3; i++)
            {
                Entity button = m_Context->CreateEntity(labels[i]);
                auto& bt = button.AddComponent<UITransformComponent>();
                bt.SetAnchor(UITransformComponent::AnchorPoint::Center);
                bt.Position = { 0.0f, 0.0f };
                bt.Size = { 260.0f, 52.0f };
                bt.FontSize = 24.0f;
                button.AddComponent<UIButtonComponent>();
                auto& t = button.AddComponent<TextComponent>();
                t.Text = labels[i];
                t.AlignH = TextAlignH::Center;
                t.FontAsset = Font::GetDefault();
                if (i == 0)
                    button.AddComponent<UINavigationComponent>().SelectOnEnable = true;
                m_Context->SetParent(button, e, false);
            }
        }
        else if (p == "UI Scroll View")
        {
            // A panel that clips and scrolls a list: its content (a vertical layout group that grows
            // with its rows) is pinned to the top and stretched across.
            e = m_Context->CreateEntity("UI Scroll View");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.SetAnchor(UITransformComponent::AnchorPoint::Center);
            ui.Position = { 0.0f, 0.0f };
            ui.Size = { 360.0f, 280.0f };
            e.AddComponent<UIImageComponent>().Color = { 0.08f, 0.09f, 0.12f, 0.92f };
            e.AddComponent<UIMaskComponent>();
            auto& scroll = e.AddComponent<UIScrollViewComponent>();
            Entity content = m_Context->CreateEntity("Content");
            auto& ct = content.AddComponent<UITransformComponent>();
            ct.AnchorMin = { 0.0f, 1.0f };
            ct.AnchorMax = { 1.0f, 1.0f };
            ct.Pivot = { 0.5f, 1.0f };
            ct.Position = { 0.0f, 0.0f };
            ct.Size = { 0.0f, 400.0f };
            auto& group = content.AddComponent<UILayoutGroupComponent>();
            group.Type = UILayoutType::Vertical;
            group.Padding = glm::vec4(12.0f);
            group.Spacing = 8.0f;
            group.ControlChildWidth = group.ChildForceExpandWidth = true;
            group.FitHeight = true;
            m_Context->SetParent(content, e, false);
            scroll.Content.ID = content.GetUUID();
            for (int i = 0; i < 8; i++)
            {
                Entity row = m_Context->CreateEntity(std::format("Qator {}", i + 1));
                auto& rt = row.AddComponent<UITransformComponent>();
                rt.Size = { 300.0f, 44.0f };
                rt.FontSize = 20.0f;
                row.AddComponent<UIButtonComponent>();
                auto& t = row.AddComponent<TextComponent>();
                t.Text = std::format("Element {}", i + 1);
                t.AlignH = TextAlignH::Center;
                t.FontAsset = Font::GetDefault();
                m_Context->SetParent(row, content, false);
            }
        }
        else if (p == "UI Button")
        {
            e = m_Context->CreateEntity("UI Tugma");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.SetAnchor(UITransformComponent::AnchorPoint::Center); ui.Position = { 0, 0 }; ui.Size = { 220, 56 };
            e.AddComponent<UIButtonComponent>();
            auto& t = e.AddComponent<TextComponent>();
            t.Text = "Boshlash"; t.AlignH = TextAlignH::Center;
            t.FontAsset = Font::GetDefault();
            ui.FontSize = 26.0f;
        }
        else if (p == "Character3D")
        {
            mesh("Qahramon", PrimitiveMesh::Cylinder);
            e.GetTransform().Scale = { 0.7f, 1.8f, 0.7f };
            e.GetTransform().Translation = { 0.0f, 0.0f, 0.0f };
            e.AddComponent<CharacterController3DComponent>();
        }
        else if (p == "Tilemap")
        {
            e = m_Context->CreateEntity("Tilemap");
            auto& tm = e.AddComponent<TilemapComponent>();
            tm.EnsureSize();
            m_TilePaint = true;
        }
        else if (p == "Audio")
        {
            e = m_Context->CreateEntity("Ovoz manbai");
            auto& audio = e.AddComponent<AudioSourceComponent>();
            audio.MinDistance = Audio::GetSpatialDefaults().MinDistance;
            audio.MaxDistance = Audio::GetSpatialDefaults().MaxDistance;
        }

        if (e)
        {
            SetSelectedEntity(e);
            MarkModified();
        }
        return e;
    }

    void SceneHierarchyPanel::DrawCreateMenu()
    {
        if (ImGui::MenuItem(Tr(ICON_FA_BOX "  Bo'sh obyekt"))) CreateEntityPreset("Empty");
        ImGui::Separator();
        if (ImGui::BeginMenu(Tr(ICON_FA_SHAPES "  2D")))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_IMAGE "  Sprite"))) CreateEntityPreset("Sprite");
            if (ImGui::MenuItem(Tr(ICON_FA_CIRCLE "  Doira"))) CreateEntityPreset("Circle");
            if (ImGui::MenuItem(Tr(ICON_FA_FONT "  Matn"))) CreateEntityPreset("Text");
            if (ImGui::MenuItem(Tr(ICON_FA_TABLE_CELLS "  Tilemap"))) CreateEntityPreset("Tilemap");
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_BOX "  Fizik quti"))) CreateEntityPreset("Physics Box");
            if (ImGui::MenuItem(Tr(ICON_FA_CIRCLE "  Fizik shar"))) CreateEntityPreset("Physics Ball");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_CUBE "  3D")))
        {
            if (ImGui::MenuItem("Terrain / map")) CreateEntityPreset("Terrain");
            if (ImGui::MenuItem(Tr(ICON_FA_CUBE "  Kub"))) CreateEntityPreset("Cube");
            if (ImGui::MenuItem(Tr(ICON_FA_CIRCLE "  Shar"))) CreateEntityPreset("Sphere");
            if (ImGui::MenuItem(Tr(ICON_FA_SQUARE "  Tekislik"))) CreateEntityPreset("Plane");
            if (ImGui::MenuItem(Tr(ICON_FA_DRAW_POLYGON "  Silindr"))) CreateEntityPreset("Cylinder");
            if (ImGui::MenuItem(Tr(ICON_FA_MOUNTAIN_SUN "  Konus"))) CreateEntityPreset("Cone");
            if (ImGui::MenuItem(Tr(ICON_FA_CUBES "  Model (glTF / OBJ)"))) CreateEntityPreset("Model");
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_ATOM "  Fizik kub"))) CreateEntityPreset("Physics Cube");
            if (ImGui::MenuItem(Tr(ICON_FA_ATOM "  Fizik shar"))) CreateEntityPreset("Physics Sphere");
            if (ImGui::MenuItem(Tr(ICON_FA_ATOM "  Fizik yer (statik)"))) CreateEntityPreset("Physics Ground");
            if (ImGui::MenuItem(Tr(ICON_FA_PERSON_RUNNING "  Qahramon (Character Controller)"))) CreateEntityPreset("Character3D");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_LIGHTBULB "  Yorug'lik")))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_SUN "  Quyosh (yo'naltirilgan)"))) CreateEntityPreset("DirectionalLight");
            if (ImGui::MenuItem(Tr(ICON_FA_LIGHTBULB "  Nuqtali"))) CreateEntityPreset("PointLight");
            if (ImGui::MenuItem(Tr(ICON_FA_BULLHORN "  Spot (konus)"))) CreateEntityPreset("SpotLight");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_CAMERA "  Kamera")))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_CAMERA "  Perspektiv (3D)"))) CreateEntityPreset("Camera");
            if (ImGui::MenuItem(Tr(ICON_FA_CAMERA "  Ortografik (2D)"))) CreateEntityPreset("Camera2D");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_WAND_MAGIC_SPARKLES "  Effektlar")))
        {
            // The particle preset library: each makes a new entity with that effect.
            if (DrawParticlePresetItems({}))
                ApplyPendingParticlePreset();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_TABLE_CELLS "  UI (ekran)")))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_FONT "  Matn"))) CreateEntityPreset("UI Text");
            if (ImGui::MenuItem(Tr(ICON_FA_IMAGE "  Rasm"))) CreateEntityPreset("UI Image");
            if (ImGui::MenuItem(Tr(ICON_FA_HAND_POINTER "  Tugma"))) CreateEntityPreset("UI Button");
            if (ImGui::MenuItem(Tr(ICON_FA_SLIDERS "  Slayder"))) CreateEntityPreset("UI Slider");
            if (ImGui::MenuItem(Tr(ICON_FA_SQUARE_CHECK "  Belgilash (toggle)"))) CreateEntityPreset("UI Toggle");
            if (ImGui::MenuItem(Tr(ICON_FA_BARS_PROGRESS "  Progress bar"))) CreateEntityPreset("UI Progress");
            if (ImGui::MenuItem(Tr(ICON_FA_I_CURSOR "  Matn maydoni (input)"))) CreateEntityPreset("UI Input");
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_EXPAND "  Canvas (ekranga moslashadi)"))) CreateEntityPreset("UI Canvas");
            if (ImGui::MenuItem(Tr(ICON_FA_LIST "  Menyu (vertikal layout)"))) CreateEntityPreset("UI Menu");
            if (ImGui::MenuItem(Tr(ICON_FA_ARROWS_UP_DOWN "  Scroll view (ro'yxat)"))) CreateEntityPreset("UI Scroll View");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(Tr(ICON_FA_VOLUME_HIGH "  Ovoz manbai"))) CreateEntityPreset("Audio");
    }

    // ------------------------------------------------------------------ hierarchy
    void SceneHierarchyPanel::DrawEntityContextMenu(Entity entity)
    {
        if (!ImGui::BeginPopupContextItem("##entityctx"))
            return;
        // Right-click inside a multi-selection keeps it (the menu acts on all of it).
        if (!IsSelected(entity))
            SetSelectedEntity(entity);
        else if (GetSelectedEntity() != entity)
            m_Selection.Add(entity, true);
        const size_t count = m_Selection.Size();
        if (count > 1)
        {
            ImGui::TextDisabled(ICON_FA_OBJECT_GROUP "  %zu ta obyekt tanlangan", count);
            ImGui::Separator();
        }

        if (ImGui::BeginMenu(Tr(ICON_FA_PLUS "  Bola obyekt yaratish")))
        {
            Entity before = entity;
            DrawCreateMenu();
            Entity created = GetSelectedEntity();
            if (created && created != before)
            {
                m_Context->SetParent(created, before, false);
                MarkModified();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(Tr(ICON_FA_FEATHER "  Nomini o'zgartirish"), "F2"))
        {
            SetSelectedEntity(entity);
            m_RenamingEntity = entity;
            m_RenameBuffer = entity.GetName();
        }
        std::string duplicateLabel = count > 1 ? std::format(ICON_FA_CLONE "  Nusxa olish ({} ta)", count) : std::string(ICON_FA_CLONE "  Nusxa olish");
        if (ImGui::MenuItem(duplicateLabel.c_str(), "Ctrl+D")) m_PendingDuplicate = true;
        if (ImGui::MenuItem(Tr(ICON_FA_BULLSEYE "  Fokus"), "F")) { if (OnFocusRequested) OnFocusRequested(entity); }
        std::vector<Entity> parented;
        for (Entity e : GetSelectedRoots())
            if (m_Context->GetParent(e))
                parented.push_back(e);
        if (!parented.empty() && ImGui::MenuItem(Tr(ICON_FA_ARROW_UP "  Ota-onadan ajratish")))
        {
            for (Entity e : parented)
                if (m_Context->SetParent(e, {}, true))
                    m_Context->SetSiblingIndex(e, -1);
            MarkModified();
        }
        if (ImGui::MenuItem(Tr(ICON_FA_BOX "  Prefab sifatida saqlash")) && OnSavePrefabRequested)
            OnSavePrefabRequested(entity);
        DrawPrefabMenu(entity);
        ImGui::Separator();
        {
            // Applied to the whole selection (SetFlagFromRow), like the row buttons.
            const bool hidden = m_Context->IsHiddenInEditor(entity, false);
            const bool locked = m_Context->IsLockedInEditor(entity, false);
            if (ImGui::MenuItem(hidden ? ICON_FA_EYE "  Ko'rsatish" : ICON_FA_EYE_SLASH "  Yashirish (editorda)", "H", false, m_SceneEditable))
                SetFlagFromRow(entity, false, !hidden);
            if (ImGui::MenuItem(locked ? ICON_FA_LOCK_OPEN "  Qulfni ochish" : ICON_FA_LOCK "  Qulflash (viewport'da)", nullptr, false, m_SceneEditable))
                SetFlagFromRow(entity, true, !locked);
            // Game state: also while playing.
            const bool active = entity.IsActive();
            if (ImGui::MenuItem(active ? ICON_FA_TOGGLE_OFF "  Nofaol qilish" : ICON_FA_TOGGLE_ON "  Faollashtirish", "Alt+Shift+A"))
                SetActiveFromRow(entity, !active);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(Tr(ICON_FA_CHECK_DOUBLE "  Hammasini tanlash"), "Ctrl+A")) SelectAll();
        std::string deleteLabel = count > 1 ? std::format(ICON_FA_TRASH "  O'chirish ({} ta)", count) : std::string(ICON_FA_TRASH "  O'chirish");
        if (ImGui::MenuItem(deleteLabel.c_str(), "Delete")) m_PendingDelete = true;
        ImGui::EndPopup();
    }

    bool SceneHierarchyPanel::IsRowOpen(Entity entity) const
    {
        // The id the row's tree node has (DrawEntityNode: PushID(entity), "##node"); open by default.
        ImGui::PushID((int)(uint32_t)(entt::entity)entity);
        const ImGuiID id = ImGui::GetID("##node");
        ImGui::PopID();
        return ImGui::GetStateStorage()->GetInt(id, 1) != 0;
    }

    void SceneHierarchyPanel::BuildHierarchyRows(const std::string& filter)
    {
        m_Rows.clear();
        if (!filter.empty())
        {
            for (Entity entity : m_Context->GetEntitiesInOrder())
                if (MatchesFilter(entity, filter))
                    m_Rows.push_back({ entity, 0, false });
            return;
        }
        // Depth first: a row, then (when it is open) its children.
        auto add = [&](auto& self, Entity entity, int depth) -> void {
            const auto& children = entity.GetComponent<RelationshipComponent>().Children;
            m_Rows.push_back({ entity, depth, !children.empty() });
            if (children.empty() || !IsRowOpen(entity))
                return;
            for (UUID child : children)
                if (Entity c = m_Context->GetEntityByUUID(child))
                    self(self, c, depth + 1);
        };
        for (Entity entity : m_Context->GetEntitiesInOrder())
        {
            if ((uint64_t)entity.GetComponent<RelationshipComponent>().Parent && m_Context->GetParent(entity))
                continue; // under its parent
            add(add, entity, 0);
        }
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity, int depth, bool hasChildren)
    {
        entt::entity handle = entity;
        const std::string& tag = entity.GetName(); // not used after the context menu (see below)
        bool selected = IsSelected(entity);
        bool primary = selected && GetSelectedEntity() == entity;
        bool hiddenInView = m_Context->IsHiddenInEditor(entity);
        // Not active in the hierarchy (its own flag, or an ancestor's): the whole row is faded.
        bool inactiveSelf = !entity.IsActive();
        bool inactive = inactiveSelf || !m_Context->IsActiveInHierarchy(entity);

        ImGui::PushID((int)(uint32_t)handle);
        // The tree's depth as indentation (the rows are a flat list: no tree node pushes another level).
        const float indent = (float)depth * ImGui::GetStyle().IndentSpacing;
        if (indent > 0.0f)
            ImGui::Indent(indent);

        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 max = ImVec2(min.x + ImGui::GetContentRegionAvail().x, min.y + ImGui::GetFrameHeight());
        if (m_RowIndex++ % 2 == 1) // the stripe covers the eye / lock column too
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(m_GutterX, min.y), max, IM_COL32(255, 255, 255, 6));

        if (m_RenamingEntity == handle)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                if (!m_RenameBuffer.empty())
                    entity.SetName(m_RenameBuffer);
                m_RenamingEntity = entt::null;
                MarkModified();
            }
            else if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
                m_RenamingEntity = entt::null;
            if (indent > 0.0f)
                ImGui::Unindent(indent);
            ImGui::PopID();
            return;
        }

        // A sub-scene host lists the sub-scene's content under it (read-only rows).
        Ref<Scene> subScene = m_Filter.empty() && GetSubScene && entity.HasComponent<SubSceneComponent>() ? GetSubScene(entity) : nullptr;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding |
                                   ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (!hasChildren && !subScene) flags |= ImGuiTreeNodeFlags_Leaf;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;

        // The primary entity is highlighted stronger than the rest of a multi-selection.
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, primary ? UI::AccentDarkColor(0.75f) : UI::AccentDarkColor(0.38f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, primary ? ImVec4(0.14f, 0.58f, 0.46f, 0.85f) : ImVec4(0.14f, 0.58f, 0.46f, 0.55f));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        // Hidden in the scene view: a grey label; inactive (game state): a translucent one; an entity
        // of a prefab instance: the prefab colour.
        ImVec4 prefabColor;
        const bool prefabRow = !hiddenInView && GetPrefabRowColor(entity, prefabColor);
        const bool dimmed = hiddenInView || inactive || prefabRow;
        if (dimmed)
        {
            ImVec4 color = prefabRow ? prefabColor : ImGui::GetStyleColorVec4(hiddenInView ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            if (inactive)
                color.w *= 0.42f;
            ImGui::PushStyleColor(ImGuiCol_Text, color);
        }
        std::string label = std::string(GetRowIcon(entity)) + "  " + tag;
        bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
        if (dimmed)
            ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (selected)
            ImGui::PopStyleColor(2);
        const ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
        // Switched off itself (not only through a parent): a power icon at the row's right end.
        if (inactiveSelf)
        {
            const char* icon = ICON_FA_POWER_OFF;
            ImVec2 size = ImGui::CalcTextSize(icon);
            ImVec2 pos(rowMax.x - size.x - 6.0f, rowMin.y + (rowMax.y - rowMin.y - size.y) * 0.5f);
            ImVec4 color(0.95f, 0.55f, 0.35f, 0.8f);
            ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(color), icon);
            if (ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)) && ImGui::IsWindowHovered())
                UI::Tooltip("Nofaol: o'yinda ishtirok etmaydi (bolalari ham). Inspektordagi belgi yoki Alt+Shift+A bilan yoqiladi");
        }

        // Selection: plain click = only this row, Ctrl = toggle, Shift = range from the anchor.
        // Pressing a row of a multi-selection may start dragging all of it, so a plain click
        // there is decided on release (when the mouse did not move).
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        {
            if (!io.KeyCtrl && !io.KeyShift && selected && m_Selection.Size() > 1)
                m_ClickedSelected = entity;
            else
                m_PendingClick = { entity, io.KeyCtrl, io.KeyShift };
        }
        if (m_ClickedSelected == entity && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            const float threshold = io.MouseDragThreshold;
            if (ImGui::IsItemHovered() && io.MouseDragMaxDistanceSqr[0] < threshold * threshold)
                m_PendingClick = { entity, false, false };
            m_ClickedSelected = {};
        }
        // Prefab mode: a prefab nested in it opens on its own (one level deeper).
        if (m_PrefabStageMode && m_SceneEditable && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) &&
            PrefabUtility::IsInstanceRoot(entity) && OnOpenPrefabRequested)
            OnOpenPrefabRequested(PrefabUtility::GetPrefabPath(entity), 0);
        else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !hasChildren && OnFocusRequested)
            OnFocusRequested(entity);

        // drag & drop: reorder (row edges), parent (row middle); a multi-selection moves together
        if (ImGui::BeginDragDropSource())
        {
            uint64_t id = entity.GetUUID();
            ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &id, sizeof(uint64_t));
            size_t count = selected ? GetSelectedRoots().size() : 1;
            if (count > 1)
                ImGui::Text(ICON_FA_OBJECT_GROUP "  %zu ta obyekt", count);
            else
                ImGui::Text("%s  %s", EntityIcon(entity), tag.c_str());
            ImGui::EndDragDropSource();
        }
        DrawRowDropTarget(entity, open, hasChildren);

        // The menu can switch entities off / on while playing: script callbacks may then destroy
        // entities (even this one), so component references from above are not used after it.
        DrawEntityContextMenu(entity);
        const bool alive = entity.IsValid();
        if (alive)
        {
            DrawRowToggles(entity, rowMin, rowMax);
            DrawPrefabRowMarks(entity, rowMin, rowMax);
        }

        // (Its children are the next rows of the list.) A sub-scene's content right under it.
        if (open && subScene && alive && entity.HasComponent<SubSceneComponent>())
        {
            const float spacing = ImGui::GetStyle().IndentSpacing;
            ImGui::Indent(spacing);
            DrawSubSceneRows(entity, subScene);
            ImGui::Unindent(spacing);
        }
        if (indent > 0.0f)
            ImGui::Unindent(indent);
        ImGui::PopID();
    }

    void SceneHierarchyPanel::DrawSubSceneRows(Entity host, const Ref<Scene>& sub)
    {
        const std::string path = host.GetComponent<SubSceneComponent>().ScenePath;
        const bool playing = m_SceneManager != nullptr;
        ImGui::PushID("subScene");
        auto stripe = [&]() {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + ImGui::GetFrameHeight());
            if (m_RowIndex++ % 2 == 1)
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(m_GutterX, min.y), max, IM_COL32(255, 255, 255, 6));
        };
        // The sub-scene itself: its name and size, in the sub-scene colour.
        stripe();
        const size_t count = sub->GetRegistry().view<IDComponent>().size();
        const std::string label = std::format(ICON_FA_LAYER_GROUP "  {}  ({} obyekt)", sub->GetName(), count);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.52f, 0.74f, 0.98f, 0.95f));
        const bool open = ImGui::TreeNodeEx("##sub", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                            ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str());
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            UI::Tooltip(playing ? std::format("O'yinda yuklangan sub-sahna: {}\nIkki marta bosing: uning obyektlarini ierarxiyada ko'rsatish", path).c_str()
                                : std::format("Sub-sahna: {}\nFaqat ko'rish uchun (o'yinda shu joyga yuklanadi).\nIkki marta bosing: "
                                              "tahrirlash uchun faylni ochish", path).c_str());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            // Deferred: the rest of the hierarchy and the inspector are still drawn from this scene.
            if (playing)
                m_PendingRuntimeScene = sub;
            else if (!path.empty())
                m_PendingOpenScene = path;
        }
        if (open)
        {
            // Its entities as they are in it, greyed (no selection, no drag & drop); a long list is cut.
            int budget = 200;
            const ImVec4 grey = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            auto drawRow = [&](auto& self, Entity e) -> void {
                if (budget-- <= 0)
                    return;
                const auto& children = e.GetComponent<RelationshipComponent>().Children;
                stripe();
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DefaultOpen;
                if (children.empty())
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
                ImGui::PushStyleColor(ImGuiCol_Text, grey);
                const bool rowOpen = ImGui::TreeNodeEx((void*)(uintptr_t)(uint32_t)(entt::entity)e, flags, "%s  %s", EntityIcon(e), e.GetName().c_str());
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{} - '{}' sub-sahnasidan (faqat ko'rish uchun)", e.GetName(), sub->GetName()).c_str());
                if (rowOpen && !children.empty())
                {
                    for (UUID child : children)
                        if (Entity c = sub->GetEntityByUUID(child))
                            self(self, c);
                    ImGui::TreePop();
                }
            };
            for (Entity root : sub->GetRootEntities())
                drawRow(drawRow, root);
            if (budget < 0)
                ImGui::TextDisabled("   ... (ko'p obyekt: ro'yxat qisqartirildi)");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneHierarchyPanel::DrawRowDropTarget(Entity entity, bool open, bool hasChildren)
    {
        if (!ImGui::BeginDragDropTarget())
            return;

        const ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
        const ImGuiPayload* active = ImGui::GetDragDropPayload();
        if (active && active->IsDataType("HIERARCHY_ENTITY"))
        {
            // Upper quarter: before the row, lower quarter: after it (or as the first child of
            // an open parent), the middle: into it. A filtered (flat) list only parents.
            const float h = rowMax.y - rowMin.y, y = ImGui::GetMousePos().y;
            DropZone zone = DropZone::Into;
            if (m_Filter.empty())
            {
                if (y < rowMin.y + h * 0.25f) zone = DropZone::Before;
                else if (y > rowMax.y - h * 0.25f) zone = open && hasChildren ? DropZone::FirstChild : DropZone::After;
            }
            Entity dragged = m_Context->GetEntityByUUID(*(const uint64_t*)active->Data);
            bool self = dragged == entity && (zone == DropZone::Into || m_Selection.Size() <= 1 || !IsSelected(dragged));
            const Entity newParent = zone == DropZone::Into || zone == DropZone::FirstChild ? entity : m_Context->GetParent(entity);
            if (!self && dragged && !CanReparent(dragged, newParent))
            {
                UI::Tooltip(Tr(ICON_FA_BOX "  Prefab ichidagi obyektni tashqariga chiqarib bo'lmaydi (avval bog'lanishni uzing)"));
                self = true;
            }
            if (!self)
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY",
                        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImU32 color = UI::AccentU32();
                    if (zone == DropZone::Into)
                        dl->AddRect(rowMin, rowMax, color, 3.0f, 0, 2.0f);
                    else
                    {
                        float lineY = zone == DropZone::Before ? rowMin.y : rowMax.y;
                        float x0 = rowMin.x + (zone == DropZone::FirstChild ? ImGui::GetStyle().IndentSpacing : 0.0f);
                        dl->AddLine(ImVec2(x0, lineY), ImVec2(rowMax.x, lineY), color, 2.0f);
                        dl->AddCircleFilled(ImVec2(x0 + 3.0f, lineY), 3.5f, color);
                    }
                    if (payload->IsDelivery())
                        m_PendingDrop = { *(const uint64_t*)payload->Data, entity, zone };
                }
            }
        }
        if (active && active->IsDataType("CONTENT_BROWSER_ITEM") && AssetFiles::GetType((const char*)active->Data) == AssetType::Material)
        {
            // A material onto a row: that entity's mesh renderer uses it (only rows that have one light up).
            if (entity.HasComponent<MeshRendererComponent>())
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    if (OnMaterialDropped)
                        OnMaterialDropped((const char*)payload->Data, entity);
        }
        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
        {
            std::filesystem::path path((const char*)payload->Data);
            if (path.extension() == ".zprefab" && OnPrefabDropped)
                OnPrefabDropped(path, entity);
            else if (path.extension() == ParticlePresets::Extension && m_SceneEditable)
                RequestParticlePresetFile(entity, path); // the entity takes the effect (after the hierarchy is drawn)
        }
        ImGui::EndDragDropTarget();
    }

    void SceneHierarchyPanel::ApplyPendingClick()
    {
        PendingClick click = m_PendingClick;
        m_PendingClick = {};
        if (!click.Target)
            return;
        if (click.Shift)
            m_Selection.SelectRange(m_VisibleRows, click.Target, click.Ctrl);
        else if (click.Ctrl)
            m_Selection.Toggle(click.Target);
        else
            m_Selection.Select(click.Target);
    }

    void SceneHierarchyPanel::ApplyPendingDrop()
    {
        PendingDrop drop = m_PendingDrop;
        m_PendingDrop = {};
        Entity dragged = (uint64_t)drop.Dragged ? m_Context->GetEntityByUUID(drop.Dragged) : Entity();
        if (!dragged || drop.Zone == DropZone::None)
            return;

        // Dragging a selected row moves the whole selection (its top-level entities, in order).
        std::vector<Entity> moving = IsSelected(dragged) ? GetSelectedRoots() : std::vector<Entity>{ dragged };
        Entity target = drop.Target;
        // An entity of a prefab instance stays in its instance.
        {
            const Entity newParent = drop.Zone == DropZone::Into || drop.Zone == DropZone::FirstChild ? target
                                   : drop.Zone == DropZone::RootEnd || !target ? Entity() : m_Context->GetParent(target);
            const size_t before = moving.size();
            std::erase_if(moving, [&](Entity e) { return !CanReparent(e, newParent); });
            if (moving.size() != before)
                ZM_WARN("Prefab ichidagi obyektni tashqariga chiqarib bo'lmaydi (avval bog'lanishni uzing)");
            if (moving.empty())
                return;
        }
        bool changed = false;
        switch (drop.Zone)
        {
            case DropZone::Into:
                for (Entity e : moving)
                    if (e != target && m_Context->SetParent(e, target, true))
                        changed = true;
                break;
            case DropZone::FirstChild:
            {
                int index = 0;
                for (Entity e : moving)
                    if (e != target && m_Context->SetParent(e, target, true))
                    {
                        m_Context->SetSiblingIndex(e, index++);
                        changed = true;
                    }
                break;
            }
            case DropZone::Before:
                for (Entity e : moving)
                    if (e != target && m_Context->MoveEntityNextTo(e, target, false))
                        changed = true;
                break;
            case DropZone::After:
            {
                Entity anchor = target;
                for (Entity e : moving)
                    if (e != target && m_Context->MoveEntityNextTo(e, anchor, true))
                    {
                        anchor = e;
                        changed = true;
                    }
                break;
            }
            case DropZone::RootEnd:
                for (Entity e : moving)
                {
                    if (m_Context->GetParent(e))
                        m_Context->SetParent(e, {}, true);
                    m_Context->SetSiblingIndex(e, -1);
                    changed = true;
                }
                break;
            case DropZone::None:
                break;
        }
        if (changed)
            MarkModified();
        else if (moving.size() == 1 && (target == dragged || (target && m_Context->IsAncestorOf(dragged, target))))
            ZM_WARN("Obyektni o'zining ichiga ko'chirib bo'lmaydi: {}", dragged.GetName());
    }

    // ------------------------------------------------------------------ play mode: several scenes
    void SceneHierarchyPanel::DrawRuntimeSceneHeader()
    {
        if (!m_SceneManager || m_SceneManager->GetAllScenes().size() <= 1)
            return;
        const bool persistent = m_SceneManager->IsDontDestroyOnLoadScene(m_Context.get());
        const bool active = m_SceneManager->GetActiveScene() == m_Context;
        ImGui::SameLine();
        if (persistent)
        {
            ImGui::TextColored(ImVec4(0.96f, 0.72f, 0.32f, 1.0f), ICON_FA_THUMBTACK);
            if (ImGui::IsItemHovered())
                UI::Tooltip("DontDestroyOnLoad: bu obyektlar sahna almashganda saqlanadi");
        }
        else if (active)
        {
            ImGui::TextColored(UI::AccentColor(), ICON_FA_STAR);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Faol sahna: yangi obyektlar shu yerda yaratiladi, kamerasi o'yinni ko'rsatadi");
        }
        else
        {
            ImGui::TextDisabled(ICON_FA_LAYER_GROUP);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Qo'shimcha (additive) yuklangan sahna");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu sahna", m_SceneManager->GetAllScenes().size());
        if (ImGui::IsItemHovered())
            UI::Tooltip("O'yinda bir nechta sahna yuklangan: boshqalari daraxt ostida (ko'rish uchun " ICON_FA_EYE " tugmasi)");
    }

    void SceneHierarchyPanel::DrawOtherRuntimeScenes()
    {
        if (!m_SceneManager)
            return;
        std::vector<Ref<Scene>> scenes = m_SceneManager->GetAllScenes();
        if (scenes.size() <= 1)
            return;
        ImGui::Spacing();
        for (const Ref<Scene>& scene : scenes)
        {
            if (scene == m_Context)
                continue;
            ImGui::PushID(scene.get());
            const bool persistent = m_SceneManager->IsDontDestroyOnLoadScene(scene.get());
            const bool active = m_SceneManager->GetActiveScene() == scene;
            const size_t count = scene->GetRegistry().view<IDComponent>().size();
            std::string label = std::format("{}  {}{}  ({})###scene", persistent ? ICON_FA_THUMBTACK : ICON_FA_LAYER_GROUP, scene->GetName(),
                                            active ? "  " ICON_FA_STAR : "", count);
            ImGui::PushStyleColor(ImGuiCol_Header, persistent ? ImVec4(0.32f, 0.24f, 0.10f, 0.85f) : ImVec4(0.16f, 0.20f, 0.26f, 0.85f));
            const bool open = ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_AllowOverlap | (persistent ? ImGuiTreeNodeFlags_DefaultOpen : 0));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                UI::Tooltip(persistent ? "DontDestroyOnLoad sahnasi: sahna almashganda ham saqlanadigan obyektlar.\nIkki marta bosing: ierarxiyada ko'rish"
                                       : "Qo'shimcha yuklangan sahna. Ikki marta bosing: ierarxiyada ko'rish");
            bool show = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
            if (UI::ToolbarButton(ICON_FA_EYE, "Ierarxiyada ko'rish (inspektor bilan)", false, ImVec2(ImGui::GetFrameHeight(), 0)))
                show = true;
            if (open)
            {
                // Read-only: the scene's entities as a plain tree (this panel edits the shown scene only).
                constexpr size_t MaxRows = 200;
                size_t rows = 0;
                ImGui::Indent(ImGui::GetFrameHeight() * 0.6f);
                for (Entity e : scene->GetEntitiesInHierarchyOrder())
                {
                    if (rows++ >= MaxRows)
                    {
                        ImGui::TextDisabled("... (yana %zu ta)", count - MaxRows);
                        break;
                    }
                    int depth = 0;
                    for (Entity p = scene->GetParent(e); p && depth < 32; p = scene->GetParent(p))
                        depth++;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (float)depth * 14.0f);
                    const bool on = scene->IsActiveInHierarchy(e);
                    ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.78f, 0.81f, 0.86f, 1.0f) : ImVec4(0.48f, 0.50f, 0.54f, 1.0f));
                    ImGui::TextUnformatted(std::format("{}  {}", e.HasComponent<NativeScriptComponent>() ? ICON_FA_CODE : ICON_FA_CUBE, e.GetName()).c_str());
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        show = true;
                }
                if (count == 0)
                    ImGui::TextDisabled("(bo'sh)");
                ImGui::Unindent(ImGui::GetFrameHeight() * 0.6f);
            }
            if (show)
                m_PendingRuntimeScene = scene; // once the hierarchy is drawn
            ImGui::PopID();
        }
    }

    void SceneHierarchyPanel::DrawHierarchy()
    {
        m_HierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!m_Context)
            return;
        m_Selection.Prune();

        // Header: scene name (prefab mode: the prefab's) + add button
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        if (m_PrefabStageMode)
        {
            ImGui::TextColored(ImVec4(0.365f, 0.663f, 1.0f, 1.0f), ICON_FA_BOX "  %s.zprefab", m_Context->GetName().c_str());
            ImGui::PopFont();
            if (ImGui::IsItemHovered())
                UI::Tooltip(Tr("Prefab rejimi: prefab alohida tahrirlanmoqda (Ctrl+S - saqlash, viewport ustidagi yo'l - sahnaga qaytish)"));
        }
        else
        {
            ImGui::TextUnformatted(ICON_FA_EARTH_ASIA);
            ImGui::SameLine();
            ImGui::TextUnformatted(m_Context->GetName().c_str());
            ImGui::PopFont();
        }
        DrawRuntimeSceneHeader();
        if (m_Selection.Size() > 1)
        {
            ImGui::SameLine();
            ImGui::TextColored(UI::AccentColor(), ICON_FA_OBJECT_GROUP " %zu", m_Selection.Size());
            if (ImGui::IsItemHovered())
                UI::Tooltip("Tanlangan obyektlar soni (Ctrl+bosish: qo'shish/olib tashlash, Shift+bosish: oraliq)");
        }
        // Hidden / locked counters: a click shows / unlocks everything again.
        if (size_t hiddenCount = m_Context->CountHiddenInEditor())
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            std::string text = std::format(ICON_FA_EYE_SLASH " {}##showAll", hiddenCount);
            if (ImGui::SmallButton(text.c_str()) && m_SceneEditable)
                ShowAll();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                UI::Tooltip(std::format("{} ta obyekt editorda yashirilgan. Bosing: hammasini ko'rsatish (Alt+H)", hiddenCount).c_str());
        }
        if (size_t lockedCount = m_Context->CountLockedInEditor())
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.72f, 0.32f, 1.0f));
            std::string text = std::format(ICON_FA_LOCK " {}##unlockAll", lockedCount);
            if (ImGui::SmallButton(text.c_str()) && m_SceneEditable)
                UnlockAll();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
                UI::Tooltip(std::format("{} ta obyekt qulflangan. Bosing: hamma qulflarni ochish", lockedCount).c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
        if (UI::ToolbarButton(ICON_FA_PLUS, "Obyekt yaratish", false, ImVec2(ImGui::GetFrameHeight(), 0)))
            ImGui::OpenPopup("CreateEntityPopup");
        if (ImGui::BeginPopup("CreateEntityPopup"))
        {
            DrawCreateMenu();
            ImGui::EndPopup();
        }

        UI::SearchBox("HierarchySearch", m_Filter, "Qidirish...  (#teg)");
        ImGui::Spacing();

        std::string filter = StringUtils::ToLower(m_Filter);
        m_PendingDelete = false;
        m_PendingDuplicate = false;
        m_RowIndex = 0;
        m_VisibleRows.clear();

        ImGui::BeginChild("##entities", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
        // Eye / lock column on the left of every row (the tree starts after it).
        m_GutterX = ImGui::GetCursorScreenPos().x;
        const float gutterWidth = ImGui::GetFrameHeight() * 2.0f + 2.0f;
        ImGui::Indent(gutterWidth);
        {
            // The rows that show, as a flat list; only those on screen are drawn (ImGuiListClipper):
            // a scene of 10 000 entities costs what the visible ones do. A sub-scene host shows its
            // sub-scene's rows under it (another height): then every row is drawn.
            const auto start = std::chrono::steady_clock::now();
            BuildHierarchyRows(filter);
            m_VisibleRows.clear();
            m_VisibleRows.reserve(m_Rows.size());
            const char* noClipper = std::getenv("ZAMIN_EDITOR_NO_CLIPPER");
            bool uniform = !(noClipper && *noClipper && std::string_view(noClipper) != "0");
            int keep = -1, dragged = -1; // the row being renamed / dragged is drawn even off screen
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            const uint64_t draggedID = payload && payload->IsDataType("HIERARCHY_ENTITY") ? *(const uint64_t*)payload->Data : 0;
            const bool subScenes = GetSubScene && filter.empty() && !m_Context->GetRegistry().view<SubSceneComponent>().empty();
            for (size_t i = 0; i < m_Rows.size(); i++)
            {
                const Entity e = m_Rows[i].E;
                m_VisibleRows.push_back(e);
                if (uniform && subScenes && e.HasComponent<SubSceneComponent>())
                    uniform = false;
                if ((entt::entity)e == m_RenamingEntity)
                    keep = (int)i;
                if (draggedID && (uint64_t)e.GetUUID() == draggedID)
                    dragged = (int)i;
            }
            size_t drawn = 0;
            auto drawRow = [&](size_t i) {
                const HierarchyRow row = m_Rows[i];
                if (!row.E.IsValid())
                    return; // destroyed by an earlier row's menu this frame
                if (uniform)
                    m_RowIndex = (int)i; // (the stripes: every other row of the whole list)
                DrawEntityNode(row.E, row.Depth, row.HasChildren);
                drawn++;
            };
            if (uniform)
            {
                ImGuiListClipper clipper;
                clipper.Begin((int)m_Rows.size(), ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
                if (keep >= 0)
                    clipper.IncludeItemByIndex(keep);
                if (dragged >= 0)
                    clipper.IncludeItemByIndex(dragged);
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                        drawRow((size_t)i);
            }
            else
                for (size_t i = 0; i < m_Rows.size(); i++)
                    drawRow(i); // (the stripes count the sub-scene rows too)
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            m_ListTiming.Rows = m_Rows.size();
            m_ListTiming.DrawnRows = drawn;
            m_ListTiming.TotalMilliseconds += ms;
            m_ListTiming.MaxMilliseconds = std::max(m_ListTiming.MaxMilliseconds, ms);
            m_ListTiming.Frames++;
        }
        ImGui::Unindent(gutterWidth);
        ImGui::PopStyleVar(2);
        DrawOtherRuntimeScenes();

        // Empty area: drop to un-parent (to the end of the root list) / instantiate prefabs;
        // click to deselect
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##hierarchyEmpty", ImVec2(std::max(1.0f, avail.x), std::max(40.0f, avail.y)));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
            m_Selection.Clear();
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
                m_PendingDrop = { *(const uint64_t*)payload->Data, {}, DropZone::RootEnd };
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::filesystem::path path((const char*)payload->Data);
                if (path.extension() == ".zprefab" && OnPrefabDropped)
                    OnPrefabDropped(path, {});
                else if (path.extension() == ParticlePresets::Extension && m_SceneEditable)
                    RequestParticlePresetFile({}, path); // a new entity with the effect
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("HierarchyContext"))
        {
            DrawCreateMenu();
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_EYE "  Hammasini ko'rsatish"), "Alt+H", false, m_SceneEditable && m_Context->CountHiddenInEditor() > 0))
                ShowAll();
            if (ImGui::MenuItem(Tr(ICON_FA_LOCK_OPEN "  Hamma qulflarni ochish"), nullptr, false, m_SceneEditable && m_Context->CountLockedInEditor() > 0))
                UnlockAll();
            ImGui::EndPopup();
        }

        if (Entity primary = GetSelectedEntity(); primary && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2))
        {
            m_RenamingEntity = primary;
            m_RenameBuffer = primary.GetName();
        }
        ImGui::EndChild();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_ClickedSelected = {};

        // Deferred until every row is drawn: ranges need all visible rows, and the hierarchy
        // must not change while it is being drawn.
        ApplyPendingClick();
        ApplyPendingDrop();
        if (m_PendingDuplicate)
            DuplicateSelection();
        if (m_PendingDelete)
            DeleteSelection();
    }

    // Name search, or "#tag" to list entities by gameplay tag ("#" alone: any tagged entity).
    bool SceneHierarchyPanel::MatchesFilter(Entity entity, const std::string& lowerFilter)
    {
        if (!lowerFilter.empty() && lowerFilter[0] == '#')
        {
            const auto* tags = entity.TryGetComponent<TagsComponent>();
            if (!tags) return false;
            std::string wanted = lowerFilter.substr(1);
            for (const auto& tag : tags->Tags)
                if (StringUtils::ToLower(tag).find(wanted) != std::string::npos)
                    return true;
            return false;
        }
        return StringUtils::ToLower(entity.GetName()).find(lowerFilter) != std::string::npos;
    }

    const char* SceneHierarchyPanel::GetEntityIcon(Entity entity) { return EntityIcon(entity); }

}
