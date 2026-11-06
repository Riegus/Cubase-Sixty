#include "ScriptLog.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>

namespace cs {
namespace {
std::mutex g_mx;
FILE* g_file = nullptr;
std::string g_path;

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

void scriptLogInit(const char* argv0)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::absolute(fs::path(argv0 ? argv0 : ""), ec).parent_path();
    // The bridge runs from cubase\bridge\build\Release: the logs go to cubase\logs.
    fs::path logs = dir / "logs";
    for (fs::path p = dir; !p.empty() && p != p.parent_path(); p = p.parent_path()) {
        if (p.filename() == "bridge") { logs = p.parent_path() / "logs"; break; }
    }
    fs::create_directories(logs, ec);
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
    std::fprintf(g_file, "%s [%s] %s\n", stamp().c_str(), source.c_str(), text.c_str());
    std::fflush(g_file);
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
    scriptLogLine(source, text);
    return true;
}
}
