// A polite way to stop the bridge from outside (the tray app's Quit / Restart): a named event the main loop
// looks at. On Windows only; elsewhere nothing asks.
#include "StopEvent.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cs {
namespace { HANDLE g_stop = nullptr; }
void stopEventInit()
{
    // Manual reset, starts clear. ResetEvent too: an earlier bridge may have left it set.
    g_stop = CreateEventW(nullptr, TRUE, FALSE, kStopEventName);
    if (g_stop) ResetEvent(g_stop);
}
bool stopRequested() { return g_stop && WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0; }
}
#else
namespace cs {
void stopEventInit() {}
bool stopRequested() { return false; }
}
#endif
