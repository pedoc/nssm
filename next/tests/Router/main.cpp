#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../src/Core/ApplicationValuePolicy.h"

int NssmRouterUnderTest(int argc, wchar_t **argv);
int NssmWorkerRouterUnderTest(int argc, wchar_t **argv);
namespace {
enum class Route {
    Gui,
    Version,
    Worker,
    Reject
};
struct Probe {
    wchar_t *executable;
    int gui = 0;
    int version = 0;
    int worker = 0;
    int legacy = 0;
};
Probe *active = nullptr;
unsigned cases = 0;
void Check(std::vector<std::wstring> arguments, Route expected, bool worker = false) {
    std::vector<wchar_t *> argv;
    for (auto &argument : arguments)
        argv.push_back(argument.data());
    Probe probe{argv.empty() ? nullptr : argv[0]};
    active = &probe;
    const auto code = worker ? NssmWorkerRouterUnderTest(static_cast<int>(argv.size()), argv.data())
                             : NssmRouterUnderTest(static_cast<int>(argv.size()), argv.data());
    active = nullptr;
    ++cases;
    const auto wanted = expected == Route::Gui       ? 17
                        : expected == Route::Version ? 42
                        : expected == Route::Worker  ? 63
                                                     : 87;
    if (code != wanted || probe.gui != (expected == Route::Gui) ||
        probe.version != (expected == Route::Version) ||
        probe.worker != (expected == Route::Worker) || probe.legacy != 0)
        throw std::runtime_error(
            "GUI/worker routing crossed a boundary or allowed a legacy mutating command");
}
} // namespace
int NssmLegacyMain(int, wchar_t **) {
    ++active->legacy;
    return 99;
}
int RunWinUi() {
    ++active->gui;
    return 17;
}
int RunVersion() {
    ++active->version;
    return 42;
}
int RunManagedServiceHost(wchar_t *executable) {
    if (executable != active->executable)
        throw std::runtime_error("Worker executable argument changed");
    ++active->worker;
    return 63;
}
int main() {
    try {
        Check({L"program"}, Route::Gui);
        Check({L"program", L"--ui"}, Route::Gui);
        Check({L"program", L"--ui", L"--debug"}, Route::Reject);
        Check({L"program", L"--UI"}, Route::Reject);
        Check({L"program", L"--service"}, Route::Reject);
        for (const auto *version :
             {L"version", L"-v", L"-V", L"-version", L"--version", L"/version"})
            Check({L"program", version}, Route::Version);
        for (const auto *command : {L"install", L"edit", L"get", L"set", L"reset", L"unset",
                                    L"dump", L"start", L"stop", L"restart", L"pause", L"continue",
                                    L"remove", L"list", L"processes", L"status", L"rotate"})
            Check({L"program", command, L"service name", L"argument with spaces"}, Route::Reject);
        Check({L"worker", L"--service"}, Route::Worker, true);
        Check({L"worker"}, Route::Reject, true);
        Check({L"worker", L"--service", L"extra"}, Route::Reject, true);
        Check({L"worker", L"install", L"not-allowed"}, Route::Reject, true);
        using nssm_next::detail::IsValidApplicationBytes;
        if (IsValidApplicationBytes(0) || IsValidApplicationBytes(1) ||
            !IsValidApplicationBytes(2) || !IsValidApplicationBytes(65536) ||
            IsValidApplicationBytes(65535) || IsValidApplicationBytes(65538))
            throw std::runtime_error("Registry string allocation limit changed");
        std::cout
            << "PASS " << cases
            << " GUI/worker routing cases and 6 allocation-boundary cases; no service operation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
