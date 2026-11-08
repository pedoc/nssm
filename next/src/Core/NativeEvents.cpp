#include <windows.h>
#include <array>
#include <cstdarg>

const wchar_t *nssm_unquoted_imagepath();

void RegisterNextEventMessages() noexcept {
    HKEY key = nullptr;
    constexpr wchar_t source[] =
        L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\NssmNext";
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, source, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        OutputDebugStringW(L"NssmNext: could not register the private event source.\n");
        return;
    }
    const auto path = nssm_unquoted_imagepath();
    const auto bytes = static_cast<DWORD>((lstrlenW(path) + 1) * sizeof(wchar_t));
    const DWORD types = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;
    const auto first = RegSetValueExW(key, L"EventMessageFile", 0, REG_SZ,
                                      reinterpret_cast<const BYTE *>(path), bytes);
    const auto second = RegSetValueExW(key, L"TypesSupported", 0, REG_DWORD,
                                       reinterpret_cast<const BYTE *>(&types), sizeof(types));
    RegCloseKey(key);
    if (first != ERROR_SUCCESS || second != ERROR_SUCCESS)
        OutputDebugStringW(L"NssmNext: private event-source registration was incomplete.\n");
}

// Signature matches the unchanged native engine. The original event.cpp logger
// is compiled under a different symbol; engine calls bind to this private source.
void log_event(unsigned short type, unsigned long id, ...) {
    const auto event = RegisterEventSourceW(nullptr, L"NssmNext");
    if (!event)
        return;
    std::array<LPCWSTR, 16> strings{};
    WORD count = 0;
    va_list arguments;
    va_start(arguments, id);
    while (count < strings.size() - 1) {
        const auto value = va_arg(arguments, LPCWSTR);
        if (!value)
            break;
        strings[count++] = value;
    }
    va_end(arguments);
    ReportEventW(event, type, 0, id, nullptr, count, 0, count ? strings.data() : nullptr, nullptr);
    DeregisterEventSource(event);
}
