# Architecture

- Core: static library compiling the root native sources without editing them. Rename only the original wmain symbol through per-file compiler definitions.
- App: Windows-subsystem executable using the CRT's `wmainCRTStartup` entry to retain a testable wide-argument router without creating a console window.
- Default/no-argument startup and explicit `--ui` initialize WinUI and open the read-only desktop window.
- Other argument-based commands still enter NssmLegacyMain in-process. This is reuse, not a promise of old CLI/SCM compatibility.
- Resources: reuse the original icon, dialogs, version and message resources.
- Deployment remains self-contained C++/WinUI output with Windows App SDK DLLs/resources. The user accepted a runtime ZIP for future GitHub Release distribution and Scoop installation; no framework change or self-extracting bundle is planned.
- Existing production services and release assets remain untouched. This GUI-first prototype must not be registered as a service using a no-argument ImagePath.
