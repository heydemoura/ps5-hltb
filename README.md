# ps5-hltb

This PS5 ELF payload adds
**HowLongToBeat Main Story, Main + Extra and Completionist times** alongside the shell's **Time played**
control. It creates a native `SceShellUI` Label in its own container, as a sibling
of the played-time control. The completion stacks above played time, uses the
same font and color, and follows the control through focus/layout changes.
The original played-time wrapper and game title are preserved.

**Validated on firmware 12.20:** the user confirmed the sibling-container version
works after the game-switching and Play Game focus check. See
[validation and investigation](docs/PLAYTIME.md).
The earlier title-annotation version is available in release `v0.1.0`.

Queries use game titles and run directly from the console over verified HTTPS.
No PC proxy, account or API key is needed.

## Build and run

Install the [ps5-payload-dev SDK](https://github.com/ps5-payload-dev/sdk) and
LLVM. On macOS, the SDK recommends Homebrew LLVM.

```sh
export PS5_PAYLOAD_SDK="$HOME/opt/ps5-payload-sdk"
export LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config # macOS
make
make test
make deploy PS5_HOST=192.168.0.190
```

`build/hltb.elf` is the **single main payload**: it exports the installed-game
title catalog, injects the native renderer, validates and installs the native
hooks, then enables annotations. Send this ELF to elfldr with any compatible
payload sender. No additional files need to be copied onto the console.

Highlight a game with a visible Time played control. The shell's native label
update identifies the control. Cached results can display immediately; new
lookups run in the background, and a UI frame applies a completed result.

Example:

```text
Main Story: 11h 03m
Main + Extra: 14h 01m
Completionist: 31h 51m
[Time played icon] 9m
```

## Behavior and limits

- Reads installed PS5 JSON and PS4 SFO title metadata without modifying them.
- Resolves native methods and fields through Mono metadata; never reuses ASLR
  addresses from a previous session. Hook prologues must match supported
  instruction sequences or installation is rejected.
- Queries HowLongToBeat's website search, matches the game title, and reads
  `comp_main`, `comp_plus` and `comp_100` in seconds. Trademarks are removed from search terms and ignored
  when matching. Edition names and numerals remain significant.
- Missing matches and missing/zero Main Story estimates leave the shell alone.
  A failed title is not retried repeatedly during the same payload session.
- Uses verified TLS with an embedded GlobalSign Root R46 certificate. The
  unofficial website API can change, and strict title matching can miss
  localized names or edition differences.
- Caches successful results for up to seven days under
  `/system_tmp/hltb-cache.json`. The disk cache keeps at most 16 entries in
  8 KiB; live results also stay in memory. The console can clear temporary
  files on reboot. Logs are bounded to fit the console's small temporary
  filesystem. No saved game or application database is edited.
- Metrics appear on three lines using the played-time label's native component,
  font and color. The panel copies the played-time background color, corner
  radius and vertical padding, with 24 px horizontal padding on each side. Unavailable extra/completionist estimates display `N/A`.
  The existing game title and played-time text are preserved.
- Firmware 12.20 is the only firmware tested. This is an experimental homebrew
  payload. The resident code lasts until ShellUI restarts or the console reboots.
  Loading a duplicate into the same ShellUI is refused.

## Query and development tools

`build/hltb-query.elf` is a separate stdout-only diagnostic:

```sh
python3 tools/query.py 'ASTRO BOT' --host 192.168.0.190
```

It accepts a title on stdin or as command-line arguments when started through
an elfldr URI. It performs a live lookup and also seeds the console cache.

Additional development targets:

- `make deploy-probe`: read-only HTTP inspection service on port 9118,
  limited to 15 minutes; `/quit` stops it early. Use on the trusted test LAN.
- `make inject-probe`: Mono metadata probe, without native text patches.
- `make inject-trace` / `make remove-trace`: temporary text tracing and undo.
- `make inject-native` / `make enable-native`: staged renderer installation
  for inspection. The main `hltb.elf` performs both steps automatically.
- `make disable-native`: removes the completion labels on a subsequent UI frame.
- `make remove-native`: disables annotations, allows frame cleanup, then restores
  the original native method bytes.
- `make replace-native`: development replacement after restoring old hooks.
  Old injected threads remain inert until reboot.

Runtime logs are in `/system_tmp/hltb-native.log`. Firmware captures and build
artifacts stay in ignored `build/`; no Sony firmware or font assets are included.

Source repository: [heydemoura/ps5-hltb](https://github.com/heydemoura/ps5-hltb)
(private). License: GPL-3.0-or-later; see [third-party notices](THIRD_PARTY_NOTICES.md).
