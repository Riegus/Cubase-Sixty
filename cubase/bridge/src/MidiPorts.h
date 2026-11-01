#pragma once
//
// The bridge's two MIDI ports per surface ("... to Cubase": the bridge writes, "... from Cubase": the bridge reads).
//
// On Windows the bridge creates the ports itself with loopMIDI's driver library (teVirtualMIDI64.dll, installed
// with loopMIDI). Cubase then sees the ports appear when the bridge starts, activates the MIDI Remote scripts at
// that moment, and sends the current state (names, colours, faders, LEDs) to a bridge that is already listening.
// With --loopmidi, or if the library is missing or cannot create a port, the bridge uses ports made in loopMIDI
// instead (found by name through RtMidi).
//

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cs {

using MidiMessageCallback = std::function<void(std::vector<unsigned char>&)>;

// true: use ports made in loopMIDI. false (the default): create the ports with loopMIDI's driver library; the
// bridge falls back to loopMIDI's own ports if that fails.
void midiPortsPreferLoopMidi(bool on);

// Print what the virtual ports receive and the first sends (for --trace).
void midiPortsTrace(bool on);

// True if at least one port was created by the bridge itself.
bool midiPortsAreVirtual();

class MidiOutPort {
public:
    MidiOutPort();
    ~MidiOutPort();
    // Returns false (and prints why) if the port could not be created or found.
    bool open(const std::string& name);
    void send(std::vector<unsigned char>& msg);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MidiInPort {
public:
    MidiInPort();
    ~MidiInPort();
    // The callback runs on a driver thread. SysEx arrives as one message.
    bool open(const std::string& name, MidiMessageCallback cb);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cs
