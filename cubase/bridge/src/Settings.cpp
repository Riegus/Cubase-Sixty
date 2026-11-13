#include "Settings.h"
#include "ScriptLog.h"

#include <atomic>
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
bool g_quietReload = false;

// The settings, in the order the Settings page and settings.txt show them. Add new ones here (the page and the file
// follow); docs/settings-wishlist.md lists what is still fixed in code.
const std::vector<Def>& defs()
{
    static const std::vector<Def> d = {
        {"track-stepping", "Behaviour", "Track stepping",
         "Which tracks the UF1's arrows and the UF1 / UF8 / UC1 CHANNEL encoders step through.",
         {{"mixconsole", "The tracks the MixConsole shows (hidden ones are stepped past)"},
          {"project", "The tracks the Project window shows (Cubase's Navigate Up / Down; click in the Project window first: it only works while that window has the focus)"},
          {"all", "Every track, hidden ones included (Cubase's Select Track: Prev / Next)"}},
         "mixconsole", true},
        {"uc1-encoder2", "Behaviour", "UC1 secondary encoder at start",
         "What the UC1's secondary encoder (right of the screen) turns when Cubase starts. Hold its push and turn to change it while working.",
         {{"bus-comp", "The Bus Comp section through the project's bus comps"},
          {"strips", "The track's channel strips (as the magnifier key)"},
          {"cs-fav", "The channel strip favourites"},
          {"bc-fav", "The bus comp favourites"}},
         "bus-comp", false},
        {"track-names", "Display", "Long track names",
         "A track name longer than a display has room for (UF8 strips and the UF1: 8 characters, the UC1: 12).",
         {{"abbreviate", "Abbreviated (\"Background Vocals\" becomes \"BckgrVcl\")"},
          {"cut", "Cut off at the end (\"Backgrou\")"}},
         "abbreviate", true},
        {"led-brightness", "Devices", "Key and LED brightness",
         "How bright the lit keys and LEDs are, on all three surfaces (Rea-Sixty's and SSL 360's five steps).",
         {{"dark", "Dark"}, {"dim", "Dim"}, {"half", "Half"}, {"bright", "Bright"}, {"full", "Full"}},
         "bright", true},
        {"display-brightness", "Devices", "Display brightness",
         "How bright the displays are: the UF8 strips, the UF1's big screen, the UC1's screen and its status LEDs.",
         {{"dark", "Dark"}, {"dim", "Dim"}, {"half", "Half"}, {"bright", "Bright"}, {"full", "Full"}},
         "bright", true},
        {"sleep", "Devices", "Sleep when idle",
         "The surfaces go dark after this long without a hand on any of them (while Cubase's transport is stopped). The first touch, turn or press wakes them; that touch itself does nothing else.",
         {{"off", "Off"}, {"5", "5 min"}, {"10", "10 min"}, {"15", "15 min"}, {"30", "30 min"}, {"60", "60 min"}},
         "off", true},
        {"uc1-7seg", "Display", "UC1 track number",
         "The 7-segment display on the UC1 (the selected track's number in the MixConsole).",
         {{"blank", "Leading zeros blank (\"  7\")"}, {"zeros", "With leading zeros (\"007\", as Rea-Sixty)"}},
         "blank", true},
        {"touch-selects", "Behaviour", "Touching a UF8 fader selects its track",
         "When on, putting a finger on a UF8 fader also selects that strip's track (as Rea-Sixty's \"Touch selects channel\"), so the UF1 and UC1 follow it.",
         {{"off", "Off"}, {"on", "On"}},
         "off", true},
        {"uf1-start-view", "Behaviour", "UF1 view at start",
         "The view the UF1 starts in when Cubase (or its MIDI Remote scripts) start. MODE and the display soft keys change it while working.",
         {{"daw", "DAW (V-pots on sends 1-4)"}, {"sends", "SENDS (the four sends in the display)"},
          {"plugin", "PLUGIN (the track's SSL plug-in, or Quick Controls)"}, {"meter", "METER (SSL Meter)"}},
         "daw", false},
        {"uf1-bypass", "Behaviour", "UF1 BYPASS key",
         "BYPASS bypasses all inserts of the selected track, and a second press switches them on again. This decides what happens to inserts that were bypassed already before the first press.",
         {{"remember", "They stay bypassed (only the inserts the key bypassed are switched on again)"},
          {"all", "All inserts are switched on"}},
         "remember", true},
        {"uc1-key-magnifier", "UC1 keys", "Magnifier key",
         "What the magnifier key (next to the 360 key) does.",
         {{"next-strip", "Next channel strip on the track"}, {"prev-strip", "Previous channel strip on the track"},
          {"window", "Open / close the strip's window"}, {"channel-settings", "Open / close the track's Channel Settings"},
          {"mixconsole", "Open / close the MixConsole"}, {"bypass", "Bypass / activate the channel strip plug-in"},
          {"nothing", "Nothing"}},
         "next-strip", true},
        {"uc1-key-360", "UC1 keys", "360 key",
         "What the 360 key does.",
         {{"next-strip", "Next channel strip on the track"}, {"prev-strip", "Previous channel strip on the track"},
          {"window", "Open / close the strip's window"}, {"channel-settings", "Open / close the track's Channel Settings"},
          {"mixconsole", "Open / close the MixConsole"}, {"bypass", "Bypass / activate the channel strip plug-in"},
          {"nothing", "Nothing"}},
         "window", true},
        {"uc1-strip-load", "UC1 on Cubase's strip", "Loading the gate and compressor",
         "On a track without an SSL channel strip, the UC1 can load Cubase's strip modules when you reach for them: a Gate knob or EXP loads the Noise Gate, a Dyn knob opens the compressor picker, DYN IN loads a compressor. A module that is there but switched off is always switched on by its knobs.",
         {{"knobs-keys", "From the knobs and the keys"}, {"keys", "From the keys only (DYN IN, EXP)"}, {"never", "Never"}},
         "knobs-keys", true},
        {"uc1-dyn-in-comp", "UC1 on Cubase's strip", "The compressor DYN IN loads",
         "Which of Cubase's strip compressors a short press on DYN IN loads when the track has none. Holding DYN IN still opens the picker. \"The last one picked\" counts from when Cubase started (the Standard Compressor until then).",
         {{"standard", "Standard Compressor"}, {"tube", "Tube Compressor"}, {"vintage", "Vintage Compressor"}, {"last", "The last one picked"}},
         "standard", true},
        {"uc1-bus-comp-start", "UC1 on Cubase's strip", "Bus Comp section before one is picked",
         "Which bus compressor the Bus Comp section is on until you pick one with the Bus Comp encoder.",
         {{"output", "The one on an output channel, else the first in the project"}, {"selected", "The selected track's, when it has one"}},
         "output", true},
        {"uc1-key-push", "UC1 keys", "Secondary encoder push",
         "What a short push on the secondary encoder (right of the screen) does. \"Open / close the strip's window\" opens the window of the plug-in the encoder is on (the bus comp in Bus Comp mode). Holding the push and turning still picks the encoder's job.",
         {{"next-strip", "Next channel strip on the track"}, {"prev-strip", "Previous channel strip on the track"},
          {"window", "Open / close the strip's window"}, {"channel-settings", "Open / close the track's Channel Settings"},
          {"mixconsole", "Open / close the MixConsole"}, {"bypass", "Bypass / activate the channel strip plug-in"},
          {"nothing", "Nothing"}},
         "window", true},
        {"uc1-knob-step", "Behaviour", "UC1 knob steps",
         "How far one click of a UC1 knob moves a value (FINE makes it a quarter of that).",
         {{"fine", "Fine: 1/128 of the range"}, {"normal", "Normal: 1/64 of the range"}, {"coarse", "Coarse: 1/32 of the range"}},
         "normal", true},
        {"script-log", "Logging", "Script log",
         "The MIDI Remote scripts' lines in cubase\\logs\\scripts.log (what the scripts see and do; useful when something goes wrong).",
         {{"on", "On"}, {"off", "Off"}},
         "off", true},   // (off in a release; dev-restart.ps1 switches it on)
    };
    return d;
}

std::string defaultFileText(const std::map<std::string, std::string>& values)
{
    std::string t =
        "# Cubase-Sixty settings. One \"key = value\" per line; '#' starts a comment. The bridge reads the file again\n"
        "# when it changes, so an edit needs no restart. The Settings page (tray icon > Settings) writes this file too.\n";
    std::string section;
    for (const Def& d : defs()) {
        if (d.section != section) { section = d.section; t += "\n# ---- " + section + "\n"; }
        t += "\n# " + d.label + ": " + d.help + "\n";
        for (const Choice& c : d.choices) t += "#   " + c.value + " = " + c.label + (c.value == d.def ? " (default)" : "") + "\n";
        auto it = values.find(d.key);
        t += d.key + " = " + (it != values.end() && !it->second.empty() ? it->second : d.def) + "\n";
    }
    // Keys the file has that the list does not know (kept, at the end).
    bool header = false;
    for (auto& kv : values) {
        bool known = false;
        for (const Def& d : defs()) if (d.key == kv.first) known = true;
        if (known) continue;
        if (!header) { t += "\n# ---- Other\n"; header = true; }
        t += kv.first + " = " + kv.second + "\n";
    }
    return t;
}

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
    // (Not after the page's own change: set() has said what changed.)
    if (!g_quietReload) std::printf("Settings: read %s\n", g_path.string().c_str());
    g_quietReload = false;
}

} // namespace

void init(const char* argv0)
{
    g_path = logDir(argv0).parent_path() / "settings.txt";
    std::error_code ec;
    if (!std::filesystem::exists(g_path, ec)) {
        std::ofstream f(g_path, std::ios::binary);
        if (f) { f << defaultFileText({}); std::printf("Settings: wrote the defaults to %s\n", g_path.string().c_str()); }
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

const std::vector<Def>& schema() { return defs(); }

std::atomic<unsigned> g_generation{0};
unsigned generation() { return g_generation; }

int brightnessLevel(const std::string& key)
{
    const std::string v = lower(get(key, "bright"));
    return v == "dark" ? 0 : v == "dim" ? 1 : v == "half" ? 2 : v == "full" ? 4 : 3;
}

bool set(const std::string& key, const std::string& value, std::string* why)
{
    const std::string k = lower(trim(key)), v = trim(value);
    const Def* def = nullptr;
    for (const Def& d : defs()) if (d.key == k) def = &d;
    if (!def) { if (why) *why = "unknown setting " + k; return false; }
    bool ok = def->choices.empty();
    for (const Choice& c : def->choices) if (c.value == v) ok = true;
    if (!ok) { if (why) *why = "\"" + v + "\" is not one of " + k + "'s choices"; return false; }
    std::lock_guard<std::mutex> lk(g_mx);
    reloadIfChanged();
    auto values = g_values;
    values[k] = v;
    // Written to a new file first, then put in place: a reader never sees half a file.
    const auto tmp = g_path.string() + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (why) *why = "could not write " + tmp; return false; }
        f << defaultFileText(values);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, g_path, ec);
    if (ec) { if (why) *why = "could not replace " + g_path.string() + ": " + ec.message(); return false; }
    std::printf("Settings: %s = %s\n", k.c_str(), v.c_str());
    g_stamp = {};   // read it again on the next look
    g_quietReload = true;
    reloadIfChanged();
    ++g_generation;
    return true;
}

std::filesystem::path dir() { return g_path.parent_path(); }
Stepping trackStepping()
{
    const std::string v = lower(get("track-stepping", "mixconsole"));
    return v == "project" ? Stepping::Project : v == "all" ? Stepping::All : Stepping::MixConsole;
}

} // namespace cs::settings
