#include <Zamin/Scene/TerrainFoliage.h>
#include <Zamin/Scene/TerrainLOD.h>
#include <Zamin/Scene/TerrainTiles.h>
#include <Zamin/Renderer/TerrainSurface.h>
#include "EditorLayer.h"
#include "UI.h"
#include <Zamin/Utils/PlatformUtils.h>
#include <Zamin/Utils/FileSystem.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <stdexcept>

namespace Zamin {
    namespace {
        bool s_AutoReprojectFoliage = false;
        TerrainReprojectOptions s_FoliageOptions;
    }
    Entity EditorLayer::TerrainEntity()
    {
        if (!m_EditorScene) return {};
        auto selected = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selected && selected.GetScene() == m_EditorScene.get() && selected.IsValid() && selected.HasComponent<TerrainComponent>()) m_TerrainID = selected.GetUUID();
        auto entity = m_EditorScene->GetEntityByUUID(m_TerrainID);
        return entity && entity.HasComponent<TerrainComponent>() ? entity : Entity();
    }
    Entity EditorLayer::CreateTerrainMap()
    {
        if (!m_EditorScene || m_SceneState != SceneState::Edit) return {};
        CommitUndo();
        auto entity = m_EditorScene->CreateEntity("Terrain");
        auto& terrain = entity.AddComponent<TerrainComponent>();
        terrain.Reset(uint32_t(m_TerrainResolution), m_TerrainSize, m_TerrainBase);
        entity.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
        entity.AddComponent<MeshCollider3DComponent>();
        m_TerrainID = entity.GetUUID();
        m_SceneHierarchyPanel.SetSelectedEntity(entity);
        Set2DMode(false);
        m_EditorCamera.Focus({ m_TerrainSize.x * 0.5f, m_TerrainBase, m_TerrainSize.y * 0.5f }, glm::length(m_TerrainSize) * 0.8f);
        m_EditorCamera.SetOrientation(0.65f, 0.7f);
        m_ShowTerrain = true;
        MarkDirty("Terrain yaratish");
        return entity;
    }
    bool EditorLayer::TerrainMouseHit(Entity entity, TerrainHit& hit)
    {
        if (!entity || m_ViewportSize.x <= 0 || m_ViewportSize.y <= 0) return false;
        auto mouse = ImGui::GetMousePos();
        float x = (mouse.x - m_ViewportBounds[0].x) / m_ViewportSize.x, y = (mouse.y - m_ViewportBounds[0].y) / m_ViewportSize.y;
        if (x < 0 || x > 1 || y < 0 || y > 1) return false;
        auto inverse = glm::inverse(m_EditorCamera.GetViewProjection());
        auto a = inverse * glm::vec4(x * 2 - 1, 1 - y * 2, -1, 1), b = inverse * glm::vec4(x * 2 - 1, 1 - y * 2, 1, 1);
        glm::vec3 origin = glm::vec3(a) / a.w, dir = glm::normalize(glm::vec3(b) / b.w - origin);
        auto world = m_EditorScene->GetWorldTransform(entity);
        if (std::abs(glm::determinant(world)) < 1e-8f) return false;
        auto inv = glm::inverse(world);
        return TerrainTools::Raycast(entity.GetComponent<TerrainComponent>(), glm::vec3(inv * glm::vec4(origin, 1)), glm::vec3(inv * glm::vec4(dir, 0)), hit);
    }
    void EditorLayer::ScatterTerrainObjects(Entity entity, glm::vec2 center, bool erase)
    {
        auto& terrain = entity.GetComponent<TerrainComponent>();
        auto world = m_EditorScene->GetWorldTransform(entity);
        std::vector<Entity> objects;
        for (auto handle : m_EditorScene->GetAllEntitiesWith<TagsComponent, RelationshipComponent>())
        {
            Entity e(handle, m_EditorScene.get());
            if (e.GetComponent<RelationshipComponent>().Parent == entity.GetUUID() && e.GetComponent<TagsComponent>().Has("TerrainPainted")) objects.push_back(e);
        }
        if (erase)
        {
            std::vector<Entity> removed;
            for (auto e : objects)
            {
                glm::vec3 p = glm::vec3(glm::inverse(world) * m_EditorScene->GetWorldTransform(e)[3]);
                if (glm::length(glm::vec2(p.x, p.z) - center) <= m_TerrainBrush.Radius) removed.push_back(e);
            }
            for (auto e : removed) m_EditorScene->DestroyEntity(e);
            if (!removed.empty()) MarkDirty("Map obyektlarini o'chirish");
            return;
        }
        if (objects.size() >= 5000 || m_TerrainPlaced >= 128) return;
        std::mt19937 random(m_TerrainRandom++);
        std::uniform_real_distribution<float> unit(0, 1);
        const int attempts = std::clamp(m_TerrainDensity, 1, 16);
        for (int i = 0; i < attempts && m_TerrainPlaced < 128; ++i)
        {
            float angle = unit(random) * 6.2831853f, radius = std::sqrt(unit(random)) * m_TerrainBrush.Radius;
            glm::vec2 p = center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
            if (p.x < 0 || p.y < 0 || p.x > terrain.Size.x || p.y > terrain.Size.y) continue;
            auto normal = TerrainTools::SampleNormal(terrain, p.x, p.y);
            if (normal.y < std::cos(glm::radians(m_TerrainMaxSlope))) continue;
            glm::vec3 local(p.x, TerrainTools::SampleHeight(terrain, p.x, p.y), p.y), position = glm::vec3(world * glm::vec4(local, 1));
            bool close = false;
            for (auto e : objects)
            {
                auto a = glm::vec3(m_EditorScene->GetWorldTransform(e)[3]);
                if (glm::length(a - position) < m_TerrainSpacing)
                {
                    close = true;
                    break;
                }
            }
            if (close) continue;
            Entity root;
            if (m_TerrainObject == 2)
            {
                if (m_TerrainPrefab.empty()) break;
                root = m_EditorScene->InstantiatePrefab(Project::GetAssetFileSystemPath(m_TerrainPrefab));
                if (!root) break;
            }
            else
            {
                root = m_EditorScene->CreateEntity(m_TerrainObject == 0 ? "Map daraxti" : "Map toshi");
                auto part = [&](const char* name, PrimitiveMesh mesh, glm::vec3 offset, glm::vec3 scale, glm::vec4 color) {auto e=m_EditorScene->CreateEntity(name);auto& mr=e.AddComponent<MeshRendererComponent>();mr.Type=mesh;mr.Mat.Albedo=color;mr.Mat.Roughness=0.95f;e.GetTransform().Translation=offset;e.GetTransform().Scale=scale;m_EditorScene->SetParent(e,root,false); };
                if (m_TerrainObject == 0)
                {
                    part("Tana", PrimitiveMesh::Cylinder, { 0, 1.1f, 0 }, { 0.35f, 2.2f, 0.35f }, { 0.29f, 0.15f, 0.07f, 1 });
                    part("Shoxlar", PrimitiveMesh::Cone, { 0, 3, 0 }, { 2.7f, 3.8f, 2.7f }, { 0.13f, 0.35f, 0.10f, 1 });
                    root.AddComponent<BoxCollider3DComponent>().HalfExtents = { 0.22f, 1.3f, 0.22f };
                    root.GetComponent<BoxCollider3DComponent>().Offset = { 0, 1.3f, 0 };
                }
                else
                {
                    part("Tosh mesh", PrimitiveMesh::Sphere, { 0, 0.35f, 0 }, { 1.4f, 0.9f, 1 }, { 0.43f, 0.44f, 0.46f, 1 });
                    root.AddComponent<BoxCollider3DComponent>().HalfExtents = { 0.6f, 0.4f, 0.45f };
                    root.GetComponent<BoxCollider3DComponent>().Offset = { 0, 0.35f, 0 };
                }
                root.AddComponent<Rigidbody3DComponent>().Type = Rigidbody3DComponent::BodyType::Static;
            }
            root.GetTransform().Translation = position;
            float scale = glm::mix(m_TerrainScaleMin, m_TerrainScaleMax, unit(random));
            root.GetTransform().Scale *= scale;
            auto rotation = glm::angleAxis(unit(random) * 6.2831853f, glm::vec3(0, 1, 0));
            if (m_TerrainAlign)
            {
                auto n = glm::normalize(glm::mat3(glm::transpose(glm::inverse(world))) * normal);
                rotation = glm::rotation(glm::vec3(0, 1, 0), n) * rotation;
            }
            root.GetTransform().Rotation = glm::eulerAngles(rotation);
            m_EditorScene->SetParent(root, entity, true);
            if (!root.HasComponent<TagsComponent>()) root.AddComponent<TagsComponent>();
            root.GetComponent<TagsComponent>().Add("TerrainPainted");
            objects.push_back(root);
            ++m_TerrainPlaced;
            MarkDirty("Map obyektlarini joylash");
        }
    }
    void EditorLayer::UpdateTerrainPainting(Timestep ts)
    {
        if (!m_ShowTerrain || !m_TerrainPainting || m_SceneState != SceneState::Edit)
        {
            m_TerrainStroke = false;
            return;
        }
        bool down = Input::IsMouseButtonPressed(MouseButton::Left);
        if (!down)
        {
            if (m_TerrainStroke)
            {
                m_TerrainStroke = false;
                CommitUndo();
            }
            return;
        }
        if (!m_ViewportHovered || Input::IsKeyPressed(Key::LeftAlt) || Input::IsKeyPressed(Key::RightAlt) || m_EditorCamera.IsFlying()) return;
        auto entity = TerrainEntity();
        if (!entity || m_EditorScene->IsLockedInEditor(entity) || m_EditorScene->IsHiddenInEditor(entity)) return;
        TerrainHit hit;
        if (!TerrainMouseHit(entity, hit)) return;
        if (!m_TerrainStroke)
        {
            CommitUndo();
            m_TerrainStroke = true;
            m_TerrainPlaced = 0;
            m_TerrainLastStamp = -1;
            m_TerrainHasPrevious = false;
        }
        if (Input::IsKeyPressed(Key::LeftControl))
        {
            m_TerrainBrush.TargetHeight = hit.Position.y;
            return;
        }
        if (m_TerrainTool >= 5)
        {
            double now = ImGui::GetTime();
            if (now - m_TerrainLastStamp >= 0.15)
            {
                ScatterTerrainObjects(entity, { hit.Position.x, hit.Position.z }, m_TerrainTool == 6);
                m_TerrainLastStamp = now;
            }
            return;
        }
        auto settings = m_TerrainBrush;
        settings.Tool = TerrainBrush(m_TerrainTool);
        if (Input::IsKeyPressed(Key::LeftShift))
        {
            if (settings.Tool == TerrainBrush::Raise) settings.Tool = TerrainBrush::Lower;
            else if (settings.Tool == TerrainBrush::Lower) settings.Tool = TerrainBrush::Raise;
        }
        glm::vec2 center(hit.Position.x, hit.Position.z);
        if (!m_TerrainHasPrevious) m_TerrainPreviousCenter = center;
        int stamps = std::clamp(int(std::ceil(glm::length(center - m_TerrainPreviousCenter) / std::max(settings.Radius * 0.25f, 0.1f))), 1, 32);
        bool changed = false;
        for (int i = 1; i <= stamps; ++i) changed |= TerrainTools::ApplyBrush(entity.GetComponent<TerrainComponent>(), glm::mix(m_TerrainPreviousCenter, center, float(i) / float(stamps)), settings, float(ts) / float(stamps));
        if (changed)
        {
            if (s_AutoReprojectFoliage && settings.Tool != TerrainBrush::Paint)
            {
                auto options = s_FoliageOptions;
                options.AlignToNormal = m_TerrainAlign;
                options.Center = (m_TerrainPreviousCenter + center) * .5f;
                options.Radius = settings.Radius + glm::length(m_TerrainPreviousCenter - center) * .5f + glm::length(entity.GetComponent<TerrainComponent>().Size) / float(entity.GetComponent<TerrainComponent>().Resolution - 1) * 2;
                TerrainFoliage::Reproject(*m_EditorScene, entity, options);
            }
            MarkDirty("Terrain cho'tkasi");
        }
        m_TerrainPreviousCenter = center;
        m_TerrainHasPrevious = true;
    }
    void EditorLayer::DrawTerrainPreview()
    {
        if (!m_ShowTerrain || !m_TerrainPainting || m_SceneState != SceneState::Edit || !m_ViewportHovered) return;
        auto entity = TerrainEntity();
        TerrainHit hit;
        if (!entity || !TerrainMouseHit(entity, hit)) return;
        auto& terrain = entity.GetComponent<TerrainComponent>();
        auto world = m_EditorScene->GetWorldTransform(entity);
        glm::vec3 previous;
        for (int i = 0; i <= 64; ++i)
        {
            float a = float(i) / 64 * 6.2831853f;
            glm::vec2 p = glm::vec2(hit.Position.x, hit.Position.z) + glm::vec2(std::cos(a), std::sin(a)) * m_TerrainBrush.Radius;
            glm::vec3 point = glm::vec3(world * glm::vec4(p.x, TerrainTools::SampleHeight(terrain, p.x, p.y) + 0.08f, p.y, 1));
            if (i) Renderer2D::DrawLine(previous, point, { 1, 0.8f, 0.15f, 1 });
            previous = point;
        }
    }
    void EditorLayer::UI_Terrain()
    {
        if (!m_ShowTerrain) return;
        ImGui::SetNextWindowSize({ 430, 700 }, ImGuiCond_FirstUseEver);
        const char* qaMode = std::getenv("ZAMIN_EDITOR_TERRAIN");
        if (qaMode && (std::string_view(qaMode) == "material-test" || std::string_view(qaMode) == "foliage-test"))
        {
            ImGui::SetNextWindowPos({ 1070, 60 }, ImGuiCond_Once);
            ImGui::SetNextWindowSize({ 510, 800 }, ImGuiCond_Once);
        }
        if (!ImGui::Begin("Map tayyorlash###TerrainEditor", &m_ShowTerrain))
        {
            ImGui::End();
            return;
        }
        ImGui::BeginDisabled(m_SceneState != SceneState::Edit);
        ImGui::TextWrapped("3D yer, tekstura va o'rmon. Har bir cho'tka bosishi - bitta Undo qadami.");
        ImGui::SeparatorText("Yer yaratish");
        ImGui::DragFloat2("O'lcham (m)", &m_TerrainSize.x, 1, 1, 4096);
        const char* resolutions[] = { "33 x 33", "65 x 65", "129 x 129", "257 x 257" };
        int r = m_TerrainResolution == 33 ? 0 : m_TerrainResolution == 65 ? 1
                                            : m_TerrainResolution == 129  ? 2
                                                                          : 3;
        if (ImGui::Combo("Nuqtalar", &r, resolutions, 4)) m_TerrainResolution = (1 << (r + 5)) + 1;
        ImGui::DragFloat("Boshlang'ich balandlik", &m_TerrainBase, 0.1f, 0, 128);
        if (ImGui::Button("Yangi terrain qo'shish")) CreateTerrainMap();
        auto entity = TerrainEntity();
        if (ImGui::BeginCombo("Faol yer", entity ? entity.GetName().c_str() : "Tanlang"))
        {
            for (auto h : m_EditorScene->GetAllEntitiesWith<TerrainComponent>())
            {
                Entity e(h, m_EditorScene.get());
                if (ImGui::Selectable(e.GetName().c_str(), e == entity))
                {
                    m_TerrainID = e.GetUUID();
                    m_SceneHierarchyPanel.SetSelectedEntity(e);
                    entity = e;
                }
            }
            ImGui::EndCombo();
        }
        if (entity)
        {
            auto& t = entity.GetComponent<TerrainComponent>();
            t.EnsureSize();
            ImGui::Text("%u x %u | %.0f x %.0f metr", t.Resolution, t.Resolution, t.Size.x, t.Size.y);
            if (ImGui::Button("Tanlangan yerni ko'rsatish"))
            {
                auto w = m_EditorScene->GetWorldTransform(entity);
                m_EditorCamera.Focus(glm::vec3(w * glm::vec4(t.Size.x * 0.5f, TerrainTools::SampleHeight(t, t.Size.x * 0.5f, t.Size.y * 0.5f), t.Size.y * 0.5f, 1)), glm::length(t.Size) * 0.8f);
                Set2DMode(false);
            }
            if (const char* mode = std::getenv("ZAMIN_EDITOR_TERRAIN"); mode && std::string(mode) == "lod-test")
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Terrain unumdorligi (LOD)"))
            {
                bool changed = ImGui::Checkbox("Avtomatik LOD", &t.LODEnabled);
                changed |= ImGui::Checkbox("Katak chegarasida toliq detal", &t.LockBorderLOD);
                changed |= ImGui::SliderFloat("Detal masofasi", &t.LODDistanceScale, 0.25f, 8.0f, "%.2f");
                if (changed) MarkDirty("Terrain LOD sozlamalari");
                const auto stats = TerrainTools::GetRenderStats(t);
                ImGui::Text("Oxirgi ko'rinish: %u bo'lak, %u uchburchak", stats.Patches, stats.Triangles);
                ImGui::Text("To'liq detal: %u | Qayta qurildi: %u", stats.FullResolutionTriangles, stats.RebuiltPatches);
                ImGui::TextWrapped("Masofa oshganda detal kamayadi. Collision va cho'tka to'liq aniqlikda qoladi. Ko'proq detal uchun masofa koeffitsiyentini oshiring.");
            }
            if (qaMode && std::string_view(qaMode) == "world-test") ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("World terrain / katak chegaralari"))
            {
                static TerrainTools::WorldTerrainSettings settings{ 42, 12, 6, .02f };
                static std::string result;
                static int blendRows = 2;
                ImGui::InputScalar("World seed", ImGuiDataType_U32, &settings.Seed);
                ImGui::DragFloat("World base", &settings.Base, .1f);
                ImGui::DragFloat("World amplitude", &settings.Amplitude, .1f, 0, 100);
                ImGui::DragFloat("World frequency", &settings.Frequency, .001f, .001f, 1);
                ImGui::SliderInt("Chegara blend rows", &blendRows, 0, 16);
                auto translationOnly = [](const glm::mat4& world) {
                    for (int c = 0; c < 3; ++c)
                        for (int r = 0; r < 3; ++r)
                            if (std::abs(world[c][r] - (c == r ? 1.f : 0.f)) > 1e-5f) return false;
                    return true;
                };
                const auto world = m_EditorScene->GetWorldTransform(entity);
                if (ImGui::Button("World koordinatalardan yaratish"))
                {
                    if (!translationOnly(world)) result = "Faqat translation: rotation/scale ni avval bake qiling";
                    else
                    {
                        CommitUndo();
                        if (TerrainTools::GenerateWorldTile(t, glm::dvec3(world[3]), settings, result))
                        {
                            MarkDirty("World terrain generate");
                            result = "World terrain yaratildi";
                        }
                    }
                }
                static UUID neighborID{ 0 };
                auto neighbor = m_EditorScene->GetEntityByUUID(neighborID);
                if (ImGui::BeginCombo("Qo'shni terrain", neighbor && neighbor != entity ? neighbor.GetName().c_str() : "Tanlang"))
                {
                    for (auto handle : m_EditorScene->GetAllEntitiesWith<TerrainComponent>())
                    {
                        Entity choice(handle, m_EditorScene.get());
                        if (choice == entity) continue;
                        ImGui::PushID(choice.GetUUID().ToString().c_str());
                        if (ImGui::Selectable(choice.GetName().c_str(), choice.GetUUID() == neighborID)) neighborID = choice.GetUUID();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Tanlangan ikki terrain chegarasini ulash"))
                {
                    neighbor = m_EditorScene->GetEntityByUUID(neighborID);
                    if (!neighbor || neighbor == entity || !neighbor.HasComponent<TerrainComponent>()) result = "Qo'shni terrain tanlang";
                    else
                    {
                        const auto neighborWorld = m_EditorScene->GetWorldTransform(neighbor);
                        if (!translationOnly(world) || !translationOnly(neighborWorld)) result = "Faqat translation terrain qo'llanadi";
                        else
                        {
                            CommitUndo();
                            if (TerrainTools::StitchTiles(t, glm::dvec3(world[3]), neighbor.GetComponent<TerrainComponent>(),
                                                          glm::dvec3(neighborWorld[3]), uint32_t(blendRows), result))
                            {
                                MarkDirty("World terrain seam stitch");
                                result = "Terrain chegarasi ulandi";
                            }
                        }
                    }
                }
                if (!result.empty()) ImGui::TextWrapped("%s", result.c_str());
                ImGui::TextWrapped("Bir xil world seed/base/frequency ishlating. Stitch balandlikni ulaydi; normal shading har bir katakda alohida. Undo orqali qaytariladi.");
            }
            bool edit = ImGui::Checkbox("Cho'tka faol", &m_TerrainPainting);
            if (edit && m_TerrainPainting) m_GizmoMode = GizmoMode::None;
            const char* tools[] = { "Ko'tarish", "Pasaytirish", "Tekislash", "Silliqlash", "Tekstura bo'yash", "Obyekt ekish", "Obyekt o'chirish" };
            ImGui::Combo("Asbob", &m_TerrainTool, tools, 7);
            ImGui::SliderFloat("Radius (m)", &m_TerrainBrush.Radius, 0.25f, 64, "%.2f");
            ImGui::SliderFloat("Kuch", &m_TerrainBrush.Strength, 0.1f, 30, "%.2f");
            ImGui::SliderFloat("Qattiqlik", &m_TerrainBrush.Hardness, 0, 0.95f);
            if (m_TerrainTool == 2) ImGui::DragFloat("Maqsad balandlik", &m_TerrainBrush.TargetHeight, 0.1f, 0, t.HeightLimit);
            ImGui::TextWrapped("LMB: chizish. Shift: ko'tarish/pasaytirishni almashtirish. Ctrl: balandlikni olish. Alt/RMB: kamera.");
            if (qaMode && std::string_view(qaMode) == "foliage-test") ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Yer qatlamlari", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const char* layers[] = { "Maysa", "Tuproq / yo'l", "Qoya", "Qum" };
                ImGui::Combo("Bo'yoq", &m_TerrainBrush.Layer, layers, 4);
                auto& l = t.Layers[size_t(m_TerrainBrush.Layer)];
                bool changed = ImGui::ColorEdit3("Rang", &l.Color.x);
                changed |= ImGui::DragFloat("Tekstura takrori", &l.Tiling, 0.01f, 0.001f, 100);
                auto mapField = [&](const char* label, std::string& value) {
                    ImGui::PushID(label);
                    changed |= ImGui::InputText(label, &value);
                    if (ImGui::Button("Tanlash"))
                    {
                        auto path = FileDialogs::OpenFile("Rasm (*.png *.jpg) | *.png *.jpg", Project::GetAssetDirectory());
                        if (!path.empty())
                        {
                            if (FileSystem::IsSubPathOnDisk(path, Project::GetAssetDirectory()))
                            {
                                value = Project::GetAssetRelativePath(path);
                                changed = true;
                            }
                            else m_TerrainMessage = "Teksturani loyihaning Assets papkasiga ko'chiring.";
                        }
                    }
                    ImGui::PopID();
                };
                mapField("Rang teksturasi", l.TexturePath);
                mapField("Normal xarita", l.NormalMapPath);
                mapField("ORM xarita", l.MaskMapPath);
                ImGui::TextDisabled("ORM: R = soya, G = dag'allik, B = metall");
                changed |= ImGui::SliderFloat("Dag'allik", &l.Roughness, 0.04f, 1.0f);
                changed |= ImGui::SliderFloat("Metall", &l.Metallic, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Occlusion", &l.Occlusion, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Normal kuchi", &l.NormalStrength, 0.0f, 4.0f);
                if (ImGui::Button("Teksturalarni qayta yuklash"))
                {
                    bool failed = false;
                    size_t count = 0;
                    for (const auto& layer : t.Layers)
                        for (const auto& relative : { layer.TexturePath, layer.NormalMapPath, layer.MaskMapPath })
                        {
                            if (relative.empty()) continue;
                            auto path = FileSystem::SafeJoin(Project::GetAssetDirectory(), relative);
                            if (!path.empty() && FileSystem::IsSubPathOnDisk(path, Project::GetAssetDirectory()))
                            {
                                bool oneFailed = false;
                                count += TextureLibrary::Reload(path, &oneFailed);
                                failed |= oneFailed;
                            }
                            else failed = true;
                        }
                    m_TerrainMessage = failed ? "Ayrim xaritalar yuklanmadi. Oldingi ishlaydigan tekstura saqlandi; yo'llarni va import logini tekshiring."
                                              : std::to_string(count) + " tekstura qayta yuklandi.";
                }
                if (auto surface = TerrainTools::GetSurface(t))
                {
                    ImGui::Text("GPU control: %.1f KiB | yuklash: %llu", surface->ControlBytes / 1024.0f,
                                (unsigned long long)surface->WeightUploads);
                    if (surface->MissingMaps)
                        ImGui::TextWrapped("Topilmagan / xavfsiz bo'lmagan xaritalar: %u. Rang va PBR qiymatlari ishlatiladi.", surface->MissingMaps);
                }
                if (changed)
                {
                    t.PaintChanged();
                    MarkDirty("Terrain materiali");
                }
            }
            if (const char* mode = std::getenv("ZAMIN_EDITOR_TERRAIN"); mode && std::string_view(mode) == "foliage-test") ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::CollapsingHeader("Ekish va relyefga moslash"))
            {
                ImGui::Checkbox("Sculpt bilan avtomatik moslash", &s_AutoReprojectFoliage);
                ImGui::DragFloat("Sirt balandlik ofseti", &s_FoliageOptions.HeightOffset, .01f, -100, 100);
                ImGui::Checkbox("Sirt normaliga moslash", &m_TerrainAlign);
                if (ImGui::Button("Ekilgan obyektlarni sirtga tushirish"))
                {
                    CommitUndo();
                    TerrainReprojectOptions options;
                    options.AlignToNormal = m_TerrainAlign;
                    options.HeightOffset = s_FoliageOptions.HeightOffset;
                    auto result = TerrainFoliage::Reproject(*m_EditorScene, entity, options);
                    if (!result.Error.empty()) m_TerrainMessage = result.Error;
                    else
                    {
                        m_TerrainMessage = std::to_string(result.Changed) + " obyekt yangilandi, " + std::to_string(result.Skipped) + " o'tkazildi.";
                        if (result.Changed)
                        {
                            MarkDirty("Foliage sirtga moslash");
                            CommitUndo();
                        }
                    }
                }
                const auto& rendering = Renderer::GetStats();
                ImGui::Text("Sahna instancing: %u mesh / %u draw", rendering.InstancedMeshCount, rendering.InstancedDrawCount);
                ImGui::TextWrapped("Faqat shu terrain'ga ekilgan ildizlar yangilanadi. X/Z, masshtab va prefab bolalari saqlanadi. Ctrl+Z bilan qaytarish mumkin.");
            }
            if (m_TerrainTool >= 5)
            {
                const char* objects[] = { "Daraxt", "Tosh", "Prefab" };
                ImGui::Combo("Obyekt", &m_TerrainObject, objects, 3);
                if (m_TerrainObject == 2)
                {
                    ImGui::InputText("Prefab yo'li", &m_TerrainPrefab);
                    if (ImGui::Button("Prefab tanlash"))
                    {
                        auto path = FileDialogs::OpenFile("Zamin Prefab (*.zprefab) | *.zprefab", Project::GetAssetDirectory());
                        if (!path.empty()) m_TerrainPrefab = Project::GetAssetRelativePath(path);
                    }
                }
                ImGui::SliderInt("Zichlik", &m_TerrainDensity, 1, 16);
                ImGui::SliderFloat("Oraliq (m)", &m_TerrainSpacing, 0.3f, 15);
                ImGui::SliderFloat("Min masshtab", &m_TerrainScaleMin, 0.1f, 3);
                ImGui::SliderFloat("Max masshtab", &m_TerrainScaleMax, m_TerrainScaleMin, 5);
                ImGui::SliderFloat("Max qiyalik", &m_TerrainMaxSlope, 0, 80);
                ImGui::Checkbox("Sirtga mos burish", &m_TerrainAlign);
                ImGui::TextWrapped("O'chirg'ich faqat shu yerga ekilgan obyektlarga ta'sir qiladi. Har bir bosishda ko'pi bilan 128 ta obyekt.");
            }
            if (ImGui::CollapsingHeader("Relyef generatori"))
            {
                ImGui::InputInt("Seed", &m_TerrainSeed);
                ImGui::SliderFloat("Tepalik (m)", &m_TerrainAmplitude, 0, t.HeightLimit);
                ImGui::SliderFloat("Chastota", &m_TerrainFrequency, 0.005f, 0.2f);
                if (ImGui::Button("Relyef yaratish"))
                {
                    TerrainTools::Generate(t, uint32_t(m_TerrainSeed), m_TerrainAmplitude, m_TerrainFrequency);
                    MarkDirty("Relyef generatsiya");
                }
                if (ImGui::Button("Tekis balandlikka qaytarish"))
                {
                    std::fill(t.Heights.begin(), t.Heights.end(), std::clamp(m_TerrainBase, 0.0f, t.HeightLimit));
                    t.GeometryChanged();
                    MarkDirty("Terrain tekislash");
                }
            }
            if (ImGui::CollapsingHeader("Heightmap import / export"))
            {
                ImGui::TextWrapped("PNG (8/16-bit) yoki kvadrat R16 import. R16 export 16-bit, 0..HeightLimit oralig'ida. Sahna barcha float balandliklarni saqlaydi.");
                if (ImGui::Button("Heightmap import"))
                {
                    auto path = FileDialogs::OpenFile("Heightmap (*.png *.r16) | *.png *.r16", Project::GetAssetDirectory());
                    if (!path.empty() && TerrainTools::ImportHeightmap(t, path, m_TerrainMessage)) MarkDirty("Heightmap import");
                }
                if (ImGui::Button("R16 export"))
                {
                    auto path = FileDialogs::SaveFile("Heightmap (*.r16) | *.r16", Project::GetAssetDirectory() / "terrain.r16");
                    if (!path.empty())
                    {
                        if (std::filesystem::path(path).extension() != ".r16") path += ".r16";
                        if (TerrainTools::ExportHeightmap(t, path, m_TerrainMessage)) m_TerrainMessage = "Heightmap saqlandi.";
                    }
                }
            }
            if (ImGui::CollapsingHeader("Map yordami"))
            {
                if (ImGui::Button("Suv tekisligi qo'shish"))
                {
                    auto e = m_EditorScene->CreateEntity("Map suvi");
                    auto& m = e.AddComponent<MeshRendererComponent>();
                    m.Type = PrimitiveMesh::Plane;
                    m.Mat.Albedo = { 0.08f, 0.3f, 0.45f, 0.6f };
                    m.Mat.BlendMode = MaterialBlendMode::Transparent;
                    m.Mat.Roughness = 0.2f;
                    e.GetTransform().Translation = { t.Size.x * 0.5f, 4, t.Size.y * 0.5f };
                    e.GetTransform().Scale = { t.Size.x, 1, t.Size.y };
                    MarkDirty("Map suvi");
                }
                ImGui::TextWrapped("Ctrl+S: sahnani saqlash. Play: terrain va daraxt collider'lari o'yinda ishlaydi. Katta dunyolar uchun bir nechta terrain bo'lagidan foydalaning.");
            }
        }
        else ImGui::TextWrapped("Terrain yarating yoki sahnadagi Terrain obyektini tanlang.");
        if (!m_TerrainMessage.empty()) ImGui::TextWrapped("%s", m_TerrainMessage.c_str());
        ImGui::EndDisabled();
        ImGui::End();
    }
    void EditorLayer::ApplyTerrainAutomation()
    {
        static bool done = false;
        const char* mode = std::getenv("ZAMIN_EDITOR_TERRAIN");
        const char* packed=std::getenv("ZAMIN_EDITOR_PACKED_FOLIAGE");
        if(packed)mode="packed-test";
        if (done || !mode || !m_EditorScene || m_SceneState != SceneState::Edit) return;
        done = true;
        m_ShowColliders = false;
        if(std::string(mode)=="packed-test")
        {
            auto terrains=m_EditorScene->GetAllEntitiesWith<TerrainComponent>();if(terrains.begin()==terrains.end())throw std::runtime_error("Packed foliage QA needs terrain");
            Entity ground(*terrains.begin(),m_EditorScene.get());auto& terrain=ground.GetComponent<TerrainComponent>();
            auto children=m_EditorScene->GetChildren(ground);for(auto child:children)if(child.HasComponent<TagsComponent>()&&child.GetComponent<TagsComponent>().Has("TerrainPainted"))m_EditorScene->DestroyEntity(child);
            auto forest=m_EditorScene->CreateEntity("Packed Forest");m_EditorScene->SetParent(forest,ground,false);forest.AddComponent<TagsComponent>().Add("TerrainPainted");auto& c=forest.AddComponent<FoliageComponent>();
            c.NearType=c.FarType=PrimitiveMesh::Model;c.NearModel="Models/Foliage/pine-near.obj";c.FarModel="Models/Foliage/pine-far.obj";
            c.Settings.LODDistance=40;c.Settings.CullDistance=120;c.CastShadows=false;
            if(const auto* backend=std::getenv("ZAMIN_FOLIAGE_BACKEND")){if(std::string_view(backend)=="cpu")c.Settings.Backend=FoliageBackend::CPU;else if(std::string_view(backend)=="gpu")c.Settings.Backend=FoliageBackend::GPU;}
            FoliageTools::GenerateGrid(c,uint32_t(std::clamp(std::atoi(packed),1,int(FoliageBatch::MaxInstances))),terrain.Size,42);FoliageTools::ProjectToTerrain(c,terrain);
            m_SceneHierarchyPanel.SetSelectedEntity(forest);m_ShowTerrain=false;Set2DMode(false);m_EditorCamera.Focus({32,10,32},65);m_EditorCamera.SetOrientation(.55f,.7f);
            MarkDirty("Packed foliage demo");CommitUndo();
            if(const auto* output=std::getenv("ZAMIN_EDITOR_TERRAIN_SAVE"))if(!SceneSerializer(m_EditorScene).Serialize(output))throw std::runtime_error("Packed foliage scene save failed");
            ZM_CORE_INFO("Packed foliage demo ready: {} instances, one entity, near/far model LOD",c.Instances.size());return;
        }
        if (std::string(mode) == "foliage-test")
        {
            auto terrains = m_EditorScene->GetAllEntitiesWith<TerrainComponent>();
            if (terrains.begin() == terrains.end()) throw std::runtime_error("Foliage QA needs terrain");
            Entity ground(*terrains.begin(), m_EditorScene.get());
            auto id = ground.GetUUID();
            UUID rootID(0);
            glm::vec3 oldPosition;
            for (auto root : m_EditorScene->GetChildren(ground))
                if (root.HasComponent<TagsComponent>() && root.GetComponent<TagsComponent>().Has("TerrainPainted"))
                {
                    rootID = root.GetUUID();
                    oldPosition = root.GetTransform().Translation;
                    break;
                }
            if (uint64_t(rootID) == 0) throw std::runtime_error("Foliage QA needs painted roots");
            CommitUndo();
            auto& field = ground.GetComponent<TerrainComponent>();
            for (auto& h : field.Heights) h += 2;
            field.GeometryChanged();
            auto result = TerrainFoliage::Reproject(*m_EditorScene, ground);
            if (!result.Error.empty() || !result.Changed) throw std::runtime_error("Foliage reproject failed");
            auto moved = m_EditorScene->GetEntityByUUID(rootID).GetTransform().Translation;
            MarkDirty("Foliage QA");
            CommitUndo();
            Undo();
            if (m_EditorScene->GetEntityByUUID(rootID).GetTransform().Translation != oldPosition) throw std::runtime_error("Foliage undo failed");
            Redo();
            if (m_EditorScene->GetEntityByUUID(rootID).GetTransform().Translation != moved) throw std::runtime_error("Foliage redo failed");
            ground = m_EditorScene->GetEntityByUUID(id);
            m_TerrainID = id;
            m_SceneHierarchyPanel.SetSelectedEntity(ground);
            m_ShowTerrain = true;
            Set2DMode(false);
            m_EditorCamera.Focus({ 32, 10, 32 }, 75);
            m_EditorCamera.SetOrientation(.65f, .7f);
            m_TerrainMessage = std::to_string(result.Changed) + " obyekt sirtga moslandi; Undo/Redo tekshirildi.";
            ZM_CORE_INFO("Foliage QA passed: roots={} reprojected={} undo/redo=OK", result.Visited, result.Changed);
            return;
        }
        if (std::string(mode) == "material-test")
        {
            auto terrains = m_EditorScene->GetAllEntitiesWith<TerrainComponent>();
            if (terrains.begin() == terrains.end()) throw std::runtime_error("Material QA requires a terrain scene");
            Entity e(*terrains.begin(), m_EditorScene.get());
            auto& terrain = e.GetComponent<TerrainComponent>();
            terrain.EnsureSize();
            auto surface = TerrainTools::GetSurface(terrain);
            if (!surface || surface->MissingMaps) throw std::runtime_error("Terrain material source missing");
            for (size_t i = 0; i < 4; ++i)
                if (!surface->Albedo[i] || !surface->Normal[i] || !surface->Mask[i])
                    throw std::runtime_error("Material QA requires four complete PBR layers");
            m_TerrainID = e.GetUUID();
            m_SceneHierarchyPanel.SetSelectedEntity(e);
            m_ShowTerrain = true;
            m_TerrainPainting = false;
            Set2DMode(false);
            auto world = m_EditorScene->GetWorldTransform(e);
            m_EditorCamera.Focus(glm::vec3(world * glm::vec4(terrain.Size.x * .5f, 8, terrain.Size.y * .5f, 1)), glm::length(terrain.Size) * .85f);
            m_EditorCamera.SetOrientation(.65f, .7f);
            ZM_CORE_INFO("Terrain material QA passed: four albedo/normal/ORM layers, control={} bytes uploads={} missing={}", surface->ControlBytes, surface->WeightUploads, surface->MissingMaps);
            return;
        }
        auto entity = CreateTerrainMap();
        if (!entity) return;
        auto& t = entity.GetComponent<TerrainComponent>();
        if (std::string(mode) == "world-test")
        {
            t.Reset(17, { 32, 32 }, 2);
            const auto firstID = entity.GetUUID();
            auto second = m_EditorScene->CreateEntity("Adjacent world tile");
            const auto secondID = second.GetUUID();
            second.GetTransform().Translation = { 32, 0, 0 };
            second.AddComponent<TerrainComponent>().Reset(9, { 32, 32 }, 8);
            const auto originalA = t.Heights;
            const auto originalB = second.GetComponent<TerrainComponent>().Heights;
            MarkDirty("World terrain baseline");
            CommitUndo();
            std::string error;
            if (!TerrainTools::StitchTiles(t, { 0, 0, 0 }, second.GetComponent<TerrainComponent>(), { 32, 0, 0 }, 2, error))
                throw std::runtime_error(error);
            const auto editedA = t.Heights, editedB = second.GetComponent<TerrainComponent>().Heights;
            MarkDirty("World terrain stitch");
            CommitUndo();
            Undo();
            auto& undoneA = m_EditorScene->GetEntityByUUID(firstID).GetComponent<TerrainComponent>();
            auto& undoneB = m_EditorScene->GetEntityByUUID(secondID).GetComponent<TerrainComponent>();
            if (undoneA.Heights != originalA || undoneB.Heights != originalB || undoneA.LockBorderLOD || undoneB.LockBorderLOD)
                throw std::runtime_error("World terrain pair undo failed");
            Redo();
            auto& restoredA = m_EditorScene->GetEntityByUUID(firstID).GetComponent<TerrainComponent>();
            auto& restoredB = m_EditorScene->GetEntityByUUID(secondID).GetComponent<TerrainComponent>();
            if (restoredA.Heights != editedA || restoredB.Heights != editedB || !restoredA.LockBorderLOD || !restoredB.LockBorderLOD)
                throw std::runtime_error("World terrain pair redo failed");
            m_TerrainID = firstID;
            m_SceneHierarchyPanel.SetSelectedEntity(m_EditorScene->GetEntityByUUID(firstID));
            m_EditorCamera.Focus({ 32, 4, 16 }, 65);
            m_EditorCamera.SetOrientation(.65f, .4f);
            m_TerrainPainting = false;
            m_ShowTerrain = true;
            m_TerrainMessage = "World terrain: ikki resolution, stitch va Undo/Redo tekshirildi.";
            ZM_CORE_INFO("World terrain editor QA passed: unequal resolution seam, paired undo/redo, serialized border LOD");
            return;
        }
        if (std::string(mode) == "lod-test")
        {
            t.Reset(257, { 256, 256 }, 8);
            TerrainTools::Generate(t, 42, 30, 0.02f);
            const auto world = m_EditorScene->GetWorldTransform(entity);
            const glm::vec3 farCamera = glm::vec3(world * glm::vec4(128, 4096, 128, 1));
            TerrainTools::GetRenderPatches(t, world, farCamera);
            const auto far = TerrainTools::GetRenderStats(t);
            if (far.Patches != 64 || far.Triangles != 128 || far.FullResolutionTriangles != 131072)
                throw std::runtime_error("Terrain LOD triangle reduction failed");
            TerrainTools::GetRenderPatches(t, world, farCamera);
            if (TerrainTools::GetRenderStats(t).RebuiltPatches != 0)
                throw std::runtime_error("Unchanged terrain rebuilt GPU patches");
            TerrainBrushSettings brush;
            brush.Radius = 2;
            brush.Strength = 10;
            TerrainTools::ApplyBrush(t, { 16, 16 }, brush, 0.1f);
            TerrainTools::GetRenderPatches(t, world, farCamera);
            const auto sculpt = TerrainTools::GetRenderStats(t);
            if (!sculpt.RebuiltPatches || sculpt.RebuiltPatches > 4)
                throw std::runtime_error("Terrain sculpt rebuilt unrelated patches");
            brush.Tool = TerrainBrush::Paint;
            brush.Layer = 2;
            TerrainTools::ApplyBrush(t, { 16, 16 }, brush, 0.1f);
            TerrainTools::GetRenderPatches(t, world, farCamera);
            if (TerrainTools::GetRenderStats(t).RebuiltPatches != 0)
                throw std::runtime_error("Terrain paint rebuilt geometry");
            TerrainTools::GetRenderPatches(t, world, farCamera, true);
            if (TerrainTools::GetRenderStats(t).Triangles != 131072)
                throw std::runtime_error("Terrain orthographic detail was lost");
            auto sun = m_EditorScene->CreateEntity("Sun");
            sun.AddComponent<DirectionalLightComponent>();
            sun.GetTransform().Rotation = { -0.4f, 0, 0.2f };
            auto camera = m_EditorScene->CreateEntity("Main Camera");
            camera.AddComponent<CameraComponent>().Camera.SetPerspective(glm::radians(45.0f), 0.1f, 10000);
            camera.GetTransform().Translation = { 128, 160, 300 };
            camera.GetTransform().Rotation = { -0.7f, 0, 0 };
            m_EditorCamera.Focus({ 128, 15, 128 }, 300);
            m_EditorCamera.SetOrientation(0.65f, 0.7f);
            m_TerrainPainting = false;
            MarkDirty("Terrain LOD sinovi");
            CommitUndo();
            if (const char* output = std::getenv("ZAMIN_EDITOR_TERRAIN_SAVE"))
            {
                std::filesystem::path path(output);
                std::filesystem::create_directories(path.parent_path());
                if (!SceneSerializer(m_EditorScene).Serialize(path)) throw std::runtime_error("LOD map save failed");
            }
            ZM_CORE_INFO("Terrain LOD QA passed: full=131072 far=128 patches=64 sculptRebuilt={} cached=0 paintRebuilt=0 ortho=131072", sculpt.RebuiltPatches);
            return;
        }
        TerrainTools::Generate(t, 42, 14, 0.045f);
        TerrainBrushSettings b;
        b.Tool = TerrainBrush::Flatten;
        b.Radius = 8;
        b.TargetHeight = 5;
        b.Strength = 30;
        for (int i = 0; i < 12; ++i) TerrainTools::ApplyBrush(t, { 32, 32 }, b, 0.1f);
        b.Tool = TerrainBrush::Paint;
        b.Layer = 1;
        b.Radius = 2.5f;
        for (int x = 7; x < 58; ++x) TerrainTools::ApplyBrush(t, { float(x), 32 }, b, 0.1f);
        m_TerrainObject = 0;
        m_TerrainDensity = 8;
        m_TerrainBrush.Radius = 9;
        m_TerrainPlaced = 0;
        for (int x = 10; x < 60; x += 15)
            for (int z = 10; z < 60; z += 15) ScatterTerrainObjects(entity, { float(x), float(z) }, false);
        if (m_EditorScene->GetAllEntitiesWith<CameraComponent>().begin() == m_EditorScene->GetAllEntitiesWith<CameraComponent>().end())
        {
            auto camera = m_EditorScene->CreateEntity("Main Camera");
            camera.AddComponent<CameraComponent>().Camera.SetPerspective(glm::radians(45.0f), 0.1f, 1000);
            camera.GetTransform().Translation = { 32, 45, 75 };
            camera.GetTransform().Rotation = { -0.72f, 0, 0 };
        }
        if (m_EditorScene->GetAllEntitiesWith<DirectionalLightComponent>().begin() == m_EditorScene->GetAllEntitiesWith<DirectionalLightComponent>().end())
        {
            auto sun = m_EditorScene->CreateEntity("Sun");
            sun.AddComponent<DirectionalLightComponent>();
            sun.GetTransform().Rotation = { -0.4f, 0, 0.2f };
        }
        auto spawn = m_EditorScene->CreateEntity("Player Spawn");
        spawn.GetTransform().Translation = { 32, TerrainTools::SampleHeight(t, 32, 32) + 1, 32 };
        spawn.AddComponent<TagsComponent>().Add("Spawn");
        m_TerrainBrush.Radius = 4;
        m_TerrainPainting = false;
        MarkDirty("Map namuna");
        CommitUndo();
        if (std::string(mode) == "test")
        {
            const UUID id = entity.GetUUID();
            const auto original = t.Heights;
            b.Tool = TerrainBrush::Raise;
            b.Radius = 4;
            b.Strength = 20;
            TerrainTools::ApplyBrush(t, { 32, 32 }, b, 0.1f);
            const auto edited = t.Heights;
            MarkDirty("Terrain QA sculpt");
            CommitUndo();
            Undo();
            if (m_EditorScene->GetEntityByUUID(id).GetComponent<TerrainComponent>().Heights != original)
                throw std::runtime_error("Terrain undo failed");
            Redo();
            if (m_EditorScene->GetEntityByUUID(id).GetComponent<TerrainComponent>().Heights != edited)
                throw std::runtime_error("Terrain redo failed");
            auto countTrees = [&]() {
                size_t count = 0;
                for (auto handle : m_EditorScene->GetAllEntitiesWith<TagsComponent, RelationshipComponent>())
                {
                    Entity e(handle, m_EditorScene.get());
                    if (e.GetComponent<RelationshipComponent>().Parent == id && e.GetComponent<TagsComponent>().Has("TerrainPainted")) ++count;
                }
                return count;
            };
            const size_t before = countTrees();
            m_TerrainBrush.Radius = 100;
            ScatterTerrainObjects(m_EditorScene->GetEntityByUUID(id), { 32, 32 }, true);
            CommitUndo();
            if (!before || countTrees() != 0) throw std::runtime_error("Terrain scatter erase failed");
            Undo();
            if (countTrees() != before) throw std::runtime_error("Terrain scatter undo failed");
            Undo(); // restore the original demonstration height data
            m_TerrainBrush.Radius = 4;
            const auto originalPath = m_EditorScenePath;
            const auto blocked = std::filesystem::temp_directory_path() / ("zamin-save-qa-" + std::to_string(uint64_t(UUID()))) / "blocked.zscene";
            std::filesystem::create_directories(blocked);
            m_EditorScenePath = blocked;
            const auto autosave = GetAutosavePath();
            if (autosave.empty() || !FileSystem::WriteTextFileAtomic(autosave, "recovery data"))
                throw std::runtime_error("Cannot prepare save failure QA");
            const auto autosavedBefore = m_AutosavedScene;
            SaveScene();
            if (!m_SceneDirty || m_UndoHistory.IsAtSavedState() || m_AutosavedScene != autosavedBefore || !std::filesystem::exists(autosave))
                throw std::runtime_error("Failed save discarded dirty state or recovery data");
            std::filesystem::remove(autosave);
            std::filesystem::remove_all(blocked.parent_path());
            m_EditorScenePath = originalPath;
            ZM_CORE_INFO("Terrain editor QA passed: sculpt undo/redo, scatter erase/undo, failed save preserves dirty/history/autosave");
        }
        if (const char* output = std::getenv("ZAMIN_EDITOR_TERRAIN_SAVE"))
        {
            std::filesystem::path path(output);
            std::filesystem::create_directories(path.parent_path());
            SceneSerializer(m_EditorScene).Serialize(path);
        }
    }
}
