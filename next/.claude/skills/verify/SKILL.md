---
name: verify
description: Build the native WinUI manager and check safe packaging; capture memory-only documentation screenshots when requested, leaving UI acceptance to the user.
---

# NssmNext verification and handoff

Run written PowerShell scripts from the repository root with `pwsh -NoProfile -File` in Git Bash.

## Default scope

The user prefers implementation, necessary compilation and a handoff for manual UI acceptance. Do not spend time driving every editor page, repeatedly taking acceptance screenshots, or debugging the legacy full-UI drivers. Compilation and passing contracts are not claims that every native service behavior has been accepted.

Before a code commit, confirm the current source has built successfully. For CI/release changes, also run the non-mutating source/release contracts, script parser, routing/memory-only tests and package verification. Reuse successful results from the current unchanged code rather than repeating long builds solely for ceremony.

- Build normal app/host: `pwsh -NoProfile -File next/build/build.ps1 -Project App`
- Source/release contracts: `python -IB -m pytest -q -p no:cacheprovider next/tests`
- Script parser: `pwsh -NoProfile -File next/build/check-syntax.ps1`
- Router: build `-Project Router`, then `next/out/Tests/RouterTests.exe`.
- Memory policy/codec: build `-Project Management`, then `next/out/Tests/ManagementTests.exe`.
- Package: `pwsh -NoProfile -File next/build/package.ps1 -Version snapshot-check -OutputDirectory next/out/package-check-new`

Use a new output directory; the packager deliberately refuses to overwrite an existing one. It copies only the canonical production runtime, includes resources/licences, excludes test/debug outputs and verifies ZIP-entry hashes. It does not launch the packaged program.

## Documentation screenshots — only when requested

1. Build `-Project App -UiTestBackend` and `-Project Capture` using `next/build/build.ps1`.
2. Run `pwsh -NoProfile -File next/build/capture-docs.ps1 -OutputDirectory next/out/readme-images-new` with a new empty output path.
3. Open both PNGs and inspect them. A blank image is not evidence. Only then copy them to `next/docs/images/` and accurately label the screenshots as memory-only examples.

The capture script checks the dedicated UiTests path, spawned PID, memory title/banner and native editor-owner relationship. It opens only the list and blank application page; it does not save or open credentials. It captures the owned HWND with `CaptureWindow.exe`, not unrelated desktop content. Only its own spawned process may be closed.

The memory backend is compile-time-only, lives under `out/UiTests`, and cannot be enabled through a production runtime switch. The legacy `inspect-management-ui.ps1` / `inspect-full-editor.ps1` drivers may target old layouts; they are not default acceptance gates.

## Hard boundaries

- Never install, edit, start, stop or delete real local services as a verification probe. Never elevate for local verification.
- Never adjust local ACLs, registry entries or accounts as a verification workaround.
- Never invoke old NSSM mutating CLI commands or manually start the service host's internal `--service` mode.
- Real SCM/LocalSystem/deployment integration remains limited to the separately authorized disposable GitHub-hosted experimental-branch runner; never spoof its guards locally.
- Password values must not be read, logged or captured in screenshots.
- Commit/push, tags, Release, Scoop or repair actions require user authorization outside this skill. A branch push does not itself authorize choosing and publishing a version tag.
- Report what ran, what passed, and what remains for manual acceptance. Retain the disclosed historical incident/recovery record.
