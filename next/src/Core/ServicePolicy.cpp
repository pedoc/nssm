#include "ServicePolicy.h"
#include <algorithm>
#include <cwctype>

namespace {
constexpr std::size_t MAX_NAME = 256;
constexpr std::size_t MAX_PATH_TEXT = 32766;
constexpr std::size_t MAX_DIRECTORY = 32755;
constexpr std::size_t MAX_COMMAND = 32766;
constexpr std::size_t MAX_DESCRIPTION = 4096;

bool HasNull(const std::wstring &text) {
    return text.find(L'\0') != std::wstring::npos;
}
bool LocalPath(const std::wstring &path) {
    if (path.size() < 3 ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path[1] != L':' || (path[2] != L'\\' && path[2] != L'/'))
        return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const auto ch = path[i];
        if (ch < 32 || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L'?' ||
            ch == L'*' || (ch == L':' && i != 1))
            return false;
    }
    return true;
}
bool ValidGuid(const std::wstring &value) {
    const bool braces = value.size() == 38 && value.front() == L'{' && value.back() == L'}';
    if (value.size() != 36 && !braces)
        return false;
    const auto start = braces ? 1u : 0u;
    for (std::size_t i = 0; i < 36; ++i) {
        const auto ch = value[start + i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (ch != L'-')
                return false;
        } else if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') ||
                     (ch >= L'A' && ch <= L'F')))
            return false;
    }
    return true;
}
bool HostPath(const std::wstring &path) {
    if (!LocalPath(path))
        return false;
    const auto slash = path.find_last_of(L"\\/");
    auto filename = path.substr(slash + 1);
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return filename == L"nssmnext.servicehost.exe";
}
bool HostCommand(const std::wstring &command) {
    if (command.empty() || command.front() != L'"')
        return false;
    const auto quote = command.find(L'"', 1);
    return quote != std::wstring::npos && command.substr(quote + 1) == L" --service" &&
           HostPath(command.substr(1, quote - 1));
}
} // namespace

namespace nssm_next {
void ValidateServiceName(const std::wstring &name) {
    if (name.empty() || name.size() > MAX_NAME || name.front() == L' ' || name.back() == L' ')
        throw ServiceError(L"服务名必须为 1–256 个字符，且首尾不能有空格。");
    for (const auto ch : name) {
        if (ch < 32 || ch == L'\\' || ch == L'/')
            throw ServiceError(L"服务名不能包含斜杠、反斜杠或控制字符。");
    }
}
ServiceConfiguration NormalizeConfiguration(const ServiceConfiguration &input) {
    auto result = input;
    ValidateServiceName(result.name);
    if (result.display_name.empty())
        result.display_name = result.name;
    if (result.display_name.size() > MAX_NAME || HasNull(result.display_name))
        throw ServiceError(L"显示名称不能超过 256 个字符或包含空字符。");
    if (!LocalPath(result.application) || result.application.size() > MAX_PATH_TEXT)
        throw ServiceError(L"请填写不带引号的本地可执行文件完整路径。");
    if (HasNull(result.arguments) ||
        result.application.size() + result.arguments.size() + 3 > MAX_COMMAND)
        throw ServiceError(L"参数包含空字符，或完整命令行超过 Windows 长度限制。");
    if (result.working_directory.empty()) {
        const auto slash = result.application.find_last_of(L"\\/");
        result.working_directory = result.application.substr(0, slash == 2 ? 3 : slash);
    }
    if (!LocalPath(result.working_directory) || result.working_directory.size() > MAX_DIRECTORY)
        throw ServiceError(L"工作目录必须为有效的本地完整目录路径。");
    if (result.description.size() > MAX_DESCRIPTION || HasNull(result.description))
        throw ServiceError(L"描述不能超过 4096 个字符或包含空字符。");
    if (static_cast<std::uint32_t>(result.startup) >
        static_cast<std::uint32_t>(StartupMode::Disabled))
        throw ServiceError(L"无效的服务启动类型。");
    return result;
}
bool SameConfiguration(const ServiceConfiguration &a, const ServiceConfiguration &b) {
    return a.name == b.name && a.display_name == b.display_name && a.application == b.application &&
           a.arguments == b.arguments && a.working_directory == b.working_directory &&
           a.description == b.description && a.startup == b.startup;
}
bool IsManagedService(const ServiceInfo &service) noexcept {
    try {
        return service.managed_by == L"NssmNext" && service.schema == MANAGED_SCHEMA_VERSION &&
               ValidGuid(service.instance_id) && service.revision > 0 &&
               HostCommand(service.binary_path);
    } catch (...) {
        return false;
    }
}
void RequireSameService(const ServiceInfo &expected, const ServiceInfo &actual,
                        bool configuration) {
    if (!IsManagedService(expected) || !IsManagedService(actual))
        throw ServiceError(L"服务不属于 NssmNext；已拒绝修改或控制。", SERVICE_NOT_OWNED);
    if (expected.name != actual.name || expected.instance_id != actual.instance_id ||
        expected.binary_path != actual.binary_path)
        throw ServiceError(L"服务归属或宿主路径已改变，请刷新后重试。", STALE_SERVICE);
    if (configuration &&
        (expected.revision != actual.revision || !SameConfiguration(expected, actual)))
        throw ServiceError(L"配置已被其他进程修改，请刷新后重新编辑。", STALE_SERVICE);
}
std::wstring ServiceHostCommandLine(const std::wstring &executable) {
    if (!HostPath(executable) || executable.size() + 13 > MAX_COMMAND)
        throw ServiceError(L"无效的 NssmNext 原生服务宿主路径。");
    return L"\"" + executable + L"\" --service";
}
} // namespace nssm_next
