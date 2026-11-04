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
        if (e.HasComponent<CameraComponent>()) return ICON_FA_CAMERA;
        if (e.HasComponent<DirectionalLightComponent>()) return ICON_FA_SUN;
        if (e.HasComponent<PointLightComponent>()) return ICON_FA_LIGHTBULB;
        if (e.HasComponent<MeshRendererComponent>()) return ICON_FA_CUBE;
        if (e.HasComponent<TextComponent>()) return ICON_FA_FONT;
        if (e.HasComponent<CircleRendererComponent>()) return ICON_FA_CIRCLE;
        if (e.HasComponent<SpriteRendererComponent>()) return ICON_FA_IMAGE;
        if (e.HasComponent<AudioSourceComponent>()) return ICON_FA_VOLUME_HIGH;
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
            if (ImGui::MenuItem(ICON_FA_CUBES "  Model (.obj)")) CreateEntityPreset("Model");
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
        if (ImGui::MenuItem(ICON_FA_VOLUME_HIGH "  Ovoz manbai")) CreateEntityPreset("Audio");
    }

    // ------------------------------------------------------------------ hierarchy
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

        std::vector<entt::entity> ordered;
        for (auto e : m_Context->GetRegistry().view<IDComponent>())
            ordered.push_back(e);
        std::reverse(ordered.begin(), ordered.end());

        std::string filter = ToLower(m_Filter);
        Entity toDelete, toDuplicate;

        ImGui::BeginChild("##entities", ImVec2(0, 0), ImGuiChildFlags_None);
        int row = 0;
        for (auto handle : ordered)
        {
            Entity entity{ handle, m_Context.get() };
            const std::string& tag = entity.GetName();
            if (!filter.empty() && ToLower(tag).find(filter) == std::string::npos)
                continue;

            ImGui::PushID((int)(uint32_t)handle);
            bool selected = m_SelectionContext == entity;

            // Alternating row background
            ImVec2 min = ImGui::GetCursorScreenPos();
            ImVec2 max = ImVec2(min.x + ImGui::GetContentRegionAvail().x, min.y + ImGui::GetFrameHeight());
            if (row++ % 2 == 1)
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
            }
            else
            {
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.52f, 0.41f, 0.75f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.14f, 0.58f, 0.46f, 0.85f));
                }
                std::string label = std::string(EntityIcon(entity)) + "   " + tag;
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0, ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.y)))
                {
                    m_SelectionContext = entity;
                    if (ImGui::IsMouseDoubleClicked(0) && OnFocusRequested)
                        OnFocusRequested(entity);
                }
                if (selected)
                    ImGui::PopStyleColor(2);

                if (ImGui::BeginPopupContextItem())
                {
                    m_SelectionContext = entity;
                    if (ImGui::MenuItem(ICON_FA_FEATHER "  Nomini o'zgartirish", "F2"))
                    {
                        m_RenamingEntity = handle;
                        m_RenameBuffer = tag;
                    }
                    if (ImGui::MenuItem(ICON_FA_CLONE "  Nusxa olish", "Ctrl+D")) toDuplicate = entity;
                    if (ImGui::MenuItem(ICON_FA_BULLSEYE "  Fokus", "F")) { if (OnFocusRequested) OnFocusRequested(entity); }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_FA_TRASH "  O'chirish", "Delete")) toDelete = entity;
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }

        if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
            m_SelectionContext = {};

        if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
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

        // Drop a scene/prefab? (scene files are opened by the editor viewport)
        if (toDuplicate)
        {
            m_SelectionContext = m_Context->DuplicateEntity(toDuplicate);
            MarkModified();
        }
        if (toDelete)
        {
            m_Context->DestroyEntity(toDelete);
            if (m_SelectionContext == toDelete)
                m_SelectionContext = {};
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
                DisplayAddComponentEntry<CircleRendererComponent>("Circle Renderer", ICON_FA_CIRCLE);
                DisplayAddComponentEntry<TextComponent>("Text", ICON_FA_FONT);
                DisplayAddComponentEntry<MeshRendererComponent>("Mesh Renderer", ICON_FA_CUBE);
                DisplayAddComponentEntry<CameraComponent>("Camera", ICON_FA_CAMERA);
                ImGui::SeparatorText("Yorug'lik");
                DisplayAddComponentEntry<DirectionalLightComponent>("Directional Light", ICON_FA_SUN);
                DisplayAddComponentEntry<PointLightComponent>("Point Light", ICON_FA_LIGHTBULB);
                ImGui::SeparatorText("Fizika 2D");
                DisplayAddComponentEntry<Rigidbody2DComponent>("Rigidbody 2D", ICON_FA_ATOM);
                DisplayAddComponentEntry<BoxCollider2DComponent>("Box Collider 2D", ICON_FA_VECTOR_SQUARE);
                DisplayAddComponentEntry<CircleCollider2DComponent>("Circle Collider 2D", ICON_FA_CIRCLE_NODES);
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
            const char* meshes[] = { "Kub", "Shar", "Tekislik", "Silindr", "Konus", "Model (.obj)" };
            int type = (int)c.Type;
            if (UI::PropertyCombo("Mesh", type, meshes, 6))
            {
                c.Type = (PrimitiveMesh)type;
                c.MeshAsset = c.Type == PrimitiveMesh::Model ? nullptr : MeshLibrary::GetPrimitive(c.Type);
                changed = true;
            }
            if (c.Type == PrimitiveMesh::Model)
            {
                if (UI::PropertyAssetPath("Model fayli", c.ModelPath, ".obj", ICON_FA_CUBES) || (!c.MeshAsset && !c.ModelPath.empty() && changed))
                {
                    c.MeshAsset = c.ModelPath.empty() ? nullptr : MeshLibrary::LoadOBJ(Project::GetAssetFileSystemPath(c.ModelPath));
                    changed = true;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SeparatorText("Material");
            changed |= UI::PropertyColor4("Rang (albedo)", c.Mat.Albedo);
            if (UI::PropertyAssetPath("Tekstura", c.AlbedoPath, ".png;.jpg;.jpeg;.bmp;.tga", ICON_FA_FILE_IMAGE))
            {
                c.Mat.AlbedoMap = c.AlbedoPath.empty() ? nullptr : TextureLibrary::Load(Project::GetAssetFileSystemPath(c.AlbedoPath));
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

        DrawComponent<AudioSourceComponent>(ICON_FA_VOLUME_HIGH, "Audio Source", entity, [](AudioSourceComponent& c) {
            bool changed = UI::PropertyAssetPath("Ovoz fayli", c.Path, ".wav;.mp3;.flac;.ogg", ICON_FA_FILE_AUDIO);
            changed |= UI::PropertySlider("Balandlik", c.Volume, 0.0f, 2.0f);
            changed |= UI::PropertySlider("Ton (pitch)", c.Pitch, 0.1f, 3.0f);
            changed |= UI::PropertyBool("Takrorlash", c.Loop);
            changed |= UI::PropertyBool("Boshida ijro", c.PlayOnStart);
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
