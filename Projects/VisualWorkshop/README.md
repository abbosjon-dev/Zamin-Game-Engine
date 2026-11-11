# Visual Workshop

Open `VisualWorkshop.zproject` in Zamin Editor. No game module or C++ compiler is needed.

- Play `Scenes/Visual2D.zscene`: WASD / arrow keys move the purple square; green trigger pads change its color briefly.
- Open `Scenes/Visual3D.zscene`: the two cubes share `Scripts/Spin3D.zgraph` with different Speed overrides.
- Both scenes also run `Scripts/BlueprintGameplay.zgraph`: typed Award parameters/results, invocation-local storage across Delay, snapshot ForEach over a typed array, and a named completion event. The console prints total=12 in 2D and total=18 in 3D from per-instance Multiplier overrides.
- Double-click any `.zgraph` asset, or use Window → Visual Script, to inspect and edit the logic.
- Save the graph before attaching it to an entity. Save scene changes separately.

See [Visual Script guide](../../docs/VisualScripting.md) for controls, execution rules and current limitations.

- `Animator gameplay actor` runs `AnimatorGameplay.zgraph` in both scenes: 2D blend parameters, a trigger-driven Action, explicit state resume, damping and a checked completion log. `AnimatorDisplay2D.zgraph` colors the 2D display from that actor's state. The 3D actor uses the repository's procedural Tentacle model as `Models/BlendActor.gltf`.
- Run `python3 Tests/animator_workflow_test.py --build build-review --exports` for the real Player/editor/export acceptance. Expected final blend weight 0 is `1 - exp(-1)`; the existing Blueprint sample totals remain 12/18.
