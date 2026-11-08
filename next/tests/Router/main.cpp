#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../src/Core/ApplicationValuePolicy.h"

int NssmRouterUnderTest(int argc, wchar_t **argv);

namespace {
struct Probe {
    int argc;
    wchar_t **argv;
    int legacy_calls = 0;
    int ui_calls = 0;
};
Probe *active = nullptr;

void Check(std::vector<std::wstring> arguments, bool expect_ui) {
    std::vector<wchar_t *> argv;
    for (auto &argument : arguments)
        argv.push_back(argument.data());
    Probe probe{static_cast<int>(argv.size()), argv.data()};
    active = &probe;
    const int result = NssmRouterUnderTest(probe.argc, probe.argv);
    active = nullptr;
    if (result != (expect_ui ? 17 : 42) || probe.ui_calls != (expect_ui ? 1 : 0) ||
        probe.legacy_calls != (expect_ui ? 0 : 1)) {
        throw std::runtime_error("Entry routing or return-code passthrough changed");
    }
}
} // namespace

int NssmLegacyMain(int argc, wchar_t **argv) {
    if (!active || argc != active->argc || argv != active->argv) {
        throw std::runtime_error("Legacy argc/argv were rewritten");
    }
    ++active->legacy_calls;
    return 42;
}

int RunWinUi() {
    ++active->ui_calls;
    return 17;
}

int main() {
    try {
        Check({L"program"}, true);
        Check({L"program", L"--ui"}, true);
        Check({L"program", L"--ui", L"--debug"}, false);
        Check({L"program", L"--UI"}, false);
        Check({L"program", L"--service"}, false);
        for (const auto *command :
             {L"install", L"edit", L"get", L"set", L"reset", L"unset", L"dump", L"start", L"stop",
              L"restart", L"pause", L"continue", L"remove", L"list", L"processes", L"status",
              L"statuscode", L"rotate", L"version"}) {
            Check({L"program", command, L"service name", L"argument with spaces"}, false);
        }
        using nssm_next::detail::IsValidApplicationBytes;
        if (IsValidApplicationBytes(0) || IsValidApplicationBytes(1) ||
            !IsValidApplicationBytes(2) || !IsValidApplicationBytes(65536) ||
            IsValidApplicationBytes(65535) || IsValidApplicationBytes(65538)) {
            throw std::runtime_error("Registry string allocation limit changed");
        }
        std::cout << "PASS: 24 routing and 6 allocation-boundary cases; no service operation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
