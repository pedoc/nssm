#include <windows.h>
#include "ServiceHostBridge.h"

int NssmLegacyMain(int argc, wchar_t **argv);
void WINAPI service_main(unsigned long argc, wchar_t **argv);
void RegisterNextEventMessages() noexcept;

// The legacy entry point otherwise rewrites the machine-global NSSM source.
// Its calls are renamed only for the experimental Core build, not in root sources.
int NssmNextSkipLegacyMessages() {
    return 0;
}

void WINAPI NssmNextServiceMain(unsigned long argc, wchar_t **argv) {
    // Only the actual SCM callback performs our separate event-source registration.
    RegisterNextEventMessages();
    service_main(argc, argv);
}

int RunManagedServiceHost(wchar_t *executable) {
    wchar_t *arguments[] = {executable, nullptr};
    // Internal worker mode is never an interactive native CLI command.
    SetStdHandle(STD_INPUT_HANDLE, nullptr);
    return NssmLegacyMain(1, arguments);
}
