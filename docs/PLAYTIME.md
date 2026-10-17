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

The renderer creates a separate native `Label`, copies FontConfig, Font and
TextColor from the played-time label, and appends it to that label's native
parent. Its local position places the completion to the left of the stopwatch
row. It never sets the source label's text or changes the game title.
Construction, insertion, refresh and disposal run on the UI frame thread.
Network queries stay on the background worker. GC handles retain tracked
objects; disabling disposes annotations before hooks are removed.

## Validation status

Host parsing and storage tests pass with AddressSanitizer and
UndefinedBehaviorSanitizer. The new observer and guarded native hooks have
been exercised on the console. Visual validation of the sibling renderer,
game switching and removal is still pending; this branch is not yet a release.

The initial sibling-label insertion was logged with the original `9m` value
intact, but the user did not see the completion. Process enumeration established
that ShellUI restarted (PID 101 to 274). Therefore that insertion log is not
proof of a working renderer. Rendering is disabled during the bounds/clipping
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

This evidence does not prove on-screen visibility. Rendering is disabled while
that discrepancy is investigated. `tools/native_status.py` now rejects stale
requests when their recorded ShellUI PID no longer has a readable memory map.
