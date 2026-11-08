# Toolchain and verification

Use native MSBuild, a Windows SDK, Microsoft.WindowsAppSDK.WinUI 2.6.5 and Microsoft.Windows.CppWinRT 3.0.260818.1. The prototype is x64; its project supports the installed v145 toolchain and a v143 fallback.

The App uses the Windows GUI subsystem and `wmainCRTStartup`, retaining CRT initialization and a directly testable wide-argument router. No arguments and exact `--ui` both enter WinUI. CLI/SCM backward compatibility is not a gate.

Build products, generated messages, package restores and intermediate files stay under next. The old root solution and release assets are not modified. Format only prototype C++ files with `clang-format` and `next/.clang-format`; never format the hash-protected root sources.

Validate root SHA256 protection, project/default-GUI contracts, native build/routing tests and both default/explicit UI launches. No local service operations or service-lifecycle CI are needed for this stage.

Deployment decision: retain native C++/WinUI 3 self-contained output. Later produce a runtime ZIP for GitHub Release and a Scoop manifest/shortcut; no single-EXE rewrite, installer or self-extractor. Runtime DLLs/resources must be retained. Publishing and clean-machine validation are follow-up work.

References: Microsoft WindowsAppSDK-Samples `Samples/SelfContainedDeployment/cpp/cpp-winui-unpackaged`, Windows App SDK self-contained deployment docs and MSVC `/ENTRY` documentation.
