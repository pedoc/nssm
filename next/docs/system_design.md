# Runtime modes

1. No arguments, including Explorer double-click: initialize the desktop runtime and show the read-only GUI.
2. Exact `--ui`: alias for the same GUI.
3. Other argument-based commands: reuse the native implementation in-process; backward compatibility is not a release gate for this new executable.
4. Service hosting: not defined for the GUI-first prototype. Do not install the prototype executable as a no-argument SCM service host.

The user confirmed on 2026-11-08 that no services depend on the prototype. Do not add legacy service autodetection or a migration system for services that do not exist.

No prototype code may install, edit, stop, remove or migrate existing local services automatically. GUI management actions remain unavailable in this read-only stage.

Never send passwords through command arguments, logs or plaintext registry settings. Future privileged GUI APIs require a reviewed native management interface.
