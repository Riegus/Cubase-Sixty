// UC1 side of the bridge.
//
// The UC1 is driven by the Cubase-Sixty UC1 MIDI Remote script, which finds the SSL plug-ins on the selected
// track (Cubase's Direct Access API) and sets their parameters. The bridge only moves events and display
// content:
//
//   UC1 -> script   SysEx 20 <knob id> <64 + clicks>     a knob turned (clicks signed, usually +-1..3)
//                   SysEx 21 <button id> <1 pressed / 0 released>
//                   note 100 / 101 (press + release)     CHANNEL encoder: next / previous track
//                   note <id> (press only, velocity 127) Polarity 0x19, Solo Clear 0x1B, Solo 0x1C, Cut 0x1D:
//                                                         bound to host values in the script
//   script -> UC1   SysEx 30 <name>                       the selected track's name (3 x 7 bits per character)
//                   SysEx 31 <r> <g> <b>                  its colour (7 bits each)
//                   SysEx 32 <label>                      the plug-in shown (model field: "CS 2", "4K E", ...; empty = none)
//                   SysEx 33 <zone> <text>                a value readout: zone 03 Channel Strip, 05 Bus Comp
//                   SysEx 34 <button id> <0 off / 1 on / 2 dim>   a button LED
//                   SysEx 35 <zone>                       clear a readout zone
//                   note <id> (velocity > 0 = on)         LEDs of the note-bound buttons (Solo, Cut, Polarity)
//
// Byte maps (knob / button ids, LED cells, frame layouts) are Rea-Sixty's (extension/src/UC1Protocol.h).

#include "Uc1Bridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "RtMidi.h"
#include "MidiPorts.h"
#include "Mapping.h"
#include "Palette.h"
#include "UC1Device.h"
#include "UC1Protocol.h"

namespace cs {
namespace {

constexpr const char* kPortTo   = "Cubase-Sixty UC1 to Cubase";
constexpr const char* kPortFrom = "Cubase-Sixty UC1 from Cubase";
constexpr uint8_t kNoteNextTrack = 100, kNotePrevTrack = 101;

::uc1::UC1Device g_dev;
MidiOutPort g_out;
MidiInPort  g_in;
std::mutex g_outMx;
std::atomic<bool> g_open{false};
std::atomic<bool> g_trace{false};

// What the display shows, kept so it can be painted again in SSL 360's order (and after a reopen).
std::mutex g_stateMx;
std::string g_name, g_label;
uint8_t g_palette = 0;
std::map<int, std::vector<unsigned char>> g_cache;   // last message per kind, replayed after a reopen

void traceMidi(const char* dir, const std::vector<unsigned char>& m)
{
    if (!g_trace) return;
    std::printf("[UC1 MIDI %s]", dir);
    for (auto b : m) std::printf(" %02X", b);
    std::printf("\n");
}

void sendMidi(std::vector<unsigned char> m)
{
    traceMidi("out", m);
    std::lock_guard<std::mutex> lk(g_outMx);
    try { g_out.send(m); } catch (const RtMidiError& e) { std::fprintf(stderr, "uc1 midi out: %s\n", e.getMessage().c_str()); }
}
void sysex(std::initializer_list<unsigned char> body)
{
    std::vector<unsigned char> m{0xF0, 0x7D, 0x52, 0x36};
    m.insert(m.end(), body);
    m.push_back(0xF7);
    sendMidi(std::move(m));
}
void note(uint8_t n, bool on) { sendMidi({uint8_t(on ? 0x90 : 0x80), n, uint8_t(on ? 127 : 0)}); }

// Names arrive as 3 x 7 bits per UTF-16 character; the UC1 shows Latin-1.
std::string decodeName(const std::vector<unsigned char>& m, size_t from)
{
    std::string out;
    for (size_t i = from; i + 2 < m.size() && m[i] != 0xF7 && m[i + 1] != 0xF7 && m[i + 2] != 0xF7; i += 3) {
        const unsigned c = (unsigned(m[i]) << 14) | (unsigned(m[i + 1]) << 7) | m[i + 2];
        out.push_back(c < 256 ? char(c) : '?');
    }
    return out;
}

void send(std::vector<uint8_t> f) { if (g_open) g_dev.send(std::move(f)); }

// The central display, in SSL 360's order (Rea-Sixty UC1Surface::refresh): layout mode, model field, the large
// (Bus Comp) name row, the small (Channel Strip) name row, the track colour.
void paintCentre()
{
    std::string name, label;
    uint8_t palette;
    { std::lock_guard<std::mutex> lk(g_stateMx); name = g_name; label = g_label; palette = g_palette; }
    if (name.size() > 12) name.resize(12);
    send(::uc1::buildColourBarEnable(!label.empty()));
    send(::uc1::buildCentralLabel(label));
    send(::uc1::buildTrackNameTripleLarge("", "", ""));
    send(::uc1::buildTrackNameTripleSmall("", name, ""));
    send(::uc1::buildFocusedColour(palette));
}

// A button LED (Rea-Sixty UC1Surface::setButtonLed_): the EQ and dynamics buttons need a selection frame
// (bank 01) and a brightness frame (bank 02); Solo / Cut / Polarity / Channel IN / Bus Comp IN light with bank 01
// state 01; Fine with one bank-02 frame.
void setLed(uint8_t id, int state)
{
    namespace B = ::uc1::button;
    namespace L = ::uc1::led;
    L::Cell cell{0, 0};
    switch (id) {
        case B::kHfBell: cell = L::kHfBell; break;           case B::kEqType: cell = L::kEqType; break;
        case B::kEqIn: cell = L::kEqIn; break;               case B::kLfBell: cell = L::kLfBell; break;
        case B::kBusCompIn: cell = L::kBusCompIn; break;     case B::kFastAttComp: cell = L::kFastAttComp; break;
        case B::kPeak: cell = L::kPeak; break;               case B::kDynIn: cell = L::kDynIn; break;
        case B::kExpand: cell = L::kExpand; break;           case B::kFastAttGate: cell = L::kFastAttGate; break;
        case B::kPolarity: cell = L::kPolarity; break;       case B::kScListen: cell = L::kScListen; break;
        case B::kSolo: cell = L::kSolo; break;               case B::kSoloClear: cell = L::kSoloClear; break;
        case B::kCut: cell = L::kCut; break;                 case B::kChannelIn: cell = L::kChannelIn; break;
        case B::kFine: cell = L::kFine; break;
        default: return;
    }
    const bool on = state != 0, dim = state == 2;
    auto raw = [](uint8_t bank, uint8_t c, uint8_t b5, uint8_t st) {
        std::vector<uint8_t> f{0xFF, 0x13, 0x04, bank, c, b5, st};
        uint8_t sum = 0;
        for (size_t i = 1; i < f.size(); ++i) sum = uint8_t(sum + f[i]);
        f.push_back(sum);
        return f;
    };
    const uint8_t bri = !on ? 0x00 : (dim ? 0x33 : 0xFF);
    const bool dyn = id == B::kFastAttComp || id == B::kPeak || id == B::kDynIn || id == B::kExpand
                  || id == B::kFastAttGate || id == B::kScListen;
    const bool eq = id == B::kHfBell || id == B::kEqType || id == B::kEqIn || id == B::kLfBell;
    if (dyn || eq) {
        const uint8_t b5 = dyn ? 0x01 : 0x00;
        send(raw(0x01, cell.cell, b5, on ? 0x01 : 0x00));
        send(raw(0x02, cell.cell, b5, bri));
        return;
    }
    if (id == B::kSoloClear || id == B::kSolo || id == B::kCut || id == B::kPolarity || id == B::kChannelIn
        || id == B::kBusCompIn) {
        send(::uc1::buildLedWrite(id == B::kSoloClear ? cell.bank : 0x01, cell.cell, on ? 0x01 : 0x00));
        return;
    }
    send(::uc1::buildLedWrite(cell.bank, cell.cell, !on ? L::kStateOff : dim ? L::kStateDim : L::kStateOn));
}

void onSysex(const std::vector<unsigned char>& m)
{
    if (m.size() < 6 || m[1] != 0x7D || m[2] != 0x52 || m[3] != 0x36) return;
    switch (m[4]) {
    case 0x30: { std::lock_guard<std::mutex> lk(g_stateMx); g_name = decodeName(m, 5); } paintCentre(); break;
    case 0x31:
        if (m.size() >= 9) {
            const uint32_t rgb = (uint32_t(m[5]) << 17) | (uint32_t(m[6]) << 9) | (uint32_t(m[7]) << 1);
            { std::lock_guard<std::mutex> lk(g_stateMx); g_palette = rgb == 0 ? 0 : uf8::quantize(rgb); }
            paintCentre();
        }
        break;
    case 0x32: { std::lock_guard<std::mutex> lk(g_stateMx); g_label = decodeName(m, 5); } paintCentre(); break;
    case 0x33:
        if (m.size() >= 7) {
            // SSL 360's readout burst: precursor, the large name row, the text.
            const std::string text = decodeName(m, 6);
            send(::uc1::buildReadoutPrecursor(0x00));
            send(::uc1::buildTrackNameTripleLarge("", "", ""));
            send(::uc1::buildDisplayText(m[5], text, text.size()));
        }
        break;
    case 0x34: if (m.size() >= 8) setLed(m[5], m[6]); break;
    case 0x35: send(::uc1::buildDisplayInvalidate(m[5])); break;
    default: break;
    }
}

void onMidiIn(std::vector<unsigned char>& msg)
{
    const auto& m = msg;
    traceMidi("in", m);
    if (m.empty()) return;
    {
        std::lock_guard<std::mutex> lk(g_stateMx);
        if (m.size() >= 6 && m[0] == 0xF0 && m[4] == 0x34) g_cache[0x200 + m[5]] = m;   // per LED
        else if (m.size() >= 6 && m[0] == 0xF0 && m[4] != 0x33) g_cache[0x100 + m[4]] = m;
        else if (m.size() >= 3 && ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80)) g_cache[m[1]] = m;
    }
    if (m[0] == 0xF0) { onSysex(m); return; }
    if (m.size() < 3) return;
    const uint8_t status = m[0] & 0xF0;
    if (status == 0x90 || status == 0x80) setLed(m[1], (status == 0x90 && m[2] > 0) ? 1 : 0);
}

void onKnob(const ::uc1::KnobEvent& ev)
{
    if (g_trace) std::printf("[UC1] knob 0x%02X %+d\n", ev.id, ev.delta);
    if (ev.id == ::uc1::knob::kChannelEncoder) {
        const uint8_t n = ev.delta > 0 ? kNoteNextTrack : kNotePrevTrack;
        note(n, true);
        note(n, false);
        return;
    }
    const int d = std::clamp(64 + int(ev.delta), 1, 127);
    sysex({0x20, ev.id, uint8_t(d)});
}

void onButton(const ::uc1::ButtonEvent& ev)
{
    if (g_trace) std::printf("[UC1] button 0x%02X %s\n", ev.id, ev.pressed ? "pressed" : "released");
    namespace B = ::uc1::button;
    if (ev.id == B::kPolarity || ev.id == B::kSoloClear || ev.id == B::kSolo || ev.id == B::kCut) {
        note(ev.id, ev.pressed);
        return;
    }
    sysex({0x21, ev.id, uint8_t(ev.pressed ? 1 : 0)});
}

bool openDevice()
{
    if (!g_dev.open()) {
        std::fprintf(stderr, "UC1: could not open the device: %s\n", g_dev.lastError().c_str());
        return false;
    }
    g_dev.setKnobHandler(onKnob);
    g_dev.setButtonHandler(onButton);
    g_open = true;
    send(::uc1::buildLedBrightness(::uc1::brightness::kBright));
    send(::uc1::buildLcdBrightness(::uc1::brightness::kLcdBright));
    send(::uc1::buildStatusBrightness(::uc1::brightness::kStatusBright));
    // Rea-Sixty's start-up layout: plain MAIN, nothing in the readouts until a plug-in is found.
    send(::uc1::buildCentralMode(::uc1::CentralMode::Main, 0x00));
    send(::uc1::buildDisplayInvalidate(::uc1::zone::kChannelStripReadout));
    send(::uc1::buildDisplayInvalidate(::uc1::zone::kBusCompReadout));
    paintCentre();
    return true;
}

} // namespace

bool uc1Start()
{
    if (!openDevice()) return false;
    if (!g_out.open(kPortTo) || !g_in.open(kPortFrom, &onMidiIn)) {
        std::fprintf(stderr, "UC1: could not open the MIDI ports \"%s\" / \"%s\"; skipping the UC1.\n", kPortTo, kPortFrom);
        g_dev.close();
        g_open = false;
        return false;
    }
    std::printf("UC1 open (serial %s).\n", g_dev.serial().c_str());
    return true;
}

void uc1Stop()
{
    if (g_open) { g_dev.close(); g_open = false; }
}

void uc1SetTrace(bool on) { g_trace = on; }

bool uc1NeedsReopen() { return g_open && g_dev.needsReopen(); }

std::string uc1LastError() { return g_dev.lastError(); }

bool uc1Reopen()
{
    g_dev.close();
    g_open = false;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!openDevice()) return false;
    std::map<int, std::vector<unsigned char>> cache;
    { std::lock_guard<std::mutex> lk(g_stateMx); cache = g_cache; }
    for (auto& kv : cache) { auto msg = kv.second; onMidiIn(msg); }
    std::printf("UC1 reopened.\n");
    return true;
}

} // namespace cs

// Rea-Sixty's UC1Device reports every knob event to a census in its host; the bridge has none.
void reasixty_uc1KnobCensus(int, int, bool) {}
