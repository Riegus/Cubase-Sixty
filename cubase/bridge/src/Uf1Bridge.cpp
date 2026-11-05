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

namespace cs {
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
// 3 SENDS = V-pots on the sends and the four sends shown in the display's V-pot cells).
enum { kViewPlugin = 0, kViewDaw = 1, kViewSends = 3 };
std::atomic<int> g_view{kViewDaw};
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
void sendVpotPage()
{
    const uint8_t n = g_view == kViewPlugin ? (g_sends58 ? 89 : 88) : (g_sends58 ? 54 : 53);
    note(n, true);
    note(n, false);
}

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
            const int perPulse = (ev.id == 6 && g_scrub && !g_shift) ? 1 : kDetentsPerPulse[k];
            if ((acc[k] > 0) != (ev.delta > 0)) acc[k] = 0;  // direction change restarts the count
            acc[k] += ev.delta;
            // Jog: nudge the cursor (42 / 43); with SHIFT held, previous / next marker (79 / 80); in SCRUB,
            // Cubase's Jog Left / Right (63 / 64); in SHUTTLE, each step changes the shuttle speed.
            const bool jog = ev.id == 6;
            const uint8_t base = !jog ? 40 : g_shift ? 79 : 42;
            while (acc[k] >= perPulse || acc[k] <= -perPulse) {
                const bool right = acc[k] > 0;
                acc[k] += right ? -perPulse : perPulse;
                if (jog && g_shuttle && !g_shift) { shuttleStep(right ? 1 : -1); continue; }
                if (jog && g_scrub && !g_shift && g_mcuOpen) {
                    // Mackie jog wheel, one tick: CC 60, 0x01 = clockwise, 0x41 = counter-clockwise. In
                    // Cubase's scrub mode (SCRUB key, below) this scrubs the audio.
                    mcuSend({0xB0, 0x3C, uint8_t(right ? 0x01 : 0x41)});
                    continue;
                }
                note(uint8_t(base + (right ? 1 : 0)), true);
                note(uint8_t(base + (right ? 1 : 0)), false);
            }
            break;
        }
        if (ev.id >= 1 && ev.id <= 4) {
            const size_t k = size_t(ev.id - 1);
            if (g_vpotPos[k] < 0) break;   // position not known yet (comes from Cubase within a second)
            const int v = std::clamp(g_vpotPos[k] + ev.delta * kVpotStep, 0, 16383);
            g_vpotPos[k] = v;
            g_vpotOwnUntil[k] = nowMs() + 500;   // Cubase's echoes of our own turns are not taken back meanwhile
            const uint8_t cc = uint8_t(uf1m::kCcEncoder + ev.id);
            sendMidi({0xB0, cc, uint8_t((v >> 7) & 0x7F)});
            sendMidi({0xB0, uint8_t(cc + 32), uint8_t(v & 0x7F)});
            if (g_view == kViewSends) paintSendCells(false);
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
        if (ev.id == 0x27) {
            if (!ev.pressed) break;
            if (g_shift) { g_shuttle = !g_shuttle; if (g_shuttle) g_scrub = false; else shuttleStop(); }
            else {
                g_scrub = !g_scrub;
                if (g_scrub && g_shuttle) { g_shuttle = false; shuttleStop(); }
                // Cubase's Mackie scrub mode follows: its SCRUB button (note 101) toggles it.
                if (g_mcuOpen) { mcuSend({0x90, 0x65, 0x7F}); mcuSend({0x90, 0x65, 0x00}); }
            }
            showScrubLed();
            break;
        }
        // MODE: while held, the four display soft keys pick the view (as in Rea-Sixty): 1 PLUGIN (V-pots on
        // Cubase's Quick Controls), 2 DAW (V-pots on the sends); 3 and 4 are not used yet. Releasing MODE
        // closes the picker. The view keeps its 5-8 state (Quick Controls 5-8 / sends 5-8).
        if (ev.id == 0x20) {
            g_modeHeld = ev.pressed;
            paintSoftKeys();
            break;
        }
        if (ev.id >= 0x19 && ev.id <= 0x1C) {
            // TEST (temporary): in SENDS, soft key 1 without MODE sweeps V-pot 1's bar by itself (40 ms per
            // step); soft key 2 the same, but also rewrites V-pot 1's text on every step, as turning does.
            if (ev.pressed && !g_modeHeld && g_view == kViewSends && (ev.id == 0x19 || ev.id == 0x1A)) {
                const bool withText = ev.id == 0x1A;
                std::thread([withText] {
                    std::printf("UF1 bar test: sweep, 40 ms per step%s\n", withText ? ", with text" : "");
                    std::array<uint8_t, 8> bars{0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80};
                    for (int pos = 0; pos <= 100; pos += 2) {
                        bars[0] = uint8_t(pos);
                        if (withText) {
                            SendCell c; c.name = "Test"; c.value = std::to_string(pos) + ".0dB";
                            const std::string line = sendCellLine(c);
                            std::vector<uint8_t> p;
                            p.push_back(0);
                            p.insert(p.end(), line.begin(), line.end());
                            g_dev.send(::uf1::buildScreen(::uf1::scr::kFocusedParam, p));
                        }
                        g_dev.send(::uf1::buildScreen(::uf1::scr::kVpotBars, bars));
                        std::this_thread::sleep_for(std::chrono::milliseconds(40));
                    }
                    paintSendCells(true);
                }).detach();
                break;
            }
            if (!ev.pressed || !g_modeHeld) break;
            if (ev.id == 0x19 || ev.id == 0x1A || ev.id == 0x1C) {
                const int view = ev.id - 0x19;
                if (view != g_view) {
                    const bool pageChanges = (view == kViewPlugin) != (g_view == kViewPlugin);
                    g_view = view;
                    if (pageChanges) sendVpotPage();
                    paintSendCells(true);
                    if (g_trace) std::printf("UF1 view: %s\n", view == kViewPlugin ? "PLUGIN" : view == kViewDaw ? "DAW" : "SENDS");
                }
                paintSoftKeys();
            }
            break;
        }
        // 5-8: the V-pots switch between sends 1-4 (note 53) and 5-8 (note 54). LED on = 5-8.
        if (ev.id == 0x22) {
            if (!ev.pressed) break;
            g_sends58 = !g_sends58;
            sendVpotPage();
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
            // SHIFT + ARROW: step back / forward one bar (81 / 82).
            const uint8_t n = g_shift ? (ev.id == 0x26 ? 82 : 81) : (ev.id == 0x26 ? 41 : 40);
            note(n, true);
            note(n, false);
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
    static const char* const kPicker[4] = {"PLUGIN", "DAW", "", "SENDS"};   // 3 = METER, later
    for (int i = 0; i < 4; ++i) {
        const std::string label = g_modeHeld ? kPicker[i] : "";
        std::vector<uint8_t> p;
        p.push_back(uint8_t(i));
        p.insert(p.end(), label.begin(), label.end());
        g_dev.send(::uf1::buildScreen(::uf1::scr::kSoftKeyLabel, p));
        uint32_t rgb = 0;   // dark
        if (g_modeHeld && i != 2) {
            const bool on = i == g_view;
            rgb = on ? 0xFFFF00 : 0x595900;   // the active view yellow, the other dim yellow (35 %)
        }
        softKeyLed(i, rgb);
    }
    const uint8_t noHighlight = 0;
    g_dev.send(::uf1::buildScreen(0x0102, std::span<const uint8_t>(&noHighlight, 1)));
}

// SENDS view: the four sends in the display's V-pot cells (Rea-Sixty's cells: 0x010e text per pot with the
// pot index first, 0x010f bars = position 0..100 + brightness 0x80 per pot, 0x010d styles: 0x01 pointer,
// 0x03 empty). The script sends each pot's name, level text and position (SysEx 15) in every view; the
// cells show them only in SENDS and are blank otherwise.
std::array<SendCell, 4> g_sendCells;
std::mutex g_cellMx;   // the cells are filled on the MIDI thread and drawn from the painter and the USB thread
std::string sendCellLine(const SendCell& c)
{
    // Rea-Sixty's value line: the name left (11 characters), the value right-aligned in 19.
    std::string name = c.name.substr(0, 11), value = c.value.substr(0, 8);
    std::string line = name;
    line.append(19 - name.size() - value.size(), ' ');
    line += value;
    return line;
}
// Turning a V-pot brings several updates per step (level text, position); drawing each one at once kept
// the V-pot row behind and choppy. So updates only mark the cells dirty, and a paint goes out at most every
// 40 ms (25 per second) with the latest state.
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
void startIdleCycle()
{
    bool expected = false;
    if (!g_cycleRunning.compare_exchange_strong(expected, true)) return;
    std::thread([] {
        auto slot = std::chrono::steady_clock::now();
        while (g_open) {
            uf1pace::Parts p;
            const uint8_t level[4] = { g_meterL.load(), g_meterR.load(), 0x00, 0x00 };
            const uint8_t idle[4] = { 0x00, 0x00, 0x00, 0x00 };
            const uint8_t zero = 0x00, closeValue = 0x11;   // 011d = 11 closes the channel view's idle cycle
            p.meters.push_back(::uf1::buildScreen(0x0009, level));
            p.meters.push_back(::uf1::buildScreen(0x000a, idle));
            p.meters.push_back(::uf1::buildScreen(0x0015, std::span<const uint8_t>(&zero, 1)));
            p.meters.push_back(::uf1::buildScreen(0x0016, std::span<const uint8_t>(&zero, 1)));
            p.tail.push_back(::uf1::buildScreen(0x011d, std::span<const uint8_t>(&closeValue, 1)));
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
    std::lock_guard<std::mutex> lk(g_cellMx);
    static std::array<std::string, 4> shownLine;
    static std::array<uint8_t, 8> shownBars{};
    static std::array<uint8_t, 4> shownStyles{};
    const bool show = g_view == kViewSends;
    std::array<uint8_t, 8> bars{};
    std::array<uint8_t, 4> styles{};
    for (int i = 0; i < 4; ++i) {
        const SendCell& c = g_sendCells[size_t(i)];
        const bool on = show && c.used;
        const std::string line = on ? sendCellLine(c) : std::string();
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
        if (g_trace) std::printf("%lld cell bars out: %d %d %d %d\n", nowMs() % 100000, bars[0], bars[2], bars[4], bars[6]);
        g_dev.send(::uf1::buildScreen(::uf1::scr::kVpotBars, bars));
    }
    if (force || styles != shownStyles) { shownStyles = styles; g_dev.send(::uf1::buildScreen(::uf1::scr::kVpotStyle, styles)); }
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
std::atomic<int> g_trackNo{0};   // from the script (SysEx 0E): the selected track's MixConsole position, 0 = unknown
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
    if (auto n = parseName(m)) {
        if (n->strip != 0) return;
        std::string t(n->text.begin(), n->text.end());
        // An empty name means the selection landed on something without a mixer channel (a folder, a
        // marker track, or nothing). Keep showing the last track rather than blanking the display; the
        // colour that follows (all zero) is skipped too.
        g_emptySelection = t.empty();
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
        c.pos = uint8_t(m[6] > 100 ? 100 : m[6]);
        c.used = !c.name.empty() || !c.value.empty();
        if (g_trace) std::printf("%lld cell %d in: pos %d \"%s\"\n", nowMs() % 100000, int(m[5]), int(c.pos), c.value.c_str());
        lk.unlock();
        // Drawn at once, like the value line (which turns smoothly): a 40 ms painter sampled the updates at
        // its own beat (Cubase sends every 20-40 ms), so some frames moved two steps and some none.
        if (g_view == kViewSends) paintSendCells(false);
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
        const char* banner = "Cubase-Sixty";
        g_dev.send(::uf1::buildScreen(::uf1::scr::kHeaderRow,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(banner), 12)));
        g_dev.send(::uf1::buildMotorEnable(false));
        // Cells inside the colour bar that the UF1's start-up state leaves filled: the text cell (Rea-Sixty
        // writes 12 spaces for "nothing" - never empty, that drops the layer) and the channel number.
        screenText(::uf1::scr::kCsType, std::string(12, ' '));
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

void uf1SetStripNumber(int n)
{
    if (g_stripNo.exchange(n) != n) showStripNumber();
}

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
