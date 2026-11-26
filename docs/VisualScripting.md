# Visual Script: gameplay without code

Zamin Visual Script is the engine's built-in `.zgraph` node editor and a bounded execution system.
It works the same for 2D and 3D projects. A graph-based project needs neither a C++ compiler nor a game `.so` module.
The gameplay graph authoring and execution part works with typed functions, collections, enums, debugging and a compiler-free export. Full parity with Unreal Blueprints is not claimed.

## Getting started

1. Right-click an empty spot in the Content Browser → **New visual script (.zgraph)**. Or, in the **Window → Visual Script** window, enter the path `Scripts/NewGraph.zgraph` and press **New**.
2. Add a node through the search on the left. Set its values in the panel on the right.
3. Click an output pin, then an input pin. Only pins of the same type connect. White pins are the execution order; colored pins are data. Use **Sequence** for several actions.
4. **Save**. Select an entity in the hierarchy and press **Attach saved graph**. This is a scene change: save the scene too.
5. Press **Play**. Through **Instance overrides** the speed, color and other variables of the instances of one graph can be set separately; editing in the editor is closed while the game runs.

Middle mouse button — pan; wheel — zoom; **Fit graph** — fit every node into the view.
Dragging a node's header — position; right-clicking a pin — disconnect its wires.
Ctrl-click — select several nodes; Ctrl+A — all of them; Ctrl+C/V — copy/paste; Ctrl+D — duplicate; Delete — delete the selected ones. Dragging a group header moves the selected nodes together. There are also Copy/Paste/Duplicate buttons. The clipboard is kept for the current editor session; the inner wires and the needed signatures are copied with the nodes. A name clash gets a new name, and an invalid paste does not change the graph. A selected node can also be deleted with **Delete node**. The graph's own **Undo/Redo** history is limited to 64 states / 32 MB.
Another file does not open over an unsaved graph; you go on with **Save** or **Discard edits**.
The file is written atomically; on a write error the edits stay in the window.

## What works

- The lifecycle: Create, Enable, Start, Disable, Destroy, Update, LateUpdate.
- Collision/trigger, UI pointer/click/select/input, animation, audio finished, AI target/patrol, scene and language events.
- Number, Bool, String, Vec2/3/4, Entity values; strictly typed pins; graph variables and per-instance overrides.
- Branch, Sequence, For/Index/Completed, While, Break, Delay.
- Typed Function.Entry/Call/Return: parameters, several results, separate local variables for each call, nested/delayed/recursive calls (depth 32). Old Function/Call graphs still work.
- Enums: graph-local enums and enums imported from reflected C++ enum fields; Enum pins, variables, parameters, results and locals; Switch, Equal, ToNumber/ToText, FromNumber/FromText and ReadField/WriteField.Enum.
- Collapse to function: the selected nodes become a typed function with a call in their place.
- Bool/Number/String/Vec2/Vec3/Vec4/Entity arrays: Empty, Append, Set, Remove, Get, Length, Contains, Find, Slice, Concat and a snapshot ForEach.
- Event.Custom + EmitEvent: a named event inside a graph instance, with a Number/Text/Bool/Vector3/Other payload. SendMessage is used for a message to another entity.
- Math, logic and vector operations, joining text, turning a number into text (`FormatNumber` — 0..6 decimal places, `FormatTime` — `mm:ss` / `h:mm:ss`).
- Input axis/action, transform, finding/creating/destroying entities, the active state and tags.
- 2D/3D raycasts (Hit, Entity, Point, Normal, Distance); navigation destination/stop/arrival.
- Animator typed parameters/damping, states/transitions, 1D/2D blend weights, root motion and layer control ([the animation guide](Animation.md#73-2d-states-without-a-model-and-blueprint)).
- Scene loading (`LoadScene` with an optional `FadeOut`/`FadeIn`/`Color`: 0..10 s, FadeIn −1 — the same as FadeOut; the default values and old graphs without the pins switch at once as before), `QuitGame` (stops Play in the editor), `App.DemoMode` (`ZAMIN_DEMO` is set and not `0`); Number/Bool/String global GameState and its save/load operations.
- Settings bridges: `Audio.SetGroupVolume`/`Audio.GroupVolume` (the Music/SFX/UI groups, 0..2), `FadeAudio` (Target, 0..2, 0..30 s), `Localization.SetLanguage`/`.Language`/`.Text` (a key ≤ 128 bytes, Fallback when not found), `Display.SetFullscreen`/`Display.IsFullscreen` (without a window it is only saved), `Localization.SaveSettings`/`Display.SaveSettings` (write the language and display settings the player chose to SaveData — the Player and Editor Play load them next time, the command line wins), `Display.SetUIScale`/`Display.UIScale` (the player's UI size 0.5..2: every UI Canvas, the scenes loaded later too; [UI.md](UI.md#4-ui-canvas-fitting-the-screen)), `UIValue` (a slider/toggle/progress value: Found, Value, On) and `SetUIValue` (clamped to the range; `Notify` is off by default — the saved value is shown, not applied again), `PlaySpriteClip`, `ShakeCamera` (0..1, 0..5 s, .1..100 Hz).
- Vector/angle helpers: `SubtractVec2/3/4`, `DotVec*`, `DistanceVec*`, `LengthVec*`, `NormalizeVec*` (a zero vector stays zero), `Atan2`, `LerpAngle` (the short way, radians).
- 2D/3D velocity, force, impulse, 3D torque; prefabs, audio, animation, sprite color, UI text, messages.
- `ReadField.<Type>` / `WriteField.<Type>` work with another entity's registered C++ fields. Number is adapted to Float/Int; Int rejects fractions and overflow; the field's Range limit is kept.

`Update.Delta` is the time in seconds; rotation values are in radians. `Translate` and `SetPosition` are synchronized with physics bodies too.
SetScale changes the render transform; it does not rebuild the runtime collider geometry.
When `Target` is empty, **Self** is used. **None**, or a zero UUID returned by a lookup, is not turned into the entity itself: an invalid target gives a diagnostic.
An entity can be dragged from the hierarchy onto an Entity value.

## Writing functions and collections

**Typed functions → Function name → Create function** in Properties creates a typed entry/return pair.
In the function tree edit the type, name and initial value of the Inputs/Outputs/Locals; call it with **Add call node**.
Connect the Entry's parameter outputs to the body's operations and the result to the Return's input. Return works only in its own function.
If the execution path of a function with results ends without a Return, the VM stops with a clear error; a function without results may end freely.
Local.Get/Set touch only the value of the current call; recursion and parallel Delay tasks do not share them.
Renaming a function/graph variable updates the names of its nodes too. A parameter/result renamed with Enter keeps its wires and the
call literals. When a pin's type changes, the wires/literals that no longer fit are removed; Undo restores them. Pass event data to a function through an argument.

Every typed function is also in the **Node library** as `Functions / Call <name>` (search `call` or the name). Double-clicking a
Function.Call/Entry/Return node, or **Open function** in the function tree, opens the function: its entry, the nodes its execution reaches
and their data sources are framed and selected, the rest of the graph is dimmed, and the header shows **Function: <name>** with a
**Whole graph** button. Every signature, local, enum and collapse edit is one Undo/Redo step.

The compiler checks the call side: an argument that is not in the signature (`Argument Y is not in the signature of F`), a wire to a pin the
signature does not have, an unknown function, and wrong argument types. A function with results whose entry cannot reach any of its
Returns is a compile error. When only some execution paths end without a Return, the status line shows a yellow **warning** (hover: the
list, click: the function's entry); such a path stops the VM with a clear error if it runs. A loop counts through its Completed output,
a Sequence when one of its outputs returns on every path, a Branch/Switch when all of its outputs do.

**Collapse to function** (toolbar button or Ctrl+G) moves the selected nodes into a new typed function (the name in **Function name**;
taken names get a number) and puts a call where they were. Data wires entering the selection become parameters (evaluated when the call
starts), wires leaving it become results, the one execution wire in becomes the call's In and the one wire out its Then. The body moves
below the graph between a new entry and return. Events, entries/returns, locals, a selection without one execution entry or with more than
one exit, and a selected pure node that also feeds unselected nodes are refused with a message; a collapse that would add a compile error
does not change the graph.

## Enums

**Enums → Enum name → Create enum** adds a graph enum with one entry; add, rename (Enter) and remove entries and set their integer
values (Enter; unique, within ±2^53). Renaming an entry keeps the wires of the Switch outputs named after it; renaming an enum retypes
every node, variable and signature pin that uses it. An enum in use is not deleted. **Import from C++ field** copies the named options of
a loaded script class's `ZM_FIELD_ENUM` field (`Visual::EnumDefinition`, Source `Class.Field`) with their C++ names and values.

A graph variable, a function parameter/result and a local can have the Enum type with one of the graph's enums; literals show the
entries as a combo and store the entry's integer value. Enum nodes name their enum in the literal **Enum** input (Get/Set.Enum may
leave it empty: the variable's enum). `Enum.Switch` has one execution output per entry. `Enum.FromNumber`/`FromText` stop with an
error for a number/name that is no entry; a value outside the enum is rejected on literals, variable defaults, overrides, arguments,
returns and bridge results. `ReadField.Enum`/`WriteField.Enum` read and write a reflected C++ enum field; a written value must be one of
the field's options. Two pins connect only when they name the same enum. Enum arrays are not available yet.

An array literal has Add/Remove, pages of 16 elements and **Paste array JSON**. The index of `Array.Get/Set/Remove`
starts at 0; a fractional/negative/out-of-range index is an execution error. Slice Start/Count must be integers within range.
Find returns -1 when nothing is found. ForEach works on a copy of the input array: changing the global array inside the body
does not change the contents of the current loop. After returning from Body the next element runs, then Completed; Break ends the innermost loop.
Break cannot cross a typed function boundary into a caller's loop.
EmitEvent is delivered in the VM's next execution pass after the current chain; it does not build a recursive synchronous event stack.

## Execution rules and debugging

Data pins are evaluated when they are needed: a Get inside a loop sees the updated variable.
Sequence branches run in order; when a Delay inside a branch finishes, the next branch starts.
For Count must be an integer value 0..100000. A While condition is evaluated again on every iteration.
Break ends the inner loop and goes on to its Completed output.
A Delay keeps the data of the waiting event and the execution stack. When the entity/script is inactive, the wait stops too.
Starting one event again and again creates separate execution tasks; instead of starting a long Delay every frame, start it from Start or the relevant input event.

While playing, the graph window of the selected entity has **Enable breakpoints**, **Pause**, **Continue**, **Step**, the variables and a trace of the last 128 nodes. The call chain (Calls), the Function frame's arguments/locals and the size of the Retained data are shown.
A breakpoint stops the graph instance; the other entities and the game simulation go on. The game's general Pause control is separate.
In the Player breakpoints are off automatically; the editor's debug marker adds no forced stop to a build.

Execution is limited to 10000 instructions; instead of an endless loop, an error with the node number comes out.
The data depth is limited to 128, the execution stack to 1024, the simultaneous tasks to 128 and the depth of cross-graph calls to 32.
A graph holds up to 4096 nodes / 8192 wires / 256 variables / 64 functions / a 4 MB file; a text value up to 64 KB.
Each function has 32 inputs, 32 results and 32 locals; the defaults together 64 KiB, a typed call depth of 32.
An array is limited to 1024 elements and a computed payload of 64 KiB. An event to 32 keys and 64 KiB.
The values/task records the VM keeps are limited to 8 MiB in total; this is not a measure of the whole process RSS.
Unknown nodes, missing pins, type errors, a data input with two sources, duplicate IDs, data cycles and division by zero are rejected.
An error stops that graph instance and is printed to the console/debug panel.

## Saving, C++ and export

`.zgraph` files are format version 2: version 2 added the `Enums` section and the `Enum` key of enum-typed pins, variables and locals.
Version 1 files (all graphs written before) load unchanged, including legacy Function/Call nodes and typed functions; the window header
says `(format 1: saving writes format 2)` until the graph is saved. A version 1 file that contains an enum type, or a newer version, is rejected.

A graph runs in the engine's existing `NativeScriptComponent` lifecycle through the built-in `Zamin.VisualGraph` host.
That is why either C++ or a visual graph is attached to an entity's single script slot. On an entity with a C++ script the attach button says clearly that it replaces it.
To combine the two, use separate controller entities, messages and reflected fields.
C++ methods do not become nodes automatically: only the API in the catalogue and registered fields are available.
The enumerated Number pins of the gameplay nodes (AbilityDefine/ProjectileDefine/AreaDefine Kind,
EffectDefine Tick/Stacking, AreaDefine Falloff, UIBindHUD Kind, the equipment Slot) show up in the node inspector as a named list
(a combo, `Visual::PinChoices`) and the index is saved as a plain number; while playing the inspector also shows the
[combat state](GameplayCombat.md#editor-authoring) of the selected actor.
Curve and custom struct pins, reusable inline macros, graph inheritance, network RPC and bytecode/native code generation are separate development stages. Typed functions, collapse to function, enums and the seven array types above are part of this one.

Graph assets are listed in the registry, open in the Content Browser, and the asset inspector shows their diagnostics.
Asset paths inside a graph take part in the reference scan and in renames. Rather than hiding an asset path that overrides a graph variable
inside nested JSON text, keeping it in a graph node's Path value is recommended: the reference scanner does not parse nested override text.
After leaving Play, graph editing opens again. A saved graph is loaded on the next Play; there is no live replacement of the file during Play.
C++ hot reload keeps the graph variables, but, like other native timers, restarts the pending executions.

## Samples and checks

[VisualWorkshop](../Projects/VisualWorkshop/VisualWorkshop.zproject) has two scenes without a C++ module:

- `Visual2D`: WASD/arrow input → 2D velocity; touching a trigger changes the color for a while.
- `Visual3D`: two cubes with one graph spin at opposite speeds through different Speed overrides.
- `BlueprintGameplay.zgraph` in both scenes: an array → ForEach → a typed Award(Amount, Factor) → local/Delay/Return → Total → the Finished event → Print. Because of the Multiplier override the 2D total is 12 and the 3D total 18. The Finished event then calls `RankOf(Total)`, which returns the `Rank` enum through one of three Returns into the `FinalRank` variable and prints `BLUEPRINT_RANK rank=Silver` (2D) / `rank=Gold` (3D).

`ZaminTests --filter VisualGraph` runs the VM, the type/format/cycle checks, continuing delays/loops/functions, the budget, debugging, isolation,
the C++ field bridge, a real scene lifecycle and the null target protection.

The real ImGui mouse regression (adding nodes, a typed wire, rejection, save/undo/redo; then a typed function created in the UI, a parameter
added, called from the node library, wired, undone/redone, opened with a double-click, and the saved graph run in the Player):

```bash
ZAMIN_BUILD_DIR=build-review LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=2 \
  xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' python3 Tests/visual_graph_editor_test.py
```

The test needs `xdotool` and the ImageMagick `import` tool; it does not change the original sample.

The 2026-11-10 check: 173 node definitions; 26 VisualGraph tests (including a reference to an entity in another loaded scene),
682 unit tests overall, 12 render cases and 223 + 265 editor steps over two projects passed without errors.
In real X11/ImGui operations, adding nodes, save, undo/redo, a typed wire and rejecting a wrong wire were checked.
In the Player without UI, the 3D rotation and the chain 2D physics trigger → graph → GameState → disk save worked.
These tests ran in a Linux/Mesa software rendering environment; they are not a certification for other platforms or physical GPUs.

The 2026-11-26 check (typed functions, enums, collapse): `ZaminTests --filter Blueprint` covers return-path errors/warnings, call
arity diagnostics, format 2 round trips and the version 1 migration, enum Switch/conversions/type checks/parameters/locals/renames/paste,
recursion with enum arguments, collapse equivalence and refusals; `VisualGraphReadsAndWritesReflectedCppEnums` imports and drives a real
reflected C++ enum field.

## World navigation and traversal

- **SetWorldDestination**: switches a NavAgent to the world atlas mode; Position is a world Vec3,
  Success says a Complete/Partial route was found. False for Failed. Target defaults to Self.
- **NavTraversalState** (pure): Pending, Link/Revision/Ticket (exact Strings), Entry/Exit (Vec3), Status.
  The UUIDs are not Numbers: values above 2^53 are not rounded either.
- **Event.NavTraversalRequested**: once when the Self agent reaches the entry, for each new ticket;
  a Link, Revision, Ticket, Entry, Exit payload. When the VM is paused the event follows the existing queue rules.
- **CompleteNavTraversal**: Link/Revision/Ticket and Target from the event; a Success bool. A map change,
  a cancel, a blocked controller exit or a wrong token gives false. Then Branch chooses retry/cancel.
- **CancelNavTraversal**: cancels the pending request and stops the agent; a Success bool.

A Delay task keeps the event tokens with it. Instead of taking a new token from NavTraversalState after the delay
and applying it to the old action, connect the event pins to the complete. This is an explicit teleport handshake;
a character's walk depends on physics, and it does not move while the controller exit is blocked. Rigidbody traversal completion
is not applied yet. Sample: WorldWorkshop2D/3D `Scripts/WorldTraversal.zgraph`, with the B key.

Under the async world policy, SetWorldDestination true means the worker request was accepted; the path status may be None.
The pure bool **NavQueryPending** is true until an accepted query is ready; the portal Pending is separate.
Under backpressure the destination intent is retried and StopMoving cancels it. The sample graph updates the Searching
variable with this node. The result is applied after the revision/goal/start/entity generation checks.

`NavSteeringState` is a pure Navigation node. `TargetEntity` (empty: itself), outputs `Preferred`,
`Velocity` (Vector3), `Neighbors` (Number), `Blocked`, `Limited` (Bool). Without a NavAgent it
returns zero/false. Velocity is the movement command after the world clearance; when the physical actual velocity
is needed, use a body/controller query. In the `CrowdMonitor.zgraph` sample Event.Update
updates three variables; the indicators belong to the last navigation update.

`NavRecoveryState`: a pure node, an optional TargetEntity, outputs `Phase` (String), `Retries`,
`StalledSeconds`, `Cooldown` (Number), `Exhausted` (Bool). No agent: Idle/0/false.
`RetryNavigation`: exec, an optional TargetEntity, Then/Success. It resets the watchdog of a world agent with an active
destination, asks for a re-path and cancels the pending traversal token; the new
query starts at the owner's next nav update. `RecoveryMonitor.zgraph`, with 8 nodes / 9 links, shows reading the
state and a retry through Message.

The typed functions/collections model and the VM tests: `ZaminTests --filter Blueprint`.
Real 2D/3D Player/editor screenshots and the module-free export check:

```bash
python3 Tests/blueprint_workflow_test.py --build build-review --exports
```

The export check verifies the real score logs, the PNGs, the export report and the relocated launcher version in both scenes.
The scene lifecycle test also checks the Total value and that the tasks finished from inside the VM.
