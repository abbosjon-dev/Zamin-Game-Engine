#include "TestFramework.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>

namespace ZaminTest {

    namespace {

        constexpr size_t MaxMessagesPerTest = 20;

        // The test that runs now (its failed checks' texts go to its result).
        TestResult* s_Current = nullptr;

        char Lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

        bool HasWildcard(std::string_view pattern) { return pattern.find_first_of("*?") != std::string_view::npos; }

        // '*' and '?' over the whole text (the usual greedy match that goes back to the last '*').
        bool Glob(std::string_view pattern, std::string_view text)
        {
            size_t p = 0, t = 0, star = std::string_view::npos, resume = 0;
            while (t < text.size())
            {
                if (p < pattern.size() && (pattern[p] == '?' || (pattern[p] != '*' && Lower(pattern[p]) == Lower(text[t]))))
                {
                    p++;
                    t++;
                }
                else if (p < pattern.size() && pattern[p] == '*')
                {
                    star = p++;
                    resume = t;
                }
                else if (star != std::string_view::npos)
                {
                    p = star + 1;
                    t = ++resume;
                }
                else
                    return false;
            }
            while (p < pattern.size() && pattern[p] == '*')
                p++;
            return p == pattern.size();
        }

        bool Contains(std::string_view text, std::string_view part)
        {
            if (part.empty())
                return true;
            if (part.size() > text.size())
                return false;
            for (size_t i = 0; i + part.size() <= text.size(); i++)
            {
                size_t j = 0;
                while (j < part.size() && Lower(text[i + j]) == Lower(part[j]))
                    j++;
                if (j == part.size())
                    return true;
            }
            return false;
        }

        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        }

        std::string JsonEscape(std::string_view s)
        {
            std::string out;
            out.reserve(s.size() + 2);
            for (char c : s)
            {
                switch (c)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if ((unsigned char)c < 0x20)
                            out += std::format("\\u{:04x}", (unsigned)(unsigned char)c);
                        else
                            out += c;
                }
            }
            return out;
        }

        std::string XmlEscape(std::string_view s)
        {
            std::string out;
            out.reserve(s.size() + 8);
            for (char c : s)
            {
                switch (c)
                {
                    case '&': out += "&amp;"; break;
                    case '<': out += "&lt;"; break;
                    case '>': out += "&gt;"; break;
                    case '"': out += "&quot;"; break;
                    case '\'': out += "&apos;"; break;
                    default:
                        // XML 1.0 has no other control characters than tab and the line ends.
                        if ((unsigned char)c < 0x20 && c != '\n' && c != '\r' && c != '\t')
                            out += ' ';
                        else
                            out += c;
                }
            }
            return out;
        }

        std::string NowIso8601()
        {
            const std::time_t now = std::time(nullptr);
            std::tm utc{};
            gmtime_r(&now, &utc);
            char text[32];
            std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
            return text;
        }

        bool ParseCount(std::string_view text, int& out)
        {
            if (text.empty() || text.size() > 6)
                return false;
            int value = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                    return false;
                value = value * 10 + (c - '0');
            }
            if (value < 1)
                return false;
            out = value;
            return true;
        }

        bool WriteFile(const std::string& path, const std::string& text)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path file(path);
            if (file.has_parent_path())
                fs::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out << text;
            return (bool)out;
        }

    }

    std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    int& FailureCounter()
    {
        static int failures = 0;
        return failures;
    }

    void Fail(std::string_view message)
    {
        std::printf("    FAIL %.*s\n", (int)message.size(), message.data());
        FailureCounter()++;
        if (s_Current && s_Current->Messages.size() < MaxMessagesPerTest)
            s_Current->Messages.emplace_back(message);
    }

    void CheckFailed(const char* file, int line, const char* expression)
    {
        Fail(std::format("{}:{}: {}", file, line, expression));
    }

    bool MatchPattern(std::string_view pattern, std::string_view name)
    {
        return HasWildcard(pattern) ? Glob(pattern, name) : Contains(name, pattern);
    }

    void TestFilter::Add(std::string_view spec)
    {
        size_t start = 0;
        while (start <= spec.size())
        {
            size_t end = spec.find_first_of(":,", start);
            if (end == std::string_view::npos)
                end = spec.size();
            std::string_view token = Trim(spec.substr(start, end - start));
            if (!token.empty() && token.front() == '-')
                Exclude(token.substr(1));
            else if (!token.empty())
                m_Include.emplace_back(token);
            start = end + 1;
        }
    }

    void TestFilter::Exclude(std::string_view pattern)
    {
        pattern = Trim(pattern);
        if (!pattern.empty())
            m_Exclude.emplace_back(pattern);
    }

    bool TestFilter::Matches(std::string_view name) const
    {
        bool included = m_Include.empty();
        for (const std::string& pattern : m_Include)
            if (MatchPattern(pattern, name))
            {
                included = true;
                break;
            }
        if (!included)
            return false;
        for (const std::string& pattern : m_Exclude)
            if (MatchPattern(pattern, name))
                return false;
        return true;
    }

    std::string TestFilter::Describe() const
    {
        std::string out;
        for (const std::string& pattern : m_Include)
            out += (out.empty() ? "" : ", ") + pattern;
        for (const std::string& pattern : m_Exclude)
            out += (out.empty() ? "-" : ", -") + pattern;
        return out;
    }

    std::string Usage()
    {
        return "Foydalanish: ZaminTests [parametrlar] [namuna ...]\n"
               "\n"
               "  namuna, --filter <namuna>, -f   faqat nomi mos testlar. * va ? bilan - butun nomga glob (Prefab*),\n"
               "                                  ularsiz - nomida shu matn bor testlar; katta-kichik harf farqsiz.\n"
               "                                  Bir nechtasi ':' yoki ',' bilan; '-' bilan boshlangani chiqarib\n"
               "                                  tashlaydi (\"Scene*:-*Fuzz*\" yoki alohida: Scene* -*Fuzz*).\n"
               "                                  ZAMIN_TESTS muhit o'zgaruvchisi ham.\n"
               "  --exclude <namuna>              mos testlarni chiqarib tashlash\n"
               "  --list, -l                      testlar nomlari (har qatorda bittasi), hech narsa ishga tushmaydi\n"
               "  --timing, -t                    vaqt hisoboti: eng sekin testlar, jami, taqsimot\n"
               "  --slowest <N>                   hisobotda eng sekin N test (odatda 10; --timing ni yoqadi)\n"
               "  --fail-fast, -x                 birinchi xato bergan testdan keyin to'xtash\n"
               "  --report <fayl.json>            natijalar JSON faylga (har test: vaqt, holat, xatolar)\n"
               "  --junit <fayl.xml>              natijalar JUnit XML faylga (CI uchun)\n"
               "  --help, -h                      shu yordam\n"
               "\n"
               "Chiqish kodi: 0 - hammasi o'tdi, 1 - xato bor yoki filtrga mos test yo'q, 2 - noto'g'ri parametr.\n";
    }

    bool ParseOptions(int argc, const char* const* argv, RunnerOptions& out, std::string& error)
    {
        for (int i = 0; i < argc; i++)
        {
            const std::string_view arg = argv[i] ? argv[i] : "";
            // "--name value" or "--name=value".
            auto value = [&](std::string_view name, std::string& result) {
                if (arg == name)
                {
                    if (i + 1 >= argc || !argv[i + 1])
                    {
                        error = std::format("{} uchun qiymat berilmagan", name);
                        return true;
                    }
                    result = argv[++i];
                    return true;
                }
                if (arg.size() > name.size() + 1 && arg.starts_with(name) && arg[name.size()] == '=')
                {
                    result = std::string(arg.substr(name.size() + 1));
                    return true;
                }
                return false;
            };
            std::string text;
            if (arg == "--list" || arg == "-l")
                out.List = true;
            else if (arg == "--help" || arg == "-h")
                out.Help = true;
            else if (arg == "--timing" || arg == "-t")
                out.Timing = true;
            else if (arg == "--fail-fast" || arg == "-x")
                out.FailFast = true;
            else if (value("--filter", text) || value("-f", text))
            {
                if (!error.empty())
                    return false;
                out.Filter.Add(text);
            }
            else if (value("--exclude", text))
            {
                if (!error.empty())
                    return false;
                out.Filter.Exclude(text);
            }
            else if (value("--slowest", text))
            {
                if (!error.empty())
                    return false;
                if (!ParseCount(text, out.Slowest))
                {
                    error = std::format("--slowest: musbat butun son kerak, berilgan: '{}'", text);
                    return false;
                }
                out.Timing = true;
            }
            else if (value("--report", text))
            {
                if (!error.empty())
                    return false;
                out.JsonReport = text;
            }
            else if (value("--junit", text))
            {
                if (!error.empty())
                    return false;
                out.JUnitReport = text;
            }
            else if (!arg.empty() && arg.front() != '-')
                out.Filter.Add(arg); // a pattern
            else if (arg.size() > 2 && arg[1] != '-')
                out.Filter.Add(arg); // "-Physics*": a pattern that excludes (a short option is "-" and one letter)
            else
            {
                error = std::format("noma'lum parametr: {}", arg);
                return false;
            }
        }
        return true;
    }

    std::vector<size_t> SelectTests(const std::vector<TestCase>& tests, const TestFilter& filter)
    {
        std::vector<size_t> chosen;
        for (size_t i = 0; i < tests.size(); i++)
            if (filter.Matches(tests[i].Name))
                chosen.push_back(i);
        return chosen;
    }

    int RunSummary::CountFailures() const
    {
        int failures = 0;
        for (const TestResult& r : Results)
            failures += r.Failures;
        return failures;
    }

    size_t RunSummary::CountFailedTests() const
    {
        return (size_t)std::count_if(Results.begin(), Results.end(), [](const TestResult& r) { return !r.Passed(); });
    }

    std::string FormatDuration(double milliseconds)
    {
        if (milliseconds >= 1000.0)
            return std::format("{:.2f} s", milliseconds / 1000.0);
        if (milliseconds >= 100.0)
            return std::format("{:.0f} ms", milliseconds);
        if (milliseconds >= 10.0)
            return std::format("{:.1f} ms", milliseconds);
        return std::format("{:.2f} ms", milliseconds);
    }

    std::string FormatTimingReport(const RunSummary& summary, int slowest)
    {
        const std::vector<TestResult>& results = summary.Results;
        std::string out;
        double sum = 0.0;
        for (const TestResult& r : results)
            sum += r.Milliseconds;
        std::vector<const TestResult*> sorted;
        for (const TestResult& r : results)
            sorted.push_back(&r);
        // Slowest first; equal times keep the run's order.
        std::stable_sort(sorted.begin(), sorted.end(), [](const TestResult* a, const TestResult* b) { return a->Milliseconds > b->Milliseconds; });
        const double median = sorted.empty() ? 0.0 : sorted[sorted.size() / 2]->Milliseconds;
        out += std::format("Vaqt hisoboti: {} test, jami {} (testlarning o'zi {}; o'rtacha {}, mediana {})\n", results.size(),
                           FormatDuration(summary.TotalMilliseconds), FormatDuration(sum),
                           FormatDuration(results.empty() ? 0.0 : sum / (double)results.size()), FormatDuration(median));
        const size_t shown = std::min(sorted.size(), (size_t)std::max(0, slowest));
        if (shown > 0)
        {
            double top = 0.0;
            for (size_t i = 0; i < shown; i++)
                top += sorted[i]->Milliseconds;
            out += std::format("Eng sekin {} test (testlar vaqtining {:.1f} %):\n", shown, sum > 0.0 ? top * 100.0 / sum : 0.0);
            out += std::format("  {:>3}  {:>10}  {:>7}  {:>9}  {}\n", "#", "vaqt", "ulush", "yig'indi", "test");
            double running = 0.0;
            for (size_t i = 0; i < shown; i++)
            {
                const TestResult& r = *sorted[i];
                running += r.Milliseconds;
                out += std::format("  {:>3}  {:>10}  {:>6.1f}%  {:>8.1f}%  {}{}\n", i + 1, FormatDuration(r.Milliseconds),
                                   sum > 0.0 ? r.Milliseconds * 100.0 / sum : 0.0, sum > 0.0 ? running * 100.0 / sum : 0.0, r.Name,
                                   r.Passed() ? "" : "  (XATO)");
            }
        }
        // How the times spread: how many tests took how long, and how much of the time they make up.
        struct Bucket
        {
            const char* Label;
            double Below;
            size_t Count = 0;
            double Ms = 0.0;
        };
        Bucket buckets[] = { { "< 1 ms", 1.0 }, { "1-10 ms", 10.0 }, { "10-100 ms", 100.0 }, { "0.1-1 s", 1000.0 }, { ">= 1 s", 1e300 } };
        for (const TestResult& r : results)
            for (Bucket& b : buckets)
                if (r.Milliseconds < b.Below)
                {
                    b.Count++;
                    b.Ms += r.Milliseconds;
                    break;
                }
        out += "Taqsimot:";
        for (const Bucket& b : buckets)
            out += std::format("  {}: {} ({:.0f} %)", b.Label, b.Count, sum > 0.0 ? b.Ms * 100.0 / sum : 0.0);
        out += "\n";
        return out;
    }

    std::string FormatJsonReport(const RunSummary& summary)
    {
        std::string out = "{\n";
        out += std::format("  \"Timestamp\": \"{}\",\n", JsonEscape(summary.Timestamp));
        out += std::format("  \"Filter\": \"{}\",\n", JsonEscape(summary.Filter));
        out += std::format("  \"Registered\": {},\n  \"Ran\": {},\n  \"Skipped\": {},\n", summary.Registered, summary.Results.size(), summary.Skipped);
        out += std::format("  \"FailedTests\": {},\n  \"Failures\": {},\n", summary.CountFailedTests(), summary.CountFailures());
        out += std::format("  \"TotalMs\": {:.3f},\n  \"Tests\": [\n", summary.TotalMilliseconds);
        for (size_t i = 0; i < summary.Results.size(); i++)
        {
            const TestResult& r = summary.Results[i];
            out += std::format("    {{ \"Name\": \"{}\", \"Ms\": {:.3f}, \"Passed\": {}, \"Failures\": {}", JsonEscape(r.Name), r.Milliseconds,
                               r.Passed() ? "true" : "false", r.Failures);
            if (!r.Exception.empty())
                out += std::format(", \"Exception\": \"{}\"", JsonEscape(r.Exception));
            if (!r.Messages.empty())
            {
                out += ", \"Messages\": [";
                for (size_t m = 0; m < r.Messages.size(); m++)
                    out += std::format("{}\"{}\"", m ? ", " : "", JsonEscape(r.Messages[m]));
                out += "]";
            }
            out += std::format(" }}{}\n", i + 1 < summary.Results.size() ? "," : "");
        }
        out += "  ]\n}\n";
        return out;
    }

    std::string FormatJUnitReport(const RunSummary& summary, std::string_view suite)
    {
        size_t failed = 0, errors = 0;
        for (const TestResult& r : summary.Results)
        {
            if (!r.Exception.empty())
                errors++;
            else if (!r.Passed())
                failed++;
        }
        const std::string name = XmlEscape(suite);
        const double seconds = summary.TotalMilliseconds / 1000.0;
        std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out += std::format("<testsuites name=\"{}\" tests=\"{}\" failures=\"{}\" errors=\"{}\" time=\"{:.3f}\">\n", name, summary.Results.size(), failed,
                           errors, seconds);
        out += std::format("  <testsuite name=\"{}\" tests=\"{}\" failures=\"{}\" errors=\"{}\" skipped=\"{}\" time=\"{:.3f}\" timestamp=\"{}\">\n",
                           name, summary.Results.size(), failed, errors, summary.Skipped, seconds, XmlEscape(summary.Timestamp));
        for (const TestResult& r : summary.Results)
        {
            out += std::format("    <testcase name=\"{}\" classname=\"{}\" time=\"{:.3f}\"", XmlEscape(r.Name), name, r.Milliseconds / 1000.0);
            if (r.Passed())
            {
                out += "/>\n";
                continue;
            }
            out += ">\n";
            std::string details;
            for (const std::string& m : r.Messages)
                details += m + "\n";
            if (!r.Exception.empty())
                out += std::format("      <error message=\"istisno: {}\" type=\"exception\">{}</error>\n", XmlEscape(r.Exception), XmlEscape(details));
            else
                out += std::format("      <failure message=\"{}\" type=\"check\">{}</failure>\n",
                                   XmlEscape(std::format("{} ta tekshiruv o'tmadi", r.Failures)), XmlEscape(details));
            out += "    </testcase>\n";
        }
        out += "  </testsuite>\n</testsuites>\n";
        return out;
    }

    int RunMain(int argc, const char* const* argv)
    {
        RunnerOptions options;
        std::string error;
        if (!ParseOptions(argc > 1 ? argc - 1 : 0, argv + 1, options, error))
        {
            std::fprintf(stderr, "ZaminTests: %s\n\n%s", error.c_str(), Usage().c_str());
            return 2;
        }
        if (options.Help)
        {
            std::printf("%s", Usage().c_str());
            return 0;
        }
        // ZAMIN_TESTS=<part of a name> (while working on some tests) adds to the patterns.
        if (const char* env = std::getenv("ZAMIN_TESTS"); env && *env)
            options.Filter.Add(env);

        const std::vector<TestCase>& tests = Registry();
        const std::vector<size_t> chosen = SelectTests(tests, options.Filter);
        if (options.List)
        {
            for (size_t i : chosen)
                std::printf("%s\n", tests[i].Name);
            std::fprintf(stderr, "%zu test (jami %zu)\n", chosen.size(), tests.size());
            return 0;
        }
        if (chosen.empty())
        {
            std::printf("Filtrga mos test yo'q: %s (jami %zu test; nomlar: --list)\n", options.Filter.Describe().c_str(), tests.size());
            return 1;
        }

        RunSummary summary;
        summary.Registered = tests.size();
        summary.Filter = options.Filter.Describe();
        summary.Timestamp = NowIso8601();
        summary.Results.reserve(chosen.size());
        using Clock = std::chrono::steady_clock;
        const auto runStart = Clock::now();
        int index = 0;
        for (size_t i : chosen)
        {
            const TestCase& test = tests[i];
            TestResult& result = summary.Results.emplace_back();
            result.Name = test.Name;
            s_Current = &result;
            const int before = FailureCounter();
            const auto start = Clock::now();
            // An exception that leaves a test fails it; the next tests still run.
            try
            {
                test.Fn();
            }
            catch (const std::exception& e)
            {
                result.Exception = e.what();
            }
            catch (...)
            {
                result.Exception = "noma'lum istisno";
            }
            result.Milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            s_Current = nullptr;
            if (!result.Exception.empty())
            {
                std::printf("    FAIL istisno: %s\n", result.Exception.c_str());
                FailureCounter()++;
            }
            result.Failures = FailureCounter() - before;
            std::printf("[%2d] %-32s %-6s %10s\n", ++index, test.Name, result.Passed() ? "OK" : "FAILED", FormatDuration(result.Milliseconds).c_str());
            std::fflush(stdout); // a crash later keeps what ran
            if (options.FailFast && !result.Passed())
            {
                summary.Skipped = chosen.size() - summary.Results.size();
                break;
            }
        }
        summary.TotalMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - runStart).count();

        const TestResult* slowest = &summary.Results.front();
        for (const TestResult& r : summary.Results)
            if (r.Milliseconds > slowest->Milliseconds)
                slowest = &r;
        std::printf("\nVaqt: %s (eng sekin: %s, %s)\n", FormatDuration(summary.TotalMilliseconds).c_str(), slowest->Name.c_str(),
                    FormatDuration(slowest->Milliseconds).c_str());
        if (options.Timing)
            std::printf("%s", FormatTimingReport(summary, options.Slowest).c_str());
        if (summary.Skipped > 0)
            std::printf("--fail-fast: birinchi xatodan keyin to'xtatildi, %zu test ishga tushmadi\n", summary.Skipped);
        if (summary.Results.size() < tests.size())
            std::printf("Filtr: %s (%zu / %zu test)\n", summary.Filter.empty() ? "-" : summary.Filter.c_str(), chosen.size(), tests.size());
        bool written = true;
        if (!options.JsonReport.empty())
        {
            const bool ok = WriteFile(options.JsonReport, FormatJsonReport(summary));
            std::printf("%s: %s\n", ok ? "JSON hisobot" : "JSON hisobot YOZILMADI", options.JsonReport.c_str());
            written = written && ok;
        }
        if (!options.JUnitReport.empty())
        {
            const bool ok = WriteFile(options.JUnitReport, FormatJUnitReport(summary, "ZaminTests"));
            std::printf("%s: %s\n", ok ? "JUnit hisobot" : "JUnit hisobot YOZILMADI", options.JUnitReport.c_str());
            written = written && ok;
        }
        const int failures = summary.CountFailures();
        std::printf("\n%zu test, %d xato\n", summary.Results.size(), failures);
        return failures == 0 && written ? 0 : 1;
    }

}
