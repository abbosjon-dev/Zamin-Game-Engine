#include "SceneHierarchyPanel.h"
#include "../UI.h"

#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>

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

    void SceneHierarchyPanel::SetContext(const Ref<Scene>& context)
    {
        m_Context = context;
        m_SelectionContext = {};
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
                DrawInspector();
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
            m_SelectionContext = e;
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
        m_SelectionContext = entity;
        if (ImGui::BeginMenu(ICON_FA_PLUS "  Bola obyekt yaratish"))
        {
            Entity before = m_SelectionContext;
            DrawCreateMenu();
            Entity created = m_SelectionContext;
            if (created && created != before)
            {
                m_Context->SetParent(created, before, false);
                MarkModified();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_FEATHER "  Nomini o'zgartirish", "F2"))
        {
            m_RenamingEntity = entity;
            m_RenameBuffer = entity.GetName();
        }
        if (ImGui::MenuItem(ICON_FA_CLONE "  Nusxa olish", "Ctrl+D")) m_PendingDuplicate = entity;
        if (ImGui::MenuItem(ICON_FA_BULLSEYE "  Fokus", "F")) { if (OnFocusRequested) OnFocusRequested(entity); }
        if (m_Context->GetParent(entity) && ImGui::MenuItem(ICON_FA_ARROW_UP "  Ota-onadan ajratish"))
        {
            m_Context->SetParent(entity, {}, true);
            MarkModified();
        }
        if (ImGui::MenuItem(ICON_FA_BOX "  Prefab sifatida saqlash") && OnSavePrefabRequested)
            OnSavePrefabRequested(entity);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_TRASH "  O'chirish", "Delete")) m_PendingDelete = entity;
        ImGui::EndPopup();
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity, int depth)
    {
        entt::entity handle = entity;
        const std::string& tag = entity.GetName();
        const auto& children = entity.GetComponent<RelationshipComponent>().Children;
        bool hasChildren = !children.empty() && m_Filter.empty();
        bool selected = m_SelectionContext == entity;

        ImGui::PushID((int)(uint32_t)handle);

        ImVec2 min = ImGui::GetCursorScreenPos();
        ImVec2 max = ImVec2(min.x + ImGui::GetContentRegionAvail().x, min.y + ImGui::GetFrameHeight());
        if (m_RowIndex++ % 2 == 1)
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(255, 255, 255, 6));

        if (m_RenamingEntity == handle)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                if (!m_RenameBuffer.empty())
                    entity.GetComponent<TagComponent>().Tag = m_RenameBuffer;
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

        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.52f, 0.41f, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.14f, 0.58f, 0.46f, 0.85f));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        std::string label = std::string(EntityIcon(entity)) + "  " + tag;
        bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
        ImGui::PopStyleVar();
        if (selected)
            ImGui::PopStyleColor(2);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            m_SelectionContext = entity;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !hasChildren && OnFocusRequested)
            OnFocusRequested(entity);

        // drag & drop: parent entities by dropping onto each other
        if (ImGui::BeginDragDropSource())
        {
            uint64_t id = entity.GetUUID();
            ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &id, sizeof(uint64_t));
            ImGui::Text("%s  %s", EntityIcon(entity), tag.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
            {
                Entity dragged = m_Context->GetEntityByUUID(*(const uint64_t*)payload->Data);
                if (dragged && m_Context->SetParent(dragged, entity, true))
                    MarkModified();
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                std::filesystem::path path((const char*)payload->Data);
                if (path.extension() == ".zprefab" && OnPrefabDropped)
                    OnPrefabDropped(path, entity);
            }
            ImGui::EndDragDropTarget();
        }

        DrawEntityContextMenu(entity);

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

    void SceneHierarchyPanel::DrawHierarchy()
    {
        m_HierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!m_Context)
            return;

        // Header: scene name + add button
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(ICON_FA_EARTH_ASIA);
        ImGui::SameLine();
        ImGui::TextUnformatted(m_Context->GetName().c_str());
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
        if (UI::ToolbarButton(ICON_FA_PLUS, "Obyekt yaratish", false, ImVec2(ImGui::GetFrameHeight(), 0)))
            ImGui::OpenPopup("CreateEntityPopup");
        if (ImGui::BeginPopup("CreateEntityPopup"))
        {
            DrawCreateMenu();
            ImGui::EndPopup();
        }

        UI::SearchBox("HierarchySearch", m_Filter);
        ImGui::Spacing();

        std::string filter = ToLower(m_Filter);
        m_PendingDelete = {};
        m_PendingDuplicate = {};
        m_RowIndex = 0;

        ImGui::BeginChild("##entities", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
        for (Entity entity : m_Context->GetEntitiesInOrder())
        {
            if (filter.empty())
            {
                if ((uint64_t)entity.GetComponent<RelationshipComponent>().Parent && m_Context->GetParent(entity))
                    continue; // drawn by its parent
            }
            else if (ToLower(entity.GetName()).find(filter) == std::string::npos)
                continue;
            DrawEntityNode(entity, 0);
        }
        ImGui::PopStyleVar(2);

        // Empty area: drop to un-parent / instantiate prefabs; click to deselect
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##hierarchyEmpty", ImVec2(std::max(1.0f, avail.x), std::max(40.0f, avail.y)));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            m_SelectionContext = {};
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
            {
                Entity dragged = m_Context->GetEntityByUUID(*(const uint64_t*)payload->Data);
                if (dragged && m_Context->SetParent(dragged, {}, true))
                    MarkModified();
            }
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
            ImGui::EndPopup();
        }

        if (m_SelectionContext && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2))
        {
            m_RenamingEntity = m_SelectionContext;
            m_RenameBuffer = m_SelectionContext.GetName();
        }
        ImGui::EndChild();

        if (m_PendingDuplicate)
        {
            m_SelectionContext = m_Context->DuplicateEntity(m_PendingDuplicate);
            MarkModified();
        }
        if (m_PendingDelete)
        {
            if (m_SelectionContext == m_PendingDelete || (m_SelectionContext && m_Context->IsAncestorOf(m_PendingDelete, m_SelectionContext)))
                m_SelectionContext = {};
            m_Context->DestroyEntity(m_PendingDelete);
            MarkModified();
        }
    }

    // ------------------------------------------------------------------ inspector
    void SceneHierarchyPanel::DrawInspector()
    {
        if (m_SelectionContext && !m_SelectionContext.IsValid())
            m_SelectionContext = {};

        if (!m_SelectionContext)
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
        DrawComponents(m_SelectionContext);
    }

    template<typename T, typename UIFunction>
    static void DrawComponent(const char* icon, const std::string& name, Entity entity, UIFunction uiFunction,
                              bool removable, bool& modified)
    {
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
        if (!entity.HasComponent<T>())
            return;

        auto& component = entity.GetComponent<T>();
        ImVec2 available = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.155f, 0.18f, 1.0f));
        float lineHeight = ImGui::GetFontSize() + 12.0f;
        ImGui::Spacing();
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        std::string title = std::string(icon) + "   " + name;
        bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), flags, "%s", title.c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        bool removeComponent = false;
        if (removable)
        {
            ImGui::SameLine(available.x - lineHeight * 0.75f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button(ICON_FA_GEAR, ImVec2(lineHeight, lineHeight)))
                ImGui::OpenPopup("ComponentSettings");
            ImGui::PopStyleColor();
            if (ImGui::BeginPopup("ComponentSettings"))
            {
                if (ImGui::MenuItem(ICON_FA_TRASH "  Komponentni olib tashlash"))
                    removeComponent = true;
                ImGui::EndPopup();
            }
        }

        if (open)
        {
            ImGui::Indent(4.0f);
            if (UI::BeginPropertyGrid(name.c_str()))
            {
                modified |= uiFunction(component);
                UI::EndPropertyGrid();
            }
            ImGui::Unindent(4.0f);
            ImGui::TreePop();
        }

        if (removeComponent)
        {
            entity.RemoveComponent<T>();
            modified = true;
        }
    }

    template<typename T>
    void SceneHierarchyPanel::DisplayAddComponentEntry(const char* name, const char* icon)
    {
        if (m_SelectionContext.HasComponent<T>())
            return;
        if (!m_AddComponentFilter.empty() && ToLower(name).find(ToLower(m_AddComponentFilter)) == std::string::npos)
            return;
        std::string label = std::string(icon) + "   " + name;
        if (ImGui::MenuItem(label.c_str()))
        {
            T& c = m_SelectionContext.AddComponent<T>();
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
                v.Type = f.Type;
                v.ReadFrom(f.Accessor(tmp));
                defaults[f.Name] = v;
            }
            delete tmp;
        }
        return defaults;
    }

    static bool DrawFieldValue(const char* name, ScriptFieldType type, void* data)
    {
        switch (type)
        {
            case ScriptFieldType::Float:  return UI::PropertyFloat(name, *(float*)data, 0.05f, 0.0f, 0.0f);
            case ScriptFieldType::Int:    return UI::PropertyInt(name, *(int*)data);
            case ScriptFieldType::Bool:   return UI::PropertyBool(name, *(bool*)data);
            case ScriptFieldType::Vec2:   return UI::PropertyFloat2(name, *(glm::vec2*)data);
            case ScriptFieldType::Vec3:   return UI::PropertyFloat3(name, *(glm::vec3*)data);
            case ScriptFieldType::Vec4:   return UI::PropertyColor4(name, *(glm::vec4*)data);
            case ScriptFieldType::String: return UI::PropertyString(name, *(std::string*)data);
            default: return false;
        }
    }

    void SceneHierarchyPanel::DrawScriptComponent(Entity entity)
    {
        bool modified = false;
        DrawComponent<NativeScriptComponent>(ICON_FA_CODE, "C++ Skript", entity, [&](NativeScriptComponent& c) {
            bool changed = false;
            const auto& all = ScriptRegistry::GetAll();

            UI::PropertyLabel("Klass");
            bool exists = ScriptRegistry::Find(c.ClassName) != nullptr;
            if (!exists && !c.ClassName.empty())
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
            std::string preview = c.ClassName.empty() ? "(tanlang)" : c.ClassName;
            if (ImGui::BeginCombo("##class", preview.c_str()))
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

            const ScriptClassInfo* info = ScriptRegistry::Find(c.ClassName);
            if (!info)
                return changed;

            if (c.Instance)
            {
                // Live editing while playing
                for (const auto& f : info->Fields)
                    DrawFieldValue(f.Name.c_str(), f.Type, f.Accessor(c.Instance));
                return changed;
            }

            const auto& defaults = GetScriptDefaults(c.ClassName);
            for (const auto& f : info->Fields)
            {
                auto it = c.Fields.find(f.Name);
                ScriptFieldValue value;
                if (it != c.Fields.end() && it->second.Type == f.Type) value = it->second;
                else if (auto d = defaults.find(f.Name); d != defaults.end()) value = d->second;
                else value.Type = f.Type;

                // Edit a temporary of the right type, then store it.
                bool fieldChanged = false;
                switch (f.Type)
                {
                    case ScriptFieldType::String: fieldChanged = DrawFieldValue(f.Name.c_str(), f.Type, &value.StringValue); break;
                    default: fieldChanged = DrawFieldValue(f.Name.c_str(), f.Type, value.Data); break;
                }
                if (fieldChanged)
                {
                    c.Fields[f.Name] = value;
                    changed = true;
                }
            }
            return changed;
        }, true, modified);
        if (modified) MarkModified();
    }

    static bool DrawAnimatorStateMachine(AnimatorComponent& c, const Ref<Model>& model)
    {
        bool changed = false;
        ImGui::PushID("asm");
        if (!ImGui::CollapsingHeader(ICON_FA_DIAGRAM_PROJECT "  Animator holat mashinasi"))
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
            float addWidth = ImGui::CalcTextSize(ICON_FA_PLUS "  Komponent").x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addWidth - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::InputText("##Tag", &tag))
                modified = true;
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
                ImGui::EndPopup();
            }
            ImGui::TextDisabled("ID: %llu", (unsigned long long)(uint64_t)entity.GetUUID());
        }

        // ---- transform
        DrawComponent<TransformComponent>(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Transform", entity, [](TransformComponent& c) {
            bool changed = UI::Vec3Control("Pozitsiya", c.Translation);
            glm::vec3 rotation = glm::degrees(c.Rotation);
            if (UI::Vec3Control("Aylanish", rotation, 0.0f, 0.5f))
            {
                c.Rotation = glm::radians(rotation);
                changed = true;
            }
            changed |= UI::Vec3Control("Masshtab", c.Scale, 1.0f);
            return changed;
        }, false, modified);

        DrawComponent<CameraComponent>(ICON_FA_CAMERA, "Camera", entity, [](CameraComponent& c) {
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

        DrawComponent<SpriteRendererComponent>(ICON_FA_IMAGE, "Sprite Renderer", entity, [](SpriteRendererComponent& c) {
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

        DrawComponent<SpriteAnimationComponent>(ICON_FA_FILM, "Sprite Animation", entity, [](SpriteAnimationComponent& c) {
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

        DrawComponent<CircleRendererComponent>(ICON_FA_CIRCLE, "Circle Renderer", entity, [](CircleRendererComponent& c) {
            bool changed = UI::PropertyColor4("Rang", c.Color);
            changed |= UI::PropertySlider("Qalinlik", c.Thickness, 0.0f, 1.0f);
            changed |= UI::PropertyFloat("Yumshoqlik", c.Fade, 0.00025f, 0.0f, 1.0f);
            changed |= UI::PropertyInt("Tartib (sorting)", c.SortingOrder);
            return changed;
        }, true, modified);

        DrawComponent<TextComponent>(ICON_FA_FONT, "Text", entity, [](TextComponent& c) {
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

        DrawComponent<MeshRendererComponent>(ICON_FA_CUBE, "Mesh Renderer", entity, [](MeshRendererComponent& c) {
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

        DrawComponent<AnimatorComponent>(ICON_FA_PERSON_RUNNING, "Animator", entity, [&](AnimatorComponent& c) {
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

        if (auto* anim = entity.TryGetComponent<AnimatorComponent>())
        {
            auto* mr = entity.TryGetComponent<MeshRendererComponent>();
            modified |= DrawAnimatorStateMachine(*anim, mr ? mr->ModelAsset : nullptr);
        }

        DrawComponent<DirectionalLightComponent>(ICON_FA_SUN, "Directional Light", entity, [](DirectionalLightComponent& c) {
            bool changed = UI::PropertyColor3("Rang", c.Color);
            changed |= UI::PropertyFloat("Kuch", c.Intensity, 0.05f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Soyalar", c.CastShadows);
            return changed;
        }, true, modified);

        DrawComponent<PointLightComponent>(ICON_FA_LIGHTBULB, "Point Light", entity, [](PointLightComponent& c) {
            bool changed = UI::PropertyColor3("Rang", c.Color);
            changed |= UI::PropertyFloat("Kuch", c.Intensity, 0.05f, 0.0f, 1000.0f);
            changed |= UI::PropertyFloat("Radius", c.Radius, 0.05f, 0.0f, 1000.0f);
            return changed;
        }, true, modified);

        DrawComponent<Rigidbody2DComponent>(ICON_FA_ATOM, "Rigidbody 2D", entity, [](Rigidbody2DComponent& c) {
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

        DrawComponent<BoxCollider2DComponent>(ICON_FA_VECTOR_SQUARE, "Box Collider 2D", entity, [&](BoxCollider2DComponent& c) {
            bool changed = UI::PropertyFloat2("Siljish", c.Offset);
            changed |= UI::PropertyFloat2("Yarim o'lcham", c.Size);
            changed |= colliderCommon(c);
            return changed;
        }, true, modified);

        DrawComponent<CircleCollider2DComponent>(ICON_FA_CIRCLE_NODES, "Circle Collider 2D", entity, [&](CircleCollider2DComponent& c) {
            bool changed = UI::PropertyFloat2("Siljish", c.Offset);
            changed |= UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.0f, 1000.0f);
            changed |= colliderCommon(c);
            return changed;
        }, true, modified);

        DrawComponent<Rigidbody3DComponent>(ICON_FA_ATOM, "Rigidbody 3D", entity, [](Rigidbody3DComponent& c) {
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

        DrawComponent<BoxCollider3DComponent>(ICON_FA_CUBE, "Box Collider 3D", entity, [&](BoxCollider3DComponent& c) {
            bool changed = UI::PropertyFloat3("Yarim o'lcham", c.HalfExtents);
            return collider3D(c) || changed;
        }, true, modified);

        DrawComponent<SphereCollider3DComponent>(ICON_FA_CIRCLE, "Sphere Collider 3D", entity, [&](SphereCollider3DComponent& c) {
            bool changed = UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.001f, 1000.0f);
            return collider3D(c) || changed;
        }, true, modified);

        DrawComponent<CapsuleCollider3DComponent>(ICON_FA_CAPSULES, "Capsule Collider 3D", entity, [&](CapsuleCollider3DComponent& c) {
            bool changed = UI::PropertyFloat("Radius", c.Radius, 0.01f, 0.001f, 1000.0f);
            changed |= UI::PropertyFloat("Yarim balandlik", c.HalfHeight, 0.01f, 0.0f, 1000.0f);
            return collider3D(c) || changed;
        }, true, modified);

        DrawComponent<CharacterController3DComponent>(ICON_FA_PERSON_RUNNING, "Character Controller 3D", entity, [](CharacterController3DComponent& c) {
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

        DrawComponent<TilemapComponent>(ICON_FA_TABLE_CELLS, "Tilemap", entity, [&](TilemapComponent& c) {
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

        DrawComponent<ParticleSystemComponent>(ICON_FA_WAND_MAGIC_SPARKLES, "Particle System", entity, [](ParticleSystemComponent& c) {
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

        DrawComponent<UITransformComponent>(ICON_FA_TABLE_CELLS, "UI Transform", entity, [](UITransformComponent& c) {
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

        DrawComponent<UIButtonComponent>(ICON_FA_HAND_POINTER, "UI Button", entity, [](UIButtonComponent& c) {
            bool changed = UI::PropertyColor4("Oddiy", c.NormalColor);
            changed |= UI::PropertyColor4("Ustida (hover)", c.HoverColor);
            changed |= UI::PropertyColor4("Bosilgan", c.PressedColor);
            changed |= UI::PropertyBool("Faol", c.Interactable);
            UI::PropertyLabel("");
            ImGui::TextDisabled("Skriptda: OnClick() yoki .Clicked");
            return changed;
        }, true, modified);

        DrawComponent<AudioSourceComponent>(ICON_FA_VOLUME_HIGH, "Audio Source", entity, [](AudioSourceComponent& c) {
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

        if (modified) MarkModified();
        DrawScriptComponent(entity);
    }

}
