"""Contract tests for the WinUI3 unified prototype in next/.

These are static safety contracts, not proof of runtime or SCM compatibility.
They do not build, touch the network, or read vendor directories (nssm-2.24-*).
Native routing tests and runtime CLI/UI checks are separate acceptance gates.
"""

from __future__ import annotations

import hashlib
import json
import unittest
from pathlib import Path, PurePosixPath
from typing import Dict, Iterator, List, Optional
from xml.etree import ElementTree as ET

ROOT: Path = Path(__file__).resolve().parents[2]
NEXT: Path = ROOT / "next"
CONTRACTS: Path = NEXT / "contracts"
SRC: Path = NEXT / "src"
DOCS: Path = NEXT / "docs"

ROOT_CPP: List[str] = sorted(p.name for p in ROOT.glob("*.cpp"))


def _normalize_key(key: str) -> str:
    return key.replace("\\", "/").removeprefix("./")


def _resolve_root_file(key: str) -> Optional[Path]:
    """Resolve a baseline key to a real file under ROOT."""
    norm = _normalize_key(key)
    relative = PurePosixPath(norm)
    if relative.is_absolute() or ".." in relative.parts:
        return None
    candidate = (ROOT / norm).resolve()
    return (
        candidate
        if candidate.is_relative_to(ROOT.resolve()) and candidate.is_file()
        else None
    )


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_baseline() -> Dict[str, str]:
    path = CONTRACTS / "baseline.json"
    if not path.is_file():
        raise FileNotFoundError(f"missing contract baseline: {path}")
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise AssertionError(
            "baseline.json must be a JSON object mapping files to sha256"
        )
    return data


def _localname(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _iter_elements(tree: ET.ElementTree, name: str) -> Iterator[ET.Element]:
    for element in tree.iter():
        if _localname(element.tag) == name:
            yield element


def _xml_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


class BaselineContractTests(unittest.TestCase):
    def test_baseline_exists_and_is_nonempty(self) -> None:
        baseline = _load_baseline()
        self.assertTrue(baseline, "baseline.json must map at least one file")

    def test_baseline_covers_entrypoint_and_gui(self) -> None:
        baseline = _load_baseline()
        keys = {_normalize_key(key) for key in baseline}
        self.assertIn("nssm.cpp", keys)
        self.assertIn("gui.cpp", keys)

    def test_every_baseline_file_hash_is_unchanged(self) -> None:
        baseline = _load_baseline()
        for key, expected in baseline.items():
            resolved = _resolve_root_file(key)
            self.assertIsNotNone(resolved, f"baseline file not found: {key}")
            assert resolved is not None
            self.assertEqual(
                expected.lower(),
                _sha256(resolved),
                f"root file changed: {key} ({resolved})",
            )

    def test_root_cpp_set_matches_baseline(self) -> None:
        baseline = _load_baseline()
        keys = {_normalize_key(key) for key in baseline}
        missing = [name for name in ROOT_CPP if name not in keys]
        self.assertEqual(missing, [], "baseline.json is missing root .cpp files")


class CoreProjectContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.path = SRC / "Core" / "Core.vcxproj"
        self.tree = ET.parse(self.path)

    def test_core_project_includes_every_root_cpp(self) -> None:
        includes: List[str] = []
        for element in _iter_elements(self.tree, "ClCompile"):
            include = element.get("Include")
            if include:
                includes.append(
                    Path(include.replace("$(RepoRoot)", "").replace("\\", "/")).name
                )
        for name in ROOT_CPP:
            self.assertIn(name, includes, f"Core.vcxproj must compile {name}")

    def test_core_project_includes_gui_cpp_directly(self) -> None:
        includes = [
            Path(
                el.get("Include", "").replace("$(RepoRoot)", "").replace("\\", "/")
            ).name
            for el in _iter_elements(self.tree, "ClCompile")
        ]
        self.assertIn("gui.cpp", includes)

    def test_nssm_cpp_renames_wmain_per_file(self) -> None:
        found = False
        for element in _iter_elements(self.tree, "ClCompile"):
            include = Path(
                element.get("Include", "").replace("$(RepoRoot)", "").replace("\\", "/")
            ).name
            if include != "nssm.cpp":
                continue
            definitions = " ".join(
                (child.text or "")
                for child in element.iter()
                if _localname(child.tag) == "PreprocessorDefinitions"
            )
            if "wmain=NssmLegacyMain" in definitions:
                found = True
        self.assertTrue(found, "nssm.cpp must define wmain=NssmLegacyMain per file")


class AppProjectContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.path = SRC / "App" / "App.vcxproj"
        self.text = _xml_text(self.path)
        self.tree = ET.parse(self.path)

    def test_app_references_core_project(self) -> None:
        references = [
            el.get("Include", "")
            for el in _iter_elements(self.tree, "ProjectReference")
        ]
        self.assertTrue(
            any(
                "Core" in ref and "Core.vcxproj" in ref.replace("\\", "/")
                for ref in references
            ),
            f"App.vcxproj must reference Core.vcxproj, got {references}",
        )

    def test_app_uses_gui_subsystem_with_crt_startup(self) -> None:
        subsystems = [
            (el.text or "").strip() for el in _iter_elements(self.tree, "SubSystem")
        ]
        self.assertIn("Windows", subsystems)
        self.assertNotIn("Console", subsystems)
        entrypoints = [
            (el.text or "").strip()
            for el in _iter_elements(self.tree, "EntryPointSymbol")
        ]
        self.assertIn("wmainCRTStartup", entrypoints)

    def test_app_disables_xaml_generated_main(self) -> None:
        definitions = " ".join(
            (el.text or "")
            for el in self.tree.iter()
            if _localname(el.tag) == "PreprocessorDefinitions"
        )
        self.assertIn("DISABLE_XAML_GENERATED_MAIN", definitions)

    def test_app_bootstrap_properties_are_false(self) -> None:
        expected = {
            "WindowsAppSdkBootstrapInitialize",
            "WindowsAppSdkDeploymentManagerInitialize",
            "WindowsAppSdkUndockedRegFreeWinRTInitialize",
        }
        seen: Dict[str, str] = {}
        for element in self.tree.iter():
            name = _localname(element.tag)
            if name in expected:
                self.assertNotIn(name, seen, f"duplicate initializer property: {name}")
                seen[name] = (element.text or "").strip().lower()
        self.assertEqual(set(seen), expected)
        for name, value in seen.items():
            self.assertEqual(value, "false", f"{name} must be false")

    def test_app_has_exactly_two_pinned_package_references(self) -> None:
        versions: List[str] = []
        for element in _iter_elements(self.tree, "PackageReference"):
            version = element.get("Version")
            if version is None:
                for child in element.iter():
                    if _localname(child.tag) == "Version":
                        version = (child.text or "").strip()
                        break
            versions.append((version or "").strip())
        self.assertEqual(
            len(versions), 2, f"expected two PackageReference entries, got {versions}"
        )
        for version in versions:
            self.assertTrue(version, "PackageReference must pin a version")
            self.assertNotIn(
                "*", version, f"version must not be a floating spec: {version}"
            )


class NativeReadPolicyTests(unittest.TestCase):
    def test_registry_string_size_is_checked_before_allocation(self) -> None:
        source = (SRC / "Core" / "NativeServices.cpp").read_text(encoding="utf-8")
        check = "detail::IsValidApplicationBytes(bytes)"
        self.assertIn(check, source)
        self.assertLess(source.index(check), source.index("std::vector<wchar_t>"))


class AppMainContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.path = SRC / "App" / "main.cpp"
        self.text = _xml_text(self.path)

    def test_no_arguments_routes_to_runwinui(self) -> None:
        self.assertRegex(self.text, r"argc\s*==\s*1")
        self.assertIn("RunWinUi", self.text)

    def test_ui_flag_routes_to_runwinui(self) -> None:
        self.assertIn("RunWinUi", self.text)
        self.assertRegex(self.text, r"argc\s*==\s*2")
        self.assertIn('"--ui"', self.text)

    def test_legacy_path_calls_nssmlegacymain(self) -> None:
        self.assertIn("NssmLegacyMain", self.text)
        self.assertRegex(
            self.text, r"return\s+NssmLegacyMain\s*\(\s*argc\s*,\s*argv\s*\)"
        )

    def test_no_process_spawning_apis(self) -> None:
        lowered = self.text.lower()
        self.assertNotIn("createprocess", lowered)
        self.assertNotIn("shellexecute", lowered)


class CompatibilityDocTests(unittest.TestCase):
    def test_compatibility_doc_exists(self) -> None:
        self.assertTrue((DOCS / "compatibility.md").is_file())


if __name__ == "__main__":
    unittest.main()
