#include "SslPresets.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cs::sslpresets {

std::string dirFor(const std::string& plugin)
{
    // Longest match first: "360 Link Bus Compressor" before "360 Link" (Rea-Sixty kDirs).
    static const struct { const char* part; const char* leaf; } kDirs[] = {
        {"Channel Strip 2", "ChannelStrip2"}, {"360 Link Bus Compressor", "SSL360LinkBusCompressor"},
        {"360 Link", "SSL360Link"}, {"Bus Compressor 2", "BusCompressor2"},
        {"4K B", "SSL4KB"}, {"4K E", "SSL4KE"}, {"4K G", "SSL4KG"},
        {"Meter Pro", "MeterPro"}, {"Meter", "Meter"},   // the UF1's METER view (Rea-Sixty's kDirs)
    };
    for (const auto& d : kDirs) {
        if (plugin.find(d.part) == std::string::npos) continue;
        const char* pd = std::getenv("ProgramData");
        std::string root = (pd && *pd) ? pd : "C:\\ProgramData";
        return root + "\\Solid State Logic\\PlugIns\\Presets\\" + d.leaf;
    }
    return {};
}

namespace {
// Natural order ("+9" before "+18"), case-insensitive: SSL 360's own list order (Rea-Sixty scan()).
bool naturalLess(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = a[i], cb = b[j];
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && std::isdigit((unsigned char)a[i2])) ++i2;
            while (j2 < b.size() && std::isdigit((unsigned char)b[j2])) ++j2;
            size_t is = i; while (is < i2 - 1 && a[is] == '0') ++is;
            size_t js = j; while (js < j2 - 1 && b[js] == '0') ++js;
            if (i2 - is != j2 - js) return (i2 - is) < (j2 - js);
            for (size_t k = 0; k < i2 - is; ++k) if (a[is + k] != b[js + k]) return a[is + k] < b[js + k];
            i = i2; j = j2;
        } else {
            const int la = std::tolower(ca), lb = std::tolower(cb);
            if (la != lb) return la < lb;
            ++i; ++j;
        }
    }
    return (a.size() - i) < (b.size() - j);
}

std::vector<unsigned char> encode3x7(const std::string& utf8)
{
    std::vector<unsigned char> out;
    for (size_t i = 0; i < utf8.size(); ++i) {   // ASCII is all a preset id or value holds
        const unsigned c = (unsigned char)utf8[i];
        out.push_back((unsigned char)((c >> 14) & 0x7F)); out.push_back((unsigned char)((c >> 7) & 0x7F)); out.push_back((unsigned char)(c & 0x7F));
    }
    return out;
}
} // namespace

std::vector<Entry> scan(const std::string& dir)
{
    namespace fs = std::filesystem;
    std::vector<Entry> out;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 4) { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec)) continue;
        const fs::path p = it->path();
        std::string ext = p.extension().string();
        for (auto& c : ext) c = char(std::tolower((unsigned char)c));
        if (ext != ".xml") continue;
        std::string group = fs::relative(p.parent_path(), dir, ec).generic_string();
        if (group == ".") group.clear();
        out.push_back({p.stem().string(), group, p.string()});
    }
    std::sort(out.begin(), out.end(), [](const Entry& x, const Entry& y) {
        return naturalLess(x.group.empty() ? x.name : x.group + "/" + x.name, y.group.empty() ? y.name : y.group + "/" + y.name);
    });
    return out;
}

std::string latin1(const std::string& utf8)
{
    std::string out;
    for (size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char c = utf8[i];
        if (c < 0x80) out.push_back(char(c));
        else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) { const unsigned v = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F); out.push_back(v < 256 ? char(v) : '?'); ++i; }
        else { out.push_back('?'); while (i + 1 < utf8.size() && (utf8[i + 1] & 0xC0) == 0x80) ++i; }
    }
    return out;
}

std::vector<std::vector<unsigned char>> loadMessages(const Entry& e, unsigned char section)
{
    std::vector<std::vector<unsigned char>> out;
    std::ifstream f(e.path, std::ios::binary);
    if (!f) return out;
    std::stringstream ss; ss << f.rdbuf();
    const std::string xml = ss.str();
    for (size_t pos = 0; (pos = xml.find("<PARAM", pos)) != std::string::npos; ) {
        const size_t end = xml.find("/>", pos);
        if (end == std::string::npos) break;
        const std::string tag = xml.substr(pos, end - pos);
        pos = end + 2;
        const size_t ip = tag.find("id=\""), vp = tag.find("value=\"");
        if (ip == std::string::npos || vp == std::string::npos) continue;
        const size_t ie = tag.find('"', ip + 4), ve = tag.find('"', vp + 7);
        if (ie == std::string::npos || ve == std::string::npos) continue;
        std::vector<unsigned char> m{0xF0, 0x7D, 0x52, 0x36, 0x3C, section};
        auto id = encode3x7(tag.substr(ip + 4, ie - (ip + 4)));
        auto val = encode3x7(tag.substr(vp + 7, ve - (vp + 7)));
        m.insert(m.end(), id.begin(), id.end());
        m.insert(m.end(), {0x7F, 0x7F, 0x7F});
        m.insert(m.end(), val.begin(), val.end());
        m.push_back(0xF7);
        out.push_back(std::move(m));
    }
    std::vector<unsigned char> m{0xF0, 0x7D, 0x52, 0x36, 0x3D, section};
    auto name = encode3x7(e.name);
    m.insert(m.end(), name.begin(), name.end());
    m.push_back(0xF7);
    out.push_back(std::move(m));
    return out;
}

} // namespace cs::sslpresets
