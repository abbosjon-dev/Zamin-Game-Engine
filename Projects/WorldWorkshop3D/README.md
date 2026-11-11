# WorldWorkshop3D

36 authored cells, a persistent camera/physics anchor and a streaming manifest.

Open the project, press Play and use WASD to move the camera. Open **Oyna → World Streaming** for state, policy and retry controls. The orange physics probe shares the loaded ground’s physics world. Set `ZAMIN_WORLD_AUTOTOUR=1` for the deterministic camera route (120 frames per stop, including an empty region and a return).

Continuous world-space terrain (3D), primitives and colours are procedural; no external assets are required. The manifest declares memory reservations; these are not measured RSS/VRAM. The runtime uses soft frame budgets.

Loaded navigation tiles form one bounded grid mosaic (XY in 2D, XZ plus floor heights in 3D).
Unloaded holes remain blocked; agents with automatic grid selection use the mosaic. Authored explicit grid
references keep their own grid. This is grid A*, with one floor per XZ cell, not a polygon navmesh.
The tour logs complete cross-cell paths alongside RSS, requested GPU bytes and cache/pin/eviction counts.

World Streaming shows memory histories and per-library residency. Cache targets are 24 entries per kind,
with at most four evictions per frame. Live references and unfinished asynchronous loads are pinned.
Observed RSS / requested GPU pressure thresholds are disabled in this demo by default; configure them in
the policy panel, save and reload the runtime to test high/low-watermark admission control.
