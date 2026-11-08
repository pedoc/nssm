#include <cwchar>

int NssmLegacyMain(int argc, wchar_t **argv);
int RunWinUi();

int wmain(int argc, wchar_t **argv) {
    if (argc == 1 || (argc == 2 && std::wcscmp(argv[1], L"--ui") == 0)) {
        return RunWinUi();
    }
    return NssmLegacyMain(argc, argv);
}
