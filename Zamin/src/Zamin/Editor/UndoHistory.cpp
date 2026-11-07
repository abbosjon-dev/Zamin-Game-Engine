#include "Zamin/Editor/UndoHistory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {

    // ================================================================== history
    UndoHistory::UndoHistory(size_t maxEdits, size_t maxSelections)
        : m_MaxEdits(std::max<size_t>(maxEdits, 1)), m_MaxSelections(maxSelections)
    {
    }

    void UndoHistory::Reset(std::string scene, UndoSelection selection, std::string name, double time, bool saved)
    {
        m_Entries.clear();
        Entry first;
        first.Name = std::move(name);
        first.Kind = UndoEntryKind::Initial;
        first.Time = time;
        first.Scene = std::make_shared<const std::string>(std::move(scene));
        first.Selection = std::move(selection);
        m_Entries.push_back(std::move(first));
        m_Current = 0;
        m_Saved = saved ? m_Entries.front().Scene : nullptr;
    }

    bool UndoHistory::CommitEdit(std::string scene, UndoSelection selection, std::string name, double time)
    {
        return CommitEdit(std::move(scene), std::move(selection), std::move(name), time, {});
    }

    void UndoHistory::ReplaceCurrentScene(std::string scene)
    {
        if (m_Entries.empty())
            return;
        const std::shared_ptr<const std::string> old = m_Entries[m_Current].Scene;
        if (old && *old == scene)
            return;
        auto updated = std::make_shared<const std::string>(std::move(scene));
        // Selection steps share the scene text of the state they follow.
        for (Entry& e : m_Entries)
            if (e.Scene == old)
                e.Scene = updated;
    }

    bool UndoHistory::CommitEdit(std::string scene, UndoSelection selection, std::string name, double time, std::vector<FileEdit> files)
    {
        if (m_Entries.empty())
        {
            Reset(std::move(scene), std::move(selection), std::move(name), time, false);
            return true;
        }
        if (files.empty() && GetCurrent().Scene && *GetCurrent().Scene == scene)
            return false;
        m_Entries.resize(m_Current + 1);
        Entry entry;
        entry.Name = std::move(name);
        entry.Kind = UndoEntryKind::Edit;
        entry.Time = time;
        entry.Scene = std::make_shared<const std::string>(std::move(scene));
        entry.Selection = std::move(selection);
        entry.Files = std::move(files);
        m_Entries.push_back(std::move(entry));
        m_Current = m_Entries.size() - 1;
        Trim();
        return true;
    }

    bool UndoHistory::CommitSelection(UndoSelection selection, std::string name, double time)
    {
        if (m_Entries.empty() || GetCurrent().Selection == selection)
            return false;
        m_Entries.resize(m_Current + 1);
        Entry entry;
        entry.Name = std::move(name);
        entry.Kind = UndoEntryKind::Selection;
        entry.Time = time;
        entry.Scene = GetCurrent().Scene;
        entry.Selection = std::move(selection);
        m_Entries.push_back(std::move(entry));
        m_Current = m_Entries.size() - 1;
        Trim();
        return true;
    }

    void UndoHistory::SetCurrentSelection(UndoSelection selection)
    {
        if (!m_Entries.empty())
            m_Entries[m_Current].Selection = std::move(selection);
    }

    void UndoHistory::Trim()
    {
        // The oldest steps go first; the current state (the newest after a commit) always stays.
        auto count = [&](UndoEntryKind kind) {
            return (size_t)std::count_if(m_Entries.begin(), m_Entries.end(), [&](const Entry& e) { return e.Kind == kind; });
        };
        size_t edits = count(UndoEntryKind::Edit), selections = count(UndoEntryKind::Selection);
        size_t drop = 0;
        while (drop < m_Current && (edits > m_MaxEdits || selections > m_MaxSelections))
        {
            // Dropping the first state makes the next one the start.
            const Entry& next = m_Entries[drop + 1];
            if (next.Kind == UndoEntryKind::Edit) edits--;
            else if (next.Kind == UndoEntryKind::Selection) selections--;
            drop++;
        }
        if (drop == 0)
            return;
        m_Entries.erase(m_Entries.begin(), m_Entries.begin() + (std::ptrdiff_t)drop);
        m_Current -= drop;
        m_Entries.front().Kind = UndoEntryKind::Initial;
    }

    UndoHistory::Move UndoHistory::JumpTo(size_t index)
    {
        if (index >= m_Entries.size() || index == m_Current)
            return {};
        const Entry& from = m_Entries[m_Current];
        Move move;
        // The files of the steps passed over: back from the current one down to the one after
        // `index`, or forward from the one after the current one up to `index`.
        if (index < m_Current)
        {
            for (size_t i = m_Current; i > index; i--)
                for (auto it = m_Entries[i].Files.rbegin(); it != m_Entries[i].Files.rend(); ++it)
                    move.Files.push_back({ &*it, true });
        }
        else
        {
            for (size_t i = m_Current + 1; i <= index; i++)
                for (const FileEdit& edit : m_Entries[i].Files)
                    move.Files.push_back({ &edit, false });
        }
        m_Current = index;
        const Entry& to = m_Entries[m_Current];
        move.Target = &to;
        move.SceneChanged = from.Scene != to.Scene && (!from.Scene || !to.Scene || *from.Scene != *to.Scene);
        return move;
    }

    UndoHistory::Move UndoHistory::Undo()
    {
        return CanUndo() ? JumpTo(m_Current - 1) : Move{};
    }

    UndoHistory::Move UndoHistory::Redo()
    {
        return CanRedo() ? JumpTo(m_Current + 1) : Move{};
    }

    const std::string& UndoHistory::GetUndoName() const
    {
        static const std::string s_None;
        return CanUndo() ? m_Entries[m_Current].Name : s_None;
    }

    const std::string& UndoHistory::GetRedoName() const
    {
        static const std::string s_None;
        return CanRedo() ? m_Entries[m_Current + 1].Name : s_None;
    }

    const UndoHistory::Entry& UndoHistory::GetCurrent() const
    {
        static const Entry s_Empty;
        return m_Entries.empty() ? s_Empty : m_Entries[m_Current];
    }

    size_t UndoHistory::CountEdits() const
    {
        return (size_t)std::count_if(m_Entries.begin(), m_Entries.end(), [](const Entry& e) { return e.Kind == UndoEntryKind::Edit; });
    }

    size_t UndoHistory::CountSelections() const
    {
        return (size_t)std::count_if(m_Entries.begin(), m_Entries.end(), [](const Entry& e) { return e.Kind == UndoEntryKind::Selection; });
    }

    size_t UndoHistory::GetMemoryUsage() const
    {
        std::unordered_set<const std::string*> seen;
        size_t bytes = 0;
        for (const Entry& e : m_Entries)
            if (e.Scene && seen.insert(e.Scene.get()).second)
                bytes += e.Scene->size();
        return bytes;
    }

    void UndoHistory::MarkSaved()
    {
        m_Saved = m_Entries.empty() ? nullptr : m_Entries[m_Current].Scene;
    }

    void UndoHistory::ClearSaved() { m_Saved.reset(); }

    bool UndoHistory::IsAtSavedState() const
    {
        if (!m_Saved || m_Entries.empty())
            return false;
        const auto& scene = m_Entries[m_Current].Scene;
        return scene == m_Saved || (scene && *scene == *m_Saved);
    }

    bool UndoHistory::IsSavedState(size_t index) const
    {
        if (!m_Saved || index >= m_Entries.size())
            return false;
        const auto& scene = m_Entries[index].Scene;
        return scene == m_Saved || (scene && *scene == *m_Saved);
    }

    void UndoHistory::ClearHistory(std::string name, double time)
    {
        if (m_Entries.empty())
            return;
        Entry current = m_Entries[m_Current];
        current.Name = std::move(name);
        current.Kind = UndoEntryKind::Initial;
        current.Time = time;
        m_Entries.clear();
        m_Entries.push_back(std::move(current));
        m_Current = 0;
    }

    void UndoHistory::RewriteScenes(const std::function<void(std::string&)>& rewrite)
    {
        std::unordered_map<const std::string*, std::shared_ptr<const std::string>> rewritten;
        auto get = [&](const std::shared_ptr<const std::string>& scene) -> std::shared_ptr<const std::string> {
            if (!scene)
                return scene;
            auto it = rewritten.find(scene.get());
            if (it != rewritten.end())
                return it->second;
            std::string text = *scene;
            rewrite(text);
            auto updated = std::make_shared<const std::string>(std::move(text));
            rewritten[scene.get()] = updated;
            return updated;
        };
        for (Entry& e : m_Entries)
        {
            e.Scene = get(e.Scene);
            for (FileEdit& file : e.Files)
            {
                if (file.Before)
                    rewrite(*file.Before);
                rewrite(file.After);
            }
        }
        m_Saved = get(m_Saved);
    }

    // ================================================================== snapshot diff
    namespace {
        using Members = std::vector<std::pair<std::string_view, std::string_view>>;

        size_t SkipSpace(std::string_view t, size_t i)
        {
            while (i < t.size() && std::isspace((unsigned char)t[i]))
                i++;
            return i;
        }

        // One past the end of the string starting at t[i] == '"' (npos: unterminated).
        size_t SkipString(std::string_view t, size_t i)
        {
            for (i++; i < t.size(); i++)
            {
                if (t[i] == '\\') i++;
                else if (t[i] == '"') return i + 1;
            }
            return std::string_view::npos;
        }

        // One past the end of the JSON value starting at t[i] (npos: malformed).
        size_t SkipValue(std::string_view t, size_t i)
        {
            if (i >= t.size())
                return std::string_view::npos;
            const char c = t[i];
            if (c == '"')
                return SkipString(t, i);
            if (c == '{' || c == '[')
            {
                int depth = 0;
                for (; i < t.size(); i++)
                {
                    const char d = t[i];
                    if (d == '"')
                    {
                        i = SkipString(t, i);
                        if (i == std::string_view::npos) return i;
                        i--;
                    }
                    else if (d == '{' || d == '[') depth++;
                    else if (d == '}' || d == ']')
                    {
                        if (--depth == 0) return i + 1;
                    }
                }
                return std::string_view::npos;
            }
            // number / true / false / null
            while (i < t.size() && t[i] != ',' && t[i] != '}' && t[i] != ']' && !std::isspace((unsigned char)t[i]))
                i++;
            return i;
        }

        // The members of the object `t` (keys without their quotes; values as text).
        bool SplitObject(std::string_view t, Members& out)
        {
            out.clear();
            size_t i = SkipSpace(t, 0);
            if (i >= t.size() || t[i] != '{')
                return false;
            i = SkipSpace(t, i + 1);
            if (i < t.size() && t[i] == '}')
                return true;
            while (i < t.size())
            {
                if (t[i] != '"')
                    return false;
                const size_t keyEnd = SkipString(t, i);
                if (keyEnd == std::string_view::npos)
                    return false;
                const std::string_view key = t.substr(i + 1, keyEnd - i - 2);
                i = SkipSpace(t, keyEnd);
                if (i >= t.size() || t[i] != ':')
                    return false;
                i = SkipSpace(t, i + 1);
                const size_t valueEnd = SkipValue(t, i);
                if (valueEnd == std::string_view::npos)
                    return false;
                out.emplace_back(key, t.substr(i, valueEnd - i));
                i = SkipSpace(t, valueEnd);
                if (i < t.size() && t[i] == ',')
                {
                    i = SkipSpace(t, i + 1);
                    continue;
                }
                return i < t.size() && t[i] == '}';
            }
            return false;
        }

        bool SplitArray(std::string_view t, std::vector<std::string_view>& out)
        {
            out.clear();
            size_t i = SkipSpace(t, 0);
            if (i >= t.size() || t[i] != '[')
                return false;
            i = SkipSpace(t, i + 1);
            if (i < t.size() && t[i] == ']')
                return true;
            while (i < t.size())
            {
                const size_t end = SkipValue(t, i);
                if (end == std::string_view::npos)
                    return false;
                out.push_back(t.substr(i, end - i));
                i = SkipSpace(t, end);
                if (i < t.size() && t[i] == ',')
                {
                    i = SkipSpace(t, i + 1);
                    continue;
                }
                return i < t.size() && t[i] == ']';
            }
            return false;
        }

        std::string_view Find(const Members& members, std::string_view key)
        {
            for (const auto& [k, v] : members)
                if (k == key)
                    return v;
            return {};
        }

        // A JSON string value's text ("\"Player\"") as the string.
        std::string DecodeString(std::string_view value)
        {
            if (value.size() < 2 || value.front() != '"')
                return std::string(value);
            if (value.find('\\') == std::string_view::npos)
                return std::string(value.substr(1, value.size() - 2));
            nlohmann::json j = nlohmann::json::parse(value, nullptr, false);
            return j.is_string() ? j.get<std::string>() : std::string(value);
        }

        struct EntityText
        {
            std::string_view Text;
            std::string Tag;
            Members Fields;
        };

        bool ReadEntities(std::string_view entities, std::vector<std::pair<std::string_view, EntityText>>& out)
        {
            std::vector<std::string_view> items;
            if (!SplitArray(entities, items))
                return false;
            out.clear();
            out.reserve(items.size());
            for (std::string_view item : items)
            {
                EntityText e;
                e.Text = item;
                if (!SplitObject(item, e.Fields))
                    return false;
                e.Tag = DecodeString(Find(e.Fields, "Tag"));
                out.emplace_back(Find(e.Fields, "Entity"), std::move(e));
            }
            return true;
        }

        // The keys of `a` and `b` that differ: only in `b`, only in `a`, in both with other values.
        void CompareMembers(const Members& a, const Members& b, std::vector<std::string>& added, std::vector<std::string>& removed,
                            std::vector<std::string>& changed)
        {
            for (const auto& [key, value] : b)
            {
                const std::string_view old = Find(a, key);
                if (old.empty()) added.emplace_back(key);
                else if (old != value) changed.emplace_back(key);
            }
            for (const auto& [key, value] : a)
                if (Find(b, key).empty())
                    removed.emplace_back(key);
        }

        // The one field of two objects that differs ("" when none or several).
        std::string OnlyChangedField(std::string_view a, std::string_view b)
        {
            Members ma, mb;
            if (!SplitObject(a, ma) || !SplitObject(b, mb))
                return {};
            std::vector<std::string> added, removed, changed;
            CompareMembers(ma, mb, added, removed, changed);
            if (added.size() + removed.size() + changed.size() != 1)
                return {};
            return !changed.empty() ? changed.front() : !added.empty() ? added.front() : removed.front();
        }

        std::string WithMore(const std::vector<std::string>& names)
        {
            if (names.empty())
                return {};
            const std::string& first = names.front().empty() ? std::string("?") : names.front();
            return names.size() > 1 ? std::format("{} (+{})", first, names.size() - 1) : first;
        }

        bool OnlyKeys(const SceneChange::EntityChange& c, std::initializer_list<std::string_view> keys)
        {
            auto in = [&](const std::string& k) { return std::find(keys.begin(), keys.end(), k) != keys.end(); };
            return std::all_of(c.Added.begin(), c.Added.end(), in) && std::all_of(c.Removed.begin(), c.Removed.end(), in) &&
                   std::all_of(c.Changed.begin(), c.Changed.end(), in);
        }

        bool Touches(const SceneChange::EntityChange& c, std::string_view key)
        {
            auto has = [&](const std::vector<std::string>& v) { return std::find(v.begin(), v.end(), key) != v.end(); };
            return has(c.Added) || has(c.Removed) || has(c.Changed);
        }
    }

    SceneChange DiffSceneSnapshots(std::string_view before, std::string_view after)
    {
        SceneChange change;
        Members rootA, rootB;
        if (!SplitObject(before, rootA) || !SplitObject(after, rootB))
            return change;

        // Scene-level keys (the format version is not a change).
        {
            std::vector<std::string> added, removed, changed;
            CompareMembers(rootA, rootB, added, removed, changed);
            for (auto* list : { &added, &removed, &changed })
                for (std::string& key : *list)
                    if (key != "Entities" && key != "Version")
                        change.SceneKeys.push_back(std::move(key));
            if (change.SceneKeys.size() == 1 && change.SceneKeys.front() == "Settings")
                change.SceneField = OnlyChangedField(Find(rootA, "Settings"), Find(rootB, "Settings"));
        }

        const std::string_view entitiesA = Find(rootA, "Entities"), entitiesB = Find(rootB, "Entities");
        if (entitiesA != entitiesB)
        {
            std::vector<std::pair<std::string_view, EntityText>> a, b;
            if ((!entitiesA.empty() && !ReadEntities(entitiesA, a)) || (!entitiesB.empty() && !ReadEntities(entitiesB, b)))
                return change;
            std::unordered_map<std::string_view, size_t> indexA;
            indexA.reserve(a.size());
            for (size_t i = 0; i < a.size(); i++)
                indexA[a[i].first] = i;
            std::unordered_set<std::string_view> seen;
            for (const auto& [id, entity] : b)
            {
                seen.insert(id);
                auto it = indexA.find(id);
                if (it == indexA.end())
                {
                    change.Added.push_back(entity.Tag);
                    continue;
                }
                const EntityText& old = a[it->second].second;
                if (old.Text == entity.Text)
                    continue;
                SceneChange::EntityChange c;
                c.Tag = entity.Tag;
                c.OldTag = old.Tag;
                CompareMembers(old.Fields, entity.Fields, c.Added, c.Removed, c.Changed);
                if (c.Added.empty() && c.Removed.empty() && c.Changed.size() == 1)
                    c.Field = OnlyChangedField(Find(old.Fields, c.Changed.front()), Find(entity.Fields, c.Changed.front()));
                if (!c.Added.empty() || !c.Removed.empty() || !c.Changed.empty())
                    change.Changed.push_back(std::move(c));
            }
            for (const auto& [id, entity] : a)
                if (!seen.contains(id))
                    change.Removed.push_back(entity.Tag);
        }
        change.Valid = true;
        return change;
    }

    std::string DescribeSceneChange(const SceneChange& change)
    {
        if (!change.Valid)
            return "Sahna o'zgarishi";
        if (!change.Added.empty() && change.Removed.empty())
            return "Qo'shildi: " + WithMore(change.Added);
        if (!change.Removed.empty() && change.Added.empty())
            return "O'chirildi: " + WithMore(change.Removed);
        if (!change.Added.empty())
            return std::format("Almashtirildi: {} ta qo'shildi, {} ta o'chirildi", change.Added.size(), change.Removed.size());

        if (!change.Changed.empty())
        {
            const auto& all = change.Changed;
            // Parents / child order only: something moved in the hierarchy.
            if (std::all_of(all.begin(), all.end(), [](const auto& c) { return OnlyKeys(c, { "Parent", "ChildOrder" }); }))
            {
                std::vector<std::string> moved;
                for (const auto& c : all)
                    if (Touches(c, "Parent"))
                        moved.push_back(c.Tag);
                return moved.empty() ? std::string("Tartib o'zgardi") : "Ierarxiya o'zgardi: " + WithMore(moved);
            }
            if (all.size() == 1)
            {
                const auto& c = all.front();
                if (OnlyKeys(c, { "Tag" }))
                    return std::format("Nomi o'zgardi: {} \xe2\x86\x92 {}", c.OldTag, c.Tag);
                if (OnlyKeys(c, { "Active" }))
                    return std::format("{}: {}", std::find(c.Added.begin(), c.Added.end(), "Active") != c.Added.end() ? "Nofaol qilindi" : "Faollashtirildi", c.Tag);
                if (OnlyKeys(c, { "Editor" }))
                    return std::format("Editor holati o'zgardi: {}", c.Tag);
                // A component added / removed (a new child's ChildOrder does not count).
                std::vector<std::string> added, removed, changed;
                for (const std::string& k : c.Added) if (k != "ChildOrder") added.push_back(k);
                for (const std::string& k : c.Removed) if (k != "ChildOrder") removed.push_back(k);
                for (const std::string& k : c.Changed) if (k != "ChildOrder") changed.push_back(k);
                if (added.size() == 1 && removed.empty() && changed.empty())
                    return std::format("Komponent qo'shildi: {} ({})", added.front(), c.Tag);
                if (removed.size() == 1 && added.empty() && changed.empty())
                    return std::format("Komponent olib tashlandi: {} ({})", removed.front(), c.Tag);
                if (changed.size() == 1 && added.empty() && removed.empty())
                    return c.Field.empty() ? std::format("{} o'zgardi ({})", changed.front(), c.Tag)
                                           : std::format("{}.{} o'zgardi ({})", changed.front(), c.Field, c.Tag);
                std::vector<std::string> keys = changed;
                keys.insert(keys.end(), added.begin(), added.end());
                keys.insert(keys.end(), removed.begin(), removed.end());
                if (keys.size() <= 3)
                {
                    std::string joined;
                    for (const std::string& k : keys)
                        joined += (joined.empty() ? "" : ", ") + k;
                    return std::format("{} o'zgardi ({})", joined, c.Tag);
                }
                return std::format("{} ta xususiyat o'zgardi ({})", keys.size(), c.Tag);
            }
            // Several entities: the same single key in all of them (a multi-selection edit / drag)?
            std::vector<std::string> tags;
            for (const auto& c : all)
                tags.push_back(c.Tag);
            const auto& first = all.front();
            const bool sameChanged = std::all_of(all.begin(), all.end(), [&](const auto& c) {
                return c.Added.empty() && c.Removed.empty() && c.Changed.size() == 1 && c.Changed.front() == first.Changed.front();
            });
            if (!first.Changed.empty() && sameChanged)
                return std::format("{} o'zgardi ({})", first.Changed.front(), WithMore(tags));
            const bool sameAdded = std::all_of(all.begin(), all.end(), [&](const auto& c) {
                return c.Removed.empty() && c.Added.size() == 1 && c.Added.front() == first.Added.front();
            });
            if (!first.Added.empty() && sameAdded)
                return std::format("Komponent qo'shildi: {} ({})", first.Added.front(), WithMore(tags));
            return std::format("{} ta obyekt o'zgardi", all.size());
        }

        if (!change.SceneKeys.empty())
        {
            if (change.SceneKeys.size() == 1)
            {
                const std::string& key = change.SceneKeys.front();
                if (key == "Settings")
                    return change.SceneField.empty() ? std::string("Sahna sozlamalari o'zgardi") : "Sahna sozlamalari: " + change.SceneField;
                if (key == "Metadata") return "Sahna ma'lumoti o'zgardi";
                if (key == "Scene") return "Sahna nomi o'zgardi";
                if (key == "RootOrder") return "Tartib o'zgardi";
            }
            return "Sahna sozlamalari o'zgardi";
        }
        return "O'zgarish";
    }

    std::string DescribeSceneChange(std::string_view before, std::string_view after)
    {
        return DescribeSceneChange(DiffSceneSnapshots(before, after));
    }

    std::string DescribeSelection(const std::vector<std::string>& names)
    {
        if (names.empty())
            return "Tanlov bekor qilindi";
        return "Tanlash: " + WithMore(names);
    }

}
