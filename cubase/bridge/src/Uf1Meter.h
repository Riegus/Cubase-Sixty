#pragma once
//
// The UF1's METER view, Overview screen: the SSL Meter (Pro) plug-in's bar meters, peak / RMS readouts,
// overload marks and Lissajous, read through the SSL 360 Core stand-in (SslCoreImpersonator) and drawn the
// way Rea-Sixty draws them (uf1MeterScreenBurst_ / uf1PaintMeter_ in extension/src/main.cpp).
//

#include <cstdint>
#include <string>

#include "UF1Device.h"
#include "Uf1Pacer.h"

namespace cs::uf1meter {

// The frames SSL 360 sends when the Overview screen opens (layout 04/00, soft-key and setting labels).
void sendOverviewEntry(::uf1::UF1Device& dev);

// One Overview cycle: goniometer image, meters (0009 carries the channel level lvL/lvR), readouts, bars,
// overload mask, 011d. trackNo = the selected track's MixConsole number (the instance to follow, 0 = any),
// stopped = Cubase's transport is stopped, resetHolds = the RESET key was pressed.
void buildOverview(uf1pace::Parts& parts, uint8_t lvL, uint8_t lvR, int trackNo, bool stopped, bool resetHolds);

// Setting cell 1 (0x010e slot 0): the track whose meter is shown, "NO METER" while no instance streams.
std::string instanceLabel();

} // namespace cs::uf1meter
