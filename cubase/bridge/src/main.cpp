//
// Cubase-Sixty bridge, v0: UF8 <-> two loopMIDI ports <-> Cubase MIDI Remote script.
//
//   UF8 --USB--> [this program] --"Cubase-Sixty UF8 to Cubase"-->   Cubase script
//   UF8 <-USB--  [this program] <-"Cubase-Sixty UF8 from Cubase"--  Cubase script
//
// Scope of v0: 8 faders (motorised, with touch), SOLO/CUT/SEL buttons and LEDs,
// V-pot turn and push, bank keys, scribble-strip names and colours.
// The USB side is Rea-Sixty's UF8Device / Protocol, unchanged.
// SSL 360 must NOT be running (it claims the USB device exclusively).
//

#include <algorithm>
#include <array>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "RtMidi.h"
#include "MidiPorts.h"
#include "StopEvent.h"
#include "SettingsServer.h"
#include "Names.h"
#include "Brightness.h"
#include "Sleep.h"

#include "Mapping.h"
#include "SslCoreImpersonator.h"
#include "Protocol.h"
#include "UF8Device.h"
#include "Uf8Fader.h"
#include "Uf8Frames.h"
#include "Uf1Bridge.h"
#ifndef CS_VERSION
#define CS_VERSION "dev"
#endif
#include "Favourites.h"
#include "Settings.h"
#include <fstream>
#include "Uc1Bridge.h"
#include "ScriptLog.h"
namespace cs { int stopSslCore(); bool cubaseRunning(); }

using namespace cs;

namespace {

constexpr const char* kPortToCubase   = "Cubase-Sixty UF8 to Cubase";    // we send here
constexpr const char* kPortFromCubase = "Cubase-Sixty UF8 from Cubase";  // we listen here

std::atomic<bool> g_run{true};
void onSignal(int) { g_run = false; }

uf8::UF8Device g_dev;
MidiOutPort    g_out;
MidiInPort     g_in;
// The helper MIDI Remote script (no hardware): it holds the UF8 script's large channel lists, because Cubase
// re-checks every channel of a script whenever that script's bank moves (with the lists in the UF8 script a
// bank press took ~0.3 s instead of ~0.07 s). The bridge passes its names (SysEx 18 / 19) on to the UF8
// script, and the UF8 script's requests for them (SysEx 1B) on to the helper.
constexpr const char* kHelperTo   = "Cubase-Sixty Helper to Cubase";
constexpr const char* kHelperFrom = "Cubase-Sixty Helper from Cubase";
MidiOutPort    g_helperOut;
MidiInPort     g_helperIn;
std::atomic<bool> g_helperOpen{false};
std::mutex     g_helperMx;
std::mutex     g_outMx;
uf8frames::Assembler g_asm;

std::atomic<bool> g_touched[kStrips];
bool              g_engaged[kStrips] = {};      // motor engaged (guarded by g_faderMx)
std::mutex        g_faderMx;
uint8_t           g_palette[kStrips] = {};
std::mutex        g_paletteMx;

bool g_trace8 = false;
std::atomic<bool> g_noRec[8] = {};   // strips whose top soft key cannot arm (group, folder, FX): dark and ignored
std::atomic<bool> g_recOn[8] = {};
// Selection mode (Rea-Sixty's NORM / REC / AUTO keys, ids 0x70 / 0x71 / 0x72): what SEL does. NORM selects the
// track, REC toggles its record-enable (as the top soft key), AUTO toggles its automation Write. The SEL LED shows
// what SEL does: the selection (NORM), or red for armed (REC) / writing automation (AUTO).
enum { kSelNorm = 0, kSelRec = 1, kSelAuto = 2 };
std::atomic<int> g_selMode{kSelNorm};
std::atomic<bool> g_selOn[8] = {}, g_autoW[8] = {};
void paintSel(int strip);
void paintSelMode();

// SSL soft keys (Rea-Sixty's default for the top soft keys, as SSL 360): six pages of eight of the channel strip's
// parameters, picked with the bank keys V-POT and 1-5 (ids 0x68..0x6D). A top soft key focuses its parameter: the
// eight V-pots then work it on each strip's SSL channel strip (the script does that: SysEx 21 / 22 / 23 out, 20
// back), until the key is pressed again. Labels (Rea-Sixty's softkey::kCsLabels) in each strip's slot cell.
const char* const kSoftKeyLabels[6][8] = {
    { "BYPASS",  "IN TRIM",  "PHASE",      "PRE",       "MIC/DRV",  "",          "IMP IN",   "IMP" },
    { "WIDTH",   "",         "",           "A/B",       "HPF",      "LPF",       "EQ",       "EQ TYPE" },
    { "LF FREQ", "LF GAIN",  "LF TYPE",    "",          "LMF FREQ", "LMF GAIN",  "LMF Q",    "" },
    { "HMF FREQ","HMF GAIN", "HMF Q",      "",          "",         "HF FREQ",   "HF GAIN",  "HF TYPE" },
    { "DYNAMICS","COMP MIX", "COMP RATIO", "COMP THR",  "COMP REL", "COMP F.ATK","PEAK/RMS", "" },
    { "GATE REL","GATE THR", "GATE RANGE", "GATE HOLD", "GATE F.ATK","GATE/EXP", "HQ MODE",  "OUT TRIM" },
};
// Keys with a label but nothing Cubase can reach (A/B, HQ MODE: the plug-in's saved state in Rea-Sixty).
// The bus comp pages (Q2; Rea-Sixty's kBcLabels): two pages, the other four empty.
const char* const kBcSoftKeyLabels[6][8] = {
    { "THR",     "ATTACK",   "RELEASE",    "RATIO",     "S/C HPF",  "MIX",       "EXT S/C",  "BUS COMP" },
    { "OUTGAIN", "",         "",           "",          "",         "",          "",         "" },
    { "", "", "", "", "", "", "", "" }, { "", "", "", "", "", "", "", "" },
    { "", "", "", "", "", "", "", "" }, { "", "", "", "", "", "", "", "" },
};
std::atomic<int> g_skDomain{0};
// Nav Mode (NAV held): the top soft keys jump to Cubase's numbered markers (view 0, To Marker 1-8, script channel 2
// notes 10-17) or recall its cycle markers (view 1, Recall Cycle Marker 1-8, notes 18-25); Q1 = cycle, Q2 = markers.
// Cubase gives scripts no marker list, so the keys are by number, without names or colours.
std::atomic<bool> g_nav{false};
std::atomic<bool> g_noProject{false};   // Cubase shows the Hub: the bridge's own keys stay dark
std::atomic<std::chrono::steady_clock::time_point> g_uf8ConnectedAt{};   // the foot switches are ignored just after it
std::atomic<int> g_navView{0}, g_navLast{-1};   // 0 = markers (Q2), 1 = cycle markers (Q1)
// Track views (docs/track-views.md): which channel types the UF8 banks over. The script has one mapping page per
// view, switched by channel 2 notes 100 + view; it reports the active one (SysEx 14). Q3 opens the picker: the top
// soft keys are the views (the current one lit), a press switches and closes it, Q3 again closes it unchanged.
// Q3 is lit while a view other than ALL is on.
constexpr int kViewCount = 8;
constexpr const char* kViewNames[kViewCount] = { "ALL", "AUDIO", "INSTR", "MIDI", "GROUPS", "FX", "VCA", "ARMED" };
std::atomic<int> g_uf8View{0};
std::array<std::atomic<bool>, 8> g_dead{};   // ARMED: strips without an armed track (see armedBlank)
// The SOLO / CUT / SEL state Cubase last sent per strip, also while the strip was empty (not shown then): shown when a
// track's name arrives there (15.11.2026: back from ARMED, strip 8's SEL came before its name and was lost).
std::array<std::array<std::atomic<bool>, 3>, 8> g_ledWant{};
std::atomic<bool> g_viewPick{false};
// Settings (UF8 track views): the view at start (uf8-1-start-view: a view, or "last", kept in uf8-1-last-view.txt
// beside settings.txt) and the views the picker offers (uf8-1-view-<name>; ALL always). "1": this UF8; a second one
// gets keys of its own.
constexpr const char* kViewKeys[kViewCount] = { "all", "audio", "instr", "midi", "groups", "fx", "vca", "armed" };
bool viewOffered(int v)
{
    if (v <= 0) return true;
    if (v >= kViewCount) return false;
    return cs::settings::get(std::string("uf8-1-view-") + kViewKeys[v], "on") != "off";
}
int viewByKey(const std::string& k)
{
    for (int v = 0; v < kViewCount; ++v) if (k == kViewKeys[v]) return v;
    return -1;
}
void saveLastView(int v)
{
    static int saved = -1;
    if (v == saved || v < 0 || v >= kViewCount) return;
    saved = v;
    std::ofstream out(cs::settings::dir() / "uf8-1-last-view.txt", std::ios::trunc);
    out << kViewKeys[v] << "\n";
}
int startView()
{
    std::string k = cs::settings::get("uf8-1-start-view", "all");
    if (k == "last") {
        std::ifstream in(cs::settings::dir() / "uf8-1-last-view.txt");
        k.clear();
        in >> k;
    }
    const int v = viewByKey(k);
    return v < 0 ? 0 : v;
}
std::atomic<int> g_viewPickGen{0};
void switchView(int v);
std::mutex g_numMx;
// The filtered views' (1-6) numbers by position in the view's list (helper SysEx 1F), and which positions hold a
// channel (helper 1E, passed on to the UF8 script): the picker dims a view with none. Both under g_numMx.
std::vector<int> g_viewNumber[kViewCount];
std::array<int, 8> g_armedAll{-1, -1, -1, -1, -1, -1, -1, -1};   // ARMED: each strip's track in the full list (SysEx 32)
int g_armedCount = -1;   // how many tracks are armed (SysEx 33; -1 not known): the picker darkens ARMED with none
std::vector<bool> g_viewHas[kViewCount];   // (ARMED, 7: none, the script lays its strips out itself)
int viewChannels(int v)   // (g_numMx held)
{
    int n = 0;
    for (bool b : g_viewHas[v]) n += b;
    return n;
}
const char* skLabel(int domain, int bank, int slot) { return domain ? kBcSoftKeyLabels[bank][slot] : kSoftKeyLabels[bank][slot]; }
// A focus key: domain * 48 + bank * 8 + slot.
const char* skLabelOfKey(int key) { return skLabel(key / 48, key % 48 / 8, key % 8); }
bool softKeyLive(int domain, int bank, int slot) { const std::string l = skLabel(domain, bank, slot); return !l.empty() && l != "A/B" && l != "HQ MODE"; }
std::atomic<int> g_skBank{0};
std::atomic<int> g_focus{-1};   // bank * 8 + slot of the focused parameter, -1 = none (V-pots on pan)
// Pan as the script last sent it (shown again when the focus ends), and the focused parameter per strip.
std::array<uint16_t, 8> g_panPos{};
// The V-pot send view: each V-pot's send level (0..1, from the script's 0E; -1 = not known) and until when the bridge
// draws the ring itself. A turn moves the ring at once from here (the way round through Cubase and the script lagged,
// 12.11.2026); the script's reports are ignored for 0.5 s after a turn and its settled value taken after that.
std::array<std::atomic<double>, 8> g_selSendVal{};
std::array<std::atomic<long long>, 8> g_selSendOwnUntil{};
std::array<std::atomic<bool>, 8> g_selSendOwnText{};   // the bridge drew the value text of the turn in flight (the curve knew it)
std::array<std::atomic<bool>, 8> g_selSendHas{};   // the V-pot has a send (the script sent a level text for it)
std::array<std::string, 8> g_selSendText;
// With --trace (made 12.11.2026 for "the value sometimes stops updating"): per V-pot, one line a second while it turns
// or the script answers: turns taken / refused (no send known), how the text was drawn, what the script sent.
struct SelSendLog { std::atomic<int> turns{0}, refused{0}, curve{0}, gap{0}, text{0}, textHidden{0}, pos{0}, posIgnored{0}, last14{0}; };
std::array<SelSendLog, 8> g_selSendLog;
void selSendLogFlush()
{
    for (int s = 0; s < 8; ++s) {
        SelSendLog& l = g_selSendLog[size_t(s)];
        const int turns = l.turns.exchange(0), refused = l.refused.exchange(0);
        if (!turns && !refused && !l.text && !l.pos) continue;   // (the script's answers count in a quiet second too)
        if (!g_trace8) { l.curve = 0; l.gap = 0; l.text = 0; l.textHidden = 0; l.pos = 0; l.posIgnored = 0; continue; }
        std::printf("UF8 send V-pot %d: %d turns (last level %d of 16383), %d refused (no send known); text from the curve %d, gap %d; "
                    "script sent text %d (hidden %d), position %d (ignored %d)\n", s + 1, turns, l.last14.load(), refused,
                    l.curve.exchange(0), l.gap.exchange(0), l.text.exchange(0), l.textHidden.exchange(0), l.pos.exchange(0), l.posIgnored.exchange(0));
    }
}   // each V-pot's last level text from the script (MIDI thread only)
std::string g_panLine[8];
std::array<uint16_t, 8> g_focusPos{};
std::array<uint8_t, 8> g_focusMode{};
// The focused parameter's ring frames last sent (they carry all eight strips): only a change goes out.
// paintVpots, which draws pan or a new focus, forgets them.
std::array<uint16_t, 8> g_ringPosShown{};
std::array<uint8_t, 8> g_ringModeShown{};
void paintSoftKeys();
void paintVpots();

// The CHANNEL encoder's modes (Rea-Sixty's Channel Encoder modes, the first few): each detent is a Cubase command
// (MIDI channel 2, notes 0-5, or the Page notes for Bank by 1). Picked as in Rea-Sixty: hold the encoder's push
// (id 0x76) and turn; the scribble strips list the modes, the current one on strip 4 with its top soft key lit;
// letting go keeps the mode. NAV (0x73) and NUDGE (0x74) switch to Markers / Nudge directly and back (lit while on).
// Mouse (FOCUS, 13.11.2026): the value under Cubase's mouse pointer (as Steinberg's AI knob; Rea-Sixty: the last
// touched parameter), stepped by the UF8 script (SysEx 2D).
enum { kEncSelect = 0, kEncNudge, kEncMarkers, kEncBank, kEncCsFav, kEncBcFav, kEncMouse, kEncModes };
const char* const kEncNames[kEncModes] = { "Ch Sel", "Nudge", "Markers", "Bank 1", "CS Fav", "BC Fav", "Mouse" };
std::atomic<int> g_encMode{kEncSelect};
std::atomic<bool> g_encHeld{false};
int g_encPick = 0;
void paintEncPicker();
void paintEncLeds();
void restoreStripNames();
void setLed(int strip, int cls, bool on);
void sendLedFrames(const uf8::LedColourFrames& frames);
std::atomic<bool> g_uf8Running{false};   // the UF8 and its ports are open
std::array<uint8_t, 16> g_meterLevels{}, g_meterShown{};   // UF8 VU bytes, [s0 in, s0 out, s1 in, ...]
auto g_meterLastPush = std::chrono::steady_clock::now();
bool g_rightSeen[8] = {};              // a right-side meter value arrived for the strip's current track
bool g_stripEmpty[8] = {};             // strip shows no track (empty name): meter and dB stay blank
std::string g_dbText[8];               // last fader-level text per strip, shown again when a track returns
   // the UF8 and its ports are open
std::atomic<long long> g_cubaseFirstMs{0};   // when Cubase first sent anything (steady clock, ms)
std::atomic<bool> g_cubaseSpoke{false};
std::atomic<bool> g_projectOpen{false};   // the scripts said a project is open (7C 01); not before the first one
bool g_ownText = false;   // the bridge feeds its own text through onMidiIn ("Waiting for Cubase"): not Cubase speaking

// Bank-right by the user (UF8 key or UF1 BANK): stop at the bank where the last track is on strip 8,
// instead of a bank with empty strips at the end. The script reports the bank's track count and position
// (SysEx 12). Near the end, the bridge sends the needed single-track shifts (the Page-right note) instead
// of a bank step; on the last full bank it does nothing. Unknown count or position: a normal bank step.
std::atomic<int> g_bankTotal{-1}, g_bankOffset{-1};
std::atomic<int> g_selStrip{0};   // the UF8 strip (1-8) holding the selected track, 0 = none (from Cubase's SEL LEDs)
void note(uint8_t n, bool on);
void pageRightBurst(int count, int target);
// Returns true when it handled the press (the caller must then not send the bank note).
bool userBankRight(bool pressed)
{
    const int total = g_bankTotal, offset = g_bankOffset;
    if (total < 0 || offset < 0) return false;
    const int remaining = total - (offset + 8);   // tracks after the last strip
    if (remaining >= 8) return false;             // a full bank follows: the normal step
    if (!pressed) return false;                   // releases pass (a stray release does no harm)
    if (remaining <= 0) return true;              // the last track is already on a strip: stay
    pageRightBurst(remaining, total - 8);
    return true;
}   // set once Cubase has sent us a name or colour

long long traceMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() % 100000; }
void sendMidi(std::vector<unsigned char> m)
{
    if (g_trace8 && !(m.size() && (m[0] & 0xF0) == 0xE0)) {
        std::printf("%lld [UF8 MIDI out]", traceMs());
        for (unsigned char b : m) std::printf(" %02X", b);
        std::printf("\n");
    }
    std::lock_guard<std::mutex> lk(g_outMx);
    try { g_out.send(m); } catch (const RtMidiError& e) { std::fprintf(stderr, "midi out: %s\n", e.getMessage().c_str()); }
}
void note(uint8_t n, bool on) { sendMidi({uint8_t(on ? 0x90 : 0x80), n, uint8_t(on ? 127 : 0)}); }

// ---------------------------------------------------------------- UF8 -> Cubase

// Send views (Rea-Sixty's routing views). The script switches its fader and V-pot sub-pages on channel 2 notes:
// faders 80 = volume, 81 = pan (FLIP), 82-89 = send 1-8 of each track; V-pots 90 = pan, 91 = volume (FLIP),
// 92 = sends 1-8 of the selected track. SEND / PLUGIN 1-8 light for the fader send, FLIP for the flip.
std::atomic<int> g_routeSend{-1};
// SSL Strip Mode (the PLUGIN key): per strip, whether its fader is on the track's SSL channel strip fader (SysEx 26
// from the script). Those strips send the hand to the script (25) and ignore Cubase's volume; g_volPb keeps it.
std::atomic<bool> g_stripMode{false};
std::array<std::atomic<bool>, 8> g_stripFader{};
std::array<std::atomic<bool>, 8> g_stripPan{};   // the V-pot on the strip's pan (turns as 28, push as 29)
std::array<std::atomic<int>, 8> g_volPb{-1, -1, -1, -1, -1, -1, -1, -1};
std::array<std::atomic<int>, 8> g_handHw{};   // the hand's last fader position, in the motor's units
// The faders on the UF8's printed dB scale (15.11.2026; before, Cubase's position went straight onto the motor and
// -14 dB sat near the "30" mark). A level (track volume, a send) goes Cubase position -> dB (Cubase's curve,
// cs::levelDbOf) -> motor position (the marks below, measured on the hardware: each mark's position when a fader was
// set on it), and the hand back the same way. Pan (FLIP) and an SSL strip's fader (Strip Mode) stay linear. Rea-Sixty
// does the same with REAPER's slider law and a calibration table (pbToLinearVolume).
struct Uf8Mark { double db; int pb; };
constexpr Uf8Mark kUf8Marks[] = {
    {12, 15583}, {6, 14108}, {0, 12593}, {-5, 10961}, {-10, 9380}, {-20, 7168}, {-30, 5487}, {-40, 3585}, {-60, 1922},
};
constexpr double kUf8BottomDb = -100.0;   // below -60 straight on to the bottom (the scale's "infinity")
int dbToMotor(double db)
{
    if (db >= kUf8Marks[0].db) return kUf8Marks[0].pb;
    for (size_t i = 1; i < std::size(kUf8Marks); ++i) {
        if (db < kUf8Marks[i].db) continue;
        const auto& a = kUf8Marks[i - 1];
        const auto& b = kUf8Marks[i];
        return int(std::lround(b.pb + (a.pb - b.pb) * (db - b.db) / (a.db - b.db)));
    }
    const auto& last = kUf8Marks[std::size(kUf8Marks) - 1];
    if (db <= kUf8BottomDb) return 0;
    return int(std::lround(last.pb * (db - kUf8BottomDb) / (last.db - kUf8BottomDb)));
}
double motorToDb(int pb)
{
    if (pb <= 0) return -200.0;
    if (pb >= kUf8Marks[0].pb) return kUf8Marks[0].db;
    for (size_t i = 1; i < std::size(kUf8Marks); ++i) {
        if (pb < kUf8Marks[i].pb) continue;
        const auto& a = kUf8Marks[i - 1];
        const auto& b = kUf8Marks[i];
        return b.db + (a.db - b.db) * double(pb - b.pb) / double(a.pb - b.pb);
    }
    const auto& last = kUf8Marks[std::size(kUf8Marks) - 1];
    return kUf8BottomDb + (last.db - kUf8BottomDb) * double(pb) / double(last.pb);
}
// db: v14 is a level (Cubase's curve), placed by dB; else linear.
void driveFader(int s, uint16_t v14, bool db = false)
{
    const uint16_t pb = db ? uint16_t(dbToMotor(cs::levelDbOf(v14))) : midi14ToPb(v14);
    std::vector<std::vector<uint8_t>> frames;
    { std::lock_guard<std::mutex> lk(g_faderMx); frames = uf8fader::drive(uint8_t(s), pb, g_engaged[s]); }
    for (auto& f : frames) g_dev.send(std::move(f));
}
std::atomic<bool> g_routeSelSends{false}, g_routeFlip{false};
long long nowMs8() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
// The fader send view's dB field while the hand moves a fader: drawn at once from the send level curve (as the V-pot
// send view's value line), and the script's text (0C, which trails) held back until 0.5 s after the hand; the
// last one held is shown then (15.11.2026: the value followed the fader late).
std::array<std::atomic<long long>, 8> g_dbOwnUntil{};
std::array<std::atomic<int>, 8> g_dbHeldGen{};
std::mutex g_dbDrawnMx;
std::string g_dbDrawn[8];   // the last text drawn from the curve (for the log: how far off it was)
int g_dbDrawnPos[8] = {};
// The hand on a fader that is the script's (SysEx 25: a send level or an SSL strip's fader, written through Direct
// Access): at most one every 60 ms, the last one always, as the V-pot send view's 2A (control-feel.md rule 8). Sent
// with every position report (50+ a second), Cubase fell behind by seconds: the text that came back when the hand
// stopped was a level from far back, and corrected itself again later (15.11.2026, "jumps 4 dB and more").
std::array<std::atomic<int>, 8> g_handPos{};          // the hand's last position on a fader that is the script's
std::array<std::atomic<long long>, 8> g_handAt{};
void sendHandToScript(uint8_t s, uint16_t v)
{
    g_handPos[s] = v;
    g_handAt[s] = nowMs8();
    static std::array<std::atomic<long long>, 8> lastTx{};
    static std::array<std::atomic<int>, 8> pending{};   // position + 1 waiting to go, 0 = none
    const auto tx = [](uint8_t strip, int pos) {
        sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x25, strip, uint8_t((pos >> 7) & 0x7F), uint8_t(pos & 0x7F), 0xF7});
    };
    const long long now = nowMs8(), since = now - lastTx[s];
    if (since >= 60 && pending[s] == 0) { lastTx[s] = now; tx(s, v); return; }
    if (pending[s].exchange(v + 1) == 0) {
        std::thread([s, since, tx] {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max<long long>(1, 60 - since)));
            lastTx[s] = nowMs8();
            const int p = pending[s].exchange(0);
            if (p > 0) tx(s, p - 1);
        }).detach();
    }
}
// Cubase's text not what the bridge drew for where the hand stopped (more than 0.06 dB off): a late one, from a level
// the hand has passed. Not shown (the drawn one stays); one that agrees, or anything once 3 s have passed, is. (1.5 dB
// let the last few late ones through, a few tenths each, 15.11.2026; the drawn value is within the curve's error, a
// few hundredths where it has points, and the settled pair makes that spot exact for next time.)
bool lateDbText(uint8_t s, const std::string& cubase)
{
    std::string drawn;
    { std::lock_guard<std::mutex> lk(g_dbDrawnMx); drawn = g_dbDrawn[s]; }
    if (drawn.empty() || drawn == "(gap)" || cubase.empty()) return false;
    char* e1 = nullptr; char* e2 = nullptr;
    const double a = std::strtod(drawn.c_str(), &e1), b = std::strtod(cubase.c_str(), &e2);
    if (e1 == drawn.c_str() || e2 == cubase.c_str()) return false;
    // Where the curve is a fit (below about -18 dB) the drawn value can be a few tenths off: Cubase's own is taken
    // there when it is within 1 dB.
    return std::fabs(a - b) > (a < -18.0 ? 1.0 : 0.06);
}
// A level as the dB field shows it (Cubase's way: two decimals under 10 dB, one above; "-oo" for silence).
std::string levelFieldText(double db)
{
    if (db <= -199.0) return "-oo";
    char buf[16];
    std::snprintf(buf, sizeof buf, std::fabs(db) < 10.0 ? "%.2f" : "%.1f", db);
    return buf;
}
std::string dbFieldText(std::string t)
{
    if (const size_t at = t.rfind("dB"); at != std::string::npos) t.erase(at);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    if (t.size() > 6) t.resize(6);
    return t;
}
void note2(uint8_t n) { sendMidi({0x91, n, 127}); sendMidi({0x81, n, 0}); }
// FINE: the V-pots move a quarter as far while it is held; a double press latches it (a press unlatches).
// PAN: the V-pots back on pan (out of a focused soft-key parameter, the V-pot send view and FLIP); lit while they are.
std::atomic<bool> g_fineHeld{false}, g_fineLatched{false};
bool fineOn() { return g_fineHeld || g_fineLatched; }
void paintPanFine()
{
    if (g_noProject) return;
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Pan, g_focus < 0 && !g_routeSelSends && !g_routeFlip));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Fine, fineOn()));
}
void unfocus()
{
    g_focus = -1;
    sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x21, 0x7F, 0x7F, 0xF7});
    paintSoftKeys();
    paintVpots();
    paintPanFine();
}
void applyRoute();
// Another track view: the script's page for it, then the route again 60 ms later (a new page starts on volume / pan).
void switchView(int v)
{
    if (v < 0 || v >= kViewCount) return;
    const bool same = g_uf8View.exchange(v) == v;
    std::printf("UF8 view: %s%s\n", kViewNames[v], same ? " (already on)" : "");
    // The last view picked, for "the last view used" at start (not the script's own reports: a project opening on ALL
    // would overwrite it before the start view is read).
    saveLastView(v);
    if (same) return;
    // Leaving ARMED: its dead strips live again at once (Cubase sends the new page's LEDs before the script reports
    // the view: Audio FX's SEL was dropped, 15.11.2026).
    if (v != 7) for (auto& d : g_dead) d = false;
    note2(uint8_t(100 + v));
    std::thread([] { std::this_thread::sleep_for(std::chrono::milliseconds(60)); applyRoute(); }).detach();
}
void applyRoute()
{
    const int send = g_routeSend;
    const bool flip = g_routeFlip, sel = g_routeSelSends;
    // 40 ms apart: Cubase now and then lost the second of two back-to-back notes (the V-pots kept the old route).
    const uint8_t faderNote = uint8_t(send >= 0 ? 82 + send : flip ? 81 : 80), vpotNote = uint8_t(sel ? 92 : flip ? 91 : 90);
    note2(faderNote);
    std::thread([vpotNote] { std::this_thread::sleep_for(std::chrono::milliseconds(40)); note2(vpotNote); }).detach();
    static const uf8::Uf8GlobalLed sp[8] = { uf8::Uf8GlobalLed::SendPlugin1, uf8::Uf8GlobalLed::SendPlugin2, uf8::Uf8GlobalLed::SendPlugin3,
        uf8::Uf8GlobalLed::SendPlugin4, uf8::Uf8GlobalLed::SendPlugin5, uf8::Uf8GlobalLed::SendPlugin6, uf8::Uf8GlobalLed::SendPlugin7,
        uf8::Uf8GlobalLed::SendPlugin8 };
    for (int i = 0; i < 8; ++i) sendLedFrames(uf8::buildUf8GlobalLed(sp[i], i == send));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Flip, flip || sel));
    paintPanFine();
    if (g_trace8) std::printf("UF8 route: faders %s, V-pots %s\n", send >= 0 ? ("send " + std::to_string(send + 1)).c_str() : flip ? "pan" : "volume",
                              sel ? "selected track's sends" : flip ? "volume" : "pan");
}

void handleFrame(const uf8frames::Frame& fr)
{
    const uint8_t* d = fr.b;
    // Sleep when idle: every hand counts as activity; the touch, turn or press that wakes the surfaces is dropped.
    // (A fader's position counts only while it is touched: the UF8 reports positions by itself too.)
    if (d[1] == 0x21) { if (d[3] < kStrips && g_touched[d[3]]) cs::sleep::activity(false, "UF8 fader"); }
    else if (d[1] == 0x20) { if (d[4] != 0 && cs::sleep::activity(true, "UF8 fader touch")) return; }
    else if (d[1] == 0x24) { if (cs::sleep::activity(true, "UF8 V-pot / encoder")) return; }
    else if (d[1] == 0x22) { if (cs::sleep::key(0, d[3], d[5] == 0x01, "UF8 key")) return; }
    // The ARMED view: a strip without an armed track still has a channel behind it (its one-channel zone stays where it
    // was, blanked): nothing from the hand reaches it.
    if (g_uf8View == 7) {
        int st = -1;
        if (d[1] == 0x21 || d[1] == 0x20 || d[1] == 0x24) st = d[3] < kStrips ? d[3] : -1;
        else if (d[1] == 0x22) {
            if (d[3] >= 0x08 && d[3] <= 0x0F) st = d[3] - 0x08;   // V-pot pushes
            else if (auto n = buttonToNote(d[3])) { if ((*n >= 8 && *n < 40) || (*n >= 48 && *n < 56)) st = *n % 8; }
        }
        if (st >= 0 && g_stripEmpty[st]) return;
    }
    switch (d[1]) {
    case 0x21: {   // fader position: FF 21 03 strip lsb msb ck
        const uint8_t s = d[3];
        if (s >= kStrips || !g_touched[s]) return;   // only a hand moves a fader to Cubase
        const uint16_t pb14 = uint16_t((d[4] & 0x7F) | ((d[5] & 0x7F) << 7));
        const auto r = uf8fader::read(pb14, (d[4] & 0x80) != 0, /*grabValid=*/false);
        if (!r.usable) return;
        // A level (volume, or a send in a fader send view) by dB; pan (FLIP) and an SSL strip's fader linear.
        const bool dbScale = g_routeSend >= 0 || (!g_stripFader[s] && !g_routeFlip);
        const uint16_t v = dbScale ? uint16_t(std::clamp(cs::levelPosOf(motorToDb(r.pbOut)), 0, 16383)) : pbToMidi14(r.pbOut);
        g_handHw[s] = r.pbOut;   // (the motor's own units: the fader-scale calibration logs it on release)
        // Strip mode, and always in a fader send view (the fader's binding there is a placeholder on the send's on switch).
        if (g_stripFader[s] || g_routeSend >= 0) sendHandToScript(s, v);
        else sendMidi({uint8_t(0xE0 | s), uint8_t(v & 0x7F), uint8_t((v >> 7) & 0x7F)});
        if (g_routeSend >= 0 && !g_stripEmpty[s]) {   // the dB field at once, from the send level curve
            // From the whole curve (built-in points, a fit below the lowest): the learnt one alone had a gap below about
            // -18 dB, where nothing was drawn and Cubase's late text showed (15.11.2026).
            const std::string t = levelFieldText(cs::levelDbOf(int(v)));
            if (!t.empty()) {
                g_dbOwnUntil[s] = nowMs8() + 500;
                g_dev.send(uf8::buildFaderDbReadout(s, t, "dB"));
                std::lock_guard<std::mutex> lk(g_dbDrawnMx);
                g_dbDrawn[s] = t;
                g_dbDrawnPos[s] = v;
            } else {
                std::lock_guard<std::mutex> lk(g_dbDrawnMx);
                g_dbDrawn[s] = "(gap)";
                g_dbDrawnPos[s] = v;
            }
        }
        // Echo the hand position so the motor does not jump back on release.
        g_dev.send(uf8fader::echo(s, r.pbOut));
        break;
    }
    case 0x20: {   // fader touch: FF 20 02 strip state ck
        const uint8_t s = d[3];
        if (s >= kStrips) return;
        const bool down = d[4] != 0;
        // (Trace: where a fader was let go, in the motor's units 0-15583, and as dB on the printed scale. The marks in
        // kUf8Marks were measured with this.)
        if (!down && g_touched[s] && g_trace8)
            std::printf("UF8 fader %d let go at %d (motor units, top 15583) = %.1f dB on the scale\n", s + 1, g_handHw[s].load(), motorToDb(g_handHw[s]));
        g_touched[s] = down;
        if (down) {
            { std::lock_guard<std::mutex> lk(g_faderMx); g_engaged[s] = false; }
            g_dev.sendPriority(uf8fader::limp(s));
        }
        note(uint8_t(kNoteTouch + s), down);
        // Settings: touching a fader selects its track (a SEL press, 40 ms after the touch note: notes back to back
        // can merge).
        if (down && cs::settings::get("touch-selects", "off") == "on")
            std::thread([s] {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                note(uint8_t(kNoteSel + s), true);
                note(uint8_t(kNoteSel + s), false);
            }).detach();
        break;
    }
    case 0x22: {   // button: FF 22 03 id 00 state ck
        const bool pressed = d[5] == 0x01;
        if (d[3] == 0x54) {   // FLIP: a tap swaps faders and V-pots; held 0.4 s: the selected track's sends on the V-pots
            static std::atomic<int> gen{0};
            if (pressed) {
                const int g = ++gen;
                std::thread([g] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                    if (g != gen) return;
                    gen = -1;   // the release does nothing more
                    g_routeSelSends = !g_routeSelSends;
                    if (g_routeSelSends && g_focus >= 0) unfocus();
                    applyRoute();
                }).detach();
            } else if (gen > 0) {
                ++gen;   // a tap
                g_routeFlip = !g_routeFlip;
                applyRoute();
            }
            break;
        }
        if (d[3] >= 0x48 && d[3] <= 0x4F) {   // SEND / PLUGIN 1-8: send N of every track on the faders (again: off)
            if (pressed) {
                const int n = d[3] - 0x48;
                g_routeSend = g_routeSend == n ? -1 : n;
                applyRoute();
            }
            break;
        }
        if (d[3] == 0x50) {   // PLUGIN: SSL Strip Mode on / off (the faders on the SSL channel strips' faders)
            if (pressed) {
                g_stripMode = !g_stripMode;
                sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x24, uint8_t(g_stripMode ? 1 : 0), 0xF7});
                sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Plugin, g_stripMode));
                if (g_trace8) std::printf("UF8 SSL Strip Mode %s\n", g_stripMode ? "on" : "off");
            }
            break;
        }
        if (d[3] == 0x6E) {   // PAN: the V-pots back on pan
            if (pressed) {
                if (g_focus >= 0) unfocus();
                if (g_routeSelSends || g_routeFlip) { g_routeSelSends = false; g_routeFlip = false; applyRoute(); }
                paintPanFine();
                std::printf("UF8 PAN: V-pots on pan\n");
            }
            break;
        }
        if (d[3] == 0x6F) {   // FINE: held = fine V-pot steps; pressed twice within 0.4 s = latched; a press unlatches
            static long long lastDown = 0;
            if (pressed) {
                const long long now = nowMs8();
                if (g_fineLatched) { g_fineLatched = false; lastDown = 0; }
                else { g_fineHeld = true; if (now - lastDown < 400) g_fineLatched = true; lastDown = now; }
            } else g_fineHeld = false;
            paintPanFine();
            if (pressed || !g_fineLatched) std::printf("UF8 FINE %s\n", g_fineLatched ? "latched" : fineOn() ? "held" : "off");
            break;
        }
        if (d[3] == 0x46) {   // 360: the Settings page in the browser
            if (pressed) std::printf("UF8 360: Settings page %s\n", cs::settingsServer::openInBrowser() ? "opened" : "not available");
            break;
        }
        if (d[3] == 0x51) {   // CHANNEL (left panel): home, the strips back on track volume and pan
            if (pressed) { g_routeSend = -1; g_routeSelSends = false; g_routeFlip = false; applyRoute(); }
            break;
        }
        if (d[3] == 0x76) {   // CHANNEL encoder push: held = pick a mode
            if (pressed) { g_encHeld = true; g_encPick = g_encMode; paintEncPicker(); }
            else if (g_encHeld) {
                g_encHeld = false;
                g_encMode = g_encPick;
                if (g_trace8) std::printf("UF8 encoder mode: %s\n", kEncNames[g_encPick]);
                restoreStripNames();
                paintSoftKeys();
                paintEncLeds();
            }
            break;
        }
        if (d[3] == 0x73) {   // NAV: a tap = encoder on Markers on / off; held 0.7 s = Nav Mode on / off
            // gen: one per press (a release or a new press makes the timer of the last press do nothing);
            // held: the timer of this press fired, so its release is not a tap.
            static std::atomic<int> gen{0};
            static std::atomic<bool> held{false};
            static std::atomic<bool> down{false};   // a release without its press (the UF8 sends one when it connects) is no tap
            if (pressed) {
                down = true;
                held = false;
                const int g = ++gen;
                std::thread([g] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(700));   // (0.4 s too short, 1 s too long)
                    if (g != gen) return;
                    held = true;
                    g_nav = !g_nav;
                    if (g_nav) g_viewPick = false;
                    if (g_nav && g_focus >= 0) unfocus();
                    std::printf("UF8 Nav Mode %s\n", g_nav ? "on" : "off");
                    paintSoftKeys();
                }).detach();
            } else {
                ++gen;   // the timer of this press does nothing now
                if (down.exchange(false) && !held) {   // a tap
                    g_encMode = g_encMode == kEncMarkers ? kEncSelect : kEncMarkers;
                    std::printf("UF8 NAV tap: encoder on %s\n", g_encMode == kEncMarkers ? "Markers" : "Ch Sel");
                    paintEncLeds();
                }
            }
            break;
        }
        if (d[3] == 0x75) {   // FOCUS: the encoder on the value under the mouse pointer, on and off
            if (pressed) {
                g_encMode = g_encMode == kEncMouse ? kEncSelect : kEncMouse;
                paintEncLeds();
                std::printf("UF8 FOCUS: encoder on %s\n", g_encMode == kEncMouse ? "the value under the mouse" : "Ch Sel");
            }
            break;
        }
        if (d[3] == 0x74) {   // NUDGE: Nudge on and off
            if (pressed) {
                g_encMode = g_encMode == kEncNudge ? kEncSelect : kEncNudge;
                paintEncLeds();
            }
            break;
        }
        if (d[3] == 0x43 || d[3] == 0x44) {   // Q1 / Q2: the soft keys on the channel strip / the bus comp (Nav Mode: cycle / markers)
            if (pressed) {
                g_viewPick = false;
                if (g_nav) g_navView = d[3] == 0x43 ? 1 : 0;
                else g_skDomain = d[3] == 0x44 ? 1 : 0;
                paintSoftKeys();
            }
            break;
        }
        if (d[3] == 0x45) {   // Q3: the track view picker on the top soft keys, open / closed
            if (pressed) {
                ++g_viewPickGen;
                g_viewPick = !g_viewPick;
                if (g_viewPick) g_nav = false;   // (Nav Mode has the same keys)
                paintSoftKeys();
            }
            break;
        }
        if (d[3] >= 0x18 && d[3] <= 0x1F && g_viewPick) {   // the picker: a view; shown lit for a moment, then closed
            if (!pressed) break;
            const int v = d[3] - 0x18;
            if (v >= kViewCount || (!viewOffered(v) && v != g_uf8View)) break;   // (a view the settings leave out)
            switchView(v);
            const int gen = ++g_viewPickGen;
            paintSoftKeys();
            std::thread([gen] {
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                if (gen != g_viewPickGen) return;
                g_viewPick = false;
                paintSoftKeys();
            }).detach();
            break;
        }
        if (d[3] >= 0x68 && d[3] <= 0x6D) {   // V-POT, 1-5: the soft-key page
            if (pressed) { g_viewPick = false; g_skBank = d[3] - 0x68; paintSoftKeys(); }
            break;
        }
        if (d[3] >= 0x18 && d[3] <= 0x1F && g_nav) {   // Nav Mode: jump to marker / recall cycle marker 1-8
            if (!pressed) break;
            const int slot = d[3] - 0x18;
            note2(uint8_t((g_navView ? 18 : 10) + slot));
            // A cycle marker only sets the locators: then the cursor to the left one (its start), 40 ms later
            // (back-to-back notes can be lost).
            if (g_navView) std::thread([] { std::this_thread::sleep_for(std::chrono::milliseconds(40)); note2(26); }).detach();
            g_navLast = g_navView * 8 + slot;
            paintSoftKeys();
            break;
        }
        if (d[3] >= 0x18 && d[3] <= 0x1F) {   // top soft key: focus / unfocus its parameter
            if (!pressed) break;
            const int slot = d[3] - 0x18, key = g_skDomain * 48 + g_skBank * 8 + slot;
            if (!softKeyLive(g_skDomain, g_skBank, slot)) break;
            const int f = g_focus == key ? -1 : key;
            if (f >= 0 && g_routeSelSends) { g_routeSelSends = false; applyRoute(); }   // the V-pots go to the parameter
            g_focus = f;
            if (f < 0) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x21, 0x7F, 0x7F, 0xF7});
            else sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x21, uint8_t(f % 48 / 8 + (f >= 48 ? 8 : 0)), uint8_t(f % 8), 0xF7});   // bank + 8 = bus comp
            if (g_trace8) std::printf("UF8 soft key: %s\n", f < 0 ? "focus off (pan)" : skLabelOfKey(f));
            paintSoftKeys();
            paintVpots();
            paintPanFine();
            break;
        }
        // Strip mode: centre the strip's pan; the V-pot send view: the selected track's send on / off (always the script's
        // way there: the V-pot's binding is a placeholder on the send's on switch).
        if (d[3] >= 0x08 && d[3] <= 0x0F && g_focus < 0 && (g_stripPan[d[3] - 0x08] || (g_routeSelSends && g_routeSend < 0))) {
            if (pressed) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x29, uint8_t(d[3] - 0x08), 0xF7});
            break;
        }
        // On the track's own pan: centre it (the setting "V-pot push"; as Rea-Sixty) or, as before 13.11.2026 and
        // always under FLIP, the channel editor (the note below).
        if (d[3] >= 0x08 && d[3] <= 0x0F && g_focus < 0 && !g_routeFlip && cs::settings::get("uf8-vpot-push", "centre") == "centre") {
            if (pressed) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x2B, uint8_t(d[3] - 0x08), 0xF7});
            break;
        }
        if (d[3] >= 0x08 && d[3] <= 0x0F && g_focus >= 0) {   // V-pot push with a focused parameter: to the script
            if (pressed) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x23, uint8_t(d[3] - 0x08), 0xF7});
            break;
        }
        if (d[3] >= 0x70 && d[3] <= 0x72) {   // NORM / REC / AUTO: the selection mode
            if (pressed) {
                g_selMode = d[3] - 0x70;
                paintSelMode();
            paintSoftKeys();
            paintEncLeds();
                for (int i = 0; i < kStrips; ++i) paintSel(i);
                if (g_trace8) std::printf("UF8 selection mode %s\n", g_selMode == kSelNorm ? "NORM" : g_selMode == kSelRec ? "REC" : "AUTO");
            }
            break;
        }
        if (d[3] >= 0x20 && d[3] <= 0x37 && (d[3] - 0x20) % 3 == 2 && g_selMode != kSelNorm) {
            // SEL in REC / AUTO: arm (the top soft key's note) or automation Write.
            const int s = (d[3] - 0x20) / 3;
            if (g_selMode == kSelRec) { if (!g_noRec[s]) note(uint8_t(kNoteTopSoft + s), pressed); }
            else note(uint8_t(kNoteAutoSel + s), pressed);
            break;
        }
        if (d[3] >= 0x20 && d[3] <= 0x37 && (d[3] - 0x20) % 3 == 2) {
            // SEL in NORM on the track that is selected already (its SEL lit): open / close its channel editor (the
            // V-pot push's note; 13.11.2026, since the V-pot push centres the pan). The release goes the press's way.
            static bool editor[8] = {};
            const int s = (d[3] - 0x20) / 3;
            if (pressed) editor[s] = g_selOn[s];
            // The setting "UF8 SEL on the selected track" = ssl (default): the script opens the track's first SSL
            // channel strip's window, or asks back (2C) for the channel editor when there is none.
            if (editor[s] && cs::settings::get("uf8-sel-open", "ssl") == "ssl") {
                if (pressed) { sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x2C, uint8_t(s), 0xF7}); std::printf("UF8 SEL on the selected track (strip %d): SSL strip window\n", s + 1); }
                else editor[s] = false;
                break;
            }
            if (editor[s]) {
                note(uint8_t(kNoteVPush + s), pressed);
                if (pressed) std::printf("UF8 SEL on the selected track (strip %d): channel editor\n", s + 1);
                if (!pressed) editor[s] = false;
                break;
            }
        }
        if (auto n = buttonToNote(d[3])) {
            // An unplugged foot-switch jack reports itself as "pressed" while the UF8 starts up,
            // which started the transport. Ignore the foot switches for the first seconds after each
            // connect (at start, and when the UF8 is plugged in again).
            const bool foot = (*n == 62 || *n == 63);
            if (foot && std::chrono::steady_clock::now() - g_uf8ConnectedAt.load() < std::chrono::seconds(4)) break;
            // ARMED: the bank / page keys page through the armed tracks, by the script (SysEx 30, kinds 5-8).
            if (g_uf8View == 7 && (*n == kNoteBankRight || *n == kNoteBankLeft || *n == 44 || *n == 45)) {
                if (pressed) {
                    const uint8_t kind = *n == kNoteBankRight ? 5 : *n == kNoteBankLeft ? 6 : *n == 45 ? 7 : 8;
                    sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x30, kind, 1, 0xF7});
                }
                break;
            }
            if (*n == kNoteBankRight && userBankRight(pressed)) break;
            note(*n, pressed);
            if (pressed && *n >= kNoteAutoOff && *n <= kNoteAutoTouch) cs::autoModePressed(*n);
        }
        break;
    }
    case 0x24: {   // v-pot: FF 24 02 strip raw ck (strip 8 = the CHANNEL encoder)
        const uint8_t s = d[3];
        if (s == 8) {
            // The encoder reports about four counts per notch (Rea-Sixty: kChannelEncoderScale = 4): one step per
            // four, the rest kept for the next frame (it starts again when the direction changes).
            static int acc = 0;
            const int raw = uf8frames::vpotDelta(d[4]);
            if (raw == 0) break;
            if ((acc > 0 && raw < 0) || (acc < 0 && raw > 0)) acc = 0;
            acc += raw;
            const int delta = acc / 4;
            acc -= delta * 4;
            if (delta == 0) break;
            if (g_encHeld) {   // picking a mode
                g_encPick = std::clamp(g_encPick + delta, 0, kEncModes - 1);
                paintEncPicker();
                break;
            }
            const bool right = delta > 0;
            const int mode = g_encMode;
            if (mode == kEncMouse) {
                // The script steps the value under the mouse in Cubase's steps: about 0.4 % of the range each (measured
                // 13.11.2026), so one a notch took 250 notches end to end. Four a notch (about 1.6 %, as the V-pots);
                // FINE: one.
                const int steps = delta * (fineOn() ? 1 : 4);
                sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x2D, uint8_t(steps > 0 ? 1 : 0), uint8_t(std::min(std::abs(steps), 32)), 0xF7});
                break;
            }
            for (int i = 0; i < std::min(std::abs(delta), 8); ++i) {
                if (mode == kEncBank) { note(right ? kNotePageRight : kNotePageLeft, true); note(right ? kNotePageRight : kNotePageLeft, false); continue; }
                if (mode == kEncCsFav || mode == kEncBcFav) { cs::uc1FavouriteStep(mode == kEncBcFav, right ? 1 : -1); continue; }
                if (mode == kEncSelect && cs::settings::trackStepping() == cs::settings::Stepping::MixConsole && cs::uf1StepTrack(right ? 1 : -1)) continue;
                // Select 0 / 1 (6 / 7 = Navigate Up / Down: only the tracks the Project window shows), Nudge 2 / 3, Markers 4 / 5
                const uint8_t n = mode == kEncSelect && cs::settings::projectStepping() ? uint8_t(right ? 7 : 6) : uint8_t(mode * 2 + (right ? 1 : 0));
                sendMidi({0x91, n, 127});
                sendMidi({0x81, n, 0});
            }
            break;
        }
        if (s >= kStrips) return;
        const int raw = uf8frames::vpotDelta(d[4]);
        if (raw == 0) break;
        // FINE: a quarter of the way. A send level (kept here) takes a quarter step; the others go on as whole steps,
        // one for every four (the rest kept for the next frame; it starts again when the direction changes).
        const bool fine = fineOn();
        const bool sendTurn = g_focus < 0 && g_routeSelSends && g_routeSend < 0;
        int delta = raw;
        if (fine && !sendTurn) {
            static int acc[8] = {};
            if ((acc[s] > 0 && raw < 0) || (acc[s] < 0 && raw > 0)) acc[s] = 0;
            acc[s] += raw;
            delta = acc[s] / 4;
            acc[s] -= delta * 4;
            if (delta == 0) break;
        }
        const double sendStep = raw / (fine ? 400.0 : 100.0);
        if (g_focus >= 0) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x22, s, uint8_t(std::clamp(64 + delta, 1, 127)), 0xF7});   // the focused parameter
        else if (g_stripPan[s] || (g_routeSelSends && g_routeSend < 0)) {   // strip mode's pan; a send (V-pot send view)
            const bool sends = g_routeSelSends && g_routeSend < 0;
            if (!sends) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x28, s, uint8_t(std::clamp(64 + delta, 1, 127)), 0xF7});
            if (sends && !g_selSendHas[s]) ++g_selSendLog[s].refused;
            if (sends && g_selSendHas[s]) {   // (an empty send slot: nothing to turn)
                ++g_selSendLog[s].turns;
                // The V-pot send view: the bridge keeps the level, shows it at once and sends the script the level
                // itself (2A <strip> <msb> <lsb>), not the step: counted on both sides, the two drifted apart and the
                // settled value came as a small correction (12.11.2026).
                const double v = std::clamp(g_selSendVal[s].load() + sendStep, 0.0, 1.0);
                const int v14 = int(std::lround(v * 16383.0));
                // At most one level every 60 ms, the last one always: at 30-40 a second Cubase gave the script 2 idle
                // ticks a second instead of 10 and the MixConsole trailed; up to about 20 it kept up (12.11.2026).
                {
                    static std::array<std::atomic<long long>, 8> lastTx{};
                    static std::array<std::atomic<int>, 8> pending{};   // level + 1 waiting to go, 0 = none
                    const auto tx = [](uint8_t strip, int level) {
                        sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x2A, strip, uint8_t((level >> 7) & 0x7F), uint8_t(level & 0x7F), 0xF7});
                    };
                    const long long now = nowMs8(), since = now - lastTx[s];
                    if (since >= 60 && pending[s] == 0) { lastTx[s] = now; tx(s, v14); }
                    else if (pending[s].exchange(v14 + 1) == 0) {
                        std::thread([s, since, tx] {
                            std::this_thread::sleep_for(std::chrono::milliseconds(std::max<long long>(1, 60 - since)));
                            lastTx[s] = nowMs8();
                            const int p = pending[s].exchange(0);
                            if (p > 0) tx(s, p - 1);
                        }).detach();
                    }
                }
                g_selSendVal[s] = v;
                g_selSendOwnUntil[s] = nowMs8() + 500;
                g_panPos[s] = uf8::vpotPosUnipolar(v);
                g_dev.send(uf8::buildVPotReadoutBar(g_panPos));
                // The value too, from the send level curve (Cubase's own text trails the ring; it follows once settled).
                std::string t = cs::uf1LevelText(int(std::lround(v * 16383.0)));
                g_selSendOwnText[s] = !t.empty();   // (a gap in the curve: Cubase's own text shows meanwhile)
                g_selSendLog[s].last14 = v14;
                ++(t.empty() ? g_selSendLog[s].gap : g_selSendLog[s].curve);
                if (!t.empty() && g_focus < 0) {
                    if (const size_t at = t.rfind("dB"); at != std::string::npos && at > 0 && t[at - 1] != ' ') t.insert(at, " ");
                    std::string line = "Send";
                    if (t.size() > 14) t.resize(14);
                    line.append(19 - line.size() - t.size(), ' ');
                    line += t;
                    g_dev.send(uf8::buildValueLine(s, line));
                }
            }
        }
        else sendMidi({0xB0, uint8_t(kCcVPot + s), vpotToCc(delta)});
        break;
    }
    default: break;
    }
}

void onUsbInput(const uint8_t* data, size_t len)
{
    std::vector<uf8frames::Frame> frames;
    uf8frames::feed(g_asm, data, len, frames, [](uf8frames::Skip, const uint8_t*, size_t) {});
    for (const auto& f : frames) handleFrame(f);
}

// ---------------------------------------------------------------- Cubase -> UF8

void sendLedFrames(const uf8::LedColourFrames& frames)
{
    g_dev.send(frames.ff38);
    if (!frames.ff39.empty()) g_dev.send(frames.ff39);
    if (!frames.legacy.empty()) g_dev.send(frames.legacy);
}

// cls: 0 solo, 1 cut, 2 sel, 3 top soft key, 4 automation Read, 5 automation Write
void setLed(int strip, int cls, bool on)
{
    switch (cls) {
    case 0: case 1:
        sendLedFrames(uf8::buildLedColourPair(uint8_t(strip), static_cast<uf8::LedClass>(cls), on));
        break;
    case 2:
        if (strip >= 0 && strip < 8) {
            g_selOn[strip] = on;
            paintSel(strip);
            // Which strips are selected, as SSL 360 and Rea-Sixty send it on every selection change (cap33). Sent since
            // 15.11.2026, for the UF1's look of the colour bar (the selected strip's number box dimmed).
            uint16_t mask = 0;
            for (int i = 0; i < 8; ++i) if (g_selOn[i]) mask |= uint16_t(1u << i);
            g_dev.send(uf8::buildSelectedStripBitmask(mask));
        }
        if (strip >= 0 && strip < 8) {
            // SEL: tell the UF1 which UF8 strip holds the selected track (its channel-number box shows it).
            static bool selOn[8] = {};
            selOn[strip] = on;
            int n = 0;
            for (int i = 0; i < 8; ++i) if (selOn[i]) { n = i + 1; break; }
            g_selStrip = n;
            cs::uf1SetStripNumber(n);
        }
        break;
    case 3:
        if (strip >= 0 && strip < 8) {
            g_recOn[strip] = on;
            if (g_trace8) std::printf("UF8 rec strip %d: Cubase says %s\n", strip + 1, on ? "ON" : "OFF");
        }
        // Record-enable (the top soft keys are the SSL soft keys now): shown on SEL in REC.
        if (strip >= 0 && strip < 8 && g_selMode == kSelRec) paintSel(strip);
        break;
    case 6:
        if (strip >= 0 && strip < 8) { g_autoW[strip] = on; if (g_selMode == kSelAuto) paintSel(strip); }
        break;
    case 4: sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::AutoRead, on)); break;
    case 5: sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::AutoWrite, on)); break;
    default: break;
    }
}

// REC / AUTO: solid colours (bright and dim bytes the same), as Rea-Sixty's ledColourFor: the firmware's selected-strip
// mask (FF 66, sent on every selection change since 15.11.2026) and the on flag then cannot change how they look. With
// a dim track colour and the on flag, REC's "can arm" showed as bright as armed outside the selected strip.
const uf8::LedColour kSelDark{0x00, 0xF0, 0x00, 0xF0};   // no colour (cap41 00 F0)
void paintSel(int strip)
{
    if (strip < 0 || strip >= kStrips) return;
    const int mode = g_selMode;
    if (mode == kSelNorm) { sendLedFrames(uf8::buildLedColourPair(uint8_t(strip), uf8::LedClass::Sel, g_selOn[strip])); return; }
    if (mode == kSelRec) {
        // As the top soft keys: dark where the track cannot be armed, dim red where it can, red when armed.
        const bool can = !g_noRec[strip] && !g_stripEmpty[strip];
        const bool armed = can && g_recOn[strip];
        sendLedFrames(uf8::buildLedColourPair(uint8_t(strip), uf8::LedClass::Sel, true,
            !can ? kSelDark : armed ? uf8::ledColourRedBrightSolid() : uf8::ledColourRedDimSolid()));
        return;
    }
    // AUTO: red with Write on, dim red without (as before: red's dim bytes), dark without a track.
    sendLedFrames(uf8::buildLedColourPair(uint8_t(strip), uf8::LedClass::Sel, true, g_stripEmpty[strip] ? kSelDark
        : g_autoW[strip] ? uf8::ledColourRedBrightSolid() : uf8::ledColourRedDimSolid()));
}
void paintSoftKeys()
{
    if (g_noProject) return;
    if (g_viewPick) {   // the track view picker: the views, the current one lit
        const int cur = g_uf8View;
        bool empty[kViewCount] = {};
        {
            // A view without channels (the helper's lists): its key dark (still a key: the list may not be in yet).
            std::lock_guard<std::mutex> lk(g_numMx);
            for (int v = 1; v < 7; ++v) empty[v] = !g_viewHas[v].empty() && viewChannels(v) == 0;   // (nothing heard: not known)
            empty[7] = g_armedCount == 0;
        }
        for (int s = 0; s < kStrips; ++s) {
            const bool offered = s < kViewCount && (viewOffered(s) || s == cur);
            sendLedFrames(uf8::buildTopSoftKeyLed(uint8_t(s), !offered ? uf8::TopSoftKeyState::Off
                : s == cur ? uf8::TopSoftKeyState::On : empty[s] ? uf8::TopSoftKeyState::Off : uf8::TopSoftKeyState::Dim, uf8::ledColourWhite()));
            g_dev.send(uf8::buildPluginSlotName(uint8_t(s), offered ? kViewNames[s] : ""));
        }
        sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Quick1, false));
        sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Quick2, false));
        sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Quick3, true));
        return;
    }
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Quick3, g_uf8View != 0));
    if (g_nav) {   // Nav Mode: the eight keys are markers / cycle markers 1-8, the last one pressed lit
        const int view = g_navView;
        for (int s = 0; s < kStrips; ++s) {
            sendLedFrames(uf8::buildTopSoftKeyLed(uint8_t(s), g_navLast == view * 8 + s ? uf8::TopSoftKeyState::On : uf8::TopSoftKeyState::Dim, uf8::ledColourWhite()));
            g_dev.send(uf8::buildPluginSlotName(uint8_t(s), (std::string(view ? "Cycle " : "Marker ") + char('1' + s)).c_str()));
        }
        sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Quick1, view == 1));
        sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Quick2, view == 0));
        return;
    }
    const int bank = g_skBank, focus = g_focus, dom = g_skDomain;
    for (int s = 0; s < kStrips; ++s) {
        const bool live = softKeyLive(dom, bank, s);
        sendLedFrames(uf8::buildTopSoftKeyLed(uint8_t(s), !live ? uf8::TopSoftKeyState::Off
            : focus == dom * 48 + bank * 8 + s ? uf8::TopSoftKeyState::On : uf8::TopSoftKeyState::Dim, uf8::ledColourWhite()));
        g_dev.send(uf8::buildPluginSlotName(uint8_t(s), skLabel(dom, bank, s)));
    }
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Quick1, dom == 0));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Quick2, dom == 1));
    static const uf8::Uf8GlobalLed kBankLeds[6] = { uf8::Uf8GlobalLed::VPotBank, uf8::Uf8GlobalLed::Soft1, uf8::Uf8GlobalLed::Soft2,
                                                     uf8::Uf8GlobalLed::Soft3, uf8::Uf8GlobalLed::Soft4, uf8::Uf8GlobalLed::Soft5 };
    for (int b = 0; b < 6; ++b) sendLedFrames(uf8::buildUf8GlobalLed(kBankLeds[b], b == bank));
}
// The V-pot rings and value lines: pan, or the focused parameter.
void paintVpots()
{
    const int focus = g_focus;
    g_ringPosShown.fill(0xFFFF);
    g_ringModeShown.fill(0xFF);
    std::array<uint8_t, kStrips> modes{};
    for (int s = 0; s < kStrips; ++s) modes[size_t(s)] = focus < 0 ? 1 : g_focusMode[size_t(s)];
    g_dev.send(uf8::buildVPotReadoutBar(focus < 0 ? g_panPos : g_focusPos));
    g_dev.send(uf8::buildVPotMode(modes));
    if (focus < 0) for (int s = 0; s < kStrips; ++s) { if (!g_panLine[s].empty()) g_dev.send(uf8::buildValueLine(uint8_t(s), g_panLine[s])); }
}
void paintEncPicker()
{
    for (int s = 0; s < kStrips; ++s) {
        const int i = g_encPick + (s - 3);
        g_dev.send(uf8::buildStripTextUpper(uint8_t(s), i >= 0 && i < kEncModes ? kEncNames[i] : ""));
        sendLedFrames(uf8::buildTopSoftKeyLed(uint8_t(s), s == 3 ? uf8::TopSoftKeyState::On : uf8::TopSoftKeyState::Off, uf8::ledColourWhite()));
    }
}
void paintEncLeds()
{
    if (g_noProject) return;
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Nav, g_encMode == kEncMarkers));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Nudge, g_encMode == kEncNudge));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Focus, g_encMode == kEncMouse));
    paintPanFine();
}
void paintSelMode()
{
    if (g_noProject) return;
    const int mode = g_selMode;
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Norm, mode == kSelNorm));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Rec, mode == kSelRec));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Auto, mode == kSelAuto));
}

void pushPalette()
{
    std::array<uint8_t, uf8::kStripCount> idx{};
    {
        std::lock_guard<std::mutex> lk(g_paletteMx);
        for (int i = 0; i < kStrips; ++i) idx[size_t(i)] = g_palette[i];
    }
    g_dev.send(uf8::buildColorCommand(idx));
}

// The latest message from Cubase per kind, replayed after the UF8 is reopened (Cubase only sends changes).
std::mutex g_uf8CacheMx;
std::map<int, std::vector<unsigned char>> g_uf8Cache;
// During a follow-selection jump the UF8 is frozen: what Cubase sends for the banks passed on the way is only
// cached, and the end state is shown once, so the jump looks direct instead of banking one by one.
std::atomic<bool> g_uf8Frozen{false};

// The track numbers (15.11.2026; Rea-Sixty shows them too): the small number at the top left of each strip's colour
// bar, as the UF1 shows the selected track's: counted as the MixConsole does, hidden tracks too. The Helper script
// gives each channel of the UF8's bank list (visible channels, in order) its number (SysEx 1A); the UF8 script says
// where the bank starts (12). Blank for an empty strip and with no project.
std::vector<int> g_chanNumber;   // by position in the bank list: its number (0 = not known)
std::string g_numShown[8];       // what each strip shows ("" = not drawn since the UF8 connected)
// The text cell in each strip's colour bar (15.11.2026), as Rea-Sixty and the UF1: the SSL plug-in on the strip's
// track ("CS 2", "4K E", "Link", "BC 2"), from what the plug-ins announce (their track = the number above). Written
// at all, the cell also switches the bar to the UF1's look (the part above the meters dimmed, the number in it):
// twelve spaces for a track without one, as the UF1; nothing for an empty strip (a plain bar, as Rea-Sixty).
std::string g_csShown[8] = {"\x01", "\x01", "\x01", "\x01", "\x01", "\x01", "\x01", "\x01"};
const char* stripPluginLabel(int type)
{
    switch (type) {
        case 1: return "CS 2";   // sslmeter::PluginType: NativeChannelStrip
        case 2: case 7: return "BC 2";   // NativeBusComp, GenericBusComp (the 360 Link bus compressor)
        case 3: return "4K B";
        case 5: return "4K E";
        case 6: return "Link";   // GenericChannelStrip (SSL 360 Link)
        case 9: return "4K G";
        default: return nullptr;
    }
}
void paintStripNumbers(bool force = false)
{
    if (!g_uf8Running || g_uf8Frozen) return;
    const int offset = g_bankOffset;
    std::lock_guard<std::mutex> lk(g_numMx);
    for (int s = 0; s < kStrips; ++s) {
        std::string t = "  ";
        int number = 0;
        const size_t i = size_t(offset + s);
        const int view = g_uf8View;
        const std::vector<int>& numbers = view > 0 && view < 7 ? g_viewNumber[view] : g_chanNumber;   // the view's list
        if (view == 7) {   // ARMED: by each strip's place in the full list, from the script
            const int a = g_armedAll[s];
            if (g_projectOpen && a >= 0 && !g_stripEmpty[s] && size_t(a) < g_chanNumber.size() && g_chanNumber[a] > 0)
                t = std::to_string(number = g_chanNumber[a]);
        } else if (g_projectOpen && offset >= 0 && !g_stripEmpty[s] && i < numbers.size() && numbers[i] > 0)
            t = std::to_string(number = numbers[i]);
        else if (g_projectOpen && offset < 0 && !g_numShown[s].empty() && !g_stripEmpty[s])
            continue;   // where the bank starts is not known for a moment: keep what is shown
        if (force || g_numShown[s] != t) {
            g_numShown[s] = t;
            g_dev.send(uf8::buildChannelNumber(uint8_t(s), t));
        }
        std::string cs;
        if (g_projectOpen && !g_stripEmpty[s]) {
            const char* label = number > 0 ? stripPluginLabel(sslcore::stripPluginTypeOnTrack(uf1SslTrackOf(number))) : nullptr;
            cs = label ? label : std::string(12, ' ');
        }
        if (force || g_csShown[s] != cs) {
            g_csShown[s] = cs;
            g_dev.send(uf8::buildChannelStripType(uint8_t(s), cs));
        }
    }
}
std::atomic<int> g_freezeGen{0};
std::atomic<bool> g_freezeHeld{false};
std::atomic<int> g_holdGen{0};   // the script holds the freeze (SysEx 13 01 ... 13 00): bursts leave it on
void replayUf8Cache();
// Bank-right near the end: page-rights until the last track is on strip 8. With more than one, the UF8 frozen like a
// jump so only the end shows (15.11.2026: each step moved the motors, heard as banking two or three times), and
// 100 ms apart (at 60 ms a press was now and then lost, and the bank stopped one short). The end is shown as soon as
// the script reports the bank there (SysEx 12: it does once all eight names are in), else after 400 ms.
void pageRightBurst(int count, int target)
{
    std::thread([count, target] {
        const int gen = count > 1 ? ++g_freezeGen : 0;
        if (count > 1) g_uf8Frozen = true;
        // The script moves the bank itself, all steps at once (SysEx 30 00 <count>: its shift-right action triggered,
        // there in ~0.2 s, measured 15.11.2026); the notes only if the bank has not got there within 400 ms.
        const int before = g_bankOffset;
        sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x30, 0x00, uint8_t(count), 0xF7});
        int waited = 0;
        for (; waited < 400 && g_bankOffset != target; waited += 10) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (g_bankOffset == target) std::printf("UF8 bank-right near the end: %d step(s) by the script, there after %d ms\n", count, waited);
        else {
            const int now = g_bankOffset;
            const int left = now >= before && now < target ? target - now : count;
            std::printf("UF8 bank-right near the end: the script's %d step(s) got the bank from %d to %d in 400 ms; %d note(s)\n", count, before, now, left);
            for (int i = 0; i < left; ++i) {
                if (i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                note(kNotePageRight, true);
                note(kNotePageRight, false);
            }
            for (int w = 0; w < 400 && g_bankOffset != target; w += 20) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (count > 1 && g_freezeGen == gen && !g_freezeHeld) { g_uf8Frozen = false; replayUf8Cache(); }
    }).detach();
}
void replayUf8Cache();
void onMidiIn(std::vector<unsigned char>& msg);
// No project (the script's driver deactivated): every strip as an empty one (name, colour, meter, dB, its LEDs),
// faders down, value lines and V-pot bars off, the LEDs Cubase drives off. What Cubase sends on the next project
// paints it all again.
std::map<int, std::vector<unsigned char>> g_npBefore, g_npBlanked;   // the cache before / right after the blanking
void uf8IdleText(int kind);
void uf8NoProject()
{
    const bool first = !g_noProject;
    if (first) { std::lock_guard<std::mutex> lk(g_uf8CacheMx); g_npBefore = g_uf8Cache; }
    g_uf8Frozen = false;
    std::vector<int> notes;
    {
        std::lock_guard<std::mutex> lk(g_uf8CacheMx);
        for (auto& kv : g_uf8Cache) if (kv.first < 0x10000) notes.push_back(kv.first);
    }
    for (int s = 0; s < kStrips; ++s) {
        std::vector<unsigned char> name{0xF0, 0x7D, 0x52, 0x36, 0x01, uint8_t(s), 0xF7};
        std::vector<unsigned char> colour{0xF0, 0x7D, 0x52, 0x36, 0x02, uint8_t(s), 0, 0, 0, 0xF7};
        // The faders go to 0 dB (13.11.2026; to the bottom all eight at once was loud, 10.11.2026, and then they stayed
        // where they were). Driven, not passed through onMidiIn: the cache keeps Cubase's values, which go back when
        // the project opens.
        if (const int z = cs::fader0dbPos(); z >= 0 && !g_touched[s]) driveFader(s, uint16_t(z), true);
        onMidiIn(name);
        onMidiIn(colour);
        g_panLine[s].clear();
        g_dev.send(uf8::buildValueLine(uint8_t(s), ""));
    }
    for (int n : notes) { std::vector<unsigned char> off{0x80, uint8_t(n), 0}; onMidiIn(off); }
    if (first) { std::lock_guard<std::mutex> lk(g_uf8CacheMx); g_npBlanked = g_uf8Cache; }
    g_dev.send(uf8::buildVPotMode(std::array<uint8_t, kStrips>{3, 3, 3, 3, 3, 3, 3, 3}));
    // The bridge's own keys: the soft keys' labels and LEDs, Q1 / Q2, the soft key pages, NORM / REC / AUTO,
    // NAV / NUDGE, PLUGIN. They stay dark until a project opens (the paint functions check g_noProject).
    g_noProject = true;
    for (int s = 0; s < kStrips; ++s) {
        sendLedFrames(uf8::buildTopSoftKeyLed(uint8_t(s), uf8::TopSoftKeyState::Off, uf8::ledColourWhite()));
        g_dev.send(uf8::buildPluginSlotName(uint8_t(s), ""));
    }
    using L = uf8::Uf8GlobalLed;
    for (L l : {L::Quick1, L::Quick2, L::VPotBank, L::Soft1, L::Soft2, L::Soft3, L::Soft4, L::Soft5,
                L::Norm, L::Rec, L::Auto, L::Nav, L::Nudge, L::Plugin, L::Pan, L::Fine, L::Focus})
        sendLedFrames(uf8::buildUf8GlobalLed(l, false));
    uf8IdleText(cs::cubaseRunning() && g_cubaseSpoke ? 1 : 2);
}
// What the strips say while no project is open (strips 1-3, straight to the displays, not through the cache, so the
// names come back when a project opens): 1 = "No Project" (Cubase shows the Hub), 2 = "Waiting for Cubase" (Cubase
// is not running), 3 = "Bridge off" (the bridge is leaving).
void uf8IdleText(int kind)
{
    static const char* const t[4][3] = {{"", "", ""}, {"No", "Project", ""}, {"Waiting", "for", "Cubase"}, {"Bridge", "off", ""}};
    if (kind < 0 || kind > 3) return;
    for (int s = 0; s < 3; ++s) g_dev.send(uf8::buildStripTextUpper(uint8_t(s), t[kind][s]));
}
// A project is open again: the bridge's own keys as they were.
// Cubase does not send again what has not changed when the project opens (the level, pan, LEDs): what was shown
// before the close is put back for every kind nothing new has arrived for since the blanking.
void uf8ProjectOpen()
{
    if (!g_noProject.exchange(false)) return;
    std::vector<std::vector<unsigned char>> again;
    {
        std::lock_guard<std::mutex> lk(g_uf8CacheMx);
        for (auto& kv : g_npBefore) {
            auto b = g_npBlanked.find(kv.first);
            auto c = g_uf8Cache.find(kv.first);
            if (b != g_npBlanked.end() && c != g_uf8Cache.end() && c->second == b->second) again.push_back(kv.second);
        }
    }
    for (auto& mm : again) onMidiIn(mm);
    paintSoftKeys();
    paintSelMode();
    paintEncLeds();
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Plugin, g_stripMode));
}

void uf8NoProject();
void uf8IdleText(int kind);
// ARMED: a strip without an armed track is dead. Its one-channel zone still has a channel behind it (where it was),
// so what Cubase sends for that strip is dropped, and the strip is blanked once: name, colour, dB, meter, value line,
// LEDs off, fader down (15.11.2026: with only the name blank, the first armed track's meter, CUT and fader showed).
int stripOfMsg(const std::vector<unsigned char>& m)
{
    if (m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x7D) {
        switch (m[4]) {
            case 0x01: case 0x02: case 0x06: case 0x07: case 0x0C: case 0x0D: case 0x0E: case 0x20: case 0x26: case 0x27:
                return m[5] < kStrips ? m[5] : -1;
            default: return -1;
        }
    }
    if (m.size() < 3) return -1;
    const uint8_t st = m[0] & 0xF0;
    if (st == 0xE0) return (m[0] & 0x0F) < kStrips ? (m[0] & 0x0F) : -1;
    if (st == 0xB0 && m[1] >= kCcMeter && m[1] < kCcMeter + 2 * kStrips) return (m[1] - kCcMeter) % kStrips;
    if ((st == 0x90 || st == 0x80 || st == 0xA0) && m[0] == st) {   // (MIDI channel 1: the strips' keys)
        const int n = m[1];
        if ((n >= 8 && n < 32) || (n >= 48 && n < 56) || (n >= 64 && n < 72) || (n >= 112 && n < 120)) return n % 8;
    }
    return -1;
}
void armedBlank(int s)
{
    std::vector<unsigned char> name{0xF0, 0x7D, 0x52, 0x36, 0x01, uint8_t(s), 0xF7};
    std::vector<unsigned char> colour{0xF0, 0x7D, 0x52, 0x36, 0x02, uint8_t(s), 0, 0, 0, 0xF7};
    std::vector<unsigned char> db{0xF0, 0x7D, 0x52, 0x36, 0x0C, uint8_t(s), 0xF7};
    std::vector<unsigned char> ml{0xB0, uint8_t(kCcMeter + s), 0}, mr{0xB0, uint8_t(kCcMeter + kStrips + s), 0};
    for (auto* v : {&name, &colour, &db, &ml, &mr}) onMidiIn(*v);
    g_panLine[s].clear();
    g_dev.send(uf8::buildValueLine(uint8_t(s), ""));
    for (int cls = 0; cls < 4; ++cls) setLed(s, cls, false);
    for (auto& w : g_ledWant[s]) w = false;
    if (!g_touched[s]) driveFader(s, 0);
}
void onMidiIn(std::vector<unsigned char>& msg)
{
    const auto& m = msg;
    if (m.empty()) return;
    if (cs::scriptLogSysex(m.data(), m.size())) return;
    if (const int ds = stripOfMsg(m); ds >= 0 && g_dead[ds]) return;   // ARMED: a strip without a track
    // The script's driver: 7C 00 = deactivated (Cubase closed the project and shows the Hub), 7C 01 = active again.
    if (m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x7D && m[4] == 0x7C) {
        g_projectOpen = m[5] != 0;
        if (m[5] == 0) {
            { std::lock_guard<std::mutex> lk(g_numMx); g_chanNumber.clear(); for (auto& v : g_viewNumber) v.clear(); for (auto& v : g_viewHas) v.clear(); }
            paintStripNumbers();
        }
        if (m[5] == 0) uf8NoProject();
        else {
            g_dev.send(uf8::buildVPotMode(std::array<uint8_t, kStrips>{1, 1, 1, 1, 1, 1, 1, 1}));
            uf8ProjectOpen();
            // The view at start (settings): at once to the script (SysEx 34 <view>), which activates that page itself
            // before its start-up follow banks (15.11.2026: on ALL first, it banked there and again in the start view
            // 3 s later). Sent for ALL too: Cubase opens the UF8 on the page it was last on (INSTR, with ALL set; seen
            // 15.11.2026), so the script switches whenever that is not the start view. The script takes it only in its
            // first 10 s after a project opened, so a bridge restart (its scripts announce themselves again) leaves the
            // view as it is.
            sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x34, uint8_t(startView()), 0xF7});
        }
        // (Always logged: the tray app shows it.)
        std::printf("Cubase: %s\n", m[5] ? "project open" : "no project (the Hub)");
        return;
    }
    // Trace: when each strip's name arrives (timing of bank changes).
    if (g_trace8 && m.size() >= 7 && m[0] == 0xF0 && m[4] == 0x01)
        std::printf("%lld UF8 name strip %d: %s\n", traceMs(), int(m[5]) + 1, std::string(m.begin() + 6, m.end() - 1).c_str());
    {
        // Names / colours / pan / levels by (command, strip); faders by channel; LED notes by note number.
        std::lock_guard<std::mutex> lk(g_uf8CacheMx);
        const uint8_t st = m[0] & 0xF0;
        if (m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x7D && (m[4] == 0x01 || m[4] == 0x02 || m[4] == 0x06 || m[4] == 0x07 || m[4] == 0x0C || m[4] == 0x0D || m[4] == 0x0E))
            g_uf8Cache[0x10000 + (m[4] << 8) + m[5]] = m;
        else if (st == 0xE0 && m.size() >= 3) g_uf8Cache[0x20000 + (m[0] & 0x0F)] = m;
        else if ((st == 0x90 || st == 0x80 || st == 0xA0) && m.size() >= 3) g_uf8Cache[m[1]] = m;
    }
    if (g_uf8Frozen) {
        // Frozen (jump running): drop the strip state (cached above, shown after the jump) and meters;
        // the script's requests still go through.
        const uint8_t st = m[0] & 0xF0;
        const bool stripSysex = m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x7D && (m[4] == 0x01 || m[4] == 0x02 || m[4] == 0x06 || m[4] == 0x07 || m[4] == 0x0C || m[4] == 0x0D || m[4] == 0x0E);
        if (stripSysex || st == 0xE0 || st == 0x90 || st == 0x80 || st == 0xA0 || st == 0xB0) return;
    }
    const uint8_t status = m[0] & 0xF0;

    if (m[0] == 0xF0) {   // SysEx: names and colours
        if (!g_ownText) {
            if (!g_cubaseSpoke) std::printf("Cubase connected (the UF8 script is talking).\n");   // (the tray app shows it)
            g_cubaseSpoke = true;
        }
        if (!g_ownText && !g_cubaseFirstMs) g_cubaseFirstMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (auto n = parseName(m)) {
            std::string t(n->text.begin(), n->text.end());
            t = cs::fitName(t, int(uf8::kUf8ScribbleChars));   // abbreviated or cut (Settings: Long track names)
            g_dev.send(uf8::buildStripTextUpper(uint8_t(n->strip), t));
            g_rightSeen[n->strip] = false;   // another track: mono until it sends a right side
            if (!t.empty() && g_stripEmpty[n->strip]) {
                // A track is back on this strip. Cubase only sends the level when it differs from what the
                // strip showed before it went empty, so show the last level we had.
                g_stripEmpty[n->strip] = false;
                const std::string& v = g_dbText[n->strip];
                g_dev.send(uf8::buildFaderDbReadout(uint8_t(n->strip), v, v.empty() ? "" : "dB"));
                for (int cls = 0; cls < 3; ++cls) if (g_ledWant[n->strip][cls]) setLed(n->strip, cls, true);
            }
            if (t.empty()) {
                // An empty strip (past the last track): Cubase stops sending its meter and level, so clear
                // the meter and the dB readout here, and ignore late values until a track returns.
                g_stripEmpty[n->strip] = true;
                g_meterLevels[size_t(n->strip * 2)] = g_meterLevels[size_t(n->strip * 2 + 1)] = 0;
                g_meterShown = g_meterLevels;
                auto frames = uf8::buildVuMeter(g_meterLevels);
                g_dev.send(std::move(frames[0]));
                g_dev.send(std::move(frames[1]));
                g_dev.send(uf8::buildFaderDbReadout(uint8_t(n->strip), "", ""));
                // Its SOLO / CUT / SEL LEDs keep the previous track's state (Cubase sends nothing for an
                // empty strip), so switch them off.
                for (int cls = 0; cls < 3; ++cls) setLed(n->strip, cls, false);
                g_autoW[n->strip] = false;
                paintSel(n->strip);
            }
            paintStripNumbers();   // a strip that went empty or got a track
        } else if (m.size() >= 7 && m[4] == 0x0C && m[5] < kStrips) {
            // Fader level text -> the strip's dB readout (6 characters + "dB").
            std::string v(m.begin() + 6, m.end() - 1);
            if (v.size() > 6) v.resize(6);
            g_dbText[m[5]] = v;
            if (g_routeSend < 0 && !g_routeFlip && !g_stripFader[m[5]] && !g_noProject) cs::fader0dbSeen(v, g_volPb[m[5]]);
            const uint8_t s = m[5];
            if (g_routeSend >= 0 && (g_touched[s] || nowMs8() < g_dbOwnUntil[s])) {
                // The hand is on it (fader send view): this one shown once the hand has been off for 0.5 s, unless a
                // newer one comes.
                const int gen = ++g_dbHeldGen[s];
                std::thread([s, gen, v] {
                    for (;;) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        if (gen != g_dbHeldGen[s]) return;
                        if (g_touched[s] || nowMs8() < g_dbOwnUntil[s]) continue;
                        const bool late = lateDbText(s, v);
                        if (g_routeSend >= 0 && !g_stripEmpty[s] && !late) g_dev.send(uf8::buildFaderDbReadout(s, v, v.empty() ? "" : "dB"));
                        std::string drawn; int pos = 0;
                        { std::lock_guard<std::mutex> lk(g_dbDrawnMx); drawn = g_dbDrawn[s]; pos = g_dbDrawnPos[s]; }
                        std::printf("UF8 send fader %d stopped at %d: drawn %s, Cubase %s%s\n", s + 1, pos, drawn.c_str(), v.c_str(),
                            late ? " (late: not shown)" : drawn == v ? "" : " (corrected)");
                        return;
                    }
                }).detach();
            } else {
                ++g_dbHeldGen[s];   // (a held one is older)
                const bool recent = g_routeSend >= 0 && nowMs8() < g_dbOwnUntil[s] + 3000;
                const bool late = recent && lateDbText(s, v);
                if (!g_stripEmpty[s] && !late) g_dev.send(uf8::buildFaderDbReadout(s, v, v.empty() ? "" : "dB"));
                if (recent) std::printf("UF8 send fader %d: Cubase later %s%s\n", s + 1, v.c_str(), late ? " (late: not shown)" : "");
            }
        } else if (m.size() >= 7 && m[4] == 0x1C) {
            // Which of several same-named tracks the UF8 script found selected: for the UF1 script's track number.
            cs::uf1SendToScript(std::vector<unsigned char>(m.begin(), m.end()));
        } else if (m.size() >= 6 && m[4] == 0x1B) {
            // The UF8 script asks for the helper's channel lists.
            if (g_helperOpen) {
                std::vector<unsigned char> copy(m.begin(), m.end());
                std::lock_guard<std::mutex> lk(g_helperMx);
                g_helperOut.send(copy);
            }
        } else if (m.size() >= 7 && m[4] == 0x13) {
            // The script holds the UF8 frozen (01) while it finds the selected track at start-up, which can
            // take several tries while Cubase still ignores bank presses; 00 shows the end state. Released by
            // itself after 30 s in case the 00 never comes.
            if (m[5]) {
                const int gen = ++g_holdGen;
                g_freezeHeld = true;
                g_uf8Frozen = true;
                std::thread([gen] {
                    std::this_thread::sleep_for(std::chrono::seconds(30));
                    if (g_holdGen == gen && g_freezeHeld) { ++g_freezeGen; g_freezeHeld = false; g_uf8Frozen = false; replayUf8Cache(); }
                }).detach();
            } else if (g_freezeHeld) {
                ++g_holdGen;
                ++g_freezeGen;   // a burst still running must not show anything early
                g_freezeHeld = false;
                g_uf8Frozen = false;
                replayUf8Cache();
            }
        } else if (m.size() >= 10 && m[4] == 0x12) {
            // Bank size and position from the script: total (2 x 7 bit), offset (2 x 7 bit); 7F 7F = unknown.
            auto v = [&](int i) { return (m[i] == 0x7F && m[i + 1] == 0x7F) ? -1 : (m[i] << 7) | m[i + 1]; };
            g_bankTotal = v(5);
            g_bankOffset = v(7);
            paintStripNumbers();
        } else if (m.size() >= 9 && m[4] == 0x32 && m[5] < kStrips) {   // ARMED: a strip's track in the full list
            const int s = m[5], a = (m[6] == 0x7F && m[7] == 0x7F) ? -1 : (m[6] << 7) | m[7];
            g_armedAll[s] = a;
            const bool dead = g_uf8View == 7 && a < 0;
            if (dead && !g_dead[s]) { armedBlank(s); g_dead[s] = true; }
            else if (!dead) g_dead[s] = false;
            paintStripNumbers();
        } else if (m.size() >= 8 && m[4] == 0x33) {   // how many tracks are armed (the picker)
            { std::lock_guard<std::mutex> lk(g_numMx); g_armedCount = (m[5] << 7) | m[6]; }
            if (g_viewPick) paintSoftKeys();
        } else if (m.size() >= 7 && m[4] == 0x14) {   // the track view the script's active page is (Q3)
            const int v = m[5] < kViewCount ? m[5] : 0;
            if (g_uf8View.exchange(v) != v) {
                // The script switched itself (the start view): a new page starts on volume / pan, so the route again.
                std::printf("UF8 view: %s (from the script)\n", kViewNames[v]);
                std::thread([] { std::this_thread::sleep_for(std::chrono::milliseconds(60)); applyRoute(); }).detach();
            } else if (g_trace8) std::printf("UF8 view: %s (from the script)\n", kViewNames[v]);
            if (v != 7) for (auto& d : g_dead) d = false;   // (ARMED's dead strips live again in another view)
            paintStripNumbers(true);
            paintSoftKeys();   // Q3's LED, the picker
        } else if (m.size() >= 6 && m[4] == 0x11) {
            // The script asks for a "tick" in 300 ms (note 80): it has no timer of its own, and wants to look
            // at the bank only after Cubase has finished reacting to a new selection.
            std::thread([] {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                note(80, true);
                note(80, false);
            }).detach();
        } else if (m.size() >= 7 && m[4] == 0x10) {
            // The script asks for a jump: bank reset (note 79, to the first bank), then <count> bank-rights,
            // 60 ms apart. Used by "follow the selection" for far jumps.
            const int count = m[5] <= 64 ? m[5] : 64;
            const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            // Cubase ignores bank keys at first (at start-up a burst sent after 3 s still lost most steps).
            const long long wait = g_cubaseFirstMs + 5000 - now;
            std::thread([count, wait] {
                if (wait > 0) std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                const int gen = ++g_freezeGen;
                g_uf8Frozen = true;
                note(79, true);
                note(79, false);
                for (int i = 0; i < count; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    note(kNoteBankRight, true);
                    note(kNoteBankRight, false);
                }
                // Let Cubase send the last bank, then show the end state (unless a newer jump took over).
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                if (g_freezeGen == gen && !g_freezeHeld) {
                    g_uf8Frozen = false;
                    replayUf8Cache();
                }
                // Tell the script the burst is over (note 80), so it checks where the bank landed.
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                note(80, true);
                note(80, false);
            }).detach();
        } else if (m.size() >= 6 && (m[4] == 0x0B || m[4] == 0x0F)) {
            // The script asks for bank steps: 0B = right, 0F = left, optional count byte (default 1). Used by
            // the start-up search and by "follow the selection". Cubase ignores bank keys during its first
            // seconds, so the presses wait until 3 s after Cubase first spoke, and they are spaced 60 ms apart
            // (presses sent back to back were merged).
            const bool right = m[4] == 0x0B;
            const int count = (m.size() >= 7 && m[5] >= 1 && m[5] <= 64) ? m[5] : 1;
            const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const long long wait = g_cubaseFirstMs + 3000 - now;
            std::thread([wait, right, count] {
                if (wait > 0) std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                // More than one step: freeze the UF8 like a jump, so only the end shows; 100 ms apart (at 60 ms
                // Cubase merged presses).
                const int gen = count > 1 ? ++g_freezeGen : 0;
                if (count > 1) g_uf8Frozen = true;
                for (int i = 0; i < count; ++i) {
                    note(right ? kNoteBankRight : kNoteBankLeft, true);
                    note(right ? kNoteBankRight : kNoteBankLeft, false);
                    if (i + 1 < count) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (count > 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                    if (g_freezeGen == gen && !g_freezeHeld) { g_uf8Frozen = false; replayUf8Cache(); }
                }
            }).detach();
        } else if (m.size() >= 8 && m[4] == 0x06 && m[5] < kStrips) {
            // Pan position 0..127 -> the readout bar under the strip (a single line, like SSL's pan).
            static bool modeSent = false;
            g_panPos[m[5]] = uf8::vpotPosPan(m[6] / 127.0 * 2.0 - 1.0);
            if (g_focus >= 0) return;   // the V-pots show the focused parameter
            g_dev.send(uf8::buildVPotReadoutBar(g_panPos));
            if (!modeSent) { modeSent = true; g_dev.send(uf8::buildVPotMode(std::array<uint8_t, kStrips>{1, 1, 1, 1, 1, 1, 1, 1})); }
        } else if (m.size() >= 7 && m[4] == 0x07 && m[5] < kStrips) {
            // Pan text -> value line: "Pan" left, Cubase's value right, 19 characters.
            std::string v(m.begin() + 6, m.end() - 1);
            if (v.size() > 15) v.resize(15);
            std::string line = "Pan";
            line.append(19 - line.size() - v.size(), ' ');
            line += v;
            g_panLine[m[5]] = line;
            if (g_focus < 0) g_dev.send(uf8::buildValueLine(uint8_t(m[5]), line));
        } else if (m.size() >= 7 && m[4] == 0x2C && m[5] < kStrips) {
            // SEL on the selected track: no SSL channel strip there, so the channel editor (the V-pot push's note,
            // press and release 40 ms apart: notes back to back can merge).
            const uint8_t n = uint8_t(kNoteVPush + m[5]);
            note(n, true);
            std::thread([n] { std::this_thread::sleep_for(std::chrono::milliseconds(40)); note(n, false); }).detach();
        } else if (m.size() >= 8 && m[4] == 0x26 && m[5] < kStrips) {
            // Strip mode per strip: on = the fader follows the strip's fader (27); off = back to the track's volume.
            const int s = m[5];
            g_stripFader[s] = m[6] != 0;
            g_stripPan[s] = m.size() >= 9 && m[7] == 1;
            if (!g_stripFader[s] && g_volPb[s] >= 0 && !g_touched[s]) driveFader(s, uint16_t(g_volPb[s].load()), !g_routeFlip);
        } else if (m.size() >= 10 && m[4] == 0x16 && m[5] < kStrips) {
            // A settled send level from the fader send view: its exact position and Cubase's text, for the level curve.
            std::string t(m.begin() + 8, m.end() - 1);
            if (t.find("dB") == std::string::npos) t += "dB";
            // A pair far from what the curve says there is a late text (Cubase behind the hand): not learnt.
            const int pos = (m[6] << 7) | m[7];
            const std::string known = cs::uf1LevelText(pos);
            const double a = std::strtod(known.c_str(), nullptr), b = std::strtod(t.c_str(), nullptr);
            if (!known.empty() && std::fabs(a - b) > 1.5)
                std::printf("UF8 send level %s at %d not learnt: the curve says %s\n", t.c_str(), pos, known.c_str());
            else cs::uf1LearnLevel(pos, t);
        } else if (m.size() >= 9 && m[4] == 0x27 && m[5] < kStrips) {
            const int s = m[5], pos = (m[6] << 7) | m[7];
            // Within 3 s of the hand, a position away from where it let go is a late one (Cubase still catching up with
            // the writes): the motor stays (15.11.2026: the fader jumped back now and then after the hand let go).
            if (g_stripFader[s] && !g_touched[s]) {
                if (nowMs8() - g_handAt[s] < 3000 && std::abs(pos - g_handPos[s]) > 64)
                    std::printf("UF8 fader %d: late position %d from Cubase ignored (hand at %d)\n", s + 1, pos, g_handPos[s].load());
                else driveFader(s, uint16_t(pos), g_routeSend >= 0);   // (a send level by dB; an SSL strip's fader linear)
            }
        } else if (m.size() >= 7 && m[4] == 0x0D && m[5] < kStrips) {
            // A send level on the V-pot (the selected track's sends): "Send" left, the level right.
            std::string v(m.begin() + 6, m.end() - 1);
            if (v.size() > 14) v.resize(14);
            std::string line = "Send";
            line.append(19 - line.size() - v.size(), ' ');
            line += v;
            g_panLine[m[5]] = line;
            g_selSendText[m[5]] = v;
            g_selSendHas[m[5]] = !v.empty();
            ++g_selSendLog[m[5]].text;
            if (g_selSendOwnText[m[5]] && nowMs8() < g_selSendOwnUntil[m[5]]) ++g_selSendLog[m[5]].textHidden;
            if (g_focus < 0 && !(g_selSendOwnText[m[5]] && nowMs8() < g_selSendOwnUntil[m[5]])) g_dev.send(uf8::buildValueLine(uint8_t(m[5]), line));   // (not while the bridge draws a turn)
        } else if (m.size() >= 8 && m[4] == 0x0E && m[5] < kStrips) {
            // A send level 0..127 on the V-pot: a unipolar bar. (Not while the bridge draws a turn itself.)
            ++g_selSendLog[m[5]].pos;
            // Cubase's own level with its text (sent just before) teaches the curve, but only once a turn has settled:
            // learnt in the middle of a turn the pairs did not match and the text jumped back and forth (12.11.2026).
            const int v14 = m.size() >= 10 ? (int(m[7]) << 7) | int(m[8]) : -1;
            if (nowMs8() < g_selSendOwnUntil[m[5]]) { ++g_selSendLog[m[5]].posIgnored; return; }
            if (v14 >= 0) cs::uf1LearnLevel(v14, g_selSendText[m[5]]);
            g_selSendVal[m[5]] = v14 >= 0 ? v14 / 16383.0 : m[6] / 127.0;
            g_panPos[m[5]] = uf8::vpotPosUnipolar(m[6] / 127.0);
            if (g_focus < 0) g_dev.send(uf8::buildVPotReadoutBar(g_panPos));
        } else if (m.size() >= 10 && m[4] == 0x20 && m[5] < kStrips) {
            // The focused parameter on one strip: has (0 = no SSL channel strip / not on this model),
            // position 0..127, ring (1 unipolar, 2 bipolar), the value text.
            const int s = m[5], f = g_focus;
            if (f < 0) return;
            const bool has = m[6] != 0;
            const double v = m[7] / 127.0;
            g_focusMode[size_t(s)] = has ? (m[8] == 2 ? 2 : 1) : 3;
            g_focusPos[size_t(s)] = !has ? 0 : m[8] == 2 ? uf8::vpotPosBipolar(v * 2.0 - 1.0) : uf8::vpotPosUnipolar(v);
            std::string val = has ? std::string(m.begin() + 9, m.end() - 1) : std::string("-");
            std::string name = skLabelOfKey(f);
            if (val.size() > 9) val.resize(9);
            if (name.size() + val.size() > 18) name.resize(18 - val.size());
            std::string line = name;
            line.append(19 - line.size() - val.size(), ' ');
            line += val;
            g_dev.send(uf8::buildValueLine(uint8_t(s), line));
            // The ring frames carry all eight strips: only what changed goes out (a focus sends eight of these).
            if (g_focusPos != g_ringPosShown) { g_ringPosShown = g_focusPos; g_dev.send(uf8::buildVPotReadoutBar(g_focusPos)); }
            if (g_focusMode != g_ringModeShown) { g_ringModeShown = g_focusMode; g_dev.send(uf8::buildVPotMode(g_focusMode)); }
        } else if (auto c = parseColour(m)) {
            { std::lock_guard<std::mutex> lk(g_paletteMx); g_palette[c->strip] = colourToPalette(c->r, c->g, c->b); }
            pushPalette();
        }
        return;
    }
    if (status == 0xB0 && m.size() >= 3 && m[1] >= kCcMeter && m[1] < kCcMeter + 2 * kStrips) {
        // Meter from the script: CC 80 + strip, 0..127 -> the UF8's 0..31 VU byte (left and right the same).
        // Cubase sends a new value every few ms during playback; push a frame only when a byte moved by 2 or
        // more, or 50 ms passed, or a meter fell to zero (so a decay never sticks on one LED).
        auto& levels = g_meterLevels; auto& shown = g_meterShown; auto& lastPush = g_meterLastPush;
        // CC 80 + strip = left, CC 88 + strip = right. A mono channel sends only the left side, so until a
        // right value has arrived for the track on this strip, the left one is shown on both.
        const bool right = m[1] >= kCcMeter + kStrips;
        const int s = m[1] - kCcMeter - (right ? kStrips : 0);
        if (g_stripEmpty[s]) return;   // late value for a strip that is now empty
        const uint8_t b = uint8_t((m[2] * 31 + 63) / 127);
        if (right) { g_rightSeen[s] = true; levels[size_t(s * 2 + 1)] = b; }
        else {
            levels[size_t(s * 2)] = b;
            if (!g_rightSeen[s]) levels[size_t(s * 2 + 1)] = b;
        }
        const auto now = std::chrono::steady_clock::now();
        int maxDelta = 0;
        for (size_t i = 0; i < levels.size(); ++i) maxDelta = std::max(maxDelta, std::abs(int(levels[i]) - int(shown[i])));
        if (maxDelta == 0) return;
        if (maxDelta < 2 && b != 0 && now - lastPush < std::chrono::milliseconds(50)) return;
        lastPush = now;
        shown = levels;
        auto frames = uf8::buildVuMeter(levels);
        g_dev.send(std::move(frames[0]));
        g_dev.send(std::move(frames[1]));
        return;
    }
    if (status == 0xE0 && m.size() >= 3) {   // fader from Cubase
        const int s = m[0] & 0x0F;
        if (s >= kStrips) return;
        const uint16_t v = uint16_t((m[1] & 0x7F) | ((m[2] & 0x7F) << 7));
        // A fader send view: the faders are the script's (27, the send levels through Direct Access); what Cubase sends
        // here is a placeholder binding and must not move the motors, nor be kept as the volume.
        if (g_routeSend >= 0) return;
        g_volPb[s] = v;
        if (!g_routeFlip && !g_stripFader[s] && !g_noProject) cs::fader0dbSeen(g_dbText[s], v);
        if (g_touched[s] || g_stripFader[s]) return;   // hand wins; strip mode: the strip's fader drives it
        driveFader(s, v, !g_routeFlip);   // (volume by dB; pan under FLIP linear)
        return;
    }
    // LED feedback: note on / note off, plus polyphonic aftertouch (Cubase sends
    // value 127 for "on" this way, see cubase/docs/bridge-midi-protocol.md).
    if ((status == 0x90 || status == 0x80 || status == 0xA0) && m.size() >= 3) {
        if (m[1] >= kNoteNoRec && m[1] < kNoteNoRec + kStrips && status != 0xA0) {   // script: strip cannot be armed
            const int s = m[1] - kNoteNoRec;
            g_noRec[s] = status == 0x90 && m[2] > 0;
            if (g_trace8) std::printf("UF8 rec strip %d: %s\n", s + 1, g_noRec[s] ? "cannot be armed" : "can be armed");
            setLed(s, 3, g_recOn[s]);   // (also redraws SEL in REC)
            return;
        }
        if (auto t = noteToLed(m[1])) {
            const bool on = status != 0x80 && m[2] > 0;
            if (t->cls <= 2 && t->strip >= 0 && t->strip < kStrips) g_ledWant[t->strip][t->cls] = on;
            if (on && t->cls <= 2 && t->strip >= 0 && t->strip < kStrips && g_stripEmpty[t->strip]) return;   // empty strip stays dark
            setLed(t->strip, t->cls, on);
        }
    }
}

// Show the last state Cubase sent (names, colours, pan, dB texts, faders, LEDs) again: after a reopen, and
// after a jump during which the UF8 was frozen.
// The track names again (after the mode picker wrote over them): the last names Cubase sent.
void restoreStripNames()
{
    std::map<int, std::vector<unsigned char>> cache;
    { std::lock_guard<std::mutex> lk(g_uf8CacheMx); cache = g_uf8Cache; }
    for (int s = 0; s < kStrips; ++s) {
        auto it = cache.find(0x10000 + (0x01 << 8) + s);
        if (it != cache.end()) { auto msg = it->second; onMidiIn(msg); }
        else g_dev.send(uf8::buildStripTextUpper(uint8_t(s), ""));
    }
}

void replayUf8Cache()
{
    std::map<int, std::vector<unsigned char>> cache;
    { std::lock_guard<std::mutex> lk(g_uf8CacheMx); cache = g_uf8Cache; }
    for (auto& kv : cache) { auto msg = kv.second; onMidiIn(msg); }
    paintStripNumbers(true);
}

} // namespace

// Automation mode LEDs (UF8 Off / Trim / Latch / Touch). Cubase does not tell scripts which global mode is
// active, so the bridge remembers what was pressed on the surfaces: Off (Cross-Over), Latch and Touch are
// one mode, Trim switches on and off on top. A mode changed in Cubase itself is not seen. Called from the
// UF8 key handler and from the UF1 (SHIFT + soft keys).
void cs::autoModePressed(uint8_t n)
{
    static std::mutex mx;
    static uint8_t mode = 0;      // 0 unknown, else kNoteAutoOff / kNoteAutoLatch / kNoteAutoTouch
    static bool trim = false;
    std::lock_guard<std::mutex> lk(mx);
    if (n == kNoteAutoTrim) trim = !trim;
    else if (n != 0) mode = n;   // 0 = just draw the current state (start-up)
    if (!g_uf8Running) return;
    {
        // Off (Cross-Over): the colour-pair frames alone left it dark (white and blue tried). Rea-Sixty found
        // the same on the Auto key (cell 0x55) and Plug-in / Page keys: they also need the legacy FF 3B
        // on/off frame. Try that here.
        // Purple, and dim rather than dark when another mode is active (like its neighbours): the legacy
        // frame stays "on" in both states, the colour pair carries bright or dim.
        auto off = uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::AutoOff,
            mode == kNoteAutoOff ? uf8::GlobalLedState::Bright : uf8::GlobalLedState::Dim,
            uf8::ledColourForTrackRgb(0x9030FF));
        off.legacy = uf8::buildLedCommand(0x27, true);
        sendLedFrames(off);
    }
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::AutoLatch, mode == kNoteAutoLatch));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::AutoTouch, mode == kNoteAutoTouch));
    sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::AutoTrim, trim));
}

// The UF1's BANK keys bank the UF8 (as if its own bank key had been pressed).
void cs::uf8RedrawNumbers() { paintStripNumbers(); }
bool cs::uf8BankPress(bool right)
{
    if (!g_uf8Running) return false;
    if (right && userBankRight(true)) return true;
    note(right ? kNoteBankRight : kNoteBankLeft, true);
    note(right ? kNoteBankRight : kNoteBankLeft, false);
    return true;
}

// The UF1's BANK keys without a UF8: the UF8 script (loaded whether or not the UF8 is connected) banks its strips,
// which follow the MixConsole (hidden tracks and folders are not on strips), and the track on the same strip of
// the new bank is selected (strip 1 if the selection was not on the bank).
void cs::uf8BankSelect(bool right)
{
    // One press after the other (quick presses queue up): each waits until the script reports its bank's new
    // position (SysEx 12, sent when Cubase has put the new tracks on the strips) and then presses SEL. SEL sent
    // before that (a fixed 40 ms) landed on the old bank: the selection did not move and presses seemed lost.
    static std::mutex order;
    static int lastStrip = 0;                        // the strip the last press selected
    static std::chrono::steady_clock::time_point lastAt;
    std::printf("UF1 BANK %s pressed (no UF8)\n", right ? "right" : "left");   // the click, before it queues
    std::thread([right] {
        std::lock_guard<std::mutex> lk(order);
        const int before = g_bankOffset;
        // Right after a press Cubase's SEL LEDs are in flux (the old selection leaves the strips before the new one
        // is reported): within 3 s the strip of the last press counts.
        const bool recent = std::chrono::steady_clock::now() - lastAt < std::chrono::seconds(3);
        const int strip = recent ? lastStrip : g_selStrip > 0 ? g_selStrip - 1 : 0;
        // The script's follow selection stays out of it for 2.5 s (SysEx 5D): Cubase reports the selection late,
        // still on the old bank, and follow banked straight back.
        sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x5D, 40, 0xF7});   // 4 s
        std::this_thread::sleep_for(std::chrono::milliseconds(40));   // messages back to back can be lost
        const auto t0 = std::chrono::steady_clock::now();
        // Already at the end that way: nothing will move, so no wait (the queue was held 2.6 s by such a press).
        // The press selects the first / last track instead.
        const int total = g_bankTotal;
        if (before >= 0 && g_selStrip != 0 && ((!right && before == 0) || (right && total >= 0 && before + 8 >= total))) {
            const int end = right ? std::min(total - before, 8) - 1 : 0;   // the last track's strip / strip 1
            std::printf("UF1 BANK %s (no UF8): already at the %s (bank position %d): SEL strip %d\n", right ? "right" : "left",
                        right ? "last bank" : "first bank", before, end + 1);
            if (end >= 0 && end + 1 != g_selStrip) {
                note(uint8_t(kNoteSel + end), true);
                note(uint8_t(kNoteSel + end), false);
                lastStrip = end;
                lastAt = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            return;
        }
        const bool shifts = right && userBankRight(true);
        if (!shifts) {
            note(right ? kNoteBankRight : kNoteBankLeft, true);
            note(right ? kNoteBankRight : kNoteBankLeft, false);
        }
        // The strips have moved when the script reports a new, known position (SysEx 12). While the names change it
        // reports -1 (they match nowhere), and strip 1's name comes first: SEL on either landed on the old bank
        // (bridge.log, 9.11.2026: four presses in ten). Moved took 60-530 ms.
        int i = 0;
        auto moved = [&] { const int o = g_bankOffset; return o >= 0 && o != before; };
        // Up to 2.5 s: Cubase sometimes took more than 1.1 s, and a SEL left out then left the selection behind the
        // strips (bridge.log 9.11.2026: strips on the last bank, selection on the one before; BANK right did nothing).
        for (; i < 167 && !moved(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(15));
        const long long waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        // One line per press in bridge.log (without --trace): what was sent and what came back.
        if (i == 167) {   // nothing moved (the first / last bank)
            // The selection not on the strips (left behind earlier): select on the strips where they are.
            const bool behind = g_selStrip == 0;
            std::printf("UF1 BANK %s (no UF8): %s from bank position %d; nothing moved in %lld ms, %s\n",
                        right ? "right" : "left", shifts ? "single shifts" : "bank key", before, waited,
                        behind ? ("the selection is not on the strips: SEL strip " + std::to_string(strip + 1)).c_str() : "no SEL");
            if (!behind) return;
        }
        else std::printf("UF1 BANK %s (no UF8): %s from bank position %d; moved after %lld ms (%s), SEL strip %d\n",
                    right ? "right" : "left", shifts ? "single shifts" : "bank key", before, waited,
                    ("position " + std::to_string(g_bankOffset.load())).c_str(), strip + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));   // the rest of the strips' names
        note(uint8_t(kNoteSel + strip), true);
        note(uint8_t(kNoteSel + strip), false);
        lastStrip = strip;
        lastAt = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));   // notes back to back can merge
    }).detach();
}

// Settings: led- / display-brightness (Brightness.h's five steps).
void uf8ApplyBrightness()
{
    if (!g_uf8Running) return;
    const bool dark = cs::sleep::asleep();   // Sleep when idle: all off
    g_dev.send(uf8::buildLedBrightness(dark ? 0 : brightness::kTable[cs::settings::brightnessLevel("led-brightness")].uf8_led));
    g_dev.send(uf8::buildLcdBrightness(dark ? 0 : brightness::kTable[cs::settings::brightnessLevel("display-brightness")].uf8_lcd));
}

static std::chrono::steady_clock::time_point g_uf8NextTry;   // uf8Poll: when to look for the UF8 again
static bool g_uf8MissingSaid = false;                          // "not found" is said once, not every two seconds

// Opens the UF8 and draws the last state Cubase sent (the cache is kept while the UF8 is away). Used at start,
// when it is plugged in, and after it stopped answering. The MIDI ports stay as they are.
bool uf8Connect(bool quiet)
{
    if (!g_dev.open()) {
        if (!quiet) std::fprintf(stderr, "UF8 not found (%s); it is picked up when it is connected.\n"
                                         "(If it is connected: is SSL 360 or SSL 360 Core running? Quit it.)\n",
                                 g_dev.lastError().c_str());
        return false;
    }
    g_uf8ConnectedAt = std::chrono::steady_clock::now();
    g_dev.setRawInputHandler(onUsbInput);
    { std::lock_guard<std::mutex> lk(g_faderMx); for (auto& e : g_engaged) e = false; }
    g_meterShown = {};
    { std::lock_guard<std::mutex> lk(g_numMx); for (auto& n : g_numShown) n.clear(); for (auto& c : g_csShown) c = "\x01"; }
    g_uf8Running = true;
    uf8ApplyBrightness();
    replayUf8Cache();
    if (!g_cubaseSpoke) {
        // Nothing from Cubase yet (the bridge has just started; Cubase connects to new ports after a while).
        const char* words[] = {"Waiting", "for", "Cubase"};
        for (int s = 0; s < 3; ++s) {
            std::vector<unsigned char> name{0xF0, 0x7D, 0x52, 0x36, 0x01, uint8_t(s)};
            for (const char* c = words[s]; *c; ++c) name.push_back(uint8_t(*c));
            name.push_back(0xF7);
            g_ownText = true;
            onMidiIn(name);
            g_ownText = false;
        }
    }
    cs::autoModePressed(0);   // automation mode keys dim from the start (the mode is not known yet)
    paintSelMode();
    paintSoftKeys();
    std::printf("UF8 connected (serial %s).\n", g_dev.serial().c_str());
    return true;
}

// From the main loop: a UF8 that stopped answering or was unplugged is let go of, and a missing one is looked
// for every two seconds.
void uf8Poll()
{
    const auto now = std::chrono::steady_clock::now();
    if (g_uf8Running) {
        if (!g_dev.needsReopen()) return;
        std::fprintf(stderr, "The UF8 stopped answering or was unplugged (%s); looking for it again.\n", g_dev.lastError().c_str());
        g_uf8Running = false;
        g_dev.close();
        g_uf8MissingSaid = true;
        g_uf8NextTry = now + std::chrono::seconds(1);
        return;
    }
    if (now < g_uf8NextTry) return;
    g_uf8NextTry = now + std::chrono::seconds(2);
    g_uf8MissingSaid = !uf8Connect(g_uf8MissingSaid);
}

// To the helper script (track steps: Uf1Bridge.cpp). False if its ports are not open.
namespace cs {
bool helperSend(const std::vector<unsigned char>& msg)
{
    if (!g_helperOpen) return false;
    std::vector<unsigned char> copy = msg;
    std::lock_guard<std::mutex> lk(g_helperMx);
    try { g_helperOut.send(copy); } catch (const RtMidiError& e) { std::fprintf(stderr, "helper out: %s\n", e.getMessage().c_str()); return false; }
    return true;
}
} // namespace cs

int main(int argc, char** argv)
{
    cs::redirectTempToLogs(argc > 0 ? argv[0] : nullptr);
    cs::bridgeLogInit(argc > 0 ? argv[0] : nullptr);
    cs::favourites::init(argc > 0 ? argv[0] : nullptr);
    cs::settings::init(argc > 0 ? argv[0] : nullptr);
    cs::stopSslCore();   // SSL360Core.exe takes the devices and the SSL plug-ins (SslCoreGuard.cpp)
    // --no-script-log: the scripts' log lines (SysEx 7E) are dropped instead of written to cubase\logs\scripts.log.
    bool scriptLog = true;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--no-script-log") scriptLog = false;
    if (scriptLog) cs::scriptLogInit(argc > 0 ? argv[0] : nullptr);
    else std::printf("Script log off (--no-script-log).\n");
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--trace") { cs::uf1SetTrace(true); cs::uc1SetTrace(true); g_trace8 = true; cs::midiPortsTrace(true); }
        else if (std::string(argv[i]) == "--loopmidi") cs::midiPortsPreferLoopMidi(true);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // Every surface's ports are made whether or not the surface is connected: Cubase sees MIDI ports only when it
    // starts, and a script loads only when its ports are there. The surfaces themselves come and go (uf8Poll,
    // cs::uf1Poll, cs::uc1Poll); what Cubase sends meanwhile is cached and drawn when one is connected.
    g_uf8MissingSaid = !uf8Connect(false);
    g_uf8NextTry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const bool uf8Ports = g_out.open(kPortToCubase) && g_in.open(kPortFromCubase, &onMidiIn);
    if (!uf8Ports) std::fprintf(stderr, "UF8: could not open the MIDI ports \"%s\" / \"%s\".\n", kPortToCubase, kPortFromCubase);
    // The helper's ports: its channel names go straight on to the UF8 script.
    g_helperOpen = g_helperOut.open(kHelperTo) && g_helperIn.open(kHelperFrom, [](std::vector<unsigned char>& msg) {
        if (cs::scriptLogSysex(msg.data(), msg.size())) return;
        if (msg.size() >= 7 && msg[0] == 0xF0 && msg[4] == 0x1D) { cs::uf1SetSoloActive(msg[5] != 0); return; }   // anything soloed
        if (msg.size() >= 10 && msg[0] == 0xF0 && msg[4] == 0x1A) {   // a channel's track number (UF8 strips)
            const size_t i = (size_t(msg[5]) << 7) | msg[6];
            const int n = (int(msg[7]) << 7) | msg[8];
            if (i < 256) {
                std::lock_guard<std::mutex> lk(g_numMx);
                if (g_chanNumber.size() <= i) g_chanNumber.resize(i + 1, 0);
                g_chanNumber[i] = n;
            }
            paintStripNumbers();
            return;
        }
        if (msg.size() >= 11 && msg[0] == 0xF0 && msg[4] == 0x1F && msg[5] > 0 && msg[5] < 7) {   // a view's track number
            const size_t i = (size_t(msg[6]) << 7) | msg[7];
            const int n = (int(msg[8]) << 7) | msg[9];
            if (i < 256) {
                std::lock_guard<std::mutex> lk(g_numMx);
                auto& numbers = g_viewNumber[msg[5]];
                if (numbers.size() <= i) numbers.resize(i + 1, 0);
                numbers[i] = n;
            }
            if (g_uf8View == msg[5]) paintStripNumbers();
            return;
        }
        if (msg.size() >= 9 && msg[0] == 0xF0 && msg[4] == 0x1E && msg[5] > 0 && msg[5] < 7) {   // a view's channel (or none)
            const size_t i = (size_t(msg[6]) << 7) | msg[7];
            bool countChanged = false;
            if (i < 256) {
                std::lock_guard<std::mutex> lk(g_numMx);
                auto& has = g_viewHas[msg[5]];
                if (has.size() <= i) has.resize(i + 1, false);
                const bool now = msg.size() > 9;   // (a name follows)
                const int before = viewChannels(msg[5]);
                has[i] = now;
                countChanged = (before == 0) != (viewChannels(msg[5]) == 0);
            }
            if (countChanged && g_viewPick) paintSoftKeys();
        }
        if (msg.size() < 6 || msg[0] != 0xF0 || (msg[4] != 0x18 && msg[4] != 0x19 && msg[4] != 0x1E && msg[4] != 0x2F)) return;
        std::lock_guard<std::mutex> lk(g_outMx);
        try { g_out.send(msg); } catch (const RtMidiError& e) { std::fprintf(stderr, "midi out: %s\n", e.getMessage().c_str()); }
    });
    if (!g_helperOpen) std::fprintf(stderr, "Helper ports not available: the UF8 does not follow the selection, and its record keys stay dark.\n");
    {
        // A new bridge (SysEx 7F 01): scripts that are already running (the tray app kept the ports, so Cubase did not
        // activate them again) send their state again. Before any script talks, so the UF8's bank trick below is
        // not needed then.
        std::vector<unsigned char> hello{0xF0, 0x7D, 0x52, 0x36, 0x7F, 0x01, 0xF7};
        std::lock_guard<std::mutex> lk(g_outMx);
        if (uf8Ports) { auto h = hello; g_out.send(h); }
        if (g_helperOpen) { auto h = hello; g_helperOut.send(h); }
    }
    const bool uf1Ports = cs::uf1Start();
    const bool uc1Ports = cs::uc1Start();
    if (!uf8Ports && !uf1Ports && !uc1Ports) {
        std::fprintf(stderr, "No MIDI ports could be made (is loopMIDI installed?). Exiting.\n");
        return 1;
    }
    std::printf("Cubase-Sixty %s. Bridging; Ctrl+C to stop.\n", CS_VERSION);
    if (uf8Ports && !cs::midiPortsAreVirtual()) {
        // Cubase sent the track names and colours when it started, before the bridge was listening, and it only
        // sends changes. If nothing has arrived a couple of seconds after start-up, press bank-next and then
        // bank-previous: Cubase then sends everything again (names, colours, faders, LEDs). This assumes Cubase is
        // not on its last bank; there bank-next does nothing and the bank would end up one step back.
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            if (g_cubaseSpoke) return;
            note(kNoteBankRight, true);  note(kNoteBankRight, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            note(kNoteBankLeft, true);   note(kNoteBankLeft, false);
        }).detach();
    }
    {
        bool lamp = false;
        for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--lamptest") lamp = true;
        if (lamp) std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(6)); cs::uf1LampTest(); }).detach();
    }

    // The Settings page (the tray app's Settings opens it).
    cs::settingsServer::start(
        [] {
            const char* cubase = !g_cubaseSpoke ? "waiting for Cubase" : g_noProject ? "no project (the Hub)" : "project open";
            return std::string("{\"uf8\":\"") + (g_uf8Running ? "connected" : "not connected") + "\",\"uf1\":\"" + cs::uf1State() +
                   "\",\"uc1\":\"" + cs::uc1State() + "\",\"cubase\":\"" + cubase + "\"}";
        },
        [] { cs::uc1SendFavourites(); },
        cs::logDir(argc > 0 ? argv[0] : nullptr).string(),
        [](const std::string& key) {
            if (key == "led-brightness" || key == "display-brightness") {
                uf8ApplyBrightness();
                cs::uf1ApplyBrightness();
                cs::uc1ApplyBrightness();
                return;
            }
            if (key == "uc1-7seg") { cs::uc1RedrawSevenSeg(); return; }
            if (key == "uc1-knob-step") { cs::uc1SendKnobStep(); return; }
            if (key.rfind("uc1-key-", 0) == 0) { cs::uc1SendKeyJobs(); return; }
            if (key == "uc1-strip-load" || key == "uc1-dyn-in-comp" || key == "uc1-bus-comp-start") { cs::uc1SendStripOptions(); return; }
            if (key != "track-names") return;
            // The names again, abbreviated or cut as now set (the UC1 redraws its own all the time).
            std::vector<std::vector<unsigned char>> names;
            {
                std::lock_guard<std::mutex> lk(g_uf8CacheMx);
                for (auto& kv : g_uf8Cache) if (kv.first >= 0x10000 + (0x01 << 8) && kv.first < 0x10000 + (0x02 << 8)) names.push_back(kv.second);
            }
            for (auto& n : names) onMidiIn(n);
            cs::uf1RedrawName();
            cs::uc1RedrawNames();
        });
    cs::stopEventInit();
    while (g_run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (cs::stopRequested()) { std::printf("Stop asked for (tray app): blanking the surfaces.\n"); break; }
        cs::uf1Poll();
        cs::uc1Poll();
        { static int n = 0; if (++n % 5 == 0) selSendLogFlush(); }   // once a second
        {   // Every second: the UF8 strips' plug-in labels (a plug-in added or removed in Cubase).
            static int tick = 0;
            if (++tick % 5 == 0) paintStripNumbers();
        }
        {   // Every 2 s: what the surfaces say while no project is open. Cubase gone without closing its project
            // (a crash, ended in Task Manager): blanked as if it had.
            static int n = 0, shown = -1;
            if (++n % 10 == 0) {
                const bool running = cs::cubaseRunning();
                if (!running && g_projectOpen) {
                    g_projectOpen = false;
                    std::printf("Cubase: no project (Cubase is not running)\n");
                    uf8NoProject();
                    cs::uf1NoProjectNow();
                    cs::uc1NoProjectNow();
                }
                // Not running, or running but not connected yet: "Waiting for Cubase". Connected, no project (the Hub,
                // also before the first project): "No Project". A project open: the tracks.
                const int kind = !running || !g_cubaseSpoke ? 2 : !g_projectOpen ? 1 : 0;
                if (kind != shown) {
                    shown = kind;
                    if (kind) { uf8IdleText(kind); cs::uf1IdleText(kind); cs::uc1IdleText(kind); }
                    else cs::uf1IdleText(0);   // a project open: the UF1's name not left on "No Proj." without a track
                }
            }
        }
        uf8Poll();
        cs::sleep::tick(cs::uf1TransportRunning(), [](bool asleep) {
            std::printf("Surfaces %s.\n", asleep ? "asleep (Sleep when idle)" : "awake");
            uf8ApplyBrightness();
            cs::uf1ApplyBrightness();
            cs::uc1ApplyBrightness();
        });
    }
    // Leaving: the surfaces blank (as with no project) before they are let go of, so they do not keep showing the
    // last tracks. (Ctrl+C and the tray app's Quit come here; a killed process cannot.)
    if (g_uf8Running) {
        uf8NoProject();
        uf8IdleText(3);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));   // let the frames go out and the faders reach 0 dB
        g_uf8Running = false;
        g_dev.close();
    }
    cs::settingsServer::stop();
    cs::uf1Stop();
    cs::uc1Stop();
    return 0;
}
