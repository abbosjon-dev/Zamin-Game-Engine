// The unit tests' small framework: the registry, the failed checks, choosing tests by name and the
// timing report. It does not depend on the engine (ZaminTests.cpp defines TEST / CHECK on top of it).
//
//   ZaminTests                         every test; the output ends with "N test, M xato"
//   ZaminTests --list [patterns]       the names only (one per line), nothing runs
//   ZaminTests Prefab* -Physics*       a pattern with * / ? is a glob over the whole name, one without them
//   ZaminTests --filter "Scene*:-*Fuzz" matches the names that contain it; '-' excludes (a positional "-X" of
//                                      more than one letter is a pattern, "-x" an option); case-insensitive
//   ZaminTests --timing --slowest 20   the time report (slowest tests, totals, a histogram)
//   ZaminTests --report r.json --junit r.xml   the results for CI (JSON, JUnit XML)
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ZaminTest {

    struct TestCase
    {
        const char* Name;
        std::function<void()> Fn;
    };

    // Every test in the order of their definitions (TEST adds them before main).
    std::vector<TestCase>& Registry();
    // Failed checks in the whole run (CHECK adds one).
    int& FailureCounter();
    // A failed check: printed at once ("    FAIL <text>"), counted and kept for the test's report.
    void Fail(std::string_view message);
    void CheckFailed(const char* file, int line, const char* expression);

    // `pattern` against a test name, ignoring case. With '*' (any run) or '?' (one character) it is a glob over
    // the whole name; without them it matches the names that contain it (as ZAMIN_TESTS always did).
    bool MatchPattern(std::string_view pattern, std::string_view name);

    // Patterns separated by ':' or ',' (several Add calls add up); one that begins with '-' excludes the names
    // it matches. No including pattern: every name that is not excluded.
    class TestFilter
    {
    public:
        void Add(std::string_view spec);
        void Exclude(std::string_view pattern);
        bool IsEmpty() const { return m_Include.empty() && m_Exclude.empty(); }
        bool Matches(std::string_view name) const;
        // "Prefab*, -*Fuzz" (for the messages).
        std::string Describe() const;
        const std::vector<std::string>& GetIncluded() const { return m_Include; }
        const std::vector<std::string>& GetExcluded() const { return m_Exclude; }

    private:
        std::vector<std::string> m_Include, m_Exclude;
    };

    struct RunnerOptions
    {
        TestFilter Filter;
        bool List = false;
        bool Help = false;
        bool Timing = false;   // the time report before the summary line
        int Slowest = 10;      // how many of the slowest tests the report lists
        bool FailFast = false; // stop after the first test that failed
        std::string JsonReport, JUnitReport;
    };

    // The arguments after the program's name. False (and `error`) on an unknown option or a missing value.
    bool ParseOptions(int argc, const char* const* argv, RunnerOptions& out, std::string& error);
    std::string Usage();

    // The indices of the tests the filter chooses, in their order.
    std::vector<size_t> SelectTests(const std::vector<TestCase>& tests, const TestFilter& filter);

    struct TestResult
    {
        std::string Name;
        double Milliseconds = 0.0;
        int Failures = 0;                  // failed checks (an exception counts as one)
        std::vector<std::string> Messages; // the failed checks' texts (the first 20)
        std::string Exception;             // what() of an exception that left the test

        bool Passed() const { return Failures == 0; }
    };

    struct RunSummary
    {
        std::vector<TestResult> Results;
        double TotalMilliseconds = 0.0; // the whole run (wall clock)
        size_t Registered = 0;          // tests in the program (the filter chose Results' tests from them)
        size_t Skipped = 0;             // left out after --fail-fast stopped the run
        std::string Filter;             // TestFilter::Describe, empty: none
        std::string Timestamp;          // ISO 8601, the start of the run

        int CountFailures() const;
        size_t CountFailedTests() const;
    };

    // "0.42 ms", "152.3 ms", "12.35 s".
    std::string FormatDuration(double milliseconds);
    // The time report: totals, the slowest `slowest` tests with their share of the time, a histogram.
    std::string FormatTimingReport(const RunSummary& summary, int slowest);
    std::string FormatJsonReport(const RunSummary& summary);
    std::string FormatJUnitReport(const RunSummary& summary, std::string_view suite);

    // Runs the chosen tests: the lines "[ i] Name  OK  12.3 ms", the report, the files and last the line
    // "N test, M xato". 0 when every test passed, 1 when one failed or none matched, 2 on bad arguments.
    int RunMain(int argc, const char* const* argv);

}
