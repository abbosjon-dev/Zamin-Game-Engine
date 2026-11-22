// Engine extensions for the vertical slices: vector/angle math nodes, Gameplay.Steer, HUD kinds 7-9 (energy, ability,
// level), flow/app/settings bridges (LoadScene fades, UIValue/SetUIValue, Localization, audio groups, demo mode).
#include "TestAssertions.h"
#include <Zamin/Gameplay/GameplayRules.h>
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <Zamin/Gameplay/GameplayUI.h>
#include <Zamin/Gameplay/UIModel.h>
#include <Zamin/Gameplay/LoadoutWorld.h>
#include <Zamin/Gameplay/QuestWorld.h>
#include <Zamin/Scene/Entity.h>
#include <Zamin/Scripting/Visual/VisualGraph.h>
#include <Zamin/Scripting/Visual/VisualGraphHost.h>
#include <Zamin/Project/Project.h>
#include <Zamin/Audio/Audio.h>
#include <Zamin/Core/Display.h>
#include <Zamin/Core/SaveData.h>
#include <Zamin/Localization/Localization.h>
#include <Zamin/Project/PlayerSettings.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <functional>
using namespace Zamin;
namespace {
    namespace G = Zamin::Gameplay;
    using Visual::Value;
    bool Throws(const std::function<void()>& f)
    {
        try
        {
            f();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }
    // Runs `node`'s output `output` (a pure node) into a Number/Vec variable on Start and returns it.
    Value Evaluate(const std::string& node, const std::string& output, std::map<std::string, Value> inputs, Visual::Type type = Visual::Type::Number)
    {
        Visual::Graph g;
        g.Variables["R"] = { type, Visual::DefaultValue(type) };
        std::string setter = std::string("Set.") + Visual::TypeName(type);
        g.Nodes = { { 1, "Event.Start" }, { 2, setter, 0, 0, { { "Name", "R" } } }, { 3, node, 0, 0, inputs } };
        g.Links = { { 1, "Then", 2, "In" }, { 3, output, 2, "Value" } };
        Visual::Machine vm;
        CHECK(vm.Load(g));
        vm.Event("Start");
        CHECK(vm.Error().empty());
        return vm.Variables().at("R");
    }
}
TEST(VisualGraphVectorMathNodes)
{
    using T = Visual::Type;
    CHECK_NEAR(Evaluate("Atan2", "Value", { { "Y", 1 }, { "X", 1 } }).get<double>(), std::atan2(1, 1), 1e-12);
    CHECK(Evaluate("Atan2", "Value", { { "Y", 0 }, { "X", 0 } }) == 0.0);
    // LerpAngle takes the short way round: from 170 to -170 degrees passes 180, not 0.
    const double pi = 3.14159265358979323846, a = 170 * pi / 180, b = -170 * pi / 180;
    CHECK_NEAR(Evaluate("LerpAngle", "Value", { { "A", a }, { "B", b }, { "T", .5 } }).get<double>(), pi, 1e-9);
    CHECK_NEAR(Evaluate("LerpAngle", "Value", { { "A", 0 }, { "B", 1 }, { "T", .25 } }).get<double>(), .25, 1e-12);
    CHECK(Evaluate("LengthVec2", "Value", { { "Vector", { 3., 4. } } }) == 5.0);
    CHECK(Evaluate("DistanceVec3", "Value", { { "A", { 1., 2., 3. } }, { "B", { 1., 5., 7. } } }) == 5.0);
    CHECK(Evaluate("DotVec3", "Value", { { "A", { 1., 2., 3. } }, { "B", { 4., -5., 6. } } }) == 12.0);
    CHECK(Evaluate("SubtractVec2", "Value", { { "A", { 3., 1. } }, { "B", { 1., 4. } } }, T::Vec2) == Value::array({ 2., -3. }));
    CHECK(Evaluate("NormalizeVec2", "Value", { { "Vector", { 0., -2. } } }, T::Vec2) == Value::array({ 0., -1. }));
    CHECK(Evaluate("NormalizeVec3", "Value", { { "Vector", { 0., 0., 0. } } }, T::Vec3) == Value::array({ 0., 0., 0. })); // zero-safe
    for (auto kind : { "SubtractVec4", "DotVec2", "DistanceVec4", "LengthVec3", "NormalizeVec4" }) CHECK(Visual::Catalog().contains(kind));
    // Text formatting for HUD and markers: fixed decimals and clock time.
    CHECK(Evaluate("FormatNumber", "Value", { { "Number", 3.0 }, { "Decimals", 0 } }, T::String) == "3");
    CHECK(Evaluate("FormatNumber", "Value", { { "Number", 2.346 }, { "Decimals", 2 } }, T::String) == "2.35");
    CHECK(Evaluate("FormatNumber", "Value", { { "Number", -0.2 }, { "Decimals", 0 } }, T::String) == "0");
    CHECK(Evaluate("FormatTime", "Value", { { "Seconds", 192.7 } }, T::String) == "03:12");
    CHECK(Evaluate("FormatTime", "Value", { { "Seconds", 3725 } }, T::String) == "1:02:05");
    CHECK(Evaluate("FormatTime", "Value", { { "Seconds", -4 } }, T::String) == "00:00");
}
TEST(GameplaySteerTwoDThreeDAndCameraRoundtripThroughTheMotorMapping)
{
    Scene scene;
    auto actor = scene.CreateEntityWithUUID(UUID(10), "Pilot");
    auto camera = scene.CreateEntityWithUUID(UUID(11), "Camera");
    scene.OnRuntimeStart();
    G::Runtime runtime;
    auto steer = [&](Value goal, bool twoD, double arrive = .3, double slow = 1, uint64_t cam = 0) { return runtime.Dispatch(actor, actor, "Gameplay.Steer", { { "Goal", goal }, { "Arrive", arrive }, { "Slow", slow }, { "TwoD", twoD }, { "Camera", cam } }); };
    auto r = steer({ 3., 4., 0. }, true);
    CHECK_NEAR(r["Move"][0].get<double>(), .6, 1e-6);
    CHECK_NEAR(r["Move"][1].get<double>(), .8, 1e-6);
    CHECK(r["Distance"] == 5.0 && r["Arrived"] == false);
    CHECK_NEAR(r["Yaw"].get<double>(), std::atan2(4., 3.), 1e-6);
    // Inside Slow the stick shrinks with the distance; inside Arrive it is zero.
    r = steer({ .5, 0., 0. }, true, .1, 1);
    CHECK_NEAR(r["Move"][0].get<double>(), .5, 1e-6);
    r = steer({ .05, 0., 0. }, true, .1, 1);
    CHECK(r["Arrived"] == true && r["Move"] == Value::array({ 0., 0. }));
    // 3D without a camera: the inverse of Motor3D's world mapping (x -> x, y -> -z).
    r = steer({ 3., 9., -4. }, false);
    CHECK_NEAR(r["Move"][0].get<double>(), .6, 1e-6);
    CHECK_NEAR(r["Move"][1].get<double>(), .8, 1e-6);
    CHECK_NEAR(r["Yaw"].get<double>(), std::atan2(3., -4.), 1e-6);
    // 3D with an orbit camera: the stick maps back through right/forward to the exact world direction.
    for (float yaw : { 0.f, .7f, -2.4f, 3.1f })
    {
        camera.GetTransform().Rotation = { -.35f, yaw, 0 };
        scene.InvalidateWorldTransforms();
        const glm::vec3 goal{ -2, 0, 5 };
        r = steer({ goal.x, goal.y, goal.z }, false, .3, 1, 11);
        const glm::vec2 stick{ float(r["Move"][0].get<double>()), float(r["Move"][1].get<double>()) };
        const auto m = scene.GetWorldTransform(camera);
        const auto right = glm::normalize(glm::vec2(m[0].x, m[0].z)), forward = glm::normalize(glm::vec2(-m[2].x, -m[2].z));
        const auto world = right * stick.x + forward * stick.y;
        const auto expected = glm::normalize(glm::vec2(goal.x, goal.z));
        CHECK_NEAR(world.x, expected.x, 1e-5f);
        CHECK_NEAR(world.y, expected.y, 1e-5f);
        CHECK(glm::length(stick) <= 1.00001f);
    }
    CHECK(Throws([&] { steer({ 1., 0., 0. }, true, -1); }) && Throws([&] { steer({ 1., 0., 0. }, true, .3, 101); }));
    CHECK(Throws([&] { steer({ std::nan(""), 0., 0. }, true); }));
    scene.OnRuntimeStop();
}
TEST(GameUIModelEnergyAbilityLevelFormatting)
{
    auto p = G::PresentAbility(0, 6, true, "Nova ");
    CHECK(p.Text == "Nova " && p.Fraction == 1);
    p = G::PresentAbility(4.2, 6, true, "Nova ");
    CHECK(p.Text == "Nova 5" && std::abs(p.Fraction - .3f) < 1e-6f);
    p = G::PresentAbility(.01, 6, true, "");
    CHECK(p.Text == "1");
    p = G::PresentAbility(3, 6, false, "Mend ");
    CHECK(p.Text == "Mend --" && p.Fraction == 0);
    CHECK(G::PresentAbility(0, 0, true, "X").Fraction == 1);
    CHECK(Throws([] { G::PresentAbility(-1, 1, true, ""); }) && Throws([] { G::PresentAbility(1, std::nan(""), true, ""); }) && Throws([] { G::PresentAbility(0, 1, true, std::string(65, 'p')); }));
    p = G::PresentLevel(3, 50, 300, "Daraja ");
    CHECK(p.Text == "Daraja 3" && std::abs(p.Fraction - 1.f / 6) < 1e-6f);
    CHECK(G::PresentLevel(1, 0, 0, "").Fraction == 0);
}
TEST(GameUIHUDKinds789FollowEnergyCooldownsUnlocksAndLevels)
{
    Scene scene;
    auto hero = scene.CreateEntityWithUUID(UUID(20), "Hero");
    std::vector<Entity> widgets;
    for (int i = 0; i < 4; ++i)
    {
        auto w = scene.CreateEntityWithUUID(UUID(30 + i), "Widget");
        w.AddComponent<UITransformComponent>();
        w.AddComponent<TextComponent>();
        w.AddComponent<UIProgressBarComponent>();
        widgets.push_back(w);
    }
    scene.OnRuntimeStart();
    std::string error;
    auto& rules = G::RuleWorld::For(scene);
    CHECK(rules.Configure(hero, { 100, 0, 0, 0, 1 }, false, error));
    auto& loadout = G::LoadoutWorld::For(scene);
    CHECK(loadout.Configure(hero, { 0, 0, 0, 60, 0 }, error));
    CHECK(loadout.DefineItem(hero, { "Charm", G::EquipSlot::Trinket, { 0, 0, 0, 20, 0 } }, error));
    G::AbilitySpec mend;
    mend.Key = "Mend";
    mend.Kind = G::AbilityKind::Heal;
    mend.Cost = 20;
    mend.Cooldown = 10;
    CHECK(loadout.DefineAbility(hero, mend, error));
    mend.Key = "Nova";
    mend.Unlocked = false;
    CHECK(loadout.DefineAbility(hero, mend, error));
    auto& quests = G::QuestWorld::For(scene);
    G::QuestSpec q;
    q.Key = "Gate";
    q.Objectives = { { "Chief", "Boshliq", "", G::QuestKind::Manual, 1 } };
    q.XP = 150;
    CHECK(quests.Configure(hero, q, error));
    auto& ui = G::UIWorld::For(scene);
    auto bind = [&](int index, G::HUDKind kind, const char* item, const char* prefix, bool ratio) { CHECK(ui.BindHUD(hero, hero, widgets[index], { kind, item, prefix, ratio }, error)); };
    bind(0, G::HUDKind::Energy, "", "EN ", true);
    bind(1, G::HUDKind::Ability, "Mend", "Mend ", false);
    bind(2, G::HUDKind::Ability, "Nova", "Nova ", false);
    bind(3, G::HUDKind::Level, "", "Daraja ", false);
    auto text = [&](int i) { return widgets[i].GetComponent<TextComponent>().Text; };
    auto bar = [&](int i) { return widgets[i].GetComponent<UIProgressBarComponent>().Value; };
    ui.Refresh();
    CHECK(text(0) == "EN 60 / 60" && bar(0) == 1 && text(1) == "Mend " && bar(1) == 1 && text(2) == "Nova --" && bar(2) == 0 && text(3) == "Daraja 1" && bar(3) == 0);
    // Equipping raises the effective maximum; a cast starts the cooldown fill; an unlock shows the slot ready.
    CHECK(rules.ChangeItem(hero, "Charm", 1, false, error) && loadout.Equip(hero, "Charm", error));
    rules.Damage(hero, {}, 30, true, false, true);
    CHECK(loadout.Cast(hero, hero, "Mend", {}).Success);
    CHECK(loadout.Unlock(hero, "Nova", true, error));
    ui.Refresh();
    CHECK(text(0) == "EN 40 / 80" && bar(0) == .5f && text(1) == "Mend 10" && bar(1) == 0 && text(2) == "Nova ");
    loadout.Tick(2.5);
    ui.Refresh();
    CHECK(text(1) == "Mend 8" && std::abs(bar(1) - .25f) < 1e-5f);
    // A level-up: 150 XP is level 2 with 50 of 200 towards level 3.
    CHECK(quests.Start(hero, "Gate", error) && quests.Advance(hero, "Gate", "Chief", 1, "c", error) && quests.Claim(hero, "Gate", error));
    ui.Refresh();
    CHECK(text(3) == "Daraja 2" && std::abs(bar(3) - .25f) < 1e-5f);
    // Kind 8 needs an ability key; an unknown ability shows the fallback.
    CHECK(!ui.BindHUD(hero, hero, scene.CreateEntity(), { G::HUDKind::Ability, "", "X", false }, error));
    CHECK(ui.UnbindHUD(widgets[2]));
    bind(2, G::HUDKind::Ability, "Missing", "Q ", false);
    ui.Refresh();
    CHECK(text(2) == "Q --");
    scene.OnRuntimeStop();
}
TEST(VisualGraphAppNodesFadeSettingsAndUIValues)
{
    const auto prior = Project::GetActive();
    const auto root = std::filesystem::temp_directory_path() / ("zamin-slice-nodes-" + std::to_string(uint64_t(UUID())));
    std::filesystem::create_directories(root / "Assets/Scripts");
    Project::New();
    CHECK(Project::SaveActive(root / "Slice.zproject"));
    auto scene = CreateRef<Scene>();
    auto host = scene->CreateEntityWithUUID(UUID(40), "Host");
    auto slider = scene->CreateEntityWithUUID(UUID(41), "Slider");
    slider.AddComponent<UITransformComponent>();
    auto& s = slider.AddComponent<UISliderComponent>();
    s.MinValue = 0;
    s.MaxValue = 1;
    s.Value = .25f;
    auto toggle = scene->CreateEntityWithUUID(UUID(42), "Toggle");
    toggle.AddComponent<UITransformComponent>();
    toggle.AddComponent<UIToggleComponent>();
    // Start: read the slider, write 3.0 (clamped to 1) and the toggle, read back; an old-style LoadScene (no fade
    // pins) requests the scene at once exactly as before.
    Visual::Graph g;
    g.Variables["Before"] = { Visual::Type::Number, 0 };
    g.Variables["After"] = { Visual::Type::Number, 0 };
    g.Variables["On"] = { Visual::Type::Bool, false };
    g.Variables["Demo"] = { Visual::Type::Bool, true };
    g.Variables["Missing"] = { Visual::Type::String, "" };
    g.Variables["Group"] = { Visual::Type::Number, -1 };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Set.Number", 0, 0, { { "Name", "Before" } } }, { 3, "UIValue", 0, 0, { { "Target", 41 } } }, { 4, "SetUIValue", 0, 0, { { "Target", 41 }, { "Value", 3 } } }, { 5, "SetUIValue", 0, 0, { { "Target", 42 }, { "Value", 1 } } }, { 6, "Set.Number", 0, 0, { { "Name", "After" } } }, { 7, "UIValue", 0, 0, { { "Target", 41 } } }, { 8, "Set.Bool", 0, 0, { { "Name", "On" } } }, { 9, "UIValue", 0, 0, { { "Target", 42 } } }, { 10, "Set.Bool", 0, 0, { { "Name", "Demo" } } }, { 11, "App.DemoMode" }, { 12, "Set.String", 0, 0, { { "Name", "Missing" } } }, { 13, "Localization.Text", 0, 0, { { "Key", "menu.none" }, { "Fallback", "Yangi o'yin" } } }, { 14, "LoadScene", 0, 0, { { "Path", "Scenes/Next.zscene" } } }, { 15, "Set.Number", 0, 0, { { "Name", "Group" } } }, { 16, "Audio.GroupVolume", 0, 0, { { "Group", "Music" } } } };
    g.Links = { { 1, "Then", 2, "In" }, { 3, "Value", 2, "Value" }, { 2, "Then", 4, "In" }, { 4, "Then", 5, "In" }, { 5, "Then", 6, "In" }, { 7, "Value", 6, "Value" }, { 6, "Then", 8, "In" }, { 9, "On", 8, "Value" }, { 8, "Then", 10, "In" }, { 11, "Value", 10, "Value" }, { 10, "Then", 12, "In" }, { 13, "Value", 12, "Value" }, { 12, "Then", 15, "In" }, { 16, "Value", 15, "Value" }, { 15, "Then", 14, "In" } };
    CHECK(g.Validate().empty());
    CHECK(g.Save(root / "Assets/Scripts/Nodes.zgraph"));
    auto& script = host.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/Nodes.zgraph"));
    scene->OnRuntimeStart();
    scene->UpdateRuntime(1.f / 60);
    auto* graph = dynamic_cast<VisualGraphHost*>(script.Instance);
    CHECK(graph && graph->VM.Error().empty());
    if (graph)
    {
        auto& v = graph->VM.Variables();
        CHECK(v.at("Before") == .25 && v.at("After") == 1.0 && v.at("On") == true);
        CHECK(v.at("Demo") == bool(std::getenv("ZAMIN_DEMO") && std::string(std::getenv("ZAMIN_DEMO")) != "0"));
        CHECK(v.at("Missing") == "Yangi o'yin");
        CHECK(v.at("Group").get<double>() >= 0 && v.at("Group").get<double>() <= 2);
    }
    CHECK(slider.GetComponent<UISliderComponent>().Value == 1 && toggle.GetComponent<UIToggleComponent>().IsOn);
    CHECK(scene->GetPendingSceneLoad() == std::filesystem::path("Scenes/Next.zscene"));
    scene->OnRuntimeStop();
    // Fade bounds are validated; the catalogue keeps the old LoadScene pin defaults (instant switch).
    const auto& load = Visual::Catalog().at("LoadScene");
    CHECK(load.Inputs.size() == 5 && load.Inputs[2].Default == 0.0 && load.Inputs[3].Default == -1.0 && load.Inputs[4].Default == Value::array({ 0, 0, 0, 1 }));
    for (auto kind : { "QuitGame", "Audio.SetGroupVolume", "FadeAudio", "Localization.SetLanguage", "Localization.Language", "Display.SetFullscreen", "Display.IsFullscreen", "PlaySpriteClip", "ShakeCamera", "Gameplay.TransferWrite", "Gameplay.Steer" }) CHECK(Visual::Catalog().contains(kind));
    CHECK(Visual::Catalog().at("Gameplay.AIConfigure").Inputs.size() > 0);
    bool strike = false;
    for (auto& pin : Visual::Catalog().at("Gameplay.AIConfigure").Inputs) strike |= pin.Name == "Strike" && pin.Default == true;
    CHECK(strike);
    std::filesystem::remove_all(root);
    Project::SetActive(prior);
}
// A settings menu keeps the player's language and display choice: Localization.SaveSettings / Display.SaveSettings
// write them to SaveData (the player and the editor's Play load them at the next start).
TEST(VisualGraphSettingsNodesSaveTheLanguageAndDisplayChoice)
{
    const auto prior = Project::GetActive();
    const auto root = std::filesystem::temp_directory_path() / ("zamin-slice-settings-" + std::to_string(uint64_t(UUID())));
    std::filesystem::create_directories(root / "Assets/Scripts");
    Project::New();
    CHECK(Project::SaveActive(root / "Slice.zproject"));
    SaveData::SetFilePath(root / "prefs.json");
    Localization::ClearSavedSettings();
    Display::ClearSavedSettings();
    std::vector<StringTable> tables(2);
    tables[0].Language = "uz";
    tables[0].Strings["menu.new"] = { "Yangi o'yin", {} };
    tables[1].Language = "en";
    tables[1].Strings["menu.new"] = { "New game", {} };
    LocalizationSettings languages;
    languages.Languages = { "uz", "en" };
    Localization::SetTables(std::move(tables), languages);
    auto scene = CreateRef<Scene>();
    auto host = scene->CreateEntityWithUUID(UUID(40), "Host");
    Visual::Graph g;
    g.Variables["Saved"] = { Visual::Type::Bool, false };
    g.Nodes = { { 1, "Event.Start" }, { 2, "Localization.SetLanguage", 0, 0, { { "Code", "en" } } }, { 3, "Localization.SaveSettings" },
                { 4, "Display.SetFullscreen", 0, 0, { { "Fullscreen", true } } }, { 5, "Display.SaveSettings" }, { 6, "Set.Bool", 0, 0, { { "Name", "Saved" } } },
                { 7, "And" } };
    g.Links = { { 1, "Then", 2, "In" }, { 2, "Then", 3, "In" }, { 3, "Then", 4, "In" }, { 4, "Then", 5, "In" }, { 5, "Then", 6, "In" },
                { 3, "Success", 7, "A" }, { 5, "Success", 7, "B" }, { 7, "Value", 6, "Value" } };
    CHECK(g.Validate().empty());
    CHECK(g.Save(root / "Assets/Scripts/Settings.zgraph"));
    auto& script = host.AddComponent<NativeScriptComponent>();
    script.ClassName = "Zamin.VisualGraph";
    script.Fields["Graph"].Set(std::string("Scripts/Settings.zgraph"));
    scene->OnRuntimeStart();
    scene->UpdateRuntime(1.f / 60);
    auto* graph = dynamic_cast<VisualGraphHost*>(script.Instance);
    CHECK(graph && graph->VM.Error().empty() && graph->VM.Variables().at("Saved") == true);
    scene->OnRuntimeStop();
    // the next start: back to the default language and a window, then what was saved
    CHECK(Localization::SetLanguage("uz") && Localization::HasSavedSettings() && Localization::LoadSettings() && Localization::GetLanguage() == "en");
    CHECK(Display::HasSavedSettings() && Display::GetSavedSettings().Fullscreen == true);
    CHECK(Visual::Catalog().contains("Localization.SaveSettings") && Visual::Catalog().contains("Display.SaveSettings"));
    Localization::ClearSavedSettings();
    Display::ClearSavedSettings();
    Display::ResetOptions(PlayerSettings());
    Localization::Clear();
    SaveData::SetFilePath({});
    std::filesystem::remove_all(root);
    Project::SetActive(prior);
}
