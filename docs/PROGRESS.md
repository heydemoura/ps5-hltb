# Implementation ledger

The original scope is preserved: title-based HowLongToBeat Main Story queries,
native title-font rendering on the PS5 shell, SDK build, console testing and a
private GitHub repository. The user approved SceShellUI for the rendering
requirement; this firmware's SceShellCore does not own the home title widgets.

Implemented paths:

- `src/hltb.c`: request construction, strict game-title matching, Main Story
  parsing and time formatting.
- `src/http_ps5.c`: direct PS5 HTTPS transport, search token and token refresh.
- `src/catalog*.c`: read-only installed-game title discovery and controller-to-
  ShellUI catalog transfer.
- `src/cache.c`: title-bound, expiring successful lookup cache.
- `src/shell_native.c`: asynchronous lookup worker and native text annotation.
- `tools/inject_native.cpp`: guarded ELF injection, validated trampolines,
  verified patch writes, rollback and native-byte restoration.
- `build/hltb.elf`: main single-payload deliverable, built from source.

See [VALIDATION.md](VALIDATION.md) for current evidence and practical limits.

## Retained reverse-engineering evidence

Local Sony captures remain ignored under `build/`. The managed assemblies
contain metadata/stub CIL and native AOT implementations. Metadata names alone
were not treated as implementation proof; live Mono compilation resolved the
methods and native memory inspection verified their instruction sequences.

REA records used during investigation:

- AppSystem: `ev_6e0f154d798fb7f724a5876cef27a7242ac2d7914f92e657715699f3f88d05b7`.
- ReactNative.PUI: `ev_cf77ceae72a5397a68e63dff4fee19b05df75ef294b7869a3349105f86a775af`.
- Shell React app: `ev_91cac0587e7a908de8d434701b0d3f4e2e5176b21cdd23f758951d56684da6af`.
- Text types: `ev_f147c7fc3f7b88c2673e61814eb65ea8cad4ea263e740aa3c168c7fe3b4325cc`.
- React contexts: `ev_ed99b78b7865dbdd409c35525d3901fe659c34aacdede6b6cb2281789cdda5bc`.

REA native decompilation was unavailable because no compatible deep provider
was configured. Ordinary Capstone disassembly was used for narrow native SSL
signature and method-prologue inspections. It is distinguished from REA
metadata evidence and from the actual console execution tests.
