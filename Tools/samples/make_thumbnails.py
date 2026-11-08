#!/usr/bin/env python3
"""The sample projects' launcher pictures (Projects/<Name>/Thumbnail.jpg) from real screenshots of the games.

    python3 Tools/samples/make_thumbnails.py                   every sample
    python3 Tools/samples/make_thumbnails.py Racing Sokoban    only these

Each game runs headless in the built player (xvfb-run, ZAMIN_MAX_FRAMES / ZAMIN_SCREENSHOT; the new samples in their
demo mode - the bot plays), the last frame is scaled down to 480x270 and saved as a small JPG, and the project's
.zproject gets "Thumbnail": "Thumbnail.jpg" (and a "Description" for the older samples that have none). The editor's
launcher shows them on the sample cards.

Requirements: a build (build/bin/ZaminPlayer), xvfb-run, Pillow.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PLAYER = os.path.join(ROOT, "build", "bin", "ZaminPlayer")
SIZE = (480, 270)

# (project, environment, frames, description for a project file that has none)
SAMPLES = [
    ("Platformer2D", {}, 240, "2D platformer: tilemap, harakatlanuvchi platformalar, dushmanlar, tangalar, parallaks fon va 2D yorug'lik"),
    ("Showcase3D", {}, 150, "3D imkoniyatlar ko'rgazmasi: PBR materiallar, soyalar, post-processing, zarralar, fizika va animatsiya sahnalari"),
    ("Adventure3D", {}, 240, "3D sarguzasht: orol, uchinchi shaxs qahramoni, NPC'lar (patrul, ko'rish, quvish), suv va kun yorug'ligi"),
    ("TopDownShooter", {"ZAMIN_DEMO": "1"}, 450, ""),
    ("Breakout", {"ZAMIN_DEMO": "1", "ZAMIN_START_LEVEL": "3"}, 260, ""),
    ("FPSArena", {"ZAMIN_DEMO": "range"}, 170, ""),
    ("Sokoban", {"ZAMIN_DEMO": "1", "ZAMIN_START_LEVEL": "9"}, 230, ""),
    ("EndlessRunner", {"ZAMIN_DEMO": "1"}, 260, ""),
    ("Racing", {"ZAMIN_DEMO": "1"}, 300, ""),
]


def screenshot(project, env, frames, out_png):
    home = tempfile.mkdtemp(prefix="zamin-thumb-home-")
    run_env = dict(os.environ, HOME=home, ZAMIN_MAX_FRAMES=str(frames), ZAMIN_SCREENSHOT=out_png, **env)
    cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24", PLAYER, os.path.join(ROOT, "Projects", project), "--resolution", "1280x720",
           "--no-splash"]
    subprocess.run(cmd, env=run_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900, check=False)
    return os.path.isfile(out_png)


def update_project(project, description):
    path = os.path.join(ROOT, "Projects", project, f"{project}.zproject")
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    proj = data["Project"]
    if description and not proj.get("Description"):
        proj["Description"] = description
    proj["Thumbnail"] = "Thumbnail.jpg"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(data, indent=4, ensure_ascii=False) + "\n")


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
    failed = []
    for project, env, frames, description in SAMPLES:
        if args.projects and project not in args.projects:
            continue
        with tempfile.TemporaryDirectory() as tmp:
            png = os.path.join(tmp, "shot.png")
            if not screenshot(project, env, frames, png):
                print(f"  {project}: skrinshot olinmadi")
                failed.append(project)
                continue
            image = Image.open(png).convert("RGB").resize(SIZE, Image.LANCZOS)
            out = os.path.join(ROOT, "Projects", project, "Thumbnail.jpg")
            image.save(out, "JPEG", quality=84, optimize=True)
            update_project(project, description)
            print(f"  {project}: Thumbnail.jpg ({os.path.getsize(out) // 1024} KB)")
    if failed:
        sys.exit(f"xato: {', '.join(failed)}")


if __name__ == "__main__":
    main()
