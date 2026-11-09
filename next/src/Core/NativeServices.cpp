#include "NativeServicesInternal.h"
#include "ApplicationValuePolicy.h"
#include "NativeConfigurationStore.h"
#include <algorithm>
#include <array>

namespace nssm_next {
std::wstring CurrentExecutablePath() {
    std::vector<wchar_t> path(32768);
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        detail::Fail(L"无法读取当前程序路径");
    return {path.data(), length};
}
void ValidateApplicationFiles(const ServiceConfiguration &configuration) {
    // Expandable paths are resolved by the host under the service identity and
    // configured environment, never against the administrator GUI environment.
    const auto expandable = [](const std::wstring &value) {
        const auto first = value.find(L'%');
        return first != std::wstring::npos && value.find(L'%', first + 1) != std::wstring::npos;
    };
    if (!expandable(configuration.application)) {
        const auto file = GetFileAttributesW(configuration.application.c_str());
        if (file == INVALID_FILE_ATTRIBUTES || (file & FILE_ATTRIBUTE_DIRECTORY))
            throw ServiceError(L"可执行文件不存在或不是文件，请检查程序路径。", ERROR_FILE_NOT_FOUND);
        DWORD type = 0;
        if (!GetBinaryTypeW(configuration.application.c_str(), &type))
            throw ServiceError(L"程序不是可直接运行的 Windows 可执行文件。脚本请使用对应解释器。", ERROR_BAD_EXE_FORMAT);
    }
    auto path = configuration.working_directory;
    if (path.empty()) {
        const auto slash = configuration.application.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return;
        path = configuration.application.substr(0, slash == 2 ? 3 : slash);
    }
    if (!expandable(path)) {
        const auto directory = GetFileAttributesW(path.c_str());
        if (directory == INVALID_FILE_ATTRIBUTES || !(directory & FILE_ATTRIBUTE_DIRECTORY))
            throw ServiceError(L"工作目录不存在，请选择有效目录。", ERROR_PATH_NOT_FOUND);
    }
}
std::shared_ptr<IServiceManager> CreateNativeServiceManager() {
    return std::make_shared<detail::WindowsServices>();
}
ServiceSnapshot ReadManagedServices() {
    return CreateNativeServiceManager()->Read();
}
} // namespace nssm_next

namespace nssm_next {
namespace detail {
namespace {
constexpr DWORD ENUM_BUFFER_BYTES = 256 * 1024;
constexpr DWORD MAX_CONFIG_BYTES = 1024 * 1024;
std::vector<BYTE> ConfigurationBuffer(SC_HANDLE service) {
    DWORD required = 0;
    QueryServiceConfigW(service, nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(QUERY_SERVICE_CONFIGW) ||
        required > MAX_CONFIG_BYTES)
        Fail(L"无法读取服务配置");
    std::vector<BYTE> buffer(required);
    if (!QueryServiceConfigW(service, reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data()),
                             required, &required))
        Fail(L"无法读取服务配置");
    return buffer;
}
std::wstring Description(SC_HANDLE service) {
    DWORD required = 0;
    QueryServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SERVICE_DESCRIPTIONW) ||
        required > MAX_CONFIG_BYTES)
        Fail(L"无法读取服务描述");
    std::vector<BYTE> buffer(required);
    if (!QueryServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, buffer.data(), required,
                              &required))
        Fail(L"无法读取服务描述");
    const auto description = reinterpret_cast<SERVICE_DESCRIPTIONW *>(buffer.data())->lpDescription;
    return description ? description : L"";
}
} // namespace
[[noreturn]] void Fail(const std::wstring &operation, DWORD code) {
    wchar_t *message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    const std::wstring detail = message ? message : L"Windows 未提供错误说明。";
    if (message)
        LocalFree(message);
    throw ServiceError(operation + L"（" + std::to_wstring(code) + L"）：" + detail, code);
}
void CheckRegistry(LSTATUS result, const std::wstring &operation) {
    if (result != ERROR_SUCCESS)
        Fail(operation, static_cast<DWORD>(result));
}
ScHandle OpenManager(DWORD access) {
    ScHandle result(OpenSCManagerW(nullptr, nullptr, access), &CloseServiceHandle);
    if (!result)
        Fail(L"无法打开 Windows 服务管理器");
    return result;
}
RegHandle OpenParameters(const std::wstring &name, REGSAM access, bool create, bool optional) {
    ValidateServiceName(name);
    const auto path = L"SYSTEM\\CurrentControlSet\\Services\\" + name + L"\\Parameters";
    HKEY raw = nullptr;
    const auto error = create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, 0,
                                                access, nullptr, &raw, nullptr)
                              : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, access, &raw);
    if (optional && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
        return RegHandle(nullptr, &RegCloseKey);
    CheckRegistry(error, L"无法打开服务参数");
    return RegHandle(raw, &RegCloseKey);
}
std::wstring ReadString(HKEY key, const wchar_t *name) {
    if (!key)
        return {};
    constexpr DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
    DWORD bytes = 0;
    auto error = RegGetValueW(key, nullptr, name, flags, nullptr, nullptr, &bytes);
    if (error == ERROR_FILE_NOT_FOUND)
        return {};
    CheckRegistry(error, L"无法读取服务参数");
    if (!bytes)
        return {};
    if (!IsValidApplicationBytes(bytes))
        Fail(L"服务字符串参数长度无效", ERROR_INVALID_DATA);
    std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1, L'\0');
    CheckRegistry(RegGetValueW(key, nullptr, name, flags, nullptr, text.data(), &bytes),
                  L"无法读取服务参数");
    return text.data();
}
std::uint32_t ReadDword(HKEY key, const wchar_t *name) {
    DWORD value = 0, size = sizeof(value);
    const auto error = RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (error == ERROR_FILE_NOT_FOUND)
        return 0;
    CheckRegistry(error, L"无法读取服务归属元数据");
    return value;
}
std::uint64_t ReadQword(HKEY key, const wchar_t *name) {
    std::uint64_t value = 0;
    DWORD size = sizeof(value);
    const auto error = RegGetValueW(key, nullptr, name, RRF_RT_REG_QWORD, nullptr, &value, &size);
    if (error == ERROR_FILE_NOT_FOUND)
        return 0;
    CheckRegistry(error, L"无法读取服务配置版本");
    return value;
}
void WriteString(HKEY key, const wchar_t *name, const std::wstring &value) {
    CheckRegistry(RegSetValueExW(key, name, 0, REG_SZ,
                                 reinterpret_cast<const BYTE *>(value.c_str()),
                                 static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))),
                  L"无法写入服务参数");
}
void WriteDword(HKEY key, const wchar_t *name, DWORD value) {
    CheckRegistry(RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value),
                                 sizeof(value)),
                  L"无法写入服务归属元数据");
}
void WriteQword(HKEY key, const wchar_t *name, std::uint64_t value) {
    CheckRegistry(RegSetValueExW(key, name, 0, REG_QWORD, reinterpret_cast<const BYTE *>(&value),
                                 sizeof(value)),
                  L"无法写入服务配置版本");
}
ServiceInfo ReadRecord(SC_HANDLE service, HKEY parameters, const std::wstring &name) {
    const auto buffer = ConfigurationBuffer(service);
    const auto config = reinterpret_cast<const QUERY_SERVICE_CONFIGW *>(buffer.data());
    ServiceInfo result;
    result.name = name;
    result.display_name = config->lpDisplayName ? config->lpDisplayName : name;
    result.binary_path = config->lpBinaryPathName ? config->lpBinaryPathName : L"";
    result.account = config->lpServiceStartName ? config->lpServiceStartName : L"";
    if (config->dwServiceType != SERVICE_WIN32_OWN_PROCESS)
        return result;
    result.managed_by = ReadString(parameters, L"NssmNextOwner");
    result.schema = ReadDword(parameters, L"NssmNextSchema");
    result.instance_id = ReadString(parameters, L"NssmNextInstance");
    result.revision = ReadQword(parameters, L"NssmNextRevision");
    result.application = ReadString(parameters, L"Application");
    result.arguments = ReadString(parameters, L"AppParameters");
    result.working_directory = ReadString(parameters, L"AppDirectory");
    result.description = Description(service);
    if (config->dwStartType == SERVICE_AUTO_START) {
        SERVICE_DELAYED_AUTO_START_INFO delayed{};
        DWORD required = 0;
        if (!QueryServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                                  reinterpret_cast<BYTE *>(&delayed), sizeof(delayed), &required))
            Fail(L"无法读取延迟启动设置");
        result.startup =
            delayed.fDelayedAutostart ? StartupMode::DelayedAutomatic : StartupMode::Automatic;
    } else
        result.startup =
            config->dwStartType == SERVICE_DISABLED ? StartupMode::Disabled : StartupMode::Manual;
    SERVICE_STATUS_PROCESS status{};
    DWORD size = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE *>(&status),
                              sizeof(status), &size))
        Fail(L"无法读取服务状态");
    result.state = static_cast<ServiceState>(status.dwCurrentState);
    result.process_id = status.dwProcessId;
    ReadAdvancedConfiguration(service, parameters, result);
    return result;
}
ScHandle OpenOwned(SC_HANDLE manager, const ServiceInfo &expected, DWORD access,
                   REGSAM registryAccess, RegHandle &parameters, ServiceInfo &actual) {
    ValidateServiceName(expected.name);
    ScHandle service(OpenServiceW(manager, expected.name.c_str(),
                                  SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | access),
                     &CloseServiceHandle);
    if (!service)
        Fail(L"无法打开所选服务");
    parameters = OpenParameters(expected.name, registryAccess);
    actual = ReadRecord(service.get(), parameters.get(), expected.name);
    RequireSameService(expected, actual, false);
    return service;
}
WindowsServices::WindowsServices() {
    const auto executable = CurrentExecutablePath();
    sourceHost =
        executable.substr(0, executable.find_last_of(L"\\/") + 1) + L"NssmNext.ServiceHost.exe";
}
bool WindowsServices::CanWrite() const noexcept {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    BOOL member = FALSE;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administrators)) {
        CheckTokenMembership(nullptr, administrators, &member);
        FreeSid(administrators);
    }
    return member != FALSE;
}
ServiceSnapshot WindowsServices::Read() {
    auto manager = OpenManager(SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    std::vector<BYTE> buffer(ENUM_BUFFER_BYTES);
    ServiceSnapshot result;
    DWORD resume = 0;
    for (;;) {
        DWORD required = 0, count = 0;
        const auto complete = EnumServicesStatusExW(
            manager.get(), SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buffer.data(),
            ENUM_BUFFER_BYTES, &required, &count, &resume, nullptr);
        const auto error = complete ? ERROR_SUCCESS : GetLastError();
        if (!complete && error != ERROR_MORE_DATA)
            Fail(L"无法枚举 Windows 服务", error);
        const auto entries = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW *>(buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            try {
                auto parameters = OpenParameters(entries[i].lpServiceName, KEY_READ, false, true);
                if (!parameters || ReadString(parameters.get(), L"NssmNextOwner") != L"NssmNext")
                    continue;
                ScHandle handle(OpenServiceW(manager.get(), entries[i].lpServiceName,
                                             SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS),
                                &CloseServiceHandle);
                if (!handle)
                    Fail(L"无法查询 NssmNext 服务");
                auto record = ReadRecord(handle.get(), parameters.get(), entries[i].lpServiceName);
                if (IsManagedService(record))
                    result.services.push_back(std::move(record));
            } catch (const ServiceError &) {
                ++result.inaccessible_parameters;
            }
        }
        if (complete)
            break;
        if (!count)
            Fail(L"枚举服务未取得进展", ERROR_INVALID_DATA);
    }
    std::sort(result.services.begin(), result.services.end(),
              [](const ServiceInfo &a, const ServiceInfo &b) { return a.name < b.name; });
    return result;
}
ServiceInfo WindowsServices::Read(const ServiceInfo &expected) {
    auto manager = OpenManager(SC_MANAGER_CONNECT);
    RegHandle parameters(nullptr, &RegCloseKey);
    ServiceInfo actual;
    const auto service = OpenOwned(manager.get(), expected, 0, KEY_READ, parameters, actual);
    return actual;
}
} // namespace detail
} // namespace nssm_next
