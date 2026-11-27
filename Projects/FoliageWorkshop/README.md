# Foliage Workshop

Open `FoliageWorkshop.zproject` with ZaminEditor. `Assets/Scenes/Forest.zscene` contains 4096 static trees in one **Packed Forest** entity, parented to a PBR terrain. The two analytic OBJ fixtures have 96 and 16 triangles per tree; they are regression geometry, not production artwork. Their deterministic source is `Tools/samples/gen_foliage_lod.py`.

Select Packed Forest. The inspector controls the near/far model, tint, LOD distance (40 m), cull distance (120 m), hysteresis, and the CPU/GPU culling backend. Auto uses GL 4.3 compute/SSBO/indirect draw when available and CPU culling plus instancing on GL 4.1; both give the same counts. Move the camera to see LOD and culling change; static source upload count should remain unchanged. GPU counters show the last completed asynchronous result, and the GPU culling time comes from timestamp queries.

The HLOD fields turn far cells into merged clump proxies: set **HLOD distance** (0 = off, between the LOD and the view distance), the **HLOD transition** (the dithered crossfade width) and the **HLOD cell** size. The inspector then shows the baked cells, how many are drawn or fading, the instances they replace and how many cells the last bake rebuilt or took from its cache. **LOD/HLOD colors** paints near instances green, far ones yellow and proxies magenta. The Statistics window sums the same numbers for the scene.

Use the terrain projection button after changing the ground. It preserves X/Z, rotation and scale. It is separate from the old terrain entity-scatter workflow. Generating a grid replaces the component's instance list; editor Undo restores it, and only the HLOD cells whose instances changed are rebaked. Save, reopen, or export the project normally. No native script module is required.

Tree shadows are disabled in this dense demo to keep llvmpipe fast; when they are on, the cascades draw only the instances inside their volumes (per LOD), and HLOD proxies cast no shadow. Picking selects the entire component. These instances have no individual colliders/scripts; streaming, textured billboards and individual painting remain future work. Only static opaque/cutout model parts are supported, at most 128 parts per LOD and 65536 instances per component.

See [terrain and foliage documentation](../../docs/Terrain.md#packed-foliage-and-two-model-lod) for memory counters, limits and the reproducible GPU/CPU image oracle. Mesa software results establish correctness, not physical GPU frame-time performance.
