#pragma once
#include <string>
#include <utility>
#include <vector>

namespace nssm_next {
// Never put this object into a service snapshot, log, registry value or CLI argument.
class SecretValue {
  public:
    SecretValue() = default;
    SecretValue(const SecretValue &) = delete;
    SecretValue &operator=(const SecretValue &) = delete;
    SecretValue(SecretValue &&other) noexcept : value(std::move(other.value)) {}
    SecretValue &operator=(SecretValue &&other) noexcept {
        if (this != &other) { Clear(); value = std::move(other.value); }
        return *this;
    }
    ~SecretValue() { Clear(); }
    void Assign(const wchar_t *text, std::size_t length) {
        Clear(); value.assign(text, text + length); value.push_back(L'\0');
    }
    bool Empty() const noexcept { return value.empty() || value.size() == 1; }
    const wchar_t *Data() const noexcept { return value.empty() ? L"" : value.data(); }
    std::size_t Size() const noexcept { return value.empty() ? 0 : value.size() - 1; }
    void Clear() noexcept {
        volatile wchar_t *bytes = value.empty() ? nullptr : value.data();
        for (std::size_t i = 0; i < value.size(); ++i) bytes[i] = L'\0';
        value.clear();
    }
  private:
    std::vector<wchar_t> value;
};
struct CredentialRequest {
    SecretValue password;
    bool password_supplied = false;
    bool grant_service_logon = false;
};
} // namespace nssm_next
