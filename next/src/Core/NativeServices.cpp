#include "NativeServices.h"
#include "ApplicationValuePolicy.h"

#include <windows.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {
using ScHandle =
    std::unique_ptr<std::remove_pointer<SC_HANDLE>::type, decltype(&CloseServiceHandle)>;
constexpr DWORD ENUM_BUFFER_BYTES = 256 * 1024;

std::wstring ReadApplication(const wchar_t *service_name, bool &inaccessible) {
    const std::wstring key =
        std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + service_name + L"\\Parameters";
    const DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
    DWORD bytes = 0;
    LONG result = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"Application", flags, nullptr,
                               nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        return {};
    if (result != ERROR_SUCCESS) {
        inaccessible = true;
        return {};
    }
    if (bytes == 0)
        return {};
    if (!nssm_next::detail::IsValidApplicationBytes(bytes)) {
        inaccessible = true;
        return {};
    }
    std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1, L'\0');
    result = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"Application", flags, nullptr,
                          text.data(), &bytes);
    if (result != ERROR_SUCCESS) {
        inaccessible = true;
        return {};
    }
    return std::wstring(text.data());
}
} // namespace

namespace nssm_next {
ServiceSnapshot ReadManagedServices() {
    ScHandle manager(
        OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE),
        &CloseServiceHandle);
    if (!manager)
        throw std::runtime_error("Cannot query the service manager");
    std::vector<BYTE> buffer(ENUM_BUFFER_BYTES);
    ServiceSnapshot snapshot;
    DWORD resume = 0;
    for (;;) {
        DWORD required = 0;
        DWORD count = 0;
        const BOOL complete = EnumServicesStatusExW(
            manager.get(), SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buffer.data(),
            ENUM_BUFFER_BYTES, &required, &count, &resume, nullptr);
        const DWORD error = complete ? ERROR_SUCCESS : GetLastError();
        if (!complete && error != ERROR_MORE_DATA) {
            throw std::runtime_error("Cannot enumerate Windows services");
        }
        const auto entries = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW *>(buffer.data());
        for (DWORD index = 0; index < count; ++index) {
            bool inaccessible = false;
            auto application = ReadApplication(entries[index].lpServiceName, inaccessible);
            if (inaccessible)
                ++snapshot.inaccessible_parameters;
            if (application.empty())
                continue;
            snapshot.services.push_back({entries[index].lpServiceName, entries[index].lpDisplayName,
                                         std::move(application),
                                         entries[index].ServiceStatusProcess.dwCurrentState,
                                         entries[index].ServiceStatusProcess.dwProcessId});
        }
        if (complete)
            break;
        if (count == 0)
            throw std::runtime_error("Service enumeration made no progress");
    }
    std::sort(
        snapshot.services.begin(), snapshot.services.end(),
        [](const ServiceInfo &left, const ServiceInfo &right) { return left.name < right.name; });
    return snapshot;
}
} // namespace nssm_next
