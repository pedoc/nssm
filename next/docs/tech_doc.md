# Toolchain and verification

Use native MSBuild, a Windows SDK, Microsoft.WindowsAppSDK.WinUI 2.6.5 and Microsoft.Windows.CppWinRT 3.0.260818.1. Targets are currently x64; v145 and v143 toolchains are supported.

Build `App` to produce the WinUI manager and its project-referenced native ServiceHost. The GUI uses Windows subsystem + CRT `wmainCRTStartup`. Core compiles unchanged root supervision sources plus native management/policy/storage adapters; scoped symbol definitions isolate legacy message registration/logging.

`Management` builds portable policy/in-memory assertions without Windows service mutation code. `Router` builds GUI/host entry stubs. `App -UiTestBackend` explicitly enables a separate build-time memory provider and writes to `out/UiTests`, never replacing the production GUI output. The default build explicitly disables this property even if it exists in the environment.

Runtime GUI verification follows `next/.claude/skills/verify/SKILL.md`: drive the actual memory-backed window and inspect captured PNGs; production UI is read-only locally. Actual SCM/LocalSystem/deployment/concurrency integration is guarded to the separately authorized disposable CI runner.

All build/package/generated output remains inside next. Format only authored prototype C++ files with next/.clang-format; do not format the 41 hash-protected root files. Do not present contract counts as a measured source-coverage percentage.

Distribution remains a complete WinUI runtime ZIP plus `NssmNext.ServiceHost.exe`, followed later by GitHub Release and Scoop packaging. Exclude debug/test/build artifacts, not runtime DLLs/resources or the host. Publication is separate authorization.
