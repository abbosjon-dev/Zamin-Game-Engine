#pragma once

#include "Zamin/Core/Base.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Zamin {
    class SceneManager;

    // A repeatable camera path through a streaming world (.ztour): camera positions at frame numbers.
    // The position depends on the frame number alone - never on time - so two runs ask the streaming
    // runtime for the same cells in the same frames. Bounded, validated, saved atomically.
    struct WorldTour
    {
        static constexpr size_t MaxKeys = 1024, MaxFileBytes = 1024 * 1024;
        static constexpr uint32_t MaxFrames = 1000000, MaxWarmup = 10000;
        struct Key
        {
            uint32_t Frame = 0;
            glm::dvec3 Position{ 0 };
            // The camera is moved here at once in this frame (a teleport) instead of travelling from the
            // previous key - fast camera movement and returns are written with it.
            bool Jump = false;
        };
        std::string Name;
        // Frames before frame 0: the camera rests at the first key and nothing is recorded (start-up work).
        uint32_t Warmup = 0;
        std::vector<Key> Keys; // strictly increasing frames, the first at 0

        bool Validate(std::string& error) const;
        uint32_t GetLength() const { return Keys.empty() ? 0 : Keys.back().Frame; } // the last key's frame
        // Linear between keys, the last key's position from its frame on.
        glm::dvec3 Evaluate(uint32_t frame) const;
        bool Serialize(std::string& text, std::string& error) const;
        // The tour is unchanged when the text is refused.
        bool Deserialize(const std::string& text, std::string& error);
        static bool Load(const std::filesystem::path& file, WorldTour& tour, std::string& error);
        static bool Save(const std::filesystem::path& file, const WorldTour& tour, std::string& error);
    };

    // What a tour measured: one sample per frame and their summary (JSON). Bounded by WorldTour::MaxFrames.
    class WorldTourReport
    {
    public:
        struct Sample
        {
            uint32_t Frame = 0;
            double FrameMs = 0;  // the whole frame, start to start
            double WorldMs = 0;  // WorldStreaming's update on the main thread
            double UploadMs = 0; // AsyncLoader::Update: finishes and GPU uploads on the main thread
            uint64_t UploadBytes = 0;
            uint32_t UploadCalls = 0;
            uint64_t RSS = 0, GPU = 0; // observed resident memory, tracked GPU requests
            uint64_t StagedBytes = 0;  // decoded data charged to the staging budget
            uint32_t Active = 0, Admitted = 0, Building = 0, Failed = 0;
            bool Pressure = false;
        };
        struct Distribution
        {
            double Average = 0, P50 = 0, P95 = 0, P99 = 0, Max = 0;
        };
        struct Summary
        {
            size_t Frames = 0;
            Distribution FrameMs, WorldMs, UploadMs;
            uint64_t PeakUploadBytes = 0, PeakRSS = 0, FirstRSS = 0, LastRSS = 0, PeakGPU = 0, FirstGPU = 0, LastGPU = 0, PeakStagedBytes = 0;
            uint32_t PeakUploadCalls = 0, PeakActive = 0, PeakAdmitted = 0, PeakFailed = 0;
            size_t PressureFrames = 0;
        };
        void Add(const Sample& sample);
        const std::vector<Sample>& GetSamples() const { return m_Samples; }
        Summary Summarize() const;
        static Distribution Distribute(std::vector<double> values);

    private:
        std::vector<Sample> m_Samples;
    };

    // Drives a tour for a host (the Player): once a frame, before the scenes update, it records the
    // frame that just ended and moves the primary camera of the scene that draws (a root entity) to the
    // tour's position. The streaming viewer follows the camera as always.
    class WorldTourRunner
    {
    public:
        bool Start(WorldTour tour, std::string& error);
        bool IsActive() const { return m_Active; }
        bool IsFinished() const { return m_Finished; }
        // `previousFrameMs`: the whole time of the frame that just ended.
        void BeginFrame(SceneManager& scenes, double previousFrameMs);
        const WorldTour& GetTour() const { return m_Tour; }
        const WorldTourReport& GetReport() const { return m_Report; }
        // The report with the world's policy and final counters. `complete` false: the run ended early.
        bool WriteReport(const std::filesystem::path& file, SceneManager* scenes, std::string& error) const;

    private:
        WorldTour m_Tour;
        WorldTourReport m_Report;
        bool m_Active = false, m_Finished = false;
        uint32_t m_Tick = 0; // host frames since Start, warm-up included
        uint64_t m_UploadBytes = 0, m_UploadCalls = 0;
        double m_UploadMs = 0;
    };
}
