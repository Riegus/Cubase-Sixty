#include "CubaseSetup.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace cs::cubaseSetup {
namespace {

const char* const kSurfaces[] = {"UF8", "UF1", "UC1", "Helper", "MCU"};
const char* const kWays[] = {"to Cubase", "from Cubase"};
const std::string kMcuIn = "Cubase-Sixty MCU to Cubase";
const std::string kMcuOut = "Cubase-Sixty MCU from Cubase";
const std::string kBackupName = "Defaults (before Cubase-Sixty).xml";

std::string readFile(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const fs::path& p, const std::string& s)
{
    const fs::path tmp = p.parent_path() / (p.filename().string() + ".cubase-sixty-new");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << s;
        if (!f.good()) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);   // (replaces the old file: MoveFileEx with REPLACE_EXISTING on Windows)
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

// The MIDI device name Cubase files the ports under ("Windows MIDI"; another with Windows MIDI Services): from the
// folder's Port Setup.xml, the Cubase-Sixty ports' own if Cubase has seen them, else the one most ports use.
std::string midiDevice(const fs::path& folder)
{
    const std::string ps = readFile(folder / "Port Setup.xml");
    std::smatch m;
    if (std::regex_search(ps, m, std::regex("\"I\\|([^|\"]+)\\|Cubase-Sixty "))) return m[1];
    if (ps.find("\"I|Windows MIDI|") != std::string::npos) return "Windows MIDI";
    if (std::regex_search(ps, m, std::regex("\"I\\|([^|\"]+)\\|"))) return m[1];
    return "Windows MIDI";
}

std::vector<std::string> portIds(const std::string& device)
{
    std::vector<std::string> ids;
    for (const char* s : kSurfaces)
        for (const char* w : kWays) ids.push_back("I|" + device + "|Cubase-Sixty " + s + " " + w);
    return ids;
}

std::string eolOf(const std::string& s) { return s.find("\r\n") != std::string::npos ? "\r\n" : "\n"; }

// A top-level entry ("      <item>" ... "      </item>") by its Group name: [begin, end) of its lines; npos if none.
struct Range { size_t begin = std::string::npos, end = std::string::npos; bool found() const { return begin != std::string::npos; } };
Range group(const std::string& s, const std::string& name)
{
    Range r;
    const size_t at = s.find("<string name=\"Group\" value=\"" + name + "\"/>");
    if (at == std::string::npos) return r;
    const std::string eol = eolOf(s);
    const size_t item = s.rfind("<item>", at);
    if (item == std::string::npos) return r;
    const size_t lineStart = s.rfind(eol, item);
    const size_t close = s.find(eol + "      </item>", at);
    if (close == std::string::npos) return r;
    r.begin = lineStart == std::string::npos ? 0 : lineStart + eol.size();
    r.end = close + eol.size() + std::string("      </item>").size();
    return r;
}

// The start of the line holding position p.
size_t lineStartOf(const std::string& s, size_t p)
{
    const size_t e = s.rfind('\n', p == 0 ? 0 : p - 1);
    return e == std::string::npos ? 0 : e + 1;
}

// The text of <string name="s" value="..."/> in the object named `obj` within [r.begin, r.end).
std::string objValue(const std::string& s, const Range& r, const std::string& obj, size_t* valueAt = nullptr, size_t* valueLen = nullptr)
{
    const size_t at = s.find("name=\"" + obj + "\"", r.begin);
    if (at == std::string::npos || at >= r.end) return {};
    const std::string key = "<string name=\"s\" value=\"";
    const size_t v = s.find(key, at);
    const size_t objEnd = s.find("</obj>", at);
    if (v == std::string::npos || v >= r.end || (objEnd != std::string::npos && v > objEnd)) return {};
    const size_t from = v + key.size(), to = s.find('"', from);
    if (to == std::string::npos) return {};
    if (valueAt) *valueAt = from;
    if (valueLen) *valueLen = to - from;
    return s.substr(from, to - from);
}

long long maxId(const std::string& s)
{
    long long best = 0;
    static const std::regex re("ID=\"([0-9]+)\"");
    for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it) best = std::max(best, std::atoll((*it)[1].str().c_str()));
    return best;
}

// Where a new top-level entry goes: just after the Entries list opens.
size_t entriesStart(const std::string& s)
{
    const size_t at = s.find("<list name=\"Entries\" type=\"list\">");
    if (at == std::string::npos) return std::string::npos;
    const size_t e = s.find('\n', at);
    return e == std::string::npos ? std::string::npos : e + 1;
}

void inspectText(const std::string& s, const std::string& device, Folder& f)
{
    f.readable = s.find("<Defaults>") != std::string::npos && entriesStart(s) != std::string::npos;
    if (!f.readable) return;
    const auto ids = portIds(device);
    f.ports = int(ids.size());
    f.excluded = 0;
    if (const Range g = group(s, "All MIDI Inputs"); g.found())
        for (const auto& id : ids) {
            const size_t at = s.find("value=\"" + id + "\"", g.begin);
            if (at != std::string::npos && at < g.end) ++f.excluded;
        }
    const Range studio = group(s, "Studio");
    const size_t devices = studio.found() ? s.find("<list name=\"Devices\" type=\"list\">", studio.begin) : std::string::npos;
    if (devices == std::string::npos || devices >= studio.end) { f.mackie = Mackie::NoStudio; return; }
    const size_t listed = s.find("<string name=\"Name\" value=\"Mackie Control\"/>", devices);
    const bool isListed = listed != std::string::npos && listed < studio.end;
    std::string in, out;
    if (const Range m = group(s, "Mackie Control"); m.found()) { in = objValue(s, m, "InputPort"); out = objValue(s, m, "OutputPort"); }
    if (isListed && in == kMcuIn && out == kMcuOut) f.mackie = Mackie::Ours;
    else if (isListed && !in.empty() && in != kMcuIn) { f.mackie = Mackie::Other; f.mackieOther = in; }
    else f.mackie = Mackie::Missing;
}

// The edits. Each finds its place again in the text as it stands.
void addExcluded(std::string& s, const std::string& device)
{
    const std::string eol = eolOf(s);
    std::vector<std::string> missing;
    Range g = group(s, "All MIDI Inputs");
    for (const auto& id : portIds(device)) {
        const size_t at = g.found() ? s.find("value=\"" + id + "\"", g.begin) : std::string::npos;
        if (at == std::string::npos || at >= g.end) missing.push_back(id);
    }
    if (missing.empty()) return;
    std::string items;
    for (const auto& id : missing) items += "                  <item value=\"" + id + "\"/>" + eol;
    if (!g.found()) {
        const std::string block = "      <item>" + eol + "         <string name=\"Group\" value=\"All MIDI Inputs\"/>" + eol +
            "         <member name=\"Values\">" + eol + "            <member name=\"Excluded\">" + eol +
            "               <list name=\"PortIDs\" type=\"string\">" + eol + items + "               </list>" + eol +
            "            </member>" + eol + "         </member>" + eol + "      </item>" + eol;
        s.insert(entriesStart(s), block);
        return;
    }
    const size_t list = s.find("<list name=\"PortIDs\" type=\"string\">", g.begin);
    if (list != std::string::npos && list < g.end) {
        const size_t close = s.find("</list>", list);
        if (close != std::string::npos && close < g.end) { s.insert(lineStartOf(s, close), items); return; }
    }
    const std::string excluded = "            <member name=\"Excluded\">" + eol + "               <list name=\"PortIDs\" type=\"string\">" +
        eol + items + "               </list>" + eol + "            </member>" + eol;
    const size_t valuesClose = s.rfind(eol + "         </member>", g.end);
    if (valuesClose != std::string::npos && valuesClose > g.begin) { s.insert(valuesClose + eol.size(), excluded); return; }
    // (no Values at all: the entry is replaced)
    s.erase(g.begin, g.end - g.begin);
    addExcluded(s, device);
}

void setMackie(std::string& s, const std::string& device)
{
    const std::string eol = eolOf(s);
    long long id = maxId(s) + 16;
    // In Studio Setup's device list.
    const Range studio = group(s, "Studio");
    const size_t devices = s.find("<list name=\"Devices\" type=\"list\">", studio.begin);
    const size_t listed = s.find("<string name=\"Name\" value=\"Mackie Control\"/>", devices);
    if (listed == std::string::npos || listed >= studio.end) {
        const size_t close = s.find("</list>", devices);
        s.insert(lineStartOf(s, close), "                  <item>" + eol +
            "                     <string name=\"Category\" value=\"Remote Devices\"/>" + eol +
            "                     <string name=\"Name\" value=\"Mackie Control\"/>" + eol + "                  </item>" + eol);
    }
    // Its ports.
    const std::pair<const char*, std::string> values[] = {{"InputDevice", device}, {"InputPort", kMcuIn},
                                                          {"OutputDevice", device}, {"OutputPort", kMcuOut}};
    auto objText = [&](const char* name, const std::string& v) {
        return "            <obj class=\"CmString\" name=\"" + std::string(name) + "\" ID=\"" + std::to_string(id += 16) + "\">" + eol +
               "               <string name=\"s\" value=\"" + v + "\"/>" + eol + "            </obj>" + eol;
    };
    Range m = group(s, "Mackie Control");
    if (!m.found()) {
        std::string objs;
        for (const auto& [name, v] : values) objs += objText(name, v);
        s.insert(entriesStart(s), "      <item>" + eol + "         <string name=\"Group\" value=\"Mackie Control\"/>" + eol +
                 "         <member name=\"Values\">" + eol + objs + "         </member>" + eol + "      </item>" + eol);
        return;
    }
    for (const auto& [name, v] : values) {
        m = group(s, "Mackie Control");
        size_t at = 0, len = 0;
        if (s.find("name=\"" + std::string(name) + "\"", m.begin) < m.end && (objValue(s, m, name, &at, &len), at != 0)) {
            s.replace(at, len, v);
            continue;
        }
        const size_t valuesClose = s.rfind(eol + "         </member>", m.end);
        if (valuesClose != std::string::npos && valuesClose > m.begin) s.insert(valuesClose + eol.size(), objText(name, v));
    }
}

fs::path prefsRoot()
{
    if (const char* t = std::getenv("CUBASE_SIXTY_PREFS_ROOT")) return t;   // (tests)
#ifdef _WIN32
    if (const wchar_t* a = _wgetenv(L"APPDATA")) return fs::path(a) / L"Steinberg";
#endif
    return {};
}

std::string jsonText(const std::string& v)
{
    std::string o = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') {}
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o + "\"";
}

} // namespace

std::vector<Folder> inspect()
{
    std::vector<Folder> out;
    const fs::path root = prefsRoot();
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        const std::string name = e.path().filename().string();
        if (!e.is_directory() || name.rfind("Cubase", 0) != 0 || name.size() < 3 || name.substr(name.size() - 3) != "_64") continue;
        const fs::path file = e.path() / "Defaults.xml";
        if (!fs::exists(file, ec)) continue;
        Folder f;
        f.name = name;
        f.file = file;
        inspectText(readFile(file), midiDevice(e.path()), f);
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const Folder& a, const Folder& b) { return a.name > b.name; });   // newest first
    return out;
}

bool cubaseRunning()
{
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        const wchar_t* n = pe.szExeFile;
        if (_wcsnicmp(n, L"Cubase", 6) != 0) continue;
        const wchar_t* r = n + 6;
        while (*r >= L'0' && *r <= L'9') ++r;
        if (_wcsicmp(r, L".exe") == 0) found = true;
    }
    CloseHandle(snap);
    return found;
#else
    return false;
#endif
}

std::string plannedChanges(const std::vector<Folder>& folders)
{
    std::string o;
    for (const auto& f : folders) {
        if (!f.readable) continue;
        std::string lines;
        if (f.excluded < f.ports)
            lines += "  - \"In 'All MIDI Inputs'\" off for " + std::to_string(f.ports - f.excluded) +
                     " Cubase-Sixty port(s) (so MIDI tracks do not play the controllers' messages)\n";
        if (f.mackie == Mackie::Missing)
            lines += "  - a Mackie Control device in Studio Setup on the ports \"" + kMcuIn + "\" / \"" + kMcuOut + "\" (for SCRUB on the UF1)\n";
        if (f.mackie == Mackie::Other && lines.size())
            lines += "  (its Mackie Control device is used by another controller, \"" + f.mackieOther + "\": left as it is, so SCRUB will not work)\n";
        if (f.mackie == Mackie::NoStudio && lines.size())
            lines += "  (no Studio Setup device list yet: open Studio > Studio Setup in Cubase once, then do this again for SCRUB)\n";
        if (!lines.empty()) o += f.name + ":\n" + lines;
    }
    return o;
}

std::string apply()
{
    if (cubaseRunning()) return "Cubase is running. Close it first: Cubase writes these settings itself when it quits.";
    std::string report;
    for (Folder f : inspect()) {
        if (!f.readable) { report += f.name + ": the preferences file could not be read; left as it is.\n"; continue; }
        const bool doPorts = f.excluded < f.ports, doMackie = f.mackie == Mackie::Missing;
        if (!doPorts && !doMackie) { report += f.name + ": already set up.\n"; continue; }
        std::string s = readFile(f.file);
        const std::string device = midiDevice(f.file.parent_path());
        const fs::path backup = f.file.parent_path() / kBackupName;
        std::error_code ec;
        if (!fs::exists(backup, ec)) fs::copy_file(f.file, backup, ec);
        if (ec) { report += f.name + ": no copy of the preferences file could be made (" + ec.message() + "); left as it is.\n"; continue; }
        if (doPorts) addExcluded(s, device);
        if (doMackie) setMackie(s, device);
        Folder check = f;
        inspectText(s, device, check);
        if (!check.readable || check.excluded < check.ports || (doMackie && check.mackie != Mackie::Ours)) {
            report += f.name + ": the change could not be made safely; left as it is.\n";
            continue;
        }
        if (!writeFile(f.file, s)) { report += f.name + ": the preferences file could not be written; left as it is.\n"; continue; }
        report += f.name + ": set up";
        if (f.mackie == Mackie::Other) report += " (the Mackie Control device is another controller's: SCRUB will not work)";
        if (f.mackie == Mackie::NoStudio) report += " (no Mackie Control device: open Studio > Studio Setup once, then do this again)";
        report += ". The file as it was: " + kBackupName + ".\n";
    }
    if (report.empty()) report = "No Cubase preferences were found (start Cubase once first).\n";
    return report;
}

std::string stateJson()
{
    const auto folders = inspect();
    std::string o = "{\"cubaseRunning\":" + std::string(cubaseRunning() ? "true" : "false") + ",\"folders\":[";
    for (size_t i = 0; i < folders.size(); ++i) {
        const auto& f = folders[i];
        const char* mk = f.mackie == Mackie::Ours ? "ours" : f.mackie == Mackie::Other ? "other" : f.mackie == Mackie::NoStudio ? "nostudio" : "missing";
        o += (i ? "," : "") + std::string("{\"name\":") + jsonText(f.name) + ",\"readable\":" + (f.readable ? "true" : "false") +
             ",\"excluded\":" + std::to_string(f.excluded) + ",\"ports\":" + std::to_string(f.ports) + ",\"mackie\":\"" + mk +
             "\",\"mackieOther\":" + jsonText(f.mackieOther) + ",\"complete\":" + (f.complete() ? "true" : "false") + "}";
    }
    return o + "],\"planned\":" + jsonText(plannedChanges(folders)) + "}";
}

} // namespace cs::cubaseSetup
