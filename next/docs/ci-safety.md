# Service manager CI and release safety

## Current scope — 2026-11-09

The user requested README screenshots and automatic build/Release publication, then explicitly authorized creating a PR, merging it after checks pass, and publishing a stable Next tag from the merged main commit. No Scoop publication is included. Branch pushes build artifacts only. Deliberately pushed `next-vX.Y.Z` tags publish stable Next releases; suffixed tags publish prereleases. Legacy `v*` tags and `.github/workflows/build.yml` remain untouched.

## Build and packaging

`next-release.yml` builds the production GUI and native host on GitHub-hosted Windows, runs source/safety contracts and routing/memory-only configuration checks, then invokes `package.ps1`. This pipeline never launches a GUI or creates a service. It packages the complete production runtime, resources, documentation and package licences, excluding test/debug/build outputs. x64 PE/subsystem checks and ZIP-entry SHA256 verification run before artifact upload.

The build job has contents-read-only permissions and does not persist checkout credentials. Only the tag-gated release job has contents-write permission and a token-bearing step. That job creates a draft without overwriting existing assets, downloads and verifies its checksums, then publishes the stable/prerelease type selected by the tag suffix and verifies both its metadata and public downloads. Stable tags must be reachable from `origin/main`; all Next releases retain `latest=false` to keep legacy NSSM downloads separate. Failed draft recovery remains an explicit action, not an automatic delete/recreate loop.

## Isolated real-service integration

The earlier explicit authorization to exercise real CRUD/control for unique temporary NssmNext services remains limited to a disposable GitHub-hosted Windows runner on the experimental branch. `next-compat.yml`, `run-managed-integration.ps1` and the native integration program retain their repository/ref/run ID, prepared RUNNER_TEMP sandbox and elevation guards. Tags, pull requests, local runs and self-hosted runners cannot enter this path. Guards prevent accidents, not malicious environment spoofing; never fake them locally.

Real test operations use only a generated `NssmNextMgmtCI_<runid>_<nonce>` name. Ownership-marker fault probes verify instance GUID and ImagePath before writing. Failure attempts removal only through the known managed identity; cleanup failure remains a failed run bounded by VM teardown. Service application/host files outside the test's own deployment are not deleted. Private NssmNext registration and protected host cache live only on that disposable VM.

The probe covers owned listing, duplicate/stale/concurrent-write rejection, account/configuration behavior, arguments/CWD, restart, stop/start and actual SCM deletion. It compares the legacy NSSM event source to ensure it was not rewritten. Its evidence upload is limited to test logs/markers/result JSON with seven-day retention. This integration workflow remains contents-read-only and cannot publish releases.

## Local documentation and manual acceptance

The two README screenshots are taken by `capture-docs.ps1` from a freshly spawned, separately compiled memory-only UI. The script verifies the binary path, memory title, PID and native editor-owner relationship, opens only the blank application page, and captures only owned HWNDs. It never saves configuration, opens credentials or operates real services. Captures were visually inspected; this is documentation evidence, not a claim of complete advanced-feature acceptance.

Routine UI acceptance remains manual at the user's request. Local verification never authorizes real service CRUD, UAC elevation, ACL repair, registry changes or new accounts. A prior shell-capable reviewer violated that boundary; the disclosed, user-approved recovery remains recorded in validation.md. Historical authorization/validation records must not be erased or reinterpreted as blanket permission.
