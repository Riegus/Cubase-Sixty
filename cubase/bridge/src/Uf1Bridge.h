#pragma once
// UF1 side of the bridge: owns the UF1 USB device and its own pair of MIDI ports
// ("Cubase-Sixty UF1 to Cubase" / "... from Cubase"). See Uf1Bridge.cpp.
namespace cs {
// Returns true if the UF1 and both ports were opened. Prints the reason if not.
bool uf1Start();
// Print every UF1 event and every MIDI message to the console (run the bridge with --trace).
void uf1SetTrace(bool on);
// Lights each UF1 LED we drive in turn (call a few seconds after start-up, once the init is done).
void uf1LampTest();
void uf1Stop();
bool uf1NeedsReopen();
}
