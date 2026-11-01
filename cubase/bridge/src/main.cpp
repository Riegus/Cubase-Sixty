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

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
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

using namespace cs;

namespace {

constexpr const char* kPortToCubase   = "Cubase-Sixty UF8 to Cubase";    // we send here
constexpr const char* kPortFromCubase = "Cubase-Sixty UF8 from Cubase";  // we listen here

std::atomic<bool> g_run{true};
void onSignal(int) { g_run = false; }

uf8::UF8Device g_dev;
MidiOutPort    g_out;
MidiInPort     g_in;
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
void setLed(int strip, int cls, bool on);
std::atomic<bool> g_cubaseSpoke{false};   // set once Cubase has sent us a name or colour

void sendMidi(std::vector<unsigned char> m)
{
    if (g_trace8 && !(m.size() && (m[0] & 0xF0) == 0xE0)) {
        std::printf("[UF8 MIDI out]");
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
        if (auto n = buttonToNote(d[3])) {
            // An unplugged foot-switch jack reports itself as "pressed" while the UF8 starts up,
            // which started the transport. Ignore the foot switches for the first seconds.
            static const auto t0 = std::chrono::steady_clock::now();
            const bool foot = (*n == 62 || *n == 63);
            if (foot && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4)) break;
            if (*n >= kNoteTopSoft && *n < kNoteTopSoft + kStrips && g_noRec[*n - kNoteTopSoft]) break;   // cannot arm
            note(*n, pressed);
        }
        break;
    }
    case 0x24: {   // v-pot: FF 24 02 strip raw ck
        const uint8_t s = d[3];
        if (s >= kStrips) return;
        const int delta = uf8frames::vpotDelta(d[4]);
        if (delta != 0) sendMidi({0xB0, uint8_t(kCcVPot + s), vpotToCc(delta)});
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
    g_dev.send(frames.ff39);
    if (!frames.legacy.empty()) g_dev.send(frames.legacy);
}

// cls: 0 solo, 1 cut, 2 sel, 3 top soft key, 4 automation Read, 5 automation Write
void setLed(int strip, int cls, bool on)
{
    switch (cls) {
    case 0: case 1: case 2:
        sendLedFrames(uf8::buildLedColourPair(uint8_t(strip), static_cast<uf8::LedClass>(cls), on));
        break;
    case 3:
        if (strip >= 0 && strip < 8) {
            g_recOn[strip] = on;
            if (g_trace8) std::printf("UF8 rec strip %d: Cubase says %s\n", strip + 1, on ? "ON" : "OFF");
        }
        sendLedFrames(uf8::buildTopSoftKeyLed(uint8_t(strip),
            (strip >= 0 && strip < 8 && g_noRec[strip]) ? uf8::TopSoftKeyState::Off
                : on ? uf8::TopSoftKeyState::On : uf8::TopSoftKeyState::Dim, uf8::ledColourRed()));   // record-enable = red
        break;
    case 4: sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::AutoRead, on)); break;
    case 5: sendLedFrames(uf8::buildUf8GlobalLed(uf8::Uf8GlobalLed::AutoWrite, on)); break;
    default: break;
    }
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

void onMidiIn(std::vector<unsigned char>& msg)
{
    const auto& m = msg;
    if (m.empty()) return;
    const uint8_t status = m[0] & 0xF0;

    if (m[0] == 0xF0) {   // SysEx: names and colours
        g_cubaseSpoke = true;
        if (auto n = parseName(m)) {
            std::string t(n->text.begin(), n->text.end());
            if (t.size() > uf8::kUf8ScribbleChars) t.resize(uf8::kUf8ScribbleChars);
            g_dev.send(uf8::buildStripTextUpper(uint8_t(n->strip), t));
        } else if (auto c = parseColour(m)) {
            { std::lock_guard<std::mutex> lk(g_paletteMx); g_palette[c->strip] = colourToPalette(c->r, c->g, c->b); }
            pushPalette();
        }
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
            setLed(s, 3, g_recOn[s]);
            return;
        }
        if (auto t = noteToLed(m[1])) {
            const bool on = status != 0x80 && m[2] > 0;
            setLed(t->strip, t->cls, on);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--trace") { cs::uf1SetTrace(true); g_trace8 = true; cs::midiPortsTrace(true); }
        else if (std::string(argv[i]) == "--loopmidi") cs::midiPortsPreferLoopMidi(true);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // Each surface is optional: it is skipped (with a message) if its ports or device are missing.
    bool uf8Ok = false;
    {
        if (!g_dev.open()) {
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
            uf8Ok = true;
        }
    }
    const bool uf1Ok = cs::uf1Start();
    if (!uf8Ok && !uf1Ok) {
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
        if ((uf8Ok && g_dev.needsReopen()) || cs::uf1NeedsReopen()) {
            std::fprintf(stderr, "A surface went away; exiting (restart the bridge).\n");
            break;
        }
    }
    if (uf8Ok) g_dev.close();
    cs::uf1Stop();
    return 0;
}
