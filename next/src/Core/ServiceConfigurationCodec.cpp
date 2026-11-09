#include "ServiceConfigurationCodec.h"
#include <algorithm>
#include <cwctype>
#include <limits>

namespace nssm_next {
namespace {
constexpr std::uint32_t SZ = 1, EXPAND = 2, DWORD_VALUE = 4, MULTI = 7;
std::wstring Trim(const std::wstring &text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L"" : text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}
bool HasNull(const std::wstring &text) { return text.find(L'\0') != std::wstring::npos; }
RawConfigurationValue Number(const wchar_t *name, std::uint32_t number, std::uint32_t fallback) {
    RawConfigurationValue value; value.name = name; value.type = DWORD_VALUE; value.present = number != fallback;
    for (unsigned i = 0; i < 4; ++i) value.bytes.push_back(static_cast<std::uint8_t>(number >> (8 * i)));
    return value;
}
RawConfigurationValue String(const wchar_t *name, const std::wstring &text, std::uint32_t kind = EXPAND,
    const std::wstring &path = L"", bool present = true) {
    RawConfigurationValue value; value.path = path; value.name = name; value.type = kind;
    value.present = present && !text.empty();
    for (const auto ch : text) { value.bytes.push_back(static_cast<std::uint8_t>(ch)); value.bytes.push_back(static_cast<std::uint8_t>(ch >> 8)); }
    value.bytes.push_back(0); value.bytes.push_back(0); return value;
}
RawConfigurationValue Multi(const wchar_t *name, const std::vector<std::wstring> &lines, bool present) {
    RawConfigurationValue value; value.name = name; value.type = MULTI; value.present = present;
    for (const auto &line : lines) {
        for (const auto ch : line) { value.bytes.push_back(static_cast<std::uint8_t>(ch)); value.bytes.push_back(static_cast<std::uint8_t>(ch >> 8)); }
        value.bytes.push_back(0); value.bytes.push_back(0);
    }
    if (lines.empty()) { value.bytes.push_back(0); value.bytes.push_back(0); }
    value.bytes.push_back(0); value.bytes.push_back(0); return value;
}
const RawConfigurationValue *Find(const std::vector<RawConfigurationValue> &values, const wchar_t *name, const std::wstring &path = L"") {
    for (const auto &value : values) if (value.path == path && value.name == name && value.present) return &value;
    return nullptr;
}
void Diagnostic(ServiceInfo &config, const wchar_t *page, const wchar_t *field) {
    config.diagnostics.push_back({page, field, L"已有值的类型或内容无法识别；未修改此设置时会保留原值。"});
}
std::uint32_t ReadNumber(ServiceInfo &config, const std::vector<RawConfigurationValue> &values,
    const wchar_t *name, std::uint32_t fallback, const wchar_t *page) {
    const auto value = Find(values, name); if (!value) return fallback;
    if (value->type != DWORD_VALUE || value->bytes.size() != 4) { Diagnostic(config, page, name); return fallback; }
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= static_cast<std::uint32_t>(value->bytes[i]) << (i * 8);
    return result;
}
bool DecodeText(const RawConfigurationValue &value, std::wstring &result, bool multi = false) {
    if ((!multi && value.type != SZ && value.type != EXPAND) || (multi && value.type != MULTI) ||
        value.bytes.size() < (multi ? 4u : 2u) || value.bytes.size() % 2 || value.bytes.size() > 65536) return false;
    result.clear();
    for (std::size_t i = 0; i < value.bytes.size(); i += 2)
        result.push_back(static_cast<wchar_t>(value.bytes[i] | (static_cast<unsigned>(value.bytes[i + 1]) << 8)));
    if (result.back() != L'\0' || (multi && result[result.size() - 2] != L'\0')) return false;
    if (!multi) { result.pop_back(); if (HasNull(result)) return false; }
    return true;
}
std::wstring ReadText(ServiceInfo &config, const std::vector<RawConfigurationValue> &values,
    const wchar_t *name, const std::wstring &fallback, const wchar_t *page, const std::wstring &path = L"") {
    const auto value = Find(values, name, path); if (!value) return fallback;
    std::wstring result;
    if (!DecodeText(*value, result)) { Diagnostic(config, page, name); return fallback; }
    return result;
}
void ValidateEnvironment(const std::vector<std::wstring> &lines);
void ReadEnvironment(ServiceInfo &config, const std::vector<RawConfigurationValue> &values,
    const wchar_t *name, bool &present, std::vector<std::wstring> &lines) {
    present = false; lines.clear(); const auto value = Find(values, name); if (!value) return;
    std::wstring text;
    if (!DecodeText(*value, text, true)) { Diagnostic(config, L"Environment", name); return; }
    present = true; std::size_t offset = 0;
    while (offset + 1 < text.size() && text[offset]) {
        const auto end = text.find(L'\0', offset);
        if (end == std::wstring::npos) { Diagnostic(config, L"Environment", name); return; }
        lines.push_back(text.substr(offset, end - offset)); offset = end + 1;
    }
    try { ValidateEnvironment(lines); }
    catch (const ServiceError &) { Diagnostic(config, L"Environment", name); present = false; lines.clear(); }
}
void ValidateEnvironment(const std::vector<std::wstring> &lines) {
    std::size_t size = 1;
    for (const auto &line : lines) {
        const auto equals = line.find(L'=');
        const bool drive = line.size() >= 4 && line[0] == L'=' && line[2] == L':' && line[3] == L'=' && std::iswalpha(line[1]);
        if (line.empty() || HasNull(line) || (equals == 0 && !drive) || equals == std::wstring::npos || line.find_first_of(L"\r\n") != std::wstring::npos)
            throw ServiceError(L"环境变量必须逐行填写 NAME=VALUE；变量名不能为空。空值 NAME= 是允许的。");
        size += line.size() + 1;
    }
    if (size > 32767) throw ServiceError(L"环境变量块超过原生宿主的长度限制。");
}
}
std::uint64_t ParseUnsigned(const std::wstring &input, std::uint64_t maximum, const std::wstring &field) {
    const auto text = Trim(input); if (text.empty()) throw ServiceError(field + L"不能为空。");
    std::uint64_t value = 0;
    for (const auto ch : text) {
        if (ch < L'0' || ch > L'9' || static_cast<std::uint64_t>(ch - L'0') > maximum || value > (maximum - (ch - L'0')) / 10)
            throw ServiceError(field + L"必须是范围内的非负整数。");
        value = value * 10 + (ch - L'0');
    }
    return value;
}
std::vector<std::wstring> ParseConfigurationLines(const std::wstring &text) {
    if (HasNull(text)) throw ServiceError(L"多行设置不能包含空字符。");
    std::vector<std::wstring> result; std::size_t offset = 0;
    while (offset <= text.size()) {
        const auto end = text.find(L'\n', offset);
        auto line = text.substr(offset, end == std::wstring::npos ? end : end - offset);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) result.push_back(std::move(line));
        if (end == std::wstring::npos) break; offset = end + 1;
    }
    return result;
}
std::wstring JoinConfigurationLines(const std::vector<std::wstring> &lines) {
    std::wstring result; for (const auto &line : lines) { if (!result.empty()) result += L"\r\n"; result += line; } return result;
}
std::uint64_t ParseAffinity(const std::wstring &input) {
    const auto text = Trim(input); if (text.empty() || text == L"All" || text == L"all") return 0;
    std::uint64_t mask = 0; std::size_t offset = 0;
    while (offset <= text.size()) {
        const auto end = text.find(L',', offset); const auto part = Trim(text.substr(offset, end == std::wstring::npos ? end : end - offset));
        const auto dash = part.find(L'-');
        const auto first = ParseUnsigned(dash == std::wstring::npos ? part : part.substr(0, dash), 63, L"CPU 编号");
        const auto last = dash == std::wstring::npos ? first : ParseUnsigned(part.substr(dash + 1), 63, L"CPU 编号");
        if (last < first) throw ServiceError(L"CPU 范围的结束编号不能小于开始编号。");
        for (auto cpu = first; cpu <= last; ++cpu) mask |= (std::uint64_t{1} << cpu);
        if (end == std::wstring::npos) break; offset = end + 1;
    }
    return mask;
}
const std::array<std::pair<std::wstring, std::wstring>, 8> &HookPairs() {
    static const std::array<std::pair<std::wstring, std::wstring>, 8> pairs{{
        {L"Start", L"Pre"}, {L"Start", L"Post"}, {L"Stop", L"Pre"}, {L"Exit", L"Post"},
        {L"Power", L"Change"}, {L"Power", L"Resume"}, {L"Rotate", L"Pre"}, {L"Rotate", L"Post"}}};
    return pairs;
}
std::wstring AccountName(const LoginConfiguration &login, const std::wstring &name) {
    switch (login.kind) {
    case AccountKind::LocalSystem: return L"LocalSystem";
    case AccountKind::LocalService: return L"NT AUTHORITY\\LocalService";
    case AccountKind::NetworkService: return L"NT AUTHORITY\\NetworkService";
    case AccountKind::VirtualService: return L"NT SERVICE\\" + name;
    case AccountKind::Custom: return login.username;
    }
    throw ServiceError(L"无效的登录账户类型。");
}
void ValidateAdvancedConfiguration(const ServiceConfiguration &c) {
    if (static_cast<unsigned>(c.login.kind) > static_cast<unsigned>(AccountKind::Custom) ||
        (c.login.kind == AccountKind::Custom && (Trim(c.login.username).empty() || HasNull(c.login.username))))
        throw ServiceError(L"请填写有效的服务登录账户。");
    for (const auto &dependency : c.dependencies)
        if (Trim(dependency).empty() || HasNull(dependency) || dependency.find_first_of(L"\r\n/\\") != std::wstring::npos || dependency == L"+")
            throw ServiceError(L"依赖项必须是服务名或 +组名，每行一项。");
    const std::array<std::uint32_t, 6> priorities{{0x100, 0x80, 0x8000, 0x20, 0x4000, 0x40}};
    if (std::find(priorities.begin(), priorities.end(), c.process.priority) == priorities.end()) throw ServiceError(L"无效的进程优先级。");
    ParseAffinity(c.process.affinity);
    if (static_cast<unsigned>(c.exit.action) > 3) throw ServiceError(L"无效的退出动作。");
    for (const auto *stream : {&c.io.input, &c.io.output, &c.io.error})
        if (HasNull(stream->path) || stream->path.size() > 32766) throw ServiceError(L"I/O 路径包含空字符或过长。");
    ValidateEnvironment(c.environment.replacement); ValidateEnvironment(c.environment.extra);
    if ((c.environment.replacement_present && c.environment.replacement.empty()) ||
        (c.environment.extra_present && c.environment.extra.empty())) throw ServiceError(L"启用的环境变量块至少需要一项变量；关闭选项可使用默认环境。");
    for (const auto &command : c.hooks.commands)
        if (HasNull(command) || command.size() > 32766) throw ServiceError(L"钩子命令包含空字符或过长。");
}
std::vector<RawConfigurationValue> EncodeAdvancedConfiguration(const ServiceConfiguration &c) {
    std::vector<RawConfigurationValue> values;
    const auto number = [&](const wchar_t *name, std::uint32_t value, std::uint32_t fallback = 0) { values.push_back(Number(name, value, fallback)); };
    const auto string = [&](const wchar_t *name, const std::wstring &value, std::uint32_t kind = EXPAND) { values.push_back(String(name, value, kind)); };
    number(L"AppPriority", c.process.priority, 0x20); string(L"AppAffinity", c.process.affinity, SZ);
    number(L"AppNoConsole", c.process.console_window ? 0 : 1);
    const auto skip = c.shutdown.preserved_skip_bits | (c.shutdown.console ? 0u : 1u) | (c.shutdown.window ? 0u : 2u) |
        (c.shutdown.threads ? 0u : 4u) | (c.shutdown.terminate ? 0u : 8u);
    number(L"AppStopMethodSkip", skip); number(L"AppStopMethodConsole", c.shutdown.console_ms, 1500);
    number(L"AppStopMethodWindow", c.shutdown.window_ms, 1500); number(L"AppStopMethodThreads", c.shutdown.threads_ms, 1500);
    number(L"AppKillProcessTree", c.shutdown.kill_tree ? 1 : 0, 1);
    const wchar_t *actions[] = {L"Restart", L"Ignore", L"Exit", L"Suicide"};
    const auto action = static_cast<unsigned>(c.exit.action);
    values.push_back(String(L"", action < 4 ? actions[action] : L"Restart", SZ, L"AppExit", action != 0));
    number(L"AppRestartDelay", c.exit.restart_ms); number(L"AppThrottle", c.exit.throttle_ms, 1500);
    const wchar_t *names[] = {L"AppStdin", L"AppStdout", L"AppStderr"};
    const StreamConfiguration *streams[] = {&c.io.input, &c.io.output, &c.io.error};
    for (unsigned i = 0; i < 3; ++i) {
        string(names[i], streams[i]->path);
        const auto share = std::wstring(names[i]) + L"ShareMode", disposition = std::wstring(names[i]) + L"CreationDisposition";
        const auto flags = std::wstring(names[i]) + L"FlagsAndAttributes", copy = std::wstring(names[i]) + L"CopyAndTruncate";
        number(share.c_str(), streams[i]->share_mode, i ? 3 : 2);
        number(disposition.c_str(), streams[i]->disposition, i ? 4 : 3);
        number(flags.c_str(), streams[i]->flags, 128);
        if (i) number(copy.c_str(), streams[i]->copy_and_truncate ? 1 : 0);
    }
    number(L"AppTimestampLog", c.io.timestamp ? 1 : 0);
    number(L"AppRotateFiles", c.rotation.enabled ? 1 : 0); number(L"AppRotateOnline", c.rotation.online ? 1 : 0);
    number(L"AppRotateSeconds", c.rotation.older_than_seconds); number(L"AppRotateDelay", c.rotation.delay_ms);
    number(L"AppRotateBytes", static_cast<std::uint32_t>(c.rotation.larger_than_bytes));
    number(L"AppRotateBytesHigh", static_cast<std::uint32_t>(c.rotation.larger_than_bytes >> 32));
    values.push_back(Multi(L"AppEnvironment", c.environment.replacement, c.environment.replacement_present));
    values.push_back(Multi(L"AppEnvironmentExtra", c.environment.extra, c.environment.extra_present));
    number(L"AppRedirectHook", c.hooks.redirect_output ? 1 : 0);
    const auto &pairs = HookPairs();
    for (unsigned i = 0; i < pairs.size(); ++i)
        values.push_back(String(pairs[i].second.c_str(), c.hooks.commands[i], EXPAND, L"AppEvents\\" + pairs[i].first));
    return values;
}
void DecodeAdvancedConfiguration(ServiceInfo &c, const std::vector<RawConfigurationValue> &values) {
    c.process = {}; c.shutdown = {}; c.exit = {}; c.io = {}; c.rotation = {}; c.environment = {}; c.hooks = {};
    c.process.priority = ReadNumber(c, values, L"AppPriority", 0x20, L"Process");
    const std::array<std::uint32_t, 6> priorities{{0x100,0x80,0x8000,0x20,0x4000,0x40}};
    if (std::find(priorities.begin(), priorities.end(), c.process.priority) == priorities.end()) { Diagnostic(c,L"Process",L"AppPriority"); c.process.priority = 0x20; }
    c.process.affinity = ReadText(c,values,L"AppAffinity",L"",L"Process");
    try { ParseAffinity(c.process.affinity); } catch (const ServiceError &) { Diagnostic(c,L"Process",L"AppAffinity"); c.process.affinity.clear(); }
    c.process.console_window = !ReadNumber(c,values,L"AppNoConsole",0,L"Process");
    const auto skip = ReadNumber(c,values,L"AppStopMethodSkip",0,L"Shutdown");
    c.shutdown.console = !(skip&1); c.shutdown.window = !(skip&2); c.shutdown.threads = !(skip&4); c.shutdown.terminate = !(skip&8); c.shutdown.preserved_skip_bits = skip & ~15u;
    c.shutdown.console_ms = ReadNumber(c,values,L"AppStopMethodConsole",1500,L"Shutdown");
    c.shutdown.window_ms = ReadNumber(c,values,L"AppStopMethodWindow",1500,L"Shutdown");
    c.shutdown.threads_ms = ReadNumber(c,values,L"AppStopMethodThreads",1500,L"Shutdown");
    c.shutdown.kill_tree = ReadNumber(c,values,L"AppKillProcessTree",1,L"Shutdown") != 0;
    const auto action = ReadText(c,values,L"",L"Restart",L"Exit",L"AppExit");
    const wchar_t *actions[] = {L"Restart",L"Ignore",L"Exit",L"Suicide"};
    bool known = false; for (unsigned i=0;i<4;++i) if(action==actions[i]) { c.exit.action=static_cast<ExitAction>(i);known=true; }
    if(!known) Diagnostic(c,L"Exit",L"AppExit");
    c.exit.restart_ms = ReadNumber(c,values,L"AppRestartDelay",0,L"Exit"); c.exit.throttle_ms=ReadNumber(c,values,L"AppThrottle",1500,L"Exit");
    const wchar_t *names[]={L"AppStdin",L"AppStdout",L"AppStderr"}; StreamConfiguration *streams[]={&c.io.input,&c.io.output,&c.io.error};
    for(unsigned i=0;i<3;++i) {
        streams[i]->path=ReadText(c,values,names[i],L"",L"Io");
        streams[i]->share_mode=ReadNumber(c,values,(std::wstring(names[i])+L"ShareMode").c_str(),i?3:2,L"Io");
        streams[i]->disposition=ReadNumber(c,values,(std::wstring(names[i])+L"CreationDisposition").c_str(),i?4:3,L"Io");
        streams[i]->flags=ReadNumber(c,values,(std::wstring(names[i])+L"FlagsAndAttributes").c_str(),128,L"Io");
        if(i) streams[i]->copy_and_truncate=ReadNumber(c,values,(std::wstring(names[i])+L"CopyAndTruncate").c_str(),0,L"Io")!=0;
    }
    c.io.timestamp=ReadNumber(c,values,L"AppTimestampLog",0,L"Io")!=0;
    c.rotation.enabled=ReadNumber(c,values,L"AppRotateFiles",0,L"Rotation")!=0; c.rotation.online=ReadNumber(c,values,L"AppRotateOnline",0,L"Rotation")!=0;
    c.rotation.older_than_seconds=ReadNumber(c,values,L"AppRotateSeconds",0,L"Rotation"); c.rotation.delay_ms=ReadNumber(c,values,L"AppRotateDelay",0,L"Rotation");
    c.rotation.larger_than_bytes=ReadNumber(c,values,L"AppRotateBytes",0,L"Rotation") | (std::uint64_t{ReadNumber(c,values,L"AppRotateBytesHigh",0,L"Rotation")}<<32);
    ReadEnvironment(c,values,L"AppEnvironment",c.environment.replacement_present,c.environment.replacement);
    ReadEnvironment(c,values,L"AppEnvironmentExtra",c.environment.extra_present,c.environment.extra);
    for(auto *present : {&c.environment.replacement_present,&c.environment.extra_present}) {
        const auto &lines=present==&c.environment.replacement_present?c.environment.replacement:c.environment.extra;
        if(*present && lines.empty()) { Diagnostic(c,L"Environment",L"EmptyBlock"); *present=false; }
    }
    const auto &pairs=HookPairs();
    for(unsigned i=0;i<pairs.size();++i) c.hooks.commands[i]=ReadText(c,values,pairs[i].second.c_str(),L"",L"Hooks",L"AppEvents\\"+pairs[i].first);
    c.hooks.redirect_output=ReadNumber(c,values,L"AppRedirectHook",0,L"Hooks")!=0;
}
bool SameRawConfiguration(const std::vector<RawConfigurationValue> &left, const std::vector<RawConfigurationValue> &right) {
    if(left.size()!=right.size()) return false;
    for(std::size_t i=0;i<left.size();++i) if(left[i].path!=right[i].path ||left[i].name!=right[i].name ||left[i].service_root!=right[i].service_root ||left[i].present!=right[i].present ||left[i].type!=right[i].type ||left[i].bytes!=right[i].bytes) return false;
    return true;
}
bool SameAdvancedConfiguration(const ServiceConfiguration &a, const ServiceConfiguration &b) {
    return a.login.kind==b.login.kind && a.login.username==b.login.username && a.dependencies==b.dependencies &&
        SameRawConfiguration(EncodeAdvancedConfiguration(a),EncodeAdvancedConfiguration(b));
}
} // namespace nssm_next
