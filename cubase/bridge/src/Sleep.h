#pragma once
//
// Sleep when idle (Settings: Devices > Sleep when idle): after the set minutes without a hand on any surface, and
// with Cubase's transport stopped, the surfaces go dark (keys, LEDs, displays; Rea-Sixty's SurfaceSleep frames). The
// first touch, turn or press on any surface wakes them all, and that event itself is dropped (a hand reaching for a
// dark panel must not solo a track it cannot see); a key's release goes with its press.
//
#include <cstdint>
#include <functional>

namespace cs::sleep {

// Each surface's input handler calls this for every user event. arrives: the event can wake (a touch, a turn, a
// press; a fader position report only counts as activity). True: this event woke the surfaces, drop it.
// what: for bridge.log ("UF8 key"...), which input kept the surfaces awake last.
bool activity(bool arrives, const char* what);
// For keys: true when this press or release is to be dropped (the press that woke, and its release).
// surface: 0 UF8, 1 UF1, 2 UC1.
bool key(int surface, uint8_t id, bool pressed, const char* what);
// From the main loop: the countdown (busy = the transport runs). push(asleep) darkens or lights the surfaces.
void tick(bool busy, const std::function<void(bool)>& push);
bool asleep();

} // namespace cs::sleep
