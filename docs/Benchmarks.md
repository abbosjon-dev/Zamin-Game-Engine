# Benchmarks and their baseline results

The programs that measure the engine's speed and the measured **baseline**: compare the next change against these
numbers to see how much it changed the speed. Numbers are comparable when taken on the same computer, with the same
build type and the same settings.

## Rendering: 10 000 sprites, 1 000 meshes (`ZaminBench --render`)

`Tests/RenderBench.cpp`, run with `Tests/render_bench.sh` (or `cmake --build build --target render_bench`).
It needs a GL context: without a display a fixed 1600x900x24 xvfb screen, and always Mesa's software rasterizer
(llvmpipe) - so the numbers compare across computers and with this table (with the GPU: `ZAMIN_RENDER_BENCH_GPU=1`).
The scenes are drawn as in the game (player): the main camera, the scene frame (`OnUpdateRuntime`) into a linear HDR
target, then post-processing to the window. Every case warms up for 20 frames, then 120 frames are measured:

| Case | What is drawn |
|---|---|
| `sprites` | 10 000 sprites (a quarter with one of 4 textures), an orthographic camera |
| `meshes` | 1 000 primitives (cube, sphere, cylinder, cone; 8 materials) on the ground, a shadow-casting sun and a point light, a perspective camera; with GPU instancing (as usual) |
| `meshes-noinst` | the same meshes without instancing (shows what instancing saves) |
| `combined` | the sprites (on a wall at the back) and the meshes in one scene |

**CPU** - from the start of the frame until every command is submitted; **Frame (with the GPU)** - until `glFinish`
(the GPU has finished too). `--json` writes every case's numbers (`render-bench.json`), `--max-avg <ms>` fails the
script on a case slower than the limit, `--shots <folder>` saves every case's last frame as a PNG.

### Baseline (2026-11-08)

Environment: 4 vCPU Intel Xeon @ 2.10 GHz, Mesa 25.2.8 llvmpipe (LLVM 20.1.2, 256 bit), xvfb 1600x900x24, a 1280x720
window, GCC 13.3, a **Release** build (`-DCMAKE_BUILD_TYPE=Release`), 120 frames.

| Case | Objects | CPU ms (average / median / p95) | Frame ms, with the GPU (average / median / p95 / max) | Drawn |
|---|---:|---|---|---|
| sprites | 10 001 | 18.72 / 18.21 / 21.51 | 24.51 / 23.92 / 27.68 / 31.40 | 3 draw calls, 10 000 quads |
| meshes | 1 004 | 130.30 / 128.23 / 152.16 | 136.54 / 134.36 / 158.17 / 170.86 | 46 draw calls, 1 001 meshes (1 000 instances), 649K triangles |
| meshes-noinst | 1 004 | 135.24 / 130.99 / 156.46 | 141.68 / 137.29 / 163.78 / 187.51 | 2 027 draw calls, 1 001 meshes, 649K triangles |
| combined | 11 004 | 138.78 / 135.09 / 169.44 | 145.10 / 141.27 / 176.07 / 183.91 | 47 draw calls, 8 500 quads, 1 001 meshes (1 000 instances) |

In a Debug build (the same computer, 20 frames) roughly: sprites 75 ms, meshes 150 ms, combined 209 ms - Debug is ~3
times slower with sprites (the CPU-side batching), with meshes the difference is small (rasterization takes the time).

How to read it:

- **10 000 sprites are drawn in 3 draw calls** (Renderer2D batching: many quads per batch, textures in slots) - ~25 ms
  per frame in Release with the software rasterizer; on a real GPU this is a few ms.
- **Instancing** cuts the draw calls from 2 027 to 46 (together with the shadow cascades). On llvmpipe the time is
  almost the same (there the CPU rasterizes the triangles - 649K triangles, the shadow cascades' draws included); on a
  real GPU the draw call count decides more of the CPU time.
- In the `combined` case 1 500 sprites are outside the camera view (frustum culling): 8 500 quads were drawn.
- The result depends on the machine: on another computer first take this table there again, then compare the change.
  Small differences (±5-10%) are noise; p95 and max fluctuate especially under xvfb.

## Dense packed foliage (`ZaminBench --foliage`)

`Tests/FoliageBench.cpp`, run with `Tests/foliage_bench.sh` (smoke: `ctest -R BenchFoliageSmoke`, 262 144 instances,
5 frames). Like `--render` it needs a GL context and always uses Mesa llvmpipe under a fixed xvfb screen, so **every
number here is a software rasteriser's, not a physical GPU's**. The scene: 1 048 576 instances in 16 packed foliage
components of 65 536 (256 x 256 m tiles, one tree per square metre: a cone near LOD, a cube far LOD), LOD 40 m, view
distance 1000 m, a shadow-casting sun; a perspective camera (60°, far 1200 m) 9 m above the forest's edge moving 0.35 m
per frame into it, the same path for every case. The cases: `cpu` / `gpu` (CPU culling, the GL 4.1 fallback, against
the GL 4.3 compute pass with indirect draws) with HLOD at 160 m (20 m crossfade, 32 m cells), and `cpu-nohlod` /
`gpu-nohlod` without HLOD (every far instance up to 1000 m drawn). 10 warm-up frames, then the measured ones. **Cull
CPU** is the CPU time of all 16 culling passes (on the GPU path: issuing them - but llvmpipe runs a compute dispatch
on the CPU before `glDispatchCompute` returns, so there it includes the compute work); **cull GPU** is the compute
passes' time from timestamp queries. At the end the CPU and GPU cases' exact counts (a blocking readback) are compared:
a difference fails the run. `--json` writes `foliage-bench.json` in the `Results` format of `Tests/bench_compare.py`
(`--compare` uses `Tests/Baselines/foliage-release.json`), `--shots DIR` the last frames, `--lod-colors` the LOD/HLOD
debug colours.

### Baseline (2026-11-27)

Environment: 4 vCPU Intel Xeon @ 2.10 GHz, Mesa 25.2.8 llvmpipe (LLVM 20.1.2, 256 bit, GL 4.5), xvfb 1600x900x24, a
1280x720 window, GCC 13.3, a **Release** build, 60 frames (`Tests/Baselines/foliage-release.json`).

| Case | Frame ms (average / median / p95 / max) | CPU ms (average) | Cull CPU ms (average / p95) | Cull GPU ms | Draw calls | Triangles | Drawn near / far | Shadow set | HLOD |
|---|---|---:|---|---:|---:|---:|---|---:|---|
| `cpu` | 543.5 / 534.0 / 592.4 / 661.5 | 536.6 | 25.1 / 31.0 | - | 41 | 454K | 924 / 24 729 | 13 433 | 717 cells for 987 136 instances |
| `gpu` | 476.8 / 470.4 / 522.1 / 563.4 | 470.1 | 21.6 / 25.3 | 16.9 | 67 | 454K | 923 / 24 809 | 13 402 | 717 cells for 987 136 instances |
| `cpu-nohlod` | 2 911.3 / 2 902.9 / 3 200.0 / 3 397.2 | 2 903.9 | 80.4 / 94.5 | - | 35 | 8.71M | 924 / 716 230 | 13 433 | - |
| `gpu-nohlod` | 2 870.1 / 2 852.2 / 3 191.1 / 3 467.0 | 2 862.5 | 54.3 / 60.0 | 49.6 | 48 | 8.71M | 923 / 716 469 | 13 402 | - |

The last frame's exact counts were the same on both paths (with HLOD: near 924, far 24 729, shadow set 13 433; without:
near 924, far 716 230, shadow set 13 433). The GPU path's per-frame table numbers are its last completed asynchronous
readback (a frame behind), hence the small differences in the table. Memory (the estimate of the batches): the GPU path
387 MiB of buffers (instance input, history, two compacted sets of 108-byte instances per component, commands) and 349
MiB on the CPU; the CPU path 13 MiB of streamed buffers (sized to what is visible) and 354 MiB on the CPU.

How to read it:

- **HLOD** is what makes the dense scene affordable here: 987 136 of the 1 048 576 instances are replaced by 717 cell
  proxies, the drawn triangles fall from 8.71M to 454K and the frame from ~2.9 s to ~0.5 s - llvmpipe's time goes to
  rasterising, which is also why the CPU (submission) time tracks the frame time.
- **GPU culling** is faster than the CPU loop on the same CPU cores (16.9 against 25.1 ms with HLOD, 49.6 against 80.4
  ms without) and does not copy the visible instances through the CPU; on a physical GPU the compute pass would run
  asynchronously and the CPU would only issue it. Neither number is a physical GPU measurement.
- The GPU path takes **more draw calls** (67 against 41): it issues an indirect draw for every LOD part and shadow
  cascade without knowing on the CPU that a set is empty; the CPU path skips empty sets.
- The **shadow set** (instances inside a cascade's volume) is ~13 400 instances here instead of every near-LOD instance;
  the HLOD proxies cast no shadow.

## Terrain tile streaming (`ZaminBench --terrain-tiles`)

`Tests/TerrainTilesBench.cpp`, run with `Tests/terrain_tiles_bench.sh` (smoke: `ctest -R BenchTerrainTilesSmoke`, 6 x 6
tiles of 65 samples, 20 frames). A GL context and Mesa llvmpipe under a fixed xvfb screen, like `--foliage`: **the
frame times are a software rasteriser's, not a physical GPU's**; the streaming numbers (main-thread update, reads,
memory) are CPU work and do not depend on the rasteriser. The scene: a 16 x 16 tiled terrain (129 samples, 64 m per
tile, 1024 m across, 4 LOD levels, 400 foliage instances per tile; 46 MiB on disk, made into a temporary folder by the
world generator), a shadow-casting sun, a perspective camera 70 m up circling the map's centre at 38% of its size and
looking at it - tiles stream in ahead and out behind. The component: load radius 400 m, unload 460 m, full detail
within 80 m, at most LOD 3, 4 loads per frame, foliage within 160 m. The cases: `async` (the background reader, the
runtime default, 96 MB budget), `sync` (the same reads on the main thread), `tight` (async with a 12 MB budget). 5
warm-up frames, then the measured ones. The run fails when resident memory ever exceeded the budget or two resident
neighbours disagree on a shared sample. `--json` writes `terrain-tiles-bench.json` (`--compare` uses
`Tests/Baselines/terrain-tiles-release.json`), `--shots DIR` the last frames.

### Baseline (2026-11-27)

Environment: 4 vCPU Intel Xeon @ 2.10 GHz, Mesa 25.2.8 llvmpipe (LLVM 20.1.2, 256 bit, GL 4.5), xvfb 1600x900x24, a
1280x720 window, GCC 13.3, a **Release** build, 120 frames (`Tests/Baselines/terrain-tiles-release.json`).

| Case | Frame ms (average / p95 / max) | Streaming update ms (average / max) | Reads (total ms on the reader / main thread) | Loads / unloads (peak per frame) | Resident peak | LOD 0/1/2/3 (last frame) | Coarsened / dropped (plan steps) | Draws / triangles |
|---|---|---|---:|---|---|---|---|---|
| `async` | 193.9 / 250.9 / 262.9 | 0.86 / 2.20 | 58.7 (reader), 36.0 MiB | 572 / 204 (4) | 104 tiles, 37.7 MiB of 96 | 12 / 44 / 3 / 8 | 0 / 0 | 157 / 243K |
| `sync` | 198.6 / 242.3 / 279.0 | 1.31 / 2.58 | 68.3 (main thread), 36.4 MiB | 576 / 204 (4) | 104 tiles, 38.2 MiB of 96 | 12 / 44 / 3 / 8 | 0 / 0 | 169 / 258K |
| `tight` | 107.0 / 158.9 / 197.6 | 0.33 / 1.17 | 31.2 (reader), 18.7 MiB | 218 / 171 (4) | 9 tiles, 12.0 MiB of 12 | 7 / 0 / 0 / 1 | 11 558 / 11 817 | 32 / 35.6K |

How to read it:

- **The streaming itself is cheap**: planning, budget, integration and stitching take 0.3-1.3 ms of the main thread a
  frame on average; the frame time is llvmpipe rasterising the terrain and the shadow pass.
- **Async against sync**: with the background reader the main thread's streaming update is 0.86 ms against 1.31 ms
  (the reads, ~0.1 ms per tile here from the page cache, move off the main thread). The tiles here are small files on
  a warm disk; on a cold or slow disk the difference grows with the read time, which the sync case pays inside the
  frame. The two cases read slightly different numbers of tiles because a finished read is made resident a frame later
  on the async path.
- **The budget is a hard limit**: with 12 MB only 9 tiles fit (full-detail tiles near the camera are ~1.5 MB each with
  their foliage); the planner coarsens and drops the farthest tiles first, so the tiles at the camera keep full detail
  and the frame is cheaper because less is drawn. The coarsened / dropped counters count plan steps, summed over the
  frames. No case went over its budget or showed a crack between tiles (0 shared-sample difference at the end).
- The far edge beyond the load radius is empty (no impostor or horizon mesh beyond the streamed tiles in this version).

## Large Visual Script graphs (`ZaminBench --graphs`)

`Tests/GraphBench.cpp`, headless (no GL); `Tests/bench.sh --only graphs` saves and compares it
(`Tests/Baselines/graphs-release.json`), `ctest -R BenchGraphsSmoke` runs it at about 600 nodes. Four generated graphs
of about 4000 nodes each (a graph file holds at most 4096), saved as `.zgraph` and measured: loading the file
(`Graph::Load`: the JSON parse and `Graph::Parse`), `Validate`, the bytecode cook (`Bytecode::Compile`: validate,
resolve, expand, encode), loading the VM from the source file (what the editor's Play does) and from the cooked file
(the exported Player), running the `Start` event, and running it with the VM profiler on. The cases: `wide` (one event,
an execution chain of 1000 Set nodes, each fed by Get/Multiply/Add), `deep` (31 typed functions calling each other -
the VM's depth limit is 32 - run 100 times from a For loop), `loops` (nested 100 x 100 For loops adding to an
accumulator), `macros` (500 instances of an inline macro, expanded to 3994 program nodes). Filler expressions make each
graph up to its size. The cooked program must give the same variables as the source and the profiler must count the
run (exit code 3 otherwise). Times: the median of 9 repeats (`--repeat`); `--write DIR` saves the graphs (the editor's
large graph checks).

### Baseline (2026-11-27)

Environment: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, a **Release** build (`Tests/Baselines/graphs-release.json`).

| Case | Nodes (program) | Node runs | Source / cooked bytes | Load file | Validate | Cook | VM load: source / cooked | Run | Run, profiled |
|---|---|---:|---|---:|---:|---:|---|---:|---:|
| `wide` | 4001 | 4001 | 1 037 026 / 59 158 | 17.8 ms | 12.1 ms | 13.8 ms | 30.6 / 2.5 ms | 1.09 ms | 1.81 ms |
| `deep` | 3999 | 12 802 | 928 868 / 70 646 | 15.3 ms | 13.5 ms | 20.6 ms | 28.5 / 2.1 ms | 10.0 ms | 12.9 ms |
| `loops` | 4000 | 50 202 | 912 727 / 66 773 | 15.3 ms | 10.3 ms | 11.5 ms | 26.2 / 1.9 ms | 16.7 ms | 25.7 ms |
| `macros` | 2999 (3994) | 1501 | 716 875 / 73 540 | 12.5 ms | 35.3 ms | 83.2 ms | 76.0 / 2.9 ms | 0.46 ms | 0.77 ms |

How to read it:

- **The cooked file** is 10-17x smaller than the JSON source and loads the VM 12-26x faster: it is parsed with bounds
  checks only, without validating, resolving or expanding again (VisualWorkshop's eight graphs: 47 KB → 10 KB).
- **Execution**: about 0.25-0.8 µs per node run (a typed call 31 deep, `deep`, costs the most per node: the call
  frames and their arguments). The profiler costs 1.2-1.7x while it runs (two clock reads per node run).
- **Macros** are expanded at load and cook time (`Graph::Expand`, then validated as expanded): the most expensive load
  from source, and the case where the cooked file saves the most.

The large graph problems found and fixed with this benchmark (the same machine and build, 4000 nodes, `wide` / `deep` /
`loops`; the `macros` case was added afterwards):

| | Before | After | What changed |
|---|---|---|---|
| Load file | 108 / 81 / 84 ms | 18 / 15 / 15 ms | the JSON was parsed from a stream with a callback per value (the nesting limit); now read whole and the nesting checked on the text |
| Validate (`deep`) | 43.6 ms | 13.5 ms | the return-path check described every wire's target once per typed function; now one index per graph |
| VM load from source | 122 / 128 / 96 ms | 31 / 28 / 26 ms | (the two above) |
| Run | 2.82 / 13.4 / 23.1 ms | 1.09 / 10.0 / 16.7 ms | a check message was built as a `std::string` on every step; the data-cycle guard allocated a set node per evaluation; inputs, wires and pin descriptions were found in `std::map`s keyed by (node, name) - now per-node vectors and an index; a variable write no longer walks every variable to account the retained bytes |
| Run, profiled (`deep`) | 31.7 ms | 12.9 ms | the profiler copied the call stack's function names every step; now one time sum per call |

The editor: a 4000-node graph opened at the fit zoom drew every node and wire every frame — 4001 nodes / 4000 wires,
562 772 vertices and 149 ms of canvas CPU time a frame (a **Debug** build, Mesa llvmpipe, `ZAMIN_EDITOR_VISUAL_LAYOUT`
statistics); the canvas now culls what is off screen: 80 nodes / 99 wires, 17 932 vertices, 19 ms.

## ECS: creating, iterating, deleting (`ZaminBench --ecs`)

`Tests/EcsBench.cpp`, run with `ZaminBench --ecs [--entities N] [--frames N] [--json file]` or
`Tests/bench.sh --only ecs`. Headless (no window or GPU needed). N objects (10 000 by default) are created through the
scene's own API (the path the editor, the loader and scripts use), get components, are iterated, put into a hierarchy,
looked up, copied and deleted; for comparison a bare EnTT registry next to it (no names, UUID map, scene order or
hierarchy). At the end, game frames of a scene of that size (`UpdateRuntime`, without a camera, i.e. updating only):
empty, with N/10 scripts, and with 500 objects created and 500 deleted every frame. Every number is measured 3 times
(a new scene each time) and the median is taken; **ns / each** - per object (or component).

### Baseline (2026-11-08)

Environment: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, a **Release** build, 10 000 objects (`Tests/Baselines/ecs-release.json`).

| Operation | ms | ns / each |
|---|---:|---:|
| EnTT: `create` + `emplace<Transform>` (Sprite on half) | 0.74 | 74 |
| EnTT: `view<Transform>` writing to each | 0.008 | 0.8 |
| EnTT: `destroy` each one separately | 0.47 | 47 |
| `CreateEntity(name)` (ID, Transform, Tag, Relationship, the UUID map) | 2.29 | 229 |
| `CreateEntityWithUUID` (the loader's path) | 1.50 | 150 |
| `AddComponent<SpriteRenderer>` | 0.95 | 95 |
| `RemoveComponent<SpriteRenderer>` | 0.44 | 44 |
| `GetAllEntitiesWith<Transform>` / `<Transform, Sprite>` | 0.062 / 0.060 | 6.2 / 6.0 |
| `Entity::GetComponent<Transform>` in scene order / random | 0.066 / 0.132 | 6.6 / 13 |
| `GetEntityByUUID` random | 0.23 | 23 |
| `SetParent` (the world transform is kept) | 3.83 | 425 |
| `GetWorldTransform` (depth 2) | 2.17 | 217 |
| `DuplicateEntity` (100 groups x 10 objects) | 1.62 | 1 622 |
| `Scene::Copy` (the editor's Play copy) | 6.93 | 693 |
| `DestroyEntity` newest first | 4.40 | 440 |
| `DestroyEntity` in random order | 323 | 32 338 |
| `DestroyEntity` oldest first (1 000 of them, 10 000 in the scene) | 99 | 99 351 |
| `DestroyEntities(all)` - one batch | 6.93 | 693 |
| `DestroyEntityDeferred` all + the release at the end of the frame | 6.63 | 663 |
| Frame: 10 000 sprites, hierarchy (no physics, no scripts) | 0.165 / frame | 17 |
| Frame: 1 000 script `OnUpdate` | 0.308 / frame | 308 |
| Frame: 500 `Instantiate` + 500 Destroy (in a scene of 10 000 objects) | 1.60 / frame | 1 597 |

In a Debug build (the same computer): `CreateEntity` 1 918 ns, `Scene::Copy` 3 343 ns, an empty frame 0.72 ms, 1 000
scripts 2.75 ms, the 500 + 500 frame 25.7 ms (of which `Instantiate` 39 µs / object - 2 µs in Release: every copy
checks ~90 component types, and in unoptimized code every check is an EnTT pool lookup).

How to read it:

- **The scene API costs** 2-3 times more than bare EnTT when creating (the name, the UUID map, 4 mandatory components)
  and the same when iterating (`GetAllEntitiesWith` is the same EnTT view).
- **The scene order is kept** (the file, the hierarchy and physics determinism rely on it): deleting one old object with
  `DestroyEntity` shifts every later object by one place - O(N). In a scene of 10 000 objects the oldest object takes
  ~0.1 ms. When deleting many objects use **`DestroyEntities`** (editor) or the scripts' **`Destroy`**
  (`DestroyEntityDeferred`, one batch at the end of the frame) - both are a single pass (0.7 µs / object).
- **A bug found and fixed:** `DestroyEntityDeferred` checked for repeats by searching the whole queue - thousands of
  `Destroy` in one frame were quadratic (10 000 objects: 525 ms in Debug, now 87 ms; Release 6.6 ms). Now it is a set
  (`m_DestroyQueued`); the `DeferredDestroyOnceEachAndLinear` test checks it (and that every object is deleted once) -
  with the old code the test fails.

## Serialization: large scenes, copying, component cost (`ZaminBench --serialize`)

`Tests/SerializeBench.cpp`: `ZaminBench --serialize [--entities N] [--content 2d|3d|all] [--json file]` or
`Tests/bench.sh --only serialize`. Headless, in the Platformer2D project (the paths point to its assets):

- **A 2D scene** (N objects: sprites, 2D bodies, scripts with fields, texts, tags, hierarchy) and **a 3D scene**
  (meshes with materials, 3D bodies, lights, particles, sounds, scripts, hierarchy): JSON text (`SerializeToString`,
  writing the file, the `Deserialize` stages - `SceneLoadTiming`) and the binary cache (`.zsceneb`, MessagePack), MB and
  MB/s. A loaded scene saved again must give the same text (on a difference the program ends with code 1);
- **copy / paste**: a selection of 1 000 objects (`SerializeEntities`, `DeserializeEntities` with new UUIDs);
- **component cost**: for every component 1 000 objects (with only that component) are saved and loaded and the cost
  of an empty object is subtracted - how many ns and bytes one component adds.

Linked prefab instances separately: `ZaminBench --serialize --prefabs` (`Tests/PrefabBench.cpp`,
[Prefabs.md](Prefabs.md#17-performance)).

### Baseline (2026-11-08)

Environment: as above, **Release**, 10 000 objects (`Tests/Baselines/serialize-release.json`). Note:
`SceneSerializer.cpp` is compiled with `-O2` even in a Debug build, so the Debug numbers are close.

| Operation | 2D scene | 3D scene |
|---|---:|---:|
| JSON size / binary cache size | 8.8 MB / 3.2 MB | 12.7 MB / 4.9 MB |
| Save: JSON text | 119 ms (74 MB/s) | 204 ms (62 MB/s) |
| Save: binary cache | 96 ms | 172 ms |
| Load: JSON (parse + objects) | 176 ms (107 + 67) | 257 ms (169 + 82) |
| Load: binary cache (parse + objects) | 99 ms (38 + 59) | 134 ms (62 + 70) |
| Copy / paste, 1 000 objects (0.85 MB) | 8.3 ms / 13.3 ms | |

The cost of one component (save / load ns, JSON bytes; the empty object - the whole object):

| Component | Save ns | Load ns | Bytes |
|---|---:|---:|---:|
| empty object (ID, Tag, Transform, Relationship) | 2 931 | 6 100 | 364 |
| SpriteRenderer | 3 463 | 4 231 | 331 |
| CircleRenderer | 1 980 | 2 660 | 214 |
| Text | 3 016 | 3 649 | 271 |
| MeshRenderer (primitive, embedded material) | 8 981 | 9 976 | 776 |
| Camera | 4 470 | 5 903 | 489 |
| PointLight | 1 451 | 1 899 | 154 |
| Rigidbody2D / BoxCollider2D | 2 365 / 2 967 | 2 667 / 3 876 | 226 / 292 |
| PolygonCollider2D (5 points) | 4 486 | 6 466 | 594 |
| Rigidbody3D / BoxCollider3D | 2 493 / 2 411 | 2 964 / 3 311 | 237 / 268 |
| NativeScript (3 fields) | 3 652 | 5 393 | 356 |
| AudioSource | 3 402 | 5 132 | 270 |
| ParticleSystem (default modules) | 13 188 | 18 644 | 1 168 |
| Animator | 3 740 | 4 142 | 293 |
| UITransform + UIButton | 6 906 | 10 664 | 839 |

How to read it: ~60 % of the load time is JSON parsing (the binary cache cuts it ~3 times, which is why the game loads
from `.zsceneb`); the rest is creating the objects. The most expensive components are ParticleSystem (the settings of
every module) and MeshRenderer (the material); the object itself (4 mandatory components) is the largest share when
loading.

## M25 optimization: before and after

Environment: the computer above, **Release** (`build-m25rel`: `cmake -S . -B build-m25rel -G Ninja -DCMAKE_BUILD_TYPE=Release`;
build folders other than the main `build/` now write the game modules into their own `GameModules/` and their programs
load them from there - the main build's modules in `Projects/*/Binaries` are not overwritten), llvmpipe, xvfb. "Before" is
the build of the commit at the start of M25 (`37d2916`), "after" is the build after tasks 481-487; both with the same
benchmark code (a copy of the earlier build's `ZaminBench` and `libZamin.so` was kept). The heavy 3D cases under llvmpipe
fluctuate by ±10 %, so the times were measured in pairs (before, after, before, after). The new measurements:

- `ZaminBench --render`: **allocations / frame** for every case (`operator new` calls until the frame is submitted,
  `Tests/AllocCounter.h`; `ZAMIN_ALLOC_TRACE=<n>` prints their call stacks), why 2D batches were flushed early
  (`Flush2DTextures` / `Flush2DKind` / `Flush2DState`) and the draws `BatchOrder` moved; the new `mixed2d` case (1 000
  cells: sprites with 24 textures, circles and texts in turn, mixed by depth) and its copy without `BatchOrder`,
  `mixed2d-sorted`; `--case loading` - background loading.
- `ZaminBench --ecs`: the allocations of UpdateRuntime frames, `GetWorldTransform` when asked again and when the roots
  moved, reading straight from the component pool.
- `ZaminBench --fonts` (`Tests/FontBench.cpp`): rasterizing a font atlas and reading it from the disk cache, glyph
  lookup, laying out text with and without kerning.

### 2D batching (481)

`mixed2d`, one build, `BatchOrder` off / on:

| | Draw calls | Batch breaks (kind / texture / state) | CPU ms (average) | Frame ms, with the GPU |
|---|---:|---|---:|---:|
| Without `BatchOrder` (sorted order) | 670 | 667 / 0 / 0 | 15.98 | 22.95 |
| With `BatchOrder` | **65** | 62 / 0 / 0 (431 draws moved) | **10.14** | **17.03** |

The image does not change: only draws that do not overlap on the screen are moved (`ZaminRenderTests` 12/12, difference
0); with a row of sprites using 24 textures (the old `mixed2d` variant) the 22 "17th texture" breaks disappear with the
grouping by texture. `sprites` (10 000 sprites, 3 draw calls) did not change - its batch was already a single one.

### Per-frame allocations (482)

| Case | Before (allocations / frame) | After | What changed |
|---|---:|---:|---|
| `sprites` (10 000) | 10 023 | **2** | the world transforms are in the scene cache, not in a per-frame `unordered_map`; the 2D list is kept |
| `meshes` (1 000, instancing) | 1 504 | **6** | the instancing groups in a flat list (`FlatBatches`), the `EndScene` lists are kept |
| `meshes-noinst` | 7 105 | **6** | + shader uniform names as `std::string_view` (a name of 16+ characters was a `std::string` on every call) |
| `combined` | 11 522 | **7** | all of the above |
| `mixed2d` (the old variant) | 2 998 | **2** | + the text layout cache (`TextComponent::GetWorldLayout`), the Renderer2D uniform names |
| ECS: empty frame / 1 000 scripts / 500+500 objects | 0 / 11 / 1 518 | 0 / 11 / 1 518 | (UpdateRuntime: creating and deleting objects allocates by its nature) |

Time (paired measurement): `meshes-noinst` CPU 149.0 / 149.6 -> 141.4 / 142.7 ms (~5 %); `sprites` 25.0 / 25.2 -> 24.6 / 25.3 ms
(within the noise - there rasterization takes the time). To keep it from coming back, the
`SteadyFramesMakeNoHeapAllocations` test: a warmed-up scene frame (300 objects, a hierarchy) makes not a single
allocation in `UpdateRuntime` + `GetWorldTransform`.

### The world transform cache and pools (483, 484)

| Operation (10 000 objects, depth 2) | Before ns / each | After ns / each |
|---|---:|---:|
| `GetWorldTransform` the first time | 221 | 125 |
| `GetWorldTransform` again, nothing moved | 221 (computed every time) | **33** |
| after the group roots moved | 221 | 114 |
| `Entity::GetComponent<Transform>` (a pool lookup every time) / with the pool taken once | 6.6 | **1.7** |

The cache compares the object's Translation / Rotation / Scale, its parent and the parent's version - it is correct even
when a script writes to `Transform()` directly, when the parent changes and when an object is deleted and its slot
reused (`WorldTransformCacheFollowsChanges`). Objects attached to a bone (the animator pose) and worker threads compute
without the cache. The render loops (the 2D part of `RenderScene`) take the component pools once
(`Scene::HotStorages`).

### Fonts (485)

| | Before | After |
|---|---:|---:|
| The main atlas (2048 px, 600+ glyphs, 2x2) | 45.0 ms rasterizing | **2.4 ms** reading from the cache (a 1.87 MB file) |
| Glyph lookup | 4.46 ns (a hash map) | 3.46 ns (a direct table; Debug: 65.6 -> 13.0) |
| Text layout, 24 px, word wrap (without kerning) | 16.6 ns / character | 11.5 ns / character |
| Text layout, 24 px, word wrap, with kerning | - (there was no kerning) | 15.7 ns / character (a pair cache) |
| The width of "AVATAR To'p Yer", 24 px | 171.5 px | 161.2 px with kerning on (AV -0.062 em) |

The two text layout rows were measured again in the same new Release build with `ZaminBench --fonts`: kerning itself
adds ~37 % per character (11.5 -> 15.7 ns), and the new path without kerning is ~30 % faster than the old one (the
earlier table's "15.9 ns with kerning" was compared with the old build's 16.6 ns without kerning - which hid the cost of
kerning). Kerning is off by default - the render images did not change; `"Import": { "Kerning": true }` in a font's
`.zmeta` turns it on.

### Background loading (486, 487)

`ZaminBench --render --case loading`: Showcase3D's 27 images and 4 models (from an empty cache):

| | Main thread busy | Longest frame | Everything ready |
|---|---:|---:|---:|
| At once (`Load`) | 103.8 ms | 103.8 ms (one frame) | 103.8 ms |
| In the background (`LoadAsync` + `AsyncLoader::Update(4 ms)`) | 39.1 ms (spread over 9 frames) | **6.9 ms** | **50.3 ms** |

Decoding and glTF parsing run in parallel on 3 worker threads; only uploading to the GPU stays on the main thread.

### Shader cache (488)

For 488-494 "before" is the Release build of the commit before these tasks (`c253510`), "after" the one after them;
both with the `build-m25rel` settings, one after the other on one computer.

The engine's 25 shader programs when the Showcase3D player starts (the `Shaders: ...` line in the `Renderer::Init` log;
the rest of the startup - the scene, textures - did not change). "Before" is without the cache
(`ZAMIN_SHADER_CACHE=0`, the same as the old path: compile + link every time):

| Mesa's own disk cache | Before (compiling) | After, first run (compile + store) | After, later runs (from the binary) |
|---|---:|---:|---:|
| off (`MESA_SHADER_CACHE_DISABLE=true`) | 253 ms | 263 ms | **74-91 ms** |
| on (the usual) | 72 ms | 69 ms | 64-66 ms |

llvmpipe offers a binary format (`GL_NUM_PROGRAM_BINARY_FORMATS` = 1), but even when it takes a program from the binary
it rebuilds the LLVM code - so 3 times, not 10 times; with Mesa's own cache on it already saves much of the work. On real
GPU drivers (without their own cache or when it was cleared) the gain is close to the "off" row. 25 files, 548 KB. The
logic for a driver without a format is checked in the `ShaderCacheStoresProgramBinaries` test.

### Frame uniform buffer (489)

The mesh shaders' per-frame values: before, 49-66 `glUniform*` calls per mesh program (plain, skinned, instanced) (with
IBL the 9 `u_SH[i]` names were built as a `std::string` every frame) - 147-198 calls per frame; after, a single
`glBufferSubData` (the used part: 1 904 bytes in the `meshes` case) + one `u_DebugView` per program. The sampler uniforms
(`u_AlbedoMap` ...) once in `Init`. The time difference is within the llvmpipe noise (these calls are once per frame, not
per draw); the image is the same (`ZaminRenderTests` 12/12, the differences as before).

### Culling in the shadow pass (490)

`ZaminBench --render --case meshes` (1 000 meshes, 4 cascades, a sun shadow):

| | Shadow draws | Culled (of which "invisible shadow") | Draw calls (the no-instancing case) | CPU ms (`meshes`, average) |
|---|---:|---:|---:|---:|
| Before (the `c253510` build: only by cascade volume) | 1 024 | 1 979 (-) | 2 027 | 156.5 |
| After, `meshes-allshadow` (the same build, culling by view off) | 1 024 | 1 979 (0) | - | 146.5 |
| After | **323** | 2 680 (**701**) | **1 326** | **126.0** |

By cascade (after): 1 / 1 / 321 / 0 casters (before 1 / 22 / 1001 / 0). The image does not change: the test is
conservative (with a margin for the normal offset and the filter) - render tests 12/12, the difference as before.

### Parallel particles and animation (491, 492)

Earlier (369) independent **systems** were spread over the worker threads - one large system still ran on one thread.
Now the particles inside a system are also split into chunks of 2 048 (from 4 096 on; without world collision,
collision events and trails). One system, ~49 000 particles, noise with 2 octaves; `ZaminBench` runs on one thread by
default (`--jobs 0`), and the number of worker threads is given with `--jobs`:

```bash
build-m25rel/bin/ZaminBench --case "One big system"            # 1 thread ("one thread")
build-m25rel/bin/ZaminBench --case "One big system" --jobs 3   # 3 workers + the main thread
```

| | 1 thread (`--jobs 0`, the earlier behaviour) | 3 workers + the main thread (`--jobs 3`) |
|---|---:|---:|
| Frame, average (an idle computer) | 12.2 ms | **4.0 ms** (3.1x) |
| Measured again (a loaded computer, reviewer) | 11.8-12.3 ms | fastest 3.50 ms, average 7.64 ms |

The parallel case shares the cores with other processes: on a busy computer the average time moves well away from the
fastest one (a single thread hardly changes).

(The earlier build did not have this case; a single system there always ran on one thread because of the
`parallelSystems.size() > 1` condition - the "1 thread" column is that path.) The many-system cases did not change
(`Noise`: 3.46 -> 3.43 ms, `Plain` 0.17 ms).

Animation: the poses (369) run on the worker threads as before (`--animators 300`: 3.2 ms -> 1.0-1.2 ms with 3 workers,
before and after). New: marking the animators' visibility (`MarkVisibleAnimators`: moving every skinned mesh's box with
every bone) is parallel too; 300 skinned meshes with 40 bones, CullPose: **0.196 -> 0.114 ms**. When the work is small
(bone boxes < 2 048) it stays on one thread - otherwise the parallel work itself costs more (on a rig without meshes
0.031 ms on one thread, it would be 0.066 ms in parallel). The result is identical to the bit:
`LargeParticleSystemSteppedInPiecesMatchesSerial`, `AnimatorVisibilityMarkedInParallelMatchesSerial`.

### Editor: a large hierarchy and folder (493, 494)

A scene of 10 000 objects (1 000 groups x 9 children, all expanded) and a folder of 5 000 files, the Release editor,
1600x900, xvfb:

| | Hierarchy list, ms / frame | Content browser cells, ms / frame | Editor frame (status bar) |
|---|---:|---:|---:|
| Before (the `c253510` editor) | - | - | 42.7 ms (23 FPS) |
| Without the clipper (`ZAMIN_EDITOR_NO_CLIPPER=1`, everything drawn) | 10.8 (10 000 rows) | 7.8 (5 000 cells) | 44.0 ms |
| After (`ImGuiListClipper`) | **0.85** (27 rows drawn) | **0.06** (18 cells) | **24.7 ms (41 FPS)** |

The hierarchy's remaining 0.85 ms is gathering the flat list of 10 000 rows (expanded / collapsed state, every row for
selecting a range with Shift); the rest of the frame time is llvmpipe drawing the viewport. Measurement:
`ZAMIN_EDITOR_PANEL_TIMES=1`.

### Startup time (495)

A new measurement: in every program `StartupTimer` logs the time from loading the engine library until the first
frame is on the screen (`Startup: first frame 670 ms (engine 182, layers 88, first frame 422 ms; ~680 ms since the
process started), RAM ...`), and it also shows in the editor's Statistics panel; `ZAMIN_STARTUP_PROFILE=1` prints the
profiler tree of the startup and the first frame. `Tests/startup_bench.py` opens the editor and the player 5 times with
one frame (warming up once first: the shader / font caches) and gives the median; "process" is the exec -> exit time
measured from outside (closing included), which any build gives, so that column is compared with "before" (`14e8435`,
the build after 488-494, a copy was kept). Release, llvmpipe, `ZAMIN_MAX_FRAMES=1` (automatic mode: background loading
off).

| Program, project | Process, before (median) | Process, after (median) | First frame, after (engine / layers / frame 1) |
|---|---:|---:|---|
| Editor, Showcase3D | 1 094 ms | **750 ms** | 670 ms (182 / 88 / 422) |
| Editor, Platformer2D | 783 ms | **540 ms** | 469 ms (189 / 66 / 208) |
| Player, Showcase3D | 587 ms | 578 ms | 538 ms (217 / 70 / 276) |
| Player, Adventure3D | 561 ms | 589 ms | 551 ms (225 / 66 / 243) |
| Player, Platformer2D | 365 ms | 382 ms | 345 ms (220 / 60 / 60) |

When the editor opens as in interactive use (`ZAMIN_ASYNC_LOAD=1`, the scene images in the background) the first frame
is: Showcase3D 648 ms, Platformer2D 456 ms. The player rows did not change (the difference is within the noise, ±30 ms)
- their time is in "the rest" below.

What the profile (the Showcase3D editor, before) showed and what was done:

| Place | Before | After | What was done |
|---|---:|---:|---|
| The ImGui font atlas (first frame, "UI (ImGui)" itself) | 236 ms | 60-70 ms | the atlas has only the 225 Font Awesome icons the editor uses (from Icons.h, `IconRanges.inl`, `Tools/gen_icon_ranges.py`) - before, the whole block (~1 400 glyphs) for each of the four fonts; only ASCII text for the large font that draws only icons |
| The Scenes panel, scene files | 27 ms | 6 ms | every `.zscene`'s JSON is read in parallel on worker threads (`JobSystem::ParallelFor`) |
| The Scenes panel, cards (images) | 44 ms | 19 ms* | the scene images are opened on a worker thread, uploaded to the GPU when ready (with background loading on) |
| Player: the start scene's images | one after another | in parallel | `SceneManager::LoadNow` reads the images the scene names ahead, in parallel (`TextureLibrary::Predecode`), then uploads them in order |

\* the main thread time does not disappear completely because the worker threads compete with it on one 4-core CPU.

The rest (measured, not fixed): the window and GL context 52-68 ms, `Renderer3D::Init` 63-70 ms (25 shader programs
from the binary cache - llvmpipe rebuilds the code anyway), SSAO ~80 ms on the first frame (the driver compiles shader
variants on the first draw), fonts 17-43 ms (sending the 2048x2048 atlas to the GPU and its mipmaps). Details:
[Performance.md](Performance.md).

### Memory (496)

RAM is the process's RSS (`/proc/self/statm`), "peak" is VmHWM; "engine" is the memory the modules count themselves
(`MemoryStats`: textures, render buffers, mesh buffers ...; on llvmpipe the GPU memory is in RAM too). Editor:
`ZAMIN_EDITOR_STATS=1`, 30 frames; player: `ZAMIN_PLAYER_STATS=1` (new: a "Memory: ..." line on exit), 60 frames.

| Program, project | RAM before (peak) | RAM after (peak) | Counted by the engine |
|---|---:|---:|---|
| Editor, Showcase3D | 451 MB (467 MB) | **325 MB (341 MB)** | 40 MB (textures 29, render buffers 10.8) |
| Editor, Platformer2D | 367 MB (447 MB) | **242 MB (322 MB)** | 30 MB (textures 19, render buffers 10.8) |
| Player, Showcase3D | - | 262 MB (277 MB) | 32 MB (textures 22, render buffers 10.6) |
| Player, Adventure3D | - | 183 MB (263 MB) | 18 MB (textures 7.5, render buffers 10.6) |
| Player, Platformer2D | - | 159 MB (239 MB) | 20 MB (textures 9.4, render buffers 10.6) |

The editor uses ~125 MB less: the ImGui atlas is now 2048x2048 (because the icons and the large font shrank; the earlier
one was much larger, its alpha + RGBA copies stayed on the CPU and its texture in RAM on llvmpipe) and the atlas's CPU
copies are freed once uploaded to the texture (`ImFontAtlas::ClearTexData`, "Editor font atlas: 2048x2048 (4 fonts);
the CPU copy was freed" in the log). The player has no ImGui: its memory did not change; textures keep no CPU copy (it
is freed once uploaded), and fonts keep only the TTF file (for the sized atlases and kerning). The players have a
temporary ~80 MB growth during the first frame (peak - current; it is back by the second frame), its source was not
found (inside the frame, probably the driver).

## Gameplay combat (`ZaminBench --combat`)

`Tests/CombatBench.cpp` measures only `Gameplay::TickCombat` every frame (a 60 Hz step; the frame budgets are reset
first, as in `Scene::UpdateRuntime`) and counts its heap allocations with `AllocCounter`. After 120 (60 for effects)
warm-up frames, 600 frames are measured:

- **Combat2D / Combat3D** — 128 projectiles (the pool is full), up to 32 refires per frame (the spawn budget); 32 shooters
  (team 1) shoot along 32 lanes at 32 targets (team 2), 32 walls block every second lane. 128 sweeps per 60 Hz step: the
  analytic actor test (8 192 / step) and 3 Box2D rays each in 2D, a Jolt sphere cast in 3D.
- **CombatEffects** — 64 actors × 16 effects = 1 024 instances (the scene limit; the design's 512 × 16 = 8 192 is over
  this limit), each with a damage tick every 0.10–0.25 s: ~105 ticks / step, no projectiles.

Environment: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, **Release** (`build-release`), 2026-11-21. Five runs (three by
hand, `--update-baseline` and `--compare`):

| Scenario | Average `TickCombat` | p95 | Slowest frame | Allocations / frame | Target (average) |
|---|---:|---:|---:|---:|---|
| Combat2D (Box2D) | 0.069–0.106 ms | 0.092–0.150 ms | 0.19–0.99 ms | 9.26 | ≤ 0.5 ms — met |
| Combat3D (Jolt) | 0.079–0.101 ms | 0.218–0.270 ms | 0.27–0.40 ms | 12.15 | ≤ 1.0 ms — met |
| CombatEffects (1 024 instances) | 0.047–0.060 ms | 0.063–0.081 ms | 0.11–0.19 ms | 21.05 | ≤ 1.0 ms — met |

The allocations are not from the combat code itself (checked with `ZAMIN_ALLOC_TRACE`): in 2D/3D they are all the
result vectors of the physics queries (`Scene::Raycast2DAll` / `SphereCast3DAll` — a ray allocates only when it hits
something), and in effects the `std::deque` blocks of the `RuleWorld` event queue. In a Debug build (for comparison,
120 frames) the average is 0.72 / 0.95 / 0.39 ms. Baseline: `Tests/Baselines/combat-release.json`;
`ZAMIN_BUILD_DIR=build-release Tests/bench.sh --only combat --compare` (the noise floor for frame ms is 0.2 ms — these
numbers are below it, so only a large slowdown is caught). In `ctest -L bench`: `BenchCombatSmoke`
(`--combat --frames 5`).

## Vertical slice stress gates: physics, animation, in-game (`--physics`, `--animation`, `--slice-stress`)

The stress gates of the P5 vertical slices (Lantern Keeper 2D, Beacon Pass 3D — [VerticalSlices.md](VerticalSlices.md#gates)).
All three benchmarks are headless with a JSON result, and end with code 3 when one of the invariants breaks (however
good the numbers are):

- **`ZaminBench --physics`** (`Tests/PhysicsBench.cpp`) — every scenario is simulated twice and the state hashes are
  compared. *Physics2D*: a 60×40 m tilemap field, 2 048 dynamic circles/boxes, 64 actors moving with
  `Gameplay::StepMotor` (point to point), 128 `CombatWorld` projectiles always in flight between two teams (Box2D rays).
  Invariants: 0 bodies leaving the field, 0 NaNs, speed ≤ 40 m/s. *Physics3D*: a walled pit, two ramps, a 65×65 relief,
  0.25 m stairs and a 35° slope; 512 bodies (boxes, spheres, capsules), 64 `CharacterController3D`s on circular paths
  (over the stairs and the slope), 128 sphere casts every frame. Invariants: the feet do not go below −0.05 m under the
  ground, no y < −10, ≥ 80 % of the commanded distance is covered on walkable ground, the bodies stay in the pit.
- **`ZaminBench --animation`** (`Tests/AnimationBench.cpp`; the design's notation `--animators N --model ...` /
  `--animators N --sprite-clips` works too). *AnimationHumanoids*: 256 people (`BeaconPass`'s `Hero.glb` model, 21 bones,
  the real controller of the Dovon scene: a Locomotion 1D blend + UpperBody/Hit layers + events), the speed sweeps through
  every threshold, a trigger every 1.7 s, half off the screen (`CullPose`). One thread and 3 worker threads
  (`JobSystem`). Invariants: the blend weights add up to 1 ± 1e-5, no NaN/inf matrices, a culled animator is not posed,
  the pose hash is the same with 0 and 3 workers, the events come in the same count per animator at 20/30/60/144 Hz (±1
  at the end of the window), three hits = three Hit events. *AnimationSprites*: 1 024 animators without models
  (`DriveSpriteClips`, a 2D blend tree: Idle + 4 directions, the direction keeps turning). Invariants: frame =
  StartFrame + floor(phase × count) exactly, the phase does not break when the direction changes, the clip events are the
  same at four rates.
- **`ZaminBench --slice-stress`** (`Tests/SliceStressBench.cpp`) — a slice level with its real graphs (the hero, the
  level controller, the enemies), gameplay worlds and physics, without drawing; what is measured is every frame's
  `Scene::UpdateRuntime` (scripts, AI, physics, animation, combat, rules, UI bindings). *SliceStress2D* (Orchards): the
  enemies are multiplied up to the scene's limit of 32 AI brains, 200 crates (Box2D), up to 64 projectiles flying at the
  hero, 256 extra sprite Animators, 48 Light 2Ds. *SliceStress3D* (Dovon): 24 animated guards (skinned, the real
  locomotion controller), the hero, 64 projectiles, 200 bodies around the camp (Jolt). The hero is healed every frame
  (the fight does not stop). Invariants: no graph errors and no "Visual graph" / instruction budget messages, every brain
  alive or dead according to its health, the projectile count is reached.

Environment: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, **Release** (`build-release`), 2026-11-22. Five runs (three by
hand, `--update-baseline` and `--compare`); a range is the smallest and the largest result:

| Scenario | Average | p95 | Allocations / frame | Target (design) |
|---|---:|---:|---:|---|
| Physics2D — the physics step (the "Physics" profiler section) | 1.56–2.15 ms | 2.3–3.1 ms | 128.5 | average ≤ 4, p95 ≤ 6 ms — met |
| Physics2D — the whole frame (`UpdateRuntime`) | 2.25–3.04 ms | 3.18–4.20 ms | | |
| Physics3D — step + 64 Character Controllers | 1.51–1.80 ms | 2.00–2.17 ms | 82.6 | average ≤ 8, p95 ≤ 12 ms — met |
| Physics3D — 128 sphere casts | 0.23–0.28 ms | | | |
| AnimationHumanoids, 3 workers (the whole frame) | 0.57–0.82 ms | 0.66–1.10 ms | 15.2 | average ≤ 1.5, p95 ≤ 2.0 ms — met |
| AnimationHumanoids, one thread (the whole frame) | 0.99–1.73 ms | 1.23–2.07 ms | **0.0** | average ≤ 4.0 ms, 0 allocations — met |
| AnimationSprites, one thread — the Animator update | 0.30–0.48 ms | 0.38–0.61 ms | **0.0** | average ≤ 0.6 ms, 0 allocations — met |
| AnimationSprites, one thread (the whole frame) | 0.39–0.66 ms | 0.51–0.85 ms | 0.0 | |
| SliceStress2D (Orchards, game code) | 2.80–3.42 ms | 3.00–4.71 ms | | average ≤ 6, p95 ≤ 10 ms — met |
| SliceStress3D (Dovon, game code) | 3.89–5.45 ms | 5.41–6.37 ms | | average ≤ 8, p95 ≤ 12 ms — met |

The invariants passed in all five runs: 0 left the 2D field, the highest speed 6.88 m/s; the smallest 3D foot-to-ground
gap −0.0002 m, 0 sunk, the smallest distance covered 90.7 % (average 96.4 %), 0 bodies out of the pit; the two
simulation hashes equal (2D `70e0fe7db44ee00b`, 3D `6f7e6ad276c04e79`). Animation: the largest blend weight error
2.98e-08, the 0 / 3 worker pose hash `8eb468b84863e185` in both, the events at 20/30/60/144 Hz 440/432/440/440 (people)
and 960 × 4 (sprites) — 0 mismatches per animator; 0 sprite frame/phase breaks (14 223 clip switches in 600 frames).
Slice stress: 2D 32 brains (32 alive), on average 50 projectiles in flight; 3D 27 brains, on average 58 projectiles; 0
graph errors.

Allocations: the animator update on one thread is now allocation-free — before it was 223 / frame for 24 people and
870 / frame for 64 sprites (task lists, the pose signature, event vectors and clip name strings created anew every
frame; moved to `thread_local` frame memory, checked with `ZAMIN_ALLOC_TRACE`). The 15.2 / frame with 3 workers is
`JobSystem::ParallelFor` itself (task objects on every call). The allocations in physics are the Box2D/Jolt contact
results and the vectors of the ray queries; the design set no budget for them, the number is recorded in the baseline.

Baselines: `Tests/Baselines/physics-release.json`, `animation-release.json`, `slicestress-release.json`;
`ZAMIN_BUILD_DIR=build-release Tests/bench.sh --only physics,animation,slicestress --compare`. In `ctest -L bench`:
`BenchPhysicsSmoke` (`--physics --bodies 200 --actors 12 --frames 40`), `BenchAnimationSmoke` (`--animation --humanoids 24
--sprites 64 --frames 30 --jobs 2`) and `BenchSliceStressSmoke` (`--slice-stress --frames 20`) — with the invariants. A
difference from the design: the in-game stress (A3, P3) is in the headless `--slice-stress` on copies of the real levels
instead of the Player's `--benchmark` and separate `Stress` scenes (the slice assets are close to the 6 MiB budget, and
llvmpipe drawing would bury the game code's time).

## The vertical slices in the Player: profiling and the regression gate (`Tests/slice_bench.py`)

Every scene of both slices (Menu, Orchards, Yertola, Dovon) in a **Release** `ZaminPlayer --benchmark 300
--benchmark-warmup 90`, with a fixed game clock (`ZAMIN_CAPTURE_FPS=30`); in the levels the demo pilot plays
(`ZAMIN_DEMO=1`), the menus as the player sees them (no demo). Two windows: 284x160 `--quality Low` (the gate: game
code) and 1280x720 (compared only with the baseline — it is the llvmpipe rasterizer's work). *Game code* is the new
`GameCode` in the Player benchmark report: in every measured frame the profiler's "Layers (update)" section minus the
drawing ("Scenes (render)", "Post-processing") (scripts/graphs, gameplay, physics, AI, animation, particles, audio).
Every row runs 3 times with one HOME: the first is the first session (Mesa compiles the shader variants then and stores
them in `~/.cache`; that run's slowest frame is in the "first session" column), and the gate and the baseline take the
best of every number.

Environment: 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, Release (`build-release`), Mesa llvmpipe, 2026-11-22:

| Scene | Window | Objects (active) | Draw calls | Triangles / quads | Frame average | p95 | Slowest (first session) | Game code | p95 | RSS |
|---|---|---:|---:|---|---:|---:|---:|---:|---:|---:|
| Lantern Keeper / Menu | 284x160 Low | 57 (38) | 18 | 0 / 875 | 3.48 ms | 4.17 | 6 (7) | 0.32 ms | 0.40 | 197 MiB |
| Lantern Keeper / Orchards | 284x160 Low | 211 (158) | 30 | 0 / 1 183 | 5.44 ms | 6.32 | 12 (117) | 2.02 ms | 2.41 | 205 MiB |
| Lantern Keeper / Yertola | 284x160 Low | 224 (162) | 33 | 0 / 860 | 5.67 ms | 6.92 | 11 (1 869) | 2.03 ms | 2.49 | 211 MiB |
| Beacon Pass / Menu | 284x160 Low | 278 (259) | 130 | 34 286 / 266 | 22.22 ms | 26.19 | 34 (34) | 0.53 ms | 0.65 | 227 MiB |
| Beacon Pass / Dovon | 284x160 Low | 377 (330) | 210 | 74 516 / 342 | 28.16 ms | 36.06 | 51 (345) | 3.50 ms | 4.40 | 236 MiB |
| Lantern Keeper / Menu | 1280x720 | 57 (38) | 31 | 0 / 875 | 54.93 ms | 63.78 | 76 (98) | 0.51 ms | 0.64 | 225 MiB |
| Lantern Keeper / Orchards | 1280x720 | 211 (158) | 43 | 0 / 1 183 | 55.71 ms | 67.06 | 76 (126) | 2.44 ms | 3.06 | 234 MiB |
| Lantern Keeper / Yertola | 1280x720 | 224 (162) | 46 | 0 / 860 | 56.06 ms | 65.18 | 85 (1 951) | 2.50 ms | 3.27 | 236 MiB |
| Beacon Pass / Menu | 1280x720 | 278 (259) | 191 | 34 286 / 265 | 208.88 ms | 250.81 | 305 (305) | 0.68 ms | 0.86 | 317 MiB |
| Beacon Pass / Dovon | 1280x720 | 377 (330) | 295 | 74 560 / 342 | 227.13 ms | 266.12 | 328 (576) | 3.73 ms | 4.74 | 261 MiB |

The gates (small window): game code average ≤ 3 ms (2D) / ≤ 4 ms (3D) and p95 ≤ 6 ms — met (at most 2.03 / 3.50 ms,
p95 4.40 ms); frame average ≤ 16.6 ms (2D) — met (≤ 5.67 ms); slowest frame ≤ 1000 ms — met (≤ 51 ms). A difference
from the design: the frame limit for 3D is 33.3 ms (30 FPS) — llvmpipe draws Dovon's ~74 500 triangles twice (the shadow
map and the main pass) on the CPU, which even at 284x160 is ~17 ms ("Scenes (render)") + 7 ms of post-processing, while
the game code is 3.5 ms. The 1.9 s frame in the first session (Yertola, at the first fight) is llvmpipe's shader JIT for
a new particle/2D blend state: it is gone on the second run with the same HOME (the slowest is 11 ms). The 1280x720 rows
(and all of the small window) are written to `Tests/Baselines/slices-release.json`; `--compare` compares every number
with `bench_compare.py --tolerance 15`. A check: a remeasurement with 2 runs marked 3 of 40 numbers as slower (at
1280x720 Lantern Keeper Menu p95 +15.3 %, Yertola p95 +19.7 % and game code +17 %) — with 3 runs those rows were
−8.1 … +3.4 % (16 numbers, 0 slowdowns): at 720p the llvmpipe threads share the 4 cores with the game. That is why the
default is `--runs 3` and the p95 values at 720p are `Noisy` (listed, not counted). Allocations (`ZAMIN_ALLOC_TRACE`)
are not counted in the Player — the RSS and the memory the engine counts (`ZAMIN_SOAK_STATS`) are recorded instead.

Soak (`--no-bench --soak 18000`, the attract loop, 284x160 Low, a sample every 30 frames): Lantern Keeper 3 wins, 3
returns to the menu — 57 objects every time, RAM 302.9 MiB, engine 99.2 MiB (0.0 MiB growth after the first lap);
Dovon's torch 7 wins — 278 objects on every return, RAM 269.4 → 275.5 → 278.8 MiB and then flat (9.5 MiB growth ≤ 32),
engine 39.0 MiB; no errors in the log.

## Saving and comparing results (`Tests/bench.sh`)

`Tests/bench.sh` runs the headless benchmarks (ECS, serialization, prefabs, combat, physics, animation, slice stress)
and writes the results to `<build>/Bench/*.json` (+ the full text in `*.txt`); the `bench` CMake target does the same.
`--compare` compares them with `Tests/Baselines/<name>-<build type>.json` through `Tests/bench_compare.py`: when a number
gets worse by more than `--tolerance` (25 % by default) and more than its unit's noise floor (ms 0.5, frame ms 0.2,
ns 100, bytes 16, % 2) the script ends with code 1. File I/O rows (`Noisy`) are shown but not counted. A single
measurement on a busy computer (another process, the disk cache) can make several numbers more than 25 % slower (on this
computer 20 of 79 in one run, 1 in the next - with unchanged code): so a benchmark that looks slower is run again
(`--repeat`, 3 times in all by default) and the best result of every number is compared - a real slowdown shows in every
run. `--repeat 1` - a single measurement. `--update-baseline` makes the results the new baseline. The baselines were
taken on the computer on this page: on another computer first create them there with `--update-baseline`, then compare
the change.

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-release --target ZaminBench
ZAMIN_BUILD_DIR=build-release Tests/bench.sh --compare      # did the change make it slower? (a suspicious one 3 times)
ZAMIN_BUILD_DIR=build-release Tests/bench.sh --update-baseline
```

`ctest -L bench` runs the benchmarks at a small size (1 000 objects) - it checks within CI's quick tests that they keep
working (and `--serialize` its own check); the numbers are not compared there.

## Other benchmarks

| Program | What it measures |
|---|---|
| `ZaminBench` (the `particle_bench` target) | particle systems (~50 000 particles), `--jobs N`, `--profile` |
| `ZaminBench --animators N` | posing skinned animators, then marking visibility with Animator culling (`MarkVisibleAnimators`) |
| `ZaminBench --queries` | scene lookups (name, tag, component indexes) |
| `ZaminBench --serialize --prefabs` | linked prefab instances (save, load, sync, Apply; `Tests/Baselines/prefabs-release.json`) |
| `ZaminBench --nav` | navigation (bake, path finding, Nav Agents) |
| `ZaminBench --fonts` | the font atlas: rasterizing and the disk cache, glyph lookup, text layout with kerning |
| `ZaminBench --foliage` | ~1M packed foliage instances: CPU against GPU compute culling, with and without HLOD (above, `Tests/foliage_bench.sh`) |
| `ZaminBench --terrain-tiles` | a 16 x 16 tiled terrain streamed around a circling camera: background vs main-thread reads, a tight budget (above, `Tests/terrain_tiles_bench.sh`) |
| `ZaminBench --combat` | gameplay projectiles (2D rays / 3D sphere casts) and effect books, `TickCombat` ms / frame and allocations (above) |
| `ZaminBench --physics` / `--animation` / `--slice-stress` | the vertical slice stress gates: Box2D/Jolt + motors/Character Controllers, crowds of human and sprite animators, game code with the slice levels packed full (above) |
| `Tests/startup_bench.py` | editor and player startup time: up to the first frame (engine / layers / frame 1) and the process time, comparing two builds (`--bin`, `--module-dir`) |
| `Tests/player_bench.sh` | the sample games' start scenes in ZaminPlayer (ms / frame, p95, p99) |
| `Tests/samples_bench.py` | the nine sample games while a bot plays: frame time, the slowest frame and its profile, object count, the game code's CPU time, budgets (`--check`) — results and problems found: [Samples.md](Samples.md#performance) |


## The P1 map render workload (2026-11-10)

- Terrain: 257×257 points, 256×256 m, 64 chunks of 32×32 cells; the full 131072 triangles, 128 at the far LOD.
- 2D: 4096×4096 cells, 65536 render chunks, a 24×24-unit orthographic view; 119 hierarchy/leaf bounds tests instead of
  the earlier 65536 leaf tests, 4 visible chunks. A hidden map was dropped in a single root test.
- Correctness: 656 unit / 12 render / 265 editor steps, the transform/reference and manifold tests, the GL cache check.

The Player measurement: the same saved terrain, the camera at `(128,4096,128)`, far clip 10000, 1280×720, VSync off,
10 warm-up + 60 measured frames, every variant 3 times. Backend — Mesa llvmpipe, 2 workers, VPS software rendering.
The median of the three runs' averages: **LOD on 65.074 ms**, **LOD off 127.637 ms**. The off runs varied between
100.319–132.567 ms; this is a limited measurement in a virtual environment. It is not a speed promise for a real GPU or
for every scene. The full frame and percentile reports are in `build-review/Professional/lod-benchmark.json` and
`lod-on-*.json` / `lod-off-*.json`. The generic benchmark CLI — [Runtime](Runtime.md); the CPU workload regressions are
in the `TerrainLODTests` and `TilemapVisibilityTests` files.
