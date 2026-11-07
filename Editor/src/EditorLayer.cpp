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

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <map>
#include <string_view>

namespace Zamin {

    namespace fs = std::filesystem;

    static fs::path ConfigDirectory()
    {
        fs::path dir = EditorPreferences::GetConfigDirectory();
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir;
    }

    static std::string Quote(const fs::path& p) { return "\"" + p.string() + "\""; }

    namespace {
        // The scene view's debug views, in RenderDebugView order.
        struct DebugViewInfo
        {
            const char* Icon;
            const char* Name;
            const char* Tooltip;
        };
        constexpr DebugViewInfo s_DebugViews[RenderDebugViewCount] = {
            { ICON_FA_SUN, "Oddiy (yoritilgan)", "Haqiqiy tasvir: materiallar, yorug'lik, soyalar va post-processing" },
            { ICON_FA_LIGHTBULB, "Faqat yorug'lik", "Hamma sirtlar oq va metall emas: faqat quyosh, nuqtali/spot yorug'liklar, "
                                                    "soyalar va atrof yorug'ligi ko'rinadi (tonemapping bilan)" },
            { ICON_FA_COMPASS, "Normallar", "Dunyo koordinatalaridagi normallar rang sifatida (normal xaritalar bilan): "
                                            "X - qizil, Y - yashil, Z - ko'k" },
            { ICON_FA_PALETTE, "Albedo", "Asosiy rang (tekstura bilan), yorug'liksiz va tonemappingsiz" },
            { ICON_FA_CIRCLE_HALF_STROKE, "Roughness", "G'adir-budurlik kulrangda: qora - silliq (oyna), oq - xira" },
            { ICON_FA_GEM, "Metallic", "Metalliklik kulrangda: qora - metall emas, oq - metall" },
        };
    }

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
        // The editor gives every asset a .zmeta file (UUID, import settings); the player only reads them.
        AssetRegistry::SetWriteMetaFilesByDefault(true);
        // The game's cursor wishes reach the editor's window only while the game view has the focus.
        Cursor::BindWindow(&Application::Get().GetWindow());
        Cursor::SetHostActive(false);

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
        // Saved window layouts (Oyna > Joylashuv).
        m_Layouts.SetDirectory(ConfigDirectory() / "layouts");
        m_Layouts.Refresh();

        m_EditorCamera.Focus({ 0.0f, 0.5f, 0.0f }, 14.0f);
        RegisterShortcuts(); // before the preferences: they rebind some of them
        LoadViewSettings();
        LoadPreferences();

        m_SceneHierarchyPanel.OnFocusRequested = [this](Entity e) { FocusEntity(e); };
        // Play mode with several scenes: the hierarchy shows the one picked there.
        m_SceneHierarchyPanel.OnRuntimeSceneSelected = [this](const Ref<Scene>& scene) {
            if (m_SceneState == SceneState::Play && m_RuntimeScenes && m_RuntimeScenes->IsLoaded(scene.get()) && scene != m_ActiveScene)
            {
                m_SceneHierarchyPanel.SetSelectedEntity({});
                SetActiveScene(scene, false);
            }
        };
        // The Scenes window: open a project scene, its build list / start scene, the game's loaded scenes.
        m_ScenesPanel.SetThumbnails(&m_SceneThumbnails);
        m_ScenesPanel.OnOpenScene = [this](const fs::path& p) { OpenScene(p); };
        m_ScenesPanel.OnRevealScene = [this](const fs::path& p) { RevealAsset(p); };
        m_ScenesPanel.OnProjectModified = [this]() {
            m_ProjectSettingsPanel.MarkDirty();
            m_ProjectSettingsPanel.SaveIfDirty();
        };
        m_ScenesPanel.OnRenderThumbnails = [this](const std::vector<fs::path>& scenes) {
            for (const fs::path& scene : scenes)
                if (std::find(m_ThumbnailQueue.begin(), m_ThumbnailQueue.end(), scene) == m_ThumbnailQueue.end())
                    m_ThumbnailQueue.push_back(scene);
            ZM_INFO("{} ta sahna rasmi chizilmoqda", m_ThumbnailQueue.size());
        };
        m_ScenesPanel.OnShowRuntimeScene = [this](const Ref<Scene>& scene) {
            if (m_SceneHierarchyPanel.OnRuntimeSceneSelected)
                m_SceneHierarchyPanel.OnRuntimeSceneSelected(scene);
        };
        m_SceneHierarchyPanel.OnSceneModified = [this]() { MarkDirty(); };
        m_SceneHierarchyPanel.OnSavePrefabRequested = [this](Entity e) { SavePrefab(e); };
        m_SceneHierarchyPanel.OnProjectSettingsModified = [this]() { m_ProjectSettingsPanel.MarkDirty(); };
        m_SceneHierarchyPanel.SetFoldState(&m_ViewSettings.Inspector); // open / collapsed inspector headers, per user
        m_SceneHierarchyPanel.OnFoldStateModified = [this]() { MarkViewSettingsDirty(); };
        m_ProjectSettingsPanel.OnSceneModified = [this]() { MarkDirty(); };
        m_ProjectSettingsPanel.OnOpenSceneRequested = [this](const fs::path& scene) { OpenScene(Project::GetAssetFileSystemPath(scene)); };
        m_AudioMixerPanel.OnProjectModified = [this]() { m_ProjectSettingsPanel.MarkDirty(); };
        m_ProjectSettingsPanel.OnAudioMixerRequested = [this]() {
            m_ShowAudioMixer = true;
            m_AudioMixerPanel.Focus();
        };
        m_ProjectSettingsPanel.OnScenesPanelRequested = [this]() {
            m_ShowScenes = true;
            m_ScenesPanel.Focus();
        };
        m_ProjectSettingsPanel.OnLocalizationTableRequested = [this]() {
            m_ShowLocalization = true;
            m_LocalizationPanel.Focus();
        };
        m_ContentBrowserPanel.OnProjectSettingsModified = [this]() { m_ProjectSettingsPanel.MarkDirty(); };
        m_SceneHierarchyPanel.OnAudioMixerRequested = [this]() {
            m_ShowAudioMixer = true;
            m_AudioMixerPanel.Focus();
        };
        m_SceneHierarchyPanel.OnPrefabDropped = [this](const fs::path& p, Entity parent) { InstantiatePrefabInEditor(p, parent); };
        m_SceneHierarchyPanel.OnOpenScriptSource = [this](const std::string& className) { OpenScriptClassSource(className); };
        m_ContentBrowserPanel.OnOpenSourceFile = [this](const fs::path& file) { OpenInExternalEditor(file); };
        m_SceneHierarchyPanel.OnAnimationWindowRequested = [this](Entity e, int clip) {
            m_ShowAnimation = true;
            m_AnimationPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
            m_AnimationPanel.Open(e, clip);
        };
        m_SceneHierarchyPanel.OnAnimatorGraphRequested = [this](Entity e) {
            m_ShowAnimatorGraph = true;
            m_AnimatorGraphPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
            m_AnimatorGraphPanel.Open(e);
            m_AnimatorGraphPanel.Focus();
        };
        m_AnimatorGraphPanel.OnModified = [this]() { MarkDirty(); };
        m_ContentBrowserPanel.OnOpenAnimation = [this](const fs::path& p) {
            m_ShowAnimation = true;
            m_AnimationPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
            m_AnimationPanel.OpenClipFile(p);
        };
        m_AssetInspectorPanel.OnOpenAnimation = [this](const fs::path& p) {
            m_ShowAnimation = true;
            m_AnimationPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
            m_AnimationPanel.OpenClipFile(p);
        };
        m_AnimationPanel.OnSceneModified = [this]() { MarkDirty(); };
        m_AnimationPanel.OnClipSaved = [this](const fs::path& p) {
            m_ContentBrowserPanel.InvalidateThumbnail(p);
            m_ContentBrowserPanel.MarkListDirty();
            m_AssetUsage.MarkDirty();
        };
        m_SceneHierarchyPanel.OnMaterialDropped = [this](const std::string& p, Entity target) { ApplyMaterialToEntity(target, p); };
        m_ContentBrowserPanel.OnOpenScene = [this](const fs::path& p) { OpenScene(p); };
        m_AssetInspectorPanel.SetSceneThumbnails(&m_SceneThumbnails);
        m_AssetInspectorPanel.IsOpenScene = [this](const fs::path& p) {
            std::error_code ec;
            return !m_EditorScenePath.empty() && fs::equivalent(p, m_EditorScenePath, ec);
        };
        m_AssetInspectorPanel.OnEditSceneMetadata = [this]() {
            m_ShowSceneSettings = true;
            m_FocusSceneSettings = true;
            m_SceneSettingsScrollTo = "metadata";
        };
        m_ContentBrowserPanel.OnRequestNewScript = [this]() { m_NewScriptName = "MyScript"; m_OpenNewScriptPopup = true; };
        m_ContentBrowserPanel.OnNewScene = [this](const fs::path& p) {
            Ref<Scene> scene = CreateRef<Scene>();
            scene->SetName(p.stem().string());
            SerializeScene(scene, p);
        };
        m_ContentBrowserPanel.SetSettings(&m_ViewSettings.ContentBrowser); // grid / list, filter, sorting: per user
        m_ContentBrowserPanel.OnSettingsModified = [this]() { MarkViewSettingsDirty(); };
        m_ContentBrowserPanel.OnAssetsMoved = [this](const AssetFiles::MoveResult& result) { OnAssetsMoved(result); };
        // A file picked in the content browser takes the Inspector until an entity is picked again.
        m_SceneHierarchyPanel.SetAssetInspector(&m_AssetInspectorPanel);
        m_ContentBrowserPanel.OnAssetSelected = [this](const fs::path& p) {
            m_AssetInspectorPanel.SetAsset(p);
            m_AssetInspectorPanel.SetSelection(m_ContentBrowserPanel.GetSelection());
            m_SceneHierarchyPanel.AcknowledgeSelection();
        };
        m_ContentBrowserPanel.OnFilesChanged = [this]() { m_AssetUsage.MarkDirty(); };
        m_ContentBrowserPanel.SetUsageCache(&m_AssetUsage); // deleting warns about assets in use
        m_ContentBrowserPanel.OnFindUnusedAssets = [this]() { m_ShowUnusedAssets = true; };

        // Where assets are used: saved files, and the open scene as it is in the editor (also while
        // the game runs on a copy of it). Anything that changes that state marks the cache dirty.
        m_AssetUsage.GetOverrides = [this]() {
            std::unordered_map<std::string, std::string> overrides;
            if (m_SceneDirty && m_EditorScene && !m_EditorScenePath.empty())
                overrides[m_EditorScenePath.string()] = SceneSerializer(m_EditorScene).SerializeToString();
            return overrides;
        };
        // A new scene that was never saved has no file to read: it counts under a name of its own.
        m_AssetUsage.GetUnsaved = [this]() {
            std::vector<AssetReferenceIndex::UnsavedFile> unsaved;
            if (m_EditorScene && m_EditorScenePath.empty() && Project::GetActive())
            {
                AssetReferenceIndex::UnsavedFile scene;
                scene.File = Project::GetCacheDirectory() / "Unsaved" / "open-scene.zscene"; // made up, never written
                scene.Name = std::format("{} (ochiq sahna, saqlanmagan)", m_EditorScene->GetName());
                scene.Text = SceneSerializer(m_EditorScene).SerializeToString();
                unsaved.push_back(std::move(scene));
            }
            return unsaved;
        };
        m_AssetInspectorPanel.SetUsageCache(&m_AssetUsage);
        m_AssetInspectorPanel.OnRevealRequested = [this](const fs::path& p) { RevealAsset(p); };
        m_SceneHierarchyPanel.OnAssetEditRequested = [this](const fs::path& p) { RevealAsset(p); };
        // Sub-scenes: their content under the host (the preview in edit mode, the loaded scene in play).
        m_SceneHierarchyPanel.GetSubScene = [this](Entity host) -> Ref<Scene> {
            if (m_SceneState == SceneState::Play)
                return m_RuntimeScenes ? m_RuntimeScenes->GetSubScene(host) : nullptr;
            return m_SceneState == SceneState::Edit ? m_SubScenePreviews.Get(host) : nullptr;
        };
        m_SceneHierarchyPanel.GetSubSceneProblem = [this](Entity host) {
            return m_SceneState == SceneState::Edit ? m_SubScenePreviews.GetProblem(host) : std::string();
        };
        m_SceneHierarchyPanel.OnOpenSceneRequested = [this](const fs::path& p) {
            const fs::path file = Project::GetAssetFileSystemPath(p);
            if (m_SceneState != SceneState::Edit)
                OnSceneStop();
            // The open scene's changes are kept: it is saved before the other one opens.
            if (m_SceneDirty && !m_EditorScenePath.empty())
            {
                SaveScene();
                ZM_INFO("Sahna saqlandi, sub-sahna ochiladi: {}", file.filename().string());
            }
            OpenScene(file);
        };
        m_SceneHierarchyPanel.OnSpriteEditorRequested = [this](const std::string& texture) { OpenSpriteEditor(texture); };
        m_SceneHierarchyPanel.OnTilePaletteWindowRequested = [this]() {
            m_ShowTilePalette = true;
            ImGui::SetWindowFocus("###TilePalette");
        };
        m_SceneHierarchyPanel.OnSortingLayersRequested = [this]() {
            m_ShowProjectSettings = true;
            m_ProjectSettingsPanel.Focus(ProjectSettingsPanel::Tab::SortingLayers);
        };
        m_SceneHierarchyPanel.OnPhysicsLayersRequested = [this]() {
            m_ShowProjectSettings = true;
            m_ProjectSettingsPanel.Focus(ProjectSettingsPanel::Tab::PhysicsLayers);
        };
        m_AssetInspectorPanel.OnOpenSpriteEditor = [this](const fs::path& p) { OpenSpriteEditor(Project::GetAssetRelativePath(p)); };
        m_SceneHierarchyPanel.GetAssetThumbnail = [this](const fs::path& p) -> uint32_t {
            Ref<Texture2D> thumbnail = m_ContentBrowserPanel.GetThumbnails().GetForFile(p);
            return thumbnail ? thumbnail->GetRendererID() : 0;
        };
        // The material preview's "scene" lighting: the open scene's HDRI.
        m_AssetInspectorPanel.GetSceneLighting = [this](MaterialPreview::Settings& settings) {
            const SceneSettings* scene = m_ActiveScene ? &m_ActiveScene->GetSettings() : nullptr;
            if (!scene || scene->EnvironmentMap.empty())
                return false;
            settings.Environment = EnvironmentMap::Load(Project::GetAssetFileSystemPath(scene->EnvironmentMap));
            settings.EnvironmentIntensity = scene->EnvironmentIntensity;
            settings.EnvironmentRotation = scene->EnvironmentRotation;
            return settings.Environment != nullptr;
        };
        m_AssetInspectorPanel.OnOpenScene = [this](const fs::path& p) { OpenScene(p); };
        // Files written from the inspector (a model's materials exported): listed at once.
        auto filesCreated = [this](const std::vector<fs::path>& files) {
            for (const fs::path& file : files)
                m_ContentBrowserPanel.InvalidateThumbnail(file);
            m_ContentBrowserPanel.MarkListDirty();
            m_AssetUsage.MarkDirty();
        };
        m_AssetInspectorPanel.OnFilesCreated = filesCreated;
        m_SceneHierarchyPanel.OnFilesCreated = filesCreated;
        m_UnusedAssetsPanel.SetUsageCache(&m_AssetUsage);
        m_UnusedAssetsPanel.OnAssetSelected = [this](const fs::path& p) { RevealAsset(p); };
        m_UnusedAssetsPanel.OnAssetsDeleted = [this](const std::vector<fs::path>& deleted) {
            for (const fs::path& p : deleted)
            {
                m_ContentBrowserPanel.InvalidateThumbnail(p);
                if (m_AssetInspectorPanel.GetPath() == p)
                    m_AssetInspectorPanel.Clear();
            }
            m_ContentBrowserPanel.MarkListDirty();
            m_ContentBrowserPanel.ShowNotice(std::format("{} ta ishlatilmagan asset o'chirildi", deleted.size()));
        };

        ScriptEngine::SetBeforeUnloadCallback([this]() {
            if (m_SceneState != SceneState::Edit)
                OnSceneStop();
        });

        LoadRecentProjects();
        LoadRecentScenes();
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
        // ZAMIN_EDITOR_STATS: the last frame's numbers in the log as well (automation / CI).
        if (std::getenv("ZAMIN_EDITOR_STATS"))
        {
            ZM_INFO("Render statistikasi (oxirgi kadr): {}", Renderer::GetStats().Format());
            if (m_ActiveScene)
            {
                const Scene::AnimationStats& a = m_ActiveScene->GetAnimationStats();
                ZM_INFO("Animatsiya statistikasi: {} animator, {} pozalangan, {} o'zgarmagan, {} kesilgan, {}/{} kesh/klip namunasi, "
                        "kesh {:.1f} KB, {:.3f} ms", a.Animators, a.Posed, a.Unchanged, a.Culled, a.CachedSamples, a.ClipSamples,
                        (double)a.CacheBytes / 1024.0, a.Milliseconds);
            }
            ZM_INFO("Profiler (oxirgi kadr, ms):\n{}", Profiler::FormatTree(Profiler::GetLastFrame()));
        }
        Cursor::BindWindow(nullptr);
        m_AssetHotReload.Stop();
        SaveAssetRegistryCache(true);
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        m_ProjectSettingsPanel.SaveIfDirty();
        // The extra views' cameras are kept for the next session.
        if (std::any_of(m_ViewSettings.SceneViews.begin(), m_ViewSettings.SceneViews.end(), [](const SceneViewSettings& v) { return v.Open; }))
            m_ViewSettingsDirty = true;
        if (m_ViewSettingsDirty)
            SaveViewSettings();
        if (m_PreferencesDirty || m_ConsolePanel.ConsumePreferencesChanged())
            SavePreferences();
        // Closed normally: this session's safety copy goes, and the project is no longer locked.
        DiscardSessionAutosave();
        ReleaseProjectLock();
        m_ActiveScene.reset();
        m_EditorScene.reset();
        ScriptEngine::UnloadModule();
    }

    // ================================================================== update
    void EditorLayer::OnUpdate(Timestep ts)
    {
        // The UI's look (accent, font size, the play-mode tint) between two frames.
        UpdateEditorTheme();
        // A saved window layout chosen last frame (ImGui takes window settings between frames).
        ApplyPendingLayout();
        // Automation hooks (CI / screenshots): ZAMIN_EDITOR_SELECT=<entity name>, ZAMIN_EDITOR_PLAY=1
        static int frame = 0;
        if (++frame == 5)
        {
            const EditorViewSettings viewSettingsBefore = m_ViewSettings; // the hooks' changes are for this session
            if (const char* scene = std::getenv("ZAMIN_EDITOR_SCENE"))
                OpenScene(Project::GetAssetFileSystemPath(scene));
            // ZAMIN_EDITOR_LOAD_REPORT=1: the scene load problems window open (when the load found any).
            if (std::getenv("ZAMIN_EDITOR_LOAD_REPORT") && m_LoadReport.HasIssues())
                m_ShowLoadReport = true;
            // "Name" or "A,B,C" (multi-selection, the last is primary); ZAMIN_EDITOR_SELECT_FRAME=<n>: at
            // frame n instead (e.g. an entity a script spawns after ZAMIN_EDITOR_PLAY).
            if (const char* sel = std::getenv("ZAMIN_EDITOR_SELECT"); sel && !std::getenv("ZAMIN_EDITOR_SELECT_FRAME"))
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
            // ZAMIN_EDITOR_ANIMATION=1 | curves,time=0.5,track=1,key=1,record: the Animation window on the
            // selected entity's player (curve view, scrubbed to a time, a track / key selected, recording).
            // skeletal,clip=Walk,time=0.4,event=0,row=2: its Animator's glTF clip scrubbed there (an event /
            // a bone or blend shape row selected).
            if (const char* anim = std::getenv("ZAMIN_EDITOR_ANIMATION"))
            {
                std::string_view v(anim);
                m_ShowAnimation = true;
                m_AnimationPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
                Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
                const bool skeletal = v.find("skeletal") != std::string_view::npos;
                auto text = [&](std::string_view key) {
                    size_t at = v.find(key);
                    if (at == std::string_view::npos) return std::string();
                    std::string_view rest = v.substr(at + key.size());
                    return std::string(rest.substr(0, rest.find(',')));
                };
                for (Entity e = selected; e; e = m_ActiveScene->GetParent(e))
                    if (skeletal ? e.HasComponent<AnimatorComponent>() : e.HasComponent<AnimationPlayerComponent>())
                    {
                        if (skeletal)
                            m_AnimationPanel.OpenAnimator(e, text("clip="));
                        else
                            m_AnimationPanel.Open(e, 0);
                        break;
                    }
                auto number = [&](std::string_view key, float fallback) {
                    size_t at = v.find(key);
                    return at == std::string_view::npos ? fallback : (float)std::atof(std::string(v.substr(at + key.size())).c_str());
                };
                if (skeletal && v.find("event=") != std::string_view::npos)
                    m_AnimationPanel.SelectSkeletalEvent((int)number("event=", 0.0f));
                if (skeletal && v.find("row=") != std::string_view::npos)
                    m_AnimationPanel.SelectSkeletalRow((int)number("row=", 0.0f));
                m_AnimationPanel.SetCurveView(v.find("curves") != std::string_view::npos);
                m_AnimationPanel.SetTime(number("time=", 0.0f));
                if (v.find("track=") != std::string_view::npos)
                    m_AnimationPanel.SelectTrack((int)number("track=", 0.0f));
                if (v.find("key=") != std::string_view::npos)
                    m_AnimationPanel.SelectKey((int)number("track=", 0.0f), (int)number("key=", 0.0f));
                m_AnimationPanel.SetRecording(v.find("record") != std::string_view::npos);
            }
            // ZAMIN_EDITOR_ANIMATOR_GRAPH=1 | state=N,transition=N,viewport: the Animator graph window on
            // the selected entity (a state / transition selected; docked over the viewport).
            if (const char* graph = std::getenv("ZAMIN_EDITOR_ANIMATOR_GRAPH"))
            {
                std::string_view v(graph);
                m_ShowAnimatorGraph = true;
                m_AnimatorGraphPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
                if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity())
                    m_AnimatorGraphPanel.Open(selected);
                auto number = [&](std::string_view key) {
                    size_t at = v.find(key);
                    return at == std::string_view::npos ? -1 : std::atoi(std::string(v.substr(at + key.size())).c_str());
                };
                if (number("state=") >= 0)
                    m_AnimatorGraphPanel.SelectState(number("state="));
                if (number("transition=") >= 0)
                    m_AnimatorGraphPanel.SelectTransition(number("transition="));
                m_AnimatorGraphInViewport = v.find("viewport") != std::string_view::npos;
                m_AnimatorGraphPanel.Focus();
            }
            // ZAMIN_EDITOR_ASSET=<path relative to Assets>: the asset inspector shows that file.
            if (const char* asset = std::getenv("ZAMIN_EDITOR_ASSET"))
            {
                fs::path path = Project::GetAssetFileSystemPath(asset).lexically_normal();
                m_ContentBrowserPanel.Reveal({ path });
                m_AssetInspectorPanel.SetAsset(path);
                m_SceneHierarchyPanel.AcknowledgeSelection();
            }
            // ZAMIN_EDITOR_DELETE_PROMPT=<path relative to Assets>: the content browser asks to delete
            // it (the confirmation only; nothing is deleted without a click).
            if (const char* prompt = std::getenv("ZAMIN_EDITOR_DELETE_PROMPT"))
                m_ContentBrowserPanel.RequestDelete({ Project::GetAssetFileSystemPath(prompt).lexically_normal() });
            // ZAMIN_EDITOR_SEARCH=<text>: the content browser searches for it.
            if (const char* search = std::getenv("ZAMIN_EDITOR_SEARCH"))
                m_ContentBrowserPanel.SetSearch(search);
            // ZAMIN_EDITOR_UNUSED=1 | deep: the unused assets window scans and logs what it found.
            if (const char* unused = std::getenv("ZAMIN_EDITOR_UNUSED"))
            {
                m_ShowUnusedAssets = true;
                m_UnusedAssetsPanel.SetReachableOnly(std::string_view(unused) == "deep");
                m_UnusedAssetsPanel.LogNextResult();
            }
            if (const char* pivot = std::getenv("ZAMIN_EDITOR_PIVOT_CENTER"))
                m_GizmoPivotCenter = std::string_view(pivot) != "0";
            if (const char* filter = std::getenv("ZAMIN_EDITOR_FILTER")) // e.g. "#Coin"
                m_SceneHierarchyPanel.SetFilter(filter);
            // ZAMIN_EDITOR_INSPECTOR_BOTTOM=1: the inspector scrolled to its end; a larger number: that many px down.
            if (const char* bottom = std::getenv("ZAMIN_EDITOR_INSPECTOR_BOTTOM"))
            {
                float y = (float)std::atof(bottom);
                if (y > 1.0f) m_SceneHierarchyPanel.ScrollInspectorTo(y);
                else m_SceneHierarchyPanel.ScrollInspectorToBottom();
            }
            // ZAMIN_EDITOR_PLAY_ADDITIVE=<scene>[,<scene>]: loaded additively with Play (the Scenes panel's mark).
            if (const char* additive = std::getenv("ZAMIN_EDITOR_PLAY_ADDITIVE"))
            {
                std::string_view list(additive);
                while (!list.empty())
                {
                    const size_t comma = list.find(',');
                    m_ScenesPanel.SetPlayAdditive(std::string(list.substr(0, comma)), true);
                    list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
                }
            }
            // ZAMIN_EDITOR_GAME_VIEW=<Free|16:9|16:10|4:3|21:9|1920x1080|1280x720|<w>x<h>|<w>:<h>>[,menu]: the game
            // view's resolution while playing (not saved); "menu" opens its popup.
            if (const char* gameView = std::getenv("ZAMIN_EDITOR_GAME_VIEW"))
            {
                std::string_view spec(gameView);
                const size_t comma = spec.find(',');
                const std::string key(spec.substr(0, comma));
                m_OpenGameViewMenu = comma != std::string_view::npos && spec.substr(comma + 1) == "menu";
                auto& view = m_ViewSettings.GameView;
                view.Preset = GameViewResolution::FromKey(key, GameViewPreset::Custom);
                int w = 0, h = 0;
                char separator = 0;
                if (view.Preset == GameViewPreset::Custom && std::sscanf(key.c_str(), "%d%c%d", &w, &separator, &h) == 3)
                {
                    view.CustomSize = { w, h };
                    view.CustomAspect = separator == ':';
                    view.Sanitize();
                }
            }
            // ZAMIN_EDITOR_CONSOLE="filter=<text>;regex;case;collapse;trace;notime;select=<row>": the console's
            // search and options (not saved).
            if (const char* console = std::getenv("ZAMIN_EDITOR_CONSOLE"))
                m_ConsolePanel.ApplyAutomation(console);
            // ZAMIN_EDITOR_LOG="<n>x<trace|info|warn|error>:<text>|...": messages written to the log as the game's
            // (console filter / collapse checks), e.g. "3xwarn:Tanga topilmadi|1xinfo:Sahna yuklandi".
            if (const char* log = std::getenv("ZAMIN_EDITOR_LOG"))
            {
                std::string_view rest(log);
                while (!rest.empty())
                {
                    const size_t bar = rest.find('|');
                    const std::string_view item = rest.substr(0, bar);
                    rest = bar == std::string_view::npos ? std::string_view() : rest.substr(bar + 1);
                    const size_t x = item.find('x'), colon = item.find(':');
                    if (x == std::string_view::npos || colon == std::string_view::npos || colon < x)
                        continue;
                    const int count = std::clamp(std::atoi(std::string(item.substr(0, x)).c_str()), 1, 1000);
                    const std::string_view level = item.substr(x + 1, colon - x - 1);
                    const LogLevel logLevel = level == "trace" ? LogLevel::Trace : level == "warn" ? LogLevel::Warn
                                            : level == "error" ? LogLevel::Error : LogLevel::Info;
                    for (int i = 0; i < count; i++)
                        Log::Write(logLevel, true, std::string(item.substr(colon + 1)));
                }
            }
            // ZAMIN_EDITOR_PREFERENCES=<page 0..6>: the preferences window opens on that page.
            if (const char* prefs = std::getenv("ZAMIN_EDITOR_PREFERENCES"))
            {
                m_ShowPreferences = true;
                m_PreferencesPage = std::clamp(std::atoi(prefs), 0, 6);
            }
            // ZAMIN_EDITOR_UNDO="select=<name>;move=<name>,dx,dy,dz;rename=<name>,<new>;duplicate;delete;undo=<n>;history":
            // undo steps made one by one (each its own step, named as usual); "history" opens the window.
            if (const char* undo = std::getenv("ZAMIN_EDITOR_UNDO"))
                ApplyUndoAutomation(undo);
            // ZAMIN_EDITOR_MENU=<Fayl|Tahrirlash|Ko'rinish|Oyna|Yordam>[/recent|/layout|/views]: that main menu kept open (screenshots).
            if (const char* menu = std::getenv("ZAMIN_EDITOR_MENU"))
                m_OpenMenu = menu;
            // ZAMIN_EDITOR_ABOUT=1: the "Zamin haqida" window (the engine's version, commit and build).
            if (std::getenv("ZAMIN_EDITOR_ABOUT"))
                m_ShowAbout = true;
            // ZAMIN_EDITOR_LAYOUT=<Default|2D|Animation|FourViews|<saved name>>[,save=<name>][,manage][,saveprompt]: a window
            // layout applied / the current one saved / the Joylashuvlar window or the save prompt opened.
            if (const char* layout = std::getenv("ZAMIN_EDITOR_LAYOUT"))
                ApplyLayoutAutomation(layout);
            // ZAMIN_EDITOR_RECOVER=1 | later | delete: the recovery prompt's "Tiklash" (the offered copy is
            // loaded), "Keyinroq" (it is put aside, offered again next time) or "Nusxani o'chirish".
            if (const char* recover = std::getenv("ZAMIN_EDITOR_RECOVER"); recover && !m_Recovery.Autosave.empty())
            {
                m_Recovery.Pending = false;
                m_Recovery.CloseRequested = true;
                const std::string_view answer(recover);
                if (answer == "later")
                    DeferAutosaveRecovery();
                else if (answer == "delete")
                    DeleteOfferedAutosave();
                else
                    RecoverAutosave();
            }
            // ZAMIN_EDITOR_AUTOSAVE=now: the open scene's safety copy is written (deleted again on exit).
            if (const char* autosave = std::getenv("ZAMIN_EDITOR_AUTOSAVE"); autosave && std::string_view(autosave) == "now")
                WriteAutosave(true);
            // ZAMIN_EDITOR_ACCENT=<preset key, e.g. Violet>, ZAMIN_EDITOR_FONT_SIZE=<px>, ZAMIN_EDITOR_TINT=<preset key>:
            // the look for screenshots (not saved).
            if (const char* accent = std::getenv("ZAMIN_EDITOR_ACCENT"))
                for (const ColorPreset& preset : EditorThemeSettings::GetAccentPresets())
                    if (std::string_view(accent) == preset.Key)
                        m_Preferences.Theme.Accent = preset.Color;
            if (const char* fontSize = std::getenv("ZAMIN_EDITOR_FONT_SIZE"))
            {
                m_Preferences.Theme.FontSize = (float)std::atof(fontSize);
                m_Preferences.Theme.Sanitize();
            }
            // ZAMIN_EDITOR_LANGUAGE=en|uz: the editor's UI language; ZAMIN_EDITOR_GAME_LANGUAGE=<code>: the preview
            // language of the game's texts; ZAMIN_EDITOR_LOCALIZATION=1 (+ _RECT="x,y,w,h", _FILTER=<text>): the
            // localization table (screenshots; nothing is saved).
            if (const char* language = std::getenv("ZAMIN_EDITOR_LANGUAGE"))
                EditorText::SetLanguage(EditorText::ParseLanguage(language));
            if (const char* language = std::getenv("ZAMIN_EDITOR_GAME_LANGUAGE"); language && !Localization::SetLanguage(language))
                ZM_WARN("ZAMIN_EDITOR_GAME_LANGUAGE={}: o'yinda bunday til yo'q", language);
            if (std::getenv("ZAMIN_EDITOR_LOCALIZATION"))
            {
                m_ShowLocalization = true;
                m_LocalizationPanel.Focus();
                if (const char* rect = std::getenv("ZAMIN_EDITOR_LOCALIZATION_RECT"))
                {
                    float x = 0, y = 0, w = 0, h = 0;
                    if (std::sscanf(rect, "%f,%f,%f,%f", &x, &y, &w, &h) == 4 && w > 0 && h > 0)
                        m_LocalizationPanel.SetFloatingRect(ImVec4(x, y, w, h));
                }
                if (const char* filter = std::getenv("ZAMIN_EDITOR_LOCALIZATION_FILTER"))
                    m_LocalizationPanel.SetFilter(filter);
            }
            if (const char* tint = std::getenv("ZAMIN_EDITOR_TINT"))
                for (const ColorPreset& preset : PlayModeTintSettings::GetPresets())
                    if (std::string_view(tint) == preset.Key)
                        m_Preferences.PlayTint.Color = preset.Color;
            if (std::getenv("ZAMIN_EDITOR_PLAY"))
            {
                OnScenePlay();
                // ZAMIN_EDITOR_TIME_SCALE=<scale>: the game's clock (Time::SetTimeScale) once play started (screenshots).
                if (const char* scale = std::getenv("ZAMIN_EDITOR_TIME_SCALE"))
                    Time::SetTimeScale((float)std::atof(scale));
            }
            if (std::getenv("ZAMIN_EDITOR_STATS"))
                m_ShowStats = true;
            if (const char* colliders = std::getenv("ZAMIN_EDITOR_COLLIDERS")) // 0: collider outlines off
                m_ShowColliders = std::string_view(colliders) != "0";
            if (std::getenv("ZAMIN_EDITOR_TRANSPARENT_ORDER")) // the transparent order overlay on
                m_ShowTransparentOrder = true;
            if (std::getenv("ZAMIN_EDITOR_FRAME_ALL"))
                FrameAll();
            // ZAMIN_EDITOR_VIEWS=<axis>[,<axis>[,<axis>]] (top | front | right | bottom | back | left | persp): the extra
            // scene views open with those views (framed on the scene a few frames later).
            if (const char* views = std::getenv("ZAMIN_EDITOR_VIEWS"))
            {
                std::string_view rest(views);
                for (int i = 0; i < SceneViewSettings::Count && !rest.empty(); i++)
                {
                    const size_t comma = rest.find(',');
                    const ViewAxis axis = ViewAxisFromKey(rest.substr(0, comma));
                    rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                    OpenSceneView(i, true);
                    ExtraSceneView& view = m_ExtraViews[i];
                    EditorCameraState state = view.Camera.GetAxisViewState(axis);
                    if (axis == ViewAxis::Free)
                    {
                        state.Pitch = 0.45f;
                        state.Yaw = 0.7f;
                    }
                    view.Camera.SetViewState(state);
                    view.CameraLoaded = true;
                }
                m_LastSceneView = 0;
            }
            // ZAMIN_EDITOR_VIEW_AXIS=<view 1..4>:<axis>: a view's axis view (1: the viewport; e.g. "1:top").
            if (const char* axis = std::getenv("ZAMIN_EDITOR_VIEW_AXIS"))
            {
                std::string_view v(axis);
                const size_t colon = v.find(':');
                const int index = colon == std::string_view::npos ? 0 : std::clamp(std::atoi(std::string(v.substr(0, colon)).c_str()) - 1, 0, SceneViewSettings::Count);
                const ViewAxis target = ViewAxisFromKey(colon == std::string_view::npos ? v : v.substr(colon + 1));
                GetSceneViewCamera(index).SetViewAxis(target, 0.0f);
                if (index == 0 && target != ViewAxis::Front)
                    m_2DMode = false;
            }
            // ZAMIN_EDITOR_LABELS=all | selected | off[,menu][,fade=<start>:<end>][,scale=<x>][,nooverlap=0]: the entity names over
            // the views (for this session); "menu" opens the toolbar's names popup.
            if (const char* labels = std::getenv("ZAMIN_EDITOR_LABELS"))
            {
                std::string_view v(labels);
                auto& l = m_ViewSettings.EntityLabels;
                l.Mode = v.starts_with("all") ? EntityLabelMode::All : v.starts_with("off") ? EntityLabelMode::Off : EntityLabelMode::Selected;
                if (size_t at = v.find("fade="); at != std::string_view::npos)
                    std::sscanf(std::string(v.substr(at + 5)).c_str(), "%f:%f", &l.FadeStart, &l.FadeEnd);
                if (size_t at = v.find("scale="); at != std::string_view::npos)
                    l.Scale = (float)std::atof(std::string(v.substr(at + 6)).c_str());
                if (v.find("nooverlap=0") != std::string_view::npos)
                    l.HideOverlapping = false;
                l.Sanitize();
                m_OpenLabelsMenu = v.find("menu") != std::string_view::npos;
            }
            // ZAMIN_EDITOR_CAMERA_SETTINGS=<view 1..4>: that view's camera settings popup opens (1: the viewport).
            if (const char* settings = std::getenv("ZAMIN_EDITOR_CAMERA_SETTINGS"))
                m_OpenCameraSettings = std::clamp(std::atoi(settings) - 1, 0, SceneViewSettings::Count);
            // ZAMIN_EDITOR_CAMERA="x,y,z,distance,pitch,yaw": the editor camera looks at that point from
            // that distance (degrees; a positive pitch looks down).
            if (const char* cam = std::getenv("ZAMIN_EDITOR_CAMERA"))
            {
                float v[6] = { 0.0f, 0.0f, 0.0f, 10.0f, 30.0f, 0.0f };
                std::sscanf(cam, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
                m_EditorCamera.Focus({ v[0], v[1], v[2] }, std::max(v[3], 0.1f));
                m_EditorCamera.SetOrientation(glm::radians(v[4]), glm::radians(v[5]));
            }
            // ZAMIN_EDITOR_VIEW_MODE=lighting|normals|albedo|roughness|metallic: a debug view of the scene view;
            // ZAMIN_EDITOR_WIREFRAME=1|shaded: wireframe; ZAMIN_EDITOR_VIEW_MENU=1: the view mode menu opens.
            if (const char* mode = std::getenv("ZAMIN_EDITOR_VIEW_MODE"))
            {
                static const std::pair<const char*, RenderDebugView> modes[] = {
                    { "lit", RenderDebugView::Lit }, { "lighting", RenderDebugView::LightingOnly }, { "normals", RenderDebugView::Normals },
                    { "albedo", RenderDebugView::Albedo }, { "roughness", RenderDebugView::Roughness }, { "metallic", RenderDebugView::Metallic },
                };
                bool found = false;
                for (const auto& [name, view] : modes)
                    if (std::string_view(mode) == name)
                    {
                        m_DebugView = view;
                        found = true;
                    }
                if (!found)
                    ZM_WARN("ZAMIN_EDITOR_VIEW_MODE: noma'lum ko'rinish '{}'", mode);
            }
            if (const char* wire = std::getenv("ZAMIN_EDITOR_WIREFRAME"))
                m_Wireframe = m_WireframeStyle = std::string_view(wire) == "shaded" ? WireframeMode::ShadedWire : WireframeMode::Wire;
            if (std::getenv("ZAMIN_EDITOR_VIEW_MENU"))
                m_OpenViewModeMenu = true;
            // ZAMIN_EDITOR_DEBUG_DRAW=0 | 1 | raycasts,contacts,details,joints,velocities,normallength=0.6,menu:
            // debug drawing off / on, its physics views on, its popup open (for this session; not saved
            // unless the settings change).
            if (const char* debug = std::getenv("ZAMIN_EDITOR_DEBUG_DRAW"))
            {
                std::string_view v(debug);
                auto& d = m_ViewSettings.DebugDraw;
                d.Enabled = v != "0";
                if (v.find("raycasts") != std::string_view::npos) d.ShowRaycasts = true;
                if (v.find("contacts") != std::string_view::npos) d.ShowContacts = true;
                if (v.find("details") != std::string_view::npos) d.ShowContactDetails = true;
                if (v.find("joints") != std::string_view::npos) d.ShowJoints = true;
                if (v.find("velocities") != std::string_view::npos) d.ShowVelocities = d.ShowBodyVelocities = true;
                if (size_t at = v.find("normallength="); at != std::string_view::npos)
                {
                    d.NormalLength = std::strtof(std::string(v.substr(at + 13)).c_str(), nullptr);
                    d.Sanitize();
                }
                if (v.find("menu") != std::string_view::npos) m_OpenDebugDrawMenu = true;
            }
            // ZAMIN_EDITOR_GIZMOS=off | selected | all | frustums=all,lights=off,audio=selected,bounds=all,
            // space=local|world|both,skeleton=all,names,normals=selected,normallength=0.2,bydirection,bothsides,
            // menu: the component visualisations (for this session; not saved unless the settings change).
            if (const char* gizmos = std::getenv("ZAMIN_EDITOR_GIZMOS"))
            {
                std::string_view v(gizmos);
                auto& g = m_ViewSettings.Gizmos;
                auto parse = [](std::string_view value) {
                    if (value.starts_with("all")) return GizmoVisibility::All;
                    if (value.starts_with("off")) return GizmoVisibility::Off;
                    return GizmoVisibility::Selected;
                };
                if (v.starts_with("all") || v.starts_with("off") || v.starts_with("selected"))
                    g.CameraFrustums = g.LightDirections = g.AudioRanges = parse(v);
                for (auto [key, target] : { std::pair<std::string_view, GizmoVisibility*>{ "frustums=", &g.CameraFrustums },
                                            { "lights=", &g.LightDirections }, { "audio=", &g.AudioRanges },
                                            { "bounds=", &g.Bounds }, { "skeleton=", &g.Skeletons }, { "normals=", &g.Normals },
                                            { "joints=", &g.Joints2D }, { "particles=", &g.ParticleShapes },
                                            { "splines=", &g.Splines } })
                    if (size_t at = v.find(key); at != std::string_view::npos)
                        *target = parse(v.substr(at + key.size()));
                if (size_t at = v.find("normallength="); at != std::string_view::npos)
                    g.NormalLength = (float)std::atof(std::string(v.substr(at + 13)).c_str());
                if (v.find("bydirection") != std::string_view::npos) g.NormalsByDirection = true;
                if (v.find("bothsides") != std::string_view::npos) g.NormalsFacingOnly = false;
                g.Sanitize();
                if (size_t at = v.find("space="); at != std::string_view::npos)
                {
                    std::string_view space = v.substr(at + 6);
                    g.BoundsMode = space.starts_with("world") ? BoundsSpace::World : space.starts_with("both") ? BoundsSpace::Both
                                                                                                             : BoundsSpace::Local;
                }
                if (v.find("names") != std::string_view::npos) g.JointNames = true;
                if (v.find("menu") != std::string_view::npos) m_OpenGizmosMenu = true;
            }
            // ZAMIN_EDITOR_GIZMO_SIZE=<size> | size=1.5,thickness=2,icons=1.2,menu: the transform gizmo's size
            // factors (for this session), "menu" opens its popup.
            if (const char* gizmoSize = std::getenv("ZAMIN_EDITOR_GIZMO_SIZE"))
            {
                std::string_view v(gizmoSize);
                auto& tg = m_ViewSettings.TransformGizmo;
                auto number = [&](std::string_view key, float& out) {
                    if (size_t at = v.find(key); at != std::string_view::npos)
                        out = (float)std::atof(std::string(v.substr(at + key.size())).c_str());
                };
                if (!v.empty() && (std::isdigit((unsigned char)v[0]) || v[0] == '.'))
                    tg.Size = (float)std::atof(gizmoSize);
                number("size=", tg.Size);
                number("thickness=", tg.LineThickness);
                number("icons=", tg.IconSize);
                tg.Sanitize();
                if (v.find("menu") != std::string_view::npos) m_OpenTransformGizmoMenu = true;
            }
            // ZAMIN_EDITOR_OUTLINE=box | outline | width=3,occluded=0,children=0,hover=0,menu: the selection's
            // look (for this session); "menu" opens the gizmos popup (where its settings are).
            if (const char* outline = std::getenv("ZAMIN_EDITOR_OUTLINE"))
            {
                std::string_view v(outline);
                auto& o = m_ViewSettings.SelectionOutline;
                if (v.starts_with("box")) o.Style = SelectionStyle::Box;
                if (v.starts_with("outline")) o.Style = SelectionStyle::Outline;
                auto value = [&](std::string_view key) -> std::optional<float> {
                    if (size_t at = v.find(key); at != std::string_view::npos)
                        return (float)std::atof(std::string(v.substr(at + key.size())).c_str());
                    return std::nullopt;
                };
                if (auto w = value("width=")) o.Width = *w;
                if (auto w = value("hoverwidth=")) o.HoverWidth = *w;
                if (auto f = value("occluded=")) o.ShowOccluded = *f != 0.0f;
                if (auto f = value("children=")) o.Children = *f != 0.0f;
                if (auto f = value("hover=")) o.Hover = *f != 0.0f;
                o.Sanitize();
                if (v.find("menu") != std::string_view::npos) m_OpenGizmosMenu = true;
            }
            // ZAMIN_EDITOR_OVERLAY=0 | 1 | graph=0,calls=0,culling=0,hotspots,audio,corner=tl|tr|bl|br,target=144,menu:
            // the viewport's statistics overlay (for this session; not saved unless the settings change).
            if (const char* overlay = std::getenv("ZAMIN_EDITOR_OVERLAY"))
            {
                std::string_view v(overlay);
                auto& o = m_ViewSettings.StatsOverlay;
                o.Visible = v != "0";
                auto flag = [&](std::string_view key, bool& out) {
                    if (size_t at = v.find(key); at != std::string_view::npos)
                        out = v.substr(at + key.size()).substr(0, 1) != "0";
                };
                flag("graph=", o.FrameGraph);
                flag("calls=", o.DrawCalls);
                flag("culling=", o.Culling);
                if (v.find("hotspots") != std::string_view::npos) o.Hotspots = true;
                if (v.find("audio") != std::string_view::npos) o.Audio = true;
                if (size_t at = v.find("corner="); at != std::string_view::npos)
                {
                    std::string_view c = v.substr(at + 7, 2);
                    o.Corner = c == "tl" ? OverlayCorner::TopLeft : c == "bl" ? OverlayCorner::BottomLeft
                             : c == "br" ? OverlayCorner::BottomRight : OverlayCorner::TopRight;
                }
                if (size_t at = v.find("target="); at != std::string_view::npos)
                    o.TargetFPS = (float)std::atof(std::string(v.substr(at + 7)).c_str());
                o.Sanitize();
                if (v.find("menu") != std::string_view::npos) m_OpenStatsOverlayMenu = true;
            }
            // ZAMIN_EDITOR_AUDIO_MIXER=1 | mute=UI,solo=Music,...: the Audio Mixer window opens (in front); the
            // groups named are muted / soloed in the running mixer (for this session, not saved).
            if (const char* mixer = std::getenv("ZAMIN_EDITOR_AUDIO_MIXER"); mixer && std::string_view(mixer) != "0")
            {
                m_ShowAudioMixer = true;
                m_AudioMixerPanel.Focus();
                std::string_view rest(mixer);
                while (!rest.empty())
                {
                    size_t comma = rest.find(',');
                    std::string_view item = rest.substr(0, comma);
                    rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                    if (item.starts_with("mute=") && !AudioEngine::SetGroupMuted(std::string(item.substr(5)), true))
                        ZM_WARN("ZAMIN_EDITOR_AUDIO_MIXER: '{}' guruhi yo'q", item.substr(5));
                    if (item.starts_with("solo=") && !AudioEngine::SetGroupSolo(std::string(item.substr(5)), true))
                        ZM_WARN("ZAMIN_EDITOR_AUDIO_MIXER: '{}' guruhi yo'q", item.substr(5));
                }
            }
            // ZAMIN_EDITOR_SCENE_STATS=<components | scripts | prefabs | assets>[,<asset type key, e.g. Texture>]:
            // the scene statistics window opens on that tab.
            if (const char* sceneStats = std::getenv("ZAMIN_EDITOR_SCENE_STATS"))
            {
                std::string_view spec(sceneStats);
                std::string_view tab = spec.substr(0, spec.find(','));
                static constexpr std::string_view s_Tabs[] = { "components", "scripts", "prefabs", "assets" };
                m_ShowSceneStatistics = true;
                m_SceneStatsTab = 0;
                for (int i = 0; i < 4; i++)
                    if (tab == s_Tabs[i])
                        m_SceneStatsTab = i;
                if (size_t comma = spec.find(','); comma != std::string_view::npos)
                    if (auto type = AssetTypes::FromKey(spec.substr(comma + 1)))
                        m_SceneStatsAssetType = (int)*type;
            }
            // ZAMIN_EDITOR_PROFILER=1: the profiler's own window opens.
            if (const char* profiler = std::getenv("ZAMIN_EDITOR_PROFILER"))
                m_ShowProfiler = std::string_view(profiler) != "0";
            // ZAMIN_EDITOR_HOVER=<entity name>: the hover outline marks it as if the mouse were over it.
            if (const char* hover = std::getenv("ZAMIN_EDITOR_HOVER"))
                m_ForcedHover = hover;
            // ZAMIN_EDITOR_NO_CULLING=1 / ZAMIN_EDITOR_NO_INSTANCING=1: frustum culling (3D and 2D) / GPU
            // instancing off (to compare the pictures).
            if (std::getenv("ZAMIN_EDITOR_NO_CULLING"))
                Renderer3D::SetFrustumCulling(false);
            if (std::getenv("ZAMIN_EDITOR_NO_INSTANCING"))
                Renderer3D::SetInstancing(false);
            // ZAMIN_EDITOR_RENDER_SCALE=<0.5..2> / ZAMIN_EDITOR_AUTO_EXPOSURE=1: the open scene's settings
            // (not saved unless the scene is).
            if (const char* scale = std::getenv("ZAMIN_EDITOR_RENDER_SCALE"))
                m_ActiveScene->GetSettings().RenderScale = std::clamp((float)std::atof(scale), Renderer::MinRenderScale, Renderer::MaxRenderScale);
            if (std::getenv("ZAMIN_EDITOR_AUTO_EXPOSURE"))
                m_ActiveScene->GetSettings().Post.AutoExposure = true;
            // ZAMIN_EDITOR_SHADOWS="atlas=2048,filter=hard|pcf3|pcf5|poisson,cascades=2,split=0.5,depth=0.001,normal=2,
            // distance=60": the open scene's shadow settings (not saved unless the scene is).
            if (const char* shadows = std::getenv("ZAMIN_EDITOR_SHADOWS"))
            {
                auto& s = m_ActiveScene->GetSettings();
                std::string_view rest(shadows);
                while (!rest.empty())
                {
                    size_t comma = rest.find(',');
                    std::string_view pair = rest.substr(0, comma);
                    rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                    size_t eq = pair.find('=');
                    if (eq == std::string_view::npos)
                        continue;
                    std::string key(pair.substr(0, eq)), value(pair.substr(eq + 1));
                    if (key == "atlas") s.Shadows.AtlasSize = (uint32_t)std::atoi(value.c_str());
                    else if (key == "filter")
                        s.Shadows.Filter = value == "hard" ? ShadowFilter::Hard : value == "pcf5" ? ShadowFilter::PCF5x5
                                         : value == "poisson" ? ShadowFilter::Poisson : ShadowFilter::PCF3x3;
                    else if (key == "cascades") s.Shadows.CascadeCount = std::atoi(value.c_str());
                    else if (key == "split") s.Shadows.CascadeSplit = (float)std::atof(value.c_str());
                    else if (key == "depth") s.Shadows.DepthBias = (float)std::atof(value.c_str());
                    else if (key == "normal") s.Shadows.NormalBias = (float)std::atof(value.c_str());
                    else if (key == "distance") s.ShadowDistance = (float)std::atof(value.c_str());
                    else ZM_WARN("ZAMIN_EDITOR_SHADOWS: noma'lum kalit '{}'", key);
                }
                s.Shadows.Sanitize();
            }
            // ZAMIN_EDITOR_SCENE_SETTINGS=1 | sky | render | shadows | post | bloom | lens | exposure: the scene
            // settings tab in front (scrolled to that section).
            if (const char* settings = std::getenv("ZAMIN_EDITOR_SCENE_SETTINGS"))
            {
                m_ShowSceneSettings = m_FocusSceneSettings = true;
                m_SceneSettingsScrollTo = settings;
            }
            // ZAMIN_EDITOR_SCENES=1 | <scene relative to the assets>,list,thumbnails,thumbnails-all: the Scenes
            // window in front (that scene's card selected; "list": the list view; the thumbnails of the
            // scenes without one / of every scene drawn).
            if (const char* scenes = std::getenv("ZAMIN_EDITOR_SCENES"))
            {
                m_ShowScenes = true;
                m_ScenesPanel.Focus();
                std::string_view values(scenes);
                while (!values.empty())
                {
                    const size_t comma = values.find(',');
                    const std::string_view value = values.substr(0, comma);
                    values = comma == std::string_view::npos ? std::string_view() : values.substr(comma + 1);
                    if (value == "list")
                        m_ScenesPanel.SetListView(true);
                    else if ((value == "thumbnails" || value == "thumbnails-all") && Project::GetActive())
                    {
                        std::error_code ec;
                        for (auto it = fs::recursive_directory_iterator(Project::GetAssetDirectory(), ec); !ec && it != fs::recursive_directory_iterator();
                             it.increment(ec))
                            if (it->path().extension() == ".zscene" &&
                                (value == "thumbnails-all" || !fs::exists(SceneMetadata::GetThumbnailPath(it->path()))))
                                m_ThumbnailQueue.push_back(it->path());
                    }
                    else if (value.ends_with(".zscene"))
                        m_ScenesPanel.Select(std::string(value));
                }
            }
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
            // 1 | Input | SaveData | Tags | General | SortingLayers | PhysicsLayers | BuildScenes | Quality | Audio | Debug | Player
            if (const char* tab = std::getenv("ZAMIN_EDITOR_PROJECT_SETTINGS"))
            {
                m_ShowProjectSettings = true;
                std::string_view t(tab);
                m_ProjectSettingsPanel.Focus(t == "SaveData"        ? ProjectSettingsPanel::Tab::SaveData
                                           : t == "BuildScenes"     ? ProjectSettingsPanel::Tab::BuildScenes
                                           : t == "Tags"            ? ProjectSettingsPanel::Tab::Tags
                                           : t == "General"         ? ProjectSettingsPanel::Tab::General
                                           : t == "SortingLayers"   ? ProjectSettingsPanel::Tab::SortingLayers
                                           : t == "PhysicsLayers"   ? ProjectSettingsPanel::Tab::PhysicsLayers
                                           : t == "Quality"         ? ProjectSettingsPanel::Tab::Quality
                                           : t == "Audio"           ? ProjectSettingsPanel::Tab::Audio
                                           : t == "Debug"           ? ProjectSettingsPanel::Tab::Debug
                                           : t == "Player"          ? ProjectSettingsPanel::Tab::Player
                                           : t == "Graphics"        ? ProjectSettingsPanel::Tab::Graphics
                                           : t == "Time"            ? ProjectSettingsPanel::Tab::Time
                                           : t == "Localization"    ? ProjectSettingsPanel::Tab::Localization
                                                                    : ProjectSettingsPanel::Tab::Input);
                // ZAMIN_EDITOR_PROJECT_SETTINGS_RECT="x,y,w,h": the window floats there (screenshots);
                // ZAMIN_EDITOR_PROJECT_SETTINGS_SEARCH=<text>: the category search.
                if (const char* rect = std::getenv("ZAMIN_EDITOR_PROJECT_SETTINGS_RECT"))
                {
                    float x = 0, y = 0, w = 0, h = 0;
                    if (std::sscanf(rect, "%f,%f,%f,%f", &x, &y, &w, &h) == 4 && w > 0 && h > 0)
                        m_ProjectSettingsPanel.SetFloatingRect(ImVec4(x, y, w, h));
                }
                if (const char* search = std::getenv("ZAMIN_EDITOR_PROJECT_SETTINGS_SEARCH"))
                    m_ProjectSettingsPanel.SetSearch(search);
                // ZAMIN_EDITOR_PROJECT_SETTINGS_SCROLL=<px | max>: the category page scrolled down (screenshots).
                if (const char* scroll = std::getenv("ZAMIN_EDITOR_PROJECT_SETTINGS_SCROLL"))
                    m_ProjectSettingsPanel.RequestScroll(std::string_view(scroll) == "max" ? -1.0f : (float)std::atof(scroll));
                // ZAMIN_EDITOR_PROJECT_SETTINGS_OPEN=expand (the input map's entries) | presets (the physics presets menu)
                if (const char* open = std::getenv("ZAMIN_EDITOR_PROJECT_SETTINGS_OPEN"))
                    m_ProjectSettingsPanel.RequestOpen(open);
            }
            // ZAMIN_EDITOR_QUALITY=<level name | index>: the quality level the editor shows.
            if (const char* quality = std::getenv("ZAMIN_EDITOR_QUALITY"); quality && !Quality::SetLevelFromString(quality))
                ZM_WARN("ZAMIN_EDITOR_QUALITY={}: bunday sifat darajasi yo'q", quality);
            // ZAMIN_EDITOR_PARALLAX=1: the scene view moves parallax layers with the editor camera.
            if (std::getenv("ZAMIN_EDITOR_PARALLAX"))
                m_PreviewParallax = true;
            // ZAMIN_EDITOR_TILEMAP="tool=brush|rect|fill|eraser|picker|layer=1|rule=0|tile=5|brush=24>34|hover=12,3|drag=2,2>8,5|apply|editrule=3|
            //                      zoom=3|zoom=window:4|props|window"
            // (with ZAMIN_EDITOR_SELECT on a tilemap): tile painting on with that tool / layer / auto-tile
            // rule / tile, the preview at the hover cell (or a rectangle being dragged); "apply" uses
            // the tool there once.
            if (const char* tiles = std::getenv("ZAMIN_EDITOR_TILEMAP"))
                ApplyTilemapAutomation(tiles);
            // ZAMIN_EDITOR_SPRITE_EDITOR="<texture>[|cell=16x16|count=4x3|offset=1,1|padding=2,2|prefix=run|skip|slice|select=3|zoom=4]":
            // the Sprite Editor opens on that texture (path relative to Assets); "slice" cuts it with
            // the given settings (not saved), "save" writes the frames to its .zmeta.
            if (const char* sprite = std::getenv("ZAMIN_EDITOR_SPRITE_EDITOR"))
            {
                std::string_view spec(sprite);
                size_t bar = spec.find('|');
                OpenSpriteEditor(std::string(spec.substr(0, bar)));
                SpriteSliceSettings& slice = m_SpriteEditorPanel.GetSliceSettings();
                bool doSlice = false, doSave = false;
                int select = -1;
                auto pair = [](std::string_view v, char sep, glm::ivec2& out) {
                    size_t s = v.find(sep);
                    if (s == std::string_view::npos) return;
                    out = { std::atoi(std::string(v.substr(0, s)).c_str()), std::atoi(std::string(v.substr(s + 1)).c_str()) };
                };
                while (bar != std::string_view::npos)
                {
                    spec = spec.substr(bar + 1);
                    bar = spec.find('|');
                    std::string_view item = spec.substr(0, bar);
                    size_t eq = item.find('=');
                    std::string_view key = item.substr(0, eq), value = eq == std::string_view::npos ? std::string_view() : item.substr(eq + 1);
                    if (key == "cell") { slice.Mode = SpriteSliceSettings::SliceMode::CellSize; pair(value, 'x', slice.CellSize); }
                    else if (key == "count") { slice.Mode = SpriteSliceSettings::SliceMode::CellCount; pair(value, 'x', slice.CellCount); }
                    else if (key == "offset") pair(value, ',', slice.Offset);
                    else if (key == "padding") pair(value, ',', slice.Padding);
                    else if (key == "prefix") slice.NamePrefix = std::string(value);
                    else if (key == "skip") slice.SkipEmpty = true;
                    else if (key == "slice") doSlice = true;
                    else if (key == "save") doSave = true;
                    else if (key == "select") select = std::atoi(std::string(value).c_str());
                    else ZM_WARN("ZAMIN_EDITOR_SPRITE_EDITOR: noma'lum kalit '{}'", key);
                }
                if (doSlice)
                    ZM_INFO("Sprite muharriri: {} ta kadr kesildi", m_SpriteEditorPanel.Slice(true));
                if (select >= 0)
                    m_SpriteEditorPanel.SetSelected(select);
                if (doSave)
                    m_SpriteEditorPanel.Save();
            }
            if (const char* out = std::getenv("ZAMIN_EDITOR_EXPORT"))
            {
                BuildGame(out);
                // Automation (screenshots, CI): the export ends here, its report window opens in the next frames.
                if (Application::IsAutomationRun() && m_ExportThread.joinable())
                    m_ExportThread.join();
            }
            // ZAMIN_EDITOR_EXPORT_REPORT=<folder>: the report of an export done before (export-report.json) in its window.
            if (const char* folder = std::getenv("ZAMIN_EDITOR_EXPORT_REPORT"); folder && !OpenExportReport(folder))
                ZM_WARN("ZAMIN_EDITOR_EXPORT_REPORT: {} da eksport hisoboti yo'q", folder);
            if (std::getenv("ZAMIN_EDITOR_BUILD"))
                BuildScripts(true);
            // ZAMIN_EDITOR_BUILD_LOG=<file>: a saved compiler output in the Kompilyatsiya window (screenshots).
            if (const char* buildLog = std::getenv("ZAMIN_EDITOR_BUILD_LOG"))
                LoadBuildLogAutomation(buildLog);
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
            if (viewSettingsBefore.ToJson() != m_ViewSettings.ToJson())
                m_ViewSettingsOverride = ViewSettingsOverride{ viewSettingsBefore, m_ViewSettings };
        }

        // ZAMIN_EDITOR_CAMERA_SHAKE="intensity,duration,frequency[,frame]": the game camera shakes at that
        // frame (30; after ZAMIN_EDITOR_PLAY has started the game), the same call as a script's ShakeCamera.
        if (const char* shake = std::getenv("ZAMIN_EDITOR_CAMERA_SHAKE"))
        {
            float v[4] = { 0.7f, 1.0f, 20.0f, 30.0f };
            std::sscanf(shake, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]);
            if (frame == std::max(6, (int)v[3]) && !m_ActiveScene->ShakeCamera(v[0], v[1], v[2]))
                ZM_WARN("ZAMIN_EDITOR_CAMERA_SHAKE: asosiy kamera yo'q");
        }
        // ZAMIN_EDITOR_TOGGLE_ACTIVE=1: switches the selection off / on at frame 20 (after
        // ZAMIN_EDITOR_PLAY has started the game), the same path as Alt+Shift+A.
        // ZAMIN_EDITOR_THUMBNAIL_STATS=<frame>: logs what the content browser's thumbnail cache did by then.
        if (const char* at = std::getenv("ZAMIN_EDITOR_THUMBNAIL_STATS"); at && frame == std::max(6, std::atoi(at)))
        {
            ThumbnailCache::Stats stats = m_ContentBrowserPanel.GetThumbnails().GetStats();
            ZM_INFO("Thumbnaillar: {} ta xotirada, {} ta kutilmoqda, {} ta diskdan, {} ta yasaldi, {} ta model chizildi, "
                    "{} ta material chizildi, {} ta xato, {} ta model/material teksturasi bo'shatildi",
                    stats.Textures, stats.Waiting, stats.FromDisk, stats.Generated, stats.Rendered, stats.MaterialsRendered, stats.Failed,
                    stats.ReleasedTextures);
        }
        if (frame == 20 && std::getenv("ZAMIN_EDITOR_TOGGLE_ACTIVE"))
            ToggleSelectionActive();
        if (frame == 20 && !m_AutomationLayoutSave.empty())
        {
            SaveCurrentLayout(m_AutomationLayoutSave);
            m_AutomationLayoutSave.clear();
        }
        // ZAMIN_EDITOR_VIEWS: the extra views (their sizes known by now) framed on the scene.
        if (frame == 14 && std::getenv("ZAMIN_EDITOR_VIEWS"))
            if (AABB bounds; m_ActiveScene->ComputeSceneBounds(bounds))
                for (int i = 0; i < SceneViewSettings::Count; i++)
                    if (m_ViewSettings.SceneViews[i].Open)
                    {
                        EditorCamera& camera = m_ExtraViews[i].Camera;
                        camera.SetViewState(camera.ComputeFrameState(bounds, 1.1f));
                    }
        // ZAMIN_EDITOR_COLLIDER_EDIT=polygon|edge (with ZAMIN_EDITOR_SELECT): the collider's points are
        // edited in the viewport from frame 12; ZAMIN_EDITOR_COLLIDER_HOVER=v<i> | e<i> pins the hovered
        // point / edge (screenshots).
        if (const char* edit = std::getenv("ZAMIN_EDITOR_COLLIDER_EDIT"); edit && frame == 12)
        {
            using Kind = SceneHierarchyPanel::ColliderPointKind;
            m_SceneHierarchyPanel.SetColliderPointEdit(std::string_view(edit) == "edge" ? Kind::Edge : Kind::Polygon);
            if (const char* hover = std::getenv("ZAMIN_EDITOR_COLLIDER_HOVER"))
                m_ColliderHoverOverride = hover;
        }
        // ZAMIN_EDITOR_SPLINE_EDIT=1 (with ZAMIN_EDITOR_SELECT): the spline's points are edited in the
        // viewport from frame 12; ZAMIN_EDITOR_SPLINE_PICK=<point> picks one (gizmo, Bezier handles),
        // ZAMIN_EDITOR_SPLINE_HOVER=p<i> | i<i> | o<i> | c<u> pins the hovered point / handle / curve.
        if (std::getenv("ZAMIN_EDITOR_SPLINE_EDIT") && frame == 12)
        {
            m_SceneHierarchyPanel.SetSplinePointEdit(true);
            if (const char* pick = std::getenv("ZAMIN_EDITOR_SPLINE_PICK"))
                m_SplinePicked = std::atoi(pick);
            if (const char* hover = std::getenv("ZAMIN_EDITOR_SPLINE_HOVER"))
                m_SplineHoverOverride = hover;
        }
        // ZAMIN_EDITOR_JOINT_EDIT=distance|revolute|prismatic|weld|wheel (with ZAMIN_EDITOR_SELECT): that
        // joint's anchors are edited in the viewport from frame 12; ZAMIN_EDITOR_JOINT_HOVER=anchor |
        // connected | axis | lower | upper pins the hovered handle (screenshots).
        if (const char* edit = std::getenv("ZAMIN_EDITOR_JOINT_EDIT"); edit && frame == 12)
        {
            const std::string_view kind(edit);
            if (kind.ends_with("3d"))
            {
                // fixed3d | hinge3d | distance3d | slider3d | cone3d; ZAMIN_EDITOR_JOINT_PICK=anchor | connected:
                // that point is picked (its translate gizmo shows).
                const int type = kind.starts_with("hinge") ? (int)Joint3DType::Hinge : kind.starts_with("distance") ? (int)Joint3DType::Distance
                               : kind.starts_with("slider") ? (int)Joint3DType::Slider : kind.starts_with("cone") ? (int)Joint3DType::Cone
                                                                                                    : (int)Joint3DType::Fixed;
                m_SceneHierarchyPanel.SetJoint3DAnchorEdit(type);
                if (const char* pick = std::getenv("ZAMIN_EDITOR_JOINT_PICK"))
                {
                    m_Joint3DEntity = (uint64_t)m_SceneHierarchyPanel.GetSelectedEntity().GetUUID();
                    m_Joint3DType = type;
                    m_Joint3DPicked = std::string_view(pick) == "connected" ? 1 : 0;
                }
            }
            else
            {
                const int type = kind == "revolute" ? (int)Joint2DType::Revolute : kind == "prismatic" ? (int)Joint2DType::Prismatic
                               : kind == "weld" ? (int)Joint2DType::Weld : kind == "wheel" ? (int)Joint2DType::Wheel : (int)Joint2DType::Distance;
                m_SceneHierarchyPanel.SetJointAnchorEdit(type);
            }
            if (const char* hover = std::getenv("ZAMIN_EDITOR_JOINT_HOVER"))
                m_JointHoverOverride = hover;
        }
        // ZAMIN_EDITOR_UI_HOVER=<handle>: the UI rect gizmo shows that handle hovered ("right",
        // "top-left", "pivot", "anchor-tr", "move" ...); ZAMIN_EDITOR_UI_DRAG="<handle>:<dx>,<dy>" drags
        // the selected UI element's handle by view px at frame 20.
        if (frame == 5)
            if (const char* hover = std::getenv("ZAMIN_EDITOR_UI_HOVER"))
                m_UIRect.HoverOverride = hover;
        if (frame == 20 && m_SceneState == SceneState::Edit)
            if (const char* drag = std::getenv("ZAMIN_EDITOR_UI_DRAG"))
                ApplyUIRectAutomation(drag);
        // ZAMIN_EDITOR_FIT_SPRITE=1: the selected polygon collider follows its sprite (the inspector's
        // "Spraytga moslash") at frame 10.
        if (frame == 10 && std::getenv("ZAMIN_EDITOR_FIT_SPRITE") && m_SceneState == SceneState::Edit)
            if (Entity e = m_SceneHierarchyPanel.GetSelectedEntity(); e && e.HasComponent<PolygonCollider2DComponent>())
                if (SceneHierarchyPanel::FitPolygonToSprite(e, e.GetComponent<PolygonCollider2DComponent>()))
                {
                    ZM_INFO("Polygon collider spraytga moslandi: {} ta nuqta", e.GetComponent<PolygonCollider2DComponent>().Points.size());
                    MarkDirty();
                }

        // ZAMIN_EDITOR_PLAY_SCENE=<scene>[,additive][,<frame>]: the running game loads that scene (Single, or
        // additively) at frame 30 (or the one given); ZAMIN_EDITOR_STOP_FRAME=<n>: Play stops at frame n.
        if (const char* load = std::getenv("ZAMIN_EDITOR_PLAY_SCENE"); load && m_SceneState == SceneState::Play && m_RuntimeScenes)
        {
            std::string_view spec(load);
            std::string scene;
            LoadSceneMode mode = LoadSceneMode::Single;
            int at = 30;
            while (!spec.empty())
            {
                const size_t comma = spec.find(',');
                const std::string_view part = spec.substr(0, comma);
                spec = comma == std::string_view::npos ? std::string_view() : spec.substr(comma + 1);
                if (part == "additive")
                    mode = LoadSceneMode::Additive;
                else if (!part.empty() && std::all_of(part.begin(), part.end(), [](char c) { return std::isdigit((unsigned char)c); }))
                    at = std::atoi(std::string(part).c_str());
                else
                    scene = std::string(part);
            }
            if (frame == at && !scene.empty())
                m_RuntimeScenes->LoadScene(fs::path(scene), mode);
        }
        if (const char* stop = std::getenv("ZAMIN_EDITOR_STOP_FRAME"); stop && frame == std::atoi(stop) && m_SceneState != SceneState::Edit)
            OnSceneStop();

        if (const char* at = std::getenv("ZAMIN_EDITOR_SELECT_FRAME"); at && frame == std::max(6, std::atoi(at)))
            if (const char* sel = std::getenv("ZAMIN_EDITOR_SELECT"))
            {
                if (Entity e = m_ActiveScene->FindEntityByName(sel))
                    m_SceneHierarchyPanel.SetSelectedEntity(e);
                else
                    ZM_WARN("ZAMIN_EDITOR_SELECT: '{}' topilmadi", sel);
                // The inspector of the entity selected now shows its last components.
                if (const char* bottom = std::getenv("ZAMIN_EDITOR_INSPECTOR_BOTTOM"))
                {
                    float y = (float)std::atof(bottom);
                    if (y > 1.0f) m_SceneHierarchyPanel.ScrollInspectorTo(y);
                    else m_SceneHierarchyPanel.ScrollInspectorToBottom();
                }
            }
        // ZAMIN_EDITOR_STANDARD_MATERIALS=1: the selected entity's "Standart material..." list opens at frame 40.
        if (frame == 40 && std::getenv("ZAMIN_EDITOR_STANDARD_MATERIALS"))
            m_SceneHierarchyPanel.OpenStandardMaterialPicker();
        // ZAMIN_EDITOR_NEW_MATERIAL=1: "Yangi material" of the content browser's menu at frame 8;
        // a standard material's id instead of 1 ("wood", "glass"...) makes a copy of that one.
        if (const char* preset = std::getenv("ZAMIN_EDITOR_NEW_MATERIAL"); preset && frame == 8)
        {
            const MaterialPreset* chosen = MaterialPresets::Find(preset);
            if (!chosen && std::string_view(preset) != "1")
                ZM_WARN("ZAMIN_EDITOR_NEW_MATERIAL: '{}' standart material topilmadi", preset);
            m_ContentBrowserPanel.CreateMaterial(chosen);
        }
        // ZAMIN_EDITOR_EXPORT_MATERIALS=1 (with ZAMIN_EDITOR_ASSET on a glTF model): its materials
        // are exported as .zmat files at frame 12, like the inspector's button.
        if (frame == 12 && std::getenv("ZAMIN_EDITOR_EXPORT_MATERIALS"))
        {
            if (m_AssetInspectorPanel.ExportModelMaterials()) // the content browser shows what was written
                m_ContentBrowserPanel.SetCurrentDirectory(m_AssetInspectorPanel.GetLastMaterialExport().OutputDirectory);
            m_AssetInspectorPanel.RevealModelMaterials();
        }
        // ZAMIN_EDITOR_MATERIAL_DROP="Materials/Gold.zmat>Entity" (or "...>Entity#2" for part 2 of a
        // model): the material is dropped onto that entity at frame 30, like a viewport drop.
        if (const char* drop = std::getenv("ZAMIN_EDITOR_MATERIAL_DROP"); drop && frame == 30)
        {
            std::string_view v(drop);
            size_t arrow = v.find('>');
            if (arrow != std::string_view::npos)
            {
                std::string target(v.substr(arrow + 1));
                int submesh = -1;
                if (size_t hash = target.rfind('#'); hash != std::string::npos)
                {
                    submesh = std::atoi(target.c_str() + hash + 1);
                    target.resize(hash);
                }
                if (Entity e = m_ActiveScene->FindEntityByName(target))
                    ApplyMaterialToEntity(e, std::string(v.substr(0, arrow)), submesh);
                else
                    ZM_WARN("ZAMIN_EDITOR_MATERIAL_DROP: '{}' obyekti topilmadi", target);
            }
        }

        // ZAMIN_EDITOR_ASSET_PREVIEW (with ZAMIN_EDITOR_ASSET) at frame 12: "play" starts the sound /
        // the model's first animation ("clipsetting=N": entry N of its clip settings); R, G, B, A or RGB
        // shows that view of a texture. Audio:
        // "loop,zoom=4:0.3,play" - the preview loops, the waveform zoomed 4x around 30% of the sound.
        if (frame == 12 && m_AssetInspectorPanel.HasAsset())
            if (const char* preview = std::getenv("ZAMIN_EDITOR_ASSET_PREVIEW"))
            {
                std::string_view v(preview);
                if (v.find("zoom=") != std::string_view::npos || v.find("loop") != std::string_view::npos)
                {
                    if (v.find("loop") != std::string_view::npos)
                        m_AssetInspectorPanel.SetPreviewLoop(true);
                    if (size_t at = v.find("zoom="); at != std::string_view::npos)
                    {
                        std::string zoom(v.substr(at + 5, v.find(',', at) == std::string_view::npos ? std::string_view::npos : v.find(',', at) - at - 5));
                        double factor = std::atof(zoom.c_str()), center = 0.5;
                        if (size_t colon = zoom.find(':'); colon != std::string::npos)
                            center = std::atof(zoom.c_str() + colon + 1);
                        m_AssetInspectorPanel.ZoomWaveform(factor > 0.0 ? factor : 1.0, center);
                    }
                    if (v.find("play") != std::string_view::npos)
                        m_AssetInspectorPanel.StartPreviewPlayback();
                }
                else if (v == "play")
                    m_AssetInspectorPanel.StartPreviewPlayback();
                else if (v.starts_with("clipsetting=")) // models: that entry of the clip settings previewed (scrolled to them)
                    m_AssetInspectorPanel.PreviewClipSetting(std::atoi(std::string(v.substr(12)).c_str()));
                else if (v == "hdri" || v == "cube" || v == "plane" || v == "cylinder") // materials: lighting / shape
                {
                    MaterialPreview::Settings& view = m_AssetInspectorPanel.GetMaterialPreviewSettings();
                    if (v == "hdri")
                        m_AssetInspectorPanel.SetMaterialSceneLighting(true);
                    else
                        view.PreviewShape = v == "cube" ? MaterialPreview::Shape::Cube : v == "plane" ? MaterialPreview::Shape::Plane
                                                                                                      : MaterialPreview::Shape::Cylinder;
                }
                else
                    m_AssetInspectorPanel.SetTextureChannelView(v == "R" ? ImageChannelView::Red : v == "G" ? ImageChannelView::Green
                                                               : v == "B" ? ImageChannelView::Blue : v == "A" ? ImageChannelView::Alpha
                                                               : v == "RGB" ? ImageChannelView::RGB : ImageChannelView::RGBA);
            }

        // ZAMIN_EDITOR_ASSET_IMPORT="Filter=Nearest,sRGB=false" / "LoadMode=Stream,Volume=0.5" (with ZAMIN_EDITOR_ASSET): edits the
        // inspected asset's import settings at frame 30 and applies them, like the Apply button.
        if (frame == 30 && m_AssetInspectorPanel.HasAsset())
            if (const char* edits = std::getenv("ZAMIN_EDITOR_ASSET_IMPORT"))
            {
                AssetImportSettings import;
                AssetImporter::WriteTextureSettings(m_AssetInspectorPanel.GetEditedTextureSettings(), import);
                AssetImporter::WriteModelSettings(m_AssetInspectorPanel.GetEditedModelSettings(), import);
                AssetImporter::WriteAudioSettings(m_AssetInspectorPanel.GetEditedAudioSettings(), import);
                std::string_view rest(edits);
                while (!rest.empty())
                {
                    size_t comma = rest.find(',');
                    std::string_view pair = rest.substr(0, comma);
                    rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                    size_t eq = pair.find('=');
                    if (eq == std::string_view::npos)
                        continue;
                    std::string key(pair.substr(0, eq)), value(pair.substr(eq + 1));
                    char* end = nullptr;
                    double number = std::strtod(value.c_str(), &end);
                    if (value == "true" || value == "false")
                        import.SetBool(key, value == "true");
                    else if (end && *end == '\0' && !value.empty())
                        import.SetNumber(key, number);
                    else
                        import.SetString(key, value);
                }
                m_AssetInspectorPanel.GetEditedTextureSettings() = AssetImporter::ReadTextureSettings(import);
                m_AssetInspectorPanel.GetEditedModelSettings() = AssetImporter::ReadModelSettings(import);
                m_AssetInspectorPanel.GetEditedAudioSettings() = AssetImporter::ReadAudioSettings(import);
                m_AssetInspectorPanel.Apply();
            }

        // ZAMIN_EDITOR_MATERIAL_EDIT="Metallic=1,Roughness=0.2,Albedo=0.9:0.2:0.2:1" (with ZAMIN_EDITOR_ASSET
        // on a .zmat): edits the shown material at frame 30 like its fields do (live, not saved);
        // ZAMIN_EDITOR_MATERIAL_SAVE=1 then saves it.
        if (frame == 30 && m_AssetInspectorPanel.GetEditedMaterial())
            if (const char* edits = std::getenv("ZAMIN_EDITOR_MATERIAL_EDIT"))
            {
                MaterialData& d = *m_AssetInspectorPanel.GetEditedMaterial();
                std::string_view rest(edits);
                while (!rest.empty())
                {
                    size_t comma = rest.find(',');
                    std::string_view pair = rest.substr(0, comma);
                    rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                    size_t eq = pair.find('=');
                    if (eq == std::string_view::npos)
                        continue;
                    std::string key(pair.substr(0, eq)), value(pair.substr(eq + 1));
                    std::vector<float> numbers;
                    for (size_t start = 0; start <= value.size();)
                    {
                        size_t colon = value.find(':', start);
                        numbers.push_back(std::strtof(value.substr(start, colon - start).c_str(), nullptr));
                        if (colon == std::string::npos)
                            break;
                        start = colon + 1;
                    }
                    auto at = [&](size_t i) { return numbers[std::min(i, numbers.size() - 1)]; };
                    if (key == "Metallic") d.Metallic = at(0);
                    else if (key == "Roughness") d.Roughness = at(0);
                    else if (key == "NormalStrength") d.NormalStrength = at(0);
                    else if (key == "EmissionStrength") d.EmissionStrength = at(0);
                    else if (key == "AlphaCutoff") d.AlphaCutoff = at(0);
                    else if (key == "Albedo") d.Albedo = { at(0), at(1), at(2), numbers.size() > 3 ? at(3) : 1.0f };
                    else if (key == "Emission") d.Emission = { at(0), at(1), at(2) };
                    else if (key == "Tiling") d.Tiling = { at(0), at(1) };
                    else if (key == "Offset") d.Offset = { at(0), at(1) };
                    else if (key == "Shader") d.Shader = value == "Unlit" ? MaterialShader::Unlit : MaterialShader::Lit;
                    else if (key == "BlendMode")
                        d.BlendMode = MaterialSerializer::BlendModeFromString(value).value_or(MaterialBlendMode::Opaque);
                    else if (key == "DoubleSided") d.DoubleSided = value == "true" || value == "1";
                    else if (key == "PreserveSpecular") d.PreserveSpecular = value == "true" || value == "1";
                    else if (key == "SortPriority") d.SortPriority = (int)at(0);
                    else
                        ZM_WARN("ZAMIN_EDITOR_MATERIAL_EDIT: noma'lum maydon '{}'", key);
                }
                m_AssetInspectorPanel.ApplyMaterialEdit();
                if (std::getenv("ZAMIN_EDITOR_MATERIAL_SAVE"))
                    m_AssetInspectorPanel.SaveMaterial();
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
            m_AssetUsage.MarkDirty(); // the C++ code may name other assets now
            FinishBuildOutput(m_BuildSucceeded, Platform::GetTime() - m_BuildStartTime);
            if (m_BuildSucceeded)
            {
                ZM_INFO("Skriptlar muvaffaqiyatli kompilyatsiya qilindi ({:.1f} s)", Platform::GetTime() - m_BuildStartTime);
                if (m_ReloadAfterBuild)
                    ReloadScriptModule();
            }
            else
                ZM_ERROR("Skriptlarni kompilyatsiya qilishda xato: {} xato, {} ogohlantirish (Oyna > Kompilyatsiya)", m_BuildErrors, m_BuildWarnings);
        }
        if (!m_Exporting && m_ExportThread.joinable())
            m_ExportThread.join();
        TakeFinishedExportReport();

        CheckModuleHotReload(ts);
        ProcessAssetChanges();
        m_SceneThumbnails.Update();
        ProcessThumbnailQueue();

        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        if (m_RuntimeScenes)
            m_RuntimeScenes->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

        // The scene is drawn at the render scale of the viewport's size; post-processing scales it
        // into the final picture (the viewport's own size, where the overlays are drawn).
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
        {
            const uint32_t vw = (uint32_t)m_ViewportSize.x, vh = (uint32_t)m_ViewportSize.y;
            // Play: the game camera's size (a pixel-perfect camera draws at the art's resolution).
            glm::uvec2 renderSize = m_SceneState == SceneState::Play ? GetViewScene().GetRuntimeRenderResolution(vw, vh)
                                                                     : Renderer::GetRenderResolution(vw, vh, m_ActiveScene->GetSettings().RenderScale);
            if (const auto& spec = m_Framebuffer->GetSpecification(); spec.Width != renderSize.x || spec.Height != renderSize.y)
                m_Framebuffer->Resize(renderSize.x, renderSize.y);
            if (const auto& spec = m_FinalFramebuffer->GetSpecification(); spec.Width != vw || spec.Height != vh)
            {
                m_FinalFramebuffer->Resize(vw, vh);
                m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
            }
        }

        if (m_PendingClickThrough)
            ResolveClickThrough();

        Renderer::ResetStats();
        m_Framebuffer->Bind();
        glm::vec4 clear = GetViewScene().GetSettings().DrawSky ? glm::vec4(0.1f, 0.1f, 0.12f, 1.0f) : GetViewScene().GetClearColor();
        RenderCommand::SetClearColor({ std::pow(clear.r, 2.2f), std::pow(clear.g, 2.2f), std::pow(clear.b, 2.2f), clear.a }); // linear
        RenderCommand::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        {
            // In the game's own pixels (a scaled-down fixed resolution: its full size).
            const glm::vec2 gameMouse = ScreenToViewportPixel(ImGui::GetMousePos());
            const bool gameButton = m_ViewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            m_ActiveScene->SetViewportMouse(gameMouse, gameButton);
            // Typing reaches the game's input fields only while the game view has the focus.
            const bool gameKeyboard = m_ViewportFocused && !ImGui::GetIO().WantTextInput;
            m_ActiveScene->SetUIKeyboardEnabled(gameKeyboard);
            // The wheel over the game view scrolls the game's scroll views.
            const ImGuiIO& io = ImGui::GetIO();
            const glm::vec2 gameWheel = m_ViewportHovered && m_SceneState == SceneState::Play ? glm::vec2(io.MouseWheelH, io.MouseWheel)
                                                                                             : glm::vec2(0.0f);
            m_ActiveScene->SetViewportScroll(gameWheel);
            // Play mode: every scene of the game hears the same input.
            if (m_RuntimeScenes)
            {
                m_RuntimeScenes->SetViewportMouse(gameMouse, gameButton);
                m_RuntimeScenes->SetUIKeyboardEnabled(gameKeyboard);
                m_RuntimeScenes->SetViewportScroll(gameWheel);
            }
        }

        SceneRenderOptions options;
        options.ShowGrid = m_ViewSettings.Grid.Visible;
        options.GridPlane = GetGridPlaneForView(m_EditorCamera.GetForwardDirection(), m_EditorCamera.IsOrthographic(), m_2DMode);
        options.Grid = m_ViewSettings.Grid;
        options.SkipEditorHidden = true; // the hierarchy's eye: edit + simulate views only (play draws everything)
        options.DebugView = m_DebugView;
        options.Wireframe = m_Wireframe;
        options.Parallax = m_PreviewParallax;
        // A material dragged over a mesh is shown on it before the drop.
        if (m_MaterialDrag.Active && m_MaterialDrag.Material && m_SceneState == SceneState::Edit && (uint64_t)m_MaterialDrag.Target)
            if (Entity target = m_ActiveScene->GetEntityByUUID(m_MaterialDrag.Target))
            {
                options.MaterialPreviewEntity = (entt::entity)target;
                options.MaterialPreview = m_MaterialDrag.Material;
                options.MaterialPreviewSubmesh = m_MaterialDrag.Submesh;
            }

        // The camera does not take a mouse drag that belongs to the gizmo (Alt + drag on the
        // gizmo duplicates instead of orbiting; Alt pressed in the middle of a gizmo drag).
        if (m_GizmoAltDrag && !Input::IsMouseButtonPressed(MouseButton::Left))
            m_GizmoAltDrag = m_GizmoAltDuplicatePending = false;
        bool cameraInput = m_ViewportHovered && !m_GizmoAltDrag && !m_GizmoEdit.IsActive() && m_UIRect.Drag < 0;

        // What the viewport shows of DebugDraw: the editor's own settings (the player uses the
        // project's), given to DebugDraw only when they change - a game script's change of the
        // runtime settings stays (as in the player) until play stops.
        if (!m_AppliedDebugDraw || *m_AppliedDebugDraw != m_ViewSettings.DebugDraw)
            ApplyDebugDrawSettings();

        // The selection outline's silhouettes (the whole shapes of the outlined meshes, also where
        // they are hidden): the scene keeps this frame's draws of the selected entities, their
        // children and the entity the hover outline marked last frame.
        m_OutlineCaptured = false;
        m_OutlineHoverCaptured = {};
        if (const auto& outline = m_ViewSettings.SelectionOutline; outline.Style == SelectionStyle::Outline)
        {
            std::vector<OutlineEntity> entities = CollectSelectionOutline();
            // The scene is compared first: a hover entity of a scene that is gone (it is also cleared
            // when the active scene changes) must not be asked whether it is valid.
            if (outline.Hover && m_OutlineHover.GetScene() == m_ActiveScene.get() && m_OutlineHover.IsValid())
            {
                AppendOutlineHover(entities, m_OutlineHover);
                m_OutlineHoverCaptured = m_OutlineHover;
            }
            if (!entities.empty())
            {
                std::vector<int> ids;
                ids.reserve(entities.size());
                for (const OutlineEntity& e : entities)
                    ids.push_back(e.EntityID);
                Renderer3D::BeginCapture(std::move(ids));
                m_OutlineCaptured = true;
            }
        }

        // The Animation window's preview pose (edit mode) before the scene is drawn.
        m_AnimationPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
        m_AnimationPanel.OnUpdate(ts, m_ShowAnimation);

        uint64_t scenesDrawn = Renderer3D::GetEndSceneCount();
        {
            ZM_PROFILE_SCOPE("Sahna");
            switch (m_SceneState)
            {
                case SceneState::Edit:
                    UpdateTilePainting();
                    UpdateTilePreview(options);
                    m_EditorCamera.OnUpdate(ts, cameraInput);
                    m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera, options);
                    break;
                case SceneState::Simulate:
                    m_EditorCamera.OnUpdate(ts, cameraInput);
                    m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera, options);
                    break;
                case SceneState::Play:
                    // Every loaded scene's frame (additive ones, DontDestroyOnLoad), drawn together.
                    if (m_RuntimeScenes)
                        m_RuntimeScenes->OnUpdate(ts);
                    else
                        m_ActiveScene->OnUpdateRuntime(ts);
                    break;
            }
        }
        // What the scene drew (the overlay shows it apart from the editor's own drawing on top).
        m_SceneStats = Renderer::GetStats();
        if (m_OutlineCaptured)
            Renderer3D::EndCapture();
        // The open scene's sub-scenes in place (read-only), over its picture.
        if (m_SceneState == SceneState::Edit)
        {
            m_SubScenePreviews.Update(m_ActiveScene, m_EditorScenePath);
            m_SubScenePreviews.Render(m_EditorCamera, options);
        }
        // The order the scene's transparent surfaces were just drawn in (the overlay shows it);
        // nothing when the scene was not drawn (no camera).
        if (Renderer3D::GetEndSceneCount() != scenesDrawn)
        {
            m_TransparentOrder = Renderer3D::GetLastTransparentOrder();
            m_TransparentOrderViewProjection = Renderer3D::GetLastTransparentViewProjection();
        }
        else
            m_TransparentOrder.clear();

        if (m_SceneState == SceneState::Play && m_RuntimeScenes && m_RuntimeScenes->IsQuitRequested())
        {
            // The game's Quit: in the editor Play stops.
            ZM_INFO("O'yin chiqishni so'radi: Play to'xtatildi");
            m_RuntimeScenes->ClearQuitRequest();
            OnSceneStop();
        }

        // Mouse picking (the viewport's pixels, bottom-left origin; on the bars around a sized game
        // view the mouse is outside)
        const glm::vec2 mousePixel = ScreenToViewportPixel(ImGui::GetMousePos());
        const glm::vec2 viewportSize = m_ViewportSize;
        int mouseX = (int)std::floor(mousePixel.x), mouseY = (int)std::floor(viewportSize.y - mousePixel.y);
        if (mousePixel.x >= 0.0f && mousePixel.y >= 0.0f && mouseX < (int)viewportSize.x && mouseY >= 0 && mouseY < (int)viewportSize.y)
        {
            ZM_PROFILE_SCOPE("ID o'qish (sichqoncha)");
            glm::ivec2 render = ViewportToRenderPixel({ mouseX, mouseY });
            int pixel = m_Framebuffer->ReadPixel(1, render.x, render.y);
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

        // Post-processing into the final target (scaled to the viewport's size), then editor
        // overlays on top. Debug views that show values (normals, albedo ...) skip the effects.
        const auto& spec = m_Framebuffer->GetSpecification();
        PostProcessInput post;
        post.ColorTexture = m_Framebuffer->GetColorAttachmentRendererID(0);
        post.Width = spec.Width;
        post.Height = spec.Height;
        post.DepthTexture = m_Framebuffer->GetDepthAttachmentRendererID();
        post.Projection = GetViewScene().GetLastProjection();
        post.OutputWidth = m_FinalFramebuffer->GetSpecification().Width;
        post.OutputHeight = m_FinalFramebuffer->GetSpecification().Height;
        post.DeltaTime = ts;
        post.History = &m_PostHistory;
        m_PlayPixelZoom = {};
        if (PixelPerfectView pixel; m_SceneState == SceneState::Play && GetViewScene().GetPixelPerfectView(pixel) && pixel.Upscale &&
                                    pixel.RenderSize == glm::uvec2(spec.Width, spec.Height))
        {
            post.PixelZoom = pixel.Zoom;
            post.PixelOffset = PixelPerfect::GetUpscaleOffset(pixel);
            m_PlayPixelZoom = pixel;
        }
        const bool neutralPost = IsDebugViewActive() && !Renderer3D::DebugViewUsesPostProcessing(m_DebugView);
        PostProcessor::Apply(post, neutralPost ? PostProcessSettings::Neutral() : GetViewScene().GetSettings().Post, m_FinalFramebuffer);
        m_FinalFramebuffer->Bind();
        // A pixel-perfect game's UI: over the finished picture, at the viewport's resolution.
        if (m_SceneState == SceneState::Play && m_RuntimeScenes)
            m_RuntimeScenes->RenderDeferredUI();
        // The player's gamma and brightness (Display::SetGamma / SetBrightness, the game's options menu): the
        // game's picture in Play as the player shows it (the editor's overlays below are not adjusted).
        if (const DisplayAdjustment adjustment = Display::GetAdjustment(); m_SceneState == SceneState::Play && !adjustment.IsIdentity())
        {
            const FramebufferSpecification& finalSpec = m_FinalFramebuffer->GetSpecification();
            if (!m_PlayAdjustFramebuffer)
            {
                FramebufferSpecification spec;
                spec.Width = finalSpec.Width;
                spec.Height = finalSpec.Height;
                spec.Attachments = { FramebufferTextureFormat::RGBA8 };
                m_PlayAdjustFramebuffer = Framebuffer::Create(spec);
            }
            else if (m_PlayAdjustFramebuffer->GetSpecification().Width != finalSpec.Width ||
                     m_PlayAdjustFramebuffer->GetSpecification().Height != finalSpec.Height)
                m_PlayAdjustFramebuffer->Resize(finalSpec.Width, finalSpec.Height);
            m_PlayAdjustFramebuffer->Bind();
            DisplayPass::Present(m_FinalFramebuffer->GetColorAttachmentRendererID(0), adjustment);
            m_FinalFramebuffer->Bind();
            DisplayPass::Present(m_PlayAdjustFramebuffer->GetColorAttachmentRendererID(0), DisplayAdjustment());
            RenderCommand::SetDepthTest(true);
        }
        RenderSelectionOutline();
        RenderDebugDraw();
        {
            ZM_PROFILE_SCOPE("Editor overlay (gizmolar)");
            OnOverlayRender();
        }
        // A fade between the game's scenes covers the game view (the editor's overlays too).
        if (m_SceneState == SceneState::Play && m_RuntimeScenes)
            m_RuntimeScenes->RenderTransition(m_FinalFramebuffer->GetSpecification().Width, m_FinalFramebuffer->GetSpecification().Height);
        m_FinalFramebuffer->Unbind();

        // The extra scene views (Oyna > Ko'rinishlar), each from its own camera; a fly speed the
        // wheel changed becomes every view's.
        RenderSceneViews(ts);
        SyncFlySpeed();

        // The game's scene switches of this frame (LoadScene, async activations, DontDestroyOnLoad):
        // after its picture is finished, as the player does them.
        if (m_SceneState == SceneState::Play && m_RuntimeScenes)
        {
            m_RuntimeScenes->ProcessRequests();
            // A scene the game switched to that could not be loaded / came without some of its file.
            if (m_RuntimeScenes->GetLoadReportSerial() != m_RuntimeReportSerial)
            {
                m_RuntimeReportSerial = m_RuntimeScenes->GetLoadReportSerial();
                std::vector<SceneLoadReport> reports = m_RuntimeScenes->TakeLoadReports();
                if (!reports.empty())
                    ShowSceneLoadReport(reports.back());
            }
            SyncRuntimeScenes();
        }
    }

    Scene& EditorLayer::GetViewScene()
    {
        if (m_SceneState == SceneState::Play && m_RuntimeScenes)
            if (Scene* scene = m_RuntimeScenes->GetRenderScene())
                return *scene;
        return *m_ActiveScene;
    }

    void EditorLayer::SyncRuntimeScenes()
    {
        if (!m_RuntimeScenes)
            return;
        // The game switched its active scene: the hierarchy follows it. While the scene it shows is
        // still loaded (another additive scene, DontDestroyOnLoad picked in the hierarchy) it stays.
        const bool switched = m_RuntimeScenes->GetActiveSceneSerial() != m_RuntimeActiveSerial;
        m_RuntimeActiveSerial = m_RuntimeScenes->GetActiveSceneSerial();
        const bool shownLoaded = m_RuntimeScenes->IsLoaded(m_ActiveScene.get());
        if ((switched || !shownLoaded) && m_RuntimeScenes->GetActiveScene() && m_RuntimeScenes->GetActiveScene() != m_ActiveScene)
        {
            SetActiveScene(m_RuntimeScenes->GetActiveScene());
            ZM_INFO("Sahna almashtirildi: {}", m_ActiveScene->GetName());
        }
    }

    // Screen-space UI text gets no pixel contour: around every glyph of small text it reads as
    // garbled, overlapping letters. Its rectangle is marked by the UI rect gizmo instead (the
    // selected element's frame, a thin frame around the one under the mouse).
    static bool OutlinesPixels(Entity entity)
    {
        return !(entity.HasComponent<TextComponent>() && entity.HasComponent<UITransformComponent>());
    }

    std::vector<OutlineEntity> EditorLayer::CollectSelectionOutline()
    {
        std::vector<OutlineEntity> entities;
        Scene& scene = *m_ActiveScene;
        const bool children = m_ViewSettings.SelectionOutline.Children;
        const Entity primary = m_SceneHierarchyPanel.GetSelectedEntity();
        auto id = [](Entity e) { return (int)(uint32_t)(entt::entity)e; };
        for (Entity selected : m_SceneHierarchyPanel.GetSelection().GetEntities())
        {
            if (!selected.IsValid() || selected.GetScene() != &scene)
                continue;
            if (OutlinesPixels(selected))
                entities.push_back({ id(selected), selected == primary ? OutlineCategory::Primary : OutlineCategory::Secondary });
            if (children)
                for (Entity child : scene.GetDescendants(selected))
                    if (OutlinesPixels(child))
                        entities.push_back({ id(child), OutlineCategory::Child });
        }
        return entities;
    }

    void EditorLayer::AppendOutlineHover(std::vector<OutlineEntity>& entities, Entity hovered)
    {
        if (OutlinesPixels(hovered))
            entities.push_back({ (int)(uint32_t)(entt::entity)hovered, OutlineCategory::None, true });
        for (Entity child : m_ActiveScene->GetDescendants(hovered))
            if (OutlinesPixels(child))
                entities.push_back({ (int)(uint32_t)(entt::entity)child, OutlineCategory::None, true });
    }

    Entity EditorLayer::GetOutlineHoverEntity()
    {
        if (!m_ForcedHover.empty())
            return m_ActiveScene->FindEntityByName(m_ForcedHover);
        // What a left click would select now: nothing while playing, dragging, flying, painting,
        // over the gizmo or with a popup open; a locked entity is clicked through.
        if (m_SceneState == SceneState::Play || !m_ViewportHovered || !m_HoveredEntity || !m_HoveredEntity.IsValid())
            return {};
        if (m_ActiveScene->IsLockedInEditor(m_HoveredEntity) || m_SceneHierarchyPanel.IsTilePainting() || IsViewportHandleEditing() || m_MaterialDrag.Active ||
            m_GizmoEdit.IsActive() || m_GizmoHovered || m_EditorCamera.IsFlying())
            return {};
        for (ImGuiMouseButton button : { ImGuiMouseButton_Left, ImGuiMouseButton_Right, ImGuiMouseButton_Middle })
            if (ImGui::IsMouseDown(button))
                return {};
        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
            return {};
        if (m_UIRect.Hover >= 0 && m_UIRect.Hover != 0)
            return {}; // on a handle of the UI rect gizmo
        return m_HoveredUIElement ? m_HoveredUIElement : m_HoveredEntity;
    }

    void EditorLayer::RenderSelectionOutline()
    {
        const auto& settings = m_ViewSettings.SelectionOutline;
        // Nothing is marked when no outline is drawn (the next frame keeps no hover silhouettes).
        m_OutlineHover = {};
        if (settings.Style != SelectionStyle::Outline)
            return;
        // Play mode without a camera: nothing was drawn.
        if (m_SceneState == SceneState::Play && !GetViewScene().GetPrimaryCameraEntity())
            return;
        std::vector<OutlineEntity> entities = CollectSelectionOutline();
        // The hover outline follows the mesh silhouettes kept from this frame's scene: an entity the
        // mouse just moved onto is marked from the next frame on (its draws are kept then) - else
        // the edges of everything in front of it would be outlined for a frame.
        Entity hovered = settings.Hover ? GetOutlineHoverEntity() : Entity();
        if (hovered && !hovered.IsValid())
            hovered = {};
        m_OutlineHover = hovered;
        if (hovered)
        {
            bool meshes = false;
            if (hovered != m_OutlineHoverCaptured)
            {
                meshes = hovered.HasComponent<MeshRendererComponent>();
                for (Entity child : m_ActiveScene->GetDescendants(hovered))
                    meshes = meshes || child.HasComponent<MeshRendererComponent>();
            }
            if (!meshes)
                AppendOutlineHover(entities, hovered);
        }
        if (entities.empty())
            return;
        const auto& render = m_Framebuffer->GetSpecification();
        const auto& output = m_FinalFramebuffer->GetSpecification();
        SelectionOutlineInput input;
        input.EntityIDTexture = m_Framebuffer->GetColorAttachmentRendererID(1);
        input.IDWidth = render.Width;
        input.IDHeight = render.Height;
        input.Width = output.Width;
        input.Height = output.Height;
        input.ViewProjection = GetViewScene().GetLastProjection() * GetViewScene().GetLastView();
        input.Entities = std::move(entities);
        input.Silhouettes = m_OutlineCaptured ? &Renderer3D::GetCapturedDraws() : nullptr;
        SelectionOutline::Render(input, settings);
    }

    void EditorLayer::RenderDebugDraw()
    {
        // Play mode without a camera: nothing was drawn (the last view would be a stale one).
        if (m_SceneState == SceneState::Play && !GetViewScene().GetPrimaryCameraEntity())
            return;
        const auto& render = m_Framebuffer->GetSpecification();
        const auto& output = m_FinalFramebuffer->GetSpecification();
        DebugDrawView view;
        view.View = GetViewScene().GetLastView();
        view.Projection = GetViewScene().GetLastProjection();
        view.Width = output.Width;
        view.Height = output.Height;
        view.DepthTexture = m_Framebuffer->GetDepthAttachmentRendererID();
        view.DepthWidth = render.Width;
        view.DepthHeight = render.Height;
        DebugDraw::Render(view);
    }

    glm::ivec2 EditorLayer::ViewportToRenderPixel(glm::ivec2 pixel) const
    {
        const auto& render = m_Framebuffer->GetSpecification();
        const auto& view = m_FinalFramebuffer->GetSpecification();
        // Play with a pixel-perfect camera: the sharp, centred upscale.
        if (m_PlayPixelZoom.Upscale && m_SceneState == SceneState::Play && m_PlayPixelZoom.RenderSize == glm::uvec2(render.Width, render.Height))
            return glm::clamp(PixelPerfect::ViewToRenderPixel(pixel, m_PlayPixelZoom), glm::ivec2(0), glm::ivec2(render.Width, render.Height) - 1);
        if (view.Width == 0 || view.Height == 0 || (render.Width == view.Width && render.Height == view.Height))
            return pixel;
        // the render pixel under the viewport pixel's center
        return { (int)std::floor(((float)pixel.x + 0.5f) * (float)render.Width / (float)view.Width),
                 (int)std::floor(((float)pixel.y + 0.5f) * (float)render.Height / (float)view.Height) };
    }

    bool EditorLayer::IsDebugViewActive() const
    {
        return m_SceneState != SceneState::Play && m_DebugView != RenderDebugView::Lit;
    }

    glm::mat4 EditorLayer::OverlayViewProjection()
    {
        if (m_SceneState == SceneState::Play)
        {
            Scene& viewScene = GetViewScene();
            Entity camera = viewScene.GetPrimaryCameraEntity();
            if (!camera) return glm::mat4(0.0f);
            // What the game view shows: the camera with its shake (and its pixel-perfect view).
            return viewScene.GetCameraProjection(camera) * glm::inverse(viewScene.GetCameraViewTransform(camera));
        }
        return m_EditorCamera.GetViewProjection();
    }

    bool EditorLayer::OverlayCamera(glm::mat4& view, glm::mat4& projection)
    {
        if (m_SceneState == SceneState::Play)
        {
            Scene& viewScene = GetViewScene();
            Entity camera = viewScene.GetPrimaryCameraEntity();
            if (!camera) return false;
            view = glm::inverse(viewScene.GetCameraViewTransform(camera));
            projection = viewScene.GetCameraProjection(camera);
            return true;
        }
        view = m_EditorCamera.GetViewMatrix();
        projection = m_EditorCamera.GetProjection();
        return true;
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

    bool EditorLayer::GetTileCellUnderMouse(Entity tilemap, glm::ivec2& cell)
    {
        if (m_TileHoverOverride)
        {
            cell = m_TileHoverCell;
            return true;
        }
        if (!tilemap || !tilemap.HasComponent<TilemapComponent>())
            return false;
        const auto& tm = tilemap.GetComponent<TilemapComponent>();
        glm::mat4 world = m_ActiveScene->GetWorldTransform(tilemap);
        glm::vec3 hit;
        if (!ViewportMouseToPlane(world[3].z, hit) || std::abs(tm.TileSize) < 1e-6f)
            return false;
        glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(hit, 1.0f));
        float fx = std::floor(local.x / tm.TileSize), fy = std::floor(local.y / tm.TileSize);
        if (!std::isfinite(fx) || !std::isfinite(fy) || std::abs(fx) > 1e6f || std::abs(fy) > 1e6f)
            return false;
        cell = { (int)fx, (int)fy };
        return true;
    }

    int EditorLayer::ApplyTileTool(Entity e, SceneHierarchyPanel::TileTool tool, glm::ivec2 from, glm::ivec2 cell, bool erase)
    {
        using Tool = SceneHierarchyPanel::TileTool;
        auto& tm = e.GetComponent<TilemapComponent>();
        tm.EnsureSize();
        const int layer = std::clamp(m_SceneHierarchyPanel.GetActiveTileLayer(), 0, tm.GetLayerCount() - 1);
        const AutoTileSet* rules = AutoTileSet::Get(tm.TilesetPath);
        const AutoTileRule* rule = rules ? rules->Get(m_SceneHierarchyPanel.GetSelectedAutoTileRule()) : nullptr;
        const TileBrush& brush = m_SceneHierarchyPanel.GetTileBrush();
        erase = erase || tool == Tool::Eraser;
        // A multi-tile brush repeats from the stroke's origin (a click: centred on the cell).
        const glm::ivec2 origin = m_TileStroke.Active && m_TileStroke.Entity == e.GetUUID() ? m_TileStroke.Origin : brush.OriginFor(from);
        auto paint = [&](const std::vector<glm::ivec2>& cells, glm::ivec2 patternOrigin) {
            if (rule)
                return AutoTile::Paint(tm, layer, *rule, cells, erase);
            if (erase)
                return TilemapEdit::SetCells(tm, layer, cells, -1);
            return TilemapEdit::Stamp(tm, layer, cells, brush, patternOrigin);
        };
        switch (tool)
        {
            case Tool::Brush:
            {
                std::vector<glm::ivec2> line = TilemapEdit::LineCells(from, cell);
                return paint(rule ? line : brush.StrokeCells(line), origin);
            }
            case Tool::Eraser:
                return paint(TilemapEdit::LineCells(from, cell), origin);
            case Tool::Rectangle:
            {
                // The pattern starts at the rectangle's top-left corner.
                glm::ivec2 corner(std::min(from.x, cell.x), std::max(from.y, cell.y));
                return paint(TilemapEdit::RectCells(tm, from, cell), corner);
            }
            case Tool::Fill:
            {
                if (!tm.Contains(cell.x, cell.y))
                    return 0;
                if (rule)
                {
                    // On the terrain: the connected terrain; elsewhere: the connected cells of that tile.
                    std::vector<glm::ivec2> region = TilemapEdit::FloodRegion(tm, layer, cell.x, cell.y, rule);
                    if (!erase && rule->Contains(tm.Get(cell.x, cell.y, layer)))
                        return 0; // already this terrain
                    return AutoTile::Paint(tm, layer, *rule, region, erase);
                }
                if (brush.IsSingle() || erase)
                    return TilemapEdit::FloodFill(tm, layer, cell.x, cell.y, erase ? -1 : brush.GetTopLeft());
                return TilemapEdit::Stamp(tm, layer, TilemapEdit::FloodRegion(tm, layer, cell.x, cell.y), brush, brush.OriginFor(cell));
            }
            case Tool::Picker:
            {
                int picked = tm.Get(cell.x, cell.y, layer);
                if (picked >= 0)
                    m_SceneHierarchyPanel.PickTile(picked, rules ? rules->FindRuleOf(picked) : -1);
                return 0;
            }
        }
        return 0;
    }

    void EditorLayer::UpdateTilePainting()
    {
        using Tool = SceneHierarchyPanel::TileTool;
        auto& panel = m_SceneHierarchyPanel;
        if (!panel.IsTilePainting())
        {
            m_TileStroke.Active = false;
            return;
        }
        Entity e = panel.GetSelectedEntity();
        auto& tm = e.GetComponent<TilemapComponent>();
        tm.EnsureSize();
        const int layer = std::clamp(panel.GetActiveTileLayer(), 0, tm.GetLayerCount() - 1);
        const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_TileDragHeld;
        glm::ivec2 cell;
        const bool hasCell = GetTileCellUnderMouse(e, cell);

        if (m_TileStroke.Active)
        {
            // The selection or the layer changed under the stroke: it ends (what was painted stays).
            if (m_TileStroke.Entity != e.GetUUID() || m_TileStroke.Layer != layer)
            {
                m_TileStroke.Active = false;
                return;
            }
            if (mouseDown)
            {
                if (hasCell && cell != m_TileStroke.Last)
                {
                    if (m_TileStroke.Tool == Tool::Brush || m_TileStroke.Tool == Tool::Eraser)
                    {
                        int changed = ApplyTileTool(e, m_TileStroke.Tool, m_TileStroke.Last, cell, m_TileStroke.Erase);
                        m_TileStroke.Changed += changed;
                        if (changed) MarkDirty();
                    }
                    m_TileStroke.Last = cell;
                }
                return;
            }
            // Released: the rectangle is filled now. The stroke is one undo step (the snapshot is
            // taken once the mouse is up).
            if (m_TileStroke.Tool == Tool::Rectangle)
            {
                int changed = ApplyTileTool(e, Tool::Rectangle, m_TileStroke.Start, m_TileStroke.Last, m_TileStroke.Erase);
                m_TileStroke.Changed += changed;
                if (changed) MarkDirty();
            }
            m_TileStroke.Active = false;
            return;
        }

        if (!m_ViewportHovered || Input::IsKeyPressed(Key::LeftAlt) || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !hasCell)
            return;
        const Tool tool = panel.GetTileTool();
        const bool erase = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift) || tool == Tool::Eraser;
        m_TileStroke = TileStroke{ true, e.GetUUID(), layer, tool, cell, cell, erase, 0, panel.GetTileBrush().OriginFor(cell) };
        if (tool == Tool::Brush || tool == Tool::Eraser || tool == Tool::Fill || tool == Tool::Picker)
        {
            int changed = ApplyTileTool(e, tool, cell, cell, erase);
            if (changed) MarkDirty();
            if (tool == Tool::Fill || tool == Tool::Picker)
                m_TileStroke.Active = false; // one click
        }
    }

    void EditorLayer::UpdateTilePreview(SceneRenderOptions& options)
    {
        using Tool = SceneHierarchyPanel::TileTool;
        m_TilePreview.Valid = false;
        m_TilePreview.Erased.clear();
        auto& panel = m_SceneHierarchyPanel;
        glm::ivec2 hover;
        if (!panel.IsTilePainting() || !(m_ViewportHovered || m_TileHoverOverride))
            return;
        Entity e = panel.GetSelectedEntity();
        if (!GetTileCellUnderMouse(e, hover))
            return;
        auto& tm = e.GetComponent<TilemapComponent>();
        tm.EnsureSize();
        if ((size_t)tm.Width * tm.Height > (1u << 20))
            return; // huge maps: no live preview (painting still works)
        const int layer = std::clamp(panel.GetActiveTileLayer(), 0, tm.GetLayerCount() - 1);
        const Tool tool = m_TileStroke.Active ? m_TileStroke.Tool : panel.GetTileTool();
        const bool erase = m_TileStroke.Active ? m_TileStroke.Erase
                         : tool == Tool::Eraser || Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);
        const AutoTileSet* rules = AutoTileSet::Get(tm.TilesetPath);
        const AutoTileRule* rule = rules ? rules->Get(panel.GetSelectedAutoTileRule()) : nullptr;

        const TileBrush& brush = panel.GetTileBrush();
        const bool stamp = !rule && !erase;
        std::vector<glm::ivec2> cells;
        glm::ivec2 origin = brush.OriginFor(hover);
        if (m_TileStroke.Active && tool == Tool::Rectangle)
        {
            cells = TilemapEdit::RectCells(tm, m_TileStroke.Start, m_TileStroke.Last);
            origin = { std::min(m_TileStroke.Start.x, m_TileStroke.Last.x), std::max(m_TileStroke.Start.y, m_TileStroke.Last.y) };
        }
        else if (tool == Tool::Fill && tm.Contains(hover.x, hover.y))
        {
            if (!rule || erase || !rule->Contains(tm.Get(hover.x, hover.y, layer)))
                cells = TilemapEdit::FloodRegion(tm, layer, hover.x, hover.y, rule);
        }
        else if (tool == Tool::Brush && !rule)
        {
            // The brush's footprint (aligned with the stroke being painted).
            if (m_TileStroke.Active && m_TileStroke.Entity == e.GetUUID())
                origin = m_TileStroke.Origin;
            for (const glm::ivec2& c : brush.StrokeCells({ hover }))
                if (tm.Contains(c.x, c.y))
                    cells.push_back(c);
        }
        else if ((tool == Tool::Brush || tool == Tool::Eraser) && tm.Contains(hover.x, hover.y))
            cells = { hover };
        if (cells.empty())
            return;

        // The tool on a copy of the layer.
        TilemapComponent trial;
        trial.Width = tm.Width;
        trial.Height = tm.Height;
        trial.Layers = { tm.Layers[(size_t)layer] };
        if (rule)
            AutoTile::Paint(trial, 0, *rule, cells, erase);
        else if (stamp)
            TilemapEdit::Stamp(trial, 0, cells, brush, origin);
        else
            TilemapEdit::SetCells(trial, 0, cells, -1);
        const std::vector<int>& before = tm.Layers[(size_t)layer].Tiles;
        std::vector<int>& after = trial.Layers[0].Tiles;
        for (size_t i = 0; i < after.size(); i++)
            if (after[i] < 0 && before[i] >= 0)
                m_TilePreview.Erased.push_back({ (int)(i % (size_t)tm.Width), (int)(i / (size_t)tm.Width) });
        m_TilePreview.Valid = true;
        m_TilePreview.Entity = e.GetUUID();
        m_TilePreview.Layer = layer;
        m_TilePreview.Tiles = std::move(after);
        options.TilePreviewEntity = (entt::entity)e;
        options.TilePreviewLayer = layer;
        options.TilePreviewTiles = &m_TilePreview.Tiles;
    }

    void EditorLayer::ApplyTilemapAutomation(std::string_view spec)
    {
        using Tool = SceneHierarchyPanel::TileTool;
        auto& panel = m_SceneHierarchyPanel;
        Entity e = panel.GetSelectedEntity();
        if (!e || !e.HasComponent<TilemapComponent>())
        {
            ZM_WARN("ZAMIN_EDITOR_TILEMAP: tilemap tanlanmagan (ZAMIN_EDITOR_SELECT bilan tanlang)");
            return;
        }
        panel.SetTilePaint(true);
        bool apply = false, dragging = false;
        glm::ivec2 dragFrom(0), dragTo(0);
        while (!spec.empty())
        {
            size_t bar = spec.find('|');
            std::string_view item = spec.substr(0, bar);
            spec = bar == std::string_view::npos ? std::string_view() : spec.substr(bar + 1);
            size_t eq = item.find('=');
            std::string_view key = item.substr(0, eq), value = eq == std::string_view::npos ? std::string_view() : item.substr(eq + 1);
            std::string text(value);
            if (key == "tool")
                panel.SetTileTool(value == "rect" ? Tool::Rectangle : value == "fill" ? Tool::Fill : value == "eraser" ? Tool::Eraser
                                : value == "picker" ? Tool::Picker : Tool::Brush);
            else if (key == "layer")
                panel.SetActiveTileLayer(std::atoi(text.c_str()));
            else if (key == "rule")
                panel.SetSelectedAutoTileRule(std::atoi(text.c_str()));
            else if (key == "tile")
                panel.PickTile(std::atoi(text.c_str()), -1);
            else if (key == "brush") // "brush=24>34": the palette rectangle between two tiles
            {
                int a = 0, b = 0;
                if (std::sscanf(text.c_str(), "%d>%d", &a, &b) == 2)
                {
                    const auto& tm = e.GetComponent<TilemapComponent>();
                    panel.SetTileBrush(TileBrush::FromTileset(tm.TilesetColumns, tm.TilesetRows, a, b));
                    panel.SetSelectedAutoTileRule(-1);
                }
            }
            else if (key == "zoom") // "zoom=3" (inspector) / "zoom=window:4"
            {
                bool window = value.starts_with("window:");
                panel.SetTilePaletteZoom(window ? 1 : 0, (float)std::atof(text.c_str() + (window ? 7 : 0)));
            }
            else if (key == "props")
                panel.SetTilePropertiesOpen(true);
            else if (key == "window")
                m_ShowTilePalette = true;
            else if (key == "hover")
            {
                std::sscanf(text.c_str(), "%d,%d", &m_TileHoverCell.x, &m_TileHoverCell.y);
                m_TileHoverOverride = true;
            }
            else if (key == "drag")
            {
                std::sscanf(text.c_str(), "%d,%d>%d,%d", &dragFrom.x, &dragFrom.y, &dragTo.x, &dragTo.y);
                dragging = true;
            }
            else if (key == "apply")
                apply = true;
            else if (key == "editrule") // the selected rule's variant editor; "editrule=5": variant 5 picked
                panel.SetAutoTileRuleEditor(true, value.empty() ? -1 : std::atoi(text.c_str()));
            else
                ZM_WARN("ZAMIN_EDITOR_TILEMAP: noma'lum kalit '{}'", key);
        }
        if (dragging)
        {
            // A rectangle being dragged (its preview), or filled at once with "apply".
            m_TileHoverOverride = true;
            m_TileHoverCell = dragTo;
            if (apply)
            {
                if (ApplyTileTool(e, Tool::Rectangle, dragFrom, dragTo, false)) MarkDirty();
                apply = false;
            }
            else
            {
                m_TileStroke = TileStroke{ true, e.GetUUID(), std::clamp(panel.GetActiveTileLayer(), 0, e.GetComponent<TilemapComponent>().GetLayerCount() - 1),
                                           Tool::Rectangle, dragFrom, dragTo, false, 0, panel.GetTileBrush().OriginFor(dragFrom) };
                m_TileDragHeld = true; // as if the button were still down: only the preview
            }
        }
        if (apply && m_TileHoverOverride)
            if (int changed = ApplyTileTool(e, panel.GetTileTool(), m_TileHoverCell, m_TileHoverCell, false))
            {
                MarkDirty();
                ZM_INFO("ZAMIN_EDITOR_TILEMAP: {} ta katak o'zgardi", changed);
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

    // Arc of the circle around `center` in the plane of the unit axes A and B, from angle a0 to a1 (radians, from A towards B).
    static void DrawWireArc(const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB, float radius, float a0, float a1,
                            const glm::vec4& color, int segments = 24)
    {
        glm::vec3 prev = center + (axisA * std::cos(a0) + axisB * std::sin(a0)) * radius;
        for (int i = 1; i <= segments; i++)
        {
            float t = a0 + (a1 - a0) * i / segments;
            glm::vec3 p = center + (axisA * std::cos(t) + axisB * std::sin(t)) * radius;
            Renderer2D::DrawLine(prev, p, color);
            prev = p;
        }
    }

    // Two unit axes perpendicular to `dir` (and to each other).
    static void PerpendicularAxes(const glm::vec3& dir, glm::vec3& u, glm::vec3& v)
    {
        glm::vec3 helper = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        u = glm::normalize(glm::cross(dir, helper));
        v = glm::cross(dir, u);
    }

    // The overlay's camera, for shapes that keep their size on screen and spheres' outlines.
    struct EditorOverlayView
    {
        glm::mat4 View{ 1.0f }, Projection{ 1.0f };
        glm::vec3 Position{ 0.0f }, Forward{ 0.0f, 0.0f, -1.0f };
        float ViewportHeight = 1.0f;

        bool IsPerspective() const { return Projection[3][3] == 0.0f; }
        // World units one viewport pixel covers at `point`.
        float PixelSize(const glm::vec3& point) const
        {
            float scale = std::abs(Projection[1][1]) * std::max(ViewportHeight, 1.0f);
            if (!(scale > 1e-9f)) return 0.01f;
            if (!IsPerspective())
                return 2.0f / scale;
            float depth = std::max(-(View * glm::vec4(point, 1.0f)).z, 1e-3f);
            return 2.0f * depth / scale;
        }
    };

    // A wire sphere: three circles around the world axes and its outline as the camera sees it.
    static void DrawWireSphere(const glm::vec3& center, float radius, const glm::vec4& color, const EditorOverlayView* view = nullptr)
    {
        if (!(radius > 0.0f) || !std::isfinite(radius)) return;
        DrawWireCircle(center, { 1, 0, 0 }, { 0, 1, 0 }, radius, color);
        DrawWireCircle(center, { 1, 0, 0 }, { 0, 0, 1 }, radius, color);
        DrawWireCircle(center, { 0, 1, 0 }, { 0, 0, 1 }, radius, color);
        if (!view) return;
        glm::vec3 u, v;
        if (!view->IsPerspective())
        {
            PerpendicularAxes(view->Forward, u, v);
            DrawWireCircle(center, u, v, radius, color);
            return;
        }
        // Seen from distance D the outline is the circle where the sphere's tangent cone touches
        // it: r * sqrt(D^2 - r^2) / D across, r^2 / D closer to the camera than the centre.
        glm::vec3 toCamera = view->Position - center;
        float d = glm::length(toCamera);
        if (d <= radius * 1.001f) return; // inside: no outline
        toCamera /= d;
        PerpendicularAxes(toCamera, u, v);
        DrawWireCircle(center + toCamera * (radius * radius / d), u, v, radius * std::sqrt(d * d - radius * radius) / d, color);
    }

    // A point light's range: a sphere of circles around it.
    static void DrawPointLightRange(const glm::vec3& center, float radius, const glm::vec4& color, const EditorOverlayView& view)
    {
        DrawWireSphere(center, radius, color, &view);
    }

    // A line with a pyramid head of `headSize` at `to`.
    static void DrawWireArrow(const glm::vec3& from, const glm::vec3& to, float headSize, const glm::vec4& color)
    {
        Renderer2D::DrawLine(from, to, color);
        glm::vec3 d = to - from;
        float length = glm::length(d);
        if (!(length > 1e-6f)) return;
        d /= length;
        float head = std::min(headSize, length), width = head * 0.4f;
        glm::vec3 u, v;
        PerpendicularAxes(d, u, v);
        glm::vec3 base = to - d * head;
        const glm::vec3 fins[4] = { base + u * width, base + v * width, base - u * width, base - v * width };
        for (int i = 0; i < 4; i++)
        {
            Renderer2D::DrawLine(to, fins[i], color);
            Renderer2D::DrawLine(fins[i], fins[(i + 1) % 4], color);
        }
    }

    // How far a perspective camera's frustum is drawn solid (world units; on to the far plane faint).
    static constexpr float CameraFrustumPreviewDistance = 10.0f;

    // A camera's view volume (FrustumCorners). Perspective cameras reach far (hundreds of
    // units): solid up to `previewDistance` - a rectangle there with a small triangle on top
    // marking the camera's up side - and faint on to the far plane. Orthographic ones: the box.
    static void DrawCameraFrustum(const std::array<glm::vec3, 8>& c, const glm::mat4& cameraWorld, bool perspective,
                                  float previewDistance, const glm::vec4& color)
    {
        const glm::vec4 faint(color.r, color.g, color.b, color.a * 0.3f);
        const glm::vec3 eye(cameraWorld[3]), forward = glm::normalize(-glm::vec3(cameraWorld[2]));
        // Depth along the view direction is linear along each edge (near corner -> far corner).
        const float nearDepth = glm::dot(c[0] - eye, forward), farDepth = glm::dot(c[4] - eye, forward);
        float t = 1.0f;
        if (perspective && farDepth > previewDistance && farDepth - nearDepth > 1e-6f)
            t = std::clamp((previewDistance - nearDepth) / (farDepth - nearDepth), 0.0f, 1.0f);
        std::array<glm::vec3, 4> preview;
        for (int i = 0; i < 4; i++)
            preview[i] = glm::mix(c[i], c[i + 4], t);

        const int rect[4][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 } };
        for (const auto& e : rect)
        {
            Renderer2D::DrawLine(c[e[0]], c[e[1]], color);
            Renderer2D::DrawLine(preview[e[0]], preview[e[1]], color);
            if (t < 1.0f)
                Renderer2D::DrawLine(c[e[0] + 4], c[e[1] + 4], faint);
        }
        for (int i = 0; i < 4; i++)
        {
            Renderer2D::DrawLine(c[i], preview[i], color);
            if (t < 1.0f)
                Renderer2D::DrawLine(preview[i], c[i + 4], faint);
        }
        // The up marker over the rectangle the camera is drawn up to (the near one of a box).
        const glm::vec3* r = perspective ? preview.data() : c.data();
        glm::vec3 up = r[2] - r[0];
        float height = glm::length(up), width = glm::length(r[3] - r[2]);
        if (height > 1e-6f)
        {
            up /= height;
            glm::vec3 a = glm::mix(r[2], r[3], 0.35f), b = glm::mix(r[2], r[3], 0.65f);
            glm::vec3 apex = (r[2] + r[3]) * 0.5f + up * (width * 0.15f);
            Renderer2D::DrawLine(a, apex, color);
            Renderer2D::DrawLine(apex, b, color);
            Renderer2D::DrawLine(b, a, color);
        }
    }

    // A directional light: a ring around its position facing the way the light goes, rays along
    // it from the ring and an arrow from the middle (`pixel`: world units of a screen pixel there).
    static void DrawDirectionalLightGizmo(const glm::vec3& origin, const glm::vec3& direction, float pixel, const glm::vec4& color)
    {
        if (!(glm::length(direction) > 1e-6f)) return;
        glm::vec3 d = glm::normalize(direction), u, v;
        PerpendicularAxes(d, u, v);
        const float ring = 26.0f * pixel, rays = 52.0f * pixel, arrow = 104.0f * pixel;
        const glm::vec4 faint(color.r, color.g, color.b, color.a * 0.7f);
        DrawWireCircle(origin, u, v, ring, color);
        for (int i = 0; i < 8; i++)
        {
            float a = glm::two_pi<float>() * (float)i / 8.0f;
            glm::vec3 p = origin + (u * std::cos(a) + v * std::sin(a)) * ring;
            Renderer2D::DrawLine(p, p + d * rays, faint);
        }
        DrawWireArrow(origin, origin + d * arrow, 18.0f * pixel, color);
    }

    // A spot light's cone: the outer cone (edges, the circle where it ends and the cap of its
    // range sphere) and, fainter, the circle where the inner (full brightness) cone ends.
    static void DrawSpotLightCone(const glm::vec3& apex, const glm::vec3& direction, float range, float innerDegrees, float outerDegrees,
                                  const glm::vec4& color)
    {
        if (range <= 0.0f || glm::length(direction) < 1e-6f) return;
        glm::vec3 d = glm::normalize(direction), u, v;
        PerpendicularAxes(d, u, v);
        float outer = glm::radians(std::clamp(outerDegrees, 0.1f, SpotLightComponent::MaxAngle));
        float inner = glm::radians(std::clamp(innerDegrees, 0.0f, glm::degrees(outer)));
        glm::vec3 baseCenter = apex + d * (range * std::cos(outer));
        float baseRadius = range * std::sin(outer);
        DrawWireCircle(baseCenter, u, v, baseRadius, color);
        for (glm::vec3 side : { u, -u, v, -v })
            Renderer2D::DrawLine(apex, baseCenter + side * baseRadius, color);
        // the cap: where the range sphere closes the cone
        DrawWireArc(apex, d, u, range, -outer, outer, color);
        DrawWireArc(apex, d, v, range, -outer, outer, color);
        if (inner > 0.001f && inner < outer - 0.001f)
        {
            glm::vec4 faint(color.r, color.g, color.b, color.a * 0.45f);
            DrawWireCircle(apex + d * (range * std::cos(inner)), u, v, range * std::sin(inner), faint);
        }
        // the axis, a short way out
        Renderer2D::DrawLine(apex, apex + d * std::min(range, 1.0f), color);
    }

    // A 3D sound's directional cone (AudioConeSettings, full angles in degrees) out to `length`:
    // the outer cone faint, the inner one solid, an arrow along the way it faces. A cone wider
    // than 180 degrees opens backwards (its rim is behind the source).
    static void DrawAudioCone(const glm::vec3& apex, const glm::vec3& direction, float length, const AudioConeSettings& cone,
                              const glm::vec4& color)
    {
        if (!(length > 0.0f) || !(glm::length(direction) > 1e-6f))
            return;
        glm::vec3 d = glm::normalize(direction), u, v;
        PerpendicularAxes(d, u, v);
        auto drawCone = [&](float fullDegrees, const glm::vec4& c) {
            const float half = glm::radians(std::clamp(fullDegrees, 0.0f, 360.0f) * 0.5f);
            if (half >= glm::pi<float>() - 1e-3f)
                return; // all around: no cone to draw
            const glm::vec3 rimCenter = apex + d * (length * std::cos(half));
            const float rimRadius = length * std::sin(half);
            DrawWireCircle(rimCenter, u, v, rimRadius, c);
            for (glm::vec3 side : { u, -u, v, -v })
                Renderer2D::DrawLine(apex, rimCenter + side * rimRadius, c);
            // The cap on the sphere of `length`.
            DrawWireArc(apex, d, u, length, -half, half, c);
            DrawWireArc(apex, d, v, length, -half, half, c);
        };
        drawCone(cone.OuterAngle, glm::vec4(color.r, color.g, color.b, color.a * 0.4f));
        drawCone(cone.InnerAngle, color);
        DrawWireArrow(apex, apex + d * length * 1.15f, length * 0.18f, color);
    }

    // A Particle System's emitter shape in its local space (`world`: the emitter's transform):
    // circle / sphere / box outlines (the inner edge too when only an outer shell emits), the cone
    // with its rim rays out to its length, the edge line, the mesh's edges, and the emission direction.
    // A Spline component's path in the world: the curve, a small cross at each point (the first one
    // a ring), an arrow half way along showing the way it runs; Bezier handles of a selected path.
    static void DrawSplineGizmo(const glm::mat4& world, const SplineComponent& c, const glm::vec4& color, const EditorOverlayView& view,
                                bool selected)
    {
        const Spline& path = c.Path;
        const int segments = path.GetSegmentCount();
        auto toWorld = [&](const glm::vec3& p) { return glm::vec3(world * glm::vec4(p, 1.0f)); };
        if (segments > 0)
        {
            const int steps = path.Type == SplineType::Linear ? 1 : 24;
            glm::vec3 previous = toWorld(path.EvaluateSegment(0, 0.0f));
            for (int s = 0; s < segments; s++)
                for (int i = 1; i <= steps; i++)
                {
                    const glm::vec3 p = toWorld(path.EvaluateSegment(s, (float)i / (float)steps));
                    Renderer2D::DrawLine(previous, p, color);
                    previous = p;
                }
            // The way it runs: a chevron half way along.
            const float length = c.GetLength();
            const glm::vec3 mid = toWorld(c.GetPositionAtDistance(length * 0.5f));
            glm::vec3 along = glm::mat3(world) * c.GetTangentAtDistance(length * 0.5f);
            if (glm::dot(along, along) > 1e-12f)
            {
                along = glm::normalize(along);
                glm::vec3 side = glm::cross(along, view.Forward);
                side = glm::dot(side, side) > 1e-8f ? glm::normalize(side) : glm::vec3(0.0f, 1.0f, 0.0f);
                const float size = view.PixelSize(mid) * 9.0f;
                Renderer2D::DrawLine(mid, mid - along * size + side * size * 0.6f, color);
                Renderer2D::DrawLine(mid, mid - along * size - side * size * 0.6f, color);
            }
        }
        // The points (crosses facing the camera); the first a ring.
        glm::vec3 right = glm::cross(view.Forward, glm::vec3(0.0f, 1.0f, 0.0f));
        right = glm::dot(right, right) > 1e-8f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 up = glm::normalize(glm::cross(right, view.Forward));
        for (size_t i = 0; i < path.Points.size(); i++)
        {
            const glm::vec3 p = toWorld(path.Points[i].Position);
            const float size = view.PixelSize(p) * 4.0f;
            if (i == 0)
                DrawWireCircle(p, right, up, size * 1.4f, color);
            Renderer2D::DrawLine(p - right * size, p + right * size, color);
            Renderer2D::DrawLine(p - up * size, p + up * size, color);
            if (selected && path.Type == SplineType::Bezier)
            {
                glm::vec4 faint = color;
                faint.a *= 0.55f;
                Renderer2D::DrawLine(p, toWorld(path.Points[i].Position + path.Points[i].InTangent), faint);
                Renderer2D::DrawLine(p, toWorld(path.Points[i].Position + path.Points[i].OutTangent), faint);
            }
        }
    }

    static void DrawParticleShapeGizmo(const glm::mat4& world, const ParticleSystemComponent& ps, const glm::vec4& color, float pixel)
    {
        using Shape = ParticleSystemComponent::EmitterShape;
        auto toWorld = [&](const glm::vec3& p) { return glm::vec3(world * glm::vec4(p, 1.0f)); };
        auto line = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec4& c) { Renderer2D::DrawLine(toWorld(a), toWorld(b), c); };
        auto circle = [&](const glm::vec3& center, const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec4& c) {
            const int segments = 40;
            glm::vec3 prev = center + a * radius;
            for (int i = 1; i <= segments; i++)
            {
                const float t = glm::two_pi<float>() * i / segments;
                const glm::vec3 p = center + (a * std::cos(t) + b * std::sin(t)) * radius;
                line(prev, p, c);
                prev = p;
            }
        };
        const glm::vec4 faint(color.r, color.g, color.b, color.a * 0.45f);
        const Shape shape = ps.ShapeEnabled ? ps.Shape : Shape::Point; // the module off: from the centre
        const float inner = 1.0f - std::clamp(ps.ShapeThickness, 0.0f, 1.0f);
        const glm::vec3 axis = glm::length(ps.Direction) > 0.0f ? glm::normalize(ps.Direction) : glm::vec3(0, 1, 0);
        // About 60 px long on screen (in the emitter's units: its scale counts).
        const float scale = std::max(1e-6f, glm::length(glm::vec3(world * glm::vec4(axis, 0.0f))));
        const float arrow = 60.0f * pixel / scale;
        switch (shape)
        {
            case Shape::Circle:
                circle(glm::vec3(0.0f), { 1, 0, 0 }, { 0, 1, 0 }, ps.ShapeRadius, color);
                if (inner > 0.0f)
                    circle(glm::vec3(0.0f), { 1, 0, 0 }, { 0, 1, 0 }, ps.ShapeRadius * inner, faint);
                break;
            case Shape::Sphere:
                circle(glm::vec3(0.0f), { 1, 0, 0 }, { 0, 1, 0 }, ps.ShapeRadius, color);
                circle(glm::vec3(0.0f), { 1, 0, 0 }, { 0, 0, 1 }, ps.ShapeRadius, color);
                circle(glm::vec3(0.0f), { 0, 1, 0 }, { 0, 0, 1 }, ps.ShapeRadius, color);
                if (inner > 0.0f)
                    circle(glm::vec3(0.0f), { 1, 0, 0 }, { 0, 1, 0 }, ps.ShapeRadius * inner, faint);
                break;
            case Shape::Box:
                DrawWireBox(world * glm::scale(glm::mat4(1.0f), ps.ShapeExtents * 2.0f), color);
                break;
            case Shape::Edge:
                line({ -ps.EdgeLength * 0.5f, 0.0f, 0.0f }, { ps.EdgeLength * 0.5f, 0.0f, 0.0f }, color);
                for (float x : { -0.5f, 0.5f })
                    line({ ps.EdgeLength * x, -arrow * 0.08f, 0.0f }, { ps.EdgeLength * x, arrow * 0.08f, 0.0f }, color);
                break;
            case Shape::Cone:
            {
                glm::vec3 u, v;
                if (ps.ConeFlat)
                {
                    const glm::vec3 side(axis.y, -axis.x, 0.0f);
                    u = glm::length(side) > 1e-6f ? glm::normalize(side) : glm::vec3(1, 0, 0);
                    v = glm::vec3(0.0f);
                }
                else
                    PerpendicularAxes(axis, u, v);
                const float tanAngle = std::tan(glm::radians(std::clamp(ps.ConeAngle, 0.0f, 89.0f)));
                const float length = ps.ConeFrom == ParticleSystemComponent::ConeEmitFrom::Volume ? std::max(ps.ConeLength, 1e-3f)
                                                                                                    : std::max(arrow, ps.ShapeRadius);
                const float far = ps.ShapeRadius + length * tanAngle;
                const glm::vec4& farColor = ps.ConeFrom == ParticleSystemComponent::ConeEmitFrom::Volume ? color : faint;
                if (ps.ConeFlat)
                {
                    line(-u * ps.ShapeRadius, u * ps.ShapeRadius, color);
                    line(axis * length - u * far, axis * length + u * far, farColor);
                    for (float side : { -1.0f, 1.0f })
                        line(u * (ps.ShapeRadius * side), axis * length + u * (far * side), color);
                }
                else
                {
                    circle(glm::vec3(0.0f), u, v, ps.ShapeRadius, color);
                    if (inner > 0.0f)
                        circle(glm::vec3(0.0f), u, v, ps.ShapeRadius * inner, faint);
                    circle(axis * length, u, v, far, farColor);
                    for (glm::vec3 side : { u, -u, v, -v })
                        line(side * ps.ShapeRadius, axis * length + side * far, color);
                }
                break;
            }
            case Shape::Mesh:
                if (const ParticleEmitterMesh* mesh = ps.MeshRuntime.get(); mesh && !mesh->IsEmpty())
                {
                    // The edges Edge mode emits from (each once, not inside flat faces); a big mesh: some.
                    const size_t edges = mesh->EdgeSums.size();
                    const size_t step = std::max<size_t>(1, edges / 6000);
                    for (size_t e = 0; e < edges; e += step)
                    {
                        const float before = e == 0 ? 0.0f : mesh->EdgeSums[e - 1];
                        if (mesh->EdgeSums[e] <= before)
                            continue;
                        const size_t t = e / 3;
                        line(mesh->Positions[mesh->Indices[t * 3 + e % 3]], mesh->Positions[mesh->Indices[t * 3 + (e + 1) % 3]], faint);
                    }
                }
                break;
            default: break;
        }
        if (shape == Shape::Mesh)
            return; // the normals give the directions
        // The direction (the cone's axis).
        const glm::vec3 from = toWorld(glm::vec3(0.0f)), to = toWorld(axis * arrow);
        DrawWireArrow(from, to, glm::length(to - from) * 0.2f, faint);
    }

    // A 2D light in the XY plane: the circle it reaches, fainter the inner (full brightness) one;
    // a spot light: its cone's edges and the arc where it ends.
    // fn(std::type_identity<T>{}) for each 2D joint component type.
    template<typename... T, typename Fn>
    static void ForEachJoint2DType(ComponentGroup<T...>, Fn&& fn)
    {
        (fn(std::type_identity<T>{}), ...);
    }

    template<typename Fn>
    static void ForEachJoint2DType(Fn&& fn)
    {
        ForEachJoint2DType(Joint2DComponents{}, std::forward<Fn>(fn));
    }

    template<typename Fn>
    static void ForEachJoint3DType(Fn&& fn)
    {
        ForEachJoint2DType(Joint3DComponents{}, std::forward<Fn>(fn));
    }

    // fn(joint, type, component) for each 2D joint component the entity has.
    template<typename Fn>
    static void ForEachJointComponent(Entity entity, Fn&& fn)
    {
        if (auto* c = entity.TryGetComponent<DistanceJoint2DComponent>()) fn(*c, Joint2DType::Distance, (const void*)c);
        if (auto* c = entity.TryGetComponent<RevoluteJoint2DComponent>()) fn(*c, Joint2DType::Revolute, (const void*)c);
        if (auto* c = entity.TryGetComponent<PrismaticJoint2DComponent>()) fn(*c, Joint2DType::Prismatic, (const void*)c);
        if (auto* c = entity.TryGetComponent<WeldJoint2DComponent>()) fn(*c, Joint2DType::Weld, (const void*)c);
        if (auto* c = entity.TryGetComponent<WheelJoint2DComponent>()) fn(*c, Joint2DType::Wheel, (const void*)c);
    }

    // The 2D joints of a selected entity: its anchor (circle), the connected anchor (diamond) and
    // what the joint allows - a revolute's angle range (the arc the body's centre sweeps), the slide
    // axis of a prismatic / the suspension of a wheel with their limits, a distance's rod. While the
    // game runs the drawing follows the joint's reference pose (angle / translation from the start).
    static void DrawJoints2D(Scene& scene, Entity entity, const glm::vec4& color)
    {
        glm::vec3 p, r, sc;
        Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
        const float z = p.z;
        auto rotate = [](const glm::vec2& v, float angle) {
            float c = std::cos(angle), s = std::sin(angle);
            return glm::vec2(c * v.x - s * v.y, s * v.x + c * v.y);
        };
        const glm::vec4 faint(color.r, color.g, color.b, color.a * 0.4f);
        const glm::vec4 connectedColor(0.75f, 0.55f, 1.0f, color.a);
        const glm::vec4 limitColor(1.0f, 0.85f, 0.3f, color.a);
        auto at = [&](const glm::vec2& v) { return glm::vec3(v, z); };
        auto diamond = [&](const glm::vec2& c, float size, const glm::vec4& col) {
            glm::vec2 corners[4] = { c + glm::vec2(size, 0), c + glm::vec2(0, size), c - glm::vec2(size, 0), c - glm::vec2(0, size) };
            for (int i = 0; i < 4; i++)
                Renderer2D::DrawLine(at(corners[i]), at(corners[(i + 1) % 4]), col);
        };
        auto arc = [&](const glm::vec2& center, float radius, float from, float to, const glm::vec4& col) {
            const int segments = std::max(4, (int)(std::abs(to - from) / glm::radians(6.0f)));
            glm::vec2 previous = center + glm::vec2(std::cos(from), std::sin(from)) * radius;
            Renderer2D::DrawLine(at(center), at(previous), col);
            for (int i = 1; i <= segments; i++)
            {
                float a = from + (to - from) * (float)i / (float)segments;
                glm::vec2 q = center + glm::vec2(std::cos(a), std::sin(a)) * radius;
                Renderer2D::DrawLine(at(previous), at(q), col);
                previous = q;
            }
            Renderer2D::DrawLine(at(center), at(previous), col);
        };
        // A line along `dir` through `base` from `lower` to `upper` (ticks at the ends).
        auto axisLine = [&](const glm::vec2& base, const glm::vec2& dir, float lower, float upper, bool limited, const glm::vec4& col) {
            glm::vec2 side(-dir.y, dir.x);
            Renderer2D::DrawLine(at(base + dir * lower), at(base + dir * upper), limited ? col : faint);
            if (limited)
                for (float t : { lower, upper })
                    Renderer2D::DrawLine(at(base + dir * t - side * 0.12f), at(base + dir * t + side * 0.12f), col);
        };

        ForEachJointComponent(entity, [&](const Joint2DComponentBase& joint, Joint2DType type, const void* component) {
            const glm::vec2 origin(p);
            const glm::vec2 anchor = origin + rotate(joint.Anchor, r.z);
            // The connected anchor: in the connected body's frame, else a world point; auto-configured
            // ones are where the anchor is until the joint is made.
            Entity other = joint.ConnectedBody ? scene.GetEntityByUUID(joint.ConnectedBody.ID) : Entity();
            glm::vec2 connected = anchor;
            glm::vec2 otherOrigin = anchor;
            if (other && other != entity)
            {
                glm::vec3 op, orot, osc;
                Math::DecomposeTransform(scene.GetWorldTransform(other), op, orot, osc);
                otherOrigin = glm::vec2(op);
                if (!joint.AutoConfigureConnectedAnchor || joint.RuntimeJoint)
                    connected = otherOrigin + rotate(joint.ConnectedAnchor, orot.z);
            }
            else if (!joint.AutoConfigureConnectedAnchor || joint.RuntimeJoint)
                connected = joint.ConnectedAnchor;

            if (joint.Broken)
            {
                // A broken joint holds nothing: only where it was attached, crossed out in red.
                const glm::vec4 red(0.95f, 0.35f, 0.3f, color.a);
                Renderer2D::DrawCircle(glm::translate(glm::mat4(1.0f), at(anchor)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.16f, 0.16f, 1.0f)), red, 0.2f);
                Renderer2D::DrawLine(at(anchor + glm::vec2(-0.12f, -0.12f)), at(anchor + glm::vec2(0.12f, 0.12f)), red);
                Renderer2D::DrawLine(at(anchor + glm::vec2(-0.12f, 0.12f)), at(anchor + glm::vec2(0.12f, -0.12f)), red);
                return;
            }
            const glm::vec4 col = color;
            Renderer2D::DrawLine(at(origin), at(anchor), faint);
            if (other && other != entity)
                Renderer2D::DrawLine(at(otherOrigin), at(connected), glm::vec4(connectedColor.r, connectedColor.g, connectedColor.b, 0.4f));
            Renderer2D::DrawCircle(glm::translate(glm::mat4(1.0f), at(anchor)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.16f, 0.16f, 1.0f)), col, 0.2f);
            diamond(connected, 0.11f, connectedColor);

            switch (type)
            {
                case Joint2DType::Distance:
                {
                    const auto& d = *(const DistanceJoint2DComponent*)component;
                    // The rod / rope from the connected anchor, as long as the joint keeps it.
                    float length = d.AutoConfigureDistance && !d.RuntimeJoint ? glm::length(anchor - connected) : d.Distance;
                    glm::vec2 dir = glm::length(anchor - connected) > 1e-4f ? glm::normalize(anchor - connected) : glm::vec2(0.0f, -1.0f);
                    Renderer2D::DrawLine(at(connected), at(connected + dir * length), col);
                    if (d.MaxDistanceOnly)
                        arc(connected, length, std::atan2(dir.y, dir.x) - 0.35f, std::atan2(dir.y, dir.x) + 0.35f, faint);
                    break;
                }
                case Joint2DType::Revolute:
                {
                    const auto& j = *(const RevoluteJoint2DComponent*)component;
                    glm::vec2 arm = origin - anchor;
                    float radius = glm::length(arm) > 0.1f ? glm::length(arm) : 0.5f;
                    float base = glm::length(arm) > 0.1f ? std::atan2(arm.y, arm.x) : r.z;
                    if (j.RuntimeJoint)
                        base -= glm::radians(j.JointAngle); // from the pose it was made in
                    if (j.EnableLimit)
                        arc(anchor, radius, base + glm::radians(j.LowerAngle), base + glm::radians(j.UpperAngle), limitColor);
                    else
                        DrawWireCircle(at(anchor), { 1, 0, 0 }, { 0, 1, 0 }, radius, faint);
                    break;
                }
                case Joint2DType::Prismatic:
                case Joint2DType::Wheel:
                {
                    glm::vec2 axis;
                    bool limited;
                    float lower, upper, translation;
                    if (type == Joint2DType::Prismatic)
                    {
                        const auto& j = *(const PrismaticJoint2DComponent*)component;
                        axis = j.Axis; limited = j.EnableLimit; lower = j.LowerTranslation; upper = j.UpperTranslation;
                        translation = j.RuntimeJoint ? j.JointTranslation : 0.0f;
                    }
                    else
                    {
                        const auto& j = *(const WheelJoint2DComponent*)component;
                        axis = j.Axis; limited = j.EnableLimit; lower = j.LowerTranslation; upper = j.UpperTranslation;
                        translation = j.RuntimeJoint ? j.JointTranslation : 0.0f;
                        DrawWireCircle(at(anchor), { 1, 0, 0 }, { 0, 1, 0 }, 0.3f, faint); // the wheel turns freely
                    }
                    glm::vec2 dir = glm::length(axis) > 1e-5f ? glm::normalize(rotate(axis, r.z)) : glm::vec2(1.0f, 0.0f);
                    glm::vec2 base = anchor - dir * translation;
                    if (!limited)
                    {
                        lower = type == Joint2DType::Wheel ? -0.5f : -2.0f;
                        upper = -lower;
                    }
                    axisLine(base, dir, lower, upper, limited, limited ? limitColor : col);
                    Renderer2D::DrawLine(at(anchor), at(anchor + dir * 0.35f), col); // which way is +
                    break;
                }
                case Joint2DType::Weld:
                {
                    Renderer2D::DrawLine(at(anchor + glm::vec2(-0.12f, -0.12f)), at(anchor + glm::vec2(0.12f, 0.12f)), col);
                    Renderer2D::DrawLine(at(anchor + glm::vec2(-0.12f, 0.12f)), at(anchor + glm::vec2(0.12f, -0.12f)), col);
                    break;
                }
            }
        });
    }

    // fn(joint, type, component) for each 3D joint component the entity has.
    template<typename Fn>
    static void ForEachJoint3DComponent(Entity entity, Fn&& fn)
    {
        if (auto* c = entity.TryGetComponent<FixedJoint3DComponent>()) fn(*c, Joint3DType::Fixed, (const void*)c);
        if (auto* c = entity.TryGetComponent<HingeJoint3DComponent>()) fn(*c, Joint3DType::Hinge, (const void*)c);
        if (auto* c = entity.TryGetComponent<DistanceJoint3DComponent>()) fn(*c, Joint3DType::Distance, (const void*)c);
        if (auto* c = entity.TryGetComponent<SliderJoint3DComponent>()) fn(*c, Joint3DType::Slider, (const void*)c);
        if (auto* c = entity.TryGetComponent<ConeJoint3DComponent>()) fn(*c, Joint3DType::Cone, (const void*)c);
    }

    // The 3D joints of an entity: its anchor (a small ball), the connected anchor (an octahedron) and
    // what the joint allows - a hinge's axis and angle range (the arc the body's centre sweeps, around
    // the axis), a slider's axis with its limits, a distance joint's rod with its shortest length, a cone
    // joint's swing cone (around where its axis started) with the axis now and the twist range. While
    // the game runs the running joint's own frame is drawn (anchors on the bodies, the reference pose).
    static void DrawJoints3D(Scene& scene, Entity entity, const glm::vec4& color, float pixel)
    {
        const glm::mat4 world = scene.GetWorldTransform(entity);
        glm::vec3 p, r, sc;
        Math::DecomposeTransform(world, p, r, sc);
        const glm::quat q(r);
        const float size = std::clamp(pixel * 7.0f, 0.03f, 0.25f); // markers about 7 pixels big
        const glm::vec4 faint(color.r, color.g, color.b, color.a * 0.4f);
        const glm::vec4 connectedColor(0.75f, 0.55f, 1.0f, color.a);
        const glm::vec4 limitColor(1.0f, 0.85f, 0.3f, color.a);
        auto ball = [&](const glm::vec3& c, const glm::vec4& col) {
            DrawWireCircle(c, { 1, 0, 0 }, { 0, 1, 0 }, size, col);
            DrawWireCircle(c, { 1, 0, 0 }, { 0, 0, 1 }, size, col);
            DrawWireCircle(c, { 0, 1, 0 }, { 0, 0, 1 }, size, col);
        };
        auto octahedron = [&](const glm::vec3& c, const glm::vec4& col) {
            const glm::vec3 axes[3] = { { size, 0, 0 }, { 0, size, 0 }, { 0, 0, size } };
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    if (i != j)
                        for (float si : { -1.0f, 1.0f })
                            for (float sj : { -1.0f, 1.0f })
                                if (i < j)
                                    Renderer2D::DrawLine(c + axes[i] * si, c + axes[j] * sj, col);
        };
        PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();

        ForEachJoint3DComponent(entity, [&](const Joint3DComponentBase& joint, Joint3DType type, const void* component) {
            glm::vec3 anchor = glm::vec3(world * glm::vec4(joint.Anchor, 1.0f));
            Entity other = joint.ConnectedBody ? scene.GetEntityByUUID(joint.ConnectedBody.ID) : Entity();
            if (other == entity)
                other = {};
            // The connected anchor: in the connected body's frame, else a world point; auto-configured
            // ones are where the anchor is until the joint is made.
            glm::vec3 connected = anchor;
            if (!joint.AutoConfigureConnectedAnchor || joint.RuntimeJoint)
                connected = other ? glm::vec3(scene.GetWorldTransform(other) * glm::vec4(joint.ConnectedAnchor, 1.0f)) : joint.ConnectedAnchor;
            glm::vec3 axis(0.0f, 1.0f, 0.0f);
            if (type == Joint3DType::Hinge)
                axis = ((const HingeJoint3DComponent*)component)->Axis;
            else if (type == Joint3DType::Slider)
                axis = ((const SliderJoint3DComponent*)component)->Axis;
            else if (type == Joint3DType::Cone)
                axis = ((const ConeJoint3DComponent*)component)->Axis;
            axis = glm::length(axis) > 1e-5f ? glm::normalize(q * axis) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 startAxis = axis; // a cone's middle: where the axis was when the joint was made
            if (joint.RuntimeJoint && physics)
            {
                glm::vec3 a, b, ax, ax1;
                if (physics->GetJointFrame(joint.RuntimeJoint, a, b, ax, &ax1))
                {
                    anchor = a;
                    connected = b;
                    axis = glm::length(ax) > 1e-5f ? glm::normalize(ax) : axis;
                    startAxis = glm::length(ax1) > 1e-5f ? glm::normalize(ax1) : axis;
                }
            }

            if (joint.Broken)
            {
                // A broken joint holds nothing: only where it was attached, crossed out in red.
                const glm::vec4 red(0.95f, 0.35f, 0.3f, color.a);
                ball(anchor, red);
                for (glm::vec3 d : { glm::vec3(1, 1, 0), glm::vec3(1, -1, 0), glm::vec3(0, 1, 1), glm::vec3(0, 1, -1) })
                    Renderer2D::DrawLine(anchor - d * size * 1.4f, anchor + d * size * 1.4f, red);
                return;
            }
            Renderer2D::DrawLine(p, anchor, faint);
            if (other)
                Renderer2D::DrawLine(scene.GetWorldPosition(other), connected, glm::vec4(connectedColor.r, connectedColor.g, connectedColor.b, 0.4f));
            ball(anchor, color);
            octahedron(connected, connectedColor);
            // A frame at right angles to the axis.
            const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 side = glm::normalize(glm::cross(axis, helper)), up = glm::cross(side, axis);

            switch (type)
            {
                case Joint3DType::Fixed:
                {
                    for (glm::vec3 d : { glm::vec3(1, 1, 0), glm::vec3(1, -1, 0), glm::vec3(0, 1, 1), glm::vec3(0, 1, -1) })
                        Renderer2D::DrawLine(anchor - d * size * 1.6f, anchor + d * size * 1.6f, color);
                    break;
                }
                case Joint3DType::Hinge:
                {
                    const auto& j = *(const HingeJoint3DComponent*)component;
                    // The axis through the anchor, and the arc the body's centre may sweep around it.
                    const float axisLength = std::max(0.4f, size * 8.0f);
                    Renderer2D::DrawLine(anchor - axis * axisLength, anchor + axis * axisLength, color);
                    glm::vec3 arm = p - anchor;
                    arm -= axis * glm::dot(arm, axis);
                    const float radius = glm::length(arm) > 0.05f ? glm::length(arm) : 0.5f;
                    glm::vec3 a = glm::length(arm) > 0.05f ? arm / glm::length(arm) : side;
                    if (j.RuntimeJoint)
                        a = glm::angleAxis(glm::radians(-j.JointAngle), axis) * a; // from the pose it was made in
                    const glm::vec3 b = glm::cross(axis, a);
                    if (j.EnableLimit)
                    {
                        const float lo = glm::radians(std::min(j.LowerAngle, j.UpperAngle)), hi = glm::radians(std::max(j.LowerAngle, j.UpperAngle));
                        DrawWireArc(anchor, a, b, radius, lo, hi, limitColor, std::max(4, (int)((hi - lo) / glm::radians(6.0f))));
                        Renderer2D::DrawLine(anchor, anchor + (a * std::cos(lo) + b * std::sin(lo)) * radius, limitColor);
                        Renderer2D::DrawLine(anchor, anchor + (a * std::cos(hi) + b * std::sin(hi)) * radius, limitColor);
                    }
                    else
                        DrawWireCircle(anchor, a, b, radius, faint);
                    if (j.EnableMotor && j.MotorSpeed != 0.0f)
                    {
                        // Which way the motor turns: a short arrow along the circle.
                        const float dir = j.MotorSpeed > 0.0f ? 1.0f : -1.0f, t = 0.6f * dir;
                        const glm::vec3 tip = anchor + (a * std::cos(t) + b * std::sin(t)) * radius * 0.6f;
                        DrawWireArc(anchor, a, b, radius * 0.6f, 0.0f, t, color, 8);
                        const glm::vec3 tangent = (-a * std::sin(t) + b * std::cos(t)) * dir;
                        Renderer2D::DrawLine(tip, tip - tangent * size * 2.0f + (anchor - tip) * 0.15f, color);
                    }
                    break;
                }
                case Joint3DType::Slider:
                {
                    const auto& j = *(const SliderJoint3DComponent*)component;
                    const glm::vec3 base = anchor - axis * (j.RuntimeJoint ? j.JointTranslation : 0.0f);
                    float lower = j.LowerTranslation, upper = j.UpperTranslation;
                    if (!j.EnableLimit)
                    {
                        lower = -2.0f;
                        upper = 2.0f;
                    }
                    Renderer2D::DrawLine(base + axis * lower, base + axis * upper, j.EnableLimit ? limitColor : faint);
                    if (j.EnableLimit)
                        for (float t : { lower, upper })
                        {
                            Renderer2D::DrawLine(base + axis * t - side * size * 1.5f, base + axis * t + side * size * 1.5f, limitColor);
                            Renderer2D::DrawLine(base + axis * t - up * size * 1.5f, base + axis * t + up * size * 1.5f, limitColor);
                        }
                    Renderer2D::DrawLine(anchor, anchor + axis * std::max(0.35f, size * 6.0f), color); // which way is +
                    break;
                }
                case Joint3DType::Cone:
                {
                    const auto& j = *(const ConeJoint3DComponent*)component;
                    // The cone the axis may swing in (its rim on a sphere around the anchor), the axis
                    // now and the twist range around it.
                    const float length = std::max(0.45f, size * 9.0f);
                    const glm::vec3 helper2 = std::abs(startAxis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    const glm::vec3 u = glm::normalize(glm::cross(startAxis, helper2)), v = glm::cross(u, startAxis);
                    const float swing = glm::radians(std::clamp(j.SwingLimit, 0.0f, 180.0f));
                    if (swing < glm::radians(179.0f))
                    {
                        const glm::vec3 rim = anchor + startAxis * (length * std::cos(swing));
                        DrawWireCircle(rim, u, v, length * std::sin(swing), limitColor);
                        for (int i = 0; i < 4; i++)
                        {
                            const float t = glm::half_pi<float>() * (float)i;
                            const glm::vec3 side2 = u * std::cos(t) + v * std::sin(t);
                            Renderer2D::DrawLine(anchor, anchor + (startAxis * std::cos(swing) + side2 * std::sin(swing)) * length, limitColor);
                        }
                    }
                    else
                        DrawWireCircle(anchor, u, v, length, faint); // free: any direction
                    Renderer2D::DrawLine(anchor, anchor + startAxis * length, faint);
                    Renderer2D::DrawLine(anchor, anchor + axis * length * 1.15f, color);
                    // The twist range: an arc around the axis, from the reference side.
                    const glm::vec3 h3 = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    glm::vec3 a = glm::normalize(glm::cross(axis, h3));
                    if (j.RuntimeJoint)
                        a = glm::angleAxis(glm::radians(-j.TwistAngle), axis) * a;
                    const glm::vec3 b = glm::cross(axis, a);
                    const glm::vec3 at = anchor + axis * length * 0.55f;
                    const float lo = glm::radians(std::min(j.TwistLower, 0.0f)), hi = glm::radians(std::max(j.TwistUpper, 0.0f));
                    const float radius = length * 0.3f;
                    DrawWireArc(at, a, b, radius, lo, hi, limitColor, std::max(4, (int)((hi - lo) / glm::radians(8.0f))));
                    Renderer2D::DrawLine(at, at + (a * std::cos(lo) + b * std::sin(lo)) * radius, limitColor);
                    Renderer2D::DrawLine(at, at + (a * std::cos(hi) + b * std::sin(hi)) * radius, limitColor);
                    if (j.RuntimeJoint)
                        Renderer2D::DrawLine(at, at + a * radius * 1.2f, color); // the twist now (0 = the reference side)
                    break;
                }
                case Joint3DType::Distance:
                {
                    const auto& d = *(const DistanceJoint3DComponent*)component;
                    const glm::vec3 between = anchor - connected;
                    const float now = glm::length(between);
                    const glm::vec3 dir = now > 1e-4f ? between / now : glm::vec3(0.0f, -1.0f, 0.0f);
                    const float maxLength = d.AutoConfigureDistance && !d.RuntimeJoint ? now : d.MaxDistance;
                    const float minLength = d.MaxDistanceOnly ? 0.0f : (d.AutoConfigureDistance && !d.RuntimeJoint ? now : d.MinDistance);
                    // The rod / rope from the connected anchor as long as the joint lets it be; the
                    // shortest length is marked with a ring. The rings lie at right angles to the rod
                    // (their frame from the rod itself: the joint's axis may lie along it).
                    Renderer2D::DrawLine(connected, connected + dir * maxLength, color);
                    const glm::vec3 rodHelper = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    const glm::vec3 ringU = glm::normalize(glm::cross(dir, rodHelper)), ringV = glm::cross(dir, ringU);
                    if (minLength > 1e-3f && minLength < maxLength - 1e-3f)
                        DrawWireCircle(connected + dir * minLength, ringU, ringV, size * 1.2f, limitColor);
                    DrawWireCircle(connected + dir * maxLength, ringU, ringV, size * 1.5f, d.MaxDistanceOnly ? faint : color);
                    break;
                }
            }
        });
    }

    static void DrawLight2DRange(const glm::vec3& center, const glm::vec2& axis, const Light2DComponent& light, const glm::vec4& color)
    {
        if (light.Radius <= 0.0f) return;
        const glm::vec3 u(1.0f, 0.0f, 0.0f), v(0.0f, 1.0f, 0.0f);
        const glm::vec4 faint(color.r, color.g, color.b, color.a * 0.45f);
        if (light.Type == Light2DType::Spot && light.SpotAngle < 360.0f)
        {
            float len = glm::length(axis);
            glm::vec2 d = len > 1e-6f ? axis / len : glm::vec2(1.0f, 0.0f);
            float base = std::atan2(d.y, d.x);
            float half = glm::radians(std::clamp(light.SpotAngle, 1.0f, 360.0f) * 0.5f);
            const int segments = 32;
            glm::vec3 previous = center;
            for (int i = 0; i <= segments; i++)
            {
                float a = base - half + 2.0f * half * (float)i / (float)segments;
                glm::vec3 p = center + glm::vec3(std::cos(a), std::sin(a), 0.0f) * light.Radius;
                if (i > 0)
                    Renderer2D::DrawLine(previous, p, color);
                if (i == 0 || i == segments)
                    Renderer2D::DrawLine(center, p, color);
                previous = p;
            }
            Renderer2D::DrawLine(center, center + glm::vec3(d, 0.0f) * std::min(light.Radius, 1.0f), color);
            if (light.InnerRadius > 0.0f && light.InnerRadius < light.Radius)
                for (int i = 0; i < segments; i++)
                {
                    float a0 = base - half + 2.0f * half * (float)i / (float)segments, a1 = base - half + 2.0f * half * (float)(i + 1) / (float)segments;
                    Renderer2D::DrawLine(center + glm::vec3(std::cos(a0), std::sin(a0), 0.0f) * light.InnerRadius,
                                         center + glm::vec3(std::cos(a1), std::sin(a1), 0.0f) * light.InnerRadius, faint);
                }
            return;
        }
        DrawWireCircle(center, u, v, light.Radius, color);
        if (light.InnerRadius > 0.0f && light.InnerRadius < light.Radius)
            DrawWireCircle(center, u, v, light.InnerRadius, faint);
    }

    bool EditorLayer::ShowsGizmo(entt::entity e, GizmoVisibility visibility, const std::unordered_set<entt::entity>& hidden)
    {
        // A selected entity gets its visualisation unless the eye hides it (outside play mode);
        // with "all", every other entity the overlay draws (active, not hidden) too.
        if (visibility == GizmoVisibility::Off)
            return false;
        Scene& scene = *m_ActiveScene;
        Entity entity{ e, &scene };
        bool selected = m_SceneHierarchyPanel.IsSelected(entity);
        if (!EditorGizmoSettings::Shows(visibility, selected))
            return false;
        return selected ? !(m_SceneState != SceneState::Play && scene.IsHiddenInEditor(entity)) : !hidden.contains(e);
    }

    void EditorLayer::DrawComponentGizmos(const EditorOverlayView& view, const std::unordered_set<entt::entity>& hidden)
    {
        const EditorGizmoSettings& gizmos = m_ViewSettings.Gizmos;
        Scene& scene = *m_ActiveScene;
        auto shows = [&](entt::entity e, GizmoVisibility visibility) { return ShowsGizmo(e, visibility, hidden); };
        auto tint = [&](entt::entity e, glm::vec4 color) {
            if (!m_SceneHierarchyPanel.IsSelected({ e, &scene }))
                color.a *= 0.6f;
            return color;
        };

        // Camera frustums (perspective: a pyramid to the far plane, orthographic: a box). Not the
        // one the game is seen through while playing.
        if (gizmos.CameraFrustums != GizmoVisibility::Off)
        {
            Entity viewing = m_SceneState == SceneState::Play ? scene.GetPrimaryCameraEntity() : Entity();
            for (auto e : scene.GetAllEntitiesWith<CameraComponent>())
            {
                if ((viewing && e == (entt::entity)viewing) || !shows(e, gizmos.CameraFrustums))
                    continue;
                Entity entity{ e, &scene };
                const auto& camera = entity.GetComponent<CameraComponent>();
                glm::mat4 world = scene.GetWorldTransform(entity);
                // The transform's scale does not change what the camera sees.
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(world, p, r, sc);
                glm::mat4 rigid = glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(glm::quat(r));
                const bool perspective = camera.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective;
                // (A pixel-perfect camera: what it shows at the viewport's size.)
                DrawCameraFrustum(FrustumCorners(scene.GetCameraProjection(entity), rigid), rigid, perspective, CameraFrustumPreviewDistance,
                                  tint(e, camera.Primary ? glm::vec4(0.9f, 0.92f, 1.0f, 1.0f) : glm::vec4(0.7f, 0.8f, 0.95f, 1.0f)));
            }
        }

        // Light directions: the sun's rays and arrow, an arrow along a spot light's axis (no
        // longer than its range). In the light's own colour, brightened.
        if (gizmos.LightDirections != GizmoVisibility::Off)
        {
            auto lightColor = [&](entt::entity e, const glm::vec3& color) {
                float m = std::max({ color.r, color.g, color.b, 1e-4f });
                glm::vec3 c = glm::mix(glm::vec3(1.0f, 0.88f, 0.4f), color / m, 0.5f);
                return tint(e, glm::vec4(c, 1.0f));
            };
            for (auto e : scene.GetAllEntitiesWith<DirectionalLightComponent>())
            {
                if (!shows(e, gizmos.LightDirections))
                    continue;
                Entity entity{ e, &scene };
                glm::mat4 world = scene.GetWorldTransform(entity);
                glm::vec3 origin(world[3]);
                DrawDirectionalLightGizmo(origin, glm::vec3(world * glm::vec4(0, 0, -1, 0)), view.PixelSize(origin),
                                          lightColor(e, entity.GetComponent<DirectionalLightComponent>().Color));
            }
            for (auto e : scene.GetAllEntitiesWith<SpotLightComponent>())
            {
                if (!shows(e, gizmos.LightDirections))
                    continue;
                Entity entity{ e, &scene };
                const auto& sl = entity.GetComponent<SpotLightComponent>();
                glm::mat4 world = scene.GetWorldTransform(entity);
                glm::vec3 apex(world[3]), dir(world * glm::vec4(0, 0, -1, 0));
                if (!(glm::length(dir) > 1e-6f) || !(sl.Range > 0.0f))
                    continue;
                float pixel = view.PixelSize(apex);
                float length = std::min(90.0f * pixel, sl.Range);
                DrawWireArrow(apex, apex + glm::normalize(dir) * length, std::min(18.0f * pixel, length * 0.4f), lightColor(e, sl.Color));
            }
        }

        // 3D audio sources: full volume inside the inner sphere (MinDistance), fading out to the
        // outer one (MaxDistance), silent beyond it.
        if (gizmos.AudioRanges != GizmoVisibility::Off)
            for (auto e : scene.GetAllEntitiesWith<AudioSourceComponent>())
            {
                Entity entity{ e, &scene };
                const auto& src = entity.GetComponent<AudioSourceComponent>();
                if (!src.Spatial || !shows(e, gizmos.AudioRanges))
                    continue;
                glm::vec3 center = scene.GetWorldPosition(entity);
                const glm::vec4 color = tint(e, glm::vec4(0.88f, 0.55f, 0.9f, 1.0f));
                DrawWireSphere(center, src.MinDistance, color, &view);
                if (src.MaxDistance > src.MinDistance)
                    DrawWireSphere(center, src.MaxDistance, glm::vec4(color.r, color.g, color.b, color.a * 0.45f), &view);
                // The directional cone: out to the inner sphere, at least ~80 pixels long.
                if (src.Cone.IsActive())
                {
                    const glm::vec3 forward(scene.GetWorldTransform(entity) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
                    const float length = std::min(std::max(src.MinDistance, 80.0f * view.PixelSize(center)), std::max(src.MaxDistance, 0.1f));
                    DrawAudioCone(center, forward, length, src.Cone, tint(e, glm::vec4(1.0f, 0.7f, 0.35f, 1.0f)));
                }
            }

        // Ambient zones: the volume (full inside) and, fainter, where the blend outside ends.
        if (gizmos.AudioRanges != GizmoVisibility::Off)
            for (auto e : scene.GetAllEntitiesWith<AmbientZoneComponent>())
            {
                if (!shows(e, gizmos.AudioRanges))
                    continue;
                Entity entity{ e, &scene };
                const auto& zone = entity.GetComponent<AmbientZoneComponent>();
                const glm::mat4 world = scene.GetWorldTransform(entity);
                const glm::vec3 center(world[3]);
                // While playing: brighter with the level heard.
                const float lit = m_SceneState == SceneState::Play ? zone.Level : 0.0f;
                const glm::vec4 color = tint(e, glm::mix(glm::vec4(0.45f, 0.85f, 0.55f, 0.85f), glm::vec4(0.6f, 1.0f, 0.7f, 1.0f), lit));
                const glm::vec4 faint(color.r, color.g, color.b, color.a * 0.35f);
                if (zone.Shape == AmbientZoneComponent::ZoneShape::Sphere)
                {
                    const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
                    const float radius = std::max(0.0f, zone.Radius) * scale;
                    DrawWireSphere(center, radius, color, &view);
                    if (zone.BlendDistance > 0.0f)
                        DrawWireSphere(center, radius + zone.BlendDistance, faint, &view);
                    continue;
                }
                // The box along the entity's turned axes; the blend shell grows each face outwards.
                glm::mat4 box(1.0f), shell(1.0f);
                for (int i = 0; i < 3; i++)
                {
                    const glm::vec3 axis(world[i]);
                    const float length = glm::length(axis);
                    const glm::vec3 unit = length > 1e-6f ? axis / length : glm::vec3(i == 0, i == 1, i == 2);
                    const float size = std::max(0.0f, zone.Size[i]) * length;
                    box[i] = glm::vec4(unit * size, 0.0f);
                    shell[i] = glm::vec4(unit * (size + 2.0f * std::max(0.0f, zone.BlendDistance)), 0.0f);
                }
                box[3] = shell[3] = glm::vec4(center, 1.0f);
                DrawWireBox(box, color);
                if (zone.BlendDistance > 0.0f)
                    DrawWireBox(shell, faint);
            }

        // Spline paths; a selected follower: where it starts on its path (a dashed line to it).
        if (gizmos.Splines != GizmoVisibility::Off)
        {
            for (auto e : scene.GetAllEntitiesWith<SplineComponent>())
            {
                if (!shows(e, gizmos.Splines))
                    continue;
                Entity entity{ e, &scene };
                const auto& c = entity.GetComponent<SplineComponent>();
                const bool selected = m_SceneHierarchyPanel.IsSelected(entity);
                glm::vec4 color = c.Color;
                if (!selected)
                    color.a *= 0.6f;
                DrawSplineGizmo(scene.GetWorldTransform(entity), c, color, view, selected);
            }
            if (m_SceneState != SceneState::Play)
                for (auto e : scene.GetAllEntitiesWith<SplineFollowerComponent>())
                {
                    Entity entity{ e, &scene };
                    if (!m_SceneHierarchyPanel.IsSelected(entity) || !shows(e, gizmos.Splines))
                        continue;
                    const auto& f = entity.GetComponent<SplineFollowerComponent>();
                    Entity pathEntity = scene.GetSplineFollowerPath(entity);
                    if (!pathEntity || pathEntity == entity)
                        continue;
                    const glm::vec3 start = scene.GetSplinePosition(pathEntity, f.StartPosition * scene.GetSplineLength(pathEntity));
                    const glm::vec3 from = scene.GetWorldPosition(entity);
                    const glm::vec4 color(1.0f, 0.8f, 0.3f, 0.9f);
                    const float size = view.PixelSize(start) * 6.0f;
                    glm::vec3 right = glm::cross(view.Forward, glm::vec3(0.0f, 1.0f, 0.0f));
                    right = glm::dot(right, right) > 1e-8f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
                    const glm::vec3 up = glm::normalize(glm::cross(right, view.Forward));
                    // A diamond at the start, dashes from the follower to it.
                    Renderer2D::DrawLine(start + up * size, start + right * size, color);
                    Renderer2D::DrawLine(start + right * size, start - up * size, color);
                    Renderer2D::DrawLine(start - up * size, start - right * size, color);
                    Renderer2D::DrawLine(start - right * size, start + up * size, color);
                    const float gap = glm::length(start - from);
                    if (gap > size)
                    {
                        const int dashes = std::clamp((int)(gap / (size * 1.5f)), 1, 64);
                        for (int i = 0; i < dashes; i += 2)
                            Renderer2D::DrawLine(glm::mix(from, start, (float)i / dashes), glm::mix(from, start, (float)(i + 1) / dashes), color);
                    }
                }
        }

        // Particle System emitters: the shape they emit from.
        if (gizmos.ParticleShapes != GizmoVisibility::Off)
            for (auto e : scene.GetAllEntitiesWith<ParticleSystemComponent>())
            {
                if (!shows(e, gizmos.ParticleShapes))
                    continue;
                Entity entity{ e, &scene };
                const auto& ps = entity.GetComponent<ParticleSystemComponent>();
                const glm::mat4 world = scene.GetParticleEmitterTransform(entity, ps); // weather: at the camera
                DrawParticleShapeGizmo(world, ps, tint(e, glm::vec4(0.55f, 0.75f, 1.0f, 0.9f)), view.PixelSize(glm::vec3(world[3])));
                // Weather: the box the particles wrap around in (world axes; the flat ones as a rectangle).
                if (ps.FollowCamera.Enabled && ps.FollowActive && ps.FollowCamera.Wrap)
                {
                    const glm::vec3 extents = glm::max(ps.FollowCamera.WrapExtents, glm::vec3(0.0f));
                    if (extents.x > 0.0f || extents.y > 0.0f || extents.z > 0.0f)
                    {
                        glm::mat4 box = glm::scale(glm::translate(glm::mat4(1.0f), ps.FollowPosition), extents * 2.0f);
                        DrawWireBox(box, tint(e, glm::vec4(0.55f, 0.75f, 1.0f, 0.3f)));
                    }
                }
                // The collision plane: a grid under the emitter (where it meets the plane) and its normal.
                if (ps.Collision.Enabled && ps.Collision.Type == ParticleSystemComponent::CollisionModule::CollisionType::Plane)
                {
                    glm::vec3 normal = ps.Collision.PlaneNormal;
                    float offset = ps.Collision.PlaneOffset;
                    if (Entity planeEntity = ps.Collision.PlaneEntity ? scene.GetEntityByUUID(ps.Collision.PlaneEntity.ID) : Entity())
                    {
                        const glm::mat4 planeWorld = scene.GetWorldTransform(planeEntity);
                        normal = glm::vec3(planeWorld[1]);
                        offset = glm::length(normal) > 1e-6f ? glm::dot(glm::normalize(normal), glm::vec3(planeWorld[3])) : 0.0f;
                    }
                    else if (glm::length(normal) > 1e-6f)
                        offset /= glm::length(normal);
                    normal = glm::length(normal) > 1e-6f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
                    const glm::vec3 emitter(world[3]);
                    const glm::vec3 center = emitter - normal * (glm::dot(normal, emitter) - offset);
                    glm::vec3 u, v;
                    PerpendicularAxes(normal, u, v);
                    const glm::vec4 color = tint(e, glm::vec4(0.95f, 0.65f, 0.3f, 0.75f));
                    const float half = std::max(1.0f, 120.0f * view.PixelSize(center));
                    for (int i = -4; i <= 4; i++)
                    {
                        const float f = half * i / 4.0f;
                        const glm::vec4 c = i == 0 || std::abs(i) == 4 ? color : glm::vec4(color.r, color.g, color.b, color.a * 0.4f);
                        Renderer2D::DrawLine(center + u * f - v * half, center + u * f + v * half, c);
                        Renderer2D::DrawLine(center + v * f - u * half, center + v * f + u * half, c);
                    }
                    DrawWireArrow(center, center + normal * half * 0.5f, half * 0.1f, color);
                }
            }

        // 2D and 3D joints: anchors, connected anchors and limits (one setting for both).
        if (gizmos.Joints2D != GizmoVisibility::Off)
        {
            std::unordered_set<entt::entity> drawn;
            ForEachJoint2DType([&](auto type) {
                using T = typename decltype(type)::type;
                for (auto e : scene.GetAllEntitiesWith<T>())
                    if (shows(e, gizmos.Joints2D) && drawn.insert(e).second)
                        DrawJoints2D(scene, { e, &scene }, tint(e, glm::vec4(0.35f, 0.9f, 1.0f, 1.0f)));
            });
            drawn.clear();
            ForEachJoint3DType([&](auto type) {
                using T = typename decltype(type)::type;
                for (auto e : scene.GetAllEntitiesWith<T>())
                    if (shows(e, gizmos.Joints2D) && drawn.insert(e).second)
                    {
                        Entity entity{ e, &scene };
                        DrawJoints3D(scene, entity, tint(e, glm::vec4(0.35f, 0.9f, 1.0f, 1.0f)), view.PixelSize(scene.GetWorldPosition(entity)));
                    }
            });
        }

        DrawBoundingBoxes(hidden);
        DrawVertexNormals(view, hidden);
        DrawSkeletons(view, hidden);
    }

    void EditorLayer::DrawBoundingBoxes(const std::unordered_set<entt::entity>& hidden)
    {
        const EditorGizmoSettings& gizmos = m_ViewSettings.Gizmos;
        if (gizmos.Bounds == GizmoVisibility::Off)
            return;
        Scene& scene = *m_ActiveScene;
        const bool local = gizmos.BoundsMode != BoundsSpace::World, world = gizmos.BoundsMode != BoundsSpace::Local;
        auto box = [](const glm::vec3& min, const glm::vec3& max) {
            return glm::translate(glm::mat4(1.0f), (min + max) * 0.5f) * glm::scale(glm::mat4(1.0f), glm::max(max - min, glm::vec3(1e-4f)));
        };
        for (auto e : scene.GetAllEntitiesWith<TransformComponent>())
        {
            if (!ShowsGizmo(e, gizmos.Bounds, hidden))
                continue;
            Entity entity{ e, &scene };
            AABB bounds;
            if (!scene.GetEntityLocalBounds(entity, bounds))
                continue; // lights, cameras, empty groups: nothing drawn, no box
            const float alpha = m_SceneHierarchyPanel.IsSelected(entity) ? 1.0f : 0.55f;
            const glm::mat4 transform = scene.GetWorldTransform(entity);
            // Local: the box in the entity's own space, turned and scaled with it.
            if (local)
                DrawWireBox(transform * box(bounds.Min, bounds.Max), glm::vec4(0.35f, 0.82f, 1.0f, alpha));
            if (!world)
                continue;
            // World: axis aligned. A mesh's is the one frustum culling tests (a skinned model's
            // grows with its pose), the others' the corners of the local box.
            AABB worldBounds;
            bool any = false;
            if (auto* mr = entity.TryGetComponent<MeshRendererComponent>())
            {
                auto add = [&](const AABB& b) {
                    worldBounds.Min = any ? glm::min(worldBounds.Min, b.Min) : b.Min;
                    worldBounds.Max = any ? glm::max(worldBounds.Max, b.Max) : b.Max;
                    any = true;
                };
                if (mr->Type == PrimitiveMesh::Model && mr->ModelAsset)
                {
                    const auto* anim = entity.TryGetComponent<AnimatorComponent>();
                    const auto& subs = mr->ModelAsset->GetSubMeshes();
                    std::vector<glm::mat4> palette;
                    for (size_t s = 0; s < subs.size(); s++)
                        if (subs[s].MeshRef)
                            add(Renderer3D::GetWorldBounds(*subs[s].MeshRef, transform,
                                                           subs[s].MeshRef->IsSkinned() && anim && !anim->JointMatrices.empty()
                                                               ? &mr->ModelAsset->GetSubMeshJoints(s, anim->JointMatrices, palette) : nullptr));
                }
                else if (mr->MeshAsset)
                    add(Renderer3D::GetWorldBounds(*mr->MeshAsset, transform));
            }
            if (!any && !scene.GetEntityBounds(entity, worldBounds))
                continue;
            DrawWireBox(box(worldBounds.Min, worldBounds.Max), glm::vec4(0.78f, 1.0f, 0.42f, alpha * 0.9f));
        }
    }

    // A bone from `head` (the parent joint) to `tail` (the joint): a thin double pyramid, widest
    // a fifth of the way along (`pixel`: world units of a viewport pixel at the head).
    static void DrawBone(const glm::vec3& head, const glm::vec3& tail, float pixel, const glm::vec4& color)
    {
        glm::vec3 d = tail - head;
        float length = glm::length(d);
        if (!(length > 1e-6f))
            return;
        d /= length;
        glm::vec3 u, v;
        PerpendicularAxes(d, u, v);
        const float width = std::clamp(length * 0.1f, pixel * 2.0f, std::max(pixel * 2.0f, length * 0.25f));
        const glm::vec3 waist = head + d * (length * 0.2f);
        const glm::vec3 ring[4] = { waist + u * width, waist + v * width, waist - u * width, waist - v * width };
        for (int i = 0; i < 4; i++)
        {
            Renderer2D::DrawLine(head, ring[i], color);
            Renderer2D::DrawLine(ring[i], tail, color);
            Renderer2D::DrawLine(ring[i], ring[(i + 1) % 4], color);
        }
    }

    // A round dot that faces the camera, `diameter` world units across.
    static void DrawScreenDot(const glm::vec3& center, float diameter, const glm::vec4& color, const EditorOverlayView& view)
    {
        glm::mat4 facing = glm::mat4(glm::transpose(glm::mat3(view.View))); // the camera's rotation
        facing[3] = glm::vec4(center, 1.0f);
        Renderer2D::DrawCircle(facing * glm::scale(glm::mat4(1.0f), glm::vec3(diameter)), color, 1.0f, 0.08f);
    }

    void EditorLayer::DrawSkeletons(const EditorOverlayView& view, const std::unordered_set<entt::entity>& hidden)
    {
        m_JointLabels.clear();
        const EditorGizmoSettings& gizmos = m_ViewSettings.Gizmos;
        if (gizmos.Skeletons == GizmoVisibility::Off)
            return;
        Scene& scene = *m_ActiveScene;
        const glm::mat4 viewProjection = view.Projection * view.View;
        const glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
        static const std::vector<glm::mat4> s_RestPose;
        std::vector<glm::mat4> globals;
        std::vector<glm::vec3> joints;
        for (auto e : scene.GetAllEntitiesWith<MeshRendererComponent>())
        {
            Entity entity{ e, &scene };
            const auto& mr = entity.GetComponent<MeshRendererComponent>();
            if (mr.Type != PrimitiveMesh::Model || !mr.ModelAsset || !mr.ModelAsset->IsSkinned() || !ShowsGizmo(e, gizmos.Skeletons, hidden))
                continue;
            // The pose the model was drawn in: its animator's, else the rest pose.
            const Skeleton& skeleton = mr.ModelAsset->GetSkeleton();
            const auto* anim = entity.TryGetComponent<AnimatorComponent>();
            GetJointTransforms(skeleton, anim ? anim->JointMatrices : s_RestPose, globals);
            const glm::mat4 world = scene.GetWorldTransform(entity);
            joints.resize(globals.size());
            for (size_t i = 0; i < globals.size(); i++)
                joints[i] = glm::vec3(world * globals[i][3]);

            const bool selected = m_SceneHierarchyPanel.IsSelected(entity);
            const float alpha = selected ? 1.0f : 0.55f;
            const glm::vec4 boneColor(0.55f, 0.86f, 1.0f, alpha), jointColor(1.0f, 0.82f, 0.32f, alpha), shadow(0.05f, 0.06f, 0.08f, 0.8f * alpha);
            for (size_t i = 0; i < joints.size(); i++)
            {
                const int parent = skeleton.Joints[i].Parent;
                if (parent >= 0 && (size_t)parent < joints.size())
                    DrawBone(joints[parent], joints[i], view.PixelSize(joints[parent]), boneColor);
            }
            for (size_t i = 0; i < joints.size(); i++)
            {
                const float pixel = view.PixelSize(joints[i]);
                DrawScreenDot(joints[i], pixel * 9.0f, shadow, view);
                DrawScreenDot(joints[i], pixel * 6.0f, jointColor, view);
                if (!gizmos.JointNames)
                    continue;
                glm::vec4 clip = viewProjection * glm::vec4(joints[i], 1.0f);
                if (clip.w <= 0.0f)
                    continue;
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f)
                    continue;
                m_JointLabels.push_back({ { (ndc.x * 0.5f + 0.5f) * viewportSize.x, (0.5f - ndc.y * 0.5f) * viewportSize.y },
                                          skeleton.Joints[i].Name, selected });
            }
        }
    }

    // The most vertex normals drawn in a frame (all entities together): a mesh with more vertices
    // than are left gets an even sample of them.
    static constexpr size_t MaxVertexNormalLines = 60000;

    void EditorLayer::DrawVertexNormals(const EditorOverlayView& view, const std::unordered_set<entt::entity>& hidden)
    {
        m_NormalLinesDrawn = 0;
        m_NormalLinesLimited = false;
        const EditorGizmoSettings& gizmos = m_ViewSettings.Gizmos;
        if (gizmos.Normals == GizmoVisibility::Off)
            return;
        Scene& scene = *m_ActiveScene;
        NormalLineOptions options;
        options.Length = gizmos.NormalLength;
        options.MaxLines = MaxVertexNormalLines;
        options.FacingOnly = gizmos.NormalsFacingOnly;
        options.Orthographic = !view.IsPerspective();
        options.CameraPosition = view.Position;
        options.CameraForward = view.Forward;
        std::vector<NormalLine> lines;
        std::vector<glm::mat4> bindPose, skinPalette;
        size_t vertices = 0;
        for (auto e : scene.GetAllEntitiesWith<MeshRendererComponent>())
        {
            if (!ShowsGizmo(e, gizmos.Normals, hidden))
                continue;
            Entity entity{ e, &scene };
            const auto& mr = entity.GetComponent<MeshRendererComponent>();
            const glm::mat4 world = scene.GetWorldTransform(entity);
            const size_t first = lines.size();
            if (mr.Type == PrimitiveMesh::Model)
            {
                if (!mr.ModelAsset)
                    continue;
                // The pose the model was drawn in: its animator's, else the bind pose (as the scene draws it).
                const std::vector<glm::mat4>* joints = nullptr;
                if (mr.ModelAsset->IsSkinned())
                {
                    const auto* anim = entity.TryGetComponent<AnimatorComponent>();
                    if (anim && !anim->JointMatrices.empty())
                        joints = &anim->JointMatrices;
                    else
                    {
                        EvaluatePose(mr.ModelAsset->GetSkeleton(), nullptr, 0.0f, bindPose);
                        joints = &bindPose;
                    }
                }
                const auto& subs = mr.ModelAsset->GetSubMeshes();
                for (size_t s = 0; s < subs.size(); s++)
                    if (subs[s].MeshRef)
                    {
                        const MeshGeometry& geometry = subs[s].MeshRef->GetGeometry();
                        vertices += geometry.GetVertexCount();
                        AppendNormalLines(geometry, world,
                                          subs[s].MeshRef->IsSkinned() && joints ? &mr.ModelAsset->GetSubMeshJoints(s, *joints, skinPalette) : nullptr,
                                          options, lines);
                    }
            }
            else
            {
                Ref<Mesh> mesh = mr.MeshAsset ? mr.MeshAsset : MeshLibrary::GetPrimitive(mr.Type);
                if (!mesh)
                    continue;
                const MeshGeometry& geometry = mesh->GetGeometry();
                vertices += geometry.GetVertexCount();
                AppendNormalLines(geometry, world, nullptr, options, lines);
            }
            // Selected: full colour; the others ("all") fainter.
            const float alpha = m_SceneHierarchyPanel.IsSelected(entity) ? 0.95f : 0.5f;
            for (size_t i = first; i < lines.size(); i++)
            {
                const NormalLine& l = lines[i];
                const glm::vec3 rgb = gizmos.NormalsByDirection ? l.Normal * 0.5f + 0.5f : glm::vec3(0.35f, 0.62f, 1.0f);
                Renderer2D::DrawLine(l.From, l.To, glm::vec4(rgb, alpha));
            }
        }
        m_NormalLinesDrawn = lines.size();
        m_NormalLinesLimited = vertices > MaxVertexNormalLines; // some meshes got a sample of their vertices
    }

    void EditorLayer::OnOverlayRender()
    {
        // What the gizmos found last frame (joint names over the viewport, the normals' count) does
        // not stay when nothing is drawn now (play mode without a camera).
        m_JointLabels.clear();
        m_NormalLinesDrawn = 0;
        m_NormalLinesLimited = false;
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
        EditorOverlayView overlayView;
        if (OverlayCamera(overlayView.View, overlayView.Projection))
        {
            glm::mat4 world = glm::inverse(overlayView.View);
            overlayView.Position = glm::vec3(world[3]);
            overlayView.Forward = glm::normalize(-glm::vec3(world[2]));
        }
        overlayView.ViewportHeight = m_ViewportSize.y; // the final picture's pixels (what the overlay draws into)

        DrawComponentGizmos(overlayView, hidden);

        if (m_ShowColliders)
        {
            const glm::vec4 green(0.3f, 0.95f, 0.45f, 1.0f), yellow(0.95f, 0.8f, 0.2f, 1.0f);
            // While the game runs, the colliders of sleeping 2D bodies are blue-grey (as in Box2D's testbed).
            const glm::vec4 sleepColor(0.5f, 0.6f, 0.95f, 0.85f);
            auto solid2D = [&](Entity entity, const glm::vec4& color) {
                return scene.IsSleeping2D(entity) ? sleepColor : color;
            };
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
                const auto* effector = entity.TryGetComponent<PlatformEffector2DComponent>();
                if (!bc.IsSensor && (effector ? effector->OneWay : bc.OneWay))
                {
                    // One-way: the top edge solid, the rest faint, an arrow up through the middle (magenta).
                    const glm::vec4 oneWay(0.95f, 0.4f, 0.9f, 1.0f);
                    auto at = [&](float x, float y) { return glm::vec3(transform * glm::vec4(x, y, 0.0f, 1.0f)); };
                    Renderer2D::DrawRect(transform, glm::vec4(oneWay.r, oneWay.g, oneWay.b, 0.45f));
                    Renderer2D::DrawLine(at(-0.5f, 0.5f), at(0.5f, 0.5f), oneWay);
                    Renderer2D::DrawLine(at(0.0f, -0.3f), at(0.0f, 0.35f), oneWay);
                    Renderer2D::DrawLine(at(0.0f, 0.35f), at(-0.08f, 0.15f), oneWay);
                    Renderer2D::DrawLine(at(0.0f, 0.35f), at(0.08f, 0.15f), oneWay);
                    continue;
                }
                Renderer2D::DrawRect(transform, bc.IsSensor ? yellow : solid2D(entity, green));
            }
            // Tilemaps: the boxes their static body is made of (one-way ones magenta, with arrows up).
            const glm::vec4 oneWayColor(0.95f, 0.4f, 0.9f, 1.0f);
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, TilemapComponent>())
            {
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                const auto& tm = entity.GetComponent<TilemapComponent>();
                if (!tm.Collision || (size_t)tm.Width * tm.Height > (1u << 18))
                    continue;
                const glm::mat4 world = scene.GetWorldTransform(entity);
                const TilesetProperties* properties = TilesetProperties::Get(tm.TilesetPath);
                const auto* effector = entity.TryGetComponent<PlatformEffector2DComponent>();
                for (const TileColliderBox& box : TilemapCollision::BuildBoxes(tm, properties))
                {
                    glm::vec2 size((box.X1 - box.X0) * tm.TileSize, tm.TileSize);
                    glm::vec2 center(box.X0 * tm.TileSize + size.x * 0.5f, box.Y * tm.TileSize + size.y * 0.5f);
                    glm::mat4 t = world * glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.001f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
                    if (effector ? !effector->OneWay : box.Type != TileCollision::OneWay)
                    {
                        Renderer2D::DrawRect(t, green);
                        continue;
                    }
                    Renderer2D::DrawRect(t, glm::vec4(oneWayColor.r, oneWayColor.g, oneWayColor.b, 0.45f));
                    glm::vec3 left = world * glm::vec4(box.X0 * tm.TileSize, (box.Y + 1) * tm.TileSize, 0.001f, 1.0f);
                    glm::vec3 right = world * glm::vec4(box.X1 * tm.TileSize, (box.Y + 1) * tm.TileSize, 0.001f, 1.0f);
                    Renderer2D::DrawLine(left, right, oneWayColor);
                    for (int x = box.X0; x < box.X1; x++)
                    {
                        auto at = [&](float fx, float fy) { return glm::vec3(world * glm::vec4((x + fx) * tm.TileSize, (box.Y + fy) * tm.TileSize, 0.001f, 1.0f)); };
                        Renderer2D::DrawLine(at(0.5f, 0.25f), at(0.5f, 0.8f), oneWayColor);
                        Renderer2D::DrawLine(at(0.5f, 0.8f), at(0.35f, 0.62f), oneWayColor);
                        Renderer2D::DrawLine(at(0.5f, 0.8f), at(0.65f, 0.62f), oneWayColor);
                    }
                }
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
                Renderer2D::DrawCircle(transform, cc.IsSensor ? yellow : solid2D(entity, green), 0.04f);
            }
            // Polygons (their convex pieces faint inside the outline) and edges / chains (a one-sided
            // chain's solid side marked with short ticks). Platforms (Platform Effector 2D) magenta.
            const glm::vec4 platformColor(0.95f, 0.4f, 0.9f, 1.0f);
            auto colliderFrame = [&](Entity entity, const glm::vec2& offset) {
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                const glm::vec2 s(std::abs(sc.x), std::abs(sc.y));
                const float c = std::cos(r.z), sn = std::sin(r.z);
                return [p, s, c, sn, offset](const glm::vec2& q, bool scaled) {
                    glm::vec2 l = scaled ? offset + q * s : q; // pieces are already scaled (and offset)
                    return glm::vec3(p.x + c * l.x - sn * l.y, p.y + sn * l.x + c * l.y, p.z + 0.001f);
                };
            };
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, PolygonCollider2DComponent>())
            {
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                const auto& pc = entity.GetComponent<PolygonCollider2DComponent>();
                const glm::vec4 color = pc.IsSensor ? yellow : entity.HasComponent<PlatformEffector2DComponent>() ? platformColor : solid2D(entity, green);
                auto at = colliderFrame(entity, pc.Offset);
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                const Polygon2D::Decomposition d = Scene::DecomposePolygonCollider(pc, glm::abs(glm::vec2(sc)));
                if (d.Pieces.size() > 1 || d.Result == Polygon2D::DecomposeResult::SelfIntersecting)
                    for (const auto& piece : d.Pieces)
                        for (size_t i = 0; i < piece.size(); i++)
                            Renderer2D::DrawLine(at(piece[i], false), at(piece[(i + 1) % piece.size()], false),
                                                 glm::vec4(color.r, color.g, color.b, 0.3f));
                const glm::vec4 outline = d.Result == Polygon2D::DecomposeResult::Degenerate ? glm::vec4(0.95f, 0.35f, 0.3f, 1.0f) : color;
                for (size_t i = 0; i < pc.Points.size() && pc.Points.size() >= 2; i++)
                    Renderer2D::DrawLine(at(pc.Points[i], true), at(pc.Points[(i + 1) % pc.Points.size()], true), outline);
            }
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, EdgeCollider2DComponent>())
            {
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                const auto& ec = entity.GetComponent<EdgeCollider2DComponent>();
                const glm::vec4 color = ec.IsSensor ? yellow : entity.HasComponent<PlatformEffector2DComponent>() ? platformColor : solid2D(entity, green);
                auto at = colliderFrame(entity, ec.Offset);
                const size_t n = ec.Points.size();
                const size_t edges = n < 2 ? 0 : (ec.Loop && n >= 3 ? n : n - 1);
                for (size_t i = 0; i < edges; i++)
                {
                    const glm::vec3 a = at(ec.Points[i], true), b = at(ec.Points[(i + 1) % n], true);
                    Renderer2D::DrawLine(a, b, color);
                    if (!ec.OneSided)
                        continue;
                    // Ticks on the solid side (left of the direction), every ~0.25 units.
                    glm::vec2 dir = glm::vec2(b - a);
                    const float length = glm::length(dir);
                    if (length < 1e-4f)
                        continue;
                    dir /= length;
                    const glm::vec3 normal(-dir.y * 0.12f, dir.x * 0.12f, 0.0f);
                    const int ticks = std::max(1, (int)(length / 0.25f));
                    for (int t = 0; t < ticks; t++)
                    {
                        const glm::vec3 q = a + (b - a) * ((t + 0.5f) / (float)ticks);
                        Renderer2D::DrawLine(q, q + normal, glm::vec4(color.r, color.g, color.b, 0.6f));
                    }
                }
            }
            // Platform effectors of the selection: the surface arc (where bodies are held) from the centre.
            for (Entity entity : m_SceneHierarchyPanel.GetSelection().GetEntities())
            {
                if (!entity || !entity.IsValid() || entity.GetScene() != &scene || hidden.contains((entt::entity)entity))
                    continue;
                const auto* pe = entity.TryGetComponent<PlatformEffector2DComponent>();
                if (!pe)
                    continue;
                AABB bounds;
                float radius = 1.0f;
                if (scene.GetEntityBounds(entity, bounds))
                    radius = std::max(0.5f, glm::length(glm::vec2(bounds.Max - bounds.Min)) * 0.35f);
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                const float up = r.z + glm::radians(90.0f + pe->RotationOffset);
                const float half = glm::radians(std::clamp(pe->OneWay ? pe->SurfaceArc : 360.0f, 1.0f, 360.0f) * 0.5f);
                auto point = [&](float a, float rr) { return glm::vec3(p.x + std::cos(a) * rr, p.y + std::sin(a) * rr, p.z + 0.002f); };
                const int segments = std::max(6, (int)(half * 2.0f / glm::radians(6.0f)));
                glm::vec3 previous = point(up - half, radius);
                if (half < glm::pi<float>() - 1e-3f)
                    Renderer2D::DrawLine(point(0.0f, 0.0f), previous, platformColor);
                for (int i = 1; i <= segments; i++)
                {
                    glm::vec3 q = point(up - half + 2.0f * half * (float)i / (float)segments, radius);
                    Renderer2D::DrawLine(previous, q, platformColor);
                    previous = q;
                }
                if (half < glm::pi<float>() - 1e-3f)
                    Renderer2D::DrawLine(point(0.0f, 0.0f), previous, platformColor);
                // The surface's up.
                const glm::vec3 tip = point(up, radius * 1.15f);
                const glm::vec3 dir(std::cos(up), std::sin(up), 0.0f), side(-dir.y, dir.x, 0.0f);
                const float head = std::min(0.25f, radius * 0.2f);
                Renderer2D::DrawLine(point(0.0f, 0.0f), tip, platformColor);
                Renderer2D::DrawLine(tip, tip - dir * head + side * head * 0.6f, platformColor);
                Renderer2D::DrawLine(tip, tip - dir * head - side * head * 0.6f, platformColor);
            }

            // Area effectors: arrows along their force over the colliders' area. Buoyancy effectors: the
            // fluid below the surface (faint), the surface line and the flow. Brighter when selected.
            auto colliderBounds = [&](Entity entity, glm::vec2& lo, glm::vec2& hi) {
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                const glm::vec2 s(std::abs(sc.x), std::abs(sc.y));
                const float c = std::cos(r.z), sn = std::sin(r.z);
                lo = glm::vec2(FLT_MAX);
                hi = glm::vec2(-FLT_MAX);
                auto world = [&](const glm::vec2& l) { return glm::vec2(p.x + c * l.x - sn * l.y, p.y + sn * l.x + c * l.y); };
                auto add = [&](const glm::vec2& w, float pad) {
                    lo = glm::min(lo, w - glm::vec2(pad));
                    hi = glm::max(hi, w + glm::vec2(pad));
                };
                if (const auto* b = entity.TryGetComponent<BoxCollider2DComponent>())
                    for (glm::vec2 corner : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
                        add(world(b->Offset + corner * b->Size * s), 0.0f);
                if (const auto* cc = entity.TryGetComponent<CircleCollider2DComponent>())
                    add(world(cc->Offset), cc->Radius * s.x);
                if (const auto* pc = entity.TryGetComponent<PolygonCollider2DComponent>())
                    for (const glm::vec2& q : pc->Points)
                        add(world(pc->Offset + q * s), 0.0f);
                return lo.x <= hi.x && lo.y <= hi.y;
            };
            auto arrow2D = [](const glm::vec3& from, const glm::vec3& to, float head, const glm::vec4& color) {
                Renderer2D::DrawLine(from, to, color);
                glm::vec2 d(to - from);
                const float l = glm::length(d);
                if (l < 1e-4f)
                    return;
                d /= l;
                const glm::vec3 back(-d * head, 0.0f), side(-d.y * head * 0.55f, d.x * head * 0.55f, 0.0f);
                Renderer2D::DrawLine(to, to + back + side, color);
                Renderer2D::DrawLine(to, to + back - side, color);
            };
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, AreaEffector2DComponent>())
            {
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                const auto& ae = entity.GetComponent<AreaEffector2DComponent>();
                glm::vec2 lo, hi;
                if (!colliderBounds(entity, lo, hi) || ae.ForceMagnitude == 0.0f)
                    continue;
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                const float angle = glm::radians(ae.ForceAngle) + (ae.UseGlobalAngle ? 0.0f : r.z);
                const glm::vec2 dir = glm::vec2(std::cos(angle), std::sin(angle)) * (ae.ForceMagnitude < 0.0f ? -1.0f : 1.0f);
                const glm::vec2 size = hi - lo;
                const float spacing = std::clamp(std::min(size.x, size.y) * 0.5f, 0.6f, 2.5f);
                const int cols = std::clamp((int)(size.x / spacing), 1, 6), rows = std::clamp((int)(size.y / spacing), 1, 6);
                const float length = std::min(size.x / cols, size.y / rows) * 0.6f;
                const bool selected = m_SceneHierarchyPanel.IsSelected(entity);
                const glm::vec4 windColor(0.55f, 0.88f, 1.0f, selected ? 0.95f : 0.45f);
                for (int y = 0; y < rows; y++)
                    for (int x = 0; x < cols; x++)
                    {
                        const glm::vec2 center = lo + size * glm::vec2((x + 0.5f) / cols, (y + 0.5f) / rows);
                        arrow2D(glm::vec3(center - dir * length * 0.5f, p.z + 0.002f), glm::vec3(center + dir * length * 0.5f, p.z + 0.002f),
                                length * 0.3f, windColor);
                    }
            }
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, BuoyancyEffector2DComponent>())
            {
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                const auto& be = entity.GetComponent<BuoyancyEffector2DComponent>();
                glm::vec2 lo, hi;
                if (!colliderBounds(entity, lo, hi))
                    continue;
                const glm::vec3 p = scene.GetWorldPosition(entity);
                const float z = p.z + 0.002f;
                const bool selected = m_SceneHierarchyPanel.IsSelected(entity);
                const glm::vec4 waterColor(0.35f, 0.68f, 1.0f, selected ? 1.0f : 0.55f);
                const float surface = p.y + be.SurfaceLevel;
                const float top = std::min(surface, hi.y);
                if (top > lo.y)
                {
                    // The fluid (faint), the surface as a small wave when it is inside the colliders.
                    Renderer2D::DrawQuad(glm::translate(glm::mat4(1.0f), glm::vec3((lo.x + hi.x) * 0.5f, (lo.y + top) * 0.5f, z)) *
                                             glm::scale(glm::mat4(1.0f), glm::vec3(hi.x - lo.x, top - lo.y, 1.0f)),
                                         glm::vec4(0.25f, 0.55f, 1.0f, selected ? 0.16f : 0.08f));
                    const int segments = std::clamp((int)((hi.x - lo.x) / 0.25f), 4, 200);
                    glm::vec3 previous(lo.x, top, z);
                    for (int i = 1; i <= segments; i++)
                    {
                        const float t = (float)i / (float)segments;
                        const float wave = surface <= hi.y ? std::sin(t * segments * 1.5708f) * 0.05f : 0.0f;
                        const glm::vec3 q(lo.x + (hi.x - lo.x) * t, top + wave, z);
                        Renderer2D::DrawLine(previous, q, waterColor);
                        previous = q;
                    }
                }
                if (be.FlowMagnitude != 0.0f && top > lo.y)
                {
                    const float a = glm::radians(be.FlowAngle);
                    const glm::vec2 dir = glm::vec2(std::cos(a), std::sin(a)) * (be.FlowMagnitude < 0.0f ? -1.0f : 1.0f);
                    const float length = std::clamp((hi.x - lo.x) * 0.3f, 0.5f, 2.5f);
                    const glm::vec2 center((lo.x + hi.x) * 0.5f, (lo.y + top) * 0.5f);
                    arrow2D(glm::vec3(center - dir * length * 0.5f, z), glm::vec3(center + dir * length * 0.5f, z), length * 0.25f, waterColor);
                }
            }

            const glm::vec4 cyan(0.3f, 0.85f, 1.0f, 1.0f);
            // Triggers yellow; while the game runs, the colliders of sleeping 3D bodies blue-grey.
            auto color3D = [&](Entity entity, bool sensor) {
                return sensor ? yellow : scene.IsSleeping3D(entity) ? sleepColor : cyan;
            };
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, BoxCollider3DComponent>())
            {
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                auto& c = entity.GetComponent<BoxCollider3DComponent>();
                glm::mat4 t = scene.GetWorldTransform(entity) * glm::translate(glm::mat4(1.0f), c.Offset) * glm::scale(glm::mat4(1.0f), c.HalfExtents * 2.0f);
                DrawWireBox(t, color3D(entity, c.IsSensor));
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
                const glm::vec4 col = color3D(entity, c.IsSensor);
                DrawWireCircle(center, { 1, 0, 0 }, { 0, 1, 0 }, radius, col);
                DrawWireCircle(center, { 1, 0, 0 }, { 0, 0, 1 }, radius, col);
                DrawWireCircle(center, { 0, 1, 0 }, { 0, 0, 1 }, radius, col);
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
                glm::vec4 col = color3D(entity, c.IsSensor);
                DrawWireCircle(center + up * half, right, fwd, radius, col);
                DrawWireCircle(center - up * half, right, fwd, radius, col);
                for (glm::vec3 side : { right, -right, fwd, -fwd })
                    Renderer2D::DrawLine(center + up * half + side * radius, center - up * half + side * radius, col);
                DrawWireCircle(center + up * half, right, up, radius, col);
                DrawWireCircle(center - up * half, right, up, radius, col);
            }
            // Convex hulls: the edges of the hull the body gets (cached per model: cheap every frame).
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, ConvexHullCollider3DComponent>())
            {
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                Ref<const ConvexHull3D> hull = Scene::GetConvexHullCollider(entity);
                if (!hull) continue;
                auto& c = entity.GetComponent<ConvexHullCollider3DComponent>();
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                const glm::quat q(r);
                const glm::vec3 scale = glm::abs(sc), offset = c.Offset * scale;
                const glm::vec4 col = color3D(entity, c.IsSensor);
                for (const auto& [a, b] : hull->Edges)
                    Renderer2D::DrawLine(p + q * (offset + hull->Points[a] * scale), p + q * (offset + hull->Points[b] * scale), col);
            }
            // Mesh colliders: the edges of their triangles (flat insides left out; cached per mesh). Dimmed
            // on a dynamic body, which leaves the mesh out.
            for (auto e : scene.GetAllEntitiesWith<TransformComponent, MeshCollider3DComponent>())
            {
                if (hidden.contains(e)) continue;
                Entity entity{ e, &scene };
                Ref<const CollisionShapes3D::EdgeList> edges = CollisionShapes3D::GetFeatureEdges(Scene::GetMeshCollider(entity));
                if (!edges) continue;
                auto& c = entity.GetComponent<MeshCollider3DComponent>();
                const auto* rb = entity.TryGetComponent<Rigidbody3DComponent>();
                glm::vec3 p, r, sc;
                Math::DecomposeTransform(scene.GetWorldTransform(entity), p, r, sc);
                const glm::quat q(r);
                const glm::vec3 scale = glm::abs(sc), offset = c.Offset * scale;
                glm::vec4 col = color3D(entity, c.IsSensor);
                if (rb && rb->Type == Rigidbody3DComponent::BodyType::Dynamic)
                    col.a = 0.3f;
                for (const auto& [a, b] : *edges)
                    Renderer2D::DrawLine(p + q * (offset + a * scale), p + q * (offset + b * scale), col);
            }
        }

        // Camera 2D Follow of the selected entity: its dead zone around the view's centre (yellow)
        // and the world rectangle the view stays in (orange), on the target's plane.
        if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity(); selected && selected.IsValid() &&
            selected.HasComponent<Camera2DFollowComponent>() && !hidden.contains(selected))
        {
            const auto& follow = selected.GetComponent<Camera2DFollowComponent>();
            glm::vec3 center = scene.GetWorldPosition(selected);
            float z = 0.0f;
            if (Entity target = follow.Target ? scene.GetEntityByUUID(follow.Target.ID) : Entity(); target && target != selected)
                z = scene.GetWorldPosition(target).z;
            auto rect = [&](const glm::vec2& min, const glm::vec2& max, const glm::vec4& color) {
                glm::vec2 size = glm::max(glm::abs(max - min), glm::vec2(1e-3f));
                Renderer2D::DrawRect(glm::translate(glm::mat4(1.0f), glm::vec3((min + max) * 0.5f, z)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f)),
                                     color);
            };
            glm::vec2 half = follow.DeadZone * 0.5f;
            rect(glm::vec2(center) - half, glm::vec2(center) + half, glm::vec4(1.0f, 0.85f, 0.25f, 0.9f));
            if (follow.UseBounds)
                rect(glm::min(follow.BoundsMin, follow.BoundsMax), glm::max(follow.BoundsMin, follow.BoundsMax), glm::vec4(1.0f, 0.55f, 0.15f, 0.9f));
        }

        // Tilemap bounds + hovered cell
        if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity(); selected && selected.IsValid() && selected.HasComponent<TilemapComponent>() && !hidden.contains(selected))
        {
            auto& tm = selected.GetComponent<TilemapComponent>();
            glm::mat4 world = scene.GetWorldTransform(selected);
            glm::vec2 size = { tm.Width * tm.TileSize, tm.Height * tm.TileSize };
            Renderer2D::DrawRect(world * glm::translate(glm::mat4(1.0f), glm::vec3(size * 0.5f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f)),
                                 glm::vec4(0.3f, 0.85f, 1.0f, 0.8f));
            // The tool's preview: the cell (or the rectangle being dragged, the region a fill would
            // cover) with the tiles it would leave (an auto-tile rule's variants included); red: erased.
            glm::ivec2 hover;
            using Tool = SceneHierarchyPanel::TileTool;
            if (m_SceneHierarchyPanel.IsTilePainting() && (m_ViewportHovered || m_TileHoverOverride) && GetTileCellUnderMouse(selected, hover))
            {
                const Tool tool = m_TileStroke.Active ? m_TileStroke.Tool : m_SceneHierarchyPanel.GetTileTool();
                const bool erase = m_TileStroke.Active ? m_TileStroke.Erase
                                 : tool == Tool::Eraser || Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);
                const int layer = std::clamp(m_SceneHierarchyPanel.GetActiveTileLayer(), 0, tm.GetLayerCount() - 1);
                const AutoTileSet* rules = AutoTileSet::Get(tm.TilesetPath);
                const AutoTileRule* rule = rules ? rules->Get(m_SceneHierarchyPanel.GetSelectedAutoTileRule()) : nullptr;
                const glm::vec4 outline = erase ? glm::vec4(1.0f, 0.3f, 0.3f, 1.0f) : tool == Tool::Picker ? glm::vec4(1.0f, 0.85f, 0.25f, 1.0f)
                                        : rule ? glm::vec4(0.35f, 1.0f, 0.7f, 1.0f) : glm::vec4(1.0f);
                auto cellTransform = [&](glm::ivec2 c, glm::vec2 size = glm::vec2(1.0f)) {
                    return world * glm::translate(glm::mat4(1.0f), { (c.x + size.x * 0.5f) * tm.TileSize, (c.y + size.y * 0.5f) * tm.TileSize, 0.0f })
                                 * glm::scale(glm::mat4(1.0f), { tm.TileSize * size.x, tm.TileSize * size.y, 1.0f });
                };
                // The tiles the tool would leave are drawn by the scene view (m_TilePreview); here
                // the cells it would empty.
                if (m_TilePreview.Valid && m_TilePreview.Entity == selected.GetUUID())
                    for (const glm::ivec2& c : m_TilePreview.Erased)
                        Renderer2D::DrawQuad(cellTransform(c), glm::vec4(1.0f, 0.25f, 0.25f, 0.4f));
                if (m_TileStroke.Active && tool == Tool::Rectangle)
                {
                    std::vector<glm::ivec2> cells = TilemapEdit::RectCells(tm, m_TileStroke.Start, m_TileStroke.Last);
                    if (!cells.empty())
                    {
                        glm::ivec2 lo = cells.front(), hi = cells.back();
                        glm::vec2 size = glm::vec2(hi - lo) + 1.0f;
                        if (!m_TilePreview.Valid) // the tiles it would leave show what it covers otherwise
                            Renderer2D::DrawQuad(cellTransform(lo, size), glm::vec4(outline.r, outline.g, outline.b, 0.12f));
                        Renderer2D::DrawRect(cellTransform(lo, size), outline);
                    }
                }
                else if (tool == Tool::Fill && tm.Contains(hover.x, hover.y))
                {
                    std::vector<glm::ivec2> region = TilemapEdit::FloodRegion(tm, layer, hover.x, hover.y, rule, 4096);
                    for (const glm::ivec2& c : region)
                        Renderer2D::DrawQuad(cellTransform(c), glm::vec4(outline.r, outline.g, outline.b, m_TilePreview.Valid ? 0.06f : 0.15f));
                    Renderer2D::DrawRect(cellTransform(hover), outline);
                }
                else if (tool == Tool::Brush && !rule && !m_SceneHierarchyPanel.GetTileBrush().IsSingle())
                {
                    // The multi-tile brush's footprint around the cell.
                    const TileBrush& brush = m_SceneHierarchyPanel.GetTileBrush();
                    glm::ivec2 topLeft = brush.OriginFor(hover);
                    glm::ivec2 lo(topLeft.x, topLeft.y - brush.Height + 1);
                    Renderer2D::DrawRect(cellTransform(lo, glm::vec2((float)brush.Width, (float)brush.Height)), outline);
                }
                else if (tm.Contains(hover.x, hover.y))
                {
                    Renderer2D::DrawRect(cellTransform(hover), outline);
                }
            }
        }

        // The mesh a dragged material would go to (only the part when one is picked).
        if (m_MaterialDrag.Active && m_SceneState == SceneState::Edit && (uint64_t)m_MaterialDrag.Target)
            if (Entity target = scene.GetEntityByUUID(m_MaterialDrag.Target))
                if (auto* mr = target.TryGetComponent<MeshRendererComponent>(); mr && (mr->MeshAsset || mr->ModelAsset))
                {
                    const AABB* b = mr->ModelAsset ? &mr->ModelAsset->GetBounds() : &mr->MeshAsset->GetBounds();
                    if (mr->ModelAsset && m_MaterialDrag.Submesh >= 0 && (size_t)m_MaterialDrag.Submesh < mr->ModelAsset->GetSubMeshes().size())
                        b = &mr->ModelAsset->GetSubMeshes()[(size_t)m_MaterialDrag.Submesh].MeshRef->GetBounds();
                    glm::vec3 size = glm::max(b->Max - b->Min, glm::vec3(0.001f));
                    DrawWireBox(scene.GetWorldTransform(target) * glm::translate(glm::mat4(1.0f), (b->Min + b->Max) * 0.5f) *
                                glm::scale(glm::mat4(1.0f), size * 1.02f), glm::vec4(0.35f, 0.8f, 1.0f, 1.0f));
                }

        // The selection in the Box style: the primary entity orange, the rest of a multi-selection
        // yellow (meshes: their bounds, 2D shapes and texts: their rectangles) and lines to the
        // children. The Outline style is drawn by RenderSelectionOutline from what the entities
        // draw; a selected inactive entity draws nothing, so it gets a faint box here in both
        // styles (it shows where the switched-off entity is). One hidden with the eye gets none.
        const bool boxStyle = m_ViewSettings.SelectionOutline.Style == SelectionStyle::Box;
        Entity primary = m_SceneHierarchyPanel.GetSelectedEntity();
        for (Entity selected : m_SceneHierarchyPanel.GetSelection().GetEntities())
        {
            if (!selected.IsValid() || selected.GetScene() != &scene) continue;
            if (skipped.SkipEditorHidden && scene.IsHiddenInEditor(selected)) continue;
            glm::vec4 orange = selected == primary ? glm::vec4(1.0f, 0.6f, 0.1f, 1.0f) : glm::vec4(1.0f, 0.85f, 0.35f, 0.85f);
            const bool inactive = !scene.IsActiveInHierarchy(selected);
            if (!boxStyle && inactive)
                orange.a *= 0.5f;
            glm::mat4 transform = scene.GetWorldTransform(selected);
            if (boxStyle || inactive)
            {
                if (auto* mr = selected.TryGetComponent<MeshRendererComponent>(); mr && (mr->MeshAsset || mr->ModelAsset))
                {
                    const AABB& b = mr->ModelAsset ? mr->ModelAsset->GetBounds() : mr->MeshAsset->GetBounds();
                    glm::vec3 size = glm::max(b.Max - b.Min, glm::vec3(0.001f));
                    DrawWireBox(transform * glm::translate(glm::mat4(1.0f), (b.Min + b.Max) * 0.5f) * glm::scale(glm::mat4(1.0f), size), orange);
                }
                else if (auto* sprite = selected.TryGetComponent<SpriteRendererComponent>())
                {
                    // A sliced / tiled sprite's rectangle, an off-centre frame.
                    Renderer2D::DrawRect(transform * Scene::GetSpriteRectTransform(*sprite, selected.TryGetComponent<SpriteAnimationComponent>(),
                                                                                   Project::GetAssetRegistry()), orange);
                }
                else if (selected.HasComponent<CircleRendererComponent>())
                {
                    Renderer2D::DrawRect(transform, orange);
                }
                else if (auto* text = selected.TryGetComponent<TextComponent>())
                {
                    if (text->FontAsset)
                    {
                        // The laid-out lines; a wrap width shows as the box's width.
                        UIRect r = text->LayoutWorld().GetBounds();
                        if (text->MaxWidth > 0.0f)
                        {
                            const float x0 = text->AlignH == TextAlignH::Left ? 0.0f : text->AlignH == TextAlignH::Center ? -text->MaxWidth * 0.5f : -text->MaxWidth;
                            r.Min.x = std::min(r.Min.x, x0);
                            r.Max.x = std::max(r.Max.x, x0 + text->MaxWidth);
                        }
                        glm::mat4 box = transform * glm::translate(glm::mat4(1.0f), glm::vec3(r.GetCenter(), 0.0f)) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(glm::max(r.GetSize(), glm::vec2(0.01f)), 1.0f));
                        Renderer2D::DrawRect(box, orange);
                    }
                }
            }
            // Lights: how far they reach (point: a sphere of circles, spot: its cone).
            const glm::vec4 lightColor = selected == primary ? glm::vec4(1.0f, 0.88f, 0.4f, 1.0f) : glm::vec4(1.0f, 0.9f, 0.55f, 0.6f);
            if (auto* pl = selected.TryGetComponent<PointLightComponent>())
                DrawPointLightRange(glm::vec3(transform[3]), pl->Radius, lightColor, overlayView);
            if (auto* sl = selected.TryGetComponent<SpotLightComponent>())
                DrawSpotLightCone(glm::vec3(transform[3]), glm::vec3(transform * glm::vec4(0, 0, -1, 0)), sl->Range, sl->GetInnerAngle(),
                                  sl->GetOuterAngle(), lightColor);
            if (auto* l2 = selected.TryGetComponent<Light2DComponent>())
                DrawLight2DRange(glm::vec3(transform[3]), glm::vec2(transform * glm::vec4(1, 0, 0, 0)), *l2, lightColor);
            // children get a subtle line in the Box style (the outline marks them itself)
            if (boxStyle)
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
        // An extra view under the mouse: its camera (also while playing: the views watch the game).
        for (int i = 0; i < SceneViewSettings::Count; i++)
            if (m_ViewSettings.SceneViews[i].Open && m_ExtraViews[i].Hovered)
                m_ExtraViews[i].Camera.OnEvent(e);

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
            bool overGizmo = m_SceneHierarchyPanel.GetSelectedEntity() && m_GizmoHovered && m_GizmoMode != GizmoMode::None;
            // A press on the UI rect gizmo is its own (inside it: a click selects when the button comes up),
            // the axis navigator's too.
            if (IsUIRectGizmoHovered() || m_NavigatorHovered)
                return false;
            if (alt && overGizmo && m_SceneState == SceneState::Edit && !m_SceneHierarchyPanel.IsTilePainting())
            {
                // Alt + drag on the gizmo: drag copies of the selection, the originals stay.
                m_GizmoAltDrag = true;
                m_GizmoAltDuplicatePending = true;
            }
            if (!alt && !overGizmo && !m_SceneHierarchyPanel.IsTilePainting() && !IsViewportHandleEditing())
            {
                // Ctrl+click toggles, Shift+click adds to the selection. A click on a locked
                // entity goes through it: the next frame picks again without the locked ones
                // (hidden ones are not drawn at all).
                bool ctrl = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
                bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);
                // Screen-space UI over the scene: picked by its rectangle (also where it is see-through).
                Entity picked = m_HoveredUIElement ? m_HoveredUIElement : m_HoveredEntity;
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
        const glm::ivec2 pixel = ViewportToRenderPixel(pick.Pixel);
        if (pixel.x >= 0 && pixel.y >= 0 && pixel.x < (int)spec.Width && pixel.y < (int)spec.Height)
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
            options.Parallax = m_PreviewParallax; // the layers where the scene view shows them
            m_ActiveScene->OnRenderEditor(m_EditorCamera, options);
            int id = m_Framebuffer->ReadPixel(1, pixel.x, pixel.y);
            m_Framebuffer->Unbind();
            if (id != -1)
                picked = Entity((entt::entity)id, m_ActiveScene.get());
            if (picked && (!picked.IsValid() || m_ActiveScene->IsLockedInEditor(picked)))
                picked = {};
        }
        m_SceneHierarchyPanel.SelectWithModifiers(picked, pick.Ctrl, pick.Shift);
    }

    // ================================================================== projects
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
        SaveAssetRegistryCache(true);
        DiscardSessionAutosave(); // the previous project's scene is left
        if (!Project::Load(projectFile))
        {
            ZM_ERROR("Loyiha ochilmadi: {}", path.string());
            return false;
        }
        // The lock file first: the start scene's autosave check knows whether the last session crashed.
        AcquireProjectLock();
        m_ProjectSettingsPanel.OnProjectChanged();
        AddRecentProject(projectFile);
        m_ShowLauncher = false;
        StartAssetWatcher(); // the asset registry first (missing .zmeta files are written), then the watcher

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
        m_ScenesPanel.OnProjectChanged();
        m_SceneThumbnails.Clear();
        m_AssetInspectorPanel.Reset();
        m_SpriteEditorPanel.Reset();
        m_AssetUsage.Reset();

        auto& config = Project::GetActive()->GetConfig();
        fs::path startScene = Project::GetAssetFileSystemPath(config.StartScene);
        if (config.StartScene.empty() || !OpenScene(startScene))
            NewScene();
        // A scene that was never saved has its copy under a name of its own.
        if (!m_Recovery.Pending)
            CheckAutosaveRecovery({});
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
        m_AssetHotReload.Stop();
        SaveAssetRegistryCache(true);
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        ScriptEngine::UnloadModule();
        m_ProjectSettingsPanel.SaveIfDirty();
        m_ProjectSettingsPanel.OnProjectChanged();
        SaveData::Save();
        DiscardSessionAutosave();
        ReleaseProjectLock();
        Project::SetActive(nullptr);
        Project::ApplyLocalization(); // no project: no language files
        // No project: no quality levels (nothing limited, textures sampled at full quality) and the
        // mixer's defaults, not the closed project's.
        Quality::Reset();
        AudioEngine::SetMixerSettings(AudioSettings());
        m_LocalizationPanel.Invalidate();
        m_ContentBrowserPanel.SetRoot({});
        m_ScenesPanel.OnProjectChanged();
        m_SceneThumbnails.Clear();
        m_AssetInspectorPanel.Reset();
        m_SpriteEditorPanel.Reset();
        m_AssetUsage.Reset();
        NewScene();
        m_ShowLauncher = true;
        UpdateWindowTitle();
    }

    // ================================================================== scenes
    void EditorLayer::SetActiveScene(const Ref<Scene>& scene, bool newView)
    {
        m_ActiveScene = scene;
        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        EndGizmoDrag();
        m_SceneStatsDirty = true;
        // Entities kept from the previous scene (it may be freed right after this: undo, stop):
        // the next frame's picking finds them again in this one.
        m_HoveredEntity = {};
        for (ExtraSceneView& view : m_ExtraViews)
            view.HoveredEntity = {};
        m_OutlineHover = {};
        m_OutlineHoverCaptured = {};
        // The brightness adapted to the previous scene (or to play mode) does not carry over:
        // the next frame adapts at once.
        if (newView)
            m_PostHistory.Reset();
        // The same entities (by UUID) stay selected: play mode copies, undo snapshots.
        m_SceneHierarchyPanel.SetContext(m_ActiveScene, true);
    }

    void EditorLayer::NewScene()
    {
        if (m_SceneState != SceneState::Edit)
            OnSceneStop();
        DiscardSessionAutosave(); // the scene that was open is left
        m_EditorScene = CreateRef<Scene>();
        m_EditorScene->SetName("Yangi sahna");
        SetActiveScene(m_EditorScene);
        m_EditorScenePath.clear();
        m_EditorSceneFileVersion = SceneMigration::CurrentVersion;
        m_ViewSettings.Bookmarks.erase(std::string()); // bookmarks of the previous unsaved scene
        m_SceneDirty = false;
        m_AssetUsage.MarkDirty(); // the previous scene's unsaved state no longer counts
        ResetUndoHistory("Yangi sahna");
        if (ImGui::GetCurrentContext())
            m_LastAutosaveTime = ImGui::GetTime();
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

        const auto loadClock = std::chrono::steady_clock::now();
        Ref<Scene> newScene = CreateRef<Scene>();
        SceneSerializer serializer(newScene);
        SceneLoadTiming timing;
        timing.How = SceneLoadTiming::Source::Editor;
        timing.Path = Project::GetActive() ? Project::GetAssetRelativePath(path) : path.generic_string();
        if (!serializer.Deserialize(path))
        {
            timing.Add(serializer.GetTiming());
            timing.Failed = true;
            timing.WallMilliseconds = ElapsedMilliseconds(loadClock);
            SceneLoadProfiler::Record(timing);
            // The scene that was open stays; what went wrong is shown over the viewport.
            ShowSceneLoadReport(serializer.GetLoadReport());
            return false;
        }
        timing.Add(serializer.GetTiming());
        const auto assetsClock = std::chrono::steady_clock::now();
        newScene->ResolveAssets();
        timing[SceneLoadTiming::Stage::Assets] = ElapsedMilliseconds(assetsClock);
        timing.SceneName = newScene->GetName();
        timing.WallMilliseconds = ElapsedMilliseconds(loadClock);
        SceneLoadProfiler::Record(timing);
        if (serializer.GetLoadReport().HasIssues())
            ShowSceneLoadReport(serializer.GetLoadReport());
        else if (m_LoadReportBanner && m_LoadReport.File == serializer.GetLoadReport().File)
            m_LoadReportBanner = false; // fixed: loads clean now

        DiscardSessionAutosave(); // the scene that was open is left
        m_EditorScene = newScene;
        m_SceneHierarchyPanel.SetContext({});
        SetActiveScene(m_EditorScene);
        m_EditorScenePath = path;
        m_EditorSceneFileVersion = serializer.GetMigration().FileVersion;
        // Asset paths found again by UUID (files renamed outside the editor): the scene in memory
        // is newer than its file until it is saved.
        m_SceneDirty = !serializer.GetRepairedReferences().empty();
        m_AssetUsage.MarkDirty(); // the previous scene's unsaved state no longer counts
        if (m_SceneDirty)
            m_ContentBrowserPanel.ShowNotice(std::format("Sahnada {} ta asset havolasi UUID orqali tiklandi - saqlang",
                                                         serializer.GetRepairedReferences().size()));
        ResetUndoHistory(std::format("Sahna ochildi: {}", path.stem().string()), !m_SceneDirty);
        // Auto-detect 2D scenes (orthographic primary camera; an inactive one still tells).
        Entity sceneCamera = m_EditorScene->GetPrimaryCameraEntity();
        if (!sceneCamera)
            sceneCamera = m_EditorScene->GetPrimaryCameraEntity(true);
        if (sceneCamera)
            Set2DMode(sceneCamera.GetComponent<CameraComponent>().Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic);
        ZM_INFO("Sahna ochildi: {}", path.filename().string());
        AddRecentScene(path);
        m_LastAutosaveTime = ImGui::GetTime();
        CheckAutosaveRecovery(path); // a newer safety copy (the last session crashed): offered back
        UpdateWindowTitle();
        return true;
    }

    void EditorLayer::SerializeScene(const Ref<Scene>& scene, const fs::path& path)
    {
        SceneSerializer serializer(scene);
        bool written = false;
        // The scene's own values, not the Animation window's preview pose.
        m_AnimationPanel.WithoutPreview([&]() { written = serializer.Serialize(path); });
        if (written)
            ZM_INFO("Sahna saqlandi: {}", path.filename().string());
    }

    // ------------------------------------------------------------------ scene metadata (description, author, tags, thumbnail)
    void EditorLayer::UI_SceneMetadata(bool& changed)
    {
        ImGui::SeparatorText(Tr(ICON_FA_CIRCLE_INFO "  Ma'lumot"));
        if (m_SceneSettingsScrollTo == "metadata")
        {
            ImGui::SetScrollHereY(0.0f);
            m_SceneSettingsScrollTo.clear();
        }
        SceneMetadata& meta = m_ActiveScene->GetMetadata();
        if (UI::BeginPropertyGrid("meta"))
        {
            changed |= UI::PropertyString("Ta'rif", meta.Description, true);
            changed |= UI::PropertyString("Muallif", meta.Author);
            UI::PropertyLabel("Teglar");
            // The tags as chips (x removes one), then a field for a new one (Enter adds it).
            std::string removed;
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            for (size_t i = 0; i < meta.Tags.size(); i++)
            {
                const std::string label = std::format("{}  " ICON_FA_XMARK "##tag{}", meta.Tags[i], i);
                const float width = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                if (i > 0)
                {
                    ImGui::SameLine();
                    if (ImGui::GetCursorScreenPos().x + width > right)
                        ImGui::NewLine();
                }
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.32f, 0.45f, 1.0f));
                if (ImGui::SmallButton(label.c_str()))
                    removed = meta.Tags[i];
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Tegni olib tashlash");
            }
            if (!removed.empty() && meta.RemoveTag(removed))
                changed = true;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##newSceneTag", Tr(ICON_FA_PLUS "  teg qo'shish (Enter)"), &m_NewSceneTag, ImGuiInputTextFlags_EnterReturnsTrue))
            {
                if (meta.AddTag(m_NewSceneTag))
                    changed = true;
                m_NewSceneTag.clear();
                ImGui::SetKeyboardFocusHere(-1);
            }
            UI::PropertyLabel("Format");
            if (m_EditorSceneFileVersion >= 0 && m_EditorSceneFileVersion < SceneMigration::CurrentVersion && m_SceneState == SceneState::Edit)
            {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "v%d (fayl v%d)", SceneMigration::CurrentVersion, m_EditorSceneFileVersion);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("Fayl eski formatda (v{}): yuklashda v{} ga yangilandi, saqlanganda yangi formatda yoziladi",
                                            m_EditorSceneFileVersion, SceneMigration::CurrentVersion).c_str());
            }
            else
                ImGui::Text("v%d", SceneMigration::CurrentVersion);
            UI::EndPropertyGrid();
        }
        // The thumbnail (drawn whenever the scene is saved).
        if (m_SceneState == SceneState::Edit && !m_EditorScenePath.empty())
        {
            Ref<Texture2D> thumbnail = m_SceneThumbnails.Get(m_EditorScenePath);
            const float width = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
            if (thumbnail)
            {
                const float height = width * (float)thumbnail->GetHeight() / (float)std::max(1u, thumbnail->GetWidth());
                ImGui::Image((ImTextureID)(uintptr_t)thumbnail->GetRendererID(), ImVec2(width, height));
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Sahna rasmi: har saqlashda boshlang'ich kameradan (kamera bo'lmasa - editor kamerasidan) chiziladi");
            }
            else
                ImGui::TextDisabled(ICON_FA_IMAGE "  Rasm yo'q - sahna saqlanganda chiziladi");
            if (ImGui::SmallButton(Tr(ICON_FA_CAMERA "  Rasmni hozir chizish")))
                RenderSceneThumbnail();
            if (ImGui::IsItemHovered())
                UI::Tooltip("Sahna rasmini qayta chizish (fayl saqlanmaydi)");
        }
    }

    void EditorLayer::ProcessThumbnailQueue()
    {
        if (m_ThumbnailQueue.empty())
            return;
        const fs::path path = m_ThumbnailQueue.front();
        m_ThumbnailQueue.pop_front();
        std::error_code ec;
        if (!m_EditorScenePath.empty() && fs::equivalent(path, m_EditorScenePath, ec) && m_SceneState == SceneState::Edit)
            RenderSceneThumbnail(); // the open scene: as it is now, from the editor camera when it has none
        else
        {
            // Loaded for its picture only; a scene without a camera is seen from the default editor view.
            Ref<Scene> scene = CreateRef<Scene>();
            SceneSerializer serializer(scene);
            if (serializer.Deserialize(path))
            {
                EditorCamera camera;
                camera.SetViewportSize((float)SceneMetadata::ThumbnailWidth, (float)SceneMetadata::ThumbnailHeight);
                if (SceneThumbnails::Render(scene, path, &camera))
                {
                    m_SceneThumbnails.Invalidate(path);
                    m_ContentBrowserPanel.InvalidateThumbnail(path);
                }
            }
        }
        if (m_ThumbnailQueue.empty())
        {
            m_ScenesPanelDirty = true;
            ZM_INFO("Sahna rasmlari chizildi");
        }
    }

    bool EditorLayer::RenderSceneThumbnail()
    {
        if (m_SceneState != SceneState::Edit || m_EditorScenePath.empty() || !m_EditorScene)
            return false;
        bool drawn = false;
        // The scene's own values, not the Animation window's preview pose.
        m_AnimationPanel.WithoutPreview([&]() { drawn = SceneThumbnails::Render(m_EditorScene, m_EditorScenePath, &m_EditorCamera); });
        if (drawn)
        {
            m_SceneThumbnails.Invalidate(m_EditorScenePath);
            m_ContentBrowserPanel.InvalidateThumbnail(m_EditorScenePath);
            m_ScenesPanelDirty = true;
        }
        return drawn;
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
        CommitUndo(); // the history's current state is what the file gets
        SerializeScene(m_EditorScene, m_EditorScenePath);
        m_UndoHistory.MarkSaved();
        // The file has everything now: its safety copy (this session's or an older one) goes.
        if (const fs::path autosave = GetAutosavePath(); !autosave.empty())
        {
            std::error_code ec;
            fs::remove(autosave, ec);
            m_SessionAutosaves.erase(autosave.lexically_normal().string());
        }
        m_AutosavedScene = m_UndoHistory.GetCurrent().Scene;
        m_LastAutosaveTime = ImGui::GetTime();
        m_EditorSceneFileVersion = SceneMigration::CurrentVersion; // written in the current format now
        RenderSceneThumbnail(); // the scene's picture (Scenes panel, content browser, inspector)
        m_AnimationPanel.SaveIfDirty(); // the clip being edited goes with the scene
        m_SceneDirty = false;
        m_AssetUsage.MarkDirty(); // the file says it now (a first save: it has a path at last)
        if (auto project = Project::GetActive(); project && project->GetConfig().StartScene.empty())
        {
            project->GetConfig().StartScene = Project::GetAssetRelativePath(m_EditorScenePath);
            SaveProjectSettingsNow();
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
        DiscardSessionAutosave(); // the copy under the old path (or the unsaved scene's)
        m_EditorScenePath = p;
        SaveScene();
        AddRecentScene(p);
    }

    void EditorLayer::OnScenePlay()
    {
        if (m_SceneState == SceneState::Simulate)
            OnSceneStop();
        // Konsol > "O'yin boshlanganda tozalash": the game's run starts with an empty console.
        if (m_ConsolePanel.GetPreferences().ClearOnPlay)
            Log::Clear();
        m_SceneState = SceneState::Play;
        m_AnimationPanel.StopPreview(true); // the game starts from the scene's own values
        // The game's display options start as the player's would (the project's Player settings, no resolution
        // chosen, gamma and brightness 1; what the game saved is loaded below): nothing of the last run stays.
        // The game's resolution list is the editor's monitor's (no window of its own: none larger than the screen).
        Display::ResetOptions(Project::GetPlayerSettings());
        Display::SetHostVideoModes(Application::Get().GetWindow().GetVideoModes());
        m_PlayMaterials = MaterialLibrary::TakeSnapshot();
        // The editor's state, for Stop after the game has switched scenes.
        m_PlayStart = {};
        for (Entity e : m_SceneHierarchyPanel.GetSelection().GetEntities())
            if (e && e.IsValid() && e != m_SceneHierarchyPanel.GetSelectedEntity())
                m_PlayStart.Selection.push_back(e.GetUUID());
        if (Entity primary = m_SceneHierarchyPanel.GetSelectedEntity(); primary && primary.IsValid())
            m_PlayStart.Selection.push_back(primary.GetUUID());
        m_PlayStart.Camera = m_EditorCamera.GetViewState();
        m_PlayStart.Mode2D = m_2DMode;
        m_PlayStart.SceneName = m_EditorScene->GetName();
        m_PlayStart.QualityLevel = Quality::GetLevelName();
        m_PlayStart.Language = Localization::GetLanguage();
        // The game's first scene is a copy of the edit scene: its making counts as the entities'
        // stage of the load (the scene manager adds the physics, the scripts and the audio).
        const auto playClock = std::chrono::steady_clock::now();
        SceneLoadTiming playTiming;
        Ref<Scene> runtime = Scene::Copy(m_EditorScene);
        playTiming[SceneLoadTiming::Stage::Deserialize] = ElapsedMilliseconds(playClock);
        const auto assetsClock = std::chrono::steady_clock::now();
        runtime->ResolveAssets();
        playTiming[SceneLoadTiming::Stage::Assets] = ElapsedMilliseconds(assetsClock);
        m_PlayStart.RuntimeStart = runtime;
        SetActiveScene(runtime);
        ApplyDebugDrawSettings(); // before the scripts' OnCreate: they may change them
        // As in the player: the audio settings the game saved (its options menu) over the project's
        // mixer, for this run (the mixer is the project's again when play stops).
        if (Audio::LoadSettings())
            ZM_INFO("O'yinda saqlangan ovoz sozlamalari yuklandi (SaveData)");
        // The display options too (no window of its own: the editor's stays as it is, the game sees them) - the
        // player's gamma and brightness as the game saved them (its options menu): Play shows them too.
        Display::LoadSettings();
        // The language the player chose in the game's language menu (else the editor's preview language).
        if (Localization::LoadSettings())
            ZM_INFO("O'yinda saqlangan til yuklandi: {}", Localization::GetLanguageName(Localization::GetLanguage()));
        AudioEngine::ResetProfileCounters(); // the audio profiler counts this run
        // Every run starts as the game does: no global state, no services from the last run.
        Services::Clear();
        GameState::Clear();
        GameState::ClearListeners();
        // The game runs in a scene manager (additive scenes, DontDestroyOnLoad, async loads, fades),
        // as in the player.
        m_RuntimeScenes = std::make_unique<SceneManager>();
        m_RuntimeScenes->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        playTiming.WallMilliseconds = ElapsedMilliseconds(playClock);
        m_RuntimeScenes->Start(m_ActiveScene, m_EditorScenePath.empty() ? fs::path() : fs::path(Project::GetAssetRelativePath(m_EditorScenePath)),
                               &playTiming);
        m_RuntimeActiveSerial = m_RuntimeScenes->GetActiveSceneSerial();
        m_RuntimeReportSerial = m_RuntimeScenes->GetLoadReportSerial();
        m_SceneHierarchyPanel.SetSceneManager(m_RuntimeScenes.get());
        LoadPlayAdditiveScenes();
        ImGui::SetWindowFocus("###Viewport");
        UpdateWindowTitle();
    }

    void EditorLayer::OnSceneSimulate()
    {
        if (m_SceneState == SceneState::Play)
            OnSceneStop();
        m_SceneState = SceneState::Simulate;
        m_AnimationPanel.StopPreview(true);
        m_PlayMaterials = MaterialLibrary::TakeSnapshot();
        Ref<Scene> sim = Scene::Copy(m_EditorScene);
        sim->ResolveAssets();
        SetActiveScene(sim);
        ApplyDebugDrawSettings();
        m_ActiveScene->OnSimulationStart();
        UpdateWindowTitle();
    }

    void EditorLayer::LoadPlayAdditiveScenes()
    {
        if (!m_RuntimeScenes)
            return;
        for (const std::string& scene : m_ScenesPanel.GetPlayAdditiveScenes())
        {
            if (!m_EditorScenePath.empty() && Project::GetAssetRelativePath(m_EditorScenePath) == scene)
                continue; // the edit scene itself
            m_RuntimeScenes->LoadScene(fs::path(scene), LoadSceneMode::Additive);
            ZM_INFO("O'yin bilan qo'shimcha sahna yuklanadi: {}", scene);
        }
    }

    void EditorLayer::OnSceneStop()
    {
        // The game has switched scenes (or the hierarchy shows another of its scenes): the edit scene
        // comes back as it was left - its selection and the editor camera's view.
        bool restoreView = false;
        if (m_SceneState == SceneState::Play)
        {
            Ref<Scene> start = m_PlayStart.RuntimeStart.lock();
            restoreView = !start || start != m_ActiveScene;
        }
        if (m_SceneState == SceneState::Play && m_RuntimeScenes)
        {
            // Every scene of the game stops (the persistent one too).
            m_RuntimeScenes->Stop();
        }
        // The game may have picked another quality level (an options menu): the editor's preview comes back.
        if (m_SceneState == SceneState::Play && !m_PlayStart.QualityLevel.empty() && Quality::GetLevelName() != m_PlayStart.QualityLevel)
            Quality::SetLevel(m_PlayStart.QualityLevel);
        // The game may have switched the language (a language menu): the editor's preview language comes back.
        if (m_SceneState == SceneState::Play && !m_PlayStart.Language.empty() && Localization::GetLanguage() != m_PlayStart.Language)
            Localization::SetLanguage(m_PlayStart.Language);
        if (m_SceneState == SceneState::Play)
        {
            // The game's global state and services end with the run (made by its scripts' code).
            if (const size_t services = Services::GetCount(); services > 0 || GameState::GetCount() > 0)
                ZM_INFO("O'yin holati tozalandi: {} ta qiymat, {} ta servis", GameState::GetCount(), services);
            Services::Clear();
            GameState::Clear();
            GameState::ClearListeners();
        }
        else if (m_SceneState == SceneState::Play)
            m_ActiveScene->OnRuntimeStop();
        else if (m_SceneState == SceneState::Simulate)
            m_ActiveScene->OnSimulationStop();
        m_SceneState = SceneState::Edit;
        SetActiveScene(m_EditorScene);
        m_SceneHierarchyPanel.SetSceneManager(nullptr);
        m_RuntimeScenes.reset(); // the game's scenes go (the hierarchy shows the edit scene again)
        if (restoreView)
        {
            std::vector<Entity> selection;
            for (UUID id : m_PlayStart.Selection)
                if (Entity e = m_EditorScene->GetEntityByUUID(id))
                    selection.push_back(e);
            m_SceneHierarchyPanel.GetSelection().Set(selection);
            m_2DMode = m_PlayStart.Mode2D;
            m_EditorCamera.SetViewState(m_PlayStart.Camera);
            ZM_INFO("O'yin boshqa sahnada to'xtadi: editor '{}' sahnasiga qaytdi (tanlov: {} ta obyekt, kamera tiklandi)",
                    m_EditorScene->GetName(), selection.size());
            ShowViewportMessage(std::format(ICON_FA_ROTATE_LEFT "  Editor holati tiklandi: {} ({} ta tanlangan)", m_EditorScene->GetName(), selection.size()));
        }
        m_PlayStart.RuntimeStart.reset();
        // The game's cursor wishes end with it (visible, free, the arrow); its display options too (fullscreen,
        // vsync, the resolution it chose, its picture's gamma and brightness).
        Cursor::SetHostActive(false);
        Cursor::Reset();
        Display::ResetOptions(Project::GetPlayerSettings());
        m_GameCursorReleased = false;
        // What the game's scripts changed of the debug drawing does not stay in the editor.
        ApplyDebugDrawSettings();
        // The game's music stops with it, and the mixer is the project's again (what the game or the
        // mixer window changed while it ran goes, unless it was saved as the defaults).
        AudioEngine::StopAll();
        AudioEngine::SetMusicVolume(1.0f); // the game's (or its saved settings') music volume goes too
        if (Ref<Project> project = Project::GetActive())
            AudioEngine::SetMixerSettings(project->GetConfig().Audio);
        // Shared materials scripts changed while playing (GetSharedMaterial) look as before again.
        std::vector<std::string> restored = MaterialLibrary::RestoreSnapshot(m_PlayMaterials);
        m_PlayMaterials = {};
        if (!restored.empty())
        {
            std::string names;
            for (const std::string& name : restored)
                names += (names.empty() ? "" : ", ") + name;
            ZM_INFO("O'yin vaqtida o'zgartirilgan umumiy materiallar asl holiga qaytarildi: {}", names);
        }
        UpdateWindowTitle();
    }

    void EditorLayer::OnScenePause()
    {
        if (m_SceneState == SceneState::Edit) return;
        if (m_RuntimeScenes && m_SceneState == SceneState::Play)
            m_RuntimeScenes->SetPaused(!m_RuntimeScenes->IsPaused());
        else
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
        if (Entity copy = m_SceneHierarchyPanel.GetSelectedEntity(); copy && m_PendingUndoName.empty())
            m_PendingUndoName = count > 1 ? std::format("Nusxa olindi: {} (+{})", copy.GetName(), count - 1) : std::format("Nusxa olindi: {}", copy.GetName());
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
        EndGizmoDrag();
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

    void EditorLayer::ToggleSelectionActive()
    {
        bool active = false;
        size_t count = m_SceneHierarchyPanel.ToggleSelectionActive(&active);
        if (count == 0)
            return;
        Entity primary = m_SceneHierarchyPanel.GetSelectedEntity();
        std::string what = count > 1 ? std::format("{} ta obyekt", count) : primary ? primary.GetName() : std::string("obyekt");
        ShowViewportMessage(active ? std::format(ICON_FA_TOGGLE_ON "  Faollashtirildi: {}", what)
                                   : std::format(ICON_FA_TOGGLE_OFF "  Nofaol qilindi: {}  (Alt+Shift+A: qayta yoqish)", what));
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
        // The view the mouse / keyboard is in (the last one used otherwise).
        EditorCamera& camera = GetSceneViewCamera(GetShortcutSceneView());
        camera.TransitionTo(camera.ComputeFrameState(bounds), 0.3f);
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
        EditorCamera& camera = GetSceneViewCamera(GetShortcutSceneView());
        camera.TransitionTo(camera.ComputeFrameState(bounds), 0.35f);
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
        m_2DMode = state->Mode2D; // the camera switches its projection (and the 2D lock) itself
        m_EditorCamera.TransitionTo(*state, 0.35f);
        ShowViewportMessage(std::format(ICON_FA_BOOKMARK "  Xatcho'p {}", slot));
    }

    void EditorLayer::ShowViewportMessage(std::string text)
    {
        m_ViewportMessage = std::move(text);
        m_ViewportMessageTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
        WakeEditor(); // the notice fades in and out at full speed
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
        // The extra views' cameras where they look now (the next session starts there).
        for (int i = 0; i < SceneViewSettings::Count; i++)
            if (m_ExtraViews[i].CameraLoaded)
            {
                m_ViewSettings.SceneViews[i].Camera = m_ExtraViews[i].Camera.GetViewState();
                m_ViewSettings.SceneViews[i].Camera.Mode2D = false;
            }
        m_ViewSettings.Snap.Sanitize();
        m_ViewSettings.Grid.Sanitize();
        const EditorViewSettings* saved = &m_ViewSettings;
        std::optional<EditorViewSettings> kept;
        if (m_ViewSettingsOverride)
        {
            // What the automation hooks set for this session (and nobody changed since) is not saved.
            kept = m_ViewSettings;
            if (kept->RestoreUnchangedSections(m_ViewSettingsOverride->Before, m_ViewSettingsOverride->Applied,
                                               { "EntityLabels", "DebugDraw", "Gizmos", "TransformGizmo", "SelectionOutline",
                                                 "StatsOverlay", "GameView" }) > 0)
                saved = &*kept;
        }
        if (!saved->Save(ConfigDirectory() / "editor-view.json"))
            ZM_WARN("Viewport sozlamalari saqlanmadi");
        m_ViewSettingsDirty = false;
    }

    void EditorLayer::Set2DMode(bool enabled)
    {
        m_2DMode = enabled;
        m_EditorCamera.SetOrthographic(enabled);
        m_EditorCamera.SetRotationLocked(enabled);
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
        Entity first = roots.empty() ? entities.front() : roots.front();
        MarkDirty(roots.size() > 1 ? std::format("Joylandi: {} (+{})", first.GetName(), roots.size() - 1) : std::format("Joylandi: {}", first.GetName()));
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
        {
            ZM_INFO("Prefab saqlandi: {}", Project::GetAssetRelativePath(path));
            // The flags are saved as they are: instances start the same way.
            if (!entity.IsActive())
                ZM_INFO("'{}' nofaol holatda saqlandi: prefab nusxalari ham nofaol boshlanadi (SetActive(true) bilan yoqiladi)", entity.GetName());
        }
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
        // The instance remembers its prefab (the scene statistics count instances by it).
        m_EditorScene->SetPrefabSource(root, Project::GetAssetRelativePath(Project::GetAssetFileSystemPath(assetPath)));
        m_SceneHierarchyPanel.SetSelectedEntity(root);
        MarkDirty(std::format("Prefab qo'shildi: {}", root.GetName()));
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
            case AssetType::ParticlePreset:
            {
                // A new entity with the effect where the scene view looks (its sub-emitter templates as children).
                ParticlePreset preset;
                std::string error;
                if (!ParticlePresets::Load(path, preset, &error))
                {
                    ZM_ERROR("Particle preset o'qilmadi: {} - {}", assetPath, error);
                    return {};
                }
                Entity created = ParticlePresets::Instantiate(*m_EditorScene, preset, m_EditorCamera.GetFocalPoint());
                m_SceneHierarchyPanel.SetSelectedEntity(created);
                MarkDirty(std::format("Particle preset: {}", created.GetName()));
                return created;
            }
            default:
                return {};
        }
        if (e)
            e.SetName(path.stem().string());
        return e;
    }

    // ================================================================== materials onto meshes
    Entity EditorLayer::GetMeshUnderMouse()
    {
        Entity e = m_HoveredEntity;
        if (!e || !e.IsValid() || e.GetScene() != m_ActiveScene.get() || !e.HasComponent<MeshRendererComponent>())
            return {};
        if (m_ActiveScene->IsLockedInEditor(e)) // locked: not changed from the viewport
            return {};
        return e;
    }

    bool EditorLayer::ApplyMaterialToEntity(Entity entity, const std::string& assetPath, int submesh)
    {
        if (m_SceneState != SceneState::Edit || !entity || !entity.IsValid())
            return false;
        auto* mr = entity.TryGetComponent<MeshRendererComponent>();
        if (!mr)
        {
            ShowViewportMessage(std::format(ICON_FA_CIRCLE_INFO "  '{}' da Mesh Renderer yo'q - material qo'yilmadi", entity.GetName()));
            return false;
        }
        size_t parts = mr->Type == PrimitiveMesh::Model && mr->ModelAsset ? mr->ModelAsset->GetSubMeshes().size() : 0;
        if (submesh >= 0 && (size_t)submesh >= parts)
            submesh = -1;
        std::string where = entity.GetName();
        if (submesh >= 0)
            where += std::format(" / #{} {}", submesh, mr->ModelAsset->GetSubMeshes()[(size_t)submesh].Name);
        std::string file = fs::path(assetPath).filename().string();
        const std::string& current = submesh < 0 ? mr->MaterialPath
                                   : (size_t)submesh < mr->SubmeshMaterials.size() ? mr->SubmeshMaterials[(size_t)submesh] : std::string();
        if (current == assetPath)
        {
            ShowViewportMessage(std::format(ICON_FA_PALETTE "  {}: {} allaqachon shu material", where, file));
            return true;
        }
        if (!Scene::SetMeshMaterial(*mr, assetPath, submesh))
            return false;
        MarkDirty(std::format("Material: {} ({})", fs::path(assetPath).stem().string(), entity.GetName()));
        bool loaded = submesh < 0 ? mr->MaterialRef != nullptr : Scene::GetMeshMaterialAsset(*mr, (size_t)submesh) != nullptr;
        if (!loaded)
        {
            ShowViewportMessage(std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {}: {} yuklanmadi - konsolni tekshiring", where, file));
            return true;
        }
        std::string text = std::format(ICON_FA_PALETTE "  {}: {}", where, file);
        size_t own = submesh < 0 ? (size_t)std::count_if(mr->SubmeshMaterials.begin(), mr->SubmeshMaterials.end(),
                                                         [](const std::string& p) { return !p.empty(); }) : 0;
        if (own > 0)
            text += std::format("  ({} ta qismning o'z materiali bor)", own);
        ShowViewportMessage(text);
        ZM_INFO("Material qo'llandi: {} -> {}", assetPath, where);
        return true;
    }

    void EditorLayer::UpdateMaterialDrag(const std::string& assetPath)
    {
        ImVec2 mouse = ImGui::GetMousePos();
        ImVec2 labelAt(mouse.x + 18.0f, mouse.y + 34.0f); // under the drag tooltip
        if (m_SceneState != SceneState::Edit)
        {
            m_MaterialDrag.Path.clear();
            m_MaterialDrag.Target = 0;
            DrawMaterialDragLabel(labelAt, {});
            return; // not accepted: nothing changes while the game runs
        }
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM",
                                                                   ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (!payload)
            return;
        if (m_MaterialDrag.Path != assetPath)
        {
            m_MaterialDrag.Path = assetPath;
            m_MaterialDrag.Material = MaterialLibrary::Load(Project::GetAssetFileSystemPath(assetPath));
        }
        Entity target = GetMeshUnderMouse();
        m_MaterialDrag.Target = target ? target.GetUUID() : UUID(0);
        m_MaterialDrag.Submesh = -1;
        m_MaterialDrag.Active = true;
        DrawMaterialDragLabel(labelAt, target);
        if (!payload->IsDelivery())
            return;

        m_MaterialDrag.Active = false;
        if (!target)
            ShowViewportMessage(ICON_FA_CIRCLE_INFO "  Materialni mesh (Mesh Renderer) ustiga tashlang");
        else if (!m_MaterialDrag.Material)
            ShowViewportMessage(std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {} yuklanmadi - konsolni tekshiring", fs::path(assetPath).filename().string()));
        else
        {
            auto& mr = target.GetComponent<MeshRendererComponent>();
            size_t parts = mr.Type == PrimitiveMesh::Model && mr.ModelAsset ? mr.ModelAsset->GetSubMeshes().size() : 0;
            if (ImGui::GetIO().KeyShift && parts > 1)
                m_MaterialSlotPick = { assetPath, target.GetUUID(), true };
            else
                ApplyMaterialToEntity(target, assetPath);
        }
    }

    void EditorLayer::DrawMaterialDragLabel(ImVec2 at, Entity target)
    {
        std::string file = fs::path(m_MaterialDrag.Path).filename().string();
        std::string title, hint;
        ImU32 accent = IM_COL32(90, 200, 255, 255);
        if (m_SceneState != SceneState::Edit)
        {
            title = ICON_FA_CIRCLE_INFO "  O'yin vaqtida material o'zgartirilmaydi";
            accent = IM_COL32(150, 150, 150, 255);
        }
        else if (!target)
        {
            title = ICON_FA_CIRCLE_INFO "  Mesh ustiga olib boring";
            hint = "Material obyektning Mesh Renderer'iga qo'yiladi";
            accent = IM_COL32(150, 150, 150, 255);
        }
        else if (!m_MaterialDrag.Material)
        {
            title = std::format(ICON_FA_TRIANGLE_EXCLAMATION "  {} yuklanmadi", file);
            accent = IM_COL32(240, 110, 100, 255);
        }
        else
        {
            title = std::format(ICON_FA_PALETTE "  {}  " ICON_FA_ARROW_RIGHT "  {}", file, target.GetName());
            auto& mr = target.GetComponent<MeshRendererComponent>();
            size_t parts = mr.Type == PrimitiveMesh::Model && mr.ModelAsset ? mr.ModelAsset->GetSubMeshes().size() : 0;
            hint = parts > 1 ? std::format("Butun model ({} qism) - Shift bilan qo'yilsa: qismni tanlash", parts)
                             : std::string("Qo'yib yuboring - material qo'llanadi");
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 pad(8.0f, 5.0f);
        ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
        ImVec2 hintSize = hint.empty() ? ImVec2(0, 0) : ImGui::CalcTextSize(hint.c_str());
        float lineGap = hint.empty() ? 0.0f : 3.0f;
        ImVec2 size(std::max(titleSize.x, hintSize.x) + pad.x * 2.0f, titleSize.y + hintSize.y + lineGap + pad.y * 2.0f);
        // Kept inside the viewport (near its right / bottom edge the label goes left / up of the mouse).
        if (at.x + size.x > m_ViewportBounds[1].x - 4.0f)
            at.x = std::max(m_ViewportBounds[0].x + 4.0f, at.x - size.x - 36.0f);
        if (at.y + size.y > m_ViewportBounds[1].y - 4.0f)
            at.y = std::max(m_ViewportBounds[0].y + 4.0f, at.y - size.y - 56.0f);
        ImVec2 max(at.x + size.x, at.y + size.y);
        dl->AddRectFilled(at, max, IM_COL32(24, 26, 32, 235), 5.0f);
        dl->AddRect(at, max, accent, 5.0f, 0, 1.5f);
        dl->AddRectFilled(at, ImVec2(at.x + 3.0f, max.y), accent, 5.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddText(ImVec2(at.x + pad.x, at.y + pad.y), IM_COL32(235, 238, 245, 255), title.c_str());
        if (!hint.empty())
            dl->AddText(ImVec2(at.x + pad.x, at.y + pad.y + titleSize.y + lineGap), IM_COL32(160, 168, 182, 255), hint.c_str());
    }

    void EditorLayer::UI_MaterialSlotPopup()
    {
        if (m_MaterialSlotPick.Open)
        {
            ImGui::OpenPopup("##materialSlots");
            m_MaterialSlotPick.Open = false;
            if (m_MaterialSlotPick.At.x >= 0.0f)
                ImGui::SetNextWindowPos(ImVec2(m_MaterialSlotPick.At.x, m_MaterialSlotPick.At.y));
        }
        if (!ImGui::BeginPopup("##materialSlots"))
            return;
        Entity target = m_SceneState == SceneState::Edit ? m_ActiveScene->GetEntityByUUID(m_MaterialSlotPick.Target) : Entity();
        auto* mr = target ? target.TryGetComponent<MeshRendererComponent>() : nullptr;
        if (!mr || !mr->ModelAsset)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        std::string file = fs::path(m_MaterialSlotPick.Path).filename().string();
        auto shown = [](const std::string& path) { return path.empty() ? std::string("-") : fs::path(path).filename().string(); };
        ImGui::TextDisabled(ICON_FA_PALETTE "  %s  " ICON_FA_ARROW_RIGHT "  %s", file.c_str(), target.GetName().c_str());
        ImGui::Separator();
        int hovered = -2;
        if (ImGui::MenuItem(Tr(ICON_FA_CUBES "  Butun model"), shown(mr->MaterialPath).c_str()))
            ApplyMaterialToEntity(target, m_MaterialSlotPick.Path);
        if (ImGui::IsItemHovered())
            hovered = -1;
        ImGui::Separator();
        const auto& subMeshes = mr->ModelAsset->GetSubMeshes();
        for (size_t i = 0; i < subMeshes.size(); i++)
        {
            ImGui::PushID((int)i);
            std::string label = std::format(ICON_FA_CUBE "  #{}  {}", i, subMeshes[i].Name);
            std::string current = i < mr->SubmeshMaterials.size() && !mr->SubmeshMaterials[i].empty() ? shown(mr->SubmeshMaterials[i]) : std::string();
            if (ImGui::MenuItem(label.c_str(), current.c_str()))
                ApplyMaterialToEntity(target, m_MaterialSlotPick.Path, (int)i);
            if (ImGui::IsItemHovered())
                hovered = (int)i;
            ImGui::PopID();
        }
        // The choice under the mouse is shown on the model before it is picked.
        if (hovered >= -1)
        {
            if (m_MaterialDrag.Path != m_MaterialSlotPick.Path)
            {
                m_MaterialDrag.Path = m_MaterialSlotPick.Path;
                m_MaterialDrag.Material = MaterialLibrary::Load(Project::GetAssetFileSystemPath(m_MaterialSlotPick.Path));
            }
            m_MaterialDrag.Target = target.GetUUID();
            m_MaterialDrag.Submesh = hovered;
            m_MaterialDrag.Active = true;
        }
        ImGui::EndPopup();
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
                if (AssetRegistry* registry = Project::GetAssetRegistry(); registry && !code)
                    registry->OnFileAdded(file); // a handle and a .zmeta right away (the watcher would follow later)
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

    void EditorLayer::StartAssetWatcher()
    {
        m_AssetHotReload.Stop();
        AssetRegistry* registry = Project::GetAssetRegistry(); // scans the asset folder (writes missing .zmeta files)
        fs::path assets = Project::GetAssetDirectory();
        std::error_code ec;
        if (!registry || !fs::is_directory(assets, ec))
            return;
        // Moved / renamed while the editor was closed (found through the last session's snapshot):
        // the scenes, prefabs and models that name the old paths are updated before anything opens.
        if (std::vector<AssetRegistry::PathMove> moves = registry->TakeDetectedMoves(); !moves.empty())
            OnAssetsMovedOutside(moves);
        m_SavedRegistryRevision = ~0ull; // another registry: saved whatever its revision
        SaveAssetRegistryCache(true);
        if (m_AssetHotReload.Start(assets))
            ZM_CORE_INFO("Assetlar kuzatilmoqda ({}): o'zgargan rasm, model, material, shrift va HDRI fayllar avtomatik qayta yuklanadi",
                         m_AssetHotReload.GetWatcher().UsesNativeEvents() ? "inotify" : "har soniyada tekshirish");
        else
            ZM_WARN("Asset papkasini kuzatib bo'lmadi: {}", assets.string());
    }

    void EditorLayer::SaveAssetRegistryCache(bool now)
    {
        AssetRegistry* registry = Project::GetActive() ? Project::GetAssetRegistry() : nullptr;
        if (!registry || registry->GetRevision() == m_SavedRegistryRevision)
            return;
        double time = ImGui::GetTime();
        if (!now && time - m_RegistrySaveTime < 3.0)
            return;
        m_RegistrySaveTime = time;
        m_SavedRegistryRevision = registry->GetRevision();
        if (!Project::SaveAssetRegistryCache())
            ZM_CORE_WARN("Asset registry keshi saqlanmadi: {}", Project::GetAssetRegistryCachePath().string());
    }

    void EditorLayer::ProcessAssetChanges()
    {
        if (!m_AssetHotReload.IsRunning())
            return;
        SaveAssetRegistryCache(false);
        AssetHotReload::Report report = m_AssetHotReload.Update(Project::GetAssetRegistry());
        if (report.IsEmpty())
            return;
        m_AssetUsage.MarkDirty(); // scenes saved, files added / removed / renamed
        m_SceneStatsDirty = true;  // the files the statistics read may have changed

        for (const std::string& problem : report.Problems)
            ZM_WARN("{}", problem);
        if (!report.Moved.empty())
            OnAssetsMovedOutside(report.Moved);
        // Language files edited outside (or saved by the localization table): the texts follow.
        {
            std::error_code ec;
            const fs::path languages = fs::weakly_canonical(Project::GetLocalizationDirectory(), ec);
            bool languageFiles = false;
            for (const auto* list : { &report.Added, &report.Modified, &report.Removed })
                for (const fs::path& p : *list)
                    if (p.extension() == ".json" && fs::weakly_canonical(p, ec).parent_path() == languages)
                        languageFiles = true;
            if (languageFiles)
            {
                Localization::Reload();
                m_LocalizationPanel.OnFilesChanged();
                ZM_INFO("Til fayllari qayta yuklandi");
            }
        }
        for (const auto* list : { &report.Added, &report.Modified, &report.Removed })
            for (const fs::path& p : *list)
                m_ContentBrowserPanel.InvalidateThumbnail(p);
        if (report.HasStructureChanges() || !report.Modified.empty())
            m_ContentBrowserPanel.MarkListDirty(); // new / gone files, sizes and dates

        // A file that appeared where a scene already points, or one that could not be loaded
        // before and is fixed now (nothing loaded to update in place): the scenes that use it
        // look their assets up again.
        std::vector<Ref<Scene>> scenes;
        for (const Ref<Scene>& scene : { m_EditorScene, m_ActiveScene })
            if (scene && std::find(scenes.begin(), scenes.end(), scene) == scenes.end())
                scenes.push_back(scene);
        std::vector<std::string> sceneTexts;
        for (const auto* list : { &report.Added, &report.Modified })
            for (const fs::path& changed : *list)
            {
                AssetType type = AssetFiles::GetType(changed);
                bool resolvable = type == AssetType::Texture || type == AssetType::Model || type == AssetType::Font ||
                                  type == AssetType::Environment || type == AssetType::Material;
                if (!resolvable || std::find(report.Reloaded.begin(), report.Reloaded.end(), changed) != report.Reloaded.end() ||
                    std::find(report.Failed.begin(), report.Failed.end(), changed) != report.Failed.end())
                    continue;
                if (sceneTexts.empty())
                    for (const Ref<Scene>& scene : scenes)
                        sceneTexts.push_back(SceneSerializer(scene).SerializeToString());
                std::string needle = "\"" + Project::GetAssetRelativePath(changed) + "\"";
                for (size_t i = 0; i < scenes.size(); i++)
                    if (scenes[i] && sceneTexts[i].find(needle) != std::string::npos)
                    {
                        scenes[i]->ResolveAssets();
                        scenes[i] = nullptr; // once is enough
                    }
            }

        for (const fs::path& p : report.Reloaded)
            ZM_INFO("Qayta yuklandi: {}", Project::GetAssetRelativePath(p));
        if (!report.Failed.empty())
            m_ContentBrowserPanel.ShowNotice(std::format("'{}' qayta yuklanmadi - konsolni tekshiring", report.Failed.front().filename().string()), true);
        else if (report.Reloaded.size() == 1)
            m_ContentBrowserPanel.ShowNotice(std::format("'{}' qayta yuklandi", report.Reloaded.front().filename().string()));
        else if (report.Reloaded.size() > 1)
            m_ContentBrowserPanel.ShowNotice(std::format("{} ta asset qayta yuklandi", report.Reloaded.size()));
    }

    void EditorLayer::OpenSpriteEditor(const std::string& texturePath)
    {
        m_ShowSpriteEditor = true;
        m_SpriteEditorPanel.Open(texturePath);
    }

    void EditorLayer::RevealAsset(const fs::path& path)
    {
        m_ContentBrowserPanel.Reveal({ path });
        m_AssetInspectorPanel.SetAsset(path);
        m_AssetInspectorPanel.SetSelection({ path });
        m_SceneHierarchyPanel.AcknowledgeSelection();
        m_ShowInspector = true;
    }

    void EditorLayer::OnAssetsMovedOutside(const std::vector<std::pair<std::string, std::string>>& moves)
    {
        // Renamed / moved in a file manager or by version control, the .zmeta files with them (the
        // registry recognized the UUIDs): the same updates as a move in the content browser.
        std::error_code ec;
        fs::path assets = fs::weakly_canonical(Project::GetAssetDirectory(), ec);
        if (ec)
            assets = Project::GetAssetDirectory();
        AssetFiles::MoveResult result;
        for (const auto& [from, to] : moves)
        {
            result.Moved.push_back({ assets / from, assets / to, false });
            ZM_INFO("Asset tashqarida ko'chirildi: {} -> {}", from, to);
        }
        AssetFiles::UpdateReferences(result, assets, Project::GetSourceDirectory());
        OnAssetsMoved(result);
        if (result.UpdatedReferences > 0)
            ZM_INFO("{} ta faylda {} ta havola yangilandi", result.UpdatedFiles.size(), result.UpdatedReferences);
        for (const fs::path& source : result.SourceMentions)
            ZM_WARN("C++ kodda eski yo'l yozilgan: {}", source.filename().string());
        m_ContentBrowserPanel.ShowNotice(moves.size() == 1
            ? std::format("'{}' ko'chirilgani aniqlandi - havolalar yangilandi", fs::path(moves.front().second).filename().string())
            : std::format("{} ta asset ko'chirilgani aniqlandi - havolalar yangilandi", moves.size()));
    }

    void EditorLayer::OnAssetsMoved(const AssetFiles::MoveResult& result)
    {
        if (result.Moved.empty())
            return;
        m_AssetUsage.MarkDirty();
        std::error_code ec;
        fs::path assets = fs::weakly_canonical(Project::GetAssetDirectory(), ec);

        // The registry follows at once (handles stay; the watcher's events for the move are no-ops then).
        if (AssetRegistry* registry = Project::GetAssetRegistry())
            for (const auto& m : result.Moved)
                registry->OnFileMoved(m.From, m.To);
        // Loaded materials follow their files (the same objects: the open scene finds them by the new
        // path), and the ones whose texture paths were just rewritten read their file again.
        for (const auto& m : result.Moved)
        {
            MaterialLibrary::OnFileMoved(m.From, m.To);
            PhysicsMaterialLibrary::OnFileMoved(m.From, m.To);
            PropertyClipLibrary::OnFileMoved(m.From, m.To);
            SpriteClipLibrary::OnFileMoved(m.From, m.To);
        }
        for (const fs::path& file : result.UpdatedFiles)
            if (AssetFiles::GetType(file) == AssetType::Material)
                MaterialLibrary::ReloadFile(file);

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
                        SetActiveScene(m_EditorScene, false);
                    ZM_INFO("Ochiq sahnada {} ta havola yangilandi", n);
                }
            }
            // Undo / redo snapshots and the entity clipboard use the new paths, too.
            m_UndoHistory.RewriteScenes([&](std::string& snapshot) { remap(snapshot); });
            m_AutosavedScene.reset(); // the next copy has the new paths
            remap(m_Clipboard);
        }

        // The open scene file itself (or its folder) moved.
        if (!m_EditorScenePath.empty())
        {
            fs::path current = fs::weakly_canonical(m_EditorScenePath, ec);
            fs::path moved = result.MapPath(current);
            if (moved != current)
            {
                DiscardSessionAutosave(); // under the old path; the next copy goes under the new one
                m_EditorScenePath = moved;
                UpdateWindowTitle();
            }
        }
        for (const auto& m : result.Moved)
            if (m_ViewSettings.MoveSceneBookmarks(m.From, m.To) > 0)
                MarkViewSettingsDirty();
        // Recent scenes follow their files (folders too).
        size_t renamed = 0;
        for (const auto& m : result.Moved)
            renamed += m_RecentScenes.Rename(m.From.string(), m.To.string());
        if (renamed > 0)
            SaveRecentScenes();

        // The project's start scene and the game's icon.
        if (auto project = Project::GetActive())
        {
            auto& start = project->GetConfig().StartScene;
            for (const auto& [from, to] : moves)
                if (auto mapped = AssetFiles::RemapPath(start.generic_string(), from, to))
                {
                    start = *mapped;
                    SaveProjectSettingsNow();
                    ZM_INFO("Boshlang'ich sahna: {}", *mapped);
                    break;
                }
            auto& icon = project->GetConfig().Player.Icon;
            if (!icon.empty())
                for (const auto& [from, to] : moves)
                    if (auto mapped = AssetFiles::RemapPath(icon.generic_string(), from, to))
                    {
                        icon = *mapped;
                        SaveProjectSettingsNow();
                        ZM_INFO("O'yin ikonkasi: {}", *mapped);
                        break;
                    }
        }
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
        BeginBuildOutput(buildDir);
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
                    AddBuildOutputLine(s); // the "Kompilyatsiya" window's messages
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
    void EditorLayer::ReportExcludedAssets(const ExportAssetPlan& plan, const std::string& gameName, const fs::path& outputDirectory)
    {
        const fs::path report = outputDirectory / ExportAssets::ReportFileName;
        std::error_code ec;
        if (plan.Excluded.empty())
        {
            fs::remove(report, ec);
            ZM_INFO("Eksport: {} ta asset fayli ({}), hammasi ishlatiladi", plan.Included.size(), ExportAssets::FormatSize(plan.IncludedBytes));
            return;
        }
        ZM_INFO("Eksport: {} ta asset fayli ({}); {} tasi chiqarib tashlandi ({}) - ro'yxat: {}", plan.Included.size(),
                ExportAssets::FormatSize(plan.IncludedBytes), plan.Excluded.size(), ExportAssets::FormatSize(plan.ExcludedBytes), report.string());
        const size_t shown = std::min<size_t>(plan.Excluded.size(), 12);
        for (size_t i = 0; i < shown; i++)
            ZM_INFO("    - {} ({}, {})", plan.Excluded[i].Path, ExportAssets::FormatSize(plan.Excluded[i].Size), plan.Excluded[i].Reason);
        if (plan.Excluded.size() > shown)
            ZM_INFO("    ... va yana {} ta", plan.Excluded.size() - shown);
        FileSystem::WriteTextFile(report, plan.FormatReport(gameName));
    }

    void EditorLayer::BuildGame(const fs::path& outputDirectory)
    {
        auto project = Project::GetActive();
        if (!project || m_Exporting) return;
        if (m_SceneState != SceneState::Edit) OnSceneStop();
        if (m_SceneDirty) SaveScene();
        m_ProjectSettingsPanel.SaveIfDirty(); // the exported game reads the .zproject (input map)

        const auto started = std::chrono::steady_clock::now();
        std::string name = project->GetConfig().Name;
        const PlayerSettings player = project->GetConfig().Player;
        const std::string productName = player.GetProductName(name);
        // The report the export ends with (the window, export-report.json / .txt): what went wrong is
        // collected here and on the export thread.
        auto report = std::make_shared<ExportReport>();
        {
            std::error_code ec;
            report->Game = productName;
            report->Version = player.Version;
            report->Company = player.CompanyName;
            report->OutputDirectory = fs::absolute(outputDirectory, ec).lexically_normal().string();
            const std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_r(&now, &tm);
            char date[32];
            std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);
            report->Date = date;
        }
        // (Only the export thread uses these from here on: the main thread reads the report once it is handed over.)
        auto warn = [report](std::string message) {
            ZM_WARN("Eksport: {}", message);
            report->Warnings.push_back(std::move(message));
        };
        auto fail = [report](std::string message) {
            ZM_ERROR("Eksport: {}", message);
            report->Errors.push_back(std::move(message));
        };
        // The start scene and the build list: what would break the game is an error, the rest warnings.
        for (const BuildSceneIssue& issue : Project::CheckBuildScenes(project->GetConfig(), Project::GetAssetDirectory()))
        {
            if (issue.Error)
                fail(issue.Message);
            else
                warn(issue.Message);
        }
        const fs::path iconFile = Project::GetIconPath();
        if (!iconFile.empty())
        {
            ImageData icon;
            std::string error;
            if (!GameIcon::Load(iconFile, icon, &error))
                warn(std::format("o'yin ikonkasi o'qilmadi ({}) - .desktop faylida ikonka bo'lmaydi", error));
        }
        fs::path projectDir = Project::GetProjectDirectory();
        fs::path projectFile = Project::GetProjectFilePath();
        fs::path modulePath = Project::GetScriptModulePath();
        fs::path moduleRel = project->GetConfig().ScriptModulePath;
        fs::path assetRel = project->GetConfig().AssetDirectory;
        fs::path exeDir = FileSystem::GetExecutableDirectory();
        const ExportSettings exportSettings = project->GetConfig().Export;
        // The script module in Release (-O2) in a build folder of its own: needs the project's CMakeLists.txt
        // (written like BuildScripts does for a project without one) and its sources.
        std::error_code sourcesError;
        const bool hasSources = !modulePath.empty() && fs::is_directory(Project::GetSourceDirectory(), sourcesError);
        if (exportSettings.ReleaseScriptModule && hasSources && !fs::exists(projectDir / "CMakeLists.txt"))
            Project::WriteBuildFiles(projectDir, name);
        ExportModuleBuild moduleBuild;
        moduleBuild.ProjectDirectory = projectDir;
        moduleBuild.BuildDirectory = ExportModuleBuild::DefaultBuildDirectory(projectDir);
        moduleBuild.EngineDirectory = exeDir.parent_path();
        moduleBuild.OutputDirectory = moduleBuild.BuildDirectory / "bin";
        moduleBuild.Ninja = std::system("command -v ninja >/dev/null 2>&1") == 0;
        // Which assets go: with a build list only its enabled scenes, the start scene and their sub-scenes;
        // then what they lead to (prefabs, materials, models, clips ...), what the project file and the C++
        // code name, the folders always taken - the rest stays out (Project settings > Build scenes > Export).
        // Planned here, on this thread (the registry).
        std::vector<std::string> buildScenes, notes;
        const ExportAssetPlan assetPlan = ExportAssets::PlanForActiveProject(&buildScenes, &notes);
        for (const std::string& note : notes)
            ZM_INFO("Eksport: {}", note);
        const bool binaryScenes = m_BuildGameBinaryScenes;
        // The exported game reads its asset registry from one file (made here, on this thread) - without the
        // assets left out.
        std::string registryText;
        if (AssetRegistry* registry = Project::GetAssetRegistry())
        {
            AssetRegistry::Snapshot snapshot = registry->GetSnapshot();
            std::erase_if(snapshot, [&](const AssetRegistry::SnapshotEntry& e) { return assetPlan.IsExcluded(e.Path); });
            registryText = AssetRegistry::SerializeSnapshot(snapshot);
        }

        if (m_ExportThread.joinable()) m_ExportThread.join();
        m_Exporting = true;
        m_ExportThread = std::thread([=, this]() {
            std::error_code ec;
            auto copyOpts = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
            fs::create_directories(outputDirectory / "Game", ec);

            bool ok = true;
            auto copy = [&](const fs::path& from, const fs::path& to) {
                std::error_code e;
                if (!fs::exists(from)) { fail(std::format("topilmadi: {}", from.string())); ok = false; return; }
                fs::create_directories(to.parent_path(), e);
                fs::copy(from, to, copyOpts, e);
                if (e) { fail(std::format("nusxalashda xato {}: {}", from.string(), e.message())); ok = false; }
            };

            // The binaries: the player, the engine (and later the script module) - stripped below.
            std::vector<fs::path> binaries;
            copy(exeDir / "ZaminPlayer", outputDirectory / "ZaminPlayer");
            binaries.push_back(outputDirectory / "ZaminPlayer");
            for (auto& entry : fs::directory_iterator(exeDir, ec))
                if (entry.path().filename().string().starts_with("libZamin"))
                {
                    copy(entry.path(), outputDirectory / entry.path().filename());
                    binaries.push_back(outputDirectory / entry.path().filename());
                }
            copy(exeDir / "Resources", outputDirectory / "Resources");
            copy(projectFile, outputDirectory / "Game" / projectFile.filename());
            {
                // The assets of the plan, each with its .zmeta (a folder exported before loses what is left out now).
                const fs::path assets = outputDirectory / "Game" / assetRel;
                std::error_code e;
                if (!fs::exists(assets, e) || !fs::equivalent(assets, projectDir / assetRel, e))
                    fs::remove_all(assets, e);
                for (const std::string& file : assetPlan.Included)
                {
                    copy(projectDir / assetRel / file, assets / file);
                    const fs::path meta = projectDir / assetRel / (file + ".zmeta");
                    if (fs::exists(meta, e))
                        copy(meta, assets / (file + ".zmeta"));
                }
                if (!buildScenes.empty())
                    ZM_INFO("Eksport: build ro'yxatidagi {} ta sahna", buildScenes.size());
                ReportExcludedAssets(assetPlan, name, outputDirectory);
            }
            if (binaryScenes)
            {
                // Every scene of the game also as a binary cache next to it (smaller, already in the
                // current format): the player reads that one instead (SceneSerializer).
                const fs::path assets = outputDirectory / "Game" / assetRel;
                size_t written = 0, textBytes = 0, binaryBytes = 0;
                for (auto it = fs::recursive_directory_iterator(assets, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    if (it->path().extension() != ".zscene")
                        continue;
                    std::string error;
                    std::error_code e;
                    if (SceneSerializer::WriteBinaryCache(it->path(), &error))
                    {
                        written++;
                        textBytes += (size_t)fs::file_size(it->path(), e);
                        binaryBytes += (size_t)fs::file_size(SceneSerializer::GetBinaryCachePath(it->path()), e);
                    }
                    else
                        warn(std::format("sahnaning binar keshi yozilmadi ({}): {}", error, it->path().filename().string()));
                }
                if (written)
                    ZM_INFO("Eksport: {} ta sahna binar keshga yozildi ({:.1f} KB -> {:.1f} KB)", written, (double)textBytes / 1024.0,
                            (double)binaryBytes / 1024.0);
            }
            if (!registryText.empty() && !FileSystem::WriteTextFile(outputDirectory / "Game" / Project::BuildAssetRegistryFile, registryText))
            {
                fail("asset registri yozilmadi");
                ok = false;
            }

            // The script module: compiled again in Release (-O2, its own build folder) - the editor's module
            // when that is off or fails. (None at all: the game is exported without scripts - a warning.)
            bool moduleExported = false;
            if (!modulePath.empty())
            {
                fs::path module = modulePath;
                std::string how = "editor moduli nusxalandi";
                if (exportSettings.ReleaseScriptModule && hasSources)
                {
                    ZM_INFO("Eksport: skript moduli Release'da kompilyatsiya qilinmoqda ({}): {}", moduleBuild.OptimizationFlags,
                            moduleBuild.BuildDirectory.string());
                    const auto buildStart = std::chrono::steady_clock::now();
                    std::string firstError;
                    auto onLine = [&](const std::string& line) {
                        if (line.empty())
                            return;
                        if (firstError.empty() && (line.find("error") != std::string::npos || line.find("Error") != std::string::npos))
                            firstError = line;
                        ZM_TRACE("{}", line);
                    };
                    int code = RunShellCommand(moduleBuild.MakeConfigureCommand(), onLine);
                    if (code == 0)
                        code = RunShellCommand(moduleBuild.MakeBuildCommand(), onLine);
                    // lib<Name>.so: the project's module name, else the only module the build made.
                    fs::path built = moduleBuild.OutputDirectory / modulePath.filename();
                    std::error_code e;
                    if (code == 0 && !fs::exists(built, e))
                        for (const auto& entry : fs::directory_iterator(moduleBuild.OutputDirectory, e))
                            if (entry.path().extension() == ".so")
                                built = entry.path();
                    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart).count();
                    if (code == 0 && fs::exists(built, e))
                    {
                        module = built;
                        how = std::format("Release ({}), {:.1f} s da kompilyatsiya qilindi", moduleBuild.OptimizationFlags, seconds);
                        ZM_INFO("Eksport: skript moduli {} ({})", how, ExportAssets::FormatSize(fs::file_size(built, e)));
                    }
                    else
                    {
                        how = "editor moduli nusxalandi (Release kompilyatsiya bo'lmadi)";
                        warn(std::format("skript moduli Release'da kompilyatsiya qilinmadi{} - editor moduli nusxalandi",
                                         firstError.empty() ? std::format(" (chiqish kodi {})", code) : ": " + firstError));
                    }
                }
                if (fs::exists(module))
                {
                    copy(module, outputDirectory / "Game" / moduleRel);
                    binaries.push_back(outputDirectory / "Game" / moduleRel);
                    report->ScriptModule = how;
                    moduleExported = true;
                }
                else
                {
                    warn("skript moduli topilmadi - o'yin skriptlarsiz eksport qilinadi");
                    report->ScriptModule = "yo'q (skriptsiz)";
                }
            }
            else
                report->ScriptModule = "yo'q (loyihada skript moduli yo'q)";

            // The binaries without their debug information and local symbols (much smaller).
            if (exportSettings.StripBinaries)
            {
                if (!BinaryStrip::IsAvailable())
                    warn("strip dasturi topilmadi (binutils) - binarlar kichraytirilmadi");
                else
                {
                    uintmax_t before = 0, after = 0;
                    size_t stripped = 0;
                    for (const fs::path& binary : binaries)
                    {
                        std::error_code e;
                        if (!fs::is_regular_file(binary, e) || !BinaryStrip::IsElfFile(binary))
                            continue;
                        const StripResult result = BinaryStrip::Strip(binary);
                        if (!result.Ok)
                        {
                            warn(std::format("strip: {} - {}", binary.filename().string(), result.Error));
                            continue;
                        }
                        before += result.SizeBefore;
                        after += result.SizeAfter;
                        stripped++;
                    }
                    report->StrippedBytes = before > after ? before - after : 0;
                    if (stripped)
                        ZM_INFO("Eksport: strip - {} ta binar {} -> {}", stripped, ExportAssets::FormatSize(before), ExportAssets::FormatSize(after));
                }
            }

            // (It also points the desktop entry next to it at the icon in the folder it runs from: below.)
            fs::path launcher = outputDirectory / (name + ".sh");
            FileSystem::WriteTextFile(launcher, ExportFiles::MakeLauncherScript("Game/" + projectFile.filename().string(), name + ".desktop",
                                                                                iconFile.empty() ? std::string() : name + ".png"));
            fs::permissions(launcher, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                          fs::perms::others_read | fs::perms::others_exec, ec);

            // The game's icon next to the launcher (the PNG as it is, and icons/hicolor/<size>/apps/<id>.png in
            // every size up to its own) and a desktop entry that works wherever the folder goes (its Exec runs
            // the launcher next to the entry itself; the icon path is where the game was exported to - the
            // launcher points it at the new place when the game starts from there).
            // install-desktop-entry.sh puts the game into the application menus for wherever the folder is
            // (absolute paths, the icons into the user's icon theme).
            const std::string entryId = ExportFiles::MakeDesktopEntryId(name);
            const fs::path absoluteOut = fs::absolute(outputDirectory, ec);
            fs::path exportedIcon;
            fs::remove_all(outputDirectory / "icons", ec); // the sizes of an earlier export
            if (!iconFile.empty())
            {
                ImageData icon;
                if (GameIcon::Load(iconFile, icon))
                {
                    exportedIcon = absoluteOut / (name + ".png");
                    copy(iconFile, exportedIcon);
                    std::string iconError;
                    const std::vector<uint32_t> sizes = ExportFiles::WriteIconSet(icon, outputDirectory, entryId, &iconError);
                    if (sizes.empty())
                        warn(std::format("ikonka o'lchamlari yozilmadi: {}", iconError));
                    else
                        ZM_INFO("Eksport: ikonka {} o'lchamda ({} ... {} px, icons/hicolor)", sizes.size(), sizes.front(), sizes.back());
                }
            }
            if (!FileSystem::WriteTextFile(outputDirectory / (name + ".desktop"),
                                           ExportFiles::MakeRelocatableDesktopEntry(productName, player, launcher.filename().string(), exportedIcon)))
            {
                fail(std::format("{}.desktop yozilmadi", name));
                ok = false;
            }
            else
                fs::permissions(outputDirectory / (name + ".desktop"), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                                fs::perm_options::add, ec);
            const fs::path installScript = outputDirectory / ExportFiles::DesktopInstallScriptName;
            if (!FileSystem::WriteTextFile(installScript,
                                           ExportFiles::MakeDesktopInstallScript(productName, player, launcher.filename().string(),
                                                                                 exportedIcon.empty() ? std::string() : exportedIcon.filename().string(),
                                                                                 entryId)))
            {
                fail(std::format("{} yozilmadi", ExportFiles::DesktopInstallScriptName));
                ok = false;
            }
            else
                fs::permissions(installScript, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                                                   fs::perms::others_exec, ec);

            // The folder checks itself: everything the exported game needs to start is there (the launcher, the
            // player, the engine, the project, its start / build scenes, the registry, the script module - when
            // one was exported: a game exported without scripts was a warning above).
            for (const std::string& problem : ExportFiles::CheckExportFolder(outputDirectory, name, moduleExported))
            {
                fail(std::format("eksport to'liq emas - {}", problem));
                ok = false;
            }

            // The report: what the folder holds now, what was left out, how long it took and what went wrong.
            report->SetExcluded(assetPlan);
            report->ScanDirectory(outputDirectory);
            report->Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            report->Succeeded = ok && report->Errors.empty();
            if (!report->Write(outputDirectory))
                ZM_WARN("Eksport hisoboti yozilmadi: {}", (outputDirectory / ExportReport::JsonFileName).string());
            ZM_INFO("Eksport hisoboti: {} ta fayl, {} ({} ogohlantirish, {} xato) - {}", report->FileCount, ExportAssets::FormatSize(report->TotalBytes),
                    report->Warnings.size(), report->Errors.size(), (outputDirectory / ExportReport::TextFileName).string());

            if (report->Succeeded) ZM_INFO("O'yin eksport qilindi " ICON_FA_ROCKET " {}  (ishga tushirish: {})", outputDirectory.string(), launcher.string());
            else ZM_ERROR("O'yinni eksport qilishda xatolar bo'ldi");
            {
                std::lock_guard<std::mutex> lock(m_ExportReportMutex);
                m_PendingExportReport = std::make_unique<ExportReport>(*report);
            }
            m_Exporting = false;
        });
    }

    // ================================================================== ImGui
    void EditorLayer::OnImGuiRender()
    {
        // ---- keyboard shortcuts (EditorShortcuts.cpp; ignored while typing)
        HandleShortcuts();

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
        if (m_ShowUnusedAssets && !m_UnusedAssetsShown)
            m_UnusedAssetsPanel.Refresh(); // opened: a fresh scan (C++ files are not watched)
        m_UnusedAssetsShown = m_ShowUnusedAssets;
        m_UnusedAssetsPanel.OnImGuiRender(&m_ShowUnusedAssets);
        UI_SceneStatistics();
        UI_BuildOutput();
        m_AssetInspectorPanel.DrawPopups();
        m_ConsolePanel.OnImGuiRender(&m_ShowConsole);
        UI_SceneSettings();
        m_ScenesPanel.SetSceneManager(m_SceneState == SceneState::Play ? m_RuntimeScenes.get() : nullptr);
        m_ScenesPanel.SetShownScene(m_ActiveScene.get());
        m_ScenesPanel.SetOpenScenePath(m_EditorScenePath);
        if (m_ScenesPanelDirty)
        {
            m_ScenesPanel.MarkDirty();
            m_ScenesPanelDirty = false;
        }
        if (ImGuiWindow* content = ImGui::FindWindowByName("###ContentBrowser"); content && content->DockId)
            m_ScenesPanel.SetDefaultDock(content->DockId); // first opened: a tab next to the content browser
        m_ScenesPanel.OnImGuiRender(&m_ShowScenes);
        // ZAMIN_EDITOR_FOCUS=<window id, e.g. ContentBrowser / Console / ScenesPanel>: that window's tab in
        // front (screenshots), from frame 8 on for a few frames (docked tabs settle first).
        if (static int focusFrames = 0; focusFrames < 12)
            if (const char* focus = std::getenv("ZAMIN_EDITOR_FOCUS"); focus && ++focusFrames >= 8)
                ImGui::SetWindowFocus(std::format("###{}", focus).c_str());
        m_ProjectSettingsPanel.SetScene(m_EditorScene); // tag counts / renames apply to the saved scene
        m_ProjectSettingsPanel.SetOpenScenePath(m_EditorScenePath.empty() ? fs::path() : fs::path(Project::GetAssetRelativePath(m_EditorScenePath)));
        m_ProjectSettingsPanel.SetSceneEditable(m_SceneState == SceneState::Edit);
        m_ProjectSettingsPanel.OnImGuiRender(&m_ShowProjectSettings);
        m_SpriteEditorPanel.OnImGuiRender(&m_ShowSpriteEditor);
        m_AnimationPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
        m_AnimationPanel.SetSelection(m_SceneHierarchyPanel.GetSelectedEntity());
        if (ImGuiWindow* content = ImGui::FindWindowByName("###ContentBrowser"); content && content->DockId)
            m_AnimationPanel.SetDefaultDock(content->DockId); // first opened: a tab next to the content browser
        m_AnimationPanel.OnImGuiRender(&m_ShowAnimation);
        m_AnimatorGraphPanel.SetContext(m_ActiveScene, m_SceneState == SceneState::Edit);
        m_AnimatorGraphPanel.SetSelection(m_SceneHierarchyPanel.GetSelectedEntity());
        if (ImGuiWindow* dock = ImGui::FindWindowByName(m_AnimatorGraphInViewport ? "###Viewport" : "###ContentBrowser"); dock && dock->DockId)
            m_AnimatorGraphPanel.SetDefaultDock(dock->DockId); // first opened: a tab next to the content browser
        m_AnimatorGraphPanel.OnImGuiRender(&m_ShowAnimatorGraph);
        m_AudioMixerPanel.SetPlaying(m_SceneState == SceneState::Play);
        m_AudioMixerPanel.OnImGuiRender(&m_ShowAudioMixer);
        m_LocalizationPanel.SetScene(m_ActiveScene);
        m_LocalizationPanel.OnImGuiRender(&m_ShowLocalization);
        m_SceneHierarchyPanel.DrawTilePaletteWindow(&m_ShowTilePalette);
        UI_Stats();
        UI_ProfilerWindow();
        UI_Viewport();
        UI_SceneViews();
        if (m_MaximizeOnPlay && m_SceneState == SceneState::Play)
            UI_MaximizedGame();
        UpdateGameCursor();
        if (m_ShowImGuiDemo) ImGui::ShowDemoWindow(&m_ShowImGuiDemo);

        UI_Preferences();
        UI_LayoutWindows();
        UI_UndoHistory();
        UI_ShortcutsWindow();
        UI_Modals();
        UI_AutosaveRecovery();
        UI_SceneLoadReport();
        if (m_ShowLauncher)
            UI_Launcher();

        UpdateUndoTracking();
        UpdateAutosave();
        if (m_ViewSettingsDirty && !ImGui::IsAnyItemActive())
            SaveViewSettings();
        if (m_ConsolePanel.ConsumePreferencesChanged())
            MarkPreferencesDirty();
        if (m_PreferencesDirty && !ImGui::IsAnyItemActive())
            SavePreferences();

        static bool lastDirty = false;
        if (lastDirty != m_SceneDirty)
        {
            lastDirty = m_SceneDirty;
            UpdateWindowTitle();
        }
        UpdateIdleThrottle(); // after the UI: what is being dragged / typed into is known
    }

    void EditorLayer::UI_DockSpace()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiID dockspaceID = ImGui::GetID("ZaminDockSpace");
        // The first run (no layout file yet) or a built-in layout chosen (Oyna > Joylashuv): its
        // dock tree is built before the dock space and the windows of this frame.
        const bool firstRun = !m_LayoutInitialized;
        if (firstRun && !m_PendingBuiltInLayout)
            m_PendingBuiltInLayout = BuiltInLayout::Default;
        if (m_PendingBuiltInLayout)
        {
            const BuiltInLayout layout = *m_PendingBuiltInLayout;
            m_PendingBuiltInLayout.reset();
            UI_BuildLayout(layout, dockspaceID);
            m_LayoutInitialized = true;
            m_Preferences.Layout.Current = EditorLayoutLibrary::GetBuiltInKey(layout);
            MarkPreferencesDirty();
            if (!firstRun)
            {
                ZM_INFO("Joylashuv qo'llandi: {}", EditorLayoutLibrary::GetBuiltInName(layout));
                ShowViewportMessage(std::format(ICON_FA_WINDOW_RESTORE "  Joylashuv: {}", EditorLayoutLibrary::GetBuiltInName(layout)));
            }
        }
        DockPendingSceneViews();
        ImGui::DockSpaceOverViewport(dockspaceID, viewport, ImGuiDockNodeFlags_None);
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
        if (m_OpenMenu.starts_with("Fayl"))
            ImGui::OpenPopup(Tr("Fayl"));
        if (ImGui::BeginMenu(Tr("Fayl")))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_FILE_CIRCLE_PLUS "  Yangi loyiha..."))) m_ShowLauncher = true;
            if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Loyihani ochish..."), ShortcutLabel(EditorShortcut::OpenProject))) OpenProjectDialog();
            if (ImGui::BeginMenu(Tr(ICON_FA_LIST "  So'nggi loyihalar"), !m_RecentProjects.IsEmpty()))
            {
                std::optional<fs::path> picked;
                for (const RecentFileList::Entry& e : m_RecentProjects.GetEntries())
                {
                    const fs::path p(e.Path);
                    if (ImGui::MenuItem(std::format("{}##{}", p.stem().string(), e.Path).c_str(), p.parent_path().string().c_str()))
                        picked = p;
                }
                ImGui::EndMenu();
                if (picked)
                    OpenProject(*picked);
            }
            if (ImGui::MenuItem(Tr(ICON_FA_GEAR "  Loyiha sozlamalari..."), nullptr, false, hasProject))
            {
                m_ShowProjectSettings = true;
                m_ProjectSettingsPanel.Focus();
            }
            if (ImGui::MenuItem(Tr(ICON_FA_XMARK "  Loyihani yopish"), nullptr, false, hasProject)) CloseProject();
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_FILE "  Yangi sahna"), ShortcutLabel(EditorShortcut::NewScene))) NewScene();
            if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Sahnani ochish..."), ShortcutLabel(EditorShortcut::OpenScene))) OpenScene();
            UI_RecentScenesMenu();
            if (ImGui::MenuItem(Tr(ICON_FA_FLOPPY_DISK "  Saqlash"), ShortcutLabel(EditorShortcut::SaveScene))) SaveScene();
            if (ImGui::MenuItem(Tr(ICON_FA_FLOPPY_DISK "  Boshqacha saqlash..."), ShortcutLabel(EditorShortcut::SaveSceneAs))) SaveSceneAs();
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_ROCKET "  O'yinni build qilish..."), nullptr, false, hasProject))
            {
                m_BuildGameOutput = (Project::GetProjectDirectory() / "Build" / "Game").string();
                m_OpenBuildGamePopup = true;
            }
            // The last export's report (this session's, else the one in the default build folder).
            if (ImGui::MenuItem(Tr(ICON_FA_CHART_PIE "  Eksport hisoboti"), nullptr, false, hasProject && !m_Exporting))
            {
                if (m_HasExportReport)
                    m_ShowExportReport = true;
                else if (!OpenExportReport(m_BuildGameOutput.empty() ? Project::GetProjectDirectory() / "Build" / "Game" : fs::path(m_BuildGameOutput)))
                    ZM_WARN("Eksport hisoboti yo'q: avval o'yinni build qiling (Fayl > O'yinni build qilish)");
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_XMARK "  Chiqish"), "Alt+F4")) Application::Get().Close();
            ImGui::EndMenu();
        }

        if (m_OpenMenu.starts_with("Tahrirlash"))
            ImGui::OpenPopup(Tr("Tahrirlash"));
        if (ImGui::BeginMenu(Tr("Tahrirlash")))
        {
            bool sel = (bool)m_SceneHierarchyPanel.GetSelectedEntity();
            bool editing = m_SceneState == SceneState::Edit;
            size_t count = m_SceneHierarchyPanel.GetSelectionCount();
            std::string suffix = count > 1 ? std::format(" ({} {})", count, Tr("ta")) : std::string();
            {
                // The steps' names: "Bekor qilish: Transform o'zgardi (Player)".
                const std::string& undoName = m_UndoHistory.GetUndoName();
                const std::string& redoName = m_UndoHistory.GetRedoName();
                const std::string undoLabel = m_PendingUndo ? std::string(Tr(ICON_FA_ROTATE_LEFT "  Bekor qilish (oxirgi o'zgarish)"))
                                            : undoName.empty() ? std::string(Tr(ICON_FA_ROTATE_LEFT "  Bekor qilish (Undo)"))
                                                               : std::format(ICON_FA_ROTATE_LEFT "  {}: {}", EditorText::Choose("Bekor qilish", "Undo"), undoName);
                const std::string redoLabel = redoName.empty() ? std::string(Tr(ICON_FA_ROTATE_RIGHT "  Qaytarish (Redo)"))
                                                               : std::format(ICON_FA_ROTATE_RIGHT "  {}: {}", EditorText::Choose("Qaytarish", "Redo"), redoName);
                if (ImGui::MenuItem((undoLabel + "##undo").c_str(), ShortcutLabel(EditorShortcut::Undo), false, editing && (m_UndoHistory.CanUndo() || m_PendingUndo))) Undo();
                if (ImGui::MenuItem((redoLabel + "##redo").c_str(), ShortcutLabel(EditorShortcut::Redo), false, editing && m_UndoHistory.CanRedo())) Redo();
                if (ImGui::MenuItem(Tr(ICON_FA_CLOCK_ROTATE_LEFT "  Undo tarixi..."), ShortcutLabel(EditorShortcut::UndoHistory), m_ShowUndoHistory))
                {
                    m_ShowUndoHistory = !m_ShowUndoHistory;
                    m_UndoHistoryScroll = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem((Tr(std::string_view(ICON_FA_COPY "  Nusxalash")) + suffix).c_str(), ShortcutLabel(EditorShortcut::Copy), false, sel)) CopySelectedEntity();
            if (ImGui::MenuItem(Tr(ICON_FA_PASTE "  Joylash"), ShortcutLabel(EditorShortcut::Paste), false, editing)) PasteEntity();
            if (ImGui::MenuItem(Tr(ICON_FA_BOX "  Prefab sifatida saqlash"), nullptr, false, sel && Project::GetActive())) SavePrefab(m_SceneHierarchyPanel.GetSelectedEntity());
            ImGui::Separator();
            if (ImGui::MenuItem((Tr(std::string_view(ICON_FA_CLONE "  Nusxa olish")) + suffix).c_str(), ShortcutLabel(EditorShortcut::Duplicate), false, sel && editing)) DuplicateSelectedEntity();
            if (ImGui::MenuItem((Tr(std::string_view(ICON_FA_TRASH "  O'chirish")) + suffix).c_str(), ShortcutLabel(EditorShortcut::Delete), false, sel && editing)) DeleteSelectedEntity();
            if (ImGui::MenuItem(Tr(ICON_FA_BULLSEYE "  Fokus"), ShortcutLabel(EditorShortcut::FrameSelection), false, sel)) FrameSelection();
            ImGui::Separator();
            {
                Entity primary = m_SceneHierarchyPanel.GetSelectedEntity();
                bool hidden = primary && m_ActiveScene->IsHiddenInEditor(primary, false);
                bool locked = primary && m_ActiveScene->IsLockedInEditor(primary, false);
                if (ImGui::MenuItem((Tr(std::string_view(hidden ? ICON_FA_EYE "  Ko'rsatish" : ICON_FA_EYE_SLASH "  Yashirish (editorda)")) + suffix).c_str(), ShortcutLabel(EditorShortcut::ToggleHidden), false, sel && editing))
                    ToggleSelectionHidden();
                if (ImGui::MenuItem((Tr(std::string_view(locked ? ICON_FA_LOCK_OPEN "  Qulfni ochish" : ICON_FA_LOCK "  Qulflash (viewport'da)")) + suffix).c_str(), nullptr, false, sel && editing))
                    ToggleSelectionLocked();
                bool active = !primary || primary.IsActive();
                if (ImGui::MenuItem((Tr(std::string_view(active ? ICON_FA_TOGGLE_OFF "  Nofaol qilish" : ICON_FA_TOGGLE_ON "  Faollashtirish")) + suffix).c_str(), ShortcutLabel(EditorShortcut::ToggleActive), false, sel))
                    ToggleSelectionActive();
                if (ImGui::MenuItem(Tr(ICON_FA_EYE "  Hammasini ko'rsatish"), ShortcutLabel(EditorShortcut::ShowAllHidden), false, editing && m_ActiveScene->CountHiddenInEditor() > 0))
                    ShowAllHidden();
                if (ImGui::MenuItem(Tr(ICON_FA_LOCK_OPEN "  Hamma qulflarni ochish"), nullptr, false, editing && m_ActiveScene->CountLockedInEditor() > 0))
                    m_SceneHierarchyPanel.UnlockAll();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_CHECK_DOUBLE "  Hammasini tanlash"), ShortcutLabel(EditorShortcut::SelectAll))) m_SceneHierarchyPanel.SelectAll();
            if (ImGui::MenuItem(Tr(ICON_FA_XMARK "  Tanlovni bekor qilish"), nullptr, false, sel)) m_SceneHierarchyPanel.SetSelectedEntity({});
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_USER_GEAR "  Sozlamalar..."), ShortcutLabel(EditorShortcut::Preferences), m_ShowPreferences)) m_ShowPreferences = !m_ShowPreferences;
            if (ImGui::MenuItem(Tr(ICON_FA_KEYBOARD "  Klaviatura yorliqlari..."), ShortcutLabel(EditorShortcut::ShortcutsWindow), m_ShowShortcuts)) m_ShowShortcuts = !m_ShowShortcuts;
            ImGui::EndMenu();
        }

        UI_ViewMenu();

        if (ImGui::BeginMenu(Tr("Obyekt")))
        {
            if (m_SceneState == SceneState::Edit)
                m_SceneHierarchyPanel.DrawCreateMenu();
            else
                ImGui::TextDisabled("O'yin rejimida mavjud emas");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(Tr("Skriptlar")))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_HAMMER "  Kompilyatsiya + qayta yuklash"), ShortcutLabel(EditorShortcut::BuildScripts), false, hasProject && !m_Building)) BuildScripts(true);
            if (ImGui::MenuItem(Tr(ICON_FA_ARROWS_ROTATE "  Modulni qayta yuklash"), nullptr, false, hasProject)) ReloadScriptModule();
            ImGui::MenuItem(Tr(ICON_FA_BOLT "  Avtomatik hot-reload"), nullptr, &m_AutoReload);
            {
                const std::string label = m_BuildErrors + m_BuildWarnings > 0
                                              ? std::format(ICON_FA_SCREWDRIVER_WRENCH "  Kompilyatsiya natijalari ({} xato, {} ogohl.)", m_BuildErrors, m_BuildWarnings)
                                              : std::string(ICON_FA_SCREWDRIVER_WRENCH "  Kompilyatsiya natijalari");
                if (ImGui::MenuItem(label.c_str()))
                {
                    m_ShowBuildOutput = true;
                    m_FocusBuildOutput = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_FILE_CODE "  Yangi C++ skript..."), nullptr, false, hasProject)) { m_NewScriptName = "MyScript"; m_OpenNewScriptPopup = true; }
            if (ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Source papkasini ochish"), nullptr, false, hasProject))
                Platform::OpenInFileBrowser(Project::GetSourceDirectory());
            ImGui::Separator();
            ImGui::TextDisabled("Ro'yxatdan o'tgan klasslar: %zu (bosish: kodini ochish)", ScriptRegistry::GetAll().size());
            for (auto& [n, info] : ScriptRegistry::GetAll())
                if (ImGui::MenuItem(std::format(ICON_FA_PEN_TO_SQUARE "  {}", n).c_str(), std::format("{} maydon", info.Fields.size()).c_str()))
                    OpenScriptClassSource(n);
            ImGui::EndMenu();
        }

        if (m_OpenMenu.starts_with("Oyna"))
            ImGui::OpenPopup(Tr("Oyna"));
        if (ImGui::BeginMenu(Tr("Oyna")))
        {
            ImGui::MenuItem(Tr(ICON_FA_LIST "  Ierarxiya"), nullptr, &m_ShowHierarchy);
            ImGui::MenuItem(Tr(ICON_FA_SLIDERS "  Inspektor"), nullptr, &m_ShowInspector);
            ImGui::MenuItem(Tr(ICON_FA_FOLDER_OPEN "  Kontent"), nullptr, &m_ShowContentBrowser);
            ImGui::MenuItem(Tr(ICON_FA_TERMINAL "  Konsol"), nullptr, &m_ShowConsole);
            ImGui::MenuItem(Tr(ICON_FA_MOUNTAIN_SUN "  Sahna sozlamalari"), nullptr, &m_ShowSceneSettings);
            if (ImGui::MenuItem(Tr(ICON_FA_CLAPPERBOARD "  Sahnalar"), nullptr, &m_ShowScenes, hasProject) && m_ShowScenes)
                m_ScenesPanel.Focus();
            ImGui::MenuItem(Tr(ICON_FA_GEAR "  Loyiha sozlamalari"), nullptr, &m_ShowProjectSettings);
            ImGui::MenuItem(Tr(ICON_FA_CHART_LINE "  Statistika"), nullptr, &m_ShowStats);
            ImGui::MenuItem(Tr(ICON_FA_CHART_PIE "  Sahna statistikasi"), nullptr, &m_ShowSceneStatistics);
            if (ImGui::MenuItem(Tr(ICON_FA_SCREWDRIVER_WRENCH "  Kompilyatsiya"), nullptr, &m_ShowBuildOutput) && m_ShowBuildOutput)
                m_FocusBuildOutput = true;
            ImGui::MenuItem(Tr(ICON_FA_STOPWATCH "  Profiler"), nullptr, &m_ShowProfiler);
            ImGui::MenuItem(Tr(ICON_FA_SLIDERS "  Audio Mixer"), nullptr, &m_ShowAudioMixer);
            if (ImGui::MenuItem(Tr(ICON_FA_LANGUAGE "  Lokalizatsiya"), nullptr, &m_ShowLocalization, hasProject) && m_ShowLocalization)
                m_LocalizationPanel.Focus();
            if (ImGui::MenuItem(Tr(ICON_FA_TIMELINE "  Animatsiya"), nullptr, &m_ShowAnimation) && m_ShowAnimation)
                m_AnimationPanel.Focus();
            if (ImGui::MenuItem(Tr(ICON_FA_DIAGRAM_PROJECT "  Animator grafi"), nullptr, &m_ShowAnimatorGraph) && m_ShowAnimatorGraph)
                m_AnimatorGraphPanel.Focus();
            ImGui::MenuItem(Tr(ICON_FA_BROOM "  Ishlatilmagan assetlar"), nullptr, &m_ShowUnusedAssets, hasProject);
            if (ImGui::MenuItem(Tr(ICON_FA_SCISSORS "  Sprite muharriri"), nullptr, m_ShowSpriteEditor, hasProject))
            {
                m_ShowSpriteEditor = !m_ShowSpriteEditor;
                // Opened without a texture: the one picked in the content browser, if any.
                if (m_ShowSpriteEditor && m_SpriteEditorPanel.GetTexturePath().empty() && m_AssetInspectorPanel.HasAsset() &&
                    AssetFiles::GetType(m_AssetInspectorPanel.GetPath()) == AssetType::Texture)
                    m_SpriteEditorPanel.Open(Project::GetAssetRelativePath(m_AssetInspectorPanel.GetPath()));
            }
            ImGui::MenuItem(Tr(ICON_FA_TABLE_CELLS "  Plitka palitrasi"), nullptr, &m_ShowTilePalette);
            if (ImGui::MenuItem(Tr(ICON_FA_CLOCK_ROTATE_LEFT "  Undo tarixi"), ShortcutLabel(EditorShortcut::UndoHistory), &m_ShowUndoHistory) && m_ShowUndoHistory)
                m_UndoHistoryScroll = true;
            ImGui::Separator();
            if (m_OpenMenu == "Oyna/views")
                ImGui::OpenPopup(Tr(ICON_FA_TABLE_COLUMNS "  Ko'rinishlar"));
            if (ImGui::BeginMenu(Tr(ICON_FA_TABLE_COLUMNS "  Ko'rinishlar")))
            {
                ImGui::TextDisabled("Har biri o'z kamerasi bilan (sahna ko'rinishlari)");
                for (int i = 0; i < SceneViewSettings::Count; i++)
                {
                    bool open = m_ViewSettings.SceneViews[i].Open;
                    const ViewAxis axis = m_ExtraViews[i].Camera.GetViewAxis();
                    const std::string label = std::format(ICON_FA_CUBE "  Ko'rinish {}", i + 2);
                    const std::string hint = axis != ViewAxis::Free ? GetViewAxisInfo(axis).Short
                                           : m_ExtraViews[i].Camera.IsOrthographic() ? "Orto" : "Persp";
                    if (ImGui::MenuItem(label.c_str(), hint.c_str(), open))
                        OpenSceneView(i, !open);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(Tr(ICON_FA_TABLE_CELLS_LARGE "  4 ko'rinish (2x2 joylashuv)")))
                    RequestBuiltInLayout(BuiltInLayout::FourViews);
                if (ImGui::MenuItem(Tr(ICON_FA_XMARK "  Qo'shimcha ko'rinishlarni yopish"), nullptr, false,
                                    std::any_of(m_ViewSettings.SceneViews.begin(), m_ViewSettings.SceneViews.end(), [](const SceneViewSettings& v) { return v.Open; })))
                    for (int i = 0; i < SceneViewSettings::Count; i++)
                        OpenSceneView(i, false);
                ImGui::EndMenu();
            }
            if (m_OpenMenu == "Oyna/layout")
                ImGui::OpenPopup(Tr(ICON_FA_WINDOW_RESTORE "  Joylashuv"));
            UI_LayoutMenu();
            ImGui::Separator();
            ImGui::MenuItem(Tr("ImGui demo"), nullptr, &m_ShowImGuiDemo);
            ImGui::EndMenu();
        }

        if (m_OpenMenu.starts_with("Yordam"))
            ImGui::OpenPopup(Tr("Yordam"));
        if (ImGui::BeginMenu(Tr("Yordam")))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_KEYBOARD "  Klaviatura yorliqlari"), ShortcutLabel(EditorShortcut::ShortcutsWindow), m_ShowShortcuts))
                m_ShowShortcuts = !m_ShowShortcuts;
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_CIRCLE_INFO "  Zamin haqida"))) m_ShowAbout = true;
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
                const char* stateText = Tr(m_SceneState == SceneState::Play ? ICON_FA_PLAY "  O'yin" :
                                           m_SceneState == SceneState::Simulate ? ICON_FA_GEARS "  Simulyatsiya" : ICON_FA_FEATHER "  Tahrirlash");
                ImVec4 stateColor = m_SceneState == SceneState::Play ? ImVec4(0.3f, 0.85f, 0.5f, 1) :
                                    m_SceneState == SceneState::Simulate ? ImVec4(0.4f, 0.7f, 1.0f, 1) : ImVec4(0.7f, 0.72f, 0.76f, 1);
                ImGui::TextColored(stateColor, "%s", stateText);
                ImGui::TextDisabled("|");
                ImGui::TextDisabled("%s %s%s", ICON_FA_EARTH_ASIA, m_EditorScene ? m_EditorScene->GetName().c_str() : "", m_SceneDirty ? " *" : "");
                if (ImGui::GetTime() - m_AutosaveNoticeTime < 4.0)
                    ImGui::TextColored(UI::AccentColor(), "%s", m_AutosaveNotice.c_str());
                ImGui::TextDisabled("|");
                if (m_Building)
                {
                    float t = (float)ImGui::GetTime();
                    const char* spin[] = { "|", "/", "-", "\\" };
                    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.3f, 1), "%s %s %s", ICON_FA_HAMMER, Tr("Kompilyatsiya"), spin[(int)(t * 8) % 4]);
                }
                else if (ScriptEngine::IsModuleLoaded())
                    ImGui::TextDisabled("%s %s: %zu %s", ICON_FA_CODE, Tr("Skriptlar"), ScriptRegistry::GetAll().size(), Tr("klass"));
                else
                    ImGui::TextDisabled("%s %s", ICON_FA_CODE, Tr("Skript moduli yuklanmagan"));
                UI_BuildStatusBadge();
                if (m_Exporting)
                {
                    ImGui::TextDisabled("|");
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1), ICON_FA_ROCKET " Eksport...");
                }

                char fps[96];
                if (m_FrameLimit.IsLimited())
                    std::snprintf(fps, sizeof(fps), ICON_FA_LEAF " %s: %.0f FPS", IdleThrottle::GetReasonName(m_FrameLimit.Reason), Application::Get().GetFPS());
                else
                    std::snprintf(fps, sizeof(fps), "%.0f FPS  (%.2f ms)", Application::Get().GetFPS(), Application::Get().GetFrameTime());
                float w = ImGui::CalcTextSize(fps).x + 16;
                if (m_ActiveScene)
                    UI_StatusBarSelection(ImGui::GetWindowWidth() - w - 12.0f);
                ImGui::SameLine(ImGui::GetWindowWidth() - w);
                ImGui::TextDisabled("%s", fps);
                if (m_FrameLimit.IsLimited() && ImGui::IsItemHovered())
                    UI::Tooltip(std::format("Editor tejamkor rejimda: {} FPS gacha ({}).\nSichqoncha yoki klaviatura bilan darhol to'liq tezlikka qaytadi.\n"
                                            "Sozlash: Tahrirlash > Sozlamalar > Ishlash", m_FrameLimit.FPS,
                                            IdleThrottle::GetReasonName(m_FrameLimit.Reason)).c_str());
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
            if (fits(Tr("Hech narsa tanlanmagan")))
                ImGui::TextDisabled("%s", Tr(ICON_FA_ARROW_POINTER "  Hech narsa tanlanmagan"));
            return;
        }

        // Name: click frames the selection, right click copies its name / UUID / path.
        std::string head = s.Count > 1 ? std::format(ICON_FA_OBJECT_GROUP "  {} {}  " ICON_FA_ARROW_RIGHT "  {}", s.Count, Tr("ta obyekt"), s.Name)
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
            if (s.Inactive)
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), ICON_FA_POWER_OFF "  %s",
                                   s.InactiveSelf ? "nofaol: o'yinda ishtirok etmaydi" : "nofaol (ota obyekt nofaol)");
            if (s.Count > 1 && s.InactiveCount > 0)
                ImGui::TextDisabled("Tanlanganlardan %zu tasi nofaol", s.InactiveCount);
            ImGui::Separator();
            ImGui::TextDisabled("Bosing: kamerani yo'naltirish (F)  |  O'ng tugma: nusxalash");
            ImGui::EndTooltip();
        }
        if (ImGui::BeginPopup("##StatusSelectionMenu"))
        {
            if (ImGui::MenuItem(Tr(ICON_FA_COPY "  Nomini nusxalash")))
                ImGui::SetClipboardText(s.Name.c_str());
            if (ImGui::MenuItem(Tr(ICON_FA_HASHTAG "  UUID nusxalash")))
                ImGui::SetClipboardText(std::to_string(s.ID).c_str());
            if (ImGui::MenuItem(Tr(ICON_FA_DIAGRAM_PROJECT "  Ierarxiya yo'lini nusxalash")))
                ImGui::SetClipboardText(s.Path.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_CROSSHAIRS "  Kamerani yo'naltirish"), "F", false, m_SceneState != SceneState::Play))
                FrameSelection();
            if (ImGui::MenuItem(Tr(ICON_FA_XMARK "  Tanlovni bekor qilish")))
                m_SceneHierarchyPanel.GetSelection().Clear();
            ImGui::EndPopup();
        }

        // Position (2D: x / y), components, children, eye / lock / inactive.
        glm::vec3 p = s.Count > 1 ? s.Center : s.Position;
        std::string position = m_2DMode ? std::format("{}  {:.2f}, {:.2f}", s.Count > 1 ? "Markaz" : ICON_FA_CROSSHAIRS, p.x, p.y)
                                        : std::format("{}  {:.2f}, {:.2f}, {:.2f}", s.Count > 1 ? "Markaz" : ICON_FA_CROSSHAIRS, p.x, p.y, p.z);
        if (!fits(position))
            return;
        ImGui::TextDisabled("%s", position.c_str());
        std::string components = std::format(ICON_FA_LAYER_GROUP "  {} {}", s.Components.size(), Tr("komponent"));
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
        if (s.Inactive && fits(ICON_FA_POWER_OFF))
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, s.InactiveSelf ? 0.9f : 0.55f), ICON_FA_POWER_OFF);
    }

    void EditorLayer::UI_Viewport()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin(Tr(ICON_FA_GAMEPAD "  Viewport###Viewport"), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImVec2 regionMin = ImGui::GetWindowContentRegionMin();
        ImVec2 regionMax = ImGui::GetWindowContentRegionMax();
        ImVec2 offset = ImGui::GetWindowPos();

        m_ViewportFocused = ImGui::IsWindowFocused();
        // The picture fills the panel; while playing with a game resolution it is letterboxed /
        // scaled in it (the bounds are then where it is, the size what the game draws at).
        uint64_t textureID = m_FinalFramebuffer->GetColorAttachmentRendererID(0);
        ImGui::SetNextItemAllowOverlap();
        PlaceViewportImage(ImVec2(regionMin.x + offset.x, regionMin.y + offset.y), ImGui::GetContentRegionAvail(), (ImTextureID)textureID);
        m_ViewportHovered = ImGui::IsItemHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportHovered);

        // Drop scenes / prefabs / images / models / sounds onto the viewport; a material onto the
        // mesh under the mouse (shown on it while it is dragged there).
        m_MaterialDrag.Active = false;
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* active = ImGui::GetDragDropPayload();
            if (active && active->IsDataType("CONTENT_BROWSER_ITEM") && AssetFiles::GetType((const char*)active->Data) == AssetType::Material)
                UpdateMaterialDrag((const char*)active->Data);
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                DropAssetOnViewport((const char*)payload->Data);
            ImGui::EndDragDropTarget();
        }
        // Screenshots / CI (no drag source): ZAMIN_EDITOR_MATERIAL_DRAG="Materials/Gold.zmat>Entity"
        // shows that material dragged over that entity, the label at the entity's screen position.
        if (const char* drag = std::getenv("ZAMIN_EDITOR_MATERIAL_DRAG"); drag && m_SceneState == SceneState::Edit)
        {
            std::string_view v(drag);
            size_t arrow = v.find('>');
            Entity target = arrow == std::string_view::npos ? Entity() : m_ActiveScene->FindEntityByName(v.substr(arrow + 1));
            if (target && target.HasComponent<MeshRendererComponent>())
            {
                std::string path(v.substr(0, arrow));
                if (m_MaterialDrag.Path != path)
                {
                    m_MaterialDrag.Path = path;
                    m_MaterialDrag.Material = MaterialLibrary::Load(Project::GetAssetFileSystemPath(path));
                }
                m_MaterialDrag.Target = target.GetUUID();
                m_MaterialDrag.Submesh = -1;
                m_MaterialDrag.Active = true;
                glm::vec4 clip = OverlayViewProjection() * glm::vec4(m_ActiveScene->GetWorldPosition(target), 1.0f);
                if (clip.w > 0.0f)
                {
                    glm::vec2 ndc = glm::vec2(clip) / clip.w;
                    // Where it is for a mouse over the entity (see UpdateMaterialDrag).
                    ImVec2 at(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * m_ViewportSize.x + 18.0f,
                              m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * m_ViewportSize.y + 34.0f);
                    DrawMaterialDragLabel(at, target);
                }
            }
        }
        // ZAMIN_EDITOR_MATERIAL_SLOTS="Materials/Gold.zmat>Model": the part menu of a Shift+drop on
        // that model opens once (at the model on screen).
        static bool slotsShown = false;
        if (const char* slots = std::getenv("ZAMIN_EDITOR_MATERIAL_SLOTS"); slots && !slotsShown && m_SceneState == SceneState::Edit &&
                                                                        ImGui::GetFrameCount() > 20)
        {
            slotsShown = true;
            std::string_view v(slots);
            size_t arrow = v.find('>');
            if (Entity target = arrow == std::string_view::npos ? Entity() : m_ActiveScene->FindEntityByName(v.substr(arrow + 1)))
            {
                glm::vec4 clip = OverlayViewProjection() * glm::vec4(m_ActiveScene->GetWorldPosition(target), 1.0f);
                glm::vec2 at(-1.0f);
                if (clip.w > 0.0f)
                {
                    glm::vec2 ndc = glm::vec2(clip) / clip.w;
                    at = { m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * m_ViewportSize.x,
                           m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * m_ViewportSize.y };
                }
                m_MaterialSlotPick = { std::string(v.substr(0, arrow)), target.GetUUID(), true, at };
            }
        }
        UI_MaterialSlotPopup();
        // The drag is over (dropped elsewhere or cancelled): the material is not held any more.
        if (!m_MaterialDrag.Active && !ImGui::GetDragDropPayload() && !ImGui::IsPopupOpen("##materialSlots"))
        {
            m_MaterialDrag.Material = nullptr;
            m_MaterialDrag.Path.clear();
            m_MaterialDrag.Target = 0;
        }

        const SceneViewTarget mainView = GetSceneViewTarget(0);
        if (m_SceneState != SceneState::Play)
        {
            UI_TransformGizmo(mainView);
            if (m_ShowIcons) UI_ViewportIcons(mainView);
        }
        else
            m_GizmoHovered = false;
        // Entity names: the scene view's camera, while playing the game's (when asked for).
        if (m_ViewSettings.EntityLabels.IsEnabled())
        {
            if (m_SceneState != SceneState::Play)
                UI_EntityLabels(mainView, m_EditorCamera.GetViewProjection(), m_EditorCamera.GetPosition(), m_EditorCamera.IsOrthographic());
            else if (glm::mat4 view, projection; m_ViewSettings.EntityLabels.ShowInPlay && OverlayCamera(view, projection))
                UI_EntityLabels(mainView, projection * view, glm::vec3(glm::inverse(view)[3]), projection[3][3] == 1.0f);
        }
        UI_UIRectGizmo(); // the selected UI element's rect gizmo; the UI element under the mouse
        UI_ColliderPointEditor();
        UI_SplinePointEditor();
        UI_JointAnchorEditor();
        UI_Joint3DAnchorEditor();
        if (m_ShowTransparentOrder) UI_TransparentOrder(); // also while playing (the game camera's order)
        if (!m_JointLabels.empty()) UI_JointLabels();
        // The view mode, when it is not the real picture (the toolbar's menu changes it).
        if (m_SceneState != SceneState::Play && (m_DebugView != RenderDebugView::Lit || m_Wireframe != WireframeMode::Off))
        {
            const DebugViewInfo& info = s_DebugViews[(int)m_DebugView];
            std::string label = m_DebugView != RenderDebugView::Lit ? std::format("{}  {}", info.Icon, info.Name) : std::string();
            if (m_Wireframe != WireframeMode::Off)
                label += std::format("{}{}  {}", label.empty() ? "" : "   ", ICON_FA_DRAW_POLYGON,
                                     m_Wireframe == WireframeMode::Wire ? "Wireframe" : "Sirt + wireframe");
            ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            ImVec2 at(m_ViewportBounds[0].x + 12.0f, m_ViewportBounds[1].y - textSize.y - 16.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(at.x - 6, at.y - 4), ImVec2(at.x + textSize.x + 6, at.y + textSize.y + 4), IM_COL32(18, 20, 24, 200), 5.0f);
            dl->AddText(at, ImGui::GetColorU32(UI::AccentColor()), label.c_str());
        }
        UI_StatsOverlay();
        UI_ParticleOverlay();
        UI_ViewportToolbar();
        // The axis navigator (edit / simulate, not the 2D mode): top-right below the toolbar, or
        // bottom-right when the statistics overlay has that corner.
        m_NavigatorHovered = false;
        if (m_SceneState != SceneState::Play && !m_2DMode)
        {
            const float radius = std::floor(ImGui::GetFontSize() * 2.8f);
            const auto& overlay = m_ViewSettings.StatsOverlay;
            const bool overlayTop = overlay.Visible && overlay.Corner == OverlayCorner::TopRight;
            const float x = m_ViewportBounds[1].x - radius - 16.0f;
            const float y = overlayTop ? m_ViewportBounds[1].y - radius - 64.0f
                                       : std::max(m_ViewportBounds[0].y, m_ViewportPanelBounds[0].y + m_ToolbarBottom) + 16.0f + radius;
            m_NavigatorHovered = UI_ViewAxisNavigator(mainView, ImVec2(x, y), radius);
        }
        UI_ViewportMessage();
        UI_SceneLoadBanner();

        // Play-mode border (around the whole panel)
        if (m_SceneState != SceneState::Edit)
        {
            ImU32 col = m_SceneState == SceneState::Play ? IM_COL32(60, 200, 120, 200) : IM_COL32(80, 150, 255, 200);
            ImGui::GetWindowDrawList()->AddRect(ImVec2(m_ViewportPanelBounds[0].x, m_ViewportPanelBounds[0].y),
                                                ImVec2(m_ViewportPanelBounds[1].x, m_ViewportPanelBounds[1].y), col, 0.0f, 0, 2.0f);
        }

        if (m_SceneState == SceneState::Play && !GetViewScene().GetPrimaryCameraEntity())
        {
            ImGui::SetCursorPos(ImVec2(regionMin.x + 20, regionMax.y - 40));
            // An inactive primary camera does not count: say so instead of "there is none".
            if (Entity inactive = GetViewScene().GetPrimaryCameraEntity(true))
                ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1), ICON_FA_TRIANGLE_EXCLAMATION "  Asosiy kamera nofaol: '%s' (boshqa faol asosiy kamera yo'q)",
                                   inactive.GetName().c_str());
            else
                ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1), ICON_FA_TRIANGLE_EXCLAMATION "  Sahnada asosiy (Primary) kamera yo'q!");
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::UpdateGameCursor()
    {
        const bool playing = m_SceneState == SceneState::Play && m_RuntimeScenes && !m_RuntimeScenes->IsPaused();
        const bool wantsHold = Cursor::GetLockMode() != CursorLockMode::None;
        if (playing && wantsHold && !m_GameCursorReleased && Cursor::IsHostActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_GameCursorReleased = true;
            ShowViewportMessage(Tr(ICON_FA_ARROW_POINTER "  Kursor bo'shatildi - qaytarish uchun o'yin ko'rinishini bosing"));
        }
        if (m_GameCursorReleased && (!playing || (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
            m_GameCursorReleased = false;
        // A held cursor (confined / locked) stays the game's while its view has the focus; a hidden one or a
        // picture only over the view.
        const bool active = playing && m_ViewportFocused && !m_GameCursorReleased && (wantsHold || m_ViewportHovered);
        Cursor::SetHostActive(active);
        // ImGui would set its own cursor shape and mode every frame: not while the game has it. A locked
        // cursor's position is virtual - ImGui ignores the mouse then (it would wander over other panels).
        const CursorAppliedState wanted = Cursor::Resolve(Cursor::IsVisible(), Cursor::GetLockMode(), Cursor::HasImage(), Cursor::GetShape(), active,
                                                          Cursor::GetReleaseWhenPaused() && Time::IsPaused());
        int flags = 0;
        if (wanted != CursorAppliedState())
            flags |= ImGuiConfigFlags_NoMouseCursorChange;
        if (wanted.Mode == WindowCursorMode::Disabled)
            flags |= ImGuiConfigFlags_NoMouse;
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags = (io.ConfigFlags & ~m_GameCursorImGuiFlags) | flags;
        m_GameCursorImGuiFlags = flags;
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
        m_ViewportFocused = true;
        ImGui::SetNextItemAllowOverlap();
        PlaceViewportImage(pos, size, (ImTextureID)(uint64_t)m_FinalFramebuffer->GetColorAttachmentRendererID(0));
        m_ViewportHovered = ImGui::IsItemHovered();
        Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportHovered);

        UI_StatsOverlay();
        UI_SceneLoadBanner();
        // small stop button + hint
        ImGui::SetCursorPos(ImVec2(size.x * 0.5f - 40, 10));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1));
        if (UI::ToolbarButton(ICON_FA_STOP "  To'xtatish", "Ctrl+P", false, ImVec2(110, 0)))
            OnSceneStop();
        ImGui::PopStyleColor();
        ImGui::End();
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
            if (current) ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(1.0f));
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

    void EditorLayer::UI_TransformGizmoPopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(330, 0), ImVec2(440, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##TransformGizmo"))
        {
            ImGui::TextColored(UI::AccentColor(), ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  Gizmo o'lchami");
            ImGui::SameLine();
            ImGui::TextDisabled("(ekranda, standartga ko'paytma)");
            if (UI_TransformGizmoSettings())
                MarkViewSettingsDirty();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    bool EditorLayer::UI_TransformGizmoSettings()
    {
        auto& tg = m_ViewSettings.TransformGizmo;
        bool changed = false;
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("O'lcham", &tg.Size, TransformGizmoSettings::MinSize, TransformGizmoSettings::MaxSize, "x%.2f");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ko'chirish / aylantirish / masshtab gizmosi ekranda qancha joy egallaydi (x1 - viewport balandligining "
                        "taxminan o'ndan biri). Kamera uzoqligiga bog'liq emas");
        changed |= PresetButtons("gizmosize", tg.Size, { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f }, "x");
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("Chiziq qalinligi", &tg.LineThickness, TransformGizmoSettings::MinThickness,
                                      TransformGizmoSettings::MaxThickness, "x%.2f");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Gizmo o'qlari, uchidagi strelka va doiralar, aylantirish halqalarining qalinligi");
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("Ikonkalar", &tg.IconSize, TransformGizmoSettings::MinIconSize, TransformGizmoSettings::MaxIconSize,
                                      "x%.2f");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Viewport'dagi kamera, yorug'lik va audio manba ikonkalarining o'lchami");
        if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Standart o'lchamlar")))
        {
            tg = TransformGizmoSettings();
            changed = true;
        }
        if (changed)
            tg.Sanitize();
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
            changed |= ImGui::Checkbox(Tr("Doim yoqilgan"), &snap.Enabled);
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
            if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Standart qadamlar")))
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

    void EditorLayer::UI_ViewModePopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(290, 0), ImVec2(420, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##ViewMode"))
        {
            ImGui::TextColored(UI::AccentColor(), ICON_FA_EYE "  Ko'rinish rejimi");
            ImGui::Separator();
            for (int i = 0; i < RenderDebugViewCount; i++)
            {
                std::string label = std::format("{}  {}", s_DebugViews[i].Icon, s_DebugViews[i].Name);
                if (ImGui::Selectable(label.c_str(), (int)m_DebugView == i, ImGuiSelectableFlags_DontClosePopups))
                    m_DebugView = (RenderDebugView)i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", s_DebugViews[i].Tooltip);
            }
            ImGui::SeparatorText(Tr(ICON_FA_DRAW_POLYGON "  Wireframe"));
            auto wireOption = [&](const char* label, WireframeMode mode, const char* tooltip) {
                if (ImGui::RadioButton(label, m_Wireframe == mode))
                {
                    m_Wireframe = mode;
                    if (mode != WireframeMode::Off)
                        m_WireframeStyle = mode;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", tooltip);
            };
            wireOption("O'chiq", WireframeMode::Off, "Sirtlar odatdagidek to'ldirib chiziladi");
            wireOption("Faqat qirralar", WireframeMode::Wire, "Meshlar faqat uchburchak qirralari bilan (tanlangan ko'rinish ranglarida)");
            wireOption("Sirt + qirralar", WireframeMode::ShadedWire, "Odatdagi tasvir ustida uchburchak qirralari");
            ImGui::Spacing();
            ImGui::TextDisabled("Faqat sahna ko'rinishida: o'yin kamerasi\n(Play) doim haqiqiy tasvirni ko'rsatadi");
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::UI_DebugDrawPopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(440, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##DebugDraw"))
        {
            if (UI_DebugDrawSettings())
                MarkViewSettingsDirty();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::ApplyDebugDrawSettings()
    {
        DebugDraw::GetSettings() = m_ViewSettings.DebugDraw;
        m_AppliedDebugDraw = m_ViewSettings.DebugDraw;
    }

    void EditorLayer::ToggleDebugDraw()
    {
        m_ViewSettings.DebugDraw.Enabled = !DebugDraw::IsEnabled();
        MarkViewSettingsDirty();
        ApplyDebugDrawSettings();
    }

    bool EditorLayer::UI_DebugDrawSettings()
    {
        auto& d = m_ViewSettings.DebugDraw;
        bool changed = false;
        ImGui::TextDisabled(ICON_FA_BUG "  Debug chizish");
        if (m_SceneState != SceneState::Edit && DebugDraw::GetSettings() != d)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.3f, 1.0f), ICON_FA_CIRCLE_INFO "  O'yin skripti bu sozlamalarni o'zgartirgan");
            if (ImGui::IsItemHovered())
                UI::Tooltip("Skript DebugDraw::SetEnabled yoki DebugDraw::GetSettings() bilan o'zgartirdi. O'yin to'xtaganda "
                            "editor sozlamalari qaytadi; bu yerda biror narsani o'zgartirsangiz - darhol qaytadi.");
        }
        changed |= ImGui::Checkbox(Tr("Yoqilgan"), &d.Enabled);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Skriptlar va dvigatel chizgan shakllar: DebugDraw::Line, Box, Sphere, Arrow, Text ...\n"
                        "Davomiylik o'yin vaqtida sanaladi (pauzada to'xtaydi). Faqat editor va Development build'da.");
        ImGui::BeginDisabled(!d.Enabled);
        ImGui::Spacing();
        ImGui::TextDisabled("Fizika");
        changed |= ImGui::Checkbox(Tr(ICON_FA_CROSSHAIRS "  Raycastlar"), &d.ShowRaycasts);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Har bir Raycast2D / Raycast3D so'rovi: tekkan joyigacha yashil nur, tegish nuqtasi va sirt normali; "
                        "tegmasa butun nur qizil");
        if (d.ShowRaycasts)
        {
            ImGui::Indent();
            ImGui::SetNextItemWidth(150);
            changed |= ImGui::SliderFloat("Ko'rsatish vaqti", &d.RaycastDuration, 0.0f, 2.0f, d.RaycastDuration > 0.0f ? "%.2f s" : "1 kadr");
            ImGui::Unindent();
        }
        changed |= ImGui::Checkbox(Tr(ICON_FA_CIRCLE_DOT "  Kontakt nuqtalari"), &d.ShowContacts);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Fizik jismlar (2D, 3D, 3D personajlar) tegib turgan nuqtalar va normallari - o'yin yoki simulyatsiya "
                        "paytida. To'q sariq - jismlar, yashil - personajlar, xira ko'kish - ikkala jism uxlayapti; qizil chiziq - "
                        "shakllar bir-biriga qancha kirgani (asl o'lchamda), halqa - 3D jism kuchli urilgan joy (bir lahza)");
        changed |= ImGui::Checkbox(Tr(ICON_FA_LINK "  Joint'lar"), &d.ShowJoints);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ishlayotgan 2D / 3D joint'lar o'yin yoki simulyatsiya paytida: ikkala langar va ular orasidagi chiziq "
                        "(sharnir bo'shasa qizil), jismlar markazigacha chiziqlar, sharnir / sirpanish / konus o'qi. Rangi - "
                        "uzilishga qanchalik yaqin (yashil - sariq - qizil; ko'k - uzilmaydi), uzilgani qizil X");
        if (d.ShowContacts || d.ShowJoints)
        {
            ImGui::Indent();
            changed |= ImGui::Checkbox(Tr("Qiymatlar (yozuvlar)"), &d.ShowContactDetails);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Har tegib turgan juftlik ustida yozuv: shakllar qancha kirgani (chuqurlik, mm), 3D zarba tezligi va "
                            "taxminiy impulsi (2D: Box2D impulsi), uxlayotgani; joint'larda kuch, moment va uzilish chegarasidan "
                            "qancha foizi");
            ImGui::Unindent();
        }
        if (d.ShowRaycasts || d.ShowContacts)
        {
            ImGui::SetNextItemWidth(150);
            changed |= ImGui::SliderFloat("Normal uzunligi", &d.NormalLength, 0.05f, 2.0f, "%.2f");
        }
        changed |= ImGui::Checkbox(Tr(ICON_FA_PERSON_RUNNING "  Personaj tezligi"), &d.ShowVelocities);
        if (ImGui::IsItemHovered())
            UI::Tooltip("3D character controller'lar: haqiqiy tezlik o'qi (kapsula o'rtasidan, uchida m/s) va xira - skript "
                        "so'ragan tezlik (devor yoki qiyalik to'sganda farq qiladi). O'yin yoki simulyatsiya paytida");
        changed |= ImGui::Checkbox(Tr(ICON_FA_ARROW_RIGHT "  Jismlar tezligi"), &d.ShowBodyVelocities);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Harakatlanayotgan dinamik jismlar (2D va 3D): tezlik vektori o'q bilan");
        if (d.ShowVelocities || d.ShowBodyVelocities)
        {
            ImGui::Indent();
            ImGui::SetNextItemWidth(150);
            changed |= ImGui::SliderFloat("O'q uzunligi", &d.VelocityScale, DebugDrawSettings::MinVelocityScale, 2.0f, "%.2f s");
            if (ImGui::IsItemHovered())
                UI::Tooltip("O'q shuncha soniyalik yo'lni ko'rsatadi: uzunligi = tezlik x vaqt");
            ImGui::Unindent();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Ko'rinish");
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("Chiziq qalinligi", &d.LineWidth, DebugDrawSettings::MinLineWidth, DebugDrawSettings::MaxLineWidth, "%.1f px");
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("Nuqta o'lchami", &d.PointSize, DebugDrawSettings::MinPointSize, DebugDrawSettings::MaxPointSize, "%.0f px");
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("Matn o'lchami", &d.TextSize, DebugDrawSettings::MinTextSize, DebugDrawSettings::MaxTextSize, "%.0f px");
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("Sirt orqasidagilar", &d.OccludedOpacity, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Chuqurlik testi bilan chizilgan shakllarning sirt orqasida qolgan qismi: 0 - ko'rinmaydi, 1 - to'liq ko'rinadi");
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextDisabled("Navbatda: %zu chiziq, %zu nuqta, %zu matn", DebugDraw::GetLines().size(), DebugDraw::GetPoints().size(),
                            DebugDraw::GetTexts().size());
        if (ImGui::SmallButton(Tr(ICON_FA_BROOM "  Tozalash")))
            DebugDraw::Clear();
        if (changed)
            d.Sanitize();
        return changed;
    }

    void EditorLayer::UI_ComponentGizmosPopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(440, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##ComponentGizmos"))
        {
            ImGui::Checkbox(Tr(ICON_FA_EYE "  Ikonkalar"), &m_ShowIcons);
            ImGui::Spacing();
            bool changed = UI_ComponentGizmoSettings();
            ImGui::Spacing();
            ImGui::Separator();
            changed |= UI_SelectionOutlineSettings();
            if (changed)
                MarkViewSettingsDirty();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    bool EditorLayer::UI_ComponentGizmoSettings()
    {
        auto& g = m_ViewSettings.Gizmos;
        bool changed = false;
        ImGui::TextDisabled(ICON_FA_SHAPES "  Gizmolar (faqat editorda)");
        auto option = [&](const char* label, GizmoVisibility& value, const char* tooltip) {
            static const char* items[] = { "O'chiq", "Tanlanganda", "Hammasida" };
            int current = (int)value;
            ImGui::SetNextItemWidth(130);
            if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items)))
            {
                value = (GizmoVisibility)current;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip(tooltip);
        };
        option(ICON_FA_CAMERA "  Kamera frustumi", g.CameraFrustums,
               "Kamera ko'radigan hajm: perspektiv - 10 birlikkacha aniq piramida, undan far tekisligigacha xira; ortografik - "
               "quti. Yuqori tomoni uchburchak bilan belgilangan. O'yin paytida ko'rib turilgan kamera chizilmaydi");
        option(ICON_FA_SUN "  Yorug'lik yo'nalishi", g.LightDirections,
               "Directional light: halqa, nurlar va o'q; spot light: o'qi bo'ylab o'q (range'dan uzun emas). Yorug'lik rangida");
        option(ICON_FA_VOLUME_HIGH "  Audio radiusi", g.AudioRanges,
               "3D (spatial) audio manbalar: ichki sfera - Min masofa (to'liq ovoz), tashqi xira sfera - Max masofa (undan "
               "uzoqda eshitilmaydi)");
        option(ICON_FA_BOX "  Chegaralar (bounding box)", g.Bounds,
               "Obyekt chizadigan narsa atrofidagi quti: mesh / model, sprite, doira, matn, tilemap. Yorug'lik, kamera va bo'sh "
               "guruhlarda quti yo'q");
        if (g.Bounds != GizmoVisibility::Off)
        {
            ImGui::Indent();
            int space = (int)g.BoundsMode;
            changed |= ImGui::RadioButton(Tr("Lokal"), &space, (int)BoundsSpace::Local);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Obyektning o'z koordinatalaridagi quti - u bilan birga aylanadi va masshtablanadi (havorang)");
            ImGui::SameLine();
            changed |= ImGui::RadioButton(Tr("Dunyo (AABB)"), &space, (int)BoundsSpace::World);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Dunyo o'qlari bo'yicha quti (yashil): mesh'lar uchun frustum culling tekshiradigan chegara - skinned "
                            "model'niki pozasi bilan o'zgaradi");
            ImGui::SameLine();
            changed |= ImGui::RadioButton(Tr("Ikkalasi"), &space, (int)BoundsSpace::Both);
            g.BoundsMode = (BoundsSpace)space;
            ImGui::Unindent();
        }
        option(ICON_FA_BONE "  Skelet", g.Skeletons,
               "Skinned model'larning suyaklari (ota bo'g'indan bo'g'ingacha) va bo'g'inlari - joriy animatsiya pozasida, mesh "
               "ustidan ko'rinadi");
        if (g.Skeletons != GizmoVisibility::Off)
        {
            ImGui::Indent();
            changed |= ImGui::Checkbox(Tr("Bo'g'in nomlari"), &g.JointNames);
            ImGui::Unindent();
        }
        option(ICON_FA_BEZIER_CURVE "  Spline yo'llar", g.Splines,
               "Spline komponentlarining yo'li (egri chiziq, nuqtalari, yo'nalish o'qi) va Spline Follower'larning boshlanish nuqtasi");
        option(ICON_FA_WAND_MAGIC_SPARKLES "  Particle shakli", g.ParticleShapes,
               "Particle System emitteri: doira, sfera, quti, konus (chetidagi nurlari bilan) yoki chiziq - particle'lar qayerdan "
               "chiqadi, va yo'nalish o'qi");
        option(ICON_FA_LINK "  Joint'lar (2D / 3D)", g.Joints2D,
               "Joint langari (doira / shar), ulangan langar (romb / oktaedr, binafsha) va chegaralar: sharnirning o'qi va burchak "
               "yoyi, sirpanish / osma o'qi, masofa tayog'i. O'yin paytida joint yaratilgandagi holatga nisbatan. Uzilgan joint qizil");
        option(ICON_FA_ARROW_UP_LONG "  Normallar", g.Normals,
               "Mesh va model vertekslarining normallari: har verteksdan normali bo'ylab chiziq (skinned model'larda joriy "
               "pozada). Teskari yoki noto'g'ri normallarni topish uchun");
        if (g.Normals != GizmoVisibility::Off)
        {
            ImGui::Indent();
            ImGui::SetNextItemWidth(130);
            if (ImGui::DragFloat("Uzunligi", &g.NormalLength, std::max(0.0005f, g.NormalLength * 0.01f), EditorGizmoSettings::MinNormalLength,
                                 EditorGizmoSettings::MaxNormalLength, "%.3g m", ImGuiSliderFlags_Logarithmic))
            {
                g.Sanitize();
                changed = true;
            }
            if (ImGui::IsItemHovered())
                UI::Tooltip("Normal chizig'ining uzunligi (dunyo birliklarida)");
            ImGui::SameLine();
            changed |= PresetButtons("normalLength", g.NormalLength, { 0.05f, 0.1f, 0.25f, 0.5f });
            changed |= ImGui::Checkbox(Tr("Faqat kameraga qaraganlar"), &g.NormalsFacingOnly);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Kameradan teskari tomonga qaragan normallar chizilmaydi (ularni odatda sirtning o'zi to'sadi)");
            changed |= ImGui::Checkbox(Tr("Yo'nalish bo'yicha rang"), &g.NormalsByDirection);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Rang normal yo'nalishidan: X - qizil, Y - yashil, Z - ko'k (normallar ko'rinish rejimidagidek)");
            if (m_NormalLinesDrawn > 0 || m_NormalLinesLimited)
            {
                std::string info = std::format("{} ta chiziq", RenderStatistics::FormatCount(m_NormalLinesDrawn));
                if (m_NormalLinesLimited)
                    info += std::format(" (cheklov {}: vertekslar namunasi)", RenderStatistics::FormatCount(MaxVertexNormalLines));
                ImGui::TextDisabled("%s", info.c_str());
            }
            ImGui::Unindent();
        }
        return changed;
    }

    bool EditorLayer::UI_SelectionOutlineSettings()
    {
        auto& o = m_ViewSettings.SelectionOutline;
        bool changed = false;
        ImGui::TextDisabled(ICON_FA_DRAW_POLYGON "  Tanlash ko'rinishi");
        int style = (int)o.Style;
        changed |= ImGui::RadioButton(Tr("Kontur"), &style, (int)SelectionStyle::Outline);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Tanlangan obyektlar chizgan piksellar atrofida silliq kontur (shader): mesh, model (skinned ham), "
                        "sprite, doira, matn, tilemap");
        ImGui::SameLine();
        changed |= ImGui::RadioButton(Tr("Quti (eski)"), &style, (int)SelectionStyle::Box);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Mesh'lar chegarasi sim quti bilan, 2D shakllar va matn to'rtburchak bilan, bolalarga chiziq");
        o.Style = (SelectionStyle)style;
        ImGui::BeginDisabled(o.Style != SelectionStyle::Outline);
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("Qalinlik", &o.Width, SelectionOutlineSettings::MinWidth, SelectionOutlineSettings::MaxWidth, "%.1f px");
        ImGui::SameLine();
        changed |= ImGui::ColorEdit4("##outlinecolor", &o.Color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Asosiy tanlangan obyekt rangi (qolgan tanlanganlar - ochroq, bolalari - xiraroq)");
        changed |= ImGui::Checkbox(Tr("Yashiringan qismlar"), &o.ShowOccluded);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Tanlangan mesh boshqa obyekt orqasida qolgan joyda ham konturi xira chiziladi");
        if (o.ShowOccluded)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            changed |= ImGui::SliderFloat("##occluded", &o.OccludedOpacity, 0.05f, 1.0f, "%.2f");
        }
        changed |= ImGui::Checkbox(Tr("Bolalari ham"), &o.Children);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Tanlangan obyektning ichidagi (bola) obyektlar xiraroq kontur oladi");
        changed |= ImGui::Checkbox(Tr(ICON_FA_HAND_POINTER "  Sichqoncha ostidagi"), &o.Hover);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Bosilsa tanlanadigan obyekt (sichqoncha ostida) ingichka va xira kontur bilan ajratiladi");
        if (o.Hover)
        {
            ImGui::Indent();
            ImGui::SetNextItemWidth(150);
            changed |= ImGui::SliderFloat("Hover qalinligi", &o.HoverWidth, SelectionOutlineSettings::MinWidth,
                                          SelectionOutlineSettings::MaxWidth, "%.1f px");
            ImGui::SameLine();
            changed |= ImGui::ColorEdit4("##hovercolor", &o.HoverColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::Unindent();
        }
        if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT "  Standart kontur")))
        {
            SelectionStyle keep = o.Style;
            o = SelectionOutlineSettings();
            o.Style = keep;
            changed = true;
        }
        ImGui::EndDisabled();
        if (changed)
            o.Sanitize();
        return changed;
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
            if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Standart qiymatlar")))
            {
                GridSettings defaults;
                defaults.Visible = grid.Visible;
                grid = defaults;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr(ICON_FA_MAGNET "  Snapping qadamiga tenglash")))
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
                if (ImGui::Button(Tr(ICON_FA_FLOPPY_DISK "  Saqlash")))
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
            if (ImGui::Button(Tr(ICON_FA_TRASH "  Hammasini tozalash")))
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
        if (m_OpenMenu.starts_with("Ko'rinish"))
            ImGui::OpenPopup(Tr("Ko'rinish"));
        if (!ImGui::BeginMenu(Tr("Ko'rinish")))
            return;
        bool camera = m_SceneState != SceneState::Play;
        if (ImGui::MenuItem(Tr(ICON_FA_BORDER_ALL "  Grid"), nullptr, &m_ViewSettings.Grid.Visible)) MarkViewSettingsDirty();
        if (ImGui::BeginMenu(Tr(ICON_FA_SLIDERS "  Grid sozlamalari")))
        {
            auto& grid = m_ViewSettings.Grid;
            bool changed = false;
            ImGui::SetNextItemWidth(140);
            changed |= ImGui::DragFloat("Katak o'lchami", &grid.CellSize, 0.01f, GridSettings::MinCellSize, 1000.0f, "%.3g");
            ImGui::SetNextItemWidth(140);
            changed |= ImGui::SliderInt("Katta chiziq", &grid.MajorEvery, 1, 20, grid.MajorEvery > 1 ? "har %d" : "yo'q");
            changed |= ImGui::ColorEdit4("Chiziq rangi", &grid.Color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::ColorEdit4("Katta chiziq rangi", &grid.MajorColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            changed |= ImGui::Checkbox(Tr("O'qlar"), &grid.ShowAxes);
            if (changed)
            {
                grid.Sanitize();
                MarkViewSettingsDirty();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(Tr(ICON_FA_MAGNET "  Snapping"), "Ctrl = teskari", &m_ViewSettings.Snap.Enabled)) MarkViewSettingsDirty();
        if (ImGui::BeginMenu(Tr(ICON_FA_SLIDERS "  Snapping qadamlari")))
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
        ImGui::MenuItem(Tr(ICON_FA_VECTOR_SQUARE "  Fizik colliderlar"), nullptr, &m_ShowColliders);
        if (ImGui::MenuItem(Tr(ICON_FA_BUG "  Debug chizish"), nullptr, DebugDraw::IsEnabled())) ToggleDebugDraw();
        if (ImGui::BeginMenu(Tr(ICON_FA_SLIDERS "  Debug chizish sozlamalari")))
        {
            if (UI_DebugDrawSettings())
                MarkViewSettingsDirty();
            ImGui::EndMenu();
        }
        ImGui::MenuItem(Tr(ICON_FA_EYE "  Ikonkalar"), nullptr, &m_ShowIcons);
        ImGui::MenuItem(Tr(ICON_FA_HAND_POINTER "  UI elementlarini tanlash (to'rtburchagi bo'yicha)"), nullptr, &m_PickUIInViewport);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Viewport'da bosilganda ekran UI'si (matn, rasm, tugma...) to'rtburchagi bo'yicha tanlanadi - shaffof joyidan ham. "
                        "O'chiq bo'lsa faqat chizilgan piksellari tanlanadi");
        ImGui::MenuItem(Tr(ICON_FA_MOUNTAIN "  Parallax (editor kamerasi bilan)"), nullptr, &m_PreviewParallax);
        if (ImGui::BeginMenu(std::format(ICON_FA_DESKTOP "  {}: {}###GameViewMenu", Tr("O'yin oynasi o'lchami"), m_ViewSettings.GameView.GetLabel()).c_str()))
        {
            if (UI_GameViewResolutionItems())
                MarkViewSettingsDirty();
            ImGui::EndMenu();
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("O'yin rejimida viewport rasmi shu nisbat / o'lchamga keltiriladi (atrofida qora chiziqlar); "
                        "katta o'lcham sig'masa kichraytirib ko'rsatiladi");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Parallax qatlamlari sahna ko'rinishida ham editor kamerasi bilan siljiydi (o'yinda doim)");
        if (ImGui::BeginMenu(Tr(ICON_FA_SHAPES "  Gizmolar")))
        {
            if (UI_ComponentGizmoSettings())
                MarkViewSettingsDirty();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  Gizmo o'lchami")))
        {
            if (UI_TransformGizmoSettings())
                MarkViewSettingsDirty();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_DRAW_POLYGON "  Tanlash ko'rinishi")))
        {
            if (UI_SelectionOutlineSettings())
                MarkViewSettingsDirty();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(Tr(ICON_FA_GAUGE_HIGH "  Statistika overlay"), nullptr, &m_ViewSettings.StatsOverlay.Visible)) MarkViewSettingsDirty();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Viewport ustida: FPS grafigi va kadr vaqti, draw call'lar, uchburchaklar, culling kesganlari");
        if (ImGui::BeginMenu(Tr(ICON_FA_SLIDERS "  Overlay sozlamalari")))
        {
            if (UI_StatsOverlaySettings())
                MarkViewSettingsDirty();
            ImGui::EndMenu();
        }
        ImGui::MenuItem(Tr(ICON_FA_LAYER_GROUP "  Shaffoflar tartibi"), nullptr, &m_ShowTransparentOrder);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Shaffof sirtlar qaysi tartibda chizilayotgani: har birining ustida raqami (1 - birinchi, eng orqadagi), "
                        "ustuvorligi va kameradan uzoqligi");
        if (ImGui::BeginMenu(Tr(ICON_FA_TAG "  Obyekt nomlari")))
        {
            if (UI_EntityLabelSettings())
                MarkViewSettingsDirty();
            ImGui::EndMenu();
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ko'rinishlarda obyektlar nomi: tanlanganlar yoki hammasi, uzoqlashganda xiralashadi");
        ImGui::Separator();
        if (ImGui::BeginMenu(Tr(ICON_FA_CUBE "  Ko'rinish o'qi (viewport)"), camera))
        {
            UI_ViewAxisItems(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_VIDEO "  Editor kamerasi")))
        {
            UI_CameraSettings(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Tr(ICON_FA_TABLE_COLUMNS "  Qo'shimcha ko'rinishlar")))
        {
            for (int i = 0; i < SceneViewSettings::Count; i++)
            {
                bool open = m_ViewSettings.SceneViews[i].Open;
                if (ImGui::MenuItem(std::format(ICON_FA_CUBE "  {} {}", Tr("Ko'rinish"), i + 2).c_str(), nullptr, open))
                    OpenSceneView(i, !open);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(ICON_FA_TABLE_CELLS_LARGE "  4 ko'rinish (2x2 joylashuv)")))
                RequestBuiltInLayout(BuiltInLayout::FourViews);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(Tr(ICON_FA_BULLSEYE "  Tanlanganga fokus"), "F", false, camera && m_SceneHierarchyPanel.GetSelectedEntity())) FrameSelection();
        if (ImGui::MenuItem(Tr(ICON_FA_EXPAND "  Hammasini ko'rsatish"), "Shift+F", false, camera)) FrameAll();
        if (ImGui::BeginMenu(Tr(ICON_FA_BOOKMARK "  Kamera xatcho'plari"), camera))
        {
            CameraBookmarks& bookmarks = GetSceneBookmarks();
            for (int slot = 1; slot <= CameraBookmarks::SlotCount; slot++)
            {
                std::string label = std::format(ICON_FA_CAMERA "  {} {}{}", Tr("Xatcho'p"), slot, bookmarks.Has(slot) ? "" : Tr("  (bo'sh)"));
                if (ImGui::MenuItem(label.c_str(), std::to_string(slot).c_str(), false, bookmarks.Has(slot)))
                    RecallCameraBookmark(slot);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu(Tr(ICON_FA_FLOPPY_DISK "  Joriy ko'rinishni saqlash")))
            {
                for (int slot = 1; slot <= CameraBookmarks::SlotCount; slot++)
                    if (ImGui::MenuItem(std::format("{} {}", Tr("Xatcho'p"), slot).c_str(), std::format("Ctrl+{}", slot).c_str()))
                        SaveCameraBookmark(slot);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(Tr(ICON_FA_TRASH "  Hammasini tozalash"), nullptr, false, !bookmarks.IsEmpty()))
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
        // The "no (active) primary camera" warning (UI_Viewport) takes the bottom line while
        // playing: the banner goes above it so both stay readable.
        const bool cameraWarning = m_SceneState == SceneState::Play && !GetViewScene().GetPrimaryCameraEntity();
        const float lift = cameraWarning ? ImGui::GetTextLineHeight() + 20.0f : 0.0f;
        banner(m_ViewportMessage.c_str(), alpha, max.y - 44.0f - lift);
    }

    void EditorLayer::ShowSceneLoadReport(const SceneLoadReport& report)
    {
        if (!report.HasIssues())
            return;
        m_LoadReport = report;
        m_LoadReportBanner = true;
        // A scene that could not be opened at all: the details at once.
        if (report.Failed())
            m_ShowLoadReport = true;
    }

    static std::string LoadReportFileName(const SceneLoadReport& report)
    {
        if (report.File.empty())
            return "(matn)";
        fs::path file(report.File);
        std::string relative = Project::GetActive() ? Project::GetAssetRelativePath(file) : std::string();
        return relative.empty() || relative.starts_with("..") ? file.filename().string() : relative;
    }

    void EditorLayer::UI_SceneLoadBanner()
    {
        if (!m_LoadReportBanner || !m_LoadReport.HasIssues())
            return;
        const bool failed = m_LoadReport.Failed();
        const bool errors = failed || m_LoadReport.Count(SceneLoadIssue::Severity::Error) > 0;
        const std::string name = LoadReportFileName(m_LoadReport);
        const std::string text = failed
            ? std::format(ICON_FA_CIRCLE_XMARK "  Sahna yuklanmadi: {} - {}", name, m_LoadReport.Summary())
            : std::format(ICON_FA_TRIANGLE_EXCLAMATION "  '{}' sahnasi yuklandi, lekin: {}", name, m_LoadReport.Summary());
        const ImGuiStyle& style = ImGui::GetStyle();
        const float detailsWidth = ImGui::CalcTextSize("Batafsil").x + style.FramePadding.x * 2.0f;
        const float closeWidth = ImGui::CalcTextSize(ICON_FA_XMARK).x + style.FramePadding.x * 2.0f;
        const float available = m_ViewportBounds[1].x - m_ViewportBounds[0].x - 40.0f;
        const float buttons = detailsWidth + closeWidth + style.ItemSpacing.x * 2.0f + 8.0f;
        const float width = std::min(available, ImGui::CalcTextSize(text.c_str()).x + buttons + 24.0f);
        const float toolbar = ImGui::GetFrameHeight() + 4.0f + 26.0f;
        ImGui::SetCursorScreenPos(ImVec2((m_ViewportBounds[0].x + m_ViewportBounds[1].x - width) * 0.5f, m_ViewportBounds[0].y + toolbar));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, errors ? ImVec4(0.42f, 0.10f, 0.10f, 0.94f) : ImVec4(0.40f, 0.28f, 0.06f, 0.94f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.18f));
        if (ImGui::BeginChild("##SceneLoadBanner", ImVec2(width, 0.0f),
                              ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
        {
            const float textWidth = std::max(40.0f, ImGui::GetContentRegionAvail().x - buttons);
            ImGui::AlignTextToFramePadding();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::SameLine(ImGui::GetContentRegionMax().x - detailsWidth - closeWidth - style.ItemSpacing.x);
            if (ImGui::Button(Tr("Batafsil")))
                m_ShowLoadReport = true;
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK "##closeLoadBanner"))
                m_LoadReportBanner = false;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Yopish");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::UI_SceneLoadReport()
    {
        if (!m_ShowLoadReport)
            return;
        ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
        ImGuiViewport* main = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(main->WorkPos.x + main->WorkSize.x * 0.5f, main->WorkPos.y + main->WorkSize.y * 0.4f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(Tr(ICON_FA_TRIANGLE_EXCLAMATION "  Sahna yuklash muammolari###SceneLoadReport"), &m_ShowLoadReport, ImGuiWindowFlags_NoDocking))
        {
            ImGui::End();
            return;
        }
        const SceneLoadReport& report = m_LoadReport;
        const bool failed = report.Failed();
        ImGui::TextUnformatted(LoadReportFileName(report).c_str());
        if (ImGui::IsItemHovered() && !report.File.empty())
            ImGui::SetTooltip("%s", report.File.c_str());
        ImGui::SameLine();
        if (failed)
            ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.38f, 1.0f), " - yuklanmadi (oldingi sahna qoldi)");
        else
            ImGui::TextDisabled(" - %zu ta obyekt yuklandi%s", report.EntitiesLoaded,
                                report.EntitiesSkipped ? std::format(", {} ta yozuv o'tkazib yuborildi", report.EntitiesSkipped).c_str() : "");
        ImGui::TextDisabled("%s", report.Summary().c_str());
        ImGui::Separator();

        const float footer = ImGui::GetFrameHeightWithSpacing() + 6.0f;
        if (ImGui::BeginTable("##loadIssues", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                                    ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0.0f, -footer)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##level", ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Turi", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Obyekt", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Tafsilot", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            int row = 0;
            for (const SceneLoadIssue& issue : report.Issues)
            {
                ImGui::PushID(row++);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool warning = issue.Level == SceneLoadIssue::Severity::Warning;
                ImGui::TextColored(warning ? ImVec4(0.96f, 0.74f, 0.30f, 1.0f) : ImVec4(0.95f, 0.40f, 0.36f, 1.0f), "%s",
                                   warning ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_XMARK);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(SceneLoadIssue::KindName(issue.Type));
                ImGui::TableNextColumn();
                if (!issue.Entity.empty())
                {
                    // Click: the entity in the hierarchy (when it is in the open scene).
                    Entity target = issue.EntityID && m_ActiveScene ? m_ActiveScene->GetEntityByUUID(issue.EntityID) : Entity();
                    if (target && ImGui::Selectable(issue.Entity.c_str(), false, ImGuiSelectableFlags_None))
                        m_SceneHierarchyPanel.SetSelectedEntity(target);
                    else if (!target)
                        ImGui::TextUnformatted(issue.Entity.c_str());
                    if (target && ImGui::IsItemHovered())
                        ImGui::SetTooltip("Ierarxiyada tanlash");
                }
                else
                    ImGui::TextDisabled("%s", issue.Component == "Settings" ? "sozlamalar" : "fayl");
                ImGui::TableNextColumn();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(issue.Message.c_str());
                if (!issue.SourceLine.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.80f, 0.88f, 1.0f));
                    const float lineX = ImGui::GetCursorPosX();
                    ImGui::TextUnformatted(issue.SourceLine.c_str());
                    ImGui::PopStyleColor();
                    if (issue.Column > 0)
                    {
                        // A caret under the character the parser stopped at (measured: the font is proportional).
                        const size_t before = (size_t)std::clamp(issue.Column - 1, 0, (int)issue.SourceLine.size());
                        ImGui::SetCursorPosX(lineX + ImGui::CalcTextSize(issue.SourceLine.c_str(), issue.SourceLine.c_str() + before).x);
                        ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.36f, 1.0f), "^");
                    }
                }
                ImGui::PopTextWrapPos();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::Button(Tr(ICON_FA_COPY "  Nusxa olish")))
            ImGui::SetClipboardText((LoadReportFileName(report) + "\n" + report.Format(1000)).c_str());
        ImGui::SameLine();
        if (!report.File.empty() && fs::exists(fs::path(report.File)) && m_SceneState == SceneState::Edit)
        {
            if (ImGui::Button(Tr(ICON_FA_ROTATE "  Qayta ochish")))
            {
                m_ShowLoadReport = false;
                m_LoadReportBanner = false;
                OpenScene(fs::path(report.File));
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(Tr("Yopish")))
        {
            m_ShowLoadReport = false;
            m_LoadReportBanner = false;
        }
        ImGui::End();
    }

    void EditorLayer::UI_TransparentOrder()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        glm::vec2 size = m_ViewportBounds[1] - m_ViewportBounds[0];
        // A legend in the bottom left corner.
        {
            std::string legend = m_TransparentOrder.empty()
                ? std::string(ICON_FA_LAYER_GROUP "  Shaffoflar tartibi: shaffof sirt chizilmadi")
                : std::format(ICON_FA_LAYER_GROUP "  Shaffoflar tartibi: {} ta - 1 birinchi chiziladi (eng orqada), keyingilari ustiga", m_TransparentOrder.size());
            ImVec2 ts = ImGui::CalcTextSize(legend.c_str());
            ImVec2 a(m_ViewportBounds[0].x + 10.0f, m_ViewportBounds[1].y - ts.y - 22.0f);
            dl->AddRectFilled(a, ImVec2(a.x + ts.x + 20.0f, a.y + ts.y + 12.0f), IM_COL32(18, 20, 24, 210), 6.0f);
            dl->AddText(ImVec2(a.x + 10.0f, a.y + 6.0f), IM_COL32(230, 234, 240, 255), legend.c_str());
        }
        // Labels are pushed down until they do not cover one another (several surfaces are often
        // close together on screen - that is when the order matters).
        std::vector<std::pair<ImVec2, ImVec2>> placed;
        auto overlaps = [&](ImVec2 a, ImVec2 b) {
            for (const auto& [pa, pb] : placed)
                if (a.x < pb.x && b.x > pa.x && a.y < pb.y && b.y > pa.y)
                    return true;
            return false;
        };
        for (size_t i = 0; i < m_TransparentOrder.size(); i++)
        {
            const Renderer3D::TransparentDrawRecord& record = m_TransparentOrder[i];
            glm::vec4 clip = m_TransparentOrderViewProjection * glm::vec4(record.Center, 1.0f);
            if (clip.w <= 0.0f)
                continue;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.05f || std::abs(ndc.y) > 1.05f)
                continue;
            ImVec2 anchor(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * size.x,
                          m_ViewportBounds[0].y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y);
            std::string name;
            if (record.EntityID >= 0)
            {
                Entity e((entt::entity)record.EntityID, m_ActiveScene.get());
                if (e.IsValid())
                    name = e.GetName();
            }
            std::string number = std::to_string(i + 1);
            std::string detail = std::format("{}{}{:.1f} m{}", name, name.empty() ? "" : "  ·  ", record.Distance,
                                             record.Priority != 0 ? std::format("  ·  ustuvorlik {}", record.Priority) : std::string());
            ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
            ImVec2 detailSize = ImGui::CalcTextSize(detail.c_str());
            float r = std::max(numberSize.x, numberSize.y) * 0.5f + 5.0f;
            float height = std::max(r * 2.0f, detailSize.y + 6.0f) + 2.0f;
            float width = r * 2.0f + 4.0f + detailSize.x + 12.0f;
            ImVec2 p = anchor;
            for (int tries = 0; tries < 12 && overlaps(ImVec2(p.x - r, p.y - height * 0.5f), ImVec2(p.x - r + width, p.y + height * 0.5f)); tries++)
                p.y += height;
            placed.emplace_back(ImVec2(p.x - r, p.y - height * 0.5f), ImVec2(p.x - r + width, p.y + height * 0.5f));
            if (p.y != anchor.y) // moved: a line back to where the surface is
                dl->AddLine(anchor, p, IM_COL32(255, 255, 255, 140), 1.5f);
            ImU32 fill = record.Priority != 0 ? IM_COL32(230, 150, 40, 235) : IM_COL32(40, 150, 220, 235);
            dl->AddCircleFilled(p, r, fill, 20);
            dl->AddCircle(p, r, IM_COL32(255, 255, 255, 200), 20, 1.5f);
            dl->AddText(ImVec2(p.x - numberSize.x * 0.5f, p.y - numberSize.y * 0.5f), IM_COL32(255, 255, 255, 255), number.c_str());
            ImVec2 a(p.x + r + 4.0f, p.y - detailSize.y * 0.5f - 3.0f);
            dl->AddRectFilled(a, ImVec2(a.x + detailSize.x + 12.0f, a.y + detailSize.y + 6.0f), IM_COL32(18, 20, 24, 200), 5.0f);
            dl->AddText(ImVec2(a.x + 6.0f, a.y + 3.0f), IM_COL32(230, 234, 240, 255), detail.c_str());
        }
    }

    void EditorLayer::UI_JointLabels()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin(m_ViewportBounds[0].x, m_ViewportBounds[0].y);
        dl->PushClipRect(origin, ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);
        for (const JointLabel& label : m_JointLabels)
        {
            if (label.Name.empty())
                continue;
            const ImVec2 size = ImGui::CalcTextSize(label.Name.c_str());
            const ImVec2 at(origin.x + label.Position.x + 7.0f, origin.y + label.Position.y - size.y * 0.5f - 7.0f);
            const int alpha = label.Selected ? 215 : 140;
            dl->AddRectFilled(ImVec2(at.x - 3.0f, at.y - 1.0f), ImVec2(at.x + size.x + 3.0f, at.y + size.y + 1.0f), IM_COL32(14, 16, 20, alpha * 3 / 4), 3.0f);
            dl->AddText(at, IM_COL32(255, 222, 140, alpha + 40), label.Name.c_str());
        }
        dl->PopClipRect();
    }

    bool EditorLayer::IsColliderPointEditing() const
    {
        return m_SceneState == SceneState::Edit && m_SceneHierarchyPanel.GetColliderPointEdit() != SceneHierarchyPanel::ColliderPointKind::None;
    }

    bool EditorLayer::IsJointAnchorEditing() const
    {
        return m_SceneState == SceneState::Edit && m_SceneHierarchyPanel.GetJointAnchorEdit() >= 0;
    }

    void EditorLayer::UI_JointAnchorEditor()
    {
        const int typeIndex = m_SceneState == SceneState::Edit ? m_SceneHierarchyPanel.GetJointAnchorEdit() : -1;
        if (typeIndex < 0)
        {
            m_JointDrag = m_JointHover = -1;
            return;
        }
        enum Handle { AnchorHandle = 0, ConnectedHandle, AxisHandle, LowerHandle, UpperHandle, HandleCount };
        const Joint2DType type = (Joint2DType)typeIndex;
        Entity e = m_SceneHierarchyPanel.GetSelectedEntity();
        Scene& scene = *m_ActiveScene;
        Joint2DComponentBase* joint = nullptr;
        glm::vec2* axis = nullptr;          // prismatic / wheel
        bool* limited = nullptr;
        float* lower = nullptr;             // revolute: degrees; prismatic / wheel: units
        float* upper = nullptr;
        const char* typeName = "";
        switch (type)
        {
            case Joint2DType::Distance:
                joint = &e.GetComponent<DistanceJoint2DComponent>();
                typeName = "Distance";
                break;
            case Joint2DType::Revolute:
            {
                auto& c = e.GetComponent<RevoluteJoint2DComponent>();
                joint = &c;
                limited = &c.EnableLimit;
                lower = &c.LowerAngle;
                upper = &c.UpperAngle;
                typeName = "Revolute";
                break;
            }
            case Joint2DType::Prismatic:
            {
                auto& c = e.GetComponent<PrismaticJoint2DComponent>();
                joint = &c;
                axis = &c.Axis;
                limited = &c.EnableLimit;
                lower = &c.LowerTranslation;
                upper = &c.UpperTranslation;
                typeName = "Prismatic";
                break;
            }
            case Joint2DType::Weld:
                joint = &e.GetComponent<WeldJoint2DComponent>();
                typeName = "Weld";
                break;
            case Joint2DType::Wheel:
            {
                auto& c = e.GetComponent<WheelJoint2DComponent>();
                joint = &c;
                axis = &c.Axis;
                limited = &c.EnableLimit;
                lower = &c.LowerTranslation;
                upper = &c.UpperTranslation;
                typeName = "Wheel";
                break;
            }
        }

        // The frames: this body's (position, rotation; joint points are not scaled) and the connected
        // body's (or the world).
        auto rotate = [](const glm::vec2& v, float a) { const float c = std::cos(a), s = std::sin(a); return glm::vec2(c * v.x - s * v.y, s * v.x + c * v.y); };
        glm::vec3 p, r, sc;
        Math::DecomposeTransform(scene.GetWorldTransform(e), p, r, sc);
        const glm::vec2 origin(p);
        Entity other = joint->ConnectedBody ? scene.GetEntityByUUID(joint->ConnectedBody.ID) : Entity();
        const bool connected = other && other != e;
        glm::vec2 otherOrigin(0.0f);
        float otherAngle = 0.0f;
        if (connected)
        {
            glm::vec3 op, orot, osc;
            Math::DecomposeTransform(scene.GetWorldTransform(other), op, orot, osc);
            otherOrigin = glm::vec2(op);
            otherAngle = orot.z;
        }
        auto connectedToWorld = [&](const glm::vec2& l) { return connected ? otherOrigin + rotate(l, otherAngle) : l; };
        auto worldToConnected = [&](const glm::vec2& w) { return connected ? rotate(w - otherOrigin, -otherAngle) : w; };
        const glm::vec2 anchor = origin + rotate(joint->Anchor, r.z);
        const bool autoConnected = joint->AutoConfigureConnectedAnchor;

        const glm::mat4 viewProjection = OverlayViewProjection();
        const glm::vec2 viewport = m_ViewportBounds[1] - m_ViewportBounds[0];
        auto toScreen = [&](const glm::vec2& w, bool& visible) {
            glm::vec4 clip = viewProjection * glm::vec4(w, p.z, 1.0f);
            visible = clip.w > 0.0f;
            glm::vec2 ndc = visible ? glm::vec2(clip) / clip.w : glm::vec2(0.0f);
            return ImVec2(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * viewport.x, m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * viewport.y);
        };
        bool visible = true, visible2 = true;
        const ImVec2 a0 = toScreen(anchor, visible), a1 = toScreen(anchor + glm::vec2(1.0f, 0.0f), visible2);
        if (!visible || !visible2)
            return;
        const float pixelsPerUnit = std::max(1e-3f, std::hypot(a1.x - a0.x, a1.y - a0.y));

        // Where the handles are.
        glm::vec2 handles[HandleCount];
        bool present[HandleCount] = {};
        handles[AnchorHandle] = anchor;
        present[AnchorHandle] = true;
        handles[ConnectedHandle] = autoConnected ? anchor : connectedToWorld(joint->ConnectedAnchor);
        present[ConnectedHandle] = !autoConnected;
        glm::vec2 dir(1.0f, 0.0f);
        if (axis)
        {
            dir = glm::length(*axis) > 1e-5f ? glm::normalize(rotate(*axis, r.z)) : glm::vec2(1.0f, 0.0f);
            handles[AxisHandle] = anchor + dir * (70.0f / pixelsPerUnit);
            present[AxisHandle] = true;
        }
        // Revolute limits: on the arc the body's centre sweeps (as the joint gizmo draws it).
        const glm::vec2 arm = origin - anchor;
        const float radius = glm::length(arm) > 0.1f ? glm::length(arm) : 0.5f;
        const float base = glm::length(arm) > 0.1f ? std::atan2(arm.y, arm.x) : r.z;
        if (limited && *limited)
        {
            if (type == Joint2DType::Revolute)
            {
                handles[LowerHandle] = anchor + glm::vec2(std::cos(base + glm::radians(*lower)), std::sin(base + glm::radians(*lower))) * radius;
                handles[UpperHandle] = anchor + glm::vec2(std::cos(base + glm::radians(*upper)), std::sin(base + glm::radians(*upper))) * radius;
            }
            else
            {
                // Beside the axis (a limit of 0 would sit on the anchor): dragging goes along it.
                const glm::vec2 side = glm::vec2(-dir.y, dir.x) * (16.0f / pixelsPerUnit);
                handles[LowerHandle] = anchor + dir * *lower + side;
                handles[UpperHandle] = anchor + dir * *upper + side;
            }
            present[LowerHandle] = present[UpperHandle] = true;
        }
        ImVec2 screen[HandleCount];
        for (int i = 0; i < HandleCount; i++)
            if (present[i])
            {
                screen[i] = toScreen(handles[i], visible);
                present[i] = visible;
            }

        // Hover: the nearest handle within 9 px (later ones win ties: limits over the anchor).
        const ImVec2 mouse = ImGui::GetMousePos();
        int hover = -1;
        if (!m_JointHoverOverride.empty())
        {
            static const char* names[HandleCount] = { "anchor", "connected", "axis", "lower", "upper" };
            for (int i = 0; i < HandleCount; i++)
                if (m_JointHoverOverride == names[i] && present[i])
                    hover = i;
        }
        else if (m_ViewportHovered && m_JointDrag < 0)
        {
            float best = 9.0f;
            for (int i = 0; i < HandleCount; i++)
                if (present[i])
                    if (float d = std::hypot(screen[i].x - mouse.x, screen[i].y - mouse.y); d <= best)
                    {
                        best = d;
                        hover = i;
                    }
        }
        m_JointHover = hover;

        // Dragging.
        const ImGuiIO& io = ImGui::GetIO();
        bool changed = false;
        if (m_JointDrag >= 0)
        {
            if (!present[m_JointDrag] || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_JointDrag = -1;
            else if (glm::vec3 hit; ViewportMouseToPlane(p.z, hit))
            {
                const glm::vec2 w = glm::vec2(hit) + m_JointDragOffset;
                auto snap = [&](float v, float step) { return io.KeyShift ? std::round(v / step) * step : v; };
                auto set = [&](float& field, float value) {
                    if (field != value)
                    {
                        field = value;
                        changed = true;
                    }
                };
                switch (m_JointDrag)
                {
                    case AnchorHandle:
                    {
                        const glm::vec2 l = rotate(w - origin, -r.z);
                        set(joint->Anchor.x, snap(l.x, 0.1f));
                        set(joint->Anchor.y, snap(l.y, 0.1f));
                        break;
                    }
                    case ConnectedHandle:
                    {
                        const glm::vec2 l = worldToConnected(w);
                        set(joint->ConnectedAnchor.x, snap(l.x, 0.1f));
                        set(joint->ConnectedAnchor.y, snap(l.y, 0.1f));
                        break;
                    }
                    case AxisHandle:
                    {
                        const glm::vec2 d = w - anchor;
                        if (glm::length(d) > 1e-4f)
                        {
                            const float local = snap(glm::degrees(std::atan2(d.y, d.x) - r.z), 5.0f);
                            set(axis->x, std::cos(glm::radians(local)));
                            set(axis->y, std::sin(glm::radians(local)));
                        }
                        break;
                    }
                    case LowerHandle:
                    case UpperHandle:
                    {
                        float& field = m_JointDrag == LowerHandle ? *lower : *upper;
                        float value;
                        if (type == Joint2DType::Revolute)
                        {
                            // Continuous: past +-180 the angle goes on (the limits may span more than a turn).
                            const glm::vec2 d = w - anchor;
                            float delta = glm::degrees(std::atan2(d.y, d.x) - base) - field;
                            delta = std::remainder(delta, 360.0f);
                            value = std::clamp(snap(field + delta, 5.0f), -360.0f, 360.0f);
                        }
                        else
                            value = snap(glm::dot(w - anchor, dir), 0.1f);
                        // Lower stays below upper (the handle stops at the other one).
                        value = m_JointDrag == LowerHandle ? std::min(value, *upper) : std::max(value, *lower);
                        set(field, value);
                        break;
                    }
                }
            }
        }
        else if (hover >= 0 && m_ViewportHovered && m_JointHoverOverride.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt &&
                 !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            glm::vec3 hit;
            m_JointDrag = hover;
            m_JointDragOffset = ViewportMouseToPlane(p.z, hit) ? handles[hover] - glm::vec2(hit) : glm::vec2(0.0f);
        }
        if (changed)
        {
            MarkDirty();
            return; // drawn where it is now next frame
        }

        // Drawing: the lines between the handles, the handles, a label for the hovered / dragged one, a hint.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y), ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);
        const ImU32 shadow = IM_COL32(10, 12, 16, 170), cyan = IM_COL32(90, 220, 255, 235), purple = IM_COL32(190, 140, 255, 240);
        const ImU32 orange = IM_COL32(255, 170, 70, 240), yellow = IM_COL32(255, 215, 80, 240);
        auto line = [&](const ImVec2& from, const ImVec2& to, ImU32 color) {
            dl->AddLine(from, to, shadow, 4.0f);
            dl->AddLine(from, to, color, 2.0f);
        };
        if (present[ConnectedHandle])
            line(screen[AnchorHandle], screen[ConnectedHandle], purple);
        if (present[AxisHandle])
            line(screen[AnchorHandle], screen[AxisHandle], orange);
        if (present[LowerHandle] && type != Joint2DType::Revolute)
        {
            // The slide range beside the axis, with stems to where the limits are on it.
            line(screen[LowerHandle], screen[UpperHandle], yellow);
            for (int i : { LowerHandle, UpperHandle })
            {
                bool shown = true;
                const float t = i == LowerHandle ? *lower : *upper;
                dl->AddLine(screen[i], toScreen(anchor + dir * t, shown), yellow, 1.5f);
            }
        }
        for (int i = 0; i < HandleCount; i++)
        {
            if (!present[i])
                continue;
            const bool hovered = i == hover, dragged = i == m_JointDrag;
            const float size = hovered || dragged ? 7.5f : 6.0f;
            const ImVec2 c = screen[i];
            const ImU32 fill = dragged ? IM_COL32(255, 160, 60, 255) : hovered ? IM_COL32(255, 235, 140, 255) : IM_COL32(240, 248, 255, 255);
            switch (i)
            {
                case AnchorHandle:
                    dl->AddCircleFilled(c, size + 1.5f, shadow);
                    dl->AddCircleFilled(c, size, fill);
                    dl->AddCircle(c, size, cyan, 0, 2.0f);
                    break;
                case ConnectedHandle:
                {
                    const ImVec2 q[4] = { ImVec2(c.x + size + 1, c.y), ImVec2(c.x, c.y - size - 1), ImVec2(c.x - size - 1, c.y), ImVec2(c.x, c.y + size + 1) };
                    dl->AddConvexPolyFilled(q, 4, fill);
                    dl->AddPolyline(q, 4, purple, ImDrawFlags_Closed, 2.0f);
                    break;
                }
                case AxisHandle:
                    dl->AddRectFilled(ImVec2(c.x - size + 1, c.y - size + 1), ImVec2(c.x + size - 1, c.y + size - 1), fill, 2.0f);
                    dl->AddRect(ImVec2(c.x - size + 1, c.y - size + 1), ImVec2(c.x + size - 1, c.y + size - 1), orange, 2.0f, 0, 2.0f);
                    break;
                default:
                    dl->AddCircleFilled(c, size - 0.5f, fill);
                    dl->AddCircle(c, size - 0.5f, yellow, 0, 2.0f);
                    dl->AddText(ImVec2(c.x - 3.5f, c.y - 7.0f), IM_COL32(30, 30, 30, 255), i == LowerHandle ? "-" : "+");
                    break;
            }
        }
        if (const int shown = m_JointDrag >= 0 ? m_JointDrag : hover; shown >= 0)
        {
            std::string label;
            switch (shown)
            {
                case AnchorHandle: label = std::format("Langar  ({:.2f}, {:.2f})", joint->Anchor.x, joint->Anchor.y); break;
                case ConnectedHandle:
                    label = std::format("{}  ({:.2f}, {:.2f})", connected ? "Ulangan langar" : "Dunyodagi nuqta", joint->ConnectedAnchor.x, joint->ConnectedAnchor.y);
                    break;
                case AxisHandle: label = std::format("O'q  {:.1f}\xc2\xb0", glm::degrees(std::atan2(axis->y, axis->x))); break;
                case LowerHandle:
                    label = type == Joint2DType::Revolute ? std::format("Pastki burchak  {:.1f}\xc2\xb0", *lower) : std::format("Pastki chegara  {:.2f}", *lower);
                    break;
                default:
                    label = type == Joint2DType::Revolute ? std::format("Yuqori burchak  {:.1f}\xc2\xb0", *upper) : std::format("Yuqori chegara  {:.2f}", *upper);
                    break;
            }
            const ImVec2 at(screen[shown].x + 12.0f, screen[shown].y - 24.0f);
            const ImVec2 size = ImGui::CalcTextSize(label.c_str());
            dl->AddRectFilled(ImVec2(at.x - 4.0f, at.y - 2.0f), ImVec2(at.x + size.x + 4.0f, at.y + size.y + 2.0f), IM_COL32(14, 16, 20, 215), 3.0f);
            dl->AddText(at, IM_COL32(235, 240, 245, 255), label.c_str());
        }
        // The hint: what the handles are, in as many lines as the viewport's width needs.
        std::vector<std::string> parts = { std::format(ICON_FA_ANCHOR "  {} joint:  doira - langar", typeName),
                                           autoConnected ? "ulangan langar avtomatik" : "romb - ulangan langar" };
        if (axis)
            parts.push_back("kvadrat - o'q");
        if (limited && *limited)
            parts.push_back("+/- - chegaralar");
        parts.push_back("Shift - mayda qadam");
        parts.push_back("Esc - tugatish");
        std::vector<std::string> lines(1);
        for (const std::string& part : parts)
        {
            std::string joined = lines.back().empty() ? part : lines.back() + "  |  " + part;
            if (!lines.back().empty() && ImGui::CalcTextSize(joined.c_str()).x > viewport.x - 60.0f)
                lines.push_back(part);
            else
                lines.back() = std::move(joined);
        }
        float hintY = m_ViewportBounds[0].y + 52.0f;
        float width = 0.0f;
        for (const std::string& l : lines)
            width = std::max(width, ImGui::CalcTextSize(l.c_str()).x);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float hintX = m_ViewportBounds[0].x + (viewport.x - width) * 0.5f;
        dl->AddRectFilled(ImVec2(hintX - 10.0f, hintY - 5.0f), ImVec2(hintX + width + 10.0f, hintY + lineHeight * lines.size() + 3.0f),
                          IM_COL32(16, 20, 26, 215), 6.0f);
        for (const std::string& l : lines)
        {
            dl->AddText(ImVec2(m_ViewportBounds[0].x + (viewport.x - ImGui::CalcTextSize(l.c_str()).x) * 0.5f, hintY), IM_COL32(150, 225, 255, 255), l.c_str());
            hintY += lineHeight;
        }
        dl->PopClipRect();
    }

    bool EditorLayer::IsJoint3DAnchorEditing() const
    {
        return m_SceneState == SceneState::Edit && m_SceneHierarchyPanel.GetJoint3DAnchorEdit() >= 0;
    }

    bool EditorLayer::ViewportMouseRay(glm::vec3& origin, glm::vec3& direction)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const glm::vec2 size = m_ViewportBounds[1] - m_ViewportBounds[0];
        if (size.x <= 0 || size.y <= 0)
            return false;
        const glm::vec2 ndc = { (mouse.x - m_ViewportBounds[0].x) / size.x * 2.0f - 1.0f, 1.0f - (mouse.y - m_ViewportBounds[0].y) / size.y * 2.0f };
        const glm::mat4 inv = glm::inverse(m_EditorCamera.GetViewProjection());
        const glm::vec4 n = inv * glm::vec4(ndc, -1.0f, 1.0f), f = inv * glm::vec4(ndc, 1.0f, 1.0f);
        origin = glm::vec3(n) / n.w;
        const glm::vec3 d = glm::vec3(f) / f.w - origin;
        if (glm::length(d) < 1e-8f)
            return false;
        direction = glm::normalize(d);
        return true;
    }

    void EditorLayer::UI_Joint3DAnchorEditor()
    {
        const int typeIndex = m_SceneState == SceneState::Edit ? m_SceneHierarchyPanel.GetJoint3DAnchorEdit() : -1;
        Entity e = m_SceneHierarchyPanel.GetSelectedEntity();
        if (typeIndex < 0 || (uint64_t)e.GetUUID() != m_Joint3DEntity || typeIndex != m_Joint3DType)
        {
            // Another joint (or none): nothing is held or picked from before.
            m_Joint3DDrag = m_Joint3DHover = m_Joint3DPicked = -1;
            m_Joint3DEntity = typeIndex >= 0 ? (uint64_t)e.GetUUID() : 0;
            m_Joint3DType = typeIndex;
            if (typeIndex < 0)
                return;
        }
        enum Handle { AnchorHandle = 0, ConnectedHandle, AxisHandle, LowerHandle, UpperHandle, SwingHandle, HandleCount };
        const Joint3DType type = (Joint3DType)typeIndex;
        Scene& scene = *m_ActiveScene;
        Joint3DComponentBase* joint = nullptr;
        glm::vec3* axis = nullptr;  // hinge / slider / cone
        bool* limited = nullptr;    // hinge / slider (a cone's twist is always limited)
        float* lower = nullptr;     // hinge / cone twist: degrees; slider: units; distance: the shortest length
        float* upper = nullptr;     // ... distance: the longest length
        float* swing = nullptr;     // cone
        DistanceJoint3DComponent* distance = nullptr;
        const char* typeName = "";
        switch (type)
        {
            case Joint3DType::Fixed:
                joint = &e.GetComponent<FixedJoint3DComponent>();
                typeName = "Fixed";
                break;
            case Joint3DType::Hinge:
            {
                auto& c = e.GetComponent<HingeJoint3DComponent>();
                joint = &c;
                axis = &c.Axis;
                limited = &c.EnableLimit;
                lower = &c.LowerAngle;
                upper = &c.UpperAngle;
                typeName = "Hinge";
                break;
            }
            case Joint3DType::Distance:
            {
                auto& c = e.GetComponent<DistanceJoint3DComponent>();
                joint = distance = &c;
                if (!c.AutoConfigureDistance)
                {
                    lower = &c.MinDistance;
                    upper = &c.MaxDistance;
                }
                typeName = "Distance";
                break;
            }
            case Joint3DType::Slider:
            {
                auto& c = e.GetComponent<SliderJoint3DComponent>();
                joint = &c;
                axis = &c.Axis;
                limited = &c.EnableLimit;
                lower = &c.LowerTranslation;
                upper = &c.UpperTranslation;
                typeName = "Slider";
                break;
            }
            case Joint3DType::Cone:
            {
                auto& c = e.GetComponent<ConeJoint3DComponent>();
                joint = &c;
                axis = &c.Axis;
                lower = &c.TwistLower;
                upper = &c.TwistUpper;
                swing = &c.SwingLimit;
                typeName = "Cone";
                break;
            }
        }
        const bool angles = type == Joint3DType::Hinge || type == Joint3DType::Cone;
        const bool hasLimits = lower && (!limited || *limited);

        // The frames: this entity's (anchors scaled with it, axes turned with it) and the connected
        // body's (or the world).
        const glm::mat4 world = scene.GetWorldTransform(e);
        glm::vec3 p, r, sc;
        Math::DecomposeTransform(world, p, r, sc);
        const glm::quat q(r);
        Entity other = joint->ConnectedBody ? scene.GetEntityByUUID(joint->ConnectedBody.ID) : Entity();
        const bool connected = other && other != e;
        const glm::mat4 otherWorld = connected ? scene.GetWorldTransform(other) : glm::mat4(1.0f);
        glm::quat otherRotation(1.0f, 0.0f, 0.0f, 0.0f);
        if (connected)
        {
            glm::vec3 op, orot, osc;
            Math::DecomposeTransform(otherWorld, op, orot, osc);
            otherRotation = glm::quat(orot);
        }
        const bool autoConnected = joint->AutoConfigureConnectedAnchor;
        const glm::vec3 anchor = glm::vec3(world * glm::vec4(joint->Anchor, 1.0f));
        const glm::vec3 connectedPoint = autoConnected ? anchor : glm::vec3(otherWorld * glm::vec4(joint->ConnectedAnchor, 1.0f));
        const glm::vec3 dir = axis && glm::length(*axis) > 1e-5f ? glm::normalize(q * *axis)
                                                                  : (type == Joint3DType::Slider ? q * glm::vec3(1, 0, 0) : q * glm::vec3(0, 1, 0));

        const glm::mat4 viewProjection = m_EditorCamera.GetViewProjection();
        const glm::vec2 viewport = m_ViewportBounds[1] - m_ViewportBounds[0];
        auto toScreen = [&](const glm::vec3& w, bool& visible) {
            const glm::vec4 clip = viewProjection * glm::vec4(w, 1.0f);
            visible = clip.w > 1e-5f;
            const glm::vec2 ndc = visible ? glm::vec2(clip) / clip.w : glm::vec2(0.0f);
            return ImVec2(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * viewport.x, m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * viewport.y);
        };
        // World units per pixel at the anchor (handles keep their size on screen).
        const glm::vec3 viewDir = glm::normalize(m_EditorCamera.GetForwardDirection());
        const glm::vec3 screenRight = glm::normalize(glm::vec3(glm::inverse(m_EditorCamera.GetViewMatrix())[0]));
        bool visible = true, visible2 = true;
        const ImVec2 a0 = toScreen(anchor, visible), a1 = toScreen(anchor + screenRight, visible2);
        if (!visible || !visible2)
            return;
        const float pixel = 1.0f / std::max(1e-3f, std::hypot(a1.x - a0.x, a1.y - a0.y));

        // Where the handles are (the same places as the joint gizmo's shapes).
        glm::vec3 handles[HandleCount];
        bool present[HandleCount] = {};
        handles[AnchorHandle] = anchor;
        present[AnchorHandle] = true;
        handles[ConnectedHandle] = connectedPoint;
        present[ConnectedHandle] = !autoConnected;
        if (axis)
        {
            handles[AxisHandle] = anchor + dir * (type == Joint3DType::Cone ? std::max(0.45f, 9.0f * std::clamp(pixel * 7.0f, 0.03f, 0.25f)) * 1.15f
                                                                             : 80.0f * pixel);
            present[AxisHandle] = true;
        }
        // A frame at right angles to the axis, as the gizmo draws it.
        const glm::vec3 helper = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 side = glm::normalize(glm::cross(dir, helper));
        // Hinge: the arc the body's centre sweeps around the axis.
        glm::vec3 arcA = side, arcB = glm::cross(dir, side);
        float arcRadius = 0.5f;
        glm::vec3 arcCenter = anchor;
        if (type == Joint3DType::Hinge)
        {
            glm::vec3 arm = p - anchor;
            arm -= dir * glm::dot(arm, dir);
            if (glm::length(arm) > 0.05f)
            {
                arcRadius = glm::length(arm);
                arcA = arm / arcRadius;
            }
            arcB = glm::cross(dir, arcA);
        }
        // Cone: the twist arc around the axis, and the swing handle on the cone's rim in the plane that
        // faces the camera best.
        const float coneLength = std::max(0.45f, std::clamp(pixel * 7.0f, 0.03f, 0.25f) * 9.0f);
        glm::vec3 swingSide = glm::cross(dir, viewDir);
        swingSide = glm::length(swingSide) > 1e-3f ? glm::normalize(swingSide) : side;
        if (type == Joint3DType::Cone)
        {
            arcCenter = anchor + dir * coneLength * 0.55f;
            arcRadius = coneLength * 0.3f;
            arcA = side;
            arcB = glm::cross(dir, side);
            const float s = glm::radians(std::clamp(*swing, 0.0f, 180.0f));
            handles[SwingHandle] = anchor + (dir * std::cos(s) + swingSide * std::sin(s)) * coneLength;
            present[SwingHandle] = true;
        }
        // Distance: along the rod from the connected anchor.
        glm::vec3 rod = anchor - connectedPoint;
        rod = glm::length(rod) > 1e-4f ? glm::normalize(rod) : glm::vec3(0.0f, -1.0f, 0.0f);
        const glm::vec3 rodSide = [&]() {
            glm::vec3 s = glm::cross(rod, viewDir);
            return glm::length(s) > 1e-3f ? glm::normalize(s) : glm::normalize(glm::cross(rod, std::abs(rod.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
        }();
        const glm::vec3 slideSide = [&]() {
            glm::vec3 s = glm::cross(dir, viewDir);
            return glm::length(s) > 1e-3f ? glm::normalize(s) : side;
        }();
        if (hasLimits)
        {
            switch (type)
            {
                case Joint3DType::Hinge:
                case Joint3DType::Cone:
                    for (int i : { LowerHandle, UpperHandle })
                    {
                        const float t = glm::radians(i == LowerHandle ? *lower : *upper);
                        handles[i] = arcCenter + (arcA * std::cos(t) + arcB * std::sin(t)) * arcRadius;
                        present[i] = true;
                    }
                    break;
                case Joint3DType::Slider:
                    // Beside the axis (a limit of 0 would sit on the anchor): dragging goes along it.
                    handles[LowerHandle] = anchor + dir * *lower + slideSide * (16.0f * pixel);
                    handles[UpperHandle] = anchor + dir * *upper + slideSide * (16.0f * pixel);
                    present[LowerHandle] = present[UpperHandle] = true;
                    break;
                case Joint3DType::Distance:
                    handles[UpperHandle] = connectedPoint + rod * *upper;
                    present[UpperHandle] = true;
                    if (!distance->MaxDistanceOnly)
                    {
                        handles[LowerHandle] = connectedPoint + rod * *lower + rodSide * (16.0f * pixel);
                        present[LowerHandle] = true;
                    }
                    break;
                default:
                    break;
            }
        }
        ImVec2 screen[HandleCount];
        for (int i = 0; i < HandleCount; i++)
            if (present[i])
            {
                screen[i] = toScreen(handles[i], visible);
                present[i] &= visible;
            }
        if (m_Joint3DPicked >= 0 && !present[m_Joint3DPicked])
            m_Joint3DPicked = -1;

        const ImGuiIO& io = ImGui::GetIO();
        bool changed = false;
        auto set = [&](float& field, float value) {
            if (field != value)
            {
                field = value;
                changed = true;
            }
        };
        auto setVec = [&](glm::vec3& field, const glm::vec3& value) {
            for (int i = 0; i < 3; i++)
                set(field[i], value[i]);
        };
        auto snap = [&](float v, float step) { return io.KeyShift ? std::round(v / step) * step : v; };
        auto snapVec = [&](glm::vec3 v, float step) { return glm::vec3(snap(v.x, step), snap(v.y, step), snap(v.z, step)); };
        // A point of this body / the connected body (or the world) from a world position.
        auto setPoint = [&](int handle, const glm::vec3& w) {
            if (handle == AnchorHandle)
                setVec(joint->Anchor, snapVec(glm::vec3(glm::inverse(world) * glm::vec4(w, 1.0f)), 0.1f));
            else
                setVec(joint->ConnectedAnchor, snapVec(connected ? glm::vec3(glm::inverse(otherWorld) * glm::vec4(w, 1.0f)) : w, 0.1f));
        };

        // The picked point (anchor / connected anchor) also gets a translate gizmo: along its frame's axes.
        bool gizmoOver = false;
        if (m_Joint3DPicked == AnchorHandle || m_Joint3DPicked == ConnectedHandle)
        {
            ImGuizmo::SetOrthographic(m_EditorCamera.IsOrthographic());
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, viewport.x, viewport.y);
            const glm::quat frame = m_Joint3DPicked == AnchorHandle ? q : otherRotation;
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), handles[m_Joint3DPicked]) * glm::mat4_cast(frame);
            glm::mat4 view = m_EditorCamera.GetViewMatrix();
            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(m_EditorCamera.GetProjection()), ImGuizmo::TRANSLATE, ImGuizmo::LOCAL,
                                 glm::value_ptr(transform));
            gizmoOver = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            if (ImGuizmo::IsUsing())
                setPoint(m_Joint3DPicked, glm::vec3(transform[3]));
        }

        // Hover: the nearest handle within 9 px (later ones win ties: limits over the anchor).
        const ImVec2 mouse = ImGui::GetMousePos();
        int hover = -1;
        if (!m_JointHoverOverride.empty())
        {
            static const char* names[HandleCount] = { "anchor", "connected", "axis", "lower", "upper", "swing" };
            for (int i = 0; i < HandleCount; i++)
                if (m_JointHoverOverride == names[i] && present[i])
                    hover = i;
        }
        else if (m_ViewportHovered && m_Joint3DDrag < 0 && !gizmoOver)
        {
            float best = 9.0f;
            for (int i = 0; i < HandleCount; i++)
                if (present[i])
                    if (float d = std::hypot(screen[i].x - mouse.x, screen[i].y - mouse.y); d <= best)
                    {
                        best = d;
                        hover = i;
                    }
        }
        m_Joint3DHover = hover;

        // Where the mouse ray meets a plane (false: it runs along it or away from it).
        auto rayPlane = [&](const glm::vec3& point, const glm::vec3& normal, glm::vec3& hit) {
            glm::vec3 o, d;
            if (!ViewportMouseRay(o, d))
                return false;
            const float denom = glm::dot(d, normal);
            if (std::abs(denom) < 1e-4f)
                return false;
            const float t = glm::dot(point - o, normal) / denom;
            if (t < 0.0f)
                return false;
            hit = o + d * t;
            return true;
        };
        // How far along a line (point + dir * t) the mouse ray passes closest to it.
        auto rayLine = [&](const glm::vec3& point, const glm::vec3& lineDir, float& t) {
            glm::vec3 o, d;
            if (!ViewportMouseRay(o, d))
                return false;
            const glm::vec3 w = point - o;
            const float b = glm::dot(lineDir, d), denom = 1.0f - b * b;
            if (denom < 1e-5f)
                return false;
            t = (b * glm::dot(d, w) - glm::dot(lineDir, w)) / denom;
            return true;
        };
        // The angle (degrees) of the mouse around `axisDir` from `from` (the arc's 0), on the arc's plane.
        auto arcAngle = [&](const glm::vec3& center, const glm::vec3& axisDir, const glm::vec3& from, const glm::vec3& from90, float& degrees) {
            glm::vec3 hit;
            if (!rayPlane(center, axisDir, hit))
                return false;
            const glm::vec3 d = hit - center;
            if (glm::length(d) < 1e-5f)
                return false;
            degrees = glm::degrees(std::atan2(glm::dot(d, from90), glm::dot(d, from)));
            return true;
        };

        // Dragging.
        if (m_Joint3DDrag >= 0)
        {
            if (!present[m_Joint3DDrag] || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_Joint3DDrag = -1;
            else
                switch (m_Joint3DDrag)
                {
                    case AnchorHandle:
                    case ConnectedHandle:
                    {
                        // In the plane facing the camera through where the point was grabbed.
                        glm::vec3 hit;
                        if (rayPlane(m_Joint3DDragPlane, viewDir, hit))
                            setPoint(m_Joint3DDrag, hit + m_Joint3DDragOffset);
                        break;
                    }
                    case AxisHandle:
                    {
                        // The tip follows the mouse in the plane facing the camera through the anchor;
                        // Shift: the nearest of the entity's own axes.
                        glm::vec3 hit;
                        if (rayPlane(anchor, viewDir, hit) && glm::length(hit - anchor) > 1e-4f)
                        {
                            glm::vec3 local = glm::normalize(glm::inverse(q) * glm::normalize(hit - anchor));
                            if (io.KeyShift)
                            {
                                int best = 0;
                                for (int i = 1; i < 3; i++)
                                    if (std::abs(local[i]) > std::abs(local[best]))
                                        best = i;
                                glm::vec3 snapped(0.0f);
                                snapped[best] = local[best] < 0.0f ? -1.0f : 1.0f;
                                local = snapped;
                            }
                            else
                                local = glm::round(local * 1000.0f) / 1000.0f;
                            if (glm::length(local) > 1e-4f)
                                setVec(*axis, local);
                        }
                        break;
                    }
                    case LowerHandle:
                    case UpperHandle:
                    {
                        float& field = m_Joint3DDrag == LowerHandle ? *lower : *upper;
                        float value = field;
                        bool ok = false;
                        if (angles)
                        {
                            float a;
                            if ((ok = arcAngle(arcCenter, dir, arcA, arcB, a)))
                            {
                                // Continuous near +-180: the side the field is on wins.
                                value = snap(field + std::remainder(a - field, 360.0f), 5.0f);
                                value = m_Joint3DDrag == LowerHandle ? std::clamp(value, -180.0f, 0.0f) : std::clamp(value, 0.0f, 180.0f);
                            }
                        }
                        else if (type == Joint3DType::Slider)
                        {
                            float t;
                            if ((ok = rayLine(anchor, dir, t)))
                            {
                                value = snap(t, 0.1f);
                                value = m_Joint3DDrag == LowerHandle ? std::min(value, 0.0f) : std::max(value, 0.0f);
                            }
                        }
                        else if (type == Joint3DType::Distance)
                        {
                            float t;
                            if ((ok = rayLine(connectedPoint, rod, t)))
                            {
                                value = std::max(0.0f, snap(t, 0.1f));
                                value = m_Joint3DDrag == LowerHandle ? std::min(value, *upper) : std::max(value, distance->MaxDistanceOnly ? 0.0f : *lower);
                            }
                        }
                        if (ok)
                            set(field, value);
                        break;
                    }
                    case SwingHandle:
                    {
                        glm::vec3 hit;
                        if (rayPlane(anchor, glm::cross(dir, swingSide), hit) && glm::length(hit - anchor) > 1e-4f)
                        {
                            const glm::vec3 d = hit - anchor;
                            const float a = glm::degrees(std::atan2(std::abs(glm::dot(d, swingSide)), glm::dot(d, dir)));
                            set(*swing, std::clamp(snap(a, 5.0f), 0.0f, 180.0f));
                        }
                        break;
                    }
                }
        }
        else if (hover >= 0 && m_ViewportHovered && m_JointHoverOverride.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt &&
                 !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            m_Joint3DDrag = hover;
            m_Joint3DPicked = hover == AnchorHandle || hover == ConnectedHandle ? hover : m_Joint3DPicked;
            m_Joint3DDragPlane = handles[hover];
            glm::vec3 hit;
            m_Joint3DDragOffset = rayPlane(handles[hover], viewDir, hit) ? handles[hover] - hit : glm::vec3(0.0f);
        }
        else if (hover < 0 && !gizmoOver && m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
            m_Joint3DPicked = -1; // a click beside the handles lets the picked point go
        if (changed)
        {
            MarkDirty();
            return; // drawn where it is now next frame
        }

        // Drawing: the lines between the handles, the handles, a label for the hovered / dragged one, a hint.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y), ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);
        const ImU32 shadow = IM_COL32(10, 12, 16, 170), cyan = IM_COL32(90, 220, 255, 235), purple = IM_COL32(190, 140, 255, 240);
        const ImU32 orange = IM_COL32(255, 170, 70, 240), yellow = IM_COL32(255, 215, 80, 240);
        auto line = [&](const ImVec2& from, const ImVec2& to, ImU32 color) {
            dl->AddLine(from, to, shadow, 4.0f);
            dl->AddLine(from, to, color, 2.0f);
        };
        if (present[ConnectedHandle])
            line(screen[AnchorHandle], screen[ConnectedHandle], purple);
        if (present[AxisHandle])
            line(screen[AnchorHandle], screen[AxisHandle], orange);
        if (present[LowerHandle] && type == Joint3DType::Slider)
        {
            // The slide range beside the axis, with stems to where the limits are on it.
            line(screen[LowerHandle], screen[UpperHandle], yellow);
            for (int i : { LowerHandle, UpperHandle })
            {
                bool shown = true;
                dl->AddLine(screen[i], toScreen(anchor + dir * (i == LowerHandle ? *lower : *upper), shown), yellow, 1.5f);
            }
        }
        if (present[LowerHandle] && type == Joint3DType::Distance)
        {
            bool shown = true;
            dl->AddLine(screen[LowerHandle], toScreen(connectedPoint + rod * *lower, shown), yellow, 1.5f);
        }
        if (present[SwingHandle])
            line(screen[AnchorHandle], screen[SwingHandle], yellow);
        for (int i = 0; i < HandleCount; i++)
        {
            if (!present[i])
                continue;
            const bool hovered = i == hover, dragged = i == m_Joint3DDrag, picked = i == m_Joint3DPicked;
            const float size = hovered || dragged ? 7.5f : 6.0f;
            const ImVec2 c = screen[i];
            const ImU32 fill = dragged ? IM_COL32(255, 160, 60, 255) : hovered ? IM_COL32(255, 235, 140, 255)
                             : picked ? IM_COL32(255, 200, 110, 255) : IM_COL32(240, 248, 255, 255);
            switch (i)
            {
                case AnchorHandle:
                    dl->AddCircleFilled(c, size + 1.5f, shadow);
                    dl->AddCircleFilled(c, size, fill);
                    dl->AddCircle(c, size, cyan, 0, 2.0f);
                    break;
                case ConnectedHandle:
                {
                    const ImVec2 v[4] = { ImVec2(c.x + size + 1, c.y), ImVec2(c.x, c.y - size - 1), ImVec2(c.x - size - 1, c.y), ImVec2(c.x, c.y + size + 1) };
                    dl->AddConvexPolyFilled(v, 4, fill);
                    dl->AddPolyline(v, 4, purple, ImDrawFlags_Closed, 2.0f);
                    break;
                }
                case AxisHandle:
                    dl->AddRectFilled(ImVec2(c.x - size + 1, c.y - size + 1), ImVec2(c.x + size - 1, c.y + size - 1), fill, 2.0f);
                    dl->AddRect(ImVec2(c.x - size + 1, c.y - size + 1), ImVec2(c.x + size - 1, c.y + size - 1), orange, 2.0f, 0, 2.0f);
                    break;
                case SwingHandle:
                {
                    const ImVec2 v[3] = { ImVec2(c.x, c.y - size - 1), ImVec2(c.x + size + 1, c.y + size), ImVec2(c.x - size - 1, c.y + size) };
                    dl->AddConvexPolyFilled(v, 3, fill);
                    dl->AddPolyline(v, 3, yellow, ImDrawFlags_Closed, 2.0f);
                    break;
                }
                default:
                    dl->AddCircleFilled(c, size - 0.5f, fill);
                    dl->AddCircle(c, size - 0.5f, yellow, 0, 2.0f);
                    dl->AddText(ImVec2(c.x - 3.5f, c.y - 7.0f), IM_COL32(30, 30, 30, 255), i == LowerHandle ? "-" : "+");
                    break;
            }
        }
        if (const int shown = m_Joint3DDrag >= 0 ? m_Joint3DDrag : hover >= 0 ? hover : m_Joint3DPicked; shown >= 0)
        {
            auto vec = [](const glm::vec3& v) { return std::format("({:.2f}, {:.2f}, {:.2f})", v.x, v.y, v.z); };
            std::string label;
            switch (shown)
            {
                case AnchorHandle: label = "Langar  " + vec(joint->Anchor); break;
                case ConnectedHandle: label = std::string(connected ? "Ulangan langar  " : "Dunyodagi nuqta  ") + vec(joint->ConnectedAnchor); break;
                case AxisHandle: label = "O'q  " + vec(*axis); break;
                case SwingHandle: label = std::format("Konus burchagi  {:.1f}\xc2\xb0", *swing); break;
                case LowerHandle:
                    label = angles ? std::format("{}  {:.1f}\xc2\xb0", type == Joint3DType::Cone ? "Buralish (pastki)" : "Pastki burchak", *lower)
                          : type == Joint3DType::Distance ? std::format("Minimal masofa  {:.2f}", *lower)
                                                          : std::format("Pastki chegara  {:.2f}", *lower);
                    break;
                default:
                    label = angles ? std::format("{}  {:.1f}\xc2\xb0", type == Joint3DType::Cone ? "Buralish (yuqori)" : "Yuqori burchak", *upper)
                          : type == Joint3DType::Distance ? std::format("Maksimal masofa  {:.2f}", *upper)
                                                          : std::format("Yuqori chegara  {:.2f}", *upper);
                    break;
            }
            const ImVec2 at(screen[shown].x + 12.0f, screen[shown].y - 24.0f);
            const ImVec2 size = ImGui::CalcTextSize(label.c_str());
            dl->AddRectFilled(ImVec2(at.x - 4.0f, at.y - 2.0f), ImVec2(at.x + size.x + 4.0f, at.y + size.y + 2.0f), IM_COL32(14, 16, 20, 215), 3.0f);
            dl->AddText(at, IM_COL32(235, 240, 245, 255), label.c_str());
        }
        // The hint: what the handles are, in as many lines as the viewport's width needs.
        std::vector<std::string> parts = { std::format(ICON_FA_ANCHOR "  {} Joint 3D:  doira - langar", typeName),
                                           autoConnected ? "ulangan langar avtomatik" : "romb - ulangan langar" };
        if (axis)
            parts.push_back("kvadrat - o'q");
        if (present[SwingHandle])
            parts.push_back("uchburchak - konus");
        if (present[LowerHandle] || present[UpperHandle])
            parts.push_back(type == Joint3DType::Distance ? "+/- - masofa" : type == Joint3DType::Cone ? "+/- - buralish" : "+/- - chegaralar");
        parts.push_back("bosilgan langarda - o'qlar bo'ylab gizmo");
        parts.push_back("Shift - mayda qadam");
        parts.push_back("Esc - tugatish");
        std::vector<std::string> lines(1);
        for (const std::string& part : parts)
        {
            std::string joined = lines.back().empty() ? part : lines.back() + "  |  " + part;
            if (!lines.back().empty() && ImGui::CalcTextSize(joined.c_str()).x > viewport.x - 60.0f)
                lines.push_back(part);
            else
                lines.back() = std::move(joined);
        }
        float hintY = m_ViewportBounds[0].y + 52.0f;
        float width = 0.0f;
        for (const std::string& l : lines)
            width = std::max(width, ImGui::CalcTextSize(l.c_str()).x);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float hintX = m_ViewportBounds[0].x + (viewport.x - width) * 0.5f;
        dl->AddRectFilled(ImVec2(hintX - 10.0f, hintY - 5.0f), ImVec2(hintX + width + 10.0f, hintY + lineHeight * lines.size() + 3.0f),
                          IM_COL32(16, 20, 26, 215), 6.0f);
        for (const std::string& l : lines)
        {
            dl->AddText(ImVec2(m_ViewportBounds[0].x + (viewport.x - ImGui::CalcTextSize(l.c_str()).x) * 0.5f, hintY), IM_COL32(150, 225, 255, 255), l.c_str());
            hintY += lineHeight;
        }
        dl->PopClipRect();
    }

    void EditorLayer::UI_ColliderPointEditor()
    {
        using Kind = SceneHierarchyPanel::ColliderPointKind;
        const Kind kind = m_SceneState == SceneState::Edit ? m_SceneHierarchyPanel.GetColliderPointEdit() : Kind::None;
        if (kind == Kind::None)
        {
            m_ColliderDrag = -1;
            m_ColliderHoverVertex = m_ColliderHoverEdge = -1;
            return;
        }
        Entity e = m_SceneHierarchyPanel.GetSelectedEntity();
        std::vector<glm::vec2>* points = nullptr;
        glm::vec2 offset(0.0f);
        bool loop = true;
        int minPoints = 3;
        if (kind == Kind::Polygon)
        {
            auto& c = e.GetComponent<PolygonCollider2DComponent>();
            points = &c.Points;
            offset = c.Offset;
        }
        else
        {
            auto& c = e.GetComponent<EdgeCollider2DComponent>();
            points = &c.Points;
            offset = c.Offset;
            loop = c.Loop;
            minPoints = c.Loop ? 3 : 2;
        }
        // The collider's frame: position, rotation about Z, scale (as the physics body gets it).
        glm::vec3 position, rotation, scale;
        Math::DecomposeTransform(m_ActiveScene->GetWorldTransform(e), position, rotation, scale);
        const glm::vec2 s = glm::max(glm::abs(glm::vec2(scale)), glm::vec2(1e-4f));
        const float cs = std::cos(rotation.z), sn = std::sin(rotation.z);
        auto toWorld = [&](const glm::vec2& q) {
            glm::vec2 l = offset + q * s;
            return glm::vec3(position.x + cs * l.x - sn * l.y, position.y + sn * l.x + cs * l.y, position.z);
        };
        auto toLocal = [&](const glm::vec3& w) {
            glm::vec2 d(w.x - position.x, w.y - position.y);
            glm::vec2 l(cs * d.x + sn * d.y, -sn * d.x + cs * d.y);
            return (l - offset) / s;
        };
        const glm::mat4 viewProjection = OverlayViewProjection();
        const glm::vec2 viewport = m_ViewportBounds[1] - m_ViewportBounds[0];
        auto toScreen = [&](const glm::vec2& q, bool& visible) {
            glm::vec4 clip = viewProjection * glm::vec4(toWorld(q), 1.0f);
            visible = clip.w > 0.0f;
            glm::vec2 ndc = visible ? glm::vec2(clip) / clip.w : glm::vec2(0.0f);
            return ImVec2(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * viewport.x, m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * viewport.y);
        };
        const size_t n = points->size();
        std::vector<ImVec2> screen(n);
        bool allVisible = true;
        for (size_t i = 0; i < n; i++)
        {
            bool visible = true;
            screen[i] = toScreen((*points)[i], visible);
            allVisible &= visible;
        }
        if (!allVisible)
            return;
        const size_t edges = n < 2 ? 0 : (loop && n >= 3 ? n : n - 1);

        // What is under the mouse: a point (within 9 px), else an edge (within 7 px) and where on it.
        const ImVec2 mouse = ImGui::GetMousePos();
        int hoverVertex = -1, hoverEdge = -1;
        glm::vec2 edgePoint(0.0f);
        ImVec2 edgeScreen(0.0f, 0.0f);
        auto edgeAt = [&](size_t i, float t) {
            const glm::vec2& a = (*points)[i];
            const glm::vec2& b = (*points)[(i + 1) % n];
            edgePoint = a + (b - a) * t;
            bool visible = true;
            edgeScreen = toScreen(edgePoint, visible);
        };
        if (!m_ColliderHoverOverride.empty())
        {
            const int index = std::atoi(m_ColliderHoverOverride.c_str() + 1);
            if (m_ColliderHoverOverride[0] == 'v' && index >= 0 && index < (int)n)
                hoverVertex = index;
            else if (m_ColliderHoverOverride[0] == 'e' && index >= 0 && index < (int)edges)
            {
                hoverEdge = index;
                edgeAt((size_t)index, 0.5f);
            }
        }
        else if (m_ViewportHovered && m_ColliderDrag < 0)
        {
            float best = 9.0f;
            for (size_t i = 0; i < n; i++)
            {
                float d = std::hypot(screen[i].x - mouse.x, screen[i].y - mouse.y);
                if (d < best)
                {
                    best = d;
                    hoverVertex = (int)i;
                }
            }
            if (hoverVertex < 0)
            {
                best = 7.0f;
                for (size_t i = 0; i < edges; i++)
                {
                    const ImVec2 a = screen[i], b = screen[(i + 1) % n];
                    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
                    const float t = l2 > 0.0f ? std::clamp(((mouse.x - a.x) * dx + (mouse.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
                    const float d = std::hypot(a.x + dx * t - mouse.x, a.y + dy * t - mouse.y);
                    if (d < best && t > 0.02f && t < 0.98f)
                    {
                        best = d;
                        hoverEdge = (int)i;
                        edgeAt(i, t);
                    }
                }
            }
        }
        m_ColliderHoverVertex = hoverVertex;
        m_ColliderHoverEdge = hoverEdge;

        // Editing: drag a point; a click on an edge adds one there (and drags it); Ctrl + click removes.
        const ImGuiIO& io = ImGui::GetIO();
        const bool ctrl = io.KeyCtrl;
        bool changed = false;
        if (m_ColliderDrag >= 0)
        {
            if (m_ColliderDrag >= (int)n || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_ColliderDrag = -1;
            else
            {
                glm::vec3 hit;
                if (ViewportMouseToPlane(position.z, hit))
                {
                    glm::vec2 q = toLocal(hit) + m_ColliderDragOffset;
                    if (io.KeyShift)
                        q = glm::round(q * 10.0f) / 10.0f; // Shift: steps of 0.1
                    if (q != (*points)[(size_t)m_ColliderDrag])
                    {
                        (*points)[(size_t)m_ColliderDrag] = q;
                        changed = true;
                    }
                }
            }
        }
        else if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            glm::vec3 hit;
            const bool onPlane = ViewportMouseToPlane(position.z, hit);
            if (hoverVertex >= 0)
            {
                if (ctrl)
                {
                    if ((int)n > minPoints)
                    {
                        points->erase(points->begin() + hoverVertex);
                        changed = true;
                    }
                    else
                        ShowViewportMessage(std::format(ICON_FA_TRIANGLE_EXCLAMATION "  Kamida {} ta nuqta kerak", minPoints));
                }
                else
                {
                    m_ColliderDrag = hoverVertex;
                    m_ColliderDragOffset = onPlane ? (*points)[(size_t)hoverVertex] - toLocal(hit) : glm::vec2(0.0f);
                }
            }
            else if (hoverEdge >= 0 && !ctrl)
            {
                points->insert(points->begin() + hoverEdge + 1, edgePoint);
                m_ColliderDrag = hoverEdge + 1;
                m_ColliderDragOffset = onPlane ? edgePoint - toLocal(hit) : glm::vec2(0.0f);
                changed = true;
            }
        }
        if (m_ColliderDrag < 0 && hoverVertex >= 0 && m_ViewportHovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && (int)points->size() > minPoints)
        {
            points->erase(points->begin() + hoverVertex);
            changed = true;
        }
        if (changed)
        {
            MarkDirty();
            return; // drawn with the new points next frame
        }

        // Drawing: the outline, the points, the ghost point on the hovered edge, a hint.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y), ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);
        const ImU32 lineColor = IM_COL32(90, 220, 255, 230), shadow = IM_COL32(10, 12, 16, 170);
        for (size_t i = 0; i < edges; i++)
        {
            const ImVec2 a = screen[i], b = screen[(i + 1) % n];
            const bool hovered = (int)i == hoverEdge;
            dl->AddLine(a, b, shadow, 4.0f);
            dl->AddLine(a, b, hovered ? IM_COL32(255, 230, 120, 255) : lineColor, hovered ? 2.5f : 2.0f);
        }
        if (hoverEdge >= 0)
        {
            dl->AddCircleFilled(edgeScreen, 6.0f, IM_COL32(255, 230, 120, 120));
            dl->AddCircle(edgeScreen, 6.0f, IM_COL32(255, 230, 120, 255), 0, 1.5f);
            dl->AddLine(ImVec2(edgeScreen.x - 3.0f, edgeScreen.y), ImVec2(edgeScreen.x + 3.5f, edgeScreen.y), IM_COL32(30, 30, 30, 255), 1.5f);
            dl->AddLine(ImVec2(edgeScreen.x, edgeScreen.y - 3.0f), ImVec2(edgeScreen.x, edgeScreen.y + 3.5f), IM_COL32(30, 30, 30, 255), 1.5f);
        }
        for (size_t i = 0; i < n; i++)
        {
            const bool hovered = (int)i == hoverVertex, dragged = (int)i == m_ColliderDrag;
            const bool removing = hovered && ctrl;
            const float r = hovered || dragged ? 6.5f : 5.0f;
            const ImU32 fill = removing ? IM_COL32(240, 80, 70, 255) : dragged ? IM_COL32(255, 160, 60, 255)
                             : hovered ? IM_COL32(255, 230, 120, 255) : IM_COL32(240, 248, 255, 255);
            dl->AddCircleFilled(screen[i], r + 1.5f, shadow);
            dl->AddCircleFilled(screen[i], r, fill);
            dl->AddCircle(screen[i], r, lineColor, 0, 1.5f);
            if (hovered || dragged)
            {
                const glm::vec2& q = (*points)[i];
                std::string label = std::format("{}  ({:.2f}, {:.2f})", i, q.x, q.y);
                const ImVec2 at(screen[i].x + 10.0f, screen[i].y - 22.0f);
                const ImVec2 size = ImGui::CalcTextSize(label.c_str());
                dl->AddRectFilled(ImVec2(at.x - 4.0f, at.y - 2.0f), ImVec2(at.x + size.x + 4.0f, at.y + size.y + 2.0f), IM_COL32(14, 16, 20, 210), 3.0f);
                dl->AddText(at, IM_COL32(235, 240, 245, 255), label.c_str());
            }
        }
        const char* hint = kind == Kind::Polygon ? ICON_FA_DRAW_POLYGON "  Polygon collider nuqtalari:  sudrang  |  chiziqqa bosing - yangi nuqta  |  "
                                                   "Ctrl + bosish / Delete - o'chirish  |  Shift - 0.1 qadam  |  Esc - tugatish"
                                                 : ICON_FA_CHART_LINE "  Edge collider nuqtalari:  sudrang  |  chiziqqa bosing - yangi nuqta  |  "
                                                   "Ctrl + bosish / Delete - o'chirish  |  Shift - 0.1 qadam  |  Esc - tugatish";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        const ImVec2 hintAt(m_ViewportBounds[0].x + (viewport.x - hintSize.x) * 0.5f, m_ViewportBounds[0].y + 44.0f);
        dl->AddRectFilled(ImVec2(hintAt.x - 10.0f, hintAt.y - 5.0f), ImVec2(hintAt.x + hintSize.x + 10.0f, hintAt.y + hintSize.y + 5.0f),
                          IM_COL32(16, 20, 26, 215), 6.0f);
        dl->AddText(hintAt, IM_COL32(150, 225, 255, 255), hint);
        dl->PopClipRect();
    }

    // Colours of a frame time against the overlay's budget (ViewportStatsOverlaySettings::Rate).
    static ImVec4 FrameRateColor(int rate)
    {
        switch (rate)
        {
            case 0: return ImVec4(0.42f, 0.85f, 0.52f, 1.0f);
            case 1: return ImVec4(0.95f, 0.78f, 0.3f, 1.0f);
            default: return ImVec4(0.96f, 0.4f, 0.36f, 1.0f);
        }
    }

    // How full the voice limit is: green, yellow from 75%, red at the limit.
    static ImVec4 VoiceLimitColor(size_t voices, int maxVoices)
    {
        if (maxVoices <= 0)
            return FrameRateColor(0);
        const float share = (float)voices / (float)maxVoices;
        return FrameRateColor(share >= 1.0f ? 2 : share >= 0.75f ? 1 : 0);
    }

    // A thin bar `share` (0..1) filled, as tall as a text line takes.
    static void DrawShareBar(float width, float height, float share, const ImVec4& color)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float y = a.y + std::floor((ImGui::GetTextLineHeight() - height) * 0.5f);
        dl->AddRectFilled(ImVec2(a.x, y), ImVec2(a.x + width, y + height), IM_COL32(255, 255, 255, 24), height * 0.5f);
        if (share > 0.0f)
            dl->AddRectFilled(ImVec2(a.x, y), ImVec2(a.x + std::max(height, width * std::clamp(share, 0.0f, 1.0f)), y + height),
                              ImGui::GetColorU32(color), height * 0.5f);
        ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    }

    // The counted voices of the last frames (newest on the right), with the limit as a dashed line
    // when the graph's scale reaches it.
    static void DrawVoiceGraph(const std::vector<float>& history, int maxVoices, float width, float height, float opacity)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + width, a.y + height);
        ImGui::InvisibleButton("##voiceGraph", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, (int)(110 * opacity)), 4.0f);
        float top = 4.0f;
        for (float v : history)
            top = std::max(top, v);
        float scale = std::ceil(top * 1.25f);
        // The limit is drawn when the voices come anywhere near it.
        const bool showLimit = maxVoices > 0 && (float)maxVoices <= scale * 1.6f;
        if (showLimit)
            scale = std::max(scale, (float)maxVoices * 1.08f);
        const float barWidth = width / (float)AudioEngine::VoiceHistoryCapacity;
        auto yOf = [&](float v) { return b.y - std::min(v / scale, 1.0f) * (height - 2.0f); };
        const size_t n = history.size();
        for (size_t i = 0; i < n; i++)
        {
            const float v = history[i];
            if (v <= 0.0f)
                continue;
            const float x1 = b.x - (float)(n - 1 - i) * barWidth, x0 = x1 - barWidth;
            ImVec4 color = VoiceLimitColor((size_t)v, maxVoices);
            color.w = 0.8f;
            dl->AddRectFilled(ImVec2(x0, yOf(v)), ImVec2(std::max(x1 - 0.15f, x0 + 0.6f), b.y), ImGui::GetColorU32(color));
        }
        if (showLimit)
        {
            const float y = std::round(yOf((float)maxVoices)) + 0.5f;
            for (float x = a.x; x < b.x; x += 6.0f)
                dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + 3.0f, b.x), y), IM_COL32(255, 120, 100, 150));
            const std::string label = std::format("limit {}", maxVoices);
            const ImVec2 at(a.x + 4.0f, std::max(a.y, y - ImGui::GetFontSize() - 1.0f));
            const ImVec2 size = ImGui::CalcTextSize(label.c_str());
            dl->AddRectFilled(ImVec2(at.x - 2.0f, at.y), ImVec2(at.x + size.x + 2.0f, at.y + size.y), IM_COL32(10, 12, 14, 150), 3.0f);
            dl->AddText(at, IM_COL32(255, 170, 150, 200), label.c_str());
        }
        if (hovered && n > 0)
        {
            const float mx = ImGui::GetMousePos().x;
            const int fromRight = (int)std::floor((b.x - mx) / barWidth);
            if (fromRight >= 0 && (size_t)fromRight < n)
            {
                dl->AddLine(ImVec2(mx, a.y), ImVec2(mx, b.y), IM_COL32(255, 255, 255, 110));
                ImGui::SetTooltip("%d kadr oldin: %.0f ovoz (limit %d)", fromRight, history[n - 1 - (size_t)fromRight], maxVoices);
            }
        }
    }

    void EditorLayer::UI_StatsOverlay()
    {
        const ViewportStatsOverlaySettings& o = m_ViewSettings.StatsOverlay;
        const glm::vec2 viewport = m_ViewportBounds[1] - m_ViewportBounds[0];
        if (!o.Visible || viewport.x < 260.0f || viewport.y < 160.0f)
            return;
        const Application& app = Application::Get();
        const FrameTimeHistory& history = app.GetFrameTimeHistory();
        const float fontSize = ImGui::GetFontSize();
        const float width = std::min(std::floor(fontSize * 18.5f), viewport.x - 24.0f);
        // Below the toolbar at the top, above the bottom edge (the last frame's height places it).
        static float s_Height = 220.0f;
        const float toolbar = ImGui::GetFrameHeight() + 4.0f + 26.0f;
        const bool left = o.Corner == OverlayCorner::TopLeft || o.Corner == OverlayCorner::BottomLeft;
        const bool top = o.Corner == OverlayCorner::TopLeft || o.Corner == OverlayCorner::TopRight;
        ImVec2 pos(left ? m_ViewportBounds[0].x + 12.0f : m_ViewportBounds[1].x - width - 12.0f,
                   top ? m_ViewportBounds[0].y + toolbar : m_ViewportBounds[1].y - s_Height - 14.0f);
        // Bottom left: above the view mode's label.
        if (!top && left && (m_DebugView != RenderDebugView::Lit || m_Wireframe != WireframeMode::Off || m_ShowTransparentOrder))
            pos.y -= ImGui::GetTextLineHeight() + 22.0f;
        pos.y = std::max(pos.y, m_ViewportBounds[0].y + toolbar);
        // The toolbar's second row (the camera panel of a narrow viewport) is on the right.
        if (top && !left)
            pos.y = std::max(pos.y, m_ViewportPanelBounds[0].y + m_ToolbarBottom + 12.0f);

        ImGui::SetCursorScreenPos(pos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.08f, 0.095f, o.Opacity));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f * o.Opacity));
        if (ImGui::BeginChild("##StatsOverlay", ImVec2(width, 0.0f),
                              ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float inner = ImGui::GetContentRegionAvail().x;
            // ---- frame rate: the half-second average (steady to read) and its frame time
            const float fps = app.GetFPS(), frameMs = app.GetFrameTime();
            const ImVec4 rateColor = FrameRateColor(o.Rate(frameMs));
            if (ImFont* title = ImGuiLayer::GetTitleFont())
                ImGui::PushFont(title);
            ImGui::TextColored(rateColor, "%.0f", fps);
            if (ImGuiLayer::GetTitleFont())
                ImGui::PopFont();
            ImGui::SameLine(0.0f, 5.0f);
            ImGui::BeginGroup();
            ImGui::TextDisabled("FPS");
            ImGui::Text("%.2f ms", frameMs);
            ImGui::EndGroup();
            if (!history.Empty())
            {
                const FrameTimeHistory::Summary sum = history.Summarize();
                ImGui::SameLine();
                ImGui::BeginGroup();
                const float column = ImGui::GetCursorPosX();
                auto right = [&](const std::string& text, const ImVec4& color) {
                    float w = ImGui::CalcTextSize(text.c_str()).x;
                    ImGui::SetCursorPosX(std::max(column, ImGui::GetWindowContentRegionMax().x - w));
                    ImGui::TextColored(color, "%s", text.c_str());
                };
                const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                right(std::format("maks {:.1f} ms", sum.Max), sum.Max > o.GetBudgetMilliseconds() * 2.0f ? FrameRateColor(2) : dim);
                right(std::format("1% past {:.0f} FPS", sum.Low1PercentFPS), FrameRateColor(o.Rate(sum.Worst1Percent)));
                ImGui::EndGroup();
            }

            // ---- frame time graph: one bar per frame (newest on the right), the budget lines
            if (o.FrameGraph)
            {
                const float graphHeight = std::floor(fontSize * 3.6f);
                const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + inner, a.y + graphHeight);
                ImGui::InvisibleButton("##frameGraph", ImVec2(inner, graphHeight));
                const bool hovered = ImGui::IsItemHovered();
                dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, (int)(110 * o.Opacity)), 4.0f);
                const float budget = o.GetBudgetMilliseconds();
                const size_t n = history.Size();
                // The scale: what most frames take (a few spikes - loading, a hitch - reach the
                // top, cut), at least room for twice the budget.
                float maxValue = budget * 2.4f;
                if (n > 0)
                {
                    std::vector<float> sorted(n);
                    for (size_t i = 0; i < n; i++)
                        sorted[i] = history.Get(i);
                    const size_t at = std::min(n - 1, (size_t)((float)n * 0.97f));
                    std::nth_element(sorted.begin(), sorted.begin() + (std::ptrdiff_t)at, sorted.end());
                    maxValue = std::clamp(sorted[at] * 1.25f, maxValue, 250.0f);
                }
                const float barWidth = inner / (float)FrameTimeHistory::Capacity;
                auto yOf = [&](float ms) { return b.y - std::min(ms / maxValue, 1.0f) * (graphHeight - 2.0f); };
                for (size_t i = 0; i < n; i++)
                {
                    const float ms = history.Get(i);
                    const float x1 = b.x - (float)(n - 1 - i) * barWidth, x0 = x1 - barWidth;
                    ImVec4 color = FrameRateColor(o.Rate(ms));
                    color.w = 0.85f;
                    dl->AddRectFilled(ImVec2(x0, yOf(ms)), ImVec2(std::max(x1 - 0.15f, x0 + 0.6f), b.y), ImGui::GetColorU32(color));
                }
                // The budget (target FPS) and twice it, labelled with their FPS where there is room.
                float lastLabel = b.y + fontSize * 2.0f;
                for (float line : { budget, budget * 2.0f })
                {
                    if (line >= maxValue)
                        continue;
                    const float y = std::round(yOf(line)) + 0.5f;
                    for (float x = a.x; x < b.x; x += 6.0f)
                        dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + 3.0f, b.x), y), IM_COL32(255, 255, 255, 90));
                    if (lastLabel - y < fontSize || y - fontSize - 1.0f < a.y)
                        continue;
                    std::string label = std::format("{:.0f} FPS", 1000.0f / line);
                    const ImVec2 textAt(a.x + 4.0f, y - fontSize - 1.0f);
                    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                    dl->AddRectFilled(ImVec2(textAt.x - 2.0f, textAt.y), ImVec2(textAt.x + textSize.x + 2.0f, textAt.y + textSize.y),
                                      IM_COL32(10, 12, 14, 150), 3.0f);
                    dl->AddText(textAt, IM_COL32(255, 255, 255, 150), label.c_str());
                    lastLabel = y;
                }
                if (hovered && n > 0)
                {
                    const float mx = ImGui::GetMousePos().x;
                    const int fromRight = (int)std::floor((b.x - mx) / barWidth);
                    if (fromRight >= 0 && (size_t)fromRight < n)
                    {
                        const size_t i = n - 1 - (size_t)fromRight;
                        const float ms = history.Get(i);
                        dl->AddLine(ImVec2(mx, a.y), ImVec2(mx, b.y), IM_COL32(255, 255, 255, 110));
                        ImGui::SetTooltip("%d kadr oldin: %.2f ms (%.0f FPS)", fromRight, ms, ms > 0.0f ? 1000.0f / ms : 0.0f);
                    }
                }
                if (!history.Empty())
                {
                    const FrameTimeHistory::Summary sum = history.Summarize();
                    ImGui::TextDisabled("o'rtacha %.1f ms   eng tez %.1f ms", sum.Average, sum.Min);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(std::format("So'nggi {} kadr: o'rtacha {:.2f} ms ({:.0f} FPS), eng tezi {:.2f} ms, eng sekini {:.2f} ms; "
                                                "eng sekin 1% kadrlar o'rtacha {:.2f} ms", history.Size(), sum.Average, sum.AverageFPS,
                                                sum.Min, sum.Max, sum.Worst1Percent).c_str());
                }
            }

            // ---- what the scene drew, what culling skipped
            const RenderStatistics& scene = m_SceneStats;
            const RenderStatistics& total = Renderer::GetStats();
            auto row = [&](const char* label, const std::string& value, const char* extra = nullptr, const char* tip = nullptr) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", label);
                if (tip && ImGui::IsItemHovered())
                    UI::Tooltip(tip);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(value.c_str());
                if (extra && *extra)
                {
                    ImGui::SameLine(0.0f, 5.0f);
                    ImGui::TextDisabled("%s", extra);
                }
            };
            auto table = [&](const char* id) {
                return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings);
            };
            if (o.DrawCalls)
            {
                ImGui::Separator();
                if (table("##overlayDraw"))
                {
                    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.25f);
                    const uint32_t editorCalls = total.DrawCalls > scene.DrawCalls ? total.DrawCalls - scene.DrawCalls : 0;
                    const std::string editorText = editorCalls ? std::format("+{} editor", editorCalls) : std::string();
                    row("Draw call", std::to_string(scene.DrawCalls), editorText.c_str(),
                        "Sahna yuborgan chizish buyruqlari (instanced partiya - bitta). +editor: kontur, gizmolar, debug chizish");
                    // A 2D scene: no 3D rows (they would only say 0).
                    const bool only2D = scene.MeshCount == 0 && scene.TriangleCount == 0 && scene.CulledMeshCount == 0 &&
                                        (scene.SpriteCount || scene.CulledSpriteCount || scene.TileChunkCount || scene.QuadCount);
                    if (!only2D)
                    {
                        row("Uchburchak", RenderStatistics::FormatCount(scene.TriangleCount), nullptr, "Chizilgan 3D uchburchaklar (soyalarsiz)");
                        const std::string instanced = scene.InstancedMeshCount ? std::format("{} instanced", scene.InstancedMeshCount) : std::string();
                        row("Meshlar", RenderStatistics::FormatCount(scene.MeshCount), instanced.c_str());
                    }
                    if (scene.ShadowCasterCount)
                        row("Soyalar", RenderStatistics::FormatCount(scene.ShadowCasterCount), nullptr,
                            "Soya kaskadlariga chizilgan meshlar (kaskadlar bo'yicha yig'indi)");
                    if (scene.SpriteCount || scene.TileChunkCount || scene.QuadCount)
                        row("2D obyektlar", RenderStatistics::FormatCount(scene.SpriteCount),
                            scene.TileChunkCount ? std::format("+{} tile bo'lak", scene.TileChunkCount).c_str() : nullptr,
                            "Sprite, doira, matn va zarrachalar tizimlari");
                    if (scene.ParticleCount)
                    {
                        const std::string instanced = scene.InstancedParticleCount
                                                    ? std::format("{} instanced / {} chaqiruv", RenderStatistics::FormatCount(scene.InstancedParticleCount),
                                                                  scene.InstancedParticleDrawCount)
                                                    : std::string();
                        row("Particle'lar", RenderStatistics::FormatCount(scene.ParticleCount), instanced.c_str(),
                            "Chizilgan particle'lar. 3D billboardlar GPU instancing bilan: har tizim bitta chizish buyrug'ida");
                    }
                    ImGui::EndTable();
                }
            }
            if (o.Culling)
            {
                ImGui::Separator();
                ImGui::TextDisabled(ICON_FA_SCISSORS "  Culling (kesildi)");
                if (table("##overlayCulling"))
                {
                    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.25f);
                    auto culled = [&](const char* label, uint32_t drawn, uint32_t skipped) {
                        const uint32_t all = drawn + skipped;
                        if (all == 0)
                            return; // nothing of that kind in view or out of it
                        std::string share = all ? std::format("{:.0f}%", 100.0 * skipped / all) : std::string("-");
                        std::string of = std::format("/ {}", RenderStatistics::FormatCount(all));
                        row(label, RenderStatistics::FormatCount(skipped), all ? (of + "  " + share).c_str() : nullptr);
                    };
                    culled("Meshlar", scene.MeshCount, scene.CulledMeshCount);
                    culled("Soyalar", scene.ShadowCasterCount, scene.CulledShadowCasterCount);
                    culled("2D obyektlar", scene.SpriteCount, scene.CulledSpriteCount);
                    culled("Tile bo'laklar", scene.TileChunkCount, scene.CulledTileChunkCount);
                    if (scene.MeshCount + scene.CulledMeshCount + scene.ShadowCasterCount + scene.CulledShadowCasterCount + scene.SpriteCount +
                        scene.CulledSpriteCount + scene.TileChunkCount + scene.CulledTileChunkCount == 0)
                        row("-", "chizilgan narsa yo'q");
                    ImGui::EndTable();
                }
                if (!Renderer3D::IsFrustumCullingEnabled())
                    ImGui::TextColored(FrameRateColor(1), ICON_FA_TRIANGLE_EXCLAMATION "  Frustum culling o'chirilgan");
            }

            // ---- where the CPU time goes: the scopes that took the most time themselves
            if (o.Hotspots)
            {
                ImGui::Separator();
                ImGui::TextDisabled(ICON_FA_STOPWATCH "  Eng sekin bo'limlar (CPU)");
                const auto& entries = Profiler::GetLastFrame();
                if (table("##overlayHotspots"))
                {
                    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.6f);
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                    for (int index : Profiler::GetHotspots(entries, 4, true))
                    {
                        const Profiler::Entry& e = entries[(size_t)index];
                        float self = e.AverageMilliseconds;
                        for (int child : Profiler::GetChildren(entries, index))
                            self -= entries[(size_t)child].AverageMilliseconds;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(e.Name);
                        if (ImGui::IsItemHovered())
                            UI::Tooltip(e.Parent >= 0 ? std::format("{}\n{} ichida", e.Name, entries[(size_t)e.Parent].Name).c_str() : e.Name);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.2f ms", std::max(0.0f, self));
                    }
                    // The newest scene load (what a switch cost): its time and slowest stage.
                    if (SceneLoadTiming load; SceneLoadProfiler::GetLast(load))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled(ICON_FA_HOURGLASS_HALF " %s", load.SceneName.empty() ? load.Path.c_str() : load.SceneName.c_str());
                        if (ImGui::IsItemHovered())
                            UI::Tooltip(std::format("Oxirgi sahna yuklash:\n{}", load.Format()).c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextDisabled("%.1f ms", load.MainThreadMilliseconds());
                    }
                    ImGui::EndTable();
                }
            }
            // ---- audio: the voices against the limit, by mixer group, what the limit cost
            if (o.Audio)
            {
                ImGui::Separator();
                const AudioProfile audio = AudioEngine::GetProfile();
                ImGui::TextDisabled(ICON_FA_VOLUME_HIGH "  Audio");
                if (!audio.Available)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(FrameRateColor(1), "ovoz tizimi yo'q");
                }
                else
                {
                    const ImVec4 color = VoiceLimitColor(audio.Voices, audio.MaxVoices);
                    const std::string count = std::format("{} / {} ovoz", audio.Voices, audio.MaxVoices);
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(count.c_str()).x));
                    ImGui::TextColored(color, "%s", count.c_str());
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(std::format("Limitga kiradigan ovozlar (ijro etilayotgan va pauzadagilar) / loyiha limiti (Audio Mixer > "
                                                "Sozlamalar). Musiqa va editordagi ko'rib chiqish hisobga kirmaydi.\nCho'qqi: {}   jami "
                                                "ovozlar: {} (ijroda {})", audio.PeakVoices, audio.Sounds, audio.Playing).c_str());
                    DrawShareBar(inner, 4.0f, audio.MaxVoices > 0 ? (float)audio.Voices / (float)audio.MaxVoices : 0.0f, color);
                    if (table("##overlayAudio"))
                    {
                        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.25f);
                        // The groups something plays in (or lost a sound to the limit), as a tree.
                        bool any = false;
                        for (const AudioGroupProfile& g : audio.Groups)
                        {
                            if (g.TotalVoices == 0 && g.Stolen == 0 && g.Rejected == 0)
                                continue;
                            any = true;
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (float)g.Depth * fontSize * 0.75f);
                            ImGui::TextDisabled("%s", g.Name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%zu", g.Voices);
                            if (g.TotalVoices != g.Voices)
                            {
                                ImGui::SameLine(0.0f, 5.0f);
                                ImGui::TextDisabled("(jami %zu)", g.TotalVoices);
                            }
                            if (g.Stolen || g.Rejected)
                            {
                                ImGui::SameLine(0.0f, 5.0f);
                                ImGui::TextColored(FrameRateColor(1), "-%llu", (unsigned long long)(g.Stolen + g.Rejected));
                                if (ImGui::IsItemHovered())
                                    UI::Tooltip(std::format("{}: limit sababli to'xtatilgan {}, ijro etilmagan {}", g.Name, g.Stolen,
                                                            g.Rejected).c_str());
                            }
                        }
                        if (!any)
                            row("-", "hech narsa o'ynamayapti");
                        if (audio.Stolen || audio.Rejected)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextDisabled("Limit");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(FrameRateColor(1), "%llu to'xt. / %llu rad", (unsigned long long)audio.Stolen,
                                               (unsigned long long)audio.Rejected);
                            if (ImGui::IsItemHovered())
                                UI::Tooltip("Ovozlar limiti to'lgani uchun to'xtatilgan (boshqasiga joy bergan) / umuman ijro etilmagan "
                                            "ovozlar - Play boshlanganidan beri");
                        }
                        ImGui::EndTable();
                    }
                }
            }
            // Right click: the overlay's settings.
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##StatsOverlayContext");
            if (ImGui::BeginPopup("##StatsOverlayContext"))
            {
                if (UI_StatsOverlaySettings())
                    MarkViewSettingsDirty();
                ImGui::EndPopup();
            }
            s_Height = ImGui::GetWindowHeight();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    bool EditorLayer::UI_StatsOverlaySettings()
    {
        auto& o = m_ViewSettings.StatsOverlay;
        bool changed = false;
        ImGui::TextDisabled(ICON_FA_GAUGE_HIGH "  Statistika overlay (faqat editorda)");
        changed |= ImGui::Checkbox(Tr("Ko'rsatish"), &o.Visible);
        ImGui::BeginDisabled(!o.Visible);
        changed |= ImGui::Checkbox(Tr(ICON_FA_CHART_LINE "  FPS grafigi"), &o.FrameGraph);
        if (ImGui::IsItemHovered())
            UI::Tooltip("So'nggi 240 kadrning har biri qancha vaqt olgani (o'ngda eng yangisi), maqsad FPS chiziqlari bilan");
        changed |= ImGui::Checkbox(Tr(ICON_FA_CUBES "  Draw call va uchburchaklar"), &o.DrawCalls);
        changed |= ImGui::Checkbox(Tr(ICON_FA_SCISSORS "  Culling (kesilganlar)"), &o.Culling);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Frustum culling kamera ko'rmagani uchun chizmagan meshlar, soya tashlovchilar, 2D obyektlar va tile bo'laklari");
        changed |= ImGui::Checkbox(Tr(ICON_FA_STOPWATCH "  Eng sekin bo'limlar"), &o.Hotspots);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Profiler bo'limlaridan o'zi (ichki bo'limlarsiz) eng ko'p vaqt olgan 4 tasi, o'rtacha");
        changed |= ImGui::Checkbox(Tr(ICON_FA_VOLUME_HIGH "  Audio (faol ovozlar)"), &o.Audio);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Ijro etilayotgan ovozlar soni / limit, mixer guruhlari bo'yicha, limit sababli to'xtatilgan va ijro "
                        "etilmagan ovozlar (Play boshlanganidan beri)");
        static const char* corners[] = { "Yuqori chap", "Yuqori o'ng", "Pastki chap", "Pastki o'ng" };
        int corner = (int)o.Corner;
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Burchak", &corner, corners, IM_ARRAYSIZE(corners)))
        {
            o.Corner = (OverlayCorner)corner;
            changed = true;
        }
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::SliderFloat("Fon", &o.Opacity, ViewportStatsOverlaySettings::MinOpacity, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(150);
        changed |= ImGui::DragFloat("Maqsad FPS", &o.TargetFPS, 1.0f, ViewportStatsOverlaySettings::MinTargetFPS,
                                    ViewportStatsOverlaySettings::MaxTargetFPS, "%.0f");
        if (ImGui::IsItemHovered())
            UI::Tooltip("Grafikdagi chiziq va ranglar: shu FPS uchun kadr vaqtiga sig'gan - yashil, ikki baravarigacha - sariq, "
                        "undan sekin - qizil");
        changed |= PresetButtons("targetFps", o.TargetFPS, { 30.0f, 60.0f, 120.0f, 144.0f });
        ImGui::EndDisabled();
        if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT "  Standart")))
        {
            const bool visible = o.Visible;
            o = ViewportStatsOverlaySettings();
            o.Visible = visible;
            changed = true;
        }
        if (changed)
            o.Sanitize();
        return changed;
    }

    void EditorLayer::UI_StatsOverlayPopup()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(420, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginPopup("##StatsOverlay"))
        {
            if (UI_StatsOverlaySettings())
                MarkViewSettingsDirty();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    void EditorLayer::UI_SceneSettings()
    {
        if (!m_ShowSceneSettings)
            return;
        if (m_FocusSceneSettings)
        {
            ImGui::SetNextWindowFocus();
            m_FocusSceneSettings = false;
        }
        if (!ImGui::Begin(Tr(ICON_FA_MOUNTAIN_SUN "  Sahna###SceneSettings"), &m_ShowSceneSettings))
        {
            ImGui::End();
            return;
        }
        auto& s = m_ActiveScene->GetSettings();
        bool changed = false;

        ImGui::SeparatorText(Tr(ICON_FA_CLOUD "  Muhit"));
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
            changed |= UI::PropertyBool("Tuman", s.FogEnabled);
            if (s.FogEnabled)
            {
                changed |= UI::PropertyColor3("Tuman rangi", s.FogColor);
                changed |= UI::PropertyFloat("Tuman zichligi", s.FogDensity, 0.0005f, 0.0f, 1.0f, "%.4f");
                changed |= UI::PropertyFloat("Balandlik so'nishi", s.FogHeightFalloff, 0.005f, 0.0f, 5.0f);
            }
            UI::EndPropertyGrid();
        }

        UI_SceneMetadata(changed);

        ImGui::SeparatorText(Tr(ICON_FA_SUN "  Osmon gradienti va quyosh"));
        if (m_SceneSettingsScrollTo == "sky")
        {
            ImGui::SetScrollHereY(0.0f);
            m_SceneSettingsScrollTo.clear();
        }
        if (UI::BeginPropertyGrid("sky"))
        {
            SkySettings& sky = s.Sky;
            if (!s.EnvironmentMap.empty())
            {
                UI::PropertyLabel("");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled(ICON_FA_CIRCLE_INFO "  HDRI tanlangan: osmon HDRI'dan chiziladi, gradient va quyosh diski "
                                    "ko'rinmaydi");
                ImGui::PopTextWrapPos();
            }
            UI::PropertyLabel("Zenit egriligi", "Ufqdan tepaga (zenit) rang qanday o'tadi: kichik qiymat - tepa rangi pastgacha "
                                                "tushadi (keng ko'k osmon), katta - ufq rangi balandroq ko'tariladi");
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::SliderFloat("##skycurve", &sky.Curve, 0.05f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            UI::PropertyLabel("Yerga o'tish", "Ufqdan pastda yer rangi qanchalik tez boshlanadi: katta qiymat - ingichka ufq "
                                              "chizig'i, kichik - keng, yumshoq o'tish");
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::SliderFloat("##groundfade", &sky.GroundFade, 0.25f, 20.0f, "%.2f",
                                          ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            UI::PropertyLabel("Quyosh diski", "Osmonda asosiy Directional Light (soya beradigan quyosh) yo'nalishida disk va "
                                              "uning nur gardishi chiziladi; rangi quyosh rangidan olinadi");
            changed |= ImGui::Checkbox("##sundisc", &sky.SunDisc);
            if (sky.SunDisc)
            {
                if (!m_ActiveScene->GetSunEntity())
                {
                    ImGui::PushTextWrapPos(0.0f); // wraps inside the value column
                    ImGui::TextDisabled("Sahnada Directional Light yo'q - disk chizilmaydi");
                    ImGui::PopTextWrapPos();
                }
                UI::PropertyLabel("Disk o'lchami", "Disk markazidan yorqinligi yarmiga tushadigan burchak (gradus). Haqiqiy quyosh "
                                                   "~0.27 gradus; o'yinlarda kattaroq disk chiroyliroq ko'rinadi");
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderFloat("##sunsize", &sky.SunSize, SkySettings::MinSunSize, SkySettings::MaxSunSize, "%.2f deg",
                                              ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
                UI::PropertyLabel("Disk yorqinligi", "Disk qanchalik yorqin (quyosh rangiga ko'paytiriladi; 1 dan katta qiymat "
                                                     "HDR - bloom yoqilgan bo'lsa porlaydi)");
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderFloat("##sunintensity", &sky.SunIntensity, 0.0f, 50.0f, "%.2f",
                                              ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
                UI::PropertyLabel("Nur gardishi", "Disk atrofidagi keng, xira yorug'lik (halo) kuchi");
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderFloat("##sunglow", &sky.SunGlow, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            }
            UI::PropertyLabel("");
            if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT "  Standart osmon")))
            {
                sky = SkySettings();
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Gradient egriliklari va quyosh diski standart qiymatlarga qaytadi (ranglar o'zgarmaydi)");
            UI::EndPropertyGrid();
        }

        // The quality level the editor shows limits this scene's shadows / effects / render scale.
        if (Quality::GetLevel() >= 0)
        {
            const std::vector<std::string> limits = Quality::GetActive().DescribeLimits(s.Shadows, s.Post, s.RenderScale, s.ShadowDistance);
            ImGui::Spacing();
            if (limits.empty())
                ImGui::TextDisabled(ICON_FA_GAUGE_HIGH "  Sifat darajasi: %s (sahna o'z sozlamalari bilan)", Quality::GetLevelName().c_str());
            else
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), ICON_FA_GAUGE_HIGH "  Sifat darajasi: %s - %zu ta cheklov",
                                   Quality::GetLevelName().c_str(), limits.size());
            if (ImGui::IsItemHovered())
            {
                std::string text = limits.empty() ? std::string("Bu daraja sahna so'raganlarini cheklamaydi") : std::string();
                for (const std::string& line : limits)
                    text += (text.empty() ? "" : "\n") + line;
                ImGui::SetTooltip("%s\n\nBosing: Loyiha sozlamalari > Sifat (darajalar, editorda ko'rish)", text.c_str());
            }
            if (ImGui::IsItemClicked())
            {
                m_ShowProjectSettings = true;
                m_ProjectSettingsPanel.Focus(ProjectSettingsPanel::Tab::Quality);
            }
        }

        ImGui::SeparatorText(Tr(ICON_FA_EXPAND "  Render"));
        if (m_SceneSettingsScrollTo == "render")
        {
            ImGui::SetScrollHereY(0.0f);
            m_SceneSettingsScrollTo.clear();
        }
        if (UI::BeginPropertyGrid("render"))
        {
            UI::PropertyLabel("Render masshtabi", "Sahna tasviri ko'rinish o'lchamining shuncha barobarida chiziladi, keyin "
                                                  "chiziqli filtr bilan ko'rinish o'lchamiga keltiriladi.\n"
                                                  "1 dan kichik: tezroq, lekin yumshoqroq; 1 dan katta: supersampling (silliqroq, "
                                                  "sekinroq). Editor viewport'i va o'yin (player) ikkalasida ishlaydi.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderFloat("##renderscale", &s.RenderScale, Renderer::MinRenderScale, Renderer::MaxRenderScale, "x%.2f"))
            {
                s.RenderScale = std::clamp(s.RenderScale, Renderer::MinRenderScale, Renderer::MaxRenderScale);
                changed = true;
            }
            UI::PropertyLabel("");
            changed |= PresetButtons("renderScalePresets", s.RenderScale, { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f });
            const auto& fb = m_Framebuffer->GetSpecification();
            const auto& view = m_FinalFramebuffer->GetSpecification();
            UI::PropertyLabel("Ichki o'lcham", "Viewport'ning sahna tasviri hozir chizilayotgan o'lcham (ko'rinish o'lchami qavsda)");
            ImGui::Text("%u x %u  (%u x %u)", fb.Width, fb.Height, view.Width, view.Height);
            UI::EndPropertyGrid();
        }

        ImGui::SeparatorText(Tr(ICON_FA_MOON "  Soyalar"));
        if (m_SceneSettingsScrollTo == "shadows")
        {
            ImGui::SetScrollHereY(0.0f);
            m_SceneSettingsScrollTo.clear();
        }
        if (UI::BeginPropertyGrid("shadows"))
        {
            ShadowSettings& sh = s.Shadows;
            UI::PropertyLabel("Soya masofasi", "Kameradan shu masofagacha (metr) quyosh soyalari chiziladi; kaskadlar shu oraliqqa "
                                               "taqsimlanadi. Kichikroq - yaqindagi soyalar aniqroq.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::DragFloat("##shadowdistance", &s.ShadowDistance, 0.5f, 5.0f, 500.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);

            static const char* atlasSizes[] = { "1024", "2048", "4096" };
            int atlas = sh.AtlasSize <= 1024 ? 0 : sh.AtlasSize <= 2048 ? 1 : 2;
            UI::PropertyLabel("Atlas o'lchami", "Soya xaritasi (atlas) tomoni, tekselda. Kaskadlar uning 2x2 bo'laklarida "
                                                "(bitta kaskad bo'lsa butun atlasda). Kattaroq - aniqroq soya, lekin ko'proq "
                                                "video xotira (4096: 64 MB, 2048: 16 MB, 1024: 4 MB).");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##shadowatlas", &atlas, atlasSizes, 3))
            {
                sh.AtlasSize = 1024u << atlas;
                changed = true;
            }

            static const char* filters[] = { "O'chiq (qattiq)", "PCF 3x3", "PCF 5x5", "Poisson (16 nuqta)" };
            int filter = (int)sh.Filter;
            UI::PropertyLabel("Filtr (PCF)", "Soya chetini yumshatish: O'chiq - bitta taqqoslash (qattiq, zinapoyali chet), "
                                             "PCF 3x3 / 5x5 - 9 / 25 ta qo'shni tekselni o'rtachalash, Poisson - disk bo'ylab "
                                             "16 nuqta (5x5 dan yumshoqroq, kamroq namuna).");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##shadowfilter", &filter, filters, ShadowFilterCount))
            {
                sh.Filter = (ShadowFilter)filter;
                changed = true;
            }

            UI::PropertyLabel("Kaskadlar", "Soya masofasi nechta bo'lakka bo'linadi: har biri o'z tekstel o'lchami bilan (yaqindagisi "
                                           "eng aniq). Ortografik kamera doim bittasini ishlatadi.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::SliderInt("##cascades", &sh.CascadeCount, 1, ShadowSettings::MaxCascades, "%d", ImGuiSliderFlags_AlwaysClamp);
            if (sh.CascadeCount > 1)
            {
                UI::PropertyLabel("Kaskad taqsimoti", "Kaskadlar chegarasi: 0 - teng oraliqlar, 1 - logarifmik (kameraga yaqin joy "
                                                      "aniqroq, uzoqdagisi xiraroq)");
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderFloat("##cascadesplit", &sh.CascadeSplit, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            }
            {
                // Where the cascades of the scene view end (the editor camera's near plane).
                float nearDepth = std::max(0.05f, m_EditorCamera.GetNearClip());
                float farDepth = std::min(m_EditorCamera.GetFarClip(), s.ShadowDistance);
                std::string ends;
                for (float split : Renderer3D::GetCascadeSplits(nearDepth, farDepth, sh.CascadeCount, sh.CascadeSplit))
                    ends += std::format("{}{:.1f}", ends.empty() ? "" : " / ", split);
                UI::PropertyLabel("Chegaralar", "Har bir kaskad kameradan qancha masofada tugaydi (metr; editor kamerasi uchun)");
                ImGui::TextDisabled("%s m", ends.c_str());
            }

            UI::PropertyLabel("Chuqurlik bias", "Taqqoslanadigan chuqurlikdan ayiriladi: sirtdagi 'soya donalari' (acne) yo'qoladi, "
                                                "lekin juda katta bo'lsa soya obyektdan ajralib qoladi (peter panning)");
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::SliderFloat("##depthbias", &sh.DepthBias, 0.0f, ShadowSettings::MaxDepthBias, "%.5f",
                                          ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            UI::PropertyLabel("Normal bias", "Soya tekshiriladigan nuqta sirt normali bo'ylab siljitiladi (kaskad teksel o'lchamiga "
                                             "nisbatan; 1 - standart). Qiya sirtlardagi donalarni yo'qotadi.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::SliderFloat("##normalbias", &sh.NormalBias, 0.0f, ShadowSettings::MaxNormalBias, "x%.2f",
                                          ImGuiSliderFlags_AlwaysClamp);
            UI::PropertyLabel("");
            if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT "  Standart sifat")))
            {
                sh = ShadowSettings();
                s.ShadowDistance = SceneSettings().ShadowDistance;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Atlas 4096, PCF 3x3, 3 kaskad, bias va soya masofasi standart qiymatlarga qaytadi");
            UI::EndPropertyGrid();
        }

        ImGui::SeparatorText(Tr(ICON_FA_WAND_MAGIC_SPARKLES "  Post-processing"));
        if (m_SceneSettingsScrollTo == "post")
        {
            ImGui::SetScrollHereY(0.0f);
            m_SceneSettingsScrollTo.clear();
        }
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
            if (m_SceneSettingsScrollTo == "bloom")
            {
                ImGui::SetScrollHereY(0.1f);
                m_SceneSettingsScrollTo.clear();
            }
            changed |= UI::PropertyBool("Bloom (yorqinlik)", p.Bloom);
            if (p.Bloom)
            {
                changed |= UI::PropertySlider("Bloom chegarasi", p.BloomThreshold, 0.0f, 1.0f);
                changed |= UI::PropertySlider("Bloom kuchi", p.BloomIntensity, 0.0f, 3.0f);
                changed |= UI::PropertySlider("Bloom radiusi", p.BloomRadius, 0.5f, 3.0f);
                UI::PropertyLabel("Bloom rangi", "Porlash shu rangga ko'paytiriladi: oq - yorug'lik o'z rangida porlaydi, iliq rang - "
                                                 "oltinrang, sovuq - zangori porlash");
                changed |= ImGui::ColorEdit3("##bloomtint", &p.BloomTint.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                UI::PropertyLabel("Linza changi", "Yorqin yorug'liklarning keng porlashi ifloslangan linzadagi dog' va changlarni "
                                                  "ko'rsatadi (kamera ob'ektiviga quyosh tushgandagidek). 0 - o'chiq");
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderFloat("##bloomdirt", &p.BloomDirt, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            }
            changed |= UI::PropertySlider("Vinyetka", p.Vignette, 0.0f, 1.0f);
            if (m_SceneSettingsScrollTo == "lens")
            {
                ImGui::SetScrollHereY(0.1f);
                m_SceneSettingsScrollTo.clear();
            }
            UI::PropertyLabel("Aberratsiya", "Xromatik aberratsiya: tasvir chetlariga tomon qizil va ko'k ranglar bir-biridan "
                                             "siljiydi (arzon linzadagi rangli hoshiya). 0 - o'chiq");
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::SliderFloat("##chromatic", &p.ChromaticAberration, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            UI::PropertyLabel("Plyonka donasi", "Tayyor tasvir ustidagi har kadr o'zgaradigan shovqin (kino plyonkasi donadorligi), "
                                                "o'rta tonlarda eng kuchli. 0 - o'chiq");
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::SliderFloat("##grain", &p.FilmGrain, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            if (p.FilmGrain > 0.0f)
            {
                UI::PropertyLabel("Dona o'lchami", "Bitta donaning o'lchami, chiqish pikselida (1 - eng mayda)");
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderFloat("##grainsize", &p.FilmGrainSize, 1.0f, PostProcessSettings::MaxFilmGrainSize, "%.1f px",
                                              ImGuiSliderFlags_AlwaysClamp);
            }
            changed |= UI::PropertySlider("To'yinganlik", p.Saturation, 0.0f, 2.0f);
            changed |= UI::PropertySlider("Kontrast", p.Contrast, 0.5f, 2.0f);
            changed |= UI::PropertySlider("Yorqinlik", p.Brightness, -0.5f, 0.5f);
            changed |= UI::PropertyColor3("Rang tuslash", p.Tint);
            UI::PropertyLabel("FXAA (silliqlash)", "Tez anti-aliasing: tayyor tasvirdagi qirralarning zinapoyasini yumshatadi "
                                                   "(tonemapping va rang sozlamalaridan keyin)");
            changed |= ImGui::Checkbox("##fxaa", &p.FXAA);
            if (p.FXAA)
            {
                UI::PropertyLabel("FXAA subpiksel", "Bir pikselli mayda detallar (ingichka chiziqlar, yaltirashlar) qanchalik "
                                                    "yumshatiladi: 0 - faqat qirralar, 1 - eng yumshoq");
                changed |= ImGui::SliderFloat("##fxaasub", &p.FXAASubpixel, 0.0f, 1.0f, "%.2f");
            }
            if (m_SceneSettingsScrollTo == "exposure")
            {
                ImGui::SetScrollHereY(0.25f);
                m_SceneSettingsScrollTo.clear();
            }
            UI::PropertyLabel("Avto ekspozitsiya", "Ko'zning moslashishi: tasvirning o'rtacha yorqinligi (log luminance, GPU'da "
                                                   "kichraytirish zanjiri bilan) o'lchanadi va vaqt o'tishi bilan o'rta kulrangga "
                                                   "keltiriladi - qorong'i joyda tasvir yorishadi, yorug'da qorayadi.\n"
                                                   "O'chiq: faqat qo'lda berilgan ekspozitsiya.");
            changed |= ImGui::Checkbox("##autoexposure", &p.AutoExposure);
            if (p.AutoExposure)
            {
                float adapted = 0.0f;
                if (m_PostHistory.ReadAdaptedEV(adapted))
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("hozir: %+.1f EV, x%.2f", adapted, p.GetAutoExposureMultiplier(adapted));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Viewport'ning moslashgan o'rtacha yorqinligi (log2 luminance) va u beradigan ko'paytma");
                }
                UI::PropertyLabel("Kompensatsiya", "Moslashgan tasvirni yana yorituvchi (+) yoki qoraytiruvchi (-) qo'shimcha, EV'da: "
                                                   "+1 - ikki barobar yorug'");
                changed |= ImGui::SliderFloat("##aecomp", &p.AutoExposureCompensation, -4.0f, 4.0f, "%+.1f EV");
                UI::PropertyLabel("Min EV", "Moslashadigan eng qorong'i o'rtacha yorqinlik (log2 luminance): bundan qorong'i "
                                            "sahnalar qorong'iligicha qoladi");
                if (ImGui::SliderFloat("##aemin", &p.AutoExposureMinEV, PostProcessSettings::MinAutoExposureEV,
                                       PostProcessSettings::MaxAutoExposureEV, "%.1f EV"))
                {
                    p.AutoExposureMaxEV = std::max(p.AutoExposureMaxEV, p.AutoExposureMinEV);
                    changed = true;
                }
                UI::PropertyLabel("Max EV", "Moslashadigan eng yorug' o'rtacha yorqinlik: bundan yorug' sahnalar yorug'ligicha qoladi");
                if (ImGui::SliderFloat("##aemax", &p.AutoExposureMaxEV, PostProcessSettings::MinAutoExposureEV,
                                       PostProcessSettings::MaxAutoExposureEV, "%.1f EV"))
                {
                    p.AutoExposureMinEV = std::min(p.AutoExposureMinEV, p.AutoExposureMaxEV);
                    changed = true;
                }
                UI::PropertyLabel("Moslashish tezligi", "Yangi yorqinlikka qanchalik tez moslashadi (1/s): katta qiymat - tezroq");
                if (ImGui::SliderFloat("##aespeed", &p.AutoExposureSpeed, 0.1f, 10.0f, "%.1f /s"))
                {
                    p.SanitizeAutoExposure();
                    changed = true;
                }
            }
            UI::EndPropertyGrid();
        }

        ImGui::SeparatorText(Tr(ICON_FA_FIRE "  2D yorug'lik"));
        if (m_SceneSettingsScrollTo == "lighting2d")
        {
            ImGui::SetScrollHereY(0.0f);
            m_SceneSettingsScrollTo.clear();
        }
        if (UI::BeginPropertyGrid("light2d"))
        {
            changed |= UI::PropertyColor3("Muhit rangi", s.Lighting2D.AmbientColor);
            if (ImGui::IsItemHovered())
                UI::Tooltip("\"Lit\" sprite va tilemap'lar hamma joyda oladigan yorug'lik; Light 2D yorug'liklari unga qo'shiladi. "
                            "Oq, kuch 1: yorug'liksiz Lit sprite odatdagidek ko'rinadi. Tun uchun: to'q ko'k, 0.2-0.4");
            if (UI::PropertyFloat("Muhit kuchi", s.Lighting2D.AmbientIntensity, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                s.Lighting2D.AmbientIntensity = std::max(0.0f, s.Lighting2D.AmbientIntensity);
                changed = true;
            }
            size_t lights = 0, lit = 0;
            for (auto e : m_ActiveScene->GetAllEntitiesWith<Light2DComponent>()) { (void)e; lights++; }
            for (auto e : m_ActiveScene->GetAllEntitiesWith<SpriteRendererComponent>())
                if (Entity(e, m_ActiveScene.get()).GetComponent<SpriteRendererComponent>().Lit) lit++;
            for (auto e : m_ActiveScene->GetAllEntitiesWith<TilemapComponent>())
                if (Entity(e, m_ActiveScene.get()).GetComponent<TilemapComponent>().Lit) lit++;
            UI::PropertyLabel("");
            ImGui::TextDisabled("%zu ta Light 2D, %zu ta Lit renderer", lights, lit);
            UI::EndPropertyGrid();
        }

        ImGui::SeparatorText(Tr(ICON_FA_ATOM "  Fizika"));
        if (m_SceneSettingsScrollTo == "physics")
        {
            ImGui::SetScrollHereY(0.0f);
            m_SceneSettingsScrollTo.clear();
        }
        if (UI::BeginPropertyGrid("phys"))
        {
            // "Loyihadan": the value is the project's (Project settings > Physics / Time) and follows it; off:
            // the scene's own (saved with the scene).
            auto fromProject = [&](const char* id, bool& useProject, const char* tooltip) {
                UI::PropertyLabel("Loyihadan", tooltip);
                ImGui::PushID(id);
                if (ImGui::Checkbox("##fromProject", &useProject))
                {
                    if (useProject)
                        s.ApplyProjectDefaults();
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_GEAR))
                {
                    m_ShowProjectSettings = true;
                    m_ProjectSettingsPanel.Focus(id[0] == 's' ? ProjectSettingsPanel::Tab::Time : ProjectSettingsPanel::Tab::PhysicsLayers);
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Loyiha sozlamalarida ochish");
                ImGui::PopID();
            };
            fromProject("g2", s.UseProjectGravity, "Belgilangan: 2D gravitatsiya loyihaniki (Loyiha sozlamalari > Fizika) va unga ergashadi; "
                                                   "olib tashlansa - sahnaning o'zi (sahna bilan saqlanadi)");
            ImGui::BeginDisabled(s.UseProjectGravity);
            changed |= UI::PropertyFloat2("Gravitatsiya", s.Gravity);
            ImGui::EndDisabled();
            fromProject("g3", s.UseProjectGravity3D, "Belgilangan: 3D gravitatsiya loyihaniki (Loyiha sozlamalari > Fizika) va unga ergashadi; "
                                                     "olib tashlansa - sahnaning o'zi");
            ImGui::BeginDisabled(s.UseProjectGravity3D);
            changed |= UI::PropertyFloat3("Gravitatsiya (3D)", s.Gravity3D);
            ImGui::EndDisabled();
            auto sliderInt = [&](const char* label, const char* tooltip, int& value, int min, int max, const char* format) {
                UI::PropertyLabel(label, tooltip);
                ImGui::PushID(label);
                if (ImGui::SliderInt("##v", &value, min, max, format, ImGuiSliderFlags_AlwaysClamp))
                    changed = true;
                ImGui::PopID();
            };
            auto& step = s.PhysicsStep;
            fromProject("step", s.UseProjectPhysicsStep, "Belgilangan: qadam chastotasi va kadrdagi qadamlar loyihaniki (Loyiha sozlamalari > Vaqt) "
                                                         "va unga ergashadi; olib tashlansa - sahnaning o'zi");
            ImGui::BeginDisabled(s.UseProjectPhysicsStep);
            sliderInt("Qadam chastotasi",
                      "Fizika (2D va 3D) soniyasiga shuncha qat'iy qadam tashlaydi. Ko'proq - tez jismlar va joint'lar aniqroq, "
                      "lekin qimmatroq. Standart: loyihaniki (60 Hz)",
                      step.StepRate, SceneSettings::PhysicsStepSettings::MinRate, SceneSettings::PhysicsStepSettings::MaxRate, "%d Hz");
            sliderInt("Kadrda ko'pi bilan",
                      "Sekin kadr shuncha qadamdan ortig'ini tashlab yuboradi: o'yin sekinlashadi, lekin orqada qolib ketmaydi. "
                      "Standart: loyihaniki (5)",
                      step.MaxStepsPerFrame, 1, SceneSettings::PhysicsStepSettings::MaxSteps, "%d qadam");
            ImGui::EndDisabled();

            auto& p2 = s.Physics2D;
            UI::PropertyHeader("Box2D (2D)", "2D jismlar simulyatsiyasining aniqligi va uxlash sozlamalari");
            sliderInt("Sub-qadamlar",
                      "Har fizika qadami shuncha Box2D qadamiga bo'linadi (har biri 1 / (chastota x sub-qadam) s): tez jismlar, uzun "
                      "joint zanjirlari va ragdoll'lar barqarorroq. Effektorlar har biridan oldin itaradi, joint uzilishi har biridan "
                      "keyin tekshiriladi. Standart: 1",
                      p2.SubSteps, 1, SceneSettings::Physics2DSettings::MaxSubSteps, "%d");
            sliderInt("Tezlik iteratsiyalari",
                      "Box2D tezliklarni (urilish, joint kuchlari) har qadamda shuncha marta hal qiladi: ko'proq - qattiqroq to'plamlar "
                      "va joint'lar. Standart: 6",
                      p2.VelocityIterations, 1, 50, "%d");
            sliderInt("Pozitsiya iteratsiyalari",
                      "Bir-biriga kirib qolgan jismlar va cho'zilgan joint'larni har qadamda shuncha marta to'g'rilaydi. Standart: 2",
                      p2.PositionIterations, 1, 50, "%d");
            UI::PropertyLabel("Uxlash", "Bir muddat qimirlamay turgan jismlar (ularga tegib / ulanib turganlar bilan birga) hisoblanmaydi "
                                        "- biror narsa tegsa, joint tortsa yoki skript uyg'otsa yana harakatlanadi. Har jismniki: "
                                        "Rigidbody 2D > Uxlash");
            if (ImGui::Checkbox("##sleep", &p2.AllowSleep))
                changed = true;
            ImGui::BeginDisabled(!p2.AllowSleep);
            UI::PropertyLabel("Uxlashgacha", "Jism (va unga tegib turganlar) shuncha soniya qimirlamasa uxlaydi. Box2D'niki: 0.5 s");
            if (ImGui::DragFloat("##tts", &p2.TimeToSleep, 0.01f, 0.05f, 60.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            ImGui::EndDisabled();
            if (m_ActiveScene->GetPhysicsWorld2D()) // while playing / simulating
            {
                const PhysicsStats2D stats = m_ActiveScene->GetPhysics2DStats();
                UI::PropertyLabel("Jismlar");
                ImGui::Text("%d ta (%d harakatlanuvchi)", stats.Bodies, stats.Moving);
                UI::PropertyLabel("Uyg'oq");
                ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), ICON_FA_BOLT "  %d ta", stats.Awake);
                UI::PropertyLabel("Uxlayapti", "Fizik colliderlar ko'rsatilganda uxlayotgan jismlarning chiziqlari ko'kish-kulrang");
                ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.95f, 1.0f), ICON_FA_MOON "  %d ta", stats.Sleeping);
                UI::PropertyLabel("Joint / kontakt");
                ImGui::Text("%d joint, %d kontakt", stats.Joints, stats.Contacts);
                UI::PropertyLabel("Bu kadrda");
                ImGui::Text("%d ta Box2D qadami", m_ActiveScene->GetPhysics2DStepsLastFrame());
            }

            auto& p3 = s.Physics3D;
            UI::PropertyHeader("Jolt (3D)", "3D jismlarning uxlash sozlamalari");
            UI::PropertyLabel("Uxlash", "Bir muddat qimirlamay turgan 3D jismlar (ularga tegib / ulanib turganlar bilan birga) "
                                             "hisoblanmaydi - biror narsa tegsa, joint tortsa, qahramon itarsa yoki skript uyg'otsa "
                                             "(WakeUp3D) yana harakatlanadi. Har jismniki: Rigidbody 3D > Uxlash");
            if (ImGui::Checkbox("##sleep3d", &p3.AllowSleep))
                changed = true;
            ImGui::BeginDisabled(!p3.AllowSleep);
            UI::PropertyLabel("Uxlashgacha", "Jism (va unga tegib turganlar) shuncha soniya qimirlamasa uxlaydi. Jolt'niki: 0.5 s");
            if (ImGui::DragFloat("##tts3d", &p3.TimeToSleep, 0.01f, 0.05f, 60.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            ImGui::EndDisabled();
            if (m_ActiveScene->GetPhysicsWorld3D()) // while playing / simulating
            {
                const PhysicsStats3D stats = m_ActiveScene->GetPhysics3DStats();
                UI::PropertyLabel("Jismlar");
                ImGui::Text("%d ta (%d harakatlanuvchi), %d qahramon", stats.Bodies, stats.Moving, stats.Characters);
                UI::PropertyLabel("Uyg'oq");
                ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), ICON_FA_BOLT "  %d ta", stats.Awake);
                UI::PropertyLabel("Uxlayapti", "Fizik colliderlar ko'rsatilganda uxlayotgan jismlarning chiziqlari ko'kish-kulrang");
                ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.95f, 1.0f), ICON_FA_MOON "  %d ta", stats.Sleeping);
                UI::PropertyLabel("Joint / kontakt", "Kontakt - tegib turgan jism juftlari (triggerlar ham)");
                ImGui::Text("%d joint, %d kontakt", stats.Joints, stats.Contacts);
            }
            UI::EndPropertyGrid();
        }
        {
            // The standard: gravity and step from the project, Box2D / Jolt at their defaults.
            const bool standard = s.UseProjectGravity && s.UseProjectGravity3D && s.UseProjectPhysicsStep &&
                                  s.Physics2D == SceneSettings::Physics2DSettings() && s.Physics3D == SceneSettings::Physics3DSettings();
            ImGui::BeginDisabled(standard);
            if (ImGui::Button(Tr(ICON_FA_ROTATE_LEFT "  Standart fizika sozlamalari")))
            {
                s.UseProjectGravity = s.UseProjectGravity3D = s.UseProjectPhysicsStep = true;
                s.ApplyProjectDefaults();
                s.Physics2D = {};
                s.Physics3D = {};
                changed = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                UI::Tooltip("Gravitatsiya, qadam chastotasi va kadrdagi qadamlar loyihadan (Loyiha sozlamalari > Fizika / Vaqt), 1 sub-qadam, "
                            "6 / 2 iteratsiya, 2D va 3D uxlash 0.5 s dan keyin");
        }

        ImGui::SeparatorText(Tr(ICON_FA_CAMERA "  Editor kamerasi"));
        if (UI::BeginPropertyGrid("cam"))
        {
            UI::PropertyFloat("Uchish tezligi", m_EditorCamera.FlySpeed, 0.1f, 0.5f, 200.0f);
            UI::EndPropertyGrid();
        }
        ImGui::TextDisabled("RMB + WASD/QE: uchish  |  Alt+LMB: aylanish");
        ImGui::TextDisabled("MMB: surish  |  G'ildirak: yaqinlashtirish");

        if (auto project = Project::GetActive())
        {
            ImGui::SeparatorText(Tr(ICON_FA_GAMEPAD "  Loyiha"));
            auto& c = project->GetConfig();
            if (UI::BeginPropertyGrid("proj"))
            {
                UI::PropertyLabel("Nomi");
                ImGui::TextUnformatted(c.Name.c_str());
                UI::PropertyLabel("Boshlang'ich sahna");
                ImGui::TextUnformatted(c.StartScene.empty() ? "-" : c.StartScene.generic_string().c_str());
                UI::EndPropertyGrid();
            }
            if (!m_EditorScenePath.empty() && ImGui::Button(Tr(ICON_FA_HOUSE "  Shu sahnani boshlang'ich qilish")))
            {
                c.StartScene = Project::GetAssetRelativePath(m_EditorScenePath);
                SaveProjectSettingsNow();
            }
            if (ImGui::Button(Tr(ICON_FA_GAMEPAD "  Input va loyiha sozlamalari")))
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
        if (ImGui::Begin(Tr(ICON_FA_CHART_LINE "  Statistika###Stats"), &m_ShowStats))
        {
            auto& stats = Renderer::GetStats();
            // The real time of each recent frame (the viewport overlay draws the same history).
            const FrameTimeHistory& frameTimes = Application::Get().GetFrameTimeHistory();
            float history[FrameTimeHistory::Capacity] = {};
            for (size_t i = 0; i < frameTimes.Size(); i++)
                history[i] = frameTimes.Get(i);
            ImGui::Text("FPS: %.0f   Kadr: %.2f ms", Application::Get().GetFPS(), Application::Get().GetFrameTime());
            ImGui::PlotLines("##ft", history, (int)std::max<size_t>(1, frameTimes.Size()), 0, nullptr, 0.0f, 40.0f, ImVec2(-1, 50));
            // ZAMIN_EDITOR_STATS=profiler (automation): the render numbers start folded, the profiler shows.
            // ZAMIN_EDITOR_STATS=audio: the same, the audio profiler first; =load: the scene load times.
            static const bool profilerFirst = [] {
                const char* v = std::getenv("ZAMIN_EDITOR_STATS");
                return v && (std::string_view(v).find("profiler") != std::string_view::npos ||
                             std::string_view(v).find("audio") != std::string_view::npos ||
                             std::string_view(v).find("load") != std::string_view::npos);
            }();
            static const bool loadFirst = [] {
                const char* v = std::getenv("ZAMIN_EDITOR_STATS");
                return v && std::string_view(v).find("load") != std::string_view::npos;
            }();
            if (profilerFirst && ImGui::IsWindowAppearing())
                ImGui::SetNextItemOpen(false);
            if (!ImGui::CollapsingHeader(Tr(ICON_FA_CUBES "  Render statistikasi"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                UI_AnimationStats();
                if (loadFirst && ImGui::IsWindowAppearing())
                    ImGui::SetNextItemOpen(false); // the scene load times in view (automation)
                UI_AudioProfiler();
                UI_SceneLoadTimings(true);
                UI_ProfilerTree(true);
                ImGui::End();
                return;
            }
            // The label column as wide as the longest label (they would run into the values).
            float statsLabelWidth = 0.0f;
            for (const char* label : { "Soya chizishlari", "Soya instanced", "Shaffof meshlar", "Render o'lchami", "Nofaol yorug'lik",
                                       "Tile bo'laklari" })
                statsLabelWidth = std::max(statsLabelWidth, ImGui::CalcTextSize(label).x);
            statsLabelWidth = std::min(statsLabelWidth + ImGui::GetStyle().ItemSpacing.x, ImGui::GetContentRegionAvail().x * 0.5f);
            if (UI::BeginPropertyGrid("stats", statsLabelWidth))
            {
                // "drawn (culled)" pairs: the culled part dimmed.
                auto drawnCulled = [](uint32_t drawn, uint32_t culled) {
                    ImGui::Text("%u", drawn);
                    ImGui::SameLine(0.0f, 6.0f);
                    ImGui::TextDisabled("(%u kesildi)", culled);
                };
                auto instanced = [](uint32_t meshes, uint32_t draws) {
                    ImGui::Text("%u", meshes);
                    ImGui::SameLine(0.0f, 6.0f);
                    ImGui::TextDisabled("/ %u chaqiruv", draws);
                };
                UI::PropertyLabel("Draw calls", "GPU'ga yuborilgan chizish buyruqlari (instanced partiya bitta buyruq)");
                ImGui::Text("%u", stats.DrawCalls);
                UI::PropertyLabel("Meshlar", "Chizilgan meshlar (qavsda kesilganlari). Frustum culling: kamera ko'rmaydigan meshlar "
                                             "(dunyo AABB chegaralari ko'rish hajmidan tashqarida) chizilmaydi");
                drawnCulled(stats.MeshCount, stats.CulledMeshCount);
                UI::PropertyLabel("Instanced", "GPU instancing: bir xil mesh va material (skinned emas, opaque/cutout) bitta "
                                               "chaqiruvda - har nusxa o'z transformi va ID'si bilan chiziladi. Shu yo'l bilan "
                                               "chizilgan meshlar / ular olgan chaqiruvlar");
                instanced(stats.InstancedMeshCount, stats.InstancedDrawCount);
                UI::PropertyLabel("Shaffof meshlar", "Shaffof (Transparent) sirtlar: ustuvorlik va kameradan uzoqlik bo'yicha "
                                                     "orqadan oldinga saralanib, oxirida chiziladi");
                ImGui::Text("%u", stats.TransparentCount);
                UI::PropertyLabel("Uchburchaklar"); ImGui::Text("%u", stats.TriangleCount);
                UI::PropertyLabel("Soya chizishlari", "Soya tashlovchilar kaskadlarga (kaskadlar bo'yicha yig'indi; qavsda kaskad "
                                                      "hajmidan tashqarida qolib kesilganlari)");
                drawnCulled(stats.ShadowCasterCount, stats.CulledShadowCasterCount);
                UI::PropertyLabel("Soya instanced", "Kaskadlarga bir xil mesh (cutout bo'lsa, bir xil tekstura va chegara) bilan "
                                                    "instanced chizilgan soya tashlovchilar / chaqiruvlar");
                instanced(stats.InstancedShadowCasterCount, stats.InstancedShadowDrawCount);
                UI::PropertyLabel("2D obyektlar", "Sprite, doira, matn va zarrachalar tizimlari (qavsda kesilganlari): kamera "
                                                  "ko'rmaydiganlari chizilmaydi");
                drawnCulled(stats.SpriteCount, stats.CulledSpriteCount);
                UI::PropertyLabel("Tile bo'laklari", "Tilemap'lar 16x16 plitkali bo'laklarda chiziladi: ko'rinmaydigan bo'lak butunlay "
                                                     "tashlab ketiladi (qavsda kesilganlari)");
                drawnCulled(stats.TileChunkCount, stats.CulledTileChunkCount);
                UI::PropertyLabel("Particle'lar", "Chizilgan particle'lar; qavsda GPU instancing bilan chizilganlari / ular olgan "
                                                  "chaqiruvlar (3D billboardlar: har tizim bitta chaqiruvda)");
                ImGui::Text("%u  (%u / %u)", stats.ParticleCount, stats.InstancedParticleCount, stats.InstancedParticleDrawCount);
                UI::PropertyLabel("Quadlar"); ImGui::Text("%u", stats.QuadCount);
                UI::PropertyLabel("Doiralar"); ImGui::Text("%u", stats.CircleCount);
                UI::PropertyLabel("Chiziqlar"); ImGui::Text("%u", stats.LineCount);
                {
                    const auto& fb = m_Framebuffer->GetSpecification();
                    UI::PropertyLabel("Render o'lchami", "Sahna tasviri chizilayotgan o'lcham (render masshtabi bilan)");
                    ImGui::Text("%u x %u  (x%.2f)", fb.Width, fb.Height, m_ActiveScene->GetSettings().RenderScale);
                }
                UI::PropertyLabel("Obyektlar"); ImGui::Text("%zu", m_ActiveScene->GetRegistry().view<IDComponent>().size());
                // Lights that light the scene (inactive ones take no slot).
                {
                    SceneRenderOptions view;
                    view.SkipEditorHidden = m_SceneState != SceneState::Play;
                    Scene::LightSet lights = m_ActiveScene->CollectLights(m_EditorCamera.GetPosition(), view);
                    UI::PropertyLabel("Quyosh", "Asosiy Directional Light: sahna tartibida 'Soyalar' yoqilgan birinchisi (bo'lmasa "
                                                "birinchisi). Soyani faqat u beradi, osmondagi quyosh diski unga ergashadi.");
                    if (lights.Sun == entt::null)
                        ImGui::TextUnformatted("-");
                    else if (lights.DirectionalLights.size() > 1)
                        ImGui::Text("%s  +%zu", Entity(lights.Sun, m_ActiveScene.get()).GetName().c_str(),
                                    lights.DirectionalLights.size() - 1);
                    else
                        ImGui::TextUnformatted(Entity(lights.Sun, m_ActiveScene.get()).GetName().c_str());
                    if (lights.DirectionalLights.size() > 1 && ImGui::IsItemHovered())
                    {
                        std::string others;
                        for (entt::entity e : lights.DirectionalLights)
                            if (e != lights.Sun)
                                others += std::format("\n  - {}", Entity(e, m_ActiveScene.get()).GetName());
                        std::string dropped = lights.DroppedDirectionalLights
                            ? std::format("\nLimitdan ({} ta) oshgan {} tasi yoritmaydi.", Renderer3D::MaxDirectionalLights,
                                          lights.DroppedDirectionalLights)
                            : std::string();
                        ImGui::SetTooltip("Yana yoritadigan (soyasiz) quyoshlar:%s%s", others.c_str(), dropped.c_str());
                    }
                    static const std::string localTip = std::format("Nuqtali va spot yorug'liklar birgalikda eng ko'pi bilan {} ta "
                                                                    "joyni egallaydi", Renderer3D::MaxLocalLights);
                    UI::PropertyLabel("Nuqtali + spot", localTip.c_str());
                    ImGui::Text("%zu + %zu / %u", lights.PointLights.size(), lights.SpotLights.size(), Renderer3D::MaxLocalLights);
                    if (lights.DroppedLocalLights && ImGui::IsItemHovered())
                        ImGui::SetTooltip("Limitdan oshgan %u tasi ishlatilmayapti (kameraga eng kam ta'sir qiladiganlari: "
                                          "uzoq, kuchsiz yoki radiusi kichik)", lights.DroppedLocalLights);
                    UI::PropertyLabel("Nofaol yorug'lik");
                    ImGui::Text("%u", lights.InactiveLights);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Nofaol obyektlardagi yorug'liklar hisobga olinmaydi va joy egallamaydi");
                }
                UI::PropertyLabel("Hover");
                ImGui::TextUnformatted(m_HoveredEntity ? m_HoveredEntity.GetName().c_str() : "-");
                UI::EndPropertyGrid();
            }

            UI_AnimationStats();
            UI_AudioProfiler();
            UI_SceneLoadTimings(true);
            UI_ProfilerTree(true);
        }
        ImGui::End();
    }

    void EditorLayer::UI_AnimationStats()
    {
        const Scene::AnimationStats& a = m_ActiveScene->GetAnimationStats();
        if (a.Animators == 0 && a.CacheBytes == 0)
            return; // nothing animated: no section
        if (!ImGui::CollapsingHeader(Tr(ICON_FA_PERSON_RUNNING "  Animatsiya"), ImGuiTreeNodeFlags_DefaultOpen))
            return;
        float labelWidth = 0.0f;
        for (const char* label : { "Pozalangan", "O'zgarmagan", "Kesh / klipdan", "Kesh xotirasi" })
            labelWidth = std::max(labelWidth, ImGui::CalcTextSize(label).x);
        labelWidth = std::min(labelWidth + ImGui::GetStyle().ItemSpacing.x * 2.0f, ImGui::GetContentRegionAvail().x * 0.5f);
        if (!UI::BeginPropertyGrid("animstats", labelWidth))
            return;
        UI::PropertyLabel("Animatorlar", "Faol Animator'lar (oxirgi yangilanishda) va ularga ketgan vaqt");
        ImGui::Text("%u", a.Animators);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextDisabled("(%.3f ms)", a.Milliseconds);
        UI::PropertyLabel("Pozalangan", "Skelet (yoki blend shape'lar) shu kadrda qaytadan hisoblanganlar");
        ImGui::Text("%u", a.Posed);
        UI::PropertyLabel("O'zgarmagan", "Kirishlari o'tgan kadrdagidek (pauza, tezlik 0, tugagan klip): oldingi poza qoldi, hisoblanmadi");
        ImGui::Text("%u", a.Unchanged);
        UI::PropertyLabel("Kesilgan", "Hech bir kamera ko'rmagan modellar (Animator > Samaradorlik > Ko'rinmasa: Pozasiz / To'liq)");
        if (a.Culled > 0)
            ImGui::TextColored(UI::AccentColor(), "%u", a.Culled);
        else
            ImGui::TextDisabled("0");
        UI::PropertyLabel("Kesh / klipdan", "Klip pozalari: keshlangan kadrlardan (Poza keshi) / klip kalitlaridan o'qilganlar");
        ImGui::Text("%u / %u", a.CachedSamples, a.ClipSamples);
        UI::PropertyLabel("Kesh xotirasi", "Barcha modellarning poza keshlari");
        if (a.CacheBytes >= 1024 * 1024)
            ImGui::Text("%.2f MB", (double)a.CacheBytes / (1024.0 * 1024.0));
        else
            ImGui::Text("%.1f KB", (double)a.CacheBytes / 1024.0);
        UI::EndPropertyGrid();
    }

    void EditorLayer::UI_AudioProfiler()
    {
        if (!ImGui::CollapsingHeader(Tr(ICON_FA_VOLUME_HIGH "  Audio profiler"), ImGuiTreeNodeFlags_DefaultOpen))
            return;
        const AudioProfile audio = AudioEngine::GetProfile();
        if (!audio.Available)
        {
            ImGui::TextColored(FrameRateColor(1), ICON_FA_TRIANGLE_EXCLAMATION "  Ovoz tizimi ishlamayapti (audio qurilmasi yo'q)");
            return;
        }
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec4 color = VoiceLimitColor(audio.Voices, audio.MaxVoices);
        const ImVec4 warn = FrameRateColor(1);

        // ---- the count against the limit, what the limit cost
        if (ImFont* title = ImGuiLayer::GetTitleFont())
            ImGui::PushFont(title);
        ImGui::TextColored(color, "%zu", audio.Voices);
        if (ImGuiLayer::GetTitleFont())
            ImGui::PopFont();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Limitga kiradigan ovozlar: ijro etilayotgan va pauzadagi Audio Source / PlaySound ovozlari. Musiqa va editordagi "
                        "fayl ko'rib chiqishi hisobga kirmaydi");
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::BeginGroup();
        ImGui::TextDisabled("/ %d ovoz limiti", audio.MaxVoices);
        ImGui::Text("cho'qqi %zu", audio.PeakVoices);
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        const float column = ImGui::GetCursorPosX();
        auto right = [&](const std::string& text, bool alert) {
            const float w = ImGui::CalcTextSize(text.c_str()).x;
            ImGui::SetCursorPosX(std::max(column, ImGui::GetWindowContentRegionMax().x - w));
            if (alert)
                ImGui::TextColored(warn, "%s", text.c_str());
            else
                ImGui::TextDisabled("%s", text.c_str());
        };
        right(std::format("to'xtatilgan {}", audio.Stolen), audio.Stolen > 0);
        right(std::format("rad etilgan {}", audio.Rejected), audio.Rejected > 0);
        ImGui::EndGroup();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Limit to'lganda: boshqa ovozga joy berib to'xtatilganlar / joy topilmay umuman ijro etilmaganlar (Audio Source > "
                        "Ovozlar limiti: ustuvorlik va qoida)");
        DrawShareBar(width, 5.0f, audio.MaxVoices > 0 ? (float)audio.Voices / (float)audio.MaxVoices : 0.0f, color);
        DrawVoiceGraph(AudioEngine::GetVoiceHistory(), audio.MaxVoices, width, std::floor(ImGui::GetFontSize() * 3.2f), 1.0f);

        // ---- what kind of sounds
        float labelWidth = 0.0f;
        for (const char* label : { "Oldindan yuklangan", "Ko'rib chiqish" })
            labelWidth = std::max(labelWidth, ImGui::CalcTextSize(label).x);
        labelWidth = std::min(labelWidth + ImGui::GetStyle().ItemSpacing.x, width * 0.5f);
        if (UI::BeginPropertyGrid("audioProfile", labelWidth))
        {
            UI::PropertyLabel("Ovozlar", "Hamma tirik ovozlar (musiqa va ko'rib chiqish bilan): eshitilayotgani, pauzadagisi va so'nib "
                                         "to'xtayotgani");
            // Only the parts there are (the panel is often narrow).
            auto parts = [](std::initializer_list<std::pair<const char*, size_t>> list) {
                std::string text;
                for (const auto& [name, count] : list)
                    if (count)
                        text += std::format("{}{} {}", text.empty() ? "" : ", ", name, count);
                return text.empty() ? std::string("-") : text;
            };
            ImGui::Text("%zu", audio.Sounds);
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextDisabled("%s", parts({ { "ijroda", audio.Playing }, { "pauza", audio.Paused }, { "so'nmoqda", audio.Stopping } }).c_str());
            UI::PropertyLabel("Turi", "3D (fazoviy), diskdan oqim bilan o'qilayotgan va o'z filtri (low-pass / high-pass) bor ovozlar");
            ImGui::TextUnformatted(parts({ { "3D", audio.Spatial }, { "oqim", audio.Streamed }, { "filtrli", audio.Filtered } }).c_str());
            UI::PropertyLabel("Musiqa", "Audio::PlayMusic treklari (crossfade paytida 2 ta) - limitga kirmaydi");
            ImGui::Text("%zu", audio.Music);
            UI::PropertyLabel("Ko'rib chiqish", "Mixerdan tashqari ovozlar: editordagi fayl ko'rib chiqishi");
            ImGui::Text("%zu", audio.Previews);
            UI::PropertyLabel("Boshlangan", "Hisoblagichlar nollanganidan beri boshlangan ovozlar (musiqa bilan)");
            ImGui::Text("%llu", (unsigned long long)audio.Started);
            UI::PropertyLabel("Oldindan yuklangan", "Xotirada yechilgan holda saqlanayotgan fayllar (import sozlamasi Preload, "
                                                    "Audio::Preload)");
            ImGui::Text("%zu fayl", audio.Preloaded);
            UI::EndPropertyGrid();
        }

        // ---- by mixer group
        if (ImGui::BeginTable("##audioGroups", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                      ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("Guruh", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("O'zi", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("Jami", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("Daraja", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (const AudioGroupProfile& g : audio.Groups)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (float)g.Depth * ImGui::GetFontSize() * 0.8f);
                if (g.TotalVoices)
                    ImGui::TextUnformatted(g.Name.c_str());
                else
                    ImGui::TextDisabled("%s", g.Name.c_str());
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{} -> {}\nO'zida: {} (pauza {}, so'nmoqda {}, oqim {}, 3D {})\nBola guruhlari bilan: {}\n"
                                            "Balandligi (ota guruhlar bilan): {:.1f} dB", g.Name, g.Parent.empty() ? "chiqish" : g.Parent,
                                            g.Voices, g.Paused, g.Stopping, g.Streamed, g.Spatial, g.TotalVoices,
                                            AudioGain::ToDecibels(g.Gain)).c_str());
                ImGui::TableSetColumnIndex(1);
                if (g.Voices)
                    ImGui::Text("%zu", g.Voices);
                else
                    ImGui::TextDisabled("0");
                ImGui::TableSetColumnIndex(2);
                if (g.TotalVoices != g.Voices)
                    ImGui::Text("%zu", g.TotalVoices);
                else
                    ImGui::TextDisabled("%zu", g.TotalVoices);
                ImGui::TableSetColumnIndex(3);
                if (g.Stolen || g.Rejected)
                {
                    ImGui::TextColored(warn, "%llu / %llu", (unsigned long long)g.Stolen, (unsigned long long)g.Rejected);
                    if (ImGui::IsItemHovered())
                        UI::Tooltip("Limit sababli to'xtatilgan / ijro etilmagan ovozlar");
                }
                else
                    ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(4);
                // The meter's hold peak on a -60..0 dB scale.
                const float db = g.Level > 0.0f ? AudioGain::ToDecibels(g.Level) : AudioGain::MinDecibels;
                const float share = std::clamp((db - AudioGain::MinDecibels) / -AudioGain::MinDecibels, 0.0f, 1.0f);
                const ImVec4 levelColor = db > -1.0f ? FrameRateColor(2) : db > -9.0f ? FrameRateColor(1) : UI::AccentColor();
                DrawShareBar(std::max(10.0f, ImGui::GetContentRegionAvail().x), 5.0f, share, levelColor);
                if (ImGui::IsItemHovered() && g.Level > 0.0f)
                    UI::Tooltip(std::format("Cho'qqi: {:.1f} dBFS", db).c_str());
            }
            ImGui::EndTable();
        }
        if (ImGui::SmallButton(Tr(ICON_FA_ROTATE_LEFT "  Hisoblagichlarni nollash")))
            AudioEngine::ResetProfileCounters();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Cho'qqi, boshlangan, to'xtatilgan va rad etilgan ovozlar qaytadan sanaladi (Play boshlanganda o'zi nollanadi)");
        ImGui::SameLine();
        const int seconds = (int)audio.SinceReset;
        ImGui::TextDisabled("%s", seconds < 60 ? std::format("{} s dan beri", seconds).c_str()
                                               : std::format("{}:{:02} dan beri", seconds / 60, seconds % 60).c_str());
    }

    void EditorLayer::UI_ProfilerWindow()
    {
        if (!m_ShowProfiler)
            return;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver,
                                ImVec2(0.5f, 0.5f));
        if (ImGui::Begin(Tr(ICON_FA_STOPWATCH "  Profiler###Profiler"), &m_ShowProfiler))
        {
            UI_ProfilerTree(false);
            ImGui::Spacing();
            UI_SceneLoadTimings(false);
        }
        ImGui::End();
    }

    void EditorLayer::UI_ProfilerTree(bool header)
    {
        if (header)
        {
            const bool open = ImGui::CollapsingHeader(Tr(ICON_FA_STOPWATCH "  Profiler (CPU, ms)"), ImGuiTreeNodeFlags_DefaultOpen |
                                                      ImGuiTreeNodeFlags_AllowOverlap);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
            if (UI::ToolbarButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER "###profilerWindow", "Profiler'ni alohida oynada ochish",
                                  m_ShowProfiler, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                m_ShowProfiler = !m_ShowProfiler;
            if (!open)
                return;
        }
        ImGui::Checkbox(Tr("O'rtacha"), &m_ProfilerAverage);
        if (ImGui::IsItemHovered())
            UI::Tooltip("Har bo'limning vaqti so'nggi kadrlar bo'yicha silliqlangan (aks holda faqat oxirgi kadr - sakraydi)");
        ImGui::SameLine();
        if (ImGui::Checkbox(Tr(ICON_FA_SNOWFLAKE "  Muzlatish"), &m_ProfilerFrozen) && m_ProfilerFrozen)
            m_ProfilerFrozenFrame = Profiler::GetLastFrame();
        if (ImGui::IsItemHovered())
            UI::Tooltip("Joriy kadrni ushlab qoladi: daraxtni bemalol ko'rib chiqish uchun");
        const std::vector<Profiler::Entry>& entries = m_ProfilerFrozen ? m_ProfilerFrozenFrame : Profiler::GetLastFrame();
        const bool average = m_ProfilerAverage;
        auto timeOf = [&](const Profiler::Entry& e) { return average ? e.AverageMilliseconds : e.Milliseconds; };
        // The averaged self time: the node's average minus its direct children's averages.
        std::vector<float> self(entries.size());
        for (size_t i = 0; i < entries.size(); i++)
            self[i] = timeOf(entries[i]);
        for (const auto& e : entries)
            if (e.Parent >= 0)
                self[(size_t)e.Parent] -= timeOf(e);
        // The bars: share of the frame (the "Kadr" scope; the slowest top scope when there is none).
        float total = 0.0f;
        for (const auto& e : entries)
            if (e.Parent < 0 && std::strcmp(e.Name, "Kadr") == 0)
                total = timeOf(e);
        if (total <= 0.0f)
            for (const auto& e : entries)
                if (e.Parent < 0)
                    total = std::max(total, timeOf(e));
        total = std::max(0.001f, total);
        if (entries.empty())
        {
            ImGui::TextDisabled("Profiler yozuvlari yo'q");
            return;
        }
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("##profilerTree", 3, flags))
            return;
        const float digits = ImGui::CalcTextSize("00.00").x + ImGui::GetStyle().CellPadding.x;
        ImGui::TableSetupColumn("Bo'lim", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, digits);
        ImGui::TableSetupColumn("O'zi", ImGuiTableColumnFlags_WidthFixed, digits);
        ImGui::TableHeadersRow();
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, std::floor(ImGui::GetFontSize() * 0.85f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto drawNode = [&](auto& draw, size_t index) -> void {
            const Profiler::Entry& e = entries[index];
            const bool leaf = (size_t)e.End <= index + 1;
            const float ms = timeOf(e);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // Behind the name: its share of the frame (deeper scopes in a cooler colour).
            {
                const ImVec2 cell = ImGui::GetCursorScreenPos(); // after the tree's indentation: a flame graph
                const float width = ImGui::GetContentRegionAvail().x * std::clamp(ms / total, 0.0f, 1.0f);
                const float depthTint = std::min(1.0f, e.Depth * 0.25f);
                const ImU32 color = ImGui::GetColorU32(ImVec4(0.18f + 0.12f * depthTint, 0.62f - 0.14f * depthTint, 0.52f + 0.35f * depthTint, 0.34f));
                if (width >= 1.0f)
                    dl->AddRectFilled(cell, ImVec2(cell.x + width, cell.y + ImGui::GetTextLineHeight() + 2.0f), color, 2.0f);
            }
            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
            if (leaf)
                nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 1.0f));
            const bool open = ImGui::TreeNodeEx(e.Name, nodeFlags);
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered())
                UI::Tooltip(std::format("{}\nJami: {:.3f} ms (oxirgi kadr {:.3f}, o'rtacha {:.3f})\nO'zi (ichki bo'limlarsiz): {:.3f} ms\n"
                                        "Chaqiruvlar: {}\nKadrdagi ulushi: {:.0f}%", e.Name, ms, e.Milliseconds, e.AverageMilliseconds,
                                        std::max(0.0f, self[index]), e.Calls, 100.0f * std::clamp(ms / total, 0.0f, 1.0f)).c_str());
            if (e.Calls > 1)
            {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("x%d", e.Calls);
            }
            // Two decimals below 100 ms (the column's width), one above.
            auto number = [](float value) { value >= 99.995f ? ImGui::Text("%.1f", value) : ImGui::Text("%.2f", value); };
            ImGui::TableSetColumnIndex(1);
            number(ms);
            ImGui::TableSetColumnIndex(2);
            if (leaf)
                ImGui::TextDisabled("-");
            else
                number(std::max(0.0f, self[index]));
            if (open && !leaf)
            {
                for (size_t child = index + 1; child < (size_t)e.End && child < entries.size();
                     child = std::max(child + 1, (size_t)entries[child].End))
                    draw(draw, child);
                ImGui::TreePop();
            }
        };
        for (size_t i = 0; i < entries.size(); i = std::max(i + 1, (size_t)entries[i].End))
            drawNode(drawNode, i);
        ImGui::PopStyleVar();
        ImGui::EndTable();
    }

    // The colour of a scene load stage in the bars (the same in every row).
    static ImVec4 SceneLoadStageColor(SceneLoadTiming::Stage stage)
    {
        switch (stage)
        {
            case SceneLoadTiming::Stage::Read:        return ImVec4(0.55f, 0.60f, 0.70f, 1.0f);
            case SceneLoadTiming::Stage::Parse:       return ImVec4(0.95f, 0.76f, 0.30f, 1.0f);
            case SceneLoadTiming::Stage::Migrate:     return ImVec4(0.78f, 0.52f, 0.92f, 1.0f);
            case SceneLoadTiming::Stage::Deserialize: return ImVec4(0.30f, 0.76f, 0.56f, 1.0f);
            case SceneLoadTiming::Stage::Decode:      return ImVec4(0.50f, 0.72f, 0.98f, 1.0f);
            case SceneLoadTiming::Stage::Assets:      return ImVec4(0.96f, 0.50f, 0.34f, 1.0f);
            case SceneLoadTiming::Stage::Audio:       return ImVec4(0.92f, 0.42f, 0.66f, 1.0f);
            case SceneLoadTiming::Stage::Physics:     return ImVec4(0.36f, 0.56f, 0.96f, 1.0f);
            case SceneLoadTiming::Stage::Scripts:     return ImVec4(0.68f, 0.86f, 0.36f, 1.0f);
            case SceneLoadTiming::Stage::Count:       break;
        }
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // One load's stages side by side, as wide as their share of its time; hovering a part names it.
    static void DrawSceneLoadBar(const SceneLoadTiming& t, float width, float height, bool tooltips)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##loadBar", ImVec2(std::max(1.0f, width), height));
        const bool hovered = tooltips && ImGui::IsItemHovered();
        dl->AddRectFilled(start, ImVec2(start.x + width, start.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
        const double total = t.TotalMilliseconds();
        if (total <= 0.0)
            return;
        float x = start.x;
        const float mouseX = ImGui::GetIO().MousePos.x;
        for (size_t i = 0; i < SceneLoadTiming::StageCount; i++)
        {
            const double ms = t.Milliseconds[i];
            if (ms <= 0.0)
                continue;
            const float w = (float)(ms / total) * width;
            if (w < 0.5f)
                continue;
            const auto stage = (SceneLoadTiming::Stage)i;
            ImVec4 color = SceneLoadStageColor(stage);
            const bool worker = t.How == SceneLoadTiming::Source::Async && SceneLoadTiming::IsWorkerStage(stage);
            if (worker)
                color.w = 0.55f; // off the main thread: dimmer
            dl->AddRectFilled(ImVec2(x, start.y), ImVec2(x + w, start.y + height), ImGui::GetColorU32(color));
            if (hovered && mouseX >= x && mouseX < x + w)
            {
                dl->AddRect(ImVec2(x, start.y), ImVec2(x + w, start.y + height), IM_COL32(255, 255, 255, 220), 0.0f, 0, 1.5f);
                UI::Tooltip(std::format("{}: {:.2f} ms ({:.0f}%){}\n{}", SceneLoadTiming::StageName(stage), ms, 100.0 * ms / total,
                                        worker ? "  - fon oqimida" : "", SceneLoadTiming::StageDescription(stage)).c_str());
            }
            x += w;
        }
    }

    void EditorLayer::UI_SceneLoadTimings(bool header)
    {
        const std::vector<SceneLoadTiming> history = SceneLoadProfiler::GetHistory();
        if (header)
        {
            const std::string title = history.empty() ? std::string(Tr(ICON_FA_HOURGLASS_HALF "  Sahna yuklash vaqti###sceneLoads"))
                : std::format(ICON_FA_HOURGLASS_HALF "  {} ({})###sceneLoads", Tr("Sahna yuklash vaqti"), history.size());
            // ZAMIN_EDITOR_STATS=load (automation): this section open first.
            static const bool loadFirst = [] {
                const char* v = std::getenv("ZAMIN_EDITOR_STATS");
                return v && std::string_view(v).find("load") != std::string_view::npos;
            }();
            if (!ImGui::CollapsingHeader(title.c_str(), loadFirst || !history.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                return;
        }
        else
            ImGui::SeparatorText(Tr(ICON_FA_HOURGLASS_HALF "  Sahna yuklash vaqti"));
        if (history.empty())
        {
            ImGui::TextDisabled("Hali sahna yuklanmadi (sahna ochilganda yoki o'yinda almashganda yoziladi)");
            return;
        }
        if (m_SceneLoadTimingPick >= (int)history.size())
            m_SceneLoadTimingPick = -1;
        const int picked = m_SceneLoadTimingPick < 0 ? (int)history.size() - 1 : m_SceneLoadTimingPick;
        const SceneLoadTiming& t = history[(size_t)picked];

        // ---- the picked load (the newest by default): what took how long
        ImGui::TextUnformatted(t.SceneName.empty() ? t.Path.c_str() : t.SceneName.c_str());
        ImGui::SameLine();
        std::string how = SceneLoadTiming::SourceName(t.How);
        if (t.How == SceneLoadTiming::Source::Async && t.Additive)
            how += ", qo'shimcha";
        if (t.Binary)
            how += ", binar kesh";
        ImGui::TextDisabled("(%s%s)", how.c_str(), t.Failed ? ", muvaffaqiyatsiz" : "");
        if (!t.Path.empty() && ImGui::IsItemHovered())
            UI::Tooltip(t.Path.c_str());
        {
            const std::string total = std::format("{:.2f} ms", t.TotalMilliseconds());
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(total.c_str()).x));
            ImGui::TextColored(t.Failed ? ImVec4(0.95f, 0.45f, 0.40f, 1.0f) : ImVec4(0.92f, 0.94f, 0.97f, 1.0f), "%s", total.c_str());
        }
        ImGui::PushID("pickedLoad");
        DrawSceneLoadBar(t, ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 0.8f, true);
        ImGui::PopID();
        // The legend: every stage that took time, with its time (wraps to the width).
        {
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            bool first = true;
            for (size_t i = 0; i < SceneLoadTiming::StageCount; i++)
            {
                const double ms = t.Milliseconds[i];
                if (ms < 0.005)
                    continue;
                const auto stage = (SceneLoadTiming::Stage)i;
                const std::string label = std::format("{} {:.2f}", SceneLoadTiming::StageName(stage), ms);
                const float box = ImGui::GetTextLineHeight() * 0.7f;
                const float width = box + 4.0f + ImGui::CalcTextSize(label.c_str()).x;
                if (!first)
                {
                    ImGui::SameLine(0.0f, 12.0f);
                    if (ImGui::GetCursorScreenPos().x + width > right)
                        ImGui::NewLine();
                }
                first = false;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float pad = (ImGui::GetTextLineHeight() - box) * 0.5f;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + pad), ImVec2(p.x + box, p.y + pad + box),
                                                          ImGui::GetColorU32(SceneLoadStageColor(stage)), 2.0f);
                ImGui::Dummy(ImVec2(box, ImGui::GetTextLineHeight()));
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextUnformatted(label.c_str());
                if (ImGui::IsItemHovered())
                    UI::Tooltip(SceneLoadTiming::StageDescription(stage));
            }
        }
        if (t.How == SceneLoadTiming::Source::Async)
            ImGui::TextDisabled("Asosiy oqim: %.2f ms, %d kadr ichida (so'rovdan kirishigacha %.0f ms)", t.MainThreadMilliseconds(), t.Frames,
                                t.WallMilliseconds);
        ImGui::TextDisabled("%zu obyekt%s, eng sekini: %s", t.Entities,
                            t.FileBytes ? std::format(", fayl {:.1f} KB", (double)t.FileBytes / 1024.0).c_str() : "",
                            SceneLoadTiming::StageName(t.GetSlowestStage()));

        // ---- the recent loads, newest first (a click shows one above)
        if (history.size() > 1)
        {
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_ScrollY;
            const float rows = (float)std::min<size_t>(history.size(), 8) + 1.3f;
            if (ImGui::BeginTable("##sceneLoads", 4, flags, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * rows + 4.0f)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Sahna", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("Qanday", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0000.00").x);
                ImGui::TableSetupColumn("Bosqichlar", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableHeadersRow();
                for (int i = (int)history.size() - 1; i >= 0; i--)
                {
                    const SceneLoadTiming& row = history[(size_t)i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const std::string name = row.SceneName.empty() ? row.Path : row.SceneName;
                    if (ImGui::Selectable(name.c_str(), i == picked, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                        m_SceneLoadTimingPick = i == (int)history.size() - 1 ? -1 : i;
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(row.Format().c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s", SceneLoadTiming::SourceName(row.How));
                    ImGui::TableSetColumnIndex(2);
                    if (row.Failed)
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%.2f", row.TotalMilliseconds());
                    else
                        ImGui::Text("%.2f", row.TotalMilliseconds());
                    ImGui::TableSetColumnIndex(3);
                    DrawSceneLoadBar(row, ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() * 0.8f, false);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        if (ImGui::SmallButton(Tr(ICON_FA_BROOM "  Tozalash")))
        {
            SceneLoadProfiler::Clear();
            m_SceneLoadTimingPick = -1;
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Yuklashlar ro'yxatini bo'shatish (har yuklash konsolga ham yoziladi)");
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr(ICON_FA_COPY "  Nusxa olish")))
        {
            std::string text;
            for (const SceneLoadTiming& row : history)
                text += row.Format() + "\n";
            ImGui::SetClipboardText(text.c_str());
        }
        if (ImGui::IsItemHovered())
            UI::Tooltip("Barcha yuklashlarni bosqichlari bilan matn qilib nusxalash");
    }

    void EditorLayer::UI_Modals()
    {
        // ---- new script
        if (m_OpenNewScriptPopup) { ImGui::OpenPopup("###NewScript"); m_OpenNewScriptPopup = false; }
        if (ImGui::BeginPopupModal(Tr("Yangi C++ skript###NewScript"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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
            ImGui::SeparatorText(Tr("Shablon"));
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
            if (ImGui::Button(Tr(ICON_FA_CHECK "  Yaratish"), ImVec2(150, 0)) || (enter && problem.empty()))
            {
                if (CreateScriptClass(m_NewScriptName, m_NewScriptTemplate))
                    ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish"), ImVec2(150, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- build game
        if (m_OpenBuildGamePopup)
        {
            ImGui::OpenPopup("###BuildGame");
            m_OpenBuildGamePopup = false;
            m_BuildAssetPreviewValid = false;
        }
        if (ImGui::BeginPopupModal(Tr("O'yinni build qilish###BuildGame"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(ICON_FA_ROCKET "  O'yin mustaqil papkaga eksport qilinadi (Linux).");
            ImGui::TextDisabled("Tarkibi: ZaminPlayer, libZamin.so, Resources, Assets, skript moduli, .sh launcher, .desktop, ikonkalar");
            ImGui::TextDisabled("Tugagach eksport hisoboti ochiladi (export-report.json / .txt)");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(460);
            ImGui::InputText("##out", &m_BuildGameOutput);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FOLDER_OPEN))
            {
                std::string dir = FileDialogs::SelectFolder(Project::GetProjectDirectory());
                if (!dir.empty()) m_BuildGameOutput = dir;
            }
            ImGui::Checkbox(Tr("Sahnalarni binar keshga ham yozish (.zsceneb)"), &m_BuildGameBinaryScenes);
            if (ImGui::IsItemHovered())
                UI::Tooltip("Har sahna yoniga uning MessagePack nusxasi yoziladi: ~3 marta kichik, joriy formatda (migratsiyasiz). "
                            "O'yin uni .zscene o'rniga o'qiydi (fayl o'zgargan bo'lsa - matnni)");
            // The assets the export takes (Project settings > Build scenes > Export).
            if (auto project = Project::GetActive())
            {
                ExportSettings& exportSettings = project->GetConfig().Export;
                if (ImGui::Checkbox(Tr("Ishlatilmagan assetlarni chiqarib tashlash"), &exportSettings.ExcludeUnusedAssets))
                {
                    m_ProjectSettingsPanel.MarkDirty(); // saved with the project before the export
                    m_BuildAssetPreviewValid = false;
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip("Faqat o'yin ishlatadigan assetlar: build sahnalari va ular orqali kerak bo'lganlar, loyiha fayli, C++ "
                                "skriptlardagi asset yo'llari, Resources/ va til fayllari. \"Doim kiritish\" ro'yxati: Project settings > "
                                "Build sahnalar > Eksport");
                if (ImGui::Checkbox(Tr("Skript modulini Release'da kompilyatsiya qilish (-O2)"), &exportSettings.ReleaseScriptModule))
                    m_ProjectSettingsPanel.MarkDirty();
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("C++ skriptlar eksport uchun alohida build papkasida ({}) Release rejimida qayta kompilyatsiya "
                                            "qilinadi: tezroq va kichikroq. Editorning hot reload moduli o'zgarmaydi. Kompilyatsiya bo'lmasa - "
                                            "editor moduli nusxalanadi (hisobotda ogohlantirish).",
                                            ExportModuleBuild::DefaultBuildDirectory(Project::GetProjectDirectory()).string())
                                    .c_str());
                if (ImGui::Checkbox(Tr("Binarlarni strip qilish (player, engine, skriptlar)"), &exportSettings.StripBinaries))
                    m_ProjectSettingsPanel.MarkDirty();
                if (ImGui::IsItemHovered())
                    UI::Tooltip(BinaryStrip::IsAvailable()
                                    ? "strip --strip-unneeded: debug ma'lumotlari va lokal simvollar olib tashlanadi - fayllar bir necha marta "
                                      "kichrayadi. Crash hisobotida eksport qilingan funksiya nomlari qoladi."
                                    : "strip dasturi topilmadi (binutils paketi) - binarlar o'zgarmaydi");
                if (!m_BuildAssetPreviewValid)
                {
                    m_BuildAssetPreview = ExportAssets::PlanForActiveProject();
                    m_BuildAssetPreviewValid = true;
                }
                const ExportAssetPlan& plan = m_BuildAssetPreview;
                ImGui::TextDisabled(ICON_FA_BOX "  Assetlar: %zu ta fayl (%s)", plan.Included.size(), ExportAssets::FormatSize(plan.IncludedBytes).c_str());
                if (!plan.Excluded.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.32f, 1.0f), "  %s %zu ta (%s)", Tr("chiqarib tashlanadi:"), plan.Excluded.size(),
                                       ExportAssets::FormatSize(plan.ExcludedBytes).c_str());
                    if (ImGui::IsItemHovered())
                    {
                        std::string list;
                        const size_t shown = std::min<size_t>(plan.Excluded.size(), 15);
                        for (size_t i = 0; i < shown; i++)
                            list += std::format("{}{}  ({}, {})", i ? "\n" : "", plan.Excluded[i].Path, ExportAssets::FormatSize(plan.Excluded[i].Size),
                                                plan.Excluded[i].Reason);
                        if (plan.Excluded.size() > shown)
                            list += std::format("\n... va yana {} ta", plan.Excluded.size() - shown);
                        UI::Tooltip(list.c_str());
                    }
                }
            }
            // How the exported game starts (Project settings > Player), with a way to change it.
            if (auto project = Project::GetActive())
            {
                const ProjectConfig& config = project->GetConfig();
                const PlayerSettings& player = config.Player;
                ImGui::SeparatorText(Tr(ICON_FA_WINDOW_MAXIMIZE "  Player"));
                const char* modes[] = { "oynali", "to'liq ekran", "chegarasiz to'liq ekran" };
                ImGui::Text("%s %s%s", player.GetProductName(config.Name).c_str(), player.Version.c_str(),
                            player.CompanyName.empty() ? "" : std::format("  -  {}", player.CompanyName).c_str());
                ImGui::TextDisabled("Oyna: \"%s\", %ux%u, %s, VSync %s, %s%s", player.GetWindowTitle(config.Name).c_str(), player.Width, player.Height,
                                    modes[(int)player.Mode], player.VSync ? "yoqilgan" : "o'chiq",
                                    player.TargetFrameRate > 0 ? std::format("{} FPS", player.TargetFrameRate).c_str() : "FPS cheklanmagan",
                                    player.RunInBackground ? "" : ", fonda to'xtaydi");
                ImGui::TextDisabled("Ikonka: %s  (%s.desktop + %s.png + icons/hicolor 16...256 px)",
                                    player.Icon.empty() ? "yo'q" : player.Icon.generic_string().c_str(), config.Name.c_str(), config.Name.c_str());
                if (ImGui::IsItemHovered())
                    UI::Tooltip(std::format("{}.desktop papka qayerga ko'chirilsa ham ishlaydi: u o'zi turgan papkadagi {}.sh ni ishga "
                                            "tushiradi (%k). Dasturlar menyusiga qo'shish uchun o'sha yerda {} ni ishga tushiring: menyu "
                                            "yozuvi to'liq yo'llar bilan yoziladi, ikonka hamma o'lchamda ~/.local/share/icons ga o'rnatiladi "
                                            "(--uninstall - olib tashlash).",
                                            config.Name, config.Name, ExportFiles::DesktopInstallScriptName)
                                    .c_str());
                const GraphicsSettings& graphics = config.Graphics;
                ImGui::TextDisabled("Grafika: OpenGL %s core%s%s%s", graphics.GetVersionString().c_str(), graphics.WantsDebugContext() ? ", debug" : "",
                                    graphics.MSAASamples > 0 ? std::format(", MSAA {}x", graphics.MSAASamples).c_str() : "",
                                    graphics.SRGBFramebuffer ? ", sRGB" : "");
                if (ImGui::SmallButton(Tr(ICON_FA_GEAR "  Player sozlamalari...")))
                {
                    m_ShowProjectSettings = true;
                    m_ProjectSettingsPanel.Focus(ProjectSettingsPanel::Tab::Player);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(Tr(ICON_FA_MICROCHIP "  Grafika...")))
                {
                    m_ShowProjectSettings = true;
                    m_ProjectSettingsPanel.Focus(ProjectSettingsPanel::Tab::Graphics);
                    ImGui::CloseCurrentPopup();
                }
                // The start scene / build list problems, before the export.
                const std::vector<BuildSceneIssue> issues = Project::CheckBuildScenes(config, Project::GetAssetDirectory());
                for (const BuildSceneIssue& issue : issues)
                    ImGui::TextColored(issue.Error ? ImVec4(0.95f, 0.45f, 0.40f, 1.0f) : ImVec4(0.95f, 0.72f, 0.32f, 1.0f), "%s  %s",
                                       issue.Error ? ICON_FA_CIRCLE_EXCLAMATION : ICON_FA_TRIANGLE_EXCLAMATION, issue.Message.c_str());
            }
            ImGui::Spacing();
            if (ImGui::Button(Tr(ICON_FA_HAMMER "  Build"), ImVec2(150, 0)))
            {
                BuildGame(m_BuildGameOutput);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Bekor qilish"), ImVec2(150, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- the export's report (opens when an export ends)
        UI_ExportReportWindow();

        // ---- about
        if (m_ShowAbout) { ImGui::OpenPopup("###About"); m_ShowAbout = false; }
        if (ImGui::BeginPopupModal(Tr("Zamin haqida###About"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushFont(ImGuiLayer::GetTitleFont());
            const BuildInfo& build = BuildInfo::Get();
            ImGui::TextColored(UI::AccentColor(), ICON_FA_EARTH_ASIA "  Zamin Engine %s", build.EngineVersion.c_str());
            ImGui::PopFont();
            ImGui::TextUnformatted(Tr("C++20 da yozilgan o'yin dvigateli. O'yinlar ham C++ da yoziladi."));
            ImGui::Spacing();
            // This build: the commit (with its date, and a mark for uncommitted changes), the build's date, type, compiler.
            if (!build.Commit.empty())
                ImGui::Text("%s %s%s%s", Tr("Kommit:"), build.Commit.c_str(), build.CommitDate.empty() ? "" : (" (" + build.CommitDate + ")").c_str(),
                            build.Dirty ? Tr("  [o'zgarishlar bilan]") : "");
            else
                ImGui::TextUnformatted(Tr("Kommit: noma'lum (git'siz build)"));
            ImGui::Text("%s %s, %s%s, %s", Tr("Build:"), build.BuildDate.c_str(), build.BuildType.c_str(), build.Shipping ? " Shipping" : "", build.Compiler.c_str());
            if (ImGui::SmallButton(Tr(ICON_FA_COPY "  Versiyani nusxalash")))
                ImGui::SetClipboardText(VersionInfo::ForActiveProject().Format("Zamin Editor").c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Renderer: OpenGL 4.1 (PBR, soyalar, 2D batch)");
            ImGui::TextDisabled("ECS: EnTT  |  Fizika: Box2D  |  Audio: miniaudio  |  UI: Dear ImGui");
            ImGui::TextDisabled("Platforma: Linux (Windows, macOS, Android, iOS - rejada)");
            ImGui::Spacing();
            if (ImGui::Button(Tr("Yopish"), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
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
        if (Project::GetActive() && ImGui::Button(Tr(ICON_FA_XMARK "  Yopish"), ImVec2(120, 0)))
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
        ImGui::InputTextWithHint("##pname", Tr("Loyiha nomi"), &m_NewProjectName);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2 * ImGui::GetFrameHeight() - 140);
        ImGui::InputTextWithHint("##ploc", Tr("Joylashuv"), &m_NewProjectLocation);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##browse"))
        {
            std::string dir = FileDialogs::SelectFolder(m_NewProjectLocation);
            if (!dir.empty()) m_NewProjectLocation = dir;
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, UI::AccentDarkColor(1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::AccentMidColor());
        bool canCreate = !m_NewProjectName.empty() && !m_NewProjectLocation.empty();
        ImGui::BeginDisabled(!canCreate);
        if (ImGui::Button(Tr(ICON_FA_PLUS "  Yaratish"), ImVec2(120, 0)))
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
        ImGui::InputTextWithHint("##openpath", Tr(".zproject fayl yoki papka yo'li"), &m_OpenProjectPath);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##browse2"))
        {
            std::string file = FileDialogs::OpenFile("Zamin Project (*.zproject) | *.zproject");
            if (!file.empty()) m_OpenProjectPath = file;
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(ICON_FA_CHECK "  Ochish"), ImVec2(120, 0)) && !m_OpenProjectPath.empty())
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
            d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hovered ? UI::AccentU32(0.157f) : IM_COL32(255, 255, 255, 8), 6.0f);
            d->AddText(ImGuiLayer::GetBoldFont(), ImGui::GetFontSize(), ImVec2(p.x + 14, p.y + 6), ImGui::GetColorU32(ImGuiCol_Text),
                       (std::string(icon) + "   " + path.stem().string()).c_str());
            d->AddText(ImVec2(p.x + 14 + ImGui::GetFontSize() * 1.6f, p.y + h * 0.5f + 1), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                       path.parent_path().string().c_str());
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 2));
            return clicked;
        };

        ImGui::Dummy(ImVec2(0, 14));
        // The scenes opened last (any project): one click opens the project and the scene.
        if (UI_LauncherRecentScenes())
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndChild();
            ImGui::EndPopup();
            return;
        }
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
        if (m_RecentProjects.IsEmpty())
            ImGui::TextDisabled("Hali loyiha ochilmagan");
        for (const RecentFileList::Entry& e : m_RecentProjects.GetEntries())
        {
            const fs::path p(e.Path);
            if (projectCard(p, ICON_FA_FOLDER)) { if (OpenProject(p)) ImGui::CloseCurrentPopup(); break; }
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

}
