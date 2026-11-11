# Visual Workshop

Open `VisualWorkshop.zproject` in Zamin Editor. No game module or C++ compiler is needed.

- Play `Scenes/Visual2D.zscene`: WASD / arrow keys move the purple square; green trigger pads change its color briefly.
- Open `Scenes/Visual3D.zscene`: the two cubes share `Scripts/Spin3D.zgraph` with different Speed overrides.
- Both scenes also run `Scripts/BlueprintGameplay.zgraph`: typed Award parameters/results, invocation-local storage across Delay, snapshot ForEach over a typed array, and a named completion event. The console prints total=12 in 2D and total=18 in 3D from per-instance Multiplier overrides.
- Double-click any `.zgraph` asset, or use Window → Visual Script, to inspect and edit the logic.
- Save the graph before attaching it to an entity. Save scene changes separately.

See [Visual Script guide](../../docs/VisualScripting.md) for controls, execution rules and current limitations.
