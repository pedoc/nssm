#pragma once
#include "ServiceModel.h"

namespace nssm_next {
std::shared_ptr<IServiceManager> CreateNativeServiceManager();
ServiceSnapshot ReadManagedServices();
void ValidateApplicationFiles(const ServiceConfiguration &configuration);
std::wstring CurrentExecutablePath();
} // namespace nssm_next
