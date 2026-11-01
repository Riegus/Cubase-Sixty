#pragma once
//
// Wire mapping between the UF8 and the Cubase MIDI Remote script.
// Pure functions, no USB and no MIDI port: see ../tests/test_mapping.cpp.
// The numbering is the one in cubase/docs/bridge-midi-protocol.md.
//

#include <cstdint>
#include <optional>
#include <vector>

#include "Palette.h"
#include "UF1Protocol.h"

namespace cs {

constexpr int kStrips = 8;

// UF8 fader range (uf8fader::kPbMax) <-> 14-bit MIDI pitch bend.
constexpr uint16_t kUf8PbMax = 15583;

inline uint16_t pbToMidi14(uint16_t pb)
{
    if (pb > kUf8PbMax) pb = kUf8PbMax;
    return static_cast<uint16_t>((uint32_t(pb) * 16383u + kUf8PbMax / 2) / kUf8PbMax);
}
inline uint16_t midi14ToPb(uint16_t v)
{
    if (v > 16383) v = 16383;
    return static_cast<uint16_t>((uint32_t(v) * kUf8PbMax + 8191u) / 16383u);
}

// Notes (channel 0). Same numbers as the script.
constexpr uint8_t kNoteSolo = 8, kNoteCut = 16, kNoteSel = 24, kNoteVPush = 32;
constexpr uint8_t kNoteTouch = 104, kNoteBankLeft = 46, kNoteBankRight = 47;
constexpr uint8_t kNoteTopSoft = 48;   // 48..55: top soft keys 1..8 (channel editor)
constexpr uint8_t kNoteNoRec = 64;     // 64..71 (from the script): strip 1..8 cannot be record-armed
constexpr uint8_t kCcMeter = 80;       // CC 80 + strip (from the script): meter level 0..127
constexpr uint8_t kNotePageLeft = 44, kNotePageRight = 45;
constexpr uint8_t kNoteZoomPad = 72;   // 72..76: UF8 zoom pad up, left, centre, right, down
constexpr uint8_t kNoteAutoOff = 58, kNoteAutoTrim = 59, kNoteAutoLatch = 60, kNoteAutoTouch = 61;
constexpr uint8_t kNoteAutoRead = 56, kNoteAutoWrite = 57;
constexpr uint8_t kNoteFoot1 = 62, kNoteFoot2 = 63;
constexpr uint8_t kCcVPot = 16;

// What a UF8 button id (FF 22 03 <id> 00 <state>) means on the wire.
// Per-strip ids: V-pot push 0x08..0x0F, SOLO/CUT/SEL 0x20..0x37 (3 per strip),
// top soft keys 0x18..0x1F, automation Read/Write 0x59/0x5A, foot switches 0x00/0x01,
// bank 0x78 / 0x79 (as in Rea-Sixty's main.cpp). The UF8 has no transport keys.
inline std::optional<uint8_t> buttonToNote(uint8_t id)
{
    if (id >= 0x08 && id <= 0x0F) return uint8_t(kNoteVPush + (id - 0x08));
    if (id >= 0x20 && id <= 0x37) {
        const int strip = (id - 0x20) / 3;
        switch ((id - 0x20) % 3) {
            case 0: return uint8_t(kNoteSolo + strip);
            case 1: return uint8_t(kNoteCut + strip);
            default: return uint8_t(kNoteSel + strip);
        }
    }
    if (id >= 0x18 && id <= 0x1F) return uint8_t(kNoteTopSoft + (id - 0x18));
    if (id == 0x59) return kNoteAutoRead;
    if (id == 0x5A) return kNoteAutoWrite;
    if (id == 0x58) return kNoteAutoOff;     // Off key = Cross-Over
    if (id == 0x5B) return kNoteAutoTrim;    // automation modes: Trim, Latch, Touch
    if (id == 0x5C) return kNoteAutoLatch;
    if (id == 0x5D) return kNoteAutoTouch;
    if (id == 0x00) return kNoteFoot1;
    if (id == 0x01) return kNoteFoot2;
    if (id == 0x52) return kNotePageLeft;    // Page left / right: shift the bank by one channel
    if (id == 0x53) return kNotePageRight;
    if (id == 0x78) return kNoteBankLeft;
    if (id == 0x79) return kNoteBankRight;
    if (id >= 0x7A && id <= 0x7E) return uint8_t(kNoteZoomPad + (id - 0x7A));   // zoom pad: up, left, centre, right, down
    return std::nullopt;
}

// Relative V-pot encoding for the script: signed-bit, 1..63 up, 65..127 down.
inline uint8_t vpotToCc(int delta)
{
    if (delta == 0) return 0;
    if (delta > 63) delta = 63;
    if (delta < -63) delta = -63;
    return delta > 0 ? uint8_t(delta) : uint8_t(64 - delta);
}

// Which LED a Cubase feedback note drives.
// cls: 0 solo, 1 cut, 2 sel (per strip), 3 top soft key (per strip: channel editor open),
//      4 automation Read, 5 automation Write (global, strip = 0).
struct LedTarget { int strip; int cls; };
inline std::optional<LedTarget> noteToLed(uint8_t note)
{
    if (note >= kNoteTopSoft && note < kNoteTopSoft + kStrips) return LedTarget{note - kNoteTopSoft, 3};
    if (note == kNoteAutoRead)  return LedTarget{0, 4};
    if (note == kNoteAutoWrite) return LedTarget{0, 5};
    if (note >= kNoteSolo && note < kNoteSolo + kStrips) return LedTarget{note - kNoteSolo, 0};
    if (note >= kNoteCut  && note < kNoteCut  + kStrips) return LedTarget{note - kNoteCut, 1};
    if (note >= kNoteSel  && note < kNoteSel  + kStrips) return LedTarget{note - kNoteSel, 2};
    return std::nullopt;
}

// SysEx: F0 7D 52 36 01 <strip> <ascii...> F7   (name)
//        F0 7D 52 36 02 <strip> <r> <g> <b> F7  (colour, 0..127 per channel)
struct SysexName   { int strip; std::vector<uint8_t> text; };
struct SysexColour { int strip; uint8_t r, g, b; };

inline bool isOurSysex(const std::vector<uint8_t>& m)
{
    return m.size() >= 7 && m[0] == 0xF0 && m[1] == 0x7D && m[2] == 0x52 && m[3] == 0x36 &&
           m.back() == 0xF7;
}
inline std::optional<SysexName> parseName(const std::vector<uint8_t>& m)
{
    if (!isOurSysex(m) || m[4] != 0x01 || m[5] >= kStrips) return std::nullopt;
    SysexName n{m[5], {}};
    for (size_t i = 6; i + 1 < m.size(); ++i) n.text.push_back(m[i]);
    return n;
}
inline std::optional<SysexColour> parseColour(const std::vector<uint8_t>& m)
{
    if (!isOurSysex(m) || m[4] != 0x02 || m.size() != 10 || m[5] >= kStrips) return std::nullopt;
    return SysexColour{m[5], m[6], m[7], m[8]};
}

// 0..127 colour -> UF8 palette index.
inline uint8_t colourToPalette(uint8_t r, uint8_t g, uint8_t b)
{
    auto up = [](uint8_t v) { return uint8_t((unsigned(v) * 255u + 63u) / 127u); };
    return uf8::quantize(uf8::Rgb{up(r), up(g), up(b)});
}

// ---------------------------------------------------------------------------
// UF1 (one channel strip, so everything is on MIDI channel 0)
// Same note numbers as the UF8's strip 1, plus transport and encoders.
// ---------------------------------------------------------------------------
namespace uf1m {

constexpr uint8_t kNoteRwd = 91, kNoteFfw = 92, kNoteStop = 93, kNotePlay = 94, kNoteRec = 95;
constexpr uint8_t kNoteCycle = 86, kNoteClick = 87;
constexpr uint8_t kNoteFaderTouch = 104;
constexpr uint8_t kCcEncoder = 16;   // 16 + encoder id (0 above-fader, 1..4 V-pots, 5 channel, 6 jog)

inline std::optional<uint8_t> buttonToNote(uint8_t id)
{
    switch (id) {
        case 0x1D: return kNoteSolo;
        case 0x1E: return kNoteCut;
        case 0x1F: return kNoteSel;
        case 0x3A: return kNoteRwd;
        case 0x3B: return kNoteFfw;
        case 0x3C: return kNoteStop;
        case 0x3D: return kNotePlay;
        case 0x3E: return kNoteRec;
        case 0x32: return kNoteCycle;
        case 0x33: return kNoteClick;
        case 0x21: return kNoteBankLeft;
        case 0x23: return kNoteBankRight;
        case 0x28: return 48;   // NAV up
        case 0x29: return 49;   // NAV left
        case 0x2A: return 50;   // NAV centre
        case 0x2B: return 51;   // NAV right
        case 0x2C: return 52;   // NAV down
        default: break;
    }
    if (id >= 0x08 && id <= 0x0D) return uint8_t(kNoteVPush + (id - 0x08));   // pushes 32..37
    return std::nullopt;
}

// Feedback note -> UF1 LED element id and the colour it lights in (0xRRGGBB).
struct Led { uint8_t id; uint32_t rgb; };
inline std::optional<Led> noteToLed(uint8_t note)
{
    switch (note) {
        case kNoteSolo:  return Led{0x05, 0xFFFF00};
        case kNoteCut:   return Led{0x06, 0xFF0000};
        case kNoteRwd:   return Led{0x22, 0xFFFFFF};
        case kNoteFfw:   return Led{0x23, 0xFFFFFF};
        case kNoteStop:  return Led{0x24, 0xFFFFFF};
        case kNotePlay:  return Led{0x25, 0x00FF00};
        case kNoteRec:   return Led{0x26, 0xFF0000};
        case kNoteCycle: return Led{0x1A, 0xFFFF00};
        case kNoteClick: return Led{0x1B, 0xFFFF00};
        default: return std::nullopt;
    }
}

// UF1 fader: 15-bit with end snap <-> 14-bit MIDI pitch bend.
inline uint16_t pos15ToMidi14(uint16_t pos)
{
    const double n = ::uf1::faderPosToNorm(pos);
    return static_cast<uint16_t>(n * 16383.0 + 0.5);
}
inline uint16_t midi14ToPos15(uint16_t v)
{
    if (v > 16383) v = 16383;
    return static_cast<uint16_t>((uint32_t(v) * ::uf1::kFaderMax + 8191u) / 16383u);
}

} // namespace uf1m

} // namespace cs
