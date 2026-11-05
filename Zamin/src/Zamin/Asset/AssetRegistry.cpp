#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Utils/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <unordered_set>

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

    bool AssetRegistry::ReadStamp(const std::string& relativePath, uintmax_t& size, int64_t& time) const
    {
        if (relativePath.empty())
            return false;
        std::error_code ec;
        fs::path file = ToFileSystemPath(relativePath);
        uintmax_t bytes = fs::file_size(file, ec);
        if (ec)
            return false;
        fs::file_time_type modified = fs::last_write_time(file, ec);
        if (ec)
            return false;
        size = bytes;
        time = (int64_t)modified.time_since_epoch().count();
        return true;
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
            meta.HasMetaFile = !c.Recovered;
            if (c.Recovered)
                write = true; // its .zmeta stayed where the file was
            else if (c.Meta->Type != c.Type)
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
        ReadStamp(meta.RelativePath, meta.FileSize, meta.FileTime);

        // Whatever was registered under this path before is replaced.
        if (auto old = m_Paths.find(meta.RelativePath); old != m_Paths.end())
            Erase(old->second);
        AssetHandle key = meta.Handle;
        m_Paths[meta.RelativePath] = key;
        auto [it, inserted] = m_Assets.insert_or_assign(key, std::move(meta));
        m_Revision++;
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
        m_Revision++;
    }

    void AssetRegistry::RemoveLeftBehindMeta(const std::string& oldPath, AssetHandle handle)
    {
        if (!m_WriteMetaFiles || oldPath.empty() || ExistsOnDisk(oldPath))
            return;
        fs::path asset = ToFileSystemPath(oldPath);
        std::error_code ec;
        if (!fs::is_regular_file(AssetMetaFile::GetMetaPath(asset), ec))
            return;
        std::optional<AssetMetadata> left = AssetMetaFile::Read(asset);
        if (left && left->Handle == handle)
            fs::remove(AssetMetaFile::GetMetaPath(asset), ec);
    }

    bool AssetRegistry::AdoptMovedIdentity(Candidate& c)
    {
        uintmax_t size = 0;
        int64_t time = 0;
        if (c.Meta || !c.MetaError.empty() || !ReadStamp(c.RelativePath, size, time) || time == 0)
            return false;

        // The removal of the old file was seen first ...
        AssetHandle handle;
        std::string oldPath;
        AssetImportSettings import;
        int matches = 0;
        for (const auto& [h, removed] : m_Removed)
            if (removed.Type == c.Type && removed.Size == size && removed.Time == time && removed.Path != c.RelativePath &&
                !ExistsOnDisk(removed.Path))
            {
                matches++;
                handle = h;
                oldPath = removed.Path;
                import = removed.Import;
            }
        bool stillRegistered = false;
        if (matches == 0)
        {
            // ... or not yet: a registered asset whose file is gone.
            for (const auto& [h, meta] : m_Assets)
                if (meta.Type == c.Type && meta.FileSize == size && meta.FileTime == time && meta.RelativePath != c.RelativePath &&
                    !ExistsOnDisk(meta.RelativePath))
                {
                    matches++;
                    handle = h;
                    oldPath = meta.RelativePath;
                    import = meta.Import;
                }
            stillRegistered = true;
        }
        if (matches != 1)
            return false; // nothing, or no way to tell which one: a new identity

        if (stillRegistered)
            Erase(handle);
        else
            m_Removed.erase(handle);
        AssetMetadata meta;
        meta.Handle = handle;
        meta.Type = c.Type;
        meta.Import = std::move(import);
        c.Meta = std::move(meta);
        c.Recovered = true;
        m_DetectedMoves.emplace_back(oldPath, c.RelativePath);
        RemoveLeftBehindMeta(oldPath, handle);
        return true;
    }

    std::vector<std::string> AssetRegistry::TakeProblems()
    {
        std::vector<std::string> problems = std::move(m_Problems);
        m_Problems.clear();
        return problems;
    }

    std::vector<AssetRegistry::PathMove> AssetRegistry::TakeDetectedMoves()
    {
        std::vector<PathMove> moves = std::move(m_DetectedMoves);
        m_DetectedMoves.clear();
        return moves;
    }

    void AssetRegistry::Clear()
    {
        m_Assets.clear();
        m_Paths.clear();
        m_Problems.clear();
        m_Removed.clear();
        m_DetectedMoves.clear();
        m_LastScan = {};
        m_Scanned = false;
        m_Revision++;
    }

    const AssetRegistry::ScanResult& AssetRegistry::Scan(const fs::path& assetDirectory, const Snapshot* previous)
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
        // What moved since the previous registry (`previous`) was made.
        std::vector<std::pair<std::string, AssetHandle>> leftBehind; // .zmeta files the moved files did not take along
        if (previous && !previous->empty())
        {
            std::unordered_set<std::string> present(assets.begin(), assets.end());
            std::unordered_map<AssetHandle, const SnapshotEntry*> before;
            for (const SnapshotEntry& e : *previous)
                if (e.Handle)
                    before.emplace(e.Handle, &e);
            // Moved with the .zmeta: the UUID is somewhere else now.
            for (size_t i = 0; i < candidates.size(); i++)
            {
                const Candidate& c = candidates[i];
                if (!c.Meta || owner[c.Meta->Handle] != i)
                    continue;
                auto it = before.find(c.Meta->Handle);
                if (it != before.end() && it->second->Path != c.RelativePath && !present.contains(it->second->Path))
                    m_DetectedMoves.emplace_back(it->second->Path, c.RelativePath);
            }
            // Moved without it: a file with no .zmeta of the same type, size and time as an asset
            // that is gone (and whose UUID no file claims). Only one-to-one matches count.
            auto stampKey = [](AssetType type, uintmax_t size, int64_t time) { return std::format("{}|{}|{}", (int)type, size, time); };
            std::unordered_map<std::string, std::vector<const SnapshotEntry*>> gone;
            for (const SnapshotEntry& e : *previous)
                if (e.Handle && e.Time != 0 && !present.contains(e.Path) && !owner.contains(e.Handle))
                    gone[stampKey(e.Type, e.Size, e.Time)].push_back(&e);
            std::map<std::string, std::vector<size_t>> unclaimed;
            if (!gone.empty())
                for (size_t i = 0; i < candidates.size(); i++)
                {
                    const Candidate& c = candidates[i];
                    uintmax_t size = 0;
                    int64_t time = 0;
                    if (c.Meta || !c.MetaError.empty() || !ReadStamp(c.RelativePath, size, time))
                        continue;
                    std::string key = stampKey(c.Type, size, time);
                    if (gone.contains(key))
                        unclaimed[key].push_back(i);
                }
            for (const auto& [key, indices] : unclaimed)
            {
                const auto& entries = gone[key];
                if (indices.size() != 1 || entries.size() != 1)
                    continue;
                Candidate& c = candidates[indices.front()];
                const SnapshotEntry& e = *entries.front();
                AssetMetadata meta;
                meta.Handle = e.Handle;
                meta.Type = c.Type;
                meta.Import = e.Import;
                c.Meta = std::move(meta);
                c.Recovered = true;
                owner[e.Handle] = indices.front();
                m_DetectedMoves.emplace_back(e.Path, c.RelativePath);
                leftBehind.emplace_back(e.Path, e.Handle);
                result.RecoveredMeta++;
            }
            result.MovedOutside = m_DetectedMoves.size();
        }

        for (size_t i = 0; i < candidates.size(); i++)
        {
            const Candidate& c = candidates[i];
            bool duplicate = c.Meta && owner[c.Meta->Handle] != i;
            Insert(c, duplicate, &result);
        }
        std::set<std::string> removedMeta;
        for (const auto& [oldPath, handle] : leftBehind)
        {
            RemoveLeftBehindMeta(oldPath, handle);
            std::error_code mec;
            if (!fs::exists(AssetMetaFile::GetMetaPath(ToFileSystemPath(oldPath)), mec))
                removedMeta.insert(oldPath);
        }

        std::set<std::string> registered(assets.begin(), assets.end());
        for (const std::string& assetOfMeta : metaFiles)
            if (!registered.contains(assetOfMeta) && !removedMeta.contains(assetOfMeta))
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
                AssetMetadata& current = m_Assets.at(known->Handle);
                ReadStamp(file, current.FileSize, current.FileTime);
                if (!c.Meta && c.MetaError.empty() && m_WriteMetaFiles)
                    SaveMeta(current);
                else if (c.Meta && c.Meta->Handle != known->Handle)
                    OnMetaFileChanged(AssetMetaFile::GetMetaPath(ToFileSystemPath(file)));
                continue;
            }
            bool duplicate = false;
            if (!c.Meta)
                AdoptMovedIdentity(c); // moved here without its .zmeta (renamed in a shell, by a script...)
            else
            {
                if (const AssetMetadata* other = Find(c.Meta->Handle))
                {
                    if (ExistsOnDisk(other->RelativePath))
                        duplicate = true;                 // a copy: a new identity
                    else
                    {
                        // Moved here (its removal not seen yet).
                        if (other->HasMetaFile)
                            m_DetectedMoves.emplace_back(other->RelativePath, file);
                        Erase(other->Handle);
                    }
                }
                else if (auto removed = m_Removed.find(c.Meta->Handle); removed != m_Removed.end())
                {
                    // Moved here: the removal came first.
                    if (removed->second.Path != file && removed->second.HasMetaFile)
                        m_DetectedMoves.emplace_back(removed->second.Path, file);
                    m_Removed.erase(removed);
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
        {
            const AssetMetadata& meta = m_Assets.at(handle);
            RemovedAsset& removed = m_Removed[handle];
            removed.Path = meta.RelativePath;
            removed.Type = meta.Type;
            removed.Size = meta.FileSize;
            removed.Time = meta.FileTime;
            removed.HasMetaFile = meta.HasMetaFile;
            removed.Import = meta.Import;
            Erase(handle);
        }
        return gone.size();
    }

    bool AssetRegistry::OnFileModified(const fs::path& path)
    {
        const AssetMetadata* known = Find(path);
        if (!known)
            return false;
        AssetMetadata& meta = m_Assets.at(known->Handle);
        return ReadStamp(meta.RelativePath, meta.FileSize, meta.FileTime);
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
            ReadStamp(newPath, meta.FileSize, meta.FileTime);
            m_Paths[newPath] = handle;
            m_Assets.emplace(handle, std::move(meta));
            m_Revision++;
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
            m_Revision++;
            if (it->second.Import == onDisk->Import)
                return nullptr;
            it->second.Import = onDisk->Import;
            return &it->second;
        }

        if (!current.HasMetaFile)
            m_Revision++;
        current.HasMetaFile = true;
        if (current.Import == onDisk->Import)
            return nullptr;
        current.Import = onDisk->Import;
        m_Revision++;
        return &current;
    }

    bool AssetRegistry::SetImportSettings(AssetHandle handle, const AssetImportSettings& settings)
    {
        auto it = m_Assets.find(handle);
        if (it == m_Assets.end())
            return false;
        it->second.Import = settings;
        SaveMeta(it->second);
        m_Revision++;
        return true;
    }

    // ------------------------------------------------------------------ snapshots
    bool AssetRegistry::Load(const fs::path& assetDirectory, const Snapshot& snapshot, bool verify)
    {
        Clear();
        std::error_code ec;
        m_AssetDirectory = NormalizeDirectory(assetDirectory);
        m_CanonicalDirectory = fs::weakly_canonical(m_AssetDirectory, ec);
        if (ec)
            m_CanonicalDirectory = m_AssetDirectory;
        if (verify)
            for (const SnapshotEntry& e : snapshot)
                if (!ExistsOnDisk(e.Path))
                    return false;

        for (const SnapshotEntry& e : snapshot)
        {
            std::string rel = NormalizeRelative(e.Path);
            if (!e.Handle || rel.empty() || m_Assets.contains(e.Handle) || m_Paths.contains(rel))
                continue;
            AssetType type = AssetTypes::FromPath(rel);
            AssetMetadata meta;
            meta.Handle = e.Handle;
            meta.Type = AssetTypes::IsRegistered(type) ? type : e.Type;
            meta.RelativePath = rel;
            meta.Import = e.Import;
            meta.HasMetaFile = e.HasMetaFile;
            meta.FileSize = e.Size;
            meta.FileTime = e.Time;
            m_Paths[rel] = e.Handle;
            m_Assets.emplace(e.Handle, std::move(meta));
        }
        m_Scanned = true;
        m_LastScan = {};
        m_LastScan.Assets = m_Assets.size();
        m_Revision++;
        return true;
    }

    AssetRegistry::Snapshot AssetRegistry::GetSnapshot() const
    {
        Snapshot snapshot;
        snapshot.reserve(m_Assets.size());
        for (const AssetMetadata* meta : GetAssets())
        {
            SnapshotEntry e;
            e.Handle = meta->Handle;
            e.Type = meta->Type;
            e.Path = meta->RelativePath;
            e.Size = meta->FileSize;
            e.Time = meta->FileTime;
            ReadStamp(meta->RelativePath, e.Size, e.Time); // as the file is now
            e.HasMetaFile = meta->HasMetaFile;
            e.Import = meta->Import;
            snapshot.push_back(std::move(e));
        }
        return snapshot;
    }

    std::string AssetRegistry::Serialize() const { return SerializeSnapshot(GetSnapshot()); }

    std::string AssetRegistry::SerializeSnapshot(const Snapshot& snapshot)
    {
        using ordered_json = nlohmann::ordered_json;
        ordered_json root;
        root["Version"] = SnapshotVersion;
        ordered_json assets = ordered_json::array();
        for (const SnapshotEntry& e : snapshot)
        {
            if (!e.Handle || e.Path.empty())
                continue;
            ordered_json a;
            a["UUID"] = e.Handle.ToString();
            a["Type"] = AssetTypes::GetKey(e.Type);
            a["Path"] = e.Path;
            a["Size"] = e.Size;
            a["Time"] = e.Time;
            a["Meta"] = e.HasMetaFile;
            if (!e.Import.IsEmpty())
            {
                ordered_json import = ordered_json::parse(e.Import.ToJson(), nullptr, false);
                if (import.is_object())
                    a["Import"] = std::move(import);
            }
            assets.push_back(std::move(a));
        }
        root["Assets"] = std::move(assets);
        return root.dump(2) + "\n";
    }

    bool AssetRegistry::Deserialize(std::string_view text, Snapshot& out, std::string* error, size_t* skipped)
    {
        using ordered_json = nlohmann::ordered_json;
        out.clear();
        if (skipped)
            *skipped = 0;
        auto fail = [&](const char* reason) {
            if (error)
                *error = reason;
            return false;
        };
        ordered_json root = ordered_json::parse(text.begin(), text.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return fail("JSON formati buzilgan");
        auto version = root.find("Version");
        if (version == root.end() || !version->is_number_integer())
            return fail("asset registry fayli emas (Version yo'q)");
        if (version->get<int>() > SnapshotVersion)
            return fail("editorning yangiroq versiyasida yozilgan");
        auto assets = root.find("Assets");
        if (assets == root.end() || !assets->is_array())
            return fail("Assets ro'yxati yo'q");

        std::unordered_set<AssetHandle> handles;
        std::unordered_set<std::string> paths;
        for (const ordered_json& a : *assets)
        {
            auto skip = [&]() {
                if (skipped)
                    (*skipped)++;
            };
            if (!a.is_object())
            {
                skip();
                continue;
            }
            SnapshotEntry e;
            if (auto uuid = a.find("UUID"); uuid != a.end() && uuid->is_string())
                e.Handle = AssetHandle::FromString(uuid->get<std::string>()).value_or(AssetHandle());
            if (auto path = a.find("Path"); path != a.end() && path->is_string())
                e.Path = NormalizeRelative(fs::path(path->get<std::string>()));
            if (!e.Handle || e.Path.empty() || handles.contains(e.Handle) || paths.contains(e.Path))
            {
                skip();
                continue;
            }
            e.Type = AssetTypes::FromPath(e.Path);
            if (auto type = a.find("Type"); type != a.end() && type->is_string())
                if (auto known = AssetTypes::FromKey(type->get<std::string>()); known && !AssetTypes::IsRegistered(e.Type))
                    e.Type = *known;
            if (auto size = a.find("Size"); size != a.end() && size->is_number_unsigned())
                e.Size = size->get<uintmax_t>();
            if (auto time = a.find("Time"); time != a.end() && time->is_number_integer())
                e.Time = time->get<int64_t>();
            if (auto meta = a.find("Meta"); meta != a.end() && meta->is_boolean())
                e.HasMetaFile = meta->get<bool>();
            if (auto import = a.find("Import"); import != a.end() && import->is_object())
                AssetImportSettings::FromJson(import->dump(), e.Import);
            handles.insert(e.Handle);
            paths.insert(e.Path);
            out.push_back(std::move(e));
        }
        if (error)
            error->clear();
        return true;
    }

    bool AssetRegistry::SaveSnapshot(const fs::path& file) const
    {
        std::string text = Serialize();
        std::error_code ec;
        if (fs::is_regular_file(file, ec) && FileSystem::ReadTextFile(file) == text)
            return true;
        return FileSystem::WriteTextFile(file, text);
    }

    std::optional<AssetRegistry::Snapshot> AssetRegistry::LoadSnapshot(const fs::path& file, std::string* error)
    {
        if (error)
            error->clear();
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
            return std::nullopt;
        Snapshot snapshot;
        if (!Deserialize(FileSystem::ReadTextFile(file), snapshot, error))
            return std::nullopt;
        return snapshot;
    }

}
