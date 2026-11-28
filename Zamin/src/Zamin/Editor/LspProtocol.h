#pragma once
// The Language Server Protocol's pieces the built-in code editor uses, independent of the GUI and of any process:
// JSON-RPC framing (Content-Length headers), positions in the negotiated encoding (UTF-8 or UTF-16 code units),
// the incremental change between two versions of a text, workspace edits (parsing and applying them), URIs and the
// results of completion, go-to-definition, hover and published diagnostics. LspClient runs a server over stdio.
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin::Lsp {
    using json = nlohmann::json;

    // One JSON-RPC message with its header: "Content-Length: N\r\n\r\n<body>".
    std::string Frame(const json& message);

    // Reads framed messages from a byte stream that arrives in pieces of any size. A broken header (no
    // Content-Length, a bad number, a message over MaxMessage) or a body that is not JSON is reported by Next (false,
    // Error() set) and skipped, so one bad message never stops the stream.
    class FrameReader
    {
    public:
        static constexpr size_t MaxMessage = 64ull * 1024 * 1024;
        static constexpr size_t MaxHeader = 8 * 1024;
        void Feed(std::string_view bytes) { m_Buffer.append(bytes); }
        // The next whole message; false when none is complete yet or the next one was broken (then Error()).
        bool Next(json& out);
        const std::string& Error() const { return m_Error; }
        size_t Buffered() const { return m_Buffer.size(); }

    private:
        std::string m_Buffer, m_Error;
    };

    enum class Encoding { UTF8, UTF16 };
    struct Position
    {
        int Line = 0, Character = 0;
        bool operator==(const Position&) const = default;
        auto operator<=>(const Position&) const = default;
    };
    struct TextRange
    {
        Position Start, End;
        bool operator==(const TextRange&) const = default;
    };
    json ToJson(const Position& position);
    json ToJson(const TextRange& range);
    Position PositionFromJson(const json& value);
    TextRange RangeFromJson(const json& value);

    // A byte offset of UTF-8 text <-> a position (line, character in the encoding's code units). Out-of-range lines
    // and characters are clamped to the text (the end of the line / of the text).
    size_t OffsetOf(std::string_view text, Position position, Encoding encoding);
    Position PositionOf(std::string_view text, size_t offset, Encoding encoding);

    // didChange: the one range that turns `before` into `after` (the common prefix and suffix are kept; whole UTF-8
    // characters only). Nothing when the texts are the same.
    struct ContentChange
    {
        TextRange Range;
        std::string Text;
    };
    std::optional<ContentChange> ComputeChange(std::string_view before, std::string_view after, Encoding encoding);
    json ToJson(const ContentChange& change);

    struct TextEdit
    {
        TextRange Range;
        std::string NewText;
    };
    // Applies edits whose ranges refer to the original text (as LSP sends them, in any order). False (and nothing
    // changed) when two ranges overlap or a range is out of the text.
    bool ApplyEdits(std::string& text, std::vector<TextEdit> edits, Encoding encoding, std::string* error = nullptr);

    // file:// URIs (percent-encoded) <-> absolute paths.
    std::string PathToUri(const std::filesystem::path& path);
    std::filesystem::path UriToPath(std::string_view uri);

    struct FileEdits
    {
        std::filesystem::path File;
        std::vector<TextEdit> Edits;
    };
    // A WorkspaceEdit's "changes" or "documentChanges" (text document edits; file create / rename / delete operations
    // are refused with an error), files in a stable order.
    std::vector<FileEdits> ParseWorkspaceEdit(const json& edit, std::string* error = nullptr);

    // One file of a multi-file change (a rename): its text before and after.
    struct FileChange
    {
        std::filesystem::path File;
        std::string Before, After;
        int Edits = 0;
    };
    // The changes a workspace edit makes, applied to the texts `read` gives (an open tab's text, else the file on disk;
    // nullopt: the file cannot be read). Fails as a whole (empty + error) when any file cannot be read or edited.
    template <class Read>
    std::vector<FileChange> PlanChanges(const std::vector<FileEdits>& edits, Encoding encoding, Read&& read, std::string* error = nullptr)
    {
        std::vector<FileChange> out;
        for (const auto& file : edits)
        {
            std::optional<std::string> text = read(file.File);
            if (!text)
            {
                if (error) *error = "Cannot read " + file.File.string();
                return {};
            }
            FileChange change{ file.File, *text, *text, int(file.Edits.size()) };
            std::string problem;
            if (!ApplyEdits(change.After, file.Edits, encoding, &problem))
            {
                if (error) *error = file.File.filename().string() + ": " + problem;
                return {};
            }
            if (change.After != change.Before) out.push_back(std::move(change));
        }
        return out;
    }

    // The 1-based line numbers and texts a change touches (the rename preview): before and after, line by line.
    struct PreviewLine
    {
        int Line = 0;
        std::string Before, After;
    };
    std::vector<PreviewLine> PreviewChange(const FileChange& change, size_t maxLines = 50);

    // textDocument/publishDiagnostics.
    struct Diagnostic
    {
        TextRange Range;
        int Severity = 1; // 1 error, 2 warning, 3 information, 4 hint
        std::string Message, Source, Code;
    };
    struct PublishedDiagnostics
    {
        std::filesystem::path File;
        std::optional<int> Version;
        std::vector<Diagnostic> Diagnostics;
    };
    PublishedDiagnostics ParseDiagnostics(const json& params);

    struct CompletionItem
    {
        std::string Label, Detail, InsertText, FilterText, SortText, Documentation;
        int Kind = 0;
        std::optional<TextEdit> Edit;
        bool Snippet = false; // insertTextFormat 2: the text has $1 / ${1:x} placeholders (Plain() removes them)
        // What to insert: the edit's text, else insertText, else the label, without snippet placeholders.
        std::string Text() const;
        // The kind's short name ("method", "field", "class"...).
        const char* KindName() const;
    };
    struct CompletionList
    {
        bool Incomplete = false;
        std::vector<CompletionItem> Items;
    };
    CompletionList ParseCompletion(const json& result);
    // "${1:name}" / "$1" / "$0" placeholders replaced by their text; "\$" kept as "$".
    std::string PlainSnippet(std::string_view snippet);

    struct Location
    {
        std::filesystem::path File;
        TextRange Range;
    };
    // A Location, Location[] or LocationLink[] (targetSelectionRange) result.
    std::vector<Location> ParseLocations(const json& result);
    // A Hover result's text (MarkupContent, MarkedString or an array of them), trimmed.
    std::string ParseHover(const json& result);

    // The identifier (letters, digits, '_') around a byte offset: [start, end). Empty when there is none.
    std::pair<size_t, size_t> IdentifierAt(std::string_view text, size_t offset);
}
