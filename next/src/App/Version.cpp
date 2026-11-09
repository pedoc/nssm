#include "pch.h"
#include <iostream>
#include <tchar.h>
#include "../../../version.h"

int RunVersion() {
    std::wcout << L"NSSM Next " << NSSM_VERSION << L" (GUI service manager)\n";
    return 0;
}
