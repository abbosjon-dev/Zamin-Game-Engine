#!/usr/bin/env python3
"""Real C++ editor UI regression. Run under xvfb-run (1600x1000x24).
Requires xdotool, ImageMagick, CMake and a C++ compiler; clangd for the semantic phases (they are skipped, with a
message, when clangd is not installed). Uses an isolated project/home. Widgets are found through the rectangles and
values the editor writes (ZAMIN_EDITOR_VISUAL_LAYOUT), never fixed coordinates.
ZAMIN_BUILD_DIR selects the engine build; output (screenshots, the log) goes to <build>/CodeEditor (--out DIR).

  1. typing, Ctrl+S, undo/redo, the catalogue's Ctrl+Space completion, F7 with a compile error, the error's
     line/column navigation, the fix, build + hot reload and the debounced crash recovery (as before);
  2. clangd: semantic completion of a project class member (Ctrl+Space), a live diagnostic appearing and going away
     while typing (squiggle + the Live list), Ctrl+click and F12 go-to-definition into other files, F2 rename across
     three files with a preview, applied atomically, then undone with Ctrl+Z (every file restored on disk);
  3. without clangd (Preferences path to a missing program): the one-line notice and the catalogue completion.
"""
import argparse, json, os, shutil, signal, subprocess, tempfile, time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--out', default=None)
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
build = Path(os.environ.get('ZAMIN_BUILD_DIR', str(repo / 'build'))).resolve()
out = Path(args.out).resolve() if args.out else build / 'CodeEditor'
out.mkdir(parents=True, exist_ok=True)
temporary = tempfile.TemporaryDirectory(prefix='zamin-code-editor-ui-')
work = Path(temporary.name)
project = work / 'Project'
shutil.copytree(repo / 'Tests/Fixtures/HotReload', project, ignore=shutil.ignore_patterns('Build', '.zamin', 'Binaries', '*.zmeta'))
source = project / 'Source/HotReloadProbe.cpp'
config = project / 'HotReload.zproject'
settings = json.loads(config.read_text())
settings['Project']['ScriptModulePath'] = ''
config.write_text(json.dumps(settings, indent=2))
source.write_text('#include <Zamin/Scripting/Visual/VisualGraph.h>\n' + source.read_text())
# A project class used from two other files (the semantic phases).
hero_h, hero_cpp, arena = project / 'Source/Hero.h', project / 'Source/Hero.cpp', project / 'Source/Arena.cpp'
hero_h.write_text('#pragma once\nstruct Hero\n{\n    int Health = 3;\n    int Stamina = 5;\n    void Heal(int amount);\n};\n')
hero_cpp.write_text('#include "Hero.h"\nvoid Hero::Heal(int amount) { Health += amount; }\n')
arena.write_text('#include "Hero.h"\nint ArenaScore()\n{\n    Hero hero;\n    hero.Heal(2);\n    return hero.Health;\n}\n')
clangd = shutil.which('clangd') or next((shutil.which(f'clangd-{v}') for v in range(21, 13, -1) if shutil.which(f'clangd-{v}')), None)
layout = work / 'layout.json'
log_path = work / 'editor.log'
app = None


def start(home, extra=None):
    global app
    home.mkdir(exist_ok=True)
    env = dict(os.environ, HOME=str(home), ZAMIN_EDITOR_CODE='Source/HotReloadProbe.cpp', ZAMIN_MAX_FRAMES='100000',
               ZAMIN_EDITOR_VISUAL_LAYOUT=str(layout), **(extra or {}))
    if layout.exists():
        layout.unlink()
    app = subprocess.Popen([str(build / 'bin/ZaminEditor'), str(project)], env=env, stdout=log_path.open('a'),
                           stderr=subprocess.STDOUT, start_new_session=True)


def stop():
    if app and app.poll() is None:
        os.killpg(app.pid, signal.SIGTERM)
        try:
            app.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(app.pid, signal.SIGKILL)
            app.wait()


def x(*a):
    subprocess.run(['xdotool', *map(str, a)], check=True, stdout=subprocess.DEVNULL)


def snapshot():
    for _ in range(40):
        try:
            return json.loads(layout.read_text())
        except (OSError, ValueError):
            time.sleep(.1)
    return {'Items': {}, 'Values': {}}


def items():
    return snapshot().get('Items', {})


def values():
    return snapshot().get('Values', {})


def wait_for(fn, seconds=40, what='UI action'):
    end = time.time() + seconds
    while time.time() < end:
        if app.poll() is not None:
            raise RuntimeError('Editor exited early')
        result = fn()
        if result:
            return result
        time.sleep(0.2)
    raise RuntimeError('Timed out waiting for ' + what)


def rect(key, seconds=30):
    return wait_for(lambda: (items().get(key) or {}).get('Rect'), seconds, key)


def click_at(px, py, button=1):
    x('mousemove', int(px), int(py))
    time.sleep(0.15)
    x('click', button)
    time.sleep(0.6)


def click(key, seconds=30):
    r = rect(key, seconds)
    click_at((r[0] + r[2]) / 2, (r[1] + r[3]) / 2)


def key(k):
    parts = k.split('+')
    if len(parts) > 1:
        for modifier in parts[:-1]:
            x('keydown', modifier)
        time.sleep(0.12)
        x('keydown', parts[-1])
        time.sleep(0.12)
        x('keyup', parts[-1])
        time.sleep(0.12)
        for modifier in reversed(parts[:-1]):
            x('keyup', modifier)
    else:
        x('key', '--clearmodifiers', k)
    time.sleep(0.5)


def type_text(s):
    x('type', '--clearmodifiers', '--delay', '120', s)
    time.sleep(0.7)


def grab(name):
    subprocess.run(['import', '-window', 'root', str(out / name)], check=True)


def readlog():
    return log_path.read_text(errors='replace')


def text_point(line, column):
    """The screen point of a character (0-based line and byte column) of the active tab, from the recorded metrics."""
    v = values()
    return (v['CodeEditor/OriginX'] + (column + 0.5) * v['CodeEditor/CharWidth'],
            v['CodeEditor/OriginY'] + (line + 0.5) * v['CodeEditor/LineHeight'])


def focus_text():
    r = rect('CodeEditor/Text')
    click_at(r[2] - 30, r[3] - 20)  # empty space below the code: focuses the text, the cursor goes to the end


def open_source(name):
    click('CodeEditor/Source/Source/' + name)
    wait_for(lambda: values().get('CodeEditor/ActiveFile', '').endswith('/' + name), 20, 'tab ' + name)
    time.sleep(0.4)


def active(name):
    return values().get('CodeEditor/ActiveFile', '').endswith('/' + name)


def basic_phase():
    focus_text()
    key('ctrl+End')
    key('Return')
    type_text('// code_editor_qa_saved')
    key('ctrl+s')
    wait_for(lambda: '// code_editor_qa_saved' in source.read_text())
    saved = source.read_text()
    print('UI typing + Ctrl+S passed', flush=True)
    key('ctrl+z')
    key('ctrl+s')
    wait_for(lambda: source.read_text() != saved, 4)
    print('UI undo + save passed', flush=True)
    key('ctrl+y')
    key('ctrl+s')
    wait_for(lambda: source.read_text() == saved)
    print('UI redo + save passed', flush=True)
    key('ctrl+End')
    type_text(' SetVel')
    key('ctrl+space')
    # In a comment clangd has nothing: the catalogue completes (SetVelocity).
    wait_for(lambda: values().get('CodeEditor/CompletionSource') == 'catalogue' and 'CodeEditor/Completion/SetVelocity' in items(), 15, 'catalogue completion')
    key('Return')
    key('ctrl+s')
    wait_for(lambda: 'code_editor_qa_saved SetVelocity' in source.read_text())
    print('UI Ctrl+Space API completion passed', flush=True)
    key('ctrl+End')
    key('Return')
    type_text('#error CODE_EDITOR_QA')
    offset = len(readlog())
    key('F7')
    wait_for(lambda: 'Script compile failed:' in readlog()[offset:], 120)
    assert '#error CODE_EDITOR_QA' in source.read_text()
    assert json.loads(config.read_text())['Project']['ScriptModulePath'] == 'Binaries/libHotReload.so'
    print('UI F7 auto-save + compiler diagnostics passed', flush=True)
    click('CodeEditor/Title')
    click('CodeEditor/FileDiagnostic/0')
    type_text('X')
    key('ctrl+s')
    wait_for(lambda: '#Xerror CODE_EDITOR_QA' in source.read_text())
    print('UI diagnostic line/column navigation passed', flush=True)
    key('ctrl+z')
    key('ctrl+s')
    wait_for(lambda: '#error CODE_EDITOR_QA' in source.read_text())
    grab('code-editor-diagnostic.png')
    focus_text()
    key('ctrl+End')
    key('Home')
    key('shift+End')
    key('BackSpace')
    offset = len(readlog())
    key('F7')
    wait_for(lambda: 'Scripts compiled successfully' in readlog()[offset:], 120)
    assert '#error CODE_EDITOR_QA' not in source.read_text()
    print('UI fix + build + reload passed', flush=True)
    click('CodeEditor/Title')
    grab('code-editor-tested.png')


def semantic_phase():
    wait_for(lambda: values().get('CodeEditor/Clangd') == 'Ready', 90, 'clangd ready')
    print('clangd ready: ' + values().get('CodeEditor/ClangdStatus', ''), flush=True)
    click('CodeEditor/Title')
    open_source('Arena.cpp')
    wait_for(lambda: values().get('CodeEditor/DiagnosticsMs', -1) >= 0 and values().get('CodeEditor/Squiggles', 1) == 0, 60, 'first diagnostics')

    # Semantic completion of a project class member: "Hero().Sta" + Ctrl+Space -> Stamina.
    focus_text()
    key('ctrl+End')
    type_text('int BonusValue = Hero().Sta')
    key('ctrl+space')
    wait_for(lambda: values().get('CodeEditor/CompletionSource') == 'clangd' and 'CodeEditor/Completion/Stamina' in items(), 20, 'clangd completion')
    assert 'CodeEditor/Completion/Health' not in items(), 'the typed prefix filters the members'
    time.sleep(0.3)
    grab('clangd-completion.png')
    print(f"completion latency {values().get('CodeEditor/CompletionMs', -1):.0f} ms", flush=True)
    key('Return')
    type_text(';')
    key('ctrl+s')
    wait_for(lambda: 'int BonusValue = Hero().Stamina;' in arena.read_text(), 10, 'completed text saved')
    print('UI clangd semantic member completion passed', flush=True)

    # A live diagnostic while typing: a member that does not exist is underlined, then fixed.
    key('Return')
    type_text('int Broken = Hero().Nope;')
    wait_for(lambda: values().get('CodeEditor/Squiggles', 0) >= 1 and 'Nope' in values().get('CodeEditor/SquiggleMessage/0', ''), 30, 'live diagnostic')
    assert 'Broken' not in arena.read_text(), 'diagnostics come from the unsaved text'
    print(f"diagnostics latency {values().get('CodeEditor/DiagnosticsMs', -1):.0f} ms: " + values()['CodeEditor/SquiggleMessage/0'], flush=True)
    r = rect('CodeEditor/Squiggle/0')
    x('mousemove', int((r[0] + r[2]) / 2), int((r[1] + r[3]) / 2))
    time.sleep(1.0)
    grab('clangd-live-diagnostic.png')
    assert 'CodeEditor/LiveDiagnostic/0' in items()
    key('Home')
    key('shift+End')
    key('BackSpace')
    key('BackSpace')
    wait_for(lambda: values().get('CodeEditor/Squiggles', 1) == 0, 30, 'diagnostic gone')
    key('ctrl+s')
    print('UI live diagnostic appears and disappears while typing passed', flush=True)

    # Ctrl+click on Heal (Arena.cpp line 5) -> its definition in Hero.cpp.
    x('mousemove', *map(int, text_point(4, 10)))
    time.sleep(0.3)
    x('keydown', 'ctrl')
    time.sleep(0.15)
    x('click', 1)
    time.sleep(0.15)
    x('keyup', 'ctrl')
    wait_for(lambda: active('Hero.cpp') and values().get('CodeEditor/CursorLine') == 1, 20, 'Ctrl+click definition')
    print('UI Ctrl+click go-to-definition into another file passed', flush=True)
    # F12 on Health (Hero.cpp line 2) -> the declaration in Hero.h.
    line = hero_cpp.read_text().splitlines()[1]
    click_at(*text_point(1, line.index('Health') + 2))
    key('F12')
    wait_for(lambda: active('Hero.h') and values().get('CodeEditor/CursorLine') == 3, 20, 'F12 definition')
    time.sleep(0.5)
    grab('clangd-go-to-definition.png')
    print('UI F12 go-to-definition into a header passed', flush=True)

    # F2 rename Health -> Vitality across Hero.h, Hero.cpp and Arena.cpp: preview, apply, Ctrl+Z.
    before = {p: p.read_text() for p in (hero_h, hero_cpp, arena)}
    key('F2')
    click('CodeEditor/RenameName', 10)
    key('ctrl+a')
    type_text('Vitality')
    key('Return')
    wait_for(lambda: values().get('CodeEditor/RenameFiles', 0) >= 3 or values().get('CodeEditor/RenameError'), 40, 'rename preview')
    assert values().get('CodeEditor/RenameFiles') == 3, values().get('CodeEditor/RenameError')
    for name in ('Hero.h', 'Hero.cpp', 'Arena.cpp'):
        assert 'CodeEditor/RenameFile/' + name in items(), name
    assert all('Vitality' not in t for t in (p.read_text() for p in before)), 'the preview writes nothing'
    grab('clangd-rename-preview.png')
    click('CodeEditor/RenameApply')
    wait_for(lambda: all('Vitality' in p.read_text() and 'Health' not in p.read_text() for p in before), 20, 'rename applied')
    print('UI F2 rename across three files (atomic save) passed', flush=True)
    wait_for(lambda: 'CodeEditor/UndoRename' in items(), 10, 'undo rename offered')
    focus_text()
    key('ctrl+z')
    wait_for(lambda: all(p.read_text() == text for p, text in before.items()), 20, 'rename undone on disk')
    print('UI Ctrl+Z undoes the rename in every file passed', flush=True)

    # Ctrl+click on an engine class (ScriptableEntity, HotReloadProbe.cpp) opens the SDK header read-only.
    click('CodeEditor/Title')
    open_source('HotReloadProbe.cpp')
    focus_text()
    key('ctrl+Home')  # the class line in view (the earlier phase left the end of the file shown)
    lines = source.read_text().splitlines()
    row = next(n for n, l in enumerate(lines) if l.startswith('class HotReloadProbe'))
    column = lines[row].index('ScriptableEntity') + 3

    def ctrl_click_engine():
        if not active('HotReloadProbe.cpp'):
            return True
        x('mousemove', *map(int, text_point(row, column)))
        time.sleep(0.2)
        x('keydown', 'ctrl')
        time.sleep(0.15)
        x('click', 1)
        time.sleep(0.15)
        x('keyup', 'ctrl')
        time.sleep(1.5)
        return not active('HotReloadProbe.cpp')
    wait_for(ctrl_click_engine, 60, 'engine header definition')
    wait_for(lambda: active('ScriptableEntity.h') and values().get('CodeEditor/ReadOnly') == 1, 10, 'read-only SDK header')
    print('UI Ctrl+click into a read-only SDK header passed', flush=True)


def no_clangd_phase():
    home = work / 'home-noclangd'
    (home / '.config/zamin').mkdir(parents=True, exist_ok=True)
    (home / '.config/zamin/preferences.json').write_text(json.dumps({'ZaminEditorPreferences': 1, 'CodeEditor': {'ClangdPath': str(work / 'missing/clangd'), 'UseClangd': True}}))
    start(home)
    wait_for(lambda: values().get('CodeEditor/Clangd') == 'NotFound', 60, 'no-clangd notice')
    assert 'clangd not found' in values().get('CodeEditor/ClangdStatus', '')
    print('no-clangd notice: ' + values()['CodeEditor/ClangdStatus'], flush=True)
    click('CodeEditor/Title')
    focus_text()
    key('ctrl+End')
    key('Return')
    type_text('// SetVel')
    key('ctrl+space')
    wait_for(lambda: values().get('CodeEditor/CompletionSource') == 'catalogue' and 'CodeEditor/Completion/SetVelocity' in items(), 15, 'catalogue completion')
    time.sleep(0.3)
    grab('clangd-not-found.png')
    key('Escape')
    print('UI without clangd: notice + catalogue completion passed', flush=True)
    stop()


def recovery_phase():
    focus_text()
    key('ctrl+End')
    key('Return')
    type_text('// UNSAVED_RECOVERY_QA')
    time.sleep(1.5)
    wait_for(lambda: any(('UNSAVED_RECOVERY_QA' in json.loads(p.read_text()).get('Text', '') for p in (project / '.zamin').rglob('code-recovery/*.json'))), 20)
    assert 'UNSAVED_RECOVERY_QA' not in source.read_text()
    print('UI debounced recovery passed', flush=True)


try:
    start(work / 'home')
    wait_for(lambda: 'CodeEditor/Text' in items(), 60, 'the code editor')
    basic_phase()
    if clangd:
        semantic_phase()
    else:
        print('SKIP: clangd is not installed - the semantic phases did not run', flush=True)
    click('CodeEditor/Title')
    open_source('HotReloadProbe.cpp')
    recovery_phase()
    stop()
    no_clangd_phase()
except BaseException:
    grab('code-editor-ui-failure.png')
    raise
finally:
    stop()
    shutil.copyfile(log_path, out / 'ui-test.log')
    temporary.cleanup()
