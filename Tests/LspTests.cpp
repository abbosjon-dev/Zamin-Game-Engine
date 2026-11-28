// ---------- The code editor's clangd/LSP client (Zamin/Editor/LspProtocol.h, LspClient.h, CodeIntelligence.h)
#include "TestAssertions.h"
#include "Zamin/Core/UUID.h"
#include "Zamin/Editor/CodeIntelligence.h"
#include "Zamin/Utils/FileSystem.h"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <random>
#include <thread>
using namespace Zamin;
using namespace Zamin::Lsp;
namespace fs = std::filesystem;
namespace {
    struct TempDir
    {
        fs::path Root = fs::temp_directory_path() / ("zamin-lsp-" + std::to_string((uint64_t)UUID()));
        TempDir() { fs::create_directories(Root / "Source"); }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }
    };
    std::string Apply(std::string text, const ContentChange& change, Encoding encoding)
    {
        CHECK(ApplyEdits(text, { { change.Range, change.Text } }, encoding));
        return text;
    }
}

TEST(LspFramingRoundTripsSplitAndBrokenMessages)
{
    const json a = { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "initialize" }, { "params", { { "text", "Salom, dunyo! \xc3\xa7\xf0\x9f\x98\x80" } } } };
    const json b = { { "jsonrpc", "2.0" }, { "method", "exit" } };
    const std::string framed = Frame(a);
    CHECK(framed.starts_with("Content-Length: " + std::to_string(a.dump().size()) + "\r\n\r\n"));
    // Byte by byte: nothing until the last byte of the body.
    FrameReader reader;
    json out;
    for (size_t i = 0; i + 1 < framed.size(); ++i)
    {
        reader.Feed(std::string_view(&framed[i], 1));
        CHECK(!reader.Next(out) && reader.Error().empty());
    }
    reader.Feed(std::string_view(&framed.back(), 1));
    CHECK(reader.Next(out) && out == a && reader.Buffered() == 0);
    // Two messages in one piece, another header and other letter case.
    reader.Feed("content-length:  " + std::to_string(b.dump().size()) + "\r\nContent-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\n" + b.dump() + framed);
    CHECK(reader.Next(out) && out == b);
    CHECK(reader.Next(out) && out == a);
    CHECK(!reader.Next(out) && reader.Error().empty());
    // A header without a length, a bad length, a body that is not JSON: reported, skipped, the next one still read.
    reader.Feed("X-Header: 1\r\n\r\n");
    CHECK(!reader.Next(out) && !reader.Error().empty());
    reader.Feed("Content-Length: 12x\r\n\r\n");
    CHECK(!reader.Next(out) && !reader.Error().empty());
    reader.Feed("Content-Length: 5\r\n\r\n{nope" + framed);
    CHECK(!reader.Next(out) && !reader.Error().empty());
    CHECK(reader.Next(out) && out == a);
    reader.Feed("Content-Length: 99999999999\r\n\r\n");
    CHECK(!reader.Next(out) && !reader.Error().empty());
    reader.Feed(std::string(FrameReader::MaxHeader + 10, 'x'));
    CHECK(!reader.Next(out) && !reader.Error().empty() && reader.Buffered() == 0);
}

TEST(LspPositionsInUtf8AndUtf16CodeUnits)
{
    // "a😀b\nçx": the emoji is 4 UTF-8 bytes / 2 UTF-16 units, ç 2 bytes / 1 unit.
    const std::string text = "a\xf0\x9f\x98\x80" "b\n\xc3\xa7x";
    CHECK((PositionOf(text, 5, Encoding::UTF8) == Position{ 0, 5 }));
    CHECK((PositionOf(text, 5, Encoding::UTF16) == Position{ 0, 3 }));
    CHECK((PositionOf(text, 9, Encoding::UTF16) == Position{ 1, 1 }));
    CHECK((PositionOf(text, 9, Encoding::UTF8) == Position{ 1, 2 }));
    CHECK(OffsetOf(text, { 0, 3 }, Encoding::UTF16) == 5);
    CHECK(OffsetOf(text, { 1, 1 }, Encoding::UTF16) == 9);
    CHECK(OffsetOf(text, { 1, 2 }, Encoding::UTF8) == 9);
    // Clamped: past a line's end -> the end of that line; past the last line -> the end of the text.
    CHECK(OffsetOf(text, { 0, 99 }, Encoding::UTF16) == 6);
    CHECK(OffsetOf(text, { 7, 0 }, Encoding::UTF8) == text.size());
    for (size_t offset = 0; offset <= text.size(); ++offset)
        if (offset == text.size() || (static_cast<unsigned char>(text[offset]) & 0xC0) != 0x80)
            for (auto e : { Encoding::UTF8, Encoding::UTF16 }) CHECK(OffsetOf(text, PositionOf(text, offset, e), e) == offset);
}

TEST(LspIncrementalChangesTurnTheOldTextIntoTheNew)
{
    for (auto e : { Encoding::UTF8, Encoding::UTF16 })
    {
        CHECK(!ComputeChange("same", "same", e));
        auto insert = ComputeChange("int x;\n", "int xy;\n", e);
        CHECK(insert && insert->Text == "y" && (insert->Range.Start == Position{ 0, 5 }) && insert->Range.Start == insert->Range.End);
        auto erase = ComputeChange("a\nbc\nd", "a\nd", e);
        CHECK(erase && erase->Text.empty() && (erase->Range.Start == Position{ 1, 0 }) && (erase->Range.End == Position{ 2, 0 }));
        CHECK(Apply("a\nbc\nd", *erase, e) == "a\nd");
        // é and è share their first byte: the change must start at the character, never inside it.
        auto accent = ComputeChange("caf\xc3\xa9!", "caf\xc3\xa8!", e);
        CHECK(accent && accent->Text == "\xc3\xa8" && (accent->Range.Start == Position{ 0, 3 }) && (accent->Range.End == Position{ 0, e == Encoding::UTF8 ? 5 : 4 }));
        auto all = ComputeChange("", "new file\n", e);
        CHECK(all && all->Text == "new file\n" && Apply("", *all, e) == "new file\n");
    }
    // Random edits of a text with multi-byte characters, in both encodings: the change applied gives the new text.
    std::mt19937 random(7);
    const char* pieces[] = { "a", "b", "\n", " ", "\xc3\xa7", "\xf0\x9f\x98\x80", "\xd0\xb6", "x;" };
    for (int round = 0; round < 300; ++round)
    {
        std::string before, after;
        for (int i = 0; i < int(random() % 30); ++i) before += pieces[random() % std::size(pieces)];
        after = before;
        std::vector<size_t> starts;
        for (size_t i = 0; i <= after.size(); ++i)
            if (i == after.size() || (static_cast<unsigned char>(after[i]) & 0xC0) != 0x80) starts.push_back(i);
        size_t a = starts[random() % starts.size()], b = starts[random() % starts.size()];
        if (a > b) std::swap(a, b);
        std::string inserted;
        for (int i = 0; i < int(random() % 4); ++i) inserted += pieces[random() % std::size(pieces)];
        after.replace(a, b - a, inserted);
        for (auto e : { Encoding::UTF8, Encoding::UTF16 })
        {
            auto change = ComputeChange(before, after, e);
            CHECK(before == after ? !change : change && Apply(before, *change, e) == after);
        }
    }
}

TEST(LspWorkspaceEditsParseApplyAndPreview)
{
    // "changes" with a percent-encoded path, edits out of order; "documentChanges" for another file.
    const json changes = { { "changes",
                             { { "file:///tmp/My%20Game/Source/Player.h",
                                 { { { "range", { { "start", { { "line", 1 }, { "character", 8 } } }, { "end", { { "line", 1 }, { "character", 14 } } } } }, { "newText", "Vitality" } },
                                   { { "range", { { "start", { { "line", 0 }, { "character", 7 } } }, { "end", { { "line", 0 }, { "character", 13 } } } } }, { "newText", "Hero" } } } } } } };
    std::string error;
    auto files = ParseWorkspaceEdit(changes, &error);
    CHECK(error.empty() && files.size() == 1 && files[0].File == fs::path("/tmp/My Game/Source/Player.h") && files[0].Edits.size() == 2);
    std::string text = "struct Player {\n    int Health = 3;\n};\n";
    CHECK(ApplyEdits(text, files[0].Edits, Encoding::UTF8) && text == "struct Hero {\n    int Vitality = 3;\n};\n");
    const json documentChanges = { { "documentChanges",
                                     { { { "textDocument", { { "uri", "file:///a/B.cpp" }, { "version", 3 } } },
                                         { "edits", { { { "range", { { "start", { { "line", 0 }, { "character", 0 } } }, { "end", { { "line", 0 }, { "character", 1 } } } } }, { "newText", "Z" } } } } } } } };
    files = ParseWorkspaceEdit(documentChanges, &error);
    CHECK(files.size() == 1 && files[0].File == fs::path("/a/B.cpp"));
    CHECK(ParseWorkspaceEdit({ { "documentChanges", { { { "kind", "rename" }, { "oldUri", "file:///a" }, { "newUri", "file:///b" } } } } }, &error).empty() && !error.empty());
    // Overlapping edits: refused, the text unchanged.
    std::string same = "abcdef";
    CHECK(!ApplyEdits(same, { { { { 0, 1 }, { 0, 4 } }, "X" }, { { { 0, 3 }, { 0, 5 } }, "Y" } }, Encoding::UTF8, &error) && same == "abcdef");
    // Inserts at the same place keep their order.
    CHECK(ApplyEdits(same, { { { { 0, 2 }, { 0, 2 } }, "1" }, { { { 0, 2 }, { 0, 2 } }, "2" } }, Encoding::UTF8) && same == "ab12cdef");
    // A plan over two files: both texts change, or nothing when one cannot be read.
    std::vector<FileEdits> rename = { { "/p/A.cpp", { { { { 0, 4 }, { 0, 10 } }, "Vitality" } } }, { "/p/B.h", { { { { 1, 8 }, { 1, 14 } }, "Vitality" } } } };
    std::map<std::string, std::string> disk = { { "/p/A.cpp", "p.x=Health;\n" }, { "/p/B.h", "struct P {\n    int Health;\n};\n" } };
    auto read = [&](const fs::path& f) -> std::optional<std::string> {
        auto it = disk.find(f.string());
        return it == disk.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    auto plan = PlanChanges(rename, Encoding::UTF8, read, &error);
    CHECK(plan.size() == 2 && plan[0].After == "p.x=Vitality;\n" && plan[1].After == "struct P {\n    int Vitality;\n};\n");
    auto preview = PreviewChange(plan[1]);
    CHECK(preview.size() == 1 && preview[0].Line == 2 && preview[0].Before == "    int Health;" && preview[0].After == "    int Vitality;");
    disk.erase("/p/B.h");
    CHECK(PlanChanges(rename, Encoding::UTF8, read, &error).empty() && error.find("B.h") != std::string::npos);
    CHECK(UriToPath(PathToUri("/tmp/a b/ç#1.cpp")) == fs::path("/tmp/a b/ç#1.cpp"));
    CHECK(PathToUri("/tmp/a b") == "file:///tmp/a%20b");
}

TEST(LspParsesCompletionLocationsHoverAndDiagnostics)
{
    const json completion = { { "isIncomplete", true },
                              { "items",
                                { { { "label", " Heal(int amount)" }, { "kind", 2 }, { "sortText", "2" }, { "detail", "void" }, { "insertText", "Heal" } },
                                  { { "label", "Health" }, { "kind", 5 }, { "sortText", "1" }, { "detail", "int" },
                                    { "textEdit", { { "range", { { "start", { { "line", 3 }, { "character", 6 } } }, { "end", { { "line", 3 }, { "character", 8 } } } } }, { "newText", "Health" } } } },
                                  { { "label", "Make" }, { "insertTextFormat", 2 }, { "insertText", "Make(${1:int x}, $2)$0" }, { "sortText", "3" } } } } };
    auto list = ParseCompletion(completion);
    CHECK(list.Incomplete && list.Items.size() == 3);
    CHECK(list.Items[0].Label == "Health" && list.Items[0].Edit && list.Items[0].Text() == "Health" && std::string(list.Items[0].KindName()) == "field");
    CHECK(list.Items[1].Label == "Heal(int amount)" && list.Items[1].Text() == "Heal" && std::string(list.Items[1].KindName()) == "method");
    CHECK(list.Items[2].Text() == "Make(int x, )");
    CHECK(ParseCompletion(json::array({ { { "label", "x" } } })).Items.size() == 1 && ParseCompletion(nullptr).Items.empty());
    auto links = ParseLocations({ { { "targetUri", "file:///s/Player.cpp" }, { "targetRange", { { "start", { { "line", 9 }, { "character", 0 } } }, { "end", { { "line", 12 }, { "character", 1 } } } } },
                                    { "targetSelectionRange", { { "start", { { "line", 9 }, { "character", 13 } } }, { "end", { { "line", 9 }, { "character", 17 } } } } } } });
    CHECK(links.size() == 1 && links[0].File == fs::path("/s/Player.cpp") && (links[0].Range.Start == Position{ 9, 13 }));
    auto single = ParseLocations({ { "uri", "file:///s/Player.h" }, { "range", { { "start", { { "line", 2 }, { "character", 4 } } }, { "end", { { "line", 2 }, { "character", 8 } } } } } });
    CHECK(single.size() == 1 && single[0].Range.Start.Line == 2);
    CHECK(ParseLocations(nullptr).empty());
    CHECK(ParseHover({ { "contents", { { "kind", "plaintext" }, { "value", "  field Health\n\nType: int  " } } } }) == "field Health\n\nType: int");
    CHECK(ParseHover({ { "contents", json::array({ "a", { { "language", "cpp" }, { "value", "int x" } } }) } }) == "a\nint x");
    auto published = ParseDiagnostics({ { "uri", "file:///s/Game.cpp" }, { "version", 4 },
                                        { "diagnostics", { { { "range", { { "start", { { "line", 1 }, { "character", 8 } } }, { "end", { { "line", 1 }, { "character", 11 } } } } },
                                                             { "severity", 1 }, { "message", "Use of undeclared identifier 'foo'" }, { "source", "clang" }, { "code", "undeclared_var_use" } } } } });
    CHECK(published.File == fs::path("/s/Game.cpp") && published.Version == 4 && published.Diagnostics.size() == 1);
    CHECK(published.Diagnostics[0].Severity == 1 && published.Diagnostics[0].Code == "undeclared_var_use" && published.Diagnostics[0].Range.End.Character == 11);
    CHECK((IdentifierAt("p.Health = 3;", 4) == std::pair<size_t, size_t>{ 2, 8 }));
    CHECK((IdentifierAt("p.Health = 3;", 8) == std::pair<size_t, size_t>{ 2, 8 }));
    CHECK(IdentifierAt("a + 42", 5).first == IdentifierAt("a + 42", 5).second);
}

TEST(CodeIntelligenceCompileDatabaseKeepsFlagsAndAddsNewSources)
{
    TempDir t;
    const fs::path a = t.Root / "Source/A.cpp", b = t.Root / "Source/Sub/B.cpp";
    CHECK(FileSystem::WriteTextFile(a, "int a;\n"));
    fs::create_directories(t.Root / "Build");
    const json cmake = json::array({ { { "directory", (t.Root / "Build").string() },
                                       { "command", "/usr/bin/c++ -DGAME=1 -I\"" + (t.Root / "Source").string() + "\" -std=c++20 -fno-gnu-unique -o CMakeFiles/A.cpp.o -c " + a.string() },
                                       { "file", a.string() } } });
    CHECK(FileSystem::WriteTextFile(t.Root / "Build/compile_commands.json", cmake.dump()));
    CodeIntelligence::Settings settings;
    settings.ProjectRoot = t.Root;
    settings.SourceDirectory = t.Root / "Source";
    settings.CacheDirectory = t.Root / ".zamin";
    CHECK(CodeIntelligence::WriteCompilationDatabase(settings) == 1);
    CHECK(FileSystem::WriteTextFile(b, "int b;\n")); // a source added after the project was configured
    CHECK(CodeIntelligence::WriteCompilationDatabase(settings) == 2);
    json db = json::parse(FileSystem::ReadTextFile(t.Root / ".zamin/clangd/compile_commands.json"));
    CHECK(db.size() == 2);
    auto args = [&](size_t i) { return db[i]["arguments"].get<std::vector<std::string>>(); };
    auto has = [](const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); };
    CHECK(db[0]["file"] == a.string() && has(args(0), "-DGAME=1") && has(args(0), "-I" + (t.Root / "Source").string()));
    CHECK(!has(args(0), "-fno-gnu-unique") && has(args(0), "-Wno-unknown-warning-option")); // a GCC-only flag clangd rejects
    CHECK(db[1]["file"] == fs::weakly_canonical(b).string() && has(args(1), "-DGAME=1") && has(args(1), fs::weakly_canonical(b).string()) && !has(args(1), a.string()));
    // Without any compile database: the SDK's flags (the engine's include folder and definitions).
    fs::remove(t.Root / "Build/compile_commands.json");
    CHECK(CodeIntelligence::WriteCompilationDatabase(settings) == 2);
    db = json::parse(FileSystem::ReadTextFile(t.Root / ".zamin/clangd/compile_commands.json"));
    bool engineInclude = false;
    for (const auto& arg : args(0)) engineInclude |= arg.starts_with("-I") && fs::exists(fs::path(arg.substr(2)) / "Zamin/Zamin.h");
    CHECK(engineInclude && has(args(0), "-std=c++20") && has(args(0), "-DGLM_FORCE_RADIANS"));
}

TEST(CodeIntelligenceWithoutClangdKeepsTheCatalogue)
{
    TempDir t;
    CodeIntelligence ci;
    CodeIntelligence::Settings settings;
    settings.ClangdPath = (t.Root / "no-such-clangd").string();
    settings.ProjectRoot = t.Root;
    settings.CacheDirectory = t.Root / ".zamin";
    ci.Configure(settings);
    CHECK(ci.GetStatus() == CodeIntelligence::Status::NotFound && !ci.IsReady());
    CHECK(ci.GetStatusText().find("catalogue completion") != std::string::npos);
    bool answered = false;
    ci.Open(t.Root / "Source/A.cpp", "int a;");
    ci.Complete(t.Root / "Source/A.cpp", 3, [&](CompletionList list, const std::string& error) { answered = list.Items.empty() && !error.empty(); });
    CHECK(answered);
    CHECK(CodeIntelligence::FindClangd("/definitely/missing/clangd").empty());
}

// The real clangd (skipped when it is not installed): diagnostics while typing, completion of a project class member,
// go-to-definition into another file, a rename across two files, stale responses dropped, a crash and the restart.
SLOW_TEST(CodeIntelligenceRealClangdCompletesNavigatesRenamesAndRecovers)
{
    if (CodeIntelligence::FindClangd().empty())
    {
        std::printf("    SKIP: clangd is not installed (apt-get install clangd)\n");
        return;
    }
    TempDir t;
    const fs::path header = t.Root / "Source/Player.h", player = t.Root / "Source/Player.cpp", game = t.Root / "Source/Game.cpp";
    CHECK(FileSystem::WriteTextFile(header, "#pragma once\nstruct Player\n{\n    int Health = 3;\n    void Heal(int amount);\n};\n"));
    CHECK(FileSystem::WriteTextFile(player, "#include \"Player.h\"\nvoid Player::Heal(int amount) { Health += amount; }\n"));
    const std::string gameText = "#include \"Player.h\"\nint Play()\n{\n    Player p;\n    p.Heal(1);\n    return p.Health;\n}\n";
    CHECK(FileSystem::WriteTextFile(game, gameText));
    json db = json::array();
    for (const auto& f : { player, game })
        db.push_back({ { "directory", t.Root.string() }, { "file", f.string() }, { "arguments", { "/usr/bin/c++", "-std=c++20", "-I" + (t.Root / "Source").string(), "-c", f.string() } } });
    fs::create_directories(t.Root / "Build");
    CHECK(FileSystem::WriteTextFile(t.Root / "Build/compile_commands.json", db.dump()));
    CodeIntelligence ci;
    CodeIntelligence::Settings settings;
    settings.ProjectRoot = t.Root;
    settings.SourceDirectory = t.Root / "Source";
    settings.CacheDirectory = t.Root / ".zamin";
    ci.Configure(settings);
    auto pump = [&](auto&& done, double seconds) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < end)
        {
            ci.Poll();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };
    CHECK(pump([&] { return ci.IsReady(); }, 30));
    ci.Open(game, gameText);
    auto errors = [&](const fs::path& f) {
        int n = 0;
        if (auto* d = ci.GetDiagnostics(f))
            for (const auto& x : *d) n += x.Severity == 1;
        return n;
    };
    CHECK(pump([&] { return ci.GetDiagnostics(game) != nullptr; }, 30) && errors(game) == 0);
    // A type error typed in: it appears; fixed: it goes away.
    std::string broken = gameText;
    broken.replace(broken.find("return p.Health;"), 16, "return p.Health + \"x\";");
    ci.Change(game, broken);
    CHECK(pump([&] { return errors(game) == 1; }, 20));
    ci.Change(game, gameText);
    CHECK(pump([&] { return ci.GetDiagnostics(game) && errors(game) == 0; }, 20));
    std::printf("    diagnostics after an edit: %.0f ms\n", ci.GetLastDiagnosticsMs());
    // Completion of a project class member: "p.He|".
    std::string typing = gameText;
    const size_t at = typing.find("    return") ;
    typing.insert(at, "    p.He\n");
    ci.Change(game, typing);
    std::vector<std::string> labels;
    bool completed = false;
    ci.Complete(game, at + 8, [&](CompletionList list, const std::string& error) {
        CHECK(error.empty());
        for (auto& item : list.Items) labels.push_back(item.Text());
        completed = true;
    });
    CHECK(pump([&] { return completed; }, 15));
    CHECK(std::find(labels.begin(), labels.end(), "Health") != labels.end() && std::find(labels.begin(), labels.end(), "Heal") != labels.end());
    std::printf("    completion: %.0f ms, %zu items\n", ci.GetLastCompletionMs(), labels.size());
    // A stale response: the text changes before the answer is delivered - the callback never runs.
    bool staleCalled = false;
    const size_t droppedBefore = ci.GetStaleDropped();
    ci.Complete(game, at + 8, [&](CompletionList, const std::string&) { staleCalled = true; });
    ci.Change(game, gameText);
    CHECK(pump([&] { return ci.GetStaleDropped() > droppedBefore; }, 15) && !staleCalled);
    // Go-to-definition of Heal (Game.cpp) -> Player.cpp.
    std::vector<Location> found;
    bool located = false;
    ci.Definition(game, gameText.find("Heal(1)") + 1, [&](std::vector<Location> l, const std::string&) {
        found = std::move(l);
        located = true;
    });
    CHECK(pump([&] { return located; }, 15));
    CHECK(found.size() == 1 && fs::weakly_canonical(found[0].File) == fs::weakly_canonical(player) && found[0].Range.Start.Line == 1);
    // Rename Health (declared in Player.h, used in Player.cpp and Game.cpp): the background index finds every file.
    std::vector<FileEdits> edits;
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        bool renamed = false;
        ci.Rename(game, gameText.find("Health") + 2, "Vitality", [&](std::vector<FileEdits> e, const std::string&) {
            edits = std::move(e);
            renamed = true;
        });
        CHECK(pump([&] { return renamed; }, 30));
        if (edits.size() >= 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    CHECK(edits.size() == 3);
    auto plan = PlanChanges(edits, ci.GetEncoding(), [&](const fs::path& f) -> std::optional<std::string> { return FileSystem::ReadTextFile(f); });
    CHECK(plan.size() == 3);
    for (const auto& change : plan) CHECK(change.After.find("Vitality") != std::string::npos && change.After.find("Health") == std::string::npos);
    // A crash: restarted, the document opened again, diagnostics published again.
    const int pid = ci.GetServerPid();
    CHECK(pid > 0);
    ::kill(pid, SIGKILL);
    CHECK(pump([&] { return ci.GetStatus() == CodeIntelligence::Status::Restarting; }, 10));
    CHECK(pump([&] { return ci.IsReady() && ci.GetServerPid() != pid; }, 30) && ci.GetRestartCount() == 1);
    ci.Change(game, broken);
    CHECK(pump([&] { return errors(game) == 1; }, 30));
    ci.Shutdown();
    CHECK(ci.GetStatus() == CodeIntelligence::Status::Off && ci.GetServerPid() < 0);
}

// LspClient against small stand-in servers (no clangd): a server that never answers initialize, one that exits at once,
// and one that answers initialize but never a request (the request times out; a late answer is dropped).
TEST(LspClientTimesOutAndReportsServersThatStop)
{
    auto pump = [](LspClient& client, auto&& done, double seconds) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < end)
        {
            client.Poll();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };
    {
        LspClient client;
        std::string reason;
        client.OnExit = [&](const std::string& r) { reason = r; };
        LspClient::Options options;
        options.Command = { "sleep", "20" };
        options.InitializeTimeout = 0.3;
        CHECK(client.Start(options));
        bool failed = false;
        client.Request("textDocument/hover", json::object(), [&](const json&, const std::string& error) { failed = error == "server stopped"; });
        CHECK(pump(client, [&] { return !reason.empty(); }, 5));
        CHECK(reason.find("initialize") != std::string::npos && failed && client.GetState() == LspClient::State::Exited);
        client.Stop(0.1);
        CHECK(client.GetPid() < 0);
    }
    {
        LspClient client;
        std::string reason;
        client.OnExit = [&](const std::string& r) { reason = r; };
        LspClient::Options options;
        options.Command = { "false" };
        CHECK(client.Start(options));
        CHECK(pump(client, [&] { return !reason.empty(); }, 5) && reason.find("exited") != std::string::npos);
    }
    {
        LspClient client;
        LspClient::Options options;
        CHECK(!client.Start(options) && !client.Error().empty());
        options.Command = { "/no/such/language-server" };
        CHECK(!client.Start(options) || pump(client, [&] { return client.GetState() == LspClient::State::Exited; }, 5));
    }
    // Answers initialize (UTF-8 positions), then only reads: a request times out and is cancelled.
    const char* server = R"(import sys, json
def read():
    length = 0
    while True:
        line = sys.stdin.buffer.readline()
        if not line: sys.exit(0)
        if line in (b'\r\n', b'\n'): break
        if line.lower().startswith(b'content-length:'): length = int(line.split(b':')[1])
    return json.loads(sys.stdin.buffer.read(length))
def send(m):
    body = json.dumps(m).encode()
    sys.stdout.buffer.write(b'Content-Length: %d\r\n\r\n' % len(body) + body); sys.stdout.buffer.flush()
while True:
    m = read()
    if m.get('method') == 'initialize': send({'jsonrpc': '2.0', 'id': m['id'], 'result': {'capabilities': {'positionEncoding': 'utf-8'}, 'serverInfo': {'version': 'stand-in 1.0'}}})
    if m.get('method') == 'exit': sys.exit(0)
)";
    LspClient client;
    bool ready = false;
    client.OnReady = [&] { ready = true; };
    LspClient::Options options;
    options.Command = { "python3", "-c", server };
    CHECK(client.Start(options));
    std::string error;
    client.Request("textDocument/completion", json::object(), [&](const json&, const std::string& e) { error = e; }, 0.4);
    CHECK(pump(client, [&] { return ready; }, 10) && client.GetEncoding() == Encoding::UTF8);
    CHECK(pump(client, [&] { return !error.empty(); }, 5) && error == "timeout" && client.GetPendingCount() == 0);
    client.Stop(2.0);
    CHECK(client.GetState() == LspClient::State::Stopped && client.GetPid() < 0);
}
