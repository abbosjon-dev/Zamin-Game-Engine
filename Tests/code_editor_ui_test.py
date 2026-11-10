#!/usr/bin/env python3
"""Real C++ editor UI regression. Run under xvfb-run (1600x1000x24).
Requires xdotool, ImageMagick, CMake and a C++ compiler. Uses an isolated project/home.
ZAMIN_BUILD_DIR selects the engine build; output goes to <build>/CodeEditor.
"""
import os, subprocess, time, json, shutil, tempfile, signal
from pathlib import Path
repo = Path(__file__).resolve().parent.parent
build = Path(os.environ.get('ZAMIN_BUILD_DIR', str(repo / 'build'))).resolve()
temporary = tempfile.TemporaryDirectory(prefix='zamin-code-editor-ui-')
work = Path(temporary.name)
project = work / 'Project'
shutil.copytree(repo / 'Tests/Fixtures/HotReload', project, ignore=shutil.ignore_patterns('Build', '.zamin', 'Binaries', '*.zmeta'))
(work / 'home').mkdir()
out = build / 'CodeEditor'
out.mkdir(parents=True, exist_ok=True)
source = project / 'Source/HotReloadProbe.cpp'
config = project / 'HotReload.zproject'
settings = json.loads(config.read_text())
settings['Project']['ScriptModulePath'] = ''
config.write_text(json.dumps(settings, indent=2))
source.write_text('#include <Zamin/Scripting/Visual/VisualGraph.h>\n' + source.read_text())
env = dict(os.environ, HOME=str(work / 'home'), ZAMIN_EDITOR_CODE='Source/HotReloadProbe.cpp', ZAMIN_MAX_FRAMES='10000')
log = (work / 'editor.log').open('w')
app = subprocess.Popen([str(build / 'bin/ZaminEditor'), str(project)], env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)

def x(*args):
    subprocess.run(['xdotool', *map(str, args)], check=True, stdout=subprocess.DEVNULL)

def click(px, py):
    x('mousemove', px, py)
    x('click', 1)
    time.sleep(0.6)

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
    x('type', '--clearmodifiers', '--delay', '180', s)
    time.sleep(0.7)

def wait_for(fn, seconds=40):
    end = time.time() + seconds
    while time.time() < end:
        if app.poll() is not None:
            raise RuntimeError('Editor exited early')
        if fn():
            return
        time.sleep(0.2)
    raise RuntimeError('Timed out waiting for UI action')

def grab(name):
    subprocess.run(['import', '-window', 'root', str(out / name)], check=True)

def readlog():
    return (work / 'editor.log').read_text()
try:
    wait_for(lambda: '#282C34' in subprocess.check_output(['import', '-window', 'root', '-crop', '1x1+800+700', '-depth', '8', 'txt:-'], text=True).upper(), 45)
    click(700, 500)
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
    time.sleep(0.7)
    key('Return')
    key('ctrl+s')
    wait_for(lambda: 'code_editor_qa_saved SetVelocity' in source.read_text())
    print('UI Ctrl+Space API completion passed', flush=True)
    key('ctrl+End')
    key('Return')
    type_text('#error CODE_EDITOR_QA')
    offset = len(readlog())
    key('F7')
    wait_for(lambda: 'Skriptlarni kompilyatsiya qilishda xato:' in readlog()[offset:], 70)
    assert '#error CODE_EDITOR_QA' in source.read_text()
    assert json.loads(config.read_text())['Project']['ScriptModulePath'] == 'Binaries/libHotReload.so'
    print('UI F7 auto-save + compiler diagnostics passed', flush=True)
    click(180, 73)
    time.sleep(0.6)
    click(700, 755)
    time.sleep(0.6)
    type_text('X')
    key('ctrl+s')
    wait_for(lambda: '#Xerror CODE_EDITOR_QA' in source.read_text())
    print('UI diagnostic line/column navigation passed', flush=True)
    key('ctrl+z')
    key('ctrl+s')
    wait_for(lambda: '#error CODE_EDITOR_QA' in source.read_text())
    grab('code-editor-diagnostic.png')
    click(700, 500)
    key('ctrl+End')
    key('Home')
    key('shift+End')
    key('BackSpace')
    offset = len(readlog())
    key('F7')
    wait_for(lambda: 'Skriptlar muvaffaqiyatli kompilyatsiya qilindi' in readlog()[offset:], 70)
    assert '#error CODE_EDITOR_QA' not in source.read_text()
    print('UI fix + build + reload passed', flush=True)
    click(180, 73)
    grab('code-editor-tested.png')
    click(700, 500)
    key('ctrl+End')
    key('Return')
    type_text('// UNSAVED_RECOVERY_QA')
    time.sleep(1.5)
    wait_for(lambda: any(('UNSAVED_RECOVERY_QA' in json.loads(p.read_text()).get('Text', '') for p in (project / '.zamin').rglob('code-recovery/*.json'))), 20)
    snapshots = list((project / '.zamin').rglob('code-recovery/*.json'))
    assert snapshots
    assert any(('UNSAVED_RECOVERY_QA' in json.loads(p.read_text()).get('Text', '') for p in snapshots))
    assert 'UNSAVED_RECOVERY_QA' not in source.read_text()
    print('UI debounced recovery passed', flush=True)
except BaseException:
    grab('code-editor-ui-failure.png')
    raise
finally:
    os.killpg(app.pid, signal.SIGTERM) if app.poll() is None else None
    try:
        app.wait(timeout=15)
    except subprocess.TimeoutExpired:
        os.killpg(app.pid, signal.SIGKILL)
        app.wait()
    log.close()
    shutil.copyfile(work / 'editor.log', out / 'ui-test.log')
    temporary.cleanup()
