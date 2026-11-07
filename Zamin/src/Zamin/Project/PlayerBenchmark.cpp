#include "Zamin/Project/PlayerBenchmark.h"

#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <format>

namespace Zamin {

    PlayerBenchmark::PlayerBenchmark(PlayerBenchmarkSettings settings) : m_Settings(std::move(settings))
    {
        m_Settings.Frames = std::max(0, m_Settings.Frames);
        m_Settings.Warmup = std::max(0, m_Settings.Warmup);
        m_Frames.reserve((size_t)m_Settings.Frames);
        m_Updates.reserve((size_t)m_Settings.Frames);
    }

    bool PlayerBenchmark::AddFrame(double frameMs, double updateMs)
    {
        if (!IsEnabled() || IsDone())
            return IsDone();
        if (m_Seen++ < m_Settings.Warmup)
            return false;
        m_Frames.push_back(frameMs);
        m_Updates.push_back(updateMs);
        return IsDone();
    }

    PlayerBenchmark::Result PlayerBenchmark::GetResult() const
    {
        Result r;
        r.Warmup = std::min(m_Seen, m_Settings.Warmup);
        r.Frame = FrameTimeStats::Compute(m_Frames);
        r.Update = FrameTimeStats::Compute(m_Updates);
        r.Seconds = r.Frame.Total / 1000.0;
        r.FrameTimes = m_Frames;
        r.MaxAverageMs = m_Settings.MaxAverageMs;
        r.MaxP95Ms = m_Settings.MaxP95Ms;
        if ((int)m_Frames.size() < m_Settings.Frames)
            r.Failures.push_back(std::format("faqat {} / {} kadr o'lchandi", m_Frames.size(), m_Settings.Frames));
        if (r.MaxAverageMs > 0.0 && r.Frame.Average > r.MaxAverageMs)
            r.Failures.push_back(std::format("o'rtacha {:.2f} ms > {:.2f} ms", r.Frame.Average, r.MaxAverageMs));
        if (r.MaxP95Ms > 0.0 && r.Frame.P95 > r.MaxP95Ms)
            r.Failures.push_back(std::format("p95 {:.2f} ms > {:.2f} ms", r.Frame.P95, r.MaxP95Ms));
        r.Passed = r.Failures.empty();
        return r;
    }

    namespace {

        // Milliseconds with three decimals (the report stays readable).
        double Round(double value) { return std::isfinite(value) ? std::round(value * 1000.0) / 1000.0 : 0.0; }

        nlohmann::ordered_json StatsJson(const FrameTimeStats& s)
        {
            nlohmann::ordered_json j;
            j["Count"] = s.Count;
            j["Average"] = Round(s.Average);
            j["Median"] = Round(s.Median);
            j["P95"] = Round(s.P95);
            j["P99"] = Round(s.P99);
            j["Min"] = Round(s.Min);
            j["Max"] = Round(s.Max);
            j["StdDev"] = Round(s.StdDev);
            j["AverageFPS"] = Round(s.AverageFPS);
            j["Low1PercentFPS"] = Round(s.Low1PercentFPS);
            return j;
        }

    }

    std::string PlayerBenchmark::ToJson(const Result& r)
    {
        nlohmann::ordered_json j;
        j["Game"] = r.Game;
        j["GameVersion"] = r.GameVersion;
        j["Scene"] = r.Scene;
        j["Engine"] = r.Engine;
        j["Renderer"] = r.Renderer;
        j["Width"] = r.Width;
        j["Height"] = r.Height;
        j["Warmup"] = r.Warmup;
        j["Seconds"] = Round(r.Seconds);
        j["LoadSeconds"] = Round(r.LoadSeconds);
        j["Frame"] = StatsJson(r.Frame);
        j["Update"] = StatsJson(r.Update);
        nlohmann::ordered_json limits;
        limits["MaxAverage"] = Round(r.MaxAverageMs);
        limits["MaxP95"] = Round(r.MaxP95Ms);
        j["Limits"] = limits;
        j["Passed"] = r.Passed;
        j["Failures"] = r.Failures;
        nlohmann::json frames = nlohmann::json::array();
        for (double ms : r.FrameTimes)
            frames.push_back(Round(ms));
        j["Frames"] = frames;
        return j.dump(2) + "\n";
    }

    bool PlayerBenchmark::WriteJson(const Result& result, const std::filesystem::path& path, std::string* error)
    {
        std::error_code ec;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (FileSystem::WriteTextFile(path, ToJson(result)))
            return true;
        if (error)
            *error = std::format("{} yozilmadi", path.string());
        return false;
    }

    std::string PlayerBenchmark::Describe(const Result& r)
    {
        std::string text = std::format("Ishlash testi: {} {} - {} ({}x{}, {})\n", r.Game, r.GameVersion, r.Scene.empty() ? std::string("-") : r.Scene,
                                       r.Width, r.Height, r.Renderer.empty() ? std::string("GPU noma'lum") : r.Renderer);
        text += std::format("  {} kadr ({} ta isitish kadridan keyin), {:.2f} s; yuklanish {:.2f} s\n", r.Frame.Count, r.Warmup, r.Seconds, r.LoadSeconds);
        text += std::format("  Kadr:      {}\n", r.Frame.Format());
        text += std::format("  Yangilash: {} (o'yin yangilanishi va chizish chaqiruvlari, swap'siz)\n", r.Update.Format());
        if (r.MaxAverageMs > 0.0 || r.MaxP95Ms > 0.0)
        {
            std::string limits;
            if (r.MaxAverageMs > 0.0)
                limits += std::format("o'rtacha <= {:.2f} ms", r.MaxAverageMs);
            if (r.MaxP95Ms > 0.0)
                limits += std::format("{}p95 <= {:.2f} ms", limits.empty() ? "" : ", ", r.MaxP95Ms);
            text += std::format("  Chegaralar: {} - {}\n", limits, r.Passed ? "o'tdi" : "o'tmadi");
        }
        for (const std::string& failure : r.Failures)
            text += std::format("  XATO: {}\n", failure);
        return text;
    }

}
