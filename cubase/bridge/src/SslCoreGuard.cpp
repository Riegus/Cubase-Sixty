// SSL's own background service, SSL360Core.exe, must not run next to the bridge: it takes the controllers' USB
// devices, and the SSL plug-ins connect to whichever "Core" they find first, so with it running most of them never
// reach the bridge's stand-in (seen: 6 of ~60 connected, SSL360Core.exe running). The plug-ins start it again when
// they cannot find a Core, so the bridge closes it at start-up and then whenever it reappears.
#include <cstdio>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace cs {
// Ends every process named SSL360Core.exe. Returns how many were ended.
int stopSslCore()
{
#ifdef _WIN32
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"SSL360Core.exe") != 0) continue;
        if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID)) {
            if (TerminateProcess(h, 0)) ++n;
            CloseHandle(h);
        }
    }
    CloseHandle(snap);
    if (n) std::printf("Closed SSL360Core.exe (%d) - the bridge stands in for it.\n", n);
    return n;
#else
    return 0;
#endif
}
}
