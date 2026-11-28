# Visual Script: gameplay without code

Zamin Visual Script is the engine's built-in `.zgraph` node editor and a bounded execution system.
It works the same for 2D and 3D projects. A graph-based project needs neither a C++ compiler nor a game `.so` module; when a
project has one, graphs can call its reflected C++ methods ([Native C++ methods](#native-c-methods)).
The gameplay graph authoring and execution part works with typed functions, collections, enums, structs, macros, curves, graph inheritance, native method calls, debugging, a per-node profiler and an export that cooks graphs to bytecode. Full parity with Unreal Blueprints is not claimed.

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
- Curves: float curves of keys (time, value, constant/linear/cubic interpolation, tangents) for variables, parameters, locals, struct fields and literals; Curve.Evaluate, KeyCount, Length and AddKey; an inline curve editor.
- Graph inheritance: a graph names a parent `.zgraph` and inherits its variables, functions, enums, structs, macros and event handlers; it overrides functions and events (Parent.Call / Parent.Event run the parent's).
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
- Native C++ methods: `Native.Call` (execution) and `Native.Pure` (data) call the methods and free functions a game module registers with `ZM_METHOD` / `ZM_FUNCTION` — typed parameters and results, C++ enums and structs included ([Native C++ methods](#native-c-methods)).

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

## Curves

A `Curve` value is a float curve: up to 256 keys, each with a **Time**, a **Value**, an interpolation **Mode** to the next key and
two tangents. `Constant` holds the key's value until the next key, `Linear` goes straight to it, `Cubic` is a Hermite spline that
leaves the key with its **Out** tangent and reaches the next key with that key's **In** tangent (tangents are slopes: value per time
unit). Before the first key and after the last the curve holds the end key's value; an empty curve is 0. Times are finite and
strictly increasing; a value of another shape (unsorted or equal times, an unknown mode, a missing or extra key field) is rejected on
literals, defaults, instance overrides, arguments and results.

Graph variables, function parameters/results, locals, macro pins and struct fields can have the `Curve` type; a curve pin connects
only to a curve pin. The nodes:

- `Constant.Curve`, `Get.Curve`/`Set.Curve` and `Local.Get/Set.Curve`.
- `Curve.Evaluate` (Curve, Time → Value), `Curve.KeyCount` (→ the number of keys) and `Curve.Length` (→ `Value` = End − Start,
  `Start`, `End`: the first and last key times; 0 for an empty curve).
- `Curve.AddKey` (Curve, Time, Value, Mode, InTangent, OutTangent → a new curve): a key at the same time is replaced; Mode is a
  combo (0 Constant, 1 Linear, 2 Cubic; any other number stops the graph with an error).

**The curve editor.** Wherever a curve value is edited (a variable default, a parameter/local/field default, a node literal) the
properties column draws a small graph preview of the curve with its keys as dots; drag a key to move it (between its neighbours'
times; one undo step when the button is released). Under **Keys** each key has its **Time** (committed with Enter; a time another key
has is refused), **Value**, **Interpolation** (Constant/Linear/Cubic) and, around a cubic segment, its **In tangent** / **Out
tangent**, and **Delete key**; **Add key** appends a key one time unit after the last one. Every edit is one Undo/Redo step.

## Graph inheritance

**Inheritance → Parent path → Set parent** makes the open graph a child of another graph (an Assets-relative `.zgraph` path, saved
as the child's `Parent`; **Clear parent** removes it; both are one Undo/Redo step). The child then has the parent's variables, typed
functions, enums, structs, macros and event handlers as well as its own: they are listed in their sections marked
**[inherited]** and are read-only there (edit them in the parent); instance overrides, calls, Get/Set, Make/Break and macro uses
work with them as with the child's own. The window header shows the parent chain. A parent may itself have a parent (up to 8 levels).

- **Override a function.** Open the inherited function in **Typed functions** and press **Override function**: the function
  becomes the child's own with the parent's signature (read-only: an override keeps it) and a body of Function.Entry →
  **Parent.Call** → Function.Return with every parameter and result wired through, which runs the parent's version unchanged.
  Edit the body like any function (**Add call parent node** adds another Parent.Call). Every call of the function runs the
  override, the parent's own calls included (the parent's Start calling `Bonus` runs the child's `Bonus`). **Remove override**
  deletes the child's version and its Parent.Call nodes, and the parent's applies again. Locals of an override are its own.
- **Override an event.** **Inheritance → Parent events** lists the events the parent handles; **Override event** adds the
  child's own handler with a **Parent.Event** node that runs the parent's handlers of that event (with the same event data) and
  then continues. A child that handles an event the parent handles replaces the parent's handlers: without a Parent.Event they
  do not run. Event keys are the event names (`Start`, `Update` ...) and `Custom.<Name>` for Event.Custom.
- **Add members.** The child's own variables, functions, enums, structs, macros and events are added as usual; a name the
  parent already uses (other than a function override) is a compile error (`Variable X is already defined in parent P`).

The VM runs the child with the parent chain resolved into one graph: the parent's nodes take node IDs above the child's, and an
overridden function's parent version is called `<Parent>::<Name>` (e.g. `BlueprintBase::Bonus`) in the call stack. Diagnostics
and runtime errors in a parent's node name the parent file and the node's ID there (`In parent Scripts/Base.zgraph (node 4): ...`).
A missing or unreadable parent (`Parent graph not found: ...`), an inheritance cycle (`Graph inheritance cycle: A -> B -> A`, a graph
that is its own parent included), an override whose signature differs from the parent's, a Parent.Call that names no override, a
Parent.Event of an event the child does not override (or inside a macro) and a Parent node in a graph without a parent are compile
errors: such a child does not run.

A child is loaded with its parent chain every time (opening it in the editor, Undo/Redo, Play and the Player), so edits saved in a
parent reach its children. When a parent's variable, function, enum, struct or macro is renamed, the parent's file keeps the rename
(`Renames`, the last 64): its children follow it when they load (their nodes, types and overrides take the new name). Renames of
fields, pins and entries are not followed. The parent path is an asset reference: the export takes a child's parent with it and
asset renames update it.

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

The **Visual Script profiler** measures the VM per node and per typed function while the game plays in the editor
([Profiling large graphs](#profiling-large-graphs)).

Execution is limited to 10000 instructions; instead of an endless loop, an error with the node number comes out.
The data depth is limited to 128, the execution stack to 1024, the simultaneous tasks to 128 and the depth of cross-graph calls to 32.
A graph holds up to 4096 nodes / 8192 wires / 256 variables / 64 functions / a 4 MB file; a text value up to 64 KB.
Each function has 32 inputs, 32 results and 32 locals; the defaults together 64 KiB, a typed call depth of 32.
An array is limited to 1024 elements and a computed payload of 64 KiB. An event to 32 keys and 64 KiB.
The values/task records the VM keeps are limited to 8 MiB in total; this is not a measure of the whole process RSS.
Unknown nodes, missing pins, type errors, a data input with two sources, duplicate IDs, data cycles and division by zero are rejected.
An error stops that graph instance and is printed to the console/debug panel.

## Saving, C++ and export

`.zgraph` files are format version 7: version 2 added the `Enums` section and the `Enum` key of enum-typed pins, variables and locals;
version 3 added the `Structs` section (each struct's `Fields`: name, type, default and, for an enum field, `Enum`) and the `Struct` key
of struct-typed pins, variables and locals; version 4 added the `Macros` section (each macro's `Inputs`/`Outputs`, `Exec` pins
included), the `Macro` key of a macro's body nodes, the `ArrayEnum` type and struct fields of struct type; version 5 added the `Curve`
type (its literals: `{"Keys": [{"Time", "Value", "Mode", "In", "Out"}, ...]}`), the `Parent` path, the `Renames` log and the
Parent.Call/Parent.Event nodes; version 6 added the `Natives` section (the signatures of the C++ methods the graph's
`Native.Call`/`Native.Pure` nodes call: `Static`, `Pure`, `Inputs`, `Outputs`, `Tooltip`; only the ones in use are saved); version 7
added networking (the `Rpcs` section - each RPC's `Kind`, `Reliable`, `AnyClient`, `Params` -, the `Replicated` key of a variable and
the [networking nodes](#networking)). A child's file holds only its own members. Version 1-6 files load unchanged, including legacy
Function/Call nodes, typed functions, enums, structs, macros, curves, parents and native calls; the window header says `(format 6:
saving writes format 7)` until the graph is saved. An enum
type in a version 1 file, a struct type in a version 1 or 2 file, macros, enum arrays or nested struct fields in a version 1-3 file,
curves, a parent, renames or Parent nodes in a version 1-4 file, native method calls in a version 1-5 file, RPCs, replicated
variables or networking nodes in a version 1-6 file, or a newer version is rejected.

A graph runs in the engine's existing `NativeScriptComponent` lifecycle through the built-in `Zamin.VisualGraph` host.
That is why either C++ or a visual graph is attached to an entity's single script slot. On an entity with a C++ script the attach button says clearly that it replaces it.
To combine the two, use separate controller entities, messages, reflected fields and reflected methods (`ZM_METHOD`: a graph calls
them on another entity's C++ script).
The enumerated Number pins of the gameplay nodes (AbilityDefine/ProjectileDefine/AreaDefine Kind,
EffectDefine Tick/Stacking, AreaDefine Falloff, UIBindHUD Kind, the equipment Slot) show up in the node inspector as a named list
(a combo, `Visual::PinChoices`) and the index is saved as a plain number; while playing the inspector also shows the
[combat state](GameplayCombat.md#editor-authoring) of the selected actor.
Native code generation from graphs is a separate development stage; networking (graph RPCs, replicated variables, the
`Net.*` nodes) is in [Networking](#networking). Typed functions, collapse to function, enums (with enum arrays), graph structs (nested, with struct arrays),
inline macros, curves, graph inheritance, native method calls, the bytecode cook, the VM profiler and the array types above are part
of this one.

Graph assets are listed in the registry, open in the Content Browser, and the asset inspector shows their diagnostics.
Asset paths inside a graph take part in the reference scan and in renames. Rather than hiding an asset path that overrides a graph variable
inside nested JSON text, keeping it in a graph node's Path value is recommended: the reference scanner does not parse nested override text.
After leaving Play, graph editing opens again. A saved graph is loaded on the next Play; there is no live replacement of the file during Play.
C++ hot reload keeps the graph variables, but, like other native timers, restarts the pending executions.

## Native C++ methods

A game module registers methods of its script classes with `ZM_METHOD` and free functions with `ZM_FUNCTION` (the field macros'
companions, [Scripting.md](Scripting.md#4b-methods-for-visual-script-zm_method)); the node library's **C++** section lists every one
of the loaded module (search "c++", the class or the method). A method that returns a value and changes nothing can be registered
pure (`ZM_PURE_METHOD`, `.Pure()`): a **Native.Pure** data node, evaluated whenever its result is read. Otherwise it is a
**Native.Call** execution node (In/Then). An instance method has a **Target** entity input: the method runs on that entity's live
instance of the class (as for `ReadField`, an empty Target is Self, which has the graph's own script, so a method needs a wired or
set Target); a static member or a free function has none.

The parameters and the result take the pin types: `float`/`double`/`int`/`int64_t` → Number (an integer parameter refuses a fraction
or a value outside its range), `bool`, `std::string`, `glm::vec2/3/4`, `EntityRef` → Entity, a C++ enum registered with
`ZM_SCRIPT_ENUM` → Enum, a struct registered with `ZM_SCRIPT_STRUCT` → Struct, `std::vector` of any of these → the array types. When a
node is added, the C++ enums and structs it uses become graph enums/structs of the same names (an enum keeps its C++ names and values,
`Source: C++ enum <Name>`); a graph type of that name that differs is refused. A C++ exception from a method stops the graph instance
with `Native method <Class.Method>: <what>`, like any other node error.

The graph keeps the signature it was built with (the `Natives` section), so it loads, validates, keeps its wires and runs its other
nodes without the module. The host binds every call to the live method of the current registry generation: after a module load,
unload or hot reload the binding is looked up again. When a method is not registered (no module, or removed) or its C++ signature
(kind, parameter names and types, result) or one of its enums/structs no longer matches, its nodes are drawn red with the reason in
a tooltip and on the status line (`n C++ methods not bound`), the Player and the editor log
`Visual graph '<file>': native method <key> cannot run: <reason>` once at load, and the node stops the graph only when it runs. **Refresh
from C++** in the node's properties takes the live signature (one undo step): new pins appear, the literals and wires that still fit
stay, the others go. The signature and its binding state are in the node's properties. The cooked export keeps the signatures; a
method that would not bind with the module the editor has loaded is a warning in the export report.

VisualWorkshop's `Source/WorkshopNative.cpp` registers `WorkshopStats.Award` (an execution method that counts and returns the number
of awards), `WorkshopStats.Score` and `WorkshopStats.MedalFor` (pure; `Medal` is a `ZM_SCRIPT_ENUM`) and the free function
`WorkshopMath.Hypot`; `Scripts/NativeMethods.zgraph` calls them on the `Workshop stats` entity and prints
`BLUEPRINT_NATIVE score=36 medal=Silver hypot=5.00 awards=1` (2D; `score=54 medal=Gold` in 3D through the `Base` override).

## Cooked graphs (bytecode)

The export compiles every `.zgraph` of the game (Export settings `CookGraphs`, on by default) to a compact binary program and writes
it over the exported `.zgraph` (`ExportCook::CookGraphs`, `Scripting/Visual/VisualGraphBytecode.h`). The program is the graph as the
VM runs it: validated with exactly the runtime's checks (`Machine::Prepare` — a graph the Player would reject is not cooked: it stays
JSON, the export report lists it as `rejected` with the first error, and its parents stay JSON too), the parent chain resolved, macros
expanded, one pin description per distinct node shape, the node literals, the wires as indices, the native method signatures, the
network RPCs and the replicated flags of variables (bytecode format 2) and the macro/parent origins that error messages name. Positions and breakpoints are dropped.

The format: `ZGBC`, a format version, the hash of the engine's node catalogue (a program of another engine version is refused: export
again), an FNV-1a hash of the payload and its size, then a string table and the sections (tagged values: null, bool, integers,
doubles, strings, arrays, objects). Reading checks every count, index and wire (a damaged or truncated file is refused with a reason);
the Player's host loads a cooked file without parsing JSON, validating, resolving or expanding, and the editor keeps reading the JSON
sources. The cook cache (`<project>/Build/CookCache/<hash>.zgbc`) is keyed by the content hash of the graph, its parent chain and the
catalogue: an unchanged graph is not compiled again. `export-report.txt/json` and the editor's Export report window list every graph
(cooked / rejected / kept, source and cooked bytes, the program's nodes, cache hits). Native code generation from graphs is not done.

VisualWorkshop's eight graphs: 47 KB of JSON → 10 KB of bytecode (4.6x smaller). The large generated graphs of `ZaminBench --graphs`
(Release, about 4000 nodes): 0.7-1.0 MB → 59-74 KB (10-17x smaller), and loading the VM from the cooked file takes 1.9-2.9 ms against
26-76 ms from the JSON source ([Benchmarks](Benchmarks.md#large-visual-script-graphs-zaminbench---graphs)).

## Networking

Graphs take part in a networked game ([Networking.md](Networking.md)) with format 7:

- **Graph RPCs**: the **Network RPCs** section of the properties column defines them - a name, the kind (**Server**: a client calls
  it, the server runs it, for the graph's entity the client owns or for any entity with **Any client**; **Client**: the server calls
  it, the entity owner's client runs it; **Multicast**: the server calls it, the server and every client the entity is relevant to
  run it), **Reliable** and typed parameters. `Event.Rpc` (Name) handles one - its outputs are `Sender` (the calling client, 0 for the
  server) and the parameters -, `Net.CallServerRpc` / `Net.CallClientRpc` / `Net.CallMulticastRpc` (Name) call one - their inputs are
  the parameters. The node library lists **Network RPC / Call ... RPC <name>** and **On RPC <name>** for every RPC; renaming a
  parameter keeps the wires and literals of its nodes, changing the kind changes the call nodes, deleting an RPC deletes its nodes
  (all undoable). The arguments are checked against the parameter types when they arrive; a wrong one is refused and counted.
- **Replicated variables**: the **Replicated** toggle of a variable makes the server send its value to the clients (delta-compressed
  with the entity's snapshot); `Event.Rep` (Name: the variable) runs on a client when a value that differs arrives.
- **Role and ownership**: `Net.IsServer`, `Net.IsClient`, `Net.IsOwner` (Target), `Net.LocalClient`, `Net.OwnerOf` (Target),
  `Net.OwnedEntity` (Client). Without a session the graph is its own server (`Net.IsServer` true, RPCs run at once).
- **Connections and spawns** (server): `Event.ClientConnected` / `Event.ClientDisconnected` (Client; the editor's host is client 0),
  `Net.Spawn` (Path, Position, Owner → the entity; nothing on a client), `Net.Despawn` (Target).

Validation: an unknown RPC, the call node of another kind, two handlers of one RPC, `Event.Rep` of a variable that is not
replicated, reserved (`Name`, `Sender`, `In`, `Then`) or duplicate parameter names, more than 32 RPCs / 16 parameters / 64
replicated variables. NetArena's `ArenaRules.zgraph` spawns a player for every client that connects, counts the joins in a
replicated variable (its `Event.Rep` prints `BLUEPRINT_NET joins=N` on the clients), and has a server RPC `Horn` any client may
call (H) that multicasts `Honk`. Tests: `ZaminTests --filter NetGraph`, `Tests/network_editor_test.py`.

## Profiling large graphs

The VM profiler (`Machine::Profiling`, or `Machine::ProfileAll` for every machine) counts, per node of the loaded graph, the calls,
the inclusive time (with the data nodes it evaluated) and the exclusive time (its own), per typed function the calls and the time of
the steps that ran inside it (exclusive: its own steps; inclusive: also the functions it called; a recursive function counts its
inner calls again), and how often execution went from one node to the next. A node copied from a macro counts on its instance. The
times are the VM's CPU time: a Delay's wait does not count. Reading the stored result of an execution node, an event value or a
parameter is no node run.

In the editor, while the game plays: **Window → Visual Script profiler** (or **Profiler** in the Visual Script window) shows the open
graph's profile merged over all its running instances — the instances, runs and VM time; the nodes sortable by calls, self and total
time with their share; the typed functions; and the **hot path**, the most taken execution path from the entry with the most time
after it. A row selects its node in the graph. The **Heat** overlay (on by default) colours each node of the canvas from blue (cold)
through yellow to red (the hottest node's exclusive time) with its time and calls. The profiler runs only while the profiler panel
records or the overlay is shown; the last profile stays after Play stops. The Player and the editor also have a `Visual Script`
scope in the CPU profiler (every event and the delayed work of the graph hosts).

The canvas draws only the nodes and wires that are visible: a 4000-node graph at the fit zoom drew 4001 nodes / 4000 wires (562 772
vertices, 149 ms of canvas CPU time a frame in a Debug build) before, and 80 nodes / 99 wires (17 932 vertices, 19 ms) after.

## Samples and checks

[VisualWorkshop](../Projects/VisualWorkshop/VisualWorkshop.zproject) has two scenes; every graph but `NativeMethods.zgraph` runs without
its small C++ module (`Source/WorkshopNative.cpp`):

- `Visual2D`: WASD/arrow input → 2D velocity; touching a trigger changes the color for a while.
- `Visual3D`: two cubes with one graph spin at opposite speeds through different Speed overrides.
- `BlueprintGameplay.zgraph` in both scenes: an array → ForEach → a typed Award(Amount, Factor) → local/Delay/Return → Total → the Finished event → Print. Because of the Multiplier override the 2D total is 12 and the 3D total 18. The Finished event then calls `RankOf(Total)`, which returns the `Rank` enum through one of three Returns into the `FinalRank` variable and prints `BLUEPRINT_RANK rank=Silver` (2D) / `rank=Gold` (3D). Both values then go into `Summary`, a variable of the graph struct `Result {Total, Rank}` made with Struct.Make, which Struct.Break and Struct.GetField read back: `BLUEPRINT_SUMMARY total=12 rank=Silver` (2D) / `total=18 rank=Gold` (3D). Last, `Report`, a struct that nests that `Result` and keeps the rank history in an enum array (`Ranks`: Bronze, then the final rank), is read with nested GetFields and passed to two uses of the `Announce` macro (a Delay, then a Print): `BLUEPRINT_REPORT total=12` and `BLUEPRINT_REPORT ranks=2` (2D; `total=18` in 3D).
- `BlueprintChild.zgraph` (the `Blueprint inheritance controller` in both scenes) is a child of `BlueprintBase.zgraph`. The parent's
  Start evaluates its `Falloff` curve (cubic from 0 to 4 over 0..2, then linear to 6 at 3) at the `Level` variable and calls `Bonus`
  (the parent's: Score + 1); the child overrides `Bonus` with a Parent.Call times 10. With the Level overrides 1 (2D) and 1.5 (3D)
  it prints `BLUEPRINT_INHERIT curve=2.000 bonus=30.00 keys=3` (2D) / `curve=3.375 bonus=43.75 keys=3` (3D).

- `NativeMethods.zgraph` (the `Native methods controller` in both scenes) calls the module's methods on the `Workshop stats` entity:
  `BLUEPRINT_NATIVE score=36 medal=Silver hypot=5.00 awards=1` (2D) / `score=54 medal=Gold` (3D). Without the module the other graphs
  run unchanged and this one reports the unbound methods.

`ZaminTests --filter VisualGraph` runs the VM, the type/format/cycle checks, continuing delays/loops/functions, the budget, debugging, isolation,
the C++ field bridge, a real scene lifecycle and the null target protection.

The real ImGui mouse regression (adding nodes, a typed wire, rejection, save/undo/redo; then a typed function created in the UI, a parameter
added, called from the node library, wired, undone/redone, opened with a double-click, and the saved graph run in the Player; then a struct
created in the UI with an added and a renamed field, Make/GetField from the node library, wired, the wired field renamed with the wires kept,
undone/redone, and that graph run in the Player; then a struct field given a struct type with its nested default edited, an
`ArrayEnum` variable, and a macro created with an added and renamed output (undo/redo), opened, its body edited from the node library,
used from the node library and wired, renamed with its use, opened by a double-click and run in the Player; then a parent set on
a graph (undo/redo) with its inherited variable listed and not saved, a `Curve` variable whose keys are added, moved by their Time
field and by dragging in the preview, made cubic and deleted (undo/redo), an inherited function overridden (undo/redo) and its body
extended with Get.Curve/Curve.Evaluate/Add from the node library, an inherited Start event overridden with Parent.Event, and the child
run in the Player through its parent):

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

The 2026-11-27 check (curves, inheritance): `ZaminTests --filter Blueprint` covers curve evaluation (constant/linear/cubic, tangents,
ends, empty), shape checks, the curve nodes, curve variables/parameters/locals/struct fields/macro pins, the format 5 migration of
version 4 files, inherited members, function and event overrides with Parent.Call/Parent.Event (three levels too), saving only the
child's own members, diagnostics (conflicts, signatures, missing parent, cycles, path escape, errors in a parent's node) and a parent's
renames and edits followed on load; `BlueprintWorkshopRunsTypedDelayedCollectionsInRealScenes` runs the sample's child graph in both
scenes and checks that the export takes the parent.

The 2026-11-27 check (native methods, bytecode, profiler): `ZaminTests --filter Blueprint` (`BlueprintNative*`,
`BlueprintBytecode*`, `BlueprintProfiler*`, `BlueprintGraphCook*`, `BlueprintFormatVersion6AndOlderMigration`) covers the reflected
signatures (instance/static/free, pure/exec, int/float/string/vector/entity/enum/struct/array conversions), running them in a scene,
the error messages (fractions into int, C++ exceptions, a missing live script), a module unloaded and reloaded with a changed
signature (the graph still loads and keeps its wires; the binding problems; refresh; rebinding), the format 6 round trip and checks,
paste, the bytecode round trip of every kind of graph (functions, enums, structs, macros, a child with its parents) against the source,
refused damaged/foreign/outdated bytes, the profiler's counts, times, functions, hot path and merge, and the export's graph cook
(cooked, rejected, kept parents, the cache, native warnings, the host running a cooked file). Phase 6 of
`Tests/visual_graph_editor_test.py` adds a C++ method node from the node library (undo/redo), rebuilds the module with a changed
signature while the editor runs (the hot reload marks the nodes, Refresh from C++ takes the new signature and keeps the wires,
undo/redo) and runs it in the Player; then plays a 3000-node graph: the canvas draws a fraction of it, the heat overlay marks the
loop as the hottest, the profiler lists the nodes and the hot path and a row selects its node. `ctest -R ExportCookedTest` exports
VisualWorkshop: all graphs cooked, the exported Player prints the `BLUEPRINT_*` lines from the cooked graphs (the native methods
through the Release module) and a second export takes them from the cache. `ctest -R BenchGraphsSmoke` runs `ZaminBench --graphs`.

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
