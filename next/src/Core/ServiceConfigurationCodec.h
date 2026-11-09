#pragma once
#include "ServiceModel.h"
#include <array>
#include <utility>

namespace nssm_next {
std::uint64_t ParseUnsigned(const std::wstring &text, std::uint64_t maximum, const std::wstring &field);
std::vector<std::wstring> ParseConfigurationLines(const std::wstring &text);
std::wstring JoinConfigurationLines(const std::vector<std::wstring> &lines);
std::uint64_t ParseAffinity(const std::wstring &text);
const std::array<std::pair<std::wstring, std::wstring>, 8> &HookPairs();
std::wstring AccountName(const LoginConfiguration &login, const std::wstring &serviceName);
void ValidateAdvancedConfiguration(const ServiceConfiguration &configuration);
bool SameAdvancedConfiguration(const ServiceConfiguration &left, const ServiceConfiguration &right);
std::vector<RawConfigurationValue> EncodeAdvancedConfiguration(const ServiceConfiguration &configuration);
void DecodeAdvancedConfiguration(ServiceInfo &configuration, const std::vector<RawConfigurationValue> &values);
bool SameRawConfiguration(const std::vector<RawConfigurationValue> &left, const std::vector<RawConfigurationValue> &right);
} // namespace nssm_next
