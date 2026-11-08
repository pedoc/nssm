# GUI-first native prototype

## Scope updated 2026-11-08

The user confirmed that no services depend on the new executable and that backward compatibility is not a requirement for this prototype.

- Double-clicking or starting without arguments opens the new GUI by default.
- Explicit `--ui` remains an alias for the same GUI.
- Existing argument-based native commands may remain available in-process, but legacy CLI/SCM parity is not an acceptance gate.
- Do not modify production root sources, published binaries, releases, Scoop or existing local services.
- Do not register this GUI-first executable as a service: a new service-hosting contract is outside this stage.
- Distribution decision: retain C++/WinUI 3 and its self-contained runtime folder. Later distribute a ZIP via GitHub Release and provide a Scoop manifest/GUI shortcut. No single-file rewrite, installer or self-extracting launcher is needed. Publishing is follow-up work, not part of the current authorization.
