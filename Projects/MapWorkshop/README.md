# Map Workshop

A project for preparing a 3D map. Open it with `build-review/bin/ZaminEditor Projects/MapWorkshop`. The terrain tools: **Window → Map authoring (Terrain)**. [Guide](../../docs/Terrain.md).

The Map scene has relief, a dirt road, trees, a sun, a camera and a `Player Spawn` marker for a future hero. This scene is a sample of map authoring without game logic. Play/Simulate use the static terrain and the trees' collision.

The four terrain layers use the seamless albedo, tangent normal and ORM maps in `Assets/Textures/Terrain`. These are analytic test materials that depend on no external asset; `python3 Tools/samples/gen_terrain_materials.py` regenerates them with stable UUIDs. sRGB is off in the normal/ORM import. Change the layer parameters in the **Ground layers** window; when a source file is reloaded the terrain geometry is not rebuilt.
