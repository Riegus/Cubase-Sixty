#pragma once
//
// SSL's preset library on disk, shared by the UC1's PRESETS screen and the UF1's preset browser. SSL's plug-ins show
// the host no presets; their library is one folder per plug-in under %ProgramData%\Solid State Logic\PlugIns\Presets
// (Rea-Sixty's SslPresetLibrary). A preset is an XML file of <PARAM id="…" value="…"/>; loading one = sending each
// id / value to the Cubase script (SysEx 3C <section> <id> 7F 7F 7F <value>, then 3D <section> <name>), which sets
// the parameters through Direct Access.
//

#include <string>
#include <vector>

namespace cs::sslpresets {

struct Entry { std::string name, group, path; };   // group = sub-folder path ("" = the plug-in's folder)

// The plug-in's preset folder, from its name ("SSL Native Channel Strip 2" -> ...\ChannelStrip2); "" if unknown.
std::string dirFor(const std::string& plugin);
// Every preset under dir (sub-folders included), in SSL 360's order (natural, case ignored).
std::vector<Entry> scan(const std::string& dir);
// UTF-8 -> Latin-1, the displays' one byte per glyph ('?' for what does not fit).
std::string latin1(const std::string& utf8);
// The SysEx messages that load e into the script's section (0 = channel strip, 1 = bus comp); empty if unreadable.
std::vector<std::vector<unsigned char>> loadMessages(const Entry& e, unsigned char section);

} // namespace cs::sslpresets
