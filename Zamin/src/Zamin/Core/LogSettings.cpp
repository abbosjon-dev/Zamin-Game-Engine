#include "Zamin/Core/LogSettings.h"

#include "Zamin/Utils/StringUtils.h"

#include <cstdlib>

namespace Zamin {

    void LogLevelSettings::Apply() const
    {
        Log::SetLevel(LogSource::Engine, Engine);
        Log::SetLevel(LogSource::App, App);
        Log::SetFileLevel(File);
    }

    LogLevelSettings LogLevelSettings::GetCurrent()
    {
        LogLevelSettings s;
        s.Engine = Log::GetLevel(LogSource::Engine);
        s.App = Log::GetLevel(LogSource::App);
        s.File = Log::GetFileLevel();
        return s;
    }

    std::string LogLevelSettings::Describe() const
    {
        return std::format("dvigatel: {}, o'yin: {}, fayl: {}", Log::LevelDisplayName(Engine), Log::LevelDisplayName(App), Log::LevelDisplayName(File));
    }

    std::string LogLevelSettings::ToSpec() const
    {
        return std::format("engine={},app={},file={}", Log::LevelToName(Engine), Log::LevelToName(App), Log::LevelToName(File));
    }

    void LogLevelOverride::ApplyTo(LogLevelSettings& settings) const
    {
        if (Engine)
            settings.Engine = *Engine;
        if (App)
            settings.App = *App;
        if (File)
            settings.File = *File;
    }

    std::string LogLevelOverride::ToSpec() const
    {
        std::string out;
        auto add = [&](const char* key, const std::optional<LogLevel>& level) {
            if (!level)
                return;
            if (!out.empty())
                out += ',';
            out += std::format("{}={}", key, Log::LevelToName(*level));
        };
        add("engine", Engine);
        add("app", App);
        add("file", File);
        return out;
    }

    bool LogLevelOverride::Parse(std::string_view spec, LogLevelOverride& out, std::string* error)
    {
        auto fail = [&](std::string message) {
            if (error)
                *error = std::move(message);
            return false;
        };
        LogLevelOverride result = out;
        const std::vector<std::string_view> entries = StringUtils::SplitAnyView(spec, ",; \t", StringUtils::SplitFlags::SkipEmpty);
        if (entries.empty())
            return fail("log darajasi ko'rsatilmagan (masalan: warn yoki engine=warn,app=info,file=error)");
        for (std::string_view entry : entries)
        {
            std::string_view key = "all", value = entry;
            if (std::string_view before, after; StringUtils::SplitOnce(entry, '=', before, after) ||
                                                StringUtils::SplitOnce(entry, ':', before, after))
            {
                key = StringUtils::Trim(before);
                value = StringUtils::Trim(after);
            }
            const std::optional<LogLevel> level = Log::ParseLevel(value);
            if (!level)
                return fail(std::format("'{}': noma'lum log darajasi (trace, info, warn, error, critical, off)", value));
            if (StringUtils::EqualsIgnoreCase(key, "all") || key == "*")
                result.Engine = result.App = *level;
            else if (StringUtils::EqualsIgnoreCase(key, "engine") || StringUtils::EqualsIgnoreCase(key, "zamin") ||
                     StringUtils::EqualsIgnoreCase(key, "core") || StringUtils::EqualsIgnoreCase(key, "dvigatel"))
                result.Engine = *level;
            else if (StringUtils::EqualsIgnoreCase(key, "app") || StringUtils::EqualsIgnoreCase(key, "game") ||
                     StringUtils::EqualsIgnoreCase(key, "client") || StringUtils::EqualsIgnoreCase(key, "o'yin"))
                result.App = *level;
            else if (StringUtils::EqualsIgnoreCase(key, "file") || StringUtils::EqualsIgnoreCase(key, "fayl"))
                result.File = *level;
            else
                return fail(std::format("'{}': noma'lum logger (engine, app, file yoki all)", key));
        }
        out = result;
        return true;
    }

    LogLevelOverride LogLevelOverride::FromEnvironment()
    {
        LogLevelOverride result;
        const char* text = std::getenv("ZAMIN_LOG_LEVEL");
        if (!text || !*text)
            return result;
        std::string error;
        if (!Parse(text, result, &error))
        {
            ZM_CORE_WARN("ZAMIN_LOG_LEVEL={}: {} - e'tiborsiz qoldirildi", text, error);
            return {};
        }
        return result;
    }

}
