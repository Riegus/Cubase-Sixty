#include "Mapping.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

int main()
{
    using namespace cs;
    CHECK(pbToMidi14(0) == 0);
    CHECK(pbToMidi14(kUf8PbMax) == 16383);
    CHECK(midi14ToPb(16383) == kUf8PbMax);
    CHECK(midi14ToPb(0) == 0);
    for (uint16_t pb = 0; pb <= kUf8PbMax; pb += 7) {   // round trip within one step
        const int back = midi14ToPb(pbToMidi14(pb));
        CHECK(std::abs(back - int(pb)) <= 1);
    }
    CHECK(*buttonToNote(0x20) == kNoteSolo);
    CHECK(*buttonToNote(0x21) == kNoteCut);
    CHECK(*buttonToNote(0x22) == kNoteSel);
    CHECK(*buttonToNote(0x23) == kNoteSolo + 1);
    CHECK(*buttonToNote(0x37) == kNoteSel + 7);
    CHECK(*buttonToNote(0x08) == kNoteVPush);
    CHECK(*buttonToNote(0x78) == 46 && *buttonToNote(0x79) == 47);
    CHECK(!buttonToNote(0x40));
    CHECK(vpotToCc(1) == 1 && vpotToCc(-1) == 65 && vpotToCc(-31) == 95 && vpotToCc(0) == 0);
    CHECK(noteToLed(10)->strip == 2 && noteToLed(10)->cls == 0);
    CHECK(noteToLed(31)->strip == 7 && noteToLed(31)->cls == 2);
    CHECK(!noteToLed(40));
    auto n = parseName({0xF0, 0x7D, 0x52, 0x36, 0x01, 3, 'B', 'a', 's', 's', 0xF7});
    CHECK(n && n->strip == 3 && n->text.size() == 4);
    CHECK(!parseName({0xF0, 0x7D, 0x52, 0x36, 0x01, 9, 'x', 0xF7}));
    auto c = parseColour({0xF0, 0x7D, 0x52, 0x36, 0x02, 1, 127, 0, 0, 0xF7});
    CHECK(c && c->strip == 1 && c->r == 127);
    CHECK(colourToPalette(127, 0, 0) <= 15);
    std::puts("all mapping tests passed");
    return 0;
}
