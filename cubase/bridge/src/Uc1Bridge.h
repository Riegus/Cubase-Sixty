#pragma once
#include <string>
// UC1 side of the bridge: owns the UC1 USB device and its own pair of MIDI ports
// ("Cubase-Sixty UC1 to Cubase" / "... from Cubase"). See Uc1Bridge.cpp.
namespace cs {
// Makes the UC1's ports (always, connected or not) and opens the UC1 if it is there. False if the ports could
// not be made.
bool uc1Start();
// Print every UC1 event and every MIDI message to the console (run the bridge with --trace).
void uc1SetTrace(bool on);
void uc1Stop();
// From the main loop: lets go of a UC1 that stopped answering or was unplugged, and picks up one that is
// connected (tried every two seconds; the last state Cubase sent is drawn on it).
void uc1Poll();
// For the Settings page: "connected" or "not connected".
const char* uc1State();
// The favourites changed (Settings page): on to the UC1 script again (SysEx 43).
void uc1SendFavourites();
// Draws the names again (Settings: Long track names changed).
void uc1RedrawNames();
// Settings changed: brightness (led- / display-brightness), the 7-segment display, the knob step.
void uc1ApplyBrightness();
void uc1RedrawSevenSeg();
void uc1SendKnobStep();
void uc1SendSkipSame();   // TEST: Settings test-skip-same-track
void uc1SendKeyJobs();
// One step through the favourites (domain 0 = channel strips, 1 = bus comps; dir +1 / -1), for the UF8's CHANNEL
// encoder: on to the UC1 script (SysEx 44), which shows and swaps them.
void uc1FavouriteStep(int domain, int dir);
// The UF1's PLUGIN view was moved to a channel strip (kind 1 = SSL, by its ordinal among the track's SSL plug-ins;
// 2 = Cubase's own): the UC1 script goes to the same one (SysEx 45).
void uc1FollowStrip(int kind, int ordinal);
}
