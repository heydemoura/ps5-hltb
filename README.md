# ps5-hltb

Work in progress: a PS5 payload to display HowLongToBeat **Main Story** times
using the native home-screen game-title font.

**The native title lookup works on the test PS5. The on-screen integration
has not been implemented or verified yet.** This is not a usable UI release.

## Build

Use the [ps5-payload-dev SDK](https://github.com/ps5-payload-dev/sdk).

```sh
export PS5_PAYLOAD_SDK="$HOME/opt/ps5-payload-sdk"
export LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config # macOS Homebrew
make
make test
make deploy # direct HTTPS query for Elden Ring; stdout result only
```

The query accepts a full game title as `argv[1]` when invoked using an elfldr
URI with arguments. With no argument, the current diagnostic queries
`Elden Ring`. It initializes native PS5 networking, fetches a search token,
searches HowLongToBeat by title, matches the title, and formats `comp_main`
(seconds). Zero or missing Main Story data is not displayed as a zero-hour
estimate. Requests use verified HTTPS with an embedded public root CA.
No PC proxy, account, API key or paid service is required. HowLongToBeat's
website API is unofficial and can change.

## Console diagnostics

The default test console is `192.168.0.190`, elfldr port `9021`.

- `make deploy-probe`: runs a read-only HTTP inspection service on port 9118
  for 15 minutes. `/quit` stops it early. Intended only for the trusted test LAN.
- `make build/catalog-probe.elf`: builds a diagnostic that reads installed
  PS4 SFO and PS5 JSON title metadata without modifying it.
- `make inject-probe`: injects a metadata inspection thread into `SceShellUI`.
  It resolves the React native text setter without installing hooks. Its log is
  `/system_tmp/hltb-shell-probe.log`. The thread remains inert until reboot;
  the tool refuses another injection into the same live ShellUI process.
- `tools/capture_module.py MODULE.dll.sprx`: captures embedded managed
  metadata from a loaded ShellUI module, using the read-only HTTP probe.
  Captures are saved under ignored `build/captures/` for local investigation.

See [docs/PROGRESS.md](docs/PROGRESS.md) for evidence, limitations and next steps.
