#pragma once
#include "NativeServicesInternal.h"

namespace nssm_next { namespace detail {
std::vector<RawConfigurationValue> ReadConfigurationValues(const std::wstring &name, HKEY parameters);
void ReadAdvancedConfiguration(SC_HANDLE service, HKEY parameters, ServiceInfo &record);
void PreparePrivateEventSource(const std::wstring &host);

class ConfigurationJournal {
  public:
    explicit ConfigurationJournal(HKEY parameters);
    void Apply(const ServiceConfiguration *before, const ServiceConfiguration &after);
    bool Rollback() noexcept;
  private:
    HKEY parameters;
    std::vector<RawConfigurationValue> touched;
    std::vector<std::wstring> created_keys;
    void Write(const RawConfigurationValue &value, bool record);
};
} }
