//
// UF1 <-> Cubase. The UF1 is a single channel strip, so everything uses MIDI channel 0:
//   fader  : pitch bend (touch = note 104)       transport: notes 86/87/91..95
//   SOLO/CUT/SEL: notes 8 / 16 / 24              V-pot pushes: notes 32..37
//   encoders: CC 16 + id, relative signed-bit    LEDs: same notes coming back
// The Cubase script binds all of this to the SELECTED track.
//

#include "Uf1Bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "RtMidi.h"
#include "MidiPorts.h"

#include "Mapping.h"
#include "UF1Device.h"
#include "UF1Protocol.h"
#include "Uf1Pacer.h"
#include "ScriptLog.h"
#include "Uf1Meter.h"
#include "SslCoreImpersonator.h"
#include "Uf1EqCurve.h"
#include "SslPresets.h"

namespace cs {
void paintSoloActive();   // below, outside the anonymous namespace
bool netInit();      // NetInit.cpp
int stopSslCore();   // SslCoreGuard.cpp
namespace {

constexpr const char* kPortTo   = "Cubase-Sixty UF1 to Cubase";
constexpr const char* kPortFrom = "Cubase-Sixty UF1 from Cubase";
// A Mackie Control device for what the MIDI Remote API cannot do well (scrubbing). Set up in Cubase as a
// "Mackie Control" remote device with these ports. Only the jog (CC 60) and the SCRUB key (note 101) are
// used; everything else Cubase sends to it is ignored.
constexpr const char* kMcuTo   = "Cubase-Sixty MCU to Cubase";
constexpr const char* kMcuFrom = "Cubase-Sixty MCU from Cubase";

::uf1::UF1Device g_dev;
MidiOutPort g_out;
MidiOutPort g_mcuOut;
void mcuSend(std::vector<unsigned char> m) { g_mcuOut.send(m); }
MidiInPort g_mcuIn;
bool g_mcuOpen = false;
MidiInPort  g_in;
std::mutex g_outMx;
std::atomic<bool> g_touched{false};
std::atomic<bool> g_open{false};
std::atomic<bool> g_trace{false};
bool g_engaged = false;
std::mutex g_engMx;
std::vector<uint8_t> g_tcShown;               // last time-field payload sent
// The last display / LED message from Cubase per kind, replayed after the UF1 is reopened (Cubase only
// sends what changes, so a reopened UF1 would otherwise stay blank).
std::mutex g_cacheMx;
std::map<int, std::vector<unsigned char>> g_cache;
std::unordered_set<uint8_t> g_ledEnabled;   // LEDs that already got their FF3B enable
std::mutex g_ledMx;

void traceMidi(const char* dir, const std::vector<unsigned char>& m)
{
    if (!g_trace) return;
    std::printf("[UF1 MIDI %s]", dir);
    for (auto b : m) std::printf(" %02X", b);
    std::printf("\n");
}

void sendMidi(std::vector<unsigned char> m)
{
    traceMidi("out", m);
    std::lock_guard<std::mutex> lk(g_outMx);
    try { g_out.send(m); } catch (const RtMidiError& e) { std::fprintf(stderr, "uf1 midi out: %s\n", e.getMessage().c_str()); }
}
void note(uint8_t n, bool on) { sendMidi({uint8_t(on ? 0x90 : 0x80), n, uint8_t(on ? 127 : 0)}); }

int g_bypassState = 0;   // from the script (note 62): 0 no inserts, 1 inserts active, 2 all bypassed
bool g_shift = false, g_sends58 = false, g_master = false, g_flip = false, g_scrub = false, g_shuttle = false;   // UF1 key states (touched on the USB thread)
bool g_modeHeld = false;     // MODE held: the four display soft keys show the view picker
// The MODE view: the soft key that picks it (0 PLUGIN = V-pots on Quick Controls, 1 DAW = V-pots on the sends,
// 2 METER = the SSL Meter plug-in's Overview screen (Uf1Meter.cpp), 3 SENDS = V-pots on the sends and the four
// sends shown in the display's V-pot cells).
enum { kViewPlugin = 0, kViewDaw = 1, kViewMeter = 2, kViewSends = 3 };
std::atomic<int> g_view{kViewDaw};
std::atomic<bool> g_meterReset{false};   // METER: the RESET soft key was pressed (the cycle thread takes it)
std::atomic<bool> g_playing{false};      // Cubase's transport runs (the PLAY LED note from the script)
// PLUGIN view: the SSL plug-in on the selected track, as the script reports it (SysEx 50). While one is there, the
// V-pots, the display soft keys and ◄ ► go to the script as SysEx 53 instead of Quick Controls / track steps.
std::atomic<bool> g_plugPresent{false}, g_plugEqIn{false};
std::atomic<int> g_plugPage{0}, g_plugPages{1}, g_plugOrd{0};
// FINE (quick key 2, as in Rea-Sixty): the V-pots move a quarter as far per detent. Shown in the header's FINE
// CTRL field (the UF1 prints "FINE CTRL 2" over the key itself).
std::atomic<bool> g_fine{false};
// Something in the project is soloed (from the helper script, through main.cpp): the firmware's "SOLO ACTIVE"
// under its SOLO CLR 1 caption (cell 0x0120, Rea-Sixty's kSoloActive: non-zero = shown) and quick key 1 lit.
std::atomic<bool> g_soloActive{false};
// The preset browser (SHIFT + 5-8, Rea-Sixty's channel-view browser): the SSL plug-in's presets in the big display,
// V-pot 4 scrolls, its push loads, soft key 3 goes back. Drawn by the cycle thread (see browserFrame).
std::atomic<bool> g_presetsOpen{false};
std::atomic<int> g_browserScroll{0};        // V-pot 4 detents not yet applied
std::atomic<bool> g_browserLoad{false};     // V-pot 4 pushed
void plugEvent(uint8_t kind, uint8_t a, uint8_t b)
{
    sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x53, kind, a, b, uint8_t(g_fine ? 1 : 0), 0xF7});
}
bool plugActive() { return g_view == kViewPlugin && g_plugPresent; }
void paintHeader(bool force = false);
void paintCsType(bool force);
void paintEq(bool force);
long long nowMs();
void paintSendCells(bool force = false);
void startIdleCycle();
// V-pots 1-4 (sends / Quick Controls) are absolute in Cubase: the bridge keeps each one's position (0..16383,
// from Cubase's SysEx 17), adds the turns itself and sends the new position as a 14-bit CC (17-20 + 49-52).
// Like Rea-Sixty, which sets the value in REAPER itself: what is drawn is exactly what Cubase gets, with no
// round trip in between (the relative steps went to Cubase and back, and Cubase's answers lagged and
// wobbled).
std::array<int, 4> g_vpotPos{-1, -1, -1, -1};
std::array<long long, 4> g_vpotOwnUntil{};
constexpr int kVpotStep = 82;   // per detent: half a percent of the range (what the relative steps gave)

struct SendCell { std::string name, value; uint8_t pos = 0; bool used = false; };
std::string sendCellLine(const SendCell& c);
std::unordered_set<uint8_t> g_shiftUsed;                      // keys pressed with SHIFT, until released
void setLed(const uf1m::Led& led, bool on);
void showStripNumber();
void paintSoftKeys();
// The V-pots' page, from the view and 5-8: 53 / 54 = sends 1-4 / 5-8, 88 / 89 = Quick Controls 1-4 / 5-8.
void sendVpotPage_(int view)
{
    const uint8_t n = view == kViewPlugin ? (g_sends58 ? 89 : 88) : (g_sends58 ? 54 : 53);
    note(n, true);
    note(n, false);
}
void sendVpotPage() { sendVpotPage_(g_view); }

// Shuttle: speed index -7..7 (0 = stopped). Negative = reverse. Notes 65..71 = reverse 8x .. 1/8x,
// 72..78 = forward 1/8x .. 8x (Cubase's "Shuttle Play" commands); stop = the STOP key's note.
int g_shuttleSpeed = 0;
void shuttleSend()
{
    uint8_t n;
    if (g_shuttleSpeed == 0) n = uf1m::kNoteStop;
    else if (g_shuttleSpeed < 0) n = uint8_t(65 + (g_shuttleSpeed + 7));   // -7 -> 65 (reverse 8x), -1 -> 71
    else n = uint8_t(71 + g_shuttleSpeed);                               //  1 -> 72 (1/8x),  7 -> 78 (8x)
    note(n, true);
    note(n, false);
}
void shuttleStep(int dir)
{
    const int next = std::max(-7, std::min(7, g_shuttleSpeed + dir));
    if (next == g_shuttleSpeed) return;
    g_shuttleSpeed = next;
    shuttleSend();
}
void shuttleStop()
{
    if (g_shuttleSpeed != 0) { g_shuttleSpeed = 0; shuttleSend(); }
}
void showScrubLed()
{
    setLed(uf1m::Led{0x0F, g_shuttle ? 0xFF8000u : 0xFFFFFFu}, g_scrub || g_shuttle);
}

// Jog modes (Rea-Sixty's jog objects): what the jog wheel and the nav cross work on. Hold SCRUB and turn the jog
// to pick one (the header's first field shows it); a tap on SCRUB still turns scrubbing on / off. Scrub is a mode
// of its own in the ring, kept as g_scrub (Cubase's Mackie scrub follows it); g_jogMode is the other object.
// The cross selects, the jog moves (Cubase's Nudge / Navigate / fade commands, MIDI channel 2 notes 30-55 to the
// UF1 script). Playhead and Scrub keep the zoom cross.
enum { kJogPlayhead = 0, kJogScrub, kJogItems, kJogEnvelope, kJogRange, kJogFades, kJogModes };
const char* const kJogNames[kJogModes] = { "Playhead", "Scrub", "Items", "Envelope", "Range", "Fades" };
std::atomic<int> g_jogMode{kJogPlayhead};
bool g_scrubHeld = false, g_scrubTurned = false;
std::atomic<long long> g_jogShowUntil{0};   // the mode stays in the header (also in PLUGIN) until then
int g_rangeAim = 0;          // 0 the whole range, 1 left, 2 right edge (Nudge Top / Bottom did nothing in the Project
                             // window, so ↑ ↓ aim at nothing)
int g_fadeAim = 0;           // 0 fade-in, 1 fade-out
bool g_fadeWalk = false;     // Fades: the cross walks the events instead of aiming
bool g_envPlayhead = false;  // Envelope: the jog is on the play cursor instead of the points
bool g_itemsZoomed = false;  // Items: the centre key zoomed to the selection (the next press zooms back)
int jogShown() { return g_scrub ? kJogScrub : g_jogMode.load(); }
void cmd2(uint8_t n)   // one of the UF1 script's channel-2 commands
{
    sendMidi({0x91, n, 127});
    sendMidi({0x81, n, 0});
}
// The nav cross's LEDs show what is aimed at: the range edge, the fade, walking (centre), the play cursor in
// Envelope (centre). Order up, left, centre, right, down (LEDs 0x10-0x14, Rea-Sixty).
void paintNavLeds()
{
    int lit = -1;
    const int mode = jogShown();
    if (mode == kJogRange) { static const int byAim[3] = {2, 1, 3}; lit = byAim[g_rangeAim]; }
    else if (mode == kJogFades) lit = g_fadeWalk ? 2 : g_fadeAim == 0 ? 1 : 3;
    else if (mode == kJogEnvelope && g_envPlayhead) lit = 2;
    static int shown = -2;
    if (lit == shown) return;
    shown = lit;
    for (int i = 0; i < 5; ++i) setLed(uf1m::Led{uint8_t(0x10 + i), 0xFFFFFF}, i == lit);
}
void setScrub(bool on)
{
    if (on == g_scrub) return;
    g_scrub = on;
    if (on && g_shuttle) { g_shuttle = false; shuttleStop(); }
    if (g_mcuOpen) { mcuSend({0x90, 0x65, 0x7F}); mcuSend({0x90, 0x65, 0x00}); }   // Cubase's scrub follows
    showScrubLed();
}
void showJogMode()
{
    g_jogShowUntil = nowMs() + 2000;
    paintHeader();
    paintNavLeds();
    if (g_trace) std::printf("UF1 jog mode: %s\n", kJogNames[jogShown()]);
    std::thread([] { std::this_thread::sleep_for(std::chrono::milliseconds(2100)); if (nowMs() >= g_jogShowUntil) paintHeader(); }).detach();
}
void pickJogMode(int step)
{
    const int next = std::clamp(jogShown() + step, 0, kJogModes - 1);
    if (next == kJogScrub) setScrub(true);
    else { setScrub(false); g_jogMode = next; }
    showJogMode();
}
// One jog step in the current mode (not scrub / shuttle). Returns false for Playhead (the caller nudges the cursor).
bool jogStep(bool right)
{
    switch (g_jogMode.load()) {
    case kJogItems: cmd2(right ? 31 : 30); return true;
    case kJogEnvelope:
        // The points in time (Nudge Left / Right). Cubase has no command for their value: Nudge Up / Down moves
        // them to another automation lane (tested).
        if (g_envPlayhead) return false;
        cmd2(right ? 31 : 30);
        return true;
    case kJogRange: {
        static const uint8_t left[3] = {30, 34, 36}, rightN[3] = {31, 35, 37};
        cmd2(right ? rightN[g_rangeAim] : left[g_rangeAim]);
        return true;
    }
    case kJogFades: cmd2(g_fadeAim == 0 ? (right ? 43 : 42) : (right ? 45 : 44)); return true;
    default: return false;
    }
}
// A nav cross key in the current mode. Returns false where the cross zooms (Playhead, Scrub; the caller sends it).
bool jogNav(uint8_t id)   // 0x28 up, 0x29 left, 0x2A centre, 0x2B right, 0x2C down; on press
{
    const int mode = jogShown();
    auto navigate = [&](bool addOk) {   // Navigate Up / Left / Right / Down (SHIFT: add to the selection)
        const uint8_t n = id == 0x28 ? 48 : id == 0x29 ? 46 : id == 0x2B ? 47 : 49;
        cmd2(uint8_t(n + (addOk && g_shift ? 4 : 0)));
    };
    switch (mode) {
    case kJogItems:
        if (id == 0x2A) { cmd2(g_itemsZoomed ? 55 : 54); g_itemsZoomed = !g_itemsZoomed; }
        else { navigate(true); g_itemsZoomed = false; }
        return true;
    case kJogEnvelope:
        if (id == 0x2A) g_envPlayhead = !g_envPlayhead;
        else navigate(id == 0x29 || id == 0x2B);
        break;
    case kJogRange:
        if (id == 0x28 || id == 0x2C) return true;
        g_rangeAim = id == 0x2A ? 0 : id == 0x29 ? 1 : 2;
        break;
    case kJogFades:
        if (id == 0x2A) g_fadeWalk = !g_fadeWalk;
        else if (g_fadeWalk) navigate(true);
        else if (id == 0x29 || id == 0x2B) g_fadeAim = id == 0x2B ? 1 : 0;
        break;
    default: return false;
    }
    paintNavLeds();
    return true;
}

void onUf1Event(const ::uf1::InputEvent& ev)
{
    using K = ::uf1::InputKind;
    if (g_trace && ev.kind != K::FaderPosition)
        std::printf("%lld [UF1 event] kind=%d id=0x%02X pressed=%d delta=%d\n", nowMs() % 100000, int(ev.kind), ev.id, int(ev.pressed), ev.delta);
    switch (ev.kind) {
    case K::FaderTouch:
        g_touched = ev.pressed;
        if (ev.pressed) {
            { std::lock_guard<std::mutex> lk(g_engMx); g_engaged = false; }
            g_dev.sendPriority(::uf1::buildMotorEnable(false));
        }
        note(uf1m::kNoteFaderTouch, ev.pressed);
        break;
    case K::FaderPosition: {
        if (!g_touched) break;   // otherwise it is only the motor reporting back
        const uint16_t v = uf1m::pos15ToMidi14(ev.position);
        sendMidi({0xE0, uint8_t(v & 0x7F), uint8_t((v >> 7) & 0x7F)});
        g_dev.send(::uf1::buildMotorPosition(ev.position));   // keep the target under the hand
        break;
    }
    case K::EncoderRotate:
        if (ev.delta == 0 || ev.id > 6) break;
        // The channel encoder (5) and the jog wheel (6) are sent as note pulses: one per detent, 40/41 = channel
        // encoder left/right, 42/43 = jog left/right. The script binds them to Cubase commands.
        if (ev.id == 5 || ev.id == 6) {
            // Slow the encoders down: one pulse per kDetentsPerPulse detents (the remainder is kept between events).
            static int acc[2] = {0, 0};
            const int kDetentsPerPulse[2] = {4, 4};          // channel encoder, jog wheel: raise to slow down
            const int k = ev.id - 5;
            // SCRUB: the jog nudges the cursor on every detent (fast positioning). Cubase's own Jog Left / Right
            // commands were tried for this and played choppy, jumpy audio.
            const int perPulse = (ev.id == 6 && g_scrub && !g_shift && !g_scrubHeld) ? 1 : kDetentsPerPulse[k];
            if ((acc[k] > 0) != (ev.delta > 0)) acc[k] = 0;  // direction change restarts the count
            acc[k] += ev.delta;
            // Jog: nudge the cursor (42 / 43); with SHIFT held, previous / next marker (79 / 80); in SCRUB,
            // Cubase's Jog Left / Right (63 / 64); in SHUTTLE, each step changes the shuttle speed.
            const bool jog = ev.id == 6;
            const uint8_t base = !jog ? 40 : g_shift ? 79 : 42;
            while (acc[k] >= perPulse || acc[k] <= -perPulse) {
                const bool right = acc[k] > 0;
                acc[k] += right ? -perPulse : perPulse;
                if (jog && g_scrubHeld) { g_scrubTurned = true; pickJogMode(right ? 1 : -1); continue; }   // picking a mode
                if (jog && g_shuttle && !g_shift) { shuttleStep(right ? 1 : -1); continue; }
                if (jog && g_scrub && !g_shift && g_mcuOpen) {
                    // Mackie jog wheel, one tick: CC 60, 0x01 = clockwise, 0x41 = counter-clockwise. In
                    // Cubase's scrub mode (SCRUB key, below) this scrubs the audio.
                    mcuSend({0xB0, 0x3C, uint8_t(right ? 0x01 : 0x41)});
                    continue;
                }
                if (jog && !g_scrub && !g_shift && jogStep(right)) continue;
                note(uint8_t(base + (right ? 1 : 0)), true);
                note(uint8_t(base + (right ? 1 : 0)), false);
            }
            break;
        }
        if (ev.id >= 1 && ev.id <= 4) {
            if (g_presetsOpen) { if (ev.id == 4) g_browserScroll += ev.delta; break; }   // the browser: V-pot 4 scrolls
            if (g_view == kViewMeter) break;   // METER: the V-pots have no meter settings yet (and would change sends unseen)
            if (plugActive()) { plugEvent(0, uint8_t(ev.id - 1), uint8_t(std::clamp(64 + ev.delta, 1, 127))); break; }
            const size_t k = size_t(ev.id - 1);
            if (g_vpotPos[k] < 0) break;   // position not known yet (comes from Cubase within a second)
            const int v = std::clamp(g_vpotPos[k] + ev.delta * (g_fine ? kVpotStep / 4 : kVpotStep), 0, 16383);
            g_vpotPos[k] = v;
            g_vpotOwnUntil[k] = nowMs() + 500;   // Cubase's echoes of our own turns are not taken back meanwhile
            const uint8_t cc = uint8_t(uf1m::kCcEncoder + ev.id);
            sendMidi({0xB0, cc, uint8_t((v >> 7) & 0x7F)});
            sendMidi({0xB0, uint8_t(cc + 32), uint8_t(v & 0x7F)});
            if (g_view == kViewSends || g_view == kViewPlugin) paintSendCells(false);
            break;
        }
        sendMidi({0xB0, uint8_t(uf1m::kCcEncoder + ev.id), vpotToCc(ev.delta)});
        break;
    case K::Button:
        // SHIFT is a modifier held in the bridge. SHIFT + the transport soft keys and Cycle / Click = the
        // automation keys printed on them: < READ (56), > WRITE (57), CYCLE = Cross-Over (58), CLICK = TRIM
        // (59), 1 = LATCH (60), 2 = TOUCH (61). One pulse per press.
        if (ev.id == 0x36) { g_shift = ev.pressed; break; }
        if (g_shift && ev.id >= 0x30 && ev.id <= 0x35) {
            if (ev.pressed) {
                static const uint8_t autoNote[6] = {56, 57, 58, 59, 60, 61};
                note(autoNote[ev.id - 0x30], true);
                note(autoNote[ev.id - 0x30], false);
                if (autoNote[ev.id - 0x30] >= 58) autoModePressed(autoNote[ev.id - 0x30]);
                g_shiftUsed.insert(ev.id);
            }
            break;
        }
        if (!ev.pressed && g_shiftUsed.erase(ev.id)) break;   // release of a key pressed with SHIFT
        // SCRUB toggles the jog between nudging the cursor and Cubase's jog (scrubbing). LED on = scrub.
        // BYPASS (the key above the display). The script reports the state as note 62 (see onMidiIn). A press
        // bypasses every slot when some are active, or turns them all on when all are bypassed; nothing when
        // the track has no inserts. Slots = MIDI channel 2, notes 0..15, velocity 127 = bypass, 0 = active.
        if (ev.id == 0x18) {
            if (!ev.pressed || g_bypassState == 0) break;
            const uint8_t vel = g_bypassState == 1 ? 127 : 0;
            for (uint8_t n = 0; n < 16; ++n) sendMidi({0x91, n, vel});
            break;
        }
        // SHIFT + SCRUB toggles SHUTTLE instead: the jog then sets a shuttle speed (stopping when it goes
        // back through zero). SCRUB and SHUTTLE exclude each other; the LED is white for scrub, orange for shuttle.
        // Held and the jog turned: the jog picks the jog mode (above), and the release does nothing more.
        if (ev.id == 0x27) {
            if (ev.pressed && g_shift) {
                g_shuttle = !g_shuttle;
                if (g_shuttle) { if (g_scrub) setScrub(false); } else shuttleStop();
                showScrubLed();
                break;
            }
            if (ev.pressed) { g_scrubHeld = true; g_scrubTurned = false; break; }
            if (!g_scrubHeld) break;
            g_scrubHeld = false;
            if (g_scrubTurned) break;
            setScrub(!g_scrub);   // a tap: scrub on / off (Cubase's Mackie scrub mode follows)
            paintHeader();
            paintNavLeds();
            break;
        }
        // The nav cross in the jog modes that use it (Items, Envelope, Range, Fades); else zoom (below).
        if (ev.id >= 0x28 && ev.id <= 0x2C && !g_shuttle) {
            if (!ev.pressed) { if (jogShown() != kJogPlayhead && jogShown() != kJogScrub) break; }
            else if (jogNav(ev.id)) break;
        }
        // MODE: while held, the four display soft keys pick the view (as in Rea-Sixty): 1 PLUGIN (V-pots on
        // Cubase's Quick Controls), 2 DAW (V-pots on the sends), 3 METER (the SSL Meter's Overview), 4 SENDS.
        // Releasing MODE closes the picker. The view keeps its 5-8 state (Quick Controls 5-8 / sends 5-8).
        if (ev.id == 0x20) {
            g_modeHeld = ev.pressed;
            paintSoftKeys();
            break;
        }
        if (ev.id >= 0x19 && ev.id <= 0x1C) {
            // METER, without MODE: soft key 2 = RESET (the peak holds and overload marks). FINE and PRESETS: later.
            if (g_presetsOpen && !g_modeHeld) { if (ev.pressed && ev.id == 0x1B) g_presetsOpen = false; break; }   // 3 = Navigate Back
            if (ev.pressed && !g_modeHeld && g_view == kViewMeter && ev.id == 0x1A) { g_meterReset = true; break; }
            // PLUGIN with an SSL plug-in, without MODE: the plug-in's switches on this page.
            if (ev.pressed && !g_modeHeld && plugActive()) { plugEvent(2, uint8_t(ev.id - 0x19), 0); break; }
            if (!ev.pressed || !g_modeHeld) break;
            {
                const int view = ev.id - 0x19;
                g_presetsOpen = false;   // picking a view closes the preset browser
                // The V-pot page goes out on every pick, also of the view already shown: after the scripts are
                // reloaded, Cubase starts on the sends while the bridge still shows the view it had.
                sendVpotPage_(view);
                if (view != g_view) {
                    g_view = view;
                    paintSendCells(true);
                    paintHeader();
                    if (g_trace) std::printf("UF1 view: %s\n", view == kViewPlugin ? "PLUGIN" : view == kViewDaw ? "DAW" : view == kViewMeter ? "METER" : "SENDS");
                }
                paintSoftKeys();
            }
            break;
        }
        // 5-8: the V-pots switch between sends 1-4 (note 53) and 5-8 (note 54). LED on = 5-8.
        if (ev.id == 0x22) {
            if (!ev.pressed) break;
            // SHIFT + 5-8: the preset browser for the track's SSL plug-in (as in Rea-Sixty), in the channel views.
            if (g_shift) {
                g_shiftUsed.insert(ev.id);
                if (g_view != kViewMeter) g_presetsOpen = !g_presetsOpen;
                break;
            }
            if (g_presetsOpen) break;
            g_sends58 = !g_sends58;
            sendVpotPage();
            paintHeader();
            setLed(uf1m::Led{0x0A, 0xFFFFFF}, g_sends58);
            break;
        }
        // MASTER: the strip controls the main output (note 45) or the selected track again (note 44).
        // MASTER (0x39) and FLIP (0x38) pick one of four strip sub-pages: 44 selected track, 45 main output,
        // 46 selected track flipped, 47 main output flipped (flipped = fader on pan, pan knob on volume).
        if (ev.id == 0x39 || ev.id == 0x38) {
            if (!ev.pressed) break;
            if (ev.id == 0x39) g_master = !g_master; else g_flip = !g_flip;
            const uint8_t n = uint8_t(44 + (g_master ? 1 : 0) + (g_flip ? 2 : 0));
            note(n, true);
            note(n, false);
            setLed(uf1m::Led{0x21, 0xFFFFFF}, g_master);
            setLed(uf1m::Led{0x20, 0xFFFFFF}, g_flip);
            showStripNumber();
            break;
        }
        // BANK banks the UF8; ARROW selects the previous / next track (a channel-encoder step, notes 40 / 41).
        if (ev.id == 0x21 || ev.id == 0x23) {
            if (!ev.pressed) break;
            const bool right = ev.id == 0x23;
            if (uf8BankPress(right)) break;
            // No UF8: step the selection eight tracks instead (spaced out; back-to-back pulses were merged).
            std::thread([right] {
                for (int i = 0; i < 8; ++i) {
                    note(uint8_t(right ? 41 : 40), true);
                    note(uint8_t(right ? 41 : 40), false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
            }).detach();
            break;
        }
        if (ev.id == 0x24 || ev.id == 0x26) {
            if (!ev.pressed) break;
            if (g_presetsOpen) break;
            // PLUGIN: ◄ ► page (as in Rea-Sixty): through the SSL plug-in's controls, or without one between
            // Quick Controls 1-4 and 5-8 (as the 5-8 key does). The channel encoder still steps the track.
            if (plugActive() && !g_shift) { plugEvent(3, ev.id == 0x26 ? 1 : 0, 0); break; }
            if (g_view == kViewPlugin && !g_shift) {
                const bool want58 = ev.id == 0x26;
                if (want58 != g_sends58) {
                    g_sends58 = want58;
                    sendVpotPage();
                    setLed(uf1m::Led{0x0A, 0xFFFFFF}, g_sends58);
                    paintHeader();
                }
                break;
            }
            // SHIFT + ARROW: step back / forward one bar (81 / 82).
            const uint8_t n = g_shift ? (ev.id == 0x26 ? 82 : 81) : (ev.id == 0x26 ? 41 : 40);
            note(n, true);
            note(n, false);
            break;
        }
        if (ev.id >= 0x09 && ev.id <= 0x0C && g_presetsOpen) { if (ev.pressed && ev.id == 0x0C) g_browserLoad = true; break; }   // V-pot 4 = Select
        if (ev.id >= 0x09 && ev.id <= 0x0C && plugActive()) { if (ev.pressed) plugEvent(1, uint8_t(ev.id - 0x09), 0); break; }
        // Quick key 1 (without SHIFT): clear every solo (MIDI channel 2, note 20; the script runs Cubase's
        // "Deactivate All Solo").
        if (ev.id == 0x34) {
            sendMidi({uint8_t(ev.pressed ? 0x91 : 0x81), 20, uint8_t(ev.pressed ? 127 : 0)});
            break;
        }
        // Quick key 2 (without SHIFT): FINE on / off.
        if (ev.id == 0x35) {
            if (ev.pressed) {
                g_fine = !g_fine;
                paintHeader();
                setLed(uf1m::Led{0x1D, 0xFFFFFF}, g_fine);   // the key lit while FINE is on
                if (g_trace) std::printf("UF1 FINE %s\n", g_fine ? "on" : "off");
            }
            break;
        }
        if (auto n = uf1m::buttonToNote(ev.id)) note(*n, ev.pressed);
        break;
    default: break;
    }
}

void setLed(const uf1m::Led& led, bool on)
{
    if (g_trace) std::printf("[UF1 LED] id=0x%02X %s\n", led.id, on ? "on" : "off");
    {
        std::lock_guard<std::mutex> lk(g_ledMx);
        if (g_ledEnabled.insert(led.id).second) g_dev.send(::uf1::buildLed(led.id, true));   // FF3B enable, once
    }
    g_dev.send(::uf1::buildColourRgb(led.id, on ? led.rgb : 0u));  // off = black
    g_dev.send(::uf1::buildLedLevel(led.id, on ? 0x00 : 0x11));
}

// SEL LED: always shows the selected track's colour, dimmed; bright while the channel editor is open
// (SEL is bound to the editor). Level 0x11 alone did not dim visibly, so the colour itself is scaled down.
bool g_uf1RightSeen = false;     // a right-side meter value arrived for the current track
bool g_emptySelection = false;   // the last selected-track name was empty (see onSysex)
constexpr uint32_t kSelDimPct = 35;   // dim SEL colour, % of full (20 was invisible, 45 too bright)
uint32_t g_selRgb = 0xFFFFFF;
bool g_selOn = false;
void showSelLed()
{
    const uf1m::Led sel{0x07, 0};
    {
        std::lock_guard<std::mutex> lk(g_ledMx);
        if (g_ledEnabled.insert(sel.id).second) g_dev.send(::uf1::buildLed(sel.id, true));
    }
    uint32_t rgb = g_selRgb;
    if (!g_selOn) {
        const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, bl = rgb & 0xFF;
        rgb = ((r * kSelDimPct / 100) << 16) | ((g * kSelDimPct / 100) << 8) | (bl * kSelDimPct / 100);
    }
    g_dev.send(::uf1::buildColourRgb(sel.id, rgb));
    g_dev.send(::uf1::buildLedLevel(sel.id, 0x00));
}

// SENDS view: the four sends in the display's V-pot cells (Rea-Sixty's cells: 0x010e text per pot with the
// pot index first, 0x010f bars = position 0..100 + brightness 0x80 per pot, 0x010d styles: 0x01 pointer,
// 0x03 empty). The script sends each pot's name, level text and position (SysEx 15) in every view; the
// cells show them only in SENDS (PLUGIN shows its own, below; DAW none).
std::array<SendCell, 4> g_sendCells;
std::mutex g_cellMx;   // the cells are filled on the MIDI thread and drawn from the painter and the USB thread
// PLUGIN view cells and soft keys (SysEx 51 / 52), under g_cellMx: the SSL plug-in's controls on the current
// page, or (no SSL plug-in) the Quick Controls' names and values.
struct PlugCell { std::string name, value; uint8_t pos = 0; bool used = false, bipolar = false; };
std::array<PlugCell, 4> g_plugCells;
struct PlugKey { std::vector<uint8_t> label; uint8_t state = 2; };   // state 0 off, 1 on, 2 no switch
std::array<PlugKey, 4> g_plugKeys;
std::string g_plugModel;   // "CS 2", "4K E", "BC 2" ... ("" = none)
std::string g_plugTitle;   // the plug-in's name in Cubase (for its preset folder)
std::string g_qcSource;    // what the Quick Controls belong to (SysEx 56; "" = not known)
std::string g_trackName;   // the selected track's name (SysEx 01), under g_cellMx
uf1eq::Model g_cubaseEq;   // Cubase's own channel EQ + input filter, from the script (SysEx 55); off = flat

// The four display soft keys (LED ids 0x01-0x04, label cell 0x0104 with the key index first). Key 1's LED
// takes no FF39 frame: Rea-Sixty found that one sticks its rectangle on.
void softKeyLed(int i, uint32_t rgb)
{
    const uint8_t id = uint8_t(0x01 + i);
    {
        std::lock_guard<std::mutex> lk(g_ledMx);
        if (g_ledEnabled.insert(id).second) g_dev.send(::uf1::buildLed(id, true));
    }
    g_dev.send(::uf1::buildColourRgb(id, rgb));
    if (i != 0) g_dev.send(::uf1::buildLedLevel(id, 0x00));
}
void paintSoftKeys()
{
    if (!g_open) return;
    static const char* const kPicker[4] = {"PLUGIN", "DAW", "METER", "SENDS"};
    static const char* const kMeterKeys[4] = {"OVERVIEW", "RESET", "FINE", "PRESETS"};   // the METER view's own keys
    const bool meter = g_view == kViewMeter;
    const bool plugin = plugActive() && !g_modeHeld && !g_presetsOpen;
    const bool browser = g_presetsOpen && !g_modeHeld;
    for (int i = 0; i < 4; ++i) {
        const std::string label = g_modeHeld ? kPicker[i] : browser ? (i == 2 ? "Navigate Back" : "") : meter ? kMeterKeys[i] : "";
        std::vector<uint8_t> p;
        p.push_back(uint8_t(i));
        uint32_t rgb = 0;   // dark
        if (plugin) {
            // The plug-in's switch on this page (0x7F from the script = SSL's Ø glyph, 0xD8 in the UF1's font),
            // lit white while it is on.
            std::lock_guard<std::mutex> lk(g_cellMx);
            for (uint8_t c : g_plugKeys[size_t(i)].label) p.push_back(c == 0x7F ? 0xD8 : c);
            if (g_plugKeys[size_t(i)].state == 1) rgb = 0xFFFFFF;
        } else {
            p.insert(p.end(), label.begin(), label.end());
        }
        g_dev.send(::uf1::buildScreen(::uf1::scr::kSoftKeyLabel, p));
        if (g_modeHeld) {
            const bool on = i == g_view;
            rgb = on ? 0xFFFF00 : 0x595900;   // the active view yellow, the other dim yellow (35 %)
        }
        softKeyLed(i, rgb);
    }
    const uint8_t noHighlight = (meter && !g_modeHeld && !g_presetsOpen) ? 0x01 : 0x00;   // the meter layout's base value is 01
    g_dev.send(::uf1::buildScreen(0x0102, std::span<const uint8_t>(&noHighlight, 1)));
}

// ---------------------------------------------------------------------------
// The send level text while turning. The bar follows the bridge's own position at once, but the dB text
// comes from Cubase and trails behind. Cubase's level curve is the same for every send, so the bridge learns
// it: each position Cubase reports (SysEx 17) is paired with the text Cubase sent just before it (SysEx 15)
// and kept. While a V-pot turns, the text for the bridge's own position is looked up: the text Cubase gave
// for that exact position, or interpolated between two known positions close by (formatted like Cubase:
// two decimals below 10 dB, one above). Where nothing is known nearby, Cubase's own text is shown.
// ---------------------------------------------------------------------------
std::mutex g_levelMx;
std::map<int, std::string> g_levelText;       // position 0..16383 -> Cubase's text for it
std::array<std::string, 4> g_lastCubaseText;  // per pot: Cubase's latest text and when it came
std::array<long long, 4> g_lastCubaseTextMs{};
bool parseDb(const std::string& t, double& db)
{
    char* end = nullptr;
    db = std::strtod(t.c_str(), &end);
    return end != t.c_str() && std::string(end).rfind("dB", 0) == 0;
}
std::string levelTextFor(int pos)
{
    std::lock_guard<std::mutex> lk(g_levelMx);
    if (g_levelText.empty()) return {};
    auto hi = g_levelText.lower_bound(pos);
    if (hi != g_levelText.end() && hi->first == pos) return hi->second;
    if (hi == g_levelText.end() || hi == g_levelText.begin()) return {};
    auto lo = std::prev(hi);
    // Known on both sides: a straight line between them. Up to a quarter of the range apart: falling back
    // to Cubase's own (lagging) text in a gap made the number jump back and forth (seen on video), and a
    // rough value for half a second is better; Cubase's exact value follows when the turning stops.
    if (hi->first - lo->first > 4000) {
        if (pos - lo->first <= 20) return lo->second;
        if (hi->first - pos <= 20) return hi->second;
        return {};
    }
    double a = 0, b = 0;
    if (!parseDb(lo->second, a) || !parseDb(hi->second, b)) return {};
    const double db = a + (b - a) * double(pos - lo->first) / double(hi->first - lo->first);
    char buf[16];
    std::snprintf(buf, sizeof buf, std::fabs(db) < 10.0 ? "%.2fdB" : "%.1fdB", db);
    return buf;
}

std::string sendCellLine(const SendCell& c)
{
    // Rea-Sixty's value line: the name left (11 characters), the value right-aligned in 19.
    std::string name = c.name.substr(0, 11), value = c.value.substr(0, 8);
    std::string line = name;
    line.append(19 - name.size() - value.size(), ' ');
    line += value;
    return line;
}
// ---------------------------------------------------------------------------
// The idle cycle. Rea-Sixty found (uf1CyclePacerLoop_, cap84 / cap101) that the UF1 draws its big display
// continuously only while it gets SSL's cycle without a break: every ~40.8 ms a meter group (0009 000a 0015
// 0016) about 18 ms in and a closing group (011d) at the end, with silences in between that are the
// device's render windows. Without the stream, or with frames landing inside it, the firmware falls back to
// drawing when the traffic pauses: V-pot cells turned smooth at first, then choppy. So the bridge streams
// the channel view's idle cycle from the moment the UF1 is open, and carries the level meter in it; other
// frames sent meanwhile are held by the device class and go out right after each cycle (as SSL does).
// ---------------------------------------------------------------------------
std::atomic<uint8_t> g_meterL{0}, g_meterR{0};
std::atomic<bool> g_cycleRunning{false};
std::atomic<int> g_trackNo{0};   // from the script (SysEx 0E): the selected track's MixConsole position, 0 = unknown
void screenText(uint16_t addr, const std::string& text);
void restoreChannelView();
void put(uint16_t addr, std::initializer_list<uint8_t> p)
{
    g_dev.send(::uf1::buildScreen(addr, std::span<const uint8_t>(std::data(p), p.size())));
}
// METER needs the SSL 360 Core stand-in (shared with the UC1's gain-reduction meters). Started when the view
// opens if the UC1 has not started it; SSL360Core.exe is closed while the view is open (the plug-ins start it
// again when they find no Core, and then most of them connect there instead).
void ensureSslCore()
{
    static long long nextTry = 0, nextGuard = 0;
    const long long now = nowMs();
    if (!sslcore::isRunning() && now >= nextTry) {
        nextTry = now + 2000;
        netInit();
        if (sslcore::start()) std::printf("SSL 360 Core stand-in running (UF1 meter).\n");
        else std::fprintf(stderr, "UF1 meter: the SSL 360 Core stand-in could not start (is SSL 360 running?).\n");
    }
    if (now >= nextGuard) {
        nextGuard = now + 3000;
        std::thread([] { stopSslCore(); }).detach();   // off the cycle thread: the snapshot takes a while
    }
}
// The preset browser (Rea-Sixty's, opened from the channel view): enter = the browser layout (0x0100 = 04 03) with
// the view's own state reset, then every tick the list in the header row (0x011c: field 0 = the plug-in, fields 1-7 a
// window with the selection in the middle row, which the firmware highlights), the graph area cleared (0x0122) and
// the scrollbar (0x011f). V-pot 4 scrolls (3 detents per preset), its push loads the preset into the plug-in and
// closes the browser.
void browserFrame(bool enter)
{
    static std::vector<sslpresets::Entry> list;
    static std::string listFor;
    static int sel = 0;
    static std::map<std::string, int> lastSel;   // per plug-in: the preset last loaded
    static int scrollAcc = 0;
    static int barShown = -1;
    std::string title;
    { std::lock_guard<std::mutex> lk(g_cellMx); title = g_plugPresent ? g_plugTitle : std::string(); }
    if (enter || title != listFor) {
        listFor = title;
        list = title.empty() ? std::vector<sslpresets::Entry>{} : sslpresets::scan(sslpresets::dirFor(title));
        sel = lastSel.count(title) ? lastSel[title] : 0;
        scrollAcc = 0;
        g_browserScroll = 0;
        if (!title.empty()) std::printf("UF1 presets: %zu for \"%s\" (%s)\n", list.size(), title.c_str(), sslpresets::dirFor(title).c_str());
    }
    if (enter) {
        g_browserLoad = false;
        put(0x0100, {0x04, 0x03});
        put(0x010d, {0x0a, 0x06, 0x06, 0x0a});
        put(0x010f, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
        put(0x0101, {0x03});
        put(0x011a, {0x07});
        put(0x011e, {0x10});
        for (uint8_t i = 0; i < 3; ++i) put(0x010e, {i});
        { std::vector<uint8_t> p{0x03}; const char* t = "Select"; p.insert(p.end(), t, t + 6); g_dev.send(::uf1::buildScreen(0x010e, p)); }
        paintSoftKeys();   // Navigate Back on soft key 3, the others blank
        barShown = -1;
    }
    const int n = int(list.size());
    scrollAcc += g_browserScroll.exchange(0);
    while (scrollAcc >= 3) { scrollAcc -= 3; ++sel; }
    while (scrollAcc <= -3) { scrollAcc += 3; --sel; }
    sel = std::clamp(sel, 0, n > 0 ? n - 1 : 0);
    if (g_browserLoad.exchange(false)) {
        if (n > 0) {
            const auto msgs = sslpresets::loadMessages(list[size_t(sel)], 0);
            for (const auto& m : msgs) sendMidi(m);
            lastSel[title] = sel;
            std::printf("UF1 presets: \"%s\" sent (%zu values)\n", list[size_t(sel)].name.c_str(), msgs.empty() ? size_t(0) : msgs.size() - 1);
        }
        g_presetsOpen = false;   // closed after the load (Rea-Sixty), the channel view shows the new values
        return;
    }
    // A row is "<folder>/<name>"; one that does not fit loses its folder first (Rea-Sixty's fitField).
    std::array<uint8_t, 200> row{};
    auto field = [&](int f, const std::string& text) {
        std::string t = sslpresets::latin1(text);
        if (t.size() > 24) { const size_t sl = t.find_last_of('/'); if (sl != std::string::npos) t = t.substr(sl + 1); }
        for (size_t k = 0; k < t.size() && k < 24; ++k) row[size_t(f) * 25 + k] = uint8_t(t[k]);
    };
    field(0, title.empty() ? std::string("PRESETS") : title);
    if (title.empty()) field(1, "(no SSL plug-in)");
    else if (n == 0) field(1, "(no presets)");
    else for (int f = 1; f <= 7; ++f) {
        const int i = sel - 3 + (f - 1);
        if (i < 0 || i >= n) continue;
        const auto& e = list[size_t(i)];
        const size_t sl = e.group.find_last_of('/');
        field(f, e.group.empty() ? e.name : e.group.substr(sl == std::string::npos ? 0 : sl + 1) + "/" + e.name);
    }
    g_dev.send(::uf1::buildScreen(::uf1::scr::kHeaderRow, row));
    const std::array<uint8_t, 251> blank{};
    g_dev.send(::uf1::buildScreen(0x0122, blank));
    const int bar = n > 1 ? int(std::lround(100.0 * sel / (n - 1))) : 0;
    if (bar != barShown) { barShown = bar; const uint8_t b = uint8_t(bar); g_dev.send(::uf1::buildScreen(0x011f, std::span<const uint8_t>(&b, 1))); }
}

void startIdleCycle()
{
    bool expected = false;
    if (!g_cycleRunning.compare_exchange_strong(expected, true)) return;
    std::thread([] {
        auto slot = std::chrono::steady_clock::now();
        bool inMeter = false, inBrowser = false;
        long long flashUntil = 0;
        std::string label;
        while (g_open) {
            // The preset browser owns the big display while it is open: no cycle (as Rea-Sixty pauses its pacer),
            // the browser's frames restated every tick instead.
            if (g_presetsOpen) {
                browserFrame(!inBrowser);
                inBrowser = true;
                slot += std::chrono::microseconds(40800);
                std::this_thread::sleep_until(slot);
                continue;
            }
            if (inBrowser) {
                inBrowser = false;
                if (!inMeter) restoreChannelView();
            }
            // View changes are made here, between two cycles: the entry frames must not land inside one.
            const bool wantMeter = g_view == kViewMeter;
            if (wantMeter != inMeter) {
                inMeter = wantMeter;
                if (inMeter) {
                    ensureSslCore();
                    uf1meter::sendOverviewEntry(g_dev);
                    paintSoftKeys();   // the meter keys' labels (or the picker, while MODE is still held)
                    label.clear();
                    if (g_trace) std::printf("UF1: METER view (Overview)\n");
                } else {
                    restoreChannelView();
                }
            }
            uf1pace::Parts p;
            if (inMeter) {
                ensureSslCore();
                const bool reset = g_meterReset.exchange(false);
                // RESET lights its key for a moment (0x0102 is a bitmask: 01 base, 02 = soft key 2).
                if (reset) { flashUntil = nowMs() + 150; put(0x0102, {0x03}); }
                else if (flashUntil && nowMs() >= flashUntil) { flashUntil = 0; if (!g_modeHeld) put(0x0102, {0x01}); }
                const std::string l = uf1meter::instanceLabel();
                if (l != label) {
                    label = l;
                    std::vector<uint8_t> c{0x00};
                    c.insert(c.end(), l.begin(), l.end());
                    g_dev.send(::uf1::buildScreen(::uf1::scr::kFocusedParam, c));
                }
                uf1meter::buildOverview(p, g_meterL.load(), g_meterR.load(), g_trackNo.load(), !g_playing.load(), reset);
            } else {
                // The EQ graph, every third cycle (~120 ms): a turned EQ knob redraws about as fast as SSL 360's.
                static int eqTick = 0;
                if (++eqTick >= 3) { eqTick = 0; paintEq(false); }
                const uint8_t level[4] = { g_meterL.load(), g_meterR.load(), 0x00, 0x00 };
                const uint8_t idle[4] = { 0x00, 0x00, 0x00, 0x00 };
                const uint8_t zero = 0x00, closeValue = 0x11;   // 011d = 11 closes the channel view's idle cycle
                p.meters.push_back(::uf1::buildScreen(0x0009, level));
                p.meters.push_back(::uf1::buildScreen(0x000a, idle));
                p.meters.push_back(::uf1::buildScreen(0x0015, std::span<const uint8_t>(&zero, 1)));
                p.meters.push_back(::uf1::buildScreen(0x0016, std::span<const uint8_t>(&zero, 1)));
                p.tail.push_back(::uf1::buildScreen(0x011d, std::span<const uint8_t>(&closeValue, 1)));
            }
            uf1pace::emitCycle(g_dev, p, slot);
            slot += std::chrono::microseconds(40800);
            std::this_thread::sleep_until(slot);
        }
        g_cycleRunning = false;
    }).detach();
}

// Only what changed goes out (turning one V-pot used to resend all four cells, the bars and the styles for
// every step, which made the bar slow and choppy). force: send everything (view change, start-up, reopen).
void paintSendCells(bool force)
{
    if (!g_open) return;
    if (g_view == kViewMeter || g_presetsOpen) return;   // the meter layout / the browser use these cells
    std::lock_guard<std::mutex> lk(g_cellMx);
    static std::array<std::string, 4> shownLine;
    static std::array<uint8_t, 8> shownBars{};
    static std::array<uint8_t, 4> shownStyles{};
    const bool show = g_view == kViewSends;
    std::array<uint8_t, 8> bars{};
    std::array<uint8_t, 4> styles{};
    if (g_view == kViewPlugin) {
        // PLUGIN: the plug-in's controls (bipolar ones as a centre bar, style 08 with a signed position, as
        // Rea-Sixty draws EQ gains and trims), or the Quick Controls (the bar on the bridge's own position).
        const bool qc = !g_plugPresent;
        for (int i = 0; i < 4; ++i) {
            const PlugCell& c = g_plugCells[size_t(i)];
            SendCell sc; sc.name = c.name; sc.value = c.value; sc.used = c.used;
            const std::string line = c.used ? sendCellLine(sc) : std::string();
            if (force || line != shownLine[size_t(i)]) {
                shownLine[size_t(i)] = line;
                std::vector<uint8_t> p;
                p.push_back(uint8_t(i));
                p.insert(p.end(), line.begin(), line.end());
                g_dev.send(::uf1::buildScreen(::uf1::scr::kFocusedParam, p));
            }
            const int own = g_vpotPos[size_t(i)];
            int pos = (qc && own >= 0) ? (own * 100 + 8191) / 16383 : c.pos;
            if (c.bipolar) pos = std::clamp((pos - 50) * 2, -100, 100);
            bars[size_t(i) * 2] = c.used ? uint8_t(int8_t(pos)) : 0;
            bars[size_t(i) * 2 + 1] = 0x80;
            styles[size_t(i)] = !c.used ? 0x03 : c.bipolar ? 0x08 : 0x01;
        }
        if (force || bars != shownBars) { shownBars = bars; g_dev.send(::uf1::buildScreen(::uf1::scr::kVpotBars, bars)); }
        if (force || styles != shownStyles) { shownStyles = styles; g_dev.send(::uf1::buildScreen(::uf1::scr::kVpotStyle, styles)); }
        return;
    }
    for (int i = 0; i < 4; ++i) {
        const SendCell& c = g_sendCells[size_t(i)];
        const bool on = show && c.used;
        SendCell shownCell = c;
        if (on && g_vpotPos[size_t(i)] >= 0 && nowMs() < g_vpotOwnUntil[size_t(i)]) {
            const std::string t = levelTextFor(g_vpotPos[size_t(i)]);   // turning: the text for our own position
            if (!t.empty()) shownCell.value = t;
        }
        const std::string line = on ? sendCellLine(shownCell) : std::string();
        if (force || line != shownLine[size_t(i)]) {
            shownLine[size_t(i)] = line;
            std::vector<uint8_t> p;
            p.push_back(uint8_t(i));
            p.insert(p.end(), line.begin(), line.end());
            g_dev.send(::uf1::buildScreen(::uf1::scr::kFocusedParam, p));
        }
        // The bar follows the bridge's own V-pot position (exact, see g_vpotPos), Cubase's otherwise.
        const int own = g_vpotPos[size_t(i)];
        const uint8_t want = !on ? 0 : own >= 0 ? uint8_t((own * 100 + 8191) / 16383) : c.pos;
        bars[size_t(i) * 2] = want;
        bars[size_t(i) * 2 + 1] = 0x80;
        styles[size_t(i)] = on ? 0x01 : 0x03;
    }
    if (force || bars != shownBars) {
        shownBars = bars;
        g_dev.send(::uf1::buildScreen(::uf1::scr::kVpotBars, bars));
    }
    if (force || styles != shownStyles) { shownStyles = styles; g_dev.send(::uf1::buildScreen(::uf1::scr::kVpotStyle, styles)); }
}

// The big display's header row (0x011c, 8 fields of 25 bytes; in the channel layout the firmware draws field 0
// under CHANNEL, field 3 as the SOFT KEY page "N/M" and field 4 as FINE CTRL, Rea-Sixty's uf1spread::pageHeader).
// PLUGIN with an SSL plug-in: its page; otherwise V-pots 1-4 / 5-8 as page 1 / 2.
void paintHeader(bool force)
{
    if (!g_open || g_view == kViewMeter || g_presetsOpen) return;
    static std::array<uint8_t, 200> shown{};
    std::array<uint8_t, 200> h{};
    // Field 0 (under CHANNEL): in PLUGIN, what the V-pots work - the SSL plug-in, or what the Quick Controls belong
    // to; elsewhere the program's name.
    std::string name = std::string("JOG ") + kJogNames[jogShown()];
    if (g_view == kViewPlugin && nowMs() >= g_jogShowUntil) {
        std::lock_guard<std::mutex> lk(g_cellMx);
        if (g_plugPresent) {
            name = g_plugTitle;
            for (const char* pre : {"SSL Native ", "SSL "})
                if (name.rfind(pre, 0) == 0) { name.erase(0, std::strlen(pre)); break; }
            if (name.empty()) name = g_plugModel;
        } else {
            // The track's own Quick Controls report the track's name, which the small display already shows:
            // "Cubase QC" then, and for nothing reported. A plug-in's name (its window's Quick Controls) is kept.
            name = (g_qcSource.empty() || g_qcSource == g_trackName) ? std::string("Cubase QC") : g_qcSource;   // 13 characters fit
        }
    }
    name = sslpresets::latin1(name);
    std::memcpy(h.data(), name.data(), std::min<size_t>(name.size(), 24));
    const bool plug = plugActive();
    const std::string page = plug ? std::to_string(g_plugPage + 1) + "/" + std::to_string(int(g_plugPages))
                                  : std::string(g_sends58 ? "2/2" : "1/2");
    std::memcpy(h.data() + 75, page.data(), std::min<size_t>(page.size(), 24));
    std::memcpy(h.data() + 100, g_fine ? "ON" : "OFF", g_fine ? 2 : 3);
    if (!force && h == shown) return;
    shown = h;
    g_dev.send(::uf1::buildScreen(::uf1::scr::kHeaderRow, h));
}

// The CS TYPE cell (0x0017): the model of the selected track's SSL plug-in ("CS 2", "4K E" ...), 12 spaces
// without one (Rea-Sixty: never empty, that drops the layer).
void paintCsType(bool force)
{
    if (!g_open) return;
    static std::string shown = "\x01";
    std::string t;
    { std::lock_guard<std::mutex> lk(g_cellMx); t = g_plugPresent ? g_plugModel : std::string(); }
    if (t.empty()) t.assign(12, ' ');
    if (!force && t == shown) return;
    shown = t;
    std::vector<uint8_t> p{0x00};
    p.insert(p.end(), t.begin(), t.end());
    g_dev.send(::uf1::buildScreen(::uf1::scr::kCsType, p));
}

// The EQ graph (0x0122): the channel strip's own EQ curve, as the plug-in sends it to SSL 360 Core (the stand-in
// collects it, as Rea-Sixty's uf1PaintEqGraph_ reads it), resampled onto the UF1's 250 columns (20 Hz .. 20 kHz).
// Flat (the 0 dB line) when the track has no SSL channel strip, its EQ is out, or no curve has come. Called
// from the cycle thread between two cycles; only a changed curve goes out.
void paintEq(bool force)
{
    if (!g_open) return;
    static std::array<uint8_t, 251> last{};
    static uint8_t lastTail = 0;
    static bool have = false;
    std::array<uint8_t, 251> col{};
    uint8_t tail = 0x64;
    bool drawn = false;
    std::string model;
    { std::lock_guard<std::mutex> lk(g_cellMx); model = g_plugModel; }
    const bool strip = g_plugPresent && !model.empty() && model != "BC 2";
    if (strip && g_plugEqIn && g_trackNo > 0) {
        ensureSslCore();
        std::vector<float> wire;
        float wMin = 0.0f, wMax = 0.0f;
        if (sslcore::isRunning() &&
            sslcore::getChannelStripEqCurve(g_trackNo, model.c_str(), nullptr, 0, g_plugOrd, wire, wMin, wMax)) {
            // Points the plug-in sent as NaN / inf are filled in from their neighbours (Rea-Sixty's
            // uf1SanitizeWireCurve_); all of them bad = no curve.
            const int n = int(wire.size());
            int good = 0;
            for (float v : wire) if (std::isfinite(v)) ++good;
            if (good > 0) {
                for (int i = 0; i < n;) {
                    if (std::isfinite(wire[size_t(i)])) { ++i; continue; }
                    int j = i;
                    while (j < n && !std::isfinite(wire[size_t(j)])) ++j;
                    for (int k = i; k < j; ++k) {
                        if (i > 0 && j < n) wire[size_t(k)] = wire[size_t(i - 1)] + (wire[size_t(j)] - wire[size_t(i - 1)]) * float(k - i + 1) / float(j - i + 1);
                        else wire[size_t(k)] = i > 0 ? wire[size_t(i - 1)] : wire[size_t(j)];
                    }
                    i = j;
                }
                const double aMin = wMin > 0.0f ? double(wMin) : 20.0;
                const double aMax = wMax > aMin ? double(wMax) : 20000.0;
                const double lMin = std::log(aMin), lSpan = std::log(aMax) - lMin;
                for (int x = 2; x < 252; ++x) {   // column 251 = the tail point
                    const double f = 20.0 * std::pow(1000.0, (x - 2) / 249.0);
                    double pos = lSpan > 0.0 ? (std::log(f) - lMin) / lSpan * (n - 1) : 0.0;
                    pos = std::clamp(pos, 0.0, double(n - 1));
                    const int i0 = int(pos), i1 = i0 + 1 < n ? i0 + 1 : i0;
                    const double fr = pos - i0;
                    const uint8_t hgt = uf1eq::dbToHeight(wire[size_t(i0)] * (1.0 - fr) + wire[size_t(i1)] * fr);
                    if (x < 251) col[size_t(x)] = hgt; else tail = hgt;
                }
                drawn = true;
            }
        }
    }
    if (!drawn && !strip) {
        // No SSL channel strip: Cubase's own channel EQ, drawn from its settings with Rea-Sixty's renderer.
        uf1eq::Model m;
        { std::lock_guard<std::mutex> lk(g_cellMx); m = g_cubaseEq; }
        if (m.on) { uf1eq::render(m, col, tail); drawn = true; }
    }
    if (!drawn) { uf1eq::Model flat; uf1eq::render(flat, col, tail); }
    col[0] = 0x00;
    col[1] = 0x01;
    if (have && !force && col == last && tail == lastTail) return;
    last = col; lastTail = tail; have = true;
    const std::array<uint8_t, 2> refresh{0x01, tail};
    g_dev.send(::uf1::buildScreen(0x0122, refresh));
    g_dev.send(::uf1::buildScreen(0x0122, std::span<const uint8_t>(col.data(), col.size())));
}

// Screen text zone: 0x00, then the text (the UF1 reads one byte per glyph, so ASCII only here).
void screenText(uint16_t addr, const std::string& text)
{
    std::vector<uint8_t> p;
    p.push_back(0x00);
    p.insert(p.end(), text.begin(), text.end());
    g_dev.send(::uf1::buildScreen(addr, p));
}

// ---------------------------------------------------------------------------
// Value line + readout bar: a home view (pan) and temporary views (sends). A temporary view shows for
// kTemporaryMs after the last change, then the home view comes back. Called from the MIDI thread and the
// revert thread, hence the mutex.
// ---------------------------------------------------------------------------
constexpr long long kTemporaryMs = 3000;
constexpr long long kSelectionQuietMs = 500;
std::mutex g_lineMx;
std::string g_homeText;
uint8_t g_homePos = 50, g_homeStyle = 0x01;
bool g_homeHasBar = false;
long long g_tempUntil = 0;        // 0 = showing the home view
long long g_quietUntil = 0;       // ignore temporary updates until then (after a selection change)
uint8_t g_barStyleShown = 0x00;

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void showBarLocked(uint8_t pos, uint8_t style) {
    if (style != g_barStyleShown) {
        g_barStyleShown = style;
        g_dev.send(::uf1::buildScreen(::uf1::scr::kBarStyle, std::span<const uint8_t>(&style, 1)));
    }
    const uint8_t pb[2] = { pos, 0x00 };
    g_dev.send(::uf1::buildScreen(::uf1::scr::kVPotReadoutBar, pb));
}
void showHomeLocked() {
    screenText(::uf1::scr::kValueLine, g_homeText);
    if (g_homeHasBar) showBarLocked(g_homePos, g_homeStyle);
}
void valueHome(const std::string& t) {
    std::lock_guard<std::mutex> lk(g_lineMx);
    g_homeText = t;
    if (!g_tempUntil) screenText(::uf1::scr::kValueLine, t);
}
void barHome(uint8_t pos, uint8_t style) {
    std::lock_guard<std::mutex> lk(g_lineMx);
    g_homePos = pos; g_homeStyle = style; g_homeHasBar = true;
    if (!g_tempUntil) showBarLocked(pos, style);
}
void valueTemporary(const std::string& t) {
    std::lock_guard<std::mutex> lk(g_lineMx);
    const long long now = nowMs();
    if (now < g_quietUntil) return;
    g_tempUntil = now + kTemporaryMs;
    screenText(::uf1::scr::kValueLine, t);
}
void barTemporary(uint8_t pos, uint8_t style) {
    std::lock_guard<std::mutex> lk(g_lineMx);
    const long long now = nowMs();
    if (now < g_quietUntil) return;
    g_tempUntil = now + kTemporaryMs;
    showBarLocked(pos, style);
}
void selectionChanged() {
    std::lock_guard<std::mutex> lk(g_lineMx);
    g_quietUntil = nowMs() + kSelectionQuietMs;
    g_tempUntil = 0;
    showHomeLocked();
}
// Runs while the UF1 is open: brings the home view back when a temporary view has timed out.
void revertLoop() {
    while (g_open) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lk(g_lineMx);
        if (g_tempUntil && nowMs() >= g_tempUntil && g_open) { g_tempUntil = 0; showHomeLocked(); }
    }
}

// Channel-number box: the UF8 strip holding the selected track (blank while MASTER is on, or when the
// track is not on the UF8's current bank).
std::atomic<int> g_stripNo{0};
void showStripNumber()
{
    // The track number when the script knows it, else the UF8 strip; blank on MASTER.
    if (!g_open) return;
    const int n = g_trackNo > 0 ? int(g_trackNo) : int(g_stripNo);
    screenText(::uf1::scr::kChNumber, (g_master || n == 0) ? std::string() : std::to_string(n));
}

// Sysex from the Cubase script: 01 name, 02 colour (strip 0), 03 level text.
void onSysex(const std::vector<unsigned char>& m)
{
    if (!isOurSysex(m)) return;
    if (scriptLogSysex(m.data(), m.size())) return;
    if (auto n = parseName(m)) {
        if (n->strip != 0) return;
        std::string t(n->text.begin(), n->text.end());
        // An empty name means the selection landed on something without a mixer channel (a folder, a
        // marker track, or nothing). Keep showing the last track rather than blanking the display; the
        // colour that follows (all zero) is skipped too.
        g_emptySelection = t.empty();
        if (!t.empty()) { { std::lock_guard<std::mutex> lk(g_cellMx); g_trackName = t; } paintHeader(); }
        g_uf1RightSeen = false;   // another track: mono until it sends a right side
        if (t.empty()) return;
        if (t.size() > 8) t.resize(8);   // the UF1 name cell is 8 characters wide
        screenText(::uf1::scr::kTrackName, t);
    } else if (auto c = parseColour(m)) {
        if (c->strip != 0) return;
        if (g_emptySelection && c->r == 0 && c->g == 0 && c->b == 0) return;   // see the empty name above
        const uint8_t idx = colourToPalette(c->r, c->g, c->b);
        const uint8_t active = 0x01;
        g_dev.send(::uf1::buildScreen(::uf1::scr::kChActive, std::span<const uint8_t>(&active, 1)));
        g_dev.send(::uf1::buildScreen(::uf1::scr::kColourBar, std::span<const uint8_t>(&idx, 1)));
        g_selRgb = (uint32_t(c->r * 2 + (c->r >> 6)) << 16) | (uint32_t(c->g * 2 + (c->g >> 6)) << 8) | uint32_t(c->b * 2 + (c->b >> 6));
        if (!g_selRgb) g_selRgb = 0xFFFFFF;   // no colour: white
        showSelLed();
    } else if (m[4] == 0x04 || m[4] == 0x08) {
        // Value line text. 04 = temporary (a send V-pot), 08 = home (pan).
        std::string t;
        for (size_t i = 5; i + 1 < m.size(); ++i) t.push_back(char(m[i]));
        if (t.size() > 12) t.resize(12);
        if (g_trace) std::printf("UF1 value line (%s): \"%s\"\n", m[4] == 0x08 ? "home" : "temporary", t.c_str());
        if (m[4] == 0x08) valueHome(t);
        else if (g_view != kViewSends) valueTemporary(t);   // SENDS: the send's cell shows it already
    } else if ((m[4] == 0x05 || m[4] == 0x09) && m.size() >= 7) {
        // Readout bar: position 0..100, optional style byte (0x01 pointer, 0x02 fill). 05 temporary, 09 home.
        const uint8_t pos = uint8_t(m[5] > 100 ? 100 : m[5]);
        const uint8_t style = (m.size() >= 8 && (m[6] == 0x01 || m[6] == 0x02 || m[6] == 0x08)) ? m[6] : 0x02;
        if (m[4] == 0x09) barHome(pos, style);
        else if (g_view != kViewSends) barTemporary(pos, style);
    } else if (m[4] == 0x17 && m.size() >= 9 && m[5] < 4) {
        // A V-pot's position in Cubase (0..16383). Taken unless the V-pot is being turned right now.
        const size_t k = m[5];
        {
            // Learn the level curve: this position with the text Cubase sent just before it (SENDS and DAW,
            // where the V-pots are on the sends; never PLUGIN, whose Quick Controls have other curves).
            const int v = (int(m[6]) << 7) | int(m[7]);
            double db = 0;
            if (g_view != kViewPlugin && nowMs() - g_lastCubaseTextMs[k] < 60 && parseDb(g_lastCubaseText[k], db)) {
                std::lock_guard<std::mutex> lk(g_levelMx);
                g_levelText[v] = g_lastCubaseText[k];
            }
        }
        if (nowMs() >= g_vpotOwnUntil[k]) {
            const int v = (int(m[6]) << 7) | int(m[7]);
            if (v != g_vpotPos[k]) { g_vpotPos[k] = v; if (g_view == kViewSends) paintSendCells(false); }
        }
    } else if (m[4] == 0x15 && m.size() >= 9 && m[5] < 4) {
        // A send for the SENDS view's cells: pot index, position 0..100, then "name|value" (empty = no send).
        std::unique_lock<std::mutex> lk(g_cellMx);
        SendCell& c = g_sendCells[m[5]];
        std::string t(m.begin() + 7, m.end() - 1);
        const size_t bar = t.find('|');
        c.name = bar == std::string::npos ? t : t.substr(0, bar);
        c.value = bar == std::string::npos ? std::string() : t.substr(bar + 1);
        g_lastCubaseText[m[5]] = c.value;
        g_lastCubaseTextMs[m[5]] = nowMs();
        c.pos = uint8_t(m[6] > 100 ? 100 : m[6]);
        c.used = !c.name.empty() || !c.value.empty();
        lk.unlock();
        // Drawn at once, like the value line (which turns smoothly): a 40 ms painter sampled the updates at
        // its own beat (Cubase sends every 20-40 ms), so some frames moved two steps and some none.
        if (g_view == kViewSends) paintSendCells(false);
    } else if (m[4] == 0x55) {
        // Cubase's channel EQ for the graph: "<kind> <Hz> <dB> <Q> <order>" per band, ';' between (see the script).
        uf1eq::Model model;
        const std::string t(m.begin() + 5, m.end() - 1);
        size_t a = 0;
        while (a < t.size()) {
            size_t e = t.find(';', a);
            if (e == std::string::npos) e = t.size();
            char kind = 0; double hz = 0, db = 0, q = 1; int order = 2;
            if (std::sscanf(t.substr(a, e - a).c_str(), " %c %lf %lf %lf %d", &kind, &hz, &db, &q, &order) == 5 && hz > 0) {
                using K = uf1eq::Band::Kind;
                uf1eq::Band b;
                b.kind = kind == 'L' ? K::LowShelf : kind == 'H' ? K::HighShelf : kind == 'P' ? K::HighPass : kind == 'C' ? K::LowPass : K::Bell;
                b.freq = hz; b.gainDb = db; b.q = q > 0 ? q : 1.0; b.order = order;
                model.bands.push_back(b);
            }
            a = e + 1;
        }
        model.on = !model.bands.empty();
        std::lock_guard<std::mutex> lk(g_cellMx);
        if (model.bands.size() != g_cubaseEq.bands.size()) std::printf("UF1 EQ graph: Cubase EQ, %zu band(s) on\n", model.bands.size());
        g_cubaseEq = model;
    } else if (m[4] == 0x56) {
        // What the Quick Controls belong to ("-" = not known), for the header in PLUGIN.
        { std::lock_guard<std::mutex> lk(g_cellMx); g_qcSource.assign(m.begin() + 5, m.end() - 1); if (g_qcSource == "-") g_qcSource.clear(); }
        paintHeader();
    } else if (m[4] == 0x54) {
        // The script (re)started: it begins on the sends, so tell it the V-pot page of the view shown.
        sendVpotPage();
    } else if (m[4] == 0x50 && m.size() >= 11) {
        // PLUGIN view: the SSL plug-in on the selected track (present, page, pages, EQ in, ordinal, model).
        const bool present = m[5] != 0;
        {
            std::lock_guard<std::mutex> lk(g_cellMx);
            // model, then 00 and the plug-in's name
            const auto sep = std::find(m.begin() + 10, m.end() - 1, uint8_t(0x00));
            g_plugModel.assign(m.begin() + 10, sep);
            g_plugTitle = sep == m.end() - 1 ? std::string() : std::string(sep + 1, m.end() - 1);
            if (!present) {   // the script sends the Quick Controls' cells next
                for (auto& k : g_plugKeys) k = PlugKey{};
                for (auto& c : g_plugCells) c = PlugCell{};
            }
        }
        const bool was = g_plugPresent.exchange(present);
        g_plugPage = m[6];
        g_plugPages = std::max(1, int(m[7]));
        g_plugEqIn = m[8] != 0;
        g_plugOrd = m[9];
        if (g_trace) std::printf("UF1 PLUGIN: %s page %d/%d EQ %s\n", present ? g_plugModel.c_str() : "(none)", int(m[6]) + 1, int(m[7]), m[8] ? "in" : "out");
        paintCsType(false);
        paintHeader();
        if (was != present && g_view == kViewPlugin) { paintSoftKeys(); paintSendCells(true); }
    } else if (m[4] == 0x51 && m.size() >= 9 && m[5] < 4) {
        // A PLUGIN-view cell: pot, position 0..100, flags (1 bipolar, 2 used), "label|value".
        {
            std::lock_guard<std::mutex> lk(g_cellMx);
            PlugCell& c = g_plugCells[m[5]];
            std::string t(m.begin() + 8, m.end() - 1);
            const size_t bar = t.find('|');
            c.name = bar == std::string::npos ? t : t.substr(0, bar);
            c.value = bar == std::string::npos ? std::string() : t.substr(bar + 1);
            c.pos = uint8_t(m[6] > 100 ? 100 : m[6]);
            c.bipolar = (m[7] & 1) != 0;
            c.used = (m[7] & 2) != 0;
        }
        if (g_view == kViewPlugin) paintSendCells(false);
    } else if (m[4] == 0x52 && m.size() >= 8 && m[5] < 4) {
        // A PLUGIN-view soft key: key, state (0 off, 1 on, 2 no switch), label.
        {
            std::lock_guard<std::mutex> lk(g_cellMx);
            PlugKey& k = g_plugKeys[m[5]];
            k.state = m[6];
            k.label.assign(m.begin() + 7, m.end() - 1);
        }
        if (g_view == kViewPlugin && !g_modeHeld) paintSoftKeys();
    } else if (m[4] == 0x0E && m.size() >= 8) {
        g_trackNo = (int(m[5]) << 7) | int(m[6]);
        showStripNumber();
    } else if (m[4] == 0x0D) {
        // Transport position text -> the 7-segment time field on the big display. Cubase pads every part of
        // the position to a fixed width with spaces ("  1. 1. 1.   0"), so spaces stay as blank cells: the
        // digits then keep their places instead of shifting as the numbers change (that shifting flickered).
        // '.', ':' and ',' fold into the dot of the cell before them. Right-aligned; if the text needs more
        // than ten cells, the leftmost ones (normally padding) are dropped.
        std::string t(m.begin() + 5, m.end() - 1);
        // Readability: a ':' becomes a blank cell instead of a dot, so "0:00:47.691" reads "0 00 47.691"
        // (dots stay for decimals and the bars/beats separators). If that does not fit, a leading group of
        // zeros (hours) is dropped first: "00:00:47:12" -> "00 47 12".
        {
            // Seconds format "H:MM:SS.mmm": shown as blank-separated groups, e.g. "47 691", "3 47 691",
            // "1 03 47 69" - hours and minutes only when they are not zero, the first group without leading
            // zeros, and the milliseconds cut to fit the ten cells.
            int h = 0, mi = 0, se = 0; char ms[8] = {0};
            if (std::sscanf(t.c_str(), " %d:%d:%d.%7[0-9]", &h, &mi, &se, ms) == 4) {
                char buf[48];
                if (h > 0) std::snprintf(buf, sizeof(buf), "%d %02d %02d %s", h, mi, se, ms);
                else if (mi > 0) std::snprintf(buf, sizeof(buf), "%d %02d %s", mi, se, ms);
                else std::snprintf(buf, sizeof(buf), "%d %s", se, ms);
                t = buf;
                while (t.size() > size_t(::uf1::kTcCells) && t.back() != ' ') t.pop_back();   // trim milliseconds
            }
        }
        for (char& c : t) if (c == ':') c = ' ';
        auto isSep = [](char c) { return c == '.' || c == ','; };
        auto countCells = [&](const std::string& s) {
            int n = 0;
            for (size_t i = 0; i < s.size(); ++i) if (!isSep(s[i]) || i == 0) ++n;
            return n;
        };
        {
            const size_t first = t.find_first_not_of(' ');
            const size_t gap = (first == std::string::npos) ? std::string::npos : t.find(' ', first);
            if (countCells(t) > ::uf1::kTcCells && gap != std::string::npos
                && t.find_first_not_of('0', first) == gap)
                t.erase(0, gap + 1);
        }
        while (countCells(t) > ::uf1::kTcCells && !t.empty()) {
            t.erase(0, 1);
            while (!t.empty() && isSep(t[0])) t.erase(0, 1);
        }
        const int cells = countCells(t);
        const std::string padded = std::string(size_t(::uf1::kTcCells - cells), ' ') + t;
        auto payload = ::uf1::seg7Payload(padded);
        if (payload != g_tcShown) { g_tcShown = payload; g_dev.send(::uf1::buildScreen(::uf1::scr::kTimecode, payload)); }
    } else if (m[4] == 0x0A) {
        // A new track was selected: show its home view and ignore the burst of send values that follows.
        selectionChanged();
    } else if (m[4] == 0x03) {
        std::string v;
        for (size_t i = 5; i + 1 < m.size(); ++i) v.push_back(char(m[i]));
        std::vector<uint8_t> p;
        p.push_back(0x00);
        for (size_t k = 0; k < 6; ++k) p.push_back(k < v.size() ? uint8_t(v[k]) : 0x00);
        p.push_back('d'); p.push_back('B');
        g_dev.send(::uf1::buildScreen(::uf1::scr::kOutputDb, p));
    }
}

void onMidiIn(std::vector<unsigned char>& msg)
{
    const auto& m = msg;
    traceMidi("in", m);
    {
        // Remember the latest of each kind: SysEx by command (name, colour, level, value lines, bars, time),
        // notes / aftertouch by note number (LED states).
        std::lock_guard<std::mutex> lk(g_cacheMx);
        if (m.size() >= 6 && m[0] == 0xF0 && m[1] == 0x7D) g_cache[0x100 + m[4]] = m;
        else if (m.size() >= 3 && ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80 || (m[0] & 0xF0) == 0xA0)) g_cache[m[1]] = m;
    }
    if (!m.empty() && m[0] == 0xF0) { onSysex(m); return; }
    if (m.size() < 3) return;
    const uint8_t status = m[0] & 0xF0;
    if (status == 0xB0 && m.size() >= 3 && (m[1] == kCcMeter || m[1] == kCcMeter + 1)) {
        // Meter from the script: CC 80 left, CC 81 right (0..127) -> the level meter on the small display,
        // 0..31. A mono track sends only the left side, which is then shown on both.
        static uint8_t shownL = 0xFF, shownR = 0xFF, lvL = 0, lvR = 0;
        const uint8_t lv = uint8_t((m[2] * 31 + 63) / 127);
        if (m[1] == kCcMeter + 1) { g_uf1RightSeen = true; lvR = lv; }
        else { lvL = lv; if (!g_uf1RightSeen) lvR = lv; }
        if (lvL == shownL && lvR == shownR) return;
        shownL = lvL; shownR = lvR;
        g_meterL = lvL; g_meterR = lvR;   // the idle cycle carries the meter (startIdleCycle)
        return;
    }
    if (status == 0xE0 && (m[0] & 0x0F) == 0) {   // fader from Cubase
        if (g_touched) return;
        const uint16_t v = uint16_t((m[1] & 0x7F) | ((m[2] & 0x7F) << 7));
        const uint16_t pos = uf1m::midi14ToPos15(v);
        bool first;
        { std::lock_guard<std::mutex> lk(g_engMx); first = !g_engaged; g_engaged = true; }
        if (first) {
            g_dev.send(::uf1::buildMotorPosition(pos));
            g_dev.send(::uf1::buildMotorEnable(true));
        }
        g_dev.send(::uf1::buildMotorPosition(pos));
        return;
    }
    if ((status == 0x90 || status == 0x80 || status == 0xA0)) {
        if (m[1] == uf1m::kNotePlay && status != 0xA0) g_playing = status == 0x90 && m[2] > 0;   // for the meter view
        if (m[1] == kNoteSel) { g_selOn = status != 0x80 && m[2] > 0; showSelLed(); return; }
        if (m[1] == 62 && status != 0xA0) {
            // BYPASS LED: off without inserts, orange at 35 % while they are active, full orange when bypassed.
            const int vel = status == 0x80 ? 0 : m[2];
            g_bypassState = vel == 0 ? 0 : vel < 100 ? 1 : 2;
            const uint32_t rgb = g_bypassState == 2 ? 0xFF8000u : 0x592D00u;
            setLed(uf1m::Led{0x00, rgb}, g_bypassState != 0);
            return;
        }
        if (auto led = uf1m::noteToLed(m[1])) setLed(*led, status != 0x80 && m[2] > 0);
    }
}

// Back from METER to the channel layout: Rea-Sixty's channel entry (cap84 / cap101, the frames the meter
// layout changed), then everything the channel view shows drawn again from what Cubase sent last.
void restoreChannelView()
{
    put(0x0000, {0x03, 0x00});
    put(0x0011, {0x01});
    put(0x0100, {0x03, 0x00});   // layout: plugin / channel
    put(0x0101, {0x05});
    put(0x010d, {0x02, 0x02, 0x08, 0x02});
    put(0x0110, {0x0f});
    put(0x011a, {0x02});
    put(0x011e, {0x19});
    put(0x000a, {0x00, 0x00, 0x00, 0x00});
    put(0x011b, {});
    put(0x0102, {0x00});
    paintHeader(true);   // the header row carried the meter readouts
    paintCsType(true);
    paintSoloActive();
    paintEq(true);
    screenText(::uf1::scr::kChSoftKey, "BYPASS");
    { std::lock_guard<std::mutex> lk(g_lineMx); g_barStyleShown = 0x00; }   // the bar style goes out again
    g_tcShown.clear();
    std::map<int, std::vector<unsigned char>> cache;
    { std::lock_guard<std::mutex> lk(g_cacheMx); cache = g_cache; }
    for (auto& kv : cache)   // the script's texts (not its log lines)
        if (kv.first >= 0x100 && kv.first != 0x17E) { auto msg = kv.second; onMidiIn(msg); }
    { std::lock_guard<std::mutex> lk(g_lineMx); g_tempUntil = 0; showHomeLocked(); }
    showStripNumber();
    paintSoftKeys();
    paintSendCells(true);
    if (g_trace) std::printf("UF1: channel view again\n");
}

} // namespace

// Lights every UF1 LED we drive, one at a time, then turns it off again.
void uf1LampTest()
{
    if (!g_open) return;
    const uint8_t notes[] = {uf1m::kNoteRwd, uf1m::kNoteFfw, uf1m::kNoteStop, uf1m::kNotePlay, uf1m::kNoteRec,
                             uf1m::kNoteCycle, uf1m::kNoteClick, kNoteSolo, kNoteCut};
    std::printf("UF1 lamp test: each LED lights for a moment...\n");
    for (uint8_t n : notes) {
        if (auto led = uf1m::noteToLed(n)) {
            setLed(*led, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            setLed(*led, false);
        }
    }
    std::printf("UF1 lamp test done.\n");
}

// Opens the UF1 (three tries) and draws the start-up screen. Used at start and after a reopen.
bool uf1OpenDevice()
{
    // The device sometimes is not ready right after the previous bridge let go of it: try three times.
    bool opened = g_dev.open();
    for (int attempt = 2; !opened && attempt <= 3; ++attempt) {
        std::fprintf(stderr, "UF1: could not open the device (%s); trying again (%d of 3)...\n", g_dev.lastError().c_str(), attempt);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        opened = g_dev.open();
    }
    if (!opened) {
        std::fprintf(stderr, "UF1: could not open the device (%s); skipping the UF1.\n", g_dev.lastError().c_str());
        return false;
    }
    g_dev.setInputHandler(onUf1Event);
    g_open = true;
    // Same as Rea-Sixty after open(): a header banner, and the motor limp so the fader moves freely.
    {
        paintHeader(true);
        g_dev.send(::uf1::buildMotorEnable(false));
        // Cells inside the colour bar that the UF1's start-up state leaves filled: the text cell (Rea-Sixty
        // writes 12 spaces for "nothing" - never empty, that drops the layer) and the channel number.
        paintCsType(true);
        paintSoloActive();
        setLed(uf1m::Led{0x1D, 0xFFFFFF}, g_fine);
        screenText(::uf1::scr::kChNumber, "");
        screenText(::uf1::scr::kChSoftKey, "BYPASS");   // the key above the display: insert bypass
        paintSoftKeys();   // the four display soft keys: blank and dark until MODE is held
        paintSendCells(true);  // the V-pot cells: blank outside the SENDS view
        startIdleCycle();      // the unbroken cycle stream the UF1 wants (see startIdleCycle)
        // The meter's neighbours, as Rea-Sixty writes them: peak indicator (0x000a) and the compressor / gate
        // gain-reduction LEDs (0x0015 / 0x0016), all at rest.
        {
            const uint8_t z4[4] = {0, 0, 0, 0};
            const uint8_t z1 = 0;
            g_dev.send(::uf1::buildScreen(0x000a, z4));
            g_dev.send(::uf1::buildScreen(0x0015, std::span<const uint8_t>(&z1, 1)));
            g_dev.send(::uf1::buildScreen(0x0016, std::span<const uint8_t>(&z1, 1)));
        }
        screenText(::uf1::scr::kValueLine, "READY");   // start-up check: does the value line show anything at all?
        const uint8_t halfBar[2] = {50, 0x00};
        g_dev.send(::uf1::buildScreen(::uf1::scr::kVPotReadoutBar, halfBar));
        const uint8_t fillStyle = 0x02;   // bar style: fill from the left (the init leaves it off)
        g_dev.send(::uf1::buildScreen(::uf1::scr::kBarStyle, std::span<const uint8_t>(&fillStyle, 1)));
        g_barStyleShown = fillStyle;
        g_dev.send(::uf1::buildMasterBrightness(0xff));
        g_dev.send(::uf1::buildLedBrightness(0x20));
    }
    return true;
}

// Cubase -> the Mackie device: only the SCRUB LED (note 101) matters; it keeps our scrub state in step with
// Cubase's (e.g. after a restart of either side).
void onMcuIn(std::vector<unsigned char>& m)
{
    if (m.size() >= 3 && (m[0] & 0xF0) == 0x90 && m[1] == 0x65) {
        const bool on = m[2] > 0;
        if (on != g_scrub) {
            g_scrub = on;
            if (on && g_shuttle) { g_shuttle = false; shuttleStop(); }
            showScrubLed();
            paintHeader();
            paintNavLeds();
        }
    }
}

bool uf1Start()
{
    if (!uf1OpenDevice()) return false;
    // Ports last, so Cubase's first burst of state (sent when it sees the ports appear) finds the screen ready.
    if (!g_out.open(kPortTo) || !g_in.open(kPortFrom, &onMidiIn)) {
        std::fprintf(stderr, "UF1: could not open the MIDI ports \"%s\" / \"%s\"; skipping the UF1.\n", kPortTo, kPortFrom);
        g_dev.close();
        g_open = false;
        return false;
    }
    std::printf("UF1 open (serial %s).\n", g_dev.serial().c_str());
    g_mcuOpen = g_mcuOut.open(kMcuTo) && g_mcuIn.open(kMcuFrom, &onMcuIn);
    if (!g_mcuOpen) std::fprintf(stderr, "Mackie ports for scrubbing not available; SCRUB falls back to fast nudging.\n");
    std::thread(revertLoop).detach();
    return true;
}

void uf1Stop()
{
    if (g_open) { g_dev.close(); g_open = false; }
}

void uf1SetTrace(bool on) { g_trace = on; }

void paintSoloActive()
{
    if (!g_open) return;
    const bool on = g_soloActive;
    setLed(uf1m::Led{0x1C, 0xFFFF00}, on);   // quick key 1 (Solo Clear) lit yellow while something is soloed
    if (g_view == kViewMeter || g_presetsOpen) return;   // the meter / browser layouts use 0x0120 themselves
    const uint8_t v = on ? 0xFF : 0x00;
    g_dev.send(::uf1::buildScreen(::uf1::scr::kSoloActive, std::span<const uint8_t>(&v, 1)));
}

void uf1SetSoloActive(bool on)
{
    if (g_soloActive.exchange(on) != on) {
        if (g_trace) std::printf("UF1 solo active: %s\n", on ? "yes" : "no");
        paintSoloActive();
    }
}

void uf1SetStripNumber(int n)
{
    if (g_stripNo.exchange(n) != n) showStripNumber();
}

void uf1SendToScript(const std::vector<unsigned char>& msg) { sendMidi(msg); }

bool uf1NeedsReopen() { return g_open && g_dev.needsReopen(); }

std::string uf1LastError() { return g_dev.lastError(); }

// The UF1 stopped answering (e.g. it missed the wake right after the previous bridge let go of it): close
// it, open it again, and replay the last state Cubase sent. The MIDI ports stay as they are.
bool uf1Reopen()
{
    g_dev.close();
    g_open = false;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    {
        std::lock_guard<std::mutex> lk(g_ledMx);
        g_ledEnabled.clear();   // the LED enables have to be sent again
    }
    g_tcShown.clear();
    if (!uf1OpenDevice()) return false;
    std::thread(revertLoop).detach();
    std::map<int, std::vector<unsigned char>> cache;
    { std::lock_guard<std::mutex> lk(g_cacheMx); cache = g_cache; }
    for (auto& kv : cache) { auto msg = kv.second; onMidiIn(msg); }
    setLed(uf1m::Led{0x0A, 0xFFFFFF}, g_sends58);
    setLed(uf1m::Led{0x21, 0xFFFFFF}, g_master);
    setLed(uf1m::Led{0x20, 0xFFFFFF}, g_flip);
    showScrubLed();
    showStripNumber();
    paintSoftKeys();
    paintSendCells(true);
    startIdleCycle();
    std::printf("UF1 reopened.\n");
    return true;
}

} // namespace cs
