#include "../../src/Core/ServiceConfigurationCodec.h"
#include "../../src/Core/ServicePolicy.h"
#include "../Fakes/MemoryServices.h"
#include <functional>
#include <iostream>
#include <limits>

using namespace nssm_next;
namespace {
unsigned checked = 0;
void Assert(bool condition, const char *message) { ++checked; if (!condition) throw std::runtime_error(message); }
const RawConfigurationValue &Find(const std::vector<RawConfigurationValue> &values, const std::wstring &name, const std::wstring &path = L"") {
    for (const auto &value : values) if (value.name == name && value.path == path) return value;
    throw std::runtime_error("Missing physical configuration value");
}
std::uint32_t Dword(const RawConfigurationValue &value) {
    Assert(value.type == 4 && value.bytes.size() == 4, "physical value must be a DWORD");
    return value.bytes[0] | (std::uint32_t{value.bytes[1]} << 8) | (std::uint32_t{value.bytes[2]} << 16) | (std::uint32_t{value.bytes[3]} << 24);
}
void Reject(const std::function<void()> &operation, const char *message) {
    bool failed = false; try { operation(); } catch (const ServiceError &) { failed = true; } Assert(failed, message);
}
ServiceInfo Settings() {
    ServiceInfo c;
    c.name = L"advanced"; c.application = L"C:\\Program Files\\app.exe";
    c.process.priority = 0x80; c.process.affinity = L"0,2-3"; c.process.console_window = false;
    c.shutdown.console = false; c.shutdown.threads = false; c.shutdown.preserved_skip_bits = 0x100;
    c.shutdown.console_ms = 2500; c.shutdown.window_ms = 3000; c.shutdown.threads_ms = 4000; c.shutdown.kill_tree = false;
    c.exit.action = ExitAction::Exit; c.exit.restart_ms = 1250; c.exit.throttle_ms = 3500;
    c.io.input.path = L"%DATA_ROOT%\\input.txt"; c.io.output.path = L"%DATA_ROOT%\\output.log"; c.io.error.path = L"C:\\logs\\error.log";
    c.io.timestamp = true; c.io.output.disposition = 2; c.io.error.copy_and_truncate = true;
    c.rotation.enabled = true; c.rotation.online = true; c.rotation.older_than_seconds = 60; c.rotation.delay_ms = 250;
    c.rotation.larger_than_bytes = (std::uint64_t{7} << 32) | 123;
    c.environment.replacement_present = true; c.environment.replacement = {L"BASE=value",L"EMPTY="};
    c.environment.extra_present = true; c.environment.extra = {L"BASE=overlay",L"EQUALS=a=b=c"};
    for (unsigned i = 0; i < c.hooks.commands.size(); ++i) c.hooks.commands[i] = L"C:\\hooks\\handler.exe --case " + std::to_wstring(i);
    c.hooks.redirect_output = true;
    return c;
}
}
unsigned RunAdvancedTests() {
    const auto c = Settings();
    ValidateAdvancedConfiguration(c);
    const auto values = EncodeAdvancedConfiguration(c);
    Assert(Dword(Find(values,L"AppPriority")) == 0x80, "priority stored as Windows constant, not index");
    Assert(Find(values,L"AppAffinity").type == 1, "affinity is string list, not DWORD/QWORD");
    Assert(Dword(Find(values,L"AppStopMethodSkip")) == 0x105, "shutdown skip bits are inverted and retain unknown bits");
    Assert(Find(values,L"",L"AppExit").type == 1, "default exit action is unnamed string under AppExit");
    Assert(Find(values,L"AppStdout").type == 2, "I/O path retains expansion semantics");
    Assert(Dword(Find(values,L"AppStdoutCreationDisposition")) == 2, "overwrite uses flat stream disposition value");
    Assert(Dword(Find(values,L"AppRotateBytesHigh")) == 7 && Dword(Find(values,L"AppRotateBytes")) == 123, "64-bit rotation threshold not truncated");
    const auto &environment = Find(values,L"AppEnvironment");
    Assert(environment.type == 7 && environment.present && environment.bytes.size() >= 4, "replacement block uses MULTI_SZ");
    Assert(environment.bytes[environment.bytes.size()-1] == 0 && environment.bytes[environment.bytes.size()-3] == 0, "environment block has double termination");
    Assert(Find(values,L"AppEnvironmentExtra").present, "overlay block coexists with replacement");
    Assert(Find(values,L"Pre",L"AppEvents\\Start").type == 2, "hook nested command is expandable string");
    ServiceInfo decoded; DecodeAdvancedConfiguration(decoded, values);
    Assert(SameAdvancedConfiguration(c, decoded), "all advanced sections roundtrip together");
    Assert(decoded.environment.extra[1] == L"EQUALS=a=b=c", "embedded equals in environment value retained");
    Assert(ParseAffinity(L"0,2-3,63") == (std::uint64_t{1} | 12 | (std::uint64_t{1} << 63)), "CPU63 parsed without signed shift");
    Assert(ParseAffinity(L"All") == 0, "all CPU sentinel preserved");
    Reject([] { ParseAffinity(L"64"); }, "CPU64 cannot silently truncate");
    Reject([] { ParseAffinity(L"3-1"); }, "descending affinity range rejected");
    Assert(ParseUnsigned(L"18446744073709551615", (std::numeric_limits<std::uint64_t>::max)(), L"size") == (std::numeric_limits<std::uint64_t>::max)(), "full uint64 value parsed exactly");
    Reject([] { ParseUnsigned(L"18446744073709551616", (std::numeric_limits<std::uint64_t>::max)(), L"size"); }, "uint64 overflow rejected");
    Reject([] { ParseUnsigned(L"-1", 0xffffffffu, L"delay"); }, "negative delay rejected");
    Assert(HookPairs().size() == 8 && HookPairs()[2].first == L"Stop" && HookPairs()[2].second == L"Pre", "only canonical hook pairs exposed");
    auto invalid = c; invalid.environment.extra = {L"NO_EQUALS"};
    Reject([&] { ValidateAdvancedConfiguration(invalid); }, "malformed environment line rejected");
    const auto expanded = NormalizeConfiguration([] { ServiceConfiguration value; value.name=L"env";value.application=L"%APP_ROOT%\\app.exe";return value; }());
    Assert(expanded.application == L"%APP_ROOT%\\app.exe" && expanded.working_directory.empty(), "raw expansion and automatic directory preserved");
    testing::MemoryServices manager(L"C:\\Windows\\System32\\cmd.exe",false);
    auto created = manager.Create(c); auto fresh = manager.Read(created);
    Assert(SameAdvancedConfiguration(c,fresh), "memory manager persists all advanced fields");
    auto edited = static_cast<ServiceConfiguration>(fresh); edited.rotation.larger_than_bytes = (std::uint64_t{9} << 32) | 77;
    edited.login.kind = AccountKind::VirtualService; edited.dependencies = {L"RpcSs", L"+NetworkProvider"};
    manager.Update(fresh,edited); const auto after = manager.Read(created);
    Assert(after.account == L"NT SERVICE\\advanced" && after.dependencies == edited.dependencies, "virtual identity and dependency groups persist");
    Assert(after.rotation.larger_than_bytes == edited.rotation.larger_than_bytes && after.hooks.commands == c.hooks.commands, "unrelated edits retain high rotation bits and hooks");
    ServiceConfiguration custom = c; custom.name=L"custom";custom.login.kind=AccountKind::Custom;custom.login.username=L".\\fixture";
    Reject([&] { manager.Create(custom); }, "new custom account requires transient password");
    CredentialRequest credentials; credentials.password.Assign(L"not-a-real-secret",17); credentials.password_supplied=true;
    const auto customCreated=manager.Create(custom,credentials);
    Assert(customCreated.account == L".\\fixture", "fake custom identity persists without recording password");
    credentials.password.Clear(); Assert(credentials.password.Empty(), "owned password buffer clears");
    std::cout << "PASS " << checked << " advanced configuration codec/roundtrip assertions; no Windows service API linked\n";
    return checked;
}
