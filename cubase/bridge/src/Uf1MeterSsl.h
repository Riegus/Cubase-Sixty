#pragma once
//
// The UF1's METER view for a Meter that Cubase cannot reach (one in the Control Room's inserts): the plug-in's
// settings read and written over SSL's own protocol (the SSL 360 Core stand-in, sslcore::meterObjGet / meterObjWrite)
// instead of through the UF1 script's Direct Access. Same pages, labels and steps as the script's (METER_PAGES in
// CubaseSixty_UF1.js); the value texts are made here, as the plug-in's own window shows them.
//

#include <string>

namespace cs::uf1meterssl {

struct Cell { bool nameOnly = false; std::string label, value; };

// Whether the Meter the view shows can be set this way (it has a connection that declared its settings).
bool available();
// The three V-pot cells (2-4) of a screen's page.
void cells(int screen, int page, Cell out[3]);
// A V-pot turn (slot 0-2 = V-pots 2-4), in notches: an enum or switch one value a notch, a number its step (a
// quarter with FINE).
void turn(int screen, int page, int slot, int notches, bool fine);
// Loudness Play / Pause, and whether it plays.
void togglePlay();
bool playing();
// The settings the screens are drawn with, to uf1meter::setSetting (rows as the script's METER_DRAW_ROWS).
void publishSettings();

} // namespace cs::uf1meterssl
