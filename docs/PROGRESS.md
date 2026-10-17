# Goal and evidence ledger

Full goal: query HowLongToBeat by game title; display Main Story completion
in the PS5 shell with the native game-title font; build with the GitHub SDK;
test on `192.168.0.190`; publish the source in a private GitHub repository.

## Verified on 2026-10-17

- Workspace initially empty. Private repository created at
  https://github.com/heydemoura/ps5-hltb (visibility PRIVATE).
- Existing SDK installed at `~/opt/ps5-payload-sdk`; source inspected from
  https://github.com/ps5-payload-dev/sdk. SDK builds x86-64 PS5 ELF payloads
  on this macOS ARM host with Homebrew LLVM.
- elfldr reachable on `192.168.0.190:9021`; deployed payloads execute and
  stream stdout back to the host. Firmware reports `0x12200005` (12.20).
- Native PS5 HTTPS lookup returns:
  ```text
  Querying HowLongToBeat by title: Elden Ring
  HLTB game=68151 title=Elden Ring seconds=216464
  Main Story: 60h 08m
  ```
  This was a live request from the console using `sceHttp2`, not a fixture or
  PC proxy. Initial TLS error `0x8095f00c` was resolved by loading GlobalSign
  Root R46 via `sceSslLoadCert`. Certificate checks remain enabled.
- Host sanitizer tests cover title disambiguation, trademark handling,
  missing/zero/invalid data, JSON escaping, time formatting and small buffers.
- Console catalog probe reads 31 installed titles from PS4 SFO and PS5 JSON.
  These include homebrew tiles, which have no HowLongToBeat entry and should
  remain unannotated.
- Metadata probe injected into live `SceShellUI` PID 101:
  ```text
  attach=1 mapped=1 imports=16 unresolved=0 first= thread=1 rc=0 detach=1 auth-restored=1
  entered pid=101
  root domain=880b10000
  assembly=88585d000 image=8859a1800 class=8876e49a0 method=88799e978 code=80c12e850 text-offset=136
  parked; no patches installed
  ```
  ASLR addresses are session evidence, never reusable firmware offsets.

## Shell investigation

This firmware runs the home UI as `rnps-home` (`NPXS40002`), Hermes backend,
with native React/PUI components in SceShellUI. SceShellCore is a separate
service process. User clarification about the literal ShellCore requirement
is pending; do not silently claim ShellUI equals ShellCore.

REA metadata evidence:

- AppSystem identity/metadata: `ev_6e0f154d798fb7f724a5876cef27a7242ac2d7914f92e657715699f3f88d05b7`.
- ReactNative.PUI member inventory: `ev_cf77ceae72a5397a68e63dff4fee19b05df75ef294b7869a3349105f86a775af`.
- Shell React application inventory: `ev_91cac0587e7a908de8d434701b0d3f4e2e5176b21cdd23f758951d56684da6af`.
- Selected text-node signatures: `ev_f147c7fc3f7b88c2673e61814eb65ea8cad4ea263e740aa3c168c7fe3b4325cc`.

Managed images contain metadata and stub CIL (`ret`), with implementation in
native AOT code. REA signatures establish types and member names, not their
runtime behavior. Live mono compilation resolved
`ReactNative.Views.UI3.View.ReactRawTextShadowNode.SetText(string)` to native
code, and the `_text` field offset to 136. A host Capstone inspection of that
native code shows a managed string assignment, GC write barrier, and virtual
update call. This does not yet establish which node is the visible game title.

Native REA decompilation was unavailable because no compatible Ghidra or IDA
provider was configured; ordinary Capstone disassembly was used for the narrow
SSL signature and text setter inspections. Sony captures remain ignored and
must not be committed. The diagnostic injected thread stays inert in ShellUI.

## Required remaining work

1. Resolve the ShellCore/ShellUI process requirement.
2. Identify the visible selected-game title node at runtime and its scope.
3. Implement rendering through that native text/font path, including title
   changes, asynchronous lookup, caching, missing data, and clean disable.
4. Verify on-screen rendering and title switching on the actual PS5.
5. Push the finished source and verify remote privacy and commit state.

The goal remains active. Direct HTTPS lookup and metadata injection alone do
not satisfy the native on-screen requirement.
