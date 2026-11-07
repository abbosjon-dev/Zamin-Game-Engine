#include "Zamin/Core/Time.h"

#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Zamin {

    using ordered_json = nlohmann::ordered_json;

    // ------------------------------------------------------------------ settings
    void TimeSettings::Sanitize()
    {
        FixedStepRate = std::clamp(FixedStepRate, MinStepRate, MaxStepRate);
        MaxStepsPerFrame = std::clamp(MaxStepsPerFrame, 1, MaxStepsLimit);
        DefaultTimeScale = std::isfinite(DefaultTimeScale) ? std::clamp(DefaultTimeScale, 0.0f, MaxTimeScale) : 1.0f;
        MaxDeltaTime = std::isfinite(MaxDeltaTime) ? std::clamp(MaxDeltaTime, MinMaxDeltaTime, MaxMaxDeltaTime) : 0.1f;
    }

    const std::vector<int>& TimeSettings::GetStepRatePresets()
    {
        static const std::vector<int> s_Presets = { 50, 60, 100, 120, 144, 240 };
        return s_Presets;
    }

    std::string TimeSettings::ToJson() const
    {
        ordered_json j;
        j["FixedStepRate"] = FixedStepRate;
        j["MaxStepsPerFrame"] = MaxStepsPerFrame;
        j["DefaultTimeScale"] = DefaultTimeScale;
        j["MaxDeltaTime"] = MaxDeltaTime;
        return j.dump(4);
    }

    bool TimeSettings::FromJson(const std::string& text, TimeSettings& out)
    {
        ordered_json j = ordered_json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        TimeSettings s;
        auto readInt = [&](const char* key, int& value) {
            if (auto it = j.find(key); it != j.end() && it->is_number())
                value = (int)std::clamp(std::round(it->get<double>()), -1.0e6, 1.0e6);
        };
        auto readFloat = [&](const char* key, float& value) {
            if (auto it = j.find(key); it != j.end() && it->is_number())
                value = (float)it->get<double>();
        };
        readInt("FixedStepRate", s.FixedStepRate);
        readInt("MaxStepsPerFrame", s.MaxStepsPerFrame);
        readFloat("DefaultTimeScale", s.DefaultTimeScale);
        readFloat("MaxDeltaTime", s.MaxDeltaTime);
        s.Sanitize();
        out = s;
        return true;
    }

    // ------------------------------------------------------------------ clock
    namespace {
        const std::chrono::steady_clock::time_point s_Startup = std::chrono::steady_clock::now();
        Scene* s_GameClock = nullptr;
        float s_FrameDelta = 0.0f;
        uint64_t s_FrameCount = 0;
    }

    float Time::GetDeltaTime() { return s_GameClock ? s_GameClock->GetDeltaTime() : s_FrameDelta; }

    float Time::GetUnscaledDeltaTime() { return s_GameClock ? s_GameClock->GetUnscaledDeltaTime() : s_FrameDelta; }

    float Time::GetFixedDeltaTime()
    {
        if (s_GameClock)
        {
            SceneSettings::PhysicsStepSettings step = s_GameClock->GetSettings().PhysicsStep;
            step.Sanitize();
            return step.GetFixedStep();
        }
        return GetSettings().GetFixedDeltaTime();
    }

    float Time::GetTime() { return s_GameClock ? s_GameClock->GetTime() : (float)GetTimeSinceStartup(); }

    float Time::GetUnscaledTime() { return s_GameClock ? s_GameClock->GetUnscaledTime() : (float)GetTimeSinceStartup(); }

    double Time::GetTimeSinceStartup()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - s_Startup).count();
    }

    uint64_t Time::GetFrameCount() { return s_FrameCount; }

    void Time::SetTimeScale(float scale)
    {
        if (!s_GameClock)
            return;
        scale = std::isfinite(scale) ? std::clamp(scale, 0.0f, TimeSettings::MaxTimeScale) : 1.0f;
        // Every scene of the game runs on one clock.
        if (SceneManager* manager = s_GameClock->GetSceneManager())
            manager->SetTimeScale(scale);
        else
            s_GameClock->SetTimeScale(scale);
    }

    float Time::GetTimeScale() { return s_GameClock ? s_GameClock->GetTimeScale() : 1.0f; }

    void Time::SetPaused(bool paused)
    {
        if (!s_GameClock)
            return;
        if (SceneManager* manager = s_GameClock->GetSceneManager())
            manager->SetGamePaused(paused);
        else
            s_GameClock->SetGamePaused(paused);
    }

    bool Time::IsPaused() { return s_GameClock && s_GameClock->IsGamePaused(); }

    const TimeSettings& Time::GetSettings() { return Project::GetTimeSettings(); }

    void Time::OnFrame(float unscaledDelta)
    {
        s_FrameDelta = std::max(0.0f, unscaledDelta);
        s_FrameCount++;
    }

    void Time::SetGameClock(Scene* scene) { s_GameClock = scene; }

    void Time::ClearGameClock(const Scene* scene)
    {
        if (s_GameClock == scene)
            s_GameClock = nullptr;
    }

    Scene* Time::GetGameClock() { return s_GameClock; }

}
