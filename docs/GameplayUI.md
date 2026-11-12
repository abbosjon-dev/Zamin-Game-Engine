# Gameplay UI: HUD, inventory and modal menus

[UIWorkshop](../Projects/UIWorkshop/README.md) is a compiler-free 2D/3D course with live health,
shield-aware damage, inventory pages, keyboard/gamepad focus, nested pause/controls panels and
[gameplay save/load](GameplaySave.md). I / gamepad Y opens inventory; Escape / Start opens pause;
arrows / D-pad navigate, Enter / A submits and Escape / B goes back. WASD moves while gameplay
input is available. UI uses the existing canvas, widget, layout and tween renderer.

## Ownership and panel policy

`Gameplay::UIWorld::For(scene)` stores one context in the scene registry. Configure from an active
controller outside the panel hierarchy, normally in Event.Start. A separate graph controller can use
UIRunWhenPaused to keep its Update/LateUpdate callbacks alive at zero game delta. UIState.Time and
Delta use the scene's unscaled clock; GameTime is the gameplay clock. VM Delay still uses its normal
game delta, so use UIState.Time for UI timelines that must advance while paused.

Configure panels with unique names and optional project input action names. The default Pause action
exists; Inventory must be defined in Project settings (the sample binds I and gamepad Y). Configured
panels are hidden until explicitly opened or toggled. Their roots need UITransform, must be disjoint
siblings under one parent/container, and cannot themselves be canvases. The controller must remain
outside those roots. Initial focus, when supplied, must be a widget inside that panel.

The stack has depth eight, at most 16 panels. Opening the current top is idempotent; reopening a lower
entry is rejected. Close addresses only the top; Back respects its Back flag. Escape/B prioritizes Back;
otherwise the first matching configured toggle action in name order wins. Disable shortcuts for a
confirmation panel that must only close through its buttons. Text editing retains its own Escape
handling before menu Back. UI shortcuts require UI keyboard input enabled and no focused input field.

Only descendants of the top root accept widget selection, navigation and pointer hits. Explicit
navigation links pointing outside the modal scope are rejected. Opening a modal ends previous text
editing; programmatic focus cannot select an outside input field. A background scroll drag is cancelled
and outside scroll ancestors do not receive that modal's wheel input. Existing passive UI animations
may continue behind the panel. Managed root Order is raised with stack depth, then restored on close.
Unmanaged overlays and their canvas ordering remain content-author responsibilities.

Each panel remembers its focused widget; pop restores the previous panel's last valid focus, then its
initial focus or a bounded fallback search (4096 UI transforms). Final close restores the pre-menu
selection when still selectable. UUID and generational handles prevent a replacement entity from
inheriting old focus or bindings. Destroyed/inactive roots, destroyed controllers, disabled scripts or
paused/faulted Blueprint controllers release their open menus on the next UI frame. Script suspension
keeps configuration so re-enabling can reopen; destroyed owners remove it.

The first open captures the scene's time scale. Any stacked Pause panel sets game time to zero;
closing it restores the captured scale when no remaining panel pauses. A baseline of zero stays zero.
Coordinate external time-scale owners while menus are open: this is scene-local policy, not a global
pause-token system across additive scenes. Inventory may block gameplay input without pausing AI or
gravity. UI is processed before the game step, so automatic shortcuts pause the same frame; closing a
paused menu resumes gameplay on the next frame.

Opening/closing also fences Gameplay.Input for that frame. It returns zero movement/look, false
jump/sprint and Enabled=false while any Block panel is open or the transition fence is set. The same
opening shortcut does not immediately submit/navigate a new menu. Raw Input calls and custom movement
scripts remain author-controlled: use Gameplay.Input or UIState.Blocked for their input gate. An Open
called by gameplay scripts after the UI phase affects the next game step, like ordinary scripted pause.

## HUD and inventory views

HUDBind supports text, progress bars or both on one UITransform widget. Kind values:

| Kind | Source |
|---|---|
| 0 Health | Current / configured maximum health |
| 1 Shield | Current / configured maximum shield |
| 2 Item | Named item count / per-item stack capacity |
| 3 Bag | Used item slots / bag capacity |
| 4 Deaths | Cumulative deaths |

Prefix is at most 64 bytes; Ratio controls the optional numeric maximum in text. Numbers are rounded
for display, while fractions use canonical values and clamp to 0..1. Zero maximum produces fraction
zero. Non-smoothed progress bars update their display immediately; normal UI smoothing still applies
when enabled. The maximum is 128 widget bindings. Dynamic labels must not have a LocalizationKey;
localize their authored prefix instead. Font, color, wrapping, overflow and layout remain ordinary UI
content. Missing/recreated source actors display `--` and zero fill rather than borrowing new state.

InventoryConfigure binds up to 16 distinct text buttons to one named view (at most eight views).
Rows cannot also belong to another view/HUD binding. Items use the canonical bag's deterministic key
order, with clamped pages; empty rows show `--` and become non-interactable. State reports the full
selected item key/count, page/page count and used/capacity slots. Focus on a filled row selects its item.
Changing page preserves a valid item-row focus or moves footer focus back to an enabled row. Shrinking
a bag clamps its page and repairs disabled focus. Automatic navigation skips empty slots; authored
explicit links must still point to usable widgets. The sample uses automatic item rows and explicit
footer links. Item selection is presentation; consuming/equipping/crafting belongs to gameplay rules.

Views refresh before UI input and again after gameplay, including paused updates. Host suspension
freezes its bindings; destroying the host or removing widget components prunes them. Removing a view
restores its original text/progress/interactable values. Missing actors keep the view configured with
empty rows so content can explicitly rebind. UI state itself is transient; a gameplay save restores
health/bags and the existing UI reads them without serializing its menu stack.

[Quest HUD bindings](GameplayQuest.md) add whole-quest or objective progress and participate in the
same one-writer widget checks.

## Blueprint operations

| Nodes | Use |
|---|---|
| `Gameplay.UIConfigure` | Register panel name, action, initial focus, pause/block/back policy. |
| `UIOpen`, `UIClose`, `UIBack` | Open, close top or go back. |
| `UIRemove`, `UIClear` | Release one panel or uninstall all managed panels/views. Clear restores authored widgets/active flags; it is not just a close-all command. |
| `UIRunWhenPaused` | Explicitly set the target script's paused-update flag. |
| `UIState` | Canonical top/depth, scene pause, input block/fence, selected entity, unscaled/game clocks and dropped events. |
| `UIEvent` | Pop one bounded transition/selection event (32 entries). State is authoritative; events can be dropped. |
| `UIFocus` | Select an eligible widget inside the current modal scope. |
| `UIBindHUD`, `UIUnbindHUD` | Bind/release a native health/bag display. Actor must be supplied. |
| `UIInventoryConfigure`, `UIInventoryState` | Bind rows and read live page/selection. |
| `UIInventoryPage`, `UIInventoryRemove` | Change a page or release its view. |

All abbreviated names above have the `Gameplay.` prefix. Operations return Success and Reason;
pure State/InventoryState report canonical values. Configure inputs serialize with graph content;
records stay main-thread/scene-owned and reset on Play stop/start. Back, focus and active-state
callbacks are protected against recursive mutation during stack transitions. Callback side effects
are not a general rollback transaction. The package supplies reusable views/control policy, not a
new renderer, networking authority, equipment system or full ECS snapshot.

## Verification

`GameUI*` tests cover the pure stack/page/format models and real scene behavior: keyboard/gamepad
input, same-frame pause, transition fencing, pointer/focus/text/scroll isolation, sparse pages,
source replacement, component removal, script lifecycle, time-scale restoration and save-backed HUD.

`python3 Tests/ui_workflow_test.py --build build --exports` runs actual keyboard/gamepad Player
replay, visible second-page captures at 1280x720 and 1024x768, Editor auto-play and compiler-free
exports for both dimensions. Save deliberately reduces HP/coins afterwards; Load must restore the
saved native values for acceptance. Export relocation is the existing launcher's --version smoke
check; real relocated save resume was tested in SaveWorkshop. `Tests/ui_editor_test.py` exercises
real ImGui UIConfigure/UIBindHUD/UIInventoryConfigure add/save/Undo.
Reports and screenshots: `build/GameplayUI/`. Software Mesa results do not establish a hardware GPU
frame-time budget.
