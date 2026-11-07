#pragma once

#include "Zamin/Core/Log.h"

#include <optional>
#include <string>
#include <string_view>

namespace Zamin {

    // The minimum levels of the two loggers and of the log files: what is written at all, and what of it
    // goes to the files. Kept in Project settings > Player > Log (the player) and in the editor's
    // Preferences > Konsol; ZAMIN_LOG_LEVEL and --log-level (LogLevelOverride) go over them.
    struct LogLevelSettings
    {
        LogLevel Engine = LogLevel::Trace;   // ZM_CORE_* ("ZAMIN")
        LogLevel App = LogLevel::Trace;      // ZM_* ("APP": the game's scripts, the editor)
        LogLevel File = LogLevel::Trace;     // the lines written to the log files (on top of the two)

        bool operator==(const LogLevelSettings&) const = default;
        bool IsDefault() const { return *this == LogLevelSettings(); }
        LogLevel Get(LogSource source) const { return source == LogSource::Engine ? Engine : App; }
        void Set(LogSource source, LogLevel level) { (source == LogSource::Engine ? Engine : App) = level; }

        // Log::SetLevel for both loggers and Log::SetFileLevel.
        void Apply() const;
        static LogLevelSettings GetCurrent();
        // "dvigatel: Ma'lumot, o'yin: Trace, fayl: Ogohlantirish".
        std::string Describe() const;
        // "engine=info,app=trace,file=warn" (LogLevelOverride::Parse reads it back).
        std::string ToSpec() const;
    };

    // Levels from the command line (--log-level) or the environment (ZAMIN_LOG_LEVEL) that win over the
    // settings: only what they name changes.
    //     warn                          both loggers
    //     engine=error,app=info         each its own (keys: engine | zamin | core, app | game | client,
    //     file=warn                     file, all | *); separators ',' ';' or spaces, a later one wins
    struct LogLevelOverride
    {
        std::optional<LogLevel> Engine, App, File;

        bool operator==(const LogLevelOverride&) const = default;
        bool IsEmpty() const { return !Engine && !App && !File; }
        void ApplyTo(LogLevelSettings& settings) const;
        // The settings with this over them.
        LogLevelSettings Over(LogLevelSettings settings) const
        {
            ApplyTo(settings);
            return settings;
        }
        // "engine=warn,app=warn" (only what it sets; "" when empty).
        std::string ToSpec() const;

        // False (and `out` unchanged) for an unknown level or key; `error` says which, in Uzbek.
        static bool Parse(std::string_view spec, LogLevelOverride& out, std::string* error = nullptr);
        // ZAMIN_LOG_LEVEL (empty when it is not set; an invalid one is reported as a warning and ignored).
        static LogLevelOverride FromEnvironment();
    };

}
