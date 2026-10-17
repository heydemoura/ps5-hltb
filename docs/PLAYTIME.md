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


## Game-switch crash investigation

The user reported a console crash after approximately four game switches after
loading main commit `ab093b8`. The retained log showed annotations for Resident
Evil Requiem, Mario Kart 8 Deluxe and Marvel's Wolverine on ShellUI PID 314;
that process no longer existed when inspected. No crash stack was available,
so the exact faulting instruction is unknown.

Source inspection found retention of up to 16 old controls and their ancestor
paths, a 64-entry pinned event queue, panel recreation on identical text updates,
and boxed managed Boolean allocations every frame for every retained control.
The lookup worker also stayed attached to Mono while blocking in native HTTPS.
These are lifecycle hazards; their presence alone does not prove the crash cause.

The replacement tracks one active control and one latest pending event, invalidates
old selections by generation, releases replaced handles, rejects foreign threads
before managed observation, and reuses the panel for identical control updates.
A native scalar disposal getter avoids per-frame boxing. Widget construction,
attachment and cleanup remain on the UI thread. Cleanup detaches each owned child
before disposing it, and skips native operations on failed constructors. The one
lookup worker detaches from Mono after initialization, coalesces navigation, and
never mutates widgets. Disable polling is on the UI frame, independent of a
blocked lookup.

Successful estimates now persist in `/user/hltb/cache.json` for exactly 30 days.
File cache hits take priority over HTTPS; memory entries retain the original
expiry instead of extending it. The bounded file holds up to 512 records / 1 MiB
without evicting valid entries and migrates the previous temporary cache.
The query diagnostic follows the same cache-first behavior.

Host sanitizer tests exercise 2,000 game switches, 100,000 coalesced events,
identical update reuse, foreign-thread rejection, handle allocation failures,
and full disable cleanup using the actual renderer hooks with a mock widget
adapter. This is lifecycle evidence, not proof of Sony renderer stability.
Storage tests check values beyond seven days, the 30-day expiry boundary,
future timestamps, and more than 16 retained valid records. HTTP tests account
for all request, template, HTTP, TLS and network-pool resources over 1,000
lookups, including failures during initialization and request setup.

Console switching validation is pending on the updated payload.


### Confirmed failure and scene-root fix

A native debugger captured SIGABRT at Mono's unhandled-exception path. A
breakpoint at that path captured the outer exception and its inner exception:

```text
ReactNative.Vsh.Common.RenderScene+ReactApplicationRenderSceneAggregateException
Cannot fallback to original bundle
  System.InvalidOperationException
  Could not get tag for view.
```

This is concrete evidence of a React view-registry failure, rather than a
network-resource failure. Native logs show previous panel disposal completed;
one reproduction used file cache hits for both games. Changing ReactPanel to a
plain PUI Panel while retaining the old parent did not prevent the failure.
The missing React tag is consistent with an unregistered custom child inside
a subtree managed by React's native view hierarchy.

The renderer now resolves `Widget.get_ParentScene()` and
`Scene.get_RootWidget()`. It verifies that this native scene root is an ancestor
of the current source and is not a React class, then attaches the metrics panel
as a sibling of the React content tree. The retained, bounded current-source
ancestry is used only to translate coordinates and visibility into that native
root. No synthetic React tag or registry entry is created.

Games without a Time played label now get a title-based fallback after six UI
frames. The panel uses the current title's scene and font configuration, native
Label default size, white text, and the verified padding/background/radius.
A real played-time update replaces this fallback. No previous game tree is
retained for fallback styling. ASTRO BOT was reported by the user as an example
with no Time played control; ProsperoEden has no matching HLTB result and is
expected to remain without completion estimates.

The host runtime adapter rejects appending an untagged widget under a simulated
React-managed view, exercises scene-root placement, cold-start fallback and
replacement by played time, as well as the prior stress/cleanup checks. The
final host run retained at most 16 handles in this fixture. Native scene paths
may contain more ancestors, but are bounded to 48 and released on each selection.
Temporary fatal callbacks and standard-stream redirection used during the
investigation were removed from the final payload.

The user confirmed “It is not crashing anymore.” On ShellUI PID 373 the scene-root
build continued through 42 selection changes, including ASTRO BOT, Resident Evil
Requiem, ProsperoEden, Mario Kart 8 Deluxe and other titles. Runtime counters
showed at most one panel remaining active (30 created / 29 removed at the
42-selection checkpoint) and a handle peak of 33, instead of growth by game.

A native worker probe established that `/data` is not mounted inside ShellUI
(`stat` returns ENOENT), while creating and writing `/user/hltb` succeeds and is
visible to the privileged read-only probe. The final cache therefore uses
`/user/hltb/cache.json`; no directory-descriptor transfer or credential change
is necessary. An ASTRO BOT diagnostic returned `Source: file cache` and left
the persistent file's SHA-256 unchanged, preserving the original timestamp.

The first title-only fallback exposed a visibility issue when games use logo
artwork and leave the title text Label hidden. The final panel follows the
native scene's visibility instead of title/played-time controls. The user
confirmed that games without played time still require Play Game focus. It explicitly constructs a native 24 px
UIFont. Native `GetTextHeight` and `GetLineCount` measure all three lines after
layout, updating both text and background dimensions; an initial generous
height and FitHeightToText avoid clipping while layout settles.

The user confirmed all three lines fit. Fresh persistent cache writes were
observed, bringing the cache to eight entries. Visibility before Play Game focus
remains unresolved.

During the subsequent unfocused ASTRO BOT scene inspection, the user reported
a full console crash. The injected diagnostic invoked scene/widget getters
from an attached worker, outside the renderer UI callback. Deployment timed
out and the HTTP log endpoint became unreachable; no exception or crash dump
was recovered. The diagnostic is a suspected trigger, not a proven cause.
Do not repeat this worker-based inspection. Any further managed UI inspection
must run through the existing UI callback with bounded lifetime and retained
objects. Previous game-switch stability does not establish stability of this
diagnostic build or resolve the new full-console failure.

Offline follow-up after the full-console failure: the worker now detaches from
Mono on initialization failure as well as before normal network operation. The
host adapter asserts UI ownership for handle creation, widget allocation and
managed invocation. Ten injected managed failures cover style, text, scene
lookup, sizing and both child-attachment stages; disabling afterward leaves
zero handles and no attached panel. Panel removal counters now count only
previously successful panel constructions. ASan/UBSan tests pass; these checks
do not establish the cause of the full-console failure or prove console
stability. The updated payload has not been deployed pending console recovery.

A UI-thread-only scene trace after reboot confirmed per-game ReactScene and
ReactNavigationScene instances beneath a shared AppScene. All reported visible,
but a focus transition changed the game root final opacity. The user still
observed metrics only on Play Game. At the user’s suggestion, the next build
uses fixed lower-right placement, preferring the nearest shared AppScene native
RootWidget. Source ancestry remains retained and validated against its original
root, separately from the screen host; both are released per selection. Host
tests exercise a faded original scene, visible shared host and complete cleanup.
The fixed-position build is deployed on PID 62; visual and switching verification
are pending. The earlier worker-based scene probe has not been run again.

The user confirmed fixed-screen placement works. Both played and unplayed games
now use the same metrics style and construction path: title FontConfig, explicit
24 px UIFont, white text, translucent background, 15 px radius, 24/13 px padding
and measured three-line height. The previous played-control style/background
copying and child enumeration were removed. Revealing played time reuses the
existing panel rather than replacing it. The temporary scene-chain trace was
removed; bounded host discovery stays on the UI thread. Host tests verify shared
host placement, unchanged panel identity when played time appears, failure cleanup
and bounded resources. Visual verification of the unified styling is pending.

PR screenshot verification: the user saved a native home-screen capture with
ASTRO BOT selected and Play Game unfocused. Retrieved capture
`20261017_151503_00158173.jxr` from the console media gallery and decoded it
locally with jxrlib. The README PNG is an SDR rendering of that HDR capture at
1920×1080; no UI content was added or removed. It visibly contains all three
metrics and the unfocused Play Game button. The latest native log remained on
PID 62 across 18 selections, with one active panel (12 created / 11 removed),
28 current handles and peak 33. `make all test` passed before PR publication.
