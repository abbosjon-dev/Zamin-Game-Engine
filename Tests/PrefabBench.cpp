// ZaminBench --serialize --prefabs [--instances N] [--json <file>]: what linked prefab instances cost
// (task 412), headless. A synthetic 20-entity prefab ("Enemy": sprites, a body, a script with fields,
// internal references, one nested prefab "Weapon") is placed N times (1000 by default) as linked
// instances, a tenth of them with 3 overrides; the sample GameMenu prefab (44 UI entities) is run too.
// Timed: the scene saved / loaded (JSON and the binary cache) linked against unlinked, DeserializeEntities
// (the old call against the reporting one), SyncScene (the up-to-date fast path, a forced resync after
// the prefab changed), RecordOverrides, an Apply that reaches every instance, linking old instances,
// Scene::Copy, Instantiate(path) read every time against PrefabCache, PrefabExport::Prepare (the export's
// checks and the scene's copy without links). The budgets are printed next to
// the numbers (they are meant for a Release build).
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace Zamin;
namespace fs = std::filesystem;

namespace {

    using Clock = std::chrono::steady_clock;

    double Ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

    // The median of `runs` timings of `fn` (ms).
    double Median(int runs, const std::function<void()>& fn)
    {
        std::vector<double> times;
        for (int i = 0; i < runs; i++)
        {
            const auto start = Clock::now();
            fn();
            times.push_back(Ms(start));
        }
        std::sort(times.begin(), times.end());
        return times[times.size() / 2];
    }

    struct Row
    {
        std::string Key, Label;
        double Value = 0.0;
        std::string Unit;
        double Budget = 0.0; // 0: none
    };

    struct Report
    {
        std::vector<Row> Rows;
        void Add(std::string key, std::string label, double value, std::string unit = "ms", double budget = 0.0)
        {
            const char* mark = budget <= 0.0 ? "" : value <= budget ? "  (byudjet " : "  (BYUDJETDAN OSHDI: ";
            std::string budgetText = budget > 0.0 ? std::format("{}{:g} {})", mark, budget, unit) : std::string();
            std::printf("  %-54s %10.3f %s%s\n", label.c_str(), value, unit.c_str(), budgetText.c_str());
            Rows.push_back({ std::move(key), std::move(label), value, std::move(unit), budget });
        }
        std::string Json(const std::string& build, int instances) const
        {
            std::string out = std::format("{{\n  \"Build\": \"{}\",\n  \"Instances\": {},\n  \"Results\": {{\n", build, instances);
            for (size_t i = 0; i < Rows.size(); i++)
                out += std::format("    \"{}\": {{ \"Value\": {:.4f}, \"Unit\": \"{}\"{} }}{}\n", Rows[i].Key, Rows[i].Value, Rows[i].Unit,
                                   Rows[i].Budget > 0.0 ? std::format(", \"Budget\": {:g}", Rows[i].Budget) : std::string(),
                                   i + 1 < Rows.size() ? "," : "");
            return out + "  }\n}\n";
        }
    };

    // Weapon (3 entities) and Enemy (17 of its own + a nested Weapon = 20) in `assets`/Prefabs.
    bool MakeSyntheticPrefabs(const fs::path& assets, PrefabDatabase& db)
    {
        fs::create_directories(assets / "Prefabs");
        auto source = CreateRef<Scene>();
        Entity weapon = source->CreateEntity("Weapon");
        weapon.AddComponent<SpriteRendererComponent>().Color = { 0.3f, 0.3f, 0.35f, 1.0f };
        weapon.AddComponent<BoxCollider2DComponent>().Size = { 0.6f, 0.1f };
        Entity muzzle = source->CreateEntity("Muzzle");
        muzzle.GetComponent<TransformComponent>().Translation = { 0.6f, 0.0f, 0.0f };
        source->SetParent(muzzle, weapon, false);
        Entity flash = source->CreateEntity("Flash");
        flash.AddComponent<SpriteRendererComponent>().Color = { 1.0f, 0.8f, 0.2f, 1.0f };
        source->SetParent(flash, muzzle, false);
        if (!source->SavePrefab(weapon, assets / "Prefabs" / "Weapon.zprefab"))
            return false;

        Entity enemy = source->CreateEntity("Enemy");
        enemy.AddComponent<Rigidbody2DComponent>().Type = Rigidbody2DComponent::BodyType::Dynamic;
        enemy.AddComponent<BoxCollider2DComponent>();
        auto& script = enemy.AddComponent<NativeScriptComponent>();
        script.ClassName = "Enemy";
        ScriptFieldValue speed;
        speed.Set(3.5f);
        script.Fields["Speed"] = speed;
        ScriptFieldValue health;
        health.Set(100);
        script.Fields["Health"] = health;
        static const char* parts[] = { "Body", "Head", "ArmL", "ArmR", "LegL", "LegR", "Eye", "Shadow", "Label", "HitBox", "Hat", "Belt",
                                       "Boots", "Cape", "Glow", "Sensor" };
        std::vector<Entity> made;
        for (size_t i = 0; i < std::size(parts); i++)
        {
            Entity p = source->CreateEntity(parts[i]);
            p.GetComponent<TransformComponent>().Translation = { (float)(i % 4) * 0.2f - 0.3f, (float)(i / 4) * 0.25f, 0.0f };
            if (std::string_view(parts[i]) == "Label")
                p.AddComponent<TextComponent>().Text = "Dushman";
            else if (std::string_view(parts[i]) == "HitBox" || std::string_view(parts[i]) == "Sensor")
                p.AddComponent<BoxCollider2DComponent>().IsSensor = true;
            else
                p.AddComponent<SpriteRendererComponent>().Color = { 0.8f, 0.2f + 0.04f * (float)i, 0.2f, 1.0f };
            source->SetParent(p, i < 6 || i >= 8 ? enemy : made[0], false);
            made.push_back(p);
        }
        // Internal references (remapped on every placement).
        enemy.AddComponent<Camera2DFollowComponent>().Target.ID = made[1].GetUUID();
        made[7].AddComponent<Camera2DFollowComponent>().Target.ID = made[0].GetUUID();
        // The nested prefab.
        Entity nested = PrefabUtility::InstantiateLinked(*source, db, "Prefabs/Weapon.zprefab", made[3]);
        if (!nested)
            return false;
        return source->SavePrefab(enemy, assets / "Prefabs" / "Enemy.zprefab");
    }

    // `count` linked instances on a grid; every tenth with three overrides (recorded).
    void PlaceInstances(Scene& scene, PrefabDatabase& db, const std::string& prefab, int count, bool overrides)
    {
        for (int i = 0; i < count; i++)
        {
            Entity root = PrefabUtility::InstantiateLinked(scene, db, prefab);
            if (!root)
                continue;
            root.GetComponent<TransformComponent>().Translation = { (float)(i % 40) * 3.0f, (float)(i / 40) * 3.0f, 0.0f };
            root.SetName(std::format("{} ({})", root.GetName(), i + 1));
            if (!overrides || i % 10 != 0)
                continue;
            root.GetComponent<TransformComponent>().Scale = glm::vec3(1.25f);
            for (Entity c : scene.GetDescendants(root))
                if (auto* sprite = c.TryGetComponent<SpriteRendererComponent>())
                {
                    sprite->Color = { 0.1f, 0.9f, 0.1f, 1.0f };
                    break;
                }
            if (auto* nsc = root.TryGetComponent<NativeScriptComponent>(); nsc && nsc->Fields.contains("Speed"))
                nsc->Fields["Speed"].Set(7.0f);
            else if (Entity label = scene.FindChild(root, "Label", true); label && label.HasComponent<TextComponent>())
                label.GetComponent<TextComponent>().Text = "Boshqa";
            PrefabUtility::RecordOverrides(root, db);
        }
    }

    std::vector<Entity> InstanceRoots(Scene& scene)
    {
        std::vector<Entity> roots;
        for (Entity e : scene.GetEntitiesInHierarchyOrder())
            if (PrefabUtility::IsInstanceRoot(e) && !scene.GetParent(e))
                roots.push_back(e);
        return roots;
    }

    size_t EntityCount(Scene& scene) { return scene.GetRegistry().view<IDComponent>().size(); }

    void RunPrefabCase(Report& report, const char* name, const fs::path& assets, const std::string& prefab, int instances, bool withApply)
    {
        PrefabDatabase db(assets);
        db.SetRecheckInterval(1e9); // the files do not change behind the bench's back
        auto scene = CreateRef<Scene>();
        scene->SetName("PrefabBench");
        auto clock = Clock::now();
        PlaceInstances(*scene, db, prefab, instances, true);
        const double placeMs = Ms(clock);
        const size_t entities = EntityCount(*scene);
        std::printf("\n%s: %d nusxa, %zu obyekt (prefab: %s)\n", name, instances, entities, prefab.c_str());
        const std::string k = name;
        report.Add(k + ".Place", "Bog'langan nusxalarni qo'yish (InstantiateLinked)", placeMs);

        // ---- 1. save / load, linked against unlinked
        Ref<Scene> unlinked = Scene::Copy(scene);
        for (Entity root : InstanceRoots(*unlinked))
            PrefabUtility::Unpack(root, true);
        std::string linkedText, plainText;
        const double saveLinked = Median(3, [&]() { linkedText = SceneSerializer(scene).SerializeToString(); });
        const double savePlain = Median(3, [&]() { plainText = SceneSerializer(unlinked).SerializeToString(); });
        const double loadLinked = Median(3, [&]() { SceneSerializer(CreateRef<Scene>()).DeserializeFromString(linkedText); });
        const double loadPlain = Median(3, [&]() { SceneSerializer(CreateRef<Scene>()).DeserializeFromString(plainText); });
        std::vector<uint8_t> linkedBinary, plainBinary;
        const double binarySaveLinked = Median(3, [&]() { linkedBinary = SceneSerializer(scene).SerializeToBinary(); });
        const double binarySavePlain = Median(3, [&]() { plainBinary = SceneSerializer(unlinked).SerializeToBinary(); });
        const double binaryLoadLinked = Median(3, [&]() { SceneSerializer(CreateRef<Scene>()).DeserializeFromBinary(linkedBinary); });
        const double binaryLoadPlain = Median(3, [&]() { SceneSerializer(CreateRef<Scene>()).DeserializeFromBinary(plainBinary); });
        report.Add(k + ".SaveJsonLinked", "Saqlash JSON: bog'langan", saveLinked);
        report.Add(k + ".SaveJsonPlain", "Saqlash JSON: bog'lanmagan", savePlain);
        report.Add(k + ".LoadJsonLinked", "Yuklash JSON: bog'langan", loadLinked);
        report.Add(k + ".LoadJsonPlain", "Yuklash JSON: bog'lanmagan", loadPlain);
        report.Add(k + ".LoadOverhead", "  bog'lanishlar yuklashga qo'shgani", (loadLinked / std::max(loadPlain, 1e-6) - 1.0) * 100.0, "%", 5.0);
        report.Add(k + ".SaveBinaryLinked", "Saqlash binar: bog'langan", binarySaveLinked);
        report.Add(k + ".SaveBinaryPlain", "Saqlash binar: bog'lanmagan", binarySavePlain);
        report.Add(k + ".LoadBinaryLinked", "Yuklash binar: bog'langan", binaryLoadLinked);
        report.Add(k + ".LoadBinaryPlain", "Yuklash binar: bog'lanmagan", binaryLoadPlain);
        report.Add(k + ".LinkBytes", std::format("Bog'lanish baytlari ({:.1f} MB matnda)", (double)linkedText.size() / (1024.0 * 1024.0)),
                   (double)(linkedText.size() - std::min(linkedText.size(), plainText.size())) * 100.0 / (double)std::max<size_t>(1, linkedText.size()),
                   "%", 10.0);

        // ---- 2. DeserializeEntities: entities / s, the old call against the reporting one
        {
            const std::string text = FileSystem::ReadTextFile(db.ToFileSystemPath(prefab));
            const int spawns = std::max(20, 4000 / std::max(1, (int)db.Get(prefab)->Order.size()));
            size_t made = 0;
            const double oldMs = Median(3, [&]() {
                auto s = CreateRef<Scene>();
                made = 0;
                for (int i = 0; i < spawns; i++)
                    made += SceneSerializer::DeserializeEntities(*s, text, true).size();
            });
            const double reportMs = Median(3, [&]() {
                auto s = CreateRef<Scene>();
                for (int i = 0; i < spawns; i++)
                {
                    SceneLoadReport r;
                    SceneMigrationResult m;
                    SceneSerializer::EntitySetReadOptions options;
                    options.Report = &r;
                    options.Migration = &m;
                    SceneSerializer::DeserializeEntities(*s, text, options);
                }
            });
            report.Add(k + ".EntitiesPerSecondOld", "DeserializeEntities (eski chaqiruv): obyekt/s", (double)made / (oldMs / 1000.0), "obj/s");
            report.Add(k + ".EntitiesPerSecondReport", "DeserializeEntities (hisobot bilan): obyekt/s", (double)made / (reportMs / 1000.0), "obj/s");
        }

        // ---- 3. SyncScene: everything up to date (the hash fast path), then after the prefab changed
        PrefabUtility::SyncScene(*scene, db);
        const double fastPath = Median(5, [&]() { PrefabUtility::SyncScene(*scene, db); });
        report.Add(k + ".SyncFastPath", "SyncScene: hammasi yangi (hash tez yo'li)", fastPath, "ms", instances >= 1000 ? 5.0 : 0.0);
        {
            const auto clockForced = Clock::now();
            PrefabUtility::SyncResult forced = PrefabUtility::SyncScene(*scene, db, nullptr, true);
            const double forcedMs = Ms(clockForced);
            report.Add(k + ".SyncForced", std::format("SyncScene: majburiy qayta sinxron ({} nusxa)", forced.Instances), forcedMs, "ms",
                       entities >= 20000 ? 300.0 : 0.0);
        }

        // ---- 4. RecordOverrides
        const std::vector<Entity> roots = InstanceRoots(*scene);
        if (!roots.empty())
        {
            Entity one = roots[roots.size() / 2];
            const double recordOne = Median(9, [&]() { PrefabUtility::RecordOverrides(one, db); });
            report.Add(k + ".RecordOne", std::format("RecordOverrides: bitta nusxa ({} obyekt)", PrefabUtility::GetMembers(one).size()), recordOne, "ms");
            report.Add(k + ".RecordPerEntity", "  bitta obyektga", recordOne / (double)std::max<size_t>(1, PrefabUtility::GetMembers(one).size()), "ms", 0.2);
            const double recordAll = Median(3, [&]() { PrefabUtility::RecordOverrides(*scene, {}, true, db); });
            report.Add(k + ".RecordAll", "RecordOverrides: hamma nusxalar", recordAll);
        }

        // ---- 5. Apply that reaches every instance
        if (withApply && roots.size() > 1)
        {
            // Into a scratch copy of the prefab folder: the bench's own prefab is changed.
            Entity target = roots[1];
            target.GetComponent<TransformComponent>().Scale = glm::vec3(0.9f);
            PrefabUtility::RecordOverrides(target, db);
            const auto clockApply = Clock::now();
            PrefabApplyResult applied = PrefabUtility::Apply(target, PrefabScope::Field, "Transform/Scale", db);
            const double applyMs = Ms(clockApply);
            report.Add(k + ".ApplyPropagate", std::format("Apply (qiymat) -> {} ta boshqa nusxa yangilandi", applied.InstancesSynced), applyMs);
        }

        // ---- 6. linking old instances (only the prefab named)
        {
            Ref<Scene> legacy = Scene::Copy(scene);
            for (auto e : legacy->GetAllEntitiesWith<PrefabLinkComponent>())
                legacy->GetRegistry().remove<PrefabLinkComponent>(e);
            const auto clockLegacy = Clock::now();
            const size_t linked = PrefabUtility::LinkLegacyInstances(*legacy, db);
            report.Add(k + ".LegacyLink", std::format("Eski nusxalarni bog'lash ({} ta)", linked), Ms(clockLegacy));
        }

        // ---- 7. Scene::Copy (play mode) with the links
        report.Add(k + ".CopyLinked", "Scene::Copy (o'yin rejimi): bog'langan", Median(3, [&]() { Scene::Copy(scene); }));
        report.Add(k + ".CopyPlain", "Scene::Copy (o'yin rejimi): bog'lanmagan", Median(3, [&]() { Scene::Copy(unlinked); }));

        // ---- 8. Instantiate(path): the file read every time against PrefabCache
        {
            const fs::path file = db.ToFileSystemPath(prefab);
            const int spawns = 1000;
            const double uncached = Median(3, [&]() {
                auto s = CreateRef<Scene>();
                for (int i = 0; i < spawns; i++)
                {
                    const std::string text = FileSystem::ReadTextFile(file);
                    SceneSerializer::EntitySetReadOptions options;
                    options.KeepPrefabLinks = false;
                    SceneSerializer::DeserializeEntities(*s, text, options);
                }
            });
            PrefabCache::Clear();
            PrefabCache::ResetStats();
            // What Scene::InstantiatePrefab and a script's Instantiate(path) do (without resolving
            // assets: headless, and the same for both).
            const double cached = Median(3, [&]() {
                auto s = CreateRef<Scene>();
                for (int i = 0; i < spawns; i++)
                {
                    SceneSerializer::EntitySetReadOptions options;
                    options.KeepPrefabLinks = false;
                    PrefabCache::Instantiate(*s, file, options);
                }
            });
            const PrefabCache::Stats stats = PrefabCache::GetStats();
            report.Add(k + ".InstantiateUncached", "Instantiate(path) x1000: har safar o'qib", uncached);
            report.Add(k + ".InstantiateCached", std::format("Instantiate(path) x1000: PrefabCache ({} o'qish)", stats.Reads), cached);
        }

        // ---- 9. PrefabExport::Prepare (419): the checks of an export and the scene's copy written
        // without its links, the scene saved where the export finds it (removed after)
        {
            const fs::path sceneFile = assets / "Scenes" / (k + ".zscene");
            fs::create_directories(sceneFile.parent_path());
            if (FileSystem::WriteTextFile(sceneFile, SceneSerializer(scene).SerializeToString()))
            {
                AssetReferenceIndex index;
                AssetReferenceIndex::Sources sources;
                sources.AssetDirectory = assets;
                sources.ProjectDirectory = assets.parent_path();
                index.Build(sources);
                ExportAssetInputs inputs;
                inputs.Settings.ExcludeUnusedAssets = false; // every scene and prefab of the folder
                const ExportAssetPlan plan = ExportAssets::Plan(index, inputs);
                PrefabExportResult prepared;
                const double prepareMs = Median(3, [&]() { prepared = PrefabExport::Prepare(plan, db, &index); });
                report.Add(k + ".ExportPrepare",
                           std::format("PrefabExport::Prepare (419): {} prefab, {} nusxa fayl", prepared.PrefabsChecked, prepared.Baked.size()),
                           prepareMs);
            }
            std::error_code ec;
            fs::remove(sceneFile, ec);
        }
    }

}

int RunPrefabBench(int instances, const char* jsonFile)
{
    SceneLoadProfiler::SetLogging(false);
    Log::SetMinLevel(LogLevel::Error);
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    std::printf("Zamin prefab benchmark (%s): bog'langan nusxalar, %d ta sintetik 20 obyektli prefab nusxasi\n", build, instances);
    TempDirectory temp("zamin-prefab-bench");
    const fs::path assets = temp.GetPath();
    Report report;
    {
        PrefabDatabase db(assets);
        if (!MakeSyntheticPrefabs(assets, db))
        {
            std::printf("Sintetik prefab yaratilmadi\n");
            return 1;
        }
    }
    RunPrefabCase(report, "Enemy", assets, "Prefabs/Enemy.zprefab", instances, true);
    // The sample GameMenu (44 UI entities), from a copy (the sample is never written).
    const fs::path menu = fs::path(ZAMIN_SOURCE_DIR) / "Projects" / "Platformer2D" / "Assets" / "Prefabs" / "GameMenu.zprefab";
    std::error_code ec;
    fs::copy_file(menu, assets / "Prefabs" / "GameMenu.zprefab", fs::copy_options::overwrite_existing, ec);
    if (!ec)
        RunPrefabCase(report, "GameMenu", assets, "Prefabs/GameMenu.zprefab", std::max(10, instances / 10), true);
    if (jsonFile && *jsonFile)
    {
        FileSystem::WriteTextFile(jsonFile, report.Json(build, instances));
        std::printf("\nJSON: %s\n", jsonFile);
    }
    return 0;
}
