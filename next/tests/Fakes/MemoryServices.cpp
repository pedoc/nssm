#include "MemoryServices.h"
#include "../../src/Core/ServicePolicy.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace nssm_next::testing {
MemoryServices::MemoryServices(std::wstring application, bool seed) {
    if (!seed)
        return;
    ServiceConfiguration first;
    first.name = L"nssmnext-api";
    first.display_name = L"示例 API 服务";
    first.application = application;
    first.description = L"内存测试数据，不会创建系统服务。";
    const auto created = Create(first);
    Start(created);
    first.name = L"nssmnext-worker";
    first.display_name = L"示例后台任务";
    first.startup = StartupMode::Manual;
    Create(first);
    ServiceInfo legacy;
    static_cast<ServiceConfiguration &>(legacy) = first;
    legacy.name = L"legacy-nssm-hidden";
    legacy.application = application;
    records.push_back(std::move(legacy));
}
ServiceSnapshot MemoryServices::Read() {
    std::lock_guard<std::mutex> lock(mutex);
    ServiceSnapshot result;
    for (const auto &record : records)
        if (IsManagedService(record))
            result.services.push_back(record);
    std::sort(result.services.begin(), result.services.end(),
              [](const ServiceInfo &a, const ServiceInfo &b) { return a.name < b.name; });
    return result;
}
ServiceInfo &MemoryServices::Find(const ServiceInfo &expected, bool configuration) {
    const auto found = std::find_if(records.begin(), records.end(), [&](const ServiceInfo &item) {
        return item.name == expected.name;
    });
    if (found == records.end())
        throw ServiceError(L"服务已不存在，请刷新列表。", 1060);
    RequireSameService(expected, *found, configuration);
    return *found;
}
ServiceInfo MemoryServices::Read(const ServiceInfo &expected) {
    std::lock_guard<std::mutex> lock(mutex);
    return Find(expected);
}
ServiceInfo MemoryServices::Create(const ServiceConfiguration &configuration) {
    const auto normalized = NormalizeConfiguration(configuration);
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &record : records) {
        if (record.name == normalized.name)
            throw ServiceError(L"同名服务已存在，未覆盖现有服务。", 1073);
    }
    ServiceInfo result;
    static_cast<ServiceConfiguration &>(result) = normalized;
    result.managed_by = L"NssmNext";
    result.schema = MANAGED_SCHEMA_VERSION;
    std::wostringstream id;
    id << L"{00000000-0000-0000-0000-" << std::hex << std::setfill(L'0') << std::setw(12)
       << ++sequence << L"}";
    result.instance_id = id.str();
    result.revision = 1;
    result.binary_path = ServiceHostCommandLine(
        L"C:\\Program Files\\NssmNext\\ServiceHosts\\memory\\NssmNext.ServiceHost.exe");
    result.account = L"LocalSystem";
    records.push_back(result);
    return result;
}
void MemoryServices::Update(const ServiceInfo &expected,
                            const ServiceConfiguration &configuration) {
    const auto normalized = NormalizeConfiguration(configuration);
    if (normalized.name != expected.name)
        throw ServiceError(L"不能在编辑时更改服务名。");
    std::lock_guard<std::mutex> lock(mutex);
    auto &record = Find(expected, true);
    static_cast<ServiceConfiguration &>(record) = normalized;
    ++record.revision;
}
void MemoryServices::Remove(const ServiceInfo &expected) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto identity = Find(expected).instance_id;
    records.erase(
        std::remove_if(records.begin(), records.end(),
                       [&](const ServiceInfo &record) { return record.instance_id == identity; }),
        records.end());
}
void MemoryServices::Start(const ServiceInfo &expected) {
    std::lock_guard<std::mutex> lock(mutex);
    auto &record = Find(expected);
    if (record.startup == StartupMode::Disabled)
        throw ServiceError(L"服务已禁用。", 1058);
    record.state = ServiceState::Running;
    record.process_id = static_cast<std::uint32_t>(1000 + sequence);
}
void MemoryServices::Stop(const ServiceInfo &expected) {
    std::lock_guard<std::mutex> lock(mutex);
    auto &record = Find(expected);
    record.state = ServiceState::Stopped;
    record.process_id = 0;
}
void MemoryServices::Inject(const ServiceInfo &record) {
    std::lock_guard<std::mutex> lock(mutex);
    records.push_back(record);
}
std::shared_ptr<IServiceManager> CreateMemoryServiceManager(const std::wstring &application) {
    return std::make_shared<MemoryServices>(application);
}
} // namespace nssm_next::testing
