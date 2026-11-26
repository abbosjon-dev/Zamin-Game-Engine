# Generators of the sample games (`Tools/samples`)

The six sample games of M24 (`Projects/TopDownShooter`, `Breakout`, `FPSArena`, `Sokoban`, `EndlessRunner`,
`Racing`) contain no hand-drawn picture and no recorded sound. The textures, sprite atlases, normal maps,
`.obj` meshes, WAV sounds, the scene (`.zscene`), the `.zmeta` files, the `.zproject` and the module's `CMakeLists.txt`
are made by the Python scripts in this folder. The repository keeps the recipe (the script) and the small files it
produces. The C++ scripts (`Projects/<Game>/Source`) are written by hand and the generators do not touch them.

| File | What it does |
|---|---|
| `samplekit.py` | Shared tools: writing PNG and WAV (the `zlib` and `wave` modules), `.zmeta` with stable UUIDs, signed-distance shapes (soft edges, glow), tileable value noise, a normal map from a height map; for sound, oscillators, ADSR, noise, filters, notes; `SceneBuilder` (the `.zscene` JSON the engine reads) and component helpers (`ui_backdrop` — the dark plate under menu text); `write_project` |
| `gen_topdownshooter.py` | Neon Arena: a neon sprite atlas, the floor and the space backdrop, 15 sounds, the scene |
| `gen_breakout.py` | Zamin Breakout: the brick and power-up atlas, the field and the backdrop, 15 sounds, the scene |
| `gen_fpsarena.py` | FPS Arena: concrete, metal and crate textures with their normal maps, 22 sounds, the arena scene |
| `gen_sokoban.py` | Warehouse Keeper: brick, crate and floor textures, 10 sounds, the scene; solves every level of `Levels.cpp` |
| `gen_endlessrunner.py` | Silk Road: road, brick, sand, tile and carpet textures, 11 sounds, the scene |
| `gen_terrain_materials.py` | 12 seamless albedo/normal/ORM test maps for MapWorkshop; the Python standard library, the scene's UUID references |
| `gen_racing.py` | Valley Race: a Catmull-Rom track and the `road.obj`, `kerbs.obj`, `barriers.obj` built from it, textures, 10 sounds, the scene |
| `gen_combat_workshop.py` | CombatWorkshop: the compiler-free 2D/3D combat course — scenes, Blueprint graphs, visual projectile prefabs, the manifest and the QA oracle (byte-for-byte deterministic; from the LoadoutWorkshop scene) |
| `gen_lantern_keeper.py` | LanternKeeper (Lantern Keeper): the compiler-free 2D vertical slice — the menu, the Orchards and Crypt levels, the hero/enemies/boss, quests, equipment, the HUD, music; `lantern_graphs.py` (scenes and graphs), `slicekit.py` (characters, tiles, sound, the A* route), `graphkit.py` (the graph builder); deterministic |
| `gen_beacon_pass.py` | BeaconPass (Beacon Pass): the compiler-free 3D vertical slice — the menu and the Pass level (terrain, caravanserai, tower, pines), humanoid models and Animator locomotion, guards/archers/boss, quests, equipment, saving at shrines, music; `beacon_graphs.py` (scenes and graphs), `gltfkit.py` (skinned glTF humanoids, clips, props), `slicekit.py`, `graphkit.py`; deterministic |
| `make_thumbnails.py` | The launcher pictures (`Projects/*/Thumbnail.jpg`): from headless screenshots of the games |

## Regenerating

```bash
python3 Tools/samples/gen_racing.py            # one game (needs numpy)
for g in topdownshooter breakout fpsarena sokoban endlessrunner racing; do python3 Tools/samples/gen_$g.py; done
ZAMIN_SAMPLES_OUT=/tmp/out python3 Tools/samples/gen_breakout.py   # into /tmp/out/Breakout, not the repository
python3 Tools/samples/make_thumbnails.py Racing                     # the launcher picture (needs a build, xvfb-run, Pillow)
```

The generators are deterministic: the random seeds are fixed and the UUIDs are derived from the asset paths. So the
same script writes byte-for-byte the same files every time. The scenes are in the engine's JSON format, only lists of
numbers are written on one line (`dumps_compact`). That makes a file ~20% smaller, and the engine reads it like plain
JSON.

## How the sounds are made

Every sound is built from a few layers in a numpy array:

- an oscillator (sine, square, saw, triangle) whose pitch changes over time (a shot, for example, falls from 1500 Hz
  to 420 Hz);
- noise passed through a low-pass or high-pass filter (in an explosion the filter frequency drops from 5 kHz to
  180 Hz);
- an envelope: a fast attack and an exponential decay;
- music is assembled from notes: drum, hat, bass and melody are placed by bars and the end is mixed into the start,
  so no seam is heard when it loops.

`write_wav` writes the sound at 22 050 Hz, mono, 16-bit, normalizes its peak to 0.89 and puts a 3 ms fade at the start
and the end (no click is heard). Effect sounds last 0.05–1.5 s, music (the ambience in FPS Arena) 15–17 s.

## Budget and checks

`Tests/samples_assets.py` (ctest: `SampleAssets`) checks the following for every game:

- `Assets/` is no larger than 2 MiB (1.2–1.9 MiB now);
- a texture is no larger than 512 KB and 2048 px, a sound than 800 KB and 20 s, a scene than 700 KB;
- every asset has a `.zmeta`;
- the WAV files are neither silent nor clipped;
- every file the scenes and scripts name exists, and every sound and texture is used somewhere.

The `--regen` variant runs the generators into a temporary folder and compares the result with the files in the
repository. When you change a generator, run it again and commit the new files as well.
