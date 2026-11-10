#include <Zamin/Zamin.h>
#include <Zamin/Editor/EditorLanguage.h>
#include <Zamin/ImGui/Icons.h>
#include "TestAssertions.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
using namespace Zamin;

// ------------------------------------------------------------------ M16 Editor II
// The console's filter: text (case-insensitive substring, case-sensitive, regex), level toggles with
// counts that follow the text, collapsing identical messages (level + source + text) with a count.
TEST(ConsoleFilterAndCollapse)
{
    auto msg = [](LogLevel level, bool client, const char* time, const char* text) {
        LogMessage m;
        m.Level = level;
        m.FromClient = client;
        m.Time = time;
        m.Text = text;
        return m;
    };
    std::vector<LogMessage> log = {
        msg(LogLevel::Info, true, "10:00:00", "Sahna yuklandi"),            // 0
        msg(LogLevel::Warn, true, "10:00:01", "Tanga topilmadi"),           // 1
        msg(LogLevel::Trace, false, "10:00:02", "trace xabari"),            // 2
        msg(LogLevel::Warn, true, "10:00:03", "Tanga topilmadi"),           // 3
        msg(LogLevel::Error, false, "10:00:04", "Xato: fayl o'qilmadi"),    // 4
        msg(LogLevel::Warn, false, "10:00:05", "Tanga topilmadi"),          // 5 (another source)
        msg(LogLevel::Info, true, "10:00:06", "sahna YUKLANDI\nikkinchi qator"), // 6
        msg(LogLevel::Warn, true, "10:00:07", "Tanga topilmadi"),           // 7
        msg(LogLevel::Critical, false, "10:00:08", "Jiddiy xato"),          // 8
    };

    ConsoleFilterSettings filter;
    ConsoleTextMatcher matcher;
    // Default: everything but Trace, one row per message.
    ConsoleView view = BuildConsoleView(log, filter, matcher);
    CHECK(view.Rows.size() == 8);
    CHECK(view.LevelCounts[(int)LogLevel::Trace] == 1);
    CHECK(view.LevelCounts[(int)LogLevel::Warn] == 4);
    CHECK(view.ErrorCount() == 2);
    CHECK(view.Rows[0].First == 0 && view.Rows[0].Count == 1);
    filter.ShowTrace = true;
    CHECK(BuildConsoleView(log, filter, matcher).Rows.size() == 9);

    // Case-insensitive substring by default; case-sensitive on request.
    matcher.Set("sahna", false, false);
    view = BuildConsoleView(log, filter, matcher);
    CHECK(view.Rows.size() == 2);
    CHECK(view.LevelCounts[(int)LogLevel::Info] == 2 && view.LevelCounts[(int)LogLevel::Warn] == 0);
    matcher.Set("sahna", false, true);
    view = BuildConsoleView(log, filter, matcher);
    CHECK(view.Rows.size() == 1 && view.Rows[0].First == 6);
    // Level toggles hide rows but the counts still tell what matches.
    matcher.Set("tanga", false, false);
    filter.ShowWarn = false;
    view = BuildConsoleView(log, filter, matcher);
    CHECK(view.Rows.empty());
    CHECK(view.LevelCounts[(int)LogLevel::Warn] == 4);
    filter.ShowWarn = true;

    // Collapse: the three identical APP warnings are one row at the first one's place with a count of
    // 3 (the latest is index 7); the same text from the engine is a row of its own.
    filter.Collapse = true;
    view = BuildConsoleView(log, filter, matcher);
    CHECK(view.Rows.size() == 2);
    CHECK(view.Rows[0].First == 1 && view.Rows[0].Last == 7 && view.Rows[0].Count == 3);
    CHECK(view.Rows[1].First == 5 && view.Rows[1].Count == 1);
    matcher.Set("", false, false);
    view = BuildConsoleView(log, filter, matcher);
    CHECK(view.Rows.size() == 7); // 9 - 2 repeats
    for (size_t i = 1; i < view.Rows.size(); i++)
        CHECK(view.Rows[i - 1].First < view.Rows[i].First); // still in log order
    filter.Collapse = false;

    // Regular expressions (case-insensitive unless asked); an invalid one filters nothing.
    matcher.Set("^xato:\\s+\\w+", true, false);
    CHECK(matcher.IsValid());
    view = BuildConsoleView(log, filter, matcher);
    CHECK(view.Rows.size() == 1 && view.Rows[0].First == 4);
    matcher.Set("^xato:", true, true);
    CHECK(BuildConsoleView(log, filter, matcher).Rows.empty());
    matcher.Set("topilmadi|jiddiy", true, false);
    CHECK(BuildConsoleView(log, filter, matcher).Rows.size() == 5);
    matcher.Set("([", true, false);
    CHECK(!matcher.IsValid() && !matcher.GetError().empty());
    CHECK(BuildConsoleView(log, filter, matcher).Rows.size() == 9);
    // The same text as plain search finds the literal characters.
    matcher.Set("([", false, false);
    CHECK(matcher.IsValid());
    CHECK(BuildConsoleView(log, filter, matcher).Rows.empty());

    // Long messages: std::regex recurses per character, 60 000 of them overflowed the stack. A long
    // one is searched in a single pass (its first RegexTextLimit bytes), or (a back-reference) only
    // its first RegexDirectLimit bytes.
    {
        const std::string longText(60000, 'a');
        const std::string endsWithX = std::string(10000, 'a') + "x";
        matcher.Set(".*x", true, false);
        CHECK(matcher.IsValid());
        CHECK(!matcher.Matches(longText));
        CHECK(matcher.Matches(endsWithX));
        CHECK(matcher.Matches("abcx") && !matcher.Matches("abc"));
        matcher.Set("(a|b|c|d)+x", true, false);
        CHECK(!matcher.Matches(longText) && matcher.Matches(endsWithX));
        matcher.Set("^a+$", true, false);
        CHECK(matcher.Matches(endsWithX.substr(0, 5000)) && !matcher.Matches(endsWithX));
        matcher.Set("X$", true, true); // case-sensitive: the lower-case x does not count
        CHECK(!matcher.Matches(endsWithX));
        matcher.Set("x$", true, true);
        CHECK(matcher.Matches(endsWithX));
        // Past the limit nothing is looked at; the plain search still sees all of it.
        const std::string farX = std::string(ConsoleTextMatcher::RegexTextLimit + 10, 'a') + "x";
        matcher.Set("x", true, false);
        CHECK(!matcher.Matches(farX));
        matcher.Set("x", false, false);
        CHECK(matcher.Matches(farX));
        // Back-references: only the beginning of a long message (no crash either way).
        matcher.Set("(a)\\1", true, false);
        CHECK(matcher.IsValid() && !matcher.HasLongTextPattern());
        CHECK(matcher.Matches(longText));
        matcher.Set("(a)\\1x", true, false);
        CHECK(!matcher.Matches(longText) && !matcher.Matches(endsWithX));
        CHECK(matcher.Matches("aax"));
#if defined(__GLIBCXX__)
        matcher.Set("tanga|sahna", true, false);
        CHECK(matcher.HasLongTextPattern());
#endif
        std::vector<LogMessage> big = { msg(LogLevel::Info, true, "10:00:09", longText.c_str()), msg(LogLevel::Warn, true, "10:00:10", endsWithX.c_str()) };
        matcher.Set(".*x", true, false);
        view = BuildConsoleView(big, filter, matcher);
        CHECK(view.Rows.size() == 1 && view.Rows[0].First == 1);
    }

    CHECK(ContainsIgnoreCase("Sahna YUKLANDI", "yuklandi"));
    CHECK(ContainsIgnoreCase("abc", ""));
    CHECK(!ContainsIgnoreCase("abc", "abcd"));
    CHECK(FormatConsoleMessage(log[1], true) == "[10:00:01] [WARN] APP: Tanga topilmadi");
    CHECK(FormatConsoleMessage(log[4], false, 3) == "[ERROR] ZAMIN: Xato: fayl o'qilmadi (x3)");

    // The log's revision changes with every message and Clear; messages keep their numbers.
    const LogLevel minLevel = Log::GetMinLevel();
    const bool terminal = Log::GetTerminalOutput();
    Log::SetMinLevel(LogLevel::Trace);
    Log::SetTerminalOutput(false);
    Log::Clear();
    const uint64_t revision = Log::GetRevision();
    const uint64_t written = Log::GetTotalWritten();
    ZM_INFO("konsol testi {}", 1);
    ZM_WARN("konsol testi {}", 2);
    CHECK(Log::GetRevision() == revision + 2);
    CHECK(Log::GetTotalWritten() == written + 2);
    uint64_t first = 0;
    std::vector<LogMessage> messages = Log::GetMessages(&first);
    CHECK(messages.size() == 2 && first == written);
    CHECK(messages.size() == 2 && messages[1].Text == "konsol testi 2" && messages[1].Level == LogLevel::Warn && messages[1].FromClient);
    Log::Clear();
    CHECK(Log::GetRevision() == revision + 3);
    CHECK(Log::GetMessageCount() == 0);
    Log::SetTerminalOutput(terminal);
    Log::SetMinLevel(minLevel);
}

// The game view's resolution presets: letterboxing to an aspect ratio, fixed resolutions 1:1 or
// scaled down to fit, the mouse mapped into the game's pixels, the settings' keys and JSON.
TEST(GameViewResolutionLayout)
{
    GameViewResolution res;
    // Free: the whole (whole-pixel) area.
    GameViewLayout l = ComputeGameViewLayout(res, { 1000.7f, 600.2f });
    CHECK(l.Offset == glm::vec2(0.0f) && l.DisplaySize == glm::vec2(1000.0f, 600.0f));
    CHECK(l.RenderSize == glm::uvec2(1000, 600) && l.Scale == 1.0f);
    CHECK(res.GetLabel() == "Erkin" && !res.IsFixed() && !res.IsAspect());

    // 16:9 in a tall area: bars above and below; in a wide one: left and right.
    res.Preset = GameViewPreset::Aspect16x9;
    l = ComputeGameViewLayout(res, { 1000.0f, 700.0f });
    CHECK(l.DisplaySize == glm::vec2(1000.0f, 563.0f));
    CHECK(l.Offset == glm::vec2(0.0f, 68.0f));
    CHECK(l.RenderSize == glm::uvec2(1000, 563) && l.Scale == 1.0f);
    l = ComputeGameViewLayout(res, { 1600.0f, 700.0f });
    CHECK(l.DisplaySize == glm::vec2(1244.0f, 700.0f) && l.Offset == glm::vec2(178.0f, 0.0f));
    CHECK(res.GetLabel() == "16:9" && res.IsAspect());
    res.Preset = GameViewPreset::Aspect4x3;
    l = ComputeGameViewLayout(res, { 800.0f, 600.0f });
    CHECK(l.DisplaySize == glm::vec2(800.0f, 600.0f) && l.Offset == glm::vec2(0.0f));

    // A fixed resolution that fits: centred pixel for pixel.
    res.Preset = GameViewPreset::Fixed1280x720;
    l = ComputeGameViewLayout(res, { 1500.0f, 900.0f });
    CHECK(l.RenderSize == glm::uvec2(1280, 720) && l.DisplaySize == glm::vec2(1280.0f, 720.0f));
    CHECK(l.Offset == glm::vec2(110.0f, 90.0f) && l.Scale == 1.0f);
    CHECK(l.AreaToRender({ 110.0f, 90.0f }) == glm::vec2(0.0f));
    // Too big: drawn at its size, shown scaled down to fit.
    res.Preset = GameViewPreset::Fixed1920x1080;
    l = ComputeGameViewLayout(res, { 960.0f, 700.0f });
    CHECK(l.RenderSize == glm::uvec2(1920, 1080));
    CHECK(l.DisplaySize == glm::vec2(960.0f, 540.0f) && l.Offset == glm::vec2(0.0f, 80.0f));
    CHECK_NEAR(l.Scale, 0.5f, 1e-6f);
    // The mouse: the picture's centre on screen is the game's centre pixel; the bars are outside.
    CHECK(l.AreaToRender({ 480.0f, 350.0f }) == glm::vec2(960.0f, 540.0f));
    CHECK(l.Contains({ 480.0f, 350.0f }) && !l.Contains({ 480.0f, 40.0f }) && !l.Contains({ 480.0f, 660.0f }));
    const glm::vec2 above = l.AreaToRender({ 10.0f, 10.0f });
    CHECK(above.y < 0.0f);
    CHECK(res.GetLabel() == "1920x1080" && res.IsFixed());

    // Custom: pixels or only an aspect ratio; sizes kept in range.
    res.Preset = GameViewPreset::Custom;
    res.CustomSize = { 800, 600 };
    CHECK(res.GetLabel() == "800x600" && res.IsFixed());
    res.CustomAspect = true;
    res.CustomSize = { 5, 4 };
    CHECK(res.GetLabel() == "5:4" && res.IsAspect());
    l = ComputeGameViewLayout(res, { 1000.0f, 1000.0f });
    CHECK(l.DisplaySize == glm::vec2(1000.0f, 800.0f) && l.Offset == glm::vec2(0.0f, 100.0f));
    res.CustomSize = { 0, 100000 };
    res.Sanitize();
    CHECK(res.CustomSize == glm::ivec2(1, GameViewResolution::MaxSize));
    res.CustomAspect = false; // as pixels: at least MinSize
    CHECK(res.GetSize() == glm::ivec2(GameViewResolution::MinSize, GameViewResolution::MaxSize));
    res.CustomAspect = true;
    // A degenerate area still gives a usable picture.
    l = ComputeGameViewLayout(res, { 0.0f, -5.0f });
    CHECK(l.RenderSize.x >= 1 && l.RenderSize.y >= 1);

    // Every preset's key reads back; unknown keys fall back.
    for (int i = 0; i < GameViewPresetCount; i++)
        CHECK(GameViewResolution::FromKey(GameViewResolution::GetKey((GameViewPreset)i), GameViewPreset::Free) == (GameViewPreset)i);
    CHECK(GameViewResolution::FromKey("640x480", GameViewPreset::Free) == GameViewPreset::Free);

    // Stored with the editor's view settings.
    EditorViewSettings settings;
    settings.GameView.Preset = GameViewPreset::Custom;
    settings.GameView.CustomSize = { 1024, 768 };
    settings.GameView.CustomAspect = false;
    EditorViewSettings loaded;
    CHECK(loaded.FromJson(settings.ToJson()));
    CHECK(loaded.GameView == settings.GameView);
    settings.GameView = {};
    settings.GameView.Preset = GameViewPreset::Aspect16x10;
    CHECK(loaded.FromJson(settings.ToJson()) && loaded.GameView.Preset == GameViewPreset::Aspect16x10);
}

// The editor preferences: JSON round trip, ranges, the play-mode tint's maths, the accent presets
// and where the file lives.
TEST(EditorPreferencesSettings)
{
    EditorPreferences prefs;
    CHECK(prefs.Theme.FindAccentPreset() == 0);
    CHECK(prefs.Theme.GetUIScale() == 1.0f);
    prefs.Theme.Accent = EditorThemeSettings::GetAccentPresets()[2].Color;
    prefs.Theme.FontSize = 18.0f;
    prefs.PlayTint.Color = { 0.9f, 1.05f, 1.6f };
    prefs.PlayTint.Strength = 0.5f;
    prefs.PlayTint.InSimulate = true;
    prefs.Camera.FOV = 60.0f;
    prefs.Camera.NearClip = 0.05f;
    prefs.Camera.FarClip = 5000.0f;
    prefs.Camera.FlySpeed = 12.0f;
    prefs.Console.ClearOnPlay = true;
    prefs.Console.Collapse = true;
    prefs.Console.ShowTimestamps = false;
    prefs.Console.ShowTrace = true;

    EditorPreferences loaded;
    CHECK(loaded.FromJson(prefs.ToJson()));
    CHECK(loaded.Theme.FindAccentPreset() == 2);
    CHECK(loaded.Theme.FontSize == 18.0f);
    CHECK_NEAR(loaded.Theme.GetUIScale(), 1.125f, 1e-6f);
    CHECK(glm::all(glm::lessThan(glm::abs(loaded.PlayTint.Color - prefs.PlayTint.Color), glm::vec3(1e-5f))));
    CHECK(loaded.PlayTint.FindPreset() == 1);
    CHECK_NEAR(loaded.PlayTint.Strength, 0.5f, 1e-6f);
    CHECK(loaded.PlayTint.InSimulate && loaded.PlayTint.Enabled);
    CHECK_NEAR(loaded.Camera.FOV, 60.0f, 1e-4f);
    CHECK_NEAR(loaded.Camera.NearClip, 0.05f, 1e-6f);
    CHECK_NEAR(loaded.Camera.FarClip, 5000.0f, 1e-3f);
    CHECK_NEAR(loaded.Camera.FlySpeed, 12.0f, 1e-5f);
    CHECK(loaded.Console.ClearOnPlay && loaded.Console.Collapse && !loaded.Console.ShowTimestamps && loaded.Console.ShowTrace);

    // Not a preferences file: nothing changes; missing sections keep their values.
    CHECK(!loaded.FromJson("{\"ZaminEditorView\": 1}"));
    CHECK(!loaded.FromJson("not json"));
    CHECK(loaded.Theme.FontSize == 18.0f);
    CHECK(loaded.FromJson("{\"ZaminEditorPreferences\": 1, \"Theme\": {\"FontSize\": 14}}"));
    CHECK(loaded.Theme.FontSize == 14.0f && loaded.Theme.FindAccentPreset() == 2 && loaded.Console.Collapse);

    // Out-of-range values are brought into range when read.
    CHECK(loaded.FromJson("{\"ZaminEditorPreferences\": 1, \"Theme\": {\"FontSize\": 40, \"Accent\": [2, -1, 0.5]},"
                          " \"PlayModeTint\": {\"Color\": [5, 1, 1], \"Strength\": 3},"
                          " \"EditorCamera\": {\"FOV\": 500, \"Near\": 1, \"Far\": 2, \"FlySpeed\": 0}}"));
    CHECK(loaded.Theme.FontSize == EditorThemeSettings::MaxFontSize);
    CHECK(loaded.Theme.Accent == glm::vec3(1.0f, 0.0f, 0.5f));
    CHECK(loaded.PlayTint.Color.x == PlayModeTintSettings::MaxChannel && loaded.PlayTint.Strength == 1.0f);
    CHECK(loaded.Camera.FOV == EditorCameraDefaults::MaxFOV);
    CHECK(loaded.Camera.FarClip >= loaded.Camera.NearClip * 10.0f - 1e-4f);
    CHECK(loaded.Camera.FlySpeed == EditorCameraDefaults::MinFlySpeed);
    EditorThemeSettings theme;
    theme.FontSize = 13.4f;
    theme.Accent = glm::vec3(std::numeric_limits<float>::quiet_NaN());
    theme.Sanitize();
    CHECK(theme.FontSize == 13.0f && theme.Accent == EditorThemeSettings::GetDefaultAccent());

    // The tint: active in play (simulation only when asked), a per-channel multiplier faded by its
    // strength, clamped colours with their alpha kept.
    PlayModeTintSettings tint;
    CHECK(tint.IsActive(true, false) && !tint.IsActive(false, true) && !tint.IsActive(false, false));
    tint.InSimulate = true;
    CHECK(tint.IsActive(false, true));
    tint.Enabled = false;
    CHECK(!tint.IsActive(true, false));
    tint.Enabled = true;
    tint.Color = { 1.5f, 1.0f, 0.5f };
    tint.Strength = 1.0f;
    glm::vec4 c = tint.Apply({ 0.1f, 0.1f, 0.1f, 0.5f });
    CHECK_NEAR(c.r, 0.15f, 1e-6f);
    CHECK_NEAR(c.g, 0.1f, 1e-6f);
    CHECK_NEAR(c.b, 0.05f, 1e-6f);
    CHECK(c.a == 0.5f);
    CHECK(tint.Apply({ 0.9f, 0.9f, 0.9f, 1.0f }).r == 1.0f); // clamped
    tint.Strength = 0.0f;
    CHECK(tint.GetMultiplier() == glm::vec3(1.0f));
    tint.Strength = 0.5f;
    CHECK_NEAR(tint.GetMultiplier().r, 1.25f, 1e-6f);
    CHECK_NEAR(tint.GetMultiplier().b, 0.75f, 1e-6f);
    // Every tint preset changes a mid grey (none of them is a no-op).
    for (const ColorPreset& preset : PlayModeTintSettings::GetPresets())
    {
        PlayModeTintSettings t;
        t.Color = preset.Color;
        CHECK(glm::vec3(t.Apply({ 0.3f, 0.3f, 0.3f, 1.0f })) != glm::vec3(0.3f));
    }

    // The accent's shades: lighter / darker, within 0..1.
    for (const ColorPreset& preset : EditorThemeSettings::GetAccentPresets())
    {
        const glm::vec3 hover = EditorThemeSettings::GetHoverShade(preset.Color);
        const glm::vec3 dark = EditorThemeSettings::GetDarkShade(preset.Color);
        CHECK(hover.r + hover.g + hover.b > preset.Color.r + preset.Color.g + preset.Color.b);
        CHECK(dark.r + dark.g + dark.b < preset.Color.r + preset.Color.g + preset.Color.b);
        CHECK(glm::all(glm::lessThanEqual(hover, glm::vec3(1.0f))) && glm::all(glm::greaterThanEqual(dark, glm::vec3(0.0f))));
    }

    // Stored in the user's config directory ($XDG_CONFIG_HOME/zamin first).
    const char* oldXdg = std::getenv("XDG_CONFIG_HOME");
    std::string savedXdg = oldXdg ? oldXdg : "";
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "zamin_prefs_test";
    setenv("XDG_CONFIG_HOME", dir.string().c_str(), 1);
    CHECK(EditorPreferences::GetDefaultPath() == dir / "zamin" / "preferences.json");
    CHECK(prefs.Save(EditorPreferences::GetDefaultPath()));
    EditorPreferences fromFile;
    CHECK(fromFile.Load(EditorPreferences::GetDefaultPath()) && fromFile.Theme.FontSize == 18.0f && fromFile.Console.ClearOnPlay);
    std::error_code ec;
    fs::remove_all(dir, ec);
    CHECK(!fromFile.Load(EditorPreferences::GetDefaultPath()));
    if (oldXdg)
        setenv("XDG_CONFIG_HOME", savedXdg.c_str(), 1);
    else
        unsetenv("XDG_CONFIG_HOME");
}

TEST(ShortcutRegistryLookup)
{
    // Chords as text: what files store and what menus show.
    KeyChord chord;
    CHECK(KeyChord::Parse("Ctrl+Shift+S", chord) && chord == KeyChord(Key::S, true, true));
    CHECK(chord.ToString() == "Ctrl+Shift+S" && chord.GetDisplayName() == "Ctrl+Shift+S");
    CHECK(KeyChord::Parse("shift+ctrl+s", chord) && chord == KeyChord(Key::S, true, true));
    CHECK(KeyChord::Parse("Ctrl+,", chord) && chord == KeyChord(Key::Comma, true));
    CHECK(chord.ToString() == "Ctrl+Comma" && chord.GetDisplayName() == "Ctrl+,");
    CHECK(KeyChord::Parse("Ctrl+Comma", chord) && chord.KeyCode == Key::Comma);
    CHECK(KeyChord::Parse("Alt+Shift+A", chord) && chord.Alt && chord.Shift && !chord.Ctrl && chord.KeyCode == Key::A);
    CHECK(KeyChord::Parse("Delete", chord) && chord.GetDisplayName() == "Del");
    CHECK(KeyChord::Parse("Num +", chord) && chord.KeyCode == Key::KPAdd);
    CHECK(KeyChord::Parse("Control+Num +", chord) && chord.KeyCode == Key::KPAdd && chord.Ctrl);
    CHECK(KeyChord::Parse("", chord) && chord.IsEmpty() && chord.ToString().empty());
    CHECK(!KeyChord::Parse("Ctrl+LeftShift", chord));
    CHECK(!KeyChord::Parse("Ctrl+Nimadir", chord));
    CHECK(!KeyChord::Parse("Super+S", chord));
    for (const char* text : { "Ctrl+Z", "Shift+Escape", "F1", "Ctrl+5", "KP7", "Alt+H", "Ctrl+Alt+Shift+F12" })
    {
        KeyChord a, b;
        CHECK(KeyChord::Parse(text, a) && KeyChord::Parse(a.ToString(), b) && a == b);
        CHECK(KeyChord::Parse(a.GetDisplayName(), b) && a == b);
    }

    // The editor's table: unique ids, no conflicts in the defaults, every rebindable one bound.
    ShortcutRegistry r;
    RegisterEditorShortcuts(r);
    CHECK(r.GetAll().size() > 50);
    std::set<std::string> ids;
    for (const ShortcutInfo& s : r.GetAll())
    {
        CHECK(ids.insert(s.Id).second);
        CHECK(!s.Name.empty() && !s.Category.empty() && !s.Display.empty());
        CHECK(s.Fixed || !s.Default.IsEmpty());
    }
    CHECK(r.CountConflicts() == 0);
    CHECK(r.CountModified() == 0);
    CHECK(std::string(r.GetDisplay(EditorShortcut::SaveSceneAs)) == "Ctrl+Shift+S");
    CHECK(std::string(r.GetDisplay(EditorShortcut::BookmarkSave(3))) == "Ctrl+3");
    CHECK(std::string(r.GetDisplay("nothing.here")).empty());
    CHECK(r.GetCategories().front() == "Fayl");

    // Lookup: the narrowest scope that applies now wins; Global ones whenever nothing narrower does.
    using namespace ShortcutSituation;
    auto match = [&](KeyChord c, uint32_t situations) {
        const ShortcutInfo* s = r.Match(c, situations | Editor);
        return s ? s->Id : std::string();
    };
    CHECK(match({ Key::S, true }, 0) == EditorShortcut::SaveScene);
    CHECK(match({ Key::S, true, true }, 0) == EditorShortcut::SaveSceneAs);
    CHECK(match({ Key::S }, 0).empty()); // plain S is nothing (the camera's WASD)
    CHECK(match({ Key::Q }, 0).empty());
    CHECK(match({ Key::Q }, SceneView | ViewportTools) == EditorShortcut::ToolNone);
    CHECK(match({ Key::Q }, SceneView | ViewportTile) == EditorShortcut::TileExitNone);
    CHECK(match({ Key::Q }, SceneView | ViewportHandles) == EditorShortcut::HandlesExitNone);
    CHECK(match({ Key::E }, SceneView | ViewportTile) == EditorShortcut::TileEraser);
    CHECK(match({ Key::F }, SceneView | ViewportTile) == EditorShortcut::FrameSelection);
    CHECK(match({ Key::F }, Animation) == "animation.frame");
    CHECK(match({ Key::Delete }, Hierarchy) == EditorShortcut::Delete);
    CHECK(match({ Key::Delete }, SceneView | ViewportTools) == EditorShortcut::Delete);
    CHECK(match({ Key::Delete }, SceneView | ViewportHandles) == "handles.delete_point"); // the point editor's own
    CHECK(match({ Key::Delete }, ContentBrowser) == "content.delete");
    CHECK(match({ Key::C, true }, 0) == EditorShortcut::Copy);
    CHECK(match({ Key::C, true }, Console) == "console.copy");
    CHECK(match({ Key::Escape, false, true }, 0).empty());
    CHECK(match({ Key::Escape, false, true }, Playing) == EditorShortcut::PlayStop);
    CHECK(match({ Key::D3 }, SceneView) == EditorShortcut::BookmarkRecall(3));
    CHECK(match({ Key::D3, true }, SceneView) == EditorShortcut::BookmarkSave(3));
    CHECK(r.Match({}, All) == nullptr);

    // Actions run through Bind; fixed entries take none.
    int ran = 0;
    CHECK(r.Bind(EditorShortcut::SaveScene, [&]() { ran++; }));
    CHECK(!r.Bind("console.copy", [&]() { ran++; }));
    CHECK(!r.Bind("nothing.here", [&]() {}));
    if (const ShortcutInfo* s = r.Match({ Key::S, true }, Editor); s && s->Action)
        s->Action();
    CHECK(ran == 1);

    // Rebinding: conflicts within the same scope (or Global with Global); a narrower scope over a
    // wider one is not one.
    CHECK(r.Rebind(EditorShortcut::ToolMove, { Key::E }));
    CHECK(r.FindConflicts(EditorShortcut::ToolMove).size() == 1 && r.FindConflicts(EditorShortcut::ToolMove)[0]->Id == EditorShortcut::ToolRotate);
    CHECK(r.CountConflicts() == 2);
    CHECK(r.Rebind(EditorShortcut::ToolMove, { Key::S, true }));  // Ctrl+S in the viewport tools: shadows Save there
    CHECK(r.FindConflicts(EditorShortcut::ToolMove).empty() && r.CountConflicts() == 0);
    CHECK(match({ Key::S, true }, SceneView | ViewportTools) == EditorShortcut::ToolMove);
    CHECK(match({ Key::S, true }, Hierarchy) == EditorShortcut::SaveScene);
    CHECK(r.Rebind(EditorShortcut::SaveScene, { Key::O, true }));
    CHECK(r.FindConflicts(EditorShortcut::SaveScene).size() == 1);
    CHECK(r.FindConflicts(KeyChord(Key::O, true), ShortcutScope::Global, EditorShortcut::SaveScene).front()->Id == EditorShortcut::OpenScene);
    CHECK(!r.Rebind("console.copy", { Key::K }));           // fixed
    CHECK(!r.Rebind(EditorShortcut::Copy, { Key::LeftShift })); // a modifier alone
    CHECK(r.Rebind(EditorShortcut::Paste, {}));               // unbound
    CHECK(std::string(r.GetDisplay(EditorShortcut::Paste)).empty() && match({ Key::V, true }, 0).empty());
    CHECK(r.CountModified() == 3);
    CHECK(ShortcutRegistry::ScopesConflict(ShortcutScope::Global, ShortcutScope::Global));
    CHECK(!ShortcutRegistry::ScopesConflict(ShortcutScope::Global, ShortcutScope::Console));
    CHECK(!ShortcutRegistry::ScopesConflict(ShortcutScope::TilePaint, ShortcutScope::ViewportTools));
    CHECK(!ShortcutRegistry::ScopesConflict(ShortcutScope::Selection, ShortcutScope::ViewportTools));
    CHECK(ShortcutRegistry::ScopesConflict(ShortcutScope::Selection, ShortcutScope::Camera) == false);
    CHECK(!ShortcutRegistry::ScopesConflict(ShortcutScope::Hierarchy, ShortcutScope::ContentBrowser));

    // The preferences keep only what changed; applied on a fresh table they give the same bindings.
    std::map<std::string, std::string> overrides = r.GetOverrides();
    CHECK(overrides.size() == 3 && overrides[EditorShortcut::SaveScene] == "Ctrl+O" && overrides[EditorShortcut::Paste].empty());
    ShortcutRegistry fresh;
    RegisterEditorShortcuts(fresh);
    overrides["nothing.here"] = "Ctrl+K";
    overrides["console.copy"] = "Ctrl+K";
    overrides[EditorShortcut::Copy] = "Ctrl+Nimadir";
    CHECK(fresh.ApplyOverrides(overrides) == 3);
    CHECK(fresh.Find(EditorShortcut::ToolMove)->Chord == KeyChord(Key::S, true));
    CHECK(fresh.Find(EditorShortcut::Paste)->Chord.IsEmpty());
    CHECK(fresh.Find(EditorShortcut::Copy)->Chord == KeyChord(Key::C, true));
    CHECK(fresh.ApplyOverrides({}) == 0 && fresh.CountModified() == 0); // everything back to its default
    CHECK(r.ResetToDefault(EditorShortcut::SaveScene) && r.Find(EditorShortcut::SaveScene)->Chord == KeyChord(Key::S, true));
    r.ResetAll();
    CHECK(r.CountModified() == 0 && r.CountConflicts() == 0);

    // The window's search: every word somewhere in the name, category, chord or scope.
    auto found = [&](std::string_view query) {
        std::vector<std::string> out;
        for (const ShortcutInfo& s : r.GetAll())
            if (ShortcutRegistry::MatchesSearch(s, query))
                out.push_back(s.Id);
        return out;
    };
    CHECK(found("").size() == r.GetAll().size());
    CHECK(found("ctrl+shift+s") == std::vector<std::string>{ EditorShortcut::SaveSceneAs });
    CHECK(found("SIQLASH viewport").empty());
    CHECK(found("siljit viewport").size() == 1 && found("siljit viewport").front() == EditorShortcut::ToolMove);
    CHECK(found("xatcho'p saqla").size() == (size_t)EditorShortcut::BookmarkSlots);
    CHECK(found("konsol nusxa").size() == 1);
}

TEST(RecentFilesMRU)
{
    RecentFileList list(3);
    list.Add("/p/a.zscene", "/p/P.zproject", 100);
    list.Add("/p/b.zscene", "/p/P.zproject", 200);
    list.Add("/p/c.zscene", "/q/Q.zproject", 300);
    list.Add("/p/d.zscene", "/p/P.zproject", 400);
    CHECK(list.GetEntries().size() == 3);
    CHECK(list.GetEntries()[0].Path == "/p/d.zscene" && list.GetEntries()[2].Path == "/p/b.zscene");
    CHECK(!list.Contains("/p/a.zscene"));
    // Opened again: moves to the front, listed once (the same file however it is written).
    list.Add("/p/./b.zscene", "/p/P.zproject", 500);
    CHECK(list.GetEntries().size() == 3 && list.GetEntries()[0].Path == "/p/b.zscene" && list.GetEntries()[0].Time == 500);
    CHECK(list.Contains("/p/x/../d.zscene"));
    CHECK(RecentFileList::Normalize("/p/folder/") == "/p/folder");
    // Per owner (a project's scenes).
    std::vector<RecentFileList::Entry> mine = list.GetForOwner("/p/P.zproject");
    CHECK(mine.size() == 2 && mine[0].Path == "/p/b.zscene" && mine[1].Path == "/p/d.zscene");
    CHECK(list.GetForOwner("/p/P.zproject", 1).size() == 1);
    // A folder moved: its files follow (not "/p/bb...").
    list.Add("/p/bb.zscene", {}, 600);
    CHECK(list.Rename("/p", "/r") == 3);
    CHECK(list.Contains("/r/b.zscene") && list.Contains("/r/bb.zscene") && !list.Contains("/p/b.zscene"));
    CHECK(list.GetForOwner("/r/P.zproject").size() == 2); // "/p/c" fell off when "/p/bb" came
    CHECK(list.Rename("/r/bb.zscene", "/r/b.zscene") == 1 && list.GetEntries().size() == 2); // onto a listed one: once
    // Gone files drop off.
    CHECK(list.Prune([](const std::string& path) { return path != "/r/b.zscene"; }) == 1 && list.GetEntries().size() == 1);
    CHECK(!list.Remove("/r/c.zscene") && list.Remove("/r/d.zscene") && list.IsEmpty());
    list.Add("/a", "/o");
    list.Add("/b", "/o2");
    CHECK(list.ClearOwner("/o") == 1 && list.GetEntries().size() == 1);
    CHECK(list.GetEntries()[0].Time > 1700000000); // now

    // JSON (and the older plain list of paths).
    RecentFileList loaded(10);
    list.Add("/c", "/o2", 900);
    CHECK(loaded.FromJson(list.ToJson()));
    CHECK(loaded.GetEntries().size() == 2 && loaded.GetEntries()[0].Path == "/c" && loaded.GetEntries()[0].Owner == "/o2" &&
          loaded.GetEntries()[0].Time == 900);
    CHECK(loaded.FromJson("[\"/new.zproject\", \"/old.zproject\"]"));
    CHECK(loaded.GetEntries().size() == 2 && loaded.GetEntries()[0].Path == "/new.zproject" && loaded.GetEntries()[1].Path == "/old.zproject");
    CHECK(!loaded.FromJson("{\"x\": 1}") && !loaded.FromJson("not json"));
    CHECK(loaded.GetEntries().size() == 2);

    // "How long ago".
    CHECK(FormatTimeAgo(1000, 1030) == "hozirgina");
    CHECK(FormatTimeAgo(1000, 1000 + 5 * 60) == "5 daqiqa oldin");
    CHECK(FormatTimeAgo(1000, 1000 + 3 * 3600) == "3 soat oldin");
    CHECK(FormatTimeAgo(1000, 1000 + 86400 + 5) == "kecha");
    CHECK(FormatTimeAgo(1000, 1000 + 4 * 86400) == "4 kun oldin");
    CHECK(FormatTimeAgo(1000, 1000 + 15 * 86400) == "2 hafta oldin");
    CHECK(FormatTimeAgo(1714521600, 1714521600 + 90 * 86400).size() == 10); // a date
    CHECK(FormatTimeAgo(0, 1000).empty());
}

TEST(EditorAutosaveRecovery)
{
    namespace fs = std::filesystem;
    const fs::path project = fs::temp_directory_path() / "zamin_autosave_test";
    std::error_code ec;
    fs::remove_all(project, ec);

    // Paths: the scene's place under Assets kept, an unsaved scene has a name of its own, nothing
    // leaves the folder.
    CHECK(EditorAutosave::GetDirectory(project) == project / ".zamin" / "autosave");
    CHECK(EditorAutosave::GetFilePath(project, "Scenes/Level1.zscene") == project / ".zamin" / "autosave" / "Scenes" / "Level1.zscene");
    CHECK(EditorAutosave::GetFilePath(project, "") == project / ".zamin" / "autosave" / EditorAutosave::UnsavedSceneName);
    CHECK(EditorAutosave::GetFilePath(project, "../../etc/x.zscene") == project / ".zamin" / "autosave" / "etc" / "x.zscene");
    CHECK(EditorAutosave::GetFilePath(project, "/abs/y.zscene") == project / ".zamin" / "autosave" / "abs" / "y.zscene");
    CHECK(EditorAutosave::GetLockPath(project) == project / ".zamin" / "editor.lock");

    // When a copy is due.
    AutosaveSettings settings;
    settings.IntervalMinutes = 2;
    CHECK(!EditorAutosave::IsDue(settings, 100.0, 0.0, true, false));
    CHECK(EditorAutosave::IsDue(settings, 120.0, 0.0, true, false));
    CHECK(!EditorAutosave::IsDue(settings, 500.0, 0.0, false, false)); // nothing new
    CHECK(!EditorAutosave::IsDue(settings, 500.0, 0.0, true, true));   // dragging / typing / playing
    settings.Enabled = false;
    CHECK(!EditorAutosave::IsDue(settings, 500.0, 0.0, true, false));
    settings.IntervalMinutes = 500;
    settings.Sanitize();
    CHECK(settings.IntervalMinutes == AutosaveSettings::MaxInterval);
    settings.IntervalMinutes = 0;
    settings.Sanitize();
    CHECK(settings.IntervalMinutes == AutosaveSettings::MinInterval);

    // The decision on opening a scene.
    using D = EditorAutosave::RecoveryDecision;
    EditorAutosave::RecoveryCheck check;
    CHECK(EditorAutosave::DecideRecovery(check) == D::None);
    check.AutosaveExists = true;
    CHECK(EditorAutosave::DecideRecovery(check) == D::Offer);              // a scene never saved
    check.SceneExists = true;
    check.SceneTime = fs::file_time_type::clock::now();
    check.AutosaveTime = check.SceneTime + std::chrono::seconds(30);
    CHECK(EditorAutosave::DecideRecovery(check) == D::Offer);              // newer than the file
    check.SameContent = true;
    CHECK(EditorAutosave::DecideRecovery(check) == D::Discard);            // nothing in it the file lacks
    check.SameContent = false;
    check.AutosaveTime = check.SceneTime - std::chrono::seconds(30);
    CHECK(EditorAutosave::DecideRecovery(check) == D::Discard);            // saved after it

    // From real files (the copy written atomically: no temporary left behind).
    const fs::path scene = project / "Assets" / "Scenes" / "Level1.zscene";
    const fs::path copy = EditorAutosave::GetFilePath(project, "Scenes/Level1.zscene");
    CHECK(EditorAutosave::WriteFileAtomic(scene, "{\"Scene\": \"A\"}"));
    CHECK(EditorAutosave::WriteFileAtomic(copy, "{\"Scene\": \"B\"}"));
    CHECK(fs::exists(copy) && !fs::exists(fs::path(copy.string() + ".tmp")));
    CHECK(FileSystem::ReadTextFile(copy) == "{\"Scene\": \"B\"}");
    fs::last_write_time(copy, fs::last_write_time(scene) + std::chrono::seconds(60));
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(copy, scene)) == D::Offer);
    fs::last_write_time(copy, fs::last_write_time(scene) - std::chrono::seconds(60));
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(copy, scene)) == D::Discard);
    CHECK(EditorAutosave::WriteFileAtomic(copy, "{\"Scene\": \"A\"}"));
    fs::last_write_time(copy, fs::last_write_time(scene) + std::chrono::seconds(60));
    CHECK(EditorAutosave::InspectRecovery(copy, scene).SameContent);
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(copy, scene)) == D::Discard);
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(copy, {})) == D::Offer);
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(project / "none.zscene", scene)) == D::None);

    // A copy put aside ("Keyinroq") sits next to it, out of the way of this session's copies, and is
    // offered again even after the scene was saved - unless it holds just what the file does.
    const fs::path kept = EditorAutosave::GetKeptPath(copy);
    CHECK(kept == project / ".zamin" / "autosave" / "Scenes" / "Level1.zscene.kept");
    CHECK(kept != copy && kept.parent_path() == copy.parent_path());
    CHECK(EditorAutosave::WriteFileAtomic(kept, "{\"Scene\": \"C\"}"));
    fs::last_write_time(kept, fs::last_write_time(scene) - std::chrono::seconds(60));
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(kept, scene)) == D::Discard); // as a usual copy: stale
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(kept, scene), true) == D::Offer);
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(kept, {}), true) == D::Offer);
    CHECK(EditorAutosave::WriteFileAtomic(kept, "{\"Scene\": \"A\"}"));
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(kept, scene), true) == D::Discard); // nothing to recover
    CHECK(EditorAutosave::DecideRecovery(EditorAutosave::InspectRecovery(project / "none.kept", scene), true) == D::None);
    {
        EditorAutosave::RecoveryCheck none;
        CHECK(EditorAutosave::DecideRecovery(none, true) == D::None);
    }

    // The lock file: who has the project open, and whether that session ended without closing it.
    using L = EditorAutosave::LockState;
    const fs::path lockPath = EditorAutosave::GetLockPath(project);
    EditorAutosave::LockInfo lock = EditorAutosave::MakeLock();
    CHECK(lock.ProcessId == EditorAutosave::GetCurrentProcessId() && lock.ProcessId > 0 && lock.StartedAt > 0);
    CHECK(EditorAutosave::IsProcessAlive(lock.ProcessId) && !EditorAutosave::IsProcessAlive(0));
    CHECK(!EditorAutosave::ReadLock(lockPath).has_value());
    CHECK(EditorAutosave::CheckLock(EditorAutosave::ReadLock(lockPath), lock.ProcessId, EditorAutosave::IsProcessAlive) == L::Free);
    CHECK(EditorAutosave::WriteLock(lockPath, lock));
    auto read = EditorAutosave::ReadLock(lockPath);
    CHECK(read && read->ProcessId == lock.ProcessId && read->StartedAt == lock.StartedAt && read->Host == lock.Host);
    CHECK(EditorAutosave::CheckLock(read, lock.ProcessId, EditorAutosave::IsProcessAlive) == L::Own);
    EditorAutosave::LockInfo other{ 424242, 1, "host" };
    CHECK(EditorAutosave::CheckLock(other, lock.ProcessId, [](int) { return true; }) == L::OtherInstance);
    CHECK(EditorAutosave::CheckLock(other, lock.ProcessId, [](int) { return false; }) == L::Crashed);
    CHECK(EditorAutosave::WriteFileAtomic(lockPath, "garbage"));
    CHECK(EditorAutosave::CheckLock(EditorAutosave::ReadLock(lockPath), lock.ProcessId, EditorAutosave::IsProcessAlive) == L::Crashed);
    EditorAutosave::RemoveLock(lockPath);
    CHECK(!fs::exists(lockPath));

    // The preferences keep the autosave, undo and shortcut settings.
    EditorPreferences prefs;
    prefs.Autosave.Enabled = false;
    prefs.Autosave.IntervalMinutes = 12;
    prefs.Undo.RecordSelection = false;
    prefs.Shortcuts["file.save"] = "Ctrl+Alt+S";
    prefs.Shortcuts["edit.paste"] = "";
    EditorPreferences loaded;
    CHECK(loaded.FromJson(prefs.ToJson()));
    CHECK(!loaded.Autosave.Enabled && loaded.Autosave.IntervalMinutes == 12 && !loaded.Undo.RecordSelection);
    CHECK(loaded.Shortcuts.size() == 2 && loaded.Shortcuts["file.save"] == "Ctrl+Alt+S" && loaded.Shortcuts["edit.paste"].empty());
    CHECK(loaded.FromJson("{\"ZaminEditorPreferences\": 1, \"Autosave\": {\"IntervalMinutes\": 9999}}"));
    CHECK(loaded.Autosave.IntervalMinutes == AutosaveSettings::MaxInterval);
    fs::remove_all(project, ec);
}

TEST(UndoHistoryNamingAndJumps)
{
    auto sel = [](std::vector<uint64_t> ids) {
        UndoSelection s;
        s.Entities = ids;
        s.Primary = ids.empty() ? 0 : ids.back();
        return s;
    };
    UndoHistory h(100, 100);
    CHECK(h.IsEmpty() && !h.CanUndo() && !h.CanRedo() && h.GetCurrent().Name.empty());
    h.Reset("A", sel({ 1 }), "Sahna ochildi", 1.0);
    CHECK(!h.CanUndo() && h.GetCurrent().Kind == UndoEntryKind::Initial && h.IsAtSavedState());
    CHECK(!h.CommitEdit("A", sel({ 1 }), "hech narsa"));                // the same scene: no step
    CHECK(h.CommitEdit("B", sel({ 1 }), "Transform o'zgardi (Player)", 2.0));
    CHECK(!h.IsAtSavedState());
    CHECK(h.CommitSelection(sel({ 2 }), "Tanlash: Enemy", 3.0));
    CHECK(!h.CommitSelection(sel({ 2 }), "Tanlash: Enemy"));            // the same selection
    CHECK(h.GetEntries()[2].Scene == h.GetEntries()[1].Scene);         // shares the text
    CHECK(h.GetMemoryUsage() == 2 && h.CountEdits() == 1 && h.CountSelections() == 1);
    CHECK(h.CommitEdit("C", sel({}), "O'chirildi: Enemy", 4.0));
    CHECK(h.GetUndoName() == "O'chirildi: Enemy" && h.GetRedoName().empty());

    // Undo through them: the selection step puts back only the selection.
    UndoHistory::Move m = h.Undo();
    CHECK(m.Target && m.SceneChanged && *m.Target->Scene == "B" && m.Target->Selection == sel({ 2 }));
    CHECK(h.GetUndoName() == "Tanlash: Enemy" && h.GetRedoName() == "O'chirildi: Enemy");
    m = h.Undo();
    CHECK(m.Target && !m.SceneChanged && m.Target->Selection == sel({ 1 }));
    m = h.Undo();
    CHECK(m.Target && m.SceneChanged && *m.Target->Scene == "A" && h.IsAtSavedState());
    CHECK(!h.CanUndo() && h.Undo().Target == nullptr);
    // Jumps (the history panel), there and back.
    m = h.JumpTo(3);
    CHECK(m.Target && m.SceneChanged && *m.Target->Scene == "C" && h.GetCurrentIndex() == 3 && !h.CanRedo());
    CHECK(h.JumpTo(3).Target == nullptr && h.JumpTo(99).Target == nullptr);
    m = h.JumpTo(2);
    CHECK(m.Target && m.SceneChanged && m.Target->Name == "Tanlash: Enemy");
    // A new step after an undo drops what could have been redone.
    CHECK(h.CommitEdit("D", sel({ 2 }), "Nomi o'zgardi", 5.0));
    CHECK(h.GetEntries().size() == 4 && !h.CanRedo() && *h.GetCurrent().Scene == "D");
    // Saved: undo back to that state is clean again.
    h.MarkSaved();
    CHECK(h.IsAtSavedState());
    h.Undo();
    CHECK(!h.IsAtSavedState());
    h.Redo();
    CHECK(h.IsAtSavedState());
    h.SetCurrentSelection(sel({ 7 }));
    CHECK(h.GetCurrent().Selection == sel({ 7 }) && h.GetEntries().size() == 4);
    // Paths moved: every text rewritten, a shared one once.
    int rewrites = 0;
    h.RewriteScenes([&](std::string& text) { text += "'"; rewrites++; });
    CHECK(rewrites == 3 && *h.GetEntries()[1].Scene == "B'" && h.GetEntries()[2].Scene == h.GetEntries()[1].Scene && h.IsAtSavedState());
    h.ClearHistory("Tarix tozalandi");
    CHECK(h.GetEntries().size() == 1 && !h.CanUndo() && h.GetCurrent().Kind == UndoEntryKind::Initial && *h.GetCurrent().Scene == "D'");

    // The oldest steps drop off past the limits (the first one left is where undo stops).
    UndoHistory small(3, 2);
    small.Reset("0", sel({}), "start");
    for (int i = 1; i <= 6; i++)
    {
        small.CommitEdit(std::to_string(i), sel({}), std::format("edit {}", i));
        small.CommitSelection(sel({ (uint64_t)i }), std::format("sel {}", i));
    }
    CHECK(small.CountEdits() <= 3 && small.CountSelections() <= 2);
    CHECK(small.GetEntries().front().Kind == UndoEntryKind::Initial);
    CHECK(small.GetCurrentIndex() == small.GetEntries().size() - 1 && small.GetCurrent().Name == "sel 6");
    CHECK(!small.IsAtSavedState()); // the saved state dropped off

    // Names from what the snapshots say changed.
    Ref<Scene> scene = CreateRef<Scene>();
    Entity player = scene->CreateEntity("Player");
    Entity enemy = scene->CreateEntity("Enemy");
    auto snap = [&]() { return SceneSerializer(scene).SerializeToString(); };
    std::string before = snap();
    CHECK(DescribeSceneChange(before, before) == "O'zgarish" && DiffSceneSnapshots(before, before).IsEmpty());
    player.GetComponent<TransformComponent>().Translation.x = 3.0f;
    std::string after = snap();
    CHECK(DescribeSceneChange(before, after) == "Transform.Translation o'zgardi (Player)");
    player.GetComponent<TransformComponent>().Scale = glm::vec3(2.0f);
    CHECK(DescribeSceneChange(after, snap()) == "Transform.Scale o'zgardi (Player)");
    player.GetComponent<TransformComponent>().Rotation.z = 1.0f;
    player.GetComponent<TransformComponent>().Translation.y = 1.0f;
    CHECK(DescribeSceneChange(after, snap()) == "Transform o'zgardi (Player)");
    after = snap();
    player.AddComponent<SpriteRendererComponent>();
    CHECK(DescribeSceneChange(after, snap()) == "Komponent qo'shildi: SpriteRenderer (Player)");
    before = snap();
    player.RemoveComponent<SpriteRendererComponent>();
    CHECK(DescribeSceneChange(before, snap()) == "Komponent olib tashlandi: SpriteRenderer (Player)");
    before = snap();
    player.GetComponent<TagComponent>().Tag = "Hero \"1\"";
    CHECK(DescribeSceneChange(before, snap()) == "Nomi o'zgardi: Player \xe2\x86\x92 Hero \"1\"");
    before = snap();
    Entity coin = scene->CreateEntity("Coin");
    after = snap();
    CHECK(DescribeSceneChange(before, after) == "Qo'shildi: Coin");
    CHECK(DescribeSceneChange(after, before) == "O'chirildi: Coin");
    scene->CreateEntity("Coin 2");
    scene->CreateEntity("Coin 3");
    const std::string twoAdded = DescribeSceneChange(after, snap()); // in the file's order
    CHECK(twoAdded == "Qo'shildi: Coin 2 (+1)" || twoAdded == "Qo'shildi: Coin 3 (+1)");
    before = snap();
    scene->SetParent(coin, enemy, false);
    CHECK(DescribeSceneChange(before, snap()) == "Ierarxiya o'zgardi: Coin");
    before = snap();
    enemy.GetComponent<TransformComponent>().Translation.z = 5.0f;
    coin.GetComponent<TransformComponent>().Translation.z = 5.0f;
    const std::string twoMoved = DescribeSceneChange(before, snap());
    CHECK(twoMoved == "Transform o'zgardi (Enemy (+1))" || twoMoved == "Transform o'zgardi (Coin (+1))");
    before = snap();
    scene->GetSettings().Gravity = { 0.0f, -3.0f };
    SceneChange settingsChange = DiffSceneSnapshots(before, snap());
    CHECK(settingsChange.Valid && settingsChange.SceneKeys.size() == 1 && settingsChange.SceneField == "Gravity");
    CHECK(DescribeSceneChange(settingsChange) == "Sahna sozlamalari: Gravity");
    before = snap();
    enemy.SetActive(false);
    CHECK(DescribeSceneChange(before, snap()) == "Nofaol qilindi: Enemy");
    CHECK(DescribeSceneChange(snap(), before) == "Faollashtirildi: Enemy");
    CHECK(DescribeSceneChange("not json", before) == "Sahna o'zgarishi" && !DiffSceneSnapshots("{", before).Valid);

    CHECK(DescribeSelection({}) == "Tanlov bekor qilindi");
    CHECK(DescribeSelection({ "Player" }) == "Tanlash: Player");
    CHECK(DescribeSelection({ "Player", "Enemy", "Coin" }) == "Tanlash: Player (+2)");
}

// Editor II (311): the saved window layouts - names, files, renames, the built-in ones.
TEST(EditorLayoutLibraryFiles)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "zamin_layouts_test";
    std::error_code ec;
    fs::remove_all(dir, ec);

    // Names: trimmed, one space between words; nothing a file name cannot hold, no built-in names.
    CHECK(EditorLayoutLibrary::CleanName("  Mening   joylashuvim \t") == "Mening joylashuvim");
    CHECK(EditorLayoutLibrary::IsValidName("Animatsiya ishi"));
    CHECK(EditorLayoutLibrary::IsValidName("Ko'p ekran (2)"));
    std::string reason;
    CHECK(!EditorLayoutLibrary::IsValidName("", &reason) && !reason.empty());
    CHECK(!EditorLayoutLibrary::IsValidName("a/b"));
    CHECK(!EditorLayoutLibrary::IsValidName("a\\b"));
    CHECK(!EditorLayoutLibrary::IsValidName("what?"));
    CHECK(!EditorLayoutLibrary::IsValidName(".hidden"));
    CHECK(!EditorLayoutLibrary::IsValidName(std::string(EditorLayoutLibrary::MaxNameLength + 1, 'x')));
    CHECK(!EditorLayoutLibrary::IsValidName("standart")); // a built-in layout's name, any letter case
    CHECK(!EditorLayoutLibrary::IsValidName("FourViews"));
    CHECK(EditorLayoutLibrary::FindBuiltIn("2d") == BuiltInLayout::Mode2D);
    CHECK(EditorLayoutLibrary::FindBuiltIn("Animatsiya") == BuiltInLayout::Animation);
    CHECK(EditorLayoutLibrary::FindBuiltIn("4 ko'rinish") == BuiltInLayout::FourViews);
    CHECK(!EditorLayoutLibrary::FindBuiltIn("Mening"));
    CHECK(std::string(EditorLayoutLibrary::GetBuiltInKey(BuiltInLayout::Default)) == "Default");

    // A layout's file: its ImGui text and which windows were open.
    EditorLayout layout;
    layout.Name = "  Ish   joyi ";
    layout.Ini = "[Window][###Hierarchy]\nPos=0,0\nSize=300,600\nDockId=0x00000001,0\n\n[Docking][Data]\n";
    layout.Panels = { { "Hierarchy", true }, { "Console", false }, { "SceneView2", true } };
    layout.SavedAt = 1700000000;
    EditorLayout parsed;
    CHECK(parsed.FromJson(layout.ToJson()));
    CHECK(parsed.Ini == layout.Ini);
    CHECK(parsed.IsPanelOpen("Hierarchy", false) && !parsed.IsPanelOpen("Console", true) && parsed.IsPanelOpen("SceneView2", false));
    CHECK(parsed.IsPanelOpen("Stats", true) && !parsed.IsPanelOpen("Stats", false)); // not named: the fallback
    CHECK(parsed.SavedAt == 1700000000);
    CHECK(!parsed.FromJson("{\"Name\":\"x\"}")); // no layout marker / no ImGui text
    CHECK(!parsed.FromJson("not json"));

    EditorLayoutLibrary library(dir);
    CHECK(library.Refresh() == 0); // no folder yet
    CHECK(library.MakeUniqueName("Mening joylashuvim") == "Mening joylashuvim");
    std::string error;
    CHECK(library.Save(layout, &error));
    CHECK(fs::exists(dir / "Ish joyi.zlayout"));
    CHECK(library.GetNames().size() == 1 && library.GetNames()[0] == "Ish joyi");
    CHECK(library.Contains("ish JOYI"));
    CHECK(library.GetSavedAt("Ish joyi") == 1700000000);
    std::optional<EditorLayout> loaded = library.Load("ISH joyi");
    CHECK(loaded && loaded->Name == "Ish joyi" && loaded->Ini == layout.Ini && loaded->Panels.size() == 3);
    CHECK(!library.Load("Yo'q"));
    EditorLayout bad = layout;
    bad.Name = "a:b";
    CHECK(!library.Save(bad, &error) && !error.empty());
    bad.Name = "2D";
    CHECK(!library.Save(bad));

    // Unique names, overwriting in another letter case (one file, the new spelling).
    CHECK(library.MakeUniqueName("Ish joyi") == "Ish joyi 2");
    EditorLayout second = layout;
    second.Name = "Ish joyi 2";
    CHECK(library.Save(second));
    CHECK(library.MakeUniqueName("ish joyi") == "ish joyi 3");
    EditorLayout upper = layout;
    upper.Name = "ISH JOYI";
    upper.Ini = "[Window][###Console]\n";
    CHECK(library.Save(upper));
    CHECK(library.GetNames().size() == 2);
    CHECK(library.Contains("Ish joyi") && library.Load("ish joyi")->Ini == "[Window][###Console]\n");
    CHECK(!fs::exists(dir / "Ish joyi.zlayout") && fs::exists(dir / "ISH JOYI.zlayout"));
    // Listed by name, letter case aside; files that are no layouts are left out.
    FileSystem::WriteTextFile(dir / "Buzilgan.zlayout", "{ yo'q");
    FileSystem::WriteTextFile(dir / "boshqa.txt", "x");
    CHECK(library.Refresh() == 2);
    CHECK(library.GetNames()[0] == "ISH JOYI" && library.GetNames()[1] == "Ish joyi 2");

    // Renames: into a free name (the old file goes), not onto another layout, not to a bad name.
    CHECK(library.Rename("ish joyi 2", "Animatsiya ishi", &error));
    CHECK(library.Contains("Animatsiya ishi") && !library.Contains("Ish joyi 2"));
    CHECK(!fs::exists(dir / "Ish joyi 2.zlayout"));
    CHECK(library.Load("Animatsiya ishi")->Name == "Animatsiya ishi");
    CHECK(!library.Rename("Animatsiya ishi", "ish joyi", &error) && !error.empty()); // taken
    CHECK(!library.Rename("Animatsiya ishi", "Standart"));                          // built-in
    CHECK(!library.Rename("Yo'q", "Boshqa"));
    CHECK(library.Rename("ISH JOYI", "Ish joyi")); // letter case only
    CHECK(library.GetNames().size() == 2 && library.Contains("Ish joyi") && library.Load("Ish joyi")->Name == "Ish joyi");

    // Removing.
    CHECK(library.Remove("animatsiya ISHI"));
    CHECK(!library.Contains("Animatsiya ishi") && library.GetNames().size() == 1);
    CHECK(!library.Remove("Animatsiya ishi"));
    fs::remove_all(dir, ec);
}

// Editor II (312-314): axis views, the editor camera's projection, lock and fly settings, the
// extra views' settings in editor-view.json and the camera preferences.
TEST(SceneViewAxesAndCamera)
{
    // Every axis view's angles make the camera look along its direction, and it is found again.
    for (int i = 1; i < (int)ViewAxis::Count; i++)
    {
        const ViewAxis axis = (ViewAxis)i;
        const ViewAxisInfo& info = GetViewAxisInfo(axis);
        EditorCamera camera;
        camera.SetViewportSize(800.0f, 600.0f);
        camera.Focus({ 1.0f, 2.0f, 3.0f }, 10.0f);
        camera.SetViewState(camera.GetAxisViewState(axis));
        CHECK(camera.IsOrthographic());
        CHECK_NEAR(glm::length(camera.GetForwardDirection() - info.Forward), 0.0f, 1e-4f);
        CHECK(camera.GetViewAxis() == axis);
        CHECK(FindViewAxis(info.Forward) == axis);
        CHECK(GetOppositeViewAxis(GetOppositeViewAxis(axis)) == axis);
        CHECK_NEAR(glm::dot(GetViewAxisInfo(GetOppositeViewAxis(axis)).Forward, info.Forward), -1.0f, 1e-5f);
        CHECK(ViewAxisFromKey(info.Key) == axis);
        // The focal point and distance stay; the camera sits on the axis' side.
        CHECK_NEAR(glm::length(camera.GetFocalPoint() - glm::vec3(1.0f, 2.0f, 3.0f)), 0.0f, 1e-4f);
        CHECK_NEAR(glm::length(camera.GetPosition() - (glm::vec3(1.0f, 2.0f, 3.0f) - info.Forward * 10.0f)), 0.0f, 1e-3f);
    }
    // Top: X to the right, -Z up on screen; Right view: looking -X.
    {
        EditorCamera camera;
        camera.SetViewState(camera.GetAxisViewState(ViewAxis::Top));
        CHECK_NEAR(glm::length(camera.GetRightDirection() - glm::vec3(1, 0, 0)), 0.0f, 1e-4f);
        CHECK_NEAR(glm::length(camera.GetUpDirection() - glm::vec3(0, 0, -1)), 0.0f, 1e-4f);
    }
    CHECK(ViewAxisFromKey("yuqoridan") == ViewAxis::Top && ViewAxisFromKey("RIGHT") == ViewAxis::Right);
    CHECK(ViewAxisFromKey("persp") == ViewAxis::Free && ViewAxisFromKey("nimadir") == ViewAxis::Free);
    CHECK(FindViewAxis(glm::normalize(glm::vec3(0.0f, -1.0f, 0.004f))) == ViewAxis::Top);   // within half a degree
    CHECK(FindViewAxis(glm::normalize(glm::vec3(0.0f, -1.0f, 0.05f))) == ViewAxis::Free);   // about 3 degrees off
    CHECK(FindViewAxis(glm::vec3(0.0f)) == ViewAxis::Free);
    // The navigator's ends: the view from that side towards the centre.
    CHECK(GetViewAxisFromSide({ 1, 0, 0 }) == ViewAxis::Right && GetViewAxisFromSide({ -1, 0, 0 }) == ViewAxis::Left);
    CHECK(GetViewAxisFromSide({ 0, 1, 0 }) == ViewAxis::Top && GetViewAxisFromSide({ 0, -1, 0 }) == ViewAxis::Bottom);
    CHECK(GetViewAxisFromSide({ 0, 0, 1 }) == ViewAxis::Front && GetViewAxisFromSide({ 0, 0, -1 }) == ViewAxis::Back);
    // The grid's plane: the ground in perspective, the plane facing an orthographic side view.
    CHECK(GetGridPlaneForView({ 0, 0, -1 }, false, false) == 0);
    CHECK(GetGridPlaneForView({ 0, 0, -1 }, true, false) == 1);
    CHECK(GetGridPlaneForView({ -1, 0, 0 }, true, false) == 2);
    CHECK(GetGridPlaneForView({ 0, -1, 0 }, true, false) == 0);
    CHECK(GetGridPlaneForView({ -1, 0, 0 }, true, true) == 1); // the 2D mode: always XY

    // Free: back to perspective, the direction kept.
    {
        EditorCamera camera;
        camera.SetViewState(camera.GetAxisViewState(ViewAxis::Front));
        const glm::vec3 forward = camera.GetForwardDirection();
        camera.SetViewState(camera.GetAxisViewState(ViewAxis::Free));
        CHECK(!camera.IsOrthographic() && camera.GetViewAxis() == ViewAxis::Free);
        CHECK_NEAR(glm::length(camera.GetForwardDirection() - forward), 0.0f, 1e-5f);
        // Perspective <-> orthographic; a perspective camera along an axis is no axis view.
        camera.ToggleOrthographic();
        CHECK(camera.IsOrthographic() && camera.GetViewAxis() == ViewAxis::Front);
        camera.ToggleOrthographic();
        CHECK(!camera.IsOrthographic());
    }
    // A transition into an axis view turns in perspective and switches on arrival.
    {
        EditorCamera camera;
        camera.SetViewAxis(ViewAxis::Right, 0.5f);
        CHECK(camera.IsTransitioning() && !camera.IsOrthographic());
        camera.OnUpdate(0.25f, false);
        CHECK(!camera.IsOrthographic());
        camera.OnUpdate(0.3f, false);
        CHECK(!camera.IsTransitioning() && camera.IsOrthographic() && camera.GetViewAxis() == ViewAxis::Right);
        // Top -> Front: orthographic all the way.
        camera.SetViewAxis(ViewAxis::Top, 0.0f);
        camera.SetViewAxis(ViewAxis::Front, 0.4f);
        CHECK(camera.IsOrthographic());
        camera.OnUpdate(0.5f, false);
        CHECK(camera.GetViewAxis() == ViewAxis::Front);
    }
    // The 2D mode: orthographic with the rotation locked - no axis view, kept in its state.
    {
        EditorCamera camera;
        camera.SetOrthographic(true);
        camera.SetRotationLocked(true);
        camera.SetOrientation(0.0f, 0.0f);
        CHECK(camera.GetViewAxis() == ViewAxis::Free);
        EditorCameraState state = camera.GetViewState();
        CHECK(state.Orthographic && state.Mode2D);
        camera.ToggleOrthographic(); // not in the 2D mode
        CHECK(camera.IsOrthographic());
        EditorCamera other;
        other.SetViewState(state);
        CHECK(other.IsRotationLocked() && other.IsOrthographic());
        other.SetViewState(other.GetAxisViewState(ViewAxis::Top)); // an axis view leaves it
        CHECK(!other.IsRotationLocked() && other.GetViewAxis() == ViewAxis::Top);
    }
    // Fly acceleration: 1 at the start, growing per second, capped.
    CHECK_NEAR(GetFlyBoost(0.0f, 2.0f), 1.0f, 1e-6f);
    CHECK_NEAR(GetFlyBoost(1.0f, 0.0f), 1.0f, 1e-6f);
    CHECK_NEAR(GetFlyBoost(0.5f, 2.0f), 2.0f, 1e-6f);
    CHECK_NEAR(GetFlyBoost(100.0f, 2.0f), MaxFlyBoost, 1e-6f);
    CHECK_NEAR(GetFlyBoost(1.0f, -1.0f), 1.0f, 1e-6f);

    // editor-view.json: the extra views (defaults Top / Front / Right orthographic) and bookmarks'
    // 2D flag (files from before it: orthographic was the 2D mode).
    EditorViewSettings view;
    CHECK(!view.SceneViews[0].Open && view.SceneViews[0].Camera.Orthographic);
    {
        EditorCamera probe;
        probe.SetViewState(view.SceneViews[2].Camera);
        CHECK(probe.GetViewAxis() == ViewAxis::Right);
        probe.SetViewState(view.SceneViews[1].Camera);
        CHECK(probe.GetViewAxis() == ViewAxis::Front);
    }
    view.SceneViews[1].Open = true;
    view.SceneViews[1].ShowGrid = false;
    view.SceneViews[1].Camera.FocalPoint = { 4.0f, 5.0f, 6.0f };
    view.SceneViews[1].Camera.Distance = 33.0f;
    view.SceneViews[2].Camera.Orthographic = false;
    EditorCameraState axisMark;
    axisMark.Orthographic = true; // an axis view saved as a bookmark: not the 2D mode
    view.GetBookmarks("Scenes/A.zscene").Set(1, axisMark);
    EditorCameraState flatMark;
    flatMark.Orthographic = flatMark.Mode2D = true;
    view.GetBookmarks("Scenes/A.zscene").Set(2, flatMark);
    EditorViewSettings read;
    CHECK(read.FromJson(view.ToJson()));
    CHECK(read.SceneViews[1].Open && !read.SceneViews[1].ShowGrid && !read.SceneViews[0].Open);
    CHECK_NEAR(read.SceneViews[1].Camera.FocalPoint.z, 6.0f, 1e-5f);
    CHECK_NEAR(read.SceneViews[1].Camera.Distance, 33.0f, 1e-4f);
    CHECK(!read.SceneViews[2].Camera.Orthographic);
    CHECK(read.GetBookmarks("Scenes/A.zscene").Get(1)->Orthographic && !read.GetBookmarks("Scenes/A.zscene").Get(1)->Mode2D);
    CHECK(read.GetBookmarks("Scenes/A.zscene").Get(2)->Mode2D);
    EditorViewSettings old;
    const std::string oldKey = EditorViewSettings::SceneKey("Scenes/B.zscene");
    CHECK(old.FromJson(std::format(R"({{"ZaminEditorView":1,"CameraBookmarks":{{"{}":{{"1":{{"Distance":5,"Orthographic":true}}}}}}}})", oldKey)));
    CHECK(old.GetBookmarks("Scenes/B.zscene").Has(1) && old.GetBookmarks("Scenes/B.zscene").Get(1)->Mode2D);

    // The camera preferences' new fields, sanitized.
    EditorPreferences prefs;
    prefs.Camera.FlyAcceleration = 2.5f;
    prefs.Camera.LookSensitivity = 1.5f;
    prefs.Camera.InvertY = true;
    EditorPreferences prefsRead;
    CHECK(prefsRead.FromJson(prefs.ToJson()));
    CHECK_NEAR(prefsRead.Camera.FlyAcceleration, 2.5f, 1e-5f);
    CHECK_NEAR(prefsRead.Camera.LookSensitivity, 1.5f, 1e-5f);
    CHECK(prefsRead.Camera.InvertY);
    prefs.Camera.FlyAcceleration = 99.0f;
    prefs.Camera.LookSensitivity = 0.0f;
    prefs.Layout.Current = "Ish joyi";
    CHECK(prefsRead.FromJson(prefs.ToJson()));
    CHECK_NEAR(prefsRead.Camera.FlyAcceleration, EditorCameraDefaults::MaxAcceleration, 1e-5f);
    CHECK_NEAR(prefsRead.Camera.LookSensitivity, EditorCameraDefaults::MinSensitivity, 1e-5f);
    CHECK(prefsRead.Layout.Current == "Ish joyi");

    // The numpad axis keys are in the shortcut table, without conflicts.
    ShortcutRegistry shortcuts;
    RegisterEditorShortcuts(shortcuts);
    const ShortcutInfo* top = shortcuts.Find(EditorShortcut::ViewTop);
    CHECK(top && top->Chord == KeyChord(Key::KP7) && std::string(top->Display) == "Num 7");
    CHECK(shortcuts.Find(EditorShortcut::ViewBack) && shortcuts.Find(EditorShortcut::ViewBack)->Chord == KeyChord(Key::KP1, true));
    CHECK(shortcuts.Match(KeyChord(Key::KP3), ShortcutSituation::Editor | ShortcutSituation::SceneView)->Id == EditorShortcut::ViewRight);
    CHECK(!shortcuts.Match(KeyChord(Key::KP3), ShortcutSituation::Editor)); // only over a scene view
    CHECK(shortcuts.CountConflicts() == 0);
}

// Editor II (315): entity names over the views - fading with the distance, which labels stay where
// they would overlap, their drawing order, shortened names.
TEST(EntityLabelsLayoutAndFade)
{
    EntityLabelSettings s;
    s.Mode = EntityLabelMode::All;
    s.FadeStart = 10.0f;
    s.FadeEnd = 30.0f;
    CHECK_NEAR(s.GetAlpha(5.0f, false), 1.0f, 1e-6f);
    CHECK_NEAR(s.GetAlpha(10.0f, false), 1.0f, 1e-6f);
    CHECK_NEAR(s.GetAlpha(20.0f, false), 0.5f, 1e-5f);    // smoothstep's middle
    CHECK(s.GetAlpha(15.0f, false) > s.GetAlpha(25.0f, false));
    CHECK_NEAR(s.GetAlpha(30.0f, false), 0.0f, 1e-6f);
    CHECK_NEAR(s.GetAlpha(500.0f, false), 0.0f, 1e-6f);
    CHECK_NEAR(s.GetAlpha(500.0f, true), 1.0f, 1e-6f);    // the selection stays readable
    CHECK_NEAR(s.GetAlpha(-1.0f, false), 1.0f, 1e-6f);    // orthographic: no fading
    s.FadeSelected = true;
    CHECK_NEAR(s.GetAlpha(500.0f, true), 0.0f, 1e-6f);
    s.Fade = false;
    CHECK_NEAR(s.GetAlpha(500.0f, false), 1.0f, 1e-6f);
    s.Fade = true;
    s.FadeSelected = false;
    // Sanitizing: the range kept in order, the scale and count in range.
    EntityLabelSettings bad;
    bad.FadeStart = 50.0f;
    bad.FadeEnd = 10.0f;
    bad.Scale = 9.0f;
    bad.MaxLabels = 0;
    bad.Sanitize();
    CHECK(bad.FadeEnd > bad.FadeStart && bad.Scale == EntityLabelSettings::MaxScale && bad.MaxLabels == EntityLabelSettings::MinLabels);
    CHECK(EntityLabelSettings::ModeFromKey("selected", EntityLabelMode::Off) == EntityLabelMode::Selected);
    CHECK(EntityLabelSettings::ModeFromKey("x", EntityLabelMode::All) == EntityLabelMode::All);

    auto label = [](float x, float y, float distance, bool selected = false, bool hovered = false, float alpha = 1.0f) {
        EntityLabelCandidate c;
        c.Min = { x, y };
        c.Max = { x + 60.0f, y + 18.0f };
        c.Distance = distance;
        c.Selected = selected;
        c.Hovered = hovered;
        c.Alpha = alpha;
        return c;
    };
    const glm::vec2 viewMin(0.0f), viewMax(800.0f, 600.0f);
    // Two overlapping: the nearer stays; a third apart from them stays too.
    std::vector<EntityLabelCandidate> c = { label(100, 100, 20.0f), label(120, 105, 8.0f), label(400, 300, 50.0f) };
    std::vector<size_t> shown = LayoutEntityLabels(c, s, viewMin, viewMax);
    CHECK(shown.size() == 2);
    CHECK(std::find(shown.begin(), shown.end(), 1) != shown.end() && std::find(shown.begin(), shown.end(), 0) == shown.end());
    CHECK(shown.front() == 2 && shown.back() == 1); // drawn far to near
    // The selection keeps its place over a nearer one; the hovered one over a nearer unselected one.
    c = { label(100, 100, 20.0f, true), label(120, 105, 8.0f) };
    shown = LayoutEntityLabels(c, s, viewMin, viewMax);
    CHECK(shown.size() == 1 && shown[0] == 0);
    c = { label(100, 100, 20.0f, false, true), label(120, 105, 8.0f) };
    shown = LayoutEntityLabels(c, s, viewMin, viewMax);
    CHECK(shown.size() == 1 && shown[0] == 0);
    // Overlapping allowed: all of them, the selected one drawn last.
    s.HideOverlapping = false;
    c = { label(100, 100, 5.0f, true), label(120, 105, 8.0f), label(110, 102, 30.0f) };
    shown = LayoutEntityLabels(c, s, viewMin, viewMax);
    CHECK(shown.size() == 3 && shown[0] == 2 && shown[1] == 1 && shown[2] == 0);
    s.HideOverlapping = true;
    // Invisible ones and those outside the view are left out; at most MaxLabels, the nearest first.
    c = { label(100, 100, 5.0f, false, false, 0.01f), label(-100, 100, 5.0f), label(790, 590, 5.0f), label(300, 300, 1.0f) };
    shown = LayoutEntityLabels(c, s, viewMin, viewMax);
    CHECK(shown.size() == 2); // the one half inside the view's corner stays
    c.clear();
    for (int i = 0; i < 20; i++)
        c.push_back(label(10.0f + 70.0f * (i % 10), 20.0f + 30.0f * (i / 10), (float)(20 - i)));
    s.MaxLabels = 5;
    shown = LayoutEntityLabels(c, s, viewMin, viewMax);
    CHECK(shown.size() == 5);
    for (size_t index : shown)
        CHECK(index >= 15); // the five nearest

    // Names: trimmed, an empty one has a placeholder, long ones end in "..." on a character boundary.
    CHECK(ShortenEntityLabel("  Player  ") == "Player");
    CHECK(ShortenEntityLabel("   ") == "(nomsiz)");
    CHECK(ShortenEntityLabel("Abcdefghij", 8) == "Abcde...");
    CHECK(ShortenEntityLabel("Abcdefgh", 8) == "Abcdefgh");
    const std::string cyrillic = "\xd0\x9e\xd0\xb4\xd0\xb0\xd0\xbc\xd0\xbb\xd0\xb0\xd1\x80\xd0\xb8"; // 8 characters, 16 bytes
    CHECK(ShortenEntityLabel(cyrillic, 8) == cyrillic);
    CHECK(ShortenEntityLabel(cyrillic, 6) == std::string(cyrillic.substr(0, 6)) + "...");
}

// The scene statistics window's numbers (316): entities (active / inactive, roots, depth), components by
// type, scripts by class, prefab instances, and the assets the scene names - read from small files
// written here - with their memory estimates; materials bring their texture maps along.
TEST(SceneStatisticsCounting)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("zamin-stats-" + std::to_string((uint64_t)UUID()));
    for (const char* folder : { "Textures", "Models", "Audio", "Materials" })
        fs::create_directories(dir / folder);
    std::vector<uint8_t> rgba(64 * 32 * 4, 180), rgb(16 * 16 * 3, 90);
    CHECK(Image::WritePNG((dir / "Textures/hero.png").string(), 64, 32, 4, rgba.data(), false));
    CHECK(Image::WritePNG((dir / "Textures/bricks.png").string(), 16, 16, 3, rgb.data(), false));
    FileSystem::WriteTextFile(dir / "Models/quad.obj", "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                                         "vn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
    FileSystem::WriteTextFile(dir / "Materials/wall.zmat", "{ \"AlbedoMap\": \"Textures/bricks.png\", \"NormalMap\": \"\", \"Roughness\": 0.7 }");
    {
        // 1000 frames of 16-bit mono at 8 kHz.
        std::ofstream wav(dir / "Audio/beep.wav", std::ios::binary);
        auto u32 = [&](uint32_t v) { wav.write((const char*)&v, 4); };
        auto u16 = [&](uint16_t v) { wav.write((const char*)&v, 2); };
        const uint32_t frames = 1000, bytes = frames * 2;
        wav.write("RIFF", 4); u32(36 + bytes); wav.write("WAVE", 4);
        wav.write("fmt ", 4); u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16);
        wav.write("data", 4); u32(bytes);
        std::vector<int16_t> samples(frames);
        for (uint32_t i = 0; i < frames; i++)
            samples[i] = (int16_t)(std::sin((float)i * 0.3f) * 8000.0f);
        wav.write((const char*)samples.data(), bytes);
    }

    auto scene = CreateRef<Scene>();
    Entity root = scene->CreateEntity("Level");
    Entity hero = scene->CreateEntity("Hero");
    Entity ghost = scene->CreateEntity("Ghost");
    Entity wall = scene->CreateEntity("Wall");
    Entity speaker = scene->CreateEntity("Speaker");
    Entity brain = scene->CreateEntity("Brain");
    Entity empty = scene->CreateEntity("EmptyScript");
    scene->SetParent(hero, root, false);
    scene->SetParent(ghost, hero, false);
    hero.AddComponent<SpriteRendererComponent>().TexturePath = "Textures/hero.png";
    ghost.AddComponent<SpriteRendererComponent>().TexturePath = "Textures/hero.png";
    ghost.GetComponent<TagComponent>().Active = false;
    auto& mesh = wall.AddComponent<MeshRendererComponent>();
    mesh.Type = PrimitiveMesh::Model;
    mesh.ModelPath = "Models/quad.obj";
    mesh.MaterialPath = "Materials/wall.zmat";
    speaker.AddComponent<AudioSourceComponent>().Path = "Audio/beep.wav";
    hero.AddComponent<NativeScriptComponent>().ClassName = "TestScript";
    brain.AddComponent<NativeScriptComponent>().ClassName = "TestScript";
    empty.AddComponent<NativeScriptComponent>();
    scene->SetPrefabSource(root, "Prefabs/Level.zprefab");
    scene->SetPrefabSource(wall, "Prefabs/Wall.zprefab");
    scene->SetHiddenInEditor(speaker, true);
    scene->GetSettings().EnvironmentMap = "Sky/missing.hdr";
    CHECK(scene->GetPrefabSource(root) == "Prefabs/Level.zprefab");

    SceneStatisticsCollector collector;
    SceneStatistics s = collector.Collect(*scene, dir);
    CHECK(s.Entities == 7);
    CHECK(s.Inactive == 1 && s.Active == 6 && s.InactiveSelf == 1);
    CHECK(s.Roots == 5 && s.MaxDepth == 2);
    CHECK(s.HiddenInEditor == 1 && s.LockedInEditor == 0);
    CHECK(s.PrefabInstances == 2 && s.Prefabs.size() == 2);
    CHECK(s.FindPrefab("Prefabs/Wall.zprefab") && s.FindPrefab("Prefabs/Wall.zprefab")->Entities.front() == wall.GetUUID());
    // Components: every entity's transform; the most used first.
    CHECK(s.FindComponent("Transform") && s.FindComponent("Transform")->Count() == 7);
    CHECK(s.Components.front().Key == "Transform");
    CHECK(s.FindComponent("SpriteRenderer") && s.FindComponent("SpriteRenderer")->Count() == 2);
    CHECK(s.FindComponent("MeshRenderer")->Count() == 1 && s.FindComponent("AudioSource")->Count() == 1);
    CHECK(!s.FindComponent("Rigidbody2D"));
    size_t total = 0;
    for (const SceneStatsGroup& g : s.Components)
        total += g.Count();
    CHECK(total == s.ComponentTotal && s.ComponentTotal == 7 + 2 + 1 + 1 + 3);
    // Scripts by class ("" = a script component without one); hierarchy order inside a row.
    CHECK(s.ScriptComponents == 3 && s.Scripts.size() == 2);
    CHECK(s.Scripts.front().Key == "TestScript" && s.FindScript("TestScript")->Count() == 2);
    CHECK(s.FindScript("TestScript")->Entities[0] == hero.GetUUID());
    CHECK(s.FindScript("") && s.FindScript("")->Count() == 1);

    // Assets: the sprite texture (two users, named twice), the model, the material and its map, the
    // sound and the scene's sky (missing).
    CHECK(s.Assets.size() == 6);
    const SceneAssetStats* heroTex = s.FindAsset("Textures/hero.png");
    CHECK(heroTex && heroTex->Type == AssetType::Texture && heroTex->Exists && heroTex->Entities.size() == 2 && heroTex->Uses == 2);
    if (heroTex)
    {
        CHECK(heroTex->Width == 64 && heroTex->Height == 32 && heroTex->Channels == 4);
        const bool mips = AssetImporter::GetTextureSettings(dir / "Textures/hero.png").Mipmaps;
        CHECK(heroTex->Memory == AssetInfo::EstimateTextureMemory(64, 32, 4, mips));
        CHECK(heroTex->FileSize == (uint64_t)fs::file_size(dir / "Textures/hero.png"));
        CHECK(heroTex->Components.size() == 1 && heroTex->Components[0] == "SpriteRenderer");
    }
    const SceneAssetStats* bricks = s.FindAsset("Textures/bricks.png");
    CHECK(bricks && bricks->Channels == 3 && bricks->ViaMaterials.size() == 1 && bricks->ViaMaterials[0] == "Materials/wall.zmat");
    CHECK(bricks && bricks->Entities.size() == 1 && bricks->Entities[0] == wall.GetUUID());
    const SceneAssetStats* material = s.FindAsset("Materials/wall.zmat");
    CHECK(material && material->Type == AssetType::Material && material->Memory == 0);
    const SceneAssetStats* model = s.FindAsset("Models/quad.obj");
    ModelFileInfo quad = AssetInfo::ReadModel(dir / "Models/quad.obj");
    CHECK(model && quad.Valid && model->Vertices == quad.Vertices && model->Indices == 6 && model->Submeshes == 1);
    CHECK(model && model->Memory == EstimateMeshMemory(quad.Vertices, 0, 6));
    const SceneAssetStats* beep = s.FindAsset("Audio/beep.wav");
    CHECK(beep && beep->SampleRate == 8000 && beep->AudioChannels == 1 && !beep->Streamed);
    CHECK(beep && beep->Memory == EstimateAudioMemory(1000, 1, false) && beep->Memory == 4000);
    CHECK_NEAR(beep ? beep->Duration : 0.0, 0.125, 1e-6);
    const SceneAssetStats* sky = s.FindAsset("Sky/missing.hdr");
    CHECK(sky && !sky->Exists && sky->Entities.empty() && sky->Components.size() == 1 && sky->Components[0] == "Settings");
    CHECK(s.CountAssets(AssetType::Texture) == 2 && s.CountAssets(AssetType::Environment) == 1);
    CHECK(s.TextureMemory == heroTex->Memory + bricks->Memory);
    CHECK(s.MeshMemory == model->Memory && s.MeshVertices == quad.Vertices && s.MeshIndices == 6);
    CHECK(s.AudioMemory == 4000 && s.GetTotalMemory() == s.TextureMemory + s.MeshMemory + s.AudioMemory);

    // The estimates themselves.
    CHECK(GetTextureBytesPerPixel(1, false) == 1 && GetTextureBytesPerPixel(3, false) == 3 && GetTextureBytesPerPixel(2, false) == 4 &&
          GetTextureBytesPerPixel(4, true) == 16);
    CHECK(EstimateMeshMemory(10, 4, 30) == 6 * 32 + 4 * 64 + 30 * 4);
    CHECK(EstimateMeshMemory(2, 5, 0) == 2 * 64); // never more skinned vertices than vertices
    CHECK(EstimateAudioMemory(48000, 2, true) == 0 && EstimateAudioMemory(48000, 2, false) == 48000 * 2 * 4);

    // Sorting: by memory (largest first), by name, by users; ties by name.
    SortSceneAssetStats(s.Assets, SceneStatsColumn::Memory, false);
    for (size_t i = 1; i < s.Assets.size(); i++)
        CHECK(s.Assets[i - 1].Memory >= s.Assets[i].Memory);
    SortSceneAssetStats(s.Assets, SceneStatsColumn::Name, true);
    CHECK(s.Assets.front().Path == "Audio/beep.wav" && s.Assets.back().Path == "Textures/hero.png");
    SortSceneAssetStats(s.Assets, SceneStatsColumn::Count, false);
    CHECK(s.Assets.front().Path == "Textures/hero.png");
    SortSceneStatsGroups(s.Components, SceneStatsColumn::Name, true);
    CHECK(s.Components.front().Key == "AudioSource");
    SortSceneStatsGroups(s.Components, SceneStatsColumn::Count, true);
    CHECK(s.Components.back().Key == "Transform" && s.Components.front().Key == "AudioSource"); // ties (1 each) by name

    // Counting again reads the files from the cache; a changed file is read again.
    CHECK(collector.GetCachedFileCount() == 6);
    SceneStatistics again = collector.Collect(*scene, dir);
    CHECK(again.TextureMemory == s.TextureMemory && again.Assets.size() == 6);
    std::vector<uint8_t> bigger(128 * 128 * 4, 7);
    CHECK(Image::WritePNG((dir / "Textures/hero.png").string(), 128, 128, 4, bigger.data(), false));
    fs::last_write_time(dir / "Textures/hero.png", fs::last_write_time(dir / "Textures/hero.png") + std::chrono::seconds(2));
    SceneStatistics changed = collector.Collect(*scene, dir);
    CHECK(changed.FindAsset("Textures/hero.png") && changed.FindAsset("Textures/hero.png")->Width == 128);

    // The prefab marks are saved with the scene, not into a prefab made from an instance.
    CHECK(SceneSerializer::SerializeEntities(*scene, { root }, false).find("Prefabs/Level.zprefab") == std::string::npos);
    std::string text = SceneSerializer(scene).SerializeToString();
    auto loaded = CreateRef<Scene>();
    CHECK(SceneSerializer(loaded).DeserializeFromString(text));
    Entity loadedRoot = loaded->FindEntityByName("Level");
    CHECK(loadedRoot && loaded->GetPrefabSource(loadedRoot) == "Prefabs/Level.zprefab");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Opening code in the external editor (317): command lines split like a shell, placeholders
// substituted, the default command from $VISUAL / $EDITOR / VS Code / xdg-open (terminal editors in a
// terminal), starting a program without waiting for it, and finding a script class's source.
TEST(ExternalEditorCommands)
{
    namespace fs = std::filesystem;
    using Args = std::vector<std::string>;
    CHECK(ExternalEditor::SplitCommandLine("code  -g \"my file\" 'a b' c\\ d") == (Args{ "code", "-g", "my file", "a b", "c d" }));
    CHECK(ExternalEditor::SplitCommandLine("  ").empty());
    CHECK(ExternalEditor::SplitCommandLine("say \"it's \\\"x\\\"\" ''") == (Args{ "say", "it's \"x\"", "" }));

    CHECK(ExternalEditor::ExpandCommand("code -g {file}:{line}:{col}", "/p/Source/Player.cpp", 42, 7) ==
          (Args{ "code", "-g", "/p/Source/Player.cpp:42:7" }));
    CHECK(ExternalEditor::ExpandCommand("kate -l {line} -c {column} {file}", "/x/a b.cpp") == (Args{ "kate", "-l", "1", "-c", "1", "/x/a b.cpp" }));
    CHECK(ExternalEditor::ExpandCommand("myedit --wait", "/x/y.h", 3) == (Args{ "myedit", "--wait", "/x/y.h" })); // the file at the end
    CHECK(ExternalEditor::ExpandCommand("term --cwd {dir} {file} {nope}", "/x/y.h") == (Args{ "term", "--cwd", "/x", "/x/y.h", "{nope}" }));
    CHECK(ExternalEditor::ExpandCommand("'/opt/My Editor/bin/ed' +{line} {file}", "/s/a.cpp", 9) == (Args{ "/opt/My Editor/bin/ed", "+9", "/s/a.cpp" }));

    // The default command, with a made-up environment and PATH.
    std::map<std::string, std::string> env;
    std::set<std::string> installed;
    auto getEnv = [&](const char* name) -> const char* {
        auto it = env.find(name);
        return it == env.end() ? nullptr : it->second.c_str();
    };
    auto onPath = [&](std::string_view program) { return installed.count(std::string(program)) > 0; };
    auto defaultCommand = [&]() { return ExternalEditor::GetDefaultCommand(getEnv, onPath); };
    CHECK(defaultCommand() == "xdg-open {file}");
    installed = { "code" };
    CHECK(defaultCommand() == "code -g {file}:{line}:{col}");
    installed = { "code", "subl", "gedit", "vim" };
    env["EDITOR"] = "subl";
    CHECK(defaultCommand() == "subl {file}:{line}:{col}");
    env["VISUAL"] = "gedit";
    CHECK(defaultCommand() == "gedit +{line} {file}");            // $VISUAL first
    env["VISUAL"] = "notinstalled";
    CHECK(defaultCommand() == "subl {file}:{line}:{col}");        // a program that is not there is skipped
    env.erase("VISUAL");
    env["EDITOR"] = "vim";
    CHECK(defaultCommand() == "code -g {file}:{line}:{col}");     // a terminal editor without a terminal
    installed.insert("xterm");
    CHECK(defaultCommand() == "xterm -e vim +{line} {file}");
    installed.insert("gnome-terminal");
    CHECK(defaultCommand() == "gnome-terminal -- vim +{line} {file}");
    env["TERMINAL"] = "foot";
    installed.insert("foot");
    CHECK(defaultCommand() == "foot -e vim +{line} {file}");
    env["EDITOR"] = "myeditor --flag";
    installed.insert("myeditor");
    CHECK(defaultCommand() == "myeditor --flag {file}");          // one it does not know: its own command line
    CHECK(ExternalEditor::CommandForProgram("/usr/bin/code", getEnv, onPath) == "/usr/bin/code -g {file}:{line}:{col}");
    CHECK(ExternalEditor::CommandForProgram("unknown-editor", getEnv, onPath).empty());
    std::vector<std::string> offered = ExternalEditor::GetInstalledCommands(onPath);
    CHECK(std::find(offered.begin(), offered.end(), "subl {file}:{line}:{col}") != offered.end());
    CHECK(std::find_if(offered.begin(), offered.end(), [](const std::string& c) { return c.starts_with("vim "); }) == offered.end());

    // Starting programs: not waited for, errors reported.
    std::string error;
    CHECK(ExternalEditor::IsOnPath("sh") && !ExternalEditor::IsOnPath("zamin-no-such-program-xyz") && !ExternalEditor::IsOnPath(""));
    CHECK(!ExternalEditor::Launch({}, &error) && !error.empty());
    error.clear();
    CHECK(!ExternalEditor::Launch({ "zamin-no-such-program-xyz", "a.cpp" }, &error) && error.find("zamin-no-such-program-xyz") != std::string::npos);
    const fs::path dir = fs::temp_directory_path() / ("zamin-external-" + std::to_string((uint64_t)UUID()));
    fs::create_directories(dir / "Source" / "Enemies");
    const fs::path marker = dir / "opened.txt";
    // A stand-in "editor": writes the arguments it got to a file.
    CHECK(ExternalEditor::Open("sh -c 'echo \"$1:$2\" > \"$3\"' editor {file} {line} " + marker.string(), dir / "Source" / "Player.cpp", 17, 0, &error));
    std::string written;
    for (int i = 0; i < 200 && written.empty(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        written = FileSystem::ReadTextFile(marker);
    }
    CHECK(written.starts_with((dir / "Source" / "Player.cpp").string() + ":17"));

    // Finding a script class's source.
    FileSystem::WriteTextFile(dir / "Source" / "Player.cpp",
                              "#include <Zamin/Zamin.h>\n"
                              "using namespace Zamin;\n"
                              "class PlayerHelper;\n"
                              "// ZM_REGISTER_SCRIPT(Player) in a comment does not count\n"
                              "class Player : public ScriptableEntity\n"
                              "{\n"
                              "};\n"
                              "ZM_REGISTER_SCRIPT(Player, ZM_FIELD(Player, Speed))\n");
    FileSystem::WriteTextFile(dir / "Source" / "Enemies" / "Enemy.h", "#pragma once\nstruct Enemy : ScriptableEntity\n{\n};\n");
    FileSystem::WriteTextFile(dir / "Source" / "Enemies" / "Enemy.cpp", "#include \"Enemy.h\"\n\nZM_REGISTER_SCRIPT( Enemy )\n");
    FileSystem::WriteTextFile(dir / "Source" / "Helpers.h", "#pragma once\nclass Player;\nclass  Helper final\n{\n};\n");
    FileSystem::WriteTextFile(dir / "Source" / "notes.txt", "class Missing {};\nZM_REGISTER_SCRIPT(Missing)\n");
    SourceLocation player = ExternalEditor::FindScriptSource(dir / "Source", "Player");
    CHECK(player.IsValid() && player.File.filename() == "Player.cpp" && player.Line == 5 && player.Column == 1);
    SourceLocation enemy = ExternalEditor::FindScriptSource(dir / "Source", "Enemy");
    CHECK(enemy.IsValid() && enemy.File.filename() == "Enemy.cpp" && enemy.Line == 3); // registered where it is not declared
    SourceLocation helper = ExternalEditor::FindScriptSource(dir / "Source", "Helper");
    CHECK(helper.IsValid() && helper.File.filename() == "Helpers.h" && helper.Line == 3); // only declared
    CHECK(!ExternalEditor::FindScriptSource(dir / "Source", "Missing").IsValid());          // not a source file
    CHECK(!ExternalEditor::FindScriptSource(dir / "Source", "Play").IsValid());             // not a prefix match
    CHECK(!ExternalEditor::FindScriptSource(dir / "Nowhere", "Player").IsValid());
    CHECK(ExternalEditor::IsSourceFile("a/B.CPP") && ExternalEditor::IsSourceFile("x.hpp") && !ExternalEditor::IsSourceFile("x.zscene"));
    // The sample projects' classes are found where they are registered.
    SourceLocation crystal = ExternalEditor::FindScriptSource(fs::path(ZAMIN_SOURCE_DIR) / "Projects" / "Adventure3D" / "Source", "Crystal");
    CHECK(crystal.IsValid() && crystal.File.filename() == "World.cpp" && crystal.Line > 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Compiler output (318): GCC and Clang diagnostics with file / line / column, their notes, snippets
// and context, repeats dropped, linker errors (GNU ld, lld) and CMake errors.
TEST(CompilerOutputParsing)
{
    const char* gcc =
        "[1/3] Building CXX object CMakeFiles/Game.dir/Source/Player.cpp.o\n"
        "FAILED: CMakeFiles/Game.dir/Source/Player.cpp.o\n"
        "/usr/bin/c++ -DGame_EXPORTS -O2 -c /proj/Source/Player.cpp\n"
        "In file included from /proj/Source/Player.cpp:2:\n"
        "/proj/Source/Common.h:7:9: warning: unused variable 'unused' [-Wunused-variable]\n"
        "    7 |     int unused = 0;\n"
        "      |         ^~~~~~\n"
        "/proj/Source/Player.cpp: In member function 'virtual void Player::OnUpdate(Zamin::Timestep)':\n"
        "/proj/Source/Player.cpp:24:9: error: 'Jump' was not declared in this scope; did you mean 'jump'?\n"
        "   24 |         Jump();\n"
        "      |         ^~~~\n"
        "      |         jump\n"
        "/proj/Source/Player.cpp:30:22: error: no matching function for call to 'Foo::Bar(int)'\n"
        "   30 |         m_Foo.Bar(3);\n"
        "/proj/Source/Foo.h:5:10: note: candidate: 'void Foo::Bar()'\n"
        "    5 |     void Bar();\n"
        "      |          ^~~\n"
        "/proj/Source/Foo.h:5:10: note:   candidate expects 0 arguments, 1 provided\n"
        "[2/3] Building CXX object CMakeFiles/Game.dir/Source/Enemy.cpp.o\n"
        "In file included from /proj/Source/Enemy.cpp:1:\n"
        "/proj/Source/Common.h:7:9: warning: unused variable 'unused' [-Wunused-variable]\n"
        "    7 |     int unused = 0;\n"
        "Source/Enemy.cpp:12: warning: relative path, no column\n"
        "ninja: build stopped: subcommand failed.\n";
    CompilerOutputParser p;
    p.AddText(gcc);
    p.Finish();
    const auto& d = p.GetDiagnostics();
    CHECK(d.size() == 4);
    CHECK(p.GetErrorCount() == 2 && p.GetWarningCount() == 2);
    if (d.size() == 4)
    {
        CHECK(d[0].Severity == DiagnosticSeverity::Warning && d[0].File == "/proj/Source/Common.h" && d[0].Line == 7 && d[0].Column == 9);
        CHECK(d[0].Message == "unused variable 'unused'" && d[0].Option == "-Wunused-variable");
        CHECK(d[0].Context.size() == 1 && d[0].Context[0] == "In file included from /proj/Source/Player.cpp:2:");
        CHECK(d[0].Snippet.size() == 2 && d[0].Snippet[0].find("int unused") != std::string::npos);
        CHECK(d[1].Severity == DiagnosticSeverity::Error && d[1].Line == 24 && d[1].Column == 9);
        CHECK(d[1].Message == "'Jump' was not declared in this scope; did you mean 'jump'?");
        CHECK(d[1].Context.size() == 1 && d[1].Context[0].find("In member function") != std::string::npos);
        CHECK(d[1].Snippet.size() == 3);
        CHECK(d[2].Line == 30 && d[2].Notes.size() == 2 && d[2].Snippet.size() == 1);
        CHECK(d[2].Notes.size() == 2 && d[2].Notes[0].File == "/proj/Source/Foo.h" && d[2].Notes[0].Line == 5 &&
              d[2].Notes[0].Severity == DiagnosticSeverity::Note && d[2].Notes[0].Snippet.size() == 2);
        CHECK(d[2].Notes.size() == 2 && d[2].Notes[1].Message == "candidate expects 0 arguments, 1 provided");
        // The header's warning again (Enemy.cpp includes it too) is kept once; a line without a column.
        CHECK(d[3].File == "Source/Enemy.cpp" && d[3].Line == 12 && d[3].Column == 0 && d[3].Message == "relative path, no column");
        p.ResolvePaths("/proj/Build");
        CHECK(p.GetDiagnostics()[3].File == "/proj/Build/Source/Enemy.cpp" && p.GetDiagnostics()[0].File == "/proj/Source/Common.h");
        CHECK(d[1].Format() == "/proj/Source/Player.cpp:24:9: error: 'Jump' was not declared in this scope; did you mean 'jump'?");
        CHECK(d[0].Format().ends_with("[-Wunused-variable]"));
    }

    // GCC's template trail: the instantiation context and "required from here" come before the error.
    CompilerOutputParser t;
    t.AddText("/p/a.h: In instantiation of 'void Store(T) [with T = int]':\n"
              "/p/a.cpp:10:10:   required from here\n"
              "/p/a.h:3:5: error: 'int' is not a class\n"
              "    3 |     value.Save();\n");
    t.Finish();
    CHECK(t.GetDiagnostics().size() == 1);
    if (!t.GetDiagnostics().empty())
    {
        const CompilerDiagnostic& e = t.GetDiagnostics()[0];
        CHECK(e.Line == 3 && e.Context.size() == 1 && e.Notes.size() == 1 && e.Notes[0].Line == 10 && e.Notes[0].Message == "required from here");
        CHECK(e.Snippet.size() == 1);
    }

    // Clang: the same shape; tool messages without a file; -Werror options.
    CompilerOutputParser c;
    c.AddText("/p/b.cpp:4:12: error: use of undeclared identifier 'speed'\n"
              "    return speed * 2;\n"
              "           ^\n"
              "/p/b.cpp:9:7: warning: unused variable 'x' [-Werror,-Wunused-variable]\n"
              "2 errors generated.\n"
              "clang++: error: no such file or directory: 'missing.cpp'\n"
              "c++: fatal error: no input files\n");
    c.Finish();
    CHECK(c.GetDiagnostics().size() == 4 && c.GetErrorCount() == 3);
    if (c.GetDiagnostics().size() == 4)
    {
        CHECK(c.GetDiagnostics()[0].Snippet.size() == 2 && c.GetDiagnostics()[0].Column == 12);
        CHECK(c.GetDiagnostics()[1].Option == "-Werror,-Wunused-variable");
        CHECK(c.GetDiagnostics()[2].File.empty() && c.GetDiagnostics()[2].Message == "no such file or directory: 'missing.cpp'");
        CHECK(!c.GetDiagnostics()[3].HasLocation() && c.GetDiagnostics()[3].Message == "no input files");
    }

    // GNU ld: undefined references with the function they are in; collect2's summary is left out.
    CompilerOutputParser l;
    l.AddText("[3/3] Linking CXX shared library libGame.so\n"
              "/usr/bin/ld: CMakeFiles/Game.dir/Source/Player.cpp.o: in function `Player::OnUpdate(Zamin::Timestep)':\n"
              "/proj/Source/Player.cpp:42:(.text+0x2a): undefined reference to `Enemy::Hit()'\n"
              "/usr/bin/ld: Player.cpp:(.text+0x5f): undefined reference to `Enemy::Die()'\n"
              "/usr/bin/ld: cannot find -lmissing: No such file or directory\n"
              "/usr/bin/ld: warning: libfoo.so, needed by libbar.so, not found\n"
              "collect2: error: ld returned 1 exit status\n");
    l.Finish();
    const auto& ld = l.GetDiagnostics();
    CHECK(ld.size() == 4 && l.GetErrorCount() == 3 && l.GetWarningCount() == 1);
    if (ld.size() == 4)
    {
        CHECK(ld[0].Linker && ld[0].File == "/proj/Source/Player.cpp" && ld[0].Line == 42 && ld[0].Message == "undefined reference to `Enemy::Hit()'");
        CHECK(ld[0].Context.size() == 1 && ld[0].Context[0] == "in function `Player::OnUpdate(Zamin::Timestep)'");
        CHECK(ld[1].File == "Player.cpp" && ld[1].Line == 0 && ld[1].Message == "undefined reference to `Enemy::Die()'");
        CHECK(!ld[2].HasLocation() && ld[2].Message == "cannot find -lmissing: No such file or directory");
        CHECK(ld[3].Severity == DiagnosticSeverity::Warning && ld[3].Linker);
    }
    CompilerOutputParser onlySummary;
    onlySummary.AddLine("collect2: error: ld returned 1 exit status");
    onlySummary.Finish();
    CHECK(onlySummary.GetErrorCount() == 1 && onlySummary.GetDiagnostics()[0].Linker);

    // lld: the place comes from ">>> referenced by".
    CompilerOutputParser lld;
    lld.AddText("ld.lld: error: undefined symbol: Enemy::Hit()\n"
                ">>> referenced by Player.cpp:42 (/proj/Source/Player.cpp:42)\n"
                ">>>               CMakeFiles/Game.dir/Source/Player.cpp.o:(Player::OnUpdate(Zamin::Timestep))\n"
                "clang++: error: linker command failed with exit code 1 (use -v to see invocation)\n");
    lld.Finish();
    CHECK(lld.GetDiagnostics().size() == 1);
    if (!lld.GetDiagnostics().empty())
    {
        const CompilerDiagnostic& e = lld.GetDiagnostics()[0];
        CHECK(e.Linker && e.File == "/proj/Source/Player.cpp" && e.Line == 42 && e.Message == "undefined symbol: Enemy::Hit()" && e.Snippet.size() == 2);
    }

    // CMake: the message's indented lines joined.
    CompilerOutputParser cm;
    cm.AddText("-- Configuring incomplete, errors occurred!\n"
               "CMake Error at CMakeLists.txt:12 (add_library):\n"
               "  Cannot find source file:\n"
               "\n"
               "    Source/Missing.cpp\n"
               "\n"
               "CMake Warning (dev) at CMakeLists.txt:3 (project):\n"
               "  Policy CMP0048 is not set.\n"
               "CMake Error: The source directory \"/nowhere\" does not exist.\n");
    cm.Finish();
    CHECK(cm.GetDiagnostics().size() == 3 && cm.GetErrorCount() == 2 && cm.GetWarningCount() == 1);
    if (cm.GetDiagnostics().size() == 3)
    {
        const CompilerDiagnostic& e = cm.GetDiagnostics()[0];
        CHECK(e.CMake && e.File == "CMakeLists.txt" && e.Line == 12 && e.Message == "Cannot find source file:");
        CHECK(e.Context.size() == 1 && e.Context[0] == "(add_library)");
        CHECK(cm.GetDiagnostics()[1].Line == 3 && cm.GetDiagnostics()[1].Severity == DiagnosticSeverity::Warning);
        CHECK(cm.GetDiagnostics()[2].Message == "The source directory \"/nowhere\" does not exist." && !cm.GetDiagnostics()[2].HasLocation());
    }

    // Single lines; colour codes; prose is not a diagnostic.
    CompilerDiagnostic one;
    CHECK(CompilerOutputParser::ParseDiagnosticLine("\x1b[1m/p/x.cpp:3:4: \x1b[31merror: \x1b[0mbad", one) == false); // raw colours: AddLine strips them
    CompilerOutputParser colored;
    colored.AddLine("\x1b[01m\x1b[K/p/x.cpp:3:4:\x1b[m\x1b[K \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[Kbad thing");
    CHECK(colored.GetErrorCount() == 1 && colored.GetDiagnostics()[0].Line == 3 && colored.GetDiagnostics()[0].Message == "bad thing");
    CHECK(!CompilerOutputParser::ParseDiagnosticLine("Building target Game", one));
    CHECK(!CompilerOutputParser::ParseDiagnosticLine("the docs say: error: is a word", one));
    CHECK(CompilerOutputParser::ParseDiagnosticLine("/p/y.cpp:1:2: fatal error: Zamin.h: No such file or directory", one) &&
          one.Severity == DiagnosticSeverity::Error && one.Message == "Zamin.h: No such file or directory" && one.Column == 2);
    CHECK(std::string(CompilerDiagnostic::GetSeverityName(DiagnosticSeverity::Warning)) == "ogohlantirish");
    p.Clear();
    CHECK(p.GetDiagnostics().empty() && p.GetErrorCount() == 0);
}

// Idle throttling (319): when the editor draws fewer frames, how long a frame waits for events, and
// the frame rate it settles at over a stretch of time without input.
TEST(IdleThrottleDecision)
{
    IdleThrottleSettings settings;
    IdleThrottleState state;
    state.SecondsSinceInput = 0.0;
    CHECK(!IdleThrottle::Decide(settings, state).IsLimited());          // input just now: full speed
    state.SecondsSinceInput = 0.99;
    CHECK(!IdleThrottle::Decide(settings, state).IsLimited());          // still within IdleDelay (1 s)
    state.SecondsSinceInput = 2.0;
    FrameLimit idle = IdleThrottle::Decide(settings, state);
    CHECK(idle.Reason == FrameLimitReason::Idle && idle.FPS == 30.0f);
    CHECK_NEAR(idle.GetFrameSeconds(), 1.0 / 30.0, 1e-9);
    // Things that keep full speed while focused.
    for (int which = 0; which < 4; which++)
    {
        IdleThrottleState busy = state;
        if (which == 0) busy.Animating = true;      // a preview, the camera moving, a drag
        if (which == 1) busy.Running = true;        // the game / simulation
        if (which == 2) busy.PendingWork = true;    // loading, building
        if (which == 3) busy.Automation = true;     // ZAMIN_MAX_FRAMES runs
        CHECK(!IdleThrottle::Decide(settings, busy).IsLimited());
    }
    // Not in front: the unfocused rate, even right after input and with a preview playing...
    IdleThrottleState unfocused = state;
    unfocused.Focused = false;
    unfocused.SecondsSinceInput = 0.0;
    unfocused.Animating = true;
    FrameLimit back = IdleThrottle::Decide(settings, unfocused);
    CHECK(back.Reason == FrameLimitReason::Unfocused && back.FPS == 10.0f);
    // ...but never while the game runs, work is pending or for automation.
    unfocused.Running = true;
    CHECK(!IdleThrottle::Decide(settings, unfocused).IsLimited());
    unfocused.Running = false;
    unfocused.Automation = true;
    CHECK(!IdleThrottle::Decide(settings, unfocused).IsLimited());
    // Switched off: never.
    IdleThrottleSettings off;
    off.Enabled = false;
    CHECK(!IdleThrottle::Decide(off, state).IsLimited() && !IdleThrottle::Decide(off, IdleThrottleState{ false }).IsLimited());

    // The wait: the rest of the frame's time (none when the frame took longer, or without a limit).
    CHECK_NEAR(IdleThrottle::GetWaitSeconds(idle, 0.010), 1.0 / 30.0 - 0.010, 1e-9);
    CHECK(IdleThrottle::GetWaitSeconds(idle, 0.050) == 0.0);
    CHECK(IdleThrottle::GetWaitSeconds(FrameLimit{}, 0.0) == 0.0);
    CHECK_NEAR(IdleThrottle::GetWaitSeconds(back, -1.0), 0.1, 1e-9);

    // A stretch of frames at 60 Hz (vsync, 4 ms of work each): input at 0 s, then nothing. The first
    // second stays at 60, then the frames settle at the idle rate.
    double t = 0.0;
    int framesFirst = 0, framesThird = 0;
    while (t < 3.0)
    {
        IdleThrottleState s;
        s.SecondsSinceInput = t;
        const FrameLimit limit = IdleThrottle::Decide(settings, s);
        const double work = 1.0 / 60.0;
        const double frame = work + IdleThrottle::GetWaitSeconds(limit, work);
        if (t < 1.0) framesFirst++;
        if (t >= 2.0) framesThird++;
        t += frame;
    }
    CHECK(framesFirst >= 59 && framesFirst <= 61);
    CHECK(framesThird >= 29 && framesThird <= 31);

    // Settings kept in range; the preferences file keeps them.
    IdleThrottleSettings bad;
    bad.IdleFPS = 0.0f;
    bad.UnfocusedFPS = std::numeric_limits<float>::quiet_NaN();
    bad.IdleDelay = 1000.0f;
    bad.Sanitize();
    CHECK(bad.IdleFPS == IdleThrottleSettings::MinFPS && bad.UnfocusedFPS == 10.0f && bad.IdleDelay == IdleThrottleSettings::MaxDelay);
    EditorPreferences prefs;
    prefs.Idle.Enabled = false;
    prefs.Idle.IdleFPS = 24.0f;
    prefs.Idle.UnfocusedFPS = 5.0f;
    prefs.Idle.IdleDelay = 2.5f;
    prefs.ExternalEditor.Command = "subl {file}:{line}";
    EditorPreferences loaded;
    CHECK(loaded.FromJson(prefs.ToJson()));
    CHECK(!loaded.Idle.Enabled && loaded.Idle.IdleFPS == 24.0f && loaded.Idle.UnfocusedFPS == 5.0f && loaded.Idle.IdleDelay == 2.5f);
    CHECK(loaded.ExternalEditor.Command == "subl {file}:{line}");
    EditorPreferences older; // a file from before these settings: the defaults stay
    CHECK(older.FromJson("{ \"ZaminEditorPreferences\": 1, \"IdleThrottle\": { \"IdleFPS\": 0 } }"));
    CHECK(older.Idle.Enabled && older.Idle.IdleFPS == IdleThrottleSettings::MinFPS && older.ExternalEditor.Command.empty());
    CHECK(std::string(IdleThrottle::GetReasonName(FrameLimitReason::Unfocused)) == "fokussiz");
}

// Editor II (320): the M16 pieces the other tests leave out - console level filter, autosave interval
// and lock file, recent list capacity, fixed / modified shortcuts, the undo history's saved state,
// the layouts folder, a compiler error opened in the external editor, prefab instances placed from a
// prefab file and counted by the statistics.
TEST(EditorIIPureLogic)
{
    namespace fs = std::filesystem;
    // Console: the level toggles (Error covers Critical).
    ConsoleFilterSettings console;
    CHECK(!console.AcceptsLevel(LogLevel::Trace) && console.AcceptsLevel(LogLevel::Info) && console.AcceptsLevel(LogLevel::Critical));
    console.ShowError = false;
    console.ShowTrace = true;
    CHECK(!console.AcceptsLevel(LogLevel::Error) && !console.AcceptsLevel(LogLevel::Critical) && console.AcceptsLevel(LogLevel::Trace));

    // Autosave: the interval in seconds; the lock file's text both ways.
    AutosaveSettings autosave;
    autosave.IntervalMinutes = 3;
    CHECK(autosave.GetIntervalSeconds() == 180.0);
    EditorAutosave::LockInfo lock{ 4242, 1700000123, "ishxona" }, lockBack;
    CHECK(EditorAutosave::LockFromJson(EditorAutosave::LockToJson(lock), lockBack));
    CHECK(lockBack.ProcessId == 4242 && lockBack.StartedAt == 1700000123 && lockBack.Host == "ishxona");
    CHECK(!EditorAutosave::LockFromJson("not json", lockBack) && !EditorAutosave::LockFromJson("[1, 2]", lockBack));

    // Recent files: at least one is kept.
    RecentFileList none(0);
    CHECK(none.GetCapacity() == 1);
    none.Add("/a.zscene");
    none.Add("/b.zscene");
    CHECK(none.GetEntries().size() == 1 && none.GetEntries()[0].Path == "/b.zscene");

    // Shortcuts: fixed ones are listed but cannot be rebound; modified ones are counted.
    ShortcutRegistry shortcuts;
    shortcuts.Register("test.save", "Fayl", "Saqlash", KeyChord(Key::S, true));
    shortcuts.RegisterFixed("test.fly", "Kamera", "Uchish", KeyChord(), ShortcutScope::SceneView, "O'ng tugma + WASD");
    CHECK(!shortcuts.Rebind("test.fly", KeyChord(Key::F)) && !shortcuts.Find("test.fly")->IsModified());
    CHECK(shortcuts.Find("test.fly")->Display == "O'ng tugma + WASD");
    CHECK(shortcuts.Rebind("test.save", KeyChord(Key::S, true, true)) && shortcuts.Find("test.save")->IsModified() && shortcuts.CountModified() == 1);
    CHECK(shortcuts.ResetToDefault("test.save") && !shortcuts.Find("test.save")->IsModified());
    CHECK(KeyChord::IsModifierKey(Key::LeftControl) && KeyChord::IsModifierKey(Key::RightShift) && !KeyChord::IsModifierKey(Key::S));

    // Undo: the saved state forgotten (the file was deleted / saved elsewhere) and asked per state.
    UndoHistory history(10, 10);
    history.Reset("A", {}, "Ochildi", 1.0);
    CHECK(history.CommitEdit("B", {}, "O'zgardi", 2.0));
    CHECK(history.IsSavedState(0) && !history.IsSavedState(1) && !history.IsAtSavedState());
    history.ClearSaved();
    CHECK(!history.IsSavedState(0) && !history.IsSavedState(1));
    history.Undo();
    CHECK(!history.IsAtSavedState());

    // Layouts: another folder; names found whatever their letter case.
    const fs::path layouts = fs::temp_directory_path() / ("zamin-layouts-" + std::to_string((uint64_t)UUID()));
    EditorLayoutLibrary library(layouts / "a");
    library.SetDirectory(layouts / "b");
    CHECK(library.GetDirectory() == layouts / "b");
    EditorLayout layout;
    layout.Name = "Katta Ekran";
    layout.Ini = "[Window][###Console]\n";
    CHECK(library.Save(layout));
    CHECK(library.Contains("katta ekran") && !library.Contains("boshqa") && library.GetNames()[0] == "Katta Ekran");
    CHECK(library.Load("KATTA EKRAN") && library.Load("KATTA EKRAN")->Ini == layout.Ini);
    CHECK(fs::exists(layouts / "b") && !fs::exists(layouts / "a"));

    // A compiler error's place, given to the external editor's command (the window's double click).
    CompilerOutputParser parser;
    parser.AddLine("Source/Player.cpp:24:9: error: 'Jump' was not declared in this scope");
    parser.Finish();
    parser.ResolvePaths("/proj");
    const CompilerDiagnostic& error = parser.GetDiagnostics().front();
    CHECK(ExternalEditor::ExpandCommand("code -g {file}:{line}:{col}", error.File, error.Line, error.Column) ==
          (std::vector<std::string>{ "code", "-g", "/proj/Source/Player.cpp:24:9" }));

    // Prefab instances: placed from a prefab file, marked, duplicated - the statistics count them.
    const fs::path prefabFile = layouts / "Crate.zprefab";
    {
        auto source = CreateRef<Scene>();
        Entity crate = source->CreateEntity("Crate");
        Entity lid = source->CreateEntity("Lid");
        source->SetParent(lid, crate, false);
        source->SetPrefabSource(crate, "Prefabs/Old.zprefab"); // not written into the prefab itself
        CHECK(source->SavePrefab(crate, prefabFile));
    }
    auto scene = CreateRef<Scene>();
    Entity first = scene->InstantiatePrefab(prefabFile);
    CHECK(first && scene->GetPrefabSource(first).empty()); // the editor marks it (InstantiatePrefabInEditor)
    scene->SetPrefabSource(first, "Prefabs/Crate.zprefab");
    std::vector<Entity> copies = scene->DuplicateEntities({ first });
    CHECK(copies.size() == 1 && scene->GetPrefabSource(copies[0]) == "Prefabs/Crate.zprefab"); // copies stay instances
    scene->CreateEntity("Loose");
    SceneStatisticsCollector collector;
    SceneStatistics stats = collector.Collect(*scene, layouts);
    CHECK(stats.Entities == 5 && stats.PrefabInstances == 2);
    CHECK(stats.FindPrefab("Prefabs/Crate.zprefab") && stats.FindPrefab("Prefabs/Crate.zprefab")->Count() == 2);
    scene->SetPrefabSource(first, "");
    CHECK(!first.HasComponent<EditorMetadataComponent>()); // nothing left to remember
    CHECK(collector.Collect(*scene, layouts).PrefabInstances == 1);
    std::error_code ec;
    fs::remove_all(layouts, ec);
}

