# Played-time container variant

Branch: `feature/playtime-container`.

The requested behavior is a separate Main Story label in the shell's existing
played-time container, using the same native component. Played time and the
game title must remain intact. This variant builds `src/playtime_native.c`;
`src/shell_native.c` retains the original title annotation implementation.

## Runtime evidence on firmware 12.20

A read-only React shadow-tree snapshot located the selected Resident Evil
Requiem's `9m` label and the nearby translated tooltip, “This is your total time
spent playing the game.” The tooltip's stable localization ID is
`msgid_playtime_indicates_overall_time`. The label shares a row with a stopwatch
image and the tooltip container. Identification uses that structure and ID,
independent of the displayed language or duration.

The native `ReactTextShadowNode.UpdateLabel` observer confirmed the underlying
component is `Sce.PlayStation.PUI.UI3.Label`. Both the update callback and
`Application.Update` ran on the same UI frame thread. The hook's RIP-relative
initialization guard is relocated in its trampoline; installation checks the
actual instruction bytes before patching.

The current renderer creates a native `ReactPanel` beside the played-time
control's branch in the elements tree. Attachment uses the native common
ancestor of the selected game's title and played-time label, rather than the
focus-dependent widths of the played-time wrappers. The new panel contains a
separate native `Label`, copying FontConfig, Font and TextColor from played time.

The completion is measured and stacked 16 pixels above the played-time label,
with 24 pixels of left padding, 16 pixels of right padding, and 8 pixels of
vertical padding. Its position, effective visibility and opacity follow the
played-time control on each frame. If the shell rebuilds or reparents that
control, the annotation is recreated in its own sibling container. Existing
wrapper dimensions and source text are never set by this implementation.

Construction, insertion, refresh and disposal run on the UI frame thread.
Network queries stay on the background worker. GC handles retain tracked
objects; disabling disposes the panel and label before hooks are removed.

## Validation status

Host parsing and storage tests pass with AddressSanitizer and
UndefinedBehaviorSanitizer. The new observer and guarded native hooks have
been exercised on the console. After the final game-switching and Play Game
focus check was requested, the user confirmed: “It works, commit the current
state.” This validates the deployed sibling-container implementation at commit
`e9f1d8a`. This branch has not been published as a release.

The initial sibling-label insertion was logged with the original `9m` value
intact, but the user did not see the completion. Process enumeration established
that ShellUI restarted (PID 101 to 274). Therefore that insertion log is not
proof of a working renderer. Rendering was disabled during that bounds/clipping
investigation. Managed arguments, the target parent, the selected root and the
new text are now retained with pinned GC handles across managed calls; the
cause of the restart is not yet established.

## Investigation artifacts

Firmware captures and snapshots stay in ignored `build/` and `.deps/`.
`make inspect-layout` runs a one-shot metadata probe without widget mutation.
`tools/inspect_tree.py ROOT_ADDRESS` captures a read-only shadow-tree snapshot;
its offsets are verified only for firmware 12.20 and reads are not atomic.

REA managed metadata evidence:

- React shadow-node members: `ev_5b67d59e528a76fb703ca4787f6f8e0e0e2e8945c8835bdba60ad849193400e1`.
- Text and dispatcher members: `ev_c133121fc105a2fb7b9d810062a758556949f9b116fb98e67ba26f026b58d078`.
- Native Label, Widget and SpannableText declarations: `ev_4b52ab0c931ab6ac6321bb82d53a3f2ae8d0c4665ec13a01faf7cdbccdc7ac6b`.

These establish declarations, not execution. The live trace and memory
snapshots establish the component, layout and thread observations above.

## Follow-up on the restarted shell

On PID 274, pinned-reference insertion continued through more than 12,000 UI
frames without another observed restart. The user still reported no visible
completion. Memory inspection established:

- Source and added objects are native Label components with the same parent.
- Their FontConfig references, 32-byte native Font values, and native TextColor
  values match exactly.
- The new element's SpannableText contains `Main Story: 11h 03m`, and its native
  text rendering list has an entry.
- Both labels have nonzero size, full opacity, visible/on-screen state, a ready
  render state and the same parent scene.
- World coordinates were `(1701, 875)` for played time and `(1295, 875)` for
  completion, within the same final clip rectangle `(0, 332, 1920, 1080)`.
- Disabling removed the extra native child and restored the row's three original
  children. Frame execution continued normally.

At this stage, that evidence did not prove on-screen visibility. Rendering was
disabled while the discrepancy was investigated. `tools/native_status.py` now rejects stale
requests when their recorded ShellUI PID no longer has a readable memory map.

## Draw observer and container expansion

A temporary `LabelElement.OnRender` observer counted identical per-frame calls
for the source played-time label and the completion label (for example,
2,519 calls each). Thus the invisible completion was not explained by omission
from that native callback. The observer's original bytes were restored and
verified before the next renderer test.

The narrow played-time row and its wrappers originally measured 97 pixels
wide. A subsequent experiment expands the row and its equally narrow wrappers
to 453 pixels, shifts their outer wrapper left by 356 pixels, and offsets the
three original row children right by the same amount. The completion Label
occupies the added space within the same row. Native world coordinates confirm
the played-time label stays at `(1701,875)` while the new label is at
`(1295,875)`. Geometry snapshots retain original values for disable/removal.
This is a visibility experiment; a narrow wrapper or intermediate render target
was not established as the cause. The user subsequently confirmed this
experiment was visible, as recorded below.

## User feedback incorporated

The user confirmed the expanded same-row experiment became visible. They asked
for more left padding, a vertical stack above played time, and a separate
sibling container because focusing Play Game causes the original wrapper to
rerender and change dimensions. The current implementation above replaces the
expansion experiment and does not resize or reposition original shell widgets.
The user subsequently confirmed the final version works after being asked to
switch away and back, then focus Play Game to check the stack, padding and
preservation of the original played-time control. The final implementation is
committed on `feature/playtime-container`; the earlier expansion experiment is
retained here only as investigation history.

## Three completion metrics

The next iteration adds `comp_plus` (Main + Extra) and `comp_100`
(Completionist), each on its own line after Main Story. Missing, zero, negative,
non-numeric or oversized optional estimates become `N/A`. Main Story matching
and validation retain their prior behavior. The bounded cache now uses schema
version 2 so pre-existing Main Story-only entries trigger a fresh lookup.

The metrics panel copies the first non-transparent native panel background in
the played-time branch and measures the visible row's content insets. A
three-line native Label uses the same source font and color. The panel remains
a sibling of the played-time control and is placed above its background box.

A live PS5 HTTPS lookup for Resident Evil Requiem returned:

```text
Main Story: 11h 03m
Main + Extra: 14h 01m
Completionist: 31h 51m
```

Host tests cover all three values, optional invalid/unavailable estimates,
multiline formatting and buffer limits, cache round-trip and legacy-cache
invalidation. Sanitizer-enabled parsing and bounded storage tests pass.
The user confirmed all three lines, translucent black background, padding and
Play Game focus behavior on the console. The background copied from the native
played-time panel was RGBA `(0,0,0,0.701961)`.

The user then requested matching border radius, confirmed that change works,
and asked for more horizontal padding. The final spacing is 24 pixels on both
sides; vertical padding and border radius continue to come from the native
played-time panel. The user confirmed the increased horizontal padding looks
right and requested committing these changes.

A second live PS5 lookup for Astro Bot returned Main Story `11h 06m`,
Main + Extra `15h 12m`, and Completionist `18h 31m`. Both live queries and all
host tests passed before the final commit.
