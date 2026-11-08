#!/usr/bin/env python3
"""The pictures of docs/Samples.md (docs/images/sample-<game>.jpg) from real headless screenshots of the games.

    python3 Tools/samples/make_gallery.py                  every M24 sample
    python3 Tools/samples/make_gallery.py Racing Sokoban   only these

Each game runs headless in the built player (xvfb-run, ZAMIN_MAX_FRAMES / ZAMIN_SCREENSHOT) with its bot (ZAMIN_DEMO)
at a fixed game clock (ZAMIN_CAPTURE_FPS: the same moment on any computer); the last frame (1280x720) is scaled to
960x540 and saved as a JPG. The older samples' pictures are docs/images/game-*.jpg (docs/README.md).

Requirements: a build (build/bin/ZaminPlayer), xvfb-run, Pillow.
"""

import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLAYER = os.path.join(ROOT, "build", "bin", "ZaminPlayer")
OUT = os.path.join(ROOT, "docs", "images")
SIZE = (960, 540)

# (picture name, project, environment, frames at 30 game frames a second)
SHOTS = [
    ("topdownshooter", "TopDownShooter", {"ZAMIN_DEMO": "1"}, 760),
    ("breakout", "Breakout", {"ZAMIN_DEMO": "1", "ZAMIN_START_LEVEL": "5"}, 330),
    ("fpsarena", "FPSArena", {"ZAMIN_DEMO": "range"}, 330),
    ("sokoban", "Sokoban", {"ZAMIN_DEMO": "1", "ZAMIN_START_LEVEL": "7"}, 300),
    ("endlessrunner", "EndlessRunner", {"ZAMIN_DEMO": "1"}, 520),
    ("racing", "Racing", {"ZAMIN_DEMO": "1"}, 520),
]


def screenshot(project, env, frames, out_png):
    home = tempfile.mkdtemp(prefix="zamin-gallery-home-")
    run_env = dict(os.environ, HOME=home, ZAMIN_MAX_FRAMES=str(frames), ZAMIN_SCREENSHOT=out_png, ZAMIN_CAPTURE_FPS="30", **env)
    cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24", PLAYER, os.path.join(ROOT, "Projects", project), "--resolution", "1280x720",
           "--no-splash"]
    subprocess.run(cmd, env=run_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900, check=False)
    return os.path.isfile(out_png)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("projects", nargs="*", help="only these samples")
    args = parser.parse_args()
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow kerak: pip install pillow")
    if not os.path.isfile(PLAYER):
        sys.exit(f"Player topilmadi: {PLAYER} (avval build qiling)")
    os.makedirs(OUT, exist_ok=True)
    failed = []
    for name, project, env, frames in SHOTS:
        if args.projects and project not in args.projects:
            continue
        with tempfile.TemporaryDirectory() as tmp:
            png = os.path.join(tmp, "shot.png")
            if not screenshot(project, env, frames, png):
                print(f"  {project}: skrinshot olinmadi")
                failed.append(project)
                continue
            out = os.path.join(OUT, f"sample-{name}.jpg")
            Image.open(png).convert("RGB").resize(SIZE, Image.LANCZOS).save(out, "JPEG", quality=82, optimize=True, progressive=True)
            print(f"  {project}: docs/images/sample-{name}.jpg ({os.path.getsize(out) // 1024} KB)")
    if failed:
        sys.exit(f"xato: {', '.join(failed)}")


if __name__ == "__main__":
    main()
