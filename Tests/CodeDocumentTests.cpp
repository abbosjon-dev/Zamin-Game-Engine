#include "TestAssertions.h"
#include "Zamin/Editor/CodeDocument.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Core/UUID.h"
#include <filesystem>
using namespace Zamin;
namespace {
    struct Fixture
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / ("zamin-code-" + std::to_string((uint64_t)UUID()));
        Fixture() { std::filesystem::create_directories(Root / "Source"); }
        ~Fixture()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }
        auto File() const { return Root / "Source/Player.cpp"; }
        auto Recovery() const { return Root / ".zamin/recovery"; }
        void Write(const std::string& text) { CHECK(FileSystem::WriteTextFile(File(), text)); }
    };
}
TEST(CodeDocumentPreservesUnicodeBomCrlfAndFinalNewline)
{
    Fixture f;
    f.Write("\xef\xbb\xbf// Salom, O'zbekiston!\r\nint x = 1;");
    CodeDocument d;
    CHECK(d.Open(f.File(), f.Recovery(), f.Root));
    CHECK(d.BOM() && d.CRLF());
    CHECK(!d.Dirty());
    CHECK(d.Text() == "// Salom, O'zbekiston!\nint x = 1;");
    CHECK(d.SetText("// Salom, O'zbekiston!\nint x = 2;"));
    CHECK(d.Save());
    CHECK(FileSystem::ReadTextFile(f.File()) == "\xef\xbb\xbf// Salom, O'zbekiston!\r\nint x = 2;");
    CHECK(!d.Dirty());
}
TEST(CodeDocumentConflictsNeverSilentlyOverwrite)
{
    Fixture f;
    f.Write("original\n");
    CodeDocument d;
    CHECK(d.Open(f.File(), f.Recovery(), f.Root));
    CHECK(d.SetText("edited\n"));
    f.Write("external\n");
    CHECK(d.HasExternalChange());
    CHECK(!d.Save());
    CHECK(d.Dirty());
    CHECK(d.Text() == "edited\n");
    CHECK(FileSystem::ReadTextFile(f.File()) == "external\n");
    CHECK(d.Save(true));
    CHECK(FileSystem::ReadTextFile(f.File()) == "edited\n");
}
TEST(CodeDocumentRecoveryKeepsOriginalConflictBaseline)
{
    Fixture f;
    f.Write("before\n");
    CodeDocument d;
    CHECK(d.Open(f.File(), f.Recovery(), f.Root));
    CHECK(d.SetText("unsaved\n"));
    CHECK(d.WriteRecovery());
    f.Write("external after crash\n");
    CodeDocument reopened;
    CHECK(reopened.Open(f.File(), f.Recovery(), f.Root));
    CHECK(reopened.HasRecovery());
    CHECK(reopened.Text() == "external after crash\n");
    CHECK(reopened.Recover());
    CHECK(reopened.Text() == "unsaved\n");
    CHECK(reopened.HasExternalChange());
    CHECK(!reopened.Save());
    CHECK(reopened.Save(true));
    CHECK(!std::filesystem::exists(reopened.RecoveryPath()));
}
TEST(CodeDocumentUndoToSavedRemovesStaleRecovery)
{
    Fixture f;
    f.Write("base");
    CodeDocument d;
    CHECK(d.Open(f.File(), f.Recovery(), f.Root));
    CHECK(d.SetText("draft"));
    CHECK(d.WriteRecovery());
    CHECK(std::filesystem::exists(d.RecoveryPath()));
    CHECK(d.SetText("base"));
    CHECK(!d.Dirty());
    CHECK(d.WriteRecovery());
    CHECK(!std::filesystem::exists(d.RecoveryPath()));
}
TEST(CodeDocumentRejectsBinaryInvalidUtf8AndOversizedEdits)
{
    Fixture f;
    f.Write("safe");
    CodeDocument d;
    CHECK(d.Open(f.File(), f.Recovery(), f.Root));
    CHECK(!d.SetText(std::string("a\0b", 3)));
    CHECK(!d.SetText(std::string(1, (char)0xff)));
    CHECK(!d.SetText(std::string(CodeDocument::MaxBytes + 1, 'x')));
    CHECK(d.Text() == "safe");
    CHECK(d.SetText("// o‘zbekcha\n"));
    CHECK(d.Save());
    f.Write(std::string("bin\0ary", 7));
    CodeDocument binary;
    CHECK(!binary.Open(f.File()));
}
TEST(CodeDocumentSearchReplaceIsNonOverlappingAndBounded)
{
    CHECK(CodeDocument::Find("Foo foo FOO", "foo", false) == std::vector<size_t>({ 0, 4, 8 }));
    CHECK(CodeDocument::Find("aaaa", "aa") == std::vector<size_t>({ 0, 2 }));
    CHECK(CodeDocument::Find("abc", "").empty());
    CHECK(CodeDocument::ReplaceAll("Foo foo", "foo", "bar", false) == "bar bar");
    CHECK(CodeDocument::ReplaceAll("hello", "hello", "").empty());
    bool threw = false;
    try
    {
        CodeDocument::ReplaceAll("aa", "a", std::string(CodeDocument::MaxBytes, 'x'));
    }
    catch (...)
    {
        threw = true;
    }
    CHECK(threw);
}
TEST(CodeDocumentProjectBoundaryAndSymlinkEscape)
{
    Fixture f;
    f.Write("inside");
    auto outside = f.Root / "Outside.cpp";
    CHECK(FileSystem::WriteTextFile(outside, "outside"));
    CodeDocument d;
    CHECK(d.Open(f.File(), f.Recovery(), f.Root / "Source"));
    CHECK(d.SetText("draft"));
    std::filesystem::remove(f.File());
    std::filesystem::create_symlink(outside, f.File());
    CHECK(!d.Save(true));
    CHECK(FileSystem::ReadTextFile(outside) == "outside");
    CHECK(d.Dirty());
}
TEST(CodeDocumentFailedAtomicSaveKeepsDraft)
{
    Fixture f;
    f.Write("inside");
    CodeDocument d;
    CHECK(d.Open(f.File(), f.Recovery(), f.Root));
    CHECK(d.SetText("draft"));
    std::filesystem::remove(f.File());
    std::filesystem::create_directory(f.File());
    CHECK(!d.Save(true));
    CHECK(d.Dirty());
    CHECK(d.Text() == "draft");
}
TEST(CodeDocumentDamagedRecoveryDoesNotBlockSource)
{
    Fixture f;
    f.Write("source");
    CodeDocument d;
    CHECK(d.Open(f.File(), f.Recovery(), f.Root));
    CHECK(FileSystem::WriteTextFile(d.RecoveryPath(), "invalid-json"));
    CodeDocument other;
    CHECK(other.Open(f.File(), f.Recovery(), f.Root));
    CHECK(!other.HasRecovery());
    CHECK(other.Text() == "source");
}
