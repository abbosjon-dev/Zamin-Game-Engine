#!/usr/bin/env python3
"""Installer control-flow regression without running apt, sudo, or editing host mirror files."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='zamin-dependency-installer-') as temp:
    work = Path(temp)
    binaries = work / 'bin'
    binaries.mkdir()
    scripts = {
        'dpkg-query': '#!/usr/bin/env python3\nimport os\nprint("installed" if os.environ.get("TASK_INSTALLED")=="1" else "missing")\n',
        'timeout': '#!/usr/bin/env python3\nimport os,sys\nos.execvp(sys.argv[2],sys.argv[2:])\n',
        'getent': '''#!/usr/bin/env python3
import os,sys
host=sys.argv[-1];mode=os.environ['TASK_DNS']
raise SystemExit(0 if mode=='up' or (mode=='fallback' and host=='archive.ubuntu.com') else 2)
''',
        'sudo': '''#!/usr/bin/env python3
import os,sys,json
from pathlib import Path
with open(os.environ['TASK_CALLS'],'a') as f:f.write(json.dumps(sys.argv[1:])+'\\n')
if 'install' in sys.argv:
 state=Path(os.environ['TASK_INSTALL_STATE']);old=int(state.read_text()) if state.exists() else 0;state.write_text(str(old+1))
 mode=os.environ.get('TASK_INSTALL_FAILURE','')
 if mode=='always' or (mode=='once' and old==0):raise SystemExit(100)
''',
    }
    for name, body in scripts.items():
        path = binaries / name
        path.write_text(body)
        path.chmod(0o755)
    def run(name, dns, failure='', installed=False):
        calls = work / (name + '.calls')
        env = os.environ.copy()
        env.update(PATH=str(binaries) + ':' + env['PATH'], TASK_DNS=dns, TASK_CALLS=str(calls),
                   TASK_INSTALL_STATE=str(work / (name + '.state')), TASK_INSTALL_FAILURE=failure, TASK_INSTALLED='1' if installed else '0')
        result = subprocess.run(['bash', str(ROOT / 'Tests/install_linux_dependencies.sh'), 'ninja-build'],
                                env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
        rows = [json.loads(line) for line in calls.read_text().splitlines()] if calls.exists() else []
        apt = [row for row in rows if 'apt-get' in row]
        for row in apt:
            assert row[:2] == ['timeout', '--kill-after=10s'], row
            assert row[2] == ('240s' if 'install' in row else '120s'), row
        return result, apt
    result, calls = run('cached', 'down', installed=True)
    assert result.returncode == 0 and not calls and 'already installed' in result.stdout
    result, calls = run('dns_down', 'down')
    assert result.returncode == 69 and not calls and 'C++ build/tests have not run' in result.stdout
    result, calls = run('primary', 'up')
    assert result.returncode == 0 and len(calls) == 2
    result, calls = run('mirror_fallback', 'fallback')
    assert result.returncode == 0 and len(calls) == 2 and 'Using archive.ubuntu.com' in result.stdout
    result, calls = run('retry', 'up', 'once')
    assert result.returncode == 0 and len(calls) == 4
    result, calls = run('bounded_failure', 'up', 'always')
    assert result.returncode == 100 and len(calls) == 4
print('PASS installer: DNS fail-fast, official fallback, bounded retries and command wall-clock limits (mocked)')
