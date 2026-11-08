---
name: verify
description: Build and drive the native WinUI service manager safely using a separate in-memory UI backend; capture screenshots and observations without touching real local services.
---

# NssmNext runtime verification

Run from the repository root in Git Bash, invoking written PowerShell scripts via `pwsh -NoProfile -File`.

## Full management UI — safe local surface

1. Build the separate test UI (this also builds the native host but does not run/register it):
   `pwsh -NoProfile -File next/build/build.ps1 -Project App -UiTestBackend`
2. Drive the actual window:
   `pwsh -NoProfile -File next/build/inspect-management-ui.ps1`
3. Read the emitted `EVIDENCE=` directory. Open its PNGs and `observations.json`; UI Automation success alone is not visual acceptance.

The driver exercises owned-only listing, filtering, required-field errors, create, edit, duplicate-name rejection, start/stop/restart, cancel deletion and confirmed deletion. All writes are to the process-local memory backend. It checks the executable is under `next/out/UiTests/` and the window explicitly says `内存测试模式` before invoking any write button.

The production build has no runtime switch to enable this backend. `-UiTestBackend` is a build-time option with a separate output directory; screenshots must visibly show the memory-test banner.

## Production GUI — read-only local surface

Build: `pwsh -NoProfile -File next/build/build.ps1 -Project App`

Drive only startup, refresh, search/clear, selection and normal close:
`pwsh -NoProfile -File next/build/smoke-ui.ps1`

The default/no-argument route opens the GUI; `-ExplicitUi` verifies its `--ui` alias. Only the process started by the driver may be closed. No name-based process sweep.

## Runtime capture

The memory driver captures the owned HWND using `next/out/Tests/CaptureWindow.exe` when available, otherwise its foreground window region. Capture helper build: `pwsh -NoProfile -File next/build/build.ps1 -Project Capture`.

Look at the images. A blank capture is not visual proof; retain raw output and report the limitation rather than inferring pixels from the accessibility tree.

## Hard boundaries

- Never install, edit, start, stop or delete real local services as a verification probe. Never elevate for local verification.
- Never invoke the old NSSM CLI to set up fixtures. Real GUI operations use the native owned-service API, not legacy CLI forwarding.
- The service host only accepts its internal `--service` mode; do not launch that mode manually for local testing.
- Actual SCM/LocalSystem/host-deployment integration needs explicit authorization for a disposable environment. Memory UI success does not prove it.
- No automatic commit/push, Release, Scoop change or registration repair is part of verification.
