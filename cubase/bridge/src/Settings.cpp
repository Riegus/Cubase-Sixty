#include "Settings.h"
#include "ScriptLog.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace cs::settings {
namespace {

std::filesystem::path g_path;
std::mutex g_mx;
std::map<std::string, std::string> g_values;
std::filesystem::file_time_type g_stamp{};

const char* const kDefault =
    "# Cubase-Sixty settings. One \"key = value\" per line; '#' starts a comment. The bridge reads the file again\n"
    "# when it changes, so an edit needs no restart.\n"
    "\n"
    "# Which tracks the UF1's arrows, and the UF1 / UF8 / UC1 CHANNEL encoders, step through:\n"
    "#   mixconsole = the tracks shown in the MixConsole (hidden ones are stepped past; needs the UF1)\n"
    "#   project    = the tracks shown in the Project window (Cubase's Navigate Up / Down; the Project window\n"
    "#                should have the focus)\n"
    "#   all        = every track, hidden ones included (Cubase's Select Track: Prev / Next)\n"
    "track-stepping = mixconsole\n";

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }

void reloadIfChanged()
{
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(g_path, ec);
    if (ec || stamp == g_stamp) return;
    g_stamp = stamp;
    g_values.clear();
    std::ifstream f(g_path, std::ios::binary);
    for (std::string line; f && std::getline(f, line); ) {
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);   // UTF-8 BOM
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        g_values[lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }
    std::printf("Settings: read %s (track-stepping = %s)\n", g_path.string().c_str(),
                g_values.count("track-stepping") ? g_values["track-stepping"].c_str() : "mixconsole");
}

} // namespace

void init(const char* argv0)
{
    g_path = logDir(argv0).parent_path() / "settings.txt";
    std::error_code ec;
    if (!std::filesystem::exists(g_path, ec)) {
        std::ofstream f(g_path, std::ios::binary);
        if (f) { f << kDefault; std::printf("Settings: wrote the defaults to %s\n", g_path.string().c_str()); }
    }
    std::lock_guard<std::mutex> lk(g_mx);
    reloadIfChanged();
}

std::string get(const std::string& key, const std::string& fallback)
{
    std::lock_guard<std::mutex> lk(g_mx);
    reloadIfChanged();
    auto it = g_values.find(lower(key));
    return it == g_values.end() || it->second.empty() ? fallback : it->second;
}

Stepping trackStepping()
{
    const std::string v = lower(get("track-stepping", "mixconsole"));
    return v == "project" ? Stepping::Project : v == "all" ? Stepping::All : Stepping::MixConsole;
}

} // namespace cs::settings
