#include "SceneHierarchyPanel.h"
#include "AssetInspectorPanel.h"
#include "../UI.h"

#include <Zamin/Utils/Image.h>

#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <set>

namespace Zamin {

    static std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    static const char* EntityIcon(Entity e)
    {
        if (e.HasComponent<UIButtonComponent>()) return ICON_FA_HAND_POINTER;
        if (e.HasComponent<UITransformComponent>()) return ICON_FA_TABLE_CELLS;
        if (e.HasComponent<ParticleSystemComponent>()) return ICON_FA_WAND_MAGIC_SPARKLES;
        if (e.HasComponent<CameraComponent>()) return ICON_FA_CAMERA;
        if (e.HasComponent<ParallaxComponent>()) return ICON_FA_MOUNTAIN;
        if (e.HasComponent<DirectionalLightComponent>()) return ICON_FA_SUN;
        if (e.HasComponent<PointLightComponent>()) return ICON_FA_LIGHTBULB;
        if (e.HasComponent<SpotLightComponent>()) return ICON_FA_BULLHORN;
        if (e.HasComponent<Light2DComponent>()) return ICON_FA_FIRE;
        if (e.HasComponent<MeshRendererComponent>()) return ICON_FA_CUBE;
        if (e.HasComponent<TextComponent>()) return ICON_FA_FONT;
        if (e.HasComponent<CircleRendererComponent>()) return ICON_FA_CIRCLE;
        if (e.HasComponent<SpriteRendererComponent>()) return ICON_FA_IMAGE;
        if (e.HasComponent<AudioSourceComponent>()) return ICON_FA_VOLUME_HIGH;
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
    static void InactiveComponentNote(Entity entity, const char* text)
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
            if (ImGui::Begin(ICON_FA_LIST "  Ierarxiya###Hierarchy", showHierarchy))
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
            if (ImGui::Begin(ICON_FA_SLIDERS "  Inspektor###Inspector", showInspector))
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
        else if (p == "Fire")
        {
            e = m_Context->CreateEntity("Olov");
            auto& ps = e.AddComponent<ParticleSystemComponent>();
            ps.EmissionRate = 60.0f; ps.Spread = 18.0f; ps.SpeedMin = 1.0f; ps.SpeedMax = 2.2f;
            ps.Shape = ParticleSystemComponent::EmitterShape::Circle; ps.ShapeRadius = 0.25f;
            ps.StartSize = 0.45f; ps.EndSize = 0.05f; ps.Gravity = { 0.0f, 1.0f, 0.0f };
        }
        else if (p == "Sparks")
        {
            e = m_Context->CreateEntity("Uchqunlar");
            auto& ps = e.AddComponent<ParticleSystemComponent>();
            ps.EmissionRate = 25.0f; ps.Spread = 60.0f; ps.SpeedMin = 3.0f; ps.SpeedMax = 6.0f;
            ps.Gravity = { 0.0f, -9.8f, 0.0f }; ps.StartSize = 0.12f; ps.EndSize = 0.02f;
            ps.StartColor = { 1.0f, 0.95f, 0.6f, 1.0f }; ps.EndColor = { 1.0f, 0.4f, 0.1f, 0.0f };
            ps.LifetimeMin = 0.4f; ps.LifetimeMax = 1.0f;
        }
        else if (p == "Smoke")
        {
            e = m_Context->CreateEntity("Tutun");
            auto& ps = e.AddComponent<ParticleSystemComponent>();
            ps.EmissionRate = 12.0f; ps.Spread = 15.0f; ps.SpeedMin = 0.5f; ps.SpeedMax = 1.2f;
            ps.StartSize = 0.4f; ps.EndSize = 1.6f; ps.LifetimeMin = 2.0f; ps.LifetimeMax = 3.5f;
            ps.StartColor = { 0.6f, 0.6f, 0.62f, 0.5f }; ps.EndColor = { 0.4f, 0.4f, 0.42f, 0.0f };
            ps.Additive = false; ps.AngularVelocityMax = 1.0f;
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
            ui.Anchor = UITransformComponent::AnchorPoint::Center; ui.Position = { 0, 0 }; ui.Size = { 128, 128 };
            e.AddComponent<SpriteRendererComponent>();
        }
        else if (p == "UI Button")
        {
            e = m_Context->CreateEntity("UI Tugma");
            auto& ui = e.AddComponent<UITransformComponent>();
            ui.Anchor = UITransformComponent::AnchorPoint::Center; ui.Position = { 0, 0 }; ui.Size = { 220, 56 };
            e.AddComponent<UIButtonComponent>();
            auto& t = e.AddComponent<TextComponent>();
            t.Text = "Boshlash"; t.Centered = true;
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
        else if (p == "Audio") { e = m_Context->CreateEntity("Ovoz manbai"); e.AddComponent<AudioSourceComponent>(); }

        if (e)
        {
            SetSelectedEntity(e);
            MarkModified();
        }
        return e;
    }

    void SceneHierarchyPanel::DrawCreateMenu()
    {
        if (ImGui::MenuItem(ICON_FA_BOX "  Bo'sh obyekt")) CreateEntityPreset("Empty");
        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_FA_SHAPES "  2D"))
        {
            if (ImGui::MenuItem(ICON_FA_IMAGE "  Sprite")) CreateEntityPreset("Sprite");
            if (ImGui::MenuItem(ICON_FA_CIRCLE "  Doira")) CreateEntityPreset("Circle");
            if (ImGui::MenuItem(ICON_FA_FONT "  Matn")) CreateEntityPreset("Text");
            if (ImGui::MenuItem(ICON_FA_TABLE_CELLS "  Tilemap")) CreateEntityPreset("Tilemap");
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_BOX "  Fizik quti")) CreateEntityPreset("Physics Box");
            if (ImGui::MenuItem(ICON_FA_CIRCLE "  Fizik shar")) CreateEntityPreset("Physics Ball");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_FA_CUBE "  3D"))
        {
            if (ImGui::MenuItem(ICON_FA_CUBE "  Kub")) CreateEntityPreset("Cube");
            if (ImGui::MenuItem(ICON_FA_CIRCLE "  Shar")) CreateEntityPreset("Sphere");
            if (ImGui::MenuItem(ICON_FA_SQUARE "  Tekislik")) CreateEntityPreset("Plane");
            if (ImGui::MenuItem(ICON_FA_DRAW_POLYGON "  Silindr")) CreateEntityPreset("Cylinder");
            if (ImGui::MenuItem(ICON_FA_MOUNTAIN_SUN "  Konus")) CreateEntityPreset("Cone");
            if (ImGui::MenuItem(ICON_FA_CUBES "  Model (glTF / OBJ)")) CreateEntityPreset("Model");
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_ATOM "  Fizik kub")) CreateEntityPreset("Physics Cube");
            if (ImGui::MenuItem(ICON_FA_ATOM "  Fizik shar")) CreateEntityPreset("Physics Sphere");
            if (ImGui::MenuItem(ICON_FA_ATOM "  Fizik yer (statik)")) CreateEntityPreset("Physics Ground");
            if (ImGui::MenuItem(ICON_FA_PERSON_RUNNING "  Qahramon (Character Controller)")) CreateEntityPreset("Character3D");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_FA_LIGHTBULB "  Yorug'lik"))
        {
            if (ImGui::MenuItem(ICON_FA_SUN "  Quyosh (yo'naltirilgan)")) CreateEntityPreset("DirectionalLight");
            if (ImGui::MenuItem(ICON_FA_LIGHTBULB "  Nuqtali")) CreateEntityPreset("PointLight");
            if (ImGui::MenuItem(ICON_FA_BULLHORN "  Spot (konus)")) CreateEntityPreset("SpotLight");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_FA_CAMERA "  Kamera"))
        {
            if (ImGui::MenuItem(ICON_FA_CAMERA "  Perspektiv (3D)")) CreateEntityPreset("Camera");
            if (ImGui::MenuItem(ICON_FA_CAMERA "  Ortografik (2D)")) CreateEntityPreset("Camera2D");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_FA_WAND_MAGIC_SPARKLES "  Effektlar"))
        {
            if (ImGui::MenuItem(ICON_FA_FIRE "  Olov")) CreateEntityPreset("Fire");
            if (ImGui::MenuItem(ICON_FA_BOLT "  Uchqunlar")) CreateEntityPreset("Sparks");
            if (ImGui::MenuItem(ICON_FA_CLOUD "  Tutun")) CreateEntityPreset("Smoke");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_FA_TABLE_CELLS "  UI (ekran)"))
        {
            if (ImGui::MenuItem(ICON_FA_FONT "  Matn")) CreateEntityPreset("UI Text");
            if (ImGui::MenuItem(ICON_FA_IMAGE "  Rasm")) CreateEntityPreset("UI Image");
            if (ImGui::MenuItem(ICON_FA_HAND_POINTER "  Tugma")) CreateEntityPreset("UI Button");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_VOLUME_HIGH "  Ovoz manbai")) CreateEntityPreset("Audio");
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

        if (ImGui::BeginMenu(ICON_FA_PLUS "  Bola obyekt yaratish"))
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
        if (ImGui::MenuItem(ICON_FA_FEATHER "  Nomini o'zgartirish", "F2"))
        {
            SetSelectedEntity(entity);
            m_RenamingEntity = entity;
            m_RenameBuffer = entity.GetName();
        }
        std::string duplicateLabel = count > 1 ? std::format(ICON_FA_CLONE "  Nusxa olish ({} ta)", count) : std::string(ICON_FA_CLONE "  Nusxa olish");
        if (ImGui::MenuItem(duplicateLabel.c_str(), "Ctrl+D")) m_PendingDuplicate = true;
        if (ImGui::MenuItem(ICON_FA_BULLSEYE "  Fokus", "F")) { if (OnFocusRequested) OnFocusRequested(entity); }
        std::vector<Entity> parented;
        for (Entity e : GetSelectedRoots())
            if (m_Context->GetParent(e))
                parented.push_back(e);
        if (!parented.empty() && ImGui::MenuItem(ICON_FA_ARROW_UP "  Ota-onadan ajratish"))
        {
            for (Entity e : parented)
                if (m_Context->SetParent(e, {}, true))
                    m_Context->SetSiblingIndex(e, -1);
            MarkModified();
        }
        if (ImGui::MenuItem(ICON_FA_BOX "  Prefab sifatida saqlash") && OnSavePrefabRequested)
            OnSavePrefabRequested(entity);
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
        if (ImGui::MenuItem(ICON_FA_CHECK_DOUBLE "  Hammasini tanlash", "Ctrl+A")) SelectAll();
        std::string deleteLabel = count > 1 ? std::format(ICON_FA_TRASH "  O'chirish ({} ta)", count) : std::string(ICON_FA_TRASH "  O'chirish");
        if (ImGui::MenuItem(deleteLabel.c_str(), "Delete")) m_PendingDelete = true;
        ImGui::EndPopup();
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity, int depth)
    {
        entt::entity handle = entity;
        const std::string& tag = entity.GetName(); // not used after the context menu (see below)
        bool hasChildren = !entity.GetComponent<RelationshipComponent>().Children.empty() && m_Filter.empty();
        bool selected = IsSelected(entity);
        bool primary = selected && GetSelectedEntity() == entity;
        bool hiddenInView = m_Context->IsHiddenInEditor(entity);
        // Not active in the hierarchy (its own flag, or an ancestor's): the whole row is faded.
        bool inactiveSelf = !entity.IsActive();
        bool inactive = inactiveSelf || !m_Context->IsActiveInHierarchy(entity);

        ImGui::PushID((int)(uint32_t)handle);
        m_VisibleRows.push_back(entity);

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
            ImGui::PopID();
            return;
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;

        // The primary entity is highlighted stronger than the rest of a multi-selection.
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, primary ? ImVec4(0.12f, 0.52f, 0.41f, 0.75f) : ImVec4(0.12f, 0.52f, 0.41f, 0.38f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, primary ? ImVec4(0.14f, 0.58f, 0.46f, 0.85f) : ImVec4(0.14f, 0.58f, 0.46f, 0.55f));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        // Hidden in the scene view: a grey label; inactive (game state): a translucent one.
        const bool dimmed = hiddenInView || inactive;
        if (dimmed)
        {
            ImVec4 color = ImGui::GetStyleColorVec4(hiddenInView ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            if (inactive)
                color.w *= 0.42f;
            ImGui::PushStyleColor(ImGuiCol_Text, color);
        }
        std::string label = std::string(EntityIcon(entity)) + "  " + tag;
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
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !hasChildren && OnFocusRequested)
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
            DrawRowToggles(entity, rowMin, rowMax);

        if (open)
        {
            if (hasChildren && alive)
            {
                // A copy: may be modified by drag & drop.
                std::vector<UUID> copy = entity.GetComponent<RelationshipComponent>().Children;
                for (UUID child : copy)
                    if (Entity c = m_Context->GetEntityByUUID(child))
                        DrawEntityNode(c, depth + 1);
            }
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

    void SceneHierarchyPanel::DrawHierarchy()
    {
        m_HierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!m_Context)
            return;
        m_Selection.Prune();

        // Header: scene name + add button
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(ICON_FA_EARTH_ASIA);
        ImGui::SameLine();
        ImGui::TextUnformatted(m_Context->GetName().c_str());
        ImGui::PopFont();
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

        std::string filter = ToLower(m_Filter);
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
        for (Entity entity : m_Context->GetEntitiesInOrder())
        {
            if (filter.empty())
            {
                if ((uint64_t)entity.GetComponent<RelationshipComponent>().Parent && m_Context->GetParent(entity))
                    continue; // drawn by its parent
            }
            else if (!MatchesFilter(entity, filter))
                continue;
            DrawEntityNode(entity, 0);
        }
        ImGui::Unindent(gutterWidth);
        ImGui::PopStyleVar(2);

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
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("HierarchyContext"))
        {
            DrawCreateMenu();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_EYE "  Hammasini ko'rsatish", "Alt+H", false, m_SceneEditable && m_Context->CountHiddenInEditor() > 0))
                ShowAll();
            if (ImGui::MenuItem(ICON_FA_LOCK_OPEN "  Hamma qulflarni ochish", nullptr, false, m_SceneEditable && m_Context->CountLockedInEditor() > 0))
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
                if (ToLower(tag).find(wanted) != std::string::npos)
                    return true;
            return false;
        }
        return ToLower(entity.GetName()).find(lowerFilter) != std::string::npos;
    }

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
            { "Tilemap", "Tilemap" }, { "Parallax", "Parallax" }, { "Camera2DFollow", "Camera 2D Follow" }, { "ParticleSystem", "Particle System" }, { "UITransform", "UI Transform" },
            { "UIButton", "UI Button" }, { "AudioSource", "Audio Source" }, { "NativeScript", "C++ Skript" },
        };
        for (const auto& [k, name] : s_Names)
            if (k == key)
                return name;
        return "Komponent";
    }

    const char* SceneHierarchyPanel::GetEntityIcon(Entity entity) { return EntityIcon(entity); }
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
        if (ImGui::MenuItem(ICON_FA_COPY "  Komponentni nusxalash"))
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
        if (ImGui::MenuItem(ICON_FA_ARROW_UP "  Yuqoriga ko'chirish", nullptr, false, canMove && index > first))
            m_PendingMove = { std::string(key), {}, false, index - 1 };
        if (ImGui::MenuItem(ICON_FA_ARROW_DOWN "  Pastga ko'chirish", nullptr, false, canMove && index < last))
            m_PendingMove = { std::string(key), {}, false, index + 1 };
        if (ImGui::MenuItem(ICON_FA_ANGLES_UP "  Eng yuqoriga", nullptr, false, canMove && index > first))
            m_PendingMove = { std::string(key), {}, false, first };
        if (ImGui::MenuItem(ICON_FA_ANGLES_DOWN "  Eng pastga", nullptr, false, canMove && index < last))
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
            if (removable)
            {
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_TRASH "  Komponentni olib tashlash"))
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
        std::string ext = std::filesystem::path(c.ModelPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if (!Project::GetActive() || (ext != ".gltf" && ext != ".glb"))
            return false;
        UI::PropertyLabel("");
        bool clicked = ImGui::Button(ICON_FA_FILE_EXPORT "  Modeldan .zmat yaratib biriktirish", ImVec2(-FLT_MIN, 0));
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
        if (ImGui::Button(ICON_FA_SWATCHBOOK "  Standart material...", ImVec2(-FLT_MIN, 0)) || m_OpenStandardMaterials)
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
            bool open = ImGui::TreeNodeEx("Materiallar###submeshMaterials", ImGuiTreeNodeFlags_SpanAllColumns);
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
            bool save = ImGui::Button(ICON_FA_FLOPPY_DISK "  Asset qilib saqlash", ImVec2(-FLT_MIN, 0));
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
        if (!m_AddComponentFilter.empty() && ToLower(name).find(ToLower(m_AddComponentFilter)) == std::string::npos)
            return;
        std::string label = std::string(icon) + "   " + name;
        if (ImGui::MenuItem(label.c_str()))
        {
            T& c = entity.AddComponent<T>();
            if constexpr (std::is_same_v<T, MeshRendererComponent>)
                c.MeshAsset = MeshLibrary::GetPrimitive(c.Type);
            if constexpr (std::is_same_v<T, TextComponent>)
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

    bool SceneHierarchyPanel::DrawEntityRefField(const char* name, EntityRef& ref, const char* tooltip)
    {
        UI::PropertyLabel(name, tooltip);
        ImGui::PushID(name);
        bool changed = false;

        Entity target = ref.ID && m_Context ? m_Context->GetEntityByUUID(ref.ID) : Entity();
        bool missing = ref.ID && !target;

        float buttonWidth = ImGui::GetFrameHeight();
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float width = std::max(20.0f, ImGui::GetContentRegionAvail().x - (buttonWidth + spacing) * 2.0f);
        std::string display;
        if (!ref.ID) display = ICON_FA_LINK "  (yo'q)";
        else if (missing) display = ICON_FA_TRIANGLE_EXCLAMATION "  (topilmadi)";
        else display = std::string(EntityIcon(target)) + "  " + target.GetName();

        ImVec4 textColor = missing ? ImVec4(0.95f, 0.45f, 0.35f, 1.0f)
                         : !ref.ID ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                         : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        if (ImGui::Button(display.c_str(), ImVec2(width, 0)))
        {
            m_EntityPickerFilter.clear();
            ImGui::OpenPopup("##entityPicker");
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            UI::Tooltip(missing ? "Bog'langan obyekt sahnada yo'q. Boshqasini tanlang yoki tozalang"
                                : "Ierarxiyadan obyektni tortib tashlang yoki bosib tanlang");

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
            {
                uint64_t id = *(const uint64_t*)payload->Data;
                if (id != ref.ID)
                {
                    ref.ID = id;
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopup("##entityPicker"))
        {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS "  Qidirish...", &m_EntityPickerFilter);
            ImGui::Separator();
            std::string filter = ToLower(m_EntityPickerFilter);
            ImGui::BeginChild("##list", ImVec2(ImGui::GetFontSize() * 16.0f, ImGui::GetFontSize() * 14.0f), ImGuiChildFlags_None);
            if (ImGui::Selectable(ICON_FA_XMARK "  (yo'q)", ref.ID == 0))
            {
                if (ref.ID) changed = true;
                ref.ID = 0;
                ImGui::CloseCurrentPopup();
            }
            if (m_Context)
            {
                for (Entity e : m_Context->GetEntitiesInOrder())
                {
                    const std::string& entityName = e.GetName();
                    if (!filter.empty() && ToLower(entityName).find(filter) == std::string::npos)
                        continue;
                    ImGui::PushID((int)(uint32_t)e);
                    std::string label = std::string(EntityIcon(e)) + "  " + entityName;
                    if (ImGui::Selectable(label.c_str(), (uint64_t)e.GetUUID() == ref.ID))
                    {
                        if ((uint64_t)e.GetUUID() != ref.ID) changed = true;
                        ref.ID = e.GetUUID();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        // Locate: select the referenced entity in the hierarchy.
        ImGui::SameLine();
        ImGui::BeginDisabled(!target);
        if (ImGui::Button(ICON_FA_CROSSHAIRS, ImVec2(buttonWidth, 0)))
            SetSelectedEntity(target);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && target)
            UI::Tooltip("Ierarxiyada ko'rsatish");

        // Clear
        ImGui::SameLine();
        ImGui::BeginDisabled(!ref.ID);
        if (ImGui::Button(ICON_FA_XMARK, ImVec2(buttonWidth, 0)))
        {
            ref.ID = 0;
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ref.ID)
            UI::Tooltip("Tozalash");

        ImGui::PopID();
        return changed;
    }

    static std::string TrimTag(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    bool SceneHierarchyPanel::PeekTransformClipboard(TransformComponent& out)
    {
        // The system clipboard first (copied in another editor window), then our own copy.
        const char* text = ImGui::GetClipboardText();
        if (text && SceneSerializer::DeserializeTransform(text, out))
            return true;
        if (m_TransformClipboard)
        {
            out = *m_TransformClipboard;
            return true;
        }
        return false;
    }

    bool SceneHierarchyPanel::DrawTransformMenu(Entity entity)
    {
        // Reset / paste act on every selected entity (the inspector shows the primary one).
        std::vector<Entity> targets;
        if (IsSelected(entity))
            for (Entity e : m_Selection.GetEntities())
                if (e) targets.push_back(e);
        if (targets.empty())
            targets.push_back(entity);
        if (ImGui::IsWindowAppearing()) // read the clipboard once per opening
            m_CanPasteTransform = PeekTransformClipboard(m_PasteTransform);

        bool changed = false;
        auto apply = [&](auto&& edit) {
            for (Entity e : targets)
                edit(e.GetComponent<TransformComponent>());
            changed = true;
        };

        if (targets.size() > 1)
        {
            ImGui::TextDisabled(ICON_FA_OBJECT_GROUP "  %zu ta obyektga qo'llanadi", targets.size());
            ImGui::Separator();
        }
        if (ImGui::MenuItem(ICON_FA_ARROW_ROTATE_LEFT "  Tiklash (hammasi)"))
            apply([](TransformComponent& t) { t = TransformComponent(); });
        if (ImGui::MenuItem("      Pozitsiyani tiklash"))
            apply([](TransformComponent& t) { t.Translation = glm::vec3(0.0f); });
        if (ImGui::MenuItem("      Aylanishni tiklash"))
            apply([](TransformComponent& t) { t.Rotation = glm::vec3(0.0f); });
        if (ImGui::MenuItem("      Masshtabni tiklash"))
            apply([](TransformComponent& t) { t.Scale = glm::vec3(1.0f); });

        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_COPY "  Nusxalash"))
        {
            const TransformComponent& t = entity.GetComponent<TransformComponent>();
            m_TransformClipboard = t;
            m_PasteTransform = t;
            m_CanPasteTransform = true;
            ImGui::SetClipboardText(SceneSerializer::SerializeTransform(t).c_str());
            ZM_INFO("Transform nusxalandi: {}", entity.GetName());
        }
        const TransformComponent clip = m_PasteTransform;
        const bool canPaste = m_CanPasteTransform;
        if (ImGui::MenuItem(ICON_FA_PASTE "  Joylash (hammasi)", nullptr, false, canPaste))
            apply([&](TransformComponent& t) { t = clip; });
        if (ImGui::MenuItem("      Faqat pozitsiyani joylash", nullptr, false, canPaste))
            apply([&](TransformComponent& t) { t.Translation = clip.Translation; });
        if (ImGui::MenuItem("      Faqat aylanishni joylash", nullptr, false, canPaste))
            apply([&](TransformComponent& t) { t.Rotation = clip.Rotation; });
        if (ImGui::MenuItem("      Faqat masshtabni joylash", nullptr, false, canPaste))
            apply([&](TransformComponent& t) { t.Scale = clip.Scale; });
        if (canPaste)
        {
            glm::vec3 r = glm::degrees(clip.Rotation);
            ImGui::Separator();
            ImGui::TextDisabled("Buferda: P(%.2f, %.2f, %.2f)", clip.Translation.x, clip.Translation.y, clip.Translation.z);
            ImGui::TextDisabled("          A(%.1f, %.1f, %.1f)  M(%.2f, %.2f, %.2f)", r.x, r.y, r.z, clip.Scale.x, clip.Scale.y, clip.Scale.z);
        }
        else
        {
            ImGui::Separator();
            ImGui::TextDisabled("Bufer bo'sh: avval 'Nusxalash'ni bosing");
        }
        return changed;
    }

    // ------------------------------------------------------------------ 2D renderers
    bool SceneHierarchyPanel::DrawSortingLayer(uint32_t& layer, int& order)
    {
        bool changed = false;
        const SortingLayerList& layers = Project::GetSortingLayers();
        UI::PropertyLabel("Saralash qatlami", "Orqadan oldinga: avval qatlam (Loyiha sozlamalari > Saralash qatlamlari "
                                              "ro'yxatidagi o'rni), keyin qatlamdagi tartib, keyin kameradan uzoqligi. "
                                              "(Y) qatlamda tartibdan oldin Y: yuqoriroqdagi orqada chiziladi.");
        ImGui::PushID("sortingLayer");
        const SortingLayer* current = layers.Find(layer);
        std::string preview = current ? std::format(ICON_FA_LAYER_GROUP "  {}{}", current->Name, current->YSort ? "  (Y)" : "")
                                      : std::format(ICON_FA_TRIANGLE_EXCLAMATION "  #{} (yo'q - Default)", layer);
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::BeginCombo("##layer", preview.c_str()))
        {
            const auto& list = layers.GetLayers();
            for (size_t i = 0; i < list.size(); i++)
            {
                ImGui::PushID((int)list[i].ID);
                std::string label = std::format("{}  {}{}", i + 1, list[i].Name, list[i].YSort ? "  (Y bo'yicha)" : "");
                if (ImGui::Selectable(label.c_str(), list[i].ID == layer))
                {
                    layer = list[i].ID;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!current && ImGui::IsItemHovered())
            UI::Tooltip("Bu qatlam loyihada yo'q (o'chirilgan): Default qatlamida chiziladi");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PEN, ImVec2(button, 0)) && OnSortingLayersRequested)
            OnSortingLayersRequested();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Qatlamlarni tahrirlash (Loyiha sozlamalari)");
        ImGui::PopID();
        changed |= UI::PropertyInt("Qatlamdagi tartib", order);
        return changed;
    }

    bool SceneHierarchyPanel::DrawCollisionLayer(const char* label, int& layer, bool inherit, const char* tooltip)
    {
        bool changed = false;
        const CollisionLayers2D& layers = Project::GetCollisionLayers2D();
        UI::PropertyLabel(label, tooltip);
        ImGui::PushID(label);
        auto name = [&](int l) { return std::format("{}  {}", l, layers.GetName(l)); };
        std::string preview = layer < 0 ? std::string("(Rigidbody 2D qatlami)") : std::format(ICON_FA_LAYER_GROUP "  {}", name(layer));
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::BeginCombo("##collisionLayer", preview.c_str()))
        {
            if (inherit && ImGui::Selectable("(Rigidbody 2D qatlami)", layer < 0))
            {
                layer = -1;
                changed = true;
            }
            std::vector<int> list = layers.GetNamedLayers();
            if (layer >= 0 && std::find(list.begin(), list.end(), layer) == list.end())
                list.insert(std::upper_bound(list.begin(), list.end(), layer), layer); // unnamed, but in use
            for (int l : list)
            {
                ImGui::PushID(l);
                if (ImGui::Selectable(name(l).c_str(), l == layer))
                {
                    layer = l;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PEN, ImVec2(button, 0)) && OnPhysicsLayersRequested)
            OnPhysicsLayersRequested();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Qatlamlar va qaysilari to'qnashishini tahrirlash (Loyiha sozlamalari > Fizika qatlamlari)");
        ImGui::PopID();
        return changed;
    }

    bool SceneHierarchyPanel::DrawLayerMask(const char* label, uint16_t& mask, const char* tooltip)
    {
        bool changed = false;
        const CollisionLayers2D& layers = Project::GetCollisionLayers2D();
        const std::vector<int> named = layers.GetNamedLayers();
        uint16_t namedBits = 0;
        for (int l : named)
            namedBits |= CollisionLayers2D::GetCategoryBits(l);
        const uint16_t unnamedBits = (uint16_t)~namedBits;
        // "Hammasi", "Hech biri", or the names (the unnamed ones as one).
        std::string preview;
        if (mask == CollisionLayers2D::AllLayers)
            preview = "Hammasi";
        else if (mask == 0)
            preview = "Hech biri";
        else
        {
            for (int l : named)
                if (mask & CollisionLayers2D::GetCategoryBits(l))
                    preview += (preview.empty() ? "" : ", ") + layers.GetName(l);
            if (mask & unnamedBits)
                preview += preview.empty() ? "nomsiz qatlamlar" : " + nomsiz";
        }
        UI::PropertyLabel(label, tooltip);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##layerMask", (ICON_FA_LAYER_GROUP "  " + preview).c_str()))
        {
            if (ImGui::SmallButton("Hammasi"))
            {
                changed = mask != CollisionLayers2D::AllLayers;
                mask = CollisionLayers2D::AllLayers;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hech biri"))
            {
                changed = mask != 0;
                mask = 0;
            }
            ImGui::Separator();
            for (int l : named)
            {
                ImGui::PushID(l);
                bool on = (mask & CollisionLayers2D::GetCategoryBits(l)) != 0;
                if (ImGui::Checkbox(std::format("{}  {}", l, layers.GetName(l)).c_str(), &on))
                {
                    mask = on ? (uint16_t)(mask | CollisionLayers2D::GetCategoryBits(l)) : (uint16_t)(mask & ~CollisionLayers2D::GetCategoryBits(l));
                    changed = true;
                }
                ImGui::PopID();
            }
            if (unnamedBits)
            {
                bool on = (mask & unnamedBits) == unnamedBits;
                if (ImGui::Checkbox("Nomsiz qatlamlar", &on))
                {
                    mask = on ? (uint16_t)(mask | unnamedBits) : (uint16_t)(mask & ~unnamedBits);
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Loyiha sozlamalarida nomi berilmagan qatlamlar (hammasi birga)");
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    void SceneHierarchyPanel::DrawEffectorNotes(Entity entity, bool fluid)
    {
        // The colliders that make the effector's area: box, circle and polygon ones (edges have no inside).
        int areaColliders = 0, triggers = 0;
        auto count = [&](const auto* c) {
            if (!c)
                return;
            areaColliders++;
            triggers += c->IsSensor ? 1 : 0;
        };
        count(entity.TryGetComponent<BoxCollider2DComponent>());
        count(entity.TryGetComponent<CircleCollider2DComponent>());
        count(entity.TryGetComponent<PolygonCollider2DComponent>());
        const char* warning = nullptr;
        const char* info = nullptr;
        if (!entity.HasComponent<Rigidbody2DComponent>())
            warning = "Rigidbody 2D (odatda statik) va kollider kerak: effektor shu kolliderlar ichidagi jismlarga ta'sir qiladi";
        else if (areaColliders == 0)
            warning = "Box / Circle / Polygon Collider 2D kerak: effektor maydoni shu kolliderlar";
        else if (triggers < areaColliders)
        {
            if (fluid)
                warning = "Suv kollideri trigger bo'lishi kerak ('Trigger (sensor)' ni yoqing): aks holda jismlar ichiga kira olmaydi";
            else
                info = "Kollider trigger emas: faqat unga tegib turgan jismlar itariladi";
        }
        if (!warning && !info)
            return;
        UI::PropertyLabel("");
        ImGui::PushTextWrapPos(0.0f);
        if (warning)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", warning);
        else
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  %s", info);
        ImGui::PopTextWrapPos();
    }

    bool SceneHierarchyPanel::DrawPhysicsMaterialField(Entity entity, std::string& path, const PhysicsMaterialData& own, bool& active, bool threeD)
    {
        bool changed = false, edit = false;
        Ref<PhysicsMaterialAsset> asset = path.empty() ? nullptr : PhysicsMaterialLibrary::Load(path);
        UI::AssetPathOptions options;
        options.ActionIcon = ICON_FA_PEN;
        options.ActionTooltip = "Fizik materialni tahrirlash (inspektorda ochiladi)";
        options.ActionClicked = &edit;
        options.Missing = !path.empty() && !asset;
        options.Tooltip = "Fizik material asset (.zphysmat): ishqalanish, sakrash, zichlik va ular boshqa kollider bilan qanday "
                          "qo'shilishi. Tanlansa quyidagi o'z qiymatlari o'rniga ishlatiladi; fayl o'zgarsa o'yin davomida ham yangilanadi.";
        if (UI::PropertyAssetPath("Fizik material", path, PhysicsMaterial::Extension, ICON_FA_GEM, options))
        {
            changed = true;
            asset = path.empty() ? nullptr : PhysicsMaterialLibrary::Load(path);
        }
        if (edit && asset && OnAssetEditRequested)
            OnAssetEditRequested(Project::GetAssetFileSystemPath(path));
        active = asset != nullptr;
        if (asset)
        {
            const PhysicsMaterialData& d = asset->GetData();
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            if (threeD)
            {
                // Jolt's own rule is the same as Box2D's (friction sqrt(a * b), bounce max(a, b)).
                auto combine = [](PhysicsCombine mode) { return mode == PhysicsCombine::Default ? "Standart" : PhysicsMaterial::GetCombineName(mode); };
                ImGui::TextDisabled(ICON_FA_GEM "  Ishqalanish %.2f (%s), sakrash %.2f (%s), sakrash chegarasi %.2f m/s. Zichlik 3D'da "
                                    "ishlatilmaydi: massa - Rigidbody 3D'da",
                                    d.Friction, combine(d.FrictionCombine), d.Restitution, combine(d.RestitutionCombine), d.RestitutionThreshold);
            }
            else
                ImGui::TextDisabled(ICON_FA_GEM "  Ishqalanish %.2f (%s), sakrash %.2f (%s), zichlik %.2f", d.Friction,
                                    PhysicsMaterial::GetCombineName(d.FrictionCombine), d.Restitution,
                                    PhysicsMaterial::GetCombineName(d.RestitutionCombine), d.Density);
            ImGui::PopTextWrapPos();
        }
        else if (path.empty() && Project::GetActive())
        {
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!m_SceneEditable);
            bool create = ImGui::SmallButton(ICON_FA_PLUS "  Shu qiymatlardan material yaratish");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Quyidagi qiymatlarni Assets/Physics/ ga .zphysmat qilib saqlaydi va shu kolliderga biriktiradi - keyin "
                            "uni boshqa kolliderlar ham ishlatishi mumkin.");
            if (create)
            {
                std::filesystem::path dir = Project::GetAssetDirectory() / "Physics";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                std::string name = entity.GetName().empty() ? std::string("PhysicsMaterial") : entity.GetName();
                for (char& ch : name)
                    if (std::string_view("/\\:*?\"<>|").find(ch) != std::string_view::npos)
                        ch = '_';
                std::filesystem::path file = AssetFiles::MakeUniquePath(dir, name + PhysicsMaterial::Extension);
                if (PhysicsMaterial::Save(file, own))
                {
                    if (AssetRegistry* registry = Project::GetAssetRegistry())
                        registry->OnFileAdded(file);
                    path = Project::GetAssetRelativePath(file);
                    changed = true;
                    if (OnFilesCreated)
                        OnFilesCreated({ file });
                    ZM_INFO("Fizik material yaratildi: {}", path);
                }
                else
                    ZM_ERROR("Fizik material saqlanmadi: {}", file.string());
            }
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawColliderPoints(std::vector<glm::vec2>& points, ColliderPointKind kind, int minPoints)
    {
        bool changed = false;
        const bool editing = m_ColliderPointEdit == kind;
        UI::PropertyLabel("Viewportda", "Nuqtalarni sahnada sudrab o'zgartirish. Chiziq ustiga bosish - o'sha joyga yangi nuqta, "
                                        "Ctrl + nuqtaga bosish - nuqtani o'chirish. Esc yoki tugma qayta bosilsa - tugaydi.");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (editing)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
        if (ImGui::Button(editing ? ICON_FA_CHECK "  Tahrirlashni tugatish" : ICON_FA_DRAW_POLYGON "  Nuqtalarni tahrirlash", ImVec2(-FLT_MIN, 0)))
        {
            m_ColliderPointEdit = editing ? ColliderPointKind::None : kind;
            if (!editing)
            {
                m_TilePaint = false;
                m_JointAnchorEdit = m_Joint3DAnchorEdit = -1;
            }
        }
        if (editing)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();

        UI::PropertyHeader(std::format("Nuqtalar ({})", points.size()).c_str(), "Obyekt koordinatalarida (masshtabi bilan ko'paytiriladi)");
        int remove = -1;
        const float button = ImGui::GetFrameHeight();
        for (size_t i = 0; i < points.size(); i++)
        {
            ImGui::PushID((int)i);
            UI::PropertyLabel(std::format("  {}", i).c_str());
            ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
            changed |= ImGui::DragFloat2("##p", &points[i].x, 0.01f, 0.0f, 0.0f, "%.3f");
            ImGui::SameLine();
            ImGui::BeginDisabled((int)points.size() <= minPoints);
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0)))
                remove = (int)i;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip((int)points.size() <= minPoints ? "Kamida shuncha nuqta kerak" : "Nuqtani o'chirish");
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            points.erase(points.begin() + remove);
            changed = true;
        }
        UI::PropertyLabel("");
        if (ImGui::Button(ICON_FA_PLUS "  Nuqta qo'shish", ImVec2(-FLT_MIN, 0)))
        {
            // Polygons: in the middle of the closing edge; lines: one step further on.
            if (points.empty())
                points.push_back({ 0.0f, 0.0f });
            else if (points.size() == 1)
                points.push_back(points[0] + glm::vec2(1.0f, 0.0f));
            else if (kind == ColliderPointKind::Polygon)
                points.push_back((points.back() + points.front()) * 0.5f);
            else
                points.push_back(points.back() + (points.back() - points[points.size() - 2]));
            changed = true;
        }
        return changed;
    }

    bool SceneHierarchyPanel::FitPolygonToSprite(Entity entity, PolygonCollider2DComponent& c, int maxVertices)
    {
        auto* sprite = entity.TryGetComponent<SpriteRendererComponent>();
        if (!sprite)
            return false;
        AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
        const auto* animation = entity.TryGetComponent<SpriteAnimationComponent>();
        const glm::mat4 rect = Scene::GetSpriteRectTransform(*sprite, animation, registry);
        // The opaque outline of the frame the sprite shows (in its unit square, y up).
        std::vector<glm::vec2> unit;
        if (!sprite->TexturePath.empty())
        {
            ImageData image;
            if (Image::Load(Project::GetAssetFileSystemPath(sprite->TexturePath).string(), image, 4, false))
            {
                const SpriteRegion region = Scene::GetSpriteRegion(*sprite, animation, registry);
                const float w = (float)image.Width, h = (float)image.Height;
                // UVs have v up (textures are loaded bottom row first); the picture's rows go down.
                const int x0 = (int)std::floor(region.UVMin.x * w + 0.01f), x1 = (int)std::ceil(region.UVMax.x * w - 0.01f);
                const int y0 = (int)std::floor((1.0f - region.UVMax.y) * h + 0.01f), y1 = (int)std::ceil((1.0f - region.UVMin.y) * h - 0.01f);
                unit = Polygon2D::TraceAlphaOutline(image.Pixels.data(), (int)image.Width, (int)image.Height, x0, y0, x1, y1, 16, maxVertices);
            }
        }
        if (unit.size() < 3)
            unit = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } }; // the sprite's rectangle
        std::vector<glm::vec2> points;
        for (glm::vec2 u : unit)
        {
            if (sprite->FlipX) u.x = 1.0f - u.x;
            if (sprite->FlipY) u.y = 1.0f - u.y;
            glm::vec4 p = rect * glm::vec4(u.x - 0.5f, u.y - 0.5f, 0.0f, 1.0f);
            points.push_back(glm::vec2(p) - c.Offset);
        }
        if (sprite->FlipX != sprite->FlipY)
            std::reverse(points.begin(), points.end()); // mirrored: counter-clockwise again
        c.Points = std::move(points);
        return true;
    }

    // A frame of a texture shown in a box of `size` px (the whole texture without a frame).
    static void SpriteImage(const Ref<Texture2D>& texture, const SpriteRegion& region, float size)
    {
        glm::vec2 px = region.Pixels;
        float scale = size / std::max(1.0f, std::max(px.x, px.y));
        ImVec2 extent(std::max(4.0f, px.x * scale), std::max(4.0f, px.y * scale));
        // ImGui wants the top-left corner first: v flipped.
        ImGui::Image((ImTextureID)(uintptr_t)texture->GetRendererID(), extent, ImVec2(region.UVMin.x, region.UVMax.y),
                     ImVec2(region.UVMax.x, region.UVMin.y));
    }

    bool SceneHierarchyPanel::DrawSpriteRenderer(Entity entity, SpriteRendererComponent& c)
    {
        bool changed = UI::PropertyColor4("Rang", c.Color);
        if (UI::PropertyAssetPath("Tekstura", c.TexturePath, ".png;.jpg;.jpeg;.bmp;.tga", ICON_FA_FILE_IMAGE))
        {
            c.Texture = c.TexturePath.empty() ? nullptr
                : TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TexturePath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            changed = true;
        }

        // ---- sprite sheet frame
        const SpriteSheet* sheet = c.TexturePath.empty() ? nullptr : SpriteSheet::Get(c.TexturePath);
        const bool missingFrame = !c.SpriteName.empty() && (!sheet || !sheet->Find(c.SpriteName));
        UI::PropertyLabel("Sprite (kadr)", "Teksturaning nomlangan kadri (Sprite muharriri bilan kesiladi, .zmeta faylida saqlanadi). "
                                           "Bo'sh - butun rasm.");
        ImGui::PushID("spriteFrame");
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
        std::string preview = c.SpriteName.empty() ? std::string("(butun rasm)")
                            : missingFrame ? std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {} (topilmadi)", c.SpriteName) : c.SpriteName;
        ImGui::BeginDisabled(!sheet && c.SpriteName.empty());
        if (ImGui::BeginCombo("##frame", preview.c_str(), ImGuiComboFlags_HeightLarge))
        {
            if (ImGui::Selectable("(butun rasm)", c.SpriteName.empty()))
            {
                c.SpriteName.clear();
                changed = true;
            }
            if (sheet)
                for (const SpriteFrame& f : sheet->Frames)
                {
                    std::string label = std::format("{}  ({}x{})", f.Name, f.Size.x, f.Size.y);
                    if (ImGui::Selectable(label.c_str(), f.Name == c.SpriteName))
                    {
                        c.SpriteName = f.Name;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered() && c.Texture)
                    {
                        ImGui::BeginTooltip();
                        SpriteImage(c.Texture, SpriteRegion::FromFrame(f, c.Texture->GetWidth(), c.Texture->GetHeight()), 96.0f);
                        ImGui::EndTooltip();
                    }
                }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !sheet && c.SpriteName.empty())
            UI::Tooltip("Teksturada kadrlar yo'q: Sprite muharririda kesing (" ICON_FA_SCISSORS " tugma)");
        ImGui::SameLine();
        ImGui::BeginDisabled(c.TexturePath.empty());
        if (ImGui::Button(ICON_FA_SCISSORS, ImVec2(button, 0)) && OnSpriteEditorRequested)
            OnSpriteEditorRequested(c.TexturePath);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Sprite muharriri: teksturani kadrlarga kesish");
        ImGui::PopID();

        if (c.Texture)
        {
            UI::PropertyLabel("");
            SpriteRegion region = Scene::GetSpriteRegion(c, nullptr, Project::GetActive() ? Project::GetAssetRegistry() : nullptr);
            SpriteImage(c.Texture, region, 64.0f);
        }

        // ---- draw mode
        const char* modes[] = { "Oddiy (Simple)", "9-slice (Sliced)", "Plitka (Tiled)" };
        int mode = (int)c.DrawMode;
        UI::PropertyLabel("Chizish rejimi", "Oddiy: rasm kvadratga cho'ziladi (o'lchami - masshtab). 9-slice: burchaklar o'z o'lchamida qoladi, "
                                            "chetlar va o'rta cho'ziladi - panellar, tugmalar, ramkalar uchun. Plitka: chetlar va o'rta takrorlanadi.");
        ImGui::PushID("drawMode");
        if (ImGui::Combo("##v", &mode, modes, 3) && mode != (int)c.DrawMode)
        {
            // Switching to a sized mode: the scale becomes the size, so the sprite keeps its
            // footprint and the corners are not stretched.
            if (c.DrawMode == SpriteDrawMode::Simple && c.Size == glm::vec2(1.0f))
            {
                auto& t = entity.GetComponent<TransformComponent>();
                if (glm::abs(t.Scale.x) > 1e-4f && glm::abs(t.Scale.y) > 1e-4f && (t.Scale.x != 1.0f || t.Scale.y != 1.0f))
                {
                    c.Size = glm::abs(glm::vec2(t.Scale));
                    t.Scale.x = t.Scale.x < 0.0f ? -1.0f : 1.0f;
                    t.Scale.y = t.Scale.y < 0.0f ? -1.0f : 1.0f;
                }
            }
            c.DrawMode = (SpriteDrawMode)mode;
            changed = true;
        }
        ImGui::PopID();
        if (c.DrawMode != SpriteDrawMode::Simple)
        {
            if (UI::PropertyFloat2("O'lcham (birlik)", c.Size, 0.02f))
            {
                c.Size = glm::max(c.Size, glm::vec2(0.0f));
                changed = true;
            }
            UI::PropertyLabel("Chegaralar (px)", "Chap, o'ng, yuqori, past - burchaklar shu piksellar. Hammasi 0 bo'lsa kadrning "
                                                 "chegaralari ishlatiladi (Sprite muharriri).");
            ImGui::PushID("border");
            if (ImGui::DragFloat4("##v", &c.SliceBorder.x, 0.25f, 0.0f, 4096.0f, "%.0f"))
            {
                c.SliceBorder = glm::max(c.SliceBorder, glm::vec4(0.0f));
                changed = true;
            }
            ImGui::PopID();
            if (!c.HasSliceBorder())
            {
                const SpriteFrame* frame = sheet && !c.SpriteName.empty() ? sheet->Find(c.SpriteName) : nullptr;
                UI::PropertyLabel("");
                if (frame && frame->HasBorder())
                    ImGui::TextDisabled("Kadrdan: %.0f, %.0f, %.0f, %.0f", frame->Border.x, frame->Border.y, frame->Border.z, frame->Border.w);
                else
                    ImGui::TextDisabled(c.DrawMode == SpriteDrawMode::Tiled ? "Chegarasiz: butun rasm takrorlanadi" : "Chegara yo'q: oddiy cho'zish");
            }
            if (UI::PropertyFloat("Piksel / birlik", c.PixelsPerUnit, 0.5f, 0.01f, 10000.0f, "%.1f"))
            {
                c.PixelsPerUnit = std::max(0.01f, c.PixelsPerUnit);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Bir dunyo birligiga necha piksel: chegara burchaklarining va plitkalarning o'lchami");
        }
        else
        {
            ImGui::BeginDisabled(!c.SpriteName.empty());
            changed |= UI::PropertyFloat("Takrorlash", c.TilingFactor, 0.05f, 0.0f, 100.0f);
            ImGui::EndDisabled();
        }

        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText("Saralash");
        changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);
        if (UI::PropertyBool("Pixel-art (aniq piksellar)", c.PixelArt) && !c.TexturePath.empty())
        {
            c.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TexturePath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            changed = true;
        }
        changed |= UI::PropertyBool("Gorizontal aks (Flip X)", c.FlipX);
        changed |= UI::PropertyBool("Vertikal aks (Flip Y)", c.FlipY);
        changed |= UI::PropertyBool(ICON_FA_FIRE "  Yorug'lik (Lit)", c.Lit);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Light 2D yorug'liklari va sahnaning 2D muhit yorug'ligi (Sahna sozlamalari) bilan yoritiladi. "
                        "O'chiq: rasm o'z rangida chiziladi");

        // ---- effects: hit flash, outline
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText("Effektlar");
        UI::PropertyLabel("Flash rangi", "Rasm shu rangga aralashadi (oq - urilish chaqnashi). Skriptdan: Flash(rang, soniya)");
        changed |= ImGui::ColorEdit4("##flashColor", &c.FlashColor.x, ImGuiColorEditFlags_AlphaBar);
        UI::PropertyLabel("Flash", "0 - o'chiq, 1 - butunlay flash rangida. Skriptdagi Flash() buning ustidan so'nib boradigan flash o'ynaydi");
        if (ImGui::SliderFloat("##flashAmount", &c.FlashAmount, 0.0f, 1.0f, "%.2f"))
        {
            c.FlashAmount = std::clamp(c.FlashAmount, 0.0f, 1.0f);
            changed = true;
        }
        {
            const bool running = m_Context && m_Context->IsRunning();
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!running);
            if (ImGui::Button(ICON_FA_BOLT "  Sinash"))
                c.Flash(glm::vec4(glm::vec3(c.FlashColor), 1.0f), 0.3f);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip(running ? "Flash rangida 0.3 soniyalik chaqnash" : "O'yin rejimida sinab ko'ring");
            if (running && c.IsFlashing())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%.2f", c.GetFlashAmount());
            }
        }
        UI::PropertyLabel("Kontur rangi");
        changed |= ImGui::ColorEdit4("##outlineColor", &c.OutlineColor.x, ImGuiColorEditFlags_AlphaBar);
        UI::PropertyLabel("Kontur (px)", "Rasmning ko'rinadigan piksellari atrofidagi chegara, tekstura pikselida (0 - o'chiq). "
                                         "Skriptdan: SetOutline(rang, qalinlik)");
        if (ImGui::DragFloat("##outlineThickness", &c.OutlineThickness, 0.05f, 0.0f, 16.0f, "%.1f"))
        {
            c.OutlineThickness = std::clamp(c.OutlineThickness, 0.0f, 64.0f);
            changed = true;
        }
        if (c.OutlineThickness > 0.0f)
        {
            // The renderer's own rule (a screen UI sprite in a sized mode is laid out in its UI rect).
            const auto* ui = entity.TryGetComponent<UITransformComponent>();
            const glm::vec2* uiSize = ui && c.DrawMode != SpriteDrawMode::Simple ? &ui->Size : nullptr;
            if (!Scene::CanDrawOutline(c, ui ? nullptr : entity.TryGetComponent<SpriteAnimationComponent>(),
                                       Project::GetActive() ? Project::GetAssetRegistry() : nullptr, uiSize))
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled(!c.Texture ? "Kontur uchun tekstura kerak"
                                               : "Kontur faqat bitta bo'lak bo'lib chiziladigan sprite'da (chegarasiz, takrorlashsiz)");
            }
        }
        return changed;
    }

    // ------------------------------------------------------------------ tint animation
    // The gradient as a strip `height` px high (a checkerboard under see-through colours).
    static void DrawTintGradient(const std::vector<TintKey>& keys, float width, float height, float marker = -1.0f)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + width, p0.y + height);
        const float cell = height * 0.5f;
        for (float x = p0.x; x < p1.x; x += cell)
            for (int row = 0; row < 2; row++)
            {
                bool dark = ((int)((x - p0.x) / cell) + row) % 2 == 0;
                dl->AddRectFilled(ImVec2(x, p0.y + row * cell), ImVec2(std::min(x + cell, p1.x), p0.y + (row + 1) * cell),
                                  dark ? IM_COL32(90, 90, 96, 255) : IM_COL32(140, 140, 148, 255));
            }
        const int steps = std::max(8, (int)(width / 4.0f));
        for (int i = 0; i < steps; i++)
        {
            float t0 = (float)i / steps, t1 = (float)(i + 1) / steps;
            glm::vec4 ca = TintAnimation::EvaluateGradient(keys, t0), cb = TintAnimation::EvaluateGradient(keys, t1);
            ImU32 a = ImGui::ColorConvertFloat4ToU32(ImVec4(ca.r, ca.g, ca.b, ca.a));
            ImU32 b = ImGui::ColorConvertFloat4ToU32(ImVec4(cb.r, cb.g, cb.b, cb.a));
            dl->AddRectFilledMultiColor(ImVec2(p0.x + width * t0, p0.y), ImVec2(p0.x + width * t1, p1.y), a, b, b, a);
        }
        for (const TintKey& k : keys)
        {
            float x = p0.x + width * std::clamp(k.Time, 0.0f, 1.0f);
            dl->AddTriangleFilled(ImVec2(x - 4.0f, p1.y + 5.0f), ImVec2(x + 4.0f, p1.y + 5.0f), ImVec2(x, p1.y - 1.0f), IM_COL32(230, 230, 235, 255));
        }
        if (marker >= 0.0f)
        {
            float x = p0.x + width * std::clamp(marker, 0.0f, 1.0f);
            dl->AddLine(ImVec2(x, p0.y - 2.0f), ImVec2(x, p1.y + 2.0f), IM_COL32(255, 210, 80, 255), 2.0f);
        }
        dl->AddRect(p0, p1, IM_COL32(20, 20, 24, 255));
        ImGui::Dummy(ImVec2(width, height + 6.0f));
    }

    bool SceneHierarchyPanel::DrawTintAnimation(Entity entity, TintAnimationComponent& c)
    {
        bool changed = false;
        const bool renders = entity.HasComponent<SpriteRendererComponent>() || entity.HasComponent<CircleRendererComponent>() ||
                             entity.HasComponent<TextComponent>() || entity.HasComponent<TilemapComponent>() ||
                             entity.HasComponent<Light2DComponent>();
        if (!renders)
        {
            UI::PropertyLabel("");
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Bo'yaydigan narsa yo'q (Sprite, Circle, Text, Tilemap yoki Light 2D)");
        }

        // ---- gradient: a strip with the keys, then the keys one by one
        UI::PropertyLabel("Gradiyent", "Chiziladigan rang = renderer rangi x shu gradiyent rangi (animatsiya vaqtida). "
                                       "Oq - o'zgarishsiz, alfa 0 - ko'rinmas. Renderer'ning o'z rangi o'zgarmaydi");
        const bool playing = c.Playing || c.EditorPreview;
        DrawTintGradient(c.Keys, std::max(40.0f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight() * 0.8f,
                         playing || c.Elapsed > 0.0f || c.StartOffset > 0.0f ? c.GetPhase() : -1.0f);
        int remove = -1;
        bool sort = false;
        for (int i = 0; i < (int)c.Keys.size(); i++)
        {
            TintKey& k = c.Keys[(size_t)i];
            ImGui::PushID(i);
            UI::PropertyLabel(i == 0 ? "Kalitlar" : "", "Vaqt (0..1, davomiylikka nisbatan) va rang. Kalitlar orasida rang tekis o'tadi");
            const float button = ImGui::GetFrameHeight();
            const float swatch = ImGui::GetFrameHeight() * 1.8f;
            const float avail = ImGui::GetContentRegionAvail().x - button - swatch - ImGui::GetStyle().ItemSpacing.x * 2.0f;
            ImGui::SetNextItemWidth(std::max(40.0f, avail));
            if (ImGui::DragFloat("##time", &k.Time, 0.005f, 0.0f, 1.0f, "vaqt %.2f"))
                changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit())
                sort = true;
            ImGui::SameLine();
            // The swatch opens the picker (rgba, the alpha shown over a checkerboard).
            changed |= ImGui::ColorEdit4("##color", &k.Color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar |
                                                                ImGuiColorEditFlags_AlphaPreviewHalf);
            ImGui::SameLine();
            ImGui::BeginDisabled(c.Keys.size() <= 1);
            if (ImGui::Button(ICON_FA_TRASH, ImVec2(button, 0)))
                remove = i;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip(c.Keys.size() <= 1 ? "Kamida bitta kalit qoladi" : "Kalitni o'chirish");
            ImGui::PopID();
        }
        if (remove >= 0 && c.Keys.size() > 1)
        {
            c.Keys.erase(c.Keys.begin() + remove);
            changed = true;
        }
        UI::PropertyLabel("");
        if (ImGui::Button(ICON_FA_PLUS "  Kalit"))
        {
            // In the widest gap, with the colour the gradient has there (nothing changes until it is edited).
            TintAnimation::SortKeys(c.Keys);
            float time = 0.5f, widest = -1.0f;
            for (size_t i = 0; i + 1 < c.Keys.size(); i++)
                if (c.Keys[i + 1].Time - c.Keys[i].Time > widest)
                {
                    widest = c.Keys[i + 1].Time - c.Keys[i].Time;
                    time = (c.Keys[i].Time + c.Keys[i + 1].Time) * 0.5f;
                }
            if (c.Keys.size() == 1)
                time = c.Keys[0].Time < 0.5f ? 1.0f : 0.0f;
            c.Keys.push_back({ time, TintAnimation::EvaluateGradient(c.Keys, time) });
            sort = changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Teskari"))
        {
            for (TintKey& k : c.Keys)
                k.Time = 1.0f - k.Time;
            sort = changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Gradiyentni teskari aylantirish");
        if (sort)
            TintAnimation::SortKeys(c.Keys);

        // ---- timing
        if (UI::PropertyFloat("Davomiylik (s)", c.Duration, 0.01f, 0.0f, 3600.0f, "%.2f"))
        {
            c.Duration = std::max(0.0f, c.Duration);
            changed = true;
        }
        const char* modes[] = { "Bir marta (Once)", "Takror (Loop)", "Borib-qaytish (PingPong)" };
        int mode = (int)c.Mode;
        if (UI::PropertyCombo("Rejim", mode, modes, 3))
        {
            c.Mode = (TintPlayMode)mode;
            changed = true;
        }
        static const char* eases[EaseCount] = { "Linear", "In Quad", "Out Quad", "In-Out Quad", "Out Cubic", "In-Out Cubic",
                                                "Out Back", "Out Elastic", "Out Bounce" };
        int ease = (int)c.Easing;
        if (UI::PropertyCombo("Silliqlik (ease)", ease, eases, EaseCount))
        {
            c.Easing = (Ease)ease;
            changed = true;
        }
        if (c.Mode != TintPlayMode::Once)
        {
            if (UI::PropertyInt("Sikllar (0 = cheksiz)", c.Cycles))
            {
                c.Cycles = std::max(0, c.Cycles);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Shuncha marta aylanib to'xtaydi (PingPong: borib-qaytish bitta sikl). Masalan miltillash: oq -> shaffof, PingPong, 6 sikl");
        }
        if (UI::PropertyFloat("Boshlanish siljishi (s)", c.StartOffset, 0.01f, 0.0f, 3600.0f, "%.2f"))
        {
            c.StartOffset = std::max(0.0f, c.StartOffset);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Animatsiya shuncha soniyadan boshlanadi: bir xil obyektlar bir vaqtda miltillamasin");
        changed |= UI::PropertyBool("O'yin boshida", c.PlayOnStart);
        if (ImGui::IsItemHovered())
            UI::Tooltip("O'yin boshlanganda (yoki obyekt paydo bo'lganda) o'zi boshlanadi. Skriptdan: PlayTint() / StopTint()");

        // ---- preview (scene view) / play controls (game, and Simulate: it plays the tints too)
        UI::PropertyLabel("");
        const bool running = m_Context && (m_Context->IsRunning() || m_Context->IsSimulating());
        if (running)
        {
            if (ImGui::Button(c.Playing ? ICON_FA_STOP "  To'xtatish" : ICON_FA_PLAY "  Boshlash"))
            {
                if (c.Playing) c.Stop();
                else c.Play(true);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f s%s", c.Elapsed, c.IsFinished() ? "  (tugadi)" : "");
        }
        else
        {
            if (ImGui::Button(c.EditorPreview ? ICON_FA_STOP "  Ko'rishni to'xtatish" : ICON_FA_PLAY "  Sahnada ko'rish"))
            {
                c.EditorPreview = !c.EditorPreview;
                c.Elapsed = 0.0f;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Editor sahnasida aylanib o'ynaydi (saqlanmaydi; o'yin har doim boshidan boshlanadi)");
        }
        return changed;
    }

    // ------------------------------------------------------------------ 2D light
    bool SceneHierarchyPanel::DrawLight2D(Entity entity, Light2DComponent& c)
    {
        InactiveComponentNote(entity, "Obyekt nofaol: yorug'lik hisobga olinmaydi");
        bool changed = false;
        const char* types[] = { "Nuqta (Point)", "Konus (Spot)" };
        int type = (int)c.Type;
        if (UI::PropertyCombo("Turi", type, types, 2))
        {
            c.Type = (Light2DType)type;
            changed = true;
        }
        changed |= UI::PropertyColor3("Rang", c.Color);
        if (UI::PropertyFloat("Kuch", c.Intensity, 0.01f, 0.0f, 100.0f, "%.2f"))
        {
            c.Intensity = std::max(0.0f, c.Intensity);
            changed = true;
        }
        if (UI::PropertyFloat("Radius", c.Radius, 0.02f, 0.0f, 1000.0f, "%.2f"))
        {
            c.Radius = std::max(0.0f, c.Radius);
            c.InnerRadius = std::min(c.InnerRadius, c.Radius);
            changed = true;
        }
        if (UI::PropertyFloat("Ichki radius", c.InnerRadius, 0.02f, 0.0f, 1000.0f, "%.2f"))
        {
            c.InnerRadius = std::clamp(c.InnerRadius, 0.0f, c.Radius);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Shu masofagacha to'liq yorqinlik, keyin radiusgacha so'nadi");
        UI::PropertyLabel("So'nish", "(1 - t)^daraja: 1 - chiziqli, 2 - yumshoq, 4 - yorqin markaz, tez so'nadi");
        if (ImGui::SliderFloat("##falloff", &c.Falloff, 0.25f, 6.0f, "%.2f"))
        {
            c.Falloff = std::clamp(c.Falloff, 0.1f, 8.0f);
            changed = true;
        }
        if (c.Type == Light2DType::Spot)
        {
            UI::PropertyLabel("Konus burchagi", "Konusning to'liq kengligi (gradus). Obyektning +X o'qi bo'ylab: aylantirish bilan buriladi");
            if (ImGui::SliderFloat("##spotAngle", &c.SpotAngle, 1.0f, 360.0f, "%.0f\xc2\xb0"))
            {
                c.SpotAngle = std::clamp(c.SpotAngle, 1.0f, 360.0f);
                changed = true;
            }
            UI::PropertyLabel("Chet yumshoqligi", "Konus chetining qanchasi (0..1) yumshoq so'nadi");
            if (ImGui::SliderFloat("##spotSoftness", &c.SpotSoftness, 0.0f, 1.0f, "%.2f"))
            {
                c.SpotSoftness = std::clamp(c.SpotSoftness, 0.0f, 1.0f);
                changed = true;
            }
        }
        UI::PropertyLabel("Miltillash", "Kuch shuncha (0..1) tebranadi: mash'al, olov. 0 - tekis");
        if (ImGui::SliderFloat("##flicker", &c.Flicker, 0.0f, 1.0f, "%.2f"))
        {
            c.Flicker = std::clamp(c.Flicker, 0.0f, 1.0f);
            changed = true;
        }
        if (c.Flicker > 0.0f && UI::PropertyFloat("Miltillash tezligi", c.FlickerSpeed, 0.1f, 0.0f, 100.0f, "%.1f"))
        {
            c.FlickerSpeed = std::max(0.0f, c.FlickerSpeed);
            changed = true;
        }
        // Which renderers it lights: only the Lit ones.
        if (m_Context)
        {
            size_t lit = 0;
            for (auto e : m_Context->GetAllEntitiesWith<SpriteRendererComponent>())
                if (Entity(e, m_Context.get()).GetComponent<SpriteRendererComponent>().Lit) lit++;
            for (auto e : m_Context->GetAllEntitiesWith<TilemapComponent>())
                if (Entity(e, m_Context.get()).GetComponent<TilemapComponent>().Lit) lit++;
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            if (lit == 0)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Sahnada \"Lit\" belgilangan sprite/tilemap yo'q: "
                                   "yorug'lik faqat ularni yoritadi");
            else
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  %zu ta Lit renderer'ni yoritadi (muhit yorug'ligi: Sahna sozlamalari > 2D yorug'lik)", lit);
            ImGui::PopTextWrapPos();
        }
        return changed;
    }

    // ------------------------------------------------------------------ camera: pixel perfect
    bool SceneHierarchyPanel::DrawPixelPerfect(CameraComponent& c)
    {
        bool changed = false;
        PixelPerfectSettings& pp = c.PixelPerfect;
        UI::PropertyHeader(ICON_FA_CHESS_BOARD "  Pixel-perfect",
                           "Har bir tekstura pikseli ekranda bir xil butun sondagi pikselga aylanadi (pixel-art o'yinlar). "
                           "Faqat o'yin ko'rinishida (Play / Player); editor sahnasi o'zgarmaydi");
        changed |= UI::PropertyBool("Yoqilgan", pp.Enabled);
        if (!pp.Enabled)
            return changed;
        if (UI::PropertyFloat("Piksel / birlik", pp.PixelsPerUnit, 0.25f, PixelPerfect::MinPixelsPerUnit, PixelPerfect::MaxPixelsPerUnit, "%.1f"))
        {
            PixelPerfect::Sanitize(pp);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Bir dunyo birligida nechta tekstura pikseli (sprite'lar shu o'lchamda chizilgan bo'lishi kerak)");
        UI::PropertyLabel("Asosiy o'lcham (px)", "O'yin shu o'lchamdagi (art piksel) ekran uchun yasalgan: kattalashtirish - shu o'lcham "
                                                 "ko'rinishga sig'adigan eng katta butun son. Katta oyna dunyodan ko'proq ko'rsatadi");
        if (ImGui::DragInt2("##reference", &pp.ReferenceResolution.x, 1.0f, 1, 8192))
        {
            PixelPerfect::Sanitize(pp);
            changed = true;
        }
        changed |= UI::PropertyBool("Past o'lchamda chizish", pp.UpscaleRenderTarget);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sahna art o'lchamida chiziladi va aniq piksellar bilan kattalashtiriladi: aylantirilgan sprite'lar va "
                        "chiziqlar ham piksel to'rida qoladi (UI ham shu o'lchamda)");
        changed |= UI::PropertyBool("Kamerani pikselga", pp.SnapCamera);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ko'rinish butun piksellarda siljiydi - harakatda piksellar titramaydi");
        changed |= UI::PropertyBool("Sprite'larni pikselga", pp.SnapSprites);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Sprite va tilemap'lar ham butun piksellarda chiziladi (transformlari o'zgarmaydi)");
        if (m_Context)
        {
            glm::vec2 viewport = m_Context->GetViewportSize();
            if (viewport.x > 0.0f && viewport.y > 0.0f)
            {
                PixelPerfectView v = PixelPerfect::Compute(pp, (uint32_t)viewport.x, (uint32_t)viewport.y);
                UI::PropertyLabel("");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  %.0fx%.0f oynada: x%d, %ux%u art piksel, %.2f x %.2f birlik", viewport.x, viewport.y,
                                    v.Zoom, (unsigned)((float)v.ViewSize.x / (float)v.Zoom), (unsigned)((float)v.ViewSize.y / (float)v.Zoom),
                                    v.VisibleWorldSize.x, v.VisibleWorldSize.y);
                ImGui::PopTextWrapPos();
            }
        }
        return changed;
    }

    // ------------------------------------------------------------------ tilemap
    static const char* TileToolName(SceneHierarchyPanel::TileTool tool)
    {
        switch (tool)
        {
            case SceneHierarchyPanel::TileTool::Brush: return "Mo'yqalam";
            case SceneHierarchyPanel::TileTool::Rectangle: return "To'rtburchak";
            case SceneHierarchyPanel::TileTool::Fill: return "To'ldirish";
            case SceneHierarchyPanel::TileTool::Eraser: return "O'chirg'ich";
            case SceneHierarchyPanel::TileTool::Picker: return "Tomizg'ich";
        }
        return "";
    }

    void SceneHierarchyPanel::SetTileTool(TileTool tool)
    {
        if (tool == TileTool::Picker && m_TileTool != TileTool::Picker)
            m_ToolBeforePicker = m_TileTool;
        m_TileTool = tool;
        m_TilePaint = true;
    }

    void SceneHierarchyPanel::PickTile(int tile, int rule)
    {
        if (tile < 0)
            return;
        m_TileBrush = TileBrush::Single(tile);
        m_PaletteScrollRequest++;
        m_TileRule = rule;
        if (m_TileTool == TileTool::Picker)
            m_TileTool = m_ToolBeforePicker == TileTool::Picker || m_ToolBeforePicker == TileTool::Eraser ? TileTool::Brush : m_ToolBeforePicker;
    }

    // The 3 x 3 neighbourhood of an auto-tile variant: the cell in the middle, its neighbours of the
    // same terrain filled.
    static void DrawAutoTileMask(ImDrawList* draw, ImVec2 min, float size, AutoTileMode mode, int variant)
    {
        const float cell = size / 3.0f;
        const ImU32 on = UI::AccentU32(0.95f), off = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f)), center = IM_COL32(240, 240, 240, 255);
        uint8_t mask8 = 0;
        if (mode == AutoTileMode::Blob8)
            mask8 = AutoTile::BlobMask(variant);
        else
            mask8 = (uint8_t)(((variant & AutoTile::North) ? AutoTile::N : 0) | ((variant & AutoTile::West) ? AutoTile::W : 0) |
                              ((variant & AutoTile::East) ? AutoTile::E : 0) | ((variant & AutoTile::South) ? AutoTile::S : 0));
        // Rows top to bottom: NW N NE / W . E / SW S SE
        const uint8_t bits[9] = { AutoTile::NW, AutoTile::N, AutoTile::NE, AutoTile::W, 0, AutoTile::E, AutoTile::SW, AutoTile::S, AutoTile::SE };
        for (int i = 0; i < 9; i++)
        {
            ImVec2 a(min.x + (i % 3) * cell + 1.0f, min.y + (i / 3) * cell + 1.0f), b(a.x + cell - 2.0f, a.y + cell - 2.0f);
            bool corner = i == 0 || i == 2 || i == 6 || i == 8;
            if (i == 4)
                draw->AddRectFilled(a, b, center, 1.0f);
            else if (mode == AutoTileMode::Edges4 && corner)
                continue;
            else
                draw->AddRectFilled(a, b, (mask8 & bits[i]) ? on : off, 1.0f);
        }
    }

    bool SceneHierarchyPanel::SaveAutoTiles(const std::string& tilesetPath, const AutoTileSet& set)
    {
        AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
        const AssetMetadata* meta = registry ? registry->Find(std::filesystem::path(tilesetPath)) : nullptr;
        if (!meta)
        {
            ZM_ERROR("Auto-tile qoidalari saqlanmadi: '{}' asset registry'da topilmadi", tilesetPath);
            return false;
        }
        AssetImportSettings import = meta->Import; // the texture's own settings and its sprites stay
        set.WriteTo(import);
        if (!registry->SetImportSettings(meta->Handle, import))
        {
            ZM_ERROR("Auto-tile qoidalari saqlanmadi: {}", tilesetPath);
            return false;
        }
        return true;
    }

    bool SceneHierarchyPanel::DrawTilemap(Entity entity, TilemapComponent& c)
    {
        bool changed = false;
        // Another tilemap: its own tileset's rules, from its first layer (the paint tool stays).
        if ((uint64_t)entity.GetUUID() != m_TileEntity)
        {
            if (m_TileEntity)
            {
                m_TileRule = -1;
                m_EditRule = false;
                m_EditRuleVariant = -1;
                m_TileLayer = std::max(0, c.FindLayer("Asosiy"));
            }
            m_TileEntity = (uint64_t)entity.GetUUID();
        }
        if (UI::PropertyAssetPath("Tileset", c.TilesetPath, ".png;.jpg;.jpeg", ICON_FA_FILE_IMAGE)) { c.Tileset = nullptr; changed = true; m_TileRule = -1; }
        if (UI::PropertyBool("Pixel-art", c.PixelArt)) { c.Tileset = nullptr; changed = true; }
        changed |= UI::PropertyInt("Tileset ustunlari", c.TilesetColumns);
        changed |= UI::PropertyInt("Tileset qatorlari", c.TilesetRows);
        c.TilesetColumns = std::max(1, c.TilesetColumns); c.TilesetRows = std::max(1, c.TilesetRows);
        int w = c.Width, h = c.Height;
        bool resized = UI::PropertyInt("Kenglik (plitka)", w);
        resized |= UI::PropertyInt("Balandlik (plitka)", h);
        if (resized) { c.Resize(std::clamp(w, 1, 1024), std::clamp(h, 1, 1024)); changed = true; }
        changed |= UI::PropertyFloat("Plitka o'lchami", c.TileSize, 0.01f, 0.01f, 100.0f);
        changed |= UI::PropertyColor4("Rang", c.Color);
        changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);
        changed |= UI::PropertyBool(ICON_FA_FIRE "  Yorug'lik (Lit)", c.Lit);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Plitkalar Light 2D yorug'liklari va 2D muhit yorug'ligi bilan yoritiladi (g'orlar, tungi sahnalar)");
        changed |= UI::PropertyBool("Collision (fizika)", c.Collision);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Collision belgilangan qatlamlardagi plitkalar qattiq (qo'shni plitkalar bitta to'rtburchakka birlashadi)");
        InactiveComponentNote(entity, c.Collision ? "Obyekt nofaol: tilemap chizilmaydi, kolliziyasi o'chirilgan (faollashganda qayta quriladi)"
                                                  : "Obyekt nofaol: tilemap chizilmaydi");
        {
            PhysicsMaterialData own;
            own.Friction = c.Friction;
            own.Restitution = c.Restitution;
            own.RestitutionThreshold = 1.0f;
            own.Density = 0.0f;
            bool material = false;
            changed |= DrawPhysicsMaterialField(entity, c.MaterialPath, own, material);
            ImGui::BeginDisabled(material);
            changed |= UI::PropertySlider("Ishqalanish", c.Friction, 0.0f, 1.0f);
            changed |= UI::PropertySlider("Sakrash", c.Restitution, 0.0f, 1.0f);
            ImGui::EndDisabled();
            changed |= DrawCollisionLayer("Qatlam", c.CollisionLayer, false,
                                          "Plitkalarning to'qnashuv qatlami (Loyiha sozlamalari > Fizika qatlamlari)");
        }
        c.EnsureSize();

        // ---- layers
        m_TileLayer = std::clamp(m_TileLayer, 0, c.GetLayerCount() - 1);
        UI::PropertyHeader(ICON_FA_LAYER_GROUP "  Qatlamlar", "Har bir qatlamning o'z plitkalari bor. Ro'yxat tartibida chiziladi (birinchisi "
                                                             "orqada); bo'yash faol (belgilangan) qatlamga tushadi");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetColumnIndex(1);
        {
            const float rowHeight = ImGui::GetFrameHeight();
            const float listHeight = std::min(6, c.GetLayerCount()) * (rowHeight + ImGui::GetStyle().ItemSpacing.y) +
                                     ImGui::GetStyle().WindowPadding.y * 2.0f;
            ImGui::BeginChild("##tileLayers", ImVec2(-FLT_MIN, listHeight), ImGuiChildFlags_Border);
            // Top of the list = the layer drawn last (in front), like image editors.
            for (int i = c.GetLayerCount() - 1; i >= 0; i--)
            {
                TilemapLayer& layer = c.Layers[(size_t)i];
                ImGui::PushID(i);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, ImGui::GetStyle().FramePadding.y));
                if (UI::ToolbarButton(layer.Visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, layer.Visible ? "Ko'rinadi (yashirish)" : "Yashirin (ko'rsatish)"))
                {
                    layer.Visible = !layer.Visible;
                    changed = true;
                }
                ImGui::SameLine(0, 2);
                ImGui::PushStyleColor(ImGuiCol_Text, layer.Collision ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (UI::ToolbarButton(ICON_FA_VECTOR_SQUARE, layer.Collision ? "Collision: plitkalari qattiq (o'chirish)" : "Collision yo'q (yoqish)"))
                {
                    layer.Collision = !layer.Collision;
                    changed = true;
                }
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                ImGui::SameLine();
                std::string label = std::format("{}##layer", layer.Name);
                if (ImGui::Selectable(label.c_str(), m_TileLayer == i, ImGuiSelectableFlags_None, ImVec2(0, rowHeight)))
                    m_TileLayer = i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip(std::format("{}: {} ta plitka{}{}", layer.Name, c.CountTiles(i), layer.Collision ? ", qattiq" : "",
                                            layer.SortingOrder ? std::format(", tartib {:+}", layer.SortingOrder) : std::string()).c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        UI::PropertyLabel("");
        {
            ImGui::BeginDisabled(c.GetLayerCount() >= TilemapComponent::MaxLayers);
            if (UI::ToolbarButton(ICON_FA_PLUS, "Yangi qatlam (faol qatlam ustiga)"))
            {
                int added = c.AddLayer("Qatlam");
                if (added >= 0)
                {
                    // Right above the active one.
                    int to = std::min(m_TileLayer + 1, c.GetLayerCount() - 1);
                    c.MoveLayer(added, to);
                    m_TileLayer = to;
                    changed = true;
                }
            }
            ImGui::SameLine(0, 2);
            if (UI::ToolbarButton(ICON_FA_COPY, "Faol qatlamdan nusxa"))
            {
                TilemapLayer copy = c.Layers[(size_t)m_TileLayer];
                int added = c.AddLayer(copy.Name);
                if (added >= 0)
                {
                    std::string name = c.Layers[(size_t)added].Name;
                    c.Layers[(size_t)added] = std::move(copy);
                    c.Layers[(size_t)added].Name = name;
                    int to = std::min(m_TileLayer + 1, c.GetLayerCount() - 1);
                    c.MoveLayer(added, to);
                    m_TileLayer = to;
                    changed = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(m_TileLayer >= c.GetLayerCount() - 1);
            if (UI::ToolbarButton(ICON_FA_ARROW_UP, "Yuqoriga (oldinroq chiziladi)") && c.MoveLayer(m_TileLayer, m_TileLayer + 1))
            {
                m_TileLayer++;
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(m_TileLayer <= 0);
            if (UI::ToolbarButton(ICON_FA_ARROW_DOWN, "Pastga (orqaroq chiziladi)") && c.MoveLayer(m_TileLayer, m_TileLayer - 1))
            {
                m_TileLayer--;
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(c.GetLayerCount() <= 1);
            if (UI::ToolbarButton(ICON_FA_TRASH, "Faol qatlamni o'chirish (plitkalari bilan)") && c.RemoveLayer(m_TileLayer))
            {
                m_TileLayer = std::clamp(m_TileLayer, 0, c.GetLayerCount() - 1);
                changed = true;
            }
            ImGui::EndDisabled();
        }
        {
            TilemapLayer& layer = c.Layers[(size_t)m_TileLayer];
            UI::PropertyLabel("Nomi");
            std::string name = layer.Name;
            if (ImGui::InputText("##layerName", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) || ImGui::IsItemDeactivatedAfterEdit())
            {
                int other = c.FindLayer(name);
                if (!name.empty() && (other < 0 || other == m_TileLayer) && name != layer.Name)
                {
                    layer.Name = name;
                    changed = true;
                }
            }
            UI::PropertyLabel("Shaffoflik", "Qatlam plitkalarining ko'rinishi (tilemap rangining alfasiga ko'paytiriladi)");
            if (ImGui::SliderFloat("##layerOpacity", &layer.Opacity, 0.0f, 1.0f, "%.2f"))
            {
                layer.Opacity = std::clamp(layer.Opacity, 0.0f, 1.0f);
                changed = true;
            }
            UI::PropertyLabel("Tartib (+)", "Tilemap tartibiga qo'shiladi: masalan +2 - sprite'lar (0) oldida chiziladigan old plan qatlami");
            changed |= ImGui::DragInt("##layerOrder", &layer.SortingOrder, 0.1f, -1000, 1000);
            changed |= UI::PropertyBool("Ko'rinadi", layer.Visible);
            changed |= UI::PropertyBool("Collision", layer.Collision);
            UI::PropertyLabel("Plitkalar");
            ImGui::TextDisabled("%zu / %d", c.CountTiles(m_TileLayer), c.Width * c.Height);
            if (layer.Collision && !c.Collision)
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled("Tilemap'ning Collision'i o'chiq");
            }
        }

        // ---- paint tools
        UI::PropertyHeader(ICON_FA_PAINTBRUSH "  Bo'yash", "Viewport'da sichqoncha bilan. B - mo'yqalam, R - to'rtburchak, G - to'ldirish, "
                                                         "E - o'chirg'ich, I - tomizg'ich, Esc - chiqish. Shift bosilsa o'chiradi. Ctrl+Z - bekor qilish");
        UI::PropertyLabel("Rejim");
        ImGui::Checkbox(ICON_FA_PEN "  Viewport'da chizish", &m_TilePaint);
        UI::PropertyLabel("Asbob");
        {
            struct ToolButton { TileTool Tool; const char* Icon; const char* Tip; };
            static const ToolButton s_Tools[] = {
                { TileTool::Brush, ICON_FA_PAINTBRUSH, "Mo'yqalam (B): bosib sudrang. Shift - o'chiradi" },
                { TileTool::Rectangle, ICON_FA_VECTOR_SQUARE, "To'rtburchak (R): burchakdan burchakka sudrang. Shift - o'chiradi" },
                { TileTool::Fill, ICON_FA_FILL_DRIP, "To'ldirish (G): bir xil qo'shni plitkalar sohasini to'ldiradi. Shift - sohani o'chiradi" },
                { TileTool::Eraser, ICON_FA_ERASER, "O'chirg'ich (E)" },
                { TileTool::Picker, ICON_FA_EYEDROPPER, "Tomizg'ich (I): xaritadagi plitkani tanlaydi" },
            };
            for (size_t i = 0; i < std::size(s_Tools); i++)
            {
                if (i) ImGui::SameLine(0, 2);
                if (UI::ToolbarButton(s_Tools[i].Icon, s_Tools[i].Tip, m_TilePaint && m_TileTool == s_Tools[i].Tool))
                    SetTileTool(s_Tools[i].Tool);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TileToolName(m_TileTool));
        }
        UI::PropertyLabel("");
        if (ImGui::Button(ICON_FA_ERASER "  Qatlamni tozalash"))
        {
            TilemapLayer& layer = c.Layers[(size_t)m_TileLayer];
            if (std::any_of(layer.Tiles.begin(), layer.Tiles.end(), [](int t) { return t >= 0; }))
            {
                std::fill(layer.Tiles.begin(), layer.Tiles.end(), -1);
                changed = true;
            }
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Faol qatlamning hamma plitkalarini o'chiradi (Ctrl+Z bilan qaytadi)");

        // ---- auto-tiles (rules of the tileset, kept in its .zmeta)
        const AutoTileSet* stored = AutoTileSet::Get(c.TilesetPath);
        AutoTileSet rules = stored ? *stored : AutoTileSet();
        if (m_TileRule >= (int)rules.Rules.size())
            m_TileRule = -1;
        bool rulesEdited = false;
        UI::PropertyHeader(ICON_FA_BORDER_ALL "  Auto-tile", "Qoida tanlansa, asboblar plitka o'rniga shu yerni (terrain) chizadi: har bir katak "
                                                            "qo'shnilariga qarab to'g'ri plitkani oladi, qo'shnilari ham yangilanadi. Qoidalar "
                                                            "tileset'ning .zmeta faylida saqlanadi (shu tileset'li hamma tilemap'larda)");
        UI::PropertyLabel("Qoida");
        {
            std::string preview = m_TileRule >= 0 ? std::string(ICON_FA_BORDER_ALL "  ") + rules.Rules[(size_t)m_TileRule].Name : std::string("(oddiy plitka)");
            if (ImGui::BeginCombo("##rule", preview.c_str()))
            {
                if (ImGui::Selectable("(oddiy plitka)", m_TileRule < 0))
                    m_TileRule = -1;
                for (int i = 0; i < (int)rules.Rules.size(); i++)
                {
                    const AutoTileRule& r = rules.Rules[(size_t)i];
                    std::string item = std::format(ICON_FA_BORDER_ALL "  {}  ({})##{}", r.Name, r.Mode == AutoTileMode::Blob8 ? "8 qo'shni, 47" : "4 qo'shni, 16", i);
                    if (ImGui::Selectable(item.c_str(), m_TileRule == i))
                    {
                        m_TileRule = i;
                        if (m_TileTool == TileTool::Picker) m_TileTool = TileTool::Brush;
                        m_TilePaint = true;
                    }
                }
                ImGui::EndCombo();
            }
            UI::PropertyLabel("");
            ImGui::BeginDisabled(c.TilesetPath.empty() || !m_SceneEditable);
            if (UI::ToolbarButton(ICON_FA_PLUS, "Yangi qoida (tanlangan plitkadan boshlab ketma-ket)"))
            {
                AutoTileRule rule = AutoTileRule::Sequential(rules.MakeUniqueName("Yer"), AutoTileMode::Edges4, std::max(0, GetSelectedTile()));
                rules.Rules.push_back(std::move(rule));
                m_TileRule = (int)rules.Rules.size() - 1;
                m_EditRule = true;
                m_EditRuleVariant = -1;
                rulesEdited = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(m_TileRule < 0);
            if (UI::ToolbarButton(ICON_FA_PEN, "Qoidani tahrirlash", m_EditRule && m_TileRule >= 0))
            {
                m_EditRule = !m_EditRule;
                m_EditRuleVariant = -1;
            }
            ImGui::SameLine(0, 2);
            if (UI::ToolbarButton(ICON_FA_TRASH, "Qoidani o'chirish (xaritadagi plitkalar qoladi)") && m_TileRule >= 0)
            {
                rules.Rules.erase(rules.Rules.begin() + m_TileRule);
                m_TileRule = -1;
                m_EditRule = false;
                rulesEdited = true;
            }
            ImGui::EndDisabled();
        }
        if (c.TilesetPath.empty())
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled("Avval tileset tanlang");
        }
        if (m_TileRule >= 0 && m_EditRule)
        {
            AutoTileRule& rule = rules.Rules[(size_t)m_TileRule];
            UI::PropertyLabel("Nomi");
            std::string name = rule.Name;
            if (ImGui::InputText("##ruleName", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) || ImGui::IsItemDeactivatedAfterEdit())
                if (!name.empty() && name != rule.Name && rules.Find(name) < 0)
                {
                    rule.Name = name;
                    rulesEdited = true;
                }
            const char* modes[] = { "4 qo'shni (16 plitka)", "8 qo'shni, blob (47 plitka)" };
            int mode = (int)rule.Mode;
            if (UI::PropertyCombo("Turi", mode, modes, 2))
            {
                // A new layout: the tiles from the first one on, in order.
                int first = rule.HasTiles() ? *std::min_element(rule.Tiles.begin(), rule.Tiles.end(), [](int a, int b) {
                    return (a < 0 ? INT32_MAX : a) < (b < 0 ? INT32_MAX : b); }) : 0;
                rule = AutoTileRule::Sequential(rule.Name, (AutoTileMode)mode, first);
                m_EditRuleVariant = -1;
                rulesEdited = true;
            }
            if (UI::PropertyBool("Chegaradan tashqarisi ham", rule.ConnectOutside))
                rulesEdited = true;
            if (ImGui::IsItemHovered())
                UI::Tooltip("Xarita chegarasi tashqarisi shu yer deb hisoblanadi (chetlarida qirra chizilmaydi)");
            UI::PropertyLabel("Ketma-ket", "Hamma variantlarga shu plitkadan boshlab ketma-ket plitkalarni beradi (tileset shu tartibda chizilgan bo'lsa)");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
            ImGui::DragInt("##seqFirst", &m_RuleFirstTile, 0.2f, 0, std::max(0, c.TilesetColumns * c.TilesetRows - 1));
            ImGui::SameLine();
            if (ImGui::Button("Qo'llash"))
            {
                rule = AutoTileRule::Sequential(rule.Name, rule.Mode, m_RuleFirstTile);
                rulesEdited = true;
            }

            // The variants: the neighbour pattern and its tile. Click one, then a tile in the palette.
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Variantlar");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Variantni bosing, keyin palitradan plitkani tanlang (keyingi variantga o'tadi). O'ng tugma - tozalash");
            ImGui::TableSetColumnIndex(1);
            if (!c.Tileset && !c.TilesetPath.empty())
                c.Tileset = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TilesetPath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            const float thumb = std::max(14.0f, ImGui::GetFontSize() * 1.1f);
            const float cellWidth = thumb * 2.0f + 6.0f;
            const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 4.0f) / (cellWidth + 4.0f)));
            const int cols = std::max(1, c.TilesetColumns), rows = std::max(1, c.TilesetRows);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (int v = 0; v < rule.GetVariantCount(); v++)
            {
                if (v % perRow) ImGui::SameLine(0, 4);
                ImGui::PushID(v);
                ImVec2 p = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::InvisibleButton("##variant", ImVec2(cellWidth, thumb + 4.0f));
                bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && rule.Tiles[(size_t)v] >= 0)
                {
                    rule.Tiles[(size_t)v] = -1;
                    rulesEdited = true;
                }
                if (clicked)
                    m_EditRuleVariant = m_EditRuleVariant == v ? -1 : v;
                ImU32 frame = m_EditRuleVariant == v ? UI::AccentU32() : hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg);
                draw->AddRectFilled(p, ImVec2(p.x + cellWidth, p.y + thumb + 4.0f), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
                draw->AddRect(p, ImVec2(p.x + cellWidth, p.y + thumb + 4.0f), frame, 3.0f, 0, m_EditRuleVariant == v ? 2.0f : 1.0f);
                DrawAutoTileMask(draw, ImVec2(p.x + 2.0f, p.y + 2.0f), thumb, rule.Mode, v);
                int tile = rule.Tiles[(size_t)v];
                ImVec2 t0(p.x + thumb + 4.0f, p.y + 2.0f), t1(t0.x + thumb, t0.y + thumb);
                if (tile >= 0 && c.Tileset)
                {
                    int tx = tile % cols, ty = tile / cols;
                    ImVec2 uv0((float)tx / cols, 1.0f - (float)ty / rows), uv1((float)(tx + 1) / cols, 1.0f - (float)(ty + 1) / rows);
                    draw->AddImage((ImTextureID)(uintptr_t)c.Tileset->GetRendererID(), t0, t1, uv0, uv1);
                }
                else
                    draw->AddText(ImVec2(t0.x + thumb * 0.3f, t0.y + thumb * 0.15f), ImGui::GetColorU32(ImGuiCol_TextDisabled), tile >= 0 ? "#" : "-");
                if (hovered)
                    UI::Tooltip(tile >= 0 ? std::format("Variant {}: plitka {}", v, tile).c_str() : std::format("Variant {}: plitka yo'q", v).c_str());
                ImGui::PopID();
            }
        }

        // ---- palette + the selected tile's properties (both kept in the tileset's .zmeta)
        const TilesetProperties* storedProperties = TilesetProperties::Get(c.TilesetPath);
        TilesetProperties properties = storedProperties ? *storedProperties : TilesetProperties();
        bool propertiesEdited = false;
        UI::PropertyHeader(ICON_FA_TABLE_CELLS "  Palitra", "G'ildirak - kattalashtirish / kichraytirish (sichqoncha ostida), o'rta yoki o'ng "
                                                          "tugma bilan sudrash - surish. Bosish - plitka, sudrash - bir nechta plitkali "
                                                          "mo'yqalam. Pastki chetini tortib balandligini o'zgartiring");
        // The palette takes the whole width of the inspector: the property grid pauses around it.
        UI::EndPropertyGrid();
        m_PaletteViews[0].Resizable = true;
        DrawTilePalette(c, m_PaletteViews[0], std::max(120.0f, ImGui::GetFontSize() * 16.0f), rules, rulesEdited, properties, propertiesEdited);
        UI::BeginPropertyGrid("##tilemapAfterPalette"); // never fails here: the window is drawing (its section's grid did not)
        DrawTileProperties(c, properties, propertiesEdited);

        if (rulesEdited && !c.TilesetPath.empty())
            SaveAutoTiles(c.TilesetPath, rules);
        if (propertiesEdited && !c.TilesetPath.empty())
            SaveTileProperties(c.TilesetPath, properties);
        return changed;
    }

    bool SceneHierarchyPanel::SaveTileProperties(const std::string& tilesetPath, const TilesetProperties& properties)
    {
        AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
        const AssetMetadata* meta = registry ? registry->Find(std::filesystem::path(tilesetPath)) : nullptr;
        if (!meta)
        {
            ZM_ERROR("Plitka xususiyatlari saqlanmadi: '{}' asset registry'da topilmadi", tilesetPath);
            return false;
        }
        AssetImportSettings import = meta->Import; // sprites, auto-tile rules and texture settings stay
        properties.WriteTo(import);
        if (!registry->SetImportSettings(meta->Handle, import))
        {
            ZM_ERROR("Plitka xususiyatlari saqlanmadi: {}", tilesetPath);
            return false;
        }
        return true;
    }

    void SceneHierarchyPanel::SetTileBrush(const TileBrush& brush)
    {
        if (brush.Width < 1 || brush.Height < 1 || (int)brush.Tiles.size() != brush.Width * brush.Height)
            return;
        m_TileBrush = brush;
        m_PaletteScrollRequest++;
    }

    // Tile `index` of a cols x rows tileset as UVs of its (bottom-up) texture.
    static void TileUV(int index, int cols, int rows, ImVec2& uv0, ImVec2& uv1)
    {
        int tx = index % cols, ty = index / cols;
        uv0 = ImVec2((float)tx / cols, 1.0f - (float)ty / rows);
        uv1 = ImVec2((float)(tx + 1) / cols, 1.0f - (float)(ty + 1) / rows);
    }

    static const char* TileCollisionLabel(TileCollision collision)
    {
        switch (collision)
        {
            case TileCollision::Solid: return "Qattiq";
            case TileCollision::None: return "Yo'q (o'tib ketiladi)";
            case TileCollision::OneWay: return "Bir tomonlama (platforma)";
        }
        return "";
    }

    void SceneHierarchyPanel::DrawTilePalette(TilemapComponent& c, TilePaletteView& view, float height, AutoTileSet& rules, bool& rulesEdited,
                                              TilesetProperties& props, bool& propsEdited)
    {
        if (!c.Tileset && !c.TilesetPath.empty())
            c.Tileset = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TilesetPath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
        if (!c.Tileset)
        {
            ImGui::TextDisabled("Tileset teksturasini tanlang");
            return;
        }
        const int cols = std::max(1, c.TilesetColumns), rows = std::max(1, c.TilesetRows), count = cols * rows;
        const float texW = (float)std::max(1u, c.Tileset->GetWidth()), texH = (float)std::max(1u, c.Tileset->GetHeight());
        const ImTextureID texture = (ImTextureID)(uintptr_t)c.Tileset->GetRendererID();
        AutoTileRule* activeRule = m_TileRule >= 0 && m_TileRule < (int)rules.Rules.size() ? &rules.Rules[(size_t)m_TileRule] : nullptr;
        const bool assigning = activeRule && m_EditRule && m_EditRuleVariant >= 0;
        const bool addingFrames = !assigning && m_AnimFramesTile >= 0;
        if (view.Tileset != c.TilesetPath)
        {
            if (!view.Tileset.empty())
                view.AutoFit = true; // another tileset: fitted afresh
            view.Tileset = c.TilesetPath;
            view.DragStart = view.DragEnd = -1;
        }
        ImGui::PushID(&view);

        // ---- toolbar: zoom, the property marks, the brush
        // Fitted: the inspector's palette to its width (its height is dragged by the user), the
        // window's to the whole area.
        const ImGuiStyle& style = ImGui::GetStyle();
        float fitZoom = (ImGui::GetContentRegionAvail().x - style.ScrollbarSize - style.WindowPadding.x * 2.0f - 2.0f) / texW;
        if (height <= 0.0f)
        {
            float areaH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * (assigning || addingFrames ? 2.0f : 1.0f) -
                          style.WindowPadding.y * 2.0f - 4.0f;
            fitZoom = std::min(fitZoom, areaH / texH);
        }
        fitZoom = std::clamp(fitZoom, 0.25f, 32.0f);
        if (view.AutoFit || view.Zoom <= 0.0f)
        {
            view.Zoom = fitZoom;
            view.AutoFit = true;
        }
        if (UI::ToolbarButton(ICON_FA_MAGNIFYING_GLASS_MINUS, "Kichraytirish (g'ildirak)"))
        {
            view.Zoom = std::max(0.25f, view.Zoom / 1.25f);
            view.AutoFit = false;
        }
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_MAGNIFYING_GLASS_PLUS, "Kattalashtirish (g'ildirak)"))
        {
            view.Zoom = std::min(32.0f, view.Zoom * 1.25f);
            view.AutoFit = false;
        }
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_EXPAND, height > 0.0f ? "Kenglikka moslash" : "Oynaga moslash", view.AutoFit))
            view.AutoFit = true;
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_TAGS, "Plitka xususiyatlari belgilari: qizil x - collision yo'q, pushti - bir tomonlama, "
                                            "uchburchak - animatsiya", m_ShowTileProperties))
            m_ShowTileProperties = !m_ShowTileProperties;
        if (view.Resizable && OnTilePaletteWindowRequested)
        {
            ImGui::SameLine(0, 2);
            if (UI::ToolbarButton(ICON_FA_UP_RIGHT_FROM_SQUARE, "Alohida oynada ochish (Oyna > Plitka palitrasi)"))
                OnTilePaletteWindowRequested();
        }
        ImGui::SameLine();
        if (m_TileBrush.IsSingle() || activeRule)
            ImGui::TextDisabled("%.0f%%", view.Zoom * 100.0f);
        else
            ImGui::TextDisabled("%.0f%%  " ICON_FA_PAINTBRUSH " %d x %d", view.Zoom * 100.0f, m_TileBrush.Width, m_TileBrush.Height);
        if (assigning)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_HAND_POINTER "  Variant %d uchun plitkani bosing", m_EditRuleVariant);
        else if (addingFrames)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_HAND_POINTER "  Plitka %d animatsiyasiga kadr: plitkani bosing", m_AnimFramesTile);

        // ---- the scrolling area
        ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | (view.Resizable ? ImGuiChildFlags_ResizeY : 0);
        float areaHeight = height > 0.0f ? height : 0.0f; // the window: the rest of it
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 26, 30, 255));
        ImGui::BeginChild("##paletteArea", ImVec2(-FLT_MIN, areaHeight), childFlags, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PopStyleColor();
        ImGuiIO& io = ImGui::GetIO();
        // Zoom around the cursor before the tiles are laid out (the content takes the new size at once).
        if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f)
        {
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImVec2 local(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
            float old = view.Zoom;
            view.Zoom = std::clamp(view.Zoom * std::pow(1.2f, io.MouseWheel), 0.25f, 32.0f);
            view.AutoFit = false;
            float k = view.Zoom / old;
            ImGui::SetScrollX(ImGui::GetScrollX() + local.x * (k - 1.0f));
            ImGui::SetScrollY(ImGui::GetScrollY() + local.y * (k - 1.0f));
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(texW * view.Zoom, texH * view.Zoom);
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        ImGui::InvisibleButton("##tiles", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // the wheel zooms here, it does not scroll the inspector
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)))
        {
            ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        }
        const float cw = size.x / cols, ch = size.y / rows;
        auto tileAt = [&](ImVec2 p, bool clampToSet) {
            int tx = (int)std::floor((p.x - origin.x) / cw), ty = (int)std::floor((p.y - origin.y) / ch);
            if (clampToSet)
            {
                tx = std::clamp(tx, 0, cols - 1);
                ty = std::clamp(ty, 0, rows - 1);
            }
            else if (tx < 0 || ty < 0 || tx >= cols || ty >= rows)
                return -1;
            return ty * cols + tx;
        };
        auto tileMin = [&](int index) { return ImVec2(origin.x + (index % cols) * cw, origin.y + (index / cols) * ch); };
        // The brush was picked elsewhere (the picker, automation): bring it into view.
        if (view.ScrolledTo != m_PaletteScrollRequest)
        {
            view.ScrolledTo = m_PaletteScrollRequest;
            int topLeft = m_TileBrush.GetTopLeft();
            if (topLeft >= 0 && topLeft < count && !activeRule)
            {
                float localY = (topLeft / cols) * ch, localX = (topLeft % cols) * cw;
                float viewH = ImGui::GetWindowHeight(), viewW = ImGui::GetWindowWidth();
                if (localY < ImGui::GetScrollY() || localY + ch * m_TileBrush.Height > ImGui::GetScrollY() + viewH)
                    ImGui::SetScrollY(std::max(0.0f, localY + ch * m_TileBrush.Height * 0.5f - viewH * 0.5f));
                if (localX < ImGui::GetScrollX() || localX + cw * m_TileBrush.Width > ImGui::GetScrollX() + viewW)
                    ImGui::SetScrollX(std::max(0.0f, localX + cw * m_TileBrush.Width * 0.5f - viewW * 0.5f));
            }
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        {
            // A checkerboard under the transparent parts (only where it can be seen).
            ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
            ImVec2 a(std::max(origin.x, clipMin.x), std::max(origin.y, clipMin.y)), b(std::min(end.x, clipMax.x), std::min(end.y, clipMax.y));
            if (b.x > a.x && b.y > a.y)
            {
                dl->AddRectFilled(a, b, IM_COL32(56, 58, 64, 255));
                const float cell = 8.0f;
                int x0 = (int)std::floor((a.x - origin.x) / cell), y0 = (int)std::floor((a.y - origin.y) / cell);
                for (int y = y0; origin.y + y * cell < b.y; y++)
                    for (int x = x0 + ((x0 + y) & 1); origin.x + x * cell < b.x; x += 2)
                    {
                        ImVec2 c0(std::max(a.x, origin.x + x * cell), std::max(a.y, origin.y + y * cell));
                        ImVec2 c1(std::min(b.x, origin.x + (x + 1) * cell), std::min(b.y, origin.y + (y + 1) * cell));
                        if (c1.x > c0.x && c1.y > c0.y)
                            dl->AddRectFilled(c0, c1, IM_COL32(76, 78, 86, 255));
                    }
            }
        }
        dl->AddImage(texture, origin, end, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        if (cw >= 10.0f && ch >= 10.0f)
        {
            const ImU32 line = IM_COL32(0, 0, 0, 70);
            for (int x = 1; x < cols; x++)
                dl->AddLine(ImVec2(origin.x + x * cw, origin.y), ImVec2(origin.x + x * cw, end.y), line);
            for (int y = 1; y < rows; y++)
                dl->AddLine(ImVec2(origin.x, origin.y + y * ch), ImVec2(end.x, origin.y + y * ch), line);
        }
        // Collision / animation marks.
        if (m_ShowTileProperties && !props.IsEmpty())
        {
            const ImU32 red = IM_COL32(255, 90, 90, 235), oneWay = IM_COL32(242, 102, 230, 245), play = IM_COL32(120, 210, 255, 240);
            const float m = std::max(3.0f, std::min(cw, ch) * 0.22f);
            for (int t : props.GetTilesWith(TileCollision::None))
            {
                if (t >= count) break;
                ImVec2 a = tileMin(t);
                ImVec2 p0(a.x + cw - m - 2.0f, a.y + 2.0f), p1(a.x + cw - 2.0f, a.y + m + 2.0f);
                dl->AddLine(p0, p1, red, 2.0f);
                dl->AddLine(ImVec2(p1.x, p0.y), ImVec2(p0.x, p1.y), red, 2.0f);
            }
            for (int t : props.GetTilesWith(TileCollision::OneWay))
            {
                if (t >= count) break;
                ImVec2 a = tileMin(t);
                dl->AddRectFilled(a, ImVec2(a.x + cw, a.y + std::max(2.0f, ch * 0.08f)), oneWay);
                float cx = a.x + cw * 0.5f, top = a.y + std::max(3.0f, ch * 0.12f);
                dl->AddTriangleFilled(ImVec2(cx, top), ImVec2(cx - m * 0.6f, top + m * 0.7f), ImVec2(cx + m * 0.6f, top + m * 0.7f), oneWay);
            }
            for (const TileAnimation& anim : props.GetAnimations())
            {
                if (anim.Tile >= count) break;
                ImVec2 a = tileMin(anim.Tile);
                ImVec2 b(a.x + cw - 2.0f, a.y + ch - 2.0f);
                dl->AddTriangleFilled(ImVec2(b.x - m, b.y - m), ImVec2(b.x, b.y - m * 0.5f), ImVec2(b.x - m, b.y), play);
            }
        }
        // The selected rule's tiles; the edited animation's frames (numbered); the brush.
        if (activeRule)
        {
            for (int t = 0; t < count; t++)
                if (activeRule->Contains(t))
                {
                    ImVec2 a = tileMin(t);
                    dl->AddRect(a, ImVec2(a.x + cw, a.y + ch), UI::AccentU32(0.9f), 0.0f, 0, 2.0f);
                }
        }
        else
        {
            int topLeft = m_TileBrush.GetTopLeft();
            if (topLeft >= 0 && topLeft < count)
            {
                ImVec2 a = tileMin(topLeft);
                ImVec2 b(a.x + cw * m_TileBrush.Width, a.y + ch * m_TileBrush.Height);
                dl->AddRectFilled(a, b, UI::AccentU32(0.16f));
                dl->AddRect(a, b, UI::AccentU32(), 0.0f, 0, 2.0f);
            }
        }
        const int framesOf = addingFrames ? m_AnimFramesTile : m_TilePropertiesOpen ? m_TileBrush.GetTopLeft() : -1;
        if (const TileAnimation* anim = framesOf >= 0 ? props.FindAnimation(framesOf) : nullptr)
            for (int k = 0; k < (int)anim->Frames.size(); k++)
            {
                int f = anim->Frames[(size_t)k];
                if (f < 0 || f >= count) continue;
                ImVec2 a = tileMin(f);
                std::string label = std::to_string(k + 1);
                ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                ImVec2 p(a.x + 2.0f, a.y + 2.0f);
                dl->AddRectFilled(p, ImVec2(p.x + ts.x + 4.0f, p.y + ts.y), IM_COL32(20, 90, 140, 220), 2.0f);
                dl->AddText(ImVec2(p.x + 2.0f, p.y), IM_COL32(255, 255, 255, 255), label.c_str());
            }
        if (view.DragStart >= 0)
        {
            TileBrush dragged = TileBrush::FromTileset(cols, rows, view.DragStart, view.DragEnd);
            ImVec2 a = tileMin(dragged.GetTopLeft());
            ImVec2 b(a.x + cw * dragged.Width, a.y + ch * dragged.Height);
            dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 40));
            dl->AddRect(a, b, IM_COL32(255, 255, 255, 230), 0.0f, 0, 2.0f);
        }

        // ---- input
        const int hoveredTile = hovered ? tileAt(io.MousePos, false) : -1;
        if (hoveredTile >= 0)
        {
            ImVec2 a = tileMin(hoveredTile);
            dl->AddRect(a, ImVec2(a.x + cw, a.y + ch), IM_COL32(255, 255, 255, 190), 0.0f, 0, 1.5f);
            if (view.DragStart < 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                std::string tip = std::format("Plitka {}", hoveredTile);
                TileCollision collision = props.GetCollision(hoveredTile);
                if (collision != TileCollision::Solid)
                    tip += std::format("\nCollision: {}", TileCollisionLabel(collision));
                if (const TileAnimation* anim = props.FindAnimation(hoveredTile))
                    tip += std::format("\nAnimatsiya: {} kadr, {:.1f} FPS", anim->Frames.size(), anim->FPS);
                if (int rule = rules.FindRuleOf(hoveredTile); rule >= 0)
                    tip += std::format("\nAuto-tile: {}", rules.Rules[(size_t)rule].Name);
                UI::Tooltip(tip.c_str());
            }
        }
        if (hoveredTile >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (assigning)
            {
                activeRule->Tiles[(size_t)m_EditRuleVariant] = hoveredTile;
                m_EditRuleVariant = m_EditRuleVariant + 1 < activeRule->GetVariantCount() ? m_EditRuleVariant + 1 : -1;
                rulesEdited = true;
            }
            else if (addingFrames)
            {
                const TileAnimation* existing = props.FindAnimation(m_AnimFramesTile);
                TileAnimation anim = existing ? *existing : TileAnimation{ m_AnimFramesTile, {}, 6.0f };
                if ((int)anim.Frames.size() < TileAnimation::MaxFrames)
                {
                    anim.Frames.push_back(hoveredTile);
                    props.SetAnimation(std::move(anim));
                    propsEdited = true;
                }
            }
            else
                view.DragStart = view.DragEnd = hoveredTile;
        }
        if (view.DragStart >= 0)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                view.DragEnd = tileAt(io.MousePos, true);
            else
            {
                // Released: the tile (or the rectangle of tiles) is the brush now (in view already).
                SetTileBrush(TileBrush::FromTileset(cols, rows, view.DragStart, view.DragEnd));
                view.ScrolledTo = m_PaletteScrollRequest;
                view.DragStart = view.DragEnd = -1;
                m_TileRule = -1;
                if (m_TileTool == TileTool::Eraser || m_TileTool == TileTool::Picker)
                    m_TileTool = TileTool::Brush;
                m_TilePaint = true;
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
    }

    void SceneHierarchyPanel::DrawTileProperties(TilemapComponent& c, TilesetProperties& props, bool& propsEdited)
    {
        const int cols = std::max(1, c.TilesetColumns), rows = std::max(1, c.TilesetRows), count = cols * rows;
        const int tile = m_TileBrush.GetTopLeft();
        std::vector<int> selected;
        for (int t : m_TileBrush.Tiles)
            if (t >= 0 && t < count)
                selected.push_back(t);
        UI::PropertyHeader(ICON_FA_TAGS "  Plitka xususiyatlari", "Tileset'ning har bir plitkasi uchun (uning .zmeta faylida, shu tileset'li hamma "
                                                                "tilemap'larda): collision turi va animatsiya");
        UI::PropertyLabel("Ko'rsatish");
        ImGui::Checkbox("##tileProps", &m_TilePropertiesOpen);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Tanlangan plitkaning collision va animatsiya sozlamalari");
        if (!m_TilePropertiesOpen)
        {
            m_AnimFramesTile = -1;
            return;
        }
        if (c.TilesetPath.empty() || tile < 0 || tile >= count || selected.empty())
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled("Palitradan plitka tanlang");
            m_AnimFramesTile = -1;
            return;
        }
        if (m_AnimFramesTile >= 0 && m_AnimFramesTile != tile)
            m_AnimFramesTile = -1; // another tile was picked
        const ImTextureID texture = c.Tileset ? (ImTextureID)(uintptr_t)c.Tileset->GetRendererID() : (ImTextureID)0;
        const float thumb = std::max(18.0f, ImGui::GetFrameHeight());
        auto image = [&](int t, float size) {
            ImVec2 uv0, uv1;
            TileUV(t, cols, rows, uv0, uv1);
            if (texture)
                ImGui::Image(texture, ImVec2(size, size), uv0, uv1);
            else
                ImGui::Dummy(ImVec2(size, size));
        };
        UI::PropertyLabel("Plitka");
        image(tile, thumb);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (selected.size() == 1)
            ImGui::Text("%d", tile);
        else
            ImGui::Text("%d ta (%d x %d)", (int)selected.size(), m_TileBrush.Width, m_TileBrush.Height);

        // ---- collision (every selected tile)
        bool mixed = false;
        TileCollision collision = props.GetCollision(tile);
        for (int t : selected)
            mixed |= props.GetCollision(t) != collision;
        UI::PropertyLabel("Collision", "Collision yoqilgan qatlamlarda: qattiq; yo'q - o't, gul, bezaklar (o'tib ketiladi); bir tomonlama - "
                                       "pastdan sakrab o'tiladi, ustiga qo'niladi (skriptdan DropThroughPlatforms bilan tushib ketiladi)");
        if (ImGui::BeginCombo("##tileCollision", mixed ? "(turlicha)" : TileCollisionLabel(collision)))
        {
            for (TileCollision option : { TileCollision::Solid, TileCollision::None, TileCollision::OneWay })
                if (ImGui::Selectable(TileCollisionLabel(option), !mixed && option == collision))
                {
                    for (int t : selected)
                        props.SetCollision(t, option);
                    propsEdited = true;
                }
            ImGui::EndCombo();
        }

        // ---- animation (of the top-left tile)
        const TileAnimation* anim = props.FindAnimation(tile);
        UI::PropertyLabel("Animatsiya", "Xaritada shu plitka turgan joyda kadrlar navbat bilan ko'rsatiladi (editorda ham). "
                                        "Fizika shu plitkaning collision'idan foydalanadi");
        bool animated = anim != nullptr;
        if (ImGui::Checkbox("##tileAnimated", &animated))
        {
            if (animated)
                props.SetAnimation({ tile, selected.size() > 1 ? selected : std::vector<int>{ tile }, 6.0f });
            else
            {
                props.RemoveAnimation(tile);
                m_AnimFramesTile = -1;
            }
            propsEdited = true;
            anim = props.FindAnimation(tile);
        }
        if (!anim)
            return;
        // A live preview of the animation.
        ImGui::SameLine();
        image(anim->GetFrame(ImGui::GetTime()), thumb);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu kadr, %.2f s", anim->Frames.size(), anim->GetDuration());

        UI::PropertyLabel("FPS", "Sekundiga necha kadr (0 - birinchi kadrda turadi)");
        float fps = anim->FPS;
        if (ImGui::DragFloat("##tileFps", &fps, 0.1f, 0.0f, 60.0f, "%.1f"))
        {
            TileAnimation edited = *anim;
            edited.FPS = std::clamp(fps, 0.0f, 60.0f);
            props.SetAnimation(std::move(edited));
            propsEdited = true;
            anim = props.FindAnimation(tile);
        }
        UI::PropertyLabel("Kadrlar", "Navbat bilan ko'rsatiladigan plitkalar. O'ng tugma - kadrni olib tashlash");
        {
            const float small = std::max(16.0f, ImGui::GetFontSize() * 1.3f);
            const float perRow = std::max(1.0f, std::floor((ImGui::GetContentRegionAvail().x + 3.0f) / (small + 3.0f + ImGui::GetStyle().FramePadding.x * 2.0f)));
            int remove = -1;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
            for (int k = 0; k < (int)anim->Frames.size(); k++)
            {
                if (k % (int)perRow) ImGui::SameLine();
                ImGui::PushID(k);
                ImVec2 uv0, uv1;
                TileUV(anim->Frames[(size_t)k], cols, rows, uv0, uv1);
                if (texture)
                    ImGui::ImageButton("##frame", texture, ImVec2(small, small), uv0, uv1);
                else
                    ImGui::Button("##frame", ImVec2(small, small));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    remove = k;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    UI::Tooltip(std::format("Kadr {}: plitka {} (o'ng tugma - olib tashlash)", k + 1, anim->Frames[(size_t)k]).c_str());
                ImGui::PopID();
            }
            ImGui::PopStyleVar(2);
            if (remove >= 0)
            {
                TileAnimation edited = *anim;
                edited.Frames.erase(edited.Frames.begin() + remove);
                if (edited.Frames.empty())
                    edited.Frames.push_back(tile);
                props.SetAnimation(std::move(edited));
                propsEdited = true;
                anim = props.FindAnimation(tile);
            }
        }
        UI::PropertyLabel("");
        if (UI::ToolbarButton(ICON_FA_PLUS, "Palitradan kadr qo'shish: endi bosilgan plitkalar oxiriga qo'shiladi (yana bosing - tugatish)",
                              m_AnimFramesTile == tile))
            m_AnimFramesTile = m_AnimFramesTile == tile ? -1 : tile;
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(selected.size() < 2);
        if (UI::ToolbarButton(ICON_FA_TABLE_CELLS, "Kadrlar = tanlangan plitkalar (palitrada bir nechtasini sudrab tanlang)") && anim)
        {
            TileAnimation edited = *anim;
            edited.Frames = selected;
            props.SetAnimation(std::move(edited));
            propsEdited = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        if (UI::ToolbarButton(ICON_FA_ROTATE_LEFT, "Kadrlarni tozalash (faqat plitkaning o'zi qoladi)") && anim)
        {
            TileAnimation edited = *anim;
            edited.Frames = { tile };
            props.SetAnimation(std::move(edited));
            propsEdited = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d ta animatsiya", (int)props.GetAnimations().size());
    }

    void SceneHierarchyPanel::DrawTilePaletteWindow(bool* open)
    {
        if (!open || !*open)
            return;
        ImGui::SetNextWindowSize(ImVec2(440, 560), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_TABLE_CELLS "  Plitka palitrasi###TilePalette", open))
        {
            ImGui::End();
            return;
        }
        Entity e = GetSelectedEntity();
        TilemapComponent* c = e && e.IsValid() ? e.TryGetComponent<TilemapComponent>() : nullptr;
        if (!c)
        {
            ImGui::TextDisabled("Ierarxiyada tilemap tanlang");
            ImGui::End();
            return;
        }
        c->EnsureSize();
        const int layer = std::clamp(m_TileLayer, 0, c->GetLayerCount() - 1);
        ImGui::TextUnformatted(ICON_FA_TABLE_CELLS);
        ImGui::SameLine();
        ImGui::Text("%s", e.GetName().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  |  qatlam: %s  |  %s", c->TilesetPath.empty() ? "(tileset yo'q)" : c->TilesetPath.c_str(),
                            c->Layers[(size_t)layer].Name.c_str(), m_TilePaint ? TileToolName(m_TileTool) : "bo'yash o'chiq");
        const AutoTileSet* storedRules = AutoTileSet::Get(c->TilesetPath);
        AutoTileSet rules = storedRules ? *storedRules : AutoTileSet();
        const TilesetProperties* storedProperties = TilesetProperties::Get(c->TilesetPath);
        TilesetProperties properties = storedProperties ? *storedProperties : TilesetProperties();
        bool rulesEdited = false, propertiesEdited = false;
        if (m_TileRule >= (int)rules.Rules.size())
            m_TileRule = -1;
        m_PaletteViews[1].Resizable = false;
        DrawTilePalette(*c, m_PaletteViews[1], 0.0f, rules, rulesEdited, properties, propertiesEdited);
        if (rulesEdited && !c->TilesetPath.empty())
            SaveAutoTiles(c->TilesetPath, rules);
        if (propertiesEdited && !c->TilesetPath.empty())
            SaveTileProperties(c->TilesetPath, properties);
        ImGui::End();
    }

    bool SceneHierarchyPanel::DrawJoint2DCommon(Entity entity, Joint2DComponentBase& c, Joint2DType type)
    {
        bool changed = DrawEntityRefField("Ulangan jism", c.ConnectedBody,
                                          "Boshqa obyektning Rigidbody 2D yoki Tilemap'i (ierarxiyadan tortib tashlang). "
                                          "Bo'sh - jism dunyodagi qo'zg'almas nuqtaga bog'lanadi");
        // What keeps the joint from being made (it waits for it while the game runs).
        const char* problem = nullptr;
        Entity other = c.ConnectedBody && m_Context ? m_Context->GetEntityByUUID(c.ConnectedBody.ID) : Entity();
        if (!entity.HasComponent<Rigidbody2DComponent>())
            problem = "Rigidbody 2D kerak: busiz joint yaratilmaydi";
        else if (c.ConnectedBody && !other)
            problem = "Ulangan obyekt sahnada yo'q (o'chirilgan, yoki prefab boshqa sahnada yaratilgan): joint yaratilmaydi";
        else if (other && other == entity)
            problem = "Obyekt o'ziga ulana olmaydi";
        else if (other && !other.HasComponent<Rigidbody2DComponent>() && !other.HasComponent<TilemapComponent>())
            problem = "Ulangan obyektda Rigidbody 2D (yoki Tilemap) yo'q";
        if (problem)
        {
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", problem);
            ImGui::PopTextWrapPos();
        }
        InactiveComponentNote(entity, "Obyekt nofaol: joint yo'q (faollashganda qayta yaratiladi)");

        // The anchors (and the axis / limits) dragged in the viewport.
        const bool editing = m_JointAnchorEdit == (int)type;
        UI::PropertyLabel("Viewportda", "Langarlarni sahnada sudrab qo'yish: langar (doira), ulangan langar (romb), sirpanish / osma o'qi "
                                        "va chegaralari. Shift - mayda qadam (0.1 birlik / 5\xc2\xb0). Esc yoki tugma qayta bosilsa - tugaydi");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (editing)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
        if (ImGui::Button(editing ? ICON_FA_CHECK "  Tahrirlashni tugatish" : ICON_FA_ANCHOR "  Langarlarni tahrirlash", ImVec2(-FLT_MIN, 0)))
            SetJointAnchorEdit(editing ? -1 : (int)type);
        if (editing)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();

        changed |= UI::PropertyFloat2("Langar", c.Anchor, 0.01f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Shu jismdagi bog'lanish nuqtasi - jismning o'z koordinatalarida (masshtabsiz, collider siljishi kabi)");
        changed |= UI::PropertyBool("Avto ulangan langar", c.AutoConfigureConnectedAnchor);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Ulangan langar joint yaratilganda \"Langar\" turgan nuqtaga qo'yiladi (o'yin davomida shu yerda ko'rinadi)");
        ImGui::BeginDisabled(c.AutoConfigureConnectedAnchor);
        changed |= UI::PropertyFloat2(c.ConnectedBody ? "Ulangan langar" : "Dunyodagi nuqta", c.ConnectedAnchor, 0.01f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(c.ConnectedBody ? "Ulangan jismdagi nuqta, uning o'z koordinatalarida"
                                        : "Ulangan jism yo'q: joint dunyodagi shu nuqtaga bog'lanadi");
        ImGui::EndDisabled();
        if (!c.AutoConfigureConnectedAnchor && m_SceneEditable && m_Context && (!other || other != entity))
        {
            UI::PropertyLabel("");
            if (ImGui::SmallButton(ICON_FA_LOCATION_CROSSHAIRS "  Langar turgan joyga"))
            {
                // Where this body's anchor is now, in the connected body's frame (or the world).
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(m_Context->GetWorldTransform(entity), p, r, sc);
                const float cs = std::cos(r.z), sn = std::sin(r.z);
                glm::vec2 world(p.x + cs * c.Anchor.x - sn * c.Anchor.y, p.y + sn * c.Anchor.x + cs * c.Anchor.y);
                if (other)
                {
                    glm::vec3 op, orot, osc;
                    Math::DecomposeTransform(m_Context->GetWorldTransform(other), op, orot, osc);
                    const glm::vec2 d = world - glm::vec2(op);
                    const float oc = std::cos(orot.z), os = std::sin(orot.z);
                    world = glm::vec2(oc * d.x + os * d.y, -os * d.x + oc * d.y);
                }
                c.ConnectedAnchor = world;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ulangan langarni hozir shu jismning langari turgan nuqtaga qo'yadi (ulangan jism koordinatalarida)");
        }
        changed |= UI::PropertyBool("Bir-biri bilan to'qnashsin", c.CollideConnected);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'chiq bo'lsa ulangan ikki jism bir-biridan o'tib ketadi (ragdoll bo'g'imlari uchun qulay)");

        UI::PropertyHeader("Uzilish", "Joint tortish kuchi yoki momenti shundan oshsa uziladi: yo'qoladi va skriptga "
                                      "OnJointBreak keladi. 0 - hech qachon uzilmaydi");
        UI::PropertyLabel("Kuch chegarasi", "Nyuton (N). 0 - cheksiz");
        if (ImGui::DragFloat("##breakForce", &c.BreakForce, 1.0f, 0.0f, 1e7f, c.BreakForce > 0.0f ? "%.1f N" : "cheksiz"))
        {
            c.BreakForce = std::max(0.0f, c.BreakForce);
            changed = true;
        }
        UI::PropertyLabel("Moment chegarasi", "N*m. 0 - cheksiz");
        if (ImGui::DragFloat("##breakTorque", &c.BreakTorque, 1.0f, 0.0f, 1e7f, c.BreakTorque > 0.0f ? "%.1f N*m" : "cheksiz"))
        {
            c.BreakTorque = std::max(0.0f, c.BreakTorque);
            changed = true;
        }
        return changed;
    }

    void SceneHierarchyPanel::DrawJoint2DStatus(const Joint2DComponentBase& c, const char* readout)
    {
        if (!m_Context || (!m_Context->IsRunning() && !m_Context->IsSimulating()))
            return;
        UI::PropertyHeader("Holati", "O'yin davomidagi qiymatlar (har fizika qadamidan keyin)");
        UI::PropertyLabel("Joint");
        if (c.Broken)
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_LINK_SLASH "  Uzilgan (%.1f N, %.1f N*m)", c.ReactionForce, c.ReactionTorque);
        else if (c.RuntimeJoint)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_LINK "  Ishlayapti");
        else
            ImGui::TextDisabled(ICON_FA_LINK "  Kutmoqda (jismlar yo'q)");
        if (!c.RuntimeJoint)
            return;
        UI::PropertyLabel("Kuch / moment");
        ImGui::TextDisabled("%.1f N   %.1f N*m", c.ReactionForce, c.ReactionTorque);
        // One row per line ("name: value").
        for (std::string_view rest = readout ? readout : ""; !rest.empty();)
        {
            size_t end = rest.find('\n');
            std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            UI::PropertyLabel(std::string(line.substr(0, colon)).c_str());
            ImGui::TextDisabled("%s", std::string(line.substr(colon + 1 + (colon + 1 < line.size() && line[colon + 1] == ' ' ? 1 : 0))).c_str());
        }
    }

    bool SceneHierarchyPanel::DrawJoint3DCommon(Entity entity, Joint3DComponentBase& c, Joint3DType type)
    {
        bool changed = DrawEntityRefField("Ulangan jism", c.ConnectedBody,
                                          "Boshqa obyektning Rigidbody 3D'si (ierarxiyadan tortib tashlang). "
                                          "Bo'sh - jism dunyodagi qo'zg'almas nuqtaga bog'lanadi");
        // What keeps the joint from being made (it waits for it while the game runs).
        const char* problem = nullptr;
        Entity other = c.ConnectedBody && m_Context ? m_Context->GetEntityByUUID(c.ConnectedBody.ID) : Entity();
        if (!entity.HasComponent<Rigidbody3DComponent>())
            problem = "Rigidbody 3D kerak: busiz joint yaratilmaydi";
        else if (c.ConnectedBody && !other)
            problem = "Ulangan obyekt sahnada yo'q (o'chirilgan, yoki prefab boshqa sahnada yaratilgan): joint yaratilmaydi";
        else if (other && other == entity)
            problem = "Obyekt o'ziga ulana olmaydi";
        else if (other && !other.HasComponent<Rigidbody3DComponent>())
            problem = "Ulangan obyektda Rigidbody 3D yo'q";
        if (problem)
        {
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", problem);
            ImGui::PopTextWrapPos();
        }
        InactiveComponentNote(entity, "Obyekt nofaol: joint yo'q (faollashganda qayta yaratiladi)");

        // The anchors (and the axis / limits) dragged in the viewport.
        const bool editing = m_Joint3DAnchorEdit == (int)type;
        UI::PropertyLabel("Viewportda", "Langarlarni sahnada sudrab qo'yish: langar (doira), ulangan langar (romb), o'q (kvadrat), "
                                        "chegaralar (+/-). Nuqta ekran tekisligida suriladi; tanlangan nuqtada ko'chirish gizmosi "
                                        "ham chiqadi. Shift - mayda qadam (0.1 birlik / 5\xc2\xb0, o'q - asosiy o'qlarga). Esc yoki "
                                        "tugma qayta bosilsa - tugaydi");
        ImGui::BeginDisabled(!m_SceneEditable);
        if (editing)
            ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
        if (ImGui::Button(editing ? ICON_FA_CHECK "  Tahrirlashni tugatish" : ICON_FA_ANCHOR "  Langarlarni tahrirlash", ImVec2(-FLT_MIN, 0)))
            SetJoint3DAnchorEdit(editing ? -1 : (int)type);
        if (editing)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();

        changed |= UI::PropertyFloat3("Langar", c.Anchor, 0.01f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Shu jismdagi bog'lanish nuqtasi - obyektning o'z koordinatalarida (masshtab bilan, 3D collider siljishi kabi)");
        changed |= UI::PropertyBool("Avto ulangan langar", c.AutoConfigureConnectedAnchor);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Ulangan langar joint yaratilganda \"Langar\" turgan nuqtaga qo'yiladi (o'yin davomida shu yerda ko'rinadi)");
        ImGui::BeginDisabled(c.AutoConfigureConnectedAnchor);
        changed |= UI::PropertyFloat3(c.ConnectedBody ? "Ulangan langar" : "Dunyodagi nuqta", c.ConnectedAnchor, 0.01f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            UI::Tooltip(c.ConnectedBody ? "Ulangan jismdagi nuqta, uning o'z koordinatalarida"
                                        : "Ulangan jism yo'q: joint dunyodagi shu nuqtaga bog'lanadi");
        ImGui::EndDisabled();
        if (!c.AutoConfigureConnectedAnchor && m_SceneEditable && m_Context && (!other || other != entity))
        {
            UI::PropertyLabel("");
            if (ImGui::SmallButton(ICON_FA_LOCATION_CROSSHAIRS "  Langar turgan joyga"))
            {
                // Where this body's anchor is now, in the connected body's frame (or the world).
                glm::vec3 world = glm::vec3(m_Context->GetWorldTransform(entity) * glm::vec4(c.Anchor, 1.0f));
                if (other)
                    world = glm::vec3(glm::inverse(m_Context->GetWorldTransform(other)) * glm::vec4(world, 1.0f));
                c.ConnectedAnchor = world;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Ulangan langarni hozir shu jismning langari turgan nuqtaga qo'yadi (ulangan jism koordinatalarida)");
        }
        changed |= UI::PropertyBool("Bir-biri bilan to'qnashsin", c.CollideConnected);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("O'chiq bo'lsa ulangan ikki jism bir-biridan o'tib ketadi (ragdoll bo'g'imlari, zanjir halqalari uchun qulay)");

        UI::PropertyHeader("Uzilish", "Joint tortish kuchi yoki momenti shundan oshsa uziladi: yo'qoladi va skriptga "
                                      "OnJointBreak3D keladi. 0 - hech qachon uzilmaydi");
        UI::PropertyLabel("Kuch chegarasi", "Nyuton (N). 0 - cheksiz");
        if (ImGui::DragFloat("##breakForce", &c.BreakForce, 1.0f, 0.0f, 1e7f, c.BreakForce > 0.0f ? "%.1f N" : "cheksiz"))
        {
            c.BreakForce = std::max(0.0f, c.BreakForce);
            changed = true;
        }
        UI::PropertyLabel("Moment chegarasi", "N*m. 0 - cheksiz");
        if (ImGui::DragFloat("##breakTorque", &c.BreakTorque, 1.0f, 0.0f, 1e7f, c.BreakTorque > 0.0f ? "%.1f N*m" : "cheksiz"))
        {
            c.BreakTorque = std::max(0.0f, c.BreakTorque);
            changed = true;
        }
        return changed;
    }

    void SceneHierarchyPanel::DrawJoint3DStatus(const Joint3DComponentBase& c, const char* readout)
    {
        if (!m_Context || (!m_Context->IsRunning() && !m_Context->IsSimulating()))
            return;
        UI::PropertyHeader("Holati", "O'yin davomidagi qiymatlar (har fizika qadamidan keyin)");
        UI::PropertyLabel("Joint");
        if (c.Broken)
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_LINK_SLASH "  Uzilgan (%.1f N, %.1f N*m)", c.ReactionForce, c.ReactionTorque);
        else if (c.RuntimeJoint)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_LINK "  Ishlayapti");
        else
            ImGui::TextDisabled(ICON_FA_LINK "  Kutmoqda (jismlar yo'q)");
        if (!c.RuntimeJoint)
            return;
        UI::PropertyLabel("Kuch / moment");
        ImGui::TextDisabled("%.1f N   %.1f N*m", c.ReactionForce, c.ReactionTorque);
        for (std::string_view rest = readout ? readout : ""; !rest.empty();)
        {
            size_t end = rest.find('\n');
            std::string_view line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            UI::PropertyLabel(std::string(line.substr(0, colon)).c_str());
            ImGui::TextDisabled("%s", std::string(line.substr(colon + 1 + (colon + 1 < line.size() && line[colon + 1] == ' ' ? 1 : 0))).c_str());
        }
    }

    bool SceneHierarchyPanel::DrawConvexHullCollider(Entity entity, ConvexHullCollider3DComponent& c)
    {
        bool changed = UI::PropertyAssetPath("Model fayli", c.ModelPath, ".gltf;.glb;.obj", ICON_FA_CUBES);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Qobiq shu modeldan quriladi. Bo'sh - obyektning Mesh Renderer'idagi model yoki primitiv");
        UI::PropertyLabel("Maks. burchaklar", "Qobiq ko'pi bilan shuncha burchakka ega bo'ladi (4..256). Kam - tezroq, lekin qobiq "
                                              "meshdan biroz kichikroq: eng chetdagi nuqtalar olinadi");
        int maxVertices = c.MaxVertices;
        if (ImGui::SliderInt("##maxVertices", &maxVertices, CollisionShapes3D::MinHullVertices, CollisionShapes3D::MaxHullVertices))
        {
            c.MaxVertices = std::clamp(maxVertices, CollisionShapes3D::MinHullVertices, CollisionShapes3D::MaxHullVertices);
            changed = true;
        }
        // What the hull came out as (cached per model: cheap to ask every frame).
        std::string error;
        Ref<const ConvexHull3D> hull = Scene::GetConvexHullCollider(entity, &error);
        UI::PropertyLabel("Qobiq", "Fizik jism oladigan shakl: viewport'da collider chiziqlari bilan ko'rinadi");
        if (hull)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled(ICON_FA_SHAPES "  %zu burchak, %zu yuz (%zu nuqtadan)%s", hull->Points.size(), hull->Faces.size(),
                                hull->SourceVertexCount, hull->Simplified ? ", soddalashtirilgan" : "");
            ImGui::PopTextWrapPos();
        }
        else
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", error.c_str());
            ImGui::PopTextWrapPos();
        }
        if (!entity.HasComponent<Rigidbody3DComponent>())
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Rigidbody 3D bilan ishlaydi");
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawMeshCollider(Entity entity, MeshCollider3DComponent& c)
    {
        bool changed = UI::PropertyAssetPath("Model fayli", c.ModelPath, ".gltf;.glb;.obj", ICON_FA_CUBES);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            UI::Tooltip("Collider shu modelning uchburchaklaridan quriladi. Bo'sh - obyektning Mesh Renderer'idagi model yoki primitiv");
        // What the body gets (cached per model: cheap to ask every frame).
        std::string error;
        Ref<const CollisionMesh3D> mesh = Scene::GetMeshCollider(entity, &error);
        UI::PropertyLabel("Mesh", "Fizik jism oladigan uchburchaklar: viewport'da collider chiziqlari bilan ko'rinadi. Uchburchaklar "
                                  "old tomonidan (chizilgan tomonidan) qattiq");
        ImGui::PushTextWrapPos(0.0f);
        if (mesh)
            ImGui::TextDisabled(ICON_FA_DRAW_POLYGON "  %zu uchburchak, %zu nuqta", mesh->GetTriangleCount(), mesh->Positions.size());
        else
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", error.c_str());
        ImGui::PopTextWrapPos();
        const auto* rb = entity.TryGetComponent<Rigidbody3DComponent>();
        if (!rb)
        {
            UI::PropertyLabel("");
            ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  Rigidbody 3D (Statik yoki Kinematik) bilan ishlaydi");
        }
        else if (rb->Type == Rigidbody3DComponent::BodyType::Dynamic)
        {
            UI::PropertyLabel("");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Dinamik jism mesh collider'ni hisobga "
                               "olmaydi: Rigidbody 3D turini Statik yoki Kinematik qiling, harakatlanadigan jism uchun Convex Hull "
                               "Collider 3D ishlating");
            ImGui::PopTextWrapPos();
        }
        return changed;
    }

    bool SceneHierarchyPanel::DrawCamera2DFollow(Entity entity, Camera2DFollowComponent& c)
    {
        bool changed = DrawEntityRefField("Nishon", c.Target, "Kuzatiladigan obyekt (ierarxiyadan tortib tashlang). O'yin davomida "
                                                              "fizika va skriptlardan keyin har kadrda uning ortidan siljiydi (faqat x / y)");
        Entity target = c.Target && m_Context ? m_Context->GetEntityByUUID(c.Target.ID) : Entity();
        if (!c.Target || (target && target == entity))
        {
            UI::PropertyLabel("");
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                               !c.Target ? "Nishon tanlanmagan: kamera joyida turadi" : "O'zini kuzata olmaydi");
        }
        InactiveComponentNote(entity, "Obyekt nofaol: kuzatmaydi");

        auto float2 = [&](const char* label, const char* tooltip, glm::vec2& v, float speed, float min, float max) {
            UI::PropertyLabel(label, tooltip);
            ImGui::PushID(label);
            bool edited = ImGui::DragFloat2("##v", &v.x, speed, min, max, "%.2f");
            ImGui::PopID();
            if (edited && min < max)
                v = glm::clamp(v, glm::vec2(min), glm::vec2(max));
            return edited;
        };
        changed |= float2("Siljish", "Nishon pozitsiyasiga qo'shiladi (masalan, o'yinchidan biroz yuqoriga qarash)", c.Offset, 0.05f, 0.0f, 0.0f);
        changed |= float2("Silliqlik (s)", "Kamera masofaning ~63% ini necha soniyada bosib o'tadi (x / y alohida). 0 - nishonga mahkam",
                          c.Damping, 0.01f, 0.0f, 10.0f);
        changed |= float2("O'lik zona", "Ko'rinish markazidagi to'rtburchak (to'liq eni / bo'yi, birlik): nishon uning ichida "
                                         "yurganda kamera qimirlamaydi", c.DeadZone, 0.05f, 0.0f, 1000.0f);

        UI::PropertyHeader("Oldinga qarash", "Kamera nishon oxirgi marta harakatlangan tomonga biroz oldinga o'tadi");
        changed |= float2("Masofa", "Harakat yo'nalishi bo'yicha qancha oldinga (birlik, x / y). 0 - o'chiq", c.LookAhead, 0.05f, -100.0f, 100.0f);
        UI::PropertyLabel("Silliqlik (s)", "Oldinga qarash qancha sekin almashadi (yo'nalish o'zgarganda)");
        if (ImGui::DragFloat("##lookDamping", &c.LookAheadDamping, 0.01f, 0.0f, 10.0f, "%.2f"))
        {
            c.LookAheadDamping = std::clamp(c.LookAheadDamping, 0.0f, 10.0f);
            changed = true;
        }
        UI::PropertyLabel("Min. tezlik", "Nishon shundan tezroq yursa yo'nalish almashadi (birlik/s): mayda qimirlashlar e'tiborga olinmaydi");
        if (ImGui::DragFloat("##lookSpeed", &c.LookAheadMinSpeed, 0.05f, 0.0f, 100.0f, "%.2f"))
        {
            c.LookAheadMinSpeed = std::clamp(c.LookAheadMinSpeed, 0.0f, 100.0f);
            changed = true;
        }

        UI::PropertyHeader("Chegaralar", "Kameraning butun ko'rinishi (nishon chuqurligida) shu dunyo to'rtburchagidan chiqmaydi");
        changed |= UI::PropertyBool("Chegaralash", c.UseBounds);
        if (c.UseBounds)
        {
            changed |= float2("Min (chap-past)", nullptr, c.BoundsMin, 0.1f, 0.0f, 0.0f);
            changed |= float2("Maks (o'ng-yuqori)", nullptr, c.BoundsMax, 0.1f, 0.0f, 0.0f);
            UI::PropertyLabel("");
            ImGui::BeginDisabled(!m_SceneEditable);
            if (ImGui::Button(ICON_FA_EXPAND "  Sahna chegaralari") && m_Context)
            {
                AABB box;
                if (m_Context->ComputeSceneBounds(box))
                {
                    c.BoundsMin = glm::vec2(box.Min);
                    c.BoundsMax = glm::vec2(box.Max);
                    changed = true;
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Sahnada chiziladigan hamma narsani o'rab oluvchi to'rtburchak (Shift+F kabi)");
            if (!entity.HasComponent<CameraComponent>())
            {
                UI::PropertyLabel("");
                ImGui::TextDisabled("Kamera komponentisiz: faqat markaz chegaralanadi");
            }
        }
        changed |= UI::PropertyBool("Boshida nishonga", c.SnapOnStart);
        if (ImGui::IsItemHovered())
            UI::Tooltip("O'yin boshlanganda kamera darhol nishon ustida turadi (aks holda joyidan silliq yetib boradi)");

        UI::PropertyLabel("");
        ImGui::BeginDisabled(!target || target == entity || !m_SceneEditable);
        if (ImGui::Button(ICON_FA_CROSSHAIRS "  Kamerani nishonga qo'yish") && target && m_Context)
        {
            glm::mat4 world = m_Context->GetWorldTransform(entity);
            glm::vec3 targetPosition = m_Context->GetWorldPosition(target);
            glm::vec2 halfView(0.0f);
            if (entity.HasComponent<CameraComponent>())
                halfView = Camera2DFollow::GetHalfView(m_Context->GetCameraProjection(entity), world, targetPosition.z);
            glm::vec2 position = Camera2DFollow::GetSnapPosition(c, glm::vec2(targetPosition), halfView);
            world[3].x = position.x;
            world[3].y = position.y;
            m_Context->SetWorldTransform(entity, world);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip("Editorda: kamerani o'yin boshidagi joyiga (nishon + siljish, chegaralar ichida) qo'yadi");
        return changed;
    }

    bool SceneHierarchyPanel::DrawSpriteAnimation(Entity entity, SpriteAnimationComponent& c)
    {
        InactiveComponentNote(entity, "Obyekt nofaol: animatsiya to'xtab turadi");
        bool changed = false;
        const auto* sprite = entity.TryGetComponent<SpriteRendererComponent>();
        const SpriteSheet* sheet = sprite && !sprite->TexturePath.empty() ? SpriteSheet::Get(sprite->TexturePath) : nullptr;

        const char* sources[] = { "Grid (ustun x qator)", "Sprite sheet kadrlari" };
        int source = c.UsesFrameList() ? 1 : 0;
        UI::PropertyLabel("Kadrlar manbai", "Grid: tekstura teng kataklarga bo'linadi. Sprite sheet: teksturaning nomlangan kadrlari "
                                            "(Sprite muharriri) ro'yxati - turli o'lchamli kadrlar ham bo'ladi.");
        ImGui::PushID("source");
        if (ImGui::Combo("##v", &source, sources, 2))
        {
            if (source == 0)
                c.Frames.clear();
            else if (sheet)
                c.PlayFrames(sheet->FindSequence(sheet->GetSequencePrefixes().empty() ? "" : sheet->GetSequencePrefixes().front()), c.FPS, c.Loop);
            changed = true;
        }
        ImGui::PopID();
        if (source == 1 && !c.UsesFrameList())
        {
            UI::PropertyLabel("");
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Teksturada kadrlar yo'q");
        }

        if (c.UsesFrameList())
        {
            // Sequences of the sheet ("run_0", "run_1", ...) by their prefix.
            UI::PropertyLabel("Ketma-ketlik", "Bir xil nomli, raqamlangan kadrlar (masalan run_0, run_1, ...) tartib bilan qo'yiladi");
            ImGui::PushID("sequence");
            if (ImGui::BeginCombo("##v", std::format("{} ta kadr", c.Frames.size()).c_str()))
            {
                if (sheet)
                {
                    for (const std::string& prefix : sheet->GetSequencePrefixes())
                    {
                        std::vector<std::string> sequence = sheet->FindSequence(prefix);
                        if (ImGui::Selectable(std::format("{}  ({} ta)", prefix, sequence.size()).c_str()))
                        {
                            c.PlayFrames(sequence, c.FPS, c.Loop);
                            changed = true;
                        }
                    }
                    if (ImGui::Selectable(std::format("(hammasi, {} ta)", sheet->Frames.size()).c_str()))
                    {
                        c.PlayFrames(sheet->FindSequence(""), c.FPS, c.Loop);
                        changed = true;
                    }
                }
                else
                    ImGui::TextDisabled("Teksturada kadrlar yo'q");
                ImGui::EndCombo();
            }
            ImGui::PopID();
            UI::PropertyLabel("Kadrlar");
            std::string list;
            for (size_t i = 0; i < c.Frames.size() && i < 12; i++)
                list += (i ? ", " : "") + c.Frames[i];
            if (c.Frames.size() > 12)
                list += std::format(" ... (+{})", c.Frames.size() - 12);
            ImGui::TextWrapped("%s", list.c_str());
            int missing = 0;
            for (const std::string& name : c.Frames)
                if (!sheet || !sheet->Find(name))
                    missing++;
            if (missing > 0)
            {
                UI::PropertyLabel("");
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %d ta kadr teksturada topilmadi", missing);
            }
        }
        else
        {
            changed |= UI::PropertyInt("Ustunlar", c.Columns);
            changed |= UI::PropertyInt("Qatorlar", c.Rows);
        }
        changed |= UI::PropertyInt("Boshlang'ich kadr", c.StartFrame);
        changed |= UI::PropertyInt("Kadrlar soni", c.FrameCount);
        changed |= UI::PropertyFloat("FPS", c.FPS, 0.1f, 0.0f, 120.0f);
        changed |= UI::PropertyBool("Takrorlash", c.Loop);
        changed |= UI::PropertyBool("Ijroda", c.Playing);
        c.Columns = std::max(1, c.Columns); c.Rows = std::max(1, c.Rows);
        c.FrameCount = std::max(1, c.FrameCount); c.StartFrame = std::max(0, c.StartFrame);
        UI::PropertyLabel("Hozirgi kadr");
        if (c.UsesFrameList())
            ImGui::Text("%d  (%s)", c.GetCurrentFrame(), c.GetCurrentFrameName().c_str());
        else
            ImGui::Text("%d", c.GetCurrentFrame());
        return changed;
    }

    bool SceneHierarchyPanel::DrawTagsComponent(TagsComponent& c)
    {
        bool changed = false;
        const ImGuiStyle& style = ImGui::GetStyle();

        UI::PropertyLabel("Teglar");
        if (c.Tags.empty())
            ImGui::TextDisabled("(teg yo'q)");
        else
        {
            // Chips wrap onto new lines; clicking one removes the tag.
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            int remove = -1;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.52f, 0.41f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.27f, 0.24f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.30f, 0.27f, 1.0f));
            for (int i = 0; i < (int)c.Tags.size(); i++)
            {
                std::string label = std::string(ICON_FA_TAG " ") + c.Tags[i] + "  " ICON_FA_XMARK;
                float w = ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
                if (i > 0 && ImGui::GetItemRectMax().x + style.ItemSpacing.x + w <= right)
                    ImGui::SameLine();
                ImGui::PushID(i);
                if (ImGui::Button(label.c_str()))
                    remove = i;
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Tegni olib tashlash");
                ImGui::PopID();
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            if (remove >= 0)
            {
                c.Tags.erase(c.Tags.begin() + remove);
                changed = true;
            }
        }

        // Add: type a new tag (Enter) or pick one used in the project / scene.
        UI::PropertyLabel("Qo'shish");
        const float buttonW = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - buttonW - style.ItemSpacing.x));
        std::string toAdd;
        if (ImGui::InputTextWithHint("##newTag", "Yangi teg (Enter)", &m_NewTagBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            toAdd = TrimTag(m_NewTagBuffer);
            m_NewTagBuffer.clear();
            ImGui::SetKeyboardFocusHere(-1); // keep typing the next tag
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LIST "##tagSuggestions", ImVec2(buttonW, 0)))
            ImGui::OpenPopup("TagSuggestions");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Mavjud teglardan tanlash");
        if (ImGui::BeginPopup("TagSuggestions"))
        {
            std::vector<std::string> known;
            if (auto project = Project::GetActive())
                known = project->GetConfig().Tags;
            for (auto& t : m_Context->GetAllTags())
                if (std::find(known.begin(), known.end(), t) == known.end())
                    known.push_back(t);
            std::string filter = ToLower(TrimTag(m_NewTagBuffer));
            int shown = 0;
            for (const auto& t : known)
            {
                if (c.Has(t) || (!filter.empty() && ToLower(t).find(filter) == std::string::npos))
                    continue;
                if (ImGui::Selectable((std::string(ICON_FA_TAG "  ") + t).c_str()))
                {
                    toAdd = t;
                    m_NewTagBuffer.clear();
                }
                shown++;
            }
            if (shown == 0)
                ImGui::TextDisabled("Taklif yo'q: yangi teg nomini yozing");
            ImGui::EndPopup();
        }

        if (!toAdd.empty() && c.Add(toAdd))
        {
            changed = true;
            // New names go to the project's tag list (Project settings > Tags).
            if (Project::RegisterTag(toAdd) && OnProjectSettingsModified)
                OnProjectSettingsModified();
        }

        UI::PropertyLabel("");
        ImGui::TextDisabled("Skriptda: HasTag(\"%s\"), FindEntitiesWithTag(...)", c.Tags.empty() ? "Enemy" : c.Tags.front().c_str());
        return changed;
    }

    bool SceneHierarchyPanel::DrawScriptField(const ScriptFieldInfo& f, ScriptFieldValue& v)
    {
        if (!f.HeaderText.empty())
            UI::PropertyHeader(f.HeaderText.c_str());

        const char* label = f.GetDisplayName().c_str();
        const char* tooltip = f.TooltipText.empty() ? nullptr : f.TooltipText.c_str();
        if (f.Type == ScriptFieldType::Entity)
        {
            EntityRef ref = v.Get<EntityRef>();
            if (!DrawEntityRefField(label, ref, tooltip))
                return false;
            v.Set(ref);
            return true;
        }

        UI::PropertyLabel(label, tooltip);
        ImGui::PushID(f.Name.c_str());
        bool changed = false;
        const ImGuiSliderFlags clamp = ImGuiSliderFlags_AlwaysClamp;
        auto vecField = [&](auto value, int components) {
            float* p = &value.x;
            bool edited = false;
            if (f.IsColor && components == 3)
                edited = ImGui::ColorEdit3("##v", p, ImGuiColorEditFlags_Float | (f.IsHdr ? ImGuiColorEditFlags_HDR : 0));
            else if (f.IsColor && components == 4)
                edited = ImGui::ColorEdit4("##v", p, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                                                     (f.IsHdr ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0));
            else if (f.HasRange)
                edited = ImGui::SliderScalarN("##v", ImGuiDataType_Float, p, components, &f.RangeMin, &f.RangeMax, "%.3f", clamp);
            else
                edited = ImGui::DragScalarN("##v", ImGuiDataType_Float, p, components, 0.05f, nullptr, nullptr, "%.3f");
            if (edited) v.Set(value);
            return edited;
        };

        switch (f.Type)
        {
            case ScriptFieldType::Float:
            {
                float value = v.Get<float>();
                changed = f.HasRange ? ImGui::SliderFloat("##v", &value, f.RangeMin, f.RangeMax, "%.3f", clamp)
                                     : ImGui::DragFloat("##v", &value, 0.05f);
                if (changed) v.Set(value);
                break;
            }
            case ScriptFieldType::Int:
            case ScriptFieldType::Enum:
            {
                bool isEnum = f.Type == ScriptFieldType::Enum;
                int64_t value = isEnum ? v.GetEnum() : (int64_t)v.Get<int>();
                if (!f.EnumOptions.empty())
                {
                    const ScriptEnumOption* current = f.FindOption(value);
                    std::string preview = current ? current->Name : std::format("({})", value);
                    if (ImGui::BeginCombo("##v", preview.c_str()))
                    {
                        for (const auto& option : f.EnumOptions)
                        {
                            bool selected = option.Value == value;
                            if (ImGui::Selectable(option.Name.c_str(), selected) && !selected)
                            {
                                value = option.Value;
                                changed = true;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else if (isEnum)
                    changed = ImGui::DragScalar("##v", ImGuiDataType_S64, &value, 0.2f);
                else
                {
                    int i = (int)value;
                    int lo = (int)std::ceil(f.RangeMin), hi = (int)std::floor(f.RangeMax);
                    changed = f.HasRange && lo <= hi ? ImGui::SliderInt("##v", &i, lo, hi, "%d", clamp) : ImGui::DragInt("##v", &i);
                    value = i;
                }
                if (changed)
                {
                    if (isEnum) v.SetEnum(value);
                    else v.Set((int)value);
                }
                break;
            }
            case ScriptFieldType::Bool:
            {
                bool value = v.Get<bool>();
                changed = ImGui::Checkbox("##v", &value);
                if (changed) v.Set(value);
                break;
            }
            case ScriptFieldType::Vec2: changed = vecField(v.Get<glm::vec2>(), 2); break;
            case ScriptFieldType::Vec3: changed = vecField(v.Get<glm::vec3>(), 3); break;
            case ScriptFieldType::Vec4: changed = vecField(v.Get<glm::vec4>(), 4); break;
            case ScriptFieldType::String:
                changed = f.IsMultiline ? ImGui::InputTextMultiline("##v", &v.StringValue, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f))
                                        : ImGui::InputText("##v", &v.StringValue);
                break;
            default:
                ImGui::TextDisabled("(noma'lum tur)");
                break;
        }
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tooltip);
        ImGui::PopID();
        if (changed)
            f.ClampToRange(v);
        return changed;
    }

    void SceneHierarchyPanel::DrawScriptComponent(Entity entity, bool& modified)
    {
        // Drawn after this function returns (AddSection): captures by value only.
        AddSection<NativeScriptComponent>(ICON_FA_CODE, "C++ Skript", entity, [this, entity](NativeScriptComponent& c) {
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

            UI::PropertyLabel("Klass");
            bool exists = ScriptRegistry::Find(c.ClassName) != nullptr;
            if (!exists && !c.ClassName.empty())
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
            std::string preview = c.ClassName.empty() ? "(tanlang)" : c.ClassName;
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
            {
                ImGui::PopStyleColor();
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

    static bool DrawAnimatorStateMachine(AnimatorComponent& c, const Ref<Model>& model, bool* isOpen)
    {
        bool changed = false;
        ImGui::PushID("asm");
        *isOpen = ImGui::CollapsingHeader(ICON_FA_DIAGRAM_PROJECT "  Animator holat mashinasi");
        if (!*isOpen)
        {
            ImGui::PopID();
            return false;
        }
        ImGui::Indent(6.0f);
        if (c.CurrentState >= 0)
            ImGui::TextColored(UI::AccentColor(), ICON_FA_PLAY "  Hozirgi holat: %s", c.GetCurrentStateName());
        if (c.States.empty())
            ImGui::TextDisabled("Holatlar yo'q - Animator Play() bilan qo'lda boshqariladi.");

        // ---- parameters
        ImGui::SeparatorText("Parametrlar");
        const char* types[] = { "Float", "Bool", "Trigger" };
        for (size_t i = 0; i < c.Parameters.size(); i++)
        {
            auto& p = c.Parameters[i];
            ImGui::PushID((int)i);
            ImGui::SetNextItemWidth(110); changed |= ImGui::InputText("##n", &p.Name);
            ImGui::SameLine(); ImGui::SetNextItemWidth(80);
            int t = (int)p.Kind;
            if (ImGui::Combo("##t", &t, types, 3)) { p.Kind = (AnimatorParameter::Type)t; changed = true; }
            ImGui::SameLine(); ImGui::SetNextItemWidth(70);
            if (p.Kind == AnimatorParameter::Type::Float) changed |= ImGui::DragFloat("##v", &p.Value, 0.05f);
            else { bool b = p.Value != 0.0f; if (ImGui::Checkbox("##v", &b)) { p.Value = b ? 1.0f : 0.0f; changed = true; } }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) { c.Parameters.erase(c.Parameters.begin() + i); changed = true; ImGui::PopID(); break; }
            ImGui::PopID();
        }
        if (ImGui::SmallButton(ICON_FA_PLUS "  Parametr")) { c.Parameters.push_back({ "Param" + std::to_string(c.Parameters.size()), AnimatorParameter::Type::Float, 0.0f }); changed = true; }

        // ---- states
        ImGui::SeparatorText("Holatlar");
        for (size_t i = 0; i < c.States.size(); i++)
        {
            auto& st = c.States[i];
            ImGui::PushID(1000 + (int)i);
            bool isDefault = c.DefaultState == (int)i;
            if (ImGui::RadioButton("##def", isDefault)) { c.DefaultState = (int)i; changed = true; }
            if (ImGui::IsItemHovered()) UI::Tooltip("Boshlang'ich holat");
            ImGui::SameLine(); ImGui::SetNextItemWidth(90); changed |= ImGui::InputText("##name", &st.Name);
            ImGui::SameLine(); ImGui::SetNextItemWidth(90);
            if (ImGui::BeginCombo("##clip", st.Clip.empty() ? "(klip)" : st.Clip.c_str()))
            {
                if (model)
                    for (const auto& clip : model->GetAnimations())
                        if (ImGui::Selectable(clip.Name.c_str(), clip.Name == st.Clip)) { st.Clip = clip.Name; changed = true; }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); ImGui::SetNextItemWidth(50); changed |= ImGui::DragFloat("##spd", &st.Speed, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::SameLine(); changed |= ImGui::Checkbox("##loop", &st.Loop);
            if (ImGui::IsItemHovered()) UI::Tooltip("Takrorlash");
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK))
            {
                c.States.erase(c.States.begin() + i);
                for (auto it = c.Transitions.begin(); it != c.Transitions.end();)
                {
                    if (it->From == (int)i || it->To == (int)i) { it = c.Transitions.erase(it); continue; }
                    if (it->From > (int)i) it->From--;
                    if (it->To > (int)i) it->To--;
                    ++it;
                }
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::SetNextItemWidth(160);
            ImGui::Indent(28);
            if (ImGui::BeginCombo("##sp", st.SpeedParameter.empty() ? "Tezlik parametri: yo'q" : ("Tezlik x " + st.SpeedParameter).c_str()))
            {
                if (ImGui::Selectable("yo'q", st.SpeedParameter.empty())) { st.SpeedParameter.clear(); changed = true; }
                for (auto& p : c.Parameters)
                    if (p.Kind == AnimatorParameter::Type::Float && ImGui::Selectable(p.Name.c_str(), p.Name == st.SpeedParameter)) { st.SpeedParameter = p.Name; changed = true; }
                ImGui::EndCombo();
            }
            ImGui::Unindent(28);
            ImGui::PopID();
        }
        if (ImGui::SmallButton(ICON_FA_PLUS "  Holat"))
        {
            AnimatorState st;
            st.Name = "Holat" + std::to_string(c.States.size());
            if (model && !model->GetAnimations().empty()) st.Clip = model->GetAnimations()[std::min(c.States.size(), model->GetAnimations().size() - 1)].Name;
            c.States.push_back(st);
            changed = true;
        }

        // ---- transitions
        ImGui::SeparatorText("O'tishlar");
        auto stateName = [&](int i) -> std::string { return i < 0 ? "Har qanday" : (i < (int)c.States.size() ? c.States[i].Name : "?"); };
        const char* ops[] = { ">", "<", "rost", "yolg'on", "trigger" };
        for (size_t i = 0; i < c.Transitions.size(); i++)
        {
            auto& tr = c.Transitions[i];
            ImGui::PushID(2000 + (int)i);
            ImGui::SetNextItemWidth(95);
            if (ImGui::BeginCombo("##from", stateName(tr.From).c_str()))
            {
                for (int k = -1; k < (int)c.States.size(); k++)
                    if (ImGui::Selectable(stateName(k).c_str(), k == tr.From)) { tr.From = k; changed = true; }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); ImGui::TextUnformatted(ICON_FA_ARROW_RIGHT);
            ImGui::SameLine(); ImGui::SetNextItemWidth(95);
            if (ImGui::BeginCombo("##to", stateName(tr.To).c_str()))
            {
                for (int k = 0; k < (int)c.States.size(); k++)
                    if (ImGui::Selectable(stateName(k).c_str(), k == tr.To)) { tr.To = k; changed = true; }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); ImGui::SetNextItemWidth(45); changed |= ImGui::DragFloat("##fade", &tr.Fade, 0.01f, 0.0f, 5.0f, "%.2fs");
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_XMARK)) { c.Transitions.erase(c.Transitions.begin() + i); changed = true; ImGui::PopID(); break; }

            ImGui::Indent(18);
            changed |= ImGui::Checkbox("Exit time", &tr.HasExitTime);
            if (tr.HasExitTime) { ImGui::SameLine(); ImGui::SetNextItemWidth(60); changed |= ImGui::DragFloat("##exit", &tr.ExitTime, 0.01f, 0.0f, 10.0f); }
            for (size_t k = 0; k < tr.Conditions.size(); k++)
            {
                auto& cd = tr.Conditions[k];
                ImGui::PushID((int)k);
                ImGui::SetNextItemWidth(95);
                if (ImGui::BeginCombo("##p", cd.Parameter.empty() ? "(parametr)" : cd.Parameter.c_str()))
                {
                    for (auto& p : c.Parameters)
                        if (ImGui::Selectable(p.Name.c_str(), p.Name == cd.Parameter)) { cd.Parameter = p.Name; changed = true; }
                    ImGui::EndCombo();
                }
                ImGui::SameLine(); ImGui::SetNextItemWidth(70);
                int op = (int)cd.Mode;
                if (ImGui::Combo("##op", &op, ops, 5)) { cd.Mode = (AnimatorCondition::Op)op; changed = true; }
                if (cd.Mode == AnimatorCondition::Op::Greater || cd.Mode == AnimatorCondition::Op::Less)
                {
                    ImGui::SameLine(); ImGui::SetNextItemWidth(55); changed |= ImGui::DragFloat("##th", &cd.Threshold, 0.05f);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK)) { tr.Conditions.erase(tr.Conditions.begin() + k); changed = true; ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (ImGui::SmallButton(ICON_FA_PLUS "  Shart")) { tr.Conditions.push_back({}); changed = true; }
            ImGui::Unindent(18);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (!c.States.empty() && ImGui::SmallButton(ICON_FA_PLUS "  O'tish")) { c.Transitions.push_back({ -1, 0 }); changed = true; }
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
            ImGui::TextUnformatted(EntityIcon(entity));
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
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.52f, 0.41f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.62f, 0.50f, 1.0f));
            if (ImGui::Button(ICON_FA_PLUS "  Komponent"))
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
                ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS "  Qidirish", &m_AddComponentFilter);
                ImGui::SeparatorText("Render");
                DisplayAddComponentEntry<SpriteRendererComponent>("Sprite Renderer", ICON_FA_IMAGE);
                DisplayAddComponentEntry<SpriteAnimationComponent>("Sprite Animation", ICON_FA_FILM);
                DisplayAddComponentEntry<TintAnimationComponent>("Tint Animation (rang)", ICON_FA_PALETTE);
                DisplayAddComponentEntry<CircleRendererComponent>("Circle Renderer", ICON_FA_CIRCLE);
                DisplayAddComponentEntry<TextComponent>("Text", ICON_FA_FONT);
                DisplayAddComponentEntry<MeshRendererComponent>("Mesh Renderer", ICON_FA_CUBE);
                DisplayAddComponentEntry<AnimatorComponent>("Animator (skeletal)", ICON_FA_PERSON_RUNNING);
                DisplayAddComponentEntry<CameraComponent>("Camera", ICON_FA_CAMERA);
                ImGui::SeparatorText("Yorug'lik");
                DisplayAddComponentEntry<DirectionalLightComponent>("Directional Light", ICON_FA_SUN);
                DisplayAddComponentEntry<PointLightComponent>("Point Light", ICON_FA_LIGHTBULB);
                DisplayAddComponentEntry<SpotLightComponent>("Spot Light", ICON_FA_BULLHORN);
                DisplayAddComponentEntry<Light2DComponent>("Light 2D", ICON_FA_FIRE);
                ImGui::SeparatorText("Fizika 2D");
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
                ImGui::SeparatorText("Fizika 3D");
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
                ImGui::SeparatorText("Effektlar va UI");
                DisplayAddComponentEntry<TilemapComponent>("Tilemap", ICON_FA_TABLE_CELLS);
                DisplayAddComponentEntry<ParallaxComponent>("Parallax", ICON_FA_MOUNTAIN);
                DisplayAddComponentEntry<Camera2DFollowComponent>("Camera 2D Follow", ICON_FA_VIDEO);
                DisplayAddComponentEntry<ParticleSystemComponent>("Particle System", ICON_FA_WAND_MAGIC_SPARKLES);
                DisplayAddComponentEntry<UITransformComponent>("UI Transform", ICON_FA_TABLE_CELLS);
                DisplayAddComponentEntry<UIButtonComponent>("UI Button", ICON_FA_HAND_POINTER);
                ImGui::SeparatorText("Boshqa");
                DisplayAddComponentEntry<AudioSourceComponent>("Audio Source", ICON_FA_VOLUME_HIGH);
                DisplayAddComponentEntry<NativeScriptComponent>("C++ Script", ICON_FA_CODE);
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
            if (ImGui::Button(ICON_FA_BURST "  Sinash"))
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
            changed |= UI::PropertyColor4("Rang", c.Color);
            changed |= UI::PropertyFloat("Harf oralig'i", c.Kerning, 0.005f);
            changed |= UI::PropertyFloat("Qator oralig'i", c.LineSpacing, 0.01f);
            changed |= UI::PropertyBool("Markazlash", c.Centered);
            if (UI::PropertyAssetPath("Shrift", c.FontPath, ".ttf;.otf", ICON_FA_FONT))
            {
                c.FontAsset = c.FontPath.empty() ? Font::GetDefault() : Font::Load(Project::GetAssetFileSystemPath(c.FontPath));
                changed = true;
            }
            changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);
            return changed;
        }, true, modified);

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
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SeparatorText("Material");
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
                if (ImGui::Selectable("(birinchi)", c.Clip.empty())) { c.Clip.clear(); changed = true; }
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
                modified |= DrawAnimatorStateMachine(*anim, mr ? mr->ModelAsset : nullptr, &open);
                if (m_FoldState && open != wasOpen && m_FoldState->SetOpen(foldKey, open, false) && OnFoldStateModified)
                    OnFoldStateModified();
            } });

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
                if (ImGui::SmallButton(ICON_FA_TURN_UP "  Platform Effector 2D ga o'tkazish"))
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
            if (ImGui::Button(ICON_FA_CROP "  Spraytga moslash", ImVec2(-FLT_MIN, 0)))
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
                if (ImGui::SmallButton(ICON_FA_ARROWS_LEFT_RIGHT "  Tomonini almashtirish"))
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
            changed |= UI::PropertyFloat("Balandlik", c.Height, 0.01f, 0.2f, 20.0f);
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
            bool changed = false;
            InactiveComponentNote(entity, "Obyekt nofaol: particle'lar to'xtatilgan");
            UI::PropertyLabel("");
            if (ImGui::Button(c.Playing ? ICON_FA_PAUSE "  Pauza" : ICON_FA_PLAY "  Ijro")) { c.Playing = !c.Playing; }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Qayta")) c.Clear();
            ImGui::SameLine();
            ImGui::TextDisabled("%zu", c.Particles.size());
            changed |= UI::PropertyBool("Takrorlash", c.Looping);
            changed |= UI::PropertyBool("Editorda ko'rsatish", c.PlayInEditor);
            changed |= UI::PropertyFloat("Chiqarish (1/s)", c.EmissionRate, 0.5f, 0.0f, 10000.0f);
            changed |= UI::PropertyInt("Portlash (burst)", c.Burst);
            changed |= UI::PropertyInt("Maksimal soni", c.MaxParticles);
            const char* shapes[] = { "Nuqta", "Doira", "Sfera", "Quti" };
            int shape = (int)c.Shape;
            if (UI::PropertyCombo("Shakl", shape, shapes, 4)) { c.Shape = (ParticleSystemComponent::EmitterShape)shape; changed = true; }
            if (c.Shape == ParticleSystemComponent::EmitterShape::Box) changed |= UI::PropertyFloat3("O'lchami", c.ShapeExtents);
            else if (c.Shape != ParticleSystemComponent::EmitterShape::Point) changed |= UI::PropertyFloat("Radius", c.ShapeRadius, 0.01f, 0.0f, 1000.0f);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText("Harakat");
            changed |= UI::PropertyFloat("Umr (min)", c.LifetimeMin, 0.01f, 0.01f, 100.0f);
            changed |= UI::PropertyFloat("Umr (max)", c.LifetimeMax, 0.01f, 0.01f, 100.0f);
            changed |= UI::PropertyFloat("Tezlik (min)", c.SpeedMin, 0.05f);
            changed |= UI::PropertyFloat("Tezlik (max)", c.SpeedMax, 0.05f);
            changed |= UI::PropertyFloat3("Yo'nalish", c.Direction);
            changed |= UI::PropertySlider("Tarqalish (°)", c.Spread, 0.0f, 180.0f);
            changed |= UI::PropertyFloat3("Gravitatsiya", c.Gravity);
            changed |= UI::PropertyFloat("Qarshilik", c.Drag, 0.01f, 0.0f, 50.0f);
            changed |= UI::PropertyFloat("Aylanish (max)", c.AngularVelocityMax, 0.05f, 0.0f, 100.0f);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText("Ko'rinish");
            changed |= UI::PropertyColor4("Boshlang'ich rang", c.StartColor);
            changed |= UI::PropertyColor4("Oxirgi rang", c.EndColor);
            changed |= UI::PropertyFloat("Boshlang'ich o'lcham", c.StartSize, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyFloat("Oxirgi o'lcham", c.EndSize, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Doira shaklida", c.RenderAsCircle);
            changed |= UI::PropertyBool("Qo'shimcha yorqinlik (additive)", c.Additive);
            changed |= UI::PropertyBool("Dunyo koordinatalari", c.WorldSpace);
            changed |= UI::PropertyBool("Kameraga qaragan", c.Billboard);
            if (UI::PropertyAssetPath("Tekstura", c.TexturePath, ".png;.jpg;.jpeg", ICON_FA_FILE_IMAGE)) { c.Texture = nullptr; changed = true; }
            changed |= DrawSortingLayer(c.SortingLayer, c.SortingOrder);
            return changed;
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

        AddSection<UITransformComponent>(ICON_FA_TABLE_CELLS, "UI Transform", entity, [](UITransformComponent& c) {
            bool changed = false;
            const char* anchors[] = { "Yuqori-chap", "Yuqori", "Yuqori-o'ng", "Chap", "Markaz", "O'ng", "Past-chap", "Past", "Past-o'ng" };
            int a = (int)c.Anchor;
            if (UI::PropertyCombo("Langar (anchor)", a, anchors, 9)) { c.Anchor = (UITransformComponent::AnchorPoint)a; changed = true; }
            changed |= UI::PropertyFloat2("Pozitsiya (px)", c.Position, 1.0f);
            changed |= UI::PropertyFloat2("O'lcham (px)", c.Size, 1.0f);
            changed |= UI::PropertyFloat("Shrift o'lchami (px)", c.FontSize, 0.5f, 0.0f, 500.0f);
            changed |= UI::PropertyInt("Tartib", c.Order);
            return changed;
        }, true, modified);

        AddSection<UIButtonComponent>(ICON_FA_HAND_POINTER, "UI Button", entity, [&](UIButtonComponent& c) {
            InactiveComponentNote(entity, "Obyekt nofaol: tugma ko'rinmaydi va bosilmaydi");
            bool changed = UI::PropertyColor4("Oddiy", c.NormalColor);
            changed |= UI::PropertyColor4("Ustida (hover)", c.HoverColor);
            changed |= UI::PropertyColor4("Bosilgan", c.PressedColor);
            changed |= UI::PropertyBool("Faol", c.Interactable);
            UI::PropertyLabel("");
            ImGui::TextDisabled("Skriptda: OnClick() yoki .Clicked");
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
            }
            UI::PropertyLabel("");
            if (ImGui::Button(ICON_FA_PLAY "  Eshitish") && !c.Path.empty())
                AudioEngine::Play(Project::GetAssetFileSystemPath(c.Path), c.Volume, c.Pitch, false);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_STOP))
                AudioEngine::StopAll();
            return changed;
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
        if (modified) MarkModified();

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
