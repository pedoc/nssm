# Architecture

- Core compiles the unchanged production native sources as a static library, plus NssmNext ownership/validation and Win32 management adapters. The 41-file root baseline remains protected.
- GUI: `NssmNext.exe`, Windows subsystem with CRT startup, opens WinUI by default and for `--ui`. It does not forward legacy mutating CLI commands.
- Service host: `NssmNext.ServiceHost.exe --service`, a separate native executable with no WinUI dependency. It reuses the native supervision engine; per-file compiler symbol adapters suppress shared NSSM event registration and route actual SCM callbacks/events to the private NssmNext source.
- Management: Win32 APIs implement owned-only list/create/edit/delete/start/stop. Metadata includes product/schema/instance GUID/revision, and the ImagePath must use the dedicated host protocol. Mutations revalidate identity under a protected cross-process mutex; edits reject stale configuration and compensate failures.
- Host deployment: content-addressed copies in a protected, non-reparse Program Files directory with SYSTEM/Administrators write access and standard-user read/execute. Existing running versions are not overwritten.
- Privileges: viewing stays non-elevated; explicit writes offer a UAC relaunch. No credentials are passed through command arguments.
- Verification: portable policy/in-memory tests do not link service mutation code. A separate build-time UI backend drives real controls using only memory data. Actual SCM integration is guarded to an explicitly authorized disposable GitHub runner.
- Distribution remains a complete WinUI runtime ZIP plus native host, with future GitHub Release/Scoop packaging. No single-EXE rewrite or current production publication.
