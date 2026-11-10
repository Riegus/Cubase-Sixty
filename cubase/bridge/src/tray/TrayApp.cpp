// Cubase-Sixty tray app (Windows): runs the bridge in the background and keeps it running.
//
//   - Starts the bridge (cubase_sixty_bridge.exe, next to this program) without a console window, and starts it
//     again if it stops on its own (up to five times a minute; then it waits for "Restart bridge").
//   - A tray icon: green = the bridge runs and Cubase has a project open, yellow = the bridge runs but Cubase is not
//     there (yet) or shows the Hub, red = the bridge is not running. The menu shows each surface's and Cubase's
//     state (read from cubase\logs\bridge.log), and has Settings (the bridge's Settings page in the browser; also a
//     click on the icon), Restart bridge, Open logs folder, Start with Windows, Quit.
//   - Start with Windows: the usual per-user Run entry (HKCU\Software\Microsoft\Windows\CurrentVersion\Run).
//   - Stop bridge / Start bridge: stopping blanks the surfaces and lets go of them (e.g. for another program); the
//     icon turns grey and the bridge is not started again until Start bridge (or the app's next start).
//   - One instance only. Not a Windows service on purpose (see docs\release-plan.md, step 3).
//
// When the bridge is started again, its MIDI ports are made anew; Cubase connects the scripts to them again by
// itself after a little while (seen 9.11.2026), so Cubase does not need a restart.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 500;
enum : UINT { kCmdRestart = 100, kCmdLogs, kCmdAutostart, kCmdQuit, kCmdSettings, kCmdStop };
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"Cubase-Sixty";

HWND g_wnd = nullptr;
NOTIFYICONDATAW g_nid{};
PROCESS_INFORMATION g_bridge{};
bool g_bridgeRunning = false;
bool g_quitting = false;
std::deque<std::chrono::steady_clock::time_point> g_restarts;   // automatic restarts in the last minute
bool g_gaveUp = false;
fs::path g_exeDir, g_bridgeExe, g_logsDir;
std::wstring g_bridgeArgs;
bool g_loopMidi = false;
// After a restart: Cubase connects to the new ports by itself (seen once, 9.11.2026). Not trusted blindly: if it
// was connected before and has not spoken again within 20 s, the app says to restart Cubase.
std::chrono::steady_clock::time_point g_expectCubaseBy{};
bool g_expectCubase = false;
// Stopped from the menu: the bridge is not started again until Start bridge.
bool g_paused = false;

// What bridge.log says.
struct Status {
    std::string uf8 = "not found", uf1 = "not found", uc1 = "not found";
    std::string cubase = "not seen yet";
    bool projectOpen = false;
    std::string settingsUrl;   // the bridge's Settings page ("Settings page: http://127.0.0.1:<port>/")
} g_status;
std::uintmax_t g_logPos = 0;
fs::file_time_type g_logTime{};

std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// The logs folder the bridge uses: <...>\cubase\logs when the bridge sits under cubase\bridge (as built), else
// "logs" next to it (ScriptLog.cpp, logDir).
fs::path logsDirFor(const fs::path& exeDir)
{
    for (fs::path p = exeDir; !p.empty() && p != p.parent_path(); p = p.parent_path())
        if (p.filename() == "bridge") return p.parent_path() / "logs";
    return exeDir / "logs";
}

// ---------------------------------------------------------------------------------------------- the icon

// A small mixing surface in the status colour, drawn at run time (no resource file): a rounded panel with three
// fader slots and their caps at different heights. Drawn at 4x and averaged down, for smooth edges at any size.
HICON makeIcon(COLORREF rgb)
{
    const int size = GetSystemMetrics(SM_CXSMICON);
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi;
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    auto* px = static_cast<uint32_t*>(bits);

    // The design on a 16-unit square (u = x / size * 16).
    struct Rgba { double r, g, b, a; };
    const Rgba panel{GetRValue(rgb) / 255.0, GetGValue(rgb) / 255.0, GetBValue(rgb) / 255.0, 1};
    const Rgba rim{panel.r * 0.5, panel.g * 0.5, panel.b * 0.5, 1};
    const Rgba slot{0.12, 0.12, 0.12, 1};
    const Rgba cap{1, 1, 1, 1};
    auto inRound = [](double x, double y, double x0, double y0, double x1, double y1, double rad) {
        if (x < x0 || x > x1 || y < y0 || y > y1) return false;
        const double cx = x < x0 + rad ? x0 + rad : x > x1 - rad ? x1 - rad : x;
        const double cy = y < y0 + rad ? y0 + rad : y > y1 - rad ? y1 - rad : y;
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad;
    };
    const double slotsX[3] = {4.5, 8.0, 11.5};
    const double capsY[3] = {9.5, 5.0, 7.5};   // fader positions
    auto sample = [&](double u, double v) -> Rgba {
        Rgba c{0, 0, 0, 0};
        if (inRound(u, v, 0.5, 1.0, 15.5, 15.0, 2.5)) c = rim;
        if (inRound(u, v, 1.5, 2.0, 14.5, 14.0, 1.8)) c = panel;
        for (int k = 0; k < 3; ++k) {
            if (inRound(u, v, slotsX[k] - 0.6, 3.5, slotsX[k] + 0.6, 12.5, 0.6)) c = slot;
            if (inRound(u, v, slotsX[k] - 1.6, capsY[k] - 1.1, slotsX[k] + 1.6, capsY[k] + 1.1, 0.5)) c = cap;
        }
        return c;
    };
    constexpr int ss = 4;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx) {
                    const Rgba c = sample((x + (sx + 0.5) / ss) * 16.0 / size, (y + (sy + 0.5) / ss) * 16.0 / size);
                    r += c.r * c.a; g += c.g * c.a; b += c.b * c.a; a += c.a;
                }
            const double n = ss * ss;
            const uint32_t A = uint32_t(a / n * 255 + 0.5);
            const uint32_t R = uint32_t(r / n * 255 + 0.5), G = uint32_t(g / n * 255 + 0.5), B = uint32_t(b / n * 255 + 0.5);
            px[y * size + x] = (A << 24) | (R << 16) | (G << 8) | B;   // premultiplied
        }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

HICON g_icons[4]{};   // red, yellow, green, grey (stopped from the menu)

std::wstring tooltip()
{
    std::wstring t = L"Cubase-Sixty: ";
    if (g_paused) t += L"bridge stopped (Start bridge in the menu)";
    else if (!g_bridgeRunning) t += g_gaveUp ? L"bridge stopped (restart it from the menu)" : L"bridge not running";
    else t += L"UF8 " + widen(g_status.uf8) + L", UF1 " + widen(g_status.uf1) + L", UC1 " + widen(g_status.uc1);
    if (t.size() > 127) t.resize(127);
    return t;
}

void updateIcon()
{
    const int level = g_paused ? 3 : !g_bridgeRunning ? 0 : g_status.projectOpen ? 2 : 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;   // (NIF_SHOWTIP: version 4 icons show no tip otherwise)
    g_nid.hIcon = g_icons[level];
    wcsncpy_s(g_nid.szTip, tooltip().c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void notify(const std::wstring& title, const std::wstring& text)
{
    g_nid.uFlags = NIF_INFO;
    wcsncpy_s(g_nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(g_nid.szInfo, text.c_str(), _TRUNCATE);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ---------------------------------------------------------------------------------------------- the bridge

bool startBridge()
{
    g_status = Status{};
    g_logPos = 0;
    // stdout / stderr to NUL: the bridge writes its own log (bridge.log); no console window.
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = nul;
    si.hStdError = nul;
    std::wstring cmd = L"\"" + g_bridgeExe.wstring() + L"\"" + g_bridgeArgs;
    const BOOL ok = CreateProcessW(g_bridgeExe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   g_exeDir.c_str(), &si, &g_bridge);
    CloseHandle(nul);
    g_bridgeRunning = ok != FALSE;
    if (ok) CloseHandle(g_bridge.hThread);
    updateIcon();
    return g_bridgeRunning;
}

void stopBridge()
{
    if (!g_bridgeRunning) return;
    // Ask politely (the named event the bridge watches, StopEvent.h): it blanks the surfaces and closes them. Ended
    // the hard way only if it has not gone within 5 s.
    if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\CubaseSixtyBridgeStop")) { SetEvent(ev); CloseHandle(ev); }
    if (WaitForSingleObject(g_bridge.hProcess, 5000) != WAIT_OBJECT_0) {
        TerminateProcess(g_bridge.hProcess, 0);
        WaitForSingleObject(g_bridge.hProcess, 5000);
    }
    CloseHandle(g_bridge.hProcess);
    g_bridge = {};
    g_bridgeRunning = false;
}

void restartBridge(bool automatic)
{
    const bool cubaseWasThere = g_status.cubase != "not seen yet";
    stopBridge();
    Sleep(1500);   // let Windows release the USB devices and the ports (the bridge now closes them itself first)
    if (!startBridge()) return;
    if (automatic)
        notify(L"The bridge stopped and was started again",
               L"Cubase should connect to it again after a few seconds. bridge.previous.log (Open logs folder) says why it stopped.");
    g_expectCubase = cubaseWasThere && !g_loopMidi;
    g_expectCubaseBy = std::chrono::steady_clock::now() + std::chrono::seconds(20);
}

// The bridge ended without being asked to: start it again, unless that has happened five times in a minute.
void checkBridge()
{
    if (!g_bridgeRunning || WaitForSingleObject(g_bridge.hProcess, 0) != WAIT_OBJECT_0) return;
    DWORD code = 0;
    GetExitCodeProcess(g_bridge.hProcess, &code);
    CloseHandle(g_bridge.hProcess);
    g_bridge = {};
    g_bridgeRunning = false;
    updateIcon();
    if (g_quitting || g_paused) return;
    const auto now = std::chrono::steady_clock::now();
    while (!g_restarts.empty() && now - g_restarts.front() > std::chrono::minutes(1)) g_restarts.pop_front();
    if (g_restarts.size() >= 5) {
        g_gaveUp = true;
        updateIcon();
        notify(L"The bridge keeps stopping", L"It stopped five times in a minute (exit code " + std::to_wstring(code) +
               L"). See bridge.log (Open logs folder), then Restart bridge.");
        return;
    }
    g_restarts.push_back(now);
    restartBridge(true);
}

// ---------------------------------------------------------------------------------------------- bridge.log

void readLogLine(const std::string& line)
{
    auto has = [&](const char* s) { return line.find(s) != std::string::npos; };
    struct { const char* name; std::string* state; } surfaces[] = {{"UF8", &g_status.uf8}, {"UF1", &g_status.uf1}, {"UC1", &g_status.uc1}};
    for (auto& s : surfaces) {
        const std::string n = s.name;
        if (has((n + " connected").c_str())) *s.state = "connected";
        else if (has((n + " not found").c_str())) *s.state = "not found";
        else if (has(("The " + n + " stopped answering or was unplugged").c_str())) *s.state = "unplugged";
    }
    if (const size_t at = line.find("Settings page: http://"); at != std::string::npos) {
        std::string url = line.substr(at + 15);
        while (!url.empty() && (url.back() == '\r' || url.back() == ' ')) url.pop_back();
        g_status.settingsUrl = url;
    }
    if (has("Cubase connected")) { g_status.cubase = "connected"; g_status.projectOpen = true; }
    if (has("Cubase: project open")) { g_status.cubase = "project open"; g_status.projectOpen = true; }
    if (has("Cubase: no project")) { g_status.cubase = "no project (the Hub)"; g_status.projectOpen = false; }
}

// Reads what has been added to bridge.log since the last look (it is started anew with each bridge).
void readLog()
{
    const fs::path file = g_logsDir / "bridge.log";
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) return;
    if (size < g_logPos) g_logPos = 0;   // a new bridge, a new log
    if (size == g_logPos) return;
    std::ifstream in(file, std::ios::binary);
    in.seekg(std::streamoff(g_logPos));
    std::string line;
    const std::string old = std::string(g_status.uf8 + g_status.uf1 + g_status.uc1 + g_status.cubase);
    while (std::getline(in, line)) {
        if (in.eof()) break;   // an unfinished last line: read it next time
        g_logPos += line.size() + 1;
        readLogLine(line);
    }
    if (old != g_status.uf8 + g_status.uf1 + g_status.uc1 + g_status.cubase) updateIcon();
}

// While the bridge is stopped, the SSL plug-ins start SSL's own SSL360Core.exe (they look for a Core). It makes MIDI
// ports of its own, and when the bridge closes it again on its start, Cubase reconnected only the UF8 script
// (10.11.2026; Reload Scripts fixed it). SSL 360 cannot use the controllers anyway (they are on Rea-Sixty's WinUSB
// driver), so the app closes it while the bridge is stopped, as the bridge does while it runs.
void closeSslCore()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"SSL360Core.exe") != 0) continue;
        if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID)) { TerminateProcess(h, 0); CloseHandle(h); }
    }
    CloseHandle(snap);
}

// ---------------------------------------------------------------------------------------------- autostart

bool autostartOn()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof buf;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void setAutostart(bool on)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        const std::wstring v = L"\"" + std::wstring(self) + L"\"";
        RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), DWORD((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
}

// ---------------------------------------------------------------------------------------------- the menu

void showMenu()
{
    HMENU m = CreatePopupMenu();
    auto info = [&](const std::wstring& t) { AppendMenuW(m, MF_STRING | MF_GRAYED, 0, t.c_str()); };
    if (g_paused) {
        info(L"Bridge: stopped (the surfaces are free)");
    } else if (g_bridgeRunning) {
        info(L"Bridge: running");
        info(L"UF8: " + widen(g_status.uf8));
        info(L"UF1: " + widen(g_status.uf1));
        info(L"UC1: " + widen(g_status.uc1));
        info(L"Cubase: " + widen(g_status.cubase));
    } else {
        info(g_gaveUp ? L"Bridge: stopped (it kept stopping)" : L"Bridge: not running");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_bridgeRunning && !g_status.settingsUrl.empty() ? 0 : MF_GRAYED), kCmdSettings, L"Settings...");
    SetMenuDefaultItem(m, kCmdSettings, FALSE);
    AppendMenuW(m, MF_STRING, kCmdRestart, g_bridgeRunning ? L"Restart bridge" : L"Start bridge");
    if (g_bridgeRunning) AppendMenuW(m, MF_STRING, kCmdStop, L"Stop bridge");
    AppendMenuW(m, MF_STRING, kCmdLogs, L"Open logs folder");
    AppendMenuW(m, MF_STRING | (autostartOn() ? MF_CHECKED : 0), kCmdAutostart, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kCmdQuit, L"Quit (stops the bridge)");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_wnd);   // so the menu closes when clicking elsewhere
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_wnd, nullptr);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) {   // Explorer restarted: put the icon back
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        updateIcon();
        return 0;
    }
    switch (msg) {
    case kTrayMsg:
        // A click opens the Settings page (the menu while there is none); the right button the menu.
        // (Version 4 icons: a click comes as NIN_SELECT, the right button as WM_CONTEXTMENU; each once.)
        if (LOWORD(lp) == NIN_SELECT || LOWORD(lp) == NIN_KEYSELECT) {
            if (!g_paused && g_bridgeRunning && !g_status.settingsUrl.empty()) PostMessageW(h, WM_COMMAND, kCmdSettings, 0);
            else showMenu();
        } else if (LOWORD(lp) == WM_CONTEXTMENU) showMenu();
        return 0;
    case WM_TIMER:
        checkBridge();
        if (g_bridgeRunning) readLog();
        if (g_paused) { static int n = 0; if (++n % 4 == 0) closeSslCore(); }   // every 2 s
        if (g_expectCubase && g_status.cubase != "not seen yet") g_expectCubase = false;   // it came back
        if (g_expectCubase && g_bridgeRunning && std::chrono::steady_clock::now() > g_expectCubaseBy) {
            g_expectCubase = false;
            notify(L"Cubase has not connected to the bridge again", L"Restart Cubase so it finds the bridge's MIDI ports.");
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kCmdStop:
            g_paused = true;   // first: checkBridge must not start it again
            stopBridge();
            updateIcon();
            break;
        case kCmdRestart:
            g_paused = false;
            g_gaveUp = false;
            g_restarts.clear();
            restartBridge(false);
            break;
        case kCmdSettings:
            if (!g_status.settingsUrl.empty())
                ShellExecuteW(nullptr, L"open", widen(g_status.settingsUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kCmdLogs:
            fs::create_directories(g_logsDir);
            ShellExecuteW(nullptr, L"open", g_logsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kCmdAutostart:
            setAutostart(!autostartOn());
            break;
        case kCmdQuit:
            g_quitting = true;
            stopBridge();
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
        }
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int)
{
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\CubaseSixtyTray");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;   // already running

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_exeDir = fs::path(self).parent_path();
    g_bridgeExe = g_exeDir / L"cubase_sixty_bridge.exe";
    g_logsDir = logsDirFor(g_exeDir);
    // Arguments are passed on to the bridge (e.g. --loopmidi, --trace).
    if (cmdLine && *cmdLine) g_bridgeArgs = std::wstring(L" ") + cmdLine;
    g_loopMidi = g_bridgeArgs.find(L"--loopmidi") != std::wstring::npos;

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"CubaseSixtyTray";
    RegisterClassW(&wc);
    g_wnd = CreateWindowW(wc.lpszClassName, L"Cubase-Sixty", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);

    g_icons[0] = makeIcon(RGB(220, 50, 47));
    g_icons[1] = makeIcon(RGB(240, 180, 30));
    g_icons[2] = makeIcon(RGB(60, 190, 80));
    g_icons[3] = makeIcon(RGB(140, 140, 140));
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = kTrayMsg;
    g_nid.hIcon = g_icons[0];
    wcsncpy_s(g_nid.szTip, L"Cubase-Sixty", _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);

    if (!fs::exists(g_bridgeExe)) {
        notify(L"Cubase-Sixty", L"The bridge was not found next to this program: " + g_bridgeExe.wstring());
    } else if (!startBridge()) {
        notify(L"Cubase-Sixty", L"The bridge could not be started.");
    }
    SetTimer(g_wnd, kTimerId, kTimerMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    stopBridge();
    for (HICON i : g_icons) DestroyIcon(i);
    if (single) CloseHandle(single);
    return 0;
}
