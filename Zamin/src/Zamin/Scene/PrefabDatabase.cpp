#include "Zamin/Scene/PrefabDatabase.h"

#include "Zamin/Core/Log.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Entity.h"
#include "Zamin/Scene/Prefab.h"
#include "Zamin/Scene/PrefabCache.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Utils/FileSystem.h"

#include <algorithm>
#include <format>
#include <functional>

namespace fs = std::filesystem;

namespace Zamin {

    struct PrefabDatabase::Entry
    {
        Template T;
        bool Built = false;
        bool Exists = false;
        fs::file_time_type Time{};
        uintmax_t Size = 0;
        std::chrono::steady_clock::time_point Checked{};
    };

    uint64_t PrefabDatabase::Template::ParentOf(uint64_t id) const
    {
        if (!Entities)
            return 0;
        Entity e = Entities->GetEntityByUUID(UUID(id));
        return e ? (uint64_t)e.GetComponent<RelationshipComponent>().Parent : 0;
    }

    std::vector<uint64_t> PrefabDatabase::Template::ChildrenOf(uint64_t id) const
    {
        std::vector<uint64_t> out;
        if (!Entities)
            return out;
        if (Entity e = Entities->GetEntityByUUID(UUID(id)))
            for (UUID child : e.GetComponent<RelationshipComponent>().Children)
                out.push_back((uint64_t)child);
        return out;
    }

    PrefabDatabase::PrefabDatabase() = default;
    PrefabDatabase::PrefabDatabase(fs::path assetRoot) : m_AssetRoot(std::move(assetRoot)) {}
    PrefabDatabase::~PrefabDatabase() = default;

    void PrefabDatabase::SetAssetRoot(fs::path root)
    {
        m_AssetRoot = std::move(root);
        Clear();
    }

    fs::path PrefabDatabase::GetAssetRoot() const
    {
        if (!m_AssetRoot.empty())
            return m_AssetRoot;
        return Project::GetActive() ? Project::GetAssetDirectory() : fs::path();
    }

    fs::path PrefabDatabase::ToFileSystemPath(std::string_view relPath) const
    {
        fs::path p{ std::string(relPath) };
        if (p.is_absolute())
            return p;
        return GetAssetRoot() / p;
    }

    std::string PrefabDatabase::NormalizePath(std::string_view relPath)
    {
        if (relPath.empty())
            return {};
        // Already normal (what this database hands out, every instance's path): nothing to parse.
        // Only '/' separators, no "//", no "." / ".." parts, no '/' at the end.
        bool simple = relPath.front() != '.' && relPath.back() != '/';
        for (size_t i = 0; simple && i < relPath.size(); i++)
        {
            const char c = relPath[i];
            if (c == '\\' || (c == '/' && i + 1 < relPath.size() && (relPath[i + 1] == '/' || relPath[i + 1] == '.')))
                simple = false;
        }
        if (simple)
            return std::string(relPath);
        std::string path = FileSystem::NormalizePath(fs::path(std::string(relPath)));
        return path == "." ? std::string() : path;
    }

    uint64_t PrefabDatabase::HashText(std::string_view text)
    {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : text)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }

    const PrefabDatabase::Template* PrefabDatabase::Get(std::string_view relPath)
    {
        const std::string key = NormalizePath(relPath);
        if (key.empty())
            return nullptr;
        Entry* entry = Load(key);
        return entry ? &entry->T : nullptr;
    }

    PrefabDatabase::Entry* PrefabDatabase::Load(const std::string& key)
    {
        std::unique_ptr<Entry>& slot = m_Entries[key];
        if (!slot)
            slot = std::make_unique<Entry>();
        Entry& entry = *slot;
        if (m_Building.contains(key))
            return &entry; // a cycle: what there is so far (the caller reports it)
        const auto now = std::chrono::steady_clock::now();
        if (entry.Built && std::chrono::duration<double>(now - entry.Checked).count() < m_RecheckSeconds)
            return &entry;
        std::error_code ec;
        const fs::path file = ToFileSystemPath(key);
        const bool exists = fs::is_regular_file(file, ec);
        const fs::file_time_type time = exists ? fs::last_write_time(file, ec) : fs::file_time_type{};
        const uintmax_t size = exists ? fs::file_size(file, ec) : 0;
        entry.Checked = now;
        if (entry.Built && exists == entry.Exists && time == entry.Time && size == entry.Size)
            return &entry;
        entry.Exists = exists;
        entry.Time = time;
        entry.Size = size;
        BuildTemplate(entry, key);
        entry.Built = true;
        m_Hashes.clear();
        m_ScanValid = false;
        return &entry;
    }

    void PrefabDatabase::BuildTemplate(Entry& entry, const std::string& path)
    {
        Template t;
        t.Path = path;
        if (!entry.Exists)
        {
            t.Issues.push_back(std::format("Prefab fayli topilmadi: {}", path));
            entry.T = std::move(t);
            return;
        }
        t.Text = FileSystem::ReadTextFile(ToFileSystemPath(path));
        t.Entities = CreateRef<Scene>();
        std::string error;
        std::unordered_map<uint64_t, UUID> ids;
        SceneSerializer::EntitySetReadOptions options;
        options.GenerateNewUUIDs = false;
        options.KeepPrefabLinks = true;
        options.IDsOut = &ids;
        options.Error = &error;
        SceneLoadReport report;
        SceneMigrationResult migration;
        options.Report = &report;
        options.Migration = &migration;
        std::vector<Entity> created = SceneSerializer::DeserializeEntities(*t.Entities, t.Text, options);
        t.FileVersion = migration.FileVersion;
        t.Newer = migration.IsNewer();
        t.LoadErrors = report.Count(SceneLoadIssue::Severity::Error) + report.Count(SceneLoadIssue::Severity::Fatal);
        if (created.empty())
        {
            t.Issues.push_back(error.empty() ? std::string("prefabda obyekt yo'q") : error);
            entry.T = std::move(t);
            return;
        }
        t.Readable = true;
        if (!error.empty())
            t.Issues.push_back(error);
        if (t.Newer)
            t.Issues.push_back(std::format("yangiroq formatda (v{}, bu dvigatel v{} gacha biladi): o'zgartirib yozilmaydi",
                                           t.FileVersion, SceneMigration::CurrentVersion));

        // Linkable: one root, every entity with an ID of its own.
        bool idsOk = ids.size() == created.size();
        for (const auto& [file, id] : ids)
            idsOk = idsOk && file == (uint64_t)id;
        std::vector<Entity> roots;
        for (Entity e : created)
            if (!(uint64_t)e.GetComponent<RelationshipComponent>().Parent)
                roots.push_back(e);
        t.Linkable = idsOk && roots.size() == 1;
        if (!idsOk)
            t.Issues.push_back("obyekt ID'lari takrorlanadi yoki yo'q");
        if (roots.size() > 1)
            t.Issues.push_back(std::format("prefabda {} ta ildiz bor (bitta bo'lishi kerak)", roots.size()));
        t.Root = roots.empty() ? 0 : (uint64_t)roots.front().GetUUID();

        // Depth-first order.
        std::function<void(Entity)> walk = [&](Entity e) {
            t.Order.push_back((uint64_t)e.GetUUID());
            for (UUID child : e.GetComponent<RelationshipComponent>().Children)
                if (Entity c = t.Entities->GetEntityByUUID(child))
                    walk(c);
        };
        for (Entity root : roots)
            walk(root);

        // Prefabs nested in it (and its base, when the root is an instance itself).
        std::vector<UUID> owners;
        for (uint64_t id : t.Order)
        {
            Entity e = t.Entities->GetEntityByUUID(UUID(id));
            const auto* meta = e.TryGetComponent<EditorMetadataComponent>();
            if (!meta || meta->Prefab.empty())
                continue;
            const std::string nested = NormalizePath(meta->Prefab);
            if (id == t.Root)
            {
                t.IsVariant = true;
                t.Base = nested;
            }
            if (std::find(t.Deps.begin(), t.Deps.end(), nested) == t.Deps.end())
                t.Deps.push_back(nested);
            owners.push_back(UUID(id));
        }

        // Brought up to date with them in memory (a file left stale by a change of a nested prefab).
        if (!owners.empty())
        {
            m_Building.insert(path);
            m_Depth++;
            for (UUID ownerID : owners)
            {
                Entity owner = t.Entities->GetEntityByUUID(ownerID);
                if (!owner)
                    continue;
                const std::string nested = NormalizePath(owner.GetComponent<EditorMetadataComponent>().Prefab);
                if (m_Building.contains(nested))
                {
                    t.Issues.push_back(std::format("Prefab sikli: {} -> {}", path, nested));
                    ZM_CORE_WARN("Prefab sikli: {} o'z ichida {} ni ishlatadi (u esa {} ni) - ichma-ich nusxa yangilanmadi", path, nested, path);
                    continue;
                }
                if (m_Depth > MaxDepth)
                {
                    t.Issues.push_back(std::format("Prefablar juda chuqur ichma-ich ({} dan ortiq): {}", MaxDepth, nested));
                    continue;
                }
                PrefabUtility::SyncInstance(owner, *this);
            }
            m_Depth--;
            m_Building.erase(path);
        }

        // Which nested prefab each entity belongs to (the nearest instance root above it).
        for (uint64_t id : t.Order)
        {
            for (Entity e = t.Entities->GetEntityByUUID(UUID(id)); e;)
            {
                const auto* meta = e.TryGetComponent<EditorMetadataComponent>();
                if (meta && !meta->Prefab.empty())
                {
                    Entity self = t.Entities->GetEntityByUUID(UUID(id));
                    const auto* link = self.TryGetComponent<PrefabLinkComponent>();
                    if (link && link->PrefabEntity)
                        t.NestedOwner[id] = { NormalizePath(meta->Prefab), link->PrefabEntity, (uint64_t)e.GetUUID() };
                    break;
                }
                const UUID parent = e.GetComponent<RelationshipComponent>().Parent;
                e = (uint64_t)parent ? t.Entities->GetEntityByUUID(parent) : Entity();
            }
        }

        // The entities as the prefab makes them now (what instances are made from).
        if (owners.empty())
            t.Resolved = t.Text;
        else
        {
            std::vector<Entity> ordered;
            ordered.reserve(t.Order.size());
            for (uint64_t id : t.Order)
                ordered.push_back(t.Entities->GetEntityByUUID(UUID(id)));
            t.Resolved = SceneSerializer::SerializeEntities(*t.Entities, ordered, false);
        }
        entry.T = std::move(t);
    }

    uint64_t PrefabDatabase::ResolvedHash(std::string_view relPath)
    {
        std::unordered_set<std::string> visiting;
        return ResolvedHashImpl(NormalizePath(relPath), visiting);
    }

    uint64_t PrefabDatabase::ResolvedHashImpl(const std::string& path, std::unordered_set<std::string>& visiting)
    {
        if (path.empty())
            return 0;
        if (auto it = m_Hashes.find(path); it != m_Hashes.end())
            return it->second;
        if (!visiting.insert(path).second)
            return 0; // a cycle (reported where the template is made)
        const Template* t = Get(path);
        uint64_t hash = 0;
        if (t && t->Readable)
        {
            hash = HashText(t->Text);
            std::vector<std::string> deps = t->Deps;
            std::sort(deps.begin(), deps.end());
            for (const std::string& dep : deps)
            {
                const uint64_t h = ResolvedHashImpl(dep, visiting);
                for (int i = 0; i < 8; i++)
                {
                    hash ^= (h >> (i * 8)) & 0xFF;
                    hash *= 1099511628211ull;
                }
            }
            if (!hash)
                hash = 1;
        }
        visiting.erase(path);
        m_Hashes[path] = hash;
        return hash;
    }

    void PrefabDatabase::ScanDependencies()
    {
        if (m_ScanValid)
            return;
        m_Uses.clear();
        const fs::path root = GetAssetRoot();
        std::error_code ec;
        std::vector<std::string> files;
        if (!root.empty() && fs::is_directory(root, ec))
            for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec)
                    break;
                const std::string name = it->path().filename().string();
                if (!name.empty() && name[0] == '.')
                {
                    if (it->is_directory(ec))
                        it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file(ec) && it->path().extension() == ".zprefab")
                    files.push_back(NormalizePath(FileSystem::RelativePath(it->path(), root)));
            }
        std::sort(files.begin(), files.end());
        for (const std::string& file : files)
            if (const Template* t = Get(file))
                m_Uses[file] = t->Deps;
        m_ScanValid = true;
    }

    std::vector<std::string> PrefabDatabase::GetDependents(std::string_view relPath)
    {
        const std::string key = NormalizePath(relPath);
        ScanDependencies();
        // Everything that uses it, directly or through others.
        std::unordered_set<std::string> found;
        std::vector<std::string> queue{ key };
        while (!queue.empty())
        {
            const std::string current = queue.back();
            queue.pop_back();
            for (const auto& [user, deps] : m_Uses)
                if (std::find(deps.begin(), deps.end(), current) != deps.end() && user != key && found.insert(user).second)
                    queue.push_back(user);
        }
        // Each after the ones it is made of (the longest chain from `key` first decides).
        std::unordered_map<std::string, int> depth;
        for (const std::string& d : found)
            depth[d] = 1;
        for (size_t pass = 0; pass < found.size(); pass++)
            for (const std::string& d : found)
                for (const std::string& dep : m_Uses[d])
                    if (auto it = depth.find(dep); it != depth.end())
                        depth[d] = std::max(depth[d], std::min(it->second + 1, MaxDepth + 1));
        std::vector<std::string> out(found.begin(), found.end());
        std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
            return depth[a] != depth[b] ? depth[a] < depth[b] : a < b;
        });
        return out;
    }

    bool PrefabDatabase::DependsOn(std::string_view a, std::string_view b)
    {
        const std::string from = NormalizePath(a), to = NormalizePath(b);
        if (from.empty() || to.empty())
            return false;
        std::unordered_set<std::string> seen;
        std::vector<std::string> stack{ from };
        while (!stack.empty())
        {
            const std::string current = stack.back();
            stack.pop_back();
            if (current == to)
                return true;
            if (!seen.insert(current).second || seen.size() > 4096)
                continue;
            if (const Template* t = Get(current))
                for (const std::string& dep : t->Deps)
                    stack.push_back(dep);
        }
        return false;
    }

    bool PrefabDatabase::Write(std::string_view relPath, const std::string& text, std::vector<FileEdit>* undo)
    {
        const std::string key = NormalizePath(relPath);
        if (key.empty())
            return false;
        const fs::path file = ToFileSystemPath(key);
        std::optional<std::string> before;
        std::error_code ec;
        if (fs::is_regular_file(file, ec))
            before = FileSystem::ReadTextFile(file);
        if (before && *before == text)
            return true;
        if (before && SceneMigration::ReadVersion(*before) > SceneMigration::CurrentVersion)
        {
            ZM_CORE_WARN("Prefab fayli yangiroq formatda (v{}, bu dvigatel v{} gacha biladi): ustiga yozilmadi - {}",
                         SceneMigration::ReadVersion(*before), SceneMigration::CurrentVersion, key);
            return false;
        }
        if (!FileSystem::WriteTextFileAtomic(file, text))
            return false;
        m_OwnWrites[FileSystem::NormalizePath(file)] = HashText(text);
        PrefabCache::Invalidate(file); // a game started from the editor spawns the new text
        if (undo)
            undo->push_back({ key, std::move(before), text });
        Invalidate(key);
        return true;
    }

    bool PrefabDatabase::IsOwnWrite(const fs::path& file, uint64_t contentHash) const
    {
        auto it = m_OwnWrites.find(FileSystem::NormalizePath(file));
        return it != m_OwnWrites.end() && it->second == contentHash;
    }

    void PrefabDatabase::Invalidate(std::string_view relPath)
    {
        const std::string key = NormalizePath(relPath);
        // The prefabs made of it hold a copy of it brought up to date: they are read again too.
        std::unordered_set<std::string> drop{ key };
        bool grew = true;
        while (grew)
        {
            grew = false;
            for (const auto& [path, entry] : m_Entries)
                if (!drop.contains(path))
                    for (const std::string& dep : entry->T.Deps)
                        if (drop.contains(dep))
                        {
                            drop.insert(path);
                            grew = true;
                            break;
                        }
        }
        for (const std::string& path : drop)
            m_Entries.erase(path);
        m_Hashes.clear();
        m_ScanValid = false;
    }

    void PrefabDatabase::Clear()
    {
        m_Entries.clear();
        m_Hashes.clear();
        m_Uses.clear();
        m_ScanValid = false;
    }

}
