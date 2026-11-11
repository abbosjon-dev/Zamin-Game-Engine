# WorldWorkshop3D

16 authored cells, a persistent camera/physics anchor and a streaming manifest.

Open the project, press Play and use WASD to move the camera. Open **Oyna → World Streaming** for state, policy and retry controls. The orange physics probe shares the loaded ground’s physics world. Set `ZAMIN_WORLD_AUTOTOUR=1` for the deterministic camera route (120 frames per stop, including an empty region and a return).

Geometry and colours are authored primitives; no external assets are required. The manifest declares memory reservations; these are not measured RSS/VRAM. The runtime uses soft frame budgets.
