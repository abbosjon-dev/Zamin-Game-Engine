#include "Zamin/Scene/Prefab.h"

#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/ComponentRegistry.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/PrefabDatabase.h"
#include "Zamin/Scene/PrefabOverridesJson.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneSerializer.h"
#include "Zamin/Scene/SceneSerializerJson.h"

#include <algorithm>
#include <format>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace Zamin {

    namespace PO = PrefabOverrides;

    namespace {

        // The entity as the prefab code compares it: no hidden / locked flags, no prefab links.
        const SceneSerializerJson::EntityJsonOptions s_CompareOptions{ true, false, false, false };

        const std::string& MarkerOf(Entity e)
        {
            static const std::string s_None;
            const auto* meta = e ? e.TryGetComponent<EditorMetadataComponent>() : nullptr;
            return meta ? meta->Prefab : s_None;
        }

        bool HasMarker(Entity e) { return !MarkerOf(e).empty(); }

        Entity ParentOf(Entity e)
        {
            const UUID parent = e.GetComponent<RelationshipComponent>().Parent;
            return (uint64_t)parent ? e.GetScene()->GetEntityByUUID(parent) : Entity();
        }

        std::vector<Entity> ChildrenOf(Entity e)
        {
            std::vector<Entity> out;
            for (UUID child : e.GetComponent<RelationshipComponent>().Children)
                if (Entity c = e.GetScene()->GetEntityByUUID(child))
                    out.push_back(c);
            return out;
        }

        void CollectSubtree(Entity e, std::vector<Entity>& out)
        {
            out.push_back(e);
            for (Entity c : ChildrenOf(e))
                CollectSubtree(c, out);
        }

        // The entities of an instance: the root's subtree without the instances placed below it.
        void CollectMembers(Entity e, std::vector<Entity>& out)
        {
            out.push_back(e);
            for (Entity c : ChildrenOf(e))
                if (!HasMarker(c))
                    CollectMembers(c, out);
        }

        uint64_t LocalOf(Entity e)
        {
            const auto* link = e.TryGetComponent<PrefabLinkComponent>();
            return link ? link->PrefabEntity : 0;
        }

        bool Contains(const std::vector<std::string>& list, std::string_view value)
        {
            return std::find(list.begin(), list.end(), value) != list.end();
        }

        // The prefab's .zmeta UUID (known to the active project's registry), else 0.
        UUID LookUpPrefabUUID(std::string_view path)
        {
            if (!Project::GetActive())
                return 0;
            AssetRegistry* registry = Project::GetAssetRegistry();
            if (!registry)
                return 0;
            const AssetMetadata* meta = registry->Find(std::filesystem::path(std::string(path)));
            return meta && meta->HasMetaFile ? UUID(meta->Handle.GetValue()) : UUID(0);
        }

        // An instance in a scene (or in a template: the prefabs nested in a prefab) with its prefab.
        struct Instance
        {
            Entity Root;
            const PrefabDatabase::Template* T = nullptr;
            std::vector<Entity> Members;                    // depth-first
            std::unordered_map<uint64_t, Entity> Linked;    // prefab ID -> its entity (the first one)
            std::vector<Entity> Duplicates;                 // a later entity with an ID already used
            std::unordered_set<uint64_t> Removed;           // prefab IDs the instance deleted (with their descendants)
            std::unordered_map<uint64_t, uint64_t> Map;     // prefab ID -> the instance's UUID (removed: 0)
        };

        bool HasTemplateEntity(const PrefabDatabase::Template& t, uint64_t id) { return t.Entities && t.Entities->GetEntityByUUID(UUID(id)); }

        void MarkRemoved(Instance& in, uint64_t id)
        {
            if (!in.Removed.insert(id).second)
                return;
            for (uint64_t child : in.T->ChildrenOf(id))
                MarkRemoved(in, child);
        }

        void Collect(Instance& in)
        {
            in.Members.clear();
            in.Linked.clear();
            in.Duplicates.clear();
            in.Removed.clear();
            in.Map.clear();
            CollectMembers(in.Root, in.Members);
            for (Entity e : in.Members)
                if (uint64_t local = LocalOf(e))
                    if (!in.Linked.emplace(local, e).second)
                        in.Duplicates.push_back(e);
            if (const auto* link = in.Root.TryGetComponent<PrefabLinkComponent>())
                for (uint64_t id : link->RemovedEntities)
                    if (id != in.T->Root && HasTemplateEntity(*in.T, id) && !in.Linked.contains(id))
                        MarkRemoved(in, id);
            Scene& scene = *in.Root.GetScene();
            const uint64_t rootID = (uint64_t)in.Root.GetUUID();
            for (uint64_t local : in.T->Order)
            {
                if (in.Removed.contains(local))
                    in.Map[local] = 0;
                else if (auto it = in.Linked.find(local); it != in.Linked.end())
                    in.Map[local] = (uint64_t)it->second.GetUUID();
                else
                {
                    uint64_t id = PO::MixUUID(rootID, local);
                    if (scene.GetEntityByUUID(UUID(id)))
                        id = (uint64_t)UUID(); // taken: any free one
                    in.Map[local] = id;
                }
            }
        }

        bool IsLinkedRoot(Entity root, const PrefabDatabase::Template* t)
        {
            const auto* link = root.TryGetComponent<PrefabLinkComponent>();
            return t && link && link->PrefabEntity && link->PrefabEntity == t->Root;
        }

        // The instance root's prefab, read and linkable, the root linked to it.
        bool Prepare(Entity root, PrefabDatabase& db, Instance& in, bool requireLinked = true)
        {
            if (!root || !HasMarker(root))
                return false;
            const PrefabDatabase::Template* t = db.Get(MarkerOf(root));
            if (!t || !t->Readable || !t->Linkable)
                return false;
            if (requireLinked && !IsLinkedRoot(root, t))
                return false;
            in = Instance();
            in.Root = root;
            in.T = t;
            Collect(in);
            return true;
        }

        // The entities `locals` as the prefab makes them for this instance (references to the
        // prefab's entities pointing at the instance's ones).
        void BuildClean(const Instance& in, const std::vector<uint64_t>& locals, std::unordered_map<uint64_t, json>& out)
        {
            Scene scratch;
            std::vector<std::pair<uint64_t, Entity>> made;
            made.reserve(locals.size());
            for (uint64_t local : locals)
            {
                Entity src = in.T->Entities->GetEntityByUUID(UUID(local));
                auto id = in.Map.find(local);
                if (!src || id == in.Map.end() || !id->second)
                    continue;
                Entity e = scratch.CreateEntityWithUUID(UUID(id->second), src.GetName());
                e.GetComponent<TagComponent>().Active = src.GetComponent<TagComponent>().Active;
                for (const ComponentTypeInfo& type : ComponentRegistry::GetTypes())
                    if (type.Has(src))
                        type.Copy(src, e);
                if (const auto* meta = src.TryGetComponent<EditorMetadataComponent>(); meta && !meta->ComponentOrder.empty())
                    e.AddOrReplaceComponent<EditorMetadataComponent>().ComponentOrder = meta->ComponentOrder;
                if (uint64_t parent = in.T->ParentOf(local))
                    if (auto p = in.Map.find(parent); p != in.Map.end())
                        e.GetComponent<RelationshipComponent>().Parent = UUID(p->second);
                made.emplace_back(local, e);
            }
            std::vector<Entity> entities;
            entities.reserve(made.size());
            for (auto& [local, e] : made)
                entities.push_back(e);
            Scene::RemapEntityReferences(entities, in.Map);
            for (auto& [local, e] : made)
                out[local] = SceneSerializerJson::SerializeEntityJson(e, s_CompareOptions);
        }

        // The prefab IDs of the entity's children that are its prefab children, in their order now.
        std::vector<uint64_t> LinkedChildOrder(const Instance& in, Entity e, uint64_t local)
        {
            std::vector<uint64_t> order;
            for (Entity c : ChildrenOf(e))
            {
                const uint64_t cl = LocalOf(c);
                if (!cl || in.T->ParentOf(cl) != local)
                    continue;
                if (auto it = in.Linked.find(cl); it != in.Linked.end() && it->second == c)
                    order.push_back(cl);
            }
            return order;
        }

        // The prefab's order of those children.
        std::vector<uint64_t> TemplateOrder(const Instance& in, uint64_t local, const std::vector<uint64_t>& present)
        {
            std::unordered_set<uint64_t> set(present.begin(), present.end());
            std::vector<uint64_t> order;
            for (uint64_t child : in.T->ChildrenOf(local))
                if (set.contains(child))
                    order.push_back(child);
            return order;
        }

        bool RecordOverridesImpl(Instance& in)
        {
            bool changed = false;
            for (Entity dup : in.Duplicates)
                if (dup.IsValid() && dup.HasComponent<PrefabLinkComponent>())
                {
                    dup.RemoveComponent<PrefabLinkComponent>(); // a copy of a linked entity: added
                    changed = true;
                }
            in.Duplicates.clear();
            std::vector<uint64_t> locals;
            for (uint64_t local : in.T->Order)
                if (in.Linked.contains(local))
                    locals.push_back(local);
            std::unordered_map<uint64_t, json> clean;
            BuildClean(in, locals, clean);
            for (uint64_t local : locals)
            {
                Entity e = in.Linked[local];
                auto c = clean.find(local);
                if (c == clean.end())
                    continue;
                std::vector<std::string> paths, removed;
                PO::DiffPaths(c->second, SceneSerializerJson::SerializeEntityJson(e, s_CompareOptions), e == in.Root, paths, removed);
                const std::vector<uint64_t> order = LinkedChildOrder(in, e, local);
                if (order != TemplateOrder(in, local, order))
                {
                    paths.push_back("ChildOrder");
                    std::sort(paths.begin(), paths.end());
                }
                auto& link = e.GetComponent<PrefabLinkComponent>();
                if (link.Overrides != paths || link.RemovedComponents != removed)
                {
                    link.Overrides = std::move(paths);
                    link.RemovedComponents = std::move(removed);
                    changed = true;
                }
            }
            // The prefab's entities the instance does not have: the top-most ones.
            std::vector<uint64_t> removedTop;
            for (uint64_t local : in.T->Order)
            {
                if (local == in.T->Root || in.Linked.contains(local))
                    continue;
                const uint64_t parent = in.T->ParentOf(local);
                if (parent && in.Linked.contains(parent))
                    removedTop.push_back(local);
            }
            std::sort(removedTop.begin(), removedTop.end());
            auto& rootLink = in.Root.GetComponent<PrefabLinkComponent>();
            if (rootLink.RemovedEntities != removedTop)
            {
                rootLink.RemovedEntities = std::move(removedTop);
                changed = true;
            }
            return changed;
        }

        // Writes the keys where `merged` differs from `current` onto the entity (never AddComponent on
        // a component it has: ApplyComponentJson copies through a scratch entity).
        bool ApplyDifferences(Entity e, const json& merged, const json& current)
        {
            std::set<std::string> keys;
            for (auto it = merged.begin(); it != merged.end(); ++it)
                keys.insert(it.key());
            for (auto it = current.begin(); it != current.end(); ++it)
                keys.insert(it.key());
            bool changed = false;
            for (const std::string& key : keys)
            {
                if (key == "Entity" || key == "Editor" || key == "Parent" || key == "ChildOrder")
                    continue;
                auto m = merged.find(key);
                auto c = current.find(key);
                const json* mv = m != merged.end() ? &*m : nullptr;
                const json* cv = c != current.end() ? &*c : nullptr;
                if (!mv && !cv)
                    continue;
                if (mv && cv && PO::ValuesEqual(*mv, *cv))
                    continue;
                std::string error;
                if (SceneSerializerJson::ApplyComponentJson(e, key, mv, &error))
                    changed = true;
                else if (!error.empty())
                    ZM_CORE_WARN("Prefab: '{}' obyektining {} qiymati yozilmadi ({})", e.GetName(), key, error);
            }
            return changed;
        }

        // The prefab children of `e` in the prefab's order (the slots they hold among its children
        // stay theirs; added children keep their places).
        bool ApplyTemplateChildOrder(const Instance& in, Entity e, uint64_t local)
        {
            auto& children = e.GetComponent<RelationshipComponent>().Children;
            std::vector<size_t> slots;
            std::vector<uint64_t> present;
            for (size_t i = 0; i < children.size(); i++)
            {
                Entity c = e.GetScene()->GetEntityByUUID(children[i]);
                const uint64_t cl = c ? LocalOf(c) : 0;
                if (!cl || in.T->ParentOf(cl) != local)
                    continue;
                if (auto it = in.Linked.find(cl); it != in.Linked.end() && it->second == c)
                {
                    slots.push_back(i);
                    present.push_back(cl);
                }
            }
            const std::vector<uint64_t> desired = TemplateOrder(in, local, present);
            if (desired == present)
                return false;
            for (size_t k = 0; k < slots.size(); k++)
                children[slots[k]] = in.Linked.at(desired[k]).GetUUID();
            return true;
        }

        bool SyncInstanceImpl(Instance& in, bool force, uint64_t hash)
        {
            Scene& scene = *in.Root.GetScene();
            bool complete = in.Duplicates.empty();
            for (uint64_t local : in.T->Order)
                if (!in.Removed.contains(local) && !in.Linked.contains(local))
                {
                    complete = false;
                    break;
                }
            bool stale = false;
            for (const auto& [local, e] : in.Linked)
                stale = stale || !HasTemplateEntity(*in.T, local);
            if (!force && hash && in.Root.GetComponent<PrefabLinkComponent>().PrefabHash == hash && complete && !stale)
                return false;

            bool changed = false;
            for (Entity dup : in.Duplicates)
                if (dup.IsValid() && dup.HasComponent<PrefabLinkComponent>())
                {
                    dup.RemoveComponent<PrefabLinkComponent>();
                    changed = true;
                }
            in.Duplicates.clear();

            std::vector<uint64_t> locals;
            for (uint64_t local : in.T->Order)
                if (!in.Removed.contains(local))
                    locals.push_back(local);
            std::unordered_map<uint64_t, json> clean;
            BuildClean(in, locals, clean);

            // The prefab's entities the instance does not have yet (not deleted in it).
            std::unordered_set<uint64_t> created;
            for (uint64_t local : locals)
            {
                if (in.Linked.contains(local))
                    continue;
                auto c = clean.find(local);
                if (c == clean.end())
                    continue;
                Entity e = scene.CreateEntityWithUUID(UUID(in.Map[local]), c->second.value("Tag", std::string("Entity")));
                json data = c->second;
                data.erase("Parent");
                data.erase("Entity");
                try
                {
                    SceneSerializerJson::ReadEntityJson(data, e);
                }
                catch (const std::exception& ex)
                {
                    ZM_CORE_WARN("Prefab: '{}' obyekti to'liq o'qilmadi ({})", e.GetName(), ex.what());
                }
                e.AddOrReplaceComponent<PrefabLinkComponent>().PrefabEntity = local;
                in.Linked[local] = e;
                created.insert(local);
                changed = true;
            }

            // The values of the ones it has: the prefab's, except where the instance changed them.
            for (uint64_t local : locals)
            {
                if (created.contains(local))
                    continue;
                Entity e = in.Linked[local];
                const json current = SceneSerializerJson::SerializeEntityJson(e, s_CompareOptions);
                std::vector<std::string> orphans;
                const auto& link = e.GetComponent<PrefabLinkComponent>();
                const json merged = PO::Merge(clean[local], current, link.Overrides, link.RemovedComponents, e == in.Root, &orphans);
                for (const std::string& path : orphans)
                    ZM_CORE_WARN("Prefab {}: '{}' obyektining '{}' o'zgarishi eskirdi (prefabda bu qism yo'q) - olib tashlandi", in.T->Path,
                                 e.GetName(), path);
                changed |= ApplyDifferences(e, merged, current);
            }

            // Parents (unless the instance moved the entity to another one of its own).
            for (uint64_t local : locals)
            {
                Entity e = in.Linked[local];
                if (!e || e == in.Root)
                    continue;
                if (!created.contains(local) && Contains(e.GetComponent<PrefabLinkComponent>().Overrides, "Parent"))
                {
                    Entity parent = ParentOf(e);
                    // Only inside the instance: moved out of it, it goes back.
                    if (parent && std::find(in.Members.begin(), in.Members.end(), parent) != in.Members.end())
                        continue;
                }
                const uint64_t parentLocal = in.T->ParentOf(local);
                auto p = in.Linked.find(parentLocal);
                if (p == in.Linked.end() || !p->second)
                    continue;
                if (ParentOf(e) != p->second)
                {
                    scene.SetParent(e, p->second, false);
                    changed = true;
                }
            }

            // Entities the prefab no longer has: what the instance added below them moves up.
            std::vector<std::pair<uint64_t, Entity>> gone;
            for (const auto& [local, e] : in.Linked)
                if (!HasTemplateEntity(*in.T, local))
                    gone.emplace_back(local, e);
            std::unordered_set<entt::entity> goneSet;
            for (auto& [local, e] : gone)
                goneSet.insert((entt::entity)e);
            auto depth = [](Entity e) {
                int d = 0;
                for (Entity p = ParentOf(e); p; p = ParentOf(p))
                    d++;
                return d;
            };
            std::sort(gone.begin(), gone.end(), [&](const auto& a, const auto& b) { return depth(a.second) > depth(b.second); });
            for (auto& [local, e] : gone)
            {
                if (!e.IsValid())
                    continue;
                Entity keep = ParentOf(e);
                while (keep && goneSet.contains((entt::entity)keep))
                    keep = ParentOf(keep);
                if (!keep)
                    keep = in.Root;
                for (Entity child : ChildrenOf(e))
                {
                    if (goneSet.contains((entt::entity)child))
                        continue;
                    ZM_CORE_WARN("Prefab {}: '{}' prefabdan olib tashlangan - unga qo'shilgan '{}' '{}' ga ko'chirildi", in.T->Path, e.GetName(),
                                 child.GetName(), keep.GetName());
                    scene.SetParent(child, keep, true);
                }
                in.Linked.erase(local);
                scene.DestroyEntity(e);
                changed = true;
            }

            // The prefab's child order (unless the instance has its own).
            for (uint64_t local : locals)
            {
                auto it = in.Linked.find(local);
                if (it == in.Linked.end() || !it->second.IsValid())
                    continue;
                Entity e = it->second;
                if (!created.contains(local) && Contains(e.GetComponent<PrefabLinkComponent>().Overrides, "ChildOrder"))
                    continue;
                changed |= ApplyTemplateChildOrder(in, e, local);
            }

            auto& rootLink = in.Root.GetComponent<PrefabLinkComponent>();
            if (rootLink.PrefabHash != hash)
            {
                rootLink.PrefabHash = hash;
                changed = true;
            }
            Collect(in);
            changed |= RecordOverridesImpl(in);
            scene.InvalidateActiveCache();
            return changed;
        }

        // The JSON of a parsed file entity list, by "Entity" ID.
        json* FindFileEntity(json& root, uint64_t id)
        {
            auto list = root.find("Entities");
            if (list == root.end() || !list->is_array())
                return nullptr;
            for (json& e : *list)
                if (e.is_object() && e.value("Entity", (uint64_t)0) == id)
                    return &e;
            return nullptr;
        }

        // The nested prefabs' links of a prefab file's entities, recorded again (what the file's
        // entities differ in from the prefabs nested in them). False with `error` on a cycle.
        bool NormalizeNestedLinks(json& root, PrefabDatabase& db, const std::string& self, std::string* error)
        {
            auto list = root.find("Entities");
            if (list == root.end() || !list->is_array())
                return true;
            bool any = false;
            for (const json& e : *list)
                if (auto ed = e.find("Editor"); e.is_object() && ed != e.end() && ed->is_object() && ed->contains("Prefab"))
                    any = true;
            if (!any)
                return true;
            Scene scratch;
            SceneSerializer::EntitySetReadOptions options;
            options.GenerateNewUUIDs = false;
            options.KeepPrefabLinks = true;
            SceneSerializer::DeserializeEntities(scratch, root.dump(), options);
            for (Entity e : scratch.GetEntitiesInHierarchyOrder())
            {
                if (!HasMarker(e))
                    continue;
                const std::string nested = PrefabDatabase::NormalizePath(MarkerOf(e));
                if (nested == self || db.DependsOn(nested, self))
                {
                    if (error)
                        *error = std::format("Prefab sikli: {} o'z ichida {} ni ishlatadi, u esa {} dan iborat", self, nested, self);
                    return false;
                }
                Instance in;
                if (Prepare(e, db, in))
                    RecordOverridesImpl(in);
            }
            static const char* const s_LinkKeys[] = { "PrefabEntity", "Overrides", "RemovedComponents", "PrefabHash", "RemovedEntities" };
            for (json& e : *list)
            {
                if (!e.is_object())
                    continue;
                Entity s = scratch.GetEntityByUUID(UUID(e.value("Entity", (uint64_t)0)));
                if (!s)
                    continue;
                const json fresh = SceneSerializerJson::SerializeEntityJson(s, { true, false, true, true });
                const json* editor = fresh.contains("Editor") ? &fresh["Editor"] : nullptr;
                json& ed = e["Editor"];
                if (!ed.is_object())
                    ed = json::object();
                for (const char* key : s_LinkKeys)
                {
                    if (editor && editor->contains(key))
                        ed[key] = (*editor)[key];
                    else
                        ed.erase(key);
                }
                if (ed.empty())
                    e.erase("Editor");
            }
            return true;
        }

        std::string DumpFile(const json& root) { return root.dump(2); }

        // The value at the same reference place of another entity (0 when it has none).
        uint64_t RefAt(Entity e, std::string_view key, std::string_view field)
        {
            uint64_t found = 0;
            if (e)
                Scene::ForEachEntityRef(e, [&](uint64_t& id, std::string_view k, std::string_view f) {
                    if (k == key && f == field)
                        found = id;
                });
            return found;
        }

        bool RefSiteTouches(std::string_view site, std::string_view path)
        {
            return !path.empty() && (PO::IsUnder(site, path) || PO::IsUnder(path, site));
        }

        void CopyComponents(Entity from, Entity to)
        {
            to.GetComponent<TagComponent>().Active = from.GetComponent<TagComponent>().Active;
            for (const ComponentTypeInfo& type : ComponentRegistry::GetTypes())
                if (type.Has(from))
                    type.Copy(from, to);
        }

        void PropagateFrom(const std::string& prefab, PrefabDatabase& db, PrefabApplyResult& result)
        {
            for (const std::string& dependent : db.GetDependents(prefab))
            {
                const size_t before = result.Files.size();
                PrefabUtility::SyncPrefabFile(db, dependent, &result.Files);
                if (result.Files.size() > before)
                    result.Written.push_back(dependent);
            }
        }

        void SyncAfterWrite(Scene& scene, const std::string& prefab, PrefabDatabase& db, PrefabApplyResult& result, Entity self)
        {
            std::vector<std::string> filter{ prefab };
            for (const std::string& d : db.GetDependents(prefab))
                filter.push_back(d);
            const UUID selfID = self ? self.GetUUID() : UUID(0);
            if (self)
                PrefabUtility::SyncInstance(self, db, true);
            PrefabUtility::SyncResult sync = PrefabUtility::SyncScene(scene, db, &filter);
            result.InstancesSynced = sync.Changed;
            (void)selfID;
        }

        PrefabApplyResult Fail(std::string error)
        {
            PrefabApplyResult r;
            r.Error = std::move(error);
            return r;
        }

        // ---- Apply into the instance's own prefab
        PrefabApplyResult ApplyOwn(Instance& in, Entity entity, PrefabScope scope, std::string_view path, PrefabDatabase& db)
        {
            PrefabApplyResult result;
            Scene& scene = *in.Root.GetScene();
            const std::string prefab = in.T->Path;
            RecordOverridesImpl(in);
            Collect(in);

            std::vector<Entity> full;
            CollectSubtree(in.Root, full);
            std::unordered_map<uint64_t, uint64_t> ids;   // instance UUID -> prefab ID
            std::unordered_set<uint64_t> used(in.T->Order.begin(), in.T->Order.end());
            for (const auto& [local, e] : in.Linked)
                if (HasTemplateEntity(*in.T, local))
                    ids[(uint64_t)e.GetUUID()] = local;
            std::unordered_set<uint64_t> added;
            for (Entity e : full)
                if (!ids.contains((uint64_t)e.GetUUID()))
                    added.insert((uint64_t)e.GetUUID());

            const bool entityAdded = added.contains((uint64_t)entity.GetUUID());
            if (scope == PrefabScope::Field || scope == PrefabScope::Component)
                if (entityAdded)
                    return Fail("Bu obyekt prefabda yo'q: avval obyektni qo'llang");

            // What goes into the file.
            std::unordered_set<uint64_t> inScope;
            if (scope == PrefabScope::Entity && entityAdded)
            {
                std::vector<Entity> sub;
                CollectSubtree(entity, sub);
                for (Entity e : sub)
                    inScope.insert((uint64_t)e.GetUUID());
            }
            std::vector<Entity> writeSet;
            for (Entity e : full)
            {
                const uint64_t id = (uint64_t)e.GetUUID();
                if (ids.contains(id) || scope == PrefabScope::Instance || inScope.contains(id))
                    writeSet.push_back(e);
            }
            // Added entities get IDs of their own (their UUID when the prefab does not use it).
            for (Entity e : writeSet)
            {
                const uint64_t id = (uint64_t)e.GetUUID();
                if (ids.contains(id))
                    continue;
                uint64_t local = id;
                while (!local || used.contains(local))
                    local = (uint64_t)UUID();
                used.insert(local);
                ids[id] = local;
            }
            // Its parent must be in the file.
            if (scope == PrefabScope::Entity && entityAdded)
            {
                Entity parent = ParentOf(entity);
                if (!parent || !ids.contains((uint64_t)parent.GetUUID()))
                    return Fail("Obyektning ota-onasi prefabda yo'q: avval uni qo'llang");
            }

            // The file's entities in a scratch scene (UUID = prefab ID).
            std::string fieldSite = scope == PrefabScope::Field ? std::string(path) : std::string();
            Ref<Scene> local = CreateRef<Scene>();
            std::vector<Entity> ordered;
            std::vector<std::pair<Entity, Entity>> sources; // (scratch, instance)
            std::string refused;
            for (Entity e : writeSet)
            {
                const uint64_t id = ids[(uint64_t)e.GetUUID()];
                Entity c = local->CreateEntityWithUUID(UUID(id), e.GetName());
                CopyComponents(e, c);
                if (const auto* meta = e.TryGetComponent<EditorMetadataComponent>(); meta && !meta->ComponentOrder.empty())
                    c.AddOrReplaceComponent<EditorMetadataComponent>().ComponentOrder = meta->ComponentOrder;
                if (e != in.Root)
                    if (Entity p = ParentOf(e))
                        if (auto pid = ids.find((uint64_t)p.GetUUID()); pid != ids.end())
                            if (Entity cp = local->GetEntityByUUID(UUID(pid->second)))
                            {
                                c.GetComponent<RelationshipComponent>().Parent = cp.GetUUID();
                                cp.GetComponent<RelationshipComponent>().Children.push_back(c.GetUUID());
                            }
                // What the file keeps of nested prefabs: the instances placed below the instance in the
                // scene keep their own links; the prefab's nested parts keep the file's.
                Entity owner = PrefabUtility::GetInstanceRoot(e);
                if (owner && owner != in.Root)
                {
                    if (e == owner)
                        c.AddOrReplaceComponent<EditorMetadataComponent>().Prefab = MarkerOf(e);
                    if (const auto* link = e.TryGetComponent<PrefabLinkComponent>())
                        c.AddOrReplaceComponent<PrefabLinkComponent>(*link);
                }
                else if (Entity src = HasTemplateEntity(*in.T, id) ? in.T->Entities->GetEntityByUUID(UUID(id)) : Entity())
                {
                    if (HasMarker(src))
                        c.AddOrReplaceComponent<EditorMetadataComponent>().Prefab = MarkerOf(src);
                    if (const auto* link = src.TryGetComponent<PrefabLinkComponent>())
                        c.AddOrReplaceComponent<PrefabLinkComponent>(*link);
                }
                if (e == in.Root)
                {
                    // Where the instance is placed is its own: the prefab keeps its root's.
                    Entity src = in.T->Entities->GetEntityByUUID(UUID(in.T->Root));
                    c.GetComponent<TagComponent>().Tag = src.GetName();
                    auto& t = c.GetComponent<TransformComponent>();
                    const auto& st = src.GetComponent<TransformComponent>();
                    t.Translation = st.Translation;
                    t.Rotation = st.Rotation;
                    if (auto* ui = c.TryGetComponent<UITransformComponent>())
                        if (const auto* sui = src.TryGetComponent<UITransformComponent>())
                            ui->Position = sui->Position;
                }
                ordered.push_back(c);
                sources.emplace_back(c, e);
            }
            local->InvalidateNameCache();
            // References in prefab IDs; to entities outside the file: what the file had (they stay overrides).
            for (auto& [c, e] : sources)
            {
                const uint64_t id = (uint64_t)c.GetUUID();
                Entity old = HasTemplateEntity(*in.T, id) ? in.T->Entities->GetEntityByUUID(UUID(id)) : Entity();
                Scene::ForEachEntityRef(c, [&](uint64_t& ref, std::string_view key, std::string_view field) {
                    if (!ref)
                        return;
                    if (auto it = ids.find(ref); it != ids.end())
                    {
                        ref = it->second;
                        return;
                    }
                    const bool toAdded = added.contains(ref);
                    const std::string site = std::format("{}/{}", key, field);
                    if (RefSiteTouches(site, fieldSite))
                        refused = toAdded ? "Avval qo'shilgan obyektni qo'llang (havola unga ko'rsatadi)"
                                          : "Tashqi havola prefabga yozilmaydi (prefabdan tashqaridagi obyekt)";
                    ref = RefAt(old, key, field);
                    result.Skipped.push_back(std::format("{}: {} - {}", e.GetName(), site,
                                                         toAdded ? "qo'shilgan obyektga havola" : "tashqi havola (prefabdan tashqarida)"));
                });
            }
            if (!refused.empty())
                return Fail(refused);

            json oldRoot = json::parse(in.T->Text, nullptr, false);
            if (oldRoot.is_discarded() || !oldRoot.is_object())
                return Fail("Prefab fayli o'qilmadi");
            SceneSerializer::EntitySetWriteOptions writeOptions;
            writeOptions.HiddenLocked = false;
            json written = json::parse(SceneSerializer::SerializeEntities(*local, ordered, writeOptions), nullptr, false);
            if (written.is_discarded())
                return Fail("Prefab ma'lumotlari yozilmadi");
            json& newList = written["Entities"];
            std::unordered_map<uint64_t, const json*> newByID;
            for (const json& e : newList)
                newByID[e.value("Entity", (uint64_t)0)] = &e;

            json file = oldRoot;
            if (scope == PrefabScope::Instance)
                file["Entities"] = newList;
            else
            {
                auto patch = [&](uint64_t id, const std::string& p) {
                    json* dst = FindFileEntity(file, id);
                    auto src = newByID.find(id);
                    if (!dst || src == newByID.end())
                        return;
                    if (p == "Tag" || p == "Active" || p == "Parent" || p == "ChildOrder")
                    {
                        if (auto v = src->second->find(p); v != src->second->end())
                            (*dst)[p] = *v;
                        else
                            dst->erase(p);
                        return;
                    }
                    const std::string parentPath = p.find('/') == std::string::npos ? std::string() : p.substr(0, p.rfind('/'));
                    const json* value = PO::Get(*src->second, p);
                    const json* sidecars = parentPath.empty() ? src->second : PO::Get(*src->second, parentPath);
                    if (value)
                        PO::Set(*dst, p, *value, sidecars);
                    else
                        PO::Erase(*dst, p);
                };
                auto removeComponent = [&](uint64_t id, const std::string& key) {
                    if (json* dst = FindFileEntity(file, id))
                        dst->erase(key);
                };
                const uint64_t id = ids[(uint64_t)entity.GetUUID()];
                const auto* link = entity.TryGetComponent<PrefabLinkComponent>();
                const std::vector<std::string> overrides = link ? link->Overrides : std::vector<std::string>();
                const std::vector<std::string> removed = link ? link->RemovedComponents : std::vector<std::string>();
                if (scope == PrefabScope::Field)
                {
                    if (Contains(removed, path))
                        removeComponent(id, std::string(path));
                    else
                        patch(id, std::string(path));
                }
                else if (scope == PrefabScope::Component)
                {
                    for (const std::string& p : overrides)
                        if (PO::RootKey(p) == path)
                            patch(id, p);
                    if (Contains(removed, path))
                        removeComponent(id, std::string(path));
                }
                else if (!entityAdded)
                {
                    for (const std::string& p : overrides)
                        patch(id, p);
                    for (const std::string& key : removed)
                        removeComponent(id, key);
                }
                else
                {
                    // An added entity (with what is below it): after its parent's subtree in the file.
                    const uint64_t parentID = ids[(uint64_t)ParentOf(entity).GetUUID()];
                    json& list = file["Entities"];
                    std::unordered_map<uint64_t, uint64_t> parentOf;
                    for (const json& e : list)
                        if (e.is_object())
                            parentOf[e.value("Entity", (uint64_t)0)] = e.value("Parent", (uint64_t)0);
                    auto inside = [&](uint64_t e) {
                        for (int guard = 0; e && guard < 4096; guard++)
                        {
                            if (e == parentID)
                                return true;
                            e = parentOf[e];
                        }
                        return false;
                    };
                    size_t at = list.size();
                    for (size_t i = 0; i < list.size(); i++)
                        if (list[i].is_object() && inside(list[i].value("Entity", (uint64_t)0)))
                            at = i + 1;
                    std::vector<json> insert;
                    for (const json& e : newList)
                        if (auto it = std::find_if(sources.begin(), sources.end(),
                                                   [&](const auto& s) { return (uint64_t)s.first.GetUUID() == e.value("Entity", (uint64_t)0); });
                            it != sources.end() && inScope.contains((uint64_t)it->second.GetUUID()))
                            insert.push_back(e);
                    json rebuilt = json::array();
                    for (size_t i = 0; i <= list.size(); i++)
                    {
                        if (i == at)
                            for (json& e : insert)
                                rebuilt.push_back(std::move(e));
                        if (i < list.size())
                            rebuilt.push_back(std::move(list[i]));
                    }
                    list = std::move(rebuilt);
                    patch(parentID, "ChildOrder");
                }
            }

            std::string error;
            if (!NormalizeNestedLinks(file, db, prefab, &error))
                return Fail(error);
            if (!db.Write(prefab, DumpFile(file), &result.Files))
                return Fail(std::format("Prefab fayliga yozib bo'lmadi: {}", prefab));
            result.Written.push_back(prefab);
            result.Target = prefab;

            // The added entities that went in are the prefab's now; instances placed below the
            // instance became nested in the prefab (one link layer per file).
            for (Entity e : writeSet)
                if (added.contains((uint64_t)e.GetUUID()))
                {
                    if (e != in.Root && HasMarker(e))
                        scene.SetPrefabSource(e, "");
                    PrefabLinkComponent link;
                    link.PrefabEntity = ids[(uint64_t)e.GetUUID()];
                    e.AddOrReplaceComponent<PrefabLinkComponent>(std::move(link));
                }
            Entity root = in.Root;
            PropagateFrom(prefab, db, result);
            SyncAfterWrite(scene, prefab, db, result, root);
            std::sort(result.Skipped.begin(), result.Skipped.end());
            result.Skipped.erase(std::unique(result.Skipped.begin(), result.Skipped.end()), result.Skipped.end());
            result.Applied = true;
            return result;
        }

        // ---- Apply into a prefab nested in the instance's prefab (a value or a component)
        PrefabApplyResult ApplyNested(Instance& in, Entity entity, PrefabScope scope, std::string_view path, const std::string& target,
                                      PrefabDatabase& db)
        {
            if (scope != PrefabScope::Field && scope != PrefabScope::Component)
                return Fail("Ichma-ich prefabga faqat qiymat yoki komponent qo'llanadi");
            Scene& scene = *in.Root.GetScene();
            RecordOverridesImpl(in);
            Collect(in);
            const uint64_t entityLocal = LocalOf(entity);
            if (!entityLocal)
                return Fail("Bu obyekt prefabda yo'q");

            // Down the chain of nested prefabs: (file, the entity's ID there).
            std::vector<std::pair<std::string, uint64_t>> levels;
            std::unordered_map<uint64_t, uint64_t> map; // instance UUID -> ID at the current level
            for (const auto& [local, e] : in.Linked)
                map[(uint64_t)e.GetUUID()] = local;
            std::string current = in.T->Path;
            uint64_t currentLocal = entityLocal;
            bool reached = false;
            for (int depth = 0; depth < PrefabDatabase::MaxDepth && !reached; depth++)
            {
                const PrefabDatabase::Template* t = db.Get(current);
                if (!t || !t->Readable)
                    return Fail(std::format("Prefab o'qilmadi: {}", current));
                auto nested = t->NestedOwner.find(currentLocal);
                if (nested == t->NestedOwner.end())
                    break;
                levels.emplace_back(current, currentLocal);
                std::unordered_map<uint64_t, uint64_t> next;
                for (const auto& [uuid, l] : map)
                    if (auto n = t->NestedOwner.find(l); n != t->NestedOwner.end() && n->second.Owner == nested->second.Owner)
                        next[uuid] = n->second.LocalID;
                map = std::move(next);
                current = nested->second.Prefab;
                currentLocal = nested->second.LocalID;
                reached = current == target;
            }
            if (!reached)
                return Fail(std::format("{} bu obyekt uchun qo'llash maqsadi emas", target));
            const PrefabDatabase::Template* qt = db.Get(target);
            if (!qt || !qt->Readable || !HasTemplateEntity(*qt, currentLocal))
                return Fail(std::format("Prefab o'qilmadi: {}", target));

            // The entity's values in the nested prefab's IDs.
            Scene scratch;
            Entity c = scratch.CreateEntityWithUUID(UUID(currentLocal), entity.GetName());
            CopyComponents(entity, c);
            Entity old = qt->Entities->GetEntityByUUID(UUID(currentLocal));
            std::string refused;
            PrefabApplyResult result;
            Scene::ForEachEntityRef(c, [&](uint64_t& ref, std::string_view key, std::string_view field) {
                if (!ref)
                    return;
                if (auto it = map.find(ref); it != map.end())
                {
                    ref = it->second;
                    return;
                }
                const std::string site = std::format("{}/{}", key, field);
                if (scope == PrefabScope::Field ? RefSiteTouches(site, path) : PO::RootKey(site) == path)
                    refused = "Tashqi havola prefabga yozilmaydi (ichma-ich prefabdan tashqarida)";
                ref = RefAt(old, key, field);
            });
            if (!refused.empty())
                return Fail(refused);
            const json values = SceneSerializerJson::SerializeEntityJson(c, { false, false, false, false });

            const auto& link = entity.GetComponent<PrefabLinkComponent>();
            std::vector<std::string> paths;
            bool removeComponent = false;
            if (scope == PrefabScope::Field)
            {
                if (Contains(link.RemovedComponents, path))
                    removeComponent = true;
                else
                    paths.push_back(std::string(path));
            }
            else
            {
                for (const std::string& p : link.Overrides)
                    if (PO::RootKey(p) == path)
                        paths.push_back(p);
                removeComponent = Contains(link.RemovedComponents, path);
            }

            json file = json::parse(qt->Text, nullptr, false);
            json* dst = file.is_discarded() ? nullptr : FindFileEntity(file, currentLocal);
            if (!dst)
                return Fail(std::format("Prefab fayli o'qilmadi: {}", target));
            for (const std::string& p : paths)
            {
                if (p == "Tag" || p == "Active" || p == "Parent" || p == "ChildOrder")
                {
                    if (auto v = values.find(p); v != values.end())
                        (*dst)[p] = *v;
                    else
                        dst->erase(p);
                    continue;
                }
                const std::string parentPath = p.find('/') == std::string::npos ? std::string() : p.substr(0, p.rfind('/'));
                const json* value = PO::Get(values, p);
                if (value)
                    PO::Set(*dst, p, *value, parentPath.empty() ? &values : PO::Get(values, parentPath));
                else
                    PO::Erase(*dst, p);
            }
            if (removeComponent)
                dst->erase(std::string(path));
            std::string error;
            const std::string targetPath = qt->Path;
            if (!NormalizeNestedLinks(file, db, targetPath, &error))
                return Fail(error);
            if (!db.Write(targetPath, DumpFile(file), &result.Files))
                return Fail(std::format("Prefab fayliga yozib bo'lmadi: {}", targetPath));
            result.Written.push_back(targetPath);
            result.Target = targetPath;

            // The prefabs on the way inherit it now: their own change of it goes.
            auto drop = [&](std::vector<std::string>& list) {
                const size_t before = list.size();
                std::erase_if(list, [&](const std::string& p) {
                    return scope == PrefabScope::Field ? PO::IsUnder(p, path) : PO::RootKey(p) == path;
                });
                return list.size() != before;
            };
            for (const auto& [file2, id] : levels)
            {
                const PrefabDatabase::Template* t = db.Get(file2);
                if (!t || !t->Readable)
                    continue;
                json f = json::parse(t->Text, nullptr, false);
                json* e = f.is_discarded() ? nullptr : FindFileEntity(f, id);
                if (!e || !e->contains("Editor"))
                    continue;
                json& ed = (*e)["Editor"];
                bool changed = false;
                for (const char* key : { "Overrides", "RemovedComponents" })
                    if (auto list = ed.find(key); list != ed.end() && list->is_array())
                    {
                        std::vector<std::string> values2;
                        for (const json& v : *list)
                            if (v.is_string())
                                values2.push_back(v.get<std::string>());
                        if (std::string_view(key) == "RemovedComponents")
                        {
                            const size_t before = values2.size();
                            std::erase(values2, std::string(path));
                            changed |= values2.size() != before;
                        }
                        else
                            changed |= drop(values2);
                        if (values2.empty())
                            ed.erase(key);
                        else
                            ed[key] = values2;
                    }
                if (changed && db.Write(file2, DumpFile(f), &result.Files))
                    result.Written.push_back(file2);
            }

            // The instance inherits it too.
            {
                auto& l = entity.GetComponent<PrefabLinkComponent>();
                drop(l.Overrides);
                if (removeComponent)
                    std::erase(l.RemovedComponents, std::string(path));
            }
            Entity root = in.Root;
            PropagateFrom(targetPath, db, result);
            SyncAfterWrite(scene, targetPath, db, result, root);
            result.Applied = true;
            return result;
        }

    }

    // ------------------------------------------------------------------ queries
    Entity PrefabUtility::GetInstanceRoot(Entity entity)
    {
        for (Entity e = entity; e && e.IsValid(); e = ParentOf(e))
            if (HasMarker(e))
                return e;
        return {};
    }

    bool PrefabUtility::IsInstanceRoot(Entity entity) { return entity && entity.IsValid() && HasMarker(entity); }

    std::string PrefabUtility::GetPrefabPath(Entity root) { return PrefabDatabase::NormalizePath(MarkerOf(root)); }

    uint64_t PrefabUtility::GetPrefabEntity(Entity entity) { return entity && GetInstanceRoot(entity) ? LocalOf(entity) : 0; }

    bool PrefabUtility::IsLinked(Entity entity) { return GetPrefabEntity(entity) != 0; }

    bool PrefabUtility::IsAdded(Entity entity)
    {
        if (!entity || !entity.IsValid())
            return false;
        Entity root = GetInstanceRoot(entity);
        return root && root != entity && !LocalOf(entity) && !IsLegacyInstance(root);
    }

    bool PrefabUtility::IsLegacyInstance(Entity root) { return IsInstanceRoot(root) && !LocalOf(root); }

    bool PrefabUtility::HasOverrides(Entity entity)
    {
        const auto* link = entity ? entity.TryGetComponent<PrefabLinkComponent>() : nullptr;
        return link && link->HasOverrides();
    }

    std::vector<Entity> PrefabUtility::GetMembers(Entity root)
    {
        std::vector<Entity> members;
        if (root && root.IsValid())
            CollectMembers(root, members);
        return members;
    }

    // ------------------------------------------------------------------ instances
    Entity PrefabUtility::InstantiateLinked(Scene& scene, PrefabDatabase& db, std::string_view relPath, Entity parent)
    {
        const std::string path = PrefabDatabase::NormalizePath(relPath);
        const PrefabDatabase::Template* t = db.Get(path);
        if (!t || !t->Readable)
        {
            ZM_CORE_ERROR("Prefab o'qilmadi: {} ({})", path, t && !t->Issues.empty() ? t->Issues.front() : std::string("fayl yo'q"));
            return {};
        }
        const bool linkable = t->Linkable;
        const std::string text = linkable ? t->Resolved : t->Text;
        const uint64_t hash = linkable ? db.ResolvedHash(path) : 0;
        if (!linkable)
            ZM_CORE_WARN("Prefab bog'lanmadi: bitta ildiz va takrorlanmas ID kerak ({}: {})", path, t->Issues.empty() ? std::string() : t->Issues.front());
        std::unordered_map<uint64_t, UUID> ids;
        SceneSerializer::EntitySetReadOptions options;
        options.KeepPrefabLinks = false;
        options.IDsOut = &ids;
        std::string error;
        options.Error = &error;
        std::vector<Entity> created = SceneSerializer::DeserializeEntities(scene, text, options);
        if (created.empty())
        {
            ZM_CORE_ERROR("Prefab o'qilmadi: {} ({})", path, error);
            return {};
        }
        Entity root = created.front();
        if (linkable)
        {
            std::unordered_map<uint64_t, uint64_t> localOf;
            for (const auto& [file, id] : ids)
                localOf[(uint64_t)id] = file;
            for (Entity e : created)
                if (auto it = localOf.find((uint64_t)e.GetUUID()); it != localOf.end())
                    e.AddOrReplaceComponent<PrefabLinkComponent>().PrefabEntity = it->second;
        }
        scene.SetPrefabSource(root, path);
        if (linkable)
        {
            auto& link = root.GetComponent<PrefabLinkComponent>();
            link.PrefabUUID = LookUpPrefabUUID(path);
            link.PrefabHash = hash;
        }
        if (parent)
            scene.SetParent(root, parent, false);
        return root;
    }

    void PrefabUtility::LinkSource(Entity root, std::string_view relPath, PrefabDatabase& db)
    {
        if (!root)
            return;
        Scene& scene = *root.GetScene();
        const std::string path = PrefabDatabase::NormalizePath(relPath);
        db.Invalidate(path);
        const PrefabDatabase::Template* t = db.Get(path);
        if (!t || !t->Readable || !t->Linkable)
        {
            scene.SetPrefabSource(root, path);
            return;
        }
        std::vector<Entity> subtree;
        CollectSubtree(root, subtree);
        for (Entity e : subtree)
        {
            // The prefabs nested in it went into its file: in the scene they are part of this instance.
            if (e != root && HasMarker(e))
                scene.SetPrefabSource(e, "");
            PrefabLinkComponent link;
            link.PrefabEntity = (uint64_t)e.GetUUID();
            e.AddOrReplaceComponent<PrefabLinkComponent>(std::move(link));
        }
        scene.SetPrefabSource(root, path);
        auto& link = root.GetComponent<PrefabLinkComponent>();
        link.PrefabUUID = LookUpPrefabUUID(path);
        link.PrefabHash = db.ResolvedHash(path);
        // A nested prefab that was out of date in the scene is not in the file: what differs is recorded.
        RecordOverrides(root, db);
    }

    size_t PrefabUtility::LinkLegacyInstances(Scene& scene, PrefabDatabase& db)
    {
        if (scene.IsRunning())
            return 0;
        size_t linked = 0;
        std::vector<UUID> roots;
        for (Entity e : scene.GetEntitiesInHierarchyOrder())
            if (HasMarker(e))
                roots.push_back(e.GetUUID());
        for (UUID id : roots)
        {
            Entity root = scene.GetEntityByUUID(id);
            if (!root)
                continue;
            const PrefabDatabase::Template* t = db.Get(MarkerOf(root));
            if (!t || !t->Readable || !t->Linkable || IsLinkedRoot(root, t))
                continue;
            std::vector<Entity> members;
            CollectMembers(root, members);
            for (Entity e : members)
                if (e.HasComponent<PrefabLinkComponent>())
                    e.RemoveComponent<PrefabLinkComponent>();
            // The root with the root, then the children in order by name.
            size_t paired = 0;
            std::function<void(Entity, uint64_t)> pair = [&](Entity e, uint64_t local) {
                e.AddOrReplaceComponent<PrefabLinkComponent>().PrefabEntity = local;
                paired++;
                std::vector<Entity> children;
                for (Entity c : ChildrenOf(e))
                    if (!HasMarker(c))
                        children.push_back(c);
                std::vector<bool> used(children.size(), false);
                for (uint64_t child : t->ChildrenOf(local))
                {
                    Entity tc = t->Entities->GetEntityByUUID(UUID(child));
                    if (!tc)
                        continue;
                    for (size_t i = 0; i < children.size(); i++)
                        if (!used[i] && children[i].GetName() == tc.GetName())
                        {
                            used[i] = true;
                            pair(children[i], child);
                            break;
                        }
                }
            };
            pair(root, t->Root);
            const std::string path = PrefabDatabase::NormalizePath(MarkerOf(root));
            auto& link = root.GetComponent<PrefabLinkComponent>();
            link.PrefabUUID = LookUpPrefabUUID(path);
            link.PrefabHash = db.ResolvedHash(path);
            Instance in;
            if (Prepare(root, db, in))
                RecordOverridesImpl(in);
            size_t overrides = 0;
            for (Entity e : GetMembers(root))
                if (const auto* l = e.TryGetComponent<PrefabLinkComponent>())
                    overrides += l->Overrides.size() + l->RemovedComponents.size();
            overrides += root.GetComponent<PrefabLinkComponent>().RemovedEntities.size();
            ZM_CORE_INFO("Eski prefab nusxasi bog'landi: {} ({}/{}, {} override)", root.GetName(), paired, t->Order.size(), overrides);
            linked++;
        }
        return linked;
    }

    size_t PrefabUtility::NormalizeLinks(Scene& scene, const std::vector<Entity>* created)
    {
        size_t removed = 0;
        std::unordered_set<entt::entity> fresh;
        if (created)
            for (Entity e : *created)
                fresh.insert((entt::entity)e);
        std::vector<Entity> linked;
        for (auto handle : scene.GetAllEntitiesWith<PrefabLinkComponent>())
            linked.emplace_back(handle, &scene);
        std::unordered_map<entt::entity, std::vector<Entity>> byOwner;
        for (Entity e : linked)
        {
            Entity owner = GetInstanceRoot(e);
            const auto& link = e.GetComponent<PrefabLinkComponent>();
            if (!owner || link.IsDefault())
            {
                e.RemoveComponent<PrefabLinkComponent>();
                removed++;
                continue;
            }
            byOwner[(entt::entity)owner].push_back(e);
        }
        for (auto& [ownerHandle, list] : byOwner)
        {
            Entity owner(ownerHandle, &scene);
            std::vector<Entity> members;
            CollectMembers(owner, members);
            std::unordered_map<uint64_t, Entity> keep;
            // The ones that were there first keep their IDs (then the copies, in hierarchy order).
            for (int pass = 0; pass < 2; pass++)
                for (Entity e : members)
                {
                    const uint64_t local = LocalOf(e);
                    if (!local || fresh.contains((entt::entity)e) != (pass == 1))
                        continue;
                    if (!keep.emplace(local, e).second)
                    {
                        e.RemoveComponent<PrefabLinkComponent>();
                        removed++;
                    }
                }
        }
        return removed;
    }

    // ------------------------------------------------------------------ overrides
    bool PrefabUtility::RecordOverrides(Entity root, PrefabDatabase& db)
    {
        if (!root || root.GetScene()->IsRunning())
            return false;
        Instance in;
        return Prepare(root, db, in) && RecordOverridesImpl(in);
    }

    size_t PrefabUtility::RecordOverrides(Scene& scene, const std::vector<UUID>& touched, bool entitiesRemoved, PrefabDatabase& db)
    {
        if (scene.IsRunning())
            return 0;
        std::vector<UUID> roots;
        auto add = [&](Entity root) {
            if (root && std::find(roots.begin(), roots.end(), root.GetUUID()) == roots.end())
                roots.push_back(root.GetUUID());
        };
        for (UUID id : touched)
            if (Entity e = scene.GetEntityByUUID(id))
            {
                add(GetInstanceRoot(e));
                // A child moved out of an instance changes the instance it left.
                for (Entity p = ParentOf(e); p; p = ParentOf(p))
                    if (HasMarker(p))
                        add(p);
            }
        if (entitiesRemoved)
            for (auto handle : scene.GetAllEntitiesWith<PrefabLinkComponent>())
            {
                Entity e(handle, &scene);
                if (HasMarker(e))
                    add(e);
            }
        size_t changed = 0;
        for (UUID id : roots)
            if (Entity root = scene.GetEntityByUUID(id); root && RecordOverrides(root, db))
                changed++;
        return changed;
    }

    std::vector<PrefabOverrideInfo> PrefabUtility::GetOverrides(Entity root)
    {
        std::vector<PrefabOverrideInfo> out;
        if (!IsInstanceRoot(root))
            return out;
        for (Entity e : GetMembers(root))
        {
            const auto* link = e.TryGetComponent<PrefabLinkComponent>();
            if (!link || !link->PrefabEntity)
            {
                if (e != root && LocalOf(ParentOf(e)))
                    out.push_back({ PrefabOverrideInfo::Kind::AddedEntity, e.GetUUID(), 0, {}, e.GetName() });
                continue;
            }
            for (const std::string& path : link->Overrides)
            {
                const bool component = path.find('/') == std::string::npos && ComponentRegistry::Find(path);
                out.push_back({ component ? PrefabOverrideInfo::Kind::AddedComponent : PrefabOverrideInfo::Kind::Value, e.GetUUID(),
                                link->PrefabEntity, path, e.GetName() });
            }
            for (const std::string& key : link->RemovedComponents)
                out.push_back({ PrefabOverrideInfo::Kind::RemovedComponent, e.GetUUID(), link->PrefabEntity, key, e.GetName() });
        }
        if (const auto* link = root.TryGetComponent<PrefabLinkComponent>())
            for (uint64_t id : link->RemovedEntities)
                out.push_back({ PrefabOverrideInfo::Kind::RemovedEntity, 0, id, {}, {} });
        return out;
    }

    // ------------------------------------------------------------------ following the prefab
    bool PrefabUtility::SyncInstance(Entity root, PrefabDatabase& db, bool force)
    {
        if (!root || !root.IsValid() || root.GetScene()->IsRunning() || !HasMarker(root))
            return false;
        const std::string path = PrefabDatabase::NormalizePath(MarkerOf(root));
        const uint64_t hash = db.ResolvedHash(path);
        Instance in;
        if (!Prepare(root, db, in))
            return false;
        return SyncInstanceImpl(in, force, hash);
    }

    PrefabUtility::SyncResult PrefabUtility::SyncScene(Scene& scene, PrefabDatabase& db, const std::vector<std::string>* prefabs, bool force)
    {
        SyncResult result;
        if (scene.IsRunning())
            return result;
        result.Linked = LinkLegacyInstances(scene, db);
        NormalizeLinks(scene);
        std::vector<UUID> roots;
        for (Entity e : scene.GetEntitiesInHierarchyOrder())
            if (HasMarker(e))
                roots.push_back(e.GetUUID());
        std::vector<std::string> filter;
        if (prefabs)
            for (const std::string& p : *prefabs)
                filter.push_back(PrefabDatabase::NormalizePath(p));
        std::unordered_set<std::string> reported;
        for (UUID id : roots)
        {
            Entity root = scene.GetEntityByUUID(id);
            if (!root)
                continue;
            const std::string path = PrefabDatabase::NormalizePath(MarkerOf(root));
            if (prefabs && std::find(filter.begin(), filter.end(), path) == filter.end())
                continue;
            const PrefabDatabase::Template* t = db.Get(path);
            if (!t || !t->Readable || !t->Linkable)
            {
                if (reported.insert(path).second)
                    result.Issues.push_back(std::format("{}: {}", path, t && !t->Issues.empty() ? t->Issues.front() : std::string("o'qilmadi")));
                continue;
            }
            if (!IsLinkedRoot(root, t))
                continue;
            result.Instances++;
            if (SyncInstance(root, db, force))
                result.Changed++;
        }
        return result;
    }

    // ------------------------------------------------------------------ apply / revert
    std::vector<std::string> PrefabUtility::GetApplyTargets(Entity entity, PrefabDatabase& db)
    {
        std::vector<std::string> targets;
        Entity root = GetInstanceRoot(entity);
        if (!root)
            return targets;
        std::string path = PrefabDatabase::NormalizePath(MarkerOf(root));
        targets.push_back(path);
        uint64_t local = LocalOf(entity);
        for (int depth = 0; local && depth < PrefabDatabase::MaxDepth; depth++)
        {
            const PrefabDatabase::Template* t = db.Get(path);
            if (!t)
                break;
            auto nested = t->NestedOwner.find(local);
            if (nested == t->NestedOwner.end())
                break;
            path = nested->second.Prefab;
            local = nested->second.LocalID;
            if (std::find(targets.begin(), targets.end(), path) != targets.end())
                break; // a cycle
            targets.push_back(path);
        }
        return targets;
    }

    PrefabApplyResult PrefabUtility::Apply(Entity entity, PrefabScope scope, std::string_view path, PrefabDatabase& db, std::string_view target)
    {
        if (!entity || !entity.IsValid())
            return Fail("Obyekt yo'q");
        if (entity.GetScene()->IsRunning())
            return Fail("O'yin paytida prefabga yozilmaydi");
        Entity root = GetInstanceRoot(entity);
        if (!root)
            return Fail("Bu obyekt prefab nusxasi emas");
        Instance in;
        if (!Prepare(root, db, in))
            return Fail(std::format("Prefab bog'lanmagan yoki o'qilmadi: {}", GetPrefabPath(root)));
        if (scope == PrefabScope::Instance)
            entity = root;
        const std::vector<std::string> targets = GetApplyTargets(entity, db);
        const std::string goal = target.empty() ? targets.front() : PrefabDatabase::NormalizePath(target);
        auto it = std::find(targets.begin(), targets.end(), goal);
        if (it == targets.end())
            return Fail(std::format("{} bu obyekt uchun qo'llash maqsadi emas", goal));
        Prepare(root, db, in); // GetApplyTargets may have read the files again
        if (it == targets.begin())
            return ApplyOwn(in, entity, scope, path, db);
        return ApplyNested(in, entity, scope, path, goal, db);
    }

    bool PrefabUtility::Revert(Entity entity, PrefabScope scope, std::string_view path, PrefabDatabase& db)
    {
        if (!entity || !entity.IsValid() || entity.GetScene()->IsRunning())
            return false;
        Entity root = GetInstanceRoot(entity);
        Instance in;
        if (!Prepare(root, db, in))
            return false;
        Scene& scene = *root.GetScene();
        bool changed = false;
        if (scope == PrefabScope::Instance)
        {
            // Everything: the added entities go, the deleted ones come back.
            std::vector<Entity> addedTop;
            std::vector<Entity> full;
            CollectSubtree(root, full);
            std::unordered_set<entt::entity> members;
            for (Entity m : in.Members)
                if (LocalOf(m))
                    members.insert((entt::entity)m);
            for (Entity e : full)
                if (e != root && !members.contains((entt::entity)e) && members.contains((entt::entity)ParentOf(e)))
                    addedTop.push_back(e);
            for (Entity e : addedTop)
                if (e.IsValid())
                {
                    scene.DestroyEntity(e);
                    changed = true;
                }
            Collect(in);
            for (Entity e : in.Members)
                if (auto* link = e.TryGetComponent<PrefabLinkComponent>())
                {
                    changed |= link->HasOverrides();
                    link->Overrides.clear();
                    link->RemovedComponents.clear();
                    link->RemovedEntities.clear();
                }
        }
        else if (scope == PrefabScope::Entity && IsAdded(entity))
        {
            scene.DestroyEntity(entity);
            return true;
        }
        else
        {
            auto* link = entity.TryGetComponent<PrefabLinkComponent>();
            if (!link || !link->PrefabEntity)
                return false;
            const size_t before = link->Overrides.size() + link->RemovedComponents.size();
            if (scope == PrefabScope::Entity)
            {
                link->Overrides.clear();
                link->RemovedComponents.clear();
            }
            else if (scope == PrefabScope::Component)
            {
                std::erase_if(link->Overrides, [&](const std::string& p) { return PO::RootKey(p) == path; });
                std::erase(link->RemovedComponents, std::string(path));
            }
            else
            {
                std::erase_if(link->Overrides, [&](const std::string& p) { return PO::IsUnder(p, path); });
                std::erase(link->RemovedComponents, std::string(path));
            }
            changed = link->Overrides.size() + link->RemovedComponents.size() != before;
        }
        const uint64_t hash = db.ResolvedHash(GetPrefabPath(root));
        if (!Prepare(root, db, in))
            return changed;
        return SyncInstanceImpl(in, true, hash) || changed;
    }

    size_t PrefabUtility::RestoreRemoved(Entity root, PrefabDatabase& db)
    {
        Instance in;
        if (!root || root.GetScene()->IsRunning() || !Prepare(root, db, in))
            return 0;
        auto& link = root.GetComponent<PrefabLinkComponent>();
        if (link.RemovedEntities.empty())
            return 0;
        const size_t before = GetMembers(root).size();
        link.RemovedEntities.clear();
        const uint64_t hash = db.ResolvedHash(GetPrefabPath(root));
        if (Prepare(root, db, in))
            SyncInstanceImpl(in, true, hash);
        const size_t after = GetMembers(root).size();
        return after > before ? after - before : 0;
    }

    size_t PrefabUtility::Unpack(Entity root, bool completely)
    {
        if (!IsInstanceRoot(root))
            return 0;
        Scene& scene = *root.GetScene();
        size_t count = 0;
        std::vector<Entity> nestedRoots;
        if (completely)
        {
            std::vector<Entity> full;
            CollectSubtree(root, full);
            for (Entity e : full)
                if (e != root && HasMarker(e))
                    nestedRoots.push_back(e);
        }
        for (Entity e : GetMembers(root))
            if (e.HasComponent<PrefabLinkComponent>())
            {
                e.RemoveComponent<PrefabLinkComponent>();
                count++;
            }
        scene.SetPrefabSource(root, "");
        for (Entity nested : nestedRoots)
            if (nested.IsValid())
                count += Unpack(nested, true);
        return count;
    }

    bool PrefabUtility::SyncPrefabFile(PrefabDatabase& db, std::string_view relPath, std::vector<FileEdit>* edits)
    {
        const std::string path = PrefabDatabase::NormalizePath(relPath);
        const PrefabDatabase::Template* t = db.Get(path);
        if (!t || !t->Readable)
            return false;
        if (t->Deps.empty())
            return true; // nothing nested: nothing to bring up to date
        json file = json::parse(t->Text, nullptr, false);
        if (file.is_discarded() || !file.is_object())
            return false;
        // The file as written, read without bringing anything up to date - compared entity by
        // entity with the template (both through the same writer): an unchanged entity keeps its text.
        Scene plain;
        SceneSerializer::EntitySetReadOptions options;
        options.GenerateNewUUIDs = false;
        options.KeepPrefabLinks = true;
        SceneSerializer::DeserializeEntities(plain, t->Text, options);
        std::vector<Entity> healedOrder, plainOrder;
        for (uint64_t id : t->Order)
            healedOrder.push_back(t->Entities->GetEntityByUUID(UUID(id)));
        for (Entity e : plain.GetEntitiesInHierarchyOrder())
            plainOrder.push_back(e);
        SceneSerializer::EntitySetWriteOptions write;
        write.HiddenLocked = false;
        json healed = json::parse(SceneSerializer::SerializeEntities(*t->Entities, healedOrder, write), nullptr, false);
        json unhealed = json::parse(SceneSerializer::SerializeEntities(plain, plainOrder, write), nullptr, false);
        if (healed.is_discarded() || unhealed.is_discarded())
            return false;
        std::unordered_map<uint64_t, const json*> before, original;
        for (const json& e : unhealed["Entities"])
            before[e.value("Entity", (uint64_t)0)] = &e;
        for (const json& e : file["Entities"])
            if (e.is_object())
                original[e.value("Entity", (uint64_t)0)] = &e;
        json list = json::array();
        bool changed = healed["Entities"].size() != unhealed["Entities"].size();
        for (const json& e : healed["Entities"])
        {
            const uint64_t id = e.value("Entity", (uint64_t)0);
            auto b = before.find(id);
            auto o = original.find(id);
            if (b != before.end() && o != original.end() && PO::ValuesEqual(*b->second, e))
                list.push_back(*o->second);
            else
            {
                list.push_back(e);
                changed = true;
            }
        }
        if (!changed)
            return true;
        file["Entities"] = std::move(list);
        const bool ok = db.Write(path, DumpFile(file), edits);
        if (ok)
            ZM_CORE_INFO("Prefab yangilandi (ichidagi prefab o'zgardi): {}", path);
        return ok;
    }

}
