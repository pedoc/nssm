"""Static boundaries for managed-service UI and its non-mutating test backend."""

from pathlib import Path
import unittest
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
NEXT = ROOT / "next"
CORE = NEXT / "src" / "Core"
APP = NEXT / "src" / "App"


def definitions_for(filename: str) -> str:
    tree = ET.parse(CORE / "Core.vcxproj")
    for element in tree.iter():
        if element.tag.rsplit("}", 1)[-1] == "ClCompile" and element.get(
            "Include", ""
        ).endswith(filename):
            return " ".join((child.text or "") for child in element.iter())
    raise AssertionError(f"missing compile input: {filename}")


class ManagementContracts(unittest.TestCase):
    def test_gui_has_all_management_actions(self) -> None:
        text = (APP / "MainWindow.xaml").read_text(encoding="utf-8")
        for name in (
            "NewButton",
            "EditButton",
            "DeleteButton",
            "StartButton",
            "StopButton",
            "RestartButton",
        ):
            self.assertIn(f'x:Name="{name}"', text)
        self.assertNotIn("只读原型", text)
        self.assertIn('x:Name="EmptyState"', text)

    def test_gui_does_not_forward_legacy_mutating_commands(self) -> None:
        text = (APP / "main.cpp").read_text(encoding="utf-8")
        self.assertNotIn("NssmLegacyMain", text)
        self.assertIn("RunVersion", text)
        self.assertIn("return 87", text)

    def test_worker_is_an_explicit_separate_mode(self) -> None:
        text = (NEXT / "src" / "ServiceHost" / "main.cpp").read_text(encoding="utf-8")
        self.assertIn('L"--service"', text)
        self.assertIn("argc != 2", text)
        self.assertIn("RunManagedServiceHost", text)
        project = (APP / "App.vcxproj").read_text(encoding="utf-8")
        self.assertIn("ServiceHost.vcxproj", project)
        self.assertIn("<LinkLibraryDependencies>false", project)

    def test_legacy_event_registration_and_logger_are_isolated(self) -> None:
        self.assertIn(
            "create_messages=NssmNextSkipLegacyMessages", definitions_for("nssm.cpp")
        )
        self.assertIn("service_main=NssmNextServiceMain", definitions_for("nssm.cpp"))
        self.assertIn("log_event=NssmLegacyLogEvent", definitions_for("event.cpp"))
        events = (CORE / "NativeEvents.cpp").read_text(encoding="utf-8")
        self.assertIn(r"EventLog\\Application\\NssmNext", events)
        self.assertNotIn(r'EventLog\\Application\\NSSM"', events)

    def test_ownership_is_more_than_an_application_value(self) -> None:
        native = (CORE / "NativeServices.cpp").read_text(encoding="utf-8")
        policy = (CORE / "ServicePolicy.cpp").read_text(encoding="utf-8")
        for marker in (
            "NssmNextOwner",
            "NssmNextInstance",
            "NssmNextSchema",
            "NssmNextRevision",
        ):
            self.assertIn(marker, native)
        for guard in ("ValidGuid", "HostCommand", "STALE_SERVICE", "IsManagedService"):
            self.assertIn(guard, policy)

    def test_writes_use_owned_handles_and_never_legacy_cli(self) -> None:
        text = (CORE / "NativeServiceActions.cpp").read_text(encoding="utf-8")
        self.assertGreaterEqual(text.count("OpenOwned("), 4)
        self.assertIn("CreateServiceW", text)
        self.assertIn("RequireSameService(expected, actual, true)", text)
        self.assertNotIn("NssmLegacyMain", text)
        self.assertNotIn("RegDeleteTree", text)
        self.assertNotIn("TerminateProcess", text)

    def test_all_native_writes_serialize_before_revalidation(self) -> None:
        text = (CORE / "NativeServiceActions.cpp").read_text(encoding="utf-8")
        for method in ("Create", "Update", "Remove", "Start", "Stop"):
            body = text.split(f"WindowsServices::{method}(", 1)[1]
            body = body.split("WindowsServices::", 1)[0]
            self.assertLess(
                body.index("MutationGuard mutation"), body.index("OpenManager(")
            )
        guard = (CORE / "MutationGuard.cpp").read_text(encoding="utf-8")
        for boundary in (
            "Global",
            "VerifyMutexSecurity",
            "WAIT_ABANDONED",
            "ReleaseMutex",
            "MUTATION_WAIT_MS",
        ):
            self.assertIn(boundary, guard)

    def test_dialogs_disable_parent_actions(self) -> None:
        text = (APP / "MainWindow.Actions.cpp").read_text(encoding="utf-8")
        controls = (APP / "MainWindow.xaml.cpp").read_text(encoding="utf-8")
        self.assertIn("m_dialog_open = true", text)
        self.assertIn("!m_busy && !m_dialog_open", controls)
        self.assertIn("服务已重启", text)

    def test_real_integration_is_guarded_before_side_effects(self) -> None:
        runner = (NEXT / "build" / "run-managed-integration.ps1").read_text(
            encoding="utf-8"
        )
        before_writes = runner[: runner.index("New-Item")]
        for guard in (
            "GITHUB_ACTIONS",
            "RUNNER_ENVIRONMENT",
            "github-hosted",
            "GITHUB_REPOSITORY",
            "GITHUB_REF",
            "GITHUB_RUN_ID",
            "Administrator",
        ):
            self.assertIn(guard, before_writes)
        native = (NEXT / "tests" / "Integration" / "main.cpp").read_text(
            encoding="utf-8"
        )
        self.assertLess(
            native.index("Guard(root, token);"),
            native.index("created = manager->Create"),
        )
        self.assertIn(
            "Require", (CORE / "ServicePolicy.cpp").read_text(encoding="utf-8")
        )
        self.assertIn("LegacyEvents()", native)

    def test_system_host_storage_is_protected_and_content_addressed(self) -> None:
        text = (CORE / "ServiceHostDeployment.cpp").read_text(encoding="utf-8")
        for guard in (
            "FOLDERID_ProgramFiles",
            "BCRYPT_SHA256_ALGORITHM",
            "CREATE_NEW",
            "VerifyProtected",
            "FILE_FLAG_OPEN_REPARSE_POINT",
            "WinBuiltinAdministratorsSid",
        ):
            self.assertIn(guard, text)
        self.assertIn("O:BAG:BAD:P", text)

    def test_test_backend_is_compile_time_only_and_separate_output(self) -> None:
        project = (APP / "App.vcxproj").read_text(encoding="utf-8")
        self.assertIn("NssmNextUiTestBackend", project)
        self.assertIn("NSSM_NEXT_UI_TEST_BACKEND", project)
        self.assertIn("UiTests\\", project)
        tree = ET.parse(APP / "App.vcxproj")
        fake = [
            element for element in tree.iter() if "Fakes" in element.get("Include", "")
        ]
        self.assertEqual(len(fake), 1)
        self.assertIn("'true'", fake[0].get("Condition", ""))
        ui = (APP / "MainWindow.xaml.cpp").read_text(encoding="utf-8")
        self.assertIn("#ifdef NSSM_NEXT_UI_TEST_BACKEND", ui)
        self.assertIn("内存测试模式", ui)

    def test_native_policy_tests_do_not_link_windows_service_implementation(
        self,
    ) -> None:
        project = (NEXT / "tests" / "Management" / "Management.vcxproj").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("ProjectReference", project)
        self.assertNotIn("NativeServiceActions", project)
        self.assertNotIn("Core.vcxproj", project)
        self.assertIn("MemoryServices.cpp", project)
        self.assertIn("ServicePolicy.cpp", project)


if __name__ == "__main__":
    unittest.main()
