# Gameplay: motor, camera and checkpoint

`GameplayWorkshop` is a compiler-free 2D/3D course. Open its project in the launcher, choose
`Course2D.zscene` or `Course3D.zscene`, then Play. WASD/arrows move, Space jumps, Shift sprints,
and E restores the last checkpoint. Reach the golden marker, clear the coral wall and enter the
green gate. Falling below the course restores the checkpoint. Exit and reopen Player to resume.
The bundled skeletal runner is a procedural animated test model, not a humanoid animation set.

## Blueprint nodes

| Node | Contract |
|---|---|
| `Gameplay.Input` | Composite action input: analog Move, mouse Look, jump press/hold, sprint. Enabled=false and focused text fields suppress input. |
| `Gameplay.Motor2D` | Dynamic Box2D body: platform movement or TopDown XY movement. Requires a running physics world. |
| `Gameplay.Motor3D` | Running CharacterController3D, optional camera-relative horizontal movement. Cannot also have Rigidbody3D. |
| `Gameplay.Motion` | Actual physics velocity, position, horizontal speed and grounded status. |
| `Gameplay.BindAnimator` | Actual physics movement into typed Speed/MoveX/MoveY/VerticalSpeed/Grounded parameters, normalized by ReferenceSpeed, optionally damped. Target is the Animator, Source is the motor actor. |
| `Gameplay.Camera` | Camera target + Anchor: 2D follow with viewport-aware bounds, or 3D orbit with spherecast obstruction. |
| `Gameplay.CheckpointCapture` | Save actor world position/rotation to a named memory or persistent slot. |
| `Gameplay.CheckpointRestore` | Validate identity, transform and optional center clearance probe, synchronize physics, clear movement intent. |
| `Gameplay.CheckpointHas` | Query memory/disk slot existence and validity. |
| `Gameplay.CheckpointClear` | Remove memory/disk slot. |
| `TextEqual` | Exact case-sensitive text comparison, useful for named message routing. |

Connect Update → Input/variables → Motor. Invoke one motor per actor per graph frame; duplicate
advancement fails instead of applying acceleration twice. Call BindAnimator after the motor or in
LateUpdate. A 2D motor retains pre-command measured velocity for Update binding; LateUpdate reads
post-step velocity when a fixed physics step occurred. A frame with zero fixed steps retains the
last measured velocity. This avoids animating requested speed when the body is blocked by a wall.
Empty binding parameter names disable individual outputs. All requested parameter types are
validated before any parameter changes; mismatched configuration returns Success=false.

Set Speed, acceleration/deceleration, air control, sprint multiplier, jump speed, coyote time,
jump buffer and early-release cut through node pins. Move is clamped to unit length while preserving
analog magnitude. TopDown disables gravity-oriented jump handling; configure body gravity scale
for the desired game. 3D camera-relative movement projects the camera onto the horizontal plane.
The motor supports strafing; visual facing/rotation is controlled separately by the game graph.

The 2D grounded probe uses three downward rays and upward-facing normals, excluding the actor.
Box sizes are half extents, as in the engine's existing BoxCollider2D API. Circle colliders are
also supported. A 3D CharacterController root is at its feet, not its capsule center.

## Lifecycle and camera

Each VisualGraphHost owns its motor/camera/checkpoint state. Disabling or destroying the host,
a VM error or debugger pause stops controlled horizontal motion; vertical gravity continues.
TopDown stops both controlled axes. Dead or recreated UUID targets are discarded using generational
entity handles. Targets from another scene are rejected. Limits per host are 64 motors, 16 cameras
and 16 cached checkpoints.

Run follow cameras in LateUpdate. Offset, Eye, sensitivity, damping, snap and teleport threshold are
configurable. 3D obstruction uses a sphere sweep, ignores sensors/player/camera and checks the
smoothed path each frame. Set Mask to the obstacle layers. 2D bounds account for camera view size;
when the view is larger than the bounds it is centered. This is a follow/orbit controller, not a
cinematic collision solver: no shoulder selection, predictive occlusion or camera volume system.

## Persistence and restore

Persistent slots live beside SaveData in `gameplay-checkpoints/<slot>.json`. Slot names allow
1–64 ASCII letters/digits/underscore/hyphen. Files are atomically written, at most 4 KiB, versioned,
depth-limited and contain scene name, full decimal-string actor UUID, world position and rotation.
Memory capture does not require persistent write success; persistent capture updates its cache
only after a successful disk write. I/O is limited to eight requests per host frame. These files
are local game saves, not authenticated multiplayer state.

Restore validates scene+actor identity and finite transform before mutation. Optional Clearance is
a center circle/sphere probe excluding the actor and triggers; it is **not a full capsule or box
fit test**. Set it to zero to disable. Box/circle collider offsets and the character's feet-to-center
offset are considered. Rigidbody2D/3D and character positions are synchronized, velocities and
motor jump state are cleared. The capture preserves position/rotation, not inventory, complete ECS,
scale or arbitrary graph variables. For larger world state see [Scenes.md](Scenes.md).
A camera in another graph can snap on a restore displacement through SnapDistance or an explicit
Snap input. Rename a scene or change actor UUID deliberately to invalidate its previous saves.

## Verification

`GameplayMathTests.cpp` exercises math and bounded checkpoint decoding; `GameplayRuntimeTests.cpp`
uses real Box2D/Jolt worlds, typed Animator binding, camera obstruction, persistence, identity and
lifecycle checks. The leaf math/codec tests can run with ASan/UBSan/LSan independently of the engine.

The real ImGui test `Tests/gameplay_editor_test.py` checks motor/camera/checkpoint node search,
save and Undo in a private project copy (run under xvfb-run with ZAMIN_BUILD_DIR set).

Run `python3 Tests/gameplay_workflow_test.py --build build --exports` for real Player input replay,
fresh-process checkpoint recovery, Editor auto-play, compiler-free export runs and screenshots.
The gameplay acceptance requires movement, jump, checkpoint, goal and restore flags; missing flags
fail the test. Exports run twice and are relocated for a version check by the existing export harness.
Private fixture overrides enable deterministic replay/observation; source project defaults remain
interactive. CI runs the workflow for both Linux compilers. See [Testing.md](Testing.md).
