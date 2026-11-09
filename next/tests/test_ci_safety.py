"""Safety contracts for the CI service smoke script and next-compat workflow.

These tests are static contracts only. They must not build, publish, or alter
local services. Runtime execution of the script is attempted in a pwsh
subprocess, but only on a GitHub-hosted runner; elsewhere it is skipped.
"""

from __future__ import annotations

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Dict, List

import yaml

ROOT: Path = Path(__file__).resolve().parents[2]
NEXT: Path = ROOT / "next"
SMOKE: Path = NEXT / "build" / "service-smoke.ps1"
WORKFLOW: Path = ROOT / ".github" / "workflows" / "next-compat.yml"

REQUIRED_ENV_GUARDS: List[str] = [
    "GITHUB_ACTIONS",
    "RUNNER_ENVIRONMENT",
    "RUNNER_OS",
    "github-hosted",
]
FORBIDDEN_WORKFLOW_VERBS: List[str] = ["release", "publish"]


def _is_github_hosted() -> bool:
    return (
        os.environ.get("GITHUB_ACTIONS") == "true"
        and os.environ.get("RUNNER_ENVIRONMENT") == "github-hosted"
    )


def _assert_no_releases_side_effects(uses: List[str]) -> None:
    for value in uses:
        lowered = str(value).lower()
        for verb in FORBIDDEN_WORKFLOW_VERBS:
            assert verb not in lowered, f"forbidden release verb in action: {value}"


class ServiceSmokeScriptContractTests(unittest.TestCase):
    """Static contracts for next/build/service-smoke.ps1."""

    @classmethod
    def setUpClass(cls) -> None:
        assert SMOKE.is_file(), f"missing CI smoke script: {SMOKE}"
        cls.text: str = SMOKE.read_text(encoding="utf-8")
        cls.lower: str = cls.text.lower()

    def test_script_declares_github_hosted_guards(self) -> None:
        for guard in REQUIRED_ENV_GUARDS:
            assert guard in self.text, f"script must check {guard}"
        assert "github-hosted" in self.text, "script must require github-hosted"
        assert "$ErrorActionPreference" in self.text, "script must stop on errors"

    def test_script_defines_assert_owned_service(self) -> None:
        assert "Assert-OwnedService" in self.text, "missing Assert-OwnedService helper"
        assert "function Assert-OwnedService" in self.text

    def test_script_uses_exclusive_ci_prefix(self) -> None:
        assert "NssmNextCI_" in self.text, "service name must use NssmNextCI_ prefix"
        assert (
            self.text.count("NssmNextCI_") >= 2
        ), "prefix must name service and cleanup"

    def test_script_cleans_up_in_finally(self) -> None:
        assert "finally" in self.lower, "script must clean up in a finally block"
        assert "finally" in self.text, "script must use a real finally block"
        assert (
            "sc.exe" in self.lower or "sc.exe" in self.text
        ), "cleanup must remove service"

    def test_assert_owned_service_checks_system_evidence(self) -> None:
        """Assert-OwnedService must verify SYSTEM evidence, not just ImagePath.

        A matching ImagePath alone does not prove the service runs as the
        built-in LocalSystem account or starts automatically. Cleanup must
        only delete a service it still owns, so the helper must reject a
        service whose StartName is not LocalSystem or whose StartMode is not
        Auto.
        """
        start = self.text.find("function Assert-OwnedService")
        assert start != -1, "missing Assert-OwnedService definition"
        end = self.text.find("\nfunction ", start + 1)
        if end == -1:
            end = self.text.find("\n}", start + 1)
        assert end != -1, "cannot locate Assert-OwnedService body"
        body = self.text[start:end]
        assert (
            "$service.PathName" in body
        ), "Assert-OwnedService must validate ImagePath"
        assert (
            "LocalSystem" in body
        ), "Assert-OwnedService must validate StartName == LocalSystem"
        assert (
            "StartMode" in body or "StartType" in body
        ), "Assert-OwnedService must validate the automatic start mode"
        assert (
            "Auto" in body
        ), "Assert-OwnedService must require the automatic start mode"

    def test_cleanup_checks_sc_stop_exit_before_delete(self) -> None:
        """Cleanup must inspect sc.exe stop's LASTEXITCODE before deleting.

        A stop that fails for a reason other than ERROR_SERVICE_NOT_ACTIVE
        (1062) means the service may still be running; proceeding to delete
        would be unsafe. Accepting only 0 and 1062 is the narrow contract.
        """
        cleanup_start = self.lower.find("finally")
        assert cleanup_start != -1, "script must clean up in a finally block"
        cleanup_body = self.text[cleanup_start:]
        assert "sc.exe" in cleanup_body, "cleanup must call sc.exe"
        stop_pos = cleanup_body.lower().find("sc.exe")
        tail = cleanup_body[stop_pos:]
        assert (
            "LASTEXITCODE" in tail
        ), "cleanup must check sc.exe stop's LASTEXITCODE before waiting/deleting"
        assert (
            "1062" in tail
        ), "cleanup must explicitly accept ERROR_SERVICE_NOT_ACTIVE (1062)"
        assert (
            "$global:LASTEXITCODE" in tail or "$LASTEXITCODE" in tail
        ), "cleanup must read the stop exit code from LASTEXITCODE"

    def test_script_does_not_touch_vendor_directories(self) -> None:
        assert "nssm-2.24" not in self.text, "must not read official vendor directory"
        assert "\\root\\" not in self.lower.replace("c:\\root", "")

    def test_rejects_non_github_hosted_environment(self) -> None:
        """On a runner, unset the hosted markers and require a nonzero exit."""
        env = dict(os.environ)
        env.pop("GITHUB_ACTIONS", None)
        env.pop("RUNNER_ENVIRONMENT", None)
        env["RUNNER_OS"] = "Windows"
        result = subprocess.run(
            ["pwsh", "-NoProfile", "-File", str(SMOKE)],
            env=env,
            capture_output=True,
            text=True,
            timeout=60,
        )
        assert result.returncode != 0, "self-hosted must be rejected"
        assert not Path(os.environ.get("TEMP", "")).joinpath("NssmNextCI").exists()


class ServiceSmokeRuntimeGuardTests(unittest.TestCase):
    """Runtime guards for local/self-hosted environments.

    These are skipped locally because the script does not exist yet and local
    machines must never install or remove services.
    """

    def setUp(self) -> None:
        self.marker = NEXT / ".nssm-next-ci-smoke-marker"

    def tearDown(self) -> None:
        if self.marker.exists():
            self.marker.unlink()

    def _run_smoke(
        self, env_overrides: Dict[str, str]
    ) -> subprocess.CompletedProcess[str]:
        env = dict(os.environ)
        env.update(env_overrides)
        return subprocess.run(
            ["pwsh", "-NoProfile", "-File", str(SMOKE)],
            env=env,
            capture_output=True,
            text=True,
            timeout=60,
        )

    @unittest.skipUnless(SMOKE.is_file(), "service-smoke.ps1 not implemented yet")
    def test_local_environment_is_rejected_without_side_effects(self) -> None:
        env = dict(os.environ)
        env.pop("GITHUB_ACTIONS", None)
        env.pop("RUNNER_ENVIRONMENT", None)
        env["RUNNER_OS"] = "Windows"
        env["TEMP"] = str(NEXT / ".nssm-next-ci-temp")
        result = subprocess.run(
            ["pwsh", "-NoProfile", "-File", str(SMOKE)],
            env=env,
            capture_output=True,
            text=True,
            timeout=60,
        )
        assert result.returncode != 0, "local environment must be rejected"
        assert not Path(env["TEMP"]).exists(), "script must not create directories"
        assert not self.marker.exists(), "script must not create service artifacts"

    @unittest.skipUnless(SMOKE.is_file(), "service-smoke.ps1 not implemented yet")
    def test_self_hosted_environment_is_rejected(self) -> None:
        result = self._run_smoke(
            {
                "GITHUB_ACTIONS": "true",
                "RUNNER_ENVIRONMENT": "self-hosted",
                "RUNNER_OS": "Windows",
            }
        )
        assert result.returncode != 0, "self-hosted must be rejected"
        assert not self.marker.exists(), "script must not create service artifacts"


class NextCompatWorkflowContractTests(unittest.TestCase):
    """Static contracts for .github/workflows/next-compat.yml."""

    @classmethod
    def setUpClass(cls) -> None:
        assert WORKFLOW.is_file(), f"missing workflow: {WORKFLOW}"
        cls.doc: Dict[object, object] = yaml.load(
            WORKFLOW.read_text(encoding="utf-8"), Loader=yaml.BaseLoader
        )

    def _trigger(self) -> Dict[str, object]:
        trigger = self.doc.get("on") or self.doc.get(True)
        assert isinstance(trigger, dict), "workflow must declare an 'on' mapping"
        return trigger

    def test_push_is_limited_to_prototype_branch(self) -> None:
        push = self._trigger().get("push")
        assert isinstance(push, dict), "push trigger must be a mapping"
        branches = push.get("branches")
        assert branches == [
            "feat/winui3-unified-prototype"
        ], "push must target only feat/winui3-unified-prototype"
        assert "tags" not in push, "tags must not trigger the compatibility workflow"

    def test_workflow_dispatch_is_allowed(self) -> None:
        assert "workflow_dispatch" in self._trigger(), "manual dispatch must be allowed"

    def test_contents_permission_is_read_only(self) -> None:
        permissions = self.doc.get("permissions")
        assert permissions == {
            "contents": "read"
        }, "workflow must request read-only contents"

    def test_gui_prototype_does_not_run_legacy_compatibility_gates(self) -> None:
        for job in (self.doc.get("jobs") or {}).values():
            for step in job.get("steps") or []:
                run = str(step.get("run", ""))
                self.assertNotIn("service-smoke.ps1", run)
                self.assertNotIn("verify-compatibility.ps1", run)
                self.assertNotIn("-Project Reference", run)

    def test_no_release_or_publish_actions(self) -> None:
        uses: List[str] = []
        for job in (self.doc.get("jobs") or {}).values():
            for step in job.get("steps") or []:
                if isinstance(step, dict) and "uses" in step:
                    uses.append(str(step["uses"]))
        _assert_no_releases_side_effects(uses)
        assert uses, "workflow must declare at least one action step"


class WorkflowNativeStepFailureContractTests(unittest.TestCase):
    """A nonzero exit from any native command must fail its Actions step.

    GitHub Actions wraps multi-command ``run`` blocks and prefixes them with
    ``$ErrorActionPreference = "Stop"`` plus a ``$LASTEXITCODE`` epilogue.
    PSNativeCommandUseErrorActionPreference defaults to false on Windows
    PowerShell, so a native failure only stops the step when the workflow
    explicitly checks ``$LASTEXITCODE``. This reproduces the Actions failure
    semantics against the authored YAML run blocks using harmless stubs.
    """

    @classmethod
    def setUpClass(cls) -> None:
        assert WORKFLOW.is_file(), f"missing workflow: {WORKFLOW}"
        cls.doc: Dict[object, object] = yaml.load(
            WORKFLOW.read_text(encoding="utf-8"), Loader=yaml.BaseLoader
        )

    def _job(self, job_name: str) -> Dict[str, object]:
        jobs = self.doc.get("jobs") or {}
        assert job_name in jobs, f"missing job: {job_name}"
        job = jobs[job_name]
        assert isinstance(job, dict), f"job {job_name} must be a mapping"
        return job

    def _steps(self, job_name: str) -> List[Dict[str, object]]:
        steps = self._job(job_name).get("steps") or []
        assert steps, f"job {job_name} must declare steps"
        result: List[Dict[str, object]] = []
        for step in steps:
            assert isinstance(step, dict), "step must be a mapping"
            result.append(step)
        return result

    def test_validate_source_step_fails_when_any_native_command_fails(self) -> None:
        """Validate source step must fail when a non-final native command fails."""
        step = next(
            (
                s
                for s in self._steps("compatibility")
                if str(s.get("name", "")).startswith("Validate source and CI safety")
            ),
            None,
        )
        assert (
            step is not None
        ), "missing 'Validate source and CI safety contracts' step"
        run = step.get("run")
        assert isinstance(run, str), "step must use a run block"
        native_lines = [
            line
            for line in run.splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        assert (
            len(native_lines) > 1
        ), "regression coverage requires a multi-command native run block"
        with tempfile.TemporaryDirectory(prefix="nssm-ci-sim-") as tmp:
            result = _run_step_simulation(run, tmp, fail={"pytest": 1})
        assert result.returncode != 0, (
            "a nonzero exit from 'pytest' must fail the step even though a later "
            f"native command exits 0 (simulated exit: {result.returncode})"
        )

    def test_routing_step_fails_when_build_fails(self) -> None:
        """A failed routing-test build must not be masked by the executable."""
        step = next(
            (
                s
                for s in self._steps("compatibility")
                if str(s.get("name", "")) == "Build and test GUI routing"
            ),
            None,
        )
        assert step is not None, "missing 'Build and test GUI routing' step"
        run = step.get("run")
        assert isinstance(run, str), "step must use a run block"
        with tempfile.TemporaryDirectory(prefix="nssm-ci-sim-") as tmp:
            result = _run_step_simulation(run, tmp, fail={"build.ps1": 1})
        assert result.returncode != 0, (
            "a failed Router build must fail the step even if later tests exit 0 "
            f"(simulated exit: {result.returncode})"
        )

    def test_routing_step_fails_when_compiled_tests_fail(self) -> None:
        """A failed compiled routing test must fail the CI step."""
        step = next(
            (
                s
                for s in self._steps("compatibility")
                if str(s.get("name", "")) == "Build and test GUI routing"
            ),
            None,
        )
        assert step is not None, "missing 'Build and test GUI routing' step"
        run = step.get("run")
        assert isinstance(run, str), "step must use a run block"
        with tempfile.TemporaryDirectory(prefix="nssm-ci-sim-") as tmp:
            result = _run_step_simulation(run, tmp, fail={"RouterTests.exe": 5})
        assert result.returncode != 0, (
            "a nonzero RouterTests.exe exit must fail the step "
            f"(simulated exit: {result.returncode})"
        )

    def test_multi_command_steps_keep_github_error_action_stop_prefix(self) -> None:
        """Simulation harness preserves the Actions 'Stop' prefix and epilogue."""
        step = next(
            (
                s
                for s in self._steps("compatibility")
                if str(s.get("name", "")) == "Build and test GUI routing"
            ),
            None,
        )
        assert step is not None, "missing multi-command routing step"
        run = step.get("run")
        assert isinstance(run, str), "step must use a run block"
        self.assertMultiCommandLineBlock(run)
        with tempfile.TemporaryDirectory(prefix="nssm-ci-sim-") as tmp:
            result = _run_step_simulation(run, tmp, fail={})
            wrapper_text = (Path(tmp) / "run_step.ps1").read_text(encoding="utf-8")
        assert result.returncode == 0, result.stderr
        assert (
            'ErrorActionPreference = "Stop"' in wrapper_text
        ), "simulation must reproduce GitHub's ErrorActionPreference Stop prefix"
        assert (
            "if ($global:LASTEXITCODE -ne 0) { exit $global:LASTEXITCODE }"
            in wrapper_text
        ), "simulation must reproduce GitHub's $LASTEXITCODE epilogue"

    def assertMultiCommandLineBlock(self, run: str) -> None:
        native_lines = [
            line
            for line in run.splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        assert (
            len(native_lines) > 1
        ), "expected more than one native command in the run block"


def _run_step_simulation(
    run_block: str, tmp: Path | str, fail: Dict[str, object]
) -> subprocess.CompletedProcess[str]:
    """Simulate a GitHub Actions PowerShell run block with native stubs.

    GitHub wraps Windows ``run`` blocks with an ``ErrorActionPreference =
    "Stop"`` prefix and a ``$LASTEXITCODE`` epilogue. PSNativeCommandUseErrorActionPreference
    defaults to false, so a native failure only stops the step when the
    workflow explicitly checks ``$LASTEXITCODE``. Each native command is
    replaced by a harmless ``cmd /c exit <code>`` so no original workflow
    command is executed; the original line is recorded in ``invoked.log``.

    ``fail`` maps a substring of a native command line to either an int exit
    code (applied to every matching line) or a list of exit codes (applied to
    successive matching lines, then 0).
    """
    tmp = Path(tmp)
    marker = tmp / "invoked.log"
    marker.touch()
    native_lines = [
        line.strip()
        for line in run_block.splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    occurrence: Dict[str, int] = {}
    transformed_lines: List[str] = []
    for line in native_lines:
        # Preserve only this allowlisted control statement, without replacing
        # it with a successful stub that would erase the preceding exit code.
        if line == "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }":
            transformed_lines.append(line)
            continue
        key = next((k for k in fail if k in line), None)
        exit_code = 0
        if key is not None:
            index = occurrence.get(key, 0)
            occurrence[key] = index + 1
            value = fail[key]
            if isinstance(value, (list, tuple)):
                exit_code = value[index] if index < len(value) else 0
            else:
                exit_code = int(value)
        escaped_line = line.replace("'", "''")
        transformed_lines.append(
            f"Add-Content -LiteralPath '{marker}' -Value '{escaped_line}'\n"
            f"$global:LASTEXITCODE = 0\n"
            f"cmd /c exit {exit_code}\n"
            f"$global:LASTEXITCODE = {exit_code}"
        )
    body = "\n".join(transformed_lines)
    # GitHub's wrapper for a multi-command native run block. The epilogue
    # only sees the final native command's exit code, which is why a failure
    # in an earlier command is masked without an explicit per-command check.
    wrapper = (
        '$ErrorActionPreference = "Stop"\n'
        + body
        + "\n"
        + "if ($global:LASTEXITCODE -ne 0) { exit $global:LASTEXITCODE }\n"
    )
    script = tmp / "run_step.ps1"
    script.write_text(wrapper, encoding="utf-8")
    return subprocess.run(
        ["pwsh", "-NoProfile", "-File", str(script)],
        capture_output=True,
        text=True,
        timeout=60,
    )


class WorkflowAutocrlfOrderingContractTests(unittest.TestCase):
    """Hosted runner checkout line endings must be pinned before checkout.

    Windows Git defaults may enable ``core.autocrlf``, which rewrites
    production baselines on checkout. The workflow must disable it globally
    on the disposable hosted runner before ``actions/checkout`` runs so raw
    SHA256 baselines are independent of installation defaults. Root
    ``.gitattributes`` cannot change, so this ordering contract is the guard.
    """

    @classmethod
    def setUpClass(cls) -> None:
        assert WORKFLOW.is_file(), f"missing workflow: {WORKFLOW}"
        cls.doc = yaml.load(
            WORKFLOW.read_text(encoding="utf-8"), Loader=yaml.BaseLoader
        )

    def _job_steps(self) -> List[Dict[str, object]]:
        jobs = self.doc.get("jobs") or {}
        job = jobs.get("compatibility")
        assert isinstance(job, dict), "missing compatibility job"
        steps = job.get("steps") or []
        result: List[Dict[str, object]] = []
        for step in steps:
            assert isinstance(step, dict), "step must be a mapping"
            result.append(step)
        return result

    def _run_text(self, step: Dict[str, object]) -> str:
        run = step.get("run")
        assert isinstance(run, str), "autocrlf step must use a run block"
        return run

    def test_workflow_disables_core_autocrlf_before_checkout(self) -> None:
        steps = self._job_steps()
        checkout_index = next(
            (
                index
                for index, step in enumerate(steps)
                if str(step.get("uses", "")).startswith("actions/checkout")
            ),
            None,
        )
        assert checkout_index is not None, "workflow must check out the branch"
        autocrlf_steps = [
            (index, step)
            for index, step in enumerate(steps)
            if "core.autocrlf" in self._step_searchable_text(step)
        ]
        assert (
            autocrlf_steps
        ), "workflow must explicitly set core.autocrlf before actions/checkout"
        autocrlf_index, autocrlf_step = autocrlf_steps[0]
        assert autocrlf_index < checkout_index, (
            "core.autocrlf must be disabled before actions/checkout, "
            f"got step index {autocrlf_index} after checkout index {checkout_index}"
        )
        run = self._run_text(autocrlf_step)
        assert (
            re.search(r"git\s+config\s+--global\s+core\.autocrlf\s+false", run)
            is not None
        ), "autocrlf step must run 'git config --global core.autocrlf false'"

    def _step_searchable_text(self, step: Dict[str, object]) -> str:
        parts: List[str] = []
        if isinstance(step.get("run"), str):
            parts.append(step["run"])
        for key in ("with", "env"):
            value = step.get(key)
            if isinstance(value, dict):
                parts.extend(str(v) for v in value.values())
            elif isinstance(value, str):
                parts.append(value)
        return "\n".join(parts)


if __name__ == "__main__":
    unittest.main()
