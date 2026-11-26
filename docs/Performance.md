# Performance: the bottleneck report

The state at the end of M25 (tasks 481–500): where the time goes, what was fixed and what remains. Every number was
measured (profiler records, benchmarks); the before / after tables and how to reproduce them —
[Benchmarks.md](Benchmarks.md), the samples table — [Samples.md](Samples.md#performance).

**Environment:** 4 vCPU Intel Xeon @ 2.10 GHz, no GPU — Mesa 25.2.8 **llvmpipe** (LLVM 20.1.2, a software rasterizer),
xvfb, GCC 13.3, a **Release** build, a 1280x720 window. On llvmpipe the "GPU" work also runs on these four cores: the
pixel and vertex shaders, shadow maps, post-processing. That is why two kinds of time are shown separately below —
the engine's own CPU work (game code, the scene, submitting draw commands) and rasterization (on a real GPU it would
be done on the graphics card).

## 1. How it was measured

| Source | What it gives |
|---|---|
| Profiler (`ZM_PROFILE_SCOPE`, `Profiler::FormatTree`) | the frame's tree: the time of each part, its own time, the number of calls; the Profiler window in the editor |
| `ZAMIN_PLAYER_STATS=1` | when the player exits: the last frame's profile, render statistics, the scene, memory |
| `ZaminPlayer --benchmark` | frame times (mean, p95, p99) and **the profile of the slowest frame** |
| `ZAMIN_WORLD_TOUR=<.ztour>` + `ZAMIN_WORLD_REPORT=<.json>` | the streaming world: a camera path tied to the frame number and each frame's time, streaming/upload work, RSS, requested GPU bytes ([Terrain.md](Terrain.md#the-asset-budget-residency-limits-and-the-camera-path)) |
| `ZAMIN_STARTUP_PROFILE=1` | the startup tree (window, renderer, audio, project, scene) and the first frame's tree |
| `ZAMIN_EDITOR_PANEL_TIMES=1`, `ZAMIN_EDITOR_STATS=1` | the time of the editor panels, the editor's memory |
| `Tests/samples_bench.py` | nine samples while a bot plays them (collects the above) |
| `Tests/startup_bench.py` | startup time, comparing two builds |
| `ZaminBench` (`--render`, `--ecs`, `--serialize`, `--fonts`, `--animators`, `--nav`) | isolated cases: 10 000 sprites, 1 000 meshes, ECS, serialization, fonts ... |

## 2. The heaviest places (frame)

From the profiles of the last frames of the samples (1280x720, the mean of 300 frames; `Tests/samples_bench.py`, 2026-11-09):

| Sample | Frame | Post-processing (SSAO / Bloom) | Drawing the scene | Game code (CPU) |
|---|---:|---|---:|---:|
| Adventure3D | 152.9 ms | 125.7 ms (109.0 / 12.9) | 18.1 ms | 0.50 ms |
| EndlessRunner | 145.2 ms | 86.8 ms (62.5 / 11.8) | 38.2 ms | 0.11 ms |
| FPSArena | 131.6 ms | 99.2 ms (73.7 / 12.5) | 21.2 ms | 0.40 ms |
| Sokoban | 131.5 ms | 93.8 ms (69.4 / 12.2) | 12.9 ms | 0.08 ms |
| Racing | 109.4 ms | 73.3 ms (47.8 / 12.6) | 25.5 ms | 0.16 ms |
| Showcase3D | 108.4 ms | 80.3 ms (62.9 / 13.2) | 13.6 ms | 0.39 ms |
| TopDownShooter (2D) | 62.4 ms | 33.0 ms (- / 29.4) | 1.0 ms | 0.23 ms |
| Breakout (2D) | 48.9 ms | 27.7 ms (- / 24.0) | 1.4 ms | 0.18 ms |
| Platformer2D (2D) | 12.5 ms | 4.1 ms | 0.2 ms | 0.04 ms |

1. **Shading pixels (llvmpipe) — 80–95 % of the frame.** llvmpipe defers drawing and does it when the image is first
   read: the PBR lighting, shadows and transparent parts of a 3D scene show up inside "SSAO" (it reads the depth
   first), those of a 2D scene (20+ 2D lights per pixel) inside "Bloom".
2. **Drawing the scene takes 13–38 ms** — the engine's own share of it is small: the CPU time of the draw commands
   (instancing, batching, culling, uniform buffers) is measured in ZaminBench; the rest is llvmpipe's vertex work and
   the shadow maps (2048 px, cascades — independent of the window size). The most is Silk Road (EndlessRunner):
   155 284 triangles.
3. **Game code** (scripts, physics, particles, animation, audio, AI) is 0.04–0.55 ms in every sample.

**CPU time (task 497).** To remove nearly all rasterization the samples were measured in a 284x160 window (the 3D
samples with `--quality Low` too: no SSAO, small shadow maps): the remaining frame time is the engine's CPU work and
llvmpipe's vertex work together, i.e. an upper bound on the engine's work. 2D: 1.9–9.3 ms, 3D: 8.5–12.7 ms —
within 16.6 ms (60 FPS); EndlessRunner was 26.6 ms (the vertex work of 155K triangles) - with the primitive LODs it is
15.1-15.5 ms (67K triangles; p95 still 19-20 ms). The table:
[Samples.md](Samples.md#the-m25-check-2026-11-09).

**The slowest frames.** In TopDownShooter the first run has one frame of ~0.8 s: when a large text (the wave banner)
is first drawn, the font atlas of that size is rasterized (~600 glyphs). The font cache (485) writes it to disk: on
the second run the slowest frame is 866 -> 148 ms. `samples_bench` starts every game with a fresh HOME, so it shows
up every time.

## 3. The heaviest places (startup)

`ZAMIN_STARTUP_PROFILE=1`, Release, Showcase3D (the latest measurement):

| Part | Editor | Player |
|---|---:|---:|
| Window and GL context | 52–68 ms | 55–68 ms |
| `Renderer::Init` (of which `Renderer3D::Init`: 25 shader programs from the binary cache) | 97–122 ms (63) | 97–110 ms (63–70) |
| Audio | 9–10 ms | 9 ms |
| Project + start scene (assets: meshes / materials, fonts) | 64–90 ms | 50–60 ms |
| The first frame (of which SSAO — the driver compiles shader variants on the first draw) | 380–420 ms (84–107) | 220–276 ms (80–90) |

## 4. What was fixed in M25

| Task | Place | Before -> after |
|---|---|---|
| 481 | 2D batching (`BatchOrder`, fewer flushes) | `mixed2d`: 670 -> 65 draw calls, CPU 16.0 -> 10.1 ms |
| 482 | per-frame allocations | 10k sprites: 10 023 -> 2, 1k meshes: 1 504 -> 6 allocations / frame |
| 483, 484 | the world transform cache, the component pool cache | `GetWorldTransform` 221 -> 33 ns; pool read 6.6 -> 1.7 ns |
| 485 | font atlases on disk, a glyph table, kerning (optional) | atlas 45 ms of rasterizing -> 2.4 ms of reading |
| 486, 487 | loading textures and models in the background | Showcase3D: 103.8 ms in one frame -> the longest of 9 frames 6.9 ms |
| 488 | the shader program binary cache | shaders 253 -> 74–91 ms (Mesa cache off); 72 -> 64 ms (on) |
| 489 | a per-frame uniform buffer (std140) | ~150–200 `glUniform` -> 1 buffer upload (no noticeable speed-up on llvmpipe) |
| 490 | culling in the shadow pass | 1 000 meshes: shadow draws 1 024 -> 323, CPU 156.5 -> 126.0 ms |
| 491, 492 | parallel particles and animation | 49K particles: 12.2 -> 4.0 ms; animator view 0.196 -> 0.114 ms |
| 493, 494 | editor: `ImGuiListClipper` | 10 000 entities + 5 000 files: editor frame 42.7 -> 24.7 ms |
| 495 | startup | editor (Showcase3D) 1 094 -> 750 ms; the ImGui atlas 236 -> 60–70 ms; the Scenes panel 71 -> 25 ms |
| 496 | memory | editor RAM 451 -> 325 MB (Showcase3D), 367 -> 242 MB (Platformer2D) |

No optimization changed the picture (`ZaminRenderTests` 12/12), the physics determinism tests pass, and the parallel
paths are compared bit for bit with the single-threaded result.

## 5. Remaining limits

1. **Rasterization (no GPU).** On llvmpipe the 3D samples run at 7–10 FPS, the 2D ones at 16–80 FPS. The most
   expensive is the SSAO pass (the scene itself is shaded inside it). Solutions: a real GPU; computing SSAO at half
   size; the Low quality level (SSAO off) — the 3D samples run at 100+ FPS at 284x160.
2. **EndlessRunner geometry.** 155K triangles: even at Low quality and in a small window it was 22.7-26.6 ms. Fixed:
   small spheres / cylinders / cones on screen draw a copy with half or a quarter of the segments
   (`MeshLibrary::GetPrimitiveLOD`) - 67K triangles, 15.1-15.5 ms on average. What remains: p95 19-20 ms (the frames
   where pieces appear); culling distant pieces by distance is the next step.
3. **A font atlas of a new size.** On the first run, the first draw of a large text takes ~0.8 s (later runs use the
   cache on disk). Solution: preparing the sized atlases on a worker thread, or limiting the glyph set to the text.
4. **Startup (player ~0.35–0.55 s).** The GL context, the shader programs (llvmpipe regenerates the code even from the
   binary cache) and the driver compilation in the first frame — outside the engine. Real GPU drivers have their own
   shader cache. The ImGui atlas is still rasterized every time (~60 ms) — it could be cached on disk.
5. **Temporary memory.** In the players there is a temporary growth of ~80 MB during the first frame (it comes back
   on the second frame); its source was not found.
6. **ECS.** A case that creates / destroys 500 entities every frame still makes 1 518 allocations (`ZaminBench --ecs`).

## 6. Measuring it yourself

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DZAMIN_GAME_MODULE_OUTPUT_DIR=$PWD/build-release/GameModules
cmake --build build-release
ZAMIN_BUILD_DIR=build-release python3 Tests/samples_bench.py               # the samples (frame, profile, game code)
ZAMIN_BUILD_DIR=build-release python3 Tests/samples_bench.py --resolution 284x160   # without rasterization (CPU)
ZAMIN_BUILD_DIR=build-release python3 Tests/startup_bench.py               # startup time
ZAMIN_STARTUP_PROFILE=1 ZAMIN_MAX_FRAMES=2 build-release/bin/ZaminPlayer Projects/Showcase3D   # the startup profile
```
