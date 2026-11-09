#pragma once
// Track names on the surfaces' displays: a name longer than the display has room for is abbreviated (Rea-Sixty's
// "Smart abbreviate": "Background Vocals" -> "BckgrVcl") or cut, as the Settings page's "Long track names" says
// (track-names = abbreviate (default) / cut). Widths: UF8 strips 8, UF1 name 8, UC1 names 12.
#include <string>

namespace cs {
std::string fitName(const std::string& name, int width);
}
