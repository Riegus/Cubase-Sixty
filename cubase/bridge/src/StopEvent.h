#pragma once
// The bridge stops when another program sets this named event (the tray app does, on Quit and Restart bridge):
// it blanks the surfaces and closes them first, which killing the process cannot do.
namespace cs {
constexpr const wchar_t* kStopEventName = L"Local\\CubaseSixtyBridgeStop";
void stopEventInit();
bool stopRequested();
}
