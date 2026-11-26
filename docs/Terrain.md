# Building 3D maps and terrain

The terrain system is for authoring 3D maps: a height field, brushes, four texture layers, planting prefabs / trees / rocks, saving and static Jolt collision. It does not replace the 2D Tilemap; for 2D maps the Tilemap and the Tile palette keep working.

## Getting started

1. Open **Window → Map authoring (Terrain)**. Or create **Object → 3D → Terrain / map**.
2. Choose the size in meters and the number of points: 33, 65, 129 or 257. **Add new terrain** adds new ground to the scene; the existing map is not removed.
3. The starting height is 8 meters. Heights range over 0–128 meters: there is room for lowering too.
4. Choose the active ground in the Terrain window. **Show the selected ground** points the camera at it.
5. Turn on **Brush active**. The yellow ring in the viewport shows the radius of influence. The transform gizmo switches off so that it does not get in the way of painting; it can be picked again with `W/E/R`.

## Relief brushes

- **Raise / Lower** — changes the height inside the radius with a soft edge. The strength is time based, so the speed does not depend on the FPS.
- **Flatten** — brings the ground to the chosen target height.
- **Smoothing** — softens sharp edges with the average of the neighbouring points. One step computes from the previous field.
- **Shift** — swaps raise/lower; **Ctrl + LMB** — takes the flatten height from the surface.
- **Alt / RMB** — the usual camera controls. The brush does not work while the camera moves.

One click-and-drag is one undo step. `Ctrl+Z` and `Ctrl+Y` undo relief, paint, trees and prefabs. Painting works only in Edit mode, on a visible and unlocked terrain. To avoid damaging other ground only the active terrain is edited.

## Textures and paths

Four layers: **grass**, **soil/path**, **rock**, **sand**. Every layer has a color texture, a color multiplier, a normal map, an ORM map and tiling per local meter. The images must be in the project's `Assets` folder; paths that lead outside and symlinks are not accepted. A layer without an image uses a flat color.

In the **Ground layers** window the normal strength `0..4`, roughness `0.04..1`, metallic and occlusion `0..1` are set. The ORM channels: **R — ambient occlusion, G — roughness, B — metallic**. The scalar values are multiplied with the matching map channel. Normal maps are tangent-space, in the positive-Y format; ORM and normal maps are not converted as colors. The color texture's sRGB import flag is respected. Occlusion affects only ambient lighting and reflections.

**Paint texture** raises the weight of the chosen layer and lowers the others; the weights always add up to 1. The GPU blends colors in linear space and computes the normal and PBR layers per fragment. The UVs are continuous over the whole terrain; texture coordinates do not break between LOD chunks. On steep slopes (cliffs, mountainsides) the color and ORM layers are also read from the sides (world X and Z) and blended by the surface slope (triplanar) so that the top-down projection does not stretch; the side projections have no normal map, and the slope is lit by its own shape. Gentle slopes are read only from above, as before. Source textures use their own import size, mipmap and sampling settings. The runtime is no longer limited to a 512×512 baked albedo; the old `GetAlbedo()` is kept only for the legacy preview.

The control map is an `RGBA32F` the size of the terrain resolution: four weights, linear sampling, clamp and UVs matched to the texel centers. 66 KiB for 65×65, about 1032 KiB for the maximum 257×257. This is only the control texture's size; the source textures, GPU mipmaps and CPU copies need separate memory. An unchanged control map is not uploaded again; changing sculpt and PBR parameters does not upload it. Painting does not rebuild the geometry. The window shows the control size, the upload count and the number of maps that were not loaded.

A missing or unsafe map is replaced with the color/scalar and a flat normal. Four layers use 12 source samplers, one control map, the shared shadow and environment — 15 fragment texture units in all (the OpenGL 4.1 minimum is 16). The terrain is currently drawn with a separate draw call; GPU instancing, texture arrays, height/parallax blending and automatic shader-layer expansion have not been added yet.

## Planting trees, rocks and prefabs

In **Plant objects** mode:

- the built-in tree and rock, or your own `.zprefab` file;
- density, minimum spacing, random scale and rotation;
- a maximum slope and alignment to the surface normal;
- 128 per click, up to 5000 planted root objects per terrain.

The built-in trees/rocks are created with collision. A prefab keeps its own components. The objects become children of the terrain and are marked with the `TerrainPainted` tag. **Erase objects** affects only the objects planted on this terrain; objects placed by hand are not removed.

The button in the **Planting and fitting to the relief** section drops the planted roots onto the current terrain surface. With **Fit automatically while sculpting** on, the roots in the area a height brush touched and on its normal boundary are updated within the same Undo step. After the generator / an import use the manual fit button. For a larger or more detailed tree make a prefab from your own model.

## Generator, heightmap and water

The **Relief generator** gives a repeatable three-octave relief from a seed, amplitude and frequency. Generating updates the heights and keeps the material paint; it can be undone.

- PNG import: 8- or 16-bit grayscale; up to 16 megapixels. The image is resampled to the terrain's point count.
- `.r16` import/export: square, unsigned 16-bit, little-endian. The values map to `0..HeightLimit` meters; the resolution is not stored in the file, the square's side comes from its size.
- The first row of a PNG/R16 matches the terrain's `Z=0` edge.
- The `.zscene` stores the real float heights and the layer weights; an R16 export is accurate to about `HeightLimit / 65535` meters.
- **Map help → Add water plane** adds a transparent blue plane; change its height in the Transform. This is not water physics or a wave simulation.

When you change a texture file in an outside program, press **Ground layers → Reload textures**. Source images are cached in common through `TextureLibrary` and use the existing asset hot reload path. Reloading does not damage the geometry. When a corrupt source is refused on reimport the previous working texture is kept; the window shows a message.

## Saving and using it in the game

Save to the usual `.zscene` file with `Ctrl+S`. The texture files must stay in the project; the scene stores their asset references. The terrain data is not tied to an outside cache.

Creating a terrain also adds a Static Rigidbody 3D and a Mesh Collider 3D. During Play/Simulate the real terrain triangles become a Jolt collider: the hero and bodies can walk over the hills. The `Collision` flag turns the terrain collision off. A dynamic terrain body is not supported.

The Play copy and a Duplicate have independent height/paint and mesh caches. The editor does not change the relief in play mode. Changing the relief at runtime with a live collision rebuild, endless world streaming and road spline generation are not in this version.

For a large map use several terrain pieces. The borders are not stitched automatically; use the same border heights or the same heightmap source. Unit scale and a positive scale are recommended for authoring.

## Checking

```bash
cmake --build build-review --target ZaminTests ZaminEditor
./build-review/bin/ZaminTests 'Terrain*'
```

Heights, smoothing/flatten, paint normalization, raycast/triangle surface agreement, the seed, serializer/copy, R16 and a body falling onto Jolt are checked. Editor automation: `ZAMIN_EDITOR_TERRAIN=demo`; `ZAMIN_EDITOR_TERRAIN=test` also checks the real Undo/Redo and the scatter erase/Undo history; the optional `ZAMIN_EDITOR_TERRAIN_SAVE=/absolute/path/Map.zscene` saves a copy of the scene. Run the automation on a copy of a test project that may be changed.


## Render chunks and distance-based LOD

The terrain is split into render chunks of 32×32 cells. As a perspective camera moves away the point step goes to
1/2/4/8/16/32. Neighbouring chunks differ by at most one level; the edge of a finer chunk is joined to exactly the same
points of its neighbour, and the edge ring triangles are rebuilt to match. No hiding wall (skirt) is used to close
cracks. A 15% hysteresis reduces rebuilds when the camera hovers around a boundary. An orthographic camera is at full
detail.

The **Terrain performance (LOD)** section has the LOD on/off switch, the detail distance and the chunk / triangle /
rebuilt-chunk counters of the last drawn view. A larger distance factor keeps more detail. The counters show the
geometry handed to the renderer; they are not to be read as the real GPU frame time or the number of chunks visible on
the screen. An extra scene view can be the last view counted.

Sculpting rebuilds the chunks affected by the changed height points and their normal derivatives. Painting only updates
the GPU control map. One active GPU mesh is cached per chunk; LOD variants do not pile up without limit. Collision,
the heightmap and the brush raycast work from the full height field. LOD does not simplify the physics.

For now this is distance-based LOD; screen-space error, geomorphing, disk streaming and automatically joining
neighbouring terrain tiles come in later stages. GPU splat/PBR layers exist.

CPU regressions: `TerrainLODTests.cpp`. For a real GL cache/rebuild test `ZAMIN_EDITOR_TERRAIN=lod-test` creates a
257×257-point, 256×256-meter map and runs the far/near/orthographic/sculpt/paint cache checks. The optional
`ZAMIN_EDITOR_TERRAIN_SAVE` saves the test scene. Pick a separate empty test project to run it.


P1 check (2026-11-10): 656 unit tests, 12 render cases and the editor's 265 steps passed without errors.
A 257×257 field is made of 64 render chunks: the full geometry is 131072 triangles, the LOD at 4096 meters 128.
A screenshot of a normal editor view shows 23056 triangles. These are geometry counters; the FPS depends on the
backend and hardware separately. The real GL check confirmed 0 GPU mesh rebuilds for an unchanged mesh, 1/64 for a small
sculpt and 0 for paint. The orthographic view kept 131072 triangles. Corners/manifold edges and final chunks one cell
wide were tested too.


GPU material regression (needs a GL context):

```bash
cmake --build build-review --target ZaminTerrainSurfaceTests -j3
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a ./build-review/bin/ZaminTerrainSurfaceTests
```

The test compares the color/ORM/normal/AO results with precomputed pixel values; it also checks picking, wireframe, binding with a native mesh, UV tiling, source reload/recovery, UV agreement across LOD chunks, shadow atlas resize, the control cache/resize and safe paths. The CPU tests cover the new fields, copying, the defaults of old scenes and corrupt PBR values. Native C++ game modules must be rebuilt when the engine SDK structures change; the SDK ABI preflight refuses mismatching modules before they run; [SDK compatibility](Scripting.md#native-sdk--abi-compatibility).

`ZAMIN_EDITOR_TERRAIN=material-test` selects the existing terrain in a MapWorkshop copy, checks that the 12 PBR maps are loaded and opens the terrain window. It does not create new relief. Float weights are not re-divided during validation for a small rounding error: otherwise some weights could flip between two values and call a control upload every frame. After GPU sampling the weights are normalized again; this case is tested in the CPU and GL regressions.


P2 check (2026-11-10): the Release build and the UI-free standalone were built; 696/696 unit tests, the 12/12 render regression, the GL 4.1 terrain pixel/cache suite, the editor `material-test` and the MapWorkshop export tests passed. The export contains all 12 PBR maps; the Player passed the screenshot and started after the folder was moved. The editor opened with a 67600-byte control map, 1 upload and 0 missing maps. The logs and images are in `build-review/TerrainPBR/`. These are Mesa llvmpipe correctness tests, not a physical GPU frame-time estimate.


## Reprojecting foliage onto the surface

`TerrainFoliage::Reproject` changes, in Edit mode, only the `TerrainPainted` roots that are direct children of the terrain. X/Z, scale and the prefab children are kept; aligning to the surface normal is optional. The height offset is in the terrain's local coordinates. Locked/hidden objects, objects outside the map and other hand-placed objects are skipped. At most 5000 roots are changed per call. The region and offset parameters are in the API too; reprojecting during gameplay/Simulate is refused.

Fitting with the button and automatically with the brush goes into the Undo/Redo history. The automatic fit checkbox, normal alignment and the offset are editor session settings in this version; the changed transforms are saved through the scene/prefab. The scene instancing counter shows the mesh instancing of the whole scene. This entity authoring path is separate from packed foliage; HLOD and streaming are later work.

`ZAMIN_EDITOR_TERRAIN=foliage-test` changes the height in a MapWorkshop copy, fits the roots and checks the real editor Undo/Redo state. Do not run it on the original project; it is meant for a test copy.

## Packed foliage and two-model LOD

`Rendering → Packed foliage` stores up to 65536 static instances inside one entity. The inspector sets the near/far primitive or model, the tint, the LOD and view distances, hysteresis, the backend and shadows. There are buttons for creating a grid and for placing onto the surface of the direct parent terrain. The instance transforms are saved through scene/prefab/copy/undo; model paths are written with the asset UUID. The terrain brush's old entity-reproject checkbox does not update packed instances automatically: fit them with their own button.

With OpenGL 4.3 a compute shader checks the frustum/distance, gathers the visible instances into near/far SSBOs and prepares the indirect draw commands on the GPU. OpenGL 4.1 or the CPU backend applies the same rules on the CPU and uses instanced draws. The LOD is chosen by the distance to the center; distance culling also takes the bounds radius into account. Hysteresis reduces flicker at the boundary. When the camera changes the static source transforms are not uploaded again; the CPU backend updates the visible instance buffers.

The GPU visibility counters show the last ready result of three asynchronous fence slots. The renderer does not wait for them; `DebugVisibleCounts` is a blocking oracle only for tests/tools. The CPU memory estimate in the inspector covers the batch arrays and the GPU estimate the instance/SSBO/command buffers; the component copy, model textures, the driver and the overall process RSS are separate. The general renderer mesh/triangle counter does not read the GPU packed visibility synchronously: see that information in the foliage panel.

V1 accepts static opaque/cutout meshes and up to 128 model parts per LOD; with a skinned, morph or transparent part the whole model is refused. The winding must be the same within one batch. No collider/script entity is created per instance; the picking ID and the visible selection outline belong to the whole component. An occluded packed silhouette is not drawn. The shadow path draws every instance conservatively with the near LOD; for a large forest mind the shadow toggle and the budget. GPU shadow culling, billboards, HLOD, disk streaming and a per-instance brush are the next stage.

The repeatable GL oracle:

```bash
cmake --build build-review --target ZaminFoliageRenderTests -j3
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a ./build-review/bin/ZaminFoliageRenderTests --require-gpu
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 MESA_GL_VERSION_OVERRIDE=4.1 MESA_GLSL_VERSION_OVERRIDE=410 xvfb-run -a ./build-review/bin/ZaminFoliageRenderTests
```

The oracle checks 4096 instances, five camera states and CPU/GPU pixel/count agreement, hysteresis, the source upload cache, picking, wire/shaded-wire, shadows and switching the backend. This is a Mesa correctness test; it is not evidence of physical GPU speed.

V1 check: 711/711 unit tests, 12/12 reference renders, the GL 4.3/4.1 foliage oracle, SDK compatibility and the runtime without UI passed. `FoliageWorkshop` was reopened, exported and started from another folder. In the control view with 4096 instances 54756 triangles were drawn instead of 491520; camera input did not increase the uploads. This is a geometry and correctness measurement; physical GPU frame-time acceptance is still separate. The evidence is in `build-review/PackedFoliage/`.


## World-cell manifest — the metadata base for streaming

`WorldManifest` in `Zamin/Scene/WorldManifest.h` describes 2D and 3D maps in one format. XY cells are used for 2D, XZ or XYZ for 3D. A `.zworld` JSON file has the `Format` value `ZaminWorld` and the `Version` value `1`; `Name`, `Origin`, `CellSize` and `Cells` are required. Every cell has a non-zero 16-hex-digit `ID`, three integer `Coordinate` values, a `Layer`, a `.zscene` `Scene` path relative to the asset root, `CPUBytes`, `GPUBytes` and a `Dependencies` list of UUIDs.

Within one layer coordinates do not repeat; other layers can sit on top of each other in one place. `DependencyOrder` returns the dependencies first and the ready cells in the manifest's index order. Missing, duplicate, self-referencing and cyclic dependencies are refused. `Bounds` gives a double-precision metadata AABB; the renderer has no floating-origin system yet. `DeclaredMemory` only adds up the CPU/GPU estimates the author gave; it is not a measurement of real residency or VRAM.

`Deserialize` and `Load` keep the previous manifest on an error; `Serialize` does not replace the output text on an error either. `Save` checks first, then replaces the file atomically. Parsing can run on a separate worker thread into an independent object; writing to the same object in parallel needs outside synchronization. Limits: an 8 MiB file, 32 levels of JSON nesting, 4096 cells, 64 dependencies per cell, ±1 000 000 on the coordinate axes, 1 TiB per CPU/GPU estimate. A repeated JSON key is refused as well.

`ResolveScene(assets, cell)` checks for traversal, absolute paths and escaping the root through a symlink on disk. For a missing scene a safe path can be returned: the later loader must report the IO error separately. This check does not block the disk from changing between it and a later file open. Loading a manifest creates no entities, colliders, scripts or GPU resources. For now the editor panel, the AssetRegistry/export link, a runtime activation/unload budget and residency management are queued; a `.zworld` does not stream existing scenes automatically.


## WorldCellLoader — CPU staging on a worker

`WorldCellLoader::Create(manifest, assets, limits, error)` in `Zamin/Scene/WorldCellLoader.h` checks the manifest, pins the Linux asset directory with an FD and opens 1–4 private workers. Even with the `JobSystem` off, disk reads and parsing do not run on the owner thread. `Create` and every API call belong to a single owner thread; a call from another thread throws `logic_error`. A construction error returns `nullptr` and an explanation.

`SetWanted(cellIDs, error)` replaces the list of cells chosen by the camera/layer, together with their full dependency closure. An unknown UUID or a closure that does not fit leaves the previous choice unchanged. A new request goes `Queued → Reading → Ready`; an error leads to the `Failed` state and the dependants get a clear error too. Parsing starts after a dependency is `Ready` or `Transferred`; this does not mean the scenes are activated.

Every new request has a monotonic generation number. Removing a cell from the choice drops the staged data or marks the worker for cancellation; adding it again creates a new generation. When an old worker finishes, its result does not mix into the new request. The same choice keeps the generations; to retry a failed cell, remove it and add it again. Unloading active game entities is not yet part of this API's job.

Call `Poll(maxCompletions)` every frame: it announces at most that many ready results, does not wait for unfinished workers and queues the dependency-ready work that fits. `GetStatus(id)` gives the state/generation/error/file bytes, `GetStats()` the queue and reservation counts. `TakeReady(id, generation)` hands over only a matching ready result as a `unique_ptr<SceneLoadJob>`; at that moment the state becomes `Transferred` and the loader's reservation is freed. This state is not activation. The caller is now responsible for the received job and its memory; include `SceneSerializer.h` to use/destroy it. The next `Poll` accepts work into the freed capacity.

Defaults: 2 workers, 128 wanted cells (with dependencies), 16 MiB per file, a 64 MiB source reservation, 262144 JSON key/value nodes and 32768 entities. Every queued/running/ready job reserves the maximum file size up front; a cancelled unfinished job also stays fully counted. **The source reservation is not a real RAM/VRAM limit**: the DOM, parser scratch, migration and jobs handed to the caller are outside it. The hard configuration limits: 64 MiB per file, a 512 MiB source reservation, at most 64 reservation slots, 1 000 000 nodes and 100 000 entities. When there is no free capacity the queue waits.

The worker refuses symlinks in every child segment with `openat`/`O_NOFOLLOW` (even a symlink pointing inside the root is not accepted). `O_NONBLOCK` and `fstat` keep FIFOs/devices/directories from being read as a scene; when the file length changes while it is being written, the result is refused. Even when the asset root is swapped for another directory, an existing loader keeps looking at the directory it opened. Only `.zscene` JSON text: `Scene`, `Version`, `Entities` are required, with 64 levels of nesting and a duplicate-key check. Migration from 0 to the current scene version runs on the worker, a future version is refused. The binary cache and the `.znav` sidecar are not connected yet.

Reading is in 64 KiB chunks, and the SAX check sees cancellation at node boundaries; it does not stop the following DOM parse/migration stage midway, but drops its result when it finishes. The destructor cancels and joins the workers; a filesystem operation stuck inside the kernel is not guaranteed to be interrupted at once. `Poll` is limited by count; there is no hard millisecond deadline for freeing the DOM or for a later `SceneLoadJob::Build`. Build must run only on the main thread, in the matching active project's asset context. Texture decode/upload, the physics/script lifecycle, automatic camera selection, activation/unload and residency integration are the next stage.


## The large runtime package: camera-driven 2D/3D streaming

`Project.StreamingWorld` is the path of a `.zworld` inside `Assets`. When Play/the Player starts, the `SceneManager`
reads the manifest and its `Streaming` policy. The **Window → World Streaming** panel saves the manifest/policy and
links it to the project. During the game the panel shows the active/loading/build/failed cells, the viewer on the grid,
the input reservation and the declared budgets; there are retry, reconnect and disable. The manifest is saved with its
policy through `WorldStreaming::SaveDefinition`; the plain `WorldManifest::Save` saves only the metadata. Cells can be
added and their scene/layer/coordinate/CPU/GPU estimates edited.

Selection works by AABB distance: a new cell inside `LoadRadius`, while a previously selected cell is kept until it
leaves `UnloadRadius`. `AxisMask` XY=3, XZ=5, XYZ=7; the dependency closure is taken from the active `Layers` list. The
nearest cells come first; precomputed dependency bitmasks save a graph traversal every frame. A closure that does not
fit is not partly activated.

The policy limits `MaxResidentCells`, `MaxResidentEntities`, `MaxEntitiesPerCell` and the declared CPU/GPU
reservation. While old active cells still stand within the unload limit, new cells are admitted only into the
remaining capacity. These declared figures are not the real process RSS/VRAM; a residency/eviction policy for the shared
asset caches and physical GPU acceptance are still separate work. The source IO reservation is counted separately too.
A zero estimate means the author gave no resource estimate.

On the main thread the scene build runs in pieces, with asset resolve limited by entity count and the
`MainThreadMilliseconds` soft budget. Even with a small budget at least one build/resolve quantum runs, which prevents
starvation. `MaxBuildingCells`, `MaxResolveEntitiesPerFrame`, `MaxActivationsPerFrame` and `MaxUnloadsPerFrame` limit
the work in one frame. A single entity/asset operation, hierarchy linking, physics/script activation or an unload
callback is not interrupted midway; this is not a hard frame deadline. With `AssetStreaming` off (the default, old
worlds) the texture/model IO in resolve can be synchronous; with it on they load with the
[asset budget](#the-asset-budget-residency-limits-and-the-camera-path) below.

All the entities of a cell are moved into the anchor scene together through `Scene::ImportStagedCell`. Physics, scripts
and effects start once every copy and the hierarchy exist; that is why the anchor's player and a streamed floor share
the same 2D/3D collision world. UUIDs and internal references are kept, and a collision is refused before any mutation.
On a copy/start error the imported objects are rolled back; rolling back the outside side effects of a user C++
callback is not guaranteed. The authored transform inside a cell is local; `Origin + Coordinate * CellSize` is added to
the root.

Activation/unload runs at the end of `SceneManager::ProcessRequests`; do not call the streaming host APIs inside a
callback. Dependency cells become Active first, and unload goes in reverse dependency order. Unload destroys the owned
UUIDs from the anchor; bodies and the script lifecycle go through the normal entity destroy path. The bus payload of
`WorldCellLoaded` / `WorldCellUnloaded` is `WorldStreaming::CellEvent`. The anchor is protected from being unloaded on
its own. Stop removes the world's objects; Start again leaves no duplicates. A Single scene load clears the world; it can
be connected again through the host for another level.

`Projects/WorldWorkshop3D` — 36 cells; `Projects/WorldWorkshop2D` — 24 cells. Press Play and move the camera with WASD.
`ZAMIN_WORLD_AUTOTOUR=1` takes the camera to a new area, an empty area and the starting point every 120 frames; the
window title and the log show the active/load/unload statistics. The geometry, colors and the physics probe are built
from authored primitives, without outside assets.

V1 limits: the scene files are separate per cell, and entity UUIDs are unique across all loaded content; a SubScene
component inside a cell is refused. A runtime offset larger than ±1 000 000 units is refused until there is a floating
origin. Layer/policy changes are applied with Save + a runtime reload. When a scene file is renamed, update the manifest
path in the panel. There is no binary world sidecar, HLOD, polygon navmesh or physical VRAM measurement; the RSS /
requested GPU limits are below, and saving the cells' state to disk is in the checkpoint section.

Export adds the configured manifest and its scene/asset closure to the build list; the unused asset filter keeps that
closure. A missing/unsafe manifest or scene is an error in the export preflight.


### World terrain and borders

`TerrainTools::GenerateWorldTile` (`Scene/TerrainTiles.h`) samples absolute X/Z coordinates with the same
seed/base/amplitude/frequency. In neighbouring cells the shared points have the same height; the world Y offset is
subtracted from the local height. When the result falls outside the height limit nothing changes.
`StitchTiles` works only for two terrains that are only translated and share a complete border. The resolution
intervals must be in a whole-number ratio; the finer border is fitted to the shared coarse polyline. The blend rows
reduce the correction gradually into the inner rows. This is C0 geometric continuity; shading normals are not shared
between cells automatically. Changing another border of a cell later can affect an earlier corner; for a large tile set
use the same world generator.

`LockBorderLOD` is saved/copied and keeps the outer patches at full precision; the inner LOD adapts to the distance.
That is why a small, single-patch cell stays at full detail. The Terrain panel has the world parameters, the generator
and the neighbour stitch actions; the edit scene undoes them through its Undo history.

### Residency and observed memory pressure

Through `CacheEntriesPerKind` and `MaxCacheEvictionsPerFrame` the world policy clears idle cache entries in the order
model → material → mesh → texture. Automatic and manual trims share one eviction allowance within a world tick; the LRU
metadata of entries removed by thumbnails/reimports is cleared too. Within every library the oldest cache acquisition
goes first; the mesh library keeps its OBJ and primitive groups separately and checks OBJ first. Live `shared_ptr`
owners, background model/texture loads and queued mesh uploads are pinned. GPU destructors run on the main thread; GPU
resources are not destroyed under the texture cache mutex. The thumbnail `GetLoadSerial/ReleaseUnused` semantics are
kept separate from the LRU count. This policy does not apply to the prefab/audio/font/shader caches.

`ObservedRSSLimitBytes` is the Linux `/proc` RSS signal; `RequestedGPULimitBytes` is the sum of the tracked texture,
render target and mesh allocations. A zero threshold turns the signal off. They are not a physical VRAM measurement or a
hard allocation cap. At the limit idle caches are trimmed and new admission/build/resolve stops; objects already alive
are kept, and unneeded cells are unloaded first. Work resumes when the value drops below the `PressureResumeRatio` low
watermark (the threshold is rounded to the nearest whole byte, at least 1). RSS is read at most every half second;
malloc may not return freed pages to the OS at once. When pressure stays high because of pinned or staged objects, the
camera/policy/content must change. A cache scan and a single resource destructor are not preempted; the number of
evictions is limited, and there is no hard frame deadline guarantee. The editor shows the RSS/GPU histories,
pinned/loading/evicted, pressure frames and a manual trim.

### The asset budget, residency limits and the camera path

**The asset budget** (`Streaming.AssetStreaming: true`, default `false` — old `.zworld` files work unchanged). Once a
cell is built it moves to the new `Staging` stage:

1. The main thread lists the cell's `SpriteRenderer` / `Tilemap` / `UIImage` textures, the `MeshRenderer` model, its
   inline maps and the `.zmat` materials (it loads nothing).
2. `WorldAssetStager` (`Scene/WorldAssetStager.h`, 2 low-priority private workers) decodes / parses the files: the
   texture pixels and the **mip chain** (2×2 averages) are prepared on the worker, a model is parsed without a GPU with
   `Model::Stage`, and a `.zmat` is read only for its texture names. Strict budgets: `MaxAssetFilesPerCell`,
   `MaxAssetFileBytes` (the file on disk), `MaxAssetBytesPerCell` (decoded pixels + meshes; refused from the image header,
   **before** decoding) and `MaxStagedAssetBytes` (the total; while a ticket is being prepared the full cell budget is
   reserved, then its real size; a ticket that does not fit waits).
3. The main thread takes the ready files with `TextureLibrary::LoadStaged` / `Model::Load` (neither decoding nor GL
   calls) and they are uploaded to the GPU in `AsyncLoader::Update` with a **frame budget**: at most
   `MaxAssetUploadsPerFrame` calls and `MaxAssetUploadBytesPerFrame` bytes. A texture is sent row by row
   (`glTexSubImage2D`; the storage first — a separate chunk, then the base level and the mip levels); until it is fully
   uploaded it is bound as a placeholder and `GetRendererID()` returns 0. A mesh is not split: a cell with a mesh larger
   than the frame budget is refused.
4. Only when every asset is on the GPU is the cell resolved and activated — a half-loaded cell is not drawn.

A cell over budget becomes `Failed` with the file name and both numbers (it shows in the panel; **Retry** once fixed). A
missing or corrupt file is reported once, not decoded again, and the cell loads without it. When a cell is left before
it activates its ticket is cancelled: the prepared pixels are dropped, a half-uploaded texture leaves the cache and the
GPU allocation is returned (`Stats::AssetCancellations`). Cache releases also stay within `MaxAssetReleaseBytesPerFrame`
requested GPU bytes per frame (a single entry can always go). `Stats::MainThreadImageDecodes` / `MainThreadModelParses`
— the number of decodes/parses on the main thread since the world started; it stays 0 when no file is loaded outside the
staging path. While a world runs the upload budget belongs to the whole process (`AsyncLoader::SetUploadBudget`); when
the world closes the previous value comes back. The host calls `AsyncLoader::Update` once per frame (the `Application`
does so).

**Not** in the budget: terrain layer textures, fonts, animation clips, the decode size of images embedded in a model
file (their upload is in the budget), the small JSON read of a `.zmat` and textures found again by UUID (moved) — they
load as before and show in the counters. Creating/destroying GL objects takes the driver's time: the byte budget bounds
the work, not the milliseconds.

**Residency limits.** `ObservedRSSLimitBytes` and `RequestedGPULimitBytes` stop new work as before. With
`EvictOnPressure: true` they become limits:

- when cache trimming cannot give anything else, the **farthest** cell that no admitted cell depends on is evicted (the
  normal unload path: capture, event); the cell the viewer stands in is never evicted; an evicted cell is held for
  `PressureCooldownFrames` frames (`CellInfo::Held`, an orange border on the grid);
- the GPU signal is exact and immediate — one cell per frame. RSS is observed (once every half second): first the freed
  heap memory is returned to the OS on a helper thread (`MemoryStats::ReleaseFreeMemory`, glibc `malloc_trim`), then
  when a new sample is still above the limit one cell is evicted every 0.75 s;
- with `AssetStreaming` the requested GPU bytes of a prepared cell are known: when it would push past the limit it is
  **not loaded at all**, its measured size is remembered (`CellInfo::MeasuredGPUBytes`) and it waits until that much room
  is free; while a cell is waiting, idle cache entries are given up first.

These are requested GPU bytes and observed RSS — not physical VRAM. The RSS limit acts after the fact: when the limit is
crossed, a cell being prepared and its uploads may still arrive (at most `MaxAssetBytesPerCell`). Memory created during
resolve (terrain meshes) is not counted in advance.

**The camera path and the report.** `WorldTour` (`Scene/WorldTour.h`, `.ztour` JSON): camera keys tied to frame numbers
(`Frame`, `Position`, `Jump` — jump on that frame), `Warmup` frames. The position is computed only from the frame number,
so two runs ask for the same cells on the same frame. Player: `ZAMIN_WORLD_TOUR=<file.ztour>` (relative to the asset
root or absolute) moves the main camera (a root entity) along that path, and `ZAMIN_WORLD_REPORT=<file.json>` writes for
every frame the frame time, the `WorldStreaming` update and `AsyncLoader` work (ms), the bytes/calls sent, RSS, the
requested GPU bytes, the reserved staging bytes, the active/admitted/build/failed cells and the camera position, and at
the end p50/p95/p99/max, the policy and the world counters (`"Complete": false` when the tour did not finish).

Sample: [`Projects/WorldGallery3D`](../Projects/WorldGallery3D/README.md) — 24 cells, 32 PNGs, 8 OBJs;
`Tools/make_world_gallery.py` creates it and its heavy copy (2048 px, 5184 triangles) deterministically.
The measurements — [Quality.md](Quality.md#the-world-asset-streaming-package--2026-11-23).

### Grid navigation across cells

`NavigationMode` 0 (off), 2 (XY), 3 (XZ plus heights); `NavigationMaxCells` limits the union grid's memory
(default 262144, policy maximum 1048576). Only the active, baked NavGrids in active streamed cells are merged with
`BuildNavMosaic`. The cell size, grid alignment and climb rule must be the same; an overlap/mixed resolution/budget error
shows in the panel, the old mosaic is removed and no stale path is kept. An unloaded area is blocked; cost, floor height
and the source's carved walkability are copied. When the source revision, carve revision, serial or the set of cells
changes the mosaic is rebuilt; automatic agents replan. An explicitly authored grid reference wins.
`GetNavigationEntity(dimension)` belongs only to the world anchor. World close/Stop destroys the mosaic entity too. This
is a bounded, synchronous grid rebuild; an incremental polygon navmesh, multi-storey floors and a cross-resolution portal
system are separate later work.

## World authoring and the checkpoint workflow

**Window → World Streaming → Create a new 2D / 3D world** creates a new bundle: an anchor camera, a 3D sun, the cell
scenes, metadata and a manifest. 1–256 cells, a cell size of 8–256, terrain resolution 9/17/33/65. The navigation cell
size must divide the world cell's sides evenly. The terrain is created in global coordinates with the same seed. The 2D
sample gives ground, a static obstacle and XY navigation.

`WorldGridSettings::TemplateScene` is an optional cell-local `.zscene` inside Assets. The template goes through the
worker loader's byte/node/symlink protection; cameras and SubScenes are refused. The hierarchy and components are copied,
and the UUIDs and internal entity references are remapped for every cell. When a template is used its terrain/nav
content is kept; the generator does not recreate it automatically.

`CreateWorldGrid` is a CPU authoring action: every file is prepared in a new, private staging folder, then published
together through the pinned Assets FD with Linux `renameat2(RENAME_NOREPLACE)`. An existing bundle, a traversal name, a
root symlink and wrong parameters are refused. On an error the staging is removed and the result/config are kept. In the
editor **Link to project and open anchor** saves the config and opens the anchor. Opening a single cell scene and pressing
Play does not start the whole configured world; the automatic world is connected only for the start/anchor scene.
Template and grid creation are explicit, synchronous authoring operations; 256 large cells can keep the UI busy for a
while.

### Checkpoint state

`WorldCheckpoint` stores a complete serialized scene copy of every cell. A missing entry means the authored source;
empty `Entities` means removed cell content. The world manifest identity is compared in full as canonical JSON,
independent of formatting. Another world, duplicate keys/IDs, a wrong hierarchy, SubScenes, symlinks/FIFOs, and corrupt
or oversized files are refused. Limits: 8 MiB per cell, 64 MiB of source in total, a 160 MiB envelope, 32768 entities per
cell and a JSON depth of 64; the runtime policy may allow even fewer entities. These are serialized byte limits, not a
hard cap on the process RSS. The SAX node limit of a single scene JSON applies too.

The `WorldStreaming` host APIs (outside callbacks, between frames):

- `SetCaptureOnUnload(true)`: takes a RAM snapshot of a leaving cell. It does no automatic disk IO.
- `CaptureCell(id, error)`: a single active cell; `SaveCheckpoint(file, error)`: an explicit synchronous atomic write of
  the active cells + the previously captured state. On a save error the previous disk file stays.
- `RequestRestoreCheckpoint(file, error)`: preflights the file and every component in a staging scene; once valid, it
  unloads the old cells frame by frame and admits the snapshots again. The new IO/parse runs on the worker; old
  generations are cancelled/discarded. An invalid preflight does not change the live world.
- `RequestResetToAuthored(error)`: frees the RAM snapshots through the same staged retire/load; the disk checkpoint is
  not deleted.
- `GetCellEntities`, `AdoptEntity`, `RemoveCellEntity`: taking a spawned subtree into explicit ownership and removing it.
  Stealing an entity from another cell, going over the quota or mixed ownership are refused.

In the checkpoint the root transform goes back to cell-local coordinates; child-local transforms and UUIDs are kept. A
removed entity does not reappear, and an adopted actor comes back from disk too. On capture an outside parent and a child
without ownership are refused: adopt/detach the subtree first. An automatic capture error stops the unload instead of
silently destroying the cell, and the panel shows the reason. Ownership is not spatial migration: keep a moving
persistent player in the anchor or manage its ownership yourself.

A snapshot restores the transform/terrain and the components / stored script fields the serializer saves. Arbitrary C++
instance heap, physics velocity/contact replay, timers and outside service state are not saved automatically. Derived
nav/physics/GPU state is rebuilt on activation. A snapshot restore does not bring back an old asset version; the assets
come from the current project. The authored world files do not change.

### Script-safe commands and background writes

From a script callback use `WorldStreaming::Enqueue(Command, error)` instead of the host mutation APIs.
`CommandKind`: Capture, Save, Restore, Reset, Adopt, Remove. The queue stores engine-owned data; it keeps no plugin
closure that would run after a hot reload. A serial is returned (0 — refused), 64 pending in all, at most 4 started per
tick. The next command waits until the Save IO finishes. The result of a Restore/Reset command means the validated
request was accepted; the reactivation continues through the normal cell stages. Check that the actor is ready too.
The `WorldCommandCompleted` payload is a `CommandResult` (Serial, Kind, Success, Error); the stats show the pending,
completed and failed counts.

A queued Save takes a serialized snapshot of the active cells on the main thread and hands the disk serialization /
atomic write to `WorldCheckpointWriter`'s single jthread. The worker never touches the ECS, the GPU or a script instance.
The snapshot is made of shared immutable sources; a later gameplay edit does not change the copy on its way to disk. One
write in flight; the destructor joins a started write. An IO error keeps the previous file and is returned on completion.
The CPU work of capture and the restore preflight is not preempted yet; because of the source size / component weight
there is no hard frame deadline guarantee.

WorldWorkshop2D/3D: **F5** — queue a save, **F9** — queue a restore. The default file is `world-checkpoint.json` in the
project's folder in the user data; `ZAMIN_WORLD_CHECKPOINT_FILE` is a test/tool override. Both demos turn on RAM capture
on unload. The world panel has the checkpoint path, the source size, the command status, save/restore/reset, adopt UUID
and per-cell runtime entity transform/delete controls. Switching the disk slot is explicit.

`Tests/world_checkpoint_test.py` checks, in isolated projects, the edit/delete/spawn, unload/return and disk restore
oracles in both Players, then the editor wizard, the config link, Play of a generated world and actor restore, with real
GL. `build-review/CheckpointPackage/` keeps this session's evidence.
