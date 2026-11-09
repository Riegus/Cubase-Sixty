#pragma once
//
// The Settings page: a small web server inside the bridge, on this computer only (127.0.0.1), that serves the page
// (settings-page.html, embedded) and reads / writes cubase\settings.txt and cubase\favourites.txt for it. The tray
// app's "Settings" opens it in the browser. Only the page itself can change anything: every request must carry the
// token the page was served with, and the Host header must be this computer (no other web page can reach it).
//

#include <functional>
#include <string>

namespace cs::settingsServer {

// Starts the server on the first free port from 23660 up; logs "Settings page: http://127.0.0.1:<port>/".
// status: a JSON object with the surfaces' and Cubase's state, shown on the page (Devices).
// favouritesSaved: called after the favourites were written (the bridge passes them on to the UC1 script).
// logsDir: shown on the page (where the logs are).
bool start(std::function<std::string()> status, std::function<void()> favouritesSaved, const std::string& logsDir);
void stop();

} // namespace cs::settingsServer
