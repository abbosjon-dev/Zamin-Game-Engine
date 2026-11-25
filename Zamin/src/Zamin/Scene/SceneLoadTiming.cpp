#include "Zamin/Scene/SceneLoadTiming.h"
#include "Zamin/Core/Log.h"

#include <deque>
#include <format>
#include <mutex>

namespace Zamin {

    double SceneLoadTiming::TotalMilliseconds() const
    {
        double total = 0.0;
        for (double ms : Milliseconds)
            total += ms;
        return total;
    }

    double SceneLoadTiming::MainThreadMilliseconds() const
    {
        if (How != Source::Async)
            return TotalMilliseconds();
        double total = 0.0;
        for (size_t i = 0; i < StageCount; i++)
            if (!IsWorkerStage((Stage)i))
                total += Milliseconds[i];
        return total;
    }

    SceneLoadTiming::Stage SceneLoadTiming::GetSlowestStage() const
    {
        size_t slowest = 0;
        for (size_t i = 1; i < StageCount; i++)
            if (Milliseconds[i] > Milliseconds[slowest])
                slowest = i;
        return (Stage)slowest;
    }

    void SceneLoadTiming::Add(const SceneLoadTiming& other)
    {
        for (size_t i = 0; i < StageCount; i++)
            Milliseconds[i] += other.Milliseconds[i];
        Binary = Binary || other.Binary;
        if (other.FileBytes)
            FileBytes = other.FileBytes;
        if (other.Entities)
            Entities = other.Entities;
    }

    const char* SceneLoadTiming::StageName(Stage stage)
    {
        switch (stage)
        {
            case Stage::Read:        return "Read";
            case Stage::Parse:       return "JSON";
            case Stage::Migrate:     return "Migration";
            case Stage::Deserialize: return "Objects";
            case Stage::Decode:      return "Images (background)";
            case Stage::Assets:      return "Assets";
            case Stage::Audio:       return "Audio";
            case Stage::Physics:     return "Physics";
            case Stage::Scripts:     return "Scripts";
            case Stage::Count:       break;
        }
        return "?";
    }

    const char* SceneLoadTiming::StageDescription(Stage stage)
    {
        switch (stage)
        {
            case Stage::Read:        return "Reading the file from disk";
            case Stage::Parse:       return "Parsing the JSON text (or the binary cache)";
            case Stage::Migrate:     return "Converting from an old format version to the current one";
            case Stage::Deserialize: return "Creating objects and components, linking the hierarchy";
            case Stage::Decode:      return "Pre-decoding images (on a background thread, without stopping the main thread)";
            case Stage::Assets:      return "Loading textures, meshes, fonts and animations (uploading to the GPU)";
            case Stage::Audio:       return "Sound clips the scene preloads";
            case Stage::Physics:     return "Creating the 2D and 3D physics worlds, bodies and colliders";
            case Stage::Scripts:     return "The scripts' OnCreate and OnEnable calls";
            case Stage::Count:       break;
        }
        return "";
    }

    const char* SceneLoadTiming::SourceName(Source source)
    {
        switch (source)
        {
            case Source::Editor:   return "editor";
            case Source::Start:    return "start";
            case Source::Single:   return "yagona";
            case Source::Additive: return "additive";
            case Source::Async:    return "background";
        }
        return "?";
    }

    bool SceneLoadTiming::IsWorkerStage(Stage stage)
    {
        return stage == Stage::Read || stage == Stage::Parse || stage == Stage::Migrate || stage == Stage::Decode;
    }

    std::string SceneLoadTiming::Format() const
    {
        std::string how = SourceName(How);
        if (How == Source::Async && Additive)
            how += ", additive";
        if (Binary)
            how += ", binary cache";
        std::string text = std::format("{} ({}): {:.2f} ms", SceneName.empty() ? Path : SceneName, how, TotalMilliseconds());
        if (How == Source::Async)
            text += std::format(" (main thread {:.2f} ms, {} frames, over {:.0f} ms)", MainThreadMilliseconds(), Frames, WallMilliseconds);
        if (Entities)
            text += std::format(", {} objects", Entities);
        if (Failed)
            text += ", failed";
        std::string stages;
        for (size_t i = 0; i < StageCount; i++)
            if (Milliseconds[i] >= 0.005)
                stages += std::format("{}{} {:.2f}", stages.empty() ? "" : ", ", StageName((Stage)i), Milliseconds[i]);
        if (!stages.empty())
            text += " - " + stages;
        return text;
    }

    // ------------------------------------------------------------------ history
    namespace {
        std::mutex s_Mutex;
        std::deque<SceneLoadTiming> s_History;
        uint64_t s_Serial = 0;
        bool s_Logging = true;
    }

    void SceneLoadProfiler::Record(const SceneLoadTiming& timing)
    {
        bool log = false;
        {
            std::lock_guard lock(s_Mutex);
            if (s_History.size() >= Capacity)
                s_History.pop_front();
            s_History.push_back(timing);
            s_Serial++;
            log = s_Logging;
        }
        if (log)
            ZM_CORE_INFO("Scene load time: {}", timing.Format());
    }

    std::vector<SceneLoadTiming> SceneLoadProfiler::GetHistory()
    {
        std::lock_guard lock(s_Mutex);
        return std::vector<SceneLoadTiming>(s_History.begin(), s_History.end());
    }

    bool SceneLoadProfiler::GetLast(SceneLoadTiming& out)
    {
        std::lock_guard lock(s_Mutex);
        if (s_History.empty())
            return false;
        out = s_History.back();
        return true;
    }

    size_t SceneLoadProfiler::GetCount()
    {
        std::lock_guard lock(s_Mutex);
        return s_History.size();
    }

    uint64_t SceneLoadProfiler::GetSerial()
    {
        std::lock_guard lock(s_Mutex);
        return s_Serial;
    }

    void SceneLoadProfiler::Clear()
    {
        std::lock_guard lock(s_Mutex);
        s_History.clear();
        s_Serial++;
    }

    void SceneLoadProfiler::SetLogging(bool enabled)
    {
        std::lock_guard lock(s_Mutex);
        s_Logging = enabled;
    }

    bool SceneLoadProfiler::IsLogging()
    {
        std::lock_guard lock(s_Mutex);
        return s_Logging;
    }

}
