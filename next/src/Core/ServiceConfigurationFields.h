#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nssm_next {
enum class AccountKind : std::uint32_t { LocalSystem, LocalService, NetworkService, VirtualService, Custom };
struct LoginConfiguration {
    AccountKind kind = AccountKind::LocalSystem;
    std::wstring username;
};
struct ProcessConfiguration {
    std::uint32_t priority = 0x20; // NORMAL_PRIORITY_CLASS, not a combo-box index.
    std::wstring affinity; // Empty means all processors; otherwise CPU list/ranges.
    bool console_window = true;
};
struct ShutdownConfiguration {
    bool console = true, window = true, threads = true, terminate = true;
    std::uint32_t console_ms = 1500, window_ms = 1500, threads_ms = 1500;
    bool kill_tree = true;
    std::uint32_t preserved_skip_bits = 0;
};
enum class ExitAction : std::uint32_t { Restart, Ignore, Exit, Suicide };
struct ExitConfiguration {
    ExitAction action = ExitAction::Restart;
    std::uint32_t restart_ms = 0, throttle_ms = 1500;
};
struct StreamConfiguration {
    std::wstring path;
    std::uint32_t share_mode = 3;
    std::uint32_t disposition = 4; // OPEN_ALWAYS
    std::uint32_t flags = 128; // FILE_ATTRIBUTE_NORMAL
    bool copy_and_truncate = false;
};
struct IoConfiguration {
    StreamConfiguration input;
    StreamConfiguration output;
    StreamConfiguration error;
    bool timestamp = false;
    IoConfiguration() { input.share_mode = 2; input.disposition = 3; }
};
struct RotationConfiguration {
    bool enabled = false, online = false;
    std::uint32_t older_than_seconds = 0, delay_ms = 0;
    std::uint64_t larger_than_bytes = 0;
};
struct EnvironmentConfiguration {
    bool replacement_present = false, extra_present = false;
    std::vector<std::wstring> replacement;
    std::vector<std::wstring> extra;
};
struct HookConfiguration {
    // Start/Pre, Start/Post, Stop/Pre, Exit/Post, Power/Change,
    // Power/Resume, Rotate/Pre, Rotate/Post.
    std::array<std::wstring, 8> commands{};
    bool redirect_output = false;
};
struct RawConfigurationValue {
    std::wstring path; // Relative to Parameters; "$service" denotes the service key.
    std::wstring name;
    bool service_root = false;
    bool present = false;
    std::uint32_t type = 0;
    std::vector<std::uint8_t> bytes;
};
struct ConfigurationDiagnostic {
    std::wstring page, field, message;
};
} // namespace nssm_next
