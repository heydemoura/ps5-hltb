# Validation — 2026-10-17

Test console: `192.168.0.190`, elfldr TCP 9021, firmware `0x12200005` (12.20).
Build host: macOS ARM, Homebrew LLVM, ps5-payload-dev SDK.

## Live title queries executed on PS5

| Query | HLTB game ID | Main Story seconds | Formatted result |
| --- | ---: | ---: | --- |
| Elden Ring | 68151 | 216464 | 60h 08m |
| ASTRO BOT | 151301 | 39942 | 11h 06m |
| RESIDENT EVIL requiem | 169267 | 39800 | 11h 03m |
| Mario Kart 8 Deluxe | runtime background lookup | 22538 | 6h 16m |

Requests used `sceHttp2` on the PS5, not a host proxy or mocked data. Adding
GlobalSign Root R46 resolved the initial missing-CA error while retaining
certificate verification.

## Native shell rendering

The user explicitly approved SceShellUI as the renderer after the process
roles were explained. Home-screen text belongs to SceShellUI on this firmware;
SceShellCore is a separate service process.

Live metadata resolved `ReactRawTextShadowNode.SetText(string)` and
`Application.Update`. Installed method and trampoline bytes were read back
and verified; the installer detached and restored its original authorization.
The privileged controller exported all 31 installed PS4/PS5 title records for
the renderer to read.

Runtime evidence included:

```text
rendered-inline thread=887ffcf00 RESIDENT EVIL requiem  |  Main Story: 11h 03m
lookup title=ASTRO BOT status=0 seconds=39942
rendered-inline thread=88dbea740 ASTRO BOT  |  Main Story: 11h 06m
lookup title=ProsperoEden status=1 seconds=0
lookup title=Forwarder Manager status=1 seconds=0
```

Thread and address values are observations from one session, not firmware
constants. React callbacks ran on several React threads, distinct from the
PUI frame thread. The renderer applies cached strings directly inside the
original setter; pending refreshes are restricted to the recorded owner
thread and same root widget. The frame hook is used for observation, not
cross-thread text mutation. GC handles pin tracked nodes while retained.

The completion is appended to the existing text node. The code does not set
font family, size, weight, color or an alternative text renderer.

The user directly confirmed both:

1. Resident Evil Requiem's time was visible beside its title in the title font.
2. Astro Bot's time appeared automatically after highlighting it and waiting,
   without requiring selection a second time.

Original text-trace hooks were restored and verified before replacement.
Original native text/frame hooks were also restored and verified during the
renderer replacement test. No console restart occurred during these tests.

## Host checks

`make test` builds and runs with AddressSanitizer and UndefinedBehaviorSanitizer:

- Correct title chosen rather than a similarly named sequel.
- Trademark/case normalization and search-term cleanup.
- Malformed JSON and missing/zero/oversized Main Story data.
- JSON escaping and output buffer boundaries.
- Cache values and title identity checks.
- Catalog snapshot round-trip, capacity and malformed/trailing-data rejection.

## Scope

On-screen evidence is the user's direct observation plus native runtime logs;
no screenshot was captured. Other firmware versions, every installed game,
localized title aliases, and arbitrarily long native label layouts have not
been exhaustively tested. The shell controls title clipping. Website changes
or unavailable estimates can leave a title unannotated.

## Final packaged startup and storage checks

The main single-file `hltb.elf` was deployed directly to elfldr and reported:

```text
Exporting 31 console game titles for ShellUI
native inject pid=101 attach=1 mapped=1 imports=79 unresolved=0 thread=1 detach=1 auth-restored=1
install kind=1 method=80c12e850 verified=1
install kind=2 method=808d47e70 verified=1
HowLongToBeat native title annotations enabled
```

Its native frame counter continued advancing with annotations enabled.
An additional live query with trademark-bearing input, `The Last Guardian™`,
returned HLTB game 9995, 42828 seconds, `Main Story: 11h 54m`.

Testing revealed that `/system_tmp` is a 256 KiB filesystem on this console.
The final implementation uses one cache file bounded to 8 KiB and 16 entries,
and a synchronized log that rotates at approximately 6 KiB. The cache uses an
atomic temporary-file rename; it can occupy two filesystem blocks while being
replaced. Successful lookups remain in memory even if a cache write fails.
After the final payload installation, five 16 KiB blocks remained free despite
inert diagnostic threads from this development session retaining some earlier
unlinked log files. All linked HLTB diagnostic leftovers were removed before
that final installation. No unrelated console files were removed.

The storage tests additionally exercised 40 long-title writes, checking that
the cache stayed within 8192 bytes, enforced the entry limit, and rejected an
expired entry. The deployment helper checks the payload's success message and
returns a failure if installation does not complete.
