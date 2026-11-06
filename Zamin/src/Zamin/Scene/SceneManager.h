#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Core/Timestep.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Scene/SceneSerializer.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Zamin {

    class Scene;
    class Entity;
    class EventBus;
    class SceneLoadJob;
    struct FinishedSound;

    // How SceneManager::LoadScene adds a scene: Single - every loaded scene is unloaded and the new
    // one becomes the active scene (the persistent DontDestroyOnLoad scene stays); Additive - it is
    // loaded next to the others (the active scene stays the active one).
    enum class LoadSceneMode { Single = 0, Additive };

    // A scene of a scene event (SceneManager): the scripts' OnSceneLoaded / OnSceneUnloaded /
    // OnActiveSceneChanged and the event bus's "SceneLoaded" / "SceneUnloaded" (payload: this) and
    // "ActiveSceneChanged" (payload: ActiveSceneChange).
    struct SceneEventInfo
    {
        // The scene: an unloaded one is still there during the call (stopped: its scripts are gone)
        // and freed afterwards - keep its name / path, not the pointer. Null: no scene (the active
        // scene before the first one).
        Scene* SceneRef = nullptr;
        std::string Name;
        std::string Path;                          // relative to the assets ("" without a file)
        int BuildIndex = -1;                       // in the build list (Project settings), -1: not in it
        LoadSceneMode Mode = LoadSceneMode::Single; // SceneLoaded: how it came
        bool IsValid() const { return SceneRef != nullptr; }
    };
    struct ActiveSceneChange
    {
        SceneEventInfo Previous, Current;
    };

    // A fade between scenes: the screen fades to Color over FadeOut seconds (the game goes on under
    // it), the scene switches while it is covered, then the cover fades away over FadeIn seconds.
    // Real time: a paused game fades too.
    struct SceneTransition
    {
        float FadeOut = 0.0f;
        float FadeIn = 0.0f;
        glm::vec4 Color{ 0.0f, 0.0f, 0.0f, 1.0f };

        bool IsNone() const { return FadeOut <= 0.0f && FadeIn <= 0.0f; }
        // fadeIn < 0: as long as fadeOut.
        static SceneTransition Fade(float fadeOut = 0.5f, float fadeIn = -1.0f, const glm::vec4& color = { 0.0f, 0.0f, 0.0f, 1.0f })
        {
            SceneTransition t;
            t.FadeOut = std::max(0.0f, fadeOut);
            t.FadeIn = fadeIn < 0.0f ? t.FadeOut : fadeIn;
            t.Color = color;
            return t;
        }
    };

    // A scene loading in the background (SceneManager::LoadSceneAsync). The file is read and parsed
    // and its textures are decoded on a worker thread; the entities are made and the GPU assets
    // (textures, meshes, fonts) are uploaded on the main thread a slice per frame. A loading screen
    // shows GetProgress(); with SetAllowSceneActivation(false) the loaded scene waits (progress 0.9)
    // until it is allowed in - the switch then happens at the end of a frame (after the fade out of
    // its transition).
    class AsyncOperation
    {
    public:
        enum class Stage { Reading = 0, Decoding, Building, Resolving, Ready, Done, Failed };

        AsyncOperation(std::filesystem::path path, LoadSceneMode mode, const SceneTransition& transition);
        ~AsyncOperation();
        AsyncOperation(const AsyncOperation&) = delete;
        AsyncOperation& operator=(const AsyncOperation&) = delete;

        // 0..1: read and parsed 0.25, textures decoded 0.5, entities made 0.7, assets uploaded 0.9
        // (Ready: waiting for activation), 1 once the scene is in.
        float GetProgress() const;
        Stage GetStage() const { return m_Stage; }
        // In (activated) or failed: nothing more happens.
        bool IsDone() const { return m_Stage == Stage::Done || m_Stage == Stage::Failed; }
        // Loaded and waiting for activation (or for its fade to cover the screen).
        bool IsReady() const { return m_Stage == Stage::Ready; }
        bool HasFailed() const { return m_Stage == Stage::Failed; }
        const std::string& GetError() const { return m_Error; }
        // What the load found (see SceneLoadReport): the failure, or what the loaded scene is without.
        const SceneLoadReport& GetReport() const { return m_Report; }

        void SetAllowSceneActivation(bool allow) { m_AllowActivation = allow; }
        bool GetAllowSceneActivation() const { return m_AllowActivation; }

        const std::filesystem::path& GetPath() const { return m_Path; }   // relative to the assets
        LoadSceneMode GetMode() const { return m_Mode; }
        const SceneTransition& GetTransition() const { return m_Transition; }
        // The scene being made (empty until Building), also before it is activated.
        const Ref<Scene>& GetScene() const { return m_Scene; }
        // Images decoded ahead on the worker thread / entities made / entities resolved.
        size_t GetDecodedImageCount() const { return m_ImagesDecoded; }
        size_t GetImageCount() const { return m_Images.size(); }
        // How long it has taken so far, stage by stage (SceneLoadTiming; complete once it is done -
        // the scene load profiler then has it too).
        const SceneLoadTiming& GetTiming() const { return m_Timing; }

    private:
        friend class SceneManager;
        void StartReading(const std::filesystem::path& file);
        void JoinWorker();

        std::filesystem::path m_Path, m_File;
        LoadSceneMode m_Mode = LoadSceneMode::Single;
        SceneTransition m_Transition;
        bool m_AllowActivation = true;
        Stage m_Stage = Stage::Reading;
        std::string m_Error;
        SceneLoadReport m_Report;
        Ref<Scene> m_Scene;
        std::unique_ptr<SceneLoadJob> m_Job;
        // Worker thread (reading / decoding): it sets m_WorkerDone when its part is finished.
        std::thread m_Worker;
        std::atomic<bool> m_WorkerDone = false;
        std::atomic<bool> m_Cancel = false;
        bool m_WorkerFailed = false;
        std::vector<std::string> m_Images;          // asset-relative image paths the scene names
        std::vector<std::filesystem::path> m_Decode; // the files decoded ahead (not loaded yet)
        std::atomic<size_t> m_ImagesDecoded = 0;
        std::vector<UUID> m_ResolveOrder;
        size_t m_Resolved = 0;
        float m_BuildProgress = 0.0f;
        SceneLoadTiming m_Timing;
        std::chrono::steady_clock::time_point m_Started = std::chrono::steady_clock::now();
        double m_DecodeMilliseconds = 0.0; // written by the worker before it sets m_WorkerDone
    };

    // The scenes of a running game (owned by the host: the editor's play mode, the player). One is the
    // active scene; others can be loaded next to it (Additive): each has its own entities, scripts and
    // physics worlds (bodies of different scenes do not touch, raycasts see their own scene), and they
    // run in the same frame and are drawn together through one camera - the active scene's primary
    // camera (else the first loaded scene that has one), the others over its picture without a sky.
    // Entities moved to the DontDestroyOnLoad scene survive every Single load. Every scene shares one
    // event bus and one game clock (time scale, pause). Scene switches requested during a frame happen
    // at its end (ProcessRequests), outside every script callback.
    //
    // Scene events, at the end of the frame the change happened in (SetActiveScene: at once), to every
    // script of every loaded scene and the persistent one (ScriptableEntity::OnSceneLoaded ...) and
    // then on the event bus: a Single load gives "SceneUnloaded" for each scene that went, then
    // "ActiveSceneChanged", then "SceneLoaded" (the new scene's own scripts hear it too: after their
    // OnCreate, before their OnStart); an Additive load only "SceneLoaded"; UnloadScene "SceneUnloaded"
    // (and "ActiveSceneChanged" when it was the active one). Start gives "ActiveSceneChanged" (from no
    // scene) and "SceneLoaded"; Stop gives none.
    class SceneManager
    {
    public:
        SceneManager();
        ~SceneManager();
        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;

        static constexpr const char* DontDestroyOnLoadSceneName = "DontDestroyOnLoad";

        // ---- the run (the host)
        // Starts the game with `scene` (its assets resolved) as the active scene; `path`: its file
        // (relative to the assets) for GetScenePath / UnloadScene by path. `prepared`: what the host
        // already timed of it (loading / copying it, its assets) - the start adds the physics, the
        // scripts and the audio and records it (SceneLoadProfiler).
        void Start(const Ref<Scene>& scene, const std::filesystem::path& path = {}, const SceneLoadTiming* prepared = nullptr);
        // Loads the file (relative to the assets, or absolute) and starts with it; false with the
        // reason in `error` when it cannot be read.
        bool Start(const std::filesystem::path& path, std::string* error = nullptr);
        // Every scene stops (OnRuntimeStop), the persistent one too; loads in progress are dropped.
        void Stop();
        bool IsRunning() const { return m_Running; }

        // One frame: the async loads take their main-thread slice, the fade advances, every scene's
        // frame runs (the persistent scene first, then the loaded ones in load order), then all of
        // them are drawn into the bound render target through the render scene's camera.
        void OnUpdate(Timestep ts);
        // The game's part of OnUpdate without drawing / drawing only (tests, custom hosts).
        void UpdateScenes(Timestep ts);
        void RenderScenes();
        // End of the frame, after the host has finished its picture: what was requested during it
        // happens now - DontDestroyOnLoad moves, unloads, loads, async activations, active scene changes.
        void ProcessRequests();
        // After post-processing (display colours): the scenes' deferred UI (pixel-perfect upscaling),
        // then the transition's cover over everything (`width` x `height`: the bound target's pixels).
        void RenderDeferredUI();
        void RenderTransition(uint32_t width, uint32_t height);

        // ---- the host's input, forwarded to every scene
        void OnViewportResize(uint32_t width, uint32_t height);
        void SetViewportMouse(const glm::vec2& position, bool leftButtonDown);
        void SetViewportScroll(const glm::vec2& wheel);
        void SetUIKeyboardEnabled(bool enabled);
        // The editor's pause / frame step (every scene).
        void SetPaused(bool paused);
        bool IsPaused() const { return m_Paused; }
        void Step(int frames = 1);
        bool IsQuitRequested() const;
        void ClearQuitRequest();

        // ---- loading (paths relative to the assets; the switch happens at the end of the frame)
        void LoadScene(const std::filesystem::path& path, LoadSceneMode mode = LoadSceneMode::Single,
                       const SceneTransition& transition = {});
        // By build index (Project settings > Build scenes: the enabled ones in order, from 0). False
        // (logged) when there is no such scene.
        bool LoadScene(int buildIndex, LoadSceneMode mode = LoadSceneMode::Single, const SceneTransition& transition = {});
        // In the background; null when the file cannot be found. A transition fades out once the
        // operation is allowed to activate (while it still loads), and in after the switch.
        Ref<AsyncOperation> LoadSceneAsync(const std::filesystem::path& path, LoadSceneMode mode = LoadSceneMode::Single,
                                           const SceneTransition& transition = {});
        Ref<AsyncOperation> LoadSceneAsync(int buildIndex, LoadSceneMode mode = LoadSceneMode::Single, const SceneTransition& transition = {});
        // The scene after the active one in the build list (Single); false at the end of the list or
        // when the active scene is not in it.
        bool LoadNextScene(const SceneTransition& transition = {});
        // The active scene again from its file (Single: a level restart); false when it has none.
        bool ReloadActiveScene(const SceneTransition& transition = {});
        // A loaded scene's build index (-1: not in the build list / not loaded here).
        int GetBuildIndex(const Scene* scene) const;
        int GetActiveSceneBuildIndex() const { return GetBuildIndex(m_Active.get()); }
        // The loaded scene with that build index (null when none is loaded).
        Ref<Scene> GetSceneByBuildIndex(int buildIndex) const;
        // How many scenes the build list has (the enabled ones).
        static int GetBuildSceneCount();
        // At the end of the frame. The last loaded scene cannot be unloaded (load another one Single);
        // unloading the active scene makes the first other loaded scene active. False when it is not
        // loaded here.
        bool UnloadScene(const Ref<Scene>& scene);
        bool UnloadScene(std::string_view nameOrPath);
        // Main-thread budget of the async loads per frame (milliseconds; default 4).
        void SetAsyncBudget(float milliseconds) { m_AsyncBudget = std::max(0.1f, milliseconds); }
        float GetAsyncBudget() const { return m_AsyncBudget; }
        // Async loads in progress (not yet activated).
        const std::vector<Ref<AsyncOperation>>& GetAsyncOperations() const { return m_AsyncOps; }
        // Advances the async loads now (`budgetMs` of main-thread work); the host's OnUpdate does it.
        void UpdateAsyncLoads(float budgetMs);

        // ---- the loaded scenes
        // The scene new entities of the game belong to: its settings (sky, post) are the picture's
        // when its camera draws.
        const Ref<Scene>& GetActiveScene() const { return m_Active; }
        // Makes a loaded scene the active one at once. False when it is not loaded here.
        bool SetActiveScene(const Ref<Scene>& scene);
        // In load order (without the persistent scene).
        std::vector<Ref<Scene>> GetLoadedScenes() const;
        size_t GetSceneCount() const { return m_Scenes.size(); }
        // Loaded scenes, then the persistent one (when it exists): the order they are drawn in.
        std::vector<Ref<Scene>> GetAllScenes() const;
        Ref<Scene> GetSceneByName(std::string_view name) const;
        Ref<Scene> GetSceneByPath(const std::filesystem::path& path) const;
        // The file a loaded scene came from (relative to the assets, "" for none / not loaded here).
        std::string GetScenePath(const Scene* scene) const;
        bool IsLoaded(const Scene* scene) const;
        // The persistent scene (null until the first DontDestroyOnLoad).
        const Ref<Scene>& GetDontDestroyOnLoadScene() const { return m_Persistent; }
        bool IsDontDestroyOnLoadScene(const Scene* scene) const { return scene && m_Persistent.get() == scene; }
        // The scene whose primary camera draws the game: the active scene's, else the first loaded
        // scene (then the persistent one) with a primary camera; null without any camera.
        Scene* GetRenderScene() const;
        // Grows with every switch of the active scene (hosts reset what adapted to the old one).
        uint64_t GetActiveSceneSerial() const { return m_ActiveSerial; }

        // ---- persistent objects
        // At the end of the frame `entity` (with its descendants; a child leaves its parent) moves to
        // the DontDestroyOnLoad scene: it survives every Single load, its script and sounds go on.
        void DontDestroyOnLoad(Entity entity);

        // ---- across the loaded scenes (the active one first, then load order, the persistent last)
        // Each scene's lookup is indexed (Scene::FindEntityByName / FindEntitiesWithTag).
        Entity FindEntityByName(std::string_view name) const;
        Entity FindEntityByUUID(UUID id) const;
        std::vector<Entity> FindEntitiesWithTag(std::string_view tag) const;
        Entity FindEntityWithTag(std::string_view tag) const;
        // Every T of every scene (a component or a script class, see Scene::FindObjectsOfType).
        template<typename T>
        std::vector<T*> FindObjectsOfType() const
        {
            std::vector<T*> found;
            for (Scene* scene : GetSearchOrder())
            {
                std::vector<T*> more = scene->template FindObjectsOfType<T>();
                found.insert(found.end(), more.begin(), more.end());
            }
            return found;
        }
        // The scenes in the order the lookups go through them.
        std::vector<Scene*> GetSearchOrder() const;

        // ---- shared by every scene
        EventBus& GetEventBus() { return *m_EventBus; }
        // The game clock of every scene (Scene::SetTimeScale / SetGamePaused). A Single load starts
        // at 1 (as play mode does); additive scenes join with the current one.
        void SetTimeScale(float scale);
        float GetTimeScale() const;
        void SetGamePaused(bool paused);
        bool IsGamePaused() const;

        // ---- load problems (see SceneLoadReport): a scene that could not be loaded (the game stays
        // where it was), or one that came without some of its file (a broken component, a missing
        // asset...). Kept until taken (at most MaxLoadReports, the newest); the editor shows them,
        // the player shows an error screen when its first scene fails.
        static constexpr size_t MaxLoadReports = 16;
        const std::vector<SceneLoadReport>& GetLoadReports() const { return m_LoadReports; }
        std::vector<SceneLoadReport> TakeLoadReports();
        // Grows with every report (hosts notice new ones).
        uint64_t GetLoadReportSerial() const { return m_LoadReportSerial; }

        // ---- transitions
        bool IsTransitioning() const { return m_Fade.Phase != FadePhase::None; }
        // How much the cover hides now (0..1) and its colour.
        float GetTransitionAlpha() const { return m_Fade.Alpha; }
        glm::vec4 GetTransitionColor() const { return m_Fade.Transition.Color; }

        // ---- for the scenes
        // The sounds that ended since the last frame (taken once for all scenes).
        const std::vector<FinishedSound>& GetFrameFinishedSounds() const;

    private:
        struct SceneEntry
        {
            Ref<Scene> SceneRef;
            std::string Path;
        };
        enum class FadePhase { None = 0, Waiting, Out, Covered, In };
        struct FadeState
        {
            FadePhase Phase = FadePhase::None;
            SceneTransition Transition;
            Ref<AsyncOperation> Operation;
            float Alpha = 0.0f;
            float PreviousFadeIn = 0.0f; // Waiting: the cover of the transition before goes at its rate
            bool CoverShown = false; // the full cover was drawn (the switch may happen behind it)
        };
        struct Request
        {
            enum class Kind { Load, Unload, Persist } Type = Kind::Load;
            std::filesystem::path Path;
            LoadSceneMode Mode = LoadSceneMode::Single;
            Ref<Scene> Target;
            Scene* EntityScene = nullptr;
            UUID EntityID = 0;
        };

        // Gives the scene the manager's bus, viewport and input, starts it (OnRuntimeStart).
        void Adopt(const Ref<Scene>& scene);
        // `timing` (optional) receives its OnRuntimeStart's physics / scripts time.
        void StartScene(const Ref<Scene>& scene, SceneLoadTiming* timing = nullptr);
        void StopScene(const Ref<Scene>& scene);
        void AddLoaded(const Ref<Scene>& scene, const std::string& path, LoadSceneMode mode, SceneLoadTiming* timing = nullptr);
        void RemoveLoaded(const Ref<Scene>& scene);
        // `timing` (optional) receives the file's stages and the assets' time.
        Ref<Scene> LoadNow(const std::filesystem::path& path, std::string* error, SceneLoadTiming* timing = nullptr);
        void Activate(const Ref<AsyncOperation>& op);
        void AdvanceAsync(AsyncOperation& op, double deadlineMs);
        void UpdateFade(float dt);
        void ApplySharedPreloads();
        // Applies the shared preloads (the audio of the scenes that came: its time goes to the newest
        // of `timings`), then records every one of them (SceneLoadProfiler).
        void FinishLoadTimings(std::vector<SceneLoadTiming>& timings, bool preloads, AsyncOperation* last = nullptr);
        void AssignFrameRoles();
        void SetActive(const Ref<Scene>& scene);
        void AddLoadReport(const SceneLoadReport& report);
        // Scene events (see the class comment): every running script, then the bus.
        SceneEventInfo MakeEventInfo(const Ref<Scene>& scene, LoadSceneMode mode = LoadSceneMode::Single) const;
        void NotifySceneLoaded(const SceneEventInfo& scene);
        void NotifySceneUnloaded(const SceneEventInfo& scene);
        void NotifyActiveSceneChanged(const SceneEventInfo& previous, const SceneEventInfo& current);
        Ref<Scene> EnsurePersistentScene();
        std::vector<Scene*> GetUpdateOrder() const;
        static std::filesystem::path ToAssetFile(const std::filesystem::path& path);
        static std::string ToAssetRelative(const std::filesystem::path& path);

        std::vector<SceneEntry> m_Scenes;
        Ref<Scene> m_Active;
        Ref<Scene> m_Persistent;
        std::shared_ptr<EventBus> m_EventBus;
        std::vector<Request> m_Requests;
        std::vector<Ref<AsyncOperation>> m_AsyncOps;
        std::vector<Ref<AsyncOperation>> m_ToActivate;
        FadeState m_Fade;
        std::unique_ptr<std::vector<FinishedSound>> m_FinishedSounds;
        std::vector<SceneLoadReport> m_LoadReports;
        uint64_t m_LoadReportSerial = 0;
        float m_AsyncBudget = 4.0f;
        bool m_Running = false;
        bool m_Paused = false;
        uint64_t m_ActiveSerial = 0;
        uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
        glm::vec2 m_Mouse{ -1.0f };
        bool m_MouseDown = false;
        bool m_UIKeyboard = true;
    };

}
