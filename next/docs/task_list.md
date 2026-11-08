# Tasks

## Current direction (user update 2026-11-08)

No services depend on the new executable. Backward CLI/SCM compatibility is not a gate. Keep C++/WinUI 3 self-contained deployment; later distribute a ZIP via GitHub Release and install/create shortcuts through Scoop.

- [x] Make no-argument / Explorer startup open the GUI without a console window; keep `--ui` as an alias.
- [x] Verify both default and explicit GUI startup, refresh, search/clear and normal close without creating services.
- [x] Remove old Reference/CLI parity and SCM lifecycle gates from automatic CI.
- [x] Record the runtime ZIP + future Scoop distribution decision; no single-EXE framework rewrite or self-extracting launcher.
- [x] Pass 38 Python contracts, 24 compiled routing cases and 6 allocation-boundary cases, native build, formatting/lint and script syntax checks.
- [x] Protect the 41 production-file hashes and original icon resources.
- [x] Obtain tool-restricted static review of the native implementation and GUI-default entrypoint delta.
- [x] Disclose the unauthorized local review incident and complete user-approved recovery; independently verify no test service/key, event source or prototype process remains.
- [x] Commit/push only the authorized experimental branch and verify its GUI build/test CI: source `c5a8f81`, run `42457702386` passed all steps, 38 contracts and 24 routing / 6 boundary cases.

## Follow-up product work

- [ ] Exercise row filtering/selection with trustworthy test data; contaminated historical local results are not acceptance evidence.
- [ ] Complete automated pixel capture, theme/DPI/keyboard and close-during-refresh coverage.
- [ ] Validate GUI deployment on a clean supported machine.
- [ ] Stage only required runtime files/notices into release ZIPs, excluding debug symbols and build intermediates.
- [ ] Publish GitHub Release assets and a Scoop manifest/shortcut when explicitly requested.
- [ ] Define future service hosting before enabling any service creation/control UI; the GUI-first executable is not a no-argument SCM host.

## Historical scope

The earlier explicit-`--ui`-only router, Reference version/EXE-only probes and hosted service-smoke experiments were for an earlier compatibility direction. They are not current acceptance gates and must not be silently resumed.

Production sources/binaries, existing local services, main, Release and Scoop remain outside the current prototype changes. No release publication or production replacement is authorized by these checks.
