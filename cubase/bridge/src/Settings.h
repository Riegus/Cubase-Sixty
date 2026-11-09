#pragma once
//
// Cubase-Sixty settings: cubase\settings.txt (written with the defaults and their explanations when missing), lines
// "key = value", '#' comments. Read again whenever the file has changed, so an edit needs no restart.
//

#include <string>
#include <vector>

namespace cs::settings {

void init(const char* argv0);
std::string get(const std::string& key, const std::string& fallback);
// track-stepping: which tracks ◀ ▶ and the CHANNEL encoders step through.
//   mixconsole (default) = the ones the MixConsole shows (the UF1 script steps on past hidden ones)
//   project = the ones the Project window shows (Cubase's Navigate Up / Down)
//   all     = every track (Select Track: Prev / Next)
enum class Stepping { MixConsole, Project, All };
Stepping trackStepping();
inline bool projectStepping() { return trackStepping() == Stepping::Project; }

// The settings the Settings page shows (and settings.txt is written from): one entry per key.
struct Choice { std::string value, label; };
struct Def {
    std::string key, section, label, help;
    std::vector<Choice> choices;   // the values it can take (the first is not necessarily the default)
    std::string def;               // the default value
    bool live;                     // takes effect at once (else: when the bridge / Cubase starts again)
};
const std::vector<Def>& schema();
// Sets one value (checked against its choices) and writes settings.txt again. False if the key or value is unknown,
// with the reason in *why.
bool set(const std::string& key, const std::string& value, std::string* why);
// Counts the changes made with set() (code that keeps a setting at hand looks again when it changes).
unsigned generation();
// led-brightness / display-brightness as Brightness.h's level (0 dark .. 4 full).
int brightnessLevel(const std::string& key);

} // namespace cs::settings
