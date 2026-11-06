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
//                   SysEx 3A <cc> <value>                 send CC <cc> <value> (channel 1) back to Cubase: lets the script
//                                                         set a host value it has bound to a CC (a strip module's on switch)
// The two encoders send one step per detent (Rea-Sixty's accumulator: 3 ticks per step, reset after a 100 ms
// pause or a change of direction): CHANNEL as notes 100 / 101, the Bus Comp encoder as SysEx 20 15 <64 + steps>.
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
#include "ScriptLog.h"

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
std::string g_bcNames[3];                            // the Bus Comp name row: previous, current, next
std::string g_header;                                // the Bus Comp scroll header while it shows
std::chrono::steady_clock::time_point g_headerUntil; // when it goes again
int g_headerGen = 0;
int g_sevenSeg = -1;                                 // the 7-segment number, -1 = blank
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
    auto cut = [](std::string n) { if (n.size() > 12) n.resize(12); return n; };   // the glass shows 12
    return ::uc1::buildTrackNameTripleLarge(cut(g_bcNames[0]), cut(g_bcNames[1]), cut(g_bcNames[2]));
}
// While the Bus Comp scroll header shows, the model field gives way to it (SSL 360: layout 02 + header).
void paintCentre()
{
    std::string name, label, header;
    uint8_t palette;
    bool headerOn;
    {
        std::lock_guard<std::mutex> lk(g_stateMx);
        name = g_name; label = g_label; palette = g_palette; header = g_header;
        headerOn = !header.empty() && std::chrono::steady_clock::now() < g_headerUntil;
    }
    if (name.size() > 12) name.resize(12);
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

void onSysex(const std::vector<unsigned char>& m)
{
    if (m.size() < 6 || m[1] != 0x7D || m[2] != 0x52 || m[3] != 0x36) return;
    if (scriptLogSysex(m.data(), m.size())) return;
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
            send(largeRow());
            send(::uc1::buildDisplayText(m[5], text, text.size()));
        }
        break;
    case 0x34: if (m.size() >= 8) setLed(m[5], m[6]); break;
    case 0x35: send(::uc1::buildDisplayInvalidate(m[5])); break;
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
        send(largeRow());
        break;
    }
    case 0x39: showHeader(decodeName(m, 5)); break;
    case 0x3A: if (m.size() >= 8) sendMidi({0xB0, uint8_t(m[5] & 0x7F), uint8_t(m[6] & 0x7F)}); break;
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
        else if (m.size() >= 6 && m[0] == 0xF0 && m[4] == 0x36) g_cache[0x300 + m[5]] = m;   // per ring
        else if (m.size() >= 6 && m[0] == 0xF0 && m[4] != 0x33 && m[4] != 0x39 && m[4] != 0x3A) g_cache[0x100 + m[4]] = m;
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

void onKnob(const ::uc1::KnobEvent& ev)
{
    if (g_trace) std::printf("[UC1] knob 0x%02X %+d\n", ev.id, ev.delta);
    if (ev.id == ::uc1::knob::kChannelEncoder) {
        const int steps = g_channelAcc.step(ev.delta);
        for (int k = 0; k < std::abs(steps); ++k) {
            const uint8_t n = steps > 0 ? kNoteNextTrack : kNotePrevTrack;
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
    paintSevenSeg();
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
    { std::lock_guard<std::mutex> lk(g_ringMx); g_ringShown.clear(); }
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
