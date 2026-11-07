#include "Zamin/Core/Stopwatch.h"

#include "Zamin/Utils/StringUtils.h"

namespace Zamin {

    std::string Stopwatch::Format() const
    {
        return StringUtils::FormatDuration(ElapsedSeconds());
    }

    ScopedLogTimer::~ScopedLogTimer()
    {
        if (m_Cancelled)
            return;
        const double ms = m_Watch.ElapsedMilliseconds();
        if (m_Output)
        {
            *m_Output = ms;
            return;
        }
        if (ms < m_Threshold || !Log::IsEnabled(m_Level, m_FromClient))
            return;
        const std::string took = StringUtils::FormatDuration(ms / 1000.0);
        Log::Write(m_Level, m_FromClient,
                   m_Threshold > 0.0 ? std::format("{}: {} (chegara {})", m_Name, took, StringUtils::FormatDuration(m_Threshold / 1000.0))
                                     : std::format("{}: {}", m_Name, took));
    }

}
