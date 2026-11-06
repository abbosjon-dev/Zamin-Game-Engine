#include "Zamin/Project/AssetFiles.h"
#include "Zamin/Asset/AssetMetadata.h"
#include "Zamin/Utils/FileSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <map>
#include <set>

namespace Zamin {

    namespace fs = std::filesystem;

    static std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    // ------------------------------------------------------------------ type filter
    bool AssetTypeFilter::Has(AssetType type) const
    {
        return (Mask >> (uint32_t)type) & 1u;
    }

    void AssetTypeFilter::Set(AssetType type, bool enabled)
    {
        if (type == AssetType::Folder || type >= AssetType::Count)
            return;
        uint32_t bit = 1u << (uint32_t)type;
        Mask = enabled ? (Mask | bit) : (Mask & ~bit);
    }

    int AssetTypeFilter::Count() const
    {
        int count = 0;
        for (uint32_t m = Mask; m; m &= m - 1)
            count++;
        return count;
    }

    bool AssetTypeFilter::Matches(AssetType type) const
    {
        if (!IsActive())
            return true;
        if (type == AssetType::Folder)
            return false;
        if (type == AssetType::Project)
            type = AssetType::Other;
        return Has(type);
    }

    const std::vector<AssetType>& AssetTypeFilter::GetFilterableTypes()
    {
        static const std::vector<AssetType> s_Types = {
            AssetType::Scene, AssetType::Prefab, AssetType::Texture, AssetType::Model, AssetType::Audio,
            AssetType::Font, AssetType::Environment, AssetType::Material, AssetType::PhysicsMaterial, AssetType::Script, AssetType::Other,
        };
        return s_Types;
    }

    // ------------------------------------------------------------------ types
    AssetType AssetFiles::GetType(const fs::path& path, bool isDirectory) { return AssetTypes::FromPath(path, isDirectory); }
    const char* AssetFiles::GetTypeName(AssetType type) { return AssetTypes::GetName(type); }
    const char* AssetFiles::GetTypeKey(AssetType type) { return AssetTypes::GetKey(type); }
    std::optional<AssetType> AssetFiles::FindTypeByKey(std::string_view key) { return AssetTypes::FromKey(key); }
    const char* AssetFiles::GetImportFolder(AssetType type) { return AssetTypes::GetImportFolder(type); }

    // ------------------------------------------------------------------ listing
    int AssetFiles::CompareNames(std::string_view a, std::string_view b)
    {
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size())
        {
            unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[j];
            if (std::isdigit(ca) && std::isdigit(cb))
            {
                // Compare whole numbers: skip leading zeros, then the longer number is larger.
                size_t ia = i, jb = j;
                while (ia < a.size() && a[ia] == '0') ia++;
                while (jb < b.size() && b[jb] == '0') jb++;
                size_t ea = ia, eb = jb;
                while (ea < a.size() && std::isdigit((unsigned char)a[ea])) ea++;
                while (eb < b.size() && std::isdigit((unsigned char)b[eb])) eb++;
                if (ea - ia != eb - jb)
                    return ea - ia < eb - jb ? -1 : 1;
                if (int c = a.substr(ia, ea - ia).compare(b.substr(jb, eb - jb)); c != 0)
                    return c < 0 ? -1 : 1;
                i = ea;
                j = eb;
                continue;
            }
            int la = std::tolower(ca), lb = std::tolower(cb);
            if (la != lb)
                return la < lb ? -1 : 1;
            i++;
            j++;
        }
        if (i < a.size() || j < b.size())
            return i < a.size() ? 1 : -1;
        int raw = a.compare(b); // equal ignoring case and zeros: still a stable order
        return raw < 0 ? -1 : (raw > 0 ? 1 : 0);
    }

    void AssetFiles::Sort(std::vector<AssetEntry>& entries, AssetSortColumn column, bool ascending)
    {
        std::stable_sort(entries.begin(), entries.end(), [&](const AssetEntry& a, const AssetEntry& b) {
            if (a.IsDirectory != b.IsDirectory)
                return a.IsDirectory; // folders first, in either direction
            int c = 0;
            switch (column)
            {
                case AssetSortColumn::Type:
                    c = a.Type == b.Type ? 0 : CompareNames(GetTypeName(a.Type), GetTypeName(b.Type));
                    break;
                case AssetSortColumn::Size:
                    c = a.Size == b.Size ? 0 : (a.Size < b.Size ? -1 : 1);
                    break;
                case AssetSortColumn::Modified:
                    c = a.Modified == b.Modified ? 0 : (a.Modified < b.Modified ? -1 : 1);
                    break;
                default:
                    break;
            }
            if (c == 0)
                c = CompareNames(a.Name, b.Name);
            if (c == 0)
                c = CompareNames(a.Path.generic_string(), b.Path.generic_string()); // same name in other folders
            return ascending ? c < 0 : c > 0;
        });
    }

    // ------------------------------------------------------------------ search
    AssetSearch::AssetSearch(std::string_view text)
    {
        size_t i = 0;
        while (i < text.size())
        {
            while (i < text.size() && std::isspace((unsigned char)text[i]))
                i++;
            size_t start = i;
            while (i < text.size() && !std::isspace((unsigned char)text[i]))
                i++;
            if (start == i)
                break;
            std::string word = Lower(std::string(text.substr(start, i - start)));
            std::replace(word.begin(), word.end(), '\\', '/');

            // t:<type> / tur:<type>
            std::string_view typeName;
            if (word.starts_with("t:"))
                typeName = std::string_view(word).substr(2);
            else if (word.starts_with("tur:"))
                typeName = std::string_view(word).substr(4);
            if (word.starts_with("t:") || word.starts_with("tur:"))
            {
                if (typeName.empty())
                    continue; // still being typed
                m_HasTypes = true;
                for (size_t t = 0; t < (size_t)AssetType::Count; t++)
                {
                    AssetType type = (AssetType)t;
                    // The key or a word of the name ("t:skript" finds "C++ skript", "t:hdr" "HDRI muhit").
                    bool matches = Lower(AssetTypes::GetKey(type)).starts_with(typeName);
                    std::string name = Lower(AssetTypes::GetName(type));
                    for (size_t w = 0; !matches && w < name.size(); w++)
                        if (w == 0 || name[w - 1] == ' ')
                            matches = std::string_view(name).substr(w).starts_with(typeName);
                    if (matches && std::find(m_Types.begin(), m_Types.end(), type) == m_Types.end())
                        m_Types.push_back(type);
                }
                continue;
            }

            Term term;
            term.Text = word;
            term.Inside = word.size() > 1 && word.back() == '/';
            size_t p = 0;
            while (p <= word.size())
            {
                size_t slash = word.find('/', p);
                if (slash == std::string::npos)
                    slash = word.size();
                if (slash > p)
                    term.Parts.push_back(word.substr(p, slash - p));
                p = slash + 1;
            }
            if (!term.Parts.empty())
                m_Terms.push_back(std::move(term));
        }
    }

    bool AssetSearch::MatchPattern(std::string_view text, std::string_view pattern)
    {
        size_t at = 0, p = 0;
        while (p <= pattern.size())
        {
            size_t star = pattern.find('*', p);
            if (star == std::string_view::npos)
                star = pattern.size();
            std::string_view piece = pattern.substr(p, star - p);
            if (!piece.empty())
            {
                size_t found = text.find(piece, at);
                if (found == std::string_view::npos)
                    return false;
                at = found + piece.size();
            }
            p = star + 1;
        }
        return true;
    }

    bool AssetSearch::Matches(std::string_view relativePath, AssetType type, bool skipTopFolder) const
    {
        if (m_HasTypes && std::find(m_Types.begin(), m_Types.end(), type) == m_Types.end())
            return false;
        if (m_Terms.empty())
            return true;
        std::string path = Lower(std::string(relativePath));
        std::replace(path.begin(), path.end(), '\\', '/');
        std::vector<std::string_view> segments;
        for (size_t p = 0; p <= path.size();)
        {
            size_t slash = path.find('/', p);
            if (slash == std::string::npos)
                slash = path.size();
            if (slash > p)
                segments.push_back(std::string_view(path).substr(p, slash - p));
            p = slash + 1;
        }
        // What plain words see: the path below its first folder (a top folder alone is a name).
        std::string_view plain = path;
        if (size_t slash = path.find('/'); skipTopFolder && slash != std::string::npos && slash + 1 < path.size())
            plain = std::string_view(path).substr(slash + 1);
        for (const Term& term : m_Terms)
        {
            bool hasSlash = term.Text.find('/') != std::string::npos;
            if (MatchPattern(hasSlash ? std::string_view(path) : plain, term.Text))
                continue; // a part of the whole path ("textures/co", "*.png")
            if (term.Parts.size() < 2)
                return false;
            // Parts in order over the folders and the name: "tex/coin" ~ "assets/textures/items/coin.png".
            size_t next = 0;
            bool all = true;
            for (const std::string& part : term.Parts)
            {
                while (next < segments.size() && !MatchPattern(segments[next], part))
                    next++;
                if (next == segments.size())
                {
                    all = false;
                    break;
                }
                next++;
            }
            if (!all || (term.Inside && next >= segments.size()))
                return false; // "Textures/": something inside the folder, not the folder itself
        }
        return true;
    }

    std::vector<AssetEntry> AssetFiles::List(const fs::path& directory, const AssetListOptions& options)
    {
        std::vector<AssetEntry> entries;
        std::error_code ec;
        if (!fs::is_directory(directory, ec))
            return entries;

        std::vector<fs::path> excluded;
        for (const auto& e : options.Excluded)
            excluded.push_back(e.lexically_normal());
        auto isExcluded = [&](const fs::path& p) {
            fs::path n = p.lexically_normal();
            return std::find(excluded.begin(), excluded.end(), n) != excluded.end();
        };

        AssetSearch search(options.Search);
        bool recursive = !search.IsEmpty() || options.Filter.IsActive();
        fs::path searchRoot = (options.SearchRoot.empty() ? directory : options.SearchRoot).lexically_normal();
        fs::path listed = directory.lexically_normal();
        // Only for paths that do start at the root (not the listed folder's own fallback below).
        fs::path listedFromRoot = listed.lexically_relative(searchRoot);
        bool skipTopFolder = options.SkipTopFolder && !listedFromRoot.empty() && *listedFromRoot.begin() != "..";
        auto searchPath = [&](const fs::path& p) {
            fs::path n = p.lexically_normal();
            fs::path rel = n.lexically_relative(searchRoot);
            if (rel.empty() || *rel.begin() == "..")
                rel = n.lexically_relative(listed); // the listed folder is not inside the root
            return rel.generic_string();
        };
        // Returns false for a folder that must not be entered.
        auto consider = [&](const fs::directory_entry& e) {
            std::error_code fec;
            std::string name = e.path().filename().string();
            bool isDir = e.is_directory(fec);
            if (name.empty() || name.starts_with(".") || (isDir && isExcluded(e.path())))
                return false;
            if (!isDir && AssetMetaFile::IsMetaFile(e.path()))
                return true; // .zmeta files go with their asset, they are not shown on their own
            AssetType type = GetType(e.path(), isDir);
            if (!search.IsEmpty() && !search.Matches(searchPath(e.path()), type, skipTopFolder))
                return true;
            if (!options.Filter.Matches(type))
                return true;
            AssetEntry entry;
            entry.Path = e.path();
            entry.Name = name;
            entry.Type = type;
            entry.IsDirectory = isDir;
            if (!isDir)
            {
                uintmax_t size = e.file_size(fec);
                entry.Size = fec ? 0 : size;
            }
            auto time = e.last_write_time(fec);
            if (!fec)
                entry.Modified = time;
            entries.push_back(std::move(entry));
            return true;
        };

        if (recursive)
        {
            fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end;
            for (; !ec && it != end; it.increment(ec))
                if (!consider(*it))
                    it.disable_recursion_pending();
        }
        else
        {
            for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
                consider(*it);
        }
        Sort(entries, options.SortBy, options.Ascending);
        return entries;
    }

    std::string AssetFiles::FormatSize(uintmax_t bytes)
    {
        if (bytes < 1024)
            return std::format("{} B", bytes);
        const char* units[] = { "KB", "MB", "GB", "TB" };
        double value = (double)bytes / 1024.0;
        int unit = 0;
        while (value >= 1024.0 && unit < 3)
        {
            value /= 1024.0;
            unit++;
        }
        return value < 10.0 ? std::format("{:.1f} {}", value, units[unit]) : std::format("{:.0f} {}", value, units[unit]);
    }

    // ------------------------------------------------------------------ paths
    static fs::path Normalize(const fs::path& p)
    {
        std::error_code ec;
        fs::path abs = fs::absolute(p, ec);
        if (ec) abs = p;
        fs::path result = fs::weakly_canonical(abs, ec);
        if (ec) result = abs.lexically_normal();
        if (!result.has_filename() && result.has_relative_path())
            result = result.parent_path(); // "dir/" -> "dir"
        return result;
    }

    // Component-wise prefix test of already normalized paths.
    static bool IsUnder(const fs::path& path, const fs::path& directory)
    {
        auto [d, p] = std::mismatch(directory.begin(), directory.end(), path.begin(), path.end());
        return d == directory.end();
    }

    bool AssetFiles::IsInside(const fs::path& path, const fs::path& directory)
    {
        if (path.empty() || directory.empty())
            return false;
        return IsUnder(Normalize(path), Normalize(directory));
    }

    fs::path AssetFiles::MakeUniquePath(const fs::path& directory, const fs::path& fileName)
    {
        std::error_code ec;
        fs::path candidate = directory / fileName;
        if (!fs::exists(candidate, ec))
            return candidate;
        std::string stem = fileName.stem().string(), ext = fileName.extension().string();
        if (stem.empty()) { stem = fileName.string(); ext.clear(); } // ".hidden"
        for (int i = 1; i < 100000; i++)
        {
            candidate = directory / (stem + "_" + std::to_string(i) + ext);
            if (!fs::exists(candidate, ec))
                return candidate;
        }
        return directory / fileName;
    }

    std::optional<std::string> AssetFiles::RemapPath(std::string_view value, std::string_view from, std::string_view to)
    {
        if (from.empty() || value.size() < from.size() || value.substr(0, from.size()) != from)
            return std::nullopt;
        if (value.size() == from.size())
            return std::string(to);
        if (value[from.size()] != '/')
            return std::nullopt; // "Textures2/..." is not inside "Textures"
        return std::string(to) + std::string(value.substr(from.size()));
    }

    bool AssetFiles::IsAssetPathValue(std::string_view key, std::string_view value)
    {
        // Keys of scene / prefab files whose strings are names or text, never asset paths: an
        // entity called "Player" must stay "Player" when the folder Assets/Player is renamed.
        static constexpr std::string_view NameKeys[] = {
            "Tag", "Tags", "Scene", "Name", "Class", "ComponentOrder", "ZaminComponent", "Text", "Type",
            "Mesh", "BodyType", "Clip", "DefaultState", "SpeedParameter", "Parameter", "From", "To", "ClickMessage",
        };
        if (value.empty() || std::find(std::begin(NameKeys), std::end(NameKeys), key) != std::end(NameKeys))
            return false;
        // Shaped like a file: a folder separator or an extension ("Textures/a.png", "hero.png").
        // A bare word ("Player", script string fields too) is a name, not a top-level folder.
        return value.find('/') != std::string_view::npos || fs::path(value).has_extension();
    }

    std::optional<std::string> AssetFiles::RemapAssetReference(std::string_view key, std::string_view value, const PathMoves& moves)
    {
        if (!IsAssetPathValue(key, value))
            return std::nullopt;
        for (const auto& [from, to] : moves)
            if (auto mapped = RemapPath(value, from, to))
                return mapped;
        return std::nullopt;
    }

    // ------------------------------------------------------------------ JSON strings
    static void AppendUtf8(std::string& out, uint32_t cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else
        {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    static bool ReadHex4(const std::string& text, size_t at, uint32_t& out)
    {
        if (at + 4 > text.size()) return false;
        out = 0;
        for (size_t k = 0; k < 4; k++)
        {
            char c = text[at + k];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    // `i` is at the opening quote; on success it is just past the closing one.
    static bool ParseJsonString(const std::string& text, size_t& i, std::string& out)
    {
        out.clear();
        i++;
        while (i < text.size())
        {
            char c = text[i];
            if (c == '"') { i++; return true; }
            if (c != '\\') { out += c; i++; continue; }
            if (i + 1 >= text.size()) return false;
            char e = text[i + 1];
            i += 2;
            switch (e)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    uint32_t cp;
                    if (!ReadHex4(text, i, cp)) return false;
                    i += 4;
                    uint32_t low;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= text.size() && text[i] == '\\' && text[i + 1] == 'u' &&
                        ReadHex4(text, i + 2, low) && low >= 0xDC00 && low <= 0xDFFF)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    static std::string EncodeJsonString(const std::string& value)
    {
        std::string out = "\"";
        for (unsigned char c : value)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) out += std::format("\\u{:04x}", (unsigned)c);
                    else out += (char)c;
            }
        }
        out += '"';
        return out;
    }

    size_t AssetFiles::RewriteJsonStrings(std::string& text, const JsonStringRemap& remap)
    {
        std::string out;
        size_t count = 0, copied = 0, i = 0;
        std::string key, value;
        bool keyPending = false; // a key and its ':' were read: the next string is its value
        // Open objects / arrays; an array remembers the key it belongs to (its items report it).
        struct Container { bool IsArray; std::string Key; };
        std::vector<Container> containers;
        while (i < text.size())
        {
            char c = text[i];
            if (c != '"')
            {
                if (c == '{' || c == '[')
                {
                    std::string owner = keyPending ? key
                                      : (!containers.empty() && containers.back().IsArray ? containers.back().Key : std::string());
                    containers.push_back({ c == '[', c == '[' ? std::move(owner) : std::string() });
                }
                else if ((c == '}' || c == ']') && !containers.empty())
                    containers.pop_back();
                if (c != ':' && !std::isspace((unsigned char)c))
                    keyPending = false;
                i++;
                continue;
            }
            size_t start = i;
            if (!ParseJsonString(text, i, value))
                break; // broken text: leave the rest alone
            size_t next = i;
            while (next < text.size() && std::isspace((unsigned char)text[next]))
                next++;
            if (next < text.size() && text[next] == ':')
            {
                key = value;
                keyPending = true;
                continue;
            }
            std::string_view owner = keyPending ? std::string_view(key)
                                   : (!containers.empty() && containers.back().IsArray ? std::string_view(containers.back().Key) : std::string_view());
            keyPending = false;
            std::optional<std::string> replacement = remap(owner, value);
            if (!replacement || *replacement == value)
                continue;
            out.append(text, copied, start - copied);
            out += EncodeJsonString(*replacement);
            copied = i;
            count++;
        }
        if (count > 0)
        {
            out.append(text, copied, std::string::npos);
            text = std::move(out);
        }
        return count;
    }

    // ------------------------------------------------------------------ model files (.gltf uri)
    static bool IsExternalUri(const std::string& uri)
    {
        return uri.empty() || uri.starts_with("data:") || uri.find("://") != std::string::npos || uri.front() == '/';
    }

    // "%20" -> ' ' (glTF file names are URIs).
    static std::string PercentDecode(const std::string& uri)
    {
        std::string out;
        for (size_t i = 0; i < uri.size(); i++)
        {
            uint32_t v = 0;
            if (uri[i] == '%' && i + 2 < uri.size() && ReadHex4("00" + uri.substr(i + 1, 2), 0, v))
            {
                out += (char)v;
                i += 2;
                continue;
            }
            out += uri[i];
        }
        return out;
    }

    static std::string PercentEncode(const std::string& path)
    {
        std::string out;
        for (char c : path)
        {
            if (c == ' ') out += "%20";
            else if (c == '%') out += "%25";
            else out += c;
        }
        return out;
    }

    std::vector<fs::path> AssetFiles::GetModelDependencies(const fs::path& modelPath)
    {
        std::vector<fs::path> deps;
        if (Lower(modelPath.extension().string()) != ".gltf")
            return deps;
        std::string text = FileSystem::ReadTextFile(modelPath);
        if (text.find("\"uri\"") == std::string::npos)
            return deps;
        fs::path dir = modelPath.parent_path();
        RewriteJsonStrings(text, [&](std::string_view key, const std::string& value) -> std::optional<std::string> {
            if (key != "uri" || IsExternalUri(value))
                return std::nullopt;
            fs::path dep = (dir / PercentDecode(value)).lexically_normal();
            std::error_code ec;
            if (fs::is_regular_file(dep, ec) && std::find(deps.begin(), deps.end(), dep) == deps.end())
                deps.push_back(dep);
            return std::nullopt;
        });
        return deps;
    }

    // ------------------------------------------------------------------ moving
    fs::path AssetFiles::MoveResult::MapPath(const fs::path& path) const
    {
        for (const Move& m : Moved)
        {
            if (path == m.From)
                return m.To;
            if (m.IsDirectory && IsUnder(path, m.From))
                return m.To / path.lexically_relative(m.From);
        }
        return path;
    }

    // Where a file that is at `path` now was before the moves.
    static fs::path MapBack(const AssetFiles::MoveResult& result, const fs::path& path)
    {
        for (const auto& m : result.Moved)
        {
            if (path == m.To)
                return m.From;
            if (m.IsDirectory && IsUnder(path, m.To))
                return m.From / path.lexically_relative(m.To);
        }
        return path;
    }

    std::string AssetFiles::CheckMove(const fs::path& source, const fs::path& destinationDir, const fs::path& assetDirectory)
    {
        std::error_code ec;
        fs::path src = Normalize(source), dst = Normalize(destinationDir);
        std::string name = src.filename().string();
        if (!fs::exists(src, ec))
            return std::format("'{}' topilmadi", name);
        if (!fs::is_directory(dst, ec))
            return std::format("'{}' papka emas", dst.filename().string());
        fs::path assets = assetDirectory.empty() ? fs::path() : Normalize(assetDirectory);
        if (!assets.empty() && src == assets)
            return "Assets papkasini ko'chirib bo'lmaydi";
        if (IsUnder(dst, src))
            return "Papkani o'zining ichiga ko'chirib bo'lmaydi";
        if (src.parent_path() == dst)
            return std::format("'{}' allaqachon shu papkada", name);
        if (!assets.empty() && IsUnder(src, assets) != IsUnder(dst, assets))
            return "Assets papkasidan tashqariga (yoki tashqaridan ichiga) ko'chirib bo'lmaydi";
        if (fs::exists(dst / src.filename(), ec))
            return std::format("'{}' papkasida '{}' nomli fayl allaqachon bor", dst.filename().string(), name);
        return {};
    }

    static std::error_code MoveOnDisk(const fs::path& from, const fs::path& to)
    {
        std::error_code ec;
        fs::rename(from, to, ec);
        if (ec == std::errc::cross_device_link)
        {
            // Another file system: copy, then remove the original.
            ec.clear();
            fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            if (!ec)
                fs::remove_all(from, ec);
        }
        return ec;
    }

    // The .zmeta file of a moved asset goes with it (replacing an orphan one at the new place).
    static void MoveMetaFile(const fs::path& from, const fs::path& to)
    {
        std::error_code ec;
        fs::path meta = AssetMetaFile::GetMetaPath(from);
        if (!fs::is_regular_file(meta, ec))
            return;
        fs::path target = AssetMetaFile::GetMetaPath(to);
        fs::rename(meta, target, ec);
        if (ec)
        {
            ec.clear();
            fs::copy_file(meta, target, fs::copy_options::overwrite_existing, ec);
            if (!ec)
                fs::remove(meta, ec);
        }
    }

    static std::string RelativeTo(const fs::path& path, const fs::path& base)
    {
        return path.lexically_relative(base).generic_string();
    }

    static bool IsSourceFile(const fs::path& p)
    {
        return AssetFiles::GetType(p) == AssetType::Script;
    }

    // Rewrites what refers to the moved items (see MoveInto).
    void AssetFiles::UpdateReferences(MoveResult& result, const fs::path& assetDirectory, const fs::path& sourceDirectory)
    {
        if (result.Moved.empty() || assetDirectory.empty())
            return;
        std::error_code ec;
        fs::path assets = Normalize(assetDirectory);

        struct RelativeMove { std::string From, To; bool IsDirectory; };
        std::vector<RelativeMove> moves;
        AssetFiles::PathMoves pathMoves;
        for (const auto& m : result.Moved)
            if (IsUnder(m.From, assets) && IsUnder(m.To, assets))
            {
                moves.push_back({ RelativeTo(m.From, assets), RelativeTo(m.To, assets), m.IsDirectory });
                pathMoves.emplace_back(moves.back().From, moves.back().To);
            }

        // Asset paths only: names, tags and texts that happen to equal a moved path stay.
        auto remapAssetPath = [&](std::string_view key, const std::string& value) {
            return AssetFiles::RemapAssetReference(key, value, pathMoves);
        };

        std::vector<fs::path> files;
        fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            std::error_code fec;
            if (it->path().filename().string().starts_with("."))
            {
                if (it->is_directory(fec)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(fec))
                files.push_back(it->path());
        }

        for (const fs::path& file : files)
        {
            std::string ext = Lower(file.extension().string());
            size_t replaced = 0;
            std::string text;
            if ((ext == ".zscene" || ext == ".zprefab" || ext == ".zmat") && !moves.empty())
            {
                text = FileSystem::ReadTextFile(file);
                bool mentioned = std::any_of(moves.begin(), moves.end(), [&](const RelativeMove& m) { return text.find(m.From) != std::string::npos; });
                if (mentioned)
                    replaced = AssetFiles::RewriteJsonStrings(text, remapAssetPath);
            }
            else if (ext == ".gltf")
            {
                // Relative file names inside a model: keep them pointing at the same files when the
                // model or the files moved.
                text = FileSystem::ReadTextFile(file);
                if (text.find("\"uri\"") == std::string::npos)
                    continue;
                fs::path oldModel = MapBack(result, file);
                bool modelMoved = oldModel != file;
                replaced = AssetFiles::RewriteJsonStrings(text, [&](std::string_view key, const std::string& value) -> std::optional<std::string> {
                    if (key != "uri" || IsExternalUri(value))
                        return std::nullopt;
                    std::string decoded = PercentDecode(value);
                    fs::path oldTarget = (oldModel.parent_path() / decoded).lexically_normal();
                    fs::path newTarget = result.MapPath(oldTarget);
                    if (!modelMoved && newTarget == oldTarget)
                        return std::nullopt;
                    std::string uri = newTarget.lexically_relative(file.parent_path()).generic_string();
                    if (uri.empty() || uri == decoded)
                        return std::nullopt;
                    return PercentEncode(uri);
                });
            }
            if (replaced > 0 && FileSystem::WriteTextFile(file, text))
            {
                result.UpdatedFiles.push_back(file);
                result.UpdatedReferences += replaced;
            }
        }

        // C++ code is not rewritten, only reported (string literals with the old path).
        if (sourceDirectory.empty() || moves.empty() || !fs::is_directory(sourceDirectory, ec))
            return;
        for (fs::recursive_directory_iterator sit(sourceDirectory, fs::directory_options::skip_permission_denied, ec), send;
             !ec && sit != send; sit.increment(ec))
        {
            std::error_code fec;
            if (!sit->is_regular_file(fec) || !IsSourceFile(sit->path()))
                continue;
            std::string code = FileSystem::ReadTextFile(sit->path());
            bool found = std::any_of(moves.begin(), moves.end(), [&](const RelativeMove& m) {
                return code.find("\"" + m.From + "\"") != std::string::npos ||
                       (m.IsDirectory && code.find("\"" + m.From + "/") != std::string::npos);
            });
            if (found)
                result.SourceMentions.push_back(sit->path());
        }
    }

    AssetFiles::MoveResult AssetFiles::MoveInto(const std::vector<fs::path>& sources, const fs::path& destinationDir,
                                                const fs::path& assetDirectory, const fs::path& sourceDirectory)
    {
        MoveResult result;
        fs::path dst = Normalize(destinationDir);
        std::vector<fs::path> items;
        for (const auto& s : sources)
        {
            fs::path n = Normalize(s);
            if (std::find(items.begin(), items.end(), n) == items.end())
                items.push_back(n);
        }
        // Parents first: whatever is inside a moved folder goes with it.
        std::sort(items.begin(), items.end(), [](const fs::path& a, const fs::path& b) {
            return std::distance(a.begin(), a.end()) < std::distance(b.begin(), b.end());
        });
        for (const fs::path& src : items)
        {
            bool insideMoved = std::any_of(result.Moved.begin(), result.Moved.end(),
                                           [&](const Move& m) { return m.IsDirectory && IsUnder(src, m.From); });
            if (insideMoved)
                continue;
            std::string reason = CheckMove(src, dst, assetDirectory);
            if (!reason.empty())
            {
                result.Errors.push_back(std::move(reason));
                continue;
            }
            std::error_code ec;
            Move move{ src, dst / src.filename(), fs::is_directory(src, ec) };
            ec = MoveOnDisk(move.From, move.To);
            if (ec)
            {
                result.Errors.push_back(std::format("'{}' ko'chirilmadi: {}", src.filename().string(), ec.message()));
                continue;
            }
            if (!move.IsDirectory)
                MoveMetaFile(move.From, move.To);
            result.Moved.push_back(std::move(move));
        }
        UpdateReferences(result, assetDirectory, sourceDirectory);
        return result;
    }

    AssetFiles::MoveResult AssetFiles::Rename(const fs::path& source, const std::string& newName,
                                              const fs::path& assetDirectory, const fs::path& sourceDirectory)
    {
        MoveResult result;
        std::error_code ec;
        fs::path src = Normalize(source);
        if (newName.empty() || newName == "." || newName == ".." || newName.find_first_of("/\\") != std::string::npos ||
            newName.find('\0') != std::string::npos)
        {
            result.Errors.push_back(std::format("'{}' - noto'g'ri nom", newName));
            return result;
        }
        if (!fs::exists(src, ec))
        {
            result.Errors.push_back(std::format("'{}' topilmadi", src.filename().string()));
            return result;
        }
        if (!assetDirectory.empty() && src == Normalize(assetDirectory))
        {
            result.Errors.push_back("Assets papkasini qayta nomlab bo'lmaydi");
            return result;
        }
        fs::path target = src.parent_path() / newName;
        if (target == src)
            return result;
        if (fs::exists(target, ec))
        {
            result.Errors.push_back(std::format("'{}' nomli fayl allaqachon bor", newName));
            return result;
        }
        Move move{ src, target, fs::is_directory(src, ec) };
        ec = MoveOnDisk(move.From, move.To);
        if (ec)
        {
            result.Errors.push_back(std::format("Nomlab bo'lmadi: {}", ec.message()));
            return result;
        }
        if (!move.IsDirectory)
            MoveMetaFile(move.From, move.To);
        result.Moved.push_back(std::move(move));
        UpdateReferences(result, assetDirectory, sourceDirectory);
        return result;
    }

    // ------------------------------------------------------------------ deleting
    bool AssetFiles::Remove(const fs::path& path, std::string* error)
    {
        std::error_code ec;
        bool isDirectory = fs::is_directory(path, ec);
        if (!fs::exists(path, ec))
        {
            if (error) *error = std::format("'{}' topilmadi", path.filename().string());
            return false;
        }
        fs::remove_all(path, ec);
        if (ec)
        {
            if (error) *error = std::format("'{}' o'chirilmadi: {}", path.filename().string(), ec.message());
            return false;
        }
        if (!isDirectory)
            fs::remove(AssetMetaFile::GetMetaPath(path), ec); // the asset's .zmeta goes with it
        return true;
    }

    // ------------------------------------------------------------------ importing
    AssetFiles::ImportResult AssetFiles::Import(const std::vector<fs::path>& sources, const fs::path& destinationDir)
    {
        ImportResult result;
        std::error_code ec;
        fs::create_directories(destinationDir, ec);
        fs::path dst = Normalize(destinationDir);
        if (!fs::is_directory(dst, ec))
        {
            result.Errors.push_back(std::format("'{}' papkasini yaratib bo'lmadi", destinationDir.string()));
            return result;
        }
        for (const fs::path& source : sources)
        {
            fs::path src = Normalize(source);
            std::string name = src.filename().string();
            if (!fs::exists(src, ec))
            {
                result.Errors.push_back(std::format("'{}' topilmadi", name));
                continue;
            }
            if (IsUnder(dst, src))
            {
                result.Errors.push_back(std::format("'{}' papkasini o'zining ichiga import qilib bo'lmaydi", name));
                continue;
            }

            if (fs::is_directory(src, ec))
            {
                fs::path target = MakeUniquePath(dst, src.filename());
                fs::copy(src, target, fs::copy_options::recursive, ec);
                if (ec) result.Errors.push_back(std::format("'{}' nusxalanmadi: {}", name, ec.message()));
                else result.Imported.push_back(target);
                continue;
            }

            // Import settings come along (a UUID that is already taken gets replaced by the registry).
            auto copyMetaFile = [](const fs::path& from, const fs::path& to) {
                std::error_code mec;
                fs::path meta = AssetMetaFile::GetMetaPath(from);
                if (fs::is_regular_file(meta, mec))
                    fs::copy_file(meta, AssetMetaFile::GetMetaPath(to), fs::copy_options::overwrite_existing, mec);
            };
            std::vector<fs::path> deps = GetModelDependencies(src);
            if (deps.empty())
            {
                fs::path target = MakeUniquePath(dst, src.filename());
                fs::copy_file(src, target, ec);
                if (ec) result.Errors.push_back(std::format("'{}' nusxalanmadi: {}", name, ec.message()));
                else
                {
                    copyMetaFile(src, target);
                    result.Imported.push_back(target);
                }
                continue;
            }

            // A model with separate buffers / textures: a folder of its own, the files beside it.
            fs::path folder = MakeUniquePath(dst, src.stem());
            fs::create_directories(folder, ec);
            fs::path target = folder / src.filename();
            fs::copy_file(src, target, ec);
            if (ec)
            {
                result.Errors.push_back(std::format("'{}' nusxalanmadi: {}", name, ec.message()));
                continue;
            }
            copyMetaFile(src, target);
            std::map<fs::path, std::string> moved; // files from outside the model's folder: new uri
            fs::path srcDir = src.parent_path();
            for (const fs::path& dep : deps)
            {
                std::error_code dec;
                fs::path out;
                if (IsUnder(dep, srcDir))
                {
                    out = folder / dep.lexically_relative(srcDir); // the same relative name works
                    fs::create_directories(out.parent_path(), dec);
                }
                else
                {
                    out = MakeUniquePath(folder, dep.filename());
                    moved[dep] = out.lexically_relative(folder).generic_string();
                }
                fs::copy_file(dep, out, fs::copy_options::skip_existing, dec);
                if (dec)
                    result.Errors.push_back(std::format("'{}' ({}) nusxalanmadi: {}", dep.filename().string(), name, dec.message()));
            }
            if (!moved.empty())
            {
                std::string text = FileSystem::ReadTextFile(target);
                size_t n = RewriteJsonStrings(text, [&](std::string_view key, const std::string& value) -> std::optional<std::string> {
                    if (key != "uri" || IsExternalUri(value))
                        return std::nullopt;
                    auto it = moved.find((srcDir / PercentDecode(value)).lexically_normal());
                    if (it == moved.end())
                        return std::nullopt;
                    return PercentEncode(it->second);
                });
                if (n > 0)
                    FileSystem::WriteTextFile(target, text);
            }
            result.Imported.push_back(target);
        }
        return result;
    }

}
