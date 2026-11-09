#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "NativeServices.h"
#include "ServicePolicy.h"
#include <type_traits>

namespace nssm_next {
namespace detail {
using ScHandle =
    std::unique_ptr<std::remove_pointer<SC_HANDLE>::type, decltype(&CloseServiceHandle)>;
using RegHandle = std::unique_ptr<std::remove_pointer<HKEY>::type, decltype(&RegCloseKey)>;
[[noreturn]] void Fail(const std::wstring &operation, DWORD code = GetLastError());
void CheckRegistry(LSTATUS result, const std::wstring &operation);
ScHandle OpenManager(DWORD access);
RegHandle OpenParameters(const std::wstring &name, REGSAM access, bool create = false,
                         bool optional = false);
std::wstring ReadString(HKEY key, const wchar_t *name);
std::uint32_t ReadDword(HKEY key, const wchar_t *name);
std::uint64_t ReadQword(HKEY key, const wchar_t *name);
void WriteString(HKEY key, const wchar_t *name, const std::wstring &value);
void WriteDword(HKEY key, const wchar_t *name, DWORD value);
void WriteQword(HKEY key, const wchar_t *name, std::uint64_t value);
ServiceInfo ReadRecord(SC_HANDLE service, HKEY parameters, const std::wstring &name);
ScHandle OpenOwned(SC_HANDLE manager, const ServiceInfo &expected, DWORD access,
                   REGSAM registryAccess, RegHandle &parameters, ServiceInfo &actual);
std::wstring DeployServiceHost(const std::wstring &source);
void EnsureServiceHostAccess(const std::wstring &host);

class MutationGuard {
  public:
    MutationGuard();
    ~MutationGuard();
    MutationGuard(const MutationGuard &) = delete;
    MutationGuard &operator=(const MutationGuard &) = delete;

  private:
    std::unique_ptr<void, decltype(&CloseHandle)> handle{nullptr, &CloseHandle};
    bool locked = false;
};

class WindowsServices final : public IServiceManager {
  public:
    WindowsServices();
    ServiceSnapshot Read() override;
    ServiceInfo Read(const ServiceInfo &expected) override;
    ServiceInfo Create(const ServiceConfiguration &configuration, const CredentialRequest &credentials = {}) override;
    void Update(const ServiceInfo &expected, const ServiceConfiguration &configuration, const CredentialRequest &credentials = {}) override;
    void Remove(const ServiceInfo &expected) override;
    void Start(const ServiceInfo &expected) override;
    void Stop(const ServiceInfo &expected) override;
    bool CanWrite() const noexcept override;

  private:
    std::wstring sourceHost;
};
} // namespace detail
} // namespace nssm_next
