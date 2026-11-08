#pragma once
#include "ServiceModel.h"

namespace nssm_next {
void ValidateServiceName(const std::wstring &name);
ServiceConfiguration NormalizeConfiguration(const ServiceConfiguration &configuration);
bool SameConfiguration(const ServiceConfiguration &left, const ServiceConfiguration &right);
bool IsManagedService(const ServiceInfo &service) noexcept;
void RequireSameService(const ServiceInfo &expected, const ServiceInfo &actual, bool configuration);
std::wstring ServiceHostCommandLine(const std::wstring &executable);
} // namespace nssm_next
