// Windows sockets must be started once per process before any socket call (WSAStartup). Rea-Sixty runs inside REAPER,
// which has already done it; the bridge is its own process, so it does it here for the SSL 360 Core stand-in.
#ifdef _WIN32
#include <winsock2.h>
#endif

namespace cs {
bool netInit()
{
#ifdef _WIN32
    static bool done = false;
    if (done) return true;
    WSADATA wsa;
    done = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    return done;
#else
    return true;
#endif
}
}
