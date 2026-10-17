# ps5-hltb

A PS5 ELF payload that adds **HowLongToBeat Main Story completion time** to
native game-title text on the home screen.

The payload queries games **by title**, using HowLongToBeat directly from the
console over HTTPS. It updates the existing React text node in **SceShellUI**,
so the completion text uses the title's existing font, size and styling.
No separate overlay font, PC proxy, account, or API key is needed.

**Tested on PS5 firmware 12.20 with elfldr at `192.168.0.190:9021`.** The user
confirmed Resident Evil Requiem's completion time was visible in the title font,
and Astro Bot's time appeared automatically after its first lookup.
See [validation evidence](docs/VALIDATION.md).

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
title catalog, injects the native renderer, validates and installs the text
hooks, then enables annotations. Send this ELF to elfldr with any compatible
payload sender. No additional files need to be copied onto the console.

Highlight a game on the home screen. Cached results appear with the title;
new titles are queried in the background and applied on a subsequent React
text update. The first lookup can take up to the next UI update (often within
a minute); selecting the game again also displays a ready result.

Example:

```text
RESIDENT EVIL requiem  |  Main Story: 11h 03m
```

## Behavior and limits

- Reads installed PS5 JSON and PS4 SFO title metadata without modifying them.
- Resolves native methods and fields through Mono metadata; never reuses ASLR
  addresses from a previous session. Hook prologues must match supported
  instruction sequences or installation is rejected.
- Queries HowLongToBeat's website search, matches the game title, and reads
  `comp_main` in seconds. Trademarks are removed from search terms and ignored
  when matching. Edition names and numerals remain significant.
- Missing matches and missing/zero Main Story estimates leave the title alone.
  A failed title is not retried repeatedly during the same payload session.
- Uses verified TLS with an embedded GlobalSign Root R46 certificate. The
  unofficial website API can change, and strict title matching can miss
  localized names or edition differences.
- Caches successful results for up to seven days under
  `/system_tmp/hltb-cache.json`. The disk cache keeps at most 16 entries in
  8 KiB; live results also stay in memory. The console can clear temporary
  files on reboot. Logs are bounded to fit the console's small temporary
  filesystem. No saved game or application database is edited.
- Font settings are left to the existing title widget. Long titles can still
  be constrained by the shell's native text layout.
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
- `make disable-native`: stops annotating subsequent title updates.
- `make remove-native`: restores the original native method bytes. Existing
  title text returns to normal when the shell next recreates that text.
- `make replace-native`: development replacement after restoring old hooks.
  Old injected threads remain inert until reboot.

Runtime logs are in `/system_tmp/hltb-native.log`. Firmware captures and build
artifacts stay in ignored `build/`; no Sony firmware or font assets are included.

Source repository: [heydemoura/ps5-hltb](https://github.com/heydemoura/ps5-hltb)
(private). License: GPL-3.0-or-later; see [third-party notices](THIRD_PARTY_NOTICES.md).
