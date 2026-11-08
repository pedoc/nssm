#include "NativeServicesInternal.h"
#include <aclapi.h>
#include <sddl.h>

namespace nssm_next {
namespace detail {
namespace {
constexpr DWORD MUTATION_WAIT_MS = 5000;
void VerifyMutexSecurity(HANDLE mutex) {
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto result = GetSecurityInfo(mutex, SE_KERNEL_OBJECT,
                                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                        &owner, nullptr, &acl, nullptr, &raw);
    CheckRegistry(static_cast<LSTATUS>(result), L"无法核对管理锁权限");
    std::unique_ptr<void, decltype(&LocalFree)> descriptor(raw, &LocalFree);
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    if (!owner ||
        !(IsWellKnownSid(owner, WinBuiltinAdministratorsSid) ||
          IsWellKnownSid(owner, WinLocalSystemSid)) ||
        !acl || acl->AceCount != 2 || !GetSecurityDescriptorControl(raw, &control, &revision) ||
        !(control & SE_DACL_PROTECTED))
        Fail(L"已有管理锁的归属或权限不安全，已拒绝操作", ERROR_ACCESS_DENIED);
    bool system = false, administrators = false;
    GENERIC_MAPPING mapping{STANDARD_RIGHTS_READ | SYNCHRONIZE,
                            STANDARD_RIGHTS_WRITE | MUTEX_MODIFY_STATE,
                            STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE, MUTEX_ALL_ACCESS};
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void *value = nullptr;
        if (!GetAce(acl, i, &value))
            Fail(L"无法读取管理锁权限");
        const auto ace = static_cast<ACCESS_ALLOWED_ACE *>(value);
        auto mask = ace->Mask;
        MapGenericMask(&mask, &mapping);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || mask != MUTEX_ALL_ACCESS)
            Fail(L"已有管理锁允许了非预期权限", ERROR_ACCESS_DENIED);
        auto sid = reinterpret_cast<PSID>(&ace->SidStart);
        if (IsWellKnownSid(sid, WinLocalSystemSid) && !system)
            system = true;
        else if (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) && !administrators)
            administrators = true;
        else
            Fail(L"已有管理锁不属于 NssmNext 管理范围", ERROR_ACCESS_DENIED);
    }
    if (!system || !administrators)
        Fail(L"管理锁权限不完整", ERROR_ACCESS_DENIED);
}
} // namespace
MutationGuard::MutationGuard() {
    PSECURITY_DESCRIPTOR raw = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &raw, nullptr))
        Fail(L"无法初始化服务管理锁");
    std::unique_ptr<void, decltype(&LocalFree)> descriptor(raw, &LocalFree);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), raw, FALSE};
    handle.reset(CreateMutexExW(&security, L"Global\\NssmNext.ServiceManagement.v1", 0,
                                SYNCHRONIZE | MUTEX_MODIFY_STATE | READ_CONTROL));
    if (!handle)
        Fail(L"无法取得服务管理锁，请使用管理员权限");
    VerifyMutexSecurity(handle.get());
    const auto wait = WaitForSingleObject(handle.get(), MUTATION_WAIT_MS);
    if (wait == WAIT_OBJECT_0) {
        locked = true;
        return;
    }
    if (wait == WAIT_ABANDONED) {
        ReleaseMutex(handle.get());
        throw ServiceError(L"上次管理进程异常退出；请刷新确认服务配置后重试。",
                           ERROR_ABANDONED_WAIT_0);
    }
    if (wait == WAIT_TIMEOUT)
        throw ServiceError(L"另一个 NssmNext 窗口正在操作服务，请稍后重试。", ERROR_BUSY);
    Fail(L"等待服务管理锁失败");
}
MutationGuard::~MutationGuard() {
    if (locked)
        ReleaseMutex(handle.get());
}
} // namespace detail
} // namespace nssm_next
