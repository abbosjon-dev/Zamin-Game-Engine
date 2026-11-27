# Visual Script: gameplay without code

Zamin Visual Script is the engine's built-in `.zgraph` node editor and a bounded execution system.
It works the same for 2D and 3D projects. A graph-based project needs neither a C++ compiler nor a game `.so` module.
The gameplay graph authoring and execution part works with typed functions, collections, enums, structs, debugging and a compiler-free export. Full parity with Unreal Blueprints is not claimed.

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
- Structs: graph-local structs of typed fields, structs inside structs; Struct/ArrayStruct pins, variables, parameters, results and locals; Make, Break, GetField, SetField and the array nodes.
- Inline macros: graph-local subgraphs with execution and data pins, expanded in place of each use; they may hold Delay and loops.
- Bool/Number/String/Vec2/Vec3/Vec4/Entity/Enum arrays: Empty, Append, Set, Remove, Get, Length, Contains, Find, Slice, Concat and a snapshot ForEach.
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
the field's options. Two pins connect only when they name the same enum.

An enum array (`ArrayEnum`) holds entry values of one enum. Variables, parameters, results, locals and struct fields can have
this type; `Constant/Get/Set.ArrayEnum`, `Local.Get/Set.ArrayEnum`, `Array.*.Enum` and `ForEach.Enum` name the enum in their
literal **Enum** input (variable and local Get/Set may leave it empty), and every element is checked against the enum like a
single Enum value. The literal editor shows each element as a combo of the entries.

An array literal has Add/Remove, pages of 16 elements and **Paste array JSON**. The index of `Array.Get/Set/Remove`
starts at 0; a fractional/negative/out-of-range index is an execution error. Slice Start/Count must be integers within range.
Find returns -1 when nothing is found. ForEach works on a copy of the input array: changing the global array inside the body
does not change the contents of the current loop. After returning from Body the next element runs, then Completed; Break ends the innermost loop.
Break cannot cross a typed function boundary into a caller's loop.
EmitEvent is delivered in the VM's next execution pass after the current chain; it does not build a recursive synchronous event stack.

## Structs

**Structs → Struct name → Create struct** adds a graph struct with one Number field. In its tree add, remove (a struct keeps at
least one field), rename (Enter) and retype fields and set their defaults. A field can be any value type: Bool, Number, String,
Vec2/3/4, Entity, an enum of the graph, an array, another struct of the graph (`Struct`) or an array of one (`ArrayStruct`). A
struct cannot contain itself, directly or through other structs: such a field type is refused in the editor and is a compile
error (`Struct contains itself: A -> B -> A`). A struct may nest to 7 levels. A graph holds up to 64 structs of 1..32 fields;
the field defaults of one struct are limited to 64 KiB, nested values included. Renaming a field keeps the wires of the Make/Break pins named
after it, moves the GetField/SetField field literals and renames the field in every stored value; renaming a struct retypes every
node, variable and signature pin that uses it. When a field is added, removed or retyped, every stored value of the struct keeps
its fitting fields and takes the field defaults for the others, and wires/literals that no longer fit are removed (Undo restores
them). A struct in use is not deleted. Every struct edit is one Undo/Redo step.

A nested field works like any other: Make takes the inner struct value on the field's pin, Break and GetField give it, SetField
replaces it, and the inner value is read with another Break/GetField of the inner struct. A field rename or field edit of the
inner struct reaches every stored outer value (variable defaults, field defaults, literals); renaming the inner struct retypes
the outer struct's fields. The literal editor opens a nested value as a tree and edits it field by field.

A graph variable, a function parameter/result and a local can have the Struct type (one value) or the ArrayStruct type (an array of
one struct's values). Their literals are edited field by field. Struct nodes name their struct in the literal **Struct** input
(Get/Set and locals may leave it empty: the variable's or local's struct) and the field nodes name the field in the literal **Field**
input; both are combos in the node inspector and must be literals. The node library also lists **Structs / Make <name>** and
**Break <name>** for each struct.

- `Struct.Make`: one input per field → the struct value.
- `Struct.Break`: the struct value → one output per field.
- `Struct.GetField`: the value of the named field. `Struct.SetField`: a copy of the struct with the named field replaced
  (`FieldValue`); like `Array.Set` it does not change a variable until a Set stores the copy.
- `Get/Set.Struct`, `Local.Get/Set.Struct`, and for struct arrays `Constant/Get/Set.ArrayStruct`, `Array.*.Struct` and `ForEach.Struct`.

Two pins connect only when they name the same struct. A struct value must have exactly the struct's fields, each of its field's
type; this is checked on literals, variable defaults, instance overrides (an override of another shape is ignored), arguments,
returns and every node result. A struct value is limited to 32 fields and 64 KiB, a struct array to 1024 elements and 64 KiB: a
struct array that would grow past it stops the graph with an error at the node that builds it.

## Inline macros

**Macros → Macro name → Create macro** adds a graph-local macro with an `In` execution input and a `Then` execution output,
passed straight through between its **Macro.Entry** and **Macro.Exit** nodes. In its tree add, remove, rename (Enter) and retype
the **Inputs** and **Outputs**: a pin is either `Exec` or a data pin of any value type (with a default). **Open macro** (or a
double-click on a use) shows the macro's body on the canvas; the header says **Macro: <name>** and **Whole graph** goes back. Nodes
added or pasted there belong to the macro. The node library lists **Macros / <name>** for each macro; **Add macro node** puts
one into the open view, and a use's **Name** is a combo of the graph's macros. **Rename macro** renames every use and the body;
**Delete macro and its nodes** removes the body and the uses with their wires. Every macro edit is one Undo/Redo step. A macro's
entry and exit cannot be deleted on their own.

A macro is not a function call: each use (`Macro.Instance`) is replaced at compile time by its own copy of the body, with node
IDs unique to that use. So, unlike a typed function, a macro may hold latent and execution flow (Delay, loops, Sequence) and its
execution outputs continue the caller's chain. A data input without a wire passes the use's literal (or the pin default); a data
output the body leaves unwired gives the exit's literal. A macro may use other macros, but not itself (`Macro M uses itself`),
and holds no events or function entries. A macro without execution pins is a pure data node.

The body is checked on its own (findings name the body node and start with `In macro M:`), and the graph is checked with
every macro expanded: a finding in a copy, and a runtime error in one, names the use node, with the body node (in the
diagnostics list in parentheses, in the error as `In macro M (node N)`). A graph holds up to 64 macros of 32 inputs and 32
outputs; the expanded graph keeps the 4096-node limit. Pasting a use needs the same macro in the target graph; nodes inside a
macro cannot be collapsed into a function.

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

`.zgraph` files are format version 4: version 2 added the `Enums` section and the `Enum` key of enum-typed pins, variables and locals;
version 3 added the `Structs` section (each struct's `Fields`: name, type, default and, for an enum field, `Enum`) and the `Struct` key
of struct-typed pins, variables and locals; version 4 added the `Macros` section (each macro's `Inputs`/`Outputs`, `Exec` pins
included), the `Macro` key of a macro's body nodes, the `ArrayEnum` type and struct fields of struct type. Version 1-3 files load
unchanged, including legacy Function/Call nodes, typed functions, enums and structs; the window header says `(format 3: saving writes
format 4)` until the graph is saved. An enum type in a version 1 file, a struct type in a version 1 or 2 file, macros, enum arrays or
nested struct fields in a version 1-3 file, or a newer version is rejected.

A graph runs in the engine's existing `NativeScriptComponent` lifecycle through the built-in `Zamin.VisualGraph` host.
That is why either C++ or a visual graph is attached to an entity's single script slot. On an entity with a C++ script the attach button says clearly that it replaces it.
To combine the two, use separate controller entities, messages and reflected fields.
C++ methods do not become nodes automatically: only the API in the catalogue and registered fields are available.
The enumerated Number pins of the gameplay nodes (AbilityDefine/ProjectileDefine/AreaDefine Kind,
EffectDefine Tick/Stacking, AreaDefine Falloff, UIBindHUD Kind, the equipment Slot) show up in the node inspector as a named list
(a combo, `Visual::PinChoices`) and the index is saved as a plain number; while playing the inspector also shows the
[combat state](GameplayCombat.md#editor-authoring) of the selected actor.
Curve pins, graph inheritance, network RPC and bytecode/native code generation are separate development stages. Typed functions, collapse to function, enums (with enum arrays), graph structs (nested, with struct arrays), inline macros and the array types above are part of this one.

Graph assets are listed in the registry, open in the Content Browser, and the asset inspector shows their diagnostics.
Asset paths inside a graph take part in the reference scan and in renames. Rather than hiding an asset path that overrides a graph variable
inside nested JSON text, keeping it in a graph node's Path value is recommended: the reference scanner does not parse nested override text.
After leaving Play, graph editing opens again. A saved graph is loaded on the next Play; there is no live replacement of the file during Play.
C++ hot reload keeps the graph variables, but, like other native timers, restarts the pending executions.

## Samples and checks

[VisualWorkshop](../Projects/VisualWorkshop/VisualWorkshop.zproject) has two scenes without a C++ module:

- `Visual2D`: WASD/arrow input → 2D velocity; touching a trigger changes the color for a while.
- `Visual3D`: two cubes with one graph spin at opposite speeds through different Speed overrides.
- `BlueprintGameplay.zgraph` in both scenes: an array → ForEach → a typed Award(Amount, Factor) → local/Delay/Return → Total → the Finished event → Print. Because of the Multiplier override the 2D total is 12 and the 3D total 18. The Finished event then calls `RankOf(Total)`, which returns the `Rank` enum through one of three Returns into the `FinalRank` variable and prints `BLUEPRINT_RANK rank=Silver` (2D) / `rank=Gold` (3D). Both values then go into `Summary`, a variable of the graph struct `Result {Total, Rank}` made with Struct.Make, which Struct.Break and Struct.GetField read back: `BLUEPRINT_SUMMARY total=12 rank=Silver` (2D) / `total=18 rank=Gold` (3D). Last, `Report`, a struct that nests that `Result` and keeps the rank history in an enum array (`Ranks`: Bronze, then the final rank), is read with nested GetFields and passed to two uses of the `Announce` macro (a Delay, then a Print): `BLUEPRINT_REPORT total=12` and `BLUEPRINT_REPORT ranks=2` (2D; `total=18` in 3D).

`ZaminTests --filter VisualGraph` runs the VM, the type/format/cycle checks, continuing delays/loops/functions, the budget, debugging, isolation,
the C++ field bridge, a real scene lifecycle and the null target protection.

The real ImGui mouse regression (adding nodes, a typed wire, rejection, save/undo/redo; then a typed function created in the UI, a parameter
added, called from the node library, wired, undone/redone, opened with a double-click, and the saved graph run in the Player; then a struct
created in the UI with an added and a renamed field, Make/GetField from the node library, wired, the wired field renamed with the wires kept,
undone/redone, and that graph run in the Player; then a struct field given a struct type with its nested default edited, an
`ArrayEnum` variable, and a macro created with an added and renamed output (undo/redo), opened, its body edited from the node library,
used from the node library and wired, renamed with its use, opened by a double-click and run in the Player):

```bash
ZAMIN_BUILD_DIR=build-review LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=2 \
  xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' python3 Tests/visual_graph_editor_test.py
```

The test needs `xdotool` and the ImageMagick `import` tool; it does not change the original sample. It clicks the properties column and
the canvas through `ZAMIN_EDITOR_VISUAL_LAYOUT=<file.json>`: the editor writes the screen rectangles of the column's widgets and of the
canvas node headers/pins, the header buttons (`header/...`) and an open type list (`popup/<type>`) there each frame, and the test fails if a widget in the column is clipped at its right edge.

The 2026-11-10 check: 173 node definitions; 26 VisualGraph tests (including a reference to an entity in another loaded scene),
682 unit tests overall, 12 render cases and 223 + 265 editor steps over two projects passed without errors.
In real X11/ImGui operations, adding nodes, save, undo/redo, a typed wire and rejecting a wrong wire were checked.
In the Player without UI, the 3D rotation and the chain 2D physics trigger → graph → GameState → disk save worked.
These tests ran in a Linux/Mesa software rendering environment; they are not a certification for other platforms or physical GPUs.

The 2026-11-26 check (typed functions, enums, collapse): `ZaminTests --filter Blueprint` covers return-path errors/warnings, call
arity diagnostics, format 2 round trips and the version 1 migration, enum Switch/conversions/type checks/parameters/locals/renames/paste,
recursion with enum arguments, collapse equivalence and refusals; `VisualGraphReadsAndWritesReflectedCppEnums` imports and drives a real
reflected C++ enum field.

The 2026-11-27 check (nested structs, enum arrays, macros): `ZaminTests --filter Blueprint` covers nested Make/Break/GetField/
SetField, cycle/depth/64 KiB checks, nested renames and conformed values, enum arrays through the array nodes, ForEach, Switch,
parameters and locals, macro expansion with latent flow, nested/pure/pass-through macros, diagnostics and runtime errors on the
use, macro and pin renames, paste, and the format 4 migration of version 1-3 files.

The 2026-11-26 struct check: `ZaminTests --filter BlueprintStruct` (and the format test) covers Make/Break/GetField/SetField, struct
parameters/results/locals, overrides, static and runtime type errors, the 64 KiB struct array limit, format 3 round trips with the
version 1/2 migration, paste, field and struct renames that keep wires and values, field add/remove/retype with conformed values,
struct arrays through the array nodes and ForEach, and collapse over a struct wire.

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
