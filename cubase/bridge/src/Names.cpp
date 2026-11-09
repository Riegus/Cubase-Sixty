#include "Names.h"

#include "Settings.h"
#include "TrackName.h"   // Rea-Sixty's abbreviation (extension/src)

#include <atomic>
#include <chrono>

// (TrackName.cpp defines its own mode switch, g_trackNameMode; fitName passes the mode with each call.)

namespace cs {

std::string fitName(const std::string& name, int width)
{
    if (int(name.size()) <= width) return name;
    // The setting, looked at once a second at most (names come in bursts when banking).
    // (A change on the Settings page is seen at once; an edit of the file within a second.)
    thread_local auto checked = std::chrono::steady_clock::time_point{};
    thread_local unsigned gen = ~0u;
    thread_local bool abbreviate = true;
    const auto now = std::chrono::steady_clock::now();
    if (now - checked > std::chrono::seconds(1) || gen != settings::generation()) {
        checked = now;
        gen = settings::generation();
        abbreviate = settings::get("track-names", "abbreviate") != "cut";
    }
    if (!abbreviate) return name.substr(0, size_t(width));
    std::string s = abbreviateTrackName_(name, width, TNM_SmartAbbrev);
    if (int(s.size()) > width) s.resize(size_t(width));
    return s;
}

} // namespace cs
