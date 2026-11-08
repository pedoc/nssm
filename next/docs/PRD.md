# NssmNext GUI service management

## Current scope

The user expects a working service manager, not a read-only browser: show NssmNext-maintained services and support add/edit/delete/start/stop/restart directly in the GUI. No old services depend on this new executable, and backward CLI/SCM compatibility is not a gate.

- Default/no-argument startup and `--ui` open the WinUI manager.
- List only services with valid NssmNext metadata and dedicated native-host protocol; do not import or rewrite old NSSM services.
- Add/edit the executable, arguments, working directory, display name, description and startup mode. Name is immutable on edit. New services are not auto-started.
- Require explicit user actions/permissions for writes; confirm deletion/restart and preserve application/log files.
- Keep production root sources, existing services, main, releases and Scoop unchanged.
- Retain C++/WinUI 3 self-contained deployment. Later publish runtime ZIPs via GitHub Release and a Scoop manifest/shortcut; no installer/self-extractor/framework rewrite.

The user authorized this feature's experimental-branch push and real CRUD integration only on a disposable GitHub-hosted Windows runner. No actual local service mutation, main merge, tag, release or Scoop publication is authorized.

Advanced account credentials, environment and I/O pages are follow-up work, not part of the current basic editor.
