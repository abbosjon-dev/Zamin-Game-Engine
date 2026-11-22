// The UI audit (Scene::AuditUI, CheckUIAudit; UIAudit.h) and the vertical slices' UI layout gate (P5 item 3, U4): every
// screen of Chiroqbon (Projects/LanternKeeper) and Dovon mash'ali (Projects/BeaconPass) - the title menu with each of its
// panels, each level's HUD and every gameplay UI panel the level configures (pause, inventory, controls, level card,
// victory, defeat) - laid out headless at six resolutions (4:3, 16:9, 21:9) and three UI scale settings, measured with
// the default font's metrics, must keep the layout rules: usable widgets inside the view, no overlapping siblings,
// texts that fit, readable font sizes, the selection inside the open panel and every widget of it reachable by keyboard.
#include "TestAssertions.h"
#include <Zamin/Core/GameState.h>
#include <Zamin/Core/Log.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Gameplay/GameplayUI.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Scene/SceneSerializer.h>
#include <Zamin/UI/UIAudit.h>
#include <Zamin/Utils/FileSystem.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <set>
using namespace Zamin;
namespace {
    namespace G = Zamin::Gameplay;
    using J = nlohmann::json;

    UIAuditRecord Widget(uint64_t id, UIRect rect, uint64_t parent = 1, UIAuditWidget kind = UIAuditWidget::Button)
    {
        UIAuditRecord r;
        r.ID = id;
        r.Tag = std::format("W{}", id);
        r.Parent = parent;
        r.Widget = kind;
        r.Rect = rect;
        r.Interactable = r.Selectable = true;
        r.Depth = 1;
        return r;
    }
    bool Has(const std::vector<UIAuditIssue>& issues, const char* rule, uint64_t a = 0)
    {
        for (const auto& i : issues)
            if (i.Rule == rule && (!a || i.A == a)) return true;
        return false;
    }
}

// Each rule on a small hand-made report: a clean one passes, each defect is named.
TEST(UIAuditRulesNameEachLayoutDefect)
{
    UIAuditReport report;
    report.Viewport = { 800, 600 };
    report.TopPanel = "Main";
    report.TopPanelRoot = 1;
    auto a = Widget(10, { { 100, 400 }, { 300, 460 } }), b = Widget(11, { { 100, 300 }, { 300, 360 } });
    a.Panel = b.Panel = 1;
    a.Focused = true;
    a.Nav = { 0, 11, 0, 0 };
    b.Nav = { 10, 0, 0, 0 };
    UIAuditRecord label;
    label.ID = 12;
    label.Parent = 1;
    label.Widget = UIAuditWidget::Text;
    label.Rect = { { 100, 200 }, { 400, 240 } };
    label.HasText = true;
    label.FontPx = 14;
    label.TextSize = { 200, 20 };
    report.Records = { a, b, label };
    report.Focus = 10;
    UIAuditRules rules;
    CHECK(CheckUIAudit(report, rules).empty());

    auto broken = report;
    broken.Records[1].Rect = { { 2, 300 }, { 202, 360 } }; // 2 px from the edge
    CHECK(Has(CheckUIAudit(broken, rules), "Margin", 11));
    broken = report;
    broken.Records[1].Rect = { { 150, 420 }, { 350, 480 } }; // on top of the first
    CHECK(Has(CheckUIAudit(broken, rules), "Overlap", 10));
    broken = report;
    broken.Records[2].TextFits = false;
    CHECK(Has(CheckUIAudit(broken, rules), "TextOverflow", 12));
    broken.Records[2].TextCutByDesign = true; // ellipsis by design: fine
    CHECK(CheckUIAudit(broken, rules).empty());
    broken = report;
    broken.Records[2].FontPx = 10;
    CHECK(Has(CheckUIAudit(broken, rules), "MinFont", 12));
    broken = report;
    broken.Records[2].RawHint = "[Interact]"; // a prompt without InputHints shows the action's name, not its key
    CHECK(Has(CheckUIAudit(broken, rules), "RawHint", 12));
    broken = report;
    broken.Records[2].Rect = { { 700, 200 }, { 1000, 240 } };
    CHECK(Has(CheckUIAudit(broken, rules), "Offscreen", 12));
    broken = report;
    broken.Records[0].Nav = { 0, 0, 0, 0 }; // a navigation link broken: the second button cannot be reached
    CHECK(Has(CheckUIAudit(broken, rules), "Unreachable", 11));
    broken = report;
    broken.Focus = 0;
    CHECK(Has(CheckUIAudit(broken, rules), "Focus"));
    broken = report;
    broken.Records[1].Panel = 0; // the focus outside the open panel
    broken.Focus = 11;
    CHECK(Has(CheckUIAudit(broken, rules), "Focus"));
    // Hidden (faded / masked away) elements are not checked.
    broken = report;
    broken.Records[2].FontPx = 4;
    broken.Records[2].Hidden = true;
    CHECK(CheckUIAudit(broken, rules).empty());
    // A wide view: a centred top-level element in the outer tenth fails, one anchored to that side passes.
    auto wide = report;
    wide.Viewport = { 2560, 1080 };
    wide.TopPanelRoot = 0;
    UIAuditRecord bar;
    bar.ID = 20;
    bar.Widget = UIAuditWidget::Image;
    bar.Rect = { { 40, 1000 }, { 400, 1060 } };
    bar.AnchorMin = bar.AnchorMax = { 0.5f, 1.0f };
    wide.Records = { bar };
    CHECK(Has(CheckUIAudit(wide, rules), "SideBand", 20));
    wide.Records[0].AnchorMin = wide.Records[0].AnchorMax = { 0.0f, 1.0f };
    CHECK(CheckUIAudit(wide, rules).empty());
    CHECK(UIAuditRules::MinFontFor({ 800, 600 }, 1.0f) == 12.0f && UIAuditRules::MinFontFor({ 1280, 720 }, 1.0f) == 14.0f &&
          UIAuditRules::MinFontFor({ 1280, 720 }, 1.2f) == 14.0f && std::abs(UIAuditRules::MinFontFor({ 800, 600 }, 0.8f) - 9.6f) < 1e-4f);
    const std::string json = UIAuditToJson(report, CheckUIAudit(broken, rules), true);
    CHECK(json.find("\"Viewport\":[800,600]") != std::string::npos && json.find("\"Elements\":[") != std::string::npos);
}

// The audit lays the UI out for any view size without changing the scene's own layout; the UI scale setting scales
// every canvas; navigation targets and the selection are recorded.
TEST(UIAuditLaysOutAtAnyViewSizeAndKeepsTheScenesOwn)
{
    auto scene = CreateRef<Scene>();
    Entity canvas = scene->CreateEntity("Canvas");
    canvas.AddComponent<UICanvasComponent>(); // 1920 x 1080 reference, match 0.5
    auto button = [&](const char* name, glm::vec2 position) {
        Entity e = scene->CreateEntity(name);
        scene->SetParent(e, canvas, false);
        auto& t = e.AddComponent<UITransformComponent>();
        t.AnchorMin = t.AnchorMax = { 0.5f, 0.5f };
        t.Position = position;
        t.Size = { 400, 80 };
        t.FontSize = 32;
        e.AddComponent<UIButtonComponent>();
        e.AddComponent<TextComponent>().Text = name;
        e.AddComponent<UINavigationComponent>().NavigationMode = UINavigationComponent::Mode::Vertical;
        return e;
    };
    Entity play = button("Play", { 0, 60 }), quit = button("Quit", { 0, -60 });
    scene->OnViewportResize(1280, 720);
    scene->OnRuntimeStart();
    scene->UpdateRuntime(1.0f / 60.0f);
    scene->SetSelectedUI(play);
    const UIRect own = scene->GetUIScreenRect(play);
    const UIAuditReport big = scene->AuditUI(1920, 1080, true);
    CHECK(scene->GetUIScreenRect(play).Min == own.Min && scene->GetUIScreenRect(play).Max == own.Max);
    const auto find = [](const UIAuditReport& r, Entity e) -> const UIAuditRecord* {
        for (const auto& x : r.Records)
            if (x.ID == (uint64_t)e.GetUUID()) return &x;
        return nullptr;
    };
    const UIAuditRecord* p = find(big, play);
    CHECK(p && std::abs(p->Rect.GetWidth() - 400.0f) < 0.01f && std::abs(p->FontPx - 32.0f) < 0.01f); // reference size: 1 px per unit
    CHECK(p && p->Focused && p->Widget == UIAuditWidget::Button && p->Nav[1] == (uint64_t)quit.GetUUID());
    CHECK(p && p->HasText && p->TextFits && p->TextSize.x > 30.0f && p->TextSize.x < 200.0f); // measured with the font's metrics
    CHECK(big.Focus == (uint64_t)play.GetUUID() && CheckUIAudit(big, {}).empty());
    // 800 x 600 (match 0.5: sqrt(800/1920 x 600/1080)) and the UI scale setting on top.
    const UIAuditReport small = scene->AuditUI(800, 600, true);
    const float s = std::sqrt(800.0f / 1920.0f * 600.0f / 1080.0f);
    CHECK(find(small, play) && std::abs(find(small, play)->Rect.GetWidth() - 400.0f * s) < 0.05f);
    scene->SetUIScaleMultiplier(1.2f);
    const UIAuditReport scaled = scene->AuditUI(800, 600, true);
    CHECK(scaled.UserScale == 1.2f && find(scaled, play) && std::abs(find(scaled, play)->Rect.GetWidth() - 400.0f * s * 1.2f) < 0.05f);
    scene->SetUIScaleMultiplier(9.0f);
    CHECK(scene->GetUIScaleMultiplier() == Scene::MaxUIScaleMultiplier);
    scene->SetUIScaleMultiplier(1.0f);
    // A label too small for its text overflows; a hidden (faded) element is marked hidden.
    play.GetComponent<TextComponent>().Text = "Bu yozuv tugma uchun juda ham uzun bo'lib qoldi";
    quit.GetComponent<UITransformComponent>().Alpha = 0.0f;
    const UIAuditReport tight = scene->AuditUI(1920, 1080, true);
    CHECK(find(tight, play) && !find(tight, play)->TextFits && find(tight, quit) && find(tight, quit)->Hidden);
    CHECK(Has(CheckUIAudit(tight, {}), "TextOverflow", (uint64_t)play.GetUUID()));
    scene->OnRuntimeStop();
}

namespace {
    // A private copy of a slice (its saves out of the source tree; headless: projectile visuals left out, as in the
    // slices' oracle tests), loaded as the active project.
    std::filesystem::path PrepareSlice(const std::string& name)
    {
        const auto work = std::filesystem::temp_directory_path() / std::format("zamin-ui-{}-{}", name, uint64_t(UUID()));
        std::filesystem::create_directories(work);
        std::filesystem::copy(std::filesystem::path(ZAMIN_SOURCE_DIR) / "Projects" / name, work / name, std::filesystem::copy_options::recursive);
        const auto project = work / name;
        const auto graph = project / "Assets/Scripts/Hero.zgraph";
        auto json = J::parse(FileSystem::ReadTextFile(graph));
        for (auto& node : json["Nodes"])
            if (node["Kind"] == "Gameplay.ProjectileDefine") node["Inputs"]["Prefab"] = "";
        CHECK(FileSystem::WriteTextFile(graph, json.dump() + "\n"));
        SaveData::SetFilePath(work / "home" / "prefs.json");
        GameState::Clear();
        CHECK(Project::Load(project / (name + ".zproject")) != nullptr);
        return work;
    }

    struct LayoutTally
    {
        int Screens = 0, Audits = 0, Elements = 0, Issues = 0;
        float MinFont = 1e9f;
        std::set<std::string> Seen;
    };

    // The matrix: the six resolutions (4:3, 16:9, 21:9) at the UI scale setting's 80 %, 100 % and 120 %.
    struct AuditSize
    {
        glm::uvec2 Size;
        float Scale;
    };
    const std::vector<AuditSize>& AuditMatrix()
    {
        static const std::vector<AuditSize> matrix = [] {
            std::vector<AuditSize> m;
            for (float scale : { 0.8f, 1.0f, 1.2f })
                for (glm::uvec2 size : { glm::uvec2(800, 600), glm::uvec2(1024, 768), glm::uvec2(1280, 720), glm::uvec2(1600, 900), glm::uvec2(1920, 1080),
                                         glm::uvec2(2560, 1080) })
                    m.push_back({ size, scale });
            return m;
        }();
        return matrix;
    }

    // Every size of the matrix for the current screen: the rules, the first issues printed (all with ZAMIN_UI_AUDIT_ALL).
    void AuditScreen(Scene& scene, const std::string& screen, LayoutTally& tally)
    {
        tally.Screens++;
        static const bool all = std::getenv("ZAMIN_UI_AUDIT_ALL") != nullptr;
        for (const auto& [size, scale] : AuditMatrix())
        {
            scene.SetUIScaleMultiplier(scale);
            {
                const UIAuditReport report = scene.AuditUI(size.x, size.y, true);
                UIAuditRules rules;
                rules.MinFontPx = UIAuditRules::MinFontFor(size, scale);
                const auto issues = CheckUIAudit(report, rules);
                tally.Audits++;
                tally.Elements += (int)report.Records.size();
                for (const auto& r : report.Records)
                    if (r.HasText && !r.Hidden) tally.MinFont = std::min(tally.MinFont, r.FontPx);
                tally.Issues += (int)issues.size();
                for (const auto& i : issues)
                    if (tally.Seen.insert(screen + i.Rule + std::to_string(i.A)).second && (all || tally.Seen.size() < 40))
                        std::printf("    %s %ux%u UI %.0f%%: %s %s\n", screen.c_str(), size.x, size.y, scale * 100.0f, i.Rule.c_str(), i.Detail.c_str());
            }
        }
        scene.SetUIScaleMultiplier(1.0f);
    }

    // A scene of the slice played headless for `frames` (its graphs configure the HUD and the panels), its screens
    // audited: with nothing open, then each configured panel opened in turn.
    void AuditSceneScreens(const std::string& file, int frames, LayoutTally& tally)
    {
        auto scene = CreateRef<Scene>();
        CHECK(SceneSerializer(scene).Deserialize(Project::GetAssetDirectory() / file));
        scene->OnViewportResize(1280, 720);
        scene->OnRuntimeStart();
        for (int i = 0; i < frames; ++i) scene->UpdateRuntime(1.0f / 30.0f);
        G::UIWorld* ui = G::FindGameplayUI(*scene);
        CHECK(ui != nullptr);
        if (!ui)
        {
            scene->OnRuntimeStop();
            return;
        }
        // What is shown at the start (the main menu / the HUD), then each panel.
        AuditScreen(*scene, file + ":" + (ui->State().Top.empty() ? "HUD" : ui->State().Top), tally);
        const std::string start = ui->State().Top;
        for (const auto& [name, root] : ui->PanelRoots())
        {
            if (name == start) continue;
            std::string error;
            if (!ui->Open(name, error))
            {
                std::printf("    %s: '%s' ochilmadi: %s\n", file.c_str(), name.c_str(), error.c_str());
                continue;
            }
            for (int i = 0; i < 12; ++i) scene->UpdateRuntime(1.0f / 30.0f); // its show tween
            if (ui->State().Top == name) AuditScreen(*scene, file + ":" + name, tally);
            ui->Close(name, error);
            for (int i = 0; i < 4; ++i) scene->UpdateRuntime(1.0f / 30.0f);
        }
        scene->OnRuntimeStop();
    }

    void AuditSlice(const std::string& name, const std::vector<std::string>& scenes, int minScreens)
    {
        const auto level = Log::GetMinLevel();
        Log::SetMinLevel(LogLevel::Error);
        const auto prior = Project::GetActive();
        const auto work = PrepareSlice(name);
        LayoutTally tally;
        for (const auto& file : scenes)
            AuditSceneScreens(file, 45, tally);
        std::printf("    %s: %d ekran x %zu o'lcham/UI masshtab = %d audit, %d element, eng kichik matn %.1f px, %d xato\n", name.c_str(), tally.Screens,
                    AuditMatrix().size(), tally.Audits, tally.Elements, tally.MinFont, tally.Issues);
        CHECK(tally.Screens >= minScreens);
        CHECK(tally.Issues == 0);
        Log::SetMinLevel(level);
        Project::SetActive(prior);
        std::error_code ec;
        std::filesystem::remove_all(work, ec);
    }
}

TEST(SliceMenuLayoutLanternKeeper)
{
    AuditSlice("LanternKeeper", { "Scenes/Menu.zscene", "Scenes/Boglar.zscene", "Scenes/Yertola.zscene" }, 10);
}

TEST(SliceMenuLayoutBeaconPass)
{
    AuditSlice("BeaconPass", { "Scenes/Menu.zscene", "Scenes/Dovon.zscene" }, 7);
}

// The gate catches what it is for: in Chiroqbon's settings panel the focused music slider's broken links (Down, and
// Up's wrap-around) leave the rest of the panel unreachable, and the version line back at its old 18 units is under 12 px at 800 x 600.
TEST(SliceMenuLayoutCatchesABrokenLinkAndTinyText)
{
    const auto level = Log::GetMinLevel();
    Log::SetMinLevel(LogLevel::Error);
    const auto prior = Project::GetActive();
    const auto work = PrepareSlice("LanternKeeper");
    auto scene = CreateRef<Scene>();
    CHECK(SceneSerializer(scene).Deserialize(Project::GetAssetDirectory() / "Scenes/Menu.zscene"));
    scene->OnViewportResize(1280, 720);
    scene->OnRuntimeStart();
    for (int i = 0; i < 45; ++i) scene->UpdateRuntime(1.0f / 30.0f);
    G::UIWorld* ui = G::FindGameplayUI(*scene);
    std::string error;
    CHECK(ui && ui->Open("Settings", error));
    for (int i = 0; i < 12; ++i) scene->UpdateRuntime(1.0f / 30.0f);
    UIAuditRules rules;
    CHECK(CheckUIAudit(scene->AuditUI(1280, 720, true), rules).empty());
    Entity music = scene->GetEntityByUUID(UUID(6111));
    CHECK(music && scene->GetSelectedUI() == music);
    if (music)
    {
        auto& nav = music.GetComponent<UINavigationComponent>();
        nav.Down = {};
        nav.Up = {}; // (its wrap-around to Orqaga too: from the focused slider nothing else is reached)
        const auto issues = CheckUIAudit(scene->AuditUI(1280, 720, true), rules);
        CHECK(Has(issues, "Unreachable"));
    }
    if (Entity version = scene->GetEntityByUUID(UUID(6032)))
    {
        version.GetComponent<UITransformComponent>().FontSize = 18.0f;
        rules.MinFontPx = UIAuditRules::MinFontFor({ 800, 600 }, 1.0f);
        CHECK(Has(CheckUIAudit(scene->AuditUI(800, 600, true), rules), "MinFont", 6032));
    }
    scene->OnRuntimeStop();
    Log::SetMinLevel(level);
    Project::SetActive(prior);
    std::error_code ec;
    std::filesystem::remove_all(work, ec);
}
