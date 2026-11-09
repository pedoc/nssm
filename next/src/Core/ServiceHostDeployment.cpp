#include "NativeServicesInternal.h"
#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <array>
#include <iomanip>
#include <sstream>

namespace nssm_next {
namespace detail {
namespace {
constexpr DWORD MAX_HOST_BYTES = 64 * 1024 * 1024;
constexpr wchar_t HOST_NAME[] = L"NssmNext.ServiceHost.exe";
constexpr wchar_t HOST_SECURITY[] =
    L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;AU)";
using FileHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
using LocalMemory = std::unique_ptr<void, decltype(&LocalFree)>;

FileHandle OpenFile(const std::wstring &path, DWORD access, DWORD disposition,
                    SECURITY_ATTRIBUTES *security = nullptr, DWORD flags = FILE_ATTRIBUTE_NORMAL) {
    const auto raw =
        CreateFileW(path.c_str(), access, FILE_SHARE_READ, security, disposition, flags, nullptr);
    if (raw == INVALID_HANDLE_VALUE)
        Fail(L"无法访问服务宿主文件或目录");
    return FileHandle(raw, &CloseHandle);
}
std::vector<BYTE> ReadFileBytes(HANDLE file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size))
        Fail(L"无法读取宿主文件大小");
    if (size.QuadPart <= 0 || size.QuadPart > MAX_HOST_BYTES)
        Fail(L"服务宿主文件大小无效", ERROR_INVALID_DATA);
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
        Fail(L"无法读取宿主文件");
    std::vector<BYTE> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD offset = 0;
    while (offset < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &read,
                      nullptr))
            Fail(L"无法读取宿主文件");
        if (!read)
            Fail(L"服务宿主文件读取不完整", ERROR_HANDLE_EOF);
        offset += read;
    }
    return bytes;
}
std::wstring Digest(const std::vector<BYTE> &bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    const auto opened =
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (opened < 0)
        Fail(L"无法初始化宿主校验", ERROR_INVALID_DATA);
    std::array<BYTE, 32> hash{};
    const auto result =
        BCryptHash(algorithm, nullptr, 0, const_cast<BYTE *>(bytes.data()),
                   static_cast<ULONG>(bytes.size()), hash.data(), static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0)
        Fail(L"无法校验服务宿主", ERROR_INVALID_DATA);
    std::wostringstream output;
    for (const auto byte : hash)
        output << std::hex << std::setw(2) << std::setfill(L'0') << static_cast<unsigned>(byte);
    return output.str();
}
bool EqualSidType(PSID sid, WELL_KNOWN_SID_TYPE kind) {
    std::array<BYTE, SECURITY_MAX_SID_SIZE> expected{};
    DWORD size = static_cast<DWORD>(expected.size());
    return CreateWellKnownSid(kind, nullptr, expected.data(), &size) &&
           EqualSid(sid, expected.data());
}
void VerifyProtected(HANDLE file, const std::wstring &path) {
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file, &information))
        Fail(L"无法核对宿主存储目录");
    if (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        Fail(L"宿主存储路径包含重解析点，已拒绝部署", ERROR_ACCESS_DENIED);
    PSECURITY_DESCRIPTOR raw = nullptr;
    PSID owner = nullptr;
    PACL acl = nullptr;
    const auto error = GetSecurityInfo(file, SE_FILE_OBJECT,
                                       OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                       &owner, nullptr, &acl, nullptr, &raw);
    CheckRegistry(static_cast<LSTATUS>(error), L"无法核对宿主存储权限");
    LocalMemory descriptor(raw, &LocalFree);
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    const auto reject = [&path](const wchar_t *reason) {
        Fail(L"宿主权限检查失败：" + path + L"\n" + reason, ERROR_ACCESS_DENIED);
    };
    if (!owner || !EqualSidType(owner, WinBuiltinAdministratorsSid))
        reject(L"所有者不是 Administrators，已拒绝部署；未自动修改已有目录的所有权。");
    if (!acl || !IsValidAcl(acl))
        reject(L"缺少有效的访问控制列表，已拒绝部署。");
    if (!GetSecurityDescriptorControl(raw, &control, &revision) || !(control & SE_DACL_PROTECTED))
        reject(L"访问控制列表未关闭继承，已拒绝部署。");
    bool system = false, administrators = false, users = false;
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE,
                            FILE_ALL_ACCESS};
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *rawAce = nullptr;
        if (!GetAce(acl, index, &rawAce))
            Fail(L"无法核对宿主访问规则");
        const auto ace = static_cast<ACCESS_ALLOWED_ACE *>(rawAce);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
            Fail(L"宿主访问规则不符合安全要求", ERROR_ACCESS_DENIED);
        auto mask = ace->Mask;
        MapGenericMask(&mask, &mapping);
        const auto sid = reinterpret_cast<PSID>(&ace->SidStart);
        // Windows may split one OI/CI generic-rights ACE into an inherit-only
        // ACE and a mapped, effective ACE for this directory. Check each grant's
        // meaning, not the resulting ACE count. Inherit-only entries must still
        // be safe, but do not prove that the current object is accessible.
        const bool effective = !(ace->Header.AceFlags & INHERIT_ONLY_ACE);
        if (EqualSidType(sid, WinLocalSystemSid) && mask == FILE_ALL_ACCESS)
            system = system || effective;
        else if (EqualSidType(sid, WinBuiltinAdministratorsSid) && mask == FILE_ALL_ACCESS)
            administrators = administrators || effective;
        else if ((EqualSidType(sid, WinBuiltinUsersSid) || EqualSidType(sid, WinAuthenticatedUserSid)) &&
                 mask == (FILE_GENERIC_READ | FILE_GENERIC_EXECUTE))
            users = users || effective;
        else
            reject(L"访问规则包含非预期的身份或写入权限，已拒绝部署。");
    }
    if (!system || !administrators || !users)
        reject(L"缺少 SYSTEM、Administrators 或服务运行身份所需的有效访问权限。");
}
void UpgradeProtectedAccess(HANDLE target, PSECURITY_DESCRIPTOR desired, const std::wstring &path) {
    VerifyProtected(target, path);
    PACL acl = nullptr; BOOL present = FALSE, defaulted = FALSE;
    if (!GetSecurityDescriptorDacl(desired, &present, &acl, &defaulted) || !present || !acl) Fail(L"宿主访问规则无效", ERROR_INVALID_DATA);
    CheckRegistry(static_cast<LSTATUS>(SetSecurityInfo(target, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr)), L"无法为服务身份准备宿主读取权限");
    VerifyProtected(target, path);
}
FileHandle ProtectedDirectory(const std::wstring &path, SECURITY_ATTRIBUTES *security) {
    if (!CreateDirectoryW(path.c_str(), security) && GetLastError() != ERROR_ALREADY_EXISTS)
        Fail(L"无法创建受保护的服务宿主目录");
    auto directory = OpenFile(path, READ_CONTROL | FILE_READ_ATTRIBUTES | WRITE_DAC, OPEN_EXISTING, nullptr,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
    UpgradeProtectedAccess(directory.get(), security->lpSecurityDescriptor, path);
    return directory;
}
} // namespace
std::wstring DeployServiceHost(const std::wstring &source) {
    auto input = OpenFile(source, GENERIC_READ, OPEN_EXISTING);
    const auto bytes = ReadFileBytes(input.get());
    if (bytes.size() < 2 || bytes[0] != 'M' || bytes[1] != 'Z')
        Fail(L"服务宿主不是有效的可执行文件，请重新构建完整程序", ERROR_BAD_EXE_FORMAT);
    const auto hash = Digest(bytes);
    PWSTR folder = nullptr;
    const auto result =
        SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &folder);
    if (FAILED(result))
        Fail(L"无法定位受保护的程序目录", static_cast<DWORD>(result));
    const std::wstring base(folder);
    CoTaskMemFree(folder);
    // Hold each directory without FILE_SHARE_DELETE while resolving its children.
    auto baseHandle = OpenFile(base, READ_CONTROL | FILE_READ_ATTRIBUTES, OPEN_EXISTING, nullptr,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
    BY_HANDLE_FILE_INFORMATION baseInfo{};
    if (!GetFileInformationByHandle(baseHandle.get(), &baseInfo) ||
        (baseInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        Fail(L"程序目录不是可用的本地受保护目录", ERROR_ACCESS_DENIED);
    PSECURITY_DESCRIPTOR raw = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(HOST_SECURITY, SDDL_REVISION_1, &raw,
                                                              nullptr))
        Fail(L"无法创建宿主安全描述符");
    LocalMemory descriptor(raw, &LocalFree);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), raw, FALSE};
    const auto root = base + L"\\NssmNext";
    auto rootHandle = ProtectedDirectory(root, &security);
    const auto hosts = root + L"\\ServiceHosts";
    auto hostsHandle = ProtectedDirectory(hosts, &security);
    const auto version = hosts + L"\\" + hash;
    auto versionHandle = ProtectedDirectory(version, &security);
    const auto target = version + L"\\" + HOST_NAME;
    const auto rawOutput =
        CreateFileW(target.c_str(), GENERIC_WRITE | READ_CONTROL | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (rawOutput == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            Fail(L"无法部署原生服务宿主", error);
        auto existing = OpenFile(target, GENERIC_READ | READ_CONTROL | WRITE_DAC, OPEN_EXISTING, nullptr,
                                 FILE_FLAG_OPEN_REPARSE_POINT);
        UpgradeProtectedAccess(existing.get(), raw, target);
        if (Digest(ReadFileBytes(existing.get())) != hash)
            Fail(L"已有宿主内容与校验不符，未覆盖任何文件", ERROR_INVALID_DATA);
        return target;
    }
    FileHandle output(rawOutput, &CloseHandle);
    try {
        VerifyProtected(output.get(), target);
        DWORD offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            if (!WriteFile(output.get(), bytes.data() + offset,
                           static_cast<DWORD>(bytes.size() - offset), &written, nullptr) ||
                !written)
                Fail(L"无法写入服务宿主文件");
            offset += written;
        }
        if (!FlushFileBuffers(output.get()))
            Fail(L"无法完成服务宿主部署");
    } catch (...) {
        output.reset();
        if (!DeleteFileW(target.c_str()))
            throw ServiceError(L"宿主部署失败，且未完成文件的清理失败：" + target);
        throw;
    }
    return target;
}
void EnsureServiceHostAccess(const std::wstring &host) {
    PWSTR folder = nullptr;
    const auto status = SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &folder);
    if (FAILED(status)) Fail(L"无法定位宿主目录", static_cast<DWORD>(status));
    const std::wstring base(folder); CoTaskMemFree(folder);
    const auto root = base + L"\\NssmNext";
    const auto hosts = root + L"\\ServiceHosts";
    const auto prefix = hosts + L"\\";
    if (host.size() <= prefix.size() || CompareStringOrdinal(host.c_str(), static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()), TRUE) != CSTR_EQUAL)
        Fail(L"宿主路径不属于本程序的受保护存储，拒绝调整权限", ERROR_ACCESS_DENIED);
    const auto remaining = host.substr(prefix.size());
    const auto slash = remaining.find(L'\\');
    if (slash != 64 || remaining.substr(slash + 1) != HOST_NAME || remaining.substr(0, slash).find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
        Fail(L"宿主版本路径无效，拒绝调整权限", ERROR_INVALID_DATA);
    PSECURITY_DESCRIPTOR raw = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(HOST_SECURITY, SDDL_REVISION_1, &raw, nullptr)) Fail(L"无法准备宿主读取权限");
    LocalMemory descriptor(raw, &LocalFree); SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), raw, FALSE};
    auto baseHandle = OpenFile(base, READ_CONTROL | FILE_READ_ATTRIBUTES, OPEN_EXISTING, nullptr, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
    BY_HANDLE_FILE_INFORMATION baseInfo{};
    if (!GetFileInformationByHandle(baseHandle.get(), &baseInfo) || (baseInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) Fail(L"程序目录不可用于安全宿主存储", ERROR_ACCESS_DENIED);
    auto rootHandle = ProtectedDirectory(root, &security);
    auto hostsHandle = ProtectedDirectory(hosts, &security);
    auto versionHandle = ProtectedDirectory(hosts + L"\\" + remaining.substr(0, slash), &security);
    auto file = OpenFile(host, GENERIC_READ | READ_CONTROL | WRITE_DAC, OPEN_EXISTING, nullptr, FILE_FLAG_OPEN_REPARSE_POINT);
    UpgradeProtectedAccess(file.get(), raw, host);
    if (Digest(ReadFileBytes(file.get())) != remaining.substr(0, slash)) Fail(L"宿主内容与版本校验不符", ERROR_INVALID_DATA);
}
} // namespace detail
} // namespace nssm_next
