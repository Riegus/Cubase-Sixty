#pragma once
//
// The UF1's display soft keys in the DAW and SENDS views (14.11.2026). Rea-Sixty lets the user put any action on them;
// a MIDI Remote script cannot run a command picked at run time, so the UF1 script binds a fixed list of Cubase
// commands, each to a note on MIDI channel 3 (CubaseSixty_UF1.js, SOFT_KEY_COMMANDS: the same notes), and the
// setting "uf1-soft-keys" decides what the four keys do:
//   markers (default) = first / previous / next / last marker, labelled;
//   list              = the command chosen for each key (uf1-soft-key-1 .. -4) from this list;
//   mapping           = notes on channel 4 (0-3) that nothing in the script uses: free for Cubase's Mapping Assistant.
// A command is up to three of those notes in a row (the bridge sends them 40 ms apart: Cubase merges notes sent back
// to back). Keep this list and the script's in step: the note numbers are the link.
//

#include <string>
#include <vector>

namespace cs::uf1softkeys {

struct Command {
    const char* key;       // the setting's value
    const char* name;      // the Settings page's text
    const char* label;     // the label above the key (the UF1 shows about 12 characters)
    int steps[3];          // notes on MIDI channel 3, in order; -1 = no more
};

inline const std::vector<Command>& commands()
{
    static const std::vector<Command> c = {
        // The first marker: project start, then the next marker, then back one, so a marker right at the start is
        // not skipped (from the start, "next" passes it). The last: project end, then the previous marker.
        {"first-marker", "First marker", "First Marker", {0, 3, 2}},
        {"prev-marker", "Previous marker", "Prev Marker", {2, -1, -1}},
        {"next-marker", "Next marker", "Next Marker", {3, -1, -1}},
        {"last-marker", "Last marker", "Last Marker", {1, 2, -1}},
        {"insert-marker", "Insert a marker", "Add Marker", {4, -1, -1}},
        {"project-start", "Go to the project start", "Proj Start", {0, -1, -1}},
        {"project-end", "Go to the project end", "Proj End", {1, -1, -1}},
        {"left-locator", "Go to the left locator", "Left Loc", {5, -1, -1}},
        {"right-locator", "Go to the right locator", "Right Loc", {6, -1, -1}},
        {"locators-to-selection", "Locators to the selection", "Loc to Sel", {7, -1, -1}},
        {"prev-event", "Previous event", "Prev Event", {8, -1, -1}},
        {"next-event", "Next event", "Next Event", {9, -1, -1}},
        {"play-selection", "Play the selection range", "Play Sel", {10, -1, -1}},
        {"metronome", "Metronome on / off", "Metronome", {11, -1, -1}},
        {"precount", "Precount on / off", "Precount", {12, -1, -1}},
        {"pre-post-roll", "Pre- / post-roll on / off", "Pre/Post", {13, -1, -1}},
        {"punch-in", "Auto punch in on / off", "Punch In", {14, -1, -1}},
        {"punch-out", "Auto punch out on / off", "Punch Out", {15, -1, -1}},
        {"undo", "Undo", "Undo", {16, -1, -1}},
        {"redo", "Redo", "Redo", {17, -1, -1}},
        {"split", "Split at the cursor", "Split", {18, -1, -1}},
        {"duplicate", "Duplicate", "Duplicate", {19, -1, -1}},
        {"glue", "Glue", "Glue", {20, -1, -1}},
        {"snap", "Snap on / off", "Snap", {21, -1, -1}},
        {"autoscroll", "Autoscroll on / off", "Autoscroll", {22, -1, -1}},
        {"unmute-all", "Unmute all", "Unmute All", {23, -1, -1}},
        {"save", "Save the project", "Save", {24, -1, -1}},
        {"save-version", "Save a new version", "Save Vers", {25, -1, -1}},
        {"export", "Export the audio mixdown", "Export", {26, -1, -1}},
        {"mixconsole", "Open / close the MixConsole", "MixConsole", {27, -1, -1}},
        {"control-room", "Open / close the Control Room", "Ctrl Room", {28, -1, -1}},
        {"vst-instruments", "Open / close the VST Instruments", "VST Instr", {29, -1, -1}},
        {"markers-window", "Open the Markers window", "Markers", {30, -1, -1}},
        {"zoom-full", "Zoom full", "Zoom Full", {31, -1, -1}},
        {"zoom-selection", "Zoom to the selection", "Zoom Sel", {32, -1, -1}},
        {"close-plugins", "Close all plug-in windows", "Close Plugs", {33, -1, -1}},
        {"nothing", "Nothing", "", {-1, -1, -1}},
    };
    return c;
}

inline const Command* find(const std::string& key)
{
    for (const Command& c : commands()) if (key == c.key) return &c;
    return nullptr;
}

// The four keys in the markers setting, and each key's command in the list setting until one is chosen.
inline const char* markerKey(int k) { static const char* const m[4] = {"first-marker", "prev-marker", "next-marker", "last-marker"}; return m[k & 3]; }
inline const char* listDefault(int k) { static const char* const d[4] = {"undo", "redo", "save", "mixconsole"}; return d[k & 3]; }

} // namespace cs::uf1softkeys
