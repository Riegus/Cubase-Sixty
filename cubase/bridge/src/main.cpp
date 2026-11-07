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

#include "Mapping.h"
#include "Protocol.h"
#include "UF8Device.h"
#include "Uf8Fader.h"
#include "Uf8Frames.h"
#include "Uf1Bridge.h"
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
bool softKeyLive(int bank, int slot) { const std::string l = kSoftKeyLabels[bank][slot]; return !l.empty() && l != "A/B" && l != "HQ MODE"; }
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

// Bank-right by the user (UF8 key or UF1 BANK): stop at the bank where the last track is on strip 8,
// instead of a bank with empty strips at the end. The script reports the bank's track count and position
// (SysEx 12). Near the end, the bridge sends the needed single-track shifts (the Page-right note) instead
// of a bank step; on the last full bank it does nothing. Unknown count or position: a normal bank step.
std::atomic<int> g_bankTotal{-1}, g_bankOffset{-1};
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

void handleFrame(const uf8frames::Frame& fr)
{
    const uint8_t* d = fr.b;
    switch (d[1]) {
    case 0x21: {   // fader position: FF 21 03 strip lsb msb ck
        const uint8_t s = d[3];
        if (s >= kStrips || !g_touched[s]) return;   // only a hand moves a fader to Cubase
        const uint16_t pb14 = uint16_t((d[4] & 0x7F) | ((d[5] & 0x7F) << 7));
        const auto r = uf8fader::read(pb14, (d[4] & 0x80) != 0, /*grabValid=*/false);
        if (!r.usable) return;
        const uint16_t v = pbToMidi14(r.pbOut);
        sendMidi({uint8_t(0xE0 | s), uint8_t(v & 0x7F), uint8_t((v >> 7) & 0x7F)});
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
        break;
    }
    case 0x22: {   // button: FF 22 03 id 00 state ck
        const bool pressed = d[5] == 0x01;
        if (d[3] == 0x54) {   // FLIP: faders on pan, V-pots on volume (script sub-pages, notes 77 / 78)
            if (pressed) {
                static bool flip = false;
                flip = !flip;
                note(flip ? 78 : 77, true);
                note(flip ? 78 : 77, false);
                sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::Flip, flip));
            }
            break;
        }
        if (d[3] >= 0x68 && d[3] <= 0x6D) {   // V-POT, 1-5: the soft-key page
            if (pressed) { g_skBank = d[3] - 0x68; paintSoftKeys(); }
            break;
        }
        if (d[3] >= 0x18 && d[3] <= 0x1F) {   // top soft key: focus / unfocus its parameter
            if (!pressed) break;
            const int slot = d[3] - 0x18, key = g_skBank * 8 + slot;
            if (!softKeyLive(g_skBank, slot)) break;
            const int f = g_focus == key ? -1 : key;
            g_focus = f;
            if (f < 0) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x21, 0x7F, 0x7F, 0xF7});
            else sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x21, uint8_t(f / 8), uint8_t(f % 8), 0xF7});
            if (g_trace8) std::printf("UF8 soft key: %s\n", f < 0 ? "focus off (pan)" : kSoftKeyLabels[f / 8][f % 8]);
            paintSoftKeys();
            paintVpots();
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
            // which started the transport. Ignore the foot switches for the first seconds.
            static const auto t0 = std::chrono::steady_clock::now();
            const bool foot = (*n == 62 || *n == 63);
            if (foot && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4)) break;
            if (*n == kNoteBankRight && userBankRight(pressed)) break;
            note(*n, pressed);
            if (pressed && *n >= kNoteAutoOff && *n <= kNoteAutoTouch) cs::autoModePressed(*n);
        }
        break;
    }
    case 0x24: {   // v-pot: FF 24 02 strip raw ck
        const uint8_t s = d[3];
        if (s >= kStrips) return;
        const int delta = uf8frames::vpotDelta(d[4]);
        if (delta == 0) break;
        if (g_focus >= 0) sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x22, s, uint8_t(std::clamp(64 + delta, 1, 127)), 0xF7});   // the focused parameter
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
    const int bank = g_skBank, focus = g_focus;
    for (int s = 0; s < kStrips; ++s) {
        const bool live = softKeyLive(bank, s);
        sendLedFrames(uf8::buildTopSoftKeyLed(uint8_t(s), !live ? uf8::TopSoftKeyState::Off
            : focus == bank * 8 + s ? uf8::TopSoftKeyState::On : uf8::TopSoftKeyState::Dim, uf8::ledColourWhite()));
        g_dev.send(uf8::buildPluginSlotName(uint8_t(s), kSoftKeyLabels[bank][s]));
    }
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
void paintSelMode()
{
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

void onMidiIn(std::vector<unsigned char>& msg)
{
    const auto& m = msg;
    if (m.empty()) return;
    if (cs::scriptLogSysex(m.data(), m.size())) return;
    // Trace: when each strip's name arrives (timing of bank changes).
    if (g_trace8 && m.size() >= 7 && m[0] == 0xF0 && m[4] == 0x01)
        std::printf("%lld UF8 name strip %d: %s\n", traceMs(), int(m[5]) + 1, std::string(m.begin() + 6, m.end() - 1).c_str());
    {
        // Names / colours / pan / levels by (command, strip); faders by channel; LED notes by note number.
        std::lock_guard<std::mutex> lk(g_uf8CacheMx);
        const uint8_t st = m[0] & 0xF0;
        if (m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x7D && (m[4] == 0x01 || m[4] == 0x02 || m[4] == 0x06 || m[4] == 0x07 || m[4] == 0x0C))
            g_uf8Cache[0x10000 + (m[4] << 8) + m[5]] = m;
        else if (st == 0xE0 && m.size() >= 3) g_uf8Cache[0x20000 + (m[0] & 0x0F)] = m;
        else if ((st == 0x90 || st == 0x80 || st == 0xA0) && m.size() >= 3) g_uf8Cache[m[1]] = m;
    }
    if (g_uf8Frozen) {
        // Frozen (jump running): drop the strip state (cached above, shown after the jump) and meters;
        // the script's requests still go through.
        const uint8_t st = m[0] & 0xF0;
        const bool stripSysex = m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x7D && (m[4] == 0x01 || m[4] == 0x02 || m[4] == 0x06 || m[4] == 0x07 || m[4] == 0x0C);
        if (stripSysex || st == 0xE0 || st == 0x90 || st == 0x80 || st == 0xA0 || st == 0xB0) return;
    }
    const uint8_t status = m[0] & 0xF0;

    if (m[0] == 0xF0) {   // SysEx: names and colours
        g_cubaseSpoke = true;
        if (!g_cubaseFirstMs) g_cubaseFirstMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (auto n = parseName(m)) {
            std::string t(n->text.begin(), n->text.end());
            if (t.size() > uf8::kUf8ScribbleChars) t.resize(uf8::kUf8ScribbleChars);
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
            std::string name = kSoftKeyLabels[f / 8][f % 8];
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
        if (s >= kStrips || g_touched[s]) return;   // hand wins
        const uint16_t v = uint16_t((m[1] & 0x7F) | ((m[2] & 0x7F) << 7));
        std::vector<std::vector<uint8_t>> frames;
        { std::lock_guard<std::mutex> lk(g_faderMx); frames = uf8fader::drive(uint8_t(s), midi14ToPb(v), g_engaged[s]); }
        for (auto& f : frames) g_dev.send(std::move(f));
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

int main(int argc, char** argv)
{
    cs::redirectTempToLogs(argc > 0 ? argv[0] : nullptr);
    cs::bridgeLogInit(argc > 0 ? argv[0] : nullptr);
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

    // Each surface is optional: it is skipped (with a message) if its ports or device are missing.
    bool uf8Ok = false;
    {
        // The device sometimes is not ready right after the previous bridge let go of it: try three times.
        bool opened = g_dev.open();
        for (int attempt = 2; !opened && attempt <= 3; ++attempt) {
            std::fprintf(stderr, "UF8: could not open the device (%s); trying again (%d of 3)...\n", g_dev.lastError().c_str(), attempt);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            opened = g_dev.open();
        }
        if (!opened) {
            std::fprintf(stderr, "UF8: could not open the device: %s\n"
                                 "Is SSL 360 (or SSL 360 Core) still running? Quit it first.\n",
                         g_dev.lastError().c_str());
        } else if (!g_out.open(kPortToCubase) || !g_in.open(kPortFromCubase, &onMidiIn)) {
            std::fprintf(stderr, "UF8: could not open the MIDI ports \"%s\" / \"%s\"; skipping the UF8.\n",
                         kPortToCubase, kPortFromCubase);
            g_dev.close();
        } else {
            g_dev.setRawInputHandler(onUsbInput);
            std::printf("UF8 open (serial %s).\n", g_dev.serial().c_str());
            // The helper's ports: its channel names go straight on to the UF8 script.
            g_helperOpen = g_helperOut.open(kHelperTo) && g_helperIn.open(kHelperFrom, [](std::vector<unsigned char>& msg) {
                if (cs::scriptLogSysex(msg.data(), msg.size())) return;
                if (msg.size() >= 7 && msg[0] == 0xF0 && msg[4] == 0x1D) { cs::uf1SetSoloActive(msg[5] != 0); return; }   // anything soloed
                if (msg.size() < 6 || msg[0] != 0xF0 || (msg[4] != 0x18 && msg[4] != 0x19)) return;
                std::lock_guard<std::mutex> lk(g_outMx);
                try { g_out.send(msg); } catch (const RtMidiError& e) { std::fprintf(stderr, "midi out: %s\n", e.getMessage().c_str()); }
            });
            if (!g_helperOpen) std::fprintf(stderr, "Helper ports not available: the UF8 does not follow the selection, and its record keys stay dark.\n");
            uf8Ok = true;
            g_uf8Running = true;
            cs::autoModePressed(0);   // automation mode keys dim from the start (the mode is not known yet)
            paintSelMode();
            paintSoftKeys();
        }
    }
    const bool uf1Ok = cs::uf1Start();
    bool uc1Ok = cs::uc1Start();
    if (!uf8Ok && !uf1Ok && !uc1Ok) {
        std::fprintf(stderr, "Nothing to bridge. Exiting.\n");
        return 1;
    }
    std::printf("Bridging; Ctrl+C to stop.\n");
    if (uf8Ok && !cs::midiPortsAreVirtual()) {
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
    if (uf1Ok) {
        bool lamp = false;
        for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--lamptest") lamp = true;
        if (lamp) std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(6)); cs::uf1LampTest(); }).detach();
    }

    while (g_run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (cs::uf1NeedsReopen()) {
            std::fprintf(stderr, "UF1 stopped answering (%s); reopening...\n", cs::uf1LastError().c_str());
            if (!cs::uf1Reopen()) { std::fprintf(stderr, "UF1 could not be reopened; exiting (restart the bridge).\n"); break; }
        }
        if (uc1Ok && cs::uc1NeedsReopen()) {
            std::fprintf(stderr, "The UC1 stopped answering (%s); reopening...\n", cs::uc1LastError().c_str());
            if (!cs::uc1Reopen()) { std::fprintf(stderr, "The UC1 could not be reopened; it stays dark until the bridge is restarted.\n"); uc1Ok = false; }
        }
        if (uf8Ok && g_dev.needsReopen()) {
            // Reopen it (three tries) and replay the last state Cubase sent; the MIDI ports stay as they are.
            std::fprintf(stderr, "The UF8 stopped answering (%s); reopening...\n", g_dev.lastError().c_str());
            g_dev.close();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            bool reopened = false;
            for (int attempt = 1; attempt <= 3 && !reopened; ++attempt) {
                if (attempt > 1) std::this_thread::sleep_for(std::chrono::seconds(2));
                reopened = g_dev.open();
            }
            if (!reopened) {
                std::fprintf(stderr, "The UF8 could not be reopened (%s); exiting (restart the bridge).\n", g_dev.lastError().c_str());
                g_uf8Running = false;
                uf8Ok = false;
                break;
            }
            g_dev.setRawInputHandler(onUsbInput);
            { std::lock_guard<std::mutex> lk(g_faderMx); for (auto& e : g_engaged) e = false; }
            g_meterShown = {};
            replayUf8Cache();
            cs::autoModePressed(0);
            paintSelMode();
            paintSoftKeys();
            std::printf("UF8 reopened.\n");
        }
    }
    if (uf8Ok) g_dev.close();
    cs::uf1Stop();
    cs::uc1Stop();
    return 0;
}
