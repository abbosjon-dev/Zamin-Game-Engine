#include "Zamin/Editor/LspProtocol.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>

namespace Zamin::Lsp {
    namespace {
        bool Continuation(unsigned char c) { return (c & 0xC0) == 0x80; }
        // The length of the UTF-8 sequence that starts with `lead` (a broken byte counts as one).
        size_t SequenceLength(unsigned char lead)
        {
            if (lead < 0x80) return 1;
            if ((lead & 0xE0) == 0xC0) return 2;
            if ((lead & 0xF0) == 0xE0) return 3;
            if ((lead & 0xF8) == 0xF0) return 4;
            return 1;
        }
        int Units(unsigned char lead, Encoding encoding)
        {
            if (encoding == Encoding::UTF8) return int(SequenceLength(lead));
            return (lead & 0xF8) == 0xF0 ? 2 : 1; // a character outside the BMP is a surrogate pair
        }
        size_t LineStart(std::string_view text, int line)
        {
            size_t offset = 0;
            for (int i = 0; i < line; ++i)
            {
                const size_t next = text.find('\n', offset);
                if (next == std::string_view::npos) return text.size();
                offset = next + 1;
            }
            return offset;
        }
        std::string Trim(std::string text)
        {
            const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!text.empty() && space((unsigned char)text.back())) text.pop_back();
            size_t start = 0;
            while (start < text.size() && space((unsigned char)text[start])) ++start;
            return text.substr(start);
        }
        std::string MarkupText(const json& value)
        {
            if (value.is_string()) return value.get<std::string>();
            if (value.is_object())
            {
                if (auto v = value.find("value"); v != value.end() && v->is_string()) return v->get<std::string>();
                return {};
            }
            if (value.is_array())
            {
                std::string out;
                for (const auto& part : value)
                {
                    std::string text = MarkupText(part);
                    if (text.empty()) continue;
                    if (!out.empty()) out += "\n";
                    out += text;
                }
                return out;
            }
            return {};
        }
        std::string String(const json& object, const char* key)
        {
            if (!object.is_object()) return {};
            auto it = object.find(key);
            return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
        }
        bool ParseTextEdit(const json& value, TextEdit& out)
        {
            if (!value.is_object() || !value.contains("newText") || !value["newText"].is_string()) return false;
            out.NewText = value["newText"].get<std::string>();
            if (value.contains("range")) out.Range = RangeFromJson(value["range"]);
            else if (value.contains("replace")) out.Range = RangeFromJson(value["replace"]); // InsertReplaceEdit
            else return false;
            return true;
        }
    }

    std::string Frame(const json& message)
    {
        const std::string body = message.dump(-1, ' ', false, json::error_handler_t::replace);
        return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    }

    bool FrameReader::Next(json& out)
    {
        m_Error.clear();
        const size_t end = m_Buffer.find("\r\n\r\n");
        if (end == std::string::npos)
        {
            if (m_Buffer.size() > MaxHeader)
            {
                m_Error = "The message header is too long";
                m_Buffer.clear();
            }
            return false;
        }
        // Header lines: "Name: value"; Content-Length is required, Content-Type is ignored.
        std::optional<size_t> length;
        std::string_view header(m_Buffer.data(), end);
        while (!header.empty())
        {
            const size_t eol = header.find("\r\n");
            std::string_view line = header.substr(0, eol);
            header = eol == std::string_view::npos ? std::string_view() : header.substr(eol + 2);
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos) continue;
            std::string name(line.substr(0, colon));
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            std::string_view value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
            while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
            if (name == "content-length")
            {
                size_t n = 0;
                auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
                if (ec != std::errc() || ptr != value.data() + value.size()) break;
                length = n;
            }
        }
        if (!length || *length > MaxMessage)
        {
            m_Error = !length ? "A message without a valid Content-Length" : "A message over the size limit";
            m_Buffer.erase(0, end + 4);
            return false;
        }
        if (m_Buffer.size() < end + 4 + *length) return false;
        json parsed = json::parse(m_Buffer.begin() + std::ptrdiff_t(end + 4), m_Buffer.begin() + std::ptrdiff_t(end + 4 + *length), nullptr, false);
        m_Buffer.erase(0, end + 4 + *length);
        if (parsed.is_discarded() || !parsed.is_object())
        {
            m_Error = "A message body that is not a JSON object";
            return false;
        }
        out = std::move(parsed);
        return true;
    }

    json ToJson(const Position& position) { return { { "line", position.Line }, { "character", position.Character } }; }
    json ToJson(const TextRange& range) { return { { "start", ToJson(range.Start) }, { "end", ToJson(range.End) } }; }
    Position PositionFromJson(const json& value)
    {
        Position p;
        if (!value.is_object()) return p;
        if (auto it = value.find("line"); it != value.end() && it->is_number_integer()) p.Line = std::max(0, it->get<int>());
        if (auto it = value.find("character"); it != value.end() && it->is_number_integer()) p.Character = std::max(0, it->get<int>());
        return p;
    }
    TextRange RangeFromJson(const json& value)
    {
        TextRange r;
        if (!value.is_object()) return r;
        if (value.contains("start")) r.Start = PositionFromJson(value["start"]);
        if (value.contains("end")) r.End = PositionFromJson(value["end"]);
        return r;
    }

    size_t OffsetOf(std::string_view text, Position position, Encoding encoding)
    {
        size_t offset = LineStart(text, std::max(0, position.Line));
        int units = 0;
        while (offset < text.size() && text[offset] != '\n' && units < position.Character)
        {
            const size_t length = std::min(SequenceLength((unsigned char)text[offset]), text.size() - offset);
            units += Units((unsigned char)text[offset], encoding);
            offset += length;
        }
        return offset;
    }

    Position PositionOf(std::string_view text, size_t offset, Encoding encoding)
    {
        offset = std::min(offset, text.size());
        Position p;
        size_t start = 0;
        for (size_t i = 0; i < offset; ++i)
            if (text[i] == '\n')
            {
                ++p.Line;
                start = i + 1;
            }
        for (size_t i = start; i < offset;)
        {
            p.Character += Units((unsigned char)text[i], encoding);
            i += std::max<size_t>(1, SequenceLength((unsigned char)text[i]));
        }
        return p;
    }

    std::optional<ContentChange> ComputeChange(std::string_view before, std::string_view after, Encoding encoding)
    {
        if (before == after) return std::nullopt;
        size_t prefix = 0;
        const size_t limit = std::min(before.size(), after.size());
        while (prefix < limit && before[prefix] == after[prefix]) ++prefix;
        // Never split a UTF-8 character: back to its first byte.
        while (prefix > 0 && ((prefix < before.size() && Continuation((unsigned char)before[prefix])) ||
                              (prefix < after.size() && Continuation((unsigned char)after[prefix]))))
            --prefix;
        size_t suffix = 0;
        while (suffix < limit - prefix && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) ++suffix;
        while (suffix > 0 && ((before.size() - suffix < before.size() && Continuation((unsigned char)before[before.size() - suffix])) ||
                              (after.size() - suffix < after.size() && Continuation((unsigned char)after[after.size() - suffix]))))
            --suffix;
        ContentChange change;
        change.Range.Start = PositionOf(before, prefix, encoding);
        change.Range.End = PositionOf(before, before.size() - suffix, encoding);
        change.Text = std::string(after.substr(prefix, after.size() - suffix - prefix));
        return change;
    }

    json ToJson(const ContentChange& change) { return { { "range", ToJson(change.Range) }, { "text", change.Text } }; }

    bool ApplyEdits(std::string& text, std::vector<TextEdit> edits, Encoding encoding, std::string* error)
    {
        struct Span
        {
            size_t Start, End;
            const std::string* Text;
            size_t Order;
        };
        std::vector<Span> spans;
        spans.reserve(edits.size());
        const auto lines = size_t(std::count(text.begin(), text.end(), '\n')) + 1;
        for (size_t i = 0; i < edits.size(); ++i)
        {
            const auto& e = edits[i];
            if (e.Range.End < e.Range.Start || size_t(e.Range.Start.Line) >= lines + 1 || size_t(e.Range.End.Line) >= lines + 1)
            {
                if (error) *error = "An edit range is outside the text";
                return false;
            }
            spans.push_back({ OffsetOf(text, e.Range.Start, encoding), OffsetOf(text, e.Range.End, encoding), &e.NewText, i });
        }
        std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.Start < b.Start; });
        for (size_t i = 1; i < spans.size(); ++i)
            if (spans[i].Start < spans[i - 1].End)
            {
                if (error) *error = "Two edits overlap";
                return false;
            }
        std::string out;
        out.reserve(text.size());
        size_t at = 0;
        for (const auto& s : spans)
        {
            out.append(text, at, s.Start - at);
            out += *s.Text;
            at = s.End;
        }
        out.append(text, at, std::string::npos);
        text = std::move(out);
        return true;
    }

    std::string PathToUri(const std::filesystem::path& path)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out = "file://";
        for (unsigned char c : path.generic_string())
        {
            if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
            else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }

    std::filesystem::path UriToPath(std::string_view uri)
    {
        if (!uri.starts_with("file://")) return {};
        uri.remove_prefix(7);
        if (!uri.empty() && uri.front() != '/') // file://host/path: the host is dropped
            uri.remove_prefix(std::min(uri.size(), uri.find('/')));
        std::string out;
        for (size_t i = 0; i < uri.size(); ++i)
        {
            if (uri[i] == '%' && i + 2 < uri.size())
            {
                int value = 0;
                auto [ptr, ec] = std::from_chars(uri.data() + i + 1, uri.data() + i + 3, value, 16);
                if (ec == std::errc() && ptr == uri.data() + i + 3)
                {
                    out += char(value);
                    i += 2;
                    continue;
                }
            }
            out += uri[i];
        }
        return std::filesystem::path(out);
    }

    std::vector<FileEdits> ParseWorkspaceEdit(const json& edit, std::string* error)
    {
        std::map<std::string, FileEdits> files;
        auto add = [&](const std::string& uri, const json& list) {
            auto path = UriToPath(uri);
            if (path.empty() || !list.is_array()) return false;
            auto& file = files[path.string()];
            file.File = path;
            for (const auto& item : list)
            {
                TextEdit e;
                if (!ParseTextEdit(item, e)) return false;
                file.Edits.push_back(std::move(e));
            }
            return true;
        };
        if (!edit.is_object())
        {
            if (error) *error = "The rename result is empty";
            return {};
        }
        if (auto changes = edit.find("documentChanges"); changes != edit.end() && changes->is_array())
        {
            for (const auto& change : *changes)
            {
                if (change.contains("kind"))
                {
                    if (error) *error = "The edit creates, renames or deletes files (not supported)";
                    return {};
                }
                if (!change.contains("textDocument") || !add(String(change["textDocument"], "uri"), change.value("edits", json::array())))
                {
                    if (error) *error = "A malformed text document edit";
                    return {};
                }
            }
        }
        else if (auto map = edit.find("changes"); map != edit.end() && map->is_object())
        {
            for (const auto& [uri, list] : map->items())
                if (!add(uri, list))
                {
                    if (error) *error = "A malformed text edit";
                    return {};
                }
        }
        std::vector<FileEdits> out;
        for (auto& [_, file] : files) out.push_back(std::move(file));
        return out;
    }

    std::vector<PreviewLine> PreviewChange(const FileChange& change, size_t maxLines)
    {
        auto split = [](const std::string& text) {
            std::vector<std::string_view> lines;
            size_t start = 0;
            for (size_t i = 0; i <= text.size(); ++i)
                if (i == text.size() || text[i] == '\n')
                {
                    lines.emplace_back(text.data() + start, i - start);
                    start = i + 1;
                }
            return lines;
        };
        const auto a = split(change.Before), b = split(change.After);
        std::vector<PreviewLine> out;
        if (a.size() == b.size())
        {
            for (size_t i = 0; i < a.size() && out.size() < maxLines; ++i)
                if (a[i] != b[i]) out.push_back({ int(i + 1), std::string(a[i]), std::string(b[i]) });
            return out;
        }
        size_t prefix = 0, suffix = 0;
        while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
        while (suffix < a.size() - prefix && suffix < b.size() - prefix && a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix]) ++suffix;
        for (size_t i = prefix; i < std::max(a.size(), b.size()) - suffix && out.size() < maxLines; ++i)
            out.push_back({ int(i + 1), i < a.size() - suffix ? std::string(a[i]) : std::string(), i < b.size() - suffix ? std::string(b[i]) : std::string() });
        return out;
    }

    PublishedDiagnostics ParseDiagnostics(const json& params)
    {
        PublishedDiagnostics out;
        if (!params.is_object()) return out;
        out.File = UriToPath(String(params, "uri"));
        if (auto v = params.find("version"); v != params.end() && v->is_number_integer()) out.Version = v->get<int>();
        if (auto list = params.find("diagnostics"); list != params.end() && list->is_array())
            for (const auto& item : *list)
            {
                if (!item.is_object()) continue;
                Diagnostic d;
                d.Range = RangeFromJson(item.value("range", json::object()));
                if (auto s = item.find("severity"); s != item.end() && s->is_number_integer()) d.Severity = std::clamp(s->get<int>(), 1, 4);
                d.Message = String(item, "message");
                d.Source = String(item, "source");
                if (auto c = item.find("code"); c != item.end()) d.Code = c->is_string() ? c->get<std::string>() : c->is_number() ? c->dump() : std::string();
                out.Diagnostics.push_back(std::move(d));
            }
        return out;
    }

    std::string PlainSnippet(std::string_view snippet)
    {
        std::string out;
        for (size_t i = 0; i < snippet.size(); ++i)
        {
            const char c = snippet[i];
            if (c == '\\' && i + 1 < snippet.size() && (snippet[i + 1] == '$' || snippet[i + 1] == '}' || snippet[i + 1] == '\\'))
            {
                out += snippet[++i];
                continue;
            }
            if (c == '$' && i + 1 < snippet.size())
            {
                if (std::isdigit((unsigned char)snippet[i + 1]))
                {
                    while (i + 1 < snippet.size() && std::isdigit((unsigned char)snippet[i + 1])) ++i;
                    continue;
                }
                if (snippet[i + 1] == '{')
                {
                    size_t j = i + 2;
                    while (j < snippet.size() && std::isdigit((unsigned char)snippet[j])) ++j;
                    if (j < snippet.size() && snippet[j] == ':')
                    {
                        // ${1:text}: the text (nested placeholders flattened); ${1|a,b|}: not produced without snippetSupport
                        int depth = 1;
                        size_t k = j + 1;
                        std::string inner;
                        for (; k < snippet.size() && depth > 0; ++k)
                        {
                            if (snippet[k] == '{') ++depth;
                            else if (snippet[k] == '}' && --depth == 0) break;
                            inner += snippet[k];
                        }
                        out += PlainSnippet(inner);
                        i = k;
                        continue;
                    }
                    if (j < snippet.size() && snippet[j] == '}')
                    {
                        i = j;
                        continue;
                    }
                }
            }
            out += c;
        }
        return out;
    }

    std::string CompletionItem::Text() const
    {
        std::string text = Edit ? Edit->NewText : !InsertText.empty() ? InsertText : Label;
        return Snippet ? PlainSnippet(text) : text;
    }

    const char* CompletionItem::KindName() const
    {
        static const char* names[] = { "", "text", "method", "function", "constructor", "field", "variable", "class", "interface", "module",
                                       "property", "unit", "value", "enum", "keyword", "snippet", "color", "file", "reference", "folder",
                                       "enum member", "constant", "struct", "event", "operator", "type parameter" };
        return Kind >= 0 && Kind < int(std::size(names)) ? names[Kind] : "";
    }

    CompletionList ParseCompletion(const json& result)
    {
        CompletionList out;
        const json* items = nullptr;
        if (result.is_array()) items = &result;
        else if (result.is_object())
        {
            out.Incomplete = result.value("isIncomplete", false);
            if (auto it = result.find("items"); it != result.end() && it->is_array()) items = &*it;
        }
        if (!items) return out;
        for (const auto& item : *items)
        {
            if (!item.is_object()) continue;
            CompletionItem c;
            c.Label = Trim(String(item, "label"));
            if (c.Label.empty()) continue;
            c.Detail = String(item, "detail");
            c.InsertText = String(item, "insertText");
            c.FilterText = String(item, "filterText");
            c.SortText = String(item, "sortText");
            if (auto d = item.find("documentation"); d != item.end()) c.Documentation = Trim(MarkupText(*d));
            if (auto k = item.find("kind"); k != item.end() && k->is_number_integer()) c.Kind = k->get<int>();
            c.Snippet = item.value("insertTextFormat", 1) == 2;
            if (auto e = item.find("textEdit"); e != item.end())
            {
                TextEdit edit;
                if (ParseTextEdit(*e, edit)) c.Edit = std::move(edit);
            }
            out.Items.push_back(std::move(c));
        }
        std::stable_sort(out.Items.begin(), out.Items.end(), [](const CompletionItem& a, const CompletionItem& b) {
            return (a.SortText.empty() ? a.Label : a.SortText) < (b.SortText.empty() ? b.Label : b.SortText);
        });
        return out;
    }

    std::vector<Location> ParseLocations(const json& result)
    {
        std::vector<Location> out;
        auto one = [&](const json& item) {
            if (!item.is_object()) return;
            if (item.contains("targetUri"))
                out.push_back({ UriToPath(String(item, "targetUri")),
                                RangeFromJson(item.contains("targetSelectionRange") ? item["targetSelectionRange"] : item.value("targetRange", json::object())) });
            else if (item.contains("uri"))
                out.push_back({ UriToPath(String(item, "uri")), RangeFromJson(item.value("range", json::object())) });
        };
        if (result.is_array())
            for (const auto& item : result) one(item);
        else one(result);
        std::erase_if(out, [](const Location& l) { return l.File.empty(); });
        return out;
    }

    std::string ParseHover(const json& result)
    {
        if (!result.is_object() || !result.contains("contents")) return {};
        return Trim(MarkupText(result["contents"]));
    }

    std::pair<size_t, size_t> IdentifierAt(std::string_view text, size_t offset)
    {
        auto word = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
        offset = std::min(offset, text.size());
        if ((offset == text.size() || !word(text[offset])) && offset > 0 && word(text[offset - 1])) --offset;
        if (offset >= text.size() || !word(text[offset])) return { offset, offset };
        size_t start = offset, end = offset;
        while (start > 0 && word(text[start - 1])) --start;
        while (end < text.size() && word(text[end])) ++end;
        if (std::isdigit((unsigned char)text[start])) return { offset, offset };
        return { start, end };
    }
}
