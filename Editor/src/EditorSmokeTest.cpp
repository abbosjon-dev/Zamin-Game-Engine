// The editor's smoke test: ZAMIN_EDITOR_OPEN_ALL_PANELS=1 (Tests/editor_smoke.sh, CI). Every window of the
// editor is opened in turn, then all of them together, then every page of the large ones (project settings
// categories, preferences pages, scene statistics tabs, scene settings sections), every main menu and its
// sub menus, the viewport toolbar's popups, the editor's prompts (about, launcher, new script, build game,
// save layout, the content browser's create / rename / delete), every entity of the scene selected, every
// component's inspector section (a temporary entity gets each component in turn), the asset inspector on a
// file of each asset type, the sprite editor, prefab mode, the built-in layouts, Play / Pause / Stop and
// Simulate - each for a few frames.
//
// What is counted (per step, in the log and in the summary): errors written to the log meanwhile - the
// OpenGL debug output (ZAMIN_GL_DEBUG=1 asks for a debug context), failed ImGui checks (IM_ASSERT, see
// Zamin/ImGui/ImGuiConfig.h; a Debug build stops on the first) and the editor's own errors - and OpenGL
// error flags (glGetError, every frame). At the end the editor closes itself: exit code 0 when nothing was
// found, 1 otherwise (also when the run ended before the last step, e.g. at ZAMIN_MAX_FRAMES).
// ZAMIN_EDITOR_SMOKE_SHOTS=<folder>: some steps' last frame as PNG files there.
//
// Nothing of the run is kept: the window layout is not written (imgui.ini), the windows' visibility is
// put back after every step, the temporary entity is removed and nothing is saved.

#include "EditorLayer.h"
#include "EditorSmokeTest.h"

#include <Zamin/Editor/ProjectSettingsIndex.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <type_traits>

namespace Zamin {

    namespace fs = std::filesystem;

    namespace {

        // Every open popup and menu (modal or not) closed from outside them (between frames' windows).
        void CloseAllPopups()
        {
            if (ImGuiContext* g = ImGui::GetCurrentContext(); g && g->OpenPopupStack.Size > 0)
                ImGui::ClosePopupToLevel(0, false);
        }

        template<typename... T, typename Fn>
        void ForEachComponentType(ComponentGroup<T...>, Fn&& fn)
        {
            (fn(std::type_identity<T>{}), ...);
        }

        // "Loyiha sozlamalari: Fizika" -> "loyiha-sozlamalari-fizika" (screenshot file names).
        std::string Slug(std::string_view name)
        {
            std::string slug;
            for (char c : name)
            {
                if (std::isalnum((unsigned char)c))
                    slug += (char)std::tolower((unsigned char)c);
                else if (!slug.empty() && slug.back() != '-')
                    slug += '-';
            }
            while (!slug.empty() && slug.back() == '-')
                slug.pop_back();
            return slug;
        }

        constexpr size_t MaxReportedFailures = 40;
        constexpr size_t MaxSelectedEntities = 80;
    }

    void EditorLayer::StartSmokeTest()
    {
        m_SmokeTest = std::make_unique<EditorSmokeTest>();
        EditorSmokeTest& t = *m_SmokeTest;
        if (const char* dir = std::getenv("ZAMIN_EDITOR_SMOKE_SHOTS"); dir && *dir)
        {
            t.ShotDir = dir;
            std::error_code ec;
            fs::create_directories(t.ShotDir, ec);
        }
        // Nothing of the run is kept: the docking the steps change is not written to imgui.ini.
        ImGui::GetIO().IniFilename = nullptr;
        for (bool* flag : { &m_ShowHierarchy, &m_ShowInspector, &m_ShowContentBrowser, &m_ShowConsole, &m_ShowSceneSettings, &m_ShowScenes,
                            &m_ShowProjectSettings, &m_ShowStats, &m_ShowSceneStatistics, &m_ShowBuildOutput, &m_ShowProfiler,
                            &m_ShowAudioMixer, &m_ShowLocalization, &m_ShowAnimation, &m_ShowAnimatorGraph, &m_ShowUnusedAssets,
                            &m_ShowSpriteEditor, &m_ShowTilePalette, &m_ShowUndoHistory, &m_ShowPreferences, &m_ShowShortcuts,
                            &m_ShowLayouts, &m_ShowImGuiDemo, &m_ShowPrefabCheck, &m_ShowExportReport, &m_ShowLoadReport, &m_ShowLauncher })
            t.SavedFlags.emplace_back(flag, *flag);
        for (const SceneViewSettings& view : m_ViewSettings.SceneViews)
            t.SavedViews.push_back(view.Open);
        t.Steps = BuildSmokeSteps();
        t.LogSeen = Log::GetTotalWritten();
        t.ImGuiAssertsAtStart = GetImGuiAssertCount();
        t.Start = std::chrono::steady_clock::now();
        t.Running = true;
        // Whatever the GPU reported before the test is not the test's.
        RenderCommand::PopErrors();
        ZM_INFO("Editor smoke testi: {} qadam (har biri bir necha kadr), loyiha '{}'", t.Steps.size(),
                Project::GetActive() ? Project::GetActive()->GetConfig().Name : std::string("-"));
    }

    void EditorLayer::RestoreSmokeState()
    {
        EditorSmokeTest& t = *m_SmokeTest;
        for (auto& [flag, value] : t.SavedFlags)
            *flag = value;
        for (int i = 0; i < SceneViewSettings::Count && i < (int)t.SavedViews.size(); i++)
            if (m_ViewSettings.SceneViews[i].Open != t.SavedViews[i])
                OpenSceneView(i, t.SavedViews[i]);
        m_OpenMenu.clear();
        m_OpenViewModeMenu = m_OpenDebugDrawMenu = m_OpenGizmosMenu = m_OpenTransformGizmoMenu = m_OpenStatsOverlayMenu = false;
        m_OpenLabelsMenu = m_OpenGameViewMenu = m_OpenSaveLayout = m_OpenNewScriptPopup = m_OpenBuildGamePopup = m_ShowAbout = false;
        m_OpenCameraSettings = -1;
        m_ContentBrowserPanel.CancelPrompts();
        CloseAllPopups();
    }

    void EditorLayer::RecordSmokeFailure(std::string message)
    {
        EditorSmokeTest& t = *m_SmokeTest;
        t.Errors++;
        std::string line = std::format("{}: {}", t.LastStep.empty() ? std::string("(boshlanish)") : t.LastStep, std::move(message));
        // (A warning: the test's own lines are not errors it would count again.)
        ZM_WARN("Smoke xatosi - {}", line);
        if (t.Failures.size() < MaxReportedFailures)
            t.Failures.push_back(std::move(line));
    }

    void EditorLayer::CollectSmokeErrors()
    {
        EditorSmokeTest& t = *m_SmokeTest;
        if (Log::GetTotalWritten() != t.LogSeen)
        {
            uint64_t first = 0;
            const std::vector<LogMessage> messages = Log::GetMessages(&first);
            std::vector<std::string> found;
            for (size_t i = 0; i < messages.size(); i++)
                if (first + i >= t.LogSeen && (messages[i].Level == LogLevel::Error || messages[i].Level == LogLevel::Critical))
                    found.push_back(messages[i].Text);
            t.LogSeen = first + messages.size();
            for (std::string& text : found)
                RecordSmokeFailure(std::move(text));
        }
        std::string names;
        if (const uint32_t count = RenderCommand::PopErrors(&names))
        {
            t.GLErrors += count;
            RecordSmokeFailure(std::format("OpenGL xato bayrog'i: {}", names));
        }
    }

    void EditorLayer::UpdateSmokeTest()
    {
        if (!m_SmokeTest || !m_SmokeTest->Running)
            return;
        EditorSmokeTest& t = *m_SmokeTest;
        // The previous frame's errors (its UI, its scene update and rendering) belong to the step it drew.
        CollectSmokeErrors();
        if (t.Frame >= 0 && t.Frame >= t.Steps[t.Current].Frames)
        {
            if (t.Steps[t.Current].Close)
                t.Steps[t.Current].Close();
            RestoreSmokeState();
            t.Current++;
            t.Frame = -1;
        }
        if (t.Current >= t.Steps.size())
        {
            FinishSmokeTest(true);
            return;
        }
        EditorSmokeStep& step = t.Steps[t.Current];
        if (t.Frame < 0)
        {
            ZM_INFO("Smoke [{}/{}] {}", t.Current + 1, t.Steps.size(), step.Name);
            if (step.Open)
                step.Open();
            t.Frame = 0;
        }
        t.Frame++;
        t.Frames++;
        t.LastStep = step.Name;
        if (step.Screenshot && t.Frame == step.Frames && !t.ShotDir.empty())
            Application::Get().RequestScreenshot((fs::path(t.ShotDir) / std::format("{:03}-{}.png", t.Current + 1, Slug(step.Name))).string());
    }

    void EditorLayer::FinishSmokeTest(bool completed)
    {
        EditorSmokeTest& t = *m_SmokeTest;
        CollectSmokeErrors();
        t.Running = false;
        if (completed)
            RestoreSmokeState();
        else
        {
            t.LastStep = t.Current < t.Steps.size() ? t.Steps[t.Current].Name : std::string("(oxiri)");
            RecordSmokeFailure(std::format("test oxirigacha yetmadi: {} qadamdan {} tasi (dastur oldinroq yopildi - ZAMIN_MAX_FRAMES?)",
                                           t.Steps.size(), t.Current));
        }
        // The temporary entity, if a step was left half way.
        if (t.TempEntity && m_ActiveScene)
            if (Entity e = m_ActiveScene->GetEntityByUUID(UUID(t.TempEntity)))
                m_ActiveScene->DestroyEntity(e);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.Start).count();
        const unsigned long long asserts = GetImGuiAssertCount() - t.ImGuiAssertsAtStart;
        const std::string summary = std::format("{}/{} qadam, {} kadr, {:.1f} s: {} xato (OpenGL bayroqlari: {}, ImGui tekshiruvlari: {})",
                                                std::min(t.Current, t.Steps.size()), t.Steps.size(), t.Frames, seconds, t.Errors, t.GLErrors, asserts);
        if (t.Errors == 0)
            ZM_INFO("Editor smoke testi o'tdi: {}", summary);
        else
        {
            for (const std::string& failure : t.Failures)
                ZM_WARN("  - {}", failure);
            ZM_ERROR("Editor smoke testi o'tmadi: {}", summary);
        }
        Application::Get().SetExitCode(t.Errors == 0 ? 0 : 1);
        if (completed)
            Application::Get().Close();
    }

    void EditorLayer::OnRunEnded()
    {
        // Closed before the last step (ZAMIN_MAX_FRAMES, the window closed): the run failed.
        if (m_SmokeTest && m_SmokeTest->Running)
            FinishSmokeTest(false);
    }

    std::vector<EditorSmokeStep> EditorLayer::BuildSmokeSteps()
    {
        std::vector<EditorSmokeStep> steps;
        auto add = [&](std::string name, std::function<void()> open, std::function<void()> close = {}, int frames = 3, bool shot = false) {
            steps.push_back({ std::move(name), std::move(open), std::move(close), frames, shot });
        };
        const bool hasProject = Project::GetActive() != nullptr;

        // ---- every window on its own (the others as they were)
        struct WindowEntry { const char* Name; bool* Flag; };
        const WindowEntry windows[] = {
            { "Ierarxiya", &m_ShowHierarchy }, { "Inspektor", &m_ShowInspector }, { "Kontent", &m_ShowContentBrowser },
            { "Konsol", &m_ShowConsole }, { "Sahna sozlamalari", &m_ShowSceneSettings }, { "Sahnalar", &m_ShowScenes },
            { "Loyiha sozlamalari", &m_ShowProjectSettings }, { "Statistika", &m_ShowStats }, { "Sahna statistikasi", &m_ShowSceneStatistics },
            { "Kompilyatsiya natijalari", &m_ShowBuildOutput }, { "Profiler", &m_ShowProfiler }, { "Audio Mixer", &m_ShowAudioMixer },
            { "Lokalizatsiya", &m_ShowLocalization }, { "Animatsiya", &m_ShowAnimation }, { "Animator grafi", &m_ShowAnimatorGraph },
            { "Ishlatilmagan assetlar", &m_ShowUnusedAssets }, { "Sprite muharriri", &m_ShowSpriteEditor }, { "Plitka palitrasi", &m_ShowTilePalette },
            { "Undo tarixi", &m_ShowUndoHistory }, { "Sozlamalar", &m_ShowPreferences }, { "Klaviatura yorliqlari", &m_ShowShortcuts },
            { "Joylashuvlar", &m_ShowLayouts }, { "ImGui demo", &m_ShowImGuiDemo },
        };
        for (const WindowEntry& w : windows)
        {
            bool* flag = w.Flag;
            add(std::format("Oyna: {}", w.Name), [flag]() { *flag = true; }, {}, 4);
        }
        for (int i = 0; i < SceneViewSettings::Count; i++)
            add(std::format("Oyna: Ko'rinish {}", i + 2), [this, i]() { OpenSceneView(i, true); }, {}, 4);
        if (hasProject)
        {
            add("Oyna: Prefab'lar tekshiruvi", [this]() { RunPrefabCheck(); });
            const fs::path exportDir = Project::GetProjectDirectory() / "Build" / "Game";
            std::error_code ec;
            if (fs::is_directory(exportDir, ec))
                add("Oyna: Eksport hisoboti", [this, exportDir]() { OpenExportReport(exportDir); });
        }
        if (m_LoadReport.HasIssues())
            add("Oyna: Sahna yuklash hisoboti", [this]() { m_ShowLoadReport = true; });

        // ---- all of them at once (docked together, the extra views too)
        auto openAll = [this, windows]() {
            for (const WindowEntry& w : windows)
                if (w.Flag != &m_ShowImGuiDemo)
                    *w.Flag = true;
            for (int i = 0; i < SceneViewSettings::Count; i++)
                OpenSceneView(i, true);
        };
        add("Barcha oynalar birga", openAll, {}, 20, true);

        // ---- the pages of the large windows
        const auto& categories = ProjectSettingsIndex::GetCategories();
        for (int c = 0; c < (int)ProjectSettingsPanel::Category::Count; c++)
        {
            const std::string title = c < (int)categories.size() ? categories[c].Title : std::to_string(c);
            add(std::format("Loyiha sozlamalari: {}", title), [this, c]() {
                m_ShowProjectSettings = true;
                m_ProjectSettingsPanel.Focus((ProjectSettingsPanel::Category)c);
            }, {}, 3, c == 0);
        }
        for (int page = 0; page < 8; page++)
            add(std::format("Sozlamalar: {}-sahifa", page + 1), [this, page]() {
                m_ShowPreferences = true;
                m_PreferencesPage = page;
            });
        static const char* s_StatsTabs[] = { "komponentlar", "skriptlar", "prefablar", "assetlar" };
        for (int tab = 0; tab < 4; tab++)
            add(std::format("Sahna statistikasi: {}", s_StatsTabs[tab]), [this, tab]() {
                m_ShowSceneStatistics = true;
                m_SceneStatsTab = tab;
            });
        for (const char* section : { "sky", "render", "shadows", "post", "bloom", "lens", "exposure" })
            add(std::format("Sahna sozlamalari: {}", section), [this, section]() {
                m_ShowSceneSettings = m_FocusSceneSettings = true;
                m_SceneSettingsScrollTo = section;
            });

        // ---- the main menus and their sub menus
        for (const char* menu : { "Fayl", "Fayl/recent", "Tahrirlash", "Ko'rinish", "Obyekt", "Skriptlar", "Oyna", "Oyna/views", "Oyna/layout", "Yordam" })
            add(std::format("Menyu: {}", menu), [this, menu]() { m_OpenMenu = menu; });

        // ---- the viewport toolbar's popups
        struct PopupEntry { const char* Name; bool* Flag; };
        const PopupEntry popups[] = {
            { "ko'rinish rejimi", &m_OpenViewModeMenu }, { "debug chizish", &m_OpenDebugDrawMenu }, { "gizmolar", &m_OpenGizmosMenu },
            { "gizmo o'lchami", &m_OpenTransformGizmoMenu }, { "statistika overlay'i", &m_OpenStatsOverlayMenu },
            { "obyekt nomlari", &m_OpenLabelsMenu }, { "o'yin oynasi o'lchami", &m_OpenGameViewMenu },
        };
        for (const PopupEntry& p : popups)
        {
            bool* flag = p.Flag;
            add(std::format("Viewport: {}", p.Name), [flag]() { *flag = true; });
        }
        add("Viewport: kamera sozlamalari", [this]() { m_OpenCameraSettings = 0; });
        add("Ko'rinish 2: kamera sozlamalari", [this]() {
            OpenSceneView(0, true);
            m_OpenCameraSettings = 1;
        }, {}, 4);

        // ---- the editor's prompts
        add("Oyna: Zamin haqida", [this]() { m_ShowAbout = true; }, {}, 3, true);
        add("Oyna: Loyihalar (launcher)", [this]() { m_ShowLauncher = true; });
        if (hasProject)
        {
            add("Oyna: Yangi C++ skript", [this]() {
                m_NewScriptName = "SmokeScript";
                m_OpenNewScriptPopup = true;
            });
            add("Oyna: O'yinni build qilish", [this]() {
                m_BuildGameOutput = (Project::GetProjectDirectory() / "Build" / "Game").string();
                m_OpenBuildGamePopup = true;
            }, {}, 4, true);
        }
        add("Oyna: Joylashuvni saqlash", [this]() { m_OpenSaveLayout = true; });
        if (hasProject)
        {
            add("Kontent: yangi papka", [this]() { m_ContentBrowserPanel.RequestNewItem(0); });
            add("Kontent: yangi sahna", [this]() { m_ContentBrowserPanel.RequestNewItem(1); });
        }

        // ---- the asset inspector on a file of each type, the sprite editor, prefab mode
        std::map<AssetType, fs::path> samples;
        if (hasProject)
        {
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(Project::GetAssetDirectory(), fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (!it->is_regular_file(ec) || it->path().filename().string().starts_with("."))
                    continue;
                const AssetType type = AssetTypes::FromPath(it->path());
                if (type != AssetType::Other && type != AssetType::Folder && type != AssetType::Project)
                    samples.emplace(type, it->path()); // the first of each type (directory order)
            }
        }
        for (const auto& [type, path] : samples)
            add(std::format("Asset inspektori: {} ({})", AssetTypes::GetName(type), path.filename().string()), [this, path]() {
                m_ContentBrowserPanel.Reveal({ path });
                m_AssetInspectorPanel.SetAsset(path);
                m_SceneHierarchyPanel.AcknowledgeSelection();
            }, [this]() { m_AssetInspectorPanel.Clear(); });
        if (auto any = samples.begin(); any != samples.end())
        {
            const fs::path file = any->second;
            add(std::format("Kontent: nomini o'zgartirish ({})", file.filename().string()), [this, file]() { m_ContentBrowserPanel.RequestRename(file); });
            add(std::format("Kontent: o'chirishni tasdiqlash ({})", file.filename().string()),
                [this, file]() { m_ContentBrowserPanel.RequestDelete({ file }); });
        }
        if (auto texture = samples.find(AssetType::Texture); texture != samples.end())
        {
            const std::string relative = Project::GetAssetRelativePath(texture->second);
            add("Sprite muharriri (rasm bilan)", [this, relative]() {
                m_SpriteEditorPanel.Open(relative);
                m_ShowSpriteEditor = true;
            }, {}, 4);
        }
        if (auto prefab = samples.find(AssetType::Prefab); prefab != samples.end())
        {
            const std::string relative = Project::GetAssetRelativePath(prefab->second);
            add(std::format("Prefab rejimi ({})", prefab->second.filename().string()), [this, relative]() { RequestPrefabStage(relative); },
                [this]() { RequestLeavePrefabStages(0); }, 8);
            add("Prefab rejimidan chiqish", {}, {}, 4);
        }

        // ---- every entity selected (the inspector, the animation windows on it), then all of them
        std::vector<Entity> entities = m_ActiveScene->GetEntitiesInHierarchyOrder();
        if (entities.size() > MaxSelectedEntities)
            entities.resize(MaxSelectedEntities);
        for (Entity entity : entities)
        {
            const UUID id = entity.GetUUID();
            add(std::format("Tanlov: '{}'", entity.GetName()), [this, id]() {
                m_ShowAnimation = m_ShowAnimatorGraph = true;
                if (Entity e = m_ActiveScene->GetEntityByUUID(id))
                    m_SceneHierarchyPanel.SetSelectedEntity(e);
            }, [this]() { m_SceneHierarchyPanel.SetSelectedEntity({}); }, 2);
        }
        add("Tanlov: hammasi", [this]() { m_SceneHierarchyPanel.GetSelection().Set(m_ActiveScene->GetEntitiesInHierarchyOrder()); },
            [this]() { m_SceneHierarchyPanel.SetSelectedEntity({}); }, 4);

        // ---- every component's inspector section: a temporary entity gets each in turn
        add("Komponentlar: vaqtinchalik obyekt", [this]() {
            Entity e = m_ActiveScene->CreateEntity("ZaminSmokeTest");
            m_SmokeTest->TempEntity = e.GetUUID();
            m_SceneHierarchyPanel.SetSelectedEntity(e);
        }, {}, 2);
        ForEachComponentType(AllComponents{}, [&](auto type) {
            using T = typename decltype(type)::type;
            if constexpr (!std::is_same_v<T, RelationshipComponent> && !std::is_same_v<T, EditorMetadataComponent> &&
                          !std::is_same_v<T, PrefabLinkComponent>)
            {
                auto added = std::make_shared<bool>(false);
                add(std::format("Komponent: {}", ComponentKey<T>()), [this, added]() {
                    Entity e = m_ActiveScene->GetEntityByUUID(UUID(m_SmokeTest->TempEntity));
                    if (!e)
                        return;
                    if (!e.HasComponent<T>())
                    {
                        e.AddComponent<T>();
                        *added = true;
                    }
                    m_SceneHierarchyPanel.SetSelectedEntity(e);
                }, [this, added]() {
                    Entity e = m_ActiveScene->GetEntityByUUID(UUID(m_SmokeTest->TempEntity));
                    if (e && *added && e.HasComponent<T>())
                        e.RemoveComponent<T>();
                }, 2);
            }
        });
        add("Komponentlar: obyekt olib tashlandi", [this]() {
            m_SceneHierarchyPanel.SetSelectedEntity({});
            if (Entity e = m_ActiveScene->GetEntityByUUID(UUID(m_SmokeTest->TempEntity)))
                m_ActiveScene->DestroyEntity(e);
            m_SmokeTest->TempEntity = 0;
        }, {}, 2);

        // ---- the built-in layouts (back to the default one at the end)
        static const std::pair<const char*, BuiltInLayout> s_Layouts[] = {
            { "2D", BuiltInLayout::Mode2D }, { "Animatsiya", BuiltInLayout::Animation }, { "4 ko'rinish", BuiltInLayout::FourViews },
            { "Standart", BuiltInLayout::Default },
        };
        for (const auto& [name, layout] : s_Layouts)
        {
            const BuiltInLayout which = layout;
            add(std::format("Joylashuv: {}", name), [this, which]() { RequestBuiltInLayout(which); }, {}, 6, which == BuiltInLayout::FourViews);
        }

        // ---- the game: play with every window open, pause, stop; simulate
        add("O'yin: Play (barcha oynalar ochiq)", [this, openAll]() {
            openAll();
            OnScenePlay();
        }, {}, 40, true);
        add("O'yin: pauza", [this]() { OnScenePause(); }, {}, 4);
        add("O'yin: davom", [this]() { OnScenePause(); }, [this]() { OnSceneStop(); }, 6);
        add("Simulyatsiya", [this]() { OnSceneSimulate(); }, [this]() { OnSceneStop(); }, 20);
        add("Tahrirlash rejimi (oxiri)", {}, {}, 4);
        return steps;
    }

}
