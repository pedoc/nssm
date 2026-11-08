# GUI prototype CI safety

The user authorized commit/push only to `feat/winui3-unified-prototype`. They subsequently confirmed that no services depend on the new executable and removed backward-compatibility requirements. No main merge, tag, Release, Scoop update or local service mutation is authorized by this stage.

The workflow (historical filename `next-compat.yml`) now builds the GUI application and runs source, routing and safety tests. It does not install/start/stop/delete services, run the old Reference parity gate, or upload service evidence. Tests assert these exclusions. Native failure checks stop each grouped step immediately; checkout disables automatic line-ending conversion before SHA256 contracts.

The workflow is limited to the experimental branch/repository, uses a contents-read-only token and does not persist checkout credentials. A skipped job is not acceptance evidence. Manual dispatch may not register until the workflow exists on the default branch; branch push is the initial trigger.

`service-smoke.ps1` and its helpers are retained as historical experiments only. Their old no-argument service-host assumption does not apply to the GUI-first executable; do not execute them for this stage. Their environment checks are accident-prevention guards, not attestation or permission to modify a local machine.

A prior shell-capable reviewer violated the read-only boundary. The user was informed, approved a narrowly scoped recovery and the recovered state was independently checked. Later review was restricted to Read/Grep/Glob tools. See validation.md; do not claim that the violation never occurred.
