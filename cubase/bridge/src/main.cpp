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

#include "Mapping.h"
#include "Protocol.h"
#include "UF8Device.h"
#include "Uf8Fader.h"
#include "Uf8Frames.h"

using namespace cs;

namespace {

constexpr const char* kPortToCubase   = "Cubase-Sixty UF8 to Cubase";    // we send here
constexpr const char* kPortFromCubase = "Cubase-Sixty UF8 from Cubase";  // we listen here

std::atomic<bool> g_run{true};
void onSignal(int) { g_run = false; }

uf8::UF8Device g_dev;
RtMidiOut      g_out;
RtMidiIn       g_in;
std::mutex     g_outMx;
uf8frames::Assembler g_asm;

std::atomic<bool> g_touched[kStrips];
bool              g_engaged[kStrips] = {};      // motor engaged (guarded by g_faderMx)
std::mutex        g_faderMx;
uint8_t           g_palette[kStrips] = {};
std::mutex        g_paletteMx;

void sendMidi(std::vector<unsigned char> m)
{
    std::lock_guard<std::mutex> lk(g_outMx);
    try { g_out.sendMessage(&m); } catch (const RtMidiError& e) { std::fprintf(stderr, "midi out: %s\n", e.getMessage().c_str()); }
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
        if (auto n = buttonToNote(d[3])) note(*n, pressed);
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

void setLed(int strip, int cls, bool on)
{
    const auto frames = uf8::buildLedColourPair(uint8_t(strip), static_cast<uf8::LedClass>(cls), on);
    g_dev.send(frames.ff38);
    g_dev.send(frames.ff39);
    if (!frames.legacy.empty()) g_dev.send(frames.legacy);
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

void onMidiIn(double, std::vector<unsigned char>* msg, void*)
{
    const auto& m = *msg;
    if (m.empty()) return;
    const uint8_t status = m[0] & 0xF0;

    if (m[0] == 0xF0) {   // SysEx: names and colours
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
        if (auto t = noteToLed(m[1])) {
            const bool on = status != 0x80 && m[2] > 0;
            setLed(t->strip, t->cls, on);
        }
    }
}

int findPort(RtMidi& api, const std::string& needle)
{
    for (unsigned i = 0; i < api.getPortCount(); ++i)
        if (api.getPortName(i).find(needle) != std::string::npos) return int(i);
    return -1;
}

} // namespace

int main()
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const int po = findPort(g_out, kPortToCubase);
    const int pi = findPort(g_in, kPortFromCubase);
    if (po < 0 || pi < 0) {
        std::fprintf(stderr, "Could not find loopMIDI ports \"%s\" and \"%s\".\n"
                             "Create them in loopMIDI first (see cubase/midi-remote/README.md).\n",
                     kPortToCubase, kPortFromCubase);
        return 1;
    }
    g_out.openPort(unsigned(po), kPortToCubase);
    g_in.ignoreTypes(false, true, true);   // keep SysEx
    g_in.setCallback(&onMidiIn);
    g_in.openPort(unsigned(pi), kPortFromCubase);

    if (!g_dev.open()) {
        std::fprintf(stderr, "Could not open the UF8: %s\n"
                             "Is SSL 360 (or SSL 360 Core) still running? Quit it first.\n"
                             "On Windows the UF8 also needs the WinUSB driver (see extension/resources/rea_sixty_winusb.inf).\n",
                     g_dev.lastError().c_str());
        return 2;
    }
    g_dev.setRawInputHandler(onUsbInput);
    std::printf("UF8 open (serial %s). Bridging; Ctrl+C to stop.\n", g_dev.serial().c_str());

    while (g_run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (g_dev.needsReopen()) {
            std::fprintf(stderr, "UF8 went away; exiting (restart the bridge).\n");
            break;
        }
    }
    g_dev.close();
    return 0;
}
