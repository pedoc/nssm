#include "../../src/Core/ServicePolicy.h"
#include "../Fakes/MemoryServices.h"
#include <functional>
#include <iostream>

using namespace nssm_next;
unsigned RunAdvancedTests();
namespace {
unsigned checks = 0;
void Check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
void Reject(const std::function<void()> &action, const char *message) {
    bool rejected = false;
    try {
        action();
    } catch (const ServiceError &) {
        rejected = true;
    }
    Check(rejected, message);
}
ServiceConfiguration Configuration(std::wstring name = L"example") {
    ServiceConfiguration value;
    value.name = std::move(name);
    value.application = L"C:\\Applications\\worker.exe";
    value.arguments = L"--value \"with spaces\"";
    return value;
}
ServiceInfo Owned() {
    ServiceInfo service;
    static_cast<ServiceConfiguration &>(service) = NormalizeConfiguration(Configuration());
    service.managed_by = L"NssmNext";
    service.schema = MANAGED_SCHEMA_VERSION;
    service.instance_id = L"{12345678-1234-1234-1234-123456789abc}";
    service.revision = 1;
    service.binary_path = ServiceHostCommandLine(
        L"C:\\Program Files\\NssmNext\\ServiceHosts\\test\\NssmNext.ServiceHost.exe");
    service.account = L"LocalSystem";
    return service;
}
void PolicyTests() {
    const auto normalized = NormalizeConfiguration(Configuration());
    Check(normalized.display_name == L"example", "display name defaults to service name");
    Check(normalized.working_directory.empty(), "empty directory preserves host automatic-directory semantics");
    Check(normalized.arguments == L"--value \"with spaces\"", "arguments retain quoting");
    Check(normalized.startup == StartupMode::Automatic, "automatic startup default");
    Check(SameConfiguration(normalized, normalized), "configuration equality");
    auto changed = normalized;
    changed.description = L"changed";
    Check(!SameConfiguration(normalized, changed), "description affects configuration equality");
    for (const auto &name : {L"", L"bad/name", L"bad\\name", L" bad", L"bad ", L"bad\nname"}) {
        Reject([&] { NormalizeConfiguration(Configuration(name)); },
               "unsafe service name accepted");
    }
    Reject([&] { NormalizeConfiguration(Configuration(std::wstring(257, L'x'))); },
           "oversized name accepted");
    auto invalid = normalized;
    invalid.application.clear();
    Reject([&] { NormalizeConfiguration(invalid); }, "missing application accepted");
    invalid.application = L"worker.exe";
    Reject([&] { NormalizeConfiguration(invalid); }, "relative application accepted");
    invalid.application = L"\\\\server\\share\\worker.exe";
    Reject([&] { NormalizeConfiguration(invalid); }, "remote application accepted");
    invalid.application = L"\"C:\\worker.exe\"";
    Reject([&] { NormalizeConfiguration(invalid); }, "quoted application accepted");
    invalid = normalized;
    invalid.arguments = std::wstring(L"valid\0hidden", 12);
    Reject([&] { NormalizeConfiguration(invalid); }, "embedded null arguments accepted");
    invalid = normalized;
    invalid.arguments.assign(32767, L'x');
    Reject([&] { NormalizeConfiguration(invalid); }, "oversized command line accepted");
    invalid = normalized;
    invalid.working_directory = L"relative";
    Reject([&] { NormalizeConfiguration(invalid); }, "relative working directory accepted");
    invalid = normalized;
    invalid.startup = static_cast<StartupMode>(99);
    Reject([&] { NormalizeConfiguration(invalid); }, "invalid startup accepted");
    invalid = normalized;
    invalid.description.assign(16383, L'x');
    Reject([&] { NormalizeConfiguration(invalid); }, "oversized description accepted");
    const auto unicode = NormalizeConfiguration(Configuration(L"测试服务"));
    Check(unicode.name == L"测试服务", "Unicode service name rejected");
    const auto owned = Owned();
    Check(IsManagedService(owned), "valid managed service not recognized");
    auto foreign = owned;
    foreign.managed_by.clear();
    Check(!IsManagedService(foreign), "legacy Application-only service included");
    Reject([&] { RequireSameService(owned, foreign, false); }, "unowned service can be controlled");
    foreign = owned;
    foreign.schema = 2;
    Check(!IsManagedService(foreign), "unknown schema accepted");
    foreign = owned;
    foreign.instance_id = L"not-a-guid";
    Check(!IsManagedService(foreign), "invalid GUID accepted");
    foreign = owned;
    foreign.revision = 0;
    Check(!IsManagedService(foreign), "missing revision accepted");
    foreign = owned;
    foreign.binary_path = L"\"C:\\other.exe\" --service";
    Check(!IsManagedService(foreign), "foreign executable accepted");
    foreign = owned;
    foreign.binary_path += L" unexpected";
    Check(!IsManagedService(foreign), "extra host arguments accepted");
    foreign = owned;
    foreign.instance_id = L"{87654321-1234-1234-1234-123456789abc}";
    Reject([&] { RequireSameService(owned, foreign, false); }, "recreated service accepted");
    foreign = owned;
    foreign.binary_path = ServiceHostCommandLine(L"D:\\Other\\NssmNext.ServiceHost.exe");
    Reject([&] { RequireSameService(owned, foreign, false); }, "changed ImagePath accepted");
    foreign = owned;
    ++foreign.revision;
    Reject([&] { RequireSameService(owned, foreign, true); }, "stale edit accepted");
    RequireSameService(owned, foreign, false);
    Check(true, "revision changes do not block a fresh control action");
    foreign = owned;
    foreign.description = L"external change";
    Reject([&] { RequireSameService(owned, foreign, true); },
           "external configuration edit accepted");
    Reject([&] { ServiceHostCommandLine(L"relative.exe"); }, "relative host accepted");
}
void CrudTests() {
    testing::MemoryServices manager(L"C:\\Windows\\System32\\cmd.exe", false);
    Check(manager.Read().services.empty(), "empty owned list expected");
    Check(manager.CanWrite() && manager.IsTestBackend(), "memory backend must be explicit");
    const auto first = manager.Create(Configuration());
    Check(IsManagedService(first), "create must attach ownership identity");
    Check(first.state == ServiceState::Stopped, "create must not start target");
    Check(manager.Read().services.size() == 1, "created service missing");
    Reject([&] { manager.Create(Configuration()); }, "duplicate name overwrote service");
    auto foreign = Owned();
    foreign.name = L"legacy";
    foreign.managed_by.clear();
    manager.Inject(foreign);
    Check(manager.Read().services.size() == 1, "foreign service appears in list");
    Reject([&] { manager.Start(foreign); }, "foreign service start accepted");
    Reject([&] { manager.Stop(foreign); }, "foreign service stop accepted");
    Reject([&] { manager.Remove(foreign); }, "foreign service delete accepted");
    manager.Start(first);
    auto running = manager.Read(first);
    Check(running.state == ServiceState::Running && running.process_id != 0,
          "start did not update state");
    auto edit = static_cast<ServiceConfiguration>(running);
    edit.display_name = L"Edited service";
    edit.description = L"Updated";
    edit.arguments = L"--new-value";
    edit.startup = StartupMode::Manual;
    manager.Update(running, edit);
    auto updated = manager.Read(first);
    Check(updated.display_name == L"Edited service" && updated.arguments == L"--new-value",
          "edit not saved");
    Check(updated.revision == first.revision + 1, "edit revision not advanced");
    Check(updated.state == ServiceState::Running, "edit unexpectedly stopped service");
    Reject([&] { manager.Update(first, edit); }, "stale configuration saved");
    auto renamed = edit;
    renamed.name = L"renamed";
    Reject([&] { manager.Update(updated, renamed); }, "service rename accepted");
    manager.Stop(updated);
    updated = manager.Read(updated);
    Check(updated.state == ServiceState::Stopped && updated.process_id == 0,
          "stop did not update state");
    manager.Start(updated);
    manager.Remove(updated);
    Check(manager.Read().services.empty(), "delete did not remove owned service");
    Reject([&] { manager.Read(updated); }, "deleted service still readable");
    const auto recreated = manager.Create(Configuration());
    Check(recreated.instance_id != first.instance_id, "recreated service reused identity");
    Reject([&] { manager.Remove(first); }, "old selection deleted recreated service");
}
} // namespace
int main() {
    try {
        PolicyTests();
        CrudTests();
        RunAdvancedTests();
        std::cout
            << "PASS " << checks
            << " management policy and in-memory CRUD assertions; no Windows service API linked\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL management test: " << error.what() << '\n';
        return 1;
    }
}
