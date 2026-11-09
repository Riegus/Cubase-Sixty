#include "Favourites.h"
#include "ScriptLog.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace cs::favourites {
namespace {

std::filesystem::path g_path;

const char* const kDefault =
    "# Cubase-Sixty favourites: the channel strips and bus compressors the UF8's CHANNEL encoder steps through in its\n"
    "# CS Fav and BC Fav modes (hold the encoder and turn to pick a mode).\n"
    "#\n"
    "# One plug-in per line, its name as Cubase's plug-in list shows it, up to eight under each heading.\n"
    "# The UC1 knows these (their settings are carried across a swap): SSL Native Channel Strip 2, SSL 4K B, SSL 4K E,\n"
    "# SSL 4K G, SSL 360 Link; SSL Native Bus Compressor 2, SSL 360 Link Bus Compressor. Others can be listed too;\n"
    "# they arrive on their own settings.\n"
    "#\n"
    "# The file is read again when you start stepping, so a change needs no restart.\n"
    "\n"
    "[Channel Strip]\n"
    "SSL Native Channel Strip 2\n"
    "SSL 4K E\n"
    "SSL 4K G\n"
    "SSL 4K B\n"
    "\n"
    "[Bus Comp]\n"
    "SSL Native Bus Compressor 2\n"
    "SSL 360 Link Bus Compressor\n";

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

// UTF-8 -> code points, each as three 7-bit bytes (the scripts' decodeName).
void encode(const std::string& utf8, std::vector<unsigned char>& out)
{
    for (size_t i = 0; i < utf8.size(); ) {
        unsigned c = (unsigned char)utf8[i];
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra) c &= 0x3F >> extra;
        ++i;
        for (int k = 0; k < extra && i < utf8.size(); ++k, ++i) c = (c << 6) | ((unsigned char)utf8[i] & 0x3F);
        if (c > 0x1FFFFF) c = '?';
        out.push_back((unsigned char)((c >> 14) & 0x7F)); out.push_back((unsigned char)((c >> 7) & 0x7F)); out.push_back((unsigned char)(c & 0x7F));
    }
}

} // namespace

void init(const char* argv0)
{
    g_path = logDir(argv0).parent_path() / "favourites.txt";
    std::error_code ec;
    if (std::filesystem::exists(g_path, ec)) return;
    std::ofstream f(g_path, std::ios::binary);
    if (f) { f << kDefault; std::printf("Favourites: wrote the default list to %s\n", g_path.string().c_str()); }
}

std::vector<std::string> list(int which)
{
    std::vector<std::string> lists[2];
    std::ifstream f(g_path, std::ios::binary);
    int domain = -1;
    for (std::string line; f && std::getline(f, line); ) {
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line = line.substr(3);
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[') {
            std::string h; for (char c : line) h += char(std::tolower((unsigned char)c));
            domain = h.find("bus") != std::string::npos ? 1 : h.find("channel") != std::string::npos || h.find("strip") != std::string::npos ? 0 : -1;
            continue;
        }
        if (domain >= 0 && lists[domain].size() < 8) lists[domain].push_back(line);
    }
    return which == 0 || which == 1 ? lists[which] : std::vector<std::string>{};
}

std::vector<std::vector<unsigned char>> messages()
{
    const std::vector<std::string> lists[2] = {list(0), list(1)};
    std::vector<std::vector<unsigned char>> out;
    for (int d = 0; d < 2; ++d) {
        std::vector<unsigned char> m{0xF0, 0x7D, 0x52, 0x36, 0x43, (unsigned char)d};
        for (size_t i = 0; i < lists[d].size(); ++i) {
            if (i) m.insert(m.end(), {0x7F, 0x7F, 0x7F});
            encode(lists[d][i], m);
        }
        m.push_back(0xF7);
        out.push_back(std::move(m));
    }
    return out;
}

const std::vector<std::string>& known(int domain)
{
    static const std::vector<std::string> strips = {"SSL Native Channel Strip 2", "SSL 4K B", "SSL 4K E", "SSL 4K G", "SSL 360 Link"};
    static const std::vector<std::string> buses = {"SSL Native Bus Compressor 2", "SSL 360 Link Bus Compressor"};
    return domain == 1 ? buses : strips;
}

bool write(const std::vector<std::string>& strips, const std::vector<std::string>& busComps, std::string* why)
{
    // The explanation at the top of the default file (up to its first heading), then the two lists.
    std::string head(kDefault);
    head = head.substr(0, head.find("[Channel Strip]"));
    std::string t = head + "[Channel Strip]\n";
    auto add = [&](const std::vector<std::string>& names) {
        int n = 0;
        for (const auto& raw : names) {
            const std::string name = trim(raw);
            if (name.empty() || name[0] == '#' || name[0] == '[' || n >= 8) continue;
            t += name + "\n";
            ++n;
        }
    };
    add(strips);
    t += "\n[Bus Comp]\n";
    add(busComps);
    const auto tmp = g_path.string() + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (why) *why = "could not write " + tmp; return false; }
        f << t;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, g_path, ec);
    if (ec) { if (why) *why = "could not replace " + g_path.string() + ": " + ec.message(); return false; }
    std::printf("Favourites: saved (%s)\n", g_path.string().c_str());
    return true;
}

} // namespace cs::favourites
