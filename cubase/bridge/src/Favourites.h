#pragma once
//
// The favourites (Rea-Sixty's CS / BC favourites): the channel strips and bus comps the UF8's CHANNEL encoder steps
// through in its CS Fav / BC Fav modes. Listed in cubase\favourites.txt (written with a default list when missing),
// read again each time the UC1 script asks (SysEx 42), and sent to it as SysEx 43 <0 = CS, 1 = BC> <names, 3 x 7 bits
// per character, 7F 7F 7F between them>. The script does the swapping (see CubaseSixty_UC1.js, "Favourites").
//

#include <string>
#include <vector>

namespace cs::favourites {

// Where the file is: cubase\favourites.txt, next to cubase\logs.
void init(const char* argv0);
// The two SysEx messages, from the file as it is now.
std::vector<std::vector<unsigned char>> messages();
// The two lists as the file has them now (0 = channel strips, 1 = bus comps; up to eight each).
std::vector<std::string> list(int domain);
// Writes both lists (empty names are left out, eight at most each), keeping the file's explanation at the top.
bool write(const std::vector<std::string>& strips, const std::vector<std::string>& busComps, std::string* why);
// The plug-ins the UC1 knows (their settings are carried across a swap), for the Settings page's suggestions.
const std::vector<std::string>& known(int domain);

} // namespace cs::favourites
