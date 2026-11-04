#include "EditorLayer.h"
#include "UI.h"

#include <Zamin/Utils/PlatformUtils.h>
#include <Zamin/Utils/FileSystem.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>
#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Zamin {

    namespace fs = std::filesystem;

    static fs::path ConfigDirectory()
    {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        const char* home = std::getenv("HOME");
        fs::path dir = xdg ? fs::path(xdg) / "zamin" : (home ? fs::path(home) / ".config" / "zamin" : fs::temp_directory_path() / "zamin");
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir;
    }

    static std::string Quote(const fs::path& p) { return "\"" + p.string() + "\""; }

    EditorLayer::EditorLayer(fs::path startupProject)
        : Layer("EditorLayer"), m_StartupProject(std::move(startupProject)),
          m_EditorCamera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f)
    {
    }

    EditorLayer::~EditorLayer()
    {
        if (m_BuildThread.joinable()) m_BuildThread.join();
        if (m_ExportThread.joinable()) m_ExportThread.join();
    }

    void EditorLayer::OnAttach()
    {
        FramebufferSpecification fbSpec;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Framebuffer::Create(fbSpec);
        FramebufferSpecification finalSpec;
        finalSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
        finalSpec.Width = fbSpec.Width;
        finalSpec.Height = fbSpec.Height;
        m_FinalFramebuffer = Framebuffer::Create(finalSpec);

        static std::string iniPath = (ConfigDirectory() / "editor-layout.ini").string();
        ImGui::GetIO().IniFilename = iniPath.c_str();
        m_LayoutInitialized = fs::exists(iniPath);

        m_EditorCamera.Focus({ 0.0f, 0.5f, 0.0f }, 14.0f);

        m_SceneHierarchyPanel.OnFocusRequested = [this](Entity e) { FocusEntity(e); };
        m_SceneHierarchyPanel.OnSceneModified = [this]() { MarkDirty(); };
        m_SceneHierarchyPanel.OnSavePrefabRequested = [this](Entity e) { SavePrefab(e); };
        m_SceneHierarchyPanel.OnProjectSettingsModified = [this]() { m_ProjectSettingsPanel.MarkDirty(); };
        m_ProjectSettingsPanel.OnSceneModified = [this]() { MarkDirty(); };
        m_SceneHierarchyPanel.OnPrefabDropped = [this](const fs::path& p, Entity parent) { InstantiatePrefabInEditor(p, parent); };
        m_ContentBrowserPanel.OnOpenScene = [this](const fs::path& p) { OpenScene(p); };
        m_ContentBrowserPanel.OnCreateScript = [this](const std::string& name) { CreateScriptClass(name); };
        m_ContentBrowserPanel.OnNewScene = [this](const fs::path& p) {
            Ref<Scene> scene = CreateRef<Scene>();
            scene->SetName(p.stem().string());
            SerializeScene(scene, p);
        };

        ScriptEngine::SetBeforeUnloadCallback([this]() {
            if (m_SceneState != SceneState::Edit)
                OnSceneStop();
        });

        LoadRecentProjects();
        m_NewProjectLocation = (fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / "ZaminProjects").string();

        NewScene();
        if (!m_StartupProject.empty())
        {
            if (!OpenProject(m_StartupProject))
                m_ShowLauncher = true;
        }
        else
        {
            m_ShowLauncher = true;
        }
        UpdateWindowTitle();
    }

    void EditorLayer::OnDetach()
    {
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        m_ProjectSettingsPanel.SaveIfDirty();
        m_ActiveScene.reset();
        m_EditorScene.reset();
        ScriptEngine::UnloadModule();
    }

    // ================================================================== update
    void EditorLayer::OnUpdate(Timestep ts)
    {
        // Automation hooks (CI / screenshots): ZAMIN_EDITOR_SELECT=<entity name>, ZAMIN_EDITOR_PLAY=1
        static int frame = 0;
        if (++frame == 5)
        {
            if (const char* scene = std::getenv("ZAMIN_EDITOR_SCENE"))
                OpenScene(Project::GetAssetFileSystemPath(scene));
            if (const char* sel = std::getenv("ZAMIN_EDITOR_SELECT"))
                m_SceneHierarchyPanel.SetSelectedEntity(m_ActiveScene->FindEntityByName(sel));
            if (const char* filter = std::getenv("ZAMIN_EDITOR_FILTER")) // e.g. "#Coin"
                m_SceneHierarchyPanel.SetFilter(filter);
            if (std::getenv("ZAMIN_EDITOR_PLAY"))
                OnScenePlay();
            if (std::getenv("ZAMIN_EDITOR_STATS"))
                m_ShowStats = true;
            if (const char* tab = std::getenv("ZAMIN_EDITOR_PROJECT_SETTINGS")) // 1 | SaveData | Tags | General
            {
                m_ShowProjectSettings = true;
                std::string_view t(tab);
                m_ProjectSettingsPanel.Focus(t == "SaveData" ? ProjectSettingsPanel::Tab::SaveData
                                           : t == "Tags"     ? ProjectSettingsPanel::Tab::Tags
                                           : t == "General"  ? ProjectSettingsPanel::Tab::General
                                                             : ProjectSettingsPanel::Tab::Input);
            }
            if (const char* out = std::getenv("ZAMIN_EDITOR_EXPORT"))
                BuildGame(out);
            if (std::getenv("ZAMIN_EDITOR_BUILD"))
                BuildScripts(true);
            if (const char* np = std::getenv("ZAMIN_EDITOR_NEW_PROJECT")) // "<dir>|<name>"
            {
                std::string v = np;
                size_t bar = v.find('|');
                if (bar != std::string::npos)
                    NewProject(v.substr(0, bar), v.substr(bar + 1));
            }
        }

        // Finished background build?
        if (m_BuildFinished.exchange(false))
        {
            if (m_BuildThread.joinable()) m_BuildThread.join();
            if (m_BuildSucceeded)
            {
                ZM_INFO("Skriptlar muvaffaqiyatli kompilyatsiya qilindi ({:.1f} s)", Platform::GetTime() - m_BuildStartTime);
                if (m_ReloadAfterBuild)
                    ReloadScriptModule();
            }
            else
                ZM_ERROR("Skriptlarni kompilyatsiya qilishda xato! Konsolni tekshiring.");
        }
        if (!m_Exporting && m_ExportThread.joinable())
            m_ExportThread.join();

        CheckModuleHotReload(ts);

        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

        if (FramebufferSpecification spec = m_Framebuffer->GetSpecification();
            m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
            (spec.Width != (uint32_t)m_ViewportSize.x || spec.Height != (uint32_t)m_ViewportSize.y))
        {
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_FinalFramebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        }

        Renderer::ResetStats();
        m_Framebuffer->Bind();
        glm::vec4 clear = m_ActiveScene->GetSettings().DrawSky ? glm::vec4(0.1f, 0.1f, 0.12f, 1.0f) : m_ActiveScene->GetClearColor();
        RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), clear.a }); // linear
        RenderCommand::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        {
            ImVec2 mouse = ImGui::GetMousePos();
            m_ActiveScene->SetViewportMouse({ mouse.x - m_ViewportBounds[0].x, mouse.y - m_ViewportBounds[0].y },
                                            m_ViewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        }

        SceneRenderOptions options;
        options.ShowGrid = m_ShowGrid;
        options.GridPlane = m_2DMode ? 1 : 0;

        switch (m_SceneState)
        {
            case SceneState::Edit:
                UpdateTilePainting();
                m_EditorCamera.OnUpdate(ts, m_ViewportHovered);
                m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera, options);
                break;
            case SceneState::Simulate:
                m_EditorCamera.OnUpdate(ts, m_ViewportHovered);
                m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera, options);
                break;
            case SceneState::Play:
                m_ActiveScene->OnUpdateRuntime(ts);
                break;
        }

        if (m_SceneState == SceneState::Play && !m_ActiveScene->GetPendingSceneLoad().empty())
        {
            fs::path next = Project::GetAssetFileSystemPath(m_ActiveScene->GetPendingSceneLoad());
            m_ActiveScene->ClearPendingSceneLoad();
            LoadRuntimeScene(next);
        }

        // Mouse picking
        auto [mx, my] = ImGui::GetMousePos();
        mx -= m_ViewportBounds[0].x;
        my -= m_ViewportBounds[0].y;
        glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
        my = viewportSize.y - my;
        int mouseX = (int)mx, mouseY = (int)my;
        if (mouseX >= 0 && mouseY >= 0 && mouseX < (int)viewportSize.x && mouseY < (int)viewportSize.y)
        {
            int pixel = m_Framebuffer->ReadPixel(1, mouseX, mouseY);
            m_HoveredEntity = pixel == -1 ? Entity() : Entity((entt::entity)pixel, m_ActiveScene.get());
            if (m_HoveredEntity && !m_HoveredEntity.IsValid())
                m_HoveredEntity = {};
        }
        else
            m_HoveredEntity = {};

        // Post-processing into the final target, then editor overlays on top.
        const auto& spec = m_Framebuffer->GetSpecification();
        PostProcessor::Apply(m_Framebuffer->GetColorAttachmentRendererID(0), spec.Width, spec.Height,
                             m_ActiveScene->GetSettings().Post, m_FinalFramebuffer,
                             m_Framebuffer->GetDepthAttachmentRendererID(), m_ActiveScene->GetLastProjection());
        m_FinalFramebuffer->Bind();
        OnOverlayRender();
        m_FinalFramebuffer->Unbind();
    }

    glm::mat4 EditorLayer::OverlayViewProjection()
    {
        if (m_SceneState == SceneState::Play)
        {
            Entity camera = m_ActiveScene->GetPrimaryCameraEntity();
            if (!camera) return glm::mat4(0.0f);
            return camera.GetComponent<CameraComponent>().Camera.GetProjection() * glm::inverse(m_ActiveScene->GetWorldTransform(camera));
        }
        return m_EditorCamera.GetViewProjection();
    }

    bool EditorLayer::ViewportMouseToPlane(float planeZ, glm::vec3& out)
    {
        ImVec2 mouse = ImGui::GetMousePos();
        glm::vec2 size = m_ViewportBounds[1] - m_ViewportBounds[0];
        if (size.x <= 0 || size.y <= 0) return false;
        glm::vec2 ndc = { (mouse.x - m_ViewportBounds[0].x) / size.x * 2.0f - 1.0f, 1.0f - (mouse.y - m_ViewportBounds[0].y) / size.y * 2.0f };
        glm::mat4 inv = glm::inverse(m_EditorCamera.GetViewProjection());
        glm::vec4 n = inv * glm::vec4(ndc, -1.0f, 1.0f), f = inv * glm::vec4(ndc, 1.0f, 1.0f);
        glm::vec3 a = glm::vec3(n) / n.w, b = glm::vec3(f) / f.w, d = b - a;
        if (std::abs(d.z) < 1e-6f) return false;
        float t = (planeZ - a.z) / d.z;
        out = a + d * t;
        return true;
    }

    void EditorLayer::UpdateTilePainting()
    {
        if (!m_SceneHierarchyPanel.IsTilePainting() || !m_ViewportHovered) return;
        if (Input::IsKeyPressed(Key::LeftAlt) || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
        Entity e = m_SceneHierarchyPanel.GetSelectedEntity();
        auto& tm = e.GetComponent<TilemapComponent>();
        glm::mat4 world = m_ActiveScene->GetWorldTransform(e);
        glm::vec3 hit;
        if (!ViewportMouseToPlane(world[3].z, hit)) return;
        glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(hit, 1.0f));
        int x = (int)std::floor(local.x / tm.TileSize), y = (int)std::floor(local.y / tm.TileSize);
        int tile = Input::IsKeyPressed(Key::LeftShift) ? -1 : m_SceneHierarchyPanel.GetSelectedTile();
        if (tm.Get(x, y) != tile && x >= 0 && y >= 0 && x < tm.Width && y < tm.Height)
        {
            tm.Set(x, y, tile);
            MarkDirty();
        }
    }

    static void DrawWireBox(const glm::mat4& transform, const glm::vec4& color)
    {
        glm::vec3 c[8];
        for (int i = 0; i < 8; i++)
            c[i] = glm::vec3(transform * glm::vec4((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f));
        const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
        for (auto& edge : edges)
            Renderer2D::DrawLine(c[edge[0]], c[edge[1]], color);
    }

    static void DrawWireCircle(const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB, float radius, const glm::vec4& color)
    {
        const int segments = 40;
        glm::vec3 prev = center + axisA * radius;
        for (int i = 1; i <= segments; i++)
        {
            float t = glm::two_pi<float>() * i / segments;
            glm::vec3 p = center + (axisA * std::cos(t) + axisB * std::sin(t)) * radius;
            Renderer2D::DrawLine(prev, p, color);
            prev = p;
        }
    }

    void EditorLayer::OnOverlayRender()
    {
        glm::mat4 viewProj = OverlayViewProjection();
        if (viewProj == glm::mat4(0.0f))
            return;

        RenderCommand::SetDepthTest(false);
        Renderer2D::BeginScene(viewProj);
        Scene& scene = *m_ActiveScene;

        if (m_ShowColliders)
        {
            const glm::vec4 green(0.3f, 0.95f, 0.45f, 1.0f), yellow(0.95f, 0.8f, 0.2f, 1.0f);
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, BoxCollider2DComponent>())
            {
                Entity entity{ e, &scene };
                auto& bc = entity.GetComponent<BoxCollider2DComponent>();
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), p)
                    * glm::rotate(glm::mat4(1.0f), r.z, glm::vec3(0, 0, 1))
                    * glm::translate(glm::mat4(1.0f), glm::vec3(bc.Offset, 0.001f))
                    * glm::scale(glm::mat4(1.0f), glm::vec3(glm::vec2(sc) * bc.Size * 2.0f, 1.0f));
                Renderer2D::DrawRect(transform, bc.IsSensor ? yellow : green);
            }
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, CircleCollider2DComponent>())
            {
                Entity entity{ e, &scene };
                auto& cc = entity.GetComponent<CircleCollider2DComponent>();
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                float d = std::abs(sc.x) * cc.Radius * 2.0f;
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), p + glm::vec3(cc.Offset, 0.001f)) * glm::scale(glm::mat4(1.0f), glm::vec3(d, d, 1.0f));
                Renderer2D::DrawCircle(transform, cc.IsSensor ? yellow : green, 0.04f);
            }

            const glm::vec4 cyan(0.3f, 0.85f, 1.0f, 1.0f);
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, BoxCollider3DComponent>())
            {
                Entity entity{ e, &scene };
                auto& c = entity.GetComponent<BoxCollider3DComponent>();
                glm::mat4 t = scene.GetWorldTransform(entity) * glm::translate(glm::mat4(1.0f), c.Offset) * glm::scale(glm::mat4(1.0f), c.HalfExtents * 2.0f);
                DrawWireBox(t, c.IsSensor ? yellow : cyan);
            }
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, SphereCollider3DComponent>())
            {
                Entity entity{ e, &scene };
                auto& c = entity.GetComponent<SphereCollider3DComponent>();
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                float radius = c.Radius * std::max({ std::abs(sc.x), std::abs(sc.y), std::abs(sc.z) });
                glm::vec3 center = p + glm::quat(r) * (c.Offset * sc);
                DrawWireCircle(center, { 1, 0, 0 }, { 0, 1, 0 }, radius, c.IsSensor ? yellow : cyan);
                DrawWireCircle(center, { 1, 0, 0 }, { 0, 0, 1 }, radius, c.IsSensor ? yellow : cyan);
                DrawWireCircle(center, { 0, 1, 0 }, { 0, 0, 1 }, radius, c.IsSensor ? yellow : cyan);
            }
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, CharacterController3DComponent>())
            {
                Entity entity{ e, &scene };
                auto& c = entity.GetComponent<CharacterController3DComponent>();
                glm::vec3 feet = scene.GetWorldPosition(entity);
                float r = c.Radius, half = std::max(0.0f, c.Height * 0.5f - r);
                glm::vec3 center = feet + glm::vec3(0, c.Height * 0.5f, 0);
                glm::vec4 col(0.6f, 0.4f, 1.0f, 1.0f);
                DrawWireCircle(center + glm::vec3(0, half, 0), { 1, 0, 0 }, { 0, 0, 1 }, r, col);
                DrawWireCircle(center - glm::vec3(0, half, 0), { 1, 0, 0 }, { 0, 0, 1 }, r, col);
                for (glm::vec3 side : { glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1) })
                    Renderer2D::DrawLine(center + glm::vec3(0, half, 0) + side * r, center - glm::vec3(0, half, 0) + side * r, col);
                DrawWireCircle(center + glm::vec3(0, half, 0), { 1, 0, 0 }, { 0, 1, 0 }, r, col);
                DrawWireCircle(center - glm::vec3(0, half, 0), { 1, 0, 0 }, { 0, 1, 0 }, r, col);
            }
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, CapsuleCollider3DComponent>())
            {
                Entity entity{ e, &scene };
                auto& c = entity.GetComponent<CapsuleCollider3DComponent>();
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                glm::quat q(r);
                float radius = c.Radius * std::max(std::abs(sc.x), std::abs(sc.z));
                float half = c.HalfHeight * std::abs(sc.y);
                glm::vec3 center = p + q * (c.Offset * sc);
                glm::vec3 up = q * glm::vec3(0, 1, 0), right = q * glm::vec3(1, 0, 0), fwd = q * glm::vec3(0, 0, 1);
                glm::vec4 col = c.IsSensor ? yellow : cyan;
                DrawWireCircle(center + up * half, right, fwd, radius, col);
                DrawWireCircle(center - up * half, right, fwd, radius, col);
                for (glm::vec3 side : { right, -right, fwd, -fwd })
                    Renderer2D::DrawLine(center + up * half + side * radius, center - up * half + side * radius, col);
                DrawWireCircle(center + up * half, right, up, radius, col);
                DrawWireCircle(center - up * half, right, up, radius, col);
            }
        }

        // Tilemap bounds + hovered cell
        if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity(); selected && selected.IsValid() && selected.HasComponent<TilemapComponent>())
        {
            auto& tm = selected.GetComponent<TilemapComponent>();
            glm::mat4 world = scene.GetWorldTransform(selected);
            glm::vec2 size = { tm.Width * tm.TileSize, tm.Height * tm.TileSize };
            Renderer2D::DrawRect(world * glm::translate(glm::mat4(1.0f), glm::vec3(size * 0.5f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f)),
                                 glm::vec4(0.3f, 0.85f, 1.0f, 0.8f));
            glm::vec3 hit;
            if (m_SceneHierarchyPanel.IsTilePainting() && m_ViewportHovered && ViewportMouseToPlane(world[3].z, hit))
            {
                glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(hit, 1.0f));
                int x = (int)std::floor(local.x / tm.TileSize), y = (int)std::floor(local.y / tm.TileSize);
                if (x >= 0 && y >= 0 && x < tm.Width && y < tm.Height)
                {
                    glm::mat4 cell = world * glm::translate(glm::mat4(1.0f), { (x + 0.5f) * tm.TileSize, (y + 0.5f) * tm.TileSize, 0.0f })
                                           * glm::scale(glm::mat4(1.0f), { tm.TileSize, tm.TileSize, 1.0f });
                    Renderer2D::DrawRect(cell, Input::IsKeyPressed(Key::LeftShift) ? glm::vec4(1, 0.3f, 0.3f, 1) : glm::vec4(1, 1, 1, 1));
                }
            }
        }

        // Selection outline
        if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity(); selected && selected.IsValid())
        {
            const glm::vec4 orange(1.0f, 0.6f, 0.1f, 1.0f);
            glm::mat4 transform = scene.GetWorldTransform(selected);
            if (auto* mr = selected.TryGetComponent<MeshRendererComponent>(); mr && (mr->MeshAsset || mr->ModelAsset))
            {
                const AABB& b = mr->ModelAsset ? mr->ModelAsset->GetBounds() : mr->MeshAsset->GetBounds();
                glm::vec3 size = glm::max(b.Max - b.Min, glm::vec3(0.001f));
                DrawWireBox(transform * glm::translate(glm::mat4(1.0f), (b.Min + b.Max) * 0.5f) * glm::scale(glm::mat4(1.0f), size), orange);
            }
            else if (selected.HasComponent<SpriteRendererComponent>() || selected.HasComponent<CircleRendererComponent>())
            {
                Renderer2D::DrawRect(transform, orange);
            }
            else if (auto* text = selected.TryGetComponent<TextComponent>())
            {
                if (text->FontAsset)
                {
                    glm::vec2 size = Renderer2D::MeasureString(text->Text, text->FontAsset, { text->Color, text->Kerning, text->LineSpacing });
                    float asc = text->FontAsset->GetAscent();
                    glm::vec2 center = text->Centered ? glm::vec2(0.0f) : glm::vec2(size.x * 0.5f, asc - size.y * 0.5f);
                    glm::mat4 box = transform * glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
                    Renderer2D::DrawRect(box, orange);
                }
            }
            // children get a subtle outline too
            for (UUID child : selected.GetComponent<RelationshipComponent>().Children)
                if (Entity c = scene.GetEntityByUUID(child))
                    Renderer2D::DrawLine(glm::vec3(transform[3]), scene.GetWorldPosition(c), glm::vec4(1.0f, 0.6f, 0.1f, 0.5f));
        }

        Renderer2D::EndScene();
        RenderCommand::SetDepthTest(true);
    }

    // ================================================================== events
    void EditorLayer::OnEvent(Event& e)
    {
        if (m_SceneState != SceneState::Play && m_ViewportHovered)
            m_EditorCamera.OnEvent(e);

        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<KeyPressedEvent>(ZM_BIND_EVENT_FN(OnKeyPressed));
        dispatcher.Dispatch<MouseButtonPressedEvent>(ZM_BIND_EVENT_FN(OnMouseButtonPressed));
        dispatcher.Dispatch<WindowDropEvent>([this](WindowDropEvent& ev) {
            for (const auto& p : ev.GetPaths())
            {
                fs::path path(p);
                if (path.extension() == ".zproject") { OpenProject(path); break; }
                if (path.extension() == ".zscene") { OpenScene(path); break; }
            }
            return true;
        });
    }

    bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        // Global shortcuts are handled in OnImGuiRender (so they respect text input).
        return false;
    }

    bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetMouseButton() == MouseButton::Left && m_ViewportHovered && m_SceneState != SceneState::Play)
        {
            bool alt = Input::IsKeyPressed(Key::LeftAlt) || Input::IsKeyPressed(Key::RightAlt);
            bool overGizmo = m_SceneHierarchyPanel.GetSelectedEntity() && ImGuizmo::IsOver() && m_GizmoMode != GizmoMode::None;
            if (!alt && !overGizmo && !m_SceneHierarchyPanel.IsTilePainting())
                m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
        }
        return false;
    }

    // ================================================================== projects
    void EditorLayer::LoadRecentProjects()
    {
        m_RecentProjects.clear();
        if (!fs::exists(ConfigDirectory() / "recent-projects.json")) return;
        std::string text = FileSystem::ReadTextFile(ConfigDirectory() / "recent-projects.json");
        if (text.empty()) return;
        try
        {
            auto j = nlohmann::json::parse(text);
            for (auto& p : j)
                if (fs::exists(p.get<std::string>()))
                    m_RecentProjects.emplace_back(p.get<std::string>());
        }
        catch (...) {}
    }

    void EditorLayer::AddRecentProject(const fs::path& path)
    {
        fs::path abs = fs::absolute(path);
        std::erase(m_RecentProjects, abs);
        m_RecentProjects.insert(m_RecentProjects.begin(), abs);
        if (m_RecentProjects.size() > 10) m_RecentProjects.resize(10);
        nlohmann::json j = nlohmann::json::array();
        for (auto& p : m_RecentProjects) j.push_back(p.string());
        FileSystem::WriteTextFile(ConfigDirectory() / "recent-projects.json", j.dump(2));
    }

    void EditorLayer::NewProject(const fs::path& directory, const std::string& name)
    {
        fs::path dir = directory / name;
        if (fs::exists(dir / (name + ".zproject")))
        {
            ZM_WARN("Bu joyda loyiha allaqachon bor - ochilmoqda");
            OpenProject(dir / (name + ".zproject"));
            return;
        }
        if (!Project::CreateFromTemplate(dir, name))
        {
            ZM_ERROR("Loyiha yaratilmadi: {}", dir.string());
            return;
        }
        OpenProject(dir / (name + ".zproject")); // also compiles the template scripts (module missing)
    }

    bool EditorLayer::OpenProject(const fs::path& path)
    {
        fs::path projectFile = path;
        if (fs::is_directory(path))
        {
            for (auto& entry : fs::directory_iterator(path))
                if (entry.path().extension() == ".zproject") { projectFile = entry.path(); break; }
        }
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();

        m_ProjectSettingsPanel.SaveIfDirty();
        SaveData::Save(); // the previous project's game data
        if (!Project::Load(projectFile))
        {
            ZM_ERROR("Loyiha ochilmadi: {}", path.string());
            return false;
        }
        m_ProjectSettingsPanel.OnProjectChanged();
        AddRecentProject(projectFile);
        m_ShowLauncher = false;

        ScriptEngine::UnloadModule();
        fs::path module = Project::GetScriptModulePath();
        if (!module.empty())
        {
            if (fs::exists(module))
                ScriptEngine::LoadModule(module);
            else if (fs::exists(Project::GetProjectDirectory() / "CMakeLists.txt"))
                BuildScripts(true);
        }
        m_SceneHierarchyPanel.OnScriptsReloaded();
        m_ContentBrowserPanel.SetRoot(Project::GetProjectDirectory());

        auto& config = Project::GetActive()->GetConfig();
        fs::path startScene = Project::GetAssetFileSystemPath(config.StartScene);
        if (config.StartScene.empty() || !OpenScene(startScene))
            NewScene();
        UpdateWindowTitle();
        return true;
    }

    void EditorLayer::OpenProjectDialog()
    {
        std::string path = FileDialogs::OpenFile("Zamin Project (*.zproject) | *.zproject");
        if (!path.empty())
            OpenProject(path);
    }

    void EditorLayer::CloseProject()
    {
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        ScriptEngine::UnloadModule();
        m_ProjectSettingsPanel.SaveIfDirty();
        m_ProjectSettingsPanel.OnProjectChanged();
        SaveData::Save();
        Project::SetActive(nullptr);
        m_ContentBrowserPanel.SetRoot({});
        NewScene();
        m_ShowLauncher = true;
        UpdateWindowTitle();
    }

    // ================================================================== scenes
    void EditorLayer::SetActiveScene(const Ref<Scene>& scene)
    {
        m_ActiveScene = scene;
        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
        UUID selectedID = selected && selected.IsValid() ? selected.GetUUID() : UUID(0);
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
        if ((uint64_t)selectedID)
            m_SceneHierarchyPanel.SetSelectedEntity(m_ActiveScene->GetEntityByUUID(selectedID));
    }

    void EditorLayer::NewScene()
    {
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        m_EditorScene = CreateRef<Scene>();
        m_EditorScene->SetName("Yangi sahna");
        SetActiveScene(m_EditorScene);
        m_EditorScenePath.clear();
        m_SceneDirty = false;
        ResetUndoHistory();
        UpdateWindowTitle();
    }

    void EditorLayer::OpenScene()
    {
        std::string path = FileDialogs::OpenFile("Zamin Scene (*.zscene) | *.zscene", Project::GetAssetDirectory());
        if (!path.empty())
            OpenScene(path);
    }

    bool EditorLayer::OpenScene(const fs::path& path)
    {
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        if (path.extension() != ".zscene")
        {
            ZM_WARN("Sahna fayli emas: {}", path.string());
            return false;
        }

        Ref<Scene> newScene = CreateRef<Scene>();
        SceneSerializer serializer(newScene);
        if (!serializer.Deserialize(path))
            return false;
        newScene->ResolveAssets();

        m_EditorScene = newScene;
        m_SceneHierarchyPanel.SetContext({});
        SetActiveScene(m_EditorScene);
        m_EditorScenePath = path;
        m_SceneDirty = false;
        ResetUndoHistory();
        // Auto-detect 2D scenes (orthographic primary camera).
        if (Entity cam = m_EditorScene->GetPrimaryCameraEntity())
            Set2DMode(cam.GetComponent<CameraComponent>().Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic);
        ZM_INFO("Sahna ochildi: {}", path.filename().string());
        UpdateWindowTitle();
        return true;
    }

    void EditorLayer::SerializeScene(const Ref<Scene>& scene, const fs::path& path)
    {
        SceneSerializer serializer(scene);
        if (serializer.Serialize(path))
            ZM_INFO("Sahna saqlandi: {}", path.filename().string());
    }

    void EditorLayer::SaveScene()
    {
        if (m_SceneState != SceneState::Edit)
        {
            ZM_WARN("O'yin rejimida saqlab bo'lmaydi - avval to'xtating");
            return;
        }
        if (m_EditorScenePath.empty())
        {
            SaveSceneAs();
            return;
        }
        SerializeScene(m_EditorScene, m_EditorScenePath);
        m_SceneDirty = false;
        if (auto project = Project::GetActive(); project && project->GetConfig().StartScene.empty())
        {
            project->GetConfig().StartScene = Project::GetAssetRelativePath(m_EditorScenePath);
            Project::SaveActive(Project::GetProjectFilePath());
        }
        UpdateWindowTitle();
    }

    void EditorLayer::SaveSceneAs()
    {
        if (m_SceneState != SceneState::Edit) return;
        fs::path start = Project::GetActive() ? Project::GetAssetDirectory() / "Scenes" : fs::current_path();
        std::string path = FileDialogs::SaveFile("Zamin Scene (*.zscene) | *.zscene", start);
        if (path.empty())
        {
            if (!FileDialogs::IsAvailable() && Project::GetActive())
            {
                // No dialog program: save next to the project assets with the scene name.
                path = (start / (m_EditorScene->GetName() + ".zscene")).string();
                ZM_WARN("Fayl dialogi yo'q (zenity o'rnating). Saqlanmoqda: {}", path);
            }
            else return;
        }
        fs::path p(path);
        if (p.extension() != ".zscene") p += ".zscene";
        m_EditorScene->SetName(p.stem().string());
        m_EditorScenePath = p;
        SaveScene();
    }

    void EditorLayer::OnScenePlay()
    {
        if (m_SceneState == SceneState::Simulate)
            OnSceneStop();
        m_SceneState = SceneState::Play;
        Ref<Scene> runtime = Scene::Copy(m_EditorScene);
        runtime->ResolveAssets();
        SetActiveScene(runtime);
        m_ActiveScene->OnRuntimeStart();
        ImGui::SetWindowFocus("###Viewport");
        UpdateWindowTitle();
    }

    void EditorLayer::OnSceneSimulate()
    {
        if (m_SceneState == SceneState::Play)
            OnSceneStop();
        m_SceneState = SceneState::Simulate;
        Ref<Scene> sim = Scene::Copy(m_EditorScene);
        sim->ResolveAssets();
        SetActiveScene(sim);
        m_ActiveScene->OnSimulationStart();
        UpdateWindowTitle();
    }

    void EditorLayer::OnSceneStop()
    {
        if (m_SceneState == SceneState::Play)
            m_ActiveScene->OnRuntimeStop();
        else if (m_SceneState == SceneState::Simulate)
            m_ActiveScene->OnSimulationStop();
        m_SceneState = SceneState::Edit;
        SetActiveScene(m_EditorScene);
        UpdateWindowTitle();
    }

    void EditorLayer::OnScenePause()
    {
        if (m_SceneState == SceneState::Edit) return;
        m_ActiveScene->SetPaused(!m_ActiveScene->IsPaused());
    }

    void EditorLayer::DuplicateSelectedEntity()
    {
        if (m_SceneState != SceneState::Edit) return;
        if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity())
        {
            Entity copy = m_EditorScene->DuplicateEntity(selected);
            m_SceneHierarchyPanel.SetSelectedEntity(copy);
            MarkDirty();
        }
    }

    void EditorLayer::DeleteSelectedEntity()
    {
        if (m_SceneState != SceneState::Edit) return;
        if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity())
        {
            m_SceneHierarchyPanel.SetSelectedEntity({});
            m_EditorScene->DestroyEntity(selected);
            MarkDirty();
        }
    }

    void EditorLayer::FocusEntity(Entity entity)
    {
        if (!entity) return;
        glm::vec3 p, r, sc;
        Math::DecomposeTransform(m_ActiveScene->GetWorldTransform(entity), p, r, sc);
        float size = std::max({ std::abs(sc.x), std::abs(sc.y), std::abs(sc.z), 1.0f });
        m_EditorCamera.Focus(p, size * 3.5f + 2.0f);
    }

    void EditorLayer::Set2DMode(bool enabled)
    {
        m_2DMode = enabled;
        m_EditorCamera.SetOrthographic(enabled);
        if (enabled)
        {
            glm::vec3 focus = m_EditorCamera.GetFocalPoint();
            if (Entity cam = m_ActiveScene->GetPrimaryCameraEntity())
                focus = m_ActiveScene->GetWorldPosition(cam);
            m_EditorCamera.SetOrientation(0.0f, 0.0f);
            m_EditorCamera.Focus({ focus.x, focus.y, 0.0f }, 18.0f);
        }
        else
        {
            m_EditorCamera.SetOrientation(0.35f, -0.6f);
            m_EditorCamera.Focus({ 0.0f, 0.5f, 0.0f }, 14.0f);
        }
    }

    void EditorLayer::UpdateWindowTitle()
    {
        std::string title = "Zamin Editor";
        if (auto project = Project::GetActive())
            title += " - " + project->GetConfig().Name;
        if (m_EditorScene)
            title += " - " + m_EditorScene->GetName() + (m_SceneDirty ? " *" : "");
        if (m_SceneState == SceneState::Play) title += "  [O'YIN]";
        Application::Get().GetWindow().SetTitle(title);
    }

    // ================================================================== undo / redo
    std::string EditorLayer::TakeSnapshot() { return SceneSerializer(m_EditorScene).SerializeToString(); }

    void EditorLayer::ResetUndoHistory()
    {
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_PendingUndo = false;
        m_LastSnapshot = m_EditorScene ? TakeSnapshot() : std::string();
    }

    void EditorLayer::CommitUndo()
    {
        if (!m_PendingUndo || m_SceneState != SceneState::Edit)
            return;
        m_PendingUndo = false;
        std::string current = TakeSnapshot();
        if (current == m_LastSnapshot)
            return;
        m_UndoStack.push_back(std::move(m_LastSnapshot));
        if (m_UndoStack.size() > 100)
            m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
        m_LastSnapshot = std::move(current);
    }

    void EditorLayer::RestoreSnapshot(const std::string& snapshot)
    {
        Ref<Scene> scene = CreateRef<Scene>();
        if (!SceneSerializer(scene).DeserializeFromString(snapshot))
            return;
        scene->ResolveAssets();
        m_EditorScene = scene;
        SetActiveScene(m_EditorScene);
        m_SceneDirty = true;
        UpdateWindowTitle();
    }

    void EditorLayer::Undo()
    {
        if (m_SceneState != SceneState::Edit) return;
        CommitUndo();
        if (m_UndoStack.empty()) return;
        m_RedoStack.push_back(std::move(m_LastSnapshot));
        m_LastSnapshot = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();
        RestoreSnapshot(m_LastSnapshot);
    }

    void EditorLayer::Redo()
    {
        if (m_SceneState != SceneState::Edit || m_RedoStack.empty()) return;
        m_UndoStack.push_back(std::move(m_LastSnapshot));
        m_LastSnapshot = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();
        RestoreSnapshot(m_LastSnapshot);
    }

    // ================================================================== clipboard / prefabs
    static void CollectSubtree(Scene& scene, Entity root, std::vector<Entity>& out)
    {
        out.push_back(root);
        for (UUID child : root.GetComponent<RelationshipComponent>().Children)
            if (Entity c = scene.GetEntityByUUID(child))
                CollectSubtree(scene, c, out);
    }

    void EditorLayer::CopySelectedEntity()
    {
        Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
        if (!selected) return;
        std::vector<Entity> entities;
        CollectSubtree(*m_ActiveScene, selected, entities);
        m_Clipboard = SceneSerializer::SerializeEntities(*m_ActiveScene, entities);
        ImGui::SetClipboardText(m_Clipboard.c_str());
        ZM_INFO("Nusxalandi: {} ({} obyekt)", selected.GetName(), entities.size());
    }

    void EditorLayer::PasteEntity()
    {
        if (m_SceneState != SceneState::Edit) return;
        const char* text = ImGui::GetClipboardText();
        std::string data = (text && std::string_view(text).find("\"ZaminEntities\"") != std::string_view::npos) ? text : m_Clipboard;
        if (data.empty()) return;
        auto entities = SceneSerializer::DeserializeEntities(*m_EditorScene, data, true);
        if (entities.empty()) return;
        m_EditorScene->ResolveAssets();
        m_SceneHierarchyPanel.SetSelectedEntity(entities.front());
        MarkDirty();
    }

    void EditorLayer::SavePrefab(Entity entity)
    {
        if (!entity || !Project::GetActive()) return;
        fs::path dir = Project::GetAssetDirectory() / "Prefabs";
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::string name;
        for (char ch : entity.GetName())
            name += (std::isalnum((unsigned char)ch) || ch == '_' || ch == '-') ? ch : '_';
        if (name.empty()) name = "Prefab";
        fs::path path = dir / (name + ".zprefab");
        if (m_ActiveScene->SavePrefab(entity, path))
            ZM_INFO("Prefab saqlandi: {}", Project::GetAssetRelativePath(path));
    }

    Entity EditorLayer::InstantiatePrefabInEditor(const fs::path& assetPath, Entity parent)
    {
        if (m_SceneState != SceneState::Edit) return {};
        Entity root = m_EditorScene->InstantiatePrefab(Project::GetAssetFileSystemPath(assetPath));
        if (!root)
        {
            ZM_ERROR("Prefab yuklanmadi: {}", assetPath.string());
            return {};
        }
        if (parent)
            m_EditorScene->SetParent(root, parent, false);
        m_SceneHierarchyPanel.SetSelectedEntity(root);
        MarkDirty();
        return root;
    }

    void EditorLayer::LoadRuntimeScene(const fs::path& path)
    {
        Ref<Scene> next = CreateRef<Scene>();
        if (!SceneSerializer(next).Deserialize(path))
        {
            ZM_ERROR("Sahna topilmadi: {}", path.string());
            return;
        }
        next->ResolveAssets();
        m_ActiveScene->OnRuntimeStop();
        SetActiveScene(next);
        m_ActiveScene->OnRuntimeStart();
        ZM_INFO("Sahna almashtirildi: {}", path.filename().string());
    }

    // ================================================================== scripts
    void EditorLayer::BuildScripts(bool reloadAfter)
    {
        if (m_Building)
        {
            ZM_WARN("Kompilyatsiya allaqachon ketmoqda...");
            return;
        }
        auto project = Project::GetActive();
        if (!project)
        {
            ZM_WARN("Avval loyiha oching");
            return;
        }
        fs::path projectDir = Project::GetProjectDirectory();
        if (!fs::exists(projectDir / "CMakeLists.txt"))
            Project::WriteBuildFiles(projectDir, project->GetConfig().Name);

        if (m_BuildThread.joinable()) m_BuildThread.join();

        fs::path buildDir = projectDir / "Build";
        fs::path engineCMakeDir = FileSystem::GetExecutableDirectory().parent_path();
        bool ninja = std::system("command -v ninja >/dev/null 2>&1") == 0;
        bool needsConfigure = !fs::exists(buildDir / "CMakeCache.txt");

        std::string configure = "cmake -S " + Quote(projectDir) + " -B " + Quote(buildDir) + (ninja ? " -G Ninja" : "") +
                                " -DCMAKE_BUILD_TYPE=Release -DZamin_DIR=" + Quote(engineCMakeDir);
        std::string build = "cmake --build " + Quote(buildDir) + " --parallel";
        std::string command = (needsConfigure ? configure + " 2>&1 && " : "") + build + " 2>&1";

        m_Building = true;
        m_ReloadAfterBuild = reloadAfter;
        m_BuildStartTime = Platform::GetTime();
        ZM_INFO("C++ skriptlar kompilyatsiya qilinmoqda: {}", project->GetConfig().Name);

        m_BuildThread = std::thread([this, command]() {
            FILE* pipe = popen(command.c_str(), "r");
            bool ok = false;
            if (pipe)
            {
                char line[2048];
                while (fgets(line, sizeof(line), pipe))
                {
                    std::string s(line);
                    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
                    if (s.empty()) continue;
                    if (s.find("error") != std::string::npos || s.find("Error") != std::string::npos) ZM_ERROR("{}", s);
                    else if (s.find("warning") != std::string::npos) ZM_WARN("{}", s);
                    else ZM_TRACE("{}", s);
                }
                int status = pclose(pipe);
                ok = status == 0;
            }
            m_BuildSucceeded = ok;
            m_Building = false;
            m_BuildFinished = true;
        });
    }

    void EditorLayer::ReloadScriptModule()
    {
        fs::path module = Project::GetScriptModulePath();
        if (module.empty())
            return;
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        if (ScriptEngine::LoadModule(module))
            ZM_INFO("Skriptlar qayta yuklandi (hot reload) " ICON_FA_CHECK);
        m_SceneHierarchyPanel.OnScriptsReloaded();
    }

    void EditorLayer::CheckModuleHotReload(Timestep ts)
    {
        m_HotReloadTimer += ts;
        if (m_HotReloadTimer < 1.0f || !m_AutoReload || m_Building)
            return;
        m_HotReloadTimer = 0.0f;
        fs::path module = Project::GetScriptModulePath();
        std::error_code ec;
        if (module.empty() || !fs::exists(module, ec))
            return;
        auto time = fs::last_write_time(module, ec);
        if (!ec && (!ScriptEngine::IsModuleLoaded() || time != ScriptEngine::GetLoadedModuleWriteTime()))
        {
            ZM_INFO("Skript moduli o'zgardi - qayta yuklanmoqda");
            ReloadScriptModule();
        }
    }

    void EditorLayer::CreateScriptClass(const std::string& className)
    {
        if (!Project::GetActive()) return;
        std::string name;
        for (char ch : className)
            if (std::isalnum((unsigned char)ch) || ch == '_') name += ch;
        if (name.empty() || std::isdigit((unsigned char)name[0]))
        {
            ZM_ERROR("Klass nomi noto'g'ri: '{}'", className);
            return;
        }
        fs::path file = Project::GetSourceDirectory() / (name + ".cpp");
        if (fs::exists(file))
        {
            ZM_WARN("Fayl allaqachon mavjud: {}", file.string());
            return;
        }
        std::string code = std::format(R"(#include <Zamin/Zamin.h>

using namespace Zamin;

class {0} : public ScriptableEntity
{{
public:
    // Inspector'da ko'rinadigan maydonlar (ZM_FIELD bilan ro'yxatdan o'tkazing)
    float Speed = 3.0f;

    void OnCreate() override
    {{
        ZM_INFO("{0} yaratildi: {{}}", GetName());
    }}

    void OnUpdate(Timestep ts) override
    {{
        // Har kadrda chaqiriladi. Masalan:
        // Transform().Translation.x += Speed * ts;
    }}

    void OnCollisionBegin(Entity other) override
    {{
        // 2D fizik to'qnashuv boshlandi
    }}
}};

ZM_REGISTER_SCRIPT({0}, ZM_FIELD({0}, Speed))
)", name);
        if (FileSystem::WriteTextFile(file, code))
        {
            ZM_INFO("Yangi skript yaratildi: {}", file.string());
            BuildScripts(true);
        }
    }

    // ================================================================== export
    void EditorLayer::BuildGame(const fs::path& outputDirectory)
    {
        auto project = Project::GetActive();
        if (!project || m_Exporting) return;
        if (m_SceneState != SceneState::Edit) OnSceneStop();
        if (m_SceneDirty) SaveScene();
        m_ProjectSettingsPanel.SaveIfDirty(); // the exported game reads the .zproject (input map)

        std::string name = project->GetConfig().Name;
        fs::path projectDir = Project::GetProjectDirectory();
        fs::path projectFile = Project::GetProjectFilePath();
        fs::path modulePath = Project::GetScriptModulePath();
        fs::path moduleRel = project->GetConfig().ScriptModulePath;
        fs::path assetRel = project->GetConfig().AssetDirectory;
        fs::path exeDir = FileSystem::GetExecutableDirectory();

        if (m_ExportThread.joinable()) m_ExportThread.join();
        m_Exporting = true;
        m_ExportThread = std::thread([=, this]() {
            std::error_code ec;
            auto copyOpts = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
            fs::create_directories(outputDirectory / "Game", ec);

            bool ok = true;
            auto copy = [&](const fs::path& from, const fs::path& to) {
                std::error_code e;
                if (!fs::exists(from)) { ZM_ERROR("Topilmadi: {}", from.string()); ok = false; return; }
                fs::create_directories(to.parent_path(), e);
                fs::copy(from, to, copyOpts, e);
                if (e) { ZM_ERROR("Nusxalashda xato {}: {}", from.string(), e.message()); ok = false; }
            };

            copy(exeDir / "ZaminPlayer", outputDirectory / "ZaminPlayer");
            for (auto& entry : fs::directory_iterator(exeDir, ec))
                if (entry.path().filename().string().starts_with("libZamin"))
                    copy(entry.path(), outputDirectory / entry.path().filename());
            copy(exeDir / "Resources", outputDirectory / "Resources");
            copy(projectFile, outputDirectory / "Game" / projectFile.filename());
            copy(projectDir / assetRel, outputDirectory / "Game" / assetRel);
            if (!modulePath.empty())
            {
                if (fs::exists(modulePath)) copy(modulePath, outputDirectory / "Game" / moduleRel);
                else ZM_WARN("Skript moduli topilmadi - o'yin skriptlarsiz eksport qilinadi");
            }

            fs::path launcher = outputDirectory / (name + ".sh");
            FileSystem::WriteTextFile(launcher, "#!/bin/sh\ncd \"$(dirname \"$0\")\"\nexec ./ZaminPlayer \"Game/" +
                                                    projectFile.filename().string() + "\" \"$@\"\n");
            fs::permissions(launcher, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                          fs::perms::others_read | fs::perms::others_exec, ec);

            if (ok) ZM_INFO("O'yin eksport qilindi " ICON_FA_ROCKET " {}  (ishga tushirish: {})", outputDirectory.string(), launcher.string());
            else ZM_ERROR("O'yinni eksport qilishda xatolar bo'ldi");
            m_Exporting = false;
        });
    }

    // ================================================================== ImGui
    void EditorLayer::OnImGuiRender()
    {
        ImGuiIO& io = ImGui::GetIO();

        // ---- keyboard shortcuts (ignored while typing)
        if (!io.WantTextInput)
        {
            bool ctrl = io.KeyCtrl, shift = io.KeyShift;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) { shift ? SaveSceneAs() : SaveScene(); m_ProjectSettingsPanel.SaveIfDirty(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) { shift ? OpenProjectDialog() : OpenScene(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) NewScene();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelectedEntity();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { shift ? Redo() : Undo(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelectedEntity();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteEntity();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) BuildScripts(true);
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) { m_SceneState == SceneState::Edit ? OnScenePlay() : OnSceneStop(); }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && (m_ViewportFocused || m_SceneHierarchyPanel.IsHierarchyFocused()))
                DeleteSelectedEntity();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && m_SceneState == SceneState::Play && Input::IsKeyPressed(Key::LeftShift))
                OnSceneStop();

            bool flying = Input::IsMouseButtonPressed(MouseButton::Right);
            if (!ctrl && !flying && m_SceneState == SceneState::Edit && (m_ViewportHovered || m_ViewportFocused))
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_GizmoMode = GizmoMode::None;
                if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoMode = GizmoMode::Translate;
                if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoMode = GizmoMode::Rotate;
                if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoMode = GizmoMode::Scale;
                if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusEntity(m_SceneHierarchyPanel.GetSelectedEntity());
            }
        }

        UI_MenuBar();
        UI_StatusBar();
        UI_DockSpace();

        m_SceneHierarchyPanel.OnImGuiRender(&m_ShowHierarchy, &m_ShowInspector);
        m_ContentBrowserPanel.OnImGuiRender(&m_ShowContentBrowser);
        m_ConsolePanel.OnImGuiRender(&m_ShowConsole);
        UI_SceneSettings();
        m_ProjectSettingsPanel.SetScene(m_EditorScene); // tag counts / renames apply to the saved scene
        m_ProjectSettingsPanel.OnImGuiRender(&m_ShowProjectSettings);
        UI_Stats();
        UI_Viewport();
        if (m_MaximizeOnPlay && m_SceneState == SceneState::Play)
            UI_MaximizedGame();
        if (m_ShowImGuiDemo) ImGui::ShowDemoWindow(&m_ShowImGuiDemo);

        UI_Modals();
        if (m_ShowLauncher)
            UI_Launcher();

        if (m_PendingUndo && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            CommitUndo();

        static bool lastDirty = false;
        if (lastDirty != m_SceneDirty)
        {
            lastDirty = m_SceneDirty;
            UpdateWindowTitle();
        }
    }

    void EditorLayer::UI_DockSpace()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiID dockspaceID = ImGui::GetID("ZaminDockSpace");
        if (!m_LayoutInitialized)
        {
            UI_BuildDefaultLayout(dockspaceID);
            m_LayoutInitialized = true;
        }
        ImGui::DockSpaceOverViewport(dockspaceID, viewport, ImGuiDockNodeFlags_None);
    }

    void EditorLayer::UI_BuildDefaultLayout(ImGuiID dockspaceID)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspaceID);
        ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->WorkSize);

        ImGuiID main = dockspaceID;
        ImGuiID left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.18f, nullptr, &main);
        ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.26f, nullptr, &main);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.30f, nullptr, &main);

        ImGui::DockBuilderDockWindow("###Hierarchy", left);
        ImGui::DockBuilderDockWindow("###Inspector", right);
        ImGui::DockBuilderDockWindow("###SceneSettings", right);
        ImGui::DockBuilderDockWindow("###ProjectSettings", right);
        ImGui::DockBuilderDockWindow("###ContentBrowser", bottom);
        ImGui::DockBuilderDockWindow("###Console", bottom);
        ImGui::DockBuilderDockWindow("###Stats", left);
        ImGui::DockBuilderDockWindow("###Viewport", main);
        ImGui::DockBuilderFinish(dockspaceID);
    }

    void EditorLayer::UI_MenuBar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 7));
        if (!ImGui::BeginMainMenuBar())
        {
            ImGui::PopStyleVar();
            return;
        }
        ImGui::PopStyleVar();

        ImGui::PushStyleColor(ImGuiCol_Text, UI::AccentColor());
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(ICON_FA_EARTH_ASIA " ZAMIN");
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool hasProject = Project::GetActive() != nullptr;
        if (ImGui::BeginMenu("Fayl"))
        {
            if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS "  Yangi loyiha...")) m_ShowLauncher = true;
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Loyihani ochish...", "Ctrl+Shift+O")) OpenProjectDialog();
            if (ImGui::BeginMenu(ICON_FA_LIST "  So'nggi loyihalar", !m_RecentProjects.empty()))
            {
                for (auto& p : m_RecentProjects)
                    if (ImGui::MenuItem(p.stem().string().c_str(), p.parent_path().string().c_str()))
                    {
                        OpenProject(p);
                        break;
                    }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(ICON_FA_GEAR "  Loyiha sozlamalari...", nullptr, false, hasProject))
            {
                m_ShowProjectSettings = true;
                m_ProjectSettingsPanel.Focus();
            }
            if (ImGui::MenuItem(ICON_FA_XMARK "  Loyihani yopish", nullptr, false, hasProject)) CloseProject();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_FILE "  Yangi sahna", "Ctrl+N")) NewScene();
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Sahnani ochish...", "Ctrl+O")) OpenScene();
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Saqlash", "Ctrl+S")) SaveScene();
            if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Boshqacha saqlash...", "Ctrl+Shift+S")) SaveSceneAs();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_ROCKET "  O'yinni build qilish...", nullptr, false, hasProject))
            {
                m_BuildGameOutput = (Project::GetProjectDirectory() / "Build" / "Game").string();
                m_OpenBuildGamePopup = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_XMARK "  Chiqish", "Alt+F4")) Application::Get().Close();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tahrirlash"))
        {
            bool sel = (bool)m_SceneHierarchyPanel.GetSelectedEntity();
            bool editing = m_SceneState == SceneState::Edit;
            if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Bekor qilish (Undo)", "Ctrl+Z", false, editing && (!m_UndoStack.empty() || m_PendingUndo))) Undo();
            if (ImGui::MenuItem(ICON_FA_ROTATE_RIGHT "  Qaytarish (Redo)", "Ctrl+Y", false, editing && !m_RedoStack.empty())) Redo();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_COPY "  Nusxalash", "Ctrl+C", false, sel)) CopySelectedEntity();
            if (ImGui::MenuItem(ICON_FA_PASTE "  Joylash", "Ctrl+V", false, editing)) PasteEntity();
            if (ImGui::MenuItem(ICON_FA_BOX "  Prefab sifatida saqlash", nullptr, false, sel && Project::GetActive())) SavePrefab(m_SceneHierarchyPanel.GetSelectedEntity());
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CLONE "  Nusxa olish", "Ctrl+D", false, sel)) DuplicateSelectedEntity();
            if (ImGui::MenuItem(ICON_FA_TRASH "  O'chirish", "Delete", false, sel)) DeleteSelectedEntity();
            if (ImGui::MenuItem(ICON_FA_BULLSEYE "  Fokus", "F", false, sel)) FocusEntity(m_SceneHierarchyPanel.GetSelectedEntity());
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Obyekt"))
        {
            if (m_SceneState == SceneState::Edit)
                m_SceneHierarchyPanel.DrawCreateMenu();
            else
                ImGui::TextDisabled("O'yin rejimida mavjud emas");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Skriptlar"))
        {
            if (ImGui::MenuItem(ICON_FA_HAMMER "  Kompilyatsiya + qayta yuklash", "Ctrl+B", false, hasProject && !m_Building)) BuildScripts(true);
            if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE "  Modulni qayta yuklash", nullptr, false, hasProject)) ReloadScriptModule();
            ImGui::MenuItem(ICON_FA_BOLT "  Avtomatik hot-reload", nullptr, &m_AutoReload);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_FILE_CODE "  Yangi C++ skript...", nullptr, false, hasProject)) { m_NewScriptName = "MyScript"; m_OpenNewScriptPopup = true; }
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Source papkasini ochish", nullptr, false, hasProject))
                Platform::OpenInFileBrowser(Project::GetSourceDirectory());
            ImGui::Separator();
            ImGui::TextDisabled("Ro'yxatdan o'tgan klasslar: %zu", ScriptRegistry::GetAll().size());
            for (auto& [n, info] : ScriptRegistry::GetAll())
                ImGui::BulletText("%s (%zu maydon)", n.c_str(), info.Fields.size());
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Oyna"))
        {
            ImGui::MenuItem(ICON_FA_LIST "  Ierarxiya", nullptr, &m_ShowHierarchy);
            ImGui::MenuItem(ICON_FA_SLIDERS "  Inspektor", nullptr, &m_ShowInspector);
            ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Kontent", nullptr, &m_ShowContentBrowser);
            ImGui::MenuItem(ICON_FA_TERMINAL "  Konsol", nullptr, &m_ShowConsole);
            ImGui::MenuItem(ICON_FA_MOUNTAIN_SUN "  Sahna sozlamalari", nullptr, &m_ShowSceneSettings);
            ImGui::MenuItem(ICON_FA_GEAR "  Loyiha sozlamalari", nullptr, &m_ShowProjectSettings);
            ImGui::MenuItem(ICON_FA_CHART_LINE "  Statistika", nullptr, &m_ShowStats);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_BORDER_ALL "  Standart joylashuv")) m_LayoutInitialized = false;
            ImGui::MenuItem("ImGui demo", nullptr, &m_ShowImGuiDemo);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Yordam"))
        {
            if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO "  Zamin haqida")) m_ShowAbout = true;
            ImGui::EndMenu();
        }

        // Project name on the right side
        if (auto project = Project::GetActive())
        {
            std::string label = std::string(ICON_FA_GAMEPAD "  ") + project->GetConfig().Name;
            float w = ImGui::CalcTextSize(label.c_str()).x + 20;
            ImGui::SameLine(ImGui::GetWindowWidth() - w);
            ImGui::TextDisabled("%s", label.c_str());
        }
        ImGui::EndMainMenuBar();
    }

    void EditorLayer::UI_StatusBar()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float height = ImGui::GetFrameHeight() + 2.0f;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
        if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags))
        {
            if (ImGui::BeginMenuBar())
            {
                const char* stateText = m_SceneState == SceneState::Play ? ICON_FA_PLAY "  O'yin" :
                                        m_SceneState == SceneState::Simulate ? ICON_FA_GEARS "  Simulyatsiya" : ICON_FA_FEATHER "  Tahrirlash";
                ImVec4 stateColor = m_SceneState == SceneState::Play ? ImVec4(0.3f, 0.85f, 0.5f, 1) :
                                    m_SceneState == SceneState::Simulate ? ImVec4(0.4f, 0.7f, 1.0f, 1) : ImVec4(0.7f, 0.72f, 0.76f, 1);
                ImGui::TextColored(stateColor, "%s", stateText);
                ImGui::TextDisabled("|");
                ImGui::TextDisabled("%s %s%s", ICON_FA_EARTH_ASIA, m_EditorScene ? m_EditorScene->GetName().c_str() : "", m_SceneDirty ? " *" : "");
                ImGui::TextDisabled("|");
                if (m_Building)
                {
                    float t = (float)ImGui::GetTime();
                    const char* spin[] = { "|", "/", "-", "\\" };
                    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.3f, 1), "%s Kompilyatsiya %s", ICON_FA_HAMMER, spin[(int)(t * 8) % 4]);
                }
                else if (ScriptEngine::IsModuleLoaded())
                    ImGui::TextDisabled("%s Skriptlar: %zu klass", ICON_FA_CODE, ScriptRegistry::GetAll().size());
                else
                    ImGui::TextDisabled("%s Skript moduli yuklanmagan", ICON_FA_CODE);
                if (m_Exporting)
                {
                    ImGui::TextDisabled("|");
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1), ICON_FA_ROCKET " Eksport...");
                }

                char fps[64];
                std::snprintf(fps, sizeof(fps), "%.0f FPS  (%.2f ms)", Application::Get().GetFPS(), Application::Get().GetFrameTime());
                float w = ImGui::CalcTextSize(fps).x + 16;
                ImGui::SameLine(ImGui::GetWindowWidth() - w);
                ImGui::TextDisabled("%s", fps);
                ImGui::EndMenuBar();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::UI_Viewport()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin(ICON_FA_GAMEPAD "  Viewport###Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImVec2 regionMin = ImGui::GetWindowContentRegionMin();
        ImVec2 regionMax = ImGui::GetWindowContentRegionMax();
        ImVec2 offset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { regionMin.x + offset.x, regionMin.y + offset.y };
        m_ViewportBounds[1] = { regionMax.x + offset.x, regionMax.y + offset.y };

        m_ViewportFocused = ImGui::IsWindowFocused();
        ImVec2 panelSize = ImGui::GetContentRegionAvail();
        m_ViewportSize = { std::max(1.0f, panelSize.x), std::max(1.0f, panelSize.y) };

        uint64_t textureID = m_FinalFramebuffer->GetColorAttachmentRendererID(0);
        ImGui::SetNextItemAllowOverlap();
        ImGui::Image((ImTextureID)textureID, ImVec2(m_ViewportSize.x, m_ViewportSize.y), ImVec2(0, 1), ImVec2(1, 0));
        m_ViewportHovered = ImGui::IsItemHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportHovered);

        // Drop scenes / prefabs onto the viewport
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                fs::path path = Project::GetAssetFileSystemPath((const char*)payload->Data);
                std::string ext = path.extension().string();
                if (ext == ".zscene")
                    OpenScene(path);
                else if ((ext == ".png" || ext == ".jpg" || ext == ".jpeg") && m_SceneState == SceneState::Edit)
                {
                    Entity e = m_SceneHierarchyPanel.CreateEntityPreset("Sprite");
                    auto& sprite = e.GetComponent<SpriteRendererComponent>();
                    sprite.TexturePath = (const char*)payload->Data;
                    sprite.Texture = TextureLibrary::Load(path);
                    e.SetName(path.stem().string());
                }
                else if (ext == ".zprefab" && m_SceneState == SceneState::Edit)
                {
                    InstantiatePrefabInEditor((const char*)payload->Data, {});
                }
                else if ((ext == ".obj" || ext == ".gltf" || ext == ".glb") && m_SceneState == SceneState::Edit)
                {
                    Entity e = m_SceneHierarchyPanel.CreateEntityPreset("Model");
                    auto& mr = e.GetComponent<MeshRendererComponent>();
                    mr.ModelPath = (const char*)payload->Data;
                    Scene::ResolveMeshRenderer(mr);
                    e.SetName(path.stem().string());
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (m_SceneState != SceneState::Play)
        {
            UI_Gizmos();
            if (m_ShowIcons) UI_ViewportIcons();
        }
        UI_ViewportToolbar();

        // Play-mode border
        if (m_SceneState != SceneState::Edit)
        {
            ImU32 col = m_SceneState == SceneState::Play ? IM_COL32(60, 200, 120, 200) : IM_COL32(80, 150, 255, 200);
            ImGui::GetWindowDrawList()->AddRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y),
                                                ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), col, 0.0f, 0, 2.0f);
        }

        if (!m_ActiveScene->GetPrimaryCameraEntity() && m_SceneState == SceneState::Play)
        {
            ImGui::SetCursorPos(ImVec2(regionMin.x + 20, regionMax.y - 40));
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1), ICON_FA_TRIANGLE_EXCLAMATION "  Sahnada asosiy (Primary) kamera yo'q!");
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::UI_MaximizedGame()
    {
        // Full-window game view while playing (the docked viewport keeps rendering underneath).
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("##MaximizedGame", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
        ImVec2 size = ImGui::GetContentRegionAvail();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        m_ViewportBounds[0] = { pos.x, pos.y };
        m_ViewportBounds[1] = { pos.x + size.x, pos.y + size.y };
        m_ViewportSize = { std::max(1.0f, size.x), std::max(1.0f, size.y) };
        m_ViewportFocused = true;
        ImGui::SetNextItemAllowOverlap();
        ImGui::Image((ImTextureID)(uint64_t)m_FinalFramebuffer->GetColorAttachmentRendererID(0), size, ImVec2(0, 1), ImVec2(1, 0));
        m_ViewportHovered = ImGui::IsItemHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportHovered);

        // small stop button + hint
        ImGui::SetCursorPos(ImVec2(size.x * 0.5f - 40, 10));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1));
        if (UI::ToolbarButton(ICON_FA_STOP "  To'xtatish", "Ctrl+P", false, ImVec2(110, 0)))
            OnSceneStop();
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::UI_ViewportToolbar()
    {
        ImVec2 regionMin = ImGui::GetWindowContentRegionMin();
        float width = m_ViewportBounds[1].x - m_ViewportBounds[0].x;
        float buttonSize = ImGui::GetFrameHeight() + 4.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        auto panelBg = [&](ImVec2 pos, float w) {
            ImVec2 a(m_ViewportBounds[0].x + pos.x - 4, m_ViewportBounds[0].y + pos.y - 4);
            ImVec2 b(a.x + w + 8, a.y + buttonSize + 8);
            dl->AddRectFilled(a, b, IM_COL32(18, 20, 24, 210), 7.0f);
        };

        // ---- left: gizmo tools
        {
            ImVec2 pos(10, 10);
            float w = buttonSize * 4 + 2 * 3;
            panelBg(pos, w);
            ImGui::SetCursorPos(ImVec2(regionMin.x + pos.x, regionMin.y + pos.y));
            ImVec2 size(buttonSize, buttonSize);
            if (UI::ToolbarButton(ICON_FA_ARROW_POINTER, "Tanlash (Q)", m_GizmoMode == GizmoMode::None, size)) m_GizmoMode = GizmoMode::None;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Ko'chirish (W)", m_GizmoMode == GizmoMode::Translate, size)) m_GizmoMode = GizmoMode::Translate;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_ROTATE, "Aylantirish (E)", m_GizmoMode == GizmoMode::Rotate, size)) m_GizmoMode = GizmoMode::Rotate;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Masshtab (R)", m_GizmoMode == GizmoMode::Scale, size)) m_GizmoMode = GizmoMode::Scale;
        }

        // ---- center: play controls
        {
            float w = buttonSize * 4 + 2 * 3;
            ImVec2 pos((width - w) * 0.5f, 10);
            panelBg(pos, w);
            ImGui::SetCursorPos(ImVec2(regionMin.x + pos.x, regionMin.y + pos.y));
            ImVec2 size(buttonSize, buttonSize);
            bool editing = m_SceneState == SceneState::Edit;

            ImGui::PushStyleColor(ImGuiCol_Text, m_SceneState == SceneState::Play ? ImVec4(0.95f, 0.4f, 0.4f, 1) : ImVec4(0.35f, 0.9f, 0.55f, 1));
            if (UI::ToolbarButton(m_SceneState == SceneState::Play ? ICON_FA_STOP : ICON_FA_PLAY,
                                  m_SceneState == SceneState::Play ? "To'xtatish (Ctrl+P)" : "O'ynash (Ctrl+P)", false, size))
                m_SceneState == SceneState::Play ? OnSceneStop() : OnScenePlay();
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, m_SceneState == SceneState::Simulate ? ImVec4(0.95f, 0.4f, 0.4f, 1) : ImVec4(0.45f, 0.7f, 1.0f, 1));
            if (UI::ToolbarButton(m_SceneState == SceneState::Simulate ? ICON_FA_STOP : ICON_FA_GEARS,
                                  m_SceneState == SceneState::Simulate ? "Simulyatsiyani to'xtatish" : "Fizikani simulyatsiya qilish", false, size))
                m_SceneState == SceneState::Simulate ? OnSceneStop() : OnSceneSimulate();
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::BeginDisabled(editing);
            bool paused = !editing && m_ActiveScene->IsPaused();
            if (UI::ToolbarButton(ICON_FA_PAUSE, "Pauza", paused, size)) OnScenePause();
            ImGui::SameLine();
            ImGui::BeginDisabled(!paused);
            if (UI::ToolbarButton(ICON_FA_FORWARD_STEP, "Bir kadr oldinga", false, size)) m_ActiveScene->Step(1);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }

        // ---- right: view options
        {
            float w = buttonSize * 6 + 2 * 5;
            ImVec2 pos(width - w - 10, 10);
            panelBg(pos, w);
            ImGui::SetCursorPos(ImVec2(regionMin.x + pos.x, regionMin.y + pos.y));
            ImVec2 size(buttonSize, buttonSize);
            if (UI::ToolbarButton(m_2DMode ? "2D" : "3D", m_2DMode ? "2D rejim (bosing: 3D)" : "3D rejim (bosing: 2D)", m_2DMode, size))
                Set2DMode(!m_2DMode);
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_BORDER_ALL, "Grid", m_ShowGrid, size)) m_ShowGrid = !m_ShowGrid;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_VECTOR_SQUARE, "Fizik colliderlar", m_ShowColliders, size)) m_ShowColliders = !m_ShowColliders;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_EYE, "Ikonkalar", m_ShowIcons, size)) m_ShowIcons = !m_ShowIcons;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_GLOBE, m_GizmoLocal ? "Lokal koordinatalar" : "Global koordinatalar", m_GizmoLocal, size))
                m_GizmoLocal = !m_GizmoLocal;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "O'yinda to'liq oyna (Maximize on Play)", m_MaximizeOnPlay, size))
                m_MaximizeOnPlay = !m_MaximizeOnPlay;
        }

        // camera fly speed hint
        if (m_EditorCamera.IsFlying())
        {
            ImGui::SetCursorPos(ImVec2(regionMin.x + 12, regionMin.y + buttonSize + 26));
            ImGui::TextColored(ImVec4(1, 1, 1, 0.75f), ICON_FA_PERSON_RUNNING "  Tezlik: %.1f  (g'ildirak bilan o'zgartiring)", m_EditorCamera.FlySpeed);
        }

        ImGui::PopStyleVar(2);
    }

    void EditorLayer::UI_ViewportIcons()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        glm::mat4 viewProj = m_EditorCamera.GetViewProjection();
        glm::vec2 size = m_ViewportBounds[1] - m_ViewportBounds[0];
        ImVec2 mouse = ImGui::GetMousePos();
        Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();

        auto drawIcon = [&](Entity e, const char* icon, ImU32 color) {
            glm::vec4 clip = viewProj * glm::vec4(m_ActiveScene->GetWorldPosition(e), 1.0f);
            if (clip.w <= 0.0f) return;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.1f || std::abs(ndc.y) > 1.1f) return;
            ImVec2 p(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * size.x, m_ViewportBounds[0].y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y);
            float r = 14.0f;
            bool hovered = (mouse.x - p.x) * (mouse.x - p.x) + (mouse.y - p.y) * (mouse.y - p.y) < r * r;
            bool isSelected = selected == e;
            dl->AddCircleFilled(p, r, isSelected ? IM_COL32(30, 140, 110, 230) : (hovered ? IM_COL32(50, 55, 65, 230) : IM_COL32(20, 22, 26, 200)));
            dl->AddCircle(p, r, color, 0, 1.5f);
            ImVec2 ts = ImGui::CalcTextSize(icon);
            dl->AddText(ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), color, icon);
            if (hovered && m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver())
                m_SceneHierarchyPanel.SetSelectedEntity(e);
        };

        for (auto e : m_ActiveScene->GetAllEntitiesWith<CameraComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_CAMERA, IM_COL32(220, 220, 230, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<DirectionalLightComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_SUN, IM_COL32(255, 210, 90, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<PointLightComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_LIGHTBULB, IM_COL32(255, 230, 140, 255));
        for (auto e : m_ActiveScene->GetAllEntitiesWith<AudioSourceComponent>())
            drawIcon({ e, m_ActiveScene.get() }, ICON_FA_VOLUME_HIGH, IM_COL32(220, 140, 220, 255));
    }

    void EditorLayer::UI_Gizmos()
    {
        Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
        if (!selected || !selected.IsValid() || m_GizmoMode == GizmoMode::None || m_SceneHierarchyPanel.IsTilePainting())
            return;

        ImGuizmo::SetOrthographic(m_EditorCamera.IsOrthographic());
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x, m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        const glm::mat4& projection = m_EditorCamera.GetProjection();
        glm::mat4 view = m_EditorCamera.GetViewMatrix();

        auto& tc = selected.GetComponent<TransformComponent>();
        glm::mat4 transform = m_ActiveScene->GetWorldTransform(selected);

        bool snap = Input::IsKeyPressed(Key::LeftControl);
        float snapValue = m_GizmoMode == GizmoMode::Rotate ? 15.0f : 0.5f;
        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::OPERATION op = m_GizmoMode == GizmoMode::Translate ? ImGuizmo::TRANSLATE :
                                 m_GizmoMode == GizmoMode::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        ImGuizmo::MODE mode = (m_GizmoLocal || m_GizmoMode == GizmoMode::Scale) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), op, mode, glm::value_ptr(transform),
                             nullptr, snap ? snapValues : nullptr);

        if (ImGuizmo::IsUsing())
        {
            Entity parent = m_ActiveScene->GetParent(selected);
            glm::mat4 local = parent ? glm::inverse(m_ActiveScene->GetWorldTransform(parent)) * transform : transform;
            glm::vec3 translation, rotation, scale;
            if (Math::DecomposeTransform(local, translation, rotation, scale))
            {
                glm::vec3 deltaRotation = rotation - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;
                MarkDirty();
            }
        }
    }

    void EditorLayer::UI_SceneSettings()
    {
        if (!m_ShowSceneSettings)
            return;
        if (!ImGui::Begin(ICON_FA_MOUNTAIN_SUN "  Sahna###SceneSettings", &m_ShowSceneSettings))
        {
            ImGui::End();
            return;
        }
        auto& s = m_ActiveScene->GetSettings();
        bool changed = false;

        ImGui::SeparatorText(ICON_FA_CLOUD "  Muhit");
        if (UI::BeginPropertyGrid("env"))
        {
            std::string name = m_ActiveScene->GetName();
            if (UI::PropertyString("Nomi", name)) { m_ActiveScene->SetName(name); changed = true; }
            changed |= UI::PropertyBool("Osmon", s.DrawSky);
            changed |= UI::PropertyAssetPath("HDRI (IBL)", s.EnvironmentMap, ".hdr;.png;.jpg;.jpeg", ICON_FA_GLOBE);
            if (!s.EnvironmentMap.empty())
            {
                changed |= UI::PropertySlider("HDRI kuchi", s.EnvironmentIntensity, 0.0f, 5.0f);
                float deg = glm::degrees(s.EnvironmentRotation);
                if (UI::PropertySlider("HDRI burilishi", deg, -180.0f, 180.0f)) { s.EnvironmentRotation = glm::radians(deg); changed = true; }
            }
            changed |= UI::PropertyColor3("Osmon (tepa)", s.SkyTopColor);
            changed |= UI::PropertyColor3("Ufq", s.SkyHorizonColor);
            changed |= UI::PropertyColor3("Yer", s.GroundColor);
            changed |= UI::PropertySlider("Atrof yorug'ligi", s.AmbientIntensity, 0.0f, 3.0f);
            changed |= UI::PropertySlider("Ekspozitsiya", s.Exposure, 0.1f, 4.0f);
            changed |= UI::PropertyFloat("Soya masofasi", s.ShadowDistance, 0.5f, 5.0f, 500.0f);
            changed |= UI::PropertyBool("Tuman", s.FogEnabled);
            if (s.FogEnabled)
            {
                changed |= UI::PropertyColor3("Tuman rangi", s.FogColor);
                changed |= UI::PropertyFloat("Tuman zichligi", s.FogDensity, 0.0005f, 0.0f, 1.0f, "%.4f");
                changed |= UI::PropertyFloat("Balandlik so'nishi", s.FogHeightFalloff, 0.005f, 0.0f, 5.0f);
            }
            UI::EndPropertyGrid();
        }

        ImGui::SeparatorText(ICON_FA_WAND_MAGIC_SPARKLES "  Post-processing");
        if (UI::BeginPropertyGrid("post"))
        {
            auto& p = s.Post;
            const char* tonemaps[] = { "Yo'q (2D)", "ACES (film)", "Reinhard" };
            int tm = (int)p.Tonemap;
            if (UI::PropertyCombo("Tonemapping", tm, tonemaps, 3)) { p.Tonemap = (TonemapMode)tm; changed = true; }
            changed |= UI::PropertyBool("SSAO (soyalanish)", p.SSAO);
            if (p.SSAO)
            {
                changed |= UI::PropertySlider("SSAO radiusi", p.SSAORadius, 0.05f, 3.0f);
                changed |= UI::PropertySlider("SSAO kuchi", p.SSAOIntensity, 0.2f, 4.0f);
            }
            changed |= UI::PropertyBool("Bloom (yorqinlik)", p.Bloom);
            if (p.Bloom)
            {
                changed |= UI::PropertySlider("Bloom chegarasi", p.BloomThreshold, 0.0f, 1.0f);
                changed |= UI::PropertySlider("Bloom kuchi", p.BloomIntensity, 0.0f, 3.0f);
                changed |= UI::PropertySlider("Bloom radiusi", p.BloomRadius, 0.5f, 3.0f);
            }
            changed |= UI::PropertySlider("Vinyetka", p.Vignette, 0.0f, 1.0f);
            changed |= UI::PropertySlider("To'yinganlik", p.Saturation, 0.0f, 2.0f);
            changed |= UI::PropertySlider("Kontrast", p.Contrast, 0.5f, 2.0f);
            changed |= UI::PropertySlider("Yorqinlik", p.Brightness, -0.5f, 0.5f);
            changed |= UI::PropertyColor3("Rang tuslash", p.Tint);
            UI::EndPropertyGrid();
        }

        ImGui::SeparatorText(ICON_FA_ATOM "  Fizika 2D");
        if (UI::BeginPropertyGrid("phys"))
        {
            changed |= UI::PropertyFloat2("Gravitatsiya", s.Gravity);
            changed |= UI::PropertyFloat3("Gravitatsiya (3D)", s.Gravity3D);
            changed |= UI::PropertyInt("Tezlik iteratsiyalari", s.VelocityIterations);
            changed |= UI::PropertyInt("Pozitsiya iteratsiyalari", s.PositionIterations);
            UI::EndPropertyGrid();
        }

        ImGui::SeparatorText(ICON_FA_CAMERA "  Editor kamerasi");
        if (UI::BeginPropertyGrid("cam"))
        {
            UI::PropertyFloat("Uchish tezligi", m_EditorCamera.FlySpeed, 0.1f, 0.5f, 200.0f);
            UI::EndPropertyGrid();
        }
        ImGui::TextDisabled("RMB + WASD/QE: uchish  |  Alt+LMB: aylanish");
        ImGui::TextDisabled("MMB: surish  |  G'ildirak: yaqinlashtirish");

        if (auto project = Project::GetActive())
        {
            ImGui::SeparatorText(ICON_FA_GAMEPAD "  Loyiha");
            auto& c = project->GetConfig();
            if (UI::BeginPropertyGrid("proj"))
            {
                UI::PropertyLabel("Nomi");
                ImGui::TextUnformatted(c.Name.c_str());
                UI::PropertyLabel("Boshlang'ich sahna");
                ImGui::TextUnformatted(c.StartScene.empty() ? "-" : c.StartScene.generic_string().c_str());
                UI::EndPropertyGrid();
            }
            if (!m_EditorScenePath.empty() && ImGui::Button(ICON_FA_HOUSE "  Shu sahnani boshlang'ich qilish"))
            {
                c.StartScene = Project::GetAssetRelativePath(m_EditorScenePath);
                Project::SaveActive(Project::GetProjectFilePath());
            }
            if (ImGui::Button(ICON_FA_GAMEPAD "  Input va loyiha sozlamalari"))
            {
                m_ShowProjectSettings = true;
                m_ProjectSettingsPanel.Focus();
            }
        }

        if (changed) MarkDirty();
        ImGui::End();
    }

    void EditorLayer::UI_Stats()
    {
        if (!m_ShowStats)
            return;
        if (ImGui::Begin(ICON_FA_CHART_LINE "  Statistika###Stats", &m_ShowStats))
        {
            auto& stats = Renderer::GetStats();
            static float history[120] = {};
            static int offset = 0;
            history[offset] = Application::Get().GetFrameTime();
            offset = (offset + 1) % 120;
            ImGui::Text("FPS: %.0f   Kadr: %.2f ms", Application::Get().GetFPS(), Application::Get().GetFrameTime());
            ImGui::PlotLines("##ft", history, 120, offset, nullptr, 0.0f, 40.0f, ImVec2(-1, 50));
            if (UI::BeginPropertyGrid("stats"))
            {
                UI::PropertyLabel("Draw calls"); ImGui::Text("%u", stats.DrawCalls);
                UI::PropertyLabel("Quadlar"); ImGui::Text("%u", stats.QuadCount);
                UI::PropertyLabel("Doiralar"); ImGui::Text("%u", stats.CircleCount);
                UI::PropertyLabel("Chiziqlar"); ImGui::Text("%u", stats.LineCount);
                UI::PropertyLabel("Meshlar"); ImGui::Text("%u", stats.MeshCount);
                UI::PropertyLabel("Uchburchaklar"); ImGui::Text("%u", stats.TriangleCount);
                UI::PropertyLabel("Obyektlar"); ImGui::Text("%zu", m_ActiveScene->GetRegistry().view<IDComponent>().size());
                UI::PropertyLabel("Hover");
                ImGui::TextUnformatted(m_HoveredEntity ? m_HoveredEntity.GetName().c_str() : "-");
                UI::EndPropertyGrid();
            }

            ImGui::SeparatorText(ICON_FA_CHART_LINE "  Profiler (CPU, ms)");
            const auto& entries = Profiler::GetLastFrame();
            float total = std::max(0.001f, Profiler::GetLastFrameTotal());
            if (ImGui::BeginTable("##prof", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                for (const auto& e : entries)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(e.Name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.2f", e.Milliseconds);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::ProgressBar(std::min(1.0f, e.Milliseconds / total), ImVec2(-1, ImGui::GetTextLineHeight()), "");
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    void EditorLayer::UI_Modals()
    {
        // ---- new script
        if (m_OpenNewScriptPopup) { ImGui::OpenPopup("Yangi C++ skript"); m_OpenNewScriptPopup = false; }
        if (ImGui::BeginPopupModal("Yangi C++ skript", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Klass nomi (masalan: PlayerController)");
            ImGui::SetNextItemWidth(320);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##scriptname", &m_NewScriptName, ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button(ICON_FA_CHECK "  Yaratish", ImVec2(150, 0)) || enter)
            {
                CreateScriptClass(m_NewScriptName);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(150, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- build game
        if (m_OpenBuildGamePopup) { ImGui::OpenPopup("O'yinni build qilish"); m_OpenBuildGamePopup = false; }
        if (ImGui::BeginPopupModal("O'yinni build qilish", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(ICON_FA_ROCKET "  O'yin mustaqil papkaga eksport qilinadi (Linux).");
            ImGui::TextDisabled("Tarkibi: ZaminPlayer, libZamin.so, Resources, Assets, skript moduli, .sh launcher");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(460);
            ImGui::InputText("##out", &m_BuildGameOutput);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FOLDER_OPEN))
            {
                std::string dir = FileDialogs::SelectFolder(Project::GetProjectDirectory());
                if (!dir.empty()) m_BuildGameOutput = dir;
            }
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_HAMMER "  Build", ImVec2(150, 0)))
            {
                BuildGame(m_BuildGameOutput);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Bekor qilish", ImVec2(150, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- about
        if (m_ShowAbout) { ImGui::OpenPopup("Zamin haqida"); m_ShowAbout = false; }
        if (ImGui::BeginPopupModal("Zamin haqida", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushFont(ImGuiLayer::GetTitleFont());
            ImGui::TextColored(UI::AccentColor(), ICON_FA_EARTH_ASIA "  Zamin Engine 0.1");
            ImGui::PopFont();
            ImGui::TextUnformatted("C++20 da yozilgan o'yin dvigateli. O'yinlar ham C++ da yoziladi.");
            ImGui::Spacing();
            ImGui::TextDisabled("Renderer: OpenGL 4.1 (PBR, soyalar, 2D batch)");
            ImGui::TextDisabled("ECS: EnTT  |  Fizika: Box2D  |  Audio: miniaudio  |  UI: Dear ImGui");
            ImGui::TextDisabled("Platforma: Linux (Windows, macOS, Android, iOS - rejada)");
            ImGui::Spacing();
            if (ImGui::Button("Yopish", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void EditorLayer::UI_Launcher()
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 size(std::min(980.0f, vp->WorkSize.x - 40), std::min(620.0f, vp->WorkSize.y - 40));
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::OpenPopup("##Launcher");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        bool open = ImGui::BeginPopupModal("##Launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar(2);
        if (!open)
            return;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        float sidebar = 300.0f;

        // Sidebar with branding
        dl->AddRectFilledMultiColor(wp, ImVec2(wp.x + sidebar, wp.y + size.y),
                                    IM_COL32(18, 80, 66, 255), IM_COL32(14, 40, 48, 255), IM_COL32(12, 18, 24, 255), IM_COL32(16, 52, 50, 255));
        ImGui::SetCursorPos(ImVec2(28, 40));
        ImGui::BeginGroup();
        ImGui::PushFont(ImGuiLayer::GetLargeIconFont());
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.85f, 1.0f), ICON_FA_EARTH_ASIA);
        ImGui::PopFont();
        ImGui::PushFont(ImGuiLayer::GetTitleFont());
        ImGui::TextUnformatted("Zamin Engine");
        ImGui::PopFont();
        ImGui::TextColored(ImVec4(0.75f, 0.85f, 0.85f, 1), "C++ o'yin dvigateli  v0.1");
        ImGui::Dummy(ImVec2(0, 30));
        ImGui::PushTextWrapPos(sidebar - 28);
        ImGui::TextColored(ImVec4(0.75f, 0.8f, 0.82f, 1), "2D va 3D o'yinlarni yarating. O'yin mantiqi C++ skriptlarda yoziladi, editor ularni avtomatik kompilyatsiya qilib qayta yuklaydi.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();

        ImGui::SetCursorPos(ImVec2(28, size.y - 50));
        if (Project::GetActive() && ImGui::Button(ICON_FA_XMARK "  Yopish", ImVec2(120, 0)))
        {
            m_ShowLauncher = false;
            ImGui::CloseCurrentPopup();
        }

        // Content
        ImGui::SetCursorPos(ImVec2(sidebar + 30, 30));
        ImGui::BeginChild("##launcherContent", ImVec2(size.x - sidebar - 60, size.y - 60));

        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(ICON_FA_FILE_CIRCLE_PLUS "  Yangi loyiha");
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##pname", "Loyiha nomi", &m_NewProjectName);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2 * ImGui::GetFrameHeight() - 140);
        ImGui::InputTextWithHint("##ploc", "Joylashuv", &m_NewProjectLocation);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##browse"))
        {
            std::string dir = FileDialogs::SelectFolder(m_NewProjectLocation);
            if (!dir.empty()) m_NewProjectLocation = dir;
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.52f, 0.41f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.62f, 0.50f, 1.0f));
        bool canCreate = !m_NewProjectName.empty() && !m_NewProjectLocation.empty();
        ImGui::BeginDisabled(!canCreate);
        if (ImGui::Button(ICON_FA_PLUS "  Yaratish", ImVec2(120, 0)))
        {
            std::string clean;
            for (char ch : m_NewProjectName)
                if (std::isalnum((unsigned char)ch) || ch == '_') clean += ch;
            if (!clean.empty())
            {
                NewProject(m_NewProjectLocation, clean);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(2);
        ImGui::TextDisabled("Shablon: 3D sahna + Rotator va PlayerController C++ skriptlari");

        ImGui::Dummy(ImVec2(0, 14));
        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(ICON_FA_FOLDER_OPEN "  Loyihani ochish");
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2 * ImGui::GetFrameHeight() - 140);
        ImGui::InputTextWithHint("##openpath", ".zproject fayl yoki papka yo'li", &m_OpenProjectPath);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##browse2"))
        {
            std::string file = FileDialogs::OpenFile("Zamin Project (*.zproject) | *.zproject");
            if (!file.empty()) m_OpenProjectPath = file;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CHECK "  Ochish", ImVec2(120, 0)) && !m_OpenProjectPath.empty())
        {
            if (OpenProject(m_OpenProjectPath))
                ImGui::CloseCurrentPopup();
        }

        // Recent + examples
        auto projectCard = [&](const fs::path& path, const char* icon) {
            ImGui::PushID(path.string().c_str());
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            float h = ImGui::GetFrameHeight() * 2.0f;
            bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, h));
            bool hovered = ImGui::IsItemHovered();
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hovered ? IM_COL32(46, 188, 148, 40) : IM_COL32(255, 255, 255, 8), 6.0f);
            d->AddText(ImGuiLayer::GetBoldFont(), ImGui::GetFontSize(), ImVec2(p.x + 14, p.y + 6), ImGui::GetColorU32(ImGuiCol_Text),
                       (std::string(icon) + "   " + path.stem().string()).c_str());
            d->AddText(ImVec2(p.x + 14 + ImGui::GetFontSize() * 1.6f, p.y + h * 0.5f + 1), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                       path.parent_path().string().c_str());
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 2));
            return clicked;
        };

        ImGui::Dummy(ImVec2(0, 14));
        std::vector<fs::path> examples;
        {
            fs::path exe = FileSystem::GetExecutableDirectory();
            for (const fs::path& base : { exe / ".." / ".." / "Projects", exe / ".." / "Projects", exe / "Projects" })
            {
                std::error_code ec;
                if (!fs::exists(base, ec)) continue;
                for (auto& dir : fs::directory_iterator(base, ec))
                    for (auto& f : fs::directory_iterator(dir.path(), ec))
                        if (f.path().extension() == ".zproject")
                            examples.push_back(fs::weakly_canonical(f.path()));
                break;
            }
        }
        if (!examples.empty())
        {
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            ImGui::TextUnformatted(ICON_FA_WAND_MAGIC_SPARKLES "  Namuna o'yinlar");
            ImGui::PopFont();
            ImGui::Spacing();
            for (auto& ex : examples)
                if (projectCard(ex, ICON_FA_GAMEPAD)) { if (OpenProject(ex)) ImGui::CloseCurrentPopup(); break; }
            ImGui::Dummy(ImVec2(0, 10));
        }

        ImGui::PushFont(ImGuiLayer::GetBoldFont());
        ImGui::TextUnformatted(ICON_FA_LIST "  So'nggi loyihalar");
        ImGui::PopFont();
        ImGui::Spacing();
        if (m_RecentProjects.empty())
            ImGui::TextDisabled("Hali loyiha ochilmagan");
        for (auto& p : m_RecentProjects)
            if (projectCard(p, ICON_FA_FOLDER)) { if (OpenProject(p)) ImGui::CloseCurrentPopup(); break; }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

}
