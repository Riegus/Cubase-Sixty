#include "ScriptLog.h"
#include "Settings.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define CS_PIPE(fds) _pipe(fds, 1 << 16, _O_BINARY)
#define CS_DUP _dup
#define CS_DUP2 _dup2
#define CS_READ _read
#define CS_WRITE _write
#else
#include <unistd.h>
#define CS_PIPE(fds) pipe(fds)
#define CS_DUP dup
#define CS_DUP2 dup2
#define CS_READ read
#define CS_WRITE write
#endif

namespace cs {
namespace {
std::mutex g_mx;
FILE* g_file = nullptr;
std::string g_path;
long g_bytes = 0;
constexpr long kMaxBytes = 20L * 1024 * 1024;   // past this the file starts again (the old part kept as .old)

std::string stamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}
}

// The bridge runs from cubase\bridge\build\Release: the logs go to cubase\logs.
std::filesystem::path logDir(const char* argv0)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::absolute(fs::path(argv0 ? argv0 : ""), ec).parent_path();
    fs::path logs = dir / "logs";
    for (fs::path p = dir; !p.empty() && p != p.parent_path(); p = p.parent_path()) {
        if (p.filename() == "bridge") { logs = p.parent_path() / "logs"; break; }
    }
    fs::create_directories(logs, ec);
    return logs;
}

void redirectTempToLogs(const char* argv0)
{
    const std::string dir = logDir(argv0).string() + (logDir(argv0).string().back() == '\\' ? "" : "\\");
#ifdef _WIN32
    _putenv_s("TMP", dir.c_str());
    _putenv_s("TEMP", dir.c_str());
    std::error_code ec;
    std::filesystem::remove(logDir(argv0) / "rea_sixty.log", ec);   // one session per file, like the others
#else
    (void)dir;
#endif
}

void bridgeLogInit(const char* argv0)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path logs = logDir(argv0);
    const fs::path file = logs / "bridge.log";
    if (fs::exists(file, ec)) fs::rename(file, logs / "bridge.previous.log", ec);
    const std::string path = file.string();
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) { std::fprintf(stderr, "Bridge log could not be opened: %s\n", path.c_str()); return; }
    // stdout and stderr go into a pipe; a thread copies what comes out to the window and to the file.
    int fds[2];
    if (CS_PIPE(fds) != 0) { std::fclose(f); return; }
    std::fflush(stdout);
    std::fflush(stderr);
    const int window = CS_DUP(1);
    CS_DUP2(fds[1], 1);
    CS_DUP2(fds[1], 2);
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a pipe would otherwise hold lines back
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::thread([f, window, rd = fds[0], path]() mutable {
        char buf[4096];
        bool lineStart = true;
        long bytes = 0;
        constexpr long kMax = 20L * 1024 * 1024;
        for (;;) {
            const int n = CS_READ(rd, buf, sizeof buf);
            if (n <= 0) break;
            CS_WRITE(window, buf, n);
            for (int i = 0; i < n; ++i) {
                if (lineStart) { bytes += std::fprintf(f, "%s ", stamp().c_str()); lineStart = false; }
                std::fputc(buf[i], f);
                ++bytes;
                if (buf[i] == '\n') lineStart = true;
            }
            std::fflush(f);
            if (bytes > kMax && lineStart) {
                std::fclose(f);
                std::error_code e2;
                std::filesystem::rename(path, path + ".old", e2);
                f = std::fopen(path.c_str(), "w");
                bytes = 0;
                if (!f) break;
            }
        }
    }).detach();
    std::printf("Bridge log: %s\n", path.c_str());
}

void scriptLogInit(const char* argv0)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path logs = logDir(argv0);
    const fs::path file = logs / "scripts.log";
    if (fs::exists(file, ec)) fs::rename(file, logs / "scripts.previous.log", ec);
    std::lock_guard<std::mutex> lk(g_mx);
    g_path = file.string();
    g_file = std::fopen(g_path.c_str(), "w");
    if (g_file) std::printf("Script log: %s\n", g_path.c_str());
    else std::fprintf(stderr, "Script log could not be opened: %s\n", g_path.c_str());
}

void scriptLogLine(const std::string& source, const std::string& text)
{
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_file) return;
    const int n = std::fprintf(g_file, "%s [%s] %s\n", stamp().c_str(), source.c_str(), text.c_str());
    std::fflush(g_file);
    if (n > 0) g_bytes += n;
    if (g_bytes > kMaxBytes) {
        std::fclose(g_file);
        std::error_code ec;
        std::filesystem::rename(g_path, g_path + ".old", ec);
        g_file = std::fopen(g_path.c_str(), "w");
        g_bytes = 0;
    }
}

bool scriptLogSysex(const unsigned char* m, size_t len)
{
    if (len < 7 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 0x52 || m[3] != 0x36 || m[4] != 0x7E) return false;
    static const char* kSources[] = {"?", "UF8", "UF1", "UC1", "Helper"};
    const char* source = m[5] < 5 ? kSources[m[5]] : "?";
    std::string text;
    for (size_t i = 6; i + 2 < len && m[i] != 0xF7 && m[i + 1] != 0xF7 && m[i + 2] != 0xF7; i += 3) {
        const unsigned c = (unsigned(m[i]) << 14) | (unsigned(m[i + 1]) << 7) | m[i + 2];
        // UTF-8, so the file shows names exactly as Cubase has them.
        if (c < 0x80) text.push_back(char(c));
        else if (c < 0x800) { text.push_back(char(0xC0 | (c >> 6))); text.push_back(char(0x80 | (c & 0x3F))); }
        else { text.push_back(char(0xE0 | (c >> 12))); text.push_back(char(0x80 | ((c >> 6) & 0x3F))); text.push_back(char(0x80 | (c & 0x3F))); }
    }
    // The Settings page's "Script log" (script-log = on / off), looked at once a second at most.
    thread_local auto checked = std::chrono::steady_clock::time_point{};   // (each MIDI thread its own)
    thread_local bool on = true;
    const auto now = std::chrono::steady_clock::now();
    if (now - checked > std::chrono::seconds(1)) { checked = now; on = settings::get("script-log", "off") == "on"; }
    if (on) scriptLogLine(source, text);
    return true;
}
}
