#include "NativeConfigurationStore.h"
#include "NativeServiceAccounts.h"
#include "ServiceConfigurationCodec.h"
#include <algorithm>

namespace nssm_next { namespace detail {
namespace {
constexpr DWORD MAX_CONFIGURATION_BYTES = 4 * 1024 * 1024;
constexpr DWORD MAX_CONFIGURATION_VALUES = 4096;
RawConfigurationValue CaptureValue(HKEY key, const std::wstring &path, const std::wstring &name) {
    RawConfigurationValue value; value.path = path; value.name = name;
    DWORD bytes = 0, type = 0;
    auto error = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes);
    if (error == ERROR_FILE_NOT_FOUND) return value;
    CheckRegistry(error, L"无法读取完整配置快照");
    if (bytes > MAX_CONFIGURATION_BYTES) Fail(L"配置项超过安全读取上限", ERROR_INVALID_DATA);
    value.present = true; value.bytes.resize(bytes);
    CheckRegistry(RegQueryValueExW(key, name.c_str(), nullptr, &type, value.bytes.data(), &bytes), L"无法读取完整配置快照");
    value.type = type; value.bytes.resize(bytes); return value;
}
void ReadTree(HKEY key, const std::wstring &path, std::vector<RawConfigurationValue> &values, DWORD &budget, unsigned depth) {
    if (depth > 8) Fail(L"配置子键层级过深", ERROR_INVALID_DATA);
    DWORD count = 0, nameSize = 0, subCount = 0, subSize = 0;
    CheckRegistry(RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subCount, &subSize, nullptr, &count, &nameSize, nullptr, nullptr, nullptr), L"无法读取配置目录");
    if (count > MAX_CONFIGURATION_VALUES || subCount > MAX_CONFIGURATION_VALUES || values.size() + count > MAX_CONFIGURATION_VALUES)
        Fail(L"配置项过多", ERROR_INVALID_DATA);
    std::vector<wchar_t> name(std::max(nameSize, subSize) + 2);
    for (DWORD i = 0; i < count; ++i) {
        DWORD length = static_cast<DWORD>(name.size());
        CheckRegistry(RegEnumValueW(key, i, name.data(), &length, nullptr, nullptr, nullptr, nullptr), L"无法枚举配置项");
        auto value = CaptureValue(key, path, std::wstring(name.data(), length));
        if (value.bytes.size() > MAX_CONFIGURATION_BYTES - budget) Fail(L"配置快照超过安全上限", ERROR_INVALID_DATA);
        budget += static_cast<DWORD>(value.bytes.size()); values.push_back(std::move(value));
    }
    for (DWORD i = 0; i < subCount; ++i) {
        DWORD length = static_cast<DWORD>(name.size());
        CheckRegistry(RegEnumKeyExW(key, i, name.data(), &length, nullptr, nullptr, nullptr, nullptr), L"无法枚举配置子键");
        HKEY raw = nullptr; CheckRegistry(RegOpenKeyExW(key, name.data(), 0, KEY_READ, &raw), L"无法读取配置子键");
        RegHandle child(raw, &RegCloseKey);
        ReadTree(child.get(), path.empty() ? std::wstring(name.data(), length) : path + L"\\" + std::wstring(name.data(), length), values, budget, depth + 1);
    }
}
std::vector<wchar_t> MultiStrings(const std::vector<std::wstring> &items) {
    std::vector<wchar_t> result;
    for (const auto &item : items) { result.insert(result.end(), item.begin(), item.end()); result.push_back(0); }
    if (items.empty()) result.push_back(0); result.push_back(0); return result;
}
RawConfigurationValue Basic(const wchar_t *name, const std::wstring &text) {
    RawConfigurationValue result; result.name = name; result.present = !text.empty(); result.type = REG_EXPAND_SZ;
    for (const auto ch : text) { result.bytes.push_back(static_cast<BYTE>(ch)); result.bytes.push_back(static_cast<BYTE>(ch >> 8)); }
    result.bytes.push_back(0); result.bytes.push_back(0); return result;
}
std::vector<RawConfigurationValue> KnownValues(const ServiceConfiguration &configuration) {
    auto values = EncodeAdvancedConfiguration(configuration);
    values.push_back(Basic(L"Application", configuration.application));
    values.push_back(Basic(L"AppParameters", configuration.arguments));
    values.push_back(Basic(L"AppDirectory", configuration.working_directory));
    return values;
}
bool SameValue(const RawConfigurationValue &a, const RawConfigurationValue &b) {
    return a.present == b.present && (!a.present || (a.type == b.type && a.bytes == b.bytes));
}
}
std::vector<RawConfigurationValue> ReadConfigurationValues(const std::wstring &name, HKEY parameters) {
    std::vector<RawConfigurationValue> result; DWORD budget = 0;
    ReadTree(parameters, L"", result, budget, 0);
    // Revision is compared separately. Exclude it from deep equality so callers can
    // refresh the same identity after a successful write without copying secrets.
    result.erase(std::remove_if(result.begin(), result.end(), [](const RawConfigurationValue &v) {
        return v.path.empty() && v.name == L"NssmNextRevision";
    }), result.end());
    HKEY raw = nullptr;
    const auto root = L"SYSTEM\\CurrentControlSet\\Services\\" + name;
    CheckRegistry(RegOpenKeyExW(HKEY_LOCAL_MACHINE, root.c_str(), 0, KEY_READ, &raw), L"无法读取服务级配置");
    RegHandle key(raw, &RegCloseKey);
    auto environment = CaptureValue(key.get(), L"", L"Environment"); environment.service_root = true;
    result.push_back(std::move(environment));
    std::sort(result.begin(), result.end(), [](const RawConfigurationValue &a, const RawConfigurationValue &b) {
        if (a.service_root != b.service_root) return a.service_root < b.service_root;
        if (a.path != b.path) return a.path < b.path;
        return a.name < b.name;
    });
    return result;
}
void ReadAdvancedConfiguration(SC_HANDLE service, HKEY parameters, ServiceInfo &record) {
    DWORD required = 0; QueryServiceConfigW(service, nullptr, 0, &required);
    if (!required || required > MAX_CONFIGURATION_BYTES) Fail(L"服务配置长度无效", ERROR_INVALID_DATA);
    std::vector<BYTE> bytes(required);
    auto config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(bytes.data());
    if (!QueryServiceConfigW(service, config, required, &required)) Fail(L"无法读取服务依赖");
    if (config->lpDependencies) {
        for (const auto *item = config->lpDependencies; *item; item += lstrlenW(item) + 1) record.dependencies.emplace_back(item);
    }
    record.login = ReadLoginConfiguration(record.account, record.name);
    record.raw_configuration = ReadConfigurationValues(record.name, parameters);
    DecodeAdvancedConfiguration(record, record.raw_configuration);
}
void PreparePrivateEventSource(const std::wstring &host) {
    HKEY raw = nullptr;
    CheckRegistry(RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\NssmNext", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &raw, nullptr), L"无法准备 NssmNext 私有日志源");
    RegHandle key(raw, &RegCloseKey);
    WriteString(key.get(), L"EventMessageFile", host);
    WriteDword(key.get(), L"TypesSupported", EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE);
}
ConfigurationJournal::ConfigurationJournal(HKEY key) : parameters(key) {}
void ConfigurationJournal::Write(const RawConfigurationValue &value, bool record) {
    if (value.service_root) throw ServiceError(L"参数事务不能写服务级环境。", ERROR_INVALID_PARAMETER);
    RegHandle target(nullptr, &RegCloseKey);
    HKEY destination = parameters;
    if (!value.path.empty()) {
        std::wstring prefix; std::size_t start = 0;
        while (start < value.path.size()) {
            const auto end = value.path.find(L'\\', start);
            prefix = value.path.substr(0, end);
            HKEY raw = nullptr;
            auto error = RegOpenKeyExW(parameters, prefix.c_str(), 0, KEY_READ | KEY_WRITE, &raw);
            if ((error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) && !value.present) return;
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                DWORD disposition = 0;
                error = RegCreateKeyExW(parameters, prefix.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &raw, &disposition);
                if (error == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY && record) created_keys.push_back(prefix);
            }
            CheckRegistry(error, L"无法打开高级配置子键"); target.reset(raw); destination = target.get();
            if (end == std::wstring::npos) break; start = end + 1;
        }
    }
    if (record) touched.push_back(CaptureValue(destination, value.path, value.name));
    auto error = value.present
        ? RegSetValueExW(destination, value.name.c_str(), 0, value.type, value.bytes.data(), static_cast<DWORD>(value.bytes.size()))
        : RegDeleteValueW(destination, value.name.c_str());
    if (!value.present && error == ERROR_FILE_NOT_FOUND) return;
    CheckRegistry(error, L"无法保存高级服务配置");
}
void ConfigurationJournal::Apply(const ServiceConfiguration *before, const ServiceConfiguration &after) {
    const auto next = KnownValues(after);
    const auto previous = before ? KnownValues(*before) : std::vector<RawConfigurationValue>{};
    for (const auto &value : next) {
        bool unchanged = false;
        for (const auto &old : previous) if (old.path == value.path && old.name == value.name) { unchanged = SameValue(old, value); break; }
        if (!unchanged) Write(value, true);
    }
}
bool ConfigurationJournal::Rollback() noexcept {
    bool restored = true;
    for (auto i = touched.rbegin(); i != touched.rend(); ++i) {
        try { Write(*i, false); } catch (...) { restored = false; }
    }
    for (auto i = created_keys.rbegin(); i != created_keys.rend(); ++i) {
        HKEY raw = nullptr;
        auto error = RegOpenKeyExW(parameters, i->c_str(), 0, KEY_READ, &raw);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
        if (error != ERROR_SUCCESS) { restored = false; continue; }
        RegHandle key(raw, &RegCloseKey); DWORD subkeys = 0, values = 0;
        error = RegQueryInfoKeyW(key.get(), nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
        key.reset();
        if (error != ERROR_SUCCESS) { restored = false; continue; }
        if (!subkeys && !values && RegDeleteKeyW(parameters, i->c_str()) != ERROR_SUCCESS) restored = false;
    }
    return restored;
}
} }
