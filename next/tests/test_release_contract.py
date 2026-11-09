"""Non-mutating contracts for the separate Next runtime/release pipeline."""

from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
NEXT = ROOT / "next"
WORKFLOW = ROOT / ".github/workflows/next-release.yml"


def workflow():
    return yaml.load(WORKFLOW.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)


def test_next_release_namespace_is_separate_from_legacy():
    doc = workflow()
    assert doc["on"]["push"]["tags"] == ["next-v*"]
    assert "workflow_dispatch" in doc["on"]
    assert doc["permissions"] == {"contents": "read"}
    assert doc["jobs"]["release"]["needs"] == "build"
    assert "refs/tags/next-v" in doc["jobs"]["release"]["if"]
    assert "github.repository == 'pedoc/nssm'" in doc["jobs"]["release"]["if"]
    assert doc["jobs"]["release"]["permissions"] == {"contents": "write"}
    assert "permissions" not in doc["jobs"]["build"]


def test_release_builds_production_and_keeps_native_failures_fatal():
    steps = workflow()["jobs"]["build"]["steps"]
    checkout = next(
        i for i, s in enumerate(steps) if "actions/checkout" in s.get("uses", "")
    )
    assert "core.autocrlf false" in steps[checkout - 1]["run"]
    assert steps[checkout]["with"]["persist-credentials"] == "false"
    assert steps[checkout]["with"]["fetch-depth"] == "0"
    text = "\n".join(s.get("run", "") for s in steps)
    assert "-Project App" in text
    assert "-UiTestBackend" not in text
    assert "RouterTests.exe" in text
    assert "ManagementTests.exe" in text
    assert "run-managed-integration.ps1" not in text
    assert "package.ps1" in text
    assert "numeric version must match version.h" in text
    assert "git merge-base --is-ancestor $env:GITHUB_SHA origin/main" in text
    assert "Stable Next tags must point to a commit merged into main" in text
    for step in steps:
        lines = step.get("run", "").splitlines()
        for index, line in enumerate(lines):
            if (
                line.strip().startswith(("python ", "pwsh ", "./next/out/Tests/"))
                and len(lines) > 1
            ):
                assert (
                    lines[index + 1].strip()
                    == "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }"
                )


def test_release_is_draft_first_with_distinct_stable_and_preview_flags():
    steps = workflow()["jobs"]["release"]["steps"]
    scripts = "\n".join(s.get("run", "") for s in steps)
    assert "sha256sum --check SHA256SUMS.txt" in scripts
    assert scripts.index("gh release create") < scripts.index("gh release download")
    assert scripts.index("gh release download") < scripts.index("gh release edit")
    assert "prerelease=false" in scripts
    assert (
        'if [[ "${RELEASE_TAG#next-v}" == *-* ]]; then prerelease=true; fi' in scripts
    )
    assert 'release_flags=("--prerelease=$prerelease" --latest=false)' in scripts
    assert '--draft --verify-tag --generate-notes "${release_flags[@]}"' in scripts
    assert '--draft=false "${release_flags[@]}"' in scripts
    assert "--json isPrerelease --jq .isPrerelease" in scripts
    assert "curl --fail --location" in scripts
    assert "--clobber" not in scripts
    assert "gh release delete" not in scripts
    for step in steps:
        if "GH_TOKEN" in step.get("env", {}):
            assert "gh release create" in step["run"]


def test_package_preserves_runtime_but_excludes_debug_and_test_binaries():
    text = (NEXT / "build/package.ps1").read_text(encoding="utf-8")
    for name in (
        "NssmNext.exe",
        "NssmNext.ServiceHost.exe",
        "NssmNext.pri",
        "Microsoft.ui.xaml.dll",
        "Microsoft.UI.Xaml.Controls.dll",
        "Microsoft.WindowsAppRuntime.dll",
        "App.xbf",
        "MainWindow.xbf",
        "THIRD_PARTY_NOTICES.md",
        "BUILDINFO.json",
        "FILELIST.json",
        "SHA256SUMS.txt",
    ):
        assert name in text
    for extension in (".pdb", ".ilk", ".lib", ".exp", ".obj"):
        assert extension in text
    assert "out/x64/Release" in text
    assert "ReparsePoint" in text
    assert "Refusing to overwrite" in text
    assert "Assert-X64Executable" in text
    assert "HashData($stream)" in text
    assert "Tests|CaptureWindow" in text


def test_host_security_accepts_equivalent_split_aces_without_weakening_owner_checks():
    text = (NEXT / "src/Core/ServiceHostDeployment.cpp").read_text(encoding="utf-8")
    assert "acl->AceCount != 3" not in text
    for guard in (
        "WinBuiltinAdministratorsSid",
        "SE_DACL_PROTECTED",
        "IsValidAcl",
        "INHERIT_ONLY_ACE",
        "FILE_GENERIC_READ | FILE_GENERIC_EXECUTE",
        "FILE_ATTRIBUTE_REPARSE_POINT",
    ):
        assert guard in text


def test_documentation_images_exist():
    for name in ("services.png", "service-editor.png"):
        image = NEXT / "docs/images" / name
        assert image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
