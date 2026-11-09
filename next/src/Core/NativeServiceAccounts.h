#pragma once
#include "NativeServicesInternal.h"

namespace nssm_next { namespace detail {
struct PreparedAccountChange {
    std::wstring name;
    bool change = false;
    bool password_supplied = false;
    bool custom = false;
    bool grant_service_logon = false;
};
PreparedAccountChange PrepareAccountChange(const ServiceConfiguration &configuration,
    const ServiceInfo *current, const CredentialRequest &credentials);
// Returns true only when this call adds a machine-wide account right.
bool PrepareServiceLogonRight(const PreparedAccountChange &account);
// Final non-fully-reversible commit: no subsequent configuration write is allowed.
void CommitServiceAccount(SC_HANDLE service, const PreparedAccountChange &account,
    const CredentialRequest &credentials);
LoginConfiguration ReadLoginConfiguration(const std::wstring &account, const std::wstring &serviceName);
} }
