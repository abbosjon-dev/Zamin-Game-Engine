#include "Zamin/Renderer/GPUProfiler.h"
#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace Zamin {
    namespace {
        struct Slot
        {
            GLuint Begin = 0, End = 0;
            uint64_t Serial = 0;
            bool Pending = false;
        };
        std::array<Slot, 8> s_Slots;
        std::array<double, 120> s_History{};
        size_t s_HistoryCount = 0, s_HistoryNext = 0;
        int s_Active = -1;
        bool s_Initialized = false;
        GPUFrameStats s_Stats;
        uint64_t s_Serial = 0, s_LastSerial = 0;
        void Poll()
        {
            s_Stats.PendingQueries = 0;
            for (auto& slot : s_Slots)
            {
                if (!slot.Pending) continue;
                GLint ready = 0;
                glGetQueryObjectiv(slot.End, GL_QUERY_RESULT_AVAILABLE, &ready);
                if (!ready)
                {
                    ++s_Stats.PendingQueries;
                    continue;
                }
                GLuint64 begin = 0, end = 0;
                glGetQueryObjectui64v(slot.Begin, GL_QUERY_RESULT, &begin);
                glGetQueryObjectui64v(slot.End, GL_QUERY_RESULT, &end);
                slot.Pending = false;
                if (end < begin)
                {
                    ++s_Stats.DroppedSamples;
                    continue;
                }
                const double ms = double(end - begin) / 1e6;
                if (!std::isfinite(ms)) continue;
                if (slot.Serial > s_LastSerial)
                {
                    s_Stats.LastMilliseconds = ms;
                    s_LastSerial = slot.Serial;
                }
                ++s_Stats.CompletedSamples;
                s_History[s_HistoryNext] = ms;
                s_HistoryNext = (s_HistoryNext + 1) % s_History.size();
                s_HistoryCount = std::min(s_HistoryCount + 1, s_History.size());
                double total = 0;
                s_Stats.MinimumMilliseconds = ms;
                s_Stats.MaximumMilliseconds = ms;
                for (size_t i = 0; i < s_HistoryCount; ++i)
                {
                    total += s_History[i];
                    s_Stats.MinimumMilliseconds = std::min(s_Stats.MinimumMilliseconds, s_History[i]);
                    s_Stats.MaximumMilliseconds = std::max(s_Stats.MaximumMilliseconds, s_History[i]);
                }
                s_Stats.AverageMilliseconds = total / double(s_HistoryCount);
            }
        }
    }
    void GPUProfiler::BeginFrame()
    {
        if (s_Active >= 0) return;
        if (!s_Initialized)
        {
            s_Initialized = true;
            if (!glGetQueryiv || !glGetQueryObjectui64v || !glQueryCounter) return;
            GLint bits = 0;
            glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
            s_Stats.Supported = bits > 0;
            if (s_Stats.Supported)
                for (auto& slot : s_Slots)
                {
                    glGenQueries(1, &slot.Begin);
                    glGenQueries(1, &slot.End);
                }
        }
        if (!s_Stats.Supported) return;
        Poll();
        if (!s_Stats.Enabled) return;
        for (size_t i = 0; i < s_Slots.size(); ++i)
            if (!s_Slots[i].Pending)
            {
                s_Active = int(i);
                s_Slots[i].Serial = ++s_Serial;
                glQueryCounter(s_Slots[i].Begin, GL_TIMESTAMP);
                return;
            }
        ++s_Stats.DroppedSamples;
    }
    void GPUProfiler::EndFrame()
    {
        if (s_Active < 0) return;
        glQueryCounter(s_Slots[size_t(s_Active)].End, GL_TIMESTAMP);
        s_Slots[size_t(s_Active)].Pending = true;
        ++s_Stats.PendingQueries;
        s_Active = -1;
    }
    void GPUProfiler::SetEnabled(bool enabled) { s_Stats.Enabled = enabled; }
    const GPUFrameStats& GPUProfiler::GetStats() { return s_Stats; }
    void GPUProfiler::Shutdown()
    {
        if (s_Active >= 0) EndFrame();
        if (s_Stats.Supported)
            for (auto& slot : s_Slots)
            {
                if (slot.Begin) glDeleteQueries(1, &slot.Begin);
                if (slot.End) glDeleteQueries(1, &slot.End);
            }
        s_Slots = {};
        s_History = {};
        s_HistoryCount = s_HistoryNext = 0;
        s_Active = -1;
        s_Initialized = false;
        s_Stats = {};
        s_Serial = s_LastSerial = 0;
    }
}
