#include "NativeServicesInternal.h"
#include "NativeConfigurationStore.h"
#include "NativeServiceAccounts.h"
#include <array>
#include <chrono>
#include <limits>
#include <thread>

namespace nssm_next {
namespace detail {
namespace {
constexpr auto CONTROL_TIMEOUT = std::chrono::seconds(30);
constexpr auto CONTROL_POLL = std::chrono::milliseconds(100);
constexpr DWORD MAX_VALUE_BYTES = 64 * 1024;
struct SavedValue {
    const wchar_t *name;
    bool present = false;
    DWORD type = 0;
    std::vector<BYTE> bytes;
};
SavedValue Capture(HKEY key, const wchar_t *name) {
    SavedValue result{name};
    DWORD size = 0;
    auto error = RegQueryValueExW(key, name, nullptr, &result.type, nullptr, &size);
    if (error == ERROR_FILE_NOT_FOUND)
        return result;
    CheckRegistry(error, L"无法备份原服务参数");
    if (size > MAX_VALUE_BYTES)
        Fail(L"原服务参数过大，已拒绝修改", ERROR_INVALID_DATA);
    result.present = true;
    result.bytes.resize(size);
    CheckRegistry(RegQueryValueExW(key, name, nullptr, &result.type, result.bytes.data(), &size),
                  L"无法备份原服务参数");
    result.bytes.resize(size);
    return result;
}
void Restore(HKEY key, const SavedValue &value) {
    const auto error = value.present
                           ? RegSetValueExW(key, value.name, 0, value.type, value.bytes.data(),
                                            static_cast<DWORD>(value.bytes.size()))
                           : RegDeleteValueW(key, value.name);
    if (!value.present && error == ERROR_FILE_NOT_FOUND)
        return;
    CheckRegistry(error, L"无法恢复原服务参数");
}
DWORD NativeStartup(StartupMode mode) {
    if (mode == StartupMode::Manual)
        return SERVICE_DEMAND_START;
    if (mode == StartupMode::Disabled)
        return SERVICE_DISABLED;
    return SERVICE_AUTO_START;
}
void Configure(SC_HANDLE service, const ServiceConfiguration &configuration) {
    std::vector<wchar_t> dependencies;
    for (const auto &name : configuration.dependencies) { dependencies.insert(dependencies.end(), name.begin(), name.end()); dependencies.push_back(0); }
    if (dependencies.empty()) dependencies.push_back(0);
    dependencies.push_back(0);
    if (!ChangeServiceConfigW(service, SERVICE_NO_CHANGE, NativeStartup(configuration.startup),
                              SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, dependencies.data(), nullptr,
                              nullptr, configuration.display_name.c_str()))
        Fail(L"无法保存服务名称或启动类型");
    SERVICE_DESCRIPTIONW description{const_cast<wchar_t *>(configuration.description.c_str())};
    if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description))
        Fail(L"无法保存服务描述");
    if (NativeStartup(configuration.startup) == SERVICE_AUTO_START) {
        SERVICE_DELAYED_AUTO_START_INFO delayed{configuration.startup ==
                                                StartupMode::DelayedAutomatic};
        if (!ChangeServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed))
            Fail(L"无法保存延迟启动设置");
    }
}
void ApplicationValues(HKEY key, const ServiceConfiguration &configuration) {
    WriteString(key, L"Application", configuration.application);
    WriteString(key, L"AppParameters", configuration.arguments);
    WriteString(key, L"AppDirectory", configuration.working_directory);
}
SERVICE_STATUS_PROCESS Status(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD required = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE *>(&status),
                              sizeof(status), &required))
        Fail(L"无法读取服务状态");
    return status;
}
void WaitState(SC_HANDLE service, DWORD wanted, std::chrono::steady_clock::time_point deadline) {
    for (;;) {
        const auto status = Status(service);
        if (status.dwCurrentState == wanted)
            return;
        if (wanted == SERVICE_RUNNING && status.dwCurrentState == SERVICE_STOPPED)
            Fail(L"服务已退出，请检查应用程序及 NssmNext 事件日志",
                 status.dwWin32ExitCode ? status.dwWin32ExitCode : ERROR_SERVICE_SPECIFIC_ERROR);
        if (std::chrono::steady_clock::now() >= deadline)
            Fail(L"等待服务状态变化超时，未强制终止任何进程", ERROR_TIMEOUT);
        std::this_thread::sleep_for(CONTROL_POLL);
    }
}
void StopAndWait(SC_HANDLE service) {
    const auto deadline = std::chrono::steady_clock::now() + CONTROL_TIMEOUT;
    auto status = Status(service);
    while (status.dwCurrentState == SERVICE_START_PENDING ||
           status.dwCurrentState == SERVICE_CONTINUE_PENDING ||
           status.dwCurrentState == SERVICE_PAUSE_PENDING) {
        if (std::chrono::steady_clock::now() >= deadline)
            Fail(L"服务仍在转换状态，未删除服务", ERROR_TIMEOUT);
        std::this_thread::sleep_for(CONTROL_POLL);
        status = Status(service);
    }
    if (status.dwCurrentState == SERVICE_STOPPED)
        return;
    if (status.dwCurrentState != SERVICE_STOP_PENDING) {
        SERVICE_STATUS response{};
        if (!ControlService(service, SERVICE_CONTROL_STOP, &response)) {
            const auto error = GetLastError();
            if (error != ERROR_SERVICE_NOT_ACTIVE)
                Fail(L"无法停止服务", error);
        }
    }
    WaitState(service, SERVICE_STOPPED, deadline);
}
} // namespace
ServiceInfo WindowsServices::Create(const ServiceConfiguration &configuration, const CredentialRequest &credentials) {
    const auto normalized = NormalizeConfiguration(configuration);
    ValidateApplicationFiles(normalized);
    MutationGuard mutation;
    auto manager = OpenManager(SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    ScHandle existing(OpenServiceW(manager.get(), normalized.name.c_str(), SERVICE_QUERY_STATUS),
                      &CloseServiceHandle);
    if (existing)
        throw ServiceError(L"同名服务已存在，未覆盖现有服务。", ERROR_SERVICE_EXISTS);
    if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
        Fail(L"无法确认服务名是否可用");
    const auto account = PrepareAccountChange(normalized, nullptr, credentials);
    const auto host = DeployServiceHost(sourceHost);
    PreparePrivateEventSource(host);
    const auto binary = ServiceHostCommandLine(host);
    GUID guid{};
    const auto guidResult = CoCreateGuid(&guid);
    if (FAILED(guidResult))
        Fail(L"无法生成服务归属标识", static_cast<DWORD>(guidResult));
    std::array<wchar_t, 40> identity{};
    if (!StringFromGUID2(guid, identity.data(), static_cast<int>(identity.size())))
        Fail(L"无法生成服务标识", ERROR_INVALID_DATA);
    ServiceInfo result;
    static_cast<ServiceConfiguration &>(result) = normalized;
    result.managed_by = L"NssmNext"; result.schema = MANAGED_SCHEMA_VERSION;
    result.instance_id = identity.data(); result.revision = 1; result.binary_path = binary;
    result.account = account.name; result.login = ReadLoginConfiguration(account.name, normalized.name);
    bool rightAdded = false;
    ScHandle service(
        CreateServiceW(manager.get(), normalized.name.c_str(), normalized.display_name.c_str(),
                       SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_CHANGE_CONFIG | DELETE,
                       SERVICE_WIN32_OWN_PROCESS, NativeStartup(normalized.startup),
                       SERVICE_ERROR_NORMAL, binary.c_str(), nullptr, nullptr, nullptr, nullptr,
                       nullptr),
        &CloseServiceHandle);
    if (!service)
        Fail(L"无法创建服务");
    try {
        auto parameters = OpenParameters(normalized.name, KEY_READ | KEY_WRITE, true);
        WriteString(parameters.get(), L"NssmNextInstance", identity.data());
        WriteDword(parameters.get(), L"NssmNextSchema", MANAGED_SCHEMA_VERSION);
        WriteQword(parameters.get(), L"NssmNextRevision", 1);
        WriteString(parameters.get(), L"NssmNextOwner", L"NssmNext");
        ConfigurationJournal journal(parameters.get());
        journal.Apply(nullptr, normalized);
        Configure(service.get(), normalized);
        result.raw_configuration = ReadConfigurationValues(normalized.name, parameters.get());
        rightAdded = PrepareServiceLogonRight(account);
        CommitServiceAccount(service.get(), account, credentials);
        // No fallible configuration write follows the credential commit.
    } catch (const ServiceError &error) {
        if (!DeleteService(service.get())) {
            const auto cleanup = GetLastError();
            throw ServiceError(error.message + L"；新建服务清理也失败，需管理员检查服务 " +
                                   normalized.name + L"（" + std::to_wstring(cleanup) + L"）。",
                               error.code);
        }
        if (rightAdded) throw ServiceError(error.message + L"；本次已授予账户服务登录权限，未盲目撤销该全局权限。", error.code);
        throw;
    } catch (...) {
        const bool removed = DeleteService(service.get()) != FALSE;
        if (!removed)
            throw ServiceError(L"创建中发生异常，且新建服务清理失败：" + normalized.name);
        throw;
    }
    return result;
}
void WindowsServices::Update(const ServiceInfo &expected,
                             const ServiceConfiguration &configuration, const CredentialRequest &credentials) {
    const auto normalized = NormalizeConfiguration(configuration);
    if (normalized.name != expected.name)
        throw ServiceError(L"编辑时不能更改服务名。");
    ValidateApplicationFiles(normalized);
    MutationGuard mutation;
    auto manager = OpenManager(SC_MANAGER_CONNECT);
    RegHandle parameters(nullptr, &RegCloseKey);
    ServiceInfo actual;
    auto service = OpenOwned(manager.get(), expected, SERVICE_CHANGE_CONFIG,
                             KEY_READ | KEY_WRITE, parameters, actual);
    RequireSameService(expected, actual, true);
    if (actual.revision == (std::numeric_limits<std::uint64_t>::max)())
        throw ServiceError(L"服务配置版本无效。");
    const auto account = PrepareAccountChange(normalized, &actual, credentials);
    const auto revision = Capture(parameters.get(), L"NssmNextRevision");
    ConfigurationJournal journal(parameters.get());
    bool rightAdded = false;
    const auto rollback = [&]() {
        bool restored = journal.Rollback();
        try { Restore(parameters.get(), revision); } catch (...) { restored = false; }
        try { Configure(service.get(), actual); } catch (...) { restored = false; }
        return restored;
    };
    try {
        Configure(service.get(), normalized);
        journal.Apply(&actual, normalized);
        WriteQword(parameters.get(), L"NssmNextRevision", actual.revision + 1);
        if (account.change) {
            const auto quote = actual.binary_path.find(L'"', 1);
            const auto host = actual.binary_path.substr(1, quote - 1);
            EnsureServiceHostAccess(host);
            PreparePrivateEventSource(host);
        }
        rightAdded = PrepareServiceLogonRight(account);
        CommitServiceAccount(service.get(), account, credentials);
    } catch (const ServiceError &error) {
        const auto restored = rollback();
        throw ServiceError(error.message + (restored ? L"；已恢复原配置。" : L"；原配置未能完全恢复，请刷新检查。") +
            (rightAdded ? L" 本次新增的服务登录权限可能仍保留，未自动撤销全局权限。" : L""), error.code);
    } catch (...) {
        if (!rollback())
            throw ServiceError(L"保存发生异常，且原配置未能完全恢复，请刷新检查。");
        throw;
    }
}
void WindowsServices::Start(const ServiceInfo &expected) {
    MutationGuard mutation;
    auto manager = OpenManager(SC_MANAGER_CONNECT);
    RegHandle parameters(nullptr, &RegCloseKey);
    ServiceInfo actual;
    auto service = OpenOwned(manager.get(), expected, SERVICE_START, KEY_READ, parameters, actual);
    if (actual.state == ServiceState::Running)
        return;
    if (actual.state == ServiceState::StartPending) {
        WaitState(service.get(), SERVICE_RUNNING,
                  std::chrono::steady_clock::now() + CONTROL_TIMEOUT);
        return;
    }
    if (!StartServiceW(service.get(), 0, nullptr)) {
        const auto error = GetLastError();
        if (error != ERROR_SERVICE_ALREADY_RUNNING)
            Fail(L"无法启动服务", error);
    }
    WaitState(service.get(), SERVICE_RUNNING, std::chrono::steady_clock::now() + CONTROL_TIMEOUT);
}
void WindowsServices::Stop(const ServiceInfo &expected) {
    MutationGuard mutation;
    auto manager = OpenManager(SC_MANAGER_CONNECT);
    RegHandle parameters(nullptr, &RegCloseKey);
    ServiceInfo actual;
    auto service = OpenOwned(manager.get(), expected, SERVICE_STOP, KEY_READ, parameters, actual);
    StopAndWait(service.get());
}
void WindowsServices::Remove(const ServiceInfo &expected) {
    MutationGuard mutation;
    auto manager = OpenManager(SC_MANAGER_CONNECT);
    RegHandle parameters(nullptr, &RegCloseKey);
    ServiceInfo actual;
    auto service =
        OpenOwned(manager.get(), expected, SERVICE_STOP | DELETE, KEY_READ, parameters, actual);
    StopAndWait(service.get());
    const auto finalRecord = ReadRecord(service.get(), parameters.get(), expected.name);
    RequireSameService(expected, finalRecord, false);
    if (!DeleteService(service.get()))
        Fail(L"无法删除服务注册");
    parameters.reset();
    service.reset();
    // SCM owns registry deletion. Never delete application/log/host files here.
}
} // namespace detail
} // namespace nssm_next
