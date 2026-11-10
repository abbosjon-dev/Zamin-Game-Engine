#include "TestAssertions.h"
#include <Zamin/Core/Process.h>
#include <Zamin/Core/UUID.h>
#include <Zamin/Editor/UndoHistory.h>
#include <Zamin/Renderer/Shader.h>
#include <Zamin/Utils/FileSystem.h>
#include <filesystem>
#include <stdexcept>

using namespace Zamin;
namespace fs = std::filesystem;

// ------------------------------------------------------------------ audit regression tests
TEST(ProcessLiteralArgumentsAndOutput)
{
    const auto marker = fs::temp_directory_path() / ("zamin-shell-marker-" + std::to_string(uint64_t(UUID())));
    const std::string literal = "spaces ' double\" ; $(touch " + marker.string() + ") `touch " + marker.string() + "` $HOME \\";
    std::vector<std::string> lines;
    auto result = RunProcess({ "printf", "%s\\n", literal, "", "last" }, [&](const std::string& line) { lines.push_back(line); });
    CHECK(result.Succeeded());
    CHECK(lines == std::vector<std::string>({ literal, "", "last" }));
    CHECK(!fs::exists(marker));
    CHECK(!RunProcess({}).Succeeded());
    CHECK(!RunProcess({ "zamin-missing-program-974234" }).Succeeded());
    CHECK(!RunProcess({ "printf", std::string("a\0b", 3) }).Succeeded());
    CHECK(RunProcess({ "false" }).ExitCode == 1);
    lines.clear();
    // An explicit shell here is a fixed test fixture, never supplied by the process API.
    CHECK(RunProcess({ "sh", "-c", "printf 'first\\r\\n'; printf 'second' >&2; exit 7" },
                     [&](const std::string& line) { lines.push_back(line); })
              .ExitCode == 7);
    CHECK(lines == std::vector<std::string>({ "first", "second" }));
    CHECK(RunProcess({ "sh", "-c", "kill -TERM $$" }).ExitCode == 143);
}

TEST(ProcessDrainsOutputAndReapsOnCallbackFailure)
{
    bool threw = false;
    try
    {
        RunProcess({ "sh", "-c", "head -c 262144 /dev/zero; printf '\\nfinished\\n'" }, [](const std::string&) {
            throw std::runtime_error("callback failure");
        });
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
    CHECK(RunProcess({ "printf", "still works" }).Succeeded());
}

TEST(IncompleteShaderFilesDoNotCrash)
{
    auto path = fs::temp_directory_path() / ("zamin-bad-shader-" + std::to_string(uint64_t(UUID())) + ".glsl");
    for (const std::string source : { "", "#type", "#type vertex", "#type vertex\n", "#type vertex\nvoid main(){}",
                                      "#type fragment\nvoid main(){}", "#type geometry\nvoid main(){}", "#type vertex\n#type fragment\n",
                                      "#type vertex\nvoid main(){}\n#type vertex\nvoid main(){}\n#type pixel\nvoid main(){}" })
    {
        CHECK(FileSystem::WriteTextFile(path, source));
        CHECK(!Shader::Create(path.string()));
    }
    fs::remove(path);
    CHECK(!Shader::Create(path.string()));
}

TEST(UndoMemoryBudgetAndSavedState)
{
    UndoHistory history(100, 100, 1000);
    history.Reset(std::string(300, 'a'), {}, "initial");
    std::weak_ptr<const std::string> oldSaved = history.GetCurrent().Scene;
    for (int i = 0; i < 12; ++i)
    {
        history.CommitEdit(std::string(300, char('b' + i)), {}, "edit");
        CHECK(history.GetRetainedBytes() <= history.GetMemoryBudget());
    }
    CHECK(oldSaved.expired());
    CHECK(!history.IsAtSavedState());
    CHECK(history.CanUndo());
    history.MarkSaved();
    history.Undo();
    CHECK(!history.IsAtSavedState());
    history.Redo();
    CHECK(history.IsAtSavedState());
    history.SetMemoryBudget(0);
    CHECK(history.GetEntries().size() == 1 && !history.CanUndo());
    CHECK(history.IsAtSavedState());
    history.CommitEdit(std::string(2000, 'z'), {}, "oversized current state");
    CHECK(history.GetEntries().size() == 1 && history.GetCurrent().Scene->size() == 2000);
}

TEST(UndoBudgetIncludesFileEditsAndRewrite)
{
    UndoHistory history(100, 100, 1200);
    history.Reset(std::string(200, 'a'), {}, "initial");
    history.CommitEdit(std::string(200, 'b'), {}, "prefab apply", 0, { { "Prefab.zprefab", std::string(400, 'x'), std::string(400, 'y') } });
    CHECK(history.GetRetainedBytes() <= history.GetMemoryBudget());
    history.CommitEdit(std::string(200, 'c'), {}, "edit");
    history.CommitEdit(std::string(200, 'd'), {}, "edit");
    history.Undo();
    const auto current = *history.GetCurrent().Scene;
    history.RewriteScenes([](std::string& text) { text += std::string(1000, 'r'); });
    CHECK(*history.GetCurrent().Scene == current + std::string(1000, 'r'));
    CHECK(history.GetEntries().size() == 1);
    CHECK(!history.CanUndo() && !history.CanRedo());
}

TEST(AtomicSavePermissionFailureKeepsOriginal)
{
    auto directory = fs::temp_directory_path() / ("zamin-permission-" + std::to_string(uint64_t(UUID())));
    fs::create_directories(directory);
    auto file = directory / "Map.zscene";
    CHECK(FileSystem::WriteTextFileAtomic(file, "original scene"));
    fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);
    CHECK(RunProcess({ ZAMIN_SAVE_PERMISSION_PROBE, file.string() }).Succeeded());
    CHECK(FileSystem::ReadTextFile(file) == "original scene");
    size_t files = 0;
    for (const auto& entry : fs::directory_iterator(directory))
    {
        (void)entry;
        ++files;
    }
    CHECK(files == 1);
    fs::permissions(directory, fs::perms::owner_all);
    fs::remove_all(directory);
}
