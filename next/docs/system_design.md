# Runtime modes and service model

1. `NssmNext.exe` without arguments, or exact `--ui`: open the WinUI manager. Read-only version aliases print version information. Other legacy CLI commands are rejected rather than forwarded to the old mutating entry point.
2. `NssmNext.ServiceHost.exe --service`: dedicated SCM-only hosting path using the existing native supervision engine. No GUI runtime is loaded. Do not manually use this internal mode for local verification.
3. Services created by the GUI point at a protected content-addressed copy of the native host, never the per-user GUI/Scoop folder.

Ownership consists of NssmNext product/schema, instance GUID, revision and host protocol. Every mutation re-reads identity under a cross-process management mutex. The mutex serializes cooperating NssmNext processes; external administrative SCM/registry writers are not serialized and still require coordination. Editing checks the captured configuration and revision again before writing.

Creation failure attempts removal through only the freshly created service handle. Edit failure attempts configuration/registry restoration and reports incomplete recovery. Deletion stops and removes only owned registration, without deleting application/log/host files.

The GUI serializes background operations, disables parent actions during dialogs and prevents close during committed writes. Admin relaunch is explicit and passes no secrets/configuration. New services use LocalSystem, visibly disclosed at the top of the editor, and are not auto-started.

The old shared NSSM event-source registration is suppressed in the experimental core. Only actual SCM callbacks register the separate NssmNext event source.
