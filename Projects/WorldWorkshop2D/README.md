# WorldWorkshop2D

24 authored cells, a persistent camera/physics anchor and a streaming manifest.

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

F5 queues a checkpoint save to the project's user-data folder; F9 queues restore. Cell edits survive
unload/return in RAM. Adopt spawned subtrees with the world command API; unowned anchor actors stay
outside cell snapshots. The checkpoint panel can edit/delete owned entities and save/restore/reset.
`ZAMIN_WORLD_CHECKPOINT_TEST=1` runs the 540-frame edit/delete/adopt/return/disk-restore oracle;
`ZAMIN_WORLD_CHECKPOINT_FILE` chooses an isolated slot. Saves contain serialized component state,
not arbitrary C++ heap or physics replay. Queued writes use a bounded background writer.

The world wizard creates a new 2D/3D bundle with terrain/ground, navigation and anchor, or clones a
cell scene template with fresh entity UUIDs. A published bundle is never overwritten by the wizard.

### Sparse navigation va portal

**N** — yashil route nuqtalari, binafsha portal chiqishi va sariq kinematic actor.
Actor portal oldida kutadi, demo explicit teleport completion qiladi; bu physics controller emas.
Topology o‘zgarsa actor to‘xtaydi va route qayta olinadi. Portal faqat loaded walkable endpoint bilan faol.
`Tests/world_navigation_test.py` disable, unload/return va qayta rejalashni ikkala demoda tekshiradi.

### NavAgent va Visual Graph

**B** — haqiqiy NavAgent + built-in Visual Graph demo. 3D actor CharacterController bilan yuradi.
`Scripts/WorldTraversal.zgraph`: Message → SetWorldDestination, NavTraversalRequested → Delay →
CompleteNavTraversal → Success/Cancel. UUID tokenlar String; link, revision va yangi ticket kerak.
Band controller exit yoki eski token teleport qilmaydi. Unloaded maydonda namuna driver'i actor
gravity'sini muzlatadi; return'da boshlang‘ich nuqtadan yangi graph destination boshlaydi.
`Tests/world_agent_test.py` fixed capture time bilan 2D/3D Player va graph editor screenshot'larini tekshiradi.

### Async yo‘l qidiruvi

World policy AsyncNavigation=true: 2 worker, 64 outstanding request, tick'da 4 start / 8 publication.
Graph'dagi Searching = NavQueryPending. Worker hech qanday Scene/physics reference olmaydi; owner stale
resultni tekshirib keyin o‘rnatadi. B demo'da har uch faza async delivered query bilan tekshiriladi.
World Streaming panelida queue va retained payload MiB diagnostikasi mavjud.


**C — crowd workshop:** four real world agents cross, stop and return to their starting side.
`CrowdMonitor.zgraph` displays neighbors/blocked/limited via `NavSteeringState`.
World `SmoothPath` now runs bounded portal-preserving smoothing on the query worker; each movement
step has a conservative atlas clearance guard. Crowd steering is opt-in and physics remains authoritative.
`ZAMIN_WORLD_CROWD_TEST=1 ZAMIN_CAPTURE_FPS=30` runs the two-phase 450-frame oracle;
`Tests/world_crowd_test.py` captures both Player and editor. Dense neighbor/candidate overflow stops visibly.
