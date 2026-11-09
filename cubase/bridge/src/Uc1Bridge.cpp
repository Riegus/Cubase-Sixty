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
//                   SysEx 36 <knob id> <hi> <lo> <dim>    a knob's LED ring: position 0..16382 (7F 7F = off)
//                   SysEx 37 <hi> <lo>                    the 7-segment number (0..999; 7F 7F = blank)
//                   SysEx 38 <prev> 7F 7F 7F <curr> 7F 7F 7F <next>   the Bus Comp name row (tracks with a bus comp)
//                   SysEx 39 <header>                     show the Bus Comp scroll header (e.g. "BUS COMP 2") for 1.5 s
//                   SysEx 3B <cs plug-in> 7F 7F 7F <bc plug-in>   the SSL plug-ins the two sections are on (names; empty =
//                                                         none), for the PRESETS screen
//                   SysEx 3F <f-in> <f-sc> <eq-sc> <dyn-pre-eq> <ext-sc>   the channel strip's routing switches (0 / 1;
//                                                         7F = the strip has no routing), for the ROUTING screen
//   UC1 -> script   SysEx 3E <f-in> <f-sc> <eq-sc> <dyn-pre-eq> <ext-sc>   set the routing (ROUTING screen)
//                   SysEx 40 <cs track hi> <lo> <cs ordinal> <bc track hi> <lo> <bc ordinal> <cs model>
//                                                         (cs ordinal: among the SSL strips / bus comps on the track;
//                                                         bc ordinal: among the bus comps on its track)
//                                                         where the gain-reduction meters read from (see "Gain reduction")
//                   SysEx 3A <cc> <value>                 send CC <cc> <value> (channel 1) back to Cubase: lets the script
//                                                         set a host value it has bound to a CC (a strip module's on switch)
// The two encoders send one step per detent (Rea-Sixty's accumulator: 3 ticks per step, reset after a 100 ms
// pause or a change of direction): CHANNEL as notes 100 / 101, the Bus Comp encoder as SysEx 20 15 <64 + steps>.
//                   note <id> (velocity > 0 = on)         LEDs of the note-bound buttons (Solo, Cut, Polarity)
//
// Byte maps (knob / button ids, LED cells, frame layouts) are Rea-Sixty's (extension/src/UC1Protocol.h).

#include "Uc1Bridge.h"
#include "Names.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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
#include "Favourites.h"
#include "Settings.h"
#include "Uf1Bridge.h"
#include "ScriptLog.h"
#include "SslCoreImpersonator.h"
#include "SslPresets.h"
#include "Uf1Bridge.h"

namespace cs {
bool netInit();   // NetInit.cpp
int stopSslCore(); // SslCoreGuard.cpp
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
std::string g_bcNames[3];                            // the Bus Comp name row: previous, current, next
std::string g_header;                                // the Bus Comp scroll header while it shows
std::chrono::steady_clock::time_point g_headerUntil; // when it goes again
int g_headerGen = 0;
int g_sevenSeg = -1;                                 // the 7-segment number, -1 = blank
uint8_t g_palette = 0;
std::map<int, std::vector<unsigned char>> g_cache;   // last message per kind, replayed after a reopen
bool g_ownText = false;        // the bridge feeds its own text through onMidiIn ("Waiting for Cubase")
bool g_waitingShown = false;   // that text is on the UC1 until Cubase sends something

// The PRESETS screen (see "PRESETS screen" below).
std::mutex g_presetMx;
std::atomic<bool> g_presetsOn{false};   // the PRESETS screen is showing (the main screen's painting waits)
bool g_presetsBrowse = false;    // false: the CS / BC selector; true: the list
bool g_presetsCs = true;         // which section's plug-in is browsed
std::string g_presetGroup;       // the folder shown ('/'-separated below the plug-in's folder)
int g_presetSel = 0;
std::string g_csPlugin, g_bcPlugin;   // from the script (SysEx 3B)
std::atomic<bool> g_routingOn{false};   // the ROUTING screen is showing
// The channel strip's routing switches as the script reports them (SysEx 3F): -1 = the strip has none.
int g_routing[5] = {-1, -1, -1, -1, -1};   // filters to input, filters to S/C, EQ to S/C, dynamics pre EQ, external S/C
std::atomic<bool> g_extOn{false};       // the EXT FUNCS screen is showing (the script draws it, SysEx 4A)
bool menuOn() { return g_presetsOn || g_routingOn || g_extOn; }
// Gain reduction: which SSL plug-in instances the meters read (SysEx 40). Track = the channel's MixConsole number
// (0 = none), ordinal = the plug-in's position among the SSL plug-ins on that channel (insert order).
std::mutex g_grMx;
int g_grCsTrack = 0, g_grCsOrd = 0, g_grBcTrack = 0, g_grBcOrd = 0;
std::string g_grCsModel;
void renderRouting();   // the ROUTING screen (below)

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
std::string decodeName(const std::vector<unsigned char>& m, size_t from, size_t* end = nullptr)
{
    std::string out;
    size_t i = from;
    for (; i + 2 < m.size() && m[i] != 0xF7 && m[i + 1] != 0xF7 && m[i + 2] != 0xF7; i += 3) {
        if (m[i] == 0x7F && m[i + 1] == 0x7F && m[i + 2] == 0x7F) break;   // separator
        const unsigned c = (unsigned(m[i]) << 14) | (unsigned(m[i + 1]) << 7) | m[i + 2];
        out.push_back(c < 256 ? char(c) : '?');
    }
    if (end) *end = i;
    return out;
}

void send(std::vector<uint8_t> f) { if (g_open) g_dev.send(std::move(f)); }

// The central display, in SSL 360's order (Rea-Sixty UC1Surface::refresh): layout mode, model field, the large
// (Bus Comp) name row, the small (Channel Strip) name row, the track colour.
std::vector<uint8_t> largeRow()
{
    std::lock_guard<std::mutex> lk(g_stateMx);
    auto cut = [](const std::string& n) { return fitName(n, 12); };   // the glass shows 12 (abbreviated or cut: Settings)
    return ::uc1::buildTrackNameTripleLarge(cut(g_bcNames[0]), cut(g_bcNames[1]), cut(g_bcNames[2]));
}
// While the Bus Comp scroll header shows, the model field gives way to it (SSL 360: layout 02 + header).
void paintCentre()
{
    if (menuOn()) return;
    std::string name, label, header;
    uint8_t palette;
    bool headerOn;
    {
        std::lock_guard<std::mutex> lk(g_stateMx);
        name = g_name; label = g_label; palette = g_palette; header = g_header;
        headerOn = !header.empty() && std::chrono::steady_clock::now() < g_headerUntil;
    }
    name = fitName(name, 12);   // abbreviated or cut (Settings: Long track names)
    if (headerOn) {
        send(::uc1::buildCentralMode(::uc1::CentralMode::Main, 0x02));
        send(::uc1::buildLcdHeader(header));
    } else {
        send(::uc1::buildColourBarEnable(!label.empty()));
        send(::uc1::buildCentralLabel(label));
    }
    send(largeRow());
    send(::uc1::buildTrackNameTripleSmall("", name, ""));
    send(::uc1::buildFocusedColour(palette));
}

void showHeader(const std::string& text)
{
    if (menuOn()) return;
    int gen;
    {
        std::lock_guard<std::mutex> lk(g_stateMx);
        g_header = text;
        g_headerUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        gen = ++g_headerGen;
    }
    paintCentre();
    std::thread([gen] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1550));
        {
            std::lock_guard<std::mutex> lk(g_stateMx);
            if (gen != g_headerGen) return;   // turned again: that one reverts
            g_header.clear();
        }
        if (menuOn()) return;
        send(::uc1::buildCentralMode(::uc1::CentralMode::Main, 0x00));
        paintCentre();
    }).detach();
}

// The 7-segment display: three digits (cells 0x10 ones, 0x08 tens, 0x00 hundreds; segments a..g), written
// like Rea-Sixty's buildSevenSeg but with leading zeros blanked, and all blank for -1.
void paintSevenSeg()
{
    int value;
    { std::lock_guard<std::mutex> lk(g_stateMx); value = g_sevenSeg; }
    static const uint8_t kDigit[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const int v = std::clamp(value, -1, 999);
    const uint8_t bases[3] = {0x10, 0x08, 0x00};
    for (int d = 0; d < 3; ++d) {
        const int place = d == 0 ? 1 : d == 1 ? 10 : 100;
        const bool blank = v < 0 || (d > 0 && v < place);
        const uint8_t segs = blank ? 0 : kDigit[(v / place) % 10];
        for (int seg = 0; seg < 7; ++seg) {
            auto f = ::uc1::buildLedWrite(0x01, uint8_t(bases[d] + seg), (segs >> seg) & 1);
            f[5] = 0x00;   // the 7-segment address space
            uint8_t sum = 0;
            for (size_t i = 1; i + 1 < f.size(); ++i) sum = uint8_t(sum + f[i]);
            f.back() = sum;
            send(std::move(f));
        }
    }
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


// ---------------------------------------------------------------------------
// Knob rings (copied from Rea-Sixty UC1Surface.cpp: the cell maps and ringFor; pushRing is its pushKnobRing_).
// ---------------------------------------------------------------------------
enum class RingMode : uint8_t { Position = 0, Gradient = 1 };
// Most rings use a uniform byte5 (the section "role" byte) for every
// cell. A few have a quirk LED whose byte5 differs — e.g. BC Release
// LED 7/7 lives at cell 0x00 byte5=0x01 (decoded from uc1_34 sweep)
// because cell 0x00 byte5=0x00 is taken by the hundreds 7-seg
// segment 'a'. `b5Override` is an optional parallel array — when
// non-null, b5Override[i] supersedes the knob-default byte5 for
// cell i. Keep it null for rings without quirks.
struct RingDef {
    const uint8_t* cells;
    int            nCells;
    RingMode       mode = RingMode::Position;
    const uint8_t* b5Override = nullptr;
};

// ---- EQ section (12 knobs) ----
// All rings contiguous 11 cells. Earlier guess that the centre cell of
// each Gain pot was a separately-driven 0-dB indicator was wrong — the
// user reported the 12 o'clock LED missing on all four Gains after we
// excluded those cells. The capture happened to miss writes to those
// cells (sweep timing artefact), but they ARE part of the ring.
constexpr uint8_t kLpfCells[]    = {0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F};
constexpr uint8_t kHpfCells[]    = {0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94};
constexpr uint8_t kHfGainCells[] = {0x7E,0x7F,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88};
constexpr uint8_t kHfFreqCells[] = {0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D};
constexpr uint8_t kHmfGainCells[]= {0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72};
constexpr uint8_t kHmfFreqCells[]= {0x5D,0x5E,0x5F,0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67};
constexpr uint8_t kHmfQCells[]   = {0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C};
constexpr uint8_t kLmfGainCells[]= {0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F};
constexpr uint8_t kLmfFreqCells[]= {0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44};
constexpr uint8_t kLmfQCells[]   = {0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39};
constexpr uint8_t kLfFreqCells[] = {0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E};
constexpr uint8_t kLfGainCells[] = {0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22};

// ---- Channel Strip I/O (Input Trim + Fader Level / Output Gain) ----
// uc1_32: Input Trim — 11 contiguous cells, 3-step brightness, byte5=0x00.
constexpr uint8_t kInputTrimCells[]  = {0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA};
// Fader Level / Output Gain — knob 0x16, byte5=0x01. Confirmed in
// Output Gain / Fader Level — full 11-LED ring per user 2026-05-01.
// First-revision map (0x0C..0x16) was off-by-one: 0x0C belongs to
// BC Mix LED 11/11 (BC Mix's high-address neighbour, byte5=0x01).
// Real Output Gain range is 0x0D..0x17. The 7-seg digit cells at
// 0x10..0x16 are byte5=0x00 — independent address space, no collision.
constexpr uint8_t kFaderLevelCells[] = {0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17};

// ---- Dyn / Gate section (7 knobs) ----
constexpr uint8_t kGateReleaseCells[]   = {0x7C,0x7D,0x7E,0x7F,0x80,0x81,0x82,0x83,0x84,0x85,0x86};
constexpr uint8_t kGateHoldCells[]      = {0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91};
// 11-LED ring (user 2026-05-01) — earlier 10-cell map missed the CCW LED.
constexpr uint8_t kGateThresholdCells[] = {0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B};
// Gate Range = 11-cell ring 0x66..0x70. The 5 cells BELOW the ring
// (0x61..0x65) are the **Gate GR meter** — the right-hand of the two
// 5-LED GR strips on the Channel Strip Dynamics section (Comp GR is
// the left strip at 0x5C..0x60). Earlier guess that the ring was
// 0x61..0x6B was wrong — the dot bled into the Gate GR strip at
// CCW positions because we were addressing GR cells as ring cells.
constexpr uint8_t kGateRangeCells[]     = {0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70};
constexpr uint8_t kCompReleaseCells[]   = {0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A};
// 11-LED rings (user 2026-05-01) — earlier 10-cell maps missed the CCW LED.
constexpr uint8_t kCompThresholdCells[] = {0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F};
constexpr uint8_t kCompRatioCells[]     = {0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44};

// ---- Bus Comp section (7 knobs) ----
// Cell maps confirmed in uc1_31 (2026-04-26). All BC pots use byte5=0x00
// EXCEPT BC Mix which uses byte5=0x01 (a quirk — Mix shares the address
// space with Dyn/Gate rings rather than the rest of BC).
constexpr uint8_t kBcRatioCells[]    = {0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC};
constexpr uint8_t kBcScHpfCells[]    = {0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5};
constexpr uint8_t kBcAttackCells[]   = {0xDD,0xDE,0xDF,0xE0,0xE1,0xE2,0xE3};
// BC Release: 7 LEDs. First 6 at byte5=0x00 (0xFA..0xFF). LED 7/7
// (Auto position, full-CW) lives at cell 0x00 byte5=0x01 — quirk
// decoded from uc1_34_bc_release_sweep at t=4.08s. Cell 0x00 at
// byte5=0x00 is the hundreds 7-seg segment 'a', so SSL360 routes
// the wrap LED through the byte5=0x01 address space instead.
constexpr uint8_t kBcReleaseCells[]      = {0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,0x00};
constexpr uint8_t kBcReleaseB5Override[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x01};
// BC Threshold: 11 LEDs (user 2026-05-01) — extend CW past previous 10-cell map.
constexpr uint8_t kBcThresholdCells[]= {0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE};
// BC Makeup: 11 LEDs (user 2026-05-01) — extend CCW past previous 10-cell map.
constexpr uint8_t kBcMakeupCells[]   = {0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9};
// BC Mix — byte5=0x01, 11 LEDs (user 2026-05-01); previous 9-cell map
// missed both the CCW and CW LEDs. 0x0C is BC Mix LED 11/11 (Output
// Gain map starts at 0x0D right above it).
constexpr uint8_t kBcMixCells[]      = {0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C};

const RingDef* ringFor(uint8_t knobId)
{
    constexpr auto P = RingMode::Position;
    constexpr auto G = RingMode::Gradient;
    static const RingDef kLpf       {kLpfCells,        11, P};
    static const RingDef kHpf       {kHpfCells,        11, P};
    static const RingDef kHfGain    {kHfGainCells,     11, G};
    static const RingDef kHfFreq    {kHfFreqCells,     11, P};
    static const RingDef kHmfGain   {kHmfGainCells,    11, G};
    static const RingDef kHmfFreq   {kHmfFreqCells,    11, P};
    static const RingDef kHmfQ      {kHmfQCells,       11, P};
    static const RingDef kLmfGain   {kLmfGainCells,    11, G};
    static const RingDef kLmfFreq   {kLmfFreqCells,    11, P};
    static const RingDef kLmfQ      {kLmfQCells,       11, P};
    static const RingDef kLfFreq    {kLfFreqCells,     11, P};
    static const RingDef kLfGain    {kLfGainCells,     11, G};
    static const RingDef kInputTrim {kInputTrimCells,  11, G};
    static const RingDef kFaderLevel{kFaderLevelCells, 11, G};
    static const RingDef kGateRelease  {kGateReleaseCells,   11, P};
    static const RingDef kGateHold     {kGateHoldCells,      11, P};
    static const RingDef kGateThr      {kGateThresholdCells, 11, G};
    static const RingDef kGateRange    {kGateRangeCells,     11, P};
    static const RingDef kCompRelease  {kCompReleaseCells,   11, P};
    static const RingDef kCompThr      {kCompThresholdCells, 11, G};
    static const RingDef kCompRatio    {kCompRatioCells,     11, G};
    static const RingDef kBcRatio   {kBcRatioCells,     7, P};
    static const RingDef kBcScHpf   {kBcScHpfCells,    11, P};
    static const RingDef kBcAttack  {kBcAttackCells,    7, P};
    static const RingDef kBcRelease {kBcReleaseCells,   7, P, kBcReleaseB5Override};
    static const RingDef kBcThr     {kBcThresholdCells,11, G};
    static const RingDef kBcMakeup  {kBcMakeupCells,   11, G};
    static const RingDef kBcMix     {kBcMixCells,      11, G};

    switch (knobId) {
        // EQ
        case ::uc1::knob::kCSLowPass:    return &kLpf;
        case ::uc1::knob::kCSHighPass:   return &kHpf;
        case ::uc1::knob::kCSHfGain:     return &kHfGain;
        case ::uc1::knob::kCSHfFreq:     return &kHfFreq;
        case ::uc1::knob::kCSHmfGain:    return &kHmfGain;
        case ::uc1::knob::kCSHmfFreq:    return &kHmfFreq;
        case ::uc1::knob::kCSHmfQ:       return &kHmfQ;
        case ::uc1::knob::kCSLmfGain:    return &kLmfGain;
        case ::uc1::knob::kCSLmfFreq:    return &kLmfFreq;
        case ::uc1::knob::kCSLmfQ:       return &kLmfQ;
        case ::uc1::knob::kCSLfFreq:     return &kLfFreq;
        case ::uc1::knob::kCSLfGain:     return &kLfGain;
        // Channel Strip I/O
        case ::uc1::knob::kCSInputTrim:      return &kInputTrim;
        case ::uc1::knob::kCSFaderLevel:     return &kFaderLevel;
        // Dyn / Gate
        case ::uc1::knob::kCSGateRelease:    return &kGateRelease;
        case ::uc1::knob::kCSGateHold:       return &kGateHold;
        case ::uc1::knob::kCSGateThreshold:  return &kGateThr;
        case ::uc1::knob::kCSGateRange:      return &kGateRange;
        case ::uc1::knob::kCSCompRelease:    return &kCompRelease;
        case ::uc1::knob::kCSCompThreshold:  return &kCompThr;
        case ::uc1::knob::kCSCompRatio:      return &kCompRatio;
        // Bus Comp
        case ::uc1::knob::kBCRatio:          return &kBcRatio;
        case ::uc1::knob::kBCScHpf:          return &kBcScHpf;
        case ::uc1::knob::kBCAttack:         return &kBcAttack;
        case ::uc1::knob::kBCRelease:        return &kBcRelease;
        case ::uc1::knob::kBCThreshold:      return &kBcThr;
        case ::uc1::knob::kBCMakeup:         return &kBcMakeup;
        case ::uc1::knob::kBCMix:            return &kBcMix;
    }
    return nullptr;
}

std::mutex g_ringMx;
std::map<uint8_t, std::vector<uint16_t>> g_ringShown;   // per knob: what each cell shows (selection | brightness << 8)

// value 0..1 (already flipped for knobs that run the other way); value < 0 clears the ring.
void pushRing(uint8_t knobId, double value, bool dim)
{
    const RingDef* def = ringFor(knobId);
    if (!def) return;
    std::vector<uint8_t> target(def->nCells, 0), bri(def->nCells, 0);
    if (value >= 0) {
        value = std::clamp(value, 0.0, 1.0);
        const uint8_t full = dim ? 0x33 : 0xFF;
        if (def->mode == RingMode::Gradient) {
            const int subMax = (def->nCells - 1) * 3;
            const int k = std::clamp(int(value * subMax + 0.5), 0, subMax);
            const int main = k / 3, frac = k % 3;
            target[main] = 1;
            bri[main] = full;
            if (frac > 0 && main + 1 < def->nCells) bri[main + 1] = frac == 1 ? 0x19 : 0x4C;
        } else {
            const int idx = std::clamp(int(value * (def->nCells - 1) + 0.5), 0, def->nCells - 1);
            target[idx] = 1;
            bri[idx] = full;
        }
    }
    const uint8_t b5Default = (knobId == ::uc1::knob::kBCMix || knobId == ::uc1::knob::kCSFaderLevel
                               || (knobId >= 0x17 && knobId <= 0x1D)) ? 0x01 : 0x00;
    auto make = [](uint8_t bank, uint8_t cell, uint8_t b5, uint8_t st) {
        std::vector<uint8_t> f{0xFF, 0x13, 0x04, bank, cell, b5, st};
        uint8_t sum = 0;
        for (size_t i = 1; i < f.size(); ++i) sum = uint8_t(sum + f[i]);
        f.push_back(sum);
        return f;
    };
    std::lock_guard<std::mutex> lk(g_ringMx);
    auto& shown = g_ringShown[knobId];
    if (int(shown.size()) != def->nCells) shown.assign(def->nCells, 0xFFFF);
    for (int i = 0; i < def->nCells; ++i) {
        const uint16_t want = uint16_t(target[i] | (bri[i] << 8));
        if (shown[i] == want) continue;
        shown[i] = want;
        const uint8_t b5 = def->b5Override ? def->b5Override[i] : b5Default;
        send(make(0x01, def->cells[i], b5, target[i]));
        send(make(0x02, def->cells[i], b5, bri[i]));
    }
}

void setPresetsMode(bool on);
void setRoutingMode(bool on);
void setExtMode(bool on);
void renderExt(const std::vector<uint8_t>& m);
void onMidiIn(std::vector<unsigned char>& msg);
// No project (the script's driver deactivated, Cubase shows the Hub): "No Project" as the track, no colour or model,
// the 7-segment blank, the knob rings and the LEDs the script drives off, any menu screen closed.
std::map<int, std::vector<unsigned char>> g_npBefore, g_npBlanked;   // the cache before / right after the blanking
bool g_noProject = false;
void uc1NoProject(const char* label = "No Project")
{
    const bool first = !g_noProject;
    g_noProject = true;
    if (first) { std::lock_guard<std::mutex> lk(g_stateMx); g_npBefore = g_cache; }
    std::vector<int> keys;
    {
        std::lock_guard<std::mutex> lk(g_stateMx);
        for (auto& kv : g_cache) keys.push_back(kv.first);
    }
    { std::lock_guard<std::mutex> lk(g_presetMx); setPresetsMode(false); setRoutingMode(false); setExtMode(false); }
    std::vector<unsigned char> name{0xF0, 0x7D, 0x52, 0x36, 0x30};
    for (char c : std::string(label)) { name.push_back(0); name.push_back(0); name.push_back(uint8_t(c)); }
    name.push_back(0xF7);
    std::vector<std::vector<unsigned char>> msgs{
        name,
        {0xF0, 0x7D, 0x52, 0x36, 0x31, 0, 0, 0, 0xF7},
        {0xF0, 0x7D, 0x52, 0x36, 0x32, 0xF7},
        {0xF0, 0x7D, 0x52, 0x36, 0x37, 0x7F, 0x7F, 0xF7},
    };
    for (int k : keys) {
        if (k >= 0x300 && k < 0x400) msgs.push_back({0xF0, 0x7D, 0x52, 0x36, 0x36, uint8_t(k - 0x300), 0x7F, 0x7F, 0, 0xF7});
        else if (k >= 0x200 && k < 0x300) msgs.push_back({0xF0, 0x7D, 0x52, 0x36, 0x34, uint8_t(k - 0x200), 0, 0xF7});
        else if (k < 0x100) msgs.push_back({0x80, uint8_t(k), 0});
    }
    for (auto& mm : msgs) onMidiIn(mm);
    // The bus comp row (the channel the UC1's bus comp is on) empty too.
    { std::lock_guard<std::mutex> lk(g_stateMx); for (auto& n : g_bcNames) n.clear(); }
    send(largeRow());
    if (first) { std::lock_guard<std::mutex> lk(g_stateMx); g_npBlanked = g_cache; }
}
// Cubase does not send again what has not changed when the project opens (the level, pan, LEDs): what was shown
// before the close is put back for every kind nothing new has arrived for since the blanking.
void uc1ProjectOpen()
{
    if (!g_noProject) return;
    g_noProject = false;
    std::vector<std::vector<unsigned char>> again;
    {
        std::lock_guard<std::mutex> lk(g_stateMx);
        for (auto& kv : g_npBefore) {
            if (kv.first == 0x17C) continue;   // the driver state
            auto b = g_npBlanked.find(kv.first);
            auto c = g_cache.find(kv.first);
            if (b != g_npBlanked.end() && c != g_cache.end() && c->second == b->second) again.push_back(kv.second);
        }
    }
    for (auto& mm : again) onMidiIn(mm);
}

void onSysex(const std::vector<unsigned char>& m)
{
    if (m.size() < 6 || m[1] != 0x7D || m[2] != 0x52 || m[3] != 0x36) return;
    if (scriptLogSysex(m.data(), m.size())) return;
    if (m[4] == 0x7C && m.size() >= 7) {   // the script's driver: 00 deactivated (no project), 01 active again
        if (m[5] == 0) uc1NoProject(); else uc1ProjectOpen();
        if (g_trace) std::printf("UC1 script %s\n", m[5] ? "active (project open)" : "inactive (no project)");
        return;
    }
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
            if (menuOn()) break;
            send(::uc1::buildReadoutPrecursor(0x00));
            send(largeRow());
            send(::uc1::buildDisplayText(m[5], text, text.size()));
        }
        break;
    case 0x34: if (m.size() >= 8) setLed(m[5], m[6]); break;
    case 0x35: if (!menuOn()) send(::uc1::buildDisplayInvalidate(m[5])); break;
    case 0x36:   // knob ring: <knob> <value hi> <value lo> (0..16382; 7F 7F = off) <1 = dim>
        if (m.size() >= 10) {
            const int v = (m[6] << 7) | m[7];
            pushRing(m[5], v == 0x3FFF ? -1.0 : v / 16382.0, m[8] == 1);
            // Some rings share cells with the 7-segment digits (Rea-Sixty: the 7-segment has to be written last).
            if (m[5] == ::uc1::knob::kBCRelease || m[5] == ::uc1::knob::kBCMix || m[5] == ::uc1::knob::kCSFaderLevel)
                paintSevenSeg();
        }
        break;
    case 0x37:
        if (m.size() >= 8) {
            const int v = (m[5] << 7) | m[6];
            { std::lock_guard<std::mutex> lk(g_stateMx); g_sevenSeg = v == 0x3FFF ? -1 : v; }
            paintSevenSeg();
        }
        break;
    case 0x38: {
        size_t at = 5;
        std::string names[3];
        for (int k = 0; k < 3; ++k) {
            size_t end = at;
            names[k] = decodeName(m, at, &end);
            at = end + 3;   // past the separator
        }
        { std::lock_guard<std::mutex> lk(g_stateMx); for (int k = 0; k < 3; ++k) g_bcNames[k] = names[k]; }
        if (!menuOn()) send(largeRow());
        break;
    }
    case 0x39: showHeader(decodeName(m, 5)); break;
    case 0x3B: {
        size_t end = 5;
        const std::string cs = decodeName(m, 5, &end);
        const std::string bc = end + 3 < m.size() ? decodeName(m, end + 3) : std::string();
        std::lock_guard<std::mutex> lk(g_presetMx);
        g_csPlugin = cs;
        g_bcPlugin = bc;
        break;
    }
    case 0x3F:
        if (m.size() >= 11) {
            std::lock_guard<std::mutex> lk(g_presetMx);
            for (int k = 0; k < 5; ++k) g_routing[k] = m[5 + k] == 0x7F ? -1 : m[5 + k];
            renderRouting();
        }
        break;
    case 0x40:
        if (m.size() >= 12) {
            std::lock_guard<std::mutex> lk(g_grMx);
            g_grCsTrack = (m[5] << 7) | m[6]; g_grCsOrd = m[7];
            g_grBcTrack = (m[8] << 7) | m[9]; g_grBcOrd = m[10];
            g_grCsModel = decodeName(m, 11);
        }
        break;
    case 0x3A: if (m.size() >= 8) sendMidi({0xB0, uint8_t(m[5] & 0x7F), uint8_t(m[6] & 0x7F)}); break;
    case 0x42: {   // the script asks for the favourites (cubase\favourites.txt) and the secondary encoder's job
        for (auto& f : favourites::messages()) sendMidi(std::move(f));
        const std::string job = settings::get("uc1-encoder2", "bus-comp");
        const uint8_t mode = job == "strips" ? 1 : job == "cs-fav" ? 2 : job == "bc-fav" ? 3 : 0;
        sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x4C, mode, 0xF7});
        break;
    }
    case 0x4B:   // the script opens EXT FUNCS (BACK on the main screen)
        if (m.size() >= 7 && m[5] == 1) { std::lock_guard<std::mutex> lk(g_presetMx); setPresetsMode(false); setRoutingMode(false); setExtMode(true); }
        break;
    case 0x4A: { std::lock_guard<std::mutex> lk(g_presetMx); renderExt(m); break; }
    case 0x41:
        // The channel strip picked with the magnifier key: on to the UF1 script (SysEx 57), so the UF1's PLUGIN
        // view shows the same one.
        if (m.size() >= 8) uf1SendToScript({0xF0, 0x7D, 0x52, 0x36, 0x57, uint8_t(m[5] & 0x7F), uint8_t(m[6] & 0x7F), 0xF7});
        break;
    default: break;
    }
}

void onMidiIn(std::vector<unsigned char>& msg)
{
    const auto& m = msg;
    traceMidi("in", m);
    if (m.empty()) return;
    if (g_waitingShown && !g_ownText) {
        // Cubase has spoken: the "for Cubase" label goes (the script may never send a label of its own).
        g_waitingShown = false;
        std::vector<unsigned char> clear{0xF0, 0x7D, 0x52, 0x36, 0x32, 0xF7};
        g_ownText = true;
        onMidiIn(clear);
        g_ownText = false;
    }
    {
        std::lock_guard<std::mutex> lk(g_stateMx);
        if (m.size() >= 6 && m[0] == 0xF0 && m[4] == 0x34) g_cache[0x200 + m[5]] = m;   // per LED
        else if (m.size() >= 6 && m[0] == 0xF0 && m[4] == 0x36) g_cache[0x300 + m[5]] = m;   // per ring
        else if (m.size() >= 6 && m[0] == 0xF0 && m[4] != 0x33 && m[4] != 0x39 && m[4] != 0x3A && m[4] != 0x3B && m[4] != 0x3F && m[4] != 0x42 && m[4] != 0x4A && m[4] != 0x4B) g_cache[0x100 + m[4]] = m;
        else if (m.size() >= 3 && ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80)) g_cache[m[1]] = m;
    }
    if (m[0] == 0xF0) { onSysex(m); return; }
    if (m.size() < 3) return;
    const uint8_t status = m[0] & 0xF0;
    if (status == 0x90 || status == 0x80) setLed(m[1], (status == 0x90 && m[2] > 0) ? 1 : 0);
}

// Rea-Sixty's encoder accumulator (UC1Surface stepFromAccumulator): the CHANNEL and Bus Comp encoders send 3 or 4
// ticks per detent; 3 ticks make a step, the rest is dropped after a 100 ms pause or a change of direction.
struct Accumulator {
    int acc = 0;
    std::chrono::steady_clock::time_point last{};
    int step(int delta)
    {
        const auto now = std::chrono::steady_clock::now();
        if (last.time_since_epoch().count() != 0 && now - last > std::chrono::milliseconds(100)) acc = 0;
        last = now;
        if ((delta > 0 && acc < 0) || (delta < 0 && acc > 0)) acc = 0;
        acc += delta;
        const int s = acc / 3;
        acc -= s * 3;
        return s;
    }
};
Accumulator g_channelAcc, g_bcAcc;

// ---------------------------------------------------------------------------
// PRESETS screen (Rea-Sixty UC1Surface: setMode / renderPresetsSubscreen_ / the PRESETS key, BC encoder, BACK and
// CONFIRM). SSL's plug-ins show the host no presets; their library is on disk, one folder per plug-in under
// %ProgramData%\Solid State Logic\PlugIns\Presets (Rea-Sixty SslPresetLibrary). The bridge owns the screen: PRESETS
// opens it on a selector (CHANNEL STRIP / BUS COMP, the Bus Comp encoder flips it, CONFIRM or the encoder push
// enters), the Bus Comp encoder then moves through folders and presets, CONFIRM steps into a folder or loads,
// BACK goes up a folder / back to the selector / out. Loading: each <PARAM id value> of the preset file goes to the
// script (SysEx 3C <section> <id> 7F 7F 7F <value>, then 3D <section> <name> at the end), which sets the parameters.
// ---------------------------------------------------------------------------
using PresetEntry = sslpresets::Entry;
using sslpresets::latin1;
struct PresetRow { std::string label; bool folder; int entry; };

std::vector<PresetEntry> g_presetList;
std::string g_presetListFor;     // the plug-in g_presetList was read for

// Sub-folders of the current folder first, then its presets (Rea-Sixty presetRows_).
std::vector<PresetRow> presetRows()
{
    std::vector<PresetRow> rows;
    std::vector<std::string> seen;
    const std::string& g = g_presetGroup;
    for (const auto& e : g_presetList) {
        const std::string& eg = e.group;
        if (eg == g) continue;
        if (!g.empty() && (eg.size() <= g.size() + 1 || eg.compare(0, g.size(), g) != 0 || eg[g.size()] != '/')) continue;
        const size_t base = g.empty() ? 0 : g.size() + 1;
        const size_t sl = eg.find('/', base);
        const std::string child = eg.substr(base, sl == std::string::npos ? std::string::npos : sl - base);
        if (child.empty() || std::find(seen.begin(), seen.end(), child) != seen.end()) continue;
        seen.push_back(child);
        rows.push_back({child + "/", true, -1});
    }
    for (size_t i = 0; i < g_presetList.size(); ++i)
        if (g_presetList[i].group == g) rows.push_back({g_presetList[i].name, false, int(i)});
    return rows;
}

void renderPresets()   // g_presetMx held
{
    namespace U = ::uc1;
    if (!g_presetsOn) return;
    if (!g_presetsBrowse) {
        send(U::buildLcdHeader("PRESETS"));
        send(g_presetsCs ? U::buildTrackNameTripleLarge("", "CHANNEL STRIP", "BUS COMP")
                         : U::buildTrackNameTripleLarge("", "BUS COMP", "CHANNEL STRIP"));
        send(U::buildMenuCommit());
        return;
    }
    send(U::buildCentralMode(U::CentralMode::PresetsSub));
    const std::string& g = g_presetGroup;
    send(U::buildLcdHeader(g.empty() ? (g_presetsCs ? "CHANNEL STRIP " : "BUS COMP ")
                                     : latin1(g.substr(g.find_last_of('/') == std::string::npos ? 0 : g.find_last_of('/') + 1)) + " "));
    auto body = [&](const std::string& a, const std::string& b, const std::string& c, const std::string& d, const std::string& e) {
        send(U::buildPresetListScroll(a, b, c, d, e));
        send(U::buildLcdSubHeader("PRESETS"));
        send(U::buildMenuIndicator08());
        send(U::buildMenuCommit());
    };
    const std::string plugin = g_presetsCs ? g_csPlugin : g_bcPlugin;
    if (plugin.empty()) { body("", "", "<no plug-in>", "", ""); return; }
    if (g_presetListFor != plugin) {
        g_presetList = sslpresets::scan(sslpresets::dirFor(plugin));
        g_presetListFor = plugin;
        g_presetGroup.clear();
        g_presetSel = 0;
        std::printf("UC1 presets: %zu for \"%s\" (%s)\n", g_presetList.size(), plugin.c_str(), sslpresets::dirFor(plugin).c_str());
    }
    if (g_presetList.empty()) { body("", "", "<no presets>", "", ""); return; }
    const auto rows = presetRows();
    if (rows.empty()) { body("", "", "<empty folder>", "", ""); return; }
    const int n = int(rows.size());
    g_presetSel = std::clamp(g_presetSel, 0, n - 1);
    auto slot = [&](int off) { const int i = g_presetSel + off; return (i < 0 || i >= n) ? std::string() : latin1(rows[size_t(i)].label); };
    body(slot(-2), slot(-1), slot(0), slot(1), slot(2));
}

void setPresetsMode(bool on)   // g_presetMx held
{
    namespace U = ::uc1;
    if (g_presetsOn == on) return;
    g_presetsOn = on;
    g_presetsBrowse = false;
    send(U::buildCentralMode(on ? U::CentralMode::Presets : U::CentralMode::Main));
    send(U::buildBcModeDot(!on));
    for (auto& f : U::buildMenuDot(U::kCellPresetsDot, on)) send(f);
    if (on) renderPresets();
    else { paintCentre(); paintSevenSeg(); }
}

// Send each <PARAM id="…" value="…"/> of the file to the script, then the end marker with the preset's name.
void loadPreset(const PresetEntry& e, bool cs)
{
    auto msgs = sslpresets::loadMessages(e, cs ? 0 : 1);
    if (msgs.empty()) { std::printf("UC1 presets: cannot open %s\n", e.path.c_str()); return; }
    for (auto& m : msgs) sendMidi(std::move(m));
    std::printf("UC1 presets: \"%s\" sent (%zu values)\n", e.name.c_str(), msgs.size() - 1);
}

// ---------------------------------------------------------------------------
// ROUTING screen (Rea-Sixty UC1Surface: kRoutingPresets, renderRoutingSubscreen_, the ROUTING encoder handler). The
// SSL channel strips' routing is five switches; the Bus Comp encoder steps through the ten orders of the first four
// (wrapping), and the screen shows the order number. Rea-Sixty also stepped External S/C off / on between orders
// (20 positions, 0x80 in the number), but the UC1 then showed each number twice while the plug-in changed, so
// External S/C is left as it is. The script reports the switches (SysEx 3F) and sets them (3E).
// ---------------------------------------------------------------------------
struct RoutingOrder { uint8_t filtIn, filtSC, eqSC, dynPreEq; };
constexpr RoutingOrder kRoutingOrders[10] = {
    {1, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 0, 1},
    {1, 0, 1, 0}, {1, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 1, 0}, {0, 1, 1, 1},
};
int routingPosition()   // 0..9 (g_presetMx held); a combination set by hand counts as order 1
{
    for (int i = 0; i < 10; ++i) {
        const auto& o = kRoutingOrders[i];
        if (o.filtIn == g_routing[0] && o.filtSC == g_routing[1] && o.eqSC == g_routing[2] && o.dynPreEq == g_routing[3])
            return i;
    }
    return 0;
}
void renderRouting()   // g_presetMx held
{
    if (!g_routingOn) return;
    if (g_routing[0] < 0) { send(::uc1::buildLcdHeader("NO ROUTING")); return; }
    const int pos = routingPosition();
    // The UC1 draws the picture and its number list itself. Its picture 10 is not quite the plug-ins' order 10 (it
    // draws EQ to the side-chain and Dynamics before the Filters; the plug-ins' 10 has Dynamics alone in the path,
    // Filters and EQ to the side-chain). Picture 8 draws that path but renumbers the list to 8, so 10 keeps picture
    // 10: the numbers run 1-10 (Geir's choice).
    send(::uc1::buildRoutingOrderIndicator(uint8_t(pos + 1)));
}
void setRoutingMode(bool on)   // g_presetMx held
{
    namespace U = ::uc1;
    if (g_routingOn == on) return;
    g_routingOn = on;
    send(U::buildCentralMode(on ? U::CentralMode::Routing : U::CentralMode::Main));
    send(U::buildBcModeDot(!on));
    for (auto& f : U::buildMenuDot(U::kCellRoutingDot, on)) send(f);
    if (on) renderRouting();
    else { paintCentre(); paintSevenSeg(); }
}
void routingStep(int step)   // g_presetMx held
{
    if (g_routing[0] < 0) return;
    int pos = (routingPosition() + step) % 10;
    if (pos < 0) pos += 10;
    const auto& o = kRoutingOrders[pos];
    const uint8_t ext = uint8_t(g_routing[4] == 1 ? 1 : 0);   // unchanged
    g_routing[0] = o.filtIn; g_routing[1] = o.filtSC; g_routing[2] = o.eqSC; g_routing[3] = o.dynPreEq; g_routing[4] = ext;
    sysex({0x3E, o.filtIn, o.filtSC, o.eqSC, o.dynPreEq, ext});
    renderRouting();
}

// ---------------------------------------------------------------------------
// EXT FUNCS screen (Rea-Sixty UC1Surface::renderExtFuncsSubscreen_): the script keeps the list and the values and
// sends what to draw (SysEx 4A <prev> 00 <current> 00 <next> 00 <long name> 00 <value> 00 <pos 0-127> <active>);
// the bridge sends it the secondary encoder and the keys while the screen is open (4D <op> <64 + steps>: 1 turn,
// 2 push = scroll / adjust, 3 closed). BACK or CONFIRM closes it.
// ---------------------------------------------------------------------------
bool g_extActiveShown = false;
void setExtMode(bool on)   // g_presetMx held
{
    namespace U = ::uc1;
    if (g_extOn == on) return;
    g_extOn = on;
    g_extActiveShown = false;
    send(U::buildCentralMode(on ? U::CentralMode::ExtFuncs : U::CentralMode::Main));
    send(U::buildBcModeDot(!on));
    if (on) send(U::buildMenuCommit(false));
    else {
        sysex({0x4D, 3, 64});
        paintCentre();
        paintSevenSeg();
    }
}
void renderExt(const std::vector<uint8_t>& m)   // g_presetMx held
{
    namespace U = ::uc1;
    if (!g_extOn) return;
    std::vector<std::string> f;
    size_t i = 5;
    for (int k = 0; k < 5 && i < m.size(); ++k) {
        std::string t;
        while (i < m.size() && m[i] != 0x00 && m[i] != 0xF7) t += char(m[i++]);
        if (i < m.size() && m[i] == 0x00) ++i;
        f.push_back(t);
    }
    if (f.size() < 5 || i + 1 >= m.size()) return;
    const int pos = m[i];
    const bool active = m[i + 1] == 1;
    send(U::buildCentralMode(U::CentralMode::ExtFuncs));   // (SSL 360 re-sends it with every step: a redraw)
    send(U::buildLcdHeader(f[3]));
    send(U::buildTrackNameTripleLarge(f[0], f[1], f[2]));
    send(U::buildLcdValue(f[4]));
    send(U::buildLcdUnit(""));
    send(U::buildLcdRoundIndicator(pos / 127.0));
    if (active != g_extActiveShown) { g_extActiveShown = active; send(U::buildMenuCommit(active)); }
}

// The PRESETS screen's keys and encoder. Returns true if the event was the screen's.
bool presetsButton(uint8_t id, bool pressed)
{
    namespace B = ::uc1::button;
    std::lock_guard<std::mutex> lk(g_presetMx);
    if (id == B::kPresets) { if (pressed) { setExtMode(false); setRoutingMode(false); setPresetsMode(!g_presetsOn); } return true; }
    if (id == B::kRouting) { if (pressed) { setExtMode(false); setPresetsMode(false); setRoutingMode(!g_routingOn); } return true; }
    if (g_extOn) {
        if (id == B::kBack || id == B::kConfirm) { if (pressed) setExtMode(false); return true; }
        if (id == B::kSecEncPush) { if (pressed) sysex({0x4D, 2, 64}); return true; }
        return false;
    }
    if (g_routingOn) {
        if (id == B::kBack || id == B::kConfirm || id == B::kSecEncPush) { if (pressed) setRoutingMode(false); return true; }
        return false;
    }
    if (!g_presetsOn) return false;
    if (id != B::kBack && id != B::kConfirm && id != B::kSecEncPush) return false;
    if (!pressed) return true;
    if (id == B::kBack) {
        if (g_presetsBrowse && !g_presetGroup.empty()) {
            const size_t sl = g_presetGroup.find_last_of('/');
            g_presetGroup = sl == std::string::npos ? std::string() : g_presetGroup.substr(0, sl);
            g_presetSel = 0;
            renderPresets();
        } else if (g_presetsBrowse) {
            g_presetsBrowse = false;
            send(::uc1::buildCentralMode(::uc1::CentralMode::Presets));
            renderPresets();
        } else setPresetsMode(false);
        return true;
    }
    // CONFIRM / encoder push: enter the list from the selector; in the list, into a folder or load.
    if (!g_presetsBrowse) { g_presetsBrowse = true; renderPresets(); return true; }
    const auto rows = presetRows();
    if (rows.empty()) return true;
    const PresetRow& row = rows[size_t(std::clamp(g_presetSel, 0, int(rows.size()) - 1))];
    if (row.folder) {
        std::string child = row.label;
        if (!child.empty() && child.back() == '/') child.pop_back();
        g_presetGroup = g_presetGroup.empty() ? child : g_presetGroup + "/" + child;
        g_presetSel = 0;
        renderPresets();
        return true;
    }
    if (row.entry >= 0) loadPreset(g_presetList[size_t(row.entry)], g_presetsCs);
    setPresetsMode(false);
    return true;
}

bool presetsKnob(uint8_t id, int delta)
{
    std::lock_guard<std::mutex> lk(g_presetMx);
    if (!menuOn()) return false;
    if (id != ::uc1::knob::kBcEncoder) return id == ::uc1::knob::kChannelEncoder;   // CHANNEL does nothing here
    static Accumulator acc;
    const int step = acc.step(delta);
    if (step == 0) return true;
    if (g_routingOn) { routingStep(step); return true; }
    if (g_extOn) { sysex({0x4D, 1, uint8_t(std::clamp(64 + step, 1, 127))}); return true; }
    if (!g_presetsBrowse) { g_presetsCs = !g_presetsCs; renderPresets(); return true; }
    const int n = int(presetRows().size());
    if (n > 0) g_presetSel = std::clamp(g_presetSel + step, 0, n - 1);
    renderPresets();
    return true;
}


void onKnob(const ::uc1::KnobEvent& ev)
{
    if (g_trace) std::printf("[UC1] knob 0x%02X %+d\n", ev.id, ev.delta);
    if (presetsKnob(ev.id, ev.delta)) return;
    if (ev.id == ::uc1::knob::kChannelEncoder) {
        const int steps = g_channelAcc.step(ev.delta);
        for (int k = 0; k < std::abs(steps); ++k) {
            if (settings::trackStepping() == settings::Stepping::MixConsole && uf1StepTrack(steps > 0 ? 1 : -1)) continue;
            // 102 / 103 = Navigate Down / Up: only the tracks the Project window shows (settings.txt).
            const uint8_t n = settings::projectStepping() ? uint8_t(steps > 0 ? 102 : 103) : steps > 0 ? kNoteNextTrack : kNotePrevTrack;
            note(n, true);
            note(n, false);
        }
        return;
    }
    if (ev.id == ::uc1::knob::kBcEncoder) {
        const int steps = g_bcAcc.step(ev.delta);
        if (steps != 0) sysex({0x20, ev.id, uint8_t(std::clamp(64 + steps, 1, 127))});
        return;
    }
    const int d = std::clamp(64 + int(ev.delta), 1, 127);
    sysex({0x20, ev.id, uint8_t(d)});
}

void onButton(const ::uc1::ButtonEvent& ev)
{
    if (g_trace) std::printf("[UC1] button 0x%02X %s\n", ev.id, ev.pressed ? "pressed" : "released");
    namespace B = ::uc1::button;
    if (presetsButton(ev.id, ev.pressed)) return;
    if (ev.id == B::kPolarity || ev.id == B::kSoloClear || ev.id == B::kSolo || ev.id == B::kCut) {
        note(ev.id, ev.pressed);
        return;
    }
    sysex({0x21, ev.id, uint8_t(ev.pressed ? 1 : 0)});
}

// ---------------------------------------------------------------------------
// Gain reduction. SSL's plug-ins stream their meters to SSL 360 Core; the bridge stands in for it (Rea-Sixty's
// SslCoreImpersonator, so SSL 360 must not be running) and reads the channel strip's compressor (CompGain) and gate
// (GateGain) and the bus compressor's gain reduction. Each plug-in announces the track it is on; the script says which
// instances the two sections are on (SysEx 40). The Bus Comp needle: UC1Device's 50 Hz stream; the Dyn and Gate LED
// strips: Rea-Sixty's UC1Surface::pushGainReduction (paired selection / brightness writes, SSL's 3/6/10/14/20 dB
// segments).
// ---------------------------------------------------------------------------
struct GrStrip { uint8_t bri[5] = {0xFE, 0xFE, 0xFE, 0xFE, 0xFE}; uint8_t sel[5] = {0, 0, 0, 0, 0}; };
GrStrip g_compStrip, g_gateStrip;
std::atomic<bool> g_grRun{false};
std::thread g_grThread;

int grSubStep(float dB)
{
    if (dB <= 0) return 0;
    double s;
    if      (dB <=  3.0) s = dB * 2.0;
    else if (dB <=  6.0) s =  6.0 + (dB -  3.0) * 2.0;
    else if (dB <= 10.0) s = 12.0 + (dB -  6.0) * 1.5;
    else if (dB <= 14.0) s = 18.0 + (dB - 10.0) * 1.5;
    else if (dB <= 20.0) s = 24.0 + (dB - 14.0);
    else s = 30.0;
    return std::clamp(int(std::lround(s)), 0, 30);
}
void pushGrStrip(uint8_t baseCell, float dB, GrStrip& st)
{
    static const uint8_t kLevels[6] = {0x03, 0x19, 0x2D, 0x54, 0x99, 0xFF};
    const int sub = grSubStep(dB);
    uint8_t target[5] = {0, 0, 0, 0, 0};
    if (sub >= 30) for (auto& t : target) t = 0xFF;
    else {
        const int active = sub / 6;
        for (int i = 0; i < active; ++i) target[i] = 0xFF;
        target[active] = sub == 0 ? 0x00 : kLevels[sub % 6];
    }
    bool changed = false;
    for (int i = 0; i < 5; ++i) if (target[i] != st.bri[i]) changed = true;
    if (!changed || menuOn()) return;
    for (int i = 0; i < 5; ++i) {
        const uint8_t cell = uint8_t(baseCell + i);
        const uint8_t sel = target[i] ? 1 : 0;
        if (sel == st.sel[i]) continue;
        if (sel) { send(::uc1::buildLedWrite(0x02, cell, 0x00)); send(::uc1::buildLedWrite(0x01, cell, 0x01)); }
        else send(::uc1::buildLedWrite(0x01, cell, 0x00));
        st.sel[i] = sel;
    }
    for (int i = 0; i < 5; ++i) {
        if (target[i] != 0 || st.bri[i] != target[i]) send(::uc1::buildLedWrite(0x02, uint8_t(baseCell + i), target[i]));
        st.bri[i] = target[i];
    }
}

float readGr(int csType, int track, int ordinal, const std::string& model)
{
    if (track <= 0) return 0.0f;
    std::vector<float> v;
    if (!sslcore::getChannelStripMeterForTrackModel(csType, track, model.empty() ? nullptr : model.c_str(), ordinal, v) || v.empty())
        return 0.0f;
    return std::isfinite(v[0]) ? std::fabs(v[0]) : 0.0f;
}

void grLoop()
{
    using namespace std::chrono;
    auto nextLog = steady_clock::now() + seconds(3);
    std::string lastSummary;
    while (g_grRun) {
        std::this_thread::sleep_for(milliseconds(33));
        if (!g_open || !sslcore::isRunning()) continue;
        int csT, csO, bcT, bcO;
        std::string model;
        { std::lock_guard<std::mutex> lk(g_grMx); csT = g_grCsTrack; csO = g_grCsOrd; bcT = g_grBcTrack; bcO = g_grBcOrd; model = g_grCsModel; }
        const int compType = int(sslcore::ChannelStripMeter::CompGain), gateType = int(sslcore::ChannelStripMeter::GateGain);
        const float comp = readGr(compType, csT, csO, model);
        const float gate = readGr(gateType, csT, csO, model);
        float bc = 0.0f;
        if (bcT > 0 && sslcore::getBusCompGr(bcT, bcO, bc)) bc = std::fabs(bc); else bc = 0.0f;
        g_dev.setGainReduction(bc);
        pushGrStrip(0x5C, comp, g_compStrip);
        pushGrStrip(0x61, gate, g_gateStrip);
        if (steady_clock::now() >= nextLog) {
            nextLog = steady_clock::now() + seconds(3);
            stopSslCore();   // the plug-ins start it again when they cannot find a Core
            std::string sum = sslcore::debugSummary();
            // Only when the set of instances or their tracks change (the GR values move all the time).
            std::string key;
            for (size_t a = 0; a < sum.size();) {
                const size_t e = sum.find('\n', a);
                const std::string line = sum.substr(a, e - a);
                if (line.find("streams:") == std::string::npos) key += line.substr(0, line.find(" comp ")) + "\n";
                if (e == std::string::npos) break;
                a = e + 1;
            }
            // The two reports below were for finding the gain-reduction meters; now only with --trace.
            {   // Liveness, every 30 s: how many plug-ins stream, and how long since data / a new connection.
                static int every = 0;
                if (++every % 10 == 0 && g_trace) {
                    int n = 0;
                    for (char c : sum) if (c == '\n') ++n;
                    const long long d = sslcore::msSinceLastData(), c = sslcore::msSinceLastNewConn();
                    std::printf("SSL Core stand-in: %d plug-ins streaming, last data %lld ms ago, last new connection %lld s ago\n",
                                n, d == INT64_MAX ? -1 : d, c == INT64_MAX ? -1 : c / 1000);
                }
            }
            if (sum.empty()) key = sslcore::pluginConnected() ? "connected, no meter data" : "none";
            if (key != lastSummary && g_trace) {
                lastSummary = key;
                std::printf("SSL plug-ins connected (UC1 reads: strip track %d #%d \"%s\", bus comp track %d #%d):\n%s",
                            csT, csO, model.c_str(), bcT, bcO, sum.empty() ? ("  " + key + "\n").c_str() : sum.c_str());
            }
        }
    }
}

bool openDevice(bool quiet)
{
    if (!g_dev.open()) {
        if (!quiet) std::fprintf(stderr, "UC1 not found (%s); it is picked up when it is connected.\n", g_dev.lastError().c_str());
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
    paintSevenSeg();
    return true;
}

} // namespace

bool uc1Connect(bool quiet);
static std::chrono::steady_clock::time_point g_nextTry;   // uc1Poll: when to look for the UC1 again
static bool g_missingSaid = false;                          // "not found" is said once, not every two seconds

// The ports are made whether or not the UC1 is connected (the script
// loads only when its ports are there). The UC1 itself comes and goes: see uc1Poll.
bool uc1Start()
{
    if (!netInit()) std::fprintf(stderr, "Windows sockets could not be started.\n");
    g_missingSaid = !uc1Connect(false);
    g_nextTry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const bool ports = g_out.open(kPortTo) && g_in.open(kPortFrom, &onMidiIn);
    if (!ports) std::fprintf(stderr, "UC1: could not open the MIDI ports \"%s\" / \"%s\".\n", kPortTo, kPortFrom);
    if (!g_grThread.joinable()) { g_grRun = true; g_grThread = std::thread(grLoop); }
    return ports;
}

// The SSL 360 Core stand-in, for the gain-reduction meters (SSL 360 must not be running). Started when a UC1 is
// first connected.
void startSslCore()
{
    static bool tried = false;
    if (tried) return;
    tried = true;
    if (sslcore::isRunning()) return;   // the UF1 started it already (its meter)
    // With --trace: Rea-Sixty's own trace of the SSL plug-in traffic (reaper_sslcore.log) and its object dump
    // (reasixty_ssl_objects.log: every object a plug-in declares and every value it writes), both in cubase\logs.
#ifdef _WIN32
    if (g_trace) _putenv_s("REASIXTY_SSLCORE_TRACE", "1");
#endif
    if (sslcore::start()) {
        std::printf("SSL 360 Core stand-in running (gain reduction).\n");
        if (g_trace) sslcore::setObjectDump(true);
    }
    else std::fprintf(stderr, "SSL 360 Core stand-in could not start (is SSL 360 running?); no gain reduction.\n");
}

void uc1Stop()
{
    g_grRun = false;
    if (g_grThread.joinable()) g_grThread.join();
    sslcore::stop();
    if (!g_open) return;
    uc1NoProject("Bridge off");   // blank, as with no project, before letting go (the name shows 12 characters)
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    g_dev.close();
    g_open = false;
}

void uc1SetTrace(bool on) { g_trace = on; }

void uc1FollowStrip(int kind, int ordinal)
{
    sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x45, uint8_t(kind & 0x7F), uint8_t(ordinal & 0x7F), 0xF7});
}

void uc1FavouriteStep(int domain, int dir)
{
    sendMidi({0xF0, 0x7D, 0x52, 0x36, 0x44, uint8_t(domain ? 1 : 0), uint8_t(dir > 0 ? 1 : 0), 0xF7});
}

// Opens the UC1 and draws the last state Cubase sent (the cache is kept while the UC1 is away). Used at start,
// when it is plugged in, and after it stopped answering. The MIDI ports stay as they are.
bool uc1Connect(bool quiet)
{
    { std::lock_guard<std::mutex> lk(g_ringMx); g_ringShown.clear(); }
    if (!openDevice(quiet)) return false;
    std::printf("UC1 connected (serial %s).\n", g_dev.serial().c_str());
    startSslCore();
    std::map<int, std::vector<unsigned char>> cache;
    { std::lock_guard<std::mutex> lk(g_stateMx); cache = g_cache; }
    for (auto& kv : cache) { if (kv.first == 0x17C) continue; auto msg = kv.second; onMidiIn(msg); }
    if (cache.empty()) {
        // Nothing from Cubase yet (the bridge has just started; Cubase connects to new ports after a while).
        // The name shows 12 characters: "Waiting" there, "for Cubase" in the label above it.
        auto text = [](uint8_t cmd, const char* t) {
            std::vector<unsigned char> m{0xF0, 0x7D, 0x52, 0x36, cmd};
            for (const char* c = t; *c; ++c) { m.push_back(0); m.push_back(0); m.push_back(uint8_t(*c)); }
            m.push_back(0xF7);
            return m;
        };
        auto name = text(0x30, "Waiting");
        auto label = text(0x32, "for Cubase");
        g_ownText = true;
        onMidiIn(name);
        onMidiIn(label);
        g_ownText = false;
        g_waitingShown = true;
    }
    return true;
}

const char* uc1State() { return g_open ? "connected" : "not connected"; }
void uc1SendFavourites() { for (auto& f : favourites::messages()) sendMidi(std::move(f)); }

// From the main loop: a UC1 that stopped answering or was unplugged is let go of, and a missing one is looked
// for every two seconds.
void uc1Poll()
{
    const auto now = std::chrono::steady_clock::now();
    if (g_open) {
        if (!g_dev.needsReopen()) return;
        std::fprintf(stderr, "The UC1 stopped answering or was unplugged (%s); looking for it again.\n", g_dev.lastError().c_str());
        g_open = false;
        g_dev.close();
        g_missingSaid = true;
        g_nextTry = now + std::chrono::seconds(1);
        return;
    }
    if (now < g_nextTry) return;
    g_nextTry = now + std::chrono::seconds(2);
    g_missingSaid = !uc1Connect(g_missingSaid);
}

} // namespace cs

// Rea-Sixty's impersonator names its build in its logs; the bridge has no build id of that kind.
namespace uf8 { const char* reasixtyBuildId() { return "cubase-sixty-bridge"; } }

// Rea-Sixty's UC1Device reports every knob event to a census in its host; the bridge has none.
void reasixty_uc1KnobCensus(int, int, bool) {}
