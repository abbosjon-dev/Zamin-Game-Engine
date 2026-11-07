#include "Zamin/Scene/SceneMigrationJson.h"
#include "Zamin/Core/Log.h"
#include "Zamin/UI/UILayout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <format>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Zamin {

    using json = nlohmann::json;

    // ------------------------------------------------------------------ the steps
    namespace {

        // A whole number written as text ("1234") as a number; other values stay. True when changed.
        bool NumberFromString(json& value)
        {
            if (!value.is_string())
                return false;
            const std::string& s = value.get_ref<const std::string&>();
            if (s.empty() || s.size() > 20 || !std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit((unsigned char)c); }))
                return false;
            try
            {
                const uint64_t number = std::stoull(s);
                value = number;
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        size_t NumbersFromStrings(json& list)
        {
            size_t changes = 0;
            if (list.is_array())
                for (json& item : list)
                    changes += NumberFromString(item) ? 1 : 0;
            return changes;
        }

        json* EntityList(json& root)
        {
            auto it = root.find("Entities");
            return it != root.end() && it->is_array() ? &*it : nullptr;
        }

        // v0 -> v1: files without a version (written by hand or by tools) in the form the first
        // engine wrote: entity IDs ("Entity", "Parent", "ChildOrder", "RootOrder") as numbers, the
        // display name under "Tag".
        size_t MigrateV0(json& root)
        {
            size_t changes = 0;
            if (auto it = root.find("RootOrder"); it != root.end())
                changes += NumbersFromStrings(*it);
            json* entities = EntityList(root);
            if (!entities)
                return changes;
            for (json& e : *entities)
            {
                if (!e.is_object())
                    continue;
                for (const char* key : { "Entity", "Parent" })
                    if (auto it = e.find(key); it != e.end())
                        changes += NumberFromString(*it) ? 1 : 0;
                if (auto it = e.find("ChildOrder"); it != e.end())
                    changes += NumbersFromStrings(*it);
                if (!e.contains("Tag"))
                    if (auto name = e.find("Name"); name != e.end() && name->is_string())
                    {
                        json value = *name;
                        e.erase(name);
                        e["Tag"] = std::move(value);
                        changes++;
                    }
            }
            return changes;
        }

        // v1 -> v2: the keys of features that were reworked, in their current form - Text "Centered"
        // as AlignH / AlignV (centred both ways; else UI text on the left in the middle, world text
        // on its first baseline), UITransform "Anchor" (0..8) as AnchorMin = AnchorMax at that point
        // with the pivot in the centre (the old position was the rect's centre from the anchor).
        size_t MigrateV1(json& root)
        {
            size_t changes = 0;
            json* entities = EntityList(root);
            if (!entities)
                return changes;
            for (json& e : *entities)
            {
                if (!e.is_object())
                    continue;
                if (auto text = e.find("Text"); text != e.end() && text->is_object())
                {
                    json& t = *text;
                    const bool hasCentered = t.contains("Centered");
                    if (!t.contains("AlignH") && !t.contains("AlignV"))
                    {
                        const auto centered = t.find("Centered");
                        const bool isCentered = centered != t.end() && centered->is_boolean() && centered->get<bool>();
                        t["AlignH"] = isCentered ? "Center" : "Left";
                        t["AlignV"] = isCentered || e.contains("UITransform") ? "Middle" : "Baseline";
                        changes++;
                    }
                    else if (hasCentered)
                        changes++;
                    t.erase("Centered");
                }
                if (auto ui = e.find("UITransform"); ui != e.end() && ui->is_object())
                {
                    json& u = *ui;
                    if (!u.contains("AnchorMin") && !u.contains("AnchorMax"))
                    {
                        int preset = 0;
                        if (auto anchor = u.find("Anchor"); anchor != u.end() && anchor->is_number())
                            preset = std::clamp(anchor->get<int>(), 0, 8);
                        const glm::vec2 point = UILayout::PresetPoint(preset);
                        u["AnchorMin"] = json::array({ point.x, point.y });
                        u["AnchorMax"] = json::array({ point.x, point.y });
                        u["Pivot"] = json::array({ 0.5f, 0.5f });
                        u.erase("Anchor");
                        changes++;
                    }
                    else if (u.contains("Anchor"))
                    {
                        u.erase("Anchor");
                        changes++;
                    }
                }
            }
            return changes;
        }

        // v2 -> v3: a sub-scene's file under "Path" / "PathUUID" (was "Scene" / "SceneUUID").
        // "Scene" is a name key (the scene's own name at the top of the file): the tools that
        // rewrite asset paths when files move (AssetFiles::UpdateReferences, the open scene's
        // remap) and the asset usage index passed the sub-scene's file by.
        size_t MigrateV2(json& root)
        {
            size_t changes = 0;
            json* entities = EntityList(root);
            if (!entities)
                return changes;
            for (json& e : *entities)
            {
                if (!e.is_object())
                    continue;
                auto sub = e.find("SubScene");
                if (sub == e.end() || !sub->is_object())
                    continue;
                json& s = *sub;
                for (const auto& [from, to] : { std::pair<const char*, const char*>{ "Scene", "Path" }, { "SceneUUID", "PathUUID" } })
                {
                    auto it = s.find(from);
                    if (it == s.end())
                        continue;
                    json value = std::move(*it);
                    s.erase(it);
                    if (!s.contains(to))
                        s[to] = std::move(value);
                    changes++;
                }
            }
            return changes;
        }

        // v3 -> v4: the gravity only when the scene has its own. The engine's value - the default every
        // file had - goes: such a scene follows the project's gravity (the same one without a "Physics"
        // section in the project).
        size_t MigrateV3(json& root)
        {
            size_t changes = 0;
            auto settings = root.find("Settings");
            if (settings == root.end() || !settings->is_object())
                return changes;
            auto isDefault = [](const json& value, std::initializer_list<float> engine) {
                if (!value.is_array() || value.size() != engine.size())
                    return false;
                size_t i = 0;
                for (float e : engine)
                {
                    const json& c = value[i++];
                    if (!c.is_number() || std::abs((float)c.get<double>() - e) > 1e-5f)
                        return false;
                }
                return true;
            };
            if (auto g = settings->find("Gravity"); g != settings->end() && isDefault(*g, { 0.0f, -9.8f }))
            {
                settings->erase(g);
                changes++;
            }
            if (auto g = settings->find("Gravity3D"); g != settings->end() && isDefault(*g, { 0.0f, -9.81f, 0.0f }))
            {
                settings->erase(g);
                changes++;
            }
            return changes;
        }

        // v4 -> v5: prefab instance links (an instance's entities linked to their prefab's:
        // "PrefabEntity", "Overrides" ... under "Editor") - nothing to change in an older file but the
        // prefab path of an instance root, written with '/' (a path typed on Windows) so that it
        // matches the prefab's path everywhere (links, moves, the reference index).
        size_t MigrateV4(json& root)
        {
            size_t changes = 0;
            json* entities = EntityList(root);
            if (!entities)
                return changes;
            for (json& e : *entities)
            {
                if (!e.is_object())
                    continue;
                auto editor = e.find("Editor");
                if (editor == e.end() || !editor->is_object())
                    continue;
                auto prefab = editor->find("Prefab");
                if (prefab == editor->end() || !prefab->is_string())
                    continue;
                std::string& path = prefab->get_ref<std::string&>();
                if (path.find('\\') == std::string::npos)
                    continue;
                std::replace(path.begin(), path.end(), '\\', '/');
                changes++;
            }
            return changes;
        }

        struct StepFunction
        {
            SceneMigrationStep Step;
            size_t (*Apply)(json& root);
        };

        const std::vector<StepFunction>& Steps()
        {
            static const std::vector<StepFunction> s_Steps = {
                { { 0, 1, "Versiyasiz fayl: obyekt ID'lari raqamga, \"Name\" -> \"Tag\"" }, &MigrateV0 },
                { { 1, 2, "Eski maydonlar: Text \"Centered\" -> AlignH/AlignV, UITransform \"Anchor\" -> AnchorMin/AnchorMax/Pivot; "
                          "sahna metadata (\"Metadata\")" }, &MigrateV1 },
                { { 2, 3, "Sub-sahna fayli: SubScene \"Scene\" -> \"Path\" (fayl ko'chirilganda havola yangilanadi)" }, &MigrateV2 },
                { { 3, 4, "Gravitatsiya: dvigatelning standart qiymati olib tashlandi (sahna loyiha gravitatsiyasiga ergashadi)" }, &MigrateV3 },
                { { 4, 5, "Prefab bog'lanishlari: Editor.Prefab yo'li '/' bilan; nusxa bog'lanishlari (PrefabEntity/Overrides) qo'shildi" }, &MigrateV4 },
            };
            return s_Steps;
        }

    }

    // ------------------------------------------------------------------ the chain
    int SceneMigrationJson::ReadVersion(const json& root)
    {
        auto it = root.find("Version");
        if (it == root.end())
            return 0;
        if (it->is_number_unsigned())
            return (int)std::min<uint64_t>(it->get<uint64_t>(), 1000000);
        if (it->is_number_integer())
            return 0; // negative
        if (it->is_number_float())
        {
            const double v = it->get<double>();
            return v >= 0.0 && v <= 1000000.0 && v == (double)(int)v ? (int)v : 0;
        }
        return 0;
    }

    SceneMigrationResult SceneMigrationJson::Migrate(json& root)
    {
        SceneMigrationResult result;
        result.FileVersion = result.Version = ReadVersion(root);
        if (result.FileVersion >= SceneMigration::CurrentVersion)
            return result;
        for (const StepFunction& step : Steps())
        {
            if (step.Step.From < result.Version)
                continue;
            if (step.Step.From != result.Version)
                break; // a gap in the chain (cannot happen: the steps are contiguous)
            SceneMigrationStep applied = step.Step;
            applied.Changes = step.Apply(root);
            result.Version = applied.To;
            result.Steps.push_back(std::move(applied));
        }
        root["Version"] = result.Version;
        return result;
    }

    // ------------------------------------------------------------------ result
    bool SceneMigrationResult::IsNewer() const
    {
        return FileVersion > SceneMigration::CurrentVersion;
    }

    size_t SceneMigrationResult::GetChangeCount() const
    {
        size_t count = 0;
        for (const SceneMigrationStep& step : Steps)
            count += step.Changes;
        return count;
    }

    std::string SceneMigrationResult::Summary() const
    {
        if (!Migrated())
            return {};
        return std::format("v{} -> v{} ({} qadam, {} ta o'zgarish)", FileVersion, Version, Steps.size(), GetChangeCount());
    }

    // ------------------------------------------------------------------ SceneMigration
    const std::vector<SceneMigrationStep>& SceneMigration::GetChain()
    {
        static const std::vector<SceneMigrationStep> s_Chain = [] {
            std::vector<SceneMigrationStep> chain;
            for (const StepFunction& step : Steps())
                chain.push_back(step.Step);
            return chain;
        }();
        return s_Chain;
    }

    // A scene file, or an entity set (a prefab file, copied entities: "ZaminEntities").
    static bool IsVersionedRoot(const json& root)
    {
        return root.is_object() && (root.contains("Scene") || root.contains("ZaminEntities"));
    }

    int SceneMigration::ReadVersion(const std::string& text)
    {
        json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !IsVersionedRoot(root))
            return -1;
        return SceneMigrationJson::ReadVersion(root);
    }

    bool SceneMigration::MigrateText(std::string& text, SceneMigrationResult* result, std::string* error)
    {
        json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !IsVersionedRoot(root))
        {
            if (error)
                *error = root.is_discarded() ? "JSON o'qilmadi" : "sahna fayli emas (\"Scene\" kaliti yo'q)";
            return false;
        }
        SceneMigrationResult migrated = SceneMigrationJson::Migrate(root);
        if (migrated.Migrated())
            text = root.dump(2);
        if (result)
            *result = std::move(migrated);
        if (error)
            error->clear();
        return true;
    }

    static std::mutex s_LogMutex;
    static std::unordered_map<std::string, int> s_Logged; // file -> the version it was logged from

    bool SceneMigration::LogMigration(const std::string& file, const SceneMigrationResult& result)
    {
        if (!result.Migrated() && !result.IsNewer())
            return false;
        {
            std::lock_guard lock(s_LogMutex);
            auto [it, added] = s_Logged.try_emplace(file, result.FileVersion);
            if (!added && it->second == result.FileVersion)
                return false;
            it->second = result.FileVersion;
        }
        const std::string name = file.empty() ? std::string("(matn)") : file;
        if (result.IsNewer())
        {
            ZM_CORE_WARN("Sahna fayli yangiroq formatda (v{}, bu dvigatel v{} gacha biladi): {} - tanilgan qismi o'qildi",
                         result.FileVersion, SceneMigration::CurrentVersion, name);
            return true;
        }
        ZM_CORE_INFO("Sahna formati yangilandi {}: {} (fayl saqlanganda yangi formatda yoziladi)", result.Summary(), name);
        for (const SceneMigrationStep& step : result.Steps)
            ZM_CORE_INFO("    v{} -> v{}: {} - {} ta o'zgarish", step.From, step.To, step.Description, step.Changes);
        return true;
    }

    void SceneMigration::ResetLog()
    {
        std::lock_guard lock(s_LogMutex);
        s_Logged.clear();
    }

}
