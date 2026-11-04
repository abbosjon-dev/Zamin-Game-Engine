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
#include <format>
#include <map>
#include <string_view>

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
        LoadViewSettings();

        m_SceneHierarchyPanel.OnFocusRequested = [this](Entity e) { FocusEntity(e); };
        m_SceneHierarchyPanel.OnSceneModified = [this]() { MarkDirty(); };
        m_SceneHierarchyPanel.OnSavePrefabRequested = [this](Entity e) { SavePrefab(e); };
        m_SceneHierarchyPanel.OnProjectSettingsModified = [this]() { m_ProjectSettingsPanel.MarkDirty(); };
        m_SceneHierarchyPanel.SetFoldState(&m_ViewSettings.Inspector); // open / collapsed inspector headers, per user
        m_SceneHierarchyPanel.OnFoldStateModified = [this]() { MarkViewSettingsDirty(); };
        m_ProjectSettingsPanel.OnSceneModified = [this]() { MarkDirty(); };
        m_SceneHierarchyPanel.OnPrefabDropped = [this](const fs::path& p, Entity parent) { InstantiatePrefabInEditor(p, parent); };
        m_ContentBrowserPanel.OnOpenScene = [this](const fs::path& p) { OpenScene(p); };
        m_ContentBrowserPanel.OnRequestNewScript = [this]() { m_NewScriptName = "MyScript"; m_OpenNewScriptPopup = true; };
        m_ContentBrowserPanel.OnNewScene = [this](const fs::path& p) {
            Ref<Scene> scene = CreateRef<Scene>();
            scene->SetName(p.stem().string());
            SerializeScene(scene, p);
        };
        m_ContentBrowserPanel.SetSettings(&m_ViewSettings.ContentBrowser); // grid / list, filter, sorting: per user
        m_ContentBrowserPanel.OnSettingsModified = [this]() { MarkViewSettingsDirty(); };
        m_ContentBrowserPanel.OnAssetsMoved = [this](const AssetFiles::MoveResult& result) { OnAssetsMoved(result); };

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
        if (m_ViewSettingsDirty)
            SaveViewSettings();
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
            if (const char* sel = std::getenv("ZAMIN_EDITOR_SELECT")) // "Name" or "A,B,C" (multi-selection, the last is primary)
            {
                if (Entity e = m_ActiveScene->FindEntityByName(sel))
                    m_SceneHierarchyPanel.SetSelectedEntity(e);
                else
                {
                    std::vector<Entity> picked;
                    std::string_view names(sel);
                    while (!names.empty())
                    {
                        size_t comma = names.find(',');
                        if (Entity e = m_ActiveScene->FindEntityByName(names.substr(0, comma)))
                            picked.push_back(e);
                        names = comma == std::string_view::npos ? std::string_view() : names.substr(comma + 1);
                    }
                    m_SceneHierarchyPanel.GetSelection().Set(picked);
                }
            }
            if (const char* pivot = std::getenv("ZAMIN_EDITOR_PIVOT_CENTER"))
                m_GizmoPivotCenter = std::string_view(pivot) != "0";
            if (const char* filter = std::getenv("ZAMIN_EDITOR_FILTER")) // e.g. "#Coin"
                m_SceneHierarchyPanel.SetFilter(filter);
            if (std::getenv("ZAMIN_EDITOR_INSPECTOR_BOTTOM"))
                m_SceneHierarchyPanel.ScrollInspectorToBottom();
            if (std::getenv("ZAMIN_EDITOR_PLAY"))
                OnScenePlay();
            if (std::getenv("ZAMIN_EDITOR_STATS"))
                m_ShowStats = true;
            if (std::getenv("ZAMIN_EDITOR_FRAME_ALL"))
                FrameAll();
            // Content browser: folder (relative to the project), list view, type filter ("Texture,Model").
            if (const char* dir = std::getenv("ZAMIN_EDITOR_CB_DIR"))
                m_ContentBrowserPanel.SetCurrentDirectory(Project::GetProjectDirectory() / dir);
            if (const char* list = std::getenv("ZAMIN_EDITOR_CB_LIST"))
                m_ViewSettings.ContentBrowser.ListView = std::string_view(list) != "0";
            if (const char* filter = std::getenv("ZAMIN_EDITOR_CB_FILTER"))
            {
                m_ViewSettings.ContentBrowser.Filter.Clear();
                std::string_view keys(filter);
                while (!keys.empty())
                {
                    size_t comma = keys.find(',');
                    if (auto type = AssetFiles::FindTypeByKey(keys.substr(0, comma)))
                        m_ViewSettings.ContentBrowser.Filter.Set(*type, true);
                    keys = comma == std::string_view::npos ? std::string_view() : keys.substr(comma + 1);
                }
            }
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
            if (const char* ns = std::getenv("ZAMIN_EDITOR_NEW_SCRIPT")) // "<Class>[|minimal]" or "dialog"
            {
                std::string v = ns;
                if (v == "dialog")
                {
                    m_NewScriptName = "EnemyAI";
                    m_OpenNewScriptPopup = true;
                }
                else
                {
                    size_t bar = v.find('|');
                    bool minimal = bar != std::string::npos && v.substr(bar + 1) == "minimal";
                    CreateScriptClass(v.substr(0, bar), minimal ? ScriptTemplateKind::Minimal : ScriptTemplateKind::Full);
                }
            }
        }

        // Simulated file drops (no drag & drop source in CI): ZAMIN_EDITOR_DROP="a.png;b.wav" at frame
        // ZAMIN_EDITOR_DROP_FRAME (30), or a request file ZAMIN_EDITOR_DROP_FILE whose lines are the
        // paths (an optional first line "@x,y" is the mouse position in window pixels).
        if (const char* drop = std::getenv("ZAMIN_EDITOR_DROP"))
        {
            const char* at = std::getenv("ZAMIN_EDITOR_DROP_FRAME");
            if (frame == (at ? std::max(6, std::atoi(at)) : 30))
            {
                std::string_view list(drop);
                while (!list.empty())
                {
                    size_t sep = list.find(';');
                    if (sep != 0)
                        m_PendingFileDrop.emplace_back(list.substr(0, sep));
                    list = sep == std::string_view::npos ? std::string_view() : list.substr(sep + 1);
                }
                m_PendingFileDropPos = Input::GetMousePosition();
            }
        }
        if (const char* request = std::getenv("ZAMIN_EDITOR_DROP_FILE"); request && frame % 10 == 0 && fs::exists(request))
        {
            std::string text = FileSystem::ReadTextFile(request);
            std::error_code ec;
            fs::remove(request, ec);
            m_PendingFileDropPos = Input::GetMousePosition();
            size_t start = 0;
            while (start < text.size())
            {
                size_t end = text.find('\n', start);
                std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                start = end == std::string::npos ? text.size() : end + 1;
                if (line.empty())
                    continue;
                float x, y;
                if (line[0] == '@' && std::sscanf(line.c_str() + 1, "%f,%f", &x, &y) == 2)
                    m_PendingFileDropPos = { x, y };
                else
                    m_PendingFileDrop.push_back(line);
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

        if (m_PendingClickThrough)
            ResolveClickThrough();

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
        options.ShowGrid = m_ViewSettings.Grid.Visible;
        options.GridPlane = m_2DMode ? 1 : 0;
        options.Grid = m_ViewSettings.Grid;
        options.SkipEditorHidden = true; // the hierarchy's eye: edit + simulate views only (play draws everything)

        // The camera does not take a mouse drag that belongs to the gizmo (Alt + drag on the
        // gizmo duplicates instead of orbiting; Alt pressed in the middle of a gizmo drag).
        if (m_GizmoAltDrag && !Input::IsMouseButtonPressed(MouseButton::Left))
            m_GizmoAltDrag = m_GizmoAltDuplicatePending = false;
        bool cameraInput = m_ViewportHovered && !m_GizmoAltDrag && !m_GizmoEdit.IsActive();

        switch (m_SceneState)
        {
            case SceneState::Edit:
                UpdateTilePainting();
                m_EditorCamera.OnUpdate(ts, cameraInput);
                m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera, options);
                break;
            case SceneState::Simulate:
                m_EditorCamera.OnUpdate(ts, cameraInput);
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
            m_HoveredPixel = { mouseX, mouseY };
        }
        else
        {
            m_HoveredEntity = {};
            m_HoveredPixel = { -1, -1 };
        }

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
        // Inactive entities get no outlines (they have no bodies), nor do the ones hidden with
        // the hierarchy's eye outside play mode (the game view shows everything).
        SceneRenderOptions skipped;
        skipped.SkipEditorHidden = m_SceneState != SceneState::Play;
        std::unordered_set<entt::entity> hidden = scene.CollectRenderSkipped(skipped);

        if (m_ShowColliders)
        {
            const glm::vec4 green(0.3f, 0.95f, 0.45f, 1.0f), yellow(0.95f, 0.8f, 0.2f, 1.0f);
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, BoxCollider2DComponent>())
            {
                if (hidden.contains(e)) continue;
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
                if (hidden.contains(e)) continue;
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
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                auto& c = entity.GetComponent<BoxCollider3DComponent>();
                glm::mat4 t = scene.GetWorldTransform(entity) * glm::translate(glm::mat4(1.0f), c.Offset) * glm::scale(glm::mat4(1.0f), c.HalfExtents * 2.0f);
                DrawWireBox(t, c.IsSensor ? yellow : cyan);
            }
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, SphereCollider3DComponent>())
            {
                if (hidden.contains(e)) continue;
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
                if (hidden.contains(e)) continue;
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
                if (hidden.contains(e)) continue;
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
        if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity(); selected && selected.IsValid() && selected.HasComponent<TilemapComponent>() && !hidden.contains(selected))
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

        // Selection outline: the primary entity orange, the rest of a multi-selection yellow. A
        // selected inactive entity keeps its outline (it shows where the switched-off entity is);
        // one hidden with the eye does not.
        Entity primary = m_SceneHierarchyPanel.GetSelectedEntity();
        for (Entity selected : m_SceneHierarchyPanel.GetSelection().GetEntities())
        {
            if (!selected.IsValid() || selected.GetScene() != &scene) continue;
            if (skipped.SkipEditorHidden && scene.IsHiddenInEditor(selected)) continue;
            const glm::vec4 orange = selected == primary ? glm::vec4(1.0f, 0.6f, 0.1f, 1.0f) : glm::vec4(1.0f, 0.85f, 0.35f, 0.85f);
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
                    Renderer2D::DrawLine(glm::vec3(transform[3]), scene.GetWorldPosition(c), glm::vec4(orange.r, orange.g, orange.b, 0.5f));
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
            // Handled in OnImGuiRender (inside a frame). Where the mouse is decides where the files go.
            m_PendingFileDrop.insert(m_PendingFileDrop.end(), ev.GetPaths().begin(), ev.GetPaths().end());
            m_PendingFileDropPos = Input::GetMousePosition();
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
            if (alt && overGizmo && m_SceneState == SceneState::Edit && !m_SceneHierarchyPanel.IsTilePainting())
            {
                // Alt + drag on the gizmo: drag copies of the selection, the originals stay.
                m_GizmoAltDrag = true;
                m_GizmoAltDuplicatePending = true;
            }
            if (!alt && !overGizmo && !m_SceneHierarchyPanel.IsTilePainting())
            {
                // Ctrl+click toggles, Shift+click adds to the selection. A click on a locked
                // entity goes through it: the next frame picks again without the locked ones
                // (hidden ones are not drawn at all).
                bool ctrl = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
                bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);
                Entity picked = m_HoveredEntity;
                if (picked && m_ActiveScene->IsLockedInEditor(picked) && m_HoveredPixel.x >= 0)
                    m_PendingClickThrough = PendingPick{ m_HoveredPixel, ctrl, shift, m_ActiveScene.get() };
                else
                    m_SceneHierarchyPanel.SelectWithModifiers(picked && !m_ActiveScene->IsLockedInEditor(picked) ? picked : Entity(), ctrl, shift);
            }
        }
        return false;
    }

    void EditorLayer::ResolveClickThrough()
    {
        PendingPick pick = *m_PendingClickThrough;
        m_PendingClickThrough.reset();
        // Only for the scene and view the click was made in.
        if (pick.SceneAtClick != m_ActiveScene.get() || m_SceneState == SceneState::Play)
            return;
        const auto& spec = m_Framebuffer->GetSpecification();
        Entity picked;
        if (pick.Pixel.x >= 0 && pick.Pixel.y >= 0 && pick.Pixel.x < (int)spec.Width && pick.Pixel.y < (int)spec.Height)
        {
            // The scene once more from the same camera, without the locked (and hidden)
            // entities; only the entity IDs matter. The frame's real pass clears it all again.
            m_Framebuffer->Bind();
            RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
            RenderCommand::Clear();
            m_Framebuffer->ClearAttachment(1, -1);
            SceneRenderOptions options;
            options.SkipEditorHidden = true;
            options.SkipEditorLocked = true;
            m_ActiveScene->OnRenderEditor(m_EditorCamera, options);
            int pixel = m_Framebuffer->ReadPixel(1, pick.Pixel.x, pick.Pixel.y);
            m_Framebuffer->Unbind();
            if (pixel != -1)
                picked = Entity((entt::entity)pixel, m_ActiveScene.get());
            if (picked && (!picked.IsValid() || m_ActiveScene->IsLockedInEditor(picked)))
                picked = {};
        }
        m_SceneHierarchyPanel.SelectWithModifiers(picked, pick.Ctrl, pick.Shift);
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
        m_GizmoEdit.End();
        // The same entities (by UUID) stay selected: play mode copies, undo snapshots.
        m_SceneHierarchyPanel.SetContext(m_ActiveScene, true);
    }

    void EditorLayer::NewScene()
    {
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        m_EditorScene = CreateRef<Scene>();
        m_EditorScene->SetName("Yangi sahna");
        SetActiveScene(m_EditorScene);
        m_EditorScenePath.clear();
        m_ViewSettings.Bookmarks.erase(std::string()); // bookmarks of the previous unsaved scene
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
        if (m_EditorScenePath.empty())
        {
            // Camera bookmarks made before the first save belong to the new file now.
            auto unsaved = m_ViewSettings.Bookmarks.find(std::string());
            if (unsaved != m_ViewSettings.Bookmarks.end())
            {
                if (!unsaved->second.IsEmpty())
                {
                    m_ViewSettings.Bookmarks[EditorViewSettings::SceneKey(p)] = unsaved->second;
                    MarkViewSettingsDirty();
                }
                m_ViewSettings.Bookmarks.erase(std::string());
            }
        }
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
        bool midDrag = m_GizmoEdit.IsActive() && m_GizmoEdit.GetScene() == m_ActiveScene.get();
        // Every selected subtree at once: one undo step (MarkDirty via OnSceneModified). In the
        // middle of a drag only the dragged (unlocked) subtrees are copied.
        size_t count = m_SceneHierarchyPanel.DuplicateSelection(midDrag);
        if (count == 0)
            return;
        if (midDrag)
        {
            // Ctrl+D during a gizmo drag: the originals stay where they are now and the drag
            // goes on with the copies (press again to leave more copies along the way). The
            // gizmo's current matrix is where the copies are, so the edit restarts from it.
            m_GizmoEdit.Begin(*m_ActiveScene, GetGizmoTargets(), m_GizmoMatrix);
            ShowViewportMessage(count > 1 ? std::format(ICON_FA_CLONE "  {} ta nusxa qoldirildi", count)
                                          : std::string(ICON_FA_CLONE "  Nusxa qoldirildi"));
        }
        else if (count > 1)
            ZM_INFO("{} ta obyekt nusxalandi", count);
    }

    void EditorLayer::DeleteSelectedEntity()
    {
        if (m_SceneState != SceneState::Edit) return;
        m_GizmoEdit.End();
        size_t count = m_SceneHierarchyPanel.DeleteSelection();
        if (count > 1)
            ZM_INFO("{} ta obyekt o'chirildi", count);
    }

    std::vector<Entity> EditorLayer::GetGizmoTargets()
    {
        std::vector<Entity> targets = m_SceneHierarchyPanel.GetSelectedRoots();
        std::erase_if(targets, [&](Entity e) { return !e.IsValid() || m_ActiveScene->IsLockedInEditor(e); });
        return targets;
    }

    void EditorLayer::ToggleSelectionHidden()
    {
        bool hidden = false;
        size_t count = m_SceneHierarchyPanel.ToggleSelectionHidden(&hidden);
        if (count == 0)
            return;
        std::string what = count > 1 ? std::format("{} ta obyekt", count) : m_SceneHierarchyPanel.GetSelectedEntity().GetName();
        ShowViewportMessage(hidden ? std::format(ICON_FA_EYE_SLASH "  Yashirildi: {}  (H: ko'rsatish, Alt+H: hammasi)", what)
                                   : std::format(ICON_FA_EYE "  Ko'rsatildi: {}", what));
    }

    void EditorLayer::ToggleSelectionLocked()
    {
        bool locked = false;
        size_t count = m_SceneHierarchyPanel.ToggleSelectionLocked(&locked);
        if (count == 0)
            return;
        std::string what = count > 1 ? std::format("{} ta obyekt", count) : m_SceneHierarchyPanel.GetSelectedEntity().GetName();
        ShowViewportMessage(locked ? std::format(ICON_FA_LOCK "  Qulflandi: {}", what) : std::format(ICON_FA_LOCK_OPEN "  Qulf ochildi: {}", what));
    }

    void EditorLayer::ShowAllHidden()
    {
        size_t count = m_SceneHierarchyPanel.ShowAll();
        ShowViewportMessage(count ? std::format(ICON_FA_EYE "  {} ta yashirin obyekt ko'rsatildi", count)
                                  : std::string(ICON_FA_EYE "  Yashirin obyekt yo'q"));
    }

    void EditorLayer::FocusEntity(Entity entity)
    {
        if (entity && entity.IsValid())
            FrameEntities({ entity });
    }

    bool EditorLayer::FrameEntities(const std::vector<Entity>& entities)
    {
        AABB bounds;
        if (!m_ActiveScene->ComputeBounds(entities, bounds, true))
            return false;
        m_EditorCamera.TransitionTo(m_EditorCamera.ComputeFrameState(bounds), 0.3f);
        return true;
    }

    void EditorLayer::FrameSelection()
    {
        if (!FrameEntities(m_SceneHierarchyPanel.GetSelectedRoots()))
            ShowViewportMessage(ICON_FA_BULLSEYE "  Hech narsa tanlanmagan (Shift+F: butun sahna)");
    }

    void EditorLayer::FrameAll()
    {
        AABB bounds;
        if (!m_ActiveScene->ComputeSceneBounds(bounds))
        {
            // Everything may be hidden with the eye button (those are left out of the framing).
            bool empty = m_ActiveScene->GetAllEntitiesWith<IDComponent>().empty();
            ShowViewportMessage(empty ? ICON_FA_EXPAND "  Sahna bo'sh" : ICON_FA_EXPAND "  Ko'rinadigan obyekt yo'q");
            return;
        }
        m_EditorCamera.TransitionTo(m_EditorCamera.ComputeFrameState(bounds), 0.35f);
        ShowViewportMessage(ICON_FA_EXPAND "  Butun sahna");
    }

    CameraBookmarks& EditorLayer::GetSceneBookmarks()
    {
        return m_ViewSettings.GetBookmarks(m_EditorScenePath);
    }

    void EditorLayer::SaveCameraBookmark(int slot)
    {
        if (slot < 1 || slot > CameraBookmarks::SlotCount) return;
        GetSceneBookmarks().Set(slot, m_EditorCamera.GetViewState());
        MarkViewSettingsDirty();
        ShowViewportMessage(std::format(ICON_FA_BOOKMARK "  Kamera xatcho'pi {} saqlandi", slot));
        ZM_INFO("Kamera xatcho'pi {} saqlandi", slot);
    }

    void EditorLayer::RecallCameraBookmark(int slot)
    {
        const EditorCameraState* state = GetSceneBookmarks().Get(slot);
        if (!state)
        {
            ShowViewportMessage(std::format(ICON_FA_BOOKMARK "  Xatcho'p {} bo'sh (saqlash: Ctrl+{})", slot, slot));
            return;
        }
        m_2DMode = state->Orthographic; // the camera switches its projection itself
        m_EditorCamera.TransitionTo(*state, 0.35f);
        ShowViewportMessage(std::format(ICON_FA_BOOKMARK "  Xatcho'p {}", slot));
    }

    void EditorLayer::ShowViewportMessage(std::string text)
    {
        m_ViewportMessage = std::move(text);
        m_ViewportMessageTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    }

    void EditorLayer::LoadViewSettings()
    {
        fs::path path = ConfigDirectory() / "editor-view.json";
        std::error_code ec;
        if (fs::exists(path, ec) && !m_ViewSettings.Load(path))
            ZM_WARN("Viewport sozlamalari o'qilmadi (standart qiymatlar ishlatiladi): {}", path.string());
        m_ViewSettingsDirty = false;
    }

    void EditorLayer::SaveViewSettings()
    {
        m_ViewSettings.Snap.Sanitize();
        m_ViewSettings.Grid.Sanitize();
        if (!m_ViewSettings.Save(ConfigDirectory() / "editor-view.json"))
            ZM_WARN("Viewport sozlamalari saqlanmadi");
        m_ViewSettingsDirty = false;
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
        std::vector<Entity> roots = m_SceneHierarchyPanel.GetSelectedRoots();
        if (roots.empty()) return;
        std::vector<Entity> entities;
        for (Entity root : roots)
            CollectSubtree(*m_ActiveScene, root, entities);
        m_Clipboard = SceneSerializer::SerializeEntities(*m_ActiveScene, entities);
        ImGui::SetClipboardText(m_Clipboard.c_str());
        if (roots.size() == 1)
            ZM_INFO("Nusxalandi: {} ({} obyekt)", roots.front().GetName(), entities.size());
        else
            ZM_INFO("Nusxalandi: {} ta tanlangan ({} obyekt)", roots.size(), entities.size());
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
        // Every pasted root is selected (the list comes roots first).
        std::vector<Entity> roots;
        for (Entity e : entities)
            if (!m_EditorScene->GetParent(e))
                roots.push_back(e);
        m_SceneHierarchyPanel.GetSelection().Set(roots, roots.empty() ? entities.front() : roots.front());
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

    Entity EditorLayer::DropAssetOnViewport(const std::string& assetPath)
    {
        fs::path path = Project::GetAssetFileSystemPath(assetPath);
        AssetType type = AssetFiles::GetType(path);
        if (type == AssetType::Scene)
        {
            OpenScene(path);
            return {};
        }
        if (m_SceneState != SceneState::Edit)
            return {};
        Entity e;
        switch (type)
        {
            case AssetType::Texture:
            {
                e = m_SceneHierarchyPanel.CreateEntityPreset("Sprite");
                auto& sprite = e.GetComponent<SpriteRendererComponent>();
                sprite.TexturePath = assetPath;
                sprite.Texture = TextureLibrary::Load(path);
                break;
            }
            case AssetType::Model:
            {
                e = m_SceneHierarchyPanel.CreateEntityPreset("Model");
                auto& mr = e.GetComponent<MeshRendererComponent>();
                mr.ModelPath = assetPath;
                Scene::ResolveMeshRenderer(mr);
                break;
            }
            case AssetType::Audio:
                e = m_SceneHierarchyPanel.CreateEntityPreset("Audio");
                e.GetComponent<AudioSourceComponent>().Path = assetPath;
                break;
            case AssetType::Prefab:
                return InstantiatePrefabInEditor(assetPath, {});
            default:
                return {};
        }
        if (e)
            e.SetName(path.stem().string());
        return e;
    }

    // ================================================================== project files
    void EditorLayer::HandleFileDrop(const std::vector<std::string>& paths, glm::vec2 mouse)
    {
        std::vector<fs::path> files(paths.begin(), paths.end());
        // A project file opens that project, whatever else came with it.
        for (const fs::path& p : files)
            if (p.extension() == ".zproject")
            {
                OpenProject(p);
                return;
            }
        if (!Project::GetActive())
        {
            for (const fs::path& p : files)
                if (p.extension() == ".zscene")
                {
                    OpenScene(p);
                    return;
                }
            ZM_WARN("Fayllarni import qilish uchun avval loyiha oching");
            return;
        }

        fs::path projectDir = Project::GetProjectDirectory(), assets = Project::GetAssetDirectory(), source = Project::GetSourceDirectory();
        bool overBrowser = m_ContentBrowserPanel.ContainsScreenPoint(mouse.x, mouse.y);
        bool overViewport = !overBrowser && mouse.x >= m_ViewportBounds[0].x && mouse.y >= m_ViewportBounds[0].y &&
                            mouse.x < m_ViewportBounds[1].x && mouse.y < m_ViewportBounds[1].y;
        fs::path browserDir = m_ContentBrowserPanel.GetCurrentDirectory();
        bool browserInAssets = overBrowser && AssetFiles::IsInside(browserDir, assets);
        bool browserInSource = overBrowser && !source.empty() && AssetFiles::IsInside(browserDir, source);

        // Files of this project are not copied: a scene opens, the rest is shown in the browser.
        std::vector<fs::path> inProject;
        std::map<fs::path, std::vector<fs::path>> imports; // destination folder -> files
        for (const fs::path& p : files)
        {
            if (AssetFiles::IsInside(p, projectDir))
            {
                inProject.push_back(p);
                continue;
            }
            std::error_code ec;
            AssetType type = AssetFiles::GetType(p, fs::is_directory(p, ec));
            fs::path destination;
            if (type == AssetType::Script && !source.empty())
                destination = browserInSource ? browserDir : source; // C++ files belong to the script module
            else if (browserInAssets)
                destination = browserDir;
            else
                destination = assets / AssetFiles::GetImportFolder(type);
            imports[destination].push_back(p);
        }

        bool openedScene = false;
        std::vector<fs::path> reveal;
        for (const fs::path& p : inProject)
        {
            if (p.extension() == ".zscene" && !openedScene)
                openedScene = OpenScene(p);
            else
                reveal.push_back(p);
        }

        std::vector<fs::path> imported;
        bool scripts = false;
        for (const auto& [destination, sources] : imports)
        {
            AssetFiles::ImportResult result = AssetFiles::Import(sources, destination);
            for (const std::string& error : result.Errors)
                ZM_WARN("Import: {}", error);
            for (const fs::path& file : result.Imported)
            {
                bool code = AssetFiles::IsInside(file, source);
                scripts |= code;
                ZM_INFO("Import qilindi: {}", code ? file.lexically_relative(fs::weakly_canonical(projectDir)).generic_string()
                                                   : Project::GetAssetRelativePath(file));
                imported.push_back(file);
            }
        }

        if (imported.empty())
        {
            if (!reveal.empty())
                m_ContentBrowserPanel.Reveal(reveal);
            return;
        }
        m_ContentBrowserPanel.Refresh();
        m_ContentBrowserPanel.Reveal(imported);
        fs::path folder = imported.front().parent_path();
        std::string where = AssetFiles::IsInside(folder, assets) ? Project::GetAssetRelativePath(folder) : folder.filename().string();
        std::string notice = imported.size() == 1 ? std::format("'{}' import qilindi ({})", imported.front().filename().string(), where)
                                                  : std::format("{} ta fayl import qilindi", imported.size());
        if (scripts)
            notice += " - skriptlarni kompilyatsiya qiling (Ctrl+B)";
        m_ContentBrowserPanel.ShowNotice(notice);

        // Dropped onto the viewport: the imported assets go into the scene, too. A dropped scene
        // opens first (unless one of the project's own did already) and the rest goes into it:
        // opening it later would throw away the entities just made.
        if (overViewport && m_SceneState == SceneState::Edit)
        {
            std::vector<fs::path> toPlace;
            for (const fs::path& file : imported)
            {
                std::error_code ec;
                if (fs::is_directory(file, ec) || !AssetFiles::IsInside(file, assets))
                    continue;
                if (AssetFiles::GetType(file) == AssetType::Scene)
                {
                    if (!openedScene)
                        openedScene = OpenScene(file);
                    continue;
                }
                toPlace.push_back(file);
            }
            std::vector<Entity> created;
            for (const fs::path& file : toPlace)
                if (Entity e = DropAssetOnViewport(Project::GetAssetRelativePath(file)))
                    created.push_back(e);
            // Only what is in the open scene can be selected.
            std::erase_if(created, [&](Entity e) { return !e.IsValid() || e.GetScene() != m_ActiveScene.get(); });
            if (created.size() > 1)
                m_SceneHierarchyPanel.GetSelection().Set(created);
            if (!created.empty())
                notice += created.size() > 1 ? std::format(", sahnaga {} ta obyekt qo'shildi", created.size()) : std::string(", sahnaga qo'shildi");
        }
        if (!overBrowser) // the browser shows it in its footer already
            ShowViewportMessage(std::string(ICON_FA_FILE_IMPORT "  ") + notice);
    }

    void EditorLayer::OnAssetsMoved(const AssetFiles::MoveResult& result)
    {
        if (result.Moved.empty())
            return;
        std::error_code ec;
        fs::path assets = fs::weakly_canonical(Project::GetAssetDirectory(), ec);

        // Asset paths as scenes store them (relative to Assets).
        AssetFiles::PathMoves moves;
        for (const auto& m : result.Moved)
        {
            std::string from = m.From.lexically_relative(assets).generic_string(), to = m.To.lexically_relative(assets).generic_string();
            if (!from.empty() && !to.empty() && !from.starts_with("..") && !to.starts_with(".."))
                moves.emplace_back(from, to);
        }
        auto remap = [&](std::string& text) {
            if (text.empty())
                return (size_t)0;
            // Asset paths only (as in the files): names and tags equal to a moved path stay.
            return AssetFiles::RewriteJsonStrings(text, [&](std::string_view key, const std::string& value) {
                return AssetFiles::RemapAssetReference(key, value, moves);
            });
        };

        if (!moves.empty() && m_EditorScene)
        {
            // The open scene in memory (it may have unsaved changes; its file is already rewritten).
            std::string text = SceneSerializer(m_EditorScene).SerializeToString();
            if (size_t n = remap(text); n > 0)
            {
                Ref<Scene> scene = CreateRef<Scene>();
                if (SceneSerializer(scene).DeserializeFromString(text))
                {
                    scene->ResolveAssets();
                    m_EditorScene = scene;
                    // Playing: the running copy keeps what it loaded; Stop returns to the updated scene.
                    if (m_SceneState == SceneState::Edit)
                        SetActiveScene(m_EditorScene);
                    ZM_INFO("Ochiq sahnada {} ta havola yangilandi", n);
                }
            }
            // Undo / redo snapshots and the entity clipboard use the new paths, too.
            for (std::string& snapshot : m_UndoStack) remap(snapshot);
            for (std::string& snapshot : m_RedoStack) remap(snapshot);
            remap(m_LastSnapshot);
            remap(m_Clipboard);
        }

        // The open scene file itself (or its folder) moved.
        if (!m_EditorScenePath.empty())
        {
            fs::path current = fs::weakly_canonical(m_EditorScenePath, ec);
            fs::path moved = result.MapPath(current);
            if (moved != current)
            {
                m_EditorScenePath = moved;
                UpdateWindowTitle();
            }
        }
        for (const auto& m : result.Moved)
            if (m_ViewSettings.MoveSceneBookmarks(m.From, m.To) > 0)
                MarkViewSettingsDirty();

        // The project's start scene.
        if (auto project = Project::GetActive())
        {
            auto& start = project->GetConfig().StartScene;
            for (const auto& [from, to] : moves)
                if (auto mapped = AssetFiles::RemapPath(start.generic_string(), from, to))
                {
                    start = *mapped;
                    Project::SaveActive(Project::GetProjectFilePath());
                    ZM_INFO("Boshlang'ich sahna: {}", *mapped);
                    break;
                }
        }
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

    bool EditorLayer::CreateScriptClass(const std::string& className, ScriptTemplateKind kind)
    {
        if (!Project::GetActive()) return false;
        if (std::string problem = ScriptTemplate::ValidateClassName(className); !problem.empty())
        {
            ZM_ERROR("Skript yaratilmadi: {}", problem);
            return false;
        }
        if (ScriptRegistry::Find(className))
        {
            ZM_WARN("'{}' klassi allaqachon mavjud (skript modulida ro'yxatdan o'tgan)", className);
            return false;
        }
        fs::path file = Project::GetSourceDirectory() / (className + ".cpp");
        if (fs::exists(file))
        {
            ZM_WARN("Fayl allaqachon mavjud: {}", file.string());
            return false;
        }
        if (!FileSystem::WriteTextFile(file, ScriptTemplate::Generate(className, kind)))
        {
            ZM_ERROR("Fayl yozilmadi: {}", file.string());
            return false;
        }
        ZM_INFO("Yangi skript yaratildi ({} shablon): {}", ScriptTemplate::GetKindName(kind), file.string());
        BuildScripts(true);
        return true;
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
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false) &&
                (m_SceneHierarchyPanel.IsHierarchyFocused() || (m_ViewportFocused && m_SceneState != SceneState::Play)))
                m_SceneHierarchyPanel.SelectAll();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && m_SceneState == SceneState::Play && Input::IsKeyPressed(Key::LeftShift))
                OnSceneStop();
            // H: hide / show the selection in the scene view, Alt+H: show everything again.
            if (!ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_H, false) && m_SceneState == SceneState::Edit &&
                (m_SceneHierarchyPanel.IsHierarchyFocused() || m_ViewportFocused || m_ViewportHovered) &&
                !Input::IsMouseButtonPressed(MouseButton::Right))
            {
                if (io.KeyAlt) ShowAllHidden();
                else ToggleSelectionHidden();
            }

            bool flying = Input::IsMouseButtonPressed(MouseButton::Right);
            bool viewportKeys = !flying && (m_ViewportHovered || m_ViewportFocused);
            if (!ctrl && viewportKeys && m_SceneState == SceneState::Edit)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_GizmoMode = GizmoMode::None;
                if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoMode = GizmoMode::Translate;
                if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoMode = GizmoMode::Rotate;
                if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoMode = GizmoMode::Scale;
            }
            // Editor camera (edit + simulate): F frames the selection, Shift+F the whole scene,
            // Ctrl+1..5 stores the view in a bookmark, 1..5 flies back to it. (Not Home: ImGui's
            // keyboard navigation uses it.)
            if (viewportKeys && m_SceneState != SceneState::Play)
            {
                if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) { shift ? FrameAll() : FrameSelection(); }
                for (int slot = 1; slot <= CameraBookmarks::SlotCount; slot++)
                {
                    if (!ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + slot), false))
                        continue;
                    if (ctrl) SaveCameraBookmark(slot);
                    else if (!shift && !io.KeyAlt) RecallCameraBookmark(slot);
                }
            }
        }

        if (!m_PendingFileDrop.empty())
        {
            std::vector<std::string> dropped = std::move(m_PendingFileDrop);
            m_PendingFileDrop.clear();
            HandleFileDrop(dropped, m_PendingFileDropPos);
        }

        UI_MenuBar();
        UI_StatusBar();
        UI_DockSpace();

        m_SceneHierarchyPanel.SetSceneEditable(m_SceneState == SceneState::Edit);
        m_SceneHierarchyPanel.OnImGuiRender(&m_ShowHierarchy, &m_ShowInspector);
        m_ContentBrowserPanel.OnImGuiRender(&m_ShowContentBrowser);
        m_ConsolePanel.OnImGuiRender(&m_ShowConsole);
        UI_SceneSettings();
        m_ProjectSettingsPanel.SetScene(m_EditorScene); // tag counts / renames apply to the saved scene
        m_ProjectSettingsPanel.SetSceneEditable(m_SceneState == SceneState::Edit);
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
        if (m_ViewSettingsDirty && !ImGui::IsAnyItemActive())
            SaveViewSettings();

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
            size_t count = m_SceneHierarchyPanel.GetSelectionCount();
            std::string suffix = count > 1 ? std::format(" ({} ta)", count) : std::string();
            if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Bekor qilish (Undo)", "Ctrl+Z", false, editing && (!m_UndoStack.empty() || m_PendingUndo))) Undo();
            if (ImGui::MenuItem(ICON_FA_ROTATE_RIGHT "  Qaytarish (Redo)", "Ctrl+Y", false, editing && !m_RedoStack.empty())) Redo();
            ImGui::Separator();
            if (ImGui::MenuItem((ICON_FA_COPY "  Nusxalash" + suffix).c_str(), "Ctrl+C", false, sel)) CopySelectedEntity();
            if (ImGui::MenuItem(ICON_FA_PASTE "  Joylash", "Ctrl+V", false, editing)) PasteEntity();
            if (ImGui::MenuItem(ICON_FA_BOX "  Prefab sifatida saqlash", nullptr, false, sel && Project::GetActive())) SavePrefab(m_SceneHierarchyPanel.GetSelectedEntity());
            ImGui::Separator();
            if (ImGui::MenuItem((ICON_FA_CLONE "  Nusxa olish" + suffix).c_str(), "Ctrl+D", false, sel && editing)) DuplicateSelectedEntity();
            if (ImGui::MenuItem((ICON_FA_TRASH "  O'chirish" + suffix).c_str(), "Delete", false, sel && editing)) DeleteSelectedEntity();
            if (ImGui::MenuItem(ICON_FA_BULLSEYE "  Fokus", "F", false, sel)) FrameSelection();
            ImGui::Separator();
            {
                Entity primary = m_SceneHierarchyPanel.GetSelectedEntity();
                bool hidden = primary && m_ActiveScene->IsHiddenInEditor(primary, false);
                bool locked = primary && m_ActiveScene->IsLockedInEditor(primary, false);
                if (ImGui::MenuItem(((hidden ? ICON_FA_EYE "  Ko'rsatish" : ICON_FA_EYE_SLASH "  Yashirish (editorda)") + suffix).c_str(), "H", false, sel && editing))
                    ToggleSelectionHidden();
                if (ImGui::MenuItem(((locked ? ICON_FA_LOCK_OPEN "  Qulfni ochish" : ICON_FA_LOCK "  Qulflash (viewport'da)") + suffix).c_str(), nullptr, false, sel && editing))
                    ToggleSelectionLocked();
                if (ImGui::MenuItem(ICON_FA_EYE "  Hammasini ko'rsatish", "Alt+H", false, editing && m_ActiveScene->CountHiddenInEditor() > 0))
                    ShowAllHidden();
                if (ImGui::MenuItem(ICON_FA_LOCK_OPEN "  Hamma qulflarni ochish", nullptr, false, editing && m_ActiveScene->CountLockedInEditor() > 0))
                    m_SceneHierarchyPanel.UnlockAll();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CHECK_DOUBLE "  Hammasini tanlash", "Ctrl+A")) m_SceneHierarchyPanel.SelectAll();
            if (ImGui::MenuItem(ICON_FA_XMARK "  Tanlovni bekor qilish", nullptr, false, sel)) m_SceneHierarchyPanel.SetSelectedEntity({});
            ImGui::EndMenu();
        }

        UI_ViewMenu();

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
                if (m_ActiveScene)
                    UI_StatusBarSelection(ImGui::GetWindowWidth() - w - 12.0f);
                ImGui::SameLine(ImGui::GetWindowWidth() - w);
                ImGui::TextDisabled("%s", fps);
                ImGui::EndMenuBar();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::UI_StatusBarSelection(float maxX)
    {
        SelectionSummary s = SummarizeSelection(*m_ActiveScene, m_SceneHierarchyPanel.GetSelection());
        // Pieces are added while they fit before maxX (the FPS counter), the most useful first.
        auto fits = [&](const std::string& text) { return ImGui::GetCursorPosX() + ImGui::CalcTextSize(text.c_str()).x + 16.0f < maxX; };
        if (!fits("|"))
            return;
        ImGui::TextDisabled("|");
        if (s.Count == 0)
        {
            if (fits("Hech narsa tanlanmagan"))
                ImGui::TextDisabled(ICON_FA_ARROW_POINTER "  Hech narsa tanlanmagan");
            return;
        }

        // Name: click frames the selection, right click copies its name / UUID / path.
        std::string head = s.Count > 1 ? std::format(ICON_FA_OBJECT_GROUP "  {} ta obyekt  " ICON_FA_ARROW_RIGHT "  {}", s.Count, s.Name)
                                       : std::format("{}  {}", SceneHierarchyPanel::GetEntityIcon(s.Primary), s.Name);
        float headWidth = std::min(ImGui::CalcTextSize(head.c_str()).x, std::max(40.0f, maxX - ImGui::GetCursorPosX() - 16.0f));
        if (headWidth <= 40.0f && !fits(head))
            return;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.14f));
        if (ImGui::Selectable((head + "##statussel").c_str(), false, ImGuiSelectableFlags_None, ImVec2(headWidth, 0)) &&
            m_SceneState != SceneState::Play)
            FrameSelection();
        bool headHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##StatusSelectionMenu");
        ImGui::PopStyleColor(3);

        if (headHovered && ImGui::BeginTooltip())
        {
            ImGui::PushFont(ImGuiLayer::GetBoldFont());
            ImGui::TextUnformatted(s.Name.c_str());
            ImGui::PopFont();
            if (s.Count > 1)
                ImGui::TextDisabled("Asosiy obyekt (%zu ta tanlangan)", s.Count);
            ImGui::Separator();
            ImGui::Text(ICON_FA_DIAGRAM_PROJECT "  Yo'l: %s", s.Path.c_str());
            ImGui::Text(ICON_FA_HASHTAG "  UUID: %llu", (unsigned long long)s.ID);
            ImGui::Text(ICON_FA_CROSSHAIRS "  Joylashuv: %.3f, %.3f, %.3f", s.Position.x, s.Position.y, s.Position.z);
            if (s.Count > 1)
                ImGui::Text(ICON_FA_OBJECT_GROUP "  Tanlov markazi: %.3f, %.3f, %.3f", s.Center.x, s.Center.y, s.Center.z);
            std::string components;
            for (const std::string& key : s.Components)
            {
                std::string name = SceneHierarchyPanel::GetComponentDisplayName(key);
                if (key == ComponentKey<NativeScriptComponent>())
                    if (const auto* script = s.Primary.TryGetComponent<NativeScriptComponent>(); script && !script->ClassName.empty())
                        name += " (" + script->ClassName + ")";
                components += (components.empty() ? "" : ", ") + name;
            }
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
            ImGui::Text(ICON_FA_LAYER_GROUP "  Komponentlar (%zu): %s", s.Components.size(), components.c_str());
            if (!s.Tags.empty())
            {
                std::string tags;
                for (const std::string& tag : s.Tags)
                    tags += (tags.empty() ? "" : ", ") + tag;
                ImGui::Text(ICON_FA_TAGS "  Teglar: %s", tags.c_str());
            }
            ImGui::PopTextWrapPos();
            if (s.ChildCount > 0)
                ImGui::Text(ICON_FA_SITEMAP "  Bolalar: %zu (jami ichida %zu)", s.ChildCount, s.DescendantCount);
            if (s.Hidden || s.Locked)
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 1.0f), "%s%s", s.Hidden ? ICON_FA_EYE_SLASH "  yashirin   " : "",
                                   s.Locked ? ICON_FA_LOCK "  qulflangan" : "");
            if (s.Count > 1 && (s.HiddenCount > 0 || s.LockedCount > 0))
                ImGui::TextDisabled("Tanlanganlardan: %zu ta yashirin, %zu ta qulflangan", s.HiddenCount, s.LockedCount);
            ImGui::Separator();
            ImGui::TextDisabled("Bosing: kamerani yo'naltirish (F)  |  O'ng tugma: nusxalash");
            ImGui::EndTooltip();
        }
        if (ImGui::BeginPopup("##StatusSelectionMenu"))
        {
            if (ImGui::MenuItem(ICON_FA_COPY "  Nomini nusxalash"))
                ImGui::SetClipboardText(s.Name.c_str());
            if (ImGui::MenuItem(ICON_FA_HASHTAG "  UUID nusxalash"))
                ImGui::SetClipboardText(std::to_string(s.ID).c_str());
            if (ImGui::MenuItem(ICON_FA_DIAGRAM_PROJECT "  Ierarxiya yo'lini nusxalash"))
                ImGui::SetClipboardText(s.Path.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CROSSHAIRS "  Kamerani yo'naltirish", "F", false, m_SceneState != SceneState::Play))
                FrameSelection();
            if (ImGui::MenuItem(ICON_FA_XMARK "  Tanlovni bekor qilish"))
                m_SceneHierarchyPanel.GetSelection().Clear();
            ImGui::EndPopup();
        }

        // Position (2D: x / y), components, children, eye / lock.
        glm::vec3 p = s.Count > 1 ? s.Center : s.Position;
        std::string position = m_2DMode ? std::format("{}  {:.2f}, {:.2f}", s.Count > 1 ? "Markaz" : ICON_FA_CROSSHAIRS, p.x, p.y)
                                        : std::format("{}  {:.2f}, {:.2f}, {:.2f}", s.Count > 1 ? "Markaz" : ICON_FA_CROSSHAIRS, p.x, p.y, p.z);
        if (!fits(position))
            return;
        ImGui::TextDisabled("%s", position.c_str());
        std::string components = std::format(ICON_FA_LAYER_GROUP "  {} komponent", s.Components.size());
        if (!fits(components))
            return;
        ImGui::TextDisabled("%s", components.c_str());
        if (s.ChildCount > 0)
        {
            std::string children = std::format(ICON_FA_SITEMAP "  {} bola", s.ChildCount);
            if (!fits(children))
                return;
            ImGui::TextDisabled("%s", children.c_str());
        }
        if (s.Hidden && fits(ICON_FA_EYE_SLASH))
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 0.9f), ICON_FA_EYE_SLASH);
        if (s.Locked && fits(ICON_FA_LOCK))
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 0.9f), ICON_FA_LOCK);
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

        // Drop scenes / prefabs / images / models / sounds onto the viewport
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                DropAssetOnViewport((const char*)payload->Data);
            ImGui::EndDragDropTarget();
        }

        if (m_SceneState != SceneState::Play)
        {
            UI_Gizmos();
            if (m_ShowIcons) UI_ViewportIcons();
        }
        UI_ViewportToolbar();
        UI_ViewportMessage();

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
        const float spacing = 2.0f, caretWidth = std::floor(buttonSize * 0.55f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        auto panelBg = [&](ImVec2 pos, float w) {
            ImVec2 a(m_ViewportBounds[0].x + pos.x - 4, m_ViewportBounds[0].y + pos.y - 4);
            ImVec2 b(a.x + w + 8, a.y + buttonSize + 8);
            dl->AddRectFilled(a, b, IM_COL32(18, 20, 24, 210), 7.0f);
        };
        auto beginPanel = [&](ImVec2 pos, float w) {
            panelBg(pos, w);
            ImGui::SetCursorPos(ImVec2(regionMin.x + pos.x, regionMin.y + pos.y));
        };
        ImVec2 size(buttonSize, buttonSize);
        bool editing = m_SceneState == SceneState::Edit;
        auto& snap = m_ViewSettings.Snap;
        auto& grid = m_ViewSettings.Grid;
        // The play controls sit in the middle; the side panels next to them are left out when
        // the viewport is too narrow (snapping / camera stay reachable from the "Ko'rinish" menu).
        const float centerW = buttonSize * 4 + spacing * 3;
        const float centerMin = (width - centerW) * 0.5f - 12.0f, centerMax = (width + centerW) * 0.5f + 12.0f;

        // ---- left: gizmo tools | snapping
        float leftEnd = 10.0f;
        {
            ImVec2 pos(10, 10);
            float w = buttonSize * 5 + spacing * 4;
            beginPanel(pos, w);
            if (UI::ToolbarButton(ICON_FA_ARROW_POINTER, "Tanlash (Q)", m_GizmoMode == GizmoMode::None, size)) m_GizmoMode = GizmoMode::None;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Ko'chirish (W)\nAlt+sudrash: nusxasini sudrash", m_GizmoMode == GizmoMode::Translate, size)) m_GizmoMode = GizmoMode::Translate;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_ROTATE, "Aylantirish (E)", m_GizmoMode == GizmoMode::Rotate, size)) m_GizmoMode = GizmoMode::Rotate;
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Masshtab (R)", m_GizmoMode == GizmoMode::Scale, size)) m_GizmoMode = GizmoMode::Scale;
            ImGui::SameLine();
            if (UI::ToolbarButton(m_GizmoPivotCenter ? ICON_FA_OBJECT_GROUP : ICON_FA_CIRCLE_DOT,
                                  m_GizmoPivotCenter ? "Gizmo: tanlov markazida (bosing: asosiy obyektda)"
                                                     : "Gizmo: asosiy obyektda (bosing: tanlov markazida)",
                                  m_GizmoPivotCenter, size))
                m_GizmoPivotCenter = !m_GizmoPivotCenter;
            leftEnd = pos.x + w;
        }
        {
            // The step of the current gizmo operation, e.g. "0.5", "15°", "x0.1".
            std::string step = m_GizmoMode == GizmoMode::Rotate ? std::format("{:g}\xc2\xb0", snap.Rotate)
                             : m_GizmoMode == GizmoMode::Scale  ? std::format("x{:g}", snap.Scale)
                                                                : std::format("{:g}", snap.Translate);
            std::string label = step + "  " ICON_FA_CARET_DOWN;
            float stepWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImVec2 pos(leftEnd + 14.0f, 10);
            float w = buttonSize + spacing + stepWidth;
            if (pos.x + w < centerMin)
            {
                beginPanel(pos, w);
                if (UI::ToolbarButton(ICON_FA_MAGNET, snap.Enabled ? "Snapping yoqilgan (Ctrl bosib turish: vaqtincha o'chirish)"
                                                                   : "Snapping o'chiq (Ctrl bosib turish: vaqtincha yoqish)",
                                      snap.Enabled, size))
                {
                    snap.Enabled = !snap.Enabled;
                    MarkViewSettingsDirty();
                }
                ImGui::SameLine();
                if (UI::ToolbarButton((label + "###SnapStep").c_str(), "Snapping qadamlari (ko'chirish / aylantirish / masshtab)", false,
                                      ImVec2(stepWidth, buttonSize)))
                    ImGui::OpenPopup("##SnapSettings");
            }
            UI_SnapSettingsPopup();
        }

        // ---- center: play controls
        {
            float w = centerW;
            ImVec2 pos((width - w) * 0.5f, 10);
            beginPanel(pos, w);

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

        // ---- right: camera (frame all, bookmarks) | view options
        float viewW = buttonSize * 6 + caretWidth + spacing * 6;
        ImVec2 viewPos(width - viewW - 10, 10);
        {
            bool cameraTools = m_SceneState != SceneState::Play;
            float w = buttonSize * 2 + spacing;
            ImVec2 pos(viewPos.x - 14.0f - w, 10);
            if (pos.x > centerMax)
            {
                beginPanel(pos, w);
                ImGui::BeginDisabled(!cameraTools);
                if (UI::ToolbarButton(ICON_FA_EXPAND, "Hammasini ko'rsatish (Shift+F)", false, size)) FrameAll();
                ImGui::SameLine();
                int saved = GetSceneBookmarks().Count();
                std::string tip = std::format("Kamera xatcho'plari ({} / {})\nCtrl+1..5: saqlash   1..5: qaytish", saved, CameraBookmarks::SlotCount);
                if (UI::ToolbarButton(ICON_FA_BOOKMARK, tip.c_str(), saved > 0, size))
                    ImGui::OpenPopup("##CameraBookmarks");
                ImGui::EndDisabled();
            }
            UI_CameraBookmarksPopup();
        }
        {
            beginPanel(viewPos, viewW);
            if (UI::ToolbarButton(m_2DMode ? "2D" : "3D", m_2DMode ? "2D rejim (bosing: 3D)" : "3D rejim (bosing: 2D)", m_2DMode, size))
                Set2DMode(!m_2DMode);
            ImGui::SameLine();
            if (UI::ToolbarButton(ICON_FA_BORDER_ALL, "Grid (o'ng tugma: sozlamalar)", grid.Visible, size))
            {
                grid.Visible = !grid.Visible;
                MarkViewSettingsDirty();
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##GridSettings");
            ImGui::SameLine(0.0f, 0.0f);
            if (UI::ToolbarButton(ICON_FA_CARET_DOWN "###GridMenu", "Grid sozlamalari: o'lcham, rang, ko'rinish", false, ImVec2(caretWidth, buttonSize)))
                ImGui::OpenPopup("##GridSettings");
            UI_GridSettingsPopup();
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

    // Small buttons that set `value` to one of `presets` (the current one highlighted).
    static bool PresetButtons(const char* id, float& value, std::initializer_list<float> presets, const char* suffix = "")
    {
        bool changed = false;
        ImGui::PushID(id);
        int i = 0;
        for (float preset : presets)
        {
            if (i++) ImGui::SameLine(0.0f, 3.0f);
            bool current = std::abs(value - preset) <= 1e-4f * std::max(1.0f, preset);
            if (current) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.52f, 0.41f, 1.0f));
            std::string label = std::format("{:g}{}", preset, suffix);
            if (ImGui::SmallButton(label.c_str()))
            {
                value = preset;
                changed = true;
            }
            if (current) ImGui::PopStyleColor();
        }
        ImGui::PopID();
        return changed;
    }

    void EditorLayer::UI_SnapSettingsPopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(330, 0), ImVec2(420, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##SnapSettings"))
        {
            auto& snap = m_ViewSettings.Snap;
            bool changed = false;
            ImGui::TextColored(UI::AccentColor(), ICON_FA_MAGNET "  Snapping");
            ImGui::SameLine();
            ImGui::TextDisabled("(Ctrl bosib turish teskari qiladi)");
            changed |= ImGui::Checkbox("Doim yoqilgan", &snap.Enabled);
            if (UI::BeginPropertyGrid("snapgrid"))
            {
                changed |= UI::PropertyFloat("Ko'chirish", snap.Translate, 0.01f, SnapSettings::MinStep, 1000.0f, "%.3g");
                UI::PropertyLabel("");
                changed |= PresetButtons("t", snap.Translate, { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f });
                changed |= UI::PropertyFloat("Aylantirish (\xc2\xb0)", snap.Rotate, 0.5f, SnapSettings::MinStep, 360.0f, "%.3g");
                UI::PropertyLabel("");
                changed |= PresetButtons("r", snap.Rotate, { 5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f }, "\xc2\xb0");
                changed |= UI::PropertyFloat("Masshtab", snap.Scale, 0.01f, SnapSettings::MinStep, 100.0f, "%.3g");
                UI::PropertyLabel("");
                changed |= PresetButtons("s", snap.Scale, { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f });
                UI::EndPropertyGrid();
            }
            std::string fromGrid = std::format(ICON_FA_BORDER_ALL "  Ko'chirish qadami = grid katagi ({:g})", m_ViewSettings.Grid.CellSize);
            if (ImGui::Button(fromGrid.c_str()))
            {
                snap.Translate = m_ViewSettings.Grid.CellSize;
                changed = true;
            }
            if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Standart qadamlar"))
            {
                SnapSettings defaults;
                defaults.Enabled = snap.Enabled;
                snap = defaults;
                changed = true;
            }
            if (changed)
            {
                snap.Sanitize();
                MarkViewSettingsDirty();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::UI_GridSettingsPopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(340, 0), ImVec2(440, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##GridSettings"))
        {
            auto& grid = m_ViewSettings.Grid;
            bool changed = false;
            ImGui::TextColored(UI::AccentColor(), ICON_FA_BORDER_ALL "  Grid");
            ImGui::SameLine();
            ImGui::TextDisabled(m_2DMode ? "(XY tekislik, 2D)" : "(XZ tekislik, 3D)");
            if (UI::BeginPropertyGrid("gridprops"))
            {
                changed |= UI::PropertyBool("Ko'rinadi", grid.Visible);
                changed |= UI::PropertyFloat("Katak o'lchami", grid.CellSize, 0.01f, GridSettings::MinCellSize, 1000.0f, "%.3g");
                UI::PropertyLabel("");
                changed |= PresetButtons("cell", grid.CellSize, { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f });
                UI::PropertyLabel("Katta chiziq", "Har nechanchi chiziq qalinroq ko'rinadi (1 = katta chiziqlar yo'q)");
                changed |= ImGui::SliderInt("##major", &grid.MajorEvery, 1, 20, grid.MajorEvery > 1 ? "har %d" : "yo'q");
                changed |= UI::PropertyColor4("Chiziq rangi", grid.Color);
                changed |= UI::PropertyColor4("Katta chiziq rangi", grid.MajorColor);
                changed |= UI::PropertyBool("O'qlar (X / Y / Z)", grid.ShowAxes);
                UI::PropertyLabel("Masofa", "Grid qancha uzoqqacha ko'rinadi (standart qiymatga ko'paytma)");
                changed |= ImGui::SliderFloat("##fade", &grid.FadeDistance, 0.25f, 4.0f, "x%.2f");
                UI::EndPropertyGrid();
            }
            if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Standart qiymatlar"))
            {
                GridSettings defaults;
                defaults.Visible = grid.Visible;
                grid = defaults;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_MAGNET "  Snapping qadamiga tenglash"))
            {
                m_ViewSettings.Snap.Translate = grid.CellSize;
                changed = true;
            }
            if (changed)
            {
                grid.Sanitize();
                MarkViewSettingsDirty();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::UI_CameraBookmarksPopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(360, 0), ImVec2(520, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##CameraBookmarks"))
        {
            CameraBookmarks& bookmarks = GetSceneBookmarks();
            ImGui::TextColored(UI::AccentColor(), ICON_FA_BOOKMARK "  Kamera xatcho'plari");
            ImGui::SameLine();
            if (m_EditorScenePath.empty())
                ImGui::TextDisabled("(saqlanmagan sahna)");
            else
                ImGui::TextDisabled("(%s)", m_EditorScenePath.filename().string().c_str());
            ImGui::TextDisabled("Ctrl+1..5: saqlash    1..5: qaytish");
            ImGui::Separator();
            for (int slot = 1; slot <= CameraBookmarks::SlotCount; slot++)
            {
                ImGui::PushID(slot);
                const EditorCameraState* state = bookmarks.Get(slot);
                ImGui::BeginDisabled(!state);
                if (ImGui::Button(std::format(ICON_FA_CAMERA "  {}", slot).c_str(), ImVec2(60, 0)))
                {
                    RecallCameraBookmark(slot);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Saqlash"))
                    SaveCameraBookmark(slot);
                ImGui::SameLine();
                ImGui::BeginDisabled(!state);
                if (ImGui::Button(ICON_FA_XMARK))
                {
                    bookmarks.Clear(slot);
                    MarkViewSettingsDirty();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                if (state)
                    ImGui::Text("%s  (%.1f, %.1f, %.1f)  %.1f m", state->Orthographic ? "2D" : "3D",
                                state->FocalPoint.x, state->FocalPoint.y, state->FocalPoint.z, state->Distance);
                else
                    ImGui::TextDisabled("bo'sh");
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::BeginDisabled(bookmarks.IsEmpty());
            if (ImGui::Button(ICON_FA_TRASH "  Hammasini tozalash"))
            {
                bookmarks.ClearAll();
                MarkViewSettingsDirty();
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::UI_ViewMenu()
    {
        if (!ImGui::BeginMenu("Ko'rinish"))
            return;
        bool camera = m_SceneState != SceneState::Play;
        if (ImGui::MenuItem(ICON_FA_BORDER_ALL "  Grid", nullptr, &m_ViewSettings.Grid.Visible)) MarkViewSettingsDirty();
        if (ImGui::BeginMenu(ICON_FA_SLIDERS "  Grid sozlamalari"))
        {
            auto& grid = m_ViewSettings.Grid;
            bool changed = false;
            ImGui::SetNextItemWidth(140);
            changed |= ImGui::DragFloat("Katak o'lchami", &grid.CellSize, 0.01f, GridSettings::MinCellSize, 1000.0f, "%.3g");
            ImGui::SetNextItemWidth(140);
            changed |= ImGui::SliderInt("Katta chiziq", &grid.MajorEvery, 1, 20, grid.MajorEvery > 1 ? "har %d" : "yo'q");
            changed |= ImGui::ColorEdit4("Chiziq rangi", &grid.Color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Katta chiziq rangi", &grid.MajorColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::Checkbox("O'qlar", &grid.ShowAxes);
            if (changed)
            {
                grid.Sanitize();
                MarkViewSettingsDirty();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_MAGNET "  Snapping", "Ctrl = teskari", &m_ViewSettings.Snap.Enabled)) MarkViewSettingsDirty();
        if (ImGui::BeginMenu(ICON_FA_SLIDERS "  Snapping qadamlari"))
        {
            auto& snap = m_ViewSettings.Snap;
            bool changed = false;
            ImGui::SetNextItemWidth(140);
            changed |= ImGui::DragFloat("Ko'chirish", &snap.Translate, 0.01f, SnapSettings::MinStep, 1000.0f, "%.3g");
            ImGui::SetNextItemWidth(140);
            changed |= ImGui::DragFloat("Aylantirish (\xc2\xb0)", &snap.Rotate, 0.5f, SnapSettings::MinStep, 360.0f, "%.3g");
            ImGui::SetNextItemWidth(140);
            changed |= ImGui::DragFloat("Masshtab", &snap.Scale, 0.01f, SnapSettings::MinStep, 100.0f, "%.3g");
            if (changed)
            {
                snap.Sanitize();
                MarkViewSettingsDirty();
            }
            ImGui::EndMenu();
        }
        ImGui::MenuItem(ICON_FA_VECTOR_SQUARE "  Fizik colliderlar", nullptr, &m_ShowColliders);
        ImGui::MenuItem(ICON_FA_EYE "  Ikonkalar", nullptr, &m_ShowIcons);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_BULLSEYE "  Tanlanganga fokus", "F", false, camera && m_SceneHierarchyPanel.GetSelectedEntity())) FrameSelection();
        if (ImGui::MenuItem(ICON_FA_EXPAND "  Hammasini ko'rsatish", "Shift+F", false, camera)) FrameAll();
        if (ImGui::BeginMenu(ICON_FA_BOOKMARK "  Kamera xatcho'plari", camera))
        {
            CameraBookmarks& bookmarks = GetSceneBookmarks();
            for (int slot = 1; slot <= CameraBookmarks::SlotCount; slot++)
            {
                std::string label = std::format(ICON_FA_CAMERA "  Xatcho'p {}{}", slot, bookmarks.Has(slot) ? "" : "  (bo'sh)");
                if (ImGui::MenuItem(label.c_str(), std::to_string(slot).c_str(), false, bookmarks.Has(slot)))
                    RecallCameraBookmark(slot);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu(ICON_FA_FLOPPY_DISK "  Joriy ko'rinishni saqlash"))
            {
                for (int slot = 1; slot <= CameraBookmarks::SlotCount; slot++)
                    if (ImGui::MenuItem(std::format("Xatcho'p {}", slot).c_str(), std::format("Ctrl+{}", slot).c_str()))
                        SaveCameraBookmark(slot);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(ICON_FA_TRASH "  Hammasini tozalash", nullptr, false, !bookmarks.IsEmpty()))
            {
                bookmarks.ClearAll();
                MarkViewSettingsDirty();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    void EditorLayer::UI_ViewportMessage()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        glm::vec2 min = m_ViewportBounds[0], max = m_ViewportBounds[1];
        auto banner = [&](const char* text, float alpha, float y) {
            ImVec2 ts = ImGui::CalcTextSize(text);
            ImVec2 a((min.x + max.x - ts.x) * 0.5f - 12.0f, y);
            ImVec2 b(a.x + ts.x + 24.0f, a.y + ts.y + 12.0f);
            dl->AddRectFilled(a, b, IM_COL32(18, 20, 24, (int)(215 * alpha)), 6.0f);
            dl->AddText(ImVec2(a.x + 12.0f, a.y + 6.0f), IM_COL32(235, 238, 242, (int)(255 * alpha)), text);
        };

        // Hint while dragging with the gizmo (duplicating in the middle of a drag).
        if (m_GizmoEdit.IsActive() && m_SceneState == SceneState::Edit)
            banner(ICON_FA_CLONE "  Ctrl+D: shu joyda nusxa qoldirish", 0.85f, max.y - 78.0f);

        double age = ImGui::GetTime() - m_ViewportMessageTime;
        const double duration = 2.5, fadeOut = 0.5;
        if (m_ViewportMessage.empty() || age < 0.0 || age > duration)
            return;
        float alpha = age > duration - fadeOut ? (float)((duration - age) / fadeOut) : 1.0f;
        banner(m_ViewportMessage.c_str(), alpha, max.y - 44.0f);
    }

    void EditorLayer::UI_ViewportIcons()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        glm::mat4 viewProj = m_EditorCamera.GetViewProjection();
        glm::vec2 size = m_ViewportBounds[1] - m_ViewportBounds[0];
        ImVec2 mouse = ImGui::GetMousePos();

        // Hidden and inactive entities have no icon; locked ones are drawn faint and cannot be clicked.
        SceneRenderOptions skipped;
        skipped.SkipEditorHidden = true;
        std::unordered_set<entt::entity> hidden = m_ActiveScene->CollectRenderSkipped(skipped);
        auto drawIcon = [&](Entity e, const char* icon, ImU32 color) {
            if (hidden.contains(e)) return;
            glm::vec4 clip = viewProj * glm::vec4(m_ActiveScene->GetWorldPosition(e), 1.0f);
            if (clip.w <= 0.0f) return;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.1f || std::abs(ndc.y) > 1.1f) return;
            ImVec2 p(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * size.x, m_ViewportBounds[0].y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y);
            float r = 14.0f;
            const bool locked = m_ActiveScene->IsLockedInEditor(e);
            bool hovered = !locked && (mouse.x - p.x) * (mouse.x - p.x) + (mouse.y - p.y) * (mouse.y - p.y) < r * r;
            bool isSelected = m_SceneHierarchyPanel.IsSelected(e);
            if (locked)
                color = (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 110);
            dl->AddCircleFilled(p, r, isSelected ? IM_COL32(30, 140, 110, 230) : (hovered ? IM_COL32(50, 55, 65, 230) : IM_COL32(20, 22, 26, 200)));
            dl->AddCircle(p, r, color, 0, 1.5f);
            ImVec2 ts = ImGui::CalcTextSize(icon);
            dl->AddText(ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), color, icon);
            if (hovered && m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver())
                m_SceneHierarchyPanel.SelectWithModifiers(e, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
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
        Entity primary = m_SceneHierarchyPanel.GetSelectedEntity();
        if (!primary || !primary.IsValid() || m_GizmoMode == GizmoMode::None || m_SceneHierarchyPanel.IsTilePainting())
        {
            m_GizmoEdit.End();
            return;
        }
        // Every selected subtree moves together (an entity inside another selected one follows
        // it); locked ones stay where they are.
        std::vector<Entity> targets = GetGizmoTargets();
        if (targets.empty())
        {
            m_GizmoEdit.End();
            return;
        }
        if (m_ActiveScene->IsLockedInEditor(primary))
            primary = targets.front(); // the pivot follows an entity that moves

        ImGuizmo::SetOrthographic(m_EditorCamera.IsOrthographic());
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x, m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        const glm::mat4& projection = m_EditorCamera.GetProjection();
        glm::mat4 view = m_EditorCamera.GetViewMatrix();

        // Snapping: the toolbar toggle (magnet), inverted while Ctrl is held; steps per operation.
        bool ctrl = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
        bool snap = m_ViewSettings.Snap.IsActive(ctrl);
        float snapValue = m_ViewSettings.Snap.GetStep((int)m_GizmoMode);
        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::OPERATION op = m_GizmoMode == GizmoMode::Translate ? ImGuizmo::TRANSLATE :
                                 m_GizmoMode == GizmoMode::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        ImGuizmo::MODE mode = (m_GizmoLocal || m_GizmoMode == GizmoMode::Scale) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        // Pivot: the primary entity, or the center of the selection. While dragging, the
        // gizmo's own previous result is fed back (ImGuizmo works incrementally).
        if (m_GizmoEdit.IsActive() && m_GizmoEditMode != m_GizmoMode)
            m_GizmoEdit.End(); // W/E/R pressed mid-drag: start over with the new operation
        bool dragging = m_GizmoEdit.IsActive() && m_GizmoEdit.GetScene() == m_ActiveScene.get();
        bool center = m_GizmoPivotCenter && targets.size() > 1;
        glm::mat4 pivot = dragging ? m_GizmoMatrix
                                   : GroupTransformEdit::ComputePivot(*m_ActiveScene, targets, primary, center, mode == ImGuizmo::LOCAL);
        glm::mat4 transform = pivot;
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), op, mode, glm::value_ptr(transform),
                             nullptr, snap ? snapValues : nullptr);

        if (ImGuizmo::IsUsing())
        {
            if (!dragging)
            {
                m_GizmoEdit.Begin(*m_ActiveScene, targets, pivot);
                m_GizmoEditMode = m_GizmoMode;
            }
            m_GizmoMatrix = transform;
            if (m_GizmoAltDuplicatePending && m_SceneState == SceneState::Edit)
            {
                // Alt + drag: the copies move, the originals stay where they were. ImGuizmo is
                // "using" from the click on, so the copies are made only once the gizmo really
                // moves past the drag threshold: a plain Alt+click leaves nothing behind. Until
                // then nothing was applied, so the copies sit exactly on the originals and the
                // drag restarts from its first pivot with them.
                if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) || !m_GizmoEdit.HasMoved(transform))
                    return;
                m_GizmoAltDuplicatePending = false;
                if (size_t count = m_SceneHierarchyPanel.DuplicateSelection(true))
                {
                    m_GizmoEdit.Begin(*m_ActiveScene, GetGizmoTargets(), m_GizmoEdit.GetStartPivot());
                    ShowViewportMessage(count > 1 ? std::format(ICON_FA_CLONE "  {} ta nusxa sudralmoqda", count)
                                                  : std::string(ICON_FA_CLONE "  Nusxa sudralmoqda"));
                }
            }
            m_GizmoEdit.Update(transform, (GroupTransformEdit::Operation)(int)m_GizmoMode);
            MarkDirty();
        }
        else if (m_GizmoEdit.IsActive())
            m_GizmoEdit.End();
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
            const float width = 460.0f;
            ImGui::TextUnformatted("Klass nomi (masalan: PlayerController)");
            ImGui::SetNextItemWidth(width);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##scriptname", &m_NewScriptName, ImGuiInputTextFlags_EnterReturnsTrue);

            // Live check: the same rules CreateScriptClass applies.
            std::string problem = ScriptTemplate::ValidateClassName(m_NewScriptName);
            if (problem.empty() && ScriptRegistry::Find(m_NewScriptName))
                problem = std::format("'{}' klassi allaqachon mavjud", m_NewScriptName);
            fs::path file = Project::GetActive() ? Project::GetSourceDirectory() / (m_NewScriptName + ".cpp") : fs::path();
            if (problem.empty() && !file.empty() && fs::exists(file))
                problem = std::format("{} fayli allaqachon bor", file.filename().string());
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            if (!problem.empty())
            {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  %s", problem.c_str());
                if (std::string fixed = ScriptTemplate::SanitizeClassName(m_NewScriptName);
                    fixed != m_NewScriptName && ScriptTemplate::IsValidClassName(fixed))
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(std::format("{} ni ishlatish", fixed).c_str()))
                        m_NewScriptName = fixed;
                }
            }
            else
                ImGui::TextDisabled(ICON_FA_FILE_CODE "  Source/%s.cpp", m_NewScriptName.c_str());

            ImGui::Spacing();
            ImGui::SeparatorText("Shablon");
            for (ScriptTemplateKind kind : { ScriptTemplateKind::Full, ScriptTemplateKind::Minimal })
            {
                if (ImGui::RadioButton(ScriptTemplate::GetKindName(kind), m_NewScriptTemplate == kind))
                    m_NewScriptTemplate = kind;
                ImGui::Indent(ImGui::GetFrameHeight());
                ImGui::TextDisabled("%s", ScriptTemplate::GetKindDescription(kind));
                ImGui::Unindent(ImGui::GetFrameHeight());
            }
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            ImGui::BeginDisabled(!problem.empty());
            if (ImGui::Button(ICON_FA_CHECK "  Yaratish", ImVec2(150, 0)) || (enter && problem.empty()))
            {
                if (CreateScriptClass(m_NewScriptName, m_NewScriptTemplate))
                    ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
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
