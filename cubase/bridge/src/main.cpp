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
#include "Protocol.h"
#include "UF8Device.h"
#include "Uf8Fader.h"
#include "Uf8Frames.h"
#include "Uf1Bridge.h"
#include "Favourites.h"
#include "Settings.h"
#include "Uc1Bridge.h"
#include "ScriptLog.h"
namespace cs { int stopSslCore(); }

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
const char* skLabel(int domain, int bank, int slot) { return domain ? kBcSoftKeyLabels[bank][slot] : kSoftKeyLabels[bank][slot]; }
// A focus key: domain * 48 + bank * 8 + slot.
const char* skLabelOfKey(int key) { return skLabel(key / 48, key % 48 / 8, key % 8); }
bool softKeyLive(int domain, int bank, int slot) { const std::string l = skLabel(domain, bank, slot); return !l.empty() && l != "A/B" && l != "HQ MODE"; }
std::atomic<int> g_skBank{0};
std::atomic<int> g_focus{-1};   // bank * 8 + slot of the focused parameter, -1 = none (V-pots on pan)
// Pan as the script last sent it (shown again when the focus ends), and the focused parameter per strip.
std::array<uint16_t, 8> g_panPos{};
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
enum { kEncSelect = 0, kEncNudge, kEncMarkers, kEncBank, kEncCsFav, kEncBcFav, kEncModes };
const char* const kEncNames[kEncModes] = { "Ch Sel", "Nudge", "Markers", "Bank 1", "CS Fav", "BC Fav" };
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
bool g_ownText = false;   // the bridge feeds its own text through onMidiIn ("Waiting for Cubase"): not Cubase speaking

// Bank-right by the user (UF8 key or UF1 BANK): stop at the bank where the last track is on strip 8,
// instead of a bank with empty strips at the end. The script reports the bank's track count and position
// (SysEx 12). Near the end, the bridge sends the needed single-track shifts (the Page-right note) instead
// of a bank step; on the last full bank it does nothing. Unknown count or position: a normal bank step.
std::atomic<int> g_bankTotal{-1}, g_bankOffset{-1};
std::atomic<int> g_selStrip{0};   // the UF8 strip (1-8) holding the selected track, 0 = none (from Cubase's SEL LEDs)
void note(uint8_t n, bool on);
// Returns true when it handled the press (the caller must then not send the bank note).
bool userBankRight(bool pressed)
{
    const int total = g_bankTotal, offset = g_bankOffset;
    if (total < 0 || offset < 0) return false;
    const int remaining = total - (offset + 8);   // tracks after the last strip
    if (remaining >= 8) return false;             // a full bank follows: the normal step
    if (!pressed) return false;                   // releases pass (a stray release does no harm)
    if (remaining <= 0) return true;              // the last track is already on a strip: stay
    std::thread([remaining] {
        for (int i = 0; i < remaining; ++i) {
            if (i) std::this_thread::sleep_for(std::chrono::milliseconds(60));
            note(kNotePageRight, true);
            note(kNotePageRight, false);
        }
    }).detach();
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
void driveFader(int s, uint16_t v14)
{
    std::vector<std::vector<uint8_t>> frames;
    { std::lock_guard<std::mutex> lk(g_faderMx); frames = uf8fader::drive(uint8_t(s), midi14ToPb(v14), g_engaged[s]); }
    for (auto& f : frames) g_dev.send(std::move(f));
}
std::atomic<bool> g_routeSelSends{false}, g_routeFlip{false};
long long nowMs8() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void note2(uint8_t n) { sendMidi({0x91, n, 127}); sendMidi({0x81, n, 0}); }
void unfocus()
{
    g_focus = -1;
    sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x21, 0x7F, 0x7F, 0xF7});
    paintSoftKeys();
    paintVpots();
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
    switch (d[1]) {
    case 0x21: {   // fader position: FF 21 03 strip lsb msb ck
        const uint8_t s = d[3];
        if (s >= kStrips || !g_touched[s]) return;   // only a hand moves a fader to Cubase
        const uint16_t pb14 = uint16_t((d[4] & 0x7F) | ((d[5] & 0x7F) << 7));
        const auto r = uf8fader::read(pb14, (d[4] & 0x80) != 0, /*grabValid=*/false);
        if (!r.usable) return;
        const uint16_t v = pbToMidi14(r.pbOut);
        if (g_stripFader[s]) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x25, s, uint8_t((v >> 7) & 0x7F), uint8_t(v & 0x7F), 0xF7});
        else sendMidi({uint8_t(0xE0 | s), uint8_t(v & 0x7F), uint8_t((v >> 7) & 0x7F)});
        // Echo the hand position so the motor does not jump back on release.
        g_dev.send(uf8fader::echo(s, r.pbOut));
        break;
    }
    case 0x20: {   // fader touch: FF 20 02 strip state ck
        const uint8_t s = d[3];
        if (s >= kStrips) return;
        const bool down = d[4] != 0;
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
        if (d[3] == 0x73) {   // NAV: a tap = encoder on Markers on / off; held 1 s = Nav Mode on / off
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
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    if (g != gen) return;
                    held = true;
                    g_nav = !g_nav;
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
        if (d[3] == 0x74) {   // NUDGE: Nudge on and off
            if (pressed) {
                g_encMode = g_encMode == kEncNudge ? kEncSelect : kEncNudge;
                paintEncLeds();
            }
            break;
        }
        if (d[3] == 0x43 || d[3] == 0x44) {   // Q1 / Q2: the soft keys on the channel strip / the bus comp (Nav Mode: cycle / markers)
            if (pressed) {
                if (g_nav) g_navView = d[3] == 0x43 ? 1 : 0;
                else g_skDomain = d[3] == 0x44 ? 1 : 0;
                paintSoftKeys();
            }
            break;
        }
        if (d[3] >= 0x68 && d[3] <= 0x6D) {   // V-POT, 1-5: the soft-key page
            if (pressed) { g_skBank = d[3] - 0x68; paintSoftKeys(); }
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
            break;
        }
        if (d[3] >= 0x08 && d[3] <= 0x0F && g_focus < 0 && g_stripPan[d[3] - 0x08]) {   // strip mode: centre the strip's pan
            if (pressed) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x29, uint8_t(d[3] - 0x08), 0xF7});
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
        if (auto n = buttonToNote(d[3])) {
            // An unplugged foot-switch jack reports itself as "pressed" while the UF8 starts up,
            // which started the transport. Ignore the foot switches for the first seconds after each
            // connect (at start, and when the UF8 is plugged in again).
            const bool foot = (*n == 62 || *n == 63);
            if (foot && std::chrono::steady_clock::now() - g_uf8ConnectedAt.load() < std::chrono::seconds(4)) break;
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
        const int delta = uf8frames::vpotDelta(d[4]);
        if (delta == 0) break;
        if (g_focus >= 0) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x22, s, uint8_t(std::clamp(64 + delta, 1, 127)), 0xF7});   // the focused parameter
        else if (g_stripPan[s]) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x28, s, uint8_t(std::clamp(64 + delta, 1, 127)), 0xF7});   // strip mode: the strip's pan
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
        if (strip >= 0 && strip < 8) { g_selOn[strip] = on; paintSel(strip); }
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

void paintSel(int strip)
{
    if (strip < 0 || strip >= kStrips) return;
    const int mode = g_selMode;
    if (mode == kSelNorm) { sendLedFrames(uf8::buildLedColourPair(uint8_t(strip), uf8::LedClass::Sel, g_selOn[strip])); return; }
    if (mode == kSelRec) {
        // As the top soft keys: dark where the track cannot be armed, dim red where it can, red when armed.
        const bool can = !g_noRec[strip] && !g_stripEmpty[strip];
        const bool armed = can && g_recOn[strip];
        sendLedFrames(can ? uf8::buildLedColourPair(uint8_t(strip), uf8::LedClass::Sel, true,
                                                    armed ? uf8::ledColourRed() : uf8::ledColourForTrackRgb(0x5A0000))
                          : uf8::buildLedColourPair(uint8_t(strip), uf8::LedClass::Sel, false, uf8::ledColourRed()));
        return;
    }
    const bool on = g_autoW[strip].load();
    sendLedFrames(uf8::buildLedColourPair(uint8_t(strip), uf8::LedClass::Sel, on && !g_stripEmpty[strip], uf8::ledColourRed()));
}
void paintSoftKeys()
{
    if (g_noProject) return;
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
std::atomic<int> g_freezeGen{0};
std::atomic<bool> g_freezeHeld{false};
std::atomic<int> g_holdGen{0};   // the script holds the freeze (SysEx 13 01 ... 13 00): bursts leave it on
void replayUf8Cache();
void onMidiIn(std::vector<unsigned char>& msg);
// No project (the script's driver deactivated): every strip as an empty one (name, colour, meter, dB, its LEDs),
// faders down, value lines and V-pot bars off, the LEDs Cubase drives off. What Cubase sends on the next project
// paints it all again.
std::map<int, std::vector<unsigned char>> g_npBefore, g_npBlanked;   // the cache before / right after the blanking
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
        std::vector<unsigned char> fader{uint8_t(0xE0 | s), 0, 0};
        onMidiIn(name);
        onMidiIn(colour);
        onMidiIn(fader);
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
                L::Norm, L::Rec, L::Auto, L::Nav, L::Nudge, L::Plugin})
        sendLedFrames(uf8::buildUf8GlobalLed(l, false));
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
void onMidiIn(std::vector<unsigned char>& msg)
{
    const auto& m = msg;
    if (m.empty()) return;
    if (cs::scriptLogSysex(m.data(), m.size())) return;
    // The script's driver: 7C 00 = deactivated (Cubase closed the project and shows the Hub), 7C 01 = active again.
    if (m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x7D && m[4] == 0x7C) {
        if (m[5] == 0) uf8NoProject();
        else { g_dev.send(uf8::buildVPotMode(std::array<uint8_t, kStrips>{1, 1, 1, 1, 1, 1, 1, 1})); uf8ProjectOpen(); }
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
        } else if (m.size() >= 7 && m[4] == 0x0C && m[5] < kStrips) {
            // Fader level text -> the strip's dB readout (6 characters + "dB").
            std::string v(m.begin() + 6, m.end() - 1);
            if (v.size() > 6) v.resize(6);
            g_dbText[m[5]] = v;
            if (!g_stripEmpty[m[5]]) g_dev.send(uf8::buildFaderDbReadout(uint8_t(m[5]), v, v.empty() ? "" : "dB"));
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
        } else if (m.size() >= 8 && m[4] == 0x26 && m[5] < kStrips) {
            // Strip mode per strip: on = the fader follows the strip's fader (27); off = back to the track's volume.
            const int s = m[5];
            g_stripFader[s] = m[6] != 0;
            g_stripPan[s] = m.size() >= 9 && m[7] == 1;
            if (!g_stripFader[s] && g_volPb[s] >= 0 && !g_touched[s]) driveFader(s, uint16_t(g_volPb[s].load()));
        } else if (m.size() >= 9 && m[4] == 0x27 && m[5] < kStrips) {
            const int s = m[5];
            if (g_stripFader[s] && !g_touched[s]) driveFader(s, uint16_t((m[6] << 7) | m[7]));
        } else if (m.size() >= 7 && m[4] == 0x0D && m[5] < kStrips) {
            // A send level on the V-pot (the selected track's sends): "Send" left, the level right.
            std::string v(m.begin() + 6, m.end() - 1);
            if (v.size() > 14) v.resize(14);
            std::string line = "Send";
            line.append(19 - line.size() - v.size(), ' ');
            line += v;
            g_panLine[m[5]] = line;
            if (g_focus < 0) g_dev.send(uf8::buildValueLine(uint8_t(m[5]), line));
        } else if (m.size() >= 8 && m[4] == 0x0E && m[5] < kStrips) {
            // A send level 0..127 on the V-pot: a unipolar bar.
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
        g_volPb[s] = v;
        if (g_touched[s] || g_stripFader[s]) return;   // hand wins; strip mode: the strip's fader drives it
        driveFader(s, v);
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
        if (msg.size() < 6 || msg[0] != 0xF0 || (msg[4] != 0x18 && msg[4] != 0x19)) return;
        std::lock_guard<std::mutex> lk(g_outMx);
        try { g_out.send(msg); } catch (const RtMidiError& e) { std::fprintf(stderr, "midi out: %s\n", e.getMessage().c_str()); }
    });
    if (!g_helperOpen) std::fprintf(stderr, "Helper ports not available: the UF8 does not follow the selection, and its record keys stay dark.\n");
    const bool uf1Ports = cs::uf1Start();
    const bool uc1Ports = cs::uc1Start();
    if (!uf8Ports && !uf1Ports && !uc1Ports) {
        std::fprintf(stderr, "No MIDI ports could be made (is loopMIDI installed?). Exiting.\n");
        return 1;
    }
    std::printf("Bridging; Ctrl+C to stop.\n");
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
            if (key == "uf1-eq-poll") { cs::uf1SendEqPoll(); return; }
            if (key == "test-skip-same-track") { cs::uf1SendEqPoll(); cs::uc1SendSkipSame(); std::printf("Turn test: repeated track reports %s\n", cs::settings::get(key, "on") == "on" ? "skipped" : "rescanned"); return; }
            if (key.rfind("uc1-key-", 0) == 0) { cs::uc1SendKeyJobs(); return; }
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
        std::this_thread::sleep_for(std::chrono::milliseconds(400));   // let the frames go out
        g_uf8Running = false;
        g_dev.close();
    }
    cs::settingsServer::stop();
    cs::uf1Stop();
    cs::uc1Stop();
    return 0;
}
