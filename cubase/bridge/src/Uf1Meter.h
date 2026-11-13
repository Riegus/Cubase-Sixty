#pragma once
//
// The UF1's METER view (Overview, Analogue, RTA, Loudness): the SSL Meter (Pro) plug-in's bar meters, peak / RMS readouts,
// overload marks and Lissajous, read through the SSL 360 Core stand-in (SslCoreImpersonator) and drawn the
// way Rea-Sixty draws them (uf1MeterScreenBurst_ / uf1PaintMeter_ in extension/src/main.cpp).
//

#include <cstdint>
#include <string>

#include "UF1Device.h"
#include "Uf1Pacer.h"

namespace cs::uf1meter {

// The four screens, as SSL's: 0 Overview, 1 Analogue, 2 RTA, 3 Loudness (Meter Pro only).
const char* screenName(int screen);
// How many screens soft key 1 steps through: 3, or 4 while a Meter Pro streams its loudness.
int screenCount();

// The frames SSL 360 sends when a screen opens (layout 04/0x, soft-key and setting labels).
void sendEntry(::uf1::UF1Device& dev, int screen);
inline void sendOverviewEntry(::uf1::UF1Device& dev) { sendEntry(dev, 0); }

// One cycle of the Analogue, RTA or Loudness screen (all its frames in parts.tail). force = the screen has just
// opened: what is sent only on a change goes out anyway.
void buildOther(int screen, uf1pace::Parts& parts, uint8_t lvL, uint8_t lvR, int trackNo, bool stopped,
                bool resetHolds, bool force);

// One Overview cycle: goniometer image, meters (0009 carries the channel level lvL/lvR), readouts, bars,
// overload mask, 011d. trackNo = the selected track's MixConsole number (the instance to follow, 0 = any),
// stopped = Cubase's transport is stopped, resetHolds = the RESET key was pressed.
void buildOverview(uf1pace::Parts& parts, uint8_t lvL, uint8_t lvR, int trackNo, bool stopped, bool resetHolds);

// Setting cell 1 (0x010e slot 0): the track whose meter is shown, "NO METER" while no instance streams.
std::string instanceLabel();

} // namespace cs::uf1meter
