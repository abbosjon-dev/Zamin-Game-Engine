# WorldGallery3D

A streaming demo of 24 cells (6×4), each with its own ground texture and a textured monument model:
world streaming's **asset budget**, **residency limits** and **repeatable camera path** are shown
here. The project has no script module — everything is data: `Worlds/Main.zworld`, `Worlds/Gallery.ztour`,
24 cell scenes, 32 PNGs, 8 OBJs and 8 `.zmat` files.

Open the project in the editor and press **Play**; the **Window → World Streaming** panel has the grid of cells,
the *Asset streaming and residency limits* section (staged files, the upload queue, the frame peak,
the number of decodes/parses on the main thread, the limit counters) and the policy fields.

## The camera path and the report

```bash
ZAMIN_WORLD_TOUR=Worlds/Gallery.ztour ZAMIN_WORLD_REPORT=/tmp/gallery.json \
ZAMIN_MAX_FRAMES=1350 build/bin/ZaminPlayer Projects/WorldGallery3D
```

`Gallery.ztour` — camera positions by frame number (independent of time): 240 frames of warm-up, then
1080 frames — a slow walk along three sides, a jump from corner to corner every 6 frames (a fast camera, loads
are cancelled), an empty area (every cell unloaded) and the return to the starting place. For every frame the report
gives the frame time, the streaming and upload work (ms), the bytes sent, RSS, the requested GPU bytes and the number
of active cells, and at the end p50/p95/p99/max. Two runs give the same camera path and the same
active cells at the "quiet" points.

## Budgets (`Main.zworld` → `Streaming`)

| Field | Value | Meaning |
|---|---|---|
| `AssetStreaming` | `true` | texture decode / model parse on workers, GPU upload under a budget |
| `MaxAssetUploadBytesPerFrame` | 2 MiB | the bytes sent to the GPU in one frame (textures are uploaded row by row) |
| `MaxAssetUploadsPerFrame` | 8 | upload calls in one frame |
| `MaxAssetReleaseBytesPerFrame` | 16 MiB | the GPU bytes freed in one frame when the cache is cleaned |
| `MaxAssetFileBytes` / `MaxAssetBytesPerCell` / `MaxStagedAssetBytes` | 32 / 128 / 256 MiB | per file, per cell and total staged bytes |
| `EvictOnPressure` | `true` | when an RSS/GPU limit is on, far cells are evicted (the limits are 0 = off in this demo) |

To try the limits, enter `Observed RSS limit bytes` or `Requested GPU limit bytes` in the panel,
save and reconnect the runtime.

## Regenerating

```bash
python3 Tools/make_world_gallery.py Projects/WorldGallery3D                      # this project (bit for bit the same)
python3 Tools/make_world_gallery.py /tmp/heavy --texture-size 2048 --model-detail 3   # a heavy copy (tests)
python3 Tools/make_world_gallery.py /tmp/legacy --legacy                         # AssetStreaming off
```

Existing `.zmeta` files are kept (the UUIDs do not change). Tests: `ctest -R WorldAssetStreaming`
(`Tests/world_asset_streaming_test.py --stage budget|residency|demo`).

Limits: the requested GPU bytes and the observed RSS are not physical VRAM; terrain layer textures,
fonts and animation clips are not part of the asset budget (they load as before).
