#include "Zamin/Asset/AssetReferences.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Project/AssetFiles.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <deque>
#include <functional>

namespace Zamin {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    static std::string LowerExtension(const fs::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return ext;
    }

    static fs::path Absolute(const fs::path& path)
    {
        std::error_code ec;
        fs::path abs = fs::absolute(path, ec);
        return (ec ? path : abs).lexically_normal();
    }

    static std::string PercentDecode(std::string_view uri)
    {
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::string out;
        for (size_t i = 0; i < uri.size(); i++)
        {
            if (uri[i] == '%' && i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0)
            {
                out += (char)(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
                i += 2;
            }
            else
                out += uri[i];
        }
        return out;
    }

    static bool HasRegisteredExtension(const std::string& path)
    {
        return AssetTypes::IsRegistered(AssetTypes::FromPath(path));
    }

    // ------------------------------------------------------------------ helpers
    std::string AssetReferenceIndex::ToKey(const fs::path& file) const
    {
        return Absolute(file).generic_string();
    }

    std::string AssetReferenceIndex::ToAssetPath(const fs::path& path) const
    {
        if (m_AssetDirectory.empty())
            return {};
        fs::path relative = Absolute(path).lexically_relative(m_AssetDirectory);
        std::string text = relative.generic_string();
        if (text.empty() || text == "." || text.starts_with(".."))
            return {};
        return text;
    }

    bool AssetReferenceIndex::AssetExists(const std::string& relativePath) const
    {
        if (relativePath.empty())
            return false;
        if (m_AssetSet.count(relativePath))
            return true;
        std::error_code ec;
        return fs::is_regular_file(m_AssetDirectory / relativePath, ec); // model buffers and other files
    }

    std::string AssetReferenceIndex::ReadText(const fs::path& file) const
    {
        if (auto it = m_Sources.Overrides.find(ToKey(file)); it != m_Sources.Overrides.end())
            return it->second;
        if (auto it = m_Sources.Overrides.find(file.string()); it != m_Sources.Overrides.end())
            return it->second;
        return FileSystem::ReadTextFile(file);
    }

    void AssetReferenceIndex::AddReference(const fs::path& file, const std::string& asset, AssetReferenceKind kind)
    {
        std::string key = ToKey(file);
        if (key == ToKey(m_AssetDirectory / asset))
            return; // a scene that names itself (restart) does not keep itself alive
        m_ReferenceCount++;
        auto& referrers = m_Referrers[asset];
        auto it = std::find_if(referrers.begin(), referrers.end(), [&](const AssetReferrer& r) { return r.File == fs::path(key); });
        if (it == referrers.end())
        {
            AssetReferrer referrer;
            referrer.File = key;
            referrer.Name = fs::path(key).lexically_relative(m_ProjectDirectory).generic_string();
            if (referrer.Name.empty() || referrer.Name.starts_with(".."))
                referrer.Name = key;
            referrer.AssetPath = ToAssetPath(file);
            referrers.push_back(std::move(referrer));
            it = referrers.end() - 1;
        }
        it->Count++;
        it->Kinds |= (uint8_t)kind;
        auto& outgoing = m_Outgoing[key];
        if (std::find(outgoing.begin(), outgoing.end(), asset) == outgoing.end())
            outgoing.push_back(asset);
    }

    void AssetReferenceIndex::AddMissing(const fs::path& file, const std::string& path)
    {
        std::string key = ToKey(file);
        for (const MissingAssetReference& m : m_Missing)
            if (m.Path == path && m.File == fs::path(key))
                return;
        MissingAssetReference missing;
        missing.File = key;
        missing.Name = fs::path(key).lexically_relative(m_ProjectDirectory).generic_string();
        missing.Path = path;
        m_Missing.push_back(std::move(missing));
    }

    // ------------------------------------------------------------------ scanning
    void AssetReferenceIndex::ScanJson(const fs::path& file, const std::string& text)
    {
        json root = json::parse(text, nullptr, false);
        if (root.is_discarded())
            return;
        const AssetRegistry* registry = m_Sources.Registry;
        auto pathOfHandle = [&](const std::string& value) -> std::string {
            if (!registry)
                return {};
            auto handle = AssetHandle::FromString(value);
            const AssetMetadata* meta = handle ? registry->Find(*handle) : nullptr;
            return meta ? meta->RelativePath : std::string();
        };
        // An asset path stored under `key`; `uuid` is the "<key>UUID" next to it ("" = none).
        auto visitPath = [&](std::string_view key, const std::string& value, const std::string& uuid) {
            if (!AssetFiles::IsAssetPathValue(key, value))
                return;
            fs::path p(value);
            std::string relative = p.is_absolute() ? ToAssetPath(p) : p.lexically_normal().generic_string();
            if (relative.starts_with(".."))
                return; // outside the asset folder
            std::string byHandle = uuid.empty() ? std::string() : pathOfHandle(uuid);
            if (!relative.empty() && AssetExists(relative))
            {
                uint8_t kinds = (uint8_t)AssetReferenceKind::Path | (byHandle == relative ? (uint8_t)AssetReferenceKind::UUID : 0);
                AddReference(file, relative, (AssetReferenceKind)kinds);
            }
            else if (!byHandle.empty())
                AddReference(file, byHandle, AssetReferenceKind::UUID); // repaired from the UUID on load
            else if (!relative.empty() && HasRegisteredExtension(relative))
                AddMissing(file, value);
        };

        std::function<void(const json&, std::string_view)> walk = [&](const json& node, std::string_view key) {
            if (node.is_object())
            {
                for (auto it = node.begin(); it != node.end(); ++it)
                {
                    const std::string& k = it.key();
                    const json& v = it.value();
                    if (!v.is_string())
                    {
                        walk(v, k);
                        continue;
                    }
                    const std::string& value = v.get_ref<const std::string&>();
                    if (k.size() > 4 && k.ends_with("UUID"))
                    {
                        // Belongs to the path next to it; alone (the path key is gone) it still refers.
                        std::string base = k.substr(0, k.size() - 4);
                        auto pathIt = node.find(base);
                        if (pathIt == node.end() || !pathIt->is_string())
                            if (std::string path = pathOfHandle(value); !path.empty())
                                AddReference(file, path, AssetReferenceKind::UUID);
                        continue;
                    }
                    std::string uuid;
                    if (auto uuidIt = node.find(k + "UUID"); uuidIt != node.end() && uuidIt->is_string())
                        uuid = uuidIt->get<std::string>();
                    visitPath(k, value, uuid);
                }
            }
            else if (node.is_array())
            {
                for (const json& item : node)
                {
                    if (item.is_string())
                        visitPath(key, item.get_ref<const std::string&>(), {});
                    else
                        walk(item, key);
                }
            }
        };
        walk(root, "");
    }

    void AssetReferenceIndex::ScanModel(const fs::path& file, const std::string& text)
    {
        json root = json::parse(text, nullptr, false);
        if (root.is_discarded())
            return;
        fs::path dir = file.parent_path();
        for (const char* list : { "buffers", "images" })
        {
            auto it = root.find(list);
            if (it == root.end() || !it->is_array())
                continue;
            for (const json& item : *it)
            {
                if (!item.is_object() || !item.contains("uri") || !item["uri"].is_string())
                    continue;
                std::string uri = item["uri"].get<std::string>();
                if (uri.empty() || uri.starts_with("data:") || uri.find("://") != std::string::npos)
                    continue;
                std::string relative = ToAssetPath(dir / PercentDecode(uri));
                if (relative.empty())
                    continue;
                if (AssetExists(relative))
                    AddReference(file, relative, AssetReferenceKind::ModelFile);
                else
                    AddMissing(file, relative);
            }
        }
    }

    // String literals of C++ code (comments, character literals and digit separators skipped).
    static std::vector<std::string> FindStringLiterals(const std::string& code)
    {
        std::vector<std::string> literals;
        size_t n = code.size();
        for (size_t i = 0; i < n; i++)
        {
            char c = code[i];
            if (c == '/' && i + 1 < n && code[i + 1] == '/')
            {
                i = code.find('\n', i);
                if (i == std::string::npos) break;
                continue;
            }
            if (c == '/' && i + 1 < n && code[i + 1] == '*')
            {
                i = code.find("*/", i + 2);
                if (i == std::string::npos) break;
                i++;
                continue;
            }
            if (c == '\'')
            {
                if (i > 0 && std::isalnum((unsigned char)code[i - 1]))
                    continue; // 1'000'000
                for (i++; i < n && code[i] != '\'' && code[i] != '\n'; i++)
                    if (code[i] == '\\') i++;
                continue;
            }
            if (c != '"')
                continue;
            if (i > 0 && code[i - 1] == 'R')
            {
                // R"delim( ... )delim"
                size_t open = code.find('(', i + 1);
                if (open == std::string::npos) break;
                std::string end = ")" + code.substr(i + 1, open - i - 1) + "\"";
                size_t close = code.find(end, open + 1);
                if (close == std::string::npos) break;
                literals.push_back(code.substr(open + 1, close - open - 1));
                i = close + end.size() - 1;
                continue;
            }
            std::string literal;
            for (i++; i < n && code[i] != '"' && code[i] != '\n'; i++)
            {
                if (code[i] == '\\' && i + 1 < n)
                    i++;
                literal += code[i];
            }
            literals.push_back(std::move(literal));
        }
        return literals;
    }

    void AssetReferenceIndex::ScanCode(const fs::path& file, const std::string& text)
    {
        for (const std::string& literal : FindStringLiterals(text))
        {
            if (literal.empty() || literal.size() > 260 || (literal.find('/') == std::string::npos && !fs::path(literal).has_extension()))
                continue;
            fs::path p(literal);
            if (p.is_absolute())
                continue;
            std::string relative = p.lexically_normal().generic_string();
            if (relative.starts_with(".."))
                continue;
            if (AssetExists(relative))
                AddReference(file, relative, AssetReferenceKind::Code);
            else if (size_t slash = relative.find('/'); slash != std::string::npos && HasRegisteredExtension(relative) &&
                     m_TopFolders.count(relative.substr(0, slash)))
                AddMissing(file, literal); // "Audio/jump.wav" spelled in code, but no such file
        }
    }

    // ------------------------------------------------------------------ building
    void AssetReferenceIndex::Clear()
    {
        m_Sources = {};
        m_AssetDirectory.clear();
        m_ProjectDirectory.clear();
        m_AssetSet.clear();
        m_AssetList.clear();
        m_TopFolders.clear();
        m_Referrers.clear();
        m_Outgoing.clear();
        m_Roots.clear();
        m_Missing.clear();
        m_ScannedFiles = 0;
        m_ReferenceCount = 0;
        m_Built = false;
    }

    void AssetReferenceIndex::Build(const Sources& sources)
    {
        Sources copy = sources; // `sources` may be m_Sources itself
        Clear();
        m_Sources = std::move(copy);
        // Overrides by normalized absolute path.
        std::unordered_map<std::string, std::string> overrides;
        for (auto& [path, text] : m_Sources.Overrides)
            overrides[ToKey(path)] = std::move(text);
        m_Sources.Overrides = std::move(overrides);
        m_AssetDirectory = Absolute(m_Sources.AssetDirectory);
        m_ProjectDirectory = m_Sources.ProjectDirectory.empty() ? m_AssetDirectory.parent_path() : Absolute(m_Sources.ProjectDirectory);
        m_Built = true;
        std::error_code ec;
        if (m_AssetDirectory.empty() || !fs::is_directory(m_AssetDirectory, ec))
            return;

        // ---- every file of the asset folder (hidden names and .zmeta files skipped)
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(m_AssetDirectory, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            std::error_code fec;
            std::string name = it->path().filename().string();
            bool directory = it->is_directory(fec);
            if (name.starts_with("."))
            {
                if (directory) it.disable_recursion_pending();
                continue;
            }
            if (directory)
            {
                if (it.depth() == 0)
                    m_TopFolders.insert(name);
                continue;
            }
            if (!it->is_regular_file(fec) || LowerExtension(it->path()) == ".zmeta")
                continue;
            files.push_back(it->path());
        }

        if (m_Sources.Registry)
        {
            for (const AssetMetadata* meta : m_Sources.Registry->GetAssets())
                m_AssetList.push_back(meta->RelativePath);
        }
        else
        {
            for (const fs::path& file : files)
                if (AssetTypes::IsRegistered(AssetTypes::FromPath(file)))
                    m_AssetList.push_back(ToAssetPath(file));
        }
        std::sort(m_AssetList.begin(), m_AssetList.end());
        m_AssetSet.insert(m_AssetList.begin(), m_AssetList.end());

        // ---- files that refer to assets
        for (const fs::path& file : files)
        {
            std::string ext = LowerExtension(file);
            if (ext == ".zscene" || ext == ".zprefab" || ext == ".zmat")
                ScanJson(file, ReadText(file));
            else if (ext == ".gltf")
                ScanModel(file, ReadText(file));
            else
                continue;
            m_ScannedFiles++;
        }
        if (!m_Sources.ProjectFile.empty() && fs::is_regular_file(m_Sources.ProjectFile, ec))
        {
            ScanJson(m_Sources.ProjectFile, ReadText(m_Sources.ProjectFile));
            m_Roots.insert(ToKey(m_Sources.ProjectFile));
            m_ScannedFiles++;
        }
        if (!m_Sources.SourceDirectory.empty() && fs::is_directory(m_Sources.SourceDirectory, ec))
        {
            for (fs::recursive_directory_iterator it(m_Sources.SourceDirectory, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                std::error_code fec;
                if (it->path().filename().string().starts_with("."))
                {
                    if (it->is_directory(fec)) it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(fec) || AssetTypes::FromPath(it->path()) != AssetType::Script)
                    continue;
                ScanCode(it->path(), ReadText(it->path()));
                m_Roots.insert(ToKey(it->path()));
                m_ScannedFiles++;
            }
        }

        for (auto& [asset, referrers] : m_Referrers)
            std::sort(referrers.begin(), referrers.end(), [](const AssetReferrer& a, const AssetReferrer& b) {
                return AssetFiles::CompareNames(a.Name, b.Name) < 0;
            });
        std::sort(m_Missing.begin(), m_Missing.end(), [](const MissingAssetReference& a, const MissingAssetReference& b) {
            return a.Name != b.Name ? AssetFiles::CompareNames(a.Name, b.Name) < 0 : a.Path < b.Path;
        });
    }

    bool AssetReferenceIndex::BuildForActiveProject(std::unordered_map<std::string, std::string> overrides)
    {
        if (!Project::GetActive())
        {
            Clear();
            return false;
        }
        Sources sources;
        sources.AssetDirectory = Project::GetAssetDirectory();
        sources.ProjectDirectory = Project::GetProjectDirectory();
        sources.ProjectFile = Project::GetProjectFilePath();
        sources.SourceDirectory = Project::GetSourceDirectory();
        sources.Registry = Project::GetAssetRegistry();
        sources.Overrides = std::move(overrides);
        Build(sources);
        return true;
    }

    // ------------------------------------------------------------------ queries
    const std::vector<AssetReferrer>& AssetReferenceIndex::GetReferrers(const fs::path& asset) const
    {
        static const std::vector<AssetReferrer> s_None;
        std::string relative = asset.is_absolute() ? ToAssetPath(asset) : asset.lexically_normal().generic_string();
        auto it = m_Referrers.find(relative);
        return it != m_Referrers.end() ? it->second : s_None;
    }

    uint32_t AssetInUse::GetReferenceCount() const
    {
        uint32_t count = 0;
        for (const AssetReferrer& r : Referrers)
            count += r.Count;
        return count;
    }

    std::vector<AssetInUse> AssetReferenceIndex::FindUsesOutside(const std::vector<fs::path>& paths) const
    {
        std::vector<AssetInUse> uses;
        if (!m_Built || paths.empty())
            return uses;
        // What goes: asset-relative paths (to find the assets) and absolute keys (to leave out
        // referrers that go with them).
        std::vector<std::string> relatives, keys;
        auto trim = [](std::string text) {
            while (text.size() > 1 && text.back() == '/')
                text.pop_back(); // "Textures/" = the folder
            return text;
        };
        for (const fs::path& p : paths)
        {
            fs::path absolute = p.is_absolute() ? p : m_AssetDirectory / p;
            keys.push_back(trim(ToKey(absolute)));
            if (std::string rel = trim(ToAssetPath(absolute)); !rel.empty() && rel != ".")
                relatives.push_back(std::move(rel));
        }
        auto inside = [](const std::string& path, const std::vector<std::string>& roots) {
            for (const std::string& root : roots)
                if (path == root || (path.size() > root.size() && path.starts_with(root) && path[root.size()] == '/'))
                    return true;
            return false;
        };
        if (relatives.empty())
            return uses;
        for (const auto& [asset, referrers] : m_Referrers)
        {
            if (!inside(asset, relatives))
                continue;
            AssetInUse use;
            use.Path = asset;
            use.Type = AssetTypes::FromPath(asset);
            for (const AssetReferrer& r : referrers)
                if (!inside(ToKey(r.File), keys))
                    use.Referrers.push_back(r);
            if (!use.Referrers.empty())
                uses.push_back(std::move(use));
        }
        std::sort(uses.begin(), uses.end(), [](const AssetInUse& a, const AssetInUse& b) { return a.Path < b.Path; });
        return uses;
    }

    std::vector<std::string> AssetReferenceIndex::GetReferences(const fs::path& file) const
    {
        std::string key = ToKey(file.is_absolute() ? file : m_AssetDirectory / file);
        auto it = m_Outgoing.find(key);
        if (it == m_Outgoing.end())
            return {};
        std::vector<std::string> out = it->second;
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<UnusedAsset> AssetReferenceIndex::FindUnused(bool reachableOnly) const
    {
        std::unordered_set<std::string> used;
        if (reachableOnly)
        {
            // Everything the project file and the code lead to, through the files they lead to.
            std::deque<std::string> queue(m_Roots.begin(), m_Roots.end());
            while (!queue.empty())
            {
                std::string key = std::move(queue.front());
                queue.pop_front();
                auto it = m_Outgoing.find(key);
                if (it == m_Outgoing.end())
                    continue;
                for (const std::string& asset : it->second)
                    if (used.insert(asset).second)
                        queue.push_back(ToKey(m_AssetDirectory / asset));
            }
        }
        else
        {
            for (const auto& [asset, referrers] : m_Referrers)
                if (!referrers.empty())
                    used.insert(asset);
        }

        std::vector<UnusedAsset> unused;
        for (const std::string& asset : m_AssetList)
        {
            if (used.count(asset))
                continue;
            UnusedAsset entry;
            entry.Path = asset;
            entry.Type = AssetTypes::FromPath(asset);
            std::error_code ec;
            uintmax_t size = fs::file_size(m_AssetDirectory / asset, ec);
            entry.Size = ec ? 0 : size;
            unused.push_back(std::move(entry));
        }
        return unused;
    }

}
