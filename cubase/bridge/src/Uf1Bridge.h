#pragma once
#include <cstdint>
#include <string>
#include <vector>
// UF1 side of the bridge: owns the UF1 USB device and its own pair of MIDI ports
// ("Cubase-Sixty UF1 to Cubase" / "... from Cubase"). See Uf1Bridge.cpp.
namespace cs {
// Makes the UF1's ports (always, connected or not) and opens the UF1 if it is there. False if the ports could
// not be made.
bool uf1Start();
// Print every UF1 event and every MIDI message to the console (run the bridge with --trace).
void uf1SetTrace(bool on);
// Lights each UF1 LED we drive in turn (call a few seconds after start-up, once the init is done).
void uf1LampTest();
void uf1Stop();
// From the main loop: lets go of a UF1 that stopped answering or was unplugged, and picks up one that is
// connected (tried every two seconds; the last state Cubase sent is drawn on it).
void uf1Poll();
// For the Settings page: "connected" or "not connected".
const char* uf1State();
// Draws the track name again (Settings: Long track names changed).
void uf1RedrawName();
// Settings changed: led- / display-brightness.
void uf1ApplyBrightness();
// Cubase's transport plays or records (keeps the surfaces awake: Sleep when idle).
bool uf1TransportRunning();
// Defined in main.cpp: one bank step on the UF8 (the UF1's BANK keys use it). False if no UF8 is running.
bool uf8BankPress(bool right);
// Defined in main.cpp: BANK without a UF8: the UF8 script banks, and the track on the same strip is selected.
void uf8BankSelect(bool right);
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
// The fader position (Cubase's 14-bit volume value) of 0 dB, learnt from the faders: a level text of 0.00 dB with
// its position (UF8 and UF1). Kept in cubase\fader-0db.txt. -1 until known. Used to park the faders at 0 dB when a
// project closes or the bridge stops.
void fader0dbSeen(const std::string& text, int pos14);
int fader0dbPos();
std::string uf1LevelText(int pos14);                          // "" where the curve is not known
void uf1LearnLevel(int pos14, const std::string& text);       // Cubase's text for a position ("-6.02 dB")
// One track back / forward (dir -1 / +1) past the tracks hidden in the MixConsole (track-stepping = mixconsole): the
// UF1 script steps and steps again while the new track is hidden (SysEx 5C), whether or not the UF1 is connected.
// False if its ports could not be made.
bool uf1StepTrack(int dir);
}
