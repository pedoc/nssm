#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "ServiceConfigurationFields.h"
#include "ServiceCredentials.h"

namespace nssm_next {
constexpr std::uint32_t MANAGED_SCHEMA_VERSION = 1;
constexpr std::uint32_t INVALID_CONFIGURATION = 87;
constexpr std::uint32_t SERVICE_NOT_OWNED = 5;
constexpr std::uint32_t STALE_SERVICE = 1237;

class ServiceError : public std::runtime_error {
  public:
    ServiceError(std::wstring message, std::uint32_t code = INVALID_CONFIGURATION)
        : std::runtime_error("NssmNext service operation failed"), message(std::move(message)),
          code(code) {
    }
    std::wstring message;
    std::uint32_t code;
};

enum class StartupMode : std::uint32_t {
    Automatic,
    DelayedAutomatic,
    Manual,
    Disabled
};
enum class ServiceState : std::uint32_t {
    Stopped = 1,
    StartPending,
    StopPending,
    Running,
    ContinuePending,
    PausePending,
    Paused
};

struct ServiceConfiguration {
    std::wstring name;
    std::wstring display_name;
    std::wstring application;
    std::wstring arguments;
    std::wstring working_directory;
    std::wstring description;
    StartupMode startup = StartupMode::Automatic;
    LoginConfiguration login;
    std::vector<std::wstring> dependencies;
    ProcessConfiguration process;
    ShutdownConfiguration shutdown;
    ExitConfiguration exit;
    IoConfiguration io;
    RotationConfiguration rotation;
    EnvironmentConfiguration environment;
    HookConfiguration hooks;
};

struct ServiceInfo : ServiceConfiguration {
    std::wstring managed_by;
    std::uint32_t schema = 0;
    std::wstring instance_id;
    std::uint64_t revision = 0;
    std::wstring binary_path;
    std::wstring account;
    ServiceState state = ServiceState::Stopped;
    std::uint32_t process_id = 0;
    std::vector<RawConfigurationValue> raw_configuration;
    std::vector<ConfigurationDiagnostic> diagnostics;
};

struct ServiceSnapshot {
    std::vector<ServiceInfo> services;
    std::uint32_t inaccessible_parameters = 0;
};

class IServiceManager {
  public:
    virtual ~IServiceManager() = default;
    virtual ServiceSnapshot Read() = 0;
    virtual ServiceInfo Read(const ServiceInfo &expected) = 0;
    virtual ServiceInfo Create(const ServiceConfiguration &configuration, const CredentialRequest &credentials = {}) = 0;
    virtual void Update(const ServiceInfo &expected, const ServiceConfiguration &configuration, const CredentialRequest &credentials = {}) = 0;
    virtual void Remove(const ServiceInfo &expected) = 0;
    virtual void Start(const ServiceInfo &expected) = 0;
    virtual void Stop(const ServiceInfo &expected) = 0;
    virtual bool CanWrite() const noexcept = 0;
    virtual bool IsTestBackend() const noexcept {
        return false;
    }
};
} // namespace nssm_next
