# NSSM Next — GUI-first native/WinUI prototype

This is an unreleased, read-only prototype. Production root sources, current releases, Scoop and existing local services are not replaced or migrated.

## Design

- One C++ application links the existing native sources as a static library; it does not launch an old NSSM backend executable.
- Double-clicking `NssmNext.exe` or starting without arguments opens the new WinUI interface. `--ui` remains an alias.
- The application uses the Windows GUI subsystem with CRT startup, so normal desktop launch does not create a console window.
- Other argument-based commands currently reuse the native implementation in-process. Backward CLI/SCM compatibility is not a requirement for this new prototype.
- The new view reads service status and the existing Application registry value. It cannot create, edit, start, stop or delete services.
- Original icons and message/dialog resources are reused.

## Development

Requires Visual Studio C++ tools and a Windows SDK. The prototype targets x64 and Windows 10 build 19041 or newer.

```powershell
pwsh -NoProfile -File next/build/build.ps1 -Project App
pwsh -NoProfile -File next/build/build.ps1 -Project Router
pwsh -NoProfile -File next/build/test.ps1
pwsh -NoProfile -File next/build/smoke-ui.ps1
pwsh -NoProfile -File next/build/smoke-ui.ps1 -ExplicitUi
```

If direct NuGet access fails, the build script accepts `-Proxy http://127.0.0.1:10808`. Dependencies are pinned. Build and restore outputs stay inside `next`. Format only the prototype C++ files using `next/.clang-format`.

After building, double-click `next/out/x64/Release/NssmNext.exe`. Keep the accompanying runtime files: GUI startup is not a standalone-EXE deployment.

## Distribution direction

Keep C++/WinUI 3 and self-contained deployment. A later release will package the required executable, DLLs, resources and notices into a ZIP for GitHub Release; Scoop can then download/extract that ZIP and create a GUI shortcut. Exclude build intermediates and debug artifacts, not required runtime DLLs/resources. No installer, self-extracting launcher or UI-framework rewrite is planned for this stage.

Release publication and Scoop changes are deferred; they are not authorized by the current local checks.

## Boundaries

No services depend on the prototype, and old no-argument SCM startup is intentionally not preserved. Do not register this GUI-first executable as a service. A future service-hosting contract is separate work.

CI builds the prototype and runs source/routing/safety tests; it does not run service lifecycle or old-binary compatibility gates. Static/build/UI Automation results do not establish screenshot quality, theme/DPI/keyboard coverage or clean-machine deployment. See `docs/validation.md` for measured results and the disclosed review incident/recovery.
