#include <cwchar>
#include <initializer_list>

int RunWinUi();
int RunVersion();

int wmain(int argc, wchar_t **argv) {
    if (argc == 1 || (argc == 2 && std::wcscmp(argv[1], L"--ui") == 0))
        return RunWinUi();
    if (argc == 2) {
        for (const auto *flag : {L"version", L"-v", L"-V", L"-version", L"--version", L"/version"})
            if (std::wcscmp(argv[1], flag) == 0)
                return RunVersion();
    }
    // Management is through the reviewed native API, never the legacy CLI.
    return 87;
}
