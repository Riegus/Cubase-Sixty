#pragma once
#include <string>
// The MIDI Remote scripts' log lines, sent to the bridge as SysEx 7E, written to a file (cubase\logs\scripts.log)
// so nobody has to copy them out of Cubase's script console.
namespace cs {
// Opens the log next to the bridge's folder (cubase\logs\scripts.log); the previous one becomes scripts.previous.log.
void scriptLogInit(const char* argv0);
// SysEx F0 7D 52 36 7E <source> <text, 3 x 7 bits per character> F7. Source 1 UF8, 2 UF1, 3 UC1, 4 helper.
// Returns true if msg was a log line (and has been written).
bool scriptLogSysex(const unsigned char* msg, size_t len);
// A line from the bridge itself.
void scriptLogLine(const std::string& source, const std::string& text);
}
