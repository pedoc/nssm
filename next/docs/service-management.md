# NssmNext service management

## Goal

Replace the read-only legacy-service browser with a working manager for services created by NssmNext: list, create, edit, delete, start, stop and restart. Existing production NSSM services are not imported, rewritten or controlled automatically.

## Model and ownership

- Store `NssmNextOwner=NssmNext`, schema version 1, a generated per-service instance GUID and revision under that service's Parameters key.
- A managed service must also use the dedicated `NssmNext.ServiceHost.exe --service` protocol. An Application registry value alone does not establish ownership.
- Before every write/control/delete, acquire a protected cross-process management mutex and re-read ownership, GUID and ImagePath; editing additionally rejects a stale revision/configuration. Hold the lock through write/rollback. This serializes cooperating manager processes, not arbitrary external administrative writers. Never identify a mutable service by list position or name alone.
- Basic editable fields: service name (creation only), display name, executable, arguments, working directory, description and startup mode. Application/arguments are stored literally. Advanced credentials/environment/I/O pages are separate follow-up work.
- New registrations use LocalSystem, which must be clearly disclosed in the editor; creating a service does not automatically start it. The selected application must be trusted and its permissions considered by the administrator.

## Service hosting and deployment

- Keep the GUI-first `NssmNext.exe` and its complete WinUI runtime folder. Add a dedicated native `NssmNext.ServiceHost.exe`, reusing the unchanged native supervision engine, without WinUI runtime requirements.
- GUI create/update/control operations call Win32 APIs directly, never spawn the legacy NSSM CLI. Remove GUI forwarding of legacy mutating CLI commands.
- Deploy the service host into an administrator/SYSTEM-owned, non-reparse protected Program Files directory, versioned by content hash. This avoids binding SYSTEM services to a mutable per-user Scoop directory and avoids overwriting running host versions.
- Suppress the legacy engine's shared NSSM event-source registration. Only an actual SCM service callback registers/logs to the separate NssmNext event source. Production NSSM event registration must remain untouched.
- Creation failure compensates by deleting only the newly created service handle. Edit failure restores the relevant original configuration/registry values when possible and reports incomplete rollback explicitly.
- Deletion confirms stop/removal, waits for a safe stopped state, and removes only the owned service registration. Application, logs and host-cache files are not deleted.

## UI and privileges

- Show only NssmNext-owned services, with empty/search states and selected configuration/status details.
- Provide Add/Edit/Delete/Start/Stop/Restart buttons and a configuration dialog. Delete requires confirmation. Refresh after successful mutations and preserve selection by service identity.
- Normal startup remains non-elevated for viewing. Writes explicitly offer a UAC restart; no passwords or configuration secrets are placed in elevation arguments.
- Run blocking operations off the UI thread. Prevent window closure during a committed write, serialize operations and handle errors visibly.

## Validation boundary

- Native policy and in-memory CRUD tests do not link the Windows service mutation implementation.
- Build a separate, clearly labelled test-only UI backend for full CRUD interaction tests. The production build has no runtime switch to activate this backend.
- Local default UI tests only read real system state. Do not create/edit/delete/start/stop real local services for validation.
- The user explicitly authorized this feature's experimental-branch commit/push and real CRUD/control verification on a disposable GitHub-hosted Windows runner. Both parent script and native probe enforce branch/repository/environment/sandbox guards. No local real service mutation, main merge, release or Scoop publication is authorized.

## Work in progress

1. Policy/model, ownership and in-memory tests.
2. Win32 storage/CRUD/control, protected host deployment and service host.
3. Management UI and privilege handling.
4. Test-only UI CRUD automation, native builds and review.
5. Report measured results and any pending isolated integration checks.
