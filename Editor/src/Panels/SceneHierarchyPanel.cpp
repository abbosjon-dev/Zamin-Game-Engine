#include "SceneHierarchyPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

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
        if (e.HasComponent<DirectionalLightComponent>()) return ICON_FA_SUN;
        if (e.HasComponent<PointLightComponent>()) return ICON_FA_LIGHTBULB;
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
        if (*showInspector)
        {
            if (ImGui::Begin(ICON_FA_SLIDERS "  Inspektor###Inspector", showInspector))
            {
                DrawInspector();
                if (m_ScrollInspectorFrames > 0) // automation: show the last components
                {
                    ImGui::SetScrollY(ImGui::GetScrollMaxY());
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
        else if (p == "Camera") { e = m_Context->CreateEntity("Kamera"); e.AddComponent<CameraComponent>().Primary = !m_Context->GetPrimaryCameraEntity(); }
        else if (p == "Camera2D")
        {
            e = m_Context->CreateEntity("2D Kamera");
            auto& c = e.AddComponent<CameraComponent>();
            c.Primary = !m_Context->GetPrimaryCameraEntity();
            c.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
        }
        else if (p == "DirectionalLight")
        {
            e = m_Context->CreateEntity("Quyosh");
            e.AddComponent<DirectionalLightComponent>();
            e.GetTransform().Rotation = { -0.9f, 0.6f, 0.0f };
        }
        else if (p == "PointLight") { e = m_Context->CreateEntity("Nuqtali yorug'lik"); e.AddComponent<PointLightComponent>(); e.GetTransform().Translation = { 0, 2, 0 }; }
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
        const std::string& tag = entity.GetName();
        const auto& children = entity.GetComponent<RelationshipComponent>().Children;
        bool hasChildren = !children.empty() && m_Filter.empty();
        bool selected = IsSelected(entity);
        bool primary = selected && GetSelectedEntity() == entity;
        bool hiddenInView = m_Context->IsHiddenInEditor(entity);

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
        if (hiddenInView) // hidden in the scene view: a faint label
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        std::string label = std::string(EntityIcon(entity)) + "  " + tag;
        bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
        if (hiddenInView)
            ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (selected)
            ImGui::PopStyleColor(2);
        const ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();

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

        DrawEntityContextMenu(entity);
        DrawRowToggles(entity, rowMin, rowMax);

        if (open)
        {
            if (hasChildren)
            {
                std::vector<UUID> copy = children; // may be modified by drag & drop
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
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
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
            { "Animator", "Animator" }, { "DirectionalLight", "Directional Light" }, { "PointLight", "Point Light" },
            { "Rigidbody2D", "Rigidbody 2D" }, { "BoxCollider2D", "Box Collider 2D" }, { "CircleCollider2D", "Circle Collider 2D" },
            { "Rigidbody3D", "Rigidbody 3D" }, { "BoxCollider3D", "Box Collider 3D" }, { "SphereCollider3D", "Sphere Collider 3D" },
            { "CapsuleCollider3D", "Capsule Collider 3D" }, { "CharacterController3D", "Character Controller 3D" },
            { "Tilemap", "Tilemap" }, { "ParticleSystem", "Particle System" }, { "UITransform", "UI Transform" },
            { "UIButton", "UI Button" }, { "AudioSource", "Audio Source" }, { "NativeScript", "C++ Skript" },
        };
        for (const auto& [k, name] : s_Names)
            if (k == key)
                return name;
        return "Komponent";
    }

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
                ImGui::SameLine();
                ImGui::TextDisabled(c.Instance->HasStarted() ? "ishlayapti" : enabled ? "OnStart kutilmoqda" : "o'chirilgan");
            }

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
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(EntityIcon(entity));
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
                DisplayAddComponentEntry<CircleRendererComponent>("Circle Renderer", ICON_FA_CIRCLE);
                DisplayAddComponentEntry<TextComponent>("Text", ICON_FA_FONT);
                DisplayAddComponentEntry<MeshRendererComponent>("Mesh Renderer", ICON_FA_CUBE);
                DisplayAddComponentEntry<AnimatorComponent>("Animator (skeletal)", ICON_FA_PERSON_RUNNING);
                DisplayAddComponentEntry<CameraComponent>("Camera", ICON_FA_CAMERA);
                ImGui::SeparatorText("Yorug'lik");
                DisplayAddComponentEntry<DirectionalLightComponent>("Directional Light", ICON_FA_SUN);
                DisplayAddComponentEntry<PointLightComponent>("Point Light", ICON_FA_LIGHTBULB);
                ImGui::SeparatorText("Fizika 2D");
                DisplayAddComponentEntry<Rigidbody2DComponent>("Rigidbody 2D", ICON_FA_ATOM);
                DisplayAddComponentEntry<BoxCollider2DComponent>("Box Collider 2D", ICON_FA_VECTOR_SQUARE);
                DisplayAddComponentEntry<CircleCollider2DComponent>("Circle Collider 2D", ICON_FA_CIRCLE_NODES);
                ImGui::SeparatorText("Fizika 3D");
                DisplayAddComponentEntry<Rigidbody3DComponent>("Rigidbody 3D", ICON_FA_ATOM);
                DisplayAddComponentEntry<BoxCollider3DComponent>("Box Collider 3D", ICON_FA_CUBE);
                DisplayAddComponentEntry<SphereCollider3DComponent>("Sphere Collider 3D", ICON_FA_CIRCLE);
                DisplayAddComponentEntry<CapsuleCollider3DComponent>("Capsule Collider 3D", ICON_FA_CAPSULES);
                DisplayAddComponentEntry<CharacterController3DComponent>("Character Controller 3D", ICON_FA_PERSON_RUNNING);
                ImGui::SeparatorText("Effektlar va UI");
                DisplayAddComponentEntry<TilemapComponent>("Tilemap", ICON_FA_TABLE_CELLS);
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

        AddSection<CameraComponent>(ICON_FA_CAMERA, "Camera", entity, [](CameraComponent& c) {
            bool changed = false;
            auto& camera = c.Camera;
            changed |= UI::PropertyBool("Asosiy (Primary)", c.Primary);

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
            return changed;
        }, true, modified);

        AddSection<SpriteRendererComponent>(ICON_FA_IMAGE, "Sprite Renderer", entity, [](SpriteRendererComponent& c) {
            bool changed = UI::PropertyColor4("Rang", c.Color);
            if (UI::PropertyAssetPath("Tekstura", c.TexturePath, ".png;.jpg;.jpeg;.bmp;.tga", ICON_FA_FILE_IMAGE))
            {
                c.Texture = c.TexturePath.empty() ? nullptr : TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TexturePath));
                changed = true;
            }
            if (c.Texture)
            {
                UI::PropertyLabel("");
                ImGui::Image((ImTextureID)(uintptr_t)c.Texture->GetRendererID(), ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
            }
            changed |= UI::PropertyFloat("Takrorlash", c.TilingFactor, 0.05f, 0.0f, 100.0f);
            changed |= UI::PropertyInt("Tartib (sorting)", c.SortingOrder);
            if (UI::PropertyBool("Pixel-art (aniq piksellar)", c.PixelArt) && !c.TexturePath.empty())
                c.Texture = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TexturePath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            changed |= UI::PropertyBool("Gorizontal aks (Flip X)", c.FlipX);
            changed |= UI::PropertyBool("Vertikal aks (Flip Y)", c.FlipY);
            return changed;
        }, true, modified);

        AddSection<SpriteAnimationComponent>(ICON_FA_FILM, "Sprite Animation", entity, [](SpriteAnimationComponent& c) {
            bool changed = UI::PropertyInt("Ustunlar", c.Columns);
            changed |= UI::PropertyInt("Qatorlar", c.Rows);
            changed |= UI::PropertyInt("Boshlang'ich kadr", c.StartFrame);
            changed |= UI::PropertyInt("Kadrlar soni", c.FrameCount);
            changed |= UI::PropertyFloat("FPS", c.FPS, 0.1f, 0.0f, 120.0f);
            changed |= UI::PropertyBool("Takrorlash", c.Loop);
            changed |= UI::PropertyBool("Ijroda", c.Playing);
            c.Columns = std::max(1, c.Columns); c.Rows = std::max(1, c.Rows);
            c.FrameCount = std::max(1, c.FrameCount); c.StartFrame = std::max(0, c.StartFrame);
            UI::PropertyLabel("Hozirgi kadr");
            ImGui::Text("%d", c.GetCurrentFrame());
            return changed;
        }, true, modified);

        AddSection<CircleRendererComponent>(ICON_FA_CIRCLE, "Circle Renderer", entity, [](CircleRendererComponent& c) {
            bool changed = UI::PropertyColor4("Rang", c.Color);
            changed |= UI::PropertySlider("Qalinlik", c.Thickness, 0.0f, 1.0f);
            changed |= UI::PropertyFloat("Yumshoqlik", c.Fade, 0.00025f, 0.0f, 1.0f);
            changed |= UI::PropertyInt("Tartib (sorting)", c.SortingOrder);
            return changed;
        }, true, modified);

        AddSection<TextComponent>(ICON_FA_FONT, "Text", entity, [](TextComponent& c) {
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
            return changed;
        }, true, modified);

        AddSection<MeshRendererComponent>(ICON_FA_CUBE, "Mesh Renderer", entity, [](MeshRendererComponent& c) {
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
            changed |= UI::PropertyColor4("Rang (albedo)", c.Mat.Albedo);
            const char* imgExt = ".png;.jpg;.jpeg;.bmp;.tga";
            bool maps = UI::PropertyAssetPath("Tekstura", c.AlbedoPath, imgExt, ICON_FA_FILE_IMAGE);
            maps |= UI::PropertyAssetPath("Normal map", c.NormalPath, imgExt, ICON_FA_FILE_IMAGE);
            if (!c.NormalPath.empty())
                changed |= UI::PropertySlider("Normal kuchi", c.Mat.NormalStrength, 0.0f, 3.0f);
            maps |= UI::PropertyAssetPath("Metal/Roughness", c.MetalRoughPath, imgExt, ICON_FA_FILE_IMAGE);
            maps |= UI::PropertyAssetPath("Nurlanish map", c.EmissivePath, imgExt, ICON_FA_FILE_IMAGE);
            if (maps)
            {
                Scene::ResolveMeshRenderer(c);
                changed = true;
            }
            changed |= UI::PropertyFloat("Takrorlash", c.Mat.Tiling, 0.05f, 0.0f, 1000.0f);
            changed |= UI::PropertySlider("Metallik", c.Mat.Metallic, 0.0f, 1.0f);
            changed |= UI::PropertySlider("G'adir-budurlik", c.Mat.Roughness, 0.0f, 1.0f);
            changed |= UI::PropertyColor3("Nurlanish", c.Mat.Emission);
            changed |= UI::PropertyFloat("Nurlanish kuchi", c.Mat.EmissionStrength, 0.05f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Soya tashlaydi", c.CastShadows);
            return changed;
        }, true, modified);

        AddSection<AnimatorComponent>(ICON_FA_PERSON_RUNNING, "Animator", entity, [&](AnimatorComponent& c) {
            bool changed = false;
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

        AddSection<DirectionalLightComponent>(ICON_FA_SUN, "Directional Light", entity, [](DirectionalLightComponent& c) {
            bool changed = UI::PropertyColor3("Rang", c.Color);
            changed |= UI::PropertyFloat("Kuch", c.Intensity, 0.05f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Soyalar", c.CastShadows);
            return changed;
        }, true, modified);

        AddSection<PointLightComponent>(ICON_FA_LIGHTBULB, "Point Light", entity, [](PointLightComponent& c) {
            bool changed = UI::PropertyColor3("Rang", c.Color);
            changed |= UI::PropertyFloat("Kuch", c.Intensity, 0.05f, 0.0f, 1000.0f);
            changed |= UI::PropertyFloat("Radius", c.Radius, 0.05f, 0.0f, 1000.0f);
            return changed;
        }, true, modified);

        AddSection<Rigidbody2DComponent>(ICON_FA_ATOM, "Rigidbody 2D", entity, [](Rigidbody2DComponent& c) {
            bool changed = false;
            const char* types[] = { "Statik", "Dinamik", "Kinematik" };
            int type = (int)c.Type;
            if (UI::PropertyCombo("Turi", type, types, 3)) { c.Type = (Rigidbody2DComponent::BodyType)type; changed = true; }
            changed |= UI::PropertyBool("Aylanmasin", c.FixedRotation);
            changed |= UI::PropertyFloat("Gravitatsiya", c.GravityScale, 0.05f);
            changed |= UI::PropertyFloat("Chiziqli so'nish", c.LinearDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyFloat("Burchak so'nishi", c.AngularDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Tez obyekt (bullet)", c.Bullet);
            return changed;
        }, true, modified);

        auto colliderCommon = [](auto& c) {
            bool changed = UI::PropertyFloat("Zichlik", c.Density, 0.01f, 0.0f, 1000.0f);
            changed |= UI::PropertySlider("Ishqalanish", c.Friction, 0.0f, 1.0f);
            changed |= UI::PropertySlider("Sakrash", c.Restitution, 0.0f, 1.0f);
            changed |= UI::PropertyFloat("Sakrash chegarasi", c.RestitutionThreshold, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Trigger (sensor)", c.IsSensor);
            return changed;
        };

        AddSection<BoxCollider2DComponent>(ICON_FA_VECTOR_SQUARE, "Box Collider 2D", entity, [&](BoxCollider2DComponent& c) {
            bool changed = UI::PropertyFloat2("Siljish", c.Offset);
            changed |= UI::PropertyFloat2("Yarim o'lcham", c.Size);
            changed |= colliderCommon(c);
            return changed;
        }, true, modified);

        AddSection<CircleCollider2DComponent>(ICON_FA_CIRCLE_NODES, "Circle Collider 2D", entity, [&](CircleCollider2DComponent& c) {
            bool changed = UI::PropertyFloat2("Siljish", c.Offset);
            changed |= UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.0f, 1000.0f);
            changed |= colliderCommon(c);
            return changed;
        }, true, modified);

        AddSection<Rigidbody3DComponent>(ICON_FA_ATOM, "Rigidbody 3D", entity, [](Rigidbody3DComponent& c) {
            bool changed = false;
            const char* types[] = { "Statik", "Dinamik", "Kinematik" };
            int type = (int)c.Type;
            if (UI::PropertyCombo("Turi", type, types, 3)) { c.Type = (Rigidbody3DComponent::BodyType)type; changed = true; }
            changed |= UI::PropertyFloat("Massa", c.Mass, 0.05f, 0.001f, 100000.0f);
            changed |= UI::PropertyFloat("Gravitatsiya", c.GravityFactor, 0.05f);
            changed |= UI::PropertyFloat("Chiziqli so'nish", c.LinearDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyFloat("Burchak so'nishi", c.AngularDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Aylanmasin", c.LockRotation);
            return changed;
        }, true, modified);

        auto collider3D = [](auto& c) {
            bool changed = UI::PropertyFloat3("Siljish", c.Offset);
            changed |= UI::PropertySlider("Ishqalanish", c.Friction, 0.0f, 1.0f);
            changed |= UI::PropertySlider("Sakrash", c.Restitution, 0.0f, 1.0f);
            changed |= UI::PropertyBool("Trigger (sensor)", c.IsSensor);
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

        AddSection<CharacterController3DComponent>(ICON_FA_PERSON_RUNNING, "Character Controller 3D", entity, [](CharacterController3DComponent& c) {
            bool changed = UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.05f, 10.0f);
            changed |= UI::PropertyFloat("Balandlik", c.Height, 0.01f, 0.2f, 20.0f);
            changed |= UI::PropertySlider("Maks. qiyalik (°)", c.MaxSlope, 0.0f, 89.0f);
            changed |= UI::PropertyFloat("Zinapoya balandligi", c.StepHeight, 0.01f, 0.0f, 2.0f);
            changed |= UI::PropertyFloat("Massa", c.Mass, 0.5f, 1.0f, 10000.0f);
            changed |= UI::PropertyFloat("Gravitatsiya", c.GravityScale, 0.05f, 0.0f, 10.0f);
            changed |= UI::PropertyBool("Yerga yopishish", c.StickToFloor);
            UI::PropertyLabel("Holat");
            ImGui::TextDisabled(c.Grounded ? "Yerda" : "Havoda");
            return changed;
        }, true, modified);

        AddSection<TilemapComponent>(ICON_FA_TABLE_CELLS, "Tilemap", entity, [&](TilemapComponent& c) {
            bool changed = false;
            if (UI::PropertyAssetPath("Tileset", c.TilesetPath, ".png;.jpg;.jpeg", ICON_FA_FILE_IMAGE)) { c.Tileset = nullptr; changed = true; }
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
            changed |= UI::PropertyInt("Tartib (sorting)", c.SortingOrder);
            changed |= UI::PropertyBool("Collision (fizika)", c.Collision);
            changed |= UI::PropertySlider("Ishqalanish", c.Friction, 0.0f, 1.0f);

            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::SeparatorText("Bo'yash");
            UI::PropertyLabel("Rejim");
            if (ImGui::Checkbox(ICON_FA_PEN "  Viewport'da chizish", &m_TilePaint)) {}
            UI::PropertyLabel("");
            ImGui::TextDisabled("LMB: chizish  |  Shift+LMB: o'chirish");
            UI::PropertyLabel("");
            if (ImGui::Button(ICON_FA_ERASER "  Hammasini tozalash")) { std::fill(c.Tiles.begin(), c.Tiles.end(), -1); changed = true; }

            // palette
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Palitra");
            ImGui::TableSetColumnIndex(1);
            if (!c.Tileset && !c.TilesetPath.empty())
                c.Tileset = TextureLibrary::Load(Project::GetAssetFileSystemPath(c.TilesetPath), c.PixelArt ? TextureFilter::Nearest : TextureFilter::Linear);
            if (c.Tileset)
            {
                float cellPx = std::max(20.0f, std::min(40.0f, (ImGui::GetContentRegionAvail().x - 8) / c.TilesetColumns - 4));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
                for (int ty = 0; ty < c.TilesetRows; ty++)
                {
                    for (int tx = 0; tx < c.TilesetColumns; tx++)
                    {
                        int index = ty * c.TilesetColumns + tx;
                        ImVec2 uv0((float)tx / c.TilesetColumns, 1.0f - (float)ty / c.TilesetRows);
                        ImVec2 uv1((float)(tx + 1) / c.TilesetColumns, 1.0f - (float)(ty + 1) / c.TilesetRows);
                        ImGui::PushID(index);
                        bool selected = m_SelectedTile == index;
                        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentColor());
                        if (ImGui::ImageButton("##tile", (ImTextureID)(uintptr_t)c.Tileset->GetRendererID(), ImVec2(cellPx, cellPx), uv0, uv1))
                        {
                            m_SelectedTile = index;
                            m_TilePaint = true;
                        }
                        if (selected) ImGui::PopStyleColor();
                        ImGui::PopID();
                        if (tx + 1 < c.TilesetColumns) ImGui::SameLine();
                    }
                }
                ImGui::PopStyleVar(2);
            }
            else
                ImGui::TextDisabled("Tileset teksturasini tanlang");
            return changed;
        }, true, modified);

        AddSection<ParticleSystemComponent>(ICON_FA_WAND_MAGIC_SPARKLES, "Particle System", entity, [](ParticleSystemComponent& c) {
            bool changed = false;
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
            changed |= UI::PropertyInt("Tartib (sorting)", c.SortingOrder);
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

        AddSection<UIButtonComponent>(ICON_FA_HAND_POINTER, "UI Button", entity, [](UIButtonComponent& c) {
            bool changed = UI::PropertyColor4("Oddiy", c.NormalColor);
            changed |= UI::PropertyColor4("Ustida (hover)", c.HoverColor);
            changed |= UI::PropertyColor4("Bosilgan", c.PressedColor);
            changed |= UI::PropertyBool("Faol", c.Interactable);
            UI::PropertyLabel("");
            ImGui::TextDisabled("Skriptda: OnClick() yoki .Clicked");
            return changed;
        }, true, modified);

        AddSection<AudioSourceComponent>(ICON_FA_VOLUME_HIGH, "Audio Source", entity, [](AudioSourceComponent& c) {
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
