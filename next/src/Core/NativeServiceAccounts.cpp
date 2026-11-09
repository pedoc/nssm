#include "NativeServiceAccounts.h"
#include "ServiceConfigurationCodec.h"
#include <ntsecapi.h>
#include <memory>
#include <utility>
#include <vector>

namespace nssm_next { namespace detail {
namespace {
constexpr wchar_t LocalSystemName[] = L"LocalSystem";
constexpr wchar_t LocalServiceName[] = L"NT AUTHORITY\\LocalService";
constexpr wchar_t NetworkServiceName[] = L"NT AUTHORITY\\NetworkService";
constexpr wchar_t VirtualAccountPrefix[] = L"NT SERVICE\\";
constexpr wchar_t ServiceLogonRight[] = L"SeServiceLogonRight";

bool SameName(const std::wstring &left, const std::wstring &right) {
    const int result = CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE);
    if (!result) Fail(L"Compare service account names");
    return result == CSTR_EQUAL;
}

void ValidateAccountName(const std::wstring &name) {
    if (name.empty() || name.find(L'\0') != std::wstring::npos)
        throw ServiceError(L"Enter a valid service account name.", ERROR_INVALID_ACCOUNT_NAME);
}

std::wstring ExpandLocalAccountName(const std::wstring &name) {
    if (name.compare(0, 2, L".\\") != 0) return name;
    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD length = static_cast<DWORD>(sizeof(computer) / sizeof(computer[0]));
    if (!GetComputerNameW(computer, &length)) Fail(L"Read the local computer name for a service account");
    return std::wstring(computer, length) + L"\\" + name.substr(2);
}

struct LocalMemoryDeleter {
    void operator()(void *value) const noexcept { if (value) LocalFree(value); }
};
using LocalMemory = std::unique_ptr<void, LocalMemoryDeleter>;

struct ResolvedAccount {
    LocalMemory sid;
    SID_NAME_USE use = SidTypeUnknown;
};

// A deleted/unmapped current account is not an error when switching away from it.
// Other lookup failures must not be mistaken for a different account identity.
bool TryResolveAccount(const std::wstring &name, ResolvedAccount &resolved) {
    ValidateAccountName(name);
    const std::wstring expanded = ExpandLocalAccountName(name);
    DWORD sidBytes = 0, domainCharacters = 0;
    SID_NAME_USE use = SidTypeUnknown;
    if (LookupAccountNameW(nullptr, expanded.c_str(), nullptr, &sidBytes, nullptr,
                           &domainCharacters, &use))
        Fail(L"Look up the service account", ERROR_INVALID_DATA);
    DWORD error = GetLastError();
    if (error == ERROR_NONE_MAPPED) return false;
    if (error != ERROR_INSUFFICIENT_BUFFER) Fail(L"Look up the service account", error);
    if (!sidBytes) Fail(L"Look up the service account", ERROR_INVALID_SID);

    LocalMemory sid(LocalAlloc(LMEM_FIXED, sidBytes));
    if (!sid) Fail(L"Allocate the service account SID", ERROR_NOT_ENOUGH_MEMORY);
    std::vector<wchar_t> domain(domainCharacters ? domainCharacters : 1);
    domainCharacters = static_cast<DWORD>(domain.size());
    if (!LookupAccountNameW(nullptr, expanded.c_str(), sid.get(), &sidBytes, domain.data(),
                            &domainCharacters, &use)) {
        error = GetLastError();
        if (error == ERROR_NONE_MAPPED) return false;
        Fail(L"Look up the service account", error);
    }
    if (!IsValidSid(sid.get())) Fail(L"Validate the service account SID", ERROR_INVALID_SID);
    resolved.sid = std::move(sid);
    resolved.use = use;
    return true;
}

ResolvedAccount ResolveAccount(const std::wstring &name) {
    ResolvedAccount resolved;
    if (!TryResolveAccount(name, resolved))
        throw ServiceError(L"The selected service account could not be found. Enter a valid user account.",
                           ERROR_NONE_MAPPED);
    return resolved;
}

bool BuiltinKind(PSID sid, AccountKind &kind) {
    if (IsWellKnownSid(sid, WinLocalSystemSid)) kind = AccountKind::LocalSystem;
    else if (IsWellKnownSid(sid, WinLocalServiceSid)) kind = AccountKind::LocalService;
    else if (IsWellKnownSid(sid, WinNetworkServiceSid)) kind = AccountKind::NetworkService;
    else return false;
    return true;
}

void RequireUserAccount(const ResolvedAccount &account) {
    if (account.use != SidTypeUser)
        throw ServiceError(L"Select a user account, not a group, computer, or another service's virtual account.",
                           ERROR_INVALID_ACCOUNT_NAME);
}

std::wstring CanonicalUserName(PSID sid) {
    DWORD nameCharacters = 0, domainCharacters = 0;
    SID_NAME_USE use = SidTypeUnknown;
    if (LookupAccountSidW(nullptr, sid, nullptr, &nameCharacters, nullptr, &domainCharacters, &use))
        Fail(L"Read the canonical service account name", ERROR_INVALID_DATA);
    const DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER)
        Fail(L"Read the canonical service account name", error);
    std::vector<wchar_t> name(nameCharacters ? nameCharacters : 1);
    std::vector<wchar_t> domain(domainCharacters ? domainCharacters : 1);
    nameCharacters = static_cast<DWORD>(name.size());
    domainCharacters = static_cast<DWORD>(domain.size());
    if (!LookupAccountSidW(nullptr, sid, name.data(), &nameCharacters, domain.data(),
                           &domainCharacters, &use))
        Fail(L"Read the canonical service account name");
    if (use != SidTypeUser || !name[0] || !domain[0])
        throw ServiceError(L"The selected service account does not have a usable domain and user name.",
                           ERROR_INVALID_ACCOUNT_NAME);
    return std::wstring(domain.data()) + L"\\" + name.data();
}

bool SameIdentity(const std::wstring &desiredName, AccountKind desiredKind, PSID desiredSid,
                  const ServiceInfo &current) {
    if (SameName(desiredName, current.account)) return true;
    const LoginConfiguration existing = ReadLoginConfiguration(current.account, current.name);
    if (desiredKind != AccountKind::Custom && existing.kind != AccountKind::Custom) {
        if (desiredKind != existing.kind) return false;
        return desiredKind != AccountKind::VirtualService ||
               SameName(desiredName, std::wstring(VirtualAccountPrefix) + current.name);
    }
    if (desiredKind == AccountKind::VirtualService || existing.kind != AccountKind::Custom)
        return false;

    ResolvedAccount oldAccount;
    if (!TryResolveAccount(current.account, oldAccount)) return false;
    if (desiredSid) return EqualSid(desiredSid, oldAccount.sid.get()) != FALSE;
    AccountKind oldKind = AccountKind::Custom;
    return BuiltinKind(oldAccount.sid.get(), oldKind) && oldKind == desiredKind;
}

bool HasNewPassword(const CredentialRequest &credentials) {
    if (!credentials.password_supplied || credentials.password.Empty()) return false;
    // SecretValue is the only owner: do not make a string copy even for validation.
    const wchar_t *password = credentials.password.Data();
    for (std::size_t i = 0; i < credentials.password.Size(); ++i) {
        if (password[i] == L'\0')
            throw ServiceError(L"A service account password cannot contain an embedded NUL character.");
    }
    return true;
}

struct PolicyDeleter {
    void operator()(void *value) const noexcept { if (value) LsaClose(value); }
};
using PolicyHandle = std::unique_ptr<void, PolicyDeleter>;
struct LsaMemoryDeleter {
    void operator()(LSA_UNICODE_STRING *value) const noexcept { if (value) LsaFreeMemory(value); }
};
using AccountRights = std::unique_ptr<LSA_UNICODE_STRING, LsaMemoryDeleter>;

PolicyHandle OpenAccountPolicy(ACCESS_MASK access) {
    LSA_OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    LSA_HANDLE raw = nullptr;
    const NTSTATUS status = LsaOpenPolicy(nullptr, &attributes, access, &raw);
    PolicyHandle policy(raw);
    if (status != 0) Fail(L"Open the local service account policy", LsaNtStatusToWinError(status));
    return policy;
}

bool HasServiceLogonRight(LSA_HANDLE policy, PSID sid, bool &accountExists) {
    LSA_UNICODE_STRING *raw = nullptr;
    ULONG count = 0;
    const NTSTATUS status = LsaEnumerateAccountRights(policy, sid, &raw, &count);
    AccountRights rights(raw);
    if (status != 0) {
        const DWORD error = LsaNtStatusToWinError(status);
        if (error == ERROR_FILE_NOT_FOUND) {
            accountExists = false;
            return false;
        }
        Fail(L"Check the account's 'Log on as a service' right", error);
    }
    accountExists = true;
    if (count && !rights) Fail(L"Read service account rights", ERROR_INVALID_DATA);
    constexpr int characters = static_cast<int>(sizeof(ServiceLogonRight) / sizeof(wchar_t) - 1);
    for (ULONG i = 0; i < count; ++i) {
        const auto &right = rights.get()[i];
        if (right.Length != characters * sizeof(wchar_t) || !right.Buffer) continue;
        // LSA strings have byte lengths and need not be NUL-terminated.
        const int comparison = CompareStringOrdinal(right.Buffer, characters, ServiceLogonRight,
                                                    characters, TRUE);
        if (!comparison) Fail(L"Compare service account rights");
        if (comparison == CSTR_EQUAL) return true;
    }
    return false;
}
} // namespace

LoginConfiguration ReadLoginConfiguration(const std::wstring &account, const std::wstring &serviceName) {
    LoginConfiguration login;
    if (account.empty() || SameName(account, LocalSystemName) || SameName(account, L"NT AUTHORITY\\SYSTEM"))
        login.kind = AccountKind::LocalSystem;
    else if (SameName(account, LocalServiceName) || SameName(account, L"NT AUTHORITY\\LOCAL SERVICE"))
        login.kind = AccountKind::LocalService;
    else if (SameName(account, NetworkServiceName) || SameName(account, L"NT AUTHORITY\\NETWORK SERVICE"))
        login.kind = AccountKind::NetworkService;
    else if (!serviceName.empty() && SameName(account, std::wstring(VirtualAccountPrefix) + serviceName))
        login.kind = AccountKind::VirtualService;
    else {
        login.kind = AccountKind::Custom;
        login.username = account;
    }
    return login;
}

PreparedAccountChange PrepareAccountChange(const ServiceConfiguration &configuration,
    const ServiceInfo *current, const CredentialRequest &credentials) {
    PreparedAccountChange prepared;
    prepared.name = AccountName(configuration.login, configuration.name);
    ValidateAccountName(prepared.name);
    AccountKind kind = ReadLoginConfiguration(prepared.name, configuration.name).kind;
    prepared.custom = kind == AccountKind::Custom;
    prepared.grant_service_logon = prepared.custom && credentials.grant_service_logon;

    // An unrelated edit must work without contacting an unchanged account's domain.
    // Blank input, even if marked supplied, is never an empty-password reset.
    if (prepared.custom && current && SameName(prepared.name, current->account) &&
        (!credentials.password_supplied || credentials.password.Empty())) {
        prepared.name = current->account;
        return prepared;
    }

    ResolvedAccount resolved;
    if (prepared.custom) {
        resolved = ResolveAccount(prepared.name);
        if (BuiltinKind(resolved.sid.get(), kind)) {
            LoginConfiguration builtin;
            builtin.kind = kind;
            prepared.name = AccountName(builtin, configuration.name);
            prepared.custom = false;
            prepared.grant_service_logon = false;
        } else {
            RequireUserAccount(resolved);
        }
    }

    const bool same = current && SameIdentity(prepared.name, kind, resolved.sid.get(), *current);
    if (prepared.custom) {
        prepared.password_supplied = HasNewPassword(credentials);
        if (!same && !prepared.password_supplied)
            throw ServiceError(L"Supply a nonempty password when creating a service with a custom account or changing its account.");
        // Keep the SCM spelling for an existing identity, especially .\user.
        prepared.name = same ? current->account : CanonicalUserName(resolved.sid.get());
    }
    prepared.change = !same || prepared.password_supplied;
    return prepared;
}

bool PrepareServiceLogonRight(const PreparedAccountChange &account) {
    if (!account.custom || (!account.change && !account.grant_service_logon)) return false;
    ResolvedAccount resolved = ResolveAccount(account.name);
    AccountKind kind = AccountKind::Custom;
    if (BuiltinKind(resolved.sid.get(), kind)) return false;
    RequireUserAccount(resolved);

    // Reading requires only LOOKUP_NAMES. Request CREATE_ACCOUNT only if the
    // account has no local LSA policy entry and an explicitly authorized grant is needed.
    PolicyHandle policy = OpenAccountPolicy(POLICY_LOOKUP_NAMES);
    bool accountExists = false;
    if (HasServiceLogonRight(policy.get(), resolved.sid.get(), accountExists)) return false;
    if (!account.grant_service_logon)
        throw ServiceError(L"The selected account does not have the 'Log on as a service' right. "
                           L"Explicitly allow NssmNext to grant this machine-wide right, or ask an administrator to grant it before saving.",
                           ERROR_LOGON_TYPE_NOT_GRANTED);
    if (!accountExists) {
        policy = OpenAccountPolicy(POLICY_LOOKUP_NAMES | POLICY_CREATE_ACCOUNT);
        // Another writer may have granted the right while the policy was reopened.
        if (HasServiceLogonRight(policy.get(), resolved.sid.get(), accountExists)) return false;
    }

    LSA_UNICODE_STRING right{};
    right.Buffer = const_cast<wchar_t *>(ServiceLogonRight);
    right.Length = static_cast<USHORT>(sizeof(ServiceLogonRight) - sizeof(wchar_t));
    right.MaximumLength = static_cast<USHORT>(sizeof(ServiceLogonRight));
    const NTSTATUS status = LsaAddAccountRights(policy.get(), resolved.sid.get(), &right, 1);
    if (status != 0)
        Fail(L"Grant the account's 'Log on as a service' right", LsaNtStatusToWinError(status));
    // This machine-wide grant is deliberately not revoked by service rollback.
    return true;
}

void CommitServiceAccount(SC_HANDLE service, const PreparedAccountChange &account,
    const CredentialRequest &credentials) {
    if (!account.change) return;
    ValidateAccountName(account.name);
    const wchar_t *password = nullptr;
    if (account.custom && account.password_supplied) {
        if (!HasNewPassword(credentials))
            throw ServiceError(L"The new service account password is no longer available. Re-enter it before saving.");
        password = credentials.password.Data();
    }
    // Final, non-fully-reversible write. Never pass an empty password to reset it,
    // and never change any other SCM field as part of this commit.
    if (!ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
                              nullptr, nullptr, nullptr, nullptr, account.name.c_str(), password, nullptr))
        Fail(L"Update the service logon account");
}
} } // namespace nssm_next::detail
