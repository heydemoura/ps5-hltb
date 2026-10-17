# Third-party code

- **ps5-payload-dev/sdk**, https://github.com/ps5-payload-dev/sdk,
  GPL-3.0-or-later (except its separately licensed headers). External build
  dependency. Native HTTPS initialization follows its `http2_get` sample,
  copyright 2024 John Törnblom.
- **cJSON 1.7.19**, https://github.com/DaveGamble/cJSON/tree/v1.7.19,
  MIT. Vendored unchanged under `third_party/cJSON`; license alongside source.
- **ps5-payload-dev/shsrv v0.20**, commit
  `6f320637d56d344a0e7797753099e33238bbf146`,
  https://github.com/ps5-payload-dev/shsrv,
  GPL-3.0-or-later, copyright John Törnblom. Ptrace and ELF-loading primitives
  under `third_party/shsrv`.
- **ps5-dualsense-overlay**, commit
  `e1de419707860a8da01d3604b0b5632771159756`,
  https://github.com/erickdavestech/ps5-dualsense-overlay,
  GPL-3.0-or-later. Loader bridge and thread bootstrap under
  `third_party/overlay-loader`, copyright porhe911 and erickdavestech.
- **GlobalSign Root R46**, public CA certificate, provenance and fingerprint
  in `certs/README.md`.

Sony firmware captures remain local in ignored `build/`. No firmware, managed
assemblies, home-screen bundles, database snapshots, or fonts are distributed.
