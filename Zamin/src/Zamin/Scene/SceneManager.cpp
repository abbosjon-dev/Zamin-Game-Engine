#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Audio/AudioEngine.h"
#include "Zamin/Core/Profiler.h"
#include "Zamin/Debug/DebugDraw.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Renderer/RenderCommand.h"
#include "Zamin/Renderer/Renderer2D.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scripting/EventBus.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>

namespace Zamin {

    namespace fs = std::filesystem;

    static double NowMs()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // ------------------------------------------------------------------ AsyncOperation
    AsyncOperation::AsyncOperation(fs::path path, LoadSceneMode mode, const SceneTransition& transition)
        : m_Path(std::move(path)), m_Mode(mode), m_Transition(transition)
    {
    }

    AsyncOperation::~AsyncOperation()
    {
        m_Cancel = true;
        JoinWorker();
        if (!m_Decode.empty())
            TextureLibrary::DropPredecoded(m_Decode); // decoded ahead, never uploaded
    }

    void AsyncOperation::JoinWorker()
    {
        if (m_Worker.joinable())
            m_Worker.join();
    }

    void AsyncOperation::StartReading(const fs::path& file)
    {
        m_File = file;
        m_Job = std::make_unique<SceneLoadJob>();
        m_Stage = Stage::Reading;
        m_WorkerDone = false;
        // Only the file and the job are touched on the worker: reading and parsing the JSON, then
        // listing the images the entities name.
        m_Worker = std::thread([this]() {
            if (m_Job->Parse(m_File))
                m_Images = m_Job->CollectImagePaths();
            else
                m_WorkerFailed = true;
            m_WorkerDone.store(true, std::memory_order_release);
        });
    }

    float AsyncOperation::GetProgress() const
    {
        switch (m_Stage)
        {
            case Stage::Reading:
                return m_WorkerDone.load(std::memory_order_acquire) ? 0.25f : 0.05f;
            case Stage::Decoding:
                return 0.25f + 0.25f * (m_Decode.empty() ? 1.0f : (float)m_ImagesDecoded.load() / (float)m_Decode.size());
            case Stage::Building:
                return 0.5f + 0.2f * m_BuildProgress;
            case Stage::Resolving:
                return 0.7f + 0.2f * (m_ResolveOrder.empty() ? 1.0f : (float)m_Resolved / (float)m_ResolveOrder.size());
            case Stage::Ready:
                return 0.9f;
            case Stage::Done:
            case Stage::Failed:
                return 1.0f;
        }
        return 0.0f;
    }

    // ------------------------------------------------------------------ SceneManager
    SceneManager::SceneManager()
        : m_EventBus(std::make_shared<EventBus>()), m_FinishedSounds(std::make_unique<std::vector<FinishedSound>>())
    {
    }

    SceneManager::~SceneManager()
    {
        if (m_Running)
            Stop();
    }

    fs::path SceneManager::ToAssetFile(const fs::path& path)
    {
        return path.is_absolute() ? path : Project::GetAssetFileSystemPath(path);
    }

    std::string SceneManager::ToAssetRelative(const fs::path& path)
    {
        return path.is_absolute() ? Project::GetAssetRelativePath(path) : path.lexically_normal().generic_string();
    }

    // A bare name ("Level2") is looked up in the build list; anything else is a path.
    static fs::path ResolveScenePath(const fs::path& path)
    {
        if (!path.has_extension() && !path.has_parent_path())
            if (fs::path build = Project::FindBuildScene(path.string()); !build.empty())
                return build;
        return path;
    }

    void SceneManager::Adopt(const Ref<Scene>& scene)
    {
        scene->m_SceneManager = this;
        scene->m_EventBus = m_EventBus;
        scene->m_SharedEventBus = true;
        scene->OnViewportResize(m_ViewportWidth, m_ViewportHeight);
        scene->SetViewportMouse(m_Mouse, m_MouseDown);
        scene->SetUIKeyboardEnabled(m_UIKeyboard);
        scene->SetPaused(m_Paused);
    }

    void SceneManager::StartScene(const Ref<Scene>& scene)
    {
        Adopt(scene);
        scene->OnRuntimeStart();
    }

    void SceneManager::StopScene(const Ref<Scene>& scene)
    {
        if (!scene)
            return;
        if (scene->IsRunning())
            scene->OnRuntimeStop();
        // Queued events must not point into a scene that is about to be freed.
        m_EventBus->ForgetSender(scene.get());
        scene->m_SceneManager = nullptr;
    }

    void SceneManager::SetActive(const Ref<Scene>& scene)
    {
        if (m_Active == scene)
            return;
        m_Active = scene;
        m_ActiveSerial++;
    }

    void SceneManager::Start(const Ref<Scene>& scene, const fs::path& path)
    {
        if (m_Running)
            Stop();
        if (!scene)
            return;
        m_Running = true;
        m_Fade = {};
        DebugDraw::Clear(); // the game starts with an empty debug drawing queue
        AudioEngine::TakeFinishedSounds(); // sounds that ended before this run are not its
        m_FinishedSounds->clear();
        StartScene(scene);
        m_Scenes.push_back({ scene, path.empty() ? std::string() : ToAssetRelative(path) });
        SetActive(scene);
        ApplySharedPreloads();
    }

    bool SceneManager::Start(const fs::path& path, std::string* error)
    {
        std::string reason;
        Ref<Scene> scene = LoadNow(path, &reason);
        if (!scene)
        {
            if (error)
                *error = reason;
            return false;
        }
        Start(scene, path);
        return true;
    }

    void SceneManager::Stop()
    {
        // Loads in progress are dropped (their workers are joined as they go).
        m_AsyncOps.clear();
        m_ToActivate.clear();
        m_Fade = {};
        m_Requests.clear();
        std::vector<SceneEntry> scenes = std::move(m_Scenes);
        m_Scenes.clear();
        for (auto it = scenes.rbegin(); it != scenes.rend(); ++it)
            StopScene(it->SceneRef);
        StopScene(m_Persistent);
        m_Persistent.reset();
        m_Active.reset();
        m_EventBus->Clear();
        // What a single scene's OnRuntimeStop did for the whole game: the music goes on.
        AudioEngine::StopAllExceptMusic();
        DebugDraw::Clear();
        m_FinishedSounds->clear();
        m_Running = false;
        m_Paused = false;
    }

    Ref<Scene> SceneManager::LoadNow(const fs::path& path, std::string* error)
    {
        const fs::path file = ToAssetFile(ResolveScenePath(path));
        Ref<Scene> scene = CreateRef<Scene>();
        scene->OnViewportResize(m_ViewportWidth, m_ViewportHeight); // cameras get the view's aspect
        SceneSerializer serializer(scene);
        if (!serializer.Deserialize(file))
        {
            if (error)
                *error = serializer.GetLastError();
            return nullptr;
        }
        scene->ResolveAssets();
        return scene;
    }

    // ------------------------------------------------------------------ frames
    std::vector<Scene*> SceneManager::GetUpdateOrder() const
    {
        // The persistent scene first (a game manager reads the input before the level does), then
        // the loaded scenes in load order.
        std::vector<Scene*> order;
        order.reserve(m_Scenes.size() + 1);
        if (m_Persistent)
            order.push_back(m_Persistent.get());
        for (const SceneEntry& entry : m_Scenes)
            order.push_back(entry.SceneRef.get());
        return order;
    }

    void SceneManager::AssignFrameRoles()
    {
        std::vector<Scene*> order = GetUpdateOrder();
        for (size_t i = 0; i < order.size(); i++)
        {
            order[i]->m_FrameFollower = i > 0;
            order[i]->m_PlacesAudioListener = false;
        }
        // The listener: the drawing scene's (its camera / Audio Listener), else the first that has one.
        Scene* render = GetRenderScene();
        Scene* listener = render && render->GetAudioListenerEntity() ? render : nullptr;
        for (size_t i = 0; i < order.size() && !listener; i++)
            if (order[i]->GetAudioListenerEntity())
                listener = order[i];
        if (listener)
            listener->m_PlacesAudioListener = true;
    }

    const std::vector<FinishedSound>& SceneManager::GetFrameFinishedSounds() const { return *m_FinishedSounds; }

    void SceneManager::OnUpdate(Timestep ts)
    {
        if (!m_Running)
            return;
        UpdateAsyncLoads(m_AsyncBudget);
        UpdateFade(ts);
        UpdateScenes(ts);
        RenderScenes();
    }

    void SceneManager::UpdateScenes(Timestep ts)
    {
        if (!m_Running)
            return;
        *m_FinishedSounds = AudioEngine::TakeFinishedSounds();
        AssignFrameRoles();
        // Scenes are only added / removed in ProcessRequests: the list holds while the scripts run.
        for (Scene* scene : GetUpdateOrder())
            scene->UpdateRuntime(ts);
        m_FinishedSounds->clear();
    }

    void SceneManager::RenderScenes()
    {
        if (!m_Running)
            return;
        ZM_PROFILE_SCOPE("Sahnalar (chizish)");
        Scene* camera = GetRenderScene();
        Scene::RuntimeView view;
        if (!camera || !camera->GetRuntimeView(view))
            return;
        // The camera's scene with its sky, then the others over it (the persistent one last: on top).
        camera->RenderRuntime(view, false);
        for (const Ref<Scene>& scene : GetAllScenes())
            if (scene.get() != camera)
                scene->RenderRuntime(view, true);
    }

    void SceneManager::RenderDeferredUI()
    {
        for (const Ref<Scene>& scene : GetAllScenes())
            if (scene->HasDeferredUI())
                scene->RenderDeferredUI();
    }

    void SceneManager::RenderTransition(uint32_t width, uint32_t height)
    {
        if (m_Fade.Phase == FadePhase::None || m_Fade.Alpha <= 0.0f || width == 0 || height == 0)
            return;
        glm::vec4 color = m_Fade.Transition.Color;
        color.a *= std::clamp(m_Fade.Alpha, 0.0f, 1.0f);
        // Over the finished picture (display colours); the picture itself stays opaque.
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetColorWriteMask(0, true, false);
        Renderer2D::SetDisplayOutput(true);
        Renderer2D::SetLit(false);
        Renderer2D::BeginScene(glm::ortho(0.0f, (float)width, 0.0f, (float)height, -1.0f, 1.0f));
        Renderer2D::DrawQuad(glm::vec2((float)width * 0.5f, (float)height * 0.5f), glm::vec2((float)width, (float)height), color);
        Renderer2D::EndScene();
        Renderer2D::SetDisplayOutput(false);
        RenderCommand::SetColorWriteMask(0, true, true);
    }

    void SceneManager::UpdateFade(float dt)
    {
        // The cover moves at the transition's rates from wherever it is (a new transition can take
        // over while the last one still fades in).
        FadeState& f = m_Fade;
        auto fadeIn = [&](float seconds) { f.Alpha = seconds > 0.0f ? std::max(0.0f, f.Alpha - dt / seconds) : 0.0f; };
        switch (f.Phase)
        {
            case FadePhase::None:
                f.Alpha = 0.0f;
                return;
            case FadePhase::Waiting:
                // The screen starts to fade once the load may come in (it goes on loading meanwhile);
                // the cover of the transition before this one goes away meanwhile.
                if (f.Operation && f.Operation->HasFailed())
                {
                    f.Phase = FadePhase::In; // nothing will come: no fade out for it
                    f.Transition.FadeIn = f.PreviousFadeIn;
                    fadeIn(f.Transition.FadeIn);
                    return;
                }
                if (!f.Operation || f.Operation->GetAllowSceneActivation() || f.Operation->IsDone())
                    f.Phase = FadePhase::Out;
                else
                {
                    fadeIn(f.PreviousFadeIn);
                    return;
                }
                [[fallthrough]];
            case FadePhase::Out:
                f.Alpha = f.Transition.FadeOut > 0.0f ? std::min(1.0f, f.Alpha + dt / f.Transition.FadeOut) : 1.0f;
                if (f.Alpha < 1.0f)
                    return;
                f.Phase = FadePhase::Covered;
                [[fallthrough]];
            case FadePhase::Covered:
                f.Alpha = 1.0f;
                // Drawn in full this frame: the switch at its end happens behind it.
                f.CoverShown = true;
                if (f.Operation && f.Operation->HasFailed())
                    f.Phase = FadePhase::In; // nothing to switch to: back to the old scene
                return;
            case FadePhase::In:
                fadeIn(f.Transition.FadeIn);
                if (f.Alpha <= 0.0f)
                    f = {};
                return;
        }
    }

    // ------------------------------------------------------------------ requests
    void SceneManager::LoadScene(const fs::path& path, LoadSceneMode mode, const SceneTransition& transition)
    {
        if (!transition.IsNone())
        {
            // With a fade: loaded in the background while the screen fades out.
            LoadSceneAsync(path, mode, transition);
            return;
        }
        Request request;
        request.Type = Request::Kind::Load;
        request.Path = ResolveScenePath(path);
        request.Mode = mode;
        m_Requests.push_back(std::move(request));
    }

    bool SceneManager::LoadScene(int buildIndex, LoadSceneMode mode, const SceneTransition& transition)
    {
        fs::path path = Project::GetBuildScenePath(buildIndex);
        if (path.empty())
        {
            ZM_CORE_ERROR("Build ro'yxatida {} indeksli sahna yo'q ({} ta sahna bor)", buildIndex, Project::GetBuildScenes().size());
            return false;
        }
        LoadScene(path, mode, transition);
        return true;
    }

    Ref<AsyncOperation> SceneManager::LoadSceneAsync(const fs::path& requested, LoadSceneMode mode, const SceneTransition& transition)
    {
        const fs::path path = ResolveScenePath(requested);
        const fs::path file = ToAssetFile(path);
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
        {
            AssetImportLog::Report(file, "fayl topilmadi"); // logged once with the path
            return nullptr;
        }
        Ref<AsyncOperation> op = CreateRef<AsyncOperation>(fs::path(ToAssetRelative(path)), mode, transition);
        if (!transition.IsNone())
        {
            // A transition that still covers the screen for its switch keeps it; one that only fades
            // in (its scene is in: a loading screen starting the next load) hands the cover over.
            if (m_Fade.Phase == FadePhase::Waiting || m_Fade.Phase == FadePhase::Out || m_Fade.Phase == FadePhase::Covered)
            {
                ZM_CORE_WARN("Oldingi sahna o'tishi hali tugamagan: '{}' o'tishsiz yuklanadi", op->GetPath().generic_string());
                op->m_Transition = {};
            }
            else
            {
                const float alpha = m_Fade.Phase == FadePhase::In ? m_Fade.Alpha : 0.0f;
                const float previousFadeIn = m_Fade.Transition.FadeIn;
                m_Fade = {};
                m_Fade.Phase = FadePhase::Waiting;
                m_Fade.Transition = transition;
                m_Fade.Operation = op;
                m_Fade.Alpha = alpha;
                m_Fade.PreviousFadeIn = previousFadeIn;
            }
        }
        op->StartReading(file);
        m_AsyncOps.push_back(op);
        return op;
    }

    bool SceneManager::UnloadScene(const Ref<Scene>& scene)
    {
        if (!IsLoaded(scene.get()))
            return false;
        Request request;
        request.Type = Request::Kind::Unload;
        request.Target = scene;
        m_Requests.push_back(std::move(request));
        return true;
    }

    bool SceneManager::UnloadScene(std::string_view nameOrPath)
    {
        Ref<Scene> scene = GetSceneByPath(fs::path(nameOrPath));
        if (!scene)
            scene = GetSceneByName(nameOrPath);
        if (!scene || scene == m_Persistent)
        {
            ZM_CORE_WARN("Yopiladigan sahna topilmadi: '{}'", nameOrPath);
            return false;
        }
        return UnloadScene(scene);
    }

    void SceneManager::DontDestroyOnLoad(Entity entity)
    {
        if (!entity.IsValid())
            return;
        Request request;
        request.Type = Request::Kind::Persist;
        request.EntityScene = entity.GetScene();
        request.EntityID = entity.GetUUID();
        m_Requests.push_back(std::move(request));
    }

    Ref<Scene> SceneManager::EnsurePersistentScene()
    {
        if (!m_Persistent)
        {
            m_Persistent = CreateRef<Scene>();
            m_Persistent->SetName(DontDestroyOnLoadSceneName);
            m_Persistent->GetSettings().DrawSky = false;
            StartScene(m_Persistent);
            if (m_Active)
            {
                m_Persistent->m_TimeScale = m_Active->m_TimeScale;
                m_Persistent->m_TimeScaleBeforePause = m_Active->m_TimeScaleBeforePause;
            }
        }
        return m_Persistent;
    }

    void SceneManager::AddLoaded(const Ref<Scene>& scene, const std::string& path, LoadSceneMode mode)
    {
        if (mode == LoadSceneMode::Single)
        {
            // Every loaded scene goes (the persistent one stays); the game clock starts at 1 again.
            std::vector<SceneEntry> old = std::move(m_Scenes);
            m_Scenes.clear();
            m_Active.reset();
            for (auto it = old.rbegin(); it != old.rend(); ++it)
                StopScene(it->SceneRef);
            if (m_Persistent)
            {
                m_Persistent->SetGamePaused(false);
                m_Persistent->SetTimeScale(1.0f);
            }
        }
        const Scene* clock = m_Active ? m_Active.get() : nullptr;
        StartScene(scene);
        if (clock)
        {
            // An additive scene runs on the game's clock as it is now.
            scene->m_TimeScale = clock->m_TimeScale;
            scene->m_TimeScaleBeforePause = clock->m_TimeScaleBeforePause;
        }
        m_Scenes.push_back({ scene, path });
        if (mode == LoadSceneMode::Single || !m_Active)
            SetActive(scene);
        ZM_CORE_INFO("Sahna yuklandi: {} ({})", path.empty() ? scene->GetName() : path, mode == LoadSceneMode::Additive ? "qo'shimcha" : "yagona");
    }

    void SceneManager::RemoveLoaded(const Ref<Scene>& scene)
    {
        std::erase_if(m_Scenes, [&](const SceneEntry& e) { return e.SceneRef == scene; });
        if (m_Active == scene)
            SetActive(m_Scenes.empty() ? nullptr : m_Scenes.front().SceneRef);
    }

    void SceneManager::Activate(const Ref<AsyncOperation>& op)
    {
        AddLoaded(op->m_Scene, op->m_Path.generic_string(), op->m_Mode);
        op->m_Stage = AsyncOperation::Stage::Done;
        op->m_Job.reset();
        if (m_Fade.Operation == op)
        {
            m_Fade.Phase = FadePhase::In;
            m_Fade.Operation.reset();
        }
    }

    void SceneManager::ProcessRequests()
    {
        if (!m_Running)
            return;
        // Code that asked a scene directly (Scene::RequestLoadScene) is heard too.
        for (Scene* scene : GetUpdateOrder())
            if (!scene->GetPendingSceneLoad().empty())
            {
                LoadScene(scene->GetPendingSceneLoad());
                scene->ClearPendingSceneLoad();
            }

        bool changed = false;
        std::vector<Request> requests = std::move(m_Requests);
        m_Requests.clear(); // callbacks below (OnDestroy of an unloaded scene) queue for the next frame
        for (Request& request : requests)
        {
            switch (request.Type)
            {
                case Request::Kind::Persist:
                {
                    Scene* source = request.EntityScene;
                    const bool known = source == m_Persistent.get() ||
                                       std::any_of(m_Scenes.begin(), m_Scenes.end(), [&](const SceneEntry& e) { return e.SceneRef.get() == source; });
                    Entity entity = known ? source->GetEntityByUUID(request.EntityID) : Entity();
                    if (!entity || source == m_Persistent.get())
                        break; // gone meanwhile, or already persistent
                    const std::string name = entity.GetName();
                    if (Entity moved = Scene::MoveEntityToScene(entity, *EnsurePersistentScene()))
                    {
                        ZM_CORE_INFO("'{}' {} sahnasiga ko'chirildi: sahna almashganda saqlanadi", name, DontDestroyOnLoadSceneName);
                        changed = true;
                    }
                    break;
                }
                case Request::Kind::Unload:
                {
                    if (!IsLoaded(request.Target.get()) || request.Target == m_Persistent)
                        break;
                    if (m_Scenes.size() <= 1)
                    {
                        ZM_CORE_WARN("Oxirgi sahnani yopib bo'lmaydi: '{}' (boshqasini Single rejimida yuklang)", request.Target->GetName());
                        break;
                    }
                    const std::string name = request.Target->GetName();
                    RemoveLoaded(request.Target);
                    StopScene(request.Target);
                    ZM_CORE_INFO("Sahna yopildi: {}", name);
                    changed = true;
                    break;
                }
                case Request::Kind::Load:
                {
                    std::string error;
                    Ref<Scene> scene = LoadNow(request.Path, &error);
                    if (!scene)
                    {
                        ZM_CORE_ERROR("Sahnaga o'tilmadi: {} ({})", request.Path.generic_string(), error);
                        break;
                    }
                    AddLoaded(scene, ToAssetRelative(request.Path), request.Mode);
                    changed = true;
                    break;
                }
            }
        }

        // Async loads that are in and may come in (with a fade: once it covers the screen).
        for (size_t i = 0; i < m_AsyncOps.size();)
        {
            Ref<AsyncOperation> op = m_AsyncOps[i];
            if (op->HasFailed())
            {
                m_AsyncOps.erase(m_AsyncOps.begin() + (ptrdiff_t)i);
                continue;
            }
            const bool fadeReady = m_Fade.Operation != op || (m_Fade.Phase == FadePhase::Covered && m_Fade.CoverShown);
            if (op->IsReady() && op->GetAllowSceneActivation() && fadeReady)
            {
                m_AsyncOps.erase(m_AsyncOps.begin() + (ptrdiff_t)i);
                Activate(op);
                changed = true;
                continue;
            }
            i++;
        }
        if (changed)
            ApplySharedPreloads();
    }

    void SceneManager::UpdateAsyncLoads(float budgetMs)
    {
        if (m_AsyncOps.empty())
            return;
        ZM_PROFILE_SCOPE("Sahna yuklash (fon)");
        const double deadline = NowMs() + (double)budgetMs;
        // A copy: a failure is only marked here (the list changes in ProcessRequests).
        std::vector<Ref<AsyncOperation>> ops = m_AsyncOps;
        for (const Ref<AsyncOperation>& op : ops)
        {
            AdvanceAsync(*op, deadline);
            if (NowMs() >= deadline)
                break;
        }
    }

    void SceneManager::AdvanceAsync(AsyncOperation& op, double deadline)
    {
        using Stage = AsyncOperation::Stage;
        auto fail = [&](const std::string& reason) {
            op.JoinWorker();
            op.m_Error = reason;
            op.m_Stage = Stage::Failed;
            AssetImportLog::Report(op.m_File, reason); // logged once with the path
            if (!op.m_Decode.empty())
            {
                TextureLibrary::DropPredecoded(op.m_Decode);
                op.m_Decode.clear();
            }
        };
        for (;;)
        {
            switch (op.m_Stage)
            {
                case Stage::Reading:
                {
                    if (!op.m_WorkerDone.load(std::memory_order_acquire))
                        return;
                    op.JoinWorker();
                    if (op.m_WorkerFailed)
                        return fail(op.m_Job->GetError());
                    // The images that are not loaded yet are decoded ahead on the worker.
                    for (const std::string& image : op.m_Images)
                    {
                        fs::path file = Project::GetAssetFileSystemPath(image);
                        std::error_code ec;
                        if (fs::is_regular_file(file, ec) && !TextureLibrary::IsLoaded(file))
                            op.m_Decode.push_back(file);
                    }
                    if (op.m_Decode.empty())
                    {
                        op.m_Stage = Stage::Building;
                        break;
                    }
                    op.m_Stage = Stage::Decoding;
                    op.m_WorkerDone = false;
                    op.m_Worker = std::thread([&op]() {
                        for (const fs::path& file : op.m_Decode)
                        {
                            if (op.m_Cancel)
                                break;
                            TextureLibrary::Predecode(file);
                            op.m_ImagesDecoded++;
                        }
                        op.m_WorkerDone.store(true, std::memory_order_release);
                    });
                    return;
                }
                case Stage::Decoding:
                    if (!op.m_WorkerDone.load(std::memory_order_acquire))
                        return;
                    op.JoinWorker();
                    op.m_Stage = Stage::Building;
                    break;
                case Stage::Building:
                {
                    if (!op.m_Scene)
                    {
                        op.m_Scene = CreateRef<Scene>();
                        op.m_Scene->OnViewportResize(m_ViewportWidth, m_ViewportHeight);
                    }
                    const bool finished = op.m_Job->Build(*op.m_Scene, std::max(0.0, deadline - NowMs()));
                    op.m_BuildProgress = op.m_Job->GetBuildProgress();
                    if (!finished)
                        return;
                    if (!op.m_Job->GetError().empty())
                        return fail(op.m_Job->GetError());
                    AssetImportLog::Clear(op.m_File);
                    for (Entity e : op.m_Scene->GetEntitiesInOrder())
                        op.m_ResolveOrder.push_back(e.GetUUID());
                    op.m_Stage = Stage::Resolving;
                    break;
                }
                case Stage::Resolving:
                {
                    // The GPU uploads (textures - predecoded -, meshes, fonts), a slice per frame.
                    while (op.m_Resolved < op.m_ResolveOrder.size())
                    {
                        if (Entity e = op.m_Scene->GetEntityByUUID(op.m_ResolveOrder[op.m_Resolved]))
                            op.m_Scene->ResolveEntityAssets(e);
                        op.m_Resolved++;
                        if (NowMs() >= deadline && op.m_Resolved < op.m_ResolveOrder.size())
                            return;
                    }
                    TextureLibrary::DropPredecoded(op.m_Decode); // what no component asked for
                    op.m_Decode.clear();
                    op.m_Stage = Stage::Ready;
                    ZM_CORE_INFO("Sahna fonda yuklandi: {} ({} ta obyekt, {} ta rasm oldindan o'qildi)", op.m_Path.generic_string(),
                                 op.m_ResolveOrder.size(), op.m_ImagesDecoded.load());
                    return;
                }
                case Stage::Ready:
                case Stage::Done:
                case Stage::Failed:
                    return;
            }
            if (NowMs() >= deadline)
                return;
        }
    }

    void SceneManager::ApplySharedPreloads()
    {
        std::vector<fs::path> preloads;
        for (const Ref<Scene>& scene : GetAllScenes())
            for (fs::path& file : scene->CollectAudioPreloads())
                if (std::find(preloads.begin(), preloads.end(), file) == preloads.end())
                    preloads.push_back(std::move(file));
        AudioEngine::SetScenePreloads(preloads);
    }

    // ------------------------------------------------------------------ the scenes
    bool SceneManager::SetActiveScene(const Ref<Scene>& scene)
    {
        if (!scene || scene == m_Persistent || !IsLoaded(scene.get()))
            return false;
        SetActive(scene);
        return true;
    }

    std::vector<Ref<Scene>> SceneManager::GetLoadedScenes() const
    {
        std::vector<Ref<Scene>> scenes;
        scenes.reserve(m_Scenes.size());
        for (const SceneEntry& entry : m_Scenes)
            scenes.push_back(entry.SceneRef);
        return scenes;
    }

    std::vector<Ref<Scene>> SceneManager::GetAllScenes() const
    {
        std::vector<Ref<Scene>> scenes = GetLoadedScenes();
        if (m_Persistent)
            scenes.push_back(m_Persistent);
        return scenes;
    }

    Ref<Scene> SceneManager::GetSceneByName(std::string_view name) const
    {
        for (const SceneEntry& entry : m_Scenes)
            if (entry.SceneRef->GetName() == name)
                return entry.SceneRef;
        if (m_Persistent && name == DontDestroyOnLoadSceneName)
            return m_Persistent;
        return nullptr;
    }

    Ref<Scene> SceneManager::GetSceneByPath(const fs::path& path) const
    {
        if (path.empty())
            return nullptr;
        const std::string wanted = ToAssetRelative(path);
        for (const SceneEntry& entry : m_Scenes)
            if (!entry.Path.empty() && entry.Path == wanted)
                return entry.SceneRef;
        return nullptr;
    }

    std::string SceneManager::GetScenePath(const Scene* scene) const
    {
        for (const SceneEntry& entry : m_Scenes)
            if (entry.SceneRef.get() == scene)
                return entry.Path;
        return {};
    }

    bool SceneManager::IsLoaded(const Scene* scene) const
    {
        return scene && (scene == m_Persistent.get() ||
                         std::any_of(m_Scenes.begin(), m_Scenes.end(), [&](const SceneEntry& e) { return e.SceneRef.get() == scene; }));
    }

    Scene* SceneManager::GetRenderScene() const
    {
        auto hasCamera = [](Scene* s) { return s && s->GetPrimaryCameraEntity(); };
        if (hasCamera(m_Active.get()))
            return m_Active.get();
        for (const SceneEntry& entry : m_Scenes)
            if (hasCamera(entry.SceneRef.get()))
                return entry.SceneRef.get();
        if (hasCamera(m_Persistent.get()))
            return m_Persistent.get();
        return m_Active.get();
    }

    // ------------------------------------------------------------------ across scenes
    Entity SceneManager::FindEntityByName(std::string_view name) const
    {
        if (m_Active)
            if (Entity e = m_Active->FindEntityByName(name))
                return e;
        for (const SceneEntry& entry : m_Scenes)
            if (entry.SceneRef != m_Active)
                if (Entity e = entry.SceneRef->FindEntityByName(name))
                    return e;
        return m_Persistent ? m_Persistent->FindEntityByName(name) : Entity();
    }

    Entity SceneManager::FindEntityByUUID(UUID id) const
    {
        if (m_Active)
            if (Entity e = m_Active->GetEntityByUUID(id))
                return e;
        for (const SceneEntry& entry : m_Scenes)
            if (entry.SceneRef != m_Active)
                if (Entity e = entry.SceneRef->GetEntityByUUID(id))
                    return e;
        return m_Persistent ? m_Persistent->GetEntityByUUID(id) : Entity();
    }

    std::vector<Entity> SceneManager::FindEntitiesWithTag(std::string_view tag) const
    {
        std::vector<Entity> found;
        auto add = [&](const Ref<Scene>& scene) {
            std::vector<Entity> list = scene->FindEntitiesWithTag(tag);
            found.insert(found.end(), list.begin(), list.end());
        };
        if (m_Active)
            add(m_Active);
        for (const SceneEntry& entry : m_Scenes)
            if (entry.SceneRef != m_Active)
                add(entry.SceneRef);
        if (m_Persistent)
            add(m_Persistent);
        return found;
    }

    // ------------------------------------------------------------------ shared state
    void SceneManager::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;
        for (Scene* scene : GetUpdateOrder())
            scene->OnViewportResize(width, height);
    }

    void SceneManager::SetViewportMouse(const glm::vec2& position, bool leftButtonDown)
    {
        m_Mouse = position;
        m_MouseDown = leftButtonDown;
        for (Scene* scene : GetUpdateOrder())
            scene->SetViewportMouse(position, leftButtonDown);
    }

    void SceneManager::SetViewportScroll(const glm::vec2& wheel)
    {
        for (Scene* scene : GetUpdateOrder())
            scene->SetViewportScroll(wheel);
    }

    void SceneManager::SetUIKeyboardEnabled(bool enabled)
    {
        m_UIKeyboard = enabled;
        for (Scene* scene : GetUpdateOrder())
            scene->SetUIKeyboardEnabled(enabled);
    }

    void SceneManager::SetPaused(bool paused)
    {
        m_Paused = paused;
        for (Scene* scene : GetUpdateOrder())
            scene->SetPaused(paused);
    }

    void SceneManager::Step(int frames)
    {
        for (Scene* scene : GetUpdateOrder())
            scene->Step(frames);
    }

    bool SceneManager::IsQuitRequested() const
    {
        for (Scene* scene : GetUpdateOrder())
            if (scene->IsQuitRequested())
                return true;
        return false;
    }

    void SceneManager::ClearQuitRequest()
    {
        for (Scene* scene : GetUpdateOrder())
            scene->ClearQuitRequest();
    }

    void SceneManager::SetTimeScale(float scale)
    {
        for (Scene* scene : GetUpdateOrder())
            scene->SetTimeScale(scale);
    }

    float SceneManager::GetTimeScale() const { return m_Active ? m_Active->GetTimeScale() : 1.0f; }

    void SceneManager::SetGamePaused(bool paused)
    {
        for (Scene* scene : GetUpdateOrder())
            scene->SetGamePaused(paused);
    }

    bool SceneManager::IsGamePaused() const { return m_Active && m_Active->IsGamePaused(); }

}
