#pragma once
#include <cstdint>
#include <string>
#include <vector>
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
// Why the UF1 needs a reopen, and the reopen itself (keeps the MIDI ports, replays the last state).
std::string uf1LastError();
bool uf1Reopen();
// Defined in main.cpp: one bank step on the UF8 (the UF1's BANK keys use it). False if no UF8 is running.
bool uf8BankPress(bool right);
// Defined in main.cpp: an automation mode key was pressed (note 58-61); updates the UF8's mode LEDs.
void autoModePressed(uint8_t note);
// The UF8 strip (1-8) that holds the selected track, 0 = not on the UF8's current bank. Shown in the UF1's
// channel-number box.
void uf1SetStripNumber(int n);
// Something in the project is soloed (the helper script watches every channel): the UF1 shows SOLO ACTIVE.
void uf1SetSoloActive(bool on);
// A SysEx message for the UF1 script (passed on from the UF8 script: which of several same-named tracks is
// selected, SysEx 1C).
void uf1SendToScript(const std::vector<unsigned char>& msg);
}
