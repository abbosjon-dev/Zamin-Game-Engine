#include "Zamin/Scene/PrefabOverridesJson.h"

#include <algorithm>
#include <cmath>

using json = nlohmann::json;

namespace Zamin::PrefabOverrides {

    std::string EscapeSegment(std::string_view name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
        {
            if (c == '~')
                out += "~0";
            else if (c == '/')
                out += "~1";
            else
                out += c;
        }
        return out;
    }

    std::string UnescapeSegment(std::string_view segment)
    {
        std::string out;
        out.reserve(segment.size());
        for (size_t i = 0; i < segment.size(); i++)
        {
            if (segment[i] == '~' && i + 1 < segment.size() && (segment[i + 1] == '0' || segment[i + 1] == '1'))
            {
                out += segment[i + 1] == '0' ? '~' : '/';
                i++;
            }
            else
                out += segment[i];
        }
        return out;
    }

    std::vector<std::string> SplitPath(std::string_view path)
    {
        std::vector<std::string> parts;
        if (path.empty())
            return parts;
        size_t start = 0;
        while (true)
        {
            const size_t slash = path.find('/', start);
            parts.push_back(UnescapeSegment(path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start)));
            if (slash == std::string_view::npos)
                break;
            start = slash + 1;
        }
        return parts;
    }

    std::string JoinPath(const std::vector<std::string>& names)
    {
        std::string out;
        for (size_t i = 0; i < names.size(); i++)
        {
            if (i)
                out += '/';
            out += EscapeSegment(names[i]);
        }
        return out;
    }

    std::string_view RootKey(std::string_view path)
    {
        const size_t slash = path.find('/');
        return slash == std::string_view::npos ? path : path.substr(0, slash);
    }

    bool IsUnder(std::string_view path, std::string_view prefix)
    {
        if (!path.starts_with(prefix))
            return false;
        return path.size() == prefix.size() || path[prefix.size()] == '/';
    }

    bool IsInstanceOwned(bool isRoot, std::string_view path)
    {
        if (!isRoot)
            return false;
        for (std::string_view owned : { "Tag", "Parent", "Transform/Translation", "Transform/Rotation", "UITransform/Position" })
            if (IsUnder(path, owned))
                return true;
        return false;
    }

    uint64_t MixUUID(uint64_t root, uint64_t local)
    {
        uint64_t z = root ^ (local * 0x9E3779B97F4A7C15ull);
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return z | 1;
    }

    // ------------------------------------------------------------------ JSON
    const json* Get(const json& entity, std::string_view path)
    {
        const json* node = &entity;
        for (const std::string& name : SplitPath(path))
        {
            if (!node->is_object())
                return nullptr;
            auto it = node->find(name);
            if (it == node->end())
                return nullptr;
            node = &*it;
        }
        return node;
    }

    static std::string SidecarKey(const std::string& key) { return key + "UUID"; }

    void Set(json& entity, std::string_view path, const json& value, const json* sidecarFrom)
    {
        const std::vector<std::string> names = SplitPath(path);
        if (names.empty())
            return;
        json* node = &entity;
        for (size_t i = 0; i + 1 < names.size(); i++)
        {
            if (!node->is_object())
                *node = json::object();
            node = &(*node)[names[i]];
        }
        if (!node->is_object())
            *node = json::object();
        const std::string& key = names.back();
        (*node)[key] = value;
        // An asset path's UUID goes with it.
        if (value.is_string())
        {
            const std::string sidecar = SidecarKey(key);
            if (sidecarFrom && sidecarFrom->is_object() && sidecarFrom->contains(sidecar))
                (*node)[sidecar] = (*sidecarFrom)[sidecar];
            else
                node->erase(sidecar);
        }
    }

    bool Erase(json& entity, std::string_view path)
    {
        const std::vector<std::string> names = SplitPath(path);
        if (names.empty())
            return false;
        json* node = &entity;
        for (size_t i = 0; i + 1 < names.size(); i++)
        {
            if (!node->is_object())
                return false;
            auto it = node->find(names[i]);
            if (it == node->end())
                return false;
            node = &*it;
        }
        if (!node->is_object())
            return false;
        const bool erased = node->erase(names.back()) > 0;
        node->erase(SidecarKey(names.back()));
        return erased;
    }

    bool ValuesEqual(const json& a, const json& b)
    {
        if (a.is_number() && b.is_number())
        {
            if (a.is_number_float() || b.is_number_float())
                return (float)a.get<double>() == (float)b.get<double>();
            if (a.is_number_unsigned() && b.is_number_unsigned())
                return a.get<uint64_t>() == b.get<uint64_t>();
            return a.get<int64_t>() == b.get<int64_t>();
        }
        if (a.type() != b.type())
            return false;
        if (a.is_array())
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
                if (!ValuesEqual(a[i], b[i]))
                    return false;
            return true;
        }
        if (a.is_object())
        {
            if (a.size() != b.size())
                return false;
            for (auto it = a.begin(); it != a.end(); ++it)
            {
                auto other = b.find(it.key());
                if (other == b.end() || !ValuesEqual(it.value(), *other))
                    return false;
            }
            return true;
        }
        return a == b;
    }

    // "<key>UUID" next to an asset path (string) "<key>": it follows the path and is no value of its own.
    static bool IsSidecar(const json& a, const json& b, const std::string& key)
    {
        if (key.size() <= 4 || !key.ends_with("UUID"))
            return false;
        const std::string base = key.substr(0, key.size() - 4);
        auto isPath = [&](const json& j) { auto it = j.find(base); return it != j.end() && it->is_string(); };
        return isPath(a) || isPath(b);
    }

    static bool IsScriptField(std::string_view path)
    {
        // "NativeScript/Fields/<name>": one field ({Type, Value} is one value).
        if (!path.starts_with("NativeScript/Fields/"))
            return false;
        return path.find('/', std::string_view("NativeScript/Fields/").size()) == std::string_view::npos;
    }

    static void DiffValue(const json& a, const json& b, const std::string& path, bool isRoot, std::vector<std::string>& paths)
    {
        if (IsInstanceOwned(isRoot, path))
            return;
        if (a.is_object() && b.is_object() && !IsScriptField(path))
        {
            for (auto it = a.begin(); it != a.end(); ++it)
            {
                const std::string& key = it.key();
                if (IsSidecar(a, b, key))
                    continue;
                const std::string sub = path + "/" + EscapeSegment(key);
                auto other = b.find(key);
                if (other == b.end())
                {
                    if (!IsInstanceOwned(isRoot, sub))
                        paths.push_back(sub);
                }
                else
                    DiffValue(it.value(), *other, sub, isRoot, paths);
            }
            for (auto it = b.begin(); it != b.end(); ++it)
            {
                if (a.contains(it.key()) || IsSidecar(a, b, it.key()))
                    continue;
                const std::string sub = path + "/" + EscapeSegment(it.key());
                if (!IsInstanceOwned(isRoot, sub))
                    paths.push_back(sub);
            }
            return;
        }
        if (!ValuesEqual(a, b))
            paths.push_back(path);
    }

    static bool IsEntityValueKey(const std::string& key) { return key == "Entity" || key == "Editor" || key == "ChildOrder"; }

    void DiffPaths(const json& clean, const json& current, bool isRoot, std::vector<std::string>& paths, std::vector<std::string>& removed)
    {
        paths.clear();
        removed.clear();
        if (!clean.is_object() || !current.is_object())
            return;
        auto active = [](const json& e) {
            auto it = e.find("Active");
            return it == e.end() || !it->is_boolean() || it->get<bool>();
        };
        if (active(clean) != active(current))
            paths.push_back("Active");
        auto visit = [&](const std::string& key) {
            if (IsEntityValueKey(key) || key == "Active")
                return;
            auto a = clean.find(key);
            auto b = current.find(key);
            if (key == "Tag" || key == "Parent")
            {
                if (IsInstanceOwned(isRoot, key))
                    return;
                const bool same = (a == clean.end()) == (b == current.end()) && (a == clean.end() || ValuesEqual(*a, *b));
                if (!same)
                    paths.push_back(key);
                return;
            }
            if (a == clean.end())
                paths.push_back(EscapeSegment(key));
            else if (b == current.end())
                removed.push_back(key);
            else
                DiffValue(*a, *b, EscapeSegment(key), isRoot, paths);
        };
        for (auto it = clean.begin(); it != clean.end(); ++it)
            visit(it.key());
        for (auto it = current.begin(); it != current.end(); ++it)
            if (!clean.contains(it.key()))
                visit(it.key());
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        std::sort(removed.begin(), removed.end());
    }

    static std::string ParentPath(std::string_view path)
    {
        const size_t slash = path.rfind('/');
        return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
    }

    json Merge(const json& clean, const json& current, const std::vector<std::string>& overrides, const std::vector<std::string>& removed,
               bool isRoot, std::vector<std::string>* orphans)
    {
        json result = clean.is_object() ? clean : json::object();
        auto copyFromCurrent = [&](std::string_view path) {
            const std::string parent = ParentPath(path);
            const json* value = Get(current, path);
            const json* sidecars = parent.empty() ? &current : Get(current, parent);
            if (value)
                Set(result, path, *value, sidecars);
            else
                Erase(result, path);
        };
        for (const std::string& path : overrides)
        {
            if (path == "ChildOrder")
                continue; // the order of the children, not a value (PrefabUtility)
            if (IsInstanceOwned(isRoot, path))
                continue;
            const std::string parent = ParentPath(path);
            if (!parent.empty())
            {
                const json* inClean = Get(result, parent);
                if (!inClean || !inClean->is_object())
                {
                    if (orphans)
                        orphans->push_back(path);
                    continue;
                }
            }
            copyFromCurrent(path);
        }
        for (const std::string& key : removed)
            result.erase(key);
        // The entity's own data and what belongs to the instance.
        for (const char* key : { "Entity", "Editor" })
        {
            if (auto it = current.find(key); it != current.end())
                result[key] = *it;
            else
                result.erase(key);
        }
        if (isRoot)
        {
            for (const char* key : { "Tag", "Parent" })
            {
                if (auto it = current.find(key); it != current.end())
                    result[key] = *it;
                else
                    result.erase(key);
            }
            for (std::string_view path : { "Transform/Translation", "Transform/Rotation", "UITransform/Position" })
                if (Get(result, ParentPath(path)) && Get(current, path))
                    copyFromCurrent(path);
        }
        return result;
    }

}
