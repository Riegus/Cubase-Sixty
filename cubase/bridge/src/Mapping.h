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
    if (id == 0x00) return kNoteFoot1;
    if (id == 0x01) return kNoteFoot2;
    if (id == 0x78) return kNoteBankLeft;
    if (id == 0x79) return kNoteBankRight;
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

// Which LED a Cubase feedback note drives. cls: 0 solo, 1 cut, 2 sel.
struct LedTarget { int strip; int cls; };
inline std::optional<LedTarget> noteToLed(uint8_t note)
{
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

} // namespace cs
