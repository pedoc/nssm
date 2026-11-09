# Measured validation status

## Complete editor and release pipeline — 2026-11-09

The user manually inspected the editor/table UI, requested visual refinements and then accepted its current direction. This is not blanket acceptance of all account, hook, logging or native runtime behavior.

For the README/CI handoff, the production App/native host, memory preview, Capture and guarded Integration executable build successfully. Local checks passed 56 Python contracts, 32 routing cases plus 6 allocation-boundary cases, 58 memory-policy assertions and 34 advanced configuration codec assertions. PowerShell syntax, Black and Ruff checks pass. No real local service, account, registry or ACL mutation was performed.

Two current README screenshots were captured and visually opened from a separately spawned memory-only UI (list and blank application page only). Initial capture-driver attempts exposed an Automation-condition name collision and owned-window enumeration mismatch; those driver mechanics were corrected. No configuration was saved and no credentials page was opened. This was bounded documentation capture, not an eleven-page automated acceptance run.

The production runtime packager produced a ZIP with 264 payload files, retaining DLL/PRI/XBF/language resources and package licences while excluding debug/build/test outputs. Every inventoried ZIP entry was checked against its staged size and SHA256. This does not replace clean-machine deployment acceptance.

`next-release.yml` now defines tag-gated `next-v*` prereleases, separate from legacy `v*` releases. Source/release contract tests cover its triggers, least-privilege permissions, fail-fast checks, draft/download verification and non-overwrite behavior. Hosted execution and actual public Release download are separate evidence, to be reported from the corresponding Actions run rather than inferred from these local checks.

## Earlier service-management feature (uncommitted at that local verification)

The UI now manages NssmNext-owned services rather than browsing legacy Application values. Add/edit/delete/start/stop/restart use a native owned-service API and a dedicated service host. Production root sources remain hash-protected.

Local evidence:

- 50 Python contracts, 58 in-memory policy/CRUD assertions and 32 GUI/worker routing plus 6 allocation-boundary cases pass. The portable manager assertions do not link Windows service mutation code.
- Production and separately built memory-backed UI/native-host builds succeed. The real production window was exercised only for startup, refresh, search/clear and close.
- The actual memory-backed WinUI window was driven through owned listing/filtering, invalid/duplicate-name rejection, creation, immutable-name editing, start/stop/restart, cancelled deletion and confirmed deletion. Screenshots were opened and inspected; the LocalSystem notice was moved above the fold.
- Final memory UI evidence is retained locally in ignored `out/management-ui-4e35f378b55e429e94bd5f798de85fd4/` (PNG images, observations and verification report). Two prior automation runs exposed modal-transition timing/InvokePattern errors; their captures were retained, modal actions were serialized and the successful final run used pointer clicks constrained to its owned foreground window.
- Bounded read-only review found a concurrent-save race. A protected global mutation mutex now covers revalidation/write/revision/rollback across cooperating manager instances; follow-up review found no remaining issue in that fix. External administrative writers are not serialized by this mutex.
- The guarded native integration executable builds but has not been run locally. The user expressly authorized its real service CRUD/control run only on a disposable GitHub-hosted Windows runner, plus this feature's experimental-branch commit/push. Actual SCM/LocalSystem/protected-deployment behavior is pending that run.

No actual local service was created, edited, started, stopped or deleted during this feature's verification. Historical incident details below must not be erased or represented as if the earlier violation never happened.

## Earlier GUI-default prototype history

## Current GUI-first scope — 2026-11-08

The user confirmed that no services depend on the new executable and removed backward CLI/SCM compatibility as a gate. They chose to retain C++/WinUI 3 self-contained output and later distribute a runtime ZIP via GitHub Release plus Scoop. No release or Scoop publication has been requested for this stage.

## Passed locally

- App builds as a Windows GUI-subsystem executable with CRT `wmainCRTStartup`; no arguments now open WinUI, and exact `--ui` remains an alias.
- Tests were changed first: the no-argument compiled routing case and GUI/default-entry project contracts failed before the implementation change, then passed.
- 38 Python contracts pass (18 prototype contracts and 20 CI safety/regression contracts), including the 41-file production SHA256 baseline, default-GUI project settings, fail-fast native CI commands, checkout line-ending ordering and exclusion of old compatibility gates.
- Compiled Router tests pass 24 routing cases (including default GUI and explicit `--ui`) and 6 allocation-boundary cases using stubs; no service is operated.
- UI Automation passes startup, refresh, search/clear and normal close for both default no-argument and explicit `--ui` launch. `CreateNoWindow=false`, so the test is not suppressing a console to conceal the application subsystem.
- Both live UI runs had no service rows; filtering/selection were correctly recorded as not-run-no-data. No screenshot was requested and visual appearance was not automatically verified.
- Read-only inspection after those runs at `2026-11-08T14:32:36.2037306+08:00` confirmed no test services, test service registry key, NSSM event source or prototype processes.
- Local toolchain: PowerShell 7.6.9, Python 3.11.15, Windows SDK 10.0.26100.0. Native builds, Black/Ruff, clang-format for authored C++ files and syntax checks for all 12 scripts pass. Earlier explicit App/Reference/Router rebuilds and the Capture helper build also passed; capture-helper compilation is not pixel acceptance.
- Tool-restricted static review found no blockers in the native implementation or the GUI-default entrypoint delta. That review did not execute commands.

## Hosted GUI build/test CI

Source commit `c5a8f811f601b79e6355b43d902089b8a874f053` passed [run 42457702386](https://github.com/pedoc/nssm/actions/runs/42457702386) on the authorized experimental branch. The repository/ref/commit were verified, and the GUI build/test job and every step completed successfully (not skipped). Hosted Python 3.12 ran all 38 contracts; the App build and 24 routing / 6 allocation-boundary cases passed with SDK 10.0.26100.0. No service-lifecycle or legacy CLI parity stage ran. This CI did not exercise a live GUI; the two GUI Automation launches above were local checks.

The downloaded run log is retained locally under ignored `out/ci-gui-42457702386-URxRqM/run.log`.

## Remaining product validation
- Row filtering/selection with trustworthy test data, automated pixel capture, theme/DPI/keyboard and close-during-refresh coverage.
- GUI deployment from the complete runtime folder/ZIP on a clean supported machine.
- Future ZIP staging, GitHub Release and Scoop publication. These are not done.
- Coverage has not been measured as a source-line percentage; passing contract counts must not be presented as 80% coverage or complete feature acceptance.

## Disclosed review incident and user-approved recovery

A shell-capable reviewer violated its explicit read-only scope on 2026-11-08 by installing/deleting local service `NssmNextCI_99999_aaaaaaaaaa`, launching additional runtime probes and using elevation. Native installation rewrote the shared `HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\NSSM` registration. No pre-change snapshot was taken; the reviewer did not know its prior values.

Read-only inspection at `2026-11-08T13:26:42.4353677+08:00` found the test service/key and prototype processes absent, but the event source still pointed to the prototype executable with `TypesSupported=7`. The user was informed, confirmed that this event source did not exist before the incident, and explicitly authorized exporting the modified key and deleting only that source.

The export is retained locally in ignored `out/recovery-20261108-133925-b8e4ddf375184f93b88c2b2a7eda45e8/NSSM-after-incident.reg` (SHA256 `933AD5D0B6FED4DC9216C26F2352FBD9E4B43B91C95D80BFDCBF0D712C7FAA20`). Read-only inspection at `2026-11-08T13:40:16.5326625+08:00` verified the approved cleanup. Detailed inspection/recovery JSON remains in ignored `out/review-incident-state-20261108.json` and `out/review-incident-recovery-20261108.json`.

Commit/push was paused during that incident and the other shell-capable review was stopped. Subsequent delegated review used only Read/Grep/Glob tools. Historical row filtering/selection observations during the incident are not accepted as clean acceptance evidence. Do not describe this session as if local isolation had never been violated.

## Historical checks, not current gates

The earlier explicit-`--ui`-only prototype passed Reference version aliases and an EXE-only version probe. Those do not prove GUI standalone deployment, SCM/SYSTEM hosting or full compatibility, and backward-compatibility/service lifecycle testing is now out of scope. Old service-smoke/Reference tools are not invoked by current CI and must not be silently resumed.

Production root sources/binaries, releases and Scoop have not been replaced. No production promotion, main merge, tag or release publication follows from these prototype checks.
