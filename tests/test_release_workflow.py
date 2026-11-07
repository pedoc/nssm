import os
import re
import unittest
from pathlib import Path
from typing import Any

import yaml

ROOT = Path(__file__).resolve().parents[1]


class ReleaseWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = yaml.load(
            (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8-sig"),
            Loader=yaml.BaseLoader,
        )
        cls.jobs = cls.workflow["jobs"]
        cls.header = (ROOT / "version.h").read_text(encoding="utf-8-sig")
        version = re.search(r'NSSM_VERSION _T\("([\d.]+)"\)', cls.header)
        if version is None:
            raise ValueError("Missing NSSM_VERSION in version.h")
        cls.version = version[1]

    def test_manual_and_tag_triggers_are_preserved(self) -> None:
        self.assertIn("workflow_dispatch", self.workflow["on"])
        self.assertEqual(self.workflow["on"]["push"], {"tags": ["v*"]})

    def test_default_token_is_read_only(self) -> None:
        self.assertEqual(self.workflow.get("permissions"), {"contents": "read"})

    def test_validation_precedes_build(self) -> None:
        self.assertIn("validate", self.jobs)
        self.assertEqual(self.jobs["build"].get("needs"), "validate")
        commands = self.commands("validate")
        self.assertIn("tests/requirements.txt", commands)
        self.assertIn("pytest", commands)

    def test_both_windows_architectures_are_built(self) -> None:
        matrix = self.jobs["build"]["strategy"]["matrix"]
        self.assertEqual(matrix["platform"], ["win32", "win64"])
        self.assertEqual(matrix["configuration"], ["Release"])

    def test_packaging_rejects_missing_or_wrong_version_executable(self) -> None:
        commands = self.commands("build")
        for required in ("Test-Path", "ProductVersion", "Compress-Archive", "nssm.exe"):
            self.assertIn(required, commands)
        uploads = self.actions("build", "actions/upload-artifact@v4")
        self.assertEqual(len(uploads), 1)
        self.assertEqual(uploads[0]["with"].get("if-no-files-found"), "error")
        self.assertIn("dist/", uploads[0]["with"]["path"])

    def test_release_waits_for_both_builds_and_is_tag_only(self) -> None:
        self.assertIn("release", self.jobs)
        release = self.jobs["release"]
        self.assertEqual(release["needs"], "build")
        self.assertIn("refs/tags/v", release["if"])
        self.assertEqual(release["permissions"], {"contents": "write"})
        for name in ("validate", "build"):
            self.assertNotIn("GH_TOKEN", str(self.jobs[name]))

    def test_release_downloads_both_zip_artifacts(self) -> None:
        downloads = self.actions("release", "actions/download-artifact@v4")
        self.assertEqual(len(downloads), 1)
        settings = downloads[0]["with"]
        self.assertEqual(settings["pattern"], "nssm-*-Release")
        self.assertEqual(settings["merge-multiple"], "true")
        self.assertEqual(settings["path"], "dist")

    def test_release_is_verified_before_publication_without_overwriting(self) -> None:
        commands = self.commands("release")
        required_commands = (
            "nssm-win32.zip",
            "nssm-win64.zip",
            "SHA256SUMS.txt",
            "unzip",
            "sha256sum",
            "gh release create",
            "--draft",
            "--verify-tag",
            "gh release download",
            "gh release edit",
            "--draft=false",
            "--latest",
        )
        for required in required_commands:
            self.assertIn(required, commands)
        self.assertNotIn("--clobber", commands)
        self.assertLess(
            commands.index("gh release create"), commands.index("gh release download")
        )
        self.assertLess(
            commands.index("gh release download"), commands.index("gh release edit")
        )
        self.assertIn("sha256sum --check", commands)

    def test_published_assets_are_downloaded_without_authentication(self) -> None:
        commands = self.commands("release")
        self.assertIn("curl --fail", commands)
        self.assertIn("--retry", commands)
        self.assertIn("/releases/download/", commands)
        self.assertLess(
            commands.index("gh release edit"), commands.index("curl --fail")
        )
        self.assertNotIn("Authorization:", commands)

    def test_version_string_matches_windows_numeric_version(self) -> None:
        self.assertRegex(self.version, r"^\d+\.\d+\.\d+$")
        numbers = re.search(r"NSSM_VERSIONINFO ([\d,]+)", self.header)
        self.assertIsNotNone(numbers)
        self.assertEqual(numbers[1], self.version.replace(".", ",") + ",0")

    @unittest.skipUnless(os.environ.get("GITHUB_REF_TYPE") == "tag", "not a tag run")
    def test_release_tag_matches_binary_version(self) -> None:
        self.assertEqual(os.environ["GITHUB_REF_NAME"], "v" + self.version)

    def commands(self, job: str) -> str:
        return "\n".join(
            step.get("run", "") for step in self.jobs.get(job, {}).get("steps", [])
        )

    def actions(self, job: str, action: str) -> list[dict[str, Any]]:
        return [
            step
            for step in self.jobs.get(job, {}).get("steps", [])
            if step.get("uses") == action
        ]
