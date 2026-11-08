# Service manager CI safety

The user explicitly authorized committing/pushing this feature only to `feat/winui3-unified-prototype` and exercising real CRUD/control for unique temporary NssmNext services on a disposable GitHub-hosted Windows runner. This does not authorize local service mutation, main merge, tags, Release or Scoop publication.

The workflow builds GUI/native-host binaries, runs policy/routing/source checks, then invokes `run-managed-integration.ps1`. Both that script and the native integration program reject execution unless GitHub-hosted Windows, repository/ref/run ID, prepared RUNNER_TEMP sandbox and elevation checks match. Guards prevent accidents, not malicious environment spoofing; never fake them locally.

Real test operations use only a generated `NssmNextMgmtCI_<runid>_<nonce>` service name. Ownership-marker fault probes verify instance GUID and ImagePath on an open service object before writing. Failure attempts removal only through the known managed identity; cleanup failure remains a failed run bounded by VM teardown. Service application/host files outside the test's own deployment are not deleted.

The probe verifies owned listing, duplicate/stale/concurrent-write rejection, LocalSystem execution, spaced arguments/CWD, native target restart, stop/start and actual SCM deletion. It compares the legacy NSSM event-source snapshot to ensure it was not rewritten. Private NssmNext event registration and protected host cache live only on the disposable VM during this test.

Workflow permissions are contents-read-only, checkout credentials are not persisted, and evidence upload is limited to native test logs/markers/result JSON with seven-day retention. No release action or legacy Reference/service-smoke gate is used. A skipped job is not success evidence.

Local verification remains memory-only for writes and read-only for the production GUI. A prior shell-capable reviewer violated that boundary; its disclosed, user-approved recovery is retained in validation.md. Later reviews were explicitly read-only and their executed scope was reported.
