# Product scope and production safety

The user confirmed that no services depend on the new GUI executable. Backward CLI/SCM compatibility is not a gate. No old NSSM service is automatically imported, migrated or reconfigured.

`NssmNext.exe` opens the manager by default or with `--ui`, accepts read-only version aliases, and rejects legacy mutating CLI commands. New services use the separate `NssmNext.ServiceHost.exe --service` protocol and instance metadata. The GUI executable itself is not a no-argument SCM host.

Production root files, published binaries, main, Release, Scoop and existing local services remain protected. The 41-file SHA256 baseline checks root immutability, not feature parity. Native logging is routed to the separate NssmNext event source instead of rewriting the old NSSM provider.

Real integration now validates new owned-service functionality only on an explicitly authorized disposable runner; it is not a return to the old compatibility-testing requirement. Historical Reference/service-smoke tools are not automatically invoked.

Never use actual local install/edit/start/stop/remove probes for verification. The earlier unauthorized local review incident and its user-approved recovery remain documented in validation.md.
