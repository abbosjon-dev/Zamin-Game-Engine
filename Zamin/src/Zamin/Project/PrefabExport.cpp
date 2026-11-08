#include "Zamin/Project/PrefabExport.h"

#include "Zamin/Asset/AssetReferences.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Project/ExportAssets.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Prefab.h"
#include "Zamin/Scene/PrefabDatabase.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneMigration.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scene/SceneSerializerJson.h"
#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Utils/StringUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <set>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Zamin {

    using json = nlohmann::json;
    using Severity = PrefabExportIssue::Severity;

    namespace {
        // The editor's prefab link keys under an entity's "Editor" (docs/prefabs.md).
        constexpr const char* s_LinkKeys[] = { "Prefab", "PrefabUUID", "PrefabEntity", "PrefabHash", "Overrides", "RemovedComponents", "RemovedEntities" };

        bool IsPrefabPath(std::string_view value)
        {
            return value.size() > 8 && StringUtils::ToLower(std::string(value.substr(value.size() - 8))) == ".zprefab";
        }

        // A string naming a prefab somewhere in a scene / prefab file.
        struct PrefabRef
        {
            std::string Path;      // as written
            bool Editor = false;   // under "Editor" (an instance's link), not a value the game reads
            std::string Entity;    // the entity's name
        };

        void FindRefs(const json& j, bool editor, const std::string& entity, std::vector<PrefabRef>& out)
        {
            if (j.is_string())
            {
                if (const std::string& s = j.get_ref<const std::string&>(); IsPrefabPath(s))
                    out.push_back({ s, editor, entity });
                return;
            }
            if (j.is_array())
            {
                for (const json& item : j)
                    FindRefs(item, editor, entity, out);
                return;
            }
            if (j.is_object())
                for (const auto& [key, value] : j.items())
                    FindRefs(value, editor || key == "Editor", entity, out);
        }

        std::vector<PrefabRef> FindPrefabRefs(const json& root)
        {
            std::vector<PrefabRef> out;
            if (!root.is_object())
                return out;
            for (const auto& [key, value] : root.items())
            {
                if (key != "Entities" || !value.is_array())
                {
                    FindRefs(value, false, std::string(), out);
                    continue;
                }
                for (const json& e : value)
                {
                    std::string tag;
                    if (e.is_object())
                        if (auto t = e.find("Tag"); t != e.end() && t->is_string())
                            tag = t->get<std::string>();
                    FindRefs(e, false, tag, out);
                }
            }
            return out;
        }

        bool HasLinkKeys(const json& root)
        {
            if (!root.is_object())
                return false;
            auto list = root.find("Entities");
            if (list == root.end() || !list->is_array())
                return false;
            for (const json& e : *list)
            {
                if (!e.is_object())
                    continue;
                auto editor = e.find("Editor");
                if (editor == e.end() || !editor->is_object())
                    continue;
                for (const char* key : s_LinkKeys)
                    if (editor->contains(key))
                        return true;
            }
            return false;
        }

        bool StripJson(json& root)
        {
            bool changed = false;
            if (!root.is_object())
                return false;
            auto list = root.find("Entities");
            if (list == root.end() || !list->is_array())
                return false;
            for (json& e : *list)
            {
                if (!e.is_object())
                    continue;
                auto editor = e.find("Editor");
                if (editor == e.end() || !editor->is_object())
                    continue;
                for (const char* key : s_LinkKeys)
                    changed |= editor->erase(key) > 0;
                if (editor->empty())
                {
                    e.erase(editor);
                    changed = true;
                }
            }
            return changed;
        }

        uint64_t ParseHash(const json& value)
        {
            if (!value.is_string())
                return 0;
            const std::string& text = value.get_ref<const std::string&>();
            uint64_t out = 0;
            std::from_chars(text.data(), text.data() + text.size(), out, 16);
            return out;
        }

        // A prefab file's nested instances older than their prefabs (the file left stale on disk).
        bool HasStaleNested(const json& root, PrefabDatabase& db)
        {
            for (const json& e : root.value("Entities", json::array()))
            {
                if (!e.is_object())
                    continue;
                auto editor = e.find("Editor");
                if (editor == e.end() || !editor->is_object())
                    continue;
                auto prefab = editor->find("Prefab");
                if (prefab == editor->end() || !prefab->is_string())
                    continue;
                const uint64_t hash = editor->contains("PrefabHash") ? ParseHash((*editor)["PrefabHash"]) : 0;
                if (hash != db.ResolvedHash(prefab->get<std::string>()))
                    return true;
            }
            return false;
        }

        std::string Plural(size_t n, const char* one) { return std::format("{} ta {}", n, one); }
    }

    // ------------------------------------------------------------------ result
    size_t PrefabExportResult::Count(Severity level) const
    {
        return (size_t)std::count_if(Issues.begin(), Issues.end(), [&](const PrefabExportIssue& i) { return i.Level == level; });
    }

    bool PrefabExportResult::HasErrors(bool warningsAsErrors) const
    {
        return Count(Severity::Error) > 0 || (warningsAsErrors && Count(Severity::Warning) > 0);
    }

    std::string PrefabExportResult::Summary() const
    {
        return std::format("{} tekshirildi: {} xato, {} ogohlantirish", Plural(PrefabsChecked, "prefab"), Count(Severity::Error),
                           Count(Severity::Warning));
    }

    bool PrefabExport::StripLinks(std::string& text)
    {
        json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !StripJson(root))
            return false;
        text = SceneSerializerJson::DumpIndented(root, text.size());
        return true;
    }

    // ------------------------------------------------------------------ the check
    PrefabExportResult PrefabExport::Prepare(const ExportAssetPlan& plan, PrefabDatabase& db, const AssetReferenceIndex* index)
    {
        PrefabExportResult result;
        const fs::path assets = db.GetAssetRoot();
        std::set<std::tuple<std::string, std::string, std::string>> said; // (rule, file, what): each once
        auto add = [&](Severity level, const char* rule, const std::string& file, std::string message, const std::string& key = {}) {
            if (!said.emplace(rule, file, key.empty() ? message : key).second)
                return;
            result.Issues.push_back({ level, rule, file, std::move(message) });
        };
        auto exists = [&](const std::string& rel) {
            std::error_code ec;
            return !rel.empty() && FileSystem::IsSafeRelativePath(rel) && fs::is_regular_file(assets / rel, ec);
        };
        std::unordered_set<std::string> planned(plan.Included.begin(), plan.Included.end());

        // ---- the scenes and prefabs the game gets, and the prefabs they name
        std::vector<std::string> files;
        for (const std::string& file : plan.Included)
        {
            const std::string ext = StringUtils::ToLower(fs::path(file).extension().string());
            if (ext == ".zscene" || ext == ".zprefab")
                files.push_back(PrefabDatabase::NormalizePath(file));
        }
        std::sort(files.begin(), files.end());
        std::set<std::string> prefabs;
        std::map<std::string, json> parsed;
        for (const std::string& file : files)
        {
            const bool isPrefab = fs::path(file).extension() == ".zprefab";
            if (isPrefab)
                prefabs.insert(file);
            json root = json::parse(FileSystem::ReadTextFile(assets / file), nullptr, false);
            if (root.is_discarded() || !root.is_object())
                continue; // a prefab: R2 below; a scene: not the prefab check's
            for (const PrefabRef& ref : FindPrefabRefs(root))
            {
                const std::string path = PrefabDatabase::NormalizePath(ref.Path);
                if (exists(path))
                {
                    prefabs.insert(path);
                    continue;
                }
                const std::string who = ref.Entity.empty() ? std::string() : std::format("'{}': ", ref.Entity);
                if (!ref.Editor)
                    add(Severity::Error, "R1", file, std::format("{}'{}' prefabi topilmadi - o'yinda undan nusxa yaratilmaydi (Instantiate bo'sh qaytaradi)", who, ref.Path),
                        path);
                else if (isPrefab)
                    add(Severity::Warning, "R8", file, std::format("{}ichma-ich / asosiy prefab topilmadi: '{}' (prefabdagi tayyor ma'lumot eksport qilinadi)", who, ref.Path),
                        path);
                else
                    add(Severity::Warning, "R8", file, std::format("{}nusxaning prefabi topilmadi: '{}' (sahnadagi ma'lumoti eksport qilinadi)", who, ref.Path), path);
            }
            parsed.emplace(file, std::move(root));
        }

        // ---- C++ string literals naming prefabs that are not there; assets prefabs name that are not there
        if (index)
            for (const MissingAssetReference& missing : index->GetMissing())
            {
                const std::string inside = FileSystem::GetRelativeInside(missing.File, index->GetAssetDirectory());
                if (IsPrefabPath(missing.Path))
                {
                    if (inside.empty()) // code (scene / prefab files: above)
                        add(Severity::Error, "R1", missing.Name,
                            std::format("C++ kodda '{}' prefabi yozilgan, lekin u topilmadi - Instantiate bo'sh qaytaradi", missing.Path),
                            PrefabDatabase::NormalizePath(missing.Path));
                }
                else if (!inside.empty() && fs::path(inside).extension() == ".zprefab" && planned.contains(inside))
                    add(Severity::Warning, "R7", inside, std::format("prefab ishlatadigan asset topilmadi: '{}'", missing.Path), missing.Path);
            }

        // ---- every prefab the game gets
        std::unordered_set<std::string> cyclic; // nested in each other: their copies are only stripped
        for (const std::string& prefab : prefabs)
        {
            result.PrefabsChecked++;
            const PrefabDatabase::Template* t = db.Get(prefab);
            if (!t || !t->Readable)
            {
                add(Severity::Error, "R2", prefab, std::format("prefab o'qilmadi: {}", t && !t->Issues.empty() ? t->Issues.front() : std::string("?")));
                continue;
            }
            if (t->Newer)
                add(Severity::Error, "R3", prefab,
                    std::format("prefab yangiroq formatda (v{}; bu dvigatel v{} gacha biladi) - o'yin uni to'liq o'qiy olmaydi", t->FileVersion,
                                SceneMigration::CurrentVersion));
            // Read on its own, with a report: what a scene load would say about it.
            Scene scratch;
            SceneLoadReport report;
            std::string error;
            SceneSerializer::EntitySetReadOptions options;
            options.GenerateNewUUIDs = false;
            options.KeepPrefabLinks = true;
            options.Report = &report;
            options.Error = &error;
            std::vector<Entity> made = SceneSerializer::DeserializeEntities(scratch, t->Text, options);
            for (const SceneLoadIssue& issue : report.Issues)
            {
                const std::string where = issue.Entity.empty() ? std::string() : std::format("'{}': ", issue.Entity);
                switch (issue.Type)
                {
                    case SceneLoadIssue::Kind::UnknownComponent:
                        add(Severity::Warning, "R5", prefab, std::format("noma'lum komponent: {}", issue.Message));
                        break;
                    case SceneLoadIssue::Kind::DuplicateID:
                    case SceneLoadIssue::Kind::BadHierarchy:
                        add(Severity::Warning, "R4", prefab, issue.Message);
                        break;
                    case SceneLoadIssue::Kind::BadEntity:
                    case SceneLoadIssue::Kind::BadComponent:
                        add(Severity::Warning, "R2", prefab, std::format("qisman o'qildi: {}", issue.Message));
                        break;
                    case SceneLoadIssue::Kind::MissingAsset:
                        add(Severity::Warning, "R7", prefab, std::format("{}prefab ishlatadigan asset topilmadi: '{}'", where, issue.Asset), issue.Asset);
                        break;
                    default:
                        break;
                }
            }
            // IDs and roots.
            if (auto file = parsed.find(prefab); file != parsed.end())
            {
                size_t noID = 0;
                for (const json& e : file->second.value("Entities", json::array()))
                    if (e.is_object() && (!e.contains("Entity") || (e["Entity"].is_number_unsigned() && e["Entity"].get<uint64_t>() == 0)))
                        noID++;
                if (noID)
                    add(Severity::Warning, "R4", prefab, std::format("{} ta obyektning ID'si yo'q yoki 0 (nusxalar bog'lanmaydi)", noID));
            }
            std::unordered_set<uint64_t> inSet;
            size_t roots = 0;
            for (Entity e : made)
            {
                inSet.insert((uint64_t)e.GetUUID());
                if (!scratch.GetParent(e))
                    roots++;
            }
            if (roots > 1)
                add(Severity::Warning, "R4", prefab, std::format("prefabda {} ta ildiz bor (bitta bo'lishi kerak): Instantiate faqat birinchisini qaytaradi", roots));
            // References to entities that are not in it.
            for (Entity e : made)
                Scene::ForEachEntityRef(e, [&](uint64_t& id, std::string_view key, std::string_view field) {
                    if (id && !inSet.contains(id))
                        add(Severity::Warning, "R6", prefab,
                            std::format("'{}' obyektidagi {}.{} prefabdan tashqaridagi obyektga ishora qiladi - boshqa sahnalarda u bo'lmaydi",
                                        e.GetName(), key, field));
                });
            // Its nested prefabs / base: missing, or made of it (a cycle).
            bool cycle = false;
            for (const std::string& dep : t->Deps)
            {
                if (!exists(dep))
                {
                    add(Severity::Warning, "R8", prefab, std::format("ichma-ich / asosiy prefab topilmadi: '{}' (prefabdagi tayyor ma'lumot eksport qilinadi)", dep), dep);
                    continue;
                }
                if (dep == prefab || db.DependsOn(dep, prefab))
                {
                    add(Severity::Error, "R9", prefab, std::format("prefab sikli: {} -> {} -> ... -> {}", prefab, dep, prefab));
                    cycle = true;
                }
            }
            for (const std::string& issue : t->Issues)
                if (!cycle && issue.find("sikli") != std::string::npos)
                {
                    add(Severity::Error, "R9", prefab, issue);
                    cycle = true;
                }
            if (cycle)
                cyclic.insert(prefab);
        }

        // ---- the copies: prefab links out, stale instances brought up to date
        for (auto& [file, root] : parsed)
        {
            if (!HasLinkKeys(root))
                continue;
            const bool isPrefab = fs::path(file).extension() == ".zprefab";
            std::string text;
            if (isPrefab)
            {
                // Flat as the prefab makes it now (the nested prefabs' current state, no links).
                const PrefabDatabase::Template* t = db.Get(file);
                if (!t || !t->Readable || t->Newer || t->LoadErrors || cyclic.contains(file))
                {
                    StripJson(root);
                    text = SceneSerializerJson::DumpIndented(root);
                }
                else
                {
                    if (HasStaleNested(root, db))
                        result.Updated++;
                    text = t->Resolved;
                    StripLinks(text);
                }
            }
            else
            {
                // The scene as the editor would open it: older instances linked, every instance as
                // its prefab makes it now (its overrides kept).
                Ref<Scene> scene = CreateRef<Scene>();
                SceneSerializer serializer(scene);
                const std::string source = FileSystem::ReadTextFile(assets / file);
                bool synced = false;
                if (serializer.DeserializeFromString(source))
                {
                    PrefabUtility::LinkLegacyInstances(*scene, db);
                    for (Entity e : scene->GetEntitiesInOrder())
                    {
                        if (!PrefabUtility::IsInstanceRoot(e))
                            continue;
                        const std::string prefab = PrefabUtility::GetPrefabPath(e);
                        const PrefabDatabase::Template* t = db.Get(prefab);
                        if (!t || !t->Readable)
                            continue; // missing: R8 above
                        if (PrefabUtility::IsLegacyInstance(e))
                            add(Severity::Warning, "R10", file,
                                std::format("'{}': eski prefab nusxasi bog'lanmadi ({}): prefab o'zgarsa, nusxa yangilanmaydi", e.GetName(),
                                            t->Linkable ? std::string("obyektlari mos kelmadi") : t->Issues.empty() ? std::string("?") : t->Issues.front()));
                        else
                            for (const std::string& orphan : PrefabUtility::FindOrphanOverrides(e, db))
                                add(Severity::Warning, "R10", file, std::format("'{}' nusxasida eskirgan o'zgarish (prefabda bu qism yo'q): {}", e.GetName(), orphan));
                    }
                    const PrefabUtility::SyncResult sync = PrefabUtility::SyncScene(*scene, db);
                    if (sync.Changed)
                    {
                        result.Updated++;
                        text = SceneSerializer(scene).SerializeToString();
                        StripLinks(text);
                        synced = true;
                    }
                }
                if (!synced)
                {
                    StripJson(root);
                    text = SceneSerializerJson::DumpIndented(root, source.size());
                }
            }
            result.Baked[file] = std::move(text);
            result.Stripped++;
        }
        if (result.Updated)
            add(Severity::Info, "Info", std::string(),
                std::format("{} ta sahna/prefab eksportda yangilandi (eskirgan nusxalar; asl fayllar o'zgarmaydi)", result.Updated));
        return result;
    }

}
