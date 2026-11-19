#pragma once
//
// Cubase's own settings that Cubase-Sixty needs (19.11.2026; the user guide's "Setting up Cubase (once)"), read and,
// with the user's permission, written in Cubase's preferences file (%APPDATA%\Steinberg\Cubase <n>_64\Defaults.xml):
//
//   - "In 'All MIDI Inputs'" off for the ten Cubase-Sixty input ports: the "All MIDI Inputs" group's Excluded >
//     PortIDs list ("I|Windows MIDI|Cubase-Sixty UF8 to Cubase", ...). Ports not yet seen by Cubase can be listed.
//   - The Mackie Control device for SCRUB: "Mackie Control" in the Studio group's DeviceList (Category "Remote
//     Devices"), and the "Mackie Control" group's InputDevice / InputPort / OutputDevice / OutputPort.
//
// Only while Cubase is closed: Cubase writes the whole file when it quits, so a change made while it runs is lost.
// The file is copied first ("Defaults (before Cubase-Sixty).xml", once, beside it), only what is missing is added, and
// a Mackie Control device that is set up for other ports (another controller) is left alone and reported.
//

#include <filesystem>
#include <string>
#include <vector>

namespace cs::cubaseSetup {

enum class Mackie { Missing, Ours, Other, NoStudio };

struct Folder {
    std::string name;               // "Cubase 15_64"
    std::filesystem::path file;     // its Defaults.xml
    int excluded = 0;               // Cubase-Sixty input ports out of "All MIDI Inputs"
    int ports = 10;                 // of these
    Mackie mackie = Mackie::Missing;
    std::string mackieOther;        // the input port of another controller's Mackie Control device
    bool readable = false;
    bool complete() const { return readable && excluded >= ports && (mackie == Mackie::Ours || mackie == Mackie::Other); }
};

// Every Cubase preferences folder of this user (Cubase <n>_64 and other editions), with what is set up in it.
std::vector<Folder> inspect();
// True when Cubase (Cubase.exe, Cubase<n>.exe) is running.
bool cubaseRunning();
// What apply() would change, as lines for a confirmation ("" = nothing to do).
std::string plannedChanges(const std::vector<Folder>& folders);
// Makes the changes (Cubase must be closed); returns what was done, one line per folder.
std::string apply();
// The state as JSON, for the Settings page: {"cubaseRunning":..,"folders":[{"name","excluded","ports","mackie",
// "mackieOther","complete"}],"planned":"..."}.
std::string stateJson();

} // namespace cs::cubaseSetup
