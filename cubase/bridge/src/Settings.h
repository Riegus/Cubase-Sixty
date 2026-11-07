#pragma once
//
// Cubase-Sixty settings: cubase\settings.txt (written with the defaults and their explanations when missing), lines
// "key = value", '#' comments. Read again whenever the file has changed, so an edit needs no restart.
//

#include <string>

namespace cs::settings {

void init(const char* argv0);
std::string get(const std::string& key, const std::string& fallback);
// track-stepping: which tracks ◀ ▶ and the CHANNEL encoders step through.
//   mixconsole (default) = the ones the MixConsole shows (the UF1 script steps on past hidden ones; needs the UF1)
//   project = the ones the Project window shows (Cubase's Navigate Up / Down)
//   all     = every track (Select Track: Prev / Next)
enum class Stepping { MixConsole, Project, All };
Stepping trackStepping();
inline bool projectStepping() { return trackStepping() == Stepping::Project; }

} // namespace cs::settings
