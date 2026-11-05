#include "Zamin/Asset/AssetRegistry.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>

namespace Zamin {

    namespace fs = std::filesystem;

    static bool s_WriteMetaFilesByDefault = false;

    AssetRegistry::AssetRegistry() : m_WriteMetaFiles(s_WriteMetaFilesByDefault) {}
    AssetRegistry::AssetRegistry(bool writeMetaFiles) : m_WriteMetaFiles(writeMetaFiles) {}

    void AssetRegistry::SetWriteMetaFilesByDefault(bool write) { s_WriteMetaFilesByDefault = write; }
    bool AssetRegistry::GetWriteMetaFilesByDefault() { return s_WriteMetaFilesByDefault; }

    // ------------------------------------------------------------------ paths
    fs::path AssetRegistry::NormalizeDirectory(const fs::path& directory)
    {
        std::error_code ec;
        fs::path result = fs::absolute(directory, ec);
        if (ec)
            result = directory;
        result = result.lexically_normal();
        while (!result.has_filename() && result.has_relative_path())
            result = result.parent_path(); // "Assets/" -> "Assets"
        return result;
    }

    std::string AssetRegistry::NormalizeRelative(const fs::path& path)
    {
        std::string rel = path.lexically_normal().generic_string();
        while (rel.size() > 1 && rel.back() == '/')
            rel.pop_back();
        if (rel.empty() || rel == "." || rel == ".." || rel.starts_with("../") || rel.starts_with("/"))
            return {};
        return rel;
    }

    std::string AssetRegistry::ToRelativePath(const fs::path& path) const
    {
        if (path.empty())
            return {};
        if (path.is_relative())
            return NormalizeRelative(path);
        if (m_AssetDirectory.empty())
            return {};
        if (std::string rel = NormalizeRelative(path.lexically_normal().lexically_relative(m_AssetDirectory)); !rel.empty())
            return rel;
        // Another spelling of the same place (symlinks, "..").
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(path, ec);
        if (ec || m_CanonicalDirectory.empty())
            return {};
        return NormalizeRelative(canonical.lexically_relative(m_CanonicalDirectory));
    }

    fs::path AssetRegistry::ToFileSystemPath(std::string_view relativePath) const
    {
        return m_AssetDirectory / fs::path(relativePath);
    }

    bool AssetRegistry::ExistsOnDisk(const std::string& relativePath) const
    {
        std::error_code ec;
        return !relativePath.empty() && fs::is_regular_file(ToFileSystemPath(relativePath), ec);
    }

    // ------------------------------------------------------------------ lookups
    const AssetMetadata* AssetRegistry::Find(AssetHandle handle) const
    {
        auto it = m_Assets.find(handle);
        return it != m_Assets.end() ? &it->second : nullptr;
    }

    const AssetMetadata* AssetRegistry::Find(const fs::path& path) const
    {
        auto it = m_Paths.find(ToRelativePath(path));
        return it != m_Paths.end() ? Find(it->second) : nullptr;
    }

    AssetHandle AssetRegistry::GetHandle(const fs::path& path) const
    {
        const AssetMetadata* meta = Find(path);
        return meta ? meta->Handle : AssetHandle();
    }

    std::string AssetRegistry::GetPath(AssetHandle handle) const
    {
        const AssetMetadata* meta = Find(handle);
        return meta ? meta->RelativePath : std::string();
    }

    std::vector<const AssetMetadata*> AssetRegistry::GetAssets(std::optional<AssetType> type) const
    {
        std::vector<const AssetMetadata*> assets;
        assets.reserve(m_Assets.size());
        for (const auto& [handle, meta] : m_Assets)
            if (!type || meta.Type == *type)
                assets.push_back(&meta);
        std::sort(assets.begin(), assets.end(), [](const AssetMetadata* a, const AssetMetadata* b) { return a->RelativePath < b->RelativePath; });
        return assets;
    }

    // ------------------------------------------------------------------ registering
    void AssetRegistry::CollectAssets(const fs::path& directory, std::vector<std::string>& assets, std::vector<std::string>* metaFiles) const
    {
        std::error_code ec;
        fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            std::error_code fec;
            std::string name = it->path().filename().string();
            if (name.empty() || name.starts_with("."))
            {
                if (it->is_directory(fec))
                    it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory(fec))
            {
                if (it->is_symlink(fec))
                    it.disable_recursion_pending(); // no loops through links
                continue;
            }
            if (!it->is_regular_file(fec))
                continue;
            std::string rel = ToRelativePath(it->path());
            if (rel.empty())
                continue;
            if (AssetMetaFile::IsMetaFile(it->path()))
            {
                if (metaFiles)
                    metaFiles->push_back(NormalizeRelative(AssetMetaFile::GetAssetPath(rel)));
            }
            else if (AssetTypes::IsRegistered(AssetTypes::FromPath(it->path())))
                assets.push_back(std::move(rel));
        }
    }

    AssetRegistry::Candidate AssetRegistry::ReadCandidate(const std::string& relativePath, AssetType type) const
    {
        Candidate c;
        c.RelativePath = relativePath;
        c.Type = type;
        c.Meta = AssetMetaFile::Read(ToFileSystemPath(relativePath), &c.MetaError);
        return c;
    }

    bool AssetRegistry::SaveMeta(AssetMetadata& metadata)
    {
        if (!m_WriteMetaFiles)
            return false;
        if (!AssetMetaFile::Write(ToFileSystemPath(metadata.RelativePath), metadata))
            return false;
        metadata.HasMetaFile = true;
        return true;
    }

    const AssetMetadata* AssetRegistry::Insert(const Candidate& c, bool duplicate, ScanResult* scan)
    {
        AssetMetadata meta;
        meta.RelativePath = c.RelativePath;
        meta.Type = c.Type;
        bool write = false;
        if (c.Meta && !duplicate)
        {
            meta.Handle = c.Meta->Handle;
            meta.Import = c.Meta->Import;
            meta.HasMetaFile = true;
            if (c.Meta->Type != c.Type)
            {
                write = true; // the file is what its extension says
                if (scan) scan->RepairedMeta++;
            }
        }
        else if (c.Meta && duplicate)
        {
            // A copied file with its .zmeta: same settings, an identity of its own.
            meta.Handle = AssetHandle::Generate();
            meta.Import = c.Meta->Import;
            write = true;
            if (scan)
            {
                scan->RepairedMeta++;
                scan->Problems.push_back(std::format("'{}': UUID boshqa assetniki bilan bir xil - yangi UUID berildi", c.RelativePath));
            }
        }
        else if (!c.MetaError.empty())
        {
            // Unreadable (a merge conflict?): not overwritten, the user may want to fix it.
            meta.Handle = AssetHandle::Generate();
            if (scan)
            {
                scan->BrokenMeta++;
                scan->Problems.push_back(std::format("'{}.zmeta' o'qilmadi ({}) - tuzating yoki o'chiring", c.RelativePath, c.MetaError));
            }
        }
        else
        {
            meta.Handle = AssetHandle::Generate();
            write = true;
            if (scan && m_WriteMetaFiles) scan->CreatedMeta++;
        }
        if (write && SaveMeta(meta) == false && duplicate)
            meta.HasMetaFile = false; // the .zmeta on disk holds the other asset's UUID

        // Whatever was registered under this path before is replaced.
        if (auto old = m_Paths.find(meta.RelativePath); old != m_Paths.end())
            Erase(old->second);
        AssetHandle key = meta.Handle;
        m_Paths[meta.RelativePath] = key;
        auto [it, inserted] = m_Assets.insert_or_assign(key, std::move(meta));
        return &it->second;
    }

    void AssetRegistry::Erase(AssetHandle handle)
    {
        auto it = m_Assets.find(handle);
        if (it == m_Assets.end())
            return;
        if (auto p = m_Paths.find(it->second.RelativePath); p != m_Paths.end() && p->second == handle)
            m_Paths.erase(p);
        m_Assets.erase(it);
    }

    std::vector<std::string> AssetRegistry::TakeProblems()
    {
        std::vector<std::string> problems = std::move(m_Problems);
        m_Problems.clear();
        return problems;
    }

    void AssetRegistry::Clear()
    {
        m_Assets.clear();
        m_Paths.clear();
        m_Problems.clear();
        m_LastScan = {};
        m_Scanned = false;
    }

    const AssetRegistry::ScanResult& AssetRegistry::Scan(const fs::path& assetDirectory)
    {
        Clear();
        std::error_code ec;
        m_AssetDirectory = NormalizeDirectory(assetDirectory);
        m_CanonicalDirectory = fs::weakly_canonical(m_AssetDirectory, ec);
        if (ec)
            m_CanonicalDirectory = m_AssetDirectory;
        m_Scanned = true;

        ScanResult result;
        if (!fs::is_directory(m_AssetDirectory, ec))
        {
            m_LastScan = result;
            return m_LastScan;
        }

        std::vector<std::string> assets, metaFiles;
        CollectAssets(m_AssetDirectory, assets, &metaFiles);
        std::sort(assets.begin(), assets.end());

        std::vector<Candidate> candidates;
        candidates.reserve(assets.size());
        for (const std::string& rel : assets)
            candidates.push_back(ReadCandidate(rel, AssetTypes::FromPath(rel)));

        // Two assets claiming the same UUID (a file copied together with its .zmeta): the one with
        // the older .zmeta keeps it, the copy gets a new one.
        std::unordered_map<AssetHandle, size_t> owner;
        auto metaTime = [&](const Candidate& c) {
            std::error_code tec;
            return fs::last_write_time(AssetMetaFile::GetMetaPath(ToFileSystemPath(c.RelativePath)), tec);
        };
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (!candidates[i].Meta)
                continue;
            auto [it, inserted] = owner.emplace(candidates[i].Meta->Handle, i);
            if (!inserted && metaTime(candidates[i]) < metaTime(candidates[it->second]))
                it->second = i;
        }
        for (size_t i = 0; i < candidates.size(); i++)
        {
            const Candidate& c = candidates[i];
            bool duplicate = c.Meta && owner[c.Meta->Handle] != i;
            Insert(c, duplicate, &result);
        }

        std::set<std::string> registered(assets.begin(), assets.end());
        for (const std::string& assetOfMeta : metaFiles)
            if (!registered.contains(assetOfMeta))
                result.OrphanMeta++;
        result.Assets = m_Assets.size();
        m_LastScan = std::move(result);
        return m_LastScan;
    }

    // ------------------------------------------------------------------ changes
    size_t AssetRegistry::OnFileAdded(const fs::path& path)
    {
        if (!m_Scanned)
            return 0;
        std::string rel = ToRelativePath(path);
        if (rel.empty() || AssetMetaFile::IsMetaFile(rel))
            return 0;
        std::error_code ec;
        fs::path abs = ToFileSystemPath(rel);
        std::vector<std::string> files;
        if (fs::is_directory(abs, ec))
            CollectAssets(abs, files, nullptr);
        else if (fs::is_regular_file(abs, ec) && AssetTypes::IsRegistered(AssetTypes::FromPath(rel)))
            files.push_back(rel);
        std::sort(files.begin(), files.end());

        size_t added = 0;
        for (const std::string& file : files)
        {
            Candidate c = ReadCandidate(file, AssetTypes::FromPath(file));
            if (const AssetMetadata* known = Find(fs::path(file)))
            {
                // Already registered (the content browser moved it, or a repeated event): only a
                // .zmeta that is gone is written again.
                if (!c.Meta && c.MetaError.empty() && m_WriteMetaFiles)
                    SaveMeta(m_Assets.at(known->Handle));
                else if (c.Meta && c.Meta->Handle != known->Handle)
                    OnMetaFileChanged(AssetMetaFile::GetMetaPath(ToFileSystemPath(file)));
                continue;
            }
            bool duplicate = false;
            if (c.Meta)
            {
                if (const AssetMetadata* other = Find(c.Meta->Handle))
                {
                    if (ExistsOnDisk(other->RelativePath))
                        duplicate = true;                 // a copy: a new identity
                    else
                        Erase(other->Handle);             // moved here (its removal not seen yet)
                }
            }
            ScanResult problems;
            Insert(c, duplicate, &problems);
            for (std::string& problem : problems.Problems)
                m_Problems.push_back(std::move(problem));
            added++;
        }
        return added;
    }

    size_t AssetRegistry::OnFileRemoved(const fs::path& path)
    {
        std::string rel = ToRelativePath(path);
        if (rel.empty())
            return 0;
        std::vector<AssetHandle> gone;
        std::string prefix = rel + "/";
        for (const auto& [p, handle] : m_Paths)
            if (p == rel || p.starts_with(prefix))
                gone.push_back(handle);
        for (AssetHandle handle : gone)
            Erase(handle);
        return gone.size();
    }

    size_t AssetRegistry::OnFileMoved(const fs::path& from, const fs::path& to)
    {
        std::string relFrom = ToRelativePath(from), relTo = ToRelativePath(to);
        if (relFrom.empty())
            return 0;
        if (relTo.empty())
            return OnFileRemoved(from); // out of the asset folder
        if (relFrom == relTo)
            return 0;

        std::vector<std::pair<AssetHandle, std::string>> moves;
        std::string prefix = relFrom + "/";
        for (const auto& [p, handle] : m_Paths)
        {
            if (p == relFrom)
                moves.emplace_back(handle, relTo);
            else if (p.starts_with(prefix))
                moves.emplace_back(handle, relTo + p.substr(relFrom.size()));
        }
        for (const auto& [handle, newPath] : moves)
        {
            AssetMetadata meta = m_Assets.at(handle);
            Erase(handle);
            if (auto replaced = m_Paths.find(newPath); replaced != m_Paths.end())
                Erase(replaced->second); // a file that was overwritten by the move
            meta.RelativePath = newPath;
            AssetType type = AssetTypes::FromPath(newPath);
            if (!AssetTypes::IsRegistered(type))
                continue; // renamed into something that is not an asset
            if (type != meta.Type)
            {
                meta.Type = type;
                SaveMeta(meta);
            }
            m_Paths[newPath] = handle;
            m_Assets.emplace(handle, std::move(meta));
        }
        return moves.size();
    }

    const AssetMetadata* AssetRegistry::OnMetaFileChanged(const fs::path& metaPath)
    {
        if (!m_Scanned || !AssetMetaFile::IsMetaFile(metaPath))
            return nullptr;
        std::string rel = ToRelativePath(AssetMetaFile::GetAssetPath(metaPath));
        if (rel.empty())
            return nullptr;
        const AssetMetadata* known = Find(fs::path(rel));
        if (!known)
        {
            OnFileAdded(rel); // the asset may have been waiting for its .zmeta
            return nullptr;
        }
        AssetMetadata& current = m_Assets.at(known->Handle);
        std::string error;
        std::optional<AssetMetadata> onDisk = AssetMetaFile::Read(ToFileSystemPath(rel), &error);
        if (!onDisk)
        {
            if (error.empty())
            {
                current.HasMetaFile = false;
                if (ExistsOnDisk(rel))
                    SaveMeta(current); // deleted by hand: the asset keeps its identity
            }
            return nullptr; // broken: kept as it was until the file is fixed
        }

        if (onDisk->Handle != current.Handle)
        {
            const AssetMetadata* other = Find(onDisk->Handle);
            if (other && other != &current && ExistsOnDisk(other->RelativePath))
            {
                SaveMeta(current); // another asset's UUID: this one keeps its own
                return nullptr;
            }
            // The UUID was changed on disk (a .zmeta put back from version control): adopt it.
            AssetMetadata moved = current;
            Erase(moved.Handle);
            if (other)
                Erase(other->Handle);
            moved.Handle = onDisk->Handle;
            moved.HasMetaFile = true;
            m_Paths[moved.RelativePath] = moved.Handle;
            auto [it, inserted] = m_Assets.insert_or_assign(moved.Handle, std::move(moved));
            if (it->second.Import == onDisk->Import)
                return nullptr;
            it->second.Import = onDisk->Import;
            return &it->second;
        }

        current.HasMetaFile = true;
        if (current.Import == onDisk->Import)
            return nullptr;
        current.Import = onDisk->Import;
        return &current;
    }

    bool AssetRegistry::SetImportSettings(AssetHandle handle, const AssetImportSettings& settings)
    {
        auto it = m_Assets.find(handle);
        if (it == m_Assets.end())
            return false;
        it->second.Import = settings;
        SaveMeta(it->second);
        return true;
    }

}
