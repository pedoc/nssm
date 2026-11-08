#pragma once
#include "../../src/Core/ServiceModel.h"
#include <mutex>

namespace nssm_next::testing {
class MemoryServices final : public IServiceManager {
  public:
    explicit MemoryServices(std::wstring application, bool seed = true);
    ServiceSnapshot Read() override;
    ServiceInfo Read(const ServiceInfo &expected) override;
    ServiceInfo Create(const ServiceConfiguration &configuration) override;
    void Update(const ServiceInfo &expected, const ServiceConfiguration &configuration) override;
    void Remove(const ServiceInfo &expected) override;
    void Start(const ServiceInfo &expected) override;
    void Stop(const ServiceInfo &expected) override;
    bool CanWrite() const noexcept override {
        return true;
    }
    bool IsTestBackend() const noexcept override {
        return true;
    }
    void Inject(const ServiceInfo &record);

  private:
    std::mutex mutex;
    std::vector<ServiceInfo> records;
    std::uint64_t sequence = 0;
    ServiceInfo &Find(const ServiceInfo &expected, bool configuration = false);
};
std::shared_ptr<IServiceManager> CreateMemoryServiceManager(const std::wstring &application);
} // namespace nssm_next::testing
