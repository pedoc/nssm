# NSSM Next — native WinUI service manager

Experimental C++/WinUI 3 management UI with a dedicated native service host. This is not a released production replacement; existing NSSM services are not imported or changed automatically.

## Management UI

- Double-click `NssmNext.exe` (or use `--ui`) to open the GUI without a console window.
- List only services with NssmNext ownership metadata, a per-service instance identity and the dedicated host protocol. An old NSSM Application registry value alone is not ownership.
- Add/edit application path, arguments, working directory, display name, description and startup mode. Service names are immutable after creation.
- Start, stop, restart and delete owned services; restart/delete require confirmation. Deletion removes service registration, not application/log/host files.
- New services use LocalSystem and are not automatically started. Only select trusted applications and consider their file permissions.
- Viewing is non-elevated. Write operations explicitly offer a UAC restart; they do not automatically run on startup.

Advanced account credentials, environment and I/O settings are not yet exposed in this basic editor. Real SCM integration validation is separate from memory-backed UI verification.

## Native service hosting

`NssmNext.ServiceHost.exe --service` is the internal SCM host. It reuses the existing native supervision engine, without WinUI runtime dependencies. GUI service creation deploys its content-addressed version into a protected Program Files folder, rather than registering a SYSTEM host inside a mutable per-user Scoop directory.

The GUI does not forward legacy install/edit/remove CLI commands. The old engine's shared NSSM event registration is suppressed; actual service callbacks use a separate NssmNext event source. No existing service is migrated.

## Build and local verification

Requires Visual Studio native C++ tools and a Windows SDK; the prototype currently targets x64 and Windows 10 build 19041 or newer.

```powershell
pwsh -NoProfile -File next/build/build.ps1 -Project App
```

The App build also builds the native host. GUI output is `next/out/x64/Release/NssmNext.exe`; keep its accompanying runtime files and `NssmNext.ServiceHost.exe`.

Safe local policy and interface verification:

```powershell
pwsh -NoProfile -File next/build/build.ps1 -Project Management
./next/out/Tests/ManagementTests.exe
pwsh -NoProfile -File next/build/build.ps1 -Project App -UiTestBackend
pwsh -NoProfile -File next/build/inspect-management-ui.ps1
```

The separately compiled UI test backend lives under `out/UiTests`, is visibly marked as memory-only, and never creates or controls real services. Production builds have no runtime switch to activate it. The production `smoke-ui.ps1` only reads service state and exercises refresh/search/close.

If NuGet access fails, use `-Proxy http://127.0.0.1:10808`. Dependencies and generated output remain inside `next`. Format only prototype C++ files with `next/.clang-format`; production root files are hash-protected.

## Distribution and safety

Keep WinUI self-contained deployment. Later publish the required runtime folder as a GitHub Release ZIP and add a Scoop manifest/GUI shortcut. Exclude debug/build intermediates, not required DLLs/resources or the native host. No single-file rewrite or self-extractor is planned.

Local verification never authorizes real local service CRUD or elevation. The user separately authorized this feature's experimental-branch commit/push and real Windows service/LocalSystem/deployment integration on a disposable GitHub-hosted Windows runner. No main merge, Release or Scoop publication is authorized. See `docs/service-management.md` and the recorded validation/incident history in `docs/validation.md`.
