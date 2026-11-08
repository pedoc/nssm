# Scope and production safety

On 2026-11-08 the user confirmed that no services depend on the new executable. Backward CLI/SCM compatibility is not a gate for the GUI-first prototype.

No-argument / Explorer startup opens WinUI. Exact `--ui` is an alias; other argument-based commands currently reuse the native core without a compatibility promise. Do not install this executable as a no-argument SCM service host.

Production root files, binaries, releases, Scoop and existing local service registrations must not be changed or migrated automatically. The 41-file SHA256 baseline protects that boundary; it is not proof of feature parity.

Old Reference/CLI/service smoke tools are historical experiments, not automatic CI gates for this GUI-first direction. Never execute local install/edit/start/stop/remove probes. Native installation also rewrites the shared NSSM Event Log message registration; an unauthorized review did so and was disclosed and recovered with user approval, as recorded in validation.md.
