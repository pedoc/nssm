#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../../src/Core/NativeServices.h"
#include "../../src/Core/ServicePolicy.h"
#include "../../src/Core/ServiceConfigurationCodec.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>

using namespace nssm_next;
namespace {
unsigned cases = 0;
std::wstring Env(const wchar_t *name) {
    std::vector<wchar_t> value(32768);
    const auto size = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    if (!size || size >= value.size())
        return {};
    return {value.data(), size};
}
std::wstring FullPath(const std::wstring &input) {
    std::vector<wchar_t> value(32768);
    const auto size =
        GetFullPathNameW(input.c_str(), static_cast<DWORD>(value.size()), value.data(), nullptr);
    if (!size || size >= value.size())
        throw std::runtime_error("Invalid test path");
    return {value.data(), size};
}
void Guard(const std::wstring &root, const std::wstring &token) {
    const auto run = Env(L"GITHUB_RUN_ID");
    if (Env(L"GITHUB_ACTIONS") != L"true" || Env(L"RUNNER_ENVIRONMENT") != L"github-hosted" ||
        Env(L"RUNNER_OS") != L"Windows" || Env(L"GITHUB_REPOSITORY") != L"pedoc/nssm" ||
        Env(L"GITHUB_REF") != L"refs/heads/feat/winui3-unified-prototype" || run.empty() ||
        run.find_first_not_of(L"0123456789") != std::wstring::npos)
        throw std::runtime_error(
            "Real service integration requires the authorized GitHub-hosted Windows branch");
    const auto temporary = Env(L"RUNNER_TEMP");
    if (temporary.empty())
        throw std::runtime_error("RUNNER_TEMP is required");
    const auto prefix = FullPath(temporary) + L"\\";
    const auto resolved = FullPath(root);
    if (resolved.size() <= prefix.size() ||
        _wcsnicmp(resolved.c_str(), prefix.c_str(), prefix.size()) || token.size() != 32 ||
        token.find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
        throw std::runtime_error("Integration root/token is not the prepared disposable sandbox");
    if (!CreateNativeServiceManager()->CanWrite())
        throw std::runtime_error("Disposable runner is not elevated");
}
void Check(bool value, const char *name) {
    if (!value)
        throw std::runtime_error(name);
    ++cases;
    std::cout << "PASS integration: " << name << std::endl;
}
void Reject(const std::function<void()> &action, const char *name) {
    bool rejected = false;
    try {
        action();
    } catch (const ServiceError &) {
        rejected = true;
    }
    Check(rejected, name);
}
void Wait(const std::function<bool()> &condition, const char *name) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (std::chrono::steady_clock::now() < end) {
        if (condition())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    throw std::runtime_error(name);
}
std::wstring ReadUnicodeFile(const std::wstring &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Could not read fixture state");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 2 || size.QuadPart > 65536 ||
        size.QuadPart % 2) {
        CloseHandle(file);
        throw std::runtime_error("Invalid fixture state length");
    }
    std::wstring text(static_cast<std::size_t>(size.QuadPart / 2), L'\0');
    DWORD read = 0;
    const auto success =
        ReadFile(file, &text[0], static_cast<DWORD>(size.QuadPart), &read, nullptr);
    CloseHandle(file);
    if (!success || read != size.QuadPart)
        throw std::runtime_error("Could not read complete fixture state");
    return text;
}
std::vector<std::wstring> Started(const std::wstring &root) {
    WIN32_FIND_DATAW data{};
    std::vector<std::wstring> result;
    const auto search = FindFirstFileW((root + L"\\started-*.txt").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE)
        return result;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            result.push_back(root + L"\\" + data.cFileName);
    } while (FindNextFileW(search, &data));
    FindClose(search);
    return result;
}
std::vector<BYTE> LegacyEvents() {
    HKEY key = nullptr;
    const auto opened = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\NSSM", 0,
        KEY_READ, &key);
    if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND)
        return {0};
    if (opened != ERROR_SUCCESS)
        throw std::runtime_error("Cannot snapshot legacy event source");
    std::vector<BYTE> result{1};
    for (const auto *name : {L"EventMessageFile", L"TypesSupported"}) {
        DWORD type = 0, size = 0;
        auto error = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
        result.push_back(error == ERROR_FILE_NOT_FOUND ? 0 : 1);
        if (error == ERROR_FILE_NOT_FOUND)
            continue;
        if (error != ERROR_SUCCESS || size > 65536) {
            RegCloseKey(key);
            throw std::runtime_error("Invalid event source snapshot");
        }
        std::vector<BYTE> bytes(size);
        if (RegQueryValueExW(key, name, nullptr, &type, bytes.data(), &size) != ERROR_SUCCESS) {
            RegCloseKey(key);
            throw std::runtime_error("Cannot read event source snapshot");
        }
        result.insert(result.end(), reinterpret_cast<const BYTE *>(&type),
                      reinterpret_cast<const BYTE *>(&type) + sizeof(type));
        result.insert(result.end(), bytes.begin(), bytes.end());
    }
    RegCloseKey(key);
    return result;
}
void Marker(const ServiceInfo &service, const wchar_t *value) {
    std::unique_ptr<SC_HANDLE__, decltype(&CloseServiceHandle)> manager(
        OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT), &CloseServiceHandle);
    if (!manager)
        throw std::runtime_error("Cannot verify test service manager");
    std::unique_ptr<SC_HANDLE__, decltype(&CloseServiceHandle)> handle(
        OpenServiceW(manager.get(), service.name.c_str(), SERVICE_QUERY_CONFIG),
        &CloseServiceHandle);
    if (!handle)
        throw std::runtime_error("Cannot verify test service object");
    DWORD required = 0;
    QueryServiceConfigW(handle.get(), nullptr, 0, &required);
    if (!required || required > 65536)
        throw std::runtime_error("Cannot verify test service configuration");
    std::vector<BYTE> buffer(required);
    auto configuration = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
    if (!QueryServiceConfigW(handle.get(), configuration, required, &required) ||
        !configuration->lpBinaryPathName || service.binary_path != configuration->lpBinaryPathName)
        throw std::runtime_error("Test service ImagePath changed; refusing metadata write");
    HKEY raw = nullptr;
    const auto path = L"SYSTEM\\CurrentControlSet\\Services\\" + service.name + L"\\Parameters";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | KEY_SET_VALUE, &raw) !=
        ERROR_SUCCESS)
        throw std::runtime_error("Cannot open owned test metadata");
    std::unique_ptr<HKEY__, decltype(&RegCloseKey)> key(raw, &RegCloseKey);
    wchar_t identity[40]{};
    DWORD size = sizeof(identity);
    if (RegGetValueW(key.get(), nullptr, L"NssmNextInstance", RRF_RT_REG_SZ, nullptr, identity,
                     &size) != ERROR_SUCCESS ||
        service.instance_id != identity)
        throw std::runtime_error("Test service identity changed; refusing metadata write");
    const auto error = RegSetValueExW(key.get(), L"NssmNextOwner", 0, REG_SZ,
                                      reinterpret_cast<const BYTE *>(value),
                                      static_cast<DWORD>((lstrlenW(value) + 1) * sizeof(wchar_t)));
    if (error != ERROR_SUCCESS)
        throw std::runtime_error("Cannot update owned test metadata");
}
bool ServiceAbsent(const std::wstring &name) {
    std::unique_ptr<SC_HANDLE__, decltype(&CloseServiceHandle)> manager(
        OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT), &CloseServiceHandle);
    if (!manager)
        throw std::runtime_error("Cannot confirm service cleanup");
    std::unique_ptr<SC_HANDLE__, decltype(&CloseServiceHandle)> service(
        OpenServiceW(manager.get(), name.c_str(), SERVICE_QUERY_STATUS), &CloseServiceHandle);
    if (service)
        return false;
    const auto error = GetLastError();
    if (error == ERROR_SERVICE_DOES_NOT_EXIST)
        return true;
    if (error == ERROR_SERVICE_MARKED_FOR_DELETE)
        return false;
    throw std::runtime_error("Cannot confirm test service is absent");
}
std::string Utf8(const std::wstring &text) {
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string output(size, '\0');
    if (size)
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &output[0],
                            size, nullptr, nullptr);
    return output;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    std::shared_ptr<IServiceManager> manager;
    ServiceInfo created;
    bool registered = false, markerChanged = false;
    std::vector<BYTE> legacy;
    try {
        if (argc != 5)
            throw std::runtime_error("Expected application, fixture, sandbox and nonce");
        const std::wstring application = argv[1], fixture = argv[2], root = argv[3],
                           token = argv[4];
        Guard(root, token); // No service/registry/filesystem writes may precede this guard.
        legacy = LegacyEvents();
        manager = CreateNativeServiceManager();
        ServiceConfiguration configuration;
        configuration.name =
            L"NssmNextMgmtCI_" + Env(L"GITHUB_RUN_ID") + L"_" + token.substr(0, 12);
        configuration.display_name = L"NssmNext isolated management test";
        configuration.application = application;
        configuration.arguments = L"-NoProfile -NonInteractive -File \"" + fixture +
                                  L"\" -RootDirectory \"" + root + L"\" -Token " + token +
                                  L" -ArgumentProbe \"value with spaces\"";
        configuration.working_directory = root + L"\\working with spaces";
        configuration.description = L"Disposable hosted-runner service only";
        configuration.startup = StartupMode::Manual;
        configuration.process.priority = BELOW_NORMAL_PRIORITY_CLASS;
        DWORD_PTR allowed = 0, system = 0;
        if (!GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system)) throw std::runtime_error("Cannot read test CPU availability");
        unsigned cpu = 0; while (cpu < 63 && !(allowed & (DWORD_PTR{1} << cpu))) ++cpu;
        configuration.process.affinity = std::to_wstring(cpu);
        configuration.shutdown.console_ms = 750; configuration.shutdown.window_ms = 750; configuration.shutdown.threads_ms = 750;
        configuration.exit.restart_ms = 250; configuration.exit.throttle_ms = 100;
        configuration.io.output.path = root + L"\\stdout.log"; configuration.io.error.path = root + L"\\stderr.log";
        configuration.io.timestamp = true;
        configuration.rotation.enabled = true; configuration.rotation.online = true;
        configuration.rotation.larger_than_bytes = (std::uint64_t{1} << 32) | 1024;
        configuration.environment.replacement_present = true;
        configuration.environment.replacement = {L"SystemRoot=" + Env(L"SystemRoot"), L"PATH=" + Env(L"PATH"), L"TEMP=" + root,
            L"TMP=" + root, L"NSSM_NEXT_BASE=base", L"NSSM_NEXT_PROBE=base", L"NSSM_NEXT_EXE=" + application};
        configuration.environment.extra_present = true;
        configuration.environment.extra = {L"NSSM_NEXT_PROBE=overlay", L"NSSM_NEXT_EMPTY="};
        configuration.application = L"%NSSM_NEXT_EXE%";
        const auto hook = fixture.substr(0, fixture.find_last_of(L"\\/") + 1) + L"managed-hook.ps1";
        const auto &pairs = HookPairs();
        for (unsigned i = 0; i < pairs.size(); ++i) configuration.hooks.commands[i] = L"\"" + application + L"\" -NoProfile -NonInteractive -File \"" + hook +
            L"\" -RootDirectory \"" + root + L"\" -Token " + token + L" -Label " + pairs[i].first + L"-" + pairs[i].second;
        configuration.hooks.redirect_output = true;
        created = manager->Create(configuration);
        registered = true;
        std::cout << "SERVICE=" << Utf8(created.name) << std::endl;
        Check(IsManagedService(created) && created.state == ServiceState::Stopped,
              "create owns a stopped service and does not auto-start");
        auto list = manager->Read();
        Check(
            std::any_of(list.services.begin(), list.services.end(),
                        [&](const ServiceInfo &s) { return s.instance_id == created.instance_id; }),
            "owned service appears in list");
        Reject([&] { manager->Create(configuration); },
               "duplicate creation does not overwrite service");
        auto edited = configuration;
        edited.description = L"Edited through native management API";
        manager->Update(created, edited);
        auto current = manager->Read(created);
        Check(current.description == edited.description && current.revision == created.revision + 1,
              "edit persists and advances revision");
        Check(SameAdvancedConfiguration(configuration, current), "advanced settings survive unrelated native edit and reopen");
        Check(current.rotation.larger_than_bytes == configuration.rotation.larger_than_bytes, "nonzero rotation high word survives roundtrip");
        Reject([&] { manager->Update(created, configuration); },
               "stale revision cannot overwrite current configuration");
        std::atomic<unsigned> saved{0}, stale{0};
        const auto concurrent = [&](const wchar_t *description) {
            auto change = static_cast<ServiceConfiguration>(current);
            change.description = description;
            try {
                manager->Update(current, change);
                ++saved;
            } catch (const ServiceError &error) {
                if (error.code == STALE_SERVICE)
                    ++stale;
            }
        };
        std::thread first(concurrent, L"Concurrent A"), second(concurrent, L"Concurrent B");
        first.join();
        second.join();
        Check(saved == 1 && stale == 1,
              "concurrent updates serialize: one save, one stale rejection");
        current = manager->Read(created);
        Check(current.binary_path.find(L"\\ServiceHosts\\") != std::wstring::npos &&
                  current.binary_path.find(L"NssmNext.ServiceHost.exe\" --service") !=
                      std::wstring::npos,
              "SCM uses the protected standalone native host");
        manager->Start(current);
        Wait([&] { return !Started(root).empty(); }, "target did not publish a ready marker");
        const auto data = ReadUnicodeFile(Started(root).front());
        Check(data.find(L"S-1-5-18") != std::wstring::npos, "target executes as LocalSystem");
        Check(data.find(configuration.working_directory) != std::wstring::npos &&
                  data.find(L"value with spaces") != std::wstring::npos,
              "working directory and spaced arguments reach the target");
        Check(data.find(L"ENV_BASE=base") != std::wstring::npos && data.find(L"ENV_PROBE=overlay") != std::wstring::npos && data.find(L"ENV_EMPTY=") != std::wstring::npos,
            "replacement and overlay environment blocks both reach the child");
        Check(data.find(L"PRIORITY=BelowNormal") != std::wstring::npos, "configured process priority is effective");
        Check(data.find(L"AFFINITY=" + std::to_wstring(std::uint64_t{1} << cpu)) != std::wstring::npos, "configured processor affinity is effective");
        Wait([&] { return GetFileAttributesW((root + L"\\hook-Start-Pre.txt").c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW((root + L"\\hook-Start-Post.txt").c_str()) != INVALID_FILE_ATTRIBUTES; }, "startup hooks did not execute");
        Check(true, "Start Pre/Post hooks execute");
        Wait([&] { return GetFileAttributesW((root + L"\\stdout.log").c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW((root + L"\\stderr.log").c_str()) != INVALID_FILE_ATTRIBUTES; }, "I/O redirection files were not created");
        Check(true, "stdout and stderr redirection are effective");
        Marker(current, L"NotNssmNext");
        markerChanged = true;
        Reject([&] { manager->Stop(current); }, "removed ownership blocks stop");
        Reject([&] { manager->Remove(current); }, "removed ownership blocks delete");
        Marker(current, L"NssmNext");
        markerChanged = false;
        Check(manager->Read(current).state == ServiceState::Running,
              "failed foreign-control probes did not stop the owned target");
        const auto count = Started(root).size();
        {
            std::ofstream exitFile((root + L"\\exit-code.txt").c_str());
            exitFile << 17;
        }
        Wait([&] { return Started(root).size() > count; },
             "native supervisor did not restart exited target");
        Check(true, "native supervisor restarts an unexpected target exit");
        manager->Stop(current);
        Check(manager->Read(current).state == ServiceState::Stopped,
              "stop reaches SCM stopped state");
        manager->Start(current);
        Check(manager->Read(current).state == ServiceState::Running,
              "stopped service can start again");
        manager->Stop(current);
        for (const auto kind : {AccountKind::LocalService, AccountKind::NetworkService, AccountKind::VirtualService}) {
            current = manager->Read(created);
            auto accountConfiguration = static_cast<ServiceConfiguration>(current);
            accountConfiguration.login.kind = kind; accountConfiguration.login.username.clear();
            manager->Update(current, accountConfiguration);
            current = manager->Read(created);
            const auto beforeStart = Started(root).size();
            manager->Start(current);
            Wait([&] { return Started(root).size() > beforeStart; }, "alternate service identity did not start target");
            Check(manager->Read(current).state == ServiceState::Running, "builtin/virtual identity starts from protected native host");
            manager->Stop(current);
        }
        current = manager->Read(created);
        auto unknownAccount = static_cast<ServiceConfiguration>(current);
        unknownAccount.login.kind = AccountKind::Custom; unknownAccount.login.username = L".\\NssmNextMissing_" + token.substr(0, 12);
        CredentialRequest invalidCredentials; invalidCredentials.password.Assign(L"test-only-not-a-user-password", 28); invalidCredentials.password_supplied = true;
        Reject([&] { manager->Update(current, unknownAccount, invalidCredentials); }, "unresolved custom account is rejected without losing configuration");
        Check(manager->Read(created).revision == current.revision, "failed account preparation preserves revision");
        manager->Remove(current);
        registered = false;
        Wait([&] { return ServiceAbsent(created.name); },
             "deleted service remains registered with SCM");
        const auto finalList = manager->Read();
        Check(std::none_of(
                  finalList.services.begin(), finalList.services.end(),
                  [&](const ServiceInfo &s) { return s.instance_id == created.instance_id; }),
              "deleted service is absent from the owned list");
        Check(true, "delete removes owned registration");
        Check(legacy == LegacyEvents(), "shared legacy NSSM event registration remains unchanged");
        std::cout << "PASS " << cases
                  << " isolated native service integration observations; cleanup passed\n";
        return 0;
    } catch (const ServiceError &error) {
        std::cerr << "FAIL integration: " << Utf8(error.message) << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL integration: " << error.what() << '\n';
    }
    if (registered && manager) {
        try {
            if (markerChanged)
                Marker(created, L"NssmNext");
            manager->Remove(created);
            std::cerr << "CLEANUP: owned service removal requested\n";
        } catch (const ServiceError &error) {
            std::cerr << "CLEANUP FAILED: " << Utf8(error.message) << '\n';
        } catch (const std::exception &error) {
            std::cerr << "CLEANUP FAILED: " << error.what() << '\n';
        }
    }
    return 1;
}
