//
// UF1 <-> Cubase. The UF1 is a single channel strip, so everything uses MIDI channel 0:
//   fader  : pitch bend (touch = note 104)       transport: notes 86/87/91..95
//   SOLO/CUT/SEL: notes 8 / 16 / 24              V-pot pushes: notes 32..37
//   encoders: CC 16 + id, relative signed-bit    LEDs: same notes coming back
// The Cubase script binds all of this to the SELECTED track.
//

#include "Uf1Bridge.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "RtMidi.h"
#include "MidiPorts.h"

#include "Mapping.h"
#include "UF1Device.h"
#include "UF1Protocol.h"

namespace cs {
namespace {

constexpr const char* kPortTo   = "Cubase-Sixty UF1 to Cubase";
constexpr const char* kPortFrom = "Cubase-Sixty UF1 from Cubase";

::uf1::UF1Device g_dev;
MidiOutPort g_out;
MidiInPort  g_in;
std::mutex g_outMx;
std::atomic<bool> g_touched{false};
std::atomic<bool> g_open{false};
std::atomic<bool> g_trace{false};
bool g_engaged = false;
std::mutex g_engMx;
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

void onUf1Event(const ::uf1::InputEvent& ev)
{
    using K = ::uf1::InputKind;
    if (g_trace && ev.kind != K::FaderPosition)
        std::printf("[UF1 event] kind=%d id=0x%02X pressed=%d delta=%d\n", int(ev.kind), ev.id, int(ev.pressed), ev.delta);
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
            if ((acc[k] > 0) != (ev.delta > 0)) acc[k] = 0;  // direction change restarts the count
            acc[k] += ev.delta;
            const uint8_t base = ev.id == 5 ? 40 : 42;
            while (acc[k] >= kDetentsPerPulse[k] || acc[k] <= -kDetentsPerPulse[k]) {
                const bool right = acc[k] > 0;
                acc[k] += right ? -kDetentsPerPulse[k] : kDetentsPerPulse[k];
                note(uint8_t(base + (right ? 1 : 0)), true);
                note(uint8_t(base + (right ? 1 : 0)), false);
            }
            break;
        }
        sendMidi({0xB0, uint8_t(uf1m::kCcEncoder + ev.id), vpotToCc(ev.delta)});
        break;
    case K::Button:
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

// Screen text zone: 0x00, then the text (the UF1 reads one byte per glyph, so ASCII only here).
void screenText(uint16_t addr, const std::string& text)
{
    std::vector<uint8_t> p;
    p.push_back(0x00);
    p.insert(p.end(), text.begin(), text.end());
    g_dev.send(::uf1::buildScreen(addr, p));
}

// Sysex from the Cubase script: 01 name, 02 colour (strip 0), 03 level text.
void onSysex(const std::vector<unsigned char>& m)
{
    if (!isOurSysex(m)) return;
    if (auto n = parseName(m)) {
        if (n->strip != 0) return;
        std::string t(n->text.begin(), n->text.end());
        if (t.size() > 8) t.resize(8);   // the UF1 name cell is 8 characters wide
        screenText(::uf1::scr::kTrackName, t);
    } else if (auto c = parseColour(m)) {
        if (c->strip != 0) return;
        const uint8_t idx = colourToPalette(c->r, c->g, c->b);
        const uint8_t active = 0x01;
        g_dev.send(::uf1::buildScreen(::uf1::scr::kChActive, std::span<const uint8_t>(&active, 1)));
        g_dev.send(::uf1::buildScreen(::uf1::scr::kColourBar, std::span<const uint8_t>(&idx, 1)));
    } else if (m[4] == 0x04) {
        // Value line (send name + level of the V-pot just touched), 12 characters at most.
        std::string t;
        for (size_t i = 5; i + 1 < m.size(); ++i) t.push_back(char(m[i]));
        if (t.size() > 12) t.resize(12);
        std::printf("UF1 value line: \"%s\"\n", t.c_str());
        screenText(::uf1::scr::kValueLine, t);
    } else if (m[4] == 0x05 && m.size() >= 7) {
        // Readout bar under the value line: position 0..100.
        const uint8_t pb[2] = { uint8_t(m[5] > 100 ? 100 : m[5]), 0x00 };
        g_dev.send(::uf1::buildScreen(::uf1::scr::kVPotReadoutBar, pb));
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
    if (!m.empty() && m[0] == 0xF0) { onSysex(m); return; }
    if (m.size() < 3) return;
    const uint8_t status = m[0] & 0xF0;
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

bool uf1Start()
{
    if (!g_dev.open()) {
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
        screenText(::uf1::scr::kValueLine, "READY");   // start-up check: does the value line show anything at all?
        const uint8_t halfBar[2] = {50, 0x00};
        g_dev.send(::uf1::buildScreen(::uf1::scr::kVPotReadoutBar, halfBar));
        const uint8_t fillStyle = 0x02;   // bar style: fill from the left (the init leaves it off)
        g_dev.send(::uf1::buildScreen(::uf1::scr::kBarStyle, std::span<const uint8_t>(&fillStyle, 1)));
        g_dev.send(::uf1::buildMasterBrightness(0xff));
        g_dev.send(::uf1::buildLedBrightness(0x20));
    }
    // Ports last, so Cubase's first burst of state (sent when it sees the ports appear) finds the screen ready.
    if (!g_out.open(kPortTo) || !g_in.open(kPortFrom, &onMidiIn)) {
        std::fprintf(stderr, "UF1: could not open the MIDI ports \"%s\" / \"%s\"; skipping the UF1.\n", kPortTo, kPortFrom);
        g_dev.close();
        g_open = false;
        return false;
    }
    std::printf("UF1 open (serial %s).\n", g_dev.serial().c_str());
    return true;
}

void uf1Stop()
{
    if (g_open) { g_dev.close(); g_open = false; }
}

void uf1SetTrace(bool on) { g_trace = on; }

bool uf1NeedsReopen() { return g_open && g_dev.needsReopen(); }

} // namespace cs
