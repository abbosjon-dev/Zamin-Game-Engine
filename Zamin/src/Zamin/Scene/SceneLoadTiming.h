#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Zamin {

    // How long a scene load took, stage by stage: reading the file, parsing its JSON (or its binary
    // cache), the format migration, making the entities, decoding the images ahead (asynchronous
    // loads, on the worker), loading the assets (textures, meshes, fonts: the GPU uploads), the audio
    // clips the scene preloads, the physics worlds and the scripts' OnCreate / OnEnable. Filled by
    // SceneSerializer / SceneLoadJob (the file's stages) and SceneManager (the rest); the editor's
    // profiler and statistics windows show the recent ones (SceneLoadProfiler) and every load is
    // logged with its breakdown.
    struct SceneLoadTiming
    {
        enum class Stage
        {
            Read = 0, Parse, Migrate, Deserialize, Decode, Assets, Audio, Physics, Scripts,
            Count
        };
        static constexpr size_t StageCount = (size_t)Stage::Count;

        // How the scene came: opened in the editor, the game's first scene (SceneManager::Start), a
        // Single / Additive switch during the game, or loaded in the background (LoadSceneAsync).
        enum class Source { Editor = 0, Start, Single, Additive, Async };

        std::string SceneName;
        std::string Path;                   // relative to the assets when it can be ("" without a file)
        Source How = Source::Single;
        bool Additive = false;              // Async: loaded next to the others
        bool Binary = false;                // read from the binary cache (.zsceneb) instead of the JSON text
        bool Failed = false;                // the load stopped (the stages up to there)
        std::array<double, StageCount> Milliseconds{};
        double WallMilliseconds = 0.0;      // from the request until the scene was in (async: across frames)
        int Frames = 0;                     // Async: the frames it took (from the request to the activation)
        size_t Entities = 0;
        size_t FileBytes = 0;

        double& operator[](Stage stage) { return Milliseconds[(size_t)stage]; }
        double Get(Stage stage) const { return Milliseconds[(size_t)stage]; }
        // Every stage added up (the worker's too).
        double TotalMilliseconds() const;
        // What the main thread was held by: an async load's worker stages (reading, parsing, the
        // migration, decoding the images) left out.
        double MainThreadMilliseconds() const;
        // The stage that took the longest.
        Stage GetSlowestStage() const;
        void Add(const SceneLoadTiming& other); // stage by stage (a job's file stages into a load)

        // "O'qish", "JSON", ... (the profiler's and the log's names; static strings).
        static const char* StageName(Stage stage);
        // What the stage is, for tooltips.
        static const char* StageDescription(Stage stage);
        static const char* SourceName(Source source);
        // A stage of an asynchronous load that runs on its worker thread (off the main thread).
        static bool IsWorkerStage(Stage stage);

        // "Level1 (yagona): 12.40 ms - o'qish 0.30, JSON 2.10, ..." (stages under 0.005 ms left out).
        std::string Format() const;
    };

    // The recent scene loads of this process (newest last), for the editor's windows and the
    // player's statistics; every Record is logged. Main thread.
    class SceneLoadProfiler
    {
    public:
        static constexpr size_t Capacity = 32;

        static void Record(const SceneLoadTiming& timing);
        static std::vector<SceneLoadTiming> GetHistory();
        // The newest (false when there is none).
        static bool GetLast(SceneLoadTiming& out);
        static size_t GetCount();
        // Grows with every Record (windows notice new loads).
        static uint64_t GetSerial();
        static void Clear();
        // Whether Record writes the breakdown to the console (on by default).
        static void SetLogging(bool enabled);
        static bool IsLogging();
    };

    // Milliseconds since `start` (steady clock): the stages' stopwatch.
    inline double ElapsedMilliseconds(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

}
