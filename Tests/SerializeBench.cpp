// ZaminBench --serialize [--entities N] [--content 2d|3d|all] [--json <file>]: saving and loading large scenes,
// headless (tasks 297 and 432). In the sample Platformer2D project (its assets resolve the paths):
//   - a 2D scene (N entities, 10 000 by default: sprites, 2D bodies, scripts with fields, texts, tags, a hierarchy)
//     and a 3D one (meshes with materials, lights, 3D bodies, particle systems, sounds, scripts, a hierarchy):
//     the JSON text (SerializeToString, the file, Deserialize stage by stage - SceneLoadTiming) and the binary
//     cache (.zsceneb), MB and MB/s; the scenes read back are checked (saved again: the same text);
//   - copy / paste of a 1 000-entity selection (SerializeEntities, DeserializeEntities with new ids);
//   - what each component costs: 1 000 entities that have only it, saved and loaded, less an entity without
//     one (ns and bytes per component).
// Every number is the median of 3 runs (5 for the component table). The linked prefab instances have a
// benchmark of their own (--serialize --prefabs, PrefabBench.cpp); docs/benchmarks.md has the baseline.
#include <Zamin/Zamin.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <vector>

using namespace Zamin;
namespace fs = std::filesystem;

namespace {

    using Clock = std::chrono::steady_clock;

    struct SerializeRow
    {
        std::string Key, Label;
        double Value = 0.0;
        std::string Unit;
        bool Noisy = false; // file I/O: the disk cache decides (Tests/bench_compare.py lists it, never fails on it)
    };

    struct SerializeReport
    {
        std::vector<SerializeRow> Rows;

        void Add(std::string key, std::string label, double value, std::string unit = "ms", std::string note = {}, bool noisy = false)
        {
            std::printf("  %-46s %10.3f %-5s %s\n", label.c_str(), value, unit.c_str(), note.c_str());
            Rows.push_back({ std::move(key), std::move(label), value, std::move(unit), noisy });
        }
        // Kept in the JSON only (the component table prints its own rows).
        void Keep(std::string key, std::string label, double value, std::string unit)
        {
            Rows.push_back({ std::move(key), std::move(label), value, std::move(unit) });
        }

        std::string Json(const std::string& build, int entities) const
        {
            std::string out = std::format("{{\n  \"Build\": \"{}\",\n  \"Entities\": {},\n  \"Results\": {{\n", build, entities);
            for (size_t i = 0; i < Rows.size(); i++)
                out += std::format("    \"{}\": {{ \"Value\": {:.4f}, \"Unit\": \"{}\"{} }}{}\n", Rows[i].Key, Rows[i].Value, Rows[i].Unit,
                                   Rows[i].Noisy ? ", \"Noisy\": true" : "", i + 1 < Rows.size() ? "," : "");
            return out + "  }\n}\n";
        }
    };

    double MedianOf(std::vector<double> values)
    {
        std::sort(values.begin(), values.end());
        return values.empty() ? 0.0 : values[values.size() / 2];
    }

    double MBps(size_t bytes, double ms) { return ms > 0.0 ? (double)bytes / (1024.0 * 1024.0) / (ms / 1000.0) : 0.0; }

    // ---- the scenes
    void FillLarge2DScene(Scene& scene, int count)
    {
        static const char* textures[] = { "Textures/coin.png", "Textures/boulder.png", "Textures/crystal.png", "Textures/bushes.png",
                                          "Textures/player.png" };
        Entity group;
        for (int i = 0; i < count; i++)
        {
            Entity e = scene.CreateEntity(std::format("Obyekt {}", i));
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation = { (float)(i % 100) * 1.5f, (float)(i / 100) * 1.25f, 0.0f };
            t.Rotation.z = (float)(i % 13) * 0.1f;
            t.Scale = glm::vec3(1.0f + (float)(i % 3) * 0.25f);
            if (i % 10 == 0)
                group = e; // a parent of the next nine
            else if (group)
                scene.SetParent(e, group);
            if (i % 10 != 0)
            {
                auto& sprite = e.AddComponent<SpriteRendererComponent>();
                sprite.TexturePath = textures[i % 5];
                sprite.Color = { 1.0f, 0.9f, 0.8f, 1.0f };
                sprite.PixelArt = true;
            }
            if (i % 5 == 0)
            {
                e.AddComponent<Rigidbody2DComponent>().Type = i % 2 ? Rigidbody2DComponent::BodyType::Dynamic : Rigidbody2DComponent::BodyType::Static;
                e.AddComponent<BoxCollider2DComponent>();
            }
            if (i % 20 == 3)
            {
                auto& nsc = e.AddComponent<NativeScriptComponent>();
                nsc.ClassName = "Coin";
                ScriptFieldValue value;
                value.Set(10 + i % 7);
                nsc.Fields["Value"] = value;
                ScriptFieldValue speed;
                speed.Set(2.5f);
                nsc.Fields["SpinSpeed"] = speed;
            }
            if (i % 50 == 7)
                e.AddComponent<TextComponent>().Text = std::format("Matn {}", i);
            if (i % 7 == 0)
                e.AddTag("Pickup");
        }
    }

    void FillLarge3DScene(Scene& scene, int count)
    {
        static const PrimitiveMesh shapes[] = { PrimitiveMesh::Cube, PrimitiveMesh::Sphere, PrimitiveMesh::Cylinder, PrimitiveMesh::Cone };
        Entity group;
        for (int i = 0; i < count; i++)
        {
            Entity e = scene.CreateEntity(std::format("Obyekt {}", i));
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation = { (float)(i % 50) * 2.0f, (float)(i % 7) * 0.5f, (float)(i / 50) * 2.0f };
            t.Rotation.y = (float)(i % 17) * 0.2f;
            if (i % 10 == 0)
                group = e;
            else if (group)
                scene.SetParent(e, group);
            if (i % 10 != 0)
            {
                auto& mesh = e.AddComponent<MeshRendererComponent>();
                mesh.Type = shapes[i % 4];
                mesh.Mat.Albedo = { 0.2f + (float)(i % 5) * 0.15f, 0.5f, 0.8f, 1.0f };
                mesh.Mat.Metallic = (float)(i % 3) * 0.5f;
                mesh.Mat.Roughness = 0.3f + (float)(i % 4) * 0.15f;
            }
            if (i % 5 == 0)
            {
                e.AddComponent<Rigidbody3DComponent>().Type = i % 2 ? Rigidbody3DComponent::BodyType::Dynamic : Rigidbody3DComponent::BodyType::Static;
                e.AddComponent<BoxCollider3DComponent>();
            }
            if (i % 100 == 11)
                e.AddComponent<PointLightComponent>().Color = { 1.0f, 0.8f, 0.6f };
            if (i % 200 == 13)
                e.AddComponent<ParticleSystemComponent>().EmissionRate = 40.0f;
            if (i % 50 == 17)
            {
                auto& audio = e.AddComponent<AudioSourceComponent>();
                audio.Path = "Audio/coin.wav";
                audio.Spatial = true;
            }
            if (i % 20 == 3)
            {
                auto& nsc = e.AddComponent<NativeScriptComponent>();
                nsc.ClassName = "Rotator";
                ScriptFieldValue speed;
                speed.Set(1.5f);
                nsc.Fields["Speed"] = speed;
                ScriptFieldValue axis;
                axis.Set(glm::vec3(0.0f, 1.0f, 0.0f));
                nsc.Fields["Axis"] = axis;
            }
        }
    }

    // A scene saved and read back, both formats (the median of `runs`). False when what was read back differs.
    bool BenchScene(SerializeReport& report, const std::string& key, const Ref<Scene>& source, const fs::path& dir, int runs)
    {
        bool ok = true;
        const fs::path textFile = dir / (key + ".zscene");
        const fs::path binaryFile = dir / (key + ".zsceneb");
        std::vector<double> save, write, binarySave, load, binaryLoad;
        std::vector<SceneLoadTiming> textTimings, binaryTimings;
        size_t textBytes = 0, binaryBytes = 0;
        std::string text;
        for (int run = 0; run < runs; run++)
        {
            auto clock = Clock::now();
            text = SceneSerializer(source).SerializeToString();
            save.push_back(ElapsedMilliseconds(clock));
            textBytes = text.size();
            clock = Clock::now();
            FileSystem::WriteTextFile(textFile, text);
            write.push_back(ElapsedMilliseconds(clock));

            clock = Clock::now();
            {
                std::vector<uint8_t> bytes = SceneSerializer(source).SerializeToBinary();
                binarySave.push_back(ElapsedMilliseconds(clock));
                binaryBytes = bytes.size();
                FileSystem::WriteBinaryFile(binaryFile, bytes.data(), bytes.size());
            }

            auto loaded = CreateRef<Scene>();
            SceneSerializer textLoader(loaded);
            clock = Clock::now();
            if (!textLoader.Deserialize(textFile))
                std::printf("JSON yuklanmadi: %s\n", textLoader.GetLastError().c_str());
            load.push_back(ElapsedMilliseconds(clock));
            textTimings.push_back(textLoader.GetTiming());

            auto fromBinary = CreateRef<Scene>();
            SceneSerializer binaryLoader(fromBinary);
            clock = Clock::now();
            if (!binaryLoader.Deserialize(binaryFile))
                std::printf("Binar kesh yuklanmadi: %s\n", binaryLoader.GetLastError().c_str());
            binaryLoad.push_back(ElapsedMilliseconds(clock));
            binaryTimings.push_back(binaryLoader.GetTiming());
            if (run == 0)
            {
                // Both give the same scene as the source (saved again: the same text).
                const bool same = SceneSerializer(loaded).SerializeToString() == text && SceneSerializer(fromBinary).SerializeToString() == text;
                std::printf("  Tekshiruv: JSON va binar keshdan yuklangan sahna qayta saqlanganda bir xil matn: %s\n", same ? "ha" : "YO'Q");
                ok = ok && same && loaded->GetEntitiesInOrder().size() == source->GetEntitiesInOrder().size();
            }
        }
        const double mb = (double)textBytes / (1024.0 * 1024.0), binaryMb = (double)binaryBytes / (1024.0 * 1024.0);
        const double saveMs = MedianOf(save), loadMs = MedianOf(load), binarySaveMs = MedianOf(binarySave), binaryLoadMs = MedianOf(binaryLoad);
        report.Add(key + ".SaveJson", "Saqlash: JSON matn (SerializeToString)", saveMs, "ms",
                   std::format("({:.1f} MB, {:.0f} MB/s)", mb, MBps(textBytes, saveMs)));
        report.Add(key + ".WriteFile", "  faylga yozish", MedianOf(write), "ms", {}, true);
        report.Add(key + ".SaveBinary", "Saqlash: binar kesh (MessagePack)", binarySaveMs, "ms",
                   std::format("({:.1f} MB, {:.0f} MB/s)", binaryMb, MBps(binaryBytes, binarySaveMs)));
        report.Add(key + ".LoadJson", "Yuklash: JSON matn (Deserialize)", loadMs, "ms", std::format("({:.0f} MB/s)", MBps(textBytes, loadMs)));
        for (size_t s = 0; s < SceneLoadTiming::StageCount; s++)
        {
            std::vector<double> v;
            for (const SceneLoadTiming& t : textTimings)
                v.push_back(t.Milliseconds[s]);
            if (const double ms = MedianOf(v); ms >= 0.05)
                report.Add(std::format("{}.LoadJson.{}", key, s), std::format("  {}", SceneLoadTiming::StageName((SceneLoadTiming::Stage)s)), ms, "ms",
                           {}, s == (size_t)SceneLoadTiming::Stage::Read);
        }
        report.Add(key + ".LoadBinary", "Yuklash: binar kesh (Deserialize)", binaryLoadMs, "ms",
                   std::format("({:.1f}x JSON'dan tez)", binaryLoadMs > 0.0 ? loadMs / binaryLoadMs : 0.0));
        for (size_t s = 0; s < SceneLoadTiming::StageCount; s++)
        {
            std::vector<double> v;
            for (const SceneLoadTiming& t : binaryTimings)
                v.push_back(t.Milliseconds[s]);
            if (const double ms = MedianOf(v); ms >= 0.05)
                report.Add(std::format("{}.LoadBinary.{}", key, s), std::format("  {}", SceneLoadTiming::StageName((SceneLoadTiming::Stage)s)), ms,
                           "ms", {}, s == (size_t)SceneLoadTiming::Stage::Read);
        }
        report.Keep(key + ".JsonMB", "JSON hajmi", mb, "MB");
        report.Keep(key + ".BinaryMB", "Binar kesh hajmi", binaryMb, "MB");
        return ok;
    }

    // ---- what one component costs
    struct ComponentCase
    {
        const char* Key;
        const char* Name;
        std::function<void(Entity)> Add; // null: the entity without one (the base)
    };

    std::vector<ComponentCase> ComponentCases()
    {
        return {
            { "Base", "(bo'sh obyekt: ID, Tag, Transform, Relationship)", {} },
            { "SpriteRenderer", "SpriteRenderer", [](Entity e) { e.AddComponent<SpriteRendererComponent>().TexturePath = "Textures/coin.png"; } },
            { "CircleRenderer", "CircleRenderer", [](Entity e) { e.AddComponent<CircleRendererComponent>(); } },
            { "Text", "Text", [](Entity e) { e.AddComponent<TextComponent>().Text = "Salom, dunyo"; } },
            { "MeshRenderer", "MeshRenderer (primitiv, ichki material)", [](Entity e) { e.AddComponent<MeshRendererComponent>(); } },
            { "Camera", "Camera", [](Entity e) { e.AddComponent<CameraComponent>(); } },
            { "PointLight", "PointLight", [](Entity e) { e.AddComponent<PointLightComponent>(); } },
            { "Rigidbody2D", "Rigidbody2D", [](Entity e) { e.AddComponent<Rigidbody2DComponent>(); } },
            { "BoxCollider2D", "BoxCollider2D", [](Entity e) { e.AddComponent<BoxCollider2DComponent>(); } },
            { "PolygonCollider2D", "PolygonCollider2D (5 nuqta)", [](Entity e) { e.AddComponent<PolygonCollider2DComponent>(); } },
            { "Rigidbody3D", "Rigidbody3D", [](Entity e) { e.AddComponent<Rigidbody3DComponent>(); } },
            { "BoxCollider3D", "BoxCollider3D", [](Entity e) { e.AddComponent<BoxCollider3DComponent>(); } },
            { "NativeScript", "NativeScript (3 maydon)",
              [](Entity e) {
                  auto& nsc = e.AddComponent<NativeScriptComponent>();
                  nsc.ClassName = "Coin";
                  ScriptFieldValue a, b, c;
                  a.Set(10);
                  b.Set(2.5f);
                  c.Set(std::string("oltin"));
                  nsc.Fields["Value"] = a;
                  nsc.Fields["SpinSpeed"] = b;
                  nsc.Fields["Kind"] = c;
              } },
            { "AudioSource", "AudioSource", [](Entity e) { e.AddComponent<AudioSourceComponent>().Path = "Audio/coin.wav"; } },
            { "ParticleSystem", "ParticleSystem (standart modullar)", [](Entity e) { e.AddComponent<ParticleSystemComponent>(); } },
            { "Animator", "Animator", [](Entity e) { e.AddComponent<AnimatorComponent>(); } },
            { "UIButton", "UITransform + UIButton",
              [](Entity e) {
                  e.AddComponent<UITransformComponent>();
                  e.AddComponent<UIButtonComponent>();
              } },
        };
    }

    void BenchComponents(SerializeReport& report)
    {
        const int count = 1000, runs = 5;
        std::printf("\nKomponent narxi: %d obyekt, har birida faqat shu komponent; bo'sh obyekt narxi ayirilgan (uning qatori - butun obyekt),\n"
                "%d marta mediana, bitta komponent uchun\n", count, runs);
        std::printf("  %-46s %12s %12s %10s\n", "Komponent", "saqlash ns", "yuklash ns", "JSON bayt");
        double baseSave = 0.0, baseLoad = 0.0, baseBytes = 0.0;
        for (const ComponentCase& c : ComponentCases())
        {
            auto scene = CreateRef<Scene>();
            for (int i = 0; i < count; i++)
            {
                Entity e = scene->CreateEntity(std::format("Obyekt {}", i));
                if (c.Add)
                    c.Add(e);
            }
            std::vector<double> save, load;
            std::string text;
            for (int r = 0; r < runs; r++)
            {
                auto clock = Clock::now();
                text = SceneSerializer(scene).SerializeToString();
                save.push_back(ElapsedMilliseconds(clock));
                auto loaded = CreateRef<Scene>();
                clock = Clock::now();
                SceneSerializer(loaded).DeserializeFromString(text);
                load.push_back(ElapsedMilliseconds(clock));
            }
            // Per entity, in ns; the base row is the whole entity, the others what the component adds.
            const double saveNs = MedianOf(save) * 1e6 / count, loadNs = MedianOf(load) * 1e6 / count, bytes = (double)text.size() / count;
            if (!c.Add)
            {
                baseSave = saveNs;
                baseLoad = loadNs;
                baseBytes = bytes;
                std::printf("  %-46s %12.0f %12.0f %10.0f\n", c.Name, saveNs, loadNs, bytes);
            }
            else
                std::printf("  %-46s %12.0f %12.0f %10.0f\n", c.Name, saveNs - baseSave, loadNs - baseLoad, bytes - baseBytes);
            const bool base = !c.Add;
            report.Keep(std::format("Component.{}.SaveNs", c.Key), c.Name, base ? saveNs : saveNs - baseSave, "ns");
            report.Keep(std::format("Component.{}.LoadNs", c.Key), c.Name, base ? loadNs : loadNs - baseLoad, "ns");
            report.Keep(std::format("Component.{}.Bytes", c.Key), c.Name, base ? bytes : bytes - baseBytes, "B");
        }
    }

}

int RunSerializeBench(int count, const char* jsonFile, const std::string& content)
{
    Log::SetMinLevel(LogLevel::Error);
    const fs::path root = fs::path(ZAMIN_SOURCE_DIR) / "Projects" / "Platformer2D";
    Ref<Project> project = Project::Load(root / "Platformer2D.zproject");
    if (!project)
    {
        std::printf("Platformer2D loyihasi ochilmadi: %s\n", root.string().c_str());
        return 1;
    }
    Project::SetActive(project);
    SceneLoadProfiler::SetLogging(false);
    const fs::path dir = fs::temp_directory_path() / std::format("zamin-serialize-bench-{}", (uint64_t)UUID());
    fs::create_directories(dir);
#if defined(NDEBUG)
    const char* build = "Release";
#else
    const char* build = "Debug";
#endif
    const bool all = content == "all";
    std::printf("Zamin sahna serializatsiyasi benchmark: %d obyekt (%s), har raqam 3 marta o'lchanib mediana\n", count, build);
    SerializeReport report;
    const int runs = 3;
    bool ok = true; // the checks (what was read back is what was saved)
    if (all || content == "2d")
    {
        std::printf("\n2D sahna: spritelar, 2D jismlar, skriptlar (maydonlar bilan), matnlar, teglar, ierarxiya\n");
        auto source = CreateRef<Scene>();
        source->SetName("Large2D");
        FillLarge2DScene(*source, count);
        ok = BenchScene(report, "2D", source, dir, runs) && ok;
    }
    if (all || content == "3d")
    {
        std::printf("\n3D sahna: meshlar (material bilan), 3D jismlar, yorug'liklar, particle'lar, ovozlar, skriptlar, ierarxiya\n");
        auto source = CreateRef<Scene>();
        source->SetName("Large3D");
        FillLarge3DScene(*source, count);
        ok = BenchScene(report, "3D", source, dir, runs) && ok;
    }
    if (all)
    {
        // The editor's copy / paste: a selection of 1 000 entities (100 groups of 10) to the clipboard and back.
        auto scene = CreateRef<Scene>();
        FillLarge2DScene(*scene, std::max(count, 1000));
        std::vector<Entity> selection;
        for (Entity e : scene->GetEntitiesInOrder())
            if (selection.size() < 1000)
                selection.push_back(e);
        std::printf("\nNusxalash / joylash (clipboard): %zu obyektli tanlov\n", selection.size());
        std::vector<double> copy, paste;
        std::string text;
        size_t pasted = 0;
        for (int r = 0; r < runs; r++)
        {
            auto clock = Clock::now();
            text = SceneSerializer::SerializeEntities(*scene, selection);
            copy.push_back(ElapsedMilliseconds(clock));
            auto target = CreateRef<Scene>();
            clock = Clock::now();
            pasted = SceneSerializer::DeserializeEntities(*target, text, true).size();
            paste.push_back(ElapsedMilliseconds(clock));
        }
        report.Add("Clipboard.Copy", "Nusxalash (SerializeEntities)", MedianOf(copy), "ms",
                   std::format("({:.2f} MB)", (double)text.size() / (1024.0 * 1024.0)));
        report.Add("Clipboard.Paste", "Joylash (DeserializeEntities, yangi UUID)", MedianOf(paste), "ms",
                   std::format("({} obyekt{})", pasted, pasted == selection.size() ? "" : " - KAM!"));
        ok = ok && pasted == selection.size();
        BenchComponents(report);
    }
    if (jsonFile && *jsonFile)
    {
        FileSystem::WriteTextFile(jsonFile, report.Json(build, count));
        std::printf("\nJSON: %s\n", jsonFile);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    Project::SetActive(nullptr);
    if (!ok)
        std::printf("\nTEKSHIRUV O'TMADI: o'qilgan sahna saqlanganidan farq qiladi\n");
    return ok ? 0 : 1;
}
