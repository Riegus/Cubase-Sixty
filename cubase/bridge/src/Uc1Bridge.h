#pragma once
#include <string>
// UC1 side of the bridge: owns the UC1 USB device and its own pair of MIDI ports
// ("Cubase-Sixty UC1 to Cubase" / "... from Cubase"). See Uc1Bridge.cpp.
namespace cs {
// Returns true if the UC1 and both ports were opened. Prints the reason if not.
bool uc1Start();
// Print every UC1 event and every MIDI message to the console (run the bridge with --trace).
void uc1SetTrace(bool on);
void uc1Stop();
bool uc1NeedsReopen();
std::string uc1LastError();
// Close and open the UC1 again (keeps the MIDI ports, replays the last state).
bool uc1Reopen();
// One step through the favourites (domain 0 = channel strips, 1 = bus comps; dir +1 / -1), for the UF8's CHANNEL
// encoder: on to the UC1 script (SysEx 44), which shows and swaps them.
void uc1FavouriteStep(int domain, int dir);
}
