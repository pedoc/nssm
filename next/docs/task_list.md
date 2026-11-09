# Tasks

## Service-management feature

- [x] Replace legacy Application-only listing with NssmNext ownership/schema/instance/host filtering.
- [x] Implement basic add/edit/delete and start/stop/restart native operations with identity checks, stale-edit rejection and compensating cleanup.
- [x] Add protected cross-process mutation serialization, retaining the lock through revalidation/write/rollback.
- [x] Add a standalone native host, private event logging and protected content-addressed host deployment.
- [x] Wire WinUI management buttons, configuration dialog, explicit elevation, confirmation and busy/modal lifetime guards.
- [x] Observe actual memory-backed UI list/filter/create/edit/duplicate rejection/start/stop/restart/cancel/delete, with readable screenshots.
- [x] Put the LocalSystem warning at the top of the editor and label the host PID accurately.
- [x] Build the production UI and observe read-only refresh/search/close without service mutations.
- [x] Pass 50 source/safety contracts, 58 portable management assertions and 32 routing / 6 allocation-boundary cases.
- [x] Obtain bounded read-only review; fix the concurrent-update finding and verify the fix statically.
- [x] Receive explicit user authorization for experimental-branch push and real integration on disposable GitHub-hosted Windows only.
- [ ] Run real isolated SCM create/edit/concurrency/identity/start/restart/stop/delete checks and retain evidence.
- [ ] Commit/push only the authorized experimental branch after preflight.

## Follow-up product work

- [ ] Advanced logon credentials, environment and I/O settings UI.
- [ ] Additional keyboard, accessibility, DPI/theme and abnormal-close coverage.
- [ ] Clean-machine ZIP GUI deployment validation.
- [ ] Stage required runtime files/notices and native host into release ZIPs, excluding build/debug/test artifacts.
- [ ] Publish GitHub Release and Scoop manifest/shortcut only when explicitly requested.

## Boundaries/history

Default startup is GUI-first; old CLI compatibility is not a gate. Current service-hosting tests exercise the new manager, not the obsolete legacy service-smoke experiment. No existing production NSSM service is imported or migrated.

The previous unauthorized local review incident was disclosed and recovered with user approval. Local verification now uses the separately built memory-only UI backend; actual service mutations must not be run locally. Main, production root sources, Release and Scoop remain outside current changes.
