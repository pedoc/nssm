#include <cwchar>
#include "../Core/ServiceHostBridge.h"

int wmain(int argc, wchar_t **argv) {
    if (argc != 2 || std::wcscmp(argv[1], L"--service") != 0)
        return 87;
    return RunManagedServiceHost(argv[0]);
}
