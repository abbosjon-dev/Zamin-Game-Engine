# Foliage Workshop

Open `FoliageWorkshop.zproject` with ZaminEditor. `Assets/Scenes/Forest.zscene` contains 4096 static trees in one **Packed Forest** entity, parented to a PBR terrain. The two analytic OBJ fixtures have 96 and 16 triangles per tree; they are regression geometry, not production artwork. Their deterministic source is `Tools/samples/gen_foliage_lod.py`.

Select Packed Forest. The inspector controls the near/far model, tint, LOD distance (40 m), cull distance (120 m), hysteresis, and CPU/GPU backend. Auto uses GL 4.3 compute/SSBO/indirect draw when available and CPU culling plus instancing on GL 4.1. Move the camera to see LOD and culling change; static source upload count should remain unchanged. GPU counters show the last completed asynchronous result.

Use the terrain projection button after changing the ground. It preserves X/Z, rotation and scale. It is separate from the old terrain entity-scatter workflow. Generating a grid replaces the component's instance list; editor Undo restores it. Save, reopen, or export the project normally. No native script module is required.

Tree shadows are disabled in this dense demo: the current conservative shadow pass renders all near-LOD instances. Picking selects the entire component. These instances have no individual colliders/scripts; GPU shadow culling, HLOD, streaming, billboards and individual painting remain future work. Only static opaque/cutout model parts are supported, at most 128 parts per LOD and 65536 instances per component.

See [terrain and foliage documentation](../../docs/Terrain.md#packed-foliage-va-ikki-model-lod) for memory counters, limits and the reproducible GPU/CPU image oracle. Mesa software results establish correctness, not physical GPU frame-time performance.
