#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nssm_next {
struct ServiceInfo {
    std::wstring name;
    std::wstring display_name;
    std::wstring application;
    std::uint32_t state;
    std::uint32_t process_id;
};

struct ServiceSnapshot {
    std::vector<ServiceInfo> services;
    std::uint32_t inaccessible_parameters = 0;
};

// Query-only API. Never creates keys, changes service configuration or starts processes.
ServiceSnapshot ReadManagedServices();
} // namespace nssm_next
