#include "Zamin/Scene/WorldTour.h"
#include "Zamin/Scene/SceneManager.h"
#include "Zamin/Scene/WorldStreaming.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Asset/AsyncLoader.h"
#include "Zamin/Utils/FileSystem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace Zamin {
    using json = nlohmann::json;

    bool WorldTour::Validate(std::string& error) const
    {
        error.clear();
        if (Name.size() > 128 || Warmup > MaxWarmup || Keys.empty() || Keys.size() > MaxKeys || Keys.front().Frame != 0 || Keys.back().Frame > MaxFrames)
        {
            error = "Invalid world tour: 1..1024 keys, the first at frame 0, at most 1000000 frames";
            return false;
        }
        for (size_t i = 0; i < Keys.size(); ++i)
        {
            const auto& p = Keys[i].Position;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || std::abs(p.x) > 1e6 || std::abs(p.y) > 1e6 || std::abs(p.z) > 1e6 ||
                (i && Keys[i].Frame <= Keys[i - 1].Frame))
            {
                error = "Invalid world tour key: frames must increase and positions stay within +/-1000000";
                return false;
            }
        }
        return true;
    }
    glm::dvec3 WorldTour::Evaluate(uint32_t frame) const
    {
        if (Keys.empty()) return glm::dvec3(0);
        // The first key after `frame`; the one before it is where the camera comes from.
        const auto next = std::upper_bound(Keys.begin(), Keys.end(), frame, [](uint32_t f, const Key& key) { return f < key.Frame; });
        if (next == Keys.begin()) return Keys.front().Position;
        const auto& from = *(next - 1);
        if (next == Keys.end() || next->Jump) return from.Position;
        const double t = double(frame - from.Frame) / double(next->Frame - from.Frame);
        return from.Position + (next->Position - from.Position) * t;
    }
    bool WorldTour::Serialize(std::string& text, std::string& error) const
    {
        if (!Validate(error)) return false;
        json keys = json::array();
        for (const auto& key : Keys)
            keys.push_back({ { "Frame", key.Frame }, { "Position", { key.Position.x, key.Position.y, key.Position.z } }, { "Jump", key.Jump } });
        text = json{ { "Format", "ZaminWorldTour" }, { "Version", 1 }, { "Name", Name }, { "Warmup", Warmup }, { "Keys", keys } }.dump(2);
        return true;
    }
    bool WorldTour::Deserialize(const std::string& text, std::string& error)
    {
        if (text.size() > MaxFileBytes)
        {
            error = "World tour file is too large";
            return false;
        }
        try
        {
            const auto j = json::parse(text);
            if (!j.is_object() || j.value("Format", std::string()) != "ZaminWorldTour" || !j.contains("Version") || !j.at("Version").is_number_unsigned() || j.at("Version").get<uint64_t>() != 1)
                throw std::runtime_error("Not a version 1 ZaminWorldTour file");
            WorldTour next;
            if (j.contains("Name")) next.Name = j.at("Name").get<std::string>();
            auto frames = [](const json& value, uint32_t limit) {
                if (!value.is_number_unsigned() || value.get<uint64_t>() > limit) throw std::runtime_error("Invalid world tour frame number");
                return value.get<uint32_t>();
            };
            if (j.contains("Warmup")) next.Warmup = frames(j.at("Warmup"), MaxWarmup);
            const auto& keys = j.at("Keys");
            if (!keys.is_array() || keys.size() > MaxKeys) throw std::runtime_error("Invalid world tour keys");
            for (const auto& item : keys)
            {
                Key key;
                key.Frame = frames(item.at("Frame"), MaxFrames);
                const auto& position = item.at("Position");
                if (!position.is_array() || position.size() != 3) throw std::runtime_error("Invalid world tour position");
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!position[axis].is_number()) throw std::runtime_error("Invalid world tour position");
                    key.Position[axis] = position[axis].get<double>();
                }
                if (item.contains("Jump"))
                {
                    if (!item.at("Jump").is_boolean()) throw std::runtime_error("Invalid world tour jump flag");
                    key.Jump = item.at("Jump").get<bool>();
                }
                next.Keys.push_back(key);
            }
            if (!next.Validate(error)) return false;
            *this = std::move(next);
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
    bool WorldTour::Load(const std::filesystem::path& file, WorldTour& tour, std::string& error)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec) || std::filesystem::file_size(file, ec) > MaxFileBytes || ec)
        {
            error = "World tour must be a regular file of at most 1 MiB";
            return false;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            error = "Cannot read the world tour";
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return tour.Deserialize(text, error);
    }
    bool WorldTour::Save(const std::filesystem::path& file, const WorldTour& tour, std::string& error)
    {
        std::string text;
        if (file.extension() != ".ztour")
        {
            error = "World tours must use .ztour";
            return false;
        }
        if (!tour.Serialize(text, error)) return false;
        if (!FileSystem::WriteTextFileAtomic(file, text))
        {
            error = "Cannot save the world tour";
            return false;
        }
        return true;
    }

    void WorldTourReport::Add(const Sample& sample)
    {
        if (m_Samples.size() <= WorldTour::MaxFrames) m_Samples.push_back(sample);
    }
    WorldTourReport::Distribution WorldTourReport::Distribute(std::vector<double> values)
    {
        Distribution out;
        if (values.empty()) return out;
        std::sort(values.begin(), values.end());
        double sum = 0;
        for (double value : values) sum += value;
        // Nearest rank: the smallest value with at least that share of the samples at or below it.
        auto rank = [&](double share) { return values[std::min(values.size() - 1, size_t(std::ceil(share * double(values.size()))) - 1)]; };
        out.Average = sum / double(values.size());
        out.P50 = rank(.5);
        out.P95 = rank(.95);
        out.P99 = rank(.99);
        out.Max = values.back();
        return out;
    }
    WorldTourReport::Summary WorldTourReport::Summarize() const
    {
        Summary out;
        out.Frames = m_Samples.size();
        if (m_Samples.empty()) return out;
        std::vector<double> frame, world, upload;
        for (const auto& s : m_Samples)
        {
            frame.push_back(s.FrameMs);
            world.push_back(s.WorldMs);
            upload.push_back(s.UploadMs);
            out.PeakUploadBytes = std::max(out.PeakUploadBytes, s.UploadBytes);
            out.PeakUploadCalls = std::max(out.PeakUploadCalls, s.UploadCalls);
            out.PeakRSS = std::max(out.PeakRSS, s.RSS);
            out.PeakGPU = std::max(out.PeakGPU, s.GPU);
            out.PeakStagedBytes = std::max(out.PeakStagedBytes, s.StagedBytes);
            out.PeakActive = std::max(out.PeakActive, s.Active);
            out.PeakAdmitted = std::max(out.PeakAdmitted, s.Admitted);
            out.PeakFailed = std::max(out.PeakFailed, s.Failed);
            out.PressureFrames += s.Pressure;
        }
        out.FrameMs = Distribute(std::move(frame));
        out.WorldMs = Distribute(std::move(world));
        out.UploadMs = Distribute(std::move(upload));
        out.FirstRSS = m_Samples.front().RSS;
        out.LastRSS = m_Samples.back().RSS;
        out.FirstGPU = m_Samples.front().GPU;
        out.LastGPU = m_Samples.back().GPU;
        return out;
    }

    bool WorldTourRunner::Start(WorldTour tour, std::string& error)
    {
        if (!tour.Validate(error)) return false;
        m_Tour = std::move(tour);
        m_Report = {};
        m_Active = true;
        m_Finished = false;
        m_Tick = 0;
        const auto uploads = AsyncLoader::GetStats();
        m_UploadBytes = uploads.UploadBytes;
        m_UploadCalls = uploads.UploadCalls;
        m_UploadMs = uploads.UploadMilliseconds + uploads.FinishMilliseconds;
        return true;
    }
    void WorldTourRunner::BeginFrame(SceneManager& scenes, double previousFrameMs)
    {
        if (!m_Active || m_Finished) return;
        const auto uploads = AsyncLoader::GetStats();
        const double uploadMs = uploads.UploadMilliseconds + uploads.FinishMilliseconds;
        // The frame that just ended was tour frame m_Tick - 1 - Warmup.
        if (m_Tick > m_Tour.Warmup)
        {
            WorldTourReport::Sample sample;
            sample.Frame = m_Tick - 1 - m_Tour.Warmup;
            sample.FrameMs = previousFrameMs;
            sample.UploadMs = std::max(0.0, uploadMs - m_UploadMs);
            sample.UploadBytes = uploads.UploadBytes - std::min(uploads.UploadBytes, m_UploadBytes);
            sample.UploadCalls = uint32_t(uploads.UploadCalls - std::min(uploads.UploadCalls, m_UploadCalls));
            if (const auto* world = scenes.GetWorldStreaming())
            {
                const auto stats = world->GetStats();
                sample.WorldMs = stats.LastMilliseconds;
                sample.RSS = stats.Residency.Process.Valid ? stats.Residency.Process.Resident : 0;
                sample.GPU = stats.Residency.RequestedGPUBytes;
                sample.StagedBytes = stats.Assets.ReservedBytes;
                sample.Active = uint32_t(stats.Active);
                sample.Admitted = uint32_t(stats.Admitted);
                sample.Building = uint32_t(stats.Building);
                sample.Failed = uint32_t(stats.Failed);
                sample.Pressure = stats.MemoryPressure;
            }
            m_Report.Add(sample);
            if (sample.Frame >= m_Tour.GetLength())
            {
                m_Finished = true;
                return;
            }
        }
        m_UploadBytes = uploads.UploadBytes;
        m_UploadCalls = uploads.UploadCalls;
        m_UploadMs = uploadMs;
        const uint32_t frame = m_Tick > m_Tour.Warmup ? m_Tick - m_Tour.Warmup : 0;
        ++m_Tick;
        if (auto* scene = scenes.GetRenderScene())
            if (auto camera = scene->GetPrimaryCameraEntity()) camera.GetTransform().Translation = glm::vec3(m_Tour.Evaluate(frame));
    }
    bool WorldTourRunner::WriteReport(const std::filesystem::path& file, SceneManager* scenes, std::string& error) const
    {
        const auto summary = m_Report.Summarize();
        auto distribution = [](const WorldTourReport::Distribution& d) {
            return json{ { "Average", d.Average }, { "P50", d.P50 }, { "P95", d.P95 }, { "P99", d.P99 }, { "Max", d.Max } };
        };
        json j = { { "Format", "ZaminWorldTourReport" }, { "Version", 1 }, { "Tour", m_Tour.Name }, { "TourFrames", m_Tour.GetLength() + 1 },
                   { "Warmup", m_Tour.Warmup }, { "Complete", m_Finished } };
        j["Summary"] = { { "Frames", summary.Frames }, { "FrameMs", distribution(summary.FrameMs) }, { "WorldMs", distribution(summary.WorldMs) },
                         { "UploadMs", distribution(summary.UploadMs) }, { "PeakUploadBytesPerFrame", summary.PeakUploadBytes },
                         { "PeakUploadCallsPerFrame", summary.PeakUploadCalls }, { "PeakRSSBytes", summary.PeakRSS }, { "FirstRSSBytes", summary.FirstRSS },
                         { "LastRSSBytes", summary.LastRSS }, { "PeakRequestedGPUBytes", summary.PeakGPU }, { "FirstRequestedGPUBytes", summary.FirstGPU },
                         { "LastRequestedGPUBytes", summary.LastGPU }, { "PeakStagedBytes", summary.PeakStagedBytes }, { "PeakActive", summary.PeakActive },
                         { "PeakAdmitted", summary.PeakAdmitted }, { "PeakFailed", summary.PeakFailed }, { "PressureFrames", summary.PressureFrames } };
        if (const auto* world = scenes ? scenes->GetWorldStreaming() : nullptr)
        {
            const auto stats = world->GetStats();
            const auto& p = world->GetPolicy();
            j["Policy"] = { { "AssetStreaming", p.AssetStreaming }, { "MainThreadMilliseconds", p.MainThreadMilliseconds }, { "MaxResidentCells", p.MaxResidentCells },
                            { "MaxAssetUploadsPerFrame", p.MaxAssetUploadsPerFrame }, { "MaxAssetUploadBytesPerFrame", p.MaxAssetUploadBytesPerFrame },
                            { "MaxAssetFileBytes", p.MaxAssetFileBytes }, { "MaxAssetBytesPerCell", p.MaxAssetBytesPerCell }, { "MaxStagedAssetBytes", p.MaxStagedAssetBytes },
                            { "ObservedRSSLimitBytes", p.ObservedRSSLimitBytes }, { "RequestedGPULimitBytes", p.RequestedGPULimitBytes },
                            { "EvictOnPressure", p.EvictOnPressure }, { "CacheEntriesPerKind", p.CacheEntriesPerKind } };
            j["World"] = { { "Cells", world->GetManifest().Cells.size() }, { "Activations", stats.Activations }, { "Unloads", stats.Unloads }, { "Failed", stats.Failed },
                           { "CacheEvictions", stats.CacheEvictions }, { "PressureFrames", stats.PressureFrames }, { "PressureEvictions", stats.PressureEvictions },
                           { "ResidencyBlocked", stats.ResidencyBlocked }, { "PeakWorldMs", stats.PeakMilliseconds },
                           { "MainThreadImageDecodes", stats.MainThreadImageDecodes }, { "MainThreadModelParses", stats.MainThreadModelParses },
                           { "AssetCancellations", stats.AssetCancellations }, { "AssetRestages", stats.AssetRestages },
                           { "FilesStaged", stats.Assets.FilesStaged }, { "FilesUnreadable", stats.Assets.FilesUnreadable }, { "StagedBytes", stats.Assets.StagedBytes },
                           { "PeakReservedStagedBytes", stats.Assets.PeakReservedBytes }, { "StagingBudgetFailures", stats.Assets.BudgetFailures },
                           { "StagingWorkerMs", stats.Assets.WorkerMilliseconds }, { "UploadBytes", stats.Uploads.UploadBytes }, { "Uploads", stats.Uploads.Uploads },
                           { "OversizeUploads", stats.Uploads.OversizeUploads }, { "UploadBudgetDeferrals", stats.Uploads.BudgetDeferrals } };
        }
        j["Columns"] = { "Frame", "FrameMs", "WorldMs", "UploadMs", "UploadBytes", "UploadCalls", "RSSBytes", "RequestedGPUBytes", "StagedBytes", "Active", "Admitted", "Building", "Failed", "Pressure" };
        json rows = json::array();
        auto round = [](double value) { return std::round(value * 1000.0) / 1000.0; };
        for (const auto& s : m_Report.GetSamples())
            rows.push_back({ s.Frame, round(s.FrameMs), round(s.WorldMs), round(s.UploadMs), s.UploadBytes, s.UploadCalls, s.RSS, s.GPU, s.StagedBytes, s.Active, s.Admitted, s.Building, s.Failed, s.Pressure ? 1 : 0 });
        j["Samples"] = std::move(rows);
        if (!FileSystem::WriteTextFileAtomic(file, j.dump(1)))
        {
            error = "Cannot write the world tour report";
            return false;
        }
        return true;
    }
}
